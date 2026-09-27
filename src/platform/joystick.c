/* Joystick (game port 201h, platform.md §3.4): INTRO 05da:3667.., PS 16e3:000e.., AP 1d32:030d..
 * The port timing loop is the gameport.c model (synthetic counts 0..399, 400 = no stick). */
#include "platform/rtl_priv.h"

/* INTRO 05da:3667 / PS 16e3:000e / AP 1d32:030d joy_read_axes (near: BL mask -> SI, DI) */
static void joy_read_axes(u8 bl, u16 *si, u16 *di)
{
    gameport_read_axes(bl, si, di);
}

/* INTRO 05da:36ce / PS 16e3:0075 / AP 1d32:0374 joy_scale (near: AX raw, BX centre, CX scale) */
static s16 joy_scale(u16 raw, u16 centre, u16 scale)
{
    s16 d = (s16)(raw - centre);
    bool neg = d < 0;
    u16 a = (u16)(neg ? -d : d);
    if (scale) a = (u16)(((u32)a * scale) >> 8);
    if (a > 0x7F) a = 0x7F;
    if ((u8)a < 8) a = 0;
    a &= 0xFF;
    return neg ? (s16)-a : (s16)a;
}

static u16 *centre(int i) { return (u16 *)DSP((u16)(lds(G_joy_centre) + 2 * i)); }
static u16 *scalev(int i) { return (u16 *)DSP((u16)(lds(G_joy_scale) + 2 * i)); }

/* INTRO 05da:3705 / PS 16e3:00ac / AP 1d32:03ac joy_calibrate: presence, centre and scale from one
 * reading (the stick must be centred). PORT: the PIT loop-speed calibration (joy_delay) is dropped;
 * joy_delay is left as it is. */
s16 joy_calibrate(void)
{
    u16 si, di;
    u8 *ok = LP(G_joy_axis_ok);
    u8 *pres = LP(G_joy_present);
    joy_read_axes(3, &si, &di);
    *centre(0) = si;
    *ok = (u8)((*ok >> 1) | (si < 400 ? 0x80 : 0));   /* RCR: CF = below 400 */
    *centre(1) = di;
    *ok = (u8)((*ok >> 1) | (di < 400 ? 0x80 : 0));
    pres[0] = (*ok & 0xC0) == 0xC0;
    if (si) *scalev(0) = (u16)(0x7F00 / si);
    if (di) *scalev(1) = (u16)(0x7F00 / di);
    joy_read_axes(0x0C, &si, &di);
    *centre(2) = si;
    *ok = (u8)((*ok >> 1) | (si < 400 ? 0x80 : 0));
    *centre(3) = di;
    *ok = (u8)((*ok >> 1) | (di < 400 ? 0x80 : 0));
    pres[1] = (*ok & 0xC0) == 0xC0;
    if (si) *scalev(2) = (u16)(0x7F00 / si);
    if (di) *scalev(3) = (u16)(0x7F00 / di);
    u8 v = gameport_in();                            /* fire, short delay, read */
    if (!(v & 3)) pres[0] = 0;
    if (!(v & 0xC)) pres[1] = 0;
    *ok >>= 4;
    return (s16)(pres[1] << 1 | pres[0]);
}

/* INTRO 05da:383c / PS 16e3:01e3 / AP 1d32:04e5 joy_read(stick, int *x, int *y): -127..127 */
void joy_read(s16 stick, u16 x_ds, u16 y_ds)
{
    int s = stick != 0;
    if (!LP(G_joy_present)[s]) return;
    u16 si, di;
    joy_read_axes(s ? 0x0C : 3, &si, &di);
    LS(G_joy_tmp_x) = joy_scale(si, *centre(2 * s), *scalev(2 * s));
    s16 y = joy_scale(di, *centre(2 * s + 1), *scalev(2 * s + 1));
    DSS(y_ds) = y;
    DSS(x_ds) = LS(G_joy_tmp_x);
}

/* PS 16e3:025b / AP 1d32:055e joy_read_raw(stick, int *x, int *y): the counts */
void joy_read_raw(s16 stick, u16 x_ds, u16 y_ds)
{
    int s = stick != 0;
    if (!LP(G_joy_present)[s]) return;
    u16 si, di;
    joy_read_axes(s ? 0x0C : 3, &si, &di);
    DSW(y_ds) = di;
    DSW(x_ds) = si;
}

/* INTRO 05da:38b4 / PS 16e3:0296 / AP 1d32:059a joy_dir_bits(stick): up 1, down 2, left 4, right 8 */
s16 joy_dir_bits(s16 stick)
{
    if (!LP(G_joy_present)[stick != 0]) return 0;
    u16 xy = lds(G_joy_dir_xy);
    joy_read(stick, xy, (u16)(xy + 2));
    s16 r = 0;
    s16 x = DSS(xy), y = DSS(xy + 2);
    if (x < -30) r |= 4;
    else if (x >= 30) r |= 8;
    if (y < -30) r |= 1;
    else if (y >= 30) r |= 2;
    return r;
}

/* INTRO 05da:390d / PS 16e3:02ef / AP 1d32:05f5 joy_button(n): 1 = pressed */
s16 joy_button(s16 n)
{
    return (s16)(((gameport_in() >> ((n & 3) + 4)) & 1) ^ 1);
}

/* INTRO 05da:3927 / PS 16e3:0309 / AP 1d32:060f joy_axis(i): one scaled axis (no presence test) */
s16 joy_axis(s16 i)
{
    u16 si, di;
    joy_read_axes((u8)(1 << i), &si, &di);
    return joy_scale((i & 1) ? di : si, *centre(i), *scalev(i));
}

/* PS 16e3:0339 / AP 1d32:063f joy_axis_raw(i): one raw count */
s16 joy_axis_raw(s16 axis)
{
    u16 si, di;
    joy_read_axes((u8)(1 << axis), &si, &di);
    return (s16)((axis & 1) ? di : si);
}
