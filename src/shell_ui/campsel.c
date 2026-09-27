/* shell_ui part B: campsel.c (PS 0fa1) — campaign selection and "CAMPAIGN REVIEW"
 * (shell_ui_career.md §8). Screens 15 CAMPSEL / 16 CAMPSELJ, hot-spot table 4bb0:0010 (9 items). */
#include "shell_ui/sui_b_priv.h"

#define CS_SEG      0x4bb0
#define CS_TBL      CS_SEG, 0x0010
#define CS_CURSOR   0x0020      /* item 0's user word */
#define CS_NALLOC   0x0A64
#define CS_LIST     0x0A66      /* near: u8 campaign indices */
#define CS_FHDL     0x4A1A
#define CS_FSML     0x4A1C
#define CS_TEXT     0x4A1E      /* far */
#define CS_DATE     0x4A22
#define CS_SHOWN    0x4A24
#define CS_NUSED    0x4A26

static u16 cs_cursor(void) { return SEGW(CS_SEG, CS_CURSOR); }
static void cs_set_cursor(u16 v) { SEGW(CS_SEG, CS_CURSOR) = v; }
static s16 cs_at(u16 i) { return (s16)(s8)DSB((u16)(DSW(CS_LIST) + i)); }

/* PS 0fa1:0002 cs_draw_prompt */
static void cs_draw_prompt(void)
{
    draw_popup_box();
    gfx_font_select((s16)DSW(F_FONT3));
    if (uw(cur_item(), HS_LABEL) != 0) text_engrave_center(uw(cur_item(), HS_LABEL), 0xC1, 0xB1, 0xC0);
}

/* PS 0fa1:0039 cs_btn_cb */
static void cs_btn_cb(void)
{
    if (btn_small_cb(0)) {
        DSW(HS_RES) = DSW(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0fa1:005b cs_draw_header */
static void cs_draw_header(void)
{
    u16 mk = stk_mark();
    u16 date = stk_alloc(0x16);
    u16 name = campaign_name((s16)DSW(CS_SHOWN)).off;
    date_text_short(DSW(CS_DATE), date);
    DSB(LINE_C) = 0xC3;
    DSB(TXT_COL) = 0xC3;
    gfx_font_select((s16)DSW(CS_FHDL));
    s16 x = (s16)ps_text_center_x(name, DSW(RX), DSW(RW));
    gfx_text(name, x, (s16)(DSS(RY) + 4));
    s16 w = gfx_text_width(name);
    s16 ly = (s16)(DSS(RY) + 0x0E);
    gfx_line(x, ly, (s16)(x + w), ly);
    gfx_font_select((s16)DSW(CS_FSML));
    x = (s16)ps_text_center_x(date, DSW(RX), DSW(RW));
    gfx_text(date, x, (s16)(DSS(RY) + 0x11));
    stk_release(mk);
}

/* PS 0fa1:0112 cs_draw_text */
static void cs_draw_text(void)
{
    DSB(TXT_COL) = 0xC3;
    flow_setup(0, 8, 1);
    flow_text(ds_far(CS_TEXT), 1, fnull());
}

/* PS 0fa1:0147 cs_side_items — side 0 (Japan): items 7, 8; else items 5, 6 */
static void cs_side_items(void)
{
    u8 us = DSB(0x4775) ? 0x40 : 0, jp = DSB(0x4775) ? 0 : 0x40;
    set_mask(hs_item(CS_TBL, 5), us);
    set_mask(hs_item(CS_TBL, 6), us);
    set_mask(hs_item(CS_TBL, 7), jp);
    set_mask(hs_item(CS_TBL, 8), jp);
    hs_default_links(hs_item(CS_TBL, 0), 9);
}

/* PS 0fa1:0192 cs_cmp(a, b) — by start date (qsort comparator, 1-byte elements) */
static s16 cs_cmp(u16 a_ds, u16 b_ds)
{
    u16 da = campaign_start((s8)DSB(a_ds));
    u16 db = campaign_start((s8)DSB(b_ds));
    return (s16)(u16)(da - db);
}

/* PS 0fa1:01c8 cs_build_list */
static void cs_build_list(void)
{
    DSW(CS_NALLOC) = (u16)campaign_count();
    DSW(CS_LIST) = xmalloc(DSW(CS_NALLOC));
    u16 date = DSW(0x477B);
    s16 first = campaign_first();
    s16 end = (s16)(first + (s16)DSW(CS_NALLOC));
    s8 k = (s8)first, n = 0;
    for (; (s16)k < end; k++) {
        if (!(s8)campaign_exists(k)) continue;
        if (campaign_start(k) < date) continue;
        if (!campaign_has_service(k, (s8)DSB(0x4776))) continue;
        DSB((u16)(DSW(CS_LIST) + n)) = (u8)k;
        n++;
    }
    DSW(CS_NUSED) = (u16)(s16)n;
    if (DSW(CS_NUSED) == 0) ufatal(CS_SEG, 0x10E);
    if ((s16)DSW(CS_NUSED) > 1) rtl_qsort(DSW(CS_LIST), DSW(CS_NUSED), 1, UFP(cs_cmp));
    for (k = (s8)DSB(CS_NUSED); (s16)k < (s16)DSW(CS_NALLOC); k++) DSB((u16)(DSW(CS_LIST) + (s16)k)) = 0xFF;
}

/* PS 0fa1:02c8 cs_frame */
static void cs_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 2) DSW(HS_RES) = 3;
}

static void cs_fonts_load(u16 sml, u16 sml_ext, u16 hdl, u16 hdl_ext)
{
    DSW(CS_FSML) = font_load(make_name(sml, -1, sml_ext));
    DSW(CS_FHDL) = font_load(make_name(hdl, -1, hdl_ext));
}

static u8 cs_side_screen(void) { return DSB(0x4775) ? 0x0F : 0x10; }

/* PS 0fa1:02f9 campsel_run(new_career) -> u8 — callers: pilot_new (1), career menu, next campaign (0) */
u16 campsel_run(u16 new_career)
{
    u8 loop = 1, first = 1, result = 0;
    if ((s8)new_career == 0) uww(hs_item(CS_TBL, 3), HS_LABEL, 7);        /* "QUIT FOR NOW" */
    campaign_load((s8)DSB(0x4779));
    cmptext_open();
    cs_build_list();
    DSW(CS_SHOWN) = (u16)cs_at(0);
    cs_set_cursor(0);
    cs_side_items();
    cs_fonts_load(0x0A68 /* "simq_sml" */, 0x0A71, 0x0A75 /* "simq_hdl" */, 0x0A7E);
    while (loop) {
        DSW(CS_DATE) = campaign_start((s8)cs_at(cs_cursor()));
        ds_far_wr(CS_TEXT, cmptext_intro((s8)cs_at(cs_cursor())));
        screen_show(cs_side_screen(), 0x88, 1);
        hs_set_table(hs_item(CS_TBL, 0), 9);
        if (first) { hs_focus(4); first = 0; }
        else hs_focus(DSW(HS_RES));
        hs_draw_all();
        menu_run(UFP(cs_frame));
        ui_busy();
        switch (DSW(HS_RES)) {
        case 1:                                              /* PREVIOUS */
            cs_set_cursor(cs_cursor() ? (u16)(cs_cursor() - 1) : (u16)(DSW(CS_NUSED) - 1));
            DSW(CS_SHOWN) = (u16)cs_at(cs_cursor());
            break;
        case 2: {                                            /* NEXT */
            s16 rem;
            idiv16_retry((s32)(s16)(cs_cursor() + 1), (s16)DSW(CS_NUSED), &rem);
            cs_set_cursor((u16)rem);
            DSW(CS_SHOWN) = (u16)cs_at(cs_cursor());
            break;
        }
        case 3:                                              /* CANCEL / QUIT FOR NOW */
            result = 0; loop = 0;
            break;
        case 4:                                              /* SELECT: then the squadron */
            DSW(0x4790) = (u16)cs_at(cs_cursor());
            squad_open();
            air_open();
            ace_open();
            station_open();
            result = (u8)squadinf(0);
            station_close();
            ace_close();
            air_close();
            squad_close();
            loop = (u8)(result == 0);                        /* squadron cancelled: back to the campaigns */
            first = 1;
            break;
        }
    }
    campaign_close();
    cmptext_close();
    xfree(DSW(CS_LIST));
    font_free(DSW(CS_FSML));
    font_free(DSW(CS_FHDL));
    uww(hs_item(CS_TBL, 3), HS_LABEL, 4);
    if (result) pilot_store();
    return result;
}

/* PS 0fa1:055a cs_review_layout */
static void cs_review_layout(void)
{
    static const int hide[3] = { 1, 2, 4 };
    for (int i = 0; i < 3; i++) {
        uwb(hs_item(CS_TBL, hide[i]), HS_MASK, 0);
        uwb(hs_item(CS_TBL, hide[i]), HS_FLAGS, 0);
    }
    uww(hs_item(CS_TBL, 0), HS_LABEL, 1);                  /* "CAMPAIGN REVIEW" */
    uww(hs_item(CS_TBL, 3), HS_LABEL, 6);                  /* "CONTINUE" */
    uww(hs_item(CS_TBL, 3), HS_X, 0x7D);
}

/* PS 0fa1:05ad cs_normal_layout */
static void cs_normal_layout(void)
{
    static const int items[3] = { 1, 2, 4 };
    for (int i = 0; i < 3; i++) {
        uwb(hs_item(CS_TBL, items[i]), HS_MASK, 0x74);
        uwb(hs_item(CS_TBL, items[i]), HS_FLAGS, 0x82);
    }
    uww(hs_item(CS_TBL, 0), HS_LABEL, 0);
    uww(hs_item(CS_TBL, 3), HS_LABEL, 4);
    uww(hs_item(CS_TBL, 3), HS_X, 0xA4);
}

/* PS 0fa1:0600 campsel_show_text(far text) — "CAMPAIGN REVIEW" at the end of a campaign */
void campsel_show_text(FarPtr text)
{
    DSW(CS_SHOWN) = DSW(0x4790);
    cs_review_layout();
    cs_side_items();
    cs_fonts_load(0x0A82 /* "simq_sml" */, 0x0A8B, 0x0A8F /* "simq_hdl" */, 0x0A98);
    screen_show(cs_side_screen(), 0x88, 1);
    hs_set_table(hs_item(CS_TBL, 0), 9);
    DSW(CS_DATE) = campaign_end((s16)DSW(0x4790));           /* the end date */
    ds_far_wr(CS_TEXT, text);
    hs_draw_all();
    menu_run(UFP(cs_frame));
    ui_busy();
    cs_normal_layout();
    /* PORT: the original calls xfree(DS:0A66) here, freeing the list pointer the last campsel_run
     * already freed (a double free of a stale near-heap block): skipped. */
    font_free(DSW(CS_FSML));
    font_free(DSW(CS_FHDL));
}

__attribute__((constructor)) static void campsel_codeptrs(void)
{
    UREG(cs_draw_prompt);
    UREG(cs_btn_cb);
    UREG(cs_draw_header);
    UREG(cs_draw_text);
    UREG(cs_cmp);
    UREG(cs_frame);
}
