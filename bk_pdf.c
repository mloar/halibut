/*
 * PDF backend for Halibut
 */

#include <assert.h>
#include "halibut.h"
#include "paper.h"

#define TREE_BRANCH 2		       /* max branching factor in page tree */

paragraph *pdf_config_filename(char *filename)
{
    paragraph *p;
    wchar_t *ufilename, *up;
    int len;

    p = mknew(paragraph);
    memset(p, 0, sizeof(*p));
    p->type = para_Config;
    p->next = NULL;
    p->fpos.filename = "<command line>";
    p->fpos.line = p->fpos.col = -1;

    ufilename = ufroma_dup(filename);
    len = ustrlen(ufilename) + 2 + lenof(L"pdf-filename");
    p->keyword = mknewa(wchar_t, len);
    up = p->keyword;
    ustrcpy(up, L"pdf-filename");
    up = uadv(up);
    ustrcpy(up, ufilename);
    up = uadv(up);
    *up = L'\0';
    assert(up - p->keyword < len);
    sfree(ufilename);

    return p;
}

typedef struct object_Tag object;
typedef struct objlist_Tag objlist;

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

static object *new_object(objlist *list);
static void objtext(object *o, char const *text);
static void objstream(object *o, char const *text);
static void pdf_string(void (*add)(object *, char const *),
		       object *, char const *);
static void objref(object *o, object *dest);

static void make_pages_node(object *node, object *parent, page_data *first,
			    page_data *last, object *resources);
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
    object *o, *cat, *outlines, *pages, *resources;
    int fileoff;

    IGNORE(keywords);
    IGNORE(idx);

    filename = dupstr("output.pdf");
    for (p = sourceform; p; p = p->next) {
	if (p->type == para_Config && p->parent) {
	    if (!ustricmp(p->keyword, L"pdf-filename")) {
		sfree(filename);
		filename = utoa_dup(uadv(p->keyword));
	    }
	}
    }

    olist.head = olist.tail = NULL;
    olist.number = 1;

    cat = new_object(&olist);
    outlines = new_object(&olist);
    pages = new_object(&olist);
    resources = new_object(&olist);

    /*
     * The catalogue just contains references to the outlines and
     * pages objects.
     */
    objtext(cat, "<<\n/Type /Catalog\n/Outlines ");
    objref(cat, outlines);
    objtext(cat, "\n/Pages ");
    objref(cat, pages);
    objtext(cat, "\n/PageMode /UseOutlines\n>>\n");

    /*
     * Set up the resources dictionary, which mostly means
     * providing all the font objects and names to call them by.
     */
    font_index = 0;
    objtext(resources, "<<\n/Font <<\n");
    for (fe = doc->fonts->head; fe; fe = fe->next) {
	char fname[40];
	int i;
	object *font;

	sprintf(fname, "f%d", font_index++);
	fe->name = dupstr(fname);

	font = new_object(&olist);

	objtext(resources, "/");
	objtext(resources, fe->name);
	objtext(resources, " ");
	objref(resources, font);
	objtext(resources, "\n");

	objtext(font, "<<\n/Type /Font\n/Subtype /Type1\n/Name /");
	objtext(font, fe->name);
	objtext(font, "\n/BaseFont /");
	objtext(font, fe->font->name);
	objtext(font, "\n/Encoding <<\n/Type /Encoding\n/Differences [");

	for (i = 0; i < 256; i++) {
	    char buf[20];
	    if (!fe->vector[i])
		continue;
	    sprintf(buf, "\n%d /", i);
	    objtext(font, buf);
	    objtext(font, fe->vector[i] ? fe->vector[i] : ".notdef");
	}

	objtext(font, "\n]\n>>\n");

	{
	    object *widths = new_object(&olist);
	    objtext(font, "/FirstChar 0\n/LastChar 255\n/Widths ");
	    objref(font, widths);
	    objtext(font, "\n");
	    objtext(widths, "[\n");
	    for (i = 0; i < 256; i++) {
		char buf[80];
		double width;
		if (fe->indices[i] < 0)
		    width = 0.0;
		else
		    width = fe->font->widths[fe->indices[i]];
		sprintf(buf, "%g\n", 1000.0 * width / 4096.0);
		objtext(widths, buf);
	    }
	    objtext(widths, "]\n");
	}

	objtext(font, ">>\n");
    }
    objtext(resources, ">>\n>>\n");

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
    make_pages_node(pages, NULL, doc->pages, NULL, resources);

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
	 * So we don't need a /Resources entry here.
	 */
	sprintf(buf, "/MediaBox [0 0 %g %g]\n",
		doc->paper_width / 4096.0, doc->paper_height / 4096.0);
	objtext(opage, buf);

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
	    sprintf(buf, "%g %g %g %g re f\n", r->x / 4096.0,
		    r->y / 4096.0, r->w / 4096.0, r->h / 4096.0);
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
			    frag->x/4096.0, frag->y/4096.0);
		} else {
		    sprintf(buf, "%g %g Td ",
			    (frag->x - lx)/4096.0, (frag->y - ly)/4096.0);
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
				    (4096.0 * frag->fontsize));
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
		object *annot;
		char buf[256];

		annot = new_object(&olist);
		objref(opage, annot);
		objtext(opage, "\n");

		objtext(annot, "<<\n/Type /Annot\n/Subtype /Link\n/Rect [");
		sprintf(buf, "%g %g %g %g",
			xr->lx / 4096.0, xr->by / 4096.0,
			xr->rx / 4096.0, xr->ty / 4096.0);
		objtext(annot, buf);
		objtext(annot, "]\n/Border [0 0 0]\n");

		if (xr->dest.type == PAGE) {
		    objtext(annot, "/Dest [");
		    objref(annot, (object *)xr->dest.page->spare);
		    objtext(annot, " /XYZ null null null]\n");
		} else {
		    objtext(annot, "/A <<\n/Type /Action\n/S /URI\n/URI ");
		    pdf_string(objtext, annot, xr->dest.url);
		    objtext(annot, "\n>>\n");
		}

		objtext(annot, ">>\n");
	    }

	    objtext(opage, "]\n");
	}

	objtext(opage, ">>\n");
    }

    /*
     * Set up the outlines dictionary.
     */
    {
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

	sprintf(text, "%d 0 obj\n", o->number);
	rdaddsc(&rs, text);

	if (!o->main.text && o->stream.text) {
	    sprintf(text, "<<\n/Length %d\n>>\n", o->stream.pos);
	    rdaddsc(&o->main, text);
	}

	assert(o->main.text);
	rdaddsc(&rs, o->main.text);
	sfree(o->main.text);

	if (rs.text[rs.pos-1] != '\n')
	    rdaddc(&rs, '\n');

	if (o->stream.text) {
	    /*
	     * FIXME: If we ever start compressing stream data then
	     * it will have zero bytes in it, so we'll have to be
	     * more careful than this.
	     */
	    rdaddsc(&rs, "stream\n");
	    rdaddsc(&rs, o->stream.text);
	    rdaddsc(&rs, "\nendstream\n");
	    sfree(o->stream.text);
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
     * well, simply in PDF comments.
     */
    fileoff = fprintf(fp, "%%PDF-1.3\n");
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
    fprintf(fp, "trailer\n<<\n/Size %d\n/Root %d 0 R\n>>\n",
	    olist.tail->number + 1, cat->number);
    fprintf(fp, "startxref\n%d\n%%%%EOF\n", fileoff);

    fclose(fp);

    sfree(filename);
}

static object *new_object(objlist *list)
{
    object *obj = mknew(object);

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

static void objtext(object *o, char const *text)
{
    rdaddsc(&o->main, text);
}

static void objstream(object *o, char const *text)
{
    rdaddsc(&o->stream, text);
}

static void objref(object *o, object *dest)
{
    char buf[40];
    sprintf(buf, "%d 0 R", dest->number);
    rdaddsc(&o->main, buf);
}

static void make_pages_node(object *node, object *parent, page_data *first,
			    page_data *last, object *resources)
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
		make_pages_node(newnode, node, thisfirst, thislast, NULL);
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

    objtext(node, ">>\n");
}

/*
 * In text on the page, PDF uses the PostScript font model, which
 * means that glyphs are identified by PS strings and hence font
 * encoding can be managed independently of the supplied encoding
 * of the font. However, in the document outline, the PDF spec
 * simply asks for ordinary text strings without mentioning what
 * character set they are supposed to be interpreted in.
 * 
 * Therefore, for the moment, I'm going to assume they're US-ASCII
 * only. If anyone knows better, they should let me know :-/
 */
static int pdf_convert(wchar_t *s, char **result) {
    int doing = (result != 0);
    int ok = TRUE;
    char *p = NULL;
    int plen = 0, psize = 0;

    for (; *s; s++) {
	wchar_t c = *s;
	char outc;

	if (c >= 32 && c <= 126) {
	    /* Char is OK. */
	    outc = (char)c;
	} else {
	    /* Char is not OK. */
	    ok = FALSE;
	    outc = 0xBF;	       /* approximate the good old DEC `uh?' */
	}
	if (doing) {
	    if (plen >= psize) {
		psize = plen + 256;
		p = resize(p, psize);
	    }
	    p[plen++] = outc;
	}
    }
    if (doing) {
	p = resize(p, plen+1);
	p[plen] = '\0';
	*result = p;
    }
    return ok;
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

	/*
	 * Here we expect to be sitting on an item at the given
	 * level. So we start by constructing an outline entry for
	 * that item.
	 */
	assert(items->level == level);

	pdf_convert(items->pdata->outline_title, &title);

	totalcount++;
	curr = new_object(parent->list);
	if (!first) first = curr;
	last = curr;
	objtext(curr, "<<\n/Title ");
	pdf_string(objtext, curr, title);
	objtext(curr, "\n/Parent ");
	objref(curr, parent);
	objtext(curr, "\n/Dest [");
	objref(curr, (object *)items->pdata->first->page->spare);
	objtext(curr, " /XYZ null null null]\n");
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
	    text = utoa_dup(words->text);
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

static void pdf_string(void (*add)(object *, char const *),
		       object *o, char const *str)
{
    char const *p;

    add(o, "(");
    for (p = str; *p; p++) {
	char c[2];
	if (*p == '\\' || *p == '(' || *p == ')')
	    add(o, "\\");
	c[0] = *p;
	c[1] = '\0';
	add(o, c);
    }
    add(o, ")");
}
