#ifndef _LRU_LOWMEM_H_
#define _LRU_LOWMEM_H_
#include "types.h"
#define INVALID_KEY 0xFFFFFFFFFFFFFFFF
#if 0
struct lowmemlru_ele {
	uint64 key;
	struct lowmemlru_ele *next;
	struct lowmemlru_ele *prev;
};
#endif
struct tagTable {
	uint32 tagTable;
	uint32 tagIOCount;
	struct tagTable * next;
};

struct lowmemlru {
	uint32 lowmemblks;
	uint32 ioCounter;
	uint32 bumpupCounter;
	uint32 globalTag;
	uint32 lowmemlruMaxTag;
	uint32 blocksPresent;
	uint32 cacheSize;
	struct tagTable *latestEntry;
	struct tagTable *tagTable;
};

struct lowmemlru_ele {
	uint64 key;
	uint32 blockTag;
};
struct lowmemlru * lru_init (uint32 lruPct, uint32 maxBlks);
struct lowmemlru_ele* lru_insert (struct lowmemlru *lru, uint64 key, uint64 *removed_key);
uint32 lru_bump (struct lowmemlru * lru, struct lowmemlru_ele * ele);


	
#endif
