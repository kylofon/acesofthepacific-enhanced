/* PS-only parts of the ts2 library owned by video (video_ts.md §4-§6): bitmap-shape size cap, the dead
 * flat item drawer, the shadow colour, the curve helpers (also AP 70b4) and the 4960 fatal-error /
 * stack-check module. */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "platform/rtl.h"
#include "render3d/render3d.h"
#include "prog.h"
#include "symbols_ps.h"

/* PS 3d50:3e4d eye_in_obj — PORT: data (i16[3] in the 3d50 code segment, video_ts.md §2.1), not a
 * function; render3d reads it in place. Nothing to implement. */

/* ---------------------------------------------------------------- ts_bm (4818) */

void bm_set_cap(u16 n) { DSW(PS_DS_bm_cap) = n; }          /* 4818:0006 */
void bm_reset_cap(void) { DSW(PS_DS_bm_cap) = 0x100; }     /* 4818:0011 */
void bm_stub(void) { DSW(PS_DS_bm_mult) = 1; }             /* 4818:093a (fragment, no caller) */

/* 4818:001c (dead: DS:3B0C is always 1) */
void bm_draw_item_flat(FarPtr it)
{
    s16 idx = (s8)rd8(it.seg, it.off);
    u16 bm = bitmap_global((s16)(DSW(PS_DS_bm_base) + idx));
    if (!bm) assert_fail0(ds_ptr(0x3B10) /* "ts_bm.c" */, 0x5F);
    if (!bm) return;
    u16 sx = rd16(it.seg, (u16)(it.off + 3)); if (!sx) sx = 0x400;
    u16 sy = rd16(it.seg, (u16)(it.off + 5)); if (!sy) sy = 0x400;
    s16 w = (s16)mul_shr10(DSW(bm + 6), sx);
    s16 h = (s16)mul_shr10(DSW(bm + 8), sy);
    if (!w || !h) return;
    s16 x = (s16)(SEGS(0x3d50, 0x3E91) + (s8)rd8(it.seg, (u16)(it.off + 1)));   /* cx (3d50 CS data) */
    s16 y = (s16)(SEGS(0x3d50, 0x3E93) + (s8)rd8(it.seg, (u16)(it.off + 2)));   /* cy */
    DRVB(D_CLIP_ON) = 1;
    FarPtr hot = ds_far(PS_DS_bm_hot);
    s16 m = DSS(PS_DS_bm_mult);
    s16 hy = (s16)idiv16_retry((s8)rd8(hot.seg, (u16)(hot.off + 2 * idx + 1)), m, NULL);
    s16 hx = (s16)idiv16_retry((s8)rd8(hot.seg, (u16)(hot.off + 2 * idx)), m, NULL);
    gfx_sprite_rotated_ind(bm, x, y, rd16(it.seg, (u16)(it.off + 7)), (s16)(w * m), (s16)(h * m), hx, hy);
}

/* 48ac:013a */
void shadow_set_colour(u8 c) { DSB(PS_DS_shadow_colour) = c; }

/* ---------------------------------------------------------------- curves (4915 / AP 70b4), §6.1 */

/* PS 4915:02fb / AP 70b4:0188 */
void bezier2_draw(s16 k, s32 x0, s32 x1, s32 x2, s32 y0, s32 y1, s32 y2)
{
    s32 ax = x0 + x2 - 2 * x1, bx = (s32)((u32)(x1 - x0) << (k + 1)), px = (s32)((u32)x0 << (2 * k));
    s32 ay = y0 + y2 - 2 * y1, by = (s32)((u32)(y1 - y0) << (k + 1)), py = (s32)((u32)y0 << (2 * k));
    s16 n = (s16)(1 << k);
    s16 prx = (s16)(px >> (2 * k)), pry = (s16)(py >> (2 * k));
    for (s16 i = 0; i <= n; i++) {
        s16 cx = (s16)(px >> (2 * k)), cy = (s16)(py >> (2 * k));
        if (cx != prx || cy != pry) { gfx_line(prx, pry, cx, cy); prx = cx; pry = cy; }
        px += bx + (s32)(2 * i + 1) * ax;
        py += by + (s32)(2 * i + 1) * ay;
    }
}

/* PS 4915:0173 / AP 70b4:0000 (no callers) */
void curve4_draw(u16 xs_ds, u16 ys_ds)
{
    s16 x[4], y[4];
    for (int i = 0; i < 4; i++) { x[i] = DSS(xs_ds + 2 * i); y[i] = DSS(ys_ds + 2 * i); }
#define MID(a, b) ((s16)((a) + (s16)((s16)((b) - (a)) >> 1)))
    s16 m01x = MID(x[0], x[1]), m01y = MID(y[0], y[1]);
    s16 m03x = MID(x[0], x[3]), m03y = MID(y[0], y[3]);
    s16 m12x = MID(x[1], x[2]), m12y = MID(y[1], y[2]);
    s16 m32x = MID(x[3], x[2]), m32y = MID(y[3], y[2]);
#undef MID
    bezier2_draw(3, m03x, x[0], m01x, m03y, y[0], m01y);
    bezier2_draw(3, m01x, x[1], m12x, m01y, y[1], m12y);
    bezier2_draw(3, m12x, x[2], m32x, m12y, y[2], m32y);
    bezier2_draw(3, m32x, x[3], m03x, m32y, y[3], m03y);
}

/* ---------------------------------------------------------------- 4960 fatal error / stack check, §6.2 */

/* 4960:0001 */
u16 str_rotbuf(FarPtr s)
{
    DSW(PS_DS_ts_rotbuf_sel) ^= 1;
    u16 d = (u16)(PS_DS_ts_rotbuf + DSS(PS_DS_ts_rotbuf_sel) * 0x50), p = d;
    u8 c;
    do { c = rd8(s.seg, s.off); s.off++; DSB(p) = c; p++; } while (c);
    return d;
}

/* 4960:003c */
u16 src_name(FarPtr path)
{
    u16 d = str_rotbuf(path);
    rtl_strrev(d);
    u16 p = rtl_strchr(d, '\\');
    if (p) DSB(p) = 0;
    rtl_strrev(d);
    p = rtl_strchr(d, '.');
    if (p) DSB(p) = 0;
    rtl_strupr(d);
    return d;
}

/* 4960:0213 */
void stack_report(void)
{
    rtl_printf(0x3C9A);                                             /* "Stack usage:\n" */
    rtl_printf(0x3CA8, src_name(far_make(DSW(PS_DS_ts_stack_file + 2), DSW(PS_DS_ts_stack_file))));
    rtl_printf(0x3CB4, DSW(PS_DS_ts_stack_line));
    u32 depth = 0xFFFFu - DSL(PS_DS_ts_stack_low);
    rtl_printf(0x3CC0, (unsigned)(u16)depth, (unsigned)(u16)(depth >> 16));
}

/* 4960:0094 */
u16 ts_fatal(FarPtr file, s16 line, u16 msg_ds)
{
    gfx_restore_text();
    rtl_printf(0x3BFE);
    rtl_printf(0x3C32);
    rtl_printf(0x3C67, src_name(file), (unsigned)(u16)line, msg_ds ? msg_ds : 0x3C73);
    stack_report();
    sys_shutdown();
    prog_exit(0);
}

/* 4960:00f6 (no caller). PORT: the local's address is the emulated stack pointer. */
u16 stack_free(void)
{
    u32 t = (u32)(u16)(dg_sp - 4) + DSW(PS_DS__stklen);
    return t > 0xFFFFu ? (u16)(t + 1) : 0;
}

/* 4960:0133 */
void stack_overflow(FarPtr file, s16 line)
{
    gfx_restore_text();
    rtl_printf(0x3C75);                                             /* "Bad Human!" */
    rtl_printf(0x3C81, src_name(file));
    rtl_printf(0x3C8C, (unsigned)(u16)line);
    stack_report();
    sys_shutdown();
    prog_exit(0);
}

/* 4960:0184. PORT: the address of the first argument is modelled by the emulated stack pointer. */
void stack_check(FarPtr file, s16 line, ...)
{
    u32 limit = (u32)(0xFFFFu - DSW(PS_DS__stklen)) + 0x12C;
    u32 sp = (u16)dg_sp;
    if (sp < DSL(PS_DS_ts_stack_low)) {
        DSL(PS_DS_ts_stack_low) = sp;
        DSW(PS_DS_ts_stack_file) = file.off;
        DSW(PS_DS_ts_stack_file + 2) = file.seg;
        DSW(PS_DS_ts_stack_line) = (u16)line;
    }
    if (sp < limit) stack_overflow(file, line);
}
