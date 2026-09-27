/* render3d — ts_draw.c (AP 2f0e / PS 4708): visibility, LOD, draw mode, BSP part sort and walk; the
 * sprite/disc/bitmap variants (AP 2edf, 70e8, 2ec2 / PS 46d0, 4915) and shadows (AP 2ecb / PS 48ac).
 * render3d_shapes.md §2, §3; video_ts.md §4.
 *
 * PORT: the part lists live in the caller's stack frame in the original (54 words in ts_draw, reused by
 * every recursion level); here a host array (nothing else reads it). The stack checks at the top of
 * part_sort (AP empty 2494:01df, PS 4960:0184) are not ported. */
#include "render3d/r3d_priv.h"
#include "codeptr.h"
#include "video/video.h"
#include "platform/platform.h"
#include "sim_world/sim_world.h"   /* ces_check (07fe:000a) */
#include "enhanced/enhanced.h"     /* ENH: instance poses */

/* ================================================================================================
 * Sprite helpers (AP 2edf / PS 46d0)
 * ============================================================================================== */

/* PS 46d0:0000 / AP 2edf:0007 — bitmap i over all sprite sets (0 if out of range) */
u16 bitmap_global(s16 i)
{
    if (i < 0) return 0;
    for (s16 si = 0; si < (s16)DSW(DS_NSETS); si++) {
        u16 set = (u16)(DSW(DS_SETS) + 4 * si);
        if ((s16)DSW(set + 2) > i) return DSW((u16)(DSW(set) + 2 * i));
        i = (s16)(i - (s16)DSW(set + 2));
    }
    return 0;
}

/* PS 46d0:005b / AP 2edf:005e — eye position of vertex idx of the current part (FFh = the origin) */
u16 vtx_eye16(u8 idx, u16 out)
{
    u8 *K = cs_blk();
    if (idx == 0xFF) { memcpy(DSP(out), K + CS_E_POS16, 6); return out; }
    u16 m = stk_mark();
    u16 v = stk_alloc(6), mat = stk_alloc(0x14);
    u16 seg = KW(CS_SHAPE_PTR + 2), o = (u16)(KW(CS_VTX_OFS) + idx * 6);
    for (int i = 0; i < 6; i += 2) DSW(v + i) = rd16(seg, (u16)(o + i));
    vsar(v, (u16)-KW(CS_VTX_SHIFT));
    memcpy(DSP(mat), K + CS_M_OBJ_EYE, 0x13);
    vec_rotate(v, mat, out);
    vadd_far(out, far_make(r3d_cs_seg(), r3d_cs_off(CS_E_POS16)));
    stk_release(m);
    return out;
}

/* the sprite of a variant: the table's set (+0A) or the global numbering */
static u16 sprite_of(s16 id)
{
    u16 set = DSW(DSW(DS_TABLES) + 12 * CSW(CS_SHAPE_TBL) + 0x0A);
    if (set == 0xFFFF) return bitmap_global(id);
    u16 rec = (u16)(DSW(DS_SETS) + 4 * set);
    if (id >= 0 && (s16)DSW(rec + 2) > id) return DSW((u16)(DSW(rec) + 2 * id));
    return 0;
}

static s16 ldiv_s32(s32 a, s32 b) { return (s16)(b ? a / b : (s32)div_unreachable("LDIV by zero")); }   /* LDIV@ */

/* PS 46d0:0117 / AP 2edf:0119 — variant type >= 2: a scaled bitmap at a vertex (unused by the data) */
void sprite_draw(FarPtr var)
{
    u16 bm = sprite_of((s16)rd16(var.seg, var.off));
    if (!bm) return;
    u16 m = stk_mark(), p = stk_alloc(6);
    vtx_eye16(rd8(var.seg, (u16)(var.off + 5)), p);
    s16 px = DSS(p), py = DSS(p + 2), pz = DSS(p + 4);
    stk_release(m);
    if ((s16)CSW(CS_NEAR_Z) > py) return;
    u8 sc = rd8(var.seg, (u16)(var.off + 4));
    s16 s = sc ? (s16)mul_shr8(CSW(CS_APP_SIZE), sc) : (s16)CSW(CS_APP_SIZE);
    s16 w = DSS(bm + 6), h = DSS(bm + 8);
    s16 mx = (s16)((w > h ? w : h) >> 1);
    if (!s || !mx) return;
    s16 f = ldiv_s32((s32)s << 10, mx);
    w = (s16)mul_shr10((u16)w, (u16)f);
    h = (s16)mul_shr10((u16)h, (u16)f);
    u16 S = CSW(CS_PROJ_SHIFT);
    s16 x = (s16)(CSW(CS_CX) + ldiv_s32((s32)((u32)(s32)px << (S & 31)), py));
    s16 y = (s16)(CSW(CS_CY) - ldiv_s32((s32)((u32)(s32)pz << (S & 31)), py));
    u16 fl = rd16(var.seg, (u16)(var.off + 2));
    DRV_CLIP_ON = 1;
    gfx_sprite_rotated_ind(bm, x, y, 0, w, h, (s16)((fl >> 8) & 0xFF), (s16)(fl & 0xFF));
}

/* PS 46d0:02fa / AP 2ec2:000d — the 4-point bitmap of polygon flag 10h (render3d_poly.md §2.3): AP only
 * calls the assert-like 07fe:000a (ces_check), PS maps the bitmap onto the quad (16e3:065d) */
void bitmap4_draw(u16 colours, u16 xs, u16 ys)
{
    u16 bm = sprite_of((s16)colours);
    if (!bm) return;
    DRV_CLIP_ON = 1;
    if (r3d_is_ps()) gfx_bitmap_quad(bm, xs, ys);
    else ces_check(bm, xs, ys);
    DRV_CLIP_ON = 0;
}

/* AP 2ec2:0002 (named nop_2ec2 in the index; = 2e33:08f2 bm_reset_mult) — DS:23BE = 1 */
void nop_2ec2(void) { DSW(AP_DS_bm_mult) = 1; }

/* PS 4915:0007 / AP 70e8:0000 — variant type 1: filled ellipse of radius size at a vertex */
void disc_draw(FarPtr var)
{
    u16 m = stk_mark(), p = stk_alloc(6);
    vtx_eye16(rd8(var.seg, (u16)(var.off + 2)), p);
    s16 px = DSS(p), py = DSS(p + 2), pz = DSS(p + 4);
    stk_release(m);
    if ((s16)CSW(CS_NEAR_Z) > py) return;
    u16 S = CSW(CS_PROJ_SHIFT);
    s16 x = (s16)(CSW(CS_CX) + ldiv_s32((s32)((u32)(s32)px << (S & 31)), py));
    s16 y = (s16)(CSW(CS_CY) - ldiv_s32((s32)((u32)(s32)pz << (S & 31)), py));
    s16 size = (s16)((s16)rd16(var.seg, var.off) >> ((u8)-(u8)CSW(CS_VTX_SHIFT) & 31));
    s16 r = ldiv_s32((s32)((u32)(s32)size << (S & 31)), py);
    if (r >= 0x100) r = 0x100;
    if (!r) return;
    s16 w = (s16)(r + (r >> 2));
    if (x > DRV_X2 || (s16)(x + w) <= DRV_X1 || y > DRV_Y2 || (s16)(y + r) <= DRV_Y1) return;
    DRV_CLIP_ON = 1;
    DRV_FILL_ON = 1;
    u8 c = DRV_VGA ? rd8(var.seg, (u16)(var.off + 3)) : rd8(var.seg, (u16)(var.off + 4));
    u16 remap = CSW(CS_REMAP);
    if (remap) c = DSB((u16)(remap + c));
    DRV_FILL_COL = c;
    DRV_EDGE_COL = c;
    gfx_circle(r, x, y);
}

/* ================================================================================================
 * Shadows (AP 2ecb / PS 48ac)
 * ============================================================================================== */

/* PS 48ac:0005 / AP 2ecb:0007 — ground point of pos along the world light; 0 when the light is not
 * from above (z < 1) */
s8 shadow_pos(u16 pos, u16 out)
{
    u16 l = DS_LIGHT_COPY;
    if (DSS(l + 4) < 1) return 0;
    s32 t = (s32)(-(s32)((u32)DSL(pos + 8) << 12)) / (s32)DSS(l + 4);          /* LDIV@ */
    DSL(out) = DSL(pos) + lsar((s32)((u32)t * (u32)(s32)DSS(l)), 12);          /* LXMUL@, 277a:3d52 */
    DSL(out + 4) = DSL(pos + 4) + lsar((s32)((u32)t * (u32)(s32)DSS(l + 2)), 12);
    DSL(out + 8) = 0;
    return 1;
}

/* PS 48ac:00ba / AP 2ecb:00ba — draw o flattened at its shadow point with the fixed LOD */
void shadow_draw(u16 o, u16 lod)
{
    u16 m = stk_mark(), tmp = stk_alloc(12), save = stk_alloc(12);
    if (shadow_pos((u16)(o + 2), tmp)) {
        memcpy(DSP(save), DSP(o + 2), 12);
        memcpy(DSP(o + 2), DSP(tmp), 12);
        DSW(DS_SHADOW_MODE) = 1;
        DSW(DS_SHADOW_LOD) = lod;
        CSW(CS_STATE_PTR) = DSW(o + 0x14);
        ts_draw(o);
        DSW(DS_SHADOW_MODE) = 0;
        memcpy(DSP(o + 2), DSP(save), 12);
    }
    stk_release(m);
}

/* AP 2ecb:013c (PS 48ac:013a shadow_set_colour is video's) — no caller in AP */
void set_shadow_colour(u8 c) { DSB(AP_DS_shadow_colour) = c; }

/* ================================================================================================
 * Visibility and LOD (render3d_shapes.md §2.2)
 * ============================================================================================== */

static s32 labs32(s32 v) { return v < 0 ? -v : v; }      /* rtl_labs (0000:1fff) */

/* p.x > x1 || p.y < y0 for the projected corner c (i32 x, y = depth, z) */
static bool corner_out(u16 c, u16 p)
{
    pt_project(c, p);
    pt_add_centre(p);
    return DSSL(p) > (s32)DRV_X2 || DSSL(p + 4) < (s32)DRV_Y1;
}

/* PS 4708:0003 / AP 2f0e:0008 — 1 = culled; sets app_size and need_clip */
u8 ts_visible_lod(void)
{
    u8 *K = cs_blk();
    u8 res = 1;
    u16 m = stk_mark(), c = stk_alloc(12), p = stk_alloc(8);
    s32 R = KSL(CS_RADIUS), ey = KSL(CS_E_POS + 4);
    s32 nz = (s32)(s16)CSW(CS_NEAR_Z);
    if ((s32)((u32)ey + (u32)R) < nz) goto out;
    {
        DSL(c) = (u32)R;
        s32 d = (s32)((u32)ey - (u32)R);
        s32 f = (s32)(s16)KW(CS_FOCAL);
        DSSL(c + 4) = f > d ? f : d;
        s32 A = (s32)apparent_size(c);
        if (A > 0x100) KW(CS_APP_SIZE) = 0x100;
        else { KW(CS_APP_SIZE) = (u16)A; if ((u16)A == 0 && !enh_pass) goto out; }   /* ENH: sub-pixel kept */
        DSL(c) = (u32)labs32(KSL(CS_E_POS)) - (u32)R;
        DSL(c + 8) = (u32)labs32(KSL(CS_E_POS + 8)) - (u32)R;
        DSL(c + 4) = (u32)ey + (u32)R;
        if (corner_out(c, p)) goto out;
        KB(CS_NEED_CLIP) = 0;
        if (KB(CS_FORCE_CLIP) || nz > (s32)((u32)ey - (u32)R)) KB(CS_NEED_CLIP) = 1;
        else {
            DSL(c) = (u32)labs32(KSL(CS_E_POS)) + (u32)R;
            DSL(c + 8) = (u32)labs32(KSL(CS_E_POS + 8)) + (u32)R;
            DSL(c + 4) = (u32)ey - (u32)R;
            if (corner_out(c, p)) KB(CS_NEED_CLIP) = 1;
        }
        u16 t = (u16)(DSW(DS_TABLES) + 12 * KW(CS_SHAPE_TBL));
        s16 scale = (s16)DSW(t + 6), lim = (s16)DSW(t + 8);
        if (scale == 0x100) ;
        else if (scale == -1) KW(CS_APP_SIZE) = (u16)lim;
        else {
            s32 a = (s16)mul_shr8(KW(CS_APP_SIZE), (u16)scale);          /* cwd of the 16-bit result */
            if (a < 0) a = 0;
            else if ((s32)lim < a) a = lim;
            KW(CS_APP_SIZE) = (u16)a;
        }
        res = 0;
    }
out:
    stk_release(m);
    return res;
}

/* ================================================================================================
 * Parts (render3d_shapes.md §2.3–§2.5)
 * ============================================================================================== */

#define PART_LIST_MAX 1024                   /* PORT: the original had 54 words and no bound check */

/* PS 4708:0715 / AP 2f0e:06eb — out[i] = parts_ofs + 14·i, then self if non-zero */
static s16 part_list_(u16 self, s16 n, u16 parts_ofs, u16 *out, u16 *end)
{
    s16 i;
    for (i = 0; i < n; i++) if (out + i < end) out[i] = (u16)(parts_ofs + 14 * i);
    if (self) { if (out + n < end) out[n] = self; n++; }
    return n;
}
u16 part_list(u16 self, s16 n, u16 parts_ofs, u16 *out) { return (u16)part_list_(self, n, parts_ofs, out, out + PART_LIST_MAX); }

static bool part_hidden(u16 seg, u16 p)
{
    u16 st = CSW(CS_STATE_PTR);
    return st && rd8(seg, p) != 0xFF && DSB((u16)(st + rd8(seg, p))) == 0xFF;
}

/* PS 4708:034c / AP 2f0e:033d — drop hidden parts; with shape flag 40h sort by N·P descending */
s16 part_sort(u16 self, s16 n, u16 *list)
{
    u8 *K = cs_blk();
    u16 seg = KW(CS_SHAPE_PTR + 2);
    if (n < 2) {
        if (n == 0) return 0;
        return part_hidden(seg, list[0]) ? 0 : 1;
    }
    FarPtr hdr = { KW(CS_SHAPE_PTR), seg };
    s16 cnt = 0;
    if (!(rd8(hdr.seg, hdr.off) & 0x40)) {
        for (s16 i = 0; i < n; i++) {
            u16 p = list[i];
            if (part_hidden(seg, p)) continue;
            list[cnt++] = p;
        }
        return cnt;
    }
    s32 keys[57 + PART_LIST_MAX];
    for (s16 i = 0; i < n; i++) {
        u16 p = list[i];
        if (part_hidden(seg, p)) continue;
        s32 key;
        if (self == p || rd8(seg, (u16)(p + 1)) == 0xFF) key = 0;
        else {
            u16 va = rd16(seg, (u16)(p + 4));
            if (!va && !seg) r3d_assert(0x2538, 0xA9, 0x39B6, 0xA9);
            s16 P[3], N[3];
            u8 pt = rd8(seg, (u16)(p + 2));
            if (pt == 0xFF) for (int k = 0; k < 3; k++) P[k] = KS(CS_EYE_IN_OBJ + 2 * k);
            else {
                u8 cl = (u8)-(u8)KW(CS_VTX_SHIFT);
                for (int k = 0; k < 3; k++)
                    P[k] = (s16)(((s16)rd16(seg, (u16)(va + pt * 6 + 2 * k)) >> (cl & 31)) + KS(CS_EYE_IN_OBJ + 2 * k));
            }
            u8 nv = rd8(seg, (u16)(p + 1));
            for (int k = 0; k < 3; k++) N[k] = (s16)rd16(seg, (u16)(va + nv * 6 + 2 * k));
            key = (s32)((u32)((s32)P[0] * N[0]) + (u32)((s32)P[1] * N[1]) + (u32)((s32)P[2] * N[2]));
        }
        list[cnt] = p;
        keys[cnt] = key;
        cnt++;
    }
    for (s16 i = 0; i < cnt - 1; i++) {                      /* selection sort, descending */
        s16 best = i;
        s32 mx = keys[i];
        for (s16 j = (s16)(i + 1); j < cnt; j++) if (keys[j] > mx) { best = j; mx = keys[j]; }
        if (best != i) {
            s32 tk = keys[i]; keys[i] = keys[best]; keys[best] = tk;
            u16 tp = list[i]; list[i] = list[best]; list[best] = tp;
        }
    }
    return cnt;
}

static void part_walk_(u16 self, s16 n, u16 *list, u16 *end)
{
    u8 *K = cs_blk();
    n = part_sort(self, n, list);
    for (s16 i = 0; i < n; i++) {
        u16 seg = KW(CS_SHAPE_PTR + 2);
        u16 p = list[i];
        s16 nch = (s16)rd16(seg, (u16)(p + 0x0A));
        if (p != self && nch > 0) {
            u16 *sub = list + n;
            if (sub >= end) continue;
            s16 m = part_list_(p, nch, rd16(seg, (u16)(p + 0x0C)), sub, end);
            if (sub + m > end) m = (s16)(end - sub);
            part_walk_(p, m, sub, end);
            continue;
        }
        KW(CS_VTX_OFS) = rd16(seg, (u16)(p + 4));
        KW(CS_VTX_COUNT) = rd8(seg, (u16)(p + 3));
        s16 v = 0;
        if (KW(CS_STATE_PTR) && rd8(seg, p) != 0xFF) {
            v = DSB((u16)(KW(CS_STATE_PTR) + rd8(seg, p)));
            s16 nv = (s16)rd16(seg, (u16)(p + 6));
            if (nv > 0) while (nv <= v) v = (s16)(v - nv);   /* PORT: nv <= 0 looped forever */
        }
        FarPtr var = { (u16)(rd16(seg, (u16)(p + 8)) + 8 * v), seg };
        u8 type = rd8(var.seg, var.off);
        FarPtr arg = far_add(var, 2);
        if (type == 0) {
            DRV_CLIP_ON = 0;
            switch (KB(CS_DRAW_MODE)) {
            case 0: if (DSW(DS_SHADOW_MODE)) part_draw_shadow(arg); else part_draw16(arg); break;
            case 3: part_draw16s(arg); break;
            default: part_draw32(arg); break;
            }
        } else if (type == 1) disc_draw(arg);
        else sprite_draw(arg);
    }
}
/* PS 4708:074b / AP 2f0e:071f */
void part_walk(u16 self, s16 n, u16 *list) { part_walk_(self, n, list, list + PART_LIST_MAX); }

/* PS 4708:0925 / AP 2f0e:08e9 — draw one instance (render3d_shapes.md §2.1, video_ts.md §4) */
void ts_draw(u16 o)
{
    u8 *K = cs_blk();
    if (bm_is_bitmap((s16)DSW(o))) { if (!enh_pass) bm_draw(o); return; }   /* ENH: bitmaps not re-drawn */
    FarPtr hdr = shape_ptr((s16)DSW(o));
    KW(CS_SHAPE_PTR + 2) = hdr.seg;
    KW(CS_SHAPE_PTR) = hdr.off;
    if (far_is_null(hdr)) { r3d_assert(0x2550, 0x167, 0x39CE, 0x167); return; }
    KW(CS_STATE_PTR) = DSW(o + 0x14);
    u8 sh = rd8(hdr.seg, (u16)(hdr.off + 3));
    KW(CS_OBJ_SHIFT) = sh;
    KL(CS_RADIUS) = sh >= 32 ? 0 : (u32)(s32)(s16)rd16(hdr.seg, (u16)(hdr.off + 0x0C)) << sh;   /* LXLSH@ */
    obj_set_position(o);
    if (ts_visible_lod()) return;
    obj_set_orientation();
    bool ps = r3d_is_ps();
    u8 ns = (u8)CSW(CS_NEAR_SHIFT);
    s32 lim7d = ps ? 0x7D00 : (0x7D00 >> (ns >= 32 ? 31 : ns));              /* LXRSH@ */
    s32 lim3f = ps ? 0x3FAC : (0x3FAC >> (ns >= 32 ? 31 : ns));
    s32 R = KSL(CS_RADIUS);
    u8 mode;
    u16 enh_shift = 0;                                           /* ENH: eye coordinates >> this */
    if (KW(CS_OBJ_SHIFT) == 0 && R <= 0x3FAC && lim7d >= (s32)(KL(CS_E_DIST) + (u32)R)) mode = 0;
    else {
        obj_near_depth();
        u8 k = (u8)KW(CS_OBJ_DOWN);
        s32 t = KSL(CS_E_NEAR) >> (k >= 32 ? 31 : k);
        if (t > (s32)(s16)CSW(CS_NEAR_Z)) {
            KW(CS_OBJ_SHIFT) = KW(CS_VTX_SHIFT);
            KSL(CS_E_DEPTH) = KS(CS_E_DEPTH16);
            for (int i = 0; i < 3; i++) KSL(CS_E_POS + 4 * i) = KS(CS_E_POS16 + 2 * i);
            mode = KW(CS_OBJ_SHIFT) ? 3 : 0;
            enh_shift = k;
        } else mode = lim3f > R ? 1 : 2;
    }
    KB(CS_DRAW_MODE) = mode;
    enh_ts_object(enh_shift);                                    /* ENH: the instance's pose */
    /* LOD */
    s16 nlod = (s16)rd16(hdr.seg, (u16)(hdr.off + 8));
    u16 lod = rd16(hdr.seg, (u16)(hdr.off + 0x0A));
    if (DSW(DS_SHADOW_MODE) && nlod > (s16)DSW(DS_SHADOW_LOD)) lod = (u16)(lod + 6 * DSW(DS_SHADOW_LOD));
    else {
        s16 i = 0;
        while (i < nlod && (s16)KW(CS_APP_SIZE) > (s16)rd16(hdr.seg, lod)) { i++; lod = (u16)(lod + 6); }
        if (i == nlod) lod = (u16)(lod - 6);
        if (enh_lod_finest() && nlod > 0)                  /* ENH: the finest LOD in the enhanced pass */
            lod = (u16)(rd16(hdr.seg, (u16)(hdr.off + 0x0A)) + 6 * (nlod - 1));
    }
    KW(CS_VTX_KEY + 2) = 0;
    KW(CS_VTX_KEY) = 0;
    KW(CS_VTX_OFS) = 0;
    u16 list[PART_LIST_MAX];
    s16 cnt = part_list_(0, (s16)rd16(hdr.seg, (u16)(lod + 2)), rd16(hdr.seg, (u16)(lod + 4)), list, list + PART_LIST_MAX);
    if (cnt > PART_LIST_MAX) cnt = PART_LIST_MAX;
    part_walk_(0, cnt, list, list + PART_LIST_MAX);
}
