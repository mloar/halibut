/*
 * winhelp.c   a module to generate Windows .HLP files
 * 
 * Documentation of the .HLP file format comes from the excellent
 * HELPFILE.TXT, published alongside the Help decompiler HELPDECO
 * by Manfred Winterhoff. This code would not have been possible
 * without his efforts. Many thanks.
 */

/*
 * Still to do:
 * 
 *  - Go back and look at the KW* sections, which should allow us
 *    to add indexing support.
 *  - nonbreaking spaces and hyphens will be needed.
 *  - tabs, and tab stop settings in the paragraphinfo.
 *  - browse sequence support.
 * 
 * Potential future features:
 * 
 *  - perhaps LZ77 compression? This appears to cause a phase order
 *    problem: it's hard to do the compression until the data to be
 *    compressed is finalised, and yet you can't finalise the data
 *    to be compressed until you know how much of it is going into
 *    which TOPICBLOCK in order to work out the offsets in the
 *    topic headers - for which you have to have already done the
 *    compression. Perhaps the thing to do is to implement an LZ77
 *    compressor that can guarantee to leave particular bytes in
 *    the stream as literals, and then go back and fix the offsets
 *    up later. Not pleasant.
 * 
 *  - tables might be nice.
 * 
 * Unlikely future features:
 * 
 *  - Phrase compression sounds harder. It's reasonably easy
 *    (though space-costly) to analyse all the text in the file to
 *    determine the one key phrase which would save most space if
 *    replaced by a reference everywhere it appears; but finding
 *    the _1024_ most effective phrases seems much harder since a
 *    naive analysis might find lots of phrases that all overlap
 *    (so you wouldn't get the saving you expected, as after taking
 *    out the first phrase the rest would never crop up). In
 *    addition, MS hold US patent number 4955066 which may cover
 *    phrase compression, so perhaps it's best just to leave it.
 * 
 * Cleanup work:
 * 
 *  - outsource the generation of the |FONT section. Users should
 *    specify their own font descriptors and then just pass a font
 *    descriptor number in to whlp_set_font. This will also mean
 *    removing the WHLP_FONT_* enum in winhelp.h.
 * 
 *  - find out what should happen if a single topiclink crosses
 *    _two_ topicblock boundaries.
 * 
 *  - What is the BlockSize in a topic header (first 4 bytes of
 *    LinkData1 in a type 2 record) supposed to mean? How on earth
 *    is it measured? The help file doesn't become perceptibly
 *    corrupt if I frob it randomly; and on some occasions taking a
 *    bit _out_ of the help file _increases_ that value. I have a
 *    feeling it's completely made up and/or vestigial, so for the
 *    moment I'm just making up a plausible value as I go along.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdarg.h>

#include "winhelp.h"
#include "tree234.h"

/* ----------------------------------------------------------------------
 * FIXME: remove this whole section when we integrate to Buttress.
 */
#define smalloc malloc
#define srealloc realloc
#define sfree free
#define mknew(type) ( (type *) smalloc (sizeof (type)) )
#define mknewa(type, number) ( (type *) smalloc ((number) * sizeof (type)) )
#define resize(array, len) ( srealloc ((array), (len) * sizeof (*(array))) )
#define lenof(array) ( sizeof(array) / sizeof(*(array)) )
char *dupstr(char *s) { char *r = mknewa(char, 1+strlen(s)); strcpy(r,s); return r; }
/* ------------------------------------------------------------------- */

#define GET_32BIT_LSB_FIRST(cp) \
  (((unsigned long)(unsigned char)(cp)[0]) | \
  ((unsigned long)(unsigned char)(cp)[1] << 8) | \
  ((unsigned long)(unsigned char)(cp)[2] << 16) | \
  ((unsigned long)(unsigned char)(cp)[3] << 24))

#define PUT_32BIT_LSB_FIRST(cp, value) do { \
  (cp)[0] = 0xFF & (value); \
  (cp)[1] = 0xFF & ((value) >> 8); \
  (cp)[2] = 0xFF & ((value) >> 16); \
  (cp)[3] = 0xFF & ((value) >> 24); } while (0)

#define GET_16BIT_LSB_FIRST(cp) \
  (((unsigned long)(unsigned char)(cp)[0]) | \
  ((unsigned long)(unsigned char)(cp)[1] << 8))

#define PUT_16BIT_LSB_FIRST(cp, value) do { \
  (cp)[0] = 0xFF & (value); \
  (cp)[1] = 0xFF & ((value) >> 8); } while (0)

#define MAX_PAGE_SIZE 0x800	       /* max page size in any B-tree */
#define TOPIC_BLKSIZE 4096	       /* implied by version/flags combo */

struct file {
    char *name;			       /* file name, will need freeing */
    unsigned char *data;	       /* file data, will need freeing */
    int pos;			       /* position for adding data */
    int len;			       /* # of meaningful bytes in data */
    int size;			       /* # of allocated bytes in data */
    int fileoffset;		       /* offset in the real .HLP file */
};

struct topiclink {
    int topicoffset, topicpos;	       /* for referencing from elsewhere */
    int recordtype;
    int len1, len2;
    unsigned char *data1, *data2;
    struct topiclink *browse_next, *browse_prev;
    struct topiclink *nonscroll, *scroll, *nexttopic;
    int block_size;		       /* for the topic header - *boggle* */
};

typedef struct WHLP_TOPIC_tag context;
struct WHLP_TOPIC_tag {
    char *name;			       /* needs freeing */
    unsigned long hash;
    struct topiclink *link;	       /* this provides TOPICOFFSET */
    char *title;		       /* needs freeing */
};

struct WHLP_tag {
    tree234 *files;		       /* stores `struct file' */
    tree234 *pre_contexts;	       /* stores `context' */
    tree234 *contexts;		       /* also stores `context' */
    tree234 *titles;		       /* _also_ stores `context' */
    tree234 *text;		       /* stores `struct topiclink' */
    struct file *contextfile;	       /* the |CONTEXT internal file */
    struct file *titlefile;	       /* the |TTLBTREE internal file */
    struct file *systemfile;	       /* the |SYSTEM internal file */
    context *ptopic;		       /* primary topic */
    struct topiclink *prevtopic;       /* to link type-2 records together */
    struct topiclink *link;	       /* while building a topiclink */
    unsigned char linkdata1[TOPIC_BLKSIZE];   /* while building a topiclink */
    unsigned char linkdata2[TOPIC_BLKSIZE];   /* while building a topiclink */
    int topicblock_remaining;	       /* while building |TOPIC section */
    int lasttopiclink;		       /* while building |TOPIC section */
    int firsttopiclink_offset;	       /* while building |TOPIC section */
    int lasttopicstart;		       /* while building |TOPIC section */
    int para_flags;
    int para_attrs[7];
};

/* Functions to return the index and leaf data for B-tree contents. */
typedef int (*bt_index_fn)(const void *item, unsigned char *outbuf);
typedef int (*bt_leaf_fn)(const void *item, unsigned char *outbuf);

/* Forward references. */
static void whlp_para_reset(WHLP h);
static struct file *whlp_new_file(WHLP h, char *name);
static void whlp_file_add(struct file *f, const void *data, int len);
static void whlp_file_add_char(struct file *f, int data);
static void whlp_file_add_short(struct file *f, int data);
static void whlp_file_add_long(struct file *f, int data);
static void whlp_file_fill(struct file *f, int len);
static void whlp_file_seek(struct file *f, int pos, int whence);
static int whlp_file_offset(struct file *f);

/* ----------------------------------------------------------------------
 * Fiddly little functions: B-tree compare, index and leaf functions.
 */

/* The master index maps file names to help-file offsets. */

static int filecmp(void *av, void *bv)
{
    const struct file *a = (const struct file *)av;
    const struct file *b = (const struct file *)bv;
    return strcmp(a->name, b->name);
}

static int fileindex(const void *av, unsigned char *outbuf)
{
    const struct file *a = (const struct file *)av;
    int len = 1+strlen(a->name);
    memcpy(outbuf, a->name, len);
    return len;
}

static int fileleaf(const void *av, unsigned char *outbuf)
{
    const struct file *a = (const struct file *)av;
    int len = 1+strlen(a->name);
    memcpy(outbuf, a->name, len);
    PUT_32BIT_LSB_FIRST(outbuf+len, a->fileoffset);
    return len+4;
}

/* The |CONTEXT internal file maps help context hashes to TOPICOFFSETs. */

static int ctxcmp(void *av, void *bv)
{
    const context *a = (const context *)av;
    const context *b = (const context *)bv;
    if ((signed long)a->hash < (signed long)b->hash)
	return -1;
    if ((signed long)a->hash > (signed long)b->hash)
	return +1;
    return 0;
}

static int ctxindex(const void *av, unsigned char *outbuf)
{
    const context *a = (const context *)av;
    PUT_32BIT_LSB_FIRST(outbuf, a->hash);
    return 4;
}

static int ctxleaf(const void *av, unsigned char *outbuf)
{
    const context *a = (const context *)av;
    PUT_32BIT_LSB_FIRST(outbuf, a->hash);
    PUT_32BIT_LSB_FIRST(outbuf+4, a->link->topicoffset);
    return 8;
}

/* The |TTLBTREE internal file maps TOPICOFFSETs to title strings. */

static int ttlcmp(void *av, void *bv)
{
    const context *a = (const context *)av;
    const context *b = (const context *)bv;
    if (a->link->topicoffset < b->link->topicoffset)
	return -1;
    if (a->link->topicoffset > b->link->topicoffset)
	return +1;
    return 0;
}

static int ttlindex(const void *av, unsigned char *outbuf)
{
    const context *a = (const context *)av;
    PUT_32BIT_LSB_FIRST(outbuf, a->link->topicoffset);
    return 4;
}

static int ttlleaf(const void *av, unsigned char *outbuf)
{
    const context *a = (const context *)av;
    int slen;
    PUT_32BIT_LSB_FIRST(outbuf, a->link->topicoffset);
    slen = 1+strlen(a->title);
    memcpy(outbuf+4, a->title, slen);
    return 4+slen;
}

/* ----------------------------------------------------------------------
 * Manage help contexts and topics.
 */

/*
 * This is the code to compute the hash of a context name. Copied
 * straight from Winterhoff's documentation.
 */
unsigned long context_hash(char *context)
{
    signed char bytemapping[256] =
	"\x00\xD1\xD2\xD3\xD4\xD5\xD6\xD7\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF"
	"\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF"
	"\xF0\x0B\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA\xFB\xFC\xFD\x0C\xFF"
	"\x0A\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x0B\x0C\x0D\x0E\x0D"
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
	"\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5A\x5B\x5C\x5D\x5E\x5F"
	"\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6A\x6B\x6C\x6D\x6E\x6F"
	"\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7A\x7B\x7C\x7D\x7E\x7F"
	"\x80\x81\x82\x83\x0B\x85\x86\x87\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F"
	"\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F"
	"\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF"
	"\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF"
	"\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF";
    unsigned long hash;
    
    /* Sanity check the size of unsigned long */
    enum { assertion = 1 /
	    (((unsigned long)0xFFFFFFFF) + 2 == (unsigned long)1) };

    /*
     * The hash algorithm starts the hash at 0 and updates it with
     * each character. Therefore, logically, the hash of an empty
     * string should be 0 (it starts at 0 and is never updated);
     * but Winterhoff says it is in fact 1. Shouldn't matter, since
     * I never plan to use empty context names, but I'll stick the
     * special case in here anyway.
     */
    if (!*context)
	return 1;

    /*
     * Now compute the hash in the normal way.
     */
    hash = 0;
    while (*context) {
	hash = hash * 43 + bytemapping[(unsigned char)*context];
	context++;
    }
    return hash;
}

WHLP_TOPIC whlp_register_topic(WHLP h, char *context_name, char **clash)
{
    context *ctx = mknew(context);
    context *otherctx;

    if (context_name) {
	/*
	 * We have a context name, which means we can put this
	 * context straight into the `contexts' tree.
	 */
	ctx->name = dupstr(context_name);
	ctx->hash = context_hash(context_name);
	otherctx = add234(h->contexts, ctx);
	if (otherctx != ctx) {
	    /*
	     * Hash clash. Destroy the new context and return NULL,
	     * providing the clashing string.
	     */
	    sfree(ctx->name);
	    sfree(ctx);
	    if (clash) *clash = otherctx->name;
	    return NULL;
	}
    } else {
	/*
	 * We have no context name yet. Enter this into the
	 * pre_contexts tree of anonymous topics, which we will go
	 * through later and allocate unique context names and hash
	 * values.
	 */
	ctx->name = NULL;
	addpos234(h->pre_contexts, ctx, count234(h->pre_contexts));
    }
    return ctx;
}

void whlp_prepare(WHLP h)
{
    /*
     * We must go through pre_contexts and allocate a context ID to
     * each anonymous context, making sure it doesn't clash with
     * the existing contexts.
     * 
     * Our own context IDs will just be of the form `t00000001',
     * and we'll increment the number each time and skip over any
     * IDs that clash with existing context names.
     */
    int ctx_num = 0;
    context *ctx, *otherctx;

    while ( (ctx = index234(h->pre_contexts, 0)) != NULL ) {
	delpos234(h->pre_contexts, 0);
	ctx->name = mknewa(char, 20);
	do {
	    sprintf(ctx->name, "t%08d", ctx_num++);
	    ctx->hash = context_hash(ctx->name);
	    otherctx = add234(h->contexts, ctx);
	} while (otherctx != ctx);
    }
}

char *whlp_topic_id(WHLP_TOPIC topic)
{
    return topic->name;
}

void whlp_begin_topic(WHLP h, WHLP_TOPIC topic, char *title, ...)
{
    struct topiclink *link = mknew(struct topiclink);
    int len, slen;
    char *macro;
    va_list ap;

    link->nexttopic = NULL;
    if (h->prevtopic)
	h->prevtopic->nexttopic = link;
    h->prevtopic = link;

    link->browse_next = link->browse_prev =
	link->nonscroll = link->scroll = NULL;
    link->block_size = 0;

    link->recordtype = 2;	       /* topic header */
    link->len1 = 4*7;		       /* standard linkdata1 size */
    link->data1 = mknewa(unsigned char, link->len1);
    
    slen = strlen(title);
    assert(slen+1 <= TOPIC_BLKSIZE);
    memcpy(h->linkdata2, title, slen+1);
    len = slen+1;

    va_start(ap, title);
    while ( (macro = va_arg(ap, char *)) != NULL) {
	slen = strlen(macro);
	assert(len+slen+1 <= TOPIC_BLKSIZE);
	memcpy(h->linkdata2+len, macro, slen+1);
	len += slen+1;
    }
    va_end(ap);
    len--;			       /* lose the last \0 on the last macro */

    link->len2 = len;
    link->data2 = mknewa(unsigned char, link->len2);
    memcpy(link->data2, h->linkdata2, link->len2);

    topic->title = dupstr(title);
    topic->link = link;

    addpos234(h->text, link, count234(h->text));
}

/* ----------------------------------------------------------------------
 * Manage the actual generation of paragraph and text records.
 */

static void whlp_linkdata(WHLP h, int which, int c)
{
    int *len = (which == 1 ? &h->link->len1 : &h->link->len2);
    char *data = (which == 1 ? h->linkdata1 : h->linkdata2);
    assert(*len < TOPIC_BLKSIZE);
    data[(*len)++] = c;
}

static void whlp_linkdata_short(WHLP h, int which, int data)
{
    whlp_linkdata(h, which, data & 0xFF);
    whlp_linkdata(h, which, (data >> 8) & 0xFF);
}

static void whlp_linkdata_long(WHLP h, int which, int data)
{
    whlp_linkdata(h, which, data & 0xFF);
    whlp_linkdata(h, which, (data >> 8) & 0xFF);
    whlp_linkdata(h, which, (data >> 16) & 0xFF);
    whlp_linkdata(h, which, (data >> 24) & 0xFF);
}

static void whlp_linkdata_cushort(WHLP h, int which, int data)
{
    if (data <= 0x7F) {
	whlp_linkdata(h, which, data*2);
    } else {
	whlp_linkdata(h, which, 1 + (data%128 * 2));
	whlp_linkdata(h, which, data/128);
    }
}

static void whlp_linkdata_csshort(WHLP h, int which, int data)
{
    if (data >= -0x40 && data <= 0x3F)
	whlp_linkdata_cushort(h, which, data+64);
    else
	whlp_linkdata_cushort(h, which, data+16384);
}

static void whlp_linkdata_culong(WHLP h, int which, int data)
{
    if (data <= 0x7FFF) {
	whlp_linkdata_short(h, which, data*2);
    } else {
	whlp_linkdata_short(h, which, 1 + (data%32768 * 2));
	whlp_linkdata_short(h, which, data/32768);
    }
}

static void whlp_linkdata_cslong(WHLP h, int which, int data)
{
    if (data >= -0x4000 && data <= 0x3FFF)
	whlp_linkdata_culong(h, which, data+16384);
    else
	whlp_linkdata_culong(h, which, data+67108864);
}

static void whlp_para_reset(WHLP h)
{
    h->para_flags = 0;
}

void whlp_para_attr(WHLP h, int attr_id, int attr_param)
{
    if (attr_id >= WHLP_PARA_SPACEABOVE &&
	attr_id <= WHLP_PARA_FIRSTLINEINDENT) {
	h->para_flags |= 1 << attr_id;
	h->para_attrs[attr_id] = attr_param;
    } else if (attr_id == WHLP_PARA_ALIGNMENT) {
	h->para_flags &= ~0xC00;
	if (attr_param == WHLP_ALIGN_RIGHT)
	    h->para_flags |= 0x400;
	else if (attr_param == WHLP_ALIGN_CENTRE)
	    h->para_flags |= 0x800;
    }
}

void whlp_begin_para(WHLP h, int para_type)
{
    struct topiclink *link = mknew(struct topiclink);
    int i;

    /*
     * Clear these to NULL out of paranoia, although in records
     * that aren't type 2 they should never actually be needed.
     */
    link->nexttopic = NULL;
    link->browse_next = link->browse_prev =
	link->nonscroll = link->scroll = NULL;

    link->recordtype = 32;	       /* text record */

    h->link = link;
    link->len1 = link->len2 = 0;
    link->data1 = h->linkdata1;
    link->data2 = h->linkdata2;

    if (para_type == WHLP_PARA_NONSCROLL && h->prevtopic &&
	!h->prevtopic->nonscroll)
	h->prevtopic->nonscroll = link;
    if (para_type == WHLP_PARA_SCROLL && h->prevtopic &&
	!h->prevtopic->scroll)
	h->prevtopic->scroll = link;

    /*
     * Now we're ready to start accumulating stuff in linkdata1 and
     * linkdata2. Next we build up the paragraph info. Note that
     * the TopicSize (cslong: size of LinkData1 minus the topicsize
     * and topiclength fields) and TopicLength (cushort: size of
     * LinkData2) fields are missing; we will put those on when we
     * end the paragraph.
     */
    whlp_linkdata(h, 1, 0);	       /* must-be-0x00 */
    whlp_linkdata(h, 1, 0x80);	       /* must-be-0x80 */
    whlp_linkdata_short(h, 1, 0); /* Winterhoff says `id'; always 0 AFAICT */
    whlp_linkdata_short(h, 1, h->para_flags);
    for (i = WHLP_PARA_SPACEABOVE; i <= WHLP_PARA_FIRSTLINEINDENT; i++) {
	if (h->para_flags & (1<<i))
	    whlp_linkdata_csshort(h, 1, h->para_attrs[i]);
    }

    /*
     * Fine. Now we're ready to start writing actual text and
     * formatting commands.
     */
}

void whlp_set_font(WHLP h, int font_id)
{
    /*
     * Write a NUL into linkdata2 to cause the reader to flip over
     * to linkdata1 to see the formatting command.
     */
    whlp_linkdata(h, 2, 0);
    /*
     * Now the formatting command is 0x80 followed by a short.
     */
    whlp_linkdata(h, 1, 0x80);
    whlp_linkdata_short(h, 1, font_id);
}

void whlp_start_hyperlink(WHLP h, WHLP_TOPIC target)
{
    /*
     * Write a NUL into linkdata2.
     */
    whlp_linkdata(h, 2, 0);
    /*
     * Now the formatting command is 0xE3 followed by the context
     * hash.
     */
    whlp_linkdata(h, 1, 0xE3);
    whlp_linkdata_long(h, 1, target->hash);
}

void whlp_end_hyperlink(WHLP h)
{
    /*
     * Write a NUL into linkdata2.
     */
    whlp_linkdata(h, 2, 0);
    /*
     * Now the formatting command is 0x89.
     */
    whlp_linkdata(h, 1, 0x89);
}

void whlp_text(WHLP h, char *text)
{
    while (*text) {
	whlp_linkdata(h, 2, *text++);
    }
}

void whlp_end_para(WHLP h)
{
    int data1cut;

    /*
     * Round off the paragraph with 0x82 and 0xFF formatting
     * commands. Each requires a NUL in linkdata2.
     */
    whlp_linkdata(h, 2, 0);
    whlp_linkdata(h, 1, 0x82);
    whlp_linkdata(h, 2, 0);
    whlp_linkdata(h, 1, 0xFF);

    /*
     * Now finish up: create the header of linkdata1 (TopicLength
     * and TopicSize fields), allocate the real linkdata1 and
     * linkdata2 fields, and copy them out of the buffers in h.
     * Then insert the finished topiclink into the `text' tree, and
     * clean up.
     */
    data1cut = h->link->len1;
    whlp_linkdata_cslong(h, 1, data1cut);
    whlp_linkdata_cushort(h, 1, h->link->len2);

    h->link->data1 = mknewa(unsigned char, h->link->len1);
    memcpy(h->link->data1, h->linkdata1 + data1cut, h->link->len1 - data1cut);
    memcpy(h->link->data1 + h->link->len1 - data1cut, h->linkdata1, data1cut);
    h->link->data2 = mknewa(unsigned char, h->link->len2);
    memcpy(h->link->data2, h->linkdata2, h->link->len2);

    addpos234(h->text, h->link, count234(h->text));

    /* Hack: accumulate the `blocksize' parameter in the topic header. */
    if (h->prevtopic)
	h->prevtopic->block_size += 21 + h->link->len1 + h->link->len2;

    whlp_para_reset(h);
}

/* ----------------------------------------------------------------------
 * Manage the layout and generation of the |TOPIC section.
 */

static void whlp_topicsect_write(WHLP h, struct file *f, void *data, int len)
{
    unsigned char *p = (unsigned char *)data;

    if (h->topicblock_remaining <= 0) {
	/*
	 * Start a new block.
	 */
	whlp_file_add_long(f, h->lasttopiclink);
	h->firsttopiclink_offset = whlp_file_offset(f);
	whlp_file_add_long(f, -1L);    /* this will be filled in later */
	whlp_file_add_long(f, h->lasttopicstart);
	h->topicblock_remaining = TOPIC_BLKSIZE - 12;
    }
    while (len > 0) {
	int thislen = (h->topicblock_remaining < len ?
		       h->topicblock_remaining : len);
	whlp_file_add(f, p, thislen);
	p += thislen;
	len -= thislen;	
	h->topicblock_remaining -= thislen;
	if (len > 0 && h->topicblock_remaining <= 0) {
	    /*
	     * Start a new block.
	     */
	    whlp_file_add_long(f, h->lasttopiclink);
	    h->firsttopiclink_offset = whlp_file_offset(f);
	    whlp_file_add_long(f, -1L);    /* this will be filled in later */
	    whlp_file_add_long(f, h->lasttopicstart);
	    h->topicblock_remaining = TOPIC_BLKSIZE - 12;
	}
    }
}

static void whlp_topic_layout(WHLP h)
{
    int block, offset, pos;
    int i, nlinks, size;
    int topicnum;
    struct topiclink *link;
    struct file *f;

    /*
     * Create a final TOPICLINK containing no usable data.
     */
    link = mknew(struct topiclink);
    link->nexttopic = NULL;
    if (h->prevtopic)
	h->prevtopic->nexttopic = link;
    h->prevtopic = link;
    link->data1 = mknewa(unsigned char, 0x1c);
    link->block_size = 0;
    link->data2 = NULL;
    link->len1 = 0x1c;
    link->len2 = 0;
    link->nexttopic = NULL;
    link->recordtype = 2;
    link->browse_next = link->browse_prev =
	link->nonscroll = link->scroll = NULL;
    addpos234(h->text, link, count234(h->text));

    /*
     * Each TOPICBLOCK has space for TOPIC_BLKSIZE-12 bytes. The
     * size of each TOPICLINK is 21 bytes plus the combined lengths
     * of LinkData1 and LinkData2. So we can now go through and
     * break up the TOPICLINKs into TOPICBLOCKs, and also set up
     * the TOPICOFFSET and TOPICPOS of each one while we do so.
     */

    block = 0;
    offset = 0;
    pos = 12;
    nlinks = count234(h->text);
    for (i = 0; i < nlinks; i++) {
	link = index234(h->text, i);
	link->topicoffset = block * 0x8000 + offset;
	link->topicpos = block * 0x4000 + pos;
	size = 21 + link->len1 + link->len2;
	pos += size;
	if (link->recordtype != 2)     /* TOPICOFFSET doesn't count titles */
	    offset += link->len2;
	while (pos > TOPIC_BLKSIZE) {
	    block++;
	    offset = 0;
	    pos -= TOPIC_BLKSIZE - 12;
	}
    }

    /*
     * Now we have laid out the TOPICLINKs into blocks, and
     * determined the final TOPICOFFSET and TOPICPOS of each one.
     * So now we can go through and write the headers of the type-2
     * records.
     */

    topicnum = 0;
    for (i = 0; i < nlinks; i++) {
	link = index234(h->text, i);
	if (link->recordtype != 2)
	    continue;
	
	PUT_32BIT_LSB_FIRST(link->data1 + 0, link->block_size);
	if (link->browse_prev)
	    PUT_32BIT_LSB_FIRST(link->data1 + 4,
				link->browse_prev->topicoffset);
	else
	    PUT_32BIT_LSB_FIRST(link->data1 + 4, 0xFFFFFFFFL);
	if (link->browse_next)
	    PUT_32BIT_LSB_FIRST(link->data1 + 8,
				link->browse_next->topicoffset);
	else
	    PUT_32BIT_LSB_FIRST(link->data1 + 8, 0xFFFFFFFFL);
	PUT_32BIT_LSB_FIRST(link->data1 + 12, topicnum);
	topicnum++;
	if (link->nonscroll)
	    PUT_32BIT_LSB_FIRST(link->data1 + 16, link->nonscroll->topicpos);
	else
	    PUT_32BIT_LSB_FIRST(link->data1 + 16, 0xFFFFFFFFL);
	if (link->scroll)
	    PUT_32BIT_LSB_FIRST(link->data1 + 20, link->scroll->topicpos);
	else
	    PUT_32BIT_LSB_FIRST(link->data1 + 20, 0xFFFFFFFFL);
	if (link->nexttopic)
	    PUT_32BIT_LSB_FIRST(link->data1 + 24, link->nexttopic->topicpos);
	else
	    PUT_32BIT_LSB_FIRST(link->data1 + 24, 0xFFFFFFFFL);
    }

    /*
     * Having done all _that_, we're now finally ready to go
     * through and create the |TOPIC section in its final form.
     */

    h->lasttopiclink = -1L;
    h->lasttopicstart = 0L;
    f = whlp_new_file(h, "|TOPIC");
    h->topicblock_remaining = -1;
    whlp_topicsect_write(h, f, NULL, 0);   /* start the first block */
    for (i = 0; i < nlinks; i++) {
	unsigned char header[21];
	struct topiclink *otherlink;

	link = index234(h->text, i);

	/*
	 * Fill in the `first topiclink' pointer in the block
	 * header if appropriate.
	 */
	if (h->firsttopiclink_offset > 0) {
	    whlp_file_seek(f, h->firsttopiclink_offset, 0);
	    whlp_file_add_long(f, link->topicpos);
	    h->firsttopiclink_offset = 0;
	    whlp_file_seek(f, 0, 2);
	}

	/*
	 * Update the `last topiclink', and possibly `last
	 * topicstart', pointers.
	 */
	h->lasttopiclink = link->topicpos;
	if (link->recordtype == 2)
	    h->lasttopicstart = link->topicpos;

	/*
	 * Create and output the TOPICLINK header.
	 */
	PUT_32BIT_LSB_FIRST(header + 0, 21 + link->len1 + link->len2);
	PUT_32BIT_LSB_FIRST(header + 4, link->len2);
	if (i == 0) {
	    PUT_32BIT_LSB_FIRST(header + 8, 0xFFFFFFFFL);
	} else {
	    otherlink = index234(h->text, i-1);
	    PUT_32BIT_LSB_FIRST(header + 8, otherlink->topicpos);
	}
	if (i+1 >= nlinks) {
	    PUT_32BIT_LSB_FIRST(header + 12, 0xFFFFFFFFL);
	} else {
	    otherlink = index234(h->text, i+1);
	    PUT_32BIT_LSB_FIRST(header + 12, otherlink->topicpos);
	}
	PUT_32BIT_LSB_FIRST(header + 16, 21 + link->len1);
	header[20] = link->recordtype;
	whlp_topicsect_write(h, f, header, 21);
	
	/*
	 * Output LinkData1 and LinkData2.
	 */
	whlp_topicsect_write(h, f, link->data1, link->len1);
	whlp_topicsect_write(h, f, link->data2, link->len2);

	/*
	 * Output the block header.
	 */

	link = index234(h->text, i);
	
    }
}

/* ----------------------------------------------------------------------
 * Standard chunks of data for the |SYSTEM and |FONT sections.
 */

static void whlp_system_record(struct file *f, int id,
			       const void *data, int length)
{
    whlp_file_add_short(f, id);
    whlp_file_add_short(f, length);
    whlp_file_add(f, data, length);
}

static void whlp_standard_systemsection(struct file *f)
{
    const char lcid[] = { 0, 0, 0, 0, 0, 0, 0, 0, 9, 4 };
    const char charset[] = { 0, 0, 0, 2, 0 };

    whlp_file_add_short(f, 0x36C);     /* magic number */
    whlp_file_add_short(f, 33);	       /* minor version: HCW 4.00 Win95+ */
    whlp_file_add_short(f, 1);	       /* major version */
    whlp_file_add_long(f, time(NULL)); /* generation date */
    whlp_file_add_short(f, 0);	       /* flags=0 means no compression */

    /*
     * Add some magic locale identifier information. FIXME: it
     * would be good to find out what relation (if any) this stuff
     * bears to the codepage used in the actual help text, so as to
     * be able to vary that if the user needs it. For the moment I
     * suspect we're stuck with Win1252.
     */
    whlp_system_record(f, 9, lcid, sizeof(lcid));
    whlp_system_record(f, 11, charset, sizeof(charset));
}

void whlp_title(WHLP h, char *title)
{
    whlp_system_record(h->systemfile, 1, title, 1+strlen(title));
}

void whlp_copyright(WHLP h, char *copyright)
{
    whlp_system_record(h->systemfile, 2, copyright, 1+strlen(copyright));
}

void whlp_start_macro(WHLP h, char *macro)
{
    whlp_system_record(h->systemfile, 4, macro, 1+strlen(macro));
}

void whlp_primary_topic(WHLP h, WHLP_TOPIC t)
{
    h->ptopic = t;
}

static void whlp_do_primary_topic(WHLP h)
{
    unsigned char firsttopic[4];
    PUT_32BIT_LSB_FIRST(firsttopic, h->ptopic->link->topicoffset);
    whlp_system_record(h->systemfile, 3, firsttopic, sizeof(firsttopic));
}

static void whlp_standard_fontsection(struct file *f)
{
    static const char *const fontnames[] = {
	"Times New Roman", "Courier New", "Arial", "Wingdings"
    };
    enum {
	FLAG_BOLD = 1,
	FLAG_ITALIC = 2,
	FLAG_UNDERLINE = 4,
	FLAG_STRIKEOUT = 8,
	FLAG_DOUBLEUND = 16,
	FLAG_SMALLCAPS = 32
    };
    enum {
	FAM_MODERN = 1,
	FAM_ROMAN = 2,
	FAM_SWISS = 3,
	FAM_SCRIPT = 4,
	FAM_DECOR = 5
    };
    static const struct fontdesc {
	int flags, halfpoints, facetype, font;
    } fontdescriptors[] = {
	/* Title face: 15-point Arial */
	{ FLAG_BOLD, 30, FAM_SWISS, 2},
	/* Main text face: 12-point Times */
	{ 0, 24, FAM_ROMAN, 0},
	/* Emphasised text face: 12-point Times Italic */
	{ FLAG_ITALIC, 24, FAM_ROMAN, 0},
	/* Code text face: 12-point Courier */
	{ 0, 24, FAM_MODERN, 1},
	/* FIXME: dunno what this is for */
	{ 0, 24, FAM_DECOR, 3},
	/* FIXME: dunno what this is for */
	{ 0, 24, FAM_DECOR, 3},
	/* FIXME: dunno what this is for */
	{ 0, 24, FAM_DECOR, 3},
	/* FIXME: dunno what this is for */
	{ 0, 24, FAM_DECOR, 3},
    };

    int i;

    /*
     * Header block: number of font names, number of font
     * descriptors, offset to font names, and offset to font
     * descriptors.
     */
    whlp_file_add_short(f, lenof(fontnames));
    whlp_file_add_short(f, lenof(fontdescriptors));
    whlp_file_add_short(f, 8);
    whlp_file_add_short(f, 8 + 32 * lenof(fontnames));

    /*
     * Font names.
     */
    for (i = 0; i < lenof(fontnames); i++) {
	char data[32];
	memset(data, i, sizeof(data));
	strncpy(data, fontnames[i], sizeof(data));
	whlp_file_add(f, data, sizeof(data));
    }

    /*
     * Font descriptors.
     */
    for (i = 0; i < lenof(fontdescriptors); i++) {
	whlp_file_add_char(f, fontdescriptors[i].flags);
	whlp_file_add_char(f, fontdescriptors[i].halfpoints);
	whlp_file_add_char(f, fontdescriptors[i].facetype);
	whlp_file_add_short(f, fontdescriptors[i].font);
	/* Foreground RGB is always zero */
	whlp_file_add_char(f, 0);
	whlp_file_add_char(f, 0);
	whlp_file_add_char(f, 0);
	/* Background RGB is apparently unused and always set to zero */
	whlp_file_add_char(f, 0);
	whlp_file_add_char(f, 0);
	whlp_file_add_char(f, 0);
    }
}

/* ----------------------------------------------------------------------
 * Routines to manage a B-tree type file.
 */

static void whlp_make_btree(struct file *f, int flags, int pagesize,
			    char *dataformat, tree234 *tree,
			    bt_index_fn indexfn, bt_leaf_fn leaffn)
{
    void **page_elements = NULL;
    int npages = 0, pagessize = 0;
    int npages_this_level, nentries, nlevels;
    char btdata[MAX_PAGE_SIZE];
    int btlen;
    int page_start, fixups_offset, unused_bytes;
    void *element;
    int index;

    assert(pagesize <= MAX_PAGE_SIZE);

    /*
     * Start with the B-tree header. We'll have to come back and
     * fill in a few bits later.
     */
    whlp_file_add_short(f, 0x293B);    /* magic number */
    whlp_file_add_short(f, flags);
    whlp_file_add_short(f, pagesize);
    {
	char data[16];
	memset(data, 0, sizeof(data));
	assert(strlen(dataformat) <= sizeof(data));
	memcpy(data, dataformat, strlen(dataformat));
	whlp_file_add(f, data, sizeof(data));
    }
    whlp_file_add_short(f, 0);	       /* must-be-zero */
    fixups_offset = whlp_file_offset(f);
    whlp_file_add_short(f, 0);	       /* page splits; fix up later */
    whlp_file_add_short(f, 0);	       /* root page index; fix up later */
    whlp_file_add_short(f, -1);	       /* must-be-minus-one */
    whlp_file_add_short(f, 0);	       /* total number of pages; fix later */
    whlp_file_add_short(f, 0);	       /* number of levels; fix later */
    whlp_file_add_long(f, count234(tree));/* total B-tree entries */

    /* 
     * Now create the leaf pages.
     */
    index = 0;

    npages_this_level = 0;

    element = index234(tree, index);
    while (element) {
	/*
	 * Make a new leaf page.
	 */
	npages_this_level++;
	if (npages >= pagessize) {
	    pagessize = npages + 32;
	    page_elements = resize(page_elements, pagessize);
	}
	page_elements[npages++] = element;

	/*
	 * Leave space in the leaf page for the header. We'll
	 * come back and add it later.
	 */
	page_start = whlp_file_offset(f);
	whlp_file_add(f, "12345678", 8);
	unused_bytes = pagesize - 8;
	nentries = 0;

	/*
	 * Now add leaf entries until we run out of room, or out of
	 * elements.
	 */
	while (element) {
	    btlen = leaffn(element, btdata);
	    if (btlen > unused_bytes)
		break;
	    whlp_file_add(f, btdata, btlen);
	    unused_bytes -= btlen;
	    nentries++;
	    index++;
	    element = index234(tree, index);
	}

	/*
	 * Now add the unused bytes, and then go back and put
	 * in the header.
	 */
	whlp_file_fill(f, unused_bytes);
	whlp_file_seek(f, page_start, 0);
	whlp_file_add_short(f, unused_bytes);
	whlp_file_add_short(f, nentries);
	/* Previous-page indicator will automatically go to -1 when
	 * absent. */
	whlp_file_add_short(f, npages-2);
	/* Next-page indicator must be -1 if we're at the end. */
	if (!element)
	    whlp_file_add_short(f, -1);
	else
	    whlp_file_add_short(f, npages);
	whlp_file_seek(f, 0, 2);
    }

    /*
     * Now create further levels until we're down to one page.
     */
    nlevels = 1;
    while (npages_this_level > 1) {
	int first = npages - npages_this_level;
	int last = npages - 1;
	int current;

	nlevels++;
	npages_this_level = 0;

	current = first;
	while (current <= last) {
	    /*
	     * Make a new index page.
	     */
	    npages_this_level++;
	    if (npages >= pagessize) {
		pagessize = npages + 32;
		page_elements = resize(page_elements, pagessize);
	    }
	    page_elements[npages++] = page_elements[current];

	    /*
	     * Leave space for some of the header, but we can put
	     * in the PreviousPage link already.
	     */
	    page_start = whlp_file_offset(f);
	    whlp_file_add(f, "1234", 4);
	    whlp_file_add_short(f, current);
	    unused_bytes = pagesize - 6;

	    /*
	     * Now add index entries until we run out of either
	     * space or pages.
	     */
	    current++;
	    nentries = 0;
	    while (current <= last) {
		btlen = indexfn(page_elements[current], btdata);
		if (btlen + 2 > unused_bytes)
		    break;
		whlp_file_add(f, btdata, btlen);
		whlp_file_add_short(f, current);
		unused_bytes -= btlen+2;
		nentries++;
		current++;
	    }

	    /*
	     * Now add the unused bytes, and then go back and put
	     * in the header.
	     */
	    whlp_file_fill(f, unused_bytes);
	    whlp_file_seek(f, page_start, 0);
	    whlp_file_add_short(f, unused_bytes);
	    whlp_file_add_short(f, nentries);
	    whlp_file_seek(f, 0, 2);
	}
    }

    /*
     * Now we have all our pages ready, and we know where our root
     * page is. Fix up the main B-tree header.
     */
    whlp_file_seek(f, fixups_offset, 0);
    /* Creation of every page requires a split unless it's the first in
     * a new level. Hence, page splits equals pages minus levels. */
    whlp_file_add_short(f, npages - nlevels);
    whlp_file_add_short(f, npages-1);  /* root page index */
    whlp_file_add_short(f, -1);	       /* must-be-minus-one */
    whlp_file_add_short(f, npages);    /* total number of pages */
    whlp_file_add_short(f, nlevels);   /* number of levels */

    /* Just for tidiness, seek to the end of the file :-) */
    whlp_file_seek(f, 0, 2);
}
			    

/* ----------------------------------------------------------------------
 * Routines to manage the `internal file' structure.
 */

static struct file *whlp_new_file(WHLP h, char *name)
{
    struct file *f;
    f = mknew(struct file);
    f->data = NULL;
    f->pos = f->len = f->size = 0;
    if (name) {
	f->name = dupstr(name);
	add234(h->files, f);
    } else {
	f->name = NULL;
    }
    return f;
}

static void whlp_free_file(struct file *f)
{
    sfree(f->data);
    sfree(f->name);
    sfree(f);
}

static void whlp_file_add(struct file *f, const void *data, int len)
{
    if (f->pos + len > f->size) {
	f->size = f->pos + len + 1024;
	f->data = resize(f->data, f->size);
    }
    memcpy(f->data + f->pos, data, len);
    f->pos += len;
    if (f->len < f->pos)
	f->len = f->pos;
}

static void whlp_file_add_char(struct file *f, int data)
{
    unsigned char s;
    s = data & 0xFF;
    whlp_file_add(f, &s, 1);
}

static void whlp_file_add_short(struct file *f, int data)
{
    unsigned char s[2];
    PUT_16BIT_LSB_FIRST(s, data);
    whlp_file_add(f, s, 2);
}

static void whlp_file_add_long(struct file *f, int data)
{
    unsigned char s[4];
    PUT_32BIT_LSB_FIRST(s, data);
    whlp_file_add(f, s, 4);
}

static void whlp_file_fill(struct file *f, int len)
{
    if (f->pos + len > f->size) {
	f->size = f->pos + len + 1024;
	f->data = resize(f->data, f->size);
    }
    memset(f->data + f->pos, 0, len);
    f->pos += len;
    if (f->len < f->pos)
	f->len = f->pos;
}

static void whlp_file_seek(struct file *f, int pos, int whence)
{
    f->pos = (whence == 0 ? 0 : whence == 1 ? f->pos : f->len) + pos;
}

static int whlp_file_offset(struct file *f)
{
    return f->pos;
}

/* ----------------------------------------------------------------------
 * Open and close routines; final wrapper around everything.
 */

WHLP whlp_new(void)
{
    WHLP ret;
    struct file *f;

    ret = mknew(struct WHLP_tag);

    /*
     * Internal B-trees.
     */
    ret->files = newtree234(filecmp);
    ret->pre_contexts = newtree234(NULL);
    ret->contexts = newtree234(ctxcmp);
    ret->titles = newtree234(ttlcmp);
    ret->text = newtree234(NULL);

    /*
     * Some standard files.
     */
    f = whlp_new_file(ret, "|CTXOMAP");
    whlp_file_add_short(f, 0);	       /* dummy section */
    f = whlp_new_file(ret, "|CONTEXT");
    ret->contextfile = f;
    f = whlp_new_file(ret, "|TTLBTREE");
    ret->titlefile = f;
    f = whlp_new_file(ret, "|FONT");
    whlp_standard_fontsection(f);
    f = whlp_new_file(ret, "|SYSTEM");
    whlp_standard_systemsection(f);
    ret->systemfile = f;

    /*
     * Other variables.
     */
    ret->prevtopic = NULL;

    return ret;
}

void whlp_close(WHLP h, char *filename)
{
    FILE *fp;
    int filecount, offset, index, filelen;
    struct file *file, *md;
    context *ctx;

    /*
     * Lay out the topic section.
     */
    whlp_topic_layout(h);

    /*
     * Finish off the system section.
     */
    whlp_do_primary_topic(h);

    /*
     * Set up the `titles' B-tree for the |TTLBTREE section.
     */
    for (index = 0; (ctx = index234(h->contexts, index)) != NULL; index++)
	add234(h->titles, ctx);

    /*
     * Construct the various B-trees.
     */
    whlp_make_btree(h->contextfile, 0x0002, 0x0800, "L4",
		    h->contexts, ctxindex, ctxleaf);
    whlp_make_btree(h->titlefile, 0x0002, 0x0800, "Lz",
		    h->titles, ttlindex, ttlleaf);

    /*
     * Open the output file.
     */
    fp = fopen(filename, "wb");
    if (!fp) {
	whlp_abandon(h);
	return;
    }

    /*
     * Work out all the file offsets.
     */
    filecount = count234(h->files);
    offset = 16;		       /* just after header */
    for (index = 0; index < filecount; index++) {
	file = index234(h->files, index);
	file->fileoffset = offset;
	offset += 9 + file->len;       /* 9 is size of file header */
    }
    /* Now `offset' holds what will be the offset of the master directory. */

    md = whlp_new_file(h, NULL);       /* master directory file */
    whlp_make_btree(md, 0x0402, 0x0400, "z4", h->files, fileindex, fileleaf);

    filelen = offset + 9 + md->len;

    /*
     * Write out the file header.
     */
    {
	unsigned char header[16];
	PUT_32BIT_LSB_FIRST(header+0, 0x00035F3FL);  /* magic */
	PUT_32BIT_LSB_FIRST(header+4, offset);       /* offset to directory */
	PUT_32BIT_LSB_FIRST(header+8, 0xFFFFFFFFL);  /* first free block */
	PUT_32BIT_LSB_FIRST(header+12, filelen);     /* total file length */
	fwrite(header, 1, 16, fp);
    }

    /*
     * Now write out each file.
     */
    for (index = 0; index <= filecount; index++) {
	int used, reserved;
	unsigned char header[9];

	if (index == filecount)
	    file = md;		       /* master directory comes last */
	else
	    file = index234(h->files, index);

	used = file->len;
	reserved = used + 9;

	/* File header. */
	PUT_32BIT_LSB_FIRST(header+0, reserved);
	PUT_32BIT_LSB_FIRST(header+4, used);
	header[8] = 0;		       /* flags */
	fwrite(header, 1, 9, fp);

	/* File data. */
	fwrite(file->data, 1, file->len, fp);
    }

    fclose(fp);

    whlp_free_file(md);

    whlp_abandon(h);		       /* now free everything */
}

void whlp_abandon(WHLP h)
{
    struct file *f;

    /*
     * Free all the internal files.
     */
    while ( (f = index234(h->files, 0)) != NULL ) {
	delpos234(h->files, 0);
	whlp_free_file(f);
    }
    freetree234(h->files);

    sfree(h);
}

#ifndef NOT_TESTMODE_FIXME_FLIP_SENSE_OF_THIS

int main(void)
{
    WHLP h;
    WHLP_TOPIC t1, t2, t3;
    char *e;
    char mymacro[100];

    h = whlp_new();

    whlp_title(h, "Test Help File");
    whlp_copyright(h, "This manual is copyright \251 2001 Simon Tatham."
		   " All rights reversed.");
    whlp_start_macro(h, "CB(\"btn_about\",\"&About\",\"About()\")");
    whlp_start_macro(h, "CB(\"btn_up\",\"&Up\",\"Contents()\")");
    whlp_start_macro(h, "BrowseButtons()");

    t1 = whlp_register_topic(h, "foobar", &e);
    assert(t1 != NULL);
    t2 = whlp_register_topic(h, "M359HPEHGW", &e);
    assert(t2 != NULL);
    t3 = whlp_register_topic(h, "Y5VQEXZQVJ", &e);
    assert(t3 == NULL && !strcmp(e, "M359HPEHGW"));
    t3 = whlp_register_topic(h, NULL, NULL);
    assert(t3 != NULL);

    whlp_primary_topic(h, t2);

    whlp_prepare(h);

    whlp_begin_topic(h, t1, "First Topic", "DB(\"btn_up\")", NULL);

    whlp_begin_para(h, WHLP_PARA_NONSCROLL);
    whlp_set_font(h, WHLP_FONT_TITLE);
    whlp_text(h, "Foobar");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "This is a silly paragraph with ");
    whlp_set_font(h, WHLP_FONT_FIXED);
    whlp_text(h, "code");
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, " in it.");
    whlp_end_para(h);

    whlp_para_attr(h, WHLP_PARA_SPACEABOVE, 12);
    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "This second, equally silly, paragraph has ");
    whlp_set_font(h, WHLP_FONT_ITALIC);
    whlp_text(h, "emphasis");
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, " just to prove we can do it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Now I'm going to waffle on indefinitely, in a vague attempt"
	      " to make some wrapping happen, and also to make the topicblock"
	      " go across its boundaries. This is going to take a fair amount"
	      " of text, so I'll just have to cheat and c'n'p a lot of it.");
    whlp_end_para(h);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "Have a ");
    whlp_start_hyperlink(h, t2);
    whlp_text(h, "hyperlink");
    whlp_end_hyperlink(h);
    whlp_text(h, " to another topic.");
    whlp_end_para(h);

    sprintf(mymacro, "CBB(\"btn_up\",\"JI(`',`%s')\");EB(\"btn_up\")",
	    whlp_topic_id(t3));

    whlp_begin_topic(h, t2, "Second Topic", mymacro, NULL);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "This topic contains no non-scrolling region. I would"
	      " illustrate this with a ludicrously long paragraph, but that"
	      " would get very tedious very quickly. Instead I'll just waffle"
	      " on pointlessly for a little bit and then shut up.");
    whlp_end_para(h);

    sprintf(mymacro, "CBB(\"btn_up\",\"JI(`',`%s')\");EB(\"btn_up\")",
	    whlp_topic_id(t1));

    whlp_begin_topic(h, t3, "Third Topic", mymacro, NULL);

    whlp_begin_para(h, WHLP_PARA_SCROLL);
    whlp_set_font(h, WHLP_FONT_NORMAL);
    whlp_text(h, "This third topic is almost as boring as the first. Woo!");
    whlp_end_para(h);

#if 0
    {
	int i;
	for (i = 0; i < count234(h->text); i++) {
	    struct topiclink *link = index234(h->text, i);
	    FILE *p;
	    printf("record type %d\n", link->recordtype);
	    printf("linkdata1: %p\n", link->data1);
	    p = popen("dump -f", "w");
	    fwrite(link->data1, 1, link->len1, p);
	    fclose(p);
	    printf("linkdata2: %p\n", link->data2);
	    p = popen("dump -f", "w");
	    fwrite(link->data2, 1, link->len2, p);
	    fclose(p);
	}
    }
    #endif

    whlp_close(h, "test.hlp");
    return 0;
}

#endif
