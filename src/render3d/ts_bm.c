/* render3d — ts_bm.c: bitmap shapes / .BMD effects (AP 2e33, loader 241c; PS 4818).
 * render3d_shapes.md §4, video_ts.md §5. The PS item drawer differs (scale base << 10, size cap before
 * scaling, position per item, gfx_sprite_rotated); PS bm_draw culls with ts_visible_lod. */
#include "render3d/r3d_priv.h"
#include "codeptr.h"
#include "video/video.h"
#include "platform/platform.h"

static s16 ldiv_s32(s32 a, s32 b) { return (s16)(b ? a / b : (s32)div_unreachable("LDIV by zero")); }   /* LDIV@ */
static s32 lxlsh(s32 v, u8 cl) { return cl >= 32 ? 0 : (s32)((u32)v << cl); }                        /* LXLSH@ */

/* ================================================================================================
 * .BMD loading (AP 241c)
 * ============================================================================================== */

/* AP 241c:0005 — build a bitmap shape for shape_id from the BMD file `name` (render3d_shapes.md §4.2);
 * returns the far pointer to the BMD data D (DX:AX) or 0 */
u32 bmd_load_far(s16 id, s16 set, u16 name_ds)
{
    FarPtr q = { 0, 0 };
    u16 f = ts_open(name_ds, 0x1D64);                           /* "VER:" */
    if (!f) return 0;
    u16 m = stk_mark(), ver = stk_alloc(2), nm = stk_alloc(10);
    if (my_fread(ver, 1, 2, f) != 2 || DSB(ver) != '4') goto done;
    FarPtr tmpl = shape_ptr(id);
    if (far_is_null(tmpl)) goto done;
    if (chunk_find(f, 0x1D69, 0) != -1) {                        /* "EXT:" */
        my_fread(DS_BMD_EXT0, 2, 1, f);
        my_fread(DS_BMD_EXT1, 2, 1, f);
    } else DSW(DS_BMD_EXT0) = 0;
    if (chunk_find(f, 0x1D6E, 0) == -1) goto done;               /* "BMD:" */
    {
        u32 n = (u32)chunk_size(f) + 0x16;
        u16 h = ap_pool_alloc_1d32(1, (u16)n);
        if (!h) goto done;
        FarPtr p = pool_map_save(h);
        ts_read(ds_ptr(nm), 8, f);
        DSB(nm + 8) = 0;
        huge_memset_fp(p, 0, n);
        wr8(p.seg, (u16)(p.off + 1), rd8(tmpl.seg, (u16)(tmpl.off + 1)));    /* class of the placeholder */
        FarPtr d = far_add(p, 0x1A);
        n -= 0x1A;
        wr16(d.seg, d.off, DSW(DS_BMD_EXT0));
        wr16(d.seg, (u16)(d.off + 2), DSW(DS_BMD_EXT1));
        n -= 4;
        d = far_add(d, 4);
        ts_read(d, n, f);
        q = d;
        wr8(p.seg, p.off, 0x30);                                  /* no box + bitmap */
        wr16(p.seg, (u16)(p.off + 0x0C), rd16(q.seg, q.off));      /* radius */
        wr16(p.seg, (u16)(p.off + 4), 1);                         /* one state slot */
        wr16(p.seg, (u16)(p.off + 6), (u16)(q.off + 2));          /* its variant count = D+2 */
        wr16(p.seg, (u16)(p.off + 0x0A), (u16)set);
        shape_set_ptr(id, p);
        pool_unmap(h);
    }
done:
    stk_release(m);
    ts_close(f);
    return (u32)q.seg << 16 | q.off;
}
/* AP 241c:0005 — render3d_shapes.md §4.2 (the u16 return is the offset; bmd_load_far has DX:AX) */
u16 bmd_load(s16 id, s16 set, u16 name_ds) { return (u16)bmd_load_far(id, set, name_ds); }

/* AP 241c:01f6 — dos_free of the shape pointer (a handle-allocated shape: latent bug kept) */
void bmd_free(s16 id) { dos_free(shape_ptr(id)); }

/* ================================================================================================
 * Size cap / multiplier (AP 2e33:0006/0011/08e7, PS 4818:092f)
 * ============================================================================================== */

/* AP 2e33:0006 */
void bm_set_size_cap(s16 a0) { DSW(AP_DS_bm_size_cap) = (u16)a0; }
/* AP 2e33:0011 */
void bm_reset_size_cap(void) { DSW(AP_DS_bm_size_cap) = 0x200; }
/* PS 4818:092f / AP 2e33:08e7 */
void bm_set_mult(s16 n) { DSW(DS_BM_MULT) = (u16)n; }

/* ================================================================================================
 * Drawing
 * ============================================================================================== */

/* AP 2e33:0208 — 1 = culled; sets app_size (render3d_shapes.md §4.3) */
u8 bm_visible(void)
{
    u8 *K = cs_blk();
    s32 ey = KSL(CS_E_POS + 4);
    if ((s32)(s16)CSW(CS_NEAR_Z) > ey) return 1;
    u16 m = stk_mark(), c = stk_alloc(12), p = stk_alloc(8);
    u8 r = 1;
    DSL(c) = KL(CS_RADIUS);
    DSSL(c + 4) = ey;
    s32 A = (s32)apparent_size(c);
    if (A) {
        KW(CS_APP_SIZE) = (u16)A;
        s32 R = KSL(CS_RADIUS);
        s32 ax = KSL(CS_E_POS), az = KSL(CS_E_POS + 8);
        DSL(c) = (u32)(ax < 0 ? -ax : ax) - (u32)R;
        DSL(c + 8) = (u32)(az < 0 ? -az : az) - (u32)R;
        DSSL(c + 4) = ey;
        pt_project(c, p);
        pt_add_centre(p);
        r = (DSSL(p) > (s32)DRV_X2 || DSSL(p + 4) < (s32)DRV_Y1) ? 1 : 0;
    }
    stk_release(m);
    return r;
}

static s16 abs_sat(s16 v) { return v >= 0 ? v : v == (s16)0x8000 ? 0x7FFF : (s16)-v; }

/* PS 4818:0468 / AP 2e33:0333 — elevation / azimuth of the eye seen from the object, on-screen roll */
void bm_view_angles(void)
{
    u8 *K = cs_blk();
    s16 v[3];
    for (int i = 0; i < 3; i++) v[i] = (s16)-KS(CS_EYE_IN_OBJ + 2 * i);
    s16 d = (s16)dist2_16((u16)v[0], (u16)v[1]);
    s16 elev = (s16)(0x4000 - r3d_atan2_16(v[2], d));
    DSS(DS_BM_ELEV) = elev;
    bool clamped;
    if (abs_sat(elev) > 0x3FD0) {
        DSW(DS_BM_ELEV) = elev < 0 ? 0xC000 : 0x4000;
        DSW(DS_BM_AZIM) = 0;
        clamped = true;
    } else {
        DSW(DS_BM_AZIM) = (u16)(r3d_atan2_16(v[0], v[1]) + 0xC000);
        clamped = false;
    }
    for (int i = 0; i < 3; i++) v[i] = KS(CS_E_POS16 + 2 * i);
    d = (s16)dist2_16((u16)v[0], (u16)v[1]);
    s16 si = (s16)(0x4000 - r3d_atan2_16(v[2], d));
    s16 az2;
    if (abs_sat(si) > 0x3FD0) { si = si < 0 ? (s16)0xC000 : 0x4000; az2 = 0; }
    else az2 = (s16)(r3d_atan2_16(v[0], v[1]) + 0xC000);
    u8 m1[20], r1[20], m2[20], ang[6];
    memcpy(m1, K + CS_M_OBJ_EYE, 0x13);
    p16(ang, 0, 0); p16(ang, 2, 0); p16(ang, 4, (u16)-az2);
    r3d_mat_from_angles(ang, r1);
    r3d_mat_mul(m1, r1, m2);
    p16(ang, 0, (u16)-si); p16(ang, 2, 0); p16(ang, 4, 0);
    r3d_mat_from_angles(ang, m1);
    r3d_mat_mul(m2, m1, r1);                                     /* M3 in r1 (bp-46) */
    u16 a = clamped ? r3d_atan2_16(gs16(r1, 6), gs16(r1, 0x0A)) : r3d_atan2_16(gs16(r1, 0x0C), gs16(r1, 0x10));
    DSW(DS_BM_ROLL) = (u16)(0x4000 - a);
}

/* PS 4818:0152 / AP 2e33:001c — one item of a BMD frame. AP: (item, a, b, x, y, k, c) with the frame's
 * size/position; PS: (item) and everything computed here (video_ts.md §5). */
void bm_draw_item(FarPtr it, s16 a, s16 b, s16 x, s16 y, s16 k, s16 c)
{
    u8 *K = cs_blk();
    s16 idx = (s8)rd8(it.seg, it.off);
    u16 bm = bitmap_global((s16)(DSW(DS_BM_BASE) + idx));
    u16 roll = DSW(DS_BM_ROLL);
    s16 mult = DSS(DS_BM_MULT);
    FarPtr hot = ds_far(DS_BM_HOT);
    s16 hx = (s8)rd8(hot.seg, (u16)(hot.off + 2 * idx)), hy = (s8)rd8(hot.seg, (u16)(hot.off + 2 * idx + 1));
    if (r3d_is_ps()) {
        if (!bm) r3d_assert(0x23D6, 0x5D, 0x3B26, 0x94);
        s16 ex = KS(CS_E_POS16), ey = KS(CS_E_POS16 + 2), ez = KS(CS_E_POS16 + 4);
        if ((s16)KW(CS_FOCAL) > ey) return;
        s16 app = (s16)KW(CS_APP_SIZE);
        if (app > DSS(DS_BM_CAP)) app = DSS(DS_BM_CAP);
        s16 s = ldiv_s32(lxlsh(app, 10), 0x7F);
        u16 isx = rd16(it.seg, (u16)(it.off + 3)), isy = rd16(it.seg, (u16)(it.off + 5));
        s16 sx = isx ? (s16)((s32)((u32)(s32)s * (u32)(s32)(s16)isx) >> 10) : s;
        s16 sy = isy ? (s16)((s32)((u32)(s32)s * (u32)(s32)(s16)isy) >> 10) : s;
        s16 cc = 0;
        if (DSW(DS_BMD_EXT0)) {
            s16 kk = r3d_acos16(ldiv_s32(lxlsh(DSS(DS_BMD_EXT1), 14), 100));
            s32 t = (s32)(((u32)(s32)DSS(DS_BM_ELEV) << 1) * (u32)(s32)kk);         /* LXMUL@ */
            cc = (s16)(r3d_cos((u16)ldiv_s32(t, (s32)0xFFFF8000)) >> 4);
            sy = (s16)mul_shr10((u16)sy, (u16)cc);
        }
        s16 w = (s16)mul_shr10(DSW(bm + 6), (u16)sx), h = (s16)mul_shr10(DSW(bm + 8), (u16)sy);
        if (!w || !h) return;
        u16 S = KW(CS_PROJ_SHIFT);
        s16 px = (s16)(KW(CS_CX) + ldiv_s32(lxlsh(ex, (u8)S), ey));
        s16 py = (s16)(KW(CS_CY) - ldiv_s32(lxlsh(ez, (u8)S), ey));
        s16 dx = (s8)rd8(it.seg, (u16)(it.off + 1)), dy = (s8)rd8(it.seg, (u16)(it.off + 2));
        if (DSW(DS_BMD_EXT0)) dy = (s16)mul_shr10((u16)dy, (u16)cc);
        s16 rx = (s16)((s16)mul_cos(dx, roll) - (s16)mul_sin(dy, roll));
        s16 ry = (s16)((s16)mul_sin(dx, roll) + (s16)mul_cos(dy, roll));
        px = (s16)(px + (s16)mul_shr10((u16)rx, (u16)s));
        py = (s16)(py + (s16)mul_shr10((u16)ry, (u16)s));
        DRV_CLIP_ON = 1;
        gfx_sprite_rotated_ind(bm, px, py, (u16)(rd16(it.seg, (u16)(it.off + 7)) + roll),
                               (s16)(w * mult), (s16)(h * mult), idiv16_retry(hx, mult, NULL), idiv16_retry(hy, mult, NULL));
        return;
    }
    if (!bm) r3d_assert(0x23D6, 0x5D, 0x3B26, 0x94);
    u16 isx = rd16(it.seg, (u16)(it.off + 3)), isy = rd16(it.seg, (u16)(it.off + 5));
    if (isx) a = (s16)mul_shr10((u16)a, isx);
    if (isy) b = (s16)mul_shr10((u16)b, isy);
    s16 cap = DSS(AP_DS_bm_size_cap);
    if (a > cap) a = cap;
    if (b > cap) b = cap;
    s16 w = (s16)mul_shr8(DSW(bm + 6), (u16)a), h = (s16)mul_shr8(DSW(bm + 8), (u16)b);
    if (!w || !h) return;
    s16 dx = (s8)rd8(it.seg, (u16)(it.off + 1)), dy = (s8)rd8(it.seg, (u16)(it.off + 2));
    if (DSW(DS_BMD_EXT0)) dy = (s16)mul_shr8((u16)dy, (u16)c);
    s16 rx = (s16)((s16)mul_cos(dx, roll) - (s16)mul_sin(dy, roll));
    s16 ry = (s16)((s16)mul_sin(dx, roll) + (s16)mul_cos(dy, roll));
    rx = (s16)mul_shr8((u16)rx, (u16)k);
    ry = (s16)mul_shr8((u16)ry, (u16)k);
    DRV_CLIP_ON = 1;
    bmp_draw_ems_rotated(bm, (u16)(x + rx), (u16)(y + ry), (u16)(rd16(it.seg, (u16)(it.off + 7)) + roll),
                         (u16)(w * mult), (u16)(h * mult),
                         (u16)idiv16_retry(hx, mult, NULL), (u16)idiv16_retry(hy, mult, NULL));
}

/* pick the entry {i16 value, u16 offset} with the smallest |value − ref| (first on ties) */
static u16 bm_pick(FarPtr *q, s16 ref, bool *ok)
{
    s16 n = (s16)rd16(q->seg, q->off);
    q->off = (u16)(q->off + 2);
    if (!n) { *ok = false; return 0; }
    s16 best = -1;
    u16 ofs = 0;
    for (s16 i = 0; i < n; i++) {
        s16 d = abs_sat((s16)(rd16(q->seg, (u16)(q->off + 4 * i)) - ref));
        if (best < 0 || d < best) { best = d; ofs = rd16(q->seg, (u16)(q->off + 4 * i + 2)); }
    }
    *ok = true;
    return ofs;
}

/* PS 4818:063d / AP 2e33:0508 — draw frame `frame` of the BMD at p (= header + 1Ah) */
void bm_draw_frame(FarPtr p, s16 frame)
{
    u8 *K = cs_blk();
    DSW(DS_BMD_EXT0) = rd16(p.seg, p.off);
    DSW(DS_BMD_EXT1) = rd16(p.seg, (u16)(p.off + 2));
    FarPtr D = far_add(p, 4);
    FarPtr q = far_add(D, 0x12);
    s16 nhot = (s16)rd16(q.seg, q.off);
    q = far_add(q, 2);
    ds_far_wr(DS_BM_HOT, q);
    q = far_add(q, (u16)(nhot * 2));
    bool ok;
    u16 ofs = bm_pick(&q, DSS(DS_BM_ELEV), &ok);
    if (!ok) return;
    q = far_add(D, ofs);
    ofs = bm_pick(&q, DSS(DS_BM_AZIM), &ok);
    if (!ok) return;
    q = far_add(D, ofs);
    s16 nf = (s16)rd16(q.seg, q.off);
    q = far_add(q, 2);
    if (!nf) return;
    while (frame >= nf) frame = (s16)(frame - nf);
    s16 nitems = 0;
    FarPtr items = q;
    for (; frame >= 0; frame--) {                              /* the last pass reads the chosen frame */
        nitems = (s16)rd16(q.seg, q.off);
        q = far_add(q, 2);
        items = q;
        q = far_add(q, (u16)(nitems * 9));
    }
    if (r3d_is_ps()) {
        for (; nitems; nitems--) {
            if (DSW(0x3B0C)) bm_draw_item(items, 0, 0, 0, 0, 0, 0);
            else bm_draw_item_flat(items);
            items = far_add(items, 9);
        }
        return;
    }
    s16 s = ldiv_s32(lxlsh((s16)KW(CS_APP_SIZE), 8), 0x7F), sy = s, c = 0;   /* c: stack garbage when ext0 = 0 */
    if (DSW(DS_BMD_EXT0)) {
        s16 a = r3d_acos16(ldiv_s32(lxlsh(DSS(DS_BMD_EXT1), 14), 100));
        c = (s16)(r3d_cos((u16)ldiv_s32((s32)mul32(DSW(DS_BM_ELEV), (u16)a), 0x4000)) >> 6);
        sy = (s16)mul_shr8((u16)sy, (u16)c);
    }
    u16 S = KW(CS_PROJ_SHIFT);
    s16 ey = KS(CS_E_POS16 + 2);
    s16 x = (s16)(KW(CS_CX) + ldiv_s32(lxlsh(KS(CS_E_POS16), (u8)S), ey));
    s16 y = (s16)(KW(CS_CY) - ldiv_s32(lxlsh(KS(CS_E_POS16 + 4), (u8)S), ey));
    for (; nitems; nitems--) {
        bm_draw_item(items, s, sy, x, y, s, c);
        items = far_add(items, 9);
    }
}

/* PS 4818:0833 / AP 2e33:07f4 */
void bm_draw(u16 o)
{
    u8 *K = cs_blk();
    FarPtr hdr = shape_ptr((s16)DSW(o));
    if (far_is_null(hdr) || !(rd8(hdr.seg, hdr.off) & 0x10)) r3d_assert(0x23EC, 0x196, 0x3B3C, 0x1C4);
    KW(CS_SHAPE_PTR + 2) = hdr.seg;
    KW(CS_SHAPE_PTR) = hdr.off;
    KW(CS_OBJ_SHIFT) = 0;
    KSL(CS_RADIUS) = (s16)rd16(hdr.seg, (u16)(hdr.off + 0x0C));
    obj_set_position(o);
    if (r3d_is_ps() ? ts_visible_lod() : bm_visible()) return;
    obj_set_orientation();
    bm_view_angles();
    DSW(DS_BM_BASE) = rd16(hdr.seg, (u16)(hdr.off + 0x0A));
    u16 st = DSW(o + 0x14);
    bm_draw_frame(far_add(hdr, 0x1A), st ? DSB(st) : 0);
}

/* PS 4818:08fc / AP 2e33:08b8 */
s16 bm_is_bitmap(s16 id)
{
    FarPtr p = shape_ptr(id);
    return !far_is_null(p) && (rd8(p.seg, p.off) & 0x10) ? 1 : 0;
}
