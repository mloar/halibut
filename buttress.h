#ifndef BUTTRESS_BUTTRESS_H
#define BUTTRESS_BUTTRESS_H

#include <stdio.h>
#include <wchar.h>

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * Structure tags
 */
typedef struct input_Tag input;
typedef struct filepos_Tag filepos;
typedef struct paragraph_Tag paragraph;
typedef struct word_Tag word;

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
#define INPUT_PUSHBACK_MAX 16
struct input_Tag {
    char **filenames;		       /* complete list of input files */
    int nfiles;			       /* how many in the list */
    FILE *currfp;		       /* the currently open one */
    int currindex;		       /* which one is that in the list */
    int pushback[INPUT_PUSHBACK_MAX];  /* pushed-back input characters */
    int npushback;
    filepos pos;
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
    para_Biblio,
    para_Bullet,
    para_NumberedList,
    para_Code,
    para_Copyright,
    para_Preamble,
    para_NoCite,
    para_Title,
    para_VersionID
};

/*
 * Data structure to hold an individual word
 */
struct word_Tag {
    word *next;
    int type;
    wchar_t *text;
};
enum {
    word_Normal,
    word_Emph,
    word_Code,			       /* monospaced; `quoted' in text */
    word_WeakCode,		       /* monospaced, normal in text */
    word_UpperXref,		       /* \K */
    word_LowerXref,		       /* \k */
    word_IndexRef,		       /* (always an invisible one) */
    word_WhiteSpace		       /* text is NULL or ignorable */
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
    err_badmidcmd,		       /* invalid command in mid-para */
    err_unexbrace,		       /* unexpected brace */
    err_explbr,			       /* expected `{' after command */
    err_kwexprbr,		       /* expected `}' after cross-ref */
    err_missingrbrace,		       /* unclosed braces at end of para */
    err_nestedstyles		       /* unable to nest text styles */
};

/*
 * malloc.c
 */
void *smalloc(int size);
void *srealloc(void *p, int size);
void sfree(void *p);

/*
 * ustring.c
 */
wchar_t *ustrdup(wchar_t *s);
char *ustrtoa(wchar_t *s, char *outbuf, int size);
int ustrlen(wchar_t *s);
wchar_t *ustrcpy(wchar_t *dest, wchar_t *source);

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

/*
 * input.c
 */
paragraph *read_input(input *in);

#endif
