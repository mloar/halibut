/*
 * main.c: command line parsing and top level
 */

#include <stdio.h>
#include <stdlib.h>
#include "buttress.h"

int main(int argc, char **argv) {
    char **infiles;
    char *outfile;
    int nfiles;
    int nogo;
    int errs;

    /*
     * Set up initial (default) parameters.
     */
    infiles = smalloc(argc*sizeof(char *));
    outfile = NULL;
    nfiles = 0;
    nogo = errs = FALSE;

    if (argc == 1) {
	usage();
	exit(EXIT_SUCCESS);
    }

    /*
     * Parse command line arguments.
     */
    while (--argc) {
	char *p = *++argv;
	if (*p == '-') {
	    /*
	     * An option.
	     */
	    while (p && *++p) {
		char c = *p;
		switch (c) {
		  case '-':
		    /*
		     * Long option.
		     */
		    {
			char *opt, *val;
			opt = p++;     /* opt will have _one_ leading - */
			while (*p && *p != '=')
			    p++;	       /* find end of option */
			if (*p == '=') {
			    *p++ = '\0';
			    val = p;
			} else
			    val = NULL;
			if (!strcmp(opt, "-help")) {
			    help();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-version")) {
			    showversion();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-licence") ||
				   !strcmp(opt, "-license")) {
			    licence();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-output")) {
			    if (!val)
				errs = TRUE, error(err_optnoarg, opt);
			    else
				outfile = val;
			} else {
			    errs = TRUE, error(err_nosuchopt, opt);
			}
		    }
		    p = NULL;
		    break;
		  case 'h':
		  case 'V':
		  case 'L':
		    /*
		     * Option requiring no parameter.
		     */
		    switch (c) {
		      case 'h':
			help();
			nogo = TRUE;
			break;
		      case 'V':
			showversion();
			nogo = TRUE;
			break;
		      case 'L':
			licence();
			nogo = TRUE;
			break;
		    }
		    break;
		  case 'o':
		    /*
		     * Option requiring parameter.
		     */
		    p++;
		    if (!*p && argc > 1)
			--argc, p = *++argv;
		    else if (!*p) {
			char opt[2];
			opt[0] = c;
			opt[1] = '\0';
			errs = TRUE, error(err_optnoarg, opt);
		    }
		    /*
		     * Now c is the option and p is the parameter.
		     */
		    switch (c) {
		      case 'o':
			outfile = p;
			break;
		    }
		    p = NULL;	       /* prevent continued processing */
		    break;
		  default:
		    /*
		     * Unrecognised option.
		     */
		    {
			char opt[2];
			opt[0] = c;
			opt[1] = '\0';
			errs = TRUE, error(err_nosuchopt, opt);
		    }
		}
	    }
	} else {
	    /*
	     * A non-option argument.
	     */
	    infiles[nfiles++] = p;
	}
    }

    if (errs)
	exit(EXIT_FAILURE);
    if (nogo)
	exit(EXIT_SUCCESS);

    /*
     * Do the work.
     */
    if (nfiles == 0) {
	error(err_noinput);
	usage();
	exit(EXIT_FAILURE);
    }

    {
	input in;
	paragraph *sourceform;

	in.filenames = infiles;
	in.nfiles = nfiles;
	in.currfp = NULL;
	in.currindex = 0;
	in.npushback = 0;

	sourceform = read_input(&in);
	if (!sourceform)
	    exit(EXIT_FAILURE);

	sfree(infiles);

	/*
	 * FIXME: having read it, do something with it!
	 */
	{
	    paragraph *p;
	    word *w;
	    for (p = sourceform; p; p = p->next) {
		wchar_t *wp;
		printf("para %d ", p->type);
		if (p->keyword) {
		    wp = p->keyword;
		    while (*wp) {
			putchar('\"');
			for (; *wp; wp++)
			    putchar(*wp);
			putchar('\"');
			if (*++wp)
			    printf(", ");
		    }
		} else
		    printf("(no keyword)");
		printf(" {\n");
		for (w = p->words; w; w = w->next) {
		    printf("    word %d ", w->type);
		    if (w->text) {
			printf("\"");
			for (wp = w->text; *wp; wp++)
			    putchar(*wp);
			printf("\"");
		    } else
			printf("(no text)");
		    printf("\n");
		}
		printf("}\n");
	    }
	}
    }

    return 0;
}
