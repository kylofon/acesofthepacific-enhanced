/* render3d — world loading and the effect pool: 05a9 (effect records), 333b / 33a1 (.FLT detail table
 * and its interpolators), 3368 (GID: ground data), 3591 (class arrays), 35a4 / 35d1 (ground helpers),
 * 35ad (obwload.c: WD3), 35d6 (shape attribute cache), 51ca (file helpers), 62ca (lists), 62d2 (the
 * area loader). render3d_world.md §4.1, §5.2, §6. */
#include "render3d/r3d_priv.h"
#include "codeptr.h"
#include "platform/platform.h"
#include "sim_flow/sim_flow.h"

/* ---- DGROUP globals (render3d_world.md §1.5, §4.1, §6) ------------------------------------------- */
enum {
    FX_CULLED = 0x55D8,         /* u8: records have been stepped since the last view update */
    FX_REF = 0x55D9,            /* i32[3]: re-cull reference (never updated after init) */
    FX_VIEW = 0x55E5,           /* i32[3]: view position */
    FX_POOL = 0x55F1,           /* pool header */
    G_SHIFT = 0x7109, G_LAYER = 0x7108,   /* u8: shape shift / layer while probing GID data */
    A_DATE = 0x4787,            /* mission date (days) */
    A_MIN = 0x0C34, A_MAX = 0x0C40,       /* world bounding box */
    A_GRID_SHIFT = 0x0C2E, A_GRID_HALF = 0x0C30, A_GRID_ORG = 0x0C4C,
    A_TGT_NAME = 0x0C58, A_WD3_EXT = 0x0C62, A_FLT_EXT = 0x0C66, A_MBL_TAG = 0x0C6A,
    A_TGT_N = 0x5122, A_TGT_POS = 0x5124, A_LINKS = 0x5126, A_GROUPS_N = 0x5128, A_GROUPS = 0x512A,
    A_LONG0 = 0x512E, A_SHORT0 = 0x5130, A_FLAT1 = 0x5132, A_ISLE0 = 0x5134, A_INST_N = 0x5136,
    A_INST = 0x5138, A_TMEM_PTRS = 0x513C, A_TMEM_N = 0x5140, A_TMEM_OWN = 0x5142, A_TMEM = 0x5146,
    FLT_RB = 0x365C, GID_TAG = 0x3660, WD2_OB_TAG = 0x36ED, WD2_WHD_TAG = 0x36E4, WD2_MBL_TAG = 0x36F6,
};
#define OBW_TAG  far_make(SEG(0x3B31), 0)            /* "obwload.c" */
#define INIT_TAG far_make(0x386B, 0x20)              /* the init.c tag as pushed (seg 386B, off 20h) */

/* ================================================================================================
 * Effect pool (AP 05a9, render3d_world.md §4.1)
 * ============================================================================================== */

/* AP 05a9:0005 */
void fx_pool_create(void)
{
    DSW(FX_POOL) = ap_pool_create(100, 0x30);
    DSL(FX_VIEW + 8) = DSL(FX_VIEW + 4) = DSL(FX_VIEW) = 0x22222222u;
    memcpy(DSP(FX_REF), DSP(FX_VIEW), 12);
    DSB(FX_CULLED) = 0;
}
/* AP 05a9:0043 */
void fx_pool_free(void) { pool_destroy(DSW(FX_POOL)); DSW(FX_POOL) = 0; }

/* AP 05a9:025f — 1 when the record is within its cull range (+1Ch << 10) of the view */
u16 fx_in_range(FarPtr e)
{
    u16 m = stk_mark(), p = stk_alloc(12);
    memcpy(DSP(p), far_mp(far_add(e, 4)), 12);
    u32 r = (u32)rd16(e.seg, (u16)(e.off + 0x1C)) << 10;
    u16 res = 0;
    if (ldist2(p, FX_VIEW) < r && ldist3(p, FX_VIEW) < r) res = 1;
    stk_release(m);
    return res;
}

/* AP 05a9:005a — copy the 48-byte template into a new record (0 when out of range or full) */
u32 fx_spawn(u16 tmpl)
{
    if (!fx_in_range(ds_ptr(tmpl))) return 0;
    u32 e = ap_pool_alloc_3546(DSW(FX_POOL));
    if (e) {
        FarPtr p = { (u16)e, (u16)(e >> 16) };
        memcpy(far_mp(p), DSP(tmpl), 0x30);
        wr8(p.seg, (u16)(p.off + 0x1F), 0);
    }
    return e;
}

/* AP 05a9:0233 — free the listed records */
void fx_release_list(u16 list, s16 n)
{
    while (n--) {
        u32 p = DSL(list); list += 4;
        ap_pool_free_3546(DSW(FX_POOL), (u16)p, (u16)(p >> 16));
    }
}

/* AP 05a9:01d1 — clear 2000h; out of range → flags = 100h (the whole word) */
void fx_recull(void)
{
    u32 e = 0;
    while ((e = pool_next(DSW(FX_POOL), far_make((u16)(e >> 16), (u16)e))) != 0) {
        FarPtr p = { (u16)e, (u16)(e >> 16) };
        wr16(p.seg, p.off, rd16(p.seg, p.off) & 0xDFFF);
        if (!fx_in_range(p)) wr16(p.seg, p.off, 0x100);
    }
    /* the original then frees a list of 0 records */
}

/* AP 05a9:00ad — new view position; re-cull (every frame: the reference is never updated); free the
 * records not updated since the last step */
void fx_update_ref(u16 eye)
{
    memcpy(DSP(FX_VIEW), DSP(eye), 12);
    if (ldist3(FX_VIEW, FX_REF) > 3000) fx_recull();
    if (!DSB(FX_CULLED)) {
        u16 m = stk_mark(), list = stk_alloc(0x194);
        s16 n = 0;
        u32 e = 0;
        while ((e = pool_next(DSW(FX_POOL), far_make((u16)(e >> 16), (u16)e))) != 0)
            if (!(rd8((u16)(e >> 16), (u16)((u16)e + 0x1F)) & 1)) { DSL(list + 4 * n) = e; n++; }
        fx_release_list(list, n);
        stk_release(m);
    }
    DSB(FX_CULLED) = 0;
}

typedef u16 (*FxCallback)(FarPtr e);
/* AP 05a9:014c — per sim step: call each record's update callback (z >= 0); free the others */
void fx_run(void)
{
    u16 m = stk_mark(), list = stk_alloc(0x194);
    s16 n = 0;
    u32 e = 0;
    while ((e = pool_next(DSW(FX_POOL), far_make((u16)(e >> 16), (u16)e))) != 0) {
        FarPtr p = { (u16)e, (u16)(e >> 16) };
        FarPtr cb = far_rd(p.seg, (u16)(p.off + 0x18));
        if (!far_is_null(cb) && (s32)rd32(p.seg, (u16)(p.off + 0x0C)) >= 0 && ((FxCallback)codeptr_lookup(cb))(p))
            wr8(p.seg, (u16)(p.off + 0x1F), rd8(p.seg, (u16)(p.off + 0x1F)) | 1);
        else { DSL(list + 4 * n) = e; n++; }
    }
    fx_release_list(list, n);
    stk_release(m);
    DSB(FX_CULLED) = 1;
}

/* ================================================================================================
 * Interpolators (AP 33a1) and the .FLT detail table (AP 333b), render3d_world.md §6.4
 * 14-byte record: +0 x0, +2 x1, +4 x, +6 i32 y (16.16), +0A i32 step
 * ============================================================================================== */

/* AP 33a1:00b3 — step = (step − y) / dx, start fraction quirk */
u16 lerp_step_init(u16 y, s16 dx)
{
    if (dx <= 0) { DSL(y + 4) = 0; DSW(y) = 0; return 0; }
    DSW(y) = 0;
    DSW(y + 4) = 0;
    DSSL(y + 4) = (s32)(DSL(y + 4) - DSL(y));
    DSSL(y + 4) = DSSL(y + 4) / (s32)dx;
    bool neg = DSSL(y + 4) < 0;
    if (neg) DSSL(y + 4) = -DSSL(y + 4);
    DSW(y) = (DSL(y + 4) & 0xFFFF8000u) ? DSW(y + 4) : 0x8000;
    if (neg) DSSL(y + 4) = -DSSL(y + 4);
    return 1;
}
/* AP 33a1:0008 — clamp x to [x0, x1], step there, return the integer part */
u16 lerp_eval(u16 s, u16 x0)
{
    s16 x = (s16)x0;
    if (DSS(s + 2) < x) x = DSS(s + 2);
    else if (DSS(s) > x) x = DSS(s);
    while (DSS(s + 4) < x) { DSL(s + 6) += DSL(s + 0x0A); DSW(s + 4)++; }
    while (DSS(s + 4) > x) { DSL(s + 6) -= DSL(s + 0x0A); DSW(s + 4)--; }
    return DSW(s + 8);
}
/* AP 33a1:0055 — (xa, ya) → (xb, yb); only the high words of y and step are set before the slope */
void lerp_setup(u16 s, u16 xa, u16 ya, u16 xb, u16 yb)
{
    s16 orig = (s16)xa;
    if ((s16)xa > (s16)xb) { u16 t = xa; xa = xb; xb = t; t = ya; ya = yb; yb = t; }
    DSW(s) = xa; DSW(s + 4) = xa; DSW(s + 2) = xb;
    DSW(s + 8) = ya;
    DSW(s + 0x0C) = yb;
    lerp_step_init((u16)(s + 6), (s16)(xb - xa));
    lerp_eval(s, (u16)orig);
}
/* AP 333b:024c / 0290 — the same on a 14-byte copy (copied in and out) */
void lerp_setup_copy(u16 s, u16 xa, u16 ya, u16 xb, u16 yb)
{
    u16 m = stk_mark(), t = stk_alloc(14);
    memcpy(DSP(t), DSP(s), 14);
    lerp_setup(t, xa, ya, xb, yb);
    memcpy(DSP(s), DSP(t), 14);
    stk_release(m);
}
u16 lerp_eval_copy(u16 s, u16 x)
{
    u16 m = stk_mark(), t = stk_alloc(14);
    memcpy(DSP(t), DSP(s), 14);
    u16 r = lerp_eval(t, x);
    memcpy(DSP(s), DSP(t), 14);
    stk_release(m);
    return r;
}

/* AP 333b:0098 — one 24-byte FLT record into its class entry */
void flt_record(u16 r)
{
    u16 e = (u16)(DSW(W_FLT_TAB) + 0x55 * DSW(r));
    DSB(e) = 1;
    DSW(e + 1) = DSW(r + 2);
    DSB(e + 3) = DSB(r + 4);
    if (DSB(e + 3)) lerp_setup_copy((u16)(e + 0x0F), 0, DSW(r + 6), 0xFF, DSW(r + 8));
    else {
        lerp_setup_copy((u16)(e + 0x1D), 0, DSW(r + 0x0A), 0xFF, DSW(r + 0x0C));
        lerp_setup_copy((u16)(e + 0x2B), 0, DSW(r + 0x0E), 0xFF, DSW(r + 0x10));
        lerp_setup_copy((u16)(e + 0x39), 0, DSW(r + 0x12), 0xFF, DSW(r + 0x14));
    }
    lerp_setup_copy((u16)(e + 0x47), 0, DSW(r + 0x16), 0xFF, 0x3FF);
}
/* AP 333b:000a — the area's .FLT file (missing file: nothing, not even the global interpolator) */
void flt_load(u16 name)
{
    u16 f = my_fopen(name, FLT_RB);
    if (!f) return;
    s32 len = my_filelength(f);
    s16 n = ldiv_sat(len, 0x18);
    u16 m = stk_mark(), r = stk_alloc(0x18);
    for (s16 i = 0; i < n; i++) { my_fread(r, 0x18, 1, f); flt_record(r); }
    stk_release(m);
    my_fclose(f);
    lerp_setup_copy(0x70FA, 0, 0, 0x80, 0x100);
}
/* AP 333b:0143 — class present */
u8 flt_valid(s16 c) { return DSB((u16)(DSW(W_FLT_TAB) + 0x55 * c)); }
/* AP 333b:0158 — detail level 0..255 */
void flt_set_detail(u16 lv)
{
    s16 a = clamp((s16)lv, 0, 0xFF);
    s16 b = clamp((s16)((u16)(lv - 0x80) << 1), 0, 0xFF);
    u16 e = DSW(W_FLT_TAB);
    for (s16 c = 0; c < (s16)DSW(W_FLT_N); c++, e = (u16)(e + 0x55)) {
        if (!flt_valid(c)) continue;
        DSB(e + 4) = (DSS(e + 1) > a) ? 1 : 0;
        if (DSB(e + 3)) DSW(e + 5) = lerp_eval_copy((u16)(e + 0x0F), (u16)a);
        else {
            DSW(e + 7) = lerp_eval_copy((u16)(e + 0x1D), (u16)a);
            DSW(e + 9) = lerp_eval_copy((u16)(e + 0x2B), (u16)a);
            DSW(e + 0x0B) = lerp_eval_copy((u16)(e + 0x39), (u16)a);
        }
        DSW(e + 0x0D) = lerp_eval_copy((u16)(e + 0x47), (u16)b);
    }
}

/* ================================================================================================
 * GID: ground data (AP 3368, 35a4, 35d1), render3d_world.md §6.6
 * ============================================================================================== */

/* AP 3368:0001 — "GID:" chunk, pointer list relocated by the chunk address */
void gid_load(u16 name)
{
    FarPtr g = ts_load_chunk(name, GID_TAG);
    ds_far_wr(W_GID_BASE, g);
    FarPtr q = g;
    DSW(W_GID_N) = 0;
    while (rd32(q.seg, q.off)) {
        wr32(q.seg, q.off, ((u32)g.seg << 16 | g.off) + rd32(q.seg, q.off));   /* 32-bit add of the dwords */
        DSW(W_GID_N)++;
        q.off = (u16)(q.off + 4);
    }
}
/* AP 3368:0057 */
void gid_free(void) { mem_far_delete(ds_far(W_GID_BASE)); }

/* AP 35d1:000e — (x, y) inside all edges (outward normals) of the region */
u16 point_in_region(FarPtr region, s16 x, s16 y)
{
    s16 cx = (s8)rd8(region.seg, (u16)(region.off + 2));
    if (!cx) return 0;
    u16 si = rd16(region.seg, region.off);
    u16 n = (u16)cx;
    do {
        s32 v = (s32)(s8)rd8(region.seg, (u16)(si + 1)) * (s16)(y - (s16)rd16(region.seg, (u16)(si + 4)));
        v += (s32)(s8)rd8(region.seg, si) * (s16)(x - (s16)rd16(region.seg, (u16)(si + 2)));
        if (v > 0) return 0;
        si = (u16)(si + 6);
    } while (--n);
    return 1;
}

/* AP 35a4:000f — the object's angles by placement kind (other kinds: out not written) */
void obj_angles(FarPtr o, u16 out)
{
    switch (rd16(o.seg, o.off) & 0x0E00) {
    case 0x800: case 0xC00: memcpy(DSP(out), far_mp(far_add(o, 0x10)), 6); break;
    case 0x200: DSW(out + 2) = 0; DSW(out) = 0; DSW(out + 4) = rd16(o.seg, (u16)(o.off + 0x10)); break;
    case 0x600: DSW(out + 2) = 0; DSW(out) = 0; DSW(out + 4) = rd16(o.seg, (u16)(o.off + 0x0C)); break;
    default: break;
    }
}

/* AP 3368:0128 — region pointer by index (no callers) */
u16 gid_region(FarPtr entry, s16 i)
{
    u16 stride = (rd8(entry.seg, (u16)(entry.off + 4)) & 2) ? 10 : 6;
    return (u16)(rd16(entry.seg, (u16)(entry.off + 6)) + i * stride);
}

/* AP 3368:02f4 — height of a sloped region at (x, y) */
s16 gid_slope_height(FarPtr region, s16 x, s16 y)
{
    if (far_is_null(ds_far(W_GID_BASE))) return 0;
    FarPtr pl = { rd16(region.seg, (u16)(region.off + 6)), region.seg };
    s32 v = 0;
    s16 gx = (s8)rd8(pl.seg, pl.off);
    if (gx) v = (s32)gx * (s16)(rd16(pl.seg, (u16)(pl.off + 2)) - (u16)x);
    s16 gy = (s8)rd8(pl.seg, (u16)(pl.off + 1));
    if (gy) v += (s32)gy * (s16)(rd16(pl.seg, (u16)(pl.off + 4)) - (u16)y);
    s16 cx = v ? (s16)((s32)((u32)v * rd8(region.seg, (u16)(region.off + 3))) >> 12) : 0;
    return (s16)(rd16(region.seg, (u16)(region.off + 4)) + cx);
}

/* AP 3368:0169 — ground under p for one object (out: +0 far object, +4 i32 z, +8 far region) */
u8 ground_query_obj(u16 out, u16 p, FarPtr entry, FarPtr o)
{
    u8 sh = DSB(G_SHIFT);
    s32 dx = (s32)(DSL(p) - rd32(o.seg, (u16)(o.off + 4))) >> (sh & 31);
    u16 m = stk_mark(), d = stk_alloc(4), ang = stk_alloc(6);
    u8 r = 0;
    DSW(d) = (u16)dx;
    if ((s32)(s16)rd16(entry.seg, entry.off) < (dx < 0 ? -dx : dx)) goto done;
    s32 dy = (s32)(DSL(p + 4) - rd32(o.seg, (u16)(o.off + 8))) >> (sh & 31);
    DSW(d + 2) = (u16)dy;
    if ((s32)(s16)rd16(entry.seg, (u16)(entry.off + 2)) < (dy < 0 ? -dy : dy)) goto done;
    /* PORT: the angles of other placement kinds are the stack's previous contents in the original */
    memset(DSP(ang), 0, 6);
    obj_angles(o, ang);
    if (DSW(ang + 4)) vrot_z(d, (u16)-DSW(ang + 4));
    {
        s16 n = rd8(entry.seg, (u16)(entry.off + 5));
        FarPtr reg = { rd16(entry.seg, (u16)(entry.off + 6)), entry.seg };
        u16 stride = (rd8(entry.seg, (u16)(entry.off + 4)) & 2) ? 10 : 6;
        if (!(rd8(entry.seg, (u16)(entry.off + 4)) & 1)) {
            for (;;) {
                s16 c = n; n--;
                if (!c) break;
                if (point_in_region(reg, DSS(d), DSS(d + 2))) break;
                reg.off = (u16)(reg.off + stride);
            }
            if (n == -1) goto done;
        }
        far_wr(cur_dgroup, (u16)(out + 8), reg);
        far_wr(cur_dgroup, out, o);
        if (DSB(G_LAYER)) DSL(out + 4) = 0;
        else {
            s32 h = (rd8(entry.seg, (u16)(entry.off + 4)) & 2) ? gid_slope_height(reg, DSS(d), DSS(d + 2))
                                                             : (s16)rd16(reg.seg, (u16)(reg.off + 4));
            DSL(out + 4) = ((u32)h << (sh & 31)) + rd32(o.seg, (u16)(o.off + 0x0C));
        }
        r = 1;
    }
done:
    stk_release(m);
    return r;
}

/* AP 3368:0069 — first object of objs[0..n) with ground data under p */
u16 ground_query(u16 out, u32 p, u32 objs, u16 n)
{
    u16 m = stk_mark(), lp = stk_alloc(12);
    memcpy(DSP(lp), far_mp(far_make((u16)(p >> 16), (u16)p)), 12);
    u8 r = 0;
    if (!far_is_null(ds_far(W_GID_BASE))) {
        memset(DSP(out), 0, 12);
        FarPtr q = { (u16)objs, (u16)(objs >> 16) };
        for (s16 i = 0; i < (s16)n; i++, q.off = (u16)(q.off + 4)) {
            FarPtr o = far_rd(q.seg, q.off);
            u16 id = rd16(o.seg, (u16)(o.off + 2)) & 0x1FFF;
            FarPtr g = ds_far(W_GID_BASE);
            FarPtr entry = far_rd(g.seg, (u16)(g.off + 4 * id));
            if (!rd8(entry.seg, (u16)(entry.off + 5))) continue;
            DSB(G_SHIFT) = (u8)shape_shift_cached(id);
            DSB(G_LAYER) = (u8)shape_layer_cached(id);
            if (ground_query_obj(out, lp, entry, o)) { r = 1; break; }
        }
    }
    stk_release(m);
    return r;
}

/* ================================================================================================
 * Class arrays (AP 3591)
 * ============================================================================================== */

static u16 init_tag(void) { return far_to_near_str(INIT_TAG); }

/* AP 3591:0001 — 85-byte class records and the range array */
void ob_init(u16 unused, u16 n)
{
    (void)unused;
    if (n) {
        DSW(W_FLT_TAB) = mem_new((u16)(n * 0x55), 0, init_tag(), 0x21);
        if (!DSW(W_FLT_TAB)) assert_fail(INIT_TAG, 0x22, 0);
        memset(DSP(DSW(W_FLT_TAB)), 0, (u16)(n * 0x55));
        DSW(W_FLT_RANGE) = mem_new((u16)(n * 4), 0, init_tag(), 0x26);
        if (!DSW(W_FLT_RANGE)) assert_fail(INIT_TAG, 0x27, 0);
        memset(DSP(DSW(W_FLT_RANGE)), 0, (u16)(n * 4));
    }
    DSW(W_FLT_N) = n;
}
/* AP 3591:00dc */
void ob_done(void)
{
    if (DSW(W_FLT_TAB)) { mem_delete(DSW(W_FLT_TAB), 0, init_tag(), 0x33); DSW(W_FLT_TAB) = 0; }
    if (DSW(W_FLT_RANGE)) { mem_delete(DSW(W_FLT_RANGE), 0, init_tag(), 0x39); DSW(W_FLT_RANGE) = 0; }
    DSW(W_FLT_N) = 0;
}

/* ================================================================================================
 * obwload.c (AP 35ad)
 * ============================================================================================== */

static u16 obw_tag(void) { return far_to_near_str(OBW_TAG); }

/* AP 35ad:000c — the 256-word WD2:WHD: count table (loaded once) */
u16 wd3_open(u16 name)
{
    if (DSW(W_WHD_TAB)) return DSW(W_WHD_TAB);
    u16 buf = mem_new(0x200, 0, obw_tag(), 0x29);
    u16 f = chunk_open(name);
    if (!f) assert_fail(OBW_TAG, 0x2E, name);
    chunk_find(f, WD2_WHD_TAG, 0);
    u16 m = stk_mark(), k = stk_alloc(2);
    my_fread(k, 1, 1, f);
    my_fseek(f, (s32)(s16)((s16)(s8)DSB(k) << 1), 1);
    stk_release(m);
    my_fread(buf, 2, 0x100, f);
    chunk_close(f);
    DSW(W_WHD_TAB) = buf;
    return buf;
}
/* AP 35ad:00ce */
void wd3_close(void) { mem_delete(DSW(W_WHD_TAB), 0, obw_tag(), 0x3F); DSW(W_WHD_TAB) = 0; }

typedef u16 (*Wd3Handler)(u16 rec);
/* AP 35ad:00f8 — WD2:OB0..OB5: (26-byte records), then WD2:MBL: (16 + 6 or 24 bytes) through DS:7110 */
void wd3_read_objects(u16 name)
{
    u16 f = chunk_open(name);
    if (!f) assert_fail(OBW_TAG, 0x5B, name);
    Wd3Handler h = (Wd3Handler)codeptr_lookup(ds_far(W_WD3_HANDLER));
    u16 m = stk_mark(), rec = stk_alloc(0x28), n = stk_alloc(2);
    for (s16 d = 0; d < 6; d++) {
        DSB(WD2_OB_TAG + 6) = (u8)('0' + d);
        if (chunk_find(f, WD2_OB_TAG, 0) == -1) continue;
        s16 k = (s16)(chunk_size(f) / 0x1A);
        for (;;) {
            s16 c = k; k--;
            if (!c) break;
            if (!my_fread(rec, 0x1A, 1, f)) break;
            h(rec);
        }
    }
    if (chunk_find(f, WD2_MBL_TAG, 0) != -1) {
        my_fread(n, 2, 1, f);
        for (;;) {
            u16 c = DSW(n); DSW(n)--;
            if (!c) break;
            my_fread(rec, 0x10, 1, f);
            s16 extra = ((DSW(rec + 2) & 0xC000) == 0xC000) ? 0x18 : 6;
            my_fread((u16)(rec + 0x10), extra, 1, f);
            h(rec);
        }
    }
    stk_release(m);
    chunk_close(f);
}

/* ================================================================================================
 * Shape attribute cache (AP 35d6)
 * ============================================================================================== */

static FarPtr attr(u16 w) { return ds_far(w); }

/* AP 35d6:000c — refresh the four caches; 1 if any value changed */
u16 shape_attr_refresh(void)
{
    u16 changed = 0;
    for (s16 id = 0; id < (s16)DSW(AP_DS_shape_total); id++) {
        FarPtr c = attr(W_ATTR_CLASS), l = attr(W_ATTR_LAYER), s = attr(W_ATTR_SHIFT), r = attr(W_ATTR_RADIUS);
        u8 v = shape_class(id);
        if (v != rd8(c.seg, (u16)(c.off + id))) { wr8(c.seg, (u16)(c.off + id), v); changed = 1; }
        v = shape_subclass(id);
        if (v != rd8(l.seg, (u16)(l.off + id))) { wr8(l.seg, (u16)(l.off + id), v); changed = 1; }
        v = shape_shift(id);
        if (v != rd8(s.seg, (u16)(s.off + id))) { wr8(s.seg, (u16)(s.off + id), v); changed = 1; }
        u16 w = (u16)shape_radius(id);
        if (w != rd16(r.seg, (u16)(r.off + 2 * id))) { wr16(r.seg, (u16)(r.off + 2 * id), w); changed = 1; }
    }
    DSB(W_ATTR_VALID) = 1;
    return changed;
}
/* AP 35d6:00b8 */
void shape_attr_build(void)
{
    ds_far_wr(W_ATTR_CLASS, mem_far_new(DSW(AP_DS_shape_total), 0));
    ds_far_wr(W_ATTR_LAYER, mem_far_new(DSW(AP_DS_shape_total), 0));
    ds_far_wr(W_ATTR_SHIFT, mem_far_new(DSW(AP_DS_shape_total), 0));
    ds_far_wr(W_ATTR_RADIUS, mem_far_new((u16)(DSW(AP_DS_shape_total) << 1), 0));
    shape_attr_refresh();
}
/* AP 35d6:0128 */
void shape_attr_free(void)
{
    mem_far_delete(attr(W_ATTR_CLASS)); mem_far_delete(attr(W_ATTR_LAYER));
    mem_far_delete(attr(W_ATTR_SHIFT)); mem_far_delete(attr(W_ATTR_RADIUS));
    FarPtr z = { 0, 0 };
    ds_far_wr(W_ATTR_CLASS, z); ds_far_wr(W_ATTR_LAYER, z); ds_far_wr(W_ATTR_SHIFT, z); ds_far_wr(W_ATTR_RADIUS, z);
    DSB(W_ATTR_VALID) = 0;
}
/* AP 35d6:018a / 01b8 / 01e2 / 020c — AX = id */
u16 shape_radius_cached(u16 ax)
{
    if (!DSB(W_ATTR_VALID)) return (u16)shape_radius((s16)ax);
    FarPtr r = attr(W_ATTR_RADIUS);
    return rd16(r.seg, (u16)(r.off + 2 * ax));
}
u16 shape_shift_cached(u16 ax)
{
    if (!DSB(W_ATTR_VALID)) return shape_shift((s16)ax);
    FarPtr s = attr(W_ATTR_SHIFT);
    return rd8(s.seg, (u16)(s.off + ax));
}
u16 shape_layer_cached(u16 ax)
{
    if (!DSB(W_ATTR_VALID)) return shape_subclass((s16)ax);
    FarPtr l = attr(W_ATTR_LAYER);
    return rd8(l.seg, (u16)(l.off + ax));
}
u16 shape_class_cached(u16 ax)
{
    if (!DSB(W_ATTR_VALID)) return shape_class((s16)ax);
    FarPtr c = attr(W_ATTR_CLASS);
    return rd8(c.seg, (u16)(c.off + ax));
}
/* AP 35d6:0236 — AX = id, DL = class */
void shape_set_class_cached(u16 ax, u8 dl)
{
    shape_set_class((s16)ax, dl);
    if (DSB(W_ATTR_VALID)) { FarPtr c = attr(W_ATTR_CLASS); wr8(c.seg, (u16)(c.off + ax), dl); }
}
/* AP 35d6:0267 — AX = id, DX = radius */
void shape_set_radius_cached(u16 ax, u16 dx)
{
    shape_set_radius((s16)ax, (s16)dx);
    if (DSB(W_ATTR_VALID)) { FarPtr r = attr(W_ATTR_RADIUS); wr16(r.seg, (u16)(r.off + 2 * ax), dx); }
}

/* ================================================================================================
 * File helpers (AP 51ca) on a volume FILE*
 * ============================================================================================== */

/* AP 51ca:0000 — getc | getc << 8 (no EOF check) */
u16 fread_word(u16 f) { u16 a = (u16)my_fgetc(f); return (u16)(a + ((u16)my_fgetc(f) << 8)); }
/* AP 51ca:0027 — 4 × getc, little endian (sign-extended ints added) */
u32 fread_long(u16 f)
{
    s32 v = (s32)my_fgetc(f);
    v += (s32)((u32)(s32)my_fgetc(f) << 8);
    v += (s32)((u32)(s32)my_fgetc(f) << 16);
    v += (s32)((u32)(s32)my_fgetc(f) << 24);
    return (u32)v;
}
/* AP 51ca:008d — string into DS:4192 (256 bytes, no length check); 0 at EOF or empty */
u16 fread_str(u16 f)
{
    DSW(0x4292) = 0x4192;
    s16 c;
    do {
        c = my_fgetc(f);
        if (c == -1) break;
        u16 p = DSW(0x4292); DSW(0x4292)++;
        DSB(p) = (u8)c;
    } while ((u8)c);
    if (c == -1 || DSB(0x4192) == 0) return 0;
    return 0x4192;
}
/* AP 51ca:00ce / 010c / 0180 — writers (no callers): 1 ok, 0 on error */
u16 fwrite_word(u16 w, u16 f)
{
    if (my_fputc((s16)(w & 0xFF), f) == -1) return 0;
    if (my_fputc((s16)(w >> 8), f) == -1) return 0;
    return 1;
}
u16 fwrite_long(u32 v, u16 f)
{
    for (int i = 0; i < 4; i++) if (my_fputc((s16)((v >> (8 * i)) & 0xFF), f) == -1) return 0;
    return 1;
}
u16 fwrite_str(u16 s, u16 f)
{
    if (s) while (DSB(s)) { if (my_fputc((s16)(s8)DSB(s), f) == -1) break; s++; }
    return my_fputc(0, f) != -1;
}

/* ================================================================================================
 * Lists (AP 62ca)
 * ============================================================================================== */

/* AP 62ca:0000 — insert by ascending u16 at +2, after equal keys */
u16 sorted_insert(u16 head, u16 node)
{
    if (!head || DSW(node + 2) < DSW(head + 2)) { DSW(node) = head; return node; }
    u16 si = head, key = DSW(node + 2);
    while (DSW(si) && key >= DSW(DSW(si) + 2)) si = DSW(si);
    DSW(node) = DSW(si);
    DSW(si) = node;
    return head;
}
/* AP 62ca:004f — append (next = 0 set only for an empty list) */
u16 sorted_insert2(u16 head, u16 node)
{
    if (!head) { DSW(node) = 0; return node; }
    u16 si = head;
    while (DSW(si)) si = DSW(si);
    DSW(si) = node;
    return head;
}

/* ================================================================================================
 * The area loader (AP 62d2), render3d_world.md §6.1, §6.3
 * ============================================================================================== */

/* AP 62d2:0000 — number of halvings before 0 (a negative value looped forever in the original) */
u16 bitlen_long(s32 v)
{
    u16 n = 0;
    for (;;) { v >>= 1; if (v == 0 || v == -1) break; n++; }   /* PORT: stop at −1 (no negative input) */
    return n;
}

/* AP 62d2:001d — MBL group record (42 bytes) or 0 for a destroyed short group */
u32 group_record(FarPtr dst, u16 r)
{
    u8 *d = far_mp(dst);
    if ((DSW(r + 2) & 0xC000) == 0xC000) {
        p16(d, 0, 0x1001);
        p16(d, 2, DSW(r + 2));
        memcpy(d + 4, DSP(r + 4), 12);
        memcpy(d + 0x10, DSP(r + 0x10), 12);
        memcpy(d + 0x1C, DSP(r + 0x1C), 12);
        return (u32)dst.seg << 16 | dst.off;
    }
    s16 k = (s16)target_at_date((u16)(r + 4), DSW(A_DATE));
    u16 f;
    switch ((u16)(k + 1)) {
    case 0: f = 0; break;
    case 1: f = 0x10; break;
    case 2: case 3: case 4: case 5: case 6: f = 2; break;
    case 11: return 0;
    default: f = 0x11; break;
    }
    f |= 0x1200;
    p16(d, 0, f);
    p16(d, 2, DSW(r + 2));
    memcpy(d + 4, DSP(r + 4), 12);
    p16(d, 0x10, DSW(r + 0x10));
    memset(d + 0x12, 0, 24);
    return (u32)dst.seg << 16 | dst.off;
}

/* AP 62d2:0133 — WD3 static instance (24 bytes) */
u32 instance_build(FarPtr dst, u16 r)
{
    u8 *d = far_mp(dst);
    u16 fl = 0x801;
    if (!(DSW(r + 0x14) | DSW(r + 0x10))) fl |= 0x4000;
    p16(d, 0, fl);
    p16(d, 2, DSW(r + 2));
    memcpy(d + 4, DSP(r + 4), 12);
    p16(d, 0x10, DSW(r + 0x10)); p16(d, 0x12, DSW(r + 0x12)); p16(d, 0x14, DSW(r + 0x14)); p16(d, 0x16, 0);
    return (u32)dst.seg << 16 | dst.off;
}

/* the bounding box update of 62d2:0199: pos ± (i16)radius_w */
static void bbox_add(u16 pos, u16 shape, u16 tmp)
{
    s32 R = shape_radius_world((s16)shape);
    ladd_scalar16(pos, (u16)-(u16)R, tmp);
    lmin(A_MIN, tmp);
    ladd_scalar16(pos, (u16)R, tmp);
    lmax(A_MAX, tmp);
}

/* AP 62d2:0199 — the DS:7110 handler for each WD3 record (render3d_world.md §6.3) */
u16 object_record_handler(u16 r)
{
    u16 m = stk_mark(), pos = stk_alloc(12), tmp = stk_alloc(12);
    u32 g;
    if (DSW(r + 2) & 0x8000) {
        FarPtr base = ds_far(A_GROUPS);
        u16 k = DSW(A_GROUPS_N); DSW(A_GROUPS_N)++;
        g = group_record(far_add(base, (u16)(k * 0x2A)), r);
        if (!g) { DSW(A_GROUPS_N)--; stk_release(m); return 0; }
        if ((DSW(r + 2) & 0xC000) == 0x8000) {
            bool link = false;
            s16 ti = 0;
            for (s16 si = 0; si < (s16)DSW(A_TGT_N); si++) {
                if (ldist3((u16)(r + 4), (u16)(DSW(A_TGT_POS) + 12 * si)) < 10) {
                    link = true; ti = si;
                    DSL(DSW(A_LINKS) + 6 * si) = g;
                    break;
                }
            }
            u16 n = macro_expand(far_make((u16)(g >> 16), (u16)g));
            s16 si;
            for (si = 0; si < (s16)n; si++) {
                FarPtr mb = far_make(SEG(0x3B05), (u16)(0x16 * si));
                u16 sh = rd16(mb.seg, (u16)(mb.off + 2));
                if (link && shape_layer_cached(sh) && shape_class_cached(sh)) {
                    DSW(DSW(A_LINKS) + 6 * ti + 4) = (u16)si;
                    link = false;
                }
                memcpy(DSP(pos), far_mp(far_add(mb, 4)), 12);
                bbox_add(pos, sh, tmp);
            }
            if (link && si == (s16)n) DSW(DSW(A_LINKS) + 6 * ti + 4) = n;
        }
    } else {
        FarPtr base = ds_far(A_INST);
        u16 k = DSW(A_INST_N); DSW(A_INST_N)++;
        g = instance_build(far_add(base, (u16)(k * 0x18)), r);
        if ((s16)DSW(A_ISLE0) == -1 && (u8)shape_class_cached(DSW(r + 2)) == 1) DSW(A_ISLE0) = DSW(W_WORLD_N);
        if ((s16)DSW(A_FLAT1) == -1 && (u8)shape_layer_cached(DSW(r + 2)) == 0) DSW(A_FLAT1) = DSW(W_WORLD_N);
        bbox_add((u16)(r + 4), rd16((u16)(g >> 16), (u16)((u16)g + 2)), tmp);
    }
    FarPtr P = ds_far(W_STATIC_OBJS);
    wr32(P.seg, (u16)(P.off + 4 * DSW(W_WORLD_N)), g);
    DSW(W_WORLD_N)++;
    stk_release(m);
    return 1;
}

/* AP 62d2:044a — clear the per-member damage bytes of the short groups (mission start) */
void area_free(void)
{
    FarPtr P = ds_far(W_STATIC_OBJS);
    for (s16 i = (s16)DSW(A_SHORT0); i < (s16)DSW(A_LONG0); i++) {
        FarPtr g = far_rd(P.seg, (u16)(P.off + 4 * i));
        memset(far_mp(far_add(g, 0x12)), 0, 0x18);
    }
}

/* AP 62d2:0a2e — groups destroyed at the mission date (reads 16 + 6 bytes per record, any form) */
u16 area_read_groups(u16 name)
{
    u16 dead = 0;
    u16 f = chunk_open(name);
    u16 m = stk_mark(), n = stk_alloc(2), rec = stk_alloc(0x16);
    if (chunk_find(f, A_MBL_TAG, 0) != -1) {
        my_fread(n, 2, 1, f);
        for (;;) {
            u16 c = DSW(n); DSW(n)--;
            if (!c) break;
            my_fread(rec, 0x10, 1, f);
            my_fread((u16)(rec + 0x10), 6, 1, f);
            if (target_at_date((u16)(rec + 4), DSW(A_DATE)) == 10) dead++;
        }
    }
    stk_release(m);
    chunk_close(f);
    return dead;
}

/* AP 62d2:049e — load area `name` (render3d_world.md §6.1); returns DS:5146 (far) */
u32 area_load(u16 name, u16 tpos, u16 tcount, u16 n)
{
    u16 mk = stk_mark();
    u16 tn = stk_alloc(10), L = stk_alloc(0x3C), pos = stk_alloc(12), q = stk_alloc(12);
    memcpy(DSP(tn), DSP(A_TGT_NAME), 10);
    memset(DSP(tcount), 0, (u16)(n * 2));
    u16 wd3 = res_name(name, A_WD3_EXT);
    u16 W = wd3_open(wd3);
    s16 total = (s16)sum_i16(W, 0x100);
    s16 groups = (s16)(DSW(W + 0x1FC) + DSW(W + 0x1FE));
    s16 statics = (s16)(total - groups);
    DSW(A_SHORT0) = (u16)statics;
    DSW(A_LONG0) = (u16)(statics + DSW(W + 0x1FC));
    DSB(tn + 7) = DSB(name + 2);
    DSB(tn + 8) = DSB(name + 3) == '.' ? 0 : DSB(name + 3);
    target_load(tn);
    u16 dead = area_read_groups(wd3);
    groups = (s16)(groups - dead);
    total = (s16)(total - dead);
    DSW(A_LONG0) = (u16)(DSW(A_LONG0) - dead);
    ds_far_wr(A_INST, mem_far_new((u16)(statics * 0x18), 1));
    ds_far_wr(A_GROUPS, mem_far_new((u16)(groups * 0x2A), 1));
    ds_far_wr(W_STATIC_OBJS, mem_far_new((u16)(total << 2), 1));
    ds_far_wr(W_DRAW_LIST, mem_far_new(0x7D0, 1));
    DSW(W_LIST_N) = 0; DSW(W_WORLD_N) = 0; DSW(A_INST_N) = 0;
    DSW(A_FLAT1) = 0xFFFF; DSW(A_ISLE0) = 0xFFFF;
    DSL(A_MIN + 8) = DSL(A_MIN + 4) = DSL(A_MIN) = 0x7FFFFFFFu;
    DSL(A_MAX + 8) = DSL(A_MAX + 4) = DSL(A_MAX) = 0x80000000u;
    ds_far_wr(W_WD3_HANDLER, codeptr_far(0x3DE90034u));                /* stub of 62d2:0199 */
    DSW(A_LINKS) = L;
    DSW(A_TGT_POS) = tpos;
    DSW(A_TGT_N) = n;
    DSW(A_TMEM_N) = 0;
    memset(DSP(L), 0, 0x3C);
    wd3_read_objects(wd3);
    FarPtr P = ds_far(W_STATIC_OBJS);
    for (s16 i = (s16)DSW(A_SHORT0); i < (s16)DSW(A_LONG0); i++) {   /* inherit the nearest stated type */
        FarPtr o = far_rd(P.seg, (u16)(P.off + 4 * i));
        if (rd16(o.seg, o.off) & 0x1F) continue;
        memcpy(DSP(pos), far_mp(far_add(o, 4)), 12);
        s32 best = 0x7FFFFFFF;
        u16 t = 0;
        for (s16 j = (s16)DSW(A_SHORT0); j < (s16)DSW(A_LONG0); j++) {
            FarPtr g = far_rd(P.seg, (u16)(P.off + 4 * j));
            if (!(rd16(g.seg, g.off) & 0x1F)) continue;
            memcpy(DSP(q), far_mp(far_add(g, 4)), 12);
            s32 d = (s32)ldist2(pos, q);
            if (d < best) { best = d; t = rd16(g.seg, g.off) & 0x1F; }
        }
        wr16(o.seg, o.off, (u16)(rd16(o.seg, o.off) | t));
    }
    target_free();
    if ((s16)DSW(A_FLAT1) == -1) DSW(A_FLAT1) = DSW(A_SHORT0);
    u16 s = 0;
    for (u16 k = 0; k < n; k++) { DSW(tcount + 2 * k) = DSW(L + 6 * k + 4); s = (u16)(s + DSW(L + 6 * k + 4)); }
    FarPtr z = { 0, 0 };
    ds_far_wr(A_TMEM, z); ds_far_wr(A_TMEM_OWN, z); ds_far_wr(A_TMEM_PTRS, z);
    DSW(A_TMEM_N) = s;
    if (s) {
        FarPtr mem = mem_far_new((u16)(s * 0x16), 0);
        ds_far_wr(A_TMEM, mem);
        FarPtr ptrs = mem_far_new((u16)(s << 2), 0);
        ds_far_wr(A_TMEM_PTRS, ptrs);
        FarPtr own = mem_far_new((u16)(s * 6), 0);
        ds_far_wr(A_TMEM_OWN, own);
        for (u16 k = 0; k < n; k++) {
            if (!DSW(L + 6 * k + 4)) continue;
            FarPtr g = ds_far((u16)(L + 6 * k));
            u16 mcount = macro_expand(g);
            FarPtr src = far_make(SEG(0x3B05), 0);
            for (u16 j = 0; (s16)j < (s16)mcount; j++) {
                if (shape_layer_cached(rd16(src.seg, (u16)(src.off + 2)))) break;
                far_wr(ptrs.seg, ptrs.off, mem);
                ptrs.off = (u16)(ptrs.off + 4);
                memcpy(far_mp(mem), far_mp(src), 0x16);
                src.off = (u16)(src.off + 0x16);
                mem.off = (u16)(mem.off + 0x16);
                far_wr(own.seg, own.off, g);
                wr16(own.seg, (u16)(own.off + 4), j);
                own.off = (u16)(own.off + 6);
            }
        }
    }
    DSW(W_LIST_CAP) = 0xFA;
    DSW(W_LIST_RESERVE) = 0x64;
    wd3_close();
    memcpy(DSP(q), DSP(A_MAX), 12);
    lsub(q, A_MIN);
    s32 mx = DSSL(q) > DSSL(q + 4) ? DSSL(q) : DSSL(q + 4);
    DSW(A_GRID_SHIFT) = (u16)(bitlen_long(mx / 0x3E) + 1);
    DSL(A_GRID_HALF) = (u32)1 << ((u8)(DSB(A_GRID_SHIFT) - 1) & 31);
    memcpy(DSP(A_GRID_ORG), DSP(A_MIN), 12);
    u32 t = (u32)1 << (DSB(A_GRID_SHIFT) & 31);
    DSL(A_GRID_ORG) -= t;
    DSL(A_GRID_ORG + 4) -= t;
    flt_load(res_name(name, A_FLT_EXT));
    flt_set_detail(0x80);
    DSW(W_REGATHER_DIST) = 0x1D4C;
    stk_release(mk);
    FarPtr r = ds_far(A_TMEM);
    return (u32)r.seg << 16 | r.off;
}

/* AP 62d2:0969 — free the area arrays */
void area_free2(void)
{
    static const u16 far_vars[] = { A_INST, A_GROUPS, W_STATIC_OBJS, W_DRAW_LIST, A_TMEM_OWN, A_TMEM, A_TMEM_PTRS };
    for (unsigned i = 0; i < sizeof far_vars / sizeof far_vars[0]; i++) mem_far_delete(ds_far(far_vars[i]));
    FarPtr z = { 0, 0 };
    ds_far_wr(A_INST, z); ds_far_wr(A_GROUPS, z); ds_far_wr(W_STATIC_OBJS, z);
    ds_far_wr(W_DRAW_LIST, z); ds_far_wr(A_TMEM, z); ds_far_wr(A_TMEM_PTRS, z);
    DSW(W_LIST_N) = 0; DSW(W_WORLD_N) = 0; DSW(A_GROUPS_N) = 0; DSW(A_INST_N) = 0;
}

/* AP 62d2:0a0d — class arrays (20) and macro tables (31 macros, 500 members) */
void area_init(void)
{
    ob_init(0x2BC, 0x14);
    macros_alloc(0x1F, 0x1F4);
}

/* the DS:7110 handler (62d2:0199 through the stub 3de9:0034) */
__attribute__((constructor)) static void world_load_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x62D20199u, (CodeFn)object_record_handler);
}
