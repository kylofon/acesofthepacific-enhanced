/* sim_flow: results — the RNG (1889), the random table and XOR checksum (14b8), goal tracking and
 * scoring (1899), results.c (5254: fate, mission result, SIM.OUT). sim_flow_results.md §2–§7. */
#include "sim_flow/sfa_priv.h"

#define ASSERT_RESULTS_C far_make(SEG(0x3894), 0)
#define P()          PLAYER()
#define FL(o)        DSW((u16)((o) + 0x34))          /* flight of an object */
#define SIDE(o)      DSC((u16)((o) + 0x26))
#define DEAD(o)      ((DSB((u16)((o) + 0x1A)) & 0x40) || (DSB((u16)((o) + 0x1A)) & 1))
#define B(o, x)      DSB((u16)((o) + (x)))
#define DAT_PLANE(o) DSW((u16)(DSW(0x55D2) + 2 * DSB((u16)(0x54BC + DSB((u16)((o) + 0x19))))))
#define DAT_SHIP(o)  DSW((u16)(DSW(0x55D0) + 2 * DSB((u16)(0x555B + DSB((u16)((o) + 0x19))))))
#define W_BOMB       DSW(0x1344)
#define W_ROCKET     DSW(0x193A)
#define W_TORP       DSW(0x19EC)
#define W_BULLET     DSW(0x1348)

static u32 dist(u16 a, u16 b) { return ldist3((u16)(a + 4), (u16)(b + 4)); }

/* ---- 1889: lagged-Fibonacci RNG -------------------------------------------------------------------- */

/* AP 1889:0002 rng_seed(st) — the 56 seed words at far 3A60:0000 */
void ap_rng_seed(u16 st)
{
    for (int i = 0; i < 0x38; i++) DSW((u16)(st + 2 * i)) = SEGW(0x3A60, 2 * i);
    DSB(st + 0x71) = 0x18;
    DSB(st + 0x70) = 0x37;
    DSL(st + 0x72) = 0;
}

/* AP 1889:005f rng_next(st) -> u16 */
u16 ap_rng_next(u16 st)
{
    if (DSC(st + 0x71) < 0) {
        FarPtr h = ds_far((u16)(st + 0x72));
        if (!far_is_null(h)) codeptr_lookup(h)();
        return 0xFFFF;
    }
    s8 k = DSC(st + 0x71);
    DSB(st + 0x71) = (u8)(k + 1);
    u16 a = DSW((u16)(st + 2 * k));
    s8 j = DSC(st + 0x70);
    DSB(st + 0x70) = (u8)(j + 1);
    DSW((u16)(st + 2 * j)) = (u16)(DSW((u16)(st + 2 * j)) + a);
    u16 r = DSW((u16)(st + 2 * j));
    if (DSB(st + 0x70) == 0x38) DSB(st + 0x70) = 0;
    else if (DSB(st + 0x71) == 0x38) DSB(st + 0x71) = 0;
    return r;
}

/* AP 1889:0044 rng_skip(st, n) */
static void rng_skip(u16 st, s32 n)
{
    while (n >= 1) { ap_rng_next(st); n--; }
}

/* AP 1889:00b4 rng_mod(st, n) */
s16 rng_mod(u16 st, s16 n)
{
    if (n < 2) return 0;
    return (s16)((s32)(ap_rng_next(st) & 0x7FFF) % n);
}

/* AP 1889:00db rng_chance(st, p) */
s16 rng_chance(u16 st, s16 p)
{
    return (s16)(ap_rng_next(st) & 0xFFF) < (s16)(p * 0x29);
}

/* ---- 14b8 ------------------------------------------------------------------------------------------- */

/* AP 14b8:000d rand_table_init — 256 words at far 3A0D:0000 from a local RNG */
void rand_table_init(void)
{
    u16 m = stk_mark();
    u16 st = stk_alloc(0x76);
    ap_rng_seed(st);
    rng_skip(st, 0x14);
    for (u16 si = 0; si != 0x200; si = (u16)(si + 2)) SEGW(0x3A0D, si) = ap_rng_next(st);
    stk_release(m);
}

/* AP 14b8:0057 xor_bytes(p, n) */
u8 xor_bytes(u16 p, s16 n)
{
    u8 x = 0;
    while (--n >= 0) x ^= DSB(p++);
    return x;
}

/* AP 14b8:0072 rand_table_pair -> hi << 16 | lo */
u32 rand_table_pair(void)
{
    DSW(0x5822) = (u16)(DSW(0x5822) + 0x0D);
    u16 hi = SEGW(0x3A0D, (u16)((DSW(0x5822) & 0xFF) << 1));
    DSW(0x5822) = (u16)(DSW(0x5822) + 0x0D);
    u16 lo = SEGW(0x3A0D, (u16)((DSW(0x5822) & 0xFF) << 1));
    return (u32)hi << 16 | lo;
}

/* ---- 1899: goals and scoring ----------------------------------------------------------------------- */

/* AP 1899:000d goal_init — empty */
void goal_init(void) {}

/* AP 1899:10f8 targets(F, x) — does flight F have x as a target */
u8 targets(u16 f, u16 x)
{
    s8 kind = DSC(f + 0x43);
    if ((u16)(s16)kind > 3) return 0;
    s16 n = (s16)(s8)DSB(f + 0x0E);
    if (kind == 0) {
        for (s16 i = 0; i < n; i++) {
            u16 g = DSW((u16)(f + 0x10 + 2 * i));
            if (g == x) return 1;
            u16 mem = DSW(g + 8);
            for (s16 k = 0; k < DSS(g + 6); k++) if (DSW((u16)(mem + 2 * k)) == x) return 1;
        }
        return 0;
    }
    for (s16 i = 0; i < n; i++) if (DSW((u16)(f + 0x10 + 2 * i)) == x) return 1;
    return 0;
}

/* AP 1899:139f crash_imminent(o) */
u8 crash_imminent(u16 o)
{
    return DSSL(o + 0x0C) <= 0x4B0 && DSS(o + 0x10) < (s16)0xF8E4 && DSS(o + 0x94) > 0x4B0;
}

/* AP 1899:13c9 near_targets(o, r) — the last lnear() result (0 when there is nothing to test) */
u8 near_targets(u16 o, u32 r)
{
    u16 f = FL(o);
    u16 m = stk_mark();
    u16 p = stk_alloc(12);
    memcpy(DSP(p), DSP(o + 4), 12);
    u8 dl = 0;
    s8 kind = DSC(f + 0x43);
    s16 n = (s16)(s8)DSB(f + 0x0E);
    if ((u16)(s16)kind <= 3) {
        if (kind == 0) {
            for (s16 i = 0; i < n && !dl; i++) {
                u16 g = DSW((u16)(f + 0x10 + 2 * i));
                u16 mem = DSW(g + 8);
                for (s16 k = 0; k < DSS(g + 6); k++) {
                    dl = (u8)lnear(p, (u16)(DSW((u16)(mem + 2 * k)) + 4), r);
                    if (dl) break;
                }
            }
        } else {
            for (s16 i = 0; i < n; i++) {
                dl = (u8)lnear(p, (u16)(DSW((u16)(f + 0x10 + 2 * i)) + 4), r);
                if (dl) break;
            }
        }
    }
    stk_release(m);
    return dl;
}

/* the aircraft rule of goal_fraction: dead, or at the final evaluation a friendly still in combat */
static s32 air_member_value(u16 m, u16 g, s8 me, u8 own_is_g)
{
    if (DEAD(m)) return 0x400;
    if (!DSB(0x5C8E) || DSB(0x5CC9) || SIDE(m) != me) return 0;
    if (own_is_g) return (B(m, 0x1F) & 1) ? 0x400 : 0;
    return (B(m, 0x1C) & 0x80) ? 0 : 0x400;
}

static void ship_value(u16 m, s8 me, s16 *tot, s32 *acc)
{
    s16 v = DSS((u16)(DAT_SHIP(m) + 0x98));
    *tot = (s16)(*tot + v);
    u16 dm;
    if (DSB(0x5C8E) && !DSB(0x5CC9) && SIDE(m) == me && !(B(m, 0x1D) & 1)) dm = 0x400;
    else dm = DSW(DSW((u16)(m + 0x2A)));
    if (dm == 0x400) *acc += v;
    else *acc += ((s32)(s16)dm * v) >> 10;
}

static s32 clamp400(s32 v) { return v > 0x400 ? 0x400 : v; }

/* AP 1899:152e goal_fraction(F) -> 0..400h */
s16 goal_fraction(u16 f)
{
    s32 res = 0;
    s8 me = SIDE(P());
    s8 kind = DSC(f + 0x43);
    s16 n = (s16)(s8)DSB(f + 0x0E);
    if ((u16)(s16)kind > 3) return 0;
    switch (kind) {
    case 0: {
        s8 k0 = DSC(DSW(f + 0x10));
        if (k0 == 0) {
            s32 sum = 0;
            s16 cnt = 0;
            for (s16 i = 0; i < n; i++) {
                u16 g = DSW((u16)(f + 0x10 + 2 * i));
                cnt = (s16)(cnt + DSS(g + 6));
                u16 mem = DSW(g + 8);
                for (s16 k = 0; k < DSS(g + 6); k++)
                    sum += air_member_value(DSW((u16)(mem + 2 * k)), g, me, FL(P()) == g);
            }
            res = clamp400(ldiv_sat(sum, cnt));
        } else if (k0 == 1) {
            s16 tot = 0;
            s32 acc = 0;
            for (s16 i = 0; i < n; i++) {
                u16 g = DSW((u16)(f + 0x10 + 2 * i));
                u16 mem = DSW(g + 8);
                for (s16 k = 0; k < DSS(g + 6); k++) ship_value(DSW((u16)(mem + 2 * k)), me, &tot, &acc);
            }
            res = clamp400(ldiv_sat((s32)((u32)acc << 10), tot));
        }
        break;
    }
    case 1: {
        u16 r0 = DSW(f + 0x10);
        s8 side = (s8)plane_c0((u16)(s16)DSC(r0 + 0x1D));
        if (DSB(0x5C8E) && (!DSB(0x5CC9) || plane_in_trouble(P())) && side == me && !(B(r0, 0x1E) & 2))
            res = 0x400;
        else
            res = clamp400((s16)place_damage_ratio(r0));
        break;
    }
    case 2: {
        s16 tot = 0;
        s32 acc = 0;
        for (s16 i = 0; i < n; i++) ship_value(DSW((u16)(f + 0x10 + 2 * i)), me, &tot, &acc);
        res = clamp400(ldiv_sat((s32)((u32)acc << 10), tot));
        break;
    }
    case 3: {
        s32 sum = 0;
        for (s16 i = 0; i < n; i++) sum += air_member_value(DSW((u16)(f + 0x10 + 2 * i)), f, me, FL(P()) == f);
        if (n == 2) sum >>= 2;                                  /* quirk: a quarter, not a half */
        else if (n > 2) sum = ldiv_sat(sum, n);
        res = clamp400(sum);
        break;
    }
    }
    return (s16)res;
}

/* AP 1899:11aa objective_done(o). PORT: the undefined cases (the caller's DL) return 0. */
u8 objective_done(u16 o)
{
    u16 f = FL(o);
    s8 role = DSC(f + 2);
    switch (role) {
    case 0: return B(o, 0x1C) & 0x20;
    case 1: return wp_current_pos((u16)(f + 0x16)) == 0;
    case 2: return B(o, 0x1A) & 1;
    case 3:
        if (goal_fraction(f) == 0x400) return 1;
        if (has_ordnance(o) || rockets_left(o) || !(B(o, 0x1B) & 0x80)) return (B(o, 0x1B) & 0x20) != 0;
        if (DSC(f + 0x43) == 1) return 1;
        if (!torpedo_loaded(o)) return 1;
        return (B(o, 0x1B) & 0x20) != 0;
    case 4:
        return goal_fraction(f) == 0x400 || (B(o, 0x1E) & 1) || (B(o, 0x1B) & 0x80);
    case 5: {
        u16 r0 = DSW(f + 0x10);
        s8 kind = DSC(f + 0x43);
        if (kind == 0) {
            if (DSB(r0) != 1) return 0;                      /* PORT: undefined (DL) */
            u8 all1 = 1, all2 = 1;
            u16 mem = DSW(r0 + 8);
            for (s16 k = 0; k < DSS(r0 + 6); k++) {
                u16 m = DSW((u16)(mem + 2 * k));
                if (!(B(m, 0x1D) & 1)) all1 = 0;
                if (!(B(m, 0x1B) & 2)) all2 = 0;
            }
            return all1 || all2;
        }
        if (kind == 1) return (B(r0, 0x1E) & 2) || place_damage_ratio(r0) == 0x400;
        if (kind == 2) return (B(r0, 0x1D) & 1) || (B(r0, 0x1B) & 2);
        return 0;                                             /* PORT: undefined (DL) */
    }
    case 6: {
        u16 r0 = DSW(f + 0x10);
        u16 mem = DSW(r0 + 8);
        for (s16 k = 0; k < DSS(r0 + 6); k++) {
            u16 m = DSW((u16)(mem + 2 * k));
            if (!(B(m, 0x1D) & 4) || !(B(m, 0x1C) & 0x80)) return 0;
        }
        return 1;
    }
    default:
        return 0;                                             /* PORT: undefined (DL) */
    }
}

/* the prefix of phase 0 for an aircraft */
static void status_prefix(u16 x)
{
    u16 p = P();
    if (!(B(x, 0x1C) & 0x20) && ((x == p && !(u8)can_reach_home(x)) || DSS(DSW((u16)(x + 0x2A))) > 0x332))
        DSB(x + 0x1C) |= 0x20;
    if (!(B(x, 0x1D) & 0x80)) {
        if (near_targets(x, 50000)) DSB(x + 0x1D) |= 0x80;
    } else if (!(B(x, 0x1E) & 1) && !near_targets(x, 150000)) {
        DSB(x + 0x1E) |= 1;
    }
    DSB(x + 0x1F) &= 0xFE;
    DSB(x + 0x1C) &= 0xBF;
}

static void mark_contact(u16 x, u16 y, u8 nr)
{
    u16 p = P();
    u8 y_ok = !DEAD(y) && !(B(y, 0x1B) & 0x80);
    if (!(B(x, 0x1F) & 1) && y_ok && nr && !(x == p && (DSB(0x5C97) || DSB(0x5C98))))
        DSB(x + 0x1F) |= 1;
    if (!(B(x, 0x1C) & 0x40) && y_ok) {
        if (nr) {
            DSB(x + 0x1C) |= 0x40;
        } else if ((targets(FL(y), x) || (DSC(FL(x) + 2) == 3 && targets(FL(y), DSW((u16)(FL(x) + 0x10)))))
                   && !(B(y, 0x1D) & 4)) {
            DSB(x + 0x1C) |= 0x40;
        }
    }
}

static void update_done(u16 x)
{
    if (!(B(x, 0x1D) & 2) && objective_done(x)) DSB(x + 0x1D) |= 2;
    if (!(B(x, 0x1D) & 4) && ((B(x, 0x1D) & 2) || (B(x, 0x1C) & 0x20) || (B(x, 0x1A) & 1))) DSB(x + 0x1D) |= 4;
}

static void min_d(u32 *minD, u32 d)
{
    if (*minD == 0xFFFFFFFFu || d < *minD) *minD = d;
}

/* a ship against the planes of the other side (phase 1) */
static void ship_vs(u16 s, u16 list, s16 n, u32 *minD)
{
    DSB(s + 0x1F) &= 0xFE;
    DSB(s + 0x1C) &= 0xBF;
    DSB(s + 0x1D) &= 0xFE;
    for (s16 k = 0; k < n; k++) {
        u16 a = DSW((u16)(list + 2 * k));
        u32 d = dist(s, a);
        if (a == P()) min_d(minD, d);
        u8 a_ok = !DEAD(a) && (has_ordnance(a) || rockets_left(a) || torpedo_loaded(a) || !(B(a, 0x1B) & 0x80));
        if (!(B(s, 0x1F) & 1) && a_ok && d < 25000) DSB(s + 0x1F) |= 1;
        if (!(B(s, 0x1C) & 0x40) && a_ok && (d < 25000 || (targets(FL(a), s) && !(B(a, 0x1D) & 4))))
            DSB(s + 0x1C) |= 0x40;
    }
    if (!(B(s, 0x1C) & 0x40) && !(B(s, 0x1B) & 2)) DSB(s + 0x1D) |= 1;
}

/* a ground place against the planes of the other side (phase 2) */
static void place_vs(u16 q, u16 list, s16 n, u32 *minD)
{
    for (s16 k = 0; k < n; k++) {
        u16 a = DSW((u16)(list + 2 * k));
        u32 d = dist(q, a);
        if (a == P()) min_d(minD, d);
        u8 a_ok = !DEAD(a) && (has_ordnance(a) || rockets_left(a) || !(B(a, 0x1B) & 0x80));
        if (!(B(q, 0x1E) & 8) && a_ok && d < 25000) DSB(q + 0x1E) |= 8;
        if (!(B(q, 0x1E) & 1) && a_ok && (d < 25000 || (targets(FL(a), q) && !(B(a, 0x1D) & 4))))
            DSB(q + 0x1E) |= 1;
    }
    if (!(B(q, 0x1E) & 1) && (s16)place_damage_ratio(q) < 0x400) DSB(q + 0x1E) |= 2;
}

/* AP 1899:0012 goal_update — one phase per call, then the common tail; sim_flow_results.md §4 */
void goal_update(void)
{
    u32 minD = 0xFFFFFFFFu;
    u16 p = P();
    switch (DSW(0x5CC7)) {
    case 0: {
        u16 outer, inner;
        s16 no, ni;
        if (DSS(0x5CD4) < DSS(0x5CD8)) { no = DSS(0x5CD8); inner = DSW(0x5CCC); ni = DSS(0x5CD4); outer = DSW(0x5CD0); }
        else                           { no = DSS(0x5CD4); inner = DSW(0x5CD0); ni = DSS(0x5CD8); outer = DSW(0x5CCC); }
        for (s16 i = 0; i < no; i++) {
            u16 a = DSW((u16)(outer + 2 * i));
            status_prefix(a);
            DSB(a + 0x1C) &= 0x7F;
            for (s16 k = 0; k < ni; k++) {
                u16 b = DSW((u16)(inner + 2 * k));
                if (i == 0) {
                    status_prefix(b);
                    DSB(b + 0x1C) |= 0x80;
                }
                u32 d = dist(a, b);
                u8 nr = d < 25000;
                if (a == p || b == p) min_d(&minD, d);
                mark_contact(a, b, nr);
                mark_contact(b, a, nr);
                if (((B(b, 0x1C) & 0x80) && (B(b, 0x1C) & 0x40)) || crash_imminent(b) || DEAD(b)
                    || (b == p && !(u8)can_reach_home(b)))
                    DSB(b + 0x1C) &= 0x7F;
                if (i == 0) update_done(b);
            }
            if (!(B(a, 0x1C) & 0x40) && !crash_imminent(a)) DSB(a + 0x1C) |= 0x80;
            else if (!DEAD(a) && a == p && (u8)can_reach_home(a)) DSB(a + 0x1C) |= 0x80;
            update_done(a);
        }
        if (minD != 0xFFFFFFFFu) {
            if (!DSB(0x5C92)) { if ((s32)minD < 8000) DSB(0x5C92) = 1; }
            else if ((s32)minD > 30000) DSB(0x5C92) = 0;
        }
        DSW(0x5CC7) += 1;
        break;
    }
    case 1:
        for (s16 i = 0; i < DSS(0x5CD6); i++) ship_vs(DSW((u16)(DSW(0x5CCE) + 2 * i)), DSW(0x5CCC), DSS(0x5CD4), &minD);
        for (s16 i = 0; i < DSS(0x5CD2); i++) ship_vs(DSW((u16)(DSW(0x5CCA) + 2 * i)), DSW(0x5CD0), DSS(0x5CD8), &minD);
        if (minD != 0xFFFFFFFFu) {
            if (!DSB(0x5C91)) { if ((s32)minD < 20000) DSB(0x5C91) = 1; }
            else if ((s32)minD > 30000) DSB(0x5C91) = 0;
        }
        DSW(0x5CC7) += 1;
        break;
    case 2: {
        u16 q = DSW(0x4755);
        for (s16 i = 0; i < DSS(0x476E); i++, q = (u16)(q + 0x20)) {
            if (DSB(q + 0x1C) == 0x0D) continue;
            u8 s = (u8)plane_c0((u16)(s16)DSC(q + 0x1D));
            DSB(q + 0x1E) &= 0xF7;
            DSB(q + 0x1E) &= 0xFE;
            DSB(q + 0x1E) &= 0xFD;
            if (s == 0) place_vs(q, DSW(0x5CCC), DSS(0x5CD4), &minD);
            else        place_vs(q, DSW(0x5CD0), DSS(0x5CD8), &minD);
        }
        if (minD != 0xFFFFFFFFu) {
            if (!DSB(0x5C90)) { if ((s32)minD < 32000) DSB(0x5C90) = 1; }
            else if ((s32)minD > 45000) DSB(0x5C90) = 0;
        }
        DSW(0x5CC7) += 1;
        break;
    }
    case 3: {
        u16 f = DSW(0x4753);
        for (s16 i = 0; i < DSS(0x476B); i++, f = (u16)(f + 0x56)) {
            if (DSB(f) != 0 || (DSB(f + 0x3B) & 4)) continue;
            s16 n = 0, k = 0;
            u8 pf = 0;
            u16 mem = DSW(f + 8);
            for (s16 j = 0; j < DSS(f + 6); j++) {
                u16 m = DSW((u16)(mem + 2 * j));
                if (DEAD(m)) continue;
                if (m == p) { if (B(m, 0x1A) & 8) pf = 1; }
                else {
                    n++;
                    if ((B(m, 0x1B) & 0x80) && !(B(m, 0x1B) & 0x40)) k++;
                }
            }
            if (pf || ((s16)(n + 1) >> 1) <= k) DSB(f + 0x3B) |= 4;
        }
        DSW(0x5CC7) = 0;
        break;
    }
    default:
        break;
    }
    if (!DSB(0x5C93)) {
        if (DSB(0x5C92) || DSB(0x5C91) || DSB(0x5C90)) {
            DSB(0x5C93) = 1;
            world_detail_apply(slider_value(1));
        }
    } else if (!DSB(0x5C92) && !DSB(0x5C91) && !DSB(0x5C90)) {
        DSB(0x5C93) = 0;
        world_detail_apply(slider_value(1));
    }
    if (!DSB(0x5CC9) && (B(p, 0x1D) & 4) && ((B(p, 0x1C) & 0x80) || (B(p, 0x1A) & 1))) DSB(0x5CC9) = 1;
}

static s16 kill_value(u16 victim)
{
    s16 v = DSS((u16)(DAT_PLANE(victim) + 0x7D));
    u8 bb = B(victim, 0xBB);
    if (bb < 0x2D) return (s16)(v + DSS((u16)(0x1930 + 2 * (bb & 3))));
    return (s16)(v + 100);
}

static void ship_hit_flags(u16 victim)
{
    u16 sd = DAT_SHIP(victim);
    if (targets(FL(P()), victim)) {
        s8 c = DSC(sd + 0x6C);
        if (c == 1) DSB(0x5C95) = 1;
        else if (c == 0 || c == 3 || c == 4) DSB(0x5C96) = 1;
        DSB(0x5C94) = 1;
    }
}

/* AP 1899:0cf0 kill_credit(killer, victim, wclass, -, -, destroyed) — sim_flow_results.md §5.1 */
void kill_credit(u16 killer, u16 victim, u16 wclass, u16 a3, u16 a4, u16 a5)
{
    u8 destroyed = (u8)a5;
    u16 p = P();
    u8 bomb_torp = wclass == W_BOMB || wclass == W_TORP;
    if (killer == p) {
        u8 enemy = SIDE(killer) != SIDE(victim);
        if (DSB(victim + 0x18) == 0) {
            if (enemy) {
                if (!destroyed) return;
                if (B(victim, 0xBB) >= 0x2D) DSB(0x5C9C) = 1;
                DSW(0x5CA1) = (u16)(DSS(0x5CA1) + kill_value(victim));
                DSW(0x5CB9) += 1;
                DSW(0x5CC1) += 1;
            } else if (destroyed) {
                DSB(0x5CA5) |= 0x10;
            }
        } else if (DSB(victim + 0x18) == 1) {
            if (enemy) {
                u16 sd = DAT_SHIP(victim);
                if (!destroyed) {
                    if (!bomb_torp) return;
                    ship_hit_flags(victim);
                    if (DSW(victim + 0x78) == 0) {
                        DSW(victim + 0x78) = 0x5DC;
                        DSW(0x5CA1) = (u16)(DSS(0x5CA1) + (DSS(sd + 0x98) >> 1));
                        DSW(0x5CB5) += 1;
                        DSW(0x5CBD) += 1;
                    }
                } else {
                    DSW(0x5CA1) = (u16)(DSS(0x5CA1) + DSS(sd + 0x98));
                    DSW(0x5CB7) += 1;
                    DSW(0x5CBF) += 1;
                    if (bomb_torp) { DSW(0x5CB5) += 1; DSW(0x5CBD) += 1; }
                }
            } else if (destroyed) {
                DSB(0x5CA5) |= 0x20;
            }
        }
    } else if (FL(p) == FL(killer)) {
        if (DSB(victim + 0x18) == 0) {
            if (destroyed) {
                DSW(0x5C9F) = (u16)(DSS(0x5C9F) + kill_value(victim));
                DSW(0x5CC1) += 1;
            }
        } else if (DSB(victim + 0x18) == 1) {
            u16 sd = DAT_SHIP(victim);
            if (!destroyed) {
                if (!bomb_torp) return;
                ship_hit_flags(victim);
                if (DSW(victim + 0x7A) == 0) {
                    DSW(victim + 0x7A) = 0x5DC;
                    DSW(0x5C9F) = (u16)(DSS(0x5C9F) + (DSS(sd + 0x98) >> 1));
                    DSW(0x5CBD) += 1;
                }
            } else {
                DSW(0x5C9F) = (u16)(DSS(0x5C9F) + DSS(sd + 0x98));
                DSW(0x5CBF) += 1;
                if (bomb_torp) DSW(0x5CBD) += 1;
            }
        }
    } else if (SIDE(p) != SIDE(killer)) {
        if (DSB(killer + 0x18) == 0) {
            if (victim == p && destroyed && wclass == W_BULLET) {
                u16 pr = DSW((u16)(DSW(0x55CE) + 2 * B(killer, 0xBB)));
                DSB(0x5C9E) = 1;
                if (DSC(killer + 0xBB) > 0x2C) {
                    DSB(0x5C9B) = 1;
                    DSW(0x5C99) = DSB(pr + 0x41) ? (u16)(pr + 0x58) : 0;
                }
            }
        } else if (DSB(killer + 0x18) == 1 && victim == p && destroyed && wclass == W_BULLET) {
            DSB(0x5C9E) = 1;
        }
    }
}

/* AP 1899:102f target_credit(o, far t, far ref, wclass, -, -, destroyed) — through a pointer
 * (weapon impacts); sim_flow_results.md §5.2 */
void target_credit(u16 o, FarPtr t, FarPtr ref, u16 wclass, u16 type, u16 delta, u16 destroyed)
{
    u16 p = P();
    u8 bomb_rocket = wclass == W_BOMB || wclass == W_ROCKET;
    if (o == p) {
        u16 k = (u16)(rd16(t.seg, t.off) & 0x1F);
        u8 own = (SIDE(o) == 0 && k == 2) || (SIDE(o) == 1 && k == 0x10);
        if (own) {
            if ((u8)destroyed) DSB(0x5CA5) |= 0x40;
            return;
        }
        if (!(u8)destroyed) return;
        DSW(0x5CA1) = (u16)(DSS(0x5CA1) + (s16)target_value((u32)t.seg << 16 | t.off, (u32)ref.seg << 16 | ref.off));
        if (bomb_rocket) { DSW(0x5CB3) += 1; DSW(0x5CBB) += 1; }
    } else if (FL(p) == FL(o)) {
        if (!(u8)destroyed) return;
        DSW(0x5C9F) = (u16)(DSS(0x5C9F) + (s16)target_value((u32)t.seg << 16 | t.off, (u32)ref.seg << 16 | ref.off));
        if (bomb_rocket) DSW(0x5CBB) += 1;
    }
}

/* ---- 5254: results.c -------------------------------------------------------------------------------- */

/* AP 5254:0000 results_init — sim_flow_results.md §3 */
void results_init(void)
{
    DSW(0x5CC7) = 0;
    memset(DSP(0x5CA3), 0, 0x24);
    DSW(0x5CA1) = 0;
    DSW(0x5C9F) = 0;
    DSB(0x5C9E) = 0; DSB(0x5C9D) = 0; DSB(0x5C9C) = 0; DSB(0x5C9B) = 0;
    DSW(0x5C99) = 0;
    DSB(0x5C98) = 0; DSB(0x5C97) = 0; DSB(0x5C96) = 0; DSB(0x5C95) = 0; DSB(0x5C94) = 0;
    DSB(0x5CC9) = 0;
    DSB(0x5C93) = 0; DSB(0x5C92) = 0; DSB(0x5C91) = 0; DSB(0x5C90) = 0; DSB(0x5C8F) = 0; DSB(0x5C8E) = 0;
    DSW(0x5CD2) = 0; DSW(0x5CD4) = 0; DSW(0x5CD6) = 0; DSW(0x5CD8) = 0;
    u16 buf = arena_alloc((u16)(DSW(0x4778) << 1));
    u16 lo = buf, hi = (u16)(buf + (DSW(0x4778) << 1));
    DSW(0x5CD0) = buf;
    for (s16 i = 0; i < DSS(0x4776); i++) {
        u16 o = DSW((u16)(DSW(0x475B) + 2 * i));
        if (DSB(o + 0x26) == 0) { DSW(lo) = o; DSW(0x5CD8) += 1; lo = (u16)(lo + 2); }
        else { hi = (u16)(hi - 2); DSW(hi) = o; DSW(0x5CD4) += 1; }
    }
    DSW(0x5CCC) = hi;
    if (DSW(0x5CD8) == 0) DSW(0x5CD0) = 0;
    if (DSW(0x5CD4) == 0) DSW(0x5CCC) = 0;
    DSW(0x5CCE) = lo;
    for (s16 i = 0; i < DSS(0x4774); i++) {
        u16 o = DSW((u16)(DSW(0x4759) + 2 * i));
        if (DSB(o + 0x26) == 0) { DSW(lo) = o; DSW(0x5CD6) += 1; lo = (u16)(lo + 2); }
        else { hi = (u16)(hi - 2); DSW(hi) = o; DSW(0x5CD2) += 1; }
    }
    DSW(0x5CCA) = hi;
    if (DSW(0x5CD6) == 0) DSW(0x5CCE) = 0;
    if (DSW(0x5CD2) == 0) DSW(0x5CCA) = 0;
}

/* AP 5254:0663 captured() — the nearest object (enemy distances doubled) is enemy */
u8 captured(void)
{
    u16 m = stk_mark();
    u16 pp = stk_alloc(12), op = stk_alloc(12);
    memcpy(DSP(pp), DSP(P() + 4), 12);
    s16 me = SIDE(P());
    u8 cap = 0;
    u32 best = 1000000000u;
    FarPtr list = ds_far(0x06F3);
    FarPtr it = far_add(list, (u16)(DSW(0x5130) << 2));
    for (s16 i = DSS(0x5130); i < DSS(0x512E); i++) {
        FarPtr obj = far_rd(it.seg, it.off);
        it = far_add(it, 4);
        memcpy(DSP(op), far_mp(far_add(obj, 4)), 12);
        u32 d = ldist3(pp, op);
        u16 k = (u16)(rd16(obj.seg, obj.off) & 0x1F);
        u8 flag;
        if ((me == 0 && k == 0x10) || (me == 1 && k == 2)) { d <<= 1; flag = 1; }
        else if ((me == 0 && k == 2) || (me == 1 && k == 0x10)) flag = 0;
        else continue;
        if (d < best) { best = d; cap = flag; }
    }
    for (s16 i = 0; i < DSS(0x4774); i++) {
        u16 s = DSW((u16)(DSW(0x4759) + 2 * i));
        memcpy(DSP(op), DSP(s + 4), 12);
        u32 d = ldist3(pp, op);
        u8 flag;
        if (SIDE(s) != me) { d <<= 1; flag = 1; }
        else flag = 0;
        if (d < best) { best = d; cap = flag; }
    }
    stk_release(m);
    return cap;
}

/* AP 5254:0161 fate_record(out[5]) -> out — sim_flow_results.md §7.2 */
u16 fate_record(u16 out)
{
    u16 p = P();
    u16 w = 0;
    FarPtr name = ds_ptr((u16)(DAT_PLANE(p) + 0x0A));
    FarPtr ace = far_make(0, 0);
    u8 ground = (s32)(s16)(DSW((u16)(DSW(p + 0x66) + 0x98)) + DSW(p + 0xB8)) >= DSSL(p + 0x0C);
    u8 nt = DSB(0x477C) != 5;
    u8 a = 0, b = 0;
    s16 s = (s16)DSC(p + 0xAB);
    u8 airborne = !(B(p, 0x1C) & 0x10) && !ground && !DSB(0x5C97) && !DSB(0x5C98);
    if (DSB(0x0FEB) == 2 || s == 3) {
        s = 3;
    } else if (s == 2 || (B(p, 0x1A) & 0x40) || (B(p, 0x1A) & 1)
               || (airborne && (DSB(0x0FEB) || crash_imminent(p))) || (B(p, 0x1F) & 1)) {
        s = 2;
    } else if (s == 1) {
        s = 1;
    } else {
        u8 bad = DSB(0x0FE8) || DSB(0x0FE9) || DSB(0x0FEA) || !(u8)can_reach_home(p);
        if (!bad) {
            s16 health = DSS((u16)(DSW(p + 0x66) + 0x90)) > 1 ? (s16)((s16)(DSW(p + 0xA4) + DSW(p + 0xA6)) >> 1)
                                                              : DSS(p + 0xA4);
            bad = health < 0x200;
        }
        if (bad && airborne) {
            s = 1;
        } else if (DSB(FL(p) + 3) && (B(DSW(FL(p) + 4), 0x1B) & 2)) {
            s = 1;
            nt = 0;
        } else {
            s = 0;
            if (!(B(p, 0x1C) & 0x10) && !ground && !DSB(0x5C8F) && !DSB(0x5C97) && !DSB(0x5C98)) nt = 0;
            if (DSB(FL(p) + 3)) a = 1; else b = 1;
        }
    }
    if (DSB(0x5C9E) && !DSB(0x5C8F)) {
        w |= 1;
        DSB(0x5CA5) |= 1;
        DSB(0x5C9D) = 1;
        if (DSW(0x5C99)) { w |= 0x10; ace = ds_ptr(DSW(0x5C99)); }
    } else if (DSB(0x5C8F)) {
        w |= 3;
        DSB(0x5C9D) = 1;
        if (DSB(0x0FEB)) {
            if ((rtl_rand() & 3) == 1) { w |= 0x20; DSB(0x5CA5) |= 1; }
            else if (nt && captured()) { w |= 0x40; DSB(0x5CA5) |= 2; }
            else { w |= 0x50; DSB(0x5CA5) |= 4; }
        } else {
            /* TODO(verify): the original's `cdq; idiv bx` takes DX from the stale upper EAX */
            if (rtl_rand() % 10 == 1) { w |= 0x30; DSB(0x5CA5) |= 1; }
            else if (nt && captured()) { w |= 0x40; DSB(0x5CA5) |= 2; }
            else w |= 0x60;
        }
    } else if (s != 0) {
        w |= 2;
        DSB(0x5C9D) = 1;
        if (s == 3) { w |= 0x20; DSB(0x5CA5) |= 1; }
        else if (nt && captured()) { w |= 0x40; DSB(0x5CA5) |= 2; }
        else if (s == 2) { w |= 0x50; DSB(0x5CA5) |= 4; }
        else if (s == 1) w |= 0x60;
    } else {
        w |= 4;
        if (nt && captured()) { w |= 0x40; DSB(0x5CA5) |= 2; }
        else if (DSB(0x0FEB)) { w |= 0x50; DSB(0x5CA5) |= 4; }
        else if (DSB(0x5C98) || b) w |= 0x70;
        else if (DSB(0x5C97) || a) w |= 0x80;
        else w |= 0x60;
    }
    DSW(out) = w;
    DSW(out + 2) = name.off;
    DSW(out + 4) = name.seg;
    DSW(out + 6) = ace.off;
    DSW(out + 8) = ace.seg;
    return out;
}

/* AP 5254:065e results_cleanup — empty */
void results_cleanup(void) {}

/* AP 5254:081e reached_area — any member of the player's flight in the target area */
u8 reached_area(void)
{
    u16 f = FL(P());
    u16 mem = DSW(f + 8);
    for (s16 i = 0; i < DSS(f + 6); i++) if (B(DSW((u16)(mem + 2 * i)), 0x1D) & 0x80) return 1;
    return 0;
}

/* AP 5254:0859 sim_out_write(buf) */
void sim_out_write(u16 buf)
{
    u16 f = my_fopen(0x064A /* "SIM.OUT" */, 0x0652 /* "wb" */);
    if (!f) assert_fail(ASSERT_RESULTS_C, 0x7DC, 0x064A);
    if (my_fwrite(buf, 0x24, 1, f) != 1) assert_fail(ASSERT_RESULTS_C, 0x7E0, 0x064A);
    my_fclose(f);
}

/* AP 5254:1301 quartile(f) */
s16 quartile(s16 f)
{
    if (f == 0) return 0;
    if (f < 0x133) return 1;
    if (f < 0x2CC) return 2;
    if (f < 0x400) return 3;
    return 4;
}

/* AP 5254:1332 flight_size(g) */
s16 flight_size(u16 g)
{
    return g ? DSS(g + 6) : 0;
}

/* AP 5254:1348 flight_active(g) */
s16 ap_flight_active_5254(u16 g)
{
    if (!g) return 0;
    u8 own = FL(P()) == g;
    s16 n = 0;
    u16 mem = DSW(g + 8);
    if (DSB(g) == 0) {
        for (s16 i = 0; i < DSS(g + 6); i++) {
            u16 m = DSW((u16)(mem + 2 * i));
            u8 enemy = SIDE(m) != SIDE(P());
            if (DEAD(m)) continue;
            if (DSB(0x5CC9) || enemy || (own ? !(B(m, 0x1F) & 1) : (B(m, 0x1C) & 0x80) != 0)) n++;
        }
    } else if (DSB(g) == 1) {
        for (s16 i = 0; i < DSS(g + 6); i++) {
            u16 m = DSW((u16)(mem + 2 * i));
            u8 enemy = SIDE(m) != SIDE(P());
            if (B(m, 0x1B) & 2) continue;
            if (DSB(0x5CC9) || enemy || (B(m, 0x1D) & 1)) n++;
        }
    }
    return n;
}

/* AP 5254:1473 flight_lost(g) */
s16 flight_lost(u16 g)
{
    if (!g) return 0;
    return (s16)(flight_size(g) - ap_flight_active_5254(g));
}

/* AP 5254:149c aircraft_lost() */
s16 aircraft_lost(void)
{
    u16 p = P();
    u16 f = FL(p);
    u16 mem = DSW(f + 8);
    s16 n = 0;
    for (s16 i = 0; i < DSS(f + 6); i++) {
        u16 m = DSW((u16)(mem + 2 * i));
        if (DEAD(m) || (m == p && DSB(0x5C9D)) || (!DSB(0x5CC9) && (B(m, 0x1F) & 1))) n++;
    }
    return n;
}

static s16 sdiv(s16 a, s16 b) { return b ? (s16)(a / b) : 0; }   /* PORT: a zero divisor faulted; 0 */

/* AP 5254:08d9 mission_result(&code, &frac) — sim_flow_results.md §7.1 */
void mission_result(u16 code_ds, u16 frac_ds)
{
    u16 p = P();
    u16 d = FL(p);
    u16 r0 = DSW(d + 0x10);
    s16 code = -1, frac = 0;
    s8 type = DSC(0x477C);
    switch (type) {
    case 0: {
        u8 own_down = ap_flight_active_5254(d) == 0;
        u8 tgt_down = 0;
        if (DSC(d + 0x43) == 0) tgt_down = ap_flight_active_5254(r0) == 0;
        else if (DSC(d + 0x43) == 3) tgt_down = DEAD(r0);
        if (!(B(p, 0x1D) & 0x80)) code = 1;
        else if (own_down && tgt_down) code = 2;
        else if (!own_down && !tgt_down) code = 3;
        else if (tgt_down && DSB(0x5C9C) && !own_down) code = 4;
        else if (!tgt_down && DSB(0x5C9B)) code = 5;
        else if (tgt_down && !own_down) code = 6;
        else if (!tgt_down && own_down) code = 7;
        frac = !tgt_down ? 0 : DSB(0x5C9C) ? 0x400 : 0x200;
        break;
    }
    case 1:
    case 3: {
        s16 a = flight_lost(d), l = flight_lost(r0), n = flight_size(r0), h = (s16)((n + 1) >> 1);
        if (type == 1) {
            if (!reached_area()) code = 1;
            else if (a == 0 && l == 0) code = 2;
            else if (a == 0 && l == n) code = 3;
            else if (l == n) code = 4;
            else if (l == 0) code = 5;
            else if (h <= l) code = 6;
            else if (l == 1) code = 7;
            else if (l > 0 && h > l) code = 8;
        } else {
            if (!reached_area()) code = 1;
            else if (a == 0 && l == n) code = 2;
            else if (l == n) code = 3;
            else if (l == 0) code = 4;
            else if (h < l) code = 5;
            else if (l == 1) code = 6;
            else if (l > 0 && h >= l) code = 7;
        }
        frac = sdiv((s16)(l << 10), n);
        break;
    }
    case 2: {
        u8 wp = wp_current_pos((u16)(d + 0x16)) == 0;
        s16 c = flight_lost(d), k = DSS(0x5CC1);
        s16 e = SIDE(p) == 0 ? DSS(0x5CD4) : DSS(0x5CD8);
        if (k == 0 && c == 0) code = wp ? 1 : 2;
        else if (k == 0 && c > 0) code = 3;
        else if (k < c) code = 4;
        else if (k == c) code = 5;
        else if (c + 4 < k) code = 6;
        else if (c + 1 < k) code = 7;
        else if (c + 1 == k) code = 8;
        frac = sdiv((s16)(k << 10), e);
        break;
    }
    case 4:
    case 7: {
        s16 f = goal_fraction(d), q = quartile(f), a = flight_lost(d), L = 0, S = 0;
        u16 g = DSW(0x4753);
        for (s16 i = 0; i < DSS(0x476B); i++, g = (u16)(g + 0x56)) {
            if (g == d) continue;
            u8 t = 0;
            if (DSC(d + 0x43) == 0) {
                u16 mem = DSW(r0 + 8);
                for (s16 k = 0; k < DSS(r0 + 6) && !t; k++) t = targets(g, DSW((u16)(mem + 2 * k)));
            } else {
                t = targets(g, r0);
            }
            if (t) { L = (s16)(L + flight_lost(g)); S = (s16)(S + flight_size(g)); }
        }
        if (q == 0 && L == S) code = a == 0 ? 1 : 2;
        else code = (s16)(q + 3);
        s16 lf = sdiv((s16)(L << 10), S);
        if (type == 4) frac = (s16)((s16)((0x400 - f) * 3) / 4 + lf / 4);
        else frac = (s16)((s16)(0x400 - f) / 2 + lf / 2);
        break;
    }
    case 5:
        switch (DSB(0x5C97)) {
        case 1: code = 1; break;
        case 2: code = 2; break;
        case 3: code = 3; break;
        case 0: code = 4; break;
        default: break;
        }
        frac = 0;
        break;
    case 6: {
        s16 a = flight_lost(d), al = ap_flight_active_5254(r0), lg = flight_lost(r0), n = flight_size(r0);
        s16 h = (s16)((n + 1) >> 1);
        if (lg == 0) code = a == 0 ? 1 : 2;
        else if (al == 0) code = 3;
        else if (h <= al) code = 4;
        else if (al > 0 && h > al) code = 5;
        frac = sdiv((s16)(al << 10), n);
        break;
    }
    case 8: {
        s16 T = 0, C = 0, S = 0, Ts = 0, Cs = 0, Ss = 0;
        s8 k43 = DSC(d + 0x43);
        for (s16 i = 0; i < (s16)(s8)DSB(d + 0x0E); i++) {
            u16 r = DSW((u16)(d + 0x10 + 2 * i));
            s16 cnt = k43 == 0 ? DSS(r + 6) : k43 == 2 ? 1 : 0;
            for (s16 k = 0; k < cnt; k++) {
                u16 m = k43 == 0 ? DSW((u16)(DSW(r + 8) + 2 * k)) : r;
                s8 cls = DSC(DAT_SHIP(m) + 0x6C);
                u8 sunk = (B(m, 0x1B) & 2) != 0;
                T++; if (sunk) Ts++;
                if (cls == 1) { C++; if (sunk) Cs++; }
                else if (cls == 0 || cls == 3 || cls == 4) { S++; if (sunk) Ss++; }
            }
        }
        s16 f = goal_fraction(d), q = quartile(f);
        if (!reached_area()) { code = 1; frac = 0; }
        else if (T == 1) {
            if (Cs == 1 && C == 1) { code = 2; frac = 0x400; }
            else if (DSB(0x5C95) && Cs == 0 && C == 1) { code = 3; frac = 0x200; }
            else if (Ts == 1) { code = 4; frac = 0x400; }
            else if (DSB(0x5C94) && Ts == 0) { code = 5; frac = 0x199; }
            else if (!DSB(0x5C94)) { code = 6; frac = 0; }
        } else {
            if (q == 0) { code = 7; frac = 0; }
            else if (T == Ts) { code = 8; frac = 0x400; }
            else if (C > 0 && DSB(0x5C95) && Cs == 0) { code = 9; frac = 0x200; }
            else if (C > 0 && Cs > 0) { code = 0x0A; frac = 0x333; }
            else if (S > 0 && C == 0 && DSB(0x5C96) && Ss == 0) { code = 0x0B; frac = 0x200; }
            else if (S > 0 && C == 0 && Ss > 0) { code = 0x0C; frac = 0x333; }
            else if (q == 1) { code = 0x0D; frac = 0x133; }
            else if (q == 2) { code = 0x0E; frac = 0x200; }
            else if (q == 3) { code = 0x0F; frac = 0x333; }
        }
        break;
    }
    case 9: {
        s16 f = goal_fraction(d);
        code = !reached_area() ? 1 : (s16)(quartile(f) + 2);
        frac = f;
        break;
    }
    case 10: {
        s16 f = goal_fraction(d), q = quartile(f), a = flight_lost(d);
        if (q == 0) code = a == 0 ? 1 : 2;
        else if (q == 1) code = 3;
        else if (q == 2) code = 4;
        else if (q == 3 || q == 4) code = 5;
        frac = (s16)(0x400 - f);
        break;
    }
    default:
        break;                              /* PORT: uninitialised in the original */
    }
    DSW(code_ds) = (u16)code;
    DSW(frac_ds) = (u16)frac;
}

/* AP 5254:04f1 results_write — sim_flow_results.md §7.3 */
void results_write(void)
{
    u16 m = stk_mark();
    u16 code = stk_alloc(2), frac = stk_alloc(2);
    u16 p = P();
    DSB(0x5C8E) = 1;
    mission_result(code, frac);
    s16 merit = (s16)(((s32)DSS(0x4783) * DSS(frac)) >> 10);
    s16 sc = DSS(0x5CA1);
    if (DSB(0x5C9E)) sc = (s16)(sc - 0x14);
    if (DSW(p + 0xDD) == 0) {
        u16 f = FL(p);
        u16 mem = DSW(f + 8);
        for (s16 i = 0; i < DSS(f + 6); i++) {
            u16 o = DSW((u16)(mem + 2 * i));
            if (o == p) continue;
            if (DEAD(o) || (!DSB(0x5CC9) && (B(o, 0x1F) & 1))) sc = (s16)(sc - 0x14);
        }
    }
    s16 si = (s16)(sc + 2 * merit);
    si = (s16)(si + ((DSB(0x5C98) && !(DSC(0x5CA5) & 2)) ? 10 : 0));
    si = (s16)(si + ((DSB(0x5C97) && !(DSC(0x5CA5) & 2)) ? 30 : 0));
    if (si < 0) si = 0;
    si = (s16)(((s32)si * ap_realism_percent()) / 100);
    if (si >= 0x384) si = 0x384;
    DSW(0x5CA3) = DSW(code);
    DSB(0x5CA6) = 0;
    DSW(0x5CA7) = (u16)aircraft_lost();
    for (int i = 0; i < 4; i++) DSW((u16)(0x5CA9 + 2 * i)) = 0xFFFF;
    DSW(0x5CB1) = (DSB(FL(p) + 3) && (B(DSW(FL(p) + 4), 0x1B) & 2)) ? 1 : 0;
    DSW(0x5CC3) = (u16)merit;
    DSW(0x5CC5) = (u16)si;
    sim_out_write(0x5CA3);
    stk_release(m);
}
