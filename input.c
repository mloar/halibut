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
	    in->currindex++;
	}
	return c;
    } else
	return EOF;
}

paragraph *read_input(input *in) {
    /* FIXME: do some reading */
}
