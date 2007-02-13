/*
 * help.c: usage instructions
 */

#include <stdio.h>
#include "halibut.h"

static const char *const helptext[] = {
    "usage:   halibut [options] files",
    "options: --text[=filename]     generate plain text output",
    "         --html[=filename]     generate XHTML output",
    "         --winhelp[=filename]  generate Windows Help output",
    "         --man[=filename]      generate man page output",
    "         --info[=filename]     generate GNU info output",
    "         --ps[=filename]       generate PostScript output",
    "         --pdf[=filename]      generate PDF output",
    "         -Cfoo:bar:baz         append \\cfg{foo}{bar}{baz} to input",
    "         --input-charset=cs    change default input file charset",
    "         --list-charsets       display supported character set names",
    "         --list-fonts          display supported font names",
    "         --precise             report column numbers in error messages",
    "         --help                display this text",
    "         --version             display version number",
    "         --licence             display licence text",
    NULL
};

static const char *const usagetext[] = {
    "usage: halibut [--format[=filename]] [options] file.but [file.but...]",
    NULL
};

void help(void) {
    const char *const *p;
    for (p = helptext; *p; p++)
	puts(*p);
}

void usage(void) {
    const char *const *p;
    for (p = usagetext; *p; p++)
	puts(*p);
}

void showversion(void) {
    printf("Halibut, %s\n", version);
}

void listcharsets(void) {
    int i = 0, c;
    do {
	c = charset_localenc_nth(i);
	if (c == CS_NONE) break;
	printf("%s\n", charset_to_localenc(c));
	i++;
    } while (1);
}
