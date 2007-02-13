/*
 * main.c: command line parsing and top level
 */

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include "halibut.h"

static void dbg_prtsource(paragraph *sourceform);
static void dbg_prtwordlist(int level, word *w);
static void dbg_prtkws(keywordlist *kws);

static const struct pre_backend {
    void *(*func)(paragraph *, keywordlist *, indexdata *);
    int bitfield;
} pre_backends[] = {
    {paper_pre_backend, 0x0001}
};

static const struct backend {
    char *name;
    void (*func)(paragraph *, keywordlist *, indexdata *, void *);
    paragraph *(*filename)(char *filename);
    int bitfield, prebackend_bitfield;
} backends[] = {
    {"text", text_backend, text_config_filename, 0x0001, 0},
    {"xhtml", html_backend, html_config_filename, 0x0002, 0},
    {"html", html_backend, html_config_filename, 0x0002, 0},
    {"hlp", whlp_backend, whlp_config_filename, 0x0004, 0},
    {"whlp", whlp_backend, whlp_config_filename, 0x0004, 0},
    {"winhelp", whlp_backend, whlp_config_filename, 0x0004, 0},
    {"man", man_backend, man_config_filename, 0x0008, 0},
    {"info", info_backend, info_config_filename, 0x0010, 0},
    {"ps", ps_backend, ps_config_filename, 0x0020, 0x0001},
    {"pdf", pdf_backend, pdf_config_filename, 0x0040, 0x0001},
};

int main(int argc, char **argv) {
    char **infiles;
    int nfiles;
    int nogo;
    int errs;
    int reportcols;
    int list_fonts;
    int input_charset;
    int debug;
    int backendbits, prebackbits;
    int k, b;
    paragraph *cfg, *cfg_tail;
    void *pre_backend_data[16];

    /*
     * Use the specified locale everywhere. It'll be used for
     * output of error messages, and as the default character set
     * for input files if one is not explicitly specified.
     * 
     * However, we need to use standard numeric formatting for
     * output of things like PDF.
     */
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");

    /*
     * Set up initial (default) parameters.
     */
    infiles = snewn(argc, char *);
    nfiles = 0;
    nogo = errs = FALSE;
    reportcols = 0;
    list_fonts = 0;
    input_charset = CS_ASCII;
    debug = 0;
    backendbits = 0;
    cfg = cfg_tail = NULL;

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
				if (val) {
				    paragraph *p = backends[k].filename(val);
				    assert(p);
				    if (cfg_tail)
					cfg_tail->next = p;
				    else
					cfg = p;
				    while (p->next)
					p = p->next;
				    cfg_tail = p;
				}
				break;
			    }
			if (k < (int)lenof(backends)) {
			    /* do nothing */;
			} else if (!strcmp(opt, "-input-charset")) {
			    if (!val) {
				errs = TRUE, error(err_optnoarg, opt);
			    } else {
				int charset = charset_from_localenc(val);
				if (charset == CS_NONE) {
				    errs = TRUE, error(err_cmdcharset, val);
				} else {
				    input_charset = charset;
				}
			    }
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
			} else if (!strcmp(opt, "-list-charsets")) {
			    listcharsets();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-list-fonts")) {
			    list_fonts = TRUE;
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
		  case 'C':
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
		      case 'C':
			/*
			 * -C means we split our argument up into
			 * colon-separated chunks and assemble them
			 * into a config paragraph.
			 */
			{
			    char *s = dupstr(p), *q, *r;
			    paragraph *para;

			    para = cmdline_cfg_new();

			    q = r = s;
			    while (*q) {
				if (*q == ':') {
				    *r = '\0';
				    /* XXX ad-hoc diagnostic */
				    if (!strcmp(s, "input-charset"))
					error(err_futileopt, "Cinput-charset",
					      "; use --input-charset");
				    cmdline_cfg_add(para, s);
				    r = s;
				} else {
				    if (*q == '\\' && q[1])
					q++;
				    *r++ = *q;
				}
				q++;
			    }
			    *r = '\0';
			    cmdline_cfg_add(para, s);

			    if (cfg_tail)
				cfg_tail->next = para;
			    else
				cfg = para;
			    cfg_tail = para;
			}
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
    if (nfiles == 0 && !list_fonts) {
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
	in.defcharset = input_charset;

	idx = make_index();

	sourceform = read_input(&in, idx);
	if (list_fonts) {
	    listfonts();
	    exit(EXIT_SUCCESS);
	}
	if (!sourceform)
	    exit(EXIT_FAILURE);

	/*
	 * Append the config directives acquired from the command
	 * line.
	 */
	{
	    paragraph *end;

	    end = sourceform;
	    while (end && end->next)
		end = end->next;
	    assert(end);

	    end->next = cfg;
	}

	sfree(in.pushback);

	sfree(infiles);

	keywords = get_keywords(sourceform);
	if (!keywords)
	    exit(EXIT_FAILURE);
	gen_citations(sourceform, keywords);
	subst_keywords(sourceform, keywords);

	for (p = sourceform; p; p = p->next)
	    if (p->type == para_IM)
		index_merge(idx, TRUE, p->keyword, p->words, &p->fpos);

	build_index(idx);

	/*
	 * Set up attr_First / attr_Last / attr_Always, in the main
	 * document and in the index entries.
	 */
	for (p = sourceform; p; p = p->next)
	    mark_attr_ends(p->words);
	{
	    int i;
	    indexentry *entry;

	    for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++)
		mark_attr_ends(entry->text);
	}

	if (debug) {
	    index_debug(idx);
	    dbg_prtkws(keywords);
	    dbg_prtsource(sourceform);
	}

	/*
	 * Select and run the pre-backends.
	 */
	prebackbits = 0;
	for (k = 0; k < (int)lenof(backends); k++)
	    if (backendbits == 0 || (backendbits & backends[k].bitfield))
		prebackbits |= backends[k].prebackend_bitfield;
	for (k = 0; k < (int)lenof(pre_backends); k++)
	    if (prebackbits & pre_backends[k].bitfield) {
		assert(k < (int)lenof(pre_backend_data));
		pre_backend_data[k] =
		    pre_backends[k].func(sourceform, keywords, idx);
	    }

	/*
	 * Run the selected set of backends.
	 */
	for (k = b = 0; k < (int)lenof(backends); k++)
	    if (b != backends[k].bitfield) {
		b = backends[k].bitfield;
		if (backendbits == 0 || (backendbits & b)) {
		    void *pbd = NULL;
		    int pbb = backends[k].prebackend_bitfield;
		    int m;

		    for (m = 0; m < (int)lenof(pre_backends); m++)
			if (pbb & pre_backends[m].bitfield) {
			    assert(m < (int)lenof(pre_backend_data));
			    pbd = pre_backend_data[m];
			    break;
			}
			    
		    backends[k].func(sourceform, keywords, idx, pbd);
		}
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
	if (w->breaks)
	    printf(" [breaks]");
	if (w->alt) {
	    printf(" alt = {\n");
	    dbg_prtwordlist(level+1, w->alt);
	    printf("%*s}", level*4, "");
	}
	printf("\n");
    }
}
