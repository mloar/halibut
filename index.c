/*
 * index.c: create and collate index data structures
 */

#include <stdio.h>
#include <stdlib.h>
#include "halibut.h"

static int compare_tags(void *av, void *bv);
static int compare_entries(void *av, void *bv);

indexdata *make_index(void) {
    indexdata *ret = snew(indexdata);
    ret->tags = newtree234(compare_tags);
    ret->entries = newtree234(compare_entries);
    return ret;
}

static indextag *make_indextag(void) {
    indextag *ret = snew(indextag);
    ret->name = NULL;
    ret->implicit_text = NULL;
    ret->explicit_texts = NULL;
    ret->explicit_fpos = NULL;
    ret->nexplicit = ret->explicit_size = ret->nrefs = 0;
    ret->refs = NULL;
    return ret;
}

static int compare_tags(void *av, void *bv) {
    indextag *a = (indextag *)av, *b = (indextag *)bv;
    return ustricmp(a->name, b->name);
}

static int compare_to_find_tag(void *av, void *bv) {
    wchar_t *a = (wchar_t *)av;
    indextag *b = (indextag *)bv;
    return ustricmp(a, b->name);
}

static int compare_entries(void *av, void *bv) {
    indexentry *a = (indexentry *)av, *b = (indexentry *)bv;
    return compare_wordlists(a->text, b->text);    
}

/*
 * Back-end utility: find the indextag with a given name.
 */
indextag *index_findtag(indexdata *idx, wchar_t *name) {
    return find234(idx->tags, name, compare_to_find_tag);
}

/*
 * Add a \IM. `tags' points to a zero-terminated chain of
 * zero-terminated strings ("first\0second\0thirdandlast\0\0").
 * `text' points to a word list.
 *
 * Guarantee on calling sequence: all implicit merges are given
 * before the explicit ones.
 */
void index_merge(indexdata *idx, int is_explicit, wchar_t *tags, word *text,
		 filepos *fpos) {
    indextag *t, *existing;

    /*
     * For an implicit merge, we want to remove all emphasis,
     * because the chances are that the user didn't really want to
     * index the term as emphasised.
     */
    {
	word *w;

	for (w = text; w; w = w->next) {
	    if (w->type == word_Emph)
		w->type = word_Normal;
	    else if (w->type == word_EmphSpace)
		w->type = word_WhiteSpace;
	    else if (w->type == word_EmphQuote)
		w->type = word_Quote;
	}
    }

    /*
     * FIXME: want to warn on overlapping source sets.
     */
    for (; *tags; tags = uadv(tags)) {
	t = make_indextag();
	t->name = tags;
	existing = add234(idx->tags, t);
	if (existing == t) {
	    /*
	     * Duplicate this so we can free it independently.
	     */
	    t->name = ustrdup(tags);

	    /*
	     * Every tag has an implicit \IM. So if this tag
	     * doesn't exist and we're explicit, then we should
	     * warn (and drop it, since it won't be referenced).
	     */
	    if (is_explicit) {
		error(err_nosuchidxtag, fpos, tags);
		continue;
	    }

	    /*
	     * Otherwise, this is a new tag with an implicit \IM.
	     */
	    t->implicit_text = text;
	    t->implicit_fpos = *fpos;
	} else {
	    if (!is_explicit) {
 		/*
		 * An implicit \IM for a tag that's had an implicit
		 * \IM before. FIXME: we should check the text
		 * against the existing text and warn on
		 * differences. And check the tag for case match
		 * against the existing tag, likewise.
		 */

		/*
		 * Check the tag against its previous occurrence to
		 * see if the cases match.
		 */
		if (ustrcmp(t->name, existing->name)) {
		    error(err_indexcase, fpos, t->name,
			  &existing->implicit_fpos, existing->name);
		}

		sfree(t);
	    } else {
		/*
		 * An explicit \IM added to a valid tag. In
		 * particular, this removes the implicit \IM if
		 * present.
		 */
		sfree(t);
		t = existing;
		if (t->implicit_text) {
		    free_word_list(t->implicit_text);
		    t->implicit_text = NULL;
		}
		if (t->nexplicit >= t->explicit_size) {
		    t->explicit_size = t->nexplicit + 8;
		    t->explicit_texts = sresize(t->explicit_texts,
						t->explicit_size, word *);
		    t->explicit_fpos = sresize(t->explicit_fpos,
					       t->explicit_size, filepos);
		}
		t->explicit_texts[t->nexplicit] = text;
		t->explicit_fpos[t->nexplicit] = *fpos;
		t->nexplicit++;
	    }
	}
    }
}

/*
 * Build the final-form index. We now have every tag, with every
 * \IM, set up in a 2-3 tree indexed by tag. We now want to collate
 * the RHSes of the \IMs, and sort by final form, and decorate the
 * entries in the original 2-3 tree with pointers to the RHS
 * entries.
 */
void build_index(indexdata *i) {
    indextag *t;
    word **ta;
    filepos *fa;
    int ti;
    int j;

    for (ti = 0; (t = (indextag *)index234(i->tags, ti)) != NULL; ti++) {
	if (t->implicit_text) {
	    t->nrefs = 1;
	    ta = &t->implicit_text;
	    fa = &t->implicit_fpos;
	} else {
	    t->nrefs = t->nexplicit;
	    ta = t->explicit_texts;
	    fa = t->explicit_fpos;
	}
	if (t->nrefs) {
	    t->refs = snewn(t->nrefs, indexentry *);
	    for (j = 0; j < t->nrefs; j++) {
		indexentry *ent = snew(indexentry);
		ent->text = *ta++;
		ent->fpos = *fa++;
		t->refs[j] = add234(i->entries, ent);
		if (t->refs[j] != ent)     /* duplicate */
		    sfree(ent);
	    }
	}
    }
}

void cleanup_index(indexdata *i) {
    indextag *t;
    indexentry *ent;
    int ti;

    for (ti = 0; (t = (indextag *)index234(i->tags, ti)) != NULL; ti++) {
	sfree(t->name);
	free_word_list(t->implicit_text);
	sfree(t->explicit_texts);
	sfree(t->refs);
	sfree(t);
    }
    freetree234(i->tags);
    for (ti = 0; (ent = (indexentry *)index234(i->entries, ti))!=NULL; ti++) {
	sfree(ent);
    }
    freetree234(i->entries);
    sfree(i);
}

static void dbg_prtwordlist(int level, word *w);
static void dbg_prtmerge(int is_explicit, wchar_t *tag, word *text);

void index_debug(indexdata *i) {
    indextag *t;
    indexentry *y;
    int ti;
    int j;

    printf("\nINDEX TAGS\n==========\n\n");
    for (ti = 0; (t = (indextag *)index234(i->tags, ti)) != NULL; ti++) {
        printf("\n");
	if (t->implicit_text)
	    dbg_prtmerge(0, t->name, t->implicit_text);
	for (j = 0; j < t->nexplicit; j++)
	    dbg_prtmerge(1, t->name, t->explicit_texts[j]);
    }

    printf("\nINDEX ENTRIES\n=============\n\n");
    for (ti = 0; (y = (indexentry *)index234(i->entries, ti)) != NULL; ti++) {
        printf("\n");
	printf("{\n");
	dbg_prtwordlist(1, y->text);
	printf("}\n");
    }
}

static void dbg_prtmerge(int is_explicit, wchar_t *tag, word *text) {
    printf("\\IM: %splicit: \"", is_explicit ? "ex" : "im");
    for (; *tag; tag++)
	putchar(*tag);
    printf("\" {\n");
    dbg_prtwordlist(1, text);
    printf("}\n");
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
