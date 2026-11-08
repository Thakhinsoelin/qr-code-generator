/**
 * Raven - core/heap.c
 * The allocator of the library.
 *
 * Memory comes from the OS in regions of 4 MB (mmap / VirtualAlloc) and is
 * handed out by a TLSF heap ("two-level segregated fit", Masmano et al.):
 *
 *   - every block has a 16-byte header: the block before it in the region
 *     and its size. Two neighbours that are free are always merged, so the
 *     heap does not crumble into small pieces over time;
 *   - free blocks are sorted by size into 23 x 32 lists (powers of two, each
 *     divided in 32 steps). Two bitmaps tell which lists are not empty, so
 *     finding a block of a given size is two bit scans: allocating and
 *     freeing take a constant time whatever the state of the heap;
 *   - a request is rounded up by 3 % at most to find its list.
 *
 * Requests of 1 MB and more (pixel buffers, large draw lists) go straight
 * to the OS and return to it when they are freed. A region that becomes
 * empty returns to the OS as well, except one that is kept for the next
 * allocations.
 *
 * One lock protects the heap. It is a spin lock: the UI thread makes nearly
 * all the allocations, and the critical sections are a few dozen instructions.
 */

#include "heap.h"
#include "thread.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#define RVN_HEAP_MMAP 1
#endif

/* ---------------------------------------------------------------------------
 *  Layout
 * ------------------------------------------------------------------------- */

#define RVN_HEAP_ALIGN ((size_t)16)
/** Bytes between the start of a block and its payload. */
#define RVN_HEAP_HEADER ((size_t)16)
/** A free block keeps its two list links in its payload. */
#define RVN_HEAP_MIN_PAYLOAD ((size_t)16)

#define RVN_SL_LOG2 5
#define RVN_SL_COUNT (1 << RVN_SL_LOG2)
/** Below 2^9 = 512 bytes, the lists are 16 bytes apart. */
#define RVN_FL_SHIFT (RVN_SL_LOG2 + 4)
#define RVN_SMALL_SIZE ((size_t)1 << RVN_FL_SHIFT)
/** Blocks are smaller than 2^31 bytes. */
#define RVN_FL_MAX 31
#define RVN_FL_COUNT (RVN_FL_MAX - RVN_FL_SHIFT + 1)

/** Larger requests are refused: they could not be rounded up without overflowing. */
#define RVN_HEAP_MAX_REQUEST (((size_t)-1) / 2)

#define RVN_REGION_SIZE ((size_t)4 << 20)
#define RVN_HUGE_SIZE ((size_t)1 << 20)

#define RVN_BLOCK_FREE ((size_t)1)
#define RVN_BLOCK_HUGE ((size_t)2)
#define RVN_BLOCK_FLAGS ((size_t)15)

typedef struct rvn_block_t {
	/** The block before this one in its region, NULL for the first. For a huge block: bytes taken from the OS. */
	union {
		struct rvn_block_t* prev_phys;
		size_t os_size;
		/* keeps the payload on 16 bytes where pointers are 4 bytes */
		char align_[8];
	} u;
	/** Bytes of payload, a multiple of 16, with the flags in the low bits. */
	union {
		size_t size;
		char align_[8];
	} s;
	/* the payload starts here; while the block is free it holds the links of its list */
	struct rvn_block_t* next_free;
	struct rvn_block_t* prev_free;
} rvn_block_t;

/** Start of the memory taken from the OS; the first block follows. */
typedef struct rvn_region_t {
	struct rvn_region_t* next;
	struct rvn_region_t* prev;
	size_t os_size;
	char align_[32 - 2 * sizeof(void*) - sizeof(size_t)];
} rvn_region_t;

/* the layout above is what the code below relies on */
typedef char rvn_heap_header_is_16_bytes[(offsetof(rvn_block_t, next_free) == 16) ? 1 : -1];
typedef char rvn_heap_region_is_32_bytes[(sizeof(rvn_region_t) == 32) ? 1 : -1];

static struct {
	rvn_atomic_t lock;
	uint32_t fl_bitmap;
	uint32_t sl_bitmap[RVN_FL_COUNT];
	rvn_block_t* lists[RVN_FL_COUNT][RVN_SL_COUNT];
	rvn_region_t* regions;
	/** Regions that hold no allocation (at most one is kept). */
	int empty_regions;
	RMemoryStats stats;
} g_heap;

/* ---------------------------------------------------------------------------
 *  Helpers
 * ------------------------------------------------------------------------- */

static void rvn_heap_lock(void) {
	int spins = 0;
	while (rvn_atomic_exchange(&g_heap.lock, 1)) {
		/* the owner is a few instructions from the end: only yield when it was preempted */
		if (++spins > 200) {
			rvn_sleep_ms(0);
			spins = 0;
		}
	}
}

static void rvn_heap_unlock(void) {
	rvn_atomic_store(&g_heap.lock, 0);
}

/** Index of the highest bit set; `value` is not 0. */
static int rvn_fls(size_t value) {
#if defined(__GNUC__) || defined(__clang__)
	return (int)(sizeof(unsigned long long) * 8 - 1) - __builtin_clzll((unsigned long long)value);
#else
	int bit = 0;
	while (value >>= 1) bit++;
	return bit;
#endif
}

/** Index of the lowest bit set; `value` is not 0. */
static int rvn_ffs(uint32_t value) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_ctz(value);
#else
	int bit = 0;
	while (!(value & 1u)) {
		value >>= 1;
		bit++;
	}
	return bit;
#endif
}

static size_t rvn_block_size(const rvn_block_t* block) {
	return block->s.size & ~RVN_BLOCK_FLAGS;
}

static int rvn_block_is_free(const rvn_block_t* block) {
	return (block->s.size & RVN_BLOCK_FREE) != 0;
}

static void* rvn_block_payload(rvn_block_t* block) {
	return (char*)block + RVN_HEAP_HEADER;
}

static rvn_block_t* rvn_block_of(const void* ptr) {
	return (rvn_block_t*)((char*)ptr - RVN_HEAP_HEADER);
}

/** The block after `block` in its region. The last block of a region is a used block of size 0. */
static rvn_block_t* rvn_block_next(const rvn_block_t* block) {
	return (rvn_block_t*)((char*)block + RVN_HEAP_HEADER + rvn_block_size(block));
}

/** R_TRUE when the block is the only one of its region: the region holds no allocation. */
static int rvn_block_is_whole_region(const rvn_block_t* block) {
	return block->u.prev_phys == NULL && rvn_block_size(rvn_block_next(block)) == 0;
}

static size_t rvn_heap_round(size_t size) {
	if (size < RVN_HEAP_MIN_PAYLOAD) size = RVN_HEAP_MIN_PAYLOAD;
	return (size + RVN_HEAP_ALIGN - 1) & ~(RVN_HEAP_ALIGN - 1);
}

/** The list that holds the free blocks of `size` bytes. */
static void rvn_heap_mapping(size_t size, int* fl, int* sl) {
	if (size < RVN_SMALL_SIZE) {
		*fl = 0;
		*sl = (int)(size >> 4);
	} else {
		int high = rvn_fls(size);
		*sl = (int)(size >> (high - RVN_SL_LOG2)) ^ RVN_SL_COUNT;
		*fl = high - (RVN_FL_SHIFT - 1);
	}
}

static void rvn_heap_insert(rvn_block_t* block) {
	int fl, sl;
	rvn_block_t* head;
	rvn_heap_mapping(rvn_block_size(block), &fl, &sl);
	head = g_heap.lists[fl][sl];
	block->s.size |= RVN_BLOCK_FREE;
	block->next_free = head;
	block->prev_free = NULL;
	if (head) head->prev_free = block;
	g_heap.lists[fl][sl] = block;
	g_heap.fl_bitmap |= 1u << fl;
	g_heap.sl_bitmap[fl] |= 1u << sl;
}

static void rvn_heap_remove(rvn_block_t* block) {
	int fl, sl;
	rvn_heap_mapping(rvn_block_size(block), &fl, &sl);
	if (block->prev_free) block->prev_free->next_free = block->next_free;
	else g_heap.lists[fl][sl] = block->next_free;
	if (block->next_free) block->next_free->prev_free = block->prev_free;
	if (!g_heap.lists[fl][sl]) {
		g_heap.sl_bitmap[fl] &= ~(1u << sl);
		if (!g_heap.sl_bitmap[fl]) g_heap.fl_bitmap &= ~(1u << fl);
	}
	block->s.size &= ~RVN_BLOCK_FREE;
}

/** A free block of at least `size` bytes, still in its list, or NULL. */
static rvn_block_t* rvn_heap_find(size_t size) {
	int fl, sl;
	uint32_t map;
	/* round up to the next list: every block of that list is large enough */
	if (size >= RVN_SMALL_SIZE) size += ((size_t)1 << (rvn_fls(size) - RVN_SL_LOG2)) - 1;
	rvn_heap_mapping(size, &fl, &sl);
	if (fl >= RVN_FL_COUNT) return NULL;
	map = g_heap.sl_bitmap[fl] & (~0u << sl);
	if (!map) {
		/* nothing in this power of two: take the smallest block of the next one that has some */
		uint32_t fl_map = fl + 1 < 32 ? g_heap.fl_bitmap & (~0u << (fl + 1)) : 0u;
		if (!fl_map) return NULL;
		fl = rvn_ffs(fl_map);
		map = g_heap.sl_bitmap[fl];
	}
	return g_heap.lists[fl][rvn_ffs(map)];
}

/* ---------------------------------------------------------------------------
 *  Memory of the OS
 * ------------------------------------------------------------------------- */

static void* rvn_os_alloc(size_t size) {
#if defined(_WIN32)
	return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(RVN_HEAP_MMAP)
	void* memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return memory == MAP_FAILED ? NULL : memory;
#else
	return malloc(size);
#endif
}

static void rvn_os_free(void* memory, size_t size) {
#if defined(_WIN32)
	RVN_UNUSED(size);
	VirtualFree(memory, 0, MEM_RELEASE);
#elif defined(RVN_HEAP_MMAP)
	munmap(memory, size);
#else
	RVN_UNUSED(size);
	free(memory);
#endif
}

/** Takes a region from the OS and puts its single block in the free lists. Called with the lock. */
static int rvn_heap_add_region(void) {
	rvn_region_t* region = (rvn_region_t*)rvn_os_alloc(RVN_REGION_SIZE);
	rvn_block_t* block;
	rvn_block_t* last;
	if (!region) return R_FALSE;

	region->next = g_heap.regions;
	region->prev = NULL;
	region->os_size = RVN_REGION_SIZE;
	if (g_heap.regions) g_heap.regions->prev = region;
	g_heap.regions = region;

	block = (rvn_block_t*)((char*)region + sizeof(rvn_region_t));
	block->u.prev_phys = NULL;
	block->s.size = RVN_REGION_SIZE - sizeof(rvn_region_t) - 2 * RVN_HEAP_HEADER;
	/* the end of the region: a used block of no size, so that nothing merges past it */
	last = rvn_block_next(block);
	last->u.prev_phys = block;
	last->s.size = 0;

	rvn_heap_insert(block);
	g_heap.empty_regions++;
	g_heap.stats.reserved_bytes += RVN_REGION_SIZE;
	return R_TRUE;
}

/** Returns the region of a block that is alone in it, and out of the free lists, to the OS. */
static void rvn_heap_release_region(rvn_block_t* block) {
	rvn_region_t* region = (rvn_region_t*)((char*)block - sizeof(rvn_region_t));
	if (region->prev) region->prev->next = region->next;
	else g_heap.regions = region->next;
	if (region->next) region->next->prev = region->prev;
	g_heap.stats.reserved_bytes -= region->os_size;
	rvn_os_free(region, region->os_size);
}

/* ---------------------------------------------------------------------------
 *  Blocks
 * ------------------------------------------------------------------------- */

/** Cuts a used block down to `size` and frees what is left, when it is worth a block. */
static void rvn_heap_split(rvn_block_t* block, size_t size) {
	size_t extra = rvn_block_size(block) - size;
	rvn_block_t* rest;
	rvn_block_t* next;
	if (extra < RVN_HEAP_HEADER + RVN_HEAP_MIN_PAYLOAD) return;

	block->s.size = size | (block->s.size & RVN_BLOCK_FLAGS);
	rest = rvn_block_next(block);
	rest->u.prev_phys = block;
	rest->s.size = extra - RVN_HEAP_HEADER;
	next = rvn_block_next(rest);
	if (rvn_block_is_free(next)) {
		/* (only when shrinking: a block found in a list never has a free neighbour) */
		rvn_heap_remove(next);
		rest->s.size += RVN_HEAP_HEADER + rvn_block_size(next);
		next = rvn_block_next(rest);
	}
	next->u.prev_phys = rest;
	rvn_heap_insert(rest);
}

static void rvn_heap_count_alloc(size_t size) {
	g_heap.stats.used_bytes += size;
	if (g_heap.stats.used_bytes > g_heap.stats.peak_bytes) g_heap.stats.peak_bytes = g_heap.stats.used_bytes;
	g_heap.stats.allocation_count++;
	g_heap.stats.total_allocations++;
}

static void* rvn_heap_alloc_huge(size_t size) {
	size_t os_size = (size + RVN_HEAP_HEADER + 4095) & ~(size_t)4095;
	rvn_block_t* block = (rvn_block_t*)rvn_os_alloc(os_size);
	if (!block) return NULL;
	block->u.os_size = os_size;
	block->s.size = size | RVN_BLOCK_HUGE;
	rvn_heap_lock();
	g_heap.stats.reserved_bytes += os_size;
	rvn_heap_count_alloc(size);
	rvn_heap_unlock();
	return rvn_block_payload(block);
}

void* rvn_heap_alloc(size_t size) {
	rvn_block_t* block;
	if (size > RVN_HEAP_MAX_REQUEST) return NULL;
	size = rvn_heap_round(size);
	if (size >= RVN_HUGE_SIZE) return rvn_heap_alloc_huge(size);

	rvn_heap_lock();
	block = rvn_heap_find(size);
	if (!block) {
		if (!rvn_heap_add_region()) {
			rvn_heap_unlock();
			return NULL;
		}
		block = rvn_heap_find(size);
	}
	if (rvn_block_is_whole_region(block)) g_heap.empty_regions--;
	rvn_heap_remove(block);
	rvn_heap_split(block, size);
	rvn_heap_count_alloc(rvn_block_size(block));
	rvn_heap_unlock();
	return rvn_block_payload(block);
}

void rvn_heap_free(void* ptr) {
	rvn_block_t* block;
	rvn_block_t* neighbour;
	if (!ptr) return;
	block = rvn_block_of(ptr);

	if (block->s.size & RVN_BLOCK_HUGE) {
		size_t os_size = block->u.os_size;
		rvn_heap_lock();
		g_heap.stats.used_bytes -= rvn_block_size(block);
		g_heap.stats.reserved_bytes -= os_size;
		g_heap.stats.allocation_count--;
		rvn_heap_unlock();
		rvn_os_free(block, os_size);
		return;
	}

	rvn_heap_lock();
	if (rvn_block_is_free(block)) {
		rvn_heap_unlock();
		rvn_error(R_ERROR_INVALID_OPERATION, R_SEVERITY_FATAL, "memory: block %p freed twice", ptr);
		return;
	}
	g_heap.stats.used_bytes -= rvn_block_size(block);
	g_heap.stats.allocation_count--;

	/* merge with the neighbours that are free: there are never two free blocks in a row */
	neighbour = rvn_block_next(block);
	if (rvn_block_is_free(neighbour)) {
		rvn_heap_remove(neighbour);
		block->s.size += RVN_HEAP_HEADER + rvn_block_size(neighbour);
	}
	neighbour = block->u.prev_phys;
	if (neighbour && rvn_block_is_free(neighbour)) {
		rvn_heap_remove(neighbour);
		neighbour->s.size += RVN_HEAP_HEADER + rvn_block_size(block);
		block = neighbour;
	}
	rvn_block_next(block)->u.prev_phys = block;

	if (rvn_block_is_whole_region(block) && g_heap.empty_regions >= 1) {
		/* one empty region is kept, so that a program that allocates and
		 * frees around a region boundary does not call the OS every time */
		rvn_heap_release_region(block);
	} else {
		if (rvn_block_is_whole_region(block)) g_heap.empty_regions++;
		rvn_heap_insert(block);
	}
	rvn_heap_unlock();
}

void* rvn_heap_realloc(void* ptr, size_t size) {
	rvn_block_t* block;
	rvn_block_t* next;
	size_t old_size;
	void* copy;

	if (!ptr) return rvn_heap_alloc(size);
	if (size > RVN_HEAP_MAX_REQUEST) return NULL;
	block = rvn_block_of(ptr);
	old_size = rvn_block_size(block);
	size = rvn_heap_round(size);

	if (block->s.size & RVN_BLOCK_HUGE) {
		/* a huge block that stays huge and fits where it is does not move */
		if (size >= RVN_HUGE_SIZE / 2 && size + RVN_HEAP_HEADER <= block->u.os_size) {
			rvn_heap_lock();
			g_heap.stats.used_bytes += size;
			g_heap.stats.used_bytes -= old_size;
			if (g_heap.stats.used_bytes > g_heap.stats.peak_bytes) g_heap.stats.peak_bytes = g_heap.stats.used_bytes;
			rvn_heap_unlock();
			block->s.size = size | RVN_BLOCK_HUGE;
			return ptr;
		}
	} else if (size < RVN_HUGE_SIZE) {
		rvn_heap_lock();
		if (size <= old_size) {
			rvn_heap_split(block, size);
			g_heap.stats.used_bytes -= old_size - rvn_block_size(block);
			rvn_heap_unlock();
			return ptr;
		}
		/* grow into the next block when it is free and large enough */
		next = rvn_block_next(block);
		if (rvn_block_is_free(next) && old_size + RVN_HEAP_HEADER + rvn_block_size(next) >= size) {
			rvn_heap_remove(next);
			block->s.size += RVN_HEAP_HEADER + rvn_block_size(next);
			rvn_block_next(block)->u.prev_phys = block;
			rvn_heap_split(block, size);
			g_heap.stats.used_bytes += rvn_block_size(block) - old_size;
			if (g_heap.stats.used_bytes > g_heap.stats.peak_bytes) g_heap.stats.peak_bytes = g_heap.stats.used_bytes;
			rvn_heap_unlock();
			return ptr;
		}
		rvn_heap_unlock();
	}

	copy = rvn_heap_alloc(size);
	if (!copy) return NULL;
	memcpy(copy, ptr, old_size < size ? old_size : size);
	rvn_heap_free(ptr);
	return copy;
}

size_t rvn_heap_size(const void* ptr) {
	return ptr ? rvn_block_size(rvn_block_of(ptr)) : 0;
}

void rvn_heap_trim(void) {
	rvn_region_t* region;
	rvn_region_t* next;
	rvn_heap_lock();
	for (region = g_heap.regions; region; region = next) {
		rvn_block_t* block = (rvn_block_t*)((char*)region + sizeof(rvn_region_t));
		next = region->next;
		if (rvn_block_is_free(block) && rvn_block_is_whole_region(block)) {
			rvn_heap_remove(block);
			rvn_heap_release_region(block);
			g_heap.empty_regions--;
		}
	}
	rvn_heap_unlock();
}

void rvn_heap_get_stats(RMemoryStats* stats) {
	rvn_heap_lock();
	*stats = g_heap.stats;
	rvn_heap_unlock();
}

/* ---------------------------------------------------------------------------
 *  Self-check
 * ------------------------------------------------------------------------- */

int rvn_heap_check(void) {
	rvn_region_t* region;
	size_t used = 0, free_blocks = 0, listed = 0, huge;
	int fl, sl, ok = R_TRUE, empty = 0;

#define RVN_HEAP_EXPECT(condition) do { if (!(condition)) { ok = R_FALSE; goto done; } } while (0)

	rvn_heap_lock();
	for (region = g_heap.regions; region; region = region->next) {
		rvn_block_t* block = (rvn_block_t*)((char*)region + sizeof(rvn_region_t));
		rvn_block_t* previous = NULL;
		char* end = (char*)region + region->os_size;
		int previous_free = R_FALSE;
		RVN_HEAP_EXPECT(!region->next || region->next->prev == region);
		for (;;) {
			size_t size = rvn_block_size(block);
			RVN_HEAP_EXPECT((char*)block + RVN_HEAP_HEADER <= end);
			RVN_HEAP_EXPECT(block->u.prev_phys == previous);
			RVN_HEAP_EXPECT(!(block->s.size & RVN_BLOCK_HUGE));
			if (size == 0) {
				/* the end of the region */
				RVN_HEAP_EXPECT(!rvn_block_is_free(block));
				RVN_HEAP_EXPECT((char*)block + RVN_HEAP_HEADER == end);
				break;
			}
			RVN_HEAP_EXPECT((size & (RVN_HEAP_ALIGN - 1)) == 0 && size >= RVN_HEAP_MIN_PAYLOAD);
			if (rvn_block_is_free(block)) {
				/* two free neighbours would have been merged */
				RVN_HEAP_EXPECT(!previous_free);
				free_blocks++;
				if (rvn_block_is_whole_region(block)) empty++;
			} else {
				used += size;
			}
			previous_free = rvn_block_is_free(block);
			previous = block;
			block = rvn_block_next(block);
		}
	}
	RVN_HEAP_EXPECT(empty == g_heap.empty_regions);

	for (fl = 0; fl < RVN_FL_COUNT; fl++) {
		RVN_HEAP_EXPECT(((g_heap.fl_bitmap >> fl) & 1u) == (g_heap.sl_bitmap[fl] != 0));
		for (sl = 0; sl < RVN_SL_COUNT; sl++) {
			rvn_block_t* block = g_heap.lists[fl][sl];
			rvn_block_t* previous = NULL;
			RVN_HEAP_EXPECT(((g_heap.sl_bitmap[fl] >> sl) & 1u) == (block != NULL));
			for (; block; block = block->next_free) {
				int block_fl, block_sl;
				RVN_HEAP_EXPECT(rvn_block_is_free(block));
				RVN_HEAP_EXPECT(block->prev_free == previous);
				rvn_heap_mapping(rvn_block_size(block), &block_fl, &block_sl);
				RVN_HEAP_EXPECT(block_fl == fl && block_sl == sl);
				listed++;
				previous = block;
			}
		}
	}
	/* every free block is in a list, once */
	RVN_HEAP_EXPECT(listed == free_blocks);
	/* what is not in the regions is in the huge blocks */
	RVN_HEAP_EXPECT(used <= g_heap.stats.used_bytes);
	huge = g_heap.stats.used_bytes - used;
	RVN_HEAP_EXPECT(huge == 0 || huge >= RVN_HUGE_SIZE / 2);

done:
	rvn_heap_unlock();
	return ok;
#undef RVN_HEAP_EXPECT
}
