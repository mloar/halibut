/*
 * ustring.c: Unicode string routines
 */

#include <wchar.h>
#include "buttress.h"

wchar_t *ustrdup(wchar_t *s) {
    wchar_t *r;
    if (s) {
	r = smalloc((1+ustrlen(s)) * sizeof(wchar_t));
	ustrcpy(r, s);
    } else {
	r = smalloc(1);
	*r = 0;
    }
    return r;
}

char *ustrtoa(wchar_t *s, char *outbuf, int size) {
    char *p;
    if (!s) {
	*outbuf = '\0';
	return outbuf;
    }
    for (p = outbuf; *s && p < outbuf+size; p++,s++)
	*p = *s;
    if (p < outbuf+size)
	*p = '\0';
    else
	outbuf[size-1] = '\0';
    return outbuf;
}

int ustrlen(wchar_t *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

wchar_t *ustrcpy(wchar_t *dest, wchar_t *source) {
    wchar_t *ret = dest;
    do {
	*dest++ = *source;
    } while (*source++);
    return ret;
}
