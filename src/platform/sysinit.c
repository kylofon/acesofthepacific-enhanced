/* System shutdown and small system helpers (platform.md §3.7, §3.9). */
#include "platform/rtl_priv.h"

#include "platform/vga.h"
#include "prog.h"
#include "symbols_ap.h"
#include "video/video.h"

/* INTRO 05da:5db4 / PS 16e3:5b0e / AP 1d32:012a sys_shutdown */
void sys_shutdown(void)
{
    kbd_restore();
    mouse_shutdown();
    timer_restore();
    divzero_restore();
}

/* INTRO 05da:5f48 / PS 16e3:5ca2 / AP 1d32:02be (near) bios_set_text_mode(bits): BIOS equipment
 * video bits (40:10 bits 4-5) = bits, then INT 10h AX=0003h */
void bios_set_text_mode(u16 a0)
{
    wr8(0x40, 0x10, (u8)((rd8(0x40, 0x10) & 0xCF) | (u8)(a0 << 4)));
    vga_set_mode(3);
}

/* AP 1d32:0001: the byte DS:5E3C (flag of AP module 1c30; no direct caller) */
u8 ap_flag_5e3c_get(void) { return DSB(0x5E3C); }

/* INTRO 05da:5c92 / PS 16e3:59ec: empty */
void empty_5c92(void) { }

/* AP 1d32:6a91 sys_exit(code) (dead): sys_shutdown, text mode; code 1 = INT 19h reboot, else INT 21h
 * 4C00h. PORT: the reboot quits to DOS too (exit code 0). 1d32:6aaf (sys_exit_tail) is its RETF. */
void sys_exit(s16 code)
{
    (void)code;
    sys_shutdown();
    gfx_restore_text();
    prog_exit(0);
}
