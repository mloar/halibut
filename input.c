/*
 * input.c: read the source form
 */

#include <stdio.h>
#include <assert.h>
#include "buttress.h"

#define TAB_STOP 8		       /* for column number tracking */

static void unget(input *in, int c) {
    assert(in->npushback < INPUT_PUSHBACK_MAX);
    in->pushback[in->npushback++] = c;
}

/*
 * Can return EOF
 */
static int get(input *in) {
    if (in->npushback)
	return in->pushback[--in->npushback];
    else if (in->currfp) {
	int c = getc(in->currfp);
	if (c == EOF) {
	    fclose(in->currfp);
	    in->currfp = NULL;
	}
	/* Track line numbers, for error reporting */
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
	/* FIXME: do input charmap translation. We should be returning
	 * Unicode here. */
	return c;
    } else
	return EOF;
}

/*
 * Small routines to amalgamate a string from an input source.
 */
typedef struct tagRdstring rdstring;
struct tagRdstring {
    int pos, size;
    wchar_t *text;
};
static void rdadd(rdstring *rs, wchar_t c) {
    if (rs->pos >= rs->size-1) {
	rs->size = rs->pos + 128;
	rs->text = srealloc(rs->text, rs->size * sizeof(wchar_t));
    }
    rs->text[rs->pos++] = c;
    rs->text[rs->pos] = 0;
}
static void rdadds(rdstring *rs, wchar_t *p) {
    int len = ustrlen(p);
    if (rs->pos >= rs->size - len) {
	rs->size = rs->pos + len + 128;
	rs->text = srealloc(rs->text, rs->size * sizeof(wchar_t));
    }
    ustrcpy(rs->text + rs->pos, p);
    rs->pos += len;
}
static wchar_t *rdtrim(rdstring *rs) {
    rs->text = srealloc(rs->text, (rs->pos + 1) * sizeof(wchar_t));
    return rs->text;
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
	int n = 0;
	while (*p && isdec(*p)) {
	    n = 10 * n + fromdec(*p);
	    p++;
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
    ret.pos = in->pos;
    c = get(in);
    if (iswhite(c)) {		       /* tok_white or tok_eop */
	nls = 0;
	do {
	    if (isnl(c))
		nls++;
	} while ((c = get(in)) != EOF && iswhite(c));
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

    ret.pos = in->pos;
    ret.type = tok_word;
    c = get(in);		       /* expect (and discard) one space */
    if (c == ' ') {
	c = get(in);
	ret.pos = in->pos;
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
static void addword(word newword, word ***hptrptr) {
    word *mnewword = smalloc(sizeof(word));
    *mnewword = newword;	       /* structure copy */
    mnewword->next = NULL;
    **hptrptr = mnewword;
    *hptrptr = &mnewword->next;
}

/*
 * Adds a new paragraph to a linked list
 */
static void addpara(paragraph newpara, paragraph ***hptrptr) {
    paragraph *mnewpara = smalloc(sizeof(paragraph));
    *mnewpara = newpara;	       /* structure copy */
    mnewpara->next = NULL;
    **hptrptr = mnewpara;
    *hptrptr = &mnewpara->next;
}

/*
 * Reads a single file (ie until get() returns EOF)
 */
static void read_file(paragraph ***ret, input *in) {
    token t;
    paragraph par;
    word wd, **whptr;
    int style;
    struct stack_item {
	enum {
	    stack_ualt,		       /* \u alternative */
	    stack_style,	       /* \e, \c, \cw */
	    stack_idx,		       /* \I, \i, \ii */
	    stack_nop		       /* do nothing (for error recovery) */
	} type;
	word **whptr;		       /* to restore from \u alternatives */
    } *sitem;
    stack parsestk;

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
	t = get_token(in);
	if (t.type == tok_eof)
	    return;

	/*
	 * Parse code paragraphs separately.
	 */
	if (t.type == tok_cmd && t.cmd == c_c && !isbrace(in)) {
	    par.type = para_Code;
	    while (1) {
		t = get_codepar_token(in);
		wd.type = word_WeakCode;
		wd.text = ustrdup(t.text);
		addword(wd, &whptr);
		t = get_token(in);
		if (t.type == tok_white) {
		    /*
		     * The newline after a code-paragraph line
		     */
		    t = get_token(in);
		}
		if (t.type == tok_eop || t.type == tok_eof)
		    break;
		else if (t.type != tok_cmd || t.cmd != c_c) {
		    error(err_brokencodepara, &t.pos);
		    addpara(par, ret);
		    while (t.type != tok_eop)   /* error recovery: */
			t = get_token(in);   /* eat rest of paragraph */
		    continue;
		}
	    }
	    addpara(par, ret);
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

	    switch (t.cmd) {
	      default:
		needkw = -1;
		break;
	      case c__comment:
		do {
		    t = get_token(in);
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
		/* FIXME: multiple levels of Subsect */
	      case c_S: needkw = 2; par.type = para_Subsect; break;
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
		t = get_token(in);
		fp = t.pos;
		while (t.type == tok_lbrace) {
		    /* This is a keyword. */
		    nkeys++;
		    /* FIXME: there will be bugs if anyone specifies an
		     * empty keyword (\foo{}), so trap this case. */
		    while (t = get_token(in),
			   t.type == tok_word || t.type == tok_white) {
			if (t.type == tok_white)
			    rdadd(&rs, ' ');
			else
			    rdadds(&rs, t.text);
		    }
		    if (t.type != tok_rbrace) {
			error(err_kwunclosed, &t.pos);
			/* FIXME: memory leak */
			continue;
		    }
		    rdadd(&rs, 0);     /* add string terminator */
		    t = get_token(in); /* eat right brace */
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
			while (t.type != tok_eop)   /* error recovery: */
			    t = get_token(in);   /* eat rest of paragraph */
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
	 *  \\ \{ \}
	 */
	parsestk = stk_new();
	style = word_Normal;
	while (t.type != tok_eop && t.type != tok_eof) {
	    if (t.type == tok_cmd && t.cmd == c__escaped)
		t.type = tok_word;     /* nice and simple */
	    switch (t.type) {
	      case tok_white:
		wd.text = NULL;
		wd.type = word_WhiteSpace;
		addword(wd, &whptr);
		break;
	      case tok_word:
		wd.text = ustrdup(t.text);
		wd.type = style;
		addword(wd, &whptr);
		break;
	      case tok_lbrace:
		error(err_unexbrace, &t.pos);
		/* FIXME: errorrec. Push nop. */
		break;
	      case tok_rbrace:
		sitem = stk_pop(parsestk);
		if (!sitem)
		    error(err_unexbrace, &t.pos);
		else switch (sitem->type) {
		  case stack_ualt:
		    whptr = sitem->whptr;
		    break;
		  case stack_style:
		    style = word_Normal;
		    break;
		  case stack_idx:
		    /* FIXME: do this bit! */
		  case stack_nop:
		    break;
		}
		sfree(sitem);
		break;
	      case tok_cmd:
		switch (t.cmd) {
		  case c_K:
		  case c_k:
		    /*
		     * Keyword. We expect a left brace, some text,
		     * and then a right brace. No nesting; no
		     * arguments.
		     */
		    if (t.cmd == c_K)
			wd.type = word_UpperXref;
		    else
			wd.type = word_LowerXref;
		    t = get_token(in);
		    if (t.type != tok_lbrace) {
			error(err_explbr, &t.pos);
		    }
		    {
			rdstring rs = { 0, 0, NULL };
			while (t = get_token(in),
			       t.type == tok_word || t.type == tok_white) {
			    if (t.type == tok_white)
				rdadd(&rs, ' ');
			    else
				rdadds(&rs, t.text);
			}
			wd.text = ustrdup(rs.text);
		    }
		    if (t.type != tok_rbrace) {
			error(err_kwexprbr, &t.pos);
		    }
		    addword(wd, &whptr);
		    break;
		  case c_c:
		  case c_cw:
		  case c_e:
		    if (style != word_Normal) {
			error(err_nestedstyles, &t.pos);
			/* Error recovery: eat lbrace, push nop. */
			t = get_token(in);
			sitem = smalloc(sizeof(*sitem));
			sitem->type = stack_nop;
			stk_push(parsestk, sitem);
		    }
		    t = get_token(in);
		    if (t.type != tok_lbrace) {
			error(err_explbr, &t.pos);
		    } else {
			style = (t.cmd == c_c ? word_Code :
				 t.cmd == c_cw ? word_WeakCode :
				 word_Emph);
			sitem = smalloc(sizeof(*sitem));
			sitem->type = stack_style;
			stk_push(parsestk, sitem);
		    }
		    break;
		  case c_i:
		  case c_ii:
		  case c_I:
		    if (style != word_Normal) {
			error(err_nestedstyles, &t.pos);
			/* Error recovery: eat lbrace, push nop. */
			t = get_token(in);
			sitem = smalloc(sizeof(*sitem));
			sitem->type = stack_nop;
			stk_push(parsestk, sitem);
		    }
		    t = get_token(in);
		    if (t.type != tok_lbrace) {
			error(err_explbr, &t.pos);
		    } else {
			/*
			 * FIXME: do something useful
			 * Add an index-ref word and keep a pointer to it
			 * Set a flag so that other addwords also update it
			 */
			sitem = smalloc(sizeof(*sitem));
			sitem->type = stack_idx;
			stk_push(parsestk, sitem);
		    }
		    break;
		  case c_u:
		  case c_W:
		  default:
		    error(err_badmidcmd, t.text, &t.pos);
		    break;
		}
	    }
	    t = get_token(in);
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
}

paragraph *read_input(input *in) {
    paragraph *head = NULL;
    paragraph **hptr = &head;

    while (in->currindex < in->nfiles) {
	in->currfp = fopen(in->filenames[in->currindex], "r");
	if (in->currfp) {
	    in->pos.filename = in->filenames[in->currindex];
	    in->pos.line = 1;
	    in->pos.col = 1;
	    read_file(&hptr, in);
	}
	in->currindex++;
    }

    return head;
}
