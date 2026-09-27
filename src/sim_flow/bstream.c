/* sim_flow: bstream.c — the VCR bit streams (48c7 open/close, 63ed bit I/O with the EMS cache), the
 * Huffman tables (4d15 load/build, 6922 code/decode, 6de4 find) — sim_flow_vcr.md §3.
 * PORT: implemented as the original (EMS cache of DS:0100/DS:0C78 bytes, fopen per cache block, the
 * 200-byte bit buffers), so the film bytes and the "short read leaves the destination" behaviour are
 * the original's by construction. */
#include "sim_flow/sfb_priv.h"

/* BSTREAM (0x3B bytes, table of 2 at DS:3CDA) */
#define BS_MODE   0x00
#define BS_BUF    0x02          /* far ptr */
#define BS_SIZE   0x06          /* u32 */
#define BS_POS    0x0A          /* u32 bit position */
#define BS_LIMIT  0x0E          /* u32 bit limit */
#define BS_FP     0x12
#define BS_NAME   0x14
#define BS_FOFF   0x2D          /* u32 */
#define BS_EMS    0x31
#define BS_USED   0x33
#define BS_GOT    0x35          /* u32 */
#define BS_FILLS  0x39

#define BS_MASK(i) DSB(0x0C7A + ((i) & 7))            /* 01 02 04 .. 80 */

static u8 *bs_byte(u16 s, u32 byte)                   /* huge pointer buf + byte (PADD@) */
{
    FarPtr b = ds_far((u16)(s + BS_BUF));
    return mem + far_lin(b) + byte;
}

/* Path "<work dir DS:4032><name>" into an 80-byte stack buffer */
static u16 bs_path(u16 s)
{
    u16 p = stk_alloc(0x50);
    rtl_strcpy(p, 0x4032);
    rtl_strcat(p, (u16)(s + BS_NAME));
    return p;
}

/* AP 48c7:0000 bs_file_fill(s, far cache) — sim_flow_vcr.md §3.3 */
void bs_file_fill(u16 s, FarPtr cache)
{
    u16 m = stk_mark();
    u16 path = bs_path(s);
    DSW(s + BS_FP) = rtl_fopen(path, 0x011A /* "rb" */);
    if (DSW(s + BS_FP) == 0) DSW(s + BS_FP) = rtl_fopen((u16)(s + BS_NAME), 0x011A);
    if (DSW(s + BS_FP) == 0) assert_fail(far_make(SEG(0x3838), 0), 0x3C, (u16)(s + BS_NAME));
    my_fseek(DSW(s + BS_FP), DSSL(s + BS_FOFF), 0);
    s16 n = DSS(0x0100);
    DSL(s + BS_GOT) = 0;
    u32 p = far_lin(cache);
    while (n > 0) {
        s16 c = rtl_fgetc(DSW(s + BS_FP));
        if (c == -1) break;
        DSL(s + BS_GOT) += 1;
        mem[p++] = (u8)c;
        n--;
    }
    DSL(s + BS_FOFF) += DSL(s + BS_GOT);
    DSW(s + BS_FILLS) += 1;
    rtl_fclose(DSW(s + BS_FP));
    DSW(s + BS_FP) = 0;
    stk_release(m);
}

/* AP 48c7:0127 bs_fill_cache(s) */
void bs_fill_cache(u16 s)
{
    FarPtr c = lib_ems_map(DSW(s + BS_EMS), 0);
    bs_file_fill(s, c);
    DSW(s + BS_FILLS) = 0;                            /* sic: reset after the fill */
    lib_ems_restore_map(DSW(s + BS_EMS));
}

/* AP 48c7:0161 bs_debug_log(s) — fprintf to c:\aotp.txt; no callers. PORT: not reproduced (it would
 * write outside the game folder). */
void bs_debug_log(u16 s_ds)
{
    (void)s_ds;
}

/* AP 48c7:0190 bs_open(name, mode, far buf, u32 size) -> BSTREAM* / 0 */
u16 bs_open(u16 name_ds, u16 mode, FarPtr buf, u32 size)
{
    s16 i = 0;
    u16 s = 0x3CDA;
    while (i < 2 && (DSW(s) & 4)) { i++; s = (u16)(s + 0x3B); }
    if (i == 2) return 0;
    memset(DSP(s), 0, 0x3B);
    rtl_strcpy((u16)(s + BS_NAME), name_ds);
    if (!(mode & 1)) {
        DSW(s + BS_FP) = my_fopen(name_ds, 0x011D /* "wb" */);
        if (DSW(s + BS_FP) == 0) fatal_disk_full();
    } else {
        if (mode & 2) return 0;
        DSW(s + BS_FP) = my_fopen(name_ds, 0x011A /* "rb" */);
    }
    if (DSW(s + BS_FP) == 0) return 0;
    DSL(s + BS_FOFF) = 0;
    my_fclose(DSW(s + BS_FP));
    DSW(s + BS_EMS) = lib_ems_alloc((u32)DSW(0x0100));
    if (DSW(s + BS_EMS) == 0) assert_fail(far_make(SEG(0x3838), 0), 0x8C, 0);
    DSW(s + BS_USED) = 0;
    if (mode & 1) bs_fill_cache(s);
    DSL(s + BS_SIZE) = size;
    ds_far_wr((u16)(s + BS_BUF), buf);
    DSW(s + BS_MODE) = (u16)(mode | 4);
    if (mode & 2) DSL(s + BS_LIMIT) = size << 3;
    return s;
}

/* AP 48c7:02dd bs_close(s) -> ok */
s16 bs_close(u16 s)
{
    s16 r = 1;
    if (DSW(s + BS_MODE) & 2) {
        DSL(s + BS_POS) = (DSL(s + BS_POS) + 7) & 0xFFFFFFF8u;
        r = bs_flush(s, 1);
        if (r == 0) fatal_disk_full();
    }
    DSW(s + BS_MODE) = 0;
    lib_ems_free(DSW(s + BS_EMS));
    return r;
}

/* AP 48c7:0329 bs_rewind(s) */
void bs_rewind(u16 s)
{
    DSL(s + BS_FOFF) = 0;
    DSL(s + BS_POS) = 0;
    DSL(s + BS_LIMIT) = 0;
    DSW(s + BS_USED) = 0;
    if (DSW(s + BS_FILLS) != 0) bs_fill_cache(s);
}

/* AP 48c7:035e bs_abort(s) -> 1 */
s16 bs_abort(u16 s)
{
    DSW(s + BS_MODE) = 0;
    lib_ems_free(DSW(s + BS_EMS));
    return 1;
}

/* ---- 63ed: bit I/O -------------------------------------------------------------------------------- */

/* AP 63ed:0000 bs_write_bits(src, nbits, s) -> 1 / 0 */
s16 bs_write_bits(u16 src_ds, s16 nbits, u16 s)
{
    s16 k = 0;                                        /* source bit index */
    while (nbits > 0) {
        s32 avail = (s32)(DSL(s + BS_LIMIT) - DSL(s + BS_POS));
        if (avail == 0) {
            if (bs_flush(s, 0) == 0) return 0;
            avail = (s32)(DSL(s + BS_LIMIT) - DSL(s + BS_POS));
        }
        s16 n = avail < nbits ? (s16)avail : nbits;
        nbits = (s16)(nbits - n);
        for (; n > 0; n--) {
            u32 pos = DSL(s + BS_POS);
            u8 bit = BS_MASK(pos);
            u8 *d = bs_byte(s, pos >> 3);
            if (DSB(src_ds + (k >> 3)) & BS_MASK(k)) *d |= bit;
            else *d &= (u8)~bit;
            DSL(s + BS_POS) = pos + 1;
            k++;
        }
    }
    return 1;
}

/* AP 63ed:00e7 bs_read_bits(dst, nbits, s) -> bits read (short at the end of the data) */
s16 bs_read_bits(u16 dst_ds, s16 nbits, u16 s)
{
    s16 k = 0;
    while (nbits > 0) {
        s32 avail = (s32)(DSL(s + BS_LIMIT) - DSL(s + BS_POS));
        if (avail == 0) {
            if (bs_refill(s) == 0) return k;
            avail = (s32)(DSL(s + BS_LIMIT) - DSL(s + BS_POS));
        }
        s16 n = avail < nbits ? (s16)avail : nbits;
        nbits = (s16)(nbits - n);
        for (; n > 0; n--) {
            u8 bit = BS_MASK(k);
            u16 d = (u16)(dst_ds + (k >> 3));
            u32 pos = DSL(s + BS_POS);
            if (*bs_byte(s, pos >> 3) & BS_MASK(pos)) DSB(d) |= bit;
            else DSB(d) &= (u8)~bit;
            DSL(s + BS_POS) = pos + 1;
            k++;
        }
    }
    return k;
}

/* AP 63ed:01c3 bs_file_append(s, far buf, n) -> ok */
s16 bs_file_append(u16 s, FarPtr buf, s16 n)
{
    u16 m = stk_mark();
    s16 ok = 1;
    u16 path = bs_path(s);
    DSW(s + BS_FP) = rtl_fopen(path, 0x0C82 /* "ab" */);
    if (DSW(s + BS_FP) == 0) assert_fail(far_make(SEG(0x3838), 0x30), 0x19B, (u16)(s + BS_NAME));
    u32 p = far_lin(buf);
    while (n > 0 && ok) {
        s16 c = rtl_fputc((s16)(s8)mem[p++], DSW(s + BS_FP));
        n--;
        ok = c != -1;
    }
    rtl_fclose(DSW(s + BS_FP));
    DSW(s + BS_USED) = 0;
    stk_release(m);
    return ok;
}

/* AP 63ed:02b1 bs_cache_put(far src, n, s, final) */
void bs_cache_put(FarPtr src, s16 n, u16 s, s16 final)
{
    s16 room = (s16)(DSW(0x0C78) - DSW(s + BS_USED));
    FarPtr c = lib_ems_map(DSW(s + BS_EMS), 0);
    if (room < n) {
        if (room != 0) {
            far_memmove_fp(far_add(c, DSW(s + BS_USED)), src, (u32)(s32)room);
            src = far_norm(far_lin(src) + (u32)(s32)room);
            n = (s16)(n - room);
        }
        if (bs_file_append(s, c, (s16)DSW(0x0C78)) == 0) fatal_disk_full();
    }
    if (n != 0) {
        far_memmove_fp(far_add(c, DSW(s + BS_USED)), src, (u32)(s32)n);
        DSW(s + BS_USED) += (u16)n;
    }
    if (final && DSW(s + BS_USED) != 0)
        if (bs_file_append(s, c, (s16)DSW(s + BS_USED)) == 0) fatal_disk_full();
    lib_ems_restore_map(DSW(s + BS_EMS));
}

/* AP 63ed:0395 bs_flush(s, final) -> 1 */
s16 bs_flush(u16 s, s16 final)
{
    u32 pos = DSL(s + BS_POS);
    u8 part = 0;
    if (pos & 7) part = *bs_byte(s, pos >> 3);
    bs_cache_put(ds_far((u16)(s + BS_BUF)), (s16)(pos >> 3), s, final);
    DSL(s + BS_POS) &= 7;
    if (DSL(s + BS_POS) != 0) *bs_byte(s, 0) = part;
    return 1;
}

/* AP 63ed:0405 bs_cache_get(far dst, s32 n, s) -> bytes copied (the limit is DS:0C78) */
s16 bs_cache_get(FarPtr dst, s32 n, u16 s)
{
    s16 first = 0, k = 0;
    FarPtr c = lib_ems_map(DSW(s + BS_EMS), 0);
    while (k == 0) {
        u16 end = (u16)(DSW(s + BS_USED) + (u16)n);
        s32 got = DSSL(s + BS_GOT);
        if ((s32)(s16)end > got && (s32)(u32)DSW(0x0C78) > got) {
            k = (s16)((u16)got - DSW(s + BS_USED));
            if (k == 0) break;                        /* PORT: the original loops forever at the end
                                                       * of the data; return 0 bytes instead */
        } else if (end > DSW(0x0C78)) {
            first = (s16)(DSW(0x0C78) - DSW(s + BS_USED));
            far_memmove_fp(dst, far_add(c, DSW(s + BS_USED)), (u32)(s32)first);
            bs_file_fill(s, c);
            DSW(s + BS_USED) = 0;
            n -= first;
            dst = far_norm(far_lin(dst) + (u32)(s32)first);
        } else {
            k = (s16)n;
        }
    }
    far_memmove_fp(dst, far_add(c, DSW(s + BS_USED)), (u32)(s32)k);
    DSW(s + BS_USED) += (u16)k;
    lib_ems_restore_map(DSW(s + BS_EMS));
    return (s16)(k + first);
}

/* AP 63ed:04f8 bs_refill(s) -> valid bits after the refill */
s16 bs_refill(u16 s)
{
    bs_compact(s);
    u32 have = DSL(s + BS_LIMIT) >> 3;
    FarPtr b = ds_far((u16)(s + BS_BUF));
    s16 got = bs_cache_get(far_norm(far_lin(b) + have), (s32)(DSL(s + BS_SIZE) - have), s);
    DSL(s + BS_LIMIT) += (u32)(s32)got * 8;
    return (s16)(DSL(s + BS_LIMIT) - DSL(s + BS_POS));
}

/* AP 63ed:0570 bs_compact(s) */
void bs_compact(u16 s)
{
    u32 pb = (u32)((s32)DSL(s + BS_POS) >> 3);
    FarPtr b = ds_far((u16)(s + BS_BUF));
    far_memmove_fp(b, far_norm(far_lin(b) + pb), (u32)((s32)DSL(s + BS_LIMIT) >> 3) - pb);
    DSL(s + BS_POS) &= 7;
    DSL(s + BS_LIMIT) -= pb * 8;
}

/* ---- 4d15 / 6de4 / 6922: Huffman tables ----------------------------------------------------------- */

/* Table object (near): +0 n, +2 m, +4 s32 total, +8 code buffer, +A pool handle, +C far root.
 * Node (10 bytes): +0 s32 freq, +4 parent, +6 sym / child 0, +8 depth / child 1. */
#define HT_N(t)     DSS((t) + 0)
#define HT_M(t)     DSS((t) + 2)
#define HT_TOTAL(t) DSSL((t) + 4)
#define HT_CODE(t)  DSW((t) + 8)
#define HT_H(t)     DSW((t) + 0x0A)
#define ND(b, i, o) ((b) + (u32)(s32)(i) * 10 + (o))          /* linear address of node i field o */
#define NW(b, i, o) ((s16)(mem[ND(b, i, o)] | mem[ND(b, i, o) + 1] << 8))
#define NW_SET(b, i, o, v) do { u32 a_ = ND(b, i, o); u16 v_ = (u16)(v); mem[a_] = (u8)v_; mem[a_ + 1] = (u8)(v_ >> 8); } while (0)
#define NL(b, i)    ((s32)(mem[ND(b, i, 0)] | mem[ND(b, i, 0) + 1] << 8 | mem[ND(b, i, 0) + 2] << 16 | (u32)mem[ND(b, i, 0) + 3] << 24))
#define NL_SET(b, i, v) do { u32 a_ = ND(b, i, 0); u32 v_ = (u32)(v); mem[a_] = (u8)v_; mem[a_ + 1] = (u8)(v_ >> 8); mem[a_ + 2] = (u8)(v_ >> 16); mem[a_ + 3] = (u8)(v_ >> 24); } while (0)

/* AP 4d15:0000 huff_depth(tab, i, far nodes) -> steps from node i to the root */
s16 huff_depth(u16 t, s16 i, FarPtr nodes)
{
    u32 b = far_lin(nodes);
    s16 d = 0;
    while (i != (s16)((HT_N(t) - 1) * 2)) {
        d++;
        i = NW(b, i, 4);
    }
    return d;
}

/* AP 4d15:0036 huff_load(path) -> table / 0 */
u16 huff_load(u16 path_ds)
{
    u16 f = my_fopen(path_ds, 0x0314 /* "rb" */);
    if (!f) return 0;
    u16 m = stk_mark();
    u16 n = stk_alloc(2), sym = stk_alloc(2), freq = stk_alloc(4);
    my_fread(n, 2, 1, f);
    u16 t = huff_alloc(DSS(n));
    if (!t) { my_fclose(f); stk_release(m); return 0; }
    s16 k = 0;
    if (DSS(n) > 0) {
        for (;;) {
            if (my_fread(sym, 2, 1, f) != 1) break;
            if (my_fread(freq, 4, 1, f) != 1) break;
            if (huff_add(t, DSS(sym), DSSL(freq)) == 0) break;
            if (++k >= DSS(n)) break;
        }
    }
    my_fclose(f);
    if (k != DSS(n)) { huff_free(t); stk_release(m); return 0; }
    huff_build(t);
    stk_release(m);
    return t;
}

/* AP 4d15:0112 huff_save(path, tab) -> 1 — no callers (tool leftover) */
s16 huff_save(u16 path_ds, u16 t)
{
    u16 f = my_fopen(path_ds, 0x011D /* "wb" */);
    if (!f) return 0;
    u16 m = stk_mark();
    u16 n = stk_alloc(2), sym = stk_alloc(2), freq = stk_alloc(4);
    DSW(n) = (u16)HT_N(t);
    my_fwrite(n, 2, 1, f);
    u32 b = far_lin(pool_map_save(HT_H(t)));
    for (s16 i = 0; i < DSS(n); i++) {
        DSW(sym) = (u16)NW(b, i, 6);
        DSSL(freq) = NL(b, i);
        my_fwrite(sym, 2, 1, f);
        my_fwrite(freq, 4, 1, f);
    }
    my_fclose(f);
    pool_unmap(HT_H(t));
    stk_release(m);
    return 1;
}

/* AP 4d15:01ca huff_alloc(n) -> table / 0 */
u16 huff_alloc(s16 n)
{
    if (n <= 1 || n > 0x800) return 0;
    u16 t = safe_malloc((u16)(((n + 6) >> 3) + 0x10));
    if (!t) return t;
    u16 h = ap_pool_alloc_1d32(0, (u16)((n * 2 - 1) * 10));
    DSW(t + 0x0A) = h;
    if (h == 0) { lib_free(t); return 0; }
    FarPtr nodes = pool_map_save(h);
    DSW(t + 8) = (u16)(t + 0x10);
    DSL(t + 4) = 0;
    DSW(t + 2) = 0;
    DSW(t + 0) = (u16)n;
    ds_far_wr((u16)(t + 0x0C), far_add(nodes, (u16)((n - 1) * 20)));
    u32 b = far_lin(nodes);
    for (s16 i = 0; i <= (n - 1) * 2; i++) memset(mem + b + (u32)i * 10, 0, 10);
    pool_unmap(h);
    return t;
}

/* AP 4d15:02d8 huff_free(tab) */
void huff_free(u16 t)
{
    ap_pool_free_1d32(HT_H(t));
    lib_free(t);
}

/* AP 4d15:02f6 huff_add(tab, sym, freq) -> 1 / 0 */
s16 huff_add(u16 t, s16 sym, s32 freq)
{
    if (freq < 1) return 0;
    if (HT_TOTAL(t) + freq > 0x7FFFFFFE) return 0;
    u16 m = stk_mark();
    u16 idx = stk_alloc(2);
    if (huff_find(t, sym, idx) == 0) {
        if (HT_N(t) == HT_M(t)) { stk_release(m); return 0; }
        u32 b = far_lin(pool_map_save(HT_H(t)));
        for (s16 i = HT_M(t); DSS(idx) < i; i--)
            memcpy(mem + ND(b, i, 0), mem + ND(b, i - 1, 0), 10);
        pool_unmap(HT_H(t));
        DSW(t + 2) += 1;
    }
    u32 b = far_lin(pool_map_save(HT_H(t)));
    HT_TOTAL(t) += freq;
    NL_SET(b, DSS(idx), NL(b, DSS(idx)) + freq);       /* quirk: += on an inserted slot */
    NW_SET(b, DSS(idx), 6, sym);
    pool_unmap(HT_H(t));
    stk_release(m);
    return 1;
}

/* AP 4d15:03e4 huff_build(tab) -> 1 */
s16 huff_build(u16 t)
{
    FarPtr nodes = pool_map_save(HT_H(t));
    u32 b = far_lin(nodes);
    s16 n = HT_N(t);
    for (s16 i = 0; i < n; i++) NW_SET(b, i, 4, 0);
    s16 k = n;
    for (s16 left = n; --left != 0;) {
        s32 m2 = 0x7FFFFFFF, m1 = 0x7FFFFFFF;
        s16 d = -1, c = -1;
        for (s16 i = 0; i < k; i++) {
            if (NW(b, i, 4) != 0) continue;
            s32 f = NL(b, i);
            if (f < m1) { m2 = m1; d = c; c = i; m1 = f; }
            else if (f < m2) { d = i; m2 = f; }
        }
        if (m1 == m2 && NW(b, d, 6) < NW(b, c, 6)) { s16 x = c; c = d; d = x; }
        NW_SET(b, d, 4, k);
        NW_SET(b, c, 4, k);
        NW_SET(b, k, 6, c);
        NW_SET(b, k, 8, d);
        NL_SET(b, k, m1 + m2);
        NW_SET(b, k, 4, 0);
        k++;
    }
    for (s16 i = 0; i < n; i++) NW_SET(b, i, 8, huff_depth(t, i, nodes));
    pool_unmap(HT_H(t));
    return 1;
}

/* AP 6de4:0000 huff_find(tab, sym, &idx) -> found */
s16 huff_find(u16 t, s16 sym, u16 idx_ds)
{
    if (HT_M(t) == 0) { DSW(idx_ds) = 0; return 0; }
    s16 m = HT_M(t);
    u32 b = far_lin(pool_map_save(HT_H(t)));
    s16 i = 0;
    while (i < m && sym > NW(b, i, 6)) i++;
    DSS(idx_ds) = i;
    s16 r = !(i == m || NW(b, i, 6) != sym);
    pool_unmap(HT_H(t));
    return r;
}

/* AP 6922:0000 huff_code(tab, sym, &nbits) -> code bit buffer / 0 */
u16 huff_code(u16 t, s16 sym, u16 nbits_ds)
{
    u16 m = stk_mark();
    u16 idx = stk_alloc(2);
    if (huff_find(t, sym, idx) == 0) {
        DSW(nbits_ds) = 0;
        stk_release(m);
        return 0;
    }
    u16 buf = HT_CODE(t);
    memset(DSP(buf), 0, (u16)((HT_N(t) + 6) >> 3));
    u32 b = far_lin(pool_map_save(HT_H(t)));
    s16 root = (s16)((HT_N(t) - 1) * 2);
    s16 i = DSS(idx);
    s16 L = NW(b, i, 8);
    DSS(nbits_ds) = L;
    while (i != root) {
        L--;
        s16 p = NW(b, i, 4);
        s16 one = NW(b, p, 8) == i;
        i = p;
        if (one) DSB(buf + (L >> 3)) |= (u8)(1 << (L & 7));
    }
    pool_unmap(HT_H(t));
    stk_release(m);
    return buf;
}

/* AP 6922:00dd huff_dec_begin(tab) */
void huff_dec_begin(u16 t)
{
    DSW(0x51A8) = t;
    ds_far_wr(0x51A0, pool_map_save(HT_H(t)));
    DSL(0x51A4) = DSL(t + 0x0C);
}

/* AP 6922:0105 huff_dec_bit(b) -> 1 at a leaf (symbol in DS:519E) */
s16 huff_dec_bit(s16 bit)
{
    FarPtr cur = ds_far(0x51A4);
    s16 child = (s16)rd16(cur.seg, (u16)(cur.off + bit * 2 + 6));
    FarPtr base = ds_far(0x51A0);
    if (child < DSS(DSW(0x51A8))) {
        DSW(0x519E) = rd16(base.seg, (u16)(base.off + child * 10 + 6));
        return 1;
    }
    ds_far_wr(0x51A4, far_make(base.seg, (u16)(base.off + child * 10)));
    return 0;
}

/* AP 6922:0155 huff_dec_end() -> symbol */
s16 huff_dec_end(void)
{
    pool_unmap(DSW(DSW(0x51A8) + 0x0A));
    return DSS(0x519E);
}
