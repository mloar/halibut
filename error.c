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
    wchar_t *wsp, *wsp2;
    filepos fpos, fpos2, *fposp;
    int flags = 0;

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
      case err_cmdcharset:
	sp = va_arg(ap, char *);
	sprintf(error, "character set `%.200s' not recognised", sp);
	flags = PREFIX;
	break;
      case err_futileopt:
	sp = va_arg(ap, char *);
	sp2 = va_arg(ap, char *);
	sprintf(error, "warning: option `-%s' has no effect%s", sp, sp2);
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
      case err_codequote:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unable to nest \\q{...} within \\c{...} or \\cw{...}");
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
      case err_indexcase:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	fpos2 = *va_arg(ap, filepos *);
	wsp2 = va_arg(ap, wchar_t *);
	sp2 = utoa_locale_dup(wsp2);
	sprintf(error, "warning: index tag `%.200s' used with ", sp);
	sprintf(error + strlen(error), "different case (`%.200s') at %s:%d",
		sp2, fpos2.filename, fpos2.line);
	flags = FILEPOS;
	sfree(sp);
	sfree(sp2);
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
      case err_cfginsufarg:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	i = va_arg(ap, int);
	sprintf(error, "\\cfg{%s} expects at least %d parameter%s", sp,
		i, (i==1)?"":"s");
	flags = FILEPOS;
	break;
      case err_infonodechar:
	fposp = va_arg(ap, filepos *);
	c = (char)va_arg(ap, int);
	sprintf(error, "info output format does not support '%c' in"
		" node names; removing", c);
	if (fposp) {
	    flags = FILEPOS;
	    fpos = *fposp;
	}
	break;
      case err_text_codeline:
	fpos = *va_arg(ap, filepos *);
	i = va_arg(ap, int);
	j = va_arg(ap, int);
	sprintf(error, "warning: code paragraph line is %d chars wide, wider"
		" than body width %d", i, j);
	flags = FILEPOS;
	break;
      case err_htmlver:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "unrecognised HTML version keyword `%.200s'", sp);
	sfree(sp);
	flags = FILEPOS;
	break;
      case err_charset:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "character set `%.200s' not recognised", sp);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_nofont:
	fpos = *va_arg(ap, filepos *);
	wsp = va_arg(ap, wchar_t *);
	sp = utoa_locale_dup(wsp);
	sprintf(error, "font `%.200s' not recognised", sp);
	flags = FILEPOS;
	sfree(sp);
	break;
      case err_afmeof:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "AFM file ended unexpectedly");
	flags = FILEPOS;
	break;
      case err_afmkey:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sprintf(error, "required AFM key '%.200s' missing", sp);
	flags = FILEPOS;
	break;
      case err_afmvers:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "unsupported AFM version");
	flags = FILEPOS;
	break;
      case err_afmval:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	i = va_arg(ap, int);
	if (i == 1)
	    sprintf(error, "AFM key '%.200s' requires a value", sp);
	else
	    sprintf(error, "AFM key '%.200s' requires %d values", sp, i);
	flags = FILEPOS;
	break;	
      case err_pfeof:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "Type 1 font file ended unexpectedly");
	flags = FILEPOS;
	break;
      case err_pfhead:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "Type 1 font file header line invalid");
	flags = FILEPOS;
	break;
      case err_pfbad:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "Type 1 font file invalid");
	flags = FILEPOS;
	break;
      case err_pfnoafm:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sprintf(error, "no metrics available for Type 1 font '%.200s'", sp);
	flags = FILEPOS;
	break;
      case err_chmnames:
	sprintf(error, "only one of html-mshtmlhelp-chm and "
		"html-mshtmlhelp-hhp found");
	flags = PREFIX;
	break;
      case err_sfntnotable:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sprintf(error, "font has no '%.4s' table", sp);
	flags = FILEPOS;
	break;
      case err_sfntnopsname:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "font has no PostScript name");
	flags = FILEPOS;
	break;
      case err_sfntbadtable:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sprintf(error, "font has an invalid '%.4s' table", sp);
	flags = FILEPOS;
	break;
      case err_sfntnounicmap:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "font has no UCS-2 character map");
	flags = FILEPOS;
	break;	
      case err_sfnttablevers:
	fpos = *va_arg(ap, filepos *);
	sp = va_arg(ap, char *);
	sprintf(error, "font has an unsupported '%.4s' table version", sp);
	flags = FILEPOS;
	break;
      case err_sfntbadhdr:
	fpos = *va_arg(ap, filepos *);
	sprintf(error, "font has an invalid header");
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
