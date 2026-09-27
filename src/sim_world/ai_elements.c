/* sim_world part B — elements (sections), spotting, pairing (583c). sim_world_ai.md §6; every function
 * checked against the disassembly. Element records: far array DS:47E2 of DS:47E0 × 16 bytes {+0 used,
 * +1 i8 n, +2 Plane* m[4], +0A front, +0C side, +0E rear scan timers}. */
#include "sim_world/swb_priv.h"

#define E_COUNT  DSW(0x47E0)
static inline FarPtr elems(void) { return ds_far(0x47E2); }
static inline void far_to_near(u16 dst, FarPtr src, u16 n) { memcpy(DSP(dst), far_mp(src), n); }
static inline u16 bitbyte(u16 q) { return (u16)((s16)OC(q, 0xC9) >> 3); }
static inline u8 bitmask(u16 q) { return (u8)(1 << (OB(q, 0xC9) & 7)); }

/* 583c:0000 first_alive(f, i) — first index >= i of a member alive and not going down; −1 */
u16 first_alive(u16 f, u16 i)
{
    for (s16 k = (s16)i; OS(f, 6) > k; k++) {
        u16 m = DSW(OW(f, 8) + 2 * k);
        if (obj_alive(m) && !(OB(m, 0x1A) & 0x40)) return (u16)k;
    }
    return 0xFFFF;
}

/* 583c:0050 — first unused record (past the end when none) */
static FarPtr element_alloc(void)
{
    FarPtr e = elems();
    for (s16 i = 0; i < (s16)E_COUNT; i++, e = far_add(e, 16))
        if (FC(e, 0) == 0) break;
    return e;
}

/* 583c:0084 — (sic: counts up to the number of planes DS:4776, not DS:47E0; they are equal) */
static void elements_clear(u16 f)
{
    FarPtr e = elems();
    for (s16 i = 0; i < (s16)DSW(0x4776); i++, e = far_add(e, 16)) {
        if (FB(e, 0) == 0) continue;
        if ((s16)find_int(OW(f, 8), FW(e, 2), OW(f, 6)) >= 0) memset(far_mp(e), 0, 16);
    }
}

/* 583c:00df element_accepts(far e, p, i32 *best) */
static u16 element_accepts(FarPtr e, u16 p, u16 best)
{
    s16 n = (s16)FC(e, 1);
    u16 ok = 0;
    s32 far_ = -1, bestv = DSSL(best);
    while (--n >= 0) {
        u16 m = FW(e, 2 + 2 * n);
        s32 d = dist2(m, p);
        if (d > far_) {
            far_ = d;
            if (d > bestv) break;
        }
        if (d < 18000) ok = 1;
    }
    if (ok) DSSL(best) = far_;
    return ok;
}

/* 583c:0176 — quirk: no bound check (a fifth member would overwrite the scan timers) */
static void element_add(FarPtr e, u16 p)
{
    s8 n = FC(e, 1);
    FB(e, 1) = (u8)(n + 1);
    FW(e, 2 + 2 * n) = p;
    FB(e, 0) = 1;
}

/* 583c:0199 element_pair(e (near copy), mode) — mode 2: Japanese three-plane shotai */
static void element_pair(u16 e, u16 mode)
{
    s8 n = (s8)DSB(e + 1);
    if (n == 0) return;
    u16 m0 = DSW(e + 2);
    OW(m0, 0xDD) = 0;
    OB(m0, 0x1A) &= 0xDF;
    if (n <= 1) return;
    u16 m1 = DSW(e + 4);
    OW(m1, 0xDD) = m0;
    OB(m1, 0x1A) &= 0xDF;
    OB(m1, 0x1A) &= 0x7F;
    if (n <= 2) return;
    u16 m2 = DSW(e + 6);
    if (mode == 2) {
        OW(m2, 0xDD) = m0;
        OB(m2, 0x1A) |= 0x20;
        OB(m1, 0x1A) |= 0x80;
        return;
    }
    OW(m2, 0xDD) = 0;
    OB(m2, 0x1A) &= 0xDF;
    if (n <= 3) return;
    u16 m3 = DSW(e + 8);
    OW(m3, 0xDD) = m2;
    OB(m3, 0x1A) &= 0xDF;
    OB(m3, 0x1A) &= 0x7F;
}

/* 583c:0212 elements_build(f, force) — §6.2 */
static u16 elements_build(u16 f, u16 force)
{
    u16 mk = stk_mark(), mask = stk_alloc(6), best = stk_alloc(4), loc = stk_alloc(16);
    FarPtr made[10];                                       /* bp-2E: 40 bytes */
    s16 k = 0, i = 0, j;
    memset(DSP(mask), 0, 6);
    while ((j = (s16)first_alive(f, (u16)i)) >= 0) {
        i = j;
        FarPtr chosen = { 0, 0 };
        u16 p = DSW(OW(f, 8) + 2 * i);
        i++;
        DSSL(best) = 0x7FFFFFFF;
        for (s16 x = 0; x < k; x++)
            if (element_accepts(made[x], p, best)) chosen = made[x];
        if (far_is_null(chosen)) {
            chosen = element_alloc();
            if (k < 10) made[k] = chosen;                   /* PORT: bound of the 10-entry local list */
            k++;
        }
        element_add(chosen, p);
        DSB(mask + bitbyte(p)) |= bitmask(p);
    }
    s16 first = -1;
    u16 old80 = OB(f, 0x3A) & 0x80;
    OB(f, 0x3A) |= 0x80;
    for (s16 x = 0; x < k && x < 10; x++) {
        far_to_near(loc, made[x], 16);
        for (s16 y = 0; (s16)DSC(loc + 1) > y; y++) {
            u16 m = DSW(loc + 2 + 2 * y);
            u16 bb = bitbyte(m);
            u8 bm = bitmask(m);
            if (!plane_is_down(m)) {
                if (first < 0) first = x;
                else if (first != x) OB(f, 0x3A) &= 0x7F;
            }
            DSB(mask + bb) ^= bm;
            bytes_or((u16)(m + 0xC3), (u16)(m + 0xC3), mask, 6);
            DSB(mask + bb) ^= bm;
        }
        force |= (old80 == 0 && (OB(f, 0x3A) & 0x80) && !(OB(f, 0x3B) & 1));
        if (force) element_pair(loc, (OW(f, 6) == 3 && OB(f, 0x42) == 1) ? 2 : 1);
    }
    stk_release(mk);
    return (u16)k;
}

/* 583c:042b */
void elements_rebuild(u16 f, u16 force_pair)
{
    elements_clear(f);
    elements_build(f, force_pair);
}

/* 583c:0448 — the aircraft flights, last first */
static void elements_build_all(void)
{
    for (s16 i = (s16)DSW(0x4763); --i >= 0;)
        elements_rebuild(DSW(DSW(0x4751) + 2 * (DSW(0x4765) + i)), 0);
}

/* 583c:0471 */
u16 sun_dir(void)
{
    u16 v = sun_vector_if_up();
    if (!v) return 0;
    memcpy(DSP(0x47D6), DSP(v), 6);
    vsar(0x47D6, 4);
    return 0x47D6;
}

/* 583c:04a9 sun_glare(a, b, d, sun, p) — looking into the sun along a − b */
u16 ap_sun_glare_583c(u16 a, u16 b, u32 d32, u16 sun, u16 p)
{
    s32 d = (s32)d32;
    if (!sun || d <= 3000 || d >= 100000) return 0;
    u16 mk = stk_mark(), v = stk_alloc(12), w = stk_alloc(6);
    memcpy(DSP(v), DSP(a), 12);
    lsub(v, b);
    lvsar(v, 3);
    d >>= 3;
    l2v(w, v);
    s32 c = ldiv_sat((s32)vdot(w, sun), (s16)d);
    stk_release(mk);
    return (s32)PW(p, 0x48) < c;
}

/* 583c:056d in_cone(ang, cone) */
u16 in_cone(u16 ang, u16 cone)
{
    s16 lo = DSS(cone), hi = DSS(cone + 2);
    if (lo > hi) { s16 t = lo; lo = hi; hi = t; }
    if ((u16)(DSW(ang) - lo) >= (u16)(hi - lo)) return 0;
    s16 a = DSS(ang + 4);
    u16 gt = a > DSS(cone + 4), lt = a < DSS(cone + 6);
    if (DSS(cone + 4) > DSS(cone + 6)) return gt || lt;
    return gt && lt;
}

/* 583c:0600 — the two 8-byte cones at .DAT+5B */
static u16 in_blind_spot(u16 ang, u16 p)
{
    u16 c = (u16)(DAT(p) + 0x5B);
    for (int i = 0; i < 2; i++, c = (u16)(c + 8))
        if (in_cone(ang, c)) return 1;
    return 0;
}

/* 583c:0649 body_angles(out, tgt, eye, mblk) */
void body_angles(u16 out, u16 tgt, u16 eye, u16 m)
{
    u16 mk = stk_mark(), d = stk_alloc(12), v = stk_alloc(6), r = stk_alloc(6), e = stk_alloc(12);
    memcpy(DSP(d), DSP(tgt), 12);
    lsub(d, eye);
    while (!linbox(d, 16000)) lvsar(d, 1);
    l2v(v, d);
    vec_rotate(v, obj_matrix_t(m), r);
    memcpy(DSP(e), DSP(eye), 12);
    ladd_v(e, r);
    angles_to(out, e, eye);
    stk_release(mk);
}

/* 583c:06ef glare_blocks(p, tgt) */
u16 glare_blocks(u16 p, u16 tgt)
{
    u16 sun = sun_dir();
    if (!sun) return 0;
    u32 d = ldist2(tgt, (u16)(p + 4));
    return ap_sun_glare_583c(tgt, (u16)(p + 4), d, sun, p) ? 1 : 0;
}

/* 583c:0749 can_see(p, t) (RNG) */
u16 can_see(u16 p, u16 t)
{
    u16 tgt = OW(p, 0xD7);
    s16 r = PW(p, 4);
    if (glare_blocks(p, (u16)(t + 4))) return 0;
    u16 mk = stk_mark(), a = stk_alloc(6);
    body_angles(a, (u16)(t + 4), (u16)(p + 4), OW(p, 0x20));
    u16 blind = in_blind_spot(a, p);
    stk_release(mk);
    if (blind) return 0;
    if (tgt && (u16)(OW(p, 0xD1) + 0x2000) < 0x4000) {
        s16 k;
        if (OSL(p, 0xD3) < 8000) {
            r = (s16)(((s32)r * (s16)(0x400 - PW(p, 0x1E))) >> 10);
            k = 200;
        } else {
            k = 400;
        }
        r = (s16)(((s32)r * k) >> 10);
    }
    return (s16)(RNG() & 0x3FF) < r;                                              /* RNG */
}

/* 583c:0834 */
u16 my_wingman(u16 p)
{
    u16 f = FLT(p);
    for (s16 i = 0; OS(f, 6) > i; i++) {
        u16 q = DSW(OW(f, 8) + 2 * i);
        if (!plane_is_down(q) && OW(q, 0xDD) == p) return q;
    }
    return 0;
}

/* 583c:0887 radio_bandit_call(t, by) — §6.5 */
static void radio_bandit_call(u16 t, u16 by)
{
    u16 mk = stk_mark(), a = stk_alloc(6), rec = radio_rec(0x18);
    angles_to(a, (u16)(t + 4), (u16)(PLAYER + 4));
    u16 rel = (u16)(DSW(a + 4) - OW(PLAYER, 0x14));
    if ((u16)(rel + 0x5500) >= 0xAA00) {
        u16 b = 0;
        DSW(rec + 0x0A) = OB(t, 0x19);
        u16 ph = OW(PLAYER, 0x14);
        for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
            u16 q = DSW(DSW(0x475B) + 2 * i);
            u16 c = 0;
            if (q == t) c = 1;
            else if (OW(q, 0xD7) == PLAYER) {
                u16 r2 = (u16)(ph - (u16)(OW(q, 0xD1) + OW(q, 0x14) + 0x8000));
                if (OSL(q, 0xD3) < 10000 && (u16)(r2 + 0x5500) >= 0xAA00) c = 1;
            }
            if (c) {
                DSB(rec + 0x0C)++;
                if (OB(q, 0x42) & 0x20) b++;
            }
        }
        u16 id;
        if (by) { DSB(rec + 0x0C) = 1; id = 0x1C; }
        else if (b != 0 && (s8)DSB(rec + 0x0C) > 1) id = (u16)((s16)OC(PLAYER, 0x26) + 0x1F);
        else id = 0x1E;
        DSSL(rec + 2) = OSL(t, 0x0C) - OSL(PLAYER, 0x0C);
        DSSL(rec + 6) = OSL(t, 0x0C);
        DSW(rec) = rel;
        DSW(rec + 0x16) = by ? by : PLAYER;
        radio_queue7(id, rec, 0);
    }
    stk_release(mk);
}

/* 583c:09de ai_spot_reaction(p, t) — §6.4 */
u16 ai_spot_reaction(u16 p, u16 t)
{
    u16 w = my_wingman(p);
    u16 cur = OW(p, 0xD7);
    u16 seen = 0;
    if (OB(p, 0x43) & 2) return 1;
    if (CLS(p) != 0 && !glare_blocks(p, (u16)(t + 4))) {
        if (p == PLAYER) radio_bandit_call(t, 0);
        seen = 1;
    }
    if (cur == 0 && (OB(p, 0xC3 + bitbyte(t)) & bitmask(t))) seen = 1;
    else if (OW(p, 0xD7) == t) seen = 1;
    else if (w && (u32)dist2(w, p) < 10000) {
        if (OW(w, 0xD7) == t || can_see(w, t)) {
            if (p == PLAYER) radio_bandit_call(t, w);
            seen = 1;
        }
    }
    if (!seen && can_see(p, t)) seen = 1;
    if (seen) {
        OB(p, 0xC3 + bitbyte(t)) |= bitmask(t);
        if (w) {
            OB(w, 0xC3 + bitbyte(t)) |= bitmask(t);
            alert_flight(w);
        }
    }
    return seen;
}

/* 583c:0b51 spotted_split(out, p, &n_friend) — enemies packed at the front, friends from the back */
u16 spotted_split(u16 out, u16 p, u16 n_friend)
{
    u16 mk = stk_mark(), l = stk_alloc(0x60), tmp = stk_alloc(0x60);
    s16 n = (s16)bits_to_list(l, (u16)(p + 0xC3), 0x30);
    gather(tmp, DSW(0x475B), l, (u16)n);
    s16 side = (s16)OC(p, 0x26);
    s16 ne = 0, nf = 0;
    u16 dst = out, back = (u16)(out + 2 * n);
    for (s16 i = 0; i < n; i++) {
        u16 q = DSW(tmp + 2 * i);
        if ((s16)OC(q, 0x26) + side == 1) { DSW(dst) = q; dst = (u16)(dst + 2); ne++; }
        else { back = (u16)(back - 2); nf++; DSW(back) = q; }
    }
    if (n_friend) DSW(n_friend) = (u16)nf;
    stk_release(mk);
    return (u16)ne;
}

/* 583c:0c01 */
static void positions_of(u16 out, u16 l, s16 n)
{
    for (s16 i = 0; i < n; i++, l = (u16)(l + 2), out = (u16)(out + 12)) memcpy(DSP(out), DSP(DSW(l) + 4), 12);
}

/* 583c:0c40 element_centre(out, far e) */
static u16 element_centre(u16 out, FarPtr e)
{
    u16 mk = stk_mark(), loc = stk_alloc(16), pos = stk_alloc(0x30);
    far_to_near(loc, e, 16);
    positions_of(pos, (u16)(loc + 2), (s16)DSC(loc + 1));
    u16 c = bounding_centre(pos, (u16)(s16)DSC(loc + 1));
    memcpy(DSP(out), DSP(c), 12);
    stk_release(mk);
    return out;
}

/* 583c:0c91 select_sector(out, bear, k, n, head, sector) */
static s16 select_sector(u16 out, u16 bear, s16 k, s16 n, u16 head, u16 sector)
{
    head = (u16)(head + 0x2000);
    for (s16 i = 0; i < n; i++, bear = (u16)(bear + 2))
        if (((u16)(DSW(bear) - head) & 0xC000) == sector) { DSW(out + 2 * k) = (u16)i; k++; }
    return k;
}

/* 583c:0cd4 min_field(l, n, off, &idx) — index of the last minimum */
static s16 min_field(u16 l, s16 n, u16 off, u16 idx)
{
    s16 m = 0x7FFF, k = 0;
    for (s16 i = 0; i < n; i++, l = (u16)(l + 2)) {
        u16 a = (u16)(DSW(l) + off);
        if (DSS(a) <= m) { m = DSS(a); k = i; }
    }
    if (idx) DSW(idx) = (u16)k;
    return m;
}

/* 583c:0d1d max_field */
static s16 max_field(u16 l, s16 n, u16 off, u16 idx)
{
    s16 m = (s16)0x8001, k = 0;
    for (s16 i = 0; i < n; i++, l = (u16)(l + 2)) {
        u16 a = (u16)(DSW(l) + off);
        if (DSS(a) >= m) { m = DSS(a); k = i; }
    }
    if (idx) DSW(idx) = (u16)k;
    return m;
}

/* 583c:0d66 pilots_of(out, l, n) — the pilots of the planes other than the player, in reverse order */
static s16 pilots_of(u16 out, u16 l, s16 n)
{
    s16 c = 0;
    u16 q = (u16)(l + 2 * n);
    while (q = (u16)(q - 2), --n >= 0) {
        u16 pl = DSW(q);
        if (pl == PLAYER) continue;
        DSW(out) = PIL(pl);
        out = (u16)(out + 2);
        c++;
    }
    return c;
}

/* 583c:0daa rear_watchers(out[2], m, n) — pairs have no rear watcher (arithmetic quirk kept) */
static s16 rear_watchers(u16 out, u16 m, s16 n)
{
    if (n == 2) {
        DSW(out) = DSW(m + 2);
    } else if (n == 3 || n == 4) {
        DSW(out + 2) = DSW(m + 2 + 2 * (n == 4 ? 1 : 0) + 2);
        DSW(out) = DSW(m + 2);
    }
    s16 r = (s16)((n - 1) & 0xFFFE);
    if (DSW(out) == PLAYER) {
        s16 o = r--;
        if (o) DSW(out) = DSW(out + 2);
    } else if (DSW(out + 2) == PLAYER) {
        r--;
    }
    return r;
}

/* 583c:0e10 evasive_pilots(out, l, n) — planes whose pilot +41 != 0 */
static s16 evasive_pilots(u16 out, u16 l, s16 n)
{
    s16 c = 0;
    while (--n >= 0) {
        u16 q = DSW(l);
        l = (u16)(l + 2);
        if (DSB(PIL(q) + 0x41) != 0) { DSW(out) = q; out = (u16)(out + 2); c++; }
    }
    return c;
}

/* 583c:0e4d spot_range(eye, e (near), c, r) */
static s16 spot_range(u16 eye, u16 e, u16 c, s16 r)
{
    if (glare_blocks(eye, c)) return 0;
    if (CLS(DSW(e + 2)) == 3) r = (s16)(((s32)r * 1500) >> 10);
    r = (s16)(((s32)r * DSS(0x858 + 2 * (s16)DSC(0x477A))) >> 10);
    r = (s16)(((s32)r * PW(eye, 0)) >> 10);
    return r;
}

/* 583c:0ef0 element_visual_size(e (near)) */
static u32 element_visual_size(u16 e)
{
    s32 r = 0;
    for (s16 i = 0; (s16)DSC(e + 1) > i; i++) {
        s16 v = (s16)shape_radius_cached(OW(DSW(e + 2 + 2 * i), 2));
        if ((s32)v > r) r = v;
    }
    return dist_limit((u16)r);
}

/* 583c:0f46 element_spots(o (near), bearing, d) — spotter element DS:47DE, heading DS:47DC */
static u16 element_spots(u16 o, u16 bearing, s32 d)
{
    u16 e = DSW(0x47DE);
    u16 rel = (u16)(bearing - DSW(0x47DC));
    if (DSB(e + 1) == 1 && DSW(e + 2) == PLAYER) return 0;
    u16 front = (u16)(rel + 0x4000) < 0x8000;
    if (front && d < 3000) return 1;
    u16 mk = stk_mark(), w = stk_alloc(8), pl = stk_alloc(8), idx = stk_alloc(2), c = stk_alloc(12),
        ba = stk_alloc(6);
    s16 k = 0;
    memset(DSP(w), 0, 8);                                  /* PORT: rear_watchers leaves it unset for n = 1 */
    if (!front) k = rear_watchers(w, (u16)(e + 2), (s16)DSC(e + 1));
    if (front || k == 0) {
        k = 0;
        for (s16 i = 0; (s16)DSC(e + 1) > i; i++) {
            u16 m = DSW(e + 2 + 2 * i);
            if (m != PLAYER) { DSW(w + 2 * k) = m; k++; }
        }
    }
    s16 np = pilots_of(pl, w, k);
    DSW(idx) = 0;
    min_field(pl, np, 2, idx);
    u16 eye = DSW(w + 2 * DSS(idx));              /* (sic: an index into the reversed pilot list) */
    element_centre(c, ds_ptr(o));
    u16 r = 0;
    if (!cloud_blocks_segment(c, (u16)(eye + 4))) {
        body_angles(ba, c, (u16)(eye + 4), OW(eye, 0x20));
        if (!in_blind_spot(ba, eye)) {
            s32 sz = (s32)element_visual_size(o) >> 3;
            s16 rr;
            if ((u16)(rel + 0x2000) < 0x4000) rr = (s16)sz;
            else if ((u16)(rel + 0x6000) >= 0xC000) rr = (s16)(((s32)(s16)sz * 600) >> 10);
            else rr = (s16)(((s32)(s16)sz * 400) >> 10);
            s16 rng_ = spot_range(eye, o, c, rr);
            r = (d >> 3) < (s32)rng_;
        }
    }
    stk_release(mk);
    return r;
}

/* 583c:1110 contact_report(out, eye, e (near)) */
static u16 contact_report(u16 out, u16 eye, u16 e)
{
    u16 mk = stk_mark(), c = stk_alloc(12);
    element_centre(c, ds_ptr(e));
    DSW(out) = (u16)(bearing_between(c, (u16)(eye + 4)) - OW(eye, 0x14));
    DSSL(out + 6) = DSSL(c + 8);
    DSSL(out + 2) = DSSL(c + 8) - OSL(eye, 0x0C);
    DSW(out + 0x0A) = OB(DSW(e + 2), 0x19);
    DSB(out + 0x0C) = DSB(e + 1);
    DSB(out + 0x0D) = 0;
    DSL(out + 0x0E) = 0;
    DSL(out + 0x12) = ldist2((u16)(eye + 4), c);
    stk_release(mk);
    return out;
}

/* 583c:119b element_mark_seen(o (near), quiet) — spotter element DS:47DE */
static void element_mark_seen(u16 o, u16 quiet)
{
    u16 mk = stk_mark(), loc = stk_alloc(16), mask = stk_alloc(6), tmp = stk_alloc(6), rec = radio_rec(0x18);
    memcpy(DSP(loc), DSP(DSW(0x47DE)), 16);
    memset(DSP(mask), 0, 6);
    for (s16 i = 0; (s16)DSC(o + 1) > i; i++) {
        u16 m = DSW(o + 2 + 2 * i);
        DSB(mask + bitbyte(m)) |= bitmask(m);
    }
    for (s16 i = 0; (s16)DSC(loc + 1) > i; i++) {
        u16 o0 = DSW(o + 2);
        u16 q = DSW(loc + 2 + 2 * i);
        u16 enemy = (s16)OC(o0, 0x26) + (s16)OC(q, 0x26) == 1;
        bytes_and(tmp, (u16)(q + 0xC3), mask, 6);
        if (all_zero(tmp, 6) && q != PLAYER && FLT(q) == FLT(PLAYER) && quiet == 0 &&
            (u32)dist2(PLAYER, q) < 10000) {
            contact_report(rec, PLAYER, o);
            radio_queue5(enemy ? 1 : CLS(o0) == 3 ? 2 : 3, rec, 0);
        }
        bytes_or((u16)(q + 0xC3), (u16)(q + 0xC3), mask, 6);
        if (enemy) alert_flight(q);
    }
    stk_release(mk);
}

/* 583c:132f player_contact_report(rel, &bear, &pos) — radio orders 45h–48h */
u16 player_contact_report(s16 rel, u16 out_bear, u16 out_pos)
{
    u16 mk = stk_mark(), own = stk_alloc(16), loc = stk_alloc(16), c = stk_alloc(12);
    FarPtr list[16];                                       /* bp-38: 12 far entries in the original */
    u16 flags = 0;
    u16 h = (u16)(OW(PLAYER, 0x14) + rel);
    s16 n = 0;
    memset(DSP(own), 0, 16);                              /* PORT: uninitialised when the player has no element */
    DSW(0x47DE) = own;
    FarPtr e = elems();
    for (s16 i = 0; i < (s16)E_COUNT; i++, e = far_add(e, 16)) {
        if (FB(e, 0) == 0) continue;
        far_to_near(loc, e, 16);
        if ((s16)find_int((u16)(loc + 2), PLAYER, (u16)(s16)FC(e, 1)) >= 0) {
            memcpy(DSP(own), DSP(loc), 16);
            continue;
        }
        element_centre(c, e);
        if (ldist2(c, (u16)(PLAYER + 4)) >= element_visual_size(loc)) continue;
        u16 m0 = FW(e, 2);
        u16 b = (u16)(bearing_between((u16)(m0 + 4), (u16)(PLAYER + 4)) - h);
        if ((u16)(b + 0x2AF8) >= 0x55F0) continue;
        if (n < 16) list[n] = e;
        n++;
        if (OB(m0, 0x26) != OB(PLAYER, 0x26)) {
            if (!(flags & 1)) {
                flags |= 1;
                if (out_pos) memcpy(DSP(out_pos), DSP(c), 12);
                if (out_bear) DSW(out_bear) = (u16)(b + rel);
            }
        } else {
            flags |= 2;
        }
    }
    for (s16 i = 0; i < n && i < 16; i++) {
        far_to_near(loc, list[i], 16);
        element_mark_seen(loc, 1);
    }
    stk_release(mk);
    return flags;
}

/* 583c:14e9 element_scan(far e) — §6.3 */
static void element_scan(FarPtr ef)
{
    u16 mk = stk_mark(), e = stk_alloc(16), pl = stk_alloc(8), w = stk_alloc(8), c = stk_alloc(12),
        oc = stk_alloc(12), bear = stk_alloc(0x60), dist = stk_alloc(0xC0), sel = stk_alloc(0x60),
        cand = stk_alloc(16);
    FarPtr ce[48];
    memset(DSP(w), 0, 8);                                  /* PORT: rear_watchers leaves it unset for n = 1 */
    far_to_near(e, ef, 16);
    s16 n = (s16)DSC(e + 1);
    u16 A = timer16_dec((u16)(e + 0x0A)) == 0;
    if (A) DSW(e + 0x0A) = 7500;
    u16 B = timer16_dec((u16)(e + 0x0C)) == 0;
    if (B) {
        s16 np = pilots_of(pl, (u16)(e + 2), n);
        /* PORT: min_field is called with three words here; the fourth (the index pointer) is the caller's
         * saved DI, so the original stores the index at DS:[DI] when DI != 0 (register state of the frame
         * step, not modelled). No index is written. */
        s16 m = min_field(pl, np, 2, 0);
        s16 r = (s16)(RNG() & 0x3FF);                                              /* RNG */
        s16 t = (s16)(((s32)m * r) >> 10);
        DSW(e + 0x0C) = (u16)((s16)(((s32)t * 4500) >> 10) + 7500);
    }
    u16 C = timer16_dec((u16)(e + 0x0E)) == 0;
    if (C) {
        s16 m, k = rear_watchers(w, (u16)(e + 2), n);
        if (k) {
            s16 np = pilots_of(pl, w, k);
            m = min_field(pl, np, 2, 0);                   /* PORT: see above */
        } else {
            m = 0x400;
        }
        s16 v = evasive_pilots(w, (u16)(e + 2), n);
        if (v) {
            s16 np = pilots_of(pl, w, v);
            s16 m2 = min_field(pl, np, 2, 0);              /* PORT: see above */
            if (m2 < m) m = m2;
        }
        s16 r = (s16)(RNG() & 0x3FF);                                              /* RNG */
        s16 t = (s16)(((s32)m * r) >> 10);
        DSW(e + 0x0E) = (u16)((s16)(((s32)t * 9000) >> 10) + 7500);
    }
    if (A || B || C) {
        element_centre(c, ef);
        u16 head = OW(DSW(e + 2), 0x14);
        s16 nc = 0;
        FarPtr o = elems();
        for (s16 i = 0; i < (s16)E_COUNT; i++, o = far_add(o, 16)) {
            if (FB(o, 0) == 0) continue;
            if (o.off == ef.off && o.seg == ef.seg) continue;
            element_centre(oc, o);
            if (DSSL(oc + 8) >= 100000000) continue;
            s32 d = dist2v(oc, c);
            if (d >= 50000) continue;
            if (nc < 48) {
                ce[nc] = o;
                DSW(bear + 2 * nc) = bearing_between(oc, c);
                DSSL(dist + 4 * nc) = d;
            }
            nc++;
        }
        if (nc > 48) nc = 48;                             /* PORT: bound of the local lists */
        if (nc) {
            s16 ns = 0;
            if (A) ns = select_sector(sel, bear, 0, nc, head, 0);
            if (B) {
                ns = select_sector(sel, bear, ns, nc, head, 0xC000);
                ns = select_sector(sel, bear, ns, nc, head, 0x4000);
            }
            if (C) ns = select_sector(sel, bear, ns, nc, head, 0x8000);
            DSW(0x47DE) = e;
            DSW(0x47DC) = head;
            for (s16 i = 0; i < ns; i++) {
                s16 x = DSS(sel + 2 * i);
                far_to_near(cand, ce[x], 16);
                if (element_spots(cand, DSW(bear + 2 * x), DSSL(dist + 4 * x))) element_mark_seen(cand, 0);
            }
        }
    }
    memcpy(far_mp(ef), DSP(e), 16);
    stk_release(mk);
}

/* 583c:1895 forget(p, t) */
static void forget(u16 p, u16 t) { OB(p, 0xC3 + bitbyte(t)) &= (u8)~bitmask(t); }

/* 583c:18bf forget_far(p) */
static void forget_far(u16 p)
{
    u16 mk = stk_mark(), l = stk_alloc(0x64), nf = stk_alloc(2);
    s16 n = (s16)spotted_split(l, p, nf);
    s16 tot = (s16)(n + DSS(nf));
    u16 f = FLT(p);
    while (--tot >= 0) {
        u16 q = DSW(l + 2 * tot);
        if (FLT(q) == f) continue;
        u32 lim = dist_limit(shape_radius_cached(OW(q, 2)));
        if ((u32)dist3(q, p) > lim + 12000) forget(p, q);
    }
    stk_release(mk);
}

/* 583c:1966 */
static void forget_far_all(void)
{
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) forget_far(DSW(DSW(0x475B) + 2 * i));
}

/* 583c:1992 spotting_update — §6.6 */
void spotting_update(void)
{
    if (DSW(0x47EA) == 0) {
        DSW(0x47EA) = 1;
        elements_build_all();
    } else {
        if (timer16_dec(0x47E6) == 0) {
            DSW(0x47E6) = 700;
            elements_rebuild(DSW(DSW(0x4751) + 2 * (DSW(0x4765) + DSW(0x47E8))), 0);
            if ((s16)--DSW(0x47E8) < 0) DSW(0x47E8) = (u16)(DSW(0x4763) - 1);
        }
        while (DSL(0x47EE) < DSL(0x4086)) {
            DSL(0x47EE) += 0x80;
            forget_far(DSW(DSW(0x475B) + 2 * DSW(0x47EC)));
            if ((s16)--DSW(0x47EC) < 0) DSW(0x47EC) = (u16)(DSW(0x4776) - 1);
        }
    }
    FarPtr e = elems();
    for (s16 i = 0; i < (s16)E_COUNT; i++, e = far_add(e, 16))
        if (FB(e, 0) != 0) element_scan(e);
}

/* 583c:1a55 */
u16 flight_leader(u16 f)
{
    s16 i = (s16)first_alive(f, 0);
    return i >= 0 ? DSW(OW(f, 8) + 2 * i) : 0;
}

/* 583c:1a7c — true for the flight leader */
u16 plane_in_formation(u16 p) { return flight_leader(FLT(p)) == p; }

/* 583c:1a9b */
void elements_init(void)
{
    memset(DSP(0x47E0), 0, 18);
    E_COUNT = DSW(0x4776);
    ds_far_wr(0x47E2, mem_far_new((u32)(u16)(E_COUNT << 4), 0));
}

/* 583c:1ad0 */
void elements_free(void) { mem_far_delete(elems()); }

/* 583c:1ae2 */
void spotting_nop(void) {}

/* unused helpers kept for completeness (583c:0d1d max_field, 583c:1966 forget_far_all have no caller
 * outside this segment in the code read) */
void swb_elements_unused(void) { (void)max_field; (void)forget_far_all; }
