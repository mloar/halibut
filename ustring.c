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
	r = snewn(1+ustrlen(s), wchar_t);
	ustrcpy(r, s);
    } else {
	r = snew(wchar_t);
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
    outbuf = snewn(outlen, char);

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
	    outbuf = sresize(outbuf, outlen, char);
	}
	outpos += ret;
	outbuf[outpos] = '\0';
    }
    /*
     * Clean up
     */
    outlen = outpos + 32;
    outbuf = sresize(outbuf, outlen, char);
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
	buf = sresize(buf, len, wchar_t);
	ustrfroma(s, buf, len, charset);
	len = (3 * len) / 2 + 1;       /* this guarantees a strict increase */
    } while (ustrlen(buf) >= len-1);

    buf = sresize(buf, ustrlen(buf)+1, wchar_t);
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

    ret = snewn(1 + MB_CUR_MAX * len, char);

    siz = wcstombs(ret, s, len);

    if (siz) {
	assert(siz <= (size_t)(MB_CUR_MAX * len));
	ret[siz] = '\0';
	ret = sresize(ret, siz+1, char);
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

    ret = snewn(1 + 2*len, wchar_t);  /* be conservative */

    siz = mbstowcs(ret, s, len);

    if (siz) {
	assert(siz <= (size_t)(2 * len));
	ret[siz] = L'\0';
	ret = sresize(ret, siz+1, wchar_t);
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

wchar_t *ustrncpy(wchar_t *dest, wchar_t const *source, int n) {
    wchar_t *ret = dest;
    do {
	*dest++ = *source;
	if (*source) source++;
    } while (n-- > 0);
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
#ifdef HAS_TOWLOWER
    return towlower(c);
#else
    if (c >= 'A' && c <= 'Z')
	c += 'a'-'A';
    return c;
#endif
}

int uisalpha(wchar_t c) {
#ifdef HAS_ISWALPHA
    return iswalpha(c);
#else
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
#endif
}

int ustricmp(wchar_t const *lhs, wchar_t const *rhs) {
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

int ustrnicmp(wchar_t const *lhs, wchar_t const *rhs, int maxlen) {
    wchar_t lc = 0, rc = 0;
    while (maxlen-- > 0 &&
	   (lc = utolower(*lhs)) == (rc = utolower(*rhs)) && lc && rc)
	lhs++, rhs++;
    if (lc < rc)
	return -1;
    else if (lc > rc)
	return 1;
    else
	return 0;
}

wchar_t *ustrlow(wchar_t *s) {
    wchar_t *p = s;
    while (*p) {
	*p = utolower(*p);
	p++;
    }
    return s;
}

int utoi(wchar_t const *s) {
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

double utof(wchar_t const *s)
{
    char *cs = utoa_dup(s, CS_ASCII);
    double ret = atof(cs);
    sfree(cs);
    return ret;
}

int utob(wchar_t const *s) {
    if (!ustricmp(s, L"yes") || !ustricmp(s, L"y") ||
	!ustricmp(s, L"true") || !ustricmp(s, L"t"))
	return TRUE;
    return FALSE;
}

int uisdigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

#define USTRFTIME_DELTA 128
static void ustrftime_internal(rdstring *rs, char formatchr,
			       const struct tm *timespec)
{
    /*
     * strftime has the entertaining property that it returns 0
     * _either_ on out-of-space _or_ on successful generation of
     * the empty string. Hence we must ensure our format can never
     * generate the empty string. Somebody throw a custard pie at
     * whoever was responsible for that. Please?
     */

#ifdef HAS_WCSFTIME
    wchar_t *buf = NULL;
    wchar_t fmt[4];
    int size, ret;

    fmt[0] = L' ';
    fmt[1] = L'%';
    /* Format chars are all ASCII, so conversion to Unicode is no problem */
    fmt[2] = formatchr;
    fmt[3] = L'\0';

    size = 0;
    do {
	size += USTRFTIME_DELTA;
	buf = sresize(buf, size, wchar_t);
	ret = (int) wcsftime(buf, size, fmt, timespec);
    } while (ret == 0);

    rdadds(rs, buf+1);
    sfree(buf);
#else
    char *buf = NULL;
    wchar_t *cvtbuf;
    char fmt[4];
    int size, ret;

    fmt[0] = ' ';
    fmt[1] = '%';
    fmt[2] = formatchr;
    fmt[3] = '\0';

    size = 0;
    do {
	size += USTRFTIME_DELTA;
	buf = sresize(buf, size, char);
	ret = (int) strftime(buf, size, fmt, timespec);
    } while (ret == 0);

    cvtbuf = ufroma_locale_dup(buf+1);
    rdadds(rs, cvtbuf);
    sfree(cvtbuf);
    sfree(buf);
#endif
}

wchar_t *ustrftime(const wchar_t *wfmt, const struct tm *timespec)
{
    rdstring rs = { 0, 0, NULL };

    if (!wfmt)
	wfmt = L"%c";

    while (*wfmt) {
	if (wfmt[0] == L'%' && wfmt[1] == L'%') {
	    rdadd(&rs, L'%');
	    wfmt += 2;
	} else if (wfmt[0] == L'%' && wfmt[1]) {
	    ustrftime_internal(&rs, wfmt[1], timespec);
	    wfmt += 2;
	} else {
	    rdadd(&rs, wfmt[0]);
	    wfmt++;
	}
    }

    return rdtrim(&rs);
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

/*
 * Wrapper around charset_from_localenc which accepts the charset
 * name as a wide string (since that happens to be more useful).
 * Also throws a Halibut error and falls back to CS_ASCII if the
 * charset is unrecognised, meaning the rest of the program can
 * rely on always getting a valid charset id back from this
 * function.
 */
int charset_from_ustr(filepos *fpos, const wchar_t *name)
{
    char *csname;
    int charset;

    csname = utoa_dup(name, CS_ASCII);
    charset = charset_from_localenc(csname);

    if (charset == CS_NONE) {
	charset = CS_ASCII;
	error(err_charset, fpos, name);
    }

    sfree(csname);
    return charset;
}
