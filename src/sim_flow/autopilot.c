/* sim_flow: the autopilot ("A" key; 496a) and its segment/sphere geometry (082a) —
 * sim_flow_results.md §8. Register-interface near helpers (BX = object, AX/DX = pointers) are C
 * functions with explicit parameters. */
#include "sim_flow/sfa_priv.h"

#define PFL()        DSW(0x3B07)                    /* the player's flight */
#define B(o, x)      DSB((u16)((o) + (x)))
#define SIDE(o)      DSC((u16)((o) + 0x26))
#define JUMP_T       DSS(0x3F02)
#define JUMP_MODE    DSB(0x3F04)

static u32 d2(u16 a, u16 b) { return ldist2(a, b); }
static void vcopy(u16 dst, u16 src) { memmove(DSP(dst), DSP(src), 12); }

/* ---- 082a ---------------------------------------------------------------------------------------- */

/* AP 082a:0007 bisect_radius(out, c, r, a, b) — the point at distance r from c between a and b */
u16 bisect_radius(u16 out, u16 c, s32 r, u16 a, u16 b)
{
    u16 m0 = stk_mark();
    u16 pts = stk_alloc(0x18);
    s16 cnt = 0x0C;
    u16 idx = d2(c, a) > d2(c, b) ? 1 : 0;
    vcopy((u16)(pts + 12 * idx), a);
    vcopy((u16)(pts + 12 * (idx ^ 1)), b);
    u16 m;
    for (;;) {
        m = lmid(pts, (u16)(pts + 12));
        if (--cnt < 0) break;
        s32 d = (s32)d2(c, m);
        vcopy((u16)(pts + 12 * (d > r ? 1 : 0)), m);
        if (rtl_labs(d - r) < 5) break;
    }
    vcopy(out, m);
    stk_release(m0);
    return out;
}

static s16 abs16(s16 v) { return v < 0 ? (s16)-v : v; }   /* TODO(verify): `cdq` on a stale EAX upper half */

/* AP 082a:010e segment_circle(out, c, r, a, b) -> out or 0 (2-D) */
u16 segment_circle(u16 out, u16 c, s32 r, u16 a, u16 b)
{
    u16 m0 = stk_mark();
    u16 vac = stk_alloc(12), vbc = stk_alloc(12), vab = stk_alloc(12), proj = stk_alloc(12), sh = stk_alloc(2);
    u16 res = 0;
    lsub3(a, c, vac);
    lsub3(b, c, vbc);
    lsub3(vbc, vac, vab);
    s32 da = (s32)d2(a, c);
    u8 ina = da < r;
    s32 db = (s32)d2(c, b);
    u8 inb = db < r;
    if (ina || inb) {
        if (ina && inb) {
            vcopy(out, da > db ? a : b);
            res = out;
        } else {
            res = bisect_radius(out, c, r, a, b);
        }
        stk_release(m0);
        return res;
    }
    u16 ang1 = (u16)(atan2_32(DSW(vbc), DSW(vbc + 2), DSW(vbc + 4), DSW(vbc + 6)) + 0x8000);
    u16 ang2 = atan2_32(DSW(vab), DSW(vab + 2), DSW(vab + 4), DSW(vab + 6));
    u16 ang2b = (u16)(ang2 + 0x8000);
    s16 diff = abs16((s16)(ang1 - ang2b));
    if (diff >= 0x4000) { stk_release(m0); return 0; }
    u16 ang3 = (u16)(atan2_32(DSW(vac), DSW(vac + 2), DSW(vac + 4), DSW(vac + 6)) + 0x8000);
    diff = abs16((s16)(ang3 - ang2));
    if (diff >= 0x4000) { stk_release(m0); return 0; }
    s16 n = lnorm(da, sh);
    s16 s = (s16)mul_sin(n, (u16)diff);
    if ((s32)s >= (r >> (DSB(sh) & 31))) { stk_release(m0); return 0; }
    s16 c2 = (s16)mul_cos(n, (u16)diff);
    DSSL(proj) = (s16)mul_cos(c2, ang2);
    DSSL(proj + 4) = (s16)mul_sin(c2, ang2);
    DSSL(proj + 8) = 0;
    ladd(lvshl(proj, DSW(sh)), a);
    res = bisect_radius(out, c, r, a, proj);
    stk_release(m0);
    return res;
}

/* ---- 496a ---------------------------------------------------------------------------------------- */

/* AP 496a:0000 autopilot_init — empty */
void autopilot_init(void) {}

/* AP 496a:0005 three_quarters(x) */
s32 three_quarters(s32 x)
{
    return x - (x >> 2);
}

/* AP 496a:0022 nearest(&d, p, list, n) -> object */
u16 nearest(u16 d_ds, u16 p, u16 list, s16 n)
{
    s32 best = 0x7FFFFFFF;
    u16 o = 0;
    while (--n >= 0) {
        u16 x = DSW(list);
        list = (u16)(list + 2);
        s32 d = (s32)d2(p, (u16)(x + 4));
        if (d < best) { o = x; best = d; }
    }
    if (d_ds) DSSL(d_ds) = best;
    return o;
}

/* AP 496a:008a charge_in_danger(o) */
u8 charge_in_danger(u16 o)
{
    if (!plane_is_down(o) && !(B(o, 0x1D) & 2) && near_targets(o, 50000)) return 1;
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x62), d = stk_alloc(4);
    u16 n = pure_fighters_of_side(buf, SIDE(o) == 0 ? 1 : 0);
    nearest(d, (u16)(o + 4), buf, (s16)n);
    u8 r = DSSL(d) < 30000;
    stk_release(m);
    return r;
}

/* AP 496a:0d4c bits_clear(list, n, bit) -> count */
s16 bits_clear(u16 list, s16 n, s16 bit)
{
    s16 cnt = 0;
    u16 byte = (u16)(bit >> 3);
    u8 mask = (u8)(1 << (bit & 7));
    while (--n >= 0) {
        u16 o = DSW(list);
        list = (u16)(list + 2);
        if (DSB(o + byte + 0x1A) & mask) {
            DSB(o + byte + 0x1A) &= (u8)~mask;
            cnt++;
        }
    }
    return cnt;
}

/* AP 496a:0106 clear_danger_marks */
void clear_danger_marks(void)
{
    bits_clear(DSW(0x475B), DSS(0x4776), 0x2B);
}

/* AP 496a:011d mark_charges */
void mark_charges(void)
{
    if (DSB(PFL() + 2) != 6) return;
    u16 g = DSW(PFL() + 0x10);
    for (s16 i = 0; i < DSS(g + 6); i++) {
        u16 m = DSW((u16)(DSW(g + 8) + 2 * i));
        if (charge_in_danger(m)) DSB(m + 0x1F) |= 8;
    }
}

/* AP 496a:0169 friends_in_danger */
u8 friends_in_danger(void)
{
    u8 r = 0;
    if (DSB(PFL() + 2) != 6) return 0;
    mark_charges();
    u16 g = DSW(PFL() + 0x10);
    if (bits_clear(DSW(g + 8), DSS(g + 6), 0x2B) != 0) {
        u16 m = stk_mark();
        u16 d = stk_alloc(4);
        nearest(d, (u16)(PLAYER() + 4), DSW(g + 8), DSS(g + 6));
        if (DSSL(d) < 30000) r = 1;
        stk_release(m);
    }
    clear_danger_marks();
    return r;
}

/* AP 496a:01cf burn_fuel(o, dist) */
void burn_fuel(u16 o, s32 dist)
{
    u16 f = DSW(o + 0x28), t = DSW(o + 0x2A);
    if (o == PLAYER() && DSC(0x42CE) == 0) return;
    s32 v = (s32)(s16)fm_cruise_speed(DSW(o + 0x66), 10) * (s16)engine_power_of(o);
    v = (v + 0x3FF) >> 10;
    v = ((s32)(s16)v * DSS(0x3F22)) >> 10;
    if (v == 0) v = 1;
    s32 frames = (dist / v) >> 1;
    if (DSSL(t + 0x10) != 0) {
        DSSL(t + 0x10) -= (s32)DSS(f + 0x0C) * frames;
        if (DSSL(t + 0x10) < 0) DSSL(t + 0x10) = 0;
    } else if (DSSL(t + 0x0C) != 0) {
        DSSL(t + 0x0C) -= (s32)DSS(f + 0x0A) * frames;
        if (DSSL(t + 0x0C) < 0) DSSL(t + 0x0C) = 0;
    }
}

/* AP 496a:02c9 mark_cluster(p, list, n, bit, r) */
void mark_cluster(u16 p, u16 list, s16 n, s16 bit, u32 r)
{
    u16 byte = (u16)(bit >> 3);
    u8 mask = (u8)(1 << (bit & 7));
    for (s16 i = 0; i < n; i++) {
        u16 o = DSW((u16)(list + 2 * i));
        if (d2((u16)(o + 4), p) < r) {
            DSB(o + 0x1F) |= 4;
            DSB(o + byte + 0x1A) |= mask;
        }
    }
    s16 unchanged;
    do {
        unchanged = 1;
        for (s16 i = 0; i < n; i++) {
            u16 o = DSW((u16)(list + 2 * i));
            if (!(B(o, 0x1F) & 4)) continue;
            for (s16 k = 0; k < n; k++) {
                u16 s = DSW((u16)(list + 2 * k));
                if (B(s, 0x1F) & 4) continue;
                if (d2((u16)(o + 4), (u16)(s + 4)) < r) {
                    DSB(s + 0x1F) |= 4;
                    DSB(s + byte + 0x1A) |= mask;
                    unchanged = 0;
                }
            }
        }
    } while (!unchanged);
    bits_clear(list, n, 0x2A);
}

/* AP 496a:0400 save_positions(buf) */
void save_positions(u16 buf)
{
    DSW(0x3F00) = 0;
    for (s16 i = 0; i < DSS(0x4776); i++, buf = (u16)(buf + 0x10)) {
        u16 o = DSW((u16)(DSW(0x475B) + 2 * i));
        memcpy(DSP(buf), DSP(o + 4), 12);
        DSW(buf + 0x0C) = DSW(o + 0xA4);
        DSW(buf + 0x0E) = DSW(o + 0xA6);
        DSB(o + 0x56) |= 4;
        u16 dat = DSW((u16)(DSW(0x55D2) + 2 * DSB((u16)(0x54BC + DSB(o + 0x19)))));
        if (DSS(dat + 0x73) > DSS(0x3F00)) DSW(0x3F00) = DSW(dat + 0x73);
    }
    for (s16 i = 0; i < DSS(0x4763); i++) {
        u16 f = DSW((u16)(DSW(0x474F) + 2 * i));
        u16 l = flight_leader(f);
        for (s16 k = 0; k < DSS(f + 6); k++) {
            u16 m = DSW((u16)(DSW(f + 8) + 2 * k));
            DSB(m + 0x1D) &= 0xF7;
            if (m == l || m == PLAYER()) continue;
            if (d2((u16)(m + 4), (u16)(l + 4)) != 0) DSB(m + 0x1D) |= 8;
        }
        if (!DSB(f + 3)) continue;
        u8 any = 0;
        for (s16 k = 0; k < DSS(f + 6); k++)
            if (DSW((u16)(DSW((u16)(DSW(f + 8) + 2 * k)) + 0x4A)) == 0x3CC) { any = 1; break; }
        if (!any) continue;
        for (s16 k = 0; k < DSS(f + 6); k++) {
            u16 m = DSW((u16)(DSW(f + 8) + 2 * k));
            if (DSW(m + 0x4A) != 0x3CC && DSW(m + 0x4A) != 0x3F4) continue;
            if (m == PLAYER() || plane_gone(m)) continue;
            DSL(m + 0x0C) = 10000;
            ai_leave_combat(m);
        }
    }
}

/* AP 496a:05ca finish_positions(buf) */
void finish_positions(u16 buf)
{
    for (s16 i = 0; i < DSS(0x4776); i++, buf = (u16)(buf + 0x10)) {
        u16 o = DSW((u16)(DSW(0x475B) + 2 * i));
        if (ldist3(buf, (u16)(o + 4)) > 0x1F4) {
            DSW(o + 0x12) = 0;
            DSW(o + 0x10) = 0;
            DSW(o + 0x14) = bearing_between((u16)(o + 4), buf);
        }
        fm_reinit(o, DSW(o + 0xB2));
        DSB(o + 0x56) &= 0xFB;
        if (DSSL(o + 0x0C) > 0x190 && DSB(DSW(o + 0x16) + 1) != 0) {
            DSB(o + 0x1D) |= 0x10;
            DSB(o + 0xBA) &= 0xEF;
        }
    }
    DSB(0x3FEF) &= 0xF7;
    DSW(0x06B5) = 0;
    skip_hook();
    u16 f = DSW(0x4753);
    for (s16 i = 0; i < DSS(0x476B); i++, f = (u16)(f + 0x56)) DSB(f + 0x3A) &= 0xBF;
    bits_clear(DSW(0x475D), (s16)(DSW(0x4778) + DSW(0x4772)), 0x11);
}

/* AP 496a:06b8 jump_flight_ok(F) */
u8 jump_flight_ok(u16 f)
{
    u8 dl = 1;
    if (JUMP_MODE) {
        u8 friendly = (s16)(DSC(f + 0x42) + SIDE(PLAYER())) != 1;
        dl = friendly;
        if (JUMP_MODE != 1) dl = !friendly;
    }
    if (DSB(f + 0x3B) & 2) dl = 0;
    else if (DSB(f + 0x3A) & 0x40) dl = 0;
    return dl;
}

/* AP 496a:070e nearest_enemy(o, &d) -> object */
u16 ap_nearest_enemy_496a(u16 o, u16 d_ds)
{
    u16 best_o = 0;
    s32 best = 1000000000;
    s8 side = SIDE(o);
    for (s16 i = 0; i < DSS(0x4778); i++) {
        u16 x = DSW((u16)(DSW(0x475D) + 2 * i));
        if (B(x, 0x1F) & 2) continue;
        if ((s16)(SIDE(x) + side) != 1) continue;
        if (plane_gone(x)) continue;
        s32 d = (s32)d2((u16)(o + 4), (u16)(x + 4));
        if (d < best) { best = d; best_o = x; }
    }
    if (d_ds) DSSL(d_ds) = best;
    return best_o;
}

/* AP 496a:07b5 approach_hit(hit, o, to, r, ships) -> object. Quirk kept: *hit gets the last point
 * computed, not the one of the returned object. */
u16 approach_hit(u16 hit, u16 o, u16 to, s32 r, u8 ships)
{
    u16 m = stk_mark();
    u16 pt = stk_alloc(12);
    u16 best_o = 0;
    s32 best = 1000000000;
    s16 side = SIDE(o);
    u16 op = (u16)(o + 4);
    s16 n = ships ? DSS(0x4778) : DSS(0x4776);
    for (s16 i = 0; i < n; i++) {
        u16 x = DSW((u16)(DSW(0x475D) + 2 * i));
        if (B(x, 0x1F) & 2) continue;
        u16 xf = DSW(x + 0x34);
        u16 carrier = (u16)(DSB(xf + 0x3B) & 2);
        if ((s16)(SIDE(x) + side) != 1) {
            if (!carrier) continue;
            if (DSW((u16)(DSW(o + 0x34) + 0x10)) != xf) continue;
        }
        if (plane_gone(x)) continue;
        s32 rr = carrier ? three_quarters((s32)flight_leader_first()) : r;
        s32 d = 1000000001;
        if (to) {
            if (segment_circle(pt, (u16)(x + 4), rr, op, to)) d = (s32)d2(pt, op);
        } else {
            s32 dd = (s32)d2((u16)(x + 4), op);
            if (dd < rr) d = dd;
        }
        if (d < best) { best = d; best_o = x; }
    }
    if (best_o && hit) vcopy(hit, pt);
    stk_release(m);
    return best_o;
}

/* AP 496a:093c advance_point(out, from, to, d) -> the distance left */
s32 advance_point(u16 out, u16 from, u16 to, s32 d)
{
    s32 D = (s32)ldist3(from, to);
    if (d >= D) {
        vcopy(out, to);
        return 0;
    }
    bisect_radius(out, from, d, from, to);
    return D - d;
}

/* AP 496a:09a8 move_dist_air(BX o) */
s32 move_dist_air(u16 o)
{
    return (s32)DSS((u16)(DSW(o + 0x34) + 0x30)) * JUMP_T;
}

/* AP 496a:09c6 move_dist_ship(BX s) */
s32 move_dist_ship(u16 s)
{
    u16 sd = DSW((u16)(DSW(0x55D0) + 2 * DSB((u16)(0x555B + DSB(s + 0x19)))));
    return (s32)DSS(sd + 0x4A) * JUMP_T;
}

/* AP 496a:09f8 set_level(BX o, AX hdg) */
void set_level(u16 o, u16 hdg)
{
    DSW(o + 0x12) = 0;
    DSW(o + 0x10) = 0;
    DSW(o + 0x14) = hdg;
}

/* AP 496a:0a0a ship_advance(BX s, AX to, DX hdg) */
void ship_advance(u16 s, u16 to, u16 hdg)
{
    u16 m = stk_mark();
    u16 t = stk_alloc(12);
    if (advance_point(t, (u16)(s + 4), to, move_dist_ship(s)) < 0x12C) {
        vcopy((u16)(s + 4), to);
        DSW(s + 0x14) = hdg;
    } else {
        vcopy((u16)(s + 4), t);
    }
    stk_release(m);
}

/* AP 496a:0a8e place_object(BX o, AX p) */
void place_object(u16 o, u16 p)
{
    if (DSB(o + 0x18) == 0) burn_fuel(o, (s32)d2((u16)(o + 4), p));
    vcopy((u16)(o + 0x36), (u16)(o + 4));
    vcopy((u16)(o + 4), p);
}

/* AP 496a:0ae8 fly_toward(BX o, AX to, DX ang) */
void fly_toward(u16 o, u16 to, u16 ang)
{
    u16 m = stk_mark();
    u16 t = stk_alloc(12);
    if (advance_point(t, (u16)(o + 4), to, move_dist_air(o)) < 0x12C) {
        vcopy(t, to);
        if (ang) set_level(o, DSW(ang + 4));
    }
    place_object(o, t);
    stk_release(m);
}

/* AP 496a:0b63 near_old_or_new(p, o, r) */
s16 near_old_or_new(u16 p, u16 o, u32 r)
{
    if (d2(p, (u16)(o + 0x36)) < r) return 1;
    if (d2(p, (u16)(o + 4)) < r) return 2;
    return 0;
}

/* AP 496a:0bbe free_to_move(BX o) */
u8 free_to_move(u16 o)
{
    return !(B(o, 0x1C) & 2) && !(DSB(DSW(o + 0x34) + 0x3A) & 0x40);
}

/* AP 496a:0be3 stop_flight(BX o) */
void stop_flight(u16 o)
{
    u16 f = DSW(o + 0x34);
    s16 cx = DSS(f + 0x44);
    DSB(f + 0x3A) |= 0x40;
    u16 di = (u16)(f + 0x46 + 2 * cx);
    while (--cx >= 0) {
        di = (u16)(di - 2);
        DSB(DSW(di) + 0x3A) |= 0x40;
    }
    DSB(o + 0x1C) |= 2;
}

/* AP 496a:0c14 engage_scan(r) -> the smallest distance of the pairs considered */
s32 engage_scan(s32 r)
{
    s32 best = 10000000;
    s16 n = (s16)(DSW(0x4778) + DSW(0x4772));
    for (s16 i = 0; i < n; i++) {
        u16 a = DSW((u16)(DSW(0x475D) + 2 * i));
        for (s16 k = i + 1; k < n; k++) {
            u16 b = DSW((u16)(DSW(0x475D) + 2 * k));
            if ((s16)(SIDE(a) + SIDE(b)) != 1) continue;
            if (!free_to_move(a) && !free_to_move(b)) continue;
            s32 d = (s32)d2((u16)(a + 4), (u16)(b + 4));
            u8 nr = d < r, flag = 0;
            if (nr) {
                if (!(B(a, 0x1C) & 4)) stop_flight(a); else flag = 1;
                if (!(B(b, 0x1C) & 4)) stop_flight(b); else flag = 1;
            }
            if ((!nr || flag) && d < best) best = d;
        }
    }
    return best;
}

/* AP 496a:0da9 destination() -> lvec* (the map target, else the current waypoint) */
u16 destination(void)
{
    u16 t = map_target();
    if (t) return t;
    return flight_wp_pos(PFL());
}

/* AP 496a:0dc7 low_fuel(BX o) */
u8 low_fuel(u16 o)
{
    return (s32)range_left(o) < 40000;
}

/* AP 496a:0ded dogfight_mission */
s16 dogfight_mission(void)
{
    return DSB(0x477C) == 0 || DSB(0x477C) == 1;
}

/* AP 496a:0e07 autopilot_check -> reason (0 = may engage) */
s16 autopilot_check(void)
{
    u16 p = PLAYER();
    u8 low = DSSL(p + 0x0C) <= 0x44C || (DSSL(p + 0x0C) <= 0x9C4 && DSS(p + 0x10) < (s16)0xF8E4);
    if (dogfight_mission()) return 1;
    if (friends_in_danger()) return 2;
    if (low) return 3;
    if (low_fuel(p)) return 4;
    if (approach_hit(0, p, 0, 41600, 0)) return 5;
    if (plane_in_trouble(p)) return 7;
    if (!destination()) return 8;
    return 0;
}

/* AP 496a:0eb4 field(s, n) — after the n-th '|' (0 when missing) */
u16 field(u16 s, s16 n)
{
    u16 dx = s;
    for (s16 i = 0; i < n; i++) {
        u16 len = (u16)(rtl_strlen(dx) + 1);
        u16 p = 0;
        for (u16 k = 0; k < len; k++) if (DSB((u16)(dx + k)) == '|') { p = (u16)(dx + k); break; }
        dx = p ? (u16)(p + 1) : 0;
    }
    return dx;
}

/* AP 496a:0ef5 waypoint_text(n) */
void waypoint_text(s16 n)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x3E8);
    u16 p;
    u32 t = waypoint_message(PFL());
    if (t) {
        t = waypoint_message(PFL());
        rtl__fstrcpy(ds_ptr(buf), far_make((u16)(t >> 16), (u16)t));
        p = field(buf, n);
        if (p) str_subst(p, '|', 0);
    } else {
        p = 0x0138;                                            /* "Bad path text" */
    }
    if (p) msg_show(ds_ptr(p), 1, far_make(0, 0));
    stk_release(m);
}

/* AP 496a:0f65 autopilot_announce */
void autopilot_announce(void)
{
    if (map_target()) msg_show(str(1, 0x33), 1, far_make(0, 0));
    else waypoint_text(2);
}

/* AP 496a:0f9b autopilot_msg(r, stopped) */
void autopilot_msg(s16 r, s16 stopped)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0xC8);
    rtl__fstrcpy(ds_ptr(buf), str(1, (s16)(r + 0x28)));
    if (r != 1) {
        rtl_strcat(buf, 0x0146 /* " " */);
        rtl__fstrcat(ds_ptr(buf), str(1, stopped ? 0x28 : 0x27));
    }
    msg_show(ds_ptr(buf), 1, far_make(0, 0));
    stk_release(m);
}

static void regroup_members(u16 f, s16 only_near)
{
    u16 m = stk_mark();
    u16 pt = stk_alloc(12);
    u16 l = flight_leader(f);
    for (s16 i = 0; i < DSS(f + 6); i++) {
        u16 o = DSW((u16)(DSW(f + 8) + 2 * i));
        if (o == l || plane_gone(o)) continue;
        if (only_near && !lnear((u16)(l + 4), (u16)(o + 4), 0xEA60)) continue;
        formation_point(pt, l, my_formation_offset(o));
        place_object(o, pt);
    }
    stk_release(m);
}

/* AP 496a:1020 regroup(F) */
void regroup(u16 f)
{
    regroup_members(f, 0);
}

/* AP 496a:1096 regroup_player_flight */
void regroup_player_flight(void)
{
    regroup_members(PFL(), 1);
}

/* AP 496a:112e place_relative(A, B, off) */
void place_relative(u16 a, u16 b, u16 off)
{
    u16 m = stk_mark();
    u16 t = stk_alloc(6);
    u16 la = flight_leader(a);
    u16 lb = flight_leader(b);
    vec_rotate(off, obj_matrix(DSW(la + 0x20)), t);
    ladd3_v((u16)(la + 4), t, (u16)(lb + 4));
    regroup(b);
    stk_release(m);
}

/* formation on a flight T (states 0 and 5) */
static void follow_leader(u16 L, u16 f, u16 T, u32 r)
{
    u16 m = stk_mark();
    u16 off = stk_alloc(6), p = stk_alloc(12);
    FarPtr rec = far_make(DSW(f + 0x54), (u16)(DSW(f + 0x52) + 0x28));
    memcpy(DSP(off), far_mp(rec), 6);
    T = flight_leader(T);
    formation_point(p, T, off);
    if (near_old_or_new((u16)(L + 4), T, r)) {
        place_object(L, p);
        set_level(L, DSW(L + 0x14));
    } else {
        fly_toward(L, p, (u16)(T + 0x10));
    }
    stk_release(m);
}

/* AP 496a:118f jump_air_flight(F) — sim_flow_results.md §8.3 */
void jump_air_flight(u16 f)
{
    s16 n = DSS(f + 6);
    s16 moved = 0;
    u16 L = flight_leader(f);
    if (!L || !free_to_move(L) || (B(L, 0x1F) & 8)) return;
    s16 s;
    if (f != PFL()) {
        s = (s16)flight_next_task(L, 1);
    } else if (DSB(0x3EFE)) {
        s = 0x270F;
    } else {
        s = (s16)flight_next_task(L, 1);
        if (s == 0) { DSB(0x3EFF) = 1; s = 3; }
    }
    u16 m = stk_mark();
    u16 ang = stk_alloc(6), p = stk_alloc(12);
    switch (s) {
    case 0:
        follow_leader(L, f, DSW(f + 0x10), 20000);
        moved = 1;
        break;
    case 1:
        fly_toward(L, flight_home(f), 0);
        moved = 1;
        break;
    case 3:
    case 0x270F: {
        memcpy(DSP(ang), DSP(DSW(0x3716)), 6);
        u16 w = s == 0x270F ? destination() : wp_current_pos((u16)(f + 0x16));
        DSW(ang + 4) = bearing_between(w, (u16)(L + 4));
        fly_toward(L, w, ang);
        moved = 1;
        break;
    }
    case 5:
        follow_leader(L, f, DSW(f + 0x3E), 15000);
        moved = 1;
        break;
    case 6:
        for (s16 i = 0; i < n; i++) {
            u16 o = DSW((u16)(DSW(f + 8) + 2 * i));
            park_on_carrier(o, (s16)DSC(o + 0xBC));
        }
        break;
    default:
        break;
    }
    if (s != 0x270F) waypoint_advance(L);
    if (moved) {
        for (s16 i = 0; i < n; i++) {
            u16 o = DSW((u16)(DSW(f + 8) + 2 * i));
            if (o == L || plane_gone(o)) continue;
            formation_point(p, L, my_formation_offset(o));
            if (B(o, 0x1D) & 8) {
                fly_toward(o, p, (u16)(L + 0x10));
            } else {
                place_object(o, p);
                set_level(o, DSW(L + 0x14));
            }
        }
    }
    stk_release(m);
}

/* AP 496a:1487 jump_ship_group(F) */
void jump_ship_group(u16 f)
{
    u16 w = wp_current_pos((u16)(f + 0x16));
    if (!w) return;
    u16 M = DSW(f + 0x2E);
    ship_advance(M, w, bearing_between(w, (u16)(M + 4)));
    ship_waypoint_advance(M, (u16)(f + 0x16));
    for (s16 i = 0; i < DSS(f + 6); i++) {
        u16 s = DSW((u16)(DSW(f + 8) + 2 * i));
        if (plane_gone(s)) continue;
        lrotate((u16)(s + 0x68), obj_matrix(DSW(M + 0x20)), (u16)(s + 4));
        ladd((u16)(s + 4), (u16)(M + 4));
    }
}

/* AP 496a:153f jump_flights(mode, t) */
void jump_flights(s16 mode, s16 t)
{
    JUMP_MODE = (u8)mode;
    DSW(0x3F02) = (u16)t;
    for (s16 i = 0; i < DSS(0x476B); i++) {
        u16 f = DSW((u16)(DSW(0x4751) + 2 * i));
        if (!jump_flight_ok(f)) continue;
        if (DSB(f) == 0) jump_air_flight(f);
        else jump_ship_group(f);
    }
    JUMP_MODE = 0;
    DSW(0x3F02) = 0;
}

/* AP 496a:15a8 limit_time(&t, &dist, d, spd) */
s16 limit_time(u16 t_ds, u16 dist_ds, s32 d, s16 spd)
{
    s16 q = ldiv_sat(d, spd ? spd : 1);
    if (q == 0x7FFF) d = (s32)spd * 0x7FFF;
    if (DSS(t_ds) < 0 || q < DSS(t_ds)) {
        DSW(t_ds) = (u16)q;
        DSSL(dist_ds) = d;
        return 1;
    }
    return 0;
}

/* AP 496a:160c dest_is_target(dest) */
u8 dest_is_target(u16 dest)
{
    u16 m = stk_mark();
    u16 save = stk_alloc(12);
    u8 r = 0;
    s8 type = DSC(0x477C);
    u16 L = 0;
    if (type == 6) {
        L = flight_leader(DSW(PFL() + 0x10));
        if (!L) {
            module_flags(1);
            stk_release(m);
            return 0;
        }
    } else if (type == 8 || type == 9) {
        L = PLAYER();
    } else {
        stk_release(m);
        return 0;
    }
    vcopy(save, (u16)(L + 4));
    vcopy((u16)(L + 4), dest);
    DSL(L + 0x0C) = 0;
    r = near_targets(L, 10000);
    vcopy((u16)(L + 4), save);
    stk_release(m);
    return r;
}

/* AP 496a:170e autopilot_jump — sim_flow_results.md §8.3 */
void autopilot_jump(void)
{
    u16 p = PLAYER();
    if (DSW(p + 0x4A) != 0x458) return;
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x300), dest = stk_alloc(12), to = stk_alloc(12), hit = stk_alloc(12);
    u16 t = stk_alloc(2), dist = stk_alloc(4), nd = stk_alloc(4), dd = stk_alloc(4);
    u16 e = 0;
    u8 arrived = 1, low = 0, far_esc = 0;
    DSB(0x3EFE) = map_target() != 0;
    DSB(0x3EFF) = 0;
    if (DSB(PFL() + 2) == 6 && !DSB(0x3EFE)) {
        u16 g = DSW(PFL() + 0x10);
        nearest(dd, (u16)(p + 4), DSW(g + 8), DSS(g + 6));
        if (DSSL(dd) > 30000) far_esc = 1;
    }
    mark_charges();
    s32 T0 = (s32)flight_leader_first() - 3000;
    vcopy(dest, destination());
    s32 stop = dest_is_target(dest) ? 50000 : 20000;
    u16 L = plane_in_formation(p) ? p : flight_leader(PFL());
    mark_cluster((u16)(p + 4), DSW(0x4759), DSS(0x4774), 0x29, 35000);
    save_positions(buf);
    for (;;) {
        s16 spd = DSS(PFL() + 0x30);
        DSW(t) = 0xFFFF;
        s32 d = engage_scan(T0) - T0;
        if (d < 10000) d = 10000;
        limit_time(t, dist, d, spd);
        d = (s32)d2((u16)(L + 4), dest) - stop;
        if (d < 0) break;
        if (d < 800) d = 800;
        s32 x = (s32)range_left(p) - 40000;
        if (x < 10000) x = 10000;
        if (x < d) d = x;
        limit_time(t, dist, d, spd);
        limit_time(t, dist, 250000, spd);
        advance_point(to, (u16)(L + 4), dest, DSSL(dist));
        e = approach_hit(hit, L, to, 26000, 1);
        if (e && !limit_time(t, dist, (s32)d2((u16)(L + 4), hit), spd)) e = 0;
        jump_flights(1, DSS(t));
        vcopy(dest, destination());
        sky_advance(DSW(t));
        if (e) break;
        low = low_fuel(p);
        if (low) break;
        engage_scan(T0);
        jump_flights(2, DSS(t));
        vcopy(dest, destination());
        e = ap_nearest_enemy_496a(L, nd);
        if (e) {
            if (DSSL(nd) < T0) break;
            e = 0;
        }
        if ((s32)d2((u16)(L + 4), dest) < stop) break;
    }
    clear_danger_marks();
    bits_clear(DSW(0x4759), DSS(0x4774), 0x29);
    finish_positions(buf);
    midair_grace_set(0xF00);
    u16 off = stk_alloc(6);
    for (s16 i = 0; i < DSS(0x4763); i++) {
        u16 g = DSW((u16)(DSW(0x474F) + 2 * i));
        if (!g || DSB(g + 2) != 6) continue;
        u16 lg = flight_leader(g);
        u16 E = DSW(g + 0x10);
        u16 le = flight_leader(E);
        if (!lg || !le) continue;
        if (d2((u16)(lg + 4), (u16)(le + 4)) >= 80000) continue;
        if (E == PFL()) {
            DSW(off) = 0x0FA0; DSW(off + 2) = 0xE890; DSW(off + 4) = 0x03E8;
            place_relative(E, g, off);
        } else if (g != PFL() || (DSB(0x3EFF) && !far_esc)) {
            DSW(off) = 0xF060; DSW(off + 2) = 0x1770; DSW(off + 4) = 0xFC18;
            place_relative(g, E, off);
        }
    }
    regroup_player_flight();
    if ((e || low) && d2((u16)(L + 4), dest) > 40000) {
        arrived = 0;
        if (e) autopilot_msg((s16)(SIDE(p) + SIDE(e)) == 1 ? 5 : 6, 1);
        else autopilot_msg(4, 1);
    }
    if (arrived) {
        if (DSB(0x3EFE)) {
            map_set_target(0);
            msg_show(str(1, 0x32), 1, far_make(0, 0));
        } else {
            waypoint_text(0);
            wp_cursor_advance((u16)(PFL() + 0x16), 1);
        }
    }
    stk_release(mk);
}
