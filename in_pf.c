/*
 * PostScript Type 1 font file support for Halibut
 */
/*
 * Type 1 font file formats are specified by Adobe Technical Note
 * #5040: "Supporting Downloadable PostScript Language Fonts".
 * Halibut supports hexadecimal format (section 3.1) and IBM PC format
 * (section 3.3), commonly called PFA and PFB respectively.
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "halibut.h"
#include "paper.h"

#define PFB_ASCII	1
#define PFB_BINARY	2
#define PFB_EOF		3

typedef struct t1_font_Tag t1_font;
typedef struct t1_data_Tag t1_data;

struct t1_font_Tag {
    t1_data *data;
    size_t length1;
    size_t length2;
    filepos pos;
};

struct t1_data_Tag {
    char type;
    size_t length;
    unsigned char *data;
    t1_data *next;
};

typedef struct pfstate_Tag {
    t1_data *data;
    t1_data *curblock;
    size_t offset;
} pfstate;

static void pf_identify(t1_font *tf);

static t1_data *load_pfb_file(FILE *fp, filepos *pos) {
    t1_data *head = NULL, *tail = NULL;
    int c, i;
    char type;

    pos->line = 0;
    for (;;) {
	if (fgetc(fp) != 128) abort();
	type = fgetc(fp);
	if (type == PFB_EOF) return head;
	if (tail) {
	    tail->next = snew(t1_data);
	    tail = tail->next;
	} else {
	    head = snew(t1_data);
	    tail = head;
	}
	tail->type = type;
	tail->length = 0;
	for (i = 0; i < 4; i++) {
	    c = fgetc(fp);
	    if (c == EOF) abort();
	    tail->length |= c << (8 * i);
	}
	tail->data = snewn(tail->length, unsigned char);
	if (fread(tail->data, 1, tail->length, fp) != tail->length) abort();
    }
    tail->next = NULL;
}

static t1_data *load_pfa_file(FILE *fp, filepos *pos) {
    t1_data *ret = snew(t1_data);
    size_t off = 0, len, got;

    pos->line = 0;
    ret->type = PFB_ASCII;
    len = 32768;
    ret->data = snewn(len, unsigned char);
    for (;;) {
	got = fread(ret->data + off, 1, len - off, fp);
	off += got;
	if (off != len) break;
	len *= 2;
	ret->data = sresize(ret->data, len, unsigned char);
    }
    ret->data = sresize(ret->data, off, unsigned char);
    ret->length = off;
    ret->next = NULL;
    return ret;
}

void read_pfa_file(input *in) {
    t1_font *tf = snew(t1_font);

    tf->data = load_pfa_file(in->currfp, &in->pos);
    tf->pos = in->pos;
    tf->length1 = tf->length2 = 0;
    fclose(in->currfp);
    pf_identify(tf);
}

void read_pfb_file(input *in) {
    t1_font *tf = snew(t1_font);

    tf->data = load_pfb_file(in->currfp, &in->pos);
    tf->pos = in->pos;
    tf->length1 = tf->length2 = 0;
    fclose(in->currfp);
    pf_identify(tf);
}
static char *pf_read_token(pfstate *);

/*
 * Read a character from the initial plaintext part of a Type 1 font
 */
static int pf_getc(pfstate *pf) {
    if (pf->offset == pf->curblock->length) {
	if (pf->curblock->next == NULL) return EOF;
	pf->curblock = pf->curblock->next;
	pf->offset = 0;
    }
    if (pf->curblock->type != PFB_ASCII) return EOF;
    return pf->curblock->data[pf->offset++];
}

static void pf_ungetc(int c, pfstate *pf) {
    assert(pf->offset > 0);
    pf->offset--;
    assert(c == pf->curblock->data[pf->offset]);
}

static void pf_rewind(pfstate *pf) {
    pf->curblock = pf->data;
    pf->offset = 0;
}

static void pf_seek(pfstate *pf, size_t off) {
    t1_data *td = pf->data;

    while (td->length < off) {
	off -= td->length;
	td = td->next;
    }
    pf->curblock = td;
    pf->offset = off;
}

static size_t pf_tell(pfstate *pf) {
    t1_data *td = pf->data;
    size_t o = 0;

    while (td != pf->curblock) {
	o += td->length;
	td = td->next;
    }
    return o + pf->offset;
}

static void pf_identify(t1_font *tf) {
    rdstringc rsc = { 0, 0, NULL };
    char *p;
    size_t len;
    char *fontname;
    font_info *fi;
    int c;
    pfstate pfs, *pf = &pfs;

    pf->data = tf->data;
    pf_rewind(pf);
    do {
	c = pf_getc(pf);
	if (c == EOF) {
	    sfree(rsc.text);
	    error(err_pfeof, &tf->pos);
	    return;
	}
	rdaddc(&rsc, c);
    } while (c != 012 && c != 015);
    p = rsc.text;
    if ((p = strchr(p, ':')) == NULL) {
	sfree(rsc.text);
	error(err_pfhead, &tf->pos);
	return;
    }
    p++;
    p += strspn(p, " \t");
    len = strcspn(p, " \t");
    fontname = snewn(len + 1, char);
    memcpy(fontname, p, len);
    fontname[len] = 0;
    sfree(rsc.text);

    for (fi = all_fonts; fi; fi = fi->next) {
	if (strcmp(fi->name, fontname) == 0) {
	    fi->fontfile = tf;
	    fi->filetype = TYPE1;
	    sfree(fontname);
	    return;
	}
    }
    error(err_pfnoafm, &tf->pos, fontname);
    sfree(fontname);
}

/*
 * PostScript white space characters; PLRM3 table 3.1
 */
static int pf_isspace(int c) {
    return c == 000 || c == 011 || c == 012 || c == 014 || c == 015 ||
	c == ' ';
}

/*
 * PostScript special characters; PLRM3 page 27
 */
static int pf_isspecial(int c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
	c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

static size_t pf_findtoken(t1_font *tf, size_t off, char const *needle) {
    char *tok;
    pfstate pfs, *pf = &pfs;

    pf->data = tf->data;
    pf_seek(pf, off);
    for (;;) {
	tok = pf_read_token(pf);
	if (tok == NULL) {
	    if (pf->offset == 0 && pf->curblock->type == PFB_BINARY)
		pf->curblock = pf->curblock->next;
	    else
		return (size_t)-1;
	} else {
	    if (strcmp(tok, needle) == 0) {
		sfree(tok);
		return pf_tell(pf);
	    }
	    sfree(tok);
	}
    }
}

static size_t pf_length1(t1_font *tf) {
    size_t ret;

    ret = pf_findtoken(tf, 0, "eexec");
    if (ret == (size_t)-1) {
	error(err_pfeof, &tf->pos);
	return 0;
    }
    return ret;
}

static size_t pf_length2(t1_font *tf) {
    size_t ret;

    if (tf->length1 == 0)
	tf->length1 = pf_length1(tf);
    ret = pf_findtoken(tf, tf->length1, "cleartomark");
    if (ret == (size_t)-1) {
	error(err_pfeof, &tf->pos);
	return 0;
    }
    return ret - 12 - tf->length1; /* backspace over "cleartomark\n" */
}

static void pf_getascii(t1_font *tf, size_t off, size_t len,
			char **bufp, size_t *lenp) {
    t1_data *td = tf->data;
    size_t blk, i;
    char *p;

    while (td && off >= td->length) {
	off -= td->length;
	td = td->next;
    }
    *bufp = NULL;
    *lenp = 0;
    while (td && len) {
	blk = len < td->length ? len : td->length;
	if (td->type == PFB_ASCII) {
	    *bufp = sresize(*bufp, *lenp + blk, char);
	    memcpy(*bufp + *lenp, td->data + off, blk);
	    *lenp += blk;
	} else {
	    *bufp = sresize(*bufp, *lenp + blk * 2 + blk / 39 + 3, char);
	    p = *bufp + *lenp;
	    for (i = 0; i < blk; i++) {
		if (i % 39 == 0) p += sprintf(p, "\n");
		p += sprintf(p, "%02x", td->data[off + i]);
	    }
	    p += sprintf(p, "\n");
	    *lenp = p - *bufp;
	}
	len -= blk;
	td = td->next;
	off = 0;
    }   
}

void pf_writeps(font_info const *fi, FILE *ofp) {
    char *buf;
    size_t len;

    pf_getascii(fi->fontfile, 0, INT_MAX, &buf, &len);
    fwrite(buf, 1, len, ofp);
    sfree(buf);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 0xA;
    if (c >= 'a' && c <= 'f') return c - 'a' + 0xa;
    return 0;
}

static void pf_getbinary(t1_font *tf, size_t off, size_t len,
			 char **bufp, size_t *lenp) {
    t1_data *td = tf->data;
    size_t blk, i;
    int havenybble = 0;
    char *p, nybble;

    while (td && off >= td->length) {
	off -= td->length;
	td = td->next;
    }
    *bufp = NULL;
    *lenp = 0;
    while (td && len) {
	blk = len < td->length ? len : td->length;
	if (td->type == PFB_BINARY) {
	    *bufp = sresize(*bufp, *lenp + blk, char);
	    memcpy(*bufp + *lenp, td->data + off, blk);
	    *lenp += blk;
	} else {
	    *bufp = sresize(*bufp, *lenp + blk / 2 + 1, char);
	    p = *bufp + *lenp;
	    for (i = 0; i < blk; i++) {
		if (pf_isspace(td->data[off + i])) continue;
		if (!havenybble)
		    nybble = hexval(td->data[off+i]);
		else
		    *p++ = (nybble << 4) | hexval(td->data[off+i]);
		havenybble = !havenybble;
	    }
	    *lenp = p - *bufp;
	}
	len -= blk;
	td = td->next;
	off = 0;
    }   
}


/*
 * Return the initial, unencrypted, part of a font.
 */
void pf_part1(font_info *fi, char **bufp, size_t *lenp) {
    t1_font *tf = fi->fontfile;

    if (tf->length1 == 0)
	tf->length1 = pf_length1(tf);
    pf_getascii(tf, 0, tf->length1, bufp, lenp);
}

/*
 * Return the middle, encrypted, part of a font.
 */
void pf_part2(font_info *fi, char **bufp, size_t *lenp) {
    t1_font *tf = fi->fontfile;

    if (tf->length2 == 0)
	tf->length2 = pf_length2(tf);
    pf_getbinary(tf, tf->length1, tf->length2, bufp, lenp);
    if (*lenp >= 256)
	*lenp -= 256;
}

static char *pf_read_litstring(pfstate *pf) {
    rdstringc rsc = { 0, 0, NULL };
    int depth = 1;
    int c;

    rdaddc(&rsc, '(');
    do {
	c = pf_getc(pf);
	switch (c) {
	  case '(':
	    depth++; break;
	  case ')':
	    depth--; break;
	  case '\\':
	    rdaddc(&rsc, '\\');
	    c = pf_getc(pf);
	    break;
	}
	if (c != EOF) rdaddc(&rsc, c);
    } while (depth > 0 && c != EOF);
    return rsc.text;
}

static char *pf_read_hexstring(pfstate *pf) {
    rdstringc rsc = { 0, 0, NULL };
    int c;

    rdaddc(&rsc, '<');
    do {
	c = pf_getc(pf);
	if (c != EOF) rdaddc(&rsc, c);
    } while (c != '>' && c != EOF);
    return rsc.text;
}   

static char *pf_read_word(pfstate *pf, int c) {
    rdstringc rsc = { 0, 0, NULL };

    rdaddc(&rsc, c);
    if (c == '{' || c == '}' || c == '[' || c == ']')
	return rsc.text;
    for (;;) {
	c = pf_getc(pf);
	if (pf_isspecial(c) || pf_isspace(c) || c == EOF) break;
	rdaddc(&rsc, c);
    }
    if (pf_isspecial(c)) pf_ungetc(c, pf);
    return rsc.text;
}   

static char *pf_read_token(pfstate *pf) {
    int c;

    do {
	c = pf_getc(pf);
    } while (pf_isspace(c));
    if (c == EOF) return NULL;
    if (c == '%') {
	do {
	    c = pf_getc(pf);
	} while (c != 012 && c != 015);
	return pf_read_token(pf);
    }
    if (c == '(') return pf_read_litstring(pf);
    if (c == '<') return pf_read_hexstring(pf);
    return pf_read_word(pf, c);
}
