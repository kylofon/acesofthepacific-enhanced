/* render3d — scene.c: the 3D frame (AP 5440:0269/0375/03aa), object lists and depth order (33d1, 34bf,
 * 33b4, 465c:0589). render3d_world.md §2–§3.
 *
 * Draw list DS:36D4 (far, 8-byte entries {far obj, i32 key}): [0, 36DA) ground layer (priority byte
 * of the shape header, ordered once per rebuild), [36DA, 36DA+36DC) static distance entries, then the
 * moving objects of the frame. The depth order is a binary tree built per frame (tree_build) and
 * walked far to near (tree_draw). */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"
#include "codeptr.h"
#include "sim_world/sim_world.h"
#include "sim_flow/sim_flow.h"

enum {
    S_CAMERA = 0x437E,            /* near: camera record of the 3D view */
    S_P_STATIC_END = 0x5134,      /* partition of DS:06F3 (render3d_world.md §6.1) */
    S_P_BIG_END = 0x5132, S_P_REST_END = 0x5130, S_P_SHORT_END = 0x512E,
    S_RAND_CURSOR = 0x5822,       /* shared random-table cursor */
    S_DRAW_COUNT = 0x06B7,        /* objects drawn (VCR hook every other one) */
    S_SHADOW_COL = 0x5CE0,        /* colour of shadow objects (type 0Bh) */
    S_PLAYER = 0x3B09, S_CAM_MOVED = 0x19F2, S_SUN_PITCH = 0x51D9, S_SUN_HEADING = 0x51DD,
    S_NO_CELESTIAL = 0x5BDA, S_SKY = 0x5BDB, S_SPOT_ON = 0x42C8, S_KEEP = 0x18C0, S_REBUILD = 0x4380,
    S_MOVERS = 0x00BC, S_NMOVERS = 0x00BE, S_ORD_POOL = 0x00E6, S_FX_POOL = 0x55F1, S_FLAK = 0x54B5,
    S_PROXIES = 0x5EDE, S_SIDE_OF = 0x54BC, S_TYPES = 0x55D2,
};
#define RAND_TABLE_SEG SEG(0x3A0D)        /* 256 words, filled by sim_flow rand_table_init */
#define TREE_MAX 256

typedef void (*ObjFn)(FarPtr o);

/* ldist3 from a DS position to the position (+4) of a far object (the original copies it to its
 * stack with SCOPY@ first) */
static u32 dist_obj(u16 e, FarPtr o)
{
    u16 m = stk_mark(), p = stk_alloc(12);
    memcpy(DSP(p), far_mp(far_add(o, 4)), 12);
    u32 d = ldist3(e, p);
    stk_release(m);
    return d;
}
static s16 lnear_obj(u16 e, FarPtr o, u32 r)
{
    u16 m = stk_mark(), p = stk_alloc(12);
    memcpy(DSP(p), far_mp(far_add(o, 4)), 12);
    s16 v = lnear(e, p, r);
    stk_release(m);
    return v;
}
static u16 eye_ofs(void) { return (u16)(DSW(DSW(W_SCENE_VIEW) + 2) + 2); }

/* ================================================================================================
 * 33b4 — draw records */

/* AP 33b4:0004 */
void draw_entry(FarPtr o)
{
    FarPtr cb = ds_far(W_DRAW_CB);
    if (!far_is_null(cb)) ((ObjFn)codeptr_lookup(cb))(o);
    else draw_entry_default(o);
}

/* the 22-byte record of §1.2 at DS:r (zeroed first) */
static void fill_record(u16 r, FarPtr o)
{
    memset(DSP(r), 0, 0x16);
    u8 *s = far_mp(o);
    DSW(r) = (u16)(g16(s, 2) & 0x1FFF);
    memcpy(DSP(r + 2), s + 4, 12);
    switch (g16(s, 0) & 0x0E00) {
    case 0x0800: memcpy(DSP(r + 0x0E), s + 0x10, 6); break;
    case 0x0400: DSL(r + 0x0A) = 0; break;
    case 0x0200: DSW(r + 0x12) = g16(s, 0x10); break;
    case 0x0C00: DSL(r + 0x0A) = 0; memcpy(DSP(r + 0x0E), s + 0x0C, 6); break;
    case 0x0600: DSL(r + 0x0A) = 0; DSW(r + 0x12) = g16(s, 0x0C); break;
    default: break;
    }
}

/* AP 33b4:002a — without a callback: the record on the stack, no state (unused in AP) */
void draw_entry_default(FarPtr o)
{
    u16 m = stk_mark(), r = stk_alloc(0x16);
    memset(DSP(r), 0, 0x16);
    if (rd16(o.seg, o.off) & 0x100) { wr16(o.seg, o.off, (u16)(rd16(o.seg, o.off) & ~0x100)); stk_release(m); return; }
    fill_record(r, o);
    ts_draw(r);
    stk_release(m);
}

/* AP 33b4:010e — the record at DS:3666 */
u16 draw_record(FarPtr o)
{
    fill_record(W_DRAW_REC, o);
    return W_DRAW_REC;
}

/* ================================================================================================
 * 33d1 — lists */

/* AP 33d1:0004 */
void scene_begin(u16 v)
{
    DSW(W_SCENE_VIEW) = v;
    s32 a = DSSL(DSW(v + 2) + 0x0A) >> 2;
    s16 alt = a >= 0x7FFF ? 0x7FFF : (s16)a;               /* no lower clamp */
    u16 e = DSW(W_FLT_TAB);
    for (s16 c = 0; c < (s16)DSW(W_FLT_N); c++, e = (u16)(e + 0x55)) {
        if (DSB(e) && DSB(e + 3) == 0) {                   /* range mode */
            s16 r0 = (s16)DSW(e + 7), r1 = (s16)DSW(e + 9);
            s32 t = ((s32)(s16)(r1 - r0) * alt) >> 15;
            DSSL(DSW(W_FLT_RANGE) + 4 * c) = (s32)(s16)(r0 + (s16)t) * 1000;
        }
    }
    ap_arena_reset_34f1();
}

/* AP 33d1:00a8 — the eye moved more than DS:36C4 since the last rebuild */
u8 scene_eye_moved(u16 v)
{
    u16 eye = (u16)(DSW(v + 2) + 2);
    u32 d = ldist3(eye, W_GATHER_EYE);
    if (d > (u32)(s32)(s16)DSW(W_REGATHER_DIST)) { memcpy(DSP(W_GATHER_EYE), DSP(eye), 12); return 1; }
    return 0;
}

/* AP 33d1:00f2 */
s16 list_add(FarPtr o, u32 key)
{
    if ((s16)DSW(W_LIST_N) >= (s16)DSW(W_LIST_CAP)) return 0;
    u16 f = rd16(o.seg, o.off);
    if (f & 0x100) { wr16(o.seg, o.off, (u16)(f & ~0x100)); return 0; }
    FarPtr l = far_add(ds_far(W_DRAW_LIST), (u16)(DSW(W_LIST_N) << 3));
    far_wr(l.seg, l.off, o);
    wr32(l.seg, (u16)(l.off + 4), key);
    DSW(W_LIST_N)++;
    return 1;
}

/* the class test of 33d1:0140 / 02eb (render3d_world.md §3.4); *vis set, returns 0 to reject early */
static u8 class_visible(FarPtr o, u32 d, u16 p, u8 big_min)
{
    u16 id = (u16)(rd16(o.seg, (u16)(o.off + 2)) & 0x1FFF);
    u16 c = (u8)shape_class_cached(id);
    u16 e = (u16)(DSW(W_FLT_TAB) + c * 0x55);           /* no bound check on c */
    if (DSB(e) == 0) return 1;
    u16 si = (u16)(e + 3);
    if (DSB(si + 1)) return 0;                             /* class disabled at this detail */
    if ((s16)DSW(si + 0x0A) < 0x3FF) {                     /* density thinning */
        DSW(S_RAND_CURSOR) = (u16)(xor_bytes(p, 12) + 13);
        if ((s16)(rd16(RAND_TABLE_SEG, (u16)((DSW(S_RAND_CURSOR) & 0xFF) * 2)) & 0x3FF) > (s16)DSW(si + 0x0A)) return 0;
    }
    if (DSB(si)) {                                         /* size mode */
        u16 k = (u16)(log_key((u32)shape_radius_world((s16)id)) + (DSW(DSW(W_SCENE_VIEW)) << 8));
        return (s16)(k - log_key(d)) > (s16)DSW(si + 2);
    }
    s32 rw = shape_radius_world((s16)id);                  /* range mode */
    s32 lim = DSSL(DSW(W_FLT_RANGE) + 4 * c);
    if (big_min && rw >= 5000000 && lim < DSSL(W_BIG_RANGE_MIN)) lim = DSSL(W_BIG_RANGE_MIN);
    return (s32)(d - (u32)rw) < lim;
}

/* AP 33d1:0140 — a moving object or effect (key: ground priority or distance) */
u8 scene_add_dynamic(FarPtr o)
{
    u16 f = rd16(o.seg, o.off);
    if (f & 0x100) { wr16(o.seg, o.off, (u16)(f & ~0x100)); return 0; }
    if (f & 0x1000) return 0;
    u16 m = stk_mark(), p = stk_alloc(12);
    memcpy(DSP(p), far_mp(far_add(o, 4)), 12);
    u32 d = ldist3(eye_ofs(), p);
    u8 vis = class_visible(o, d, p, 0);                    /* no big-range minimum here */
    stk_release(m);
    if (!vis) return 0;
    u8 l = (u8)shape_layer_cached((u16)(rd16(o.seg, (u16)(o.off + 2)) & 0x1FFF));
    return (u8)list_add(o, l ? (u32)l : d);
}

/* AP 33d1:02ea — `inc bp` entry of the near routine below */
u8 static_test_add(void) { return static_test_add_body(); }

/* AP 33d1:02eb — class test of DS:369C; visible objects go to the front (ground layer) or the back
 * (distance) fill pointer */
u8 static_test_add_body(void)
{
    FarPtr o = ds_far(W_GATHER_OBJ);
    u16 m = stk_mark(), p = stk_alloc(12);
    memcpy(DSP(p), far_mp(far_add(o, 4)), 12);
    u32 d = ldist3(eye_ofs(), p);
    u8 vis = class_visible(o, d, p, 1);
    stk_release(m);
    if (vis) {
        u8 l = (u8)shape_layer_cached((u16)(rd16(o.seg, (u16)(o.off + 2)) & 0x1FFF));
        if (l) {
            DSW(W_LIST_GROUND_N)++;
            FarPtr f = ds_far(W_LIST_FRONT);
            far_wr(f.seg, f.off, o);
            wr32(f.seg, (u16)(f.off + 4), (u32)l);
            DSW(W_LIST_FRONT) = (u16)(DSW(W_LIST_FRONT) + 8);
        } else {
            DSW(W_LIST_STATIC_N)++;
            DSW(W_LIST_BACK) = (u16)(DSW(W_LIST_BACK) - 8);
            FarPtr b = ds_far(W_LIST_BACK);
            far_wr(b.seg, b.off, o);
            wr32(b.seg, (u16)(b.off + 4), d);
        }
        DSW(W_LIST_N)++;
    }
    return vis;
}

/* AP 33d1:04dc */
void scene_force_rebuild(void) { DSW(W_GATHER_FORCE) = 1; }

/* the "(log_key(size) + S·256) > log_key(d)" test of the macro groups (i16 compare) */
static bool group_big_enough(FarPtr g, u32 d, u16 S)
{
    s16 a = (s16)(log_key(macro_distance(g)) + (u16)(S << 8));
    return a > (s16)log_key(d);
}
/* the "hypot" of the tile test: max + min/4 + min/8 (i32 sar) */
static s32 approx_len(s32 a, s32 b) { return b > a ? (a >> 2) + (a >> 3) + b : (b >> 2) + (b >> 3) + a; }

/* AP 33d1:04e7 (render3d_world.md §3.3) */
void scene_gather_static(u8 keep)
{
    u16 mk = stk_mark(), E = stk_alloc(12);
    memcpy(DSP(E), DSP(eye_ofs()), 12);
    FarPtr list = ds_far(W_DRAW_LIST);
    if (DSW(W_GATHER_FORCE) == 0 && (keep || !scene_eye_moved(DSW(W_SCENE_VIEW)))) {
        /* refresh path: new keys for the static entries; last frame's moving objects are dropped */
        ds_far_wr(W_LIST_BACK, far_add(list, (u16)(DSW(W_LIST_GROUND_N) << 3)));
        for (s16 i = 0; i < (s16)DSW(W_LIST_STATIC_N); i++) {
            FarPtr b = ds_far(W_LIST_BACK);
            u32 d = dist_obj(E, far_rd(b.seg, b.off));
            wr32(b.seg, (u16)(b.off + 4), d);
            DSW(W_LIST_BACK) = (u16)(DSW(W_LIST_BACK) + 8);
        }
        DSW(W_LIST_N) = (u16)(DSW(W_LIST_GROUND_N) + DSW(W_LIST_STATIC_N));
        stk_release(mk);
        return;
    }
    DSW(W_GATHER_FORCE) = 0;
    u16 S = DSW(DSW(W_SCENE_VIEW));
    DSW(W_LIST_N) = 0;
    ds_far_wr(W_LIST_FRONT, list);
    DSW(W_LIST_GROUND_N) = 0;
    ds_far_wr(W_LIST_BACK, far_add(list, (u16)(DSW(W_LIST_CAP) << 3)));
    DSW(W_LIST_STATIC_N) = 0;
    FarPtr P = ds_far(W_STATIC_OBJS);
    s16 i = 0;
    s16 lim = (s16)(DSW(W_LIST_CAP) - DSW(W_LIST_RESERVE));
#define NEXT_OBJ() (ds_far_wr(W_GATHER_OBJ, far_rd(P.seg, P.off)), P = far_add(P, 4), i++, ds_far(W_GATHER_OBJ))

    while (i < (s16)DSW(S_P_STATIC_END)) {
        FarPtr o = NEXT_OBJ();
        if (lnear_obj(E, o, 2000000)) static_test_add();
        if ((s16)DSW(W_LIST_N) >= lim) goto done;
    }
    while (i < (s16)DSW(S_P_BIG_END)) {
        FarPtr o = NEXT_OBJ();
        u32 r = (u32)shape_radius_world((s16)(rd16(o.seg, (u16)(o.off + 2)) & 0x1FFF)) + 4000000;
        if (lnear_obj(E, o, r)) static_test_add();
        if ((s16)DSW(W_LIST_N) >= lim) goto done;
    }
    while (i < (s16)DSW(S_P_REST_END)) {
        FarPtr o = NEXT_OBJ();
        if (lnear_obj(E, o, 2000000)) static_test_add();
        if ((s16)DSW(W_LIST_N) >= lim) goto done;
    }
    while (i < (s16)DSW(S_P_SHORT_END)) {                  /* short macro groups */
        FarPtr g = NEXT_OBJ();
        u32 d = dist_obj(E, g);
        if (d > 1300000) continue;
        if (!group_big_enough(g, d, S)) continue;
        u16 flags = (u16)(0x1800 | (rd16(g.seg, g.off) & 0xFF));
        s16 n = (s16)macro_expand(g);
        FarPtr src = far_make(SEG(0x3B05), 0);
        for (s16 mi = 0; mi < n; mi++, src = far_add(src, 0x16)) {
            u8 layer = (u8)shape_layer_cached(rd16(src.seg, (u16)(src.off + 2)));
            FarPtr a;
            if (layer == 0) {                              /* 3D member: A record */
                a = fp_u32(arena_alloc30());
                if (far_is_null(a)) goto done;
                memcpy(far_mp(a), far_mp(src), 0x16);
                wr16(a.seg, a.off, flags);
                wr8(a.seg, (u16)(a.off + 0x1D), (u8)objtype_class(rd16(a.seg, (u16)(a.off + 2))));
                if (rd8(g.seg, (u16)(g.off + 0x12 + mi)) == 0xFF) {        /* destroyed member */
                    wr16(a.seg, a.off, (u16)(rd16(a.seg, a.off) | 0x40));
                    u16 w = objtype_shape(rd16(a.seg, (u16)(a.off + 2)));
                    wr16(a.seg, (u16)(a.off + 2), w);
                    if (w == 0xFFFF) { arena_free30(); continue; }
                }
                far_wr(a.seg, (u16)(a.off + 0x16), g);
                wr16(a.seg, (u16)(a.off + 0x1A), (u16)mi);
                wr8(a.seg, (u16)(a.off + 0x1C), 0xFF);
            } else {                                       /* flat member: B record */
                a = fp_u32(arena_alloc40());
                if (far_is_null(a)) goto done;
                memcpy(far_mp(a), far_mp(src), 0x16);
                wr16(a.seg, a.off, flags);
            }
            ds_far_wr(W_GATHER_OBJ, a);
            if (lnear_obj(E, a, 1300000) && static_test_add()) {
                if ((s16)DSW(W_LIST_N) >= lim) goto done;
            } else if (layer == 0) arena_free30();
            else arena_free40();
        }
    }
    while (i < (s16)DSW(W_WORLD_N)) {                      /* long (tiled) groups */
        FarPtr g = NEXT_OBJ();
        u32 d = dist_obj(E, g);
        if (d > 2000000) continue;
        if (!group_big_enough(g, d, S)) continue;
        u8 *gp = far_mp(g);
        FarPtr M = far_add(ds_far(W_MACRO_TAB), (u16)((g16(gp, 2) & 0x3FFF) * 0x12));
        s32 Bx = gs32(gp, 4), By = gs32(gp, 8), Xx = gs32(gp, 0x1C), Xy = gs32(gp, 0x20);
        u16 flags = (u16)(0x1800 | (rd16(g.seg, g.off) & 0xFF));
        s32 bx = (s32)rd32(M.seg, (u16)(M.off + 2)), by = (s32)rd32(M.seg, (u16)(M.off + 6));
        s32 Ex = DSSL(E), Ey = DSSL(E + 4), Ez = DSSL(E + 8);
        s32 ks = (s32)(s16)(u16)(log_key((u32)approx_len(bx, by)) << 1);
        for (s32 ty = By - gs32(gp, 0x14); By + Xy > ty; ty += (s32)rd32(M.seg, (u16)(M.off + 6))) {
            for (s32 tx = Bx - gs32(gp, 0x10); Bx + Xx > tx; tx += (s32)rd32(M.seg, (u16)(M.off + 2))) {
                s32 ax = r3d_labs(Ex - tx), ay = r3d_labs(Ey - ty);
                s32 h = approx_len(ax, ay);
                s32 w = approx_len(h, Ez);
                /* PORT: S compared raw (not S << 8) as the original — every shipped tile passes */
                if ((s32)(s16)S < (s32)(s16)log_key((u32)w) - ks) continue;
                FarPtr mem = far_rd(M.seg, (u16)(M.off + 0x0E));
                for (s16 mi = 0; (s16)rd16(M.seg, M.off) > mi; mi++, mem = far_add(mem, 0x16)) {
                    s32 qx = (s32)rd32(mem.seg, (u16)(mem.off + 4)) + tx;
                    s32 qy = (s32)rd32(mem.seg, (u16)(mem.off + 8)) + ty;
                    s32 qz = (s32)rd32(mem.seg, (u16)(mem.off + 0x0C));
                    if (qx < Bx || Bx + Xx < qx || qy < By || By + Xy < qy) continue;
                    FarPtr a = fp_u32(arena_alloc40());
                    if (far_is_null(a)) goto done;
                    memcpy(far_mp(a), far_mp(mem), 0x16);
                    wr16(a.seg, a.off, flags);
                    wr32(a.seg, (u16)(a.off + 4), (u32)qx);
                    wr32(a.seg, (u16)(a.off + 8), (u32)qy);
                    wr32(a.seg, (u16)(a.off + 0x0C), (u32)qz);
                    ds_far_wr(W_GATHER_OBJ, a);
                    if (lnear_obj(E, a, 2000000) && static_test_add()) {
                        if ((s16)DSW(W_LIST_N) >= lim) goto done;
                    } else arena_free40();
                }
            }
        }
    }
#undef NEXT_OBJ
done:
    if (DSW(W_LIST_GROUND_N)) {                            /* order the ground layer once */
        static R3dNode T[TREE_MAX];
        memset(T, 0, sizeof T);
        tree_build((s16)DSW(W_LIST_GROUND_N), list, T);
        for (s16 j = 0; j < (s16)DSW(W_LIST_GROUND_N) && j < TREE_MAX; j++) T[j].ptr = far_rd(T[j].ptr.seg, T[j].ptr.off);
        ds_far_wr(W_FLATTEN_PTR, list);
        tree_flatten(T);
    }
    if (DSW(W_LIST_STATIC_N))                              /* statics right behind the ground layer */
        far_memmove_fp(ds_far(W_LIST_FRONT), ds_far(W_LIST_BACK), (u32)(u16)(DSW(W_LIST_STATIC_N) << 3));
    stk_release(mk);
}

/* in-order walk far subtree, node, near subtree (consumes the tree; §3.8) */
static void tree_walk(R3dNode *T, void (*visit)(R3dNode *))
{
    s16 cur = 0;
    while (cur >= 0) {
        R3dNode *nd = &T[cur];
        u8 f = nd->flags;
        if (f & 4) { cur = nd->farther; nd->flags &= (u8)~4; }
        else if (f & 8) cur = (f & 1) ? nd->parent : -1;
        else {
            visit(nd);
            nd->flags |= 8;
            cur = (f & 2) ? nd->nearer : (f & 1) ? nd->parent : -1;
        }
    }
}
static void visit_flatten(R3dNode *nd)
{
    FarPtr p = ds_far(W_FLATTEN_PTR);
    far_wr(p.seg, p.off, nd->ptr);                         /* object pointer only (keys stay stale) */
    DSW(W_FLATTEN_PTR) = (u16)(DSW(W_FLATTEN_PTR) + 8);
}
/* AP 33d1:0e69 / 0e6a */
void tree_flatten(R3dNode *T) { tree_flatten_body(T); }
void tree_flatten_body(R3dNode *T) { tree_walk(T, visit_flatten); }

/* ================================================================================================
 * 34bf — frame list drawing */

/* AP 34bf:0006 */
void scene_draw(void)
{
    FarPtr list = ds_far(W_DRAW_LIST);
    for (s16 i = 0; i < (s16)DSW(W_LIST_GROUND_N); i++) {
        FarPtr e = far_add(list, (u16)(i * 8));
        draw_entry(far_rd(e.seg, e.off));
    }
    FarPtr cb = ds_far(W_AFTER_GROUND_CB);
    if (!far_is_null(cb)) codeptr_lookup(cb)();
    s16 n = (s16)(DSW(W_LIST_N) - DSW(W_LIST_GROUND_N));
    if (n) {
        /* PORT: 256 nodes; the original's 0x644-byte frame holds 200 and tree_build accepts 256 */
        static R3dNode T[TREE_MAX];
        memset(T, 0, sizeof T);
        tree_build(n, far_add(list, (u16)(DSW(W_LIST_GROUND_N) << 3)), T);
        tree_draw(T);
    }
}

/* AP 34bf:0084 (render3d_world.md §3.7) */
void tree_build(s16 n, FarPtr list, R3dNode *T)
{
    if (n <= 0) return;
    if (n > TREE_MAX) n = TREE_MAX;
    T[0].ptr = list; T[0].parent = T[0].nearer = T[0].farther = 0; T[0].flags = 0;
    for (s16 i = 1; i < n; i++) {
        FarPtr ent = far_add(list, (u16)(i * 8));
        R3dNode *ni = &T[i];
        ni->ptr = ent; ni->parent = ni->nearer = ni->farther = 0; ni->flags = 0;
        s32 key = (s32)rd32(ent.seg, (u16)(ent.off + 4));
        s16 cur = 0;
        do {
            R3dNode *nd = &T[cur];
            FarPtr ce = nd->ptr;
            s32 diff = key - (s32)rd32(ce.seg, (u16)(ce.off + 4));
            FarPtr on = far_rd(ent.seg, ent.off), oc = far_rd(ce.seg, ce.off);
            u16 tn = (u16)(rd16(on.seg, on.off) & 0x1F), tc = (u16)(rd16(oc.seg, oc.off) & 0x1F);
            u8 nearer;
            if (tn == tc) nearer = diff < 0;
            else {
                s32 ad = r3d_labs(diff);
                if (ad >= 10000) nearer = diff < 0;
                else if ((s32)(s16)shape_radius_cached(rd16(on.seg, (u16)(on.off + 2)))
                         + (s32)(s16)shape_radius_cached(rd16(oc.seg, (u16)(oc.off + 2))) < ad)
                    nearer = diff < 0;
                else {
                    R3dCmp c = { tn, tc, diff, on, oc };
                    nearer = (u8)sort_compare(&c);
                }
            }
            if (nearer) {
                if (nd->flags & 2) cur = nd->nearer;
                else { nd->nearer = (u8)i; nd->flags |= 2; ni->parent = (u8)cur; ni->flags |= 1; cur = -1; }
            } else {
                if (nd->flags & 4) cur = nd->farther;
                else { nd->farther = (u8)i; nd->flags |= 4; ni->parent = (u8)cur; ni->flags |= 1; cur = -1; }
            }
        } while (cur >= 0);
    }
}

static void visit_draw(R3dNode *nd) { draw_entry(far_rd(nd->ptr.seg, nd->ptr.off)); }
/* AP 34bf:02a3 / 02a4 */
void tree_draw(R3dNode *T) { tree_draw_body(T); }
void tree_draw_body(R3dNode *T) { tree_walk(T, visit_draw); }

/* ================================================================================================
 * 465c:0589 — pair order of close objects of different types (1 = new is nearer) */
static u16 seg_hits(FarPtr a, FarPtr box_of, FarPtr b)
{
    u16 m = stk_mark(), pa = stk_alloc(12), pb = stk_alloc(12);
    memcpy(DSP(pa), far_mp(far_add(a, 4)), 12);
    memcpy(DSP(pb), far_mp(far_add(b, 4)), 12);
    u16 eye = (u16)(DSW(DSW(S_CAMERA) + 2) + 2);
    u16 r = segment_hits_boxes(pa, eye, rd16(box_of.seg, (u16)(box_of.off + 0x20)), pb,
                               rd16(box_of.seg, (u16)(box_of.off + 0x24)));
    stk_release(m);
    return r;
}
static s32 obj_z(FarPtr o) { return (s32)rd32(o.seg, (u16)(o.off + 0x0C)); }
static s32 eye_z(void) { return DSSL(DSW(DSW(S_CAMERA) + 2) + 0x0A); }

s16 sort_compare(const R3dCmp *c)
{
    u16 code = (u16)(c->tn << 5 | c->tc);
    if (code == 0x6C || code == 0x6E) {                    /* new aircraft, cur 0Ch/0Eh */
        s32 z = obj_z(c->cur);
        if (z == 0) return 1;
        if (z == 1) return 0;
        return (s16)seg_hits(c->cur, c->nw, c->nw);
    }
    if (code == 0x183 || code == 0x1C3) {                  /* new 0Ch/0Eh, cur aircraft */
        s32 z = obj_z(c->nw);
        if (z == 0) return 0;
        if (z == 1) return 1;
        return !seg_hits(c->nw, c->cur, c->cur);
    }
    switch (c->tn) {                                       /* jump table 465c:0802 */
    case 4: case 0x0D:
        if (eye_z() > 1000) return 0;
        return !seg_hits(c->cur, c->nw, c->nw);
    case 0x0B: return 0;
    case 0x0C: case 0x0E: return 1;
    default: break;
    }
    switch (c->tc) {                                       /* jump table 465c:07ec */
    case 4: case 0x0D:
        if (eye_z() > 1000) return 1;
        return !seg_hits(c->nw, c->cur, c->cur);
    case 0x0B: case 0x0C: case 0x0E: return 0;
    default: break;
    }
    return c->diff < 0;
}

/* ================================================================================================
 * 5440 — the 3D frame */

/* AP 5440:0269 — the DS:36CC callback */
void draw_object(FarPtr o)
{
    u16 f = rd16(o.seg, o.off), t = (u16)(f & 0x1F);
    u16 cnt = DSW(S_DRAW_COUNT); DSW(S_DRAW_COUNT)++;
    if (!(cnt & 1)) vcr_poll_panel();
    if (t == 0x0F) parachute_draw(o);                      /* then continues below */
    if (t == 0x0E) { effect_draw(o); return; }
    if (t == 0x12) { far_dot_draw(o); return; }
    if (rd16(o.seg, o.off) & 0x2000) return;
    if (t == 5) { ordnance_draw(o); return; }
    u16 mk = stk_mark(), tmp = stk_alloc(2);               /* the stack byte of the state */
    u16 r = draw_record(o);
    if (rd16(o.seg, o.off) & 0x1000) {
        if (rd8(o.seg, (u16)(o.off + 0x1D)) == 0x0C) {     /* flags on bases */
            if (t == 0x11) { stk_release(mk); return; }
            DSW(r + 0x14) = tmp;
            DSB(tmp) = t == 0x10 ? 0 : 1;
        }
    } else {
        DSW(r + 0x14) = rd16(o.seg, (u16)(o.off + 0x16));
        if (DSW(r + 0x14) < 0x40) { DSB(tmp) = (u8)DSW(r + 0x14); DSW(r + 0x14) = tmp; }
    }
    set_near((u16)(get_near_n() - 3));
    if (t == 0x0B) shadow_draw(r, DSW(S_SHADOW_COL));
    else ts_draw(r);
    reset_near();
    stk_release(mk);
}

/* AP 5440:0375 — range from a size value (sim_world AI spotting) */
u32 dist_limit(u16 v)
{
    s32 r = (s32)(s16)(v + 40) << 8;
    if (r < 100000) r = 100000;
    return (u32)r;
}

/* AP 5440:03aa (render3d_world.md §2.2) */
void render_scene(void)
{
    u16 cam = DSW(S_CAMERA);
    limit_window(cam);
    s16 cy = cockpit_view_window((u16)(cam + 0x0A));
    cam_begin_frame(cam);
    if (DSW(S_CAM_MOVED)) { CSW(CS_CY) = (u16)cy; CSB(CS_FORCE_CLIP) = 1; }
    set_light(DSW(S_SUN_PITCH), DSW(S_SUN_HEADING));
    DRV_CLIP_ON = 1;
    DRV_X1 = (s16)DSW(cam + 0x0A);
    DRV_Y1 = (s16)DSW(cam + 0x0C);
    DRV_Y2 = (s16)(DSW(cam + 0x0C) + DSW(cam + 0x10) - 1);
    DRV_X2 = (s16)(DRV_X1 + (s16)DSW(cam + 0x0E) - 1);
    clip_to_view();
    CSB(CS_FORCE_CLIP) = (u8)(CSB(CS_FORCE_CLIP) + (fullscreen_view() == 0));
    FarPtr sky = ds_far(S_SKY);
    u32 cel = DSB(S_NO_CELESTIAL) ? 0 : u32_fp(codeptr_far(0x3E1D002Au));   /* stub of 6964:01a2 */
    draw_sky(sky.off, sky.seg, CSW(CS_CAM_ANG), CSW(CS_CAM_ANG + 2), cel);
    u16 pl = DSW(S_PLAYER);
    if (!(DSB(pl + 0x1A) & 1)) {
        if (DSW(S_CAM_MOVED)) DSW(pl) |= 0x2000;
        else DSW(DSW(S_PLAYER)) &= (u16)~0x2000;
    }
    ds_far_wr(W_DRAW_CB, codeptr_far(0x3D320070u));        /* stub of 5440:0269 */
    ds_far_wr(W_AFTER_GROUND_CB, codeptr_far(0x3E070020u));   /* stub of 675d:0000 */
    scene_begin(DSW(S_CAMERA));
    if (DSW(S_KEEP)) DSW(S_KEEP)--;
    if (!overcast_split()) {
        if (DSW(S_REBUILD)) { scene_force_rebuild(); DSW(S_REBUILD) = 0; }
        scene_gather_static(DSB(S_KEEP));
    } else {
        DSW(W_LIST_N) = 0; DSW(W_LIST_STATIC_N) = 0; DSW(W_LIST_GROUND_N) = 0;
        DSW(S_REBUILD) = 1;                                /* eye above the overcast */
    }
    u16 eye = (u16)camera_obj_ref();
    range_cull();
    clouds_draw(eye);
    spawn_ground_fires();
    u16 spot = sun_dir();
    s16 k = 0;
    u16 pp = DSW(S_MOVERS);
    for (s16 i = 0; i < (s16)DSW(S_NMOVERS); i++, pp = (u16)(pp + 2)) {
        u16 o = DSW(pp);
        if ((DSB(DSW(o + 0x34) + 0x3B) & 2) && DSB(o + 0x18) != 1) continue;
        u32 d = ldist3(eye, (u16)(o + 4));
        if (DSB(o + 0x18) == 0 && !(DSW(o) & 0x100)) {     /* aircraft */
            s32 R = (s32)(s16)shape_radius_cached(DSW(o + 2)) << 7;
            if (!((u32)(R << 1) > d)) continue;            /* not drawn at all */
            if ((s8)DSB(S_SPOT_ON) && ap_sun_glare_583c((u16)(o + 4), eye, d, spot, DSW(S_PLAYER))) continue;
            if (d < (u32)R) list_add(ds_ptr(o), d);
            else {                                         /* one-pixel dot proxy (no bound check) */
                u16 p = (u16)(DSW(S_PROXIES) + 18 * k++);
                DSW(p) = 0x8012;
                DSW(p + 2) = DSW(o + 2);
                memcpy(DSP(p + 4), DSP(o + 4), 12);
                DSW(p + 0x10) = DSW(DSW(DSW(S_TYPES) + 2 * DSB(S_SIDE_OF + DSB(o + 0x19))) + 0x81);
                list_add(ds_ptr(p), d);
            }
        } else if (DSB(o + 0x18) == 1) { if (d < 250000) list_add(ds_ptr(o), d); }   /* ship */
        else list_add(ds_ptr(o), d);
    }
    s16 own = view_get() == 2 || view_get() == 3;
    for (FarPtr e = fp_u32(pool_next(DSW(S_ORD_POOL), far_make(0, 0))); !far_is_null(e);
         e = fp_u32(pool_next(DSW(S_ORD_POOL), e))) {
        if ((rd16(e.seg, (u16)(e.off + 0x18)) & 0x0F) == DSW(S_FLAK)) flak_visible_test(e, eye);
        else if (!own || rd16(e.seg, (u16)(e.off + 0x16)) != DSW(S_PLAYER)) scene_add_dynamic(e);
    }
    for (FarPtr e = fp_u32(pool_next(DSW(S_FX_POOL), far_make(0, 0))); !far_is_null(e);
         e = fp_u32(pool_next(DSW(S_FX_POOL), e)))
        scene_add_dynamic(e);
    scene_draw();
    CSB(CS_FORCE_CLIP) = (u8)(CSB(CS_FORCE_CLIP) - (fullscreen_view() == 0));
}

/* callbacks stored in DS:36CC / DS:36D0 and passed to draw_sky (through their VROOMM stubs) */
__attribute__((constructor)) static void scene_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x54400269u, (CodeFn)draw_object);
}
