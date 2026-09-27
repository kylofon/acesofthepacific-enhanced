/* render3d — 3Space core: near plane, camera, light, polygon colours and fill, the 16-bit part
 * routines (AP 277a, PS 3d50). render3d_math.md §4.1/§4.2/§4.6, render3d_poly.md §2, §3.1–§3.3.
 *
 * The routines run with DS = CS in the original: every CS:xxxx below is the code-segment block
 * (r3d_priv.h, AP offsets); the shape data is addressed through ES = the shape segment and BP = the
 * vertex array offset. PORT: the loop counters CS:03B7/07AC and the "behind" flags CS:03B9/03BA are
 * kept in C locals (nothing else reads them). The data blocks m_cam_block (277a:09b6 / 3d50:3e0f),
 * m_obj_eye_block (09c9), eye_in_obj_block (0a07), e_pos16_block (0a0d) that the index lists as
 * functions are the CS variables accessed through CS_* (render3d_math.md §2.3). */
#include "render3d/r3d_priv.h"
#include "codeptr.h"
#include "video/video.h"      /* PORT: video.h uses CodeFn without including codeptr.h */
#include "symbols_ap.h"

/* ================================================================================================
 * Near plane and camera (render3d_math.md §4.1, §4.2, §4.6)
 * ============================================================================================== */

/* PS 3d50:2342 / AP 277a:0012 — near = 1 << n, near_shift = S − n */
void set_near(u16 n)
{
    CSW(CS_NEAR_Z) = (u16)(1u << (n & 31));
    CSW(CS_NEAR_SHIFT) = (u16)(CSW(CS_PROJ_SHIFT) - n);
}
/* AP 277a:002d */
s16 get_near_n(void) { return (s16)(CSW(CS_PROJ_SHIFT) - CSW(CS_NEAR_SHIFT)); }
/* AP 277a:0037 — near = focal */
void reset_near(void) { CSW(CS_NEAR_Z) = CSW(CS_FOCAL); CSW(CS_NEAR_SHIFT) = 0; }

/* PS 3d50:237b / AP 277a:004b */
void cam_begin_frame(u16 c_ds)
{
    u8 *K = cs_blk();
    u16 eye = DSW(c_ds + 2);
    for (int i = 0; i < 12; i += 2) KW(CS_CAM_POS + i) = DSW(eye + 2 + i);
    for (int i = 0; i < 6; i += 2) KW(CS_CAM_ANG + i) = DSW(eye + 0x0E + i);
    u16 S = DSW(c_ds);
    KW(CS_PROJ_SHIFT) = S;
    KW(CS_FOCAL) = (u16)(1u << (S & 31));
    CSW(CS_NEAR_Z) = KW(CS_FOCAL);
    CSW(CS_NEAR_SHIFT) = 0;
    KW(CS_SHADES) = DSW(c_ds + 0x12);
    KW(CS_REMAP) = DSW(c_ds + 0x14);
    u16 x0 = DSW(c_ds + 0x0A), y0 = DSW(c_ds + 0x0C), w = DSW(c_ds + 0x0E), h = DSW(c_ds + 0x10);
    DRV_X1 = (s16)x0;
    DRV_X2 = (s16)(x0 + w - 1);
    DRV_Y1 = (s16)y0;
    DRV_Y2 = (s16)(y0 + h - 1);
    KW(CS_CX) = (u16)((w >> 1) + x0);
    KW(CS_CY) = (u16)((h >> 1) + y0);
    KB(CS_FORCE_CLIP) = 0;
    KB(CS_09B1) = 0;
    for (int i = 0; i < 6; i += 2) KW(CS_SCR_VEC + i) = DSW(c_ds + 4 + i);
    r3d_mat_from_angles(K + CS_SCR_VEC, K + CS_M_LOOK);
    if (KB(CS_M_LOOK + 0x12) == 0) r3d_mat_from_angles(K + CS_CAM_ANG, K + CS_M_CAM);
    else {
        r3d_mat_from_angles(K + CS_CAM_ANG, K + CS_M_OBJ_EYE);
        r3d_mat_mul(K + CS_M_LOOK, K + CS_M_OBJ_EYE, K + CS_M_CAM);
        r3d_mat_to_angles(K + CS_M_CAM, K + CS_CAM_ANG);
    }
    r3d_mat_transpose(K + CS_M_CAM);
}

/* PS 3d50:2482 / AP 277a:0152 — light = forward axis of (pitch a0, heading a1), length 200h; the copy
 * to DGROUP DS:6492 (PS 76E8); returns CS:0008 */
void set_light(u16 a0, u16 a1)
{
    u8 *K = cs_blk();
    KW(CS_SCR_VEC) = a0; KW(CS_SCR_VEC + 2) = 0; KW(CS_SCR_VEC + 4) = a1;
    r3d_mat_from_angles(K + CS_SCR_VEC, K + CS_M_LOOK);         /* overwrites m_look (harmless) */
    KW(CS_SCR_VEC) = 0; KW(CS_SCR_VEC + 4) = 0; KW(CS_SCR_VEC + 2) = 0x200;
    vec_rotate_n(K + CS_SCR_VEC, K + CS_M_LOOK, csp(CS_LIGHT_WORLD));
    u16 d = DS_LIGHT_COPY;
    DSW(d) = CSW(CS_LIGHT_WORLD); DSW(d + 2) = CSW(CS_LIGHT_WORLD + 2); DSW(d + 4) = CSW(CS_LIGHT_WORLD + 4);
}

/* ================================================================================================
 * Per-polygon routines (render3d_poly.md §2)
 * ============================================================================================== */

/* PS 3d50:27de / AP 277a:01ba — colours of the polygon record ES:DI; BP = vertex array offset */
void poly_colours(u16 es, u16 di, u16 bp)
{
    u8 *K = cs_blk();
    u8 dl = DRV_VGA, cl, ch;
    if (dl) { cl = rd8(es, (u16)(di + 1)); ch = rd8(es, (u16)(di + 2)); }
    else    { cl = rd8(es, (u16)(di + 3)); ch = rd8(es, (u16)(di + 4)); }
    u8 al = rd8(es, di);
    u8 filled = (al & 0x80) ? 1 : 0;
    DRV_FILL_ON = filled;
    if (filled && (al & 0x20) && dl && KW(CS_SHADES)) {
        u16 n = DSB((u16)(KW(CS_SHADES) + ch));                  /* zero-extended: FFh excluded */
        if ((s16)n > 0 && (s16)n <= 8) {
            u16 bx = (u16)(rd8(es, (u16)(di + 5)) * 6 + bp);
            KW(CS_SCR_NORMAL) = rd16(es, bx);
            KW(CS_SCR_NORMAL + 2) = rd16(es, (u16)(bx + 2));
            KW(CS_SCR_NORMAL + 4) = rd16(es, (u16)(bx + 4));
            if (KB(CS_BACKLIT)) {
                KW(CS_SCR_NORMAL) = (u16)-KW(CS_SCR_NORMAL);
                KW(CS_SCR_NORMAL + 2) = (u16)-KW(CS_SCR_NORMAL + 2);
                KW(CS_SCR_NORMAL + 4) = (u16)-KW(CS_SCR_NORMAL + 4);
            }
            u16 level = (u16)((u32)vdot_n(K + CS_SCR_NORMAL, K + CS_LIGHT_OBJ) >> 16);
            if (level == 0x10) level--;
            u16 idx = (u16)(((n - 1) << 5) + (u16)(level + 0x10));
            u8 s = (u8)(KB((u16)(CS_SHADE_OFS + idx)) + ch);        /* 16-bit index, as DS:[bx+21F8] */
            if (cl == ch) cl = s;
            ch = s;
        }
    }
    u16 remap = KW(CS_REMAP);
    if (remap) { cl = DSB((u16)(remap + cl)); ch = DSB((u16)(remap + ch)); }
    DRV_EDGE_COL = cl;
    DRV_FILL_COL = ch;
}

/* PS 3d50:28c3 / AP 277a:029f — hand the CS:0C77/0C9F lists to the library fill (1d32:09ce) */
void poly_fill(void)
{
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    if (!n) return;
    LW(G_poly_n) = n;
    u16 xs = lds(G_poly_x), ys = lds(G_poly_y);
    for (u16 i = 0; i < n; i++) { DSW(xs + 2 * i) = KW(CS_PX + 2 * i); DSW(ys + 2 * i) = KW(CS_PY + 2 * i); }
    gfx_polygon((s16)LW(G_poly_n), xs, ys);
}

/* PS 3d50:2908 / AP 277a:02e4 — flag 10h: 4 points to the 4-point bitmap drawer (unused by the data) */
void poly_fill_bitmap4(u16 es, u16 di)
{
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    if (n != 4) return;
    LW(G_poly_n) = n;
    u8 dl, dh;
    if (DRV_VGA) { dl = rd8(es, (u16)(di + 1)); dh = rd8(es, (u16)(di + 2)); }
    else         { dl = rd8(es, (u16)(di + 3)); dh = rd8(es, (u16)(di + 4)); }
    u16 xs = lds(G_poly_x), ys = lds(G_poly_y);
    for (u16 i = 0; i < n; i++) { DSW(xs + 2 * i) = KW(CS_PX + 2 * i); DSW(ys + 2 * i) = KW(CS_PY + 2 * i); }
    bitmap4_draw((u16)(dh << 8 | dl), xs, ys);
}

/* PS 3d50:573e / AP 277a:0346 — N·(v >> −vtx_shift + eye_in_obj) for the normal vertex AL and the first
 * vertex AH of the polygon; < 0 = front facing. Leaves N at CS:21BC and the point at CS:21C2. */
s32 poly_facing(u8 al, u8 ah, u16 es, u16 bp)
{
    u8 *K = cs_blk();
    u16 bx = (u16)(al * 6 + bp);
    KW(CS_SCR_NORMAL) = rd16(es, bx);
    KW(CS_SCR_NORMAL + 2) = rd16(es, (u16)(bx + 2));
    KW(CS_SCR_NORMAL + 4) = rd16(es, (u16)(bx + 4));
    bx = (u16)(ah * 6 + bp);
    u8 cl = (u8)-(u8)KW(CS_VTX_SHIFT);
    for (int i = 0; i < 6; i += 2)
        KW(CS_SCR_RESULT + i) = (u16)(((s16)rd16(es, (u16)(bx + i)) >> (cl & 31)) + KW(CS_EYE_IN_OBJ + i));
    return vdot_n(K + CS_SCR_NORMAL, K + CS_SCR_RESULT);
}

/* ================================================================================================
 * The 16-bit part routines (render3d_poly.md §3.1–§3.3)
 * ============================================================================================== */

enum { PD16, PD_SHADOW, PD16S };

/* (i32)v << S then IDIV by the depth; the INT 0 handler halves an overflowing dividend. */
static s16 proj16(s16 v, s16 depth, u16 S)
{
    return idiv16_retry((s32)((u32)(s32)v << (S & 31)), depth, NULL);
}

static void project16(u8 *K, u16 bx)
{
    u16 S = KW(CS_PROJ_SHIFT);
    s16 si = KS(CS_EY + bx);
    KW(CS_VX + bx) = (u16)proj16(KS(CS_EX + bx), si, S);
    KW(CS_VY + bx) = (u16)proj16(KS(CS_EZ + bx), si, S);
}

static void vtx_cache_check(u8 *K, u16 es, u16 bp)
{
    if (KW(CS_VTX_KEY + 2) != es || KW(CS_VTX_KEY) != bp) {
        KW(CS_VTX_KEY + 2) = es;
        KW(CS_VTX_KEY) = bp;
        for (u16 i = 0; i < KW(CS_VTX_COUNT); i++) KW(CS_VTX_STATE + 2 * i) = 0;   /* rep stosw */
    }
}

static void part_draw_common(FarPtr var, int kind)
{
    u8 *K = cs_blk();
    u16 es = var.seg, di = var.off;
    u16 bp = KW(CS_VTX_OFS);
    vtx_cache_check(K, es, bp);
    s16 count = (s16)rd16(es, di);
    di = rd16(es, (u16)(di + 2));
    for (; --count >= 0; di = (u16)(di + 8)) {
        u8 f = rd8(es, di), m = f & 3;
        if (m == 2) continue;
        u16 si = rd16(es, (u16)(di + 6));
        if (kind == PD_SHADOW) {
            u8 c = LB(G_shadow_colour);                  /* 0 + [2402] */
            DRV_EDGE_COL = c;
            DRV_FILL_COL = c;
            DRV_FILL_ON = 1;
        } else {
            KB(CS_BACKLIT) = 0;
            if (m == 0) {
                if ((f & 0x20) && poly_facing(rd8(es, (u16)(di + 5)), rd8(es, si), es, bp) >= 0) KB(CS_BACKLIT) = 1;
            } else if (poly_facing(rd8(es, (u16)(di + 5)), rd8(es, si), es, bp) >= 0) continue;
            poly_colours(es, di, bp);
        }
        bool behind = false;
        KW(CS_POLY_N) = 0;
        for (;; si++) {
            u8 idx = rd8(es, si);
            if (idx == 0xFF) break;
            u16 bx = (u16)(idx * 2);
            u8 st = KB(CS_VTX_STATE + bx);
            if (st == 2) goto listed;
            if (st == 0) {
                KB(CS_VTX_STATE + bx) = 1;
                u16 v = (u16)(bx * 3 + bp);
                KW(CS_SCR_VEC) = rd16(es, v);
                KW(CS_SCR_VEC + 2) = rd16(es, (u16)(v + 2));
                KW(CS_SCR_VEC + 4) = rd16(es, (u16)(v + 4));
                if (kind == PD_SHADOW) {
                    r3d_mat_transpose(K + CS_M_OBJ);
                    vec_rotate_n(K + CS_SCR_VEC, K + CS_M_OBJ, K + CS_SCR_RESULT);
                    KW(CS_SCR_VEC) = KW(CS_SCR_RESULT);
                    KW(CS_SCR_VEC + 2) = KW(CS_SCR_RESULT + 2);
                    KW(CS_SCR_VEC + 4) = 0;                        /* flattened onto the ground */
                    vec_rotate_n(K + CS_SCR_VEC, K + CS_M_CAM, K + CS_SCR_RESULT);
                } else vec_rotate_n(K + CS_SCR_VEC, K + CS_M_OBJ_EYE, K + CS_SCR_RESULT);
                s16 r[3];
                for (int i = 0; i < 3; i++) {
                    r[i] = KS(CS_SCR_RESULT + 2 * i);
                    if (kind == PD16S) {                           /* shift by obj_shift (= vtx_shift) */
                        s8 cl = (s8)KB(CS_OBJ_SHIFT);
                        r[i] = cl > 0 ? (s16)((u16)r[i] << (cl & 31)) : (s16)(r[i] >> ((u8)-cl & 31));
                    }
                }
                KW(CS_EX + bx) = (u16)(r[0] + KW(CS_E_POS));
                KW(CS_EY + bx) = (u16)(r[1] + KW(CS_E_POS + 4));
                KW(CS_EZ + bx) = (u16)(r[2] + KW(CS_E_POS + 8));
                if (kind == PD_SHADOW) r3d_mat_transpose(K + CS_M_OBJ);
            }
            if (kind == PD16S) {                                    /* no near test in mode 3 */
                KB(CS_VTX_STATE + bx) = 2;
                project16(K, bx);
            } else if (KS(CS_EY + bx) < (s16)CSW(CS_NEAR_Z)) {
                KB(CS_VTX_STATE + bx) = 3;
                behind = true;
            } else {
                KB(CS_VTX_STATE + bx) = 2;
                project16(K, bx);
            }
listed:
            KW(CS_POLY_IDX + 2 * KW(CS_POLY_N)) = bx;
            KW(CS_POLY_N)++;
        }
        if (behind) near_clip16();
        else {
            u16 n = KW(CS_POLY_N);
            if (!n) continue;
            for (u16 i = 0; i < n; i++) {
                u16 s = KW(CS_POLY_IDX + 2 * i);
                KW(CS_PX + 2 * i) = KW(CS_VX + s);
                KW(CS_PY + 2 * i) = KW(CS_VY + s);
            }
        }
        poly_add_centre16();
        if (rd8(es, di) & 0x10) poly_fill_bitmap4(es, di);
        else {
            if (KB(CS_NEED_CLIP)) clip2d16();
            poly_fill();
        }
    }
}

/* PS 3d50:59be / AP 277a:05c6 — mode 0 */
void part_draw16(FarPtr a0) { part_draw_common(a0, PD16); }
/* PS 3d50:57b3 / AP 277a:03bb — mode 0 with the shadow flag DS:2403: flattened, shadow colour */
void part_draw_shadow(FarPtr a0) { part_draw_common(a0, PD_SHADOW); }
/* PS 3d50:2b92 / AP 277a:07ae — mode 3: vertices shifted by obj_shift, no near plane */
void part_draw16s(FarPtr a0) { part_draw_common(a0, PD16S); }
