#ifndef _HASH_H_
#define _HASH_H_
#include "types.h"

struct hash {
	uint64 key;
	struct hash * next;
	void *data;
};

struct hash_table_entries {
	uint32 nelements;
	struct hash * next;
};

struct hash_table {
	uint32 num_tables;
	uint64 (*hash_func)(struct hash_table*, uint64 key);
	struct hash_table_entries *table;
	uint32 nelements;
};

struct hash_table *hash_init(uint32 buckets, uint64 (*hash_func)(struct hash_table*table, uint64 key));
uint32 hash_insert(struct hash_table* table, uint64 key, void* data);
uint32 hash_lookup(struct hash_table* table, uint64 key, void ** data);
uint32 hash_delete (struct hash_table*table,uint64 key, void ** data);
uint32 hash_num_elements (struct hash_table*table);
uint32 hash_num_elements_bucket(struct hash_table*table, uint32 bucket);

#endif
