/*
 * PostScript backend for Halibut
 */

#include <assert.h>
#include "halibut.h"
#include "paper.h"

paragraph *ps_config_filename(char *filename)
{
    paragraph *p;
    wchar_t *ufilename, *up;
    int len;

    p = mknew(paragraph);
    memset(p, 0, sizeof(*p));
    p->type = para_Config;
    p->next = NULL;
    p->fpos.filename = "<command line>";
    p->fpos.line = p->fpos.col = -1;

    ufilename = ufroma_dup(filename);
    len = ustrlen(ufilename) + 2 + lenof(L"ps-filename");
    p->keyword = mknewa(wchar_t, len);
    up = p->keyword;
    ustrcpy(up, L"ps-filename");
    up = uadv(up);
    ustrcpy(up, ufilename);
    up = uadv(up);
    *up = L'\0';
    assert(up - p->keyword < len);
    sfree(ufilename);

    return p;
}

void ps_backend(paragraph *sourceform, keywordlist *keywords,
		indexdata *idx, void *vdoc) {
    document *doc = (document *)vdoc;
    int font_index;
    font_encoding *fe;
    page_data *page;
    int pageno;
    FILE *fp;
    char *filename;
    paragraph *p;

    IGNORE(keywords);
    IGNORE(idx);

    filename = dupstr("output.ps");
    for (p = sourceform; p; p = p->next) {
	p->private_data = NULL;
	if (p->type == para_Config && p->parent) {
	    if (!ustricmp(p->keyword, L"ps-filename")) {
		sfree(filename);
		filename = utoa_dup(uadv(p->keyword));
	    }
	}
    }

    fp = fopen(filename, "w");
    if (!fp) {
	error(err_cantopenw, filename);
	return;
    }

    fprintf(fp, "%%!PS-Adobe-1.0\n");
    for (pageno = 0, page = doc->pages; page; page = page->next)
	pageno++;
    fprintf(fp, "%%%%Pages: %d\n", pageno);
    fprintf(fp, "%%%%EndComments\n");

    fprintf(fp, "%%%%BeginProlog\n");
    fprintf(fp, "%%%%EndProlog\n");

    fprintf(fp, "%%%%BeginSetup\n");
    /*
     * Re-encode and re-metric the fonts.
     */
    font_index = 0;
    for (fe = doc->fonts->head; fe; fe = fe->next) {
	char fname[40];
	int i;

	sprintf(fname, "f%d", font_index++);
	fe->name = dupstr(fname);

	fprintf(fp, "/%s findfont dup length dict begin\n", fe->font->name);
	fprintf(fp, "{1 index /FID ne {def} {pop pop} ifelse} forall\n");
	fprintf(fp, "/Encoding [\n");
	for (i = 0; i < 256; i++)
	    fprintf(fp, "/%s\n", fe->vector[i] ? fe->vector[i] : ".notdef");
	fprintf(fp, "] def /Metrics 256 dict dup begin\n");
	for (i = 0; i < 256; i++) {
	    if (fe->indices[i] >= 0) {
		double width = fe->font->widths[fe->indices[i]];
		fprintf(fp, "/%s %g def\n", fe->vector[i],
			1000.0 * width / 4096.0);
	    }
	}
	fprintf(fp, "end def currentdict end\n");
	fprintf(fp, "/fontname-%s exch definefont /%s exch def\n\n",
	       fe->name, fe->name);
    }
    fprintf(fp, "%%%%EndSetup\n");

    /*
     * Output the text.
     */
    pageno = 0;
    for (page = doc->pages; page; page = page->next) {
	text_fragment *frag;

	pageno++;
	fprintf(fp, "%%%%Page: %d %d\n", pageno, pageno);
	fprintf(fp, "%%%%BeginPageSetup\n");
	fprintf(fp, "%%%%EndPageSetup\n");

#if 0
	{
	    xref *xr;
	    /*
	     * I used this diagnostic briefly to ensure that
	     * cross-reference rectangles were being put where they
	     * should be.
	     */
	    for (xr = page->first_xref; xr; xr = xr->next) {
		fprintf(fp, "gsave 0.7 setgray %g %g moveto",
			xr->lx/4096.0, xr->ty/4096.0);
		fprintf(fp, " %g %g lineto %g %g lineto",
			xr->lx/4096.0, xr->by/4096.0,
			xr->rx/4096.0, xr->by/4096.0);
		fprintf(fp, " %g %g lineto closepath fill grestore\n",
			xr->rx/4096.0, xr->ty/4096.0);
	    }
	}
#endif

	for (frag = page->first_text; frag; frag = frag->next) {
	    char *c;

	    fprintf(fp, "%s %d scalefont setfont %g %g moveto (",
		   frag->fe->name, frag->fontsize,
		   frag->x/4096.0, frag->y/4096.0);

	    for (c = frag->text; *c; c++) {
		if (*c == '(' || *c == ')' || *c == '\\')
		    fputc('\\', fp);
		fputc(*c, fp);
	    }

	    fprintf(fp, ") show\n");
	}

	fprintf(fp, "showpage\n");
    }

    fprintf(fp, "%%%%EOF\n");

    fclose(fp);

    sfree(filename);
}
