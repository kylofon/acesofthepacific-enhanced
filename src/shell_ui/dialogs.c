/* shell_ui: the toolkit dialogs — soft.c (PS 2ed6: CHOOSE list and the dialog runners), choose.c
 * (2f47: list picker), text entry (2f7f), request box (3038), fltalert.c (307d: flight roster) —
 * shell_ui_toolkit.md §6. The hot-spot tables are far data of the image (4cb0, 4cc3, 4cce, 4ce2,
 * 4ce8); their callbacks are the relocated code pointers stored there. */
#include "shell_ui/sui_a_priv.h"

#define TBL_CHOOSE  fd(0x4CB0, 0x000D)   /* 13 items */
#define TBL_PICK    fd(0x4CC3, 0x000F)   /* 7 items */
#define TBL_ENTRY   fd(0x4CCE, 0x0000)   /* 4 items */
#define TBL_REQ     fd(0x4CE2, 0x0000)   /* 4 items */
#define TBL_FLT     fd(0x4CE8, 0x0011)   /* 7 template items */
#define TBL_FLT_RUN fd(0x4CE8, 0x00AB)   /* the run-time copy */

#define CH_OK       0x6026   /* far: chooser "on ok" callback */
#define CH_BASE_H   0x602A   /* s16 */
#define CH_PITCH    0x602C   /* s16 */
#define CH_CHOICE   0x602E   /* s16 */
#define CH_COUNT    0x6030   /* s16 */
#define CH_PICKED   0x6032   /* s8 */
#define PK_OK       0x6034   /* far */
#define PK_DH       0x6038   /* s16 */
#define EN_BUF      0x603A   /* near char * */
#define RQ_NSTR     0x607C   /* u8 */
#define RQ_STR      0x607D   /* u16 */
#define RQ_YES      0x607F   /* u8 */
#define RQ_SUBST    0x6080   /* far */
#define FL_ENEMY    0x6084   /* u8 */
#define FL_SIZE     0x6085   /* s8 */

/* ======================================================================== soft.c (2ed6) */

/* PS 2ed6:000d choose_draw_title */
void choose_draw_title(void)
{
    gfx_font_select((s16)DSW(T_FONT4));
    s16 tx = (s16)ps_text_center_x(0x20C3 /* "CHOOSE" */, (u16)RXs, (u16)RWs);
    s16 tw = gfx_text_width(0x20C3);
    text_engrave_center(0x20C3, 0xC1, 0xB1, 0xC0);
    draw_rivets((u16)(tx - 7), (u16)(RYs + 4), 2);
    draw_rivets((u16)(tx - 15), (u16)(RYs + 11), 1);
    draw_rivets((u16)(tx + tw + 3), (u16)(RYs + 4), 2);
    draw_rivets((u16)(tx + tw + 11), (u16)(RYs + 11), 1);
}

/* PS 2ed6:00c4 choose_draw_entry(pressed) */
static void choose_draw_entry(u8 pressed)
{
    gfx_font_select((s16)DSW(T_FONT3));
    u16 l = fw(cur_item(), IT_LABEL);
    u16 tx = ps_text_center_x(l, (u16)RXs, (u16)RWs);
    line_c(pressed == 0 ? 0xB3 : 0xC7, RXs, RYs, (s16)(RXs + RWs), RYs);
    line_c(0xC0, RXs, RYs, RXs, (s16)(RYs + RHs));
    gfx_line((s16)(RXs + RWs), RYs, (s16)(RXs + RWs), (s16)(RYs + RHs));
    line_c(pressed == 0 ? 0xC7 : 0xB3, RXs, (s16)(RYs + RHs), (s16)(RXs + RWs), (s16)(RYs + RHs));
    box_c(0xB8, (s16)(RXs + 1), (s16)(RYs + 1), (s16)(RWs - 1), (s16)(RHs - 1));
    text_engrave(l, 0xB3, 0xC7, 0xC0, tx, (u16)(pressed == 0 ? RYs + 2 : RYs + 3));
}

/* PS 2ed6:01f2 choose_entry_cb -> activated */
u16 choose_entry_cb(void)
{
    if ((DSW(T_EVT) & 0x0C) == 0) {
        choose_draw_entry(0);
    } else {
        choose_draw_entry(1);
        snd_play(0x18);
    }
    return (DSW(T_EVT) & 0x10) != 0;
}

/* PS 2ed6:022b choose_frame */
void choose_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 2ed6:024c choose_click */
void choose_click(void)
{
    DSS(CH_CHOICE) = (s16)(DSS(T_IDX) - 2);
    if (choose_entry_cb()) menu_set_result(DSS(CH_CHOICE) < DSS(CH_COUNT) ? 1 : 2);
}

/* PS 2ed6:027a choose_layout(title, labels[], cb) */
static void choose_layout(u16 title, const u16 *labels, FarPtr cb)
{
    FarPtr t = fd(0x4CB0, 0);
    fww(t, 0x1B, title);                              /* item 1 label */
    s16 maxw = gfx_text_width(title);
    gfx_font_select((s16)DSW(T_FONT3));
    s16 k;
    for (k = 0; k < DSS(CH_COUNT); k++) {
        fpw(t, (u16)(k * IT_SIZE + 0x41), cb);
        fww(t, (u16)(k * IT_SIZE + 0x47), labels[k]);
        s16 w = gfx_text_width(labels[k]);
        if (maxw < w) maxw = w;
    }
    k = DSS(CH_COUNT);
    fpw(t, (u16)(k * IT_SIZE + 0x41), cb);
    fww(t, (u16)(k * IT_SIZE + 0x47), 0x20BC /* "CANCEL" */);
    s16 w = gfx_text_width(0x20BC);
    if (maxw < w) maxw = w;
    fww(t, 0x13, (u16)(DSS(CH_BASE_H) + DSS(CH_PITCH) * DSS(CH_COUNT)));
    fww(t, 0x11, (u16)(maxw + 60));
    fww(t, 0x0D, (u16)(160 - (fs(t, 0x11) >> 1)));
    s16 ex = (s16)(160 - ((s16)(maxw + 30) >> 1) - fs(t, 0x0D));
    for (k = 1; k < 13; k++) {
        fww(t, (u16)(k * IT_SIZE + 0x0D), (u16)ex);
        fww(t, (u16)(k * IT_SIZE + 0x11), (u16)(maxw + 30));
    }
    DSB(CH_PICKED) = 0xFF;
}

/* PS 2ed6:03c9 choose_on_ok */
void choose_on_ok(void)
{
    FarPtr cb = ds_far(CH_OK);
    if (far_is_null(cb)) return;
    s16 v = DSC(CH_PICKED) != -1 ? DSC(CH_PICKED) : DSS(CH_CHOICE);
    ((UiOkFn)codeptr_lookup(cb))(v);
}

/* PS 2ed6:03ee choose_run(cb, first_str, scd_row): the CHOOSE list of the calling item's count */
void choose_run(FarPtr cb, u16 first_str, u16 scd_row)
{
    u16 labels[10];
    DSS(CH_COUNT) = fs(cur_item(), IT_USER);
    if (DSS(CH_COUNT) > 10) assert_line(AF_SOFT, 0x122);
    for (s16 i = 0; i < DSS(CH_COUNT); i++) {
        FarPtr s = strings_get(0, (s16)(scd_row + i));
        u16 n = (u16)(rtl__fstrlen(s) + 1);
        labels[i] = xmalloc(n);
        strcpy_far_src(labels[i], s);
    }
    s8 items = (s8)(13 - (10 - (s8)DSS(CH_COUNT)));
    ds_far_wr(CH_OK, cb);
    DSW(CH_CHOICE) = first_str;
    choose_layout(0x20CA /* "CHOOSE" */, labels, FNP(choose_click));
    dlg_modal(TBL_CHOOSE, items, (s16)(DSS(CH_CHOICE) + 2), FNP(choose_frame), FNP(choose_on_ok));
    for (s16 i = 0; i < DSS(CH_COUNT); i++) xfree(labels[i]);
}

/* PS 2ed6:050e choose_init */
void choose_init(void)
{
    FarPtr t = fd(0x4CB0, 0);
    hs_default_links(TBL_CHOOSE, 13);
    DSS(CH_PITCH) = (s16)(fs(t, 0x51) - fs(t, 0x3B));
    DSS(CH_BASE_H) = (s16)(fs(t, 0x13) + (fs(t, 0x51) - fs(t, 0x3B)) * -10);
}

/* PS 2ed6:0552 dlg_run(tbl, n, frame) -> result: a modal table over a saved copy of the screen */
u16 dlg_run(FarPtr p, u16 n, FarPtr frame)
{
    copy_back_to_bg();
    hs_push();
    hs_default_links(p, n);
    hs_set_table(p, n);
    hs_draw_all();
    u8 r = (u8)menu_run(frame);
    hs_pop();
    req_close();
    copy_bg_to_back();
    hs_draw_all();
    hs_pop();
    return r;
}

/* PS 2ed6:05bc dlg_open(tbl, n, focus, links) */
void dlg_open(FarPtr tbl, s16 n, s16 focus, u8 links)
{
    copy_back_to_bg();
    hs_push();
    if (links) hs_default_links(tbl, (u16)n);
    hs_set_table(tbl, (u16)n);
    hs_focus((u16)focus);
    hs_draw_all();
}

/* PS 2ed6:0602 dlg_close */
void dlg_close(void)
{
    copy_bg_to_back();
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 1);
    hs_pop();
    hs_focus(DSW(T_IDX));
    cursor_set(parts(6));
    mouse_move_to(DSS(T_PTR_X), DSS(T_PTR_Y));
    hs_present();
}

/* PS 2ed6:0654 dlg_modal(tbl, n, focus, frame, on_ok) -> result (byte) */
u16 dlg_modal(FarPtr tbl, s16 n, s16 focus, FarPtr frame, FarPtr on_ok)
{
    dlg_open(tbl, n, focus, 1);
    u8 r = (u8)menu_run(frame);
    hs_pop();
    if (r == 1 && !far_is_null(on_ok)) call_far(on_ok);
    dlg_close();
    return r;
}

/* PS 2ed6:06a3 dlg_modal2(tbl, n, frame, on_ok) -> result (byte) */
u16 dlg_modal2(FarPtr tbl, s16 n, FarPtr frame, FarPtr on_ok)
{
    copy_back_to_bg();
    hs_push();
    hs_default_links(tbl, (u16)n);
    hs_set_table(tbl, (u16)n);
    hs_draw_all();
    u8 r = (u8)menu_run(frame);
    hs_pop();
    copy_bg_to_back();
    hs_draw_all();
    hs_pop();
    if (r == 1 && !far_is_null(on_ok)) call_far(on_ok);
    return r;
}

/* ======================================================================== choose.c (2f47) */

/* PS 2f47:0009 pick_btn_cb */
void pick_btn_cb(void)
{
    if (btn_small_cb(0)) menu_set_result(DSW(T_IDX) == 6 ? 1 : 2);
}

/* PS 2f47:0031 pick_draw_window */
void pick_draw_window(void)
{
    draw_side_riveted_panel();
    u16 l = fw(cur_item(), IT_LABEL);
    if (l) {
        gfx_font_select((s16)DSW(T_FONT3));
        DSB(TXT_FLAGS) = 3;
        text_engrave(l, 0xC7, 0xB1, 0xC0, (u16)(RXs + 12), (u16)(RYs + 3));
    }
}

/* PS 2f47:0083 pick_frame */
void pick_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 2f47:00a4 pick_resize(h) */
void pick_resize(s16 h)
{
    FarPtr t = fd(0x4CC3, 0);
    DSS(PK_DH) = (s16)(h - fs(t, 0x29));
    fww(t, 0x13, (u16)(fs(t, 0x13) + DSS(PK_DH)));
    fww(t, 0x29, (u16)(fs(t, 0x29) + DSS(PK_DH)));
    for (DSW(T_IDX) = 2; DSS(T_IDX) < 7; DSW(T_IDX)++)
        if (DSS(T_IDX) != 5) fww(t, (u16)(DSS(T_IDX) * IT_SIZE + 0x0F), (u16)(fs(t, (u16)(DSS(T_IDX) * IT_SIZE + 0x0F)) + DSS(PK_DH)));
    fww(t, 0x0F, (u16)(160 - (fs(t, 0x13) >> 1)));
}

/* PS 2f47:0113 pick_unresize */
void pick_unresize(void)
{
    FarPtr t = fd(0x4CC3, 0);
    fww(t, 0x13, (u16)(fs(t, 0x13) - DSS(PK_DH)));
    fww(t, 0x29, (u16)(fs(t, 0x29) - DSS(PK_DH)));
    for (DSW(T_IDX) = 2; DSS(T_IDX) < 7; DSW(T_IDX)++)
        if (DSS(T_IDX) != 5) fww(t, (u16)(DSS(T_IDX) * IT_SIZE + 0x0F), (u16)(fs(t, (u16)(DSS(T_IDX) * IT_SIZE + 0x0F)) - DSS(PK_DH)));
    fww(t, 0x0F, (u16)(160 - (fs(t, 0x13) >> 1)));
}

/* PS 2f47:0178 pick_on_ok */
void pick_on_ok(void)
{
    FarPtr cb = ds_far(PK_OK);
    if (!far_is_null(cb)) ((UiOkFn)codeptr_lookup(cb))((s16)lbox_selected());
}

static void pick_run(u16 title, s16 n, s16 sel, FarPtr fmt, FarPtr on_ok)
{
    fww(fd(0x4CC3, 0), 0x1D, title);
    lbox_setup(0, 0, (u16)n, 5, fmt, fnul());
    lbox_bind(TBL_PICK, 1, 3);
    lbox_select((u16)sel, 2);
    ds_far_wr(PK_OK, on_ok);
    dlg_modal(TBL_PICK, 7, 6, FNP(pick_frame), FNP(pick_on_ok));
}

/* PS 2f47:0191 pick_list(title, n, sel, fmt, on_ok) */
void pick_list(u16 title, s8 n, s8 sel, FarPtr fmt, FarPtr on_ok)
{
    if (n < 1) assert_line(AF_CHOOSE, 0x89);
    pick_run(title, n, sel, fmt, on_ok);
}

/* PS 2f47:0249 pick_aircraft(title, on_ok, n, a, b, sel): aircraft names (2777:06fd filter, 2777:0741) */
void pick_aircraft(u16 title, FarPtr on_ok, s16 n, s8 a, s8 b, s16 sel)
{
    fww(fd(0x4CC3, 0), 0x1D, title);
    acl_select(a, b);
    lbox_setup(0, 0, (u16)n, 5, FNP(acl_name), fnul());
    lbox_bind(TBL_PICK, 1, 3);
    lbox_select((u16)sel, 2);
    ds_far_wr(PK_OK, on_ok);
    dlg_modal(TBL_PICK, 7, 6, FNP(pick_frame), FNP(pick_on_ok));
}

/* PS 2f47:02e6 pick_list2(title, on_ok, n): ace names (2550:0c99) */
void pick_list2(u16 title, FarPtr on_ok, s16 n)
{
    pick_run(title, n, 0, FNP(acelist_row_name), on_ok);
}

/* PS 2f47:0374 pick_init */
void pick_init(void) { hs_default_links(TBL_PICK, 7); }

/* ======================================================================== text entry (2f7f) */

/* PS 2f7f:000d entry_btn_cb */
void entry_btn_cb(void)
{
    if (btn_small_cb(0)) menu_set_result(DSW(T_IDX) == 3 ? 1 : 2);
}

/* PS 2f7f:0035 entry_draw_window */
void entry_draw_window(void)
{
    tile_fill_rect();
    ps_draw_frame();
    u16 l = fw(cur_item(), IT_LABEL);
    if (l) {
        gfx_font_select((s16)DSW(T_FONT3));
        text_engrave(l, 0xC7, 0xB3, 0xC0, (u16)(RXs + 13), (u16)(RYs + 5));
    }
    draw_rivets((u16)(RXs + 5), (u16)(RYs + 5), 6);
    draw_rivets((u16)(RXs + RWs - 7), (u16)(RYs + 5), 6);
}

/* PS 2f7f:00bf entry_frame */
void entry_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
    edit_poll();
}

/* PS 2f7f:00e5 entry_trim(anon) */
static void entry_trim(u8 anon)
{
    u16 b = DSW(EN_BUF);
    s16 i = (s16)rtl_strlen(b);
    while (--i >= 0 && DSB((u16)(b + i)) == ' ') DSB((u16)(b + i)) = 0;
    if (DSB(b) == 0 && anon) rtl_strcpy(b, 0x20F2 /* "(Anonymous)" */);
}

/* PS 2f7f:012d entry_store */
static void entry_store(void) { rtl_strcpy(DSW(EN_BUF), edit_text()); }

/* PS 2f7f:0143 text_entry(prompt_id, buf, max, anon) -> accepted */
u16 text_entry(u16 prompt_id, u16 buf_ds, u16 max, u16 anon)
{
    FarPtr t = TBL_ENTRY;
    DSW(EN_BUF) = buf_ds;
    req_open(57 /* FIELD */, 0x43);
    gfx_font_select((s16)DSW(T_FONT3));
    u16 prompt = str_get((s16)prompt_id);
    fww(t, 0x0E, prompt);
    fww(t, 0x04, (u16)(gfx_text_width(prompt) + (s16)rtl_strlen(prompt) + 8));
    fww(t, 0x42, (u16)(fs(t, 0x04) - fs(t, 0x46) - fs(t, 0x2C)));
    fww(t, 0x16, (u16)((s16)(fs(t, 0x04) - fs(t, 0x1A)) / 2));
    edit_setup(DSW(EN_BUF), max, 1);
    edit_bind(t, 1);
    dlg_open(t, 4, 3, 0);
    DSB(T_SPACE_TYPES) = 1;
    u8 r = (u8)menu_run(FNP(entry_frame));
    DSB(T_SPACE_TYPES) = 0;
    req_close();
    hs_pop();
    if (r == 1) entry_store();
    entry_trim((u8)anon);
    dlg_close();
    return r == 1;
}

/* PS 2f7f:026a (empty init) */
void entry_init(void) { }

/* ======================================================================== request box (3038) */

/* PS 3038:0005 req_draw_text */
void req_draw_text(void)
{
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FLAGS) = 1;
    flow_setup(3, 8, 1);
    flow_text(scd_string((s16)DSW(RQ_STR)), (s8)DSB(RQ_NSTR), ds_far(RQ_SUBST));
}

/* PS 3038:004c req_btn_cb */
void req_btn_cb(void)
{
    if (btn_small_cb(0)) {
        DSB(RQ_YES) = DSW(T_IDX) == 3;
        menu_set_result(1);
    }
}

/* PS 3038:0079 req_frame */
void req_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 3038:009a req_open(screen_id, arg): push the string table, load the screen's second table */
void req_open(u16 screen_id, u16 arg)
{
    scd_push();
    scr_load_scd(screen_id, arg);
    DSB(T_REQ_OPEN) = 1;
}

/* PS 3038:00b6 req_close */
void req_close(void)
{
    if (DSB(T_REQ_OPEN)) {
        scd_pop();
        DSB(T_REQ_OPEN) = 0;
    }
}

/* PS 3038:00cc req_layout(subst, str_id, nstr, extra_h): measures the text, sizes the box */
void req_layout(FarPtr subst, u16 str_id, u8 nstr, s16 extra_h)
{
    FarPtr t = TBL_REQ;
    u16 save_idx = DSW(T_IDX);
    FarPtr save_tbl = ds_far(T_TBL);
    ds_far_wr(RQ_SUBST, subst);
    DSW(RQ_STR) = str_id;
    DSB(RQ_NSTR) = nstr;
    req_open(52 /* REQUEST */, 0x396);
    gfx_font_select((s16)DSW(T_FONT3));
    hs_load(t, 1);
    flow_setup(3, 8, 0);
    s8 h = (s8)flow_text(scd_string((s16)DSW(RQ_STR)), (s8)DSB(RQ_NSTR), ds_far(RQ_SUBST));
    fww(t, 0x06, (u16)(h + extra_h + 40));
    fww(t, 0x1C, (u16)(h + extra_h + 15));
    fww(t, 0x2E, (u16)(fs(t, 0x06) - fs(t, 0x32) - 6));
    fww(t, 0x44, fw(t, 0x2E));
    DSW(T_IDX) = save_idx;
    ds_far_wr(T_TBL, save_tbl);
}

/* PS 3038:01b9 req_ok(subst, str_id, btn_id): a message with one button */
void req_ok(u16 subst_off, u16 subst_seg, u16 str_id, u16 btn_id)
{
    FarPtr t = TBL_REQ;
    req_layout(far_make(subst_seg, subst_off), str_id, 1, 0);
    if ((s16)btn_id == -1) btn_id = 2;
    fww(t, 0x2C, 62);
    fww(t, 0x3A, btn_id);
    hs_default_links(t, 3);
    dlg_run(t, 3, FNP(req_frame));
    fww(t, 0x2C, 15);
}

/* PS 3038:0237 req_yes_no(subst, str_id, yes_id, no_id) -> yes */
u16 req_yes_no(u16 subst_off, u16 subst_seg, u16 str_id, u16 yes_id, u16 no_id)
{
    FarPtr t = TBL_REQ;
    req_layout(far_make(subst_seg, subst_off), str_id, 1, 0);
    fww(t, 0x3A, (s16)yes_id == -1 ? 3 : yes_id);
    fww(t, 0x50, (s16)no_id == -1 ? 5 : no_id);
    fww(t, 0x04, (u16)(fs(t, 0x04) - 0x23));
    fww(t, 0x1A, (u16)(fs(t, 0x1A) - 0x23));
    DSB(RQ_YES) = 0;
    hs_default_links(t, 4);
    dlg_run(t, 4, FNP(req_frame));
    fbw(t, 0x4F, 0x82);
    fbw(t, 0x39, 0x82);
    fww(t, 0x04, (u16)(fs(t, 0x04) + 0x23));
    fww(t, 0x1A, (u16)(fs(t, 0x1A) + 0x23));
    return DSB(RQ_YES);
}

/* PS 3038:02fe req_yes_no_wide(subst, str_id, yes_id, no_id) -> yes: wide buttons, 15 px higher */
u16 req_yes_no_wide(u16 subst_off, u16 subst_seg, u16 str_id, u16 yes_id, u16 no_id)
{
    FarPtr t = TBL_REQ;
    u16 s46 = fw(t, 0x46), s42 = fw(t, 0x42), s2c = fw(t, 0x2C);
    req_layout(far_make(subst_seg, subst_off), str_id, 1, 0);
    fww(t, 0x06, (u16)(fs(t, 0x06) + 15));
    fww(t, 0x1C, (u16)(fs(t, 0x1C) + 15));
    fww(t, 0x2C, 0x19);
    fww(t, 0x42, 0x19);
    fww(t, 0x30, 0x82);
    fww(t, 0x46, 0x82);
    fww(t, 0x2E, (u16)(fs(t, 0x2E) + 15));
    fww(t, 0x3A, (s16)yes_id == -1 ? 3 : yes_id);
    fww(t, 0x50, (s16)no_id == -1 ? 5 : no_id);
    DSB(RQ_YES) = 0;
    hs_default_links(t, 4);
    dlg_run(t, 4, FNP(req_frame));
    fww(t, 0x42, s42);
    fww(t, 0x2C, s2c);
    fww(t, 0x30, s46);
    fww(t, 0x46, s46);
    fbw(t, 0x4F, 0x82);
    fbw(t, 0x39, 0x82);
    return DSB(RQ_YES);
}

/* PS 3038:043f req_init */
void req_init(void) { hs_default_links(TBL_REQ, 4); }

/* ======================================================================== fltalert.c (307d) */

/* PS 307d:0008 flt_subst(c, item): %a = the flight size word, as an inline button */
FarPtr flt_subst(s8 c, FarPtr item)
{
    if (c == 'a') fww(item, IT_LABEL, DSW((u16)(DSC(FL_SIZE) * 2 + 0x2122)));
    return fnul();
}

/* PS 307d:002a flt_draw */
void flt_draw(void)
{
    u16 m = stk_mark();
    u16 strs = stk_alloc(4);
    gfx_font_select((s16)DSW(T_FONT3));
    draw_side_riveted_panel();
    if (DSB(FL_ENEMY) == 0) {
        DSW(strs) = 0x213F;                         /* "Your flight of %a includes:" */
        rtl_strcpy(T_STRBUF, 0x215B /* "Yourself" */);
        text_engrave(T_STRBUF, 0xC1, 0xB1, 0xC0, (u16)(RXs + 65), (u16)(RYs + 20));
    } else {
        DSW(strs) = 0x2164;                         /* "Enemy flight of %a includes:" */
    }
    DSW((u16)(strs + 2)) = 0;
    flow_text_buttons(TBL_FLT_RUN, 1, FNP(flt_subst), strs, (s16)(RXs + 10), (s16)(RYs + 6), (s16)(RXs + RWs));
    fbw(cur_item(), IT_MASK, 0x40);
    stk_release(m);
}

/* PS 307d:00cf flt_frame */
void flt_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 307d:00f0 flt_close_cb */
void flt_close_cb(void)
{
    if (choose_entry_cb()) {
        DSB(CH_PICKED) = 0xFE;
        menu_set_result(1);
    }
}

/* PS 307d:010d flt_pick_cb */
void flt_pick_cb(void)
{
    if (!btn_small_cb(1)) return;
    DSB(CH_PICKED) = (u8)(DSB(T_IDX) - 2);
    s16 last = DSB(FL_ENEMY) == 0 ? (s16)(DSC(FL_SIZE) - 1) : DSC(FL_SIZE);
    menu_set_result(DSC(CH_PICKED) >= last ? 2 : 1);
}

/* PS 307d:0159 flt_build(names) */
static void flt_build(const u16 *names)
{
    s16 nn, ncopy;
    if (DSB(FL_ENEMY) == 0) { ncopy = (s16)(6 - (4 - DSC(FL_SIZE))); nn = (s16)(DSC(FL_SIZE) - 1); }
    else { nn = DSC(FL_SIZE); ncopy = (s16)(7 - (4 - nn)); }
    FarPtr t = fd(0x4CE8, 0);
    for (s16 i = 0; i < nn; i++) fww(t, (u16)(i * IT_SIZE + 0x4B), names[i]);
    for (s16 i = 0; i < ncopy; i++)
        for (u16 b = 0; b < IT_SIZE; b++) fbw(t, (u16)(i * IT_SIZE + 0xAB + b), fb(t, (u16)(i * IT_SIZE + 0x11 + b)));
    fww(t, (u16)(nn * IT_SIZE + 0xE5), 0x2181 /* "ACCEPT" */);
}

/* PS 307d:020f flt_roster(cb, pilots, enemy) -> not cancelled: the pilots of a flight */
u16 flt_roster(FarPtr cb, u16 pilots_ds, u16 enemy)
{
    u16 names[4] = { 0, 0, 0, 0 };
    DSB(FL_ENEMY) = (u8)enemy;
    DSB(FL_SIZE) = fb(cur_item(), IT_USER);
    fww(fd(0x4CE8, 0), 0x17, (u16)(((u8)enemy == 0 ? 30 : 50) + DSC(FL_SIZE) * 20));
    if (DSC(FL_SIZE) > 4) assert_line(AF_FLTALERT, 0xF2);
    for (s16 i = 0; i < DSC(FL_SIZE); i++) {
        s8 code = DSC((u16)(pilots_ds + i));
        FarPtr s = code < 4 ? strings_get(0, code) : ace_name((s16)(code - 4));
        u16 p = strpool_copy(s).off;
        names[i] = xmalloc((u16)(rtl_strlen(p) + 1));
        rtl_strcpy(names[i], p);
    }
    s8 n = DSB(FL_ENEMY) == 0 ? (s8)(7 - (4 - DSC(FL_SIZE)) - 1) : (s8)(7 - (4 - DSC(FL_SIZE)));
    ds_far_wr(CH_OK, cb);
    flt_build(names);
    u8 r = (u8)dlg_modal2(TBL_FLT_RUN, n, FNP(flt_frame), FNP(choose_on_ok));
    for (s16 i = 0; i < DSC(FL_SIZE); i++) xfree(names[i]);
    return r != 2;
}

/* PS 307d:0376 flt_init */
void flt_init(void) { hs_default_links(TBL_FLT, 7); }

__attribute__((constructor)) static void dialogs_codeptrs(void)
{
    REG(choose_draw_title);
    REG(choose_entry_cb);
    REG(choose_frame);
    REG(choose_click);
    REG(choose_on_ok);
    REG(pick_btn_cb);
    REG(pick_draw_window);
    REG(pick_frame);
    REG(pick_on_ok);
    REG(entry_btn_cb);
    REG(entry_draw_window);
    REG(entry_frame);
    REG(req_draw_text);
    REG(req_btn_cb);
    REG(req_frame);
    REG(flt_subst);
    REG(flt_draw);
    REG(flt_frame);
    REG(flt_close_cb);
    REG(flt_pick_cb);
    /* shell_data formatters whose code pointers the pickers pass (not registered by shell_data) */
    codeptr_register(PROG_PS, PS_FN_acl_name, (CodeFn)acl_name);
    codeptr_register(PROG_PS, PS_FN_acelist_row_name, (CodeFn)acelist_row_name);
}
