

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "lru.h"


struct lru * lru_init (uint32 max_elements)
{
	struct lru * lru = malloc(sizeof(*lru));
	memset(lru, 0, sizeof(*lru));
	lru->max_elements = max_elements;
	return lru;
}

struct lru_ele* lru_insert (struct lru *lru, uint64 key, uint64 *removed_key)
{
	struct lru_ele * removed_ele = NULL;
	struct lru_ele * ele = malloc(sizeof(*ele));
	memset(ele, 0, sizeof(*ele));
	ele->key = key;
	ele->prev = lru->head;
	lru->head->next = ele;
	lru->head = ele;
	if (lru->nelements == lru->max_elements) {
		removed_ele = lru->tail;
		lru->tail = lru->tail->next;
		lru->tail->prev = NULL;
		removed_ele->next = removed_ele->prev = NULL;
	} else {
		lru->nelements--;
	}
	if (removed_ele) {
		*removed_key = removed_ele->key;
	}
	return removed_ele;
}

uint32 lru_bump (struct lru * lru, struct lru_ele * ele) 
{
	if (ele == lru->head)	return SUCCESS;
	
	if (ele == lru->tail) {
		lru->tail = lru->tail->next;
		lru->tail->prev = NULL;
		ele->next = ele->prev = NULL;
	} else {
		ele->prev->next = ele->next;
		ele->next->prev = ele->prev;
		ele->next = ele->prev = NULL;
	}
	ele->prev = lru->head;
	lru->head->next = ele;
	ele->prev = lru->head;
	lru->head = ele;
	return SUCCESS;
}
	
		
