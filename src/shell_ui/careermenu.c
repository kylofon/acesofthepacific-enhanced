/* shell_ui: the CAREER MENU (PS segment 0607, screen 45, table 49b1:0000) — shell_ui_debrief.md §4. */
#include "shell_ui/sui_a_priv.h"

#define TBL_CAREER fd(0x49B1, 0x0000)   /* 5 items */
#define FLY_NOW    0x43D6               /* u8: BEGIN MISSION pressed (flight deck / briefing) */

/* PS 0607:000d career_draw_window */
void career_draw_window(void)
{
    draw_titled_panel();
    draw_rivets((u16)(RXs + 3), (u16)(RYs + 18), 14);
    draw_rivets((u16)(RXs + RWs - 6), (u16)(RYs + 18), 14);
}

/* PS 0607:004f career_btn_cb */
void career_btn_cb(void)
{
    if (btn_cb()) {
        DSW(T_DISP_RES) = DSW(T_IDX);
        menu_set_result(1);
    }
}

/* PS 0607:006d career_frame: CONTINUE with no active pilot / BEST CAREERS with an empty list refused */
void career_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
    if (menu_get_result() == 0) return;
    u16 msg;
    if (DSW(T_DISP_RES) == 2) {
        s16 n = 0;
        for (s16 i = 0; i < DSS(PS_DS_roster_count); i++)
            if (rd8(SEG(0x4B4C), (u16)(i * 0x7A + 0x32)) == 0) n++;
        if ((n != 0 || module_latest() != 0) && DSS(PS_DS_roster_count) != 0) return;
        msg = 6;
    } else if (DSW(T_DISP_RES) == 3) {
        if (DSW(PS_DS_topace_count) != 0) return;
        msg = 7;
    } else {
        return;
    }
    req_ok(0, 0, msg, 8);
    menu_set_result(0);
    DSW(T_DISP_RES) = 4;
}

/* the career world files around the flight deck */
static void career_open_files(bool new_career)
{
    if (new_career) {
        station_open();
        bigmap_show();
        ord_open();
        ace_open();
        air_open();
        squad_open();
    } else {
        station_open();
        ord_open();
        squad_open();
        ace_open();
        air_open();
    }
}

/* PS 0607:011d career_menu_run */
void career_menu_run(void)
{
    u8 again = 1;
    do {
        screen_show(45, 0x396, 1);
        hs_set_table(TBL_CAREER, 5);
        DSW(T_DISP_RES) = 4;
        hs_draw_all();
        menu_run(FNP(career_frame));
        ui_busy();
        switch (DSW(T_DISP_RES)) {
        case 1:                                                /* START A CAREER */
            if (!(u8)pilot_new()) break;
            career_open_files(true);
            rng_seed_time();
            DSW(PS_DS_career_day_seed) = rng_count();
            snd_stop(0x3EA);
            snd_play(0x3F2);
            flight_deck();
            if (DSB(FLY_NOW) == 0) {
                snd_stop(0x3F2);
                snd_play(0x3EA);
                squad_close();
                air_close();
                ace_close();
                ord_close();
                station_close();
            } else {
                squad_close();
                fly_mission(1);
            }
            again = 0;
            break;
        case 2:                                                /* CONTINUE A CAREER */
            if ((u8)roster_pick() && (DSS(0x4790) > -1 || (u8)campsel_run(0))) {
                rng_seed_time();
                DSW(PS_DS_career_day_seed) = rng_count();
                snd_stop(0x3EA);
                snd_play(0x3F2);
                career_open_files(false);
                flight_deck();
                if (DSB(FLY_NOW) == 0) {
                    snd_stop(0x3F2);
                    snd_play(0x3EA);
                    air_close();
                    ace_close();
                    squad_close();
                    ord_close();
                    station_close();
                } else {
                    squad_close();
                }
                again = 0;
            }
            if (DSB(FLY_NOW)) fly_mission(1);
            break;
        case 3: best_careers(); break;                         /* VIEW BEST CAREERS */
        case 4: again = 0; break;                              /* RETURN TO MAIN */
        default: break;
        }
    } while (again);
}

/* PS 0607:02f5 career_init */
void career_init(void) { hs_default_links(TBL_CAREER, 5); }

__attribute__((constructor)) static void careermenu_codeptrs(void)
{
    REG(career_draw_window);
    REG(career_btn_cb);
    REG(career_frame);
}
