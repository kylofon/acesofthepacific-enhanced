/* AP 368f page/wipe/wait helpers and 4c9b fonts_load (video_misc.md). */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "sim_flow/sim_flow.h"
#include "symbols_ap.h"

/* ---------------------------------------------------------------- §1 page selection */

void write_back(void) { DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK); }   /* 368f:03b6 */
void read_back(void)  { DRVW(D_PAGE_READ)  = DRVW(D_PAGE_BACK); }   /* 368f:03c1 */
void write_bg(void)   { DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BG); }     /* 368f:03cc */
void read_bg(void)    { DRVW(D_PAGE_READ)  = DRVW(D_PAGE_BG); }     /* 368f:03d7 */
void write_front(void){ DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT); }  /* 368f:03e2 */
void read_front(void) { DRVW(D_PAGE_READ)  = DRVW(D_PAGE_FRONT); }  /* 368f:03ed */

/* 368f:01cf clip_full */
void ap_clip_full(void)
{
    DRVB(D_CLIP_ON) = 1;
    DRVB(D_FILL_ON) = 1;
    DRVW(D_CLIP_X1) = 0;
    DRVW(D_CLIP_Y1) = 0;
    DRVW(D_CLIP_X2) = (u16)(DRVW(D_SCR_W) - 1);
    DRVW(D_CLIP_Y2) = (u16)(DRVW(D_SCR_H) - 1);
}

/* 368f:01f2 */
void copy_screen(void) { gfx_copy_rect(0, 0, DRVS(D_SCR_W), DRVS(D_SCR_H)); }

/* ---------------------------------------------------------------- §2 countdown waits and the wipe */

/* 368f:008d — PORT: the floppy motor wait (RTL 0000:18d6) is a no-op */
void floppy_off(void) {}

/* 368f:009f */
s16 dec_floor(u16 p_ds)
{
    DSW(p_ds) = (u16)(DSW(p_ds) - 1);
    if (DSS(p_ds) > 0) return 1;
    DSW(p_ds) = 0;
    return 0;
}

/* 368f:00b7 */
s16 countdown_get(void) { return DSB(AP_DS_timer_installed) ? DSS(AP_DS_timer_countdown) : 0; }

/* 368f:00ca — PORT: pumps the host while spinning (the timer interrupt is host_pump's tick) */
s16 countdown_wait(void)
{
    while (countdown_get() && !key_or_click()) host_pump();
    return countdown_get();
}

/* 368f:00e6 */
void delay18(s16 n)
{
    DSW(AP_DS_timer_countdown) = (u16)(13 * n);
    wait_key_release();
    countdown_wait();
}

/* 368f:00ff — the 10-pass line blind */
void wipe(s16 x, s16 y, s16 w, s16 h, s16 ticks, s16 copy)
{
    s16 last = (s16)(y + h - 1);
    DSW(AP_DS_timer_countdown) = 0;
    for (s16 k = 0; k < 10; k++) {
        /* PORT: present the displayed page once per pass, and pump while waiting for the slot */
        vid_present();
        while (countdown_get()) host_pump();
        DSW(AP_DS_timer_countdown) = (u16)ticks;
        for (s16 r = (s16)(y + k); r <= last; r = (s16)(r + 10)) {
            if (!w) continue;
            if (copy) gfx_copy_rect(x, r, w, 1);
            else gfx_rect(x, r, w, 1);
        }
    }
    vid_present();
}

/* 368f:017f */
void wipe_in(s16 ticks)
{
    read_back();
    write_front();
    wipe(0, 0, 320, 200, ticks, 1);
}

/* 368f:01a3 */
void wipe_black(void)
{
    write_front();
    DRVB(D_FILL_ON) = 1;
    DRVB(D_FILL_COL) = 0;
    DRVB(D_DRAW_COL) = 0;
    wipe(0, 0, DRVS(D_SCR_W), DRVS(D_SCR_H), 4, 0);
}

/* ---------------------------------------------------------------- §4 font set */

/* 4c9b:0000 fonts_load */
s16 ap_fonts_load(void)
{
    static const u16 names[5] = { AP_DS_str_font1, AP_DS_str_font2, AP_DS_str_font3, AP_DS_str_font4, AP_DS_str_talk };
    static const u16 slots[5] = { AP_DS_font1, AP_DS_font2, AP_DS_font3, AP_DS_font4, AP_DS_font_talk };
    for (int i = 0; i < 5; i++) DSW(slots[i]) = font_load(res_name(names[i], AP_DS_str_FNT));
    for (int i = 0; i < 5; i++) if (!DSW(slots[i])) return 0;
    return 1;
}
