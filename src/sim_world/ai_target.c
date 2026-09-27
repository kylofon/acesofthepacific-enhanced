/* sim_world part B — target choice and the common think (1142). sim_world_ai.md §5; every function
 * checked against the disassembly (target_class 1142:05ce re-read there: the role-2/bomber branches). */
#include "sim_world/swb_priv.h"

/* 1142:0000 */
u16 plane_is_down(u16 p) { return (OB(p, 0x1A) & 1) || (OB(p, 0x1A) & 0x40); }

/* 1142:001d (near) — lexicographic compare of three ints */
u16 cmp3_gt(u16 a, u16 b)
{
    for (int i = 0; i < 3; i++) {
        s16 d = (s16)(DSS(a + 2 * i) - DSS(b + 2 * i));
        if (d > 0) return 1;
        if (d < 0) return 0;
    }
    return 0;
}

/* 1142:005d (near) */
void offer_keep(u16 rec)
{
    if (cmp3_gt(rec, 0x5814)) memcpy(DSP(0x5814), DSP(rec), 12);
}

/* 1142:008e (near) */
static u16 band_factor(s16 d, u16 r, u16 num, u16 den)
{
    if (DSS(r + 2) < d) {
        if (DSS(r + 4) < d) return 0;
        DSS(num) = (s16)(d - DSS(r + 2));
        DSS(den) = (s16)(DSS(r + 4) - DSS(r + 2));
        return 1;
    }
    if (DSS(r) > d) return 0;
    DSS(num) = (s16)(DSS(r + 2) - d);
    DSS(den) = (s16)(DSS(r + 2) - DSS(r));
    return 1;
}

/* 1142:00eb */
static s16 aim_factor(u16 a, u16 b, u16 ang)
{
    u16 m = stk_mark(), o = stk_alloc(6);
    angles_to(o, a, b);
    vsub(o, ang);
    s32 s = (s32)abs16(DSS(o)) + abs16(DSS(o + 4));
    s16 f = (s16)((0xFFFF - s) >> 8);
    stk_release(m);
    return f <= 1 ? 1 : f;
}

/* 1142:0164 — §5.2 */
u16 threat_score(u16 a, u16 b, u16 out_f)
{
    u16 m = stk_mark(), t = stk_alloc(6), num = stk_alloc(2), den = stk_alloc(2);
    s16 r = 0;
    s32 d = dist3v(a, b);
    if (band_factor((s16)d, DSW(0x1644), num, den)) {       /* quirk: the low word of the distance */
        memcpy(DSP(t), DSP(a + 0x0C), 6);
        vadd(t, 0x1622);
        s16 f1 = aim_factor(b, a, t);
        if (out_f) DSS(out_f) = f1;
        f1 = (s16)(((s32)f1 * f1) >> 8);
        memcpy(DSP(t), DSP(0x1628), 6);
        vsub(t, (u16)(a + 0x0C));
        s16 f2 = aim_factor(a, b, t);
        s16 q = (s16)(DSS(den) - DSS(num));
        s16 rr = (s16)(ldiv_sat((s32)q << 8, DSS(den)) & 0xFFE0);
        s16 h = (s16)(((s32)f1 * f2) >> 1);
        r = (s16)(((s32)h * rr) >> 9);
    }
    stk_release(m);
    return (u16)r;
}

/* 1142:027c (near) */
void offer(u16 prio, u16 desc, u16 target, u16 score)
{
    u16 m = stk_mark(), rec = stk_alloc(12);
    memset(DSP(rec), 0, 12);
    DSW(rec) = prio;
    DSW(rec + 2) = score;
    DSW(rec + 6) = target;
    DSW(rec + 8) = desc;
    offer_keep(rec);
    stk_release(m);
}

/* 1142:02b8 — x, y, z, pitch, roll, heading, speed */
void plane_rec(u16 out, u16 p)
{
    memcpy(DSP(out), DSP(p + 4), 12);
    DSW(out + 0x0C) = OW(p, 0x10);
    DSW(out + 0x0E) = OW(p, 0x12);
    DSW(out + 0x10) = OW(p, 0x14);
    DSW(out + 0x12) = OW(p, 0x6C);
}

/* 1142:02f5 */
void offer_climb(u16 p, u16 tgt)
{
    s32 z = OSL(p, 0x0C);
    if (z < 0) z = 0;
    z = 0xFFF - z;
    if (z < 0) z = 0;
    offer((u16)(s16)(z >> 9), D_CLIMB, tgt, 0);
}

/* 1142:0350 — §5.3 */
u16 pursuit_variant(u16 p)
{
    u16 t = OW(p, 0xD7);
    if (DESC(t) == D_COVER) t = OW(t, 0xDD);
    u16 b = bearing_between((u16)(t + 4), (u16)(p + 4));
    u16 rel_p = (u16)(b - OW(p, 0x14));
    u16 rel_t8 = (u16)(b - OW(t, 0x14) + 0x8000);
    u16 k = D_PURSUE;
    u16 Pp = PIL(p), Pt = PIL(t);
    u16 tt = OW(t, 0xD7);
    if (DESC(tt) == D_COVER) tt = OW(tt, 0xDD);
    if (t == PLAYER || (DESC(t) == D_PURSUE && tt == p)) {
        s16 et = (s16)(((s32)(s16)(0x7F - DSS(Pt + 0x44)) * (s16)OC(DAT(t), 0x59)) >> 8);
        s16 ep = (s16)(((s32)(s16)(0x7F - DSS(Pp + 0x44)) * (s16)OC(DAT(p), 0x59)) >> 8);
        if (et >= ep && (u16)(rel_p + 0x1F40) >= 0x3E80 && (u16)(rel_t8 + 0x5DC0) < 0xBB80 &&
            ((u16)(rel_t8 + 0x1F40) >= 0x3E80 || (u16)(rel_p + 0x5DC0) < 0xBB80)) {
            s16 r = (s16)(RNG() & 0x3FF);                                     /* RNG */
            if (DSS(Pp + 0x1A) > r) k = OS(p, 0x12) < 0 ? D_HEADON_A : D_HEADON_B;
        }
    }
    return k;
}

/* 1142:04cc */
u16 attackers_of(u16 t, u16 except)
{
    u16 n = 0;
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
        u16 q = DSW(DSW(0x475B) + 2 * i);
        if (q != except && OW(q, 0xD7) == t) n++;
    }
    return n;
}

/* 1142:0500 — obj in p's flight's protected list */
u16 flight_protects_obj(u16 obj, u16 p)
{
    u16 f = FLT(p);
    s16 n = (s16)OC(f, 0x0E);
    for (s16 i = 0; i < n; i++)
        if (OW(f, 0x10 + 2 * i) == obj) return 1;
    return 0;
}

/* 1142:0536 */
u16 covers(u16 t, u16 p)
{
    u16 o = OB(FLT(p), 0x43) == 0 ? FLT(t) : t;
    return flight_protects_obj(o, p);
}

/* 1142:0564 */
u16 flight_all_done(u16 f)
{
    for (s16 i = 0; i < OS(f, 6); i++) {
        u16 m = DSW(OW(f, 8) + 2 * i);
        if (OB(f, 0) == 0) {
            if (!plane_is_down(m) && !(OB(m, 0x1C) & 0x80)) return 0;
        } else {
            if (!(OB(m, 0x1B) & 2) && !(OB(m, 0x1D) & 1)) return 0;
        }
    }
    return 1;
}

/* 1142:05ce ap_target_class(p, t, is_place) → 0..4 — §5.4, verified in the disassembly */
u16 ap_target_class(u16 p, u16 t, u16 is_place)
{
    s16 role = (s16)OC(FLT(p), 2);
    u16 r = 0;
    u16 bomber = role == 3;
    s16 k = (s16)OC(p, 0xCA);
    u16 ga = has_ordnance(p);
    u16 player_leads = flight_leader(FLT(p)) == PLAYER;
    if (OSL(p, 0xE3) != 0 && k != 0) {
        if (!is_place && OB(t, 0x18) == 0) {
            r = (k < 5 && k - 1 == CLS(t)) ? 4 : 1;
            return r;
        }
        if (OW(p, 0xBD) == t) r = 4;
        return r;
    }
    if (is_place) {
        if (!bomber && role != 2) return 0;
        if (!flight_protects_obj(t, p)) return 0;
        if (player_leads) { help_msg(p); return 0; }
        return bomber ? (ga ? 4 : 2) : 4;
    }
    u16 air = OB(t, 0x18) == 0;
    if (role == 2 || (bomber && !is_pure_fighter(p))) {
        if (air) return 0;
        if (!covers(t, p)) return 0;
        if (player_leads) { help_msg(p); return 0; }
        return bomber ? (ga ? 4 : 2) : 4;
    }
    if (role == 0 || role == 1 || bomber) return air ? 2 : 0;
    if (role == 4) {
        if (!air) return 0;
        return covers(t, p) ? 4 : 2;
    }
    if (role == 5) {
        if (!air) return 0;
        u16 f = FLT(p), c;
        if (OB(f, 0x43) == 2) c = covers(OW(f, 0x10), t);
        else if (OB(FLT(t), 0x43) == 2) c = covers(OW(FLT(t), 0x10), p);
        else c = flight_protects_obj(OW(f, 0x10), t);
        return c ? 4 : 2;
    }
    if (role == 6) {
        if (!air) return 0;
        u16 g = OW(FLT(p), 0x10);
        if (flight_all_done(g)) return 2;
        if (CLS(t) != 0) return 0;
        s16 j = (s16)first_alive(g, 0);
        u16 L = DSW((u16)(OW(g, 8) + 2 * j));                 /* quirk: j = −1 reads before the list */
        return dist2(L, t) < 30000 ? 4 : 2;
    }
    return 0;
}

/* 1142:08c2 */
void apply_class(u16 score, u16 p, u16 t, u16 is_place)
{
    u16 c = ap_target_class(p, t, is_place);
    DSS(score) = (s16)(((s32)DSS(score) * DSS(0x1634 + 2 * c)) >> 10);
}

/* 1142:08fb (near) offer_air_target(p, t) — returns "keep looking" (§5.5) */
u16 offer_air_target(u16 p, u16 t)
{
    u16 m = stk_mark(), a = stk_alloc(0x14), b = stk_alloc(0x14), f = stk_alloc(2), s = stk_alloc(2),
        lim = stk_alloc(2);
    u16 w = OW(p, 0xDD);
    u16 stop = 0;
    s32 d = dist3(p, t);
    DSW(lim) = 0;                                           /* PORT: uninitialised when w == 0 (unused then) */
    plane_rec(a, p);
    plane_rec(b, t);
    DSS(s) = (s16)threat_score(a, b, f);
    if (DSS(s) != 0) {
        u16 desc = D_PURSUE, prio = 6;
        apply_class(s, p, t, 0);
        if (DSS(s) == 0) stop = 1;
        if (OSL(p, 0xE3) != 0) {
            DSS(lim) = 0;
        } else {
            if ((has_ordnance(t) || CLS(t) != 0) && w && target_vulnerable(t)) {
                desc = D_TAC_B;
                if (!(OW(w, 0xD7) == t && DESC(w) == D_PAIR_ATTACK)) DSS(s) = 0;
            }
            if (w) {
                DSS(lim) = PW(p, 0x16);
                if (w == PLAYER) {
                    if (OSL(p, 0xE7) != 0) DSS(lim) = (s16)(DSS(lim) + ((OB(p, 0x1B) & 0x10) ? 15000 : -5000));
                    if (DESC(p) == D_COVER && DSS(f) > 200 && d < 15000) {
                        DSS(lim) = (s16)(DSS(lim) - 9000);
                        desc = D_TAC_B;
                    }
                } else if (DESC(w) == D_TAC_B || DESC(w) == D_PAIR_ATTACK) {
                    DSS(lim) = (s16)(DSS(lim) - 9000);
                    desc = D_TAC_B;
                }
            }
        }
        if (t == PLAYER) {
            s32 v = ((s32)DSS(s) * DSS(0x163E + 2 * (s16)DSC(0x42D2))) >> 10;
            DSS(s) = v > 32000 ? 32000 : (s16)v;
        }
        if (DSS(s) != 0 && (w == 0 || DSS(s) > DSS(lim))) {
            DSS(s) = ldiv_sat(DSS(s), (s16)(attackers_of(t, p) + 1));
            offer(prio, desc, t, DSW(s));
        }
    } else if (w == 0 && d < 60000) {
        DSS(s) = (s16)((60000 - d) >> 4);
        s32 dz = OSL(t, 0x0C) - OSL(p, 0x0C);
        apply_class(s, p, t, 0);
        if (DSS(s) == 0) stop = 1;
        else if (dz < -5000) offer(0, D_DIVE, t, DSW(s));
        else offer(2, D_PURSUE, t, DSW(s));
    }
    stk_release(m);
    return stop == 0;
}

/* 1142:0b73 */
u16 within_50000(u16 t, u16 p) { return (u32)dist2(p, t) < 50000; }

/* 1142:0ba6 (near) choose_target(p, bomber_type, ga) — §5.6; the ship pick is the member with the
 * lowest priority (read in the disassembly), a task/cover ship or a negative priority ends the scan */
u16 choose_target(u16 p, u16 bomber_type, u16 ga)
{
    u16 m = stk_mark(), sc = stk_alloc(2), list = stk_alloc(0x66), nf = stk_alloc(2);
    u16 best = 0;
    u16 addon = addon1_flag() && rockets_left(p);
    if (ga || can_strafe(p) || addon) {
        for (s16 i = 0; i < (s16)DSW(0x4765); i++) {
            u16 g = DSW(DSW(0x4751) + 2 * i);
            if (OB(g, 0x42) == OB(p, 0x26)) continue;
            u16 near = 0, pick = 0;
            s16 bestv = 100;
            for (s16 j = 0; j < OS(g, 6); j++) {
                u16 s = DSW(OW(g, 8) + 2 * j);
                if (OB(s, 0x1B) & 2) continue;
                if (within_50000(s, p)) near = 1;
                if (!ga && OB(SDAT(s), 0x6D) == 0 && !addon) continue;
                if (OSL(p, 0xE3) != 0 && OB(p, 0xCA) == 5 && OW(p, 0xBD) == s) { pick = s; break; }
                u16 f = FLT(p);
                if (OB(f, 0x43) == 2 && OW(f, 0x10) == s) { pick = s; break; }
                s16 v = (s16)ship_priority(s);
                if (v < bestv) {
                    bestv = v;
                    pick = s;
                    if (v < 0) break;
                }
            }
            if (near && pick) {
                DSS(sc) = 10000;
                apply_class(sc, p, pick, 0);
                if (DSS(sc) != 0) {
                    offer(6, D_SHIP_ATTACK, pick, DSW(sc));
                    best = 1;
                }
            }
        }
        u16 pl = DSW(0x4755);
        for (s16 i = 0; i < (s16)DSW(0x476E); i++, pl = (u16)(pl + 0x20)) {
            if (OB(pl, 0x1C) == 0x0D) continue;
            if ((s16)plane_c0((u16)(s16)OC(pl, 0x1D)) == (s16)OC(p, 0x26)) continue;
            if ((u32)dist2(p, pl) >= 80000) continue;
            DSS(sc) = 10000;
            apply_class(sc, p, pl, 1);
            if (DSS(sc) != 0) {
                offer(6, D_PLACE_ATTACK, pl, DSW(sc));
                best = 1;
            }
        }
    }
    if (!bomber_type && !ga) {
        s16 n = (s16)spotted_split(list, p, nf);
        for (s16 i = 0; i < n; i++) {
            u16 t = DSW(list + 2 * i);
            if (!plane_is_down(t)) best = offer_air_target(p, t);    /* (sic: the last call decides) */
        }
        if (best) offer_climb(p, 0);
    }
    stk_release(m);
    return best;
}

/* 1142:0e4e */
void help_msg_reset(void) { DSW(0x57EA) = 0; }

/* 1142:0e59 — once, for a plane of the player's flight */
void help_msg(u16 p)
{
    if (DSW(0x57EA) != 0 || FLT(p) != FLT(PLAYER)) return;
    DSW(0x57EA) = 1;
    u16 m = stk_mark(), rec = radio_rec(0x1A);
    DSW(rec + 0x16) = p;
    radio_queue7(plane_in_formation(PLAYER) ? 0x57 : 0x54, rec, 0);
    stk_release(m);
}

/* 1142:0eab (near) — §5.7 */
void engage_best(u16 p)
{
    u16 lead_msg = 1;
    if (DSW(0x581C) == D_SHIP_ATTACK) {
        ship_attack_setup(p, DSW(0x581A));
        OW(p, 0xD7) = 0;
    } else if (DSW(0x581C) == D_PLACE_ATTACK) {
        place_attack_setup(p, DSW(0x581A));
        OW(p, 0xD7) = 0;
    } else {
        lead_msg = 0;
        OW(p, 0xD7) = DSW(0x581A);
        if (DSW(0x581A)) gun_solution(p);
        if (DSW(0x581C) == D_TAC_B) {
            st_b_enter(p);
        } else {
            if (DSW(0x581C) == D_PURSUE && DESC(p) == D_PURSUE) DSW(0x581C) = pursuit_variant(p);
            set_state(p, DSW(0x581C));
        }
    }
    if (lead_msg && plane_in_formation(p)) help_msg(p);
}

/* 1142:0f5f */
u16 flight_alive_members(u16 f, u16 out)
{
    u16 n = 0;
    for (s16 i = 0; i < OS(f, 6); i++) {
        u16 m = DSW(OW(f, 8) + 2 * i);
        if (!plane_is_down(m)) { DSW(out) = m; out = (u16)(out + 2); n++; }
    }
    return n;
}

/* 1142:0fb2 */
u16 bomber_cover_think(u16 p)
{
    u16 o = OW(FLT(p), 0x10);
    OB(p, 0x1C) &= 0xFE;
    if ((u32)dist2(p, o) < 80000 && bombers_assign(FLT(p), o)) return 1;
    return 0;
}

/* 1142:100d — §5.8, the think of most combat states */
u16 ai_think_target(u16 p)
{
    u16 ga = has_ordnance(p);
    s16 cls = CLS(p);
    u16 bomber_type = cls != 0;
    u16 w = OW(p, 0xDD);
    u16 ret = 0;
    u16 L = flight_leader(FLT(p));
    u16 player_leads = L == PLAYER;
    if (DSB(0x477C) == 5) { OB(p, 0x1C) &= 0xFE; return 0; }
    if (DFLAGS(DESC(p)) & 0x400) return 0;
    if (ga && cls == 3 && OB(FLT(p), 0x43) == 1) return bomber_cover_think(p);
    memset(DSP(0x5814), 0, 12);
    if (bomber_type || ga) DSW(0x581C) = w ? D_JOIN : D_WAYPOINTS;
    else DSW(0x581C) = w ? D_COVER : D_CLIMB;
    u16 hold = player_leads ? player_orders_active()
                            : (is_pure_fighter(p) && (OB(FLT(p), 0x3B) & 4));
    if (hold) {
        if (!player_leads && FLT(PLAYER) == FLT(p) && (DFLAGS(DESC(p)) & 0x10)) {
            u16 m = stk_mark(), rec = radio_rec(0x18);
            DSW(rec + 0x16) = L;
            radio_queue7(0x52, rec, 0);
            stk_release(m);
        }
        OB(p, 0x1C) &= 0xFE;
        ai_leave_combat(p);
        ret = 1;
    } else if (choose_target(p, bomber_type, ga)) {
        OB(p, 0x1C) |= 1;
        if (p != PLAYER) {
            engage_best(p);
            ret = 1;
        }
    } else {
        OB(p, 0x1C) &= 0xFE;
    }
    return ret;
}

/* 1142:11aa — engine factor below 79 % */
u16 low_power(u16 p) { return OS(p, 0xA4) < 0x32A; }

/* 1142:11c3 — §5.9 */
void flight_targets_update(u16 f)
{
    u16 mk = stk_mark(), m = stk_alloc(0x20);            /* (10 bytes in the original) */
    s16 n = (s16)flight_alive_members(f, m);
    u16 fighting = 0;
    u16 m0 = DSW(m);
    for (s16 i = 0; i < n; i++) {
        u16 q = DSW(m + 2 * i);
        u16 fl = DFLAGS(DESC(q));
        if (OB(q, 0x1C) & 1) fighting = 1;
        else if (q != PLAYER && (fl & 0x10) && !(fl & 0x600)) {
            if (OB(f, 0x3A) & 0x80) ai_leave_combat(q);
            else set_state(q, D_REGROUP);
        }
    }
    if (fighting) OB(f, 0x3B) |= 1;
    else OB(f, 0x3B) &= 0xFE;
    if (!fighting && low_power(m0)) {
        for (s16 i = 1; i < n; i++) {
            u16 q = DSW(m + 2 * i);
            if (low_power(q)) continue;
            u16 s0 = 0, s1 = 0, l = OW(f, 8);
            for (s16 j = 0; j < OS(f, 6); j++, l = (u16)(l + 2)) {
                if (DSW(l) == m0) s0 = l;
                else if (DSW(l) == q) s1 = l;
            }
            DSW(s0) = q;
            DSW(s1) = m0;
            st_think_combat(q);
            st_think_combat(m0);
            break;
        }
    }
    stk_release(mk);
}

/* 1142:12f5 — someone spotted p's enemy */
void alert_flight(u16 p)
{
    u16 f = FLT(p);
    if (p == PLAYER) return;
    if (DFLAGS(DESC(p)) & 0x410) return;
    if (!(OB(f, 0x3A) & 0x10)) {
        elements_rebuild(f, 1);
        OB(f, 0x3A) |= 0x10;
    }
    if (CLS(p) == 3) return;
    ai_think_target(p);
    OSL(p, 0xDF) = 0;
}

/* 1142:135b — §5.10 */
void ai_forget_plane(u16 p)
{
    u16 f = FLT(p);
    u16 mk = stk_mark(), l = stk_alloc(0x20);            /* (10 bytes in the original) */
    u16 first = 0, n = 0;
    elements_rebuild(f, 0);
    for (s16 i = 0; i < OS(f, 6); i++) {
        u16 q = DSW(OW(f, 8) + 2 * i);
        if (q == p || plane_is_down(q)) continue;
        DSW(l + 2 * n) = q;
        n++;
        if (OW(q, 0xDD) == p) {
            OW(q, 0xDD) = first;
            if (first == 0) {
                first = q;
                if (q != PLAYER) st_think_combat(q);
            }
        }
    }
    if (n == 2 && OW(DSW(l), 0xDD) == 0 && OW(DSW(l + 2), 0xDD) == 0) {
        u16 b = DSW(l + 2);
        OW(b, 0xDD) = DSW(l);
        if (b != PLAYER) st_think_combat(b);
    }
    stk_release(mk);
}

/* 1142:1421 — the CSV name; the function is empty */
void targeting_nop(void) {}

__attribute__((constructor)) static void ai_target_codeptrs(void)
{
    codeptr_register(PROG_AP, AP_FN_ai_think_target, (CodeFn)ai_think_target);   /* the think of most states */
}
