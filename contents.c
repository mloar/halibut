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
    int oklevel;
    int maxsectlevel;
    int listitem;
    wchar_t *chaptertext;	       /* the word for a chapter */
    wchar_t *sectiontext;	       /* the word for a section */
    wchar_t *apptext;		       /* the word for an appendix */
};

numberstate *number_init(void) {
    numberstate *ret = mknew(numberstate);
    ret->chapternum = 0;
    ret->appendixnum = -1;
    ret->ischapter = 1;
    ret->oklevel = -1;		       /* not even in a chapter yet */
    ret->maxsectlevel = 32;
    ret->sectionlevels = mknewa(int, ret->maxsectlevel);
    ret->listitem = -1;
    return ret;
}

void number_free(numberstate *state) {
    sfree(state->sectionlevels);
    sfree(state);
}

static void dotext(word ***wret, wchar_t *text) {
    word *mnewword = mknew(word);
    mnewword->text = ustrdup(text);
    mnewword->type = word_Normal;
    mnewword->alt = NULL;
    mnewword->next = NULL;
    **wret = mnewword;
    *wret = &mnewword->next;
}

static void dospace(word ***wret) {
    word *mnewword = mknew(word);
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

void number_cfg(numberstate *state, paragraph *source) {
    /*
     * Defaults
     */
    state->chaptertext = L"Chapter";
    state->sectiontext = L"Section";
    state->apptext = L"Appendix";

    for (; source; source = source->next) {
	if (source->type == para_Config) {
	    if (!ustricmp(source->keyword, L"chapter")) {
		state->chaptertext = uadv(source->keyword);
	    } else if (!ustricmp(source->keyword, L"section")) {
		state->sectiontext = uadv(source->keyword);
	    } else if (!ustricmp(source->keyword, L"appendix")) {
		state->apptext = uadv(source->keyword);
	    }
	}
    }
}

word *number_mktext(numberstate *state, int para, int aux, int prev,
		    word **auxret, filepos fpos, int *errflag) {
    word *ret = NULL;
    word **ret2 = &ret;
    word **pret = &ret;
    int i, level;

    switch (para) {
      case para_Chapter:
	state->chapternum++;
	for (i = 0; i < state->maxsectlevel; i++)
	    state->sectionlevels[i] = 0;
	dotext(&pret, state->chaptertext);
	dospace(&pret);
	ret2 = pret;
	donumber(&pret, state->chapternum);
	state->ischapter = 1;
	state->oklevel = 0;
	break;
      case para_Heading:
      case para_Subsect:
	level = (para == para_Heading ? 0 : aux);
	if (level > state->oklevel) {
	    error(err_sectjump, &fpos);
	    *errflag = TRUE;
	    return NULL;
	}
	state->oklevel = level+1;
	if (state->maxsectlevel <= level) {
	    state->maxsectlevel = level + 32;
	    state->sectionlevels = resize(state->sectionlevels,
					  state->maxsectlevel);
	}
	state->sectionlevels[level]++;
	for (i = level+1; i < state->maxsectlevel; i++)
	    state->sectionlevels[i] = 0;
	dotext(&pret, state->sectiontext);
	dospace(&pret);
	ret2 = pret;
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
	dotext(&pret, state->apptext);
	dospace(&pret);
	ret2 = pret;
	doanumber(&pret, state->appendixnum);
	state->ischapter = 0;
	state->oklevel = 0;
	break;
      case para_NumberedList:
	ret2 = pret;
	if (prev != para_NumberedList)
	    state->listitem = 0;
	state->listitem++;
	donumber(&pret, state->listitem);
	break;
    }

    if (auxret)
	*auxret = *ret2;
    return ret;
}
