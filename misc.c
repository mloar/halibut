/*
 * misc.c: miscellaneous useful items
 */

#include "buttress.h"

struct stackTag {
    void **data;
    int sp;
    int size;
};

stack stk_new(void) {
    stack s;

    s = mknew(struct stackTag);
    s->sp = 0;
    s->size = 0;
    s->data = NULL;

    return s;
}

void stk_free(stack s) {
    sfree(s->data);
    sfree(s);
}

void stk_push(stack s, void *item) {
    if (s->size <= s->sp) {
	s->size = s->sp + 32;
	s->data = resize(s->data, s->size);
    }
    s->data[s->sp++] = item;
}

void *stk_pop(stack s) {
    if (s->sp > 0)
	return s->data[--s->sp];
    else
	return NULL;
}

/*
 * Small routines to amalgamate a string from an input source.
 */
void rdadd(rdstring *rs, wchar_t c) {
    if (rs->pos >= rs->size-1) {
	rs->size = rs->pos + 128;
	rs->text = resize(rs->text, rs->size);
    }
    rs->text[rs->pos++] = c;
    rs->text[rs->pos] = 0;
}
void rdadds(rdstring *rs, wchar_t *p) {
    int len = ustrlen(p);
    if (rs->pos >= rs->size - len) {
	rs->size = rs->pos + len + 128;
	rs->text = resize(rs->text, rs->size);
    }
    ustrcpy(rs->text + rs->pos, p);
    rs->pos += len;
}
wchar_t *rdtrim(rdstring *rs) {
    rs->text = resize(rs->text, rs->pos + 1);
    return rs->text;
}

void rdaddc(rdstringc *rs, char c) {
    if (rs->pos >= rs->size-1) {
	rs->size = rs->pos + 128;
	rs->text = resize(rs->text, rs->size);
    }
    rs->text[rs->pos++] = c;
    rs->text[rs->pos] = 0;
}
void rdaddsc(rdstringc *rs, char *p) {
    int len = strlen(p);
    if (rs->pos >= rs->size - len) {
	rs->size = rs->pos + len + 128;
	rs->text = resize(rs->text, rs->size);
    }
    strcpy(rs->text + rs->pos, p);
    rs->pos += len;
}
char *rdtrimc(rdstringc *rs) {
    rs->text = resize(rs->text, rs->pos + 1);
    return rs->text;
}

int compare_wordlists(word *a, word *b) {
    int t;
    while (a && b) {
	if (a->type != b->type)
	    return (a->type < b->type ? -1 : +1);   /* FIXME? */
	t = a->type;
	if ((t != word_Normal && t != word_Code &&
	     t != word_WeakCode && t != word_Emph) ||
	    a->alt || b->alt) {
	    int c;
	    if (a->text && b->text) {
		c = ustricmp(a->text, b->text);
		if (c)
		    return c;
	    }
	    c = compare_wordlists(a->alt, b->alt);
	    if (c)
		return c;
	    a = a->next;
	    b = b->next;
	} else {
	    wchar_t *ap = a->text, *bp = b->text;
	    while (*ap && *bp) {
		wchar_t ac = utolower(*ap), bc = utolower(*bp);
		if (ac != bc)
		    return (ac < bc ? -1 : +1);
		if (!*++ap && a->next && a->next->type == t && !a->next->alt)
		    a = a->next, ap = a->text;
		if (!*++bp && b->next && b->next->type == t && !b->next->alt)
		    b = b->next, bp = b->text;
	    }
	    if (*ap || *bp)
		return (*ap ? +1 : -1);
	    a = a->next;
	    b = b->next;
	}
    }

    if (a || b)
	return (a ? +1 : -1);
    else
	return 0;
}

void mark_attr_ends(paragraph *sourceform) {
    paragraph *p;
    word *w, *wp;
    for (p = sourceform; p; p = p->next) {
	wp = NULL;
	for (w = p->words; w; w = w->next) {
	    if (isattr(w->type)) {
		int before = (wp && isattr(wp->type) &&
			      sameattr(wp->type, w->type));
		int after = (w->next && isattr(w->next->type) &&
			     sameattr(w->next->type, w->type));
		w->aux = (before ?
			  (after ? attr_Always : attr_Last) :
			  (after ? attr_First : attr_Only));
	    }
	    wp = w;
	}
    }
}

wrappedline *wrap_para(word *text, int width, int subsequentwidth,
		       int (*widthfn)(word *)) {
    wrappedline *head = NULL, **ptr = &head;
    word *spc;
    int nspc;
    int thiswidth, lastgood;

    while (text) {
	wrappedline *w = mknew(wrappedline);
	*ptr = w;
	ptr = &w->next;
	w->next = NULL;

	w->begin = text;
	spc = NULL;
	nspc = 0;
	thiswidth = lastgood = 0;
	while (text) {
	    thiswidth += widthfn(text);
	    if (text->next && (text->next->type == word_WhiteSpace ||
			       text->next->type == word_EmphSpace ||
			       text->breaks)) {
		if (thiswidth > width)
		    break;
		spc = text->next;
		lastgood = thiswidth;
		nspc++;
	    }
	    text = text->next;
	}
	/*
	 * We've exited this loop on one of three conditions. spc
	 * might be non-NULL and we've overflowed: we have broken
	 * the paragraph there. spc might be NULL and text might be
	 * NULL too: we've reached the end of the paragraph and
	 * should output the last line. Or text might be non-NULL
	 * and spc might be NULL: we've got an anomalously long
	 * line with no spaces we could have broken at. Output it
	 * anyway as the best we can do.
	 */
	if (spc && thiswidth > width) {
	    w->end = text = spc;
	    w->nspaces = nspc-1;
	    w->shortfall = width - lastgood;
	} else if (!text) {
	    w->end = NULL;	       /* no end marker needed */
	    w->nspaces = nspc;
	    w->shortfall = width - thiswidth;
	} else {
	    w->end = text;
	    w->nspaces = 0;
	    w->shortfall = width - thiswidth;
	}
	/*
	 * Skip the space if we're on one.
	 */
	if (text && (text->type == word_WhiteSpace ||
		     text->type == word_EmphSpace))
	    text = text->next;
	width = subsequentwidth;
    }

    return head;
}

void wrap_free(wrappedline *w) {
    while (w) {
	wrappedline *t = w->next;
	sfree(w);
	w = t;
    }
}
