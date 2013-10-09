#ifndef _LRU_H_
#define _LRU_H_
#include "types.h"
#define INVALID_KEY 0xFFFFFFFFFFFFFFFF
struct lru_ele {
	uint64 key;
	struct lru_ele *next;
	struct lru_ele *prev;
};

struct lru {
	struct lru_ele *head;
	struct lru_ele *tail;
	uint32 max_elements;
	uint32 nelements;
};


struct lru * lru_init (uint32 max_elements);
struct lru_ele* lru_insert (struct lru *lru, uint64 key, uint64 *removed_key);
uint32 lru_bump (struct lru * lru, struct lru_ele * ele);
#endif
