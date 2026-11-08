/**
 * Raven - core/lfmap.c
 */

#include "lfmap.h"

static uint32_t rvn_lfmap_hash(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdULL;
	key ^= key >> 33;
	key *= 0xc4ceb9fe1a85ec53ULL;
	key ^= key >> 33;
	return (uint32_t)key;
}

int rvn_lfmap_find(rvn_lfmap_t* map, uint64_t key, uint64_t value[2]) {
	rvn_lfmap_table_t* table = (rvn_lfmap_table_t*)rvn_atomic_ptr_load(&map->table);
	uint32_t mask, slot, probe;
	if (!table) return R_FALSE;
	mask = table->cap - 1;
	slot = rvn_lfmap_hash(key) & mask;
	for (probe = 0; probe < table->cap; probe++) {
		rvn_lfmap_slot_t* entry = &table->slots[slot];
		/* reading the state first makes the key and value safe to read */
		if (!rvn_atomic_load(&entry->state)) return R_FALSE;
		if (entry->key == key) {
			value[0] = entry->value[0];
			value[1] = entry->value[1];
			return R_TRUE;
		}
		slot = (slot + 1) & mask;
	}
	return R_FALSE;
}

static rvn_lfmap_table_t* rvn_lfmap_table_new(uint32_t cap) {
	size_t size = sizeof(rvn_lfmap_table_t) + sizeof(rvn_lfmap_slot_t) * (size_t)(cap - 1);
	rvn_lfmap_table_t* table = (rvn_lfmap_table_t*)rvn_calloc(size);
	table->cap = cap;
	return table;
}

static void rvn_lfmap_table_put(rvn_lfmap_table_t* table, uint64_t key, const uint64_t value[2]) {
	uint32_t mask = table->cap - 1;
	uint32_t slot = rvn_lfmap_hash(key) & mask;
	rvn_lfmap_slot_t* entry;
	while (rvn_atomic_load(&table->slots[slot].state)) slot = (slot + 1) & mask;
	entry = &table->slots[slot];
	entry->key = key;
	entry->value[0] = value[0];
	entry->value[1] = value[1];
	/* publication: from here on, readers may use the slot */
	rvn_atomic_store(&entry->state, 1);
	table->count++;
}

void rvn_lfmap_insert(rvn_lfmap_t* map, uint64_t key, const uint64_t value[2]) {
	rvn_lfmap_table_t* table = (rvn_lfmap_table_t*)rvn_atomic_ptr_load(&map->table);
	if (!table || (table->count + 1) * 4 > table->cap * 3) {
		rvn_lfmap_table_t* larger = rvn_lfmap_table_new(table ? table->cap * 2 : 256);
		uint32_t i;
		for (i = 0; table && i < table->cap; i++) {
			if (rvn_atomic_load(&table->slots[i].state)) {
				rvn_lfmap_table_put(larger, table->slots[i].key, table->slots[i].value);
			}
		}
		rvn_lfmap_table_put(larger, key, value);
		rvn_atomic_ptr_store(&map->table, larger);
		/* readers may still be probing the old table */
		if (table) {
			table->retired_next = map->retired;
			map->retired = table;
		}
		return;
	}
	rvn_lfmap_table_put(table, key, value);
}

void rvn_lfmap_collect(rvn_lfmap_t* map) {
	rvn_lfmap_table_t* table = map->retired;
	while (table) {
		rvn_lfmap_table_t* next = table->retired_next;
		rvn_free(table);
		table = next;
	}
	map->retired = NULL;
}

void rvn_lfmap_clear(rvn_lfmap_t* map) {
	rvn_lfmap_collect(map);
	rvn_free(rvn_atomic_ptr_load(&map->table));
	rvn_atomic_ptr_store(&map->table, NULL);
}
