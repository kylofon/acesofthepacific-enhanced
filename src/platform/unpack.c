/* Packed streams (platform_resources.md §6.4): INTRO 05da:6567..7c51, PS 16e3:66b5.., AP 1d32:25c6..
 * The read side (stored / RLE / LZW = compress 4.0 block mode) exactly as the original, including the
 * 128-byte ring that holds the bytes decoded beyond a request, the global (not per-stream) LZW state
 * and the scratch (prefix u16[4096] at +0, suffix u8[4096] at +2720h, decode stack at +3720h,
 * addressed from the scratch segment). The stream records and every working variable live in mem[].
 * PORT: the writer (compressor) and the memory-mode stream are dead code in all three programs (no
 * callers) and are not ported; calling them is fatal. */
#include "platform/rtl_priv.h"

#include "host.h"

/* stream record (calloc(1, 21h)) */
#define PR_ring     0x00
#define PR_scratch  0x02
#define PR_file     0x06
#define PR_consumed 0x0A
#define PR_limit    0x0E
#define PR_unpacked 0x12
#define PR_pos      0x16
#define PR_head     0x1A
#define PR_tail     0x1B
#define PR_start    0x1C
#define PR_flags    0x20

#define PB(f) DSB((u16)(LW(G_pk_cur) + (f)))
#define PW(f) DSW((u16)(LW(G_pk_cur) + (f)))
#define PLL(f) DSL((u16)(LW(G_pk_cur) + (f)))

/* method table entries (0Eh bytes): ring, scratch read, scratch write, read, write, winit, rinit */
static u16 meth(int m, int f) { return DSW((u16)(lds(G_pk_methods) + 0x0E * m + f)); }

static FarPtr pada(FarPtr p, u16 n) { return far_norm(far_lin(p) + n); }   /* PADA@: add, normalise */

/* ------------------------------------------------------------------ helpers */

/* INTRO 05da:668b / PS 16e3:67d9 / AP 1d32:26ea pk_getbyte: next packed byte, -1 at the limit */
static s16 pk_getbyte(void)
{
    if (PLL(PR_consumed) == PLL(PR_limit)) return -1;
    PLL(PR_consumed) += 1;
    if (LB(G_pk_flags) & 0x20) return my_fgetc(LW(G_pk_file));
    FarPtr s = ds_far(lds(G_pk_src));
    u8 c = rd8(s.seg, s.off);
    ds_far_wr(lds(G_pk_src), pada(s, 1));             /* PINA@ */
    return c;
}

/* INTRO 05da:66e3 / PS 16e3:6831 / AP 1d32:2742 pk_getbytes(dst near, n): up to n packed bytes */
static u16 pk_getbytes(u16 dst, u16 n)
{
    s32 left = (s32)(PLL(PR_limit) - PLL(PR_consumed));
    if (left == 0) return 0;
    u32 k;
    s16 hi = (s16)(left >> 16);
    if (0 < hi) k = n;
    else if (0 > hi) k = (u32)left;
    else k = (n <= (u16)left) ? n : (u32)left;
    PLL(PR_consumed) += k;
    if (LB(G_pk_flags) & 0x20) return (u16)my_fread(dst, 1, (s16)k, LW(G_pk_file));
    FarPtr s = ds_far(lds(G_pk_src));
    huge_memcpy_fp(ds_ptr(dst), s, (u16)k);
    ds_far_wr(lds(G_pk_src), pada(s, (u16)k));
    return (u16)k;
}

/* INTRO 05da:661f / PS 16e3:676d / AP 1d32:267e pk_copy_file(far dst, n): my_fread in pieces of
 * at most 50 bytes through pk_litbuf */
static void pk_copy_file(FarPtr dst, u16 n)
{
    s16 di = 1;
    u16 buf = lds(G_pk_litbuf);
    while (n > 0 && di > 0) {
        u16 k = n > 0x32 ? 0x32 : n;
        di = my_fread(buf, 1, (s16)k, LW(G_pk_file));
        n = (u16)(n - di);
        huge_memcpy_fp(dst, ds_ptr(buf), (u16)di);
        dst = pada(dst, (u16)di);
    }
}

/* INTRO 05da:6887 / PS 16e3:69d5 / AP 1d32:28e6 pk_put1: one decoded byte to the request or the ring */
static s16 pk_put1(u8 c)
{
    if (LW(G_pk_want) < 1) {
        u8 h = PB(PR_head);
        PB(PR_head) = (u8)(h + 1);
        DSB((u16)(LW(G_pk_ring) + h)) = c;
        return 0;
    }
    FarPtr d = ds_far(lds(G_pk_dest));
    if (LB(G_pk_io) & 0x40) wr8(d.seg, d.off, c);
    ds_far_wr(lds(G_pk_dest), pada(d, 1));
    LW(G_pk_want) = (u16)(LW(G_pk_want) - 1);
    return 1;
}

/* ------------------------------------------------------------------ methods: read side */

/* INTRO 05da:6567 / PS 16e3:66b5 / AP 1d32:25c6 m0_read: stored */
static void m0_read(void)
{
    s16 ok = 1, c;
    while (ok && (c = pk_getbyte()) != -1) ok = pk_put1((u8)c);
}

/* INTRO 05da:678a / PS 16e3:68d8 / AP 1d32:27e9 rle_literal(n): copy the next n bytes */
static s16 rle_literal(u16 n)
{
    PLL(PR_consumed) += n;
    if (LW(G_pk_want) < n) {
        PB(PR_head) = (u8)(PB(PR_head) + n);
        pk_copy_file(ds_ptr(LW(G_pk_ring)), n);          /* to the ring BASE (it is empty here) */
        return 0;
    }
    if (LB(G_pk_io) & 0x40) pk_copy_file(ds_far(lds(G_pk_dest)), n);
    else my_fseek(LW(G_pk_file), (s32)n, 1);
    LW(G_pk_want) = (u16)(LW(G_pk_want) - n);
    ds_far_wr(lds(G_pk_dest), pada(ds_far(lds(G_pk_dest)), n));
    return 1;
}

/* INTRO 05da:680d / PS 16e3:695b / AP 1d32:286c rle_fill(v, n): v repeated n times */
static s16 rle_fill(u8 v, u16 n)
{
    if (LW(G_pk_want) < n) {
        huge_memset_fp(ds_ptr((u16)(LW(G_pk_ring) + PB(PR_head))), v, n);
        PB(PR_head) = (u8)(PB(PR_head) + n);
        return 0;
    }
    if (LB(G_pk_io) & 0x40) huge_memset_fp(ds_far(lds(G_pk_dest)), v, n);
    LW(G_pk_want) = (u16)(LW(G_pk_want) - n);
    ds_far_wr(lds(G_pk_dest), pada(ds_far(lds(G_pk_dest)), n));
    return 1;
}

/* INTRO 05da:658b / PS 16e3:66d9 / AP 1d32:25ea rle_read (RLE). PORT: the memory-source variant
 * rle_read_mem (05da:760d) has no reachable caller. */
static void rle_read(void)
{
    if (!(LB(G_pk_io) & 0x20)) host_fatal("rle_read_mem: memory-mode packed streams are not ported");
    s16 ok = 1, c;
    while (ok && (c = pk_getbyte()) != -1) {
        if (c & 0x80) {
            s16 v = pk_getbyte();
            ok = rle_fill((u8)v, (u16)(c & 0x7F));
        } else {
            ok = rle_literal((u16)(c & 0x7F));
        }
    }
}

/* ---- LZW (compress 4.0), the global state in lzw_* */

#define LZW_PREFIX(seg, i)   (*(u16 *)mp((seg), (u16)(2 * (i))))
#define LZW_SUFFIX(seg, i)   (*(u8 *)mp((seg), (u16)(0x2720 + (i))))

/* INTRO 05da:7253 / PS 16e3:73a1 / AP 1d32:32b2 lzw_read_init */
static void lzw_read_init(void)
{
    FarPtr sc = ds_far(lds(G_pk_scratch));
    huge_memset_fp(sc, 0, 0x3AA1);
    LW(G_lzw_nbits) = 9;
    LW(G_lzw_maxcode) = 0x1FF;
    for (s16 i = 0xFF; i >= 0; i--) {
        FarPtr p = huge_add(sc, (u32)(2 * i));
        wr16(p.seg, p.off, 0);
        FarPtr q = huge_add(huge_add(sc, (u32)i), 0x2720);
        wr8(q.seg, q.off, (u8)i);
    }
    LW(G_lzw_free) = 0x101;
    LW(G_lzw_clear) = 0;
    LB(G_lzw_first) = 1;
    LB(G_lzw_pending) = 0;
    LW(G_lzw_offset) = 0;
    LW(G_lzw_size) = 0;
    ds_far_wr(lds(G_lzw_stackp), huge_add(sc, 0x3720));
}

/* INTRO 05da:7546 / PS 16e3:7694 / AP 1d32:35a5 lzw_getcode: the next code or -1 */
static s16 lzw_getcode(void)
{
    u16 ax;
    bool fill = false;
    if ((s16)LW(G_lzw_free) > (s16)LW(G_lzw_maxcode)) {
        u16 cx = (u16)(LW(G_lzw_nbits) + 1);
        LW(G_lzw_nbits) = cx;
        LW(G_lzw_maxcode) = (u8)cx == 12 ? 0x1000 : (u16)((1u << (cx & 0x1F)) - 1);
        if (LW(G_lzw_clear) == 0) fill = true;
    }
    if (!fill && LW(G_lzw_clear) != 0) {
        LW(G_lzw_nbits) = 9;
        LW(G_lzw_maxcode) = 0x1FF;
        LW(G_lzw_clear) = 0;
        fill = true;
    }
    if (!fill && (s16)LW(G_lzw_offset) >= (s16)LW(G_lzw_size)) fill = true;
    if (fill) {
        u16 si = LW(G_lzw_nbits);
        s16 got = (s16)pk_getbytes(lds(G_lzw_buf), si);
        if (got <= 0) { LW(G_lzw_size) = (u16)got; return -1; }
        LW(G_lzw_offset) = 0;
        LW(G_lzw_size) = (u16)(got * 8 - (si - 1));
        ax = 0;
    } else {
        ax = LW(G_lzw_offset);
    }
    u16 buf = lds(G_lzw_buf);
    u16 bx = LW(G_lzw_nbits);
    u8 ch = (u8)ax;
    LW(G_lzw_offset) = (u16)(ax + bx);
    u16 si = (u16)(buf + (ax >> 3));
    ch &= 7;
    u16 dx = (u16)(DSB(si++) >> ch);
    ch = (u8)(8 - ch);
    u8 bl = (u8)((u8)bx - ch);
    if ((s8)bl >= 8) {
        dx |= (u16)(DSB(si++) << ch);
        ch = (u8)(ch + 8);
        bl = (u8)(bl - 8);
    }
    u16 code = (u16)((DSB((u16)(lds(G_lzw_rmask) + bl)) & DSB(si)) << ch) | dx;
    return (s16)code;
}

/* The string output loop of lzw_read (05da:747c/74cf): stack chars from index si down to 0 go to the
 * request; the char that does not fit goes to the ring, and the decoder stops with pending = 1.
 * Returns true when the whole string was output (then cx - 1 is the request left). */
static bool lzw_emit(u16 stk_seg, s16 si, u16 *want_left)
{
    u16 cx = (u16)(LW(G_pk_want) + 1);
    FarPtr d = ds_far(lds(G_pk_dest));
    bool store = (LB(G_pk_io) & 0x40) != 0;
    for (;;) {
        u8 al = rd8(stk_seg, (u16)si);
        si++;
        if (--cx == 0) {                              /* 74da: the request is full */
            ds_far_wr(lds(G_pk_dest), d);
            LW(G_lzw_resume) = (u16)si;
            u8 h = PB(PR_head);
            PW(PR_head) = (u16)(PW(PR_head) + 1);    /* INC WORD [rec+1Ah] */
            DSB((u16)(LW(G_pk_ring) + h)) = al;
            LW(G_pk_want) = 0;
            LB(G_lzw_pending) = 1;
            return false;
        }
        if (store) wr8(d.seg, d.off, al);
        d.off = (u16)(d.off + 1);                     /* ES:DI, not normalised */
        si = (s16)(si - 2);
        if (si < 0) break;
    }
    cx--;                                             /* 7503 */
    LW(G_pk_want) = cx;
    ds_far_wr(lds(G_pk_dest), d);
    *want_left = cx;
    return true;
}

/* INTRO 05da:7343 / PS 16e3:7491 / AP 1d32:33a2 lzw_read (asm) */
static void lzw_read(void)
{
    u16 sseg = ds_far(lds(G_pk_scratch)).seg;
    u16 stk = (u16)(sseg + 0x372);                    /* the decode stack segment (scratch + 3720h) */
    u16 left;
    s16 code;
    if (LB(G_lzw_pending)) {                          /* the rest of the last string */
        LB(G_lzw_pending) = 0;
        s16 si = (s16)(LW(G_lzw_resume) - 2);        /* enters the loop at SUB SI,2 */
        if (si >= 0 && !lzw_emit(stk, si, &left)) return;
        goto add_entry;
    }
    if (LB(G_lzw_first)) {
        LB(G_lzw_first) = 0;
        code = lzw_getcode();                         /* (no -1 check) */
        LW(G_lzw_oldcode) = (u16)code;
        LW(G_lzw_finchar) = (u16)code;
        pk_put1((u8)code);
    }
    for (;;) {
        code = lzw_getcode();
        if (code < 0) return;
        if (code == 0x100) {
            for (int i = 0; i < 0x100; i++) LZW_PREFIX(sseg, i) = 0;
            LW(G_lzw_clear) = 1;
            LW(G_lzw_free) = 0x100;
            code = lzw_getcode();
            if (code < 0) return;
        }
        u16 di = 0;
        u16 si = (u16)code;
        LW(G_lzw_incode) = (u16)code;
        if ((s16)code >= (s16)LW(G_lzw_free)) {
            wr8(stk, di++, (u8)LW(G_lzw_finchar));
            si = LW(G_lzw_oldcode);
        }
        while ((s16)si >= 0x100) {
            wr8(stk, di++, LZW_SUFFIX(sseg, si));
            si = LZW_PREFIX(sseg, si);
        }
        u8 al = LZW_SUFFIX(sseg, si);
        wr8(stk, di++, al);
        LW(G_lzw_finchar) = al;
        if (!lzw_emit(stk, (s16)(di - 1), &left)) return;
add_entry:
        if ((s16)LW(G_lzw_free) < 0x1000) {
            u16 fe = LW(G_lzw_free);
            LZW_PREFIX(sseg, fe) = LW(G_lzw_oldcode);
            LW(G_lzw_free) = (u16)(fe + 1);
            LZW_SUFFIX(sseg, fe) = (u8)LW(G_lzw_finchar);
        }
        LW(G_lzw_oldcode) = LW(G_lzw_incode);
    }
}

/* ------------------------------------------------------------------ stream management */

/* INTRO 05da:6926 / PS 16e3:6a74 / AP 1d32:2985 pk_select: load stream id into the working globals */
static s16 pk_select(s16 id)
{
    if (id < 0 || id >= 100) return 0;
    u16 r = DSW((u16)(lds(G_pk_streams) + 2 * id));
    LW(G_pk_cur) = r;
    if (!r) return 0;
    ds_far_wr(lds(G_pk_scratch), far_rd(cur_dgroup, (u16)(r + PR_scratch)));
    LW(G_pk_ring) = DSW(r);
    LB(G_pk_flags) = DSB((u16)(r + PR_flags));
    LB(G_pk_method) = LB(G_pk_flags) & 0x1F;
    if (LB(G_pk_flags) & 0x20) {
        LW(G_pk_file) = DSW((u16)(r + PR_file));
        LB(G_pk_io) = 0x20;
    } else {
        LB(G_pk_io) = 0;
        FarPtr b = far_rd(cur_dgroup, (u16)(r + PR_file));
        ds_far_wr(lds(G_pk_src), huge_norm(far_norm(far_lin(b) + DSL((u16)(r + PR_consumed)))));
    }
    return 1;
}

/* INTRO 05da:69af / PS 16e3:6afd / AP 1d32:2a0e pk_mode_is_read: the mode contains 'r' */
static s16 pk_mode_is_read(u16 mode)
{
    for (u16 s = mode; DSB(s); s++)
        if (DSB(s) == 'r') return 1;
    return 0;
}

/* INTRO 05da:69cf / PS 16e3:6b1d / AP 1d32:2a2e pk_nfree */
static void pk_nfree(u16 p) { if (p) rtl_free(p); }

/* INTRO 05da:69e5 / PS 16e3:6b33 / AP 1d32:2a44 pk_free: ring, own scratch, record; returns -1 */
static s16 pk_free(s16 id)
{
    u16 r = DSW((u16)(lds(G_pk_streams) + 2 * id));
    LW(G_pk_cur) = r;
    if (r) {
        pk_nfree(DSW(r));
        FarPtr sc = far_rd(cur_dgroup, (u16)(r + PR_scratch));
        if (!far_is_null(sc) && far_is_null(ds_far(lds(G_pk_shared_scratch)))) dos_free(sc);
    }
    pk_nfree(LW(G_pk_cur));
    DSW((u16)(lds(G_pk_streams) + 2 * id)) = 0;
    return -1;
}

/* INTRO 05da:6a4f / PS 16e3:6b9d / AP 1d32:2aae pk_alloc_id: first free id, calloc(1, 21h) */
static s16 pk_alloc_id(void)
{
    s16 i = 0;
    while (i < 100 && DSW((u16)(lds(G_pk_streams) + 2 * i))) i++;
    if (i == 100) return -1;
    u16 r = rtl_calloc(1, 0x21);
    LW(G_pk_cur) = r;
    if (!r) return -1;
    DSW((u16)(lds(G_pk_streams) + 2 * i)) = r;
    return i;
}

/* INTRO 05da:6a9e / PS 16e3:6bec / AP 1d32:2afd pk_setup(method, mode): ring and scratch */
static s16 pk_setup(s16 m, u16 mode)
{
    if (m > 2) return -1;
    u16 ring = 0x80, sz;
    if (pk_mode_is_read(mode)) { ring = meth(m, 0); sz = meth(m, 2); }
    else sz = meth(m, 4);
    u16 r = LW(G_pk_cur);
    DSW(r) = rtl_calloc(1, ring);
    if (!DSW(r)) return -1;
    if (sz) {
        FarPtr sh = ds_far(lds(G_pk_shared_scratch));
        FarPtr sc = far_is_null(sh) ? dos_alloc(sz, 0, 0) : sh;
        far_wr(cur_dgroup, (u16)(r + PR_scratch), sc);
        ds_far_wr(lds(G_pk_scratch), sc);
        if (far_is_null(sc)) return -1;
    }
    DSB((u16)(r + PR_flags)) = (u8)m;
    return m;
}

static void method_read(void)
{
    switch (LB(G_pk_method)) {
    case 0: m0_read(); break;
    case 1: rle_read(); break;
    case 2: lzw_read(); break;
    default: host_fatal("pk: method %d", LB(G_pk_method));
    }
}

/* INTRO 05da:6b6c / PS 16e3:6cba / AP 1d32:2bcb pk_open(method, f, mode, packed_size) */
s16 pk_open(s16 method, u16 p1_ds, u16 mode_ds, u32 packed_size)
{
    (void)method;                                     /* the method byte of the stream is used */
    s16 id = pk_alloc_id();
    if (id == -1) return -1;
    PW(PR_file) = p1_ds;
    PLL(PR_start) = (u32)my_ftell(p1_ds);
    PLL(PR_consumed) = 5;
    if (!pk_mode_is_read(mode_ds)) host_fatal("pk_open: the packed-stream writer is not ported");
    u8 m = (u8)my_fgetc(p1_ds);
    PB(PR_flags) = m;
    if (pk_setup(m, mode_ds) == -1) {
        my_fseek(p1_ds, -1, 1);
        return pk_free(id);
    }
    PLL(PR_limit) = packed_size;
    my_fread((u16)(LW(G_pk_cur) + PR_unpacked), 1, 4, p1_ds);
    if (meth(m, 0xC)) lzw_read_init();               /* read-init (LZW only) */
    PB(PR_flags) |= 0x40;
    PB(PR_flags) |= 0x20;
    return id;
}

/* INTRO 05da:6cb9 / PS 16e3:6e07 / AP 1d32:2d18 pk_open_mem (no callers) */
s16 pk_open_mem(s16 method, FarPtr buf, u16 mode_ds, u32 size)
{
    (void)method; (void)buf; (void)mode_ds; (void)size;
    host_fatal("pk_open_mem: memory-mode packed streams are not ported (dead code)");
}

/* INTRO 05da:6e85 / PS 16e3:6fd3 / AP 1d32:2ee4 pk_from_ring: serve the request from the ring */
static void pk_from_ring(void)
{
    u16 t = PB(PR_tail);
    u16 k = (u16)(PB(PR_head) - t);
    if (k > LW(G_pk_want)) {
        k = LW(G_pk_want);
        PB(PR_tail) = (u8)(t + k);
    } else {
        PB(PR_head) = 0;
        PB(PR_tail) = 0;
    }
    if (!k) return;
    if (LB(G_pk_io) & 0x40) huge_memcpy_fp(ds_far(lds(G_pk_dest)), ds_ptr((u16)(LW(G_pk_ring) + t)), k);
    LW(G_pk_want) = (u16)(LW(G_pk_want) - k);
    ds_far_wr(lds(G_pk_dest), pada(ds_far(lds(G_pk_dest)), k));
}

/* INTRO 05da:6efa / PS 16e3:7048 / AP 1d32:2f59 pk_fill(id, n) */
static u16 pk_fill(s16 id, u16 n)
{
    (void)id;
    LW(G_pk_want) = n;
    pk_from_ring();
    if (LW(G_pk_want)) {
        method_read();
        if (LW(G_pk_want)) pk_from_ring();
    }
    u16 got = (u16)(n - LW(G_pk_want));
    PLL(PR_pos) += got;
    return got;
}

/* INTRO 05da:6f3d / PS 16e3:708b / AP 1d32:2f9c pk_read(id, dst, n) */
u16 pk_read(s16 id, FarPtr dst, u16 n)
{
    if (!pk_select(id)) return 0xFFFF;
    ds_far_wr(lds(G_pk_dest), huge_norm(dst));
    LB(G_pk_io) |= 0x40;
    return pk_fill(id, n);
}

/* INTRO 05da:702d / PS 16e3:717b / AP 1d32:308c pk_size: the unpacked size */
s32 pk_size(s16 id)
{
    if (!pk_select(id)) return -1;
    return (s32)PLL(PR_unpacked);
}

/* INTRO 05da:71a3 / PS 16e3:72f1 / AP 1d32:3202 pk_rewind */
static s16 pk_rewind(s16 id)
{
    if (!pk_select(id) || !(LB(G_pk_flags) & 0x40)) return -1;
    if (meth(LB(G_pk_method), 0xC)) lzw_read_init();
    PLL(PR_consumed) = 5;
    if (PB(PR_flags) & 0x20) my_fseek(LW(G_pk_file), (s32)(PLL(PR_start) + 5), 0);
    else {
        FarPtr b = far_rd(cur_dgroup, (u16)(LW(G_pk_cur) + PR_file));
        ds_far_wr(lds(G_pk_src), huge_norm(far_norm(far_lin(b) + 5)));
    }
    PLL(PR_pos) = 0;
    PB(PR_tail) = 0;
    PB(PR_head) = 0;
    return 0;
}

/* INTRO 05da:7050 / PS 16e3:719e / AP 1d32:30af pk_seek(id, off, whence): decode and discard */
u16 pk_seek(s16 id, s32 off, s16 whence)
{
    if (!pk_select(id)) return 0xFFFF;
    s32 t = whence == 1 ? (s32)PLL(PR_pos) : whence == 2 ? (s32)PLL(PR_unpacked) : 0;
    t += off;
    if ((s32)PLL(PR_pos) == t) return (u16)t;
    s32 delta;
    if ((s32)PLL(PR_pos) > t) {
        pk_rewind(id);
        if (t <= 0) return 0;
        delta = t;
    } else if ((s32)PLL(PR_unpacked) <= t) {
        delta = (s32)(PLL(PR_unpacked) - PLL(PR_pos));
    } else {
        delta = t - (s32)PLL(PR_pos);
    }
    bool first = true;
    for (;;) {
        if (!first && !(PB(PR_flags) & 0x20)) {
            FarPtr b = far_rd(cur_dgroup, (u16)(LW(G_pk_cur) + PR_file));
            ds_far_wr(lds(G_pk_src), huge_norm(far_norm(far_lin(b) + PLL(PR_consumed))));
        }
        first = false;
        u16 k = (delta >> 16) > 0 || (u16)delta >= 0x7D00 ? 0x7D00 : (u16)delta;
        u16 got = pk_fill(id, k);
        if (got == 0 && delta != 0) break;           /* PORT: the original loops forever here (R1) */
        delta -= got;
        if (delta == 0) break;
    }
    return (u16)PLL(PR_pos);
}

/* INTRO 05da:6db5 / PS 16e3:6f03 / AP 1d32:2e14 pk_close: (writer: flush) and free; 0 for a reader */
u16 pk_close(s16 id)
{
    if (!pk_select(id)) return 0xFFFF;
    LW(G_pk_count) = 0;
    if (!(LB(G_pk_flags) & 0x40)) host_fatal("pk_close: the packed-stream writer is not ported");
    pk_free(id);
    return LW(G_pk_count);
}

/* INTRO 05da:6f7a / PS 16e3:70c8 / AP 1d32:2fd9 pk_write, and the writer methods m0_write 65d9,
 * rle_write 7c51, lzw_write_init 7700, lzw_write 7775, lzw_output 7988, lzw_cl_block 7b2b,
 * lzw_cl_hash 7c03, pk_putbyte 68d5, pk_ldiv 7b15: dead code (no callers). PORT: not ported. */
u16 pk_write(s16 id, FarPtr src, u16 n)
{
    (void)id; (void)src; (void)n;
    host_fatal("pk_write: the packed-stream writer is not ported (dead code)");
}

/* INTRO 05da:731d / 7322 (PS 16e3:746b / 7470, AP 1d32:337c / 3381): empty functions */
void pk_nop1(void) { }
void pk_nop2(void) { }
