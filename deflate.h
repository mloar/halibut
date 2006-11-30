/*
 * Header file for my independent implementation of Deflate
 * (RFC1951) compression.
 */

#ifndef DEFLATE_DEFLATE_H
#define DEFLATE_DEFLATE_H

enum {
    DEFLATE_TYPE_BARE,
    DEFLATE_TYPE_ZLIB
};

/* ----------------------------------------------------------------------
 * Compression functions. Create a compression context with
 * deflate_compress_new(); feed it data with repeated calls to
 * deflate_compress_data(); destroy it with
 * deflate_compress_free().
 */

typedef struct deflate_compress_ctx deflate_compress_ctx;

/*
 * Create a new compression context. `type' indicates whether it's
 * bare Deflate (as used in, say, zip files) or Zlib (as used in,
 * say, PDF).
 */
deflate_compress_ctx *deflate_compress_new(int type);

/*
 * Free a compression context previously allocated by
 * deflate_compress_new().
 */
void deflate_compress_free(deflate_compress_ctx *ctx);

/*
 * Give the compression context some data to compress. The input
 * data is passed in `inblock', and has length `inlen'. This
 * function may or may not produce some output data; if so, it is
 * written to a dynamically allocated chunk of memory, a pointer to
 * that memory is stored in `outblock', and the length of output
 * data is stored in `outlen'. It is common for no data to be
 * output, if the input data has merely been stored in internal
 * buffers.
 * 
 * `flushtype' indicates whether you want to force buffered data to
 * be output. It can be one of the following values:
 * 
 *  - DEFLATE_NO_FLUSH: nothing is output if the compressor would
 *    rather not. Use this when the best compression is desired
 *    (i.e. most of the time).
 *
 *  - DEFLATE_SYNC_FLUSH: all the buffered data is output, but the
 *    compressed data stream remains open and ready to continue
 *    compressing data. Use this in interactive protocols when a
 *    single compressed data stream is split across several network
 *    packets.
 * 
 *  - DEFLATE_END_OF_DATA: all the buffered data is output and the
 *    compressed data stream is cleaned up. Any checksums required
 *    at the end of the stream are also output.
 */
int deflate_compress_data(deflate_compress_ctx *ctx,
			  const void *inblock, int inlen, int flushtype,
			  void **outblock, int *outlen);

enum {
    DEFLATE_NO_FLUSH,
    DEFLATE_SYNC_FLUSH,
    DEFLATE_END_OF_DATA
};

/* ----------------------------------------------------------------------
 * Decompression functions. Create a decompression context with
 * deflate_decompress_new(); feed it data with repeated calls to
 * deflate_decompress_data(); destroy it with
 * deflate_decompress_free().
 */

typedef struct deflate_decompress_ctx deflate_decompress_ctx;

/*
 * Create a new decompression context. `type' means the same as it
 * does in deflate_compress_new().
 */
deflate_decompress_ctx *deflate_decompress_new(int type);

/*
 * Free a decompression context previously allocated by
 * deflate_decompress_new().
 */
void deflate_decompress_free(deflate_decompress_ctx *ctx);

/*
 * Give the decompression context some data to decompress. The
 * input data is passed in `inblock', and has length `inlen'. This
 * function may or may not produce some output data; if so, it is
 * written to a dynamically allocated chunk of memory, a pointer to
 * that memory is stored in `outblock', and the length of output
 * data is stored in `outlen'.
 *
 * FIXME: error reporting?
 */
int deflate_decompress_data(deflate_decompress_ctx *ctx,
			    const void *inblock, int inlen,
			    void **outblock, int *outlen);

#endif /* DEFLATE_DEFLATE_H */
