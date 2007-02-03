/*
 * input.c: read the source form
 */

#include <stdio.h>
#include <assert.h>
#include <time.h>
#include "halibut.h"

#define TAB_STOP 8		       /* for column number tracking */

static void setpos(input *in, char *fname) {
    in->pos.filename = fname;
    in->pos.line = 1;
    in->pos.col = (in->reportcols ? 1 : -1);
}

static void unget(input *in, int c, filepos *pos) {
    if (in->npushback >= in->pushbacksize) {
	in->pushbacksize = in->npushback + 16;
	in->pushback = sresize(in->pushback, in->pushbacksize, pushback);
    }
    in->pushback[in->npushback].chr = c;
    in->pushback[in->npushback].pos = *pos;   /* structure copy */
    in->npushback++;
}

/* ---------------------------------------------------------------------- */
/*
 * Macro subsystem
 */
typedef struct macro_Tag macro;
struct macro_Tag {
    wchar_t *name, *text;
};
struct macrostack_Tag {
    macrostack *next;
    wchar_t *text;
    int ptr, npushback;
    filepos pos;
};
static int macrocmp(void *av, void *bv) {
    macro *a = (macro *)av, *b = (macro *)bv;
    return ustrcmp(a->name, b->name);
}
static void macrodef(tree234 *macros, wchar_t *name, wchar_t *text,
		     filepos fpos) {
    macro *m = snew(macro);
    m->name = name;
    m->text = text;
    if (add234(macros, m) != m) {
	error(err_macroexists, &fpos, name);
	sfree(name);
	sfree(text);
    }
}
static int macrolookup(tree234 *macros, input *in, wchar_t *name,
		       filepos *pos) {
    macro m, *gotit;
    m.name = name;
    gotit = find234(macros, &m, NULL);
    if (gotit) {
	macrostack *expansion = snew(macrostack);
	expansion->next = in->stack;
	expansion->text = gotit->text;
	expansion->pos = *pos;	       /* structure copy */
	expansion->ptr = 0;
	expansion->npushback = in->npushback;
	in->stack = expansion;
	return TRUE;
    } else
	return FALSE;
}
static void macrocleanup(tree234 *macros) {
    int ti;
    macro *m;
    for (ti = 0; (m = (macro *)index234(macros, ti)) != NULL; ti++) {
	sfree(m->name);
	sfree(m->text);
	sfree(m);
    }
    freetree234(macros);
}

static void input_configure(input *in, paragraph *cfg) {
    assert(cfg->type == para_Config);

    if (!ustricmp(cfg->keyword, L"input-charset")) {
	in->charset = charset_from_ustr(&cfg->fpos, uadv(cfg->keyword));
    }
}

/*
 * Can return EOF
 */
static int get(input *in, filepos *pos, rdstringc *rsc) {
    int pushbackpt = in->stack ? in->stack->npushback : 0;
    if (in->npushback > pushbackpt) {
	--in->npushback;
	if (pos)
	    *pos = in->pushback[in->npushback].pos;   /* structure copy */
	return in->pushback[in->npushback].chr;
    }
    else if (in->stack) {
	wchar_t c = in->stack->text[in->stack->ptr];
        if (pos)
            *pos = in->stack->pos;
	if (in->stack->text[++in->stack->ptr] == L'\0') {
	    macrostack *tmp = in->stack;
	    in->stack = tmp->next;
	    sfree(tmp);
	}
	return c;
    }
    else if (in->currfp) {

	while (in->wcpos >= in->nwc) {

	    int c = getc(in->currfp);

	    if (c == EOF) {
		fclose(in->currfp);
		in->currfp = NULL;
		return EOF;
	    }

	    if (rsc)
		rdaddc(rsc, c);

	    /* Track line numbers, for error reporting */
	    if (pos)
		*pos = in->pos;
	    if (in->reportcols) {
		switch (c) {
		  case '\t':
		    in->pos.col = 1 + (in->pos.col + TAB_STOP-1) % TAB_STOP;
		    break;
		  case '\n':
		    in->pos.col = 1;
		    in->pos.line++;
		    break;
		  default:
		    in->pos.col++;
		    break;
		}
	    } else {
		in->pos.col = -1;
		if (c == '\n')
		    in->pos.line++;
	    }

	    /*
	     * Do input character set translation, so that we return
	     * Unicode.
	     */
	    {
		char buf[1];
		char const *p;
		int inlen;

		buf[0] = (char)c;
		p = buf;
		inlen = 1;

		in->nwc = charset_to_unicode(&p, &inlen,
					     in->wc, lenof(in->wc),
					     in->charset, &in->csstate,
					     NULL, 0);
		assert(p == buf+1 && inlen == 0);

		in->wcpos = 0;
	    }
	}

	return in->wc[in->wcpos++];

    } else
	return EOF;
}

/*
 * Lexical analysis of source files.
 */
typedef struct token_Tag token;
struct token_Tag {
    int type;
    int cmd, aux;
    wchar_t *text;
    char *origtext;
    filepos pos;
};
enum {
    tok_eof,			       /* end of file */
    tok_eop,			       /* end of paragraph */
    tok_white,			       /* whitespace */
    tok_word,			       /* a word or word fragment */
    tok_cmd,			       /* \command */
    tok_lbrace,			       /* { */
    tok_rbrace			       /* } */
};

/* Halibut command keywords. */
enum {
    c__invalid,			       /* invalid command */
    c__comment,			       /* comment command (\#) */
    c__escaped,			       /* escaped character */
    c__nop,			       /* no-op */
    c__nbsp,			       /* nonbreaking space */
    c_A,			       /* appendix heading */
    c_B,			       /* bibliography entry */
    c_BR,			       /* bibliography rewrite */
    c_C,			       /* chapter heading */
    c_H,			       /* heading */
    c_I,			       /* invisible index mark */
    c_IM,			       /* index merge/rewrite */
    c_K,			       /* capitalised cross-reference */
    c_S,			       /* aux field is 0, 1, 2, ... */
    c_U,			       /* unnumbered-chapter heading */
    c_W,			       /* Web hyperlink */
    c_b,			       /* bulletted list */
    c_c,			       /* code */
    c_cfg,			       /* configuration directive */
    c_copyright,		       /* copyright statement */
    c_cq,			       /* quoted code (sugar for \q{\cw{x}}) */
    c_cw,			       /* weak code */
    c_date,			       /* document processing date */
    c_dd,			       /* description list: description */
    c_define,			       /* macro definition */
    c_dt,			       /* description list: described thing */
    c_e,			       /* emphasis */
    c_i,			       /* visible index mark */
    c_ii,			       /* uncapitalised visible index mark */
    c_k,			       /* uncapitalised cross-reference */
    c_lcont,			       /* continuation para(s) for list item */
    c_n,			       /* numbered list */
    c_nocite,			       /* bibliography trickery */
    c_preamble,			       /* (obsolete) preamble text */
    c_q,			       /* quote marks */
    c_quote,			       /* block-quoted paragraphs */
    c_rule,			       /* horizontal rule */
    c_title,			       /* document title */
    c_u,			       /* aux field is char code */
    c_versionid			       /* document RCS id */
};

/* Perhaps whitespace should be defined in a more Unicode-friendly way? */
#define iswhite(c) ( (c)==32 || (c)==9 || (c)==13 || (c)==10 )
#define isnl(c) ( (c)==10 )
#define isdec(c) ( ((c)>='0'&&(c)<='9') )
#define fromdec(c) ( (c)-'0' )
#define ishex(c) ( ((c)>='0'&&(c)<='9') || ((c)>='A'&&(c)<='F') || ((c)>='a'&&(c)<='f'))
#define fromhex(c) ( (c)<='9' ? (c)-'0' : ((c)&0xDF) - ('A'-10) )
#define iscmd(c) ( ((c)>='0'&&(c)<='9') || ((c)>='A'&&(c)<='Z') || ((c)>='a'&&(c)<='z'))

/*
 * Keyword comparison function. Like strcmp, but between a wchar_t *
 * and a char *.
 */
static int kwcmp(wchar_t const *p, char const *q) {
    int i;
    do {
	i = *p - *q;
    } while (*p++ && *q++ && !i);
    return i;
}

/*
 * Match a keyword.
 */
static void match_kw(token *tok) {
    /*
     * FIXME. The ids are explicit in here so as to allow long-name
     * equivalents to the various very short keywords.
     */
    static const struct { char const *name; int id; } keywords[] = {
	{"#", c__comment},	       /* comment command (\#) */
	{"-", c__escaped},	       /* nonbreaking hyphen */
	{".", c__nop},		       /* no-op */
	{"A", c_A},		       /* appendix heading */
	{"B", c_B},		       /* bibliography entry */
	{"BR", c_BR},		       /* bibliography rewrite */
	{"C", c_C},		       /* chapter heading */
	{"H", c_H},		       /* heading */
	{"I", c_I},		       /* invisible index mark */
	{"IM", c_IM},		       /* index merge/rewrite */
	{"K", c_K},		       /* capitalised cross-reference */
	{"U", c_U},		       /* unnumbered-chapter heading */
	{"W", c_W},		       /* Web hyperlink */
	{"\\", c__escaped},	       /* escaped backslash (\\) */
	{"_", c__nbsp},		       /* nonbreaking space (\_) */
	{"b", c_b},		       /* bulletted list */
	{"c", c_c},		       /* code */
	{"cfg", c_cfg},		       /* configuration directive */
	{"copyright", c_copyright},    /* copyright statement */
	{"cq", c_cq},		       /* quoted code (sugar for \q{\cw{x}}) */
	{"cw", c_cw},		       /* weak code */
	{"date", c_date},	       /* document processing date */
	{"dd", c_dd},		       /* description list: description */
	{"define", c_define},	       /* macro definition */
	{"dt", c_dt},		       /* description list: described thing */
	{"e", c_e},		       /* emphasis */
	{"i", c_i},		       /* visible index mark */
	{"ii", c_ii},		       /* uncapitalised visible index mark */
	{"k", c_k},		       /* uncapitalised cross-reference */
	{"lcont", c_lcont},	       /* continuation para(s) for list item */
	{"n", c_n},		       /* numbered list */
	{"nocite", c_nocite},	       /* bibliography trickery */
	{"preamble", c_preamble},      /* (obsolete) preamble text */
	{"q", c_q},		       /* quote marks */
	{"quote", c_quote},	       /* block-quoted paragraphs */
	{"rule", c_rule},	       /* horizontal rule */
	{"title", c_title},	       /* document title */
	{"versionid", c_versionid},    /* document RCS id */
	{"{", c__escaped},	       /* escaped lbrace (\{) */
	{"}", c__escaped},	       /* escaped rbrace (\}) */
    };
    int i, j, k, c;

    /*
     * Special cases: \S{0,1,2,...} and \uABCD. If the syntax
     * doesn't match correctly, we just fall through to the
     * binary-search phase.
     */
    if (tok->text[0] == 'S') {
	/* We expect numeric characters thereafter. */
	wchar_t *p = tok->text+1;
	int n;
	if (!*p)
	    n = 1;
	else {
	    n = 0;
	    while (*p && isdec(*p)) {
		n = 10 * n + fromdec(*p);
		p++;
	    }
	}
	if (!*p) {
	    tok->cmd = c_S;
	    tok->aux = n;
	    return;
	}
    } else if (tok->text[0] == 'u') {
	/* We expect hex characters thereafter. */
	wchar_t *p = tok->text+1;
	int n = 0;
	while (*p && ishex(*p)) {
	    n = 16 * n + fromhex(*p);
	    p++;
	}
	if (!*p) {
	    tok->cmd = c_u;
	    tok->aux = n;
	    return;
	}
    }

    i = -1;
    j = sizeof(keywords)/sizeof(*keywords);
    while (j-i > 1) {
	k = (i+j)/2;
	c = kwcmp(tok->text, keywords[k].name);
	if (c < 0)
	    j = k;
	else if (c > 0)
	    i = k;
	else /* c == 0 */ {
	    tok->cmd = keywords[k].id;
	    return;
	}
    }

    tok->cmd = c__invalid;
}


/*
 * Read a token from the input file, in the normal way (`normal' in
 * the sense that code paragraphs work a different way).
 */
token get_token(input *in) {
    int c;
    int nls;
    int prevpos;
    token ret;
    rdstring rs = { 0, 0, NULL };
    rdstringc rsc = { 0, 0, NULL };
    filepos cpos;

    ret.text = NULL;		       /* default */
    ret.origtext = NULL;	       /* default */
    if (in->pushback_chars) {
	rdaddsc(&rsc, in->pushback_chars);
	sfree(in->pushback_chars);
	in->pushback_chars = NULL;
    }
    c = get(in, &cpos, &rsc);
    ret.pos = cpos;
    if (iswhite(c)) {		       /* tok_white or tok_eop */
	nls = 0;
	prevpos = 0;
	do {
	    if (isnl(c))
		nls++;
	    prevpos = rsc.pos;
	} while ((c = get(in, &cpos, &rsc)) != EOF && iswhite(c));
	if (c == EOF) {
	    ret.type = tok_eof;
	    sfree(rsc.text);
	    return ret;
	}
	if (rsc.text) {
	    in->pushback_chars = dupstr(rsc.text + prevpos);
	    sfree(rsc.text);
	}
	unget(in, c, &cpos);
	ret.type = (nls > 1 ? tok_eop : tok_white);
	return ret;
    } else if (c == EOF) {	       /* tok_eof */
	ret.type = tok_eof;
	sfree(rsc.text);
	return ret;
    } else if (c == '\\') {	       /* tok_cmd */
	rsc.pos = prevpos = 0;
	c = get(in, &cpos, &rsc);
	if (c == '-' || c == '\\' || c == '_' ||
	    c == '#' || c == '{' || c == '}' || c == '.') {
	    /* single-char command */
	    rdadd(&rs, c);
	    prevpos = rsc.pos;
	} else if (c == 'u') {
	    int len = 0;
	    do {
		rdadd(&rs, c);
		len++;
		prevpos = rsc.pos;
		c = get(in, &cpos, &rsc);
	    } while (ishex(c) && len < 5);
	    unget(in, c, &cpos);
	} else if (iscmd(c)) {
	    do {
		rdadd(&rs, c);
		prevpos = rsc.pos;
		c = get(in, &cpos, &rsc);
	    } while (iscmd(c));
	    unget(in, c, &cpos);
	}
	/*
	 * Now match the command against the list of available
	 * ones.
	 */
	ret.type = tok_cmd;
	ret.text = ustrdup(rs.text);
	if (rsc.text) {
	    in->pushback_chars = dupstr(rsc.text + prevpos);
	    rsc.text[prevpos] = '\0';
	    ret.origtext = dupstr(rsc.text);
	} else {
	    ret.origtext = dupstr("");
	}
	match_kw(&ret);
	sfree(rs.text);
	sfree(rsc.text);
	return ret;
    } else if (c == '{') {	       /* tok_lbrace */
	ret.type = tok_lbrace;
	sfree(rsc.text);
	return ret;
    } else if (c == '}') {	       /* tok_rbrace */
	ret.type = tok_rbrace;
	sfree(rsc.text);
	return ret;
    } else {			       /* tok_word */
	/*
	 * Read a word: the longest possible contiguous sequence of
	 * things other than whitespace, backslash, braces and
	 * hyphen. A hyphen terminates the word but is returned as
	 * part of it; everything else is pushed back for the next
	 * token. The `aux' field contains TRUE if the word ends in
	 * a hyphen.
	 */
	ret.aux = FALSE;	       /* assumed for now */
	prevpos = 0;
	while (1) {
	    if (iswhite(c) || c=='{' || c=='}' || c=='\\' || c==EOF) {
		/* Put back the character that caused termination */
		unget(in, c, &cpos);
		break;
	    } else {
		rdadd(&rs, c);
		if (c == '-') {
		    prevpos = rsc.pos;
		    ret.aux = TRUE;
		    break;	       /* hyphen terminates word */
		}
	    }
	    prevpos = rsc.pos;
	    c = get(in, &cpos, &rsc);
	}
	ret.type = tok_word;
	ret.text = ustrdup(rs.text);
	if (rsc.text) {
	    in->pushback_chars = dupstr(rsc.text + prevpos);
	    rsc.text[prevpos] = '\0';
	    ret.origtext = dupstr(rsc.text);
	} else {
	    ret.origtext = dupstr("");
	}
	sfree(rs.text);
	sfree(rsc.text);
	return ret;
    }
}

/*
 * Determine whether the next input character is an open brace (for
 * telling code paragraphs from paragraphs which merely start with
 * code).
 */
int isbrace(input *in) {
    int c;
    filepos cpos;

    c = get(in, &cpos, NULL);
    unget(in, c, &cpos);
    return (c == '{');
}

/*
 * Read the rest of a line that starts `\c'. Including nothing at
 * all (tok_word with empty text).
 */
token get_codepar_token(input *in) {
    int c;
    token ret;
    rdstring rs = { 0, 0, NULL };
    filepos cpos;

    ret.type = tok_word;
    ret.origtext = NULL;
    c = get(in, &cpos, NULL);	       /* expect (and discard) one space */
    ret.pos = cpos;
    if (c == ' ') {
	c = get(in, &cpos, NULL);
	ret.pos = cpos;
    }
    while (!isnl(c) && c != EOF) {
	int c2 = c;
	c = get(in, &cpos, NULL);
	/* Discard \r just before \n. */
	if (c2 != 13 || !isnl(c))
	    rdadd(&rs, c2);
    }
    unget(in, c, &cpos);
    ret.text = ustrdup(rs.text);
    sfree(rs.text);
    return ret;
}

/*
 * Adds a new word to a linked list
 */
static word *addword(word newword, word ***hptrptr) {
    word *mnewword;
    if (!hptrptr)
	return NULL;
    mnewword = snew(word);
    *mnewword = newword;	       /* structure copy */
    mnewword->next = NULL;
    **hptrptr = mnewword;
    *hptrptr = &mnewword->next;
    return mnewword;
}

/*
 * Adds a new paragraph to a linked list
 */
static paragraph *addpara(paragraph newpara, paragraph ***hptrptr) {
    paragraph *mnewpara = snew(paragraph);
    *mnewpara = newpara;	       /* structure copy */
    mnewpara->next = NULL;
    **hptrptr = mnewpara;
    *hptrptr = &mnewpara->next;
    return mnewpara;
}

/*
 * Destructor before token is reassigned; should catch most memory
 * leaks
 */
#define dtor(t) ( sfree(t.text), sfree(t.origtext) )

/*
 * Reads a single file (ie until get() returns EOF)
 */
static void read_file(paragraph ***ret, input *in, indexdata *idx,
		      tree234 *macros) {
    token t;
    paragraph par;
    word wd, **whptr, **idximplicit;
    wchar_t utext[2], *wdtext;
    int style, spcstyle;
    int already;
    int iswhite, seenwhite;
    int type;
    int prev_para_type;
    struct stack_item {
	enum {
	    stack_nop = 0,	       /* do nothing (for error recovery) */
	    stack_ualt = 1,	       /* \u alternative */
	    stack_style = 2,	       /* \e, \c, \cw */
	    stack_idx = 4,	       /* \I, \i, \ii */
	    stack_hyper = 8,	       /* \W */
	    stack_quote = 16	       /* \q */
	} type;
	word **whptr;		       /* to restore from \u alternatives */
	word **idximplicit;	       /* to restore from \u alternatives */
	filepos fpos;
	int in_code;
    } *sitem;
    stack parsestk;
    struct crossparaitem {
 	int type;		       /* currently c_lcont, c_quote or -1 */
	int seen_lcont, seen_quote;
    };
    stack crossparastk;
    word *indexword, *uword, *iword;
    word *idxwordlist;
    rdstring indexstr;
    int index_downcase, index_visible, indexing;
    const rdstring nullrs = { 0, 0, NULL };
    wchar_t uchr;

    t.text = NULL;
    t.origtext = NULL;
    already = FALSE;

    crossparastk = stk_new();

    /*
     * Loop on each paragraph.
     */
    while (1) {
	int start_cmd = c__invalid;
	par.words = NULL;
	par.keyword = NULL;
	par.origkeyword = NULL;
	whptr = &par.words;

	/*
	 * Get a token.
	 */
	do {
	    if (!already) {
		dtor(t), t = get_token(in);
	    }
	    already = FALSE;
	} while (t.type == tok_eop);
	if (t.type == tok_eof)
	    break;

	/*
	 * Parse code paragraphs separately.
	 */
	if (t.type == tok_cmd && t.cmd == c_c && !isbrace(in)) {
	    int wtype = word_WeakCode;

	    par.type = para_Code;
	    par.fpos = t.pos;
	    while (1) {
		dtor(t), t = get_codepar_token(in);
		wd.type = wtype;
		wd.breaks = FALSE;     /* shouldn't need this... */
		wd.text = ustrdup(t.text);
		wd.alt = NULL;
		wd.fpos = t.pos;
		addword(wd, &whptr);
		dtor(t), t = get_token(in);
		if (t.type == tok_white) {
		    /*
		     * The newline after a code-paragraph line
		     */
		    dtor(t), t = get_token(in);
		}
		if (t.type == tok_eop || t.type == tok_eof ||
		    t.type == tok_rbrace) { /* might be } terminating \lcont */
		    if (t.type == tok_rbrace)
			already = TRUE;
		    break;
		} else if (t.type == tok_cmd && t.cmd == c_c) {
		    wtype = word_WeakCode;
		} else if (t.type == tok_cmd && t.cmd == c_e &&
			   wtype == word_WeakCode) {
		    wtype = word_Emph;
		} else {
		    error(err_brokencodepara, &t.pos);
		    prev_para_type = par.type;
		    addpara(par, ret);
		    while (t.type != tok_eop)   /* error recovery: */
			dtor(t), t = get_token(in);   /* eat rest of paragraph */
		    goto codeparabroken;   /* ick, but such is life */
		}
	    }
	    prev_para_type = par.type;
	    addpara(par, ret);
	    codeparabroken:
	    continue;
	}

	/*
	 * Spot the special commands that define a grouping of more
	 * than one paragraph, and also the closing braces that
	 * finish them.
	 */
	if (t.type == tok_cmd &&
	    (t.cmd == c_lcont || t.cmd == c_quote)) {
	    struct crossparaitem *sitem, *stop;
	    int cmd = t.cmd;

	    /*
	     * Expect, and swallow, an open brace.
	     */
	    dtor(t), t = get_token(in);
	    if (t.type != tok_lbrace) {
		error(err_explbr, &t.pos);
		continue;
	    }

	    /*
	     * Also expect, and swallow, any whitespace after that
	     * (a newline before a code paragraph wouldn't be
	     * surprising).
	     */
	    do {
		dtor(t), t = get_token(in);
	    } while (t.type == tok_white);
	    already = TRUE;

	    if (cmd == c_lcont) {
		/*
		 * \lcont causes a continuation of a list item into
		 * multiple paragraphs (which may in turn contain
		 * nested lists, code paras etc). Hence, the previous
		 * paragraph must be of a list type.
		 */
		sitem = snew(struct crossparaitem);
		stop = (struct crossparaitem *)stk_top(crossparastk);
		if (stop)
		    *sitem = *stop;
		else
		    sitem->seen_quote = sitem->seen_lcont = 0;

		if (prev_para_type == para_Bullet ||
		    prev_para_type == para_NumberedList ||
		    prev_para_type == para_Description) {
		    sitem->type = c_lcont;
		    sitem->seen_lcont = 1;
		    par.type = para_LcontPush;
		    prev_para_type = par.type;
		    addpara(par, ret);
		} else {
		    /*
		     * Push a null item on the cross-para stack so that
		     * when we see the corresponding closing brace we
		     * don't give a cascade error.
		     */
		    sitem->type = -1;
		    error(err_misplacedlcont, &t.pos);
		}
	    } else {
		/*
		 * \quote causes a group of paragraphs to be
		 * block-quoted (typically they will be indented a
		 * bit).
		 */
		sitem = snew(struct crossparaitem);
		stop = (struct crossparaitem *)stk_top(crossparastk);
		if (stop)
		    *sitem = *stop;
		else
		    sitem->seen_quote = sitem->seen_lcont = 0;
		sitem->type = c_quote;
		sitem->seen_quote = 1;
		par.type = para_QuotePush;
		prev_para_type = par.type;
		addpara(par, ret);
	    }
	    stk_push(crossparastk, sitem);
	    continue;
	} else if (t.type == tok_rbrace) {
	    struct crossparaitem *sitem = stk_pop(crossparastk);
	    if (!sitem)
		error(err_unexbrace, &t.pos);
	    else {
		switch (sitem->type) {
		  case c_lcont:
		    par.type = para_LcontPop;
		    prev_para_type = par.type;
		    addpara(par, ret);
		    break;
		  case c_quote:
		    par.type = para_QuotePop;
		    prev_para_type = par.type;
		    addpara(par, ret);
		    break;
		}
		sfree(sitem);
	    }
	    continue;
	}

	while (t.type == tok_cmd &&
	       macrolookup(macros, in, t.text, &t.pos)) {
	    dtor(t), t = get_token(in);
	}

	/*
	 * This token begins a paragraph. See if it's one of the
	 * special commands that define a paragraph type.
	 *
	 * (note that \# is special in a way, and \nocite takes no
	 * text)
	 */
	par.type = para_Normal;
	if (t.type == tok_cmd) {
	    int needkw;
	    int is_macro = FALSE;

	    par.fpos = t.pos;
	    switch (t.cmd) {
	      default:
		needkw = -1;
		break;
	      case c__invalid:
		error(err_badparatype, t.text, &t.pos);
		needkw = 4;
		break;
	      case c__comment:
		if (isbrace(in))
		    break;	       /* `\#{': isn't a comment para */
		do {
		    dtor(t), t = get_token(in);
		} while (t.type != tok_eop && t.type != tok_eof);
		continue;	       /* next paragraph */
		/*
		 * `needkw' values:
		 *
		 *   1 -- exactly one keyword
		 *   2 -- at least one keyword
		 *   4 -- any number of keywords including zero
		 *   8 -- at least one keyword and then nothing else
		 *  16 -- nothing at all! no keywords, no body
		 *  32 -- no keywords at all
		 */
	      case c_A: needkw = 2; par.type = para_Appendix; break;
	      case c_B: needkw = 2; par.type = para_Biblio; break;
	      case c_BR: needkw = 1; par.type = para_BR;
		start_cmd = c_BR; break;
	      case c_C: needkw = 2; par.type = para_Chapter; break;
	      case c_H: needkw = 2; par.type = para_Heading;
		par.aux = 0;
		break;
	      case c_IM: needkw = 2; par.type = para_IM;
		start_cmd = c_IM; break;
	      case c_S: needkw = 2; par.type = para_Subsect;
		par.aux = t.aux; break;
	      case c_U: needkw = 32; par.type = para_UnnumberedChapter; break;
		/* For \b and \n the keyword is optional */
	      case c_b: needkw = 4; par.type = para_Bullet; break;
	      case c_dt: needkw = 4; par.type = para_DescribedThing; break;
	      case c_dd: needkw = 4; par.type = para_Description; break;
	      case c_n: needkw = 4; par.type = para_NumberedList; break;
	      case c_cfg: needkw = 8; par.type = para_Config;
		start_cmd = c_cfg; break;
	      case c_copyright: needkw = 32; par.type = para_Copyright; break;
	      case c_define: is_macro = TRUE; needkw = 1; break;
		/* For \nocite the keyword is _everything_ */
	      case c_nocite: needkw = 8; par.type = para_NoCite; break;
	      case c_preamble: needkw = 32; par.type = para_Normal; break;
	      case c_rule: needkw = 16; par.type = para_Rule; break;
	      case c_title: needkw = 32; par.type = para_Title; break;
	      case c_versionid: needkw = 32; par.type = para_VersionID; break;
	    }

	    if (par.type == para_Chapter ||
		par.type == para_Heading ||
		par.type == para_Subsect ||
		par.type == para_Appendix ||
		par.type == para_UnnumberedChapter) {
		struct crossparaitem *sitem = stk_top(crossparastk);
		if (sitem && (sitem->seen_lcont || sitem->seen_quote)) {
		    error(err_sectmarkerinblock,
			  &t.pos,
			  (sitem->seen_lcont ? "lcont" : "quote"));
		}
	    }

	    if (needkw > 0) {
		rdstring rs = { 0, 0, NULL };
		rdstringc rsc = { 0, 0, NULL };
		int nkeys = 0;
		filepos fp;

		/* Get keywords. */
		dtor(t), t = get_token(in);
		fp = t.pos;
		while (t.type == tok_lbrace ||
		       (t.type == tok_white && (needkw & 24))) {
		    /*
		     * In paragraph types which can't accept any
		     * body text (such as \cfg), we are lenient
		     * about whitespace between keywords. This is
		     * important for \cfg in particular since it
		     * can often have many keywords which are long
		     * pieces of text, so it's useful to permit the
		     * user to wrap the line between them.
		     */
		    if (t.type == tok_white) {
			dtor(t), t = get_token(in); /* eat the space */
			continue;
		    }
		    /* This is a keyword. */
		    nkeys++;
		    /* FIXME: there will be bugs if anyone specifies an
		     * empty keyword (\foo{}), so trap this case. */
		    while (dtor(t), t = get_token(in),
			   t.type == tok_word || 
			   t.type == tok_white ||
			   (t.type == tok_cmd && t.cmd == c__nbsp) ||
			   (t.type == tok_cmd && t.cmd == c__escaped) ||
			   (t.type == tok_cmd && t.cmd == c_u)) {
			if (t.type == tok_white ||
			    (t.type == tok_cmd && t.cmd == c__nbsp)) {
			    rdadd(&rs, ' ');
			    rdaddc(&rsc, ' ');
			} else if (t.type == tok_cmd && t.cmd == c_u) {
			    rdadd(&rs, t.aux);
			    rdaddc(&rsc, '\\');
			    rdaddsc(&rsc, t.origtext);
			} else {
			    rdadds(&rs, t.text);
			    rdaddsc(&rsc, t.origtext);
			}
		    }
		    if (t.type != tok_rbrace) {
			error(err_kwunclosed, &t.pos);
			continue;
		    }
		    rdadd(&rs, 0);     /* add string terminator */
		    rdaddc(&rsc, 0);   /* add string terminator */
		    dtor(t), t = get_token(in); /* eat right brace */
		}

		rdadd(&rs, 0);	       /* add string terminator */
		rdaddc(&rsc, 0);       /* add string terminator */

		/* See whether we have the right number of keywords. */
		if ((needkw & 48) && nkeys > 0)
		    error(err_kwillegal, &fp);
		if ((needkw & 11) && nkeys == 0)
		    error(err_kwexpected, &fp);
		if ((needkw & 5) && nkeys > 1)
		    error(err_kwtoomany, &fp);

		if (is_macro) {
		    /*
		     * Macro definition. Get the rest of the line
		     * as a code-paragraph token, repeatedly until
		     * there's nothing more left of it. Separate
		     * with newlines.
		     */
		    rdstring macrotext = { 0, 0, NULL };
		    while (1) {
			dtor(t), t = get_codepar_token(in);
			if (macrotext.pos > 0)
			    rdadd(&macrotext, L'\n');
			rdadds(&macrotext, t.text);
			dtor(t), t = get_token(in);
			if (t.type == tok_eop) break;
		    }
		    macrodef(macros, rs.text, macrotext.text, fp);
		    continue;	       /* next paragraph */
		}

		par.keyword = rdtrim(&rs);
		par.origkeyword = rdtrimc(&rsc);

		/* Move to EOP in case of needkw==8 or 16 (no body) */
		if (needkw & 24) {
		    /* We allow whitespace even when we expect no para body */
		    while (t.type == tok_white)
			dtor(t), t = get_token(in);
		    if (t.type != tok_eop && t.type != tok_eof &&
			(start_cmd == c__invalid ||
			 t.type != tok_cmd || t.cmd != start_cmd)) {
			error(err_bodyillegal, &t.pos);
			/* Error recovery: eat the rest of the paragraph */
			while (t.type != tok_eop && t.type != tok_eof &&
			       (start_cmd == c__invalid ||
				t.type != tok_cmd || t.cmd != start_cmd))
			    dtor(t), t = get_token(in);
		    }
		    if (t.type == tok_cmd)
			already = TRUE;/* inhibit get_token at top of loop */
		    prev_para_type = par.type;
		    addpara(par, ret);

		    if (par.type == para_Config) {
			input_configure(in, &par);
		    }
		    continue;	       /* next paragraph */
		}
	    }
	}		  

	/*
	 * Now read the actual paragraph, word by word, adding to
	 * the paragraph list.
	 *
	 * Mid-paragraph commands:
	 *
	 *  \K \k
	 *  \c \cw \cq
	 *  \e
	 *  \i \ii
	 *  \I
         *  \q
	 *  \u
	 *  \W
	 *  \date
	 *  \\ \{ \}
	 */
	parsestk = stk_new();
	style = word_Normal;
	spcstyle = word_WhiteSpace;
	indexing = FALSE;
	seenwhite = TRUE;
	while (t.type != tok_eop && t.type != tok_eof) {
	    iswhite = FALSE;
	    already = FALSE;

	    /* Handle implicit paragraph breaks after \IM, \BR etc */
	    if (start_cmd != c__invalid &&
		t.type == tok_cmd && t.cmd == start_cmd) {
		already = TRUE;	       /* inhibit get_token at top of loop */
		break;
	    }

	    if (t.type == tok_cmd && t.cmd == c__nop) {
		dtor(t), t = get_token(in);
		continue;	       /* do nothing! */
	    }

	    if (t.type == tok_cmd && t.cmd == c__escaped) {
		t.type = tok_word;     /* nice and simple */
		t.aux = 0;	       /* even if `\-' - nonbreaking! */
	    }
	    if (t.type == tok_cmd && t.cmd == c__nbsp) {
		t.type = tok_word;     /* nice and simple */
		sfree(t.text);
		t.text = ustrdup(L" ");  /* text is ` ' not `_' */
		t.aux = 0;	       /* (nonbreaking) */
	    }
	    switch (t.type) {
	      case tok_white:
		if (whptr == &par.words)
		    break;	       /* strip whitespace at start of para */
		wd.text = NULL;
		wd.type = spcstyle;
		wd.alt = NULL;
		wd.aux = 0;
		wd.fpos = t.pos;
		wd.breaks = FALSE;

		/*
		 * Inhibit use of whitespace if it's (probably the
		 * newline) before a repeat \IM / \BR type
		 * directive.
		 */
		if (start_cmd != c__invalid) {
		    dtor(t), t = get_token(in);
		    already = TRUE;
		    if (t.type == tok_cmd && t.cmd == start_cmd)
			break;
		}

		if (indexing)
		    rdadd(&indexstr, ' ');
		if (!indexing || index_visible)
		    addword(wd, &whptr);
		if (indexing)
		    addword(wd, &idximplicit);
		iswhite = TRUE;
		break;
	      case tok_word:
		if (indexing)
		    rdadds(&indexstr, t.text);
		wd.type = style;
		wd.alt = NULL;
		wd.aux = 0;
		wd.fpos = t.pos;
		wd.breaks = t.aux;
		if (!indexing || index_visible) {
		    wd.text = ustrdup(t.text);
		    addword(wd, &whptr);
		}
		if (indexing) {
		    wd.text = ustrdup(t.text);
		    addword(wd, &idximplicit);
		}
		break;
	      case tok_lbrace:
		error(err_unexbrace, &t.pos);
		/* Error recovery: push nop */
		sitem = snew(struct stack_item);
		sitem->type = stack_nop;
		sitem->fpos = t.pos;
		stk_push(parsestk, sitem);
		break;
	      case tok_rbrace:
		sitem = stk_pop(parsestk);
		if (!sitem) {
		    /*
		     * This closing brace could have been an
		     * indication that the cross-paragraph stack
		     * wants popping. Accordingly, we treat it here
		     * as an indication that the paragraph is over.
		     */
		    already = TRUE;
		    goto finished_para;
		} else {
		    if (sitem->type & stack_ualt) {
			whptr = sitem->whptr;
			idximplicit = sitem->idximplicit;
		    }
		    if (sitem->type & stack_style) {
			style = word_Normal;
			spcstyle = word_WhiteSpace;
		    }
		    if (sitem->type & stack_idx) {
			indexword->text = ustrdup(indexstr.text);
			if (index_downcase) {
			    word *w;

			    ustrlow(indexword->text);
			    ustrlow(indexstr.text);

			    for (w = idxwordlist; w; w = w->next)
				if (w->text)
				    ustrlow(w->text);
			}
			indexing = FALSE;
			rdadd(&indexstr, L'\0');
			index_merge(idx, FALSE, indexstr.text,
				    idxwordlist, &sitem->fpos);
			sfree(indexstr.text);
		    }
		    if (sitem->type & stack_hyper) {
			wd.text = NULL;
			wd.type = word_HyperEnd;
			wd.alt = NULL;
			wd.aux = 0;
			wd.fpos = t.pos;
			wd.breaks = FALSE;
			if (!indexing || index_visible)
			    addword(wd, &whptr);
			if (indexing)
			    addword(wd, &idximplicit);
		    }
		    if (sitem->type & stack_quote) {
			wd.text = NULL;
			wd.type = toquotestyle(style);
			wd.alt = NULL;
			wd.aux = quote_Close;
			wd.fpos = t.pos;
			wd.breaks = FALSE;
			if (!indexing || index_visible)
			    addword(wd, &whptr);
			if (indexing) {
			    rdadd(&indexstr, L'"');
			    addword(wd, &idximplicit);
			}
		    }
		}
		sfree(sitem);
		break;
	      case tok_cmd:
		switch (t.cmd) {
		  case c__comment:
		    /*
		     * In-paragraph comment: \#{ balanced braces }
		     *
		     * Anything goes here; even tok_eop. We should
		     * eat whitespace after the close brace _if_
		     * there was whitespace before the \#.
		     */
		    dtor(t), t = get_token(in);
		    if (t.type != tok_lbrace) {
			error(err_explbr, &t.pos);
		    } else {
			int braces = 1;
			while (braces > 0) {
			    dtor(t), t = get_token(in);
			    if (t.type == tok_lbrace)
				braces++;
			    else if (t.type == tok_rbrace)
				braces--;
			    else if (t.type == tok_eof) {
				error(err_commenteof, &t.pos);
				break;
			    }
			}
		    }
		    if (seenwhite) {
			already = TRUE;
			dtor(t), t = get_token(in);
			if (t.type == tok_white) {
			    iswhite = TRUE;
			    already = FALSE;
			}
		    }
		    break;
		  case c_q:
                  case c_cq:
                    type = t.cmd;
		    dtor(t), t = get_token(in);
		    if (t.type != tok_lbrace) {
			error(err_explbr, &t.pos);
		    } else {
			/*
			 * Enforce that \q may not be used anywhere
			 * within \c. (It shouldn't be necessary
			 * since the whole point of \c should be
			 * that the user wants to exercise exact
			 * control over the glyphs used, and
			 * forbidding it has the useful effect of
			 * relieving some backends of having to
			 * make difficult decisions.)
			 */
			int stype;

			if (style != word_Code && style != word_WeakCode) {
			    wd.text = NULL;
			    wd.type = toquotestyle(style);
			    wd.alt = NULL;
			    wd.aux = quote_Open;
			    wd.fpos = t.pos;
			    wd.breaks = FALSE;
			    if (!indexing || index_visible)
				addword(wd, &whptr);
			    if (indexing) {
				rdadd(&indexstr, L'"');
				addword(wd, &idximplicit);
			    }
			    stype = stack_quote;
			} else {
			    error(err_codequote, &t.pos);
			    stype = stack_nop;
			}
			sitem = snew(struct stack_item);
			sitem->fpos = t.pos;
			sitem->type = stype;
                        if (type == c_cq) {
                            if (style != word_Normal) {
                                error(err_nestedstyles, &t.pos);
                            } else {
                                style = word_WeakCode;
                                spcstyle = tospacestyle(style);
                                sitem->type |= stack_style;
                            }
                        }
			stk_push(parsestk, sitem);
		    }
		    break;
		  case c_K:
		  case c_k:
		  case c_W:
		  case c_date:
		    /*
		     * Keyword, hyperlink, or \date. We expect a
		     * left brace, some text, and then a right
		     * brace. No nesting; no arguments.
		     */
		    wd.fpos = t.pos;
		    wd.breaks = FALSE;
		    if (t.cmd == c_K)
			wd.type = word_UpperXref;
		    else if (t.cmd == c_k)
			wd.type = word_LowerXref;
		    else if (t.cmd == c_W)
			wd.type = word_HyperLink;
		    else
			wd.type = word_Normal;
		    dtor(t), t = get_token(in);
		    if (t.type != tok_lbrace) {
			if (wd.type == word_Normal) {
			    time_t thetime = time(NULL);
			    struct tm *broken = localtime(&thetime);
			    already = TRUE;
			    wdtext = ustrftime(NULL, broken);
			    wd.type = style;
			} else {
			    error(err_explbr, &t.pos);
			    wdtext = NULL;
			}
		    } else {
			rdstring rs = { 0, 0, NULL };
			while (dtor(t), t = get_token(in),
			       t.type == tok_word || t.type == tok_white) {
			    if (t.type == tok_white)
				rdadd(&rs, ' ');
			    else
				rdadds(&rs, t.text);
			}
			if (wd.type == word_Normal) {
			    time_t thetime = time(NULL);
			    struct tm *broken = localtime(&thetime);
			    wdtext = ustrftime(rs.text, broken);
			    wd.type = style;
			} else {
			    wdtext = ustrdup(rs.text);
			}
			sfree(rs.text);
			if (t.type != tok_rbrace) {
			    error(err_kwexprbr, &t.pos);
			}
		    }
		    wd.alt = NULL;
		    wd.aux = 0;
		    if (!indexing || index_visible) {
			wd.text = ustrdup(wdtext);
			addword(wd, &whptr);
		    }
		    if (indexing) {
			wd.text = ustrdup(wdtext);
			addword(wd, &idximplicit);
		    }
		    sfree(wdtext);
		    if (wd.type == word_HyperLink) {
			/*
			 * Hyperlinks are different: they then
			 * expect another left brace, to begin
			 * delimiting the text marked by the link.
			 */
			dtor(t), t = get_token(in);
			sitem = snew(struct stack_item);
			sitem->fpos = wd.fpos;
			sitem->type = stack_hyper;
			/*
			 * Special cases: \W{}\i, \W{}\ii
			 */
			if (t.type == tok_cmd &&
			    (t.cmd == c_i || t.cmd == c_ii)) {
			    if (indexing) {
				error(err_nestedindex, &t.pos);
			    } else {
				/* Add an index-reference word with no
				 * text as yet */
				wd.type = word_IndexRef;
				wd.text = NULL;
				wd.alt = NULL;
				wd.aux = 0;
				wd.breaks = FALSE;
				indexword = addword(wd, &whptr);
				/* Set up a rdstring to read the
				 * index text */
				indexstr = nullrs;
				/* Flags so that we do the Right
				 * Things with text */
				index_visible = (type != c_I);
				index_downcase = (type == c_ii);
				indexing = TRUE;
				idxwordlist = NULL;
				idximplicit = &idxwordlist;

				sitem->type |= stack_idx;
			    }
			    dtor(t), t = get_token(in);
			}
			/*
			 * Special cases: \W{}\c, \W{}\e, \W{}\cw
			 */
			if (t.type == tok_cmd &&
			    (t.cmd == c_e || t.cmd == c_c || t.cmd == c_cw)) {
			    if (style != word_Normal)
				error(err_nestedstyles, &t.pos);
			    else {
				style = (t.cmd == c_c ? word_Code :
					 t.cmd == c_cw ? word_WeakCode :
					 word_Emph);
				spcstyle = tospacestyle(style);
				sitem->type |= stack_style;
			    }
			    dtor(t), t = get_token(in);
			}
			if (t.type != tok_lbrace) {
			    error(err_explbr, &t.pos);
			    sfree(sitem);
			} else {
			    stk_push(parsestk, sitem);
			}
		    }
		    break;
		  case c_c:
		  case c_cw:
		  case c_e:
		    type = t.cmd;
		    if (style != word_Normal) {
			error(err_nestedstyles, &t.pos);
			/* Error recovery: eat lbrace, push nop. */
			dtor(t), t = get_token(in);
			sitem = snew(struct stack_item);
			sitem->fpos = t.pos;
			sitem->type = stack_nop;
			stk_push(parsestk, sitem);
		    }
		    dtor(t), t = get_token(in);
		    if (t.type != tok_lbrace) {
			error(err_explbr, &t.pos);
		    } else {
			style = (type == c_c ? word_Code :
				 type == c_cw ? word_WeakCode :
				 word_Emph);
			spcstyle = tospacestyle(style);
			sitem = snew(struct stack_item);
			sitem->fpos = t.pos;
			sitem->type = stack_style;
			stk_push(parsestk, sitem);
		    }
		    break;
		  case c_i:
		  case c_ii:
		  case c_I:
		    type = t.cmd;
		    if (indexing) {
			error(err_nestedindex, &t.pos);
			/* Error recovery: eat lbrace, push nop. */
			dtor(t), t = get_token(in);
			sitem = snew(struct stack_item);
			sitem->fpos = t.pos;
			sitem->type = stack_nop;
			stk_push(parsestk, sitem);
		    }
		    sitem = snew(struct stack_item);
		    sitem->fpos = t.pos;
		    sitem->type = stack_idx;
		    dtor(t), t = get_token(in);
		    /*
		     * Special cases: \i\c, \i\e, \i\cw
		     */
		    wd.fpos = t.pos;
		    if (t.type == tok_cmd &&
			(t.cmd == c_e || t.cmd == c_c || t.cmd == c_cw)) {
			if (style != word_Normal)
			    error(err_nestedstyles, &t.pos);
			else {
			    style = (t.cmd == c_c ? word_Code :
				     t.cmd == c_cw ? word_WeakCode :
				     word_Emph);
			    spcstyle = tospacestyle(style);
			    sitem->type |= stack_style;
			}
			dtor(t), t = get_token(in);
		    }
		    if (t.type != tok_lbrace) {
			sfree(sitem);
			error(err_explbr, &t.pos);
		    } else {
			/* Add an index-reference word with no text as yet */
			wd.type = word_IndexRef;
			wd.text = NULL;
			wd.alt = NULL;
			wd.aux = 0;
			wd.breaks = FALSE;
			indexword = addword(wd, &whptr);
			/* Set up a rdstring to read the index text */
			indexstr = nullrs;
			/* Flags so that we do the Right Things with text */
			index_visible = (type != c_I);
			index_downcase = (type == c_ii);
			indexing = TRUE;
			idxwordlist = NULL;
			idximplicit = &idxwordlist;
			/* Stack item to close the indexing on exit */
			stk_push(parsestk, sitem);
		    }
		    break;
		  case c_u:
		    uchr = t.aux;
		    utext[0] = uchr; utext[1] = 0;
		    wd.type = style;
		    wd.breaks = FALSE;
		    wd.alt = NULL;
		    wd.aux = 0;
		    wd.fpos = t.pos;
		    if (!indexing || index_visible) {
			wd.text = ustrdup(utext);
			uword = addword(wd, &whptr);
		    } else
			uword = NULL;
		    if (indexing) {
			wd.text = ustrdup(utext);
			iword = addword(wd, &idximplicit);
		    } else
			iword = NULL;
		    dtor(t), t = get_token(in);
		    if (t.type == tok_lbrace) {
			/*
			 * \u with a left brace. Until the brace
			 * closes, all further words go on a
			 * sidetrack from the main thread of the
			 * paragraph.
			 */
			sitem = snew(struct stack_item);
			sitem->fpos = t.pos;
			sitem->type = stack_ualt;
			sitem->whptr = whptr;
			sitem->idximplicit = idximplicit;
			stk_push(parsestk, sitem);
			whptr = uword ? &uword->alt : NULL;
			idximplicit = iword ? &iword->alt : NULL;
		    } else {
			if (indexing)
			    rdadd(&indexstr, uchr);
			already = TRUE;
		    }
		    break;
		  default:
		    if (!macrolookup(macros, in, t.text, &t.pos))
			error(err_badmidcmd, t.text, &t.pos);
		    break;
		}
	    }
	    if (!already)
		dtor(t), t = get_token(in);
	    seenwhite = iswhite;
	}
	finished_para:
	/* Check the stack is empty */
	if (stk_top(parsestk)) {
	    while ((sitem = stk_pop(parsestk)))
		sfree(sitem);
	    error(err_missingrbrace, &t.pos);
	}
	stk_free(parsestk);
	prev_para_type = par.type;
	/*
	 * Before we add the paragraph to the output list, we
	 * should check that there was any text in it at all; there
	 * might not be if (for example) the paragraph contained
	 * nothing but an unrecognised command sequence, and if we
	 * put an empty paragraph on the list it may confuse the
	 * back ends later on.
	 */
	if (par.words) {
	    addpara(par, ret);
	}
	if (t.type == tok_eof)
	    already = TRUE;
    }

    if (stk_top(crossparastk)) {
	void *p;

	error(err_missingrbrace2, &t.pos);
	while ((p = stk_pop(crossparastk)))
	    sfree(p);
    }

    /*
     * We break to here rather than returning, because otherwise
     * this cleanup doesn't happen.
     */
    dtor(t);

    stk_free(crossparastk);
}

struct {
    char const *magic;
    size_t nmagic;
    void (*reader)(input *);
} magics[] = {
    { "%!FontType1-",     12, &read_pfa_file },
    { "%!PS-AdobeFont-",  15, &read_pfa_file },
    { "\x80\x01",          2, &read_pfb_file },
    { "StartFontMetrics", 16, &read_afm_file },
    { "\x00\x01\x00\x00",  4, &read_sfnt_file },
    { "true",		   4, &read_sfnt_file },
};

paragraph *read_input(input *in, indexdata *idx) {
    paragraph *head = NULL;
    paragraph **hptr = &head;
    tree234 *macros;
    char mag[16];
    size_t len, i;
    void (*reader)(input *);

    macros = newtree234(macrocmp);

    while (in->currindex < in->nfiles) {
	in->currfp = fopen(in->filenames[in->currindex], "r");
	if (in->currfp) {
	    setpos(in, in->filenames[in->currindex]);
	    in->charset = in->defcharset;
	    in->csstate = charset_init_state;
	    in->wcpos = in->nwc = 0;
	    in->pushback_chars = NULL;
	    reader = NULL;
	    len = fread(mag, 1, sizeof(mag), in->currfp);
	    for (i = 0; i < lenof(magics); i++) {
		if (len >= magics[i].nmagic &&
		    memcmp(mag, magics[i].magic, magics[i].nmagic) == 0) {
		    reader = magics[i].reader;
		    break;
		}
	    }
	    rewind(in->currfp);
	    if (reader == NULL)
		read_file(&hptr, in, idx, macros);
	    else
		(*reader)(in);
	}
	in->currindex++;
    }

    macrocleanup(macros);

    return head;
}
