/* shell_ui part B: campset.c — campaign-set selection (PS 0a41, shell_ui_options.md §6; screen 44
 * CLOUDS/CAMPSET, table 4a88:0010, 8 items) and the historic campaign chooser (PS 12a7,
 * shell_ui_mission.md §4; screen 43 CLOUDS/CAMPBOX, table 4be4:0000, 8 items). */
#include "shell_ui/sui_b_priv.h"

#define CSET_SEG    0x4a88
#define CSET_TBL    CSET_SEG, 0x0010
#define CSET_LIST   0x0742      /* s16[]: data sets offered */
#define CSET_N      0x0754
#define CSET_MODE   0x4344

/* PS 0a41:0003 cset_draw_prompt */
static void cset_draw_prompt(void)
{
    s16 x = DSS(RX), y = DSS(RY);
    u16 s1, s2;
    gfx_font_select((s16)DSW(F_FONT3));
    if (DSB(CSET_MODE) != 0) {
        s1 = str_get(3);                                   /* "Select the Campaign Set" */
        s2 = str_get(4);                                   /* "to fly from:" */
    } else {
        u16 mk = stk_mark();
        u16 buf = stk_alloc(0x36);
        u16 s0 = str_get(0);                               /* "is now on" */
        u16 r = (u16)((s16)(s8)DSB(0x4777) << 2);
        rtl_sprintf(buf, 0x075A /* "%Fs %s %s" */, (unsigned)DSW(0x57A2 + r), (unsigned)DSW(0x57A4 + r),
                    (unsigned)0x4754, (unsigned)s0);
        text_engrave(buf, 0xC1, 0xB1, 0xC0, (u16)(x - 20), (u16)y);
        stk_release(mk);
        s1 = str_get(1);
        s2 = str_get(2);
    }
    y = (s16)(y + 8);
    text_engrave(s1, 0xC1, 0xB1, 0xC0, (u16)(x - 20), (u16)y);
    y = (s16)(y + 8);
    text_engrave(s2, 0xC1, 0xB1, 0xC0, (u16)(x - 20), (u16)y);
}

/* PS 0a41:00e7 cset_btn_cb */
static void cset_btn_cb(void)
{
    if (btn_small_cb(0)) menu_set_result(DSW(CUR_IDX) == 7 ? 1 : 2);
}

/* PS 0a41:010f cset_draw_row(i, y) — the first 80 bytes of CSET<n>.DAT */
static void cset_draw_row(s16 i, s16 y)
{
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x52);
    s16 set = (s16)DSW((u16)(CSET_LIST + i * 2));
    u16 f = my_fopen(make_name(0x0764 /* "cset" */, set, 0x0769 /* "dat" */), 0x076D /* "rb" */);
    if (f == 0) ufatal(CSET_SEG, 0x89);
    my_fread(buf, 1, 0x50, f);
    my_fclose(f);
    gfx_text(buf, (s16)(DSS(RX) + 5), y);
    stk_release(mk);
}

/* PS 0a41:01a0 cset_frame */
static void cset_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
}

/* PS 0a41:01c1 cset_choose(mode) -> set / FFh */
u16 cset_choose(u16 mode)
{
    if (DSW(CSET_N) == 0) return 0xFF;
    DSB(CSET_MODE) = (u8)mode;
    lbox_setup(0, 0, DSW(CSET_N), 5, fnull(), UFP(cset_draw_row));
    lbox_bind(fdat(CSET_TBL), 2, 4);
    screen_show(0x2C, 0x81, 1);
    hs_set_table(fdat(CSET_TBL), 8);
    hs_focus(7);
    hs_draw_all();
    u16 v;
    if (menu_run(UFP(cset_frame)) == 2) v = 0xFFFF;
    else v = DSW((u16)(CSET_LIST + lbox_selected() * 2));
    ui_busy();
    module_set_active((u8)v);
    return (u8)v;
}

/* PS 0a41:028d cset_scan(service, date, cur) -> 1 if a valid set other than cur exists */
u16 cset_scan(u16 a, u16 b, u16 cur)
{
    s8 m = module_iter_reset();
    u8 other = 0;
    DSW(CSET_N) = 0;
    while (module_highest() >= m) {
        campaign_load(m);
        if ((s8)campaign_available_after(b, (s8)a)) {
            DSW((u16)(CSET_LIST + DSW(CSET_N) * 2)) = (u16)(s16)m;
            DSW(CSET_N) = (u16)(DSW(CSET_N) + 1);
            if ((s8)cur != m) other = 1;
        }
        campaign_close();
        m = module_iter_next();
    }
    return other;
}

/* ================================================================== PS 12a7: campaign chooser */

#define CAMP_TBL    0x4be4, 0x0000
#define CAMP_NAMES  0x4C7C      /* near char *[] */
#define CAMP_IDS    0x4BB4      /* s16[] */
#define CAMP_N      0x4BB2
#define CAMP_RES    0x0E0A

/* PS 12a7:0008 camp_draw_title */
static void camp_draw_title(void)
{
    u16 s = uw(cur_item(), HS_LABEL);
    gfx_font_select((s16)DSW(F_FONT4));
    DSB(TXT_FLG) = 3;
    text_engrave(s, 0xC1, 0xB1, 0xC0, DSW(RX), DSW(RY));
}

/* PS 12a7:004b camp_btn_cb */
static void camp_btn_cb(void)
{
    if (btn_small_cb(0)) menu_set_result(DSW(CUR_IDX) == 7 ? 1 : 2);
}

/* PS 12a7:0073 camp_draw_row(i, y) */
static void camp_draw_row(s16 i, s16 y)
{
    gfx_text(DSW((u16)(CAMP_NAMES + i * 2)), (s16)(DSS(RX) + 5), y);
}

/* PS 12a7:0093 camp_list(service) — historic campaigns that have the service */
static void camp_list(s8 service)
{
    s16 n = hist_count();
    s16 k = 0;
    for (s16 i = 0; i < n; i++) {
        if (!(s8)hist_has_side(i, service)) continue;
        DSW((u16)(CAMP_NAMES + k * 2)) = campaign_name(hist_key(i)).off;
        DSW((u16)(CAMP_IDS + k * 2)) = (u16)i;
        k++;
    }
    DSW(CAMP_N) = (u16)k;
}

/* PS 12a7:00ec camp_frame */
static void camp_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
}

/* PS 12a7:010d camp_choose(service) -> campaign / FFh — the historic-mission screen */
u16 camp_choose(u16 service)
{
    camp_list((s8)service);
    lbox_setup(0, 0, DSW(CAMP_N), 5, fnull(), UFP(camp_draw_row));
    lbox_bind(fdat(CAMP_TBL), 2, 4);
    screen_show(0x2B, 0x2A, 1);
    hs_set_table(fdat(CAMP_TBL), 8);
    hs_focus(7);
    hs_draw_all();
    if (menu_run(UFP(camp_frame)) == 2) DSB(CAMP_RES) = 0xFF;
    else DSB(CAMP_RES) = DSB((u16)(CAMP_IDS + lbox_selected() * 2));
    ui_busy();
    return DSB(CAMP_RES);
}

/* PS 12a7:01c0 camp_init — dialogs_init */
void camp_init(void)
{
    hs_default_links(fdat(CAMP_TBL), 8);
}

__attribute__((constructor)) static void campset_codeptrs(void)
{
    UREG(cset_draw_prompt);
    UREG(cset_btn_cb);
    UREG(cset_draw_row);
    UREG(cset_frame);
    UREG(camp_draw_title);
    UREG(camp_btn_cb);
    UREG(camp_draw_row);
    UREG(camp_frame);
}
