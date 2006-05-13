/*
 * licence.c: licence text
 */

#include <stdio.h>

static const char *const licencetext[] = {
    "Halibut is copyright (c) 1999-2006 Simon Tatham and James Aylett.",
    "",
    "Permission is hereby granted, free of charge, to any person",
    "obtaining a copy of this software and associated documentation files",
    "(the \"Software\"), to deal in the Software without restriction,",
    "including without limitation the rights to use, copy, modify, merge,",
    "publish, distribute, sublicense, and/or sell copies of the Software,",
    "and to permit persons to whom the Software is furnished to do so,",
    "subject to the following conditions:",
    "",
    "The above copyright notice and this permission notice shall be",
    "included in all copies or substantial portions of the Software.",
    "",
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,",
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF",
    "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND",
    "NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS",
    "BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN",
    "ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN",
    "CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE",
    "SOFTWARE.",
    "",
    "Halibut contains font metrics derived from the \"Font Metrics for PDF",
    "Core 14 Fonts\", which carry the following copyright notice and licence:",
    "",
    "  Copyright (c) 1985, 1987, 1989, 1990, 1991, 1992, 1993, 1997",
    "  Adobe Systems Incorporated.  All Rights Reserved.",
    "",
    "  This file and the 14 PostScript(R) AFM files it accompanies may be",
    "  used, copied, and distributed for any purpose and without charge,",
    "  with or without modification, provided that all copyright notices",
    "  are retained; that the AFM files are not distributed without this",
    "  file; that all modifications to this file or any of the AFM files",
    "  are prominently noted in the modified file(s); and that this",
    "  paragraph is not modified. Adobe Systems has no responsibility or",
    "  obligation to support the use of the AFM files.",
    NULL
};

void licence(void) {
    const char *const *p;
    for (p = licencetext; *p; p++)
	puts(*p);
}
