/* sim_world — damage, collisions, crashes (sim_world_dmg.md): proximity / mid-air (085b), ground and
 * ship impacts and credit (088a), G load (0fd4), near-object lists and ground queries (14e0), damage
 * data (4b3a), damage application, going down, player engine (655d). Ported from the disassembly;
 * where it differs from the spec (critical numbering, the fire critical lifting the player 100 units,
 * the torpedo arming bit) the code is followed. 39e5:0000 is a data buffer (kill message), not code. */
#include "sim_world/swa_priv.h"
#include "codeptr.h"
#include "prog.h"

#define LD3(a, b) ((s32)ldist3((a), (b)))
#define MTIME DSL(0x4086)

/* sim_flow 1899:102f target_credit — not declared in sim_flow.h (request) */
void target_credit(u16 shooter, FarPtr rec, FarPtr ref, u16 cls, u16 type, u16 delta, u16 destroyed);
void crash_effect(u16 p, s32 g);
void player_engine_update(void);

static FarPtr fp32(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static u16 plane_dat(u16 p) { return DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2)); }
static void msg_str(s16 group, s16 id, s16 prio) { msg_show(str(group, id), prio, far_make(0, 0)); }
static void ev_set(u16 e, FarPtr w, u16 t, u16 dmg) { DSW(e) = w.off; DSW(e + 2) = w.seg; DSW(e + 4) = t; DSW(e + 6) = dmg; }

/* ==== 085b: proximity and mid-air collisions ===================================================== */

/* AP 085b:0006 */
void proximity_nop(void) {}

/* AP 085b:000b — avoidance flags on a (Plane+43 / +47): turn away from b, dive or climb */
static void proximity_flags(u16 a, u16 b)
{
    u16 mk = stk_mark(), ang = stk_alloc(6);
    angles_to(ang, (u16)(b + 4), (u16)(a + 4));
    s16 h = (s16)(DSW(ang + 4) - OW(a, 0x14));
    stk_release(mk);
    if (OSL(a, 0x0C) > 200) {
        u8 f = h > 0 ? 0x10 : 0x20;
        OB(a, 0x43) |= f; OB(a, 0x47) |= f;
    }
    if (OSL(b, 0x0C) > OSL(a, 0x0C)) {
        if (OSL(a, 0x0C) > 500) { OB(a, 0x43) |= 0x40; OB(a, 0x47) |= 0x40; }
    } else {
        OB(a, 0x43) |= 0x80; OB(a, 0x47) |= 0x80;
    }
}

/* AP 085b:0085 — sim_world_dmg.md §5.2 */
void proximity_update(void)
{
    s16 n = DSS(0x4776);
    for (s16 i = 0; i < n; i++) {
        u16 si = DSW((u16)(DSW(0x475B) + i * 2));
        if (si == PLAYER || !obj_alive(si)) continue;
        s32 d = LD3((u16)(si + 4), (u16)(PLAYER + 4));
        if (d > 1000) continue;
        proximity_flags(si, PLAYER);
        if (DSB(0x42D0) == 0 || d > 200 || DSW(0x1382) != 0) continue;
        u16 e = dmg_event_alloc();
        if (e) {
            ev_set(e, far_make(0, 0), PLAYER, 0x1F40);
            sx_play(1, (u16)(PLAYER + 4), 0, 1);
            if ((ap_rng_next(RNG_ST) & 0x3FF) < 0x100) DSB(0x0FEB) = 1;
        }
        e = dmg_event_alloc();
        if (e) ev_set(e, far_make(0, 0), si, 0x1F40);
    }
    for (s16 i = 0; i < DSS(0x4776) - 1; i++) {
        u16 si = DSW((u16)(DSW(0x475B) + i * 2));
        if (si == PLAYER || !obj_alive(si)) continue;
        for (s16 j = (s16)(i + 1); j < DSS(0x4776); j++) {
            u16 di = DSW((u16)(DSW(0x475B) + j * 2));
            if (di == PLAYER || !obj_alive(di)) continue;
            if ((u32)LD3((u16)(si + 4), (u16)(di + 4)) >= 600) continue;
            s16 qb = (s8)DSB((u16)(plane_dat(si) + 0x38)) == 3;
            s16 rb = (s8)DSB((u16)(plane_dat(di) + 0x38)) == 3;
            if (!qb || rb) proximity_flags(si, di);
            if (!rb || qb) proximity_flags(di, si);
        }
    }
}

/* AP 085b:02c3 / 02ce / 02e5 — mid-air grace timer */
void midair_grace_reset(void) { DSW(0x1382) = 0; }
void midair_grace_tick(void) { if (DSW(0x1382) != 0) timer16_dec(0x1382); }
void midair_grace_set(s16 v) { if (v > DSS(0x1382)) DSS(0x1382) = v; }

/* ==== 088a: impacts and credit =================================================================== */

/* AP 088a:0007 — brain state of a plane wrecked on a carrier deck */
u16 state_wreck_on_deck(u16 p)
{
    park_on_carrier(p, (s8)OB(p, 0xBC));
    u16 t = OW(p, 0x54);
    if (DSL(t) != 0) {
        timer32_dec(t);
        if (DSL(t) == 0) {
            OB(p, 0x1A) |= 1;
            if (p == PLAYER) end_request(1);
            else if (view_outside_target() == p) DSB(0x1AFF) = 1;
            DSL(t + 4) = 0x1F40;
            explosion_spawn((u16)(p + 4), 0, (u16)(DSW(0x474D) + 0x1C));
            OW(p, 0) |= 0x2000;
        }
    } else if (DSL(t + 4) != 0) {
        timer32_dec((u16)(t + 4));
        if (DSL(t + 4) == 0) { OB(p, 0x42) |= 4; OB(p, 0x46) |= 4; OB(p, 0x1B) |= 1; }
    }
    return 0;
}

/* AP 088a:00b1 — ship deck under a low point (segment down to z=0 against the ship's boxes); the
 * point is raised to 708h in place for the following ships (as the original) */
u16 ship_under(u16 p, u16 out)
{
    u16 mk = stk_mark(), q = stk_alloc(12), b = stk_alloc(12);
    memmove(DSP(q), DSP(p), 12);
    if (DSSL(q + 8) > 0x708) { stk_release(mk); return 0; }
    for (s16 i = 0; i < DSS(0x4774); i++) {
        u16 s = DSW((u16)(DSW(0x4759) + i * 2));
        if (!ship_is_real(s)) continue;
        if (rtl_labs(DSSL(q) - OSL(s, 4)) >= 5000) continue;
        if (rtl_labs(DSSL(q + 4) - OSL(s, 8)) >= 5000) continue;
        memmove(DSP(b), DSP(q), 12);
        DSL(b + 8) = 0;
        if (DSSL(q + 8) < 0x708) DSSL(q + 8) = 0x708;
        u16 h = segment_hits_boxes(q, b, OW(s, 0x20), (u16)(s + 4), OW(s, 0x24));
        if (h) { memmove(DSP(out), DSP(h), 12); stk_release(mk); return s; }
    }
    stk_release(mk);
    return 0;
}

/* AP 088a:045f — hit sound by weapon class; remembers the last 6 positions */
void hit_sound(FarPtr w, u16 by)
{
    u16 bx = FW(w, 0x18) & 0x0F;
    s16 mec = (s16)(FW(w, 0x18) & 0x3F0) >> 4;
    s16 si = -1;
    DSW(0x1398)++;
    if (DSW(0x1398) == 6) DSW(0x1398) = 0;
    u16 slot = (u16)(DSW(0x1398) * 12 + 0x5604);
    memmove(DSP(slot), far_mp(far_add(w, 4)), 12);
    if (bx == DSW(0x1344)) si = mec <= 3 ? 4 : mec <= 4 ? 5 : 6;
    else if (bx == DSW(0x19EC)) si = 6;
    else if (bx == DSW(0x193A)) si = 4;
    else if (bx == DSW(0x1348)) {
        if (DSW(0x19F2) != 0) {
            if (by == PLAYER) si = 0x14;
            else if (FW(w, 0x16) == PLAYER) si = 0x18;
        }
    } else if (bx == DSW(0x54B5)) si = 8;
    if (si != -1) sx_play((u16)si, (u16)(DSW(0x1398) * 12 + 0x5604), 0, by == PLAYER);
}

/* AP 088a:054f (kill_credit in the spec; renamed, HANDOFFS N-b) — messages and scoring of a hit */
void object_destroyed_notify(u16 t, FarPtr w, u16 delta)
{
    if (far_is_null(w)) return;
    s16 prio = 1, di = -1;
    s16 type = (s16)(FW(w, 0x18) & 0x3F0) >> 4;
    u16 cls = FW(w, 0x18) & 0x0F;
    u8 destroyed = DSS(OW(t, 0x2A)) >= 0x400;
    u8 plane = OB(t, 0x18) == 0, ship = OB(t, 0x18) == 1;
    u16 sh = FW(w, 0x16);
    if (PLAYER == sh) {
        if (destroyed && plane && OB(t, 0x26) != OB(sh, 0x26) && OB(t, 0xBB) > 0x2C) {
            u16 pil = DSW((u16)(DSW(0x55CE) + OB(t, 0xBB) * 2));
            FarPtr buf = far_make(SEG(0x39E5), 0);
            rtl__fstrcpy(buf, ds_ptr(0x139A));
            rtl__fstrcat(buf, ds_ptr((u16)(pil + 0x58)));
            rtl__fstrcat(buf, ds_ptr(0x13AE));
            msg_show(buf, 2, far_make(0, 0));
        } else if (OB(t, 0x26) == OB(sh, 0x26)) {
            if (plane) { if (t != PLAYER) di = destroyed ? 1 : 0; }
            else if (ship) di = 2;
            prio = 3;
        } else if (destroyed) {
            if (plane) di = (s8)DSB((u16)(plane_dat(t) + 0x38)) == 3 ? 7 : 6;
            else if (ship) {
                u16 sd = DSW((u16)(DSW(0x55D0) + DSB((u16)(0x555B + OB(t, 0x19))) * 2));
                di = DSB(sd + 0x6C) == 1 ? 9 : 8;
            }
            if (OB(t, 0x26) == 1) di = (s16)(di + 4);
            prio = 2;
        } else {
            if (cls == DSW(0x1344)) di = 4;
            else if (cls == DSW(0x19EC)) di = 5;
        }
        if (di != -1) msg_str(2, di, prio);
    } else if (plane && destroyed && OW(sh, 0x34) == OW(PLAYER, 0x34)) {
        u16 id = (s8)DSB((u16)(plane_dat(t) + 0x38)) == 0 ? 0x5A : 0x5B;
        u16 mk = stk_mark(), ctx = stk_alloc(0x1A);
        memset(DSP(ctx), 0, 0x1A);                /* PORT: the rest of the record is stack garbage */
        DSW(ctx + 0x18) = t;
        DSW(ctx + 0x16) = t;
        DSL(ctx + 0x0E) = 0;
        radio_queue7(id, ctx, 1);
        stk_release(mk);
    }
    hit_sound(w, t);
    kill_credit(sh, t, cls, (u16)type, delta, destroyed);
}

/* AP 088a:07ab — a ground object hit: friendly-target warning, destroyed-target message, score */
void ground_credit(FarPtr grp, u32 rec, u16 idx, FarPtr w, u16 delta)
{
    s16 type = (s16)(FW(w, 0x18) & 0x3F0) >> 4;
    u16 cls = FW(w, 0x18) & 0x0F;
    u16 gt = FW(grp, 0) & 0x1F;
    u8 destroyed = FB(grp, (u16)(idx + 0x12)) == 0xFF;
    u16 sh = FW(w, 0x16);
    u16 mk = stk_mark(), ref = stk_alloc(6), buf = stk_alloc(0x50);
    DSW(ref) = grp.off; DSW(ref + 2) = grp.seg; DSW(ref + 4) = idx;
    if (PLAYER == sh) {
        if ((gt == 2 && OB(sh, 0x26) == 0) || (gt == 0x10 && OB(sh, 0x26) == 1)) {
            msg_str(2, 3, 3);
        } else if (destroyed) {
            FarPtr name = fp32(target_name(rec, (u32)cur_dgroup << 16 | ref));
            if (!far_is_null(name)) {
                rtl__fstrcpy(ds_ptr(buf), str(2, 0x0E));
                msg_show(rtl__fstrcat(ds_ptr(buf), name), 1, far_make(0, 0));
            }
        }
    }
    hit_sound(w, 0);
    target_credit(sh, fp32(rec), ds_ptr(ref), cls, (u16)type, delta, destroyed);
    stk_release(mk);
}

/* ==== 14e0: near objects, ground queries ========================================================= */

/* AP 14e0:06c0 — ground/object under p (out: hit record, +4 i32 height) */
static u8 collision_ground(u16 out, FarPtr p, u16 ships)
{
    s32 z = FSL(p, 8);
    if (z > 0xD700) return 0;
    u32 pp = (u32)p.seg << 16 | p.off;
    if (z > 0x36B) return (u8)ground_query(out, pp, (u32)cur_dgroup << 16 | DSW(0x18B0), DSW(0x5838));
    FarPtr so = ds_far(0x06F3);
    if (!(u8)ground_query(out, pp, (u32)so.seg << 16 | (u16)(so.off + DSW(0x5134) * 4), (u16)(DSW(0x5132) - DSW(0x5134))))
        return 0;
    u16 mk = stk_mark(), tmp = stk_alloc(0x10);
    if ((u8)ships) {
        if ((u8)ground_query(tmp, pp, DSL(0x513C), DSW(0x5140))) memmove(DSP(out), DSP(tmp), 12);
    } else {
        if ((u8)ground_query(tmp, pp, (u32)cur_dgroup << 16 | DSW(0x18B0), DSW(0x18BA))) memmove(DSP(out), DSP(tmp), 12);
    }
    stk_release(mk);
    return 1;
}

/* AP 14e0:07aa — over land (terrain of the current tiles) */
u16 ground_under(u32 p)
{
    u16 mk = stk_mark(), tmp = stk_alloc(12);
    FarPtr so = ds_far(0x06F3);
    u16 r = (u8)ground_query(tmp, p, (u32)so.seg << 16 | (u16)(so.off + DSW(0x5134) * 4), (u16)(DSW(0x5132) - DSW(0x5134)));
    stk_release(mk);
    return r;
}

/* AP 14e0:07db — near ground objects under p */
u16 ground_height_at(u16 out, FarPtr p)
{
    if (FSL(p, 8) > 0xD700) return 0;
    return (u8)ground_query(out, (u32)p.seg << 16 | p.off, (u32)cur_dgroup << 16 | DSW(0x18B0), DSW(0x5838));
}

/* AP 14e0:0002 / 006d */
void near_lists_alloc(void)
{
    ds_far_wr(0x18B6, mem_far_new(0x474, 0));
    DSW(0x18B0) = arena_alloc(0x320);
    ds_far_wr(0x18B2, mem_far_new(0x15E0, 0));
    DSW(0x18C0) = 0; DSW(0x5838) = 0; DSW(0x18BE) = 0; DSW(0x18BC) = 0; DSW(0x18BA) = 0;
    memset(DSP(0x582C), 0, 12);
    DSL(0x5828) = 0;
}
void near_lists_free(void)
{
    mem_far_delete(ds_far(0x18B2));
    mem_far_delete(ds_far(0x18B6));
}

/* AP 14e0:008c — occupancy grid of low planes, rebuilt every 1900h ticks or after 3000 units */
u16 near_grid_update(void)
{
    if (MTIME < DSL(0x5828) && (u32)ldist2((u16)(PLAYER + 4), 0x582C) < 3000) return 0;
    DSL(0x5828) = MTIME + 0x1900;
    memmove(DSP(0x582C), DSP(PLAYER + 4), 12);
    DSW(0x18C0) = 0;
    memset(DSP(0x583A), 0, 0x200);
    for (s16 i = 0; i < DSS(0x4776); i++) {
        u16 q = DSW((u16)(DSW(0x475B) + i * 2));
        if (OSL(q, 0x0C) > 20000 || !obj_alive(q)) continue;
        if (OB(OW(q, 0x34), 0x3B) & 2) continue;
        u8 cl = DSB(0x0C2E) & 31;
        s16 dx = (s16)((OSL(q, 4) - DSSL(0x0C4C) - DSSL(0x0C30)) >> cl);
        s16 dy = (s16)((OSL(q, 8) - DSSL(0x0C50) - DSSL(0x0C30)) >> cl);
        if ((dx | dy) & 0xFF80) continue;
        if (dx == 0x3F || dy == 0x3F) continue;
        u16 si = (u16)(0x583A + dx * 8 + (dy >> 3));
        u8 bit = DSB((u16)(0x18C2 + (dy & 7)));
        if (bit == 0) { DSW(si) |= 0x180; DSW(si + 8) |= 0x180; }
        else { DSB(si) |= bit; DSB(si + 8) |= bit; }
    }
    return 1;
}

/* AP 14e0:0233 */
static u8 near_grid_test(FarPtr o)
{
    u8 cl = DSB(0x0C2E) & 31;
    s16 si = (s16)((FSL(o, 4) - DSSL(0x0C4C)) >> cl);
    s16 dx = (s16)((FSL(o, 8) - DSSL(0x0C50)) >> cl);
    if ((si | dx) & 0xFF80) return 0;
    return DSB((u16)(0x583A + si * 8 + (dx >> 3))) & DSB((u16)(0x0C7A + (dx & 7)));
}

/* AP 14e0:01d3 — keep x/y inside the world unless very high */
void clamp_world(u16 o)
{
    u16 si = (u16)(o + 4);
    if (DSSL(si + 8) > 0x5F5E100) return;
    if (DSSL(si) > 0xC16EC0) DSSL(si) = 0xC16EC0;
    else if (DSSL(si) < -0xC16EC0) DSSL(si) = -0xC16EC0;
    if (DSSL(si + 4) > 0xC16EC0) DSSL(si + 4) = 0xC16EC0;
    else if (DSSL(si + 4) < -0xC16EC0) DSSL(si + 4) = -0xC16EC0;
}

/* AP 14e0:0286 — static objects near low planes or the player into DS:18B0 (max 200), AA sites into
 * DS:18B6 (max 30) — sim_world_dmg.md §5.3 */
void near_objects_build(void)
{
    u16 dst_near = DSW(0x18B0);
    FarPtr dst_far = ds_far(0x18B2), aa = ds_far(0x18B6);
    u16 mark = arena_alloc(0);
    u16 planes = arena_alloc((u16)(DSW(0x4776) << 1));
    s16 n = 0;
    u16 pp = planes;
    for (s16 i = 0; i < DSS(0x4776); i++) {
        u16 q = DSW((u16)(DSW(0x475B) + i * 2));
        if (q == PLAYER || !obj_alive(q)) continue;
        if (OB(OW(q, 0x34), 0x3B) & 2) continue;
        DSW(pp) = q; pp = (u16)(pp + 2); n++;
    }
    u16 near = arena_alloc((u16)(n << 1));
    DSW(0x5838) = 0; DSW(0x18BE) = 0; DSW(0x18BC) = 0; DSW(0x18BA) = 0;
    u16 mk = stk_mark(), gpos = stk_alloc(12), mpos = stk_alloc(12);
    FarPtr so = ds_far(0x06F3);
    for (s16 i = DSS(0x5132); i < DSS(0x512E); i++) {
        FarPtr g = far_rd(so.seg, (u16)(so.off + i * 4));
        if (!near_grid_test(g)) continue;
        memmove(DSP(gpos), far_mp(far_add(g, 4)), 12);
        s32 r;
        if (FW(g, 0) & 0x1000) r = (s32)macro_distance(g) + 0x7B0C;
        else r = shape_radius_world((s16)FW(g, 2)) + 0x2904;
        u8 close = (u32)ldist2((u16)(PLAYER + 4), gpos) < (u32)r;
        s16 cnt = n ? (s16)collect_planes_near(planes, (u16)n, gpos, near, (u16)n, (u32)r) : 0;
        if (n == 0 && !close) continue;
        if (FW(g, 0) & 0x1000) {
            s16 cnt2 = (s16)macro_expand(g);
            FarPtr mem = far_make(SEG(0x3B05), 0);
            FarPtr dmgp = far_add(g, 0x12);
            for (s16 j = 0; j < cnt2; j++, mem = far_add(mem, 0x16), dmgp = far_add(dmgp, 1)) {
                if (DSS(0x18BE) >= 200 || DSS(0x18BA) >= 200) { arena_release(mark); stk_release(mk); return; }
                if (FB(dmgp, 0) == 0xFF) continue;
                if ((u8)shape_layer_cached(FW(mem, 2))) break;
                if (!(u8)shape_class_cached(FW(mem, 2))) break;
                memmove(DSP(mpos), far_mp(far_add(mem, 4)), 12);
                s16 aak = (s16)objtype_aa(FW(mem, 2));
                if (aak == 0) {
                    s16 k = 0;
                    for (u16 si = near; k < cnt; k++, si = (u16)(si + 2))
                        if ((u32)ldist2((u16)(DSW(si) + 4), mpos) < 0x2904) break;
                    if (k == cnt) {
                        if (!close) continue;
                        if ((u32)ldist2((u16)(PLAYER + 4), mpos) > 0x7B0C) continue;
                    }
                }
                memmove(far_mp(dst_far), far_mp(mem), 0x16);
                FW(dst_far, 2) |= 0x8000;
                FW(dst_far, 0x16) = g.off; FW(dst_far, 0x18) = g.seg;
                FW(dst_far, 0x1A) = (u16)j;
                DSW(dst_near + 2) = dst_far.seg; DSW(dst_near) = dst_far.off;
                dst_near = (u16)(dst_near + 4);
                DSW(0x18BE)++; DSW(0x18BA)++;
                if (DSS(0x18BC) >= 0x1E) { dst_far = far_add(dst_far, 0x1C); continue; }
                if (aak != 0 && (FW(g, 0) & 0x1F) != 0x11) {
                    FW(aa, 0) = dst_far.off; FW(aa, 2) = dst_far.seg;
                    FW(aa, 4) = 3;
                    FB(aa, 0x25) = (u8)aak;
                    FB(aa, 0x24) = (FW(g, 0) & 0x1F) == 0x10 ? 1 : 0;
                    rtl__fmemset(far_add(aa, 6), 0, 0x1E);
                    aa = far_add(aa, 0x26);
                    DSW(0x18BC)++;
                }
                dst_far = far_add(dst_far, 0x1C);
            }
        } else {
            if (DSS(0x18BE) >= 200 || DSS(0x18BA) >= 200) { arena_release(mark); stk_release(mk); return; }
            DSW(dst_near + 2) = g.seg; DSW(dst_near) = g.off;
            dst_near = (u16)(dst_near + 4);
            DSW(0x18BA)++;
            DSW(0x5838)++;
        }
    }
    arena_release(mark);
    stk_release(mk);
}

/* AP 088a:01d3 — crashes into the ground, water and ship decks (sim_world_dmg.md §5.1) */
void impacts_update(void)
{
    hitbox_set_enlarge(0);
    u16 mk = stk_mark(), q = stk_alloc(0x10), d = stk_alloc(12);
    for (s16 i = 0; i < DSS(0x4776); i++) {
        u16 p = DSW((u16)(DSW(0x475B) + i * 2));
        s32 g = OS(p, 0xB8);
        if (!obj_alive(p)) continue;
        u8 crash_now = OB(p, 0xAB) != 0 || (g < 0 && g + OS(OW(p, 0x66), 0x98) == OSL(p, 0x0C));
        if (crash_now && !(p == PLAYER && DSB(0x42CF) == 0)) {
            OSL(p, 0x0C) = g + 1;
            u8 hidden = 1;
            if (g == -1) sx_play(0x17, (u16)(p + 4), 0, p == PLAYER);
            else if (g == 0) { sx_play(0x12, (u16)(p + 4), 0, p == PLAYER); hidden = 0; }
            else if (OW(p, 0xEF)) {
                u16 e = dmg_event_alloc();
                if (e) {
                    sx_play(0x12, (u16)(p + 4), 0, p == PLAYER);
                    ev_set(e, far_make(0, 0), OW(p, 0xEF), 0x1F40);
                }
            }
            crash_effect(p, g);
            plane_die(p, hidden);
            ai_forget_plane(p);
            continue;
        }
        g = -1;
        if (collision_ground(q, far_make(cur_dgroup, (u16)(p + 4)), 0)) g = DSSL(q + 4);
        else {
            OW(p, 0xEF) = ship_under((u16)(p + 4), d);
            if (OW(p, 0xEF)) g = DSSL(d + 8);
        }
        OW(p, 0xB8) = (u16)g;
        if (p == PLAYER && DSB(0x42CF) == 0) continue;
        if (OSL(p, 0x0C) >= g) {
            if (OW(p, 0xEF) == 0) continue;
            if (g + OS(OW(p, 0x66), 0x98) != OSL(p, 0x0C)) continue;
            if (!(OB(OW(p, 0xEF), 0x1B) & 2)) continue;
        }
        if (OW(p, 0xEF)) {
            u16 e = dmg_event_alloc();
            if (e) {
                sx_play(0x12, (u16)(p + 4), 0, p == PLAYER);
                ev_set(e, far_make(0, 0), OW(p, 0xEF), 0x1F40);
            }
        }
        crash_effect(p, g);
        plane_die(p, 1);
        ai_forget_plane(p);
    }
    stk_release(mk);
}

/* ==== 0fd4: G load and blackout ================================================================== */

/* AP 0fd4:0007 */
void g_load_reset(void)
{
    DSW(0x579B) = 0; DSB(0x579A) = 0; DSW(0x5798) = 0;
    memset(DSP(0x578C), 0, 12);
    DSW(0x5792) = 2;
}

/* AP 0fd4:0033 — sim_world_dmg.md §7 */
void g_load_update(void)
{
    if (DSW(0x5792) != 0) {
        if (DSW(0x5792)-- == 1) memmove(DSP(0x578C), DSP(PLAYER + 0x70), 6);
        return;
    }
    u16 mk = stk_mark(), g = stk_alloc(6), dv = stk_alloc(6), gb = stk_alloc(6);
    memmove(DSP(g), DSP(0x1540), 6);
    memmove(DSP(dv), DSP(0x578C), 6);
    vsub(dv, (u16)(PLAYER + 0x70));
    for (int k = 0; k < 6; k += 2) DSW(dv + k) = (u16)(DSW(dv + k) * 0x100);
    DSW(dv + 4) = (u16)(DSW(dv + 4) + (u16)dt_scale((s16)0xFC00));
    s16 len = (s16)vlen(dv);
    len = (s16)(len < 0 ? -len : len);
    DSW(0x579B) = (u16)steps_to_frames((s16)(len >> 2));
    vec_rotate(g, obj_matrix(OW(PLAYER, 0x20)), gb);
    u32 dot = vdot(dv, gb);
    u16 r = (u16)(dot >> 16) | (u16)dot;
    if (r != 0 && !(r & 0x8000)) DSW(0x579B) = (u16)-DSW(0x579B);   /* or ax,dx / jle */
    memmove(DSP(0x578C), DSP(PLAYER + 0x70), 6);
    stk_release(mk);
    s16 G = DSS(0x579B);
    if (DSB(0x579A)) {
        if (G > 0x514 || G < (s16)0xFC31) return;
        if (timer16_dec(0x5796) != 0) return;
        DSB(0x579A) = 0;
        return;
    }
    if (DSB(0x42CC) == 0) { DSW(0x5798) = 0; return; }
    if (G >= 0x514 || G <= (s16)0xFC31) {
        if (G >= 0) DSW(0x5794) = (u16)(G - 0x514);
        else { s16 x = (s16)(G + 0x3CF); DSW(0x5794) = (u16)(x < 0 ? -x : x); }
        DSW(0x5794) = (u16)(DSW(0x5794) - (u16)(DSS(0x5794) >> 2));
        approach_dt(0x5798, 0x3FFF, DSS(0x5794));
        if (DSW(0x5798) == 0x3FFF) { DSB(0x579A) = 1; DSW(0x5796) = 0x9C4; }
        return;
    }
    approach_dt(0x5798, 0, 0x320);
}

/* ==== 4b3a: damage data ========================================================================== */

/* AP 4b3a:0000 — effect animation <name>.BMD into shape slot `slot` */
static void effect_register(u16 slot, u16 bank, u16 name, u16 dur)
{
    u16 r = res_name(name, 0x05EE);
    if (!bmd_load_far((s16)slot, (s16)bank, r)) assert_fail(far_make(SEG(0x384A), 0), 0x1D3, r);
    shape_set_radius_cached(slot, dur);
}

/* AP 4b3a:0061 — the 21 effect animations at DS:474D + 0Ch..20h */
static void effects_register_all(void)
{
    static const u16 tab[21][2] = {
        { 0x174, 0xC8 }, { 0x17A, 0xC8 }, { 0x180, 0xC8 }, { 0x186, 0xC8 }, { 0x18C, 0x708 }, { 0x194, 0x1F4 },
        { 0x19C, 0x1F4 }, { 0x1A3, 0xC8 }, { 0x1AB, 0xC8 }, { 0x1B3, 0xBD0 }, { 0x1BB, 0xBD0 }, { 0x1C2, 0xBD0 },
        { 0x1C7, 0x708 }, { 0x1CF, 0x118 }, { 0x1D7, 0xF0 }, { 0x1DF, 0x43 }, { 0x1E4, 0x78 }, { 0x1EC, 0x44C },
        { 0x1F4, 0x44C }, { 0x1FC, 0x44C }, { 0x204, 0x44C } };
    for (int k = 0; k < 21; k++)
        effect_register((u16)(DSW(0x474D) + 0x0C + k), DSW(0x3F06), tab[k][0], tab[k][1]);
}

/* AP 4b3a:025c */
void damage_init(void)
{
    DSW(0x3F06) = sprite_count();
    DSW(0x519C) = sprite_set_load2(0, res_name(0x020C, 0x05B4));
    DSW(0x474D) = 0;
    effects_register_all();
}

/* AP 4b3a:028f */
void damage_tables_alloc(u16 n) { DSW(0x0172) = arena_alloc((u16)(n << 1)); }

/* AP 4b3a:02a5 */
void damage_nop(void) {}

/* AP 4b3a:02aa — the player's engine/oil state */
void damage_reset(void)
{
    memset(DSP(0x5196), 0, 6);
    memset(DSP(0x5190), 0, 6);
    DSW(0x5190) = 0x7FFF; DSW(0x5196) = 0x7FFF;
    DSW(0x5192) = 0x200; DSW(0x5198) = 0x200;
    DSB(0x0FE8) = 0; DSB(0x0FE9) = 0; DSB(0x0FEA) = 0; DSB(0x0FEB) = 0; DSB(0x0FED) = 0; DSB(0x0FEC) = 0;
    DSL(0x0FEE) = 0;
    DSW(0x0FF2) = 0;
    DSB(0x0FF4) = 0;
    warn_init(0x516E, 3, 4, 3);
    warn_init(0x5164, 1, 0x36, 3);
    DSL(0x515C) = 0xFFFFFFFFu;
    DSL(0x5160) = 0xFFFFFFFFu;
}

/* AP 4b3a:0341 — .DMG image into slot (the second read asks n records of size-2 bytes: EOF ends it) */
void dmg_load(u16 slot, u16 name, u16 is_ship)
{
    u16 mk = stk_mark(), hdr = stk_alloc(2);
    u16 f = my_fopen(name, 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x384A), 0), 0x2A3, name);
    my_fread(hdr, 2, 1, f);
    u16 n = DSW(hdr) & 0x0F;
    u16 size = (u8)is_ship ? (u16)((n << 3) + 4) : (u16)(n * 10 + 0x10);
    u16 di = arena_alloc(size);
    my_fread((u16)(di + 2), (s16)(size - 2), (s16)n, f);
    DSW(di) = DSW(hdr);
    my_fclose(f);
    DSW((u16)(DSW(0x0172) + slot * 2)) = di;
    stk_release(mk);
}

/* AP 4b3a:03ff — damage state: +0 total, +2 obj, +4 zone damage, +6 zone bytes, aircraft +8 hit counter,
 * +0C main fuel, +10 drop-tank fuel (from the flight record's fuel byte) */
void dmg_state_create(u16 slot, u16 dmg_out, u16 st_out, u16 obj, u16 f)
{
    u8 ship = OB(obj, 0x18) == 1;
    u16 img = DSW((u16)(DSW(0x0172) + slot * 2));
    u16 n = DSW(img) & 0x0F;
    u16 si = arena_alloc(ship ? 8 : 0x14);
    DSW(si) = 0;
    DSW(si + 4) = arena_alloc((u16)(n << 1));
    memset(DSP(DSW(si + 4)), 0, (u16)(n << 1));
    DSW(si + 6) = arena_alloc(n);
    memset(DSP(DSW(si + 6)), 0xFF, n);
    DSW(si + 2) = obj;
    if (!ship) {
        if (f) {
            if (DSB(f + 3) != 0) {
                s16 dx = (s16)(((s32)DSB(f + 4) << 11) >> 8);
                if (dx > 0x200) {
                    DSL(si + 0x0C) = 0x8000000;
                    DSL(si + 0x10) = (u32)((s32)(dx - 0x200) << 18);
                } else {
                    DSL(si + 0x0C) = (u32)((s32)dx << 18);
                    DSL(si + 0x10) = 0;
                }
            } else {
                s16 v = (s16)(((s32)DSB(f + 4) << 10) >> 8);
                DSL(si + 0x0C) = (u32)((s32)v << 18);
                DSL(si + 0x10) = 0;
            }
        }
        DSW(si + 8) = 0;
    }
    DSW(dmg_out) = img;
    DSW(st_out) = si;
    return;
}

/* ==== 655d: damage application =================================================================== */

/* AP 655d:0000 — crash effect on land / water by size class */
void crash_effect(u16 p, s32 g)
{
    s16 cls = (s16)(DSS(OW(p, 0x28)) >> 4) & 3;
    u16 mk = stk_mark(), r = stk_alloc(0x30);
    memset(DSP(r), 0, 0x30);
    DSW(r) = 0x80E;
    DSW(r + 0x1C) = 0x80;
    DSB(r + 0x1E) = 0;
    DSW(r + 0x1A) = SEG(0x3DFF); DSW(r + 0x18) = 0x57;
    memmove(DSP(r + 4), DSP(p + 4), 12);
    DSL(r + 0x0C) = 1;
    if (g >= 0) { DSW(r + 2) = (u16)(DSW(0x474D) + 0x1C); DSW(r + 0x2A) = DSW((u16)(0x0FD2 + cls * 2)); }
    else { DSW(r + 2) = (u16)(DSW(0x474D) + 0x14); DSW(r + 0x2A) = DSW((u16)(0x0FCC + cls * 2)); }
    fx_spawn(r);
    stk_release(mk);
}

/* AP 655d:01e7 */
void plane_mark_down(u16 p)
{
    if (PLAYER != p && OW(p, 0x34) == OW(PLAYER, 0x34)) {
        u16 mk = stk_mark(), ctx = stk_alloc(0x18);
        memset(DSP(ctx), 0, 0x18);                  /* PORT: other fields are stack garbage */
        DSW(ctx + 0x16) = p;
        DSL(ctx + 0x0E) = 0;
        radio_queue7(0x58, ctx, 0);
        stk_release(mk);
    }
    OB(p, 0x1A) |= 0x40;
    ai_forget_plane(p);
}

/* AP 655d:022e — shot down: crew count, brain state FF5h (going down), controls */
void plane_shoot_down(u16 p, u16 how)
{
    s32 g = OS(p, 0xB8);
    if (DSB(0x477C) == 5) how = 0;
    else how = (u16)(how + station_loaded_count((u16)(p + 0x5E)));
    if ((how & 0xFF) && p == PLAYER) how--;
    plane_mark_down(p);
    if (OS(OW(p, 0x66), 0x98) + g + 10 >= OSL(p, 0x0C)) {
        OSL(p, 0x0C) = g;
        crash_effect(p, g);
        plane_die(p, g != 0);
    }
    u16 st = brain_set_state((u16)(p + 0x4A), 0x0FF5);
    if (how & 0xFF) {
        how |= 0x2000;
        if (OB(p, 0xBB) > 0x2C) how |= 0x1000;
        else DSL(st + 6) = 0x1900;
    }
    DSW(st) = how;
    if (how & 0x8000) { OW(p, 0xAE) = 0xC8; OW(p, 0xB2) = 0; }
    else { OW(p, 0xAE) = 0xFF01; OW(p, 0xB2) = 0xFF; }
    if (p == PLAYER) { DSB(0x0FEC) = 1; DSB(0x42BF) = 1; }
}

/* AP 655d:009b — brain state FF5h: spinning / gliding down, crew bailing out (RNG) */
u16 state_going_down(u16 p)
{
    u16 di = OW(p, 0x54);
    u16 st = DSW(di);
    s32 g = OS(p, 0xB8);
    if (OS(OW(p, 0x66), 0x98) + g >= OSL(p, 0x0C)) {
        OSL(p, 0x0C) = g;
        crash_effect(p, g);
        plane_die(p, g != 0);
        return 0;
    }
    if (st & 0x8000) {
        OW(p, 0xAE) = 0xC8;
        if ((OB(p, 0xAA) & 2) || (OB(p, 0xAA) & 1) || OS(p, 0x10) > 0x1F40 || (u16)(OW(p, 0x12) + 0x7D0) >= 0xFA0)
            DSW(di) ^= 0x8000;
    } else {
        if (steer_pitch_to(p, 0xC000, 0x32)) OW(p, 0xB2) = 0xFF;
        OW(p, 0xAC) = 0xFF;
        OW(p, 0xC1) |= 0x10;
    }
    if (st & 0x2000) {
        if (OSL(p, 0x0C) < 0x1F40) DSW(di) ^= 0x2000;
        else {
            timer32_dec((u16)(di + 6));
            if (DSL(di + 6) == 0) {
                if (st & 0x1000) {
                    parachute_spawn(p);
                    DSW(di)--;
                    if (!(DSW(di) & 0xFF)) DSW(di) ^= 0x2000;
                    else DSL(di + 6) = 0xF00;
                } else if (rng_mod(RNG_ST, 0x3FF) <= 0x133) DSW(di) |= 0x1000;
                else DSL(di + 6) = 0x1900;
            }
        }
    }
    plane_fly(p);
    return 0;
}

/* AP 655d:0322 — GDAMAGE.DAT, SHIPSIZ.DAT, three object classes */
void damage_tables_load(void)
{
    file_load(0x5A82, 8, 0x13, res_name(0x101D, 0x05BC));
    file_load(0x5178, 8, 3, res_name(0x1025, 0x05BC));
    class_register(0x0FF5);
    class_register(0x1009);
    class_register(0x1384);
    bm_set_size_cap(0x200);
}

/* AP 655d:0842 — engine hit on the player: power -9Ah (engine 2 for two-engine types with r odd) */
static void player_engine_hit(u16 img, u16 r)
{
    u16 f = ((DSW(img) & 0x0F00) >> 8) == 2 && (r & 1) ? 0xA6 : 0xA4;
    s32 v = (s32)OS(PLAYER, f) - 0x9A;
    OS(PLAYER, f) = v >= 0 ? (s16)v : 0;
}

/* AP 655d:0887 — engine fire: fire flag, effect, sound, destroyed */
static void plane_fire(u16 p, u16 st, u16 r)
{
    DSB((u16)(OW(p, 0x16) + (r & 1) + 3)) = 1;
    hit_spark(p, (u16)(p + 4), (u16)(DSW(0x474D) + 0x1C));
    sx_play(5, (u16)(p + 4), 0, p == PLAYER);
    u16 bx = (u16)(DSW(st + 4) + (r & 1) * 2);
    DSW(st) = 0x400;
    DSW(bx) = 0x400;
    plane_shoot_down(p, 1);
}

/* AP 655d:1f98 — zone damage, saturating at 400h */
static u16 zone_add(u16 st, u16 z, s16 d)
{
    u16 bx = (u16)(DSW(st + 4) + z * 2);
    s16 dx = DSS(bx);
    if (dx != 0x400) {
        dx = (s16)(dx + d);
        if (dx > 0x400) dx = 0x400;
        DSS(bx) = dx;
    }
    return (u16)dx;
}

/* AP 655d:1fd0 */
static void clamp_word(u16 p, s16 lo, s16 hi)
{
    s16 v = DSS(p);
    if (v < lo) DSS(p) = lo;
    else if (v > hi) DSS(p) = hi;
}

/* AP 655d:1c75 — nearest zone gets the damage; engine zones reduce the engine power */
static u16 plane_hit_zone(u16 p, u16 rel, s32 d)
{
    u16 img = OW(p, 0x28), zp = (u16)(img + 0x10), st = OW(p, 0x2A);
    s16 eng = (s16)((DSW(img) & 0x0F00) >> 8);
    s32 best = 0x7FFFFFFF;
    s16 z = -1;
    for (s16 i = 0; (s16)(DSW(img) & 0x0F) > i; i++, zp = (u16)(zp + 10)) {
        s32 dd = (s32)vdist(zp, rel);
        if (dd < best) { best = dd; z = i; }
    }
    s32 dd = (z < eng && eng != 1) ? (d * DSS(img + 4)) >> 10 : (d * DSS(img + 2)) >> 10;
    s16 v = dd > 0x400 ? 0x400 : (s16)dd;
    zone_add(st, (u16)z, v);
    if (z >= eng || eng == 1) DSW(st) = DSW((u16)(DSW(st + 4) + z * 2));
    if (z < eng || eng == 1) {
        if (p != PLAYER) {
            /* `cdq; idiv word` — DX is the sign of the zone coefficient left in EAX (non-negative in the data) */
            s32 r = (s32)OS(p, 0xA4) - (s16)((u16)v / (u16)eng);
            OS(p, 0xA4) = r >= 0 ? (s16)r : 0;
        } else {
            u16 f = z == 0 ? 0xA4 : 0xA6;
            s32 r = (s32)OS(p, f) - v;
            OS(p, f) = r >= 0 ? (s16)r : 0;
            if (eng == 2) {
                u16 si = (u16)(img + 0x10);
                s16 s0 = DSS(si) ? (DSS(si) < 0 ? -1 : 1) : 0;
                s32 acc = (s16)(s0 * OS(PLAYER, 0xA4));
                si = (u16)(si + 10);
                s16 s1 = DSS(si) ? (DSS(si) < 0 ? -1 : 1) : 0;
                acc += (s16)(s1 * OS(PLAYER, 0xA6));
                OS(PLAYER, 0xA8) = (s16)(acc >> 2);
            }
        }
    }
    return (u16)z;
}

/* AP 655d:1e2f — ship: nearest zone, total and zone saturating at 400h (pair = &obj+28: image, state) */
static u16 ship_hit_zone(u16 pair, u16 rel, s32 d)
{
    u16 img = DSW(pair), zp = (u16)(img + 4);
    s32 best = 0x7FFF;
    s16 z = 0;
    for (s16 i = 0; (s16)(DSW(img) & 0x0F) > i; i++, zp = (u16)(zp + 8)) {
        s32 dd = (s32)vdist(zp, rel);
        if (dd < best) { best = dd; z = i; }
    }
    s32 v = (d * DSS(img + 2)) >> 10;
    if (v >= 0x400) v = 0x400;
    u16 st = DSW(pair + 2);
    DSW(st) = (u16)(DSW(st) + (u16)v);
    if (DSS(st) >= 0x400) DSW(st) = 0x400;
    if (zone_add(st, (u16)z, (s16)v) == 0x400) DSW(DSW(pair + 2)) = 0x400;
    return (u16)z;
}

/* AP 655d:1f03 / 1f69 */
void plane_damage_all(u16 p, u16 v)
{
    u16 img = OW(p, 0x28), st = OW(p, 0x2A), di = DSW(st + 4);
    DSW(st) = v;
    for (s16 i = 0; (s16)(DSW(img) & 0x0F) > i; i++, di = (u16)(di + 2)) DSW(di) = v;
    s32 r = (s32)OS(p, 0xA4) - (s16)v;
    OS(p, 0xA4) = r >= 0 ? (s16)r : 0;
    if (PLAYER == p && ((DSW(img) & 0x0F00) >> 8) == 2) {
        r = (s32)OS(p, 0xA6) - (s16)v;
        OS(p, 0xA6) = r >= 0 ? (s16)r : 0;
    }
}
void obj_damage_all(u16 o, u16 v)
{
    u16 img = OW(o, 0x28), st = OW(o, 0x2A), si = DSW(st + 4);
    DSW(st) = v;
    for (s16 i = 0; (s16)(DSW(img) & 0x0F) > i; i++, si = (u16)(si + 2)) DSW(si) = v;
}

/* AP 655d:08fd — player criticals (numbering from the code's jump table) */
static void player_critical(u16 c, u16 p, u16 img, u16 st, s16 kind, u16 r)
{
    switch (c) {
    case 0: DSB(0x0FEB) = 1; msg_str(3, 0, 3); break;
    case 1:
        if (!(DSW(img) & 0x8000)) { DSB(0x0FED) = 1; msg_str(3, 4, 3); }
        else { DSB(0x0FEA) = 1; msg_str(3, 1, 3); }
        break;
    case 2:
        if (!(OB(p, 0x1A) & 4)) break;
        OB(p, 0x1A) &= 0xFB;
        DSL(st + 0x10) = 0;
        hit_spark(p, (u16)(p + 4), (u16)(DSW(0x474D) + 0x1C));
        sx_play(5, (u16)(p + 4), 0, p == PLAYER);
        lsub_v((u16)(p + 4), 0x0FC6);                   /* quirk kept: moves the plane by (0,0,+100) */
        plane_hit_zone(p, 0x0FC6, 0x96);
        if (DSS(st) > DSS(st + 8)) DSW(st + 8) = (u16)(DSW(st + 8) + 0x100);
        msg_str(3, 3, 3);
        break;
    case 3: {
        u8 dl = 0;
        if (kind == 2) {
            if (r & 1) {
                if (!DSB(0x0FE8)) { DSB(0x0FE8) = 1; dl = 1; }
                else if (!DSB(0x0FE9)) { DSB(0x0FE9) = 1; dl = 1; }
            }
        } else if (!DSB(0x0FE8)) { DSB(0x0FE8) = 1; dl = 1; }
        if (dl) msg_str(3, 2, 3);
        break;
    }
    case 4:
        if (ap_station_count((u16)(PLAYER + 0x5E))) {
            DSW(0x3EF5) = 1;
            station_drop((u16)(PLAYER + 0x5E), 0);
        }
        break;
    case 5: player_engine_hit(img, r); break;
    case 6:
        if (DSS(st) > 0x1C2) { DSB(0x0FEB) = 1; DSW(0x0FF2) = 7; DSB(0x0FF4) = 0; DSL(0x0FEE) = 0; }
        break;
    }
}

/* AP 655d:0b08 — AI criticals */
static void ai_critical(u16 c, u16 p, u16 img, u16 st, u16 cls, u16 rel, s16 kind, s16 r)
{
    switch (c) {
    case 0:
        if (kind != 1) break;
        if (!(OB(p, 0x1A) & 2)) OW(p, 0xA4) = (u16)(OW(p, 0xA4) - 0x100);
        OB(p, 0x1A) |= 2;
        break;
    case 1:
        if ((DSW(img) & 0x8000) && DSS((u16)(0x0FAC + cls * 2)) < r) break;
        plane_fire(p, st, (u16)r);
        break;
    case 2: station_drop((u16)(p + 0x5E), rel); break;
    case 3: plane_shoot_down(p, 0x8000); DSW(st) = 0x400; break;
    case 4:
        if (kind != 1) break;
        if ((DSW(img) & 0x4000) && DSS((u16)(0x0FB2 + cls * 2)) < r) break;
        OW(p, 0xA4) = 0;
        plane_shoot_down(p, 1);
        DSW(st) = 0x400;
        break;
    }
}

/* AP 655d:0bd9 — fuel burn, then every damage event of the frame (sim_world_dmg.md §4.1; RNG draws
 * in the original order) */
void damage_apply(void)
{
    u16 mk = stk_mark();
    u16 rec = stk_alloc(0x90), wl = stk_alloc(12), v16 = stk_alloc(6), rel = stk_alloc(6), tmp = stk_alloc(6);
    memset(DSP(rec), 0, 0x90);
    for (int k = 0; k < 3; k++) {
        DSW(rec + k * 0x30) = 0x80C;
        DSW(rec + k * 0x30 + 0x1C) = 0x80;
        DSB(rec + k * 0x30 + 0x1E) = 0x10;
    }
    /* fuel */
    for (s16 i = 0; i < DSS(0x4776); i++) {
        u16 di = DSW((u16)(DSW(0x475B) + i * 2));
        if (!obj_alive(di)) continue;
        if (PLAYER == di && DSB(0x42CE) == 0) continue;
        u16 img = OW(di, 0x28), si = OW(di, 0x2A);
        s16 thr = OS(di, 0xB2);
        if (DSL(si + 0x10) != 0) {
            s32 b = (s32)dt_scale((s16)(((s32)thr * DSS(img + 0x0C)) >> 10)) << 1;
            DSSL(si + 0x10) -= b;
            if (DSSL(si + 0x10) < 0) DSL(si + 0x10) = 0;
        } else if (DSL(si + 0x0C) != 0) {
            s32 b = (s32)dt_scale((s16)(((s32)thr * DSS(img + 0x0A)) >> 10)) << 1;
            DSSL(si + 0x0C) -= b;
            if (DSSL(si + 0x0C) < 0) DSL(si + 0x0C) = 0;
        }
        if (PLAYER == di && DSL(si + 0x0C) + DSL(si + 0x10) == 0) OW(di, 0xB2) = 0;
        if ((s32)(DSL(si + 0x0C) + DSL(si + 0x10)) < 0x666666) OB(di, 0x1A) |= 8;
    }
    /* events */
    u16 ev = DSW(0x00E8);
    for (s16 i = 0; i < DSS(0x00EA); i++, ev = (u16)(ev + 8)) {
        s16 nrec = 0;
        for (int k = 0; k < 3; k++) { DSW(rec + k * 0x30 + 0x1A) = SEG(0x3DFF); DSW(rec + k * 0x30 + 0x18) = 0x57; }
        u16 t = DSW(ev + 4);
        FarPtr w = far_make(DSW(ev + 2), DSW(ev));
        u8 is_player = PLAYER == t;
        u16 cls = far_is_null(w) ? 0xFFFF : (FW(w, 0x18) & 0x0F);
        s32 d;
        if (DSW(ev + 6) == 0x1F40) d = 0x7A120;
        else if (DSW(0x1344) == cls || DSW(0x19EC) == cls) d = (s32)DSS(ev + 6) << 4;
        else d = DSS(ev + 6);
        if (is_player && !far_is_null(w) && DSB(0x42CF) == 0) { hit_sound(w, PLAYER); continue; }
        if (!obj_alive(t)) continue;
        if (OB(t, 0x1A) & 0x40) continue;
        DSW(rec + 0x20) = t;
        DSW(rec + 0x50) = t;
        if (!far_is_null(w)) {
            memmove(DSP(wl), far_mp(far_add(w, 4)), 12);
            lsub(wl, (u16)(t + 4));
            l2v(v16, wl);
            vec_rotate(v16, obj_matrix_t(OW(t, 0x20)), rel);
        } else memset(DSP(rel), 0, 6);
        s16 before = 0, after = 0;
        if (OB(t, 0x18) == 1) {                                  /* ship */
            u16 img = OW(t, 0x28), st = OW(t, 0x2A);
            if (cls == DSW(0x1348) && ((s16)(FW(w, 0x18) & 0x3F0) >> 4) < ((DSS(img) >> 4) & 0x0F)) continue;
            before = DSS(st);
            if (before >= 0x400) after = 0x400;
            else {
                if (((OB(t, 0x1E) & 2) && DSW(0x19EC) == cls) || ((OB(t, 0x1E) & 4) && DSW(0x1344) == cls))
                    d = (s32)((u32)d * (u32)(s32)(s16)(rng_mod(RNG_ST, 7) + 1));
                ship_hit_zone((u16)(t + 0x28), rel, d);
                after = DSS(st);
                if ((s16)(after - before) >= 0xCD && rng_mod(RNG_ST, 0x400) < 0x136) after = 0x400;
                if (DSW(0x19EC) == cls) {
                    u16 r = (u16)(rec + nrec * 0x30); nrec++;
                    DSW(r + 2) = (u16)(DSW(0x474D) + 0x10);
                    memmove(DSP(r + 4), far_mp(far_add(w, 4)), 12);
                    memmove(DSP(wl), DSP(r + 4), 12);
                    lsub(wl, (u16)(t + 4));
                    l2v(rel, wl);
                    vec_rotate(rel, obj_matrix_t(OW(t, 0x20)), tmp);
                    DSW(r + 0x22) = DSW(tmp); DSW(r + 0x24) = DSW(tmp + 2); DSW(r + 0x26) = DSW(tmp + 4);
                    DSW(r + 0x1A) = SEG(0x3DFF); DSW(r + 0x18) = 0x5C;
                }
            }
            if (after >= 0x400) {
                if (DSW(img) & 0x8000) OW(t, 2) = wreck_shape(t);
                else {
                    u16 r = (u16)(rec + nrec * 0x30); nrec++;
                    DSW(r) = 0x80E;
                    DSW(r + 2) = (u16)(DSW(0x474D) + 0x18);
                    memmove(DSP(r + 4), DSP(t + 4), 12);
                    memmove(DSP(rel), DSP(img + 4 + (((DSW(img) & 0x0F) >> 1) << 3)), 6);
                    vec_rotate(rel, obj_matrix_t(OW(t, 0x20)), tmp);
                    DSW(r + 0x22) = DSW(tmp); DSW(r + 0x24) = DSW(tmp + 2); DSW(r + 0x26) = DSW(tmp + 4);
                    ladd_v_c(far_make(cur_dgroup, (u16)(r + 4)), tmp);
                    DSW(r + 0x1A) = SEG(0x3DFF); DSW(r + 0x18) = 0x5C;
                    DSW(r + 0x2A) = DSW((u16)(0x517A + ((DSS(img) >> 8) & 3) * 8));
                }
                obj_damage_all(t, 0x400);
                OB(t, 0x1B) |= 2;
                brain_set_state((u16)(t + 0x4A), 0x1009);
            }
        } else if (OB(t, 0x18) == 0) {                           /* aircraft */
            u16 img = OW(t, 0x28), zones = (u16)(img + 0x10), st = OW(t, 0x2A);
            s16 kind = (s16)((DSW(img) & 0x0F00) >> 8);
            s16 r1 = rng_mod(RNG_ST, 0x400);
            s16 r2 = rng_mod(RNG_ST, 0x400);
            if (!(DSW(img) & 0x0F)) continue;
            before = DSS(st);
            s16 z = (s16)plane_hit_zone(t, rel, d);
            if (z >= kind && DSS((u16)(DSW(st + 4) + z * 2)) >= 0x400) { DSW(st) = 0x400; after = 0x400; }
            else after = DSS(st);
            if (!far_is_null(w)) DSW(st + 0x0A) = (u16)(DSW(st + 0x0A) + (u16)(after - before));
            if (after == before) {
                if (!far_is_null(w) && FW(w, 0x16) == PLAYER && t != PLAYER && OB(PLAYER, 0x26) == OB(t, 0x26))
                    msg_str(2, 0, 3);
            } else if (after >= 0x400) {
                if (is_player) {
                    DSB(0x0FED) = 1;
                    if (!far_is_null(w)) DSB(0x3EF7) = 1;
                    plane_damage_all(PLAYER, 0x400);
                } else {
                    s16 k;
                    if (DSW(img) & 0x8000) k = r2 < 0x200 ? 1 : 2;
                    else k = r2 < 0x155 ? 0 : r2 < 0x2AA ? 1 : 2;
                    if (k == 0) plane_fire(t, st, (u16)r2);
                    else if (k == 1) { plane_shoot_down(t, 0x8000); after = 0x400; }
                    else { after = 0x400; plane_shoot_down(t, 1); }
                }
            } else {
                if (before < 0x3FF && after >= 0x3FF) {
                    if (is_player) player_engine_hit(img, (u16)r2);
                    else if (kind == 1) OB(t, 0x1A) |= 2;
                }
                if (!is_player && kind > 1 && (DSW(img) & 0x2000) && z < kind &&
                    DSS((u16)(DSW(st + 4) + z * 2)) >= 0x400) {
                    s16 side = 0;
                    debris_spawn(t, (u16)(zones + z * 10));
                    u16 r = (u16)(rec + nrec * 0x30); nrec++;
                    DSW(r + 2) = (u16)(DSW(0x474D) + 0x1C);
                    memmove(DSP(r + 0x22), DSP(zones + z * 10), 6);
                    DSW(r + 0x1A) = SEG(0x3DFF); DSW(r + 0x18) = 0x5C;
                    if (DSS(r + 0x22) > 0) side = 1;
                    DSB((u16)(OW(t, 0x16) + side + 3)) = 1;
                    DSB((u16)(OW(t, 0x16) + 1)) = 0;
                    DSB(OW(t, 0x16)) = 3;
                    if (OB(t, 0x26) == 0) DSB((u16)(OW(t, 0x16) + side + 5)) = 0;
                    after = 0x400;
                    plane_shoot_down(t, 1);
                }
                s16 hc = DSS(st + 8);
                if (hc >= before && hc < after) {
                    if (is_player) {
                        s16 thr = DSS((u16)(0x0FD8 + ((hc >> 8) & 3) * 2));
                        DSW(st + 8) = (u16)(DSW(st + 8) + 0x100);
                        if (r1 < thr) {
                            u16 si = 0x0FB8;
                            s16 c = 0;
                            while (DSS(si) < r2) { c++; r2 = (s16)(r2 - DSS(si)); si = (u16)(si + 2); if (c >= 7) break; }
                            player_critical((u16)c, t, img, st, kind, (u16)r2);
                        }
                    } else {
                        s16 thr = DSS((u16)(0x0FE0 + ((hc >> 8) & 3) * 2));
                        DSW(st + 8) = (u16)(DSW(st + 8) + 0x100);
                        if (r1 < thr) {
                            s16 ac = (s8)DSB((u16)(plane_dat(t) + 0x38));
                            u16 tb = 0x0F8E;                     /* PORT: classes > 3 used a stale table pointer */
                            if (ac == 1 || ac == 2) tb = 0x0F98;
                            else if (ac == 3) tb = 0x0FA2;
                            s16 c = 0;
                            while (DSS(tb) < r2) { c++; r2 = (s16)(r2 - DSS(tb)); tb = (u16)(tb + 2); if (c >= 5) break; }
                            ai_critical((u16)c, t, img, st, (u16)ac, rel, kind, r2);
                            after = DSS(st);
                        }
                    }
                }
            }
        }
        if (after != before) object_destroyed_notify(t, w, (u16)(after - before));
        for (s16 k = 0; k < nrec; k++) {
            FarPtr e = fp32(fx_spawn((u16)(rec + k * 0x30)));
            if (!far_is_null(e)) FW(e, 0x16) = 0;
        }
    }
    stk_release(mk);
    player_engine_update();
}

/* AP 655d:159d — the player's wounds, leaks, fuel gauges, engine temperatures (sim_world_dmg.md §4.5) */
void player_engine_update(void)
{
    u16 img = OW(PLAYER, 0x28), di = OW(PLAYER, 0x2A);
    s16 eng = (s16)((DSW(img) & 0x0F00) >> 8);
    u16 si = (u16)(PLAYER + 0x64);
    u8 warn = 0;
    if (DSB(0x0FEB)) {
        timer32_dec(0x0FEE);
        u16 dx = (u16)DSL(0x0FEE);
        if (DSB(0x0FF4) == 0) {
            if (dx == 0) {
                if (DSW(0x0FF2) == 7) { pal_fx_set(0x0C, 1, 0x4000); DSL(0x0FEE) = 0x1130; }
                else { pal_fx_set(0x0C, 1, -1); DSL(0x0FEE) = 0xAF0; }
                DSB(0x0FF4) = 1;
            }
        } else if (dx == 0) {
            DSW(0x0FF2)++;
            if (DSW(0x0FF2) == 8) { DSB(0x0FEB) = 2; end_request(1); }
            else { pal_fx_set(0x0C, 0, -1); DSL(0x0FEE) = 0x1770; DSB(0x0FF4) = 0; }
        }
    }
    if (DSB(0x0FEC)) return;
    if (DSB(0x0FE8)) {
        s32 v = (s32)DSS(0x5196) - dt_scale(6);
        if (v < 0) { DSB(0x0FE8) = 0; DSW(0x5196) = 0; } else DSS(0x5196) = (s16)v;
    }
    if (DSB(0x0FE9)) {
        s32 v = (s32)DSS(0x5190) - dt_scale(6);
        if (v < 0) { DSB(0x0FE9) = 0; DSW(0x5190) = 0; } else DSS(0x5190) = (s16)v;
    }
    if (DSB(0x0FEA)) {
        int64_t v = (int64_t)DSSL(di + 0x0C) - ((s32)dt_scale(0xB6) << 10);
        if (v < 0) { DSL(di + 0x0C) = 0; DSB(0x0FEA) = 0; } else DSSL(di + 0x0C) = (s32)v;
    }
    DSW(0x3FE8) = (u16)(DSSL(di + 0x0C) >> 17);
    DSW(0x3FEA) = (u16)(DSSL(di + 0x10) >> 17);
    s32 tot = (s32)(DSL(di + 0x0C) + DSL(di + 0x10));
    if (tot == 0) { DSB(0x42BE) = 1; DSB(0x57DF) = 1; }
    else if (tot < 0x666666) {
        if (!(DSB(0x3FEE) & 0x80)) DSB(0x57E0) = 1;
        DSB(0x3FEE) |= 0x80;
    } else if (DSL(di + 0x10) == 0 && (OB(PLAYER, 0x1A) & 4)) DSB(0x57E1) = 1;
    s16 dmg = DSS(di + 0x0A);
    if (dmg > 0x351) { DSW(0x3EFB) = 3; DSW(0x3EF9) = 3; }
    else if (dmg > 0x2A2) { DSW(0x3EFB) = 2; DSW(0x3EF9) = 2; }
    else if (dmg >= 0x1F4) { DSW(0x3EFB) = 1; DSW(0x3EF9) = 1; }
    if (dmg > 0x100) DSB(0x3EF8) = 1;
    if (eng == 2) {
        s16 o1 = DSS(0x5196), o2 = DSS(0x5190);
        s16 thr = OS(PLAYER, 0xB2);
        if (o1 > o2) OS(PLAYER, 0xB4) = ldiv_sat((s32)thr * o2, o1);
        else if (o1 < o2) {
            OS(PLAYER, 0xB4) = thr;
            OS(PLAYER, 0xB2) = ldiv_sat((s32)thr * o2, o2);   /* quirk kept: the code multiplies by o2, not o1 */
        }
    }
    if (DSB(0x0FED) && DSB(0x0FEC) == 0) plane_shoot_down(PLAYER, 0x4001);
    if (DSB(0x42D3) == 2) {
        if (DSB(0x0FEC) == 0 && DSB(0x42CF) != 0) {
            if (OB(si, 0x46) & 8) warn_tick(0x5164);
            else if (OB(si, 0x46) & 0x10) {
                msg_clear_all();
                sx_play(1, (u16)(PLAYER + 4), 0, 1);
                msg_str(1, 0x37, 3);
                plane_shoot_down(PLAYER, 0x4001);
            }
        }
        u16 f = OW(si, 2);
        if (OS(f, 0xC6) != 0 && OS(si, 0x30) > OS(f, 0xC6)) DSB(0x57E2) = 1;
        if (OS(f, 0xC0) != 0 && OS(si, 0x30) > OS(f, 0xC0)) DSB(0x57DD) = 1;
    }
    if (DSS(0x5196) < 0x7D00) DSB(0x3FEE) |= 4;
    if (DSB(0x42CB) != 0) {
        for (int e = 0; e < (eng == 2 ? 2 : 1); e++) {
            u16 oil = e ? 0x5190 : 0x5196, tmp = e ? 0x5192 : 0x5198, tmr = e ? 0x5194 : 0x519A;
            u16 pw = e ? 0x42 : 0x40;
            if (e && DSS(0x5190) < 0x7D00) DSB(0x3FEE) |= 8;
            s16 bx = (s16)(OS(PLAYER, 0xB2) + (0xFF - (DSS(oil) >> 7)));
            s16 step = bx > 0xF4 ? 10 : bx < 0xE8 ? -10 : 0;
            DSW(tmp) = (u16)(DSW(tmp) + (u16)dt_scale(step));
            clamp_word(tmp, 0, 0x1388);
            u8 hot = DSS(tmp) > 0x682;
            DSB(0x3FEE) &= e ? 0xFD : 0xFE;
            DSB(0x3FEE) |= (u8)(hot << e);
            if (hot && OW(si, pw) != 0) DSB(0x57DE) = 1;
            if (DSS(tmp) > 0x9C4 && timer16_dec(tmr) == 0) {
                s32 v = (s32)OS(si, pw) - 0x66;
                OS(si, pw) = v >= 0 ? (s16)v : 0;
                DSW(tmr) = 0x3200;
            }
        }
        if (DSS(di) >= 0x400) DSB(0x0FED) = 1;
    } else {
        DSB(0x3FEE) &= 0xFD; DSB(0x3FEE) &= 0xFE;
    }
    for (int e = 0; e < 2; e++) {
        u16 pw = e ? 0x42 : 0x40, tm = e ? 0x515C : 0x5160;
        if (OW(si, pw) != 0 && OS(si, pw) <= 0x7C) {
            if (MTIME > DSL(tm)) OW(si, pw) = 0;
            else if (DSL(tm) == 0xFFFFFFFFu) DSL(tm) = MTIME + 0x12C00;
            warn = 1;
        }
    }
    if (warn) warn_tick(0x516E);
}

/* AP 655d:1ada — effect callback: next animation frame, done at the last */
u16 fx_anim_step(FarPtr e)
{
    FB(e, 0x16)++;
    u16 f = FB(e, 0x16);
    return (s16)f < (s16)(shape_nvariants((s16)FW(e, 2), 0) - 1);
}

/* AP 655d:1b0a — effect callback: follow the attached object (offset in its frame) */
u16 fx_attached_step(FarPtr e)
{
    u16 mk = stk_mark(), pos = stk_alloc(12), off = stk_alloc(6), rot = stk_alloc(6);
    u16 o = FW(e, 0x20);
    memmove(DSP(pos), DSP(o + 4), 12);
    memmove(DSP(off), far_mp(far_add(e, 0x22)), 6);
    vec_rotate(off, obj_matrix(OW(o, 0x20)), rot);
    ladd_v(pos, rot);
    memmove(far_mp(far_add(e, 4)), DSP(pos), 12);
    stk_release(mk);
    FB(e, 0x16)++;
    u16 f = FB(e, 0x16);
    return (s16)f < (s16)(shape_nvariants((s16)FW(e, 2), 0) - 1);
}

/* AP 655d:1bb7 — effect callback (gun smoke): drift with a decaying vector while the timer runs */
u16 fx_drift_step(FarPtr e)
{
    if (timer16_dec_far(far_add(e, 0x28)) == 0) return 0;
    u16 mk = stk_mark(), v = stk_alloc(6), w = stk_alloc(6);
    memmove(DSP(v), far_mp(far_add(e, 0x22)), 6);
    memmove(DSP(w), DSP(v), 6);
    vadd(v, vneg(vsar(w, 4)));
    memmove(far_mp(far_add(e, 0x22)), DSP(v), 6);
    vec_dt(v);
    ladd_v_c(far_add(e, 4), v);
    stk_release(mk);
    FB(e, 0x16)++;
    if (FB(e, 0x16) > 3) FB(e, 0x16) = 0;
    return 1;
}

__attribute__((constructor)) static void damage_codeptrs_register(void)
{
    codeptr_register(PROG_AP, 0x655D1ADAu, (CodeFn)fx_anim_step);
    codeptr_register(PROG_AP, 0x655D1B0Au, (CodeFn)fx_attached_step);
    codeptr_register(PROG_AP, 0x655D1BB7u, (CodeFn)fx_drift_step);     /* gun smoke, stub 3DFF:0061 */
    codeptr_register(PROG_AP, 0x655D009Bu, (CodeFn)state_going_down);  /* brain state FF5h (u16 (*)(u16 p)) */
    codeptr_register(PROG_AP, 0x088A0007u, (CodeFn)state_wreck_on_deck);
}
