/* sim_world part B — flight-level states (4edf): take-off, parking, formation, waypoints, escort, return.
 * sim_world_ai.md §12; ported from the disassembly. Quirks kept: state 0444h is never entered (no code sets
 * Plane+42 bit 0) and its steer does not test for a missing leader; st_orbit_waypoint_steer orbits DS:0000
 * when the flight has no waypoint. */
#include "sim_world/swb_priv.h"

/* abs of a 16-bit difference as `cdq / xor / sub` (8000h stays 8000h) */
static inline s16 abs_wrap(s16 x) { return (s16)(x < 0 ? (u16)-(u16)x : (u16)x); }
#define FMD(p)  OW(p, 0x66)

/* 4edf:0000 (03B8h act) — scratch {pos 12, angles 6, next}: copy the posture, then continue */
u16 st_path_act(u16 p)
{
    u16 s = SCR(p);
    memcpy(DSP(p + 4), DSP(s), 12);
    OW(p, 0x10) = DSW(s + 0x0C);
    OW(p, 0x12) = DSW(s + 0x0E);
    OW(p, 0x14) = DSW(s + 0x10);
    if (DSW(s + 0x12) != 0) set_state(p, DSW(s + 0x12));
    else ai_leave_combat(p);
    return 0;
}

/* 4edf:005e path_enter(p, pos, ang, next) */
void path_enter(u16 p, u16 pos, u16 ang, u16 next)
{
    u16 s = set_state(p, D_PATH);
    memcpy(DSP(s), DSP(pos), 12);
    DSW(s + 0x0C) = DSW(ang);
    DSW(s + 0x0E) = DSW(ang + 2);
    DSW(s + 0x10) = DSW(ang + 4);
    DSW(s + 0x12) = next;
}

/* 4edf:009f fm_set_velocity(fm, v, Mt) */
static void fm_set_velocity(u16 fm, u16 v, u16 Mt)
{
    u16 mk = stk_mark(), w = stk_alloc(6), b = stk_alloc(6);
    vratio10(v, DSW(0x3F22), w);
    vec_rotate(w, Mt, b);
    memcpy(DSP(fm + 0x2E), DSP(b), 6);
    memcpy(DSP(fm + 0x34), DSP(w), 6);
    fill_int((u16)(fm + 0x1E), 0x7FFF, 3);
    stk_release(mk);
}

/* 4edf:010a ship_delta(out, c) — the carrier's motion this frame */
static void ship_delta(u16 out, u16 c) { ldiff16((u16)(c + 4), (u16)(c + 0x36), out); }

/* 4edf:0129 on_carrier_deck(c, pt) */
static u16 on_carrier_deck(u16 c, u16 pt)
{
    if (labs32(DSSL(pt) - OSL(c, 4)) >= 5000) return 0;
    if (labs32(DSSL(pt + 4) - OSL(c, 8)) >= 5000) return 0;
    u16 mk = stk_mark(), q = stk_alloc(12);
    memcpy(DSP(q), DSP(pt), 12);
    DSSL(q + 8) = 0;
    u16 r = segment_hits_boxes(pt, q, OW(c, 0x20), (u16)(c + 4), OW(c, 0x24)) ? 1 : 0;
    stk_release(mk);
    return r;
}

/* 4edf:01c7 fm_reinit(p, x) — keeps the engine factors +A4/+A6/+A8 */
u16 fm_reinit(u16 p, u16 x)
{
    u16 a4 = OW(p, 0xA4), a6 = OW(p, 0xA6), a8 = OW(p, 0xA8);
    u16 M = obj_matrix(OW(p, 0x20));
    fm_init((u16)(p + 0x64), (u16)(p + 2), DSW(DAT(p) + 0x24), M, x);
    OW(p, 0xA4) = a4;
    OW(p, 0xA6) = a6;
    OW(p, 0xA8) = a8;
    return p;
}

/* 4edf:023a */
static void throttle_min_f0(u16 p)
{
    if (OS(p, 0xB2) < 0xF0) OW(p, 0xB2) = 0xF0;
}

/* 4edf:0252 (03E0h act) runway take-off run */
u16 st_runway_takeoff_act(u16 p)
{
    u16 s = SCR(p);
    s16 gh = DSS(FMD(p) + 0x98);
    OB(p, 0x43) |= 8;
    OB(p, 0x47) |= 8;
    if (!(OB(p, 0x56) & 2)) {
        fm_reinit(p, 0);
        DSW(s) = (u16)(s16)(((s32)(s16)fm_cruise_speed_here((u16)(p + 0x64)) * DSS(0x3F22)) >> 10);
    }
    if (p != PLAYER) OW(p, 0xB2) = 0x100;
    else throttle_min_f0(p);
    if (p != PLAYER && (s16)(DSS(s) >> 2) < OS(p, 0x6C)) {
        steer_pitch_to(p, 1000, 0);
        OB(p, 0xBA) = (u8)((OB(p, 0xBA) & 0xFC) | 2);
        OW(p, 0xC1) = 2;
    }
    st_act_fly(p);
    if ((s32)gh + (p == PLAYER ? 0 : 200) < OSL(p, 0x0C)) {
        if (p == PLAYER) {
            set_state(p, D_CLIMB_OUT);
        } else {
            if (OC(DAT(p), 0x76) == 0) OB(p, 0x1D) |= 0x10;
            if ((s32)(s16)(gh + 200) < OSL(p, 0x0C)) ai_leave_combat(p);
        }
    }
    return 0;
}

/* 4edf:0370 (03CCh act) carrier take-off run — §12.1 */
u16 st_carrier_takeoff_act(u16 p)
{
    u16 c = OW(FLT(p), 4);
    u16 SD = SDAT(c);
    s32 t = OSL(p, 0x50);
    OB(p, 0x43) |= 8;
    OB(p, 0x47) |= 8;
    s16 gh = DSS(FMD(p) + 0x98);
    OW(p, 0xB8) = DSW(SD + 4);
    s16 top = (s16)(DSS(SD + 4) + gh);
    if (p != PLAYER) OW(p, 0xB2) = 0x100;
    else throttle_min_f0(p);
    if (p != PLAYER) {
        OB(p, 0xBA) = (u8)((OB(p, 0xBA) & 0xFC) | 2);
        OW(p, 0xC1) |= 2;
    }
    OB(c, 0x42) |= 8;
    OB(c, 0x46) |= 8;
    if (t < 2) {
        park_on_carrier(p, (s8)OB(p, 0xBC));
        return 0;
    }
    u16 mk = stk_mark(), v = stk_alloc(6), a = stk_alloc(12);
    if (t == 2) {
        fm_reinit(p, 0);
        ship_delta(v, c);
        vec_map_timebase(v, v);
        fm_set_velocity((u16)(p + 0x64), v, obj_matrix_t(OW(c, 0x20)));
    }
    st_act_fly(p);
    if (p == PLAYER) {
        if (!((s32)top >= OSL(p, 0x0C) && on_carrier_deck(c, (u16)(p + 4)))) set_state(p, D_CLIMB_OUT);
    } else {
        memcpy(DSP(a), DSP(p + 4), 12);
        OW(p, 0x14) = OW(c, 0x14);
        lmove(a, 1000, OW(p, 0x14));
        if (on_carrier_deck(c, a)) {
            OSL(p, 0x0C) = top;                            /* still over the deck: hold on it */
        } else if (!((s32)(s16)(top + 200) >= OSL(p, 0x0C) && on_carrier_deck(c, (u16)(p + 4)))) {
            ai_leave_combat(p);
            if (OC(DAT(p), 0x76) == 0) OB(p, 0x1D) |= 0x10;
        }
    }
    stk_release(mk);
    return 0;
}

/* 4edf:057f park_on_carrier(p, spot) */
void park_on_carrier(u16 p, s16 spot)
{
    u16 c = OW(FLT(p), 4);
    u16 mk = stk_mark(), o = stk_alloc(6);
    memcpy(DSP(o), DSP(SDAT(c) + 6 * spot), 6);
    DSW(o + 4) = (u16)(DSW(o + 4) + DSW(FMD(p) + 0x98));
    obj_to_world((u16)(p + 4), (u16)(c + 4), o, OW(c, 0x20));
    OW(p, 0x14) = OW(c, 0x14);
    OW(p, 0x10) = DSW(FMD(p) + 0x9A);
    OB(p, 0x43) |= 8;
    OB(p, 0x47) |= 8;
    stk_release(mk);
}

/* 4edf:0619 plane_gone(BX = p) */
u16 plane_gone(u16 bx) { return (OB(bx, 0x1A) & 0x40) || (OB(bx, 0x1A) & 1) || (OB(bx, 0x1B) & 2); }

/* 4edf:0639 queue_prev(p) — the last member before p in the take-off queue that is not gone */
static u16 queue_prev(u16 p)
{
    for (s16 i = (s16)((s8)OB(p, 0xBC) - 1); i >= 0; i--) {
        u16 q = DSW(OW(FLT(p), 8) + 2 * i);
        if (!plane_gone(q)) return q;
    }
    return 0;
}

/* 4edf:068a takeoff_clear(p) */
static u16 takeoff_clear(u16 p)
{
    if ((s32)vcr_frame() <= 2) return 0;
    if (p != PLAYER) OW(p, 0xB2) = 0x100;
    if (OS(p, 0xB2) <= 0x4B) return 0;
    u16 q = queue_prev(p);
    if (!q) return 1;
    return ldist2((u16)(p + 4), (u16)(q + 4)) > 1000;
}

/* 4edf:06fc (03F4h act) parked on a carrier */
u16 st_parked_carrier_act(u16 p)
{
    park_on_carrier(p, (s8)OB(p, 0xBC));
    if (OB(OW(FLT(p), 4), 0x1B) & 2) {
        if (p != PLAYER || DSB(0x42CF) != 0) {
            u16 s = set_state(p, D_DECK_WRECK);
            DSSL(s) = (5 - (s32)(s8)OB(p, 0xBC)) * 0x500;
        }
    } else if (takeoff_clear(p)) {
        set_state(p, D_CV_TAKEOFF);
    }
    plane_engine_sound(p);
    return 0;
}

/* 4edf:0780 park_on_airfield(p, spot) */
void park_on_airfield(u16 p, s16 spot)
{
    u16 pl = OW(FLT(p), 4);
    s16 r = DSS(0x128E + 6 * spot);
    u16 mk = stk_mark(), a = stk_alloc(12), b = stk_alloc(12), ang = stk_alloc(6);
    OB(p, 0x43) |= 8;
    OB(p, 0x47) |= 8;
    objtype_names((u16)(OW(pl, 2) & 0x1FFF), a, b);
    lrot_z2(a, OW(pl, 0x14));
    lrot_z2(b, OW(pl, 0x14));
    angles_to(ang, b, a);
    ladd(a, (u16)(pl + 4));
    u16 h = DSW(ang + 4);
    OW(p, 0x14) = h;
    OW(p, 0x10) = DSW(FMD(p) + 0x9A);
    OSL(p, 4) = DSSL(a) + (s16)mul_cos(r, (u16)(h + 0x4000));
    OSL(p, 8) = DSSL(a + 4) + (s16)mul_sin(r, (u16)(h + 0x4000));
    OSL(p, 0x0C) = DSS(FMD(p) + 0x98);
    OB(p, 0x43) |= 8;
    OB(p, 0x47) |= 8;
    stk_release(mk);
}

/* 4edf:0870 (0408h act) parked on an airfield */
u16 st_parked_airfield_act(u16 p)
{
    OB(p, 0x43) |= 8;
    OB(p, 0x47) |= 8;
    if (!(OB(p, 0x56) & 1)) park_on_airfield(p, (s8)OB(p, 0xBC));
    else if (takeoff_clear(p)) set_state(p, D_RW_TAKEOFF);
    return 0;
}

/* 4edf:08b7 */
static u16 flight_waypoint(u16 p) { return wp_current_pos((u16)(FLT(p) + 0x16)); }

/* 4edf:08d6 escort_situation(p) → 0 none, 1 go to my waypoint, 2 go to the escorted leader, 3 in position */
static u16 escort_situation(u16 p)
{
    u16 f = FLT(p), e = OW(f, 0x10);
    memset(DSP(0x4026), 0, 8);
    DSW(0x402A) = first_alive_of(e);
    if (!DSW(0x402A)) return 0;
    DSL(0x4026) = ldist2((u16)(p + 4), (u16)(DSW(0x402A) + 4));
    if (DSSL(0x4026) < 20000) return 3;
    DSW(0x402C) = wp_current_pos((u16)(f + 0x16));
    if (!DSW(0x402C)) return 2;
    if (OB(e, 0x3B) & 2) return 1;
    u16 w2 = wp_current_pos((u16)(e + 0x16));
    if (!w2 || ldist2(DSW(0x402C), w2) > 5000 || DESC(DSW(0x402A)) != D_WAYPOINTS) return 1;
    return 2;
}

/* 4edf:09b9 find_ground_target(p, stop, &d) — nearest object of the flight's reference list */
u16 find_ground_target(u16 p, s32 stop, u16 d_ds)
{
    u16 f = FLT(p), best = 0;
    s32 bestd = 0x7FFFFFFF;
    for (s16 i = 0; (s16)OC(f, 0x0E) > i; i++) {
        u16 o = OW(f, 0x10 + 2 * i);
        if (best && bestd < stop) break;
        switch ((s8)OB(f, 0x43)) {
        case 0:                                            /* flights: their members */
            for (s16 j = 0; OS(o, 6) > j; j++) {
                u16 m = DSW(OW(o, 8) + 2 * j);
                if (OB(o, 0) == 0 ? plane_is_down(m) : (OB(m, 0x1B) & 2)) continue;
                s32 d = (s32)ldist2((u16)(m + 4), (u16)(p + 4));
                if (d < bestd) {
                    best = m;
                    bestd = d;
                    if (d < stop) break;
                }
            }
            break;
        case 1: {                                          /* places */
            s32 d = (s32)ldist2((u16)(o + 4), (u16)(p + 4));
            if (d < bestd) { best = o; bestd = d; }
            break;
        }
        case 2:                                            /* ships */
            if (!(OB(o, 0x1B) & 2)) {
                s32 d = (s32)ldist2((u16)(o + 4), (u16)(p + 4));
                if (d < bestd) { best = o; bestd = d; }
            }
            break;
        case 3:                                            /* planes */
            if (!plane_is_down(o)) {
                s32 d = (s32)ldist2((u16)(o + 4), (u16)(p + 4));
                if (d < bestd) { best = o; bestd = d; }
            }
            break;
        default: break;
        }
    }
    DSSL(d_ds) = bestd;
    return best;
}

/* 4edf:0b6f */
static u16 find_target_45000(u16 p)
{
    u16 mk = stk_mark(), d = stk_alloc(4);
    u16 r = find_ground_target(p, 45000, d);
    if (DSSL(d) >= 45000) r = 0;
    stk_release(mk);
    return r;
}

/* 4edf:0b9c */
u16 near_home(u16 p) { return ldist2((u16)(p + 4), flight_home(FLT(p))) < 10000 ? 2 : 1; }

/* 4edf:0bda flight_all_down(f) — latched in f+3A bit 2 */
u16 flight_all_down(u16 f)
{
    if (OB(f, 0x3A) & 2) return 1;
    for (s16 i = 0; OS(f, 6) > i; i++) {
        u16 m = DSW(OW(f, 8) + 2 * i);
        if (OB(f, 0) == 0) { if (!plane_is_down(m)) return 0; }
        else if (!(OB(m, 0x1B) & 2)) return 0;
    }
    OB(f, 0x3A) |= 2;
    return 1;
}

/* 4edf:0c45 flight_next_task(p, flag) — §12.2 */
u16 flight_next_task(u16 p, u16 flag)
{
    u16 f = FLT(p);
    u16 w = wp_current_pos((u16)(f + 0x16));
    DSW(0x402E) = 0;
    if (DESC(p) == D_PARK_CV) return 6;
    s8 role = (s8)OB(f, 2);
    if (role == 6) {
        u16 g = OW(f, 0x10);
        if (flight_all_down(g)) return w ? 3 : near_home(p);
        u16 L = flight_leader(g);
        if (OB(g, 0x3B) & 2) {
            if (w) return 3;
            return wp_cursor_pos((u16)(f + 0x16), 0xFFFF) ? 4 : near_home(p);
        }
        if (!w) return 0;
        u16 w2 = wp_current_pos((u16)(g + 0x16));
        if (w2 && ldist2(w, w2) <= 5000 && DESC(L) == D_WAYPOINTS) return 0;
        return 3;
    }
    if (w) {
        if (role != 5) return 3;
        if (flag && FLT(PLAYER) == f) return 3;
        u16 mk = stk_mark(), d = stk_alloc(4), de = stk_alloc(4);
        u16 r = 3;
        DSW(0x402E) = find_ground_target(p, -1, d);
        if (DSW(0x402E)) {
            nearest_in_list((u16)(DSW(0x402E) + 4), (u16)(s16)OC(p, 0x26), de);
            if (DSSL(d) + 10000 > DSSL(de)) r = 7;
        }
        stk_release(mk);
        return r;
    }
    if (OW(f, 0x3E)) return flight_all_down(OW(f, 0x3E)) ? near_home(p) : 5;
    switch (role) {
    case 2: case 3:
        return near_home(p);
    case 5:
        DSW(0x402E) = find_target_45000(p);
        if (DSW(0x402E)) return 7;
        /* fall through */
    case 0: case 1: case 4:
        return wp_cursor_pos((u16)(f + 0x16), 0xFFFF) ? 4 : near_home(p);
    default:
        return 0xFFFF;
    }
}

/* 4edf:0e58 st_next_task(p) */
void st_next_task(u16 p)
{
    u16 d = 0;
    switch (flight_next_task(p, 0)) {
    case 0: d = D_ESCORT_JOIN; break;
    case 1: d = D_RETURN; break;
    case 2: d = D_ORBIT_HOME; break;
    case 3: d = D_WAYPOINTS; break;
    case 4: d = D_ORBIT_WP; break;
    case 5: d = D_ESCORT_FLY; break;
    case 7: DSW(set_state(p, D_GOTO_TARGET)) = DSW(0x402E); break;
    default: break;                                        /* 6 (parked on a carrier), −1: no change */
    }
    if (d) set_state(p, d);
}

/* 4edf:0edb ai_leave_combat(p) */
void ai_leave_combat(u16 p)
{
    if (plane_in_formation(p)) st_next_task(p);
    else set_state(p, D_JOIN);
}

/* 4edf:0f0a orbit_point(p, c, r) → &DS:4014, a point on the circle of radius r around c */
u16 orbit_point(u16 p, u16 c, s32 r)
{
    u16 mk = stk_mark(), ang = stk_alloc(6);
    u16 b = bearing_between((u16)(p + 4), c);
    s16 e = (s16)(b - OW(p, 0x14));
    DSW(ang) = 0;
    DSW(ang + 2) = 0;
    DSW(ang + 4) = (u16)(b + (e > 0 ? -3000 : 3000));
    u16 o = lmove_dir(0x4014, ang, (u32)r);
    u16 res = ladd(o, c);
    stk_release(mk);
    return res;
}

/* 4edf:0f6d fly_to_point(p, pt, tolh, tolp, dist) → bits 1 heading, 2 pitch, 4 distance */
u16 fly_to_point(u16 p, u16 pt, s16 tolh, s16 tolp, s16 dist)
{
    u16 r = 0;
    u32 e = steer_to_point(p, pt);
    if (in_open_range((s16)((s16)(u16)e >> 4), tolh)) r |= 1;
    if (in_open_range(OS(p, 0x10), tolp)) r |= 2;
    if ((u32)dist3v((u16)(p + 4), pt) < (u32)(s32)dist) r |= 4;             /* (unsigned compare) */
    OW(p, 0xC1) |= 1;
    THROTTLE(p) = CRUISE(p);
    THROTTLE(p) = (u16)(THROTTLE(p) - (OS(p, 0x10) < 600 ? 70 : 10));
    return r;
}

/* 4edf:1013 */
u16 fly_to_point_w(u16 p, u16 pt, s16 tolh, s16 tolp, s16 dist) { return fly_to_point(p, pt, tolh, tolp, dist); }

/* 4edf:102e fly_formation_slot(p, off, L, tolp, tolh, dist) — §12.3 */
u16 fly_formation_slot(u16 p, u16 off, u16 L, u16 tolp, u16 tolh, u16 dist)
{
    u16 mk = stk_mark(), e = stk_alloc(6), o = stk_alloc(6), t1 = stk_alloc(12), t2 = stk_alloc(12),
        t3 = stk_alloc(12), out = stk_alloc(0x18);
    u16 r = 0;
    vsar(dir_from_heading(e, OW(L, 0x14)), 1);
    memcpy(DSP(o), DSP(off), 6);
    u16 x = DSW(o); DSW(o) = DSW(o + 4); DSW(o + 4) = x;
    vrot_z(o, (u16)-OW(L, 0x10));
    x = DSW(o); DSW(o) = DSW(o + 4); DSW(o + 4) = x;
    vrot_z(o, OW(L, 0x14));
    ladd3_v((u16)(L + 4), (u16)(L + 0x70), t1);
    ladd3_v(t1, o, t2);
    vadd(o, e);
    ladd3_v(t2, e, t3);
    steer_intercept(p, L, o, 1, 1, out);
    s32 d = (s32)ldist2((u16)(p + 4), t3);
    if (d > 15000 || OS(p, 0x10) > 600) THROTTLE(p) = CRUISE(p);
    else steer_speed_match(p, L, (u16)d, (u16)(DSW(out + 0x0A) + OW(p, 0x14)), 0x2000);
    if (abs_wrap((s16)(OW(p, 0x14) - OW(L, 0x14))) < (s16)tolh) r |= 1;
    if (abs_wrap((s16)(OW(p, 0x10) - OW(L, 0x10))) < (s16)tolp) r |= 2;
    if ((s32)(s16)dist > (s32)ldist2((u16)(p + 4), t2)) r |= 4;
    OW(p, 0xC1) |= 1;
    stk_release(mk);
    return r;
}

/* 4edf:11da */
static u16 fly_formation_slot_w(u16 p, u16 off, u16 L, u16 tolp, u16 tolh, u16 dist)
{
    return fly_formation_slot(p, off, L, tolp, tolh, dist);
}

/* 4edf:11f8 (the act of most states) */
u16 st_act_fly(u16 p)
{
    plane_fly(p);
    return 0;
}

/* 4edf:120a (041Ch steer) follow waypoints */
u16 st_waypoints_steer(u16 p)
{
    u16 w = flight_waypoint(p);
    if (w) fly_to_point(p, w, 0, 0, 4000);
    else st_next_task(p);
    return 0;
}

/* 4edf:123f formation_offset(form, slot) → &DS:4020 (FORMS.DAT block, far DS:12A8) */
u16 formation_offset(u16 form, u16 slot)
{
    FarPtr t = ds_far(0x12A8);
    t.off = (u16)(t.off + ((u16)((s16)form * 4 + (s16)slot)) * 6);
    memcpy(DSP(0x4020), far_mp(t), 6);
    return 0x4020;
}

/* 4edf:1278 */
u16 formation_offset_of(u16 p, u16 slot) { return formation_offset((u16)(s16)OC(FLT(p), 1), slot); }

/* 4edf:1294 */
u16 my_formation_offset(u16 p) { return formation_offset((u16)(s16)OC(FLT(p), 1), (u16)(s16)OC(p, 0xBC)); }

/* 4edf:12b3 formation_point(out, L, off) */
u16 formation_point(u16 out, u16 L, u16 off)
{
    u16 mk = stk_mark(), o = stk_alloc(6);
    memcpy(DSP(o), DSP(off), 6);
    vrot_z(o, OW(L, 0x14));
    ladd3_v((u16)(L + 4), o, out);
    stk_release(mk);
    return out;
}

/* 4edf:12fa (0430h steer) join formation — quirk: nothing sets Plane+42 bit 0, 0444h is unreachable */
u16 st_join_steer(u16 p)
{
    u16 off = my_formation_offset(p);
    u16 L = first_alive_of(FLT(p));
    if (L) {
        u16 r = fly_formation_slot(p, off, L, 0x800, 0x800, 3000);
        if ((OB(L, 0x42) & 1) && (r & 7) == 7) set_state(p, D_KEEP_FORM);
    }
    return 0;
}

/* 4edf:135a (0444h steer) keep formation — (no test for a missing leader, unreachable anyway) */
u16 st_keep_formation_steer(u16 p)
{
    u16 off = my_formation_offset(p);
    u16 L = first_alive_of(FLT(p));
    u16 r = fly_formation_slot_w(p, off, L, 0x800, 0x800, 3000);
    if (!(OB(L, 0x42) & 1) || (r & 7) != 7) set_state(p, D_JOIN);
    return 0;
}

/* 4edf:13bd (046Ch steer) orbit over home */
u16 st_orbit_home_steer(u16 p)
{
    u16 mk = stk_mark(), pt = stk_alloc(12);
    memcpy(DSP(pt), DSP(orbit_point(p, flight_home(FLT(p)), 8000)), 12);
    DSSL(pt + 8) = 10000;
    fly_to_point(p, pt, 0, 0, 0);
    stk_release(mk);
    return 0;
}

/* 4edf:1413 (04D0h steer) fly to a stored target */
u16 st_goto_target_steer(u16 p)
{
    u16 mk = stk_mark(), pt = stk_alloc(12);
    memcpy(DSP(pt), DSP(orbit_point(p, (u16)(DSW(SCR(p)) + 4), 8000)), 12);
    DSSL(pt + 8) = 10000;
    fly_to_point(p, pt, 0, 0, 0);
    stk_release(mk);
    return 0;
}

/* 4edf:1463 (0480h steer) orbit the last waypoint */
u16 st_orbit_waypoint_steer(u16 p)
{
    u16 w = wp_cursor_pos((u16)(FLT(p) + 0x16), 0xFFFF);
    fly_to_point(p, orbit_point(p, w, 10000), 0, 0, 0);
    return 0;
}

/* 4edf:14a6 (0494h steer) return to base — AI planes orbit at home, they never land */
u16 st_return_steer(u16 p)
{
    u16 mk = stk_mark(), pt = stk_alloc(12);
    memcpy(DSP(pt), DSP(flight_home(FLT(p))), 12);
    DSSL(pt + 8) = 16000;
    u16 r = fly_to_point(p, pt, 200, 500, 500);
    if (r & 4) set_state(p, D_ORBIT_HOME);
    stk_release(mk);
    return 0;
}

/* 4edf:1503 first_alive_of(f) */
u16 first_alive_of(u16 f)
{
    for (s16 i = 0; OS(f, 6) > i; i++) {
        u16 m = DSW(OW(f, 8) + 2 * i);
        if (!plane_is_down(m)) return m;
    }
    return 0;
}

/* 4edf:153e (04A8h steer) escort: join */
u16 st_escort_join_steer(u16 p)
{
    u16 mk = stk_mark(), off = stk_alloc(6);
    switch (escort_situation(p)) {
    case 0: ai_leave_combat(p); break;
    case 1: fly_to_point(p, DSW(0x402C), 0, 0, 0); break;
    case 2: fly_to_point(p, (u16)(DSW(0x402A) + 4), 0, 0, 0); break;
    case 3:
        memcpy(DSP(off), far_mp(far_add(ds_far((u16)(FLT(p) + 0x52)), 0x28)), 6);
        fly_formation_slot(p, off, DSW(0x402A), 0, 0, 0);
        break;
    default: break;
    }
    stk_release(mk);
    return p != 0;
}

/* 4edf:15da (04BCh steer) escort: fly with */
u16 st_escort_fly_steer(u16 p)
{
    u16 L = first_alive_of(OW(FLT(p), 0x3E));
    if (!L) {
        ai_leave_combat(p);
        return p != 0;
    }
    u16 mk = stk_mark(), off = stk_alloc(6), slot = stk_alloc(12);
    memcpy(DSP(off), far_mp(far_add(ds_far((u16)(FLT(p) + 0x52)), 0x28)), 6);
    formation_point(slot, L, off);
    if (OSL(L, 0x0C) > 2000) {
        if (ldist2((u16)(p + 4), slot) > 20000) {
            fly_to_point(p, slot, 0, 0, 0);
            THROTTLE(p) = CRUISE(p);
        } else {
            fly_formation_slot(p, off, L, 0, 0, 0);
        }
    } else {
        memcpy(DSP(slot), DSP(orbit_point(p, (u16)(L + 4), 10000)), 12);
        DSSL(slot + 8) = 7000;
        fly_to_point(p, slot, 0, 0, 0);
        THROTTLE(p) = CRUISE(p);
    }
    stk_release(mk);
    return p != 0;
}

/* 4edf:16ea st_think_combat(p) — the think of the flight-level states */
u16 st_think_combat(u16 p)
{
    if (p != PLAYER && !ai_think_target(p)) ai_leave_combat(p);
    return 0;
}

/* 4edf:1711 pure_fighters_of_side(out, side) — 0-terminated; returns the count */
u16 pure_fighters_of_side(u16 out, u16 side)
{
    u16 o = out;
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
        u16 q = DSW(DSW(0x475B) + 2 * i);
        if ((s16)OC(q, 0x26) != (s16)side) continue;
        if (!is_pure_fighter(q) || plane_is_down(q)) continue;
        DSW(o) = q;
        o = (u16)(o + 2);
    }
    DSW(o) = 0;
    return (u16)((o - out) >> 1);
}

/* 4edf:1774 — the 15 descriptors at DS:04E4 */
void flight_states_register(void) { class_register_n(0x0F, 0x04E4); }

__attribute__((constructor)) static void ai_flight_codeptrs(void)
{
    static const struct { u32 a; void *f; } t[] = {
        { AP_FN_st_path_act, st_path_act }, { AP_FN_st_runway_takeoff_act, st_runway_takeoff_act },
        { AP_FN_st_carrier_takeoff_act, st_carrier_takeoff_act }, { AP_FN_st_parked_carrier_act, st_parked_carrier_act },
        { AP_FN_st_parked_airfield_act, st_parked_airfield_act }, { AP_FN_st_act_fly, st_act_fly },
        { AP_FN_st_waypoints_steer, st_waypoints_steer }, { AP_FN_st_join_steer, st_join_steer },
        { AP_FN_st_keep_formation_steer, st_keep_formation_steer }, { AP_FN_st_orbit_home_steer, st_orbit_home_steer },
        { AP_FN_st_goto_target_steer, st_goto_target_steer }, { AP_FN_st_orbit_waypoint_steer, st_orbit_waypoint_steer },
        { AP_FN_st_return_steer, st_return_steer }, { AP_FN_st_escort_join_steer, st_escort_join_steer },
        { AP_FN_st_escort_fly_steer, st_escort_fly_steer }, { AP_FN_st_think_combat, st_think_combat },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) codeptr_register(PROG_AP, t[i].a, (CodeFn)t[i].f);
}
