/* shell_ui: the list box and its text field ("listbox.c", PS segment 2fa5, table 4cd4:0010) —
 * shell_ui_toolkit.md §6.3. */
#include "shell_ui/sui_a_priv.h"

#define TBL_LBOX   fd(0x4CD4, 0x0010)   /* 9 items */
#define ED_ALLOW_SP 0x6040   /* u8 */
#define ED_REDRAW   0x6041   /* u8 */
#define ED_MAX      0x6042   /* s8 */
#define ED_BUF      0x6043   /* char[21] */
#define ED_TIMER    0x6058   /* timer record */
#define ED_ITEM     0x603C   /* far */
#define LB_UPIDX    0x6064   /* u8 */
#define LB_BODY     0x6065   /* far */
#define LB_DRAW     0x6069   /* far: void draw(i, y) */
#define LB_FMT      0x606D   /* far: char *fmt(i) */
#define LB_COUNT    0x6071   /* s16 */
#define LB_MAXTOP   0x6073   /* s16 */
#define LB_VISIBLE  0x6075   /* s16 */
#define LB_SEL      0x6077   /* s16 */
#define LB_ROW      0x6079   /* s16 */

static u16 lb_fmt(s16 i) { return ((UiFmtFn)codeptr_lookup(ds_far(LB_FMT)))(i).off; }

/* PS 2fa5:000a lbox_stub */
void lbox_stub(void) { }

/* PS 2fa5:000f lbox_btn_cb: item 7 -> 2, item 6 -> 1 */
void lbox_btn_cb(void)
{
    if (btn_cb()) menu_set_result(DSW(T_IDX) == 7 ? 2 : 1);
}

/* PS 2fa5:0033 lbox_arrow_draw -> 1 on the initial press */
static u16 lbox_arrow_draw(void)
{
    if ((DSW(T_EVT) & 0x0C) == 0) {
        draw_list_button_up();
    } else {
        draw_list_button_down();
        if (DSW(T_EVT) & 4) {
            snd_play(0x18);
            return 1;
        }
    }
    return 0;
}

static void lbox_fill_edit(void);

/* PS 2fa5:006b lbox_arrow_cb: one step per press */
void lbox_arrow_cb(void)
{
    u16 pressed = lbox_arrow_draw();
    s16 row = DSS(LB_ROW), top = DSS(T_LB_TOP);
    if (!pressed) return;
    if (DSB(LB_UPIDX) == DSW(T_IDX)) {
        if (DSS(LB_ROW) < 1) {
            if (DSS(T_LB_TOP) > 0) DSS(T_LB_TOP)--;
        } else {
            DSS(LB_ROW)--;
        }
    } else if (DSS(LB_ROW) < (s16)(DSS(LB_VISIBLE) - 1)) {
        DSS(LB_ROW)++;
    } else if (DSS(T_LB_TOP) < DSS(LB_MAXTOP)) {
        DSS(T_LB_TOP)++;
    }
    if (top != DSS(T_LB_TOP) || row != DSS(LB_ROW)) {
        DSS(LB_SEL) = (s16)(DSS(T_LB_TOP) + DSS(LB_ROW));
        lbox_fill_edit();
        fbw(ds_far(LB_BODY), IT_MASK, 3);
    }
}

/* PS 2fa5:00f6 lbox_hit_row(row, n, pitch) */
static s16 lbox_hit_row(s16 row, s16 n, s16 pitch)
{
    s16 r = row;
    if (DSW(T_EVT) & 0x0C) {
        r = (s16)((s16)(DSS(T_PTR_Y) - (s16)(RYs + 1)) / pitch);
        if (r < 0) r = 0;
        else if (n <= r) r = (s16)(n - 1);
        if (DSW(T_EVT) & 4) snd_play(0x18);
        else if (row != r) snd_play(0x19);
    }
    return r;
}

/* PS 2fa5:0153 lbox_draw: the list body (6-pixel rows) */
void lbox_draw(void)
{
    s16 old = DSS(LB_ROW);
    DSS(LB_ROW) = lbox_hit_row(DSS(LB_ROW), DSS(LB_VISIBLE), 6);
    if (DSS(LB_ROW) != old) {
        DSS(LB_SEL) = (s16)(DSS(T_LB_TOP) + DSS(LB_ROW));
        lbox_fill_edit();
    }
    draw_inset_box2();
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FLAGS) = 1;
    s16 ry = (s16)(RYs + 2);
    for (s16 r = 0; r < DSS(LB_VISIBLE); r++, ry = (s16)(ry + 6)) {
        if (r == DSS(LB_ROW)) {
            DSB(FILL_ON) = 1;
            DSB(DRAW_COL) = 0xB9;
            DSB(FILL_COL) = 0xBE;
            gfx_rect((s16)(RXs + 2), (s16)(ry + 1), (s16)(RWs - 3), 7);
            DSB(TXT_FG) = 0xCD;
        } else {
            DSB(TXT_FG) = 0xCC;
        }
        if (far_is_null(ds_far(LB_DRAW))) {
            if (!far_is_null(ds_far(LB_FMT)))
                gfx_text(lb_fmt((s16)(r + DSS(T_LB_TOP))), (s16)(RXs + 5), (s16)(ry + 2));
        } else {
            ((UiRowFn)codeptr_lookup(ds_far(LB_DRAW)))((s16)(r + DSS(T_LB_TOP)), (s16)(ry + 2));
        }
    }
    fbw(cur_item(), IT_MASK, 0x5C);
}

/* PS 2fa5:0255 edit_set(s) -> ok */
static u16 edit_set(u16 s)
{
    s16 n = (s16)rtl_strlen(s), max = DSC(ED_MAX);
    if (n <= max) {
        rtl_strcpy(ED_BUF, s);
        DSB(T_ED_LEN) = (u8)n;
        DSB(T_ED_CUR) = (u8)n;
        DSB(ED_REDRAW) = 1;
        fbw(ds_far(ED_ITEM), IT_MASK, 3);
    }
    return n <= max;
}

/* PS 2fa5:02a3 lbox_fill_edit: the selected entry's text into the field */
static void lbox_fill_edit(void)
{
    if (DSS(LB_COUNT) > 0 && DSS(LB_ROW) >= 0 && !far_is_null(ds_far(LB_FMT))) edit_set(lb_fmt(DSS(LB_SEL)));
}

/* PS 2fa5:02ce edit_char_ok(c) */
static u16 edit_char_ok(s16 c)
{
    if ((DSB((u16)(c + 0x3CD1)) & 0x0E) == 0 && c != '_' && (c != ' ' || DSB(ED_ALLOW_SP) == 0)) return 0;
    return 1;
}

/* PS 2fa5:02f9 edit_key -> changed: editing keys (table 2fa5:04D9) and typed characters */
static u16 ps_edit_key(void)
{
    s8 cur = DSC(T_ED_CUR), len = DSC(T_ED_LEN), max = DSC(ED_MAX);
    if (len < cur) assert_line(AF_LISTBOX, 0x135);
    if (max < len) assert_line(AF_LISTBOX, 0x136);
    if (max > 20) assert_line(AF_LISTBOX, 0x137);
    if (rtl_memchr(0x2104, DSB(T_SCAN), 7)) {
        cur = DSC(T_ED_CUR);
        len = DSC(T_ED_LEN);
        switch (DSB(T_SCAN)) {
        case 0x0E:                                     /* Backspace */
            if (cur <= 0) return 0;
            rtl_memmove((u16)(0x6042 + cur), (u16)(ED_BUF + cur), (u16)(len - cur + 1));
            DSB(T_ED_LEN)--;
            DSB(T_ED_CUR)--;
            return 1;
        case 0x47: DSB(T_ED_CUR) = 0; return 1;        /* Home */
        case 0x4B:                                     /* Left */
            if (cur <= 0) return 0;
            DSB(T_ED_CUR)--;
            return 1;
        case 0x4D:                                     /* Right */
            if (cur >= len) return 0;
            DSB(T_ED_CUR) = (u8)(cur + 1);
            return 1;
        case 0x4F: DSB(T_ED_CUR) = (u8)len; return 1;  /* End */
        case 0x52: DSB(T_ED_INS) ^= 1; return 0;       /* Insert */
        case 0x53:                                     /* Delete */
            if (len <= 0 || cur >= len) return 0;
            rtl_memmove((u16)(ED_BUF + cur), (u16)(0x6044 + cur), (u16)(len - cur));
            DSB(T_ED_LEN)--;
            return 1;
        default: return 0;
        }
    }
    if (!edit_char_ok(DSB(T_ASCII))) return 0;
    if (DSB(T_ED_INS)) {
        if (DSC(T_ED_LEN) >= DSC(ED_MAX)) return 0;
        cur = DSC(T_ED_CUR);
        rtl_memmove((u16)(0x6044 + cur), (u16)(ED_BUF + cur), (u16)(DSC(T_ED_LEN) - cur + 1));
        DSB(T_ED_LEN)++;
    } else {
        if (DSC(T_ED_CUR) >= DSC(ED_MAX)) return 0;
        if (DSC(T_ED_CUR) == DSC(T_ED_LEN)) DSB(T_ED_LEN)++;
    }
    DSB((u16)(ED_BUF + DSC(T_ED_CUR))) = DSB(T_ASCII);
    DSB(T_ED_CUR)++;
    return 1;
}

/* PS 2fa5:04f5 glyph_width(c) */
static u8 glyph_width(u8 c)
{
    if (c == 0) c = ' ';
    FarPtr ofs = ds_far(PS_DS_font_ofs);
    if (far_is_null(ofs)) return DSB(PS_DS_font_w);
    FarPtr wid = ds_far(PS_DS_font_wid);
    return rd8(wid.seg, (u16)(wid.off + (u16)(c - DSB(PS_DS_font_first))));
}

/* PS 2fa5:053c lbox_edit_draw: the text field with its caret */
void lbox_edit_draw(void)
{
    DSB((u16)(ED_BUF + DSC(T_ED_LEN))) = 0;
    gfx_font_select((s16)DSW(T_FONT3));
    u16 at = (u16)(ED_BUF + DSC(T_ED_CUR));
    u8 c = DSB(at);
    DSB(at) = 0;
    s16 cx = (s16)(RXs + gfx_text_width(ED_BUF) + 5);
    DSB(at) = c;
    if (DSB(ED_REDRAW) || (DSW(T_EVT) & 0x40)) {
        draw_inset_box2();
        DSB(TXT_FG) = 0xCC;
        DSB(TXT_FLAGS) = 1;
        gfx_text(ED_BUF, (s16)(RXs + 5), (s16)(RYs + 3));
    }
    DSB(DRAW_COL) = DSB(T_ED_BLINK) == 0 ? 0 : 0xCC;
    s16 ly = (s16)(RYs + 8);
    gfx_line(cx, ly, (s16)(cx + glyph_width(c) - 1), ly);
    DSB(ED_REDRAW) = 1;
    fbw(cur_item(), IT_MASK, 0x40);
}

/* PS 2fa5:0612 edit_poll: keys, caret blink (59 ticks) */
void edit_poll(void)
{
    if (rtl_memchr(0x210B, DSB(T_SCAN), 10)) DSB(T_ASCII) = 0;
    if (ps_edit_key()) {
        DSB(ED_REDRAW) = 1;
        fbw(ds_far(ED_ITEM), IT_MASK, 3);
    }
    if (tmr_expired(ED_TIMER)) {
        tmr_set(ED_TIMER, 0x3B);
        DSB(T_ED_BLINK) ^= 1;
        DSB(ED_REDRAW) = 1;
    }
    if (DSB(ED_REDRAW)) fbw(ds_far(ED_ITEM), IT_MASK, 3);
}

/* PS 2fa5:0687 lbox_draw_window */
void lbox_draw_window(void)
{
    tile_fill_rect();
    ps_draw_frame();
}

/* PS 2fa5:0696 lbox_draw_title */
void lbox_draw_title(void)
{
    u16 l = fw(cur_item(), IT_LABEL);
    if (l) {
        gfx_font_select((s16)DSW(T_FONT3));
        DSB(TXT_FLAGS) = 3;
        text_engrave(l, 0xC7, 0xB1, 0xC0, (u16)RXs, (u16)RYs);
    }
}

/* PS 2fa5:06dd edit_bind(tbl, idx) */
void edit_bind(FarPtr p, u16 idx) { ds_far_wr(ED_ITEM, far_add(p, (u16)(idx * IT_SIZE))); }

/* PS 2fa5:06fa edit_setup(init, max, allow_space) */
void edit_setup(u16 init_ds, u16 max, u16 allow_space)
{
    if ((s16)max > 20) assert_line(AF_LISTBOX, 0x23D);
    ds_far_wr(ED_ITEM, fd(0x4CD4, 0x94));
    DSB(ED_MAX) = (u8)max;
    DSB(ED_ALLOW_SP) = (u8)allow_space;
    tmr_set(ED_TIMER, 0x3B);
    if (init_ds) edit_set(init_ds);
}

/* PS 2fa5:0761 lbox_bind(tbl, idx, up_idx) */
void lbox_bind(FarPtr p, u16 idx, u16 up_idx)
{
    ds_far_wr(LB_BODY, far_add(p, (u16)(idx * IT_SIZE)));
    DSB(LB_UPIDX) = (u8)up_idx;
}

/* PS 2fa5:0784 lbox_select(i, row) */
void lbox_select(u16 i_, u16 row_)
{
    s16 i = (s16)i_, row = (s16)row_;
    if (i < 0 || DSS(LB_COUNT) <= i) assert_line(AF_LISTBOX, 0x253);
    if (row < 0) assert_line(AF_LISTBOX, 0x254);
    if (DSS(LB_VISIBLE) <= row) row = (s16)(DSS(LB_VISIBLE) - 1);
    DSS(LB_SEL) = i;
    if (i < row) {
        DSS(LB_ROW) = i;
        DSS(T_LB_TOP) = 0;
    } else {
        DSS(T_LB_TOP) = (s16)(i - row);
        DSS(LB_ROW) = row;
        if (DSS(LB_MAXTOP) < DSS(T_LB_TOP)) {
            DSS(T_LB_TOP) = DSS(LB_MAXTOP);
            DSS(LB_ROW) = (s16)(i - DSS(LB_MAXTOP));
        }
    }
}

/* PS 2fa5:082c lbox_setup(title, ok_label, n, visible, fmt, draw) */
void lbox_setup(u16 title, u16 a1, u16 n, u16 visible, FarPtr fmt, FarPtr draw)
{
    ds_far_wr(LB_BODY, fd(0x4CD4, 0x3C));
    DSB(LB_UPIDX) = 4;
    fww(fd(0x4CD4, 0), 0xCE, title);
    fww(fd(0x4CD4, 0), 0x34, a1);
    DSW(LB_COUNT) = n;
    ds_far_wr(LB_FMT, fmt);
    ds_far_wr(LB_DRAW, draw);
    DSW(LB_ROW) = 0;
    DSW(LB_SEL) = 0;
    DSW(T_LB_TOP) = 0;
    DSS(LB_MAXTOP) = (s16)((s16)n - (s16)visible);
    if (DSS(LB_MAXTOP) < 0) DSS(LB_MAXTOP) = 0;
    DSS(LB_VISIBLE) = (s16)((s16)n - DSS(LB_MAXTOP));
    edit_setup(0, 15, 0);
    lbox_fill_edit();
}

/* PS 2fa5:08bd lbox_frame */
void lbox_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
    edit_poll();
}

/* PS 2fa5:08e2 edit_text */
u16 edit_text(void) { return ED_BUF; }

/* PS 2fa5:08ec lbox_selected */
u16 lbox_selected(void) { return DSW(LB_SEL); }

/* PS 2fa5:08f6 lbox_run */
void lbox_run(void)
{
    dlg_run(TBL_LBOX, 9, FNP(lbox_frame));
    hs_draw_all();
}

/* PS 2fa5:091c lbox_init */
void lbox_init(void) { hs_default_links(TBL_LBOX, 9); }

__attribute__((constructor)) static void listbox_codeptrs(void)
{
    REG(lbox_stub);
    REG(lbox_btn_cb);
    REG(lbox_arrow_cb);
    REG(lbox_draw);
    REG(lbox_edit_draw);
    REG(edit_poll);
    REG(lbox_draw_window);
    REG(lbox_draw_title);
    REG(lbox_frame);
}
