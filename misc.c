/*
 * misc.c: miscellaneous useful items
 */

#include "buttress.h"

struct stackTag {
    void **data;
    int sp;
    int size;
};

stack stk_new(void) {
    stack s;

    s = smalloc(sizeof(*s));
    s->sp = 0;
    s->size = 0;
    s->data = NULL;

    return s;
}

void stk_free(stack s) {
    sfree(s->data);
    sfree(s);
}

void stk_push(stack s, void *item) {
    if (s->size <= s->sp) {
	s->size = s->sp + 32;
	s->data = srealloc(s->data, s->size * sizeof(*s->data));
    }
    s->data[s->sp++] = item;
}

void *stk_pop(stack s) {
    if (s->sp > 0)
	return s->data[--s->sp];
    else
	return NULL;
}
