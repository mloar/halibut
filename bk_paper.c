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
 * To be done:
 * 
 *  - implement para_Rule
 * 
 *  - set up contents section now we know what sections begin on
 *    which pages
 * 
 *  - do PDF outline
 * 
 *  - index
 * 
 *  - header/footer? Page numbers at least would be handy. Fully
 *    configurable footer can wait, though.
 * 
 * That should bring us to the same level of functionality that
 * original-Halibut had, and the same in PDF plus the obvious
 * interactive navigation features. After that, in future work:
 * 
 *  - linearised PDF, perhaps?
 * 
 *  - I'm uncertain of whether I need to include a ToUnicode CMap
 *    in each of my font definitions in PDF. Currently things (by
 *    which I mean cut and paste out of acroread) seem to be
 *    working fairly happily without it, but I don't know.
 * 
 *  - configurability
 * 
 *  - title pages
 */

#include <assert.h>
#include <stdio.h>

#include "halibut.h"
#include "paper.h"

static font_data *make_std_font(font_list *fontlist, char const *name);
static void wrap_paragraph(para_data *pdata, word *words,
			   int w, int i1, int i2);
static page_data *page_breaks(line_data *first, line_data *last,
			      int page_height);
static void render_line(line_data *ldata, int left_x, int top_y,
			xref_dest *dest, keywordlist *keywords);
static int paper_width_simple(para_data *pdata, word *text);
static void code_paragraph(para_data *pdata,
			   font_data *fn, font_data *fi, font_data *fb,
			   int font_size, int indent, word *words);
static void add_rect_to_page(page_data *page, int x, int y, int w, int h);

void *paper_pre_backend(paragraph *sourceform, keywordlist *keywords,
			indexdata *idx) {
    paragraph *p;
    document *doc;
    int indent, extra_indent, firstline_indent, aux_indent;
    para_data *pdata;
    line_data *ldata, *firstline, *lastline;
    font_data *tr, *ti, *hr, *hi, *cr, *co, *cb;
    page_data *pages;
    font_list *fontlist;
    word *aux, *aux2;

    /*
     * FIXME: All these things ought to become configurable.
     */
    int paper_width = 595 * 4096;
    int paper_height = 841 * 4096;
    int left_margin = 72 * 4096;
    int top_margin = 72 * 4096;
    int right_margin = 72 * 4096;
    int bottom_margin = 108 * 4096;
    int indent_list_bullet = 6 * 4096;
    int indent_list = 24 * 4096;
    int indent_quote = 18 * 4096;
    int base_leading = 4096;
    int base_para_spacing = 10 * 4096;
    int chapter_top_space = 72 * 4096;
    int sect_num_left_space = 12 * 4096;
    int chapter_underline_depth = 14 * 4096;
    int chapter_underline_thickness = 3 * 4096;

    int base_width = paper_width - left_margin - right_margin;
    int page_height = paper_height - top_margin - bottom_margin;

    IGNORE(idx);		       /* FIXME */

    /*
     * First, set up some font structures.
     */
    fontlist = mknew(font_list);
    fontlist->head = fontlist->tail = NULL;
    tr = make_std_font(fontlist, "Times-Roman");
    ti = make_std_font(fontlist, "Times-Italic");
    hr = make_std_font(fontlist, "Helvetica-Bold");
    hi = make_std_font(fontlist, "Helvetica-BoldOblique");
    cr = make_std_font(fontlist, "Courier");
    co = make_std_font(fontlist, "Courier-Oblique");
    cb = make_std_font(fontlist, "Courier-Bold");

    /*
     * Go through and break up each paragraph into lines.
     */
    indent = 0;
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
	    indent += indent_list; break;
	  case para_LcontPop:
	    indent -= indent_list; assert(indent >= 0); break;
	  case para_QuotePush:
	    indent += indent_quote; break;
	  case para_QuotePop:
	    indent -= indent_quote; assert(indent >= 0); break;

	    /*
	     * This paragraph type is special. Process it
	     * specially.
	     */
	  case para_Code:
	    pdata = mknew(para_data);
	    code_paragraph(pdata, cr, co, cb, 12, indent, p->words);
	    p->private_data = pdata;
	    if (pdata->first != pdata->last) {
		pdata->first->penalty_after += 100000;
		pdata->last->penalty_before += 100000;
	    }
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
	    pdata = mknew(para_data);

	    /*
	     * Choose fonts for this paragraph.
	     * 
	     * FIXME: All of this ought to be completely
	     * user-configurable.
	     */
	    switch (p->type) {
	      case para_Title:
		pdata->fonts[FONT_NORMAL] = hr;
		pdata->sizes[FONT_NORMAL] = 24;
		pdata->fonts[FONT_EMPH] = hi;
		pdata->sizes[FONT_EMPH] = 24;
		pdata->fonts[FONT_CODE] = cb;
		pdata->sizes[FONT_CODE] = 24;
		break;

	      case para_Chapter:
	      case para_Appendix:
	      case para_UnnumberedChapter:
		pdata->fonts[FONT_NORMAL] = hr;
		pdata->sizes[FONT_NORMAL] = 20;
		pdata->fonts[FONT_EMPH] = hi;
		pdata->sizes[FONT_EMPH] = 20;
		pdata->fonts[FONT_CODE] = cb;
		pdata->sizes[FONT_CODE] = 20;
		break;

	      case para_Heading:
	      case para_Subsect:
		pdata->fonts[FONT_NORMAL] = hr;
		pdata->fonts[FONT_EMPH] = hi;
		pdata->fonts[FONT_CODE] = cb;
		pdata->sizes[FONT_NORMAL] =
		    pdata->sizes[FONT_EMPH] =
		    pdata->sizes[FONT_CODE] =
		    (p->aux == 0 ? 16 : p->aux == 1 ? 14 : 13);
		break;

	      case para_Normal:
	      case para_BiblioCited:
	      case para_Bullet:
	      case para_NumberedList:
	      case para_DescribedThing:
	      case para_Description:
	      case para_Copyright:
		pdata->fonts[FONT_NORMAL] = tr;
		pdata->sizes[FONT_NORMAL] = 12;
		pdata->fonts[FONT_EMPH] = ti;
		pdata->sizes[FONT_EMPH] = 12;
		pdata->fonts[FONT_CODE] = cr;
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
	    if (p->type == para_Bullet ||
		p->type == para_NumberedList ||
		p->type == para_Description) {
		extra_indent = firstline_indent = indent_list;
	    } else {
		extra_indent = firstline_indent = 0;
	    }

	    /*
	     * Find the auxiliary text for this paragraph.
	     */
	    aux = aux2 = NULL;
	    aux_indent = 0;

	    switch (p->type) {
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
		if (p->type == para_Heading || p->type == para_Subsect) {
		    int len;

		    aux = p->kwtext2;
		    len = paper_width_simple(pdata, p->kwtext2);
		    aux_indent = -len - sect_num_left_space;
		} else {
		    aux = p->kwtext;
		    aux2 = mknew(word);
		    aux2->next = NULL;
		    aux2->alt = NULL;
		    aux2->type = word_Normal;
		    aux2->text = ustrdup(L": ");
		    aux2->breaks = FALSE;
		    aux2->aux = 0;
		    aux_indent = 0;

		    firstline_indent += paper_width_simple(pdata, aux);
		    firstline_indent += paper_width_simple(pdata, aux2);
		}
		break;

	      case para_Bullet:
		/*
		 * Auxiliary text consisting of a bullet. (FIXME:
		 * configurable bullet.)
		 */
		aux = mknew(word);
		aux->next = NULL;
		aux->alt = NULL;
		aux->type = word_Normal;
		aux->text = ustrdup(L"\x2022");
		aux->breaks = FALSE;
		aux->aux = 0;
		aux_indent = indent + indent_list_bullet;
		break;

	      case para_NumberedList:
		/*
		 * Auxiliary text consisting of the number followed
		 * by a (FIXME: configurable) full stop.
		 */
		aux = p->kwtext;
		aux2 = mknew(word);
		aux2->next = NULL;
		aux2->alt = NULL;
		aux2->type = word_Normal;
		aux2->text = ustrdup(L".");
		aux2->breaks = FALSE;
		aux2->aux = 0;
		aux_indent = indent + indent_list_bullet;
		break;

	      case para_BiblioCited:
		/*
		 * Auxiliary text consisting of the bibliography
		 * reference text, and a trailing space.
		 */
		aux = p->kwtext;
		aux2 = mknew(word);
		aux2->next = NULL;
		aux2->alt = NULL;
		aux2->type = word_Normal;
		aux2->text = ustrdup(L" ");
		aux2->breaks = FALSE;
		aux2->aux = 0;
		aux_indent = indent;
		firstline_indent += paper_width_simple(pdata, aux);
		firstline_indent += paper_width_simple(pdata, aux2);
		break;
	    }

	    wrap_paragraph(pdata, p->words, base_width,
			   indent + firstline_indent,
			   indent + extra_indent);

	    p->private_data = pdata;

	    pdata->first->aux_text = aux;
	    pdata->first->aux_text_2 = aux2;
	    pdata->first->aux_left_indent = aux_indent;

	    /*
	     * Line breaking penalties.
	     */
	    switch (p->type) {
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

	    break;
	}

	if (p->private_data) {
	    pdata = (para_data *)p->private_data;

	    /*
	     * Set the line spacing for each line in this paragraph.
	     */
	    for (ldata = pdata->first; ldata; ldata = ldata->next) {
		if (ldata == pdata->first)
		    ldata->space_before = base_para_spacing / 2;
		else
		    ldata->space_before = base_leading / 2;
		if (ldata == pdata->last)
		    ldata->space_after = base_para_spacing / 2;
		else
		    ldata->space_after = base_leading / 2;
		ldata->page_break = FALSE;
	    }

	    /*
	     * Some kinds of section heading do require a page
	     * break before them.
	     */
	    if (p->type == para_Title ||
		p->type == para_Chapter ||
		p->type == para_Appendix ||
		p->type == para_UnnumberedChapter) {
		pdata->first->page_break = TRUE;
		pdata->first->space_before = chapter_top_space;
		pdata->last->space_after +=
		    chapter_underline_depth + chapter_underline_thickness;
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
	    }
	}
    }

    /*
     * Now we have an enormous linked list of every line of text in
     * the document. Break it up into pages.
     */
    pages = page_breaks(firstline, lastline, page_height);

    /*
     * Now we're ready to actually lay out the pages. We do this by
     * looping over _paragraphs_, since we may need to track cross-
     * references between lines and even across pages.
     */
    for (p = sourceform; p; p = p->next) {
	pdata = (para_data *)p->private_data;

	if (pdata) {
	    xref_dest dest;
	    dest.type = NONE;
	    for (ldata = pdata->first; ldata; ldata = ldata->next) {
		render_line(ldata, left_margin, paper_height - top_margin,
			    &dest, keywords);
		if (ldata == pdata->last)
		    break;
	    }

	    /*
	     * Some section headings (FIXME: should be configurable
	     * which) want to be underlined.
	     */
	    if (p->type == para_Chapter || p->type == para_Appendix ||
		p->type == para_UnnumberedChapter || p->type == para_Title) {
		add_rect_to_page(pdata->last->page,
				 left_margin,
				 (paper_height - top_margin -
				  pdata->last->ypos - chapter_underline_depth),
				 base_width,
				 chapter_underline_thickness);
	    }
	}
    }

    doc = mknew(document);
    doc->fonts = fontlist;
    doc->pages = pages;
    doc->paper_width = paper_width;
    doc->paper_height = paper_height;
    return doc;
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
			      int page_height)
{
    line_data *l, *m;
    page_data *ph, *pt;

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
     * Since my line_data structures are only used for this
     * purpose, I might as well just store the algorithm data
     * directly in them.
     */

    for (l = last; l; l = l->prev) {
	int minheight, text = 0, space = 0;
	int cost;

	l->bestcost = -1;
	for (m = l; m; m = m->next) {
	    if (m != l && m->page_break)
		break;		       /* we've gone as far as we can */

	    if (m != l)
		space += m->prev->space_after;
	    if (m != l || m->page_break)
		space += m->space_before;
	    text += m->line_height;
	    minheight = text + space;

	    if (m != l && minheight > page_height)
		break;

	    /*
	     * Compute the cost of this arrangement, as the square
	     * of the amount of wasted space on the page.
	     * Exception: if this is the last page before a
	     * mandatory break or the document end, we don't
	     * penalise a large blank area.
	     */
	    if (m->next && !m->next->page_break)
	    {
		int x = page_height - minheight;
		int xf;

		xf = x & 0xFF;
		x >>= 8;

		cost = x*x;
		cost += (x * xf) >> 8;
	    } else
		cost = 0;

	    if (m->next && !m->next->page_break) {
		cost += m->penalty_after;
		cost += m->next->penalty_before;
	    }

	    if (m->next && !m->next->page_break)
		cost += m->next->bestcost;
	    if (l->bestcost == -1 || l->bestcost > cost) {
		/*
		 * This is the best option yet for this starting
		 * point.
		 */
		l->bestcost = cost;
		if (m->next && !m->next->page_break)
		    l->vshortfall = page_height - minheight;
		else
		    l->vshortfall = 0;
		l->text = text;
		l->space = space;
		l->page_last = m;
	    }
	}
    }

    /*
     * Now go through the line list forwards and assemble the
     * actual pages.
     */
    ph = pt = NULL;

    l = first;
    while (l) {
	page_data *page;
	int text, space;

	page = mknew(page_data);
	page->next = NULL;
	page->prev = pt;
	if (pt)
	    pt->next = page;
	else
	    ph = page;
	pt = page;

	page->first_line = l;
	page->last_line = l->page_last;

	page->first_text = page->last_text = NULL;
	page->first_xref = page->last_xref = NULL;
	page->first_rect = page->last_rect = NULL;

	/*
	 * Now assign a y-coordinate to each line on the page.
	 */
	text = space = 0;
	for (l = page->first_line; l; l = l->next) {
	    if (l != page->first_line)
		space += l->prev->space_after;
	    if (l != page->first_line || l->page_break)
		space += l->space_before;
	    text += l->line_height;

	    l->page = page;
	    l->ypos = text + space +
		space * (float)page->first_line->vshortfall /
		page->first_line->space;

	    if (l == page->last_line)
		break;
	}

	l = page->last_line->next;
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
			       font_encoding *fe, int size, char *text)
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

	if (glyph == 0xFFFF)
	    continue;		       /* nothing more we can do here */

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
		add_string_to_page(page, x, y, subfont, fontsize, text);
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
	add_string_to_page(page, x, y, subfont, fontsize, text);
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
		       keywordlist *keywords)
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

	    if (text->type == word_HyperLink) {
		dest.type = URL;
		dest.url = utoa_dup(text->text);
		dest.page = NULL;
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

	  case word_IndexRef:
	    goto nextword;
	    /*
	     * FIXME: we should do something with this.
	     */
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
			    xr, shortfall, nspaces, nspace, keywords);
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

static void render_line(line_data *ldata, int left_x, int top_y,
			xref_dest *dest, keywordlist *keywords)
{
    int nspace;
    xref *xr;
    
    if (ldata->aux_text) {
	int x;
	xr = NULL;
	nspace = 0;
	x = render_text(ldata->page, ldata->pdata, ldata,
			left_x + ldata->aux_left_indent,
			top_y - ldata->ypos,
			ldata->aux_text, NULL, &xr, 0, 0, &nspace, keywords);
	if (ldata->aux_text_2)
	    render_text(ldata->page, ldata->pdata, ldata,
			x, top_y - ldata->ypos,
			ldata->aux_text_2, NULL, &xr, 0, 0, &nspace, keywords);
    }
    nspace = 0;

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

    render_text(ldata->page, ldata->pdata, ldata, left_x + ldata->xpos,
		top_y - ldata->ypos, ldata->first, ldata->end, &xr,
		ldata->hshortfall, ldata->nspaces, &nspace, keywords);

    if (xr) {
	/*
	 * There's a cross-reference continued on to the next line.
	 */
	*dest = xr->dest;
    } else
	dest->type = NONE;
}

static void code_paragraph(para_data *pdata,
			   font_data *fn, font_data *fi, font_data *fb,
			   int font_size, int indent, word *words)
{
    /*
     * For code paragraphs, I'm going to hack grievously and
     * pretend the three normal fonts are the three code paragraph
     * fonts.
     */
    pdata->fonts[FONT_NORMAL] = fb;
    pdata->fonts[FONT_EMPH] = fi;
    pdata->fonts[FONT_CODE] = fn;
    pdata->sizes[FONT_NORMAL] =
	pdata->sizes[FONT_EMPH] =
	pdata->sizes[FONT_CODE] = font_size;

    pdata->first = pdata->last = NULL;

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
	ldata->line_height = font_size * 4096;

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
}
