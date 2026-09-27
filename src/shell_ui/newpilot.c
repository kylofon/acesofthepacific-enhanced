/* shell_ui part B: newpilot.c — "START A CAREER" (PS 0ed9, shell_ui_career.md §4; screen 28
 * START0/SIGNIN, hot-spot table 4b39:0000, 9 items) and the award ceremonies (PS 0f35, §6; screen 53
 * PARTS/RESULTS, table 4b46:0000, 4 items). */
#include "shell_ui/sui_b_priv.h"

#define NW_SEG      0x4b39
#define NW_TBL      NW_SEG, 0x0000
#define NW_DATES    0x4478      /* DATE<cset>.DAT: 11 services × 3 dates × 3 bytes */
#define NW_RANK0    0x45AF      /* first rank of the service */
#define NW_PRESSED  0x45B0
#define NW_RANKSEL  0x45B1
#define NW_STRINGS  0x45B2      /* char *[2]: the sentence, 0 */
#define NW_DATESEL  0x45B6
#define NW_ROW0     0x44DB      /* far copies of STRINGS_.DAT row 0 (53) */

/* PS 0ed9:0008 nw_draw_panel */
static void nw_draw_panel(void)
{
    tile_fill_rect();
    ps_draw_frame();
}

/* PS 0ed9:0017 nw_draw_title */
static void nw_draw_title(void)
{
    draw_title_bar_for((u16)(s16)(s8)DSB(0x4776), DSW(0x477B), str_get(3));   /* "START A CAREER" */
    set_mask(cur_item(), 0x40);
}

/* PS 0ed9:0040 nw_btn_cb */
static void nw_btn_cb(void)
{
    if (btn_cb()) {
        DSB(NW_PRESSED) = DSB(CUR_IDX);
        menu_set_result(DSW(CUR_IDX) == 8 ? 1 : 2);
    }
}

static void nw_set_date(void)
{
    u16 p = (u16)(NW_DATES + (s16)(s8)DSB(0x4776) * 9 + (s16)(s8)DSB(NW_DATESEL) * 3);
    DSW(0x477B) = date_make(ds_ptr(p));
}

/* PS 0ed9:006a nw_on_ok(sel) — chooser callback */
static void nw_on_ok(s16 sel)
{
    switch (DSW(CUR_IDX)) {
    case 3:
        DSB(NW_DATESEL) = (u8)sel;
        nw_set_date();
        break;
    case 5:
        DSB(NW_RANKSEL) = (u8)sel;
        DSB(0x4777) = (u8)(DSB(NW_RANK0) + DSB(NW_RANKSEL));
        DSW(0x47BD) = rank_points((s8)DSB(0x4777));
        break;
    case 6:                   /* not reached: the service button does not use a chooser */
        DSB(0x4776) = (u8)sel;
        set_mask(hs_item(NW_TBL, 1), 3);
        set_mask(hs_item(NW_TBL, 2), 3);
        DSB(0x4775) = (u8)service_side(DSB(0x4776));
        DSB(NW_DATESEL) = 0;
        return;
    default:
        return;
    }
    set_mask(hs_item(NW_TBL, 1), 3);
    set_mask(hs_item(NW_TBL, 2), 3);
}

/* PS 0ed9:010b nw_edit */
static void nw_edit(void)
{
    switch (DSW(CUR_IDX)) {
    case 3:
        uww(cur_item(), HS_USER, 3);
        choose_run(UFP(nw_on_ok), (u16)(s16)(s8)DSB(NW_DATESEL), (u16)((s16)(s8)DSB(0x4776) * 3 + 100));
        break;
    case 4:
        text_entry(0, 0x4754, 15, 1);
        break;
    case 5:
        uww(cur_item(), HS_USER, (u16)(s16)(s8)DSB((u16)(0x1DB4 + (s16)(s8)DSB(0x4776))));   /* ranks per service */
        choose_run(UFP(nw_on_ok), (u16)(s16)(s8)DSB(NW_RANKSEL), (u16)((s16)(s8)DSB(NW_RANK0) + 0x33));
        break;
    case 6:
        DSB(NW_PRESSED) = 6;
        menu_set_result(1);
        break;
    }
}

/* PS 0ed9:019f nw_slot_label(kind, item far *it) -> 0 */
static u16 nw_slot_label(s16 kind, FarPtr it)
{
    s16 idx;
    switch (kind) {
    case 3: idx = (s16)((s16)(s8)DSB(0x4776) * 3 + (s16)(s8)DSB(NW_DATESEL) + 100); break;
    case 4: uww(it, HS_LABEL, 0x4754); return 0;               /* the name itself */
    case 5: idx = (s16)((s16)(s8)DSB(0x4777) + 0x33); break;
    case 6: idx = (s16)((s16)(s8)DSB(0x4776) + 0x85); break;
    default: return 0;
    }
    strcpy_far_src(LBLBUF, strings_get(0, idx));
    uww(it, HS_LABEL, LBLBUF);
    return 0;
}

/* PS 0ed9:0223 nw_subst(c, it): %a -> item 3 ... */
static FarPtr nw_subst(s8 c, FarPtr it)
{
    FarPtr r = { nw_slot_label((s16)(c - 0x5E), it), 0 };
    return r;
}

/* PS 0ed9:023f nw_slot_cb */
static void nw_slot_cb(void)
{
    nw_slot_label((s16)DSW(CUR_IDX), cur_item());
    set_mask(cur_item(), 0x74);
    if (choose_entry_cb()) {
        nw_edit();
        hs_restart();
    }
}

/* PS 0ed9:0272 nw_draw_text */
static void nw_draw_text(void)
{
    tile_fill_rect();
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    ui_flow_text_buttons(hs_item(NW_TBL, 0), 3, UFP(nw_subst), NW_STRINGS,
                         (s16)(DSS(RX) + 0x14), (s16)(DSS(RY) + 8), (s16)(DSS(RX) + DSS(RW) - 0x14));
    draw_bottom_bar();
    set_mask(cur_item(), 0x40);
}

/* PS 0ed9:02d2 nw_frame — the RNG is stirred every frame while the player types */
static void nw_frame(void)
{
    ps_rng_next(0);
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 2) DSB(NW_PRESSED) = 7;
}

/* PS 0ed9:0307 pilot_add — the new pilot's event list starts at the end of the blob */
static void pilot_add(void)
{
    s16 sum = 0;
    for (s16 i = 0; i < (s16)DSW(0x4A18); i++)
        sum = (s16)(sum + (s16)(s8)SEGB(0x4b4c, (u16)(i * 0x7A + 0x87)));   /* roster[i].+79h */
    DSW(0x47CB) = (u16)sum;
    DSB(0x47CD) = 0;
    DSW(0x4A18) = (u16)(DSW(0x4A18) + 1);
    pilot_store();
}

/* PS 0ed9:0347 pilot_new -> u8 — CAREER MENU → START A CAREER */
u16 pilot_new(void)
{
    u8 first = 1, loaded = 0, r = 0;
    if ((s16)DSW(0x4A18) >= 13) roster_manage(0);          /* the roster is full: delete one */
    if ((s16)DSW(0x4A18) >= 13) return 0;
    memzero(0x4754, 0x7A);
    DSB(0x4774) = DSB(0x4A18);
    DSB(0x4776) = (u8)fly_for_choose(0);
    if (DSB(0x4776) == 0xFF) return 0;
    cset_scan((u16)(s16)(s8)DSB(0x4776), 0, 0xFF);
    DSB(0x4779) = (u8)cset_choose(1);
    if (DSB(0x4779) == 0xFF) return 0;
    file_read_n(NW_DATES, 3, 0x21, make_name(0x09EA /* "date" */, (s16)(s8)DSB(0x4779), 0x09EF /* "dat" */));
    strtab_load(make_name(0x09F3 /* "strings_" */, -1, 0x09FC /* "dat" */));
    strings_dup_far(NW_ROW0, 0x35, 0);
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x12C);                            /* bp-130h: the sentence */
    DSW(NW_STRINGS) = buf;
    strcpy_far_src(buf, ds_far(NW_ROW0 + 11 * 4));          /* row0[11] */
    DSW(NW_STRINGS + 2) = 0;
    strtab_free();
    DSB(NW_DATESEL) = 0;
    DSB(NW_RANKSEL) = 0;
    ranks_load();
    DSW(0x478E) = 0xFFFF;
    realism_load(0x0A00 /* "creal.prf" */, 0x47A9);        /* career realism defaults */
    for (;;) {
        DSB(0x4775) = (u8)service_side(DSB(0x4776));
        nw_set_date();
        DSB(NW_RANK0) = (u8)rank_first((s8)DSB(0x4776));
        DSB(0x4777) = DSB(NW_RANK0);
        DSW(0x47BD) = rank_points((s8)DSB(0x4777));
        if (!loaded)
            strtab_load(make_name(0x0A0A /* "sftbtns" */, (s16)(s8)DSB(0x4779), 0x0A12 /* "dat" */));
        loaded = 1;
        screen_show(0x1C, 0x43, 1);
        hs_set_table(hs_item(NW_TBL, 0), 9);
        hs_focus(8);
        hs_draw_all();
        if (first) {                                       /* the name is typed straight away */
            text_entry(0, 0x4754, 15, 1);
            first = 0;
        }
        r = (u8)menu_run(UFP(nw_frame));
        ui_busy();
        if (DSB(NW_PRESSED) == 7) break;                   /* CANCEL / Esc */
        if (DSB(NW_PRESSED) == 8) {                        /* START */
            strtab_free();
            loaded = 0;
            if ((u8)campsel_run(1)) { pilot_add(); break; }
        } else if (DSB(NW_PRESSED) == 6) {                 /* service: new service resets date/rank */
            u8 s = (u8)fly_for_choose(1);
            if (s != 0xFF) DSB(0x4776) = s;
        }
    }
    if (loaded) strtab_free();
    ranks_free();
    stk_release(mk);
    return r == 1;
}

/* PS 0ed9:05b3 nw_init — dialogs_init */
void nw_init(void)
{
    hs_default_links(hs_item(NW_TBL, 0), 9);
}

/* ================================================================== PS 0f35: award ceremonies */

#define AW_SEG      0x4b46
#define AW_TBL      AW_SEG, 0x0000
#define AW_TEXT     0x45B8      /* far text */
#define AW_TTMS     0x45BC      /* 68·side + 17·kind: {char[9] name, s16 last, nrand, span, end_frame} */

/* PS 0f35:000c aw_draw_window */
static void aw_draw_window(void)
{
    draw_titled_panel();
    draw_rivets((u16)(DSS(RX) + 3), (u16)(DSS(RY) + 0x14), 0x11);
    draw_rivets((u16)(DSS(RX) + DSS(RW) - 6), (u16)(DSS(RY) + 0x14), 0x11);
}

/* PS 0f35:004e aw_draw_text */
static void aw_draw_text(void)
{
    draw_popup_box();
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    ps_rect_shrink(3);
    flow_setup(3, 7, 1);
    DSW(PG_SRC) = DSW(PG_WORK);
    flow_text(ds_far(AW_TEXT), 1, codeptr_far(PS_FN_display_subst));
    rect_grow(3);
}

/* PS 0f35:00af aw_draw(tm) — the four callbacks directly, then the TTM background region */
static void aw_draw(s8 tm)
{
    pages_front_to_back();
    for (s16 i = 0; i < 4; i++) {
        hs_load(hs_item(AW_TBL, 0), (u16)i);
        FarPtr cb = ufp(cur_item(), HS_CB);
        if (!far_is_null(cb)) codeptr_lookup(cb)();
    }
    if (tm) {
        copy_back_to_bg();
        upd_alloc_nodes(5);
        FarPtr t = ds_far(0x1FFA);                         /* item 0 of the current table */
        upd_add(uw(t, 0), uw(t, 2), uw(t, 4), (u16)(uw(t, 6) + 5), 1, DSW(PG_SPARE), DSW(PG_FRONT), 0, fnull());
    }
}

/* PS 0f35:012d aw_load_ttms — dialogs_init: TTMS<n>.DAT of all installed data sets */
void aw_load_ttms(void)
{
    u16 mk = stk_mark();
    u16 fh = stk_alloc(0x12);          /* bp-5Ch: 9 file handles */
    u16 rec = stk_alloc(0x46);         /* bp-4Ah: 69-byte record */
    open_module_files(fh, 0x0A4A /* "ttms" */, 0x0A4F /* "dat" */);
    for (s8 m = module_iter_reset(); module_highest() >= m; m = module_iter_next()) {
        u16 f = DSW((u16)(fh + m * 2));
        if (f == 0) continue;
        while (my_fread(rec, 0x45, 1, f)) {
            for (s8 k = 0; k < 4; k++) {
                u16 dst = (u16)(AW_TTMS + (s16)(s8)DSB(rec) * 0x44 + k * 0x11);
                for (int b = 0; b < 0x11; b++) DSB(dst + b) = DSB(rec + 1 + k * 0x11 + b);
            }
        }
    }
    /* the files are not closed (as in the original) */
    stk_release(mk);
}

/* PS 0f35:01d8 award_ceremony(side, kind, far text) — kind 0 captured, 1 injured, 2 medal, 3 promoted */
void award_ceremony(s8 side, s8 kind, FarPtr text)
{
    u16 mk = stk_mark();
    u16 title = stk_alloc(0x32);
    for (int i = 0; i < 0x32; i++) DSB(title + i) = DSB(0x0A18 + i);
    u16 e = (u16)(AW_TTMS + (s16)side * 0x44 + (s16)kind * 0x11);
    ds_far_wr(AW_TEXT, text);
    uww(hs_item(AW_TBL, 0), HS_LABEL, title);
    screen_show(0x35, 0x36, 0);
    copy_front_to_back();
    hs_set_table(hs_item(AW_TBL, 0), 4);
    if (kind == 2) {
        strcpy_far_src(title, medabbr_text((s8)DSB(0x0500)));
        str_upper(title);
    } else {
        strcpy_far_src(title, ds_ptr(str_get(kind)));
    }
    aw_draw(1);
    menu_flush();
    while (menu_busy()) host_pump();
    ttm_play_file(make_name(e, -1, 0x0A53 /* "ttm" */), (s16)DSW(e + 9), (s16)DSW(e + 0x0B), (s16)DSW(e + 0x0D),
                  (s16)DSW(e + 0x0F), codeptr_far(PS_FN_ttm_skip_check));
    while (menu_busy()) host_pump();
    if (kind == 2)
        ttm_play_file(make_name(0x0A57 /* "awrdmed" */, -1, 0x0A5F /* "ttm" */), -1, 0, 0, 0x19,
                      codeptr_far(PS_FN_ttm_skip_check));
    stk_release(mk);
}

__attribute__((constructor)) static void newpilot_codeptrs(void)
{
    UREG(nw_draw_panel);
    UREG(nw_draw_title);
    UREG(nw_btn_cb);
    UREG(nw_on_ok);
    UREG(nw_subst);
    UREG(nw_slot_cb);
    UREG(nw_draw_text);
    UREG(nw_frame);
    UREG(aw_draw_window);
    UREG(aw_draw_text);
}
