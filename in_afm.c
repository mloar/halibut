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
    size_t i;

    fi = snew(font_info);
    fi->name = NULL;
    fi->widths = newtree234(width_cmp);
    fi->fontfile = NULL;
    fi->kerns = newtree234(kern_cmp);
    fi->ligs = newtree234(lig_cmp);
    fi->fontbbox[0] = fi->fontbbox[1] = fi->fontbbox[2] = fi->fontbbox[3] = 0;
    fi->capheight = fi->xheight = fi->ascent = fi->descent = 0;
    fi->stemh = fi->stemv = fi->italicangle = 0;
    for (i = 0; i < lenof(fi->bmp); i++)
	    fi->bmp[i] = 0xFFFF;
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
	} else if (strcmp(key, "FontBBox") == 0) {
	    int i;
	    for (i = 0; i < 3; i++) {
		if (!(val = strtok(NULL, " \t"))) {
		    error(err_afmval, &in->pos, key, 4);
		    goto giveup;
		}
		fi->fontbbox[i] = atof(val);
	    }
	} else if (strcmp(key, "CapHeight") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->capheight = atof(val);
	} else if (strcmp(key, "XHeight") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->xheight = atof(val);
	} else if (strcmp(key, "Ascender") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->ascent = atof(val);
	} else if (strcmp(key, "Descender") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->descent = atof(val);
	} else if (strcmp(key, "CapHeight") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->capheight = atof(val);
	} else if (strcmp(key, "StdHW") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->stemh = atof(val);
	} else if (strcmp(key, "StdVW") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->stemv = atof(val);
	} else if (strcmp(key, "ItalicAngle") == 0) {
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    fi->italicangle = atof(val);
	} else if (strcmp(key, "StartCharMetrics") == 0) {
	    int nglyphs, i;
	    if (!(val = strtok(NULL, " \t"))) {
		error(err_afmval, &in->pos, key, 1);
		goto giveup;
	    }
	    nglyphs = atoi(val);
	    sfree(line);
	    for (i = 0; i < nglyphs; i++) {
		int width = 0;
		glyph g = NOGLYPH;

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
			width = atoi(val);
		    } else if (strcmp(key, "N") == 0) {
			if (!(val = strtok(NULL, " \t")) ||
			    !strcmp(val, ";")) {
			    error(err_afmval, &in->pos, key, 1);
			    goto giveup;
			}
			g = glyph_intern(val);
		    } else if (strcmp(key, "L") == 0) {
			glyph succ, lig;
			if (!(val = strtok(NULL, " \t")) ||
			    !strcmp(val, ";")) {
			    error(err_afmval, &in->pos, key, 1);
			    goto giveup;
			}
			succ = glyph_intern(val);
			if (!(val = strtok(NULL, " \t")) ||
			    !strcmp(val, ";")) {
			    error(err_afmval, &in->pos, key, 1);
			    goto giveup;
			}
			lig = glyph_intern(val);
			if (g != NOGLYPH && succ != NOGLYPH &&
			    lig != NOGLYPH) {
			    ligature *l = snew(ligature);
			    l->left = g;
			    l->right = succ;
			    l->lig = lig;
			    add234(fi->ligs, l);
			}
		    }
		    do {
			key = strtok(NULL, " \t");
		    } while (key && strcmp(key, ";"));
		    key = strtok(NULL, " \t");
		}
		sfree(line);
		if (width != 0 && g != NOGLYPH) {
		    wchar_t ucs;
		    glyph_width *w = snew(glyph_width);
		    w->glyph = g;
		    w->width = width;
		    add234(fi->widths, w);
		    ucs = ps_glyph_to_unicode(g);
		    if (ucs < 0xFFFF)
			fi->bmp[ucs] = g;
		}
	    }
	    line = afm_read_line(in);
	    if (!line || !afm_require_key(line, "EndCharMetrics", in))
		goto giveup;
	    sfree(line);

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
		    l = glyph_intern(nl);
		    r = glyph_intern(nr);
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
