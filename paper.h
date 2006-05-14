/*
 * Paper printing definitions.
 * 
 * This header file defines data structures and constants which are
 * shared between bk_paper.c and its clients bk_ps.c and bk_pdf.c.
 */

#ifndef HALIBUT_PAPER_H
#define HALIBUT_PAPER_H

/* Number of internal units per PostScript point. */
#define UNITS_PER_PT 1000
#define FUNITS_PER_PT 1000.0

typedef struct document_Tag document;
typedef struct kern_pair_Tag kern_pair;
typedef struct font_info_Tag font_info;
typedef struct font_data_Tag font_data;
typedef struct font_encoding_Tag font_encoding;
typedef struct font_list_Tag font_list;
typedef struct para_data_Tag para_data;
typedef struct line_data_Tag line_data;
typedef struct page_data_Tag page_data;
typedef struct subfont_map_entry_Tag subfont_map_entry;
typedef struct text_fragment_Tag text_fragment;
typedef struct xref_Tag xref;
typedef struct xref_dest_Tag xref_dest;
typedef struct rect_Tag rect;
typedef struct outline_element_Tag outline_element;

/*
 * This data structure represents the overall document, in the form
 * it will be given to the client backends.
 */
struct document_Tag {
    int paper_width, paper_height;
    font_list *fonts;
    page_data *pages;
    outline_element *outline_elements;
    int n_outline_elements;
};

/*
 * This data structure represents a kerning pair within a font.
 */
struct kern_pair_Tag {
    /* Glyph indices, in font_data.glyphs. */
    unsigned short left, right;
    /* Kern amount, in internal units. */
    int kern;
};

/*
 * This data structure holds static information about a font that doesn't
 * depend on the particular document.  It gets generated when the font's
 * metrics are read in.
 */

font_info *all_fonts;

struct font_info_Tag {
    font_info *next;
    /*
     * Specify the PostScript name of the font and its point size.
     */
    const char *name;
    /*
     * The file containing this font, if any.
     */
    FILE *fp;
    /*
     * An array of pointers to the available glyph names, and their
     * corresponding character widths. These two arrays have
     * parallel indices.
     */
    int nglyphs;
    const char *const *glyphs;
    const int *widths;
    /*
     * Glyph indices sorted into glyph-name order, for name-to-index
     * mapping.
     */
    unsigned short *glyphsbyname;
    /* A tree of kern_pairs */
    tree234 *kerns;
    /*
     * For reasonably speedy lookup, we set up a 65536-element
     * table representing the Unicode BMP (I can conveniently
     * restrict myself to the BMP for the moment since I happen to
     * know that no glyph in the Adobe Glyph List falls outside
     * it), whose elements are indices into the above two arrays.
     */
    unsigned short bmp[65536];
    /*
     * Various bits of metadata needed for the /FontDescriptor dictionary
     * in PDF.
     */
    float fontbbox[4];
    float capheight;
    float xheight;
    float ascent;
    float descent;
    float stemv;
    float stemh;
    float italicangle;
};

/*
 * This structure holds the information about how a font is used
 * in a document.
 */
struct font_data_Tag {
    font_info const *info;
    /*
     * At some point I'm going to divide the font into sub-fonts
     * with largely non-overlapping encoding vectors. This array
     * will track which glyphs go into which subfonts. Also here I
     * keep track of the latest subfont of any given font, so I can
     * go back and extend its encoding.
     */
    subfont_map_entry *subfont_map;
    font_encoding *latest_subfont;
    /*
     * The font list to which this font belongs.
     */
    font_list *list;
};

struct subfont_map_entry_Tag {
    font_encoding *subfont;
    unsigned char position;
};

/*
 * This data structure represents a sub-font: a font with an
 * encoding vector.
 */
struct font_encoding_Tag {
    font_encoding *next;

    char *name;			       /* used by client backends */

    font_data *font;		       /* the parent font structure */
    const char *vector[256];	       /* the actual encoding vector */
    int indices[256];		       /* indices back into main font struct */
    wchar_t to_unicode[256];	       /* PDF will want to know this */
    int free_pos;		       /* space left to extend encoding */
};

/*
 * This data structure represents the overall list of sub-fonts in
 * the whole document.
 */
struct font_list_Tag {
    font_encoding *head;
    font_encoding *tail;
};

/*
 * Constants defining array indices for the various fonts used in a
 * paragraph.
 */
enum {
    FONT_NORMAL,
    FONT_EMPH,
    FONT_CODE,
    NFONTS
};

/*
 * This is the data structure which is stored in the private_data
 * field of each paragraph. It divides the paragraph up into a
 * linked list of lines, while at the same time providing for those
 * lines to be linked together into a much longer list spanning the
 * whole document for page-breaking purposes.
 */

struct para_data_Tag {
    para_data *next;
    /*
     * Data about the fonts used in this paragraph. Indices are the
     * FONT_* constants defined above.
     */
    font_data *fonts[NFONTS];
    int sizes[NFONTS];
    /*
     * Pointers to the first and last line of the paragraph. The
     * line structures are linked into a list, which runs from
     * `first' to `last' as might be expected. However, the list
     * does not terminate there: first->prev will end up pointing
     * to the last line of the previous paragraph in most cases,
     * and likewise last->next will point to the first line of the
     * next paragraph.
     */
    line_data *first;		       /* first line in paragraph */
    line_data *last;		       /* last line in paragraph */
    /*
     * Some paragraphs have associated graphics; currently this is
     * nothing more complex than a single black rectangle.
     */
    enum {
	RECT_NONE, RECT_CHAPTER_UNDERLINE, RECT_RULE
    } rect_type;
    /*
     * We left- and right-justify in special circumstances.
     */
    enum {
	JUST, LEFT, RIGHT
    } justification;
    /*
     * For constructing the page outline.
     */
    int outline_level;		       /* 0=title 1=C 2=H 3=S 4=S2... */
    wchar_t *outline_title;
    /*
     * For adding the page number of a contents entry afterwards.
     */
    paragraph *contents_entry;
};

struct line_data_Tag {
    /*
     * The parent paragraph.
     */
    para_data *pdata;
    /*
     * Pointers to join lines into a linked list.
     */
    line_data *prev;
    line_data *next;
    /*
     * The extent of the text displayed on this line. Also mention
     * its starting x position, and by how much the width of spaces
     * needs to be adjusted for paragraph justification.
     * 
     * (Unlike most of the `last' pointers defined in this file,
     * this `end' pointer points to the word _after_ the last one
     * that should be displayed on the line. This is how it's
     * returned from wrap_para().)
     */
    word *first;
    word *end;
    int xpos;
    int hshortfall, nspaces;	       /* for justifying paragraphs */
    int real_shortfall;
    /*
     * Auxiliary text: a section number in a margin, or a list item
     * bullet or number. Also mention where to display this text
     * relative to the left margin.
     */
    word *aux_text;
    word *aux_text_2;
    int aux_left_indent;
    /*
     * This line might have a non-negotiable page break before it.
     * Also there will be space required above and below it; also I
     * store the physical line height (defined as the maximum of
     * the heights of the three fonts in the pdata) because it's
     * easier than looking it up repeatedly during page breaking.
     */
    int page_break;
    int space_before;
    int space_after;
    int line_height;
    /*
     * Penalties for page breaking before or after this line.
     */
    int penalty_before, penalty_after;
    /*
     * These fields are used in the page breaking algorithm.
     */
    int *bestcost;
    int *vshortfall, *text, *space;
    line_data **page_last;	       /* last line on a page starting here */
    /*
     * After page breaking, we can assign an actual y-coordinate on
     * the page to each line. Also we store a pointer back to the
     * page structure itself.
     */
    int ypos;
    page_data *page;
};

/*
 * This data structure is constructed to describe each page of the
 * printed output.
 */
struct page_data_Tag {
    /*
     * Pointers to join pages into a linked list.
     */
    page_data *prev;
    page_data *next;
    /*
     * The set of lines displayed on this page.
     */
    line_data *first_line;
    line_data *last_line;
    /*
     * After text rendering: the set of actual pieces of text
     * needing to be displayed on this page.
     */
    text_fragment *first_text;
    text_fragment *last_text;
    /*
     * Cross-references.
     */
    xref *first_xref;
    xref *last_xref;
    /*
     * Rectangles to be drawn. (These are currently only used for
     * underlining chapter titles and drawing horizontal rules.)
     */
    rect *first_rect;
    rect *last_rect;
    /*
     * The page number, as a string.
     */
    wchar_t *number;
    /*
     * This spare pointer field is for use by the client backends.
     */
    void *spare;
};

struct text_fragment_Tag {
    text_fragment *next;
    int x, y;
    font_encoding *fe;
    int fontsize;
    char *text;
    int width;
};

struct xref_dest_Tag {
    enum { NONE, PAGE, URL } type;
    page_data *page;
    char *url;
};

struct xref_Tag {
    xref *next;
    int lx, rx, ty, by;
    xref_dest dest;
};

struct rect_Tag {
    rect *next;
    int x, y, w, h;
};

struct outline_element_Tag {
    int level;			       /* 0=title 1=C 2=H 3=S 4=S2... */
    para_data *pdata;
};

/*
 * Functions exported from bk_paper.c
 */
int kern_cmp(void *, void *); /* use when setting up kern_pairs */
void font_index_glyphs(font_info *fi);
int find_glyph(font_info *fi, char const *name);


/*
 * Functions and data exported from psdata.c.
 */
wchar_t ps_glyph_to_unicode(char const *glyph);
extern const char *const ps_std_glyphs[];
void init_std_fonts(void);
const int *ps_std_font_widths(char const *fontname);
const kern_pair *ps_std_font_kerns(char const *fontname);

#endif
