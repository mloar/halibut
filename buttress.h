#ifndef BUTTRESS_BUTTRESS_H
#define BUTTRESS_BUTTRESS_H

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
typedef struct paragraph_Tag paragraph;
typedef struct word_Tag word;

/*
 * Data structure to hold all the file names etc for input
 */
#define INPUT_PUSHBACK_MAX 16
struct input_Tag {
    char **filenames;		       /* complete list of input files */
    int nfiles;			       /* how many in the list */
    FILE *currfp;		       /* the currently open one */
    int currindex;		       /* which one is that in the list */
    char pushback[INPUT_PUSHBACK_MAX]; /* pushed-back input characters */
    int npushback;
};

/*
 * Data structure to hold the input form of the source, ie a linked
 * list of paragraphs
 */
struct paragraph_Tag {
    paragraph *next;
    int type;
    char *keyword;		       /* for IR, IA, and heading paras */
    word *words;		       /* list of words in paragraph */
} paragraph;
enum {
    para_IA,			       /* index alias */
    para_IR,			       /* index rewrite */
    para_Chapter,
    para_Appendix,
    para_Heading,
    para_Subsect,
    para_Normal,
    para_Bullet,
    para_Code
};

/*
 * Data structure to hold an individual word
 */
struct word_Tag {
    word *next;
    int type;
    char *text;
}
enum {
    word_Normal,
    word_Emph,
    word_Code,
    word_IndexRef		       /* always invisible */
}

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
};

/*
 * malloc.c
 */
void *smalloc(int size);
void *srealloc(void *p, int size);
void sfree(void *p);

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
 * input.c
 */
paragraph *read_input(input *in);

#endif
