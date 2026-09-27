/* shell_ui: VIEW WHICH? (PS segment 05fa, screen 55, table 49ab:0000) — shell_ui_debrief.md §3. */
#include "shell_ui/sui_a_priv.h"

#define TBL_VWHICH fd(0x49AB, 0x0000)   /* 4 items, static links */

/* PS 05fa:0003 vwhich_draw_window */
void vwhich_draw_window(void)
{
    draw_side_riveted_panel();
    gfx_font_select((s16)DSW(T_FONT4));
    text_engrave_center(fw(cur_item(), IT_LABEL), 0xC1, 0xB1, 0xC0);
}

/* PS 05fa:0033 vwhich_btn_cb */
void vwhich_btn_cb(void)
{
    if (!btn_cb()) return;
    if (DSW(T_IDX) == 3) {
        menu_set_result(2);
    } else {
        DSW(T_DISP_RES) = (u16)(DSW(T_IDX) - 1);
        menu_set_result(1);
    }
}

/* PS 05fa:005e vwhich_frame */
void vwhich_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 05fa:007f vwhich_run -> 0 airplanes, 1 ships, FFh cancelled */
s8 vwhich_run(void)
{
    screen_show(55, 0x2F, 1);
    hs_set_table(TBL_VWHICH, 4);
    hs_draw_all();
    u16 r = menu_run(FNP(vwhich_frame));
    ui_busy();
    return r == 2 ? (s8)-1 : (s8)DSB(T_DISP_RES);
}

__attribute__((constructor)) static void viewwhich_codeptrs(void)
{
    REG(vwhich_draw_window);
    REG(vwhich_btn_cb);
    REG(vwhich_frame);
}
