/*
 * Windows Help backend for Buttress
 * 
 * TODO:
 * 
 *  - rules
 *  - indexing
 *  - allow user to specify section contexts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "buttress.h"
#include "winhelp.h"

struct bk_whlp_state {
    WHLP h;
    indexdata *idx;
    keywordlist *keywords;
    WHLP_TOPIC curr_topic;
};

static void whlp_rdaddwc(rdstringc *rs, word *text);
static int whlp_convert(wchar_t *s, char **result, int hard_spaces);
static void whlp_mkparagraph(struct bk_whlp_state *state,
			     int font, word *text, int subsidiary);
static void whlp_navmenu(struct bk_whlp_state *state, paragraph *p);
    
void whlp_backend(paragraph *sourceform, keywordlist *keywords,
		  indexdata *idx) {
    WHLP h;
    char *filename;
    paragraph *p, *lastsect;
    struct bk_whlp_state state;
    WHLP_TOPIC contents_topic;
    int i;
    indexentry *ie;

    filename = "output.hlp";	       /* FIXME: configurability */
    
    h = state.h = whlp_new();
    state.keywords = keywords;
    state.idx = idx;

    whlp_start_macro(h, "CB(\"btn_about\",\"&About\",\"About()\")");
    whlp_start_macro(h, "CB(\"btn_up\",\"&Up\",\"Contents()\")");
    whlp_start_macro(h, "BrowseButtons()");

    /*
     * Loop over the source form registering WHLP_TOPICs for
     * everything.
     */

    contents_topic = whlp_register_topic(h, "Top", NULL);
    whlp_primary_topic(h, contents_topic);
    for (p = sourceform; p; p = p->next) {
	if (p->type == para_Chapter ||
	    p->type == para_Appendix ||
	    p->type == para_UnnumberedChapter ||
	    p->type == para_Heading ||
	    p->type == para_Subsect) {
	    p->private_data = whlp_register_topic(h, NULL, NULL);
	}
    }

    /*
     * Loop over the index entries, preparing final text forms for
     * each one.
     */
    for (i = 0; (ie = index234(idx->entries, i)) != NULL; i++) {
	rdstringc rs = {0, 0, NULL};
	whlp_rdaddwc(&rs, ie->text);
	ie->backend_data = rs.text;
    }

    whlp_prepare(h);

    /* ------------------------------------------------------------------
     * Do the contents page, containing title, preamble and
     * copyright.
     */

    whlp_begin_topic(h, contents_topic, "Contents", "DB(\"btn_up\")", NULL);

    /*
     * The manual title goes in the non-scroll region, and also
     * goes into the system title slot.
     */
    {
	rdstringc rs = {0, 0, NULL};
	for (p = sourceform; p; p = p->next) {
	    if (p->type == para_Title) {
		whlp_begin_para(h, WHLP_PARA_NONSCROLL);
		whlp_mkparagraph(&state, WHLP_FONT_TITLE, p->words, FALSE);
		whlp_rdaddwc(&rs, p->words);
		whlp_end_para(h);
	    }
	}
	if (rs.text) {
	    whlp_title(h, rs.text);
	    sfree(rs.text);
	}
    }

    /*
     * Next comes the preamble, which just goes into the ordinary
     * scrolling region.
     */
    for (p = sourceform; p; p = p->next) {
	if (p->type == para_Preamble) {
	    whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);
	    whlp_begin_para(h, WHLP_PARA_SCROLL);
	    whlp_mkparagraph(&state, WHLP_FONT_NORMAL, p->words, FALSE);
	    whlp_end_para(h);
	}
    }

    /*
     * The copyright goes to two places, again: into the contents
     * page and also into the system section.
     */
    {
	rdstringc rs = {0, 0, NULL};
	for (p = sourceform; p; p = p->next) {
	    if (p->type == para_Copyright) {
		whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);
		whlp_begin_para(h, WHLP_PARA_SCROLL);
		whlp_mkparagraph(&state, WHLP_FONT_NORMAL, p->words, FALSE);
		whlp_end_para(h);
		whlp_rdaddwc(&rs, p->words);
	    }
	}
	if (rs.text) {
	    whlp_copyright(h, rs.text);
	    sfree(rs.text);
	}
    }

    /*
     * Now do the primary navigation menu.
     */
    for (p = sourceform; p; p = p->next) {
	if (p->type == para_Chapter ||
	    p->type == para_Appendix ||
	    p->type == para_UnnumberedChapter)
	    whlp_navmenu(&state, p);
    }

    state.curr_topic = contents_topic;
    lastsect = NULL;

    /* ------------------------------------------------------------------
     * Now we've done the contents page, we're ready to go through
     * and do the main manual text. Ooh.
     */
    for (p = sourceform; p; p = p->next) switch (p->type) {
	/*
	 * Things we ignore because we've already processed them or
	 * aren't going to touch them in this pass.
	 */
      case para_IM:
      case para_BR:
      case para_Biblio:		       /* only touch BiblioCited */
      case para_VersionID:
      case para_Copyright:
      case para_Preamble:
      case para_NoCite:
      case para_Title:
	break;

	/*
	 * Chapter and section titles: start a new Help topic.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
      case para_Heading:
      case para_Subsect:
	if (lastsect && lastsect->child) {
	    paragraph *q;
	    /*
	     * Do a navigation menu for the previous section we
	     * were in.
	     */
	    for (q = lastsect->child; q; q = q->sibling)
		whlp_navmenu(&state, q);
	}
	{
	    rdstringc rs = {0, 0, NULL};
	    WHLP_TOPIC new_topic, parent_topic;
	    char *macro, *topicid;

	    new_topic = p->private_data;
	    whlp_browse_link(h, state.curr_topic, new_topic);
	    state.curr_topic = new_topic;

	    if (p->kwtext) {
		whlp_rdaddwc(&rs, p->kwtext);
		rdaddsc(&rs, ": ");    /* FIXME: configurability */
	    }
	    whlp_rdaddwc(&rs, p->words);
	    if (p->parent == NULL)
		parent_topic = contents_topic;
	    else
		parent_topic = (WHLP_TOPIC)p->parent->private_data;
	    topicid = whlp_topic_id(parent_topic);
	    macro = smalloc(100+strlen(topicid));
	    sprintf(macro,
		    "CBB(\"btn_up\",\"JI(`',`%s')\");EB(\"btn_up\")",
		    topicid);
	    whlp_begin_topic(h, new_topic,
			     rs.text ? rs.text : "",
			     macro, NULL);
	    sfree(macro);
	    sfree(rs.text);

	    whlp_begin_para(h, WHLP_PARA_NONSCROLL);
	    if (p->kwtext) {
		whlp_mkparagraph(&state, WHLP_FONT_TITLE, p->kwtext, FALSE);
		whlp_set_font(h, WHLP_FONT_TITLE);
		whlp_text(h, ": ");    /* FIXME: configurability */
	    }
	    whlp_mkparagraph(&state, WHLP_FONT_TITLE, p->words, FALSE);
	    whlp_end_para(h);

	    lastsect = p;
	}
	break;

      case para_Rule:
	/* FIXME: what do we do about rules? */
	break;

      case para_Normal:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
	whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);
	if (p->type == para_Bullet || p->type == para_NumberedList) {
	    whlp_para_attr(h, WHLP_PARA_LEFTINDENT, 72);
	    whlp_para_attr(h, WHLP_PARA_FIRSTLINEINDENT, -36);
	    whlp_set_tabstop(h, 72, WHLP_ALIGN_LEFT);
	    whlp_begin_para(h, WHLP_PARA_SCROLL);
	    if (p->type == para_Bullet) {
		whlp_text(h, "\x95");
	    } else {
		whlp_mkparagraph(&state, WHLP_FONT_NORMAL, p->kwtext, FALSE);
		whlp_text(h, ".");
	    }
	    whlp_tab(h);
	} else {
	    whlp_begin_para(h, WHLP_PARA_SCROLL);
	}

	if (p->type == para_BiblioCited) {
	    whlp_mkparagraph(&state, WHLP_FONT_NORMAL, p->kwtext, FALSE);
	    whlp_text(h, " ");
	}

	whlp_mkparagraph(&state, WHLP_FONT_NORMAL, p->words, FALSE);
	whlp_end_para(h);
	break;

      case para_Code:
	/*
	 * In a code paragraph, each individual word is a line. For
	 * Help files, we will have to output this as a set of
	 * paragraphs, all but the last of which don't set
	 * SPACEBELOW.
	 */
	{
	    word *w;
	    char *c;
	    for (w = p->words; w; w = w->next) {
		if (!w->next)
		    whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);
		whlp_begin_para(h, WHLP_PARA_SCROLL);
		whlp_set_font(h, WHLP_FONT_FIXED);
		whlp_convert(w->text, &c, FALSE);
		whlp_text(h, c);
		sfree(c);
		whlp_end_para(h);
	    }
	}
	break;
    }

    whlp_close(h, filename);


    /*
     * Loop over the index entries, cleaning up our final text
     * forms.
     */
    for (i = 0; (ie = index234(idx->entries, i)) != NULL; i++) {
	sfree(ie->backend_data);
    }
}

static void whlp_navmenu(struct bk_whlp_state *state, paragraph *p) {
    whlp_begin_para(state->h, WHLP_PARA_NONSCROLL);
    whlp_start_hyperlink(state->h, (WHLP_TOPIC)p->private_data);
    if (p->kwtext) {
	whlp_mkparagraph(state, WHLP_FONT_NORMAL, p->kwtext, TRUE);
	whlp_set_font(state->h, WHLP_FONT_NORMAL);
	whlp_text(state->h, ": ");    /* FIXME: configurability */
    }
    whlp_mkparagraph(state, WHLP_FONT_NORMAL, p->words, TRUE);
    whlp_end_hyperlink(state->h);
    whlp_end_para(state->h);

}

static void whlp_mkparagraph(struct bk_whlp_state *state,
			     int font, word *text, int subsidiary) {
    keyword *kwl;
    int deffont = font;
    int currfont = -1;
    int newfont;
    char *c;
    paragraph *xref_target = NULL;

    for (; text; text = text->next) switch (text->type) {
      case word_HyperLink:
      case word_HyperEnd:
	break;

      case word_IndexRef:
	if (subsidiary) break;	       /* disabled in subsidiary bits */
	{
	    indextag *tag = index_findtag(state->idx, text->text);
	    int i;
	    if (!tag)
		break;
	    for (i = 0; i < tag->nrefs; i++)
		whlp_index_term(state->h, tag->refs[i]->backend_data,
				state->curr_topic);
	}
	break;

      case word_UpperXref:
      case word_LowerXref:
	if (subsidiary) break;	       /* disabled in subsidiary bits */
        kwl = kw_lookup(state->keywords, text->text);
	assert(xref_target == NULL);
	if (kwl->para->type == para_NumberedList) {
	    break;		       /* don't xref to numbered list items */
	} else if (kwl->para->type == para_BiblioCited) {
	    /*
	     * An xref to a bibliography item jumps to the section
	     * containing it.
	     */
	    if (kwl->para->parent)
		xref_target = kwl->para->parent;
	    else
		break;
	} else {
	    xref_target = kwl->para;
	}
	whlp_start_hyperlink(state->h, (WHLP_TOPIC)xref_target->private_data);
	break;

      case word_XrefEnd:
	if (subsidiary) break;	       /* disabled in subsidiary bits */
	if (xref_target)
	    whlp_end_hyperlink(state->h);
	xref_target = NULL;
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
	if (towordstyle(text->type) == word_Emph)
	    newfont = WHLP_FONT_ITALIC;
	else if (towordstyle(text->type) == word_Code ||
		 towordstyle(text->type) == word_WeakCode)
	    newfont = WHLP_FONT_FIXED;
	else
	    newfont = deffont;
	if (newfont != currfont) {
	    currfont = newfont;
	    whlp_set_font(state->h, newfont);
	}
	if (removeattr(text->type) == word_Normal) {
	    if (whlp_convert(text->text, &c, TRUE))
		whlp_text(state->h, c);
	    else
		whlp_mkparagraph(state, deffont, text->alt, FALSE);
	    sfree(c);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    whlp_text(state->h, " ");
	} else if (removeattr(text->type) == word_Quote) {
	    whlp_text(state->h,
		      quoteaux(text->aux) == quote_Open ? "\x91" : "\x92");
				       /* FIXME: configurability */
	}
	break;
    }
}

static void whlp_rdaddwc(rdstringc *rs, word *text) {
    char *c;

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
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_First ||
		  attraux(text->aux) == attr_Only))
	    rdaddc(rs, '\x91');	       /* FIXME: configurability */
	if (removeattr(text->type) == word_Normal) {
	    if (whlp_convert(text->text, &c, FALSE))
		rdaddsc(rs, c);
	    else
		whlp_rdaddwc(rs, text->alt);
	    sfree(c);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    rdaddc(rs, ' ');
	} else if (removeattr(text->type) == word_Quote) {
	    rdaddc(rs, quoteaux(text->aux) == quote_Open ? '\x91' : '\x92');
				       /* FIXME: configurability */
	}
	if (towordstyle(text->type) == word_Emph &&
	    (attraux(text->aux) == attr_Last ||
	     attraux(text->aux) == attr_Only))
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_Last ||
		  attraux(text->aux) == attr_Only))
	    rdaddc(rs, '\x92');	       /* FIXME: configurability */
	break;
    }
}

/*
 * Convert a wide string into a string of chars. If `result' is
 * non-NULL, mallocs the resulting string and stores a pointer to
 * it in `*result'. If `result' is NULL, merely checks whether all
 * characters in the string are feasible for the output character
 * set.
 *
 * Return is nonzero if all characters are OK. If not all
 * characters are OK but `result' is non-NULL, a result _will_
 * still be generated!
 */
static int whlp_convert(wchar_t *s, char **result, int hard_spaces) {
    /*
     * FIXME. Currently this is ISO8859-1 only.
     */
    int doing = (result != 0);
    int ok = TRUE;
    char *p = NULL;
    int plen = 0, psize = 0;

    for (; *s; s++) {
	wchar_t c = *s;
	char outc;

	if ((c >= 32 && c <= 126) ||
	    (c >= 160 && c <= 255)) {
	    /* Char is OK. */
	    if (c == 32 && hard_spaces)
		outc = '\240';
	    else
		outc = (char)c;
	} else {
	    /* Char is not OK. */
	    ok = FALSE;
	    outc = 0xBF;	       /* approximate the good old DEC `uh?' */
	}
	if (doing) {
	    if (plen >= psize) {
		psize = plen + 256;
		p = resize(p, psize);
	    }
	    p[plen++] = outc;
	}
    }
    if (doing) {
	p = resize(p, plen+1);
	p[plen] = '\0';
	*result = p;
    }
    return ok;
}
