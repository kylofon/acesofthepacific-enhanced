/* shell_ui: toolkit drawing primitives (PS segment 2c11; AP 47a8/53ec share rect_grow, rect_save,
 * rect_copy, text_engrave) — shell_ui_toolkit.md §2. Everything works on the current rectangle
 * (DS:5FDA x, 5FD8 y, 5FD6 w, 5FD4 h) of the PS toolkit.
 * The AP copies (47a8:0043/0077/00b1, 53ec:01e6 with its glyph edge 53ec:013f/0000) are the same code
 * on AP's own rectangle and edge-colour globals: those offsets come from the per-program table below
 * (they have no symbols), the driver/font globals from the layout. */
#include "shell_ui/sui_a_priv.h"

typedef struct { u16 rx, ry, rw, rh, sx, sy, sw, sh, c1, c3; } UiGlobals;
static const UiGlobals ui_ps = { 0x5FDA, 0x5FD8, 0x5FD6, 0x5FD4, 0x5FFE, 0x5FFC, 0x5FFA, 0x5FF8, 0x201E, 0x201C };
static const UiGlobals ui_ap = { 0x3BC9, 0x3BC7, 0x3BC5, 0x3BC3, 0x3BE0, 0x3BDE, 0x3BDC, 0x3BDA, 0x0658, 0x0656 };
static const UiGlobals *ug(void)
{
    int id = prog_current_id();
    if (id == PROG_PS) return &ui_ps;
    if (id == PROG_AP) return &ui_ap;
    host_fatal("shell_ui draw: toolkit function called from INTRO");
}

/* PS 2c11:0001 plate_buf_alloc */
void plate_buf_alloc(void)
{
    u32 n = gfx_rect_size(10, 10);
    ds_far_wr(T_PLATE_BUF, xfarmalloc((u16)n));
}

/* PS 2c11:002e plate_buf_free */
void plate_buf_free(void) { xfarfree(ds_far(T_PLATE_BUF)); }

/* PS 2c11:0042 / AP 47a8:0043 rect_grow(n) */
void rect_grow(u16 n_)
{
    const UiGlobals *g = ug();
    s16 n = (s16)n_;
    DSS(g->rx) = (s16)(DSS(g->rx) - n);
    DSS(g->ry) = (s16)(DSS(g->ry) - n);
    DSS(g->rw) = (s16)(DSS(g->rw) + n * 2);
    DSS(g->rh) = (s16)(DSS(g->rh) + n * 2);
}

/* PS 2c11:005c rect_shrink(n) */
void ps_rect_shrink(u16 n) { rect_grow((u16)-(s16)n); }

/* PS 2c11:006c / AP 47a8:0077 rect_save */
void rect_save(void)
{
    const UiGlobals *g = ug();
    DSW(g->sx) = DSW(g->rx);
    DSW(g->sy) = DSW(g->ry);
    DSW(g->sw) = DSW(g->rw);
    DSW(g->sh) = DSW(g->rh);
}

/* PS 2c11:0089 rect_restore */
void ps_rect_restore(void)
{
    DSW(RX) = DSW(T_SAVE_X);
    DSW(RY) = DSW(T_SAVE_Y);
    DSW(RW) = DSW(T_SAVE_W);
    DSW(RH) = DSW(T_SAVE_H);
}

/* PS 2c11:00a6 / AP 47a8:00b1 rect_copy: page 2316 -> 2318 */
void rect_copy(void)
{
    const UiGlobals *g = ug();
    gfx_copy_rect(DSS(g->rx), DSS(g->ry), DSS(g->rw), DSS(g->rh));
}

/* PS 2c11:00c3 text_emboss(s, tx, ty, c1, c2, c3) */
void ps_text_emboss(u16 s, u16 tx_, u16 ty_, u16 c1, u16 c2, u16 c3)
{
    s16 tx = (s16)tx_, ty = (s16)ty_;
    if (!s) return;
    if (tx == -1) tx = (s16)ps_text_center_x(s, (u16)RXs, (u16)RWs);
    DSB(TXT_FLAGS) = 1;
    if (ty == -1) ty = (s16)(RYs + 5);
    DSB(TXT_FG) = (u8)c1;
    gfx_text(s, tx, ty);
    DSB(TXT_FG) = (u8)c3;
    gfx_text(s, tx, (s16)(ty + 2));
    DSB(TXT_FG) = (u8)c2;
    gfx_text(s, tx, (s16)(ty + 1));
}

/* PS 2c11:014d glyph_edge(ch, px, py) -> glyph width: every ink pixel not already DS:201E becomes
 * DS:201C (the engraved outline); with text flag bit 1 the pixel right of an ink pixel is ink too */
static u16 glyph_edge(u8 ch, s16 px, s16 py)
{
    const UiGlobals *ui = ug();
    u16 idx = (u16)(ch - LB(G_font_first));
    u16 w, rows = LB(G_font_h);
    FarPtr g;
    FarPtr ofs = ds_far(lds(G_font_ofs));
    if (far_is_null(ofs)) {
        w = LB(G_font_w);
        g = far_add(ds_far(lds(G_font_glyphs)), (u16)((u16)((w + 7) >> 3) * idx * rows));
    } else {
        FarPtr wid = ds_far(lds(G_font_wid));
        w = rd8(wid.seg, (u16)(wid.off + idx));
        g = far_add(ds_far(lds(G_font_glyphs)), rd16(ofs.seg, (u16)(ofs.off + 2 * idx)));
    }
    for (u16 r = 0; r < rows; r++, py++) {
        u8 mask = 0x80, prev = 0;
        for (u16 c = 0; c < w; c++) {
            if (mask == 0) { mask = 0x80; g = far_add(g, 1); }
            u8 bit = (u8)(far_mp(g)[0] & mask);
            if (bit == 0 && (prev == 0 || !(LB(G_drv_text_flags) & 2))) {
                if (!(LB(G_drv_text_flags) & 1)) gfx_put_pixel((s16)(px + c), py, (s16)(s8)LB(G_drv_text_bg));
            } else {
                s16 col = gfx_get_pixel((s16)(px + c), py);
                if (col != DSS(ui->c1)) col = DSS(ui->c3);
                gfx_put_pixel((s16)(px + c), py, col);
            }
            mask >>= 1;
            prev = bit;
        }
        g = far_add(g, 1);
    }
    return w;
}

/* PS 2c11:027c text_edge(s, x, y) */
void text_edge(u16 s, s16 x, s16 y)
{
    while (DSB(s)) {
        u8 c = DSB(s);
        s++;
        x = (s16)(x + glyph_edge(c, x, y));
        if (LB(G_drv_text_flags) & 2) x++;
    }
}

/* PS 2c11:02b1 bevel_rows(ref_x, x, y, n, dx, c) */
static void bevel_rows(s16 ref_x, s16 x, s16 y, s16 n, s16 dx, u8 c)
{
    for (s16 k = 0; k < n; k++) {
        if (ref_x < x) {
            u8 s = (u8)gfx_get_pixel((s16)(x + n + 2), y);
            gfx_put_pixel(x, y, c);
            DSB(DRAW_COL) = s;
            gfx_line((s16)(x + 1), y, (s16)(x + n + 1), y);
        } else if (x < ref_x) {
            u8 s = (u8)gfx_get_pixel((s16)(x - n - 2), y);
            gfx_put_pixel(x, y, c);
            DSB(DRAW_COL) = s;
            gfx_line((s16)(x - 1), y, (s16)(x - n - 1), y);
        }
        x = (s16)(x + dx);
        y++;
    }
}

/* PS 2c11:0362 text_engrave_center(s, c1, c2, c3) */
void text_engrave_center(u16 s, u16 c1, u16 c2, u16 c3)
{
    if (!s) return;
    DSB(TXT_FLAGS) = 1;
    s16 tx = (s16)ps_text_center_x(s, (u16)RXs, (u16)RWs);
    DSB(TXT_FG) = (u8)c1;
    gfx_text(s, tx, (s16)(RYs + 4));
    DSB(TXT_FG) = (u8)c2;
    gfx_text(s, tx, (s16)(RYs + 5));
    DSW(T_EDGE_C3) = c3;
    DSW(T_EDGE_C1) = c1;
    text_edge(s, tx, (s16)(RYs + 4));
}

/* PS 2c11:03d4 / AP 53ec:01e6 text_engrave(s, c1, c2, c3, tx, ty) — the standard label style */
void text_engrave(u16 s, u16 c1, u16 c2, u16 c3, u16 tx, u16 ty)
{
    const UiGlobals *g = ug();
    if (!s) return;
    LB(G_drv_text_flags) = 1;
    LB(G_drv_text_fg) = (u8)c1;
    gfx_text(s, (s16)tx, (s16)ty);
    LB(G_drv_text_fg) = (u8)c2;
    gfx_text(s, (s16)tx, (s16)(ty + 1));
    DSW(g->c3) = c3;
    DSW(g->c1) = c1;
    text_edge(s, (s16)tx, (s16)ty);
}

/* PS 2c11:042c draw_bevel_box */
void draw_bevel_box(void)
{
    box_c(0xB8, RXs, RYs, RWs, RHs);
    rect_save();
    RWs = (s16)(RXs + RWs);
    RHs = (s16)(RYs + RHs);
    DSB(FILL_ON) = 0;
    s16 x = RXs, y = RYs, R = RWs, B = RHs;
    line_c(0xC2, x, y, R, y);
    line_c(0xB4, (s16)(x + 1), (s16)(y + 1), (s16)(R - 1), (s16)(y + 1));
    line_c(0xC0, x, (s16)(y + 1), x, (s16)(B - 1));
    gfx_line(R, (s16)(y + 1), R, (s16)(B - 1));
    line_c(0xBF, (s16)(x + 1), (s16)(y + 2), (s16)(x + 1), B);
    gfx_line((s16)(R - 1), (s16)(y + 2), (s16)(R - 1), B);
    line_c(0xC4, (s16)(x + 1), B, (s16)(R - 1), B);
    line_c(0xC1, x, (s16)(B - 1), R, (s16)(B - 1));
    line_c(0xC0, x, (s16)(B - 2), R, (s16)(B - 2));
    ps_rect_restore();
}

/* PS 2c11:0592 draw_rivets(rx, ry, n) */
void draw_rivets(u16 rx, u16 ry, u16 n)
{
    u16 f = parts(2);
    s16 y = (s16)ry;
    for (s16 i = 0; i < (s16)n; i++, y = (s16)(y + 7)) sprite(f, (s16)rx, y, 0);
}

/* PS 2c11:05cf draw_groove(no_bottom) */
void draw_groove(u8 no_bottom)
{
    rect_save();
    RWs = (s16)(RXs + RWs);
    RHs = (s16)(RYs + RHs);
    s16 x = RXs, y = RYs, R = RWs, B = RHs;
    line_c(0xC2, x, y, (s16)(R - 1), y);
    line_c(0xC0, x, (s16)(y + 1), x, (s16)(B - 2));
    gfx_line((s16)(R - 1), (s16)(y + 1), (s16)(R - 1), (s16)(B - 2));
    line_c(0xC7, x, (s16)(B - 1), (s16)(R - 1), (s16)(B - 1));
    if (no_bottom == 0) line_c(0xC0, x, B, (s16)(R - 1), B);
    ps_rect_restore();
}

/* PS 2c11:069a draw_inset_box */
void draw_inset_box(void)
{
    rect_save();
    RWs = (s16)(RXs + RWs);
    RHs = (s16)(RYs + RHs);
    s16 x = RXs, y = RYs, R = RWs, B = RHs;
    line_c(0xC6, x, y, R, y);
    line_c(0xC0, x, y, x, B);
    gfx_line(R, y, R, B);
    line_c(0xB4, x, B, R, B);
    line_c(0xC7, (s16)(x + 1), (s16)(y + 1), (s16)(R - 1), (s16)(y + 1));
    gfx_line((s16)(x + 1), (s16)(y + 1), (s16)(x + 1), (s16)(B - 1));
    gfx_line((s16)(R - 1), (s16)(y + 1), (s16)(R - 1), (s16)(B - 1));
    line_c(0xC2, (s16)(x + 1), (s16)(B - 1), (s16)(R - 1), (s16)(B - 1));
    ps_rect_restore();
    box_c(0xC0, (s16)(RXs + 2), (s16)(RYs + 2), (s16)(RWs - 3), (s16)(RHs - 3));
}

/* PS 2c11:07de draw_inset_box2 */
void draw_inset_box2(void) { draw_inset_box(); }

/* PS 2c11:07e7 text_center_x(s, x, w) */
u16 ps_text_center_x(u16 s, u16 x_, u16 w_)
{
    s16 x = (s16)x_, d = (s16)((s16)w_ - gfx_text_width(s));
    if (DSB(TXT_FLAGS) & 2) d = (s16)(d - (s16)rtl_strlen(s));
    if (d > 0) x = (s16)(x + d / 2 + 1);
    return (u16)x;
}

/* PS 2c11:0829 tile_fill(tx, ty, tw, th): the staggered PARTS[1] brick texture */
void ps_tile_fill(s16 tx, s16 ty, s16 tw, s16 th)
{
    DSB(CLIP_ON) = 1;
    DSS(CLIP_X1) = tx;
    DSS(CLIP_X2) = (s16)(tx + tw - 1);
    DSS(CLIP_Y1) = ty;
    DSS(CLIP_Y2) = (s16)(ty + th - 1);
    u16 f = parts(1);
    s16 step = (s16)(frame_w(f) - 2);
    for (; ty <= DSS(CLIP_Y2); ty = (s16)(ty + 3)) {
        s16 col = tx;
        if (DSS(CLIP_X1) <= tx) { col = (s16)(tx - step); tx = col; }
        for (; col <= DSS(CLIP_X2); col = (s16)(col + step)) sprite(f, col, ty, 0);
        tx = (s16)(tx + 4);
    }
    DSB(CLIP_ON) = 0;
}

/* PS 2c11:08b4 tile_fill_rect */
void tile_fill_rect(void) { ps_tile_fill(RXs, RYs, RWs, RHs); }

/* PS 2c11:08d0 screen_clear(c) */
void screen_clear(u8 c)
{
    DSB(FILL_ON) = 1;
    DSB(CLIP_ON) = 0;
    DSB(FILL_COL) = c;
    DSB(DRAW_COL) = c;
    DSW(PG_WRITE) = DSW(PG_BACK);
    gfx_rect(0, 0, 320, 200);
}

/* PS 2c11:0902 screen_clear_black */
void screen_clear_black(void) { screen_clear(0); }

/* PS 2c11:090f screen_clear_panel */
void screen_clear_panel(void) { screen_clear(0xB8); }

/* PS 2c11:091c rect_fill_c1 */
void rect_fill_c1(void) { box_c(0xC1, RXs, RYs, RWs, RHs); }

/* PS 2c11:0946 draw_bottom_bar */
void draw_bottom_bar(void)
{
    s16 yb = (s16)(RYs - 25);
    sprite(parts(5), 0, yb, 0);
    sprite(parts(5), 296, yb, 2);
}

/* PS 2c11:0980 draw_title_bar(sym, title) */
void draw_title_bar(s8 sym, u16 title)
{
    rect_save();
    RXs = 49; RYs = 0; RWs = 219; RHs = 23;
    DSW(PG_WRITE) = DSW(PG_BACK);
    DSB(CLIP_ON) = 0;
    tile_fill_rect();
    ps_draw_frame();
    sprite(parts(3), 0, 0, 0);
    sprite(parts(3), 295, 0, 2);
    sprite(parts(4), 21, 0, 0);
    sprite(parts(4), 266, 0, 2);
    sprite(symbols(sym), 24, 2, 0);
    sprite(symbols(sym), 272, 2, 0);
    gfx_font_select((s16)DSW(T_FONT1));
    s16 tx = (s16)ps_text_center_x(title, (u16)RXs, (u16)RWs);
    ps_text_emboss(title, (u16)tx, (u16)(RYs + 3), 0xC1, 0xC0, 0xB1);
    s16 k = (s16)((tx - RXs) / 7);
    if (k > 4) k = 4;
    for (s16 i = 1; i <= k; i++) {
        draw_rivets((u16)(RXs + i * 7 - 3), (u16)(RYs + 3), 3);
        draw_rivets((u16)(RXs + RWs - i * 7), (u16)(RYs + 3), 3);
    }
    ps_rect_restore();
}

/* PS 2c11:0b00 draw_title_bar_for(service, date, title) */
void draw_title_bar_for(u16 service, u16 date, u16 title)
{
    u16 m = stk_mark();
    u16 d = stk_alloc(6);
    for (int i = 0; i < 3; i++) DSB((u16)(d + i)) = DSB((u16)(T_TODAY + i));   /* SCOPY@ 3 bytes */
    u16 today = date_make(ds_ptr(d));
    stk_release(m);
    DSW(PG_WRITE) = DSW(PG_BACK);
    DSB(CLIP_ON) = 0;
    s8 sym = (s8)service == 6 ? 7 : (s8)service_side((u16)(s16)(s8)service);
    if (sym == 1 && today < date) sym = 6;
    draw_title_bar(sym, title);
}

/* PS 2c11:0b65 draw_frame_lines */
void draw_frame_lines(s16 x0, s16 y0, s16 x1, s16 y1, s16 bx0, s16 by1, s16 bx1, s16 by)
{
    line_c(0xC2, x0, y0, x1, y0);
    line_c(0xB5, x0, (s16)(y0 + 1), x1, (s16)(y0 + 1));
    line_c(0xC0, x0, (s16)(y1 + 1), x0, by1);
    gfx_line(x1, (s16)(y1 + 1), x1, by1);
    line_c(0xBF, (s16)(x0 + 1), (s16)(y1 + 2), (s16)(x0 + 1), by1);
    gfx_line((s16)(x1 - 1), (s16)(y1 + 2), (s16)(x1 - 1), by1);
    line_c(0xC1, bx0, by, bx1, by);
    line_c(0xC4, bx0, (s16)(by - 1), bx1, (s16)(by - 1));
    DSB(CLIP_ON) = 0;
}

/* PS 2c11:0c46 draw_frame */
void ps_draw_frame(void)
{
    DSB(CLIP_ON) = 1;
    DSS(CLIP_X1) = RXs;
    DSS(CLIP_X2) = (s16)(RXs + RWs);
    DSS(CLIP_Y1) = RYs;
    DSS(CLIP_Y2) = (s16)(RYs + RHs);
    draw_frame_lines(RXs, RYs, DSS(CLIP_X2), RYs, RXs, DSS(CLIP_Y2), DSS(CLIP_X2), DSS(CLIP_Y2));
    DSB(CLIP_ON) = 0;
}

/* PS 2c11:0c8e draw_frame_inset(n, top) */
void draw_frame_inset(s16 n, s16 top)
{
    DSB(CLIP_ON) = 1;
    DSS(CLIP_X1) = RXs;
    DSS(CLIP_X2) = (s16)(RXs + RWs);
    DSS(CLIP_Y1) = (s16)(RYs + 2);
    DSS(CLIP_Y2) = (s16)(RYs + RHs);
    draw_frame_lines(RXs, RYs, DSS(CLIP_X2), (s16)(RYs + top), (s16)(RXs + n), (s16)(DSS(CLIP_Y2) - n),
                     (s16)(DSS(CLIP_X2) - n), DSS(CLIP_Y2));
    DSB(CLIP_ON) = 0;
}

/* PS 2c11:0cec draw_plate_corners(top) */
void draw_plate_corners(s16 top)
{
    bevel_rows(RXs, (s16)(RXs + 6), (s16)(RYs + top - 6), 7, -1, 0xC2);
    bevel_rows((s16)(RXs + RWs), (s16)(RXs + RWs - 7), (s16)(RYs + top - 7), 7, 1, 0xC2);
    bevel_rows(RXs, RXs, (s16)(RYs + RHs - 6), 6, 1, 0xC0);
    bevel_rows((s16)(RXs + RWs), (s16)(RXs + RWs), (s16)(RYs + RHs - 6), 6, -1, 0xC0);
}

/* PS 2c11:0d96 draw_plate_bottom */
void draw_plate_bottom(void)
{
    s16 x = RXs, R = (s16)(RXs + RWs), B = (s16)(RYs + RHs);
    s16 b1 = (s16)(B + 1), b5 = (s16)(B + 5);
    line_c(0xC1, (s16)(x + 15), b1, (s16)(x + 20), b5);
    gfx_line((s16)(R - 15), b1, (s16)(R - 20), b5);
    line_c(0xC0, (s16)(x + 16), b1, (s16)(x + 21), b5);
    gfx_line((s16)(R - 16), b1, (s16)(R - 21), b5);
    line_c(0xC4, (s16)(x + 17), b1, (s16)(R - 17), b1);
    line_c(0xC6, (s16)(x + 18), (s16)(B + 4), (s16)(R - 18), (s16)(B + 4));
    line_c(0xC1, (s16)(x + 19), b5, (s16)(R - 19), b5);
    line_c(0xBE, (s16)(x + 18), (s16)(B + 2), (s16)(R - 18), (s16)(B + 2));
    gfx_line((s16)(x + 19), (s16)(B + 3), (s16)(R - 19), (s16)(B + 3));
}

/* PS 2c11:0ed6 draw_title_plate — toolkit §2.1 */
void draw_title_plate(void)
{
    u16 save_read = DSW(PG_READ);
    u16 label = fw(cur_item(), IT_LABEL);
    if (label != 0) {
        DSW(PG_READ) = DSW(PG_WRITE);
        gfx_font_select((s16)DSW(T_FONT2));
        s16 tx = (s16)ps_text_center_x(label, (u16)RXs, (u16)RWs);
        s16 tw = gfx_text_width(label);
        s16 lpad = (s16)(tx - RXs - 2);
        s16 rpad = (s16)(RXs + RWs - (tx + tw) - 1);
        u16 P = parts(0);
        s16 ph = frame_h(P);
        FarPtr buf = ds_far(T_PLATE_BUF);
        ps_tile_fill(RXs, (s16)(RYs + ph), RWs, (s16)(RHs - ph - 5));
        ps_tile_fill((s16)(RXs + 6), (s16)(RYs + ph - 7), (s16)(RWs - 12), 7);
        line_c(0xC2, (s16)(RXs + lpad - 4), RYs, (s16)(RXs + RWs - rpad), RYs);
        gfx_line((s16)(RXs + 7), (s16)(RYs + ph - 7), (s16)(RXs + lpad - 12), (s16)(RYs + ph - 7));
        line_c(0xB5, (s16)(RXs + lpad - 4), (s16)(RYs + 1), (s16)(RXs + RWs - rpad), (s16)(RYs + 1));
        gfx_line((s16)(RXs + 6), (s16)(RYs + ph - 6), (s16)(RXs + lpad - 17), (s16)(RYs + ph - 6));
        line_c(0xC2, (s16)(RXs + RWs - rpad + 10), (s16)(RYs + ph - 7), (s16)(RXs + RWs - 7), (s16)(RYs + ph - 7));
        line_c(0xB5, (s16)(RXs + RWs - rpad + 16), (s16)(RYs + ph - 6), (s16)(RXs + RWs - 6), (s16)(RYs + ph - 6));
        s16 L = (s16)(RXs + lpad);
        s16 c = (s16)(L - 5);
        s16 Rr = (s16)(RXs + RWs - rpad);
        gfx_get_rect(buf, (s16)(L - 15), (s16)(RYs + ph), 10, 10);
        gfx_put_rect(buf, (s16)(L - 15), (s16)(RYs + 4), 10, 10);
        s16 lx = (s16)(L - 16), ly = (s16)(RYs + 5);
        DSB(DRAW_COL) = (u8)gfx_get_pixel((s16)(L - 15), ly);
        for (s16 k = 8; k > 0; k--) {
            gfx_line(lx, ly, lx, (s16)(ly + k));
            lx--;
            ly++;
        }
        for (; c < Rr; c = (s16)(c + 10)) {
            gfx_get_rect(buf, c, (s16)(RYs + ph), 10, 10);
            gfx_put_rect(buf, c, (s16)(RYs + 2), 10, 10);
            gfx_put_rect(buf, c, (s16)(RYs + ph - 8), 10, 10);
        }
        gfx_put_rect(buf, (s16)(Rr - 4), (s16)(RYs + 2), 10, 10);
        gfx_put_rect(buf, (s16)(Rr - 4), (s16)(RYs + ph - 8), 10, 10);
        gfx_get_rect(buf, (s16)(Rr + 6), (s16)(RYs + ph), 10, 10);
        gfx_put_rect(buf, (s16)(Rr + 6), (s16)(RYs + 4), 10, 10);
        lx = (s16)(Rr + 16);
        ly = (s16)(RYs + 5);
        DSB(DRAW_COL) = (u8)gfx_get_pixel((s16)(Rr + 15), ly);
        for (s16 k = 8; k > 0; k--) {
            gfx_line(lx, ly, lx, (s16)(ly + k));
            lx++;
            ly++;
        }
        s16 end = (s16)(RXs + RWs);
        for (c = (s16)(RXs + 5); c < (s16)(end - 9); c = (s16)(c + 10)) {
            gfx_get_rect(buf, c, (s16)(RYs + RHs - 20), 10, 10);
            gfx_put_rect(buf, c, (s16)(RYs + RHs - 10), 10, 10);
        }
        draw_frame_inset(6, ph);
        ps_text_emboss(label, 0xFFFF, 0xFFFF, 0xC1, 0xC0, 0xB1);
        sprite(P, (s16)(tx - frame_w(P) - 2), RYs, 2);
        sprite(P, (s16)(tx + tw + 1), RYs, 0);
        draw_plate_corners(ph);
    }
    DSW(PG_READ) = save_read;
}

/* PS 2c11:136a draw_titled_panel */
void draw_titled_panel(void)
{
    draw_title_plate();
    draw_plate_bottom();
}

/* PS 2c11:1377 draw_list_button_down */
void draw_list_button_down(void)
{
    draw_groove(0);
    draw_button_face(0);
    gfx_font_select((s16)DSW(T_FONT4));
    u16 l = fw(cur_item(), IT_LABEL);
    text_engrave(l, 0xC4, 0xC9, 0xC7, ps_text_center_x(l, (u16)RXs, (u16)RWs), (u16)(RYs + 5));
}

/* PS 2c11:13d1 draw_list_button_up */
void draw_list_button_up(void)
{
    draw_groove(0);
    draw_button_face(1);
    gfx_font_select((s16)DSW(T_FONT4));
    u16 l = fw(cur_item(), IT_LABEL);
    text_engrave(l, 0xC4, 0xC9, 0xC6, ps_text_center_x(l, (u16)RXs, (u16)RWs), (u16)(RYs + 4));
}

/* PS 2c11:142b draw_button_face(pressed) */
void draw_button_face(u8 pressed)
{
    box_c(0xC3, (s16)(RXs + 1), (s16)(RYs + 1), (s16)(RWs - 2), (s16)(RHs - 2));
    rect_save();
    s16 r = (s16)(RXs + RWs);
    RWs = (s16)(r - 2);
    RHs = (s16)(RYs + RHs);
    line_c(pressed == 0 ? 0xC9 : 0xC6, (s16)(RXs + 3), (s16)(RYs + 2), (s16)(r - 4), (s16)(RYs + 2));
    line_c(0xC8, (s16)(RXs + 3), (s16)(RYs + 3), (s16)(RXs + 3), (s16)(RHs - 3));
    gfx_line((s16)(RWs - 2), (s16)(RYs + 3), (s16)(RWs - 2), (s16)(RHs - 3));
    line_c(pressed == 0 ? 0xC6 : 0xC9, (s16)(RXs + 3), (s16)(RHs - 3), (s16)(RWs - 2), (s16)(RHs - 3));
    ps_rect_restore();
    u8 f = pressed == 0 ? 0xC8 : 0xC7;
    box_c(f, (s16)(RXs + 4), (s16)(RYs + 3), (s16)(RWs - 8), (s16)(RHs - 6));
}

/* PS 2c11:1575 draw_button_up */
void draw_button_up(void)
{
    gfx_font_select((s16)DSW(T_FONT4));
    u16 l = fw(cur_item(), IT_LABEL);
    u16 tx = ps_text_center_x(l, (u16)RXs, (u16)RWs);
    draw_groove(0);
    draw_button_face(1);
    text_engrave(l, 0xC4, 0xC9, 0xC6, tx, (u16)(RYs + 5));
}

/* PS 2c11:15d4 draw_button_down */
void draw_button_down(void)
{
    gfx_font_select((s16)DSW(T_FONT4));
    u16 l = fw(cur_item(), IT_LABEL);
    u16 tx = ps_text_center_x(l, (u16)RXs, (u16)RWs);
    draw_groove(0);
    draw_button_face(0);
    text_engrave(l, 0xC4, 0xC9, 0xC7, tx, (u16)(RYs + 6));
}

/* PS 2c11:1633 draw_popup_box */
void draw_popup_box(void)
{
    box_c(0xB8, RXs, RYs, RWs, RHs);
    DSB(FILL_ON) = 0;
    DSB(DRAW_COL) = 0xC4;
    gfx_rect(RXs, RYs, RWs, RHs);
    line_c(0xC2, (s16)(RXs + 1), RYs, (s16)(RXs + RWs - 2), RYs);
    line_c(0xC0, RXs, (s16)(RYs + 1), RXs, (s16)(RYs + RHs - 2));
    s16 r = (s16)(RXs + RWs - 1);
    gfx_line(r, (s16)(RYs + 1), r, (s16)(RYs + RHs - 2));
    s16 b = (s16)(RYs + RHs - 1);
    line_c(0xC7, (s16)(RXs + 1), b, (s16)(RXs + RWs - 2), b);
    DSB(DRAW_COL) = 0xC3;
    gfx_rect((s16)(RXs + 1), (s16)(RYs + 1), (s16)(RWs - 2), (s16)(RHs - 2));
}

/* PS 2c11:1746 draw_corner_rivets */
void draw_corner_rivets(void)
{
    u16 f = parts(2);
    sprite(f, (s16)(RXs + 2), (s16)(RYs + 2), 0);
    sprite(f, (s16)(RXs + 2), (s16)(RYs + RHs - 6), 0);
    sprite(f, (s16)(RXs + RWs - 5), (s16)(RYs + 2), 0);
    sprite(f, (s16)(RXs + RWs - 5), (s16)(RYs + RHs - 6), 0);
}

/* PS 2c11:17db draw_riveted_panel */
void draw_riveted_panel(void)
{
    tile_fill_rect();
    ps_draw_frame();
    draw_corner_rivets();
}

/* PS 2c11:17ec draw_header_panel */
void draw_header_panel(void)
{
    draw_bevel_box();
    draw_corner_rivets();
    gfx_font_select((s16)DSW(T_FONT4));
    text_engrave_center(fw(cur_item(), IT_LABEL), 0xC1, 0xB1, 0xC0);
}

/* PS 2c11:181e draw_side_riveted_panel */
void draw_side_riveted_panel(void)
{
    tile_fill_rect();
    ps_draw_frame();
    for (s16 ry = (s16)(RYs + 5); ry <= (s16)(RYs + RHs - 6); ry = (s16)(ry + 7)) {
        draw_rivets((u16)(RXs + 4), (u16)ry, 1);
        draw_rivets((u16)(RXs + RWs - 6), (u16)ry, 1);
    }
}

/* PS 2c11:1871 draw_small_button_up */
void draw_small_button_up(void)
{
    gfx_font_select((s16)DSW(T_FONT3));
    u16 l = fw(cur_item(), IT_LABEL);
    u16 tx = ps_text_center_x(l, (u16)RXs, (u16)RWs);
    draw_groove(1);
    draw_button_face(1);
    text_engrave(l, 0xC4, 0xC9, 0xC6, tx, (u16)(RYs + 4));
}

/* PS 2c11:18d0 draw_small_button_down */
void draw_small_button_down(void)
{
    gfx_font_select((s16)DSW(T_FONT3));
    u16 l = fw(cur_item(), IT_LABEL);
    u16 tx = ps_text_center_x(l, (u16)RXs, (u16)RWs);
    draw_groove(1);
    draw_button_face(0);
    text_engrave(l, 0xC4, 0xC9, 0xC7, tx, (u16)(RYs + 5));
}

/* PS 2c11:192f btn_small_cb(border) -> activated */
u16 btn_small_cb(u16 border)
{
    if (border) {
        rect_grow(2);
        rect_save();
        RXs = (s16)(RXs - 1);
        RWs = (s16)(RXs + RWs + 1);
        RHs = (s16)(RYs + RHs);
        line_c(0xC2, RXs, RYs, RWs, RYs);
        line_c(0xBF, RXs, (s16)(RYs + 1), RXs, RHs);
        gfx_line(RWs, (s16)(RYs + 1), RWs, RHs);
        line_c(0xC0, RXs, RHs, RWs, RHs);
        ps_rect_restore();
        ps_rect_shrink(2);
    }
    if ((DSW(T_EVT) & 0x0C) == 0) draw_small_button_up();
    else draw_small_button_down();
    bool act = (DSW(T_EVT) & 0x10) != 0;
    if (act) snd_play(0x18);
    return act;
}

/* PS 2c11:1a0e btn_cb -> activated: the standard button callback */
u16 btn_cb(void)
{
    if ((DSW(T_EVT) & 0x0C) == 0) draw_button_up();
    else draw_button_down();
    bool act = (DSW(T_EVT) & 0x10) != 0;
    if (act) snd_play(0x18);
    return act;
}

/* PS 2c11:1a40 diamond_edge(i) */
static void diamond_edge(u16 i)
{
    u16 j = (u16)((i + 1) & 3);
    gfx_line(DSS((u16)(T_DIA_X + 2 * i)), DSS((u16)(T_DIA_Y + 2 * i)),
             DSS((u16)(T_DIA_X + 2 * j)), DSS((u16)(T_DIA_Y + 2 * j)));
}

/* PS 2c11:1a76 draw_diamond(fill, phase) */
void draw_diamond(u8 fill, u16 phase)
{
    DSS(T_DIA_X) = (s16)(RXs + (RWs >> 1));
    DSS(T_DIA_X + 6) = RXs;
    DSS(T_DIA_X + 2) = (s16)(RXs + RWs - 1);
    DSS(T_DIA_Y + 2) = (s16)(RYs + (RWs >> 1));
    DSS(T_DIA_Y) = RYs;
    DSS(T_DIA_Y + 4) = (s16)(RYs + RHs - 1);
    DSB(FILL_ON) = 1;
    DSB(FILL_COL) = fill;
    DSB(DRAW_COL) = fill;
    DSS(T_DIA_Y + 6) = DSS(T_DIA_Y + 2);
    DSS(T_DIA_X + 4) = DSS(T_DIA_X);
    gfx_polygon(4, T_DIA_X, T_DIA_Y);
    for (u16 i = 0; (s16)i < 4; i++, phase++) {
        DSB(DRAW_COL) = (i & 2) == 0 ? 0xC9 : 0xC6;
        diamond_edge(phase & 3);
    }
}

/* PS 2c11:1aff diamond_label(q, pressed) */
void diamond_label(s16 q, u8 pressed)
{
    u16 c3 = pressed == 0 ? 0xC7 : 0xC8;
    s16 x = (s16)(RXs + q), y = (s16)(RYs + (s16)(s8)pressed);
    gfx_font_select((s16)DSW(T_FONT4));
    text_engrave(fw(cur_item(), IT_LABEL), 0xC4, 0xC9, c3, (u16)(x + 4), (u16)(y + 4));
}

/* PS 2c11:1b65 draw_diamond_up */
void draw_diamond_up(void)
{
    draw_diamond(0xC7, 1);
    diamond_label((s16)(fw(cur_item(), IT_USER) & 3), 0);
}

/* PS 2c11:1b8d draw_diamond_down */
void draw_diamond_down(void)
{
    draw_diamond(0xC8, 3);
    diamond_label((s16)(fw(cur_item(), IT_USER) & 3), 1);
}

/* PS 2c11:1bb5 diamond_cb -> activated */
u16 diamond_cb(void)
{
    if ((DSW(T_EVT) & 0x0C) == 0) draw_diamond_up();
    else draw_diamond_down();
    bool act = (DSW(T_EVT) & 0x10) != 0;
    if (act) snd_play(0x18);
    return act;
}

/* PS 2c11:1be7 panel_fill_frame */
void panel_fill_frame(void)
{
    DSB(CLIP_ON) = 0;
    rect_fill_c1();
    ps_draw_frame();
}

__attribute__((constructor)) static void draw_codeptrs(void)
{
    REG(draw_bevel_box);
    REG(draw_inset_box);
    REG(draw_inset_box2);
    REG(tile_fill_rect);
    REG(screen_clear_black);
    REG(screen_clear_panel);
    REG(rect_fill_c1);
    REG(draw_bottom_bar);
    codeptr_register(PROG_PS, PS_FN_draw_frame, ps_draw_frame);
    REG(draw_title_plate);
    REG(draw_titled_panel);
    REG(draw_list_button_down);
    REG(draw_list_button_up);
    REG(draw_button_up);
    REG(draw_button_down);
    REG(draw_popup_box);
    REG(draw_corner_rivets);
    REG(draw_riveted_panel);
    REG(draw_header_panel);
    REG(draw_side_riveted_panel);
    REG(draw_small_button_up);
    REG(draw_small_button_down);
    REG(btn_cb);
    REG(draw_diamond_up);
    REG(draw_diamond_down);
    REG(diamond_cb);
    REG(panel_fill_frame);
    REG(plate_buf_alloc);
    REG(plate_buf_free);
    REG(rect_save);
    codeptr_register(PROG_PS, PS_FN_rect_restore, ps_rect_restore);
    REG(rect_copy);
}
