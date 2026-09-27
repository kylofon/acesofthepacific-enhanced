/* sim_world — per-aircraft step and AI steering primitives (AP segment 4bf9, sim_world_fm.md §4).
 * plane_fly picks the flight model and runs fm_step; the steer_* primitives turn a desired attitude
 * into stick/rudder/throttle values in the Controls block (Plane+AC..B2). Ported from the disassembly
 * (differences from the spec: fly_module_init sets the gear-touch latch and timer). */
#include "sim_world/swa_priv.h"

/* aircraft .DAT of a plane: DS:55D2[DS:54BC[type]] */
static u16 plane_dat(u16 p) { return DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2)); }
/* pilot profile of a plane: DS:55CE[pilot] */
static u16 plane_pilot(u16 p) { return DSW((u16)(DSW(0x55CE) + OB(p, 0xBB) * 2)); }

/* AP 4bf9:0000 — sim_world_fm.md §4 (the spec says empty: it arms the gear-touch latch) */
void fly_module_init(void)
{
    DSB(0x3F28) = 1;
    DSW(0x3F26) = 4;
}

/* AP 4bf9:0010 — debug hook: the player flown by the AI when != 0 */
u16 fly_player_is_ai(void) { return 0; }

/* AP 4bf9:0017 — engine note, gear touch-down and fly-by sounds (sim_world_fm.md §4.2) */
void plane_engine_sound(u16 p)
{
    u16 dat = plane_dat(p);
    if (p != PLAYER) {
        s8 t = (s8)DSB((u16)(dat + 0x75));
        sx_play((u16)(s16)(s8)DSB((u16)(0x2DA + t)), (u16)(p + 4), (u16)(p + 0x70), 0);
        return;
    }
    s16 rpm;
    if (OS(OW(p, 0x66), 0x90) > 1) rpm = (s16)((s16)(DSW(0x3FDE) + DSW(0x3FE0)) >> 1);
    else rpm = DSS(0x3FDE);
    s8 type = (s8)DSB((u16)(dat + 0x75));
    u16 cx;
    switch ((u16)(s16)type) {
    case 0: cx = (u16)((s16)(((s32)rpm * 0x13B0) >> 10) + 0x28A0); break;
    case 1: cx = (u16)((s16)(((s32)rpm * 0x0DFA) >> 10) + 0x28A0); break;
    case 2: cx = (u16)((s16)(((s32)rpm * 0x0578) >> 10) + 0x3908); break;
    case 5: cx = (u16)((s16)(((s32)rpm * 0x0DFA) >> 10) + 0x28A0); break;
    default: cx = 0x2000; break;   /* PORT: types 3, 4, >5 leave CX as the caller had it; 2000h used */
    }
    sx_play_ex((u16)(s16)(s8)DSB((u16)(0x2D4 + type)), (u16)(p + 4), cx, (u16)(p + 0x70), 0, 1);
    if (DSS(0x3F26) > 0) {
        DSW(0x3F26)--;
    } else {
        u8 on = (s32)(s16)(OW(OW(p, 0x66), 0x98) + OW(p, 0xB8)) >= OSL(p, 0x0C);
        if (DSB(0x3F28) != 0) {
            if (!on) DSB(0x3F28) = 0;
        } else if (on) {
            DSB(0x3F28) = 1;
            if (OB(p, 0xBA) & 0x10) sx_play(0x16, (u16)(p + 4), (u16)(p + 0x70), 1);
        }
    }
    if (DSW(0x19F2) != 0) return;
    s32 best = 100000;
    u16 cam = (u16)camera_obj_ref();
    u16 bpos = 0, bvel = 0;
    for (s16 i = 0, si = 0; i < DSS(0x4776); i++, si += 2) {
        u16 q = DSW((u16)(DSW(0x475B) + si));
        u16 qd = plane_dat(q);
        if ((s8)DSB((u16)(qd + 0x75)) >= 3) continue;
        s32 d = (s32)ldist3((u16)(q + 4), cam);
        if (d >= best) continue;
        q = DSW((u16)(DSW(0x475B) + si));
        if (OW(q, 0xB2) == 0) continue;
        best = d;
        bpos = (u16)(q + 4);
        bvel = (u16)(q + 0x70);
    }
    if (best < 5000) sx_play(0x1F, bpos, bvel, 1);
}

/* AP 4bf9:0257 — sim_world_fm.md §4.1 */
void plane_fly(u16 p)
{
    u16 ctl = (u16)(p + 0xAC);
    s16 k = 0x400, mode;
    u16 blk = OW(p, 0x16);
    DSB(blk) = DSB(blk) == 0 ? 1 : 0;
    if (OB(p, 0xBB) == 0x2C && !fly_player_is_ai()) {
        mode = (s16)DSW((u16)(0x2CE + (s16)(s8)DSB(0x42D3) * 2));
    } else {
        if (OB(p, 0xBB) != 0x2C) {
            OW(ctl, 4) = (u16)steps_to_frames(OS(ctl, 4));
            OW(ctl, 0) = (u16)steps_to_frames(OS(ctl, 0));
            OW(ctl, 2) = (u16)steps_to_frames(OS(ctl, 2));
        }
        mode = 4;
    }
    OW(ctl, 4) = (u16)ap_clamp_sym_277a(OS(ctl, 4), 0x100);
    OW(ctl, 0) = (u16)ap_clamp_sym_277a(OS(ctl, 0), 0x100);
    OW(ctl, 2) = (u16)ap_clamp_sym_277a(OS(ctl, 2), 0x100);
    if (mode == 4) {
        if (OW(p, 0xC1) & 0x10) k = 0;
        else if (OW(p, 0xC1) & 1) k = 0x400;
        else {
            s16 f = DSS((u16)(0x2E0 + (s16)(s8)DSB(0x42D2) * 2));
            s16 a = (s16)(OW(plane_pilot(p), 0x0E) - 0x3B6);
            k = (s16)((s16)(((s32)a * f) >> 10) + 0x3B6);
        }
    }
    if (OW(p, 0xC1) & 2) mode = 1;
    s16 level = (OW(p, 0xC1) & 8) && (u16)(OW(p, 0x12) + 0x1389) < 0x2712;
    obj_matrix(OW(p, 0x20));
    fm_step((u16)(p + 0x64), (u16)(p + 0xAC), mode, k, 0, level);
    OW(p, 0xC1) = 0;
    plane_engine_sound(p);
}

/* AP 4bf9:03ce — aileron toward a roll angle; 1 when within tol (or in a steep dive) */
u16 steer_roll_to(u16 p, s16 roll, s16 tol)
{
    if (OS(p, 0x10) < (s16)0xC950) return 1;
    OS(p, 0xAC) = (s16)((s16)(roll - OS(p, 0x12)) >> 4);
    return (u16)in_open_range(OS(p, 0xAC), tol);
}

/* AP 4bf9:0401 — elevator (or rudder when banked) toward a pitch angle. Quirk kept: when banked the
 * elevator is not written and the stale elevator is tested. */
u16 steer_pitch_to(u16 p, u16 pitch, u16 tol)
{
    s16 d = (s16)(pitch - OW(p, 0x10));
    if ((u16)(OW(p, 0x12) + 0x1F40) < 0x3E80) OS(p, 0xAE) = (s16)(d >> 5);
    else OS(p, 0xB0) = (s16)((s16)(OS(p, 0x12) < 0 ? d : (s16)-d) >> 5);
    return (u16)in_open_range(OS(p, 0xAE), (s16)tol);
}

/* AP 4bf9:044b — bank-to-turn toward a heading change (sim_world_fm.md §4) */
void steer_heading(u16 p, u16 tgt, s16 dhead, s16 lead)
{
    s16 v = OS(p, 0x90);
    s16 di = (s16)(((s32)dhead * 45) >> 10);
    if (tgt) {
        if (lead && OW(p, 0xD7) == tgt) {
            s16 dx = (s8)OB(p, 0xCE);
            if (dx != -0x80) di = (s16)(di + (s16)(((s32)dx * -2000) >> 10));
        }
        v = (s16)(v - (s16)mul_cos(OS(tgt, 0x8E), (u16)(OW(p, 0x14) - OW(tgt, 0x14))));
    }
    di = (s16)(di - (s16)(((s32)dt_scale(v) * 400) >> 10));
    if ((u16)(di + 45) < 90 && (u16)(OW(p, 0x12) + 0x7D0) < 0xFA0) {
        if ((di < 0) == (OS(p, 0x12) < 0))
            steer_roll_to(p, 0, 0);          /* PORT: the original pushes no tol (stack garbage); result unused */
        OS(p, 0xB0) = (s16)-(s16)(di << 4);
        return;
    }
    u16 dat = plane_dat(p);
    s16 tbl = DSS((u16)(0x2C6 + (s16)(s8)DSB((u16)(dat + 0x38)) * 2));
    s16 lim = OS(plane_pilot(p), 0x0C);
    u16 fmd = OW(p, 0x66);
    di = ldiv_sat((s32)di << 9, OS(fmd, 0x74));
    di = ldiv_sat((s32)di << 10, OS(fmd, 0x74));
    if ((u16)(di + 0x100) < 0x200) di = (s16)(((s32)di * abs16(di)) >> 7);
    if (lim >= tbl) lim = tbl;
    s16 b4 = (s16)(lim - 500);
    if (OS(p, 0x12) >= b4 && di < 0) { steer_roll_to(p, lim, 0); return; }        /* PORT: tol garbage */
    if (OS(p, 0x12) <= (s16)-b4 && di > 0) { steer_roll_to(p, (s16)-lim, 0); return; }
    OS(p, 0xAC) = (s16)-di;
}

/* AP 4bf9:061b / 0632 */
void steer_heading_lead(u16 p, u16 tgt, u16 dhead) { steer_heading(p, tgt, (s16)dhead, 1); }
void steer_heading_nolead(u16 p, u16 tgt, u16 dhead) { steer_heading(p, tgt, (s16)dhead, 0); }

/* AP 4bf9:0649 — elevator for a pitch change, within the aircraft's pitch limits */
void steer_climb(u16 p, u16 tgt, u16 dpitch, u16 limited)
{
    u16 dat = plane_dat(p);
    s16 mn = limited ? OS(dat, 0x3A) : (s16)0xC000;
    s16 mx;
    if (limited) mx = (OW(OW(p, 0x4A), 6) & 0x10) ? OS(dat, 0x6B) : OS(dat, 0x57);
    else mx = 0x4000;
    s16 e = (s16)(((s32)(s16)dpitch * 100) >> 10);
    e = (s16)(e - (s16)(((s32)OS(p, 0x74) * 750) >> 10));
    if (tgt) e = (s16)(e + (s16)(((s32)OS(tgt, 0x74) * 750) >> 10));
    if (OS(p, 0x10) >= (s16)(mx - 500) && e > 0) { steer_pitch_to(p, (u16)mx, 0); return; }   /* PORT: tol garbage */
    if (OS(p, 0x10) <= (s16)(mn + 500) && e < 0) { steer_pitch_to(p, (u16)mn, 0); return; }
    if ((u16)(OW(p, 0x12) + 0xFA0) >= 0x1F40) {
        s16 r = OS(p, 0x12) < 0 ? e : (s16)-e;
        OS(p, 0xB0) = (s16)(r << 1);
        return;
    }
    OS(p, 0xAE) = e;
}

/* AP 4bf9:0762 — climb toward an altitude difference; returns the pitch error. The third word only
 * feeds a `sar si,0` (no effect). */
u16 steer_altitude(u16 p, u32 dz, u16 flag)
{
    (void)flag;
    s16 si = (s16)(bearing32((s32)dz, 5000) - OW(p, 0x10));
    steer_climb(p, 0, (u16)si, 1);
    return (u16)si;
}

/* AP 4bf9:07a1 — steer toward a target point (target position + offset), out: range/elevation/bearing */
void steer_intercept(u16 p, u16 tgt, u16 off, u16 shift, u16 facing, u16 out)
{
    u16 mk = stk_mark(), rel = stk_alloc(12);
    lsub(ladd3_v((u16)(tgt + 4), off, rel), (u16)(p + 4));
    u32 range = ldist2v((u32)rtl_labs(DSSL(rel)), (u32)rtl_labs(DSSL(rel + 4)));
    s16 b = (s16)(bearing32(DSSL(rel), DSSL(rel + 4)) - OW(p, 0x14));
    if (facing && (u16)(OW(p, 0x14) - OW(tgt, 0x14) + 0x4000) < 0x8000 && (u16)(b + 0x4000) >= 0x8000) {
        if (linbox(rel, 10000)) b = (s16)(0x8000 - (u16)b);
    }
    s16 elev = (s16)bearing32(-DSSL(rel + 8), 5000);
    if ((s32)range < 8000 && (u16)(elev + 0x2000) >= 0x4000)
        steer_roll_to(p, 0, 0);              /* PORT: tol garbage */
    else
        steer_heading_nolead(p, tgt, (u16)(b >> (shift & 31)));
    steer_climb(p, tgt, (u16)elev, 1);
    if (out) {
        OS(out, 6) = elev;
        OS(out, 0x0A) = b;
        OL(out, 2) = range;
    }
    stk_release(mk);
}

/* AP 4bf9:08d5 — fly toward a point; DX:AX = {altitude step result, heading error} */
u32 steer_to_point(u16 p, u16 pt)
{
    s16 b = (s16)(bearing_between(pt, (u16)(p + 4)) - OW(p, 0x14));
    steer_heading_nolead(p, 0, (u16)(b >> 1));
    u16 alt = steer_altitude(p, (u32)(OSL(p, 0x0C) - DSSL(pt + 8)), 1);
    s16 thr;
    if (OS(p, 0x10) > 0) thr = imin((s16)((OS(p, 0x10) >> 3) + 200), (s16)plane_cruise_throttle(p));
    else thr = (s16)plane_cruise_throttle(p);
    OS(p, 0xB2) = thr;
    return (u32)(u16)b | (u32)alt << 16;
}

/* AP 4bf9:0961 */
u16 plane_cruise_throttle(u16 p)
{
    if (OB(p, 0x42) & 0x10) return 0x100;
    return OW(plane_pilot(p), 0x10);
}

/* AP 4bf9:098b — throttle to keep station on a leader */
void steer_speed_match(u16 p, u16 lead, u16 len, u16 head, u16 dist)
{
    s16 t = (s16)((s16)mul_cos((s16)len, (u16)(head - OW(lead, 0x14))) - (s16)dist);
    s16 dv = (s16)(OW(lead, 0x6C) - OW(p, 0x6C));
    s16 s = (s16)((t >> 3) + (s16)(dv << 3));
    s16 thr = (s16)(OW(p, 0xB2) + s);
    OS(p, 0xB2) = clamp(thr, 0x8C, (s16)plane_cruise_throttle(p));
}

/* AP 4bf9:09f4 */
void controls_neutral(u16 c)
{
    OW(c, 0) = 0; OW(c, 4) = 0; OW(c, 2) = 0;
    OB(c, 0x0E) &= 0xFC; OB(c, 0x0E) &= 0xFB; OB(c, 0x0E) &= 0xF7;
}

/* AP 4bf9:0a18 */
void fly_module_end(void) {}
