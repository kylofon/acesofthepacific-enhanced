/* sim_world — weapons (sim_world_wpn.md): the weapon class registry (488e), the ordnance pool and the
 * damage-event queue (05d6), bullets (48ff, 0769), gun lists (4cda), stations (0ff4), bombs (0688),
 * rockets (1a32), torpedoes (1baa), flak and AA (04c4), drop tanks (6cc2). Ported from the disassembly.
 *
 * Weapon objects are far 32h-byte records of the render3d pool DS:00E6. Class callbacks are far code
 * pointers stored in DS:3C28[] (update) / DS:3BE8[] (draw), exactly the values the original pushes; the
 * port registers the C functions for them with codeptr (weapons_codeptrs_register). */
#include "sim_world/swa_priv.h"
#include "codeptr.h"
#include "prog.h"

typedef void (*WpnUpdateFn)(FarPtr o);
typedef u8   (*WpnDrawFn)(FarPtr o);

#define WPOOL      DSW(0x00E6)
#define MTIME      DSL(0x4086)                  /* mission clock */
#define GUN_REC    DSW(0x134A)                  /* current .GL record */
#define GUN_ST     DSW(0x134C)                  /* current gun state entry */

static FarPtr fp32(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static u32 fpu(FarPtr p) { return (u32)p.seg << 16 | p.off; }

/* the per-frame delete list DS:00EC[DS:00EE++] */
static void wpn_delete_later(FarPtr o)
{
    u16 bx = (u16)(DSW(0x00EC) + DSW(0x00EE) * 4);
    DSW(bx + 2) = o.seg;
    DSW(bx) = o.off;
    DSW(0x00EE)++;
}

/* ==== 488e: class registry ======================================================================= */

/* AP 488e:0000 — sim_world_wpn.md §2 */
void weapons_init(u16 n)
{
    u16 si = (u16)(n + 2);
    DSW(0x00E6) = ap_pool_create(si, 0x32);
    DSW(0x00E8) = arena_alloc((u16)(si << 3));
    DSW(0x00EA) = 0;
    DSW(0x00EE) = 0;
    DSW(0x3C68) = si;
    DSW(0x3BE6) = 0;
    DSL(0x00F4) = 0;
}

/* AP 488e:004b */
void weapons_free(void)
{
    pool_destroy(WPOOL);
    DSW(0x00E6) = 0; DSW(0x00E8) = 0; DSW(0x00EA) = 0; DSW(0x00EE) = 0; DSW(0x3C68) = 0;
}

/* AP 488e:007a — stores the two far code pointers; returns the class id */
u16 weapon_class_register(FarPtr update, FarPtr draw)
{
    u16 bx = (u16)(DSW(0x3BE6) << 2);
    DSW(bx + 0x3C2A) = update.seg; DSW(bx + 0x3C28) = update.off;
    DSW(bx + 0x3BEA) = draw.seg;   DSW(bx + 0x3BE8) = draw.off;
    return DSW(0x3BE6)++;
}

/* ==== 05d6: ordnance pool, damage events ========================================================= */

/* camera bookkeeping of a weapon being deleted (05d6: the record at DS:3C90 + 6) */
static void wcam_linger(u16 di)
{
    DSB(0x3CB0) |= 2;
    DSB(0x3CB4) |= 2;
    DSB(di) = 1;
    DSL(di + 2) = MTIME + 0x1F40;
}

/* AP 05d6:0002 — sim_world_wpn.md §2.1 */
void ordnance_update(void)
{
    u16 mk = stk_mark();
    u16 list = stk_alloc(0x4B4);                   /* bp-4B4: the per-frame delete list */
    FarPtr o = far_make(0, 0);
    u16 di = (u16)(DSW(0x3C90) + 6);
    DSL(0x3CB0) &= DSL(0x3CB4);
    DSL(0x3CB4) = 0;
    DSW(0x00EC) = list;
    u16 si = (u16)(DSW(0x00EA) * 8 + DSW(0x00E8) - 8);
    while (DSW(0x00EA) != 0) {
        if (DSL(si) != 0) {
            FarPtr w = ds_far(si);
            if ((FW(w, 0x18) & 0xF) != DSW(0x54B5)) {
                if (DSL(si) == DSL(0x00F0)) DSL(0x00F0) = 0;
                if (FW(w, 0x18) & 0x8000) {
                    DSB(0x3CB0) |= 2;
                    DSB(0x3CB4) |= 2;
                    DSL(0x3C6A) = 0;
                    DSB(di) = 1;
                    DSL(di + 2) = MTIME + 0x1F40;
                    DSB(0x1AFE) = 1;
                }
                if ((FW(w, 0x18) & 0xF) != DSW(0x1344))
                    ap_pool_free_3546(WPOOL, w.off, DSW(si + 2));
            }
        }
        DSW(0x00EA)--;
        si = (u16)(si - 8);
    }
    for (;;) {
        o = fp32(pool_next(WPOOL, o));
        if (far_is_null(o)) break;
        u16 bx = (u16)((FW(o, 0x18) & 0xF) << 2);
        if (DSW(bx + 0x3C28) | DSW(bx + 0x3C2A)) {
            WpnUpdateFn f = (WpnUpdateFn)codeptr_lookup(ds_far((u16)(bx + 0x3C28)));
            f(o);
        }
    }
    if (ap_flag_5e3c_get()) {
        if (DSB(di) != 0) {
            if ((s32)(MTIME - DSL(di + 2)) > 0) { DSB(0x3CB0) |= 4; DSB(0x3CB4) |= 4; }
        } else if (DSL(0x3C6A) != 0) {
            FarPtr c = ds_far(0x3C6A);
            if (FW(c, 0x18) & 0x8000) memmove(DSP(0x3C72), far_mp(far_add(c, 4)), 12);
        }
    }
    si = (u16)(DSW(0x00EE) * 4 + DSW(0x00EC) - 4);
    while (DSW(0x00EE) != 0) {
        if (DSL(si) == DSL(0x00F0)) DSL(0x00F0) = 0;
        FarPtr w = ds_far(si);
        if (FW(w, 0x18) & 0x8000) wcam_linger(di);
        u16 bx = (u16)(DSW(0x00EE) * 4 + DSW(0x00EC));
        ap_pool_free_3546(WPOOL, DSW(bx - 4), DSW(bx - 2));
        DSW(0x00EE)--;
        si = (u16)(si - 4);
    }
    stk_release(mk);
}

/* AP 05d6:01d3 — class draw (0 = skip) then the 3Space object */
void ordnance_draw(FarPtr o)
{
    u16 bx = (u16)((FW(o, 0x18) & 0xF) << 2);
    if ((DSW(bx + 0x3BE8) | DSW(bx + 0x3BEA)) && !(FW(o, 0) & 0x2000)) {
        WpnDrawFn f = (WpnDrawFn)codeptr_lookup(ds_far((u16)(bx + 0x3BE8)));
        if (f(o) == 0) return;
    }
    u16 mk = stk_mark(), loc = stk_alloc(0x16);
    memmove(DSP(loc), far_mp(far_add(o, 2)), 0x16);
    DSW(loc + 0x14) = 0;
    ts_draw(loc);
    stk_release(mk);
}

/* AP 05d6:023e — free the oldest weapons of the fullest shape-flag buckets until n are free */
void ordnance_make_room(u16 n)
{
    u16 mk = stk_mark();
    u16 lst = stk_alloc(0x4B0), cnt = stk_alloc(0x20);
    s16 nfree = 0, bucket = 0;
    s16 si = (s16)n;
    FarPtr o = far_make(0, 0);
    memset(DSP(cnt), 0, 0x20);
    n = (u16)(n - pool_field0(WPOOL));
    for (;;) {
        o = fp32(pool_next(WPOOL, o));
        if (far_is_null(o)) break;
        DSW(cnt + (FW(o, 0x1A) & 0xFF) * 2)++;
    }
    for (u16 di = (u16)(cnt + bucket * 2); bucket < 0x10 && si != 0; di += 2, bucket++) {
        s16 ax = DSS(di);
        if (ax < si) si = (s16)(si - ax);
        else { DSS(di) = (s16)(DSS(di) - si); si = 0; }
    }
    u16 di = (u16)(lst + nfree * 4);
    for (;;) {
        o = fp32(pool_next(WPOOL, o));
        if (far_is_null(o) || (u16)si == n) break;
        u16 c = (u16)(cnt + (FW(o, 0x1A) & 0xFF) * 2);
        if (DSW(c) != 0) {
            DSW(di + 2) = o.seg; DSW(di) = o.off;
            di = (u16)(di + 4);
            nfree++;
            DSW(c)--;
            si++;
        }
    }
    u16 p = (u16)(lst - 4 + nfree * 4);
    for (; nfree != 0; nfree--, p = (u16)(p - 4)) ap_pool_free_3546(WPOOL, DSW(p), DSW(p + 2));
    stk_release(mk);
}

/* AP 05d6:0419 — next free damage event (8 bytes) or 0 */
u16 dmg_event_alloc(void)
{
    if (DSS(0x00EA) >= DSS(0x3C68)) return 0;
    u16 i = DSW(0x00EA)++;
    return (u16)(DSW(0x00E8) + i * 8);
}

/* ==== 48ff: bulletso.c =========================================================================== */

/* AP 48ff:0000 — BULLETS.MEC (16-byte records) and the bullet class */
void bullets_load(u16 name)
{
    u16 mk = stk_mark(), cnt = stk_alloc(2);
    u16 f = my_fopen(res_name(name, 0x05E3), 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x383E), 0), 0x2D, 0);
    my_fread(cnt, 2, 1, f);
    DSW(0x1346) = arena_alloc((u16)(DSW(cnt) << 4));
    my_fread(DSW(0x1346), 0x10, DSS(cnt), f);
    my_fclose(f);
    DSW(0x1348) = weapon_class_register(far_make(SEG(0x0769), 0x0637), far_make(SEG(0x0769), 0x08A3));
    stk_release(mk);
}

/* AP 48ff:009a */
void bullets_reset(void) { DSW(0x1360) = 0; }

/* ==== 0769: guns and bullets ===================================================================== */

/* AP 0769:0002 / 001a / 0032 */
u16 gun_type_speed(u16 type_ds) { return DSW((u16)(DSW(0x1346) + DSW(type_ds) * 16 + 8)); }
u16 gun_type_refire(u16 type_ds) { return DSW((u16)(DSW(0x1346) + DSW(type_ds) * 16 + 6)); }
u16 gun_type_range(u16 type) { return (u16)DSL((u16)(DSW(0x1346) + type * 16 + 0x0C)); }

/* AP 0769:004c — firing sound of a gun type */
void gun_sound(u16 p, u16 type)
{
    s8 id = (s8)DSB((u16)(type + 0x1362));
    if (p == PLAYER) sx_play_rsv((u16)(s16)id, (u16)(p + 4), (u16)(p + 0x70), 1);
    else sx_play((u16)(s16)id, (u16)(p + 4), (u16)(p + 0x70), 0);
}

/* AP 0769:03c6 — rounds left and the refire interval elapsed */
u8 gun_ready(u16 type_ds, u16 st)
{
    if (DSS(st) <= 0) return 0;
    u32 refire = (u32)(s32)DSS((u16)(DSW(0x1346) + DSW(type_ds) * 16 + 6));
    return refire <= (u32)(MTIME - DSL(st + 2));
}

/* AP 0769:0400 — spawn one bullet of the current gun group (DS:134A record, DS:134C state) */
u8 bullet_spawn(s16 side, s16 dx, s16 dz, u16 pos, u16 ang, u16 src)
{
    u16 g = (u16)(DSW(0x1346) + (DSW(GUN_REC) << 4));
    if (DSS(GUN_ST) <= 0) return 0;
    if ((u32)(s32)DSS(g + 6) > (u32)(MTIME - DSL(GUN_ST + 2))) return 0;
    if (DSW(src) == PLAYER && pool_field0(WPOOL) == 0) ordnance_make_room(1);
    FarPtr b = fp32(pool_alloc_zero(WPOOL));
    if (far_is_null(b)) return 0;
    if (DSW(src) != PLAYER || DSB(0x42CD) != 0) {
        s32 r = (s32)DSS(GUN_ST) - DSS((u16)(DSW(GUN_REC) * 2 + 0x134E));
        DSS(GUN_ST) = r >= 0 ? (s16)r : 0;      /* sub / jge: floor at 0 */
    }
    DSL(GUN_ST + 2) = MTIME;
    FW(b, 0) = 0x805;
    FW(b, 2) = (u16)(DSW(0x474D) + 4);
    FW(b, 0x10) = DSW(ang); FW(b, 0x12) = DSW(ang + 2); FW(b, 0x14) = DSW(ang + 4);
    memmove(far_mp(far_add(b, 4)), DSP(pos), 12);
    if (DSW(src) == PLAYER && side >= 0) {
        FW(b, 0x10) = (u16)(FW(b, 0x10) + DSW(GUN_REC + 0x0C));
        u16 ax = side ? DSW(GUN_REC + 0x0E) : (u16)-DSW(GUN_REC + 0x0E);
        FW(b, 0x14) = (u16)(FW(b, 0x14) + ax);
    } else {
        if (side < 0) side = 0;
        FW(b, 0x14) = (u16)(FW(b, 0x14) + (u16)(side ? -dz : dz));
        FW(b, 0x10) = (u16)(FW(b, 0x10) + (u16)dx);
    }
    if (DSW(src) != 0) {
        u16 mk = stk_mark(), off = stk_alloc(6), rot = stk_alloc(6);
        DSW(off) = DSW(GUN_REC + 6); DSW(off + 2) = DSW(GUN_REC + 8); DSW(off + 4) = DSW(GUN_REC + 0x0A);
        if (side) DSW(off) = (u16)-DSW(off);
        vec_rotate(off, obj_matrix(OW(DSW(src), 0x20)), rot);
        ladd_v_c(far_add(b, 4), rot);
        stk_release(mk);
    }
    dir_from_angles_far(far_add(b, 0x22), fpu(far_add(b, 0x10)));
    FW(b, 0x16) = DSW(src);
    FW(b, 0x1A) = DSW(g + 2) | DSW(src + 2);
    FW(b, 0x18) = (u16)(DSW(GUN_REC) << 4) | DSW(0x1348);
    FW(b, 0x28) = DSW(g + 0x0A);
    FW(b, 0x2A) = DSW(src + 4);
    FW(b, 0x2C) = DSW(GUN_REC + 2);
    return 1;
}

/* one fixed group of gun_fire (A: bit 0, jam DS:4366, gauge 3FE2, bit 20h; B: bit 1, 4364, 3FE4, 40h) */
static s16 gun_group(u16 p, u16 aim, u16 src, u16 flags, int grp, u8 *fired, s16 *snd)
{
    u16 jam = grp ? 0x4364 : 0x4366;
    if (DSW(jam) != 0 && PLAYER == p) {
        msg_show(str(1, 5), 1, far_make(0, 0));
        return -1;
    }
    if (DSW(GUN_ST) == 0) return 0;
    *snd = (s16)DSW(GUN_REC);
    u16 single = grp ? ((flags & 0x500) == 0x100) : ((flags & 0x2800) == 0x800);
    s16 side = (single || (DSW(GUN_ST + 0x0A) & 0x8000)) ? 0 : 1;
    *fired |= bullet_spawn(side, DSS(GUN_ST + 6), DSS(GUN_ST + 8), (u16)(p + 4), aim, src);
    if (DSW(src) == PLAYER) {
        if (*fired && (OB(PLAYER, 0x1B) & 8) && DSS(0x579B) < (s16)0xFBC0 && DSB(0x42CA) != 0) {
            DSW(jam) = 1;
            guns_jammed();
        } else {
            DSW(0x4366) = 0;                 /* quirk: group B clears the group A jam flag */
        }
        u16 gauge = grp ? 0x3FE4 : 0x3FE2;
        DSW(gauge) = DSW(GUN_ST);
        if (DSW(gauge) == 0) {
            DSB(0x57E4) = 1;
            DSB(0x3FEE) &= grp ? 0xBF : 0xDF;
        }
    }
    if (*fired) {
        if (grp) DSW(0x1360) |= (u16)((side + 1) << 2);
        else DSW(0x1360) = (u16)(side + 1);
        DSW(GUN_ST + 0x0A) ^= 0xC000;
    }
    return 0;
}

/* AP 0769:009a — sim_world_wpn.md §3.2 */
u16 gun_fire(s16 trigger, u16 p, u16 aim, u16 src)
{
    u16 flags = OW(OW(p, 0x5E), 2);
    u8 fired = 0;
    s16 snd = -1;
    DSW(0x134A) = (u16)(OW(p, 0x5E) + 4);
    DSW(0x134C) = OW(p, 0x60);
    DSW(0x1360) = 0;
    if (trigger >= 4) {
        s16 k = (s16)(((flags & 0x2800) ? 1 : 0) + ((flags & 0x500) ? 1 : 0));
        s16 i = (s16)(trigger + k);
        DSW(0x134A) = (u16)(DSW(0x134A) + (u16)(i * 0x14 - 0x50));
        DSW(0x134C) = (u16)(DSW(0x134C) + (u16)(i * 0x0C - 0x30));
        gun_sound(p, DSW(GUN_REC));
        return bullet_spawn(-1, DSS(GUN_ST + 6), DSS(GUN_ST + 8), (u16)(p + 4), aim, src);
    }
    if (trigger == 0) {
        if (!(flags & 0xC000)) return 0;
        trigger = (s16)((s16)flags >> 14);
    }
    if ((trigger & 1) && (flags & 0x2800)) {
        if (gun_group(p, aim, src, flags, 0, &fired, &snd) < 0) return 0;
    }
    if ((trigger & 2) && (flags & 0x500)) {
        if (flags & 0x2800) { DSW(0x134A) = (u16)(DSW(0x134A) + 0x14); DSW(0x134C) = (u16)(DSW(0x134C) + 0x0C); }
        if (gun_group(p, aim, src, flags, 1, &fired, &snd) < 0) return 0;
    }
    if (DSW(src) == PLAYER && DSB(0x42CD) != 0 && DSW(0x3FE2) == 0 && DSW(0x3FE4) == 0) DSB(0x42BD) = 1;
    if (snd != -1) gun_sound(p, (u16)snd);
    return fired;
}

/* AP 0769:0637 — sim_world_wpn.md §3.4 */
void bullet_update(FarPtr b)
{
    u16 mk = stk_mark();
    u16 dir = stk_alloc(6), old = stk_alloc(12), step = stk_alloc(12), hit = stk_alloc(12), hp = stk_alloc(12);
    u16 type = (u16)((s16)(FW(b, 0x18) & 0x3F0) >> 4);
    u16 g = (u16)(DSW(0x1346) + (type << 4));
    memmove(DSP(dir), far_mp(far_add(b, 0x22)), 6);
    if (FS(b, 0x28) <= 0) { wpn_delete_later(b); stk_release(mk); return; }
    timer16_dec_far(far_add(b, 0x28));
    memmove(DSP(old), far_mp(far_add(b, 4)), 12);
    v2l(step, dir);
    lvsar(lscale(step, (u16)dt_scale(DSS(g + 8))), 0x0E);
    ladd_far(far_add(b, 4), step);
    if (FW(b, 0x16) != 0) {
        u32 h = segment_hits_ground_object(fpu(far_add(b, 4)), (u32)old);
        DSL(hit) = h;
        if (h) {
            if (FSL(b, 0x0C) < 0) splash_point(old, fpu(far_add(b, 4)));
            FW(b, 0x28) = 0;
            ground_object_damage(hit, DSW(g + 4), type, fpu(b));
            stk_release(mk);
            return;
        }
    }
    FW(b, 0x1C) = (u16)(DSW(old) - FW(b, 4));
    FW(b, 0x1E) = (u16)(DSW(old + 4) - FW(b, 8));
    FW(b, 0x20) = (u16)(DSW(old + 8) - FW(b, 0x0C));
    hitbox_set_enlarge(FW(b, 0x16) == PLAYER ? (u16)(2 - (s16)(s8)DSB(0x42D2)) : 3);
    u16 dmg = (u16)(DSW(g + 4) * FW(b, 0x2C));
    if (FW(b, 0x2A) != 0) {
        u16 t = segment_hits_object(old, fpu(far_add(b, 4)), 0xFFFF, b, dmg, hp);
        if (t) {
            hit_spark(t, hp, (u16)(DSW(0x474D) + 0x27));
            ai_was_hit(t);
            stk_release(mk);
            return;
        }
    }
    if (FSL(b, 0x0C) < 0) {
        splash_point(old, fpu(far_add(b, 4)));
        FW(b, 0x28) = 0;
        splash_or_dust(fpu(far_add(b, 4)), (u16)(DSW(0x474D) + 0x13), (u16)(DSW(0x474D) + 0x12));
        stk_release(mk);
        return;
    }
    if (ground_height_at(hit, far_add(b, 4)) && FSL(b, 0x0C) > DSSL(hit + 4)) FW(b, 0x28) = 0;
    stk_release(mk);
}

/* AP 0769:08a3 — tracer: the bullet shape drawn at pos + (old - pos) with a nearer near plane */
u8 bullet_draw(FarPtr b)
{
    if (FW(b, 0) & 0x2000) return 0;
    u16 mk = stk_mark(), o = stk_alloc(0x16);
    memset(DSP(o), 0, 0x16);
    DSW(o) = (u16)(DSW(0x474D) + 4);
    memmove(DSP(o + 2), far_mp(far_add(b, 4)), 12);
    memmove(DSP(o + 0x0E), far_mp(far_add(b, 0x10)), 6);
    DSSL(o + 2) += FS(b, 0x1C);
    DSSL(o + 6) += FS(b, 0x1E);
    DSSL(o + 0x0A) += FS(b, 0x20);
    set_near((u16)(get_near_n() - 4));
    SEGB(0x277A, 0x09B2)++;
    ts_draw(o);
    SEGB(0x277A, 0x09B2)--;
    reset_near();
    stk_release(mk);
    return 0;
}

/* ==== 4cda: gunlist.c ============================================================================ */

/* AP 4cda:0000 */
void gunlist_loadout_free(void)
{
    mem_far_delete(ds_far(0x1546));
    DSL(0x154E) = 0; DSL(0x154A) = 0; DSL(0x1546) = 0;
}

/* AP 4cda:0021 */
void gunlists_alloc(s16 n) { DSW(0x030C) = arena_alloc((u16)(n << 1)); }

/* AP 4cda:0037 — .GL image: u16 n, then flags and n records of 20 (aircraft) or 10 bytes (ships) */
u16 gunlist_load(u16 slot, u16 name, u16 is_ship)
{
    u16 mk = stk_mark(), cnt = stk_alloc(2);
    u16 f = my_fopen(name, 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x3861), 0), 0x69, name);
    my_fread(cnt, 2, 1, f);
    u16 size = (u16)(((u8)is_ship ? DSS(cnt) * 10 : DSS(cnt) * 0x14) + 4);
    u16 di = arena_alloc(size);
    DSW(di) = DSW(cnt);
    my_fread((u16)(di + 2), 1, (s16)(size - 2), f);
    DSW((u16)(DSW(0x030C) + slot * 2)) = di;
    my_fclose(f);
    stk_release(mk);
    return DSW((u16)(DSW(0x030C) + slot * 2));
}

/* AP 4cda:02f3 — rounds per group (<< shift: AI planes carry double) and the group flag bits */
void gun_state_init(u16 ref, u8 shift)
{
    u16 gl = DSW(ref);
    u16 dx = (u16)(gl + 2);
    u16 di = (u16)(gl + 4);
    DSW(dx) &= 0x3FFF;
    u16 flags = DSW(dx);
    u16 si = DSW(ref + 2);
    u8 cl = shift & 31;
    if (flags & 0x2800) {
        DSW(si + 0x0A) |= (flags & 0x800) ? 0xC000 : 0x8000;
        DSW(si) = (u16)(DSW(di + 4) << cl);
        DSW(dx) |= 0x4000;
        di = (u16)(di + 0x14); si = (u16)(si + 0x0C);
    }
    if (flags & 0x500) {
        DSW(si + 0x0A) |= (flags & 0x100) ? 0xC000 : 0x8000;
        DSW(si) = (u16)(DSW(di + 4) << cl);
        if (!(flags & 0x2800)) DSW(dx) |= 0x8000;
        di = (u16)(di + 0x14); si = (u16)(si + 0x0C);
    }
    s16 n = (s16)(flags & 0xFF);
    for (s16 i = 0; n > i; i++) {
        DSW(si) = (u16)(DSW(di + 4) << cl);
        si = (u16)(si + 0x0C); di = (u16)(di + 0x14);
    }
}

/* AP 4cda:00f8 — gunlist_attach(slot, &obj+5E, &obj+60, obj): gun state array (12-byte entries for
 * aircraft, 26h-byte AA entries for ships); the player also loads the .GLT loadout names */
u16 gunlist_attach(u16 slot, u16 gl_out, u16 st_out, u16 obj)
{
    u8 ship = OB(obj, 0x18) == 1;
    s16 n = DSS(DSW((u16)(DSW(0x030C) + slot * 2)));
    u16 size = (u16)(n * (ship ? 0x26 : 0x0C));
    u16 st = arena_alloc(size);
    memset(DSP(st), 0, size);
    u16 mk = stk_mark(), ref = stk_alloc(4);
    DSW(ref) = DSW((u16)(DSW(0x030C) + slot * 2));
    DSW(gl_out) = DSW(ref);
    DSW(ref + 2) = st;
    DSW(st_out) = st;
    if (ship) {
        u16 si = st, di = (u16)(DSW((u16)(DSW(0x030C) + slot * 2)) + 4);
        for (; n > 0; n--, si = (u16)(si + 0x26), di = (u16)(di + 10)) {
            DSW(si + 4) = DSW(di + 2);
            DSB(si + 0x25) = DSB(di);
            DSB(si + 0x24) = OB(obj, 0x26);
            DSW(si + 2) = cur_dgroup;
            DSW(si) = obj;
        }
    } else if (PLAYER == obj) {
        gun_state_init(ref, 0);
        u16 f = my_fopen(res_name(0x3FF0, 0x030E), 0x0312);
        if (!f) assert_fail(far_make(SEG(0x3861), 0), 0xAF, 0);
        s16 len = (s16)my_filelength(f);
        FarPtr b = mem_far_new((u32)(s32)len, 0);
        ds_far_wr(0x1546, b);
        ap_fread_far_55a4(b, (s32)len, f);
        my_fclose(f);
        DSW(0x154C) = DSW(0x1548); DSW(0x154A) = (u16)(DSW(0x1546) + 1);
        for (;;) { FarPtr q = ds_far(0x154A); DSW(0x154A)++; if (FB(q, 0) == 0) break; }
        DSW(0x1550) = DSW(0x154C); DSW(0x154E) = (u16)(DSW(0x154A) + 1);
        for (;;) { FarPtr q = ds_far(0x154E); DSW(0x154E)++; if (FB(q, 0) == 0) break; }
        u16 fl = DSW(DSW(ref) + 2);
        u16 si = DSW(ref + 2);
        DSB(0x3FEE) &= 0xBF;
        DSB(0x3FEE) &= 0xDF;
        if (fl & 0x2800) { DSW(0x3FE2) = DSW(si); DSB(0x3FEE) |= 0x20; si = (u16)(si + 0x0C); }
        if (fl & 0x500) {
            DSW(0x3FE4) = DSW(si);
            if (!(fl & 0x2800)) DSB(0x3FEE) |= 0x40;
        }
    } else {
        gun_state_init(ref, 1);
    }
    stk_release(mk);
    return st;
}

/* ==== 0688: bombs.c ============================================================================== */

#define LD3(a, b) ((s32)ldist3((a), (b)))

/* AP 0688:0004 — BOMBS.MEC (10-byte records) and the bomb class (update only) */
void bombs_load(u16 name)
{
    u16 mk = stk_mark(), cnt = stk_alloc(2);
    u16 f = my_fopen(res_name(name, 0x05E3), 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x39DD), 0), 0x59, 0);
    my_fread(cnt, 2, 1, f);
    DSW(0x1342) = arena_alloc((u16)(DSS(cnt) * 10));
    my_fread(DSW(0x1342), 10, DSS(cnt), f);
    my_fclose(f);
    DSW(0x1344) = weapon_class_register(far_make(SEG(0x0688), 0x04E3), far_make(0, 0));
    stk_release(mk);
}

/* AP 0688:009d — bomb_release(stick, pos, carrier matrix block, FireSrc) — sim_world_wpn.md §4.1 */
u16 bomb_release(u16 stick, u16 pos, u16 mblk, u16 src)
{
    u16 mk = stk_mark();
    u16 v = stk_alloc(6), a = stk_alloc(6), sp = stk_alloc(6), tgt = stk_alloc(12), cur = stk_alloc(12);
    s16 n = (s8)DSB(stick);
    u16 idx = (u16)(stick + 1);
    if (pool_field0(WPOOL) < (u16)n) ordnance_make_room((u16)n);
    s16 lead = rng_mod(RNG_ST, n);
    DSW(v) = DSW(src + 0x10); DSW(v + 2) = DSW(src + 0x12); DSW(v + 4) = DSW(src + 0x14);
    vec_rotate(v, obj_matrix(mblk), a);
    DSW(v) = (u16)-DSW(v);
    vec_rotate(v, obj_matrix(mblk), sp);
    vsub(sp, a);
    if (n == 1) {
        vsar(sp, 1);
        vadd(a, sp);
    } else if (n == 2) {
        for (int k = 0; k < 6; k += 2) DSS(sp + k) = (s16)(DSS(sp + k) / 3);
        vadd(a, sp);
    } else {
        for (int k = 0; k < 6; k += 2) DSS(sp + k) = (s16)(DSS(sp + k) / n);
    }
    if (n > 1 && (n & 1))
        for (int k = 0; k < 6; k += 2) DSS(a + k) = (s16)(DSS(a + k) + DSS(sp + k) / 2);
    u16 ang = DSW(mblk + 0x28);
    for (s16 i = 0; i < n; i++, idx++) {
        FarPtr b = fp32(pool_alloc_zero(WPOOL));
        u16 mec = (u16)(DSW(0x1342) + (s8)DSB(idx) * 10);
        FW(b, 0) = 5;
        FW(b, 2) = (u16)(DSW(0x474D) + DSW(mec) + 7);
        FW(b, 0x18) = (u16)(((s16)(s8)DSB(idx) << 4) | DSW(0x1344) | 0x2000);
        if (i == 0) FW(b, 0x18) |= 0x4000;
        FW(b, 0x1A) = DSW(mec + 2) | 0x0F;
        memmove(far_mp(far_add(b, 4)), DSP(pos), 12);
        ladd_v_c(far_add(b, 4), a);
        FW(b, 0x10) = DSW(ang); FW(b, 0x12) = DSW(ang + 2); FW(b, 0x14) = DSW(ang + 4);
        FW(b, 0x16) = DSW(src);
        u16 q = DSW(src);
        if (q == PLAYER) {
            FarPtr vv = far_add(b, 0x1C);
            memmove(far_mp(vv), DSP(q + 0x70), 6);
            s16 z = FS(vv, 4);
            FS(vv, 4) = (s16)-(s16)(z < 0 ? -z : z);
            FW(vv, 6) = (u16)((ap_rng_next(RNG_ST) & 7) + 0x0D);
            wcam_note_weapon(b);
        } else {
            u16 si = (u16)(q + 0x70);
            u16 s = (u16)ldist2v((u32)(s32)abs16(DSS(si)), (u32)(s32)abs16(DSS(si + 2)));
            FSL(b, 0x24) = (s32)(s16)s << 8;
            FW(b, 0x28) = (u16)((s16)((n - i) * 5) - DSS(si + 4));
            memmove(DSP(tgt), DSP(src + 2), 12);
            if (i != lead) {
                u16 h = DSW(ang + 4);
                s32 e = (s32)(s16)(lead - i) * (s16)mul_sin((s16)s, h);
                DSSL(tgt) += e + rng_mod(RNG_ST, (s16)((s16)s / n));
                e = (s32)(s16)(lead - i) * (s16)mul_cos((s16)s, h);
                DSSL(tgt + 4) += e + rng_mod(RNG_ST, (s16)((s16)s / n));
            }
            u16 h = DSW(ang + 4);
            FSL(b, 0x1C) = DSSL(tgt) + (s16)mul_cos(DSS(a), h);
            FSL(b, 0x20) = DSSL(tgt + 4) + (s16)mul_sin(DSS(a + 2), h);
            memmove(DSP(cur), far_mp(far_add(b, 4)), 12);
            ldist2(cur, tgt);                                  /* result unused */
            FL(b, 0x2A) = 0;
        }
        vadd(a, sp);
    }
    stk_release(mk);
    return 1;
}

/* AP 0688:0ce2 — cur moved toward target by at most step */
s32 clamp_toward(s32 cur, s16 step, s32 target)
{
    if (cur > target) { s32 v = cur - step; return v <= target ? target : v; }
    s32 v = cur + step;
    return v >= target ? target : v;
}

/* AP 0688:0d40 — blast damage events to every aircraft within the MEC radius */
void bomb_blast(FarPtr b)
{
    u16 mk = stk_mark(), pos = stk_alloc(12);
    memmove(DSP(pos), far_mp(far_add(b, 4)), 12);
    u16 mec = (u16)(DSW(0x1342) + ((s16)(FW(b, 0x18) & 0x3F0) >> 4) * 10);
    s32 r = DSS(mec + 8);
    u16 di = DSW(0x475B);
    for (s16 i = 0; i < DSS(0x4776); i++, di += 2) {
        u16 q = DSW(di);
        if ((u32)LD3(pos, (u16)(q + 4)) > (u32)r) continue;
        if (q == PLAYER && FW(b, 0x16) == PLAYER && DSB(0x42CF) != 0) DSB(0x57E7) = 1;
        u16 e = dmg_event_alloc();
        if (e) {
            DSW(e) = b.off; DSW(e + 2) = b.seg;
            DSW(e + 4) = q;
            DSW(e + 6) = DSW(mec + 6);
        }
    }
    stk_release(mk);
}

/* damage of one ground sub-object by a bomb (the common part of both loops of bomb_explode) */
static void bomb_hit_object(FarPtr dmgp, u16 shape, u16 opos, s16 blast, FarPtr grp, FarPtr rec, u16 idx, FarPtr b)
{
    u32 v = FB(dmgp, 0);
    u16 di = (u16)(0x5A82 + (objtype_class((u16)(shape & 0x1FFF)) << 3));
    v = (u32)((s32)v + (((s32)blast * DSS(di + 6)) >> 8));
    if ((s32)v >= 0xFF) {
        u16 dx = DSW(di + 2);
        FB(dmgp, 0) = 0xFF;
        if (dx) explosion_spawn(opos, dx, (u16)(DSW(0x474D) + 0x1C));
    } else {
        FB(dmgp, 0) = (u8)v;
    }
    ground_credit(grp, fpu(rec), idx, b, (u16)((u16)v - FB(dmgp, 0)));
}

/* AP 0688:08b7 — sim_world_wpn.md §4.3 */
void bomb_explode(FarPtr b)
{
    u16 mk = stk_mark();
    u16 pos = stk_alloc(12), opos = stk_alloc(12), list = stk_alloc(0x50);
    u8 hit = 0;
    memmove(DSP(pos), far_mp(far_add(b, 4)), 12);
    u16 mec = (u16)(DSW(0x1342) + ((s16)(FW(b, 0x18) & 0x3F0) >> 4) * 10);
    if (!(u8)splash_or_dust((u32)cur_dgroup << 16 | pos, (u16)(DSW(0x474D) + 0x18), (u16)(DSW(0x474D) + 0x10))) {
        stk_release(mk);
        return;
    }
    s16 blast = DSS(mec + 6);
    s32 r = DSS(mec + 8);
    bomb_blast(b);
    if (FW(b, 0x16) == PLAYER) {
        FarPtr so = ds_far(0x06F3);
        s16 n = (s16)collect_objects_near(far_add(so, (u16)(DSW(0x5130) << 2)), (u16)(DSW(0x512E) - DSW(0x5130)),
                                          pos, list, 0x14, (u32)(r + 100000));
        for (s16 i = 0; i < n; i++) {
            FarPtr g = ds_far((u16)(list + i * 4));
            memmove(DSP(opos), far_mp(far_add(g, 4)), 12);
            u32 d = (u32)LD3(pos, opos);
            if (d > (u32)(macro_distance(g) + (u32)r + 50000)) continue;
            s16 cnt = (s16)macro_expand(g);
            FarPtr mem = far_make(SEG(0x3B05), 0);
            FarPtr dmgp = far_add(g, 0x12);
            for (s16 j = 0; j < cnt; j++, mem = far_add(mem, 0x16), dmgp = far_add(dmgp, 1)) {
                if ((u8)shape_layer_cached(FW(mem, 2)) != 0) break;
                memmove(DSP(opos), far_mp(far_add(mem, 4)), 12);
                if (FB(dmgp, 0) == 0xFF) continue;
                if ((u32)LD3(pos, opos) > (u32)(shape_radius_world((s16)FW(mem, 2)) + r)) continue;
                bomb_hit_object(dmgp, FW(mem, 2), opos, blast, g, mem, (u16)j, b);
                hit = 1;
            }
        }
    } else {
        FarPtr ent = ds_far(0x5146), back = ds_far(0x5142);
        for (s16 i = 0; i < DSS(0x5140); i++, ent = far_add(ent, 0x16), back = far_add(back, 6)) {
            memmove(DSP(opos), far_mp(far_add(ent, 4)), 12);
            FarPtr grp = far_make(FW(back, 2), FW(back, 0));
            FarPtr dmgp = far_make(FW(back, 2), (u16)(FW(back, 0) + FW(back, 4) + 0x12));
            if (FB(dmgp, 0) == 0xFF) continue;
            if ((u32)LD3(pos, opos) > (u32)(shape_radius_world((s16)FW(ent, 2)) + r)) continue;
            bomb_hit_object(dmgp, FW(ent, 2), opos, blast, grp, ent, FW(back, 4), b);
            hit = 1;
        }
    }
    if (!hit) hit_sound(b, 0);
    stk_release(mk);
}

/* AP 0688:04e3 — sim_world_wpn.md §4.2 */
void bomb_update(FarPtr b)
{
    if (FW(b, 0x18) & 0x2000) { FW(b, 0x18) &= 0xDFFF; return; }
    u16 mk = stk_mark();
    u16 old = stk_alloc(12), pos = stk_alloc(12), vel = stk_alloc(6), tgt = stk_alloc(12), ang = stk_alloc(6),
        hp = stk_alloc(12);
    memmove(DSP(old), far_mp(far_add(b, 4)), 12);
    if (FW(b, 0x16) == PLAYER) {
        FarPtr v = far_add(b, 0x1C);
        s32 eax = (s32)FS(v, 6) * -0x2000;
        FW(b, 0x10) = (u16)ldiv_sat(eax, 0x3C);
        /* quirk (x86dis 0688:0557..058d): `mov ax,v; cdq; xor ax,dx; sub ax,dx` takes the sign from the
         * high word of EAX, still g·−2000h (ldiv_sat is 16-bit code): with g > 0 it always negates, so
         * only negative horizontal components decay by v >> 3 */
        s16 x = FS(v, 0);
        if ((s16)(eax < 0 ? -x : x) > 2) FW(v, 0) = (u16)(FW(v, 0) - (u16)(x >> 3));
        x = FS(v, 2);
        if ((s16)(eax < 0 ? -x : x) > 2) FW(v, 2) = (u16)(FW(v, 2) - (u16)(x >> 3));
        FW(v, 4) = (u16)(FW(v, 4) - FW(v, 6));
        if (FS(v, 6) <= 0x3C) FW(v, 6) = (u16)(FW(v, 6) + 0x0D);
        FSL(b, 4) += dt_scale(FS(v, 0));
        FSL(b, 8) += dt_scale(FS(v, 2));
        FSL(b, 0x0C) += dt_scale(FS(v, 4));
        if (FSL(b, 0x0C) > 0 && (FW(b, 0x18) & 0x4000)) {
            memmove(DSP(pos), far_mp(far_add(b, 4)), 12);
            memmove(DSP(vel), far_mp(v), 6);
            sx_play(2, pos, vel, 1);
        }
    } else {
        memmove(DSP(pos), far_mp(far_add(b, 4)), 12);
        s32 dist = FSL(b, 0x2A);
        FarPtr ft = far_add(b, 0x24);
        DSSL(tgt) = FSL(b, 0x1C); DSSL(tgt + 4) = FSL(b, 0x20); DSSL(tgt + 8) = 0;
        angles_to(ang, tgt, pos);
        s32 vz_old = FS(b, 0x28);
        FW(b, 0x28) = (u16)(FW(b, 0x28) + (u16)dt_scale(0x0D));
        s16 vz = (s16)((vz_old + FS(b, 0x28)) >> 1);
        s32 t_old = FSL(ft, 0);
        FSL(ft, 0) -= dt_scale((s16)dist);
        if (FSL(ft, 0) < 0) FSL(ft, 0) = 0;
        s16 si = (s16)((t_old + FSL(ft, 0)) >> 9);
        s16 dx = (s16)(((s32)si * sin16(DSW(ang + 4))) >> 14); dx = (s16)(dx < 0 ? -dx : dx);
        s16 dy = (s16)(((s32)si * (s16)cos16(DSW(ang + 4))) >> 14); dy = (s16)(dy < 0 ? -dy : dy);
        FSL(b, 4) = clamp_toward(FSL(b, 4), dx, FSL(b, 0x1C));
        FSL(b, 8) = clamp_toward(FSL(b, 8), dy, FSL(b, 0x20));
        FSL(b, 0x0C) = clamp_toward(FSL(b, 0x0C), vz, 0);
        FW(b, 0x10) = (u16)-atan2_16((u16)(FSL(ft, 0) >> 8), (u16)vz);
    }
    u16 mec = (u16)(DSW(0x1342) + ((s16)(FW(b, 0x18) & 0x3F0) >> 4) * 10);
    if (segment_hits_object(old, fpu(far_add(b, 4)), 1, b, DSW(mec + 4), hp)) {
        bomb_blast(b);
        wpn_delete_later(b);
    } else if (FSL(b, 0x0C) <= 0) {
        FSL(b, 0x0C) = 0;
        bomb_explode(b);
        if (!(FW(b, 0) & 0x2000)) wpn_delete_later(b);
    }
    stk_release(mk);
}

/* ==== 1a32: rockets.c ============================================================================ */

/* AP 1a32:000b */
void rockets_load(u16 name)
{
    u16 mk = stk_mark(), cnt = stk_alloc(2);
    u16 f = my_fopen(res_name(name, 0x05E3), 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x3A67), 0), 0x4E, 0);
    my_fread(cnt, 2, 1, f);
    DSW(0x1938) = arena_alloc((u16)(DSS(cnt) * 10));
    my_fread(DSW(0x1938), 10, DSS(cnt), f);
    my_fclose(f);
    DSW(0x193A) = weapon_class_register(far_make(SEG(0x1A32), 0x02F4), far_make(SEG(0x1A32), 0x0531));
    stk_release(mk);
}

/* AP 1a32:00a7 — rocket_fire(rack, pos, carrier matrix block, FireSrc) */
u16 rocket_fire(u16 rack, u16 pos, u16 mblk, u16 src)
{
    s16 n = (s8)DSB(rack);
    if (n == 0) return 0;
    n = (s8)DSB(rack + 1);
    s16 r;
    u16 bp;
    u8 bit;
    do {
        r = rng_mod(RNG_ST, n);
        bp = (u16)(rack + 2 + (r >> 3));
        bit = (u8)(r & 7);
    } while (!((1u << bit) & DSB(bp)));
    u16 is_player = DSW(src) == PLAYER;
    if (!is_player || DSB(0x42CD) != 0) {
        DSB(bp) &= (u8)~(1u << bit);
        DSB(rack)--;
        if (is_player) DSW(0x3FE6) = (u16)(s16)(s8)DSB(rack);
    }
    FarPtr b = fp32(pool_alloc_zero(WPOOL));
    if (far_is_null(b)) return 0;
    u16 mk = stk_mark(), v = stk_alloc(6), a = stk_alloc(6), sp = stk_alloc(6);
    u16 mec = DSW(0x1938);
    DSW(v) = DSW(src + 6); DSW(v + 2) = DSW(src + 8); DSW(v + 4) = DSW(src + 0x0A);
    vec_rotate(v, mblk, a);
    DSW(v) = (u16)-DSW(v);
    vec_rotate(v, mblk, sp);
    vsub(sp, a);
    if (n > 1 && (n & 1))
        for (int k = 0; k < 6; k += 2) DSS(a + k) = (s16)(DSS(a + k) + DSS(sp + k) / 2);
    for (int k = 0; k < 6; k += 2) DSS(a + k) = (s16)(DSS(a + k) + (s16)((s32)DSS(sp + k) * r / n));
    FW(b, 0) = 5;
    FW(b, 2) = (u16)(DSW(0x474D) + DSW(mec) + 0x0B);
    FB(b, 0x26) = 0;
    u16 ang = DSW(mblk + 0x28);
    FW(b, 0x10) = DSW(ang); FW(b, 0x12) = DSW(ang + 2); FW(b, 0x14) = DSW(ang + 4);
    dir_from_angles_far(far_add(b, 0x1C), fpu(far_add(b, 0x10)));
    memmove(far_mp(far_add(b, 4)), DSP(pos), 12);
    ladd_v_c(far_add(b, 4), a);
    FW(b, 0x16) = DSW(src);
    if (DSW(src) == PLAYER) wcam_note_weapon(b);
    FW(b, 0x1A) = DSW(mec + 2) | DSW(src + 2);
    FW(b, 0x18) = DSW(0x193A);
    FW(b, 0x22) = DSW(src + 4);
    FW(b, 0x24) = DSW(mec + 8);
    stk_release(mk);
    return 1;
}

static u32 dsfar(u16 off) { return (u32)cur_dgroup << 16 | off; }

/* AP 1a32:02f4 */
void rocket_update(FarPtr b)
{
    u16 mec = (u16)(DSW(0x1938) + ((s16)(FW(b, 0x18) & 0x3F0) >> 4) * 10);
    u16 mk = stk_mark();
    u16 dir = stk_alloc(6), old = stk_alloc(12), step = stk_alloc(12), hit = stk_alloc(12), hp = stk_alloc(12);
    memmove(DSP(dir), far_mp(far_add(b, 0x1C)), 6);
    if (FW(b, 0x24) == 0) {
        DSB(0x3CB0) |= 4; DSB(0x3CB4) |= 4;
        wpn_delete_later(b);
        stk_release(mk);
        return;
    }
    timer16_dec_far(far_add(b, 0x24));
    if (FS(b, 0x22) < DSS(mec + 6)) {
        s16 cx = (s16)(FW(b, 0x22) + (u16)dt_scale(100));
        FS(b, 0x22) = DSS(mec + 6) >= cx ? cx : DSS(mec + 6);
    }
    memmove(DSP(old), far_mp(far_add(b, 4)), 12);
    v2l(step, dir);
    lvsar(lscale(step, (u16)dt_scale(FS(b, 0x22))), 0x0E);
    ladd_far(far_add(b, 4), step);
    u32 h = segment_hits_ground_object(fpu(far_add(b, 4)), dsfar(old));
    DSL(hit) = h;
    if (h) {
        if (FSL(b, 0x0C) < 0) splash_point(old, fpu(far_add(b, 4)));
        FW(b, 0x24) = 0;
        ground_object_damage(hit, DSW(mec + 4), 0xC8, fpu(b));
        stk_release(mk);
        return;
    }
    u16 t = segment_hits_object(old, fpu(far_add(b, 4)), 0xFFFF, b, DSW(mec + 4), hp);
    if (t) {
        if (OB(t, 0x18) == 1) hit_spark(t, hp, (u16)(DSW(0x474D) + 0x1C));
        stk_release(mk);
        return;
    }
    if (FSL(b, 0x0C) < 0) {
        splash_point(old, fpu(far_add(b, 4)));
        FW(b, 0x24) = 0;
        splash_or_dust(fpu(far_add(b, 4)), (u16)(DSW(0x474D) + 0x13), (u16)(DSW(0x474D) + 0x12));
        stk_release(mk);
        return;
    }
    if (ground_height_at(hit, far_add(b, 4)) && FSL(b, 0x0C) > DSSL(hit + 4)) FW(b, 0x24) = 0;
    stk_release(mk);
}

/* local 3Space draw record of the rocket/torpedo/flak draw callbacks: +0 shape, +2 pos, +0E angles,
 * +14 pointer to the state byte (+17) */
static u16 draw_record(FarPtr b, u8 state, int with_angles)
{
    u16 o = stk_alloc(0x18);
    DSB(o + 0x17) = state;
    DSW(o) = FW(b, 2);
    memmove(DSP(o + 2), far_mp(far_add(b, 4)), 12);
    if (with_angles) memmove(DSP(o + 0x0E), far_mp(far_add(b, 0x10)), 6);
    else memset(DSP(o + 0x0E), 0, 6);
    DSW(o + 0x14) = (u16)(o + 0x17);
    return o;
}

/* AP 1a32:0531 — exhaust frame toggles */
u8 rocket_draw(FarPtr b)
{
    u16 mk = stk_mark();
    u16 o = draw_record(b, FB(b, 0x26), 1);
    FB(b, 0x26) ^= 1;
    ts_draw(o);
    stk_release(mk);
    return 0;
}

/* ==== 1baa: torps.c ============================================================================== */

/* AP 1baa:0005 */
u16 torp_speed_limit(s16 side) { return DSW((u16)(DSW(0x19EA) + (s16)(1 - side) * 0x12 + 4)); }

/* AP 1baa:001e */
void torps_load(u16 name)
{
    u16 mk = stk_mark(), cnt = stk_alloc(2);
    u16 f = my_fopen(res_name(name, 0x05E3), 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x3A6D), 0), 0x59, 0);
    my_fread(cnt, 2, 1, f);
    DSW(0x19EA) = arena_alloc((u16)(DSS(cnt) * 0x12));
    my_fread(DSW(0x19EA), 0x12, DSS(cnt), f);
    my_fclose(f);
    DSW(0x19EC) = weapon_class_register(far_make(SEG(0x1BAA), 0x026E), far_make(SEG(0x1BAA), 0x05F0));
    stk_release(mk);
}

/* AP 1baa:00ba — torp_drop(pos, dz, angles, TorpSrc {plane, type, ?, value}) — sim_world_wpn.md §6 */
u16 torp_drop(u16 pos, u16 dz, u16 ang, u16 src)
{
    u16 mec = (u16)(DSW(0x19EA) + DSS(src + 2) * 0x12);
    if (pool_field0(WPOOL) == 0) ordnance_make_room(1);
    FarPtr b = fp32(pool_alloc_zero(WPOOL));
    if (far_is_null(b)) return 0;
    FW(b, 0) = 5;
    FW(b, 2) = (u16)(DSW(0x474D) + DSW(mec) + 5);
    FB(b, 0x31) = 0;
    FB(b, 0x30) = DSB(mec);
    FW(b, 0x10) = DSW(ang); FW(b, 0x12) = DSW(ang + 2); FW(b, 0x14) = DSW(ang + 4);
    memmove(far_mp(far_add(b, 4)), DSP(pos), 12);
    FSL(b, 0x0C) += (s16)dz;
    if (FSL(b, 0x0C) < 2) FSL(b, 0x0C) = 2;
    memmove(far_mp(far_add(b, 0x1C)), DSP(DSW(src) + 0x70), 6);
    u16 mk = stk_mark(), v = stk_alloc(6);
    DSW(v) = 0; DSW(v + 2) = 0x100; DSW(v + 4) = 0;
    vrot_z(v, DSW(ang + 4));
    FW(b, 0x28) = DSW(v);
    FW(b, 0x2A) = DSW(v + 2);
    stk_release(mk);
    s16 z = FS(b, 0x20);
    FS(b, 0x20) = (s16)-(s16)(z < 0 ? -z : z);
    FW(b, 0x22) = 0x0D;
    FW(b, 0x26) = DSW(src + 6);
    FW(b, 0x16) = DSW(src);
    if (DSW(src) == PLAYER) wcam_note_weapon(b);
    FW(b, 0x18) = (u16)((DSW(src + 2) << 4) | DSW(0x19EC) | 0x2000);
    FW(b, 0x1A) = DSW(mec + 2) | 0x0F;
    FW(b, 0x24) = (u16)(DSW(mec + 0x10) << 8);
    s16 spd = (s16)(OS(DSW(src), 0x94) / 12);
    z = FS(b, 0x20);
    if ((s32)DSS(mec + 0x0A) < FSL(b, 0x0C) || DSS(mec + 8) < spd || (s16)(z < 0 ? -z : z) > 0x32)
        FW(b, 0x18) |= 0x4000;                                   /* dud */
    return 1;
}

/* AP 1baa:026e — sim_world_wpn.md §6 */
void torp_update(FarPtr b)
{
    u16 mec = (u16)(DSW(0x19EA) + ((s16)(FW(b, 0x18) & 0x3F0) >> 4) * 0x12);
    if (FW(b, 0x18) & 0x2000) { FW(b, 0x18) &= 0xDFFF; return; }
    u16 mk = stk_mark();
    u16 pos = stk_alloc(12), deck = stk_alloc(12), old = stk_alloc(12), cur = stk_alloc(12);
    if (FSL(b, 0x0C) > 1) {                                      /* falling */
        FW(b, 0x10) = (u16)ldiv_sat((s32)FS(b, 0x22) * -0x2000, 0x3C);
        s16 x = FS(b, 0x1C);
        if ((s16)(x < 0 ? -x : x) > 2) FW(b, 0x1C) = (u16)(FW(b, 0x1C) - (u16)(x >> 4));
        x = FS(b, 0x1E);
        if ((s16)(x < 0 ? -x : x) > 2) FW(b, 0x1E) = (u16)(FW(b, 0x1E) - (u16)(x >> 4));
        FW(b, 0x20) = (u16)(FW(b, 0x20) - FW(b, 0x22));
        if (FS(b, 0x22) <= 0x3C) FW(b, 0x22) = (u16)(FW(b, 0x22) + 0x0D);
        FSL(b, 4) += dt_scale(FS(b, 0x1C));
        FSL(b, 8) += dt_scale(FS(b, 0x1E));
        FSL(b, 0x0C) += dt_scale(FS(b, 0x20));
        memmove(DSP(pos), far_mp(far_add(b, 4)), 12);
        if (ship_under(pos, deck) && FSL(b, 0x0C) < DSSL(deck + 8)) {
            wpn_delete_later(b);
        } else if (FSL(b, 0x0C) <= 1) {
            FSL(b, 0x0C) = 1;
            if (FB(b, 0x30) == 0) FB(b, 0x31) = 1;
            else FW(b, 0) |= 0x2000;
            if (!(u8)splash_or_dust(fpu(far_add(b, 4)), 0, (u16)(DSW(0x474D) + 0x11)) || (FW(b, 0x18) & 0x4000)) {
                wpn_delete_later(b);
                DSB(0x57E6) = 1;
            } else {
                FW(b, 0x12) = 0; FW(b, 0x10) = 0;
            }
        }
        stk_release(mk);
        return;
    }
    if (FW(b, 0x24) == 0) {                                      /* run over */
        DSB(0x3CB0) |= 4; DSB(0x3CB4) |= 4;
        wpn_delete_later(b);
        stk_release(mk);
        return;
    }
    s16 s = dt_scale(DSS(mec + 0x0C));
    FW(b, 0x24) = (u16)(FW(b, 0x24) - (u16)dt_scale(0x40));
    memmove(DSP(old), far_mp(far_add(b, 4)), 12);
    FSL(b, 4) += ((s32)FS(b, 0x28) * s) >> 8;
    FSL(b, 8) += ((s32)FS(b, 0x2A) * s) >> 8;
    memmove(DSP(cur), far_mp(far_add(b, 4)), 12);
    FSL(b, 0x2C) += LD3(old, cur);
    if ((s32)DSS(mec + 0x0E) < FSL(b, 0x2C)) FW(b, 0x18) |= 0x4000;
    if ((u8)ground_under(fpu(far_add(b, 4)))) { wpn_delete_later(b); stk_release(mk); return; }
    u16 dmg = (FW(b, 0x18) & 0x4000) ? DSW(mec + 6) : 0;
    if (segment_hits_object(old, dsfar(cur), 1, b, dmg, pos)) {
        if (dmg) {
            explosion_spawn(pos, 0x7D0, (u16)(DSW(0x474D) + 0x10));
        } else {
            if (FW(b, 0x16) == PLAYER) DSB(0x57E5) = 1;
            wpn_delete_later(b);
        }
    }
    stk_release(mk);
}

/* AP 1baa:05f0 — wake animation */
u8 torp_draw(FarPtr b)
{
    u16 mk = stk_mark();
    u16 o = draw_record(b, FB(b, 0x31), 1);
    if (FB(b, 0x31) != 0) FB(b, 0x31) ^= 3;
    ts_draw(o);
    stk_release(mk);
    return 0;
}

/* ==== 0ff4: weapon stations ====================================================================== */

/* AP 0ff4:0001 — select the next fixed gun group (A, B or both) for the trigger; message the loadout */
void station_cycle(u16 ref)
{
    FarPtr msg = far_make(0, 0);
    u16 si = 0;
    u16 gl = DSW(ref);
    u16 cx = DSW(gl + 2);
    u16 st = DSW(ref + 2);
    if (!(cx & 0x2800) && !(cx & 0x500)) return;
    if ((cx & 0x2800) && !(cx & 0x500)) msg = ds_far(0x1546);
    else if (!(cx & 0x2800) && (cx & 0x500)) msg = ds_far(0x154A);
    else {
        if (!(cx & 0xC000)) { DSB(0x57E3) = 1; return; }
        if (DSW(st) != 0) si |= 0x4000;
        if (DSW(st + 0x0C) != 0) si |= 0x8000;
        if (si == 0) { DSW(gl + 2) &= 0x3FFF; DSB(0x57E3) = 1; return; }
        if (si != 0xC000 && (cx & 0xC000) == si) return;
        switch (cx & 0xC000) {
        case 0x4000: cx = (u16)((si & 0xC000) | (cx & 0x3FFF)); break;
        case 0xC000:
            if (si != 0xC000) cx = (u16)((si & 0xC000) | (cx & 0x3FFF));
            else cx &= 0xBFFF;
            break;
        case 0x8000: if (si & 0x4000) cx ^= 0xC000; break;
        }
        if ((cx & 0xC000) == 0xC000 && si != 0xC000) cx &= 0x7FFF;
        DSW(gl + 2) = cx;
        switch (cx & 0xC000) {
        case 0x4000: DSB(0x3FEE) |= 0x20; DSB(0x3FEE) &= 0xBF; msg = ds_far(0x1546); break;
        case 0x8000: DSB(0x3FEE) &= 0xDF; DSB(0x3FEE) |= 0x40; msg = ds_far(0x154A); break;
        case 0xC000: DSB(0x3FEE) |= 0x40; DSB(0x3FEE) &= 0xDF; DSB(0x3FEE) |= 0x20; msg = ds_far(0x154E); break;
        }
    }
    if (FB(msg, 0) != 0) msg_show(msg, 1, far_make(0, 0));   /* quirk: with no case matched it reads 0000:0000 */
}

/* AP 0ff4:0186 — mark a station empty: the first after the fixed groups, or (quirk kept: the best
 * distance is overwritten with the compare result 1) in practice the first one nearer than 7FFFFFFFh */
void station_drop(u16 ref, u16 near_pt)
{
    u16 di = 0;
    u16 gl = DSW(ref);
    u16 fl = DSW(gl + 2);
    if (near_pt == 0) {
        di = DSW(ref + 2);
        if (fl & 0x2800) di = (u16)(di + 0x0C);
        if (fl & 0x500) di = (u16)(di + 0x0C);
        DSW(di) = 0xFFFF;
        return;
    }
    if (!(fl & 0xFF)) return;
    s16 si = (s16)(((fl & 0x2800) ? 1 : 0) + ((fl & 0x500) ? 1 : 0));
    s32 best = 0x7FFFFFFF;
    u16 ent = (u16)(DSW(ref + 2) + si * 0x0C);
    u16 rec = (u16)(gl + si * 0x14 + 4);
    for (si = 0; (s16)(fl & 0xFF) > si; si++, ent = (u16)(ent + 0x0C), rec = (u16)(rec + 0x14)) {
        s32 d = (s32)vdist((u16)(rec + 6), near_pt);
        s32 lt = d < best ? 1 : 0;
        if (lt) { best = lt; di = ent; }
    }
    DSW(di) = 0xFFFF;
}

/* AP 0ff4:0277 */
u16 ap_station_count(u16 ref) { return DSW(DSW(ref) + 2) & 0xFF; }

/* AP 0ff4:0289 — stations with rounds left */
u16 station_loaded_count(u16 ref)
{
    u16 fl = DSW(DSW(ref) + 2), di = 0;
    s16 k = (s16)(((fl & 0x2800) ? 1 : 0) + ((fl & 0x500) ? 1 : 0));
    u16 si = (u16)(DSW(ref + 2) + k * 0x0C);
    for (s16 cx = 0; (s16)(fl & 0xFF) > cx; cx++, si = (u16)(si + 0x0C))
        if (DSS(si) > 0) di++;
    return di;
}

/* AP 0ff4:02ea — rounds of the first station (the `test dx, 0` guard never fires) */
u16 station_first(u16 ref)
{
    u16 fl = DSW(DSW(ref) + 2);
    s16 k = (s16)(((fl & 0x2800) ? 1 : 0) + ((fl & 0x500) ? 1 : 0));
    return DSW((u16)(DSW(ref + 2) + k * 0x0C));
}

/* ==== 6cc2: drop tanks =========================================================================== */

/* AP 6cc2:0000 — jettison: a falling tank effect from the template DS:1170, fuel and mass removed */
void drop_tank_release(u16 p)
{
    u16 mk = stk_mark(), e = stk_alloc(0x30);
    memmove(DSP(e), DSP(0x1170), 0x30);
    u16 v = (u16)(e + 0x20);
    u16 dat = DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2));
    DSW(e + 2) = (u16)(DSW(0x474D) + (OB(p, 0x26) != 0 ? 1 : 0) + 0x25);
    DSW(e + 0x10) = OW(p, 0x10); DSW(e + 0x12) = OW(p, 0x12); DSW(e + 0x14) = OW(p, 0x14);
    memmove(DSP(e + 4), DSP(p + 4), 12);
    dir_from_angles(v, (u16)(p + 0x10));
    DSW(v + 6) = OW(p, 0x6C);
    DSW(v + 8) = 0;
    OB(p, 0x1A) &= 0xFB;
    OL(OW(p, 0x2A), 0x10) = 0;
    OW(p, 0xB6) = (u16)(OW(p, 0xB6) - DSW(dat + 0x7F));
    fx_spawn(e);
    stk_release(mk);
}

/* AP 6cc2:00cb — tank effect callback: drift along the release direction, falling faster; returns the
 * low word of z */
u16 drop_tank_update(FarPtr o)
{
    FarPtr v = far_add(o, 0x20);
    s32 s = dt_scale(FS(v, 6));
    if (FS(v, 8) < 0x7FFF) FW(v, 8)++;
    FSL(o, 4) += ((s32)FS(v, 0) * s) >> 14;
    FSL(o, 8) += ((s32)FS(v, 2) * s) >> 14;
    FSL(o, 0x0C) += ((s32)FS(v, 4) * s) >> 14;
    FSL(o, 0x0C) -= (s32)FS(v, 8) << 6;
    s16 dx = (s16)(FW(v, 6) - (u16)(FS(v, 6) >> 3));
    FS(v, 6) = dx > 0 ? dx : 0;
    return FW(o, 0x0C);
}

/* ==== 04c4: flak and AA ========================================================================== */

/* AP 04c4:000b — draw ranges of the flak bursts by the detail slider */
void flak_set_range(u16 detail)
{
    s16 dx = (s16)detail;
    if (dx < 0x55) dx = 0x55;
    s16 v = lerp_map(dx, 0x55, 0x100, 0, 0x100);
    s32 r = (((s32)v * 10000) >> 8) + 1000;
    DSSL(0x5490) = r;
    DSSL(0x548C) = (s32)((u32)r << 8) >> 8;
}

/* AP 04c4:0057 */
void flak_register(void)
{
    DSW(0x54B5) = weapon_class_register(far_make(SEG(0x04C4), 0x0910), far_make(SEG(0x04C4), 0x0BF0));
}

/* AP 04c4:0073 */
void flak_reset(void)
{
    memset(DSP(0x5494), 0, 13);
    DSW(0x548A) = 0x10;
    DSL(0x549D) = 0xFA0A1F00u;
}

/* AP 04c4:07a2 — target within the site's envelope (heavy flak by kind DS:54A8 < 4, else light AA) */
u8 flak_in_range(u16 t)
{
    if ((s8)DSB(0x54A8) > 3) {
        if (OSL(t, 0x0C) <= 100 || OSL(t, 0x0C) >= 3000) return 0;
        return (u32)LD3(0x54A9, (u16)(t + 4)) < 12000;
    }
    s32 ceil = overcast() ? 0x34BC : DSSL((u16)(0x1266 + (s8)DSB(0x54A8) * 4));
    if (OSL(t, 0x0C) <= 0x9C4 || OSL(t, 0x0C) >= ceil) return 0;
    return (u32)ldist2(0x54A9, (u16)(t + 4)) < DSL((u16)(0x1276 + (s8)DSB(0x54A8) * 4));
}

/* AP 04c4:062e — keep the gun's target while in range (≤ 5 salvos, then other guns of the site drop it),
 * else the first untargeted enemy in range, else a random already-targeted one (RNG) */
u16 flak_pick_target(FarPtr gun)
{
    u16 mk = stk_mark(), lst = stk_alloc(0x68);
    s16 cnt = 0;
    if (FW(gun, 0) != 0) {
        if (flak_in_range(FW(gun, 0)) && FS(gun, 4) < 5) {
            FW(gun, 4)++;
            if (FS(gun, 4) >= 2) {
                FarPtr g = ds_far(0x54A3);
                for (s16 dx = 0; dx < DSS(0x54A1); dx++, g = far_add(g, 6))
                    if (fpu(g) != fpu(gun) && FW(g, 0) == FW(gun, 0)) FW(g, 4) = 0;
            }
        } else {
            FW(gun, 0) = 0;
            FW(gun, 4) = 0;
        }
        stk_release(mk);
        return FW(gun, 0);
    }
    u16 di = lst;
    for (s16 i = 0; i < DSS(0x4776); i++) {
        u16 si = DSW((u16)(DSW(0x475B) + i * 2));
        if (!obj_alive(si) || OB(si, 0x26) == DSB(0x54A7)) continue;
        s16 dx = DSS(0x54A1);
        FarPtr g = ds_far(0x54A3);
        while (dx > 0) {
            if (FW(g, 0) == si) break;
            dx--;
            g = far_add(g, 6);
        }
        if (dx != 0) { DSW(di) = si; di = (u16)(di + 2); cnt++; continue; }
        if (flak_in_range(si)) { FW(gun, 4) = 0; FW(gun, 0) = si; break; }
    }
    if (cnt != 0 && FW(gun, 0) == 0) {
        u16 si = DSW((u16)(lst + rng_mod(RNG_ST, cnt) * 2));
        if (flak_in_range(si)) FW(gun, 0) = si;
        FW(gun, 4) = 0;
    }
    stk_release(mk);
    return FW(gun, 0);
}

/* AP 04c4:0860 — a burst with a fuse, aimed at a target */
void flak_burst_spawn(u16 aim, u16 fuse, u16 tgt)
{
    FarPtr b = fp32(pool_alloc_zero(WPOOL));
    if (far_is_null(b)) return;
    FW(b, 0) = 0x2805;
    FW(b, 2) = (u16)(DSW(0x474D) + (ap_rng_next(RNG_ST) & 3) + 0x0C);
    memmove(far_mp(far_add(b, 4)), DSP(aim), 12);
    FW(b, 0x18) = DSW(0x54B5);
    FW(b, 0x1C) = tgt;
    FW(b, 0x1E) = (u16)(fuse << 8);
    FB(b, 0x22) = 0xFE;
    if (PLAYER != tgt) {
        u16 r = ap_rng_next(RNG_ST) & 0x3FF;
        u16 pil = DSW((u16)(DSW(0x55CE) + OB(tgt, 0xBB) * 2));
        if ((s16)r < DSS(pil + 0x20)) FW(b, 0x18) |= 0x4000;
    }
}

/* AP 04c4:02be — one shot of a site gun (far 6-byte gun entry; off = the gun's offset in the ship frame
 * or 0) — sim_world_wpn.md §8 */
u8 flak_fire(FarPtr gun, u16 off)
{
    u16 t = flak_pick_target(gun);
    if (t == 0) {
        FW(gun, 2) = (u16)(DSB(0x5494) + 0x4B0);
        DSB(0x5494)++;
        return 1;
    }
    u16 mk = stk_mark();
    if ((s8)DSB(0x54A8) >= 4) {                                  /* light AA: a bullet */
        u16 src = stk_alloc(6), pos = stk_alloc(12), lead = stk_alloc(12), ang = stk_alloc(6), ang2 = stk_alloc(6),
            aim = stk_alloc(12);
        DSW(src) = DSW(0x1286); DSW(src + 2) = DSW(0x1288); DSW(src + 4) = DSW(0x128A);
        s16 level = (s8)DSB(0x42D2);
        s16 tvx = OS(t, 0x70), tvy = OS(t, 0x72), tvz = OS(t, 0x74);
        DSW(GUN_REC) = (u16)((s16)(s8)DSB(0x54A8) - 4);
        if ((ap_rng_next(RNG_ST) & 0x3FF) < 0x64) FW(gun, 2) = 10000;
        else FW(gun, 2) = (u16)(((s32)(s16)gun_type_refire(GUN_REC) * 2000) >> 10);
        if (off) ladd3_v(0x54A9, off, pos);
        else memmove(DSP(pos), DSP(0x54A9), 12);
        s32 r = LD3(pos, (u16)(t + 4));
        s16 spd = (s16)gun_type_speed(GUN_REC);
        DSSL(lead) = ldiv_sat((s32)(s16)r * tvx, spd);
        DSSL(lead + 4) = ldiv_sat((s32)(s16)r * tvy, spd);
        DSSL(lead + 8) = ldiv_sat((s32)(s16)r * tvz, spd);
        ladd(lead, (u16)(t + 4));
        angles_to(ang, pos, lead);
        DSW(GUN_ST) = 1;
        DSL(GUN_ST + 2) = 0;
        DSSL(aim + 8) = r;
        angles_to(ang2, (u16)(t + 4), pos);
        vsub(ang2, ang);
        vsar(ang2, 4);
        DSB(aim + 2) = (u16)(DSW(ang2) + 0x7E) < 0xFC ? DSB(ang2) : 0x80;
        DSB(aim + 3) = (u16)(DSW(ang2 + 4) + 0x7E) < 0xFC ? DSB(ang2 + 4) : 0x80;
        s16 q = (s16)shot_quality(aim, DSS((u16)(0x125A + level * 2)), DSS((u16)(0x1260 + level * 2)));
        if (q == 2) DSW(src + 4) = 0;
        if (q != 0) bullet_spawn(0, DSS(ang), DSS(ang + 4), pos, DSW(0x3716), src);
        stk_release(mk);
        return 0;
    }
    /* heavy flak: a burst near the target */
    u16 ofs = stk_alloc(6), rot = stk_alloc(6), aim = stk_alloc(12);
    s16 k;
    if (OS(t, 0x10) < (s16)0xE000 || DSB(DSW((u16)(DSW(0x55CE) + OB(t, 0xBB) * 2)) + 0x41) != 0) k = 0;
    else { k = FS(gun, 4); if (k > 2) k = 2; }
    s16 s = DSS((u16)(0x1246 + k * 2));
    s16 s2 = (s16)(s << 1);
    FW(gun, 2) = (u16)((ap_rng_next(RNG_ST) & 0xFFF) + 0xC00);
    u16 fuse = (u16)((u32)LD3((u16)(t + 4), 0x54A9) >> 11) + 4;
    DSW(ofs + 2) = (u16)((s16)((s32)(s16)fuse * OS(t, 0x6C)) + DSW((u16)(0x124C + k * 2)));
    DSW(ofs) = (u16)(rng_mod(RNG_ST, s2) - s);
    DSW(ofs + 4) = (u16)(rng_mod(RNG_ST, s2) - s);
    vec_rotate(ofs, obj_matrix(OW(t, 0x20)), rot);
    ladd3_v((u16)(t + 4), rot, aim);
    flak_burst_spawn(aim, fuse, t);
    stk_release(mk);
    return 0;
}

/* AP 04c4:0098 — every ground AA site and every ship gun group fires at most one shot per frame */
void flak_fire_all(void)
{
    u16 mk = stk_mark();
    u16 rec = stk_alloc(0x14), st = stk_alloc(0x0C), rot = stk_alloc(6);
    DSL(0x549D) = 0xFA0A1F00u;
    memset(DSP(rec), 0, 0x14);
    memset(DSP(st), 0, 0x0C);
    DSW(rec + 2) = 1;
    DSW(0x134A) = rec;
    DSW(0x134C) = st;
    FarPtr site = ds_far(0x18B6);
    for (s16 i = 0; i < DSS(0x18BC); i++, site = far_add(site, 0x26)) {
        DSW(0x54A1) = FW(site, 4);
        ds_far_wr(0x54A3, far_add(site, 6));
        FarPtr g = ds_far(0x54A3);
        u16 fired = 0;
        for (s16 j = 0; j < DSS(0x54A1); j++, g = far_add(g, 6)) {
            if (timer16_expired_far(far_add(g, 2)) && fired == 0) {
                DSB(0x54A7) = FB(site, 0x24);
                DSB(0x54A8) = FB(site, 0x25);
                memmove(DSP(0x54A9), mp(FW(site, 2), (u16)(FW(site, 0) + 4)), 12);
                fired = flak_fire(g, 0);
            }
        }
    }
    for (s16 i = 0; i < DSS(0x4774); i++) {
        u16 sh = DSW((u16)(DSW(0x4759) + i * 2));
        if (OB(sh, 0x1B) & 2) continue;
        u16 M = obj_matrix(OW(sh, 0x20));
        u16 grec = (u16)(OW(sh, 0x5E) + 4);
        u16 si = OW(sh, 0x60);
        s16 n = OS(OW(sh, 0x5E), 0);
        for (s16 j = 0; j < n; j++, grec = (u16)(grec + 10), si = (u16)(si + 0x26)) {
            u16 first = 1;
            DSW(0x54A1) = DSW(si + 4);
            DSW(0x54A5) = cur_dgroup;
            DSW(0x54A3) = (u16)(si + 6);
            u16 di = (u16)(si + 6);
            u16 fired = 0;
            for (s16 k = 0; k < DSS(0x54A1); k++, di = (u16)(di + 6)) {
                if (timer16_dec((u16)(di + 2)) == 0 && fired == 0) {
                    if (first) { vec_rotate((u16)(grec + 4), M, rot); first = 0; }
                    DSB(0x54A7) = DSB(si + 0x24);
                    DSB(0x54A8) = DSB(si + 0x25);
                    memmove(DSP(0x54A9), mp(DSW(si + 2), (u16)(DSW(si) + 4)), 12);
                    fired = flak_fire(far_make(cur_dgroup, di), rot);
                }
            }
        }
    }
    stk_release(mk);
}

/* AP 04c4:0910 — fuse, then the burst animation; damage events to the player only */
void flak_burst_update(FarPtr b)
{
    if (FB(b, 0x22) == 0xFE) {
        if (timer16_dec_far(far_add(b, 0x1E)) != 0) return;
        FW(b, 0) &= 0xDFFF;
        FB(b, 0x22) = 0xFF;
        FW(b, 0x1E) = 0x157C;
        FW(b, 0x20) = 0xFFFF;
        hit_sound(b, 0);
        return;
    }
    s16 last = (s16)(shape_nvariants((s16)FW(b, 2), 0) - 1);
    if ((s8)FB(b, 0x22) >= last) { wpn_delete_later(b); return; }
    s16 level = (s8)DSB(0x42D2);
    s16 di = (s16)((s16)(0x157C - FW(b, 0x1E)) >> 8);
    timer16_dec_far(far_add(b, 0x1E));
    s16 fr;
    if (di <= 4) fr = di;
    else if (di > DSS(0x548A)) { fr = (s16)(di - DSS(0x548A) + 4); if (fr > last) fr = last; }
    else fr = 4;
    FB(b, 0x22) = (u8)fr;
    if (level == 0 || DSB(0x42CF) == 0 || FW(b, 0x1C) != PLAYER || FS(b, 0x20) == di) return;
    u16 mk = stk_mark(), pos = stk_alloc(12);
    memmove(DSP(pos), far_mp(far_add(b, 4)), 12);
    if ((u32)LD3(pos, (u16)(PLAYER + 4)) < 400) {
        u16 e = dmg_event_alloc();
        if (e) {
            DSW(e) = b.off; DSW(e + 2) = b.seg;
            DSW(e + 4) = FW(b, 0x1C);
            DSW(e + 6) = DSW((u16)(0x1252 + ((s16)(FW(b, 0x18) & 0x3F0) >> 4) * 2));
            if (level == 1) DSS(e + 6) = (s16)(DSS(e + 6) >> 1);
            FS(b, 0x20) = di;
        }
    }
    stk_release(mk);
}

/* AP 04c4:0a9f — draw range near the player's flight path (4 x ahead, recomputed while DS:549D says so) */
s32 flak_player_scale(s32 r, u16 p)
{
    if (DSL(0x549D) == 0xFA0A1F00u) {
        u16 mk = stk_mark(), v = stk_alloc(6);
        dir_from_angles(v, (u16)(PLAYER + 0x10));
        vsar(v, 4);
        ladd3_v((u16)(PLAYER + 4), v, 0x5495);
        stk_release(mk);
    }
    s32 d = LD3(p, 0x5495);
    if (d <= 0x800) {
        d -= 0x400;
        if (d < 0) r <<= 3;
        else r = (s32)((u32)r * (u32)(0x400 - d)) >> 7;
    }
    return r;
}

/* AP 04c4:0b53 — cull a burst by range before it is added to the scene */
void flak_visible_test(FarPtr o, u16 eye)
{
    u16 mk = stk_mark(), pos = stk_alloc(12);
    memmove(DSP(pos), far_mp(far_add(o, 4)), 12);
    s32 d = LD3(pos, eye), r;
    if (FW(o, 0x1C) == PLAYER) {
        if ((s32)(DSL(0x548C) << 2) < d) r = DSSL(0x548C);
        else r = flak_player_scale(DSSL(0x548C), pos);
    } else r = DSSL(0x5490);
    if (d < r) scene_add_dynamic(o);
    stk_release(mk);
}

/* AP 04c4:0bf0 */
u8 flak_burst_draw(FarPtr b)
{
    if (FB(b, 0x22) == 0xFE) return 0;
    u16 mk = stk_mark();
    ts_draw(draw_record(b, FB(b, 0x22), 0));
    stk_release(mk);
    return 0;
}

/* AP 04c4:0c53 — AI avoidance of a near miss (flags Plane+43 from proximity_flags), then fly */
void plane_avoid_collision(u16 p)
{
    if (OB(p, 0x43) & 0x80) steer_climb(p, 0, 0x1F40, 0);
    else if (OB(p, 0x43) & 0x40) steer_climb(p, 0, 0xE0C0, 0);
    if (OB(p, 0x43) & 0x10) steer_heading_nolead(p, 0, 0xE0C0);
    else if (OB(p, 0x43) & 0x20) steer_heading_nolead(p, 0, 0x1F40);
    plane_fly(p);
}

/* ==== code pointers ============================================================================== */

static void reg(u32 fn, CodeFn f) { codeptr_register(PROG_AP, fn, f); }
__attribute__((constructor)) static void weapons_codeptrs_register(void)
{
    reg(0x07690637u, (CodeFn)bullet_update);
    reg(0x076908A3u, (CodeFn)bullet_draw);
    reg(0x068804E3u, (CodeFn)bomb_update);
    reg(0x1A3202F4u, (CodeFn)rocket_update);
    reg(0x1A320531u, (CodeFn)rocket_draw);
    reg(0x1BAA026Eu, (CodeFn)torp_update);
    reg(0x1BAA05F0u, (CodeFn)torp_draw);
    reg(0x04C40910u, (CodeFn)flak_burst_update);
    reg(0x04C40BF0u, (CodeFn)flak_burst_draw);
    reg(0x6CC200CBu, (CodeFn)drop_tank_update);           /* effect callback of the template DS:1170 */
}
