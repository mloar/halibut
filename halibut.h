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
    int wantclose;		       /* does the current file want closing */
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
    word_Strong,
    word_Code,			       /* monospaced; `quoted' in text */
    word_WeakCode,		       /* monospaced, normal in text */
    /* ... must be in the same order as these space types ... */
    word_WhiteSpace,		       /* text is NULL or ignorable */
    word_EmphSpace,		       /* WhiteSpace when emphasised */
    word_StrongSpace,		       /* WhiteSpace when strong */
    word_CodeSpace,		       /* WhiteSpace when code */
    word_WkCodeSpace,		       /* WhiteSpace when weak code */
    /* ... and must be in the same order as these quote types ... */
    word_Quote,			       /* text is NULL or ignorable */
    word_EmphQuote,		       /* Quote when emphasised */
    word_StrongQuote,		       /* Quote when strong */
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
#define NATTRS (word_WhiteSpace - word_Normal)
#define sameattr(x,y) ( (((x)-(y)) % NATTRS) == 0 )
#define towordstyle(x) ( word_Normal + ((x) % NATTRS) )
#define tospacestyle(x) ( word_WhiteSpace + ((x) % NATTRS) )
#define toquotestyle(x) ( word_Quote + ((x) % NATTRS) )
#define removeattr(x) ( word_Normal + ((x)/NATTRS * NATTRS) )

#define attraux(x) ( (x) & attr_mask )
#define quoteaux(x) ( (x) & quote_mask )

/*
 * error.c
 */
/* out of memory */
void fatalerr_nomemory(void) NORETURN;
/* option `-%s' requires an argument */
void err_optnoarg(const char *sp);
/* unrecognised option `-%s' */
void err_nosuchopt(const char *sp);
/* unrecognised charset %s (cmdline) */
void err_cmdcharset(const char *sp);
/* futile option `-%s'%s */
void err_futileopt(const char *sp, const char *sp2);
/* no input files */
void err_noinput(void);
/* unable to open input file `%s' */
void err_cantopen(const char *sp);
/* no data in input files */
void err_nodata(void);
/* line in codepara didn't begin `\c' */
void err_brokencodepara(const filepos *fpos);
/* expected `}' after keyword */
void err_kwunclosed(const filepos *fpos);
/* paragraph type expects no keyword */
void err_kwexpected(const filepos *fpos);
/* paragraph type expects a keyword */
void err_kwillegal(const filepos *fpos);
/* paragraph type expects only 1 */
void err_kwtoomany(const filepos *fpos);
/* paragraph type expects only kws! */
void err_bodyillegal(const filepos *fpos);
/* invalid command at start of para */
void err_badparatype(const wchar_t *wsp, const filepos *fpos);
/* invalid command in mid-para */
void err_badmidcmd(const wchar_t *wsp, const filepos *fpos);
/* unexpected brace */
void err_unexbrace(const filepos *fpos);
/* expected `{' after command */
void err_explbr(const filepos *fpos);
/* EOF inside braced comment */
void err_commenteof(const filepos *fpos);
/* expected `}' after cross-ref */
void err_kwexprbr(const filepos *fpos);
/* \q within \c is not supported */
void err_codequote(const filepos *fpos);
/* unclosed braces at end of para */
void err_missingrbrace(const filepos *fpos);
/* unclosed braces at end of file */
void err_missingrbrace2(const filepos *fpos);
/* unable to nest text styles */
void err_nestedstyles(const filepos *fpos);
/* unable to nest `\i' thingys */
void err_nestedindex(const filepos *fpos);
/* two \i differing only in case */
void err_indexcase(const filepos *fpos, const wchar_t *wsp,
                   const filepos *fpos2, const wchar_t *wsp2);
/* unresolved cross-reference */
void err_nosuchkw(const filepos *fpos, const wchar_t *wsp);
/* multiple \BRs on same keyword */
void err_multiBR(const filepos *fpos, const wchar_t *wsp);
/* \IM on unknown index tag (warning) */
void err_nosuchidxtag(const filepos *fpos, const wchar_t *wsp);
/* can't open output file for write */
void err_cantopenw(const char *sp);
/* this macro already exists */
void err_macroexists(const filepos *fpos, const wchar_t *wsp);
/* jump a heading level, eg \C -> \S */
void err_sectjump(const filepos *fpos);
/* WinHelp context ID hash clash */
void err_winhelp_ctxclash(const filepos *fpos, const char *sp, const char *sp2);
/* keyword clash in sections */
void err_multikw(const filepos *fpos, const filepos *fpos2, const wchar_t *wsp);
/* \lcont not after a list item */
void err_misplacedlcont(const filepos *fpos);
/* section marker appeared in block */
void err_sectmarkerinblock(const filepos *fpos, const char *sp);
/* \cfg{%s} insufficient args (<%d) */
void err_cfginsufarg(const filepos *fpos, const char *sp, int i);
/* colon/comma in node name in info */
void err_infonodechar(const filepos *fpos, char c) /* fpos might be NULL */;
/* \c line too long in text backend */
void err_text_codeline(const filepos *fpos, int i, int j);
/* unrecognised HTML version keyword */
void err_htmlver(const filepos *fpos, const wchar_t *wsp);
/* unrecognised character set name */
void err_charset(const filepos *fpos, const wchar_t *wsp);
/* unrecognised font name */
void err_nofont(const filepos *fpos, const wchar_t *wsp);
/* eof in AFM file */
void err_afmeof(const filepos *fpos);
/* missing expected keyword in AFM */
void err_afmkey(const filepos *fpos, const char *sp);
/* unsupported AFM version */
void err_afmvers(const filepos *fpos);
/* missing value(s) for AFM key */
void err_afmval(const filepos *fpos, const char *sp, int i);
/* eof in Type 1 font file */
void err_pfeof(const filepos *fpos);
/* bad Type 1 header line */
void err_pfhead(const filepos *fpos);
/* otherwise invalide Type 1 font */
void err_pfbad(const filepos *fpos);
/* Type 1 font but no AFM */
void err_pfnoafm(const filepos *fpos, const char *sp);
/* need both or neither of hhp+chm */
void err_chmnames(void);
/* required sfnt table missing */
void err_sfntnotable(const filepos *fpos, const char *sp);
/* sfnt has no PostScript name */
void err_sfntnopsname(const filepos *fpos);
/* sfnt table not valid */
void err_sfntbadtable(const filepos *fpos, const char *sp);
/* sfnt has no UCS-2 cmap */
void err_sfntnounicmap(const filepos *fpos);
/* sfnt table version unknown */
void err_sfnttablevers(const filepos *fpos, const char *sp);
/* sfnt has bad header */
void err_sfntbadhdr(const filepos *fpos);
/* sfnt cmap references bad glyph */
void err_sfntbadglyph(const filepos *fpos, unsigned wc);

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
