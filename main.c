/*
 * main.c: command line parsing and top level
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "halibut.h"

static void dbg_prtsource(paragraph *sourceform);
static void dbg_prtwordlist(int level, word *w);
static void dbg_prtkws(keywordlist *kws);

static const struct backend {
    char *name;
    void (*func)(paragraph *, keywordlist *, indexdata *);
    int bitfield;
} backends[] = {
    {"text", text_backend, 0x0001},
    {"xhtml", xhtml_backend, 0x0002},
    {"html", xhtml_backend, 0x0002},
    {"hlp", whlp_backend, 0x0004},
    {"whlp", whlp_backend, 0x0004},
    {"winhelp", whlp_backend, 0x0004},
    {"man", man_backend, 0x0008},
};

int main(int argc, char **argv) {
    char **infiles;
    int nfiles;
    int nogo;
    int errs;
    int reportcols;
    int debug;
    int backendbits;
    int k, b;

    /*
     * Set up initial (default) parameters.
     */
    infiles = mknewa(char *, argc);
    nfiles = 0;
    nogo = errs = FALSE;
    reportcols = 0;
    debug = 0;
    backendbits = 0;

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

			assert(opt[0] == '-');
			for (k = 0; k < (int)lenof(backends); k++)
			    if (!strcmp(opt+1, backends[k].name)) {
				backendbits |= backends[k].bitfield;
				break;
			    }
			if (k < (int)lenof(backends)) {
			    /* do nothing */;
			} else if (!strcmp(opt, "-help")) {
			    help();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-version")) {
			    showversion();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-licence") ||
				   !strcmp(opt, "-license")) {
			    licence();
			    nogo = TRUE;
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
		  case 'd':
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
		      case 'd':
			debug = TRUE;
			break;
		    }
		    break;
#if 0
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
#endif
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
	paragraph *sourceform, *p;
	indexdata *idx;
	keywordlist *keywords;

	in.filenames = infiles;
	in.nfiles = nfiles;
	in.currfp = NULL;
	in.currindex = 0;
	in.npushback = in.pushbacksize = 0;
	in.pushback = NULL;
	in.reportcols = reportcols;
	in.stack = NULL;

	idx = make_index();

	sourceform = read_input(&in, idx);
	if (!sourceform)
	    exit(EXIT_FAILURE);

	sfree(in.pushback);

	mark_attr_ends(sourceform);

	sfree(infiles);

	keywords = get_keywords(sourceform);
	if (!keywords)
	    exit(EXIT_FAILURE);
	gen_citations(sourceform, keywords);
	subst_keywords(sourceform, keywords);

	for (p = sourceform; p; p = p->next)
	    if (p->type == para_IM)
		index_merge(idx, TRUE, p->keyword, p->words);

	build_index(idx);

	if (debug) {
	    index_debug(idx);
	    dbg_prtkws(keywords);
	    dbg_prtsource(sourceform);
	}

	/*
	 * Run the selected set of backends.
	 */
	for (k = b = 0; k < (int)lenof(backends); k++)
	    if (b != backends[k].bitfield) {
		b = backends[k].bitfield;
		if (backendbits == 0 || (backendbits & b))
		    backends[k].func(sourceform, keywords, idx);
	    }

	free_para_list(sourceform);
	free_keywords(keywords);
	cleanup_index(idx);
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
    keyword *kw;

    for (i = 0; (kw = index234(kws->keys, i)) != NULL; i++) {
	wchar_t *wp;
	printf("keyword ");
	wp = kw->key;
	while (*wp) {
	    putchar('\"');
	    for (; *wp; wp++)
		putchar(*wp);
	    putchar('\"');
	    if (*++wp)
		printf(", ");
	}
	printf(" {\n");
	dbg_prtwordlist(1, kw->text);
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
