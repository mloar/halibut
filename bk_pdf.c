/*
 * PDF backend for Halibut
 */

#include <assert.h>
#include "halibut.h"
#include "paper.h"
#include "deflate.h"

#define TREE_BRANCH 8		       /* max branching factor in page tree */

paragraph *pdf_config_filename(char *filename)
{
    return cmdline_cfg_simple("pdf-filename", filename, NULL);
}

struct object_Tag {
    objlist *list;
    object *next;
    int number;
    rdstringc main, stream;
    int size, fileoff;
    char *final;
};

struct objlist_Tag {
    int number;
    object *head, *tail;
};

static void pdf_string(void (*add)(object *, char const *),
		       object *, char const *);
static void pdf_string_len(void (*add)(object *, char const *),
			   object *, char const *, int);
static void objref(object *o, object *dest);
static void objdest(object *o, page_data *p);

static int is_std_font(char const *name);

static void make_pages_node(object *node, object *parent, page_data *first,
			    page_data *last, object *resources,
			    object *mediabox);
static int make_outline(object *parent, outline_element *start, int n,
			int open);
static int pdf_versionid(FILE *fp, word *words);

void pdf_backend(paragraph *sourceform, keywordlist *keywords,
		 indexdata *idx, void *vdoc) {
    document *doc = (document *)vdoc;
    int font_index;
    font_encoding *fe;
    page_data *page;
    int pageno;
    FILE *fp;
    char *filename;
    paragraph *p;
    objlist olist;
    object *o, *info, *cat, *outlines, *pages, *resources, *mediabox;
    int fileoff;

    IGNORE(keywords);
    IGNORE(idx);

    filename = dupstr("output.pdf");
    for (p = sourceform; p; p = p->next) {
	if (p->type == para_Config) {
	    if (!ustricmp(p->keyword, L"pdf-filename")) {
		sfree(filename);
		filename = dupstr(adv(p->origkeyword));
	    }
	}
    }

    olist.head = olist.tail = NULL;
    olist.number = 1;

    {
	char buf[256];

	info = new_object(&olist);
	objtext(info, "<<\n");
	if (doc->n_outline_elements > 0) {
	    char *title;
	    int titlelen;

	    title =
	       pdf_outline_convert(doc->outline_elements->pdata->outline_title,
					&titlelen);
	    objtext(info, "/Title ");
	    pdf_string_len(objtext, info, title, titlelen);
	    sfree(title);
	    objtext(info, "\n");
	}
	objtext(info, "/Producer ");
	sprintf(buf, "Halibut, %s", version);
	pdf_string(objtext, info, buf);
	objtext(info, "\n>>\n");
    }

    cat = new_object(&olist);
    if (doc->n_outline_elements > 0)
	outlines = new_object(&olist);
    else
	outlines = NULL;
    pages = new_object(&olist);
    resources = new_object(&olist);

    /*
     * The catalogue just contains references to the outlines and
     * pages objects, and the pagelabels dictionary.
     */
    objtext(cat, "<<\n/Type /Catalog");
    if (outlines) {
	objtext(cat, "\n/Outlines ");
	objref(cat, outlines);
    }
    objtext(cat, "\n/Pages ");
    objref(cat, pages);
    /* Halibut just numbers pages 1, 2, 3, ... */
    objtext(cat, "\n/PageLabels<</Nums[0<</S/D>>]>>");
    if (outlines)
	objtext(cat, "\n/PageMode /UseOutlines");
    objtext(cat, "\n>>\n");

    /*
     * Set up the resources dictionary, which mostly means
     * providing all the font objects and names to call them by.
     */
    font_index = 0;
    objtext(resources, "<<\n/ProcSet [/PDF/Text]\n/Font <<\n");
    for (fe = doc->fonts->head; fe; fe = fe->next) {
	char fname[40];
	char buf[80];
	int i, prev;
	object *font, *fontdesc;
	int flags;
	font_info const *fi = fe->font->info;

	sprintf(fname, "f%d", font_index++);
	fe->name = dupstr(fname);

	font = new_object(&olist);

	objtext(resources, "/");
	objtext(resources, fe->name);
	objtext(resources, " ");
	objref(resources, font);
	objtext(resources, "\n");


	/*
	 * Construct those parts of the font descriptor that don't dependd
	 * on the file format.
	 */
	if (!is_std_font(fe->font->info->name)) {
	    fontdesc = new_object(&olist);

#define FF_FIXEDPITCH	0x00000001
#define FF_SERIF	0x00000002
#define FF_SYMBOLIC	0x00000004
#define FF_SCRIPT	0x00000008
#define FF_NONSYMBOLIC	0x00000020
#define FF_ITALIC	0x00000040
#define FF_ALLCAP	0x00010000
#define FF_SMALLCAP	0x00020000
#define FF_FORCEBOLD	0x00040000

	    objtext(fontdesc, "<<\n/Type /FontDescriptor\n/Name /");
	    objtext(fontdesc, fi->name);
	    flags = 0;
	    if (fi->italicangle) flags |= FF_ITALIC;
	    flags |= FF_NONSYMBOLIC;
	    sprintf(buf, "\n/Flags %d\n", flags);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/FontBBox [%g %g %g %g]\n", fi->fontbbox[0],
		    fi->fontbbox[1], fi->fontbbox[2], fi->fontbbox[3]);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/ItalicAngle %g\n", fi->italicangle);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/Ascent %g\n", fi->ascent);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/Descent %g\n", fi->descent);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/CapHeight %g\n", fi->capheight);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/XHeight %g\n", fi->xheight);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/StemH %g\n", fi->stemh);
	    objtext(fontdesc, buf);
	    sprintf(buf, "/StemV %g\n", fi->stemv);
	    objtext(fontdesc, buf);
	}

	objtext(font, "<<\n/Type /Font\n/BaseFont /");
	objtext(font, fe->font->info->name);
	if (fe->font->info->filetype == TRUETYPE) {
	    object *cidfont = new_object(&olist);
	    object *cmap = new_object(&olist);
	    unsigned short ranges[256];
	    unsigned startidx, nranges, nchars;
	    int start;

	    objtext(font, "/Subtype/Type0\n/Encoding ");
	    objtext(cmap, "<</Type/CMap\n/CMapName/");
	    objtext(cmap, fe->name);
	    objtext(cmap, "\n/CIDSystemInfo<</Registry(Adobe)"
		    "/Ordering(Identity)/Supplement 0>>\n");
	    objstream(cmap, "%!PS-Adobe-3.0 Resource-CMap\n"
		      "%%DocumentNeededResources: procset CIDInit\n"
		      "%%IncludeResource: procset CIDInit\n"
		      "%%BeginResource: CMap ");
	    objstream(cmap, fe->name);
	    objstream(cmap, "\n%%Title (");
	    objstream(cmap, fe->name);
	    objstream(cmap, " Adobe Identity 0)\n%%Version: 1\n%%EndComments\n");
	    objstream(cmap, "/CIDInit/ProcSet findresource begin\n");
	    objstream(cmap, "12 dict begin begincmap\n");
	    objstream(cmap, "/CIDSystemInfo 3 dict dup begin\n"
		      "/Registry(Adobe)def/Ordering(Identity)def"
		      "/Supplement 0 def end def\n");
	    objstream(cmap, "/CMapName/");
	    objstream(cmap, fe->name);
	    objstream(cmap, " def/CMapType 0 def/WMode 0 def\n");
	    objstream(cmap, "1 begincodespacerange<00><FF>"
		      "endcodespacerange\n");
	    start = -1; nranges = nchars = 0;
	    for (i = 0; i < 256; i++) {
		unsigned idx;

		ranges[i] = 0;
		if (fe->vector[i] == NOGLYPH)
		    continue;
		idx = sfnt_glyphtoindex(fe->font->info->fontfile,
					fe->vector[i]);
		if (start >= 0 && idx - startidx == i - start) {
		    if (ranges[start] == 1) {
			nranges++; nchars--;
		    }
		    ranges[start] = i - start + 1;
		} else {
		    ranges[i] = 1;
		    start = i;
		    startidx = idx;
		    nchars++;
		}
	    }
	    i = 0;
	    while (nranges) {
		unsigned blk = nranges > 100 ? 100 : nranges;
		nranges -= blk;
		sprintf(buf, "%u ", blk);
		objstream(cmap, buf);
		objstream(cmap, "begincidrange\n");
		while (blk) {
		    if (ranges[i] > 1) {
			sprintf(buf, "<%02X>", i);
			objstream(cmap, buf);
			sprintf(buf, "<%02X>", i + ranges[i] - 1);
			objstream(cmap, buf);
			sprintf(buf, "%hu\n",
				sfnt_glyphtoindex(fe->font->info->fontfile,
						  fe->vector[i]));
			objstream(cmap, buf);
			blk--;
		    }
		    i++;
		}
		objstream(cmap, "endcidrange\n");
	    }
	    i = 0;
	    while (nchars) {
		unsigned blk = nchars > 100 ? 100 : nchars;
		nchars -= blk;
		sprintf(buf, "%u ", blk);
		objstream(cmap, buf);
		objstream(cmap, "begincidchar\n");
		while (blk) {
		    if (ranges[i] == 1) {
			sprintf(buf, "<%02X>", i);
			objstream(cmap, buf);
			sprintf(buf, "%hu\n",
				sfnt_glyphtoindex(fe->font->info->fontfile,
						  fe->vector[i]));
			objstream(cmap, buf);
			blk--;
		    }
		    i++;
		}
		objstream(cmap, "endcidchar\n");
	    }
	    objstream(cmap, "endcmap CMapName currentdict /CMap "
		      "defineresource pop end end\n%%EndResource\n%%EOF\n");

	    objref(font, cmap);
	    objtext(font, "\n/DescendantFonts[");
	    objref(font, cidfont);
	    objtext(font, "]\n");
	    objtext(cidfont, "<<\n/Type/Font\n/Subtype/CIDFontType2\n"
		    "/BaseFont/");
	    objtext(cidfont, fe->font->info->name);
	    objtext(cidfont, "\n/CIDSystemInfo<</Registry(Adobe)"
		    "/Ordering(Identity)/Supplement 0>>\n");
	    objtext(cidfont, "/FontDescriptor ");
	    objref(cidfont, fontdesc);
	    objtext(cidfont, "\n/W[0[");
	    for (i = 0; i < sfnt_nglyphs(fe->font->info->fontfile); i++) {
		char buf[20];
		double width;
		width = find_width(fe->font,
			       sfnt_indextoglyph(fe->font->info->fontfile, i));
		sprintf(buf, "%g ", 1000.0 * width / FUNITS_PER_PT);
		objtext(cidfont, buf);
	    }
	    objtext(cidfont, "]]>>\n");
	} else {
	    objtext(font, "/Subtype /Type1\n");
	    objtext(font, "\n/Encoding <<\n/Type /Encoding\n/Differences [");

	    for (i = 0; i < 256; i++) {
		char buf[20];
		if (fe->vector[i] == NOGLYPH)
		    continue;
		if (i != prev + 1) {
		    sprintf(buf, "\n%d", i);
		    objtext(font, buf);
		}
		objtext(font, i % 8 ? "/" : "\n/");
		objtext(font, glyph_extern(fe->vector[i]));
		prev = i;
	    }

	    objtext(font, "\n]\n>>\n");
	    if (!is_std_font(fe->font->info->name)){
		object *widths = new_object(&olist);
		int firstchar = -1, lastchar = -1;
		for (i = 0; i < 256; i++)
		    if (fe->vector[i] != NOGLYPH) {
			if (firstchar < 0) firstchar = i;
			lastchar = i;
		    }
		sprintf(buf, "/FirstChar %d\n/LastChar %d\n/Widths ",
			firstchar, lastchar);
		objtext(font, buf);
		objref(font, widths);
		objtext(font, "\n");
		objtext(widths, "[\n");
		for (i = firstchar; i <= lastchar; i++) {
		    double width;
		    if (fe->vector[i] == NOGLYPH)
			width = 0.0;
		    else
			width = find_width(fe->font, fe->vector[i]);
		    sprintf(buf, "%g\n", 1000.0 * width / FUNITS_PER_PT);
		    objtext(widths, buf);
		}
		objtext(widths, "]\n");
		objtext(font, "/FontDescriptor ");
		objref(font, fontdesc);
	    }

	}

	if (!is_std_font(fe->font->info->name)) {
	    if (fi->fontfile && fi->filetype == TYPE1) {
		object *fontfile = new_object(&olist);
		size_t len;
		char *ffbuf;

		pf_part1((font_info *)fi, &ffbuf, &len);
		objstream_len(fontfile, ffbuf, len);
		sfree(ffbuf);
		sprintf(buf, "<<\n/Length1 %lu\n", (unsigned long)len);
		objtext(fontfile, buf);
		pf_part2((font_info *)fi, &ffbuf, &len);
		objstream_len(fontfile, ffbuf, len);
		sfree(ffbuf);
		sprintf(buf, "/Length2 %lu\n", (unsigned long)len);
		objtext(fontfile, buf);
		objtext(fontfile, "/Length3 0\n");
		objtext(fontdesc, "/FontFile ");
		objref(fontdesc, fontfile);
	    } else if (fi->fontfile && fi->filetype == TRUETYPE) {
		object *fontfile = new_object(&olist);
		size_t len;
		char *ffbuf;

		sfnt_data((font_info *)fi, &ffbuf, &len);
		objstream_len(fontfile, ffbuf, len);
		sprintf(buf, "<<\n/Length1 %lu\n", (unsigned long)len);
		objtext(fontfile, buf);
		objtext(fontdesc, "/FontFile2 ");
		objref(fontdesc, fontfile);
	    }
	    objtext(fontdesc, "\n>>\n");
	}

	objtext(font, "\n>>\n");
    }
    objtext(resources, ">>\n>>\n");

    {
	char buf[255];
	mediabox = new_object(&olist);
	sprintf(buf, "[0 0 %g %g]\n",
		doc->paper_width / FUNITS_PER_PT,
		doc->paper_height / FUNITS_PER_PT);
	objtext(mediabox, buf);
    }

    /*
     * Define the page objects for each page, and get each one
     * ready to have a `Parent' specification added to it.
     */
    for (page = doc->pages; page; page = page->next) {
	object *opage;

	opage = new_object(&olist);
	page->spare = opage;
	objtext(opage, "<<\n/Type /Page\n");
    }

    /*
     * Recursively build the page tree.
     */
    make_pages_node(pages, NULL, doc->pages, NULL, resources, mediabox);

    /*
     * Create and render the individual pages.
     */
    pageno = 0;
    for (page = doc->pages; page; page = page->next) {
	object *opage, *cstr;
	rect *r;
	text_fragment *frag, *frag_end;
	char buf[256];
	int x, y, lx, ly;

	opage = (object *)page->spare;
	/*
	 * At this point the page dictionary is already
	 * half-written, with /Type and /Parent already present. We
	 * continue from there.
	 */

	/*
	 * The PDF spec says /Resources is required, but also says
	 * that it's inheritable and may be omitted if it's present
	 * in a Pages node. In our case it is: it's present in the
	 * topmost /Pages node because we carefully put it there.
	 * So we don't need a /Resources entry here.  The same applies
	 * to /MediaBox.
	 */

	/*
	 * Now we're ready to define a content stream containing
	 * the actual text on the page.
	 */
	cstr = new_object(&olist);
	objtext(opage, "/Contents ");
	objref(opage, cstr);
	objtext(opage, "\n");

	/*
	 * Render any rectangles on the page.
	 */
	for (r = page->first_rect; r; r = r->next) {
	    char buf[512];
	    sprintf(buf, "%g %g %g %g re f\n",
		    r->x / FUNITS_PER_PT, r->y / FUNITS_PER_PT,
		    r->w / FUNITS_PER_PT, r->h / FUNITS_PER_PT);
	    objstream(cstr, buf);
	}

	objstream(cstr, "BT\n");

	/*
	 * PDF tracks two separate current positions: the position
	 * given in the `line matrix' and the position given in the
	 * `text matrix'. We must therefore track both as well.
	 * They start off at -1 (unset).
	 */
	lx = ly = -1;
	x = y = -1;

	frag = page->first_text;
	while (frag) {
	    /*
	     * For compactness, I'm going to group text fragments
	     * into subsequences that use the same font+size. So
	     * first find the end of this subsequence.
	     */
	    for (frag_end = frag;
		 (frag_end &&
		  frag_end->fe == frag->fe &&
		  frag_end->fontsize == frag->fontsize);
		 frag_end = frag_end->next);

	    /*
	     * Now select the text fragment, and prepare to display
	     * the text.
	     */
	    objstream(cstr, "/");
	    objstream(cstr, frag->fe->name);
	    sprintf(buf, " %d Tf ", frag->fontsize);
	    objstream(cstr, buf);

	    while (frag && frag != frag_end) {
		/*
		 * Place the text position for the first piece of
		 * text.
		 */
		if (lx < 0) {
		    sprintf(buf, "1 0 0 1 %g %g Tm ",
			    frag->x/FUNITS_PER_PT, frag->y/FUNITS_PER_PT);
		} else {
		    sprintf(buf, "%g %g Td ",
			    (frag->x - lx)/FUNITS_PER_PT,
			    (frag->y - ly)/FUNITS_PER_PT);
		}
		objstream(cstr, buf);
		lx = x = frag->x;
		ly = y = frag->y;

		/*
		 * See if we're going to use Tj (show a single
		 * string) or TJ (show an array of strings with
		 * x-spacings between them). We determine this by
		 * seeing if there's more than one text fragment in
		 * sequence with the same y-coordinate.
		 */
		if (frag->next && frag->next != frag_end &&
		    frag->next->y == y) {
		    /*
		     * The TJ strategy.
		     */
		    objstream(cstr, "[");
		    while (frag && frag != frag_end && frag->y == y) {
			if (frag->x != x) {
			    sprintf(buf, "%g",
				    (x - frag->x) * 1000.0 /
				    (FUNITS_PER_PT * frag->fontsize));
			    objstream(cstr, buf);
			}
			pdf_string(objstream, cstr, frag->text);
			x = frag->x + frag->width;
			frag = frag->next;
		    }
		    objstream(cstr, "]TJ\n");
		} else
		{
		    /*
		     * The Tj strategy.
		     */
		    pdf_string(objstream, cstr, frag->text);
		    objstream(cstr, "Tj\n");
		    frag = frag->next;
		}
	    }
	}
	objstream(cstr, "ET");

	/*
	 * Also, we want an annotation dictionary containing the
	 * cross-references from this page.
	 */
	if (page->first_xref) {
	    xref *xr;
	    objtext(opage, "/Annots [\n");

	    for (xr = page->first_xref; xr; xr = xr->next) {
		char buf[256];

		objtext(opage, "<</Subtype/Link\n/Rect[");
		sprintf(buf, "%g %g %g %g",
			xr->lx / FUNITS_PER_PT, xr->by / FUNITS_PER_PT,
			xr->rx / FUNITS_PER_PT, xr->ty / FUNITS_PER_PT);
		objtext(opage, buf);
		objtext(opage, "]/Border[0 0 0]\n");

		if (xr->dest.type == PAGE) {
		    objtext(opage, "/Dest");
		    objdest(opage, xr->dest.page);
		} else {
		    objtext(opage, "/A<</S/URI/URI");
		    pdf_string(objtext, opage, xr->dest.url);
		    objtext(opage, ">>");
		}

		objtext(opage, ">>\n");
	    }

	    objtext(opage, "]\n");
	}

	objtext(opage, ">>\n");
    }

    /*
     * Set up the outlines dictionary.
     */
    if (outlines) {
	int topcount;
	char buf[80];

	objtext(outlines, "<<\n/Type /Outlines\n");
	topcount = make_outline(outlines, doc->outline_elements,
				doc->n_outline_elements, TRUE);
	sprintf(buf, "/Count %d\n>>\n", topcount);
	objtext(outlines, buf);
    }

    /*
     * Assemble the final linear form of every object.
     */
    for (o = olist.head; o; o = o->next) {
	rdstringc rs = {0, 0, NULL};
	char text[80];
	deflate_compress_ctx *zcontext;
	void *zbuf;
	int zlen;

	sprintf(text, "%d 0 obj\n", o->number);
	rdaddsc(&rs, text);

	if (o->stream.text) {
	    if (!o->main.text)
		rdaddsc(&o->main, "<<\n");
#ifdef PDF_NOCOMPRESS
	    zlen = o->stream.pos;
	    zbuf = snewn(zlen, char);
	    memcpy(zbuf, o->stream.text, zlen);
	    sprintf(text, "/Length %d\n>>\n", zlen);
#else
	    zcontext = deflate_compress_new(DEFLATE_TYPE_ZLIB);
	    deflate_compress_data(zcontext, o->stream.text, o->stream.pos,
				  DEFLATE_END_OF_DATA, &zbuf, &zlen);
	    deflate_compress_free(zcontext);
	    sprintf(text, "/Filter/FlateDecode\n/Length %d\n>>\n", zlen);
#endif
	    rdaddsc(&o->main, text);
	}

	assert(o->main.text);
	rdaddsc(&rs, o->main.text);
	sfree(o->main.text);

	if (rs.text[rs.pos-1] != '\n')
	    rdaddc(&rs, '\n');

	if (o->stream.text) {
	    rdaddsc(&rs, "stream\n");
	    rdaddsn(&rs, zbuf, zlen);
	    rdaddsc(&rs, "\nendstream\n");
	    sfree(o->stream.text);
	    sfree(zbuf);
	}

	rdaddsc(&rs, "endobj\n");

	o->final = rs.text;
	o->size = rs.pos;
    }

    /*
     * Write out the PDF file.
     */

    fp = fopen(filename, "wb");
    if (!fp) {
	error(err_cantopenw, filename);
	return;
    }

    /*
     * Header. I'm going to put the version IDs in the header as
     * well, simply in PDF comments.  The PDF Reference also suggests
     * that binary PDF files contain four top-bit-set characters in
     * the second line.
     */
    fileoff = fprintf(fp, "%%PDF-1.3\n%% L\xc3\xba\xc3\xb0""a\n");
    for (p = sourceform; p; p = p->next)
	if (p->type == para_VersionID)
	    fileoff += pdf_versionid(fp, p->words);

    /*
     * Body
     */
    for (o = olist.head; o; o = o->next) {
	o->fileoff = fileoff;
	fwrite(o->final, 1, o->size, fp);
	fileoff += o->size;
    }

    /*
     * Cross-reference table
     */
    fprintf(fp, "xref\n");
    assert(olist.head->number == 1);
    fprintf(fp, "0 %d\n", olist.tail->number + 1);
    fprintf(fp, "0000000000 65535 f \n");
    for (o = olist.head; o; o = o->next) {
	char entry[40];
	sprintf(entry, "%010d 00000 n \n", o->fileoff);
	assert(strlen(entry) == 20);
	fputs(entry, fp);
    }

    /*
     * Trailer
     */
    fprintf(fp, "trailer\n<<\n/Size %d\n/Root %d 0 R\n/Info %d 0 R\n>>\n",
	    olist.tail->number + 1, cat->number, info->number);
    fprintf(fp, "startxref\n%d\n%%%%EOF\n", fileoff);

    fclose(fp);

    sfree(filename);
}

object *new_object(objlist *list)
{
    object *obj = snew(object);

    obj->list = list;

    obj->main.text = NULL;
    obj->main.pos = obj->main.size = 0;
    obj->stream.text = NULL;
    obj->stream.pos = obj->stream.size = 0;

    obj->number = list->number++;

    obj->next = NULL;
    if (list->tail)
	list->tail->next = obj;
    else
	list->head = obj;
    list->tail = obj;

    obj->size = 0;
    obj->final = NULL;

    return obj;
}

void objtext(object *o, char const *text)
{
    rdaddsc(&o->main, text);
}

void objstream_len(object *o, char const *text, size_t len)
{
    rdaddsn(&o->stream, text, len);
}

void objstream(object *o, char const *text)
{
    rdaddsc(&o->stream, text);
}

static void objref(object *o, object *dest)
{
    char buf[40];
    sprintf(buf, "%d 0 R", dest->number);
    rdaddsc(&o->main, buf);
}

static void objdest(object *o, page_data *p) {
    objtext(o, "[");
    objref(o, (object *)p->spare);
    objtext(o, "/XYZ null null null]");
}

static char const * const stdfonts[] = {
    "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique","Helvetica-BoldOblique",
    "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique",
    "Symbol", "ZapfDingbats"
};

static int is_std_font(char const *name) {
    unsigned i;
    for (i = 0; i < lenof(stdfonts); i++)
	if (strcmp(name, stdfonts[i]) == 0)
	    return TRUE;
    return FALSE;
}

static void make_pages_node(object *node, object *parent, page_data *first,
			    page_data *last, object *resources,
			    object *mediabox)
{
    int count;
    page_data *page;
    char buf[80];

    objtext(node, "<<\n/Type /Pages\n");
    if (parent) {
	objtext(node, "/Parent ");
	objref(node, parent);
	objtext(node, "\n");
    }

    /*
     * Count the pages in this stretch, to see if there are few
     * enough to reference directly.
     */
    count = 0;
    for (page = first; page; page = page->next) {
	count++;
	if (page == last)
	    break;
    }

    sprintf(buf, "/Count %d\n/Kids [\n", count);
    objtext(node, buf);

    if (count > TREE_BRANCH) {
	int i;
	page_data *thisfirst, *thislast;

	page = first;

	for (i = 0; i < TREE_BRANCH; i++) {
	    int number = (i+1) * count / TREE_BRANCH - i * count / TREE_BRANCH;
	    thisfirst = page;
	    while (number--) {
		thislast = page;
		page = page->next;
	    }

	    if (thisfirst == thislast) {
		objref(node, (object *)thisfirst->spare);
		objtext((object *)thisfirst->spare, "/Parent ");
		objref((object *)thisfirst->spare, node);
		objtext((object *)thisfirst->spare, "\n");
	    } else {
		object *newnode = new_object(node->list);
		make_pages_node(newnode, node, thisfirst, thislast,
				NULL, NULL);
		objref(node, newnode);
	    }
	    objtext(node, "\n");
	}

	assert(thislast == last || page == NULL);

    } else {
	for (page = first; page; page = page->next) {
	    objref(node, (object *)page->spare);
	    objtext(node, "\n");
	    objtext((object *)page->spare, "/Parent ");
	    objref((object *)page->spare, node);
	    objtext((object *)page->spare, "\n");
	    if (page == last)
		break;
	}
    }

    objtext(node, "]\n");

    if (resources) {
	objtext(node, "/Resources ");
	objref(node, resources);
	objtext(node, "\n");
    }
    if (mediabox) {
	objtext(node, "/MediaBox ");
	objref(node, mediabox);
	objtext(node, "\n");
    }

    objtext(node, ">>\n");
}

/*
 * In text on the page, PDF uses the PostScript font model, which
 * means that glyphs are identified by PS strings and hence font
 * encoding can be managed independently of the supplied encoding
 * of the font. However, in the document outline, the PDF spec
 * encodes in either PDFDocEncoding (a custom superset of
 * ISO-8859-1) or UTF-16BE.
 */
char *pdf_outline_convert(wchar_t *s, int *len) {
    char *ret;

    ret = utoa_careful_dup(s, CS_PDF);

    /*
     * Very silly special case: if the returned string begins with
     * FE FF, then the PDF reader will mistake it for a UTF-16BE
     * string. So in this case we give up on PDFDocEncoding and
     * encode it in UTF-16 straight away.
     */
    if (ret && ret[0] == '\xFE' && ret[1] == '\xFF') {
	sfree(ret);
	ret = NULL;
    }

    if (!ret) {
	ret = utoa_dup_len(s, CS_UTF16BE, len);
    } else {
	*len = strlen(ret);
    }

    return ret;
}

static int make_outline(object *parent, outline_element *items, int n,
			int open)
{
    int level, totalcount = 0;
    outline_element *itemp;
    object *curr, *prev = NULL, *first = NULL, *last = NULL;

    assert(n > 0);

    level = items->level;

    while (n > 0) {
	char *title;
	int titlelen;

	/*
	 * Here we expect to be sitting on an item at the given
	 * level. So we start by constructing an outline entry for
	 * that item.
	 */
	assert(items->level == level);

	title = pdf_outline_convert(items->pdata->outline_title, &titlelen);

	totalcount++;
	curr = new_object(parent->list);
	if (!first) first = curr;
	last = curr;
	objtext(curr, "<<\n/Title ");
	pdf_string_len(objtext, curr, title, titlelen);
	sfree(title);
	objtext(curr, "\n/Parent ");
	objref(curr, parent);
	objtext(curr, "\n/Dest");
	objdest(curr, items->pdata->first->page);
	objtext(curr, "\n");
	if (prev) {
	    objtext(curr, "/Prev ");
	    objref(curr, prev);
	    objtext(curr, "\n");

	    objtext(prev, "/Next ");
	    objref(prev, curr);
	    objtext(prev, "\n>>\n");
	}
	prev = curr;

	items++, n--;
	for (itemp = items; itemp < items+n && itemp->level > level;
	     itemp++);

	if (itemp > items) {
	    char buf[80];
	    int count = make_outline(curr, items, itemp - items, FALSE);
	    if (!open)
		count = -count;
	    else
		totalcount += count;
	    sprintf(buf, "/Count %d\n", count);
	    objtext(curr, buf);
	}

	n -= itemp - items;
	items = itemp;
    }
    objtext(prev, ">>\n");

    assert(first && last);
    objtext(parent, "/First ");
    objref(parent, first);
    objtext(parent, "\n/Last ");
    objref(parent, last);
    objtext(parent, "\n");

    return totalcount;
}

static int pdf_versionid(FILE *fp, word *words)
{
    int ret;

    ret = fprintf(fp, "%% ");

    for (; words; words = words->next) {
	char *text;
	int type;

	switch (words->type) {
	  case word_HyperLink:
	  case word_HyperEnd:
	  case word_UpperXref:
	  case word_LowerXref:
	  case word_XrefEnd:
	  case word_IndexRef:
	    continue;
	}

	type = removeattr(words->type);

	switch (type) {
	  case word_Normal:
	    text = utoa_dup(words->text, CS_ASCII);
	    break;
	  case word_WhiteSpace:
	    text = dupstr(" ");
	    break;
	  case word_Quote:
	    text = dupstr("'");
	    break;
	}

	fputs(text, fp);
	ret += strlen(text);
	sfree(text);
    }

    ret += fprintf(fp, "\n");

    return ret;
}

static void pdf_string_len(void (*add)(object *, char const *),
			   object *o, char const *str, int len)
{
    char const *p;

    add(o, "(");
    for (p = str; len > 0; p++, len--) {
	char c[10];
	if (*p < ' ' || *p > '~') {
	    sprintf(c, "\\%03o", 0xFF & (int)*p);
	} else {
	    int n = 0;
	    if (*p == '\\' || *p == '(' || *p == ')')
		c[n++] = '\\';
	    c[n++] = *p;
	    c[n] = '\0';
	}
	add(o, c);
    }
    add(o, ")");
}

static void pdf_string(void (*add)(object *, char const *),
		       object *o, char const *str)
{
    pdf_string_len(add, o, str, strlen(str));
}
