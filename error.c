/*
 * error.c: buttress error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "buttress.h"

/*
 * Error flags
 */
#define PREFIX 0x0001		       /* give `buttress:' prefix */

static void do_error(int code, va_list ap) {
    char error[1024];
    char *sp;
    int flags;

    switch(code) {
      case err_nomemory:	       /* no arguments */
	sprintf(error, "out of memory");
	flags = PREFIX;
	break;
      case err_optnoarg:
	sp = va_arg(ap, char *);
	sprintf(error, "option `-%.200s' requires an argument", sp);
	flags = PREFIX;
	break;
      case err_nosuchopt:
	sp = va_arg(ap, char *);
	sprintf(error, "unrecognised option `-%.200s'", sp);
	flags = PREFIX;
	break;
      case err_noinput:		       /* no arguments */
	sprintf(error, "no input files");
	flags = PREFIX;
	break;
    }

    if (flags & PREFIX)
	fputs("buttress: ", stderr);
    fputs(error, stderr);
    fputc('\n', stderr);
}

void fatal(int code, ...) {
    va_list ap;
    va_start(ap, code);
    do_error(code, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

void error(int code, ...) {
    va_list ap;
    va_start(ap, code);
    do_error(code, ap);
    va_end(ap);
}
