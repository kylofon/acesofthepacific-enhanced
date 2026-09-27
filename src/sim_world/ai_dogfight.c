/* sim_world part B — dog-fight states (12fd). sim_world_ai.md §9; every function checked against the
 * disassembly. Corrections to the spec from the disassembly: extend_enter(p, 1) enters 1791h (level
 * off) and (p, 0) 17A5h; the aerobatic choice of st_pursue_steer tests the altitude difference
 * (p.z − t.z > 500 → Immelmann 3, else loop 6); st_recover_steer restarts itself (17B9h) on an even coin
 * and falls back to 17E1h when no manoeuvre is picked. Quirk kept: st_cross_steer with the player as
 * the mate ends on its first frame (the timer is still running). */
#include "sim_world/swb_priv.h"

/* 12fd:0000 */
static void steer_by_angles(u16 p, u16 t, u16 ang)
{
    steer_heading_lead(p, t, DSW(ang + 4));
    steer_climb(p, t, DSW(ang), 0);
}

/* 12fd:002e steer_toward(p, t, pt) → DX:AX = {heading error, pitch error} */
u32 steer_toward(u16 p, u16 t, u16 pt)
{
    u16 mk = stk_mark(), a = stk_alloc(6);
    angles_to(a, pt, (u16)(p + 4));
    vsub(a, (u16)(p + 0x10));
    u32 r = (u32)DSW(a + 4) << 16 | DSW(a);
    steer_by_angles(p, t, a);
    stk_release(mk);
    return r;
}

/* 12fd:0086 */
void steer_lead(u16 p, u16 t)
{
    u16 mk = stk_mark(), a = stk_alloc(6);
    DSW(a) = OW(p, 0xFD);
    DSW(a + 2) = 0;
    DSW(a + 4) = OW(p, 0xFB);
    steer_by_angles(p, t, a);
    stk_release(mk);
}

/* 12fd:00b5 pursue_geometry(p, t) → 1 in position, 2 on the tail, 4 nose-on, 8 side (§9.1) */
u16 pursue_geometry(u16 p, u16 t)
{
    u16 mk = stk_mark(), off = stk_alloc(6), o = stk_alloc(6), save = stk_alloc(12), out = stk_alloc(0x18),
        a = stk_alloc(6);
    u16 ret = 0, tail = 0;
    u16 P = PIL(p);
    u16 aim = (u16)(p + 0xCB);
    u16 los = (u16)(DSW(aim + 6) + OW(p, 0x14) + 0x8000);
    s16 asp = abs16((s16)(OW(t, 0x14) - los));
    DSW(off + 4) = 0;
    if (asp < 0x2000) {
        DSW(off) = DSS(aim + 6) < 0 ? 0x1F40 : 0xE0C0;
        DSW(off + 2) = 0;
        ret |= 4;
    } else {
        DSW(off) = 0;
        DSW(off + 2) = 0xFE0C;
        if (asp < 25000) ret |= 8;
        else tail = 1;
    }
    memcpy(DSP(save), DSP(t + 4), 12);
    u16 save_vz = OW(t, 0x74);
    if (DSW(off) != 0) {
        memcpy(DSP(o), DSP(off), 6);
        vrot_z(o, OW(t, 0x14));
    } else {
        vec_rotate(off, obj_matrix(OW(t, 0x20)), o);
    }
    steer_intercept(p, t, o, 0, 0, out);
    if (!tail) {
        OSL(t, 0x0C) = DSSL(save + 8);
        OW(t, 0x74) = save_vz;
        THROTTLE(p) = DSW(P + 0x10);
    } else {
        ladd_v(save, o);
        ret = 1;
        s32 d = dist2v((u16)(p + 4), save);
        if (d > 10000) {
            THROTTLE(p) = DSW(P + 0x10);
        } else {
            u16 dl = (u16)d;
            if ((u16)(dl - DSW(off + 2) + 0xFE70) < 0x17D4) {
                DSW(a) = (u16)(OW(p, 0xCF) + OW(p, 0x10));
                DSW(a + 2) = 0;
                DSW(a + 4) = (u16)(OW(p, 0xD1) + OW(p, 0x14));
                if (on_tail(p, t, a)) ret |= 2;
            }
            steer_speed_match(p, t, dl, (u16)(OW(p, 0x14) + DSW(out + 0x0A)), 2000);
        }
    }
    stk_release(mk);
    return ret;
}

/* 12fd:02b8 / 030a — attacker slots on the target */
static u16 slot_taken(u16 p, u16 t, u16 off)
{
    u16 q = OW(t, off);
    if (!q) return 0;
    if (plane_is_down(q)) { OW(t, off) = 0; return 0; }
    if (q == p) return 0;
    if (DFLAGS(DESC(q)) & 8) return 1;
    OW(t, off) = 0;
    return 0;
}
u16 slot1_taken(u16 p, u16 t) { return slot_taken(p, t, 0xD9); }
u16 slot2_taken(u16 p, u16 t) { return slot_taken(p, t, 0xDB); }

/* 12fd:035c */
u16 too_low(u16 p, s16 margin)
{
    s32 g = OS(p, 0xB8);
    if (g > 0 && g > 2000) g += 3000;
    g += margin;
    return OSL(p, 0x0C) < g;
}

/* 12fd:03a6 (186Dh) */
static u16 st_pullup_steer(u16 p)
{
    steer_pitch_to(p, 2000, 0);
    steer_roll_to(p, 0, 0);
    THROTTLE(p) = CRUISE(p);
    return !too_low(p, 1500);
}

/* 12fd:03e8 — (named "evade check" before) */
u16 ground_avoid_check(u16 p)
{
    if (DFLAGS(DESC(p)) & 0x100) return 0;
    if (OB(p, 0x45) & 0x40) return 0;
    if (!too_low(p, 1200)) return 0;
    set_state(p, D_PULLUP);
    return 1;
}

/* 12fd:042a (16B5h) */
u16 st_climb_steer(u16 p)
{
    THROTTLE(p) = CRUISE(p);
    u16 t = OW(p, 0xD7);
    if (t) fight_choose(p, t);
    steer_roll_to(p, -1000, 0);
    steer_pitch_to(p, 4000, 10);
    return 0;
}

/* 12fd:0479 */
u16 head_on_window(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 a = (u16)(OW(p, 0x14) + 0x8000 - OW(t, 0x14));
    if ((u16)(a + 0x2EE0) >= 0x5DC0) return 0;
    if ((u16)(OW(p, 0xD1) + 0x1B58) >= 0x36B0) return 0;
    if ((u16)(OW(p, 0xCF) + 0x0FA0) >= 0x1F40) return 0;
    return OSL(p, 0xD3) > 4500;
}

/* 12fd:04ca (16C9h) — §9.2 */
static u16 st_pursue_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 P = PIL(p);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    if (!head_on_window(p)) {
        DSW(s) = 0;
    } else if (DSW(s) == 0) {
        DSW(s) = 1;
        u16 a = (s16)(RNG() & 0x3FF) < DSS(P + 0x24);                              /* RNG */
        u16 b = (s16)(RNG() & 0x3FF) < DSS(P + 0x20);                              /* RNG */
        if (a == b) {
            if (!fight_choose(p, t)) set_state(p, D_HEADON);
            return 0;
        }
    }
    u16 r = pursue_geometry(p, t);
    if ((r & 1) && can_dogfight(t, p)) {
        OW(p, 0xC1) |= 8;
        alert_raise(t, 3, p);
    }
    if (fight_choose(p, t)) return 0;
    if (r & 4) {
        u16 dh = (u16)(OW(p, 0x14) - OW(t, 0x14) + 0x8000);
        if ((s16)(RNG() & 0x3FF) < DSS(P + 0x42) && (u16)(dh + 0x0FA0) < 0x1F40 &&     /* RNG */
            ((u16)(OW(p, 0xD1) + 0x6D60) >= 0xDAC0 || OSL(p, 0xD3) < 2000) &&
            (u16)(OW(p, 0x12) + 0x0BB8) < 0x1770)
            manoeuvre_enter(OSL(p, 0x0C) - OSL(t, 0x0C) > 500 ? 3 : 6, 0, p);
        THROTTLE(p) = (u16)(DSW(P + 0x10) - 5);
        if (OB(p, 0x44) & 2) set_state(p, D_DOGFIGHT);
    } else if (r & 8) {
        THROTTLE(p) = (u16)(DSW(P + 0x10) - 5);
        if (OB(p, 0x44) & 2) set_state(p, D_DOGFIGHT);
    } else if (r & 2) {
        set_state(p, D_GUNS);
        alert_victim(p, t);
    }
    return 0;
}

/* 12fd:06c2 steer_lead_close(p, t, ang) */
void steer_lead_close(u16 p, u16 t, u16 ang)
{
    u16 mk = stk_mark(), a = stk_alloc(6);
    memcpy(DSP(a), DSP(ang), 6);
    OW(p, 0xC1) |= 8;
    OW(t, 0xDB) = p;
    steer_heading_lead(p, t, DSW(a + 4));
    steer_climb(p, t, DSW(a), 0);
    vadd(a, (u16)(p + 0x10));
    s32 r = OSL(p, 0xD3);
    s32 x = (r + 2500) - dist2(p, t);
    steer_speed_match(p, t, (u16)r, DSW(a + 4), (u16)x);
    stk_release(mk);
}

/* 12fd:076b (16F1h) */
static u16 st_guns_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    u16 mk = stk_mark(), a = stk_alloc(6);
    DSW(a) = OW(p, 0xCF);
    DSW(a + 2) = 0;                                        /* (uninitialised in the original, unused) */
    DSW(a + 4) = OW(p, 0xD1);
    steer_lead_close(p, t, a);
    vadd(a, (u16)(p + 0x10));
    if (on_tail(p, t, a)) alert_victim(p, t);
    else set_state(p, D_PURSUE);
    stk_release(mk);
    return 0;
}

/* 12fd:07f7 (1741h) */
static u16 st_manoeuvre_steer(u16 p)
{
    u16 done = manoeuvre_step(p) ? 1 : 0;
    alert_raise(p, 11, 0);
    alert_raise(p, 10, 0);
    return done;
}

/* 12fd:082b st_headon_pass(p, side) */
static u16 st_headon_pass(u16 p, u16 side)
{
    u16 P = PIL(p);
    s16 r = (s16)(DSS(P + 0x0C) >> 1);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(OW(p, 0xD7) + 4))) return 1;
    if (side == 0) r = (s16)-r;
    THROTTLE(p) = (u16)(DSW(P + 0x10) - 20);
    steer_pitch_to(p, 0, 0);
    steer_roll_to(p, r, 0);
    return 0;
}
static u16 st_headon_a(u16 p) { return st_headon_pass(p, 1); }            /* 12fd:08a7 (1719h) */
static u16 st_headon_b(u16 p) { return st_headon_pass(p, 0); }            /* 12fd:08b8 (172Dh) */

/* 12fd:08c9 extend_enter(p, level) — 1: state 1791h, 0: state 17A5h; timer = pilot +06 << 8 */
void extend_enter(u16 p, u16 level)
{
    u16 P = PIL(p);
    set_state(p, level ? D_LEVEL : D_EXTEND);
    OSL(p, 0x4C) = (s16)(DSW(P + 6) << 8);
}

/* 12fd:090b (1755h) */
static u16 st_manoeuvre_then_extend(u16 p)
{
    if (st_manoeuvre_steer(p)) extend_enter(p, 1);
    return 0;
}

/* 12fd:092d (think of 1769h) */
static u16 st_defend_think(u16 p)
{
    set_state(p, OW(p, 0xDD) ? D_JOIN : D_WAYPOINTS);
    return 1;
}

/* 12fd:0955 */
u16 target_heading(u16 t) { return DESC(t) == D_DEFEND ? DSW(SCR(t) + 2) : OW(t, 0x14); }

/* 12fd:0973 weave(p, h0, side) */
u16 weave(u16 p, u16 h0, u16 side)
{
    s16 o = side ? 3000 : -3000;
    s16 d = (s16)(h0 - OW(p, 0x14));
    steer_heading_nolead(p, 0, (u16)(o + d));
    steer_pitch_to(p, 0, 0);
    if ((u16)(d + 400) >= 800 && (u16)(d < 0) == side) side = side == 0;
    return side;
}

/* 12fd:09e2 (1769h) */
u16 st_defend_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 r = 1;
    if (DSW(s) == 0) {
        DSW(s) = 1;
        DSW(s + 2) = OW(p, 0x14);
    }
    if (OW(t, 0xD7) == p && OSL(p, 0xD3) < 9000 && (u16)(OW(t, 0xD1) + 0x4000) < 0x8000) {
        r = 0;
        DSW(s + 4) = weave(p, DSW(s + 2), DSW(s + 4));
    }
    return r;
}

/* 12fd:0a44 wingmen_ready(lead, out) */
static s16 wingmen_ready(u16 lead, u16 out)
{
    u16 f = FLT(lead);
    s16 n = 0;
    for (s16 i = 0; OS(f, 6) > i; i++) {
        u16 m = DSW(OW(f, 8) + 2 * i);
        if (OW(m, 0xDD) != lead) continue;
        u16 ready = 0;
        if (m == PLAYER) {
            u16 mk = stk_mark(), v = stk_alloc(6), c = stk_alloc(16), off = stk_alloc(6);
            memcpy(DSP(v), DSP(PLAYER + 0x6A), 6);
            memcpy(DSP(c), DSP(PLAYER + 0xAC), 15);
            slot_offset(PLAYER, lead, off);
            OB(lead, 0x42) |= 0x10;
            OB(lead, 0x46) |= 0x10;
            u16 r = fly_formation_slot(PLAYER, off, lead, 0x3800, 0x3800, 0x1388);
            if ((r & 7) == 7) ready = 1;
            memcpy(DSP(PLAYER + 0x6A), DSP(v), 6);
            memcpy(DSP(PLAYER + 0xAC), DSP(c), 15);
            stk_release(mk);
        } else if (DESC(m) == D_COVER) {
            ready = 1;
        }
        if (ready) { DSW(out) = m; out = (u16)(out + 2); n++; }
    }
    return n;
}

/* 12fd:0b79 pincer(lead, t) */
static u16 pincer(u16 lead, u16 t)
{
    u16 mk = stk_mark(), w = stk_alloc(0x10);
    u16 r = 0;
    if (PW(lead, 0x22) == 0) goto out;
    if (wingmen_ready(lead, w) != 2) goto out;
    if (!manoeuvre_allowed(0, lead)) goto out;
    if (DSW(DSW(w)) & 7) {                                 /* class bits of the object: always for planes */
        u16 x = DSW(w);
        DSW(w) = DSW(w + 2);
        DSW(w + 2) = x;
    }
    if (!manoeuvre_allowed(11, DSW(w))) goto out;
    if (!manoeuvre_allowed(12, DSW(w + 2))) goto out;
    s16 d = (s16)dist3(lead, t);
    if (d <= 200) goto out;
    if (d < 4000) {
        u16 a = DSW(w), b = DSW(w + 2);
        u16 ap = a == PLAYER;
        if (ap || b == PLAYER) {
            u16 rec = radio_rec(0x18);
            DSW(rec) = ap ? 16000 : (u16)-16000;
            radio_queue7(0x38, rec, 0);
        }
        manoeuvre_enter(0, 0, lead);
        manoeuvre_enter(11, 0, a);
        manoeuvre_enter(12, 0, b);
    }
    r = 1;
out:
    stk_release(mk);
    return r;
}

/* 12fd:0caa can_dogfight(t, p) — p is on t's tail, pointing at it */
u16 can_dogfight(u16 t, u16 p)
{
    if (OW(p, 0xD7) != t) return 0;
    if (OSL(p, 0xD3) >= 9000) return 0;
    if ((u16)(OW(p, 0xD1) + 0x2000) >= 0x4000) return 0;
    return (u16)(aspect_to(p, t) + 0x6000) >= 0xC000;
}

/* 12fd:0cf3 thach_call(p, t) (RNG ×1–2) */
static u16 thach_call(u16 p, u16 t)
{
    if (!can_dogfight(p, t)) return 0;
    if (OB(p, 0x26) == 1) return 0;
    u16 mk = stk_mark(), l = stk_alloc(0x10);
    u16 a = 0;
    if (mates_of(p, 0, l) == 0) goto out;
    u16 w = DSW(l);
    if ((u32)dist3(p, w) >= 8000) goto out;
    if (w != PLAYER && (DFLAGS(DESC(w)) & 0x20)) goto out;
    u16 P = PIL(p);
    a = (s16)(RNG() & 0x3FF) < DSS(P + 0x4C);                                     /* RNG */
    if (!a && (s16)(RNG() & 0x3FF) >= DSS(P + 0x4E)) goto out;                    /* RNG */
    if (a) weave_enter(p, w);
    if (w == PLAYER) {
        u16 rec = radio_rec(0x18);
        DSW(rec + 0x16) = p;
        radio_queue7(0x59, rec, 0);
    } else {
        cross_enter(w, p, t);
    }
out:
    stk_release(mk);
    return a;
}

/* 12fd:0e0d high_enough_4000(AX = candidate, BX = p) — candidate 20's extra condition (DS:1661) */
u16 high_enough_4000(u16 ax, u16 bx) { (void)ax; return !too_low(bx, 4000); }

/* 12fd:0e28 (177Dh) — §9.3 */
static u16 st_dogfight_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 lead = OW(p, 0xDD) ? OW(p, 0xDD) : p;
    u16 P = PIL(p);
    u16 A = (u16)(DAT(p) + 0x3E);
    u16 aim = (u16)(p + 0xCB);
    u16 ret = 0;
    if (DSW(s) == 0) {
        DSW(s) = 1;
        if ((s16)(RNG() & 0x3FF) < DSS(P + 0x18)) {                                /* RNG */
            extend_enter(p, 0);
            return 0;
        }
    }
    if (pincer(lead, t)) return 0;
    if (thach_call(p, t)) return 0;
    u16 mk = stk_mark(), w = stk_alloc(22), id = stk_alloc(22);
    for (int i = 0; i < 21; i++) {
        DSB(w + i) = DSB(A + i) ? DSB(P + 0x2C + i) : 0;
        DSB(id + i) = (u8)i;
    }
    u16 faster = (s16)(OS(t, 0x6C) - OS(p, 0x6C)) > 200 || OSL(t, 0x0C) + 600 < OSL(p, 0x0C);
    if (faster && (s16)(RNG() & 0x3FF) < DSS(P + 0x20)) {                          /* RNG */
        DSB(w + 17) = DSB(w + 20) = 0;
        DSB(w + 14) = DSB(w + 13) = 0;
        DSB(w + 16) = DSB(w + 15) = 0;
    }
    u16 lower = OSL(t, 0x0C) - 600 > OSL(p, 0x0C) || DSSL(aim + 8) > 4000;
    if (lower && (s16)(RNG() & 0x3FF) < DSS(P + 0x20)) {                           /* RNG */
        DSB(w + 7) = 0;
        DSB(w + 12) = DSB(w + 11) = 0;
    }
    u16 c = (s16)(RNG() & 0x3FF) < DSS(P + 0x20);                                  /* RNG */
    int to, from;
    if (c == (u16)(DSS(aim + 6) < 0)) { to = 19; from = 18; }
    else { to = 18; from = 19; }
    DSB(w + to) = (u8)(DSB(w + to) + DSB(w + from));
    DSB(w + from) = 0;
    s16 lim = (s16)(((s32)DSS(P + 0x20) * 99) >> 10);
    if (lim >= 99) lim = 99;
    for (int i = 0; i < 20; i++)
        if ((s16)DSC(A + i) <= lim) DSB(w + i) = 0;
    for (int i = 0; i < 21; i++) {
        if (DSB(w + i) == 0) continue;
        u16 ok;
        if (i < 20) {
            ok = manoeuvre_allowed((u16)i, p);
        } else {
            FarPtr f = ds_far((u16)(0x1661 + 4 * (i - 20)));
            if (far_is_null(f)) continue;
            ok = ((u16 (*)(u16, u16))codeptr_lookup(f))((u16)i, p);
        }
        if (!ok) DSB(w + i) = 0;
    }
    s16 m = (s16)manoeuvre_pick(21, id, w);                                             /* RNG */
    if (m < 0) {
        ret = st_defend_steer(p);
    } else if (m < 20) {
        manoeuvre_enter((u16)m, 1, p);
    } else {
        u16 rec = (u16)(0x160D + 4 * m);
        if ((s32)DSS(rec + 2) < OSL(p, 0x0C)) set_state(p, DSW(rec));
    }
    stk_release(mk);
    return ret;
}

/* 12fd:110a */
void weave_enter(u16 p, u16 mate)
{
    u16 s = set_state(p, D_WEAVE);
    DSW(s) = mate;
    DSSL(s + 2) = 50000;
}

/* 12fd:112e cross_enter(w, p, t) */
void cross_enter(u16 w, u16 p, u16 t)
{
    u16 s = set_state(w, D_CROSS);
    DSW(s) = p;
    DSSL(s + 2) = 50000;
    OW(w, 0xD7) = t;
}

/* 12fd:115d (1845h) */
static u16 st_weave_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 mate = DSW(s);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    THROTTLE(p) = CRUISE(p);
    DSW(s + 6) = weave(p, bearing_between((u16)(p + 4), (u16)(mate + 4)), DSW(s + 6));
    u16 done = mate == PLAYER ? (u16)timer32_expired((u16)(s + 2)) : DESC(mate) != D_CROSS;
    if (!done && OW(t, 0xD7) == p && (u16)(OW(t, 0xD1) + 0x3000) < 0x6000) return 0;
    OSL(p, 0xDF) = 0;
    return 1;
}

/* 12fd:1225 (1831h) — §9.4 */
static u16 st_cross_steer(u16 p)
{
    u16 mk = stk_mark(), off = stk_alloc(6), a = stk_alloc(6), out = stk_alloc(0x18);
    u16 r = 0;
    memcpy(DSP(off), DSP(0x1654), 6);
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 aim = (u16)(p + 0xCB);
    u16 mate = DSW(s);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) { r = 1; goto out; }
    DSW(a) = (u16)(DSW(aim + 4) + OW(p, 0x10));
    DSW(a + 2) = 0;
    DSW(a + 4) = (u16)(DSW(aim + 6) + OW(p, 0x14));
    u16 end = mate == PLAYER ? timer32_expired((u16)(s + 2)) == 0 : DESC(mate) != D_WEAVE;
    if (end) { OSL(p, 0xDF) = 0; r = 1; goto out; }
    if (on_tail(p, t, a)) {
        DSW(a) = DSW(aim + 4);
        DSW(a + 4) = DSW(aim + 6);
        steer_lead_close(p, t, a);
        alert_victim(p, t);
        OB(t, 0x43) |= 4;
        OB(t, 0x47) |= 4;
        goto out;
    }
    s32 dt = dist2(t, mate);
    if ((u32)dist2(p, mate) < (u32)(dt + 500)) {
        s16 b = (s16)(bearing_between((u16)(mate + 4), (u16)(p + 4)) - OW(p, 0x14));
        steer_heading_nolead(p, 0, (u16)(b + (b < 0 ? 0x4000 : 0xC000)));
        steer_altitude(p, (u32)(OSL(p, 0x0C) - OSL(mate, 0x0C)), 0);
    } else {
        s32 d = dist2(p, t);
        DSW(off + 2) = (u16)(DSW(off + 2) + 5000);
        vrot_z(off, OW(t, 0x14));
        steer_intercept(p, t, off, 0, 1, out);
        if (d < 12000) steer_speed_match(p, t, (u16)d, (u16)(DSW(out + 0x0A) + OW(p, 0x14)), 5000);
        else THROTTLE(p) = CRUISE(p);
    }
out:
    stk_release(mk);
    return r;
}

/* 12fd:1438 (1791h) */
static u16 st_level_steer(u16 p)
{
    u16 td = DESC(OW(p, 0xD7));
    THROTTLE(p) = CRUISE(p);
    steer_level(p);
    if (td != D_PURSUE && td != D_TAC_B && td != D_HEADON && td != D_GUNS && td != D_CROSS &&
        OSL(p, 0x4C) > 3000)
        OSL(p, 0x4C) = 3000;
    return 0;
}

/* 12fd:1491 (17A5h) */
static u16 st_extend_steer(u16 p) { return st_level_steer(p); }

/* 12fd:14a0 dive_geometry(p) */
static u16 dive_geometry(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 mk = stk_mark(), save = stk_alloc(12), off = stk_alloc(6);
    memcpy(DSP(save), DSP(t + 4), 12);
    u16 cf = OW(p, 0xCF);
    s32 dz = OSL(p, 0x0C) - OSL(t, 0x0C);
    u16 slow = dz < 5000;
    DSW(off + 4) = dz > 5000 ? (u16)dz : 5000;
    DSW(off) = 0;
    DSW(off + 2) = 0xF63C;
    vrot_z(off, OW(t, 0x14));
    ladd_v((u16)(t + 4), off);
    OW(p, 0xCF) = 0;
    u16 r = pursue_geometry(p, t);
    if (slow) THROTTLE(p) = CRUISE(p);
    OW(p, 0xCF) = cf;
    memcpy(DSP(t + 4), DSP(save), 12);
    if (!slot2_taken(p, t)) set_state(p, D_PURSUE);
    stk_release(mk);
    return r;
}

/* 12fd:15a3 (16DDh) */
static u16 st_dive_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 r = dive_geometry(p);
    if (r & 1) {
        if (slot1_taken(p, t)) {
            set_state(p, D_CLIMB);
            return st_climb_steer(p);
        }
        OW(t, 0xD9) = p;
    }
    return 0;
}

/* 12fd:15f4 (17B9h) (RNG) */
static u16 st_recover_steer(u16 p)
{
    u16 low = too_low(p, 4000);
    THROTTLE(p) = CRUISE(p);
    if (low || OSL(p, 0x4C) == 0) {
        if (!low && (RNG() & 1) == 0) {                                             /* RNG */
            set_state(p, D_RECOVER);
        } else {
            u16 P = PIL(p), A = (u16)(DAT(p) + 0x3E);
            u16 mk = stk_mark(), w = stk_alloc(4);
            memset(DSP(w), 0, 3);
            for (int j = 0; j < 3; j++) {
                s16 i = (s16)DSC(0x165A + j);
                if (i >= 0 && i < 21 && DSB(A + i) != 0 && manoeuvre_allowed((u16)i, p)) DSB(w + j) = DSB(P + 0x2C + i);
            }
            s16 m = (s16)manoeuvre_pick(3, 0x165A, w);                                  /* RNG */
            if (m > -1) manoeuvre_enter((u16)m, 1, p);
            else set_state(p, D_LEVEL2);
            stk_release(mk);
        }
    }
    steer_roll_to(p, 0, 0);
    steer_pitch_to(p, DSW(DAT(p) + 0x3A), 0);
    return 0;
}

/* 12fd:1753 (17CDh) */
static u16 st_headon_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    OW(p, 0xC1) |= 8;
    THROTTLE(p) = CRUISE(p);
    steer_lead(p, t);
    if (!head_on_window(p)) set_state(p, D_PURSUE);
    else alert_victim(p, t);
    return 0;
}

/* 12fd:17c4 (1705h) */
static u16 st_dive2_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 r = dive_geometry(p);
    OW(t, 0xD9) = p;
    if (!(r & 2)) set_state(p, D_DIVE);
    return 0;
}

/* 12fd:17fc (17E1h) */
static u16 st_level2_steer(u16 p)
{
    steer_level(p);
    THROTTLE(p) = (u16)(CRUISE(p) - 20);
    return 0;
}

/* 12fd:181f (17F5h) — §9.5 */
static u16 st_regroup_steer(u16 p)
{
    u16 s = SCR(p);
    THROTTLE(p) = CRUISE(p);
    if (timer16_dec(s) == 0) {
        u16 mk = stk_mark(), m = stk_alloc(0x20);          /* (10 bytes in the original) */
        u16 best = 0;
        s16 n = (s16)flight_alive_members(FLT(p), m);
        u16 fighting = 0;
        u16 me_lead = p == DSW(m);
        u16 all_in = 1;
        DSW(s) = 10000;
        if (n < 2) { st_next_task(p); stk_release(mk); return 0; }
        for (s16 i = 0; i < n; i++) {
            u16 q = DSW(m + 2 * i);
            if (q != p && (OB(q, 0x1C) & 1)) { fighting = 1; break; }
        }
        if (!fighting && !me_lead) {
            best = DSW(m);
        } else {
            s32 mind = 0x10000000, maxd = 0;
            for (s16 i = 0; i < n; i++) {
                u16 q = DSW(m + 2 * i);
                if (q == p) continue;
                u16 joining = DESC(q) != D_JOIN;
                s32 d = dist2(q, p);
                if (joining) all_in = 0;
                if (fighting) {
                    if ((OB(q, 0x1C) & 1) && d < mind) { best = q; mind = d; }
                } else if (d > maxd && joining) {
                    best = q;
                    maxd = d;
                }
            }
        }
        stk_release(mk);
        if (me_lead && all_in) { st_next_task(p); return 0; }
        DSW(s + 2) = best;
        if (!fighting && !me_lead && (u32)dist2(p, best) < 8000) { set_state(p, D_JOIN); return 0; }
    }
    steer_to_point(p, (u16)(DSW(s + 2) + 4));
    return 0;
}

/* 12fd:1a78 slot_offset(p, lead, out) */
u16 slot_offset(u16 p, u16 lead, u16 out)
{
    memcpy(DSP(out), DSP(0x164E), 6);
    u16 f = FLT(p);
    if (!(OW(f, 6) == 3 && OB(f, 0x42) == 1)) {
        if (OB(p, 0x1A) & 0x80) {
            if (OS(lead, 0x12) > 3000) OB(p, 0x1A) &= 0x7F;
        } else if (OS(lead, 0x12) < -3000) {
            OB(p, 0x1A) |= 0x80;
        }
    }
    if (OB(p, 0x1A) & 0x80) DSW(out) = (u16)-DSS(out);
    return out;
}

/* 12fd:1a1e (1809h) / 12fd:1ad8 (181Dh) */
static u16 cover_slot(u16 p)
{
    u16 w = OW(p, 0xDD);
    u16 mk = stk_mark(), off = stk_alloc(6);
    slot_offset(p, w, off);
    OB(p, 0x42) |= 0x10;
    OB(p, 0x46) |= 0x10;
    u16 r = fly_formation_slot(p, off, w, 0x3800, 0x3800, 0x1388);
    stk_release(mk);
    return r;
}
static u16 st_cover_steer(u16 p)
{
    if ((cover_slot(p) & 7) != 7) set_state(p, D_REJOIN);
    return 0;
}
static u16 st_rejoin_steer(u16 p)
{
    if ((cover_slot(p) & 7) == 7) set_state(p, D_COVER);
    return 0;
}

/* 12fd:1b31 */
void bank_enter(u16 p, u16 dir) { DSW(set_state(p, D_BANK)) = dir; }

/* 12fd:1b4d (1859h) */
static u16 st_bank_steer(u16 p)
{
    s16 r = PW(p, 0x0C);
    if (DSW(SCR(p))) r = (s16)-r;
    steer_pitch_to(p, 0, 0);
    steer_roll_to(p, r, 0);
    if (OSL(p, 0x4C) == 0) {
        OB(p, 0x43) |= 1;
        OB(p, 0x47) |= 1;
    }
    return 0;
}

/* 12fd:1bad — the 23 descriptors at DS:1881 */
void dogfight_register(void) { class_register_n(0x17, 0x1881); }

__attribute__((constructor)) static void ai_dogfight_codeptrs(void)
{
    static const struct { u32 a; void *f; } t[] = {
        { AP_FN_st_pullup_steer, st_pullup_steer }, { AP_FN_st_climb_steer, st_climb_steer },
        { AP_FN_st_pursue_steer, st_pursue_steer }, { AP_FN_st_guns_steer, st_guns_steer },
        { AP_FN_st_manoeuvre_steer, st_manoeuvre_steer }, { AP_FN_st_headon_a, st_headon_a },
        { AP_FN_st_headon_b, st_headon_b }, { AP_FN_st_manoeuvre_then_extend, st_manoeuvre_then_extend },
        { AP_FN_st_defend_think, st_defend_think }, { AP_FN_st_defend_steer, st_defend_steer },
        { AP_FN_high_enough_4000, high_enough_4000 }, { AP_FN_st_dogfight_steer, st_dogfight_steer },
        { AP_FN_st_weave_steer, st_weave_steer }, { AP_FN_st_cross_steer, st_cross_steer },
        { AP_FN_st_level_steer, st_level_steer }, { AP_FN_st_extend_steer, st_extend_steer },
        { AP_FN_st_dive_steer, st_dive_steer }, { AP_FN_st_recover_steer, st_recover_steer },
        { AP_FN_st_headon_steer, st_headon_steer }, { AP_FN_st_dive2_steer, st_dive2_steer },
        { AP_FN_st_level2_steer, st_level2_steer }, { AP_FN_st_regroup_steer, st_regroup_steer },
        { AP_FN_st_cover_steer, st_cover_steer }, { AP_FN_st_rejoin_steer, st_rejoin_steer },
        { AP_FN_st_bank_steer, st_bank_steer },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) codeptr_register(PROG_AP, t[i].a, (CodeFn)t[i].f);
}
