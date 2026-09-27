/* sim_world part A — plane / ship objects (465c) and object helpers (637f).
 * sim_world_dmg.md §1; control flow from the disassembly (x86dis AP_flat.exe). */
#include "sim_world/swa_priv.h"

/* ==== 465c ======================================================================================= */

/* AP 465c:0000 */
void objects_module_init(void) { DSW(0x3B01) = 0xFFFF; }

/* AP 465c:000b */
void movers_reset(void) { DSW(0x00BA) = 0; DSW(0x00BE) = 0; }

/* AP 465c:001c */
void movers_alloc(u16 n) { DSW(0x00BC) = arena_alloc((u16)(n << 1)); }

/* AP 465c:0032 — heading / roll bytes of a flight record to angles (pitch 0) */
u16 angles_from_bytes(u16 out, u16 b)
{
    DSW(out) = (u16)((s16)(s8)DSB(b) << 8);
    DSW(out + 4) = (u16)((s16)(s8)DSB(b + 1) << 8);
    DSW(out + 2) = 0;
    return out;
}

/* AP 465c:005a — matrix, class record of `size` bytes, optional orbit record, brain */
void object_init_common(u16 o, u16 size, u16 orbit)
{
    OW(o, 0x20) = arena_alloc(0x2A);
    obj_set_angles(OW(o, 0x20), (u16)(o + 0x10));
    OW(o, 0x16) = arena_alloc(size);
    memset(DSP(OW(o, 0x16)), 0, size);
    if (orbit) {
        OW(o, 0x22) = arena_alloc(0x0C);
        memset(DSP(OW(o, 0x22)), 0, 0x0C);
        if (OB(o, 0x18) == 1) { DSW(OW(o, 0x22)) = 0x1000; DSW(OW(o, 0x22) + 2) = 0x4000; }
        DSW(OW(o, 0x22) + 4) = (u16)orbit_min_dist(o);
    }
    class_alloc((u16)(o + 0x4A));
}

/* AP 465c:00ee — nation of a side (DS:00C0 table) */
u16 plane_c0(u16 side) { return (u16)(s16)(s8)DSB((u16)(side + 0xC0)); }

/* AP 465c:00fd */
u16 plane_setup(u16 p, s16 pilot, u16 type, s16 side, u16 ang)
{
    OB(p, 0x26) = DSB((u16)(side + 0xC0));
    if ((s16)type >= 0) OB(p, 0x19) = (u8)type;
    OW(p, 2) = helpers_nop(OB(p, 0x19));
    if (pilot >= 0) {
        OB(p, 0xBB) = (u8)pilot;
        if ((u8)pilot == 0x2C) PLAYER = p;
    }
    if (ang) { OW(p, 0x10) = DSW(ang); OW(p, 0x12) = DSW(ang + 2); OW(p, 0x14) = DSW(ang + 4); }
    return p;
}

/* AP 465c:015f — a plane from its flight record (SCRIPT.DAT plane entry) */
u16 plane_create(u16 rec)
{
    u16 mk = stk_mark(), ang = stk_alloc(6);
    u16 p = pool_alloc_zero_near(DSW(0x3B05));
    OW(p, 0) |= 0x8800;
    OB(p, 0x18) = 0;
    obj_set_class(p, 3);
    u16 a = DSS(rec + 0x0A) >= 0 ? angles_from_bytes(ang, (u16)(rec + 8)) : 0;
    plane_setup(p, (s16)(s8)DSB(rec + 2), DSB(rec), (s16)(s8)DSB(rec + 1), a);
    object_init_common(p, 8, 1);
    if (DSB(rec + 3)) OB(p, 0x1A) |= 4;
    if (p == PLAYER) {
        u16 dat = DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2));
        if (DSB(dat + 0x39)) OB(p, 0x1B) |= 8;
    }
    u16 slot = DSB((u16)(0x54BC + OB(p, 0x19)));
    gunlist_attach(slot, (u16)(p + 0x5E), (u16)(p + 0x60), p);
    dmg_state_create(slot, (u16)(p + 0x28), (u16)(p + 0x2A), p, rec);
    bxl_get((s16)slot, (u16)(p + 0x24));
    OB(p, 0xC9) = (u8)DSW(0x00BA);
    DSW(0x00BA)++;
    u16 si = (u16)(p + 0xEB);
    if (DSB(rec + 0x0C) == 0xFF) DSB(si + 1) = 0xFF;
    else {
        DSB(si) = DSB(rec + 0x0D);
        DSB(si + 1) = DSB(rec + 0x0D);
        DSW(si + 2) = (u16)((1u << (DSB(si + 1) & 0x1F)) - 1);    /* shl ax, cl (286+: count mod 32) */
    }
    OB(p, 0xF1) = DSB(rec + 0x0E) == 0xFF ? 0xFF : DSB(rec + 0x0F);
    memmove(DSP(p + 0xF3), DSP(rec + 0x10), 6);
    if (DSB(rec + 5)) plane_damage_all(p, (u16)(DSB(rec + 5) << 3));
    stk_release(mk);
    return p;
}

/* AP 465c:0322 */
void plane_free(u16 p) { pool_free2(DSW(0x3B05), p); }

/* AP 465c:0336 */
void plane_pool_create(u16 n) { DSW(0x3B05) = pool_create_near(n, 0x107); }

/* AP 465c:034c */
void plane_pool_destroy(void)
{
    u16 p;
    while ((p = pool_next_near(DSW(0x3B05), 0)) != 0) plane_free(p);
    pool_destroy_near(DSW(0x3B05));
    DSW(0x3B05) = 0;
}

/* AP 465c:0381 */
void ship_free(u16 s) { pool_free2(DSW(0x3B03), s); }

/* AP 465c:0395 — a ship from its flight record */
u16 ship_create(u16 rec)
{
    u16 s = pool_alloc_zero_near(DSW(0x3B03));
    OW(s, 0) |= 0x8800;
    OB(s, 0x18) = 1;
    OB(s, 0x19) = DSB(rec);
    OB(s, 0x26) = DSB((u16)((s16)(s8)DSB(rec + 1) + 0xC0));
    obj_set_class(s, 4);
    if (DSB(rec)) OW(s, 2) = ship_shape_of_type(DSB(rec));
    OL(s, 0x68) = DSL(rec + 8);
    OL(s, 0x6C) = DSL(rec + 0x0C);
    object_init_common(s, 0x0C, 1);
    u16 slot = (u16)(DSB((u16)(0x555B + OB(s, 0x19))) + DSW(0x54BA));
    gunlist_attach(slot, (u16)(s + 0x5E), (u16)(s + 0x60), s);
    dmg_state_create(slot, (u16)(s + 0x28), (u16)(s + 0x2A), s, 0);
    bxl_get((s16)slot, (u16)(s + 0x24));
    if (DSW(rec + 2)) obj_damage_all(s, (u16)(DSW(rec + 2) << 3));
    if (DSB(rec + 6)) OB(s, 0x1E) |= 2;
    if (DSB(rec + 7)) OB(s, 0x1E) |= 4;
    return s;
}

/* AP 465c:04a3 */
void ship_pool_create(u16 n) { DSW(0x3B03) = pool_create_near(n, 0x88); }

/* AP 465c:04b9 */
void ship_pool_destroy(void)
{
    u16 s;
    while ((s = pool_next_near(DSW(0x3B03), 0)) != 0) ship_free(s);
    pool_destroy_near(DSW(0x3B03));
    DSW(0x3B03) = 0;
}

/* AP 465c:0818 */
void objects_module_end(void) {}

/* ==== 637f ======================================================================================= */

/* AP 637f:0000 — computes the kind into a local and returns nothing */
void alive_nop(u16 o) { (void)o; }

/* AP 637f:0015 — object kind (low 5 bits of the flags word) */
void obj_set_class(u16 o, u16 k) { OW(o, 0) = (u16)((OW(o, 0) & 0xFFE0) | k); }

/* AP 637f:002b — ships: a real type; planes: not crashed (+1A bit 0) and not removed (+1B bit 0) */
u16 obj_alive(u16 o)
{
    if (OB(o, 0x18) == 1) return OB(o, 0x19) != 0;
    if (OB(o, 0x18) == 0 && !(OB(o, 0x1A) & 1) && !(OB(o, 0x1B) & 1)) return 1;
    return 0;
}

/* AP 637f:0073 */
u16 ship_is_real(u16 s) { return OB(s, 0x19) != 0; }

/* AP 637f:008a — (i + 1) mod the object count. PORT: `cdq` extends the stale 32-bit EAX; the high
 * word is taken as 0 (non-negative dividend) */
u16 ap_list_next(u16 i)
{
    s32 n = (s32)(u16)(i + 1);
    return (u16)(s16)(n % (s16)DSW(0x4778));
}

/* AP 637f:009a */
u16 flight_is_carrier_group(u16 o) { return DSB((u16)(OW(o, 0x34) + 0x3B)) & 2; }
