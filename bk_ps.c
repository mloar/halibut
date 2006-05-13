/*
 * PostScript backend for Halibut
 */

#include <assert.h>
#include "halibut.h"
#include "paper.h"

static void ps_comment(FILE *fp, char const *leader, word *words);

paragraph *ps_config_filename(char *filename)
{
    return cmdline_cfg_simple("ps-filename", filename, NULL);
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
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"ps-filename")) {
		sfree(filename);
		filename = dupstr(adv(p->origkeyword));
	    }
	}
    }

    fp = fopen(filename, "w");
    if (!fp) {
	error(err_cantopenw, filename);
	return;
    }

    fprintf(fp, "%%!PS-Adobe-3.0\n");
    fprintf(fp, "%%%%Creator: Halibut, %s\n", version);
    fprintf(fp, "%%%%DocumentData: Clean8Bit\n");
    fprintf(fp, "%%%%LanguageLevel: 1\n");
    for (pageno = 0, page = doc->pages; page; page = page->next)
	pageno++;
    fprintf(fp, "%%%%Pages: %d\n", pageno);
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Title)
	    ps_comment(fp, "%%Title: ", p->words);
    fprintf(fp, "%%%%DocumentNeededResources:\n");
    for (fe = doc->fonts->head; fe; fe = fe->next)
	/* XXX This may request the same font multiple times. */
	fprintf(fp, "%%%%+ font %s\n", fe->font->info->name);
    fprintf(fp, "%%%%DocumentSuppliedResources: procset Halibut 0 0\n");
    fprintf(fp, "%%%%EndComments\n");

    fprintf(fp, "%%%%BeginProlog\n");
    fprintf(fp, "%%%%BeginResource: procset Halibut 0 0\n");
    /*
     * Supply a prologue function which allows a reasonably
     * compressed representation of the text on the pages.
     * 
     * Expects two arguments: a y-coordinate, and then an array.
     * Elements of the array are processed sequentially as follows:
     * 
     *  - a number is treated as an x-coordinate
     *  - an array is treated as a (font, size) pair
     *  - a string is shown
     */
    fprintf(fp,
	    "/tdict 4 dict dup begin\n"
	    "  /arraytype {aload pop scalefont setfont} bind def\n"
	    "  /realtype {1 index moveto} bind def\n"
	    "  /integertype /realtype load def\n"
	    "  /stringtype {show} bind def\n"
	    "end def\n"
	    "/t { tdict begin {dup type exec} forall end pop } bind def\n");

    fprintf(fp, "%%%%EndResource\n");
    fprintf(fp, "%%%%EndProlog\n");

    fprintf(fp, "%%%%BeginSetup\n");

    /*
     * This is as good a place as any to put version IDs.
     */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID)
	    ps_comment(fp, "% ", p->words);

    for (fe = doc->fonts->head; fe; fe = fe->next)
	/* XXX This may request the same font multiple times. */
	fprintf(fp, "%%%%IncludeResource: font %s\n", fe->font->info->name);

    /*
     * Re-encode the fonts.
     */
    font_index = 0;
    for (fe = doc->fonts->head; fe; fe = fe->next) {
	char fname[40];
	int i;

	sprintf(fname, "f%d", font_index++);
	fe->name = dupstr(fname);

	fprintf(fp, "/%s findfont dup length dict begin\n",
	    fe->font->info->name);
	fprintf(fp, "{1 index /FID ne {def} {pop pop} ifelse} forall\n");
	fprintf(fp, "/Encoding [\n");
	for (i = 0; i < 256; i++)
	    fprintf(fp, "/%s%c", fe->vector[i] ? fe->vector[i] : ".notdef",
		    i % 4 == 3 ? '\n' : ' ');
	fprintf(fp, "] def\n");
	fprintf(fp, "currentdict end\n");
	fprintf(fp, "/fontname-%s exch definefont /%s exch def\n\n",
	       fe->name, fe->name);
    }
    fprintf(fp, "%%%%EndSetup\n");

    /*
     * Output the text and graphics.
     */
    pageno = 0;
    for (page = doc->pages; page; page = page->next) {
	text_fragment *frag, *frag_end;
	rect *r;

	pageno++;
	fprintf(fp, "%%%%Page: %d %d\n", pageno, pageno);
	fprintf(fp, "save\n");

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
			xr->lx/FUNITS_PER_PT, xr->ty/FUNITS_PER_PT);
		fprintf(fp, " %g %g lineto %g %g lineto",
			xr->lx/FUNITS_PER_PT, xr->by/FUNITS_PER_PT,
			xr->rx/FUNITS_PER_PT, xr->by/FUNITS_PER_PT);
		fprintf(fp, " %g %g lineto closepath fill grestore\n",
			xr->rx/FUNITS_PER_PT, xr->ty/FUNITS_PER_PT);
	    }
	}
#endif

	for (r = page->first_rect; r; r = r->next) {
	    fprintf(fp, "%g %g moveto %g 0 rlineto 0 %g rlineto "
		    "-%g 0 rlineto closepath fill\n",
		    r->x / FUNITS_PER_PT, r->y / FUNITS_PER_PT,
		    r->w / FUNITS_PER_PT, r->h / FUNITS_PER_PT,
		    r->w / FUNITS_PER_PT);
	}

	frag = page->first_text;
	while (frag) {
	    font_encoding *fe;
	    int fs;
	    char *c;

	    /*
	     * Collect all the adjacent text fragments with the
	     * same y-coordinate.
	     */
	    for (frag_end = frag;
		 frag_end && frag_end->y == frag->y;
		 frag_end = frag_end->next);

	    fprintf(fp, "%g[", frag->y / FUNITS_PER_PT);

	    fe = NULL;
	    fs = -1;

	    while (frag && frag != frag_end) {

		if (frag->fe != fe || frag->fontsize != fs)
		    fprintf(fp, "[%s %d]", frag->fe->name, frag->fontsize);
		fe = frag->fe;
		fs = frag->fontsize;

		fprintf(fp, "%g(", frag->x/FUNITS_PER_PT);
		for (c = frag->text; *c; c++) {
		    if (*c == '(' || *c == ')' || *c == '\\')
			fputc('\\', fp);
		    fputc(*c, fp);
		}
		fprintf(fp, ")");

		frag = frag->next;
	    }

	    fprintf(fp, "]t\n");
	}

	fprintf(fp, "restore showpage\n");
    }

    fprintf(fp, "%%%%EOF\n");

    fclose(fp);

    sfree(filename);
}

static void ps_comment(FILE *fp, char const *leader, word *words)
{
    fprintf(fp, "%s", leader);

    for (; words; words = words->next) {
	char *text;
	int type;

	switch (words->type) {
	  case word_HyperLink:
	  case word_HyperEnd:
	  case word_UpperXref:
	  case word_LowerXref:
	  case word_XrefEnd:
	  case word_IndexRef:
	    continue;
	}

	type = removeattr(words->type);

	switch (type) {
	  case word_Normal:
	    text = utoa_dup(words->text, CS_ASCII);
	    break;
	  case word_WhiteSpace:
	    text = dupstr(" ");
	    break;
	  case word_Quote:
	    text = dupstr("'");
	    break;
	}

	fputs(text, fp);
	sfree(text);
    }

    fprintf(fp, "\n");
}
