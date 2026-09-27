/* sim_world part B — combat and gunnery (1284), aiming (0ac2). sim_world_ai.md §7; checked against the
 * disassembly. Quirks kept: spray bursts (third word of the fire source 0) never test hits; the glare
 * test of the gunnery base is made from the target's own position. */
#include "sim_world/swb_priv.h"

/* ==== 0ac2 — aiming ====================================================================================== */

/* 0ac2:000e aim_solution(p, t) → &DS:5682 (§7.1) */
u16 aim_solution(u16 p, u16 t)
{
    u16 mk = stk_mark();
    u16 pos = stk_alloc(12), tmp = stk_alloc(6), lead = stk_alloc(12), ba = stk_alloc(6), la = stk_alloc(6),
        t1 = stk_alloc(12), t2 = stk_alloc(12), rel = stk_alloc(6), e = stk_alloc(6), cone = stk_alloc(8);
    u16 gl = OW(p, 0x5E);
    u16 st = OW(p, 0x60);
    u16 rec = (u16)(gl + 4);
    u16 in_range = 0;
    s16 fixed = 0;
    u16 noammo_ok = !(OB(p, 0x1B) & 0x80);
    if (DSW(gl + 2) & 0x2800) fixed++;
    if (DSW(gl + 2) & 0x0500) fixed++;
    s16 first_fixed = fixed;
    s16 turret = (s16)(1 - fixed);
    for (s16 i = 0; DSS(gl) > i; i++, turret++, rec = (u16)(rec + 0x14), st = (u16)(st + 0x0C)) {
        s16 cur = fixed;
        if (fixed) fixed--;
        if (cur != 0 && (cur != first_fixed || p == PLAYER)) continue;
        u16 loaded = cur ? noammo_ok : DSW(st);
        if (!loaded) continue;
        memcpy(DSP(pos), DSP(p + 4), 12);
        u16 M = obj_matrix(OW(p, 0x20));
        if (cur == 0) {
            vec_rotate((u16)(rec + 6), M, tmp);
            ladd_v(pos, tmp);
        }
        s32 d = dist3v(pos, (u16)(t + 4));
        if (d >= 15000) continue;
        s16 v = (s16)gun_type_speed(rec);
        in_range = 1;
        for (int k = 0; k < 3; k++)
            DSSL(lead + 4 * k) = ldiv_sat((s32)(s16)d * OS(t, 0x70 + 2 * k), v);
        ladd(lead, (u16)(t + 4));
        body_angles(ba, lead, pos, OW(p, 0x20));
        DSL(0x568A) = angles_to(la, lead, pos);
        DSW(0x5686) = (u16)(DSW(la) - OW(p, 0x10));
        DSW(0x5688) = (u16)(DSW(la + 4) - OW(p, 0x14));
        DSW(0x5682) = cur ? 0 : (u16)turret;
        ladd3_v(pos, (u16)(p + 0x70), t1);
        ladd3_v(lead, (u16)(t + 0x70), t2);
        angles_to(rel, t2, t1);
        vsub(rel, (u16)(p + 0x10));
        OW(p, 0xFD) = DSW(rel);
        OW(p, 0xFB) = DSW(rel + 4);
        angles_to(e, (u16)(t + 4), pos);
        vsub(e, la);
        vsar(e, 4);
        DSB(0x5684) = (u16)(DSW(e) + 0x7E) < 0xFC ? DSB(e) : 0x80;
        DSB(0x5685) = (u16)(DSW(e + 4) + 0x7E) < 0xFC ? DSB(e + 4) : 0x80;
        DSW(cone) = (u16)(DSW(rec + 0x0C) - DSW(rec + 0x10));
        DSW(cone + 2) = (u16)(DSW(rec + 0x0C) + DSW(rec + 0x10));
        DSW(cone + 4) = (u16)(DSW(rec + 0x0E) - DSW(rec + 0x12));
        DSW(cone + 6) = (u16)(DSW(rec + 0x0E) + DSW(rec + 0x12));
        if (in_cone(ba, cone)) { stk_release(mk); return 0x5682; }
    }
    if (!in_range) {
        DSL(0x568A) = (u32)dist2(p, t);
        DSW(0x5686) = (u16)(bearing32(OSL(p, 0x0C) - OSL(t, 0x0C), DSSL(0x568A)) - OW(p, 0x10));
        DSW(0x5688) = (u16)(bearing32(OSL(t, 4) - OSL(p, 4), OSL(t, 8) - OSL(p, 8)) - OW(p, 0x14));
        DSB(0x5684) = DSB(0x5685) = 0x80;
        OW(p, 0xFD) = DSW(0x5686);
        OW(p, 0xFB) = DSW(0x5688);
    }
    DSW(0x5682) = 0xFFFF;
    stk_release(mk);
    return 0x5682;
}

/* ==== 1284 =============================================================================================== */

/* 1284:0001 */
void combat_init(void) {}

/* 1284:0006 */
void ai_was_hit(u16 p)
{
    if (OB(p, 0x18) != 0) return;
    if (OW(p, 0xFF) == 0) OW(p, 0xFF) = 2000;
    s16 c = (s16)OC(p, 0xF2);
    OB(p, 0xF2)++;
    if (c >= PW(p, 0x50)) {
        OB(p, 0x44) |= 2;
        OB(p, 0x48) |= 2;
    }
}

/* 1284:004d on_tail(p, t, ang) — p behind t, both pointing the same way */
u16 on_tail(u16 p, u16 t, u16 ang)
{
    s16 dp = (s16)(DSW(ang) - OW(p, 0x10));
    s16 dh = (s16)(DSW(ang + 4) - OW(p, 0x14));
    s32 d = dist3(p, t);
    if (d >= 32000) return 0;
    if ((u16)((u16)d + 0xFED4) >= 0x1A2C) return 0;
    if ((u16)(dp + 0x0AF0) >= 0x15E0) return 0;
    if ((u16)(dh + 0x0FA0) >= 0x1F40) return 0;
    dp = (s16)(DSW(ang) - OW(t, 0x10));
    dh = (s16)(DSW(ang + 4) - OW(t, 0x14));
    if ((u16)(dp + 0x0AF0) >= 0x15E0) return 0;
    if ((u16)(dh + 0x0FA0) >= 0x1F40) return 0;
    return 1;
}

/* 1284:00f4 nearest_in_list(pos, side, &d) — the planes hostile to that side (DS:5CCC / 5CD0) */
u16 nearest_in_list(u16 pos, u16 side, u16 d_ds)
{
    s32 best = 0x7FFFFFFF;
    u16 r = 0, l;
    s16 n;
    if (side == 0) { l = DSW(0x5CCC); n = DSS(0x5CD4); }
    else { l = DSW(0x5CD0); n = DSS(0x5CD8); }
    while (--n >= 0) {
        u16 q = DSW(l);
        l = (u16)(l + 2);
        if (plane_is_down(q)) continue;
        s32 d = dist2v(pos, (u16)(q + 4));
        if (d < best) { best = d; r = q; }
    }
    if (d_ds) DSSL(d_ds) = best;
    return r;
}

/* 1284:0184 */
u16 nearest_enemy(u16 p, u16 d_ds) { return nearest_in_list((u16)(p + 4), (u16)(s16)OC(p, 0x26), d_ds); }
u16 ap_nearest_enemy_1284(u16 p, u16 d_ds) { return nearest_enemy(p, d_ds); }

/* 1284:01a3 */
void player_threat_update(u16 pl)
{
    u16 mk = stk_mark(), d = stk_alloc(4), a = stk_alloc(6);
    u16 e = nearest_enemy(pl, d);
    if (e && DSSL(d) < 20000) {
        u16 set = 0;
        if (turret_count(pl) == 0 && is_pure_fighter(pl)) {
            OW(pl, 0xD7) = e;
            set = 1;
            if (can_dogfight(e, pl)) alert_raise(e, 3, pl);
        }
        angles_to(a, (u16)(e + 4), (u16)(pl + 4));
        if (on_tail(pl, e, a)) {
            alert_victim(pl, e);
            if (OB(PLAYER, 0x44) & 1) {
                OB(e, 0x43) |= 2;
                OB(e, 0x47) |= 2;
            }
        } else if (set) {
            alert_raise(e, 1, pl);
        }
    }
    stk_release(mk);
}

/* 1284:0271 shot_quality(aim, base, thr) → 0 none, 1 a real shot, 2 a harmless spray (RNG) */
u16 shot_quality(u16 aim, s16 base, s16 thr)
{
    s16 e = (s16)ldist2v((u32)abs16((s16)DSC(aim + 2)), (u32)abs16((s16)DSC(aim + 3)));
    s32 r = DSSL(aim + 8);
    if (e >= 0x100 || r >= 6000) return 0;
    s16 v = (s16)(base - (s16)(e << 2) - (s16)(r >> 4));
    if (v < thr) return 0;
    return (s16)(RNG() & 0xFF) < v ? 1 : 2;                                      /* RNG */
}

/* 1284:02fb — once per frame */
u16 gun_solution(u16 p)
{
    if (!(OB(p, 0x1A) & 0x10)) {
        u16 old = OW(p, 0xCB);
        u16 a = aim_solution(p, OW(p, 0xD7));
        memcpy(DSP(p + 0xCB), DSP(a), 12);
        OB(p, 0x1A) |= 0x10;
        if (OW(p, 0xCB) != old) {
            OW(p, 0x101) = 0;
            OB(p, 0x1F) |= 0x20;
        }
    }
    return (u16)(p + 0xCB);
}

/* 1284:0353 ai_fire(p, sel, ang, range, spray) */
void ai_fire(u16 p, u16 sel, u16 ang, s32 range, u16 spray)
{
    u16 mk = stk_mark(), src = stk_alloc(6);
    s16 trig;
    OB(p, 0x42) |= 0x20;
    OB(p, 0x46) |= 0x20;
    DSW(src) = p;
    DSW(src + 2) = 1;
    DSW(src + 4) = spray ? 0 : 50;              /* → bullet +2A: 0 = the burst never tests for hits */
    if (sel == 0) {
        s16 r = (s16)(RNG() & 0x3FF) > PW(p, 8);                                   /* RNG */
        trig = spray == (u16)r ? 3 : 1;
    } else {
        trig = (s16)(sel + 3);
        if (OB(p, 0x1F) & 0x20) {
            if (OW(p, 0x101) != 0) { stk_release(mk); return; }
            OW(p, 0x101) = (u16)((RNG() & 0x3FF) + 0x5DC);                         /* RNG */
            OB(p, 0x1F) &= 0xDF;
        }
    }
    if ((u8)gun_fire(trig, p, ang, src) && trig < 4 && !fixed_guns_loaded(p)) OB(p, 0x1B) |= 0x80;
    stk_release(mk);
}

/* 1284:041c */
u16 turret_count(u16 p) { return DSW(OW(p, 0x5E) + 2) & 0xFF; }

/* 1284:0435 all_gunnery — §7.3 */
void all_gunnery(void)
{
    if (DSB(0x477C) == 5) return;
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
        u16 p = DSW(DSW(0x475B) + 2 * i);
        OB(p, 0x1A) &= 0xEF;
        if (p == PLAYER && turret_count(p) == 0) continue;
        if (plane_is_down(p)) continue;
        u16 t = OW(p, 0xD7);
        if (!t) continue;
        u16 aim = gun_solution(p);
        if (timer16_dec((u16)(p + 0x101)) == 0 && !(OB(p, 0x1F) & 0x20)) {
            OW(p, 0x101) = (u16)((RNG() & 0xFFF) + 5000);                          /* RNG */
            OB(p, 0x1F) |= 0x20;
        }
        if (DSS(aim) <= -1) continue;
        if (DSB(aim + 2) == 0x80) continue;
        u16 P = PIL(p);
        s16 base, thr;
        if (DSW(aim) != 0) {
            u16 b = CLS(p) == 3;
            base = DSS(0x1646 + 2 * b);
            thr = DSS(0x164A + 2 * b);
            if (t == PLAYER && DSC(0x42D2) == 0) { base -= 100; thr -= 100; }
        } else {
            thr = DSS(P + 0x12);
            base = DSS(P + 8);
            if (OB(p, 0x43) & 4) base = (s16)(((s32)base * 600) >> 10);
            if (t == PLAYER && glare_blocks(t, (u16)(PLAYER + 4))) base = (s16)(((s32)base * 500) >> 10);
        }
        u16 q = shot_quality(aim, base, thr);
        if (q == 0) continue;
        u16 mk = stk_mark(), ang = stk_alloc(6);
        u16 spray = 0;
        DSW(ang) = DSW(aim + 4);
        DSW(ang + 2) = 0;
        DSW(ang + 4) = DSW(aim + 6);
        if (q == 2) {
            s16 a = (s16)(RNG() & 0x7F);                                           /* RNG */
            s16 b = (s16)(RNG() & 0x7F);                                           /* RNG */
            DSW(ang) = (u16)(DSW(ang) + (DSS(aim + 4) >= 0 ? -a : a));
            DSW(ang + 4) = (u16)(DSW(ang + 4) + (DSS(aim + 6) >= 0 ? -b : b));
            spray = 1;
        }
        vadd(ang, (u16)(p + 0x10));
        OB(t, 0x43) |= 2;
        OB(t, 0x47) |= 2;
        if (timer16_dec(0x5820) == 0 && t == PLAYER && DSC(0x42D1) == 0) {
            DSW(0x5820) = 6000;
            msg_show(str(1, 0x46), 1, far_make(0, 0));
        }
        if (!is_pure_fighter(p) || fixed_guns_loaded(p)) {
            ai_fire(p, DSW(aim), ang, DSSL(aim + 8), spray);
            if (p == PLAYER && !(OB(PLAYER, 0x1E) & 0x20) && station_first((u16)(PLAYER + 0x5E)) == 0) {
                OB(PLAYER, 0x1E) |= 0x20;
                FarPtr sp = str(7, 0x27);
                msg_show(str(1, 0x39), 3, sp);
            }
            if (is_pure_fighter(p) && !fixed_guns_loaded(p) && FLT(p) == FLT(PLAYER) &&
                plane_in_formation(PLAYER)) {
                u16 rec = radio_rec(0x18);
                DSW(rec + 0x16) = p;
                radio_queue7(0x53, rec, 0);
            }
        }
        stk_release(mk);
    }
}

/* 1284:078b */
void combat_end(void) {}
