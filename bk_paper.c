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

/*
 * TODO in future work:
 * 
 *  - linearised PDF, perhaps?
 * 
 *  - I'm uncertain of whether I need to include a ToUnicode CMap
 *    in each of my font definitions in PDF. Currently things (by
 *    which I mean cut and paste out of acroread) seem to be
 *    working fairly happily without it, but I don't know.
 * 
 *  - rather than the ugly aux_text mechanism for rendering chapter
 *    titles, we could actually build the correct word list and
 *    wrap it as a whole.
 * 
 *  - get vertical font metrics and use them to position the PDF
 *    xref boxes more pleasantly
 * 
 *  - configurability
 *     * page header and footer should be configurable; we should
 * 	 be able to shift the page number elsewhere, and add other
 * 	 things such as the current chapter/section title and fixed
 * 	 text
 *     * remove the fixed mapping from heading levels to heading
 * 	 styles; offer a menu of styles from which the user can
 * 	 choose at every heading level
 *     * first-line indent in paragraphs
 *     * fixed text: `Contents', `Index', the colon-space and full
 * 	 stop in chapter title constructions
 *     * configurable location of contents?
 *     * certainly configurably _remove_ the contents, and possibly
 * 	 also the index
 *     * double-sided document switch?
 * 	  + means you have two header/footer formats which
 * 	    alternate
 * 	  + and means that mandatory page breaks before chapter
 * 	    titles should include a blank page if necessary to
 * 	    start the next section to a right-hand page
 * 
 *  - title pages
 * 
 *  - ability to use Type 1 fonts without AFM files
 *     * we need to parse the font to extract its metrics
 * 
 *  - character substitution for better typography?
 *     * use real ellipsis rather than ...
 *     * a hyphen in a word by itself might prefer to be an en-dash
 *     * (Americans might even want a convenient way to use an
 * 	 em-dash)
 *     * DON'T DO ANY OF THE ABOVE WITHIN \c OR \cw!
 *     * substituting `minus' for `hyphen' in the standard encoding
 * 	 is probably preferable in Courier, though certainly not in
 * 	 the main text font
 *     * if I do do this lot, I'm rather inclined to at least try
 * 	 to think up a configurable way to do it so that Americans
 * 	 can do em-dash tricks without my intervention and other
 * 	 people can do other odd things too.
 */

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "halibut.h"
#include "paper.h"

typedef struct paper_conf_Tag paper_conf;
typedef struct paper_idx_Tag paper_idx;

typedef struct {
    font_data *fonts[NFONTS];
    int font_size;
} font_cfg;

struct paper_conf_Tag {
    int paper_width;
    int paper_height;
    int left_margin;
    int top_margin;
    int right_margin;
    int bottom_margin;
    int indent_list_bullet;
    int indent_list_after;
    int indent_list;
    int indent_quote;
    int base_leading;
    int base_para_spacing;
    int chapter_top_space;
    int sect_num_left_space;
    int chapter_underline_depth;
    int chapter_underline_thickness;
    int rule_thickness;
    font_cfg fbase, fcode, ftitle, fchapter, *fsect;
    int nfsect;
    int contents_indent_step;
    int contents_margin;
    int leader_separation;
    int index_gutter;
    int index_cols;
    int index_minsep;
    int pagenum_fontsize;
    int footer_distance;
    wchar_t *lquote, *rquote, *bullet;
    wchar_t *contents_text, *index_text;
    /* These are derived from the above */
    int base_width;
    int page_height;
    int index_colwidth;
};

struct paper_idx_Tag {
    /*
     * Word list giving the page numbers on which this index entry
     * appears. Also the last word in the list, for ease of
     * construction.
     */
    word *words;
    word *lastword;
    /*
     * The last page added to the list (so we can ensure we don't
     * add one twice).
     */
    page_data *lastpage;
};

enum {
    word_PageXref = word_NotWordType + 1
};

/* Flags for render_string() */
#define RS_NOLIG	1

static font_data *make_std_font(font_list *fontlist, char const *name);
static void wrap_paragraph(para_data *pdata, word *words,
			   int w, int i1, int i2, paper_conf *conf);
static page_data *page_breaks(line_data *first, line_data *last,
			      int page_height, int ncols, int headspace);
static int render_string(page_data *page, font_data *font, int fontsize,
			 int x, int y, wchar_t *str, unsigned flags);
static int render_line(line_data *ldata, int left_x, int top_y,
		       xref_dest *dest, keywordlist *keywords, indexdata *idx,
		       paper_conf *conf);
static void render_para(para_data *pdata, paper_conf *conf,
			keywordlist *keywords, indexdata *idx,
			paragraph *index_placeholder, page_data *index_page);
static int string_width(font_data *font, wchar_t const *string, int *errs,
			unsigned flags);
static int paper_width_simple(para_data *pdata, word *text, paper_conf *conf);
static para_data *code_paragraph(int indent, word *words, paper_conf *conf);
static para_data *rule_paragraph(int indent, paper_conf *conf);
static void add_rect_to_page(page_data *page, int x, int y, int w, int h);
static para_data *make_para_data(int ptype, int paux, int indent, int rmargin,
				 word *pkwtext, word *pkwtext2, word *pwords,
				 paper_conf *conf);
static void standard_line_spacing(para_data *pdata, paper_conf *conf);
static wchar_t *prepare_outline_title(word *first, wchar_t *separator,
				      word *second);
static word *fake_word(wchar_t *text);
static word *fake_space_word(void);
static word *fake_page_ref(page_data *page);
static word *fake_end_ref(void);
static word *prepare_contents_title(word *first, wchar_t *separator,
				    word *second);
static void fold_into_page(page_data *dest, page_data *src, int right_shift);

static int fonts_ok(wchar_t *string, ...)
{
    font_data *font;
    va_list ap;
    int ret = TRUE;

    va_start(ap, string);
    while ( (font = va_arg(ap, font_data *)) != NULL) {
	int errs;
	(void) string_width(font, string, &errs, 0);
	if (errs) {
	    ret = FALSE;
	    break;
	}
    }
    va_end(ap);

    return ret;
}

static void paper_cfg_fonts(font_data **fonts, font_list *fontlist,
			    wchar_t *wp, filepos *fpos) {
    font_data *f;
    char *fn;
    int i;

    for (i = 0; i < NFONTS && *wp; i++, wp = uadv(wp)) {
	fn = utoa_dup(wp, CS_ASCII);
	f = make_std_font(fontlist, fn);
	if (f)
	    fonts[i] = f;
	else
	    /* FIXME: proper error */
	    error(err_nofont, fpos, wp);
    }
}

static paper_conf paper_configure(paragraph *source, font_list *fontlist) {
    paragraph *p;
    paper_conf ret;

    /*
     * Defaults.
     */
    ret.paper_width = 595 * UNITS_PER_PT;
    ret.paper_height = 842 * UNITS_PER_PT;
    ret.left_margin = 72 * UNITS_PER_PT;
    ret.top_margin = 72 * UNITS_PER_PT;
    ret.right_margin = 72 * UNITS_PER_PT;
    ret.bottom_margin = 108 * UNITS_PER_PT;
    ret.indent_list_bullet = 6 * UNITS_PER_PT;
    ret.indent_list_after = 18 * UNITS_PER_PT;
    ret.indent_quote = 18 * UNITS_PER_PT;
    ret.base_leading = UNITS_PER_PT;
    ret.base_para_spacing = 10 * UNITS_PER_PT;
    ret.chapter_top_space = 72 * UNITS_PER_PT;
    ret.sect_num_left_space = 12 * UNITS_PER_PT;
    ret.chapter_underline_depth = 14 * UNITS_PER_PT;
    ret.chapter_underline_thickness = 3 * UNITS_PER_PT;
    ret.rule_thickness = 1 * UNITS_PER_PT;
    ret.fbase.font_size = 12;
    ret.fbase.fonts[FONT_NORMAL] = make_std_font(fontlist, "Times-Roman");
    ret.fbase.fonts[FONT_EMPH] = make_std_font(fontlist, "Times-Italic");
    ret.fbase.fonts[FONT_CODE] = make_std_font(fontlist, "Courier");
    ret.fcode.font_size = 12;
    ret.fcode.fonts[FONT_NORMAL] = make_std_font(fontlist, "Courier-Bold");
    ret.fcode.fonts[FONT_EMPH] = make_std_font(fontlist, "Courier-Oblique");
    ret.fcode.fonts[FONT_CODE] = make_std_font(fontlist, "Courier");
    ret.ftitle.font_size = 24;
    ret.ftitle.fonts[FONT_NORMAL] = make_std_font(fontlist, "Helvetica-Bold");
    ret.ftitle.fonts[FONT_EMPH] =
	make_std_font(fontlist, "Helvetica-BoldOblique");
    ret.ftitle.fonts[FONT_CODE] = make_std_font(fontlist, "Courier-Bold");
    ret.fchapter.font_size = 20;
    ret.fchapter.fonts[FONT_NORMAL]= make_std_font(fontlist, "Helvetica-Bold");
    ret.fchapter.fonts[FONT_EMPH] =
	make_std_font(fontlist, "Helvetica-BoldOblique");
    ret.fchapter.fonts[FONT_CODE] = make_std_font(fontlist, "Courier-Bold");
    ret.nfsect = 3;
    ret.fsect = snewn(ret.nfsect, font_cfg);
    ret.fsect[0].font_size = 16;
    ret.fsect[0].fonts[FONT_NORMAL]= make_std_font(fontlist, "Helvetica-Bold");
    ret.fsect[0].fonts[FONT_EMPH] =
	make_std_font(fontlist, "Helvetica-BoldOblique");
    ret.fsect[0].fonts[FONT_CODE] = make_std_font(fontlist, "Courier-Bold");
    ret.fsect[1].font_size = 14;
    ret.fsect[1].fonts[FONT_NORMAL]= make_std_font(fontlist, "Helvetica-Bold");
    ret.fsect[1].fonts[FONT_EMPH] =
	make_std_font(fontlist, "Helvetica-BoldOblique");
    ret.fsect[1].fonts[FONT_CODE] = make_std_font(fontlist, "Courier-Bold");
    ret.fsect[2].font_size = 13;
    ret.fsect[2].fonts[FONT_NORMAL]= make_std_font(fontlist, "Helvetica-Bold");
    ret.fsect[2].fonts[FONT_EMPH] =
	make_std_font(fontlist, "Helvetica-BoldOblique");
    ret.fsect[2].fonts[FONT_CODE] = make_std_font(fontlist, "Courier-Bold");
    ret.contents_indent_step = 24 * UNITS_PER_PT;
    ret.contents_margin = 84 * UNITS_PER_PT;
    ret.leader_separation = 12 * UNITS_PER_PT;
    ret.index_gutter = 36 * UNITS_PER_PT;
    ret.index_cols = 2;
    ret.index_minsep = 18 * UNITS_PER_PT;
    ret.pagenum_fontsize = 12;
    ret.footer_distance = 32 * UNITS_PER_PT;
    ret.lquote = L"\x2018\0\x2019\0'\0'\0\0";
    ret.rquote = uadv(ret.lquote);
    ret.bullet = L"\x2022\0-\0\0";
    ret.contents_text = L"Contents";
    ret.index_text = L"Index";

    /*
     * Two-pass configuration so that we can pick up global config
     * (e.g. `quotes') before having it overridden by specific
     * config (`paper-quotes'), irrespective of the order in which
     * they occur.
     */
    for (p = source; p; p = p->next) {
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    }
	}
    }

    for (p = source; p; p = p->next) {
	p->private_data = NULL;
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"paper-quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    } else if (!ustricmp(p->keyword, L"contents")) {
		ret.contents_text = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"index")) {
		ret.index_text = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"paper-bullet")) {
		ret.bullet = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"paper-page-width")) {
		ret.paper_width =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-page-height")) {
		ret.paper_height =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-left-margin")) {
		ret.left_margin =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-top-margin")) {
		ret.top_margin =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-right-margin")) {
		ret.right_margin =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-bottom-margin")) {
		ret.bottom_margin =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-list-indent")) {
		ret.indent_list_bullet =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-listitem-indent")) {
		ret.indent_list =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-quote-indent")) {
		ret.indent_quote =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-base-leading")) {
		ret.base_leading =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-base-para-spacing")) {
		ret.base_para_spacing =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-chapter-top-space")) {
		ret.chapter_top_space =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-sect-num-left-space")) {
		ret.sect_num_left_space =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-chapter-underline-depth")) {
		ret.chapter_underline_depth =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-chapter-underline-thickness")) {
		ret.chapter_underline_thickness =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-rule-thickness")) {
		ret.rule_thickness =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-contents-indent-step")) {
		ret.contents_indent_step =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-contents-margin")) {
		ret.contents_margin =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-leader-separation")) {
		ret.leader_separation =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-index-gutter")) {
		ret.index_gutter =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-index-minsep")) {
		ret.index_minsep =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-footer-distance")) {
		ret.footer_distance =
		    (int) 0.5 + FUNITS_PER_PT * utof(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-base-font-size")) {
		ret.fbase.font_size = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-index-columns")) {
		ret.index_cols = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-pagenum-font-size")) {
		ret.pagenum_fontsize = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-base-fonts")) {
		paper_cfg_fonts(ret.fbase.fonts, fontlist, uadv(p->keyword),
				&p->fpos);
	    } else if (!ustricmp(p->keyword, L"paper-code-font-size")) {
		ret.fcode.font_size = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-code-fonts")) {
		paper_cfg_fonts(ret.fcode.fonts, fontlist, uadv(p->keyword),
				&p->fpos);
	    } else if (!ustricmp(p->keyword, L"paper-title-font-size")) {
		ret.ftitle.font_size = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-title-fonts")) {
		paper_cfg_fonts(ret.ftitle.fonts, fontlist, uadv(p->keyword),
				&p->fpos);
	    } else if (!ustricmp(p->keyword, L"paper-chapter-font-size")) {
		ret.fchapter.font_size = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"paper-chapter-fonts")) {
		paper_cfg_fonts(ret.fchapter.fonts, fontlist, uadv(p->keyword),
				&p->fpos);
	    } else if (!ustricmp(p->keyword, L"paper-section-font-size")) {
		wchar_t *q = uadv(p->keyword);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nfsect) {
		    int i;
		    ret.fsect = sresize(ret.fsect, n+1, font_cfg);
		    for (i = ret.nfsect; i <= n; i++)
			ret.fsect[i] = ret.fsect[ret.nfsect-1];
		    ret.nfsect = n+1;
		}
		ret.fsect[n].font_size = utoi(q);
	    } else if (!ustricmp(p->keyword, L"paper-section-fonts")) {
		wchar_t *q = uadv(p->keyword);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nfsect) {
		    int i;
		    ret.fsect = sresize(ret.fsect, n+1, font_cfg);
		    for (i = ret.nfsect; i <= n; i++)
			ret.fsect[i] = ret.fsect[ret.nfsect-1];
		    ret.nfsect = n+1;
		}
		paper_cfg_fonts(ret.fsect[n].fonts, fontlist, q, &p->fpos);
	    } 
	}
    }

    /*
     * Set up the derived fields in the conf structure.
     */

    ret.base_width =
	ret.paper_width - ret.left_margin - ret.right_margin;
    ret.page_height =
	ret.paper_height - ret.top_margin - ret.bottom_margin;
    ret.indent_list = ret.indent_list_bullet + ret.indent_list_after;
    ret.index_colwidth =
	(ret.base_width - (ret.index_cols-1) * ret.index_gutter)
	/ ret.index_cols;

    /*
     * Now process fallbacks on quote characters and bullets. We
     * use string_width() to determine whether all of the relevant
     * fonts contain the same character, and fall back whenever we
     * find a character which not all of them support.
     */

    /* Quote characters need not be supported in the fixed code fonts,
     * but must be in the title and body fonts. */
    while (*uadv(ret.rquote) && *uadv(uadv(ret.rquote))) {
	int n;
	if (fonts_ok(ret.lquote,
		     ret.fbase.fonts[FONT_NORMAL],
		     ret.fbase.fonts[FONT_EMPH],
		     ret.ftitle.fonts[FONT_NORMAL],
		     ret.ftitle.fonts[FONT_EMPH],
		     ret.fchapter.fonts[FONT_NORMAL],
		     ret.fchapter.fonts[FONT_EMPH], NULL) &&
	    fonts_ok(ret.rquote,
		     ret.fbase.fonts[FONT_NORMAL],
		     ret.fbase.fonts[FONT_EMPH],
		     ret.ftitle.fonts[FONT_NORMAL],
		     ret.ftitle.fonts[FONT_EMPH],
		     ret.fchapter.fonts[FONT_NORMAL],
		     ret.fchapter.fonts[FONT_EMPH], NULL)) {
	    for (n = 0; n < ret.nfsect; n++)
		if (!fonts_ok(ret.lquote,
			      ret.fsect[n].fonts[FONT_NORMAL],
			      ret.fsect[n].fonts[FONT_EMPH], NULL) ||
		    !fonts_ok(ret.rquote,
			      ret.fsect[n].fonts[FONT_NORMAL],
			      ret.fsect[n].fonts[FONT_EMPH], NULL))
		    break;
	    if (n == ret.nfsect)
		break;
	}
	ret.lquote = uadv(ret.rquote);
	ret.rquote = uadv(ret.lquote);
    }

    /* The bullet character only needs to be supported in the normal body
     * font (not even in italics). */
    while (*ret.bullet && *uadv(ret.bullet) &&
	   !fonts_ok(ret.bullet, ret.fbase.fonts[FONT_NORMAL], NULL))
	ret.bullet = uadv(ret.bullet);

    return ret;
}

void *paper_pre_backend(paragraph *sourceform, keywordlist *keywords,
			indexdata *idx) {
    paragraph *p;
    document *doc;
    int indent, used_contents;
    para_data *pdata, *firstpara = NULL, *lastpara = NULL;
    para_data *firstcont, *lastcont;
    line_data *firstline, *lastline, *firstcontline, *lastcontline;
    page_data *pages;
    font_list *fontlist;
    paper_conf *conf, ourconf;
    int has_index;
    int pagenum;
    paragraph index_placeholder_para;
    page_data *first_index_page;

    init_std_fonts();
    fontlist = snew(font_list);
    fontlist->head = fontlist->tail = NULL;

    ourconf = paper_configure(sourceform, fontlist);
    conf = &ourconf;

    /*
     * Set up a data structure to collect page numbers for each
     * index entry.
     */
    {
	int i;
	indexentry *entry;

	has_index = FALSE;

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    paper_idx *pi = snew(paper_idx);

	    has_index = TRUE;

	    pi->words = pi->lastword = NULL;
	    pi->lastpage = NULL;

	    entry->backend_data = pi;
	}
    }

    /*
     * Format the contents entry for each heading.
     */
    {
	word *contents_title;
	contents_title = fake_word(conf->contents_text);

	firstcont = make_para_data(para_UnnumberedChapter, 0, 0, 0,
				   NULL, NULL, contents_title, conf);
	lastcont = firstcont;
	lastcont->next = NULL;
	firstcontline = firstcont->first;
	lastcontline = lastcont->last;
	for (p = sourceform; p; p = p->next) {
	    word *words;
	    int indent;

	    switch (p->type) {
	      case para_Chapter:
	      case para_Appendix:
	      case para_UnnumberedChapter:
	      case para_Heading:
	      case para_Subsect:
		switch (p->type) {
		  case para_Chapter:
		  case para_Appendix:
		    words = prepare_contents_title(p->kwtext, L": ", p->words);
		    indent = 0;
		    break;
		  case para_UnnumberedChapter:
		    words = prepare_contents_title(NULL, NULL, p->words);
		    indent = 0;
		    break;
		  case para_Heading:
		  case para_Subsect:
		    words = prepare_contents_title(p->kwtext2, L" ", p->words);
		    indent = (p->aux + 1) * conf->contents_indent_step;
		    break;
		}
		pdata = make_para_data(para_Normal, p->aux, indent,
				       conf->contents_margin,
				       NULL, NULL, words, conf);
		pdata->next = NULL;
		pdata->contents_entry = p;
		lastcont->next = pdata;
		lastcont = pdata;

		/*
		 * Link all contents line structures together into
		 * a big list.
		 */
		if (pdata->first) {
		    if (lastcontline) {
			lastcontline->next = pdata->first;
			pdata->first->prev = lastcontline;
		    } else {
			firstcontline = pdata->first;
			pdata->first->prev = NULL;
		    }
		    lastcontline = pdata->last;
		    lastcontline->next = NULL;
		}

		break;
	    }
	}

	/*
	 * And one extra one, for the index.
	 */
	if (has_index) {
	    pdata = make_para_data(para_Normal, 0, 0,
				   conf->contents_margin,
				   NULL, NULL,
				   fake_word(conf->index_text), conf);
	    pdata->next = NULL;
	    pdata->contents_entry = &index_placeholder_para;
	    lastcont->next = pdata;
	    lastcont = pdata;

	    if (pdata->first) {
		if (lastcontline) {
		    lastcontline->next = pdata->first;
		    pdata->first->prev = lastcontline;
		} else {
		    firstcontline = pdata->first;
		    pdata->first->prev = NULL;
		}
		lastcontline = pdata->last;
		lastcontline->next = NULL;
	    }
	}
    }

    /*
     * Do the main paragraph formatting.
     */
    indent = 0;
    used_contents = FALSE;
    firstline = lastline = NULL;
    for (p = sourceform; p; p = p->next) {
	p->private_data = NULL;

	switch (p->type) {
	    /*
	     * These paragraph types are either invisible or don't
	     * define text in the normal sense. Either way, they
	     * don't require wrapping.
	     */
	  case para_IM:
	  case para_BR:
	  case para_Biblio:
	  case para_NotParaType:
	  case para_Config:
	  case para_VersionID:
	  case para_NoCite:
	    break;

	    /*
	     * These paragraph types don't require wrapping, but
	     * they do affect the line width to which we wrap the
	     * rest of the paragraphs, so we need to pay attention.
	     */
	  case para_LcontPush:
	    indent += conf->indent_list; break;
	  case para_LcontPop:
	    indent -= conf->indent_list; assert(indent >= 0); break;
	  case para_QuotePush:
	    indent += conf->indent_quote; break;
	  case para_QuotePop:
	    indent -= conf->indent_quote; assert(indent >= 0); break;

	    /*
	     * This paragraph type is special. Process it
	     * specially.
	     */
	  case para_Code:
	    pdata = code_paragraph(indent, p->words, conf);
	    p->private_data = pdata;
	    if (pdata->first != pdata->last) {
		pdata->first->penalty_after += 100000;
		pdata->last->penalty_before += 100000;
	    }
	    break;

	    /*
	     * This paragraph is also special.
	     */
	  case para_Rule:
	    pdata = rule_paragraph(indent, conf);
	    p->private_data = pdata;
	    break;

	    /*
	     * All of these paragraph types require wrapping in the
	     * ordinary way. So we must supply a set of fonts, a
	     * line width and auxiliary information (e.g. bullet
	     * text) for each one.
	     */
	  case para_Chapter:
	  case para_Appendix:
	  case para_UnnumberedChapter:
	  case para_Heading:
	  case para_Subsect:
	  case para_Normal:
	  case para_BiblioCited:
	  case para_Bullet:
	  case para_NumberedList:
	  case para_DescribedThing:
	  case para_Description:
	  case para_Copyright:
	  case para_Title:
	    pdata = make_para_data(p->type, p->aux, indent, 0,
				   p->kwtext, p->kwtext2, p->words, conf);

	    p->private_data = pdata;

	    break;
	}

	if (p->private_data) {
	    pdata = (para_data *)p->private_data;

	    /*
	     * If this is the first non-title heading, we link the
	     * contents section in before it.
	     */
	    if (!used_contents && pdata->outline_level > 0) {
		used_contents = TRUE;
		if (lastpara)
		    lastpara->next = firstcont;
		else
		    firstpara = firstcont;
		lastpara = lastcont;
		assert(lastpara->next == NULL);

		if (lastline) {
		    lastline->next = firstcontline;
		    firstcontline->prev = lastline;
		} else {
		    firstline = firstcontline;
		    firstcontline->prev = NULL;
		}
		assert(lastcontline != NULL);
		lastline = lastcontline;
		lastline->next = NULL;
	    }

	    /*
	     * Link all line structures together into a big list.
	     */
	    if (pdata->first) {
		if (lastline) {
		    lastline->next = pdata->first;
		    pdata->first->prev = lastline;
		} else {
		    firstline = pdata->first;
		    pdata->first->prev = NULL;
		}
		lastline = pdata->last;
		lastline->next = NULL;
	    }

	    /*
	     * Link all paragraph structures together similarly.
	     */
	    pdata->next = NULL;
	    if (lastpara)
		lastpara->next = pdata;
	    else
		firstpara = pdata;
	    lastpara = pdata;
	}
    }

    /*
     * Now we have an enormous linked list of every line of text in
     * the document. Break it up into pages.
     */
    pages = page_breaks(firstline, lastline, conf->page_height, 0, 0);

    /*
     * Number the pages.
     */
    {
	char buf[40];
	page_data *page;

	pagenum = 0;

	for (page = pages; page; page = page->next) {
	    sprintf(buf, "%d", ++pagenum);
	    page->number = ufroma_dup(buf, CS_ASCII);
	}

	if (has_index) {
	    first_index_page = snew(page_data);
	    first_index_page->next = first_index_page->prev = NULL;
	    first_index_page->first_line = NULL;
	    first_index_page->last_line = NULL;
	    first_index_page->first_text = first_index_page->last_text = NULL;
	    first_index_page->first_xref = first_index_page->last_xref = NULL;
	    first_index_page->first_rect = first_index_page->last_rect = NULL;

	    /* And don't forget the as-yet-uncreated index. */
	    sprintf(buf, "%d", ++pagenum);
	    first_index_page->number = ufroma_dup(buf, CS_ASCII);
	}
    }

    /*
     * Now we're ready to actually lay out the pages. We do this by
     * looping over _paragraphs_, since we may need to track cross-
     * references between lines and even across pages.
     */
    for (pdata = firstpara; pdata; pdata = pdata->next)
	render_para(pdata, conf, keywords, idx,
		    &index_placeholder_para, first_index_page);

    /*
     * Now we've laid out the main body pages, we should have
     * acquired a full set of page numbers for the index.
     */
    if (has_index) {
	int i;
	indexentry *entry;
	word *index_title;
	para_data *firstidx, *lastidx;
	line_data *firstidxline, *lastidxline, *ldata;
	page_data *ipages, *ipages2, *page;

	/*
	 * Create a set of paragraphs for the index.
	 */
	index_title = fake_word(conf->index_text);

	firstidx = make_para_data(para_UnnumberedChapter, 0, 0, 0,
				  NULL, NULL, index_title, conf);
	lastidx = firstidx;
	lastidx->next = NULL;
	firstidxline = firstidx->first;
	lastidxline = lastidx->last;
	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    paper_idx *pi = (paper_idx *)entry->backend_data;
	    para_data *text, *pages;

	    if (!pi->words)
		continue;

	    text = make_para_data(para_Normal, 0, 0,
				  conf->base_width - conf->index_colwidth,
				  NULL, NULL, entry->text, conf);

	    pages  = make_para_data(para_Normal, 0, 0,
				    conf->base_width - conf->index_colwidth,
				    NULL, NULL, pi->words, conf);

	    text->justification = LEFT;
	    pages->justification = RIGHT;
	    text->last->space_after = pages->first->space_before =
		conf->base_leading / 2;

	    pages->last->space_after = text->first->space_before =
		conf->base_leading;

	    assert(text->first);
	    assert(pages->first);
	    assert(lastidxline);
	    assert(lastidx);

	    /*
	     * If feasible, fold the two halves of the index entry
	     * together.
	     */
	    if (text->last->real_shortfall + pages->first->real_shortfall >
		conf->index_colwidth + conf->index_minsep) {
		text->last->space_after = -1;
		pages->first->space_before = -pages->first->line_height+1;
	    }

	    lastidx->next = text;
	    text->next = pages;
	    pages->next = NULL;
	    lastidx = pages;

	    /*
	     * Link all index line structures together into
	     * a big list.
	     */
	    text->last->next = pages->first;
	    pages->first->prev = text->last;

	    lastidxline->next = text->first;
	    text->first->prev = lastidxline;

	    lastidxline = pages->last;

	    /*
	     * Breaking an index entry anywhere is so bad that I
	     * think I'm going to forbid it totally.
	     */
	    for (ldata = text->first; ldata && ldata->next;
		 ldata = ldata->next) {
		ldata->next->space_before += ldata->space_after + 1;
		ldata->space_after = -1;
	    }
	}

	/*
	 * Now break the index into pages.
	 */
	ipages = page_breaks(firstidxline, firstidxline, conf->page_height,
			      0, 0);
	ipages2 = page_breaks(firstidxline->next, lastidxline,
			     conf->page_height,
			     conf->index_cols,
			     firstidxline->space_before +
			     firstidxline->line_height +
			     firstidxline->space_after);

	/*
	 * This will have put each _column_ of the index on a
	 * separate page, which isn't what we want. Fold the pages
	 * back together.
	 */
	page = ipages2;
	while (page) {
	    int i;

	    for (i = 1; i < conf->index_cols; i++)
		if (page->next) {
		    page_data *tpage;

		    fold_into_page(page, page->next,
				   i * (conf->index_colwidth +
					conf->index_gutter));
		    tpage = page->next;
		    page->next = page->next->next;
		    if (page->next)
			page->next->prev = page;
		    sfree(tpage);
		}

	    page = page->next;
	}
	/* Also fold the heading on to the same page as the index items. */
	fold_into_page(ipages, ipages2, 0);
	ipages->next = ipages2->next;
	if (ipages->next)
	    ipages->next->prev = ipages;
	sfree(ipages2);
	fold_into_page(first_index_page, ipages, 0);
	first_index_page->next = ipages->next;
	if (first_index_page->next)
	    first_index_page->next->prev = first_index_page;
	sfree(ipages);
	ipages = first_index_page;

	/*
	 * Number the index pages, except the already-numbered
	 * first one.
	 */
	for (page = ipages->next; page; page = page->next) {
	    char buf[40];
	    sprintf(buf, "%d", ++pagenum);
	    page->number = ufroma_dup(buf, CS_ASCII);
	}

	/*
	 * Render the index pages.
	 */
	for (pdata = firstidx; pdata; pdata = pdata->next)
	    render_para(pdata, conf, keywords, idx,
			&index_placeholder_para, first_index_page);

	/*
	 * Link the index page list on to the end of the main page
	 * list.
	 */
	if (!pages)
	    pages = ipages;
	else {
	    for (page = pages; page->next; page = page->next);
	    page->next = ipages;
	}

	/*
	 * Same with the paragraph list, which will cause the index
	 * to be mentioned in the document outline.
	 */
	if (!firstpara)
	    firstpara = firstidx;
	else
	    lastpara->next = firstidx;
	lastpara = lastidx;
    }

    /*
     * Draw the headers and footers.
     * 
     * FIXME: this should be fully configurable, but for the moment
     * I'm just going to put in page numbers in the centre of a
     * footer and leave it at that.
     */
    {
	page_data *page;

	for (page = pages; page; page = page->next) {
	    int width;

	    width = conf->pagenum_fontsize *
		string_width(conf->fbase.fonts[FONT_NORMAL], page->number,
			     NULL, 0);

	    render_string(page, conf->fbase.fonts[FONT_NORMAL],
			  conf->pagenum_fontsize,
			  conf->left_margin + (conf->base_width - width)/2,
			  conf->bottom_margin - conf->footer_distance,
			  page->number, 0);
	}
    }

    /*
     * Start putting together the overall document structure we're
     * going to return.
     */
    doc = snew(document);
    doc->fonts = fontlist;
    doc->pages = pages;
    doc->paper_width = conf->paper_width;
    doc->paper_height = conf->paper_height;

    /*
     * Collect the section heading paragraphs into a document
     * outline. This is slightly fiddly because the Title paragraph
     * isn't required to be at the start, although all the others
     * must be in order.
     */
    {
	int osize = 20;

	doc->outline_elements = snewn(osize, outline_element);
	doc->n_outline_elements = 0;

	/* First find the title. */
	for (pdata = firstpara; pdata; pdata = pdata->next) {
	    if (pdata->outline_level == 0) {
		doc->outline_elements[0].level = 0;
		doc->outline_elements[0].pdata = pdata;
		doc->n_outline_elements++;
		break;
	    }
	}

	/* Then collect the rest. */
	for (pdata = firstpara; pdata; pdata = pdata->next) {
	    if (pdata->outline_level > 0) {
		if (doc->n_outline_elements >= osize) {
		    osize += 20;
		    doc->outline_elements =
			sresize(doc->outline_elements, osize, outline_element);
		}

		doc->outline_elements[doc->n_outline_elements].level =
		    pdata->outline_level;
		doc->outline_elements[doc->n_outline_elements].pdata = pdata;
		doc->n_outline_elements++;
	    }
	}
    }

    return doc;
}

static void setfont(para_data *p, font_cfg *f) {
    int i;

    for (i = 0; i < NFONTS; i++) {
	p->fonts[i] = f->fonts[i];
	p->sizes[i] = f->font_size;
    }
}

static para_data *make_para_data(int ptype, int paux, int indent, int rmargin,
				 word *pkwtext, word *pkwtext2, word *pwords,
				 paper_conf *conf)
{
    para_data *pdata;
    line_data *ldata;
    int extra_indent, firstline_indent, aux_indent;
    word *aux, *aux2;

    pdata = snew(para_data);
    pdata->outline_level = -1;
    pdata->outline_title = NULL;
    pdata->rect_type = RECT_NONE;
    pdata->contents_entry = NULL;
    pdata->justification = JUST;
    pdata->extraflags = 0;

    /*
     * Choose fonts for this paragraph.
     */
    switch (ptype) {
      case para_Title:
	setfont(pdata, &conf->ftitle);
	pdata->outline_level = 0;
	break;

      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
	setfont(pdata, &conf->fchapter);
	pdata->outline_level = 1;
	break;

      case para_Heading:
      case para_Subsect:
	setfont(pdata,
		&conf->fsect[paux >= conf->nfsect ? conf->nfsect - 1 : paux]);
	pdata->outline_level = 2 + paux;
	break;

      case para_Normal:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
      case para_DescribedThing:
      case para_Description:
      case para_Copyright:
	setfont(pdata, &conf->fbase);
	break;
    }

    /*
     * Also select an indentation level depending on the
     * paragraph type (list paragraphs other than
     * para_DescribedThing need extra indent).
     *
     * (FIXME: Perhaps at some point we might even arrange
     * for the user to be able to request indented first
     * lines in paragraphs.)
     */
    if (ptype == para_Bullet ||
	ptype == para_NumberedList ||
	ptype == para_Description) {
	extra_indent = firstline_indent = conf->indent_list;
    } else {
	extra_indent = firstline_indent = 0;
    }

    /*
     * Find the auxiliary text for this paragraph.
     */
    aux = aux2 = NULL;
    aux_indent = 0;

    switch (ptype) {
      case para_Chapter:
      case para_Appendix:
      case para_Heading:
      case para_Subsect:
	/*
	 * For some heading styles (FIXME: be able to
	 * configure which), the auxiliary text contains
	 * the chapter number and is arranged to be
	 * right-aligned a few points left of the primary
	 * margin. For other styles, the auxiliary text is
	 * the full chapter _name_ and takes up space
	 * within the (wrapped) chapter title, meaning that
	 * we must move the first line indent over to make
	 * space for it.
	 */
	if (ptype == para_Heading || ptype == para_Subsect) {
	    int len;

	    aux = pkwtext2;
	    len = paper_width_simple(pdata, pkwtext2, conf);
	    aux_indent = -len - conf->sect_num_left_space;

	    pdata->outline_title = 
		prepare_outline_title(pkwtext2, L" ", pwords);
	} else {
	    aux = pkwtext;
	    aux2 = fake_word(L": ");
	    aux_indent = 0;

	    firstline_indent += paper_width_simple(pdata, aux, conf);
	    firstline_indent += paper_width_simple(pdata, aux2, conf);

	    pdata->outline_title = 
		prepare_outline_title(pkwtext, L": ", pwords);
	}
	break;

      case para_Bullet:
	/*
	 * Auxiliary text consisting of a bullet.
	 */
	aux = fake_word(conf->bullet);
	aux_indent = indent + conf->indent_list_bullet;
	break;

      case para_NumberedList:
	/*
	 * Auxiliary text consisting of the number followed
	 * by a (FIXME: configurable) full stop.
	 */
	aux = pkwtext;
	aux2 = fake_word(L".");
	aux_indent = indent + conf->indent_list_bullet;
	break;

      case para_BiblioCited:
	/*
	 * Auxiliary text consisting of the bibliography
	 * reference text, and a trailing space.
	 */
	aux = pkwtext;
	aux2 = fake_word(L" ");
	aux_indent = indent;
	firstline_indent += paper_width_simple(pdata, aux, conf);
	firstline_indent += paper_width_simple(pdata, aux2, conf);
	break;
    }

    if (pdata->outline_level >= 0 && !pdata->outline_title) {
	pdata->outline_title = 
	    prepare_outline_title(NULL, NULL, pwords);
    }

    wrap_paragraph(pdata, pwords, conf->base_width - rmargin,
		   indent + firstline_indent,
		   indent + extra_indent, conf);

    pdata->first->aux_text = aux;
    pdata->first->aux_text_2 = aux2;
    pdata->first->aux_left_indent = aux_indent;

    /*
     * Line breaking penalties.
     */
    switch (ptype) {
      case para_Chapter:
      case para_Appendix:
      case para_Heading:
      case para_Subsect:
      case para_UnnumberedChapter:
	/*
	 * Fixed and large penalty for breaking straight
	 * after a heading; corresponding bonus for
	 * breaking straight before.
	 */
	pdata->first->penalty_before = -500000;
	pdata->last->penalty_after = 500000;
	for (ldata = pdata->first; ldata; ldata = ldata->next)
	    ldata->penalty_after = 500000;
	break;

      case para_DescribedThing:
	/*
	 * This is treated a bit like a small heading:
	 * there's a penalty for breaking after it (i.e.
	 * between it and its description), and a bonus for
	 * breaking before it (actually _between_ list
	 * items).
	 */
	pdata->first->penalty_before = -200000;
	pdata->last->penalty_after = 200000;
	break;

      default:
	/*
	 * Most paragraph types: widow/orphan control by
	 * discouraging breaking one line from the end of
	 * any paragraph.
	 */
	if (pdata->first != pdata->last) {
	    pdata->first->penalty_after = 100000;
	    pdata->last->penalty_before = 100000;
	}
	break;
    }

    standard_line_spacing(pdata, conf);

    /*
     * Some kinds of section heading require a page break before
     * them and an underline after.
     */
    if (ptype == para_Title ||
	ptype == para_Chapter ||
	ptype == para_Appendix ||
	ptype == para_UnnumberedChapter) {
	pdata->first->page_break = TRUE;
	pdata->first->space_before = conf->chapter_top_space;
	pdata->last->space_after +=
	    (conf->chapter_underline_depth +
	     conf->chapter_underline_thickness);
	pdata->rect_type = RECT_CHAPTER_UNDERLINE;
    }

    return pdata;
}

static void standard_line_spacing(para_data *pdata, paper_conf *conf)
{
    line_data *ldata;

    /*
     * Set the line spacing for each line in this paragraph.
     */
    for (ldata = pdata->first; ldata; ldata = ldata->next) {
	if (ldata == pdata->first)
	    ldata->space_before = conf->base_para_spacing / 2;
	else
	    ldata->space_before = conf->base_leading / 2;
	if (ldata == pdata->last)
	    ldata->space_after = conf->base_para_spacing / 2;
	else
	    ldata->space_after = conf->base_leading / 2;
	ldata->page_break = FALSE;
    }
}

static font_encoding *new_font_encoding(font_data *font)
{
    font_encoding *fe;
    int i;

    fe = snew(font_encoding);
    fe->next = NULL;

    if (font->list->tail)
	font->list->tail->next = fe;
    else
	font->list->head = fe;
    font->list->tail = fe;

    fe->font = font;
    fe->free_pos = 0x21;

    for (i = 0; i < 256; i++) {
	fe->vector[i] = NOGLYPH;
	fe->to_unicode[i] = 0xFFFF;
    }

    return fe;
}

static subfont_map_entry *encode_glyph_at(glyph g, wchar_t u,
					  font_encoding *fe, int pos)
{
    subfont_map_entry *sme = snew(subfont_map_entry);

    sme->subfont = fe;
    sme->position = pos;
    fe->vector[pos] = g;
    fe->to_unicode[pos] = u;
    add234(fe->font->subfont_map, sme);
    return sme;
}

static int new_sfmap_cmp(void *a, void *b)
{
    glyph ga = *(glyph *)a;
    subfont_map_entry *sb = b;
    glyph gb = sb->subfont->vector[sb->position];

    if (ga < gb) return -1;
    if (ga > gb) return 1;
    return 0;
}

static subfont_map_entry *encode_glyph(glyph g, wchar_t u, font_data *font)
{
    subfont_map_entry *sme;
    int c;

    sme = find234(font->subfont_map, &g, new_sfmap_cmp);
    if (sme) return sme;

    /*
     * This character is not yet in a subfont. Assign one.
     */
    if (font->latest_subfont->free_pos >= 0x100)
	font->latest_subfont = new_font_encoding(font);

    c = font->latest_subfont->free_pos++;
    if (font->latest_subfont->free_pos == 0x7F)
	font->latest_subfont->free_pos = 0xA1;

    return encode_glyph_at(g, u, font->latest_subfont, c);
}

static int sfmap_cmp(void *a, void *b)
{
    subfont_map_entry *sa = a, *sb = b;
    glyph ga = sa->subfont->vector[sa->position];
    glyph gb = sb->subfont->vector[sb->position];

    if (ga < gb) return -1;
    if (ga > gb) return 1;
    return 0;
}

int width_cmp(void *a, void *b)
{
    glyph_width const *wa = a, *wb = b;

    if (wa->glyph < wb->glyph)
	return -1;
    if (wa->glyph > wb->glyph)
	return 1;
    return 0;
}

int kern_cmp(void *a, void *b)
{
    kern_pair const *ka = a, *kb = b;

    if (ka->left < kb->left)
	return -1;
    if (ka->left > kb->left)
	return 1;
    if (ka->right < kb->right)
	return -1;
    if (ka->right > kb->right)
	return 1;
    return 0;
}

int lig_cmp(void *a, void *b)
{
    ligature const *la = a, *lb = b;

    if (la->left < lb->left)
	return -1;
    if (la->left > lb->left)
	return 1;
    if (la->right < lb->right)
	return -1;
    if (la->right > lb->right)
	return 1;
    return 0;
}

static int utoglyph(font_info const *fi, wchar_t u) {
    return (u < 0 || u > 0xFFFF ? NOGLYPH : fi->bmp[u]);
}

void listfonts(void) {
    font_info const *fi;

    init_std_fonts();
    for (fi = all_fonts; fi; fi = fi->next)
	printf("%s\n", fi->name);
}

static font_data *make_std_font(font_list *fontlist, char const *name)
{
    font_info const *fi;
    font_data *f;
    font_encoding *fe;
    int i;

    for (fe = fontlist->head; fe; fe = fe->next)
	if (strcmp(fe->font->info->name, name) == 0)
	    return fe->font;

    for (fi = all_fonts; fi; fi = fi->next)
	if (strcmp(fi->name, name) == 0) break;
    if (!fi) return NULL;

    f = snew(font_data);

    f->list = fontlist;
    f->info = fi;
    f->subfont_map = newtree234(sfmap_cmp);

    /*
     * Our first subfont will contain all of US-ASCII. This isn't
     * really necessary - we could just create custom subfonts
     * precisely as the whim of render_string dictated - but
     * instinct suggests that it might be nice to have the text in
     * the output files look _marginally_ recognisable.
     */
    fe = new_font_encoding(f);
    fe->free_pos = 0xA1;	       /* only the top half is free */
    f->latest_subfont = fe;

    for (i = 0x20; i <= 0x7E; i++) {
	glyph g = utoglyph(fi, i);
	if (g != NOGLYPH)
	    encode_glyph_at(g, i, fe, i);
    }

    return f;
}

/* NB: arguments are glyph numbers from font->bmp. */
int find_width(font_data *font, glyph index)
{
    glyph_width wantw;
    glyph_width const *w;

    wantw.glyph = index;
    w = find234(font->info->widths, &wantw, NULL);
    if (!w) return 0;
    return w->width;
}

static int find_kern(font_data *font, int lindex, int rindex)
{
    kern_pair wantkp;
    kern_pair const *kp;

    if (lindex == NOGLYPH || rindex == NOGLYPH)
	return 0;
    wantkp.left = lindex;
    wantkp.right = rindex;
    kp = find234(font->info->kerns, &wantkp, NULL);
    if (kp == NULL)
	return 0;
    return kp->kern;
}

static int find_lig(font_data *font, int lindex, int rindex)
{
    ligature wantlig;
    ligature const *lig;

    if (lindex == NOGLYPH || rindex == NOGLYPH)
	return NOGLYPH;
    wantlig.left = lindex;
    wantlig.right = rindex;
    lig = find234(font->info->ligs, &wantlig, NULL);
    if (lig == NULL)
	return NOGLYPH;
    return lig->lig;
}

static int string_width(font_data *font, wchar_t const *string, int *errs,
			unsigned flags)
{
    int width = 0;
    int nindex, index, oindex, lindex;

    if (errs)
	*errs = 0;

    oindex = NOGLYPH;
    index = utoglyph(font->info, *string);
    for (; *string; string++) {
	nindex = utoglyph(font->info, string[1]);

	if (index == NOGLYPH) {
	    if (errs)
		*errs = 1;
	} else {
	    if (!(flags & RS_NOLIG) &&
		(lindex = find_lig(font, index, nindex)) != NOGLYPH) {
		index = lindex;
		continue;
	    }
	    width += find_kern(font, oindex, index) + find_width(font, index);
	}
	oindex = index;
	index = nindex;
    }

    return width;
}

static int paper_width_internal(void *vctx, word *word, int *nspaces);

struct paper_width_ctx {
    int minspacewidth;
    para_data *pdata;
    paper_conf *conf;
};

static int paper_width_list(void *vctx, word *text, word *end, int *nspaces) {
    int w = 0;
    while (text && text != end) {
	w += paper_width_internal(vctx, text, nspaces);
	text = text->next;
    }
    return w;
}

static int paper_width_internal(void *vctx, word *word, int *nspaces)
{
    struct paper_width_ctx *ctx = (struct paper_width_ctx *)vctx;
    int style, type, findex, width, errs;
    wchar_t *str;
    unsigned flags = 0;

    switch (word->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_UpperXref:
      case word_LowerXref:
      case word_PageXref:
      case word_XrefEnd:
      case word_IndexRef:
	return 0;
    }

    style = towordstyle(word->type);
    type = removeattr(word->type);

    findex = (style == word_Normal ? FONT_NORMAL :
	      style == word_Emph ? FONT_EMPH :
	      FONT_CODE);

    if (style == word_Code || style == word_WeakCode) flags |= RS_NOLIG;

    if (type == word_Normal) {
	str = word->text;
    } else if (type == word_WhiteSpace) {
	if (findex != FONT_CODE) {
	    if (nspaces)
		(*nspaces)++;
	    return ctx->minspacewidth;
	} else
	    str = L" ";
    } else /* if (type == word_Quote) */ {
	if (word->aux == quote_Open)
	    str = ctx->conf->lquote;
	else
	    str = ctx->conf->rquote;
    }

    width = string_width(ctx->pdata->fonts[findex], str, &errs, flags);

    if (errs && word->alt)
	return paper_width_list(vctx, word->alt, NULL, nspaces);
    else
	return ctx->pdata->sizes[findex] * width;
}

static int paper_width(void *vctx, word *word)
{
    return paper_width_internal(vctx, word, NULL);
}

static int paper_width_simple(para_data *pdata, word *text, paper_conf *conf)
{
    struct paper_width_ctx ctx;

    ctx.pdata = pdata;
    ctx.minspacewidth =
	(pdata->sizes[FONT_NORMAL] *
	 string_width(pdata->fonts[FONT_NORMAL], L" ", NULL, 0));
    ctx.conf = conf;

    return paper_width_list(&ctx, text, NULL, NULL);
}

static void wrap_paragraph(para_data *pdata, word *words,
			   int w, int i1, int i2, paper_conf *conf)
{
    wrappedline *wrapping, *p;
    int spacewidth;
    struct paper_width_ctx ctx;
    int line_height;

    /*
     * We're going to need to store the line height in every line
     * structure we generate.
     */
    {
	int i;
	line_height = 0;
	for (i = 0; i < NFONTS; i++)
	    if (line_height < pdata->sizes[i])
		line_height = pdata->sizes[i];
	line_height *= UNITS_PER_PT;
    }

    spacewidth = (pdata->sizes[FONT_NORMAL] *
		  string_width(pdata->fonts[FONT_NORMAL], L" ", NULL, 0));
    if (spacewidth == 0) {
	/*
	 * A font without a space?! Disturbing. I hope this never
	 * comes up, but I'll make a random guess anyway and set my
	 * space width to half the point size.
	 */
	spacewidth = pdata->sizes[FONT_NORMAL] * UNITS_PER_PT / 2;
    }

    /*
     * I'm going to set the _minimum_ space width to 3/5 of the
     * standard one, and use the standard one as the optimum.
     */
    ctx.minspacewidth = spacewidth * 3 / 5;
    ctx.pdata = pdata;
    ctx.conf = conf;

    wrapping = wrap_para(words, w - i1, w - i2, paper_width, &ctx, spacewidth);

    /*
     * Having done the wrapping, we now concoct a set of line_data
     * structures.
     */
    pdata->first = pdata->last = NULL;

    for (p = wrapping; p; p = p->next) {
	line_data *ldata;
	word *wd;
	int len, wid, spaces;

	ldata = snew(line_data);

	ldata->pdata = pdata;
	ldata->first = p->begin;
	ldata->end = p->end;
	ldata->line_height = line_height;

	ldata->xpos = (p == wrapping ? i1 : i2);

	if (pdata->last) {
	    pdata->last->next = ldata;
	    ldata->prev = pdata->last;
	} else {
	    pdata->first = ldata;
	    ldata->prev = NULL;
	}
	ldata->next = NULL;
	pdata->last = ldata;

	spaces = 0;
	len = paper_width_list(&ctx, ldata->first, ldata->end, &spaces);
	wid = (p == wrapping ? w - i1 : w - i2);
	wd = ldata->first;

	ldata->hshortfall = wid - len;
	ldata->nspaces = spaces;
	/*
	 * This tells us how much the space width needs to
	 * change from _min_spacewidth. But we want to store
	 * its difference from the _natural_ space width, to
	 * make the text rendering easier.
	 */
	ldata->hshortfall += ctx.minspacewidth * spaces;
	ldata->hshortfall -= spacewidth * spaces;
	ldata->real_shortfall = ldata->hshortfall;
	/*
	 * Special case: on the last line of a paragraph, we
	 * never stretch spaces.
	 */
	if (ldata->hshortfall > 0 && !p->next)
	    ldata->hshortfall = 0;

	ldata->aux_text = NULL;
	ldata->aux_text_2 = NULL;
	ldata->aux_left_indent = 0;
	ldata->penalty_before = ldata->penalty_after = 0;
    }

}

static page_data *page_breaks(line_data *first, line_data *last,
			      int page_height, int ncols, int headspace)
{
    line_data *l, *m;
    page_data *ph, *pt;
    int n, n1, this_height;

    /*
     * Page breaking is done by a close analogue of the optimal
     * paragraph wrapping algorithm used by wrap_para(). We work
     * backwards from the end of the document line by line; for
     * each line, we contemplate every possible number of lines we
     * could put on a page starting with that line, determine a
     * cost function for each one, add it to the pre-computed cost
     * function for optimally page-breaking everything after that
     * page, and pick the best option.
     * 
     * This is made slightly more complex by the fact that we have
     * a multi-column index with a heading at the top of the
     * _first_ page, meaning that the first _ncols_ pages must have
     * a different length. Hence, we must do the wrapping ncols+1
     * times over, hypothetically trying to put every subsequence
     * on every possible page.
     * 
     * Since my line_data structures are only used for this
     * purpose, I might as well just store the algorithm data
     * directly in them.
     */

    for (l = last; l; l = l->prev) {
	l->bestcost = snewn(ncols+1, int);
	l->vshortfall = snewn(ncols+1, int);
	l->text = snewn(ncols+1, int);
	l->space = snewn(ncols+1, int);
	l->page_last = snewn(ncols+1, line_data *);

	for (n = 0; n <= ncols; n++) {
	    int minheight, text = 0, space = 0;
	    int cost;

	    n1 = (n < ncols ? n+1 : ncols);
	    if (n < ncols)
		this_height = page_height - headspace;
	    else
		this_height = page_height;

	    l->bestcost[n] = -1;
	    for (m = l; m; m = m->next) {
		if (m != l && m->page_break)
		    break;	       /* we've gone as far as we can */

		if (m != l) {
		    if (m->prev->space_after > 0)
			space += m->prev->space_after;
		    else
			text += m->prev->space_after;
		}
		if (m != l || m->page_break) {
		    if (m->space_before > 0)
			space += m->space_before;
		    else
			text += m->space_before;
		}
		text += m->line_height;
		minheight = text + space;

		if (m != l && minheight > this_height)
		    break;

		/*
		 * If the space after this paragraph is _negative_
		 * (which means the next line is folded on to this
		 * one, which happens in the index), we absolutely
		 * cannot break here.
		 */
		if (m->space_after >= 0) {

		    /*
		     * Compute the cost of this arrangement, as the
		     * square of the amount of wasted space on the
		     * page. Exception: if this is the last page
		     * before a mandatory break or the document
		     * end, we don't penalise a large blank area.
		     */
		    if (m != last && m->next && !m->next->page_break)
		    {
			int x = (this_height - minheight) / FUNITS_PER_PT *
			    4096.0;
			int xf;

			xf = x & 0xFF;
			x >>= 8;

			cost = x*x;
			cost += (x * xf) >> 8;
		    } else
			cost = 0;

		    if (m != last && m->next && !m->next->page_break) {
			cost += m->penalty_after;
			cost += m->next->penalty_before;
		    }

		    if (m != last && m->next && !m->next->page_break)
			cost += m->next->bestcost[n1];
		    if (l->bestcost[n] == -1 || l->bestcost[n] > cost) {
			/*
			 * This is the best option yet for this
			 * starting point.
			 */
			l->bestcost[n] = cost;
			if (m != last && m->next && !m->next->page_break)
			    l->vshortfall[n] = this_height - minheight;
			else
			    l->vshortfall[n] = 0;
			l->text[n] = text;
			l->space[n] = space;
			l->page_last[n] = m;
		    }
		}

		if (m == last)
		    break;
	    }
	}
    }

    /*
     * Now go through the line list forwards and assemble the
     * actual pages.
     */
    ph = pt = NULL;

    l = first;
    n = 0;
    while (l) {
	page_data *page;
	int text, space, head;

	page = snew(page_data);
	page->next = NULL;
	page->prev = pt;
	if (pt)
	    pt->next = page;
	else
	    ph = page;
	pt = page;

	page->first_line = l;
	page->last_line = l->page_last[n];

	page->first_text = page->last_text = NULL;
	page->first_xref = page->last_xref = NULL;
	page->first_rect = page->last_rect = NULL;

	/*
	 * Now assign a y-coordinate to each line on the page.
	 */
	text = space = 0;
	head = (n < ncols ? headspace : 0);
	for (l = page->first_line; l; l = l->next) {
	    if (l != page->first_line) {
		if (l->prev->space_after > 0)
		    space += l->prev->space_after;
		else
		    text += l->prev->space_after;
	    }
	    if (l != page->first_line || l->page_break) {
		if (l->space_before > 0)
		    space += l->space_before;
		else
		    text += l->space_before;
	    }
	    text += l->line_height;

	    l->page = page;
	    l->ypos = text + space + head;
	    if (page->first_line->space[n]) {
		l->ypos += space * (float)page->first_line->vshortfall[n] /
		    page->first_line->space[n];
	    }

	    if (l == page->last_line)
		break;
	}

	l = page->last_line;
	if (l == last)
	    break;
	l = l->next;

	n = (n < ncols ? n+1 : ncols);
    }

    return ph;
}

static void add_rect_to_page(page_data *page, int x, int y, int w, int h)
{
    rect *r = snew(rect);

    r->next = NULL;
    if (page->last_rect)
	page->last_rect->next = r;
    else
	page->first_rect = r;
    page->last_rect = r;

    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
}

static void add_string_to_page(page_data *page, int x, int y,
			       font_encoding *fe, int size, char *text,
			       int width)
{
    text_fragment *frag;

    frag = snew(text_fragment);
    frag->next = NULL;

    if (page->last_text)
	page->last_text->next = frag;
    else
	page->first_text = frag;
    page->last_text = frag;

    frag->x = x;
    frag->y = y;
    frag->fe = fe;
    frag->fontsize = size;
    frag->text = dupstr(text);
    frag->width = width;
}

/*
 * Returns the updated x coordinate.
 */
static int render_string(page_data *page, font_data *font, int fontsize,
			 int x, int y, wchar_t *str, unsigned flags)
{
    char *text;
    int textpos, textwid, kern, nglyph, glyph, oglyph, lig;
    font_encoding *subfont = NULL, *sf;
    subfont_map_entry *sme;

    text = snewn(1 + ustrlen(str), char);
    textpos = textwid = 0;

    glyph = NOGLYPH;
    nglyph = utoglyph(font->info, *str);
    while (*str) {
	oglyph = glyph;
	glyph = nglyph;
	nglyph = utoglyph(font->info, str[1]);

	if (glyph == NOGLYPH) {
	    str++;
	    continue;		       /* nothing more we can do here */
	}

	if (!(flags & RS_NOLIG) &&
	    (lig = find_lig(font, glyph, nglyph)) != NOGLYPH) {
	    nglyph = lig;
	    str++;
	    continue;
	}

	/*
	 * Find which subfont this character is going in.
	 */
	sme = encode_glyph(glyph, *str, font);
	sf = sme->subfont;

	kern = find_kern(font, oglyph, glyph) * fontsize;

	if (!subfont || sf != subfont || kern) {
	    if (subfont) {
		text[textpos] = '\0';
		add_string_to_page(page, x, y, subfont, fontsize, text,
				   textwid);
		x += textwid + kern;
	    } else {
		assert(textpos == 0);
	    }
	    textpos = 0;
	    textwid = 0;
	    subfont = sf;
	}

	text[textpos++] = sme->position;
	textwid += find_width(font, glyph) * fontsize;

	str++;
    }

    if (textpos > 0) {
	text[textpos] = '\0';
	add_string_to_page(page, x, y, subfont, fontsize, text, textwid);
	x += textwid;
    }

    return x;
}

/*
 * Returns the updated x coordinate.
 */
static int render_text(page_data *page, para_data *pdata, line_data *ldata,
		       int x, int y, word *text, word *text_end, xref **xr,
		       int shortfall, int nspaces, int *nspace,
		       keywordlist *keywords, indexdata *idx, paper_conf *conf)
{
    while (text && text != text_end) {
	int style, type, findex, errs;
	wchar_t *str;
	xref_dest dest;
	unsigned flags = 0;

	switch (text->type) {
	    /*
	     * Start a cross-reference.
	     */
	  case word_HyperLink:
	  case word_UpperXref:
	  case word_LowerXref:
	  case word_PageXref:

	    if (text->type == word_HyperLink) {
		dest.type = URL;
		dest.url = utoa_dup(text->text, CS_ASCII);
		dest.page = NULL;
	    } else if (text->type == word_PageXref) {
		dest.type = PAGE;
		dest.url = NULL;
		dest.page = (page_data *)text->private_data;
	    } else {
		keyword *kwl = kw_lookup(keywords, text->text);
		para_data *pdata;

		if (kwl) {
		    assert(kwl->para->private_data);
		    pdata = (para_data *) kwl->para->private_data;
		    dest.type = PAGE;
		    dest.page = pdata->first->page;
		    dest.url = NULL;
		} else {
		    /*
		     * Shouldn't happen, but *shrug*
		     */
		    dest.type = NONE;
		    dest.page = NULL;
		    dest.url = NULL;
		}
	    }
	    if (dest.type != NONE) {
		*xr = snew(xref);
		(*xr)->dest = dest;    /* structure copy */
		if (page->last_xref)
		    page->last_xref->next = *xr;
		else
		    page->first_xref = *xr;
		page->last_xref = *xr;
		(*xr)->next = NULL;

		/*
		 * FIXME: Ideally we should have, and use, some
		 * vertical font metric information here so that
		 * our cross-ref rectangle can take account of
		 * descenders and the font's cap height. This will
		 * do for the moment, but it isn't ideal.
		 */
		(*xr)->lx = (*xr)->rx = x;
		(*xr)->by = y;
		(*xr)->ty = y + ldata->line_height;
	    }
	    goto nextword;
	    
	    /*
	     * Finish extending a cross-reference box.
	     */
	  case word_HyperEnd:
	  case word_XrefEnd:
	    *xr = NULL;
	    goto nextword;

	    /*
	     * Add the current page number to the list of pages
	     * referenced by an index entry.
	     */
	  case word_IndexRef:
	    /*
	     * We don't create index references in contents entries.
	     */
	    if (!pdata->contents_entry) {
		indextag *tag;
		int i;

		tag = index_findtag(idx, text->text);
		if (!tag)
		    goto nextword;

		for (i = 0; i < tag->nrefs; i++) {
		    indexentry *entry = tag->refs[i];
		    paper_idx *pi = (paper_idx *)entry->backend_data;

		    /*
		     * If the same index term is indexed twice
		     * within the same section, we only want to
		     * mention it once in the index.
		     */
		    if (pi->lastpage != page) {
			word **wp;

			if (pi->lastword) {
			    pi->lastword = pi->lastword->next =
				fake_word(L",");
			    pi->lastword = pi->lastword->next =
				fake_space_word();
			    wp = &pi->lastword->next;
			} else
			    wp = &pi->words;

			pi->lastword = *wp =
			    fake_page_ref(page);
			pi->lastword = pi->lastword->next =
			    fake_word(page->number);
			pi->lastword = pi->lastword->next =
			    fake_end_ref();
		    }

		    pi->lastpage = page;
		}
	    }
	    goto nextword;
	}

	style = towordstyle(text->type);
	type = removeattr(text->type);

	findex = (style == word_Normal ? FONT_NORMAL :
		  style == word_Emph ? FONT_EMPH :
		  FONT_CODE);

	if (style == word_Code || style == word_WeakCode) flags |= RS_NOLIG;
	flags |= pdata->extraflags;

	if (type == word_Normal) {
	    str = text->text;
	} else if (type == word_WhiteSpace) {
	    x += pdata->sizes[findex] *
		string_width(pdata->fonts[findex], L" ", NULL, 0);
	    if (nspaces && findex != FONT_CODE) {
		x += (*nspace+1) * shortfall / nspaces;
		x -= *nspace * shortfall / nspaces;
		(*nspace)++;
	    }
	    goto nextword;
	} else /* if (type == word_Quote) */ {
	    if (text->aux == quote_Open)
		str = conf->lquote;
	    else
		str = conf->rquote;
	}

	(void) string_width(pdata->fonts[findex], str, &errs, flags);

	if (errs && text->alt)
	    x = render_text(page, pdata, ldata, x, y, text->alt, NULL,
			    xr, shortfall, nspaces, nspace, keywords, idx,
			    conf);
	else
	    x = render_string(page, pdata->fonts[findex],
			      pdata->sizes[findex], x, y, str, flags);

	if (*xr)
	    (*xr)->rx = x;

	nextword:
	text = text->next;
    }

    return x;
}

/*
 * Returns the last x position used on the line.
 */
static int render_line(line_data *ldata, int left_x, int top_y,
		       xref_dest *dest, keywordlist *keywords, indexdata *idx,
		       paper_conf *conf)
{
    int nspace;
    xref *xr;
    int ret = 0;
    
    if (ldata->aux_text) {
	int x;
	xr = NULL;
	nspace = 0;
	x = render_text(ldata->page, ldata->pdata, ldata,
			left_x + ldata->aux_left_indent,
			top_y - ldata->ypos,
			ldata->aux_text, NULL, &xr, 0, 0, &nspace,
			keywords, idx, conf);
	if (ldata->aux_text_2)
	    render_text(ldata->page, ldata->pdata, ldata,
			x, top_y - ldata->ypos,
			ldata->aux_text_2, NULL, &xr, 0, 0, &nspace,
			keywords, idx, conf);
    }
    nspace = 0;

    if (ldata->first) {
	/*
	 * There might be a cross-reference carried over from a
	 * previous line.
	 */
	if (dest->type != NONE) {
	    xr = snew(xref);
	    xr->next = NULL;
	    xr->dest = *dest;    /* structure copy */
	    if (ldata->page->last_xref)
		ldata->page->last_xref->next = xr;
	    else
		ldata->page->first_xref = xr;
	    ldata->page->last_xref = xr;
	    xr->lx = xr->rx = left_x + ldata->xpos;
	    xr->by = top_y - ldata->ypos;
	    xr->ty = top_y - ldata->ypos + ldata->line_height;
	} else
	    xr = NULL;

	{
	    int extra_indent, shortfall, spaces;
	    int just = ldata->pdata->justification;

	    /*
	     * All forms of justification become JUST when we have
	     * to squeeze the paragraph.
	     */
	    if (ldata->hshortfall < 0)
		just = JUST;

	    switch (just) {
	      case JUST:
		shortfall = ldata->hshortfall;
		spaces = ldata->nspaces;
		extra_indent = 0;
		break;
	      case LEFT:
		shortfall = spaces = extra_indent = 0;
		break;
	      case RIGHT:
		shortfall = spaces = 0;
		extra_indent = ldata->real_shortfall;
		break;
	    }

	    ret = render_text(ldata->page, ldata->pdata, ldata,
			      left_x + ldata->xpos + extra_indent,
			      top_y - ldata->ypos, ldata->first, ldata->end,
			      &xr, shortfall, spaces, &nspace,
			      keywords, idx, conf);
	}

	if (xr) {
	    /*
	     * There's a cross-reference continued on to the next line.
	     */
	    *dest = xr->dest;
	} else
	    dest->type = NONE;
    }

    return ret;
}

static void render_para(para_data *pdata, paper_conf *conf,
			keywordlist *keywords, indexdata *idx,
			paragraph *index_placeholder, page_data *index_page)
{
    int last_x;
    xref *cxref;
    page_data *cxref_page;
    xref_dest dest;
    para_data *target;
    line_data *ldata;

    dest.type = NONE;
    cxref = NULL;
    cxref_page = NULL;

    for (ldata = pdata->first; ldata; ldata = ldata->next) {
	/*
	 * If this is a contents entry, we expect to have a single
	 * enormous cross-reference rectangle covering the whole
	 * thing. (Unless, of course, it spans multiple pages.)
	 */
	if (pdata->contents_entry && ldata->page != cxref_page) {
	    cxref_page = ldata->page;
	    cxref = snew(xref);
	    cxref->next = NULL;
	    cxref->dest.type = PAGE;
	    if (pdata->contents_entry == index_placeholder) {
		cxref->dest.page = index_page;
	    } else {
		assert(pdata->contents_entry->private_data);
		target = (para_data *)pdata->contents_entry->private_data;
		cxref->dest.page = target->first->page;
	    }
	    cxref->dest.url = NULL;
	    if (ldata->page->last_xref)
		ldata->page->last_xref->next = cxref;
	    else
		ldata->page->first_xref = cxref;
	    ldata->page->last_xref = cxref;
	    cxref->lx = conf->left_margin;
	    cxref->rx = conf->paper_width - conf->right_margin;
	    cxref->ty = conf->paper_height - conf->top_margin
		- ldata->ypos + ldata->line_height;
	}
	if (pdata->contents_entry) {
	    assert(cxref != NULL);
	    cxref->by = conf->paper_height - conf->top_margin
		- ldata->ypos;
	}

	last_x = render_line(ldata, conf->left_margin,
			     conf->paper_height - conf->top_margin,
			     &dest, keywords, idx, conf);
	if (ldata == pdata->last)
	    break;
    }

    /*
     * If this is a contents entry, add leaders and a page
     * number.
     */
    if (pdata->contents_entry) {
	word *w;
	wchar_t *num;
	int wid;
	int x;

	if (pdata->contents_entry == index_placeholder) {
	    num = index_page->number;
	} else {
	    assert(pdata->contents_entry->private_data);
	    target = (para_data *)pdata->contents_entry->private_data;
	    num = target->first->page->number;
	}

	w = fake_word(num);
	wid = paper_width_simple(pdata, w, conf);
	sfree(w);

	for (x = 0; x < conf->base_width; x += conf->leader_separation)
	    if (x - conf->leader_separation > last_x - conf->left_margin &&
		x + conf->leader_separation < conf->base_width - wid)
		render_string(pdata->last->page,
			      pdata->fonts[FONT_NORMAL],
			      pdata->sizes[FONT_NORMAL],
			      conf->left_margin + x,
			      (conf->paper_height - conf->top_margin -
			       pdata->last->ypos), L".", 0);

	render_string(pdata->last->page,
		      pdata->fonts[FONT_NORMAL],
		      pdata->sizes[FONT_NORMAL],
		      conf->paper_width - conf->right_margin - wid,
		      (conf->paper_height - conf->top_margin -
		       pdata->last->ypos), num, 0);
    }

    /*
     * Render any rectangle (chapter title underline or rule)
     * that goes with this paragraph.
     */
    switch (pdata->rect_type) {
      case RECT_CHAPTER_UNDERLINE:
	add_rect_to_page(pdata->last->page,
			 conf->left_margin,
			 (conf->paper_height - conf->top_margin -
			  pdata->last->ypos -
			  conf->chapter_underline_depth),
			 conf->base_width,
			 conf->chapter_underline_thickness);
	break;
      case RECT_RULE:
	add_rect_to_page(pdata->first->page,
			 conf->left_margin + pdata->first->xpos,
			 (conf->paper_height - conf->top_margin -
			  pdata->last->ypos -
			  pdata->last->line_height),
			 conf->base_width - pdata->first->xpos,
			 pdata->last->line_height);
	break;
      default:		       /* placate gcc */
	break;
    }
}

static para_data *code_paragraph(int indent, word *words, paper_conf *conf)
{
    para_data *pdata = snew(para_data);

    /*
     * For code paragraphs, I'm going to hack grievously and
     * pretend the three normal fonts are the three code paragraph
     * fonts.
     */
    setfont(pdata, &conf->fcode);

    pdata->first = pdata->last = NULL;
    pdata->outline_level = -1;
    pdata->rect_type = RECT_NONE;
    pdata->contents_entry = NULL;
    pdata->justification = LEFT;
    pdata->extraflags = RS_NOLIG;

    for (; words; words = words->next) {
	wchar_t *t, *e, *start;
	word *lhead = NULL, *ltail = NULL, *w;
	line_data *ldata;
	int prev = -1, curr;

	t = words->text;
	if (words->next && words->next->type == word_Emph) {
	    e = words->next->text;
	    words = words->next;
	} else
	    e = NULL;

	start = t;

	while (*start) {
	    while (*t) {
		if (!e || !*e)
		    curr = 0;
		else if (*e == L'i')
		    curr = 1;
		else if (*e == L'b')
		    curr = 2;
		else
		    curr = 0;

		if (prev < 0)
		    prev = curr;

		if (curr != prev)
		    break;

		t++;
		if (e && *e)
		    e++;
	    }

	    /*
	     * We've isolated a maximal subsequence of the line
	     * which has the same emphasis. Form it into a word
	     * structure.
	     */
	    w = snew(word);
	    w->next = NULL;
	    w->alt = NULL;
	    w->type = (prev == 0 ? word_WeakCode :
		      prev == 1 ? word_Emph : word_Normal);
	    w->text = snewn(t-start+1, wchar_t);
	    memcpy(w->text, start, (t-start) * sizeof(wchar_t));
	    w->text[t-start] = '\0';
	    w->breaks = FALSE;

	    if (ltail)
		ltail->next = w;
	    else
		lhead = w;
	    ltail = w;

	    start = t;
	    prev = -1;
	}

	ldata = snew(line_data);

	ldata->pdata = pdata;
	ldata->first = lhead;
	ldata->end = NULL;
	ldata->line_height = conf->fcode.font_size * UNITS_PER_PT;

	ldata->xpos = indent;

	if (pdata->last) {
	    pdata->last->next = ldata;
	    ldata->prev = pdata->last;
	} else {
	    pdata->first = ldata;
	    ldata->prev = NULL;
	}
	ldata->next = NULL;
	pdata->last = ldata;

	ldata->hshortfall = 0;
	ldata->nspaces = 0;
	ldata->aux_text = NULL;
	ldata->aux_text_2 = NULL;
	ldata->aux_left_indent = 0;
	/* General opprobrium for breaking in a code paragraph. */
	ldata->penalty_before = ldata->penalty_after = 50000;
    }

    standard_line_spacing(pdata, conf);

    return pdata;
}

static para_data *rule_paragraph(int indent, paper_conf *conf)
{
    para_data *pdata = snew(para_data);
    line_data *ldata;

    ldata = snew(line_data);

    ldata->pdata = pdata;
    ldata->first = NULL;
    ldata->end = NULL;
    ldata->line_height = conf->rule_thickness;

    ldata->xpos = indent;

    ldata->prev = NULL;
    ldata->next = NULL;

    ldata->hshortfall = 0;
    ldata->nspaces = 0;
    ldata->aux_text = NULL;
    ldata->aux_text_2 = NULL;
    ldata->aux_left_indent = 0;

    /*
     * Better to break after a rule than before it
     */
    ldata->penalty_after += 100000;
    ldata->penalty_before += -100000;

    pdata->first = pdata->last = ldata;
    pdata->outline_level = -1;
    pdata->rect_type = RECT_RULE;
    pdata->contents_entry = NULL;
    pdata->justification = LEFT;
    pdata->extraflags = 0;

    standard_line_spacing(pdata, conf);

    return pdata;
}

/*
 * Plain-text-like formatting for outline titles.
 */
static void paper_rdaddw(rdstring *rs, word *text) {
    for (; text; text = text->next) switch (text->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_UpperXref:
      case word_LowerXref:
      case word_XrefEnd:
      case word_IndexRef:
	break;

      case word_Normal:
      case word_Emph:
      case word_Code:
      case word_WeakCode:
      case word_WhiteSpace:
      case word_EmphSpace:
      case word_CodeSpace:
      case word_WkCodeSpace:
      case word_Quote:
      case word_EmphQuote:
      case word_CodeQuote:
      case word_WkCodeQuote:
	assert(text->type != word_CodeQuote &&
	       text->type != word_WkCodeQuote);
	if (towordstyle(text->type) == word_Emph &&
	    (attraux(text->aux) == attr_First ||
	     attraux(text->aux) == attr_Only))
	    rdadd(rs, L'_');	       /* FIXME: configurability */
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_First ||
		  attraux(text->aux) == attr_Only))
	    rdadd(rs, L'\'');	       /* FIXME: configurability */
	if (removeattr(text->type) == word_Normal) {
	    rdadds(rs, text->text);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    rdadd(rs, L' ');
	} else if (removeattr(text->type) == word_Quote) {
	    rdadd(rs, L'\'');	       /* fixme: configurability */
	}
	if (towordstyle(text->type) == word_Emph &&
	    (attraux(text->aux) == attr_Last ||
	     attraux(text->aux) == attr_Only))
	    rdadd(rs, L'_');	       /* FIXME: configurability */
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_Last ||
		  attraux(text->aux) == attr_Only))
	    rdadd(rs, L'\'');	       /* FIXME: configurability */
	break;
    }
}

static wchar_t *prepare_outline_title(word *first, wchar_t *separator,
				      word *second)
{
    rdstring rs = {0, 0, NULL};

    if (first)
	paper_rdaddw(&rs, first);
    if (separator)
	rdadds(&rs, separator);
    if (second)
	paper_rdaddw(&rs, second);

    return rs.text;
}

static word *fake_word(wchar_t *text)
{
    word *ret = snew(word);
    ret->next = NULL;
    ret->alt = NULL;
    ret->type = word_Normal;
    ret->text = ustrdup(text);
    ret->breaks = FALSE;
    ret->aux = 0;
    return ret;
}

static word *fake_space_word(void)
{
    word *ret = snew(word);
    ret->next = NULL;
    ret->alt = NULL;
    ret->type = word_WhiteSpace;
    ret->text = NULL;
    ret->breaks = TRUE;
    ret->aux = 0;
    return ret;
}

static word *fake_page_ref(page_data *page)
{
    word *ret = snew(word);
    ret->next = NULL;
    ret->alt = NULL;
    ret->type = word_PageXref;
    ret->text = NULL;
    ret->breaks = FALSE;
    ret->aux = 0;
    ret->private_data = page;
    return ret;
}

static word *fake_end_ref(void)
{
    word *ret = snew(word);
    ret->next = NULL;
    ret->alt = NULL;
    ret->type = word_XrefEnd;
    ret->text = NULL;
    ret->breaks = FALSE;
    ret->aux = 0;
    return ret;
}

static word *prepare_contents_title(word *first, wchar_t *separator,
				    word *second)
{
    word *ret;
    word **wptr, *w;

    wptr = &ret;

    if (first) {
	w = dup_word_list(first);
	*wptr = w;
	while (w->next)
	    w = w->next;
	wptr = &w->next;
    }

    if (separator) {
	w = fake_word(separator);
	*wptr = w;
	wptr = &w->next;
    }

    if (second) {
	*wptr = dup_word_list(second);
    }

    return ret;
}

static void fold_into_page(page_data *dest, page_data *src, int right_shift)
{
    line_data *ldata;

    if (!src->first_line)
	return;

    if (dest->last_line) {
	dest->last_line->next = src->first_line;
	src->first_line->prev = dest->last_line;
    }
    dest->last_line = src->last_line;

    for (ldata = src->first_line; ldata; ldata = ldata->next) {
	ldata->page = dest;
	ldata->xpos += right_shift;

	if (ldata == src->last_line)
	    break;
    }
}
