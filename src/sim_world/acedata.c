/* sim_world part A — acedata.c (45cc): model slots, pilot profiles, aircraft / ship .DAT, FORMS.DAT.
 * sim_world_dmg.md §1.5, §1.7; control flow and constants from the disassembly (x86dis AP_flat.exe 45cc). */
#include "sim_world/swa_priv.h"
#include "platform/rtl.h"

#define ACEDATA_C  far_make(SEG(0x381C), 0)      /* "acedata.c" (file name of the mem_new / assert calls) */

static u16 ace_new(u16 n, s16 line) { return mem_new(n, 0, far_to_near_str(ACEDATA_C), line); }
static void ace_delete(u16 p, s16 line) { mem_delete(p, 0, far_to_near_str(ACEDATA_C), line); }

/* AP 45cc:0000 — slot[t] = next slot for every type t with count[t] != 0, else FFh; returns the slot count */
static u16 slots_from_counts(u16 count_ds, u16 slot_ds, u16 n)
{
    memset(DSP(slot_ds), 0xFF, n);
    u16 k = 0;
    for (u16 i = 0; (s16)i < (s16)n; i++)
        if (DSW((u16)(count_ds + 2 * i)) != 0) DSB((u16)(slot_ds + i)) = (u8)k++;
    return k;
}

/* AP 45cc:0056 */
void model_slots_assign(u16 planes_ds, u16 ships_ds)
{
    DSW(0x54BA) = slots_from_counts(planes_ds, 0x54BC, 0x9F);
    DSW(0x54B8) = slots_from_counts(ships_ds, 0x555B, 0x73);
}

/* AP 45cc:0080 — ((hi − lo)·v >> 10) + lo, 16-bit difference, 32-bit product */
static s16 rating_lerp(s16 v, s16 lo, s16 hi)
{
    s32 d = (s32)(s16)(hi - lo) * v;
    return (s16)((s16)(d >> 10) + lo);
}

/* AP 45cc:00a0 — PILOT<mask> name list, one 70h-byte .PLT per used pilot, ratings converted in place */
void pilots_load(u16 used_ds, u16 mask)
{
    u16 names = res_load_set(0x00AA, mask, 0xC2, 0x0F);
    s16 n = sum_i8(used_ds, 0xC2);
    DSW(0x55CE) = ace_new((u16)(0x184 + n * 0x70), 0xAB);
    u16 si = (u16)(DSW(0x55CE) + 0x184);
    u16 used = used_ds;
    for (u16 i = 0; i < 0xC2; i++, used++) {
        if (!DSB(used)) continue;
        u16 nm = res_name(far_to_near_str(ds_far((u16)(names + 4 * i))), 0x00B0);
        file_load(si, 0x70, 1, nm);
        DSW((u16)(DSW(0x55CE) + 2 * i)) = si;
        si = (u16)(si + 0x70);
    }
    res_free(names);
    for (u16 i = 0; i < 0xC2; i++) {
        u16 p = DSW((u16)(DSW(0x55CE) + 2 * i));
        if (!p) continue;
        s16 r0 = DSS(p), r1 = DSS(p + 2), r3 = DSS(p + 6), r4 = DSS(p + 8), r5 = DSS(p + 0x0A);
        s16 r6 = DSS(p + 0x0C), r8 = DSS(p + 0x10), r9 = DSS(p + 0x12), r12 = DSS(p + 0x18);
        DSS(p + 0x00) = rating_lerp(r0, 0x200, 0x44C);
        DSS(p + 0x48) = rating_lerp(r0, 0x334, 0x3B6);
        DSS(p + 0x02) = rating_lerp(r1, 0x1F4, 0x400);
        DSS(p + 0x04) = rating_lerp(r1, 0x064, 0x44C);
        DSS(p + 0x06) = rating_lerp(r3, 0x096, (s16)0xFFE2);
        DSS(p + 0x08) = rating_lerp(r4, 0x064, 0x3E8);
        DSS(p + 0x0A) = rating_lerp(r5, 0x258, (s16)0xFF38);
        DSS(p + 0x0C) = rating_lerp(r6, 0x1F40, 0x36B0);
        DSS(p + 0x0E) = rating_lerp(r6, 0x3E8, 0x708);
        DSS(p + 0x10) = rating_lerp(r8, 0x0F3, 0x100);
        DSS(p + 0x12) = rating_lerp(r9, (s16)0xFED4, 0x46);
        DSS(p + 0x14) = rating_lerp(r9, 0x384, (s16)0xFF10);
        DSS(p + 0x16) = rating_lerp(r9, 0x1D4C, 0x2AF8);
        DSS(p + 0x1E) = rating_lerp(r9, 0x400, 1);
        DSS(p + 0x18) = rating_lerp(r12, 0x1F4, (s16)0xFF6A);
        DSS(p + 0x1A) = rating_lerp(r12, 0x1F4, (s16)0xFF6A);
        DSS(p + 0x1C) = rating_lerp(r12, 0x200, 0x400);
        DSS(p + 0x20) = rating_lerp(r12, 0x1F4, 0x384);
        if (DSS(p + 0x0A) < 0) DSS(p + 0x0A) = 0;
    }
}

/* AP 45cc:032a */
void pilots_free(void) { ace_delete(DSW(0x55CE), 0xE4); }

/* AP 45cc:034f — <name>.DAT (83h bytes) + <name>.FMD (D6h), gun list, damage image, box list */
static u16 aircraft_load(u16 type)
{
    u16 mk = stk_mark();
    u16 fmd_loc = stk_alloc(0xD6), name = stk_alloc(10);
    u16 dat = ace_new(0x83, 0xFB);
    rtl_strcpy(name, far_to_near_str(ds_far((u16)(DSW(0x55D6) + type * 4))));
    file_load(dat, 0x83, 1, res_name(name, 0x05BC));
    u16 fmd = ace_new(0xD6, 0x10D);
    file_load(fmd_loc, 0xD6, 1, res_name(name, 0x05FE));
    rtl__fmemcpy(ds_ptr(fmd), ds_ptr(fmd_loc), 0xD6);
    DSW(dat + 0x24) = fmd;
    DSB(dat + 8) = (u8)(lbits(DSL(dat + 4)) + 1);
    s16 cs = (s16)fm_cruise_speed(fmd_loc, 10000);
    s16 v = (s16)(((s32)cs * 0x2BC) >> 10);
    DSS(dat + 0x73) = (s16)(((s32)v * DSS(0x3F22)) >> 10);
    u16 slot = DSB((u16)(0x54BC + type));
    gunlist_load(slot, res_name(name, 0x05E0), 0);
    dmg_load(slot, res_name(name, 0x05E7), 0);
    bxl_load_slot((s16)slot, res_name(name, 0x05D8));
    if (DSB(dat + 0x75) == 5 && DSW(0x269D) == 0) DSB(dat + 0x75) = 2;
    stk_release(mk);
    return dat;
}

/* AP 45cc:0510 */
void aircraft_load_all(u16 counts_ds)
{
    DSW(0x55D2) = arena_alloc((u16)(DSW(0x54BA) << 1));
    for (u16 i = 0; i < 0x9F; i++)
        if (DSW((u16)(counts_ds + 2 * i)) != 0) {
            u16 d = aircraft_load(i);
            DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + i)) * 2)) = d;
        }
}

/* AP 45cc:0556 (near, `inc bp` frame) */
static void aircraft_free(u16 dat)
{
    ace_delete(DSW(dat + 0x24), 0x13E);
    ace_delete(dat, 0x13F);
}

/* AP 45cc:059e — types 9Eh down to 0 */
void aircraft_free_all(void)
{
    for (u16 i = 0x9F; i-- != 0; )
        if (DSB((u16)(0x54BC + i)) != 0xFF) aircraft_free(DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + i)) * 2)));
}

/* AP 45cc:05ce — ship .DAT (9Ah bytes), name from the SFNAME list DS:55D4 */
static u16 ship_load(u16 type)
{
    u16 dat = ace_new(0x9A, 0x15F);
    u16 nm = res_name(far_to_near_str(ds_far((u16)(DSW(0x55D4) + type * 4))), 0x05BC);
    file_load(dat, 0x9A, 1, nm);
    return dat;
}

/* AP 45cc:0632 — gun list / damage image / box list in slot `ship slot + DS:54BA`, ship flavour */
void ship_load_all(u16 counts_ds)
{
    u16 mk = stk_mark(), name = stk_alloc(12);
    DSW(0x55D0) = arena_alloc((u16)(DSW(0x54B8) << 1));
    for (u16 i = 0; i < 0x73; i++) {
        if (DSW((u16)(counts_ds + 2 * i)) == 0) continue;
        u16 d = ship_load(i);
        DSW((u16)(DSW(0x55D0) + DSB((u16)(0x555B + i)) * 2)) = d;
        rtl_strcpy(name, far_to_near_str(ds_far((u16)(DSW(0x55D4) + i * 4))));
        u16 slot = (u16)(DSB((u16)(0x555B + i)) + DSW(0x54BA));
        gunlist_load(slot, res_name(name, 0x05E0), 1);
        dmg_load(slot, res_name(name, 0x05E7), 1);
        bxl_load_slot((s16)slot, res_name(name, 0x05D8));
    }
    stk_release(mk);
}

/* AP 45cc:0748 (near, `inc bp` frame) */
static void ship_free_dat(u16 dat) { ace_delete(dat, 0x17C); }

/* AP 45cc:076e — types 72h down to 0 */
void ship_free_all(void)
{
    for (u16 i = 0x73; i-- != 0; )
        if (DSB((u16)(0x555B + i)) != 0xFF) ship_free_dat(DSW((u16)(DSW(0x55D0) + DSB((u16)(0x555B + i)) * 2)));
}

/* AP 45cc:079e — a whole file into a far block (no callers found) */
FarPtr file_load_far(u16 name_ds)
{
    u16 f = my_fopen(name_ds, 0x05EB);
    if (!f) assert_fail(ACEDATA_C, 0x198, name_ds);
    s32 len = my_filelength(f);
    FarPtr b = mem_far_new((u32)len, 0);
    ap_fread_far_55a4(b, len, f);
    my_fclose(f);
    return b;
}

/* AP 45cc:0822 — FORMS.DAT: 198h bytes → far DS:12A8, 1C0h bytes → far DS:12A4 (allocated first) */
void forms_load(void)
{
    u16 mk = stk_mark();
    u16 a = stk_alloc(0x198), b = stk_alloc(0x1C0);
    u16 f = my_fopen(res_name(0x00B4, 0x05BC), 0x05EB);
    if (!f) assert_fail(ACEDATA_C, 0x1C9, 0);
    my_fread(a, 0x198, 1, f);
    my_fread(b, 0x1C0, 1, f);
    my_fclose(f);
    ds_far_wr(0x12A4, mem_far_new(0x1C0, 0));
    ds_far_wr(0x12A8, mem_far_new(0x198, 0));
    rtl__fmemcpy(ds_far(0x12A8), ds_ptr(a), 0x198);
    rtl__fmemcpy(ds_far(0x12A4), ds_ptr(b), 0x1C0);
    stk_release(mk);
}
