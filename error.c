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
#define FILEPOS 0x0002		       /* give file position prefix */

static void do_error(int code, va_list ap) {
    char error[1024];
    char auxbuf[256];
    char *sp;
    wchar_t *wsp;
    filepos fpos;
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
      case err_cantopen:
	sp = va_arg(ap, char *);
	sprintf(error, "unable to open input file `%.200s'", sp);
	flags = PREFIX;
	break;
      case err_nodata:		       /* no arguments */
	sprintf(error, "no data in input files");
	flags = PREFIX;
	break;
      case err_brokencodepara:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "every line of a code paragraph should begin `\\c'");
	flags = FILEPOS;
	break;
      case err_kwunclosed:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected `}' after paragraph keyword");
	flags = FILEPOS;
	break;
      case err_kwexpected:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected a paragraph keyword");
	flags = FILEPOS;
	break;
      case err_kwillegal:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected no paragraph keyword");
	flags = FILEPOS;
	break;
      case err_kwtoomany:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected only one paragraph keyword");
	flags = FILEPOS;
	break;
      case err_bodyillegal:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected no text after paragraph keyword");
	flags = FILEPOS;
	break;
      case err_badmidcmd:
	wsp = va_arg(ap, wchar_t *);
	sp = ustrtoa(wsp, auxbuf, sizeof(auxbuf));
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "command `%.200s' unexpected in mid-paragraph", sp);
	flags = FILEPOS;
	break;
      case err_unexbrace:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "brace character unexpected in mid-paragraph");
	flags = FILEPOS;
	break;
      case err_explbr:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected `{' after command");
	flags = FILEPOS;
	break;
      case err_kwexprbr:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected `}' after cross-reference");
	flags = FILEPOS;
	break;
      case err_missingrbrace:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unclosed braces at end of paragraph");
	flags = FILEPOS;
	break;
      case err_nestedstyles:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unable to nest text styles");
	flags = FILEPOS;
	break;
    }

    if (flags & PREFIX)
	fputs("buttress: ", stderr);
    if (flags & FILEPOS)
	fprintf(stderr, "%s:%d: ", fpos.filename, fpos.line);
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
