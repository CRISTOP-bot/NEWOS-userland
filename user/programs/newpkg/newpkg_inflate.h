/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_inflate.h
 * Description: newpkg_inflate.h: incremental raw-DEFLATE decompressor for `.new` spec v2.
 * Added: 2026-10-22
 */
/* newpkg_inflate.h: incremental raw-DEFLATE decompressor for `.new` spec v2.
 *
 * Spec v2 (flags bit 0) stores the payload section as a raw DEFLATE
 * (RFC 1951) stream; the 8 reserved header bytes carry the *uncompressed*
 * payload length. The `.new` design has no `lseek` and installs in
 * 512-byte chunks, so the decoder never buffers a whole stream:
 *
 *   - input: the caller supplies a refill callback that yields the next
 *     compressed chunk (bounded, e.g. 512 bytes) -- the installer's
 *     callback streams from VFS and accumulates the section CRC32;
 *   - output: the decoder fills an internal staging ring; the consumer
 *     drains it with newpkg_inflate_read(), which pauses mid-match and
 *     even mid-huffman-code cleanly -- all decode state lives in the
 *     context, so the next call resumes from exactly there.
 *
 * Buffers are part of the context (window 32768 + ring 4096 + input
 * chunk 512) so nothing is malloc'ed: one static instance per process
 * covers an install. The C is freestanding (no syscalls, no libc, no
 * FPU) and compiles both under NEWOS userland flags and the host test
 * driver (-DNEWPKG_HOST).
 */

#ifndef NEWPKG_INFLATE_H
#define NEWPKG_INFLATE_H

#include "newpkg_format.h"

#define NEWPKG_INFLATE_WINDOW  32768u   /* DEFLATE back-reference range */
#define NEWPKG_INFLATE_INCHUNK 512u     /* refill granularity */
#define NEWPKG_INFLATE_RING    4096u    /* output staging ring */

/* Refill callback: place up to `cap` compressed bytes into `buf` and
 * return how many were placed. Returning 0 means the compressed section
 * ended (all header payload_len bytes were streamed). */
typedef newpkg_usize (*newpkg_inflate_in_fn)(void *ud, newpkg_u8 *buf,
                                             newpkg_usize cap);

/* Resumable DEFLATE decoding state. */
struct newpkg_inflate {
    /* input side */
    newpkg_inflate_in_fn get_in;
    void *ud;
    newpkg_u8 in[NEWPKG_INFLATE_INCHUNK];
    newpkg_usize in_len;
    newpkg_usize in_pos;
    newpkg_u32 bitbuf;
    int bitcnt;

    /* output side (ring: produced bytes not yet drained) */
    newpkg_u8 ring[NEWPKG_INFLATE_RING];
    newpkg_u32 ring_pos;
    newpkg_u32 ring_len;

    /* DEFLATE history window (last 32768 produced bytes) */
    newpkg_u8 window[NEWPKG_INFLATE_WINDOW];
    newpkg_u32 win_pos;
    newpkg_u64 total_out;

    /* resumable state machine */
    int state;                      /* ST_* */
    int final_block;
    /* stored block */
    newpkg_u32 stored_left;
    /* dynamic table build */
    int build_step;                 /* ST_TABLE sub-phase */
    int hlit, hdist;                /* dynamic table sizes */
    newpkg_u8 lens[320];            /* lit lens + dist lens */
    int lens_idx;
    int rep_left;                   /* code-length repeat pending */
    newpkg_u8 rep_val;              /* code-length repeat value */
    /* code-length huffman table (cl) */
    newpkg_u16 cl_count[16];
    newpkg_u16 cl_sym[19];
    /* block huffman tables (lit/len + distance) */
    newpkg_u16 lit_count[16];
    newpkg_u16 lit_sym[288];
    newpkg_u16 dist_count[16];
    newpkg_u16 dist_sym[32];
    /* symbol decode state (shared lit/dist: only one decodes at a time;
     * persists across ring-full pauses in the middle of a code) */
    int decode_len;                 /* current walk depth, 0 = not started */
    newpkg_u32 decode_code;
    int decode_first;
    int decode_index;
    /* match copy */
    newpkg_u32 match_left;
    newpkg_u32 match_dist;

    int err;                        /* sticky decode error flag */
    int done;                       /* final block fully decoded */
};

/* Reset the context and wire the input source. */
void newpkg_inflate_init(struct newpkg_inflate *ctx,
                         newpkg_inflate_in_fn get_in, void *ud);

/* Copy the next decompressed bytes into dst (up to want), running the
 * decoder as needed. Returns the number of bytes copied; 0 means the
 * stream ended (ctx->done set) or a decode error happened (ctx->err). */
newpkg_usize newpkg_inflate_read(struct newpkg_inflate *ctx,
                                 newpkg_u8 *dst, newpkg_usize want);

/* True once the final block has been fully decoded. */
int newpkg_inflate_done(const struct newpkg_inflate *ctx);

/* True once a decode error was raised (sticky). */
int newpkg_inflate_errored(const struct newpkg_inflate *ctx);

/* Total decompressed bytes produced so far. */
newpkg_u64 newpkg_inflate_total_out(const struct newpkg_inflate *ctx);

#endif /* NEWPKG_INFLATE_H */
