/* shell_ui: the main menu (PS segment 04df, screen 11 MAINMENU, table 4987:0000) —
 * shell_ui_options.md §1. */
#include "shell_ui/sui_a_priv.h"

#define TBL_MM      fd(0x4987, 0x0000)   /* 7 items */
#define MM_CHOICE   0x4224               /* s8: button pressed (item index) */
#define MM_OTHER    0x4225               /* s8: OTHER OPTIONS choice */
#define MISSION_KIND 0x626B              /* shell_data: mission kind of the header */

/* PS 04df:0002 mm_credits: INTACE6.TTM, then the main menu screen re-opened */
void mm_credits(void)
{
    ttm_play_file(make_name(0x04E8 /* "intace6" */, -1, 0x04F0 /* "ttm" */), -1, 0, 0, 0x1CD,
                  codeptr_far(PS_FN_ttm_skip_check));
    screen_open(11, fnul(), 0, 1);
}

/* PS 04df:004e mm_btn_cb */
void mm_btn_cb(void)
{
    if (btn_cb()) {
        DSB(MM_CHOICE) = DSB(T_IDX);
        menu_set_result(1);
    }
}

/* PS 04df:006c mm_other_pick(k): callback of the OTHER OPTIONS list */
void mm_other_pick(s16 k)
{
    DSB(MM_OTHER) = (u8)k;
    cursor_hide();
}

/* PS 04df:007c mm_other_cb: PREFERENCES / WATCH DEMO / CREDITS */
void mm_other_cb(void)
{
    if (!btn_cb()) return;
    ps_strings_load_24e3(make_name(0x04F4 /* "sftbtns" */, 0, 0x04FC /* "dat" */));
    fww(cur_item(), IT_USER, 3);
    DSB(MM_OTHER) = 0xFF;
    choose_run(FNP(mm_other_pick), 0, 0x61);
    cursor_show();
    hs_restart();
    DSB(MM_CHOICE) = 4;
    menu_set_result(1);
    strings_free();
}

/* PS 04df:00e8 mm_btn7_cb (not referenced by the table) */
void mm_btn7_cb(void)
{
    fww(cur_item(), IT_LABEL, str_get(7));
    if (btn_cb()) {
        DSB(MM_CHOICE) = 5;
        menu_set_result(1);
    }
}

/* PS 04df:0117 mm_draw_window */
void mm_draw_window(void)
{
    draw_titled_panel();
    draw_rivets((u16)(RXs + 3), (u16)(RYs + 18), 18);
    draw_rivets((u16)(RXs + RWs - 6), (u16)(RYs + 18), 18);
}

/* PS 04df:0159 mm_frame: Esc does nothing here */
void mm_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
    if (menu_get_result() == 2) menu_set_result(0);
}

/* PS 04df:018d main_menu: returns only for EXIT TO DOS */
void main_menu(void)
{
    DSB(MM_CHOICE) = 1;
    for (;;) {
        screen_show(11, 0x87, 1);
        hs_set_table(TBL_MM, 7);
        hs_focus((u16)(s16)DSC(MM_CHOICE));
        hs_draw_all();
        menu_run(FNP(mm_frame));
        ui_busy();
        cursor_show();
        switch (DSC(MM_CHOICE)) {
        case 1: flymenu_run(); break;
        case 2: career_menu_run(); break;
        case 3: {
            s8 c = vwhich_run();
            if (c == 0) {
                s8 v = (s8)fly_for_choose(2);
                if (v != -1) view_vehicle(0xFFFF, (u16)(u8)v, 0);
            } else if (c == 1) {
                s8 v = (s8)fly_for_choose(3);
                if (v != -1) view_ship((u16)(u8)v);
            }
            break;
        }
        case 4:
            switch (DSC(MM_OTHER)) {
            case 0: prefs_screen(); break;
            case 1: DSB(MISSION_KIND) = 4; fly_mission(0); break;
            case 2: mm_credits(); snd_play(0x3EA); break;
            default: break;
            }
            break;
        case 5: DSB(MISSION_KIND) = 3; fly_mission(0); break;
        case 6: return;
        default: break;
        }
    }
}

/* PS 04df:02be mm_init */
void mm_init(void) { hs_default_links(TBL_MM, 7); }

__attribute__((constructor)) static void mainmenu_codeptrs(void)
{
    REG(mm_credits);
    REG(mm_btn_cb);
    REG(mm_other_pick);
    REG(mm_other_cb);
    REG(mm_btn7_cb);
    REG(mm_draw_window);
    REG(mm_frame);
}
