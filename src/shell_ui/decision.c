/* shell_ui: CAREER DECISION (PS segment 144e, screen 47, table 4c2a:0011, 5 items) —
 * shell_ui_mission.md §6. */
#include "shell_ui/sui_a_priv.h"

#define TBL_DEC    fd(0x4C2A, 0x0011)
#define DEC_VAR    0x4EF2                /* s8: (end_of_set ? 2 : 0) + (other_sets ? 1 : 0) */
#define DEC_LINES  0x4EF3                /* far char *[4] */

/* PS 144e:0004 dec_draw_text */
void dec_draw_text(void)
{
    gfx_font_select((s16)DSW(T_FONT3));
    s16 y = RYs;
    for (s16 i = 0; i < 4; i++, y = (s16)(y + 9)) {
        FarPtr l = ds_far((u16)(DEC_LINES + i * 4));
        if (far_is_null(l)) break;
        u16 s = strpool_copy(l).off;
        text_engrave(s, 0xC1, 0xB1, 0xC0, ps_text_center_x(s, (u16)RXs, (u16)RWs), (u16)y);
    }
}

/* PS 144e:0082 dec_btn_cb: 0 retire, 1 transfer, else the variant's result for item 2 */
void dec_btn_cb(void)
{
    if (!btn_small_cb(0)) return;
    if (DSW(T_IDX) == 4) DSW(T_DISP_RES) = 0;
    else if (DSW(T_IDX) == 3) DSW(T_DISP_RES) = 1;
    else DSS(T_DISP_RES) = DSC((u16)(DSC(DEC_VAR) + 0x0EE0));
    menu_set_result(1);
}

/* PS 144e:00ca dec_layout */
static void dec_layout(void)
{
    FarPtr t = fd(0x4C2A, 0);
    s16 v = DSC(DEC_VAR);
    fww(t, 0x17, DSW((u16)(v * 2 + 0x0EBC)));
    for (s16 i = 0; i < 3; i++) {
        u16 o = (u16)(i * IT_SIZE);
        s8 y = DSC((u16)(v * 3 + i + 0x0EC4));
        if (y == 0) {
            fbw(t, (u16)(o + 0x49), 0);
            fbw(t, (u16)(o + 0x4A), 0);
        } else {
            fww(t, (u16)(o + 0x3F), (u16)(s16)y);
            fbw(t, (u16)(o + 0x49), 0x74);
            fbw(t, (u16)(o + 0x4A), 0x82);
            if (i < 2) fww(t, (u16)(o + 0x4B), DSW((u16)(v * 4 + i * 2 + 0x0ED0)));
        }
    }
    for (s16 i = 1; i < 4; i++) ds_far_wr((u16)(DEC_LINES + i * 4), fnul());
    if (v < 2) {
        ds_far_wr(DEC_LINES, scd_string((s16)DSW(0x0EEA)));
    } else {
        u16 m = stk_mark();
        u16 buf = stk_alloc(80);
        u16 f = my_fopen(make_name(0x0EEC /* "CSET" */, DSC(0x4779), 0x0EF1 /* "dat" */), 0x0EF5 /* "rb" */);
        if (!f) assert_line(0x4C2A, 0xF4);
        my_fread(buf, 1, 0x50, f);
        my_fclose(f);
        ds_far_wr(DEC_LINES, scd_string((s16)DSW(0x0EE4)));
        FarPtr name = xfarmalloc(0x50);
        ds_far_wr(DEC_LINES + 4, name);
        fstrcpy_near_src(name, buf);
        ds_far_wr(DEC_LINES + 8, scd_string((s16)DSW(0x0EE8)));
        if (v == 3) ds_far_wr(DEC_LINES + 12, scd_string((s16)DSW(0x0EEA)));
        stk_release(m);
    }
    hs_default_links(TBL_DEC, 5);
}

/* PS 144e:027b dec_frame */
void dec_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 144e:029c career_decision(end_of_set, other_sets) -> 0 retire, 1 transfer, 2 continue */
u16 career_decision(u16 end_of_set, u16 other_sets)
{
    DSB(DEC_VAR) = (u8)(((u8)end_of_set != 0) * 2 + ((u8)other_sets != 0));
    screen_show(47, 0xE5, 1);
    dec_layout();
    hs_set_table(TBL_DEC, 5);
    hs_draw_all();
    menu_run(FNP(dec_frame));
    ui_busy();
    return DSB(T_DISP_RES);
}

__attribute__((constructor)) static void decision_codeptrs(void)
{
    REG(dec_draw_text);
    REG(dec_btn_cb);
    REG(dec_frame);
}
