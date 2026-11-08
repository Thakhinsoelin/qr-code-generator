/**
 * Raven - core/heap.h
 * The allocator of the library: a TLSF heap over memory taken from the OS.
 * Nothing in Raven calls malloc; see heap.c.
 */

#ifndef RVN_HEAP_H
#define RVN_HEAP_H

#include "base.h"

/** Returns NULL when the OS has no memory left. The result is aligned on 16 bytes. */
void* rvn_heap_alloc(size_t size);
/** Grows or shrinks a block, in place when it can. NULL when out of memory (the block is kept). */
void* rvn_heap_realloc(void* ptr, size_t size);
void  rvn_heap_free(void* ptr);

/** Bytes that can be written at `ptr` (at least the size that was asked for). */
size_t rvn_heap_size(const void* ptr);

/** Gives the regions that hold no allocation back to the OS. */
void rvn_heap_trim(void);

/** Walks every block and every free list. R_FALSE when the heap is corrupted. */
int rvn_heap_check(void);

void rvn_heap_get_stats(RMemoryStats* stats);

#endif /* RVN_HEAP_H */
