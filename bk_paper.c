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
 *  - we should use PDFDocEncoding or Unicode for outline strings,
 *    now that I actually know how to do them. Probably easiest if
 *    I do this _after_ bringing in libcharset, since I can simply
 *    supply PDFDocEncoding in there.
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
 *     * all the measurements in `conf' should be configurable
 *        + notably paper size/shape
 *     * page header and footer should be configurable; we should
 * 	 be able to shift the page number elsewhere, and add other
 * 	 things such as the current chapter/section title and fixed
 * 	 text
 *     * remove the fixed mapping from heading levels to heading
 * 	 styles; offer a menu of styles from which the user can
 * 	 choose at every heading level
 *     * first-line indent in paragraphs
 *     * fixed text: `Contents', `Index', bullet, quotes, the
 * 	 colon-space and full stop in chapter title constructions
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
 *  - ability to import other Type 1 fonts
 *     * we need to parse the font to extract its metrics
 *     * then we pass the font bodily to both PS and PDF so it can
 * 	 be included in the output file
 * 
 *  - character substitution for better typography?
 *     * fi, fl, ffi, ffl ligatures
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

#include "halibut.h"
#include "paper.h"

typedef struct paper_conf_Tag paper_conf;
typedef struct paper_idx_Tag paper_idx;

struct paper_conf_Tag {
    int paper_width;
    int paper_height;
    int left_margin;
    int top_margin;
    int right_margin;
    int bottom_margin;
    int indent_list_bullet;
    int indent_list;
    int indent_quote;
    int base_leading;
    int base_para_spacing;
    int chapter_top_space;
    int sect_num_left_space;
    int chapter_underline_depth;
    int chapter_underline_thickness;
    int rule_thickness;
    int base_font_size;
    int contents_indent_step;
    int contents_margin;
    int leader_separation;
    int index_gutter;
    int index_cols;
    int index_minsep;
    int pagenum_fontsize;
    int footer_distance;
    /* These are derived from the above */
    int base_width;
    int page_height;
    int index_colwidth;
    /* Fonts used in the configuration */
    font_data *tr, *ti, *hr, *hi, *cr, *co, *cb;
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

static font_data *make_std_font(font_list *fontlist, char const *name);
static void wrap_paragraph(para_data *pdata, word *words,
			   int w, int i1, int i2);
static page_data *page_breaks(line_data *first, line_data *last,
			      int page_height, int ncols, int headspace);
static int render_string(page_data *page, font_data *font, int fontsize,
			 int x, int y, wchar_t *str);
static int render_line(line_data *ldata, int left_x, int top_y,
		       xref_dest *dest, keywordlist *keywords, indexdata *idx);
static void render_para(para_data *pdata, paper_conf *conf,
			keywordlist *keywords, indexdata *idx,
			paragraph *index_placeholder, page_data *index_page);
static int string_width(font_data *font, wchar_t const *string, int *errs);
static int paper_width_simple(para_data *pdata, word *text);
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
    paper_conf *conf;
    int has_index;
    int pagenum;
    paragraph index_placeholder_para;
    page_data *first_index_page;

    /*
     * FIXME: All these things ought to become configurable.
     */
    conf = mknew(paper_conf);
    conf->paper_width = 595 * 4096;
    conf->paper_height = 841 * 4096;
    conf->left_margin = 72 * 4096;
    conf->top_margin = 72 * 4096;
    conf->right_margin = 72 * 4096;
    conf->bottom_margin = 108 * 4096;
    conf->indent_list_bullet = 6 * 4096;
    conf->indent_list = 24 * 4096;
    conf->indent_quote = 18 * 4096;
    conf->base_leading = 4096;
    conf->base_para_spacing = 10 * 4096;
    conf->chapter_top_space = 72 * 4096;
    conf->sect_num_left_space = 12 * 4096;
    conf->chapter_underline_depth = 14 * 4096;
    conf->chapter_underline_thickness = 3 * 4096;
    conf->rule_thickness = 1 * 4096;
    conf->base_font_size = 12;
    conf->contents_indent_step = 24 * 4096;
    conf->contents_margin = 84 * 4096;
    conf->leader_separation = 12 * 4096;
    conf->index_gutter = 36 * 4096;
    conf->index_cols = 2;
    conf->index_minsep = 18 * 4096;
    conf->pagenum_fontsize = 12;
    conf->footer_distance = 32 * 4096;

    conf->base_width =
	conf->paper_width - conf->left_margin - conf->right_margin;
    conf->page_height =
	conf->paper_height - conf->top_margin - conf->bottom_margin;
    conf->index_colwidth =
	(conf->base_width - (conf->index_cols-1) * conf->index_gutter)
	/ conf->index_cols;

    /*
     * First, set up some font structures.
     */
    fontlist = mknew(font_list);
    fontlist->head = fontlist->tail = NULL;
    conf->tr = make_std_font(fontlist, "Times-Roman");
    conf->ti = make_std_font(fontlist, "Times-Italic");
    conf->hr = make_std_font(fontlist, "Helvetica-Bold");
    conf->hi = make_std_font(fontlist, "Helvetica-BoldOblique");
    conf->cr = make_std_font(fontlist, "Courier");
    conf->co = make_std_font(fontlist, "Courier-Oblique");
    conf->cb = make_std_font(fontlist, "Courier-Bold");

    /*
     * Set up a data structure to collect page numbers for each
     * index entry.
     */
    {
	int i;
	indexentry *entry;

	has_index = FALSE;

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    paper_idx *pi = mknew(paper_idx);

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
	contents_title = fake_word(L"Contents");

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
				   NULL, NULL, fake_word(L"Index"), conf);
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
	    first_index_page = mknew(page_data);
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
	index_title = fake_word(L"Index");

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
		string_width(conf->tr, page->number, NULL);

	    render_string(page, conf->tr, conf->pagenum_fontsize,
			  conf->left_margin + (conf->base_width - width)/2,
			  conf->bottom_margin - conf->footer_distance,
			  page->number);
	}
    }

    /*
     * Start putting together the overall document structure we're
     * going to return.
     */
    doc = mknew(document);
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

	doc->outline_elements = mknewa(outline_element, osize);
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
			resize(doc->outline_elements, osize);
		}

		doc->outline_elements[doc->n_outline_elements].level =
		    pdata->outline_level;
		doc->outline_elements[doc->n_outline_elements].pdata = pdata;
		doc->n_outline_elements++;
	    }
	}
    }

    sfree(conf);

    return doc;
}

static para_data *make_para_data(int ptype, int paux, int indent, int rmargin,
				 word *pkwtext, word *pkwtext2, word *pwords,
				 paper_conf *conf)
{
    para_data *pdata;
    line_data *ldata;
    int extra_indent, firstline_indent, aux_indent;
    word *aux, *aux2;

    pdata = mknew(para_data);
    pdata->outline_level = -1;
    pdata->outline_title = NULL;
    pdata->rect_type = RECT_NONE;
    pdata->contents_entry = NULL;
    pdata->justification = JUST;

    /*
     * Choose fonts for this paragraph.
     *
     * FIXME: All of this ought to be completely
     * user-configurable.
     */
    switch (ptype) {
      case para_Title:
	pdata->fonts[FONT_NORMAL] = conf->hr;
	pdata->sizes[FONT_NORMAL] = 24;
	pdata->fonts[FONT_EMPH] = conf->hi;
	pdata->sizes[FONT_EMPH] = 24;
	pdata->fonts[FONT_CODE] = conf->cb;
	pdata->sizes[FONT_CODE] = 24;
	pdata->outline_level = 0;
	break;

      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
	pdata->fonts[FONT_NORMAL] = conf->hr;
	pdata->sizes[FONT_NORMAL] = 20;
	pdata->fonts[FONT_EMPH] = conf->hi;
	pdata->sizes[FONT_EMPH] = 20;
	pdata->fonts[FONT_CODE] = conf->cb;
	pdata->sizes[FONT_CODE] = 20;
	pdata->outline_level = 1;
	break;

      case para_Heading:
      case para_Subsect:
	pdata->fonts[FONT_NORMAL] = conf->hr;
	pdata->fonts[FONT_EMPH] = conf->hi;
	pdata->fonts[FONT_CODE] = conf->cb;
	pdata->sizes[FONT_NORMAL] =
	    pdata->sizes[FONT_EMPH] =
	    pdata->sizes[FONT_CODE] =
	    (paux == 0 ? 16 : paux == 1 ? 14 : 13);
	pdata->outline_level = 2 + paux;
	break;

      case para_Normal:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
      case para_DescribedThing:
      case para_Description:
      case para_Copyright:
	pdata->fonts[FONT_NORMAL] = conf->tr;
	pdata->sizes[FONT_NORMAL] = 12;
	pdata->fonts[FONT_EMPH] = conf->ti;
	pdata->sizes[FONT_EMPH] = 12;
	pdata->fonts[FONT_CODE] = conf->cr;
	pdata->sizes[FONT_CODE] = 12;
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
	    len = paper_width_simple(pdata, pkwtext2);
	    aux_indent = -len - conf->sect_num_left_space;

	    pdata->outline_title = 
		prepare_outline_title(pkwtext2, L" ", pwords);
	} else {
	    aux = pkwtext;
	    aux2 = fake_word(L": ");
	    aux_indent = 0;

	    firstline_indent += paper_width_simple(pdata, aux);
	    firstline_indent += paper_width_simple(pdata, aux2);

	    pdata->outline_title = 
		prepare_outline_title(pkwtext, L": ", pwords);
	}
	break;

      case para_Bullet:
	/*
	 * Auxiliary text consisting of a bullet. (FIXME:
	 * configurable bullet.)
	 */
	aux = fake_word(L"\x2022");
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
	firstline_indent += paper_width_simple(pdata, aux);
	firstline_indent += paper_width_simple(pdata, aux2);
	break;
    }

    if (pdata->outline_level >= 0 && !pdata->outline_title) {
	pdata->outline_title = 
	    prepare_outline_title(NULL, NULL, pwords);
    }

    wrap_paragraph(pdata, pwords, conf->base_width - rmargin,
		   indent + firstline_indent,
		   indent + extra_indent);

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

    fe = mknew(font_encoding);
    fe->next = NULL;

    if (font->list->tail)
	font->list->tail->next = fe;
    else
	font->list->head = fe;
    font->list->tail = fe;

    fe->font = font;
    fe->free_pos = 0x21;

    for (i = 0; i < 256; i++) {
	fe->vector[i] = NULL;
	fe->indices[i] = -1;
	fe->to_unicode[i] = 0xFFFF;
    }

    return fe;
}

static font_data *make_std_font(font_list *fontlist, char const *name)
{
    const int *widths;
    int nglyphs;
    font_data *f;
    font_encoding *fe;
    int i;

    widths = ps_std_font_widths(name);
    if (!widths)
	return NULL;

    for (nglyphs = 0; ps_std_glyphs[nglyphs] != NULL; nglyphs++);

    f = mknew(font_data);

    f->list = fontlist;
    f->name = name;
    f->nglyphs = nglyphs;
    f->glyphs = ps_std_glyphs;
    f->widths = widths;
    f->subfont_map = mknewa(subfont_map_entry, nglyphs);

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

    for (i = 0; i < (int)lenof(f->bmp); i++)
	f->bmp[i] = 0xFFFF;

    for (i = 0; i < nglyphs; i++) {
	wchar_t ucs;
	ucs = ps_glyph_to_unicode(f->glyphs[i]);
	assert(ucs != 0xFFFF);
	f->bmp[ucs] = i;
	if (ucs >= 0x20 && ucs <= 0x7E) {
	    fe->vector[ucs] = f->glyphs[i];
	    fe->indices[ucs] = i;
	    fe->to_unicode[ucs] = ucs;
	    f->subfont_map[i].subfont = fe;
	    f->subfont_map[i].position = ucs;
	} else {
	    /*
	     * This character is not yet assigned to a subfont.
	     */
	    f->subfont_map[i].subfont = NULL;
	    f->subfont_map[i].position = 0;
	}
    }

    return f;
}

static int string_width(font_data *font, wchar_t const *string, int *errs)
{
    int width = 0;

    if (errs)
	*errs = 0;

    for (; *string; string++) {
	int index;

	index = font->bmp[(unsigned short)*string];
	if (index == 0xFFFF) {
	    if (errs)
		*errs = 1;
	} else {
	    width += font->widths[index];
	}
    }

    return width;
}

static int paper_width_internal(void *vctx, word *word, int *nspaces);

struct paper_width_ctx {
    int minspacewidth;
    para_data *pdata;
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
	    str = L"\x2018";	       /* FIXME: configurability! */
	else
	    str = L"\x2019";	       /* FIXME: configurability! */
    }

    width = string_width(ctx->pdata->fonts[findex], str, &errs);

    if (errs && word->alt)
	return paper_width_list(vctx, word->alt, NULL, nspaces);
    else
	return ctx->pdata->sizes[findex] * width;
}

static int paper_width(void *vctx, word *word)
{
    return paper_width_internal(vctx, word, NULL);
}

static int paper_width_simple(para_data *pdata, word *text)
{
    struct paper_width_ctx ctx;

    ctx.pdata = pdata;
    ctx.minspacewidth =
	(pdata->sizes[FONT_NORMAL] *
	 string_width(pdata->fonts[FONT_NORMAL], L" ", NULL));

    return paper_width_list(&ctx, text, NULL, NULL);
}

static void wrap_paragraph(para_data *pdata, word *words,
			   int w, int i1, int i2)
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
	line_height *= 4096;
    }

    spacewidth = (pdata->sizes[FONT_NORMAL] *
		  string_width(pdata->fonts[FONT_NORMAL], L" ", NULL));
    if (spacewidth == 0) {
	/*
	 * A font without a space?! Disturbing. I hope this never
	 * comes up, but I'll make a random guess anyway and set my
	 * space width to half the point size.
	 */
	spacewidth = pdata->sizes[FONT_NORMAL] * 4096 / 2;
    }

    /*
     * I'm going to set the _minimum_ space width to 3/5 of the
     * standard one, and use the standard one as the optimum.
     */
    ctx.minspacewidth = spacewidth * 3 / 5;
    ctx.pdata = pdata;

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

	ldata = mknew(line_data);

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
	l->bestcost = mknewa(int, ncols+1);
	l->vshortfall = mknewa(int, ncols+1);
	l->text = mknewa(int, ncols+1);
	l->space = mknewa(int, ncols+1);
	l->page_last = mknewa(line_data *, ncols+1);

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
			int x = this_height - minheight;
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

	page = mknew(page_data);
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
	    l->ypos = text + space + head +
		space * (float)page->first_line->vshortfall[n] /
		page->first_line->space[n];

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
    rect *r = mknew(rect);

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

    frag = mknew(text_fragment);
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
			 int x, int y, wchar_t *str)
{
    char *text;
    int textpos, textwid, glyph;
    font_encoding *subfont = NULL, *sf;

    text = mknewa(char, 1 + ustrlen(str));
    textpos = textwid = 0;

    while (*str) {
	glyph = font->bmp[*str];

	if (glyph == 0xFFFF) {
	    str++;
	    continue;		       /* nothing more we can do here */
	}

	/*
	 * Find which subfont this character is going in.
	 */
	sf = font->subfont_map[glyph].subfont;

	if (!sf) {
	    int c;

	    /*
	     * This character is not yet in a subfont. Assign one.
	     */
	    if (font->latest_subfont->free_pos >= 0x100)
		font->latest_subfont = new_font_encoding(font);

	    c = font->latest_subfont->free_pos++;
	    if (font->latest_subfont->free_pos == 0x7F)
		font->latest_subfont->free_pos = 0xA1;

	    font->subfont_map[glyph].subfont = font->latest_subfont;
	    font->subfont_map[glyph].position = c;
	    font->latest_subfont->vector[c] = font->glyphs[glyph];
	    font->latest_subfont->indices[c] = glyph;
	    font->latest_subfont->to_unicode[c] = *str;

	    sf = font->latest_subfont;
	}

	if (!subfont || sf != subfont) {
	    if (subfont) {
		text[textpos] = '\0';
		add_string_to_page(page, x, y, subfont, fontsize, text,
				   textwid);
		x += textwid;
	    } else {
		assert(textpos == 0);
	    }
	    textpos = 0;
	    subfont = sf;
	}

	text[textpos++] = font->subfont_map[glyph].position;
	textwid += font->widths[glyph] * fontsize;

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
		       keywordlist *keywords, indexdata *idx)
{
    while (text && text != text_end) {
	int style, type, findex, errs;
	wchar_t *str;
	xref_dest dest;

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
		*xr = mknew(xref);
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

	if (type == word_Normal) {
	    str = text->text;
	} else if (type == word_WhiteSpace) {
	    x += pdata->sizes[findex] *
		string_width(pdata->fonts[findex], L" ", NULL);
	    if (nspaces && findex != FONT_CODE) {
		x += (*nspace+1) * shortfall / nspaces;
		x -= *nspace * shortfall / nspaces;
		(*nspace)++;
	    }
	    goto nextword;
	} else /* if (type == word_Quote) */ {
	    if (text->aux == quote_Open)
		str = L"\x2018";	       /* FIXME: configurability! */
	    else
		str = L"\x2019";	       /* FIXME: configurability! */
	}

	(void) string_width(pdata->fonts[findex], str, &errs);

	if (errs && text->alt)
	    x = render_text(page, pdata, ldata, x, y, text->alt, NULL,
			    xr, shortfall, nspaces, nspace, keywords, idx);
	else
	    x = render_string(page, pdata->fonts[findex],
			      pdata->sizes[findex], x, y, str);

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
		       xref_dest *dest, keywordlist *keywords, indexdata *idx)
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
			keywords, idx);
	if (ldata->aux_text_2)
	    render_text(ldata->page, ldata->pdata, ldata,
			x, top_y - ldata->ypos,
			ldata->aux_text_2, NULL, &xr, 0, 0, &nspace,
			keywords, idx);
    }
    nspace = 0;

    if (ldata->first) {
	/*
	 * There might be a cross-reference carried over from a
	 * previous line.
	 */
	if (dest->type != NONE) {
	    xr = mknew(xref);
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
			      keywords, idx);
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
	    cxref = mknew(xref);
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
			     &dest, keywords, idx);
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
	wid = paper_width_simple(pdata, w);
	sfree(w);

	for (x = 0; x < conf->base_width; x += conf->leader_separation)
	    if (x - conf->leader_separation > last_x - conf->left_margin &&
		x + conf->leader_separation < conf->base_width - wid)
		render_string(pdata->last->page,
			      pdata->fonts[FONT_NORMAL],
			      pdata->sizes[FONT_NORMAL],
			      conf->left_margin + x,
			      (conf->paper_height - conf->top_margin -
			       pdata->last->ypos), L".");

	render_string(pdata->last->page,
		      pdata->fonts[FONT_NORMAL],
		      pdata->sizes[FONT_NORMAL],
		      conf->paper_width - conf->right_margin - wid,
		      (conf->paper_height - conf->top_margin -
		       pdata->last->ypos), num);
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
    para_data *pdata = mknew(para_data);

    /*
     * For code paragraphs, I'm going to hack grievously and
     * pretend the three normal fonts are the three code paragraph
     * fonts.
     */
    pdata->fonts[FONT_NORMAL] = conf->cb;
    pdata->fonts[FONT_EMPH] = conf->co;
    pdata->fonts[FONT_CODE] = conf->cr;
    pdata->sizes[FONT_NORMAL] =
	pdata->sizes[FONT_EMPH] =
	pdata->sizes[FONT_CODE] = 12;

    pdata->first = pdata->last = NULL;
    pdata->outline_level = -1;
    pdata->rect_type = RECT_NONE;
    pdata->contents_entry = NULL;
    pdata->justification = LEFT;

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
	    w = mknew(word);
	    w->next = NULL;
	    w->alt = NULL;
	    w->type = (prev == 0 ? word_WeakCode :
		      prev == 1 ? word_Emph : word_Normal);
	    w->text = mknewa(wchar_t, t-start+1);
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

	ldata = mknew(line_data);

	ldata->pdata = pdata;
	ldata->first = lhead;
	ldata->end = NULL;
	ldata->line_height = conf->base_font_size * 4096;

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
    para_data *pdata = mknew(para_data);
    line_data *ldata;

    ldata = mknew(line_data);

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
    word *ret = mknew(word);
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
    word *ret = mknew(word);
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
    word *ret = mknew(word);
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
    word *ret = mknew(word);
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
