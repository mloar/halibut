/*
 * text backend for Halibut
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "halibut.h"

typedef enum { LEFT, LEFTPLUS, CENTRE } alignment;
typedef struct {
    alignment align;
    int just_numbers;
    wchar_t *underline;
    wchar_t *number_suffix;
} alignstruct;

typedef struct {
    int indent, indent_code;
    int listindentbefore, listindentafter;
    int width;
    alignstruct atitle, achapter, *asect;
    int nasect;
    int include_version_id;
    int indent_preambles;
    int charset;
    word bullet;
    wchar_t *lquote, *rquote, *rule;
    char *filename;
    wchar_t *listsuffix, *startemph, *endemph;
} textconfig;

typedef struct {
    FILE *fp;
    int charset;
    charset_state state;
} textfile;

static void text_heading(textfile *, word *, word *, word *, alignstruct,
			 int, int, textconfig *);
static void text_rule(textfile *, int, int, textconfig *);
static void text_para(textfile *, word *, wchar_t *, word *, int, int, int,
		      textconfig *);
static void text_codepara(textfile *, word *, int, int);
static void text_versionid(textfile *, word *, textconfig *);

static void text_output(textfile *, const wchar_t *);
static void text_output_many(textfile *, int, wchar_t);

static alignment utoalign(wchar_t *p) {
    if (!ustricmp(p, L"centre") || !ustricmp(p, L"center"))
	return CENTRE;
    if (!ustricmp(p, L"leftplus"))
	return LEFTPLUS;
    return LEFT;
}

static textconfig text_configure(paragraph *source) {
    textconfig ret;
    paragraph *p;
    int n;

    /*
     * Non-negotiables.
     */
    ret.bullet.next = NULL;
    ret.bullet.alt = NULL;
    ret.bullet.type = word_Normal;
    ret.atitle.just_numbers = FALSE;   /* ignored */

    /*
     * Defaults.
     */
    ret.indent = 7;
    ret.indent_code = 2;
    ret.listindentbefore = 1;
    ret.listindentafter = 3;
    ret.width = 68;
    ret.atitle.align = CENTRE;
    ret.atitle.underline = L"\x2550\0=\0\0";
    ret.achapter.align = LEFT;
    ret.achapter.just_numbers = FALSE;
    ret.achapter.number_suffix = L": ";
    ret.achapter.underline = L"\x203E\0-\0\0";
    ret.nasect = 1;
    ret.asect = snewn(ret.nasect, alignstruct);
    ret.asect[0].align = LEFTPLUS;
    ret.asect[0].just_numbers = TRUE;
    ret.asect[0].number_suffix = L" ";
    ret.asect[0].underline = L"\0";
    ret.include_version_id = TRUE;
    ret.indent_preambles = FALSE;
    ret.bullet.text = L"\x2022\0-\0\0";
    ret.rule = L"\x2500\0-\0\0";
    ret.filename = dupstr("output.txt");
    ret.startemph = L"_\0_\0\0";
    ret.endemph = uadv(ret.startemph);
    ret.listsuffix = L".";
    ret.charset = CS_ASCII;
    /*
     * Default quote characters are Unicode matched single quotes,
     * falling back to the TeXlike `'.
     */
    ret.lquote = L"\x2018\0\x2019\0`\0'\0\0";
    ret.rquote = uadv(ret.lquote);

    /*
     * Two-pass configuration so that we can pick up global config
     * (e.g. `quotes') before having it overridden by specific
     * config (`text-quotes'), irrespective of the order in which
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
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"text-indent")) {
		ret.indent = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-charset")) {
		ret.charset = charset_from_ustr(&p->fpos, uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-filename")) {
		sfree(ret.filename);
		ret.filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(p->keyword, L"text-indent-code")) {
		ret.indent_code = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-width")) {
		ret.width = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-list-indent")) {
		ret.listindentbefore = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-listitem-indent")) {
		ret.listindentafter = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-chapter-align")) {
		ret.achapter.align = utoalign(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-chapter-underline")) {
		ret.achapter.underline = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"text-chapter-numeric")) {
		ret.achapter.just_numbers = utob(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-chapter-suffix")) {
		ret.achapter.number_suffix = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"text-section-align")) {
		wchar_t *q = uadv(p->keyword);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = sresize(ret.asect, n+1, alignstruct);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].align = utoalign(q);
	    } else if (!ustricmp(p->keyword, L"text-section-underline")) {
		wchar_t *q = uadv(p->keyword);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = sresize(ret.asect, n+1, alignstruct);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].underline = q;
	    } else if (!ustricmp(p->keyword, L"text-section-numeric")) {
		wchar_t *q = uadv(p->keyword);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = sresize(ret.asect, n+1, alignstruct);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].just_numbers = utob(q);
	    } else if (!ustricmp(p->keyword, L"text-section-suffix")) {
		wchar_t *q = uadv(p->keyword);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = sresize(ret.asect, n+1, alignstruct);
		    for (i = ret.nasect; i <= n; i++) {
			ret.asect[i] = ret.asect[ret.nasect-1];
		    }
		    ret.nasect = n+1;
		}
		ret.asect[n].number_suffix = q;
	    } else if (!ustricmp(p->keyword, L"text-title-align")) {
		ret.atitle.align = utoalign(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-title-underline")) {
		ret.atitle.underline = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"text-versionid")) {
		ret.include_version_id = utob(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-indent-preamble")) {
		ret.indent_preambles = utob(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"text-bullet")) {
		ret.bullet.text = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"text-rule")) {
		ret.rule = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"text-list-suffix")) {
		ret.listsuffix = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"text-emphasis")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.startemph = uadv(p->keyword);
		    ret.endemph = uadv(ret.startemph);
		}
	    } else if (!ustricmp(p->keyword, L"text-quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    }
	}
    }

    /*
     * Now process fallbacks on quote characters, underlines, the
     * rule character, the emphasis characters, and bullets.
     */
    while (*uadv(ret.rquote) && *uadv(uadv(ret.rquote)) &&
	   (!cvt_ok(ret.charset, ret.lquote) ||
	    !cvt_ok(ret.charset, ret.rquote))) {
	ret.lquote = uadv(ret.rquote);
	ret.rquote = uadv(ret.lquote);
    }

    while (*uadv(ret.endemph) && *uadv(uadv(ret.endemph)) &&
	   (!cvt_ok(ret.charset, ret.startemph) ||
	    !cvt_ok(ret.charset, ret.endemph))) {
	ret.startemph = uadv(ret.endemph);
	ret.endemph = uadv(ret.startemph);
    }

    while (*ret.atitle.underline && *uadv(ret.atitle.underline) &&
	   !cvt_ok(ret.charset, ret.atitle.underline))
	ret.atitle.underline = uadv(ret.atitle.underline);
    
    while (*ret.achapter.underline && *uadv(ret.achapter.underline) &&
	   !cvt_ok(ret.charset, ret.achapter.underline))
	ret.achapter.underline = uadv(ret.achapter.underline);

    for (n = 0; n < ret.nasect; n++) {
	while (*ret.asect[n].underline && *uadv(ret.asect[n].underline) &&
	       !cvt_ok(ret.charset, ret.asect[n].underline))
	    ret.asect[n].underline = uadv(ret.asect[n].underline);
    }
    
    while (*ret.bullet.text && *uadv(ret.bullet.text) &&
	   !cvt_ok(ret.charset, ret.bullet.text))
	ret.bullet.text = uadv(ret.bullet.text);

    while (*ret.rule && *uadv(ret.rule) &&
	   !cvt_ok(ret.charset, ret.rule))
	ret.rule = uadv(ret.rule);

    return ret;
}

paragraph *text_config_filename(char *filename)
{
    return cmdline_cfg_simple("text-filename", filename, NULL);
}

void text_backend(paragraph *sourceform, keywordlist *keywords,
		  indexdata *idx, void *unused) {
    paragraph *p;
    textconfig conf;
    word *prefix, *body, *wp;
    word spaceword;
    textfile tf;
    wchar_t *prefixextra;
    int nesting, nestindent;
    int indentb, indenta;

    IGNORE(unused);
    IGNORE(keywords);		       /* we don't happen to need this */
    IGNORE(idx);		       /* or this */

    conf = text_configure(sourceform);

    /*
     * Open the output file.
     */
    tf.fp = fopen(conf.filename, "w");
    if (!tf.fp) {
	error(err_cantopenw, conf.filename);
	return;
    }
    tf.charset = conf.charset;
    tf.state = charset_init_state;

    /* Do the title */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Title)
	    text_heading(&tf, NULL, NULL, p->words,
			 conf.atitle, conf.indent, conf.width, &conf);

    nestindent = conf.listindentbefore + conf.listindentafter;
    nesting = (conf.indent_preambles ? 0 : -conf.indent);

    /* Do the main document */
    for (p = sourceform; p; p = p->next) switch (p->type) {

      case para_QuotePush:
	nesting += 2;
	break;
      case para_QuotePop:
	nesting -= 2;
	assert(nesting >= 0);
	break;

      case para_LcontPush:
	nesting += nestindent;
	break;
      case para_LcontPop:
	nesting -= nestindent;
	assert(nesting >= 0);
	break;

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

	/*
	 * Chapter titles.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
	text_heading(&tf, p->kwtext, p->kwtext2, p->words,
		     conf.achapter, conf.indent, conf.width, &conf);
	nesting = 0;
	break;

      case para_Heading:
      case para_Subsect:
	text_heading(&tf, p->kwtext, p->kwtext2, p->words,
		     conf.asect[p->aux>=conf.nasect ? conf.nasect-1 : p->aux],
		     conf.indent, conf.width, &conf);
	break;

      case para_Rule:
	text_rule(&tf, conf.indent + nesting, conf.width - nesting, &conf);
	break;

      case para_Normal:
      case para_Copyright:
      case para_DescribedThing:
      case para_Description:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
	if (p->type == para_Bullet) {
	    prefix = &conf.bullet;
	    prefixextra = NULL;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else if (p->type == para_NumberedList) {
	    prefix = p->kwtext;
	    prefixextra = conf.listsuffix;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else if (p->type == para_Description) {
	    prefix = NULL;
	    prefixextra = NULL;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else {
	    prefix = NULL;
	    prefixextra = NULL;
	    indentb = indenta = 0;
	}
	if (p->type == para_BiblioCited) {
	    body = dup_word_list(p->kwtext);
	    for (wp = body; wp->next; wp = wp->next);
	    wp->next = &spaceword;
	    spaceword.next = p->words;
	    spaceword.alt = NULL;
	    spaceword.type = word_WhiteSpace;
	    spaceword.text = NULL;
	} else {
	    wp = NULL;
	    body = p->words;
	}
	text_para(&tf, prefix, prefixextra, body,
		  conf.indent + nesting + indentb, indenta,
		  conf.width - nesting - indentb - indenta, &conf);
	if (wp) {
	    wp->next = NULL;
	    free_word_list(body);
	}
	break;

      case para_Code:
	text_codepara(&tf, p->words,
		      conf.indent + nesting + conf.indent_code,
		      conf.width - nesting - 2 * conf.indent_code);
	break;
    }

    /* Do the version ID */
    if (conf.include_version_id) {
	for (p = sourceform; p; p = p->next)
	    if (p->type == para_VersionID)
 		text_versionid(&tf, p->words, &conf);
    }

    /*
     * Tidy up
     */
    text_output(&tf, NULL);	       /* end charset conversion */
    fclose(tf.fp);
    sfree(conf.asect);
    sfree(conf.filename);
}

static void text_output(textfile *tf, const wchar_t *s)
{
    char buf[256];
    int ret, len;
    const wchar_t **sp;

    if (!s) {
	sp = NULL;
	len = 1;
    } else {
	sp = &s;
	len = ustrlen(s);
    }

    while (len > 0) {
	ret = charset_from_unicode(sp, &len, buf, lenof(buf),
				   tf->charset, &tf->state, NULL);
	if (!sp)
	    len = 0;
	fwrite(buf, 1, ret, tf->fp);
    }
}

static void text_output_many(textfile *tf, int n, wchar_t c)
{
    wchar_t s[2];
    s[0] = c;
    s[1] = L'\0';
    while (n--)
	text_output(tf, s);
}

static void text_rdaddw(rdstring *rs, word *text, word *end, textconfig *cfg) {
    for (; text && text != end; text = text->next) switch (text->type) {
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
	    rdadds(rs, cfg->startemph);
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_First ||
		  attraux(text->aux) == attr_Only))
	    rdadds(rs, cfg->lquote);
	if (removeattr(text->type) == word_Normal) {
	    if (cvt_ok(cfg->charset, text->text) || !text->alt)
		rdadds(rs, text->text);
	    else
		text_rdaddw(rs, text->alt, NULL, cfg);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    rdadd(rs, L' ');
	} else if (removeattr(text->type) == word_Quote) {
	    rdadds(rs, quoteaux(text->aux) == quote_Open ?
		   cfg->lquote : cfg->rquote);
	}
	if (towordstyle(text->type) == word_Emph &&
	    (attraux(text->aux) == attr_Last ||
	     attraux(text->aux) == attr_Only))
	    rdadds(rs, cfg->endemph);
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_Last ||
		  attraux(text->aux) == attr_Only))
	    rdadds(rs, cfg->rquote);
	break;
    }
}

static int text_width(void *, word *);

static int text_width_list(void *ctx, word *text) {
    int w = 0;
    while (text) {
	w += text_width(ctx, text);
	text = text->next;
    }
    return w;
}

static int text_width(void *ctx, word *text) {
    textconfig *cfg = (textconfig *)ctx;
    int wid;
    int attr;

    switch (text->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_UpperXref:
      case word_LowerXref:
      case word_XrefEnd:
      case word_IndexRef:
	return 0;
    }

    assert(text->type < word_internal_endattrs);

    wid = 0;
    attr = towordstyle(text->type);
    if (attr == word_Emph || attr == word_Code) {
	if (attraux(text->aux) == attr_Only ||
	    attraux(text->aux) == attr_First)
	    wid += ustrwid(attr == word_Emph ? cfg->startemph : cfg->lquote,
			   cfg->charset);
    }
    if (attr == word_Emph || attr == word_Code) {
	if (attraux(text->aux) == attr_Only ||
	    attraux(text->aux) == attr_Last)
	    wid += ustrwid(attr == word_Emph ? cfg->startemph : cfg->lquote,
			   cfg->charset);
    }

    switch (text->type) {
      case word_Normal:
      case word_Emph:
      case word_Code:
      case word_WeakCode:
	if (cvt_ok(cfg->charset, text->text) || !text->alt)
	    wid += ustrwid(text->text, cfg->charset);
	else
	    wid += text_width_list(ctx, text->alt);
	return wid;

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
	if (removeattr(text->type) == word_Quote) {
	    if (quoteaux(text->aux) == quote_Open)
		wid += ustrwid(cfg->lquote, cfg->charset);
	    else
		wid += ustrwid(cfg->rquote, cfg->charset);
	} else
	    wid++;		       /* space */
    }

    return wid;
}

static void text_heading(textfile *tf, word *tprefix, word *nprefix,
			 word *text, alignstruct align,
			 int indent, int width, textconfig *cfg) {
    rdstring t = { 0, 0, NULL };
    int margin, length;
    int firstlinewidth, wrapwidth;
    wrappedline *wrapping, *p;

    if (align.just_numbers && nprefix) {
	text_rdaddw(&t, nprefix, NULL, cfg);
	rdadds(&t, align.number_suffix);
    } else if (!align.just_numbers && tprefix) {
	text_rdaddw(&t, tprefix, NULL, cfg);
	rdadds(&t, align.number_suffix);
    }
    margin = length = ustrwid(t.text ? t.text : L"", cfg->charset);

    if (align.align == LEFTPLUS) {
	margin = indent - margin;
	if (margin < 0) margin = 0;
	firstlinewidth = indent + width - margin - length;
	wrapwidth = width;
    } else if (align.align == LEFT || align.align == CENTRE) {
	margin = 0;
	firstlinewidth = indent + width - length;
	wrapwidth = indent + width;
    }

    wrapping = wrap_para(text, firstlinewidth, wrapwidth,
			 text_width, cfg, 0);
    for (p = wrapping; p; p = p->next) {
	text_rdaddw(&t, p->begin, p->end, cfg);
	length = ustrwid(t.text ? t.text : L"", cfg->charset);
	if (align.align == CENTRE) {
	    margin = (indent + width - length)/2;
	    if (margin < 0) margin = 0;
	}
	text_output_many(tf, margin, L' ');
	text_output(tf, t.text);
	text_output(tf, L"\n");
	if (*align.underline) {
	    text_output_many(tf, margin, L' ');
	    while (length > 0) {
		text_output(tf, align.underline);
		length -= ustrwid(align.underline, cfg->charset);
	    }
	    text_output(tf, L"\n");
	}
	if (align.align == LEFTPLUS)
	    margin = indent;
	else
	    margin = 0;
	sfree(t.text);
	t = empty_rdstring;
    }
    wrap_free(wrapping);
    text_output(tf, L"\n");

    sfree(t.text);
}

static void text_rule(textfile *tf, int indent, int width, textconfig *cfg) {
    text_output_many(tf, indent, L' ');
    while (width > 0) {
	text_output(tf, cfg->rule);
	width -= ustrwid(cfg->rule, cfg->charset);
    }
    text_output_many(tf, 2, L'\n');
}

static void text_para(textfile *tf, word *prefix, wchar_t *prefixextra,
		      word *text, int indent, int extraindent, int width,
		      textconfig *cfg) {
    wrappedline *wrapping, *p;
    rdstring pfx = { 0, 0, NULL };
    int e;
    int firstlinewidth = width;

    if (prefix) {
	text_rdaddw(&pfx, prefix, NULL, cfg);
	if (prefixextra)
	    rdadds(&pfx, prefixextra);
	text_output_many(tf, indent, L' ');
	text_output(tf, pfx.text);
	/* If the prefix is too long, shorten the first line to fit. */
	e = extraindent - ustrwid(pfx.text ? pfx.text : L"", cfg->charset);
	if (e < 0) {
	    firstlinewidth += e;       /* this decreases it, since e < 0 */
	    if (firstlinewidth < 0) {
		e = indent + extraindent;
		firstlinewidth = width;
		text_output(tf, L"\n");
	    } else
		e = 0;
	}
	sfree(pfx.text);
    } else
	e = indent + extraindent;

    wrapping = wrap_para(text, firstlinewidth, width,
			 text_width, cfg, 0);
    for (p = wrapping; p; p = p->next) {
	rdstring t = { 0, 0, NULL };
	text_rdaddw(&t, p->begin, p->end, cfg);
	text_output_many(tf, e, L' ');
	text_output(tf, t.text);
	text_output(tf, L"\n");
	e = indent + extraindent;
	sfree(t.text);
    }
    wrap_free(wrapping);
    text_output(tf, L"\n");
}

static void text_codepara(textfile *tf, word *text, int indent, int width) {
    for (; text; text = text->next) if (text->type == word_WeakCode) {
	int wid = ustrwid(text->text, tf->charset);
	if (wid > width)
	    error(err_text_codeline, &text->fpos, wid, width);
	text_output_many(tf, indent, L' ');
	text_output(tf, text->text);
	text_output(tf, L"\n");
    }

    text_output(tf, L"\n");
}

static void text_versionid(textfile *tf, word *text, textconfig *cfg) {
    rdstring t = { 0, 0, NULL };

    rdadd(&t, L'[');
    text_rdaddw(&t, text, NULL, cfg);
    rdadd(&t, L']');
    rdadd(&t, L'\n');

    text_output(tf, t.text);
    sfree(t.text);
}
