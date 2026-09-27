/* Mouse (INT 33h, platform.md §3.5): INTRO 05da:3a40.., PS 16e3:3d64.., AP 1d32:5b9e.., and the DOS-3
 * glue INTRO 0491 / PS 2316. The driver is the int33.c model; coordinates are kept x4 and come only
 * from the event handler. */
#include "platform/rtl_priv.h"

/* INTRO 05da:3ab0 / PS 16e3:3dd4 / AP 1d32:5c0e mouse_set_range(x, y, w, h): INT 33h 07h/08h with
 * min = 4x, max = 4(x+w-1). (The disassembly of all three has the DEC: platform.md's "AP: no DEC" is
 * not what AP 1d32:5c20 does.) */
void mouse_set_range(s16 x, s16 y, s16 w, s16 h)
{
    int33_set_xrange((u16)(x * 4), (u16)((x + w - 1) * 4));
    int33_set_yrange((u16)(y * 4), (u16)((y + h - 1) * 4));
}

/* INTRO 05da:3a40 / PS 16e3:3d64 / AP 1d32:5b9e mouse_init */
s16 mouse_init(void)
{
    if (LB(G_mouse_present)) return 0;
    u16 bx;
    u16 ax = int33_reset(&bx);
    LB(G_mouse_present) = (u8)-ax;                   /* NEG AX: FFFFh -> 1 */
    if (ax == 0) return 0;
    int33_set_pos(0x7FFF, 0x7FFF);
    int33_show();
    int33_hide();
    int33_set_ratio(8, 8);
    int33_set_pos(0, 0);
    mouse_set_range(0, 0, (s16)LW(G_drv_scr_w), (s16)LW(G_drv_scr_h));
    int33_set_handler(0x1F, codeptr_far_shared(F_mouse_event_handler));
    if (LB(G_drv_adapter) == 8) {                    /* the VGA register save bytes (hand-off video) */
        u16 b = (u16)(lds(G_mouse_stack_top) + 0x0E);    /* INTRO DS:1826 / PS 308A / AP 3430 */
        DSB(b) = DSB((u16)(b + 1));
        DSB((u16)(b + 2)) = DSB((u16)(b + 3));
    }
    return 1;
}

/* INTRO 05da:3ae1 / PS 16e3:3e05 / AP 1d32:5c3f mouse_set_callback (never called) */
void mouse_set_callback(FarPtr fn) { ds_far_wr(lds(G_mouse_callback), fn); }

/* INTRO 05da:3b32 / 3b97 (PS 16e3:3e56 / 3ebb, AP 1d32:5c90 / 5cf5) mouse_vga_save / restore: the VGA
 * GC/SEQ state around the user callback. PORT: there is never a callback; no-ops. */
void mouse_vga_save(void) { }
void mouse_vga_restore(void) { }

/* INTRO 05da:3af2 / PS 16e3:3e16 / AP 1d32:5c50 mouse_event_handler (INT 33h fn 0Ch handler) */
void mouse_event_handler(u16 ax, u16 bx, u16 cx, u16 dx)
{
    (void)ax;
    LB(G_mouse_buttons) = (u8)bx;
    LW(G_mouse_x4) = cx;
    LW(G_mouse_y4) = dx;
    FarPtr cb = ds_far(lds(G_mouse_callback));
    if (!far_is_null(cb)) {
        mouse_vga_save();
        codeptr_lookup(cb)();
        mouse_vga_restore();
    }
}

/* INTRO 05da:3bf0 / PS 16e3:3f14 / AP 1d32:5d4e mouse_shutdown */
s16 mouse_shutdown(void)
{
    if (!LB(G_mouse_present)) return 0;
    LB(G_mouse_present) = 0;
    u16 bx;
    int33_reset(&bx);
    int33_set_handler(0, far_make(0, 0));
    return 1;
}

/* INTRO 05da:3c0c / PS 16e3:3f30 / AP 1d32:5d6a mouse_get_pos(int *x, int *y) */
void mouse_get_pos(u16 x_ds, u16 y_ds)
{
    if (!LB(G_mouse_present)) return;
    DSW(x_ds) = (u16)(LW(G_mouse_x4) >> 2);
    DSW(y_ds) = (u16)(LW(G_mouse_y4) >> 2);
}

/* INTRO 05da:3c36 / PS 16e3:3f5a / AP 1d32:5d94 mouse_set_pos */
s16 mouse_set_pos(s16 x, s16 y)
{
    if (!LB(G_mouse_present)) return 0;
    LW(G_mouse_x4) = (u16)(x * 4);
    LW(G_mouse_y4) = (u16)(y * 4);
    int33_set_pos(LW(G_mouse_x4), LW(G_mouse_y4));
    return 1;
}

/* INTRO 05da:3c61 / PS 16e3:3f85 / AP 1d32:5dbf mouse_button(right) */
s16 mouse_button(s16 right)
{
    if (!LB(G_mouse_present)) return 0;
    return (s16)((LB(G_mouse_buttons) >> (right ? 1 : 0)) & 1);
}

/* ---- DOS-3 glue (INTRO 0491, PS 2316): only when the DOS major version (_version DS:0092) >= 3 */

static bool dos3(void) { return DSB(RTL_DS_version) >= 3; }

/* INTRO 0491:000c / PS 2316:000f */
s16 mouse_init_dos3(void) { return dos3() ? mouse_init() : 0; }
/* INTRO 0491:0025 / PS 2316:0028 */
s16 mouse_shutdown_dos3(void) { return dos3() ? mouse_shutdown() : 0; }
/* INTRO 0491:003e / PS 2316:0041 */
void mouse_get_pos_dos3(u16 p0_ds, u16 p1_ds) { if (dos3()) mouse_get_pos(p0_ds, p1_ds); }
/* INTRO 0491:005f / PS 2316:0062 */
s16 mouse_button_dos3(s16 a0) { return dos3() ? mouse_button(a0) : 0; }

/* INTRO 0491:007c / PS 2316:007f mouse_move_to(x, y): mouse_set_pos only if the position differs */
s16 mouse_move_to(s16 x, s16 y)
{
    if (!dos3()) return 0;
    u16 m = stk_mark();
    u16 px = stk_alloc(2), py = stk_alloc(2);
    DSW(px) = DSW(py) = 0;
    mouse_get_pos(px, py);
    s16 r = (DSS(px) != x || DSS(py) != y) ? mouse_set_pos(x, y) : 1;
    stk_release(m);
    return r;
}
