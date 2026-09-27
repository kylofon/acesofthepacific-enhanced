/* shell_ui part B: fltdeck.c (PS 0787) — the flight deck, the career hub between missions
 * (shell_ui_flight.md §2). Screens 38–41 FLTDK0–FLTDK3, hot-spot table 49e3:0010 (8 items). */
#include "shell_ui/sui_b_priv.h"

#define FD_TBL      0x49e3, 0x0010
#define FD_LAYOUT   0x427E   /* FLTDECK<m>.DAT: n × {s16 x, s16 y, u16 screen} */
#define FD_LOADED   0x0614   /* never set: reloaded every time */
#define FD_CHOICE   0x42C0   /* u8: pressed button */

/* PS 0787:0006 fd_load_layout — shell_ui_flight.md §2 */
static void fd_load_layout(void)
{
    if (DSB(FD_LOADED) == 0) {
        u16 f = my_fopen(make_name(0x0615 /* "fltdeck" */, (s16)(s8)module_active(), 0x061D /* "dat" */),
                         0x0621 /* "rb" */);
        if (f == 0) ufatal(0x49e3, 0x54);
        u16 m = stk_mark();
        u16 n = stk_alloc(2);
        my_fread(n, 2, 1, f);
        if ((s16)(s8)DSB(0x4776) >= (s16)DSW(n)) ufatal(0x49e3, 0x56);
        my_fread(FD_LAYOUT, 6, (s16)DSW(n), f);
        stk_release(m);
        my_fclose(f);
    }
    u16 r = (u16)((s16)(s8)DSB(0x4776) * 6);
    FarPtr it = hs_item(FD_TBL, 0);
    uww(it, HS_X, DSW(FD_LAYOUT + r));
    uww(it, HS_Y, DSW(FD_LAYOUT + 2 + r));
}

/* PS 0787:00e7 fd_draw_window */
static void fd_draw_window(void)
{
    draw_titled_panel();
    draw_rivets((u16)(DSS(RX) + 3), (u16)(DSS(RY) + 18), 21);
    draw_rivets((u16)(DSS(RX) + DSS(RW) - 6), (u16)(DSS(RY) + 18), 21);
}

/* PS 0787:0129 fd_btn_cb — plays the press sound a second time (btn_cb already did) */
static void fd_btn_cb(void)
{
    if (btn_cb()) {
        snd_play(0x18);
        DSB(FD_CHOICE) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0787:0151 fd_frame */
static void fd_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 2) DSB(FD_CHOICE) = 7;
}

/* PS 0787:0181 flight_deck — shell_ui_flight.md §2 */
void flight_deck(void)
{
    u8 redraw = 1;
    DSB(0x4F22) = 1;                  /* preflight first entry */
    DSB(0x4356) = 1;                  /* in a career */
    fd_load_layout();
    DSB(FD_CHOICE) = 1;
    for (;;) {
        if (redraw)
            screen_show(DSW((u16)(FD_LAYOUT + 4 + (s16)(s8)DSB(0x4776) * 6)), 0x8D, 1);
        else
            redraw = 1;
        hs_set_table(hs_item(FD_TBL, 0), 8);
        hs_focus((u16)(s16)(s8)DSB(FD_CHOICE));
        hs_draw_all();
        menu_run(UFP(fd_frame));
        ui_busy();
        switch ((s8)DSB(FD_CHOICE)) {
        case 1: {                                            /* FLY NEXT MISSION */
            ps_rng_seed(DSW(0x42C1));
            s8 m = career_pick_mission();
            s16 v;
            if (DSB(0x626B) == 2) v = m;                     /* historic mission of the campaign */
            else { mission_build(m, (s8)DSB(0x4776), 1); v = -1; }
            briefing((u16)v);
            if (DSB(0x43D6)) return;                         /* BEGIN chosen: the caller flies */
            ps_mission_free_3164();
            break;
        }
        case 2: squadinf(1); break;                          /* SQUADRON INFO */
        case 3: bigmap_show(); break;                        /* VIEW THE WAR MAP */
        case 4: pilot_record(); break;                       /* VIEW PILOT RECORD */
        case 5: view_vehicle(DSW(0x478A), (u16)(s16)(s8)DSB(0x4776), 0); break;   /* VIEW AIRCRAFT */
        case 6: {                                            /* BACKUP CAREER */
            u16 mk = stk_mark();
            u16 name = stk_alloc(16);
            DSB(name) = 0;
            cursor_flush(DSW(PTR_X), DSW(PTR_Y));
            if ((u8)text_entry(1, name, 15, 0)) {
                if ((s16)DSW(0x4A18) >= 13) {                /* roster full: delete one first */
                    if ((u8)roster_manage(1)) pilot_backup(name);
                } else {
                    redraw = 0;
                    pilot_backup(name);
                }
            }
            stk_release(mk);
            break;
        }
        case 7:                                              /* RETURN TO MAIN / Esc */
            snd_play(0x3EA);
            DSB(0x4356) = 0;
            return;
        default: break;
        }
    }
}

/* PS 0787:031b fd_init - called from once-init 0a71:01be */
void fd_init(void)
{
    hs_default_links(hs_item(FD_TBL, 0), 8);
}

__attribute__((constructor)) static void fltdeck_codeptrs(void)
{
    UREG(fd_draw_window);
    UREG(fd_btn_cb);
    UREG(fd_frame);
}
