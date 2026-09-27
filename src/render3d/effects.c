/* render3d — effects.c: cloud-layer and range culling (AP 0802), ground fires and effect drawing
 * (1560), aircraft shadows (1a8b), far-aircraft dots (2466), class registry (59f9), muzzle flashes
 * (655d:038a) and ground-speed dots (675d:0000). render3d_world.md §4, render3d_sky.md §4. */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"
#include "codeptr.h"
#include "video/video.h"
#include "sim_world/sim_world.h"
#include "sim_flow/sim_flow.h"
#include "enhanced/enhanced.h"    /* ENH: range of the enhanced pass */

enum {
    E_OVERCAST_A = 0x4512, E_OVERCAST_B = 0x42C9, E_CAMERA = 0x437E,
    E_OBJS = 0x475D, E_NOBJS = 0x4778,          /* near ptr to all sim objects (near ptrs) / count */
    E_AIR = 0x475B, E_NAIR = 0x4776,            /* aircraft */
    E_ORD_POOL = 0x00E6, E_FX_POOL = 0x55F1,
    E_IN_CLOUD = 0x5BE5, E_CLOUD_X = 0x5600, E_CLOUD_Y = 0x55FC,
    E_DAMAGE = 0x5A82,                          /* GDAMAGE.DAT, 19 × 8 bytes */
    E_FX_BASE = 0x474D,                         /* first id of the common effect shapes */
    E_SHADOW_MODE = 0x5CE0, E_SHADOW_RANGE = 0x5CDE, E_SHADOW_ALT = 0x5CDA,
    E_PROXIES = 0x5EDE,
    E_CLASS_N = 0x47F2, E_CLASS_MAX = 0x47F4, E_CLASS_TAB = 0x47F6, E_CLASS_RESET = 0x488C,
    E_MOVERS = 0x00BC, E_NMOVERS = 0x00BE, E_PLAYER = 0x3B09, E_ZERO_ANG = 0x3716,
    E_GUN_TAB = 0x5178, E_SMOKE_28 = 0x515A, E_RNG = 0x43DE,
    E_DOT_COL = 0x102E,
    E_PAGE = 0x26D2, E_READ_PAGE = 0x26D6, E_DRAW_PAGE = 0x26D8,
};

/* ================================================================================================
 * 0802 — cloud layer and range culling */

/* AP 0802:000c */
u16 overcast(void) { return DSB(E_OVERCAST_A) != 0 && DSB(E_OVERCAST_B) != 0; }

static s16 deck_side(s32 z) { return z == 16000 ? 0 : z < 16000 ? -1 : 1; }   /* sign of z − 16000 */

/* AP 0802:0026 — objects on the other side of the overcast deck (z = 16000) are skipped this frame;
 * returns eye above the deck */
u8 overcast_split(void)
{
    s32 z = DSSL(DSW(DSW(E_CAMERA) + 2) + 0x0A);
    u16 own = view_outside_target();
    s16 s = deck_side(z);
    if (!overcast()) return 0;
    u16 di = DSW(E_OBJS);
    for (s16 i = 0; i < (s16)DSW(E_NOBJS); i++, di = (u16)(di + 2)) {
        u16 o = DSW(di);
        if (o == own) continue;
        if (deck_side(DSSL(o + 0x0C)) != s) DSW(o) |= 0x100;
    }
    static const u16 pools[2] = { E_ORD_POOL, E_FX_POOL };
    for (int k = 0; k < 2; k++)
        for (FarPtr e = fp_u32(pool_next(DSW(pools[k]), far_make(0, 0))); !far_is_null(e);
             e = fp_u32(pool_next(DSW(pools[k]), e)))
            if (deck_side((s32)rd32(e.seg, (u16)(e.off + 0x0C))) != s) wr16(e.seg, e.off, (u16)(rd16(e.seg, e.off) | 0x100));
    return z > 16000;
}

/* AP 0802:0173 — cloud containing the eye / line of sight through the clouds */
void range_cull(void)
{
    u16 mk = stk_mark(), E = stk_alloc(12);
    memcpy(DSP(E), DSP(DSW(DSW(E_CAMERA) + 2) + 2), 12);
    if (overcast()) { stk_release(mk); return; }
    u16 c = point_in_cloud(E);
    if (c) {
        DSB(E_IN_CLOUD) = 1;
        DSL(E_CLOUD_X) = DSL(c + 4);
        DSL(E_CLOUD_Y) = DSL(c + 8);
        u16 di = DSW(E_OBJS);
        for (s16 i = 0; i < (s16)DSW(E_NOBJS); i++, di = (u16)(di + 2)) {
            u16 o = DSW(di);
            if (DSW(o) & 0x100) continue;
            if (ldist3(E, (u16)(o + 4)) > 3000) DSW(o) |= 0x100;
        }
    } else {
        DSB(E_IN_CLOUD) = 0;
        u16 di = DSW(E_AIR);
        for (s16 i = 0; i < (s16)DSW(E_NAIR); i++, di = (u16)(di + 2)) {
            u16 o = DSW(di);
            if (DSW(o) & 0x100) continue;
            u32 d = ldist3((u16)(o + 4), E);
            if (enh_dist(d) > 200000 || cloud_blocks_segment((u16)(o + 4), E)) DSW(o) |= 0x100;   /* ENH */
        }
    }
    stk_release(mk);
}

/* ================================================================================================
 * 1560 — fires on destroyed macro members, effect drawing */

/* AP 1560:055b */
void spawn_ground_fires(void)
{
    u16 mk = stk_mark(), T = stk_alloc(0x30);
    memset(DSP(T), 0, 0x30);
    DSW(T) = 0x080E;
    DSW(T + 0x1C) = 0x80;
    DSB(T + 0x1E) = 0;
    FarPtr ent = far_add(ds_far(W_DRAW_LIST), (u16)(DSW(W_LIST_GROUND_N) << 3));
    for (s16 i = (s16)DSW(W_LIST_GROUND_N); i < (s16)DSW(W_LIST_N); i++, ent = far_add(ent, 8)) {
        FarPtr o = far_rd(ent.seg, ent.off);
        if (!(rd16(o.seg, o.off) & 0x1000)) continue;      /* expanded 3D members only */
        FarPtr g = far_rd(o.seg, (u16)(o.off + 0x16));
        if (rd8(g.seg, (u16)(g.off + rd16(o.seg, (u16)(o.off + 0x1A)) + 0x12)) != 0xFF) continue;
        u8 kind = rd8(o.seg, (u16)(o.off + 0x1D));
        if (kind >= 0x13) continue;
        if (!(rd16(o.seg, o.off) & 0xC0)) {
            wr16(o.seg, o.off, (u16)(rd16(o.seg, o.off) | 0x80));
            u16 w = objtype_shape(rd16(o.seg, (u16)(o.off + 2)));
            if (w == 0xFFFF) { wr16(o.seg, o.off, (u16)(rd16(o.seg, o.off) | 0x2000)); continue; }
            wr16(o.seg, (u16)(o.off + 2), w);
        }
        u16 f = (u16)(E_DAMAGE + 8 * kind);
        if (DSW(f) == 0) continue;
        DSW(T + 2) = (u16)(DSW(E_FX_BASE) + (DSB(f + 4) ? 0x1F : 0x1D));
        if (rd16(o.seg, o.off) & 0x80) DSW(T + 2)++;
        memcpy(DSP(T + 4), far_mp(far_add(o, 4)), 12);
        FarPtr e = fp_u32(fx_spawn(T));
        if (far_is_null(e)) continue;
        wr16(e.seg, (u16)(e.off + 0x2A), DSW(f));
        u8 c = (u8)(rd8(o.seg, (u16)(o.off + 0x1C)) + 1);
        wr8(o.seg, (u16)(o.off + 0x1C), c);
        if ((s16)c > (s16)(shape_nvariants((s16)rd16(e.seg, (u16)(e.off + 2)), 0) - 1)) {
            wr8(o.seg, (u16)(o.off + 0x1C), 0);
            if (rd16(o.seg, o.off) & 0x80) {                  /* fire -> smoke */
                wr16(o.seg, o.off, (u16)(rd16(o.seg, o.off) ^ 0xC0));
                wr16(e.seg, (u16)(e.off + 2), (u16)(rd16(e.seg, (u16)(e.off + 2)) - 1));
            }
        }
        wr16(e.seg, (u16)(e.off + 0x16), rd8(o.seg, (u16)(o.off + 0x1C)));
    }
    stk_release(mk);
}

/* AP 1560:06e9 — an effect record drawn with its own radius (the 35d6 cache is not updated) */
void effect_draw(FarPtr e)
{
    u16 mk = stk_mark(), rec = stk_alloc(0x16), st = stk_alloc(2);
    DSB(st) = rd8(e.seg, (u16)(e.off + 0x16));
    u16 shape = rd16(e.seg, (u16)(e.off + 2));
    s16 r0 = (s16)shape_radius_cached(shape);
    shape_set_radius((s16)shape, (s16)rd16(e.seg, (u16)(e.off + 0x2A)));
    memcpy(DSP(rec), far_mp(far_add(e, 2)), 0x16);
    DSW(rec + 0x14) = st;
    ts_draw(rec);
    shape_set_radius((s16)rd16(e.seg, (u16)(e.off + 2)), r0);
    stk_release(mk);
}

/* ================================================================================================
 * 1a8b — aircraft shadows */

/* AP 1a8b:0004 */
void shadows_default(void)
{
    DSW(E_SHADOW_MODE) = 2;
    DSW(E_SHADOW_RANGE) = 10;
    DSL(E_SHADOW_ALT) = 3000;
}
/* AP 1a8b:001e — detail slider */
void shadows_set_detail(u16 v)
{
    DSW(E_SHADOW_RANGE) = v;
    DSSL(E_SHADOW_ALT) = (s32)(s16)v * 300;
    DSW(E_SHADOW_MODE) = (s16)v > 13 ? 3 : 0;
}
/* AP 1a8b:004c — one shadow effect per low aircraft */
void shadows_spawn(void)
{
    if (DSW(E_SHADOW_MODE) == 0) return;
    u16 mk = stk_mark(), T = stk_alloc(0x30);
    memset(DSP(T), 0, 0x30);
    DSW(T) = 0x080B;
    DSW(T + 0x1C) = DSW(E_SHADOW_RANGE);
    DSB(T + 0x1E) = 0xC8;
    u16 di = DSW(E_AIR);
    for (s16 i = 0; i < (s16)DSW(E_NAIR); i++, di = (u16)(di + 2)) {
        u16 o = DSW(di);
        if (!obj_alive(o)) continue;
        if (DSSL(o + 0x0C) >= DSSL(E_SHADOW_ALT)) continue;
        if (!shadow_pos((u16)(o + 4), (u16)(T + 4))) continue;
        DSW(T + 2) = DSW(o + 2);
        memcpy(DSP(T + 0x10), DSP(o + 0x10), 8);
        fx_spawn(T);
    }
    stk_release(mk);
}

/* ================================================================================================
 * 2466 — far-aircraft dots */

/* AP 2466:000a */
void far_dot_alloc(void) { DSW(E_PROXIES) = arena_alloc((u16)(DSW(E_NAIR) * 18)); }

/* AP 2466:0023 — one pixel in the side colour when the aircraft projects inside the view window */
void far_dot_draw(FarPtr p)
{
    u16 mk = stk_mark(), P = stk_alloc(12), C = stk_alloc(12), M = stk_alloc(20), e = stk_alloc(12),
        s = stk_alloc(8), rc = stk_alloc(8);
    memcpy(DSP(P), far_mp(far_add(p, 4)), 12);
    memcpy(DSP(C), csp(CS_CAM_POS), 12);
    memcpy(DSP(M), csp(CS_M_CAM), 19);
    cockpit_view_window(rc);
    lsub(P, C);
    lvec_rotate(P, M, e);
    if ((s32)CSS(CS_FOCAL) > DSSL(e + 4)) { stk_release(mk); return; }
    pt_add_centre(pt_project(e, s));
    s32 sx = DSSL(s), sy = DSSL(s + 4);
    s16 x = (s16)DSW(rc), y = (s16)DSW(rc + 2), w = (s16)DSW(rc + 4), h = (s16)DSW(rc + 6);
    if ((s32)x < sx && (s32)(s16)(x + w) > sx && (s32)y < sy && (s32)(s16)(y + h) > sy) {
        DRV_CLIP_ON = 1;
        gfx_put_pixel((s16)sx, (s16)sy, (s16)rd16(p.seg, (u16)(p.off + 0x10)));
    }
    stk_release(mk);
}

/* ================================================================================================
 * 59f9 — class registry (sim behaviour descriptors) */

/* AP 59f9:0000 (BX = record) */
void class_register(u16 bx)
{
    DSW(bx + 6) |= 0x8000;
    DSW(E_CLASS_TAB + 2 * DSW(E_CLASS_N)) = bx;
    DSW(E_CLASS_N)++;
    if ((s16)DSW(bx + 4) > (s16)DSW(E_CLASS_MAX)) DSW(E_CLASS_MAX) = DSW(bx + 4);
}
/* AP 59f9:0026 (AX = n, BX = array of record pointers) */
void class_register_n(u16 ax, u16 bx)
{
    for (u16 n = ax; n; n--, bx = (u16)(bx + 2)) class_register(DSW(bx));
}
/* AP 59f9:004e (BX = object) */
void class_alloc(u16 bx) { DSW(bx + 0x0A) = arena_alloc(DSW(E_CLASS_MAX)); }
/* AP 59f9:0068 */
void class_reset(void) { DSW(E_CLASS_RESET) = 0; }

/* ================================================================================================
 * 655d:038a — muzzle flashes and gun smoke (render3d_sky.md §4.3) */

/* the effect position: a->pos + vec_rotate(ofs, obj_matrix(a[+20h])) */
static void gun_pos(u16 T, u16 a, u16 ofs, u16 tmp)
{
    vec_rotate(ofs, obj_matrix(DSW(a + 0x20)), tmp);
    memcpy(DSP(T + 4), DSP(a + 4), 12);
    ladd_v((u16)(T + 4), tmp);
}

void gun_flashes(void)
{
    u16 mk = stk_mark(), T = stk_alloc(0x30), ofs = stk_alloc(6), tmp = stk_alloc(6);
    memset(DSP(T), 0, 0x30);
    DSW(T) = 0x080E;
    DSW(T + 0x1C) = 0x80;
    DSB(T + 0x1E) = 0;
    u16 pp = DSW(E_MOVERS);
    for (s16 n = 0; n < (s16)DSW(E_NMOVERS); n++, pp = (u16)(pp + 2)) {
        u16 a = DSW(pp);
        if (!obj_alive(a)) continue;
        u16 G = DSW(a + 0x2A), K = DSW(a + 0x28);
        u8 ai = DSB(a + 0x18) == 0;
        u16 p22 = (u16)(K + 0x10), p24 = (u16)(K + 4), p26 = DSW(G + 4), p28 = DSW(G + 6);
        u16 p1e = (u16)(p22 + 8);
        for (s16 i = 0; (s16)(DSW(K) & 0x0F) > i;
             i++, p1e = (u16)(p1e + 10), p22 = (u16)(p22 + 10), p24 = (u16)(p24 + 8), p26 = (u16)(p26 + 2), p28++) {
            DSW(T + 2) = 0;                                /* shape */
            u16 za = DSW(E_ZERO_ANG);
            memcpy(DSP(T + 0x10), DSP(za), 6);
            DSL(T + 0x18) = 0;
            if (!ai) {
                u8 cls = DSW(G) == 0x400;
                u16 k0 = DSW(K);
                u16 t = (u16)(E_GUN_TAB + ((((s16)k0 >> 8) & 3) << 3));
                if (cls) {
                    if (!(DSW(K) & 0x8000)) {
                        if ((s16)(((s16)(k0 & 0x0F)) >> 1) == i) { DSW(T + 2) = (u16)(DSW(E_FX_BASE) + 0x16); DSW(T + 0x2A) = DSW(t + 6); }
                        else { DSW(T + 2) = (u16)(DSW(E_FX_BASE) + 0x17); DSW(T + 0x2A) = DSW(t + 4); }
                    }
                } else if ((s16)DSW(p26) >= 500) {
                    DSW(T + 2) = (u16)(DSW(E_FX_BASE) + ((DSW(p24 + 6) & 0x40) ? 0x16 : 0x15));
                    DSW(T) = 0x080E;
                    DSW(T + 0x2A) = DSW(t);
                }
                memcpy(DSP(ofs), DSP(p24), 6);
            } else {
                u8 big = 0, nr = 0;
                if (DSW(E_PLAYER) == a) {
                    u16 pl = DSW(E_PLAYER);
                    if (i == 0) {
                        if ((s16)DSW(pl + 0xA4) < 0x20C) big = 1;
                        if ((s16)DSW(pl + 0xA4) < 0x7C) nr = 1;
                    } else if (i == 1 && ((s16)(DSW(K) & 0xF00) >> 8) == 2) {
                        if ((s16)DSW(pl + 0xA6) < 0x20C) big = 1;
                        if ((s16)DSW(pl + 0xA6) < 0x7C) nr = 1;
                    }
                }
                if ((s16)DSW(p26) < 500 && !big) goto emit;
                if (!timer16_dec((u16)(p22 + 6))) {        /* next smoke puff */
                    DSW(T + 2) = (u16)(DSW(E_FX_BASE) + 0x1B);
                    ds_far_wr((u16)(T + 0x18), codeptr_far(0x3DFF0061u));   /* stub of 655d:1bb7 */
                    for (int c = 0; c < 3; c++)
                        DSW(T + 0x22 + 2 * c) = (u16)(((s32)(s16)DSW(a + 0x70 + 2 * c) << 7) >> 8);
                    DSW(T + 0x28) = DSW(E_SMOKE_28);
                    DSW(p22 + 6) = (u16)((ap_rng_next(E_RNG) & 0x3F) + 0xC8);
                    DSW(T) = 0x080C;
                    memcpy(DSP(ofs), DSP(p22), 6);
                    DSW(ofs + 2) = (u16)(DSW(ofs + 2) + DSW(K + 0x0E));
                }
                if ((s16)DSW(p26) < 900 && !nr) goto emit;
                if (DSW(T + 2)) {                          /* emit the smoke puff now */
                    DSW(ofs + 2) = (u16)(DSW(ofs + 2) - 0x100);
                    gun_pos(T, a, ofs, tmp);
                    if (!(DSW(p1e) & 0x10)) { DSB(p28) = 0; DSW(p1e) |= 0x10; }
                    FarPtr e = fp_u32(fx_spawn(T));
                    if (!far_is_null(e)) {
                        wr16(e.seg, (u16)(e.off + 0x20), a);
                        wr16(e.seg, (u16)(e.off + 0x16), (u16)(DSB(p28) & 3));
                    }
                    DSL(T + 0x18) = 0;
                }
                DSW(T + 2) = (u16)(DSW(E_FX_BASE) + ((DSW(p1e) & 0x80) ? 0x1A : 0x19));
                DSW(T) = 0x080C;
                memcpy(DSP(T + 0x10), DSP(a + 0x10), 6);
                DSW(T + 0x10) = (u16)(DSW(T + 0x10) - 0x4000);
                memcpy(DSP(ofs), DSP(p22), 6);
                DSW(ofs + 2) = (u16)(DSW(ofs + 2) + DSW(K + 0x0E));
            }
        emit:
            if (!DSW(T + 2)) continue;
            if (ai && a == DSW(E_PLAYER)) {                /* own flashes hidden in cockpit views */
                s16 v = view_get();
                if (v <= 5 && v != 1) continue;
            }
            gun_pos(T, a, ofs, tmp);
            u8 c = (u8)(DSB(p28) + 1);
            DSB(p28) = c;
            if ((s16)c >= (s16)(shape_nvariants((s16)DSW(T + 2), 0) - 1)) {
                DSB(p28) = 0;
                if (DSW(E_FX_BASE) + 0x19 == DSW(T + 2)) { DSW(p1e) |= 0x80; DSW(T + 2)++; }
                else if (DSW(E_FX_BASE) + 0x15 == DSW(T + 2)) { DSW(p24 + 6) |= 0x40; DSW(T + 2)++; }
            }
            FarPtr e = fp_u32(fx_spawn(T));
            if (!far_is_null(e)) {
                wr16(e.seg, (u16)(e.off + 0x20), a);
                wr16(e.seg, (u16)(e.off + 0x12), 0);
                wr16(e.seg, (u16)(e.off + 0x16), DSB(p28));
            }
        }
    }
    stk_release(mk);
}

/* ================================================================================================
 * 675d:0000 — ground-speed dots (the DS:36D0 callback, after the ground layer) */
void ground_speed_dots(void)
{
    u16 cam = DSW(E_CAMERA);
    u8 S = DSB(cam);
    s16 f = (s16)(1 << S);
    u16 eye = (u16)(DSW(cam + 2) + 2);
    if (DSSL(eye + 8) > 4000) return;
    u16 mk = stk_mark(), rc = stk_alloc(8), P = stk_alloc(6), U = stk_alloc(6), V = stk_alloc(6),
        R = stk_alloc(6), M = stk_alloc(20);
    s16 cy = cockpit_view_window(rc);
    s16 cx = (s16)((s16)DSW(rc + 4) >> 1);
    s16 xmin = (s16)DSW(rc), xmax = (s16)(DSW(rc) + DSW(rc + 4));
    s16 ymin = (s16)DSW(rc + 2), ymax = (s16)(DSW(rc + 2) + DSW(rc + 6));
    DSW(E_READ_PAGE) = DSW(E_PAGE);
    DSW(E_DRAW_PAGE) = DSW(E_PAGE);
    DSW(P) = (u16)-(DSW(eye) & 0x3FF);                     /* 1024-unit grid under the eye */
    DSW(P + 2) = (u16)-(DSW(eye + 4) & 0x3FF);
    DSW(P + 4) = (u16)-DSW(eye + 8);
    memcpy(DSP(U), DSP(P), 6);
    memcpy(DSP(V), DSP(P), 6);
    DSW(U) = (u16)(DSW(U) + 0x400);
    DSW(V + 2) = (u16)(DSW(V + 2) + 0x400);
    memcpy(DSP(M), csp(CS_M_CAM), 19);
    vec_rotate(P, M, R); memcpy(DSP(P), DSP(R), 6);
    vec_rotate(U, M, R); memcpy(DSP(U), DSP(R), 6);
    vec_rotate(V, M, R); memcpy(DSP(V), DSP(R), 6);
    vsub(U, P);
    vsub(V, P);
    if ((s16)DSW(U + 2) > 0) vneg(U);                      /* step toward the viewer */
    if ((s16)DSW(V + 2) > 0) vneg(V);
    for (int c = 0; c < 6; c += 2) DSW(P + c) = (u16)(DSW(P + c) - (u16)((DSW(U + c) + DSW(V + c)) << 2));
    for (s16 i = 0; i < 9; i++) {
        u8 row[6];
        memcpy(row, DSP(P), 6);
        for (s16 j = 0; j < 9; j++, vadd(P, V)) {
            s16 py = (s16)DSW(P + 2);
            if (py <= f) continue;
            s16 sx = (s16)(cx + ldiv_sat((s32)(s16)DSW(P) << S, py));
            if (sx <= xmin || sx >= xmax) continue;
            s16 sy = (s16)(cy - ldiv_sat((s32)(s16)DSW(P + 4) << S, py));
            if (sy <= ymin || sy >= ymax) continue;
            s16 c = gfx_get_pixel(sx, sy);
            if (c > 0x87) continue;                        /* signed compare */
            gfx_put_pixel(sx, sy, (s16)(s8)DSB((u16)(E_DOT_COL + c)));
        }
        memcpy(DSP(P), row, 6);
        vadd(P, U);
    }
    stk_release(mk);
}

__attribute__((constructor)) static void effects_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x675D0000u, (CodeFn)ground_speed_dots);
}
