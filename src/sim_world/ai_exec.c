/* sim_world part B — state executor (69fe), alert/radio state (14c3), per-frame flight update (5058).
 * sim_world_ai.md §1–§4; control flow checked against the disassembly (x86dis AP_flat.exe). */
#include "sim_world/swb_priv.h"

/* ---- RNG (sim_flow 1889 with the world state DS:4454) ------------------------------------------------- */
void (*swb_rng_hook)(u16 v, int range, s16 n);           /* developer trace (AOTP_TEST=ai), not game state */
u16 swb_rng(void)
{
    u16 v = ap_rng_next(0x4454);
    if (swb_rng_hook) swb_rng_hook(v, 0, 0);
    return v;
}
s16 swb_rng_range(s16 n)
{
    s16 v = rng_mod(0x4454, n);
    if (swb_rng_hook) swb_rng_hook((u16)v, 1, n);
    return v;
}

/* a far function of a state descriptor: slot 8 steer, 0Ch act, 10h think (called `call far [desc+slot]`
 * with the object pushed; returns AX) */
typedef u16 (*AiFn)(u16);
u16 swb_call_desc(u16 desc, u16 slot, u16 obj)
{
    FarPtr f = ds_far((u16)(desc + slot));
    if (far_is_null(f)) return 0;                         /* PORT: the original would call 0000:0000 */
    return ((AiFn)codeptr_lookup(f))(obj);
}

/* ==== 69fe — state executor ============================================================================ */

/* 69fe:0000 — sim_world_ai.md §2 */
void brain_module_init(void) {}

/* 69fe:0005 — deterministic timer jitter (not the RNG) */
u16 brain_jitter(void)
{
    DSW(0x488C) = (u16)(DSW(0x488C) + 13);
    return DSW(0x488C) & 15;
}

/* 69fe:0015 brain_set_state(AX = desc, BX = brain) — returns the scratch block */
void (*swb_state_hook)(u16 brain, u16 desc);             /* developer trace (AOTP_TEST=ai), not game state */
u16 brain_set_state(u16 bx, u16 ax)
{
    if (swb_state_hook) swb_state_hook(bx, ax);
    DSW(bx) = ax;
    s16 j = (s16)brain_jitter();
    DSSL(bx + 2) = DSSL(ax) + j;
    DSSL(bx + 6) = 0;
    DSW(bx + 0x0C) = 0;
    memset(DSP(DSW(bx + 0x0A)), 0, DSW(0x47F4));
    return DSW(bx + 0x0A);
}

/* 69fe:0073 — make p turn toward a point 3000 ahead of it */
void ai_break_toward(u16 p)
{
    u16 m = stk_mark(), pt = stk_alloc(12);
    memcpy(DSP(pt), DSP(p + 4), 12);
    lmove_heading(pt, OW(p, 0x14), 3000);
    release_ordnance(p, pt);                              /* 0deb:000a (sic: the spec's attack_run_start) */
    stk_release(m);
}

/* 69fe:00bb ai_call_help(AX = victim, BX = flight) */
void ai_call_help(u16 victim, u16 f)
{
    u16 m = stk_mark(), list = stk_alloc(12);
    s16 n = (s16)flight_alive_members(f, list);
    for (s16 i = 0; i < n; i++) {
        u16 q = DSW(list + 2 * i);
        if ((u32)dist2(q, victim) >= 20000) continue;     /* unsigned compare */
        if (!has_ordnance(q)) continue;
        if (q == PLAYER) {
            u16 rec = radio_rec(0x18);
            DSW(rec + 0x16) = victim;
            radio_queue7(0x55, rec, 0);
        } else {
            ai_break_toward(q);
            ai_think_target(q);
        }
    }
    stk_release(m);
}

/* 69fe:015c brain_threat_check(BX = plane) — §2.1 (checked in the disassembly) */
u16 brain_threat_check(u16 p)
{
    u16 d = DESC(p);
    u32 ag = alert_get(p);
    u16 src = (u16)ag;
    s16 lvl = (s16)(ag >> 16);
    u16 ret = 0;
    timer16_dec((u16)(p + 0xF9));
    if (timer16_dec((u16)(p + 0xFF)) == 0) OB(p, 0xF2) = 0;
    if (plane_is_down(p)) return 0;
    if (p != PLAYER) {
        timer32_expired((u16)(p + 0xE7));
        timer32_expired((u16)(p + 0xDF));
        if ((DFLAGS(d) & 0x10) && !(DFLAGS(d) & 0x200) && (OB(FLT(p), 0x3B) & 4)) {
            ai_think_target(p);
            return 1;
        }
        if (ground_avoid_check(p)) return 1;
    }
    if (lvl == 1) {
        if (DFLAGS(d) & 4) {
            OW(p, 0xD7) = src;
            set_state(p, D_PURSUE);
            ret = 1;
        }
        return ret;
    }
    if (lvl != 6) return 0;
    u16 dat = DAT(p);
    if (!((DFLAGS(d) & 2) || (p == PLAYER && turret_count(p) != 0) || (OB(p, 0x45) & 0x20))) return 0;
    u16 k = 0;
    u16 P = PIL(p);
    s16 cls = (s16)OC(dat, 0x38);
    if ((OB(p, 0x43) & 1) || ai_spot_reaction(p, src)) {
        OB(src, 0x1B) |= 4;
        if (cls == 0 && !has_ordnance(p)) {
            if ((s16)(OS(p, 0x6C) - OS(src, 0x6C)) < 200) {
                u16 ok = can_dogfight(p, src);
                if (DSS(P + 0x46) != 0 && ok) k = D_DOGFIGHT;
                else if (ok || OW(p, 0xD7) == 0) k = D_PURSUE;
            }
        } else if (cls == 3) {
            u16 t = OW(p, 0xD7);
            if (t == 0 || OW(t, 0xD7) != p) OW(p, 0xD7) = src;
            else if (OSL(p, 0xD3) > dist2(p, src)) OW(p, 0xD7) = src;
        } else {
            k = D_DEFEND;
            if (cls == 0) {
                u16 L = flight_leader(FLT(p));
                if (L != PLAYER) {
                    if (dist2(L, p) < 20000) {
                        if (dist2(L, OW(FLT(p), 0x10)) > 60000) ai_call_help(L, FLT(p));
                    } else if (dist2(p, OW(FLT(p), 0x10)) > 60000) {
                        ai_break_toward(p);
                    }
                }
            }
        }
    }
    if (k) {
        OW(p, 0xD7) = src;
        if (is_pure_fighter(p)) OB(p, 0x1C) |= 1;
        if (p != PLAYER) {
            set_state(p, k);
            ret = 1;
        }
    }
    return ret;
}

/* 69fe:04a0 brain_think(AX = obj, BX = brain) — §2.2 */
void brain_think(u16 obj, u16 brain)
{
    u16 d = DSW(brain);
    int force = 0;
    if (obj == PLAYER) player_threat_update(PLAYER);
    if (OB(obj, 0x18) == 1) {
        timer16_dec((u16)(obj + 0x78));
        timer16_dec((u16)(obj + 0x7A));
    }
    if ((DFLAGS(d) & 1) && plane_is_down(OW(obj, 0xD7))) force = 1;
    if (force || (DSL(d + 0x10) != 0 && DSL(brain + 2) == 0)) {
        swb_call_desc(d, 0x10, obj);
        u16 nd = DSW(brain);
        if (nd == d) DSSL(brain + 2) = DSSL(d) + (s16)brain_jitter();
        else d = nd;
    }
    if (OB(obj, 0x18) == 0 && brain_threat_check(obj)) {
        brain = (u16)(obj + 0x4A);
        d = DSW(brain);
    }
    if (!(DFLAGS(d) & 0x40)) timer32_expired((u16)(brain + 2));
    if (OB(obj, 0x18) == 0 && obj != PLAYER) controls_neutral((u16)(obj + 0xAC));
    if (DSL(d + 8) != 0 && swb_call_desc(d, 8, obj) != 0) DSSL(brain + 2) = 0;
    if (DSW(brain) == d) {
        DSB(brain + 0x0C) |= 1;
        DSSL(brain + 6)++;
    }
}

/* 69fe:05f1 brain_act(AX = obj, BX = brain) */
void brain_act(u16 obj, u16 brain)
{
    u16 d = DSW(brain);
    if (DSL(d + 0x0C) != 0) swb_call_desc(d, 0x0C, obj);
    if (DSW(brain) == d) DSB(brain + 0x0C) |= 2;
    alert_tick(obj);
}

/* ==== 14c3 — alert and radio state ======================================================================= */

/* 14c3:000b (near, `inc bp`) */
static u8 alert_pair(s16 hi, u16 cnt, s16 lo)
{
    s16 nw = (s16)DSC(0x5826);
    if (nw == hi) {
        DSB(cnt) = 2;
        u16 q = DSW(0x5824);
        if ((s16)OC(q, 0x2C) == lo) OB(q, 0x2C) = 0;
        return 1;
    }
    if (nw == lo && DSB(cnt) != 0) return 1;
    return 0;
}

/* 14c3:0054 alert_raise(p, lvl, ...) — levels 1, 5, 6, 7, 8: arg = source; 9: source and cooldown */
void alert_raise(u16 p, s16 lvl, u16 a2, ...)
{
    DSB(0x5826) = (u8)lvl;
    DSW(0x5824) = p;
    if (alert_pair(10, (u16)(p + 0x2E), 7) || alert_pair(12, (u16)(p + 0x30), 9) ||
        alert_pair(11, (u16)(p + 0x2F), 8))
        lvl = 0;
    if ((s16)OC(p, 0x2C) >= lvl) return;
    u8 cd = 2;
    OB(p, 0x2C) = (u8)lvl;
    switch ((s8)lvl) {
    case 1: case 5: case 6: case 7: case 8:
        OW(p, 0x32) = a2;
        break;
    case 9: {
        va_list ap;
        va_start(ap, a2);
        OW(p, 0x32) = a2;
        cd = (u8)va_arg(ap, unsigned);
        va_end(ap);
        break;
    }
    default: break;
    }
    OB(p, 0x2D) = cd;
}

/* 14c3:0130 (near) */
static s16 cnt_dec(u16 c)
{
    s8 v = (s8)(DSB(c) - 1);
    DSB(c) = (u8)v;
    if (v <= 0) { DSB(c) = 0; return -1; }
    return 0;
}

/* 14c3:0153 */
void alert_tick(u16 p)
{
    if (cnt_dec((u16)(p + 0x2D))) OB(p, 0x2C) = 0;
    cnt_dec((u16)(p + 0x2E));
    cnt_dec((u16)(p + 0x2F));
    cnt_dec((u16)(p + 0x30));
}

/* 14c3:0197 */
void alert_clear(u16 p) { memset(DSP(p + 0x2C), 0, 8); }

/* 14c3:01af — DX = level (i8), AX = source */
u32 alert_get(u16 p) { return (u32)(u16)(s16)OC(p, 0x2C) << 16 | OW(p, 0x32); }

/* ==== 5058 — per-frame flight update ======================================================================= */

/* 5058:0000 — (named flight_leader_first in the CSV) returns the i32 scramble radius 26000 */
u32 flight_leader_first(void) { return 0x6590; }

/* 5058:0010 — leader only, from plane_act */
void waypoint_advance(u16 p)
{
    u16 f = FLT(p);
    if (OW(f, 0x3E) != 0) return;
    u16 cur = (u16)(f + 0x16);
    if (DSW(cur + 2) == 0) return;
    u16 w = wp_current_pos(cur);
    if (!w) return;
    u8 near_player = FLT(p) == FLT(PLAYER);
    u8 hurry = DSB(0x3F04) && (OB(p, 0x1C) & 4) && !near_player;
    s32 radius = hurry ? 100 : OB(f, 2) == 6 ? 10000 : 5000;
    if ((u32)dist2v((u16)(p + 4), w) >= (u32)radius) return;
    if (near_player && (DSB(0x3F04) != 0 || DSB(0x069C) != 0)) return;
    wp_cursor_advance(cur, 1);
    if (DSB(0x477C) == 5 && !near_player) DSW(cur) = 0;
    if (hurry) OB(p, 0x1C) |= 2;
}

/* 5058:010b */
void ship_waypoint_advance(u16 s, u16 cur)
{
    u16 w = wp_current_pos(cur);
    if (w) {
        if ((u32)dist2v((u16)(s + 4), w) >= 500) return;
        if (wp_cursor_advance(cur, 1)) return;
    }
    set_state(s, D_SHIP_STOP);
}

/* 5058:0165 — §4.1 */
static void standby_scramble_check(u16 f)
{
    s32 R = (s32)flight_leader_first();
    u16 P = FLT(PLAYER);
    s8 side = (s8)OB(f, 0x42);
    u16 c = (u16)(DSW(OW(f, 8)) + 4);
    s16 n = OS(f, 6);
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
        u16 q = DSW(DSW(0x475B) + 2 * i);
        if (FLT(q) == f) continue;
        if (!((s16)OC(q, 0x26) + side == 1 || FLT(q) == P ||
              (OB(FLT(q), 2) == 6 && OW(FLT(q), 0x10) == f)))
            continue;
        if (OB(FLT(q), 0x3B) & 2) continue;
        u8 hit = 0;
        s32 d = dist2v((u16)(q + 4), c);
        if (OB(f, 0) == 1) {
            if (d < 350000) {
                for (s16 j = 0; j < n; j++) {
                    u16 m = DSW(OW(f, 8) + 2 * j);
                    if ((u32)dist2(q, m) < (u32)R) { hit = 1; break; }
                }
            }
        } else {
            hit = d < R;
        }
        if (!hit) continue;
        launch_intercept(f, (u16)(q + 4));
        for (s16 j = 0; j < OS(f, 0x44); j++) {
            u16 g = OW(f, 0x46 + 2 * j);
            u16 gp = (u16)(DSW(OW(g, 8)) + 4);
            if ((OB(f, 0x3B) & 2) && (u32)dist2v(gp, c) < 500000) launch_intercept(f, gp);   /* (sic: f) */
        }
        return;
    }
}

/* 5058:0329 */
static void flights_pre(void)
{
    u16 f = DSW(0x4753);
    for (s16 i = 0; i < (s16)DSW(0x476B); i++, f = (u16)(f + 0x56)) {
        for (s16 j = 0; j < OS(f, 6); j++) {
            u16 m = DSW(OW(f, 8) + 2 * j);
            OL(m, 0x42) &= OL(m, 0x46);
            OL(m, 0x46) = 0;
        }
        if (OB(f, 0x3B) & 2) standby_scramble_check(f);
    }
}

/* 5058:0393 */
static u16 flight_renumber(u16 f)
{
    for (s16 i = 0; i < OS(f, 6); i++) OB(DSW(OW(f, 8) + 2 * i), 0xBC) = (u8)i;
    return 0;
}

/* 5058:03bb */
static u8 ap_flight_active_5058(u16 f) { return (OB(f, 0x3B) & 2) ? 0 : 1; }

/* 5058:03d1 */
static void flights_think(void)
{
    for (s16 i = 0; i < (s16)DSW(0x476B); i++) {
        u16 f = DSW(DSW(0x4751) + 2 * i);
        if (!ap_flight_active_5058(f)) continue;
        s16 n = OS(f, 6);
        if (OC(f, 0) == 0) flight_renumber(f);
        if (OB(f, 0) == 1) brain_think(OW(f, 0x2E), (u16)(OW(f, 0x2E) + 0x4A));
        for (s16 j = 0; j < n; j++) {
            u16 m = DSW(OW(f, 8) + 2 * j);
            brain_think(m, (u16)(m + 0x4A));
        }
    }
    all_gunnery();
    u16 f = DSW(0x4753);
    for (s16 i = 0; i < (s16)DSW(0x476B); i++, f = (u16)(f + 0x56))
        if (OB(f, 0) == 0) flight_targets_update(f);
}

/* 5058:048e */
static void plane_act(u16 obj)
{
    u16 m = stk_mark(), old = stk_alloc(12);
    memcpy(DSP(old), DSP(obj + 4), 12);
    u16 f = FLT(obj);
    if (OB(obj, 0x18) == 0 && !(OB(obj, 0x43) & 8) && (OB(obj, 0x43) & 0xF0))
        plane_avoid_collision(obj);
    else
        brain_act(obj, (u16)(obj + 0x4A));
    clamp_world(obj);
    if (OB(obj, 0x18) == 0 && (OB(obj, 0x1D) & 0x10) && DSB(OW(obj, 0x16) + 1) != 0) {
        u8 v = (u8)(DSB(OW(obj, 0x16) + 1) - 1);
        DSB(OW(obj, 0x16) + 1) = v;
        if (v == 0) {
            OB(obj, 0xBA) &= 0xEF;
            OB(obj, 0x1D) &= 0xEF;
        }
    }
    memcpy(DSP(obj + 0x36), DSP(old), 12);
    OB(OW(obj, 0x20), 0x26) = 0;                          /* 365a:0082: the matrix cache is stale */
    if (OB(f, 0) == 0) {
        if (plane_in_formation(obj)) waypoint_advance(obj);
    } else if (OB(obj, 0x19) != 0 && OW(f, 0x18) != 0) {
        ship_waypoint_advance(obj, (u16)(f + 0x16));
    }
    stk_release(m);
}

/* 5058:05b9 */
static void flights_act(void)
{
    for (s16 i = 0; i < (s16)DSW(0x476B); i++) {
        u16 f = DSW(DSW(0x4751) + 2 * i);
        if (!ap_flight_active_5058(f)) continue;
        s16 n = OS(f, 6);
        if (OB(f, 0) == 1) plane_act(OW(f, 0x2E));
        for (s16 j = 0; j < n; j++) plane_act(DSW(OW(f, 8) + 2 * j));
    }
}

/* 5058:0630 — §1.2 */
void world_ai_update(void)
{
    flights_pre();
    flights_act();
    flights_think();
}

/* 5058:0641 */
void flights_nop(void) {}

/* 5058:0646 */
void objects_clamp_world(void)
{
    for (s16 i = 0; i < (s16)(DSW(0x4778) + DSW(0x4772)); i++) clamp_world(DSW(DSW(0x475D) + 2 * i));
}
