/*
 * man page backend for Halibut
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "halibut.h"

static void man_text(FILE *, word *, int newline, int quote_props);
static void man_codepara(FILE *, word *);

#define QUOTE_INITCTRL 1 /* quote initial . and ' on a line */
#define QUOTE_QUOTES   2 /* quote double quotes by doubling them */

void man_backend(paragraph *sourceform, keywordlist *keywords,
		 indexdata *idx) {
    paragraph *p;
    FILE *fp;
    char const *sep;

    IGNORE(keywords);		       /* we don't happen to need this */
    IGNORE(idx);		       /* or this */

    /*
     * Determine the output file name, and open the output file
     *
     * FIXME: want configurable output file names here. For the
     * moment, we'll just call it `output.1'.
     */
    fp = fopen("output.1", "w");
    if (!fp) {
	error(err_cantopenw, "output.1");
	return;
    }

    /* Do the version ID */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID) {
	    fprintf(fp, ".\\\" ");
	    man_text(fp, p->words, TRUE, 0);
	}

    /* FIXME: .TH name-of-program manual-section */
    fprintf(fp, ".TH FIXME 1\n");

    fprintf(fp, ".UC\n");

    /* Do the preamble and copyright */
    sep = "";
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Preamble) {
	    fprintf(fp, "%s", sep);
	    man_text(fp, p->words, TRUE, 0);
	    sep = "\n";
	}
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Copyright) {
	    fprintf(fp, "%s", sep);
	    man_text(fp, p->words, TRUE, 0);
	    sep = "\n";
	}

    /*
     * FIXME:
     * 
     *  - figure out precisely what needs to be escaped.
     * 	   * A dot or apostrophe at the start of a line wants to be
     * 	     preceded by `\&', which is a zero-width space.
     *     * Literal backslashes always want doubling.
     * 	   * Within double quotes, a double quote needs doubling
     * 	     too.
     * 
     *  - work out what to do about hyphens / minuses...
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
	 * Headings.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
      case para_Heading:
      case para_Subsect:
	fprintf(fp, ".SH \"");
	/* FIXME: disable this, at _least_ by default */
	if (p->kwtext)
	    man_text(fp, p->kwtext, FALSE, QUOTE_QUOTES);
	fprintf(fp, " ");
	man_text(fp, p->words, FALSE, QUOTE_QUOTES);
	fprintf(fp, "\"\n");
	break;

	/*
	 * Code paragraphs.
	 */
      case para_Code:
	fprintf(fp, ".PP\n");
	man_codepara(fp, p->words);
	break;

	/*
	 * Normal paragraphs.
	 */
      case para_Normal:
	fprintf(fp, ".PP\n");
	man_text(fp, p->words, TRUE, 0);
	break;

	/*
	 * List paragraphs.
	 */
      case para_Description:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
	if (p->type == para_Bullet) {
	    fprintf(fp, ".IP \"\\fBo\\fP\"\n");   /* FIXME: configurable? */
	} else if (p->type == para_NumberedList) {
	    fprintf(fp, ".IP \"");
	    man_text(fp, p->kwtext, FALSE, QUOTE_QUOTES);
	    fprintf(fp, "\"\n");
	} else if (p->type == para_Description) {
	    /*
	     * Do nothing; the .xP for this paragraph is the .IP
	     * which has come before it in the DescribedThing.
	     */
	} else if (p->type == para_BiblioCited) {
	    fprintf(fp, ".IP \"");
	    man_text(fp, p->kwtext, FALSE, QUOTE_QUOTES);
	    fprintf(fp, "\"\n");
	}
	man_text(fp, p->words, TRUE, 0);
	break;

      case para_DescribedThing:
	fprintf(fp, ".IP \"");
	man_text(fp, p->words, FALSE, QUOTE_QUOTES);
	fprintf(fp, "\"\n");
	break;

      case para_Rule:
	/*
	 * FIXME.
	 */
	break;

      case para_LcontPush:
	fprintf(fp, ".RS\n");
      	break;
      case para_LcontPop:
	fprintf(fp, ".RE\n");
	break;
    }

    /*
     * Tidy up.
     */
    fclose(fp);
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
 * 
 * FIXME: Here is probably also a good place to do escaping sorts
 * of things. I know I at least need to escape backslash, and full
 * stops at the starts of words are probably trouble as well.
 */
static int man_convert(wchar_t *s, char **result, int quote_props) {
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
	    if (plen+3 >= psize) {
		psize = plen + 256;
		p = resize(p, psize);
	    }
	    if (plen == 0 && (outc == '.' || outc == '\'') &&
		(quote_props & QUOTE_INITCTRL)) {
		/*
		 * Control character (. or ') at the start of a
		 * line. Quote it by putting \& (troff zero-width
		 * space) before it.
		 */
		p[plen++] = '\\';
		p[plen++] = '&';
	    } else if (outc == '\\') {
		/*
		 * Quote backslashes by doubling them, always.
		 */
		p[plen++] = '\\';
	    } else if (outc == '"' && (quote_props & QUOTE_QUOTES)) {
		/*
		 * Double quote within double quotes. Quote it by
		 * doubling.
		 */
		p[plen++] = '"';
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

static void man_rdaddwc(rdstringc *rs, word *text, word *end,
			int quote_props) {
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
	    rdaddsc(rs, "\\fI");
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_First ||
		  attraux(text->aux) == attr_Only))
	    rdaddsc(rs, "\\fB");
	if (removeattr(text->type) == word_Normal) {
	    if (rs->pos > 0)
		quote_props &= ~QUOTE_INITCTRL;   /* not at start any more */
	    if (man_convert(text->text, &c, quote_props))
		rdaddsc(rs, c);
	    else
		man_rdaddwc(rs, text->alt, NULL, quote_props);
	    sfree(c);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    rdaddc(rs, ' ');
	} else if (removeattr(text->type) == word_Quote) {
	    rdaddc(rs, quoteaux(text->aux) == quote_Open ? '`' : '\'');
				       /* FIXME: configurability */
	}
	if (towordstyle(text->type) == word_Emph &&
	    (attraux(text->aux) == attr_Last ||
	     attraux(text->aux) == attr_Only))
	    rdaddsc(rs, "\\fP");
	else if (towordstyle(text->type) == word_Code &&
		 (attraux(text->aux) == attr_Last ||
		  attraux(text->aux) == attr_Only))
	    rdaddsc(rs, "\\fP");
	break;
    }
}

static void man_text(FILE *fp, word *text, int newline, int quote_props) {
    rdstringc t = { 0, 0, NULL };

    man_rdaddwc(&t, text, NULL, quote_props | QUOTE_INITCTRL);
    fprintf(fp, "%s", t.text);
    sfree(t.text);
    if (newline)
	fputc('\n', fp);
}

static void man_codepara(FILE *fp, word *text) {
    fprintf(fp, ".nf\n");
    for (; text; text = text->next) if (text->type == word_WeakCode) {
	char *c;
	man_convert(text->text, &c, QUOTE_INITCTRL);
	fprintf(fp, "%s\n", c);
	sfree(c);
    }
    fprintf(fp, ".fi\n");
}
