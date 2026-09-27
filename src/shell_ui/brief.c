/* shell_ui part B: brief.c (PS 0c1d) — MISSION BRIEFING (shell_ui_options.md §7).
 * Screen 21 BRIEF/MISSBRF, hot-spot table 4af1:0000 (11 items). */
#include "shell_ui/sui_b_priv.h"

#define BR_SEG      0x4af1
#define BR_TBL      BR_SEG, 0x0000
#define BR_VARIANT  0x08F8      /* s16: briefing text index (-1 = pick) */
#define BR_BEGIN    0x43D6      /* u8: BEGIN MISSION chosen */
#define BR_WEATHER  0x43D7      /* far char *[4] */
#define BR_FMAP     0x43E7
#define BR_OFFICERS 0x43E9
#define BR_SIDE     0x43EB      /* s8: player's side group for %a/%e */
#define BR_CHOICE   0x43EC

static u8 br_historic(void) { return DSB(0x626B) == 2; }

/* PS 0c1d:0000 brief_subst(c) — %a allied, %e enemy names (SFTBTNS/BRIEF table 11) */
static FarPtr brief_subst(s8 c)
{
    if (c == 'a') return strings_get(11, (s16)((s8)DSB(BR_SIDE) + 2));
    if (c == 'e') return strings_get(11, (s8)DSB(BR_SIDE));
    return fnull();
}

/* PS 0c1d:0031 brief_draw_text */
static void brief_draw_text(void)
{
    s8 type = (s8)DSB(0x626C);
    draw_side_riveted_panel();
    gfx_font_select((s16)DSW(F_FONT3));
    FarPtr text = br_historic() ? histbf_text((s16)DSW(BR_VARIANT)) : strings_get(type, (s16)DSW(BR_VARIANT));
    rect_save();
    DSW(RX) = 0x64; DSW(RY) = 0x40; DSW(RW) = 0xCC; DSW(RH) = 0x5A;
    flow_setup((u16)-3, 8, 1);
    flow_text(text, 1, UFP(brief_subst));
    ps_rect_restore();
}

/* PS 0c1d:00c7 brief_draw_overview — historic overview text */
static void brief_draw_overview(void)
{
    gfx_font_select((s16)DSW(F_FONT3));
    flow_setup((u16)-3, 8, 1);
    flow_text(histov_text((s16)DSW(BR_VARIANT)), 1, fnull());
}

/* PS 0c1d:0106 brief_draw_officer */
static void brief_draw_officer(void)
{
    draw_inset_box2();
    DSW(BR_OFFICERS) = bmp_load_planes(make_name(0x08FA /* "officers" */, -1, 0x0903 /* "bmp" */));
    s16 f = (s8)DSB(0x62C5);
    gfx_sprite_flip(bmp_frame(DSW(BR_OFFICERS), f), (s16)(DSS(RX) + 2), (s16)(DSS(RY) + 2), 0);
    bmp_free(DSW(BR_OFFICERS));
}

/* PS 0c1d:016a brief_draw_title */
static void brief_draw_title(void)
{
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x32);
    s16 type = (s8)DSB(0x626C);
    draw_popup_box();
    gfx_font_select((s16)DSW(F_FONT4));
    if (br_historic()) strcpy_far_src(buf, histnm_text((s16)DSW(BR_VARIANT)));
    else rtl_strcpy(buf, str_get((s16)(type + 8)));
    u16 tx = ps_text_center_x(buf, DSW(RX), DSW(RW));
    ps_text_emboss(buf, tx, (u16)(DSS(RY) + 3), 0xC1, 0xC0, 0xB1);
    gfx_font_select((s16)DSW(F_FONT3));
    rtl_strcpy(buf, str_get(0));                                   /* "DATE: " */
    date_text(DSW(0x6277), STRBUF);
    rtl_strcat(buf, STRBUF);
    tx = ps_text_center_x(buf, DSW(RX), DSW(RW));
    text_engrave(buf, 0xC1, 0xB1, 0xC0, tx, (u16)(DSS(RY) + 0x0F));
    stk_release(mk);
}

/* PS 0c1d:026d brief_draw_weather */
static void brief_draw_weather(void)
{
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x32);
    draw_popup_box();
    gfx_font_select((s16)DSW(F_FONT3));
    rtl_strcpy(buf, str_get(1));                                   /* "WEATHER: " */
    strcpy_far_src(STRBUF, ds_far((u16)(BR_WEATHER + ((s16)(s8)DSB(0x626F) << 2))));
    rtl_strcat(buf, STRBUF);
    text_engrave(buf, 0xC1, 0xB1, 0xC0, (u16)(DSS(RX) + 5), (u16)(DSS(RY) + 3));
    stk_release(mk);
}

/* PS 0c1d:02ef brief_draw_label */
static void brief_draw_label(void)
{
    draw_popup_box();
    gfx_font_select((s16)DSW(F_FONT3));
    text_engrave_center(uw(cur_item(), HS_LABEL), 0xC1, 0xB1, 0xC0);
}

/* PS 0c1d:031f brief_btn_cb */
static void brief_btn_cb(void)
{
    if (btn_cb()) {
        DSB(BR_CHOICE) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0c1d:033d brief_draw_bg */
static void brief_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for(DSB(0x62C5), DSW(0x6277), str_get(7));   /* "MISSION BRIEFING" */
}

/* PS 0c1d:0362 brief_set_historic(h) — weather hidden in historic missions, overview shown */
static void brief_set_historic(u8 h)
{
    set_mask(hs_item(BR_TBL, 3), h ? 0 : 0x40);
    set_mask(hs_item(BR_TBL, 10), h ? 0x40 : 0);
    set_mask(hs_item(BR_TBL, 9), h ? 0x40 : 0);
}

/* PS 0c1d:0396 brief_frame */
static void brief_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 2) DSB(BR_CHOICE) = 4;              /* Esc = DECLINE */
}

/* PS 0c1d:03c6 briefing(variant) — callers: historic missions, flight deck, set-up, preflight */
void briefing(u16 variant_w)
{
    s16 variant = (s16)variant_w;
    s8 type = (s8)DSB(0x626C);
    strtab_load(make_name(0x0907 /* "sftbtns" */, 0, 0x090F /* "dat" */));
    for (s16 k = 0; k < 4; k++) {                                  /* weather names 9..12 */
        FarPtr p = xfarmalloc(0x0F);
        ds_far_wr((u16)(BR_WEATHER + (k << 2)), p);
        rtl__fstrncpy(ds_far((u16)(BR_WEATHER + (k << 2))), strings_get(0, (s16)(k + 9)), 0x0F);
    }
    strtab_free();
    u8 historic = br_historic();
    if (historic) {
        histbf_open();
        histov_open();
        histnm_open();
        if (variant > -1) DSW(BR_VARIANT) = (u16)variant;
        else DSW(BR_VARIANT) = (u16)squad_mission_param((s16)DSW(0x478E), DSW(0x477B), (s16)DSW(0x477D));
    } else {
        strtab_load(make_name(0x0913 /* "brief" */, -1, 0x0919 /* "dat" */));
        if (type == 8) DSW(BR_VARIANT) = (u16)(s16)(s8)DSB(0x6477);                       /* anti-shipping */
        else if (type == 9) DSW(BR_VARIANT) = (u16)(s16)(s8)DSB((u16)(0x64B2 + (s16)(s8)DSB(0x62E5) * 0x12));
        else if ((s16)DSW(BR_VARIANT) == -1) DSW(BR_VARIANT) = (u16)rng_below(3);
    }
    brief_set_historic(historic);
    DSB(BR_CHOICE) = 8;                                            /* focus BEGIN MISSION */
    u8 svc;
    if (DSB(0x626C) == 8) svc = DSB((u16)(0x6475 + (s16)DSW(0x62E5) * 0x14));
    else if (DSB(0x626C) == 9) svc = DSB((u16)(0x64B3 + (s16)DSW(0x62E5) * 0x12));
    else svc = DSB((u16)(0x62C5 + (DSW(0x6263) ? 1 : 2) * 0x90));
    DSB(BR_SIDE) = (u8)service_side(svc);
    for (;;) {
        screen_open(0x15, UFP(brief_draw_bg), 0x161, 1);
        hs_set_table(fdat(BR_TBL), 0x0B);
        hs_focus((u16)(s16)(s8)DSB(BR_CHOICE));
        hs_draw_all();
        menu_run(UFP(brief_frame));
        ui_busy();
        if (DSB(BR_CHOICE) == 5) {                                 /* REALISM PANEL */
            if (DSB(0x626B) == 2) {                                /* historic: treated like a single mission */
                DSB(0x626B) = 0;
                realism_run();
                DSB(0x626B) = 2;
            } else {
                realism_run();
            }
            continue;
        }
        if (DSB(BR_CHOICE) == 6) {                                 /* FLIGHT MAP */
            u8 ff = (historic || variant > -1) ? 1 : 0;
            if (!historic) strtab_free();
            DSW(BR_FMAP) = fmap_prepare(ff);
            u16 p = DSW(BR_FMAP);
            fltmap_show(DSW(p), DSW(p + 2), DSW(p + 4), DSW(p + 6), DSB(0x626D));
            fmap_release(ff);
            if (!historic) strtab_load(make_name(0x091D /* "brief" */, -1, 0x0923 /* "dat" */));
            continue;
        }
        break;
    }
    if (historic) {
        histbf_close();
        histov_close();
        histnm_close();
    } else {
        strtab_free();
    }
    for (s16 k = 0; k < 4; k++) xfarfree(ds_far((u16)(BR_WEATHER + (k << 2))));
    if (DSB(BR_CHOICE) == 7) {
        preflight((u16)variant);                                   /* PREFLIGHT OPTIONS */
    } else if (DSB(BR_CHOICE) == 8) {
        DSB(BR_BEGIN) = 1;                                         /* BEGIN MISSION */
    } else if (DSB(BR_CHOICE) == 4) {
        DSW(BR_VARIANT) = 0xFFFF;                                  /* DECLINE / Esc */
        DSB(BR_BEGIN) = 0;
    }
}

/* PS 0c1d:06c0 brief_init — dialogs_init */
void brief_init(void)
{
    hs_default_links(fdat(BR_TBL), 0x0B);
}

__attribute__((constructor)) static void brief_codeptrs(void)
{
    UREG(brief_subst);
    UREG(brief_draw_text);
    UREG(brief_draw_overview);
    UREG(brief_draw_officer);
    UREG(brief_draw_title);
    UREG(brief_draw_weather);
    UREG(brief_draw_label);
    UREG(brief_btn_cb);
    UREG(brief_draw_bg);
    UREG(brief_frame);
}
