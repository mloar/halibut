/*
 * HTML backend for Halibut
 */

/*
 * TODO:
 * 
 *  - I'm never entirely convinced that having a fragment link to
 *    come in at the start of the real text in the file is
 *    sensible. Perhaps for the topmost section in the file, no
 *    fragment should be used? (Though it should probably still be
 *    _there_ even if unused.)
 * 
 *  - new configurability:
 *     * a few new things explicitly labelled as `FIXME:
 * 	 configurable' or similar.
 *     * HTML flavour.
 *     * Some means of specifying the distinction between
 * 	 restrict-charset and output-charset. It seems to me that
 * 	 `html-charset' is output-charset, and that
 * 	 restrict-charset usually wants to be either output-charset
 * 	 or UTF-8 (the latter indicating that any Unicode character
 * 	 is fair game and it will be specified using &#foo; if it
 * 	 isn't in output-charset). However, since XHTML defaults to
 * 	 UTF-8 and it's fiddly to tell it otherwise, it's just
 * 	 possible that some user may need to set restrict-charset
 * 	 to their charset of choice while leaving _output_-charset
 * 	 at UTF-8. Figure out some configuration, and apply it.
 *
 *  - test all HTML flavours and ensure they validate sensibly. Fix
 *    remaining confusion issues such as <?xml?> and obsoleteness
 *    of <a name>.
 * 
 *  - proper naming of all fragment IDs. The ones for sections are
 *    fine; the ones for numbered list and bibliociteds are utter
 *    crap; the ones for indexes _might_ do but it might be worth
 *    giving some thought to how to do them better.
 * 
 *  - nonbreaking spaces.
 * 
 *  - free up all the data we have allocated while running this
 *    backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include "halibut.h"

#define is_heading_type(type) ( (type) == para_Title || \
				(type) == para_Chapter || \
				(type) == para_Appendix || \
				(type) == para_UnnumberedChapter || \
				(type) == para_Heading || \
				(type) == para_Subsect)

#define heading_depth(p) ( (p)->type == para_Subsect ? (p)->aux + 1 : \
			   (p)->type == para_Heading ? 1 : \
			   (p)->type == para_Title ? -1 : 0 )

typedef struct {
    int just_numbers;
    wchar_t *number_suffix;
} sectlevel;

typedef struct {
    int nasect;
    sectlevel achapter, *asect;
    int *contents_depths;	       /* 0=main, 1=chapter, 2=sect etc */
    int ncdepths;
    int address_section, visible_version_id;
    int leaf_contains_contents, leaf_smallest_contents;
    char *contents_filename;
    char *index_filename;
    char *template_filename;
    char *single_filename;
    char *template_fragment;
    char *head_end, *body_start, *body_end, *addr_start, *addr_end;
    char *body_tag, *nav_attr;
    wchar_t *author, *description;
    int restrict_charset, output_charset;
    enum {
	HTML_3_2, HTML_4,
	XHTML_1_0_TRANSITIONAL, XHTML_1_0_STRICT
    } htmlver;
    wchar_t *lquote, *rquote;
    int leaf_level;
} htmlconfig;

#define contents_depth(conf, level) \
    ( (conf).ncdepths > (level) ? (conf).contents_depths[level] : (level)+2 )

#define is_xhtml(ver) ((ver) >= XHTML_1_0_TRANSITIONAL)

typedef struct htmlfile htmlfile;
typedef struct htmlsect htmlsect;

struct htmlfile {
    htmlfile *next;
    char *filename;
    int last_fragment_number;
    int min_heading_depth;
    htmlsect *first, *last;	       /* first/last highest-level sections */
};

struct htmlsect {
    htmlsect *next, *parent;
    htmlfile *file;
    paragraph *title, *text;
    enum { NORMAL, TOP, INDEX } type;
    int contents_depth;
    char *fragment;
};

typedef struct {
    htmlfile *head, *tail;
    htmlfile *single, *index;
    tree234 *frags;
} htmlfilelist;

typedef struct {
    htmlsect *head, *tail;
} htmlsectlist;

typedef struct {
    htmlfile *file;
    char *fragment;
} htmlfragment;

typedef struct {
    int nrefs, refsize;
    word **refs;
} htmlindex;

typedef struct {
    htmlsect *section;
    char *fragment;
} htmlindexref;

typedef struct {
    /*
     * This level deals with charset conversion, starting and
     * ending tags, and writing to the file. It's the lexical
     * level.
     */
    FILE *fp;
    int charset;
    charset_state cstate;
    int ver;
    enum {
	HO_NEUTRAL, HO_IN_TAG, HO_IN_EMPTY_TAG, HO_IN_TEXT
    } state;
    /*
     * Stuff beyond here deals with the higher syntactic level: it
     * tracks how many levels of <ul> are currently open when
     * producing a contents list, for example.
     */
    int contents_level;
} htmloutput;

static int html_fragment_compare(void *av, void *bv)
{
    htmlfragment *a = (htmlfragment *)av;
    htmlfragment *b = (htmlfragment *)bv;
    int cmp;

    if ((cmp = strcmp(a->file->filename, b->file->filename)) != 0)
	return cmp;
    else
	return strcmp(a->fragment, b->fragment);
}

static void html_file_section(htmlconfig *cfg, htmlfilelist *files,
			      htmlsect *sect, int depth);

static htmlfile *html_new_file(htmlfilelist *list, char *filename);
static htmlsect *html_new_sect(htmlsectlist *list, paragraph *title);

/* Flags for html_words() flags parameter */
#define NOTHING 0x00
#define MARKUP 0x01
#define LINKS 0x02
#define INDEXENTS 0x04
#define ALL 0x07
static void html_words(htmloutput *ho, word *words, int flags,
		       htmlfile *file, keywordlist *keywords, htmlconfig *cfg);
static void html_codepara(htmloutput *ho, word *words);

static void element_open(htmloutput *ho, char const *name);
static void element_close(htmloutput *ho, char const *name);
static void element_empty(htmloutput *ho, char const *name);
static void element_attr(htmloutput *ho, char const *name, char const *value);
static void element_attr_w(htmloutput *ho, char const *name,
			   wchar_t const *value);
static void html_text(htmloutput *ho, wchar_t const *str);
static void html_text_limit(htmloutput *ho, wchar_t const *str, int maxlen);
static void html_text_limit_internal(htmloutput *ho, wchar_t const *text,
				     int maxlen, int quote_quotes);
static void html_nl(htmloutput *ho);
static void html_raw(htmloutput *ho, char *text);
static void html_raw_as_attr(htmloutput *ho, char *text);
static void cleanup(htmloutput *ho);

static void html_href(htmloutput *ho, htmlfile *thisfile,
		      htmlfile *targetfile, char *targetfrag);

static char *html_format(paragraph *p, char *template_string);
static char *html_sanitise_fragment(htmlfilelist *files, htmlfile *file,
				    char *text);

static void html_contents_entry(htmloutput *ho, int depth, htmlsect *s,
				htmlfile *thisfile, keywordlist *keywords,
				htmlconfig *cfg);
static void html_section_title(htmloutput *ho, htmlsect *s,
			       htmlfile *thisfile, keywordlist *keywords,
			       htmlconfig *cfg, int real);

static htmlconfig html_configure(paragraph *source) {
    htmlconfig ret;
    paragraph *p;

    /*
     * Defaults.
     */
    ret.leaf_level = 2;
    ret.achapter.just_numbers = FALSE;
    ret.achapter.number_suffix = L": ";
    ret.nasect = 1;
    ret.asect = snewn(ret.nasect, sectlevel);
    ret.asect[0].just_numbers = TRUE;
    ret.asect[0].number_suffix = L" ";
    ret.ncdepths = 0;
    ret.contents_depths = 0;
    ret.visible_version_id = TRUE;
    ret.address_section = TRUE;
    ret.leaf_contains_contents = FALSE;
    ret.leaf_smallest_contents = 4;
    ret.single_filename = dupstr("Manual.html");
    ret.contents_filename = dupstr("Contents.html");
    ret.index_filename = dupstr("IndexPage.html");
    ret.template_filename = dupstr("%n.html");
    ret.template_fragment = dupstr("%b");
    ret.head_end = ret.body_tag = ret.body_start = ret.body_end =
	ret.addr_start = ret.addr_end = ret.nav_attr = NULL;
    ret.author = ret.description = NULL;
    ret.restrict_charset = CS_ASCII;
    ret.output_charset = CS_ASCII;
    ret.htmlver = HTML_4;
    /*
     * Default quote characters are Unicode matched single quotes,
     * falling back to ordinary ASCII ".
     */
    ret.lquote = L"\x2018\0\x2019\0\"\0\"\0\0";
    ret.rquote = uadv(ret.lquote);

    /*
     * Two-pass configuration so that we can pick up global config
     * (e.g. `quotes') before having it overridden by specific
     * config (`html-quotes'), irrespective of the order in which
     * they occur.
     */
    for (p = source; p; p = p->next) {
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    }
	}
    }

    for (p = source; p; p = p->next) {
	if (p->type == para_Config) {
	    wchar_t *k = p->keyword;

	    if (!ustrnicmp(k, L"xhtml-", 6))
		k++;		    /* treat `xhtml-' and `html-' the same */

	    if (!ustricmp(k, L"html-charset")) {
		char *csname = utoa_dup(uadv(k), CS_ASCII);
		ret.restrict_charset = ret.output_charset =
		    charset_from_localenc(csname);
		sfree(csname);
	    } else if (!ustricmp(k, L"html-single-filename")) {
		sfree(ret.single_filename);
		ret.single_filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(k, L"html-contents-filename")) {
		sfree(ret.contents_filename);
		ret.contents_filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(k, L"html-index-filename")) {
		sfree(ret.index_filename);
		ret.index_filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(k, L"html-template-filename")) {
		sfree(ret.template_filename);
		ret.template_filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(k, L"html-template-fragment")) {
		sfree(ret.template_fragment);
		ret.template_fragment = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(k, L"html-chapter-numeric")) {
		ret.achapter.just_numbers = utob(uadv(k));
	    } else if (!ustricmp(k, L"html-chapter-suffix")) {
		ret.achapter.number_suffix = uadv(k);
	    } else if (!ustricmp(k, L"html-leaf-level")) {
		ret.leaf_level = utoi(uadv(k));
	    } else if (!ustricmp(k, L"html-section-numeric")) {
		wchar_t *q = uadv(k);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = sresize(ret.asect, n+1, sectlevel);
		    for (i = ret.nasect; i <= n; i++)
			ret.asect[i] = ret.asect[ret.nasect-1];
		    ret.nasect = n+1;
		}
		ret.asect[n].just_numbers = utob(q);
	    } else if (!ustricmp(k, L"html-section-suffix")) {
		wchar_t *q = uadv(k);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.nasect) {
		    int i;
		    ret.asect = sresize(ret.asect, n+1, sectlevel);
		    for (i = ret.nasect; i <= n; i++) {
			ret.asect[i] = ret.asect[ret.nasect-1];
		    }
		    ret.nasect = n+1;
		}
		ret.asect[n].number_suffix = q;
	    } else if (!ustricmp(k, L"html-contents-depth") ||
		       !ustrnicmp(k, L"html-contents-depth-", 20)) {
		/*
		 * Relic of old implementation: this directive used
		 * to be written as \cfg{html-contents-depth-3}{2}
		 * rather than the usual Halibut convention of
		 * \cfg{html-contents-depth}{3}{2}. We therefore
		 * support both.
		 */
		wchar_t *q = k[19] ? k+20 : uadv(k);
		int n = 0;
		if (uisdigit(*q)) {
		    n = utoi(q);
		    q = uadv(q);
		}
		if (n >= ret.ncdepths) {
		    int i;
		    ret.contents_depths =
			sresize(ret.contents_depths, n+1, int);
		    for (i = ret.ncdepths; i <= n; i++) {
			ret.contents_depths[i] = i+2;
		    }
		    ret.ncdepths = n+1;
		}
		ret.contents_depths[n] = utoi(q);
	    } else if (!ustricmp(k, L"html-head-end")) {
		ret.head_end = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-body-tag")) {
		ret.body_tag = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-body-start")) {
		ret.body_start = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-body-end")) {
		ret.body_end = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-address-start")) {
		ret.addr_start = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-address-end")) {
		ret.addr_end = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-navigation-attributes")) {
		ret.nav_attr = adv(p->origkeyword);
	    } else if (!ustricmp(k, L"html-author")) {
		ret.author = uadv(k);
	    } else if (!ustricmp(k, L"html-description")) {
		ret.description = uadv(k);
	    } else if (!ustricmp(k, L"html-suppress-address")) {
		ret.address_section = !utob(uadv(k));
	    } else if (!ustricmp(k, L"html-versionid")) {
		ret.visible_version_id = utob(uadv(k));
	    } else if (!ustricmp(k, L"html-quotes")) {
		if (*uadv(k) && *uadv(uadv(k))) {
		    ret.lquote = uadv(k);
		    ret.rquote = uadv(ret.lquote);
		}
	    } else if (!ustricmp(k, L"html-leaf-contains-contents")) {
		ret.leaf_contains_contents = utob(uadv(k));
	    } else if (!ustricmp(k, L"html-leaf-smallest-contents")) {
		ret.leaf_smallest_contents = utoi(uadv(k));
	    }
	}
    }

    /*
     * Now process fallbacks on quote characters.
     */
    while (*uadv(ret.rquote) && *uadv(uadv(ret.rquote)) &&
	   (!cvt_ok(ret.restrict_charset, ret.lquote) ||
	    !cvt_ok(ret.restrict_charset, ret.rquote))) {
	ret.lquote = uadv(ret.rquote);
	ret.rquote = uadv(ret.lquote);
    }

    return ret;
}

paragraph *html_config_filename(char *filename)
{
    /*
     * If the user passes in a single filename as a parameter to
     * the `--html' command-line option, then we should assume it
     * to imply _two_ config directives:
     * \cfg{html-single-filename}{whatever} and
     * \cfg{html-leaf-level}{0}; the rationale being that the user
     * wants their output _in that file_.
     */
    paragraph *p, *q;

    p = cmdline_cfg_simple("html-single-filename", filename, NULL);
    q = cmdline_cfg_simple("html-leaf-level", "0", NULL);
    p->next = q;
    return p;
}

void html_backend(paragraph *sourceform, keywordlist *keywords,
		  indexdata *idx, void *unused) {
    paragraph *p;
    htmlconfig conf;
    htmlfilelist files = { NULL, NULL, NULL, NULL, NULL };
    htmlsectlist sects = { NULL, NULL }, nonsects = { NULL, NULL };

    IGNORE(unused);

    conf = html_configure(sourceform);

    /*
     * We're going to make heavy use of paragraphs' private data
     * fields in the forthcoming code. Clear them first, so we can
     * reliably tell whether we have auxiliary data for a
     * particular paragraph.
     */
    for (p = sourceform; p; p = p->next)
	p->private_data = NULL;

    files.frags = newtree234(html_fragment_compare);

    /*
     * Start by figuring out into which file each piece of the
     * document should be put. We'll do this by inventing an
     * `htmlsect' structure and stashing it in the private_data
     * field of each section paragraph; we also need one additional
     * htmlsect for the document index, which won't show up in the
     * source form but needs to be consistently mentioned in
     * contents links.
     * 
     * While we're here, we'll also invent the HTML fragment name
     * for each section.
     */
    {
	htmlsect *topsect, *sect;
	int d;

	topsect = html_new_sect(&sects, p);
	topsect->type = TOP;
	topsect->title = NULL;
	topsect->text = sourceform;
	topsect->contents_depth = contents_depth(conf, 0);
	html_file_section(&conf, &files, topsect, -1);
	topsect->fragment = NULL;

	for (p = sourceform; p; p = p->next)
	    if (is_heading_type(p->type)) {
		d = heading_depth(p);

		if (p->type == para_Title) {
		    topsect->title = p;
		    continue;
		}

		sect = html_new_sect(&sects, p);
		sect->text = p->next;

		sect->contents_depth = contents_depth(conf, d+1) - (d+1);

		if (p->parent) {
		    sect->parent = (htmlsect *)p->parent->private_data;
		    assert(sect->parent != NULL);
		} else
		    sect->parent = topsect;
		p->private_data = sect;

		html_file_section(&conf, &files, sect, d);

		sect->fragment = html_format(p, conf.template_fragment);
		sect->fragment = html_sanitise_fragment(&files, sect->file,
							sect->fragment);
	    }

	/* And the index. */
	sect = html_new_sect(&sects, NULL);
	sect->text = NULL;
	sect->type = INDEX;
	sect->parent = topsect;
	html_file_section(&conf, &files, sect, 0);   /* peer of chapters */
	sect->fragment = dupstr("Index");   /* FIXME: this _can't_ be right */
	sect->fragment = html_sanitise_fragment(&files, sect->file,
						sect->fragment);
	files.index = sect->file;
    }

    /*
     * Go through the keyword list and sort out fragment IDs for
     * all the potentially referenced paragraphs which _aren't_
     * headings.
     */
    {
	int i;
	keyword *kw;
	htmlsect *sect;

	for (i = 0; (kw = index234(keywords->keys, i)) != NULL; i++) {
	    paragraph *q, *p = kw->para;

	    if (!is_heading_type(p->type)) {
		htmlsect *parent;

		/*
		 * Find the paragraph's parent htmlsect, to
		 * determine which file it will end up in.
		 */
		q = p->parent;
		if (!q) {
		    /*
		     * Preamble paragraphs have no parent. So if we
		     * have a non-heading with no parent, it must
		     * be preamble, and therefore its parent
		     * htmlsect must be the preamble one.
		     */
		    assert(sects.head &&
			   sects.head->type == TOP);
		    parent = sects.head;
		} else
		    parent = (htmlsect *)q->private_data;

		/*
		 * Now we can construct an htmlsect for this
		 * paragraph itself, taking care to put it in the
		 * list of non-sections rather than the list of
		 * sections (so that traverses of the `sects' list
		 * won't attempt to add it to the contents or
		 * anything weird like that).
		 */
		sect = html_new_sect(&nonsects, p);
		sect->file = parent->file;
		sect->parent = parent;
		p->private_data = sect;

		/*
		 * FIXME: We need a much better means of naming
		 * these, possibly involving an additional
		 * configuration template. For the moment I'll just
		 * invent something completely stupid.
		 */
		sect->fragment = snewn(40, char);
		sprintf(sect->fragment, "frag%p", sect);
		sect->fragment = html_sanitise_fragment(&files, sect->file,
							sect->fragment);
	    }
	}
    }

    /*
     * Now sort out the index. This involves:
     * 
     * 	- For each index term, we set up an htmlindex structure to
     * 	  store all the references to that term.
     * 
     * 	- Then we make a pass over the actual document, finding
     * 	  every word_IndexRef; for each one, we actually figure out
     * 	  the HTML filename/fragment pair we will use to reference
     * 	  it, store that information in the private data field of
     * 	  the word_IndexRef itself (so we can recreate it when the
     * 	  time comes to output our HTML), and add a reference to it
     * 	  to the index term in question.
     */
    {
	int i;
	indexentry *entry;
	htmlsect *lastsect;
	word *w;

	/*
	 * Set up the htmlindex structures.
	 */

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    htmlindex *hi = snew(htmlindex);

	    hi->nrefs = hi->refsize = 0;
	    hi->refs = NULL;

	    entry->backend_data = hi;
	}

	/*
	 * Run over the document inventing fragments. Each fragment
	 * is of the form `i' followed by an integer.
	 */
	lastsect = NULL;
	for (p = sourceform; p; p = p->next) {
	    if (is_heading_type(p->type))
		lastsect = (htmlsect *)p->private_data;

	    for (w = p->words; w; w = w->next)
		if (w->type == word_IndexRef) {
		    htmlindexref *hr = snew(htmlindexref);
		    indextag *tag;
		    int i;

		    hr->section = lastsect;
		    {
			char buf[40];
			sprintf(buf, "i%d",
				lastsect->file->last_fragment_number++);
			hr->fragment = dupstr(buf);
			hr->fragment =
			    html_sanitise_fragment(&files, hr->section->file,
						   hr->fragment);
		    }
		    w->private_data = hr;

		    tag = index_findtag(idx, w->text);
		    if (!tag)
			break;

		    for (i = 0; i < tag->nrefs; i++) {
			indexentry *entry = tag->refs[i];
			htmlindex *hi = (htmlindex *)entry->backend_data;

			if (hi->nrefs >= hi->refsize) {
			    hi->refsize += 32;
			    hi->refs = sresize(hi->refs, hi->refsize, word *);
			}

			hi->refs[hi->nrefs++] = w;
		    }
		}
	}
    }

    /*
     * Now we're ready to write out the actual HTML files.
     * 
     * For each file:
     * 
     *  - we open that file and write its header
     *  - we run down the list of sections
     * 	- for each section directly contained within that file, we
     * 	  output the section text
     * 	- for each section which is not in the file but which has a
     * 	  parent that is, we output a contents entry for the
     * 	  section if appropriate
     *  - finally, we output the file trailer and close the file.
     */
    {
	htmlfile *f, *prevf;
	htmlsect *s;
	paragraph *p;

	prevf = NULL;

	for (f = files.head; f; f = f->next) {
	    htmloutput ho;
	    int displaying;
	    enum LISTTYPE { NOLIST, UL, OL, DL };
	    enum ITEMTYPE { NOITEM, LI, DT, DD };
	    struct stackelement {
		struct stackelement *next;
		enum LISTTYPE listtype;
		enum ITEMTYPE itemtype;
	    } *stackhead;

#define listname(lt) ( (lt)==UL ? "ul" : (lt)==OL ? "ol" : "dl" )
#define itemname(lt) ( (lt)==LI ? "li" : (lt)==DT ? "dt" : "dd" )

	    ho.fp = fopen(f->filename, "w");
	    ho.charset = conf.output_charset;
	    ho.cstate = charset_init_state;
	    ho.ver = conf.htmlver;
	    ho.state = HO_NEUTRAL;
	    ho.contents_level = 0;

	    /* <!DOCTYPE>. */
	    switch (conf.htmlver) {
	      case HTML_3_2:
		fprintf(ho.fp, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD "
			"HTML 3.2 Final//EN\">\n");
		break;
	      case HTML_4:
		fprintf(ho.fp, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML"
			" 4.01//EN\"\n\"http://www.w3.org/TR/html4/"
			"strict.dtd\">\n");
		break;
	      case XHTML_1_0_TRANSITIONAL:
		/* FIXME: <?xml?> to specify character encoding.
		 * This breaks HTML backwards compat, so perhaps avoid, or
		 * perhaps only emit when not using the default UTF-8? */
		fprintf(ho.fp, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML"
			" 1.0 Transitional//EN\"\n\"http://www.w3.org/TR/"
			"xhtml1/DTD/xhtml1-transitional.dtd\">\n");
		break;
	      case XHTML_1_0_STRICT:
		/* FIXME: <?xml?> to specify character encoding. */
		fprintf(ho.fp, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML"
			" 1.0 Strict//EN\"\n\"http://www.w3.org/TR/xhtml1/"
			"DTD/xhtml1-strict.dtd\">\n");
		break;
	    }

	    element_open(&ho, "html");
	    if (is_xhtml(conf.htmlver)) {
		element_attr(&ho, "xmlns", "http://www.w3.org/1999/xhtml");
	    }
	    html_nl(&ho);

	    element_open(&ho, "head");
	    html_nl(&ho);

	    element_empty(&ho, "meta");
	    element_attr(&ho, "http-equiv", "content-type");
	    {
		char buf[200];
		sprintf(buf, "text/html; charset=%.150s",
			charset_to_mimeenc(conf.output_charset));
		element_attr(&ho, "content", buf);
	    }
	    html_nl(&ho);

	    if (conf.author) {
		element_empty(&ho, "meta");
		element_attr(&ho, "name", "author");
		element_attr_w(&ho, "content", conf.author);
		html_nl(&ho);
	    }

	    if (conf.description) {
		element_empty(&ho, "meta");
		element_attr(&ho, "name", "description");
		element_attr_w(&ho, "content", conf.description);
		html_nl(&ho);
	    }

	    element_open(&ho, "title");
	    if (f->first && f->first->title) {
		html_words(&ho, f->first->title->words, NOTHING,
			   f, keywords, &conf);

		assert(f->last);
		if (f->last != f->first && f->last->title) {
		    html_text(&ho, L" - ");   /* FIXME: configurable? */
		    html_words(&ho, f->last->title->words, NOTHING,
			       f, keywords, &conf);
		}
	    }
	    element_close(&ho, "title");
	    html_nl(&ho);

	    if (conf.head_end)
		html_raw(&ho, conf.head_end);

	    element_close(&ho, "head");
	    html_nl(&ho);

	    /* FIXME: need to be able to specify replacement for this */
	    if (conf.body_tag)
		html_raw(&ho, conf.body_tag);
	    else
		element_open(&ho, "body");
	    html_nl(&ho);

	    if (conf.body_start)
		html_raw(&ho, conf.body_start);

	    /*
	     * Write out a nav bar. Special case: we don't do this
	     * if there is only one file.
	     */
	    if (files.head != files.tail) {
		element_open(&ho, "p");
		if (conf.nav_attr)
		    html_raw_as_attr(&ho, conf.nav_attr);

		if (prevf) {
		    element_open(&ho, "a");
		    element_attr(&ho, "href", prevf->filename);
		}
		html_text(&ho, L"Previous");/* FIXME: conf? */
		if (prevf)
		    element_close(&ho, "a");

		html_text(&ho, L" | ");     /* FIXME: conf? */

		if (f != files.head) {
		    element_open(&ho, "a");
		    element_attr(&ho, "href", files.head->filename);
		}
		html_text(&ho, L"Contents");/* FIXME: conf? */
		if (f != files.head)
		    element_close(&ho, "a");

		html_text(&ho, L" | ");     /* FIXME: conf? */

		if (f != files.index) {
		    element_open(&ho, "a");
		    element_attr(&ho, "href", files.index->filename);
		}
		html_text(&ho, L"Index");/* FIXME: conf? */
		if (f != files.index)
		    element_close(&ho, "a");

		html_text(&ho, L" | ");     /* FIXME: conf? */

		if (f->next) {
		    element_open(&ho, "a");
		    element_attr(&ho, "href", f->next->filename);
		}
		html_text(&ho, L"Next");    /* FIXME: conf? */
		if (f->next)
		    element_close(&ho, "a");

		element_close(&ho, "p");
		html_nl(&ho);
	    }
	    prevf = f;

	    /*
	     * Write out a prefix TOC for the file.
	     * 
	     * We start by going through the section list and
	     * collecting the sections which need to be added to
	     * the contents. On the way, we also test to see if
	     * this file is a leaf file (defined as one which
	     * contains all descendants of any section it
	     * contains), because this will play a part in our
	     * decision on whether or not to _output_ the TOC.
	     * 
	     * Special case: we absolutely do not do this if we're
	     * in single-file mode.
	     */
	    if (files.head != files.tail) {
		int ntoc = 0, tocsize = 0;
		htmlsect **toc = NULL;
		int leaf = TRUE;

		for (s = sects.head; s; s = s->next) {
		    htmlsect *a, *ac;
		    int depth, adepth;

		    /*
		     * Search up from this section until we find
		     * the highest-level one which belongs in this
		     * file.
		     */
		    depth = adepth = 0;
		    a = NULL;
		    for (ac = s; ac; ac = ac->parent) {
			if (ac->file == f) {
			    a = ac;
			    adepth = depth;
			}
			depth++;
		    }

		    if (s->file != f && a != NULL)
			leaf = FALSE;

		    if (a) {
			if (adepth <= a->contents_depth) {
			    if (ntoc >= tocsize) {
				tocsize += 64;
				toc = sresize(toc, tocsize, htmlsect *);
			    }
			    toc[ntoc++] = s;
			}
		    }
		}

		if (leaf && conf.leaf_contains_contents &&
		    ntoc >= conf.leaf_smallest_contents) {
		    int i;

		    for (i = 0; i < ntoc; i++) {
			htmlsect *s = toc[i];
			int hlevel = (s->type == TOP ? -1 :
				      s->type == INDEX ? 0 :
				      heading_depth(s->title))
			    - f->min_heading_depth + 1;

			assert(hlevel >= 1);
			html_contents_entry(&ho, hlevel, s,
					    f, keywords, &conf);
		    }
		    html_contents_entry(&ho, 0, NULL, f, keywords, &conf);
		}
	    }

	    /*
	     * Now go through the document and output some real
	     * text.
	     */
	    displaying = FALSE;
	    for (s = sects.head; s; s = s->next) {
		if (s->file == f) {
		    /*
		     * This section belongs in this file.
		     * Display it.
		     */
		    displaying = TRUE;
		} else {
		    htmlsect *a, *ac;
		    int depth, adepth;

		    displaying = FALSE;

		    /*
		     * Search up from this section until we find
		     * the highest-level one which belongs in this
		     * file.
		     */
		    depth = adepth = 0;
		    a = NULL;
		    for (ac = s; ac; ac = ac->parent) {
			if (ac->file == f) {
			    a = ac;
			    adepth = depth;
			}
			depth++;
		    }

		    if (a != NULL) {
			/*
			 * This section does not belong in this
			 * file, but an ancestor of it does. Write
			 * out a contents table entry, if the depth
			 * doesn't exceed the maximum contents
			 * depth for the ancestor section.
			 */
			if (adepth <= a->contents_depth) {
			    html_contents_entry(&ho, adepth, s,
						f, keywords, &conf);
			}
		    }
		}

		if (displaying) {
		    int hlevel;
		    char htag[3];

		    html_contents_entry(&ho, 0, NULL, f, keywords, &conf);

		    /*
		     * Display the section heading.
		     */

		    hlevel = (s->type == TOP ? -1 :
			      s->type == INDEX ? 0 :
			      heading_depth(s->title))
			- f->min_heading_depth + 1;
		    assert(hlevel >= 1);
		    /* HTML headings only go up to <h6> */
		    if (hlevel > 6)
			hlevel = 6;
		    htag[0] = 'h';
		    htag[1] = '0' + hlevel;
		    htag[2] = '\0';
		    element_open(&ho, htag);

		    /*
		     * Provide anchor for cross-links to target.
		     * 
		     * FIXME: AIcurrentlyUI, this needs to be done
		     * differently in XHTML because <a name> is
		     * deprecated or obsolete.
		     * 
		     * (Also we'll have to do this separately in
		     * other paragraph types - NumberedList and
		     * BiblioCited.)
		     */
		    element_open(&ho, "a");
		    element_attr(&ho, "name", s->fragment);
		    element_close(&ho, "a");

		    html_section_title(&ho, s, f, keywords, &conf, TRUE);

		    element_close(&ho, htag);

		    /*
		     * Now display the section text.
		     */
		    if (s->text) {
			stackhead = snew(struct stackelement);
			stackhead->next = NULL;
			stackhead->listtype = NOLIST;
			stackhead->itemtype = NOITEM;

			for (p = s->text;; p = p->next) {
			    enum LISTTYPE listtype;
			    struct stackelement *se;

			    /*
			     * Preliminary switch to figure out what
			     * sort of list we expect to be inside at
			     * this stage.
			     *
			     * Since p may still be NULL at this point,
			     * I invent a harmless paragraph type for
			     * it if it is.
			     */
			    switch (p ? p->type : para_Normal) {
			      case para_Rule:
			      case para_Normal:
			      case para_Copyright:
			      case para_BiblioCited:
			      case para_Code:
			      case para_QuotePush:
			      case para_QuotePop:
			      case para_Chapter:
			      case para_Appendix:
			      case para_UnnumberedChapter:
			      case para_Heading:
			      case para_Subsect:
			      case para_LcontPop:
				listtype = NOLIST;
				break;

			      case para_Bullet:
				listtype = UL;
				break;

			      case para_NumberedList:
				listtype = OL;
				break;

			      case para_DescribedThing:
			      case para_Description:
				listtype = DL;
				break;

			      case para_LcontPush:
				se = snew(struct stackelement);
				se->next = stackhead;
				se->listtype = NOLIST;
				se->itemtype = NOITEM;
				stackhead = se;
				continue;

			      default:     /* some totally non-printing para */
				continue;
			    }

			    html_nl(&ho);

			    /*
			     * Terminate the most recent list item, if
			     * any. (We left this until after
			     * processing LcontPush, since in that case
			     * the list item won't want to be
			     * terminated until after the corresponding
			     * LcontPop.)
			     */
			    if (stackhead->itemtype != NOITEM) {
				element_close(&ho, itemname(stackhead->itemtype));
				html_nl(&ho);
			    }
			    stackhead->itemtype = NOITEM;

			    /*
			     * Terminate the current list, if it's not
			     * the one we want to be in.
			     */
			    if (listtype != stackhead->listtype &&
				stackhead->listtype != NOLIST) {
				element_close(&ho, listname(stackhead->listtype));
				html_nl(&ho);
			    }

			    /*
			     * Leave the loop if our time has come.
			     */
			    if (!p || (is_heading_type(p->type) &&
				       p->type != para_Title))
				break;     /* end of section text */

			    /*
			     * Start a fresh list if necessary.
			     */
			    if (listtype != stackhead->listtype &&
				listtype != NOLIST)
				element_open(&ho, listname(listtype));

			    stackhead->listtype = listtype;

			    switch (p->type) {
			      case para_Rule:
				element_empty(&ho, "hr");
				break;
			      case para_Code:
				html_codepara(&ho, p->words);
				break;
			      case para_Normal:
			      case para_Copyright:
				element_open(&ho, "p");
				html_nl(&ho);
				html_words(&ho, p->words, ALL,
					   f, keywords, &conf);
				html_nl(&ho);
				element_close(&ho, "p");
				break;
			      case para_BiblioCited:
				element_open(&ho, "p");
				if (p->private_data) {
				    htmlsect *s = (htmlsect *)p->private_data;
				    element_open(&ho, "a");
				    element_attr(&ho, "name", s->fragment);
				    element_close(&ho, "a");
				}
				html_nl(&ho);
				html_words(&ho, p->kwtext, ALL,
					   f, keywords, &conf);
				html_text(&ho, L" ");
				html_words(&ho, p->words, ALL,
					   f, keywords, &conf);
				html_nl(&ho);
				element_close(&ho, "p");
				break;
			      case para_Bullet:
			      case para_NumberedList:
				element_open(&ho, "li");
				if (p->private_data) {
				    htmlsect *s = (htmlsect *)p->private_data;
				    element_open(&ho, "a");
				    element_attr(&ho, "name", s->fragment);
				    element_close(&ho, "a");
				}
				html_nl(&ho);
				stackhead->itemtype = LI;
				html_words(&ho, p->words, ALL,
					   f, keywords, &conf);
				break;
			      case para_DescribedThing:
				element_open(&ho, "dt");
				html_nl(&ho);
				stackhead->itemtype = DT;
				html_words(&ho, p->words, ALL,
					   f, keywords, &conf);
				break;
			      case para_Description:
				element_open(&ho, "dd");
				html_nl(&ho);
				stackhead->itemtype = DD;
				html_words(&ho, p->words, ALL,
					   f, keywords, &conf);
				break;

			      case para_QuotePush:
				element_open(&ho, "blockquote");
				break;
			      case para_QuotePop:
				element_close(&ho, "blockquote");
				break;

			      case para_LcontPop:
				se = stackhead;
				stackhead = stackhead->next;
				assert(stackhead);
				sfree(se);
				break;
			    }
			}

			assert(stackhead && !stackhead->next);
			sfree(stackhead);
		    }
		    
		    if (s->type == INDEX) {
			indexentry *entry;
			int i;

			/*
			 * This section is the index. I'll just
			 * render it as a single paragraph, with a
			 * colon between the index term and the
			 * references, and <br> in between each
			 * entry.
			 */
			element_open(&ho, "p");

			for (i = 0; (entry =
				     index234(idx->entries, i)) != NULL; i++) {
			    htmlindex *hi = (htmlindex *)entry->backend_data;
			    int j;

			    if (i > 0)
				element_empty(&ho, "br");
			    html_nl(&ho);

			    html_words(&ho, entry->text, MARKUP|LINKS,
				       f, keywords, &conf);

			    html_text(&ho, L": ");/* FIXME: configurable */

			    for (j = 0; j < hi->nrefs; j++) {
				htmlindexref *hr =
				    (htmlindexref *)hi->refs[j]->private_data;
				paragraph *p = hr->section->title;

				if (j > 0)
				    html_text(&ho, L", "); /* FIXME: conf */

				html_href(&ho, f, hr->section->file,
					  hr->fragment);
				if (p && p->kwtext)
				    html_words(&ho, p->kwtext, MARKUP|LINKS,
					       f, keywords, &conf);
				else if (p && p->words)
				    html_words(&ho, p->words, MARKUP|LINKS,
					       f, keywords, &conf);
				else
				    html_text(&ho, L"FIXME");
				element_close(&ho, "a");
			    }
			}
			element_close(&ho, "p");
		    }
		}
	    }

	    html_contents_entry(&ho, 0, NULL, f, keywords, &conf);
	    html_nl(&ho);

	    {
		/*
		 * Footer.
		 */
		int done_version_ids = FALSE;

		element_empty(&ho, "hr");

		if (conf.body_end)
		    html_raw(&ho, conf.body_end);

		if (conf.address_section) {
		    element_open(&ho, "address");
		    if (conf.addr_start) {
			html_raw(&ho, conf.addr_start);
			html_nl(&ho);
		    }
		    if (conf.visible_version_id) {
			int started = FALSE;
			for (p = sourceform; p; p = p->next)
			    if (p->type == para_VersionID) {
				if (!started)
				    element_open(&ho, "p");
				else
				    element_empty(&ho, "br");
				html_nl(&ho);
				html_text(&ho, L"[");   /* FIXME: conf? */
				html_words(&ho, p->words, NOTHING,
					   f, keywords, &conf);
				html_text(&ho, L"]");   /* FIXME: conf? */
				started = TRUE;
			    }
			if (started)
			    element_close(&ho, "p");
			done_version_ids = TRUE;
		    }
		    if (conf.addr_end)
			html_raw(&ho, conf.addr_end);
		    element_close(&ho, "address");
		}

		if (!done_version_ids) {
		    /*
		     * If the user didn't want the version IDs
		     * visible, I think we still have a duty to put
		     * them in an HTML comment.
		     */
		    int started = FALSE;
		    for (p = sourceform; p; p = p->next)
			if (p->type == para_VersionID) {
			    if (!started) {
				html_raw(&ho, "<!-- version IDs:\n");
				started = TRUE;
			    }
			    html_words(&ho, p->words, NOTHING,
				       f, keywords, &conf);
			    html_nl(&ho);
			}
		    if (started)
			html_raw(&ho, "-->\n");
		}
	    }

	    element_close(&ho, "body");
	    html_nl(&ho);
	    element_close(&ho, "html");
	    html_nl(&ho);
	    cleanup(&ho);
	}
    }

    /*
     * FIXME: Free all the working data.
     */
}

static void html_file_section(htmlconfig *cfg, htmlfilelist *files,
			      htmlsect *sect, int depth)
{
    htmlfile *file;
    int ldepth;

    /*
     * `depth' is derived from the heading_depth() macro at the top
     * of this file, which counts title as -1, chapter as 0,
     * heading as 1 and subsection as 2. However, the semantics of
     * cfg->leaf_level are defined to count chapter as 1, heading
     * as 2 etc. So first I increment depth :-(
     */
    ldepth = depth + 1;

    if (cfg->leaf_level == 0) {
	/*
	 * leaf_level==0 is a special case, in which everything is
	 * put into a single file.
	 */
	if (!files->single)
	    files->single = html_new_file(files, cfg->single_filename);

	file = files->single;
    } else {
	/*
	 * If the depth of this section is at or above leaf_level,
	 * we invent a fresh file and put this section at its head.
	 * Otherwise, we put it in the same file as its parent
	 * section.
	 */
	if (ldepth > cfg->leaf_level) {
	    /*
	     * We know that sect->parent cannot be NULL. The only
	     * circumstance in which it can be is if sect is at
	     * chapter or appendix level, i.e. ldepth==1; and if
	     * that's the case, then we cannot have entered this
	     * branch unless cfg->leaf_level==0, in which case we
	     * would be in the single-file case above and not here
	     * at all.
	     */
	    assert(sect->parent);

	    file = sect->parent->file;
	} else {
	    if (sect->type == TOP) {
		file = html_new_file(files, cfg->contents_filename);
	    } else if (sect->type == INDEX) {
		file = html_new_file(files, cfg->index_filename);
	    } else {
		char *title;

		assert(ldepth > 0 && sect->title);
		title = html_format(sect->title, cfg->template_filename);
		file = html_new_file(files, title);
		sfree(title);
	    }
	}
    }

    sect->file = file;

    if (file->min_heading_depth > depth) {
	/*
	 * This heading is at a higher level than any heading we
	 * have so far placed in this file; so we set the `first'
	 * pointer.
	 */
	file->min_heading_depth = depth;
	file->first = sect;
    }

    if (file->min_heading_depth == depth)
	file->last = sect;
}

static htmlfile *html_new_file(htmlfilelist *list, char *filename)
{
    htmlfile *ret = snew(htmlfile);

    ret->next = NULL;
    if (list->tail)
	list->tail->next = ret;
    else
	list->head = ret;
    list->tail = ret;

    ret->filename = dupstr(filename);
    ret->last_fragment_number = 0;
    ret->min_heading_depth = INT_MAX;
    ret->first = ret->last = NULL;

    return ret;
}

static htmlsect *html_new_sect(htmlsectlist *list, paragraph *title)
{
    htmlsect *ret = snew(htmlsect);

    ret->next = NULL;
    if (list->tail)
	list->tail->next = ret;
    else
	list->head = ret;
    list->tail = ret;

    ret->title = title;
    ret->file = NULL;
    ret->parent = NULL;
    ret->type = NORMAL;

    return ret;
}

static void html_words(htmloutput *ho, word *words, int flags,
		       htmlfile *file, keywordlist *keywords, htmlconfig *cfg)
{
    word *w;
    char *c;
    int style, type;

    for (w = words; w; w = w->next) switch (w->type) {
      case word_HyperLink:
	if (flags & LINKS) {
	    element_open(ho, "a");
	    c = utoa_dup(w->text, CS_ASCII);
	    element_attr(ho, "href", c);
	    sfree(c);
	}
	break;
      case word_UpperXref:
      case word_LowerXref:
	if (flags & LINKS) {
	    keyword *kwl = kw_lookup(keywords, w->text);
	    paragraph *p = kwl->para;
	    htmlsect *s = (htmlsect *)p->private_data;

	    assert(s);

	    html_href(ho, file, s->file, s->fragment);
	}
	break;
      case word_HyperEnd:
      case word_XrefEnd:
	if (flags & LINKS)
	    element_close(ho, "a");
	break;
      case word_IndexRef:
	if (flags & INDEXENTS) {
	    htmlindexref *hr = (htmlindexref *)w->private_data;
	    element_open(ho, "a");
	    element_attr(ho, "name", hr->fragment);
	    element_close(ho, "a");
	}
	break;
      case word_Normal:
      case word_Emph:
      case word_Code:
      case word_WeakCode:
      case word_WhiteSpace:
      case word_EmphSpace:
      case word_CodeSpace:
      case word_WkCodeSpace:
      case word_Quote:
      case word_EmphQuote:
      case word_CodeQuote:
      case word_WkCodeQuote:
	style = towordstyle(w->type);
	type = removeattr(w->type);
	if (style == word_Emph &&
	    (attraux(w->aux) == attr_First ||
	     attraux(w->aux) == attr_Only) &&
	    (flags & MARKUP))
	    element_open(ho, "em");
	else if ((style == word_Code || style == word_WeakCode) &&
		 (attraux(w->aux) == attr_First ||
		  attraux(w->aux) == attr_Only) &&
		 (flags & MARKUP))
	    element_open(ho, "code");

	if (type == word_WhiteSpace)
	    html_text(ho, L" ");
	else if (type == word_Quote) {
	    if (quoteaux(w->aux) == quote_Open)
		html_text(ho, cfg->lquote);
	    else
		html_text(ho, cfg->rquote);
	} else {
	    if (cvt_ok(ho->charset, w->text) || !w->alt)
		html_text(ho, w->text);
	    else
		html_words(ho, w->alt, flags, file, keywords, cfg);
	}

	if (style == word_Emph &&
	    (attraux(w->aux) == attr_Last ||
	     attraux(w->aux) == attr_Only) &&
	    (flags & MARKUP))
	    element_close(ho, "em");
	else if ((style == word_Code || style == word_WeakCode) &&
		 (attraux(w->aux) == attr_Last ||
		  attraux(w->aux) == attr_Only) &&
		 (flags & MARKUP))
	    element_close(ho, "code");

	break;
    }
}

static void html_codepara(htmloutput *ho, word *words)
{
    element_open(ho, "pre");
    element_open(ho, "code");
    for (; words; words = words->next) if (words->type == word_WeakCode) {
	char *open_tag;
	wchar_t *t, *e;

	t = words->text;
	if (words->next && words->next->type == word_Emph) {
	    e = words->next->text;
	    words = words->next;
	} else
	    e = NULL;

	while (e && *e && *t) {
	    int n;
	    int ec = *e;

	    for (n = 0; t[n] && e[n] && e[n] == ec; n++);

	    open_tag = NULL;
	    if (ec == 'i')
		open_tag = "em";
	    else if (ec == 'b')
		open_tag = "b";
	    if (open_tag)
		element_open(ho, open_tag);

	    html_text_limit(ho, t, n);

	    if (open_tag)
		element_close(ho, open_tag);

	    t += n;
	    e += n;
	}
	html_text(ho, t);
	html_nl(ho);
    }
    element_close(ho, "code");
    element_close(ho, "pre");
}

static void html_charset_cleanup(htmloutput *ho)
{
    char outbuf[256];
    int bytes;

    bytes = charset_from_unicode(NULL, NULL, outbuf, lenof(outbuf),
				 ho->charset, &ho->cstate, NULL);
    if (bytes > 0)
	fwrite(outbuf, 1, bytes, ho->fp);
}

static void return_to_neutral(htmloutput *ho)
{
    if (ho->state == HO_IN_TEXT) {
	html_charset_cleanup(ho);
    } else if (ho->state == HO_IN_EMPTY_TAG && is_xhtml(ho->ver)) {
	fprintf(ho->fp, " />");
    } else if (ho->state == HO_IN_EMPTY_TAG || ho->state == HO_IN_TAG) {
	fprintf(ho->fp, ">");
    }

    ho->state = HO_NEUTRAL;
}

static void element_open(htmloutput *ho, char const *name)
{
    return_to_neutral(ho);
    fprintf(ho->fp, "<%s", name);
    ho->state = HO_IN_TAG;
}

static void element_close(htmloutput *ho, char const *name)
{
    return_to_neutral(ho);
    fprintf(ho->fp, "</%s>", name);
    ho->state = HO_NEUTRAL;
}

static void element_empty(htmloutput *ho, char const *name)
{
    return_to_neutral(ho);
    fprintf(ho->fp, "<%s", name);
    ho->state = HO_IN_EMPTY_TAG;
}

static void html_nl(htmloutput *ho)
{
    return_to_neutral(ho);
    fputc('\n', ho->fp);
}

static void html_raw(htmloutput *ho, char *text)
{
    return_to_neutral(ho);
    fputs(text, ho->fp);
}

static void html_raw_as_attr(htmloutput *ho, char *text)
{
    assert(ho->state == HO_IN_TAG || ho->state == HO_IN_EMPTY_TAG);
    fputc(' ', ho->fp);
    fputs(text, ho->fp);
}

static void element_attr(htmloutput *ho, char const *name, char const *value)
{
    html_charset_cleanup(ho);
    assert(ho->state == HO_IN_TAG || ho->state == HO_IN_EMPTY_TAG);
    fprintf(ho->fp, " %s=\"%s\"", name, value);
}

static void element_attr_w(htmloutput *ho, char const *name,
			   wchar_t const *value)
{
    html_charset_cleanup(ho);
    fprintf(ho->fp, " %s=\"", name);
    html_text_limit_internal(ho, value, 0, TRUE);
    html_charset_cleanup(ho);
    fputc('"', ho->fp);
}

static void html_text(htmloutput *ho, wchar_t const *text)
{
    html_text_limit(ho, text, 0);
}

static void html_text_limit(htmloutput *ho, wchar_t const *text, int maxlen)
{
    return_to_neutral(ho);
    html_text_limit_internal(ho, text, maxlen, FALSE);
}

static void html_text_limit_internal(htmloutput *ho, wchar_t const *text,
				     int maxlen, int quote_quotes)
{
    int textlen = ustrlen(text);
    char outbuf[256];
    int bytes, err;

    if (maxlen > 0 && textlen > maxlen)
	textlen = maxlen;

    while (textlen > 0) {
	/* Scan ahead for characters we really can't display in HTML. */
	int lenbefore, lenafter;
	for (lenbefore = 0; lenbefore < textlen; lenbefore++)
	    if (text[lenbefore] == L'<' ||
		text[lenbefore] == L'>' ||
		text[lenbefore] == L'&' ||
		(text[lenbefore] == L'"' && quote_quotes))
		break;
	lenafter = lenbefore;
	bytes = charset_from_unicode(&text, &lenafter, outbuf, lenof(outbuf),
				     ho->charset, &ho->cstate, &err);
	textlen -= (lenbefore - lenafter);
	if (bytes > 0)
	    fwrite(outbuf, 1, bytes, ho->fp);
	if (err) {
	    /*
	     * We have encountered a character that cannot be
	     * displayed in the selected output charset. Therefore,
	     * we use an HTML numeric entity reference.
	     */
	    assert(textlen > 0);
	    fprintf(ho->fp, "&#%ld;", (long int)*text);
	    text++, textlen--;
	} else if (lenafter == 0 && textlen > 0) {
	    /*
	     * We have encountered a character which is special to
	     * HTML.
	     */
	    if (*text == L'<')
		fprintf(ho->fp, "&lt;");
	    else if (*text == L'>')
		fprintf(ho->fp, "&gt;");
	    else if (*text == L'&')
		fprintf(ho->fp, "&amp;");
	    else if (*text == L'"')
		fprintf(ho->fp, "&quot;");
	    else
		assert(!"Can't happen");
	    text++, textlen--;
	}
    }
}

static void cleanup(htmloutput *ho)
{
    return_to_neutral(ho);
    fclose(ho->fp);
}

static void html_href(htmloutput *ho, htmlfile *thisfile,
		      htmlfile *targetfile, char *targetfrag)
{
    rdstringc rs = { 0, 0, NULL };
    char *url;

    if (targetfile != thisfile)
	rdaddsc(&rs, targetfile->filename);
    if (targetfrag) {
	rdaddc(&rs, '#');
	rdaddsc(&rs, targetfrag);
    }
    url = rs.text;

    element_open(ho, "a");
    element_attr(ho, "href", url);
    sfree(url);
}

static char *html_format(paragraph *p, char *template_string)
{
    char *c, *t;
    word *w;
    wchar_t *ws, wsbuf[2];
    rdstringc rs = { 0, 0, NULL };

    t = template_string;
    while (*t) {
	if (*t == '%' && t[1]) {
	    int fmt;

	    t++;
	    fmt = *t++;

	    if (fmt == '%') {
		rdaddc(&rs, fmt);
		continue;
	    }

	    w = NULL;
	    ws = NULL;

	    if (p->kwtext && fmt == 'n')
		w = p->kwtext;
	    else if (p->kwtext2 && fmt == 'b') {
		/*
		 * HTML fragment names must start with a letter, so
		 * simply `1.2.3' is not adequate. In this case I'm
		 * going to cheat slightly by prepending the first
		 * character of the first word of kwtext, so that
		 * we get `C1' for chapter 1, `S2.3' for section
		 * 2.3 etc.
		 */
		if (p->kwtext && p->kwtext->text[0]) {
		    ws = wsbuf;
		    wsbuf[1] = '\0';
		    wsbuf[0] = p->kwtext->text[0];
		}
		w = p->kwtext2;
	    } else if (p->keyword && *p->keyword && fmt == 'k')
		ws = p->keyword;
	    else
		w = p->words;

	    if (ws) {
		c = utoa_dup(ws, CS_ASCII);
		rdaddsc(&rs,c);
		sfree(c);
	    }

	    while (w) {
		if (removeattr(w->type) == word_Normal) {
		    c = utoa_dup(w->text, CS_ASCII);
		    rdaddsc(&rs,c);
		    sfree(c);
		}
		w = w->next;
	    }
	} else {
	    rdaddc(&rs, *t++);
	}
    }

    return rdtrimc(&rs);
}

static char *html_sanitise_fragment(htmlfilelist *files, htmlfile *file,
				    char *text)
{
    /*
     * The HTML 4 spec's strictest definition of fragment names (<a
     * name> and "id" attributes) says that they `must begin with a
     * letter and may be followed by any number of letters, digits,
     * hyphens, underscores, colons, and periods'.
     * 
     * So here we unceremoniously rip out any characters not
     * conforming to this limitation.
     */
    char *p = text, *q = text;

    while (*p && !((*p>='A' && *p<='Z') || (*p>='a' && *p<='z')))
	p++;
    if ((*q++ = *p++) != '\0') {
	while (*p) {
	    if ((*p>='A' && *p<='Z') ||
		(*p>='a' && *p<='z') ||
		(*p>='0' && *p<='9') ||
		*p=='-' || *p=='_' || *p==':' || *p=='.')
		*q++ = *p;
	    p++;
	}

	*q = '\0';
    }

    /*
     * Now we check for clashes with other fragment names, and
     * adjust this one if necessary by appending a hyphen followed
     * by a number.
     */
    {
	htmlfragment *frag = snew(htmlfragment);
	int len = 0;		       /* >0 indicates we have resized */
	int suffix = 1;

	frag->file = file;
	frag->fragment = text;

	while (add234(files->frags, frag) != frag) {
	    if (!len) {
		len = strlen(text);
		frag->fragment = text = sresize(text, len+20, char);
	    }

	    sprintf(text + len, "-%d", ++suffix);
	}
    }

    return text;
}

static void html_contents_entry(htmloutput *ho, int depth, htmlsect *s,
				htmlfile *thisfile, keywordlist *keywords,
				htmlconfig *cfg)
{
    while (ho->contents_level > depth) {
	element_close(ho, "ul");
	ho->contents_level--;
    }

    while (ho->contents_level < depth) {
	element_open(ho, "ul");
	ho->contents_level++;
    }

    if (!s)
	return;

    element_open(ho, "li");
    html_href(ho, thisfile, s->file, s->fragment);
    html_section_title(ho, s, thisfile, keywords, cfg, FALSE);
    element_close(ho, "a");
    element_close(ho, "li");
}

static void html_section_title(htmloutput *ho, htmlsect *s, htmlfile *thisfile,
			       keywordlist *keywords, htmlconfig *cfg,
			       int real)
{
    if (s->title) {
	sectlevel *sl;
	word *number;
	int depth = heading_depth(s->title);

	if (depth < 0)
	    sl = NULL;
	else if (depth == 0)
	    sl = &cfg->achapter;
	else if (depth <= cfg->nasect)
	    sl = &cfg->asect[depth-1];
	else
	    sl = &cfg->asect[cfg->nasect-1];

	if (!sl)
	    number = NULL;
	else if (sl->just_numbers)
	    number = s->title->kwtext2;
	else
	    number = s->title->kwtext;

	if (number) {
	    html_words(ho, number, MARKUP,
		       thisfile, keywords, cfg);
	    html_text(ho, sl->number_suffix);
	}

	html_words(ho, s->title->words, real ? ALL : MARKUP,
		   thisfile, keywords, cfg);
    } else {
	assert(s->type != NORMAL);
	if (s->type == TOP)
	    html_text(ho, L"Preamble");/* FIXME: configure */
	else if (s->type == INDEX)
	    html_text(ho, L"Index");/* FIXME: configure */
    }
}
