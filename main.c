/*
 * main.c: command line parsing and top level
 */

#include <stdio.h>
#include <stdlib.h>
#include "buttress.h"

static void dbg_prtsource(paragraph *sourceform);
static void dbg_prtwordlist(int level, word *w);
static void dbg_prtkws(keywordlist *kws);

int main(int argc, char **argv) {
    char **infiles;
    char *outfile;
    int nfiles;
    int nogo;
    int errs;
    int reportcols;

    /*
     * Set up initial (default) parameters.
     */
    infiles = mknewa(char *, argc);
    outfile = NULL;
    nfiles = 0;
    nogo = errs = FALSE;
    reportcols = 0;

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
			} else if (!strcmp(opt, "-precise")) {
			    reportcols = 1;
			} else {
			    errs = TRUE, error(err_nosuchopt, opt);
			}
		    }
		    p = NULL;
		    break;
		  case 'h':
		  case 'V':
		  case 'L':
		  case 'P':
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
		      case 'P':
			reportcols = 1;
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
	keywordlist *keywords;

	in.filenames = infiles;
	in.nfiles = nfiles;
	in.currfp = NULL;
	in.currindex = 0;
	in.npushback = 0;
	in.reportcols = reportcols;

	sourceform = read_input(&in);
	if (!sourceform)
	    exit(EXIT_FAILURE);

	sfree(infiles);

	keywords = get_keywords(sourceform);
	gen_citations(sourceform, keywords);
	subst_keywords(sourceform, keywords);

	dbg_prtkws(keywords);
	dbg_prtsource(sourceform);

	free_para_list(sourceform);
    }

    return 0;
}

static void dbg_prtsource(paragraph *sourceform) {
    /*
     * Output source form in debugging format.
     */

    paragraph *p;
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
	dbg_prtwordlist(1, p->words);
	printf("}\n");
    }
}

static void dbg_prtkws(keywordlist *kws) {
    /*
     * Output keywords in debugging format.
     */

    int i;

    for (i = 0; i < kws->nkeywords; i++) {
	wchar_t *wp;
	printf("keyword ");
	wp = kws->keys[i]->key;
	while (*wp) {
	    putchar('\"');
	    for (; *wp; wp++)
		putchar(*wp);
	    putchar('\"');
	    if (*++wp)
		printf(", ");
	}
	printf(" {\n");
	dbg_prtwordlist(1, kws->keys[i]->text);
	printf("}\n");
    }
}

static void dbg_prtwordlist(int level, word *w) {
    for (; w; w = w->next) {
	wchar_t *wp;
	printf("%*sword %d ", level*4, "", w->type);
	if (w->text) {
	    printf("\"");
	    for (wp = w->text; *wp; wp++)
		    putchar(*wp);
	    printf("\"");
	} else
	    printf("(no text)");
	if (w->alt) {
	    printf(" alt = {\n");
	    dbg_prtwordlist(level+1, w->alt);
	    printf("%*s}", level*4, "");
	}
	printf("\n");
    }
}
