/* Bitmap drawing in the library (video_library.md §7.3-7.4, §9.4, §9.8; video_rotate.md): the DDA
 * helper, scaled sprites, SCN run-length sprites, the rotated sprite, PS's 4-point bitmap mapper, AP's
 * rotated rectangle, AP's entry-3 thunk and INTRO's cos16. Transcribed from INTRO 05da (PS 16e3 / AP
 * 1d32 are the same code). */
#include "video/vid_priv.h"

#include "render3d/render3d.h"

/* A DDA record as the library keeps it on its stack: {frac, int (= start), step lo, step hi (= end on
 * input)}. Stepping: frac += step_lo; int += step_hi + carry; the value is read before the step. */
typedef struct { u16 frac; s16 i; u16 slo; s16 shi; } Dda;

static void dda_step(Dda *d)
{
    u32 f = (u32)d->frac + d->slo;
    d->frac = (u16)f;
    d->i = (s16)(d->i + d->shi + (s16)(f >> 16));
}

/* INTRO 05da:24f3 / PS 16e3:2b07 / AP 1d32:4941 — video_library.md §7.3. The port keeps the record in a
 * host struct (only the library's own stack frames hold it). */
static s16 dda_init(Dda *d, s16 n)
{
    if (n <= 0) { d->shi = 0; d->slo = 0; d->frac = 0; return 0; }
    d->frac = 0;
    s32 step = (s32)((u32)(u16)(d->shi - d->i) << 16);            /* (end - start) << 16 */
    step = step / (s32)n;                                          /* LDIV@ */
    bool neg = step < 0;
    if (neg) step = -step;
    d->frac = (((u32)step & 0x8000u) | ((u32)step >> 16)) ? (u16)step : 0x8000;
    if (neg) step = -step;
    d->slo = (u16)step;
    d->shi = (s16)((u32)step >> 16);
    return 1;
}
static Dda dda_make(s16 start, s16 end) { Dda d = { 0, start, 0, end }; return d; }

/* The exported name of the helper (other modules have no use for it: the records live on stacks). */
s16 gfx_dda_init(u16 s_ds_unused, s16 n)
{
    Dda d = { DSW(s_ds_unused), DSS(s_ds_unused + 2), DSW(s_ds_unused + 4), DSS(s_ds_unused + 6) };
    s16 r = dda_init(&d, n);
    DSW(s_ds_unused) = d.frac; DSS(s_ds_unused + 2) = d.i; DSW(s_ds_unused + 4) = d.slo; DSS(s_ds_unused + 6) = d.shi;
    return r;
}

/* ------------------------------------------------------------------------ sin / cos */

/* INTRO 1739:080d cos16 (the INTRO copy of the 3Space routine; table 1739:0000) — video_rotate.md */
static u16 intro_cos16(u16 a)
{
    s16 v = (s16)((s16)a >> 4);
    if (v < 0) v = (s16)-v;
    if (v >= 0x400) return (u16)-(s16)code_rd16(SEG(0x1739), (u16)((0x800 - v) * 2));
    return code_rd16(SEG(0x1739), (u16)(v * 2));
}
u16 vid_cos(u16 a) { return pid() == PROG_INTRO ? intro_cos16(a) : cos16(a); }
s16 vid_sin(u16 a) { return pid() == PROG_INTRO ? (s16)intro_cos16((u16)(a + 0xC000)) : sin16(a); }

/* EGA set-up before a sprite (drv[21] == 10h; never on VGA) */
static void ega_pre(void)
{
    if (DRVB(D_EGA_MODE) == 0x10) { vga_outw(0x3CE, 0x0001); vga_outw(0x3CE, 0x0005); vga_out(0x3CE, 8); }
}

static s16 mul14(s16 a, s16 b) { return (s16)(((s32)a * b) >> 14); }

/* ------------------------------------------------------------------------ AP entry 3 thunk */

/* AP 1d32:3de1 — video.md §3: entry 3 */
void gfx_sprite(u16 f_ds, s16 x, s16 y) { ((DrvSpriteFn)drv_entry(3))(f_ds, x, y); }

/* ------------------------------------------------------------------------ scaled sprites */

/* INTRO 05da:25a6 / PS 16e3:2bba / AP 1d32:49f4 — video_library.md §7.3 */
void gfx_sprite_scaled(u16 f_ds, s16 x, s16 y, s16 flags, s16 w, s16 h)
{
    u16 cols = lds(G_scale_cols), rows = lds(G_scale_rows);
    if (w < 0) { w = (s16)-w; flags |= 2; }
    if (h < 0) { h = (s16)-h; flags |= 1; }
    s16 W = w < 0x280 ? w : 0x280;
    s16 H = h < 0x190 ? h : 0x190;
    s16 fw = DSS(f_ds + 6), fh = DSS(f_ds + 8);
    Dda d = (flags & 2) ? dda_make((s16)(fw - 1), 0) : dda_make(0, (s16)(fw - 1));
    dda_init(&d, (s16)(w - 1));
    s16 i;
    for (i = 0; i < W; i++) { DSS(cols + 2 * i) = d.i; dda_step(&d); }
    DSW((u16)(cols + 2 * i)) += 1;                               /* stale entry (reproduced) */
    d = dda_make(0, (s16)(fh - 1));
    dda_init(&d, (s16)(h - 1));
    s16 rowbytes = (s16)(fw >> DSB((u16)(lds(G_adapter_pix_shift) + (s8)DRVB(D_ADAPTER))));
    s16 r = 0, off = 0;
    for (s16 j = 0; j < H; j++) {
        s16 v = d.i;
        dda_step(&d);
        while (v > r) { r++; off = (s16)(off + rowbytes); }
        if (flags & 1) DSS(rows + 2 * (H - j - 1)) = off;
        else DSS(rows + 2 * j) = off;
    }
    s16 y2 = (s16)(H + y), x2 = (s16)(W + x), top = y, left = x, skip = 0;
    if (DRVB(D_CLIP_ON)) {
        if (x2 > DRVS(D_CLIP_X2)) x2 = (s16)(x2 - (x2 - DRVS(D_CLIP_X2) - 1));
        if (y2 > DRVS(D_CLIP_Y2)) y2 = (s16)(y2 - (y2 - DRVS(D_CLIP_Y2) - 1));
        if (top < DRVS(D_CLIP_Y1)) top = DRVS(D_CLIP_Y1);
        if (left < DRVS(D_CLIP_X1)) { skip = (s16)(DRVS(D_CLIP_X1) - left); left = DRVS(D_CLIP_X1); }
    }
    FarPtr pix = far_make(DSW(f_ds), DSW(f_ds + 2));
    if ((s16)(y2 - top) <= 0) return;
    if ((s16)(x2 - left) <= 1) return;
    ega_pre();
    u16 page = DRVW(D_PAGE_WRITE);
    if (DRVW(D_USE_E28)) ((DrvWord1Fn)drv_entry(28))(page);
    DrvSpanColsFn e37 = (DrvSpanColsFn)drv_entry(37);
    for (s16 rr = top; rr < y2; rr++) {
        FarPtr src = far_make(pix.seg, (u16)(DSW((u16)(rows + 2 * (rr - y))) + pix.off));
        e37(page, drv_row(rr), left, (s16)(x2 - left), src, (u16)(cols + 2 * skip), (u16)rr);
        e37 = (DrvSpanColsFn)drv_entry(37);
    }
    gfx_ega_restore();
}

/* INTRO 05da:2827 / PS 16e3:2e3b / AP 1d32:4c75 */
void gfx_sprite_scaled_centered(u16 f_ds, s16 xc, s16 yc, s16 flags, s16 scale)
{
    s16 w = (s16)(((s32)DSS(f_ds + 6) * scale) >> 10);
    s16 h = (s16)(((s32)DSS(f_ds + 8) * scale) >> 10);
    gfx_sprite_scaled(f_ds, (s16)(xc - (w >> 1)), (s16)(yc - (h >> 1)), flags, w, h);
}

/* ------------------------------------------------------------------------ SCN sprites */

/* INTRO 05da:1e3c / PS 16e3:2450 / AP 1d32:428a — video_library.md §7.4 (literals via entry 38, runs via
 * entry 10; the literal buffer lives on the emulated stack because its address goes to the driver). */
void gfx_sprite_scn(u16 f_ds, s16 x, s16 y, s16 flags)
{
    u16 page = DRVW(D_PAGE_WRITE);
    if (DRVW(D_USE_E28)) ((DrvWord1Fn)drv_entry(28))(page);
    s16 fw = DSS(f_ds + 6), fh = DSS(f_ds + 8);
    u8 clip = DRVB(D_CLIP_ON);
    if (clip && x >= DRVS(D_CLIP_X1) && (s16)(x + fw) <= DRVS(D_CLIP_X2) && y >= DRVS(D_CLIP_Y1) &&
        (s16)(y + fh) <= DRVS(D_CLIP_Y2))
        clip = 0;
    s16 dy;
    if (flags & 1) { dy = -1; y = (s16)(y + fh - 1); } else dy = 1;
    if (flags & 2) x = (s16)(x + fw - 1);
    bool row_vis = false;
    u16 rowofs = 0;
    if (clip) {
        row_vis = y <= DRVS(D_CLIP_Y2) && y >= DRVS(D_CLIP_Y1);
        if (row_vis) rowofs = drv_row(y);
    } else rowofs = drv_row(y);
    u16 ps = DSW(f_ds), po = DSW(f_ds + 2);
    u8 base = rd8(ps, po++);
    u16 mark = stk_mark();
    u16 tmp = stk_alloc(0x158);
    s16 x1 = DRVS(D_CLIP_X1), x2 = DRVS(D_CLIP_X2);
    for (;;) {
        u8 b = rd8(ps, po++);
        if (b & 0x80) {
            if (b & 0x40) {                                      /* 11nnnnnn: literal */
                u8 n = b & 0x3F, k = n;
                u16 t = tmp;
                while (k) {
                    u8 v = rd8(ps, po++);
                    DSB(t++) = (u8)((v >> 4) + base);
                    if (--k == 0) break;
                    DSB(t++) = (u8)((v & 15) + base);
                    k--;
                }
                u16 src = tmp;
                s16 nx;
                if (flags & 2) {
                    nx = (s16)(x - n);
                    if (clip) {
                        if (!row_vis) goto lit_done;
                        if (!(nx >= x1 && x < x2)) {
                            if (nx < x1) {
                                s16 kk = (s16)(x1 - nx);
                                if (kk > 63) goto lit_done;
                                n = (u8)(n - kk);
                                if ((s8)n <= 0) goto lit_done;
                            } else {
                                s16 kk = (s16)(x - x2);
                                if (kk > 63) goto lit_done;
                                n = (u8)(n - kk);
                                if ((s8)n <= 0) goto lit_done;
                                src = (u16)(src + kk);
                                x = x2;
                            }
                        }
                    }
                    ((DrvRowCopyFn)drv_entry(38))(page, rowofs, x, n, ds_ptr(src), true, (u16)y);
                } else {
                    nx = (s16)(x + n);
                    if (clip) {
                        if (!row_vis) goto lit_done;
                        if (!(x >= x1 && nx <= x2)) {
                            if (x < x1) {
                                s16 kk = (s16)(x1 - x);
                                if (kk > 63) goto lit_done;
                                n = (u8)(n - kk);
                                if ((s8)n <= 0) goto lit_done;
                                src = (u16)(src + kk);
                                x = x1;
                            } else {
                                s16 kk = (s16)(nx - x2 - 1);
                                if (kk > 63) goto lit_done;
                                n = (u8)(n - kk);
                                if (!((s8)n > 0)) goto lit_done;
                            }
                        }
                    }
                    ((DrvRowCopyFn)drv_entry(38))(page, rowofs, x, n, ds_ptr(src), false, (u16)y);
                }
lit_done:
                x = nx;
            } else {                                             /* 10nnnnnn v: run */
                u8 n = b & 0x3F;
                u8 v = rd8(ps, po++);
                u8 col = (u8)(base + v);
                s16 nx;
                if (flags & 2) {
                    nx = (s16)(x - n);
                    if (clip) {
                        if (!row_vis) goto run_done;
                        if (!(nx >= x1 && x < x2)) {
                            if (nx < x1) {
                                s16 kk = (s16)(x1 - nx);
                                if (kk > 63) goto run_done;
                                n = (u8)(n - kk);
                                if ((s8)n <= 0) goto run_done;
                            } else {
                                s16 kk = (s16)(x - x2);
                                if (kk > 63) goto run_done;
                                n = (u8)(n - kk);
                                if ((s8)n <= 0) goto run_done;
                                x = x2;
                            }
                        }
                    }
                    ((DrvHspanFn)drv_entry(10))(page, rowofs, (s16)(x - n + 1), n, col, 0, (u16)y);
                } else {
                    nx = (s16)(x + n);
                    if (clip) {
                        if (!row_vis) goto run_done;
                        if (!(x >= x1 && nx <= x2)) {
                            if (x < x1) {
                                s16 kk = (s16)(x1 - x);
                                if (kk > 63) goto run_done;
                                n = (u8)(n - kk);
                                if ((s8)n <= 0) goto run_done;
                                x = (s16)(x + kk);
                            } else {
                                s16 kk = (s16)(nx - x2 - 1);
                                if (kk > 63) goto run_done;
                                n = (u8)(n - kk);
                                if (!((s8)n > 0)) goto run_done;
                            }
                        }
                    }
                    ((DrvHspanFn)drv_entry(10))(page, rowofs, x, n, col, 0, (u16)y);
                }
run_done:
                x = nx;
            }
        } else if (b & 0x40) {                                   /* 01nnnnnn: skip / end */
            u8 n = b & 0x3F;
            if (n == 0) break;
            if (flags & 2) x = (s16)(x - n); else x = (s16)(x + n);
        } else {                                                 /* 00nnnnnn: newline */
            u8 n = b & 0x3F;
            y = (s16)(y + dy);
            if (clip) {
                row_vis = y <= DRVS(D_CLIP_Y2) && y >= DRVS(D_CLIP_Y1);
                if (row_vis) rowofs = drv_row(y);
            } else rowofs = drv_row(y);
            if (flags & 2) x = (s16)(x + n); else x = (s16)(x - n);
            u8 b2 = rd8(ps, po);
            if (b2 & 0xC0) continue;
            s16 m = b2 & 0x3F;
            if (m == 0) continue;
            po++;
            m = (s16)(m << 6);
            if (flags & 2) x = (s16)(x + m); else x = (s16)(x - m);
        }
    }
    stk_release(mark);
}

/* ------------------------------------------------------------------------ rotated sprite */

/* INTRO 05da:3c84 / PS 16e3:3fa8 / AP 1d32:5de2 (near) — video_rotate.md "Helpers": one x per row y0..y1
 * (rows outside 0 .. scr_h-1 give no entry), clamped to clip x2. Returns the next output pointer. */
static u16 rot_edge_dda(u16 out, s16 x0, s16 y0, s16 x1, s16 y1)
{
    s16 n = (s16)(y1 - (y0 - 1));
    Dda d = dda_make(x0, x1);
    dda_init(&d, n);
    u16 cx = (u16)n;
    if (cx == 0) return out;
    s16 si = y0;
    do {
        s16 v = d.i;
        dda_step(&d);
        if (si >= 0 && si < DRVS(D_SCR_H)) {
            if (v > DRVS(D_CLIP_X2)) v = DRVS(D_CLIP_X2);
            DSS(out) = v;
            out = (u16)(out + 2);
        }
        si++;
    } while (--cx);
    return out;
}

#define ROT_TAB 1024                                              /* host bound of the stack arrays */

/* INTRO 05da:3d01 / PS 16e3:4025 / AP 1d32:5e5f — video_rotate.md (transcribed from the code) */
void gfx_sprite_rotated(u16 f_ds, s16 x, s16 y, u16 angle, s16 w, s16 h, s16 px, s16 py)
{
    s16 rowtab[200], A[ROT_TAB], Bt[ROT_TAB];
    if (w == 0) goto done;
    if (w < 0) w = (s16)-w;
    if (h == 0) goto done;
    if (h < 0) h = (s16)-h;
    {
    s16 c = (s16)vid_cos(angle), s = vid_sin(angle);
    s16 fw = DSS(f_ds + 6), fh = DSS(f_ds + 8);
    s16 x0 = x, y0 = y, t;
    if (py) { t = idiv16_retry((s32)py * h, fh, NULL); y0 = (s16)(y0 - mul14(t, c)); x0 = (s16)(x0 + mul14(t, s)); }
    if (px) { t = idiv16_retry((s32)px * w, fw, NULL); x0 = (s16)(x0 - mul14(t, c)); y0 = (s16)(y0 - mul14(t, s)); }
    s16 hc = mul14(h, c), hs = mul14(h, s), wc = mul14(w, c), ws = mul14(w, s);
    u16 CX = lds(G_rot_cx), CY = lds(G_rot_cy);
#define X(i) DSS((u16)(CX + (i)))                                 /* byte-indexed as the code does */
#define Y(i) DSS((u16)(CY + (i)))
    X(0) = x0; X(2) = (s16)(x0 + wc); X(4) = (s16)(X(2) - hs); X(6) = (s16)(X(4) - wc);
    Y(0) = y0; Y(2) = (s16)(y0 + ws); Y(4) = (s16)(Y(2) + hc); Y(6) = (s16)(Y(4) - ws);
    for (s16 k = 0, v = 0; k < 200; k++, v = (s16)(v + fw)) rowtab[k] = v;
    s16 fsize = (fh >= 0 && fh < 200) ? rowtab[fh] : 0;         /* PORT: f.h >= 200 read the stack */
    s16 minv = 0x7D00;
    u16 dxi = 4;
    for (u16 cc = 4; cc >= 1; cc--) { s16 v = Y((4 - cc) * 2); if (v < minv) { minv = v; dxi = cc; } }
    u16 T = (u16)((4 - dxi) * 2);
    u16 iT = lds(G_rot_iT), iL = lds(G_rot_iL), iB = lds(G_rot_iB), iR = lds(G_rot_iR);
    DSW(iT) = T;
    if (T == 0 && angle >= 0x4000) DSW(iT) = 6;
    if (angle < 0x8000 && Y(4) == Y(6)) DSW(iT) = (u16)(DSW(iT) + 2);
    T = DSW(iT);
    u16 L = (u16)((T + 2) & 6), B = (u16)((L + 2) & 6), R = (u16)((B + 2) & 6);
    DSW(iL) = L; DSW(iB) = B; DSW(iR) = R;
    u8 q = (u8)(((8 - T) & 6) >> 1);
    u16 list = lds(G_scale_rows), tex = lds(G_scale_cols);
    u16 lp = list;
    s16 ystart = Y(T) < 0 ? 0 : Y(T);
    DSS(lp) = ystart;
    rot_edge_dda((u16)(lp + 2), X(T), Y(T), X(R), Y(R));
    s16 k2 = (s16)(Y(R) - ystart); if (k2 < 0) k2 = 0;
    rot_edge_dda((u16)(lp + 2 * (k2 + 1)), X(R), Y(R), X(B), Y(B));
    s16 fh1 = (s16)(fh - 1), fw1 = (s16)(fw - 1);
    if (q & 1) { t = fh1; fh1 = fw1; fw1 = t; t = c; c = s; s = t; t = w; w = h; h = t; }
    s16 nA = 0, nB = 0, n, m, si = 0;
    Dda U, V;
    u8 mode;
    if (Y(R) < Y(L)) {                                            /* branch 1 */
        s32 p = (s32)(s16)(Y(R) - Y(T)) * (s16)(X(L) - X(T));
        s16 dv = (s16)(Y(L) - Y(T));
        n = dv ? idiv16_retry(p, dv, NULL) : (s16)p;
        n = (s16)(n + X(T) - X(R));
        if (n == 0) goto done;
        s16 kk = 0;
        if (s) { kk = idiv16_retry((s32)h * c, s, NULL); if (kk < 0) kk = (s16)-kk; }
        s32 pr = (s32)kk * fw1;
        u32 num = ((u32)pr >> 16) ? 0xFFFFu : (u16)pr;
        m = (s16)div16_retry(num, (u16)w, NULL);
        if ((u16)m >= 0x140) m = 0x140;
        U = dda_make(0, fh1); dda_init(&U, n);
        V = dda_make(0, m); dda_init(&V, n);
        mode = q;
    } else {                                                      /* branch 2 */
        s32 p = (s32)(s16)(Y(R) - Y(B)) * (s16)(X(L) - X(B));
        s16 dv = (s16)(Y(L) - Y(B));
        n = dv ? idiv16_retry(p, dv, NULL) : (s16)p;
        n = (s16)(n + X(B) - X(R));
        if (n == 0) goto done;
        s16 kk = 0;
        if (c) { kk = idiv16_retry((s32)w * s, c, NULL); if (kk < 0) kk = (s16)-kk; }
        s32 pr = (s32)kk * fh1;
        u32 num = ((u32)pr >> 16) ? 0xFFFFu : (u16)pr;
        m = (s16)div16_retry(num, (u16)h, NULL);
        if ((u16)m >= 0x140) m = 0x140;
        U = dda_make(0, m); dda_init(&U, n);
        V = dda_make(0, fw1); dda_init(&V, n);
        mode = q;
    }
    do {
        s16 a = U.i; dda_step(&U);
        s16 b = V.i; dda_step(&V);
        s16 cnt = (s16)(a - nA + 1);
        if (cnt > 0) { for (s16 j = 0; j < cnt; j++) if ((u16)(nA + j) < ROT_TAB) A[nA + j] = si; nA = (s16)(nA + cnt); }
        cnt = (s16)(b - nB + 1);
        if (cnt >= 0) { for (s16 j = 0; j < cnt; j++) if ((u16)(nB + j) < ROT_TAB) Bt[nB + j] = si; nB = (s16)(nB + cnt); }
        s16 o;
        switch (mode) {
        case 0: o = (s16)(b - rowtab[(u16)a % 200]); break;
        case 1: o = (s16)(-a - rowtab[(u16)b % 200]); break;
        case 2: o = (s16)(rowtab[(u16)a % 200] - b); break;
        default: o = (s16)(a + rowtab[(u16)b % 200]); break;
        }
        DSS((u16)(tex + 2 * si)) = o;
        si++;
    } while (si < n);
    { s16 cnt = (s16)(fh1 + 1 - nA); if (cnt >= 0) for (s16 j = 0; j < cnt; j++) if ((u16)(nA + j) < ROT_TAB) A[nA + j] = si; }
    { s16 cnt = (s16)(fw1 + 1 - nB); if (cnt >= 0) for (s16 j = 0; j < cnt; j++) if ((u16)(nB + j) < ROT_TAB) Bt[nB + j] = si; }
#define AT(arr, i) ((u16)(i) < ROT_TAB ? (arr)[(i)] : 0)
#define RT(i) rowtab[(u16)(i) % 200]
    /* upper part: rows Y[T] .. Y[R]-1 */
    U = dda_make(0, fh1); dda_init(&U, (s16)(Y(R) - Y(T)));
    ystart = DSS(lp); lp = (u16)(lp + 2);
    u16 page = DRVW(D_PAGE_WRITE);
    if (DRVW(D_USE_E28)) ((DrvWord1Fn)drv_entry(28))(page);
    for (s16 yy = Y(T); yy < Y(R); yy++) {
        s16 a = U.i; dda_step(&U);
        if (yy < ystart) continue;
        if (yy >= DRVS(D_CLIP_Y1) && yy <= DRVS(D_CLIP_Y2)) {
            s16 xx = DSS(lp);
            s16 cnt = (yy <= (s16)(Y(L) + 1)) ? AT(A, a) : AT(Bt, fw1);
            s16 base;
            switch (q) {
            case 0: base = RT(a); break;
            case 1: base = (s16)(fsize - fw + a); break;
            case 2: base = (s16)(fsize - RT(a) - 1); break;
            default: base = (s16)(fw - a - 1); break;
            }
            if ((s16)(cnt + xx) > DRVS(D_CLIP_X2)) cnt = (s16)(DRVS(D_CLIP_X2) - xx + 1);
            s16 skip = 0;
            if ((s16)(xx - DRVS(D_CLIP_X1)) < 0) { skip = (s16)(DRVS(D_CLIP_X1) - xx); xx = (s16)(xx + skip); cnt = (s16)(cnt - skip); }
            if (cnt > 0)
                ((DrvSpanTexFn)drv_entry(36))(f_ds, (u16)base, (u16)(tex + 2 * skip), xx, cnt, page, drv_row(yy), (u16)(yy << 1));
        }
        lp = (u16)(lp + 2);
    }
    /* lower part: rows Y[R] .. Y[B]-1 */
    U = dda_make(0, fw1); dda_init(&U, (s16)(Y(B) - Y(R)));
    for (s16 yy = Y(R); yy < Y(B); yy++) {
        s16 a = U.i; dda_step(&U);
        if (yy < ystart) continue;
        if (yy >= DRVS(D_CLIP_Y1) && yy <= DRVS(D_CLIP_Y2)) {
            s16 xx = DSS(lp);
            s16 cnt = (yy <= (s16)(Y(L) - 1)) ? AT(A, fh1) : AT(Bt, (s16)(fw1 - a));
            s16 base;
            switch (q) {
            case 0: base = (s16)(fsize - fw + a); break;
            case 1: base = (s16)(fsize - RT(a) - 1); break;
            case 2: base = (s16)(fw - a - 1); break;
            default: base = RT(a); break;
            }
            if ((s16)(cnt + xx) > DRVS(D_CLIP_X2)) cnt = (s16)(DRVS(D_CLIP_X2) - xx + 1);
            s16 skip = 0;
            if ((s16)(xx - DRVS(D_CLIP_X1)) < 0) { skip = (s16)(DRVS(D_CLIP_X1) - xx); xx = (s16)(xx + skip); cnt = (s16)(cnt - skip); }
            if (cnt > 0)
                ((DrvSpanTexFn)drv_entry(36))(f_ds, (u16)base, (u16)(tex + 2 * skip), xx, cnt, page, drv_row(yy), (u16)(yy << 1));
        }
        lp = (u16)(lp + 2);
    }
#undef AT
#undef RT
#undef X
#undef Y
    }
done:
    gfx_ega_restore();
}

/* ------------------------------------------------------------------------ PS 4-point bitmap */

/* PS 16e3:0c58 (near) — video_library.md §9.4 */
static s16 quad_orient(s16 xp, s16 yp, s16 xt, s16 yt, s16 xn, s16 yn)
{
    s16 dyP = (s16)(yp - yt), dyN = (s16)(yn - yt), dxP = (s16)(xp - xt), dxN = (s16)(xn - xt), t;
    if (dxP == 0) { dxP = 1; dyP = 0x7FFF; }
    if (dxN == 0) { dxN = 1; dyN = 0x7FFF; }
    if (dxP < 0 && dxN < 0) {
        t = dxN; dxN = (s16)-dxP; dxP = (s16)-t;
        t = dyN; dyN = dyP; dyP = t;
    } else if (dxP < 0 && dxN > 0) return 1;
    else if (dxN < 0 && dxP > 0) return -1;
    u16 r1, r2;
    u16 q1 = div16_retry((u32)(s32)dyP, (u16)dxP, &r1);          /* cwd; div */
    u16 q2 = div16_retry((u32)(s32)dyN, (u16)dxN, &r2);
    if ((s16)q2 < (s16)q1) return 1;
    if (q2 > q1) return -1;
    u16 f2 = div16_retry((u32)r2 << 16, (u16)dxN, NULL);
    u16 f1 = div16_retry((u32)r1 << 16, (u16)dxP, NULL);
    return f2 > f1 ? -1 : 1;
}

/* PS 16e3:0b5e (near): texture coordinates of the edge between corners a and b (switch on 2a+b-1) */
static void quad_corner_uv(s16 a, s16 b, u16 fr, s16 *us, s16 *ue, s16 *vs, s16 *ve)
{
    s16 w1 = (s16)(DSS(fr + 6) - 1), h1 = (s16)(DSS(fr + 8) - 1);
    switch ((u16)(2 * a + b - 1)) {
    case 0: *ve = 0; *vs = 0; *us = 0; *ue = w1; break;
    case 1: *ve = 0; *vs = 0; *ue = 0; *us = w1; break;
    case 2: *vs = 0; *ue = 0; *us = 0; *ve = h1; break;
    case 3: *ue = w1; *us = w1; *vs = 0; *ve = h1; break;
    case 4: *ue = w1; *us = w1; *vs = h1; *ve = 0; break;
    case 5: *ve = 0; *ue = 0; *us = 0; *vs = h1; break;
    case 6: *ve = h1; *vs = h1; *us = w1; *ue = 0; break;
    case 7: *ve = h1; *vs = h1; *us = 0; *ue = w1; break;
    default: break;
    }
}

/* PS 16e3:065d — video_library.md §9.4 (dead with the shipped shapes: polygon flag 10h is unused).
 * Transcribed from the code; note the top vertex is the LAST minimum (<=) and the clip skip is not
 * reset between rows (both as in the code). */
void gfx_bitmap_quad(u16 fr_ds, u16 p1_ds, u16 p2_ds)
{
    s16 rowtab[200];
    u16 mark = stk_mark();
    u16 table = stk_alloc(0x56A - 0x6A);                          /* the driver reads it through SS */
    s16 skip = 0;
    for (s16 i = 0, v = 0; i < 200; i++, v = (s16)(v + DSS(fr_ds + 6))) rowtab[i] = v;
#define XS(i) DSS((u16)(p1_ds + 2 * ((i) & 3)))
#define YS(i) DSS((u16)(p2_ds + 2 * ((i) & 3)))
    s16 ymin = 0x7D00, ymax = (s16)0x8300, top = 0, bottom = 0;
    for (s16 i = 0; i < 4; i++) {
        s16 v = YS(i);
        if (v <= ymin) { ymin = v; top = i; }
        if (v > ymax) { ymax = v; bottom = i; }
    }
    s16 dir = quad_orient(XS(top - 1), YS(top - 1), XS(top), YS(top), XS(top + 1), YS(top + 1));
    s16 a = top, bA = (s16)((top + dir) & 3);
    s16 cntA = (s16)(YS(bA) - YS(a));
    s16 us, ue, vs, ve;
    Dda Ax = dda_make(XS(a), XS(bA)); dda_init(&Ax, cntA);
    quad_corner_uv(a, bA, fr_ds, &us, &ue, &vs, &ve);
    Dda Au = dda_make(us, ue); dda_init(&Au, cntA);
    Dda Av = dda_make(vs, ve); dda_init(&Av, cntA);
    s16 c;
    for (;;) {
        u16 page = DRVW(D_PAGE_WRITE);
        c = (s16)((a - dir) & 3);
        s16 cntB = (s16)(YS(c) - YS(a));
        quad_corner_uv(a, c, fr_ds, &us, &ue, &vs, &ve);
        Dda Bu = dda_make(us, ue); dda_init(&Bu, cntB);
        Dda Bv = dda_make(vs, ve); dda_init(&Bv, cntB);
        Dda Bx = dda_make(XS(a), XS(c)); dda_init(&Bx, cntB);
        s16 y = YS(a);
        while (cntB >= 0) {
            s16 xl = Bx.i; dda_step(&Bx);
            s16 xr = Ax.i; dda_step(&Ax);
            if (cntA == 0) {
                s16 old = bA;
                bA = (s16)((bA + dir) & 3);
                cntA = (s16)(YS(bA) - YS(old));
                Ax = dda_make(XS(old), XS(bA)); dda_init(&Ax, cntA);
                quad_corner_uv(old, bA, fr_ds, &us, &ue, &vs, &ve);
                Au = dda_make(us, ue); dda_init(&Au, cntA);
                Av = dda_make(vs, ve); dda_init(&Av, cntA);
            } else cntA--;
            s16 bu = Bu.i; dda_step(&Bu);
            s16 au = Au.i; dda_step(&Au);
            s16 bv = Bv.i; dda_step(&Bv);
            s16 av = Av.i; dda_step(&Av);
            if (y >= DRVS(D_CLIP_Y1) && y <= DRVS(D_CLIP_Y2)) {
                s16 n = (s16)(xr - xl);
                Dda Su = dda_make(bu, au); dda_init(&Su, n);
                Dda Sv = dda_make(bv, av); dda_init(&Sv, n);
                u16 tp = table, tstart = table;
                if (xl < DRVS(D_CLIP_X1)) {
                    skip = (s16)(DRVS(D_CLIP_X1) - xl);
                    xl = DRVS(D_CLIP_X1);
                    tstart = (u16)(table + 2 * skip);
                }
                n = (s16)(xr - xl);
                if (n >= 0) {
                    for (s16 k = (s16)(n + skip); k >= 0; k--) {
                        s16 u = Su.i; dda_step(&Su);
                        s16 v = Sv.i; dda_step(&Sv);
                        DSS(tp) = (s16)(u + rowtab[(u16)v % 200]);
                        tp = (u16)(tp + 2);
                    }
                    if ((s16)(xl + n) >= DRVS(D_CLIP_X2)) n = (s16)(DRVS(D_CLIP_X2) - xl);
                    if (n >= 0)
                        ((DrvSpanTexFn)drv_entry(36))(fr_ds, 0, tstart, xl, (s16)(n + 1), page, drv_row(y), (u16)(y << 1));
                }
            }
            y++;
            cntB--;
        }
        a = (s16)((a - dir) & 3);
        if (c == bottom) break;
    }
#undef XS
#undef YS
    stk_release(mark);
    gfx_ega_restore();
}

/* ------------------------------------------------------------------------ AP rotated rectangle */

/* AP 1d32:1eca — video_library.md §9.8 */
void rot_rect_setup(u16 p0_ds, u16 x, u16 y, u16 angle, u16 W, u16 H, u16 hx, u16 hy, u16 p8_ds)
{
    s16 w = (s16)W, h = (s16)H;
    if (w == 0) return;
    if (w < 0) w = (s16)-w;
    if (h == 0) return;
    if (h < 0) h = (s16)-h;
    s16 c = (s16)cos16(angle), s = sin16(angle);
    s16 px = (s16)x, py = (s16)y, k;
    if (hy) { k = idiv16_retry((s32)(s16)hy * h, DSS(p0_ds + 8), NULL); py = (s16)(py - mul14(k, c)); px = (s16)(px + mul14(k, s)); }
    if (hx) { k = idiv16_retry((s32)(s16)hx * w, DSS(p0_ds + 6), NULL); px = (s16)(px - mul14(k, c)); py = (s16)(py - mul14(k, s)); }
    s16 Hc = mul14(h, c), Hs = mul14(h, s), Wc = mul14(w, c), Ws = mul14(w, s);
    u16 XS = 0x1D54 /* AP rot_rect_xs */, YS = 0x1D5C;
    DSS(XS) = px; DSS(XS + 2) = (s16)(px + Wc); DSS(XS + 4) = (s16)(DSS(XS + 2) - Hs); DSS(XS + 6) = (s16)(DSS(XS + 4) - Wc);
    DSS(YS) = py; DSS(YS + 2) = (s16)(py + Ws); DSS(YS + 4) = (s16)(DSS(YS + 2) + Hc); DSS(YS + 6) = (s16)(DSS(YS + 4) - Ws);
    s16 minv = 0x7D00;
    u16 dxi = 4;
    for (u16 cc = 4; cc >= 1; cc--) { s16 v = DSS(YS + (4 - cc) * 2); if (v < minv) { minv = v; dxi = cc; } }
    u16 T = (u16)((4 - dxi) * 2);
    DSW(p8_ds + 2) = T;
    if (T == 0 && angle >= 0x4000) DSW(p8_ds + 2) = 6;
    if (angle < 0x8000 && DSS(YS + 4) == DSS(YS + 6)) DSW(p8_ds + 2) = (u16)(DSW(p8_ds + 2) + 2);
    u16 bx = DSW(p8_ds + 2);
    DSW(p8_ds + 2) = DSW((u16)(YS + bx));
    bx = (u16)((bx + 2) & 6); DSW(p8_ds + 4) = DSW((u16)(XS + bx));
    bx = (u16)((bx + 2) & 6); DSW(p8_ds + 6) = DSW((u16)(YS + bx));
    bx = (u16)((bx + 2) & 6); DSW(p8_ds + 0) = DSW((u16)(XS + bx));
}
