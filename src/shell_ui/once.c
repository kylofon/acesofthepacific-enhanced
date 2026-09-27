/* shell_ui: one-time PS initialisation ("once", segment 0a71) — shell_ui_options.md §8. */
#include "shell_ui/sui_a_priv.h"

void fd_init(void);          /* shell_ui/fltdeck.c (PS 0787:031b) */

/* PS 0a71:0001 fonts_load: FONT1..4 and TALK -> DS:4222..421A */
void ps_fonts_load(void)
{
    DSW(T_FONT1) = font_load(make_name(0x0770, -1, 0x0776));
    DSW(T_FONT2) = font_load(make_name(0x077A, -1, 0x0780));
    DSW(T_FONT3) = font_load(make_name(0x0784, -1, 0x078A));
    DSW(T_FONT4) = font_load(make_name(0x078E, -1, 0x0794));
    DSW(T_FONT_TALK) = font_load(make_name(0x0798, -1, 0x079D));
    if (!DSW(T_FONT1) || !DSW(T_FONT2) || !DSW(T_FONT3) || !DSW(T_FONT4) || !DSW(T_FONT_TALK))
        assert_line(0x4A94, 0x2F);
    gfx_font_select((s16)DSW(T_FONT1));
}

/* PS 0a71:00eb parts_load: PARTS.BMP and SYMBOLS.BMP frame tables */
void parts_load(void)
{
    DSW(T_PARTS) = bmp_load_planes(make_name(0x07A1, -1, 0x07A7));
    DSW(T_SYMBOLS) = bmp_load_planes(make_name(0x07AB, -1, 0x07B3));
    if (!DSW(T_PARTS) || !DSW(T_SYMBOLS)) assert_line(0x4A94, 0x40);
}

/* PS 0a71:015c dialogs_init: MREAL.PRF and every screen's table initialiser */
void dialogs_init(void)
{
    realism_load(0x07B7 /* "mreal.prf" */, PS_DS_mreal_prf);
    choose_init();
    entry_init();
    lbox_init();
    pick_init();
    camp_init();
    aw_load_ttms();
    mm_init();
    career_init();
    nw_init();
    pr_init();
    best_init();
    events_init_fonts();
    fm2_links_init();
    brief_init();
    bm_init();
    pf_init();
    fd_init();               /* (the spec's correction is wrong: 0a71:01ac calls 0787:031b) */
}
