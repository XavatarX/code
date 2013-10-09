#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "hash.h"
#include <string.h>

struct hash_table *hash_init(uint32 buckets, uint64 (*hash_func)(struct hash_table* table, uint64 key))
{
	struct hash_table * table = malloc(sizeof(struct hash_table));
	if (table == NULL) {
		return NULL;
	}
	memset(table, 0, sizeof(*table));
	table->hash_func = hash_func;
	table->table = malloc(sizeof(struct hash_table_entries)*buckets);
	if (table->table ==NULL) {
		free(table);
		return NULL;
	}
	table->num_tables = buckets;
	memset(table->table, 0, sizeof(struct hash_table_entries)*buckets);
	return table;
}

static struct hash * find_in_list(struct hash *h, uint64 key)
{
	while (h) {
		if (h->key == key){
			return h;
		}
		h= h->next;
	}
	return NULL;
}


uint32 hash_insert(struct hash_table* table, uint64 key, void* data)
{
	struct hash * hash;
	//search in the list for this element

	if (find_in_list(table->table[table->hash_func(table, key)].next, key)) {
		return FAILURE;
	}
	hash = malloc(sizeof(struct hash));
	hash->key = key;
	hash->data = data;
	hash->next = table->table[table->hash_func(table, key)].next;
	table->table[table->hash_func(table, key)].next = hash;
	table->table[table->hash_func(table, key)].nelements++;
	table->nelements++;
	return SUCCESS;
}

uint32 hash_lookup(struct hash_table *table, uint64 key, void ** data)
{
	struct hash * hash = find_in_list(table->table[table->hash_func(table, key)].next, key);
	if (hash) {
		*data = hash->data;
		return SUCCESS;
	} else {
		*data = NULL;
	}
	return FAILURE;
}

uint32 hash_delete (struct hash_table*table,uint64 key, void ** data)
{
	struct hash * hash = find_in_list(table->table[table->hash_func(table, key)].next, key);
	struct hash *prev = NULL;
	if (!hash) {
		return FAILURE;
	}
	if (hash == table->table[table->hash_func(table, key)].next) {
		table->table[table->hash_func(table, key)].next = hash->next;
	} else {
		prev = table->table[table->hash_func(table, key)].next;
		while (prev->next != hash) {
			prev = prev->next;
		}
		prev->next = hash->next;
		hash->next = NULL;
	}
	table->table[table->hash_func(table, key)].nelements--;
	table->nelements--;
	*data = hash->data;
	free(hash);
	return SUCCESS;
}

uint32 hash_num_elements (struct hash_table*table)
{
	return table->nelements;
}

uint32 hash_num_elements_bucket(struct hash_table*table , uint32 bucket)
{
	return table->table[bucket].nelements;
}

