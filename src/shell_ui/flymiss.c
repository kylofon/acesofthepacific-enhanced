/* shell_ui part B: flymiss.c (PS 0b16) — the FLY SINGLE MISSION menu (shell_ui_options.md §2).
 * Screen 49 MAINMENU/FLYMISS, hot-spot table 4ac1:0000 (15 items). */
#include "shell_ui/sui_b_priv.h"

#define FM_TBL      0x4ac1, 0x0000
#define FM_CHOICE   0x4368      /* u8: pressed button */
#define FM_TYPES    0x0820      /* u8[12]: [0] = custom missions exist, [1..11] = mission type (FFh historic) */

/* PS 0b16:000e fm_draw_window */
static void fm_draw_window(void)
{
    draw_titled_panel();
    draw_rivets((u16)(DSS(RX) + 3), (u16)(DSS(RY) + 0x12), 0x15);
    draw_rivets((u16)(DSS(RX) + DSS(RW) - 6), (u16)(DSS(RY) + 0x12), 0x15);
}

/* PS 0b16:0050 fm_btn_cb */
static void ps_fm_btn_cb_0b16(void)
{
    if (btn_cb()) {
        DSB(FM_CHOICE) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0b16:006e fm_set_links(custom) — items {6, 7, 11, 13, 14} (DS:082C) from DS:0831 / DS:0845 */
static void fm_set_links(u8 custom)
{
    for (s16 k = 0; k < 5; k++) {
        FarPtr it = hs_item(FM_TBL, (s8)DSB((u16)(0x082C + k)));
        u16 src = (u16)((custom ? 0x0831 : 0x0845) + (k << 2));
        for (u16 d = 0; d < 4; d++) uwb(it, (u16)(HS_LINKS + d), DSB((u16)(src + d)));
    }
}

/* PS 0b16:00ca fm_layout — CUSTOM MISSIONS only when CMS\*.CMD files exist (none shipped) */
static void fm_layout(void)
{
    u8 custom;
    DSB(FM_TYPES) = (u8)custmiss_exist();
    if (DSB(FM_TYPES)) {
        uww(hs_item(FM_TBL, 12), HS_Y, 0x7A);
        uwp(hs_item(FM_TBL, 12), HS_CB, codeptr_far(UFN(0x0b16, 0x0050)));
        uww(hs_item(FM_TBL, 13), HS_Y, 0x94);
        uww(hs_item(FM_TBL, 14), HS_X, 0x0A);
        custom = 1;
    } else {
        uwb(hs_item(FM_TBL, 12), HS_MASK, 0);
        uwb(hs_item(FM_TBL, 12), HS_FLAGS, 0);
        uww(hs_item(FM_TBL, 13), HS_Y, 0x7A);
        uww(hs_item(FM_TBL, 14), HS_X, 0x54);
        custom = 0;
    }
    fm_set_links(custom);
}

/* PS 0b16:013a fm_frame — steps the shell RNG every frame */
static void ps_fm_frame_0b16(void)
{
    ps_rng_next(0);
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 0) return;
    if (menu_get_result() == 2) {
        DSB(FM_CHOICE) = 0x0E;
    } else if (DSB(FM_CHOICE) == 0x0D && DSW(0x433F) == 0) {      /* BEST MISSIONS without records */
        req_ok(0, 0, 9, 8);
        menu_set_result(0);
    }
}

static void fm_world_open(void)
{
    ord_open();
    station_open();
    ace_open();
    squad_open();
    air_open();
}

/* PS 0b16:01a7 flymenu_run — only BEST MISSIONS comes back here; everything else returns */
void flymenu_run(void)
{
    DSB(0x4F22) = 1;
    fm_layout();
    DSB(FM_CHOICE) = 1;
    do {
        screen_show(0x31, 0x396, 1);
        hs_set_table(fdat(FM_TBL), 0x0F);
        hs_focus((u16)(s16)(s8)DSB(FM_CHOICE));
        hs_draw_all();
        menu_run(codeptr_far(UFN(0x0b16, 0x013a)));
        ui_busy();
        s8 k = (s8)DSB(FM_CHOICE);
        if (k == 0x0D) {
            best_missions();
        } else if (k == 0x0C) {
            /* BEGIN MISSION of a custom mission does nothing in v1.2: both branches continue alike */
            (void)custom_missions();
        } else if (k >= 1 && k <= 0x0B) {
            s8 t = (s8)DSB((u16)(FM_TYPES + k));
            if (t == -1) {
                flyhist_run();
            } else if (t == 0) {                                         /* DOGFIGHT A FAMOUS ACE */
                fm_world_open();
                u8 ace = (u8)dogace_choose();
                if (ace != 0xFF) {
                    if ((u8)flymiss2_setup(0, ace)) {
                        squad_close();
                        DSB(0x4B89) = ace;
                        fly_mission(0);
                    }
                    ps_mission_free_3164();
                }
                air_close(); squad_close(); ace_close(); station_close(); ord_close();
            } else {
                fm_world_open();
                u8 v = (u8)fly_for_choose(0);                             /* "WHO WILL YOU FLY FOR?" */
                if (v != 0xFF && (u8)flymiss2_setup((u16)(s16)t, v)) {
                    squad_close();
                    fly_mission(0);
                }
                ps_mission_free_3164();
                air_close(); squad_close(); ace_close(); station_close(); ord_close();
            }
        }
    } while ((s8)DSB(FM_CHOICE) == 0x0D);
}

__attribute__((constructor)) static void flymiss_codeptrs(void)
{
    UREG(fm_draw_window);
    codeptr_register(PROG_PS, UFN(0x0b16, 0x0050), (CodeFn)ps_fm_btn_cb_0b16);
    codeptr_register(PROG_PS, UFN(0x0b16, 0x013a), (CodeFn)ps_fm_frame_0b16);
}
