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
} infoconfig;

typedef struct node_tag node;
struct node_tag {
    node *listnext;
    node *up, *prev, *next, *lastchild;
    int pos, started_menu, filenum;
    char *name;
    rdstringc text;
};

typedef struct {
    char *text;
    int nnodes, nodesize;
    node **nodes;
} info_idx;

static int info_convert(wchar_t *, char **);

static void info_heading(rdstringc *, word *, word *, int);
static void info_rule(rdstringc *, int, int);
static void info_para(rdstringc *, word *, char *, word *, keywordlist *,
		      int, int, int);
static void info_codepara(rdstringc *, word *, int, int);
static void info_versionid(rdstringc *, word *);
static void info_menu_item(rdstringc *, node *, paragraph *);
static word *info_transform_wordlist(word *, keywordlist *);
static int info_check_index(word *, node *, indexdata *);

static void info_rdaddwc(rdstringc *, word *, word *, int);

static node *info_node_new(char *name);
static char *info_node_name(paragraph *p);

static infoconfig info_configure(paragraph *source) {
    infoconfig ret;

    /*
     * Defaults.
     */
    ret.filename = dupstr("output.info");
    ret.maxfilesize = 64 << 10;

    for (; source; source = source->next) {
	if (source->type == para_Config) {
	    if (!ustricmp(source->keyword, L"info-filename")) {
		sfree(ret.filename);
		ret.filename = utoa_dup(uadv(source->keyword));
	    } else if (!ustricmp(source->keyword, L"info-max-file-size")) {
		ret.maxfilesize = utoi(uadv(source->keyword));
	    }
	}
    }

    return ret;
}

paragraph *info_config_filename(char *filename)
{
    paragraph *p;
    wchar_t *ufilename, *up;
    int len;

    p = mknew(paragraph);
    memset(p, 0, sizeof(*p));
    p->type = para_Config;
    p->next = NULL;
    p->fpos.filename = "<command line>";
    p->fpos.line = p->fpos.col = -1;

    ufilename = ufroma_dup(filename);
    len = ustrlen(ufilename) + 2 + lenof(L"info-filename");
    p->keyword = mknewa(wchar_t, len);
    up = p->keyword;
    ustrcpy(up, L"info-filename");
    up = uadv(up);
    ustrcpy(up, ufilename);
    up = uadv(up);
    *up = L'\0';
    assert(up - p->keyword < len);
    sfree(ufilename);

    return p;
}

void info_backend(paragraph *sourceform, keywordlist *keywords,
		  indexdata *idx, void *unused) {
    paragraph *p;
    infoconfig conf;
    word *prefix, *body, *wp;
    word spaceword;
    char *prefixextra;
    int nesting, nestindent;
    int indentb, indenta;
    int filepos;
    int has_index;
    rdstringc intro_text = { 0, 0, NULL };
    node *topnode, *currnode;
    word bullet;
    FILE *fp;

    /*
     * FIXME
     */
    int width = 70, listindentbefore = 1, listindentafter = 3;
    int indent_code = 2, index_width = 40;

    IGNORE(unused);

    conf = info_configure(sourceform);

    /*
     * Go through and create a node for each section.
     */
    topnode = info_node_new("Top");
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

	    nodename = info_node_name(p);
	    newnode = info_node_new(nodename);
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
	    rdstringc rs = { 0, 0, NULL };

	    ii->nnodes = ii->nodesize = 0;
	    ii->nodes = NULL;

	    info_rdaddwc(&rs, entry->text, NULL, FALSE);

	    ii->text = rs.text;

	    entry->backend_data = ii;
	}
    }

    /*
     * An Info file begins with a piece of introductory text which
     * is apparently never shown anywhere. This seems to me to be a
     * good place to put the copyright notice and the version IDs. 
     * Also, Info directory entries are expected to go here.
     */

    rdaddsc(&intro_text,
	    "This Info file generated by Halibut, ");
    rdaddsc(&intro_text, version);
    rdaddsc(&intro_text, "\n\n");

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

	    rdaddsc(&intro_text, "INFO-DIR-SECTION ");
	    s = utoa_dup(section);
	    rdaddsc(&intro_text, s);
	    sfree(s);
	    rdaddsc(&intro_text, "\nSTART-INFO-DIR-ENTRY\n* ");
	    s = utoa_dup(shortname);
	    rdaddsc(&intro_text, s);
	    sfree(s);
	    rdaddsc(&intro_text, ": (");
	    s = dupstr(conf.filename);
	    if (strlen(s) > 5 && !strcmp(s+strlen(s)-5, ".info"))
		s[strlen(s)-5] = '\0';
	    rdaddsc(&intro_text, s);
	    sfree(s);
	    rdaddsc(&intro_text, ")");
	    if (*kw) {
		keyword *kwl = kw_lookup(keywords, kw);
		if (kwl && kwl->para->private_data) {
		    node *n = (node *)kwl->para->private_data;
		    rdaddsc(&intro_text, n->name);
		}
	    }
	    rdaddsc(&intro_text, ".   ");
	    s = utoa_dup(longname);
	    rdaddsc(&intro_text, s);
	    sfree(s);
	    rdaddsc(&intro_text, "\nEND-INFO-DIR-ENTRY\n\n");
	}

    for (p = sourceform; p; p = p->next)
	if (p->type == para_Copyright)
	    info_para(&intro_text, NULL, NULL, p->words, keywords,
		      0, 0, width);

    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID)
	    info_versionid(&intro_text, p->words);

    if (intro_text.text[intro_text.pos-1] != '\n')
	rdaddc(&intro_text, '\n');

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
	    rdaddsc(&currnode->up->text, "* Menu:\n\n");
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
	    prefixextra = ".";	       /* FIXME: configurability */
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

	newnode = info_node_new("Index");
	newnode->up = topnode;

	currnode->next = newnode;
	newnode->prev = currnode;
	currnode->listnext = newnode;

	rdaddsc(&newnode->text, "Index\n-----\n\n");

	info_menu_item(&topnode->text, newnode, NULL);

	for (i = 0; (entry = index234(idx->entries, i)) != NULL; i++) {
	    info_idx *ii = (info_idx *)entry->backend_data;

	    for (j = 0; j < ii->nnodes; j++) {
		int pos0 = newnode->text.pos;
		/*
		 * When we have multiple references for a single
		 * index term, we only display the actual term on
		 * the first line, to make it clear that the terms
		 * really are the same.
		 */
		if (j == 0)
		    rdaddsc(&newnode->text, ii->text);
		for (k = newnode->text.pos - pos0; k < index_width; k++)
		    rdaddc(&newnode->text, ' ');
		rdaddsc(&newnode->text, "   *Note ");
		rdaddsc(&newnode->text, ii->nodes[j]->name);
		rdaddsc(&newnode->text, "::\n");
	    }
	}
    }

    /*
     * Finalise the text of each node, by adding the ^_ delimiter
     * and the node line at the top.
     */
    for (currnode = topnode; currnode; currnode = currnode->listnext) {
	char *origtext = currnode->text.text;
	currnode->text.text = NULL;
	currnode->text.pos = currnode->text.size = 0;
	rdaddsc(&currnode->text, "\037\nFile: ");
	rdaddsc(&currnode->text, conf.filename);
	rdaddsc(&currnode->text, ",  Node: ");
	rdaddsc(&currnode->text, currnode->name);
	if (currnode->prev) {
	    rdaddsc(&currnode->text, ",  Prev: ");
	    rdaddsc(&currnode->text, currnode->prev->name);
	}
	rdaddsc(&currnode->text, ",  Up: ");
	rdaddsc(&currnode->text, (currnode->up ?
				  currnode->up->name : "(dir)"));
	if (currnode->next) {
	    rdaddsc(&currnode->text, ",  Next: ");
	    rdaddsc(&currnode->text, currnode->next->name);
	}
	rdaddsc(&currnode->text, "\n\n");
	rdaddsc(&currnode->text, origtext);
	/*
	 * Just make _absolutely_ sure we end with a newline.
	 */
	if (currnode->text.text[currnode->text.pos-1] != '\n')
	    rdaddc(&currnode->text, '\n');

	sfree(origtext);
    }    

    /*
     * Compute the offsets for the tag table.
     */
    filepos = intro_text.pos;
    for (currnode = topnode; currnode; currnode = currnode->listnext) {
	currnode->pos = filepos;
	filepos += currnode->text.pos;
    }

    /*
     * Split into sub-files.
     */
    if (conf.maxfilesize > 0) {
	int currfilesize = intro_text.pos, currfilenum = 1;
	for (currnode = topnode; currnode; currnode = currnode->listnext) {
	    if (currfilesize > intro_text.pos &&
		currfilesize + currnode->text.pos > conf.maxfilesize) {
		currfilenum++;
		currfilesize = intro_text.pos;
	    }
	    currnode->filenum = currfilenum;
	    currfilesize += currnode->text.pos;
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
    fputs(intro_text.text, fp);
    if (conf.maxfilesize == 0) {
	for (currnode = topnode; currnode; currnode = currnode->listnext)
	    fputs(currnode->text.text, fp);
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
		fputs(intro_text.text, fp);
	    }
	    fputs(currnode->text.text, fp);
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

/*
 * Convert a wide string into a string of chars. If `result' is
 * non-NULL, mallocs the resulting string and stores a pointer to
 * it in `*result'. If `result' is NULL, merely checks whether all
 * characters in the string are feasible for the output character
 * set.
 *
 * Return is nonzero if all characters are OK. If not all
 * characters are OK but `result' is non-NULL, a result _will_
 * still be generated!
 */
static int info_convert(wchar_t *s, char **result) {
    /*
     * FIXME. Currently this is ISO8859-1 only.
     */
    int doing = (result != 0);
    int ok = TRUE;
    char *p = NULL;
    int plen = 0, psize = 0;

    for (; *s; s++) {
	wchar_t c = *s;
	char outc;

	if ((c >= 32 && c <= 126) ||
	    (c >= 160 && c <= 255)) {
	    /* Char is OK. */
	    outc = (char)c;
	} else {
	    /* Char is not OK. */
	    ok = FALSE;
	    outc = 0xBF;	       /* approximate the good old DEC `uh?' */
	}
	if (doing) {
	    if (plen >= psize) {
		psize = plen + 256;
		p = resize(p, psize);
	    }
	    p[plen++] = outc;
	}
    }
    if (doing) {
	p = resize(p, plen+1);
	p[plen] = '\0';
	*result = p;
    }
    return ok;
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

static void info_rdaddwc(rdstringc *rs, word *words, word *end, int xrefs) {
    char *c;

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
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (towordstyle(words->type) == word_Code &&
		 (attraux(words->aux) == attr_First ||
		  attraux(words->aux) == attr_Only))
	    rdaddc(rs, '`');	       /* FIXME: configurability */
	if (removeattr(words->type) == word_Normal) {
	    if (info_convert(words->text, &c) || !words->alt)
		rdaddsc(rs, c);
	    else
		info_rdaddwc(rs, words->alt, NULL, FALSE);
	    sfree(c);
	} else if (removeattr(words->type) == word_WhiteSpace) {
	    rdaddc(rs, ' ');
	} else if (removeattr(words->type) == word_Quote) {
	    rdaddc(rs, quoteaux(words->aux) == quote_Open ? '`' : '\'');
				       /* FIXME: configurability */
	}
	if (towordstyle(words->type) == word_Emph &&
	    (attraux(words->aux) == attr_Last ||
	     attraux(words->aux) == attr_Only))
	    rdaddc(rs, '_');	       /* FIXME: configurability */
	else if (towordstyle(words->type) == word_Code &&
		 (attraux(words->aux) == attr_Last ||
		  attraux(words->aux) == attr_Only))
	    rdaddc(rs, '\'');	       /* FIXME: configurability */
	break;

      case word_UpperXref:
      case word_LowerXref:
	if (xrefs && words->private_data) {
	    rdaddsc(rs, "*Note ");
	    rdaddsc(rs, ((node *)words->private_data)->name);
	    rdaddsc(rs, "::");
	}
	break;
    }
}

static int info_width_internal(word *words, int xrefs);

static int info_width_internal_list(word *words, int xrefs) {
    int w = 0;
    while (words) {
	w += info_width_internal(words, xrefs);
	words = words->next;
    }
    return w;
}

static int info_width_internal(word *words, int xrefs) {
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
		(info_convert(words->text, NULL) || !words->alt ?
		 ustrlen(words->text) :
		 info_width_internal_list(words->alt, xrefs)));

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
	    return 8 + strlen(((node *)words->private_data)->name);
	}
	break;
    }
    return 0;			       /* should never happen */
}

static int info_width_noxrefs(void *ctx, word *words)
{
    IGNORE(ctx);
    return info_width_internal(words, FALSE);
}
static int info_width_xrefs(void *ctx, word *words)
{
    IGNORE(ctx);
    return info_width_internal(words, TRUE);
}

static void info_heading(rdstringc *text, word *tprefix,
			 word *words, int width) {
    rdstringc t = { 0, 0, NULL };
    int margin, length;
    int firstlinewidth, wrapwidth;
    int i;
    wrappedline *wrapping, *p;

    if (tprefix) {
	info_rdaddwc(&t, tprefix, NULL, FALSE);
	rdaddsc(&t, ": ");	       /* FIXME: configurability */
    }
    margin = length = (t.text ? strlen(t.text) : 0);

    margin = 0;
    firstlinewidth = width - length;
    wrapwidth = width;

    wrapping = wrap_para(words, firstlinewidth, wrapwidth,
			 info_width_noxrefs, NULL, 0);
    for (p = wrapping; p; p = p->next) {
	info_rdaddwc(&t, p->begin, p->end, FALSE);
	length = (t.text ? strlen(t.text) : 0);
	for (i = 0; i < margin; i++)
	    rdaddc(text, ' ');
	rdaddsc(text, t.text);
	rdaddc(text, '\n');
	for (i = 0; i < margin; i++)
	    rdaddc(text, ' ');
	while (length--)
	    rdaddc(text, '-');
	rdaddc(text, '\n');
	margin = 0;
	sfree(t.text);
	t = empty_rdstringc;
    }
    wrap_free(wrapping);
    rdaddc(text, '\n');

    sfree(t.text);
}

static void info_rule(rdstringc *text, int indent, int width) {
    while (indent--) rdaddc(text, ' ');
    while (width--) rdaddc(text, '-');
    rdaddc(text, '\n');
    rdaddc(text, '\n');
}

static void info_para(rdstringc *text, word *prefix, char *prefixextra,
		      word *input, keywordlist *keywords,
		      int indent, int extraindent, int width) {
    wrappedline *wrapping, *p;
    word *words;
    rdstringc pfx = { 0, 0, NULL };
    int e;
    int i;
    int firstlinewidth = width;

    words = info_transform_wordlist(input, keywords);

    if (prefix) {
	info_rdaddwc(&pfx, prefix, NULL, FALSE);
	if (prefixextra)
	    rdaddsc(&pfx, prefixextra);
	for (i = 0; i < indent; i++)
	    rdaddc(text, ' ');
	rdaddsc(text, pfx.text);
	/* If the prefix is too long, shorten the first line to fit. */
	e = extraindent - strlen(pfx.text);
	if (e < 0) {
	    firstlinewidth += e;       /* this decreases it, since e < 0 */
	    if (firstlinewidth < 0) {
		e = indent + extraindent;
		firstlinewidth = width;
		rdaddc(text, '\n');
	    } else
		e = 0;
	}
	sfree(pfx.text);
    } else
	e = indent + extraindent;

    wrapping = wrap_para(words, firstlinewidth, width, info_width_xrefs,
			 NULL, 0);
    for (p = wrapping; p; p = p->next) {
	for (i = 0; i < e; i++)
	    rdaddc(text, ' ');
	info_rdaddwc(text, p->begin, p->end, TRUE);
	rdaddc(text, '\n');
	e = indent + extraindent;
    }
    wrap_free(wrapping);
    rdaddc(text, '\n');

    free_word_list(words);
}

static void info_codepara(rdstringc *text, word *words,
			  int indent, int width) {
    int i;

    for (; words; words = words->next) if (words->type == word_WeakCode) {
	char *c;
	info_convert(words->text, &c);
	if (strlen(c) > (size_t)width) {
	    /* FIXME: warn */
	}
	for (i = 0; i < indent; i++)
	    rdaddc(text, ' ');
	rdaddsc(text, c);
	rdaddc(text, '\n');
	sfree(c);
    }

    rdaddc(text, '\n');
}

static void info_versionid(rdstringc *text, word *words) {
    rdaddc(text, '[');		       /* FIXME: configurability */
    info_rdaddwc(text, words, NULL, FALSE);
    rdaddsc(text, "]\n");
}

static node *info_node_new(char *name)
{
    node *n;

    n = mknew(node);
    n->text.text = NULL;
    n->text.pos = n->text.size = 0;
    n->up = n->next = n->prev = n->lastchild = n->listnext = NULL;
    n->name = dupstr(name);
    n->started_menu = FALSE;

    return n;
}

static char *info_node_name(paragraph *par)
{
    rdstringc rsc = { 0, 0, NULL };
    char *p, *q;
    info_rdaddwc(&rsc, par->kwtext ? par->kwtext : par->words, NULL, FALSE);

    /*
     * We cannot have commas or colons in a node name. Remove any
     * that we find, with a warning.
     */
    p = q = rsc.text;
    while (*p) {
	if (*p == ':' || *p == ',') {
	    error(err_infonodechar, &par->fpos, *p);
	} else {
	    *q++ = *p;
	}
	p++;
    }
    *p = '\0';

    return rsc.text;
}

static void info_menu_item(rdstringc *text, node *n, paragraph *p)
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
     * 
     */
    rdaddsc(text, "* ");
    rdaddsc(text, n->name);
    rdaddsc(text, "::");
    if (p) {
	rdaddc(text, ' ');
	info_rdaddwc(text, p->words, NULL, FALSE);
    }
    rdaddc(text, '\n');
}
