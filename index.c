/*
 * index.c: create and collate index data structures
 */

#include <stdio.h>
#include <stdlib.h>
#include "buttress.h"

#if 0
typedef struct tag_node23 node23;
typedef struct tag_tree23 tree23;
typedef struct tag_elem23 elem23;

int cmp23(elem23 *, elem23 *);

struct tag_elem23 {
    char *string;
};

struct tag_tree23 {
    node23 *root;
};

struct tag_node23 {
    node23 *parent;
    node23 *kids[3];
    elem23 *elems[2];
}

int cmp23(elem23 *a, elem23 *b) {
    return strcmp(a->string, b->string);
}

void add23(tree23 *t, elem23 *e) {
    node23 *n, *left, *right;
    int c;

    if (t->root == NULL) {
	t->root = mknew(node23);
	t->root->elems[1] = NULL;
	t->root->kids[0] = t->root->kids[1] = t->root->kids[2] = NULL;
	t->root->parent = NULL;
	t->root->elems[0] = e;
	return;
    }

    np = &t->root;
    while (*np) {
	n = *np;
	c = cmp32(e, n->elems[0]);
	if (c == 0) {
	    printf("present already\n");
	    return;
	} else if (c < 0) {
	    np = &n->kids[0];
	} else {
	    if (n->elems[1] == NULL || (c = cmp32(e, n->elems[1])) < 0)
		np = &n->kids[1];
	    else if (c > 0)
		np = &n->kids[2];
	    else {
		printf("present already\n");
		return;
	    }
	}
    }

    /*
     * We need to insert the new element in n at position np.
     */
    left = NULL;
    right = NULL;
    while (n) {
	if (n->elems[1] == NULL) {
	    /*
	     * Insert in a 2-node; simple.
	     */
	    if (np == &n->kids[0]) {
		n->kids[2] = n->kids[1];
		n->elems[1] = n->elems[0];
		n->kids[1] = right;
		n->elems[0] = e;
		n->kids[0] = left;
	    } else { /* np == &n->kids[1] */
		n->kids[2] = right;
		n->elems[1] = e;
		n->kids[1] = left;
	    }
	    break;
	} else {
	    node23 *m = mknew(node23);
	    /*
	     * Insert in a 3-node; split into two 2-nodes and move
	     * focus up a level.
	     */
	    if (np == &n->kids[0]) {
		m->kids[0] = left;
		m->elems[0] = e;
		m->kids[1] = right;
		e = n->elems[0];
		n->kids[0] = n->kids[1];
		n->elems[1] = n->elems[0];
		n->kids[1] = n->kids[2];
	    } else if (np == &n->kids[1]) {
		m->kids[0] = n->kids[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = left;
		/* e = e; */
		n->kids[0] = right;
		n->elems[0] = n->elems[1];
		n->kids[1] = n->kids[2];
	    } else { /* np == &n->kids[2] */
		m->kids[0] = n->kids[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = n->kids[1];
		e = n->elems[1];
		n->kids[0] = left;
		n->elems[0] = e;
		n->kids[1] = right;
	    }
	    m->kids[2] = n->kids[2] = NULL;
	    m->elems[1] = n->elems[1] = NULL;
	    left = m;
	    right = n;
	}
	n = n->parent;
    }

    /*
     * If we've come out of here by `break', n will still be
     * non-NULL and we've finished. If we've come here because n is
     * NULL, we need to create a new root for the tree because the
     * old one has just split into two.
     */
    if (!n) {
	t->root = mknew(node23);
	t->root->kids[0] = left;
	t->root->elems[0] = e;
	t->root->kids[1] = right;
	t->root->elems[1] = NULL;
	t->root->kids[2] = NULL;
    }
}

void find23(tree23 *t, elem23 *e) {
}
#endif

static void dbg_prtwordlist(int level, word *w);

void index_merge(int is_explicit, wchar_t *tags, word *text) {
    wchar_t *wp;
    printf("\\IM: %splicit: ", is_explicit ? "ex" : "im");
    if (tags) {
	wp = tags;
	while (*wp) {
	    putchar('\"');
	    for (; *wp; wp++)
		putchar(*wp);
	    putchar('\"');
	    if (*++wp)
		printf(", ");
	}
    } else
	printf("(no keyword)");
    printf(" {\n");
    dbg_prtwordlist(1, text);
    printf("}\n");
    free_word_list(text);
}

static void dbg_prtwordlist(int level, word *w) {
    for (; w; w = w->next) {
	wchar_t *wp;
	printf("%*sword %d ", level*4, "", w->type);
	if (w->text) {
	    printf("\"");
	    for (wp = w->text; *wp; wp++)
		    putchar(*wp);
	    printf("\"");
	} else
	    printf("(no text)");
	if (w->alt) {
	    printf(" alt = {\n");
	    dbg_prtwordlist(level+1, w->alt);
	    printf("%*s}", level*4, "");
	}
	printf("\n");
    }
}
