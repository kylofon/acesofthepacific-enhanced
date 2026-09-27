/* Game port 201h model (platform.md §3.4.6): the library's timing loop (joy_read_axes 05da:3667) is
 * replaced by counts on a synthetic scale, raw = 200 + axis * 200 / 32767 (0..399, centre 200), which the
 * unchanged calibration and scaling code (joy_calibrate, joy_scale, AP's CONTROL.PRF mapping) then
 * processes. A missing stick times out: 400 samples. PORT: joy_delay / PIT loop-speed calibration and
 * the one-shot wait loop are not modelled. */
#include "platform/plat_priv.h"

#include "host.h"

static u16 count(int axis)
{
    if (!host_joy_present()) return 400;
    s16 a[4];
    host_joy_axes(a);
    int v = 200 + (int)a[axis] * 200 / 32767;
    return (u16)(v < 0 ? 0 : v > 399 ? 399 : v);
}

u8 gameport_in(void)
{
    u8 b = host_joy_buttons();                         /* bit0 A1, bit1 A2, bit2 B1, bit3 B2 */
    /* axis bits read 1 right after firing (one-shots running); buttons are active low */
    return (u8)(0x0F | (u8)(~b << 4));
}

void gameport_read_axes(u8 mask, u16 *si, u16 *di)
{
    int ax_si = (mask & 3) ? 0 : 2, ax_di = (mask & 3) ? 1 : 3;
    u16 s = 0, d = 0;
    if (mask & (1 << ax_si)) s = count(ax_si);
    if (mask & (1 << ax_di)) d = count(ax_di);
    if (si) *si = s;
    if (di) *di = d;
}
