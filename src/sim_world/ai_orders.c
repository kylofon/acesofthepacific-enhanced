/* sim_world part B — reactions to the player's radio orders (1660, AI part) and ces.c (07fe).
 * sim_world_ai.md §15; ported from the disassembly (the order jump table 1660:0649 read from the image). */
#include "sim_world/swb_priv.h"

/* 07fe:000a ces_check(a, b, c) — the assertion helper of ces.c. As compiled every path reaches the assert
 * call (the "all zero" alternative can never be taken after the "all non-zero" test failed on b != 0). */
void ces_check(u16 a, u16 b, u16 c)
{
    if (a && b && c && !b && !c && !a) return;             /* (unreachable, as in the original) */
    assert_fail(far_make(SEG(0x39E1), 0), 0x41, 0);
}

/* 1660:0009 aircraft_name(rec, dst) — STR(6, DS:18E6[rec[1]] + rec[0]), "s" appended for a group */
u16 aircraft_name(u16 rec, u16 dst)
{
    s16 id = (s16)(DSS(0x18E6 + 2 * (s16)DSC(rec + 1)) + (s16)DSC(rec));
    FarPtr s = str(6, id);
    rtl__fstrcpy(ds_ptr(dst), s);
    if (DSS(rec + 8) > 1) {
        u16 e = (u16)(dst + strlen((char *)DSP(dst)));
        memcpy(DSP(e), DSP(0x190E), 2);
    }
    return dst;
}

/* 1660:0065 mates_of(p, mode, out) — alive members m != p of p's flight; mode < 0 or 2: all, 0: my pair,
 * 1: the others */
u16 mates_of(u16 p, s16 mode, u16 out)
{
    u16 f = FLT(p);
    u16 all = mode == 2;
    u16 want = mode == 0;
    u16 n = 0;
    if (mode < 0) all = 1;
    s16 i = 0;
    while ((i = (s16)first_alive(f, (u16)i)) > -1) {
        u16 m = DSW(OW(f, 8) + 2 * i);
        i++;
        if (m == p) continue;
        u16 pair = OW(m, 0xDD) == p || OW(p, 0xDD) == m;
        if (all || pair == want) {
            DSW(out + 2 * n) = m;
            n++;
        }
    }
    return n;
}

/* 1660:0110 aspect_to(a, b) */
u16 aspect_to(u16 a, u16 b) { return (u16)(OW(b, 0x14) - (u16)(OW(a, 0xD1) + OW(a, 0x14) + 0x8000)); }

/* 1660:0130 attacker_on_tail(t) — the first plane close behind t and pointing at it */
u16 attacker_on_tail(u16 t)
{
    for (s16 i = 0; i < (s16)DSW(0x4776); i++) {
        u16 q = DSW(DSW(0x475B) + 2 * i);
        if (OW(q, 0xD7) != t) continue;
        if (OSL(q, 0xD3) >= 10000) continue;
        if ((u16)(OW(q, 0xD1) + 0x4000) >= 0x8000) continue;
        if ((u16)(OW(q, 0xCF) + 0x3000) >= 0x6000) continue;
        if ((u16)(aspect_to(q, t) + 0x5000) < 0xA000) continue;
        return q;
    }
    return 0;
}

/* 1660:01b4 */
u16 player_orders_active(void) { return DSW(0x5BD4); }

/* radio reply 1815:06e7(id, rec, 1): rec+16h subject, rec+0Eh (far) = 0 unless given */
static void reply(u16 id, u16 subject, FarPtr text)
{
    u16 mk = stk_mark(), rec = radio_rec(0x18);
    DSW(rec + 0x16) = subject;
    ds_far_wr((u16)(rec + 0x0E), text);
    radio_queue7(id, rec, 1);
    stk_release(mk);
}

/* 1660:01bc ai_order_execute(order, mode, tgt) — §15.1 */
void ai_order_execute(u16 order, u16 mode, u16 tgt)
{
    u16 mk = stk_mark(), m = stk_alloc(0x0A), name = stk_alloc(0x28);
    s16 side = (s16)OC(PLAYER, 0x26);
    s16 n = (s16)mates_of(PLAYER, (s16)mode, m);
    FarPtr none = far_make(0, 0);
#define M(i) DSW(m + 2 * (i))
    switch (order) {
    case 0x33: {                                           /* cover me */
        if (n == 0) break;
        u16 w = M(0);
        s32 d = dist3(w, PLAYER);
        u16 id;
        if (DFLAGS(DESC(w)) & 0x20) id = 4;
        else if (d > 20000) id = 3;
        else {
            u16 x = attacker_on_tail(PLAYER);
            if (!x) id = 2;
            else {
                id = 5;
                cross_enter(w, PLAYER, x);
            }
        }
        reply(id, w, none);
        break;
    }
    case 0x34: case 0x35: {                                /* check six */
        if (n == 0) break;
        s16 first = 0;
        u16 id = 6;
        for (s16 i = 0; i < n; i++) {
            u16 q = M(i);
            if (attacker_on_tail(q)) {
                OB(q, 0x43) |= 1;
                OB(q, 0x47) |= 1;
                id = 8;
                if (first == 0) first = i;
            }
        }
        reply((u16)(id + side), M(first), none);
        break;
    }
    case 0x36: case 0x37:                                  /* break (no check of n) */
        reply((u16)(2 * side + (order == 0x37) + 10), M(0), none);
        bank_enter(M(0), order == 0x36);
        break;
    case 0x38:                                             /* split */
        for (s16 i = 0; i < n; i++) {
            u16 q = M(i);
            manoeuvre_enter((OB(q, 0x1A) & 0x80) ? 12 : 11, 0, q);
        }
        reply(0x0E, 0, none);
        break;
    case 0x39: case 0x3B: {
        u16 w = M(0), id;
        if (DFLAGS(DESC(w)) & 0x20) {
            id = 0x13;
        } else {
            id = order == 0x39 ? 0x0F : 0x11;
            OB(w, 0xCA) = 0;
            OSL(w, 0xE7) = 50000;
            OB(w, 0x1B) |= 0x10;
        }
        reply((u16)(id + side), w, none);
        OSL(w, 0x4C) = 0;
        break;
    }
    case 0x3A: case 0x3C:
        for (s16 i = 0; i < n; i++) {
            u16 q = M(i);
            OB(q, 0xCA) = 0;
            OSL(q, 0xE7) = 50000;
            OB(q, 0x1B) |= 0x10;
            ai_think_target(q);
        }
        reply((u16)(0x15 + (order == 0x3C)), M(0), none);
        break;
    case 0x3D:                                             /* resume */
        for (s16 i = 0; i < n; i++) {
            u16 q = M(i);
            OSL(q, 0xE3) = 50000;
            OB(q, 0x1B) &= 0xEF;
        }
        DSW(0x5BD4) = 0;
        reply((u16)(0x17 + side), M(0), none);
        break;
    case 0x40: case 0x41: case 0x42: {                     /* attack a target */
        u16 id = 0x19;
        DSW(0x5BD4) = 0;
        FarPtr text = ds_ptr(aircraft_name(tgt, name));
        u16 subj = mode == 2 ? 0 : M(0);
        for (s16 i = 0; i < n; i++) {
            u16 q = M(i);
            s16 kind;
            if (DSC(tgt + 1) == 0) {
                if (has_ordnance(q)) {
                    ai_break_toward(q);
                    id = 0x56;
                }
                kind = (s16)(DSC(tgt) + 1);
            } else {
                kind = (s16)(DSC(tgt + 1) + 4);
            }
            OSL(q, 0xE3) = 250000;
            OB(q, 0xCA) = (u8)kind;
            OW(q, 0xBD) = DSW(tgt + 0x0A);
            OB(q, 0x1B) &= 0xDF;
            st_think_combat(q);
        }
        reply(id, subj, text);
        break;
    }
    case 0x43: case 0x44: {                                /* attack from the right / left */
        u16 k = order == 0x44;
        DSW(0x5BD4) = 0;
        reply((u16)(0x1A + k), M(0), none);
        u8 bit = (u8)(1 << ((k + 0x23) & 7));
        u16 off = (u16)((k + 0x23) >> 3);
        for (s16 i = 0; i < n; i++) OB(M(i), 0x1A + off) |= bit;     /* +1E bit 3 / bit 4 */
        break;
    }
    case 0x50:                                             /* hold formation */
        DSW(0x5BD4) = 1;
        reply(0x51, 0, none);
        break;
    default:
        break;
    }
#undef M
    stk_release(mk);
}

/* 1660:0706 mates_have_bombers(p, mode, &torp) */
u16 mates_have_bombers(u16 p, u16 mode, u16 torp)
{
    u16 mk = stk_mark(), l = stk_alloc(0x0A);
    s16 n = (s16)mates_of(p, (s16)mode, l);
    u16 r = 0;
    if (torp) DSW(torp) = 0;
    for (s16 i = 0; i < n; i++) {
        u16 q = DSW(l + 2 * i);
        if (is_pure_fighter(q)) continue;
        if (torp && torpedo_loaded(q)) DSW(torp) = 1;
        r = 1;
        break;
    }
    stk_release(mk);
    return r;
}

/* 1660:0787 ship_priority(s) — lower = preferred (carriers first) */
u16 ship_priority(u16 s) { return DSW(0x18EC + 2 * (s16)OC(SDAT(s), 0x6C)); }

/* 1660:0bd7 alive_count(f) */
u16 alive_count(u16 f)
{
    u16 n = 0;
    for (s16 i = 0; OS(f, 6) > i; i++)
        if (!plane_is_down(DSW(OW(f, 8) + 2 * i))) n++;
    return n;
}
