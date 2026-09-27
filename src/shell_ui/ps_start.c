/* shell_ui: PS start-up helpers — the "Please Wait..." box (048e:0112), the sound toggles (315c),
 * the screenshot hotkey (0000:032c) — shell_ui.md §3.3, §4; shell_ui_toolkit.md §4.2.
 * (PS main, sys_init, fly and exit are platform's: platform/ps_main.c.) */
#include "shell_ui/sui_a_priv.h"

/* PS 0000:032c snapshot: the front page as SNAPSHOT.LBM (Alt/Ctrl-T) */
void snapshot(void)
{
    u16 save = DSW(PG_READ);
    DSW(PG_READ) = DSW(PG_FRONT);
    snap_save(0x2188 /* "SNAPSHOT.LBM" */);
    DSW(PG_READ) = save;
}

/* PS 048e:0112 please_wait */
void please_wait(void)
{
    DSB(CLIP_ON) = 0;
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FLAGS) = 1;
    RYs = 80;
    RHs = 20;
    RWs = (s16)(gfx_text_width(0x0161 /* "Please Wait..." */) + 10);
    RXs = (s16)(160 - (RWs >> 1));
    pages_back_to_front();
    draw_popup_box();
    DSB(TXT_FG) = 0xC5;
    text_engrave(0x0161, 0xC7, 0xB3, 0xC0, ps_text_center_x(0x0161, (u16)RXs, (u16)RWs), (u16)(RYs + 7));
    copy_front_to_back();
}

/* PS 315c:0003 snd_music_on */
static s16 snd_music_on(void) { return DSC(PS_DS_cfg_music_on); }

/* PS 315c:000e snd_music_toggle -> on */
s16 snd_music_toggle(void)
{
    DSB(PS_DS_cfg_music_on) ^= 1;
    if (snd_music_on()) DSB(PS_DS_cfg_music_on) = (u8)snd_enable(-1);
    else DSB(PS_DS_cfg_music_on) = snd_disable(-1) == 0;
    return snd_music_on();
}

/* PS 315c:0044 snd_fx_on */
static s16 snd_fx_on(void) { return DSC(PS_DS_cfg_sfx_on); }

/* PS 315c:004f snd_fx_toggle -> on */
s16 snd_fx_toggle(void)
{
    DSB(PS_DS_cfg_sfx_on) ^= 1;
    if (snd_fx_on()) DSB(PS_DS_cfg_sfx_on) = (u8)snd_enable(-2);
    else DSB(PS_DS_cfg_sfx_on) = snd_disable(-2) == 0;
    return snd_fx_on();
}

__attribute__((constructor)) static void ps_start_codeptrs(void)
{
    REG(please_wait);
}
