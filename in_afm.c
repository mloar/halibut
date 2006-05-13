#include <stdio.h>
#include <stdlib.h>
#include "halibut.h"
#include "paper.h"

char *afm_read_line(input *in) {
    int i, len = 256;
    int c;
    char *line;

    do {
	i = 0;
	in->pos.line++;
	c = getc(in->currfp);
	if (c == EOF) {
	    error(err_afmeof, &in->pos);
	    return NULL;
	}
	line = snewn(len, char);
	while (c != EOF && c != '\r' && c != '\n') {
	    if (i >= len - 1) {
		len += 256;
		line = sresize(line, len, char);
	    }
	    line[i++] = c;
	    c = getc(in->currfp);
	}
	if (c == '\r') {
	    /* Cope with CRLF terminated lines */
	    c = getc(in->currfp);
	    if (c != '\n' && c != EOF)
		ungetc(c, in->currfp);
	}
	line[i] = 0;
    } while (line[(strspn(line, " \t"))] == 0 ||
	     strncmp(line, "Comment ", 8) == 0 ||
	     strncmp(line, "Comment\t", 8) == 0);

    return line;
}

static int afm_require_key(char *line, char const *expected, input *in) {
    char *key = strtok(line, " \t");

    if (strcmp(key, expected) == 0)
	return TRUE;
    error(err_afmkey, &in->pos, expected);
    return FALSE;
}

void read_afm_file(input *in) {
    char *line, *key, *val;
    font_info *fi;

    fi = snew(font_info);
    fi->name = NULL;
    fi->nglyphs = 0;
    fi->glyphs = NULL;
    fi->widths = NULL;
    fi->kerns = newtree234(kern_cmp);
    in->pos.line = 0;
    line = afm_read_line(in);
    if (!line || !afm_require_key(line, "StartFontMetrics", in))
	goto giveup;
    if (!(val = strtok(NULL, " \t"))) {
	error(err_afmval, in->pos, "StartFontMetrics", 1);
	goto giveup;
    }
    if (atof(val) >= 5.0) {
	error(err_afmvers, &in->pos);
	goto giveup;
    }
    sfree(line);
    for (;;) {
	line = afm_read_line(in);
	if (line == NULL)
	    goto giveup;
	key = strtok(line, " \t");
	if (strcmp(key, "EndFontMetrics") == 0) {
	    fi->next = all_fonts;
	    all_fonts = fi;
	    fclose(in->currfp);
	    return;
	} else if (strcmp(key, "FontName") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->name = dupstr(val);
	} else if (strcmp(key, "StartCharMetrics") == 0) {
	    char const **glyphs;
	    int *widths;
	    int i;
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->nglyphs = atoi(val);
	    sfree(line);
	    glyphs = snewn(fi->nglyphs, char const *);
	    widths = snewn(fi->nglyphs, int);
	    for (i = 0; i < fi->nglyphs; i++) {
		glyphs[i] = NULL;
		line = afm_read_line(in);
		if (line == NULL)
		    goto giveup;
		key = strtok(line, " \t");
		while (key != NULL) {
		    if (strcmp(key, "WX") == 0 || strcmp(key, "W0X") == 0) {
			if (!(val = strtok(NULL, " \t")) ||
			    !strcmp(val, ";")) {
			    error(err_afmval, &in->pos, key, 1);
			    goto giveup;
			}
			widths[i] = atoi(val);
		    } else if (strcmp(key, "N") == 0) {
			if (!(val = strtok(NULL, " \t")) ||
			    !strcmp(val, ";")) {
			    error(err_afmval, &in->pos, key, 1);
			    goto giveup;
			}
			glyphs[i] = dupstr(val);
		    }
		    do {
			key = strtok(NULL, " \t");
		    } while (key && strcmp(key, ";"));
		    key = strtok(NULL, " \t");
		}
		sfree(line);
	    }
	    line = afm_read_line(in);
	    if (!line || !afm_require_key(line, "EndCharMetrics", in))
		goto giveup;
	    sfree(line);
	    fi->glyphs = glyphs;
	    fi->widths = widths;

	    for (i = 0; i < fi->nglyphs; i++) {
		wchar_t ucs;
		ucs = ps_glyph_to_unicode(fi->glyphs[i]);
		if (ucs < 0xFFFF)
		    fi->bmp[ucs] = i;
	    }
	    font_index_glyphs(fi);
	} else if (strcmp(key, "StartKernPairs") == 0 ||
		   strcmp(key, "StartKernPairs0") == 0) {
	    int nkerns, i;
	    kern_pair *kerns;
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    nkerns = atoi(val);
	    sfree(line);
	    kerns = snewn(nkerns, kern_pair);
	    for (i = 0; i < nkerns; i++) {
		line = afm_read_line(in);
		if (line == NULL)
		    goto giveup;
		key = strtok(line, " \t");
		if (strcmp(key, "KPX") == 0) {
		    char *nl, *nr;
		    int l, r;
		    kern_pair *kp;
		    nl = strtok(NULL, " \t");
		    nr = strtok(NULL, " \t");
		    val = strtok(NULL, " \t");
		    if (!val) {
			error(err_afmval, &in->pos, key, 3);
			goto giveup;
		    }
		    l = find_glyph(fi, nl);
		    r = find_glyph(fi, nr);
		    if (l == -1 || r == -1) continue;
		    kp = snew(kern_pair);
		    kp->left = l;
		    kp->right = r;
		    kp->kern = atoi(val);
		    add234(fi->kerns, kp);
		}
	    }
	    line = afm_read_line(in);
	    if (!line || !afm_require_key(line, "EndKernPairs", in))
		goto giveup;
	    sfree(line);
	}
    }
  giveup:
    sfree(fi);
    fclose(in->currfp);
    return;
}
