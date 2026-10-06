/* sim_world — impacts and explosions (1560), clouds (490a), segment vs hit box (6ac7).
 * sim_world_wpn.md §9-§10, sim_world_dmg.md §5.4; ported from the disassembly (argument orders of
 * segment_hits_boxes / point_in_boxes follow the pushes, which differ from the spec's lists). */
#include "sim_world/swa_priv.h"
#include "codeptr.h"
#include "prog.h"
#include "enhanced/enhanced.h"   /* ENH: aircraft hit box size (debug) */

#define LD3(a, b) ((s32)ldist3((a), (b)))

u16 shape_shift_cached(u16 ax);          /* render3d 35d6:01b8 (declared in r3d_priv.h only) */
u16 lsub_far(u16 a, FarPtr b);           /* render3d 277a:50f4 (not in render3d.h) */

static FarPtr fp32(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static FarPtr fx_cb(u16 off) { return far_make(SEG(0x3DFF), off); }   /* VROOMM stub entries of 655d */
static bool enh_box_aircraft;            /* ENH: the next segment_hits_boxes tests an aircraft's boxes */

/* ==== 1560 ======================================================================================= */

/* AP 1560:000a — first object hit by the segment from (near) -> to (far); filter 0 planes, 1 ships,
 * else all. Moves the weapon to the hit point, queues the damage event. */
u16 segment_hits_object(u16 from, u32 to, u16 filter, FarPtr w, u16 dmg, u16 hitpos)
{
    u16 mk = stk_mark(), tc = stk_alloc(12);
    memmove(DSP(tc), far_mp(fp32(to)), 12);
    s16 end = DSS(0x4778), si = 0;
    if (filter == 0) end = DSS(0x4776);
    else if (filter == 1) si = DSS(0x4776);
    u16 di = (u16)(DSW(0x475D) + si * 2);
    for (; si < end; si++, di += 2) {
        u16 o = DSW(di);
        if (o == PLAYER && OB(FW(w, 0x16), 0x26) == OB(PLAYER, 0x26)) continue;
        if (si == DSS(0x4776)) hitbox_set_enlarge(0);
        if (FW(w, 0x16) == o) continue;
        if (!obj_alive(o)) continue;
        u16 h;
        enh_box_aircraft = si < DSS(0x4776);                  /* ENH: the planes come first in the list */
        if (OW(o, 0x24)) h = segment_hits_boxes(from, tc, OW(o, 0x20), (u16)(o + 4), OW(o, 0x24));
        else h = (u16)(o + 4);
        enh_box_aircraft = false;                             /* ENH */
        if (!h) continue;
        if (obj_alive(o)) {
            memmove(far_mp(far_add(w, 4)), DSP(h), 12);
            memmove(DSP(hitpos), far_mp(far_add(w, 4)), 12);
            if (dmg) {
                u16 e = dmg_event_alloc();
                if (e) { DSW(e) = w.off; DSW(e + 2) = w.seg; DSW(e + 4) = o; DSW(e + 6) = dmg; }
            }
            stk_release(mk);
            return o;
        }
        u16 bx = (u16)(DSW(0x00EC) + DSW(0x00EE) * 4);
        DSW(bx + 2) = w.seg; DSW(bx) = w.off;
        DSW(0x00EE)++;
        FW(w, 0) |= 0x2000;
        stk_release(mk);
        return 0;
    }
    stk_release(mk);
    return 0;
}

/* AP 1560:0179 — splash (water, sound 17h) or dust (land); returns 1 over land */
u16 splash_or_dust(u32 pos, u16 land_fx, u16 water_fx)
{
    FarPtr p = fp32(pos);
    DSW(0x18CA)++;
    if (DSW(0x18CA) == 6) DSW(0x18CA) = 0;
    memmove(DSP(0x5A3A + DSW(0x18CA) * 12), far_mp(p), 12);
    u16 mk = stk_mark(), r = stk_alloc(0x30);
    memset(DSP(r), 0, 0x30);
    DSW(r) = 0x80C;
    DSW(r + 0x1C) = 0x80;
    DSB(r + 0x1E) = 0;
    memmove(DSP(r + 4), far_mp(p), 12);
    u8 land = (u8)ground_under(pos);
    if (land) DSW(r + 2) = land_fx;
    else {
        DSW(r + 2) = water_fx;
        sx_play(0x17, (u16)(0x5A3A + DSW(0x18CA) * 12), 0, 0);
    }
    if (DSW(r + 2)) {
        FarPtr e = fp32(fx_spawn(r));
        if (!far_is_null(e)) { far_wr(e.seg, (u16)(e.off + 0x18), fx_cb(0x57)); FW(e, 0x16) = 0; }
    }
    stk_release(mk);
    return land;
}

/* AP 1560:024a — short effect attached to the target, offset kept in its frame */
void hit_spark(u16 t, u16 p, u16 fx)
{
    if (PLAYER == t && (s16)fx >= 0x27) return;
    u16 mk = stk_mark(), r = stk_alloc(0x30), d = stk_alloc(12), v = stk_alloc(6), rv = stk_alloc(6);
    memset(DSP(r), 0, 0x30);
    DSW(r) = 0x80C;
    DSW(r + 2) = fx;
    memmove(DSP(r + 4), DSP(p), 12);
    memmove(DSP(d), DSP(r + 4), 12);
    if ((u16)(DSW(0x474D) + 0x27) == fx) {
        u16 x = DSW(p);
        DSW(r + 0x14) = x;
        DSW(r + 2) = (u16)(DSW(r + 2) + (x & 1));
    }
    DSW(r + 0x1C) = 0x80;
    DSB(r + 0x1E) = 0;
    FarPtr e = fp32(fx_spawn(r));
    if (!far_is_null(e)) {
        far_wr(e.seg, (u16)(e.off + 0x18), fx_cb(0x5C));
        FW(e, 0x16) = 0;
        FW(e, 0x20) = t;
        lsub(d, (u16)(t + 4));
        l2v(v, d);
        vec_rotate(v, obj_matrix_t(OW(t, 0x20)), rv);
        FW(e, 0x22) = DSW(rv); FW(e, 0x24) = DSW(rv + 2); FW(e, 0x26) = DSW(rv + 4);
    }
    stk_release(mk);
}

/* AP 1560:0352 — to = from moved to the z = 0 crossing of the segment (from above) */
void splash_point(u16 from, u32 to)
{
    FarPtr t = fp32(to);
    u16 mk = stk_mark(), old = stk_alloc(12);
    memmove(DSP(old), far_mp(t), 12);
    memmove(far_mp(t), DSP(from), 12);
    if (DSSL(from + 8) > 0) {
        lsub(old, from);
        s32 k = idiv32_386((int64_t)(s32)((u32)DSSL(from + 8) << 14), -DSSL(old + 8), NULL);
        FSL(t, 0) += (s32)((u32)DSSL(old) * (u32)k) >> 14;
        FSL(t, 4) += (s32)((u32)DSSL(old + 4) * (u32)k) >> 14;
        FSL(t, 8) = 0;
    }
    stk_release(mk);
}

/* AP 1560:04d7 — explosion effect (z forced to 1), with a smoke column when smoke != 0 */
void explosion_spawn(u16 p, u16 smoke, u16 shape)
{
    u16 mk = stk_mark(), r = stk_alloc(0x30);
    memset(DSP(r), 0, 0x30);
    DSW(r) = 0x800;
    DSW(r + 0x1C) = 0x80;
    DSB(r + 0x1E) = 0;
    DSW(r + 2) = shape;
    memmove(DSP(r + 4), DSP(p), 12);
    DSL(r + 0x0C) = 1;
    FarPtr e = fp32(fx_spawn(r));
    if (!far_is_null(e)) {
        far_wr(e.seg, (u16)(e.off + 0x18), fx_cb(0x57));
        if (smoke) { FW(e, 0) |= 0x0E; FW(e, 0x2A) = smoke; }
        else FW(e, 0) |= 0x0C;
    }
    stk_release(mk);
}

/* AP 1560:03dd — static object hit (HitRef = near ptr to the far 22-byte object entry) */
void ground_object_damage(u16 h, u16 dmg, u16 level, u32 w)
{
    FarPtr o = ds_far(h);
    if (!(FW(o, 2) & 0x8000)) return;
    FarPtr dp = far_make(FW(o, 0x18), (u16)(FW(o, 0x16) + FW(o, 0x1A) + 0x12));
    s32 v = FB(dp, 0);
    u16 mk = stk_mark(), pos = stk_alloc(12);
    memmove(DSP(pos), far_mp(far_add(o, 4)), 12);
    if (v != 0xFF) {
        u16 si = (u16)(0x5A82 + (objtype_class((u16)(FW(o, 2) & 0x1FFF)) << 3));
        if ((s8)DSB(si + 5) <= (s16)level) {
            v += ((s32)(s16)dmg * DSS(si + 6)) >> 10;
            if (v >= 0xFF) {
                FB(dp, 0) = 0xFF;
                if (DSW(si + 2)) explosion_spawn(pos, DSW(si + 2), (u16)(DSW(0x474D) + 0x1C));
            } else FB(dp, 0) = (u8)v;
            ground_credit(fp32(FL(o, 0x16)), (u32)o.seg << 16 | o.off, FW(o, 0x1A), fp32(w), (u16)v);
        }
    }
    stk_release(mk);
}

/* AP 1560:0755 — far object groups (list of far ptrs) with |dx|, |dy| <= r */
u16 collect_objects_near(FarPtr list, u16 n, u16 p, u16 out, u16 max, u32 r)
{
    u16 k = 0;
    while (n--) {
        FarPtr g = far_rd(list.seg, list.off);
        if (rtl_labs(DSSL(p) - FSL(g, 4)) <= (s32)r && rtl_labs(DSSL(p + 4) - FSL(g, 8)) <= (s32)r) {
            DSW(out + 2) = g.seg; DSW(out) = g.off;
            out = (u16)(out + 4);
            k++;
            if ((s16)k >= (s16)max) break;
        }
        list = far_add(list, 4);
    }
    return k;
}

/* AP 1560:07eb — the same over near Plane* lists */
u16 collect_planes_near(u16 list, u16 n, u16 p, u16 out, u16 max, u32 r)
{
    u16 k = 0;
    while (n--) {
        u16 q = DSW(list);
        if (rtl_labs(DSSL(p) - OSL(q, 4)) <= (s32)r && rtl_labs(DSSL(p + 4) - OSL(q, 8)) <= (s32)r) {
            DSW(out) = q; out = (u16)(out + 2);
            k++;
            if ((s16)k >= (s16)max) break;
        }
        list = (u16)(list + 2);
    }
    return k;
}

/* AP 1560:086f — shape bounding box (flag 20h: a cube of radius·94h >> 8) */
void ap_shape_box(u16 shape, u16 lo, u16 hi)
{
    FarPtr s = shape_ptr((s16)shape);
    if (FB(s, 0) & 0x20) {
        s16 v = (s16)(((s32)FS(s, 0x0C) * 0x94) >> 8);
        if (lo) DSW(lo + 4) = DSW(lo + 2) = DSW(lo) = (u16)-v;
        if (hi) DSW(hi + 4) = DSW(hi + 2) = DSW(hi) = (u16)v;
        return;
    }
    if (lo) memmove(DSP(lo), far_mp(far_add(s, 0x0E)), 6);
    if (hi) memmove(DSP(hi), far_mp(far_add(s, 0x14)), 6);
}

static u8 outcode(u16 v, u16 lo, u16 hi)
{
    u8 c = 0;
    if (DSS(lo) > DSSL(v)) c |= 1; else if (DSS(hi) < DSSL(v)) c |= 2;
    if (DSS(lo + 2) > DSSL(v + 4)) c |= 4; else if (DSS(hi + 2) < DSSL(v + 4)) c |= 8;
    if (DSS(lo + 4) > DSSL(v + 8)) c |= 0x10; else if (DSS(hi + 4) < DSSL(v + 8)) c |= 0x20;
    return c;
}

/* AP 1560:08fe — ground object whose box the segment a (far) -> b (far) crosses (low altitude only).
 * Quirk kept: the second end is not rotated into the object frame (the first end is rotated twice). */
u32 segment_hits_ground_object(u32 a, u32 b)
{
    FarPtr fa = fp32(a), fb = fp32(b);
    u16 mk = stk_mark();
    u16 A = stk_alloc(12), B = stk_alloc(12), out = stk_alloc(0x28), lo = stk_alloc(6), hi = stk_alloc(6);
    memmove(DSP(A), far_mp(fa), 12);
    memmove(DSP(B), far_mp(fb), 12);
    if (FSL(fa, 8) > 0x36B) { stk_release(mk); return 0; }
    s32 r = LD3(A, B);
    s16 n = (s16)collect_objects_near(far_make(cur_dgroup, DSW(0x18B0)), DSW(0x18BA), A, out, 10, (u32)r);
    for (u16 di = (u16)(out + n * 4); --n >= 0;) {
        di = (u16)(di - 4);
        FarPtr g = far_make(DSW(di + 2), DSW(di));
        u16 si = FW(g, 2) & 0x1FFF;
        ap_shape_box(si, lo, hi);
        lvsar(A, (u8)shape_shift_cached(si));
        lsub_far(A, far_add(g, 4));
        lrot_z2(A, FW(g, 0x14));
        u8 ca = outcode(A, lo, hi);
        if (ca == 0) { stk_release(mk); return (u32)g.seg << 16 | g.off; }
        memmove(DSP(B), far_mp(fb), 12);
        lvsar(B, (u8)shape_shift_cached(si));
        lsub_far(B, far_add(g, 4));
        lrot_z2(A, FW(g, 0x14));
        u8 cb = outcode(B, lo, hi);
        if (cb == 0 || (ca & cb) == 0) { stk_release(mk); return (u32)g.seg << 16 | g.off; }
        memmove(DSP(A), far_mp(fa), 12);
    }
    stk_release(mk);
    return 0;
}

/* ==== 6ac7: hit boxes ============================================================================ */

/* AP 6ac7:0000 — point p inside any box of the list (object frame): (bxl, obj pos, matrix block, p) */
u16 point_in_boxes(u16 bxl, u16 pos, u16 mblk, u16 p)
{
    u16 mk = stk_mark(), r = stk_alloc(12), v = stk_alloc(6), w = stk_alloc(6);
    memmove(DSP(r), DSP(p), 12);
    lsub(r, pos);
    lvsar(r, (u16)(s16)(s8)DSB(bxl + 1));
    u32 m = (u32)rtl_labs(DSSL(r)) | (u32)rtl_labs(DSSL(r + 4)) | (u32)rtl_labs(DSSL(r + 8));
    u16 res = 0;
    if (!(m & 0xFFFF0000u)) {
        l2v(v, r);
        vec_rotate(v, obj_matrix_t(mblk), w);
        u16 si = DSW(bxl + 6);
        for (s16 i = 0; (s8)DSB(bxl) > i; i++, si = (u16)(si + 12)) {
            if ((u16)(DSW(w) - DSW(si)) < (u16)(DSW(si + 6) - DSW(si)) &&
                (u16)(DSW(w + 2) - DSW(si + 2)) < (u16)(DSW(si + 8) - DSW(si + 2)) &&
                (u16)(DSW(w + 4) - DSW(si + 4)) < (u16)(DSW(si + 0x0A) - DSW(si + 4))) { res = 1; break; }
        }
    }
    stk_release(mk);
    return res;
}

/* AP 6ac7:0115 — one-shot box enlargement for the next segment_hits_boxes */
void hitbox_set_enlarge(u16 t) { DSW(0x51FA) = t; }

/* AP 6ac7:0704 — precision shift for long segments */
static s16 box_near(u16 a, u16 b)
{
    s32 d = LD3(a, b);
    d += d / 8;
    s16 n = lbitlen((u32)d);
    return n > 13 ? (s16)(n - 13) : 0;
}

/* AP 6ac7:03e7 — slab clip of the segment A->B (i16, object frame) against each box; hit point out */
static u8 segment_box_clip(u16 bxl, u16 A, u16 B, u16 out)
{
    u16 bp = DSW(bxl + 6);
    s16 ax = DSS(A), ay = DSS(A + 2), az = DSS(A + 4);
    s16 dx = (s16)(DSS(B) - ax), dy = (s16)(DSS(B + 2) - ay), dz = (s16)(DSS(B + 4) - az);
    s16 tx = 0, ty = 0, tz = 0;
    for (s16 i = 0; (s8)DSB(bxl) > i; i++, bp = (u16)(bp + 12)) {
        s16 lx = DSS(bp), ly = DSS(bp + 2), lz = DSS(bp + 4), hx = DSS(bp + 6), hy = DSS(bp + 8), hz = DSS(bp + 10);
        u8 c = 0, d = 0;
        if (ax > hx) { c |= 1; tx = hx; } else if (ax < lx) { c |= 2; tx = lx; }
        if (ay > hy) { c |= 4; ty = hy; } else if (ay < ly) { c |= 8; ty = ly; }
        if (az > hz) { c |= 0x10; tz = hz; } else if (az < lz) { c |= 0x20; tz = lz; }
        if (c == 0) { DSS(out) = ax; DSS(out + 2) = ay; DSS(out + 4) = az; return 1; }
        s16 bx = DSS(B), by = DSS(B + 2), bz = DSS(B + 4);
        if (bx > hx) d |= 1; else if (bx < lx) d |= 2;
        if (by > hy) d |= 4; else if (by < ly) d |= 8;
        if (bz > hz) d |= 0x10; else if (bz < lz) d |= 0x20;
        if (c & d) continue;
        s16 fx = ((c & 3) && dx) ? ldiv_sat((s32)(s16)(tx - ax) << 14, dx) : -1;
        s16 fy = ((c & 0x0C) && dy) ? ldiv_sat((s32)(s16)(ty - ay) << 14, dy) : -1;
        s16 fz = ((c & 0x30) && dz) ? ldiv_sat((s32)(s16)(tz - az) << 14, dz) : -1;
        s16 si = 0, cx = fx;
        if (fy > fx) { si = 1; cx = fy; }
        if (fz > cx) { si = 2; cx = fz; }
        if (cx == -1) continue;
        if (si != 0) {
            DSS(out) = (s16)(ax + (s16)(((s32)cx * dx) >> 14));
            if ((c & 1) && DSS(out) < lx) continue;
            if ((c & 2) && DSS(out) > hx) continue;
        } else DSS(out) = tx;
        if (si != 1) {
            DSS(out + 2) = (s16)(ay + (s16)(((s32)cx * dy) >> 14));
            if ((c & 4) && DSS(out + 2) < ly) continue;
            if ((c & 8) && DSS(out + 2) > hy) continue;
        } else DSS(out + 2) = ty;
        if (si != 2) {
            DSS(out + 4) = (s16)(az + (s16)(((s32)cx * dz) >> 14));
            if ((c & 0x10) && DSS(out + 4) < lz) continue;
            if ((c & 0x20) && DSS(out + 4) > hz) continue;
        } else DSS(out + 4) = tz;
        return 1;
    }
    return 0;
}

/* AP 6ac7:0120 — segment a->b against the object's .BXL boxes; returns DS:51EE (world hit point) or 0 */
u16 segment_hits_boxes(u16 a, u16 b, u16 mblk, u16 pos, u16 bxl)
{
    if (!bxl) return 0;
    u16 mk = stk_mark();
    u16 A = stk_alloc(12), B = stk_alloc(12), va = stk_alloc(6), vb = stk_alloc(6), ra = stk_alloc(6),
        rb = stk_alloc(6), hit = stk_alloc(6), hw = stk_alloc(6), hdr = stk_alloc(8), boxes = stk_alloc(0x108);
    memmove(DSP(A), DSP(a), 12);
    memmove(DSP(B), DSP(b), 12);
    u16 m = lmid(a, b);
    s32 enh_pct = enh_box_aircraft ? enh_hitbox_size : 100;   /* ENH: --hitbox-size */
    u32 enh_r = DSL(bxl + 2);
    if (enh_pct > 100) enh_r = (u32)((u64)enh_r * (u32)enh_pct / 100);   /* ENH: the reach of larger boxes */
    u32 d1 = (u32)LD3(m, a) + enh_r;
    if (d1 < (u32)LD3(m, pos)) { stk_release(mk); return 0; }
    lsub(A, pos);
    lsub(B, pos);
    s16 n = box_near(A, B);
    s16 sh = (s16)((s8)DSB(bxl + 1) + n);
    lvsar(A, (u16)sh);
    lvsar(B, (u16)sh);
    l2v(va, A);
    l2v(vb, B);
    s16 enl = DSS(0x51FA);
    DSW(0x51FA) = 0;
    if (n != 0 || enl != 0 || enh_pct != 100) {
        DSB(hdr) = DSB(bxl);
        DSB(hdr + 1) = DSB(bxl + 1);
        DSW(hdr + 6) = boxes;
        memmove(DSP(boxes), DSP(DSW(bxl + 6)), (u16)((s8)DSB(hdr) * 12));
        u16 si = boxes;
        for (s16 i = 0; (s8)DSB(hdr) > i; i++, si = (u16)(si + 12)) {
            u16 di = (u16)(si + 6);
            if (n) { vsar(si, (u16)n); vsar(di, (u16)n); }
            if (enl == 1) {
                for (int k = 0; k < 6; k += 2) DSW(si + k) = (u16)(DSW(si + k) + (u16)(DSS(si + k) >> 1));
                for (int k = 0; k < 6; k += 2) DSW(di + k) = (u16)(DSW(di + k) + (u16)(DSS(di + k) >> 1));
            } else if (enl == 2 || enl == 3) {
                vshl(si, (s16)(enl - 1));
                vshl(di, (s16)(enl - 1));
            }
            if (enh_pct != 100)                                 /* ENH: both corners, kept in 16 bits */
                for (int k = 0; k < 12; k += 2) {
                    s32 v = (s32)DSS(si + k) * enh_pct / 100;
                    DSW(si + k) = (u16)(s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                }
        }
        bxl = hdr;
    }
    vec_rotate(va, obj_matrix_t(mblk), ra);
    vec_rotate(vb, obj_matrix_t(mblk), rb);
    u16 res = 0;
    if (segment_box_clip(bxl, ra, rb, hit)) {
        vec_rotate(hit, obj_matrix(mblk), hw);
        v2l(0x51EE, hw);
        s16 s = (s16)((s8)DSB(bxl + 1) + n);
        if (s < enl) lvsar(0x51EE, (u16)(enl - (s8)DSB(bxl + 1) + n));
        else lvshl(0x51EE, (u16)(s - enl));
        ladd(0x51EE, pos);
        res = 0x51EE;
    }
    stk_release(mk);
    return res;
}

/* ==== 490a: clouds =============================================================================== */

#define CSHIFT DSW(0x3ECA)

/* AP 490a:0000 */
void clouds_init(void)
{
    u16 tbl = 0;
    CSHIFT = 0;
    memset(DSP(0x3D74), 0, 0x20);
    s16 w = (s8)DSB(0x477A);
    if (w == 1) { CSHIFT = 0x12; tbl = 0x128; }
    else if (w == 2) { CSHIFT = 0x12; tbl = 0x120; }
    s32 sz = (s32)1 << (CSHIFT & 31);
    DSSL(0x3EC6) = sz;
    DSSL(0x3EC2) = sz - 1;
    DSSL(0x3EBA) = (sz - 1) >> 1;
    DSSL(0x3EBE) = DSSL(0x3EBA) >> 1;
    if (tbl) {
        u16 mk = stk_mark(), c = stk_alloc(8);
        memmove(DSP(c), DSP(tbl), 8);
        normalize_to_sum(c, 4, 0x20);
        u16 si = 0;
        for (s16 k = 0; k < 4; k++)
            for (s16 j = 0; DSS(c + k * 2) > j; j++) DSB(0x3D74 + si++) = (u8)k;
        stk_release(mk);
    }
    for (u16 i = 0; i < 3; i++) {
        u16 o = (u16)(0x3E1E + i * 0x34), blk = (u16)(0x3D9A + i * 0x2A);
        DSW(0x3E18 + i * 2) = o;
        memset(DSP(o), 0, 0x34);
        DSW(o) = 0x880D;
        DSW(o + 2) = (u16)(DSW(0x474D) + i + 1);
        DSW(o + 0x20) = blk;
        obj_set_angles(blk, (u16)(o + 0x10));
        nop_3662(OW(o, 0x20));
        obj_matrix(OW(o, 0x20));
        obj_matrix_t(OW(o, 0x20));
        DSW(o + 0x24) = DSW(0x3D94 + i * 2);
    }
}

/* AP 490a:0132 / 017a */
void clouds_nop(void) {}
void clouds_nop2(void) {}

/* AP 490a:0137 — CLOUD0..2.BXL */
void clouds_load_boxes(void)
{
    u16 si = 0x3D94;
    for (u8 c = '0'; c != '3'; c++, si += 2) {
        DSB(0x0135) = c;
        DSW(si) = bxl_load(res_name(0x0130, 0x05D8)).off;      /* AX only */
    }
}

/* AP 490a:01de */
static u16 cell_floor(u16 out, u16 p)
{
    u32 m = ~DSL(0x3EC2);
    DSL(out) = DSL(p) & m;
    DSL(out + 4) = DSL(p + 4) & m;
    DSL(out + 8) = 0;
    return out;
}

/* AP 490a:021b */
static u16 cell_iter_empty(void) { DSL(0x3D54) = 1; DSL(0x3D60) = 0; return 0; }

/* AP 490a:0234 */
static u16 cell_iter_begin(u16 p, s16 nx, s16 ny)
{
    if (CSHIFT == 0 || DSB(0x42C9) == 0) return cell_iter_empty();
    memmove(DSP(0x3D68), DSP(p), 12);
    u8 cl = (u8)CSHIFT & 31;
    DSL(0x3D50) = DSL(0x3D68);
    DSL(0x3D5C) = DSL(0x3D68) + ((u32)(s32)nx << cl);
    DSL(0x3D54) = DSL(0x3D6C);
    DSL(0x3D60) = DSL(0x3D6C) + ((u32)(s32)ny << cl);
    return 1;
}

/* AP 490a:029b — the (2r-1)^2 cells centred on p */
static u16 cell_iter_around(u16 p, s16 r)
{
    if (CSHIFT == 0 || DSB(0x42C9) == 0) return cell_iter_empty();
    u16 mk = stk_mark(), c = stk_alloc(12);
    u32 o = (u32)(s32)(s16)(r ? r - 1 : 0) << (CSHIFT & 31);
    cell_floor(c, p);
    DSL(c) -= o; DSL(c + 4) -= o;
    s16 n = r ? (s16)(r * 2 - 1) : r;
    u16 v = cell_iter_begin(c, n, n);
    stk_release(mk);
    return v;
}

/* AP 490a:030d — deterministic cloud of a cell (hash of the cell coordinates) */
static u16 cloud_at_cell(u16 cell)
{
    DSW(0x5822) = (u16)(xor_bytes(cell, 12) + 0x0D);
    u16 idx = (u16)((SEGW(0x3A0D, (DSW(0x5822) & 0xFF) * 2)) & 0x1F);
    s16 k = (s8)DSB(0x3D74 + idx);
    if (!k) return 0;
    u16 o = DSW(0x3E16 + k * 2);
    OSL(o, 4) = (s32)(DSL(cell) + (rand_table_pair() & DSL(0x3EBA)) - DSL(0x3EBE));
    OSL(o, 8) = (s32)(DSL(cell + 4) + (rand_table_pair() & DSL(0x3EBA)) - DSL(0x3EBE));
    OSL(o, 0x0C) = 50000;
    return o;
}

/* AP 490a:03ac */
static u16 cell_iter_next(void)
{
    u16 dx = 0;
    while (DSSL(0x3D54) <= DSSL(0x3D60)) {
        if (DSSL(0x3D50) > DSSL(0x3D5C)) {
            DSL(0x3D50) = DSL(0x3D68);
            DSL(0x3D54) += DSL(0x3EC6);
            continue;
        }
        dx = cloud_at_cell(0x3D50);
        DSL(0x3D50) += DSL(0x3EC6);
        if (dx) return dx;
    }
    return dx;
}

/* AP 490a:03fb */
u16 point_in_cloud(u16 p)
{
    cell_iter_around(p, 1);
    for (u16 o; (o = cell_iter_next()) != 0;)
        if (point_in_boxes(OW(o, 0x24), (u16)(o + 4), OW(o, 0x20), p)) return o;
    return 0;
}

/* AP 490a:043e — first cloud whose boxes the segment crosses (long segments never blocked) */
u16 cloud_blocks_segment(u16 a, u16 b)
{
    u16 mk = stk_mark(), lo = stk_alloc(12), hi = stk_alloc(12);
    memmove(DSP(lo), DSP(a), 12);
    memmove(DSP(hi), DSP(b), 12);
    lbox_order(lo, hi);
    cell_floor(lo, lo);
    DSL(hi) += DSL(0x3EC2);
    DSL(hi + 4) += DSL(0x3EC2);
    cell_floor(hi, hi);
    u8 cl = (u8)CSHIFT & 31;
    s16 nx, ny;
    nx = (s16)((DSSL(hi) - DSSL(lo)) >> cl);
    ny = (s16)((DSSL(hi + 4) - DSSL(lo + 4)) >> cl);
    u16 di = 0;
    if (nx >= 0 && ny >= 0 && nx < 0x0F && ny < 0x0F) {
        cell_iter_begin(lo, nx, ny);
        for (u16 o; (o = cell_iter_next()) != 0;) {
            di = segment_hits_boxes(a, b, OW(o, 0x20), (u16)(o + 4), OW(o, 0x24));
            if (di) break;
        }
    }
    stk_release(mk);
    return di;
}

/* AP 490a:017f — the sun direction from the camera blocked by a cloud */
u16 sun_in_cloud(u32 sun)
{
    if (CSHIFT == 0 || DSB(0x42C9) == 0) return 0;
    u16 mk = stk_mark(), p = stk_alloc(12);
    lmove_dir(p, sun_angles_if_up(), sun);
    u16 cam = (u16)camera_obj_ref();
    ladd(p, cam);
    u16 r = cloud_blocks_segment(cam, p) ? 1 : 0;
    stk_release(mk);
    return r;
}

/* AP 490a:0509 — draw the clouds of the 5x5 cells around the camera as effects */
void clouds_draw(u16 cam)
{
    u16 mk = stk_mark(), r = stk_alloc(0x30);
    memset(DSP(r), 0, 0x30);
    DSW(r + 0x1C) = 0x1F4;
    cell_iter_around(cam, 3);
    for (u16 o; (o = cell_iter_next()) != 0;) {
        if (DSB(0x5BE5) && DSL(o + 4) == DSL(0x5600) && DSL(o + 8) == DSL(0x55FC) && (s16)pal_fx_level(6) > 0x3000)
            continue;
        memmove(DSP(r), DSP(o), 0x18);
        fx_spawn(r);
    }
    stk_release(mk);
}
