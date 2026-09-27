/* sim_flow: gauges (678a) — panel tiles, gauge faces, per-step gauge values and the 20 gauge
 * drawing routines of the front cockpit view. sim_flow_cockpit.md §5 (the .GAU loader and the gauge
 * servos are 4cad, in cockpit.c). */
#include "sim_flow/sfa_priv.h"

#define PARITY    DSW(0x3F94)            /* page parity 0/1 */
#define FROZEN    DSB(0x3FCD)            /* gauges frozen (player down) */
#define DIRTY     DSB(0x3FCE)            /* redraw everything once */
#define SET       DSW(0x3F4A)            /* common gauge bitmap set */
#define CACHE(o)  ((u16)(0x3F52 + 0x21 * PARITY + (o)))   /* per-parity cached values (o = 0..20h) */
#define GFR(i)    DSW((u16)(SET + 2 * (i)))                /* frame i of the common set */
#define NEEDLE_COL 0xFA

/* Gauge record (44 bytes, far array DS:3F4E): +00 kind, +02 face name, +10/+18 dirty rects,
 * +20 frame, +22/+24 x, y, +26/+28 centre offset, +2A needle length. */
static s16 g16(FarPtr g, u16 o) { return (s16)rd16(g.seg, (u16)(g.off + o)); }
static s16 pivot_x(FarPtr g) { return (s16)(g16(g, 0x22) + g16(g, 0x26)); }
static s16 pivot_y(FarPtr g) { return (s16)(g16(g, 0x24) + g16(g, 0x28)); }
static FarPtr dirty_rect(FarPtr g) { return far_add(g, (u16)(PARITY * 8 + 0x10)); }

/* AP 678a:0000 */
void gauges_stub(void) {}

/* AP 678a:0005 panel_tiles — tile the screen from row DS:3F48 with random tiles (Borland rand()) */
void panel_tiles(void)
{
    res_chdir(0x05FA /* "GAU" */);
    u16 set = bmp_load(0x3F3A);
    res_chdir_up();
    u16 fr = DSW(set);
    s16 w = DSS(fr + 6), h = DSS(fr + 8);
    for (s16 y = DSS(0x3F48); y <= (s16)(DSS(AP_DS_drv_scr_h) + h); y = (s16)(y + h))
        for (s16 x = 0; x <= (s16)(DSS(AP_DS_drv_scr_w) + w); x = (s16)(x + w))
            bmp_draw(DSW(set), x, y, (s16)(rtl_rand() & 3));
    bmp_free_w(set);
}

/* AP 678a:0088 gauge_faces — each record's face bitmap, frame 0 at (x, y) */
void gauge_faces(void)
{
    res_chdir(0x05FA /* "GAU" */);
    FarPtr g = ds_far(0x3F4E);
    u16 off = 0;
    for (s16 i = 0; i < DSS(0x3F4C); i++, off = (u16)(off + 0x2C)) {
        FarPtr r = far_add(g, off);
        u16 set = bmp_load(far_to_near_str(far_add(r, 2)));
        bmp_draw(DSW(set), g16(r, 0x22), g16(r, 0x24), 0);
        bmp_free_w(set);
    }
    res_chdir_up();
}

/* the engine goal: throttle x condition against the windmilling RPM */
static s16 rpm_goal(u16 p, u16 health_off, s16 w)
{
    s16 t = (s16)(((s32)(s16)(DSW(p + 0xB2) << 4) * DSS(p + health_off)) >> 10);
    if (t > w) return (s16)(w + ((s16)((s16)(t - w) * 3) >> 2));
    return (s16)(t + ((s16)((s16)(w - t) * 5) >> 4));
}

/* servo_step until settled on the first frame (DS:3FCC). TODO(verify): the original's `cdq` uses
 * the stale upper half of EAX for the abs(); treated as a 16-bit abs. */
static s16 servo_settle(u16 s)
{
    s16 v;
    do {
        v = servo_step(s);
        if (!DSB(0x3FCC)) break;
        s16 d = (s16)(DSS(s + 8) - DSS(s + 6));
        if (d < 0) d = (s16)-d;
        if (d <= 0x14) break;
    } while (1);
    return v;
}

/* AP 678a:010b gauges_update — every sim step; sim_flow_cockpit.md §5.2 */
void gauges_update(void)
{
    u16 p = PLAYER();
    u16 model = DSW(p + 0x66);
    DSSL(0x3FD2) = DSSL((u16)(DSW(p + 0x64) + 0x0A)) - (s32)DSS(model + 0x98);
    DSW(0x3FD6) = DSW(p + 0x94);
    DSW(0x3FDA) = DSW(p + 0x12);
    DSW(0x3FDC) = DSW(p + 0x14);
    DSW(0x3FEC) = DSW(p + 0xB0);
    u8 aa = DSB(p + 0xAA);
    DSB(0x3FEE) = (u8)((DSB(0x3FEE) & 0xEF) | ((((aa >> 1) & 1) | (aa & 1)) << 4));
    u8 ba = DSB(p + 0xBA);
    DSB(0x3FEF) = (u8)((DSB(0x3FEF) & 0xFE) | ((ba >> 2) & 1));
    DSB(0x3FEF) = (u8)((DSB(0x3FEF) & 0xF9) | ((ba & 3) << 1));
    DSB(0x3FEF) = (u8)((DSB(0x3FEF) & 0xF7) | ((((ba >> 4) & 1) ? 0 : 1) << 3));
    s16 m = (s16)fm_cruise_speed_here((u16)(p + 0x64));
    s16 w = lerp_map(DSS(p + 0x94), 0, m, 0, 0x1000);
    DSW(0x3F9E) = (u16)rpm_goal(p, 0xA4, w);
    DSW(0x3FDE) = (u16)clamp((s16)(servo_settle(0x3F96) >> 2), 0, 0x400);
    if (DSB(0x3FCF) & 0x40) {
        DSW(0x3FB0) = (u16)rpm_goal(p, 0xA6, w);
        DSW(0x3FE0) = (u16)clamp((s16)(servo_settle(0x3FA8) >> 2), 0, 0x400);
    }
    DSW(0x3FC2) = DSW(p + 0x9C);
    DSW(0x3FD8) = (u16)servo_settle(0x3FBA);
    DSB(0x3FCC) = 0;
}

/* AP 678a:033b gauges_dirty(a, b) — the arguments are ignored */
void gauges_dirty(s16 a0, s16 a1)
{
    DIRTY = 1;
}

/* AP 678a:0345 */
void gauges_freeze(void)
{
    FROZEN = 1;
}

typedef void (*GaugeFn)(FarPtr g, s16 redraw);
static const GaugeFn gauge_fns[20] = {
    gauge_altimeter, gauge_airspeed, gauge_vsi, gauge_bank, gauge_compass, gauge_rpm1, gauge_rpm2,
    gauge_ammo1, gauge_ammo2, gauge_rockets, gauge_fuel, gauge_k11, gauge_lamp0, gauge_lamp1,
    gauge_lamp2, gauge_lamp3, gauge_lamp4, gauge_divebrake, gauge_flaps, gauge_gear,
};

/* AP 678a:034f gauges_draw(redraw) — front view only. PORT: the far pointer table DS:10B6 (stubs
 * 3E0A:005C + 5*kind) is the C table gauge_fns (same order). */
void gauges_draw(s16 redraw)
{
    if (view_get() != 0) return;
    if (DIRTY) {
        redraw = 1;
        DIRTY = 0;
    }
    FarPtr g = ds_far(0x3F4E);
    for (s16 i = 0; i < DSS(0x3F4C); i++, g = far_add(g, 0x2C)) {
        u16 kind = rd16(g.seg, g.off);
        if (kind < 20) gauge_fns[kind](g, redraw);
    }
    PARITY ^= 1;
}

/* AP 678a:03ad set_rect(a, b, c, d, far r) */
void set_rect(u16 a0, u16 a1, u16 a2, u16 a3, FarPtr r)
{
    wr16(r.seg, r.off, a0);
    wr16(r.seg, (u16)(r.off + 2), a1);
    wr16(r.seg, (u16)(r.off + 4), a2);
    wr16(r.seg, (u16)(r.off + 6), a3);
}

/* AP 678a:03d0 rect_from_line(x0, y0, x1, y1, far r) — sorted */
static void rect_from_line(s16 x0, s16 y0, s16 x1, s16 y1, FarPtr r)
{
    if (x0 < x1) { wr16(r.seg, r.off, (u16)x0); wr16(r.seg, (u16)(r.off + 4), (u16)x1); }
    else         { wr16(r.seg, r.off, (u16)x1); wr16(r.seg, (u16)(r.off + 4), (u16)x0); }
    if (y0 < y1) { wr16(r.seg, (u16)(r.off + 2), (u16)y0); wr16(r.seg, (u16)(r.off + 6), (u16)y1); }
    else         { wr16(r.seg, (u16)(r.off + 2), (u16)y1); wr16(r.seg, (u16)(r.off + 6), (u16)y0); }
}

/* AP 678a:041d rect_union(near a, near b, far r) */
static void rect_union(u16 a, u16 b, FarPtr r)
{
    s16 v;
    v = DSS(a);     if (DSS(b) <= v) v = DSS(b);         wr16(r.seg, r.off, (u16)v);
    v = DSS(a + 2); if (DSS(b + 2) <= v) v = DSS(b + 2); wr16(r.seg, (u16)(r.off + 2), (u16)v);
    v = DSS(a + 4); if (v <= DSS(b + 4)) v = DSS(b + 4); wr16(r.seg, (u16)(r.off + 4), (u16)v);
    v = DSS(a + 6); if (v <= DSS(b + 6)) v = DSS(b + 6); wr16(r.seg, (u16)(r.off + 6), (u16)v);
}

/* AP 678a:047e needle(x, y, len, angle, far rect) */
static void needle(s16 x, s16 y, s16 len, u16 angle, FarPtr r)
{
    s16 dx = (s16)mul_sin(len, angle);
    s16 dy = (s16)mul_cos(len, angle);
    gfx_line(x, y, (s16)(x + dx), (s16)(y + dy));
    rect_from_line(x, y, (s16)(x + dx), (s16)(y + dy), r);
}

/* AP 678a:04db restore_rect(far r) — copy the rect +-1 from the background page, clipped */
static void restore_rect(FarPtr r)
{
    read_bg();
    write_back();
    s16 x0 = (s16)(rd16(r.seg, r.off) - 1);
    s16 y0 = (s16)(rd16(r.seg, (u16)(r.off + 2)) - 1);
    s16 x1 = (s16)(rd16(r.seg, (u16)(r.off + 4)) + 1);
    s16 y1 = (s16)(rd16(r.seg, (u16)(r.off + 6)) + 1);
    if (DSB(0x26C3)) {
        if (x0 < DSS(0x26C4)) x0 = DSS(0x26C4);
        if (y0 < DSS(0x26C8)) y0 = DSS(0x26C8);
        if (DSS(0x26C6) < x1) x1 = DSS(0x26C6);
        if (DSS(0x26CA) < y1) y1 = DSS(0x26CA);
    }
    if (x0 < x1 && y0 < y1) gfx_copy_rect(x0, y0, (s16)(x1 - x0 + 1), (s16)(y1 - y0 + 1));
}

static void text_setup(void)
{
    gfx_font_select(DSS(0x3F2E));
    DSB(0x26C2) = 1;
    DSB(0x26C0) = NEEDLE_COL;
}

/* AP 678a:0563 gauge_altimeter */
void gauge_altimeter(FarPtr g, s16 redraw)
{
    s32 v = FROZEN ? DSSL(0x3F55) : DSSL(0x3FD2);
    if (!redraw && DSSL(CACHE(3)) == v) return;
    DSSL(CACHE(3)) = v;
    u16 m = stk_mark();
    u16 rb = stk_alloc(8), rs = stk_alloc(8), ru = stk_alloc(8), txt = stk_alloc(4);
    s16 a1 = ldiv_sat((s32)((u32)(v % 400) << 6), 100);
    s16 a2 = ldiv_sat((s32)((u32)v << 6), 1000);
    rtl_sprintf(txt, 0x1106 /* "%02hd" */, (unsigned)(u16)((u16)a2 >> 8));
    DSB(txt + 2) = 0;
    s16 px = pivot_x(g), py = pivot_y(g);
    restore_rect(dirty_rect(g));
    text_setup();
    gfx_text(txt, (s16)(px - 4), (s16)(py + 4));
    DSB(0x26CE) = NEEDLE_COL;
    needle(px, py, g16(g, 0x2A), (u16)((0x80 - a1) << 8), ds_ptr(rb));
    needle(px, py, (s16)(g16(g, 0x2A) >> 1), (u16)((0x80 - a2) << 8), ds_ptr(rs));
    rect_union(rb, rs, ds_ptr(ru));
    set_rect((u16)(px - 4), (u16)py, (u16)(px + 6), (u16)(py + 8), ds_ptr(rb));
    rect_union(ru, rb, dirty_rect(g));
    stk_release(m);
}

/* one needle over a lerp_map'd value (airspeed, rpm) */
static void needle_gauge(FarPtr g, s16 a)
{
    s16 px = pivot_x(g), py = pivot_y(g);
    restore_rect(dirty_rect(g));
    DSB(0x26CE) = NEEDLE_COL;
    needle(px, py, g16(g, 0x2A), (u16)((0x80 - a) << 8), dirty_rect(g));
}

/* AP 678a:0704 gauge_airspeed */
void gauge_airspeed(FarPtr g, s16 redraw)
{
    s16 v;
    if (!FROZEN) v = DSS(0x3FD6) > 0 ? DSS(0x3FD6) : 0;
    else v = DSS(0x3F59);
    if (!redraw && DSS(CACHE(7)) == v) return;
    DSW(CACHE(7)) = (u16)v;
    needle_gauge(g, lerp_map(v, 0, 7200, 0, 0x100));
}

/* AP 678a:07e6 gauge_vsi */
void gauge_vsi(FarPtr g, s16 redraw)
{
    u16 v = FROZEN ? DSW(0x3F5B) : DSW(0x3FD8);
    if (!redraw && DSW(CACHE(9)) == v) return;
    DSW(CACHE(9)) = v;
    s16 a = v == 0x8000 ? 0x7FFF : ((s16)v < 0 ? (s16)-(s16)v : (s16)v);
    if (a < 0x118) a = lerp_map(a, 0, 0x118, 0, 0x1E);
    else if (a < 0x230) a = lerp_map(a, 0x118, 0x230, 0x1E, 0x40);
    else if (a < 0x690) a = lerp_map(a, 0x230, 0x690, 0x40, 0x80);
    else a = 0x80;
    if ((s16)v < 0) a = (s16)-a;
    s16 px = pivot_x(g), py = pivot_y(g);
    restore_rect(dirty_rect(g));
    DSB(0x26CE) = NEEDLE_COL;
    needle(px, py, g16(g, 0x2A), (u16)((0xC0 - a) << 8), dirty_rect(g));
}

/* AP 678a:090c gauge_bank — artificial horizon bar */
void gauge_bank(FarPtr g, s16 redraw)
{
    s16 v = FROZEN ? DSS(0x3F5D) : DSS(0x3FDA);
    if (!redraw && DSS(CACHE(0x0B)) == v) return;
    DSW(CACHE(0x0B)) = (u16)v;
    s16 dx = (s16)mul_sin(g16(g, 0x2A), (u16)(v + 0x4000));
    s16 dy = (s16)mul_cos(g16(g, 0x2A), (u16)(v + 0x4000));
    s16 px = pivot_x(g), py = pivot_y(g);
    s16 cx = (s16)(px + 7), cy = (s16)(py + 2);
    restore_rect(dirty_rect(g));
    DSB(0x26CE) = NEEDLE_COL;
    gfx_line((s16)(cx + dx), (s16)(cy + dy), (s16)(cx - dx), (s16)(cy - dy));
    bmp_draw(GFR(g16(g, 0x20)), px, py, 0);
    rect_from_line((s16)(cx + dx), (s16)(cy + dy), (s16)(cx - dx), (s16)(cy - dy), dirty_rect(g));
}

/* AP 678a:0a37 gauge_compass — a strip bitmap under a clip window */
void gauge_compass(FarPtr g, s16 redraw)
{
    s16 v = FROZEN ? DSS(0x3F5F) : DSS(0x3FDC);
    if (!redraw && DSS(CACHE(0x0D)) == v) return;
    DSW(CACHE(0x0D)) = (u16)v;
    s16 len = g16(g, 0x2A);
    s16 px = pivot_x(g), py = pivot_y(g);
    s16 xo = (s16)(lerp_map((s16)((u16)(0xFFFF - (u16)v) >> 8), 0, 0xFF, 0, (s16)0xFF35) - 3);
    u8 on = DSB(0x26C3);
    u16 c4 = DSW(0x26C4), c8 = DSW(0x26C8), c6 = DSW(0x26C6), ca = DSW(0x26CA);
    DSB(0x26C3) = 1;
    DSW(0x26C4) = (u16)(px - 1);
    DSW(0x26C8) = (u16)(py - 2);
    DSW(0x26C6) = (u16)(px + 2 * len - 1);
    DSW(0x26CA) = (u16)(py + 6);
    clip_to_view();
    restore_rect(dirty_rect(g));
    bmp_draw(GFR(g16(g, 0x20)), (s16)(px + xo), py, 0);
    DSB(0x26C3) = on;
    DSW(0x26C4) = c4;
    DSW(0x26C8) = c8;
    DSW(0x26C6) = c6;
    DSW(0x26CA) = ca;
    set_rect((u16)(px - 1), (u16)(py - 2), (u16)(px + 2 * len - 1), (u16)(py + 6), dirty_rect(g));
}

/* AP 678a:0b98 gauge_rpm1 */
void gauge_rpm1(FarPtr g, s16 redraw)
{
    s16 v = FROZEN ? DSS(0x3F61) : DSS(0x3FDE);
    if (!redraw && DSS(CACHE(0x0F)) == v) return;
    DSW(CACHE(0x0F)) = (u16)v;
    needle_gauge(g, lerp_map(v, 0, 0x400, 0, 0xE8));
}

/* AP 678a:0c59 gauge_rpm2 */
void gauge_rpm2(FarPtr g, s16 redraw)
{
    s16 v = FROZEN ? DSS(0x3F63) : DSS(0x3FE0);
    if (!redraw && DSS(CACHE(0x11)) == v) return;
    DSW(CACHE(0x11)) = (u16)v;
    needle_gauge(g, lerp_map(v, 0, 0x400, 0, 0xE8));
}

/* ammo counters: background frame (+1 with the lamp), "%04hd" at (px + 12, py) */
static void ammo_gauge(FarPtr g, s16 redraw, u16 live, u16 cache_off, u8 bit, u16 fmt)
{
    s16 v;
    u8 flags;
    if (!FROZEN) { v = DSS(live); flags = DSB(0x3FEE); }
    else         { v = DSS((u16)(0x3F52 + cache_off)); flags = DSB(0x3F71); }
    u8 lamp = (u8)((flags & bit) ? 1 : 0);
    u16 fl = (u16)(0x3F71 + 0x21 * PARITY);
    if (!redraw && DSS(CACHE(cache_off)) == v && (u8)((DSB(fl) & bit) ? 1 : 0) == lamp) return;
    DSW(CACHE(cache_off)) = (u16)v;
    DSB(fl) &= (u8)~bit;
    if (lamp) DSB(fl) |= bit;
    s16 px = pivot_x(g), py = pivot_y(g);
    bmp_draw(GFR(g16(g, 0x20) + (lamp ? 1 : 0)), px, py, 0);
    u16 m = stk_mark();
    u16 txt = stk_alloc(6);
    rtl_sprintf(txt, fmt, (unsigned)(u16)v);
    DSB(txt + 4) = 0;
    DSB(0x26C2) = 1;
    DSB(0x26C0) = NEEDLE_COL;
    gfx_font_select(DSS(0x3F2E));
    restore_rect(dirty_rect(g));
    gfx_text(txt, (s16)(px + 0x0C), py);
    set_rect((u16)(px + 0x0E), (u16)(py + 1), (u16)(px + 0x1D), (u16)(py + 5), dirty_rect(g));
    stk_release(m);
}

/* AP 678a:0d1a gauge_ammo1 */
void gauge_ammo1(FarPtr g, s16 redraw)
{
    ammo_gauge(g, redraw, 0x3FE2, 0x13, 0x20, 0x110C /* "%04hd" */);
}

/* AP 678a:0e6d gauge_ammo2 */
void gauge_ammo2(FarPtr g, s16 redraw)
{
    ammo_gauge(g, redraw, 0x3FE4, 0x15, 0x40, 0x1112 /* "%04hd" */);
}

/* AP 678a:0fc0 gauge_rockets — "%02hd" at the face position */
void gauge_rockets(FarPtr g, s16 redraw)
{
    s16 v = FROZEN ? DSS(0x3F69) : DSS(0x3FE6);
    if (!redraw && DSS(CACHE(0x17)) == v) return;
    DSW(CACHE(0x17)) = (u16)v;
    s16 x = g16(g, 0x22), y = g16(g, 0x24);
    u16 m = stk_mark();
    u16 txt = stk_alloc(4);
    rtl_sprintf(txt, 0x1118 /* "%02hd" */, (unsigned)(u16)v);
    DSB(txt + 2) = 0;
    DSB(0x26C2) = 1;
    DSB(0x26C0) = NEEDLE_COL;
    gfx_font_select(DSS(0x3F2E));
    restore_rect(dirty_rect(g));
    gfx_text(txt, (s16)(x + 3), (s16)(y + 2));
    set_rect((u16)(x + 1), (u16)(y + 2), (u16)(x + 6), (u16)(y + 8), dirty_rect(g));
    stk_release(m);
}

/* AP 678a:109b gauge_fuel — two needles on opposite arcs, lamp bit 7 */
void gauge_fuel(FarPtr g, s16 redraw)
{
    s16 v1, v2;
    u8 fl8;
    if (!FROZEN) { v1 = DSS(0x3FE8); v2 = DSS(0x3FEA); fl8 = DSB(0x3FEE); }
    else         { v1 = DSS(0x3F6B); v2 = DSS(0x3F6D); fl8 = DSB(0x3F71); }
    u8 lamp = (u8)(fl8 >> 7);
    if (v1 < 0) v1 = 0; else if (v1 > 0x3FF) v1 = 0x3FF;
    if (v2 < 0) v2 = 0; else if (v2 > 0x3FF) v2 = 0x3FF;
    u16 fl = (u16)(0x3F71 + 0x21 * PARITY);
    if (!redraw && DSS(CACHE(0x19)) == v1 && DSS(CACHE(0x1B)) == v2 && (u8)(DSB(fl) >> 7) == lamp) return;
    DSW(CACHE(0x19)) = (u16)v1;
    DSW(CACHE(0x1B)) = (u16)v2;
    DSB(fl) = (u8)((DSB(fl) & 0x7F) | (lamp << 7));
    s16 px = pivot_x(g), py = pivot_y(g);
    s16 a1 = lerp_map(v1, 0, 0x3FF, 0x1A, 0x5C);
    s16 a2 = lerp_map(v2, 0, 0x3FF, 0xE6, 0xA2);
    restore_rect(dirty_rect(g));
    bmp_draw(GFR(g16(g, 0x20) + (lamp ? 1 : 0)), px, py, 0);
    DSB(0x26CE) = NEEDLE_COL;
    u16 m = stk_mark();
    u16 ra = stk_alloc(8), rb = stk_alloc(8);
    needle((s16)(px - 1), (s16)(py - 4), g16(g, 0x2A), (u16)(a1 << 8), ds_ptr(ra));
    needle((s16)(px + 0x1B), (s16)(py - 4), g16(g, 0x2A), (u16)(a2 << 8), ds_ptr(rb));
    rect_union(ra, rb, dirty_rect(g));
    stk_release(m);
}

/* AP 678a:1258 gauge_k11 — a frame of rec.20 shifted by the clamped P.B0h value */
void gauge_k11(FarPtr g, s16 redraw)
{
    s16 v = FROZEN ? DSS(0x3F6F) : DSS(0x3FEC);
    if (!redraw && DSS(CACHE(0x1D)) == v) return;
    DSW(CACHE(0x1D)) = (u16)v;
    u16 c = (u16)clamp(v, 1, 0xFF);
    s16 off;
    if (c == 0) {
        off = 0;
    } else {
        s16 a = c == 0x8000 ? 0x7FFF : ((s16)c < 0 ? (s16)-(s16)c : (s16)c);
        off = (s16)(((s32)a * g16(g, 0x2A)) >> 8);
        if ((s16)c < 0) off = (s16)-off;
    }
    s16 x = (s16)(pivot_x(g) + off), y = pivot_y(g);
    restore_rect(dirty_rect(g));
    u16 fr = GFR(g16(g, 0x20));
    bmp_draw(fr, x, y, 0);
    set_rect((u16)x, (u16)y, (u16)(x + DSS(fr + 6) - 1), (u16)(y + DSS(fr + 8) - 1), dirty_rect(g));
}

/* two-state lamp: frame rec.20 (off) / rec.20 + 1 (on) — or reversed (divebrake) */
static void lamp_gauge(FarPtr g, s16 redraw, u16 live, u16 cached, u8 mask, s16 on_first)
{
    u8 b = FROZEN ? DSB(cached) : DSB(live);
    u8 on = (u8)(b & mask);
    u16 c = (u16)(cached + 0x21 * PARITY);
    if (!redraw && (u8)(DSB(c) & mask) == on) return;
    DSB(c) = (u8)((DSB(c) & ~mask) | on);
    s16 fr = g16(g, 0x20);
    if (on_first) fr = (s16)(fr + (on ? 0 : 1));
    else fr = (s16)(fr + (on ? 1 : 0));
    bmp_draw(GFR(fr), pivot_x(g), pivot_y(g), 0);
}

/* AP 678a:1365 */
void gauge_lamp0(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEE, 0x3F71, 0x01, 0); }
/* AP 678a:1419 */
void gauge_lamp1(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEE, 0x3F71, 0x02, 0); }
/* AP 678a:14d5 */
void gauge_lamp2(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEE, 0x3F71, 0x04, 0); }
/* AP 678a:1595 */
void gauge_lamp3(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEE, 0x3F71, 0x08, 0); }
/* AP 678a:1655 */
void gauge_lamp4(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEE, 0x3F71, 0x10, 0); }
/* AP 678a:1715 — dive brakes: frame rec.20 when set, rec.20 + 1 when clear */
void gauge_divebrake(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEF, 0x3F72, 0x01, 1); }

/* AP 678a:17c9 gauge_flaps — frame rec.20 + flap state (0..2) */
void gauge_flaps(FarPtr g, s16 redraw)
{
    u8 b = FROZEN ? DSB(0x3F72) : DSB(0x3FEF);
    u8 st = (u8)((b >> 1) & 3);
    u16 c = (u16)(0x3F72 + 0x21 * PARITY);
    if (!redraw && (u8)((DSB(c) >> 1) & 3) == st) return;
    DSB(c) = (u8)((DSB(c) & 0xF9) | (st << 1));
    s16 fr = g16(g, 0x20);
    if (st == 2) fr = (s16)(fr + 2);
    else if (st == 1) fr = (s16)(fr + 1);
    bmp_draw(GFR(fr), pivot_x(g), pivot_y(g), 0);
}

/* AP 678a:18bc */
void gauge_gear(FarPtr g, s16 redraw) { lamp_gauge(g, redraw, 0x3FEF, 0x3F72, 0x08, 0); }
