/* sim_world part A — ship movement and brain states (5410).
 * sim_world_extra.md; control flow from the disassembly (x86dis AP_flat.exe). */
#include "sim_world/swa_priv.h"

/* sim_flow 362b:02e3 dt_mul returns the full 32-bit product in DX:AX (sim_flow.h declares s16) */
static s32 dt_mul32(s16 x) { return (s32)x * DT; }

static u16 ship_dat(u16 s) { return DSW((u16)(DSW(0x55D0) + DSB((u16)(0x555B + OB(s, 0x19))) * 2)); }

/* AP 5410:0000 — the group's lead ship */
u16 ship_group_marker(u16 s) { return DSW((u16)(OW(s, 0x34) + 0x2E)); }

/* AP 5410:0010 — 16.16 value toward `target` (integer part) by rate*DT; exact hit clears the fraction */
static s32 approach_hi(s32 v, s16 target, s16 rate)
{
    s16 hi = (s16)((u32)v >> 16);
    if (hi < target) {
        v += dt_mul32(rate);
        if ((s16)((u32)v >> 16) > target) v = (s32)(((u32)(u16)target << 16) | (u16)v);
    } else if (hi > target) {
        v -= dt_mul32(rate);
        if ((s16)((u32)v >> 16) < target) v = (s32)(((u32)(u16)target << 16) | (u16)v);
    }
    if ((s16)((u32)v >> 16) == target) v = (s32)((u32)(u16)target << 16);
    return v;
}

/* AP 5410:0078 */
static u16 ship_speed_update(u16 s, s16 target, s16 rate)
{
    OSL(s, 0x74) = approach_hi(OSL(s, 0x74), target, rate);
    return OW(s, 0x76);
}

/* AP 5410:00a3 — turn toward `target` (0: keep heading) and move at `speed`; returns angles_to's result */
static u32 ship_steer(u16 s, u16 target, s16 speed)
{
    u16 mk = stk_mark(), ang = stk_alloc(6);
    u8 stopped_turn = (OB(s, 0x42) & 8) != 0;
    u16 dat = ship_dat(s);
    u32 r;
    if (target) {
        r = angles_to(ang, target, (u16)(s + 4));
        s16 rate = stopped_turn ? 0 : DSS(dat + 0x48);
        DSB(0x4332) = approach_dt((u16)(s + 0x14), DSS(ang + 4), rate) == 0;
    } else {
        DSB(0x4332) = 1;
        r = 0;
    }
    u16 v = ship_speed_update(s, speed, DSS(dat + 0x4C));
    DSW(0x4330) = v;
    dir_scaled((u16)(s + 4), (u16)(s + 0x10), (u16)dt_scale((s16)v));
    stk_release(mk);
    return r;
}

/* AP 5410:016e — brain state 682h: coast to a stop */
u16 ship_stop(u16 s)
{
    if (OL(s, 0x74) != 0) ship_steer(s, 0, 0);
    return 0;
}

/* AP 5410:018c — brain state 66Eh: sail the group's waypoints, stop at the end */
u16 ship_follow_waypoints(u16 s)
{
    u16 wp = wp_current_pos((u16)(OW(s, 0x34) + 0x16));
    if (wp) ship_steer(s, wp, DSS(ship_dat(s) + 0x4A));
    else {
        ship_stop(s);
        brain_set_state((u16)(s + 0x4A), 0x0682);
    }
    return 0;
}

/* AP 5410:01e7 — brain state 65Ah: keep the formation slot (+68h, leader frame) two lengths ahead */
u16 ship_keep_station(u16 s)
{
    u16 mk = stk_mark(), dir = stk_alloc(6), ahead = stk_alloc(12), slot = stk_alloc(12);
    u16 lead = ship_group_marker(s);
    dir_from_angles(dir, (u16)(lead + 0x10));
    u16 a = v2l_shl(ahead, dir, 2);
    ladd(ladd(lrotate((u16)(s + 0x68), obj_matrix(OW(lead, 0x20)), slot), (u16)(lead + 4)), a);
    s16 v = OS(lead, 0x76);
    s32 d = (s32)ldist2(slot, (u16)(s + 4));
    if (d < 0xD556) v = (s16)(v - (s16)(v << 2));          /* quirk kept: -3x, not a slow-down */
    else if (d > 0x12AAA) v = (s16)(v + (s16)(v << 2));
    ship_steer(s, slot, v);
    if (OW(lead, 0x4A) == 0x0682) brain_set_state((u16)(s + 0x4A), 0x0682);
    stk_release(mk);
    return 0;
}

/* AP 5410:02c0 — lead ships (type 0 marker) follow waypoints, the others keep station */
void ship_brain_init(u16 s)
{
    brain_set_state((u16)(s + 0x4A), OB(s, 0x19) == 0 ? 0x066E : 0x065A);
}

/* AP 5410:02ed — the three ship states DS:065A, 066E, 0682 */
void ship_classes_register(void) { class_register_n(3, 0x0696); }

__attribute__((constructor)) static void ships_codeptrs_register(void)
{
    codeptr_register(PROG_AP, 0x541001E7u, (CodeFn)ship_keep_station);
    codeptr_register(PROG_AP, 0x5410018Cu, (CodeFn)ship_follow_waypoints);
    codeptr_register(PROG_AP, 0x5410016Eu, (CodeFn)ship_stop);
}
