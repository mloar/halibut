/*
 * keywords.c: keep track of all cross-reference keywords
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "buttress.h"

#define heapparent(x) (((x)+1)/2-1)
#define heaplchild(x) (2*(x)+1)
#define heaprchild(x) (2*(x)+2)

#define key(x) ( kl->keys[(x)] )

#define greater(x,y) ( ustrcmp(key(x)->key, key(y)->key) > 0 )
#define swap(x,y) do { keyword *t=key(x); key(x)=key(y); key(y)=t; } while(0)

static void heap_add(keywordlist *kl, keyword *key) {
    int p;
    if (kl->nkeywords >= kl->size) {
	kl->size = kl->nkeywords + 128;
	kl->keys = resize(kl->keys, kl->size);
    }
    p = kl->nkeywords++;
    kl->keys[p] = key;
    while (heapparent(p) >= 0 && greater(p, heapparent(p))) {
	swap(p, heapparent(p));
	p = heapparent(p);
    }
}

static void heap_sort(keywordlist *kl) {
    int i, j;

    kl->size = kl->nkeywords;
    kl->keys = resize(kl->keys, kl->size);

    i = kl->nkeywords;
    while (i > 1) {
	i--;
	swap(0, i);		       /* put greatest at end */
	j = 0;
	while (1) {
	    int left = heaplchild(j), right = heaprchild(j);
	    if (left >= i || !greater(left, j))
		left = -1;
	    if (right >= i || !greater(right, j))
		right = -1;
	    if (left >= 0 && right >= 0) {
		if (greater(left, right))
		    right = -1;
		else
		    left = -1;
	    }
	    if (left >= 0) { swap(j, left); j = left; }
	    else if (right >= 0) { swap(j, right); j = right; }
	    else break;
	}
    }
    /* FIXME: check for duplicate keys; do what about them? */
}

keyword *kw_lookup(keywordlist *kl, wchar_t *str) {
    int i, j, k, cmp;

    i = -1;
    j = kl->nkeywords;
    while (j-i > 1) {
	k = (i+j)/2;
	cmp = ustrcmp(str, kl->keys[k]->key);
	if (cmp < 0)
	    j = k;
	else if (cmp > 0)
	    i = k;
	else
	    return kl->keys[k];
    }
    return NULL;
}

/*
 * This function reads through source form and collects the
 * keywords. They get collected in a heap, sorted by Unicode
 * collation, last at the top (so that we can Heapsort them when we
 * finish).
 */
keywordlist *get_keywords(paragraph *source) {
    keywordlist *kl = mknew(keywordlist);
    numberstate *n = number_init();
    int prevpara = para_NotParaType;

    kl->nkeywords = 0;
    kl->size = 0;
    kl->keys = NULL;
    for (; source; source = source->next) {
	/*
	 * Number the chapter / section / list-item / whatever.
	 */
	source->kwtext = number_mktext(n, source->type, source->aux,
				       prevpara);
	prevpara = source->type;

	if (source->keyword && *source->keyword) {
	    if (source->kwtext || source->type == para_Biblio) {
		wchar_t *p = source->keyword;
		while (*p) {
		    keyword *kw = mknew(keyword);
		    kw->key = p;
		    kw->text = source->kwtext;
		    kw->para = source;
		    heap_add(kl, kw);
		    p += ustrlen(p) + 1;
		}
	    }
	}
    }

    number_free(n);

    heap_sort(kl);

    return kl;
}

void free_keywords(keywordlist *kl) {
    int i;
    for (i = 0; i < kl->nkeywords; i++)
	sfree(kl->keys[i]);
    sfree(kl);
}

void subst_keywords(paragraph *source, keywordlist *kl) {
    for (; source; source = source->next) {
	word *ptr;
	for (ptr = source->words; ptr; ptr = ptr->next) {
	    if (ptr->type == word_UpperXref ||
		ptr->type == word_LowerXref) {
		keyword *kw;
		word **endptr, *close, *subst;

		kw = kw_lookup(kl, ptr->text);
		if (!kw) {
		    error(err_nosuchkw, &ptr->fpos, ptr->text);
		    subst = NULL;
		} else
		    subst = dup_word_list(kw->text);

		if (subst && ptr->type == word_LowerXref)
		    ustrlow(subst->text);

		close = mknew(word);
		close->text = NULL;
		close->alt = NULL;
		close->type = word_XrefEnd;
		close->fpos = ptr->fpos;

		close->next = ptr->next;
		ptr->next = subst;

		for (endptr = &ptr->next; *endptr; endptr = &(*endptr)->next)
		    (*endptr)->fpos = ptr->fpos;

		*endptr = close;
		ptr = close;
	    }
	}
    }
}
