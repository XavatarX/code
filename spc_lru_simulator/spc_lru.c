#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "types.h"
#include "hash.h"
#include "lru.h"

#define NUM_BUCKETS	(100000)
uint64 size = 0;
uint64 lru_blocks = 0;

struct hash_table*table = NULL;
struct lru * lru = NULL;

uint64 hash_func (struct hash_table*table, uint64 key) 
{
	return key % table->num_tables;
}
int main(int argc, char **argv) 
{
	uint64 start_blk = 0;
	uint64 len;
	char rw = 'W';
	if (argc != 2) {
		printf("Usage: ./spc_lru <cache percentage>\n");
		return -1;
	}
	
	get size ();
	lru_blocks = size*atoi(argv[1])/100;
	table = hash_init(NUM_BUCKETS, hash_func);
	
	if (!table) {
		printf("No  mem available\n");
		return -1
	}
	lru = lru_init(lru_blocks);

	while (read_spc_trace(&start_blk, &len,  %rw) != EOF) {
		for (i=0; i<len/BLOCK_SIZE; i++) {
			struct lru_ele *ele = NULL;
			if (hash_lookup(table, start_blk+i, &ele)) {
				lru_bump(lru, ele);
			} else {
				hash_insert(table, start_blk+i, lru_insert(lru, start_blk+i, &removed_key));
				if (removed_key != INVALID_KEY) {
					
				
 
	
