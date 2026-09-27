/* sim_flow: dlglib.c — the AP dialog library core: item iteration and the hit/callback dispatcher
 * (46de), the large panel frame (472f), the drawing helpers and buttons (47a8), embossed text (48a2),
 * far text helpers (0de8) — sim_flow_ui.md §6. A variant of the PS toolkit (shell_ui_toolkit.md): the
 * shared rect_grow/rect_save/rect_copy/text_engrave are shell_ui's.
 * Items (22 bytes, far table DS:00D6, count DS:00DC): +0 x, +2 y, +4 w, +6 h, +8 far callback, +0C event
 * mask, +0D flags (1 absolute origin, 8 diamond hit test, 80h value is a string id), +0E value, +10 user
 * word, +12 4 bytes from the .bl file. */
#include "sim_flow/sfb_priv.h"

/* AP 0de8:0000 far_text_draw(far s, x, y) */
void far_text_draw(FarPtr s, s16 x, s16 y)
{
    gfx_text(far_to_near_str(s), x, y);
}

/* AP 0de8:0020 far_text_width(far s) */
s16 far_text_width(FarPtr s)
{
    return gfx_text_width(far_to_near_str(s));
}

/* ---- 46de: iteration, hit test, dispatch ---------------------------------------------------------- */

/* AP 46de:0000 item_iter(): the current item's rectangle into DS:3BC9.. (items after the first are
 * relative to the origin DS:3B10/3B0E unless flag 1) */
FarPtr item_iter(s16 unused)
{
    (void)unused;
    FarPtr it = CUR_ITEM();
    RX = IT_S(it, 0);
    RY = IT_S(it, 2);
    if (CUR_IDX == 0 || (IT_B(it, 0x0D) & 1)) {
        DSW(0x3B10) = IT_W(it, 0);
        DSW(0x3B0E) = IT_W(it, 2);
    } else {
        RX = (s16)(RX + DSS(0x3B10));
        RY = (s16)(RY + DSS(0x3B0E));
    }
    RW = IT_S(it, 4);
    RH = IT_S(it, 6);
    return it;
}

/* AP 46de:0057 point_in_item(x, y) (diamond test with flag 8) */
s16 point_in_item(FarPtr it_unused, s16 x, s16 y)
{
    (void)it_unused;
    FarPtr it = CUR_ITEM();
    if (!(IT_B(it, 0x0D) & 8))
        return !(x < RX || RX + RW <= x || y < RY || RY + RH <= y);
    s16 r = (s16)(RW >> 1);
    s16 dx = (s16)(x - (RX + r)), dy = (s16)(y - (RY + r));
    if (dx < 0) dx = (s16)-dx;
    if (dy < 0) dy = (s16)-dy;
    return (s16)(dx + dy) < r;
}

/* AP 46de:00cd item_rect(far items, i) */
void item_rect(FarPtr items, s16 i)
{
    ds_far_wr(0x3BCF, far_make(items.seg, (u16)(items.off + i * 0x16)));
    item_iter(0);
}

/* AP 46de:00ee dlg_hit_dispatch(x, y, buttons, redraw) -> DS:3BC1 (the result set by a callback) */
s16 dlg_hit_dispatch(s16 x, s16 y, s16 buttons, s16 redraw)
{
    DSW(0x26D6) = DSW(0x26D2);
    DSW(0x26D8) = DSW(0x26D2);
    DSW(0x3B0C) = (u16)redraw;
    DSW(0x3BBF) = (u16)x;
    DSW(0x3BBD) = (u16)y;
    DSW(0x3BC1) = 0;
    DSL(0x3BD3) = 0;
    s16 pressed = 0, released = 0;
    CUR_IDX = 0;
    DSL(0x3BCF) = DSL(0x00D6);
    while (CUR_IDX < DSS(0x00DC)) {
        item_iter(0);
        STATE = redraw ? 0x40 : 0;
        u8 *down = DSP(0x3B90 + CUR_IDX);
        if (point_in_item(far_make(0, 0), x, y) == 0) {
            STATE |= 2;
            if (*down) { STATE |= 0x22; *down = 0; }
        } else {
            DSL(0x3BD3) = DSL(0x3BCF);
            if (buttons == 0) STATE |= (u16)(*down ? 0x11 : 1);
            else STATE |= (u16)(*down ? 9 : 5);
            *down = (u8)buttons;
        }
        DSW(0x3B18 + 2 * CUR_IDX) = STATE;
        if (STATE & 4) pressed = 1;
        else if ((STATE & 0x30) && pressed) released = 1;
        CUR_IDX++;
        DSW(0x3BCF) += 0x16;
    }
    s16 step;
    if (released) { CUR_IDX = (s16)(DSS(0x00DC) - 1); step = -1; }
    else { CUR_IDX = 0; step = 1; }
    for (s16 n = DSS(0x00DC); --n >= 0;) {
        FarPtr items = ds_far(0x00D6);
        ds_far_wr(0x3BCF, far_make(items.seg, (u16)(items.off + CUR_IDX * 0x16)));
        item_iter(0);
        STATE = DSW(0x3B18 + 2 * CUR_IDX);
        FarPtr it = CUR_ITEM();
        if ((STATE & IT_B(it, 0x0C)) && rd32(it.seg, (u16)(it.off + 8)) != 0) {
            DSB(0x3B68 + CUR_IDX) = 1;
            u16 v = IT_W(it, 0x0E);
            s16 swapped = 0;
            if ((IT_B(it, 0x0D) & 0x80) && v != 0xFFFF) {
                wr16(it.seg, (u16)(it.off + 0x0E), str0((s16)v).off);
                swapped = 1;
            }
            CodeFn f = codeptr_lookup(far_rd(it.seg, (u16)(it.off + 8)));
            f();
            if (swapped) wr16(it.seg, (u16)(it.off + 0x0E), v);
        } else {
            DSB(0x3B68 + CUR_IDX) = 0;
        }
        CUR_IDX = (s16)(CUR_IDX + step);
    }
    return DSS(0x3BC1);
}

/* AP 46de:02f3 dlg_redraw_dirty(): pointer on the work page, copy the redrawn items (or the whole
 * screen) to the front page, restore under the pointer */
void dlg_redraw_dirty(void)
{
    u8 wipe = DSB(0x3BB8);
    DSB(0x3BB8) = 0;
    ap_clip_full();
    DSW(0x26D6) = DSW(0x26D2);
    DSW(0x26D8) = DSW(0x26D2);
    s16 w = 0, h = 0;
    if (DSW(0x00DA) != 0 && DSW(0x00E2) != 0) {
        u16 ptr = DSW(0x00DA);
        w = (s16)(0x140 - DSS(0x3BBF));
        if (w >= DSS(ptr + 6)) w = DSS(ptr + 6);
        h = (s16)(0xC8 - DSS(0x3BBD));
        if (h >= DSS(ptr + 8)) h = DSS(ptr + 8);
        gfx_get_rect(ds_far(0x00DE), DSS(0x3BBF), DSS(0x3BBD), w, h);
        gfx_sprite(ptr, DSS(0x3BBF), DSS(0x3BBD));
    }
    DSW(0x26D6) = DSW(0x26D2);
    DSW(0x26D8) = DSW(0x26D4);
    if (DSW(0x3B0C) != 0 || DSB(0x3B68) != 0) {
        if (wipe) wipe_in(2);
        else gfx_copy_rect(0, 0, DSS(0x2DAA), DSS(0x2DAC));
    } else {
        CUR_IDX = 0;
        DSL(0x3BCF) = DSL(0x00D6);
        while (CUR_IDX < DSS(0x00DC)) {
            item_iter(0);
            if (DSB(0x3B68 + CUR_IDX)) gfx_copy_rect(RX, RY, RW, RH);
            CUR_IDX++;
            DSW(0x3BCF) += 0x16;
        }
    }
    DSW(0x26D6) = DSW(0x26D2);
    DSW(0x26D8) = DSW(0x26D4);
    if (DSW(0x00E4) != 0) gfx_copy_rect(DSS(0x3B16), DSS(0x3B14), DSS(0x00E4), DSS(0x3B12));
    if (DSW(0x00DA) != 0 && DSW(0x00E2) != 0) {
        DSW(0x00E4) = (u16)w;
        DSW(0x3B12) = (u16)h;
        DSW(0x3B14) = DSW(0x3BBD);
        DSW(0x3B16) = DSW(0x3BBF);
        gfx_copy_rect(DSS(0x3B16), DSS(0x3B14), w, h);
        DSW(0x26D8) = DSW(0x26D2);
        gfx_put_rect(ds_far(0x00DE), DSS(0x3B16), DSS(0x3B14), DSS(0x00E4), DSS(0x3B12));
        return;
    }
    DSW(0x00E4) = 0;
}

/* AP 46de:04a4 mouse_hide_over() -> pointer saved (near) */
s16 mouse_hide_over(void)
{
    return !(DSL(0x00DE) == 0 || DSW(0x00E4) == 0);
}

/* AP 46de:04bc mouse_show_over(): draw the pointer again at its saved place */
void mouse_show_over(void)
{
    if (mouse_hide_over()) gfx_sprite_flip(DSW(0x00DA), DSS(0x3B16), DSS(0x3B14), 0);
}

/* AP 46de:04de mouse_over_panel(): put back the background under the pointer */
s16 mouse_over_panel(void)
{
    if (mouse_hide_over())
        gfx_put_rect(ds_far(0x00DE), DSS(0x3B16), DSS(0x3B14), DSS(0x00E4), DSS(0x3B12));
    return 0;
}

/* ---- 472f: the large dialog frame ----------------------------------------------------------------- */

/* AP 472f:0000 frame_corners(xs, x, y, n, step, colour): n rows of a rounded corner */
void frame_corners(s16 xs, s16 x, s16 y, s16 n, s16 step, u16 col)
{
    s16 xr = (s16)(x + n), xl = (s16)(x - n);
    for (s16 i = 0; i < n; i++) {
        if (xs < x) {
            u8 c = (u8)gfx_get_pixel((s16)(xr + 2), y);
            gfx_put_pixel(x, y, (s16)col);
            DSB(0x26CE) = c;
            gfx_line((s16)(x + 1), y, (s16)(xr + 1), y);
        } else if (x < xs) {
            u8 c = (u8)gfx_get_pixel((s16)(xl - 2), y);
            gfx_put_pixel(x, y, (s16)col);
            DSB(0x26CE) = c;
            gfx_line((s16)(x - 1), y, (s16)(xl - 1), y);
        }
        xr = (s16)(xr + step);
        xl = (s16)(xl + step);
        x = (s16)(x + step);
        y++;
    }
}

/* AP 472f:00cb frame_edges(hh) */
void frame_edges(s16 hh)
{
    frame_corners(RX, (s16)(RX + 6), (s16)(RY + hh - 6), 7, -1, 0xC2);
    frame_corners((s16)(RX + RW), (s16)(RX + RW - 7), (s16)(RY + hh - 7), 7, 1, 0xC2);
    frame_corners(RX, RX, (s16)(RY + RH - 6), 6, 1, 0xC0);
    frame_corners((s16)(RX + RW), (s16)(RX + RW), (s16)(RY + RH - 6), 6, -1, 0xC0);
}

/* AP 472f:0165 frame_part_a(d, top) */
void frame_part_a(s16 d, s16 top)
{
    DSB(0x26C3) = 1;
    DSW(0x26C4) = (u16)RX;
    DSW(0x26C6) = (u16)(RX + RW);
    DSW(0x26C8) = (u16)(RY + 2);
    DSW(0x26CA) = (u16)(RY + RH);
    frame_lines(RX, RY, DSS(0x26C6), (s16)(RY + top), (s16)(RX + d), (s16)(DSS(0x26CA) - d),
                (s16)(DSS(0x26C6) - d), DSS(0x26CA));
    DSB(0x26C3) = 0;
}

/* AP 472f:01c4 frame_part_b(): the foot of the frame */
void frame_part_b(void)
{
    s16 x = RX, xr = (s16)(RX + RW), yb = (s16)(RY + RH);
    s16 y1 = (s16)(yb + 1), y5 = (s16)(yb + 5);
    DSB(0x26CE) = 0xC1;
    gfx_line((s16)(x + 0x0F), y1, (s16)(x + 0x14), y5);
    gfx_line((s16)(xr - 0x0F), y1, (s16)(xr - 0x14), y5);
    DSB(0x26CE) = 0xC0;
    gfx_line((s16)(x + 0x10), y1, (s16)(x + 0x15), y5);
    gfx_line((s16)(xr - 0x10), y1, (s16)(xr - 0x15), y5);
    DSB(0x26CE) = 0xC4;
    gfx_line((s16)(x + 0x11), y1, (s16)(xr - 0x11), y1);
    DSB(0x26CE) = 0xC6;
    gfx_line((s16)(x + 0x12), (s16)(yb + 4), (s16)(xr - 0x12), (s16)(yb + 4));
    DSB(0x26CE) = 0xC1;
    gfx_line((s16)(x + 0x13), y5, (s16)(xr - 0x13), y5);
    DSB(0x26CE) = 0xBE;
    gfx_line((s16)(x + 0x12), (s16)(yb + 2), (s16)(xr - 0x12), (s16)(yb + 2));
    gfx_line((s16)(x + 0x13), (s16)(yb + 3), (s16)(xr - 0x13), (s16)(yb + 3));
}

/* AP 472f:0302 frame_panel(): title plate, tiles, rivets, the embossed title */
void frame_panel(void)
{
    u16 saved = DSW(0x26D6);
    FarPtr it = CUR_ITEM();
    u16 title = IT_W(it, 0x0E);
    if (title != 0) {
        DSW(0x26D6) = DSW(0x26D8);
        gfx_font_select(DSS(0x3F30));
        s16 tx = ap_text_center_x(title, RX, RW);
        s16 tw = gfx_text_width(title);
        s16 di = (s16)(tx - RX - 2);
        s16 rgap = (s16)(RX + RW - (tx + tw) - 1);
        u16 hdr = DSW(DSW(0x3BD7) + 0x0C);
        s16 hh = DSS(hdr + 8);
        FarPtr buf = ds_far(0x3BE2);
        ap_tile_fill(RX, (s16)(RY + hh), RW, (s16)(RH - hh - 5));
        ap_tile_fill((s16)(RX + 6), (s16)(RY + hh - 7), (s16)(RW - 12), 7);
        frame_part_a(6, hh);
        DSB(0x26CE) = 0xC2;
        gfx_line((s16)(RX + di - 4), RY, (s16)(RX + RW - rgap), RY);
        gfx_line((s16)(RX + 7), (s16)(RY + hh - 7), (s16)(RX + di - 12), (s16)(RY + hh - 7));
        DSB(0x26CE) = 0xB5;
        gfx_line((s16)(RX + di - 4), (s16)(RY + 1), (s16)(RX + RW - rgap), (s16)(RY + 1));
        gfx_line((s16)(RX + 6), (s16)(RY + hh - 6), (s16)(RX + di - 13), (s16)(RY + hh - 6));
        DSB(0x26CE) = 0xC2;
        gfx_line((s16)(RX + RW - rgap + 10), (s16)(RY + hh - 7), (s16)(RX + RW - 7), (s16)(RY + hh - 7));
        DSB(0x26CE) = 0xB5;
        gfx_line((s16)(RX + RW - rgap + 10), (s16)(RY + hh - 6), (s16)(RX + RW - 6), (s16)(RY + hh - 6));
        di = (s16)(RX + di - 5);
        s16 xr = (s16)(RX + RW - rgap);
        gfx_get_rect(buf, (s16)(di - 10), (s16)(RY + hh), 10, 10);
        gfx_put_rect(buf, (s16)(di - 10), (s16)(RY + 4), 10, 10);
        s16 lx = (s16)(di - 11), ly = (s16)(RY + 5);
        DSB(0x26CE) = (u8)gfx_get_pixel((s16)(lx + 1), ly);
        for (s16 n = 8; n > 0; n--) { gfx_line(lx, ly, lx, (s16)(ly + n)); lx--; ly++; }
        for (; di < xr; di = (s16)(di + 10)) {
            gfx_get_rect(buf, di, (s16)(RY + hh), 10, 10);
            gfx_put_rect(buf, di, (s16)(RY + 2), 10, 10);
            gfx_put_rect(buf, di, (s16)(RY + hh - 8), 10, 10);
        }
        gfx_put_rect(buf, (s16)(xr - 4), (s16)(RY + 2), 10, 10);
        gfx_put_rect(buf, (s16)(xr - 4), (s16)(RY + hh - 8), 10, 10);
        gfx_get_rect(buf, (s16)(xr + 6), (s16)(RY + hh), 10, 10);
        gfx_put_rect(buf, (s16)(xr + 6), (s16)(RY + 4), 10, 10);
        lx = (s16)(xr + 0x10); ly = (s16)(RY + 5);
        DSB(0x26CE) = (u8)gfx_get_pixel((s16)(lx - 1), ly);
        for (s16 n = 8; n > 0; n--) { gfx_line(lx, ly, lx, (s16)(ly + n)); lx++; ly++; }
        s16 xe = (s16)(RX + RW - 9);
        for (s16 x = (s16)(RX + 5); x < xe; x = (s16)(x + 10)) {
            gfx_get_rect(buf, x, (s16)(RY + RH - 0x14), 10, 10);
            gfx_put_rect(buf, x, (s16)(RY + RH - 10), 10, 10);
        }
        ap_text_emboss(title, 0xFFFF, 0xFFFF, 0xC1, 0xC0, 0xB1);
        gfx_sprite_flip(hdr, (s16)(tx - DSS(hdr + 6) - 2), RY, 2);
        gfx_sprite_flip(hdr, (s16)(tx + tw + 1), RY, 0);
        frame_edges(hh);
    }
    DSW(0x26D6) = saved;
}

/* AP 472f:0782 dlg_frame(it) — callback of the preferences / realism frames */
void dlg_frame(FarPtr it)
{
    (void)it;
    frame_panel();
    frame_part_b();
}

/* ---- 47a8: drawing helpers and buttons ------------------------------------------------------------ */

/* AP 47a8:0000 savebuf_alloc(): the 10x10 save buffer DS:3BE2 */
void savebuf_alloc(void)
{
    u16 n = (u16)gfx_rect_size(10, 10);
    ds_far_wr(0x3BE2, mem_far_new((u32)n, 0));
}

/* AP 47a8:0031 savebuf_free() */
void savebuf_free(void)
{
    mem_far_delete(ds_far(0x3BE2));
}

/* AP 47a8:005d rect_shrink(d) */
void ap_rect_shrink(u16 d_, s16 unused)
{
    (void)unused;
    s16 d = (s16)d_;
    RX = (s16)(RX + d);
    RY = (s16)(RY + d);
    RW = (s16)(RW - d * 2);
    RH = (s16)(RH - d * 2);
}

/* AP 47a8:0094 rect_restore() */
void ap_rect_restore(u16 unused)
{
    (void)unused;
    RX = DSS(0x3BE0);
    RY = DSS(0x3BDE);
    RW = DSS(0x3BDC);
    RH = DSS(0x3BDA);
}

/* AP 47a8:00ce bevel_box() */
void bevel_box(s16 a0, s16 a1, s16 a2, u16 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xB8;
    DSB(0x26CE) = 0xB8;
    gfx_rect(RX, RY, RW, RH);
    rect_save();
    RW = (s16)(RX + RW);
    RH = (s16)(RY + RH);
    DSB(0x26CC) = 0;
    DSB(0x26CE) = 0xC2;
    gfx_line(RX, RY, RW, RY);
    DSB(0x26CE) = 0xB4;
    gfx_line((s16)(RX + 1), (s16)(RY + 1), (s16)(RW - 1), (s16)(RY + 1));
    DSB(0x26CE) = 0xC0;
    gfx_line(RX, (s16)(RY + 1), RX, (s16)(RH - 1));
    gfx_line(RW, (s16)(RY + 1), RW, (s16)(RH - 1));
    DSB(0x26CE) = 0xBF;
    gfx_line((s16)(RX + 1), (s16)(RY + 2), (s16)(RX + 1), RH);
    gfx_line((s16)(RW - 1), (s16)(RY + 2), (s16)(RW - 1), RH);
    DSB(0x26CE) = 0xC4;
    gfx_line((s16)(RX + 1), RH, (s16)(RW - 1), RH);
    DSB(0x26CE) = 0xC1;
    gfx_line(RX, (s16)(RH - 1), RW, (s16)(RH - 1));
    DSB(0x26CE) = 0xC0;
    gfx_line(RX, (s16)(RH - 2), RW, (s16)(RH - 2));
    ap_rect_restore(0);
}

/* AP 47a8:0232 item_frame_bitmap(x, y, n): n rivets every 7 pixels down */
void item_frame_bitmap(s16 x, s16 y, s16 n)
{
    u16 b = DSW(DSW(0x3BD7) + 0x10);
    for (s16 i = 0; i < n; i++) {
        gfx_sprite_flip(b, x, y, 0);
        y = (s16)(y + 7);
    }
}

/* AP 47a8:026c groove_box(flat) */
void groove_box(s16 flat)
{
    rect_save();
    RW = (s16)(RX + RW);
    RH = (s16)(RY + RH);
    DSB(0x26CE) = 0xC2;
    gfx_line(RX, RY, (s16)(RW - 1), RY);
    DSB(0x26CE) = 0xC0;
    gfx_line(RX, (s16)(RY + 1), RX, (s16)(RH - 2));
    gfx_line((s16)(RW - 1), (s16)(RY + 1), (s16)(RW - 1), (s16)(RH - 2));
    DSB(0x26CE) = 0xC7;
    gfx_line(RX, (s16)(RH - 1), (s16)(RW - 1), (s16)(RH - 1));
    if ((u8)flat == 0) {
        DSB(0x26CE) = 0xC0;
        gfx_line(RX, RH, (s16)(RW - 1), RH);
    }
    ap_rect_restore(0);
}

/* AP 47a8:0335 inset_box() */
void inset_box(void)
{
    rect_save();
    RW = (s16)(RX + RW);
    RH = (s16)(RY + RH);
    DSB(0x26CE) = 0xC6;
    gfx_line(RX, RY, RW, RY);
    DSB(0x26CE) = 0xC0;
    gfx_line(RX, RY, RX, RH);
    gfx_line(RW, RY, RW, RH);
    DSB(0x26CE) = 0xB4;
    gfx_line(RX, RH, RW, RH);
    DSB(0x26CE) = 0xC7;
    gfx_line((s16)(RX + 1), (s16)(RY + 1), (s16)(RW - 1), (s16)(RY + 1));
    gfx_line((s16)(RX + 1), (s16)(RY + 1), (s16)(RX + 1), (s16)(RH - 1));
    gfx_line((s16)(RW - 1), (s16)(RY + 1), (s16)(RW - 1), (s16)(RH - 1));
    DSB(0x26CE) = 0xC2;
    gfx_line((s16)(RX + 1), (s16)(RH - 1), (s16)(RW - 1), (s16)(RH - 1));
    ap_rect_restore(0);
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xC0;
    DSB(0x26CE) = 0xC0;
    gfx_rect((s16)(RX + 2), (s16)(RY + 2), (s16)(RW - 3), (s16)(RH - 3));
}

/* AP 47a8:0477 inset_box_item(it) — callback */
void inset_box_item(FarPtr it)
{
    (void)it;
    inset_box();
}

/* AP 47a8:0480 text_center_x(s, x, w) */
s16 ap_text_center_x(u16 s, s16 x, s16 w)
{
    s16 d = (s16)(w - gfx_text_width(s));
    if (DSB(0x26C2) & 2) d = (s16)(d - (s16)rtl_strlen(s));
    if (d > 0) x = (s16)(x + d / 2 + 1);
    return x;
}

/* AP 47a8:04c7 tile_fill(x, y, w, h): the panel texture DS:3BD7->+0E, rows every 3, shifted by 4 */
void ap_tile_fill(s16 x, s16 y, s16 w, s16 h)
{
    DSB(0x26C3) = 1;
    DSW(0x26C4) = (u16)x;
    DSW(0x26C6) = (u16)(x + w - 1);
    DSW(0x26C8) = (u16)y;
    DSW(0x26CA) = (u16)(y + h - 1);
    u16 t = DSW(DSW(0x3BD7) + 0x0E);
    s16 step = (s16)(DSS(t + 6) - 2);
    for (; y <= DSS(0x26CA); y = (s16)(y + 3)) {
        if (x >= DSS(0x26C4)) x = (s16)(x - step);
        for (s16 si = x; si <= DSS(0x26C6); si = (s16)(si + step)) gfx_sprite_flip(t, si, y, 0);
        x = (s16)(x + 4);
    }
    DSB(0x26C3) = 0;
}

/* AP 47a8:0549 fill_item(it) — callback: texture over the item */
void fill_item(FarPtr it, s16 c)
{
    (void)it; (void)c;
    ap_tile_fill(RX, RY, RW, RH);
}

/* AP 47a8:0565 fill_rect(): clear the work page to colour 0 */
void fill_rect(void)
{
    DSB(0x26CC) = 1;
    DSB(0x26C3) = 0;
    DSB(0x26CD) = 0;
    DSB(0x26CE) = 0;
    DSW(0x26D8) = DSW(0x26D2);
    gfx_rect(0, 0, 0x140, 0xC8);
}

/* AP 47a8:0593 fill_c1(): the item rectangle in C1h */
void fill_c1(void)
{
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xC1;
    DSB(0x26CE) = 0xC1;
    gfx_rect(RX, RY, RW, RH);
}

/* AP 47a8:05bd fill_rect2(): clear the work page to B8h */
void fill_rect2(void)
{
    DSB(0x26CC) = 1;
    DSB(0x26C3) = 0;
    DSB(0x26CD) = 0xB8;
    DSB(0x26CE) = 0xB8;
    DSW(0x26D8) = DSW(0x26D2);
    gfx_rect(0, 0, 0x140, 0xC8);
}

/* AP 47a8:05eb frame_lines(x0, y0, x1, ytop, bx0, yside, bx1, ybot) */
void frame_lines(s16 x0, s16 y0, s16 x1, s16 ytop, s16 bx0, s16 yside, s16 bx1, s16 ybot)
{
    DSB(0x26CE) = 0xC2;
    gfx_line(x0, y0, x1, y0);
    DSB(0x26CE) = 0xB5;
    gfx_line(x0, (s16)(y0 + 1), x1, (s16)(y0 + 1));
    DSB(0x26CE) = 0xC0;
    gfx_line(x0, (s16)(ytop + 1), x0, yside);
    gfx_line(x1, (s16)(ytop + 1), x1, yside);
    DSB(0x26CE) = 0xBF;
    gfx_line((s16)(x0 + 1), (s16)(ytop + 2), (s16)(x0 + 1), yside);
    gfx_line((s16)(x1 - 1), (s16)(ytop + 2), (s16)(x1 - 1), yside);
    DSB(0x26CE) = 0xC1;
    gfx_line(bx0, ybot, bx1, ybot);
    DSB(0x26CE) = 0xC4;
    gfx_line(bx0, (s16)(ybot - 1), bx1, (s16)(ybot - 1));
    DSB(0x26C3) = 0;
}

/* AP 47a8:06ca frame_lines_item(it) — callback */
void frame_lines_item(FarPtr it)
{
    (void)it;
    DSB(0x26C3) = 1;
    DSW(0x26C4) = (u16)RX;
    DSW(0x26C6) = (u16)(RX + RW);
    DSW(0x26C8) = (u16)RY;
    DSW(0x26CA) = (u16)(RY + RH);
    frame_lines(RX, RY, DSS(0x26C6), RY, RX, DSS(0x26CA), DSS(0x26C6), DSS(0x26CA));
    DSB(0x26C3) = 0;
}

/* AP 47a8:07ca button_draw(down) */
void button_draw(s16 down)
{
    u8 d = (u8)down;
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xC3;
    DSB(0x26CE) = 0xC3;
    gfx_rect((s16)(RX + 1), (s16)(RY + 1), (s16)(RW - 2), (s16)(RH - 2));
    rect_save();
    s16 xr = (s16)(RX + RW);
    RW = (s16)(xr - 2);
    RH = (s16)(RY + RH);
    DSB(0x26CE) = d ? 0xC6 : 0xC9;
    gfx_line((s16)(RX + 3), (s16)(RY + 2), (s16)(xr - 4), (s16)(RY + 2));
    DSB(0x26CE) = 0xC8;
    gfx_line((s16)(RX + 3), (s16)(RY + 3), (s16)(RX + 3), (s16)(RH - 3));
    gfx_line((s16)(RW - 2), (s16)(RY + 3), (s16)(RW - 2), (s16)(RH - 3));
    DSB(0x26CE) = d ? 0xC9 : 0xC6;
    gfx_line((s16)(RX + 3), (s16)(RH - 3), (s16)(RW - 2), (s16)(RH - 3));
    ap_rect_restore(0);
    DSB(0x26CC) = 1;
    DSB(0x26CD) = d ? 0xC7 : 0xC8;
    DSB(0x26CE) = DSB(0x26CD);
    gfx_rect((s16)(RX + 4), (s16)(RY + 3), (s16)(RW - 8), (s16)(RH - 6));
}

static u16 cur_value(void) { FarPtr it = CUR_ITEM(); return IT_W(it, 0x0E); }

/* AP 47a8:0712 button_up_a(it) */
void button_up_a(FarPtr it)
{
    (void)it;
    groove_box(0);
    button_draw(0);
    gfx_font_select(DSS(0x3F2C));
    s16 tx = ap_text_center_x(cur_value(), RX, RW);
    text_engrave(cur_value(), 0xC4, 0xC9, 0xC7, (u16)tx, (u16)(RY + 5));
}

/* AP 47a8:076e button_down_a(it) */
void button_down_a(FarPtr it)
{
    (void)it;
    groove_box(0);
    button_draw(1);
    gfx_font_select(DSS(0x3F2C));
    s16 tx = ap_text_center_x(cur_value(), RX, RW);
    text_engrave(cur_value(), 0xC4, 0xC9, 0xC6, (u16)tx, (u16)(RY + 4));
}

/* AP 47a8:0912 button_up_b(it) */
void button_up_b(void)
{
    gfx_font_select(DSS(0x3F2C));
    u16 s = cur_value();
    s16 tx = ap_text_center_x(s, RX, RW);
    groove_box(0);
    button_draw(1);
    text_engrave(s, 0xC4, 0xC9, 0xC6, (u16)tx, (u16)(RY + 5));
}

/* AP 47a8:0971 button_down_b(it) */
void button_down_b(void)
{
    gfx_font_select(DSS(0x3F2C));
    u16 s = cur_value();
    s16 tx = ap_text_center_x(s, RX, RW);
    groove_box(0);
    button_draw(0);
    text_engrave(s, 0xC4, 0xC9, 0xC7, (u16)tx, (u16)(RY + 6));
}

/* AP 47a8:09d0 framed_panel(it) — callback */
void framed_panel(FarPtr it)
{
    (void)it;
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xB8;
    DSB(0x26CE) = 0xB8;
    gfx_rect(RX, RY, RW, RH);
    DSB(0x26CC) = 0;
    DSB(0x26CE) = 0xC4;
    gfx_rect(RX, RY, RW, RH);
    DSB(0x26CE) = 0xC2;
    gfx_line((s16)(RX + 1), RY, (s16)(RX + RW - 2), RY);
    DSB(0x26CE) = 0xC0;
    gfx_line(RX, (s16)(RY + 1), RX, (s16)(RY + RH - 2));
    s16 xr = (s16)(RX + RW - 1);
    gfx_line(xr, (s16)(RY + 1), xr, (s16)(RY + RH - 2));
    DSB(0x26CE) = 0xC7;
    s16 yb = (s16)(RY + RH - 1);
    gfx_line((s16)(RX + 1), yb, (s16)(RX + RW - 2), yb);
    DSB(0x26CE) = 0xC3;
    gfx_rect((s16)(RX + 1), (s16)(RY + 1), (s16)(RW - 2), (s16)(RH - 2));
}

/* AP 47a8:0ae1 window_frame_title(it) — callback: bevel box, four rivets, title */
void window_frame_title(FarPtr it, FarPtr title)
{
    (void)it; (void)title;
    bevel_box(0, 0, 0, 0);
    u16 b = DSW(DSW(0x3BD7) + 0x10);
    gfx_sprite_flip(b, (s16)(RX + 2), (s16)(RY + 2), 0);
    gfx_sprite_flip(b, (s16)(RX + 2), (s16)(RY + RH - 6), 0);
    gfx_sprite_flip(b, (s16)(RX + RW - 5), (s16)(RY + 2), 0);
    gfx_sprite_flip(b, (s16)(RX + RW - 5), (s16)(RY + RH - 6), 0);
    if (DSL(0x3BCF) != 0) {
        gfx_font_select(DSS(0x3F2C));
        text_title(cur_value(), 0xC1, 0xB1, 0xC0);
    }
}

/* AP 47a8:0ba3 item_draw_dispatch(it) — callback: textured frame with rivet columns */
void item_draw_dispatch(FarPtr it)
{
    (void)it;
    fill_item(far_make(0, 0), 0);
    frame_lines_item(far_make(0, 0));
    for (s16 y = (s16)(RY + 5); y <= RY + RH - 6; y = (s16)(y + 7)) {
        item_frame_bitmap((s16)(RX + 4), y, 1);
        item_frame_bitmap((s16)(RX + RW - 6), y, 1);
    }
}

/* AP 47a8:0bf2 button_up_c(it) */
void button_up_c(FarPtr it)
{
    (void)it;
    gfx_font_select(DSS(0x3F2E));
    u16 s = cur_value();
    s16 tx = ap_text_center_x(s, RX, RW);
    groove_box(1);
    button_draw(1);
    text_engrave(s, 0xC4, 0xC9, 0xC6, (u16)tx, (u16)(RY + 4));
}

/* AP 47a8:0c51 button_down_c(it) */
void button_down_c(FarPtr it)
{
    (void)it;
    gfx_font_select(DSS(0x3F2E));
    u16 s = cur_value();
    s16 tx = ap_text_center_x(s, RX, RW);
    groove_box(1);
    button_draw(0);
    text_engrave(s, 0xC4, 0xC9, 0xC7, (u16)tx, (u16)(RY + 5));
}

/* AP 47a8:0cb0 button_press(framed) -> 1 when released inside (click sound) */
s16 button_press(s16 framed)
{
    if (framed) {
        rect_grow(2);
        rect_save();
        RX = (s16)(RX - 1);
        RW = (s16)(RX + RW + 1);
        RH = (s16)(RY + RH);
        DSB(0x26CE) = 0xC2;
        gfx_line(RX, RY, RW, RY);
        DSB(0x26CE) = 0xBF;
        gfx_line(RX, (s16)(RY + 1), RX, RH);
        gfx_line(RW, (s16)(RY + 1), RW, RH);
        DSB(0x26CE) = 0xC0;
        gfx_line(RX, RH, RW, RH);
        ap_rect_restore(0);
        ap_rect_shrink(2, 0);
    }
    if ((STATE & 0x0C) == 0) button_up_c(far_make(0, 0));
    else button_down_c(far_make(0, 0));
    if ((STATE & 0x10) == 0) return 0;
    sx_play(0x0F, 0, 0, 0);
    return 1;
}

/* AP 47a8:0d92 toggle_press(it) -> 1 when released inside */
s16 toggle_press(FarPtr it)
{
    (void)it;
    if ((STATE & 0x0C) == 0) button_up_b();
    else button_down_b();
    if (STATE & 0x10) {
        sx_play(0x0F, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* AP 47a8:0dc7 button_bitmap(it) -> 1 when released inside */
s16 button_bitmap(FarPtr it)
{
    (void)it;
    u8 up = (STATE & 0x0C) == 0;
    gfx_font_select(DSS(0x3F2E));
    u16 s = cur_value();
    s16 tx = ap_text_center_x(s, RX, RW);
    groove_box(1);
    button_draw(up);
    text_engrave(s, 0xC6, 0xC9, 0xC8, (u16)tx, (u16)(RY + 5 - up));
    if (STATE & 0x10) {
        sx_play(0x0F, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* AP 48a2:0000 text_emboss(s, x, y, c1, c2, c3): x = -1 centred, y = -1 at the item top + 5 */
void ap_text_emboss(u16 s, u16 x_, u16 y_, u16 c1, u16 c2, u16 c3)
{
    if (s == 0) return;
    s16 x = (s16)x_, y = (s16)y_;
    if (x == -1) x = ap_text_center_x(s, RX, RW);
    DSB(0x26C2) = 1;
    if (y == -1) y = (s16)(RY + 5);
    DSB(0x26C0) = (u8)c1;
    gfx_text(s, x, y);
    DSB(0x26C0) = (u8)c3;
    gfx_text(s, x, (s16)(y + 2));
    DSB(0x26C0) = (u8)c2;
    gfx_text(s, x, (s16)(y + 1));
}

__attribute__((constructor)) static void dlglib_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x47A800CEu, (CodeFn)bevel_box);
    codeptr_register(PROG_AP, 0x47A80477u, (CodeFn)inset_box_item);
    codeptr_register(PROG_AP, 0x47A80549u, (CodeFn)fill_item);
    codeptr_register(PROG_AP, 0x47A806CAu, (CodeFn)frame_lines_item);
    codeptr_register(PROG_AP, 0x47A80712u, (CodeFn)button_up_a);
    codeptr_register(PROG_AP, 0x47A8076Eu, (CodeFn)button_down_a);
    codeptr_register(PROG_AP, 0x47A809D0u, (CodeFn)framed_panel);
    codeptr_register(PROG_AP, 0x47A80AE1u, (CodeFn)window_frame_title);
    codeptr_register(PROG_AP, 0x47A80BA3u, (CodeFn)item_draw_dispatch);
    codeptr_register(PROG_AP, 0x47A80BF2u, (CodeFn)button_up_c);
    codeptr_register(PROG_AP, 0x47A80C51u, (CodeFn)button_down_c);
    codeptr_register(PROG_AP, 0x47A80D92u, (CodeFn)toggle_press);
    codeptr_register(PROG_AP, 0x47A80DC7u, (CodeFn)button_bitmap);
    codeptr_register(PROG_AP, 0x472F0782u, (CodeFn)dlg_frame);
}
