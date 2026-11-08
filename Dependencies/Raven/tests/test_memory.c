/**
 * Raven tests - the allocator of the library (core/heap.c) and the
 * replacement of it by the functions of the application.
 */

#include "test.h"

#include <stdlib.h>

/* The heap is tested for itself here, even in a build where the C runtime is
 * the default (AddressSanitizer). */
static void use_raven_heap(void) {
#if defined(_WIN32)
	_putenv("RAVEN_ALLOCATOR=raven");
#else
	setenv("RAVEN_ALLOCATOR", "raven", 1);
#endif
}

/* ---------------------------------------------------------------------------
 *  Random allocations
 * ------------------------------------------------------------------------- */

static uint32_t g_seed = 0x2545F491u;

static uint32_t next_random(void) {
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 17;
	g_seed ^= g_seed << 5;
	return g_seed;
}

/* Mostly small blocks, some of a few kilobytes, a few of several megabytes. */
static size_t random_size(void) {
	uint32_t kind = next_random() % 1000;
	if (kind < 700) return 1 + next_random() % 256;
	if (kind < 930) return 1 + next_random() % 4096;
	if (kind < 996) return 1 + next_random() % (128 * 1024);
	if (kind < 999) return 512 * 1024 + next_random() % (1024 * 1024);
	return 1024 * 1024 + next_random() % (3 * 1024 * 1024);
}

typedef struct Slot {
	uint8_t* data;
	size_t size;
	uint8_t pattern;
} Slot;

static void fill(Slot* slot) {
	memset(slot->data, slot->pattern, slot->size);
}

/* The content of a block is intact: no other block was laid over it. */
static int intact(const Slot* slot, size_t size) {
	size_t i;
	for (i = 0; i < size; i++) {
		if (slot->data[i] != slot->pattern) return 0;
	}
	return 1;
}

#define SLOT_COUNT 3000
static Slot g_slots[SLOT_COUNT];

static void test_random(void) {
	RMemoryStats before, stats;
	int step, damaged = 0, misaligned = 0;
	size_t i;

	Rmemory_get_stats(&before);
	for (step = 0; step < 200000; step++) {
		Slot* slot = &g_slots[next_random() % SLOT_COUNT];
		uint32_t action = next_random() % 10;
		if (!slot->data) {
			slot->size = random_size();
			slot->pattern = (uint8_t)next_random();
			slot->data = (uint8_t*)Rmemory_alloc(slot->size);
			if (((uintptr_t)slot->data & 15u) != 0) misaligned++;
			fill(slot);
		} else if (action < 4) {
			/* resize: what fits in both sizes is kept */
			size_t size = random_size();
			if (!intact(slot, slot->size)) damaged++;
			slot->data = (uint8_t*)Rmemory_realloc(slot->data, size);
			if (((uintptr_t)slot->data & 15u) != 0) misaligned++;
			if (!intact(slot, size < slot->size ? size : slot->size)) damaged++;
			slot->size = size;
			fill(slot);
		} else {
			if (!intact(slot, slot->size)) damaged++;
			Rmemory_free(slot->data);
			slot->data = NULL;
		}
		if (step % 20000 == 0) CHECK(Rmemory_check());
	}
	CHECK(damaged == 0);
	CHECK(misaligned == 0);
	CHECK(Rmemory_check());

	Rmemory_get_stats(&stats);
	CHECK(stats.allocation_count > before.allocation_count);
	CHECK(stats.used_bytes > before.used_bytes && stats.reserved_bytes >= stats.used_bytes);
	CHECK(stats.peak_bytes >= stats.used_bytes);
	printf("  %u blocks alive, %.1f MB used, %.1f MB taken from the OS, peak %.1f MB\n", (unsigned)stats.allocation_count,
		(double)stats.used_bytes / 1048576.0, (double)stats.reserved_bytes / 1048576.0, (double)stats.peak_bytes / 1048576.0);

	for (i = 0; i < SLOT_COUNT; i++) {
		if (g_slots[i].data && !intact(&g_slots[i], g_slots[i].size)) damaged++;
		Rmemory_free(g_slots[i].data);
		g_slots[i].data = NULL;
	}
	CHECK(damaged == 0);
	CHECK(Rmemory_check());

	/* everything was freed: the counters are back, and so is the memory of the OS */
	Rmemory_get_stats(&stats);
	CHECK(stats.allocation_count == before.allocation_count);
	CHECK(stats.used_bytes == before.used_bytes);
	Rmemory_trim();
	Rmemory_get_stats(&stats);
	CHECK(stats.reserved_bytes <= before.reserved_bytes);
}

static void test_edges(void) {
	char* a;
	char* b;
	char* c;
	size_t i;

	/* a size of zero gives a valid block */
	a = (char*)Rmemory_alloc(0);
	CHECK(a != NULL);
	Rmemory_free(a);
	Rmemory_free(NULL);

	/* growing in place: the neighbour is free */
	a = (char*)Rmemory_alloc(100);
	b = (char*)Rmemory_alloc(100);
	memset(a, 'a', 100);
	Rmemory_free(b);
	c = (char*)Rmemory_realloc(a, 180);
	CHECK(c == a);
	for (i = 0; i < 100; i++) {
		if (c[i] != 'a') break;
	}
	CHECK(i == 100);
	/* shrinking never moves */
	a = (char*)Rmemory_realloc(c, 10);
	CHECK(a == c);
	CHECK(a[9] == 'a');
	/* a NULL block is allocated */
	b = (char*)Rmemory_realloc(NULL, 50);
	CHECK(b != NULL);
	Rmemory_free(a);
	Rmemory_free(b);

	/* a block of several megabytes, grown and shrunk */
	a = (char*)Rmemory_alloc(5u << 20);
	memset(a, 'x', 5u << 20);
	a = (char*)Rmemory_realloc(a, 9u << 20);
	CHECK(a[0] == 'x' && a[(5u << 20) - 1] == 'x');
	a[(9u << 20) - 1] = 'y';
	a = (char*)Rmemory_realloc(a, 64);
	CHECK(a[0] == 'x' && a[63] == 'x');
	Rmemory_free(a);
	CHECK(Rmemory_check());
}

/* ---------------------------------------------------------------------------
 *  Several threads
 * ------------------------------------------------------------------------- */

static void thread_work(Ruint begin, Ruint end, void* user_data) {
	uint32_t seed = 0x9E3779B9u * (begin + 1);
	void* blocks[64];
	size_t sizes[64];
	Ruint i;
	int k;
	int* damaged = (int*)user_data;

	memset(blocks, 0, sizeof(blocks));
	for (i = begin; i < end; i++) {
		for (k = 0; k < 400; k++) {
			uint32_t slot;
			seed = seed * 1664525u + 1013904223u;
			slot = (seed >> 8) % 64;
			if (blocks[slot]) {
				const uint8_t* bytes = (const uint8_t*)blocks[slot];
				if (bytes[0] != (uint8_t)slot || bytes[sizes[slot] - 1] != (uint8_t)slot) *damaged = 1;
				Rmemory_free(blocks[slot]);
				blocks[slot] = NULL;
			} else {
				sizes[slot] = 1 + (seed >> 16) % 3000;
				blocks[slot] = Rmemory_alloc(sizes[slot]);
				memset(blocks[slot], (int)slot, sizes[slot]);
			}
		}
	}
	for (k = 0; k < 64; k++) Rmemory_free(blocks[k]);
}

static void test_threads(void) {
	int damaged = 0;
	/* 64 ranges shared between the worker threads and this one, while the
	 * render thread of the library allocates too */
	Rparallel_for(64, 1, thread_work, &damaged);
	CHECK(!damaged);
	CHECK(Rmemory_check());
}

/* ---------------------------------------------------------------------------
 *  The library on the allocator of the application
 * ------------------------------------------------------------------------- */

/* The functions of an allocator are called from several threads (the UI
 * thread and the render thread here): the counters are atomic. */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static volatile long g_my_live, g_my_total;
#define COUNT(counter, by) _InterlockedExchangeAdd(&(counter), (by))
#define READ(counter) ((int)_InterlockedExchangeAdd(&(counter), 0))
#else
static int g_my_live, g_my_total;
#define COUNT(counter, by) __atomic_add_fetch(&(counter), (by), __ATOMIC_SEQ_CST)
#define READ(counter) __atomic_load_n(&(counter), __ATOMIC_SEQ_CST)
#endif

static void* my_alloc(size_t size, void* user_data) {
	(void)user_data;
	COUNT(g_my_live, 1);
	COUNT(g_my_total, 1);
	return malloc(size);
}

static void* my_realloc(void* ptr, size_t size, void* user_data) {
	(void)user_data;
	return realloc(ptr, size);
}

static void my_free(void* ptr, void* user_data) {
	(void)user_data;
	COUNT(g_my_live, -1);
	free(ptr);
}

static void build_a_window(void) {
	static const char xml[] =
		"<Window Width=\"400\" Height=\"300\"><VBox>"
		"<Label>Text</Label><Button Text=\"Button\"/><TextInput Text=\"abc\"/>"
		"<TreeView><Item Text=\"a\"><Item Text=\"b\"/></Item></TreeView>"
		"</VBox></Window>";
	Rid window = Rwindow_create_from_xml(xml, sizeof(xml) - 1);
	CHECK(Rwindow_is_valid(window));
	Rdraw_frame();
}

static void test_library_memory(void) {
	RMemoryStats stats;

	/* with its own heap: everything is returned at shutdown */
	CHECK(test_init(2));
	build_a_window();
	Rmemory_get_stats(&stats);
	CHECK(stats.allocation_count > 20 && stats.used_bytes > 10000);
	printf("  a small window: %u blocks, %u KB\n", (unsigned)stats.allocation_count, (unsigned)(stats.used_bytes / 1024));
	test_threads();
	/* the allocator cannot change under a running library */
	EXPECT_ERRORS_BEGIN();
	Rset_allocator(Rallocator_get_system());
	EXPECT_ERRORS_END(1);
	Rshutdown();
	Rmemory_get_stats(&stats);
	CHECK(stats.allocation_count == 0);
	CHECK(stats.used_bytes == 0);
	CHECK(stats.reserved_bytes == 0);
}

static void test_custom_allocator(void) {
	RAllocator allocator;
	RMemoryStats stats;

	allocator.alloc = my_alloc;
	allocator.realloc = my_realloc;
	allocator.free = my_free;
	allocator.user_data = NULL;
	Rset_allocator(&allocator);

	CHECK(test_init(-1));
	build_a_window();
	CHECK(READ(g_my_live) > 20);
	Rmemory_get_stats(&stats);
	/* the heap of Raven is not used at all */
	CHECK(stats.reserved_bytes == 0 && stats.used_bytes == 0);
	CHECK(stats.allocation_count > 20);
	Rshutdown();
	CHECK(READ(g_my_live) == 0);
	CHECK(READ(g_my_total) > 20);

	/* incomplete: refused */
	allocator.free = NULL;
	EXPECT_ERRORS_BEGIN();
	Rset_allocator(&allocator);
	EXPECT_ERRORS_END(1);

	/* the C runtime, then Raven again */
	Rset_allocator(Rallocator_get_system());
	CHECK(test_init(-1));
	build_a_window();
	Rshutdown();
	Rset_allocator(NULL);
	CHECK(test_init(-1));
	build_a_window();
	Rmemory_get_stats(&stats);
	CHECK(stats.used_bytes > 10000);
	Rshutdown();
}

int main(void) {
	use_raven_heap();
	Rerror_set_callback(test_error_callback);

	RUN(test_edges);
	RUN(test_random);
	RUN(test_library_memory);
	RUN(test_custom_allocator);

	return test_report("test_memory");
}
