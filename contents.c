/*
 * contents.c: build a table of contents
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include "buttress.h"

struct numberstate_Tag {
    int chapternum;
    int appendixnum;
    int ischapter;
    int *sectionlevels;
    int maxsectlevel;
    int listitem;
};

numberstate *number_init(void) {
    numberstate *ret = smalloc(sizeof(numberstate));
    ret->chapternum = 0;
    ret->appendixnum = -1;
    ret->ischapter = 1;
    ret->maxsectlevel = 32;
    ret->sectionlevels = smalloc(ret->maxsectlevel *
				 sizeof(*ret->sectionlevels));
    ret->listitem = -1;
    return ret;
}

void number_free(numberstate *state) {
    sfree(state);
}

static void dotext(word ***wret, wchar_t *text) {
    word *mnewword = smalloc(sizeof(word));
    mnewword->text = ustrdup(text);
    mnewword->type = word_Normal;
    mnewword->alt = NULL;
    mnewword->next = NULL;
    **wret = mnewword;
    *wret = &mnewword->next;
}

static void dospace(word ***wret) {
    word *mnewword = smalloc(sizeof(word));
    mnewword->text = NULL;
    mnewword->type = word_WhiteSpace;
    mnewword->alt = NULL;
    mnewword->next = NULL;
    **wret = mnewword;
    *wret = &mnewword->next;
}

static void donumber(word ***wret, int num) {
    wchar_t text[20];
    wchar_t *p = text + sizeof(text);
    *--p = L'\0';
    while (num != 0) {
	assert(p > text);
	*--p = L"0123456789"[num % 10];
	num /= 10;
    }
    dotext(wret, p);
}

static void doanumber(word ***wret, int num) {
    wchar_t text[20];
    wchar_t *p;
    int nletters, aton;
    nletters = 1;
    aton = 25;
    while (num > aton) {
	nletters++;
	num -= aton+1;
	if (aton < INT_MAX/26)
	    aton = (aton+1) * 26 - 1;
	else
	    aton = INT_MAX;
    }
    p = text + sizeof(text);
    *--p = L'\0';
    while (nletters--) {
	assert(p > text);
	*--p = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"[num % 26];
	num /= 26;
    }
    dotext(wret, p);
}

word *number_mktext(numberstate *state, int para, int aux, int prev) {
    word *ret = NULL;
    word **pret = &ret;
    int i, level;

    switch (para) {
      case para_Chapter:
	state->chapternum++;
	for (i = 0; i < state->maxsectlevel; i++)
	    state->sectionlevels[i] = 0;
	dotext(&pret, L"Chapter");
	dospace(&pret);
	donumber(&pret, state->chapternum);
	state->ischapter = 1;
	break;
      case para_Heading:
      case para_Subsect:
	level = (para == para_Heading ? 0 : aux);
	if (state->maxsectlevel <= level) {
	    state->maxsectlevel = level + 32;
	    state->sectionlevels = srealloc(state->sectionlevels,
					    state->maxsectlevel *
					    sizeof(*state->sectionlevels));
	}
	state->sectionlevels[level]++;
	for (i = level+1; i < state->maxsectlevel; i++)
	    state->sectionlevels[i] = 0;
	dotext(&pret, L"Section");
	dospace(&pret);
	if (state->ischapter)
	    donumber(&pret, state->chapternum);
	else
	    doanumber(&pret, state->appendixnum);
	for (i = 0; i <= level; i++) {
	    dotext(&pret, L".");
	    if (state->sectionlevels[i] == 0)
		state->sectionlevels[i] = 1;
	    donumber(&pret, state->sectionlevels[i]);
	}
	break;
      case para_Appendix:
	state->appendixnum++;
	for (i = 0; i < state->maxsectlevel; i++)
	    state->sectionlevels[i] = 0;
	dotext(&pret, L"Appendix");
	dospace(&pret);
	doanumber(&pret, state->appendixnum);
	state->ischapter = 0;
	break;
      case para_NumberedList:
	if (prev != para_NumberedList)
	    state->listitem = 0;
	state->listitem++;
	donumber(&pret, state->listitem);
	break;
    }

    return ret;
}
