/*
 * input.c: read the source form
 */

#include <stdio.h>
#include <assert.h>
#include "buttress.h"

static void unget(input *in, char c) {
    assert(in->npushback < INPUT_PUSHBACK_MAX);
    in->pushback[in->npushback++] = c;
}

/*
 * Can return EOF
 */
static int get(input *in) {
    if (in->npushback)
	return (unsigned char)in->pushback[--in->npushback];
    else if (in->currfp) {
	int c = getc(in->currfp);
	if (c == EOF) {
	    fclose(in->currfp);
	    in->currfp = NULL;
	}
	return c;
    } else
	return EOF;
}

/*
 * Lexical analysis of source files.
 */
typedef struct tagToken token;
struct tagToken {
    int type;
    char *text;
};
enum {
    tok_eof,
    tok_eop,			       /* end of paragraph */
    tok_word,			       /* an ordinary word */
    tok_cmd,			       /* \command */
    tok_bracetext		       /* {text} */
};

/*
 * Adds a new paragraph to a linked list
 */
static paragraph addpara(paragraph ***hptrptr) {
    paragraph *newpara = smalloc(sizeof(paragraph));
    newpara->next = NULL;
    **hptrptr = newpara;
    *hptrptr = &newpara->next;
    return newpara;
}

/*
 * Reads a single file (ie until get() returns EOF)
 */
static void read_file(paragraph ***ret, input *in) {
    int c;

    while (1) {
	/*
	 * Get a token.
	 */
	token t = get_token(in);
	if (t.type == tok_eof)
	    return;
	printf("token: %d\n", t.type);
    }
}

paragraph *read_input(input *in) {
    paragraph *head = NULL;
    paragraph **hptr = &head;

    while (in->currindex < in->nfiles) {
	in->currfp = fopen(in->filenames[in->currindex], "r");
	if (in->currfp)
	    read_file(&hptr, in);
	in->currindex++;
    }
}
