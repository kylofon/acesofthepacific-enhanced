/* Graphics library primitives (video_library.md §1-6, §9.2, §9.6, §9.7): clipped pixels, lines,
 * rectangles, polygons (clip, edge walk, span fill), circles, flood fill, the rotated-sprite bounding
 * box, and AP's clip-list driver hooks. Same code in the three programs (INTRO 05da / PS 16e3 / AP
 * 1d32); globals through the layout (drv[] block, G_poly_*, G_circle_state, G_flood_stack). */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "symbols_ap.h"

/* ---------------------------------------------------------------------------------- pixels */

/* INTRO 05da:48cb / PS 16e3:4bef / AP 1d32:6a29: -1 outside the clip window (when clipping), else entry 21 */
s16 gfx_get_pixel(s16 x, s16 y)
{
    if (DRVB(D_CLIP_ON) && (x < DRVS(D_CLIP_X1) || x > DRVS(D_CLIP_X2) || y < DRVS(D_CLIP_Y1) || y > DRVS(D_CLIP_Y2)))
        return -1;
    return (s16)((DrvGetPixelFn)drv_entry(21))(x, y);
}

/* INTRO 05da:48fd / PS 16e3:4c21 / AP 1d32:6a5b: entry 22 unless clipped */
void gfx_put_pixel(s16 x, s16 y, s16 c)
{
    if (DRVB(D_CLIP_ON) && (x < DRVS(D_CLIP_X1) || x > DRVS(D_CLIP_X2) || y < DRVS(D_CLIP_Y1) || y > DRVS(D_CLIP_Y2)))
        return;
    ((DrvPutPixelFn)drv_entry(22))(x, y, (u16)c);
}

/* ----------------------------------------------------------------------------------- lines */

static s16 clip_div(s16 a, s16 b, s16 d) { return idiv16_retry((s32)a * b, d, NULL); }   /* imul; idiv */

/* INTRO 05da:3957 / PS 16e3:3c7b / AP 1d32:5ab5 — video_library.md §2. The ends are ordered with an
 * UNSIGNED compare (cmp bx, dx; jbe) before driver entry 2. */
void gfx_line(s16 x1, s16 y1, s16 x2, s16 y2)
{
    s16 bx = x1, cx = y1, si = x2, di = y2, t;
    if (DRVB(D_CLIP_ON)) {
        s16 ymin = DRVS(D_CLIP_Y1);                              /* top (signed) */
        if (cx < ymin) {
            if (di < ymin) return;
        } else {
            if (di >= ymin) goto left;
            t = si; si = bx; bx = t; t = di; di = cx; cx = t;
        }
        bx = (s16)(bx + clip_div((s16)(si - bx), (s16)(ymin - cx), (s16)(di - cx)));
        cx = ymin;
left:;
        s16 xmin = DRVS(D_CLIP_X1);
        if (bx < xmin) {
            if (si < xmin) return;
        } else {
            if (si >= xmin) goto bottom;
            t = si; si = bx; bx = t; t = di; di = cx; cx = t;
        }
        cx = (s16)(cx + clip_div((s16)(di - cx), (s16)(xmin - bx), (s16)(si - bx)));
        bx = xmin;
bottom:;
        u16 ymax = DRVW(D_CLIP_Y2);                              /* unsigned compares */
        if ((u16)cx > ymax) {
            if ((u16)di > ymax) return;
        } else {
            if ((u16)di <= ymax) goto right;
            t = si; si = bx; bx = t; t = di; di = cx; cx = t;
        }
        bx = (s16)(bx + clip_div((s16)(si - bx), (s16)(ymax - cx), (s16)(di - cx)));
        cx = (s16)ymax;
right:;
        u16 xmax = DRVW(D_CLIP_X2);
        if ((u16)bx > xmax) {
            if ((u16)si > xmax) return;
        } else {
            if ((u16)si <= xmax) goto draw;
            t = si; si = bx; bx = t; t = di; di = cx; cx = t;
        }
        cx = (s16)(cx + clip_div((s16)(di - cx), (s16)(xmax - bx), (s16)(si - bx)));
        bx = (s16)xmax;
    }
draw:;
    s16 dx = si, sy = di;
    if ((u16)bx > (u16)dx) { t = dx; dx = bx; bx = t; t = sy; sy = cx; cx = t; }
    ((DrvLineFn)drv_entry(2))(bx, cx, dx, sy);
}

/* ------------------------------------------------------------------------------ rectangles */

/* INTRO 05da:645b / PS 16e3:61b5 / AP 1d32:6ab0 — video_library.md §3 */
void gfx_rect(s16 x, s16 y, s16 w, s16 h)
{
    s16 x2 = (s16)(w + x - 1), y2 = (s16)(h + y - 1);
    if (DRVB(D_FILL_ON)) {
        s16 cx = x, cy = y, cw = w, ch = h, d;
        if (DRVB(D_CLIP_ON)) {
            d = (s16)(cx - DRVS(D_CLIP_X1)); if (d < 0) { cx = (s16)(cx - d); cw = (s16)(cw + d); }
            d = (s16)(cy - DRVS(D_CLIP_Y1)); if (d < 0) { cy = (s16)(cy - d); ch = (s16)(ch + d); }
            d = (s16)(DRVS(D_CLIP_X2) - x2); if (d < 0) cw = (s16)(cw + d);
            d = (s16)(DRVS(D_CLIP_Y2) - y2); if (d < 0) ch = (s16)(ch + d);
        }
        if (cw > 0 && ch > 0) {
            u16 seg = DRVW(D_SPAN_SEG), o = 0;
            wr16(seg, o, (u16)cy); o += 2;
            wr16(seg, o, (u16)ch); o += 2;
            for (u16 n = (u16)ch; n; n--) {
                wr16(seg, o, (u16)cx); o += 2;
                wr16(seg, o, (u16)(cx + cw - 1)); o += 2;
            }
            gfx_poly_spans(far_make(seg, 0));                    /* lcall [DS:19A4] = entry 27 */
        }
    }
    if (!DRVB(D_FILL_ON) || DRVB(D_DRAW_COL) != DRVB(D_FILL_COL)) {
        gfx_line(x2, y, x2, y2);
        gfx_line(x, y2, x2, y2);
        gfx_line(x, y, x, y2);
        gfx_line(x, y, x2, y);
    }
}

/* -------------------------------------------------------------------------------- polygons */

#define PX(a, i)  DSW((u16)(lds(a) + 2 * (i)))
#define PS_(o)    DSW((u16)(lds(G_poly_scratch) + (o)))          /* scratch words (INTRO 15A0 ...) */
#define PSB(o)    DSB((u16)(lds(G_poly_scratch) + (o)))
enum { SC_TOP = 0x0, SC_BOT = 0x2, SC_C1 = 0x4, SC_C2 = 0x6, SC_EDGES = 0x8, SC_EOFS = 0xA, SC_SLOT = 0xC,
       SC_LASTX = 0xE, SC_LASTY = 0x10, SC_RET = 0x12, SC_ONUM = 0x14, SC_SAVED = 0x16, SC_STRIDE = 0x18,
       SC_AMBIG = 0x19 };

/* INTRO 05da:0fbd / PS 16e3:15d1 / AP 1d32:0dfe (near) — gfx_line(xs[i+1], ys[i+1], xs[i], ys[i]) for
 * i < k (the pushes put the later point first). With yshift the clip y is halved around the calls. */
void poly_outline_lines(u16 xs_ds, u16 ys_ds, s16 k)
{
    u16 si = xs_ds, di = ys_ds;
    u16 bp = (u16)k;
    if (DRVB(D_YSHIFT) == 0) {
        do {
            s16 y0 = DSS(di), x0 = DSS(si);
            si += 2; di += 2;
            gfx_line(DSS(si), DSS(di), x0, y0);
        } while (--bp);
        return;
    }
    DRVW(D_CLIP_Y1) >>= 1;
    DRVW(D_CLIP_Y2) >>= 1;
    do {
        s16 y0 = (s16)(DSS(di) >> 1), x0 = DSS(si);
        si += 2; di += 2;
        gfx_line(DSS(si), (s16)(DSS(di) >> 1), x0, y0);
    } while (--bp);
    DRVW(D_CLIP_Y1) <<= 1;
    DRVW(D_CLIP_Y2) <<= 1;
}

/* The generic writer at 05da:1306 used by the vertical and diagonal edges: `rows` records starting at
 * row di0, x = x0 then x += step per row (bx = bp = 0: no carry). */
static void poly_edge_writer(s16 x0, s16 step, s16 row0, u16 rows)
{
    u16 seg = DRVW(D_SPAN_SEG);
    u16 di = (u16)((u16)(row0 << 2) + PS_(SC_SLOT));
    s16 stride = (s16)(s8)PSB(SC_STRIDE);                        /* 2 (+2 of stosw = one record) */
    s16 x = x0;
    for (u16 i = 0; i < rows; i++) {
        wr16(seg, di, (u16)x);
        di = (u16)(di + 2 + stride);
        x = (s16)(x + step);
    }
}

/* INTRO 05da:1009 / PS 16e3:161d / AP 1d32:0e4a — vertical edge: rows min(y)..max(y) get x */
static void poly_edge_vertical(s16 bx, s16 bp, s16 cx, s16 si)
{
    (void)bx;
    if (!(cx > si)) { s16 t = si; si = cx; cx = t; }
    PSB(SC_STRIDE) = 2;
    poly_edge_writer(bp, 0, si, (u16)(cx - si + 1));
}

/* INTRO 05da:1163 / PS 16e3:1777 / AP 1d32:0fa4 — diagonal edge (|dx| == |dy|) */
static void poly_edge_diag(s16 bx, s16 bp, s16 cx, s16 si)
{
    s16 t;
    if (!(cx < si)) { t = bp; bp = bx; bx = t; t = si; si = cx; cx = t; }
    s16 step = 1;
    if (!(bx < bp)) step = -1;
    PSB(SC_STRIDE) = 2;
    poly_edge_writer(bx, step, cx, (u16)(si - cx + 1));
}

/* INTRO 05da:1025 / PS 16e3:1639 / AP 1d32:0e66 — steep edge (|dx| < |dy|) */
static void poly_edge_steep(s16 bx, s16 bp, s16 cx, s16 si)
{
    s16 t;
    if (!(bx < bp)) { t = bp; bp = bx; bx = t; t = si; si = cx; cx = t; }
    u16 seg = DRVW(D_SPAN_SEG);
    u16 di = (u16)((u16)(cx << 2) + PS_(SC_SLOT));
    s16 d = (s16)(cx - si);
    bool down = d < 0;                                           /* cwd: start above the end */
    s16 dy = (s16)(d < 0 ? -d : d);
    s16 dx = (s16)(bp - bx);
    s16 e = (s16)(2 * dx - dy);
    s16 x = bx;
    for (u16 n = (u16)(dy + 1); n; n--) {
        wr16(seg, di, (u16)x);
        di = (u16)(down ? di + 4 : di - 4);
        if (e >= 0) { x++; e = (s16)(e + 2 * dx - 2 * dy); }
        else e = (s16)(e + 2 * dx);
    }
}

/* INTRO 05da:1245 / PS 16e3:1859 / AP 1d32:1087 — shallow edge, left slot (first x of each row's run) */
static void poly_edge_left(s16 bx, s16 bp, s16 cx, s16 si)
{
    s16 t;
    if (!(bx < bp)) { t = bp; bp = bx; bx = t; t = si; si = cx; cx = t; }
    u16 seg = DRVW(D_SPAN_SEG);
    u16 di = (u16)(cx << 2);
    s16 rstep = 4;
    s16 dy = (s16)(si - cx);
    if (dy < 0) { dy = (s16)-dy; rstep = -4; }
    s16 dx = (s16)(bp - bx);
    s16 e = (s16)(2 * dy - dx);
    s16 x = bx;
    u16 n = (u16)dy;
    wr16(seg, di, (u16)x); di = (u16)(di + rstep); x++;
    while (e < 0) { x++; e = (s16)(e + 2 * dy); }
    for (;;) {
        wr16(seg, di, (u16)x); di = (u16)(di + rstep); x++;
        if (--n == 0) break;
        e = (s16)(e + 2 * dy - 2 * dx);
        while (e < 0) { x++; e = (s16)(e + 2 * dy); }
    }
}

/* INTRO 05da:118a / PS 16e3:179e / AP 1d32:0fcb — shallow edge, right slot (last x of each row's run) */
static void poly_edge_right(s16 bx, s16 bp, s16 cx, s16 si)
{
    s16 t;
    if (!(bx > bp)) { t = bp; bp = bx; bx = t; t = si; si = cx; cx = t; }
    u16 seg = DRVW(D_SPAN_SEG);
    u16 di = (u16)((u16)(cx << 2) + 2);
    s16 rstep = 4;
    s16 dy = (s16)(si - cx);
    if (dy < 0) { dy = (s16)-dy; rstep = -4; }
    s16 dx = (s16)(bx - bp);
    s16 e = (s16)(2 * dy - dx);
    s16 x = bx;
    u16 n = (u16)dy;
    wr16(seg, di, (u16)x); di = (u16)(di + rstep); x--;
    while (e < 0) { x--; e = (s16)(e + 2 * dy); }
    for (;;) {
        wr16(seg, di, (u16)x); di = (u16)(di + rstep); x--;
        if (--n == 0) break;
        e = (s16)(e + 2 * dy - 2 * dx);
        while (e < 0) { x--; e = (s16)(e + 2 * dy); }
    }
}

/* INTRO 05da:2b2f / PS 16e3:3143 / AP 1d32:4f7d — Sutherland-Hodgman, x pass into poly_x2/y2, y pass
 * back into poly_x/y (video_library.md §4.1) */
void gfx_poly_clip(void)
{
    u16 n = DSW(lds(G_poly_n));
    if ((s16)n <= 1) return;
    s16 x1 = DRVS(D_CLIP_X1), x2 = DRVS(D_CLIP_X2), y1 = DRVS(D_CLIP_Y1), y2 = DRVS(D_CLIP_Y2);
    int X = G_poly_x, Y = G_poly_y, X2 = G_poly_x2, Y2 = G_poly_y2;
    u16 o = 0;
    u16 p = (u16)(n - 1);
    int cl = 0, ch;
    s16 v = (s16)PX(X, p);
    if (v < x1) cl |= 1;
    if (v > x2) cl |= 2;
#define EMIT(a, b, xa, ya) do { PX(xa, o) = (u16)(a); PX(ya, o) = (u16)(b); o++; } while (0)
#define IX(edge, F, T) (s16)((s16)PX(Y, T) + clip_div((s16)((s16)PX(Y, F) - (s16)PX(Y, T)), (s16)((edge) - (s16)PX(X, T)), (s16)((s16)PX(X, F) - (s16)PX(X, T))))
    for (u16 c = 0; c < n; c++) {
        ch = 0;
        v = (s16)PX(X, c);
        if (v < x1) ch |= 1;
        if (v > x2) ch |= 2;
        if ((cl | ch) == 0) EMIT(PX(X, c), PX(Y, c), X2, Y2);
        else if (cl & ch) { }
        else if (cl == 0) {                                      /* P in, C out: I from C */
            if (ch & 1) EMIT(x1, IX(x1, p, c), X2, Y2);
            else if (ch & 2) EMIT(x2, IX(x2, p, c), X2, Y2);
        } else if (ch == 0) {                                    /* P out, C in: I from P, then C */
            if (cl & 1) EMIT(x1, IX(x1, c, p), X2, Y2);
            else if (cl & 2) EMIT(x2, IX(x2, c, p), X2, Y2);
            EMIT(PX(X, c), PX(Y, c), X2, Y2);
        } else {                                                 /* both out, different sides */
            if (cl & 1) EMIT(x1, IX(x1, c, p), X2, Y2);
            else if (cl & 2) EMIT(x2, IX(x2, c, p), X2, Y2);
            if (ch & 1) EMIT(x1, IX(x1, p, c), X2, Y2);
            else if (ch & 2) EMIT(x2, IX(x2, p, c), X2, Y2);
        }
        p = c;
        cl = ch;
    }
#undef IX
    n = o;
    DSW(lds(G_poly_n)) = n;
    if ((s16)n <= 1) {                                           /* copy back (2f98) */
        for (u16 i = 0; i < n; i++) { PX(X, i) = PX(X2, i); PX(Y, i) = PX(Y2, i); }
        return;
    }
#define IY(edge, F, T) (s16)((s16)PX(X2, T) + clip_div((s16)((s16)PX(X2, F) - (s16)PX(X2, T)), (s16)((edge) - (s16)PX(Y2, T)), (s16)((s16)PX(Y2, F) - (s16)PX(Y2, T))))
    o = 0;
    p = (u16)(n - 1);
    cl = 0;
    v = (s16)PX(Y2, p);
    if (v > y2) cl |= 4;
    if (v < y1) cl |= 8;
    for (u16 c = 0; c < n; c++) {
        ch = 0;
        v = (s16)PX(Y2, c);
        if (v > y2) ch |= 4;
        if (v < y1) ch |= 8;
        if ((cl | ch) == 0) EMIT(PX(X2, c), PX(Y2, c), X, Y);
        else if (cl & ch) { }
        else if (cl == 0) {
            if (ch & 8) EMIT(IY(y1, p, c), y1, X, Y);
            else if (ch & 4) EMIT(IY(y2, p, c), y2, X, Y);
        } else if (ch == 0) {
            if (cl & 8) EMIT(IY(y1, c, p), y1, X, Y);
            else if (cl & 4) EMIT(IY(y2, c, p), y2, X, Y);
            EMIT(PX(X2, c), PX(Y2, c), X, Y);
        } else {
            if (cl & 8) EMIT(IY(y1, c, p), y1, X, Y);
            else if (cl & 4) EMIT(IY(y2, c, p), y2, X, Y);
            if (ch & 8) EMIT(IY(y1, p, c), y1, X, Y);
            else if (ch & 4) EMIT(IY(y2, p, c), y2, X, Y);
        }
        p = c;
        cl = ch;
    }
#undef IY
#undef EMIT
    DSW(lds(G_poly_n)) = o;
}

/* INTRO 05da:0b91 / PS 16e3:11a5 / AP 1d32:09ce — video_library.md §4. Returns span_seg if a fill was
 * made, else 0. */
s16 gfx_polygon(s16 n, u16 xs_ds, u16 ys_ds)
{
    int X = G_poly_x, Y = G_poly_y, X2 = G_poly_x2, Y2 = G_poly_y2, OX = G_poly_ox, OY = G_poly_oy;
    u16 pn = lds(G_poly_n);
    PS_(SC_RET) = 0;
    PSB(SC_AMBIG) = 0;
    s16 ax = n;
    if (ax >= 0) {
        DSW(pn) = (u16)ax;
        for (s16 i = 0; i < ax; i++) { PX(X, i) = DSW(xs_ds + 2 * i); PX(Y, i) = DSW(ys_ds + 2 * i); }
    }
    if (ax == 2) { poly_outline_lines(lds(X), lds(Y), 1); goto done; }
    if (ax < 2) goto done;
    if (DRVB(D_FILL_ON) == 0) {
        u16 k = DSW(pn);
        PX(X, k) = PX(X, 0);
        PX(Y, k) = PX(Y, 0);
        poly_outline_lines(lds(X), lds(Y), (s16)k);
        goto done;
    }
    if (DRVB(D_DRAW_COL) != DRVB(D_FILL_COL)) {
        u16 k = DSW(pn);
        PS_(SC_ONUM) = k;
        for (u16 i = 0; i < k; i++) { PX(OX, i) = PX(X, i); PX(OY, i) = PX(Y, i); }
        PX(OX, k) = PX(X, 0);
        PX(OY, k) = PX(Y, 0);
    }
    if (DRVB(D_CLIP_ON)) gfx_poly_clip();
    if ((s16)DSW(pn) == 2) { poly_outline_lines(lds(X), lds(Y), 1); goto done; }
    if ((s16)DSW(pn) < 2) goto done;

    /* vertex pass (0c3d): drop repeats, reverse order, track top/bottom */
    s16 miny = 0x7FFF, minx = (s16)0x8001, maxy = (s16)0x8001, maxx = 0x7FFF;
    u16 m2 = 0;                                                   /* di: kept count * 2 */
    PS_(SC_LASTY) = PX(Y, 0);
    PS_(SC_LASTX) = PX(X, 0);
    PS_(SC_TOP) = 0;
    PS_(SC_BOT) = 0;
    for (s16 i = (s16)(DSW(pn) - 1); i >= 0; i--) {
        s16 y = (s16)PX(Y, i), x = (s16)PX(X, i);
        if ((u16)y == PS_(SC_LASTY) && (u16)x == PS_(SC_LASTX)) continue;
        PS_(SC_LASTY) = (u16)y;
        PX(Y2, m2 / 2) = (u16)y;
        if (y < miny || (y == miny && x > minx)) { PS_(SC_TOP) = m2; miny = y; minx = x; }
        if (y > maxy || (y == maxy && x <= maxx)) { PS_(SC_BOT) = m2; maxy = y; maxx = x; }
        PS_(SC_LASTX) = (u16)x;
        PX(X2, m2 / 2) = (u16)x;
        m2 += 2;
    }
    if (miny == maxy) goto flat;
    {
        u16 m = m2 >> 1;
        if (m == 2) goto flat;
        if (m < 2) goto done;
        u16 cxb = m2;                                            /* cx = m * 2 (bytes) */
        DSW(pn) = m;
        /* orientation (0d08) */
        u16 t = PS_(SC_TOP);
        u16 nx = (u16)(t + 2); if (!(nx < cxb)) nx = 0;
        s16 dxn = (s16)(PX(X2, nx / 2) - PX(X2, t / 2));
        s16 dyn = (s16)(PX(Y2, nx / 2) - PX(Y2, t / 2));
        if (dyn == 0) { dyn = 1; dxn = dxn < 0 ? (s16)-0x7FFF : 0x7FFF; }
        s16 pv = (s16)(t - 2); if (pv < 0) pv = (s16)(pv + cxb);
        s16 dxp = (s16)(PX(X2, pv / 2) - PX(X2, t / 2));
        s16 dyp = (s16)(PX(Y2, pv / 2) - PX(Y2, t / 2));
        int dir;                                                  /* 1 forward, -1 reverse, 0 ambiguous */
        if (dyp == 0) { dyp = 1; dxp = dxp < 0 ? (s16)0x8001 : 0x7FFF; }
        if (dxp < 0) {
            if (dxn >= 0) { dir = 1; goto oriented; }
            s16 a = (s16)-dxn, b = (s16)-dxp;                    /* both negative: swap roles */
            dxn = b; dxp = a;
            s16 tt = dyn; dyn = dyp; dyp = tt;
        } else if (dxn <= 0) { dir = -1; goto oriented; }
        {
            u16 qp = (u16)((u16)dxp / (u16)dyp), rp = (u16)((u16)dxp % (u16)dyp);
            u16 qn = (u16)((u16)dxn / (u16)dyn), rn = (u16)((u16)dxn % (u16)dyn);
            if (qn > qp) dir = 1;
            else if (qn < qp) dir = -1;
            else {
                u16 fn = (u16)(((u32)rn << 16) / (u16)dyn), fp = (u16)(((u32)rp << 16) / (u16)dyp);
                dir = fn < fp ? -1 : fn > fp ? 1 : 0;
            }
        }
oriented:
        if (dir == 0) {                                          /* ambiguous (0d9e) */
            PSB(SC_AMBIG) = 1;
            PS_(SC_SAVED) = cxb;
            for (u16 i = 0; i < cxb / 2; i++) { PX(OX, i) = PX(X2, i); PX(OY, i) = PX(Y2, i); }
            dir = 1;
        }
again:
        if (dir > 0) {                                           /* forward (0dc1) */
            for (u16 i = 0; i < cxb / 2; i++) { PX(X, i) = PX(X2, i); PX(Y, i) = PX(Y2, i); }
        } else {                                                 /* reverse (0ddd) */
            u16 k = cxb / 2;
            for (u16 i = 0; i < k; i++) { PX(X, k - 1 - i) = PX(X2, i); PX(Y, k - 1 - i) = PX(Y2, i); }
            u16 last = (u16)(cxb - 2);
            PS_(SC_TOP) = (u16)(last - PS_(SC_TOP));
            PS_(SC_BOT) = (u16)(last - PS_(SC_BOT));
        }
        /* chains (0e1a) */
        {
            s16 ybot = (s16)PX(Y, PS_(SC_BOT) / 2);
            u16 si = PS_(SC_TOP), di = 0;
            for (;;) {
                PX(X2, di / 2) = PX(X, si / 2);
                s16 y = (s16)PX(Y, si / 2);
                PX(Y2, di / 2) = (u16)y;
                di += 2;
                if (!(y < ybot)) break;
                si += 2; if (!(si < cxb)) si = 0;
            }
            PS_(SC_C1) = di >> 1;
            s16 ytop = (s16)PX(Y, PS_(SC_TOP) / 2);
            si = PS_(SC_BOT);
            for (;;) {
                PX(X2, di / 2) = PX(X, si / 2);
                s16 y = (s16)PX(Y, si / 2);
                PX(Y2, di / 2) = (u16)y;
                di += 2;
                if (!(y > ytop)) break;
                si += 2; if (!(si < cxb)) si = 0;
            }
            PS_(SC_C2) = (u16)((di >> 1) - PS_(SC_C1));
        }
        /* edge walk (0e8e): chain 1 -> right slot (+2), chain 2 -> left slot (+0) */
        PS_(SC_SLOT) = 2;
        PS_(SC_EOFS) = 0;
        u16 cnt = PS_(SC_C1);
        for (;;) {
            cnt--;
            if (cnt == 0) {
                if (PS_(SC_SLOT) == 0) break;
                PS_(SC_EOFS) = (u16)(PS_(SC_EOFS) + 2);
                PS_(SC_SLOT) = 0;
                cnt = PS_(SC_C2);
                continue;
            }
            PS_(SC_EDGES) = cnt;
            u16 e = PS_(SC_EOFS);
            PS_(SC_EOFS) = (u16)(e + 2);
            s16 ex0 = (s16)PX(X2, e / 2), ex1 = (s16)PX(X2, e / 2 + 1);
            s16 ey0 = (s16)PX(Y2, e / 2), ey1 = (s16)PX(Y2, e / 2 + 1);
            s16 adx = (s16)(ex0 - ex1); if (adx < 0) adx = (s16)-adx;
            if (adx == 0) { poly_edge_vertical(ex0, ex1, ey0, ey1); cnt = PS_(SC_EDGES); continue; }
            s16 ady = (s16)(ey0 - ey1); if (ady < 0) ady = (s16)-ady;
            if (ady == 0) {                                      /* horizontal */
                s16 a = ex0, b = ex1;
                if (!(a < b)) { s16 tt = a; a = b; b = tt; }
                u16 slot = PS_(SC_SLOT);
                wr16(DRVW(D_SPAN_SEG), (u16)((u16)(ey0 << 2) + slot), (u16)(slot == 0 ? a : b));
            } else if (adx < ady) poly_edge_steep(ex0, ex1, ey0, ey1);
            else if (adx > ady) {
                if (PS_(SC_SLOT) == 0) poly_edge_left(ex0, ex1, ey0, ey1);
                else poly_edge_right(ex0, ex1, ey0, ey1);
            } else poly_edge_diag(ex0, ex1, ey0, ey1);
            cnt = PS_(SC_EDGES);
        }
        /* fill (0f39) */
        {
            s16 ytop = (s16)PX(Y, PS_(SC_TOP) / 2);
            u16 seg = DRVW(D_SPAN_SEG);
            PS_(SC_RET) = seg;
            u16 cntr = (u16)((s16)PX(Y, PS_(SC_BOT) / 2) - ytop + 1);
            u16 hs = (u16)(seg - 1), ho = (u16)((u16)(ytop << 2) + 0x0C);
            wr16(hs, ho, (u16)ytop);
            wr16(hs, (u16)(ho + 2), cntr);
            gfx_poly_spans(far_make(hs, ho));
        }
        if (DRVB(D_DRAW_COL) != DRVB(D_FILL_COL))
            poly_outline_lines(lds(OX), lds(OY), (s16)PS_(SC_ONUM));
        if (PSB(SC_AMBIG)) {                                     /* second pass reversed (0f84) */
            PSB(SC_AMBIG) = 0;
            cxb = PS_(SC_SAVED);
            for (u16 i = 0; i < cxb / 2; i++) { PX(X2, i) = PX(OX, i); PX(Y2, i) = PX(OY, i); }
            dir = -1;
            goto again;
        }
        goto done;
    }
flat:
    if (DRVB(D_YSHIFT) == 0) {
        gfx_line(maxx, maxy, minx, miny);
    } else {
        DRVW(D_CLIP_Y1) >>= 1;
        DRVW(D_CLIP_Y2) >>= 1;
        gfx_line(maxx, (s16)(maxy >> 1), minx, (s16)(miny >> 1));
        DRVW(D_CLIP_Y1) <<= 1;
        DRVW(D_CLIP_Y2) <<= 1;
    }
done:
    return (s16)PS_(SC_RET);
}

/* --------------------------------------------------------------------------------- circles */

/* circle_state (INTRO 264C / PS 6930 / AP 70EA): +0 xc, +2 top row, +4 yc - top, +6 far write pointer,
 * +0A y1 - top, +0C plotter (near code offset), +0E y2 - top */
#define CS_(o) DSW((u16)(lds(G_circle_state) + (o)))
static u16 plot_span_ofs(void) { return BY_PROG(0x05A1, 0x04F3, 0x6D55); }
static u16 plot_outline_ofs(void) { return BY_PROG(0x0638, 0x058A, 0x6DFC); }

static void circle_plot(s16 a, s16 b)
{
    if (CS_(0x0C) == plot_span_ofs()) circle_span_plot(a, b);
    else circle_outline_plot(a, b);
}

/* INTRO 05da:05a1 / PS 16e3:04f3 / AP 1d32:6d55 (near) — video_library.md §5, §9.6 */
void circle_span_plot(s16 a, s16 b)
{
    s16 xl = (s16)(CS_(0) - a - (a >> 3));
    s16 xr = (s16)(CS_(0) + a + (a >> 3));
    s16 row = (s16)(b + CS_(4));
    if (DRVB(D_CLIP_ON)) {
        if (row < (s16)CS_(0x0A) || row > (s16)CS_(0x0E)) return;
        if (xl < DRVS(D_CLIP_X1)) xl = DRVS(D_CLIP_X1);
        if (xr > DRVS(D_CLIP_X2)) xr = DRVS(D_CLIP_X2);
    }
    u16 seg = CS_(8), o = (u16)(CS_(6) + (u16)(row << 2));
    wr16(seg, o, (u16)xl);
    wr16(seg, (u16)(o + 2), (u16)xr);
}

/* INTRO 05da:0638 / PS 16e3:058a / AP 1d32:6dfc (near) */
void circle_outline_plot(s16 a, s16 b)
{
    s16 xl = (s16)(CS_(0) - a - (a >> 3));
    s16 xr = (s16)(CS_(0) + a + (a >> 3));
    s16 row = (s16)(b + CS_(4));
    DrvPutPixelFn put = (DrvPutPixelFn)drv_entry(22);
    u16 c = (u16)(s16)DRVC(D_DRAW_COL);
    if (DRVB(D_CLIP_ON)) {
        if (row < (s16)CS_(0x0A) || row > (s16)CS_(0x0E)) return;
        s16 y = (s16)(row + CS_(2));
        if (xl >= DRVS(D_CLIP_X1) && xl <= DRVS(D_CLIP_X2)) put(xl, y, c);
        if (xr >= DRVS(D_CLIP_X1) && xr <= DRVS(D_CLIP_X2)) put(xr, y, c);
    } else {
        s16 y = (s16)(row + CS_(2));
        put(xl, y, c);
        put(xr, y, c);
    }
}

/* INTRO 05da:046e / PS 16e3:03c0 / AP 1d32:6c22 (near) — returns the far span list or 0 */
static FarPtr circle_walk(s16 r, s16 xc, s16 yc)
{
    u16 seg = DRVW(D_SPAN_SEG);
    CS_(8) = seg;
    CS_(6) = 0;
    s16 x = 0, y = r, d = (s16)(3 - 2 * r);
    s16 top = (s16)(yc - r) < DRVS(D_CLIP_Y1) ? DRVS(D_CLIP_Y1) : (s16)(yc - r);
    CS_(2) = (u16)top;
    wr16(seg, CS_(6), (u16)top); CS_(6) = (u16)(CS_(6) + 2);
    s16 cnt = (s16)(yc - top + r + 1);
    s16 lim = (s16)(DRVS(D_CLIP_Y2) - top + 1);
    if (lim <= cnt) cnt = lim;
    wr16(seg, CS_(6), (u16)cnt); CS_(6) = (u16)(CS_(6) + 2);
    if (top > DRVS(D_CLIP_Y2)) return far_make(0, 0);
    if ((s16)(top + cnt) < DRVS(D_CLIP_Y1)) return far_make(0, 0);
    CS_(0x0A) = (u16)(DRVS(D_CLIP_Y1) - top);
    CS_(0x0E) = (u16)(DRVS(D_CLIP_Y2) - top);
    CS_(0) = (u16)xc;
    CS_(4) = (u16)(yc - top);
    while (x <= y) {
        circle_plot(x, (s16)-y);
        circle_plot(x, y);
        circle_plot(y, (s16)-x);
        circle_plot(y, x);
        if (d < 0) d = (s16)(d + 4 * x + 6);
        else { d = (s16)(d + 4 * (x - y) + 10); y--; }
        x++;
    }
    return far_make(seg, 0);
}

/* INTRO 05da:0408 / PS 16e3:035a / AP 1d32:6bbc — video_library.md §5 */
void gfx_circle(s16 r, s16 xc, s16 yc)
{
    if (DRVB(D_FILL_ON)) {
        CS_(0x0C) = plot_span_ofs();
        FarPtr l = circle_walk(r, xc, yc);
        if (!far_is_null(l)) gfx_poly_spans(l);
    }
    if (DRVB(D_DRAW_COL) != DRVB(D_FILL_COL) || DRVB(D_FILL_ON) == 0) {
        CS_(0x0C) = plot_outline_ofs();
        circle_walk(r, xc, yc);
    }
}

/* ------------------------------------------------------------------------------ flood fill */

/* flood stack pointers (INTRO 2EB8..2EBE): +0 base far, +4 top far */
#define FS_(o) DSW((u16)(lds(G_flood_stack) + (o)))

/* INTRO 05da:2aba / PS 16e3:30ce / AP 1d32:4f08 (near) */
static void flood_push(s16 x, s16 y)
{
    wr16(FS_(6), FS_(4), (u16)x); FS_(4) = (u16)(FS_(4) + 2);
    wr16(FS_(6), FS_(4), (u16)y); FS_(4) = (u16)(FS_(4) + 2);
}

/* INTRO 05da:2add / PS 16e3:30f1 / AP 1d32:4f2b (near) */
static void flood_pop(s16 *x, s16 *y)
{
    FS_(4) = (u16)(FS_(4) - 2); *y = (s16)rd16(FS_(6), FS_(4));
    FS_(4) = (u16)(FS_(4) - 2); *x = (s16)rd16(FS_(6), FS_(4));
}

/* INTRO 05da:2893 / PS 16e3:2ea7 / AP 1d32:4ce1 — video_library.md §6. Quirk kept: the early returns
 * (pixel already c, no stack memory) leave the read page = write page. */
void gfx_flood(s16 x, s16 y, s16 c)
{
    u16 saved_read = DRVW(D_PAGE_READ);
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_WRITE);
    u8 old = (u8)gfx_get_pixel(x, y);
    if (old == (u8)c) return;
    FarPtr st = dos_alloc(0x1770, 0, 0);
    FS_(6) = st.seg; FS_(4) = st.off; FS_(2) = st.seg; FS_(0) = st.off;
    if (far_is_null(st)) return;
    DrvGetPixelFn get = (DrvGetPixelFn)drv_entry(21);
    flood_push(x, y);
    do {
        s16 D2 = 0, U2 = 0, D = 0, U = 0;
        flood_pop(&x, &y);
        s16 R = x, L = x;
        s16 si = x;
        while (get(si, y) == old && si >= DRVS(D_CLIP_X1)) {
            L = si;
            if (get(si, (s16)(y - 1)) == old) {
                if (U == 0 && y > DRVS(D_CLIP_Y1)) { U2 = U = 1; flood_push(si, (s16)(y - 1)); }
            } else U = 0;
            if (get(si, (s16)(y + 1)) == old) {
                if (D == 0 && y < DRVS(D_CLIP_Y2)) { D2 = D = 1; flood_push(si, (s16)(y + 1)); }
            } else D = 0;
            si--;
        }
        si = x;
        while (get(si, y) == old && si < DRVS(D_CLIP_X2)) {
            R = (s16)(si + 1);
            if (get(si, (s16)(y - 1)) == old) {
                if (U2 == 0 && y > DRVS(D_CLIP_Y1)) { U2 = 1; flood_push(si, (s16)(y - 1)); }
            } else U2 = 0;
            if (get(si, (s16)(y + 1)) == old) {
                if (D2 == 0 && y < DRVS(D_CLIP_Y2)) { D2 = 1; flood_push(si, (s16)(y + 1)); }
            } else D2 = 0;
            si++;
        }
        ((DrvHspanFn)drv_entry(10))(DRVW(D_PAGE_WRITE), drv_row(y), L, (s16)(R - L + 1), (u8)c, DRVB(D_FILL_COL2), (u16)y);
    } while (!(FS_(2) == FS_(6) && FS_(0) == FS_(4)));
    dos_free(far_make(FS_(2), FS_(0)));
    DRVW(D_PAGE_READ) = saved_read;
}

/* ------------------------------------------------------------------------- last bounding box */

/* INTRO 05da:45d3 / PS 16e3:48f7 / AP 1d32:6731 — video_library.md §9.2. Literal order of the code:
 * *a = cx[iR], *b = cy[iT], *c = cx[iL], *d = cy[iB] (iX = the byte indices DS:1846/1840/1844/1842). */
void gfx_last_bbox(u16 x1_ds, u16 y1_ds, u16 x2_ds, u16 y2_ds)
{
    u16 cx = lds(G_rot_cx), cy = lds(G_rot_cy);
    DSW(x1_ds) = DSW((u16)(cx + DSW(lds(G_rot_iR))));
    DSW(y1_ds) = DSW((u16)(cy + DSW(lds(G_rot_iT))));
    DSW(x2_ds) = DSW((u16)(cx + DSW(lds(G_rot_iL))));
    DSW(y2_ds) = DSW((u16)(cy + DSW(lds(G_rot_iB))));
}

/* ------------------------------------------------- AP clip-list driver hooks (§9.7), AP 1d32 */

/* The list entry of row r in list l, or false if the row is outside [y0, y0 + n). */
static bool clip_entry(FarPtr l, s16 r, s16 *xl, s16 *xr)
{
    s16 y0 = (s16)rd16(l.seg, l.off);
    if (r < y0 || r >= (s16)(y0 + (s16)rd16(l.seg, (u16)(l.off + 2)))) return false;
    u16 e = (u16)(l.off + 4 + (u16)((r - y0) << 2));
    *xl = (s16)rd16(l.seg, e);
    *xr = (s16)rd16(l.seg, (u16)(e + 2));
    return true;
}

/* 1d32:1c60 — replaces entry 37: BX = row, DX = x, CX = count; BP += 2d on a left clip */
static void clip_hook_cols(u16 es_page, u16 di_row, s16 dx_x, s16 cx_count, FarPtr ds_si_src, u16 bp_table_ds, u16 bx_row)
{
    s16 xl, xr;
    if (!clip_entry(ds_far(AP_DS_clip_lists), (s16)bx_row, &xl, &xr)) return;
    if (xl > dx_x) {
        s16 d = (s16)(xl - dx_x);
        cx_count = (s16)(cx_count - d);
        if (cx_count <= 0) return;
        dx_x = (s16)(dx_x + d);
        bp_table_ds = (u16)(bp_table_ds + 2 * d);
    }
    s16 t = (s16)(xr + 1 - dx_x - cx_count);
    if (t <= 0) { cx_count = (s16)(cx_count + t); if (cx_count <= 0) return; }
    ((DrvSpanColsFn)codeptr_lookup(ds_far(AP_DS_clip_saved_e37)))(es_page, di_row, dx_x, cx_count, ds_si_src, bp_table_ds,
                                                                   (u16)((bx_row - rd16(ds_far(AP_DS_clip_lists).seg, ds_far(AP_DS_clip_lists).off)) << 2));
}

/* 1d32:1cac — replaces entry 36: SI = 2 * row; draws the part inside every list of the DS:1D12 array */
static void clip_hook_tex(u16 base_pair_ds, u16 extra, u16 table_ds, s16 dx_x, s16 cx_count, u16 es_page, u16 di_row, u16 si_row2)
{
    DSW(AP_DS_clip_list_it) = AP_DS_clip_lists;
    for (;;) {
        u16 it = DSW(AP_DS_clip_list_it);
        DSW(AP_DS_clip_list_it) = (u16)(it + 4);
        FarPtr l = ds_far(it);
        if (far_is_null(l)) break;
        s16 x = dx_x, c = cx_count;
        u16 tab = table_ds;
        s16 xl, xr;
        u16 row = si_row2 >> 1;
        if (!clip_entry(l, (s16)row, &xl, &xr)) continue;
        if (xl > x) {
            s16 d = (s16)(xl - x);
            c = (s16)(c - d);
            if (c <= 0) continue;
            x = (s16)(x + d);
            tab = (u16)(tab + 2 * d);
        }
        s16 t = (s16)(xr + 1 - x - c);
        if (t <= 0) { c = (s16)(c + t); if (c <= 0) continue; }
        if (c <= 0) continue;
        ((DrvSpanTexFn)codeptr_lookup(ds_far(AP_DS_clip_saved_e36)))(base_pair_ds, extra, tab, x, c, es_page, di_row,
                                                                      (u16)(((s16)row - (s16)rd16(l.seg, l.off)) << 2));
    }
}

/* 1d32:1d3b — replaces entry 10: DX = row (clobbered to (row - y0) * 4 = the dither parity passed on) */
static void clip_hook_hspan(u16 es_page, u16 di_row, s16 bx_x, s16 cx_count, u8 al_a, u8 ah_b, u16 dx_parity)
{
    FarPtr l = ds_far(AP_DS_clip_lists);
    s16 xl, xr;
    if (!clip_entry(l, (s16)dx_parity, &xl, &xr)) return;
    u16 dx = (u16)(((s16)dx_parity - (s16)rd16(l.seg, l.off)) << 2);
    if (xl > bx_x) {
        s16 d = (s16)(xl - bx_x);
        cx_count = (s16)(cx_count - d);
        if (cx_count <= 0) return;
        bx_x = (s16)(bx_x + d);
    }
    s16 t = (s16)(xr + 1 - bx_x - cx_count);
    if (t <= 0) { cx_count = (s16)(cx_count + t); if (cx_count <= 0) return; }
    ((DrvHspanFn)codeptr_lookup(ds_far(AP_DS_clip_saved_e10)))(es_page, di_row, bx_x, cx_count, al_a, ah_b, dx);
}

/* 1d32:1d81 — replaces entry 38: DX = row, CF = direction */
static void clip_hook_row(u16 es_page, u16 di_row, s16 bx_x, s16 cx_count, FarPtr ds_si_src, bool cf, u16 dx_row)
{
    FarPtr l = ds_far(AP_DS_clip_lists);
    s16 xl, xr;
    if (!clip_entry(l, (s16)dx_row, &xl, &xr)) return;
    u16 dx = (u16)(((s16)dx_row - (s16)rd16(l.seg, l.off)) << 2);
    if (!cf) {
        if (xl > bx_x) {
            s16 d = (s16)(xl - bx_x);
            cx_count = (s16)(cx_count - d);
            if (cx_count <= 0) return;
            bx_x = (s16)(bx_x + d);
            ds_si_src.off = (u16)(ds_si_src.off + d);
        }
        s16 t = (s16)(xr + 1 - bx_x - cx_count);
        if (t <= 0) { cx_count = (s16)(cx_count + t); if (cx_count <= 0) return; }
    } else {
        if (xr < bx_x) {
            s16 a = (s16)(xr - bx_x);
            cx_count = (s16)(cx_count + a);
            if (cx_count <= 0) return;
            bx_x = (s16)(bx_x + a);
            ds_si_src.off = (u16)(ds_si_src.off - a);
        }
        s16 a = (s16)(xl - 1 - bx_x);
        if (a >= 0) return;
        a = (s16)-a;
        if (!(a > cx_count)) cx_count = a;
    }
    ((DrvRowCopyFn)codeptr_lookup(ds_far(AP_DS_clip_saved_e38)))(es_page, di_row, bx_x, cx_count, ds_si_src, cf, dx);
}

/* AP 1d32:1e17 */
void clip_hooks_install(FarPtr list)
{
    ds_far_wr(AP_DS_clip_lists, list);
    u16 tab = drv_tab();
    ds_far_wr(AP_DS_clip_saved_e37, ds_far((u16)(tab + 4 * 37)));
    ds_far_wr((u16)(tab + 4 * 37), codeptr_far(AP_FN_clip_hook_cols));
    ds_far_wr(AP_DS_clip_saved_e36, ds_far((u16)(tab + 4 * 36)));
    ds_far_wr((u16)(tab + 4 * 36), codeptr_far(AP_FN_clip_hook_tex));
    ds_far_wr(AP_DS_clip_saved_e38, ds_far((u16)(tab + 4 * 38)));
    ds_far_wr((u16)(tab + 4 * 38), codeptr_far(AP_FN_clip_hook_row));
    ds_far_wr(AP_DS_clip_saved_e10, ds_far((u16)(tab + 4 * 10)));
    ds_far_wr((u16)(tab + 4 * 10), codeptr_far(AP_FN_clip_hook_hspan));
}

/* AP 1d32:1e7a: install with the array's first pointer, then copy the 0:0-terminated array to DS:1D12 */
void clip_hooks_install_multi(FarPtr array)
{
    clip_hooks_install(array);
    u16 di = AP_DS_clip_lists;
    u32 si = far_lin(array);
    for (;;) {
        u16 lo = (u16)(mem[si] | mem[si + 1] << 8), hi = (u16)(mem[si + 2] | mem[si + 3] << 8);
        si += 4;
        DSW(di) = lo; DSW(di + 2) = hi; di = (u16)(di + 4);
        if ((lo | hi) == 0) break;
    }
}

/* AP 1d32:1e9d */
void clip_hooks_remove(void)
{
    u16 tab = drv_tab();
    ds_far_wr((u16)(tab + 4 * 37), ds_far(AP_DS_clip_saved_e37));
    ds_far_wr((u16)(tab + 4 * 36), ds_far(AP_DS_clip_saved_e36));
    ds_far_wr((u16)(tab + 4 * 10), ds_far(AP_DS_clip_saved_e10));
    ds_far_wr((u16)(tab + 4 * 38), ds_far(AP_DS_clip_saved_e38));
}

__attribute__((constructor)) static void gfx_prim_codeptrs(void)
{
    codeptr_register(PROG_AP, AP_FN_clip_hook_cols, (CodeFn)clip_hook_cols);
    codeptr_register(PROG_AP, AP_FN_clip_hook_tex, (CodeFn)clip_hook_tex);
    codeptr_register(PROG_AP, AP_FN_clip_hook_hspan, (CodeFn)clip_hook_hspan);
    codeptr_register(PROG_AP, AP_FN_clip_hook_row, (CodeFn)clip_hook_row);
}
