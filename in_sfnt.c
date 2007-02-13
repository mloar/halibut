/*
 * Support for sfnt-housed fonts for Halibut
 *
 * sfnt-housed fonts include TrueType, OpenType, sfnt-housed Type 1
 * fonts and a couple of bitmap formats.
 *
 * The various tables that can appear in sfnt-housed fonts are defined
 * in several places.  These include:
 *
 * The OpenType Specification:
 * <http://partners.adobe.com/public/developer/opentype/index_spec.html>
 *
 * The TrueType Reference Manual:
 * <http://developer.apple.com/textfonts/TTRefMan/>
 *
 * Microsoft typography specifications:
 * <http://www.microsoft.com/typography/SpecificationsOverview.mspx>
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "halibut.h"
#include "paper.h"

typedef struct sfnt_decode_Tag sfnt_decode;
struct sfnt_decode_Tag {
    void (*decoder)(void *src, void *dest);
    size_t src_len;
    size_t dest_offset;
};

#if 0 /* unused */
static void decode_uint8(void *src, void *dest) {
    *(unsigned int *)dest = *(unsigned char *)src;
}
#define d_uint8 decode_uint8, 1
#endif

#if 0 /* unused */
static void decode_int8(void *src, void *dest) {
    *(int *)dest = *(signed char *)src;
}
#define d_int8 decode_int8, 1
#endif

static void decode_uint16(void *src, void *dest) {
    unsigned char *cp = src;
    *(unsigned int *)dest = (cp[0] << 8)  + cp[1];
}
#define d_uint16 decode_uint16, 2

static void decode_int16(void *src, void *dest) {
    signed char *cp = src;
    unsigned char *ucp = src;
    *(int *)dest = (cp[0] << 8)  + ucp[1];
}
#define d_int16 decode_int16, 2

static void decode_uint32(void *src, void *dest) {
    unsigned char *cp = src;
    *(unsigned int *)dest =
	(cp[0] << 24) + (cp[1] << 16) + (cp[2] << 8)  + cp[3];
}
#define d_uint32 decode_uint32, 4

static void decode_int32(void *src, void *dest) {
    signed char *cp = src;
    unsigned char *ucp = src;
    *(int *)dest = (cp[0] << 24) + (ucp[1] << 16) + (ucp[2] << 8)  + ucp[3];
}
#define d_int32 decode_int32, 4

static void decode_skip(void *src, void *dest) {
    IGNORE(src);
    IGNORE(dest);
    /* do nothing */
}
#define d_skip(n) decode_skip, (n), 0

static void decode_end(void *src, void *dest) {
    IGNORE(src);
    IGNORE(dest);
    /* never called */
}
#define d_end decode_end, 0, 0

static void *decode(sfnt_decode *dec, void *src, void *end, void *dest) {
    while (dec->decoder != decode_end) {
	if ((char *)src + dec->src_len > (char *)end) return NULL;
	dec->decoder(src, (char *)dest + dec->dest_offset);
	src = (char *)src + dec->src_len;
	dec++;
    }
    return src;
}

static void *decoden(sfnt_decode *dec, void *src, void *end, void *dest,
		     size_t size, size_t n) {
    while (n-- && src) {
	src = decode(dec, src, end, dest);
	dest = (char *)dest + size;
    }
    return src;
}

/* Decoding specs for simple data types */
sfnt_decode uint16_decode[] = { { d_uint16, 0 }, { d_end } };
sfnt_decode int16_decode[]  = { { d_int16,  0 }, { d_end } };
sfnt_decode uint32_decode[] = { { d_uint32, 0 }, { d_end } };

/* Offset subdirectory -- the start of the file */
typedef struct offsubdir_Tag offsubdir;
struct offsubdir_Tag {
    unsigned scaler_type;
    unsigned numTables;
};
sfnt_decode offsubdir_decode[] = {
    { d_uint32,	offsetof(offsubdir, scaler_type) },
    { d_uint16, offsetof(offsubdir, numTables) },
    { d_skip(6) },
    { d_end }
};

#define sfnt_00010000	0x00010000
#define TAG_OS_2	0x4f532f32
#define TAG_cmap	0x636d6170
#define TAG_glyf	0x676c7966
#define TAG_head	0x68656164
#define TAG_hhea	0x68686561
#define TAG_hmtx	0x686d7478
#define TAG_kern	0x6b65726e
#define TAG_loca	0x6c6f6361
#define TAG_maxp	0x6d617870
#define TAG_name	0x6e616d65
#define TAG_post	0x706f7374
#define sfnt_true	0x74727565

/* Table directory */
typedef struct tabledir_Tag tabledir;
struct tabledir_Tag {
    unsigned tag;
    unsigned checkSum;
    unsigned offset;
    unsigned length;
};
sfnt_decode tabledir_decode[] = {
    { d_uint32,	offsetof(tabledir, tag) },
    { d_uint32, offsetof(tabledir, checkSum) },
    { d_uint32, offsetof(tabledir, offset) },
    { d_uint32, offsetof(tabledir, length) },
    { d_end }
};

/* OS/2 and Windows compatibility table */
typedef struct t_OS_2_Tag t_OS_2;
struct t_OS_2_Tag {
    unsigned version;
    int sTypoAscender, sTypoDescender;
    int sxHeight, sCapHeight;
};
sfnt_decode t_OS_2_v0_decode[] = {
    { d_uint16, offsetof(t_OS_2, version) },
    { d_skip(66) }, /* xAvgCharWidth, usWeightClass, usWidthClass, fsType, */
    /* ySubscriptXSize, ySubscriptYSize, ySubscriptXOffset, */
    /* ySubscriptYOffset, ySuperscriptXSize, ySuperscriptYSize, */
    /* ySuperscriptXOffset, ySupercriptYOffset, sFamilyClass, panose, */
    /* ulUnicodeRange1, ulUnicodeRange2, ulUnicodeRange3, ulUnicodeRange4, */
    /* achVendID, fsSelection, usFirstCharIndex, usLastCharIndex */
    { d_end }
};
sfnt_decode t_OS_2_v1_decode[] = {
    { d_uint16, offsetof(t_OS_2, version) },
    { d_skip(66) }, /* xAvgCharWidth, usWeightClass, usWidthClass, fsType, */
    /* ySubscriptXSize, ySubscriptYSize, ySubscriptXOffset, */
    /* ySubscriptYOffset, ySuperscriptXSize, ySuperscriptYSize, */
    /* ySuperscriptXOffset, ySupercriptYOffset, sFamilyClass, panose, */
    /* ulUnicodeRange1, ulUnicodeRange2, ulUnicodeRange3, ulUnicodeRange4, */
    /* achVendID, fsSelection, usFirstCharIndex, usLastCharIndex */
    { d_int16, offsetof(t_OS_2, sTypoAscender) },
    { d_int16, offsetof(t_OS_2, sTypoDescender) },
    { d_skip(14) }, /* sTypoLineGap, usWinAscent, usWinDescent, */
    /* ulCodePageRange1, ulCodePageRange2 */
    { d_end }
};
sfnt_decode t_OS_2_v2_decode[] = {
    { d_uint16, offsetof(t_OS_2, version) },
    { d_skip(66) }, /* xAvgCharWidth, usWeightClass, usWidthClass, fsType, */
    /* ySubscriptXSize, ySubscriptYSize, ySubscriptXOffset, */
    /* ySubscriptYOffset, ySuperscriptXSize, ySuperscriptYSize, */
    /* ySuperscriptXOffset, ySupercriptYOffset, sFamilyClass, panose, */
    /* ulUnicodeRange1, ulUnicodeRange2, ulUnicodeRange3, ulUnicodeRange4, */
    /* achVendID, fsSelection, usFirstCharIndex, usLastCharIndex */
    { d_int16, offsetof(t_OS_2, sTypoAscender) },
    { d_int16, offsetof(t_OS_2, sTypoDescender) },
    { d_skip(14) }, /* sTypoLineGap, usWinAscent, usWinDescent, */
    /* ulCodePageRange1, ulCodePageRange2 */
    { d_int16, offsetof(t_OS_2, sxHeight) },
    { d_int16, offsetof(t_OS_2, sCapHeight) },
    { d_skip(6) }, /* usDefaultChar, usBreakChar, usMaxContext */
    { d_end }
};

/* Character to Glyph ('cmap') table */
typedef struct t_cmap_Tag t_cmap;
struct t_cmap_Tag {
    unsigned numTables;
};
sfnt_decode t_cmap_decode[] = {
    { d_skip(2) },
    { d_uint16, offsetof(t_cmap, numTables) },
    { d_end }
};
typedef struct encodingrec_Tag encodingrec;
struct encodingrec_Tag {
    unsigned platformID;
    unsigned encodingID;
    unsigned offset;
};
sfnt_decode encodingrec_decode[] = {
    { d_uint16, offsetof(encodingrec, platformID) },
    { d_uint16, offsetof(encodingrec, encodingID) },
    { d_uint32, offsetof(encodingrec, offset) },
    { d_end }
};
typedef struct cmap4_Tag cmap4;
struct cmap4_Tag {
    unsigned length;
    unsigned segCountX2;
};
sfnt_decode cmap4_decode[] = {
    { d_skip(2) }, /* format */
    { d_uint16, offsetof(cmap4, length) },
    { d_skip(2) }, /* language */
    { d_uint16, offsetof(cmap4, segCountX2) },
    { d_skip(6) }, /* searchRange, entrySelector, rangeShift */
    { d_end }
};

/* Font Header ('head') table */
typedef struct t_head_Tag t_head;
struct t_head_Tag {
    unsigned version;
    unsigned fontRevision;
    unsigned flags;
    unsigned unitsPerEm;
    int xMin, yMin, xMax, yMax;
    int indexToLocFormat;
};
sfnt_decode t_head_decode[] = {
    { d_uint32, offsetof(t_head, version) },
    { d_uint32, offsetof(t_head, fontRevision) },
    { d_skip(8) }, /* checkSumAdjustment, magicNumber, flags */
    { d_uint16, offsetof(t_head, flags) },
    { d_uint16, offsetof(t_head, unitsPerEm) },
    { d_skip(16) }, /* created, modified */
    { d_int16, offsetof(t_head, xMin) },
    { d_int16, offsetof(t_head, yMin) },
    { d_int16, offsetof(t_head, xMax) },
    { d_int16, offsetof(t_head, yMax) },
    { d_skip(6) }, /* macStyle, lowestRecPPEM, fontDirectionHint */
    { d_int16, offsetof(t_head, indexToLocFormat) },
    { d_skip(2) },
    { d_end }
};

/* Horizontal Header ('hhea') table */
typedef struct t_hhea_Tag t_hhea;
struct t_hhea_Tag {
    unsigned version;
    int ascent;
    int descent;
    int lineGap;
    int metricDataFormat;
    unsigned numOfLongHorMetrics;
};
sfnt_decode t_hhea_decode[] = {
    { d_uint32, offsetof(t_hhea, version) },
    { d_int16,	offsetof(t_hhea, ascent) },
    { d_int16,	offsetof(t_hhea, descent) },
    { d_int16,	offsetof(t_hhea, lineGap) },
    { d_skip(22) },
    { d_int16,	offsetof(t_hhea, metricDataFormat) },
    { d_uint16,	offsetof(t_hhea, numOfLongHorMetrics) },
    { d_end }
};

/* Horizontal Metrics ('hmtx') table */
sfnt_decode longhormetric_decode[] = {
    { d_uint16, 0 },
    { d_skip(2) },
    { d_end }
};

/* Kerning ('kern') table */
typedef struct t_kern_Tag t_kern;
struct t_kern_Tag {
    unsigned version;
    unsigned nTables;
};
sfnt_decode t_kern_v0_decode[] = {
    { d_uint16, offsetof(t_kern, version) },
    { d_uint16, offsetof(t_kern, nTables) },
    { d_end }
};
typedef struct kern_v0_subhdr_Tag kern_v0_subhdr;
struct kern_v0_subhdr_Tag {
    unsigned version;
    unsigned length;
    unsigned coverage;
};
sfnt_decode kern_v0_subhdr_decode[] = {
    { d_uint16, offsetof(kern_v0_subhdr, version) },
    { d_uint16, offsetof(kern_v0_subhdr, length) },
    { d_uint16, offsetof(kern_v0_subhdr, coverage) },
    { d_end }
};
#define KERN_V0_HORIZ		0x0001
#define KERN_V0_MINIMUM		0x0002
#define KERN_V0_CROSSSTREAM	0x0004
#define KERN_V0_OVERRIDE	0x0008
#define KERN_V0_FORMAT		0xff00
#define KERN_V0_FORMAT_0	0x0000
sfnt_decode t_kern_v1_decode[] = {
    { d_uint32, offsetof(t_kern, version) },
    { d_uint32, offsetof(t_kern, nTables) },
    { d_end }
};
typedef struct kern_v1_subhdr_Tag kern_v1_subhdr;
struct kern_v1_subhdr_Tag {
    unsigned length;
    unsigned coverage;
};
sfnt_decode kern_v1_subhdr_decode[] = {
    { d_uint32, offsetof(kern_v1_subhdr, length) },
    { d_uint16, offsetof(kern_v1_subhdr, coverage) },
    { d_skip(2) }, /* tupleIndex */
    { d_end }
};
#define KERN_V1_VERTICAL	0x8000
#define KERN_V1_CROSSSTREAM	0x4000
#define KERN_V1_VARIATION	0x2000
#define KERN_V1_FORMAT		0x00ff
#define KERN_V1_FORMAT_0	0x0000
typedef struct kern_f0_Tag kern_f0;
struct kern_f0_Tag {
    unsigned nPairs;
};
sfnt_decode kern_f0_decode[] = {
    { d_uint16, offsetof(kern_f0, nPairs) },
    { d_skip(6) }, /* searchRange, entrySelector, rangeShift */
    { d_end }
};
typedef struct kern_f0_pair_Tag kern_f0_pair;
struct kern_f0_pair_Tag {
    unsigned left;
    unsigned right;
    int value;
};
sfnt_decode kern_f0_pair_decode[] = {
    { d_uint16, offsetof(kern_f0_pair, left) },
    { d_uint16, offsetof(kern_f0_pair, right) },
    { d_int16, offsetof(kern_f0_pair, value) },
    { d_end }
};

/* Maximum profile ('maxp') table */
typedef struct t_maxp_Tag t_maxp;
struct t_maxp_Tag {
    unsigned version;
    unsigned numGlyphs;
};
sfnt_decode t_maxp_decode[] = {
    { d_uint32, offsetof(t_maxp, version) },
    { d_uint16, offsetof(t_maxp, numGlyphs) },
    { d_end }
};

/* Naming ('name') table  */
typedef struct t_name_Tag t_name;
typedef struct namerecord_Tag namerecord;
struct t_name_Tag {
    unsigned format;
    unsigned count;
    unsigned stringOffset;
    namerecord *nameRecord;
};
sfnt_decode t_name_decode[] = {
    { d_uint16,	offsetof(t_name, format) },
    { d_uint16,	offsetof(t_name, count) },
    { d_uint16,	offsetof(t_name, stringOffset) },
    { d_end }
};
struct namerecord_Tag {
    unsigned platformID;
    unsigned encodingID;
    unsigned languageID;
    unsigned nameID;
    unsigned length;
    unsigned offset;
};
sfnt_decode namerecord_decode[] = {
    { d_uint16, offsetof(namerecord, platformID) },
    { d_uint16, offsetof(namerecord, encodingID) },
    { d_uint16, offsetof(namerecord, languageID) },
    { d_uint16, offsetof(namerecord, nameID) },
    { d_uint16, offsetof(namerecord, length) },
    { d_uint16, offsetof(namerecord, offset) },
    { d_end }
};

/* PostScript compatibility ('post') table */
typedef struct t_post_Tag t_post;
struct t_post_Tag {
    unsigned format;
    int italicAngle;
    int underlinePosition;
    int underlineThickness;
    unsigned isFixedPitch;
    unsigned minMemType42;
    unsigned maxMemType42;
};
sfnt_decode t_post_decode[] = {
    { d_uint32, offsetof(t_post, format) },
    { d_int32,  offsetof(t_post, italicAngle) },
    { d_int16,	offsetof(t_post, underlinePosition) },
    { d_int16,	offsetof(t_post, underlineThickness) },
    { d_uint32,	offsetof(t_post, isFixedPitch) },
    { d_uint32, offsetof(t_post, minMemType42) },
    { d_uint32, offsetof(t_post, maxMemType42) },
    { d_skip(8) }, /* minMemType1, maxMemType1 */
    { d_end }
};

typedef struct {
    glyph name;
    unsigned short index;
} glyphmap;

struct sfnt_Tag {
    void *data;
    size_t len;
    void *end;
    filepos pos;
    offsubdir osd;
    tabledir *td;
    t_head head;
    unsigned nglyphs;
    glyph *glyphsbyindex;
    unsigned short *glyphsbyname;
    unsigned minmem, maxmem;
};

static int sfnt_findtable(sfnt *sf, unsigned tag,
			  void **startp, void **endp) {
    size_t i;

    for (i = 0; i < sf->osd.numTables; i++) {
	if (sf->td[i].tag == tag) {
	    *startp = (char *)sf->data + sf->td[i].offset;
	    *endp = (char *)*startp + sf->td[i].length;
	    return TRUE;
	}
    }
    return FALSE;
}

static char *sfnt_psname(font_info *fi) {
    sfnt *sf = fi->fontfile;
    t_name name;
    void *ptr, *end;
    size_t i;
    char *psname;
    namerecord *nr;

    if (!sfnt_findtable(sf, TAG_name, &ptr, &end)) {
	error(err_sfntnotable, &sf->pos, "name");
	return NULL;
    }
    ptr = decode(t_name_decode, ptr, end, &name);
    name.nameRecord = snewn(name.count, namerecord);
    ptr = decoden(namerecord_decode, ptr, sf->end, name.nameRecord,
		  sizeof(*name.nameRecord), name.count);
    for (i = 0; i < name.count; i++) {
	nr = name.nameRecord + i;
	if (nr->nameID == 6) {
	    /* PostScript name, but can we make sense of it? */
	    if (nr->platformID == 1 && nr->encodingID == 0) {
		/* Mac Roman, which is ASCII for our purposes */
		psname = snewn(nr->length + 1, char);
		memcpy(psname, (char *)ptr + nr->offset, nr->length);
		psname[nr->length] = 0;
		sfree(name.nameRecord);
		return psname;
	    }
	}
    }
    error(err_sfntnopsname, &sf->pos);
    return NULL;
}

static unsigned short *cmp_glyphsbyindex;
static int glyphsbyname_cmp(void const *a, void const *b) {
    glyph ga = cmp_glyphsbyindex[*(unsigned short *)a];
    glyph gb = cmp_glyphsbyindex[*(unsigned short *)b];
    if (ga < gb) return -1;
    if (ga > gb) return 1;
    /* For de-duping, we'd prefer to have the first glyph stay first */
    if (*(unsigned short *)a < *(unsigned short *)b) return -1;
    if (*(unsigned short *)a > *(unsigned short *)b) return 1;
    return 0;
}
static int glyphsbyname_cmp_search(void const *a, void const *b) {
    glyph ga = *(glyph *)a;
    glyph gb = cmp_glyphsbyindex[*(unsigned short *)b];
    if (ga < gb) return -1;
    if (ga > gb) return 1;
    return 0;
}

/* Generate an name for a glyph that doesn't have one. */
static glyph genglyph(unsigned idx) {
    char buf[11];
    if (idx == 0) return glyph_intern(".notdef");
    sprintf(buf, "glyph%u", idx);
    return glyph_intern(buf);
}

/*
 * Extract data from the 'post' table (mostly glyph mappings)
 *
 * TODO: cope better with duplicated glyph names (usually .notdef)
 * TODO: when presented with format 3.0, try to use 'CFF' if present.
 */
static void sfnt_mapglyphs(font_info *fi) {
    sfnt *sf = fi->fontfile;
    t_post post;
    void *ptr, *end;
    unsigned char *sptr;
    char tmp[256];
    glyph *extraglyphs, prev, this;
    unsigned nextras, i, g, suflen;

    sf->glyphsbyname = sf->glyphsbyindex = NULL;
    if (sfnt_findtable(sf, TAG_post, &ptr, &end)) {
	ptr = decode(t_post_decode, ptr, end, &post);
	if (ptr == NULL) {
	    error(err_sfntbadtable, &sf->pos, "post");
	    goto noglyphs;
	}
    
	sf->minmem = post.minMemType42;
	sf->maxmem = post.maxMemType42;
	fi->italicangle = post.italicAngle / 65536.0;
	switch (post.format) {
	  case 0x00010000:
	    if (sf->nglyphs != 258) {
		error(err_sfntbadtable, &sf->pos, "post");
		break;
	    }
	    sf->glyphsbyindex = (glyph *)tt_std_glyphs;
	    break;
	  case 0x00020000:
	    if ((char *)ptr + 2 > (char *)end) {
		error(err_sfntbadtable, &sf->pos, "post");
		break;
	    }
	    ptr = (char *)ptr + 2;
	    if ((char *)ptr + 2*sf->nglyphs > (char *)end) {
		error(err_sfntbadtable, &sf->pos, "post");
		break;
	    }
	    nextras = 0;
	    for (sptr = (unsigned char *)ptr + 2*sf->nglyphs;
		 sptr < (unsigned char *)end;
		 sptr += *sptr+1)
		nextras++;
	    extraglyphs = snewn(nextras, glyph);
	    i = 0;
	    for (sptr = (unsigned char *)ptr + 2*sf->nglyphs;
		 sptr < (unsigned char *)end;
		 sptr += *sptr+1) {
		memcpy(tmp, sptr + 1, *sptr);
		tmp[*sptr] = 0;
		assert(i < nextras);
		extraglyphs[i++] = glyph_intern(tmp);
	    }
	    sf->glyphsbyindex = snewn(sf->nglyphs, glyph);
	    for (i = 0; i < sf->nglyphs; i++) {
		decode_uint16((char *)ptr + 2*i, &g);
		if (g <= 257)
		    sf->glyphsbyindex[i] = tt_std_glyphs[g];
		else if (g < 258 + nextras)
		    sf->glyphsbyindex[i] = extraglyphs[g - 258];
		else {
		    error(err_sfntbadtable, &sf->pos, "post");
		    sf->glyphsbyindex[i] = genglyph(i);
		}
	    }
	    sfree(extraglyphs);
	    break;
	  case 0x00030000:
	    break;
	  default:
	    error(err_sfnttablevers, &sf->pos, "post");
	    break;
	}
    }
  noglyphs:
    if (!sf->glyphsbyindex) {
	sf->glyphsbyindex = snewn(sf->nglyphs, glyph);
	for (i = 0; i < sf->nglyphs; i++)
	    sf->glyphsbyindex[i] = genglyph(i);
    }
    /* Construct glyphsbyname */
    sf->glyphsbyname = snewn(sf->nglyphs, unsigned short);
    for (i = 0; i < sf->nglyphs; i++)
	sf->glyphsbyname[i] = i;
    cmp_glyphsbyindex = sf->glyphsbyindex;
    qsort(sf->glyphsbyname, sf->nglyphs, sizeof(*sf->glyphsbyname),
	  glyphsbyname_cmp);
    /*
     * It's possible for fonts to specify the same name for multiple
     * glyphs, which would make one of them inaccessible.  Check for
     * that, and rename all but one of each set.
     *
     * To ensure that we don't clash with any existing glyph names,
     * our renaming involves appending the glyph number formatted with
     * enough leading zeroes to make it longer than any all-digit
     * suffix that already exists in the font.
     */
    suflen = 4;
    for (i = 0; i < sf->nglyphs; i++) {
	char const *p;
	p = strrchr(glyph_extern(sfnt_indextoglyph(sf, i)), '.');
	if (p && !(p+1)[strspn(p+1, "0123456789")] && strlen(p+1) > suflen)
	    suflen = strlen(p+1);
    }
    suflen++;
    prev = sfnt_indextoglyph(sf, sf->glyphsbyname[0]);
    for (i = 1; i < sf->nglyphs; i++) {
	if (prev == (this = sfnt_indextoglyph(sf, sf->glyphsbyname[i]))) {
	    char const *basename;
	    char *buf;
	    basename = glyph_extern(this);
	    buf = snewn(strlen(basename) + 2 + suflen, char);
	    strcpy(buf, basename);
	    sprintf(buf + strlen(basename), ".%0*hu", suflen,
		    sf->glyphsbyname[i]);
	    sf->glyphsbyindex[sf->glyphsbyname[i]] = glyph_intern(buf);
	    sfree(buf);
	}
	prev = this;
    }
    /* We may have renamed some glyphs, so re-sort the array. */
    qsort(sf->glyphsbyname, sf->nglyphs, sizeof(*sf->glyphsbyname),
	  glyphsbyname_cmp);
}

glyph sfnt_indextoglyph(sfnt *sf, unsigned idx) {
    return sf->glyphsbyindex[idx];
}

unsigned sfnt_nglyphs(sfnt *sf) {
    return sf->nglyphs;
}

unsigned sfnt_glyphtoindex(sfnt *sf, glyph g) {
    cmp_glyphsbyindex = sf->glyphsbyindex;
    return *(unsigned short *)bsearch(&g, sf->glyphsbyname, sf->nglyphs,
				      sizeof(*sf->glyphsbyname),
				      glyphsbyname_cmp_search);
}

/*
 * Get data from 'hhea', 'hmtx', and 'OS/2' tables
 */
void sfnt_getmetrics(font_info *fi) {
    sfnt *sf = fi->fontfile;
    t_hhea hhea;
    t_OS_2 OS_2;
    void *ptr, *end;
    unsigned i, j;
    unsigned *hmtx;

    /* First, the bounding box from the 'head' table. */
    fi->fontbbox[0] = sf->head.xMin * FUNITS_PER_PT /  sf->head.unitsPerEm;
    fi->fontbbox[1] = sf->head.yMin * FUNITS_PER_PT /  sf->head.unitsPerEm;
    fi->fontbbox[2] = sf->head.xMax * FUNITS_PER_PT /  sf->head.unitsPerEm;
    fi->fontbbox[3] = sf->head.yMax * FUNITS_PER_PT /  sf->head.unitsPerEm;
    if (!sfnt_findtable(sf, TAG_hhea, &ptr, &end)) {
	error(err_sfntnotable, &sf->pos, "hhea");
	return;
    }
    if (decode(t_hhea_decode, ptr, end, &hhea) == NULL) {
	error(err_sfntbadtable, &sf->pos, "hhea");
	return;
    }
    if ((hhea.version & 0xffff0000) != 0x00010000) {
	error(err_sfnttablevers, &sf->pos, "hhea");
	return;
    }
    fi->ascent = hhea.ascent;
    fi->descent = hhea.descent;
    if (hhea.metricDataFormat != 0) {
	error(err_sfnttablevers, &sf->pos, "hmtx");
	return;
    }
    if (!sfnt_findtable(sf, TAG_hmtx, &ptr, &end)) {
	error(err_sfntnotable, &sf->pos, "hmtx");
	return;
    }
    hmtx = snewn(hhea.numOfLongHorMetrics, unsigned);
    if (decoden(longhormetric_decode, ptr, end, hmtx, sizeof(*hmtx),
		hhea.numOfLongHorMetrics) == NULL) {
	error(err_sfntbadtable, &sf->pos, "hmtx");
	return;
    }
    for (i = 0; i < sf->nglyphs; i++) {
	glyph_width *w = snew(glyph_width);
	w->glyph = sfnt_indextoglyph(sf, i);
	j = i < hhea.numOfLongHorMetrics ? i : hhea.numOfLongHorMetrics - 1;
	w->width = hmtx[j] * UNITS_PER_PT / sf->head.unitsPerEm;
	add234(fi->widths, w);
    }
    /* Now see if the 'OS/2' table has any useful metrics */
    if (!sfnt_findtable(sf, TAG_OS_2, &ptr, &end))
	return;
    if (decode(uint16_decode, ptr, end, &OS_2.version) == NULL)
	goto bados2;
    if (OS_2.version >= 2) {
	if (decode(t_OS_2_v2_decode, ptr, end, &OS_2) == NULL)
	    goto bados2;
	fi->xheight = OS_2.sxHeight * FUNITS_PER_PT / sf->head.unitsPerEm;
	fi->capheight = OS_2.sCapHeight * FUNITS_PER_PT / sf->head.unitsPerEm;
    } else if (OS_2.version == 1) {
	if (decode(t_OS_2_v1_decode, ptr, end, &OS_2) == NULL)
	    goto bados2;
    } else
	return;
    fi->ascent = OS_2.sTypoAscender * FUNITS_PER_PT / sf->head.unitsPerEm;
    fi->descent = OS_2.sTypoDescender * FUNITS_PER_PT / sf->head.unitsPerEm;
    return;
  bados2:
    error(err_sfntbadtable, &sf->pos, "OS/2");
}

/*
 * Get kerning data from a 'kern' table
 *
 * 'kern' tables have two gratuitously different header formats, one
 * used by Apple and one by Microsoft.  Happily, the kerning tables
 * themselves use the same formats.  Halibut only supports simple kern
 * pairs for horizontal kerning of horizontal text, and ignores
 * everything else.
 */
static void sfnt_getkern(font_info *fi) {
    sfnt *sf = fi->fontfile;
    t_kern kern;
    unsigned version, i, j;
    void *ptr, *end;

    if (!sfnt_findtable(sf, TAG_kern, &ptr, &end))
	return;
    if (!decode(uint16_decode, ptr, end, &version))
	goto bad;
    if (version == 0)
	ptr = decode(t_kern_v0_decode, ptr, end, &kern);
    else if (version == 1)
	ptr = decode(t_kern_v1_decode, ptr, end, &kern);
    else return;
    if (ptr == NULL) goto bad;
    for (i = 0; i < kern.nTables; i++) {
	kern_f0 f0;
	kern_pair *kerns;
	if (version == 0) {
	    kern_v0_subhdr sub;
	    ptr = decode(kern_v0_subhdr_decode, ptr, end, &sub);
	    if (ptr == NULL) goto bad;
	    if (sub.version != 0 ||
		(sub.coverage & (KERN_V0_HORIZ | KERN_V0_MINIMUM |
				 KERN_V0_CROSSSTREAM | KERN_V0_FORMAT)) !=
		(KERN_V0_HORIZ | KERN_V0_FORMAT_0)) {
		ptr = (char *)ptr + sub.length - 6;
		continue;
	    }
	} else {
	    kern_v1_subhdr sub;
	    ptr = decode(kern_v1_subhdr_decode, ptr, end, &sub);
	    if (ptr == NULL) goto bad;
	    if ((sub.coverage & (KERN_V1_VERTICAL | KERN_V1_CROSSSTREAM |
				KERN_V1_VARIATION | KERN_V1_FORMAT)) !=
		KERN_V0_FORMAT_0) {
		ptr = (char *)ptr + sub.length - 8;
		continue;
	    }
	}
	ptr = decode(kern_f0_decode, ptr, end, &f0);
	if (ptr == NULL) goto bad;
	kerns = snewn(f0.nPairs, kern_pair);
	for (j = 0; j < f0.nPairs; j++) {
	    kern_f0_pair p;
	    kern_pair *kp = kerns + j;
	    ptr = decode(kern_f0_pair_decode, ptr, end, &p);
	    if (ptr == NULL) goto bad;
	    if (p.left >= sf->nglyphs || p.right >= sf->nglyphs) goto bad;
	    kp->left = sfnt_indextoglyph(sf, p.left);
	    kp->right = sfnt_indextoglyph(sf, p.right);
	    kp->kern = p.value * UNITS_PER_PT / (int)sf->head.unitsPerEm;
	    add234(fi->kerns, kp);
	}
    }
    return;
  bad:
    error(err_sfntbadtable, &sf->pos, "kern");
    return;
}

/*
 * Get mapping data from 'cmap' table
 *
 * We look for either a (0, 3), or (3, 1) table, both of these being
 * versions of UCS-2.  We only handle format 4 of this table, since
 * that seems to be the only one in use.
 */
void sfnt_getmap(font_info *fi) {
    sfnt *sf = fi->fontfile;
    t_cmap cmap;
    encodingrec *esd;
    void *base, *ptr, *end;
    unsigned i;
    unsigned format;


    for (i = 0; i < lenof(fi->bmp); i++)
	    fi->bmp[i] = 0xFFFF;
    if (!sfnt_findtable(sf, TAG_cmap, &ptr, &end)) {
	error(err_sfntnotable, &sf->pos, "cmap");
    }
    base = ptr;
    ptr = decode(t_cmap_decode, ptr, end, &cmap);
    if (ptr == NULL) goto bad;
    esd = snewn(cmap.numTables, encodingrec);
    ptr = decoden(encodingrec_decode, ptr, end, esd, sizeof(*esd),
		  cmap.numTables);
    if (ptr == NULL) goto bad;
    for (i = 0; i < cmap.numTables; i++) {
	if (!decode(uint16_decode, (char *)base + esd[i].offset, end, &format))
	    goto bad;
	if ((esd[i].platformID == 0 && esd[i].encodingID == 3) ||
	    (esd[i].platformID == 3 && esd[i].encodingID == 1)) {
	    /* UCS-2 encoding */
	    if (!decode(uint16_decode, (char *)base + esd[i].offset, end,
			&format))
		goto bad;
	    if (format == 4) {
		unsigned *data, *endCode, *startCode, *idDelta, *idRangeOffset;
		unsigned *glyphIndexArray;
		unsigned segcount, nword, nglyphindex, j;
		cmap4 cmap4;

		ptr = decode(cmap4_decode, (char *)base + esd[i].offset, end,
			     &cmap4);
		if (!ptr) goto bad;
		segcount = cmap4.segCountX2 / 2;
		nword = cmap4.length / 2 - 7;
		data = snewn(nword, unsigned);
		if (!decoden(uint16_decode, ptr, (char *)ptr + nword * 2,
			     data, sizeof(*data), nword)) goto bad;
		endCode = data;
		startCode = data + segcount + 1;
		idDelta = startCode + segcount;
		idRangeOffset = idDelta + segcount;
		glyphIndexArray = idRangeOffset + segcount;
		nglyphindex = nword - segcount * 4 - 1;

		for (j = 0; j < segcount; j++) {
		    unsigned k, idx;

		    if (idRangeOffset[j] == 0) {
			for (k = startCode[j]; k <= endCode[j]; k++) {
			    idx = (k + idDelta[j]) & 0xffff;
			    if (idx != 0) {
				if (idx > sf->nglyphs) goto bad;
				fi->bmp[k] = sfnt_indextoglyph(sf, idx);
			    }
			}
		    } else {
			unsigned startidx = idRangeOffset[j]/2 - segcount + j;
			for (k = startCode[j]; k <= endCode[j]; k++) {
			    idx = glyphIndexArray[startidx + k - startCode[j]];
			    if (idx != 0) {
				idx = (idx + idDelta[j]) & 0xffff;
				if (idx > sf->nglyphs) goto bad;
				fi->bmp[k] = sfnt_indextoglyph(sf, idx);
			    }
			}
		    }
		}
		sfree(data);
		return;
	    }
	}
    }
    error(err_sfntnounicmap, &sf->pos);
    return;
  bad:
    error(err_sfntbadtable, &sf->pos, "cmap");
}

void read_sfnt_file(input *in) {
    sfnt *sf = snew(sfnt);
    size_t off = 0, got;
    FILE *fp = in->currfp;
    font_info *fi = snew(font_info);
    void *ptr, *end;
    t_maxp maxp;

    fi->name = NULL;
    fi->widths = newtree234(width_cmp);
    fi->kerns = newtree234(kern_cmp);
    fi->ligs = newtree234(lig_cmp);
    fi->fontbbox[0] = fi->fontbbox[1] = fi->fontbbox[2] = fi->fontbbox[3] = 0;
    fi->capheight = fi->xheight = fi->ascent = fi->descent = 0;
    fi->stemh = fi->stemv = fi->italicangle = 0;
    fi->fontfile = sf;
    fi->filetype = TRUETYPE;

    sf->len = 32768;
    sf->data = snewn(sf->len, unsigned char);
    for (;;) {
	got = fread((char *)sf->data + off, 1, sf->len - off, fp);
	off += got;
	if (off != sf->len) break;
	sf->len *= 2;
	sf->data = sresize(sf->data, sf->len, unsigned char);
    }
    fclose(in->currfp);
    sf->len = off;
    sf->data = sresize(sf->data, sf->len, unsigned char);
    sf->end = (char *)sf->data + sf->len;
    sf->pos = in->pos;
    sf->pos.line = 0;
    sf->nglyphs = 0;
    ptr = decode(offsubdir_decode, sf->data, sf->end, &sf->osd);
    if (ptr == NULL) {
	error(err_sfntbadhdr, &sf->pos);
	return;
    }
    sf->td = snewn(sf->osd.numTables, tabledir);
    ptr = decoden(tabledir_decode, ptr, sf->end, sf->td, sizeof(*sf->td),
		  sf->osd.numTables);
    if (ptr == NULL) {
	error(err_sfntbadhdr, &sf->pos);
	return;
    }	
    if (!sfnt_findtable(sf, TAG_head, &ptr, &end)) {
	error(err_sfntnotable, &sf->pos, "head");
	return;
    }
    if (decode(t_head_decode, ptr, end, &sf->head) == NULL) {
	error(err_sfntbadtable, &sf->pos, "head");
	return;
    }
    if ((sf->head.version & 0xffff0000) != 0x00010000) {
	error(err_sfnttablevers, &sf->pos, "head");
	return;
    }
    if (!sfnt_findtable(sf, TAG_maxp, &ptr, &end)) {
	error(err_sfntnotable, &sf->pos, "maxp");
	return;
    }
    if (decode(t_maxp_decode, ptr, end, &maxp) == NULL) {
	error(err_sfntbadtable, &sf->pos, "maxp");
	return;
    }
    if (maxp.version < 0x00005000 || maxp.version > 0x0001ffff) {
	error(err_sfnttablevers, &sf->pos, "maxp");
	return;
    }
    sf->nglyphs = maxp.numGlyphs;
    fi->name = sfnt_psname(fi);
    if (fi->name == NULL) return;
    sfnt_mapglyphs(fi);
    sfnt_getmetrics(fi);
    sfnt_getkern(fi);
    sfnt_getmap(fi);
    fi->next = all_fonts;
    all_fonts = fi;
}

static int sizecmp(const void *a, const void *b) {
    if (*(size_t *)a < *(size_t *)b) return -1;
    if (*(size_t *)a > *(size_t *)b) return 1;
    return 0;
}

/*
 * The format for embedding TrueType fonts in Postscript is defined in
 * Adobe Technical Note #5012: The Type 42 Font Format Specification.
 * <http://partners.adobe.com/public/developer/en/font/5012.Type42_Spec.pdf>
 */

void sfnt_writeps(font_info const *fi, FILE *ofp) {
    unsigned i, j, lastbreak;
    sfnt *sf = fi->fontfile;
    size_t *breaks, glyfoff, glyflen;
    void *glyfptr, *glyfend, *locaptr, *locaend;
    unsigned *loca;
    int cc = 0;

    /* XXX Unclear that this is the correct format. */
    fprintf(ofp, "%%!PS-TrueTypeFont-%u-%u\n", sf->osd.scaler_type,
	    sf->head.fontRevision);
    if (sf->minmem)
	fprintf(ofp, "%%%%VMUsage: %u %u\n", sf->minmem, sf->maxmem);
    fprintf(ofp, "9 dict dup begin\n");
    fprintf(ofp, "/FontType 42 def\n");
    fprintf(ofp, "/FontMatrix [1 0 0 1 0 0] def\n");
    fprintf(ofp, "/FontName /%s def\n", fi->name);
    fprintf(ofp, "/Encoding StandardEncoding def\n");
    if ((sf->head.flags & 0x0003) == 0x0003) {
	/*
	 * Sensible font with the origin in the right place, such that
	 * the bounding box is meaningful.
	 */
	fprintf(ofp, "/FontBBox [%g %g %g %g] readonly def\n",
		(double)sf->head.xMin / sf->head.unitsPerEm,
		(double)sf->head.yMin / sf->head.unitsPerEm,
		(double)sf->head.xMax / sf->head.unitsPerEm, 
		(double)sf->head.yMax / sf->head.unitsPerEm);
    } else {
	/* Non-sensible font. */
	fprintf(ofp, "/FontBBox [0 0 0 0] readonly def\n");
    }
    fprintf(ofp, "/PaintType 0 def\n");
    fprintf(ofp, "/CharStrings %u dict dup begin\n", sf->nglyphs);
    fprintf(ofp, "0 1 %u{currentfile token pop exch def}bind for\n",
	sf->nglyphs - 1);
    for (i = 0; i < sf->nglyphs; i++)
	ps_token(ofp, &cc, "/%s", glyph_extern(sfnt_indextoglyph(sf, i)));
    fprintf(ofp, "\nend readonly def\n");
    fprintf(ofp, "/sfnts [<");
    breaks = snewn(sf->osd.numTables + sf->nglyphs, size_t);
    for (i = 0; i < sf->osd.numTables; i++) {
	breaks[i] = sf->td[i].offset;
    }
    if (!sfnt_findtable(sf, TAG_glyf, &glyfptr, &glyfend)) {
	error(err_sfntnotable, &sf->pos, "glyf");
	return;
    }
    glyfoff = (char *)glyfptr - (char *)sf->data;
    glyflen = (char *)glyfend - (char *)glyfptr;
    if (!sfnt_findtable(sf, TAG_loca, &locaptr, &locaend)) {
	error(err_sfntnotable, &sf->pos, "loca");
	return;
    }
    loca = snewn(sf->nglyphs, unsigned);
    if (sf->head.indexToLocFormat == 0) {
	if (!decoden(uint16_decode, locaptr, locaend, loca, sizeof(*loca),
		     sf->nglyphs)) goto badloca;
	for (i = 0; i < sf->nglyphs; i++) loca[i] *= 2;
    } else {
	if (!decoden(uint32_decode, locaptr, locaend, loca, sizeof(*loca),
		     sf->nglyphs)) goto badloca;
    }
    for (i = 1; i < sf->nglyphs; i++) {
	if (loca[i] > glyflen) goto badloca;
	breaks[sf->osd.numTables + i - 1] = loca[i] + glyfoff;
    }
    breaks[sf->osd.numTables + sf->nglyphs - 1] = sf->len;
    qsort(breaks, sf->osd.numTables + sf->nglyphs, sizeof(*breaks), sizecmp);
    j = lastbreak = 0;
    for (i = 0; i < sf->len; i++) {
	if ((i - lastbreak) % 38 == 0) fprintf(ofp, "\n");
	if (i == breaks[j]) {
	    while (i == breaks[j]) j++;
	    lastbreak = i;
	    fprintf(ofp, "00><\n");
	}
	fprintf(ofp, "%02x", *((unsigned char *)sf->data + i));
    }
    fprintf(ofp, "00>] readonly def\n");
    sfree(breaks);
    fprintf(ofp, "end /%s exch definefont\n", fi->name);
    return;
  badloca:
    error(err_sfntbadtable, &sf->pos, "loca");
}

void sfnt_data(font_info *fi, char **bufp, size_t *lenp) {
    sfnt *sf = fi->fontfile;
    *bufp = sf->data;
    *lenp = sf->len;
}
