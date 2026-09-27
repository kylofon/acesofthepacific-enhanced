/* sim_world part B — manoeuvre library (1ad2). sim_world_ai.md §10; the manoeuvre functions were ported
 * from the disassembly (their stage bits and thresholds differ in places from the spec's summary table).
 * Controls block Plane+AC: +AC aileron, +AE elevator, +B0 rudder, +B2 throttle. Manoeuvre scratch (the
 * brain scratch of 1741h/1755h): +0 i8 manoeuvre (FFh none), +1 stage bits, +2 i16 timer, +4 heading. */
#include "sim_world/swb_priv.h"

#define AIL(p)   OW(p, 0xAC)
#define ELEV(p)  OW(p, 0xAE)
#define RUDD(p)  OW(p, 0xB0)
#define STG(p)   DSB(SCR(p) + 1)
#define MTIM(p)  ((u16)(SCR(p) + 2))
static inline u16 cruise_p(u16 p) { return DSW(PIL(p) + 0x10); }

/* 1ad2:0003 — SDEF.DAT (20 × 14 bytes) → DS:5CEC */
static void sdef_load(void) { file_load(0x5CEC, 0x118, 1, res_name(0x19B4, 0x5BC)); }

/* 1ad2:002e manoeuvre_allowed(AX = m, BX = p) — strict bounds of SDEF[m] */
u16 manoeuvre_allowed(u16 ax, u16 bx)
{
    u16 r = (u16)(0x5CEC + 14 * (s16)ax);
    s16 v = OS(bx, 0x6C), pit = OS(bx, 0x10), rol = OS(bx, 0x12);
    if (pit <= DSS(r + 4) || pit >= DSS(r + 6)) return 0;
    if (rol <= DSS(r + 8) || rol >= DSS(r + 0x0A)) return 0;
    if (DSS(r) >= v || DSS(r + 2) <= v) return 0;
    return (s32)DSS(r + 0x0C) < OSL(bx, 0x0C);
}

/* 1ad2:0082 steer_level(BX = p) */
u16 steer_level(u16 bx)
{
    u16 a = steer_roll_to(bx, 0, 8);
    u16 b = steer_pitch_to(bx, 0, 8);
    return a & b;
}

/* 1ad2:00b1 split-S (m 0) */
static u16 mnv_split_s(u16 p)
{
    u16 r = 0;
    if (!(STG(p) & 2)) {
        steer_roll_to(p, 0, 0);
        if (OS(p, 0x10) > 0 && (u16)(OW(p, 0x12) + 0x3E80) >= 0x7D00) STG(p) |= 2;
    } else if (!(STG(p) & 4)) {
        steer_roll_to(p, (s16)0x8000, 0);
        if (OS(p, 0x10) < 0 && (u16)(OW(p, 0x12) + 0x3E80) < 0x7D00) STG(p) |= 4;
    } else {
        steer_roll_to(p, 0, 0);
        if (OS(p, 0x10) > -4000) r = 1;
    }
    ELEV(p) = 0x100;
    THROTTLE(p) = cruise_p(p);
    return r;
}

/* 1ad2:015b aileron roll (dir 0 left, 1 right) */
static u16 mnv_aileron_roll(u16 p, u16 dir)
{
    u16 r = 0;
    THROTTLE(p) = cruise_p(p);
    if (!(STG(p) & 2)) {
        if ((u16)(OW(p, 0x12) + 0x6D60) >= 0xDAC0) STG(p) |= 2;
    } else if (!(STG(p) & 4)) {
        u16 c = dir == 0 ? OS(p, 0x12) > 0 : OS(p, 0x12) < 0;
        if (c) STG(p) |= 4;
    } else if ((u16)(OW(p, 0x12) + 0x0DAC) < 0x1B58) {
        r = 1;
    }
    AIL(p) = dir == 0 ? 0xFF00 : 0x100;
    ELEV(p) = 0x100;
    return r;
}
static u16 mnv_roll_left(u16 p) { return mnv_aileron_roll(p, 0); }    /* 1ad2:01f9 (m 1) */
static u16 mnv_roll_right(u16 p) { return mnv_aileron_roll(p, 1); }   /* 1ad2:020a (m 2) */

/* 1ad2:021b half loop / Immelmann (m 3) */
static u16 mnv_immelmann(u16 p)
{
    u16 r = 0;
    if (!(STG(p) & 2)) {
        s16 a = abs16(OS(p, 0x12));
        ELEV(p) = a < 13000 ? 0xFF80 : a < 26000 ? 0 : 0x100;
        if (steer_roll_to(p, (s16)0x8000, 30)) STG(p) |= 2;
    } else if ((u16)(OW(p, 0x12) + 0x4000) >= 0x8000) {
        steer_roll_to(p, (s16)0x8000, 0);
        ELEV(p) = 0x100;
    } else {
        steer_roll_to(p, 0, 0);
        ELEV(p) = 0x100;
        if (OS(p, 0x10) > -2000) r = 1;
    }
    return r;
}

/* 1ad2:02cf wing-over (dir 0 left, 1 right) */
static u16 mnv_wingover(u16 p, u16 dir)
{
    u16 r = 0;
    s16 s = dir == 0 ? -1 : 1;
    if (!(STG(p) & 1)) {
        if (steer_roll_to(p, (s16)(s * 22000), 30)) STG(p) |= 1;
    } else if (!(STG(p) & 2)) {
        ELEV(p) = 0x96;
        if (steer_roll_to(p, (s16)(s * 22000), 30)) STG(p) |= 2;
    } else if (!(STG(p) & 4)) {
        AIL(p) = (u16)(s * -0xBE);
        ELEV(p) = 0x96;
        if ((u16)(OW(p, 0x12) + 0x4000) < 0x8000) STG(p) |= 4;
    } else if (!(STG(p) & 8)) {
        steer_pitch_to(p, (u16)-1000, 0);
        if (steer_roll_to(p, 0, 30)) r = 1;
    }
    return r;
}
static u16 mnv_wingover_left(u16 p) { return mnv_wingover(p, 0); }    /* 1ad2:03b0 (m 4) */
static u16 mnv_wingover_right(u16 p) { return mnv_wingover(p, 1); }   /* 1ad2:03c1 (m 5) */

/* 1ad2:03d2 loop (m 6) */
static u16 mnv_loop(u16 p)
{
    u16 r = 0;
    THROTTLE(p) = cruise_p(p);
    if (!(STG(p) & 2)) {
        ELEV(p) = 0x100;
        steer_roll_to(p, 0, 0);
        if (OS(p, 0x10) > 0 && (u16)(OW(p, 0x12) + 0x3E80) >= 0x7D00) STG(p) |= 2;
    } else if (!(STG(p) & 4)) {
        steer_roll_to(p, (s16)0x8000, 0);
        ELEV(p) = (u16)((s16)(OW(p, 0x10) - 2000) >> 5);
        if (OS(p, 0x10) < 3000) STG(p) |= 4;
    } else if (!(STG(p) & 8)) {
        OW(p, 0xC1) |= 0x10;
        if ((u16)(OW(p, 0x12) + 0x4000) >= 0x8000) ELEV(p) = (u16)((s16)(OW(p, 0x10) - 1000) >> 3);
        else steer_pitch_to(p, 3000, 0);
        if (steer_roll_to(p, 0, 30)) r = 1;
    }
    return r;
}

/* 1ad2:04cb zoom climb (m 7) */
static u16 mnv_zoom_climb(u16 p)
{
    u16 r = 0;
    steer_roll_to(p, 0, 0);
    steer_pitch_to(p, 9000, 10);
    THROTTLE(p) = cruise_p(p);
    if (!(STG(p) & 1)) {
        DSW(MTIM(p)) = 10000;
        STG(p) |= 1;
        return 0;
    }
    if (!((OB(p, 0xAA) >> 1) & 1) && !(STG(p) & 2) && timer16_dec(MTIM(p)) != 0) return 0;
    STG(p) |= 2;
    if (steer_pitch_to(p, 0, 8)) r = 1;
    return r;
}

/* 1ad2:056c heading_kick(p, dir, bit) (RNG) */
static u16 heading_kick(u16 p, u16 dir, u16 bit)
{
    u16 s = SCR(p);
    if ((s16)(s8)DSB(s + 1) & bit) return 1;
    steer_heading_nolead(p, 0, dir == 1 ? (u16)-16000 : 16000);
    if (timer16_dec((u16)(s + 2)) != 0) return 0;
    DSB(s + 1) |= (u8)bit;
    DSW(s + 2) = (u16)(((RNG() & 15) + 20) << 8);                                   /* RNG */
    return 1;
}

/* 1ad2:05df barrel roll sequence (m 8) */
static u16 mnv_barrel_roll(u16 p)
{
    steer_pitch_to(p, 0, 0);
    if (!(STG(p) & 1)) {
        DSW(MTIM(p)) = 10;
        STG(p) |= 1;
        return 0;
    }
    if (heading_kick(p, 1, 2) && heading_kick(p, 0, 4) && heading_kick(p, 1, 8) && heading_kick(p, 0, 0x10) &&
        steer_roll_to(p, 0, 8))
        return 1;
    return 0;
}

/* 1ad2:0679 skid (dir 0 left, 1 right) */
static u16 mnv_skid(u16 p, u16 dir)
{
    steer_pitch_to(p, 0, 0);
    steer_roll_to(p, 0, 0);
    RUDD(p) = dir == 1 ? 0x100 : 0xFF00;
    AIL(p) = (u16)(OS(p, 0x8E) >> 1);
    THROTTLE(p) = cruise_p(p);
    if (!(STG(p) & 1)) {
        DSW(MTIM(p)) = 6000;
        STG(p) |= 1;
        return 0;
    }
    return timer16_dec(MTIM(p)) == 0;
}
static u16 mnv_skid_left(u16 p) { return mnv_skid(p, 0); }            /* 1ad2:0710 (m 9) */
static u16 mnv_skid_right(u16 p) { return mnv_skid(p, 1); }           /* 1ad2:0721 (m 10) */

/* 1ad2:0732 climbing turn (dir 0 left, 1 right) */
static u16 mnv_climbing_turn(u16 p, u16 dir)
{
    u16 r = 0;
    steer_roll_to(p, dir == 1 ? 5000 : -5000, 0);
    THROTTLE(p) = cruise_p(p);
    if (steer_pitch_to(p, 4500, 20) && !(STG(p) & 1)) {
        DSW(MTIM(p)) = 8000;
        STG(p) |= 1;
    }
    if ((STG(p) & 1) && timer16_dec(MTIM(p)) == 0) r = 1;
    return r;
}
static u16 mnv_climbing_turn_left(u16 p) { return mnv_climbing_turn(p, 0); }   /* 1ad2:07c2 (m 11) */
static u16 mnv_climbing_turn_right(u16 p) { return mnv_climbing_turn(p, 1); }  /* 1ad2:07d3 (m 12) */

/* 1ad2:07e4 — too low: level off (1791h) */
static u16 recover_if_low(u16 p)
{
    u16 r = too_low(p, 4000);
    if (r) set_state(p, D_LEVEL);
    return r;
}

/* 1ad2:0814 slipping dive (dir 0 left, 1 right) */
static u16 mnv_slipping_dive(u16 p, u16 dir)
{
    u16 r = 0;
    if (recover_if_low(p)) return 0;
    steer_roll_to(p, 0, 0);
    THROTTLE(p) = cruise_p(p);
    RUDD(p) = dir == 1 ? 0x100 : 0xFF00;
    AIL(p) = (u16)(OS(p, 0x8E) >> 1);
    if (steer_pitch_to(p, (u16)-8000, 20) && !(STG(p) & 1)) {
        DSW(MTIM(p)) = 5000;
        STG(p) |= 1;
    }
    if ((STG(p) & 1) && timer16_dec(MTIM(p)) == 0) r = 1;
    return r;
}
static u16 mnv_slipping_dive_left(u16 p) { return mnv_slipping_dive(p, 0); }   /* 1ad2:08c6 (m 13) */
static u16 mnv_slipping_dive_right(u16 p) { return mnv_slipping_dive(p, 1); }  /* 1ad2:08d7 (m 14) */

/* 1ad2:08e8 diving turn (dir 0 left, 1 right) */
static u16 mnv_diving_turn(u16 p, u16 dir)
{
    u16 r = 0;
    if (recover_if_low(p)) return 0;
    steer_roll_to(p, dir == 1 ? 6000 : -6000, 0);
    if (steer_pitch_to(p, (u16)-8000, 20) && !(STG(p) & 1)) {
        DSW(MTIM(p)) = 5000;
        STG(p) |= 1;
    }
    if ((STG(p) & 1) && timer16_dec(MTIM(p)) == 0) r = 1;
    return r;
}
static u16 mnv_diving_turn_left(u16 p) { return mnv_diving_turn(p, 0); }   /* 1ad2:096d (m 15) */
static u16 mnv_diving_turn_right(u16 p) { return mnv_diving_turn(p, 1); }  /* 1ad2:097e (m 16) */

/* 1ad2:098f max-bank turn (dir 0 left, 1 right) */
static u16 mnv_max_bank_turn(u16 p, u16 dir)
{
    s16 b = PW(p, 0x0C);
    s16 r = dir == 0 ? (s16)-b : b;
    steer_pitch_to(p, 0, 0);
    steer_roll_to(p, r, 0);
    if (!(STG(p) & 1)) {
        DSW(MTIM(p)) = 5000;
        STG(p) |= 1;
        return 0;
    }
    if (timer16_dec(MTIM(p)) != 0) return 0;
    OB(p, 0x43) |= 1;
    OB(p, 0x47) |= 1;
    return 1;
}
static u16 mnv_max_bank_left(u16 p) { return mnv_max_bank_turn(p, 0); }   /* 1ad2:0a1a (m 18) */
static u16 mnv_max_bank_right(u16 p) { return mnv_max_bank_turn(p, 1); }  /* 1ad2:0a2b (m 19) */

/* 1ad2:0a3c dive at the aircraft's dive pitch (m 17) */
static u16 mnv_dive(u16 p)
{
    u16 r = 0;
    if (recover_if_low(p)) return 0;
    u16 d = DSW(DAT(p) + 0x3A);
    steer_roll_to(p, 0, 0);
    if (steer_pitch_to(p, d, 20) && !(STG(p) & 1)) {
        DSW(MTIM(p)) = 7000;
        STG(p) |= 1;
    }
    if ((STG(p) & 1) && timer16_dec(MTIM(p)) == 0) r = 1;
    return r;
}

/* 1ad2:0acc manoeuvre_step(BX = p) — the table DS:193C {coordinated, far fn} × 20 */
u16 manoeuvre_step(u16 bx)
{
    u16 s = SCR(bx);
    u16 e = (u16)(0x193C + 6 * (s16)(s8)DSB(s));
    u16 r = 1;
    if (DSB(s) != 0xFF) {
        r = ((u16 (*)(u16))codeptr_lookup(ds_far((u16)(e + 2))))(bx);
        if (DSW(e) != 0) OW(bx, 0xC1) |= 1;
        if (r) {
            DSB(s) = 0xFF;
            DSB(s + 1) = 0;
        }
    }
    return r;
}

/* 1ad2:0b20 manoeuvre_start(AX = m, BX = p) */
u16 manoeuvre_start(u16 ax, u16 bx)
{
    s16 m = (s16)ax;
    if (m >= 20 || m < 0) return 0;
    if (DSW(0x193E + 6 * m) == 0 && DSW(0x1940 + 6 * m) == 0) return 0;
    if (!manoeuvre_allowed(ax, bx)) return 0;
    u16 s = SCR(bx);
    DSB(s) = (u8)m;
    DSB(s + 1) = 0;
    DSW(s + 4) = OW(bx, 0x14);
    DSW(s + 2) = 0;
    return 1;
}

/* 1ad2:0b82 manoeuvre_enter(AX = m, DX = then_extend, BX = p) */
u16 manoeuvre_enter(u16 ax, u16 dx, u16 bx)
{
    if (!manoeuvre_allowed(ax, bx)) return 0;
    set_state(bx, dx ? D_MNV_EXTEND : D_MANOEUVRE);
    manoeuvre_start(ax, bx);
    return 1;
}

/* 1ad2:0bcb manoeuvre_pick(AX = n, BX = i8 id[], DX = i8 w[]) — weighted pick (RNG) */
u16 manoeuvre_pick(u16 ax, u16 bx, u16 dx)
{
    s16 n = (s16)ax;
    s16 T = 0;
    for (s16 i = 0; i < n; i++) T = (s16)(T + (s8)DSB(dx + i));
    if (T <= 0) return 0xFFFF;
    s16 r = RNG_RANGE(T);                                                          /* RNG */
    for (s16 i = 0; i < n; i++) {
        s16 w = (s8)DSB(dx + i);
        if (w > r) return (u16)(s16)(s8)DSB(bx + i);
        r = (s16)(r - w);
    }
    return 0xFFFF;
}

/* 1ad2:0c39 */
void manoeuvres_init(void) { sdef_load(); }

__attribute__((constructor)) static void ai_manoeuvre_codeptrs(void)
{
    static const struct { u32 a; void *f; } t[] = {
        { AP_FN_mnv_split_s, mnv_split_s }, { AP_FN_mnv_roll_left, mnv_roll_left },
        { AP_FN_mnv_roll_right, mnv_roll_right }, { AP_FN_mnv_immelmann, mnv_immelmann },
        { AP_FN_mnv_wingover_left, mnv_wingover_left }, { AP_FN_mnv_wingover_right, mnv_wingover_right },
        { AP_FN_mnv_loop, mnv_loop }, { AP_FN_mnv_zoom_climb, mnv_zoom_climb },
        { AP_FN_mnv_barrel_roll, mnv_barrel_roll }, { AP_FN_mnv_skid_left, mnv_skid_left },
        { AP_FN_mnv_skid_right, mnv_skid_right }, { AP_FN_mnv_climbing_turn_left, mnv_climbing_turn_left },
        { AP_FN_mnv_climbing_turn_right, mnv_climbing_turn_right },
        { AP_FN_mnv_slipping_dive_left, mnv_slipping_dive_left },
        { AP_FN_mnv_slipping_dive_right, mnv_slipping_dive_right },
        { AP_FN_mnv_diving_turn_left, mnv_diving_turn_left }, { AP_FN_mnv_diving_turn_right, mnv_diving_turn_right },
        { AP_FN_mnv_dive, mnv_dive }, { AP_FN_mnv_max_bank_left, mnv_max_bank_left },
        { AP_FN_mnv_max_bank_right, mnv_max_bank_right }, { AP_FN_manoeuvre_allowed, manoeuvre_allowed },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) codeptr_register(PROG_AP, t[i].a, (CodeFn)t[i].f);
}
