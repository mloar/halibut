#ifndef BUTTRESS_BUTTRESS_H
#define BUTTRESS_BUTTRESS_H

#include <stdio.h>
#include <wchar.h>
#include <time.h>

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* For suppressing unused-parameter warnings */
#define IGNORE(x) ( (x) = (x) )

/*
 * Structure tags
 */
typedef struct input_Tag input;
typedef struct filepos_Tag filepos;
typedef struct paragraph_Tag paragraph;
typedef struct word_Tag word;
typedef struct keywordlist_Tag keywordlist;
typedef struct keyword_Tag keyword;
typedef struct userstyle_Tag userstyle;
typedef struct numberstate_Tag numberstate;
typedef struct index_Tag index;

/*
 * Data structure to hold a file name and index, a line and a
 * column number, for reporting errors
 */
struct filepos_Tag {
    char *filename;
    int line, col;
};

/*
 * Data structure to hold all the file names etc for input
 */
#define INPUT_PUSHBACK_MAX 1
struct input_Tag {
    char **filenames;		       /* complete list of input files */
    int nfiles;			       /* how many in the list */
    FILE *currfp;		       /* the currently open one */
    int currindex;		       /* which one is that in the list */
    int pushback[INPUT_PUSHBACK_MAX];  /* pushed-back input characters */
    int npushback;
    int reportcols;		       /* report column numbers in errors */
    filepos pos[1+INPUT_PUSHBACK_MAX];
    int posptr;
};

/*
 * Data structure to hold the input form of the source, ie a linked
 * list of paragraphs
 */
struct paragraph_Tag {
    paragraph *next;
    int type;
    wchar_t *keyword;		       /* for most special paragraphs */
    word *words;		       /* list of words in paragraph */
    int aux;			       /* number, in a numbered paragraph */
    word *kwtext;		       /* chapter/section indication */
    word *kwtext2;		       /* numeric-only form of kwtext */
    filepos fpos;
};
enum {
    para_IM,			       /* index merge */
    para_BR,			       /* bibliography rewrite */
    para_Chapter,
    para_Appendix,
    para_UnnumberedChapter,
    para_Heading,
    para_Subsect,
    para_Normal,
    para_Biblio,		       /* causes no output unless turned ... */
    para_BiblioCited,		       /*  ... into this paragraph type */
    para_Bullet,
    para_NumberedList,
    para_Code,
    para_Copyright,
    para_Preamble,
    para_NoCite,
    para_Title,
    para_VersionID,
    para_NotParaType		       /* placeholder value */
};

/*
 * Data structure to hold an individual word
 */
struct word_Tag {
    word *next, *alt;
    int type;
    wchar_t *text;
    filepos fpos;
};
enum {
    word_Normal,
    word_Emph,
    word_Code,			       /* monospaced; `quoted' in text */
    word_WeakCode,		       /* monospaced, normal in text */
    word_UpperXref,		       /* \K */
    word_LowerXref,		       /* \k */
    word_XrefEnd,		       /* (invisible; no text) */
    word_IndexRef,		       /* (always an invisible one) */
    word_WhiteSpace,		       /* text is NULL or ignorable */
    word_HyperLink,		       /* (invisible) */
    word_HyperEnd		       /* (also invisible; no text) */
};

/*
 * error.c
 */
void fatal(int code, ...) NORETURN;
void error(int code, ...);
enum {
    err_nomemory,		       /* out of memory */
    err_optnoarg,		       /* option `-%s' requires an argument */
    err_nosuchopt,		       /* unrecognised option `-%s' */
    err_noinput,		       /* no input files */
    err_cantopen,		       /* unable to open input file `%s' */
    err_nodata,			       /* no data in input files */
    err_brokencodepara,		       /* line in codepara didn't begin `\c' */
    err_kwunclosed,		       /* expected `}' after keyword */
    err_kwillegal,		       /* paragraph type expects no keyword */
    err_kwexpected,		       /* paragraph type expects a keyword */
    err_kwtoomany,		       /* paragraph type expects only 1 */
    err_bodyillegal,		       /* paragraph type expects only kws! */
    err_badparatype,		       /* invalid command at start of para */
    err_badmidcmd,		       /* invalid command in mid-para */
    err_unexbrace,		       /* unexpected brace */
    err_explbr,			       /* expected `{' after command */
    err_commenteof,		       /* EOF inside braced comment */
    err_kwexprbr,		       /* expected `}' after cross-ref */
    err_missingrbrace,		       /* unclosed braces at end of para */
    err_nestedstyles,		       /* unable to nest text styles */
    err_nestedindex,		       /* unable to nest `\i' thingys */
    err_nosuchkw,		       /* unresolved cross-reference */
    err_multiBR,		       /* multiple \BRs on same keyword */
    err_nosuchidxtag,		       /* \IM on unknown index tag (warning) */
    err_cantopenw		       /* can't open output file for write */
};

/*
 * malloc.c
 */
#ifdef LOGALLOC
void *smalloc(char *file, int line, int size);
void *srealloc(char *file, int line, void *p, int size);
void sfree(char *file, int line, void *p);
#define smalloc(x) smalloc(__FILE__, __LINE__, x)
#define srealloc(x, y) srealloc(__FILE__, __LINE__, x, y)
#define sfree(x) sfree(__FILE__, __LINE__, x)
#else
void *smalloc(int size);
void *srealloc(void *p, int size);
void sfree(void *p);
#endif
void free_word_list(word *w);
void free_para_list(paragraph *p);
word *dup_word_list(word *w);

#define mknew(type) ( (type *) smalloc (sizeof (type)) )
#define mknewa(type, number) ( (type *) smalloc ((number) * sizeof (type)) )
#define resize(array, len) ( srealloc ((array), (len) * sizeof (*(array))) )
#define lenof(array) ( sizeof(array) / sizeof(*(array)) )

/*
 * ustring.c
 */
wchar_t *ustrdup(wchar_t *s);
char *ustrtoa(wchar_t *s, char *outbuf, int size);
int ustrlen(wchar_t *s);
wchar_t *ustrcpy(wchar_t *dest, wchar_t *source);
wchar_t utolower(wchar_t);
int ustrcmp(wchar_t *lhs, wchar_t *rhs);
int ustricmp(wchar_t *lhs, wchar_t *rhs);
wchar_t *ustrlow(wchar_t *s);
wchar_t *ustrftime(wchar_t *fmt, struct tm *timespec);

/*
 * help.c
 */
void help(void);
void usage(void);
void showversion(void);

/*
 * licence.c
 */
void licence(void);

/*
 * version.c
 */
const char *const version;

/*
 * misc.c
 */
typedef struct stackTag *stack;
stack stk_new(void);
void stk_free(stack);
void stk_push(stack, void *);
void *stk_pop(stack);

typedef struct tagRdstring rdstring;
struct tagRdstring {
    int pos, size;
    wchar_t *text;
};
typedef struct tagRdstringc rdstringc;
struct tagRdstringc {
    int pos, size;
    char *text;
};
void rdadd(rdstring *rs, wchar_t c);
void rdadds(rdstring *rs, wchar_t *p);
wchar_t *rdtrim(rdstring *rs);
void rdaddc(rdstringc *rs, char c);
void rdaddsc(rdstringc *rs, char *p);
char *rdtrimc(rdstringc *rs);

int compare_wordlists(word *a, word *b);

typedef struct tagWrappedLine wrappedline;
struct tagWrappedLine {
    wrappedline *next;
    word *begin, *end;		       /* first & last words of line */
    int nspaces;		       /* number of whitespaces in line */
    int shortfall;		       /* how much shorter than max width */
};
wrappedline *wrap_para(word *, int, int, int (*)(word *));
void wrap_free(wrappedline *);

/*
 * input.c
 */
paragraph *read_input(input *in, index *idx);

/*
 * keywords.c
 */
struct keywordlist_Tag {
    int nkeywords;
    int size;
    keyword **keys;
    word **looseends;		       /* non-keyword list element numbers */
    int nlooseends;
    int looseendssize;
};
struct keyword_Tag {
    wchar_t *key;		       /* the keyword itself */
    word *text;			       /* "Chapter 2", "Appendix Q"... */
    				       /* (NB: filepos are not set) */
    paragraph *para;		       /* the paragraph referenced */
};
keyword *kw_lookup(keywordlist *, wchar_t *);
keywordlist *get_keywords(paragraph *);
void free_keywords(keywordlist *);
void subst_keywords(paragraph *, keywordlist *);

/*
 * index.c
 */
index *make_index(void);
void cleanup_index(index *);
/* index_merge takes responsibility for freeing arg 3 iff implicit; never
 * takes responsibility for arg 2 */
void index_merge(index *, int is_explicit, wchar_t *, word *);
void build_index(index *);
void index_debug(index *);

/*
 * contents.c
 */
numberstate *number_init(void);
word *number_mktext(numberstate *, int, int, int, word **);
void number_free(numberstate *);

/*
 * biblio.c
 */
void gen_citations(paragraph *, keywordlist *);

/*
 * style.c
 */
struct userstyle_Tag {
};

/*
 * tree23.c
 */
typedef struct tree23_Tag tree23;
typedef struct enum23_Tag {
    void *node;
    int posn;
} enum23;
tree23 *newtree23(void);
void freetree23(tree23 *);
void *add23(tree23 *, void *, int (*cmp)(void *, void *));
void *find23(tree23 *, void *, int (*cmp)(void *, void *));
void *first23(tree23 *, enum23 *);
void *next23(enum23 *);

/*
 * bk_text.c
 */
void text_backend(paragraph *, keywordlist *, index *);

#endif
