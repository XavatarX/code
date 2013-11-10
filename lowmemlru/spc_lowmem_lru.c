#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "types.h"
#include "hash.h"
#include "lowmem_lru.h"
#include <string.h>
#define NUM_BUCKETS	(10000000)
#define BLOCK_SIZE (512)
uint64 size = 0;
uint64 lru_blocks = 0;

struct hash_table* table = NULL;
struct lowmemlru * lru = NULL;
uint64 hits = 0, misses = 0;
uint64 hash_func (struct hash_table*table, uint64 key) 
{
	return key % table->num_tables;
}

void get_size ()
{
	scanf("%lld\n",&size);
}

int read_spc_trace(uint64 *start, uint64 *len, char *rw)
{
	return scanf("%lld %lld %c",start,len,rw);
}
int main(int argc, char **argv) 
{
	uint64 start_blk = 0;
	uint64 len;
	uint64 removed_key = INVALID_KEY;
	int lowmem = 0;
	struct lowmemlru_ele *ele = NULL;
	int i = 0;
	char rw = 'W';
	if (argc != 3) {
		printf("Usage: ./spc_lru <cache percentage> <lowmemsimulation[0/1]> \n");
		return -1;
	}
	
	get_size ();
	lru_blocks = size*atoi(argv[1])/100;
	if (lowmem) {
		lru_blocks = lru_blocks - ((lru_blocks*3)/2)/100;
	}

	table = hash_init(NUM_BUCKETS, hash_func);
	
	if (!table) {
		printf("No  mem available\n");
		return -1;
	}
	lru = lru_init(atoi(argv[2]), lru_blocks);

	while (read_spc_trace(&start_blk, &len,  &rw) != EOF) {
		for (i=0; i<len/BLOCK_SIZE; i++) {
			if (hash_lookup(table, start_blk+i, (void **)&ele)) {
				hits++;
				lru_bump(lru, ele);
			} else {
				misses++;
				hash_insert(table, start_blk+i, lru_insert(lru, start_blk+i, &removed_key));
				if (removed_key != INVALID_KEY) {
					hash_delete(table, removed_key, NULL);
				}
			}
		}
	}
	printf("%d, %lld %lld %d\n",atoi(argv[1]), hits, misses, table->nelements);
	return 0;
}			
 
/* ----------------------- low mem lru----------------------- */


struct lowmemlru * lru_init (uint32 lruPct, uint32 lruSizeInBlks)
{
	struct lowmemlru * llru = malloc(sizeof(struct lowmemlru));
	
	memset((void *)llru, 0, sizeof(struct lowmemlru));
#if 0
	llowmemlru->tag_table = malloc(sizeof(struct tagTable)*100(lruPct/2));
	memset(llowmemlru->tag_table, 0, sizeof(struct tagTable)*100(lruPct/2));
#endif
	llru->bumpupCounter = ((lruPct*lruSizeInBlks)/100)/2;
	llru->lowmemblks = ((lruPct*lruSizeInBlks)/100);
	llru->cacheSize = lruSizeInBlks;
	return llru;
}

void decrement_tag_count(struct lowmemlru *lru, struct tagTable *tagEntry);

static void remove_blk(struct lowmemlru *lru)
{
	uint32 rand = 0, count = 0;
	int i = 0, counter = 0;
	
	struct hash * hash_entry = NULL, * prev = NULL;
	for (counter = 0; counter < 1000; counter++) {
		rand = random() % table->nelements;
		rand = rand+1;
		i=0;
		while (((count + table->table[i].nelements) < rand) && i < table->num_tables) {
			count += table->table[i].nelements;
			i++;
		}
		if (!(i < table->num_tables)) {
			printf("Going beyound buckets\n");
		} 
		count = rand - count;
		hash_entry = table->table[i].next;
		while (--count && hash_entry)	{
			prev = hash_entry;
			hash_entry = hash_entry->next;
		}
		if (!hash_entry) {
			printf("Going beyound entries\n");
		}
		if (((struct lowmemlru_ele *)hash_entry->data)->blockTag  <= lru->lowmemlruMaxTag) {

			if (hash_entry == table->table[i].next) {
				table->table[i].next = table->table[i].next->next;
			} else {
				prev->next = hash_entry->next;
			}
			table->table[i].nelements--;
			// remove entry from lru.
			decrement_tag_count (lru, lru->tagTable);		
			return;
		}
	}
	printf("--------Didnt find in 1000 iterations\n");
	return;
}

static void incement_lowmemlru_iocounter(struct lowmemlru *lru)
{
	struct tagTable *table = NULL;
	lru->ioCounter++;
	if (lru->ioCounter == lru->bumpupCounter) {
		table = malloc(sizeof(*table));
		memset(table, 0, sizeof(*table));
		lru->ioCounter = 0;
		table->tagTable = lru->globalTag++;
		table->tagIOCount = lru->bumpupCounter;
		if (lru->latestEntry == NULL) {
			lru->latestEntry = lru->tagTable = table;
		} else {
			lru->latestEntry->next = table;
			lru->latestEntry = table;
		}
	}
	return ;
}
struct lowmemlru_ele* 
lru_insert (struct lowmemlru *lru, uint64 key, uint64 *removed_key)
{
	struct lowmemlru_ele* ele = malloc(sizeof(struct lowmemlru_ele));
	memset(ele, 0, sizeof(struct lowmemlru_ele));
	ele->key = key;
	ele->blockTag = lru->globalTag;
	incement_lowmemlru_iocounter(lru);
	lru->blocksPresent++;
	if (lru->blocksPresent > lru->cacheSize) {
		remove_blk (lru);
	}
	return ele;
}
void decrement_tag_count(struct lowmemlru *lru, struct tagTable *tagEntry)
{
	struct tagTable *tmp = NULL;
	tagEntry->tagIOCount--;
	/*
	 * if its the first tag entry then see to it that there are lrupct blocks in there
	 */
	if (tagEntry->tagTable == lru->lowmemlruMaxTag) {
		if (tagEntry->tagIOCount < lru->lowmemblks) {
			if (tagEntry->next != NULL) {
				tagEntry->next->tagIOCount += tagEntry->tagIOCount;
				lru->tagTable = tagEntry->next;
				tagEntry->next = NULL;
				free(tagEntry);
			}
		}
	} else {
		if (tagEntry->next) {
			if (((tagEntry->tagIOCount + tagEntry->next->tagIOCount) < (lru->bumpupCounter)) && 
						tagEntry->next){
				tagEntry->tagIOCount += tagEntry->next->tagIOCount;
				tagEntry->tagTable = tagEntry->next->tagTable;
				tmp = tagEntry->next;
				tagEntry->next = tagEntry->next->next;
				if (tmp == lru->latestEntry) {
					lru->latestEntry = tagEntry;
				}
				free(tmp);
			}
		}
	}
	return;
}
uint32 lru_bump(struct lowmemlru*lru, struct lowmemlru_ele* ele)
{
	struct tagTable *tagEntry = lru->tagTable;
	if (ele->blockTag > lru->latestEntry->tagTable) {
		return;
	}
	lru->ioCounter++;
	while ((tagEntry->tagTable < ele->blockTag)) {
		tagEntry= tagEntry->next;
	}
	decrement_tag_count(lru, tagEntry);
	
	return;
}




