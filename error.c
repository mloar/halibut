/*
 * error.c: Halibut error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "halibut.h"

/*
 * Error flags
 */
#define PREFIX 0x0001		       /* give `halibut:' prefix */
#define FILEPOS 0x0002		       /* give file position prefix */

static void do_error(int code, va_list ap) {
    char error[1024];
    char c;
    int i, j;
    char *sp, *sp2;
    wchar_t *wsp;
    filepos fpos, fpos2;
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
      case err_badparatype:
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "command `%.200s' unrecognised at start of"
		" paragraph", sp);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_badmidcmd:
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "command `%.200s' unexpected in mid-paragraph", sp);
	flags = FILEPOS;
	sfree(sp);
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
      case err_commenteof:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "end of file unexpected inside `\\#{...}' comment");
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
      case err_missingrbrace2:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unclosed braces at end of input file");
	flags = FILEPOS;
	break;
      case err_nestedstyles:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unable to nest text styles");
	flags = FILEPOS;
	break;
      case err_nestedindex:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unable to nest index markings");
	flags = FILEPOS;
	break;
      case err_nosuchkw:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "unable to resolve cross-reference to `%.200s'", sp);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_multiBR:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "multiple `\\BR' entries given for `%.200s'", sp);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_nosuchidxtag:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "`\\IM' on unknown index tag `%.200s'", sp);
	sfree(sp);
	flags = FILEPOS;
	break;
      case err_cantopenw:
	sp = va_arg(ap, char *);
	sprintf(error, "unable to open output file `%.200s'", sp);
	flags = PREFIX;
	break;
      case err_macroexists:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "macro `%.200s' already defined", sp);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_sectjump:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "expected higher heading levels before this one");
	flags = FILEPOS;
	break;
      case err_winhelp_ctxclash:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sp2 = va_arg(ap, char *);
	sprintf(error, "Windows Help context id `%.200s' clashes with "
		"previously defined `%.200s'", sp, sp2);
	flags = FILEPOS;
	break;
      case err_multikw:
	fpos = *va_arg(ap, filepos *);
	fpos2 = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "paragraph keyword `%.200s' already defined at ", sp);
	sprintf(error + strlen(error), "%s:%d", fpos2.filename, fpos2.line);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_misplacedlcont:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "\\lcont is only expected after a list item");
	flags = FILEPOS;
	break;
      case err_sectmarkerinblock:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sprintf(error, "section headings are not supported within \\%.100s",
		sp);
	flags = FILEPOS;
	break;
      case err_infodirentry:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "\\cfg{info-dir-entry} expects at least three"
		" parameters");
	flags = FILEPOS;
	break;
      case err_infonodechar:
	fpos = *va_arg(ap, filepos *);
	c = (char)va_arg(ap, int);
	sprintf(error, "info output format does not support '%c' in"
		" node names; removing", c);
	flags = FILEPOS;
	break;
      case err_text_codeline:
	fpos = *va_arg(ap, filepos *);
	i = va_arg(ap, int);
	j = va_arg(ap, int);
	sprintf(error, "warning: code paragraph line is %d chars wide, wider"
		" than body width %d", i, j);
	flags = FILEPOS;
	break;
      case err_whatever:
	sp = va_arg(ap, char *);
        vsprintf(error, sp, ap);
        flags = PREFIX;
        break;
    }

    if (flags & PREFIX)
	fputs("halibut: ", stderr);
    if (flags & FILEPOS) {
	fprintf(stderr, "%s:", fpos.filename);
	if (fpos.line > 0)
	    fprintf(stderr, "%d:", fpos.line);
	if (fpos.col > 0)
	    fprintf(stderr, "%d:", fpos.col);
	fputc(' ', stderr);
    }
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
