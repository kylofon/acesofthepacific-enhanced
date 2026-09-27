/* sim_world part B — waypoints (17fe), flight helpers (69d0), spawning of standby flights (51fb),
 * midpoints (6dec), the landing signal officer (6dfc). sim_world_ai.md §13, §14; ported from the
 * disassembly. Corrections to the spec: 17fe:000b kind 4 also reads DS:0000 (jump table); 69d0:0000 is
 * a table lookup DS:4601[i]; launch_intercept keeps the heading of the LAST try, not of the best point;
 * lso_signal's heading limit is 3000h. */
#include "sim_world/swb_priv.h"

/* ==== 17fe — waypoints ==================================================================================== */

/* 17fe:000b waypoint_pos(wp) — kind 0: the stored point; 1: the flight's first member; 2, 3, 5: the object;
 * 4 and > 5: the object pointer is 0 (quirk: reads DS:0000). Copied with the waypoint altitude into the next
 * of 4 rotating buffers. */
u16 waypoint_pos(u16 wp)
{
    if (DSB(wp) == 0) return (u16)(wp + 2);
    u16 o = DSW(wp + 2);
    u16 src = 0;
    switch ((s8)DSB(wp)) {
    case 1: src = (u16)(DSW(OW(o, 8)) + 4); break;
    case 2: case 3: case 5: src = (u16)(o + 4); break;
    default: break;                                        /* 4, > 5: src = 0 (quirk) */
    }
    if (DSW(0x5C24) == 0) module_flags(1);
    u16 b = DSW(0x5C1A + 2 * DSW(0x5C22));
    DSW(0x5C22) = (u16)((DSW(0x5C22) + 1) & 3);
    DSL(b) = DSL(src);
    DSL(b + 4) = DSL(src + 4);
    DSL(b + 8) = DSL(wp + 4);
    return b;
}

/* 17fe:00a1 wp_cursor_get(cur, i) */
u16 wp_cursor_get(u16 cur, u16 i)
{
    s16 j = (s16)(DSW(cur) + i);
    if (j < 0 || DSS(cur + 2) <= j) return 0;
    return DSW(DSW(cur + 4) + 2 * j);
}

/* 17fe:00c9 */
u16 wp_cursor_pos(u16 cur, u16 i)
{
    u16 w = wp_cursor_get(cur, i);
    return w ? waypoint_pos(w) : 0;
}

/* 17fe:00ed */
u16 wp_current_pos(u16 cur) { return wp_cursor_pos(cur, 0); }

/* 17fe:0110 wp_cursor_advance(cur, i) */
u16 wp_cursor_advance(u16 cur, u16 i)
{
    s16 j = (s16)(DSW(cur) + i);
    if (j >= 0 && DSS(cur + 2) > j) {
        DSW(cur) = (u16)j;
        return 1;
    }
    DSW(cur) = DSW(cur + 2);
    return 0;
}

/* 17fe:0137 */
void wp_flag_set(void) { DSW(0x5C24) = 1; }

/* 17fe:0142 */
void wp_buffers_init(void)
{
    memset(DSP(0x5BEA), 0, 0x3C);
    fill_seq(0x5C1A, 0x5BEA, 12, 4);
}

/* 17fe:016b */
void wp_init(void) { wp_buffers_init(); }

/* ==== 51fb — spawning standby flights (§13.1) ================================================================ */

/* 51fb:0000 formation_points(out, n, c, form, ang) */
static void formation_points(u16 out, s16 n, u16 c, u16 form, u16 ang)
{
    u16 mk = stk_mark(), M = stk_alloc(0x14), v = stk_alloc(6);
    memcpy(DSP(out), DSP(c), 12);
    out = (u16)(out + 12);
    mat_from_angles(ang, M);
    for (s16 i = 1; i < n; i++, out = (u16)(out + 12)) {
        vec_rotate(formation_offset(form, (u16)i), M, v);
        ladd3_v(c, v, out);
    }
    stk_release(mk);
}

/* 51fb:006e spawn_intercept_point(out, tgt, d) → spawn heading (RNG ×3–4) */
static u16 spawn_intercept_point(u16 out, u16 tgt, s32 d)
{
    u16 r = RNG();                                                                   /* RNG */
    s32 hi = DSSL(tgt + 8) + 2000, lo;
    if (hi <= 10000) {
        lo = hi = 10000;
    } else {
        lo = (u16)(r + 24000) < 48000 ? DSSL(tgt + 8) - 2000 : DSSL(tgt + 8) + 1000;
        if (lo < 10000) lo = 10000;
    }
    memcpy(DSP(out), DSP(tgt), 12);
    DSSL(out + 8) = lo + RNG_RANGE((s16)((u16)hi - (u16)lo));                        /* RNG */
    point_ahead(out, (u16)(OW(PLAYER, 0x14) + r), (u32)d);
    point_ahead(out, OW(PLAYER, 0x14), 4000);
    if ((u16)(r + 6000) < 12000) return RNG();                                       /* RNG */
    u16 b = bearing_between(tgt, out);
    return (u16)(b + RNG_RANGE(12000) - 6000);                                       /* RNG */
}

/* 51fb:0199 launch_intercept(f, tgt) — a standby flight becomes active; mode 2 spawns it near the target */
void launch_intercept(u16 f, u16 tgt)
{
    u16 mk = stk_mark(), best = stk_alloc(12), pt = stk_alloc(12), ang = stk_alloc(6), pts = stk_alloc(0x30);
    memcpy(DSP(best), DSP(DSW(0x3714)), 12);
    s32 bestd = 0;
    u8 mode = OB(f, 0) == 1 ? 1 : FB(ds_far((u16)(f + 0x52)), 0x1E);
    OB(f, 0x3B) &= 0xFD;
    if (mode == 2) {
        s16 tries = 20;
        u16 h = 0;
        do {
            h = spawn_intercept_point(pt, tgt, 22000);
            s32 mind = 1000000000;
            for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
                u16 q = DSW(DSW(0x475B) + 2 * i);
                if (FLT(q) == f) continue;
                s32 d = dist3v((u16)(q + 4), pt);
                if (d < mind) mind = d;
            }
            if (mind > bestd) {
                bestd = mind;
                memcpy(DSP(best), DSP(pt), 12);
            }
        } while (bestd <= 12000 && tries-- != 0);
        memcpy(DSP(ang), DSP(DSW(0x3716)), 6);
        DSW(ang + 4) = h;                                  /* (sic: the heading of the last try) */
        formation_points(pts, OS(f, 6), best, (u16)(s16)OC(f, 1), ang);
        for (s16 i = 0; OS(f, 6) > i; i++) path_enter(DSW(OW(f, 8) + 2 * i), (u16)(pts + 12 * i), ang, 0);
    }
    stk_release(mk);
}

/* 51fb:0306 */
void spawn_nop(void) {}

/* ==== 69d0 — flight helpers ================================================================================ */

/* 69d0:0000 — (CSV "helpers_nop") DS:4601[i] */
u16 helpers_nop(u16 i) { return DSW(0x4601 + 2 * i); }

/* 69d0:000e */
u16 ship_shape_of_type(u16 t) { return DSW(0x451B + 2 * t); }

/* 69d0:001c effect_frames_sum(a, b) — Σ DS:647C[i].+4 for i = a..b */
u16 effect_frames_sum(u16 a, u16 b)
{
    u16 s = 0;
    u16 q = (u16)(DSW(0x647C) + 12 * a + 4);
    for (s16 i = (s16)a; i <= (s16)b; i++, q = (u16)(q + 12)) s = (u16)(s + DSW(q));
    return s;
}

/* 69d0:0049 */
void weather_refresh(void)
{
    if ((s16)DSC(0x42C9) != DSS(0x4743)) wind_apply();
}

/* 69d0:005d */
static u16 flight_first_wp_pos(u16 f) { return OW(f, 0x0A) ? waypoint_pos(OW(f, 0x0A)) : 0; }

/* 69d0:007d flight_home(f) */
u16 flight_home(u16 f)
{
    if (OB(f, 3) != 0) {                                   /* carrier start */
        u16 b = OW(f, 4);
        return b ? (u16)(b + 4) : flight_first_wp_pos(f);
    }
    u16 w = flight_first_wp_pos(f);
    return w ? w : (u16)(OW(f, 4) + 4);
}

/* 69d0:00c4 message_text(i) → far DS:474B:(DS:4749 + DS:4745[i]) */
u32 message_text(u16 i)
{
    FarPtr t = ds_far(0x4745);
    return (u32)DSW(0x474B) << 16 | (u16)(DSW(0x4749) + FW(t, 2 * i));
}

/* 69d0:00de */
u16 flight_wp_pos(u16 f) { return wp_current_pos((u16)(f + 0x16)); }

/* 69d0:00f2 waypoint_message(f) */
u32 waypoint_message(u16 f)
{
    u16 w = wp_cursor_get((u16)(f + 0x16), 0);
    if (w && DSC(w + 1) >= 0) return message_text((u16)(s16)DSC(w + 1));
    return 0;
}

/* 69d0:0129 engine_power_of(p) */
u16 engine_power_of(u16 p)
{
    s16 e = (s16)((DSW(OW(p, 0x28)) & 0x0F00) >> 8);
    s16 v = (e == 2 && p == PLAYER) ? (s16)((s16)(OW(p, 0xA4) + OW(p, 0xA6)) >> 1) : OS(p, 0xA4);
    return (v < 0 || v > 0x400) ? 0 : (u16)v;
}

/* 69d0:0174 range_left(p) (i32) */
u32 range_left(u16 p)
{
    if (p == PLAYER && DSB(0x42CE) == 0) return 1000000000;
    u16 dmg = OW(p, 0x28), st = OW(p, 0x2A);
    s16 v = (s16)(((s32)(s16)fm_cruise_speed(OW(p, 0x66), 10) * DSS(0x3F22)) >> 10);
    s32 f = 0;
    if (DSSL(st + 0x10) != 0) {
        f = DSSL(st + 0x10) / (s32)DSS(dmg + 0x0C);        /* ldiv (0000:3c6a) */
        if (f < 0) f = 0;
    }
    if (DSSL(st + 0x0C) != 0) {
        s32 g = DSSL(st + 0x0C) / (s32)DSS(dmg + 0x0A);
        if (g > 0) f += g;
    }
    s32 k = ((s32)v * (s16)engine_power_of(p) + 0x3FF) >> 10;
    return (u32)k * (u32)f;                                /* imul, low 32 bits */
}

/* 69d0:0285 can_reach_home(p) */
u16 can_reach_home(u16 p)
{
    s32 d = (s32)ldist2((u16)(p + 4), flight_home(FLT(p)));
    return (s32)range_left(p) + 20000 > d;
}

/* ==== 6dec — midpoints ===================================================================================== */

/* 6dec:0000 midpoint(a, b) → &DS:522A */
u16 midpoint(u16 a, u16 b)
{
    for (int k = 0; k < 3; k++) DSSL(0x522A + 4 * k) = (s32)(DSL(a + 4 * k) + DSL(b + 4 * k)) >> 1;   /* 32-bit wrap */
    return 0x522A;
}

/* 6dec:003d bounding_centre(p, n) */
u16 bounding_centre(u16 p, u16 n)
{
    s32 lo[3], hi[3];
    for (int k = 0; k < 3; k++) lo[k] = hi[k] = DSSL(p + 4 * k);
    for (s16 c = (s16)n; --c > 0;) {
        p = (u16)(p + 12);
        for (int k = 0; k < 3; k++) {
            s32 v = DSSL(p + 4 * k);
            if (v < lo[k]) lo[k] = v;
            else if (v > hi[k]) hi[k] = v;
        }
    }
    u16 mk = stk_mark(), a = stk_alloc(12), b = stk_alloc(12);
    for (int k = 0; k < 3; k++) { DSSL(a + 4 * k) = lo[k]; DSSL(b + 4 * k) = hi[k]; }
    u16 r = midpoint(a, b);
    stk_release(mk);
    return r;
}

/* ==== 6dfc — landing signal officer (§14) ====================================================================== */

/* 6dfc:0000 lso_signal(p, c) → DS:523B */
static s16 lso_signal(u16 p, u16 c)
{
    u16 SD = SDAT(c);
    u16 mk = stk_mark(), rel = stk_alloc(12), tmp = stk_alloc(12);
    s16 r;
    u16 L = 0, R = 0, low;
    lvec_rotate(lsub3((u16)(p + 4), (u16)(c + 4), tmp), obj_matrix_t(OW(c, 0x20)), rel);
    s32 x = DSSL(rel), y = DSSL(rel + 4), z = DSSL(rel + 8);
    s32 h = z - DSS(SD + 4) - DSS(OW(p, 0x66) + 0x98);
    s16 st = DSS(SD + 0x66), len = DSS(SD + 0x68);
    if ((s32)(s16)(st - 25000) > y || (s32)(s16)(st + len) < y || labs32(x) > labs32(y) ||
        labs32((s32)OS(p, 0x14) - OS(c, 0x14)) > 0x3000) { r = -1; goto out; }
    if (y > -9000 && !((OB(PLAYER, 0xBA) >> 4) & 1)) { r = 0; goto out; }
    if (labs32(x) >= 0x4B) {
        s16 q = (s16)(labs32(y) / (labs32(x) - 0x4A));
        if (q > 1000) L = R = 0;
        else if (q > 15) L = R = 1;
        else { r = 0; goto out; }
        if (x > 0) R = 0;                                  /* bp-2 (→ 7) */
        else L = 0;                                        /* bp-4 (→ 4) */
    }
    if ((s32)st < y && (s32)(s16)(st + len) > y && labs32(x) < (s32)(s16)(DSS(SD + 0x6A) / 2) && h < 20) {
        r = -2;
        goto out;
    }
    if (h <= 0) {
        low = 1;
    } else {
        s16 q = (s16)(((s32)(s16)(st + DSS(SD + 0x68) / 2) - y) / h);
        if (q > 29) low = 1;
        else if (q > 3) low = 0;
        else { r = 0; goto out; }
    }
    {
        u16 stall = (OB(p, 0xAA) >> 1) & 1;
        s16 v = OS(p, 0x94) < 1200 ? 0 : OS(p, 0x94) < 1500 ? 1 : 2;
        if (v == 2) r = 1;
        else if (stall) r = 2;
        else if (low == 1) r = 3;
        else if (R == 1) r = 7;
        else if (L == 1) r = 4;
        else if (v == 1) r = 1;
        else if ((s32)(s16)(st - 400) < y && (s32)(s16)(st + len) > y) r = 6;
        else r = 5;
    }
out:
    DSW(0x523B) = (u16)r;
    stk_release(mk);
    return r;
}

/* 6dfc:02e5 lso_frame */
void lso_frame(void)
{
    if (DSW(0x5236) != 0) {
        if (timer16_dec(0x5236) == 0) {
            DSW(0x5236) = 0;
            OB(PLAYER, 0x1C) &= 0xF7;
            DSW(0x523B) = 0xFFFF;
        }
    } else if (DSW(0x5239) != 0) {
        DSW(0x523B) = 0xFFFF;
        if (timer16_dec(0x5239) == 0) msg_show(str(1, 0x35), 2, far_make(0, 0));
    } else if (OB(FLT(PLAYER), 3) != 0) {
        if (OB(PLAYER, 0x1C) & 0x10) {
            OW(PLAYER, 0xB2) = 0;
            OB(PLAYER, 0xBA) |= 8;
        } else if (OB(PLAYER, 0x1C) & 8) {
            u16 c = OW(FLT(PLAYER), 4);
            if ((u32)dist3(PLAYER, c) < 60000) {
                lso_signal(PLAYER, c);
                if (DSW(0x523B) == 0) DSW(0x5236) = 0xF00;
                else if (DSS(0x523B) == -2) {
                    OB(PLAYER, 0xBA) |= 8;
                    OB(PLAYER, 0x1C) |= 0x10;
                }
            } else {
                OB(PLAYER, 0x1C) &= 0xF7;
                DSW(0x523B) = 0xFFFF;
            }
        }
    }
    DSW(0x57E8) = DSW(0x523B);
}

/* 6dfc:0400 lso_request */
void lso_request(void)
{
    OB(PLAYER, 0x1C) |= 8;
    DSW(0x5239) = 0x500;
}

/* 6dfc:0413 lso_can_request */
u16 lso_can_request(void)
{
    u16 c = OW(FLT(PLAYER), 4);
    if (DSW(0x5236) != 0) return 0;
    if (OB(PLAYER, 0x1C) & 8) return 0;
    if (OB(FLT(PLAYER), 3) == 0) return 0;
    if (OB(c, 0x1B) & 2) return 0;
    if ((s32)(s16)(OW(PLAYER, 0xB8) + DSW(OW(PLAYER, 0x66) + 0x98)) >= OSL(PLAYER, 0x0C)) return 0;
    return (u32)dist3(PLAYER, c) < 60000;
}


