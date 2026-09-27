/* sim_flow: dialogs.c — dialog library, part 2: the text box (5119), dialog titles (5247), keyboard
 * focus / activation / redraw (53a6), shadowed title text (53ec), .BOX loading and the alrtlib2.c
 * button variants (6396) — sim_flow_ui.md §6. The near helpers xxx / xxx+1 are the "inc bp" entry
 * and its body: one C function, the _body name calls it. */
#include "sim_flow/sfb_priv.h"

/* ---- 5119: the text box (a framed banner with an embossed text) ---------------------------------- */

/* AP 5119:0000 textbox_init(): the two end caps below the box */
void textbox_init(void)
{
    s16 y = (s16)(RY - 0x19);
    u16 b = DSW(DSW(0x3BD7) + 0x16);
    gfx_sprite_flip(b, 0, y, 0);
    gfx_sprite_flip(b, 0x128, y, 2);
}

/* AP 5119:0037 textbox_draw(flag, s) */
void textbox_draw(u8 flag, u16 s)
{
    rect_save();
    RX = 0x31;
    RY = 0;
    RW = 0xDB;
    RH = 0x17;
    DSW(0x26D8) = DSW(0x26D2);
    DSB(0x26C3) = 0;
    fill_item(far_make(0, 0), 0);
    frame_lines_item(far_make(0, 0));
    u16 set = DSW(0x3BD7);
    gfx_sprite_flip(DSW(set + 0x12), 0, 0, 0);
    gfx_sprite_flip(DSW(set + 0x12), 0x127, 0, 2);
    gfx_sprite_flip(DSW(set + 0x14), 0x15, 0, 0);
    gfx_sprite_flip(DSW(set + 0x14), 0x10A, 0, 2);
    s16 k = flag ? 1 : 0;
    gfx_sprite_flip(DSW(set + 2 * k), 0x18, 0, 2);
    gfx_sprite_flip(DSW(set + 2 * k), 0x110, 0, 2);
    gfx_font_select(DSS(0x3F32));
    s16 tx = ap_text_center_x(s, RX, RW);
    ap_text_emboss(s, (u16)tx, (u16)(RY + 3), 0xC1, 0xC0, 0xB1);
    s16 n = (s16)((tx - RX) / 7);
    if (n > 4) n = 4;
    s16 d = 7;
    for (s16 i = 1; i <= n; i++) {
        item_frame_bitmap((s16)(RX + d - 3), (s16)(RY + 3), 3);
        item_frame_bitmap((s16)(RX + RW - d), (s16)(RY + 3), 3);
        d = (s16)(d + 7);
    }
    ap_rect_restore(0);
}

/* AP 5119:01ad textbox_cb(kind, s) */
void textbox_cb(s8 kind, u16 s)
{
    DSW(0x26D8) = DSW(0x26D2);
    DSB(0x26C3) = 0;
    textbox_draw(kind <= 2 ? 1 : 0, s);
}

/* ---- 5247: dialog titles -------------------------------------------------------------------------- */

/* AP 5247:0000 dlg_title_text(it) — callback: inset box with the value text (realism %, VCR title) */
void dlg_title_text(FarPtr unused)
{
    (void)unused;
    inset_box();
    FarPtr it = CUR_ITEM();
    u16 s = IT_W(it, 0x0E);
    if (s != 0) {
        DSB(0x26C0) = DSB(0x0646);
        DSB(0x26C2) = 1;
        gfx_font_select(DSW(0x0648) != 0 ? DSS(0x3F32) : DSS(0x3F2E));
        gfx_text(s, ap_text_center_x(s, RX, RW), (s16)(RY + 3));
    }
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
}

/* AP 5247:0070 dlg_title_shadow(it) — callback: engraved value text */
void dlg_title_shadow(FarPtr unused)
{
    (void)unused;
    FarPtr it = CUR_ITEM();
    u16 s = IT_W(it, 0x0E);
    if (s != 0) {
        gfx_font_select(DSS(0x3F2E));
        s16 tx = ap_text_center_x(s, RX, RW);
        text_engrave(s, 0xC7, 0xB3, 0xC0, (u16)tx, (u16)(RY + 1));
    }
}

/* ---- 53a6: activation, keyboard focus ------------------------------------------------------------- */

/* AP 53a6:0000 dlg_items() -> DS:00D6 (far item table) */
FarPtr dlg_items(void)
{
    return ds_far(0x00D6);
}

/* AP 53a6:000b dlg_count() -> DS:00DC */
s16 dlg_count(void)
{
    return DSS(0x00DC);
}

/* AP 53a6:0016 pointer_buf_alloc(bitmap): the mouse pointer bitmap DS:00DA and its save buffer DS:00DE */
u16 pointer_buf_alloc(u16 bm)
{
    DSW(0x00DA) = 0;
    if (DSL(0x00DE) != 0) dos_free(ds_far(0x00DE));
    if (bm != 0) {
        u32 n = gfx_rect_size(DSW(bm + 6), DSW(bm + 8));
        FarPtr p = mem_far_new((u32)(u16)n, 0);
        ds_far_wr(0x00DE, p);
        if (far_is_null(p)) return 0;
    }
    DSW(0x00DA) = bm;
    return 1;
}

/* AP 53a6:0077 focus_prev() (near): recompute DS:3BCD and the rectangle of the current item DS:3BCF */
void focus_prev(void)
{
    u32 cur = DSL(0x3BCF);
    CUR_IDX = 0;
    DSL(0x3BCF) = DSL(0x00D6);
    while (CUR_IDX < DSS(0x00DC)) {
        item_iter(0);
        if (DSL(0x3BCF) == cur) return;
        CUR_IDX++;
        DSW(0x3BCF) += 0x16;
    }
}
/* AP 53a6:0078 */
void focus_prev_body(void) { focus_prev(); }

/* AP 53a6:00c1 focus_next(): the pointer to the focused item (its centre, or value+3 with flag 4) */
void focus_next(void)
{
    DSW(0x42D8) = (u16)(RY + (RH >> 1));
    FarPtr it = CUR_ITEM();
    if (IT_B(it, 0x0D) & 4) DSW(0x42DA) = (u16)(IT_W(it, 0x0E) + 3);
    else DSW(0x42DA) = (u16)(RX + (RW >> 1));
    pointer_store(DSS(0x42DA), DSS(0x42D8));
}

static void focus_item(s16 i)
{
    FarPtr items = ds_far(0x00D6);
    CUR_IDX = i;
    ds_far_wr(0x3BCF, far_make(items.seg, (u16)(items.off + i * 0x16)));
}

/* AP 53a6:0107 focus_move() -> 1 when the pointer DS:42DA/42D8 is on a hot item (else it moves to the
 * nearest one) (near) */
s16 focus_move(void)
{
    if (DSW(0x42DE) == 0) return 0;
    if (DSS(0x42DC) == -1) return 1;
    s16 best = 0, bestd = 0x7FFF;
    for (s16 k = 0; k < DSS(0x42DE); k++) {
        focus_item(DSS(0x42E0 + 2 * k));
        focus_prev();
        if (point_in_item(far_make(0, 0), DSS(0x42DA), DSS(0x42D8))) {
            DSW(0x42DC) = (u16)k;
            focus_next();
            return 1;
        }
        s16 dx = (s16)(RX + (RW >> 1) - DSS(0x42DA)), dy = (s16)(RY + (RH >> 1) - DSS(0x42D8));
        if (dx < 0) dx = (s16)-dx;
        if (dy < 0) dy = (s16)-dy;
        s16 d = (s16)(dx + dy);
        if (d < bestd) { bestd = d; best = k; }
    }
    DSW(0x42DC) = (u16)best;
    focus_item(DSS(0x42E0 + 2 * best));
    focus_prev();
    focus_next();
    return 0;
}
/* AP 53a6:0108 */
s16 focus_move_body(void) { return focus_move(); }

/* AP 53a6:0202 focus_draw(x, y, delta) (near): cycle the keyboard focus over the hot list */
void focus_draw(s16 x, s16 y, s16 delta)
{
    DSW(0x42DA) = (u16)x;
    DSW(0x42D8) = (u16)y;
    DSW(0x42DC) = (u16)((s16)(DSS(0x42DC) + DSS(0x42DE) + delta) % DSS(0x42DE));
    focus_item(DSS(0x42E0 + 2 * DSS(0x42DC)));
    focus_prev();
    focus_next();
}
/* AP 53a6:0203 */
void focus_draw_body(s16 x, s16 y, s16 delta) { focus_draw(x, y, delta); }

/* AP 53a6:0255 dlg_click(x, y, button): button 2/3 (right/middle) move the focus; the item's .bl
 * byte for the button names a direct neighbour */
s16 dlg_click(s16 x, s16 y, s16 button)
{
    DSW(0x42DA) = (u16)x;
    DSW(0x42D8) = (u16)y;
    if (focus_move() != 0) {
        FarPtr it = CUR_ITEM();
        u16 n = rd8(it.seg, (u16)(it.off + button + 0x12));
        if (n != 0) {
            FarPtr items = ds_far(0x00D6);
            ds_far_wr(0x3BCF, far_make(items.seg, (u16)(items.off + n * 0x16)));
            focus_prev();
            focus_next();
            return 0;
        }
        if (button == 2) focus_draw(x, y, -1);
        else if (button == 3) focus_draw(x, y, 1);
    }
    return 0;
}

/* AP 53a6:02cf dlg_activate(far items, n) */
void dlg_activate(FarPtr items, s16 n)
{
    ds_far_wr(0x00D6, items);
    DSW(0x00DC) = 0;
    if (n > 0) {
        memset(DSP(0x3B90), 0, (u16)n);
        DSW(0x00DC) = (u16)n;
    }
    DSW(0x42DE) = 0;
    DSL(0x3BCF) = DSL(0x00D6);
    CUR_IDX = 0;
    while (CUR_IDX < DSS(0x00DC)) {
        FarPtr it = CUR_ITEM();
        if (IT_B(it, 0x0D) & 2) {
            DSW(0x42E0 + 2 * DSS(0x42DE)) = (u16)CUR_IDX;
            DSW(0x42DE) += 1;
        }
        CUR_IDX++;
        DSW(0x3BCF) += 0x16;
    }
    DSW(0x42DC) = 0xFFFF;
    focus_draw(0, 0, 1);
}

/* AP 53a6:0360 dlg_select(i) */
void dlg_select(s16 i)
{
    focus_item(i);
    focus_prev();
    focus_next();
}

/* AP 53a6:038b dlg_item_count() */
s16 dlg_item_count(void)
{
    return DSS(0x00DC);
}

/* AP 53a6:0393 dlg_key_filter(far items, n) -> DS:3BC1: run every item's redraw callback (mask 40h) */
s16 dlg_key_filter(FarPtr items, s16 n)
{
    DSW(0x3BC1) = 0;
    CUR_IDX = 0;
    ds_far_wr(0x3BCF, items);
    while (CUR_IDX < n) {
        item_iter(0);
        STATE = 0x40;
        FarPtr it = CUR_ITEM();
        if ((IT_B(it, 0x0C) & 0x40) && rd32(it.seg, (u16)(it.off + 8)) != 0) {
            u16 v = IT_W(it, 0x0E);
            s16 swapped = 0;
            if ((IT_B(it, 0x0D) & 0x80) && v != 0xFFFF) {
                wr16(it.seg, (u16)(it.off + 0x0E), str0((s16)v).off);
                swapped = 1;
            }
            codeptr_lookup(far_rd(it.seg, (u16)(it.off + 8)))();
            if (swapped) wr16(it.seg, (u16)(it.off + 0x0E), v);
        }
        CUR_IDX++;
        DSW(0x3BCF) += 0x16;
    }
    return DSS(0x3BC1);
}

/* AP 53a6:044d dlg_redraw_all(key, ascii): the last key for the callbacks (DS:3BBB, DS:3BB9) */
void ap_dlg_redraw_all_53a6(u16 key, u16 ascii)
{
    DSW(0x3BBB) = key;
    DSW(0x3BB9) = ascii;
}

/* ---- 53ec: shadowed text -------------------------------------------------------------------------- */

/* AP 53ec:0000 text_edge_pass(ch, x, y) -> glyph width: ink pixels not already DS:0658 become DS:0656 */
u16 text_edge_pass(u8 ch, s16 px, s16 py)
{
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
            if (bit == 0 && (prev == 0 || !(DSB(0x26C2) & 2))) {
                if (!(DSB(0x26C2) & 1)) gfx_put_pixel((s16)(px + c), py, (s16)DSC(0x26C1));
            } else {
                s16 col = gfx_get_pixel((s16)(px + c), py);
                if (col != DSS(0x0658)) col = DSS(0x0656);
                gfx_put_pixel((s16)(px + c), py, col);
            }
            mask >>= 1;
            prev = bit;
        }
        g = far_add(g, 1);
    }
    return w;
}

/* AP 53ec:013f text_shadow(s, x, y) */
void text_shadow(u16 s, s16 x, s16 y)
{
    while (DSB(s)) {
        u8 c = DSB(s);
        s++;
        x = (s16)(x + text_edge_pass(c, x, y));
        if (DSB(0x26C2) & 2) x++;
    }
}

/* AP 53ec:0174 text_title(s, c1, c2, c3): centred at the item top + 4 */
void text_title(u16 s, u16 c1, u16 c2, u16 c3)
{
    if (s == 0) return;
    DSB(0x26C2) = 1;
    s16 tx = ap_text_center_x(s, RX, RW);
    DSB(0x26C0) = (u8)c1;
    gfx_text(s, tx, (s16)(RY + 4));
    DSB(0x26C0) = (u8)c2;
    gfx_text(s, tx, (s16)(RY + 5));
    DSW(0x0656) = c3;
    DSW(0x0658) = c1;
    text_shadow(s, tx, (s16)(RY + 4));
}

/* ---- 6396: alrtlib2.c ----------------------------------------------------------------------------- */

/* AP 6396:0000 box_load(far items, name, n): n x {x, y, w, h} from name, n x 4 bytes from name.bl */
void box_load(FarPtr items, u16 name_ds, s16 n)
{
    u16 m = stk_mark();
    u16 rec = stk_alloc(8), bl = stk_alloc(4);
    u16 f = my_fopen(name_ds, 0x05EB /* "rb" */);
    if (f == 0) assert_fail(far_make(SEG(0x396E), 0), 0x2C, name_ds);
    FarPtr it = items;
    for (s16 i = 0; i < n; i++) {
        my_fread(rec, 8, 1, f);
        wr16(it.seg, it.off, DSW(rec));
        wr16(it.seg, (u16)(it.off + 2), DSW(rec + 2));
        wr16(it.seg, (u16)(it.off + 4), DSW(rec + 4));
        wr16(it.seg, (u16)(it.off + 6), DSW(rec + 6));
        it.off = (u16)(it.off + 0x16);
    }
    my_fclose(f);
    f = my_fopen(make_filename(name_ds, 0x0C74 /* "bl" */), 0x05EB);
    if (f != 0) {
        it = items;
        for (s16 i = 0; i < n; i++) {
            my_fread(bl, 1, 4, f);
            rtl__fmemcpy(far_make(it.seg, (u16)(it.off + 0x12)), ds_ptr(bl), 4);
            it.off = (u16)(it.off + 0x16);
        }
        my_fclose(f);
    }
    stk_release(m);
}

/* AP 6396:00ff clear_26c3(it) — callback: C1h fill */
void clear_26c3(void)
{
    fill_c1();
}

/* AP 6396:0109 hline(it) — callback: C1h fill one pixel inside */
void hline(void)
{
    ap_rect_shrink(1, 0);
    fill_c1();
    rect_grow(1);
}

/* AP 6396:0127 vline(x0, y0, x1, y1) */
void vline(s16 x0, s16 y0, s16 x1, s16 y1)
{
    gfx_line(x0, y0, x1, y1);
}

/* AP 6396:0140 fill_c1_item(x, y, w, h) */
void fill_c1_item(s16 x, s16 y, s16 w, s16 h)
{
    gfx_rect(x, y, w, h);
}

/* AP 6396:0159 item_center(colour): fill on, fill and line colour. The one word argument is the
 * colour, passed in the FarPtr's offset (the header keeps the FarPtr form video's banner uses) */
void item_center(FarPtr col)
{
    DSB(0x26CC) = 1;
    DSB(0x26CD) = (u8)col.off;
    DSB(0x26CE) = (u8)col.off;
}

/* AP 6396:016c arrow_button(down) */
void arrow_button(u8 down)
{
    rect_save();
    RW = (s16)(RX + RW - 1);
    RH = (s16)(RY + RH - 1);
    DSB(0x26CE) = down ? 0xC6 : 0xC9;
    gfx_line(RX, RY, RW, RY);
    DSB(0x26CE) = 0xC8;
    gfx_line(RX, RY, RX, RH);
    gfx_line(RW, RY, RW, RH);
    DSB(0x26CE) = down ? 0xC9 : 0xC6;
    gfx_line(RX, RH, RW, RH);
    ap_rect_restore(0);
    ap_rect_shrink(1, 0);
    DSB(0x26CC) = 1;
    DSB(0x26CD) = down ? 0xC7 : 0xC8;
    DSB(0x26CE) = DSB(0x26CD);
    gfx_rect(RX, RY, RW, RH);
    rect_grow(1);
}

/* AP 6396:0261 radio_button() — pressed look */
void radio_button(void)
{
    u16 c = panel_cb_0000() ? 0xC6 : 0xC7;
    arrow_button(1);
    gfx_font_select(DSS(0x3F2C));
    FarPtr it = CUR_ITEM();
    s16 tx = ap_text_center_x(IT_W(it, 0x0E), RX, RW);
    text_engrave(IT_W(CUR_ITEM(), 0x0E), 0xC4, 0xC9, c, (u16)tx, (u16)(RY + 2));
}

/* AP 6396:02cb check_button() — released look */
void check_button(void)
{
    u16 c = panel_cb_0000() ? 0xC7 : 0xC8;
    arrow_button(0);
    gfx_font_select(DSS(0x3F2C));
    FarPtr it = CUR_ITEM();
    s16 tx = ap_text_center_x(IT_W(it, 0x0E), RX, RW);
    text_engrave(IT_W(CUR_ITEM(), 0x0E), 0xC4, 0xC9, c, (u16)tx, (u16)(RY + 3));
}

/* AP 6396:0335 button_style_a(it) -> 1 when released inside */
u16 button_style_a(FarPtr it)
{
    (void)it;
    if ((STATE & 0x0C) == 0) check_button();
    else radio_button();
    if (STATE & 0x10) {
        sx_play(0x0F, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* AP 6396:036a button_style_b(it) -> 1 when released inside */
u16 button_style_b(FarPtr it)
{
    (void)it;
    if ((STATE & 0x0C) == 0) button_down_a(far_make(0, 0));
    else button_up_a(far_make(0, 0));
    if (STATE & 0x10) {
        sx_play(0x0F, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* AP 6396:03a1 lib2_1a40(i): one edge of the diamond DS:5152/514A */
void lib2_1a40(s16 i)
{
    s16 j = (s16)((i + 1) & 3);
    gfx_line(DSS(0x5152 + 2 * i), DSS(0x514A + 2 * i), DSS(0x5152 + 2 * j), DSS(0x514A + 2 * j));
}

/* AP 6396:03d7 lib2_1a76(colour, first): the filled diamond with shaded edges */
void lib2_1a76(u8 col, u16 first)
{
    DSW(0x5152) = (u16)(RX + (RW >> 1));
    DSW(0x5158) = (u16)RX;
    DSW(0x5154) = (u16)(RX + RW - 1);
    DSW(0x514C) = (u16)(RY + (RW >> 1));
    DSW(0x514A) = (u16)RY;
    DSW(0x514E) = (u16)(RY + RH - 1);
    DSB(0x26CC) = 1;
    DSB(0x26CD) = col;
    DSB(0x26CE) = col;
    DSW(0x5150) = DSW(0x514C);
    DSW(0x5156) = DSW(0x5152);
    gfx_polygon(4, 0x5152, 0x514A);
    for (u16 k = 0; (s16)k < 4; k++, first++) {
        DSB(0x26CE) = (k & 2) ? 0xC6 : 0xC9;
        lib2_1a40((s16)(first & 3));
    }
}

/* AP 6396:045e lib2_frame(dx, pressed): the diamond button's label */
void lib2_frame(s16 dx, u8 pressed)
{
    u16 c = pressed ? 200 : 199;
    s16 x = (s16)(RX + dx + 4);
    s16 y = (s16)(RY + pressed + 4);
    if (panel_cb_0000()) c = pressed ? 0xC7 : 0xC6;
    gfx_font_select(DSS(0x3F2C));
    text_engrave(IT_W(CUR_ITEM(), 0x0E), 0xC4, 0xC9, c, (u16)x, (u16)y);
}

/* AP 6396:04fa lib2_cb_a() — diamond up */
void lib2_cb_a(void)
{
    lib2_1a76(0xC7, 1);
    lib2_frame((s16)(IT_W(CUR_ITEM(), 0x10) & 3), 0);
}

/* AP 6396:0521 lib2_cb_b() — diamond down */
void lib2_cb_b(void)
{
    lib2_1a76(0xC8, 3);
    lib2_frame((s16)(IT_W(CUR_ITEM(), 0x10) & 3), 1);
}

/* AP 6396:0548 lib2_cb_c(it) -> enabled: the diamond button by its state */
u16 lib2_cb_c(FarPtr it)
{
    (void)it;
    u16 r = panel_cb_0000();
    if ((STATE & 4) == 0) lib2_cb_a();
    else lib2_cb_b();
    return r;
}

__attribute__((constructor)) static void dialogs_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x52470000u, (CodeFn)dlg_title_text);
    codeptr_register(PROG_AP, 0x52470070u, (CodeFn)dlg_title_shadow);
    codeptr_register(PROG_AP, 0x639600FFu, (CodeFn)clear_26c3);
    codeptr_register(PROG_AP, 0x63960109u, (CodeFn)hline);
    codeptr_register(PROG_AP, 0x63960335u, (CodeFn)button_style_a);
    codeptr_register(PROG_AP, 0x6396036Au, (CodeFn)button_style_b);
    codeptr_register(PROG_AP, 0x63960548u, (CodeFn)lib2_cb_c);
    codeptr_register(PROG_AP, 0x51190000u, (CodeFn)textbox_init);
}
