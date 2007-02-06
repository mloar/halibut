/*
 * PostScript backend for Halibut
 */

#include <assert.h>
#include <stdarg.h>
#include "halibut.h"
#include "paper.h"

/* Ideal number of characters per line, for use in PostScript code */
#define PS_WIDTH 79
/* Absolute maxiumum characters per line, for use in DSC comments */
#define PS_MAXWIDTH 255

static void ps_comment(FILE *fp, char const *leader, word *words);
static void ps_string_len(FILE *fp, int *cc, char const *str, int len);
static void ps_string(FILE *fp, int *cc, char const *str);

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
    outline_element *oe;
    int noe;
    int cc; /* Character count on current line */

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
    fprintf(fp, "%%%%DocumentData: Clean7Bit\n");
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
	if (!fe->font->info->fontfile)
	    fprintf(fp, "%%%%+ font %s\n", fe->font->info->name);
    fprintf(fp, "%%%%DocumentSuppliedResources: procset Halibut 0 3\n");
    for (fe = doc->fonts->head; fe; fe = fe->next)
	/* XXX This may request the same font multiple times. */
	if (fe->font->info->fontfile)
	    fprintf(fp, "%%%%+ font %s\n", fe->font->info->name);
    fprintf(fp, "%%%%EndComments\n");

    fprintf(fp, "%%%%BeginProlog\n");
    fprintf(fp, "%%%%BeginResource: procset Halibut 0 3\n");
    /*
     * Supply a prologue function which allows a reasonably
     * compressed representation of the text on the pages.
     * 
     * "t" expects two arguments: a y-coordinate, and then an array.
     * Elements of the array are processed sequentially as follows:
     * 
     *  - a number is treated as an x-coordinate
     *  - an array is treated as a (font, size) pair
     *  - a string is shown
     *
     * "r" takes four arguments, and behaves like "rectfill".
     */
    fprintf(fp,
	    "/tdict 4 dict dup begin\n"
	    "  /arraytype {aload pop scalefont setfont} bind def\n"
	    "  /realtype {1 index moveto} bind def\n"
	    "  /integertype /realtype load def\n"
	    "  /stringtype {show} bind def\n"
	    "end def\n"
	    "/t { tdict begin {dup type exec} forall end pop } bind def\n"
	    "/r { 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto\n"
	    "     neg 0 rlineto closepath fill } bind def\n");
    /*
     * pdfmark wrappers
     *
     * "p" generates a named destination referencing this page.
     * "x" generates a link to a named destination.
     * "u" generates a link to a URI.
     * "o" generates an outline entry.
     * "m" generates a general pdfmark.
     *
     * They all do nothing if pdfmark is undefined.
     */
    fprintf(fp,
	    "/pdfmark where { pop\n"
	    "  /p { [ /Dest 3 -1 roll /View [ /XYZ null null null ]\n"
	    "       /DEST pdfmark } bind def\n"
	    "  /x { [ /Dest 3 -1 roll /Rect 5 -1 roll /Border [0 0 0]\n"
	    "       /Subtype /Link /ANN pdfmark } bind def\n"
	    "  /u { 2 dict dup /Subtype /URI put dup /URI 4 -1 roll put\n"
	    "       [ /Action 3 -1 roll /Rect 5 -1 roll /Border [0 0 0]\n"
	    "       /Subtype /Link /ANN pdfmark } bind def\n"
	    "  /o { [ /Count 3 -1 roll /Dest 5 -1 roll /Title 7 -1 roll\n"
	    "       /OUT pdfmark } bind def\n"
	    "  /m /pdfmark load def\n"
	    "}\n");
    fprintf(fp, "{\n"
	    "  /p { pop } bind def\n"
	    "  /x { pop pop } bind def\n"
	    "  /u /x load def\n"
	    "  /o { pop pop pop } bind def\n"
	    "  /m /cleartomark load def\n"
	    "} ifelse\n");

    fprintf(fp, "%%%%EndResource\n");
    fprintf(fp, "%%%%EndProlog\n");

    fprintf(fp, "%%%%BeginSetup\n");

    /*
     * Assign a destination name to each page for pdfmark purposes.
     */
    pageno = 0;
    for (page = doc->pages; page; page = page->next) {
	char *buf;
	pageno++;
	buf = snewn(12, char);
	sprintf(buf, "/p%d", pageno);
	page->spare = buf;
    }

    /*
     * This is as good a place as any to put version IDs.
     */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID)
	    ps_comment(fp, "% ", p->words);

    cc = 0;
    /*
     * Request the correct page size.  We might want to bracket this
     * with "%%BeginFeature: *PageSize A4" or similar, and "%%EndFeature",
     * but that would require us to have a way of getting the name of
     * the page size given its dimensions.
     */
    ps_token(fp, &cc, "/setpagedevice where {\n");
    ps_token(fp, &cc, "  pop 2 dict dup /PageSize [%g %g] put setpagedevice\n",
	     doc->paper_width / FUNITS_PER_PT,
	     doc->paper_height / FUNITS_PER_PT);
    ps_token(fp, &cc, "} if\n");

    ps_token(fp, &cc, "[/PageMode/UseOutlines/DOCVIEW m\n");
    noe = doc->n_outline_elements;
    for (oe = doc->outline_elements; noe; oe++, noe--) {
	char *title;
	int titlelen, count, i;

	title = pdf_outline_convert(oe->pdata->outline_title, &titlelen);
	if (oe->level == 0) {
	    ps_token(fp, &cc, "[/Title");
	    ps_string_len(fp, &cc, title, titlelen);
	    ps_token(fp, &cc, "/DOCINFO m\n");
	}

	count = 0;
	for (i = 1; i < noe && oe[i].level > oe->level; i++)
	    if (oe[i].level == oe->level + 1)
		count++;
	if (oe->level > 0) count = -count;

	ps_string_len(fp, &cc, title, titlelen);
	sfree(title);
	ps_token(fp, &cc, "%s %d o\n",
		(char *)oe->pdata->first->page->spare, count);
    }

    for (fe = doc->fonts->head; fe; fe = fe->next) {
	/* XXX This may request the same font multiple times. */
	if (fe->font->info->fontfile) {
	    fprintf(fp, "%%%%BeginResource: font %s\n", fe->font->info->name);
	    if (fe->font->info->filetype == TYPE1)
		pf_writeps(fe->font->info, fp);
	    else
		sfnt_writeps(fe->font->info, fp);
	    fprintf(fp, "%%%%EndResource\n");
	} else {
	    fprintf(fp, "%%%%IncludeResource: font %s\n",
		    fe->font->info->name);
	}
    }

    /*
     * Re-encode the fonts.
     */
    font_index = 0;
    for (fe = doc->fonts->head; fe; fe = fe->next) {
	char fname[40];
	int i;

	sprintf(fname, "f%d", font_index++);
	fe->name = dupstr(fname);

	ps_token(fp, &cc, "/%s findfont dup length dict begin\n",
	    fe->font->info->name);
	ps_token(fp, &cc, "{1 index /FID ne {def} {pop pop} ifelse} forall\n");
	ps_token(fp, &cc, "/Encoding [\n");
	for (i = 0; i < 256; i++)
	    ps_token(fp, &cc, "/%s", glyph_extern(fe->vector[i]));
	ps_token(fp, &cc, "] def\n");
	ps_token(fp, &cc, "currentdict end\n");
	ps_token(fp, &cc, "/fontname-%s exch definefont /%s exch def\n",
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
	xref *xr;
	font_encoding *fe;
	int fs;

	pageno++;
	fprintf(fp, "%%%%Page: %d %d\n", pageno, pageno);
	cc = 0;
	ps_token(fp, &cc, "save %s p\n", (char *)page->spare);
	
	for (xr = page->first_xref; xr; xr = xr->next) {
	    ps_token(fp, &cc, "[%g %g %g %g]",
		    xr->lx/FUNITS_PER_PT, xr->by/FUNITS_PER_PT,
		    xr->rx/FUNITS_PER_PT, xr->ty/FUNITS_PER_PT);
	    if (xr->dest.type == PAGE) {
		ps_token(fp, &cc, "%s x\n", (char *)xr->dest.page->spare);
	    } else {
		ps_string(fp, &cc, xr->dest.url);
		ps_token(fp, &cc, "u\n");
	    }
	}

	for (r = page->first_rect; r; r = r->next) {
	    ps_token(fp, &cc, "%g %g %g %g r\n",
		    r->x / FUNITS_PER_PT, r->y / FUNITS_PER_PT,
		    r->w / FUNITS_PER_PT, r->h / FUNITS_PER_PT);
	}

	frag = page->first_text;
	fe = NULL;
	fs = -1;
	while (frag) {
	    /*
	     * Collect all the adjacent text fragments with the
	     * same y-coordinate.
	     */
	    for (frag_end = frag;
		 frag_end && frag_end->y == frag->y;
		 frag_end = frag_end->next);

	    ps_token(fp, &cc, "%g[", frag->y / FUNITS_PER_PT);

	    while (frag && frag != frag_end) {

		if (frag->fe != fe || frag->fontsize != fs)
		    ps_token(fp, &cc, "[%s %d]",
			     frag->fe->name, frag->fontsize);
		fe = frag->fe;
		fs = frag->fontsize;

		ps_token(fp, &cc, "%g", frag->x/FUNITS_PER_PT);
		ps_string(fp, &cc, frag->text);

		frag = frag->next;
	    }

	    ps_token(fp, &cc, "]t\n");
	}

	ps_token(fp, &cc, "restore showpage\n");
    }

    fprintf(fp, "%%%%EOF\n");

    fclose(fp);

    sfree(filename);
}

static void ps_comment(FILE *fp, char const *leader, word *words) {
    int cc = 0;

    cc += fprintf(fp, "%s", leader);

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

	if (cc + strlen(text) > PS_MAXWIDTH)
	    text[PS_MAXWIDTH - cc] = 0;
	cc += fprintf(fp, "%s", text);
	sfree(text);
    }

    fprintf(fp, "\n");
}

void ps_token(FILE *fp, int *cc, char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    if (*cc >= PS_WIDTH - 10) {
	fprintf(fp, "\n");
	*cc = 0;
    }
    *cc += vfprintf(fp, fmt, ap);
    /* Assume that \n only occurs at the end of a string */
    if (fmt[strlen(fmt) - 1] == '\n')
	*cc = 0;
}

static void ps_string_len(FILE *fp, int *cc, char const *str, int len) {
    char const *c;
    int score = 0;

    for (c = str; c < str+len; c++) {
	if (*c < ' ' || *c > '~')
	    score += 2;
	else if (*c == '(' || *c == ')' || *c == '\\')
	    score += 0;
	else
	    score -= 1;
    }
    if (score > 0) {
	ps_token(fp, cc, "<");
	for (c = str; c < str+len; c++) {
	    ps_token(fp, cc, "%02X", 0xFF & (int)*c);
	}
	ps_token(fp, cc, ">");
    } else {
	*cc += fprintf(fp, "(");
	for (c = str; c < str+len; c++) {
	    if (*cc >= PS_WIDTH - 4) {
		fprintf(fp, "\\\n");
		*cc = 0;
	    }
	    if (*c < ' ' || *c > '~') {
		*cc += fprintf(fp, "\\%03o", 0xFF & (int)*c);
	    } else {
		if (*c == '(' || *c == ')' || *c == '\\') {
		    fputc('\\', fp);
		    (*cc)++;
		}
		fputc(*c, fp);
		(*cc)++;
	    }
	}
	*cc += fprintf(fp, ")");
    }
}

static void ps_string(FILE *fp, int *cc, char const *str) {
    ps_string_len(fp, cc, str, strlen(str));
}
