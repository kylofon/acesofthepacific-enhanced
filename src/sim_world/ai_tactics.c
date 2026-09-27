/* sim_world part B — tactic states (0619 pairs against fighters, 1027 energy tactics, 1133 hold).
 * sim_world_ai.md §8; checked against the disassembly. Corrections from the disassembly: alert_victim
 * resets its cooldown only when it raised the alert; st_b_steer measures the bank (+12h), not the pitch;
 * st_a_steer's cloud test uses an uninitialised stack buffer (PORT: t.pos). */
#include "sim_world/swb_priv.h"

/* ==== 0619 ================================================================================================ */

/* 0619:000c other_in_front(p) — the last other member of t's flight close in front of p */
u16 other_in_front(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 mk = stk_mark(), m = stk_alloc(0x20), o = stk_alloc(6);   /* (10 bytes in the original) */
    s16 n = (s16)flight_alive_members(FLT(t), m);
    s16 lim = 8000;
    u16 best = 0;
    for (s16 i = 0; i < n; i++) {
        u16 q = DSW(m + 2 * i);
        if (q == t) continue;
        s32 h = (s32)angles_to(o, (u16)(q + 4), (u16)(p + 4));
        vsub(o, (u16)(p + 0x10));
        if ((u16)(DSW(o + 4) + 0x1000) >= 0x2000) continue;
        if ((u16)(DSW(o) + 0x1000) >= 0x2000) continue;
        if ((s32)lim > h) best = q;                         /* (the limit is never tightened) */
    }
    stk_release(mk);
    return best;
}

/* 0619:00ce */
u16 target_vulnerable(u16 t)
{
    if (DESC(t) == D_DEFEND) return 1;
    if (DFLAGS(DESC(t)) & 0x10) return 0;
    return (u16)(OW(t, 0x8E) + 0x32) < 0x64;
}

/* 0619:00fc tactic_choose(p) */
u16 tactic_choose(u16 p)
{
    u16 P = PIL(p);
    s16 k = DSS(P + 0x28);
    u16 t = OW(p, 0xD7);
    if (!target_vulnerable(t)) return tactic_energy(p, t);
    if (DSW(P + 0x2A) != 0) {
        u16 mk = stk_mark(), o = stk_alloc(6);
        angles_to(o, (u16)(t + 4), (u16)(p + 4));
        vsub(o, (u16)(p + 0x10));
        if (OSL(p, 0x0C) - OSL(t, 0x0C) > 2500 && (u16)(DSW(o + 4) + 0x2000) < 0x4000 &&
            (u16)(DSW(o) + 0x3E80) < 0x3E80 && (u16)(OW(p, 0x14) - OW(t, 0x14) + 0x7000) >= 0xE000)
            k = 4;
        stk_release(mk);
    }
    if (k == 4) set_state(p, D_SPREAD);
    else DSW(set_state(p, D_BRACKET)) = (u16)k;
    return 0;
}

/* 0619:01dc (1306h) */
static u16 st_bracket_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 mk = stk_mark(), pt = stk_alloc(12);
    memcpy(DSP(pt), DSP(t + 4), 12);
    u16 h = target_heading(t);
    u16 r = 0;
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) { r = 1; goto out; }
    if (!target_vulnerable(t)) { tactic_energy(p, t); goto out; }
    point_ahead(pt, (u16)(h + 0x8000), 12000);
    DSSL(pt + 8) += DSS(0x12BA + 6 * DSS(s));
    steer_to_point(p, pt);
    THROTTLE(p) = CRUISE(p);
    if (dist2v(pt, (u16)(p + 4)) < 5000 && DSSL(pt + 8) - OSL(p, 0x0C) < 2500) {
        u16 k = DSW(s);
        DSW(set_state(p, D_PAIR_PURSUE)) = k;
    }
out:
    stk_release(mk);
    return r;
}

/* 0619:0304 (12DEh) — scratch {k, side, timer} */
static u16 st_pair_pursue_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = OW(p, 0xD7);
    u16 mk = stk_mark(), off = stk_alloc(6), out = stk_alloc(0x1C);
    u16 r = 0, wait = 0;
    memcpy(DSP(off), DSP(0x12B6 + 6 * DSS(s)), 6);
    u16 h = target_heading(t);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) { r = 1; goto out; }
    if (!target_vulnerable(t)) { tactic_energy(p, t); goto out; }
    s16 dh = (s16)(OW(p, 0x14) - h);
    if (DSW(s + 2) == 0) {
        s16 v = (s16)-OS(p, 0xD1);
        if ((u16)(v + 0x4000) >= 0x8000) v = dh;
        DSW(s + 2) = (u16)((v < 0) + 1);
        if ((u16)(OW(p, 0xD1) + 1000) >= 2000) DSW(s + 4) = 0;
    }
    if (timer16_dec((u16)(s + 4)) != 0) {
        DSW(off + 2) = (u16)(DSW(off + 2) - 4000);
        wait = 1;
    }
    if (DSW(s + 2) == 1) DSW(off) = (u16)-DSS(off);
    vrot_z(off, h);
    steer_intercept(p, t, off, 1, 1, out);
    THROTTLE(p) = CRUISE(p);
    if (!wait && DSSL(out + 2) < 1000) {
        u16 k = DSW(s);
        DSW(set_state(p, D_PAIR_ATTACK)) = k;
    }
out:
    stk_release(mk);
    return r;
}

/* 0619:0472 alert_victim(p, t) */
void alert_victim(u16 p, u16 t)
{
    if (OSL(p, 0xD3) < 7000 && OW(p, 0xF9) == 0) {
        alert_raise(t, 6, p);
        OW(p, 0xF9) = 2500;
    }
}

/* 0619:04a2 (131Ah) — scratch {k, phase} */
static u16 st_pair_attack_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 s = SCR(p);
    s16 k = DSS(s);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    OW(p, 0xC1) |= 8;
    steer_lead(p, t);
    alert_victim(p, t);
    THROTTLE(p) = CRUISE(p);
    if (DSW(s + 2) == 0) {
        if ((u16)(OW(p, 0xD1) + 2000) < 4000) DSW(s + 2) = 1;
        return 0;
    }
    if (OB(p, 0x44) & 2) { tactic_choose(p); return 0; }
    if ((u16)(OW(p, 0xD1) + 0x2000) < 0x4000 && (s32)DSS(0x12AC + 2 * k) <= OSL(p, 0xD3)) return 0;
    if (OW(p, 0xDD) != 0) return 1;
    u16 n = other_in_front(p);
    if (n) {
        OW(p, 0xD7) = n;
    } else if (k == 1 || k == 2) {
        u16 s2 = set_state(p, D_PAIR_BREAK);
        DSW(s2) = (u16)k;
        DSW(s2 + 2) = 3000;
    } else {
        tactic_choose(p);
    }
    return 0;
}

/* 0619:05a8 (132Eh) */
static u16 st_pair_break_steer(u16 p)
{
    u16 s = SCR(p);
    steer_roll_to(p, 0, 0);
    steer_pitch_to(p, 0x1000, 0);
    if (timer16_dec((u16)(s + 2)) == 0) {
        u16 k = DSW(s);
        DSW(set_state(p, D_PAIR_PURSUE)) = k;
    }
    return 0;
}

/* 0619:0602 (12F2h) */
static u16 st_spread_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 c = 0x12CE;
    u16 mk = stk_mark(), pt = stk_alloc(12);
    u16 r = 0;
    memcpy(DSP(pt), DSP(t + 4), 12);
    u16 h = target_heading(t);
    if (cloud_blocks_segment((u16)(p + 4), pt)) { r = 1; goto out; }
    if (!target_vulnerable(t)) { tactic_energy(p, t); goto out; }
    THROTTLE(p) = CRUISE(p);
    point_ahead(pt, h, (u32)(s32)DSS(c + 2));
    DSSL(pt + 8) += DSS(c + 4);
    steer_to_point(p, pt);
    if ((s32)DSS(c + 2) > OSL(p, 0xD3)) DSW(set_state(p, D_PAIR_ATTACK)) = 4;
out:
    stk_release(mk);
    return r;
}

/* 0619:06e4 */
void tactics_register(void) { class_register_n(5, 0x12D4); }

/* ==== 1027 ================================================================================================ */

/* 1027:0008 energy_advantage(p, t) (RNG) — cached for 56000 in +1F bit 10h */
u16 energy_advantage(u16 p, u16 t)
{
    if (!is_pure_fighter(t)) return 0;
    if (OB(p, 0x26) == 1 && OC(DAT(p), 0x54) == 0) return 1;
    if (OSL(p, 0xDF) != 0) return (OB(p, 0x1F) & 0x10) ? 1 : 0;
    s16 a = OC(DAT(p), 0x59), b = OC(DAT(t), 0x59);
    OSL(p, 0xDF) = 56000;
    u16 r = a >= b;
    if (!r && (s16)(RNG() & 0x3FF) < PW(p, 0x14)) r = 1;                          /* RNG */
    if (r == 1) OB(p, 0x1F) |= 0x10;
    else OB(p, 0x1F) &= 0xEF;
    return r;
}

/* 1027:010b */
void st_c_enter(u16 p) { DSW(set_state(p, D_TAC_C)) = 4; }

/* 1027:0126 tactic_energy(p, t) */
u16 tactic_energy(u16 p, u16 t)
{
    u16 aim = gun_solution(p);
    u16 done = 0;
    if (DSSL(aim + 8) > 12000) {
        if ((u16)(DSW(aim + 6) + 3000) < 6000) { st_a_enter(p, t); done = 1; }
    } else if ((u16)(DSW(aim + 6) + 1500) < 3000) {
        st_b_enter(p);
        done = 1;
    }
    if (!done) st_c_enter(p);
    return 0;
}

/* 1027:0188 fight_choose(p, t) */
u16 fight_choose(u16 p, u16 t)
{
    if (OW(p, 0xDD) != 0 && is_pure_fighter(t)) return 0;
    if (CLS(t) != 0) { tactic_choose(p); return 1; }
    if (!energy_advantage(p, t)) { tactic_energy(p, t); return 1; }
    return 0;
}

/* 1027:01fe */
void st_a_enter(u16 p, u16 t)
{
    u16 s = set_state(p, D_TAC_A);
    DSSL(s) = OSL(p, 0x0C) - OSL(t, 0x0C);
    DSW(s + 4) = 1;
}

/* 1027:022c point_ahead(pt, h, d) */
void point_ahead(u16 pt, u16 h, u32 d32)
{
    s32 d = (s32)d32;
    u16 mk = stk_mark(), v = stk_alloc(6);
    DSW(v) = 0;
    DSW(v + 2) = 32000;
    DSW(v + 4) = 0;
    vrot_z(v, h);
    while (d > 32000) {
        ladd_v(pt, v);
        d -= 32000;
    }
    lmove_heading(pt, h, (u16)d);
    stk_release(mk);
}

/* 1027:0286 elevation_to(a, b) */
u16 elevation_to(u16 a, u16 b) { return bearing32(DSSL(b + 8) - DSSL(a + 8), (s32)ldist2(a, b)); }

/* 1027:02b3 */
void st_b_enter(u16 p) { DSW(set_state(p, D_TAC_B)) = OW(p, 0x14); }

/* 1027:02d3 (1560h, lag above) — §8.1 */
static u16 st_a_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 s = SCR(p);
    u16 aim = (u16)(p + 0xCB);
    u16 mk = stk_mark(), pt = stk_alloc(12);
    u16 r = 0;
    /* PORT: the original passes an uninitialised stack buffer as the second point (its result depends
     * on stale stack contents); t.pos is used. */
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) { r = 1; goto out; }
    s16 a = (s16)(OW(t, 0x14) - (u16)(DSW(aim + 6) + OW(p, 0x14)) + 0x8000);
    s16 k = (s16)(abs16(a) >> 7);
    memcpy(DSP(pt), DSP(t + 4), 12);
    DSSL(pt + 8) += DSSL(s);
    s32 rr = (s32)lmul(DSSL(aim + 8), k) >> 8;
    point_ahead(pt, OW(t, 0x14), (u32)(rr + 1000));
    u32 rel = steer_toward(p, t, pt);
    THROTTLE(p) = DSW(PIL(p) + 0x10);
    memcpy(DSP(pt), DSP(t + 4), 12);
    s32 q = (s32)lmul(DSSL(aim + 8) >> 1, (s16)(0x100 - k)) >> 8;
    u16 e = bearing32(OSL(p, 0x0C) - OSL(t, 0x0C), DSSL(aim + 8) - q);
    if (DSSL(aim + 8) < 12000 ||
        ((u16)(e + 0x0C00) >= 0x1800 && (u16)((u16)(rel >> 16) + 0x1000) < 0x2000))
        st_b_enter(p);
out:
    stk_release(mk);
    return r;
}

/* 1027:046b (1574h, attack pass) */
static u16 st_b_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 s = SCR(p);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    OW(p, 0xC1) |= 8;
    THROTTLE(p) = DSW(PIL(p) + 0x10);
    steer_lead(p, t);
    alert_victim(p, t);
    s16 d = abs16((s16)((s16)(OW(p, 0x14) - DSW(s)) >> 1));
    s16 sum = (s16)(d + (abs16(OS(p, 0x12)) >> 1));
    if (!(sum > 0x3000 || (u16)(OW(p, 0xD1) + 0x4000) >= 0x8000 || OSL(p, 0xD3) < 1000)) return 0;
    u16 not_fighter = is_pure_fighter(t) == 0;
    if (OW(p, 0xDD) != 0) return 1;
    if (not_fighter && target_vulnerable(t)) {
        tactic_choose(p);
    } else if (!energy_advantage(p, t)) {
        u16 n = other_in_front(p);
        if (n) {
            OW(p, 0xD7) = n;
            DSW(s) = OW(p, 0x14);
        } else {
            st_c_enter(p);
        }
    } else {
        set_state(p, D_PURSUE);
        OSL(p, 0xDF) = 56000;
    }
    return 0;
}

/* 1027:05b5 (RNG) */
static s16 climb_mode_pick(u16 p)
{
    u16 dat = DAT(p);
    return (s16)(RNG() & 0x3FF) < PW(p, 0x20) ? (s16)OC(dat, 0x3C) : (s16)OC(dat, 0x3D);   /* RNG */
}

/* 1027:060b (RNG) */
static u16 try_manoeuvre6(u16 p, u16 t)
{
    if (OSL(p, 0x0C) - OSL(t, 0x0C) <= -1000) return 0;
    if ((s16)(RNG() & 0x3FF) >= PW(p, 0x42)) return 0;                             /* RNG */
    if (!manoeuvre_allowed(6, p)) return 0;
    set_state(p, D_TAC_E);
    return manoeuvre_start(6, p);
}

/* 1027:0680 (1588h, extend) — scratch {mode, armed, timer} */
static u16 st_c_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 s = SCR(p);
    s32 d = dist2(p, t);
    if (cloud_blocks_segment((u16)(p + 4), (u16)(t + 4))) return 1;
    u32 ag = alert_get(p);
    u16 src = (u16)ag;
    s16 lvl = (s16)(ag >> 16);
    s16 dv = (s16)(OS(p, 0x94) - OS(src, 0x94));
    u16 fast = lvl == 3 && dv < 120;
    steer_roll_to(p, 0, 0);
    THROTTLE(p) = DSW(PIL(p) + 0x10);
    u16 pitch = DSW(0x1552 + 2 * DSS(s));
    if (too_low(p, 4000) && OS(p, 0x6E) < 0) OW(p, 0xAE) = 0x100;
    else steer_pitch_to(p, pitch, 0);
    if (DSW(s + 2) == 0) {
        if (fast) {
            DSW(s) = (u16)climb_mode_pick(p);
            DSW(s + 2) = 1;
            DSW(s + 4) = 5000;
        }
    } else if (timer16_dec((u16)(s + 4)) == 0) {
        if (!fast) {
            DSW(s) = 4;
            DSW(s + 2) = 0;
        }
        DSW(s + 4) = 5000;
    }
    if (DSS(s) < 3 && too_low(p, 4000)) DSW(s) = 3;
    if (d > 15000) {
        alert_raise(t, 2, p);
        if (d > 20000 && !try_manoeuvre6(p, t)) {
            u16 s2 = set_state(p, D_TAC_D);
            DSW(s2) = 20000;
            DSSL(s2 + 2) = OSL(p, 0x0C) - OSL(t, 0x0C);
        }
    }
    return 0;
}

/* 1027:0822 (15B0h) */
static u16 st_e_steer(u16 p)
{
    if (manoeuvre_step(p)) st_a_enter(p, OW(p, 0xD7));
    return 0;
}

/* 1027:0845 (159Ch, zoom) — scratch {timer, i32 best dz, phase} */
static u16 st_d_steer(u16 p)
{
    u16 t = OW(p, 0xD7);
    u16 s = SCR(p);
    s32 dz = OSL(p, 0x0C) - OSL(t, 0x0C);
    u16 done = 0;
    THROTTLE(p) = DSW(PIL(p) + 0x10);
    s32 r = OSL(p, 0xD3);
    if (DSW(s + 6) != 0) {
        steer_heading_nolead(p, 0, OW(p, 0xD1));
        if (r < 30000) DSW(s + 6) = 0;
    } else {
        steer_heading_nolead(p, 0, (u16)(OW(p, 0xD1) + 0x8000));
        if (r > 50000) DSW(s + 6) = 1;
    }
    steer_pitch_to(p, DSW(DAT(p) + 0x57), 0);
    if (timer16_dec(s) == 0 && dz > -500) {
        if (DSSL(s + 2) > dz) done = 1;
        else DSSL(s + 2) = dz;
    }
    if (done || dz > 1200) st_a_enter(p, t);
    return 0;
}

/* 1027:0956 — the think of the 1027 states */
static u16 tactic_think(u16 p)
{
    ai_think_target(p);
    OSL(p, 0xDF) = 0;
    return 1;
}

/* 1027:0975 */
void tactic1027_register(void) { class_register_n(5, 0x15C4); }

/* ==== 1133 ================================================================================================ */

/* 1133:0001 */
void skip_hook(void) { DSB(0x57CF) = 1; }
/* 1133:000b / 0013 — debug identities */
static u16 identity_a(u16 x) { return x; }
static u16 identity_b(u16 x) { return x; }

/* 1133:001b */
static void park_far_away(u16 pos)
{
    DSSL(pos + 8) = 500000000;
    DSSL(pos + 4) = 1000000000;
    DSSL(pos) = 1000000000;
}

/* 1133:003a (15F6h act) — scratch {phase, timer, next} */
static u16 st_hold_act(u16 p)
{
    u16 s = SCR(p);
    identity_a(p);
    identity_b(p);
    if (DSW(s) != 0) {
        if (timer16_dec((u16)(s + 2)) == 0) {
            DSW(s) = 0;
            DSW(s + 2) = 2;
        }
    } else {
        u16 c = DSW(s + 2);
        DSW(s + 2) = (u16)(c - 1);
        if (c == 0) {
            park_far_away((u16)(p + 4));
            set_state(p, DSW(s + 4));
        }
    }
    st_act_fly(p);
    return 0;
}

/* 1133:00a8 */
void st_hold_enter(u16 p, u16 next)
{
    u16 s = set_state(p, D_HOLD);
    DSW(s + 4) = next;
    DSW(s) = 1;
    DSW(s + 2) = 0x2300;
}

/* 1133:00ce — the steer of 160Ah */
static u16 is_nonzero(u16 x) { return x != 0; }

/* 1133:00e0 */
void hold_register(void) { class_register_n(2, 0x161E); }

__attribute__((constructor)) static void ai_tactics_codeptrs(void)
{
    static const struct { u32 a; void *f; } t[] = {
        { AP_FN_st_bracket_steer, st_bracket_steer }, { AP_FN_st_pair_pursue_steer, st_pair_pursue_steer },
        { AP_FN_st_pair_attack_steer, st_pair_attack_steer }, { AP_FN_st_pair_break_steer, st_pair_break_steer },
        { AP_FN_st_spread_steer, st_spread_steer }, { AP_FN_st_a_steer, st_a_steer },
        { AP_FN_st_b_steer, st_b_steer }, { AP_FN_st_c_steer, st_c_steer }, { AP_FN_st_e_steer, st_e_steer },
        { AP_FN_st_d_steer, st_d_steer }, { AP_FN_tactic_think, tactic_think },
        { AP_FN_st_hold_act, st_hold_act }, { AP_FN_is_nonzero, is_nonzero },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) codeptr_register(PROG_AP, t[i].a, (CodeFn)t[i].f);
}
