#ifndef HALIBUT_HALIBUT_H
#define HALIBUT_HALIBUT_H

#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <string.h>

#include "charset.h"

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#else
#define NORETURN /* nothing */
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* For suppressing unused-parameter warnings */
#define IGNORE(x) ( (x) = (x) )

#include "tree234.h"

/*
 * Structure tags
 */
typedef struct input_Tag input;
typedef struct filepos_Tag filepos;
typedef struct paragraph_Tag paragraph;
typedef struct word_Tag word;
typedef struct keywordlist_Tag keywordlist;
typedef struct keyword_Tag keyword;
typedef struct numberstate_Tag numberstate;
typedef struct indexdata_Tag indexdata;
typedef struct indextag_Tag indextag;
typedef struct indexentry_Tag indexentry;
typedef struct macrostack_Tag macrostack;

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
typedef struct pushback_Tag {
    int chr;
    filepos pos;
} pushback;
struct input_Tag {
    char **filenames;		       /* complete list of input files */
    int nfiles;			       /* how many in the list */
    FILE *currfp;		       /* the currently open one */
    int currindex;		       /* which one is that in the list */
    pushback *pushback;		       /* pushed-back input characters */
    int npushback, pushbacksize;
    filepos pos;
    int reportcols;		       /* report column numbers in errors */
    macrostack *stack;		       /* macro expansions in force */
    int defcharset, charset;	       /* character sets for input files */
    charset_state csstate;
    wchar_t wc[16];		       /* wide chars from input conversion */
    int nwc, wcpos;		       /* size of, and position in, wc[] */
    char *pushback_chars;	       /* used to save input-encoding data */
};

/*
 * Data structure to hold the input form of the source, ie a linked
 * list of paragraphs
 */
struct paragraph_Tag {
    paragraph *next;
    int type;
    wchar_t *keyword;		       /* for most special paragraphs */
    char *origkeyword;		       /* same again in original charset */
    word *words;		       /* list of words in paragraph */
    int aux;			       /* number, in a numbered paragraph
                                        * or subsection level
                                        */
    word *kwtext;		       /* chapter/section indication */
    word *kwtext2;		       /* numeric-only form of kwtext */
    filepos fpos;

    paragraph *parent, *child, *sibling;   /* for hierarchy navigation */

    void *private_data; 	       /* for temp use in backends */
};
enum {
    para_IM,			       /* index merge */
    para_BR,			       /* bibliography rewrite */
    para_Rule,			       /* random horizontal rule */
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
    para_DescribedThing,
    para_Description,
    para_Code,
    para_Copyright,
    para_NoCite,
    para_Title,
    para_VersionID,
    para_Config,		       /* configuration directive */
    para_LcontPush,		       /* begin continuation of list item */
    para_LcontPop,		       /* end continuation of list item */
    para_QuotePush,		       /* begin block quote */
    para_QuotePop,		       /* end block quote */
    /*
     * Back ends may define their own paragraph types beyond here,
     * in case they need to use them internally.
     */
    para_NotParaType		       /* placeholder value */
};

/*
 * Data structure to hold an individual word
 */
struct word_Tag {
    word *next, *alt;
    int type;
    int aux;
    int breaks;			       /* can a line break after it? */
    wchar_t *text;
    filepos fpos;

    void *private_data; 	       /* for temp use in backends */
};
enum {
    /* ORDERING CONSTRAINT: these normal-word types ... */
    word_Normal,
    word_Emph,
    word_Code,			       /* monospaced; `quoted' in text */
    word_WeakCode,		       /* monospaced, normal in text */
    /* ... must be in the same order as these space types ... */
    word_WhiteSpace,		       /* text is NULL or ignorable */
    word_EmphSpace,		       /* WhiteSpace when emphasised */
    word_CodeSpace,		       /* WhiteSpace when code */
    word_WkCodeSpace,		       /* WhiteSpace when weak code */
    /* ... and must be in the same order as these quote types ... */
    word_Quote,			       /* text is NULL or ignorable */
    word_EmphQuote,		       /* Quote when emphasised */
    word_CodeQuote,		       /* (can't happen) */
    word_WkCodeQuote,		       /* (can't happen) */
    /* END ORDERING CONSTRAINT */
    word_internal_endattrs,
    word_UpperXref,		       /* \K */
    word_LowerXref,		       /* \k */
    word_XrefEnd,		       /* (invisible; no text) */
    word_IndexRef,		       /* (always an invisible one) */
    word_HyperLink,		       /* (invisible) */
    word_HyperEnd,		       /* (also invisible; no text) */
    /*
     * Back ends may define their own word types beyond here, in
     * case they need to use them internally.
     */
    word_NotWordType		       /* placeholder value */
};
/* aux values for attributed words */
enum {
    attr_Only   = 0x0000,	       /* a lone word with the attribute */
    attr_First  = 0x0001,	       /* the first of a series */
    attr_Last   = 0x0002,	       /* the last of a series */
    attr_Always	= 0x0003,	       /* any other part of a series */
    attr_mask   = 0x0003
};
/* aux values for quote-type words */
enum {
    quote_Open  = 0x0010,
    quote_Close = 0x0020,
    quote_mask  = 0x0030
};
#define isvis(x) ( ( (x) >= word_Normal && (x) <= word_LowerXref ) )
#define isattr(x) ( ( (x) > word_Normal && (x) < word_WhiteSpace ) || \
                    ( (x) > word_WhiteSpace && (x) < word_internal_endattrs ) )
#define sameattr(x,y) ( (((x)-(y)) & 3) == 0 )
#define towordstyle(x) ( word_Normal + ((x) & 3) )
#define tospacestyle(x) ( word_WhiteSpace + ((x) & 3) )
#define toquotestyle(x) ( word_Quote + ((x) & 3) )
#define removeattr(x) ( word_Normal + ((x) &~ 3) )

#define attraux(x) ( (x) & attr_mask )
#define quoteaux(x) ( (x) & quote_mask )

/*
 * error.c
 */
void fatal(int code, ...) NORETURN;
void error(int code, ...);
enum {
    err_nomemory,		       /* out of memory */
    err_optnoarg,		       /* option `-%s' requires an argument */
    err_nosuchopt,		       /* unrecognised option `-%s' */
    err_cmdcharset,		       /* unrecognised charset %s (cmdline) */
    err_futileopt,		       /* futile option `-%s'%s */
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
    err_codequote,                     /* \q within \c is not supported */
    err_missingrbrace,		       /* unclosed braces at end of para */
    err_missingrbrace2,		       /* unclosed braces at end of file */
    err_nestedstyles,		       /* unable to nest text styles */
    err_nestedindex,		       /* unable to nest `\i' thingys */
    err_indexcase,		       /* two \i differing only in case */
    err_nosuchkw,		       /* unresolved cross-reference */
    err_multiBR,		       /* multiple \BRs on same keyword */
    err_nosuchidxtag,		       /* \IM on unknown index tag (warning) */
    err_cantopenw,		       /* can't open output file for write */
    err_macroexists,		       /* this macro already exists */
    err_sectjump,		       /* jump a heading level, eg \C -> \S */
    err_winhelp_ctxclash,	       /* WinHelp context ID hash clash */
    err_multikw,		       /* keyword clash in sections */
    err_misplacedlcont,		       /* \lcont not after a list item */
    err_sectmarkerinblock,	       /* section marker appeared in block */
    err_cfginsufarg,		       /* \cfg{%s} insufficient args (<%d) */
    err_infonodechar,		       /* colon/comma in node name in info */
    err_text_codeline,		       /* \c line too long in text backend */
    err_htmlver,		       /* unrecognised HTML version keyword */
    err_charset,		       /* unrecognised character set name */
    err_nofont, 		       /* unrecognised font name */
    err_afmeof, 		       /* eof in AFM file */
    err_afmkey, 		       /* missing expected keyword in AFM */
    err_afmvers,		       /* unsupported AFM version */
    err_afmval, 		       /* missing value(s) for AFM key */
    err_pfeof,			       /* eof in Type 1 font file */
    err_pfhead,			       /* bad Type 1 header line */
    err_pfbad,			       /* otherwise invalide Type 1 font */
    err_pfnoafm,		       /* Type 1 font but no AFM */
    err_chmnames,		       /* need both or neither of hhp+chm */
    err_sfntnotable,		       /* required sfnt table missing */
    err_sfntnopsname,		       /* sfnt has no PostScript name */
    err_sfntbadtable,		       /* sfnt table not valid */
    err_sfntnounicmap,		       /* sfnt has no UCS-2 cmap */
    err_sfnttablevers,		       /* sfnt table version unknown */
    err_sfntbadhdr,		       /* sfnt has bad header */
    err_whatever                       /* random error of another type */
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
char *dupstr(char const *s);

#define snew(type) ( (type *) smalloc (sizeof (type)) )
#define snewn(number, type) ( (type *) smalloc ((number) * sizeof (type)) )
#define sresize(array, number, type) \
	( (type *) srealloc ((array), (number) * sizeof (type)) )
#define lenof(array) ( sizeof(array) / sizeof(*(array)) )

/*
 * ustring.c
 */
wchar_t *ustrdup(wchar_t const *s);
char *ustrtoa(wchar_t const *s, char *outbuf, int size, int charset);
char *ustrtoa_careful(wchar_t const *s, char *outbuf, int size, int charset);
wchar_t *ustrfroma(char const *s, wchar_t *outbuf, int size, int charset);
char *utoa_dup(wchar_t const *s, int charset);
char *utoa_dup_len(wchar_t const *s, int charset, int *len);
char *utoa_careful_dup(wchar_t const *s, int charset);
wchar_t *ufroma_dup(char const *s, int charset);
char *utoa_locale_dup(wchar_t const *s);
wchar_t *ufroma_locale_dup(char const *s);
int ustrlen(wchar_t const *s);
wchar_t *uadv(wchar_t *s);
wchar_t *ustrcpy(wchar_t *dest, wchar_t const *source);
wchar_t *ustrncpy(wchar_t *dest, wchar_t const *source, int n);
wchar_t utolower(wchar_t);
int uisalpha(wchar_t);
int ustrcmp(wchar_t *lhs, wchar_t *rhs);
int ustricmp(wchar_t const *lhs, wchar_t const *rhs);
int ustrnicmp(wchar_t const *lhs, wchar_t const *rhs, int maxlen);
int utoi(wchar_t const *);
double utof(wchar_t const *);
int utob(wchar_t const *);
int uisdigit(wchar_t);
wchar_t *ustrlow(wchar_t *s);
wchar_t *ustrftime(const wchar_t *wfmt, const struct tm *timespec);
int cvt_ok(int charset, const wchar_t *s);
int charset_from_ustr(filepos *fpos, const wchar_t *name);

/*
 * wcwidth.c
 */
int strwid(char const *s, int charset);
int ustrwid(wchar_t const *s, int charset);

/*
 * help.c
 */
void help(void);
void usage(void);
void showversion(void);
void listcharsets(void);

/*
 * licence.c
 */
void licence(void);

/*
 * version.c
 */
extern const char *const version;

/*
 * misc.c
 */
char *adv(char *s);

typedef struct stackTag *stack;
stack stk_new(void);
void stk_free(stack);
void stk_push(stack, void *);
void *stk_pop(stack);
void *stk_top(stack);

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
extern const rdstring empty_rdstring;
extern const rdstringc empty_rdstringc;
void rdadd(rdstring *rs, wchar_t c);
void rdadds(rdstring *rs, wchar_t const *p);
wchar_t *rdtrim(rdstring *rs);
void rdaddc(rdstringc *rs, char c);
void rdaddsc(rdstringc *rs, char const *p);
void rdaddsn(rdstringc *rc, char const *p, int len);
char *rdtrimc(rdstringc *rs);

int compare_wordlists(word *a, word *b);

void mark_attr_ends(word *words);

typedef struct tagWrappedLine wrappedline;
struct tagWrappedLine {
    wrappedline *next;
    word *begin, *end;		       /* first & last words of line */
    int nspaces;		       /* number of whitespaces in line */
    int shortfall;		       /* how much shorter than max width */
};
wrappedline *wrap_para(word *, int, int, int (*)(void *, word *), void *, int);
void wrap_free(wrappedline *);
void cmdline_cfg_add(paragraph *cfg, char *string);
paragraph *cmdline_cfg_new(void);
paragraph *cmdline_cfg_simple(char *string, ...);

/*
 * input.c
 */
paragraph *read_input(input *in, indexdata *idx);

/*
 * in_afm.c
 */
void read_afm_file(input *in);

/*
 * in_pf.c
 */
void read_pfa_file(input *in);
void read_pfb_file(input *in);

/*
 * in_sfnt.c
 */
void read_sfnt_file(input *in);

/*
 * keywords.c
 */
struct keywordlist_Tag {
    int nkeywords;
    int size;
    tree234 *keys;		       /* sorted by `key' field */
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

/*
 * Data structure to hold both sides of the index.
 */
struct indexdata_Tag {
    tree234 *tags;		       /* holds type `indextag' */
    tree234 *entries;		       /* holds type `indexentry' */
};

/*
 * Data structure to hold an index tag (LHS of index).
 */
struct indextag_Tag {
    wchar_t *name;
    word *implicit_text;
    filepos implicit_fpos;
    word **explicit_texts;
    filepos *explicit_fpos;
    int nexplicit, explicit_size;
    int nrefs;
    indexentry **refs;		       /* array of entries referenced by tag */
};

/*
 * Data structure to hold an index entry (RHS of index).
 */
struct indexentry_Tag {
    word *text;
    void *backend_data;		       /* private to back end */
    filepos fpos;
};

indexdata *make_index(void);
void cleanup_index(indexdata *);
/* index_merge takes responsibility for freeing arg 3 iff implicit; never
 * takes responsibility for arg 2 */
void index_merge(indexdata *, int is_explicit, wchar_t *, word *, filepos *);
void build_index(indexdata *);
void index_debug(indexdata *);
indextag *index_findtag(indexdata *idx, wchar_t *name);

/*
 * contents.c
 */
numberstate *number_init(void);
void number_cfg(numberstate *, paragraph *);
word *number_mktext(numberstate *, paragraph *, wchar_t *, int *, int *);
void number_free(numberstate *);

/*
 * biblio.c
 */
void gen_citations(paragraph *, keywordlist *);

/*
 * bk_text.c
 */
void text_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *text_config_filename(char *filename);

/*
 * bk_html.c
 */
void html_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *html_config_filename(char *filename);

/*
 * bk_whlp.c
 */
void whlp_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *whlp_config_filename(char *filename);

/*
 * bk_man.c
 */
void man_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *man_config_filename(char *filename);

/*
 * bk_info.c
 */
void info_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *info_config_filename(char *filename);

/*
 * bk_paper.c
 */
void *paper_pre_backend(paragraph *, keywordlist *, indexdata *);
void listfonts(void);

/*
 * bk_ps.c
 */
void ps_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *ps_config_filename(char *filename);

/*
 * bk_pdf.c
 */
void pdf_backend(paragraph *, keywordlist *, indexdata *, void *);
paragraph *pdf_config_filename(char *filename);

#endif
