/*
 * text backend for Buttress
 */

#include <stdio.h>
#include <stdlib.h>
#include "buttress.h"

typedef enum { LEFT, CENTRE } alignment;

typedef struct {
    int indent;
    int listindentbefore, listindentafter;
    int width;
    alignment titlealign, chapteralign;
    wchar_t titleunderline, chapterunderline;
    int include_version_id;
    int indent_preambles;
    word bullet;
} textconfig;

static int text_convert(wchar_t *, char **);

static void text_title(FILE *, word *, word *, alignment, wchar_t, int, int);
static void text_heading(FILE *, word *, word *, int, int);
static void text_rule(FILE *, int, int);
static void text_para(FILE *, word *, char *, word *, int, int, int);
static void text_codepara(FILE *, word *, int, int);
static void text_versionid(FILE *, word *);

static textconfig text_configure(paragraph *sourceform) {
    textconfig ret;

    /*
     * Non-negotiables.
     */
    ret.bullet.next = NULL;
    ret.bullet.alt = NULL;
    ret.bullet.type = word_Normal;

    /*
     * Defaults.
     */
    ret.indent = 7;
    ret.listindentbefore = 1;
    ret.listindentafter = 3;
    ret.width = 68;
    ret.titlealign = CENTRE;
    ret.titleunderline = L'=';
    ret.chapteralign = LEFT;
    ret.chapterunderline = L'-';
    ret.include_version_id = TRUE;
    ret.indent_preambles = FALSE;
    ret.bullet.text = ustrdup(L"-");

    /*
     * FIXME: must walk the source form gleaning configuration from
     * \config paragraphs.
     */
    IGNORE(sourceform);		       /* for now */

    return ret;
}

void text_backend(paragraph *sourceform, keywordlist *keywords, index *idx) {
    paragraph *p;
    textconfig conf;
    word *prefix, *body, *wp;
    word spaceword;
    FILE *fp;
    char *prefixextra;
    int indentb, indenta;

    IGNORE(keywords);		       /* we don't happen to need this */
    IGNORE(idx);		       /* or this */

    conf = text_configure(sourceform);

    /*
     * Determine the output file name, and open the output file
     *
     * FIXME: want configurable output file names here. For the
     * moment, we'll just call it `output.txt'.
     */
    fp = fopen("output.txt", "w");
    if (!fp) {
	error(err_cantopenw, "output.txt");
	return;
    }

    /* Do the title */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Title)
	    text_title(fp, NULL, p->words,
		       conf.titlealign, conf.titleunderline,
		       conf.indent, conf.width);

    /* Do the preamble and copyright */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Preamble)
	    text_para(fp, NULL, NULL, p->words,
		      conf.indent_preambles ? conf.indent : 0, 0,
		      conf.width + (conf.indent_preambles ? 0 : conf.indent));
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Copyright)
	    text_para(fp, NULL, NULL, p->words,
		      conf.indent_preambles ? conf.indent : 0, 0,
		      conf.width + (conf.indent_preambles ? 0 : conf.indent));

    /* Do the main document */
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
	 * Chapter titles.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
	text_title(fp, p->kwtext, p->words,
		   conf.chapteralign, conf.chapterunderline,
		   conf.indent, conf.width);
	break;

      case para_Heading:
      case para_Subsect:
	text_heading(fp, p->kwtext2, p->words, conf.indent, conf.width);
	break;

      case para_Rule:
	text_rule(fp, conf.indent, conf.width);
	break;

      case para_Normal:
      case para_BiblioCited:	       /* FIXME: put the citation on front */
      case para_Bullet:
      case para_NumberedList:
	if (p->type == para_Bullet) {
	    prefix = &conf.bullet;
	    prefixextra = NULL;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else if (p->type == para_NumberedList) {
	    prefix = p->kwtext;
	    prefixextra = ".";
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
	text_para(fp, prefix, prefixextra, body,
		  conf.indent + indentb, indenta, conf.width);
	if (wp) {
	    wp->next = NULL;
	    free_word_list(body);
	}
	break;

      case para_Code:
	text_codepara(fp, p->words, conf.indent, conf.width);
	break;
    }

    /* Do the version ID */
    if (conf.include_version_id) {
	for (p = sourceform; p; p = p->next)
	    if (p->type == para_VersionID)
 		text_versionid(fp, p->words);
    }

    /*
     * Tidy up
     */
    fclose(fp);
    sfree(conf.bullet.text);
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
static int text_convert(wchar_t *s, char **result) {
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

static void text_rdaddwc(rdstringc *rs, word *text, word *end) {
    char *c;

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
	if (text->type == word_Emph &&
	    (text->aux == attr_First || text->aux == attr_Only))
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (text->type == word_Code &&
		 (text->aux == attr_First || text->aux == attr_Only))
	    rdaddc(rs, '`');	       /* FIXME: configurability */
	if (text_convert(text->text, &c))
	    rdaddsc(rs, c);
	else
	    text_rdaddwc(rs, text->alt, NULL);
	sfree(c);
	if (text->type == word_Emph &&
	    (text->aux == attr_Last || text->aux == attr_Only))
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (text->type == word_Code &&
		 (text->aux == attr_Last || text->aux == attr_Only))
	    rdaddc(rs, '\'');	       /* FIXME: configurability */
	break;

      case word_WhiteSpace:
      case word_EmphSpace:
      case word_CodeSpace:
      case word_WkCodeSpace:
	if (text->type == word_EmphSpace &&
	    (text->aux == attr_First || text->aux == attr_Only))
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (text->type == word_CodeSpace &&
		 (text->aux == attr_First || text->aux == attr_Only))
	    rdaddc(rs, '`');	       /* FIXME: configurability */
	rdaddc(rs, ' ');
	if (text->type == word_EmphSpace &&
	    (text->aux == attr_Last || text->aux == attr_Only))
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (text->type == word_CodeSpace &&
		 (text->aux == attr_Last || text->aux == attr_Only))
	    rdaddc(rs, '\'');	       /* FIXME: configurability */
	break;
    }
}

static int text_width(word *);

static int text_width_list(word *text) {
    int w = 0;
    while (text) {
	w += text_width(text);
	text = text->next;
    }
    return w;
}

static int text_width(word *text) {
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
		 ? (text->aux == attr_Only ? 2 :
		    text->aux == attr_Always ? 0 : 1)
		 : 0) +
		(text_convert(text->text, NULL) ?
		 ustrlen(text->text) :
		 text_width_list(text->alt)));

      case word_WhiteSpace:
      case word_EmphSpace:
      case word_CodeSpace:
      case word_WkCodeSpace:
	return (((text->type == word_EmphSpace ||
		  text->type == word_CodeSpace)
		 ? (text->aux == attr_Only ? 2 :
		    text->aux == attr_Always ? 0 : 1)
		 : 0) + 1);
    }
    return 0;			       /* should never happen */
}

static void text_title(FILE *fp, word *prefix, word *text,
		       alignment align, wchar_t underline,
		       int indent, int width) {
    rdstringc t = { 0, 0, NULL };
    int margin, length;

    if (prefix) {
	text_rdaddwc(&t, prefix, NULL);
	rdaddsc(&t, ": ");
    }
    text_rdaddwc(&t, text, NULL);

    length = strlen(t.text);
    if (align == CENTRE) {
	margin = (indent + width - length)/2;
	if (margin < 0) margin = 0;
    } else
	margin = 0;

    fprintf(fp, "%*s%s\n", margin, "", t.text);
    if (underline != L'\0') {
	char *u, uc;
	wchar_t uw[2];
	uw[0] = underline; uw[1] = L'\0';
	text_convert(uw, &u);
	uc = u[0];
	sfree(u);
	fprintf(fp, "%*s", margin, "");
	while (length--)
	    putc(uc, fp);
	putc('\n', fp);
    }
    putc('\n', fp);

    sfree(t.text);
}

static void text_heading(FILE *fp, word *prefix, word *text,
			 int indent, int width) {
    rdstringc t = { 0, 0, NULL };
    int margin;

    if (prefix) {
	text_rdaddwc(&t, prefix, NULL);
	rdaddc(&t, ' ');
	margin = strlen(t.text);
    }
    text_rdaddwc(&t, text, NULL);

    margin = indent - margin;
    if (margin < 0) margin = 0;

    fprintf(fp, "%*s%s\n\n", margin, "", t.text);

    if (strlen(t.text) > (size_t)width) {
	/* FIXME: warn */
    }

    sfree(t.text);
}

static void text_rule(FILE *fp, int indent, int width) {
    while (indent--) putc(' ', fp);
    while (width--) putc('-', fp);     /* FIXME: configurability! */
    putc('\n', fp);
    putc('\n', fp);
}

static void text_para(FILE *fp, word *prefix, char *prefixextra, word *text,
		      int indent, int extraindent, int width) {
    wrappedline *wrapping, *p;
    rdstringc pfx = { 0, 0, NULL };
    int e;
    int firstlinewidth = width;

    if (prefix) {
	text_rdaddwc(&pfx, prefix, NULL);
	if (prefixextra)
	    rdaddsc(&pfx, prefixextra);
	fprintf(fp, "%*s%s", indent, "", pfx.text);
	e = extraindent - strlen(pfx.text);
	if (e < 0) {
	    e = 0;
	    firstlinewidth -= e;
	    if (firstlinewidth < 0) {
		e = indent + extraindent;
		firstlinewidth = width;
		fprintf(fp, "\n");
	    }
	}
	sfree(pfx.text);
    } else
	e = indent + extraindent;

    wrapping = wrap_para(text, firstlinewidth, width, text_width);
    for (p = wrapping; p; p = p->next) {
	rdstringc t = { 0, 0, NULL };
	text_rdaddwc(&t, p->begin, p->end);
	fprintf(fp, "%*s%s\n", e, "", t.text);
	e = indent + extraindent;
	sfree(t.text);
    }
    wrap_free(wrapping);
    putc('\n', fp);
}

static void text_codepara(FILE *fp, word *text, int indent, int width) {
    for (; text; text = text->next) if (text->type == word_WeakCode) {
	char *c;
	text_convert(text->text, &c);
	if (strlen(c) > (size_t)width) {
	    /* FIXME: warn */
	}
	fprintf(fp, "%*s%s\n", indent, "", c);
	sfree(c);
    }

    putc('\n', fp);
}

static void text_versionid(FILE *fp, word *text) {
    rdstringc t = { 0, 0, NULL };

    rdaddc(&t, '[');		       /* FIXME: configurability */
    text_rdaddwc(&t, text, NULL);
    rdaddc(&t, ']');		       /* FIXME: configurability */

    fprintf(fp, "%s\n\n", t.text);
    sfree(t.text);
}
