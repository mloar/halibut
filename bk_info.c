/*
 * info backend for Halibut
 * 
 * Possible future work:
 * 
 *  - configurable indentation, bullets, emphasis, quotes etc?
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

static void info_heading(info_data *, word *, word *, int);
static void info_rule(info_data *, int, int);
static void info_para(info_data *, word *, wchar_t *, word *, keywordlist *,
		      int, int, int);
static void info_codepara(info_data *, word *, int, int);
static void info_versionid(info_data *, word *);
static void info_menu_item(info_data *, node *, paragraph *);
static word *info_transform_wordlist(word *, keywordlist *);
static int info_check_index(word *, node *, indexdata *);

static int info_rdaddwc(info_data *, word *, word *, int);

static node *info_node_new(char *name, int charset);
static char *info_node_name(paragraph *p, int charset);

static infoconfig info_configure(paragraph *source) {
    infoconfig ret;

    /*
     * Defaults.
     */
    ret.filename = dupstr("output.info");
    ret.maxfilesize = 64 << 10;
    ret.charset = CS_ASCII;

    for (; source; source = source->next) {
	if (source->type == para_Config) {
	    if (!ustricmp(source->keyword, L"info-filename")) {
		sfree(ret.filename);
		ret.filename = dupstr(adv(source->origkeyword));
	    } else if (!ustricmp(source->keyword, L"info-charset")) {
		char *csname = utoa_dup(uadv(source->keyword), CS_ASCII);
		ret.charset = charset_from_localenc(csname);
		sfree(csname);
	    } else if (!ustricmp(source->keyword, L"info-max-file-size")) {
		ret.maxfilesize = utoi(uadv(source->keyword));
	    }
	}
    }

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

    /*
     * FIXME: possibly configurability?
     */
    int width = 70, listindentbefore = 1, listindentafter = 3;
    int indent_code = 2, index_width = 40;

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

	    nodename = info_node_name(p, conf.charset);
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
    }

    /*
     * Set up the display form of each index entry.
     */
    {
	int i;
	indexentry *entry;

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    info_idx *ii = mknew(info_idx);
	    info_data id = EMPTY_INFO_DATA;

	    id.charset = conf.charset;

	    ii->nnodes = ii->nodesize = 0;
	    ii->nodes = NULL;

	    ii->length = info_rdaddwc(&id, entry->text, NULL, FALSE);

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
	    shortname = *section ? uadv(section) : NULL;
	    longname = *shortname ? uadv(shortname) : NULL;
	    kw = *longname ? uadv(longname) : NULL;

	    if (!*longname) {
		error(err_infodirentry, &p->fpos);
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
		      0, 0, width);

    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID)
	    info_versionid(&intro_text, p->words);

    if (intro_text.output.text[intro_text.output.pos-1] != '\n')
	info_rdaddc(&intro_text, '\n');

    /* Do the title */
    for (p = sourceform; p; p = p->next)
	if (p->type == para_Title)
	    info_heading(&topnode->text, NULL, p->words, width);

    nestindent = listindentbefore + listindentafter;
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
	info_menu_item(&currnode->up->text, currnode, p);

	has_index |= info_check_index(p->words, currnode, idx);
	info_heading(&currnode->text, p->kwtext, p->words, width);
	nesting = 0;
	break;

      case para_Rule:
	info_rule(&currnode->text, nesting, width - nesting);
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
	    bullet.text = L"-";	       /* FIXME: configurability */
	    prefix = &bullet;
	    prefixextra = NULL;
	    indentb = listindentbefore;
	    indenta = listindentafter;
	} else if (p->type == para_NumberedList) {
	    prefix = p->kwtext;
	    prefixextra = L".";	       /* FIXME: configurability */
	    indentb = listindentbefore;
	    indenta = listindentafter;
	} else if (p->type == para_Description) {
	    prefix = NULL;
	    prefixextra = NULL;
	    indentb = listindentbefore;
	    indenta = listindentafter;
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
		  width - nesting - indentb - indenta);
	if (wp) {
	    wp->next = NULL;
	    free_word_list(body);
	}
	break;

      case para_Code:
	info_codepara(&currnode->text, p->words,
		      nesting + indent_code,
		      width - nesting - 2 * indent_code);
	break;
    }

    /*
     * Create an index node if required.
     */
    if (has_index) {
	node *newnode;
	int i, j, k;
	indexentry *entry;

	newnode = info_node_new("Index", conf.charset);
	newnode->up = topnode;

	currnode->next = newnode;
	newnode->prev = currnode;
	currnode->listnext = newnode;

	info_rdaddsc(&newnode->text, "Index\n-----\n\n");

	info_menu_item(&topnode->text, newnode, NULL);

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
		for (k = (j ? 0 : ii->length); k < index_width; k++)
		    info_rdaddc(&newnode->text, ' ');
		info_rdaddsc(&newnode->text, "   *Note ");
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
		fname = mknewa(char, strlen(conf.filename) + 40);
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
		    ii->nodes = resize(ii->nodes, ii->nodesize);
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
                    break;
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

static int info_rdaddwc(info_data *id, word *words, word *end, int xrefs) {
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
	    ret += info_rdadd(id, L'_');      /* FIXME: configurability */
	else if (towordstyle(words->type) == word_Code &&
		 (attraux(words->aux) == attr_First ||
		  attraux(words->aux) == attr_Only))
	    ret += info_rdadd(id, L'`');      /* FIXME: configurability */
	if (removeattr(words->type) == word_Normal) {
	    if (cvt_ok(id->charset, words->text) || !words->alt)
		ret += info_rdadds(id, words->text);
	    else
		ret += info_rdaddwc(id, words->alt, NULL, FALSE);
	} else if (removeattr(words->type) == word_WhiteSpace) {
	    ret += info_rdadd(id, L' ');
	} else if (removeattr(words->type) == word_Quote) {
	    ret += info_rdadd(id, quoteaux(words->aux) == quote_Open ? L'`' : L'\'');
				       /* FIXME: configurability */
	}
	if (towordstyle(words->type) == word_Emph &&
	    (attraux(words->aux) == attr_Last ||
	     attraux(words->aux) == attr_Only))
	    ret += info_rdadd(id, L'_');     /* FIXME: configurability */
	else if (towordstyle(words->type) == word_Code &&
		 (attraux(words->aux) == attr_Last ||
		  attraux(words->aux) == attr_Only))
	    ret += info_rdadd(id, L'\'');     /* FIXME: configurability */
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

static int info_width_internal(word *words, int xrefs, int charset);

static int info_width_internal_list(word *words, int xrefs, int charset) {
    int w = 0;
    while (words) {
	w += info_width_internal(words, xrefs, charset);
	words = words->next;
    }
    return w;
}

static int info_width_internal(word *words, int xrefs, int charset) {
    switch (words->type) {
      case word_HyperLink:
      case word_HyperEnd:
      case word_XrefEnd:
      case word_IndexRef:
	return 0;

      case word_Normal:
      case word_Emph:
      case word_Code:
      case word_WeakCode:
	return (((words->type == word_Emph ||
		  words->type == word_Code)
		 ? (attraux(words->aux) == attr_Only ? 2 :
		    attraux(words->aux) == attr_Always ? 0 : 1)
		 : 0) +
		(cvt_ok(charset, words->text) || !words->alt ?
		 ustrwid(words->text, charset) :
		 info_width_internal_list(words->alt, xrefs, charset)));

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
	return (((towordstyle(words->type) == word_Emph ||
		  towordstyle(words->type) == word_Code)
		 ? (attraux(words->aux) == attr_Only ? 2 :
		    attraux(words->aux) == attr_Always ? 0 : 1)
		 : 0) + 1);

      case word_UpperXref:
      case word_LowerXref:
	if (xrefs && words->private_data) {
	    /* "*Note " plus "::" comes to 8 characters */
	    return 8 + strwid(((node *)words->private_data)->name, charset);
	}
	break;
    }
    return 0;			       /* should never happen */
}

static int info_width_noxrefs(void *ctx, word *words)
{
    return info_width_internal(words, FALSE, *(int *)ctx);
}
static int info_width_xrefs(void *ctx, word *words)
{
    return info_width_internal(words, TRUE, *(int *)ctx);
}

static void info_heading(info_data *text, word *tprefix,
			 word *words, int width) {
    int length;
    int firstlinewidth, wrapwidth;
    wrappedline *wrapping, *p;

    length = 0;
    if (tprefix) {
	length += info_rdaddwc(text, tprefix, NULL, FALSE);
	length += info_rdadds(text, L": ");/* FIXME: configurability */
    }

    wrapwidth = width;
    firstlinewidth = width - length;

    wrapping = wrap_para(words, firstlinewidth, wrapwidth,
			 info_width_noxrefs, &text->charset, 0);
    for (p = wrapping; p; p = p->next) {
	length += info_rdaddwc(text, p->begin, p->end, FALSE);
	info_rdadd(text, L'\n');
	while (length--)
	    info_rdadd(text, L'-');  /* FIXME: configurability */
	info_rdadd(text, L'\n');
	length = 0;
    }
    wrap_free(wrapping);
    info_rdadd(text, L'\n');
}

static void info_rule(info_data *text, int indent, int width) {
    while (indent--) info_rdadd(text, L' ');
    while (width--) info_rdadd(text, L'-');
    info_rdadd(text, L'\n');
    info_rdadd(text, L'\n');
}

static void info_para(info_data *text, word *prefix, wchar_t *prefixextra,
		      word *input, keywordlist *keywords,
		      int indent, int extraindent, int width) {
    wrappedline *wrapping, *p;
    word *words;
    int e;
    int i;
    int firstlinewidth = width;

    words = info_transform_wordlist(input, keywords);

    if (prefix) {
	for (i = 0; i < indent; i++)
	    info_rdadd(text, L' ');
	e = info_rdaddwc(text, prefix, NULL, FALSE);
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
			 &text->charset, 0);
    for (p = wrapping; p; p = p->next) {
	for (i = 0; i < e; i++)
	    info_rdadd(text, L' ');
	info_rdaddwc(text, p->begin, p->end, TRUE);
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

static void info_versionid(info_data *text, word *words) {
    info_rdadd(text, L'[');		       /* FIXME: configurability */
    info_rdaddwc(text, words, NULL, FALSE);
    info_rdadds(text, L"]\n");
}

static node *info_node_new(char *name, int charset)
{
    node *n;

    n = mknew(node);
    n->text = empty_info_data;
    n->text.charset = charset;
    n->up = n->next = n->prev = n->lastchild = n->listnext = NULL;
    n->name = dupstr(name);
    n->started_menu = FALSE;

    return n;
}

static char *info_node_name(paragraph *par, int charset)
{
    info_data id = EMPTY_INFO_DATA;
    char *p, *q;

    id.charset = charset;
    info_rdaddwc(&id, par->kwtext ? par->kwtext : par->words, NULL, FALSE);
    info_rdaddsc(&id, NULL);

    /*
     * We cannot have commas or colons in a node name. Remove any
     * that we find, with a warning.
     */
    p = q = id.output.text;
    while (*p) {
	if (*p == ':' || *p == ',') {
	    error(err_infonodechar, &par->fpos, *p);
	} else {
	    *q++ = *p;
	}
	p++;
    }
    *p = '\0';

    return id.output.text;
}

static void info_menu_item(info_data *text, node *n, paragraph *p)
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
	info_rdaddwc(text, p->words, NULL, FALSE);
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
	return strlen(cs);
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
