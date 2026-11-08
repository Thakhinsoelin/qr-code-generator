/**
 * Raven - core/lfmap.h
 * Hash map (64-bit key -> 16-byte value) whose lookups take no lock.
 *
 * It is built for caches that are read all the time by several threads and
 * written rarely (the glyph caches read by the paint jobs):
 *   - rvn_lfmap_find can be called from any thread, concurrently with
 *     anything, and never blocks;
 *   - writers must be serialized by the caller (one mutex);
 *   - entries are never removed or changed, only added;
 *   - when the table grows, the old storage is kept alive until
 *     rvn_lfmap_collect, which the owner calls at a point where no reader
 *     can be in the middle of a lookup.
 *
 * A reader that races with an insertion may miss the new entry: it then
 * takes the writer mutex and looks again, like for any other cache miss.
 */

#ifndef RVN_LFMAP_H
#define RVN_LFMAP_H

#include "thread.h"

typedef struct rvn_lfmap_slot_t {
	/** 0 = empty, 1 = key and value are published. */
	rvn_atomic_t state;
	uint32_t reserved;
	uint64_t key;
	uint64_t value[2];
} rvn_lfmap_slot_t;

typedef struct rvn_lfmap_table_t {
	uint32_t cap;
	uint32_t count;
	struct rvn_lfmap_table_t* retired_next;
	rvn_lfmap_slot_t slots[1];
} rvn_lfmap_table_t;

typedef struct rvn_lfmap_t {
	rvn_atomic_ptr_t table;
	/** Tables replaced by a larger one, waiting for rvn_lfmap_collect. */
	rvn_lfmap_table_t* retired;
} rvn_lfmap_t;

/** Lock-free. Returns R_TRUE and fills `value` when the key is present. */
int  rvn_lfmap_find(rvn_lfmap_t* map, uint64_t key, uint64_t value[2]);
/** Adds a key that is not in the map. The caller holds the writer lock. */
void rvn_lfmap_insert(rvn_lfmap_t* map, uint64_t key, const uint64_t value[2]);
/** Frees the retired tables. No reader may be inside rvn_lfmap_find. */
void rvn_lfmap_collect(rvn_lfmap_t* map);
/** Empties the map. No reader may be active. */
void rvn_lfmap_clear(rvn_lfmap_t* map);

#endif /* RVN_LFMAP_H */
