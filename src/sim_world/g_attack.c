/* sim_world part B — g_attack.c (0deb): ground and ship attack, ordnance release, target lists.
 * sim_world_ai.md §11; ported from the disassembly. Quirks kept: role-2 low attacks (14E1h) never release;
 * the bomber run (14F5h) flies at its own altitude (pt.z = p.z); torpedo phase 2 falls through into phase 3;
 * top_targets fills a short list cyclically but returns the number found. Attack scratch (20 bytes): +0 phase,
 * +2 ship (1) / place (0), +4 target, +6 torpedo side, +8 timer / pass count, +0A bits, +0C far sub-target,
 * +10/+12 aim jitter. */
#include "sim_world/swb_priv.h"

#define G_ATTACK_C  far_make(SEG(0x39FD), 0)     /* "g_attack.c" (assert / mem_new file name) */

static inline FarPtr u32_far(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static inline u32 far_u32(FarPtr p) { return (u32)p.seg << 16 | p.off; }

/* the aim point of an attack: the sub-target's position (far) or the target's own */
static void attack_point(u16 s, u16 pt)
{
    if (DSL(s + 0x0C) != 0) memcpy(DSP(pt), far_mp(far_add(ds_far((u16)(s + 0x0C)), 4)), 12);
    else memcpy(DSP(pt), DSP(DSW(s + 4) + 4), 12);
}

/* 0deb:000a release_ordnance(p, aim) — bombs (stick of the three stations) or the torpedo */
void release_ordnance(u16 p, u16 aim)
{
    u16 mk = stk_mark(), v = stk_alloc(6), up = stk_alloc(6), stick = stk_alloc(0x14), src = stk_alloc(0x16);
    u8 ok;
    u16 torp;
    if (p == PLAYER) {
        memcpy(DSP(up), DSP(0x1496), 6);
        vec_rotate(up, obj_matrix(OW(p, 0x20)), v);
        if (DSS(v + 4) < 0) {                              /* inverted */
            msg_show(str(1, 0x38), 1, far_make(0, 0));
            stk_release(mk);
            return;
        }
        if (DSW(0x19F2) != 0) sx_play(0, (u16)(PLAYER + 4), (u16)(PLAYER + 0x70), 1);
    }
    if (torpedo_loaded(p)) {
        u16 ts = stk_alloc(8);
        DSW(ts) = p;
        DSW(ts + 2) = OB(p, 0x26) == 0 ? 1 : 0;
        DSW(ts + 4) = p == PLAYER ? DSW(0x1498) : 0;       /* PORT: uninitialised for the AI (unused) */
        DSW(ts + 6) = OW(p, 0x6C);
        ok = (u8)torp_drop((u16)(p + 4), (u16)-25, (u16)(p + 0x10), ts);
        torp = 1;
    } else {
        u16 d = (u16)(stick + 1), st = (u16)(p + 0xF3);
        DSB(stick) = 0;
        for (int i = 0; i < 3; i++, st = (u16)(st + 2)) {
            DSB(stick) = (u8)(DSB(stick) + DSB(st + 1));
            for (s16 k = 0; (s16)DSC(st + 1) > k; k++) DSB(d++) = DSB(st);
        }
        DSW(src) = p;
        if (aim) {
            memcpy(DSP(src + 2), DSP(aim), 12);
            DSL(src + 0x0A) = 0;                           /* (the aim's z is cleared) */
        } else {
            memset(DSP(src + 2), 0, 12);
        }
        DSW(src + 0x0E) = OW(p, 0x6C);
        memcpy(DSP(src + 0x10), DSP(DAT(p) + 0x6D), 6);
        DSW(src + 0x10) = (u16)(s16)(((s32)DSS(src + 0x10) * 0x55) >> 8);
        DSW(src + 0x14) = (u16)(DSW(src + 0x14) - 25);
        ok = (u8)bomb_release(stick, (u16)(p + 4), OW(p, 0x20), src);
        torp = 0;
    }
    if (p != PLAYER || DSB(0x42CD) != 0) OB(p, 0x1B) &= 0xBF;
    if (p == PLAYER && ok) msg_show(str(1, (s16)(0x12 + torp)), 1, far_make(0, 0));
    OW(p, 0xB6) = (u16)(0x400 + ((OB(p, 0x1A) & 4) ? DSW(DAT(p) + 0x7F) : 0));
    stk_release(mk);
}

/* 0deb:0221 rocket_release(p) */
void rocket_release(u16 p)
{
    u16 mk = stk_mark(), src = stk_alloc(0x0C);
    u32 rack = OL(p, 0xEB);
    DSW(src) = p;
    DSW(src + 2) = 0x0F;
    DSW(src + 4) = OW(p, 0x6C);
    memcpy(DSP(src + 6), DSP(DAT(p) + 0x6D), 6);
    rocket_fire((u16)(p + 0xEB), (u16)(p + 4), OW(p, 0x20), src);
    if (p == PLAYER && DSC(0x42CD) == 0) OL(p, 0xEB) = rack;        /* unlimited ammunition */
    stk_release(mk);
}

/* 0deb:02ab target_destroyed(s) */
u16 target_destroyed(u16 s)
{
    u16 t = DSW(s + 4);
    if (DSW(s + 2) != 0) return (OB(t, 0x1B) & 2) ? 1 : 0;
    return (OB(t, 0x1E) & 4) ? 1 : 0;
}

/* 0deb:02d9 attackers_on(place, sub) — planes in a ground-attack state working on that sub-target */
static u16 attackers_on(u16 place, u32 sub)
{
    u16 n = 0;
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
        u16 q = DSW(DSW(0x475B) + 2 * i);
        if (!(DFLAGS(DESC(q)) & 0x800)) continue;
        if (plane_is_down(q)) continue;
        u16 s = SCR(q);
        if (DSW(s + 4) == place && DSL(s + 0x0C) == sub) n++;
    }
    return n;
}

/* 0deb:033d pick_subtarget(s, hard) — the entry of the four best with the fewest attackers */
static u16 pick_subtarget(u16 s, s16 hard)
{
    u16 n = 0;                                             /* PORT: uninitialised when nothing is done */
    if (DSW(s + 2) != 0 || DSL(s + 0x0C) != 0) return n;
    u16 place = DSW(s + 4);
    u16 mk = stk_mark(), l = stk_alloc(0x10);
    n = top_targets(place, 4, l, hard);
    if (n == 0) {
        OB(place, 0x1E) |= 4;
    } else {
        u32 best = 0;
        s16 min = 100;
        for (s16 i = 0; i < 4; i++) {
            s16 c = (s16)attackers_on(place, DSL(l + 4 * i));
            if (c < min) {
                min = c;
                best = DSL(l + 4 * i);
            }
        }
        DSL(s + 0x0C) = best;
    }
    stk_release(mk);
    return n;
}

/* 0deb:03d0 approach(p, pt, r, h, alt) — circle until far enough and at altitude, then 1 */
static u16 ap_approach_0deb(u16 p, u16 pt, s32 r, u16 h, s32 alt)
{
    u16 b = (u16)(bearing_between(pt, (u16)(p + 4)) - OW(p, 0x14));
    s32 dz = DSSL(pt + 8) + alt - OSL(p, 0x0C);
    u16 near = (s32)ldist2((u16)(p + 4), pt) < r;
    OB(p, 0x45) |= 0x20;
    OB(p, 0x49) |= 0x20;
    if (!near && (u16)(2 * h) > (u16)((u16)dz + h)) return 1;
    s16 e = (s16)(b + 0x8000);
    if (!near) e = (s16)(e + (e < 0 ? 0x4000 : 0xC000));
    steer_heading_nolead(p, 0, (u16)e);
    steer_altitude(p, (u32)-dz, 1);
    THROTTLE(p) = CRUISE(p);
    return 0;
}

/* 0deb:049a fly_heading_alt(p, b, dz) */
static void fly_heading_alt(u16 p, u16 b, s32 dz)
{
    steer_heading_nolead(p, 0, b);
    steer_altitude(p, (u32)-dz, 0);
    THROTTLE(p) = (u16)(CRUISE(p) - 20);
}

/* 0deb:04d6 aim_jitter(p, s, pt) (RNG ×2 the first time) */
static void aim_jitter(u16 p, u16 s, u16 pt)
{
    if (!(DSW(s + 0x0A) & 2)) {
        s16 r = PW(p, 0x0A);
        DSW(s + 0x0A) |= 2;
        DSW(s + 0x10) = (u16)(RNG_RANGE((s16)(r << 1)) - r);                          /* RNG */
        DSW(s + 0x12) = (u16)(RNG_RANGE((s16)(r << 1)) - r);                          /* RNG */
    }
    DSSL(pt) += DSS(s + 0x10);
    DSSL(pt + 4) += DSS(s + 0x12);
}

/* 0deb:0548 (14F5h) level bombers on a place: flies at its own altitude over the sub-target */
static u16 st_bomber_steer(u16 p)
{
    u16 s = SCR(p);
    u16 mk = stk_mark(), pt = stk_alloc(12);
    u16 r = 0;
    memcpy(DSP(pt), far_mp(far_add(ds_far((u16)(s + 0x0C)), 4)), 12);
    if (target_destroyed(s)) { r = 1; goto out; }
    aim_jitter(p, s, pt);
    s32 d = (s32)ldist2((u16)(p + 4), pt);
    if (DSW(s) == 0) {
        DSSL(pt + 8) = OSL(p, 0x0C);
        steer_to_point(p, pt);
        if (d < 10000) {
            release_ordnance(p, pt);
            DSW(s)++;
            DSW(s + 8) = 7000;
        }
    } else {
        steer_level(p);
        if (timer16_dec((u16)(s + 8)) == 0) ai_think_target(p);
    }
out:
    stk_release(mk);
    return r;
}

/* 0deb:060d (14A5h) dive bombing — §11.1 */
static u16 st_divebomb_steer(u16 p)
{
    u16 s = SCR(p);
    u16 mk = stk_mark(), pt = stk_alloc(12), a = stk_alloc(12);
    u16 r = 0;
    attack_point(s, pt);
    u16 k = DSW(s + 2) == 0;
    s16 alt = DSS(0x148E + 2 * k);
    if (target_destroyed(s)) { r = 1; goto out; }
    aim_jitter(p, s, pt);
    switch (DSW(s)) {
    case 0:
        if (ap_approach_0deb(p, pt, 40000, 1000, alt)) {
            if (k && DSL(s + 0x0C) == 0) pick_subtarget(s, -1);
            else DSW(s) = (u16)(DSW(s) + k + 1);
        }
        break;
    case 1: {                                              /* ships: line up astern */
        u16 t = DSW(s + 4);
        point_ahead(pt, (u16)(OW(t, 0x14) + 0x8000), 40000);
        DSSL(pt + 8) = alt;
        steer_to_point(p, pt);
        if (ldist2((u16)(p + 4), pt) < 4000) DSW(s)++;
        break;
    }
    case 2: {
        memcpy(DSP(a), DSP(pt), 12);
        if (!k) {
            u16 t = DSW(s + 4);
            s32 d = (s32)ldist2(pt, (u16)(p + 4)) - 5000;
            point_ahead(a, (u16)(OW(t, 0x14) + 0x8000), (u32)d);
            OB(t, 0x42) |= 8;
            OB(t, 0x46) |= 8;
        }
        u16 e = (u16)steer_to_point(p, a);
        steer_pitch_to(p, (u16)(e + 1000) < 2000 ? (u16)-3640 : 0, 0);
        THROTTLE(p) = (u16)(CRUISE(p) - 60);
        if (ldist2((u16)(p + 4), pt) < (u32)(s32)DSS(0x1492 + 2 * k)) {
            release_ordnance(p, pt);
            DSW(s)++;
            DSW(s + 8) = 7000;
        }
        break;
    }
    case 3:
        steer_level(p);
        if (timer16_dec((u16)(s + 8)) == 0) {
            if (addon1_flag()) attack_reenter(p);
            else ai_think_target(p);
        }
        break;
    default: break;
    }
out:
    stk_release(mk);
    return r;
}

/* 0deb:0896 level_attack(p, low) — §11.2 (14CDh glide: low 0; 14E1h: low 1, never releases) */
static u16 level_attack(u16 p, u16 low)
{
    u16 s = SCR(p);
    u16 mk = stk_mark(), pt = stk_alloc(12), aim = stk_alloc(12);
    u16 r = 0;
    attack_point(s, pt);
    s32 dzh = DSSL(pt + 8) + 40000 - OSL(p, 0x0C);
    u16 hi = low && OSL(p, 0x0C) > 15000;
    if (target_destroyed(s)) { r = 1; goto out; }
    aim_jitter(p, s, pt);
    u16 b = (u16)(bearing_between(pt, (u16)(p + 4)) - OW(p, 0x14));
    switch (DSW(s)) {
    case 0:
        if (ap_approach_0deb(p, pt, 40000, 2000, hi ? OSL(p, 0x0C) : 40000)) {
            if (DSW(s + 2) == 0 && DSL(s + 0x0C) == 0) pick_subtarget(s, -1);
            else DSW(s)++;
        }
        break;
    case 1:
        fly_heading_alt(p, b, hi ? 0 : dzh);
        if ((u16)(b + 500) < 1000 && (u16)(OW(p, 0x12) + 500) < 1000 &&
            (s16)(elevation_to(pt, (u16)(p + 4)) - OW(p, 0x10)) < -8000)
            DSW(s)++;
        break;
    case 2: {
        s16 lead = DSS(0x1466 + 2 * low);
        memcpy(DSP(aim), DSP(pt), 12);
        OB(p, 0x45) |= 0x40;
        OB(p, 0x49) |= 0x40;
        OW(p, 0xC1) |= 8;
        if (!low) {
            if (DSW(s + 2) != 0) {
                u16 t = DSW(s + 4);
                s16 v = (s16)(((s32)(s16)(OSL(t, 0x74) >> 8) * 300) >> 10);
                lmove_heading(pt, OW(t, 0x14), (u16)v);
                memcpy(DSP(aim), DSP(pt), 12);
            }
            lmove_heading(pt, OW(p, 0x14), 6000);
        }
        s16 e = (s16)elevation_to(pt, (u16)(p + 4));
        if ((s16)(lead + 0x8001) < e) e = (s16)(e - lead);
        steer_pitch_to(p, (u16)e, 0);
        THROTTLE(p) = (u16)(CRUISE(p) - 23);
        OB(p, 0xBA) = (u8)((OB(p, 0xBA) & 0xFB) | ((low == 0) << 2));   /* dive brakes */
        steer_heading_nolead(p, 0, b);
        if (!low) {
            s32 dz = OSL(p, 0x0C) - DSSL(pt + 8);
            if (dz < 7000) {
                if (has_ordnance(p)) release_ordnance(p, aim);
                if (dz < 3000) DSW(s)++;
            }
        }
        break;
    }
    case 3:
        OB(p, 0x45) |= 0x40;
        OB(p, 0x49) |= 0x40;
        steer_level(p);
        OW(p, 0xAE) = 0x100;
        if (OS(p, 0x74) > 0) DSW(s)++;                     /* climbing again */
        break;
    case 4:
        OB(p, 0x45) |= 0x40;
        OB(p, 0x49) |= 0x40;
        steer_heading_nolead(p, 0, (u16)(b + 0x8000));
        steer_altitude(p, (u32)(OSL(p, 0x0C) - 500), 1);
        THROTTLE(p) = CRUISE(p);
        if (ldist2((u16)(p + 4), pt) > 15000) attack_reenter(p);
        break;
    default: break;
    }
out:
    stk_release(mk);
    return r;
}

/* 0deb:0c1e (14E1h) / 0c2f (14CDh) */
static u16 st_low_attack(u16 p) { return level_attack(p, 1); }
static u16 st_glide_attack(u16 p) { return level_attack(p, 0); }

/* 0deb:0c40 (14B9h) torpedo run — §11.3 */
static u16 st_torpedo_steer(u16 p)
{
    u16 s = SCR(p);
    u16 t = DSW(s + 4);
    s16 side = (s16)OC(p, 0x26);
    s16 alt = DSS(0x148A + 2 * side);
    u16 mk = stk_mark(), pt = stk_alloc(12);
    u16 r = 0;
    memcpy(DSP(pt), DSP(t + 4), 12);
    if (target_destroyed(s)) { r = 1; goto out; }
    aim_jitter(p, s, pt);
    s32 dz = (s32)(s16)(alt + 300) - OSL(p, 0x0C);
    s32 d = (s32)ldist2((u16)(p + 4), pt);
    s16 tv = (s16)(OSL(t, 0x74) >> 16);
    switch (DSW(s)) {
    case 0: {
        s16 o = 0;
        if (tv == 0) {
            o = DSW(s + 6) ? 16000 : -16000;
            if (dz > -3000 && d > 30000 &&
                (u16)(OW(t, 0x14) + o + 0x8000 - OW(p, 0x14) + 0x0FA0) < 0x1F40) {
                DSW(s)++;
                goto out;
            }
        }
        OB(p, 0x45) |= 0x40; OB(p, 0x49) |= 0x40;
        OB(p, 0x45) |= 0x20; OB(p, 0x49) |= 0x20;
        THROTTLE(p) = CRUISE(p);
        point_ahead(pt, (u16)(OW(t, 0x14) + o), 45000);
        steer_to_point(p, pt);
        steer_altitude(p, (u32)-dz, 1);
        s32 dd = (s32)ldist2((u16)(p + 4), pt);
        if (OSL(p, 0x0C) < 2000) OW(p, 0xC1) |= 8;
        if (dd < 4000 && labs32(dz) < 500) DSW(s)++;
        break;
    }
    case 1: {
        s16 a = tv ? 0x1518 : 0x4000;
        u16 h = (u16)(OW(t, 0x14) + (DSW(s + 6) ? a : (s16)-a));
        s32 rr = (d < 40000 ? d : 40000) - 6000;
        OB(p, 0x45) |= 0x40; OB(p, 0x49) |= 0x40;
        OB(t, 0x42) |= 8; OB(t, 0x46) |= 8;
        point_ahead(pt, h, (u32)rr);
        DSSL(pt + 8) = alt;
        steer_to_point(p, pt);
        if (d < 40000 && ldist2(pt, (u16)(p + 4)) > 10000) {
            s16 b1 = (s16)(bearing_between(pt, (u16)(p + 4)) - OW(p, 0x14));
            s16 b2 = (s16)(bearing_between((u16)(t + 4), (u16)(p + 4)) - OW(p, 0x14));
            if ((b2 > 0) == (b1 > 0) && abs16(b2) < abs16(b1))
                steer_heading_nolead(p, 0, b1 > 0 ? (u16)-12000 : 12000);
        }
        OW(p, 0xC1) |= 8;
        s16 e = (s16)(OW(p, 0x94) - 12 * DSS(0x1486 + 2 * side));                   /* hold the drop speed */
        OW(p, 0xB2) = (u16)(OW(p, 0xB2) - (s16)(((s32)e * 1200) >> 10));
        OW(p, 0xB2) = (u16)clamp(OS(p, 0xB2), 0, 0x100);
        if (d < 15000 && (u16)(h + 0x8000 - OW(p, 0x14) + 0x0FA0) < 0x1F40) {
            release_ordnance(p, (u16)(t + 4));
            DSW(s)++;
        }
        break;
    }
    case 2: {
        OB(p, 0x45) |= 0x40; OB(p, 0x49) |= 0x40;
        OW(p, 0xC1) |= 8;
        steer_level(p);
        s32 z = (s32)(s16)(alt + 400) - OSL(p, 0x0C);
        steer_altitude(p, (u32)-z, 0);
        THROTTLE(p) = CRUISE(p);
        if (z < 100) DSW(s)++;
    }   /* fall through (as compiled) */
    /* FALLTHRU */
    case 3: {
        OB(p, 0x45) |= 0x40; OB(p, 0x49) |= 0x40;
        steer_heading_nolead(p, 0, (u16)(bearing_between((u16)(t + 4), (u16)(p + 4)) - OW(p, 0x14) + 0x8000));
        s32 z = (s32)(s16)(alt + 400) - OSL(p, 0x0C);
        steer_altitude(p, (u32)-z, 0);
        THROTTLE(p) = CRUISE(p);
        if (d > 15000) attack_reenter(p);
        break;
    }
    default: break;
    }
out:
    stk_release(mk);
    return r;
}

/* 0deb:10ea strafe_run(p, rockets) — §11.4; returns 1 when the pass is over */
static u16 strafe_run(u16 p, u16 rockets)
{
    u16 s = SCR(p);
    u16 mk = stk_mark(), pt = stk_alloc(12);
    u16 r = 0;
    attack_point(s, pt);
    u16 k = DSW(s + 2) == 0;
    if (target_destroyed(s)) { r = 1; goto out; }
    if (DSW(s + 2) != 0) DSSL(pt + 8) += DSS(SDAT(DSW(s + 4)) + 4);
    DSSL(pt + 8) += DSS(0x146A + 2 * k);
    u16 b = (u16)(bearing_between(pt, (u16)(p + 4)) - OW(p, 0x14));
    s32 dz = DSSL(pt + 8) + 2000 - OSL(p, 0x0C);
    s32 d = (s32)ldist2((u16)(p + 4), pt);
    OB(p, 0x45) |= 0x40;
    OB(p, 0x49) |= 0x40;
    switch (DSW(s)) {
    case 0:
        if (ap_approach_0deb(p, pt, 20000, 500, 2000)) {
            if (k && DSL(s + 0x0C) == 0) pick_subtarget(s, rockets ? -1 : (s16)first_gun_type(p));
            else DSW(s)++;
        }
        break;
    case 1:
        fly_heading_alt(p, b, dz);
        if ((s32)DSS(0x146E + 2 * k) > d) DSW(s)++;
        break;
    case 2: {
        OW(p, 0xC1) |= 8;
        u16 e = (u16)(elevation_to(pt, (u16)(p + 4)) - OW(p, 0x10));
        steer_climb(p, 0, (u16)((s16)(e - 0x15E) >> 1), 1);
        steer_heading_nolead(p, 0, b);
        THROTTLE(p) = CRUISE(p);
        s16 R = rockets ? DSS(0x1472 + 2 * k) : DSS(0x1476 + 2 * k);
        u16 T = rockets ? DSW(0x147A + 2 * k) : DSW(0x147E + 2 * k);
        if ((s32)R > d && (u16)(b + 1000) < 2000 && (u16)(e + T) < (u16)(T << 1)) {
            if (rockets) {
                if (rockets_left(p)) rocket_release(p);
                else DSW(s)++;
            } else {
                ai_fire(p, 0, (u16)(p + 0x10), d, 0);
            }
        }
        if ((s32)DSS(0x1482 + 2 * k) > d) DSW(s)++;
        break;
    }
    case 3: {
        steer_heading_nolead(p, 0, (u16)(b + 0x8000));
        s32 z = DSSL(pt + 8) + 1000 - OSL(p, 0x0C);
        steer_altitude(p, (u32)-z, 0);
        THROTTLE(p) = CRUISE(p);
        if (d > 16000) r = 1;
        break;
    }
    default: break;
    }
out:
    stk_release(mk);
    return r;
}

/* 0deb:13ac (1509h) rocket run */
static u16 st_rocket_steer(u16 p)
{
    if (addon1_flag() && target_destroyed(SCR(p))) return 1;
    if (strafe_run(p, 1)) attack_reenter(p);
    return 0;
}

/* 0deb:13f3 (151Dh) strafing: two passes, then no more strafing */
static u16 st_strafe_steer(u16 p)
{
    u16 s = SCR(p);
    if (strafe_run(p, 0)) {
        DSL(s + 0x0C) = 0;
        DSW(s + 8)++;
        if (DSS(s + 8) >= 2) {
            OB(p, 0x1B) |= 0x20;
            set_state(p, D_CLIMB);
        } else {
            DSW(s) = 0;
        }
    }
    return 0;
}

/* 0deb:143c */
u16 first_gun_type(u16 p) { return DSW(OW(p, 0x5E) + 4); }

/* 0deb:144e fixed_guns_loaded(p) */
u16 fixed_guns_loaded(u16 p)
{
    u16 gl = OW(p, 0x5E), st = OW(p, 0x60);
    u16 fl = DSW(gl + 2), r = 0;
    if (fl & 0x2800) r = DSS(st) > 0;
    if (fl & 0x0500) r |= DSS(st + 0x0C) > 0;
    return r;
}

/* 0deb:1490 has_ordnance(p) — returns the bit (40h) */
u16 has_ordnance(u16 p) { return OB(p, 0x1B) & 0x40; }

/* 0deb:14a2 */
u16 is_dive_bomber(u16 p) { return CLS(p) == 2 && has_ordnance(p) ? 1 : 0; }

/* 0deb:14dc */
u16 torpedo_loaded(u16 p) { return OB(p, 0xF1) != 0 && has_ordnance(p) ? 1 : 0; }

/* 0deb:1500 */
u16 rockets_left(u16 p) { return (u16)(s16)OC(p, 0xEB); }

/* 0deb:150f can_strafe(p) */
u16 can_strafe(u16 p)
{
    if (OB(p, 0x1B) & 0x20) return 0;
    if (OB(DAT(p), 9) == 0) return 0;
    return fixed_guns_loaded(p) ? 1 : 0;
}

/* 0deb:1555 is_pure_fighter(p) */
u16 is_pure_fighter(u16 p) { return CLS(p) == 0 && !has_ordnance(p) ? 1 : 0; }

/* 0deb:15a2 bombers_assign(f, place) — every member with ordnance bombs its own entry of the list */
u16 bombers_assign(u16 f, u16 place)
{
    u16 mk = stk_mark(), m = stk_alloc(0x0C), l = stk_alloc(0x20);
    s16 n = (s16)flight_alive_members(f, m);
    u16 k = top_targets(place, n, l, -1);
    if (k) {
        for (s16 i = 0; i < n; i++) {
            u16 q = DSW(m + 2 * i);
            if (!has_ordnance(q)) continue;
            OB(q, 0x1C) |= 1;
            u16 s = set_state(q, D_BOMBER);
            DSL(s + 0x0C) = DSL(l + 4 * i);
        }
    }
    stk_release(mk);
    return k;
}

/* 0deb:1637 torp_side(p, ship) (RNG on a tie) */
static u16 torp_side(u16 p, u16 ship)
{
    if ((OB(p, 0x1E) & 8) || (OB(ship, 0x1E) & 0x80)) return 1;
    if ((OB(p, 0x1E) & 0x10) || (OB(ship, 0x1E) & 0x40)) return 0;
    if (OSL(ship, 0x74) == 0) return (s16)(bearing_between((u16)(p + 4), (u16)(ship + 4)) - OW(ship, 0x14)) < 0;
    s16 c[2] = { 0, 0 };
    u16 f = FLT(p);
    for (s16 i = 0; OS(f, 6) > i; i++) {
        u16 q = DSW(OW(f, 8) + 2 * i);
        if (q == PLAYER || q == p || DESC(q) != D_TORPEDO) continue;
        u16 s = SCR(q);
        if (DSW(s + 4) == ship) c[DSW(s + 6) & 1]++;        /* PORT: & 1 bounds the index (the word is 0 or 1) */
    }
    if (c[0] < c[1]) return 0;
    if (c[0] > c[1]) return 1;
    return RNG() & 1;                                                                /* RNG */
}

/* 0deb:1718 ships_land_check — ships at the end of their route or at the first waypoint: land within
 * 100 × 100 units on either side */
void ships_land_check(void)
{
    u16 mk = stk_mark(), a = stk_alloc(12), b = stk_alloc(12), v = stk_alloc(6);
    for (s16 i = 0; i < (s16)DSW(0x4774); i++) {
        u16 s = DSW(DSW(0x4759) + 2 * i);
        u16 f = FLT(s), cur = (u16)(f + 0x16);
        if (DSW(cur) != DSW(cur + 2) && DSW(DSW(cur + 4) + 2 * DSW(cur)) != OW(f, 0x0A)) continue;
        memcpy(DSP(a), DSP(s + 4), 12);
        memcpy(DSP(b), DSP(s + 4), 12);
        DSW(v) = 100;
        DSW(v + 2) = 0;
        DSW(v + 4) = 0;
        vrot_z(v, OW(s, 0x14));
        for (s16 k = 0; k < 100; k++) {
            if ((u8)ground_under((u32)cur_dgroup << 16 | a)) OB(s, 0x1E) |= 0x80;
            if ((u8)ground_under((u32)cur_dgroup << 16 | b)) OB(s, 0x1E) |= 0x40;
            ladd_v(a, v);
            lsub_v(b, v);
        }
    }
    stk_release(mk);
}

/* 0deb:1817 ship_attack_setup(p, ship) */
u16 ship_attack_setup(u16 p, u16 ship)
{
    u16 d = 0, side = 0;                                   /* PORT: side is uninitialised unless torpedo */
    if (OB(FLT(p), 2) == 2) d = D_LOW;
    else if (torpedo_loaded(p)) { d = D_TORPEDO; side = torp_side(p, ship); }
    else if (is_dive_bomber(p)) d = D_GLIDE;
    else if (has_ordnance(p)) d = D_DIVEBOMB;
    else if (rockets_left(p)) d = D_ROCKET;
    else if (OB(SDAT(ship), 0x6D) != 0 && can_strafe(p)) d = D_STRAFE;
    if (d) {
        u16 s = set_state(p, d);
        DSW(s + 2) = 1;
        DSW(s + 4) = ship;
        DSW(s + 6) = side;
    }
    return d != 0;
}

/* 0deb:18e6 g_attack_area_load(area) — the area's 26-byte shape → list table into DS:576E */
void g_attack_area_load(u16 area)
{
    u16 name = res_name(0x149C, 0x05BC);
    u16 f = my_fopen(name, 0x05EB);
    if (!f) assert_fail(G_ATTACK_C, 0x49E, name);
    s16 off = (s16)((area - 1) * 0x1A);
    if (off) my_fseek(f, off, 1);
    my_fread(0x576E, 1, 0x1A, f);
    my_fclose(f);
}

/* 0deb:196e g_attack_load — at 208h: count, then {n, entries[n] of 4 bytes} */
static void g_attack_load(void)
{
    u16 mk = stk_mark(), n = stk_alloc(2);
    u16 name = res_name(0x149C, 0x05BC);
    u16 f = my_fopen(name, 0x05EB);
    if (!f) assert_fail(G_ATTACK_C, 0x4B3, name);
    my_fseek(f, 0x208, 1);
    my_fread(0x578A, 2, 1, f);
    DSW(0x5788) = mem_new((u16)(DSW(0x578A) << 2), 0, far_to_near_str(G_ATTACK_C), 0x4BB);
    u16 l = DSW(0x5788);
    for (s16 i = 0; i < (s16)DSW(0x578A); i++, l = (u16)(l + 4)) {
        my_fread(n, 2, 1, f);
        DSW(l) = DSW(n);
        DSW(l + 2) = mem_new((u16)(DSW(n) << 2), 0, far_to_near_str(G_ATTACK_C), 0x4C4);
        my_fread(DSW(l + 2), 4, (s16)DSW(n), f);
    }
    my_fclose(f);
    stk_release(mk);
}

/* 0deb:1a7b objtype_group(far obj) — DS:576E[type] or −1 */
static s16 objtype_group(FarPtr o)
{
    u16 i = FW(o, 2) & 0x1FFF;
    return (s16)i < 0x1A ? (s16)DSC(0x576E + i) : -1;
}

/* 0deb:1aa0 target_entry(rec, grp) — the G_ATTACK entry of the object's type, or 0 */
static u16 target_entry(FarPtr rec, FarPtr grp)
{
    s16 g = objtype_group(u32_far(FL(grp, 0)));
    if (g <= -1) return 0;
    s16 type = (s16)objtype_class(FW(rec, 2));
    u16 e = (u16)(DSW(0x5788) + 4 * g);
    u16 q = DSW(e + 2);
    for (s16 i = 0; (s16)DSW(e) > i; i++, q = (u16)(q + 4))
        if ((s16)DSC(q) == type) return q;
    return 0;
}

/* 0deb:1afc target_value(rec, grp) */
u16 target_value(u32 rec, u32 grp)
{
    u16 e = target_entry(u32_far(rec), u32_far(grp));
    return e ? DSW(e + 2) : 0;
}

/* 0deb:1b1f target_name(rec, grp) → far string or 0 */
u32 target_name(u32 rec, u32 grp)
{
    u16 e = target_entry(u32_far(rec), u32_far(grp));
    return e ? far_u32(str(6, (s16)DSC(e + 1))) : 0;
}

/* 0deb:1b50 top_targets(place, n, out, hard) — §11.5; out = far pointers into DS:5146 */
u16 top_targets(u16 place, s16 n, u16 out, s16 hard)
{
    u8 taken[50];
    s16 j = 0;
    memset(taken, 0, sizeof taken);
    FarPtr R = ds_far(0x5146), G = ds_far(0x5142);
    while (j < n) {
        FarPtr rec = far_add(R, (u16)(OW(place, 0x18) * 0x16));
        FarPtr grp = far_add(G, (u16)(OW(place, 0x18) * 6));
        s16 best = -1, pick = 0;
        for (s16 i = 0; OS(place, 0x1A) > i; i++, rec = far_add(rec, 0x16), grp = far_add(grp, 6)) {
            if (i < 50 && taken[i]) continue;
            if (hard >= 0 && (s16)DSC(0x5A87 + 8 * objtype_class(FW(rec, 2))) > hard) continue;
            FarPtr ob = u32_far(FL(grp, 0));
            ob.off = (u16)(ob.off + FW(grp, 4));
            if (FB(ob, 0x12) == 0xFF) continue;             /* destroyed */
            s16 v = (s16)target_value(far_u32(rec), far_u32(grp));
            if (v > best) { best = v; pick = i; }
        }
        if (best <= -1) {
            if (j == 0) return 0;
            for (s16 x = j, y = 0; x < n; x++, y++) DSL(out + 4 * x) = DSL(out + 4 * y);   /* cyclic fill */
            return (u16)j;
        }
        if (pick < 50) taken[pick] = 1;
        FarPtr e = far_add(R, (u16)((OW(place, 0x18) + pick) * 0x16));
        DSL(out + 4 * j) = far_u32(e);
        j++;
    }
    return (u16)j;
}

/* 0deb:1cae place_attack_setup(p, place) */
u16 place_attack_setup(u16 p, u16 place)
{
    u16 d = 0;
    if (OB(FLT(p), 2) == 2) d = D_LOW;
    else if (is_dive_bomber(p)) d = D_GLIDE;
    else if (has_ordnance(p) && !torpedo_loaded(p)) d = CLS(p) == 3 ? D_BOMBER : D_DIVEBOMB;
    else if (rockets_left(p)) d = D_ROCKET;
    else if (can_strafe(p)) d = D_STRAFE;
    if (d) {
        u16 s = set_state(p, d);
        DSW(s + 2) = 0;
        DSW(s + 4) = place;
    }
    return d != 0;
}

/* 0deb:1d6d attack_reenter(p) */
u16 attack_reenter(u16 p)
{
    u16 s = SCR(p);
    u16 r = DSW(s + 2) ? ship_attack_setup(p, DSW(s + 4)) : place_attack_setup(p, DSW(s + 4));
    if (!r) set_state(p, D_CLIMB);
    return r;
}

/* 0deb:1daf place_damage_ratio(place) — 0..400h (scoring) */
u16 place_damage_ratio(u16 place)
{
    FarPtr rec = far_add(ds_far(0x5146), (u16)(OW(place, 0x18) * 0x16));
    FarPtr grp = far_add(ds_far(0x5142), (u16)(OW(place, 0x18) * 6));
    s16 sv = 0, sd = 0;
    for (s16 i = 0; OS(place, 0x1A) > i; i++, rec = far_add(rec, 0x16), grp = far_add(grp, 6)) {
        s16 v = (s16)target_value(far_u32(rec), far_u32(grp));
        sv = (s16)(sv + v);
        FarPtr ob = u32_far(FL(grp, 0));
        ob.off = (u16)(ob.off + FW(grp, 4));
        u8 dmg = FB(ob, 0x12);
        sd = (s16)(sd + (dmg == 0xFF ? v : (s16)(((s32)dmg * v) >> 8)));
    }
    if (sv == 0 && sd == 0) return 0;
    return (u16)ldiv_sat((s32)sd << 10, sv);
}

/* 0deb:1e83 — the 7 descriptors at DS:1531 */
void g_attack_init(void)
{
    g_attack_load();
    class_register_n(7, 0x1531);
}

__attribute__((constructor)) static void g_attack_codeptrs(void)
{
    static const struct { u32 a; void *f; } t[] = {
        { AP_FN_st_bomber_steer, st_bomber_steer }, { AP_FN_st_divebomb_steer, st_divebomb_steer },
        { AP_FN_st_low_attack, st_low_attack }, { AP_FN_st_glide_attack, st_glide_attack },
        { AP_FN_st_torpedo_steer, st_torpedo_steer }, { AP_FN_st_rocket_steer, st_rocket_steer },
        { AP_FN_st_strafe_steer, st_strafe_steer },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) codeptr_register(PROG_AP, t[i].a, (CodeFn)t[i].f);
}
