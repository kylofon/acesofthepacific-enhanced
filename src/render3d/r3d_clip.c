/* render3d — 3Space core: per-object set-up, near-plane and window clipping (Sutherland–Hodgman, three
 * arithmetic variants), the 32-bit part routine, the projection helpers (AP 277a, PS 3d50).
 * render3d_math.md §4.7/§4.8, render3d_poly.md §2.4/§3.4/§4, video_ts.md §3.2/§3.3.
 *
 * PS differences kept (video_ts.md §3): pt_project / apparent_size and the "b" clippers go through
 * proj32; near_isect32a uses the focal length and no shift. The data block cs_state_block
 * (277a:0a13 / 3d50:3e53) is the CS state accessed through CS_* (r3d_priv.h). */
#include "render3d/r3d_priv.h"

/* ================================================================================================
 * Per-object set-up (render3d_math.md §4.7)
 * ============================================================================================== */

/* PS 3d50:24ea / AP 277a:22f8 — angles to CS:21B6, d = pos − cam, e = d·Mcam, e_depth */
void obj_set_position(u16 o)
{
    u8 *K = cs_blk();
    for (int i = 0; i < 6; i += 2) KW(CS_SCR_VEC + i) = DSW(o + 0x0E + i);
    for (int i = 0; i < 12; i += 4) KL(CS_D_WORLD + i) = DSL(o + 2 + i) - KL(CS_CAM_POS + i);
    lvec_rotate_n(K + CS_D_WORLD, K + CS_M_CAM, K + CS_E_POS);
    s32 ey = KSL(CS_E_POS + 4);
    u16 nz = CSW(CS_NEAR_Z);
    if ((ey >> 16) > 0 || ((ey >> 16) == 0 && (u16)ey >= nz)) KL(CS_E_DEPTH) = (u32)ey;
    else KL(CS_E_DEPTH) = nz;
}

static u32 cs_abs32(s32 v) { return v < 0 ? (u32)-(u32)v : (u32)v; }

/* PS 3d50:256f / AP 277a:237d */
void obj_set_orientation(void)
{
    u8 *K = cs_blk();
    r3d_mat_from_angles(K + CS_SCR_VEC, K + CS_M_OBJ);
    r3d_mat_mul(K + CS_M_OBJ, K + CS_M_CAM, K + CS_M_OBJ_EYE);
    r3d_mat_transpose(K + CS_M_OBJ);
    u32 m = cs_abs32(KSL(CS_E_POS));
    u32 t = cs_abs32(KSL(CS_E_POS + 4));
    if ((s32)m < (s32)t) m = t;                          /* hi signed, lo unsigned = signed 32 */
    t = cs_abs32(KSL(CS_E_POS + 8));
    if ((s32)m < (s32)t) m = t;
    KL(CS_E_DIST) = m;
    s32 v = (s32)(m + KL(CS_RADIUS));
    for (u16 c = KW(CS_OBJ_SHIFT); c; c--) v >>= 1;      /* sar/rcr loop */
    u32 u = (u32)v;
    u16 k = KW(CS_OBJ_SHIFT);
    while ((u >> 16) != 0 || (u16)u >= 0x3FAC) { u >>= 1; k++; }
    KW(CS_OBJ_DOWN) = k;
    KW(CS_VTX_SHIFT) = (u16)(KW(CS_OBJ_SHIFT) - k);
    s32 d = KSL(CS_E_DEPTH);
    for (u16 c = k; c; c--) d >>= 1;
    KW(CS_E_DEPTH16) = (u16)d;
    for (int i = 0; i < 3; i++) {
        s32 e = KSL(CS_E_POS + 4 * i);
        for (u16 c = k; c; c--) e >>= 1;
        KW(CS_E_POS16 + 2 * i) = (u16)e;
    }
    for (int i = 0; i < 3; i++) {
        s32 e = KSL(CS_D_WORLD + 4 * i);
        for (u16 c = k; c; c--) e >>= 1;
        KW(CS_SCR_VEC + 2 * i) = (u16)e;
    }
    vec_rotate_n(K + CS_SCR_VEC, K + CS_M_OBJ, K + CS_EYE_IN_OBJ);
    vec_rotate_n(csp(CS_LIGHT_WORLD), K + CS_M_OBJ, K + CS_LIGHT_OBJ);
}

/* PS 3d50:26e1 / AP 277a:24ef — nearest depth of the object's box */
void obj_near_depth(void)
{
    u8 *K = cs_blk();
    u16 es = KW(CS_SHAPE_PTR + 2), si = KW(CS_SHAPE_PTR);
    if (rd8(es, si) & 0x20) {
        KL(CS_E_NEAR) = KL(CS_E_POS + 4) - KL(CS_RADIUS);
        return;
    }
    u32 acc = 0;
    static const u8 col[3] = { 2, 8, 0x0E };                 /* m_obj_eye column 1 */
    for (int a = 0; a < 3; a++) {
        s16 c = KS(CS_M_OBJ_EYE + col[a]);
        s16 bp = 0;
        if (c) {
            s16 lo = fx14((s32)(s16)rd16(es, (u16)(si + 0x0E + 2 * a)) * c);
            s16 hi = fx14((s32)(s16)rd16(es, (u16)(si + 0x14 + 2 * a)) * c);
            bp = lo <= hi ? lo : hi;
        }
        acc += (u32)(s32)bp;
    }
    for (u16 c = KW(CS_OBJ_SHIFT); c; c--) acc <<= 1;
    KL(CS_E_NEAR) = acc + KL(CS_E_POS + 4);
}

/* ================================================================================================
 * Projection helpers (render3d_math.md §4.8, video_ts.md §3.2)
 * ============================================================================================== */

/* PS 3d50:2d86 / AP 277a:2fee — p[0] += cx; p[1] = cy − p[1] (i32) */
u16 pt_add_centre(u16 p)
{
    u8 *K = cs_blk();
    DSL(p) += (u32)(s32)KS(CS_CX);
    DSL(p + 4) = (u32)(s32)KS(CS_CY) - DSL(p + 4);
    return p;
}

/* PS 3d50:2daa / AP 277a:3010 — out = focal·(in.x, in.z) / in.y */
u16 pt_project(u16 in, u16 out)
{
    u8 *K = cs_blk();
    if (r3d_is_ps()) {
        s32 x = DSSL(in), z = DSSL(in + 8);
        r3d_proj32(&x, &z, DSSL(in + 4), (s32)KW(CS_FOCAL));
        DSSL(out) = x; DSSL(out + 4) = z;
        return out;
    }
    s32 y = DSSL(in + 4);
    s64 f = KW(CS_FOCAL);
    DSSL(out) = idiv32_386(f * DSSL(in), y, NULL);
    DSSL(out + 4) = idiv32_386(f * DSSL(in + 8), y, NULL);
    return out;
}

/* PS 3d50:2de4 / AP 277a:304c — focal·v[0] / v[1] */
u32 apparent_size(u16 v)
{
    u8 *K = cs_blk();
    if (r3d_is_ps()) {
        s32 x = DSSL(v), z = 0;
        r3d_proj32(&x, &z, DSSL(v + 4), (s32)KW(CS_FOCAL));
        return (u32)x;
    }
    return (u32)idiv32_386((s64)KW(CS_FOCAL) * DSSL(v), DSSL(v + 4), NULL);
}

/* ================================================================================================
 * Clipping (render3d_poly.md §4). Lists: x CS:0C77 (+hi 170D), y CS:0C9F (+hi 1735), temporaries
 * CS:0CC7 (+hi 175D) / CS:0CEF (+hi 1785); indices are byte offsets (i·2) as in the original.
 * ============================================================================================== */

#define L_X  CS_PX
#define L_Y  CS_PY
#define T_X  CS_TX
#define T_Y  CS_TY

/* 16-bit interpolation t·(I.b − O.b)/(I.a − O.a) + O.b, IMUL + IDIV (overflow: halving retry) */
static s16 lerp16(s16 t, s16 db, s16 da, s16 ob)
{
    return (s16)(idiv16_retry((s32)t * db, da, NULL) + ob);
}

/* PS 3d50:2e0c / AP 277a:25ec — window clip of the 16-bit lists: x pass into T, y pass back */
void clip2d16(void)
{
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    if ((s16)n < 2) return;
    s16 x0 = DRV_X1, x1 = DRV_X2, y0 = DRV_Y1, y1 = DRV_Y2;
    /* x pass */
    u16 di = 0, bx = (u16)((n - 1) * 2);
    u8 cl = 0;
    { s16 x = KS(L_X + bx); if (x < x0) cl |= 1; if (x > x1) cl |= 2; }
    for (u16 si = 0; si < n * 2; si += 2) {
        u8 ch = 0;
        s16 x = KS(L_X + si);
        if (x < x0) ch |= 1;
        if (x > x1) ch |= 2;
        if (!(cl | ch)) {
            KW(T_X + di) = KW(L_X + si); KW(T_Y + di) = KW(L_Y + si); di += 2;
        } else if (!(cl & ch)) {
            /* intersection anchored at the outside vertex O (code c) towards I */
#define XEDGE(O, I, c) do { s16 e_; if ((c) & 1) e_ = x0; else if ((c) & 2) e_ = x1; else break;              \
                KS(T_X + di) = e_;                                                                                  \
                KS(T_Y + di) = lerp16((s16)(e_ - KS(L_X + (O))), (s16)(KS(L_Y + (I)) - KS(L_Y + (O))),              \
                                      (s16)(KS(L_X + (I)) - KS(L_X + (O))), KS(L_Y + (O)));                        \
                di += 2; } while (0)
            if (!cl) XEDGE(si, bx, ch);
            else if (!ch) { XEDGE(bx, si, cl); KW(T_X + di) = KW(L_X + si); KW(T_Y + di) = KW(L_Y + si); di += 2; }
            else { XEDGE(bx, si, cl); XEDGE(si, bx, ch); }
#undef XEDGE
        }
        bx = si; cl = ch;
    }
    n = (u16)(di >> 1);
    KW(CS_POLY_N) = n;
    if ((s16)n < 2) {
        for (u16 i = 0; i < n; i++) { KW(L_X + 2 * i) = KW(T_X + 2 * i); KW(L_Y + 2 * i) = KW(T_Y + 2 * i); }
        return;
    }
    /* y pass */
    di = 0; bx = (u16)((n - 1) * 2); cl = 0;
    { s16 y = KS(T_Y + bx); if (y > y1) cl |= 4; if (y < y0) cl |= 8; }
    for (u16 si = 0; si < n * 2; si += 2) {
        u8 ch = 0;
        s16 y = KS(T_Y + si);
        if (y > y1) ch |= 4;
        if (y < y0) ch |= 8;
        if (!(cl | ch)) {
            KW(L_X + di) = KW(T_X + si); KW(L_Y + di) = KW(T_Y + si); di += 2;
        } else if (!(cl & ch)) {
#define YEDGE(O, I, c) do { s16 e_; if ((c) & 8) e_ = y0; else if ((c) & 4) e_ = y1; else break;              \
                KS(L_Y + di) = e_;                                                                                  \
                KS(L_X + di) = lerp16((s16)(e_ - KS(T_Y + (O))), (s16)(KS(T_X + (I)) - KS(T_X + (O))),              \
                                      (s16)(KS(T_Y + (I)) - KS(T_Y + (O))), KS(T_X + (O)));                        \
                di += 2; } while (0)
            if (!cl) YEDGE(si, bx, ch);
            else if (!ch) { YEDGE(bx, si, cl); KW(L_X + di) = KW(T_X + si); KW(L_Y + di) = KW(T_Y + si); di += 2; }
            else { YEDGE(bx, si, cl); YEDGE(si, bx, ch); }
#undef YEDGE
        }
        bx = si; cl = ch;
    }
    KW(CS_POLY_N) = (u16)(di >> 1);
}

/* PS 3d50:32ad / AP 277a:2a8d near_isect16 — intersection of the edge (behind b, front a) with the near plane,
 * projected by << near_shift, stored into b's vx/vy slot */
static void near_isect16(u8 *K, u16 b, u16 a)
{
    s16 bx = (s16)(KS(CS_EY + a) - KS(CS_EY + b));
    s16 cx = (s16)(CSW(CS_NEAR_Z) - KS(CS_EY + b));
    KS(CS_VX + b) = (s16)(idiv16_retry((s32)(s16)(KS(CS_EX + a) - KS(CS_EX + b)) * cx, bx, NULL) + KS(CS_EX + b));
    KS(CS_VY + b) = (s16)(idiv16_retry((s32)(s16)(KS(CS_EZ + a) - KS(CS_EZ + b)) * cx, bx, NULL) + KS(CS_EZ + b));
    u16 sh = CSW(CS_NEAR_SHIFT);
    if (sh) { KW(CS_VX + b) = (u16)(KW(CS_VX + b) << (sh & 31)); KW(CS_VY + b) = (u16)(KW(CS_VY + b) << (sh & 31)); }
}

/* The S-H walk over the vertex states shared by the three near clippers (2adf / 2f4e / 3530):
 * cur behind & prev front → X(cur); cur front & prev behind → X(prev), cur; both front → cur. */
typedef void (*NearIsect)(u8 *K, u16 b, u16 a);
static void near_walk(u8 *K, NearIsect isect, bool wide)
{
    u16 n2 = (u16)(KW(CS_POLY_N) * 2);
    u16 bp = KW(CS_POLY_IDX + n2 - 2), di = 0;
#define EMIT(v) do { KW(CS_PX + di) = KW(CS_VX + (v)); KW(CS_PY + di) = KW(CS_VY + (v));           \
                     if (wide) { KW(CS_PXH + di) = KW(CS_VXH + (v)); KW(CS_PYH + di) = KW(CS_VYH + (v)); } \
                     di += 2; } while (0)
    for (u16 cx = 0; cx != n2; cx += 2) {
        u16 si = KW(CS_POLY_IDX + cx);
        u8 al = KB(CS_VTX_STATE + bp), ah = KB(CS_VTX_STATE + si);
        if (ah == 3) {
            if (al != 3) { isect(K, si, bp); EMIT(si); }
        } else {
            if (al == 3) { isect(K, bp, si); EMIT(bp); }
            EMIT(si);
        }
        bp = si;
    }
#undef EMIT
    KW(CS_POLY_N) = (u16)(di >> 1);
}

/* PS 3d50:32f6 / AP 277a:2adf */
void near_clip16(void) { u8 *K = cs_blk(); near_walk(K, near_isect16, false); }

/* PS 3d50:3399 / AP 277a:2b5d */
void poly_add_centre16(void)
{
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    for (u16 i = 0; i < n; i++) {
        KW(CS_PX + 2 * i) = (u16)(KW(CS_PX + 2 * i) + KW(CS_CX));
        KW(CS_PY + 2 * i) = (u16)(KW(CS_CY) - KW(CS_PY + 2 * i));
    }
}

/* ---- 32-bit lists -------------------------------------------------------------------------------- */
static s32 get32(u8 *K, u16 lo, u16 hi, u16 i) { return (s32)((u32)KW(lo + i) | (u32)KW(hi + i) << 16); }
static void put32(u8 *K, u16 lo, u16 hi, u16 i, s32 v) { KW(lo + i) = (u16)v; KW(hi + i) = (u16)((u32)v >> 16); }

/* outcodes of clip2d32a/b: 32-bit compares, high word signed, low word unsigned against the u16 edge */
static bool lt_edge(s32 v, s16 e) { s16 h = (s16)(v >> 16); return h < 0 || (h == 0 && (u16)v < (u16)e); }
static bool gt_edge(s32 v, s16 e) { s16 h = (s16)(v >> 16); return h > 0 || (h == 0 && (u16)v > (u16)e); }

/* x edge of 2caa (2b88) / 3242 (3070); O = bp (outside, code cl), I = si. Output into the T lists. */
static void edge32(u8 *K, u16 O, u16 I, u8 cl, u16 *di, bool xpass, int variant)
{
    s16 e;
    u16 Ain = xpass ? L_X : T_Y, AinH = xpass ? CS_PXH : CS_TYH;     /* clipped coordinate lists */
    u16 Bin = xpass ? L_Y : T_X, BinH = xpass ? CS_PYH : CS_TXH;
    u16 Aout = xpass ? T_X : L_Y, AoutH = xpass ? CS_TXH : CS_PYH;
    u16 Bout = xpass ? T_Y : L_X, BoutH = xpass ? CS_TYH : CS_PXH;
    if (xpass) { if (cl & 1) e = DRV_X1; else if (cl & 2) e = DRV_X2; else return; }
    else       { if (cl & 8) e = DRV_Y1; else if (cl & 4) e = DRV_Y2; else return; }
    put32(K, Aout, AoutH, *di, (s32)e);
    s32 res;
    if (variant == 0) {                                  /* "a": 16-bit on the low words, cdq, adc */
        s16 t = (s16)(e - KS(Ain + O)), da = (s16)(KS(Ain + I) - KS(Ain + O)), db = (s16)(KS(Bin + I) - KS(Bin + O));
        s16 q = idiv16_retry((s32)t * db, da, NULL);
        res = (s32)((u32)(s32)q + (u32)get32(K, Bin, BinH, O));
    } else if (!r3d_is_ps()) {                           /* AP "b": imul edx / idiv ebx */
        s32 oa = get32(K, Ain, AinH, O), ob = get32(K, Bin, BinH, O);
        s32 t = (s32)((u32)(s32)e - (u32)oa), da = (s32)((u32)get32(K, Ain, AinH, I) - (u32)oa);
        s32 db = (s32)((u32)get32(K, Bin, BinH, I) - (u32)ob);
        res = (s32)((u32)idiv32_386((s64)db * t, da, NULL) + (u32)ob);
    } else {                                             /* PS "b": proj32(I.b−O.b, 0, I.a−O.a, e−O.a) */
        s32 oa = get32(K, Ain, AinH, O), ob = get32(K, Bin, BinH, O);
        s32 x = (s32)((u32)get32(K, Bin, BinH, I) - (u32)ob), z = 0;
        r3d_proj32(&x, &z, (s32)((u32)get32(K, Ain, AinH, I) - (u32)oa), (s32)((u32)(s32)e - (u32)oa));
        res = (s32)((u32)x + (u32)ob);
    }
    put32(K, Bout, BoutH, *di, res);
    *di += 2;
}

static void clip2d32(int variant)
{
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    if ((s16)n < 2) return;
    s16 x0 = DRV_X1, x1 = DRV_X2, y0 = DRV_Y1, y1 = DRV_Y2;
    u16 di = 0, bp = (u16)((n - 1) * 2);
    u8 cl = 0;
    { s32 x = get32(K, L_X, CS_PXH, bp); if (lt_edge(x, x0)) cl |= 1; if (gt_edge(x, x1)) cl |= 2; }
    for (u16 si = 0; (u16)(si >> 1) != n; si += 2) {
        u8 ch = 0;
        s32 x = get32(K, L_X, CS_PXH, si);
        if (lt_edge(x, x0)) ch |= 1;
        if (gt_edge(x, x1)) ch |= 2;
        if (!(cl | ch)) {
            put32(K, T_X, CS_TXH, di, x); put32(K, T_Y, CS_TYH, di, get32(K, L_Y, CS_PYH, si)); di += 2;
        } else if (!(cl & ch)) {
            if (!cl) edge32(K, si, bp, ch, &di, true, variant);
            else if (!ch) {
                edge32(K, bp, si, cl, &di, true, variant);
                put32(K, T_X, CS_TXH, di, get32(K, L_X, CS_PXH, si)); put32(K, T_Y, CS_TYH, di, get32(K, L_Y, CS_PYH, si)); di += 2;
            } else { edge32(K, bp, si, cl, &di, true, variant); edge32(K, si, bp, ch, &di, true, variant); }
        }
        bp = si; cl = ch;
    }
    n = (u16)(di >> 1);
    KW(CS_POLY_N) = n;
    if ((s16)n < 2) {                                     /* low words only are copied back */
        for (u16 i = 0; i < n; i++) { KW(L_X + 2 * i) = KW(T_X + 2 * i); KW(L_Y + 2 * i) = KW(T_Y + 2 * i); }
        return;
    }
    di = 0; bp = (u16)((n - 1) * 2); cl = 0;
    { s32 y = get32(K, T_Y, CS_TYH, bp); if (lt_edge(y, y0)) cl |= 8; if (gt_edge(y, y1)) cl |= 4; }
    for (u16 si = 0; (u16)(si >> 1) != n; si += 2) {
        u8 ch = 0;
        s32 y = get32(K, T_Y, CS_TYH, si);
        if (lt_edge(y, y0)) ch |= 8;
        if (gt_edge(y, y1)) ch |= 4;
        if (!(cl | ch)) {
            put32(K, L_X, CS_PXH, di, get32(K, T_X, CS_TXH, si)); put32(K, L_Y, CS_PYH, di, y); di += 2;
        } else if (!(cl & ch)) {
            if (!cl) edge32(K, si, bp, ch, &di, false, variant);
            else if (!ch) {
                edge32(K, bp, si, cl, &di, false, variant);
                put32(K, L_X, CS_PXH, di, get32(K, T_X, CS_TXH, si)); put32(K, L_Y, CS_PYH, di, get32(K, T_Y, CS_TYH, si)); di += 2;
            } else { edge32(K, bp, si, cl, &di, false, variant); edge32(K, si, bp, ch, &di, false, variant); }
        }
        bp = si; cl = ch;
    }
    KW(CS_POLY_N) = (u16)(di >> 1);
}

/* PS 3d50:34e6 / AP 277a:2caa — with the edge helpers clip32a_x_edge / clip32a_y_edge (PS 33c4/3455,
 * AP 2b88/2c19) = edge32 variant 0 */
void clip2d32a(void) { clip2d32(0); }
/* PS 3d50:3a04 / AP 277a:3242 — with clip32b_x_edge / clip32b_y_edge (PS 385a/392f, AP 3070/3159) =
 * edge32 variant 1 */
void clip2d32b(void) { clip2d32(1); }

/* PS 3d50:3713 / AP 277a:2ed7 near_isect32a — near intersection on the low words, added to the 32-bit anchor */
static void near_isect32a(u8 *K, u16 b, u16 a)
{
    bool ps = r3d_is_ps();
    s16 bx = (s16)((ps ? KW(CS_FOCAL) : CSW(CS_NEAR_Z)) - KW(CS_EY + b));
    s16 cx = (s16)(KW(CS_EY + a) - KW(CS_EY + b));
    s16 q = idiv16_retry((s32)(s16)(KW(CS_EX + a) - KW(CS_EX + b)) * bx, cx, NULL);
    u32 vx = (u32)(s32)q + (u32)get32(K, CS_EX, CS_EXH, b);
    q = idiv16_retry((s32)(s16)(KW(CS_EZ + a) - KW(CS_EZ + b)) * bx, cx, NULL);
    u32 vy = (u32)(s32)q + (u32)get32(K, CS_EZ, CS_EZH, b);
    if (!ps) { u16 sh = CSW(CS_NEAR_SHIFT); for (u16 c = sh; c; c--) { vx <<= 1; vy <<= 1; } }
    put32(K, CS_VX, CS_VXH, b, (s32)vx);
    put32(K, CS_VY, CS_VYH, b, (s32)vy);
}
/* PS 3d50:3c31 / AP 277a:346b near_isect32b — full 32-bit near intersection */
static void near_isect32b(u8 *K, u16 b, u16 a)
{
    s32 exb = get32(K, CS_EX, CS_EXH, b), eyb = get32(K, CS_EY, CS_EYH, b), ezb = get32(K, CS_EZ, CS_EZH, b);
    s32 dy = (s32)((u32)get32(K, CS_EY, CS_EYH, a) - (u32)eyb);
    s32 dx = (s32)((u32)get32(K, CS_EX, CS_EXH, a) - (u32)exb);
    s32 dz = (s32)((u32)get32(K, CS_EZ, CS_EZH, a) - (u32)ezb);
    if (r3d_is_ps()) {
        s32 f = (s32)((u32)(s32)KS(CS_FOCAL) - (u32)eyb);          /* focal (cdq) − ey[b] */
        r3d_proj32(&dx, &dz, dy, f);
        put32(K, CS_VX, CS_VXH, b, (s32)((u32)dx + (u32)exb));
        put32(K, CS_VY, CS_VYH, b, (s32)((u32)dz + (u32)ezb));
        return;
    }
    s32 t = (s32)((u32)CSW(CS_NEAR_Z) - (u32)eyb);                 /* movzx ecx, near_z */
    u8 sh = (u8)CSW(CS_NEAR_SHIFT);
    u32 vx = (u32)idiv32_386((s64)dx * t, dy, NULL) + (u32)exb;
    u32 vy = (u32)idiv32_386((s64)dz * t, dy, NULL) + (u32)ezb;
    put32(K, CS_VX, CS_VXH, b, (s32)(vx << (sh & 31)));
    put32(K, CS_VY, CS_VYH, b, (s32)(vy << (sh & 31)));
}
/* PS 3d50:376e / AP 277a:2f4e */
void near_clip32a(void) { u8 *K = cs_blk(); near_walk(K, near_isect32a, true); }
/* PS 3d50:3cb4 / AP 277a:3530 */
void near_clip32b(void) { u8 *K = cs_blk(); near_walk(K, near_isect32b, true); }

/* PS 3d50:3d9f / AP 277a:35d0 */
void poly_add_centre32(void)
{
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    for (u16 i = 0; i < n * 2; i += 2) put32(K, CS_PX, CS_PXH, i, (s32)((u32)(s32)KS(CS_CX) + (u32)get32(K, CS_PX, CS_PXH, i)));
    for (u16 i = 0; i < n * 2; i += 2) put32(K, CS_PY, CS_PYH, i, (s32)((u32)(s32)KS(CS_CY) - (u32)get32(K, CS_PY, CS_PYH, i)));
}

/* PS 3d50:296d / AP 277a:361d — modes 1 and 2 (render3d_poly.md §3.4) */
void part_draw32(FarPtr var)
{
    u8 *K = cs_blk();
    u16 es = var.seg, di = var.off;
    u16 bp = KW(CS_VTX_OFS);
    if (KW(CS_VTX_KEY + 2) != es || KW(CS_VTX_KEY) != bp) {
        KW(CS_VTX_KEY + 2) = es; KW(CS_VTX_KEY) = bp;
        for (u16 i = 0; i < KW(CS_VTX_COUNT); i++) KW(CS_VTX_STATE + 2 * i) = 0;
    }
    s16 count = (s16)rd16(es, di);
    di = rd16(es, (u16)(di + 2));
    for (; --count >= 0; di = (u16)(di + 8)) {
        u8 f = rd8(es, di), m = f & 3;
        if (m == 2) continue;
        u16 si = rd16(es, (u16)(di + 6));
        KB(CS_BACKLIT) = 0;
        if (m == 0) {
            if ((f & 0x20) && poly_facing(rd8(es, (u16)(di + 5)), rd8(es, si), es, bp) >= 0) KB(CS_BACKLIT) = 1;
        } else if (poly_facing(rd8(es, (u16)(di + 5)), rd8(es, si), es, bp) >= 0) continue;
        poly_colours(es, di, bp);
        bool behind = false;
        KW(CS_POLY_N) = 0;
        for (;; si++) {
            u8 idx = rd8(es, si);
            if (idx == 0xFF) break;
            u16 bx = (u16)(idx * 2);
            u8 st = KB(CS_VTX_STATE + bx);
            if (st != 2) {
                if (st == 0) {
                    KB(CS_VTX_STATE + bx) = 1;
                    u16 v = (u16)(bx * 3 + bp);
                    KW(CS_SCR_VEC) = rd16(es, v);
                    KW(CS_SCR_VEC + 2) = rd16(es, (u16)(v + 2));
                    KW(CS_SCR_VEC + 4) = rd16(es, (u16)(v + 4));
                    bool cf = vtx_rotate_shift(K + CS_SCR_VEC, K + CS_M_OBJ_EYE, K + CS_SCR_L32, KW(CS_OBJ_SHIFT));
                    /* adc chain through all six words (original bug kept, render3d_math.md §4.8) */
                    u64 s = (u64)KL(CS_SCR_L32) + KL(CS_E_POS) + cf;
                    put32(K, CS_EX, CS_EXH, bx, (s32)(u32)s);
                    s = (u64)KL(CS_SCR_L32 + 4) + KL(CS_E_POS + 4) + (u32)(s >> 32);
                    put32(K, CS_EY, CS_EYH, bx, (s32)(u32)s);
                    s = (u64)KL(CS_SCR_L32 + 8) + KL(CS_E_POS + 8) + (u32)(s >> 32);
                    put32(K, CS_EZ, CS_EZH, bx, (s32)(u32)s);
                }
                s16 hi = KS(CS_EYH + bx);
                if (hi < 0 || (hi == 0 && KW(CS_EY + bx) < CSW(CS_NEAR_Z))) {
                    KB(CS_VTX_STATE + bx) = 3;
                    behind = true;
                } else {
                    KB(CS_VTX_STATE + bx) = 2;
                    s32 x = get32(K, CS_EX, CS_EXH, bx), z = get32(K, CS_EZ, CS_EZH, bx);
                    r3d_proj32(&x, &z, get32(K, CS_EY, CS_EYH, bx), (s32)KW(CS_FOCAL));
                    put32(K, CS_VX, CS_VXH, bx, x);
                    put32(K, CS_VY, CS_VYH, bx, z);
                }
            }
            KW(CS_POLY_IDX + 2 * KW(CS_POLY_N)) = bx;
            KW(CS_POLY_N)++;
        }
        if (behind) {
            if (KB(CS_DRAW_MODE) == 1) near_clip32a(); else near_clip32b();
        } else {
            u16 n = KW(CS_POLY_N);
            if (!n) continue;
            for (u16 i = 0; i < n; i++) {
                u16 s = KW(CS_POLY_IDX + 2 * i);
                KW(CS_PX + 2 * i) = KW(CS_VX + s); KW(CS_PXH + 2 * i) = KW(CS_VXH + s);
                KW(CS_PY + 2 * i) = KW(CS_VY + s); KW(CS_PYH + 2 * i) = KW(CS_VYH + s);
            }
        }
        poly_add_centre32();
        if (KB(CS_NEED_CLIP)) { if (KB(CS_DRAW_MODE) == 1) clip2d32a(); else clip2d32b(); }
        poly_fill();                                          /* flag 10h ignored in the 32-bit modes */
    }
}

/* ================================================================================================
 * Boxes (render3d_math.md §5.2)
 * ============================================================================================== */

/* AP 277a:384e — swap *a and *b when *a > *b (signed 32) */
void lswap_order(u16 a, u16 b)
{
    if (DSSL(a) > DSSL(b)) { u32 t = DSL(b); DSL(b) = DSL(a); DSL(a) = t; }
}
/* AP 277a:3892 */
void lbox_order(u16 mn, u16 mx)
{
    lswap_order(mn, mx);
    lswap_order((u16)(mn + 4), (u16)(mx + 4));
    lswap_order((u16)(mn + 8), (u16)(mx + 8));
}
/* AP 277a:38d0 — 1 when the boxes overlap on all axes (no callers found) */
s16 lbox_overlap(u16 amin, u16 amax, u16 bmin, u16 bmax)
{
    for (int i = 0; i < 12; i += 4) {
        if (DSSL(amin + i) > DSSL(bmax + i)) return 0;
        if (DSSL(amax + i) < DSSL(bmin + i)) return 0;
    }
    return 1;
}
