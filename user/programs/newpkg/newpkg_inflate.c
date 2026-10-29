/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_inflate.c
 * Description: newpkg_inflate.c: incremental raw-DEFLATE (RFC 1951) decoder.
 * Added: 2026-10-22
 */
/* newpkg_inflate.c: incremental raw-DEFLATE (RFC 1951) decoder.
 *
 * Written as a resumable state machine at byte-production granularity:
 * every piece of decode progress (current block, huffman tables, the
 * decodable lit/len code, the pending match, the stored block) lives in
 * the context, so newpkg_inflate_read() may stop at ANY production point
 * when the staging ring fills -- the next call continues from exactly
 * there. Table construction and bit reads inside one step are blocking:
 * they refill from the input chunk, and end of input before enough bits
 * raises a truncation error instead of pausing. Table-building steps
 * produce no output bytes, so the ring-full pause can never strike
 * mid-table; from there on every emission point is byte-granular and
 * resumable.
 *
 * Bytes between the end of the final block and the section end are
 * padding/garbage by spec (mainstream decoders ignore them): the
 * container's CRC32 over the whole compressed section and the manifest
 * per-file CRCs over the produced bytes bound correctness; the raw
 * length is checked by the caller.
 *
 * Pure integer code: no syscalls, no libc, no FPU; divisions are only by
 * the power-of-two window/ring constants (% with literals), so the
 * freestanding -mno-sse -mgeneral-regs-only -fno-builtin build is
 * unchanged.
 */

#include "newpkg_inflate.h"

/* Context states (resumed across newpkg_inflate_read() calls). */
#define ST_HEAD        0   /* read the next block header */
#define ST_STORED_HDR  1   /* read LEN/NLEN at the byte boundary */
#define ST_STORED      2   /* copy stored (uncompressed) bytes */
#define ST_TABLE       3   /* decode the dynamic huffman code lengths */
#define ST_LIT         4   /* decode a lit/len symbol (or block end) */
#define ST_DIST        5   /* decode a distance symbol */
#define ST_MATCH       6   /* copy a match from the window */
#define ST_DONE        7   /* final block finished */

/* --- RFC 1951 constant tables ------------------------------------------ */

/* Fixed lit/len code lengths for block type 01 (RFC 1951 3.2.6):
 * 0..143 -> 8, 144..255 -> 9, 256..279 -> 7, 280..287 -> 8; complete. */
static const newpkg_u8 nf_fixed_lens[288] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,      /* 0..143 */
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,     /* 144..255 */
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,     /* 256..271 */
    7, 7, 7, 7, 7, 7, 7, 7,                             /* 272..279 */
    8, 8, 8, 8, 8, 8, 8, 8                              /* 280..287 */
};

/* Fixed distance code lengths (all 30 codes at 5 bits). */
static const newpkg_u8 nf_fixed_dists[30] = {
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};

/* Match length base + extra bits for symbols 257..285 (length 258 is
 * symbol 285 with no extra bits; symbol 284 has 5 extra bits). */
static const newpkg_u16 nf_lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const newpkg_u8 nf_lext[29] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

/* Distance base + extra bits for symbols 0..29. */
static const newpkg_u16 nf_dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const newpkg_u8 nf_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* Order of code-length code lengths (RFC 1951 3.2.7). */
static const newpkg_u8 nf_corder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* --- local helpers ------------------------------------------------------ */

static newpkg_usize ni_min_usize(newpkg_usize a, newpkg_usize b)
{
    return a < b ? a : b;
}

/* --- input / bit reader ----------------------------------------------------- */

/* Refill the input chunk from the callback. Returns -1 (err set) when
 * the compressed section is exhausted. */
static int ni_fill_chunk(struct newpkg_inflate *ctx)
{
    if (ctx->in_pos < ctx->in_len)
        return 0;
    ctx->in_len = ctx->get_in(ctx->ud, ctx->in, NEWPKG_INFLATE_INCHUNK);
    ctx->in_pos = 0;
    if (ctx->in_len == 0) {
        ctx->err = 1;           /* truncated compressed stream */
        return -1;
    }
    return 0;
}

/* Pull `need` bits (1..16), LSB-first as the DEFLATE wire order demands.
 * Blocking: refills byte by byte (bitbuf holds at most 31 bits). */
static int ni_getbits(struct newpkg_inflate *ctx, int need,
                      newpkg_u32 *out)
{
    newpkg_u32 v = 0;
    int got = 0;

    if (need == 0) {
        *out = 0;
        return 0;
    }
    while (got < need) {
        int take;

        while (ctx->bitcnt < need - got) {
            if (ni_fill_chunk(ctx) != 0)
                return -1;
            ctx->bitbuf |=
                ((newpkg_u32)ctx->in[ctx->in_pos++]) << ctx->bitcnt;
            ctx->bitcnt += 8;
        }
        take = need - got;
        v |= (ctx->bitbuf & ((1u << take) - 1u)) << got;
        ctx->bitbuf >>= take;
        ctx->bitcnt -= take;
        got += take;
    }
    *out = v;
    return 0;
}

/* Drop bits until the stream position is byte-aligned (stored blocks:
 * any bits left in the partially processed byte are discarded). */
static int ni_align_byte(struct newpkg_inflate *ctx)
{
    int drop = ctx->bitcnt & 7;

    if (drop) {
        newpkg_u32 junk;
        return ni_getbits(ctx, drop, &junk);
    }
    return 0;
}

/* --- huffman tables ------------------------------------------------------------ */

/* Build canonical count[]/sym[] tables from `lengths[0..n)`. Returns 0
 * on success, -1 (err set) when over-subscribed, or when the table is
 * incomplete and `strict` was requested (RFC: lit/len and code-length
 * tables must be complete; zlib tolerates incomplete distance tables and
 * so do we here -- an unassigned code fails per-symbol in
 * ni_decode_sym). */
static int ni_huf_build(struct newpkg_inflate *ctx,
                        newpkg_u16 count[16], newpkg_u16 *sym,
                        newpkg_usize sym_cap,
                        const newpkg_u8 *lengths, newpkg_usize n,
                        int strict)
{
    int i;
    newpkg_u16 offs[16];
    int left = 1;
    int len;
    newpkg_u16 base = 0;

    for (i = 0; i < 16; i++)
        count[i] = 0;
    for (i = 0; i < (int)n; i++)
        count[lengths[i]]++;
    left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= count[len];
        if (left < 0) {
            ctx->err = 1;       /* over-subscribed */
            return -1;
        }
    }
    if (strict && left != 0) {
        ctx->err = 1;           /* incomplete where completeness required */
        return -1;
    }

    offs[0] = 0;
    for (len = 1; len <= 15; len++) {
        base = (newpkg_u16)(base + count[len]);
        offs[len] = base;       /* end offset of the length group */
    }
    for (i = 0; i < (int)sym_cap; i++)
        sym[i] = 0;
    for (len = 1; len <= 15; len++) {
        newpkg_u16 start = (newpkg_u16)(offs[len] - count[len]);
        newpkg_u16 k = start;

        for (i = 0; i < (int)n; i++) {
            if (lengths[i] == len) {
                if (k < sym_cap)
                    sym[k] = (newpkg_u16)i;
                k++;
            }
        }
    }
    return 0;
}

/* Decode one symbol from the table (bit-by-bit canonical walk). The
 * walk state persists inside ctx, so a ring-full pause in the middle of
 * a code resumes exactly. Returns the symbol (>= 0) or -1 (err set). */
static int ni_decode_sym(struct newpkg_inflate *ctx,
                         const newpkg_u16 *count, const newpkg_u16 *sym)
{
    if (ctx->decode_len == 0) {
        ctx->decode_code = 0;
        ctx->decode_first = 0;
        ctx->decode_index = 0;
        ctx->decode_len = 1;
    }
    for (;;) {
        newpkg_u32 bit;
        int len = ctx->decode_len;
        int cnt, sym_index;

        if (len > 15) {
            ctx->err = 1;       /* unassigned / invalid code */
            return -1;
        }
        if (ni_getbits(ctx, 1, &bit) != 0)
            return -1;
        ctx->decode_code = (ctx->decode_code << 1) | bit;
        cnt = count[len];
        if (ctx->decode_code < (newpkg_u32)(ctx->decode_first + cnt)) {
            sym_index = ctx->decode_index + (int)(ctx->decode_code -
                                                  (newpkg_u32)ctx->decode_first);
            ctx->decode_code = 0;
            ctx->decode_first = 0;
            ctx->decode_index = 0;
            ctx->decode_len = 0;
            return (int)sym[sym_index];
        }
        ctx->decode_first = (ctx->decode_first + cnt) << 1;
        ctx->decode_index += cnt;
        ctx->decode_len = len + 1;
    }
}

/* --- output ------------------------------------------------------------------------ */

/* Emit one decompressed byte through ring + window. Callers check ring
 * space before each production. */
static void ni_emit(struct newpkg_inflate *ctx, newpkg_u8 b)
{
    ctx->ring[(ctx->ring_pos + ctx->ring_len) % NEWPKG_INFLATE_RING] = b;
    ctx->ring_len++;
    ctx->window[ctx->win_pos] = b;
    ctx->win_pos = (ctx->win_pos + 1) % NEWPKG_INFLATE_WINDOW;
    ctx->total_out++;
}

/* Enter the state that follows the block currently being decoded. */
static void ni_block_end(struct newpkg_inflate *ctx)
{
    if (ctx->final_block) {
        ctx->state = ST_DONE;
        ctx->done = 1;
    } else {
        ctx->state = ST_HEAD;
        ctx->decode_len = 0;
    }
}

/* --- state machine ------------------------------------------------------------------------ */

/* Produce until the ring is full, the stream is done, or an error. */
static void ni_run(struct newpkg_inflate *ctx)
{
    while (!ctx->done && !ctx->err &&
           ctx->ring_len < NEWPKG_INFLATE_RING) {
        switch (ctx->state) {
        case ST_HEAD: {
            newpkg_u32 type;

            ctx->final_block = 0;
            if (ni_getbits(ctx, 3, &type) != 0)
                return;
            ctx->final_block = (int)(type & 1u);
            switch ((int)((type >> 1) & 3u)) {
            case 0:
                ctx->state = ST_STORED_HDR;
                break;
            case 1:
                if (ni_huf_build(ctx, ctx->lit_count, ctx->lit_sym, 288,
                                 nf_fixed_lens, 288, 1) != 0 ||
                    ni_huf_build(ctx, ctx->dist_count, ctx->dist_sym, 32,
                                 nf_fixed_dists, 30, 0) != 0)
                    return;
                ctx->state = ST_LIT;
                ctx->decode_len = 0;
                break;
            case 2:
                ctx->state = ST_TABLE;
                ctx->build_step = 0;
                break;
            default:
                ctx->err = 1;   /* reserved block type 11 */
                return;
            }
            break;
        }
        case ST_STORED_HDR: {
            newpkg_u32 nlen;

            if (ni_align_byte(ctx) != 0)
                return;
            if (ni_getbits(ctx, 16, &ctx->stored_left) != 0 ||
                ni_getbits(ctx, 16, &nlen) != 0)
                return;
            if (((~nlen) & 0xffffu) != ctx->stored_left) {
                ctx->err = 1;   /* LEN/NLEN complement mismatch */
                return;
            }
            ctx->state = ST_STORED;
            break;
        }
        case ST_STORED: {
            if (ctx->stored_left == 0) {
                ni_block_end(ctx);
                break;
            }
            while (ctx->stored_left > 0 &&
                   ctx->ring_len < NEWPKG_INFLATE_RING) {
                newpkg_u32 b;

                if (ni_getbits(ctx, 8, &b) != 0)
                    return;
                ni_emit(ctx, (newpkg_u8)b);
                ctx->stored_left--;
            }
            if (ctx->stored_left == 0)
                ni_block_end(ctx);
            break;
        }
        case ST_TABLE: {
            if (ctx->build_step == 0) {
                newpkg_u32 vs;
                int hclen;
                int i_cl;
                newpkg_u8 cl_lens[19];

                if (ni_getbits(ctx, 5, &vs) != 0)
                    return;
                ctx->hlit = (int)vs + 257;
                if (ni_getbits(ctx, 5, &vs) != 0)
                    return;
                ctx->hdist = (int)vs + 1;
                if (ni_getbits(ctx, 4, &vs) != 0)
                    return;
                hclen = (int)vs + 4;
                for (i_cl = 0; i_cl < 19; i_cl++)
                    cl_lens[i_cl] = 0;
                for (i_cl = 0; i_cl < hclen; i_cl++) {
                    newpkg_u32 l;

                    if (ni_getbits(ctx, 3, &l) != 0)
                        return;
                    cl_lens[nf_corder[i_cl]] = (newpkg_u8)l;
                }
                if (ni_huf_build(ctx, ctx->cl_count, ctx->cl_sym, 19,
                                 cl_lens, 19, 1) != 0)
                    return;
                if (ctx->hlit > 286 || ctx->hdist > 30) {
                    ctx->err = 1;
                    return;
                }
                ctx->lens_idx = 0;
                ctx->rep_left = 0;
                ctx->build_step = 1;
            }
            if (ctx->build_step == 1) {
                int total = ctx->hlit + ctx->hdist;

                while (ctx->lens_idx < total) {
                    int sym;

                    while (ctx->lens_idx < total && ctx->rep_left > 0) {
                        ctx->lens[ctx->lens_idx++] = ctx->rep_val;
                        ctx->rep_left--;
                    }
                    if (ctx->lens_idx >= total)
                        break;
                    sym = ni_decode_sym(ctx, ctx->cl_count, ctx->cl_sym);
                    if (sym < 0)
                        return;
                    if (sym < 16) {
                        ctx->lens[ctx->lens_idx++] = (newpkg_u8)sym;
                    } else if (sym == 16) {
                        newpkg_u32 e;

                        if (ctx->lens_idx == 0) {
                            ctx->err = 1;   /* no previous length */
                            return;
                        }
                        if (ni_getbits(ctx, 2, &e) != 0)
                            return;
                        ctx->rep_val = ctx->lens[ctx->lens_idx - 1];
                        if (ctx->lens_idx + 3 + (int)e > total) {
                            ctx->err = 1;   /* repeat overruns table */
                            return;
                        }
                        ctx->rep_left = 3 + (int)e;
                    } else if (sym == 17) {
                        newpkg_u32 e;

                        if (ni_getbits(ctx, 3, &e) != 0)
                            return;
                        ctx->rep_val = 0;
                        if (ctx->lens_idx + 3 + (int)e > total) {
                            ctx->err = 1;
                            return;
                        }
                        ctx->rep_left = 3 + (int)e;
                    } else {
                        newpkg_u32 e;

                        if (ni_getbits(ctx, 7, &e) != 0)
                            return;
                        ctx->rep_val = 0;
                        if (ctx->lens_idx + 11 + (int)e > total) {
                            ctx->err = 1;
                            return;
                        }
                        ctx->rep_left = 11 + (int)e;
                    }
                }
                if (ni_huf_build(ctx, ctx->lit_count, ctx->lit_sym, 288,
                                 ctx->lens, (newpkg_usize)ctx->hlit,
                                 1) != 0)
                    return;
                {
                    newpkg_u8 dist_lens[32];
                    int d;

                    for (d = 0; d < ctx->hdist; d++)
                        dist_lens[d] = ctx->lens[ctx->hlit + d];
                    if (ni_huf_build(ctx, ctx->dist_count, ctx->dist_sym,
                                     32, dist_lens,
                                     (newpkg_usize)ctx->hdist, 0) != 0)
                        return;
                }
                ctx->build_step = 2;
                ctx->state = ST_LIT;
                ctx->decode_len = 0;
            }
            break;
        }
        case ST_LIT: {
            int sym;

            sym = ni_decode_sym(ctx, ctx->lit_count, ctx->lit_sym);
            if (sym < 0)
                return;
            if (sym < 256) {
                ni_emit(ctx, (newpkg_u8)sym);
            } else if (sym == 256) {
                ni_block_end(ctx);
            } else if (sym <= 285) {
                newpkg_u32 extra;
                int li = sym - 257;

                ctx->match_left = nf_lbase[li];
                if (nf_lext[li]) {
                    if (ni_getbits(ctx, nf_lext[li], &extra) != 0)
                        return;
                    ctx->match_left += extra;
                }
                ctx->state = ST_DIST;
                ctx->decode_len = 0;
            } else {
                ctx->err = 1;   /* invalid lit/len symbol 286/287 */
                return;
            }
            break;
        }
        case ST_DIST: {
            int sym;

            sym = ni_decode_sym(ctx, ctx->dist_count, ctx->dist_sym);
            if (sym < 0)
                return;
            if (sym > 29) {
                ctx->err = 1;   /* invalid distance symbol 30/31 */
                return;
            }
            {
                newpkg_u32 extra = 0;

                ctx->match_dist = nf_dbase[sym];
                if (nf_dext[sym]) {
                    if (ni_getbits(ctx, nf_dext[sym], &extra) != 0)
                        return;
                    ctx->match_dist += extra;
                }
            }
            if (ctx->match_dist > ctx->total_out) {
                ctx->err = 1;   /* distance before stream start */
                return;
            }
            ctx->state = ST_MATCH;
            break;
        }
        case ST_MATCH: {
            if (ctx->match_left == 0) {
                ctx->state = ST_LIT;
                ctx->decode_len = 0;
                break;
            }
            while (ctx->match_left > 0 &&
                   ctx->ring_len < NEWPKG_INFLATE_RING) {
                newpkg_u32 src = ctx->win_pos + NEWPKG_INFLATE_WINDOW -
                                 ctx->match_dist;

                ni_emit(ctx,
                        ctx->window[src % NEWPKG_INFLATE_WINDOW]);
                ctx->match_left--;
            }
            if (ctx->match_left == 0) {
                ctx->state = ST_LIT;
                ctx->decode_len = 0;
            }
            break;
        }
        case ST_DONE:
        default:
            return;
        }
    }
}

/* --- public API ---------------------------------------------------------- */

void newpkg_inflate_init(struct newpkg_inflate *ctx,
                         newpkg_inflate_in_fn get_in, void *ud)
{
    int i;
    newpkg_usize n;

    ctx->get_in = get_in;
    ctx->ud = ud;
    n = NEWPKG_INFLATE_INCHUNK;
    for (i = 0; i < (int)n; i++)
        ctx->in[i] = 0;
    ctx->in_len = 0;
    ctx->in_pos = 0;
    ctx->bitbuf = 0;
    ctx->bitcnt = 0;
    ctx->ring_pos = 0;
    ctx->ring_len = 0;
    for (n = 0; n < NEWPKG_INFLATE_RING; n++)
        ctx->ring[n] = 0;
    for (n = 0; n < NEWPKG_INFLATE_WINDOW; n++)
        ctx->window[n] = 0;
    ctx->win_pos = 0;
    ctx->total_out = 0;
    ctx->state = ST_HEAD;
    ctx->final_block = 0;
    ctx->stored_left = 0;
    ctx->build_step = 0;
    ctx->hlit = 0;
    ctx->hdist = 0;
    ctx->lens_idx = 0;
    ctx->rep_left = 0;
    ctx->rep_val = 0;
    for (i = 0; i < 320; i++)
        ctx->lens[i] = 0;
    for (i = 0; i < 16; i++) {
        ctx->cl_count[i] = 0;
        ctx->lit_count[i] = 0;
        ctx->dist_count[i] = 0;
    }
    ctx->decode_len = 0;
    ctx->decode_code = 0;
    ctx->decode_first = 0;
    ctx->decode_index = 0;
    ctx->match_left = 0;
    ctx->match_dist = 0;
    ctx->err = 0;
    ctx->done = 0;
}

newpkg_usize newpkg_inflate_read(struct newpkg_inflate *ctx,
                                 newpkg_u8 *dst, newpkg_usize want)
{
    newpkg_usize got = 0;

    if (!dst || ctx->err || want == 0)
        return 0;
    while (got < want) {
        newpkg_usize take;

        if (ctx->ring_len > 0) {
            take = ni_min_usize(ctx->ring_len, want - got);
            {
                newpkg_u32 i;

                for (i = 0; i < take; i++)
                    dst[got + i] = ctx->ring[(ctx->ring_pos + i) %
                                             NEWPKG_INFLATE_RING];
            }
            ctx->ring_pos = (ctx->ring_pos + (newpkg_u32)take) %
                            NEWPKG_INFLATE_RING;
            ctx->ring_len -= (newpkg_u32)take;
            got += take;
        } else if (ctx->done) {
            break;              /* stream ended: return short read */
        } else {
            ni_run(ctx);
            if (ctx->err)
                return 0;
            if (ctx->ring_len == 0 && !ctx->done) {
                /* No progress without an error is not possible in this
                 * machine; treat defensively as a corrupted stream. */
                ctx->err = 1;
                return 0;
            }
        }
    }
    return got;
}

int newpkg_inflate_done(const struct newpkg_inflate *ctx)
{
    return ctx->done;
}

int newpkg_inflate_errored(const struct newpkg_inflate *ctx)
{
    return ctx->err;
}

newpkg_u64 newpkg_inflate_total_out(const struct newpkg_inflate *ctx)
{
    return ctx->total_out;
}
