/*
 * input.c: read the source form
 */

#include <stdio.h>
#include <assert.h>
#include <time.h>
#include "buttress.h"

#define TAB_STOP 8		       /* for column number tracking */

static void setpos(input *in, char *fname) {
    in->pos[0].filename = fname;
    in->pos[0].line = 1;
    in->pos[0].col = (in->reportcols ? 1 : -1);
    in->pos[1] = in->pos[0];
    in->posptr = 1;
}

static filepos getpos(input *in) {
    return in->pos[in->posptr];
}

static void unget(input *in, int c) {
    assert(in->npushback < INPUT_PUSHBACK_MAX);
    in->pushback[in->npushback++] = c;
    in->posptr += lenof(in->pos)-1;
    in->posptr %= lenof(in->pos);
}

/*
 * Can return EOF
 */
static int get(input *in) {
    if (in->npushback) {
	in->posptr++;
	in->posptr %= lenof(in->pos);
	return in->pushback[--in->npushback];
    }
    else if (in->currfp) {
	int c = getc(in->currfp);
	filepos fp;

	if (c == EOF) {
	    fclose(in->currfp);
	    in->currfp = NULL;
	}
	/* Track line numbers, for error reporting */
	fp = in->pos[in->posptr];
	in->posptr++;
	in->posptr %= lenof(in->pos);
	if (in->reportcols) {
	    switch (c) {
	      case '\t':
		fp.col = 1 + (fp.col + TAB_STOP-1) % TAB_STOP;
		break;
	      case '\n':
		fp.col = 1;
		fp.line++;
		break;
	      default:
		fp.col++;
		break;
	    }
	} else {
	    fp.col = -1;
	    if (c == '\n')
		fp.line++;
	}
	in->pos[in->posptr] = fp;
	/* FIXME: do input charmap translation. We should be returning
	 * Unicode here. */
	return c;
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

/* Buttress command keywords. */
enum {
    c__invalid,			       /* invalid command */
    c__comment,			       /* comment command (\#) */
    c__escaped,			       /* escaped character */
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
    c_copyright,		       /* copyright statement */
    c_cw,			       /* weak code */
    c_date,			       /* document processing date */
    c_e,			       /* emphasis */
    c_i,			       /* visible index mark */
    c_ii,			       /* uncapitalised visible index mark */
    c_k,			       /* uncapitalised cross-reference */
    c_n,			       /* numbered list */
    c_nocite,			       /* bibliography trickery */
    c_preamble,			       /* document preamble text */
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
	{"b", c_b},		       /* bulletted list */
	{"c", c_c},		       /* code */
	{"copyright", c_copyright},    /* copyright statement */
	{"cw", c_cw},		       /* weak code */
	{"date", c_date},	       /* document processing date */
	{"e", c_e},		       /* emphasis */
	{"i", c_i},		       /* visible index mark */
	{"ii", c_ii},		       /* uncapitalised visible index mark */
	{"k", c_k},		       /* uncapitalised cross-reference */
	{"n", c_n},		       /* numbered list */
	{"nocite", c_nocite},	       /* bibliography trickery */
	{"preamble", c_preamble},      /* document preamble text */
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
    token ret;
    rdstring rs = { 0, 0, NULL };

    ret.text = NULL;		       /* default */
    ret.pos = getpos(in);
    c = get(in);
    if (iswhite(c)) {		       /* tok_white or tok_eop */
	nls = 0;
	do {
	    if (isnl(c))
		nls++;
	} while ((c = get(in)) != EOF && iswhite(c));
	if (c == EOF) {
	    ret.type = tok_eof;
	    return ret;
	}
	unget(in, c);
	ret.type = (nls > 1 ? tok_eop : tok_white);
	return ret;
    } else if (c == EOF) {	       /* tok_eof */
	ret.type = tok_eof;
	return ret;
    } else if (c == '\\') {	       /* tok_cmd */
	c = get(in);
	if (c == '\\' || c == '#' || c == '{' || c == '}') {
	    /* single-char command */
	    rdadd(&rs, c);
	} else if (c == 'u') {
	    int len = 0;
	    do {
		rdadd(&rs, c);
		len++;
		c = get(in);
	    } while (ishex(c) && len < 5);
	    unget(in, c);
	} else if (iscmd(c)) {
	    do {
		rdadd(&rs, c);
		c = get(in);
	    } while (iscmd(c));
	    unget(in, c);
	}
	/*
	 * Now match the command against the list of available
	 * ones.
	 */
	ret.type = tok_cmd;
	ret.text = ustrdup(rs.text);
	match_kw(&ret);
	sfree(rs.text);
	return ret;
    } else if (c == '{') {	       /* tok_lbrace */
	ret.type = tok_lbrace;
	return ret;
    } else if (c == '}') {	       /* tok_rbrace */
	ret.type = tok_rbrace;
	return ret;
    } else {			       /* tok_word */
	/*
	 * Read a word: the longest possible contiguous sequence of
	 * things other than whitespace, backslash, braces and
	 * hyphen. A hyphen terminates the word but is returned as
	 * part of it; everything else is pushed back for the next
	 * token.
	 */
	while (1) {
	    if (iswhite(c) || c=='{' || c=='}' || c=='\\' || c==EOF) {
		/* Put back the character that caused termination */
		unget(in, c);
		break;
	    } else {
		rdadd(&rs, c);
		if (c == '-')
		    break;	       /* hyphen terminates word */
	    }
	    c = get(in);
	}
	ret.type = tok_word;
	ret.text = ustrdup(rs.text);
	sfree(rs.text);
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

    c = get(in);
    unget(in, c);
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

    ret.pos = getpos(in);
    ret.type = tok_word;
    c = get(in);		       /* expect (and discard) one space */
    if (c == ' ') {
	ret.pos = getpos(in);
	c = get(in);
    }
    while (!isnl(c) && c != EOF) {
	rdadd(&rs, c);
	c = get(in);
    }
    unget(in, c);
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
    mnewword = mknew(word);
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
    paragraph *mnewpara = mknew(paragraph);
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
#define dtor(t) ( sfree(t.text) )

/*
 * Reads a single file (ie until get() returns EOF)
 */
static void read_file(paragraph ***ret, input *in, index *idx) {
    token t;
    paragraph par;
    word wd, **whptr, **idximplicit;
    wchar_t utext[2], *wdtext;
    int style;
    int already;
    int iswhite, seenwhite;
    int type;
    struct stack_item {
	enum {
	    stack_nop = 0,	       /* do nothing (for error recovery) */
	    stack_ualt = 1,	       /* \u alternative */
	    stack_style = 2,	       /* \e, \c, \cw */
	    stack_idx = 4,	       /* \I, \i, \ii */
	    stack_hyper = 8,	       /* \W */
	} type;
	word **whptr;		       /* to restore from \u alternatives */
	word **idximplicit;	       /* to restore from \u alternatives */
    } *sitem;
    stack parsestk;
    word *indexword, *uword, *iword;
    word *idxwordlist;
    rdstring indexstr;
    int index_downcase, index_visible, indexing;
    const rdstring nullrs = { 0, 0, NULL };
    wchar_t uchr;

    t.text = NULL;

    /*
     * Loop on each paragraph.
     */
    while (1) {
	par.words = NULL;
	par.keyword = NULL;
	whptr = &par.words;

	/*
	 * Get a token.
	 */
	dtor(t), t = get_token(in);
	if (t.type == tok_eof)
	    return;

	/*
	 * Parse code paragraphs separately.
	 */
	if (t.type == tok_cmd && t.cmd == c_c && !isbrace(in)) {
	    par.type = para_Code;
	    par.fpos = t.pos;
	    while (1) {
		dtor(t), t = get_codepar_token(in);
		wd.type = word_WeakCode;
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
		if (t.type == tok_eop || t.type == tok_eof)
		    break;
		else if (t.type != tok_cmd || t.cmd != c_c) {
		    error(err_brokencodepara, &t.pos);
		    addpara(par, ret);
		    while (t.type != tok_eop)   /* error recovery: */
			dtor(t), t = get_token(in);   /* eat rest of paragraph */
		    continue;
		}
	    }
	    addpara(par, ret);
	    continue;
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

	    par.fpos = t.pos;
	    switch (t.cmd) {
	      default:
		needkw = -1;
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
		 *   0 -- no keywords at all
		 *   1 -- exactly one keyword
		 *   2 -- at least one keyword
		 *   4 -- any number of keywords including zero
		 *   8 -- at least one keyword and then nothing else
		 */
	      case c_A: needkw = 2; par.type = para_Appendix; break;
	      case c_B: needkw = 2; par.type = para_Biblio; break;
	      case c_BR: needkw = 1; par.type = para_BR; break;
	      case c_C: needkw = 2; par.type = para_Chapter; break;
	      case c_H: needkw = 2; par.type = para_Heading; break;
	      case c_IM: needkw = 2; par.type = para_IM; break;
	      case c_S: needkw = 2; par.type = para_Subsect;
		par.aux = t.aux; break;
	      case c_U: needkw = 0; par.type = para_UnnumberedChapter; break;
		/* For \b and \n the keyword is optional */
	      case c_b: needkw = 4; par.type = para_Bullet; break;
	      case c_n: needkw = 4; par.type = para_NumberedList; break;
	      case c_copyright: needkw = 0; par.type = para_Copyright; break;
		/* For \nocite the keyword is _everything_ */
	      case c_nocite: needkw = 8; par.type = para_NoCite; break;
	      case c_preamble: needkw = 0; par.type = para_Preamble; break;
	      case c_title: needkw = 0; par.type = para_Title; break;
	      case c_versionid: needkw = 0; par.type = para_VersionID; break;
	    }

	    if (needkw >= 0) {
		rdstring rs = { 0, 0, NULL };
		int nkeys = 0;
		filepos fp;

		/* Get keywords. */
		dtor(t), t = get_token(in);
		fp = t.pos;
		while (t.type == tok_lbrace) {
		    /* This is a keyword. */
		    nkeys++;
		    /* FIXME: there will be bugs if anyone specifies an
		     * empty keyword (\foo{}), so trap this case. */
		    while (dtor(t), t = get_token(in),
			   t.type == tok_word || 
			   t.type == tok_white ||
			   (t.type == tok_cmd && t.cmd == c__escaped)) {
			if (t.type == tok_white)
			    rdadd(&rs, ' ');
			else
			    rdadds(&rs, t.text);
		    }
		    if (t.type != tok_rbrace) {
			error(err_kwunclosed, &t.pos);
			continue;
		    }
		    rdadd(&rs, 0);     /* add string terminator */
		    dtor(t), t = get_token(in); /* eat right brace */
		}

		rdadd(&rs, 0);     /* add string terminator */

		/* See whether we have the right number of keywords. */
		if (needkw == 0 && nkeys > 0)
		    error(err_kwillegal, &fp);
		if ((needkw & 11) && nkeys == 0)
		    error(err_kwexpected, &fp);
		if ((needkw & 5) && nkeys > 1)
		    error(err_kwtoomany, &fp);

		par.keyword = rdtrim(&rs);

		/* Move to EOP in case of needkw==8 (no body) */
		if (needkw == 8) {
		    if (t.type != tok_eop) {
			error(err_bodyillegal, &t.pos);
			/* Error recovery: eat the rest of the paragraph */
			while (t.type != tok_eop)
			    dtor(t), t = get_token(in);
		    }
		    addpara(par, ret);
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
	 *  \c \cw
	 *  \e
	 *  \i \ii
	 *  \I
	 *  \u
	 *  \W
	 *  \date
	 *  \\ \{ \}
	 */
	parsestk = stk_new();
	style = word_Normal;
	indexing = FALSE;
	seenwhite = TRUE;
	while (t.type != tok_eop && t.type != tok_eof) {
	    iswhite = FALSE;
	    already = FALSE;
	    if (t.type == tok_cmd && t.cmd == c__escaped)
		t.type = tok_word;     /* nice and simple */
	    switch (t.type) {
	      case tok_white:
		if (whptr == &par.words)
		    break;	       /* strip whitespace at start of para */
		wd.text = NULL;
		wd.type = word_WhiteSpace;
		wd.alt = NULL;
		wd.fpos = t.pos;
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
		if (!indexing || index_visible) {
		    wd.text = ustrdup(t.text);
		    wd.type = style;
		    wd.alt = NULL;
		    wd.fpos = t.pos;
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
		sitem = mknew(struct stack_item);
		sitem->type = stack_nop;
		stk_push(parsestk, sitem);
		break;
	      case tok_rbrace:
		sitem = stk_pop(parsestk);
		if (!sitem)
		    error(err_unexbrace, &t.pos);
		else {
		    if (sitem->type & stack_ualt) {
			whptr = sitem->whptr;
			idximplicit = sitem->idximplicit;
		    }
		    if (sitem->type & stack_style)
			style = word_Normal;
		    if (sitem->type & stack_idx) {
			indexword->text = ustrdup(indexstr.text);
			if (index_downcase)
			    ustrlow(indexword->text);
			indexing = FALSE;
			rdadd(&indexstr, L'\0');
			index_merge(idx, FALSE, indexstr.text, idxwordlist);
			sfree(indexstr.text);
		    }
		    if (sitem->type & stack_hyper) {
			wd.text = NULL;
			wd.type = word_HyperEnd;
			wd.alt = NULL;
			wd.fpos = t.pos;
			if (!indexing || index_visible)
			    addword(wd, &whptr);
			if (indexing)
			    addword(wd, &idximplicit);
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
			} else
			    error(err_explbr, &t.pos);
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
				sitem->type |= stack_style;
			    }
			    dtor(t), t = get_token(in);
			}
			if (t.type != tok_lbrace) {
			    error(err_explbr, &t.pos);
			} else {
			    sitem = mknew(struct stack_item);
			    sitem->type = stack_hyper;
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
			sitem = mknew(struct stack_item);
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
			sitem = mknew(struct stack_item);
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
			sitem = mknew(struct stack_item);
			sitem->type = stack_nop;
			stk_push(parsestk, sitem);
		    }
		    sitem = mknew(struct stack_item);
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
		    if (!indexing || index_visible) {
			wd.text = ustrdup(utext);
			wd.type = style;
			wd.alt = NULL;
			wd.fpos = t.pos;
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
			sitem = mknew(struct stack_item);
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
		    error(err_badmidcmd, t.text, &t.pos);
		    break;
		}
	    }
	    if (!already)
		dtor(t), t = get_token(in);
	    seenwhite = iswhite;
	}
	/* Check the stack is empty */
	if (NULL != (sitem = stk_pop(parsestk))) {
	    do {
		sfree(sitem);
		sitem = stk_pop(parsestk);
	    } while (sitem);
	    error(err_missingrbrace, &t.pos);
	}
	stk_free(parsestk);
	addpara(par, ret);
    }
    dtor(t);
}

paragraph *read_input(input *in, index *idx) {
    paragraph *head = NULL;
    paragraph **hptr = &head;

    while (in->currindex < in->nfiles) {
	in->currfp = fopen(in->filenames[in->currindex], "r");
	if (in->currfp) {
	    setpos(in, in->filenames[in->currindex]);
	    read_file(&hptr, in, idx);
	}
	in->currindex++;
    }

    return head;
}
