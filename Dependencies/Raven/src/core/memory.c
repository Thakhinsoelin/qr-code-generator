/**
 * Raven - core/memory.c
 * Choice of the allocator, dynamic arrays, arena, strings and hashing.
 */

#include "base.h"
#include "heap.h"
#include "thread.h"

/* ---------------------------------------------------------------------------
 *  Allocator
 *
 *  Every allocation of the library goes through rvn_alloc / rvn_realloc /
 *  rvn_free, including the ones of the bundled decoders. By default they use
 *  the heap of Raven (heap.c); the application can give its own functions
 *  instead (Rset_allocator).
 * ------------------------------------------------------------------------- */

typedef enum rvn_memory_mode_t {
	RVN_MEMORY_UNDECIDED = 0,
	RVN_MEMORY_HEAP,
	RVN_MEMORY_USER
} rvn_memory_mode_t;

static RAllocator g_allocator;
static rvn_atomic_t g_memory_mode;
/* allocations made through the functions of the application: only counted */
static rvn_atomic_t g_user_live;
static rvn_atomic_t g_user_total;

static void* rvn_system_alloc(size_t size, void* user_data) {
	RVN_UNUSED(user_data);
	return malloc(size);
}

static void* rvn_system_realloc(void* ptr, size_t size, void* user_data) {
	RVN_UNUSED(user_data);
	return realloc(ptr, size);
}

static void rvn_system_free(void* ptr, void* user_data) {
	RVN_UNUSED(user_data);
	free(ptr);
}

static const RAllocator g_system_allocator = { rvn_system_alloc, rvn_system_realloc, rvn_system_free, NULL };

const RAllocator* Rallocator_get_system(void) {
	return &g_system_allocator;
}

/* Address sanitizers follow malloc, not a private heap: with them, the C
 * runtime is the default, so that they keep seeing every block of the library. */
#if defined(__SANITIZE_ADDRESS__)
#define RVN_MEMORY_PREFER_SYSTEM 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer)
#define RVN_MEMORY_PREFER_SYSTEM 1
#endif
#endif

/** Chooses the allocator at the first allocation. */
static int rvn_memory_mode(void) {
	int mode = rvn_atomic_load(&g_memory_mode);
	if (mode == RVN_MEMORY_UNDECIDED) {
		/* RAVEN_ALLOCATOR=system|raven overrides the default, to debug with external tools */
		const char* choice = getenv("RAVEN_ALLOCATOR");
		int system = R_FALSE;
#ifdef RVN_MEMORY_PREFER_SYSTEM
		system = R_TRUE;
#endif
		if (choice && rvn_stricmp(choice, "system") == 0) system = R_TRUE;
		if (choice && rvn_stricmp(choice, "raven") == 0) system = R_FALSE;
		if (g_allocator.alloc) {
			mode = RVN_MEMORY_USER;
		} else if (system) {
			g_allocator = g_system_allocator;
			mode = RVN_MEMORY_USER;
		} else {
			mode = RVN_MEMORY_HEAP;
		}
		rvn_atomic_store(&g_memory_mode, mode);
	}
	return mode;
}

void Rset_allocator(const RAllocator* allocator) {
	RMemoryStats stats;
	Rmemory_get_stats(&stats);
	if (Ris_initialized() || stats.allocation_count != 0) {
		rvn_error(R_ERROR_INVALID_OPERATION, R_SEVERITY_ERROR,
			"Rset_allocator must be called before Rinit, while the library holds no memory");
		return;
	}
	if (allocator && !(allocator->alloc && allocator->realloc && allocator->free)) {
		rvn_error(R_ERROR_INVALID_VALUE, R_SEVERITY_ERROR, "Rset_allocator: the three functions are required");
		return;
	}
	rvn_heap_trim();
	if (allocator) {
		g_allocator = *allocator;
		rvn_atomic_store(&g_memory_mode, RVN_MEMORY_USER);
	} else {
		memset(&g_allocator, 0, sizeof(g_allocator));
		rvn_atomic_store(&g_memory_mode, RVN_MEMORY_HEAP);
	}
}

void Rmemory_get_stats(RMemoryStats* stats) {
	if (!stats) return;
	rvn_heap_get_stats(stats);
	if (rvn_atomic_load(&g_memory_mode) == RVN_MEMORY_USER) {
		/* sizes are only known to the allocator of the application */
		stats->allocation_count = (size_t)rvn_atomic_load(&g_user_live);
		stats->total_allocations = (uint64_t)(uint32_t)rvn_atomic_load(&g_user_total);
	}
}

int Rmemory_check(void) {
	return rvn_heap_check();
}

void Rmemory_trim(void) {
	rvn_heap_trim();
}

/* Debug builds fill new memory with 0xCD and freed memory with 0xDD: code that
 * reads a block before writing it, or after freeing it, fails at once instead
 * of working by luck. */
#if !defined(NDEBUG) && !defined(RVN_MEMORY_NO_FILL)
#define RVN_MEMORY_FILL 1
#endif

void* Rmemory_alloc(size_t size) {
	return rvn_alloc(size);
}

void* Rmemory_realloc(void* ptr, size_t size) {
	return rvn_realloc(ptr, size);
}

void Rmemory_free(void* ptr) {
	rvn_free(ptr);
}

static void rvn_out_of_memory(size_t size) {
	rvn_error(R_ERROR_OUT_OF_MEMORY, R_SEVERITY_FATAL, "allocation of %zu bytes failed", size);
	abort();
}

void* rvn_alloc(size_t size) {
	void* ptr;
	if (size == 0) size = 1;
	if (rvn_memory_mode() == RVN_MEMORY_HEAP) {
		ptr = rvn_heap_alloc(size);
#ifdef RVN_MEMORY_FILL
		if (ptr) memset(ptr, 0xCD, rvn_heap_size(ptr));
#endif
	} else {
		ptr = g_allocator.alloc(size, g_allocator.user_data);
		rvn_atomic_add(&g_user_live, 1);
		rvn_atomic_add(&g_user_total, 1);
	}
	if (!ptr) rvn_out_of_memory(size);
	return ptr;
}

void* rvn_calloc(size_t size) {
	void* ptr = rvn_alloc(size);
	memset(ptr, 0, size);
	return ptr;
}

void* rvn_realloc(void* ptr, size_t size) {
	void* out;
	if (!ptr) return rvn_alloc(size);
	if (size == 0) size = 1;
	if (rvn_memory_mode() == RVN_MEMORY_HEAP) {
#ifdef RVN_MEMORY_FILL
		size_t old_size = rvn_heap_size(ptr);
		out = rvn_heap_realloc(ptr, size);
		if (out && rvn_heap_size(out) > old_size) memset((char*)out + old_size, 0xCD, rvn_heap_size(out) - old_size);
#else
		out = rvn_heap_realloc(ptr, size);
#endif
	} else {
		out = g_allocator.realloc(ptr, size, g_allocator.user_data);
	}
	if (!out) rvn_out_of_memory(size);
	return out;
}

void rvn_free(void* ptr) {
	if (!ptr) return;
	if (rvn_memory_mode() == RVN_MEMORY_HEAP) {
#ifdef RVN_MEMORY_FILL
		memset(ptr, 0xDD, rvn_heap_size(ptr));
#endif
		rvn_heap_free(ptr);
	} else {
		g_allocator.free(ptr, g_allocator.user_data);
		rvn_atomic_add(&g_user_live, -1);
	}
}

uint8_t* Rpixels_alloc(size_t size) {
	return (uint8_t*)rvn_alloc(size);
}

void Rpixels_free(uint8_t* pixels) {
	rvn_free(pixels);
}

/* ---------------------------------------------------------------------------
 *  Dynamic arrays
 * ------------------------------------------------------------------------- */

void* rvn_array_grow_(void* data, uint32_t* cap, uint32_t needed, size_t item_size) {
	uint32_t new_cap = *cap ? *cap : 8;
	while (new_cap < needed) {
		new_cap = new_cap < 0x40000000u ? new_cap * 2 : needed;
	}
	data = rvn_realloc(data, (size_t)new_cap * item_size);
	*cap = new_cap;
	return data;
}

/* ---------------------------------------------------------------------------
 *  Arena
 * ------------------------------------------------------------------------- */

void rvn_arena_init(rvn_arena_t* arena, size_t block_size) {
	arena->head = NULL;
	arena->current = NULL;
	arena->block_size = block_size ? block_size : 16384;
}

void* rvn_arena_alloc(rvn_arena_t* arena, size_t size) {
	rvn_arena_block_t* block = arena->current;
	void* ptr;
	size = (size + 15u) & ~(size_t)15u;
	if (!block || block->used + size > block->cap) {
		size_t cap = size > arena->block_size ? size : arena->block_size;
		block = (rvn_arena_block_t*)rvn_alloc(sizeof(rvn_arena_block_t) + 16 + cap);
		block->next = NULL;
		block->cap = cap;
		block->used = 0;
		if (arena->current) arena->current->next = block;
		else arena->head = block;
		arena->current = block;
	}
	/* the payload starts on a 16-byte boundary after the header */
	ptr = (char*)block + ((sizeof(rvn_arena_block_t) + 15u) & ~(size_t)15u) + block->used;
	block->used += size;
	return ptr;
}

char* rvn_arena_strndup(rvn_arena_t* arena, const char* text, size_t length) {
	char* out = (char*)rvn_arena_alloc(arena, length + 1);
	if (length) memcpy(out, text, length);
	out[length] = '\0';
	return out;
}

void rvn_arena_free(rvn_arena_t* arena) {
	rvn_arena_block_t* block = arena->head;
	while (block) {
		rvn_arena_block_t* next = block->next;
		rvn_free(block);
		block = next;
	}
	arena->head = NULL;
	arena->current = NULL;
}

/* ---------------------------------------------------------------------------
 *  Strings
 * ------------------------------------------------------------------------- */

char* rvn_strndup(const char* text, size_t length) {
	char* out = (char*)rvn_alloc(length + 1);
	if (length) memcpy(out, text, length);
	out[length] = '\0';
	return out;
}

char* rvn_strdup(const char* text) {
	return text ? rvn_strndup(text, strlen(text)) : NULL;
}

int rvn_str_set(char** dst, const char* src) {
	if (*dst == src) return R_FALSE;
	if (*dst && src && strcmp(*dst, src) == 0) return R_FALSE;
	if (!*dst && !src) return R_FALSE;
	{
		char* copy = rvn_strdup(src);
		rvn_free(*dst);
		*dst = copy;
	}
	return R_TRUE;
}

void rvn_text_append(rvn_text_t* text, const char* bytes, size_t length) {
	if (text->length + length + 1 > text->cap) {
		size_t cap = text->cap ? text->cap * 2 : 256;
		while (cap < text->length + length + 1) cap *= 2;
		text->data = (char*)rvn_realloc(text->data, cap);
		text->cap = cap;
	}
	if (length) memcpy(text->data + text->length, bytes, length);
	text->length += length;
	text->data[text->length] = '\0';
}

void rvn_text_add(rvn_text_t* text, const char* string) {
	if (string) rvn_text_append(text, string, strlen(string));
}

void rvn_text_printf(rvn_text_t* text, const char* format, ...) {
	char local[512];
	va_list args;
	int needed;
	va_start(args, format);
	needed = vsnprintf(local, sizeof(local), format, args);
	va_end(args);
	if (needed < 0) return;
	if ((size_t)needed < sizeof(local)) {
		rvn_text_append(text, local, (size_t)needed);
	} else {
		char* large = (char*)rvn_alloc((size_t)needed + 1);
		va_start(args, format);
		vsnprintf(large, (size_t)needed + 1, format, args);
		va_end(args);
		rvn_text_append(text, large, (size_t)needed);
		rvn_free(large);
	}
}

static int rvn_lower(int c) {
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int rvn_stricmp(const char* a, const char* b) {
	if (!a) a = "";
	if (!b) b = "";
	for (;; a++, b++) {
		int ca = rvn_lower((unsigned char)*a), cb = rvn_lower((unsigned char)*b);
		if (ca != cb) return ca - cb;
		if (!ca) return 0;
	}
}

int rvn_strnicmp(const char* a, const char* b, size_t n) {
	size_t i;
	for (i = 0; i < n; i++) {
		int ca = rvn_lower((unsigned char)a[i]), cb = rvn_lower((unsigned char)b[i]);
		if (ca != cb) return ca - cb;
		if (!ca) return 0;
	}
	return 0;
}

uint32_t rvn_hash_bytes(const void* data, size_t size) {
	/* FNV-1a */
	const uint8_t* bytes = (const uint8_t*)data;
	uint32_t hash = 2166136261u;
	size_t i;
	for (i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

uint32_t rvn_hash_str(const char* text) {
	return rvn_hash_bytes(text, text ? strlen(text) : 0);
}

/* ---------------------------------------------------------------------------
 *  Paths
 * ------------------------------------------------------------------------- */

static int rvn_is_separator(char c) {
	return c == '/' || c == '\\';
}

static int rvn_path_is_absolute(const char* path) {
	if (rvn_is_separator(path[0])) return R_TRUE;
	/* drive letter: "C:" */
	return path[0] && path[1] == ':';
}

char* rvn_path_join(const char* base, const char* path) {
	size_t base_len, path_len;
	char* out;
	if (!path) return NULL;
	if (!base || !base[0] || rvn_path_is_absolute(path)) return rvn_strdup(path);
	base_len = strlen(base);
	path_len = strlen(path);
	out = (char*)rvn_alloc(base_len + path_len + 2);
	memcpy(out, base, base_len);
	if (!rvn_is_separator(base[base_len - 1])) out[base_len++] = '/';
	memcpy(out + base_len, path, path_len + 1);
	return out;
}

char* rvn_path_dir(const char* path) {
	size_t length = path ? strlen(path) : 0;
	while (length > 0 && !rvn_is_separator(path[length - 1])) length--;
	return rvn_strndup(path ? path : "", length);
}
