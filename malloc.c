/*
 * malloc.c: safe wrappers around malloc, realloc, free, strdup
 */

#include <stdlib.h>
#include "buttress.h"

/*
 * smalloc should guarantee to return a useful pointer - buttress
 * can do nothing except die when it's out of memory anyway
 */
void *smalloc(int size) {
    void *p = malloc(size);
    if (!p)
	fatal(err_nomemory);
    return p;
}

/*
 * sfree should guaranteeably deal gracefully with freeing NULL
 */
void sfree(void *p) {
    if (p)
	free(p);
}

/*
 * srealloc should guaranteeably be able to realloc NULL
 */
void *srealloc(void *p, int size) {
    void *q;
    if (p)
	q = realloc(p, size);
    else
	q = malloc(size);
    if (!q)
	fatal(err_nomemory);
    return p;
}
