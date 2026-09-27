/* sim_flow: the in-flight map, fltmap.c (6cdc) — sim_flow_ui.md §4. The map target DS:521C
 * (12 bytes) / DS:5228 (set) is the autopilot destination. */
#include "sim_flow/sfa_priv.h"

#define TARGET     0x521C
#define TARGET_SET DSB(0x5228)
#define ICONS      DSW(0x521A)          /* fltmap.BMP sprite set */
#define MAP_UNIT   0x5288               /* 21128 world units = 1 mile */

/* AP 6cdc:0000 fltmap_cb_a(it) — dialog callback: inset box + the item's text */
void fltmap_cb_a(FarPtr it)
{
    inset_box_item(far_make(0, 0));
    gfx_font_select(DSS(0x3F2E));
    DSB(0x26C2) = 1;
    DSB(0x26C0) = 0xCC;
    FarPtr cur = ds_far(0x3BCF);
    gfx_text(rd16(cur.seg, (u16)(cur.off + 0x0E)), (s16)(DSS(0x3BC9) + 3), (s16)(DSS(0x3BC7) + 3));
}

/* AP 6cdc:003e fltmap_cb_b(it) — dialog callback: button up/down, result code */
void fltmap_cb_b(FarPtr it)
{
    if (DSW(0x3BCB) & 0x0C) button_down_c(far_make(0, 0));
    else button_up_c(far_make(0, 0));
    if (DSW(0x3BCB) & 0x10) {
        FarPtr cur = ds_far(0x3BCF);
        DSW(0x3BC1) = rd16(cur.seg, (u16)(cur.off + 0x10));
    }
}

/* AP 6cdc:006a world_to_map(&mx, &my, x, y) */
void world_to_map(u16 mx_ds, u16 my_ds, s32 x, s32 y)
{
    s32 a = x / MAP_UNIT, b = y / MAP_UNIT;
    DSW(mx_ds) = (u16)(s16)((a * 0x93) / 0x4B0);
    DSW(my_ds) = (u16)(s16)((b * 0x93) / 0x4B0);
    DSW(my_ds) = (u16)-(s16)DSW(my_ds);
    DSW(mx_ds) = (u16)(DSW(mx_ds) + 0x1A);
    DSW(my_ds) = (u16)(DSW(my_ds) + 0x10);
    DSW(mx_ds) = (u16)(DSW(mx_ds) + 0x49);
    DSW(my_ds) = (u16)(DSW(my_ds) + 0x49);
}

/* AP 6cdc:00db map_to_world(&x, &y, sx, sy) */
void map_to_world(u16 x_ds, u16 y_ds, s16 sx, s16 sy)
{
    s16 bx = (s16)(sx - 0x63);
    s16 cx = (s16)-(s16)(sy - 0x59);
    s32 a = ((s32)bx * 0x4B0) / 0x93;
    s32 b = ((s32)cx * 0x4B0) / 0x93;
    DSSL(x_ds) = a * MAP_UNIT;
    DSSL(y_ds) = b * MAP_UNIT;
}

/* AP 6cdc:014c dir8(h) */
s16 dir8(u8 h)
{
    return (s16)((u8)(h + 0x10) >> 5);
}

static void icon_put(u16 ic, u8 kind, u8 hdg, u16 pos)
{
    DSB(ic) = kind;
    DSB(ic + 1) = hdg;
    memcpy(DSP(ic + 2), DSP(pos), 12);
}

/* AP 6cdc:0161 map_icons(icons) -> n (14-byte icons {kind, heading, pos}) */
s16 map_icons(u16 ic)
{
    s16 n = 0;
    u16 p = PLAYER();
    u16 pf = DSW(p + 0x34);
    s8 type = DSC(0x477C);
    if (type == 0 || type == 1 || type == 4) {
        icon_put(ic, 0, DSB(pf + 0x1C), (u16)(p + 4));
        u8 k;
        if (DSB(p + 0x26) == 1) k = DSB(pf + 3) ? 4 : 6;
        else k = DSB(pf + 3) ? 3 : 5;
        icon_put((u16)(ic + 0x0E), k, 0, (u16)(DSW(pf + 4) + 4));
        n = 2;
    } else {
        u16 f = DSW(0x4753);
        for (s16 i = 0; i < DSS(0x476B); i++, f = (u16)(f + 0x56)) {
            if (DSW(p + 0x34) == f) continue;
            u16 e = (u16)(ic + 0x0E * n);
            if (DSB(f) == 0) {
                u16 lead = DSW(DSW(f + 8));
                if (DSB(f + 2) != 3 && DSB(f + 2) != 2) continue;
                icon_put(e, DSB(f + 0x42) == 1 ? 2 : 1, DSB(f + 0x1C), (u16)(lead + 4));
                n++;
            } else if (DSB(f) == 1) {
                icon_put(e, DSB(f + 0x42) == 1 ? 4 : 3, DSB(f + 0x1C), (u16)(DSW(DSW(f + 8)) + 4));
                n++;
            }
        }
        u16 pl = DSW(0x4755);
        u16 e = (u16)(ic + 0x0E * n);
        /* quirk kept: the place pointer is never advanced, every place icon shows the first place */
        for (s16 i = 0; i < DSS(0x476E); i++, e = (u16)(e + 0x0E)) {
            DSB(e) = DSB(pl + 0x1D) == 3 ? 6 : 5;
            DSB(e + 1) = 0;
            FarPtr src = far_make(DSW(0x5148), (u16)(DSW(0x5146) + DSW(pl + 0x18) * 0x16 + 4));
            memcpy(DSP(e + 2), far_mp(src), 12);
            n++;
        }
        icon_put((u16)(ic + 0x0E * n), 0, DSB(pf + 0x1C), (u16)(p + 4));
        n++;
    }
    u16 t = map_target();
    if (t) {
        icon_put((u16)(ic + 0x0E * n), 7, 0, t);
        n++;
    }
    return n;
}

/* AP 6cdc:0408 map_icons_draw(icons, n) */
void map_icons_draw(u16 ic, s16 n)
{
    u16 m = stk_mark();
    u16 xy = stk_alloc(4);
    u16 fr = 0;
    for (s16 i = 0; i < n; i++, ic = (u16)(ic + 0x0E)) {
        s8 kind = DSC(ic);
        world_to_map(xy, (u16)(xy + 2), DSSL(ic + 2), DSSL(ic + 6));
        if (kind >= 0 && kind <= 2) {
            s16 d = (s16)(s8)dir8(DSB(ic + 1));
            DSW(xy) -= 4;
            DSW(xy + 2) -= 4;
            fr = DSW((u16)(ICONS + 2 * (kind * 8 + d)));
        } else if (kind >= 3 && kind <= 4) {
            DSW(xy) -= 8;
            DSW(xy + 2) -= 3;
            fr = DSW((u16)(ICONS + 2 * kind + 0x2A));
        } else if (kind >= 5 && kind <= 6) {
            DSW(xy) -= 4;
            DSW(xy + 2) -= 2;
            fr = DSW((u16)(ICONS + 2 * kind + 0x2A));
        } else if (kind == 7) {
            DSW(xy) -= 1;
            DSW(xy + 2) -= 1;
            fr = DSW(ICONS + 0x38);
        }
        bmp_draw(fr, DSS(xy), DSS(xy + 2), 0);
    }
    stk_release(m);
}

/* AP 6cdc:04fd route_points(pts) -> n (16-byte points {pos, far text}) */
s16 route_points(u16 pts)
{
    u16 p = PLAYER();
    s8 type = DSC(0x477C);
    s16 n;
    memcpy(DSP(pts), DSP(p + 4), 12);
    if (type == 0 || type == 1 || type == 4) {
        if (type == 0 || type == 1) ds_far_wr((u16)(pts + 0x0C), str(0, 0xAE));
        else DSL(pts + 0x0C) = DSL(0x4749);
        n = 1;
        u16 t = map_target();
        if (t) {
            memcpy(DSP(pts + 16 * n), DSP(t), 12);
            ds_far_wr((u16)(pts + 16 * n + 0x0C), str(0, 0xAF));
            n++;
        }
        return n;
    }
    ds_far_wr((u16)(pts + 0x0C), str(0, 0xAE));
    n = 1;
    u16 t = map_target();
    if (t) {
        memcpy(DSP(pts + 16 * n), DSP(t), 12);
        ds_far_wr((u16)(pts + 16 * n + 0x0C), str(0, 0xAF));
        n++;
    }
    u16 cur = (u16)(DSW(p + 0x34) + 0x16);
    u16 w = wp_cursor_pos(cur, 0);
    u16 e = (u16)(pts + 16 * n);
    for (s16 i = 0; w; i++, e = (u16)(e + 0x10), n++) {
        u16 wp = wp_cursor_get(cur, (u16)i);
        memcpy(DSP(e), DSP(w), 12);
        u32 txt = DSC(wp + 1) >= 0 ? message_text((u16)(s16)DSC(wp + 1)) : 0;
        DSL(e + 0x0C) = txt;
        w = wp_cursor_pos(cur, (u16)(i + 1));
    }
    return n;
}

/* AP 6cdc:06cb bearing_deg(a, b) -> 0..359 */
s16 bearing_deg(u16 a, u16 b)
{
    s32 dx = DSSL(b) - DSSL(a), dy = DSSL(b + 4) - DSSL(a + 4);
    s32 v = (s16)atan2_32((u16)dx, (u16)((u32)dx >> 16), (u16)dy, (u16)((u32)dy >> 16));
    v -= 0x4000;
    v = 0x10000 - v;
    u32 r = ((u32)(v * 0x168)) >> 16;
    return (s16)(r % 0x168);
}

/* AP 6cdc:072f miles(a, b) */
s16 miles(u16 a, u16 b)
{
    return (s16)(ldist2(a, b) / MAP_UNIT);
}

/* AP 6cdc:0761 feet(z) */
s32 feet(s32 z)
{
    return z / 4;
}

/* AP 6cdc:077a input_abort() -> 2 Esc, 1 any key / button, 0 */
s16 input_abort(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x26);
    input_poll(in);
    s16 r = 0;
    if (DSB(in + 0x1B) == 1) r = 2;
    else if (DSB(in + 0x1B) != 0 || (DSB(in + 0x23) & 0xE0) || (DSB(in + 0x24) & 1)) r = 1;
    stk_release(m);
    return r;
}

/* wait up to DS:31E5 ticks or until input; 1 on Esc */
static s16 wait_or_input(void)
{
    s16 r = input_abort();                   /* (the first result is ignored) */
    for (;;) {
        if (DSW(AP_DS_timer_countdown) == 0) return 0;
        host_pump();                          /* PORT: the busy-wait pumps the host */
        r = input_abort();
        if (r == 2) return 1;
        if (r != 0) return 0;
    }
}

static void plot2(s16 x, s16 y)
{
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    gfx_put_pixel(x, y, 0xA3);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_front);
    gfx_put_pixel(x, y, 0xA3);
}

/* AP 6cdc:07da route_segment(x0, y0, x1, y1) -> 1 when aborted (Esc). TODO(verify): the `cdq`
 * after the 16-bit `imul` sign-extends a stale EAX; modelled as a 16-bit product. */
u8 route_segment(s16 x0, s16 y0, s16 x1, s16 y1)
{
    s16 dx = (s16)(x1 - x0), dy = (s16)(y1 - y0);
    if (dx == 0 && dy == 0) return 0;
    while (input_abort()) host_pump();
    s16 adx = dx < 0 ? (s16)-dx : dx, ady = dy < 0 ? (s16)-dy : dy;
    if (adx > ady) {
        for (s16 si = 0; dx > 0 ? si <= dx : si >= dx; si = (s16)(si + (dx > 0 ? 1 : -1))) {
            s16 di = idiv16_retry((s32)(s16)(dy * si), dx, NULL);
            plot2((s16)(x0 + si), (s16)(y0 + di));
            DSW(AP_DS_timer_countdown) = 0x0F;
            if (wait_or_input()) return 1;
        }
    } else {
        for (s16 di = 0; dy > 0 ? di <= dy : di >= dy; di = (s16)(di + (dy > 0 ? 1 : -1))) {
            s16 si = idiv16_retry((s32)(s16)(dx * di), dy, NULL);
            plot2((s16)(x0 + si), (s16)(y0 + di));
            DSW(AP_DS_timer_countdown) = 0x0F;
            if (wait_or_input()) return 1;
        }
    }
    while (input_abort()) host_pump();
    return 0;
}

/* AP 6cdc:098e route_info_box(a, b) -> 1 when aborted: bearing/distance/altitude of the leg a -> b
 * and the point's text, shown for DS:31E5 = 58Ch ticks */
u8 route_info_box(u16 a, u16 b)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x14), line = stk_alloc(0x60);
    FarPtr txt = ds_far((u16)(b + 0x0C));
    s16 y = 5, brg = 0, mi = 0;
    s32 ft = 0;
    if (a) {
        brg = bearing_deg(a, b);
        mi = miles(a, b);
        ft = feet(DSSL(b + 8));
    }
    tl_init(0, 7, 0);
    DSW(0x3BC9) = 0xB8;
    DSW(0x3BC7) = 0x62;
    DSW(0x3BC5) = 0x66;
    DSW(0x3BC3) = (u16)(!far_is_null(txt) ? (s16)(s8)tl_format((u32)txt.seg << 16 | txt.off, 1, 0) : 0);
    DSW(0x3BC3) += a ? 0x1B : 0;
    DSW(0x3BC3) += 0x0A;
    DSW(0x3BC5) = 0x70;
    if ((s16)(DSS(0x3BC7) + DSS(0x3BC3)) > 0xC5) DSW(0x3BC7) = (u16)(0xC5 - DSS(0x3BC3));
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    framed_panel(far_make(0, 0));
    if (!far_is_null(txt)) {
        DSW(0x3BC9) += 5;
        DSW(0x3BC5) -= 0x0A;
        DSW(0x3BC7) += 5;
        DSB(0x26C0) = 0xC5;
        tl_init(1, 7, 1);
        y = (s16)(y + (s16)(s8)tl_format((u32)txt.seg << 16 | txt.off, 1, 0));
        DSW(0x3BC9) -= 5;
        DSW(0x3BC5) += 0x0A;
        DSW(0x3BC7) -= 5;
    }
    if (a) {
        u16 x = (u16)(DSW(0x3BC9) + 5);
        u16 yy = (u16)(DSW(0x3BC7) + y);
        s16 th = (s16)(ft / 1000);
        if (th) rtl_sprintf(buf, 0x11A7 /* "%d," */, (unsigned)th);
        else DSB(buf) = 0;
        s16 rest = (s16)(ft % 1000);
        rtl_sprintf(line, 0x11AB /* "Altitude: %s%03d feet" */, (unsigned)buf, (unsigned)rest);
        text_engrave(line, 0xC7, 0xB3, 0xC0, x, yy);
        yy = (u16)(yy + 7);
        rtl_sprintf(line, 0x11C1 /* "Bearing: %03d" */, (unsigned)brg);
        text_engrave(line, 0xC7, 0xB3, 0xC0, x, yy);
        yy = (u16)(yy + 7);
        rtl_sprintf(line, 0x11D0, 0x11DE, (unsigned)mi, mi == 1 ? 0x11E7 : 0x11E8);
        text_engrave(line, 0xC7, 0xB3, 0xC0, x, yy);
    }
    DSW(0x26D6) = DSW(AP_DS_drv_page_back);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_front);
    copy_screen();
    DSW(AP_DS_timer_countdown) = 0x58C;
    s16 r = input_abort();
    for (;;) {
        if (DSW(AP_DS_timer_countdown) == 0 || r != 0) break;
        host_pump();
        r = input_abort();
        if (r == 2) { stk_release(m); return 1; }
    }
    while (input_abort()) host_pump();
    DSW(0x26D6) = DSW(0x26D0);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    gfx_copy_rect(DSS(0x3BC9), DSS(0x3BC7), (s16)(DSS(0x3BC5) + 1), (s16)(DSS(0x3BC3) + 1));
    DSW(0x26D6) = DSW(AP_DS_drv_page_back);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_front);
    copy_screen();
    stk_release(m);
    return 0;
}

/* AP 6cdc:0c11 map_route() -> 1 when interrupted */
u8 map_route(void)
{
    u16 m = stk_mark();
    u16 pts = stk_alloc(0xA4), xy = stk_alloc(8);
    u8 r = 0;
    s16 n = route_points(pts);
    if (n > 0) {
        world_to_map(xy, (u16)(xy + 2), DSSL(pts), DSSL(pts + 4));
        if (route_info_box(0, pts)) { r = 1; goto out; }
        for (s16 i = 1; i < n; i++) {
            u16 pt = (u16)(pts + 16 * i);
            world_to_map((u16)(xy + 4), (u16)(xy + 6), DSSL(pt), DSSL(pt + 4));
            DSB(0x26CE) = 0xA3;
            if (route_segment(DSS(xy), DSS(xy + 2), DSS(xy + 4), DSS(xy + 6))) { r = 1; goto out; }
            if (route_info_box((u16)(pt - 16), pt)) { r = 1; goto out; }
            DSW(xy) = DSW(xy + 4);
            DSW(xy + 2) = DSW(xy + 6);
        }
    }
out:
    stk_release(m);
    return r;
}

/* AP 6cdc:0ce7 fltmap_background — fltmap.SCR, the grid, the weather bitmap W<n> */
void fltmap_background(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x0A);
    scr_load_planes(res_name(0x11A0 /* "fltmap" */, 0x0606 /* "SCR" */));
    DSB(0x26CD) = 0x80;
    DSB(0x26CE) = 0x82;
    gfx_rect(0x1A, 0x10, 0x91, 0x91);
    for (s16 si = 0x1A; si <= 0xAA; si = (s16)(si + 0x0C)) gfx_line(si, 0x10, si, 0xA0);
    for (s16 si = 0x10; si <= 0xA0; si = (s16)(si + 0x0C)) gfx_line(0x1A, si, 0xAA, si);
    rtl_sprintf(buf, 0x11EA, (unsigned)DSB(0x477D));
    u16 set = bmp_load(res_name(buf, 0x05B4 /* "BMP" */));
    bmp_draw(DSW(set), 0x1A, 0x10, 0);
    bmp_free_w(set);
    stk_release(m);
}

/* AP 6cdc:0da7 map_reset */
void map_reset(void)
{
    TARGET_SET = 0;
}

/* AP 6cdc:0db1 map_target() -> &DS:521C or 0; the target altitude follows the player (>= 2500) */
u16 map_target(void)
{
    if (!TARGET_SET) return 0;
    DSSL(TARGET + 8) = DSSL(PLAYER() + 0x0C);
    if (DSSL(TARGET + 8) < 0x9C4) DSSL(TARGET + 8) = 0x9C4;
    return TARGET;
}

/* AP 6cdc:0de1 map_set_target(p) — p == 0 clears */
void map_set_target(u16 p)
{
    if (p) {
        memcpy(DSP(TARGET), DSP(p), 12);
        TARGET_SET = 1;
    } else {
        TARGET_SET = 0;
    }
}

/* AP 6cdc:0e0b fltmap — the map screen (M key) */
void fltmap(void)
{
    u16 m = stk_mark();
    u16 icons = stk_alloc(0x80), tgt = stk_alloc(12);
    quit_block();
    wipe_black();
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    u32 pal = pal_load(res_name(0x11A0 /* "fltmap" */, 0x05CC /* "PAL" */));
    FarPtr palp = far_make((u16)(pal >> 16), (u16)pal);
    pal_set(palp);
    fltmap_background();
    DSW(0x26D6) = DSW(AP_DS_drv_page_back);
    DSW(AP_DS_drv_page_write) = DSW(0x26D0);
    copy_screen();
    ICONS = bmp_load(res_name(0x11A0, 0x05B4 /* "BMP" */));
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    map_icons_draw(icons, map_icons(icons));
    SEGW(0x399A, 0x004E) = 0x5B44;
    dlg_activate(far_make(SEG(0x399A), 0x0014), 6);
    dlg_force_redraw();
    DSB(0x3BB8) = 1;
    dlg_poll();
    u8 redraw = 0, route = 0, done = 0;
    while (!done) {
        if (redraw) {
            DSW(0x26D6) = DSW(0x26D0);
            DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
            copy_screen();
            map_icons_draw(icons, map_icons(icons));
            dlg_force_redraw();
            dlg_poll();
            redraw = 0;
        }
        if (route) {
            if (map_route()) done = 1;
            route = 0;
        }
        s16 r = dlg_poll();
        if (r == 9) done = 1;
        else if (r == 0xC8) { redraw = 1; route = 1; }
        if (done) break;
        u16 ev = DSW(0x3BBB);
        if (ev == 0x0E || ev == 0x53) {
            map_set_target(0);
            redraw = 1;
        } else if (ev == 0x20) {
            if (dogfight_mission()) continue;
            s16 sx = DSS(0x3BBF), sy = DSS(0x3BBD);
            if (sx > 0x1A && sx < 0xAD && sy > 0x10 && sy < 0xA3) {
                map_to_world(tgt, (u16)(tgt + 4), sx, sy);
                DSL(tgt + 8) = 0;
                map_set_target(tgt);
            } else if (!(sx > 0x1A && sx < 0xAD)) {
                map_set_target(0);
            }
            redraw = 1;
        }
        host_pump();                          /* PORT: the dialog loop pumps the host */
    }
    bmp_free_w(ICONS);
    pal_free(palp);
    wipe_black();
    pal_set(ds_far(AP_DS_sim_palette));
    DSW(AP_DS_redraw_pages) = 2;
    redraw_all();
    cockpit_force_rebuild();
    palfx_force();
    palfx_frame();
    pal_fx_update(1);
    quit_unblock();
    DSB(0x0B80) = 0;
    stk_release(m);
}

__attribute__((constructor)) static void fltmap_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x6CDC0000u, (CodeFn)fltmap_cb_a);
    codeptr_register(PROG_AP, 0x6CDC003Eu, (CodeFn)fltmap_cb_b);
}
