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

    s = mknew(struct stackTag);
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
	s->data = resize(s->data, s->size);
    }
    s->data[s->sp++] = item;
}

void *stk_pop(stack s) {
    if (s->sp > 0)
	return s->data[--s->sp];
    else
	return NULL;
}

int compare_wordlists(word *a, word *b) {
    int t;
    while (a && b) {
	if (a->type != b->type)
	    return (a->type < b->type ? -1 : +1);   /* FIXME? */
	t = a->type;
	if ((t != word_Normal && t != word_Code &&
	     t != word_WeakCode && t != word_Emph) ||
	    a->alt || b->alt) {
	    int c;
	    if (a->text && b->text) {
		c = ustricmp(a->text, b->text);
		if (c)
		    return c;
	    }
	    c = compare_wordlists(a->alt, b->alt);
	    if (c)
		return c;
	    a = a->next;
	    b = b->next;
	} else {
	    wchar_t *ap = a->text, *bp = b->text;
	    while (*ap && *bp) {
		wchar_t ac = utolower(*ap), bc = utolower(*bp);
		if (ac != bc)
		    return (ac < bc ? -1 : +1);
		if (!*++ap && a->next && a->next->type == t && !a->next->alt)
		    a = a->next, ap = a->text;
		if (!*++bp && b->next && b->next->type == t && !b->next->alt)
		    b = b->next, bp = b->text;
	    }
	    if (*ap || *bp)
		return (*ap ? +1 : -1);
	    a = a->next;
	    b = b->next;
	}
    }

    if (a || b)
	return (a ? +1 : -1);
    else
	return 0;
}
