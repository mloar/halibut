/*
 * info backend for Halibut
 * 
 * Possible future work:
 * 
 *  - configurable choice of how to allocate node names?
 *     + possibly a template-like approach, choosing node names to
 * 	 be the full section title or perhaps the internal keyword?
 *     + neither of those seems quite right. Perhaps instead a
 * 	 Windows Help-like mechanism, where a magic config
 * 	 directive allows user choice of name for every node.
 *     + Only trouble with that is, now what happens to the section
 * 	 numbers? Do they become completely vestigial and just sit
 * 	 in the title text of each node? Or do we keep them in the
 * 	 menus somehow? I think people might occasionally want to
 * 	 go to a section by number, if only because all the _other_
 * 	 formats of the same document will reference the numbers
 * 	 all the time. So our menu lines could look like one of
 * 	 these:
 *        * Nodename: Section 1.2. Title of section.
 *        * Section 1.2: Nodename. Title of section.
 * 
 *  - might be helpful to diagnose duplicate node names!
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "halibut.h"

typedef struct {
    char *filename;
    int maxfilesize;
    int charset;
    int listindentbefore, listindentafter;
    int indent_code, width, index_width;
    wchar_t *bullet, *listsuffix;
    wchar_t *startemph, *endemph;
    wchar_t *lquote, *rquote;
    wchar_t *sectsuffix, *underline;
    wchar_t *rule;
    wchar_t *index_text;
} infoconfig;

typedef struct {
    rdstringc output;
    int charset;
    charset_state state;
    int wcmode;
} info_data;
#define EMPTY_INFO_DATA { { 0, 0, NULL }, 0, CHARSET_INIT_STATE, FALSE }
static const info_data empty_info_data = EMPTY_INFO_DATA;

typedef struct node_tag node;
struct node_tag {
    node *listnext;
    node *up, *prev, *next, *lastchild;
    int pos, started_menu, filenum;
    char *name;
    info_data text;
};

typedef struct {
    char *text;
    int length;
    int nnodes, nodesize;
    node **nodes;
} info_idx;

static int info_rdadd(info_data *, wchar_t);
static int info_rdadds(info_data *, wchar_t const *);
static int info_rdaddc(info_data *, char);
static int info_rdaddsc(info_data *, char const *);

static void info_heading(info_data *, word *, word *, int, infoconfig *);
static void info_rule(info_data *, int, int, infoconfig *);
static void info_para(info_data *, word *, wchar_t *, word *, keywordlist *,
		      int, int, int, infoconfig *);
static void info_codepara(info_data *, word *, int, int);
static void info_versionid(info_data *, word *, infoconfig *);
static void info_menu_item(info_data *, node *, paragraph *, infoconfig *);
static word *info_transform_wordlist(word *, keywordlist *);
static int info_check_index(word *, node *, indexdata *);

static int info_rdaddwc(info_data *, word *, word *, int, infoconfig *);

static node *info_node_new(char *name, int charset);
static char *info_node_name_for_para(paragraph *p, infoconfig *);
static char *info_node_name_for_text(wchar_t *text, infoconfig *);

static infoconfig info_configure(paragraph *source) {
    infoconfig ret;
    paragraph *p;

    /*
     * Defaults.
     */
    ret.filename = dupstr("output.info");
    ret.maxfilesize = 64 << 10;
    ret.charset = CS_ASCII;
    ret.width = 70;
    ret.listindentbefore = 1;
    ret.listindentafter = 3;
    ret.indent_code = 2;
    ret.index_width = 40;
    ret.listsuffix = L".";
    ret.bullet = L"\x2022\0-\0\0";
    ret.rule = L"\x2500\0-\0\0";
    ret.startemph = L"_\0_\0\0";
    ret.endemph = uadv(ret.startemph);
    ret.lquote = L"\x2018\0\x2019\0`\0'\0\0";
    ret.rquote = uadv(ret.lquote);
    ret.sectsuffix = L": ";
    ret.underline = L"\x203E\0-\0\0";
    ret.index_text = L"Index";

    /*
     * Two-pass configuration so that we can pick up global config
     * (e.g. `quotes') before having it overridden by specific
     * config (`info-quotes'), irrespective of the order in which
     * they occur.
     */
    for (p = source; p; p = p->next) {
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    } else if (!ustricmp(p->keyword, L"index")) {
		ret.index_text = uadv(p->keyword);
	    }
	}
    }

    for (p = source; p; p = p->next) {
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"info-filename")) {
		sfree(ret.filename);
		ret.filename = dupstr(adv(p->origkeyword));
	    } else if (!ustricmp(p->keyword, L"info-charset")) {
		ret.charset = charset_from_ustr(&p->fpos, uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-max-file-size")) {
		ret.maxfilesize = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-width")) {
		ret.width = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-indent-code")) {
		ret.indent_code = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-index-width")) {
		ret.index_width = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-list-indent")) {
		ret.listindentbefore = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-listitem-indent")) {
		ret.listindentafter = utoi(uadv(p->keyword));
	    } else if (!ustricmp(p->keyword, L"info-section-suffix")) {
		ret.sectsuffix = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"info-underline")) {
		ret.underline = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"info-bullet")) {
		ret.bullet = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"info-rule")) {
		ret.rule = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"info-list-suffix")) {
		ret.listsuffix = uadv(p->keyword);
	    } else if (!ustricmp(p->keyword, L"info-emphasis")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.startemph = uadv(p->keyword);
		    ret.endemph = uadv(ret.startemph);
		}
	    } else if (!ustricmp(p->keyword, L"info-quotes")) {
		if (*uadv(p->keyword) && *uadv(uadv(p->keyword))) {
		    ret.lquote = uadv(p->keyword);
		    ret.rquote = uadv(ret.lquote);
		}
	    }
	}
    }

    /*
     * Now process fallbacks on quote characters, underlines, the
     * rule character, the emphasis characters, and bullets.
     */
    while (*uadv(ret.rquote) && *uadv(uadv(ret.rquote)) &&
	   (!cvt_ok(ret.charset, ret.lquote) ||
	    !cvt_ok(ret.charset, ret.rquote))) {
	ret.lquote = uadv(ret.rquote);
	ret.rquote = uadv(ret.lquote);
    }

    while (*uadv(ret.endemph) && *uadv(uadv(ret.endemph)) &&
	   (!cvt_ok(ret.charset, ret.startemph) ||
	    !cvt_ok(ret.charset, ret.endemph))) {
	ret.startemph = uadv(ret.endemph);
	ret.endemph = uadv(ret.startemph);
    }

    while (*ret.underline && *uadv(ret.underline) &&
	   !cvt_ok(ret.charset, ret.underline))
	ret.underline = uadv(ret.underline);

    while (*ret.bullet && *uadv(ret.bullet) &&
	   !cvt_ok(ret.charset, ret.bullet))
	ret.bullet = uadv(ret.bullet);

    while (*ret.rule && *uadv(ret.rule) &&
	   !cvt_ok(ret.charset, ret.rule))
	ret.rule = uadv(ret.rule);

    return ret;
}

paragraph *info_config_filename(char *filename)
{
    return cmdline_cfg_simple("info-filename", filename, NULL);
}

void info_backend(paragraph *sourceform, keywordlist *keywords,
		  indexdata *idx, void *unused) {
    paragraph *p;
    infoconfig conf;
    word *prefix, *body, *wp;
    word spaceword;
    wchar_t *prefixextra;
    int nesting, nestindent;
    int indentb, indenta;
    int filepos;
    int has_index;
    info_data intro_text = EMPTY_INFO_DATA;
    node *topnode, *currnode;
    word bullet;
    FILE *fp;

    IGNORE(unused);

    conf = info_configure(sourceform);

    /*
     * Go through and create a node for each section.
     */
    topnode = info_node_new("Top", conf.charset);
    currnode = topnode;
    for (p = sourceform; p; p = p->next) switch (p->type) {
	/*
	 * Chapter titles.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
      case para_Heading:
      case para_Subsect:
	{
	    node *newnode, *upnode;
	    char *nodename;

	    nodename = info_node_name_for_para(p, &conf);
	    newnode = info_node_new(nodename, conf.charset);
	    sfree(nodename);

	    p->private_data = newnode;

	    if (p->parent)
		upnode = (node *)p->parent->private_data;
	    else
		upnode = topnode;
	    assert(upnode);
	    newnode->up = upnode;

	    currnode->next = newnode;
	    newnode->prev = currnode;

	    currnode->listnext = newnode;
	    currnode = newnode;
	}
	break;
      default:
        p->private_data = NULL;
        break;
    }

    /*
     * Set up the display form of each index entry.
     */
    {
	int i;
	indexentry *entry;

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    info_idx *ii = snew(info_idx);
	    info_data id = EMPTY_INFO_DATA;

	    id.charset = conf.charset;

	    ii->nnodes = ii->nodesize = 0;
	    ii->nodes = NULL;

	    ii->length = info_rdaddwc(&id, entry->text, NULL, FALSE, &conf);

	    ii->text = id.output.text;

	    entry->backend_data = ii;
	}
    }

    /*
     * An Info file begins with a piece of introductory text which
     * is apparently never shown anywhere. This seems to me to be a
     * good place to put the copyright notice and the version IDs. 
     * Also, Info directory entries are expected to go here.
     */
    intro_text.charset = conf.charset;

    info_rdaddsc(&intro_text,
	    "This Info file generated by Halibut, ");
    info_rdaddsc(&intro_text, version);
    info_rdaddsc(&intro_text, "\n\n");

    for (p = sourceform; p; p = p->next)
	if (p->type == para_Config &&
	    !ustricmp(p->keyword, L"info-dir-entry")) {
	    wchar_t *section, *shortname, *longname, *kw;
	    char *s;

	    section = uadv(p->keyword);
	    shortname = *section ? uadv(section) : L"";
	    longname = *shortname ? uadv(shortname) : L"";
	    kw = *longname ? uadv(longname) : L"";

	    if (!*longname) {
		error(err_cfginsufarg, &p->fpos, p->origkeyword, 3);
		continue;
	    }

	    info_rdaddsc(&intro_text, "INFO-DIR-SECTION ");
	    info_rdadds(&intro_text, section);
	    info_rdaddsc(&intro_text, "\nSTART-INFO-DIR-ENTRY\n* ");
	    info_rdadds(&intro_text, shortname);
	    info_rdaddsc(&intro_text, ": (");
	    s = dupstr(conf.filename);
	    if (strlen(s) > 5 && !strcmp(s+strlen(s)-5, ".info"))
		s[strlen(s)-5] = '\0';
	    info_rdaddsc(&intro_text, s);
	    sfree(s);
	    info_rdaddsc(&intro_text, ")");
	    if (*kw) {
		keyword *kwl = kw_lookup(keywords, kw);
		if (kwl && kwl->para->private_data) {
		    node *n = (node *)kwl->para->private_data;
		    info_rdaddsc(&intro_text, n->name);
		}
	    }
	    info_rdaddsc(&intro_text, ".   ");
	    info_rdadds(&intro_text, longname);
	    info_rdaddsc(&intro_text, "\nEND-INFO-DIR-ENTRY\n\n");
	}

    for (p = sourceform; p; p = p->next)
	if (p->type == para_Copyright)
	    info_para(&intro_text, NULL, NULL, p->words, keywords,
		      0, 0, conf.width, &conf);

    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID)
	    info_versionid(&intro_text, p->words, &conf);

    if (intro_text.output.text[intro_text.output.pos-1] != '\n')
	info_rdaddc(&intro_text, '\n');

    /* Do the title */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Title)
	    info_heading(&topnode->text, NULL, p->words, conf.width, &conf);

    nestindent = conf.listindentbefore + conf.listindentafter;
    nesting = 0;

    currnode = topnode;

    /* Do the main document */
    for (p = sourceform; p; p = p->next) switch (p->type) {

      case para_QuotePush:
	nesting += 2;
	break;
      case para_QuotePop:
	nesting -= 2;
	assert(nesting >= 0);
	break;

      case para_LcontPush:
	nesting += nestindent;
	break;
      case para_LcontPop:
	nesting -= nestindent;
	assert(nesting >= 0);
	break;

	/*
	 * Things we ignore because we've already processed them or
	 * aren't going to touch them in this pass.
	 */
      case para_IM:
      case para_BR:
      case para_Biblio:		       /* only touch BiblioCited */
      case para_VersionID:
      case para_NoCite:
      case para_Title:
	break;

	/*
	 * Chapter titles.
	 */
      case para_Chapter:
      case para_Appendix:
      case para_UnnumberedChapter:
      case para_Heading:
      case para_Subsect:
	currnode = p->private_data;
	assert(currnode);
	assert(currnode->up);

	if (!currnode->up->started_menu) {
	    info_rdaddsc(&currnode->up->text, "* Menu:\n\n");
	    currnode->up->started_menu = TRUE;
	}
	info_menu_item(&currnode->up->text, currnode, p, &conf);

	has_index |= info_check_index(p->words, currnode, idx);
	info_heading(&currnode->text, p->kwtext, p->words, conf.width, &conf);
	nesting = 0;
	break;

      case para_Rule:
	info_rule(&currnode->text, nesting, conf.width - nesting, &conf);
	break;

      case para_Normal:
      case para_Copyright:
      case para_DescribedThing:
      case para_Description:
      case para_BiblioCited:
      case para_Bullet:
      case para_NumberedList:
	has_index |= info_check_index(p->words, currnode, idx);
	if (p->type == para_Bullet) {
	    bullet.next = NULL;
	    bullet.alt = NULL;
	    bullet.type = word_Normal;
	    bullet.text = conf.bullet;
	    prefix = &bullet;
	    prefixextra = NULL;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else if (p->type == para_NumberedList) {
	    prefix = p->kwtext;
	    prefixextra = conf.listsuffix;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else if (p->type == para_Description) {
	    prefix = NULL;
	    prefixextra = NULL;
	    indentb = conf.listindentbefore;
	    indenta = conf.listindentafter;
	} else {
	    prefix = NULL;
	    prefixextra = NULL;
	    indentb = indenta = 0;
	}
	if (p->type == para_BiblioCited) {
	    body = dup_word_list(p->kwtext);
	    for (wp = body; wp->next; wp = wp->next);
	    wp->next = &spaceword;
	    spaceword.next = p->words;
	    spaceword.alt = NULL;
	    spaceword.type = word_WhiteSpace;
	    spaceword.text = NULL;
	} else {
	    wp = NULL;
	    body = p->words;
	}
	info_para(&currnode->text, prefix, prefixextra, body, keywords,
		  nesting + indentb, indenta,
		  conf.width - nesting - indentb - indenta, &conf);
	if (wp) {
	    wp->next = NULL;
	    free_word_list(body);
	}
	break;

      case para_Code:
	info_codepara(&currnode->text, p->words,
		      nesting + conf.indent_code,
		      conf.width - nesting - 2 * conf.indent_code);
	break;
    }

    /*
     * Create an index node if required.
     */
    if (has_index) {
	node *newnode;
	int i, j, k;
	indexentry *entry;
	char *nodename;

	nodename = info_node_name_for_text(conf.index_text, &conf);
	newnode = info_node_new(nodename, conf.charset);
	sfree(nodename);

	newnode->up = topnode;

	currnode->next = newnode;
	newnode->prev = currnode;
	currnode->listnext = newnode;

	k = info_rdadds(&newnode->text, conf.index_text);
	info_rdaddsc(&newnode->text, "\n");
	while (k > 0) {
	    info_rdadds(&newnode->text, conf.underline);
	    k -= ustrwid(conf.underline, conf.charset);
	}
	info_rdaddsc(&newnode->text, "\n\n");

	info_menu_item(&topnode->text, newnode, NULL, &conf);

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    info_idx *ii = (info_idx *)entry->backend_data;

	    for (j = 0; j < ii->nnodes; j++) {
		/*
		 * When we have multiple references for a single
		 * index term, we only display the actual term on
		 * the first line, to make it clear that the terms
		 * really are the same.
		 */
		if (j == 0)
		    info_rdaddsc(&newnode->text, ii->text);
		for (k = (j ? 0 : ii->length); k < conf.index_width-2; k++)
		    info_rdaddc(&newnode->text, ' ');
		info_rdaddsc(&newnode->text, "  *Note ");
		info_rdaddsc(&newnode->text, ii->nodes[j]->name);
		info_rdaddsc(&newnode->text, "::\n");
	    }
	}
    }

    /*
     * Finalise the text of each node, by adding the ^_ delimiter
     * and the node line at the top.
     */
    for (currnode = topnode; currnode; currnode = currnode->listnext) {
	char *origtext = currnode->text.output.text;
	currnode->text = empty_info_data;
	currnode->text.charset = conf.charset;
	info_rdaddsc(&currnode->text, "\037\nFile: ");
	info_rdaddsc(&currnode->text, conf.filename);
	info_rdaddsc(&currnode->text, ",  Node: ");
	info_rdaddsc(&currnode->text, currnode->name);
	if (currnode->prev) {
	    info_rdaddsc(&currnode->text, ",  Prev: ");
	    info_rdaddsc(&currnode->text, currnode->prev->name);
	}
	info_rdaddsc(&currnode->text, ",  Up: ");
	info_rdaddsc(&currnode->text, (currnode->up ?
				       currnode->up->name : "(dir)"));
	if (currnode->next) {
	    info_rdaddsc(&currnode->text, ",  Next: ");
	    info_rdaddsc(&currnode->text, currnode->next->name);
	}
	info_rdaddsc(&currnode->text, "\n\n");
	info_rdaddsc(&currnode->text, origtext);
	/*
	 * Just make _absolutely_ sure we end with a newline.
	 */
	if (currnode->text.output.text[currnode->text.output.pos-1] != '\n')
	    info_rdaddc(&currnode->text, '\n');

	sfree(origtext);
    }    

    /*
     * Compute the offsets for the tag table.
     */
    filepos = intro_text.output.pos;
    for (currnode = topnode; currnode; currnode = currnode->listnext) {
	currnode->pos = filepos;
	filepos += currnode->text.output.pos;
    }

    /*
     * Split into sub-files.
     */
    if (conf.maxfilesize > 0) {
	int currfilesize = intro_text.output.pos, currfilenum = 1;
	for (currnode = topnode; currnode; currnode = currnode->listnext) {
	    if (currfilesize > intro_text.output.pos &&
		currfilesize + currnode->text.output.pos > conf.maxfilesize) {
		currfilenum++;
		currfilesize = intro_text.output.pos;
	    }
	    currnode->filenum = currfilenum;
	    currfilesize += currnode->text.output.pos;
	}
    }

    /*
     * Write the primary output file.
     */
    fp = fopen(conf.filename, "w");
    if (!fp) {
	error(err_cantopenw, conf.filename);
	return;
    }
    fputs(intro_text.output.text, fp);
    if (conf.maxfilesize == 0) {
	for (currnode = topnode; currnode; currnode = currnode->listnext)
	    fputs(currnode->text.output.text, fp);
    } else {
	int filenum = 0;
	fprintf(fp, "\037\nIndirect:\n");
	for (currnode = topnode; currnode; currnode = currnode->listnext)
	    if (filenum != currnode->filenum) {
		filenum = currnode->filenum;
		fprintf(fp, "%s-%d: %d\n", conf.filename, filenum,
			currnode->pos);
	    }
    }
    fprintf(fp, "\037\nTag Table:\n");
    if (conf.maxfilesize > 0)
	fprintf(fp, "(Indirect)\n");
    for (currnode = topnode; currnode; currnode = currnode->listnext)
	fprintf(fp, "Node: %s\177%d\n", currnode->name, currnode->pos);
    fprintf(fp, "\037\nEnd Tag Table\n");
    fclose(fp);

    /*
     * Write the subfiles.
     */
    if (conf.maxfilesize > 0) {
	int filenum = 0;
	fp = NULL;

	for (currnode = topnode; currnode; currnode = currnode->listnext) {
	    if (filenum != currnode->filenum) {
		char *fname;

		filenum = currnode->filenum;

		if (fp)
		    fclose(fp);
		fname = snewn(strlen(conf.filename) + 40, char);
		sprintf(fname, "%s-%d", conf.filename, filenum);
		fp = fopen(fname, "w");
		if (!fp) {
		    error(err_cantopenw, fname);
		    return;
		}
		sfree(fname);
		fputs(intro_text.output.text, fp);
	    }
	    fputs(currnode->text.output.text, fp);
	}

	if (fp)
	    fclose(fp);
    }
}

static int info_check_index(word *w, node *n, indexdata *idx)
{
    int ret = 0;

    for (; w; w = w->next) {
	if (w->type == word_IndexRef) {
	    indextag *tag;
	    int i;

	    tag = index_findtag(idx, w->text);
	    if (!tag)
		break;

	    for (i = 0; i < tag->nrefs; i++) {
		indexentry *entry = tag->refs[i];
		info_idx *ii = (info_idx *)entry->backend_data;

		if (ii->nnodes > 0 && ii->nodes[ii->nnodes-1] == n) {
		    /*
		     * If the same index term is indexed twice
		     * within the same section, we only want to
		     * mention it once in the index. So do nothing
		     * here.
		     */
		    continue;
		}

		if (ii->nnodes >= ii->nodesize) {
		    ii->nodesize += 32;
		    ii->nodes = sresize(ii->nodes, ii->nodesize, node *);
		}

		ii->nodes[ii->nnodes++] = n;

		ret = 1;
	    }
	}
    }

    return ret;
}

static word *info_transform_wordlist(word *words, keywordlist *keywords)
{
    word *ret = dup_word_list(words);
    word *w;
    keyword *kwl;

    for (w = ret; w; w = w->next) {
	w->private_data = NULL;
	if (w->type == word_UpperXref || w->type == word_LowerXref) {
	    kwl = kw_lookup(keywords, w->text);
	    if (kwl) {
		if (kwl->para->type == para_NumberedList ||
		    kwl->para->type == para_BiblioCited) {
		    /*
		     * In Info, we do nothing special for xrefs to
		     * numbered list items or bibliography entries.
		     */
                    continue;
		} else {
		    /*
		     * An xref to a different section has its text
		     * completely replaced.
		     */
		    word *w2, *w3, *w4;
		    w2 = w3 = w->next;
		    w4 = NULL;
		    while (w2) {
			if (w2->type == word_XrefEnd) {
			    w4 = w2->next;
			    w2->next = NULL;
			    break;
			}
			w2 = w2->next;
		    }
		    free_word_list(w3);

		    /*
		     * Now w is the UpperXref / LowerXref we
		     * started with, and w4 is the next word after
		     * the corresponding XrefEnd (if any). The
		     * simplest thing is just to stick a pointer to
		     * the target node structure in the private
		     * data field of the xref word, and let
		     * info_rdaddwc and friends read the node name
		     * out from there.
		     */
		    w->next = w4;
		    w->private_data = kwl->para->private_data;
		    assert(w->private_data);
		}
	    }
	}
    }

    return ret;
}

static int info_rdaddwc(info_data *id, word *words, word *end, int xrefs,
			infoconfig *cfg) {
    int ret = 0;

    for (; words && words != end; words = words->next) switch (words->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_XrefEnd:
      case word_IndexRef:
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
	assert(words->type != word_CodeQuote &&
	       words->type != word_WkCodeQuote);
	if (towordstyle(words->type) == word_Emph &&
	    (attraux(words->aux) == attr_First ||
	     attraux(words->aux) == attr_Only))
	    ret += info_rdadds(id, cfg->startemph);
	else if (towordstyle(words->type) == word_Code &&
		 (attraux(words->aux) == attr_First ||
		  attraux(words->aux) == attr_Only))
	    ret += info_rdadds(id, cfg->lquote);
	if (removeattr(words->type) == word_Normal) {
	    if (cvt_ok(id->charset, words->text) || !words->alt)
		ret += info_rdadds(id, words->text);
	    else
		ret += info_rdaddwc(id, words->alt, NULL, FALSE, cfg);
	} else if (removeattr(words->type) == word_WhiteSpace) {
	    ret += info_rdadd(id, L' ');
	} else if (removeattr(words->type) == word_Quote) {
	    ret += info_rdadds(id, quoteaux(words->aux) == quote_Open ?
			       cfg->lquote : cfg->rquote);
	}
	if (towordstyle(words->type) == word_Emph &&
	    (attraux(words->aux) == attr_Last ||
	     attraux(words->aux) == attr_Only))
	    ret += info_rdadds(id, cfg->endemph);
	else if (towordstyle(words->type) == word_Code &&
		 (attraux(words->aux) == attr_Last ||
		  attraux(words->aux) == attr_Only))
	    ret += info_rdadds(id, cfg->rquote);
	break;

      case word_UpperXref:
      case word_LowerXref:
	if (xrefs && words->private_data) {
	    /*
	     * This bit is structural and so must be done in char
	     * rather than wchar_t.
	     */
	    ret += info_rdaddsc(id, "*Note ");
	    ret += info_rdaddsc(id, ((node *)words->private_data)->name);
	    ret += info_rdaddsc(id, "::");
	}
	break;
    }

    return ret;
}

static int info_width_internal(word *words, int xrefs, infoconfig *cfg);

static int info_width_internal_list(word *words, int xrefs, infoconfig *cfg) {
    int w = 0;
    while (words) {
	w += info_width_internal(words, xrefs, cfg);
	words = words->next;
    }
    return w;
}

static int info_width_internal(word *words, int xrefs, infoconfig *cfg) {
    int wid;
    int attr;

    switch (words->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_XrefEnd:
      case word_IndexRef:
	return 0;

      case word_UpperXref:
      case word_LowerXref:
	if (xrefs && words->private_data) {
	    /* "*Note " plus "::" comes to 8 characters */
	    return 8 + strwid(((node *)words->private_data)->name,
			      cfg->charset);
	} else
	    return 0;
    }

    assert(words->type < word_internal_endattrs);

    wid = 0;
    attr = towordstyle(words->type);

    if (attr == word_Emph || attr == word_Code) {
	if (attraux(words->aux) == attr_Only ||
	    attraux(words->aux) == attr_First)
	    wid += ustrwid(attr == word_Emph ? cfg->startemph : cfg->lquote,
			   cfg->charset);
    }
    if (attr == word_Emph || attr == word_Code) {
	if (attraux(words->aux) == attr_Only ||
	    attraux(words->aux) == attr_Last)
	    wid += ustrwid(attr == word_Emph ? cfg->startemph : cfg->lquote,
			   cfg->charset);
    }

    switch (words->type) {
      case word_Normal:
      case word_Emph:
      case word_Code:
      case word_WeakCode:
	if (cvt_ok(cfg->charset, words->text) || !words->alt)
	    wid += ustrwid(words->text, cfg->charset);
	else
	    wid += info_width_internal_list(words->alt, xrefs, cfg);
	return wid;

      case word_WhiteSpace:
      case word_EmphSpace:
      case word_CodeSpace:
      case word_WkCodeSpace:
      case word_Quote:
      case word_EmphQuote:
      case word_CodeQuote:
      case word_WkCodeQuote:
	assert(words->type != word_CodeQuote &&
	       words->type != word_WkCodeQuote);
	if (removeattr(words->type) == word_Quote) {
	    if (quoteaux(words->aux) == quote_Open)
		wid += ustrwid(cfg->lquote, cfg->charset);
	    else
		wid += ustrwid(cfg->rquote, cfg->charset);
	} else
	    wid++;		       /* space */
    }
    return wid;
}

static int info_width_noxrefs(void *ctx, word *words)
{
    return info_width_internal(words, FALSE, (infoconfig *)ctx);
}
static int info_width_xrefs(void *ctx, word *words)
{
    return info_width_internal(words, TRUE, (infoconfig *)ctx);
}

static void info_heading(info_data *text, word *tprefix,
			 word *words, int width, infoconfig *cfg) {
    int length;
    int firstlinewidth, wrapwidth;
    wrappedline *wrapping, *p;

    length = 0;
    if (tprefix) {
	length += info_rdaddwc(text, tprefix, NULL, FALSE, cfg);
	length += info_rdadds(text, cfg->sectsuffix);
    }

    wrapwidth = width;
    firstlinewidth = width - length;

    wrapping = wrap_para(words, firstlinewidth, wrapwidth,
			 info_width_noxrefs, cfg, 0);
    for (p = wrapping; p; p = p->next) {
	length += info_rdaddwc(text, p->begin, p->end, FALSE, cfg);
	info_rdadd(text, L'\n');
	while (length > 0) {
	    info_rdadds(text, cfg->underline);
	    length -= ustrwid(cfg->underline, cfg->charset);
	}
	info_rdadd(text, L'\n');
	length = 0;
    }
    wrap_free(wrapping);
    info_rdadd(text, L'\n');
}

static void info_rule(info_data *text, int indent, int width, infoconfig *cfg)
{
    while (indent--) info_rdadd(text, L' ');
    while (width > 0) {
	info_rdadds(text, cfg->rule);
	width -= ustrwid(cfg->rule, cfg->charset);
    }
    info_rdadd(text, L'\n');
    info_rdadd(text, L'\n');
}

static void info_para(info_data *text, word *prefix, wchar_t *prefixextra,
		      word *input, keywordlist *keywords, int indent,
		      int extraindent, int width, infoconfig *cfg) {
    wrappedline *wrapping, *p;
    word *words;
    int e;
    int i;
    int firstlinewidth = width;

    words = info_transform_wordlist(input, keywords);

    if (prefix) {
	for (i = 0; i < indent; i++)
	    info_rdadd(text, L' ');
	e = info_rdaddwc(text, prefix, NULL, FALSE, cfg);
	if (prefixextra)
	    e += info_rdadds(text, prefixextra);
	/* If the prefix is too long, shorten the first line to fit. */
	e = extraindent - e;
	if (e < 0) {
	    firstlinewidth += e;       /* this decreases it, since e < 0 */
	    if (firstlinewidth < 0) {
		e = indent + extraindent;
		firstlinewidth = width;
		info_rdadd(text, L'\n');
	    } else
		e = 0;
	}
    } else
	e = indent + extraindent;

    wrapping = wrap_para(words, firstlinewidth, width, info_width_xrefs,
			 cfg, 0);
    for (p = wrapping; p; p = p->next) {
	for (i = 0; i < e; i++)
	    info_rdadd(text, L' ');
	info_rdaddwc(text, p->begin, p->end, TRUE, cfg);
	info_rdadd(text, L'\n');
	e = indent + extraindent;
    }
    wrap_free(wrapping);
    info_rdadd(text, L'\n');

    free_word_list(words);
}

static void info_codepara(info_data *text, word *words,
			  int indent, int width) {
    int i;

    for (; words; words = words->next) if (words->type == word_WeakCode) {
	for (i = 0; i < indent; i++)
	    info_rdadd(text, L' ');
	if (info_rdadds(text, words->text) > width) {
	    /* FIXME: warn */
	}
	info_rdadd(text, L'\n');
    }

    info_rdadd(text, L'\n');
}

static void info_versionid(info_data *text, word *words, infoconfig *cfg) {
    info_rdadd(text, L'[');
    info_rdaddwc(text, words, NULL, FALSE, cfg);
    info_rdadds(text, L"]\n");
}

static node *info_node_new(char *name, int charset)
{
    node *n;

    n = snew(node);
    n->text = empty_info_data;
    n->text.charset = charset;
    n->up = n->next = n->prev = n->lastchild = n->listnext = NULL;
    n->name = dupstr(name);
    n->started_menu = FALSE;

    return n;
}

static char *info_node_name_core(info_data *id, filepos *fpos)
{
    char *p, *q;

    /*
     * We cannot have commas, colons or parentheses in a node name.
     * Remove any that we find, with a warning.
     */
    p = q = id->output.text;
    while (*p) {
	if (*p == ':' || *p == ',' || *p == '(' || *p == ')') {
	    error(err_infonodechar, fpos, *p);
	} else {
	    *q++ = *p;
	}
	p++;
    }
    *q = '\0';

    return id->output.text;
}

static char *info_node_name_for_para(paragraph *par, infoconfig *cfg)
{
    info_data id = EMPTY_INFO_DATA;

    id.charset = cfg->charset;
    info_rdaddwc(&id, par->kwtext ? par->kwtext : par->words,
		 NULL, FALSE, cfg);
    info_rdaddsc(&id, NULL);

    return info_node_name_core(&id, &par->fpos);
}

static char *info_node_name_for_text(wchar_t *text, infoconfig *cfg)
{
    info_data id = EMPTY_INFO_DATA;

    id.charset = cfg->charset;
    info_rdadds(&id, text);
    info_rdaddsc(&id, NULL);

    return info_node_name_core(&id, NULL);
}

static void info_menu_item(info_data *text, node *n, paragraph *p,
			   infoconfig *cfg)
{
    /*
     * FIXME: Depending on how we're doing node names in this info
     * file, we might want to do
     * 
     *   * Node name:: Chapter title
     * 
     * _or_
     * 
     *   * Chapter number: Node name.
     * 
     * This function mostly works in char rather than wchar_t,
     * because a menu item is a structural component.
     */
    info_rdaddsc(text, "* ");
    info_rdaddsc(text, n->name);
    info_rdaddsc(text, "::");
    if (p) {
	info_rdaddc(text, ' ');
	info_rdaddwc(text, p->words, NULL, FALSE, cfg);
    }
    info_rdaddc(text, '\n');
}

/*
 * These functions implement my wrapper on the rdadd* calls which
 * allows me to switch arbitrarily between literal octet-string
 * text and charset-translated Unicode. (Because no matter what
 * character set I write the actual text in, I expect info readers
 * to treat node names and file names literally and to expect
 * keywords like `*Note' in their canonical form, so I have to take
 * steps to ensure that those structural elements of the file
 * aren't messed with.)
 */
static int info_rdadds(info_data *d, wchar_t const *wcs)
{
    if (!d->wcmode) {
	d->state = charset_init_state;
	d->wcmode = TRUE;
    }

    if (wcs) {
	char buf[256];
	int len, width, ret;

	width = ustrwid(wcs, d->charset);

	len = ustrlen(wcs);
	while (len > 0) {
	    int prevlen = len;

	    ret = charset_from_unicode(&wcs, &len, buf, lenof(buf),
				       d->charset, &d->state, NULL);

	    assert(len < prevlen);

	    if (ret > 0) {
		buf[ret] = '\0';
		rdaddsc(&d->output, buf);
	    }
	}

	return width;
    } else
	return 0;
}

static int info_rdaddsc(info_data *d, char const *cs)
{
    if (d->wcmode) {
	char buf[256];
	int ret;

	ret = charset_from_unicode(NULL, 0, buf, lenof(buf),
				   d->charset, &d->state, NULL);
	if (ret > 0) {
	    buf[ret] = '\0';
	    rdaddsc(&d->output, buf);
	}

	d->wcmode = FALSE;
    }

    if (cs) {
	rdaddsc(&d->output, cs);
	return strwid(cs, d->charset);
    } else
	return 0;
}

static int info_rdadd(info_data *d, wchar_t wc)
{
    wchar_t wcs[2];
    wcs[0] = wc;
    wcs[1] = L'\0';
    return info_rdadds(d, wcs);
}

static int info_rdaddc(info_data *d, char c)
{
    char cs[2];
    cs[0] = c;
    cs[1] = '\0';
    return info_rdaddsc(d, cs);
}
