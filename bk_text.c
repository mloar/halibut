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
    wchar_t underline;
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
    char *filename;
} textconfig;

typedef struct {
    FILE *fp;
    int charset;
    charset_state state;
} textfile;

static void text_heading(textfile *, word *, word *, word *, alignstruct,
			 int,int);
static void text_rule(textfile *, int, int);
static void text_para(textfile *, word *, wchar_t *, word *, int, int, int);
static void text_codepara(textfile *, word *, int, int);
static void text_versionid(textfile *, word *);

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
    ret.atitle.underline = L'=';
    ret.achapter.align = LEFT;
    ret.achapter.just_numbers = FALSE;
    ret.achapter.number_suffix = L": ";
    ret.achapter.underline = L'-';
    ret.nasect = 1;
    ret.asect = mknewa(alignstruct, ret.nasect);
    ret.asect[0].align = LEFTPLUS;
    ret.asect[0].just_numbers = TRUE;
    ret.asect[0].number_suffix = L" ";
    ret.asect[0].underline = L'\0';
    ret.include_version_id = TRUE;
    ret.indent_preambles = FALSE;
    ret.bullet.text = L"-";
    ret.filename = dupstr("output.txt");
    ret.charset = CS_ASCII;

    for (; source; source = source->next) {
	if (source->type == para_Config) {
	    if (!ustricmp(source->keyword, L"text-indent")) {
		ret.indent = utoi(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-charset")) {
		char *csname = utoa_dup(uadv(source->keyword), CS_ASCII);
		ret.charset = charset_from_localenc(csname);
		sfree(csname);
	    } else if (!ustricmp(source->keyword, L"text-filename")) {
		sfree(ret.filename);
		ret.filename = dupstr(adv(source->origkeyword));
	    } else if (!ustricmp(source->keyword, L"text-indent-code")) {
		ret.indent_code = utoi(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-width")) {
		ret.width = utoi(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-list-indent")) {
		ret.listindentbefore = utoi(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-listitem-indent")) {
		ret.listindentafter = utoi(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-chapter-align")) {
		ret.achapter.align = utoalign(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-chapter-underline")) {
		ret.achapter.underline = *uadv(source->keyword);
	    } else if (!ustricmp(source->keyword, L"text-chapter-numeric")) {
		ret.achapter.just_numbers = utob(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-chapter-suffix")) {
		ret.achapter.number_suffix = uadv(source->keyword);
	    } else if (!ustricmp(source->keyword, L"text-section-align")) {
		wchar_t *p = uadv(source->keyword);
		int n = 0;
		if (uisdigit(*p)) {
		    n = utoi(p);
		    p = uadv(p);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = resize(ret.asect, n+1);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].align = utoalign(p);
	    } else if (!ustricmp(source->keyword, L"text-section-underline")) {
		wchar_t *p = uadv(source->keyword);
		int n = 0;
		if (uisdigit(*p)) {
		    n = utoi(p);
		    p = uadv(p);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = resize(ret.asect, n+1);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].underline = *p;
	    } else if (!ustricmp(source->keyword, L"text-section-numeric")) {
		wchar_t *p = uadv(source->keyword);
		int n = 0;
		if (uisdigit(*p)) {
		    n = utoi(p);
		    p = uadv(p);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = resize(ret.asect, n+1);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].just_numbers = utob(p);
	    } else if (!ustricmp(source->keyword, L"text-section-suffix")) {
		wchar_t *p = uadv(source->keyword);
		int n = 0;
		if (uisdigit(*p)) {
		    n = utoi(p);
		    p = uadv(p);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = resize(ret.asect, n+1);
		    for (i = ret.nasect; i <= n; i++) {
			ret.asect[i] = ret.asect[ret.nasect-1];
		    }
		    ret.nasect = n+1;
		}
		ret.asect[n].number_suffix = p;
	    } else if (!ustricmp(source->keyword, L"text-title-align")) {
		ret.atitle.align = utoalign(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-title-underline")) {
		ret.atitle.underline = *uadv(source->keyword);
	    } else if (!ustricmp(source->keyword, L"text-versionid")) {
		ret.include_version_id = utob(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-indent-preamble")) {
		ret.indent_preambles = utob(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"text-bullet")) {
		ret.bullet.text = uadv(source->keyword);
	    }
	}
    }

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
			 conf.atitle, conf.indent, conf.width);

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
		     conf.achapter, conf.indent, conf.width);
	nesting = 0;
	break;

      case para_Heading:
      case para_Subsect:
	text_heading(&tf, p->kwtext, p->kwtext2, p->words,
		     conf.asect[p->aux>=conf.nasect ? conf.nasect-1 : p->aux],
		     conf.indent, conf.width);
	break;

      case para_Rule:
	text_rule(&tf, conf.indent + nesting, conf.width - nesting);
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
	    prefixextra = L".";	       /* FIXME: configurability */
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
		  conf.width - nesting - indentb - indenta);
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
 		text_versionid(&tf, p->words);
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

static void text_rdaddw(int charset, rdstring *rs, word *text, word *end) {
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
	    rdadd(rs, L'_');	       /* FIXME: configurability */
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_First ||
		  attraux(text->aux) == attr_Only))
	    rdadd(rs, L'`');	       /* FIXME: configurability */
	if (removeattr(text->type) == word_Normal) {
	    if (cvt_ok(charset, text->text) || !text->alt)
		rdadds(rs, text->text);
	    else
		text_rdaddw(charset, rs, text->alt, NULL);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    rdadd(rs, L' ');
	} else if (removeattr(text->type) == word_Quote) {
	    rdadd(rs, quoteaux(text->aux) == quote_Open ? L'`' : L'\'');
				       /* FIXME: configurability */
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
    int charset = * (int *) ctx;

    switch (text->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_UpperXref:
      case word_LowerXref:
      case word_XrefEnd:
      case word_IndexRef:
	return 0;

      case word_Normal:
      case word_Emph:
      case word_Code:
      case word_WeakCode:
	return (((text->type == word_Emph ||
		  text->type == word_Code)
		 ? (attraux(text->aux) == attr_Only ? 2 :
		    attraux(text->aux) == attr_Always ? 0 : 1)
		 : 0) +
		(cvt_ok(charset, text->text) || !text->alt ?
		 ustrwid(text->text, charset) :
		 text_width_list(ctx, text->alt)));

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
	return (((towordstyle(text->type) == word_Emph ||
		  towordstyle(text->type) == word_Code)
		 ? (attraux(text->aux) == attr_Only ? 2 :
		    attraux(text->aux) == attr_Always ? 0 : 1)
		 : 0) + 1);
    }
    return 0;			       /* should never happen */
}

static void text_heading(textfile *tf, word *tprefix, word *nprefix,
			 word *text, alignstruct align,
			 int indent, int width) {
    rdstring t = { 0, 0, NULL };
    int margin, length;
    int firstlinewidth, wrapwidth;
    wrappedline *wrapping, *p;

    if (align.just_numbers && nprefix) {
	text_rdaddw(tf->charset, &t, nprefix, NULL);
	rdadds(&t, align.number_suffix);
    } else if (!align.just_numbers && tprefix) {
	text_rdaddw(tf->charset, &t, tprefix, NULL);
	rdadds(&t, align.number_suffix);
    }
    margin = length = t.pos;

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
			 text_width, &tf->charset, 0);
    for (p = wrapping; p; p = p->next) {
	text_rdaddw(tf->charset, &t, p->begin, p->end);
	length = t.pos;
	if (align.align == CENTRE) {
	    margin = (indent + width - length)/2;
	    if (margin < 0) margin = 0;
	}
	text_output_many(tf, margin, L' ');
	text_output(tf, t.text);
	text_output(tf, L"\n");
	if (align.underline != L'\0') {
	    text_output_many(tf, margin, L' ');
	    text_output_many(tf, length, align.underline);
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

static void text_rule(textfile *tf, int indent, int width) {
    text_output_many(tf, indent, L' ');
    text_output_many(tf, width, L'-');     /* FIXME: configurability! */
    text_output_many(tf, 2, L'\n');
}

static void text_para(textfile *tf, word *prefix, wchar_t *prefixextra,
		      word *text, int indent, int extraindent, int width) {
    wrappedline *wrapping, *p;
    rdstring pfx = { 0, 0, NULL };
    int e;
    int firstlinewidth = width;

    if (prefix) {
	text_rdaddw(tf->charset, &pfx, prefix, NULL);
	if (prefixextra)
	    rdadds(&pfx, prefixextra);
	text_output_many(tf, indent, L' ');
	text_output(tf, pfx.text);
	/* If the prefix is too long, shorten the first line to fit. */
	e = extraindent - pfx.pos;
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
			 text_width, &tf->charset, 0);
    for (p = wrapping; p; p = p->next) {
	rdstring t = { 0, 0, NULL };
	text_rdaddw(tf->charset, &t, p->begin, p->end);
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
	if (ustrlen(text->text) > width) {
	    /* FIXME: warn */
	}
	text_output_many(tf, indent, L' ');
	text_output(tf, text->text);
	text_output(tf, L"\n");
    }

    text_output(tf, L"\n");
}

static void text_versionid(textfile *tf, word *text) {
    rdstring t = { 0, 0, NULL };

    rdadd(&t, L'[');		       /* FIXME: configurability */
    text_rdaddw(tf->charset, &t, text, NULL);
    rdadd(&t, L']');		       /* FIXME: configurability */
    rdadd(&t, L'\n');

    text_output(tf, t.text);
    sfree(t.text);
}
