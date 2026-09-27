/* shell_ui: the REALISM PANEL (PS segment 0a8d, screen 51, table 4a95:0000, 32 items) —
 * shell_ui_options.md §3. Settings: 16 bytes at DS:4357 (MREAL.PRF / the career pilot's copy). */
#include "shell_ui/sui_a_priv.h"

void realism_save(u16 name_ds, u16 buf_ds);        /* platform/config.c (PS 1193:0840) */

#define TBL_REAL    fd(0x4A95, 0x0000)
#define REAL        PS_DS_mreal_prf                 /* 0x4357: the settings being edited */
#define REAL_SAVED  0x4346                          /* copy taken on entry (RESTORE) */
#define REAL_CAREER 0x4356                          /* u8: editing the career pilot's settings */
#define PILOT_REAL  0x47A9                          /* the career pilot record's realism */
#define WEIGHTS     0x07C2                          /* s8[16] */
#define PRESETS     0x07D2                          /* u8[3][16] */
#define MISSION_KIND PS_DS_mission_header           /* 0 = single mission */

/* PS 0a8d:0005 real_weight_sum */
static s16 real_weight_sum(void)
{
    s16 s = 0;
    for (s16 i = 0; i < 16; i++) s = (s16)(s + DSC((u16)(WEIGHTS + i)));
    return s;
}

/* PS 0a8d:0023 realism_percent(s) */
static s16 ps_realism_percent(u16 s)
{
    s16 lo, hi;
    if (DSB((u16)(s + 7)) == 0) { hi = 20; lo = 0; }
    else { hi = 100; lo = 20; }
    s16 sum = 0;
    for (s16 i = 0; i < 16; i++) {
        if (DSB((u16)(s + i)) == 0) continue;
        if (i == 11 || i == 10) sum = (s16)(sum + (s16)(DSC((u16)(WEIGHTS + i)) * DSC((u16)(s + i))) / 2);
        else sum = (s16)(sum + DSC((u16)(WEIGHTS + i)));
    }
    if (DSB(MISSION_KIND) == 0) sum = (s16)(sum + DSC(WEIGHTS + 6));
    return (s16)(lo + (s16)((hi - lo) * sum) / real_weight_sum());
}

/* PS 0a8d:00ba realism_current */
static s16 realism_current(void) { return ps_realism_percent(REAL); }

/* PS 0a8d:00ca real_show_model: item 28 */
static void real_show_model(void)
{
    FarPtr t = TBL_REAL;
    fww(t, 0x276, str_get((s16)(DSC(REAL + 11) + 12)));
    fbw(t, 0x274, 3);
}

/* PS 0a8d:00f1 real_show_combat: item 27 */
static void real_show_combat(void)
{
    FarPtr t = TBL_REAL;
    fww(t, 0x260, str_get((s16)(DSC(REAL + 10) + 15)));
    fbw(t, 0x25E, 3);
}

/* PS 0a8d:0118 real_sync_check(i): the check box item i+12 shows setting item[i].user */
static void real_sync_check(s16 i)
{
    FarPtr t = TBL_REAL;
    u16 o = (u16)(i * IT_SIZE);
    fww(t, (u16)(o + 0x118), DSB((u16)(REAL + fw(t, (u16)(o + IT_USER)))) != 0);
    fbw(t, (u16)(o + 0x114), 3);
}

/* PS 0a8d:015e real_refresh */
static void real_refresh(void)
{
    for (s16 i = 5; i < 15; i++) real_sync_check(i);
    fbw(TBL_REAL, 0x28A, 3);
    real_show_model();
    real_sync_check(13);
    real_show_combat();
}

/* PS 0a8d:0192 real_apply_preset(dst, p): limited fuel stays off in single missions */
void real_apply_preset(u16 dst_ds, u16 p)
{
    u16 src = (u16)(p * 16 + PRESETS);
    for (s16 i = 0; i < 16; i++)
        DSB((u16)(dst_ds + i)) = (i == 6 && DSB(MISSION_KIND) == 0) ? 0 : DSB((u16)(src + i));
}

/* PS 0a8d:01ca real_preset_cb */
void real_preset_cb(void)
{
    if (btn_cb()) {
        real_apply_preset(REAL, fw(cur_item(), IT_USER));
        real_refresh();
    }
}

/* PS 0a8d:01ee real_toggle_cb */
void real_toggle_cb(void)
{
    u16 k = fw(cur_item(), IT_USER);
    if (!btn_small_cb(0)) return;
    if (k == 10) {
        DSB(REAL + 10) = (u8)((s16)(DSC(REAL + 10) + 1) % 3);
        real_show_combat();
    } else if (k == 11) {
        DSB(REAL + 11) = (u8)((s16)(DSC(REAL + 11) + 1) % 3);
        real_show_model();
    } else if (k == 6 && DSB(MISSION_KIND) == 0) {
        DSB(REAL + 6) = 0;
    } else {
        u8 v = DSB((u16)(REAL + k)) == 0;
        DSB((u16)(REAL + k)) = v;
        FarPtr it = cur_item();
        fww(it, 0x118, v);
        fbw(it, 0x114, 3);
    }
    fbw(TBL_REAL, 0x28A, 3);
}

/* PS 0a8d:0298 real_accept_cb */
void real_accept_cb(void)
{
    if (btn_cb()) menu_set_result(1);
}

/* PS 0a8d:02b0 real_restore_cb */
void real_restore_cb(void)
{
    if (btn_cb()) {
        rtl_memcpy(REAL, REAL_SAVED, 16);
        real_refresh();
    }
}

/* PS 0a8d:02d6 real_draw_value */
void real_draw_value(void)
{
    draw_inset_box2();
    u16 l = fw(cur_item(), IT_LABEL);
    if (l) {
        DSB(TXT_FG) = 0xCC;
        DSB(TXT_FLAGS) = 1;
        gfx_font_select((s16)DSW(T_FONT3));
        gfx_text(l, (s16)ps_text_center_x(l, (u16)RXs, (u16)RWs), (s16)(RYs + 3));
    }
    fbw(cur_item(), IT_MASK, 0x40);
}

/* PS 0a8d:0339 real_draw_check */
void real_draw_check(void)
{
    draw_inset_box2();
    if (fw(cur_item(), IT_USER) != 0) sprite(parts(8), (s16)(RXs + 2), (s16)(RYs + 2), 0);
    fbw(cur_item(), IT_MASK, 0x40);
}

/* PS 0a8d:0377 real_draw_percent: OVERALL DIFFICULTY nn% */
void real_draw_percent(void)
{
    draw_popup_box();
    gfx_font_select((s16)DSW(T_FONT3));
    u16 s = str_get(18);
    text_engrave(s, 0xC1, 0xB1, 0xC0, ps_text_center_x(s, (u16)RXs, (u16)RWs), (u16)(RYs + 3));
    fww(cur_item(), IT_LABEL, str_int(0x411A, realism_current()));
    rect_save();
    RXs = 0xDE; RYs = 0x9A; RWs = 0x16; RHs = 10;
    real_draw_value();
    ps_rect_restore();
}

/* PS 0a8d:040f real_draw_frame: two panels joined by a tab-shaped outline */
void real_draw_frame(void)
{
    rect_save();
    RXs = (s16)(RXs + 2); RYs = (s16)(RYs + 2); RWs = (s16)(RWs - 3); RHs = (s16)(RHs - 0x21);
    box_c(0xB8, RXs, RYs, RWs, RHs);
    ps_rect_restore();
    rect_save();
    RXs = (s16)(RXs + 13);
    RYs = (s16)(RYs + RHs - 0x20);
    RWs = (s16)(RWs - 25);
    RHs = 31;
    box_c(0xB8, RXs, RYs, RWs, 31);
    ps_rect_restore();
    s16 x = RXs, y = RYs, w = RWs, h = RHs;
    line_c(0xC2, x, y, (s16)(x + w), y);
    line_c(0xB5, (s16)(x + 1), (s16)(y + 1), (s16)(x + w - 1), (s16)(y + 1));
    line_c(0xC0, x, (s16)(y + 1), x, (s16)(y + h - 31));
    gfx_line((s16)(x + w), (s16)(y + 1), (s16)(x + w), (s16)(y + h - 31));
    gfx_line((s16)(x + 11), (s16)(y + h - 30), (s16)(x + 11), (s16)(y + h));
    gfx_line((s16)(x + w - 11), (s16)(y + h - 30), (s16)(x + w - 11), (s16)(y + h));
    line_c(0xBF, (s16)(x + 1), (s16)(y + 2), (s16)(x + 1), (s16)(y + h - 31));
    gfx_line((s16)(x + w - 1), (s16)(y + 2), (s16)(x + w - 1), (s16)(y + h - 31));
    gfx_line((s16)(x + 12), (s16)(y + h - 30), (s16)(x + 12), (s16)(y + h));
    gfx_line((s16)(x + w - 12), (s16)(y + h - 30), (s16)(x + w - 12), (s16)(y + h));
    line_c(0xC4, (s16)(x + 11), (s16)(y + h), (s16)(x + w - 11), (s16)(y + h));
    gfx_line(x, (s16)(y + h - 29), (s16)(x + 10), (s16)(y + h - 29));
    gfx_line((s16)(x + w), (s16)(y + h - 29), (s16)(x + w - 10), (s16)(y + h - 29));
    line_c(0xC1, (s16)(x + 11), (s16)(y + h - 1), (s16)(x + w - 11), (s16)(y + h - 1));
    gfx_line(x, (s16)(y + h - 30), (s16)(x + 11), (s16)(y + h - 30));
    gfx_line((s16)(x + w), (s16)(y + h - 30), (s16)(x + w - 11), (s16)(y + h - 30));
    line_c(0xC0, (s16)(x + 11), (s16)(y + h - 2), (s16)(x + w - 11), (s16)(y + h - 2));
    gfx_line((s16)(x + 2), (s16)(y + h - 31), (s16)(x + 12), (s16)(y + h - 31));
    gfx_line((s16)(x + w - 2), (s16)(y + h - 31), (s16)(x + w - 12), (s16)(y + h - 31));
}

/* PS 0a8d:0785 real_frame */
void real_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 0a8d:07a6 real_draw_bg */
void real_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for(DSB(PS_DS_flights), DSW(PS_DS_mission_date), str_get(19));
}

/* PS 0a8d:07cb realism_run */
void realism_run(void)
{
    if (DSB(REAL_CAREER) == 0) realism_load(0x0802 /* "mreal.prf" */, REAL);
    else rtl_memcpy(REAL, PILOT_REAL, 16);
    rtl_memcpy(REAL_SAVED, REAL, 16);
    screen_open(51, FNP(real_draw_bg), 0x156, 1);
    real_refresh();
    hs_set_table(TBL_REAL, 32);
    hs_focus(31);
    hs_draw_all();
    menu_run(FNP(real_frame));
    ui_busy();
    u16 name;
    if (DSB(REAL_CAREER) == 0) {
        name = 0x0816;                                  /* "mreal.prf" */
    } else {
        rtl_memcpy(PILOT_REAL, REAL, 16);
        pilot_store();
        name = 0x080C;                                  /* "creal.prf" */
    }
    realism_save(name, REAL);
}

__attribute__((constructor)) static void realism_codeptrs(void)
{
    REG(real_preset_cb);
    REG(real_toggle_cb);
    REG(real_accept_cb);
    REG(real_restore_cb);
    REG(real_draw_value);
    REG(real_draw_check);
    REG(real_draw_percent);
    REG(real_draw_frame);
    REG(real_frame);
    REG(real_draw_bg);
}
