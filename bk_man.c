/*
 * man page backend for Halibut
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "halibut.h"

typedef struct {
    wchar_t *th;
    int headnumbers;
    int mindepth;
    char *filename;
    int charset;
    wchar_t *bullet, *rule, *lquote, *rquote;
} manconfig;

static void man_text(FILE *, word *,
		     int newline, int quote_props, manconfig *conf);
static void man_codepara(FILE *, word *, int charset);
static int man_convert(wchar_t const *s, int maxlen,
		       char **result, int quote_props,
		       int charset, charset_state *state);

/*
 * My TROFF reference is "NROFF/TROFF User's Manual", Joseph
 * F. Ossana, October 11 1976.
 *
 * not yet used:
 * \(ru  rule
 * \(pl  math plus
 * \(mi  math minus
 * \(eq  math equals
 * \(ga  grave accent
 * \(ul  underrule
 * \(sl  slash (matching bakslash)
 * \(br  box vertical rule
 * \(br  Bell System logo
 * \(or  or
 * all characters for constructing large brackets
 */

static struct {
    unsigned short uni;
    char const *troff;
} const man_charmap[] = {
    {0x00A2, "\\(ct"}, {0x00A7, "\\(sc"}, {0x00A9, "\\(co"}, {0x00AC, "\\(no"},
    {0x00AE, "\\(rg"}, {0x00B0, "\\(de"}, {0x00B1, "\\(+-"}, {0x00B4, "\\(aa"},
    {0x00BC, "\\(14"}, {0x00BD, "\\(12"}, {0x00BE, "\\(34"}, {0x00D7, "\\(mu"},
    {0x00F7, "\\(di"},

    {0x0391, "\\(*A"}, {0x0392, "\\(*B"}, {0x0393, "\\(*G"}, {0x0394, "\\(*D"},
    {0x0395, "\\(*E"}, {0x0396, "\\(*Z"}, {0x0397, "\\(*Y"}, {0x0398, "\\(*H"},
    {0x0399, "\\(*I"}, {0x039A, "\\(*K"}, {0x039B, "\\(*L"}, {0x039C, "\\(*M"},
    {0x039D, "\\(*N"}, {0x039E, "\\(*C"}, {0x039F, "\\(*O"}, {0x03A0, "\\(*P"},
    {0x03A1, "\\(*R"}, {0x03A3, "\\(*S"}, {0x03A4, "\\(*T"}, {0x03A5, "\\(*U"},
    {0x03A6, "\\(*F"}, {0x03A7, "\\(*X"}, {0x03A8, "\\(*Q"}, {0x03A9, "\\(*W"},
    {0x03B1, "\\(*a"}, {0x03B2, "\\(*b"}, {0x03B3, "\\(*g"}, {0x03B4, "\\(*d"},
    {0x03B5, "\\(*e"}, {0x03B6, "\\(*z"}, {0x03B7, "\\(*y"}, {0x03B8, "\\(*h"},
    {0x03B9, "\\(*i"}, {0x03BA, "\\(*k"}, {0x03BB, "\\(*l"}, {0x03BC, "\\(*m"},
    {0x03BD, "\\(*n"}, {0x03BE, "\\(*c"}, {0x03BF, "\\(*o"}, {0x03C0, "\\(*p"},
    {0x03C1, "\\(*r"}, {0x03C2, "\\(ts"}, {0x03C3, "\\(*s"}, {0x03C4, "\\(*t"},
    {0x03C5, "\\(*u"}, {0x03C6, "\\(*f"}, {0x03C7, "\\(*x"}, {0x03C8, "\\(*q"},
    {0x03C9, "\\(*w"},

    {0x2014, "\\(em"}, {0x2018, "`"},     {0x2019, "'"},     {0x2020, "\\(dg"},
    {0x2021, "\\(dd"}, {0x2022, "\\(bu"}, {0x2032, "\\(fm"},

    {0x2190, "\\(<-"}, {0x2191, "\\(ua"}, {0x2192, "\\(->"}, {0x2193, "\\(da"},

    {0x2202, "\\(pd"}, {0x2205, "\\(es"}, {0x2207, "\\(gr"}, {0x2208, "\\(mo"},
    {0x2212, "\\-"},   {0x2217, "\\(**"}, {0x221A, "\\(sr"}, {0x221D, "\\(pt"},
    {0x221E, "\\(if"}, {0x2229, "\\(ca"}, {0x222A, "\\(cu"}, {0x222B, "\\(is"},
    {0x223C, "\\(ap"}, {0x2245, "\\(~="}, {0x2260, "\\(!="}, {0x2261, "\\(=="},
    {0x2264, "\\(<="}, {0x2265, "\\(>="}, {0x2282, "\\(sb"}, {0x2283, "\\(sp"},
    {0x2286, "\\(ib"}, {0x2287, "\\(ip"},

    {0x25A1, "\\(sq"},  {0x25CB, "\\(ci"},

    {0x261C, "\\(lh"},  {0x261E, "\\(rh"},
};

static char const *troffchar(int unichar) {
    int i, j, k;

    i = -1;
    j = lenof(man_charmap);
    while (j-i > 1) {
	k = (i + j) / 2;
	if (man_charmap[k].uni == unichar)
	    return man_charmap[k].troff;
	else if (man_charmap[k].uni > unichar)
	    j = k;
	else
	    i = k;
    }
    return NULL;
}

/*
 * Return TRUE if we can represent the whole of the given string either
 * in the output charset or as named characters; FALSE otherwise.
 */
static int troff_ok(int charset, wchar_t *string) {
    wchar_t test[2];
    while (*string) {
	test[0] = *string;
	test[1] = 0;
	if (!cvt_ok(charset, test) && !troffchar(*string))
	    return FALSE;
	string++;
    }
    return TRUE;
}

static manconfig man_configure(paragraph *source) {
    paragraph *p;
    manconfig ret;

    /*
     * Defaults.
     */
    ret.th = NULL;
    ret.headnumbers = FALSE;
    ret.mindepth = 0;
    ret.filename = dupstr("output.1");
    ret.charset = CS_ASCII;
    ret.bullet = L"\x2022\0o\0\0";
    ret.rule = L"\x2500\0-\0\0";
    ret.lquote = L"\x2018\0\x2019\0\"\0\"\0\0";
    ret.rquote = uadv(ret.lquote);

    /*
     * Two-pass configuration so that we can pick up global config
     * (e.g. `quotes') before having it overridden by specific
     * config (`man-quotes'), irrespective of the order in which
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
	    if (!ustricmp(p->keyword, L"man-identity")) {
		wchar_t *wp, *ep;

		wp = uadv(p->keyword);
		ep = wp;
		while (*ep)
		    ep = uadv(ep);
		sfree(ret.th);
		ret.th = snewn(ep - wp + 1, wchar_t);
		memcpy(ret.th, wp, (ep - wp + 1) * sizeof(wchar_t));
	    } else if (!ustricmp(p->keyword, L"man-charset")) {
		ret.charset = charset_from_ustr(&p->fpos, uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"man-headnumbers")) {
		ret.headnumbers = utob(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"man-mindepth")) {
		ret.mindepth = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"man-filename")) {
		sfree(ret.filename);
		ret.filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(p->keyword, L"man-bullet")) {
		ret.bullet = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"man-rule")) {
		ret.rule = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"man-quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    }
	}
    }

    /*
     * Now process fallbacks on quote characters, bullets, and the
     * rule character.
     */
    while (*uadv(ret.rquote) && *uadv(uadv(ret.rquote)) &&
	   (!troff_ok(ret.charset, ret.lquote) ||
	    !troff_ok(ret.charset, ret.rquote))) {
	ret.lquote = uadv(ret.rquote);
	ret.rquote = uadv(ret.lquote);
    }

    while (*ret.bullet && *uadv(ret.bullet) &&
	   !troff_ok(ret.charset, ret.bullet))
	ret.bullet = uadv(ret.bullet);

    while (*ret.rule && *uadv(ret.rule) &&
	   !troff_ok(ret.charset, ret.rule))
	ret.rule = uadv(ret.rule);

    return ret;
}

static void man_conf_cleanup(manconfig cf)
{
    sfree(cf.th);
    sfree(cf.filename);
}

paragraph *man_config_filename(char *filename)
{
    return cmdline_cfg_simple("man-filename", filename, NULL);
}

#define QUOTE_INITCTRL 1 /* quote initial . and ' on a line */
#define QUOTE_QUOTES   2 /* quote double quotes by doubling them */

void man_backend(paragraph *sourceform, keywordlist *keywords,
		 indexdata *idx, void *unused) {
    paragraph *p;
    FILE *fp;
    manconfig conf;
    int had_described_thing;

    IGNORE(unused);
    IGNORE(keywords);
    IGNORE(idx);

    conf = man_configure(sourceform);

    /*
     * Open the output file.
     */
    fp = fopen(conf.filename, "w");
    if (!fp) {
	error(err_cantopenw, conf.filename);
	return;
    }

    /* Do the version ID */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID) {
	    fprintf(fp, ".\\\" ");
	    man_text(fp, p->words, TRUE, 0, &conf);
	}

    /* .TH name-of-program manual-section */
    fprintf(fp, ".TH");
    if (conf.th && *conf.th) {
	char *c;
	wchar_t *wp;

	for (wp = conf.th; *wp; wp = uadv(wp)) {
	    fputs(" \"", fp);
	    man_convert(wp, 0, &c, QUOTE_QUOTES, conf.charset, NULL);
	    fputs(c, fp);
	    sfree(c);
	    fputc('"', fp);
	}
    }
    fputc('\n', fp);

    had_described_thing = FALSE;
#define cleanup_described_thing do { \
    if (had_described_thing) \
	fprintf(fp, "\n"); \
    had_described_thing = FALSE; \
} while (0)

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

	/*
	 * Headings.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
      case para_Heading:
      case para_Subsect:

	cleanup_described_thing;
	{
	    int depth;
	    if (p->type == para_Subsect)
		depth = p->aux + 1;
	    else if (p->type == para_Heading)
		depth = 1;
	    else
		depth = 0;
	    if (depth >= conf.mindepth) {
		if (depth > conf.mindepth)
		    fprintf(fp, ".SS \"");
		else
		    fprintf(fp, ".SH \"");
		if (conf.headnumbers && p->kwtext) {
		    man_text(fp, p->kwtext, FALSE, QUOTE_QUOTES, &conf);
		    fprintf(fp, " ");
		}
		man_text(fp, p->words, FALSE, QUOTE_QUOTES, &conf);
		fprintf(fp, "\"\n");
	    }
	    break;
	}

	/*
	 * Code paragraphs.
	 */
      case para_Code:
	cleanup_described_thing;
	fprintf(fp, ".PP\n");
	man_codepara(fp, p->words, conf.charset);
	break;

	/*
	 * Normal paragraphs.
	 */
      case para_Normal:
      case para_Copyright:
	cleanup_described_thing;
	fprintf(fp, ".PP\n");
	man_text(fp, p->words, TRUE, 0, &conf);
	break;

	/*
	 * List paragraphs.
	 */
      case para_Description:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
	if (p->type != para_Description)
	    cleanup_described_thing;

	if (p->type == para_Bullet) {
	    char *bullettext;
	    man_convert(conf.bullet, -1, &bullettext, QUOTE_QUOTES,
			conf.charset, NULL);
	    fprintf(fp, ".IP \"\\fB%s\\fP\"\n", bullettext);
	    sfree(bullettext);
	} else if (p->type == para_NumberedList) {
	    fprintf(fp, ".IP \"");
	    man_text(fp, p->kwtext, FALSE, QUOTE_QUOTES, &conf);
	    fprintf(fp, "\"\n");
	} else if (p->type == para_Description) {
	    if (had_described_thing) {
		/*
		 * Do nothing; the .xP for this paragraph is the
		 * .IP which has come before it in the
		 * DescribedThing.
		 */
	    } else {
		/*
		 * A \dd without a preceding \dt is given a blank
		 * one.
		 */
		fprintf(fp, ".IP \"\"\n");
	    }
	} else if (p->type == para_BiblioCited) {
	    fprintf(fp, ".IP \"");
	    man_text(fp, p->kwtext, FALSE, QUOTE_QUOTES, &conf);
	    fprintf(fp, "\"\n");
	}
	man_text(fp, p->words, TRUE, 0, &conf);
	had_described_thing = FALSE;
	break;

      case para_DescribedThing:
	cleanup_described_thing;
	fprintf(fp, ".IP \"");
	man_text(fp, p->words, FALSE, QUOTE_QUOTES, &conf);
	fprintf(fp, "\"\n");
	had_described_thing = TRUE;
	break;

      case para_Rule:
	{
	    char *ruletext;
	    /*
	     * New paragraph containing a horizontal line 1/2em above
	     * the baseline, or a line of rule characters, whose
	     * length is the line length minus the current indent.
	     */
	    cleanup_described_thing;
	    man_convert(conf.rule, -1, &ruletext, 0, conf.charset, NULL);
	    fprintf(fp, ".PP\n.ie t \\u\\l'\\n(.lu-\\n(.iu'\\d\n"
		    ".el \\l'\\n(.lu-\\n(.iu\\&%s'\n", ruletext);
	    sfree(ruletext);
	}
	break;

      case para_LcontPush:
      case para_QuotePush:
	cleanup_described_thing;
	fprintf(fp, ".RS\n");
      	break;
      case para_LcontPop:
      case para_QuotePop:
	cleanup_described_thing;
	fprintf(fp, ".RE\n");
	break;
    }
    cleanup_described_thing;

    /*
     * Tidy up.
     */
    fclose(fp);
    man_conf_cleanup(conf);
}

/*
 * Convert a wide string into a string of chars; mallocs the
 * resulting string and stores a pointer to it in `*result'.
 * 
 * If `state' is non-NULL, updates the charset state pointed to. If
 * `state' is NULL, this function uses its own state, initialises
 * it from scratch, and cleans it up when finished. If `state' is
 * non-NULL but _s_ is NULL, cleans up a provided state.
 *
 * Return is nonzero if all characters are OK. If not all
 * characters are OK but `result' is non-NULL, a result _will_
 * still be generated!
 * 
 * This function also does escaping of groff special characters.
 */
static int man_convert(wchar_t const *s, int maxlen,
		       char **result, int quote_props,
		       int charset, charset_state *state) {
    charset_state internal_state = CHARSET_INIT_STATE;
    int slen, err;
    char *p = NULL, *q;
    int plen = 0, psize = 0;
    rdstringc out = {0, 0, NULL};
    int anyerr = 0;

    if (!state)
	state = &internal_state;

    slen = (s ? ustrlen(s) : 0);

    if (slen > maxlen && maxlen > 0)
	slen = maxlen;

    psize = 384;
    plen = 0;
    p = snewn(psize, char);
    err = 0;

    while (slen > 0) {
	int ret = charset_from_unicode(&s, &slen, p, psize,
				   charset, state, &err);
	plen = ret;

	for (q = p; q < p+plen; q++) {
	    if (q == p && (*q == '.' || *q == '\'') &&
		(quote_props & QUOTE_INITCTRL)) {
		/*
		 * Control character (. or ') at the start of a
		 * line. Quote it by putting \& (troff zero-width
		 * space) before it.
		 */
		rdaddc(&out, '\\');
		rdaddc(&out, '&');
	    } else if (*q == '`' || *q == ' ') {
		/* Quote backticks and nonbreakable spaces always. */
		rdaddc(&out, '\\');
	    } else if (*q == '\\') {
		/* Turn backslashes into \e. */
		rdaddsc(&out, "\\e");
		continue;
	    } else if (*q == '-') {
		/* Turn nonbreakable hyphens into \(hy. */
		rdaddsc(&out, "\\(hy");
		continue;
	    } else if (*q == '"' && (quote_props & QUOTE_QUOTES)) {
		/*
		 * Double quote within double quotes. Quote it by
		 * doubling.
		 */
		rdaddc(&out, '"');
	    }
	    rdaddc(&out, *q);
	}
	if (err) {
	    char const *tr = troffchar(*s);
	    if (tr == NULL)
		anyerr = TRUE;
	    else
		rdaddsc(&out, tr);
	    s++; slen--;
	}
	/* Past start of string -- no more quoting needed */
	quote_props &= ~QUOTE_INITCTRL;
    }

    if (state == &internal_state || s == NULL) {
	int ret = charset_from_unicode(NULL, 0, p+plen, psize-plen,
				       charset, state, NULL);
	if (ret > 0)
	    plen += ret;
    }

    sfree(p);

    if (out.text)
	*result = rdtrimc(&out);
    else
	*result = dupstr("");

    return !anyerr;
}

static int man_rdaddwc_reset(rdstringc *rs, int quote_props, manconfig *conf,
			     charset_state *state) {
    char *c;

    man_convert(NULL, 0, &c, quote_props, conf->charset, state);
    rdaddsc(rs, c);
    if (*c)
	quote_props &= ~QUOTE_INITCTRL;   /* not at start any more */
    sfree(c);
    *state = charset_init_state;
    return quote_props;
}

static int man_rdaddctrl(rdstringc *rs, char *c, int quote_props,
			 manconfig *conf, charset_state *state) {
    quote_props = man_rdaddwc_reset(rs, quote_props, conf, state);
    rdaddsc(rs, c);
    return quote_props;
}

static int man_rdaddwc(rdstringc *rs, word *text, word *end,
		       int quote_props, manconfig *conf,
		       charset_state *state) {
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
	     attraux(text->aux) == attr_Only)) {
	    quote_props = man_rdaddctrl(rs, "\\fI", quote_props, conf, state);
	} else if ((towordstyle(text->type) == word_Code ||
		    towordstyle(text->type) == word_WeakCode) &&
		   (attraux(text->aux) == attr_First ||
		    attraux(text->aux) == attr_Only)) {
	    quote_props = man_rdaddctrl(rs, "\\fB", quote_props, conf, state);
	}

	if (removeattr(text->type) == word_Normal) {
	    charset_state s2 = *state;
	    int len = ustrlen(text->text), hyphen = FALSE;

	    if (text->breaks && text->text[len - 1] == '-') {
		len--;
		hyphen = TRUE;
	    }
	    if (len == 0 ||
		man_convert(text->text, len, &c, quote_props, conf->charset,
			    &s2) ||
		!text->alt) {
		if (len != 0) {
		    rdaddsc(rs, c);
		    if (*c)
			quote_props &= ~QUOTE_INITCTRL;   /* not at start any more */
		    *state = s2;
		}
		if (hyphen) {
		    quote_props =
			man_rdaddctrl(rs, "-", quote_props, conf, state);
		    quote_props &= ~QUOTE_INITCTRL;
		}
	    } else {
		quote_props = man_rdaddwc(rs, text->alt, NULL,
					  quote_props, conf, state);
	    }
	    if (len != 0)
		sfree(c);
	} else if (removeattr(text->type) == word_WhiteSpace) {
	    quote_props = man_rdaddctrl(rs, " ", quote_props, conf, state);
	    quote_props &= ~QUOTE_INITCTRL;
	} else if (removeattr(text->type) == word_Quote) {
	    man_convert(quoteaux(text->aux) == quote_Open ?
			conf->lquote : conf->rquote, 0,
			&c, quote_props, conf->charset, state);
	    rdaddsc(rs, c);
	    if (*c)
		quote_props &= ~QUOTE_INITCTRL;   /* not at start any more */
	    sfree(c);
	}
	if (towordstyle(text->type) != word_Normal &&
	    (attraux(text->aux) == attr_Last ||
	     attraux(text->aux) == attr_Only)) {
	    quote_props = man_rdaddctrl(rs, "\\fP", quote_props, conf, state);
	}
	break;
    }
    quote_props = man_rdaddwc_reset(rs, quote_props, conf, state);

    return quote_props;
}

static void man_text(FILE *fp, word *text, int newline,
		     int quote_props, manconfig *conf) {
    rdstringc t = { 0, 0, NULL };
    charset_state state = CHARSET_INIT_STATE;

    man_rdaddwc(&t, text, NULL, quote_props | QUOTE_INITCTRL, conf, &state);
    fprintf(fp, "%s", t.text);
    sfree(t.text);
    if (newline)
	fputc('\n', fp);
}

static void man_codepara(FILE *fp, word *text, int charset) {
    fprintf(fp, ".nf\n");
    for (; text; text = text->next) if (text->type == word_WeakCode) {
	char *c;
	wchar_t *t, *e;
	int quote_props = QUOTE_INITCTRL;

	t = text->text;
	if (text->next && text->next->type == word_Emph) {
	    e = text->next->text;
	    text = text->next;
	} else
	    e = NULL;

	while (e && *e && *t) {
	    int n;
	    int ec = *e;

	    for (n = 0; t[n] && e[n] && e[n] == ec; n++);
	    if (ec == 'i')
		fprintf(fp, "\\fI");
	    else if (ec == 'b')
		fprintf(fp, "\\fB");
	    man_convert(t, n, &c, quote_props, charset, NULL);
	    quote_props &= ~QUOTE_INITCTRL;
	    fprintf(fp, "%s", c);
	    sfree(c);
	    if (ec == 'i' || ec == 'b')
		fprintf(fp, "\\fP");
	    t += n;
	    e += n;
	}
	man_convert(t, 0, &c, quote_props, charset, NULL);
	fprintf(fp, "%s\n", c);
	sfree(c);
    }
    fprintf(fp, ".fi\n");
}
