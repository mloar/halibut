/*
 * PostScript backend for Halibut
 */

#include "halibut.h"

paragraph *ps_config_filename(char *filename)
{
    return NULL;
}

void ps_backend(paragraph *sourceform, keywordlist *keywords,
		indexdata *idx, void *unused) {
    /*
     * FIXME
     */
    printf("[ps] %p = %s\n", unused, unused);
}
