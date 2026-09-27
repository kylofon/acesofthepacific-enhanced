/* shell_ui: self-sizing text boxes (PS segment 1305, screen 56 TEXTBOX, table 4bef:0000, 2 items) —
 * shell_ui_mission.md §2. */
#include "shell_ui/sui_a_priv.h"

FarPtr display_subst(s8 c);              /* subst.c (0cfe:0057) */

#define TBL_TBOX   fd(0x4BEF, 0x0000)
#define TB_WAIT    0x0E18                /* s16: ticks for wait_ticks_or_key after drawing */
#define TB_TITLE   0x0E1A                /* near char * */
#define TB_TEXT    0x0E1C                /* near char * */
#define TB_RY      0x4D44                /* the box: y, x, h, w */
#define TB_RX      0x4D46
#define TB_RH      0x4D48
#define TB_RW      0x4D4A
#define TB_FONT    0x4D4C                /* text font */
#define TB_TFONT   0x4D4E                /* title font */

/* PS 1305:0008 tbox_fit(text, &x, &y, &w, &h, pad_w, pad_h, line_h, subst): grows the box to the text */
static void tbox_fit(FarPtr text, u16 px, u16 py, u16 pw, u16 ph, s16 pad_w, s16 pad_h, s16 line_h, FarPtr subst)
{
    s16 w0 = (s16)(DSS(pw) - pad_w);
    s16 h0 = (s16)(DSS(ph) - pad_h);
    s32 aspect = (s32)((u32)(u16)w0 << 16) / (s32)h0;
    RXs = DSS(px);
    RYs = DSS(py);
    RWs = w0;
    RHs = h0;
    flow_setup(0, (u16)line_h, 0);
    for (;;) {
        s16 m = (s8)flow_text(text, 1, subst);
        if (m <= RHs) break;
        RHs = (s16)isqrt32((u32)((s32)m * (s32)h0));
        RWs = (s16)((u32)(aspect * (s32)RHs) >> 16);
        RHs = (s16)(RHs + 5);
        RWs = (s16)(RWs + 8);
    }
    DSS(pw) = (s16)(RWs + pad_w);
    DSS(ph) = (s16)(RHs + pad_h);
    RXs = (s16)(RXs - ((s16)(RWs - w0) >> 1));
    RYs = (s16)(RYs - ((s16)(RHs - h0) >> 1));
    DSS(px) = RXs < 0 ? 0 : RXs;
    DSS(py) = RYs < 0 ? 0 : RYs;
    if ((s16)(DSS(px) + DSS(pw)) > 0x13E) DSS(px) = (s16)(0x13E - DSS(pw));
    if ((s16)(DSS(py) + DSS(ph)) > 0xC6) DSS(py) = (s16)(0xC6 - DSS(ph));
}

/* PS 1305:0154 tbox_draw */
void tbox_draw(void)
{
    s16 th = fs(TBL_TBOX, 0x1C);
    gfx_font_select((s16)DSW(TB_FONT));
    tbox_fit(ds_ptr(DSW(TB_TEXT)), RX, RY, RW, RH, 22, (s16)(th + 8), 7, FNP(display_subst));
    DSW(TB_RX) = DSW(RX);
    DSW(TB_RY) = DSW(RY);
    DSW(TB_RW) = DSW(RW);
    DSW(TB_RH) = DSW(RH);
    tile_fill_rect();
    ps_draw_frame();
    RHs = (s16)(RHs - (th + 8));
    RYs = (s16)(RYs + th + 4);
    RXs = (s16)(RXs + 11);
    RWs = (s16)(RWs - 22);
    DSB(TXT_FLAGS) = 1;
    DSW(PG_READ) = DSW(PG_BACK);
    flow_setup(3, 7, 1);
    flow_text(ds_ptr(DSW(TB_TEXT)), 1, FNP(display_subst));
}

/* PS 1305:0222 tbox_draw_title */
void tbox_draw_title(void)
{
    RXs = DSS(TB_RX);
    RYs = DSS(TB_RY);
    RWs = DSS(TB_RW);
    draw_bevel_box();
    for (s16 ry = (s16)(RYs + 5); ry <= (s16)(RYs + DSS(TB_RH) - 6); ry = (s16)(ry + 7)) {
        draw_rivets((u16)(RXs + 4), (u16)ry, 1);
        draw_rivets((u16)(RXs + RWs - 7), (u16)ry, 1);
    }
    gfx_font_select((s16)DSW(TB_TFONT));
    ps_text_emboss(DSW(TB_TITLE), 0xFFFF, (u16)(RYs + 4), 0xC1, 0xC0, 0xB1);
}

/* PS 1305:02bd tbox_show */
static void tbox_show(void)
{
    pages_front_to_back();
    for (s8 i = 0; i < 2; i++) {
        hs_load(TBL_TBOX, (u16)(s16)i);
        FarPtr cb = fp(cur_item(), IT_CB);
        if (!far_is_null(cb)) call_far(cb);
    }
    hs_restart();
    cursor_hide();
    hs_present();
    cursor_show();
    wait_ticks_or_key((s16)DSW(TB_WAIT));
}

/* PS 1305:0323 tbox_set(title, text, title_font, text_font) */
static void tbox_set(FarPtr title, FarPtr text, u16 title_font, u16 text_font)
{
    DSW(TB_TFONT) = title_font;
    DSW(TB_FONT) = text_font;
    if (!far_is_null(text)) {
        DSW(TB_TEXT) = xmalloc((u16)(rtl__fstrlen(text) + 1));
        strcpy_far_src(DSW(TB_TEXT), text);
    }
    if (!far_is_null(title)) {
        DSW(TB_TITLE) = xmalloc((u16)(rtl__fstrlen(title) + 1));
        strcpy_far_src(DSW(TB_TITLE), title);
    }
}

/* PS 1305:0392 tbox_clear */
static void tbox_clear(void)
{
    xfree(DSW(TB_TEXT));
    DSW(TB_TEXT) = 0;
    xfree(DSW(TB_TITLE));
    DSW(TB_TITLE) = 0;
}

/* PS 1305:03b7 tbox_message(title, text, title_font, text_font) */
static void tbox_message(FarPtr title, FarPtr text, u16 title_font, u16 text_font)
{
    fww(TBL_TBOX, 0, 0x3A);
    fww(TBL_TBOX, 2, 0x29);
    tbox_set(title, text, title_font, text_font);
    tbox_show();
    tbox_clear();
}

/* PS 1305:03f2 tbox_end_of_campaign(text): the campaign's end date as the title */
void tbox_end_of_campaign(u16 a0, u16 a1)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(20);
    screen_show(56, 0xCA, 1);
    u16 d = campaign_end((s16)DSW(0x4790));
    date_text(d, buf);
    squad_open();
    tbox_message(ds_ptr(buf), far_make(a1, a0), DSW(T_FONT4), DSW(T_FONT3));
    squad_close();
    stk_release(m);
}

/* PS 1305:044e tbox_kia(text) */
void tbox_kia(FarPtr text)
{
    screen_show(56, 0xCA, 1);
    tbox_message(scd_string(1), text, DSW(T_FONT4), DSW(T_FONT3));
}

/* PS 1305:0487 tbox_inquiry(text) */
void tbox_inquiry(FarPtr text)
{
    screen_show(56, 0xCA, 1);
    tbox_message(scd_string(2), text, DSW(T_FONT4), DSW(T_FONT3));
}

/* PS 1305:04c0 tbox_titled3(text): title string 3 of the current screen's table */
void tbox_titled3(FarPtr text)
{
    tbox_message(scd_string(3), text, DSW(T_FONT4), DSW(T_FONT3));
}

/* PS 1305:04e6 tbox_over_screen(text): over the current screen, restored afterwards */
void tbox_over_screen(FarPtr text)
{
    ps_clip_full();
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), 0, 1);
    copy_back_to_bg();
    hs_push();
    tbox_message(scd_string(12), text, DSW(T_FONT4), DSW(T_FONT3));
    copy_bg_to_back();
    hs_pop();
    hs_focus(DSW(T_IDX));
}

/* PS 1305:0546 tbox_squadron_move(text) */
void tbox_squadron_move(FarPtr text)
{
    screen_show(56, 0xCA, 1);
    station_open();
    tbox_message(scd_string(3), text, DSW(T_FONT4), DSW(T_FONT3));
    station_close();
}

__attribute__((constructor)) static void textbox_codeptrs(void)
{
    REG(tbox_draw);
    REG(tbox_draw_title);
}
