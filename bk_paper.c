/*
 * Paper printing pre-backend for Halibut.
 * 
 * This module does all the processing common to both PostScript
 * and PDF output: selecting fonts, line wrapping and page breaking
 * in accordance with font metrics, laying out the contents and
 * index pages, generally doing all the page layout. After this,
 * bk_ps.c and bk_pdf.c should only need to do linear translations
 * into their literal output format.
 */

#include "halibut.h"

void *paper_pre_backend(paragraph *sourceform, keywordlist *keywords,
			indexdata *idx) {
    /*
     * FIXME
     */
    return "hello, world";
}
