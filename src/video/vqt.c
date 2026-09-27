/* The VQT quadtree decoders (video_pictures.md §3-5): the sprite decoder with flips (INTRO 0dc5 /
 * PS 383a / AP 6f1c) and the screen / memory decoders (INTRO 0e2c / PS 38a1 / AP 2651).
 *
 * The decoder's function pointers stay far pointers in DGROUP as in the original (vqt_putpix =
 * gfx_put_pixel or the driver's entry-22 pointer, vqt_bits_fn, vqt_fill_fn, vqt_src_fn, vqt_rect_fn) and
 * are called through codeptr (vid_lookup). */
#include "video/vid_priv.h"

typedef u16 (*BitsFn)(s16 n);
typedef void (*PutFn)(s16 x, s16 y, s16 c);
typedef void (*FillFn)(s16 x0, s16 y0, s16 x1, s16 y1);
typedef void (*RectFn)(s16 x, s16 y, s16 w, s16 h);

#define CTX (DSW(lds(G_vqt_ctx)))

static u16 call_bits(s16 n) { return ((BitsFn)vid_lookup(ds_far(lds(G_vqt_bits_fn))))(n); }
static void call_put(s16 x, s16 y, u8 v) { ((PutFn)vid_lookup(ds_far(lds(G_vqt_putpix))))(x, y, v); }
static u16 call_src(s16 n) { return ((BitsFn)vid_lookup(ds_far(lds(G_vqt_src_fn))))(n); }

/* INTRO 0e2c:0007 / PS 38a1:0007 / AP 2651:0006 — the bit reader (§3): n bits at the context's bit position
 * (the byte offset is added to the stream offset only: 64 KB wrap) */
u16 vqt_bits(s16 n)
{
    u16 c = CTX;
    u16 mask = (u16)(((u16)(0xFF00 << (n & 15)) | (u16)(0xFF00 >> (16 - (n & 15)))) & 0x00FF);
    if ((n & 15) == 0) mask = 0;
    u32 p = DSL(c);
    DSL(c) = p + (u16)n;
    FarPtr s = ds_far((u16)(c + 4));
    u16 w = rd16(s.seg, (u16)(s.off + (u16)(p >> 3)));
    return (u16)((w >> (p & 7)) & mask);
}

/* The inlined reads of the screen/memory decoders: the shifted word, unmasked (callers use the low byte
 * or mask 4 bits). Also returns the pre-read high word >> 3 (what is left in DX). */
static u16 bits_inline(s16 n, u16 *dx_out)
{
    u16 c = CTX;
    u32 p = DSL(c);
    DSL(c) = p + (u16)n;
    FarPtr s = ds_far((u16)(c + 4));
    u16 w = rd16(s.seg, (u16)(s.off + (u16)(p >> 3)));
    if (dx_out) *dx_out = (u16)((p >> 16) >> 3);
    return (u16)(w >> (p & 7));
}

/* INTRO 0dc5:000a / PS 383a:000a / AP 6f1c:0000 — returns &vqt_ctx0 or 0 if busy */
u16 vqt_begin(FarPtr stream)
{
    if (LW(G_vqt_busy) != 0) return 0;
    LW(G_vqt_busy) = 1;
    u16 c = lds(G_vqt_ctx0);
    ds_far_wr((u16)(c + 4), stream);
    DSL(c) = 0;
    return c;
}

/* INTRO 0dc5:003e / PS 383a:003e / AP 6f1c:0034 */
void vqt_end(void) { LW(G_vqt_busy) = 0; }

static s16 bitlen8(u8 v) { s16 n = 0; while (v) { n++; v >>= 1; } return n; }

/* INTRO 0dc5:0130 / PS 383a:0130 / AP 6f1c:0126 */
u16 vqt_tab_read(s16 n)
{
    u8 i = (u8)call_bits(n);
    return DSB((u16)(LW(G_vqt_tab) + i));
}

/* the three flipped leaf fillers (x0, y0, x1, y1), ends exclusive — §4.5 */
static void fill_px(s16 col, s16 row)
{
    u8 v = (u8)call_src((s16)LW(G_vqt_nbits));
    if (v || LB(G_vqt_draw_zero)) call_put(col, row, v);
}
/* INTRO 0dc5:035e / PS 383a:035e / AP 6f1c:0354 */
void vqt_fill_h(s16 x0, s16 y0, s16 x1, s16 y1)
{
    for (s16 c = (s16)(x1 - 1); c >= x0; c--)
        for (s16 r = y0; r < y1; r++) fill_px(c, r);
}
/* INTRO 0dc5:03aa / PS 383a:03aa / AP 6f1c:03a0 */
void vqt_fill_v(s16 x0, s16 y0, s16 x1, s16 y1)
{
    for (s16 c = x0; c < x1; c++)
        for (s16 r = (s16)(y1 - 1); r >= y0; r--) fill_px(c, r);
}
/* INTRO 0dc5:03f6 / PS 383a:03f6 / AP 6f1c:03ec */
void vqt_fill_hv(s16 x0, s16 y0, s16 x1, s16 y1)
{
    for (s16 c = (s16)(x1 - 1); c >= x0; c--)
        for (s16 r = (s16)(y1 - 1); r >= y0; r--) fill_px(c, r);
}

/* INTRO 0dc5:0445 / PS 383a:0445 / AP 6f1c:043b (near) — §4.4 */
void vqt_leaf(s16 x, s16 y, s16 w, s16 h)
{
    if (w == 0 || h == 0) return;
    if (w == 1 && h == 1) {
        u8 v = (u8)call_bits(8);
        if (v || LB(G_vqt_draw_zero)) call_put(x, y, v);
        return;
    }
    u32 size = (u32)(u16)w * (u16)h;
    s16 b1 = 8;
    if (size < 256) { b1 = 0; u8 a = (u8)(size - 1); if (a) b1 = bitlen8(a); }
    u8 k = (u8)call_bits(b1);
    LW(G_vqt_nbits) = (u16)bitlen8(k);
    u16 ncol = (u16)(k + 1);
    u32 lhs = size * 8, rhs = size * LW(G_vqt_nbits) + (u32)(s32)(s16)(ncol * 8);
    s16 x1 = (s16)(x + w), y1 = (s16)(y + h);
    FarPtr fill = ds_far(lds(G_vqt_fill_fn));
    if (lhs <= rhs) {                                            /* RAW */
        if (!far_is_null(fill)) {
            LW(G_vqt_nbits) = 8;
            ds_far_wr(lds(G_vqt_src_fn), ds_far(lds(G_vqt_bits_fn)));
            ((FillFn)vid_lookup(fill))(x, y, x1, y1);
        } else {
            for (s16 i = x; i < x1; i++)
                for (s16 j = y; j < y1; j++) {
                    u8 v = (u8)vqt_bits(8);                      /* direct call of 0e2c:0007 */
                    if (v) gfx_put_pixel(i, j, v);                /* direct call of 05da:48fd */
                }
        }
        return;
    }
    if (ncol == 1) {                                             /* FILL */
        u8 v = (u8)call_bits(8);
        DRVB(D_FILL_COL) = v;
        DRVB(D_DRAW_COL) = v;
        if (v || LB(G_vqt_draw_zero)) ((RectFn)vid_lookup(ds_far(lds(G_vqt_rect_fn))))(x, y, w, h);
        return;
    }
    u16 mark = stk_mark();                                       /* TABLE: 256-byte table on the stack */
    u16 tab = stk_alloc(0x110);
    LW(G_vqt_tab) = tab;
    u16 t = tab;
    s16 cnt = (s16)ncol;
    do { DSB(t++) = (u8)call_bits(8); } while (--cnt > 0);
    if (!far_is_null(fill)) {
        ds_far_wr(lds(G_vqt_src_fn), codeptr_far_shared(F_vqt_tab_read));
        ((FillFn)vid_lookup(fill))(x, y, x1, y1);
    } else {
        for (s16 i = x; i < x1; i++)
            for (s16 j = y; j < y1; j++) {
                u8 v = DSB((u16)(LW(G_vqt_tab) + (u8)vqt_bits((s16)LW(G_vqt_nbits))));
                if (v) gfx_put_pixel(i, j, v);
            }
    }
    stk_release(mark);
}

/* INTRO 0dc5:01ef / PS 383a:01ef / AP 6f1c:01e5 — §4.3 */
void vqt_node(s16 x, s16 y, s16 w, s16 h)
{
    if (w == 0 || h == 0) return;
    s16 wl = (s16)(w >> 1), wr = (s16)((s16)(w + 1) >> 1), ht = (s16)(h >> 1), hb = (s16)((s16)(h + 1) >> 1);
    s16 offL = 0, offR = wl, offT = 0, offB = ht;
    if (LW(G_vqt_hflip)) { offL = wr; offR = 0; }
    if (LW(G_vqt_vflip)) { offT = hb; offB = 0; }
    u8 m = (u8)call_bits(4);
    ((m & 8) ? vqt_node : vqt_leaf)((s16)(x + offL), (s16)(y + offT), wl, ht);
    ((m & 4) ? vqt_node : vqt_leaf)((s16)(x + offR), (s16)(y + offT), wr, ht);
    ((m & 2) ? vqt_node : vqt_leaf)((s16)(x + offL), (s16)(y + offB), wl, hb);
    ((m & 1) ? vqt_node : vqt_leaf)((s16)(x + offR), (s16)(y + offB), wr, hb);
}

/* INTRO 0dc5:0149 / PS 383a:0149 / AP 6f1c:013f — §4.2 */
void vqt_root(s16 x, s16 y, s16 w, s16 h)
{
    LW(G_vqt_hflip) = (LW(G_vqt_flip) & 2) ? 1 : 0;
    LW(G_vqt_vflip) = (LW(G_vqt_flip) & 1) ? 1 : 0;
    FarPtr f = far_make(0, 0);
    if (LW(G_vqt_hflip)) f = codeptr_far_shared(LW(G_vqt_vflip) ? F_vqt_fill_hv : F_vqt_fill_h);
    else if (LW(G_vqt_vflip)) f = codeptr_far_shared(F_vqt_fill_v);
    ds_far_wr(lds(G_vqt_fill_fn), f);
    s16 saved = (s16)(s8)DRVB(D_FILL_ON);
    DRVB(D_FILL_ON) = 1;
    vqt_node(x, y, w, h);
    DRVB(D_FILL_ON) = (u8)saved;
}

/* INTRO 0dc5:0049 / PS 383a:0049 / AP 6f1c:003f — §4.1 */
void vqt_draw(u16 fr_ds, s16 x, s16 y, s16 flip)
{
    u8 s0e = DRVB(D_DRAW_COL), s0d = DRVB(D_FILL_COL), s03 = DRVB(D_CLIP_ON);
    u16 fs = DSW(fr_ds), fo = DSW(fr_ds + 2);
    FarPtr p = far_make((u16)(fs + (fo >> 4)), fo & 0x0F);
    LW(G_vqt_ctx) = vqt_begin(p);
    if (LW(G_vqt_ctx) != 0) {
        s16 w = DSS(fr_ds + 6), h = DSS(fr_ds + 8);
        if (x >= DRVS(D_CLIP_X1) && y >= DRVS(D_CLIP_Y1) && (s16)(x + w) <= DRVS(D_CLIP_X2) && (s16)(y + h) <= DRVS(D_CLIP_Y2)) {
            ds_far_wr(lds(G_vqt_putpix), drv_entry_ptr(22));
        } else {
            ds_far_wr(lds(G_vqt_putpix), codeptr_far_shared(F_gfx_put_pixel));
            DRVB(D_CLIP_ON) = 1;
        }
        LB(G_vqt_draw_zero) = 0;
        LW(G_vqt_flip) = (u16)flip;
        vqt_root(x, y, w, h);
        vqt_end();
    }
    DRVB(D_CLIP_ON) = s03;
    DRVB(D_DRAW_COL) = s0e;
    DRVB(D_FILL_COL) = s0d;
}

/* ------------------------------------------------------------------ screen / memory decoders */

/* The shared leaf body of 0e2c:0155 (memory) and 0e2c:056b (screen) — §5 */
static void mem_put(u16 x, u16 y, u8 v)
{
    u16 c = CTX;
    FarPtr d = ds_far((u16)(c + 8));
    wr8(d.seg, (u16)(d.off + DSW((u16)(c + 0x18 + 2 * y)) + x), v);
}

typedef enum { LEAF_MEM, LEAF_SCR } LeafKind;

static void leaf5(u16 x, u16 y, u16 w, u16 h, LeafKind kind)
{
    if (h == 0 || w == 0) return;
    DrvPutPixelFn put = kind == LEAF_SCR ? (DrvPutPixelFn)drv_entry(22) : NULL;
#define PUT(px, py, v) (kind == LEAF_MEM ? mem_put((px), (py), (v)) : put((s16)(px), (s16)(py), (v)))
    if (w == 1 && h == 1) {
        u8 v = (u8)bits_inline(8, NULL);
        PUT(x, y, v);
        return;
    }
    u16 size = (u16)((u8)w * (u8)h);                             /* mul bl: 8 x 8 bits */
    s16 b1 = 8;
    if ((size >> 8) == 0) { u8 a = (u8)(size - 1); b1 = 0; do { b1++; a >>= 1; } while (a); }
    u8 kk = (u8)(bits_inline(b1, NULL) & (u16)(b1 >= 8 ? 0xFF : (1 << b1) - 1));
    s16 nb = bitlen8(kk);
    u16 ncolw = (u16)(u8)(kk + 1);                               /* byte increment of the word k */
    u16 lhs = (u16)(size << 3), rhs = (u16)((u16)(size * (u16)nb) + (u16)(ncolw << 3));
    u16 x1 = (u16)(x + w), y1 = (u16)(y + h);
    if (!(lhs > rhs)) {                                          /* RAW, column-major, opaque */
        for (u16 i = x; (s16)i < (s16)x1; i++)
            for (u16 j = y; (s16)j < (s16)y1; j++) PUT(i, j, (u8)bits_inline(8, NULL));
        return;
    }
    if ((u8)ncolw == 1) {                                        /* FILL */
        u16 dx;
        u8 v = (u8)bits_inline(8, &dx);
        if (kind == LEAF_MEM) {
            u16 row = y;
            u16 n = h;
            do {
                u16 xx = x;
                u16 c = w;
                do { mem_put(xx++, row, v); } while (--c);
                row++;
            } while (--n);
        } else {
            u16 di = DSW((u16)(lds(G_drv_row_ofs) + 2 * y));
            u16 n = h;
            do {
                ((DrvHspanFn)drv_entry(10))(DRVW(D_PAGE_WRITE), di, (s16)x, (s16)w, v, v, dx);
                di = (u16)(di + 80);
            } while (--n);
        }
        return;
    }
    u8 tab[256];                                                 /* TABLE (byte counter: 0 = 256) */
    u8 cnt = (u8)ncolw;
    int t = 0;
    do { tab[t++ & 255] = (u8)bits_inline(8, NULL); } while (--cnt);
    u16 mask = (u16)(nb >= 8 ? 0xFF : (1 << nb) - 1);
    for (u16 i = x; (s16)i < (s16)x1; i++)
        for (u16 j = y; (s16)j < (s16)y1; j++) PUT(i, j, tab[(u8)(bits_inline((s16)nb, NULL) & mask)]);
#undef PUT
}

/* INTRO 0e2c:0155 / PS 38a1:0155 / AP 2651:0154 — §5.3 */
void vqt_mem_leaf(u16 x, u16 y, u16 w, u16 h) { leaf5(x, y, w, h, LEAF_MEM); }
/* INTRO 0e2c:056b / PS 38a1:056b / AP 2651:056a — §5.2 */
void vqt_scr_leaf(u16 x, u16 y, u16 w, u16 h) { leaf5(x, y, w, h, LEAF_SCR); }

static void node5(u16 x, u16 y, u16 w, u16 h, LeafKind kind)
{
    if ((w | h) == 0) return;
    u8 m = (u8)(bits_inline(4, NULL) & 0x0F);
    void (*nd)(u16, u16, u16, u16) = kind == LEAF_MEM ? vqt_mem_node : vqt_scr_node;
    void (*lf)(u16, u16, u16, u16) = kind == LEAF_MEM ? vqt_mem_leaf : vqt_scr_leaf;
    ((m & 8) ? nd : lf)(x, y, (u16)(w >> 1), (u16)(h >> 1));
    ((m & 4) ? nd : lf)((u16)((w >> 1) + x), y, (u16)((w + 1) >> 1), (u16)(h >> 1));
    ((m & 2) ? nd : lf)(x, (u16)((h >> 1) + y), (u16)(w >> 1), (u16)((h + 1) >> 1));
    ((m & 1) ? nd : lf)((u16)((w >> 1) + x), (u16)((h >> 1) + y), (u16)((w + 1) >> 1), (u16)((h + 1) >> 1));
}

/* INTRO 0e2c:004e / PS 38a1:004e / AP 2651:004d — §5.1 */
void vqt_mem_node(u16 x, u16 y, u16 w, u16 h) { node5(x, y, w, h, LEAF_MEM); }
/* INTRO 0e2c:0461 / PS 38a1:0461 / AP 2651:0460 — §5.1 */
void vqt_scr_node(u16 x, u16 y, u16 w, u16 h) { node5(x, y, w, h, LEAF_SCR); }

/* INTRO 0e2c:0446 / PS 38a1:0446 / AP 2651:0445 — rep movsw of n >> 1 words + 1 byte if n odd */
void far_copy(FarPtr dst, FarPtr src, u16 n)
{
    for (u16 i = 0; i < (u16)(n & ~1u); i++) wr8(dst.seg, (u16)(dst.off + i), rd8(src.seg, (u16)(src.off + i)));
    if (n & 1) wr8(dst.seg, (u16)(dst.off + n - 1), rd8(src.seg, (u16)(src.off + n - 1)));
}

__attribute__((constructor)) static void vqt_codeptrs(void)
{
    codeptr_register_shared(F_vqt_bits, (CodeFn)vqt_bits);
    codeptr_register_shared(F_vqt_tab_read, (CodeFn)vqt_tab_read);
    codeptr_register_shared(F_vqt_fill_h, (CodeFn)vqt_fill_h);
    codeptr_register_shared(F_vqt_fill_v, (CodeFn)vqt_fill_v);
    codeptr_register_shared(F_vqt_fill_hv, (CodeFn)vqt_fill_hv);
    codeptr_register_shared(F_gfx_put_pixel, (CodeFn)gfx_put_pixel);
    codeptr_register_shared(F_gfx_rect, (CodeFn)gfx_rect);
    /* scn_draw_fn (initialised data: a far pointer to gfx_sprite_scn), called by gfx_sprite_scn_ind
     * (added in platform's integration round: AP's SCN frames reached it unregistered) */
    codeptr_register_shared(F_gfx_sprite_scn, (CodeFn)gfx_sprite_scn);
    codeptr_register_shared(F_gfx_sprite_rotated, (CodeFn)gfx_sprite_rotated);   /* rot_fn hook (demo films) */
}
