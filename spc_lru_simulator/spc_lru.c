#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "types.h"
#include "hash.h"
#include "lru.h"

#define NUM_BUCKETS	(10000000)
#define BLOCK_SIZE (512)
uint64 size = 0;
uint64 lru_blocks = 0;

struct hash_table*table = NULL;
struct lru * lru = NULL;
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
	struct lru_ele *ele = NULL;
	int i = 0;
	char rw = 'W';
	if (argc != 2) {
		printf("Usage: ./spc_lru <cache percentage>\n");
		return -1;
	}
	
	get_size ();
	lru_blocks = size*atoi(argv[1])/100;
	table = hash_init(NUM_BUCKETS, hash_func);
	
	if (!table) {
		printf("No  mem available\n");
		return -1;
	}
	lru = lru_init(lru_blocks);

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
	printf("%d, %lld %lld %lld\n",atoi(argv[1]), hits, misses, table->nelements);
	return 0;
}			
 
	
