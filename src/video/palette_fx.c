/* Palette effects (video_palette.md §6-8): the TTM timed interpolator and the 59.18 Hz tick callbacks,
 * the INTRO/PS TTM 3103 hardware scroll (VGA registers through platform/vga.h), and AP's palette effect
 * system pal_fx, the pending DAC upload with the frame flip, the VCR message dimmer and the cockpit
 * palette switches. */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "platform/rtl.h"
#include "render3d/render3d.h"
#include "sim_flow/sim_flow.h"
#include "symbols_ap.h"
#include "symbols_intro.h"
#include "symbols_ps.h"

/* ============================================================== 59.18 Hz counter and stepper */

/* INTRO 0460:000b — timer callback (period 4): the TTM counter DS:24D8 += 1 */
static void intro_tick_cb(void) { DSL(INTRO_DS_intro_ticks) += 1; }
/* PS 21f4:0009 — the same for PS (DS:52AA) */
static void shell_tick_cb(void) { DSL(PS_DS_shell_ticks) += 1; }

static u16 ds_ttm_ticks(void) { return pid() == PROG_INTRO ? INTRO_DS_intro_ticks : PS_DS_shell_ticks; }

/* INTRO 10fc:000f / PS 22a0:0089 — video_palette.md §6: 1 if the timer is not installed or the duration
 * has elapsed (PS's TTM calls the shell's 22a0:0009 instead; this one has no PS caller) */
s16 ttm_timer_done(u16 t_ds)
{
    if (LB(G_timer_installed) == 0) return 1;
    u32 e = DSL(ds_ttm_ticks()) - DSL(t_ds);
    return (u32)DSW(t_ds + 4) > e ? 0 : 1;
}

/* INTRO 10fc:0058 / PS 22a0:00d2 — interpolated value between a and b */
s16 ttm_timer_value(u16 t_ds, s16 a, s16 b)
{
    if (LB(G_timer_installed) == 0) return a;
    u32 e = DSL(ds_ttm_ticks()) - DSL(t_ds);
    u16 dur = DSW(t_ds + 4);
    if (!((u32)dur > e) || dur == 0) return b;
    if (a > b) { s16 t = a; a = b; b = t; e = dur - e; }
    u32 prod = (u32)((s32)(s16)(b - a) * (s32)e);                /* LXMUL@ */
    s16 r = (s16)(a + (s16)(prod / dur));                         /* LUDIV@ */
    if (r < a) r = a;
    else if (r > b) r = b;
    return r;
}

/* INTRO 10fc:0114 / PS 22a0:018e */
void ttm_timer_start(u16 t_ds, s32 dur)
{
    DSL(t_ds) = DSL(ds_ttm_ticks());
    DSW(t_ds + 4) = (u16)dur;
}

/* INTRO 10fc:0130 / PS 22a0:01aa (no caller) */
void ttm_timer_expire(u16 t_ds)
{
    DSW(t_ds + 4) = 0;
    DSL(t_ds) = DSL(ds_ttm_ticks()) - 1;
}

/* ============================================================== TTM 3103 hardware scroll */

/* Scroll state: INTRO DS:3320 canvas far pointer, 3324 screens, 3326 base address; PS 6086, 608C, 608A.
 * PORT: the PS copy 30b5 was not compared instruction by instruction (video_palette.md §13); the INTRO
 * code serves both. */
#define SC_CANVAS  (pid() == PROG_INTRO ? 0x3320 : 0x6086)       /* far pointer */
#define SC_SCREENS (pid() == PROG_INTRO ? 0x3324 : 0x608C)
#define SC_BASE    (pid() == PROG_INTRO ? 0x3326 : 0x608A)

/* latch copy of one planar byte (write mode 1, map mask 0Fh) between far video addresses */
static void latch(FarPtr dst, FarPtr src)
{
    u8 l[4];
    for (int p = 0; p < 4; p++) l[p] = vget(src.seg, src.off, p);
    for (int p = 0; p < 4; p++) vput(dst.seg, dst.off, p, l[p]);
}

static s16 page_bytes(void) { return DRVB(D_ADAPTER) == 8 ? 0x50 : 0x28; }

/* INTRO 103d:0933 / PS 30b5:085f — show the canvas window at (x, y): CRTC start and pel panning at the
 * retrace; set_off: CRTC offset for the wide canvas */
static void scroll_show(s16 x, s16 y, s16 pitch, s16 set_off)
{
    s16 per = (s16)(0x140 / pitch);
    u16 start = (u16)(x / per + y * pitch + DSW(SC_BASE));
    u8 pel = (u8)((x & (per - 1)) * (pitch / 0x28));
    vga_wait_vretrace_end();
    vga_out(0x3D4, 0x0C); vga_out(0x3D5, (u8)(start >> 8));
    vga_out(0x3D4, 0x0D); vga_out(0x3D5, (u8)start);
    vga_wait_vretrace_active();
    vga_in(0x3DA);
    vga_out(0x3C0, 0x13); vga_out(0x3C0, pel); vga_out(0x3C0, 0x20);
    if (set_off) vga_outw(0x3D4, (u16)(((pitch * DSS(SC_SCREENS) / 2) << 8) + 0x13));
    vid_present();
}

/* INTRO 103d:09f8 / PS 30b5:0910 — copy the final window to the front page, restore the CRTC, then the
 * page copies (third page restored, back = the final image) */
static void scroll_finish(s16 dir, s16 x, s16 y, s16 pitch)
{
    FarPtr src = ds_far(SC_CANVAS);
    src.off = (u16)(src.off + x / (0x140 / pitch) + y * pitch);
    FarPtr dst = far_make(DRVW(D_PAGE_FRONT), 0);
    vga_outw(0x3C4, 0x0F02);
    vga_outw(0x3CE, DRVB(D_ADAPTER) == 8 ? 0x4105 : 0x0105);
    for (s16 r = 200; r; r--) {
        for (s16 i = pitch; i; i--) { latch(dst, src); src.off++; dst.off++; }
        if (dir >= 2) src.off = (u16)(src.off + (DSS(SC_SCREENS) - 1) * pitch);
    }
    vga_outw(0x3CE, DRVB(D_ADAPTER) == 8 ? 0x4005 : 0x0005);
    vga_wait_vretrace_end();
    u16 start = (u16)((u16)(DRVW(D_PAGE_FRONT) - 0xA000) << 4);
    vga_out(0x3D4, 0x0C); vga_out(0x3D5, (u8)((s16)start >> 8));
    vga_out(0x3D4, 0x0D); vga_out(0x3D5, (u8)start);
    vga_wait_vretrace_active();
    vga_in(0x3DA);
    vga_out(0x3C0, 0x13); vga_out(0x3C0, 0); vga_out(0x3C0, 0x20);
    if (dir >= 2) vga_outw(0x3D4, (u16)(((pitch / 2) << 8) + 0x13));
    DRVW(D_PAGE_READ) = DSS(SC_SCREENS) > 2 ? DRVW(D_PAGE_FRONT) : DRVW(D_PAGE_BACK);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BG);
    gfx_copy_rect(0, 0, 0x140, 0xC8);
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_FRONT);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
    gfx_copy_rect(0, 0, 0x140, 0xC8);
    vid_present();
}

/* The common stepping loop of both scrolls (0341.. / 0831..). */
static void scroll_run(s16 dir, s16 steps, s16 dist, s16 x, s16 y, s16 dx, s16 dy, s16 pitch, bool unsigned_cmp)
{
    if (!(steps <= dist)) steps = dist;
    s16 d = idiv16_retry((s32)dist, steps, NULL);                /* dist == 0 -> steps == 0: INT 0 (quirk) */
    dx = (s16)(dx * d);
    dy = (s16)(dy * d);
    vga_outw(0x3CE, DRVB(D_ADAPTER) == 8 ? 0x4005 : 0x0005);
    s16 s = (s16)(dx + dy); if (s < 0) s = (s16)-s;
    s16 first = dir >= 2 ? 1 : 0;
    s16 rem = dist;
    while (rem != 0) {
        scroll_show(x, y, pitch, first);
        bool over = unsigned_cmp ? (u16)s > (u16)rem : s > rem;
        if (over) {
            if (dx) dx = dx < 0 ? (s16)-rem : rem;
            if (dy) dy = dy < 0 ? (s16)-rem : rem;
            s = rem;
        }
        x = (s16)(x + dx);
        y = (s16)(y + dy);
        rem = (s16)(rem - s);
        first = 0;
    }
    scroll_show(x, y, pitch, first);
    scroll_finish(dir, x, y, pitch);
}

/* INTRO 103d:0459 / PS 30b5:0403 — video_palette.md §7.3: three-screen scroll (dist > 320) */
static void ttm_scroll3(s16 dir, s16 steps, s16 dist)
{
    if (dir < 0 || dir > 3 || steps < 1 || dist < 0 || (DRVB(D_ADAPTER) != 2 && DRVB(D_ADAPTER) != 8)) {
        DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
        DRVW(D_PAGE_READ) = DRVW(D_PAGE_BG);
        gfx_copy_rect(0, 0, 0x140, 0xC8);
        DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
        DRVW(D_PAGE_READ) = DRVW(D_PAGE_FRONT);
        gfx_copy_rect(0, 0, 0x140, 0xC8);
        return;
    }
    vga_outw(0x3C4, 0x0F02);
    vga_outw(0x3CE, DRVB(D_ADAPTER) == 8 ? 0x4105 : 0x0105);
    s16 pitch = page_bytes();
    s16 size = (s16)(pitch * 200);
    DSW(SC_SCREENS) = 3;
    /* far pointers A000:(page << 4) + size - 1 (with the carry into the segment) */
    FarPtr pf, pb, pg;
    { u32 a = (u32)(u16)(DRVW(D_PAGE_FRONT) << 4) + (u16)size - 1; pf = far_make((u16)(0xA000 + (a >> 16 << 12)), (u16)a); }
    { u32 a = (u32)(u16)(DRVW(D_PAGE_BACK) << 4) + (u16)size - 1; pb = far_make((u16)(0xA000 + (a >> 16 << 12)), (u16)a); }
    { u32 a = (u32)(u16)(DRVW(D_PAGE_BG) << 4) + (u16)size - 1; pg = far_make((u16)(0xA000 + (a >> 16 << 12)), (u16)a); }
    FarPtr cv = far_make(0xA000, 0xFFFF);
    DSW(SC_BASE) = (u16)-(s16)(pitch * 600);
    if (dir == 0 || dir == 2) {
        for (s16 n = size; n; n--) { latch(cv, pg); pg.off--; cv.off--; }
        for (s16 n = size; n; n--) { latch(cv, pb); pb.off--; cv.off--; }
        for (s16 n = size; n; n--) { latch(cv, pf); pf.off--; cv.off--; }
    } else {
        for (s16 n = size; n; n--) { latch(cv, pf); pf.off--; cv.off--; }
        for (s16 n = size; n; n--) { latch(cv, pb); cv.off--; latch(pb, pg); pg.off--; pb.off--; }
        pb.off = (u16)(pb.off + size);
        for (s16 n = size; n; n--) { latch(cv, pb); pb.off--; cv.off--; }
    }
    cv.off++;
    ds_far_wr(SC_CANVAS, cv);
    if (dir == 2 || dir == 3) {                                    /* 600 rows -> 200 rows of 3 screens */
        FarPtr start = cv, b = cv;
        for (u16 si = 0; si < 0x257; si++) {
            u32 j = si;
            do { j = (j * 200u) % 0x257u; } while (j < si);
            if (j > si) {
                FarPtr a = far_make(start.seg, (u16)(start.off + (u16)(j * (u32)(u16)pitch)));
                FarPtr tmp = far_make(start.seg, (u16)(start.off - 1));
                for (s16 n = pitch; n; n--) {
                    latch(tmp, a);
                    latch(a, b);
                    a.off++;
                    latch(b, tmp);
                    b.off++;
                }
            } else b.off = (u16)(b.off + pitch);
        }
    }
    s16 x, y, dx, dy;
    switch (dir) {
    case 0: dist = dist > 0x190 ? 0x190 : dist; x = 0; y = 0; dx = 0; dy = 1; break;
    case 1: dist = dist > 0x190 ? 0x190 : dist; x = 0; y = 0x190; dx = 0; dy = -1; break;
    case 2: dist = dist > 0x280 ? 0x280 : dist; x = 0; y = 0; dx = 1; dy = 0; break;
    default: dist = dist > 0x280 ? 0x280 : dist; x = 0x280; y = 0; dx = -1; dy = 0; break;
    }
    scroll_run(dir, steps, dist, x, y, dx, dy, pitch, true);
}

/* INTRO 103d:0000 / PS 30b5:000f — video_palette.md §7.1: scroll from the front page to the back page
 * over an off-screen canvas at A800:0050 (two screens). Used by no shipped script. */
void ttm_scroll(s16 dir, s16 steps, s16 dist)
{
    if (dist > 0x140) { ttm_scroll3(dir, steps, dist); return; }
    if (dir < 0 || dir > 3 || steps < 1 || dist < 0 || (DRVB(D_ADAPTER) != 2 && DRVB(D_ADAPTER) != 8)) {
        DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
        DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
        gfx_copy_rect(0, 0, 0x140, 0xC8);
        DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
        DRVW(D_PAGE_READ) = DRVW(D_PAGE_FRONT);
        return;
    }
    vga_outw(0x3C4, 0x0F02);
    vga_outw(0x3CE, DRVB(D_ADAPTER) == 8 ? 0x4105 : 0x0105);
    s16 pitch = page_bytes();
    s16 size = (s16)(pitch * 200);
    FarPtr pf = far_make(DRVW(D_PAGE_FRONT), (u16)(size - 1));
    FarPtr pb = far_make(DRVW(D_PAGE_BACK), (u16)(size - 1));
    FarPtr pg = far_make(DRVW(D_PAGE_BG), (u16)(size - 1));
    FarPtr cv = far_make(0xA800, (u16)(2 * size + 0x50 - 1));
    s16 x = 0, y = 0, dx = 0, dy = 0;
    if (dir == 0) {
        for (s16 n = size; n; n--) { latch(cv, pb); cv.off--; latch(pb, pg); pg.off--; pb.off--; }
        for (s16 n = size; n; n--) { latch(cv, pf); pf.off--; cv.off--; }
        if (dist > 0xC8) dist = 0xC8;
        x = 0; y = 0; dx = 0; dy = 1;
    } else if (dir == 1) {
        for (s16 n = size; n; n--) { latch(cv, pf); pf.off--; cv.off--; }
        for (s16 n = size; n; n--) { latch(cv, pb); cv.off--; latch(pb, pg); pg.off--; pb.off--; }
        if (dist > 0xC8) dist = 0xC8;
        x = 0; y = 0xC8; dx = 0; dy = -1;
    } else if (dir == 2) {
        for (s16 r = 0xC8; r; r--) {
            for (s16 n = pitch; n; n--) { latch(cv, pb); cv.off--; latch(pb, pg); pg.off--; pb.off--; }
            for (s16 n = pitch; n; n--) { latch(cv, pf); pf.off--; cv.off--; }
        }
        if (dist > 0x140) dist = 0x140;
        x = 0; y = 0; dx = 1; dy = 0;
    } else {
        for (s16 r = 0xC8; r; r--) {
            for (s16 n = pitch; n; n--) { latch(cv, pf); pf.off--; cv.off--; }
            for (s16 n = pitch; n; n--) { latch(cv, pb); cv.off--; latch(pb, pg); pg.off--; pb.off--; }
        }
        if (dist > 0x140) dist = 0x140;
        x = 0x140; y = 0; dx = -1; dy = 0;
    }
    cv.off++;
    ds_far_wr(SC_CANVAS, cv);
    DSW(SC_BASE) = 0x8050;
    DSW(SC_SCREENS) = 2;
    scroll_run(dir, steps, dist, x, y, dx, dy, pitch, false);
}

/* ============================================================== AP palette effects (§8.4) */

#define FX (DSW(AP_DS_pal_fx))
#define FXE(i) ((u16)(FX + 0x387 + 0x25 * (i)))

/* AP 51a8:0000 */
void pal_fx_create(s16 cap, FarPtr base)
{
    u16 n = (u16)(cap * 0x25 + 0x3AC);
    DSW(AP_DS_pal_fx) = arena_alloc(n);
    for (u16 i = 0; i < n; i++) DSB((u16)(FX + i)) = 0;
    DSS(FX) = cap;
    ds_far_wr((u16)(FX + 4), base);
}

/* AP 51a8:004e */
s16 pal_fx_add(u16 src_ds, s16 i)
{
    u16 e = FXE(i);
    if (!(DSS(FX) > i)) return 0;
    s16 n1 = (s16)(i + 1);
    DSS(FX + 2) = DSS(FX + 2) > n1 ? DSS(FX + 2) : n1;
    for (u16 k = 0; k < 0x25; k++) DSB((u16)(e + k)) = DSB((u16)(src_ds + k));
    DSB(e + 0x22) = 0;
    DSW(e + 0x23) = 0;
    return 1;
}

/* AP 1792:0000 */
s16 pal_fx_level(s16 i) { return DSS(FXE(i) + 0x23); }

/* AP 1792:0021 (no caller found) */
void pal_fx_set_base(FarPtr p) { ds_far_wr((u16)(FX + 4), p); }

/* AP 1792:0032 */
void pal_fx_set(s16 i, u8 active, s16 target)
{
    u16 e = FXE(i);
    if (target != -1) {
        DSS(e + 0x1C) = target;
        if (target == 0) active = 0;
    }
    DSB(e + 0x22) = active;
}

/* AP 5440:07bd — video_palette.md §8.3 */
static u8 pal_request_upload(u16 src, u16 first, u16 count)
{
    if (DSW(AP_DS_pal_pend_src) != 0) return 0;
    DSW(AP_DS_pal_pend_src) = src;
    DSW(AP_DS_pal_pend_first) = first;
    DSW(AP_DS_pal_pend_count) = count;
    return 1;
}

/* AP 1792:0066 */
void pal_fx_show_base(void)
{
    rtl__fmemcpy(ds_ptr((u16)(FX + 8)), ds_far((u16)(FX + 4)), 0x300);
    pal_request_upload((u16)(FX + 8), 0, 0x100);
}

/* AP 1792:009b */
void pal_fx_reset_levels(void)
{
    for (s16 i = 0; DSS(FX + 2) > i; i++) DSW(FXE(i) + 0x23) = 0;
}

/* AP 1792:038a (near): T[d] = ((d-1) * lvl) >> 6, T[-d] = -T[d], d = 1..63 (at fx + 347h) */
void pal_fx_build_delta(s16 lvl)
{
    s16 acc = 0;
    for (s16 d = 1; d < 0x40; d++) {
        s8 v = (s8)(acc >> 6);
        DSC((u16)(FX + 0x347 + d)) = v;
        DSC((u16)(FX + 0x347 - d)) = (s8)-v;
        acc = (s16)(acc + lvl);
    }
}

/* AP 1792:0286 — move the colours first..last towards the tint by the table of the level */
static void pal_fx_tint(u16 work, u16 col, u16 first, u16 last, s16 lvl)
{
    if (lvl == 0) return;
    u8 r = DSB(col), g = DSB(col + 1), b = DSB(col + 2);
    if (DSS(AP_DS_pal_fx_cached_level) != lvl) {
        DSS(AP_DS_pal_fx_cached_level) = lvl;
        pal_fx_build_delta(lvl);
    }
    for (u16 k = first; (s16)k <= (s16)last; k++) {
        u16 w = (u16)(work + 3 * k);
        DSB(w) = (u8)(DSB(w) + DSB((u16)(FX + 0x347 + (s16)(r - (s8)DSB(w)))));
        DSB(w + 1) = (u8)(DSB(w + 1) + DSB((u16)(FX + 0x347 + (s16)(g - (s8)DSB(w + 1)))));
        DSB(w + 2) = (u8)(DSB(w + 2) + DSB((u16)(FX + 0x347 + (s16)(b - (s8)DSB(w + 2)))));
        DSB(w) = (u8)clamp((s16)(s8)DSB(w), 0, 0x3F);
        DSB(w + 1) = (u8)clamp((s16)(s8)DSB(w + 1), 0, 0x3F);
        DSB(w + 2) = (u8)clamp((s16)(s8)DSB(w + 2), 0, 0x3F);
    }
}

/* AP 1792:00c0 — video_palette.md §8.4 */
void pal_fx_update(s16 force)
{
    u8 changed = 0;
    rtl__fmemcpy(ds_ptr((u16)(FX + 8)), ds_far((u16)(FX + 4)), 0x300);
    for (s16 i = 0; DSS(FX + 2) > i; i++) {
        u16 e = FXE(i);
        s16 cur = DSS(e + 0x23), tgt = DSS(e + 0x1C);
        s16 old = (s16)(cur >> 8);
        if (DSB(e + 0x22)) {
            if (cur < tgt) {
                if (DSW(e + 0x1E) == 0) cur = tgt;
                else { cur = (s16)(cur + dt_scale(DSS(e + 0x1E))); if (cur > tgt) cur = tgt; }
            } else if (cur > tgt) {
                if (DSW(e + 0x20) == 0) cur = tgt;
                else { cur = (s16)(cur - dt_scale(DSS(e + 0x20))); if (cur < tgt) cur = tgt; }
            }
        } else if (cur != 0) {
            if (DSW(e + 0x20) == 0) cur = 0;
            else {
                s16 a = dt_scale(DSS(e + 0x20));
                bool ge = cur >= a;                              /* sub; jge: signed compare of the operands */
                cur = (s16)(cur - a);
                if (!ge) cur = 0;
            }
        }
        DSS(e + 0x23) = cur;
        if ((s16)(cur >> 8) != old) changed = 1;
    }
    if (!changed && (u8)force == 0) return;
    for (s16 i = 0; DSS(FX + 2) > i; i++) {
        u16 e = FXE(i);
        u16 col = DSB(e + 0x18) ? (u16)(e + 0x19) : (u16)(FX + 3 * DSW(e + 0x19) + 8);
        s16 lvl = (s16)(DSS(e + 0x23) >> 8);
        u16 r = e;
        for (s16 k = 0; k < 6; k++, r = (u16)(r + 4)) {
            if (DSW(r + 2) == 0) break;
            pal_fx_tint((u16)(FX + 8), col, DSW(r), DSW(r + 2), lvl);
        }
    }
    pal_request_upload((u16)(FX + 8), 0, 0x100);
}

/* AP 5440:07e1 — the end of the flight frame: timer, VCR overlay, page flip, then the pending DAC upload */
void frame_present(s16 doit)
{
    if (!doit) return;
    timeout_start(AP_DS_flip_timeout, 3);
    s16 w = vcr_panel_mouse_show();
    gfx_flip(w);
    if (DSW(AP_DS_pal_pend_src) != 0) {
        dac_write(DSW(AP_DS_pal_pend_src), (s16)DSW(AP_DS_pal_pend_first), (s16)DSW(AP_DS_pal_pend_count));
        DSW(AP_DS_pal_pend_src) = 0;
        DSW(AP_DS_pal_pend_first) = 0;
        DSW(AP_DS_pal_pend_count) = 0;
    }
    vcr_panel_mouse_hide();
}

/* AP 606e:029d — video_palette.md §8.5: VCR banner with the 18-step dimming of colours 0..174 */
void vcr_message(u16 text_ds)
{
    u16 mark = stk_mark();
    u16 st = stk_alloc(0x1C), buf = stk_alloc(0x300), buf2 = stk_alloc(0x300);
    gfx_state_save(st);
    DRVB(D_CLIP_ON) = 0;
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_FRONT);
    if (text_ds) banner_text(text_ds);
    if (DSW(AP_DS_vcr_dim_flag)) {
        dac_read(buf, 0, 0xAF);
        for (s16 k = 0x11; k >= 0; k--) {
            for (u16 i = 0; i < 0x20D; i++)
                DSB((u16)(buf2 + i)) = (u8)idiv16_retry((s32)((s16)(s8)DSB((u16)(buf + i)) * k), 0x12, NULL);
            dac_write(buf2, 0, 0xAF);
            vcr_poll_panel();
        }
    }
    item_center(far_make(0, 0xC5));                              /* one word pushed (C5h) */
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
    gfx_rect(0, 0, DSS(lds(G_drv_scr_w)), 0x79);
    banner_text(text_ds);
    write_front();
    read_back();
    gfx_copy_rect(0, 0, DSS(lds(G_drv_scr_w)), 0x79);
    if (DSW(AP_DS_vcr_dim_flag)) dac_write(buf, 0, 0xAF);
    gfx_state_restore(st);
    stk_release(mark);
}

/* AP 644a:0912 (near) — ACES.PAL into the base palette DS:408A, then pal_fx_update(1) */
static void cockpit_pal_copy(u16 name_ds)
{
    u32 r = pal_load(res_name(name_ds, 0x05CC /* "PAL" */));
    FarPtr p = far_make((u16)(r >> 16), (u16)r);
    rtl__fmemcpy(ds_far(AP_DS_sim_palette), p, 0x300);
    pal_free(p);
    pal_fx_update(1);
}
void cockpit_pal_aces(void) { cockpit_pal_copy(0x0F79 /* "ACES" */); }
/* AP 644a:0961 (near) — GUNNER.PAL */
void cockpit_pal_gunner(void) { cockpit_pal_copy(0x0F7E /* "GUNNER" */); }

__attribute__((constructor)) static void palette_fx_codeptrs(void)
{
    codeptr_register(PROG_INTRO, INTRO_FN_intro_tick_cb, intro_tick_cb);
    codeptr_register(PROG_PS, PS_FN_shell_tick_cb, shell_tick_cb);
}
