/*
 * PDF backend for Halibut
 */

#include "halibut.h"

paragraph *pdf_config_filename(char *filename)
{
    return NULL;
}

void pdf_backend(paragraph *sourceform, keywordlist *keywords,
		 indexdata *idx, void *unused) {
    /*
     * FIXME
     */
    printf("[pdf] %p = %s\n", unused, unused);
}
