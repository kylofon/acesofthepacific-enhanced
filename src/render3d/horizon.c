/* render3d — horizon.c: sky/sea fill along the projected horizon (AP 4d6e), the span-list helpers
 * (0a88, hand-written asm) and the DDA helpers (1abf). render3d_sky.md §2.
 *
 * DDA (8 bytes, DS): +0 u16 frac, +2 i16 val, +4 i32 step (val:frac is 16.16). Span list: {u16 y0;
 * u16 n; n × {i16 xl; i16 xr}} (the gfx_poly_spans format). */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"
#include "codeptr.h"
#include "video/video.h"

enum {
    H_SPACING = 0x031A, H_BANDS = 0x031C, H_STEP = 0x031E, H_ON = 0x0320, H_BUF = 0x0322,
    H_CB_PENDING = 0x3FFA, H_CB = 0x3FFC,
    H_Y1 = 0x4000, H_X1 = 0x4002, H_Y0 = 0x4004, H_X0 = 0x4006, H_NX = 0x4008, H_NY = 0x400A,
    H_SEA = 0x400C, H_SKY = 0x400E,
    H_SPAN_SEG = 0x3172,                       /* video: segment of the span scratch buffer */
    H_PAGE = 0x26D2, H_DRAW_PAGE = 0x26D8, H_FILL_ON = 0x26CC, H_DRAW_COL = 0x26CD, H_FILL_COL = 0x26CE,
};
#define CX0 ((s16)DSW(0x26C4))
#define CX1 ((s16)DSW(0x26C6))
#define CY0 ((s16)DSW(0x26C8))
#define CY1 ((s16)DSW(0x26CA))

static s16 abs16(s16 v) { return (s16)(v < 0 ? (u16)-v : (u16)v); }   /* cwd/xor/sub */
static void window_rect(void) { gfx_rect(CX0, CY0, (s16)(CX1 - CX0 + 1), (s16)(CY1 - CY0 + 1)); }
static void call_cb(u32 cb) { if (cb) codeptr_lookup(fp_u32(cb))(); }

/* ================================================================================================
 * 0a88 — span-list helpers (L far, n = L[+2] entries) */

static void span_each(FarPtr L, u16 field, s16 v, int add)
{
    u16 n = rd16(L.seg, (u16)(L.off + 2));
    for (u16 o = (u16)(L.off + field); n; n--, o = (u16)(o + 4))
        wr16(L.seg, o, add ? (u16)(rd16(L.seg, o) + (u16)v) : (u16)v);
}
/* AP 0a88:000a / 000f — xl = v / xr = v */
void span_left_const(FarPtr L, s16 v) { span_each(L, 4, v, 0); }
void span_right_const(FarPtr L, s16 v) { span_each(L, 6, v, 0); }
/* AP 0a88:0030 / 0035 — xl += v / xr += v */
void span_left_step(FarPtr L, s16 v) { span_each(L, 4, v, 1); }
void span_right_step(FarPtr L, s16 v) { span_each(L, 6, v, 1); }
/* AP 0a88:0059 — xr = xl */
void span_left_edge(FarPtr L)
{
    u16 n = rd16(L.seg, (u16)(L.off + 2));
    for (u16 o = (u16)(L.off + 4); n; n--, o = (u16)(o + 4)) wr16(L.seg, (u16)(o + 2), rd16(L.seg, o));
}
/* AP 0a88:007c — xl = xr */
void span_right_edge(FarPtr L)
{
    u16 n = rd16(L.seg, (u16)(L.off + 2));
    for (u16 o = (u16)(L.off + 4); n; n--, o = (u16)(o + 4)) wr16(L.seg, o, rd16(L.seg, (u16)(o + 2)));
}
static void span_from_dda(FarPtr L, u16 field, u16 d)
{
    u16 n = rd16(L.seg, (u16)(L.off + 2));
    for (u16 o = (u16)(L.off + field); n; n--, o = (u16)(o + 4)) {
        wr16(L.seg, o, DSW(d + 2));
        DSL(d) = DSL(d) + DSL(d + 4);                      /* add / adc on val:frac */
    }
}
/* AP 0a88:009a / 009f — xl / xr from the DDA (near), stepping it */
void span_left_dda(FarPtr L, u16 d) { span_from_dda(L, 4, d); }
void span_right_dda(FarPtr L, u16 d) { span_from_dda(L, 6, d); }

/* AP 0a88:00cf — n rows from y, x from two i16 arrays in segment seg, clipped to the window rows */
void span_fill(s16 y, u16 seg, u16 left, u16 right, s16 n, FarPtr D)
{
    s16 over = (s16)(y + n - 1 - CY1);
    if (over > 0) n = (s16)(n - over);
    s16 skip = 0;
    if (y < CY0) { skip = (s16)(CY0 - y); y = CY0; }
    wr16(D.seg, D.off, (u16)y);                            /* written even if nothing follows */
    n = (s16)(n - skip);
    if (n <= 0) return;
    wr16(D.seg, (u16)(D.off + 2), (u16)n);
    left = (u16)(left + 2 * skip); right = (u16)(right + 2 * skip);
    u16 o = (u16)(D.off + 4);
    for (s16 k = 0; k < n; k++, left += 2, right += 2, o = (u16)(o + 4)) {
        wr16(D.seg, o, rd16(seg, left));
        wr16(D.seg, (u16)(o + 2), rd16(seg, right));
    }
    gfx_poly_spans(D);
}

/* AP 0a88:0145 — x-clip the list to the window into D and draw it (8-bit header words) */
void span_flush(FarPtr L, FarPtr D)
{
    u16 n = rd16(L.seg, (u16)(L.off + 2));
    if (!n) return;
    s16 x0 = CX0, x1 = CX1;
    u8 dl = (u8)rd16(L.seg, L.off), dh = 0;
    u16 si = (u16)(L.off + 4), di = (u16)(D.off + 4);
    for (; n; n--, si = (u16)(si + 4)) {
        s16 xl = (s16)rd16(L.seg, si), xr = (s16)rd16(L.seg, (u16)(si + 2));
        if (xl > x1 || xr < x0) {
            if (dh) break;                                 /* the first outside row ends the list */
            dl++;
            continue;
        }
        wr16(D.seg, di, (u16)(xl < x0 ? x0 : xl));
        wr16(D.seg, (u16)(di + 2), (u16)(xr > x1 ? x1 : xr));
        di = (u16)(di + 4);
        dh++;
    }
    if (!dh) return;
    wr16(D.seg, D.off, dl);
    wr16(D.seg, (u16)(D.off + 2), dh);
    gfx_poly_spans(D);
}

/* ================================================================================================
 * 1abf — DDA */

/* AP 1abf:0008 — a to b in n steps (start fraction quirk kept) */
void dda_setup(u16 d, s16 a, s16 b, s16 n)
{
    DSW(d + 2) = (u16)a;
    DSW(d + 6) = (u16)b;
    if (n <= 0) { DSW(d + 6) = 0; DSW(d + 4) = 0; DSW(d) = 0; return; }
    DSW(d) = 0;
    DSW(d + 4) = 0;
    s32 step = (s32)(DSL(d + 4) - DSL(d));                 /* (b << 16) − (a << 16), sub/sbb */
    step = step / (s32)n;                                  /* Borland LDIV (0000:1d69), truncation */
    u8 neg = step < 0;
    if (neg) step = (s32)(0u - (u32)step);
    DSW(d) = ((u32)step & 0xFFFF8000u) == 0 ? 0x8000 : (u16)step;
    if (neg) step = (s32)(0u - (u32)step);
    DSSL(d + 4) = step;
}
/* AP 1abf:00c6 — current value, then step */
s16 dda_step(u16 d)
{
    s16 v = (s16)DSW(d + 2);
    DSL(d) = DSL(d) + DSL(d + 4);
    return v;
}
/* AP 1abf:00de — n words of a DDA from a to b */
void dda_fill_edge(FarPtr dst, s16 a, s16 b, u16 n)
{
    if (!n) return;
    u16 mk = stk_mark(), t = stk_alloc(8);
    dda_setup(t, a, b, (s16)n);
    for (; n; n--, dst = far_add(dst, 2)) {
        wr16(dst.seg, dst.off, DSW(t + 2));
        DSL(t) = DSL(t) + DSL(t + 4);
    }
    stk_release(mk);
}
/* AP 1abf:011d — rep stosw */
void dda_const_edge(FarPtr dst, s16 v, u16 n)
{
    for (; n; n--, dst = far_add(dst, 2)) wr16(dst.seg, dst.off, (u16)v);
}

/* ================================================================================================
 * 4d6e — horizon */

/* AP 4d6e:0000 — detail slider: haze bands */
void sky_set_detail(u16 v)
{
    if ((s16)v < 0x55) { DSW(H_BANDS) = 0; return; }
    if ((s16)v < 0xAA) { DSW(H_STEP) = 2; DSW(H_SPACING) = 4; DSW(H_BANDS) = 7; return; }
    DSW(H_STEP) = 1; DSW(H_SPACING) = 2; DSW(H_BANDS) = 0x0F;
}
/* AP 4d6e:0040 */
void sky_invalidate(void) { DSW(H_CB_PENDING) = 1; }
/* AP 4d6e:004b — the 780h-byte edge / span buffer; 1 = allocated */
u16 sky_alloc(void)
{
    ds_far_wr(H_BUF, mem_far_new(0x780, 0));
    return !far_is_null(ds_far(H_BUF));
}

/* AP 4d6e:015c — the horizon segment for pitch p, roll r: −1 all sea, 1 all sky, 0 crosses */
s16 horizon_line(u16 p, u16 r)
{
    u8 inverted = (s16)p > 0x4000 || (s16)p < -0x4000;
    s16 sp = sin16(p), cp = (s16)cos16(p);
    if (sp >= 0x3A98) return -1;
    if (sp <= -0x3A98) return 1;
    s16 sr = sin16(r), cr = (s16)cos16(r);
    DSW(H_X1) = (u16)(cr >> 6); DSW(H_X0) = (u16)-(cr >> 6);
    DSW(H_Y0) = (u16)(sr >> 6); DSW(H_Y1) = (u16)-(sr >> 6);
    s16 d = ldiv_sat((s32)sp << (CSB(CS_PROJ_SHIFT) & 31), cp);
    s16 bx = (s16)(((s32)d * (s16)DSW(H_Y0)) >> 8);
    s16 dx = (s16)(((s32)d * (s16)DSW(H_X1)) >> 8);
    DSW(H_X0) = (u16)(DSW(H_X0) + bx); DSW(H_X1) = (u16)(DSW(H_X1) + bx);
    DSW(H_Y0) = (u16)(DSW(H_Y0) + dx); DSW(H_Y1) = (u16)(DSW(H_Y1) + dx);
    DSW(H_X0) = (u16)(DSW(H_X0) + CSW(CS_CX)); DSW(H_X1) = (u16)(DSW(H_X1) + CSW(CS_CX));
    DSW(H_Y0) = (u16)(CSW(CS_CY) - DSW(H_Y0)); DSW(H_Y1) = (u16)(CSW(CS_CY) - DSW(H_Y1));
    if (inverted) { DSW(H_NY) = (u16)-sr; DSW(H_NX) = (u16)cr; }
    else { DSW(H_NY) = (u16)sr; DSW(H_NX) = (u16)-cr; }
    return 0;
}

/* AP 4d6e:029e — one side of the line whose rows run ya..yb (flag: the side above) */
void horizon_fill_rest(s16 flag, s16 ya, s16 yb, FarPtr A, FarPtr B, FarPtr C, FarPtr E, FarPtr D)
{
    if (flag) {
        if (yb < CY0) return;
        if (ya > CY1) { window_rect(); return; }
        if (ya >= CY0) gfx_rect(CX0, CY0, (s16)(CX1 - CX0 + 1), (s16)(ya - CY0 + 1));
        s16 y = ya > CY0 ? ya : CY0;
        s16 n = (s16)(yb - y);
        span_fill(y, C.seg, (u16)(C.off - 2 * n), (u16)(E.off - 2 * n), n, D);
    } else {
        if (ya > CY1) return;
        if (yb < CY0) { window_rect(); return; }
        if (yb <= CY1) gfx_rect(CX0, yb, (s16)(CX1 - CX0 + 1), (s16)(CY1 - yb + 1));
        span_fill(ya, A.seg, A.off, B.off, (s16)(yb - ya), D);
    }
}

/* the band DDA of both cases: offsets k·spacing / n (2.14) over the bands */
static void band_dda(u16 t, s16 n)
{
    s32 sp = (s32)(s16)DSW(H_SPACING);
    s16 b = (s16)idiv32_386(((s32)(s16)(DSW(H_BANDS) + 1) * sp) << 14, n, NULL);
    s16 a = (s16)idiv32_386(sp << 14, n, NULL);
    dda_setup(t, a, b, (s16)DSW(H_BANDS));
}
static u8 band_colour(s16 i)
{
    return (u8)(DSB(H_SKY) + (u8)(i * (s16)DSW(H_STEP)) + (u8)((s16)DSW(H_STEP) >> 1));
}

/* AP 4d6e:03be — horizon closer to horizontal: x-edge array over the crossed rows */
void horizon_fill_rows(void)
{
    FarPtr D = far_make(DSW(H_SPAN_SEG), 0);
    FarPtr L = ds_far(H_BUF);
    s32 dyl = (s32)(s16)(DSW(H_Y1) - DSW(H_Y0));
    s32 dx0 = (s32)(s16)(DSW(H_X1) - DSW(H_X0));
    s16 yl = (s16)(DSW(H_Y0) + (u16)idiv32_386((int64_t)(s16)(CX0 - (s16)DSW(H_X0)) * dyl, dx0, NULL));
    s16 yr = (s16)(DSW(H_Y0) + (u16)idiv32_386((int64_t)(s16)(CX1 - (s16)DSW(H_X0)) * dyl, dx0, NULL));
    s16 dy = (s16)(yr - yl);
    s16 m = abs16(dy) < 16 ? 16 : abs16(dy);
    s16 c = (s16)(m + abs16(dy));
    s16 si, di;
    if (dy > 0) { si = yl; di = yr; } else { si = yr; di = yl; }
    FarPtr A, B, C, E;
    if (dy > 0) {
        A = L; C = far_add(A, (u16)(2 * c)); B = far_add(L, (u16)(2 * m)); E = far_add(B, (u16)(2 * c));
        dda_const_edge(A, CX0, (u16)m);
        dda_fill_edge(far_add(C, (u16)(-2 * dy)), CX0, CX1, (u16)dy);
        dda_const_edge(far_add(B, (u16)(2 * dy)), CX1, (u16)m);
    } else {
        B = L; E = far_add(B, (u16)(2 * c)); A = far_add(L, (u16)(2 * m)); C = far_add(A, (u16)(2 * c));
        dda_const_edge(B, CX1, (u16)m);
        dda_fill_edge(A, CX1, CX0, (u16)-dy);
        dda_const_edge(far_add(A, (u16)(-2 * dy)), CX0, (u16)m);
    }
    s16 y0 = si, y1 = di;
    if (DSW(H_ON)) {
        u16 mk = stk_mark(), t = stk_alloc(8);
        band_dda(t, (s16)DSW(H_NX));
        s16 prev = 0;
        for (s16 i = 0; i < (s16)DSW(H_BANDS); i++) {
            s16 cur = dda_step(t);
            s16 dl = (s16)(cur - prev); prev = cur;
            s16 n = (s16)(abs16(dl) + abs16(dy));
            if ((s16)DSW(H_NX) < 0) { si = (s16)(si + dl); di = (s16)(di + dl); }
            DSB(H_DRAW_COL) = band_colour(i);
            if (dy > 0) span_fill(si, L.seg, (u16)(C.off - 2 * n), B.off, n, D);
            else span_fill(si, L.seg, A.off, (u16)(E.off - 2 * n), n, D);
            if ((s16)DSW(H_NX) > 0) { si = (s16)(si + dl); di = (s16)(di + dl); }
        }
        stk_release(mk);
    }
    DSB(H_FILL_COL) = DSB(H_DRAW_COL) = (u8)(DSB(H_SKY) + 0x0F);
    horizon_fill_rest((s16)DSW(H_NX) < 0, si, di, A, B, C, E, D);      /* the rest of the sky */
    call_cb(DSL(H_CB));                                                  /* sun, moon, stars */
    DSB(H_FILL_COL) = DSB(H_DRAW_COL) = DSB(H_SEA);
    horizon_fill_rest((s16)DSW(H_NX) > 0, y0, y1, A, B, C, E, D);       /* the sea */
}

/* AP 4d6e:0724 — horizon closer to vertical: a span list over every row, one edge moved at a time */
void horizon_fill_cols(void)
{
    s16 acc = 0;
    FarPtr D = far_make(DSW(H_SPAN_SEG), 0);
    FarPtr L = ds_far(H_BUF);
    u16 mk = stk_mark(), t = stk_alloc(8);
    wr16(L.seg, L.off, (u16)CY0);
    s16 H = (s16)(CY1 - CY0 + 1);
    wr16(L.seg, (u16)(L.off + 2), (u16)H);
    s16 xa = lerp_map(CY0, (s16)DSW(H_Y0), (s16)DSW(H_Y1), (s16)DSW(H_X0), (s16)DSW(H_X1));
    s16 xb = lerp_map(CY1, (s16)DSW(H_Y0), (s16)DSW(H_Y1), (s16)DSW(H_X0), (s16)DSW(H_X1));
    s16 d = (s16)(xb - xa);
    if (d <= 0) xa = xb;
    if (d > 0) dda_setup(t, 0, d, H);
    else dda_setup(t, (s16)-d, 0, H);
    if ((s16)DSW(H_NY) < 0) { span_left_dda(L, t); span_left_step(L, xa); }
    else { span_right_dda(L, t); span_right_step(L, xa); }
    if (DSW(H_ON)) {
        band_dda(t, (s16)DSW(H_NY));
        s16 prev = 0;
        for (s16 i = 0; i < (s16)DSW(H_BANDS); i++) {
            DSB(H_DRAW_COL) = band_colour(i);
            s16 cur = dda_step(t);
            s16 dl = (s16)(cur - prev); prev = cur;
            acc = (s16)(acc - dl);
            if ((s16)DSW(H_NY) < 0) { span_left_edge(L); span_left_step(L, dl); }
            else { span_right_edge(L); span_right_step(L, dl); }
            span_flush(L, D);
        }
    }
    DSB(H_DRAW_COL) = (u8)(DSB(H_SKY) + 0x0F);
    if ((s16)DSW(H_NY) < 0) { span_left_edge(L); span_left_const(L, CX0); }
    else { span_right_edge(L); span_right_const(L, CX1); }
    span_flush(L, D);                                                    /* the rest of the sky */
    call_cb(DSL(H_CB));
    DSB(H_DRAW_COL) = DSB(H_SEA);
    if ((s16)DSW(H_NY) < 0) { span_right_edge(L); span_left_step(L, acc); span_right_const(L, CX1); }
    else { span_left_edge(L); span_right_step(L, acc); span_left_const(L, CX0); }
    span_flush(L, D);                                                    /* the sea */
    stk_release(mk);
}

/* AP 4d6e:0076 — sky above / sea below the horizon, the callback (sun, moon, stars) in between.
 * a0 = sky base colour, a1 = sea colour (the dword DS:5BDB), a2 = pitch, a3 = roll, a4 = callback. */
void draw_sky(u16 a0, u16 a1, u16 a2, u16 a3, u32 a4)
{
    DSW(H_SKY) = a0;
    DSW(H_SEA) = a1;
    DSL(H_CB) = a4;
    DSW(H_DRAW_PAGE) = DSW(H_PAGE);
    s16 r = horizon_line((u16)-a2, (u16)-a3);
    if (r == 1) { DSW(H_SEA) = (u16)(a0 + 0x0F); DSW(H_CB_PENDING) = 1; }
    if (r == -1 && DSW(H_CB_PENDING)) { call_cb(a4); DSW(H_CB_PENDING) = 0; }
    if (r == -1 || r == 1) {
        DSB(H_FILL_COL) = DSB(H_DRAW_COL) = DSB(H_SEA);
        DSB(H_FILL_ON) = 1;
        window_rect();
        if (r == 1) { call_cb(a4); DSW(H_CB_PENDING) = 1; }
        return;
    }
    if (r != 0) return;
    if (abs16((s16)DSW(H_NX)) > abs16((s16)DSW(H_NY))) horizon_fill_rows();
    else horizon_fill_cols();
    DSW(H_CB_PENDING) = 1;
}
