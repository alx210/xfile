/*
 * Copyright (C) 2022-2024 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "stack.h"
#include "debug.h"
#include "memdb.h" /* must be the last header */

#ifndef STK_GROWBY
#define STK_GROWBY 64
#endif

struct stack* stk_alloc(void)
{
	struct stack *stk;
	
	stk = calloc(1, sizeof(struct stack));
	if(!stk) return NULL;
	
	stk->num_recs = 0;
	stk->num_slots = STK_GROWBY;
	stk->recs = malloc(STK_GROWBY * sizeof(struct stack_rec));
	if(!stk->recs) {
		free(stk);
		return NULL;
	}
	return stk;
}

void stk_free(struct stack *stk)
{
	unsigned int i;
	
	for(i = 0; i < stk->num_recs; i++)
		free(stk->recs[i].data);

	free(stk->recs);
	free(stk);
}

int stk_push(struct stack *stk, const void *data, size_t len)
{
	if(stk->num_recs + 1 == stk->num_slots) {
		struct stack_rec *recs;

		recs = realloc(stk->recs,
			(stk->num_slots + STK_GROWBY) * sizeof(struct stack_rec));
		if(!recs) return errno;
		stk->num_slots += STK_GROWBY;
		stk->recs = recs;
	}
	stk->recs[stk->num_recs].len = len;
	stk->recs[stk->num_recs].data = malloc(len);
	if(!stk->recs[stk->num_recs].data) return errno;
	memcpy(stk->recs[stk->num_recs].data, data, len);
	
	stk->num_recs++;
	return 0;
}

void* stk_pop(struct stack *stk, size_t *len)
{
	if(stk->num_recs == 0) return NULL;
	
	stk->num_recs--;

	if(len) *len = stk->recs[stk->num_recs].len;
	
	return stk->recs[stk->num_recs].data;
}

void* stk_iterate(struct stack *stk, size_t *len, size_t *it)
{
	void *p;
	
	if(*it >= stk->num_recs) return NULL;
	
	p = stk->recs[*it].data;
	if(len) *len = stk->recs[*it].len;
	*it = (*it + 1);

	return p;
}
