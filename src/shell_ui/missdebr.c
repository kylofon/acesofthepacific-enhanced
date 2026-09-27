/* shell_ui part B: missdebr.c (PS 0637) — MISSION DEBRIEFING (shell_ui_debrief.md §5).
 * Screen 22 MISSDEBR (palette only: the picture is not drawn), table 49b8:0011 (24 items). */
#include "shell_ui/sui_b_priv.h"

#define DB_SEG      0x49b8
#define DB_TBL      DB_SEG, 0x0011
#define DB_TEXT     0x0582      /* near: the sentence (a buffer on debrief_run's stack) */
#define DB_OFFICERS 0x426A
#define DB_ICONS    0x426C      /* DEBRIEF.BMP */
#define DB_VALUES   0x426E      /* near: 12 s32 (on debrief_run's stack) */

static s16 db_item_x(int i) { return us(hs_item(DB_TBL, i), HS_X); }
static s16 db_item_y(int i) { return us(hs_item(DB_TBL, i), HS_Y); }

/* PS 0637:000e debrief_draw_text */
static void debrief_draw_text(void)
{
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    flow_setup(3, 8, 1);
    flow_text(ds_ptr(DSW(DB_TEXT)), 1, codeptr_far(PS_FN_display_subst));
}

/* PS 0637:004d debrief_draw_officer — portrait of the player's service */
static void debrief_draw_officer(void)
{
    draw_inset_box2();
    s16 f = (s8)DSB(0x4B8E);
    gfx_sprite_flip(bmp_frame(DSW(DB_OFFICERS), f), (s16)(DSS(RX) + 2), (s16)(DSS(RY) + 2), 0);
}

/* PS 0637:0089 debrief_draw_victory_labels */
static void debrief_draw_victory_labels(void)
{
    s16 x0 = db_item_x(0), y0 = db_item_y(0);
    gfx_font_select((s16)DSW(F_FONT4));
    u16 s = str_get(1);                                    /* "CONFIRMED VICTORIES" */
    u16 tx = ps_text_center_x(s, DSW(RX), DSW(RW));
    text_engrave(s, 0xC1, 0xB1, 0xC0, tx, (u16)(DSS(RY) + 3));
    gfx_font_select((s16)DSW(F_FONT3));
    s16 ly = (s16)(y0 + db_item_y(13) - 10);
    s = str_get(2);                                        /* "PERSONAL" */
    text_engrave(s, 0xC1, 0xB1, 0xC0, (u16)(x0 + db_item_x(13) - 8), (u16)ly);
    s = str_get(3);                                        /* "FLIGHT" */
    text_engrave(s, 0xC1, 0xB1, 0xC0, (u16)(x0 + db_item_x(18) - 4), (u16)ly);
}

/* PS 0637:017c debrief_draw_open_box(open): 0 top, 1 right, 2 bottom, 3 left side open */
static void debrief_draw_open_box(s16 open)
{
    DSB(FILL_ON) = 1;
    DSB(FILL_C) = 0xB8;
    DSB(LINE_C) = 0xB8;
    gfx_rect(DSS(RX), DSS(RY), DSS(RW), DSS(RH));
    rect_save();
    s16 x = DSS(RX), y = DSS(RY);
    DSS(RW) = (s16)(DSS(RW) + x);                          /* R */
    DSS(RH) = (s16)(DSS(RH) + y);                          /* B */
    s16 R = DSS(RW), B = DSS(RH);
    if (open != 0) {
        uline(0xC2, (s16)(x + 1), y, R, y);
        uline(0xC3, (s16)(x + 1), (s16)(y + 1), R, (s16)(y + 1));
    }
    if (open != 1) {
        uline(0xC0, R, (s16)(y + 1), R, (s16)(B - 1));
        uline(0xC3, (s16)(R - 1), (s16)(y + 1), (s16)(R - 1), (s16)(B - 1));
    }
    if (open != 3) {
        uline(0xC0, x, (s16)(y + 1), x, (s16)(B - 1));
        uline(0xC3, (s16)(x + 1), (s16)(y + 1), (s16)(x + 1), (s16)(B - 1));
    }
    if (open != 2) {
        uline(0xC1, x, B, R, B);
        uline(0xC3, x, (s16)(B - 1), R, (s16)(B - 1));
    }
    ps_rect_restore();
}

/* PS 0637:02cf debrief_draw_left_box */
static void debrief_draw_left_box(void)
{
    debrief_draw_open_box(1);
    DSB(LINE_C) = 0xB8;
    s16 r = (s16)(DSS(RX) + DSS(RW)), b = (s16)(DSS(RY) + DSS(RH));
    gfx_line(r, DSS(RY), r, b);
    gfx_line((s16)(r - 1), (s16)(DSS(RY) + 1), (s16)(r - 1), (s16)(b - 1));
}

/* PS 0637:032c debrief_draw_victories_box */
static void debrief_draw_victories_box(void)
{
    debrief_draw_open_box(3);
    DSB(LINE_C) = 0xB8;
    gfx_line(DSS(RX), DSS(RY), DSS(RX), (s16)(DSS(RY) + DSS(RH) - 2));
    s16 yj = (s16)(db_item_y(0) + db_item_y(3));           /* = 94: the left edge only above the left box */
    uline(0xC3, (s16)(DSS(RX) + 1), (s16)(yj - 1), (s16)(DSS(RX) + 1), (s16)(DSS(RY) + 1));
    uline(0xC0, DSS(RX), (s16)(DSS(RY) + 1), DSS(RX), (s16)(yj - 1));
    debrief_draw_victory_labels();
    uline(0xC1, (s16)(DSS(RX) + 0x0D), (s16)(DSS(RY) + 0x13), (s16)(DSS(RX) + 0xC1), (s16)(DSS(RY) + 0x13));
    uline(0xB1, (s16)(DSS(RX) + 0x0D), (s16)(DSS(RY) + 0x14), (s16)(DSS(RX) + 0xC1), (s16)(DSS(RY) + 0x14));
}

/* PS 0637:040d debrief_draw_score_labels */
static void debrief_draw_score_labels(void)
{
    gfx_font_select((s16)DSW(F_FONT3));
    s16 lx = (s16)(db_item_x(0) + db_item_x(5));
    s16 y0 = db_item_y(0);
    u16 w = uw(hs_item(DB_TBL, 5), HS_W);
    u16 s = str_get(4);                                    /* "MISSION SCORE" */
    u16 tx = ps_text_center_x(s, (u16)lx, w);
    text_engrave(s, 0xC1, 0xB1, 0xC0, tx, (u16)(y0 + db_item_y(6) - 7));
    s = str_get(5);                                        /* "AIRCRAFT LOST" */
    tx = ps_text_center_x(s, (u16)lx, w);
    text_engrave(s, 0xC1, 0xB1, 0xC0, tx, (u16)(y0 + db_item_y(7) - 7));
}

/* PS 0637:04d8 debrief_draw_score_box */
static void debrief_draw_score_box(void)
{
    draw_popup_box();
    debrief_draw_score_labels();
}

/* PS 0637:04e6 debrief_draw_value — items < 8: k = item − 6, else k = item − 11 */
static void debrief_draw_value(void)
{
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x22);
    s16 idx = (s16)DSW(CUR_IDX);
    s16 k = idx < 8 ? (s16)(idx - 6) : (s16)(idx - 11);
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_COL) = 0xCC;
    rtl_ltoa((s32)DSL((u16)(DSW(DB_VALUES) + (k << 2))), buf, 10);
    s16 w = gfx_text_width(buf);
    gfx_text(buf, (s16)(DSS(RX) + DSS(RW) - w - 3), (s16)(DSS(RY) + 3));   /* flags as left: transparent */
    stk_release(mk);
}

/* PS 0637:0567 debrief_draw_value_box */
static void debrief_draw_value_box(void)
{
    draw_inset_box2();
    debrief_draw_value();
}

/* PS 0637:0575 debrief_draw_row_label — item 8 shows the enemy nation's flag */
static void debrief_draw_row_label(void)
{
    s8 i;
    if (DSW(CUR_IDX) == 8) i = (s8)((u8)service_side(DSB(0x4B8F)) + 4);
    else i = (s8)(DSB(CUR_IDX) + 0xF7);
    draw_inset_box2();
    gfx_sprite_flip(bmp_frame(DSW(DB_ICONS), i), DSS(RX), DSS(RY), 0);
    gfx_font_select((s16)DSW(F_FONT3));
    text_engrave(uw(cur_item(), HS_LABEL), 0xC1, 0xB1, 0xC0, (u16)(DSS(RX) + 0x1C), (u16)(DSS(RY) + 3));
}

/* PS 0637:05f6 debrief_continue_cb */
static void debrief_continue_cb(void)
{
    if (btn_cb()) menu_set_result(1);
}

/* PS 0637:060e debrief_build_values — 12 s32 from the SIM.OUT summary */
static void debrief_build_values(void)
{
    u16 v = DSW(DB_VALUES);
    static const u16 src[12] = { 0x4249, 0x422B, 0x423D, 0x4239, 0x423B, 0x4237, 0, 0x4245, 0x4241, 0x4243, 0x423F, 0 };
    for (int k = 0; k < 12; k++) {
        if (k == 6) DSL(v + 0x18) = (u32)((s32)DSL(v + 0x0C) + (s32)DSL(v + 0x14));      /* personal strikes */
        else if (k == 11) DSL(v + 0x2C) = (u32)((s32)DSL(v + 0x20) + (s32)DSL(v + 0x28)); /* flight strikes */
        else DSL((u16)(v + k * 4)) = (u32)(s32)(s16)DSW(src[k]);
    }
}

/* PS 0637:069e debrief_build_text(buf) — DEBRIEF.DAT group by mission type, sentence by result */
static void debrief_build_text(u16 buf)
{
    s8 g = 0;
    strtab_load(make_name(0x0584 /* "debrief" */, -1, 0x058C /* "dat" */));
    switch ((u16)(s16)(s8)DSB(0x4B88)) {
    case 0: g = 0; break;
    case 1: g = 1; break;
    case 2: g = 2; break;
    case 3: g = 3; break;
    case 4: g = 4; break;
    case 5: g = DSB(0x4B89) == 4 ? 10 : 11; break;        /* carrier landing / training */
    case 6: g = 5; break;
    case 7: g = 6; break;
    case 8: g = 8; break;
    case 9: g = 9; break;
    case 10: g = 7; break;
    default: ufatal(DB_SEG, 0x228); break;
    }
    DSW(DB_TEXT) = buf;
    if (DSB(0x4B88) == 5 && DSB(0x4B89) != 4) {           /* training (not carrier landing) */
        strcpy_far_src(DSW(DB_TEXT), strings_get(g, 0));
        strtab_free();
        strtab_load(make_name(0x0590 /* "optional" */, -1, 0x0599 /* "dat" */));   /* stays loaded for %x */
    } else {
        strcpy_far_src(DSW(DB_TEXT), strings_get(g, (s16)(DSW(0x4227) - 1)));
        strtab_free();
    }
}

/* PS 0637:07d2 debrief_draw_bg */
static void debrief_draw_bg(void)
{
    screen_clear_panel();
    u16 t = str_get(0);                                    /* " MISSION DEBRIEFING " */
    draw_title_bar_for(DSB(0x4B8E), DSB(0x4B87) == 1 ? DSW(0x477B) : 0, t);
}

/* PS 0637:0809 debrief_frame */
static void debrief_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
}

/* PS 0637:082a debrief_run(check_best) — callers: sim_end (1), best missions VIEW (0) */
void debrief_run(s8 check_best)
{
    u16 mk = stk_mark();
    u16 values = stk_alloc(0x30);
    u16 text = stk_alloc(0x12C);
    DSW(DB_VALUES) = values;
    DSW(DB_OFFICERS) = bmp_load_planes(make_name(0x059D /* "officers" */, -1, 0x05A6 /* "bmp" */));
    DSW(DB_ICONS) = bmp_load_planes(make_name(0x05AA /* "debrief" */, -1, 0x05B2 /* "bmp" */));
    debrief_build_values();
    debrief_build_text(text);
    screen_open(0x16, UFP(debrief_draw_bg), 0xB5, 1);
    hs_set_table(fdat(DB_TBL), 0x18);
    hs_draw_all();
    if (check_best) best_mission_insert();                 /* may show "Congratulations, you ranked..." */
    menu_run(UFP(debrief_frame));
    ui_busy();
    bmp_free(DSW(DB_OFFICERS));
    bmp_free(DSW(DB_ICONS));
    if (DSB(0x4B88) == 5 && DSB(0x4B89) != 4) strtab_free();
    stk_release(mk);
}

__attribute__((constructor)) static void missdebr_codeptrs(void)
{
    UREG(debrief_draw_text);
    UREG(debrief_draw_officer);
    UREG(debrief_draw_left_box);
    UREG(debrief_draw_victories_box);
    UREG(debrief_draw_score_box);
    UREG(debrief_draw_value_box);
    UREG(debrief_draw_row_label);
    UREG(debrief_continue_cb);
    UREG(debrief_draw_bg);
    UREG(debrief_frame);
}
