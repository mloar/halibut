/*
 * tree23.c: reasonably generic 2-3 tree routines. Currently only
 * supports insert and find operations.
 */

#include <stdio.h>		       /* we only need this for NULL :-) */

/*
 * This module should be easily de-Buttressable. All it relies on
 * from the rest of Buttress is the macro `mknew':
 *
 *   #define mknew(typ) ( (typ *) malloc_like_function (sizeof (typ)) )
 *
 * and the free-like function `sfree'.
 */
#include "buttress.h"

typedef struct node23_Tag node23;
/* typedef struct tree23_Tag tree23; */

struct tree23_Tag {
    node23 *root;
};

struct node23_Tag {
    node23 *parent;
    node23 *kids[3];
    void *elems[2];
};

/*
 * Create a 2-3 tree.
 */
tree23 *newtree23(void) {
    tree23 *ret = mknew(tree23);
    ret->root = NULL;
    return ret;
}

/*
 * Free a 2-3 tree (not including freeing the elements).
 */
static void freenode23(node23 *n) {
    if (!n)
	return;
    freenode23(n->kids[0]);
    freenode23(n->kids[1]);
    freenode23(n->kids[2]);
    sfree(n);
}
void freetree23(tree23 *t) {
    freenode23(t->root);
    sfree(t);
}

/*
 * Add an element e to a 2-3 tree t. Returns e on success, or if an
 * existing element compares equal, returns that.
 */
void *add23(tree23 *t, void *e, int (*cmp)(void *, void *)) {
    node23 *n, **np, *left, *right;
    void *orig_e = e;
    int c;

    if (t->root == NULL) {
	t->root = mknew(node23);
	t->root->elems[1] = NULL;
	t->root->kids[0] = t->root->kids[1] = t->root->kids[2] = NULL;
	t->root->parent = NULL;
	t->root->elems[0] = e;
	return orig_e;
    }

    np = &t->root;
    while (*np) {
	n = *np;
	c = cmp(e, n->elems[0]);
	if (c == 0) {
	    /* node already exists; return existing one */
	    return n->elems[0];
	} else if (c < 0) {
	    np = &n->kids[0];
	} else {
	    if (n->elems[1] == NULL || (c = cmp(e, n->elems[1])) < 0)
		np = &n->kids[1];
	    else if (c > 0)
		np = &n->kids[2];
	    else {
		/* node already exists; return existing one */
		return n->elems[1];
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
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    if (n->kids[2]) n->kids[2]->parent = n;
	    break;
	} else {
	    node23 *m = mknew(node23);
	    m->parent = n->parent;
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
		n->elems[0] = n->elems[1];
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
		n->kids[0] = left;
		n->elems[0] = e;
		n->kids[1] = right;
		e = n->elems[1];
	    }
	    m->kids[2] = n->kids[2] = NULL;
	    m->elems[1] = n->elems[1] = NULL;
	    if (m->kids[0]) m->kids[0]->parent = m;
	    if (m->kids[1]) m->kids[1]->parent = m;
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    left = m;
	    right = n;
	}
	if (n->parent)
	    np = (n->parent->kids[0] == n ? &n->parent->kids[0] :
		  n->parent->kids[1] == n ? &n->parent->kids[1] :
		  &n->parent->kids[2]);
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
	t->root->parent = NULL;
	if (t->root->kids[0]) t->root->kids[0]->parent = t->root;
	if (t->root->kids[1]) t->root->kids[1]->parent = t->root;
    }

    return orig_e;
}

/*
 * Find an element e in a 2-3 tree t. Returns NULL if not found. e
 * is always passed as the first argument to cmp, so cmp can be an
 * asymmetric function if desired.
 */
void *find23(tree23 *t, void *e, int (*cmp)(void *, void *)) {
    node23 *n;
    int c;

    if (t->root == NULL)
	return NULL;

    n = t->root;
    while (n) {
	c = cmp(e, n->elems[0]);
	if (c == 0) {
	    return n->elems[0];
	} else if (c < 0) {
	    n = n->kids[0];
	} else {
	    if (n->elems[1] == NULL || (c = cmp(e, n->elems[1])) < 0)
		n = n->kids[1];
	    else if (c > 0)
		n = n->kids[2];
	    else {
		return n->elems[1];
	    }
	}
    }

    /*
     * We've found our way to the bottom of the tree and we know
     * where we would insert this node if we wanted to. But it
     * isn't there.
     */
    return NULL;
}

/*
 * Iterate over the elements of a tree23, in order.
 */
void *first23(tree23 *t, enum23 *e) {
    node23 *n = t->root;
    if (!n)
	return NULL;
    while (n->kids[0])
	n = n->kids[0];
    e->node = n;
    e->posn = 0;
    return n->elems[0];
}

void *next23(tree23 *t, enum23 *e) {
    node23 *n = (node23 *)e->node;
    int pos = e->posn;

    if (n->kids[pos+1]) {
	n = n->kids[pos+1];
	while (n->kids[0])
	    n = n->kids[0];
	e->node = n;
	e->posn = 0;
	return n->elems[0];
    }

    if (pos == 0 && n->elems[1]) {
	e->posn = 1;
	return n->elems[1];
    }

    do {
	node23 *nn = n->parent;
	if (nn == NULL)
	    return NULL;	       /* end of tree */
	pos = (nn->kids[0] == n ? 0 :
	       nn->kids[1] == n ? 1 : 2);
	n = nn;
    } while (pos == 2 || n->kids[pos+1] == NULL);

    e->node = n;
    e->posn = pos;
    return n->elems[pos];
}
