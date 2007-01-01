/*
 * misc.c: miscellaneous useful items
 */

#include <stdarg.h>
#include "halibut.h"

char *adv(char *s) {
    return s + 1 + strlen(s);
}

struct stackTag {
    void **data;
    int sp;
    int size;
};

stack stk_new(void) {
    stack s;

    s = snew(struct stackTag);
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
	s->data = sresize(s->data, s->size, void *);
    }
    s->data[s->sp++] = item;
}

void *stk_pop(stack s) {
    if (s->sp > 0)
	return s->data[--s->sp];
    else
	return NULL;
}

void *stk_top(stack s) {
    if (s->sp > 0)
	return s->data[s->sp-1];
    else
	return NULL;
}

/*
 * Small routines to amalgamate a string from an input source.
 */
const rdstring empty_rdstring = {0, 0, NULL};
const rdstringc empty_rdstringc = {0, 0, NULL};

void rdadd(rdstring *rs, wchar_t c) {
    if (rs->pos >= rs->size-1) {
	rs->size = rs->pos + 128;
	rs->text = sresize(rs->text, rs->size, wchar_t);
    }
    rs->text[rs->pos++] = c;
    rs->text[rs->pos] = 0;
}
void rdadds(rdstring *rs, wchar_t const *p) {
    int len = ustrlen(p);
    if (rs->pos >= rs->size - len) {
	rs->size = rs->pos + len + 128;
	rs->text = sresize(rs->text, rs->size, wchar_t);
    }
    ustrcpy(rs->text + rs->pos, p);
    rs->pos += len;
}
wchar_t *rdtrim(rdstring *rs) {
    rs->text = sresize(rs->text, rs->pos + 1, wchar_t);
    return rs->text;
}

void rdaddc(rdstringc *rs, char c) {
    if (rs->pos >= rs->size-1) {
	rs->size = rs->pos + 128;
	rs->text = sresize(rs->text, rs->size, char);
    }
    rs->text[rs->pos++] = c;
    rs->text[rs->pos] = 0;
}
void rdaddsc(rdstringc *rs, char const *p) {
    rdaddsn(rs, p, strlen(p));
}
void rdaddsn(rdstringc *rs, char const *p, int len) {
    if (rs->pos >= rs->size - len) {
	rs->size = rs->pos + len + 128;
	rs->text = sresize(rs->text, rs->size, char);
    }
    memcpy(rs->text + rs->pos, p, len);
    rs->pos += len;
    rs->text[rs->pos] = 0;
}
char *rdtrimc(rdstringc *rs) {
    rs->text = sresize(rs->text, rs->pos + 1, char);
    return rs->text;
}

static int compare_wordlists_literally(word *a, word *b) {
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
	    c = compare_wordlists_literally(a->alt, b->alt);
	    if (c)
		return c;
	    a = a->next;
	    b = b->next;
	} else {
	    wchar_t *ap = a->text, *bp = b->text;
	    while (*ap && *bp) {
		wchar_t ac = *ap, bc = *bp;
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

int compare_wordlists(word *a, word *b) {
    /*
     * First we compare only the alphabetic content of the word
     * lists, with case not a factor. If that comes out equal,
     * _then_ we compare the word lists literally.
     */
    struct {
	word *w;
	int i;
	wchar_t c;
    } pos[2];

    pos[0].w = a;
    pos[1].w = b;
    pos[0].i = pos[1].i = 0;

    while (1) {
	/*
	 * Find the next alphabetic character in each word list.
	 */
	int k;

	for (k = 0; k < 2; k++) {
	    /*
	     * Advance until we hit either an alphabetic character
	     * or the end of the word list.
	     */
	    while (1) {
		if (!pos[k].w) {
		    /* End of word list. */
		    pos[k].c = 0;
		    break;
		} else if (!pos[k].w->text || !pos[k].w->text[pos[k].i]) {
		    /* No characters remaining in this word; move on. */
		    pos[k].w = pos[k].w->next;
		    pos[k].i = 0;
		} else if (!uisalpha(pos[k].w->text[pos[k].i])) {
		    /* This character isn't alphabetic; move on. */
		    pos[k].i++;
		} else {
		    /* We have an alphabetic! Lowercase it and continue. */
		    pos[k].c = utolower(pos[k].w->text[pos[k].i]);
		    break;
		}
	    }
	}

#ifdef HAS_WCSCOLL
	{
	    wchar_t a[2], b[2];
	    int ret;

	    a[0] = pos[0].c;
	    b[0] = pos[1].c;
	    a[1] = b[1] = L'\0';

	    ret = wcscoll(a, b);
	    if (ret)
		return ret;
	}
#else
	if (pos[0].c < pos[1].c)
	    return -1;
	else if (pos[0].c > pos[1].c)
	    return +1;
#endif

	if (!pos[0].c)
	    break;		       /* they're equal */

	pos[0].i++;
	pos[1].i++;
    }

    /*
     * If we reach here, the strings were alphabetically equal, so
     * compare in more detail.
     */
    return compare_wordlists_literally(a, b);
}

void mark_attr_ends(word *words)
{
    word *w, *wp;

    wp = NULL;
    for (w = words; w; w = w->next) {
	int both;
	if (!isvis(w->type))
	    /* Invisible elements should not affect this calculation */
	    continue;
	both = (isattr(w->type) &&
		wp && isattr(wp->type) &&
		sameattr(wp->type, w->type));
	w->aux |= both ? attr_Always : attr_First;
	if (wp && !both) {
	    /* If previous considered word turns out to have been
	     * the end of a run, tidy it up. */
	    int wp_attr = attraux(wp->aux);
	    wp->aux = (wp->aux & ~attr_mask) |
		((wp_attr == attr_Always) ? attr_Last
			 /* attr_First */ : attr_Only);
	}
	wp = w;
    }

    /* Tidy up last word touched */
    if (wp) {
	int wp_attr = attraux(wp->aux);
	wp->aux = (wp->aux & ~attr_mask) |
	    ((wp_attr == attr_Always) ? attr_Last
		     /* attr_First */ : attr_Only);
    }
}

/*
 * This function implements the optimal paragraph wrapping
 * algorithm, pretty much as used in TeX. A cost function is
 * defined for each line of the wrapped paragraph (typically some
 * convex function of the difference between the line's length and
 * its desired length), and a dynamic programming approach is used
 * to optimise globally across all possible layouts of the
 * paragraph to find the one with the minimum total cost.
 * 
 * The function as implemented here gives a choice of two options
 * for the cost function:
 * 
 *  - If `natural_space' is zero, then the algorithm attempts to
 *    make each line the maximum possible width (either `width' or
 *    `subsequentwidth' depending on whether it's the first line of
 *    the paragraph or not), and the cost function is simply the
 *    square of the unused space at the end of each line. This is a
 *    simple mechanism suitable for use in fixed-pitch environments
 *    such as plain text displayed on a terminal.
 * 
 *  - However, if `natural_space' is positive, the algorithm
 *    assumes the medium is fully graphical and that the width of
 *    space characters can be adjusted finely, and it attempts to
 *    make each _space character_ the width given in
 *    `natural_space'. (The provided width function should return
 *    the _minimum_ acceptable width of a space character in this
 *    case.) Therefore, the cost function for a line is dependent
 *    on the number of spaces on that line as well as the amount by
 *    which the line width differs from the optimum.
 */
wrappedline *wrap_para(word *text, int width, int subsequentwidth,
		       int (*widthfn)(void *, word *), void *ctx,
		       int natural_space) {
    wrappedline *head = NULL, **ptr = &head;
    int nwords, wordsize;
    struct wrapword {
	word *begin, *end;
	int width;
	int spacewidth;
	int cost;
	int nwords;
    } *wrapwords;
    int i, j, n;

    /*
     * Break the line up into wrappable components.
     */
    nwords = wordsize = 0;
    wrapwords = NULL;
    while (text) {
	if (nwords >= wordsize) {
	    wordsize = nwords + 64;
	    wrapwords = srealloc(wrapwords, wordsize * sizeof(*wrapwords));
	}
	wrapwords[nwords].width = 0;
	wrapwords[nwords].begin = text;
	while (text) {
	    wrapwords[nwords].width += widthfn(ctx, text);
	    wrapwords[nwords].end = text->next;
	    if (text->next && (text->next->type == word_WhiteSpace ||
			       text->next->type == word_EmphSpace ||
			       text->breaks))
		break;
	    text = text->next;
	}
	if (text && text->next && (text->next->type == word_WhiteSpace ||
			   text->next->type == word_EmphSpace)) {
	    wrapwords[nwords].spacewidth = widthfn(ctx, text->next);
	    text = text->next;
	} else {
	    wrapwords[nwords].spacewidth = 0;
	}
	nwords++;
	if (text)
	    text = text->next;
    }

    /*
     * Perform the dynamic wrapping algorithm: work backwards from
     * nwords-1, determining the optimal wrapping for each terminal
     * subsequence of the paragraph.
     */
    for (i = nwords; i-- ;) {
	int best = -1;
	int bestcost = 0;
	int cost;
	int linelen = 0, spacewidth = 0, minspacewidth = 0;
	int nspaces;
	int thiswidth = (i == 0 ? width : subsequentwidth);

	j = 0;
	nspaces = 0;
	while (i+j < nwords) {
	    /*
	     * See what happens if we put j+1 words on this line.
	     */
	    if (spacewidth) {
		nspaces++;
		minspacewidth = spacewidth;
	    }
	    linelen += spacewidth + wrapwords[i+j].width;
	    spacewidth = wrapwords[i+j].spacewidth;
	    j++;
	    if (linelen > thiswidth) {
		/*
		 * If we're over the width limit, abandon ship,
		 * _unless_ there is no best-effort yet (which will
		 * only happen if the first word is too long all by
		 * itself).
		 */
		if (best > 0)
		    break;
	    }

	    /*
	     * Compute the cost of this line. The method of doing
	     * this differs hugely depending on whether
	     * natural_space is nonzero or not.
	     */
	    if (natural_space) {
		if (!nspaces && linelen > thiswidth) {
		    /*
		     * Special case: if there are no spaces at all
		     * on the line because one single word is too
		     * long for its line, cost is zero because
		     * there's nothing we can do about it anyway.
		     */
		    cost = 0;
		} else {
		    int shortfall = thiswidth - linelen;
		    int spaceextra = shortfall / (nspaces ? nspaces : 1);
		    int spaceshortfall = natural_space -
			(minspacewidth + spaceextra);

		    if (i+j == nwords && spaceshortfall < 0) {
			/*
			 * Special case: on the very last line of
			 * the paragraph, we don't score penalty
			 * points for having to _stretch_ the line,
			 * since we won't stretch it anyway.
			 * However, we score penalties as normal
			 * for having to squeeze it.
			 */
			cost = 0;
		    } else {
			/*
			 * Squaring this number is tricky since
			 * it's liable to be quite big. Let's
			 * divide it through by 256.
			 */
			int x = spaceshortfall >> 8;
			int xf = spaceshortfall & 0xFF;

			/*
			 * Not counting strange variable-fixed-
			 * point oddities, we are computing
			 * 
			 *   (x+xf)^2 = x^2 + 2*x*xf + xf*xf
			 * 
			 * except that _our_ xf is 256 times the
			 * one listed there.
			 */

			cost = x * x;
			cost += (2 * x * xf) >> 8;
		    }
		}
	    } else {
		if (i+j == nwords) {
		    /*
		     * Special case: if we're at the very end of the
		     * paragraph, we don't score penalty points for the
		     * white space left on the line.
		     */
		    cost = 0;
		} else {
		    cost = (thiswidth-linelen) * (thiswidth-linelen);
		}
	    }

	    /*
	     * Add in the cost of wrapping all lines after this
	     * point too.
	     */
	    if (i+j < nwords)
		cost += wrapwords[i+j].cost;

	    /*
	     * We compare bestcost >= cost, not bestcost > cost,
	     * because in cases where the costs are identical we
	     * want to try to look like the greedy algorithm,
	     * because readers are likely to have spent a lot of
	     * time looking at greedy-wrapped paragraphs and
	     * there's no point violating the Principle of Least
	     * Surprise if it doesn't actually gain anything.
	     */
	    if (best < 0 || bestcost >= cost) {
		bestcost = cost;
		best = j;
	    }
	}
	/*
	 * Now we know the optimal answer for this terminal
	 * subsequence, so put it in wrapwords.
	 */
	wrapwords[i].cost = bestcost;
	wrapwords[i].nwords = best;
    }

    /*
     * We've wrapped the paragraph. Now build the output
     * `wrappedline' list.
     */
    i = 0;
    while (i < nwords) {
	wrappedline *w = snew(wrappedline);
	*ptr = w;
	ptr = &w->next;
	w->next = NULL;

	n = wrapwords[i].nwords;
	w->begin = wrapwords[i].begin;
	w->end = wrapwords[i+n-1].end;

	/*
	 * Count along the words to find nspaces and shortfall.
	 */
	w->nspaces = 0;
	w->shortfall = width;
	for (j = 0; j < n; j++) {
	    w->shortfall -= wrapwords[i+j].width;
	    if (j < n-1 && wrapwords[i+j].spacewidth) {
		w->nspaces++;
		w->shortfall -= wrapwords[i+j].spacewidth;
	    }
	}
	i += n;
    }

    sfree(wrapwords);

    return head;
}

void wrap_free(wrappedline *w) {
    while (w) {
	wrappedline *t = w->next;
	sfree(w);
	w = t;
    }
}

void cmdline_cfg_add(paragraph *cfg, char *string)
{
    wchar_t *ustring;
    int upos, ulen, pos, len;

    ulen = 0;
    while (cfg->keyword[ulen])
	ulen += 1 + ustrlen(cfg->keyword+ulen);
    len = 0;
    while (cfg->origkeyword[len])
	len += 1 + strlen(cfg->origkeyword+len);

    ustring = ufroma_locale_dup(string);

    upos = ulen;
    ulen += 2 + ustrlen(ustring);
    cfg->keyword = sresize(cfg->keyword, ulen, wchar_t);
    ustrcpy(cfg->keyword+upos, ustring);
    cfg->keyword[ulen-1] = L'\0';

    pos = len;
    len += 2 + strlen(string);
    cfg->origkeyword = sresize(cfg->origkeyword, len, char);
    strcpy(cfg->origkeyword+pos, string);
    cfg->origkeyword[len-1] = '\0';

    sfree(ustring);
}

paragraph *cmdline_cfg_new(void)
{
    paragraph *p;

    p = snew(paragraph);
    memset(p, 0, sizeof(*p));
    p->type = para_Config;
    p->next = NULL;
    p->fpos.filename = "<command line>";
    p->fpos.line = p->fpos.col = -1;
    p->keyword = ustrdup(L"\0");
    p->origkeyword = dupstr("\0");

    return p;
}

paragraph *cmdline_cfg_simple(char *string, ...)
{
    va_list ap;
    char *s;
    paragraph *p;

    p = cmdline_cfg_new();
    cmdline_cfg_add(p, string);

    va_start(ap, string);
    while ((s = va_arg(ap, char *)) != NULL)
	cmdline_cfg_add(p, s);
    va_end(ap);

    return p;
}
