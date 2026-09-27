/* INT 33h mouse driver model (platform.md §3.5, §3.5.3). The driver keeps a position in its own units,
 * clamped to the ranges of fn 07h/08h; the Dynamix library sets them to 4 x pixels, so the host
 * position maps to 4 x pixel. PORT: absolute host pointer instead of mickey integration. */
#include "platform/plat_priv.h"

#include "host.h"

static bool present;
static u16 x, y, buttons;
static u16 xmin, xmax = 639, ymin, ymax = 199;
static u16 hmask;
static FarPtr handler;
static int shown = -1;

static u16 clampu(int v, u16 lo, u16 hi) { return (u16)(v < lo ? lo : v > hi ? hi : v); }

u16 int33_reset(u16 *nbuttons)
{
    present = true;
    xmin = 0; xmax = 639; ymin = 0; ymax = 199;
    x = 320; y = 100;
    hmask = 0;
    handler = far_make(0, 0);
    shown = -1;
    if (nbuttons) *nbuttons = 2;
    return 0xFFFF;
}

void int33_show(void) { if (shown < 0) shown++; }
void int33_hide(void) { shown--; }

void int33_get(u16 *b, u16 *px, u16 *py)
{
    if (b) *b = buttons;
    if (px) *px = x;
    if (py) *py = y;
}

void int33_set_pos(u16 nx, u16 ny)
{
    x = clampu(nx, xmin, xmax);
    y = clampu(ny, ymin, ymax);
    host_mouse_warp(x / 4, y / 4);
}

void int33_set_xrange(u16 a, u16 b)
{
    xmin = a < b ? a : b;
    xmax = a < b ? b : a;
    x = clampu(x, xmin, xmax);
}

void int33_set_yrange(u16 a, u16 b)
{
    ymin = a < b ? a : b;
    ymax = a < b ? b : a;
    y = clampu(y, ymin, ymax);
}

void int33_set_handler(u16 mask, FarPtr fn)
{
    hmask = mask;
    handler = fn;
}

void int33_set_ratio(u16 h, u16 v) { (void)h; (void)v; }

void int33_host_event(int px, int py, u8 b)
{
    if (!present) return;
    u16 nx = clampu(px * 4, xmin, xmax), ny = clampu(py * 4, ymin, ymax);
    u16 ev = 0;
    if (nx != x || ny != y) ev |= 0x01;
    if ((b & 1) && !(buttons & 1)) ev |= 0x02;
    if (!(b & 1) && (buttons & 1)) ev |= 0x04;
    if ((b & 2) && !(buttons & 2)) ev |= 0x08;
    if (!(b & 2) && (buttons & 2)) ev |= 0x10;
    x = nx; y = ny; buttons = b;
    if (!(ev & hmask) || far_is_null(handler) || !prog_layout) return;
    Int33Handler h = (Int33Handler)codeptr_lookup(handler);
    h(ev, buttons, x, y);
}

void int33_unhook(void)
{
    hmask = 0;
    handler = far_make(0, 0);
}
