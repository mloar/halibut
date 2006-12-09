#include <stdio.h>
#include "halibut.h"
#include "paper.h"

static char *pf_read_token(FILE *fp);

void read_pfa_file(input *in) {
    rdstringc rsc = { 0, 0, NULL };
    char *p;
    size_t len;
    char *fontname;
    font_info *fi;
    FILE *fp = in->currfp;
    int c;

    in->pos.line = 0;
    do {
	c = fgetc(fp);
	if (c == EOF) {
	    sfree(rsc.text);
	    error(err_pfeof, &in->pos);
	    return;
	}
	rdaddc(&rsc, c);
    } while (c != 012 && c != 015);
    p = rsc.text;
    if ((p = strchr(p, ':')) == NULL) {
	sfree(rsc.text);
	error(err_pfhead, &in->pos);
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
	    fi->fp = in->currfp;
	    fi->pos = in->pos;
	    fi->length1 = fi->length2 = 0;
	    sfree(fontname);
	    return;
	}
    }
    error(err_pfnoafm, &in->pos, fontname);
    fclose(in->currfp);
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

static long pf_length1(font_info *fi) {
    FILE *fp = fi->fp;
    char *tok;

    rewind(fp);
    tok = pf_read_token(fp);
    while (tok && strcmp(tok, "eexec") != 0) {
	sfree(tok);
	tok = pf_read_token(fp);
    }
    if (tok == NULL) {
	error(err_pfeof, &fi->pos);
	return 0;
    }
    return ftell(fp);
}

/*
 * Return the initial, unencrypted, part of a font.
 */
void pf_part1(font_info *fi, char **bufp, size_t *lenp) {
    FILE *fp = fi->fp;

    if (fi->length1 == 0)
	fi->length1 = pf_length1(fi);
    rewind(fp);
    *bufp = snewn(fi->length1, char);
    *lenp = fi->length1;
    if (fread(*bufp, 1, fi->length1, fp) != (size_t)fi->length1) {
	error(err_pfeof, &fi->pos);
	*lenp = 0;
	sfree(*bufp);
	*bufp = NULL;
    }
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 0xA;
    if (c >= 'a' && c <= 'f') return c - 'a' + 0xa;
    return 0;
}

/*
 * Return the middle, encrypted, part of a font.
 */
void pf_part2(font_info *fi, char **bufp, size_t *lenp) {
    FILE *fp = fi->fp;
    rdstringc rsc = { 0, 0, NULL };
    char *tok, *p;
    unsigned char nybble;
    int havenybble = 0;

    if (fi->length1 == 0)
	fi->length1 = pf_length1(fi);
    fseek(fp, fi->length1, SEEK_SET);
    tok = pf_read_token(fp);
    while (tok && strcmp(tok, "cleartomark") != 0) {
	for (p = tok; *p; p++) {
	    if (pf_isspace(*p)) continue;
	    if (!havenybble)
		nybble = hexval(*p);
	    else
		rdaddc(&rsc, (nybble << 4) | hexval(*p));
	    havenybble = !havenybble;
	}
	sfree(tok);
	tok = pf_read_token(fp);
    }
    if (tok == NULL) {
	error(err_pfeof, &fi->pos);
	*bufp = NULL;
	*lenp = 0;
    }
    *bufp = rsc.text;
    /* Trim off the trailing zeroes */
    if (rsc.pos >= 256)
	*lenp = rsc.pos - 256;
    else {
	error(err_pfbad, &fi->pos);
	*bufp = NULL;
	*lenp = 0;
    }
}

static char *pf_read_litstring(FILE *fp) {
    rdstringc rsc = { 0, 0, NULL };
    int depth = 1;
    int c;

    rdaddc(&rsc, '(');
    do {
	c = fgetc(fp);
	switch (c) {
	  case '(':
	    depth++; break;
	  case ')':
	    depth--; break;
	  case '\\':
	    rdaddc(&rsc, '\\');
	    c = fgetc(fp);
	    break;
	}
	if (c != EOF) rdaddc(&rsc, c);
    } while (depth > 0 && c != EOF);
    return rsc.text;
}

static char *pf_read_hexstring(FILE *fp) {
    rdstringc rsc = { 0, 0, NULL };
    int c;

    rdaddc(&rsc, '<');
    do {
	c = fgetc(fp);
	if (c != EOF) rdaddc(&rsc, c);
    } while (c != '>' && c != EOF);
    return rsc.text;
}   

static char *pf_read_word(FILE *fp, int c) {
    rdstringc rsc = { 0, 0, NULL };

    rdaddc(&rsc, c);
    if (c == '{' || c == '}' || c == '[' || c == ']')
	return rsc.text;
    for (;;) {
	c = fgetc(fp);
	if (pf_isspecial(c) || pf_isspace(c) || c == EOF) break;
	rdaddc(&rsc, c);
    }
    if (pf_isspecial(c)) ungetc(c, fp);
    return rsc.text;
}   

static char *pf_read_token(FILE *fp) {
    int c;

    do {
	c = fgetc(fp);
    } while (pf_isspace(c));
    if (c == EOF) return NULL;
    if (c == '%') {
	do {
	    c = fgetc(fp);
	} while (c != 012 && c != 015);
	return pf_read_token(fp);
    }
    if (c == '(') return pf_read_litstring(fp);
    if (c == '<') return pf_read_hexstring(fp);
    return pf_read_word(fp, c);
}
