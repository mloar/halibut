/*
 * Windows Help backend for Halibut
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "halibut.h"
#include "winhelp.h"

struct bk_whlp_state {
    WHLP h;
    indexdata *idx;
    keywordlist *keywords;
    WHLP_TOPIC curr_topic;
    int charset;
    charset_state cstate;
    FILE *cntfp;
    int cnt_last_level, cnt_workaround;
};

typedef struct {
    int charset;
    wchar_t *bullet, *lquote, *rquote, *titlepage, *sectsuffix, *listsuffix;
    wchar_t *contents_text;
    char *filename;
} whlpconf;

/*
 * Indexes of fonts in our standard font descriptor set.
 */
enum {
    FONT_NORMAL,
    FONT_EMPH,
    FONT_CODE,
    FONT_ITAL_CODE,
    FONT_BOLD_CODE,
    FONT_TITLE,
    FONT_TITLE_EMPH,
    FONT_TITLE_CODE,
    FONT_RULE
};

static void whlp_rdaddwc(rdstringc *rs, word *text, whlpconf *conf,
			 charset_state *state);
static void whlp_rdadds(rdstringc *rs, const wchar_t *text, whlpconf *conf,
			charset_state *state);
static void whlp_mkparagraph(struct bk_whlp_state *state,
			     int font, word *text, int subsidiary,
			     whlpconf *conf);
static void whlp_navmenu(struct bk_whlp_state *state, paragraph *p,
			 whlpconf *conf);
static void whlp_contents_write(struct bk_whlp_state *state,
				int level, char *text, WHLP_TOPIC topic);
static void whlp_wtext(struct bk_whlp_state *state, const wchar_t *text);
    
paragraph *whlp_config_filename(char *filename)
{
    return cmdline_cfg_simple("winhelp-filename", filename, NULL);
}

static whlpconf whlp_configure(paragraph *source) {
    paragraph *p;
    whlpconf ret;

    /*
     * Defaults.
     */
    ret.charset = CS_CP1252;
    ret.bullet = L"\x2022\0-\0\0";
    ret.lquote = L"\x2018\0\x2019\0\"\0\"\0\0";
    ret.rquote = uadv(ret.lquote);
    ret.filename = dupstr("output.hlp");
    ret.titlepage = L"Title page";
    ret.contents_text = L"Contents";
    ret.sectsuffix = L": ";
    ret.listsuffix = L".";

    /*
     * Two-pass configuration so that we can pick up global config
     * (e.g. `quotes') before having it overridden by specific
     * config (`win-quotes'), irrespective of the order in which
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
	    /*
	     * In principle we should support a `winhelp-charset'
	     * here. We don't, because my WinHelp output code
	     * doesn't know how to change character set. Once I
	     * find out, I'll support it.
	     */
	    if (p->parent && !ustricmp(p->keyword, L"winhelp-topic")) {
		/* Store the topic name in the private_data field of the
		 * containing section. */
		p->parent->private_data = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"winhelp-filename")) {
		sfree(ret.filename);
		ret.filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(p->keyword, L"winhelp-bullet")) {
		ret.bullet = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"winhelp-section-suffix")) {
		ret.sectsuffix = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"winhelp-list-suffix")) {
		ret.listsuffix = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"winhelp-contents-titlepage")) {
		ret.titlepage = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"winhelp-quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    } else if (!ustricmp(p->keyword, L"contents")) {
		ret.contents_text = uadv(p->keyword);
	    }
	}
    }

    /*
     * Now process fallbacks on quote characters and bullets.
     */
    while (*uadv(ret.rquote) && *uadv(uadv(ret.rquote)) &&
	   (!cvt_ok(ret.charset, ret.lquote) ||
	    !cvt_ok(ret.charset, ret.rquote))) {
	ret.lquote = uadv(ret.rquote);
	ret.rquote = uadv(ret.lquote);
    }

    while (*ret.bullet && *uadv(ret.bullet) &&
	   !cvt_ok(ret.charset, ret.bullet))
	ret.bullet = uadv(ret.bullet);

    return ret;
}

void whlp_backend(paragraph *sourceform, keywordlist *keywords,
		  indexdata *idx, void *unused) {
    WHLP h;
    char *cntname;
    paragraph *p, *lastsect;
    struct bk_whlp_state state;
    WHLP_TOPIC contents_topic;
    int i;
    int nesting;
    indexentry *ie;
    int done_contents_topic = FALSE;
    whlpconf conf;

    IGNORE(unused);

    h = state.h = whlp_new();
    state.keywords = keywords;
    state.idx = idx;

    whlp_start_macro(h, "CB(\"btn_about\",\"&About\",\"About()\")");
    whlp_start_macro(h, "CB(\"btn_up\",\"&Up\",\"Contents()\")");
    whlp_start_macro(h, "BrowseButtons()");

    whlp_create_font(h, "Times New Roman", WHLP_FONTFAM_SERIF, 24,
		     0, 0, 0, 0);
    whlp_create_font(h, "Times New Roman", WHLP_FONTFAM_SERIF, 24,
		     WHLP_FONT_ITALIC, 0, 0, 0);
    whlp_create_font(h, "Courier New", WHLP_FONTFAM_FIXED, 24,
		     0, 0, 0, 0);
    whlp_create_font(h, "Courier New", WHLP_FONTFAM_FIXED, 24,
		     WHLP_FONT_ITALIC, 0, 0, 0);
    whlp_create_font(h, "Courier New", WHLP_FONTFAM_FIXED, 24,
		     WHLP_FONT_BOLD, 0, 0, 0);
    whlp_create_font(h, "Arial", WHLP_FONTFAM_SERIF, 30,
		     WHLP_FONT_BOLD, 0, 0, 0);
    whlp_create_font(h, "Arial", WHLP_FONTFAM_SERIF, 30,
		     WHLP_FONT_BOLD|WHLP_FONT_ITALIC, 0, 0, 0);
    whlp_create_font(h, "Courier New", WHLP_FONTFAM_FIXED, 30,
		     WHLP_FONT_BOLD, 0, 0, 0);
    whlp_create_font(h, "Courier New", WHLP_FONTFAM_SANS, 18,
		     WHLP_FONT_STRIKEOUT, 0, 0, 0);

    conf = whlp_configure(sourceform);

    state.charset = conf.charset;

    /*
     * Ensure the output file name has a .hlp extension. This is
     * required since we must create the .cnt file in parallel with
     * it.
     */
    {
	int len = strlen(conf.filename);
	if (len < 4 || conf.filename[len-4] != '.' ||
	    tolower(conf.filename[len-3] != 'h') ||
	    tolower(conf.filename[len-2] != 'l') ||
	    tolower(conf.filename[len-1] != 'p')) {
	    char *newf;
	    newf = snewn(len + 5, char);
	    sprintf(newf, "%s.hlp", conf.filename);
	    sfree(conf.filename);
	    conf.filename = newf;
	    len = strlen(newf);
	}
	cntname = snewn(len+1, char);
	sprintf(cntname, "%.*s.cnt", len-4, conf.filename);
    }

    state.cntfp = fopen(cntname, "wb");
    if (!state.cntfp) {
	error(err_cantopenw, cntname);
	return;
    }
    state.cnt_last_level = -1; state.cnt_workaround = 0;

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

	    rdstringc rs = { 0, 0, NULL };
	    char *errstr;

	    whlp_rdadds(&rs, (wchar_t *)p->private_data, &conf, NULL);

	    p->private_data = whlp_register_topic(h, rs.text, &errstr);
	    if (!p->private_data) {
		p->private_data = whlp_register_topic(h, NULL, NULL);
		error(err_winhelp_ctxclash, &p->fpos, rs.text, errstr);
	    }
	    sfree(rs.text);
	}
    }

    /*
     * Loop over the index entries, preparing final text forms for
     * each one.
     */
    {
	indexentry *ie_prev = NULL;
	int nspaces = 1;

	for (i = 0; (ie = index234(idx->entries, i)) != NULL; i++) {
	    rdstringc rs = {0, 0, NULL};
	    charset_state state = CHARSET_INIT_STATE;
	    whlp_rdaddwc(&rs, ie->text, &conf, &state);

	    if (ie_prev) {
		/*
		 * It appears that Windows Help's index mechanism
		 * is inherently case-insensitive. Therefore, if two
		 * adjacent index terms compare equal apart from
		 * case, I'm going to append nonbreaking spaces to
		 * the end of the second one so that Windows will
		 * treat them as distinct.
		 * 
		 * This is nasty because we're depending on our
		 * case-insensitive comparison having the same
		 * semantics as the Windows one :-/ but I see no
		 * alternative.
		 */
		wchar_t *a, *b;

		a = ufroma_dup((char *)ie_prev->backend_data, conf.charset);
		b = ufroma_dup(rs.text, conf.charset);
		if (!ustricmp(a, b)) {
		    int j;
		    for (j = 0; j < nspaces; j++)
			whlp_rdadds(&rs, L"\xA0", &conf, &state);
		    /*
		     * Add one to nspaces, so that if another term
		     * appears which is equivalent to the previous
		     * two it'll acquire one more space.
		     */
		    nspaces++;
		} else
		    nspaces = 1;
		sfree(a);
		sfree(b);
	    }

	    whlp_rdadds(&rs, NULL, &conf, &state);

	    ie->backend_data = rs.text;

	    /*
	     * Only move ie_prev on if nspaces==1 (since when we
	     * have three or more adjacent terms differing only in
	     * case, we will want to compare with the _first_ of
	     * them because that won't have had any extra spaces
	     * added on which will foul up the comparison).
	     */
	    if (nspaces == 1)
		ie_prev = ie;
	}
    }

    whlp_prepare(h);

    /* ------------------------------------------------------------------
     * Begin the contents page.
     */
    {
	rdstringc rs = {0, 0, NULL};
	whlp_rdadds(&rs, conf.contents_text, &conf, NULL);
	whlp_begin_topic(h, contents_topic, rs.text, "DB(\"btn_up\")", NULL);
	state.curr_topic = contents_topic;
	sfree(rs.text);
    }

    /*
     * The manual title goes in the non-scroll region, and also
     * goes into the system title slot.
     */
    {
	rdstringc rs = {0, 0, NULL};
	for (p = sourceform; p; p = p->next) {
	    if (p->type == para_Title) {
		whlp_begin_para(h, WHLP_PARA_NONSCROLL);
		state.cstate = charset_init_state;
		whlp_mkparagraph(&state, FONT_TITLE, p->words, FALSE, &conf);
		whlp_wtext(&state, NULL);
		whlp_end_para(h);
		whlp_rdaddwc(&rs, p->words, &conf, NULL);
	    }
	}
	if (rs.text) {
	    whlp_title(h, rs.text);
	    fprintf(state.cntfp, ":Title %s\r\n", rs.text);
	    sfree(rs.text);
	}
	{
	    rdstringc rs2 = {0,0,NULL};
	    whlp_rdadds(&rs2, conf.titlepage, &conf, NULL);
	    whlp_contents_write(&state, 1, rs2.text, contents_topic);
	    sfree(rs2.text);
	}
    }

    /*
     * Put the copyright into the system section.
     */
    {
	rdstringc rs = {0, 0, NULL};
	for (p = sourceform; p; p = p->next) {
	    if (p->type == para_Copyright)
		whlp_rdaddwc(&rs, p->words, &conf, NULL);
	}
	if (rs.text) {
	    whlp_copyright(h, rs.text);
	    sfree(rs.text);
	}
    }

    lastsect = NULL;

    /* ------------------------------------------------------------------
     * Now we've done the contents page, we're ready to go through
     * and do the main manual text. Ooh.
     */
    nesting = 0;
    for (p = sourceform; p; p = p->next) switch (p->type) {
	/*
	 * Things we ignore because we've already processed them or
	 * aren't going to touch them in this pass.
	 */
      case para_IM:
      case para_BR:
      case para_Biblio:		       /* only touch BiblioCited */
      case para_VersionID:
      case para_NoCite:
      case para_Title:
	break;

      case para_LcontPush:
      case para_QuotePush:
	nesting++;
	break;
      case para_LcontPop:
      case para_QuotePop:
	assert(nesting > 0);
	nesting--;
	break;

	/*
	 * Chapter and section titles: start a new Help topic.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
      case para_Heading:
      case para_Subsect:

	if (!done_contents_topic) {
	    paragraph *p;

	    /*
	     * If this is the first section title we've seen, then
	     * we're currently still in the contents topic. We
	     * should therefore finish up the contents page by
	     * writing a nav menu.
	     */
	    for (p = sourceform; p; p = p->next) {
		if (p->type == para_Chapter ||
		    p->type == para_Appendix ||
		    p->type == para_UnnumberedChapter)
		    whlp_navmenu(&state, p, &conf);
	    }

	    done_contents_topic = TRUE;
	}

	if (lastsect && lastsect->child) {
	    paragraph *q;
	    /*
	     * Do a navigation menu for the previous section we
	     * were in.
	     */
	    for (q = lastsect->child; q; q = q->sibling)
		whlp_navmenu(&state, q, &conf);
	}
	{
	    rdstringc rs = {0, 0, NULL};
	    WHLP_TOPIC new_topic, parent_topic;
	    char *macro, *topicid;
	    charset_state cstate = CHARSET_INIT_STATE;

	    new_topic = p->private_data;
	    whlp_browse_link(h, state.curr_topic, new_topic);
	    state.curr_topic = new_topic;

	    if (p->kwtext) {
		whlp_rdaddwc(&rs, p->kwtext, &conf, &cstate);
		whlp_rdadds(&rs, conf.sectsuffix, &conf, &cstate);
	    }
	    whlp_rdaddwc(&rs, p->words, &conf, &cstate);
	    whlp_rdadds(&rs, NULL, &conf, &cstate);

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

	    {
		/*
		 * Output the .cnt entry.
		 * 
		 * WinHelp has a bug involving having an internal
		 * node followed by a leaf at the same level: the
		 * leaf is output at the wrong level. We can mostly
		 * work around this by modifying the leaf level
		 * itself (see whlp_contents_write), but this
		 * doesn't work for top-level sections since we
		 * can't turn a level-1 leaf into a level-0 one. So
		 * for top-level leaf sections (Bibliography
		 * springs to mind), we output an internal node
		 * containing only the leaf for that section.
		 */
		int i;
		paragraph *q;

		/* Count up the level. */
		i = 1;
		for (q = p; q->parent; q = q->parent) i++;

		if (p->child || !p->parent) {
		    /*
		     * If p has children then it needs to be a
		     * folder; if it has no parent then it needs to
		     * be a folder to work around the bug.
		     */
		    whlp_contents_write(&state, i, rs.text, NULL);
		    i++;
		}
		whlp_contents_write(&state, i, rs.text, new_topic);
	    }

	    sfree(rs.text);

	    whlp_begin_para(h, WHLP_PARA_NONSCROLL);
	    state.cstate = charset_init_state;
	    if (p->kwtext) {
		whlp_mkparagraph(&state, FONT_TITLE, p->kwtext, FALSE, &conf);
		whlp_set_font(h, FONT_TITLE);
		whlp_wtext(&state, conf.sectsuffix);
	    }
	    whlp_mkparagraph(&state, FONT_TITLE, p->words, FALSE, &conf);
	    whlp_wtext(&state, NULL);
	    whlp_end_para(h);

	    lastsect = p;
	}
	break;

      case para_Rule:
	whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);
	whlp_para_attr(h, WHLP_PARA_ALIGNMENT, WHLP_ALIGN_CENTRE);
	whlp_begin_para(h, WHLP_PARA_SCROLL);
	whlp_set_font(h, FONT_RULE);
	state.cstate = charset_init_state;
#define TEN L"\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xA0\xA0"
#define TWENTY TEN TEN
#define FORTY TWENTY TWENTY
#define EIGHTY FORTY FORTY
	state.cstate = charset_init_state;
	whlp_wtext(&state, EIGHTY);
	whlp_wtext(&state, NULL);
#undef TEN
#undef TWENTY
#undef FORTY
#undef EIGHTY
	whlp_end_para(h);
	break;

      case para_Normal:
      case para_Copyright:
      case para_DescribedThing:
      case para_Description:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
	whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);
	if (p->type == para_Bullet || p->type == para_NumberedList) {
	    whlp_para_attr(h, WHLP_PARA_LEFTINDENT, 72*nesting + 72);
	    whlp_para_attr(h, WHLP_PARA_FIRSTLINEINDENT, -36);
	    whlp_set_tabstop(h, 72, WHLP_ALIGN_LEFT);
	    whlp_begin_para(h, WHLP_PARA_SCROLL);
	    whlp_set_font(h, FONT_NORMAL);
	    state.cstate = charset_init_state;
	    if (p->type == para_Bullet) {
		whlp_wtext(&state, conf.bullet);
	    } else {
		whlp_mkparagraph(&state, FONT_NORMAL, p->kwtext, FALSE, &conf);
		whlp_wtext(&state, conf.listsuffix);
	    }
	    whlp_wtext(&state, NULL);
	    whlp_tab(h);
	} else {
	    whlp_para_attr(h, WHLP_PARA_LEFTINDENT,
			   72*nesting + (p->type==para_Description ? 72 : 0));
	    whlp_begin_para(h, WHLP_PARA_SCROLL);
	}

	state.cstate = charset_init_state;

	if (p->type == para_BiblioCited) {
	    whlp_mkparagraph(&state, FONT_NORMAL, p->kwtext, FALSE, &conf);
	    whlp_wtext(&state, L" ");
	}

	whlp_mkparagraph(&state, FONT_NORMAL, p->words, FALSE, &conf);
	whlp_wtext(&state, NULL);
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
	    wchar_t *t, *e, *tmp;

	    for (w = p->words; w; w = w->next) if (w->type == word_WeakCode) {
		t = w->text;
		if (w->next && w->next->type == word_Emph) {
		    w = w->next;
		    e = w->text;
		} else
		    e = NULL;

		if (!w->next)
		    whlp_para_attr(h, WHLP_PARA_SPACEBELOW, 12);

		whlp_para_attr(h, WHLP_PARA_LEFTINDENT, 72*nesting);
		whlp_begin_para(h, WHLP_PARA_SCROLL);
		state.cstate = charset_init_state;
		while (e && *e && *t) {
		    int n;
		    int ec = *e;

		    for (n = 0; t[n] && e[n] && e[n] == ec; n++);
		    if (ec == 'i')
			whlp_set_font(h, FONT_ITAL_CODE);
		    else if (ec == 'b')
			whlp_set_font(h, FONT_BOLD_CODE);
		    else
			whlp_set_font(h, FONT_CODE);
		    tmp = snewn(n+1, wchar_t);
		    ustrncpy(tmp, t, n);
		    tmp[n] = L'\0';
		    whlp_wtext(&state, tmp);
		    whlp_wtext(&state, NULL);
		    state.cstate = charset_init_state;
		    sfree(tmp);
		    t += n;
		    e += n;
		}
		whlp_set_font(h, FONT_CODE);
		whlp_wtext(&state, t);
		whlp_wtext(&state, NULL);
		whlp_end_para(h);
	    }
	}
	break;
    }

    fclose(state.cntfp);
    whlp_close(h, conf.filename);

    /*
     * Loop over the index entries, cleaning up our final text
     * forms.
     */
    for (i = 0; (ie = index234(idx->entries, i)) != NULL; i++) {
	sfree(ie->backend_data);
    }

    sfree(conf.filename);
    sfree(cntname);
}

static void whlp_contents_write(struct bk_whlp_state *state,
				int level, char *text, WHLP_TOPIC topic) {
    /*
     * Horrifying bug in WinHelp. When dropping a section level or
     * more without using a folder-type entry, WinHelp accidentally
     * adds one to the section level. So we correct for that here.
     */
    if (state->cnt_last_level > level && topic)
	state->cnt_workaround = -1;
    else if (!topic)
	state->cnt_workaround = 0;
    state->cnt_last_level = level;

    fprintf(state->cntfp, "%d ", level + state->cnt_workaround);
    while (*text) {
	if (*text == '=')
	    fputc('\\', state->cntfp);
	fputc(*text, state->cntfp);
	text++;
    }
    if (topic)
	fprintf(state->cntfp, "=%s", whlp_topic_id(topic));
    fputc('\n', state->cntfp);
}

static void whlp_navmenu(struct bk_whlp_state *state, paragraph *p,
			 whlpconf *conf) {
    whlp_begin_para(state->h, WHLP_PARA_SCROLL);
    whlp_start_hyperlink(state->h, (WHLP_TOPIC)p->private_data);
    state->cstate = charset_init_state;
    if (p->kwtext) {
	whlp_mkparagraph(state, FONT_NORMAL, p->kwtext, TRUE, conf);
	whlp_set_font(state->h, FONT_NORMAL);
	whlp_wtext(state, conf->sectsuffix);
    }
    whlp_mkparagraph(state, FONT_NORMAL, p->words, TRUE, conf);
    whlp_wtext(state, NULL);
    whlp_end_hyperlink(state->h);
    whlp_end_para(state->h);

}

static void whlp_mkparagraph(struct bk_whlp_state *state,
			     int font, word *text, int subsidiary,
			     whlpconf *conf) {
    keyword *kwl;
    int deffont = font;
    int currfont = -1;
    int newfont;
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
        if (kwl) {
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
            whlp_start_hyperlink(state->h,
                                 (WHLP_TOPIC)xref_target->private_data);
        }
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
	    newfont = deffont + FONT_EMPH;
	else if (towordstyle(text->type) == word_Code ||
		 towordstyle(text->type) == word_WeakCode)
	    newfont = deffont + FONT_CODE;
	else
	    newfont = deffont;
	if (newfont != currfont) {
	    currfont = newfont;
	    whlp_set_font(state->h, newfont);
	}
	if (removeattr(text->type) == word_Normal) {
	    if (cvt_ok(conf->charset, text->text) || !text->alt)
		whlp_wtext(state, text->text);
	    else
		whlp_mkparagraph(state, deffont, text->alt, FALSE, conf);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    whlp_wtext(state, L" ");
	} else if (removeattr(text->type) == word_Quote) {
	    whlp_wtext(state,
		       quoteaux(text->aux) == quote_Open ?
		       conf->lquote : conf->rquote);
	}
	break;
    }
}

static void whlp_rdaddwc(rdstringc *rs, word *text, whlpconf *conf,
			 charset_state *state) {
    charset_state ourstate = CHARSET_INIT_STATE;

    if (!state)
	state = &ourstate;

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
	if (removeattr(text->type) == word_Normal) {
	    if (cvt_ok(conf->charset, text->text) || !text->alt)
		whlp_rdadds(rs, text->text, conf, state);
	    else
		whlp_rdaddwc(rs, text->alt, conf, state);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    whlp_rdadds(rs, L" ", conf, state);
	} else if (removeattr(text->type) == word_Quote) {
	    whlp_rdadds(rs, quoteaux(text->aux) == quote_Open ?
			conf->lquote : conf->rquote, conf, state);
	}
	break;
    }

    if (state == &ourstate)
	whlp_rdadds(rs, NULL, conf, state);
}

static void whlp_rdadds(rdstringc *rs, const wchar_t *text, whlpconf *conf,
			charset_state *state)
{
    charset_state ourstate = CHARSET_INIT_STATE;
    int textlen = text ? ustrlen(text) : 0;
    char outbuf[256];
    int ret;

    if (!state)
	state = &ourstate;

    while (textlen > 0 &&
	   (ret = charset_from_unicode(&text, &textlen, outbuf,
				       lenof(outbuf)-1,
				       conf->charset, state, NULL)) > 0) {
	outbuf[ret] = '\0';
	rdaddsc(rs, outbuf);
    }

    if (text == NULL || state == &ourstate) {
	if ((ret = charset_from_unicode(NULL, 0, outbuf, lenof(outbuf)-1,
					conf->charset, state, NULL)) > 0) {
	    outbuf[ret] = '\0';
	    rdaddsc(rs, outbuf);
	}
    }
}

static void whlp_wtext(struct bk_whlp_state *state, const wchar_t *text)
{
    int textlen = text ? ustrlen(text) : 0;
    char outbuf[256];
    int ret;

    while (textlen > 0 &&
	   (ret = charset_from_unicode(&text, &textlen, outbuf,
				       lenof(outbuf)-1,
				       state->charset, &state->cstate,
				       NULL)) > 0) {
	outbuf[ret] = '\0';
	whlp_text(state->h, outbuf);
    }

    if (text == NULL) {
	if ((ret = charset_from_unicode(NULL, 0, outbuf, lenof(outbuf)-1,
					state->charset, &state->cstate,
					NULL)) > 0) {
	    outbuf[ret] = '\0';
	    whlp_text(state->h, outbuf);
	}
    }
}
