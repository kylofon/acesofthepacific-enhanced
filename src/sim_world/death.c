/* sim_world part A — parachutes and debris (0916), wrecks and the player's death (10bf).
 * sim_world_dmg.md; control flow from the disassembly (x86dis AP_flat.exe). */
#include "sim_world/swa_priv.h"

static FarPtr fp32(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static s16 sgn16(s16 v) { return v == 0 ? 0 : v < 0 ? -1 : 1; }

/* ==== 0916: parachutes and debris ================================================================ */

/* AP 0916:000d */
static void effect_set_anim(u16 slot, s16 r)
{
    shape_set_radius((s16)slot, r);
    shape_set_class((s16)slot, 0x0D);
}

/* AP 0916:002e — parachute sprites and the two parachute animations (effect slots 22h / 23h) */
void para_load(void)
{
    u16 set = sprite_count();
    sprite_set_load2(1, res_name(0x1416, 0x05B4));
    bmd_load((s16)(DSW(0x474D) + 0x23), (s16)set, res_name(0x141E, 0x05EE));
    effect_set_anim((u16)(DSW(0x474D) + 0x23), 0x64);
    bmd_load((s16)(DSW(0x474D) + 0x22), (s16)set, res_name(0x1426, 0x05EE));
    effect_set_anim((u16)(DSW(0x474D) + 0x22), 0x64);
}

u16 parachute_update(FarPtr e);
u16 debris_update(FarPtr e);

/* AP 0916:00b3 — a crewman bails out (not over a carrier-type group: [obj+34]+2 == 2) */
void parachute_spawn(u16 p)
{
    u16 mk = stk_mark(), r = stk_alloc(0x30);
    memmove(DSP(r), DSP(0x13B6), 0x30);
    if (DSB((u16)(OW(p, 0x34) + 2)) != 2) {
        DSW(r + 2) = (u16)(DSW(0x474D) + (OB(p, 0x26) == 0 ? 0x22 : 0x23));
        memmove(DSP(r + 4), DSP(p + 4), 12);
        FarPtr e = fp32(fx_spawn(r));
        if (!far_is_null(e)) {
            FarPtr v = far_add(e, 0x20);
            FW(v, 2) = OW(p, 0x70);
            FW(v, 4) = OW(p, 0x72);
            FW(v, 6) = OW(p, 0x74);
            FW(v, 6) = 8;
            FW(v, 8) = 0xFFEC;
            FW(e, 0x16) = 0;
            FW(e, 0x14) = OW(p, 0x14);
            FW(e, 0x1A) = SEG(0x0916);
            FW(e, 0x18) = 0x0173;
        }
    }
    stk_release(mk);
}

/* AP 0916:0173 — effect callback: rise damping, then the canopy opens (frames 0..5) and drifts down */
u16 parachute_update(FarPtr e)
{
    FarPtr v = far_add(e, 0x20);
    if (FSL(e, 0x0C) < 1000) return 0;
    if (FS(v, 6) > 0) FS(v, 6) >>= 3;
    else if (FS(v, 6) > -200 && !(FB(v, 0) & 1)) {
        FW(v, 6) = (u16)(FW(v, 6) + FW(v, 8));
        if (FS(v, 8) < 0x3C) FW(v, 8) = (u16)(FW(v, 8) - 0x14);
    } else if (FB(e, 0x16) < 5) {
        FB(v, 0) |= 1;
        FB(e, 0x16)++;
        if (FB(e, 0x16) == 4) FS(v, 6) >>= 1;
        if (FB(e, 0x16) == 5) FW(v, 6) = 0xFFF6;
    }
    /* PORT: the original's `cdq; xor ax,dx; sub ax,dx` takes the sign from the stale high word of EAX
     * (set by the caller); the intended |v| is used */
    if (abs16(FS(v, 2)) > 10) FS(v, 2) >>= 2;
    if (abs16(FS(v, 4)) > 10) FS(v, 4) >>= 2;
    FSL(e, 4) += FS(v, 2);
    FSL(e, 8) += FS(v, 4);
    FSL(e, 0x0C) += FS(v, 6);
    return 1;
}

/* AP 0916:0273 — draw (object kind 0Fh): the parachute sprite at the effect position */
void parachute_draw(FarPtr e)
{
    u16 mk = stk_mark(), r = stk_alloc(0x18);
    DSB(r + 0x17) = FB(e, 0x16);
    memset(DSP(r), 0, 0x16);
    DSW(r) = (u16)(DSW(0x474D) + 0x21);
    memmove(DSP(r + 2), far_mp(far_add(e, 4)), 12);
    DSW(r + 0x0E) = 0xC000;
    DSW(r + 0x14) = (u16)(r + 0x17);
    ts_draw(r);
    stk_release(mk);
}

/* AP 0916:02cc — a broken-off part flies from zone point `zone` (plane frame) */
void debris_spawn(u16 p, u16 zone)
{
    u16 mk = stk_mark(), r = stk_alloc(0x30), w = stk_alloc(6), v = stk_alloc(6), off = stk_alloc(6);
    memmove(DSP(r), DSP(0x13E6), 0x30);
    DSW(w) = DSW(0x13B0); DSW(w + 2) = DSW(0x13B2); DSW(w + 4) = DSW(0x13B4);
    DSW(r + 2) = (u16)(DSW(0x474D) + 0x24);
    memmove(DSP(r + 4), DSP(p + 4), 12);
    vec_rotate(zone, obj_matrix(OW(p, 0x20)), off);
    ladd_v((u16)(r + 4), off);
    FarPtr e = fp32(fx_spawn(r));
    if (!far_is_null(e)) {
        DSW(w) = (u16)(sgn16(DSS(zone)) * DSS(w));
        vec_rotate(w, obj_matrix(OW(p, 0x20)), v);
        vadd(v, (u16)(p + 0x70));
        FarPtr d = far_add(e, 0x20);
        FW(d, 0) = DSW(v); FW(d, 2) = DSW(v + 2); FW(d, 4) = DSW(v + 4);
        FW(d, 6) = 0xFFEC;
        FW(e, 0x10) = OW(p, 0x10); FW(e, 0x12) = OW(p, 0x12); FW(e, 0x14) = OW(p, 0x14);
        FW(e, 0x1A) = SEG(0x0916);
        FW(e, 0x18) = 0x03F7;
    }
    stk_release(mk);
}

/* AP 0916:03f7 — effect callback: ballistic fall, horizontal drift decays by 5 per frame */
u16 debris_update(FarPtr e)
{
    FarPtr v = far_add(e, 0x20);
    if (FSL(e, 0x0C) < 500) return 0;
    if (FW(v, 6) != 0xFED4) FW(v, 6) = (u16)(FW(v, 6) - 0x14);
    if (FS(v, 0) != 0) FS(v, 0) = (s16)(FS(v, 0) - sgn16(FS(v, 0)) * 5);
    if (FS(v, 2) != 0) FS(v, 2) = (s16)(FS(v, 2) - sgn16(FS(v, 2)) * 5);
    FW(v, 4) = (u16)(FW(v, 4) + FW(v, 6));
    FSL(e, 4) += FS(v, 0);
    FSL(e, 8) += FS(v, 2);
    FSL(e, 0x0C) += FS(v, 4);
    return 1;
}

/* ==== 10bf: wrecks =============================================================================== */

/* AP 10bf:0005 */
void death_classes_register(void)
{
    class_register(0x15CE);
    class_register(0x15E2);
}

/* AP 10bf:001a */
u16 wreck_shape(u16 o) { return (u16)(DSW(0x474D) + (OB(o, 0x18) == 1 ? 0x2B : 0x29)); }

/* AP 10bf:0039 — the plane hits the ground/water: wreck shape (or hidden), brain state 15CEh (timer) */
void plane_die(u16 p, u16 hidden)
{
    if (hidden) { OW(p, 0) |= 0x2000; OW(p, 0x28) = 0; }
    else { OW(p, 0) &= 0xDFFF; OW(p, 2) = wreck_shape(p); }
    OW(p, 0x12) = 0;
    OW(p, 0x10) = 0;
    OB(p, 0x1A) |= 1;
    if ((p == PLAYER && DSW(0x19F2) != 0) || view_outside_target() == p) DSB(0x1AFF) = 1;
    u16 st = brain_set_state((u16)(p + 0x4A), 0x15CE);
    DSL(st) = 0x1F40;
    if (p == PLAYER) {
        DSB(0x42BF) = 1;
        DSW(0x523B) = 0xFFFF;
        if ((OB(PLAYER, 0x1C) & 8) && OSL(PLAYER, 0x0C) > 1) DSW(0x5C97) = 3;
        OB(PLAYER, 0x1C) &= 0xF7;
    }
}

/* AP 10bf:00d2 — brain state 15CEh: when the timer runs out the wreck disappears (player: mission end) */
u16 wreck_update(u16 p)
{
    u16 di = OW(p, 0x54);
    if (DSL(di) == 0) return 0;
    timer32_dec(di);
    if (DSL(di) == 0) {
        OW(p, 0x28) = 0;
        OW(p, 0) |= 0x2000;
        if (p == PLAYER) end_request(1);
        else {
            OSL(p, 0x0C) = 10000000;
            OB(p, 0x42) |= 4;
            OB(p, 0x46) |= 4;
            brain_set_state((u16)(p + 0x4A), 0x160A);
            OB(p, 0x1B) |= 1;
        }
    }
    return 0;
}

/* AP 10bf:013a — remember the player's position relative to his carrier (brain state 15E2h) */
void carrier_rel_save(void)
{
    u16 mk = stk_mark(), l = stk_alloc(12), v = stk_alloc(6), r = stk_alloc(6);
    u16 c = OW(PLAYER, 0xEF);
    u16 st = brain_set_state((u16)(PLAYER + 0x4A), 0x15E2);
    memmove(DSP(l), DSP(PLAYER + 4), 12);
    lsub(l, (u16)(c + 4));
    l2v(v, l);
    vec_rotate(v, obj_matrix_t(OW(c, 0x20)), r);
    DSW(st) = DSW(r); DSW(st + 2) = DSW(r + 2); DSW(st + 4) = DSW(r + 4);
    DSW(st + 6) = (u16)(OW(PLAYER, 0x14) - OW(c, 0x14));
    stk_release(mk);
}

/* AP 10bf:01d6 — brain state 15E2h: keep the player at that spot on the moving carrier */
u16 carrier_rel_restore(u16 p)
{
    u16 mk = stk_mark(), v = stk_alloc(6), r = stk_alloc(6);
    u16 c = OW(PLAYER, 0xEF), st = OW(PLAYER, 0x54);
    DSW(v) = DSW(st); DSW(v + 2) = DSW(st + 2); DSW(v + 4) = DSW(st + 4);
    memmove(DSP(PLAYER + 4), DSP(c + 4), 12);
    vec_rotate(v, obj_matrix(OW(c, 0x20)), r);
    ladd_v((u16)(PLAYER + 4), r);
    OW(PLAYER, 0x14) = (u16)(OW(c, 0x14) + DSW(st + 6));
    OW(PLAYER, 0xB2) = 0;
    stk_release(mk);
    return 0;
}

__attribute__((constructor)) static void death_codeptrs_register(void)
{
    codeptr_register(PROG_AP, 0x09160173u, (CodeFn)parachute_update);
    codeptr_register(PROG_AP, 0x091603F7u, (CodeFn)debris_update);
    codeptr_register(PROG_AP, 0x10BF00D2u, (CodeFn)wreck_update);         /* brain state 15CEh */
    codeptr_register(PROG_AP, 0x10BF01D6u, (CodeFn)carrier_rel_restore);  /* brain state 15E2h */
}
