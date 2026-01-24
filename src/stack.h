/*
 * Copyright (C) 2022-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef STACK_H 
#define STACK_H

struct stack_rec {
	void *data;
	size_t len;
};

struct stack {
	struct stack_rec *recs;
	size_t num_slots;
	size_t num_recs;
};


struct stack* stk_alloc(void);

void stk_free(struct stack *stk);

int stk_push(struct stack *stk, const void *data, size_t len);

void* stk_pop(struct stack *stk, size_t *len);

void* stk_iterate(struct stack *stk, size_t *it, size_t *len);

#endif /* STACK_H */
