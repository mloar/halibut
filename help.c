/*
 * help.c: usage instructions
 */

#include <stdio.h>
#include "halibut.h"

static char *helptext[] = {
    "usage:   halibut [options] files",
    "options: --text[=filename]     generate plain text output",
    "         --html[=filename]     generate XHTML output",
    "         --winhelp[=filename]  generate Windows Help output",
    "         --man[=filename]      generate man page output",
    "         -Cfoo:bar:baz         append \\cfg{foo}{bar}{baz} to input",
    "         --precise             report column numbers in error messages",
    "         --help                display this text",
    "         --version             display version number",
    "         --licence             display licence text",
    NULL
};

static char *usagetext[] = {
    "usage: halibut [--format[=filename]] [-Cconfig...] file.but [file.but...]",
    NULL
};

void help(void) {
    char **p;
    for (p = helptext; *p; p++)
	puts(*p);
}

void usage(void) {
    char **p;
    for (p = usagetext; *p; p++)
	puts(*p);
}

void showversion(void) {
    printf("Halibut, %s\n", version);
}
