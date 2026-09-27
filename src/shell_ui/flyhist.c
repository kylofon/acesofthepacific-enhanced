/* shell_ui part B: flyhist.c (PS 0583) — FLY HISTORIC MISSION (shell_ui_debrief.md §6).
 * Screen 23 FLYHIST, hot-spot table 4992:0010 (17 items; its static links are used: flyhist_init
 * is never called). */
#include "shell_ui/sui_b_priv.h"

#define FH_SEG      0x4992
#define FH_TBL      FH_SEG, 0x0010
#define FH_NOMISS   0x053A      /* u8: the chosen service has no historic mission */
#define FH_DELAY    0x053B      /* u8: one-frame delay before the notice */
#define FH_INSIGNIA 0x424C
#define FH_PICS     0x424E      /* MT_HIST.BMP */
#define FH_SVCNAMES 0x4250      /* near char *[11] (30-byte copies on flyhist_run's stack) */
#define FH_CAMP     0x4266      /* s8 */
#define FH_SERVICE  0x4267      /* s8 */
#define FH_ACTION   0x4268      /* s8: item to focus / activated */

/* PS 0583:0009 flyhist_btn_cb — the campaign button (user 1) shows the campaign name */
static void flyhist_btn_cb(void)
{
    if (uw(cur_item(), HS_USER) == 1) {
        if ((s8)DSB(FH_CAMP) > -1)
            uww(cur_item(), HS_LABEL, campaign_name(hist_key((s8)DSB(FH_CAMP))).off);
        else
            uww(cur_item(), HS_LABEL, 0x053C /* "" */);
    }
    if (btn_cb()) {
        DSB(FH_ACTION) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0583:005f flyhist_service_btn_cb */
static void flyhist_service_btn_cb(void)
{
    uww(cur_item(), HS_LABEL, DSW((u16)(FH_SVCNAMES + (s16)(s8)DSB(FH_SERVICE) * 2)));
    if (btn_small_cb(0)) {
        DSB(FH_ACTION) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0583:0095 flyhist_draw_scroll_frame */
static void flyhist_draw_scroll_frame(void)
{
    draw_bevel_box();
}

/* PS 0583:009f flyhist_draw_label — item 7's label is the near pointer 2 (an empty string): the
 * "SELECT MISSION:" label is never drawn (faithful) */
static void flyhist_draw_label(void)
{
    u16 s = uw(cur_item(), HS_LABEL);
    gfx_font_select((s16)DSW(F_FONT3));
    text_engrave(s, 0xC7, 0xB1, 0xC0, DSW(RX), DSW(RY));
}

/* PS 0583:00dd flyhist_draw_insignia */
static void flyhist_draw_insignia(void)
{
    tile_fill_rect();
    draw_header_panel();
    u16 f = bmp_frame(DSW(FH_INSIGNIA), (s8)DSB(FH_SERVICE));
    s16 w = (s16)(frame_w(f) - 4), h = frame_h(f);
    s16 dx = (s16)((DSS(RW) >> 1) - (w >> 1));
    s16 dy = (s16)((DSS(RH) >> 1) - (h >> 1));
    gfx_sprite_flip(f, (s16)(DSS(RX) + dx), (s16)(DSS(RY) + dy), 0);
}

/* PS 0583:014f flyhist_draw_picture — quirk: lbox_selected() used directly as the entry index */
static void flyhist_draw_picture(void)
{
    draw_inset_box2();
    if ((s8)DSB(FH_CAMP) > -1) {
        s16 pic = hist_e4((s8)DSB(FH_CAMP), (s16)lbox_selected());
        gfx_sprite_flip(bmp_frame(DSW(FH_PICS), pic), (s16)(DSS(RX) + 2), (s16)(DSS(RY) + 2), 0);
    }
    set_mask(cur_item(), 0x40);
}

/* PS 0583:01a9 flyhist_draw_synopsis */
static void flyhist_draw_synopsis(void)
{
    rect_grow(4);
    draw_popup_box();
    ps_rect_shrink(4);
    if ((s8)DSB(FH_CAMP) > -1) {
        s16 id = hist_e0((s8)DSB(FH_CAMP), (s16)lbox_selected());
        FarPtr t = histsn_text(id);
        gfx_font_select((s16)DSW(F_FONT3));
        DSB(TXT_FLG) = 1;
        flow_setup((u16)-3, 8, 1);
        flow_text(t, 1, fnull());
    }
    set_mask(cur_item(), 0x40);
}

/* PS 0583:0237 flyhist_find_campaign */
static void flyhist_find_campaign(void)
{
    s16 n = hist_count();
    DSB(FH_CAMP) = 0xFF;
    for (s16 c = 0; c < n; c++) {
        if ((s8)hist_has_side(c, (s8)DSB(FH_SERVICE))) {
            DSB(FH_CAMP) = (u8)c;
            break;
        }
    }
}

/* PS 0583:027c flyhist_draw_row(i, y) — row i = the i-th entry of the chosen service */
static void flyhist_draw_row(s16 i, s16 y)
{
    s16 e = hist_nth_side((s8)DSB(FH_CAMP), (s8)DSB(FH_SERVICE), i);
    s16 id = hist_e0((s8)DSB(FH_CAMP), e);
    if (id <= -1) ufatal(FH_SEG, 0x148);
    gfx_text(strcpy_far_src(STRBUF, histnm_text(id)).off, (s16)(DSS(RX) + 4), y);
    set_mask(hs_item(FH_TBL, 13), 3);                /* synopsis and picture repaint */
    set_mask(hs_item(FH_TBL, 12), 3);
}

/* PS 0583:0322 flyhist_draw_bg */
static void flyhist_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for(1, 0, str_get(6));              /* " HISTORIC MISSIONS " */
}

/* PS 0583:0345 flyhist_frame */
static void flyhist_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    edit_poll();                                       /* the list box's hidden text field */
    if (DSB(FH_NOMISS)) {
        if (DSB(FH_DELAY)) {
            req_ok(0, 0, 0x17, 8);    /* "There are no historic missions in the modules on disk for the service you selected." */
            DSB(FH_ACTION) = 6;        /* -> the service chooser opens again */
            menu_set_result(1);
            DSB(FH_DELAY) = 0;
        } else {
            DSB(FH_DELAY) = 1;         /* first frame: let the screen appear */
        }
    } else {
        DSB(FH_DELAY) = 0;
        if (menu_get_result() == 2) DSB(FH_ACTION) = 0x0E;
    }
}

static void flyhist_close_all(void)
{
    histnm_close();
    histsn_close();
    hist_close();
    campaign_close();
    bmp_free(DSW(FH_INSIGNIA));
    bmp_free(DSW(FH_PICS));
}

/* PS 0583:03c2 flyhist_run — single-mission menu FLY A HISTORIC MISSION */
void flyhist_run(void)
{
    u16 mk = stk_mark();
    u16 names = stk_alloc(0x14A);                                  /* 11 × 30 bytes */
    DSW(FH_PICS) = bmp_load_planes(make_name(0x0554 /* "mt_hist" */, -1, 0x055C /* "bmp" */));
    DSW(FH_INSIGNIA) = bmp_load_planes(make_name(0x0560 /* "insignia" */, -1, 0x0569 /* "bmp" */));
    if (DSW(FH_PICS) == 0 || DSW(FH_INSIGNIA) == 0) ufatal(FH_SEG, 0x191);
    strtab_load(make_name(0x056D /* "sftbtns" */, 0, 0x0575 /* "dat" */));
    for (s16 i = 0; i < 11; i++) {
        u16 p = (u16)(names + i * 0x1E);
        DSW((u16)(FH_SVCNAMES + i * 2)) = p;
        strcpy_far_src(p, strings_get(0, (s16)(i + 0x85)));
    }
    strtab_free();
    campaign_load(-1);                                             /* all campaigns */
    hist_open();
    histnm_open();
    histsn_open();
    DSB(FH_ACTION) = 0x10;                                         /* BEGIN MISSION */
    DSB(FH_SERVICE) = 0;
    flyhist_find_campaign();
    for (;;) {
        s16 n = (s8)DSB(FH_CAMP) > -1 ? hist_count_side((s8)DSB(FH_CAMP), (s8)DSB(FH_SERVICE)) : 0;
        screen_open(0x17, UFP(flyhist_draw_bg), 0x396, 1);
        lbox_setup(0, 0, (u16)n, 5, fnull(), UFP(flyhist_draw_row));
        lbox_bind(fdat(FH_TBL), 8, 10);
        hs_set_table(fdat(FH_TBL), 0x11);
        hs_focus((u16)(s16)(s8)DSB(FH_ACTION));
        hs_draw_all();
        menu_run(UFP(flyhist_frame));
        ui_busy();
        s8 a = (s8)DSB(FH_ACTION);
        if (a == 6) {                                              /* SERVICE */
            u8 svc = DSB(FH_SERVICE), camp = DSB(FH_CAMP);
            s8 s = (s8)fly_for_choose(0);
            if (s > -1) {
                DSB(FH_SERVICE) = (u8)s;
                flyhist_find_campaign();
                DSB(FH_NOMISS) = (u8)(DSB(FH_CAMP) == 0xFF);
                if (DSB(FH_NOMISS)) {
                    DSB(FH_SERVICE) = svc;
                    DSB(FH_CAMP) = camp;
                }
            } else {
                DSB(FH_NOMISS) = 0;
            }
        } else if (a == 4) {                                       /* CAMPAIGN */
            s8 c = (s8)camp_choose(DSB(FH_SERVICE));
            if (c > -1) DSB(FH_CAMP) = (u8)c;
        } else if (a == 0x0E) {                                    /* RETURN TO MAIN / Esc */
            flyhist_close_all();
            break;
        } else if (a == 0x0F) {                                    /* REALISM PANEL */
            DSB(0x626B) = 0;
            DSB(0x62C5) = DSB(FH_SERVICE);
            realism_run();
        } else if (a == 0x10) {                                    /* BEGIN MISSION */
            if ((s8)DSB(FH_CAMP) <= -1) continue;
            s16 m = hist_e2((s8)DSB(FH_CAMP), (s16)lbox_selected());
            ps_mission_load(make_name(0x0579 /* "MISS" */, m, 0x057E /* "DAT" */), 1);
            mission_write_script();
            ps_mission_free_1f06();
            DSB(0x626B) = 2;                                       /* historic mission */
            DSB(0x62C5) = DSB(FH_SERVICE);
            module_set_active(m > 0x3A ? 1 : 0);                   /* MISS59+ come from the 1946 add-on */
            flyhist_close_all();
            squad_open();
            ace_open();
            ord_open();
            air_open();
            briefing((u16)m);
            air_close();
            ord_close();
            ace_close();
            squad_close();
            if (DSB(0x43D6)) fly_mission(0);
            if (!far_is_null(ds_far(0x5272))) { xfarfree(ds_far(0x5272)); ds_far_wr(0x5272, fnull()); }
            if (!far_is_null(ds_far(0x5276))) { xfarfree(ds_far(0x5276)); ds_far_wr(0x5276, fnull()); }
            load_free();
            break;
        }
    }
    stk_release(mk);
}

/* PS 0583:075a flyhist_init — unreferenced in the original */
static void flyhist_init(void)
{
    hs_default_links(fdat(FH_TBL), 0x11);
}

__attribute__((constructor)) static void flyhist_codeptrs(void)
{
    UREG(flyhist_btn_cb);
    UREG(flyhist_service_btn_cb);
    UREG(flyhist_draw_scroll_frame);
    UREG(flyhist_draw_label);
    UREG(flyhist_draw_insignia);
    UREG(flyhist_draw_picture);
    UREG(flyhist_draw_synopsis);
    UREG(flyhist_draw_row);
    UREG(flyhist_draw_bg);
    UREG(flyhist_frame);
    (void)flyhist_init;
}
