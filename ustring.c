/*
 * ustring.c: Unicode string routines
 */

#include <wchar.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "halibut.h"

wchar_t *ustrdup(wchar_t const *s) {
    wchar_t *r;
    if (s) {
	r = mknewa(wchar_t, 1+ustrlen(s));
	ustrcpy(r, s);
    } else {
	r = mknew(wchar_t);
	*r = 0;
    }
    return r;
}

static char *ustrtoa_internal(wchar_t const *s, char *outbuf, int size,
			      int charset, int careful) {
    int len, ret, err;
    charset_state state = CHARSET_INIT_STATE;

    if (!s) {
	*outbuf = '\0';
	return outbuf;
    }

    len = ustrlen(s);
    size--;			       /* leave room for terminating NUL */
    *outbuf = '\0';
    while (len > 0) {
	err = 0;
	ret = charset_from_unicode(&s, &len, outbuf, size, charset, &state,
				   (careful ? &err : NULL));
	if (err)
	    return NULL;
	if (!ret)
	    return outbuf;
	size -= ret;
	outbuf += ret;
	*outbuf = '\0';
    }
    /*
     * Clean up
     */
    ret = charset_from_unicode(NULL, 0, outbuf, size, charset, &state, NULL);
    size -= ret;
    outbuf += ret;
    *outbuf = '\0';
    return outbuf;
}

char *ustrtoa(wchar_t const *s, char *outbuf, int size, int charset) {
    return ustrtoa_internal(s, outbuf, size, charset, FALSE);
}

char *ustrtoa_careful(wchar_t const *s, char *outbuf, int size, int charset) {
    return ustrtoa_internal(s, outbuf, size, charset, TRUE);
}

wchar_t *ustrfroma(char const *s, wchar_t *outbuf, int size, int charset) {
    int len, ret;
    charset_state state = CHARSET_INIT_STATE;

    if (!s) {
	*outbuf = L'\0';
	return outbuf;
    }

    len = strlen(s);
    size--;			       /* allow for terminating NUL */
    *outbuf = L'\0';
    while (len > 0) {
	ret = charset_to_unicode(&s, &len, outbuf, size,
				 charset, &state, NULL, 0);
	if (!ret)
	    return outbuf;
	outbuf += ret;
	size -= ret;
	*outbuf = L'\0';
    }
    return outbuf;
}

char *utoa_internal_dup(wchar_t const *s, int charset, int *lenp, int careful)
{
    char *outbuf;
    int outpos, outlen, len, ret, err;
    charset_state state = CHARSET_INIT_STATE;

    if (!s) {
	return dupstr("");
    }

    len = ustrlen(s);

    outlen = len + 10;
    outbuf = mknewa(char, outlen);

    outpos = 0;
    outbuf[outpos] = '\0';

    while (len > 0) {
	err = 0;
	ret = charset_from_unicode(&s, &len,
				   outbuf + outpos, outlen - outpos - 1,
				   charset, &state, (careful ? &err : NULL));
	if (err) {
	    sfree(outbuf);
	    return NULL;
	}
	if (!ret) {
	    outlen = outlen * 3 / 2;
	    outbuf = resize(outbuf, outlen);
	}
	outpos += ret;
	outbuf[outpos] = '\0';
    }
    /*
     * Clean up
     */
    outlen = outpos + 32;
    outbuf = resize(outbuf, outlen);
    ret = charset_from_unicode(NULL, 0,
			       outbuf + outpos, outlen - outpos + 1,
			       charset, &state, NULL);
    outpos += ret;
    outbuf[outpos] = '\0';
    if (lenp)
	*lenp = outpos;
    return outbuf;
}

char *utoa_dup(wchar_t const *s, int charset)
{
    return utoa_internal_dup(s, charset, NULL, FALSE);
}

char *utoa_dup_len(wchar_t const *s, int charset, int *len)
{
    return utoa_internal_dup(s, charset, len, FALSE);
}

char *utoa_careful_dup(wchar_t const *s, int charset)
{
    return utoa_internal_dup(s, charset, NULL, TRUE);
}

wchar_t *ufroma_dup(char const *s, int charset) {
    int len;
    wchar_t *buf = NULL;

    len = strlen(s) + 1;
    do {
	buf = resize(buf, len);
	ustrfroma(s, buf, len, charset);
	len = (3 * len) / 2 + 1;       /* this guarantees a strict increase */
    } while (ustrlen(buf) >= len-1);

    buf = resize(buf, ustrlen(buf)+1);
    return buf;
}

char *utoa_locale_dup(wchar_t const *s)
{
    /*
     * This variant uses the C library locale.
     */
    char *ret;
    int len;
    size_t siz;

    len = ustrlen(s);

    ret = mknewa(char, 1 + MB_CUR_MAX * len);

    siz = wcstombs(ret, s, len);

    if (siz) {
	assert(siz <= MB_CUR_MAX * len);
	ret[siz] = '\0';
	ret = resize(ret, siz+1);
	return ret;
    }

    /*
     * If that failed, try a different strategy (which we will also
     * attempt in the total absence of wcstombs). Retrieve the
     * locale's charset from nl_langinfo or equivalent, and use
     * normal utoa_dup.
     */
    return utoa_dup(s, charset_from_locale());
}

wchar_t *ufroma_locale_dup(char const *s)
{
    /*
     * This variant uses the C library locale.
     */
    wchar_t *ret;
    int len;
    size_t siz;

    len = strlen(s);

    ret = mknewa(wchar_t, 1 + 2*len);  /* be conservative */

    siz = mbstowcs(ret, s, len);

    if (siz) {
	assert(siz <= (size_t)(2 * len));
	ret[siz] = L'\0';
	ret = resize(ret, siz+1);
	return ret;
    }

    /*
     * If that failed, try a different strategy (which we will also
     * attempt in the total absence of wcstombs). Retrieve the
     * locale's charset from nl_langinfo or equivalent, and use
     * normal ufroma_dup.
     */
    return ufroma_dup(s, charset_from_locale());
}

int ustrlen(wchar_t const *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

wchar_t *uadv(wchar_t *s) {
    return s + 1 + ustrlen(s);
}

wchar_t *ustrcpy(wchar_t *dest, wchar_t const *source) {
    wchar_t *ret = dest;
    do {
	*dest++ = *source;
    } while (*source++);
    return ret;
}

int ustrcmp(wchar_t *lhs, wchar_t *rhs) {
    if (!lhs && !rhs) return 0;
    if (!lhs) return -1;
    if (!rhs) return +1;
    while (*lhs && *rhs && *lhs==*rhs)
	lhs++, rhs++;
    if (*lhs < *rhs)
	return -1;
    else if (*lhs > *rhs)
	return 1;
    return 0;
}

wchar_t utolower(wchar_t c) {
    if (c == L'\0')
	return c;		       /* this property needed by ustricmp */
    /* FIXME: this doesn't even come close */
    if (c >= 'A' && c <= 'Z')
	c += 'a'-'A';
    return c;
}

int uisalpha(wchar_t c) {
    /* FIXME: this doesn't even come close */
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int ustricmp(wchar_t *lhs, wchar_t *rhs) {
    wchar_t lc, rc;
    while ((lc = utolower(*lhs)) == (rc = utolower(*rhs)) && lc && rc)
	lhs++, rhs++;
    if (!lc && !rc)
	return 0;
    if (lc < rc)
	return -1;
    else
	return 1;
}

wchar_t *ustrlow(wchar_t *s) {
    wchar_t *p = s;
    while (*p) {
	*p = utolower(*p);
	p++;
    }
    return s;
}

int utoi(wchar_t *s) {
    int sign = +1;
    int n;

    if (*s == L'-') {
	s++;
	sign = -1;
    }

    n = 0;
    while (*s && *s >= L'0' && *s <= L'9') {
	n *= 10;
	n += (*s - '0');
	s++;
    }

    return n;
}

int utob(wchar_t *s) {
    if (!ustricmp(s, L"yes") || !ustricmp(s, L"y") ||
	!ustricmp(s, L"true") || !ustricmp(s, L"t"))
	return TRUE;
    return FALSE;
}

int uisdigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

#define USTRFTIME_DELTA 128
wchar_t *ustrftime(wchar_t *wfmt, struct tm *timespec) {
    void *blk = NULL;
    wchar_t *wblk, *wp;
    char *fmt, *text, *p;
    size_t size = 0;
    size_t len;

    /*
     * FIXME: really we ought to copy non-% parts of the format
     * ourselves, and only resort to strftime for % parts. Also we
     * should use wcsftime if it's present.
     */

    /*
     * strftime has the entertaining property that it returns 0
     * _either_ on out-of-space _or_ on successful generation of
     * the empty string. Hence we must ensure our format can never
     * generate the empty string. Somebody throw a custard pie at
     * whoever was responsible for that. Please?
     */
    if (wfmt) {
	len = ustrlen(wfmt);
	fmt = mknewa(char, 2+len);
	ustrtoa(wfmt, fmt+1, len+1, CS_ASCII);   /* CS_FIXME? */
	fmt[0] = ' ';
    } else
	fmt = " %c";

    while (1) {
	size += USTRFTIME_DELTA;
	blk = resize((char *)blk, size);
	len = strftime((char *)blk, size-1, fmt, timespec);
	if (len > 0)
	    break;
    }

    /* Note: +1 for the terminating 0, -1 for the initial space in fmt */
    wblk = resize((wchar_t *)blk, len);
    text = mknewa(char, len);
    strftime(text, len, fmt+1, timespec);
    /*
     * We operate in the C locale, so this all ought to be kosher
     * ASCII. If we ever move outside ASCII machines, we may need
     * to make this more portable...
     */
    for (wp = wblk, p = text; *p; p++, wp++)
	*wp = *p;
    *wp = 0;
    if (wfmt)
	sfree(fmt);
    sfree(text);
    return wblk;
}

/*
 * Determine whether a Unicode string can be translated into a
 * given charset without any missing characters.
 */
int cvt_ok(int charset, const wchar_t *s)
{
    char buf[256];
    charset_state state = CHARSET_INIT_STATE;
    int err, len = ustrlen(s);

    err = 0;
    while (len > 0) {
	(void)charset_from_unicode(&s, &len, buf, lenof(buf),
				   charset, &state, &err);
	if (err)
	    return FALSE;
    }
    return TRUE;
}
