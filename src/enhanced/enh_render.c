/* Enhanced view: draws the captured 3D frame again at the output resolution with anti-aliasing, the
 * camera and the moving objects interpolated between the last two frames, and lays the scaled VGA
 * picture over it (ENHANCED.md "Stage 1"). */
#include "enhanced/enh_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

/* sample buffers, output size x aa in each direction: colour = pal[sbuf] mixed with pal[tbuf] by
 * abuf / 255 (Stage 2: distance haze toward the horizon colour, the sky gradient between two entries of
 * the sky ramp; abuf = 0 everywhere without haze) */
static u8 *sbuf, *tbuf, *abuf;
static int sbw, sbh;
static u8 fog_col;                     /* the horizon colour of the frame (haze target) */
static bool fog_on;
static double fog_k;                   /* 1 / haze distance (world units) */
static int rx0, rx1, ry0, ry1;         /* clip rectangle in samples (end exclusive) */
/* the rows each worker draws (host_parallel bands), and per sample row the columns worth drawing: those
 * under original pixels tagged "world" (plus the widescreen sides); lo >= hi = nothing on that row */
static _Thread_local int bj0, bj1;
static int *rlo, *rhi;
static int rcap;

/* the frame's primitives in screen space (samples), drawn in order by every band: n >= 3 = polygon,
 * n = 1 = a single sample (a polygon smaller than a sample, as the original always draws a pixel) */
typedef struct { u32 first; u16 n; u8 c; } Prim;
static Prim *prim; static u32 nprim, cprim;
static double *pu, *pv, *pa;
static u32 npv, cpv;

/* per-object transforms of the current displayed frame: eye = d Â· A + b */
typedef struct { double A[9], b[3]; } Xf;
static Xf *xf;
static u32 cxf;

/* composition state */
static u32 shown_seq;
static u64 shown_since;
static double last_x = -1;
static bool last_active;

static void ensure_sbuf(int w, int h)
{
    if (w == sbw && h == sbh && sbuf) return;
    free(sbuf); free(tbuf); free(abuf);
    sbuf = malloc((size_t)w * h);
    tbuf = malloc((size_t)w * h);
    abuf = malloc((size_t)w * h);
    if (!sbuf || !tbuf || !abuf) host_fatal("enhanced view: out of memory");
    sbw = w; sbh = h;
}

/* ------------------------------------------------------------------------------------------ rasteriser */

/* haze amount (0..255) at a distance in world units */
static double haze_at(double dist)
{
    if (!fog_on || !(dist > 0)) return 0;
    return 255.0 * (1.0 - exp(-dist * fog_k));
}

static void plot(double u, double v, u8 c, double a)
{
    int i = (int)floor(u), j = (int)floor(v);
    if (j >= bj0 && j < bj1 && i >= rlo[j] && i < rhi[j]) {
        size_t o = (size_t)j * sbw + i;
        sbuf[o] = c; tbuf[o] = fog_col; abuf[o] = (u8)(a + 0.5);
    }
}

/* Scanline fill (even-odd) of the rows of the band, sampled at the sample centres; fa = haze amount per
 * vertex (interpolated linearly on the screen). */
static void fill_poly(const double *u, const double *v, int n, u8 c, const double *fa)
{
    if (n < 3) return;
    double minv = v[0], maxv = v[0];
    bool flat = true;
    for (int k = 0; k < n; k++) {
        if (v[k] < minv) minv = v[k];
        if (v[k] > maxv) maxv = v[k];
        if (fa[k] != fa[0]) flat = false;
    }
    double j0d = ceil(minv - 0.5), j1d = floor(maxv - 0.5);
    if (j0d < bj0) j0d = bj0;
    if (j1d > bj1 - 1) j1d = bj1 - 1;
    double xs[128], as[128];
    u8 a0 = (u8)(fa[0] + 0.5);
    for (int j = (int)j0d; j <= (int)j1d && j0d <= j1d; j++) {
        int lo = rlo[j], hi = rhi[j];
        if (lo >= hi) continue;
        double yc = j + 0.5;
        int m = 0;
        for (int k = 0, l = n - 1; k < n && m < 128; l = k++) {
            double va = v[l], vb = v[k];
            if ((va <= yc && yc < vb) || (vb <= yc && yc < va)) {
                double t = (yc - va) / (vb - va);
                xs[m] = u[l] + t * (u[k] - u[l]);
                as[m++] = fa[l] + t * (fa[k] - fa[l]);
            }
        }
        for (int a = 1; a < m; a++) {                 /* insertion sort */
            double t = xs[a], ta = as[a];
            int b = a - 1;
            while (b >= 0 && xs[b] > t) { xs[b + 1] = xs[b]; as[b + 1] = as[b]; b--; }
            xs[b + 1] = t; as[b + 1] = ta;
        }
        size_t ro = (size_t)j * sbw;
        for (int a = 0; a + 1 < m; a += 2) {
            double i0d = ceil(xs[a] - 0.5), i1d = ceil(xs[a + 1] - 0.5);
            if (i0d < lo) i0d = lo;
            if (i1d > hi) i1d = hi;
            if (i0d >= i1d) continue;
            int i0 = (int)i0d, i1 = (int)i1d;
            memset(sbuf + ro + i0, c, (size_t)(i1 - i0));
            memset(tbuf + ro + i0, fog_col, (size_t)(i1 - i0));
            if (flat) memset(abuf + ro + i0, a0, (size_t)(i1 - i0));
            else {
                double w = xs[a + 1] - xs[a], da = w > 1e-9 ? (as[a + 1] - as[a]) / w : 0;
                double av = as[a] + (i0 + 0.5 - xs[a]) * da;
                u8 *ap = abuf + ro;
                for (int i = i0; i < i1; i++, av += da) {
                    double q = av < 0 ? 0 : av > 255 ? 255 : av;
                    ap[i] = (u8)(q + 0.5);
                }
            }
        }
    }
}

static void *grow(void *p, u32 *cap, u32 need, size_t elem)
{
    if (need <= *cap) return p;
    u32 c = *cap ? *cap : 1024;
    while (c < need) c *= 2;
    void *q = realloc(p, (size_t)c * elem);
    if (!q) host_fatal("enhanced view: out of memory");
    *cap = c;
    return q;
}

/* record a polygon (screen space); one that covers no sample centre becomes a single sample at its
 * centroid */
static void prim_fill(const double *u, const double *v, int n, u8 c, const double *fa)
{
    if (n < 3) return;
    double minu = u[0], maxu = u[0], minv = v[0], maxv = v[0], su = 0, sv = 0, sa = 0;
    for (int k = 0; k < n; k++) {
        if (u[k] < minu) minu = u[k];
        if (u[k] > maxu) maxu = u[k];
        if (v[k] < minv) minv = v[k];
        if (v[k] > maxv) maxv = v[k];
        su += u[k]; sv += v[k]; sa += fa[k];
    }
    if (maxv < ry0 || minv > ry1 || maxu < rx0 || minu > rx1) return;
    bool dot = ceil(minv - 0.5) > floor(maxv - 0.5) || ceil(minu - 0.5) > floor(maxu - 0.5);
    u32 m = dot ? 1 : (u32)n;
    if (npv + m > cpv) {
        u32 c2 = cpv, c3 = cpv;
        pu = grow(pu, &cpv, npv + m, sizeof *pu);
        pv = grow(pv, &c2, npv + m, sizeof *pv);
        pa = grow(pa, &c3, npv + m, sizeof *pa);
    }
    prim = grow(prim, &cprim, nprim + 1, sizeof *prim);
    Prim *P = &prim[nprim++];
    P->first = npv; P->n = (u16)m; P->c = c;
    if (dot) { pu[npv] = su / n; pv[npv] = sv / n; pa[npv] = sa / n; npv++; return; }
    memcpy(pu + npv, u, sizeof *u * n); memcpy(pv + npv, v, sizeof *v * n); memcpy(pa + npv, fa, sizeof *fa * n);
    npv += m;
}

static void stroke(double u0, double v0, double u1, double v1, double w, u8 c, double a)
{
    double dx = u1 - u0, dy = v1 - v0, len = sqrt(dx * dx + dy * dy), h = w * 0.5;
    double ex, ey;
    if (len < 1e-9) { ex = h; ey = 0; }
    else { ex = dx / len * h; ey = dy / len * h; }
    double nx = -ey, ny = ex;
    double U[4] = { u0 - ex + nx, u1 + ex + nx, u1 + ex - nx, u0 - ex - nx };
    double V[4] = { v0 - ey + ny, v1 + ey + ny, v1 + ey - ny, v0 - ey - ny };
    double A[4] = { a, a, a, a };
    prim_fill(U, V, 4, c, A);
}

/* ------------------------------------------------------------------------------------------ geometry */

static void lerp_mat(const double *a, const double *b, double x, double *out)
{
    for (int i = 0; i < 9; i++) out[i] = a[i] + (b[i] - a[i]) * x;
    enh_orthonormalize(out);
}

static void mat_mul(const double *a, const double *b, double *c)      /* c = a Â· b */
{
    double t[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) t[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
    memcpy(c, t, sizeof t);
}

static void vec_mat(const double *v, const double *m, double *out)  /* out = v Â· m */
{
    double t[3];
    for (int j = 0; j < 3; j++) t[j] = v[0] * m[j] + v[1] * m[3 + j] + v[2] * m[6 + j];
    memcpy(out, t, sizeof t);
}

static double dist3(const double *a, const double *b)
{
    double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

#define ENH_HAZE_DIST 250000.0         /* world units: haze 1 - 1/e at this distance x --view-dist */
#define ENH_JUMP 30000.0               /* world units: a camera or object moving more is not interpolated */

/* Two frames form a continuous step: same window and projection, the camera moved and turned little. */
static bool continuous(const ESnap *p, const ESnap *n)
{
    if (p->wx1 != n->wx1 || p->wx2 != n->wx2 || p->wy1 != n->wy1 || p->wy2 != n->wy2) return false;
    if (p->cx != n->cx || p->cy != n->cy || p->f != n->f) return false;
    if (dist3(p->cam, n->cam) > ENH_JUMP) return false;
    for (int j = 0; j < 3; j++) {                         /* each eye axis turned by less than ~45 degrees */
        double d = p->Mc[j] * n->Mc[j] + p->Mc[3 + j] * n->Mc[3 + j] + p->Mc[6 + j] * n->Mc[6 + j];
        if (d < 0.7) return false;
    }
    return true;
}

static const EObj *match(const ESnap *p, const EObj *o)
{
    for (u32 i = 0; i < p->nobj; i++)
        if (p->obj[i].key == o->key && p->obj[i].ord == o->ord) return &p->obj[i];
    return NULL;
}

/* Sutherland-Hodgman against the plane ey = near (eye space). */
static int near_clip(double (*in)[3], int n, double near_d, double (*out)[3])
{
    int m = 0;
    for (int k = 0, l = n - 1; k < n; l = k++) {
        bool ik = in[k][1] >= near_d, il = in[l][1] >= near_d;
        if (ik != il) {
            double t = (near_d - in[l][1]) / (in[k][1] - in[l][1]);
            for (int a = 0; a < 3; a++) out[m][a] = in[l][a] + (in[k][a] - in[l][a]) * t;
            m++;
        }
        if (ik) { memcpy(out[m], in[k], sizeof out[m]); m++; }
    }
    return m;
}

/* ------------------------------------------------------------------------------------------ horizon */

/* render3d_sky.md Â§2: sea below the horizon; above it `bands` bands of `spacing` pixels (perpendicular to
 * the horizon) in colours sky + iÂ·step + step/2, then sky + 15. Distances in original pixels. */
static u8 sky_colour(const ESnap *s, double d)
{
    if (d < 0) return s->sea;
    if (s->bands_on) {
        double k = floor(d / s->spacing);
        if (k < s->bands) return (u8)(s->sky + (int)k * s->step + (s->step >> 1));
    }
    return (u8)(s->sky + 0x0F);
}

static void sky_range(const ESnap *s, double d, double *lo, double *hi)
{
    if (d < 0) { *lo = -INFINITY; *hi = 0; return; }
    if (s->bands_on) {
        double k = floor(d / s->spacing);
        if (k < s->bands) { *lo = k * s->spacing; *hi = (k + 1) * s->spacing; return; }
        *lo = (double)s->bands * s->spacing;
    } else *lo = 0;
    *hi = INFINITY;
}

/* Stage 2 horizon: the sky as a gradient through the sky ramp (the bands' colours are the ramp at the
 * band centres: index sky + step·d/spacing, from the lowest band's colour up to sky + 15), the sea hazed
 * toward the horizon colour with the distance of the sea point (camera height alt, world units), so that
 * both meet in the horizon colour. */
#define SEA_STEP 8                     /* samples between two haze evaluations along a row */
static void draw_horizon_haze(const ESnap *s, const double *Mt, double ox, double SA, double alt)
{
    double ux = Mt[6], uy = Mt[7], uz = Mt[8];            /* world up in eye coordinates */
    double nrm = sqrt(ux * ux + uz * uz);
    double f = s->f, lo = s->bands_on ? s->step * 0.5 : 15.0;
    double kd = s->bands_on ? (double)s->step / s->spacing : 0;
    if (alt < 1) alt = 1;
    for (int j = bj0; j < bj1; j++) {
        if (rlo[j] >= rhi[j]) continue;
        size_t ro = (size_t)j * sbw;
        double Y = s->cy - (j + 0.5) / SA;
        double X0 = (0.5 / SA) - ox - s->cx, dX = 1.0 / SA;   /* X = X0 + i dX */
        double dz0 = ux * X0 + uy * f + uz * Y, ddz = ux * dX; /* world-up component of the eye ray */
        double sea_a = -1, sea_b = 0;                      /* haze at the last and next evaluation */
        int sea_i = -1;
        for (int i = rlo[j]; i < rhi[j]; i++) {
            double dz = dz0 + ddz * i;
            double d = nrm > 1e-9 ? dz / nrm : (dz >= 0 ? 1e30 : -1e30);
            size_t o = ro + (size_t)i;
            if (d >= 0) {
                double v = kd * d;
                if (v < lo) v = lo;
                if (v >= 15.0) { sbuf[o] = (u8)(s->sky + 15); abuf[o] = 0; continue; }
                int vi = (int)v;
                sbuf[o] = (u8)(s->sky + vi);
                tbuf[o] = (u8)(s->sky + vi + 1);
                abuf[o] = (u8)((v - vi) * 255.0 + 0.5);
                continue;
            }
            /* sea: haze evaluated every SEA_STEP samples, interpolated between */
            if (sea_i < 0 || i >= sea_i + SEA_STEP) {
                sea_i = i;
                for (int k = 0; k < 2; k++) {
                    int ii = i + k * SEA_STEP;
                    double X = X0 + dX * ii, dzz = dz0 + ddz * ii;
                    double a = 255.0;
                    if (dzz < 0) a = haze_at(alt * sqrt(X * X + f * f + Y * Y) / -dzz);
                    if (k == 0) sea_a = a; else sea_b = a;
                }
            }
            double a = sea_a + (sea_b - sea_a) * (i - sea_i) / SEA_STEP;
            sbuf[o] = s->sea;
            tbuf[o] = fog_col;
            abuf[o] = (u8)(a < 0 ? 0 : a > 255 ? 255 : a + 0.5);
        }
    }
}

static void draw_horizon(const ESnap *s, const double *Mt, double ox, double SA, double alt)
{
    if (!s->has_sky) {
        for (int j = bj0; j < bj1; j++) {
            if (rlo[j] >= rhi[j]) continue;
            memset(sbuf + (size_t)j * sbw + rlo[j], 0, (size_t)(rhi[j] - rlo[j]));
            memset(abuf + (size_t)j * sbw + rlo[j], 0, (size_t)(rhi[j] - rlo[j]));
        }
        return;
    }
    if (fog_on) { draw_horizon_haze(s, Mt, ox, SA, alt); return; }
    for (int j = bj0; j < bj1; j++)
        if (rlo[j] < rhi[j]) memset(abuf + (size_t)j * sbw + rlo[j], 0, (size_t)(rhi[j] - rlo[j]));
    double ux = Mt[6], uy = Mt[7], uz = Mt[8];            /* world up in eye coordinates */
    double nrm = sqrt(ux * ux + uz * uz);
    for (int j = bj0; j < bj1; j++) {
        int lo = rlo[j], hi = rhi[j];
        if (lo >= hi) continue;
        u8 *row = sbuf + (size_t)j * sbw;
        double Y = s->cy - (j + 0.5) / SA;
        double B = uy * s->f + uz * Y, A = 0;
        if (nrm > 1e-9) {
            B = (B + ux * ((0.5 / SA) - ox - s->cx)) / nrm;
            A = ux / (SA * nrm);
        } else B = B >= 0 ? 1e30 : -1e30;
        if (fabs(A) < 1e-12) { memset(row + lo, sky_colour(s, B), (size_t)(hi - lo)); continue; }
        int i = lo;
        while (i < hi) {
            double d = A * i + B, lo, hi;
            u8 c = sky_colour(s, d);
            sky_range(s, d, &lo, &hi);
            double e = A > 0 ? (hi - B) / A : (lo - B) / A;  /* where d leaves [lo, hi) */
            int next;
            if (!isfinite(e) || e >= hi) next = hi;
            else { next = (int)ceil(e); if (next <= i) next = i + 1; }
            if (next > hi) next = hi;
            memset(row + i, c, (size_t)(next - i));
            i = next;
        }
    }
}

/* ------------------------------------------------------------------------------------------ the frame */

typedef struct { const ESnap *s; double Mt[9], ox, SA, alt; } BandCtx;
#define BAND_ROWS 32

/* one band of sample rows: the horizon, then every primitive in order */
static void draw_band(int b, void *ctx)
{
    const BandCtx *c = ctx;
    bj0 = ry0 + b * BAND_ROWS;
    bj1 = bj0 + BAND_ROWS < ry1 ? bj0 + BAND_ROWS : ry1;
    draw_horizon(c->s, c->Mt, c->ox, c->SA, c->alt);
    for (u32 k = 0; k < nprim; k++) {
        const Prim *P = &prim[k];
        if (P->n == 1) plot(pu[P->first], pv[P->first], P->c, pa[P->first]);
        else fill_poly(pu + P->first, pv + P->first, P->n, P->c, pa + P->first);
    }
}

static void render(const ESnap *n, const ESnap *p, double x, int W, int H, double ox, const u8 *tag)
{
    int S = enh_scale, AA = enh_aa;
    double SA = (double)(S * AA);
    ensure_sbuf(W * AA, H * AA);
    rx0 = (int)((n->wx1 + ox) * SA); rx1 = (int)((n->wx2 + 1 + ox) * SA);
    ry0 = (int)(n->wy1 * SA);        ry1 = (int)((n->wy2 + 1) * SA);
    if (enh_wide && n->wx1 <= 0 && n->wx2 >= 319) { rx0 = 0; rx1 = sbw; }   /* the view reaches the sides */
    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;
    if (rx1 > sbw) rx1 = sbw;
    if (ry1 > sbh) ry1 = sbh;
    if (rx0 >= rx1 || ry0 >= ry1) return;

    /* the columns worth drawing on each sample row (under "world" pixels) */
    if (rcap < sbh) {
        free(rlo); free(rhi);
        rlo = malloc(sizeof(int) * (size_t)sbh);
        rhi = malloc(sizeof(int) * (size_t)sbh);
        if (!rlo || !rhi) host_fatal("enhanced view: out of memory");
        rcap = sbh;
    }
    bool sides = enh_wide && n->wx1 <= 0 && n->wx2 >= 319;
    int SAi = S * AA, pyl = -1, lo = 0, hi = 0;
    for (int j = 0; j < sbh; j++) {
        if (j < ry0 || j >= ry1) { rlo[j] = rhi[j] = 0; continue; }
        int py = j / SAi;
        if (py != pyl) {
            pyl = py;
            lo = hi = 0;
            if (py < 200) {
                const u8 *t = tag + py * 320;
                int a = 0, z = 319;
                while (a < 320 && t[a] != ENH_TAG_WORLD) a++;
                while (z >= a && t[z] != ENH_TAG_WORLD) z--;
                if (a <= z) { lo = (int)floor((a + ox) * SA); hi = (int)ceil((z + 1 + ox) * SA); }
            }
            if (sides) { lo = rx0; hi = rx1; }
            if (lo < rx0) lo = rx0;
            if (hi > rx1) hi = rx1;
        }
        rlo[j] = lo; rhi[j] = hi;
    }
    nprim = npv = 0;

    /* camera at the view time */
    double P[3], Mt[9];
    if (p) {
        for (int i = 0; i < 3; i++) P[i] = p->cam[i] + (n->cam[i] - p->cam[i]) * x;
        lerp_mat(p->Mc, n->Mc, x, Mt);
    } else {
        memcpy(P, n->cam, sizeof P);
        memcpy(Mt, n->Mc, sizeof Mt);
    }
    /* Stage 2 haze: toward the horizon colour (the lowest band's colour, the top of the ramp without bands) */
    fog_on = enh_haze && n->has_sky;
    fog_col = n->has_sky ? (u8)(n->bands_on ? n->sky + (n->step >> 1) : n->sky + 0x0F) : 0;
    fog_k = 1.0 / (ENH_HAZE_DIST * (double)enh_view_dist);
    BandCtx bc = { n, { 0 }, ox, SA, P[2] };
    memcpy(bc.Mt, Mt, sizeof bc.Mt);

    /* transforms: the static world (and everything not matched) as in frame n, seen from the camera */
    Xf base;
    memcpy(base.A, Mt, sizeof base.A);
    double off[3] = { n->cam[0] - P[0], n->cam[1] - P[1], n->cam[2] - P[2] };
    vec_mat(off, Mt, base.b);
    if (cxf < n->nobj) {
        free(xf);
        cxf = n->nobj + 64;
        xf = malloc(sizeof(Xf) * cxf);
        if (!xf) host_fatal("enhanced view: out of memory");
    }
    for (u32 k = 0; k < n->nobj; k++) {
        const EObj *o = &n->obj[k];
        xf[k] = base;
        if (o->tracer) {                                  /* back along its own straight step, turn kept */
            double w = p ? 1.0 - x : 0.0;
            double t[3] = { o->back[0] * w + off[0], o->back[1] * w + off[1], o->back[2] * w + off[2] };
            vec_mat(t, Mt, xf[k].b);
            continue;
        }
        const EObj *q = p && x < 1.0 ? match(p, o) : NULL;
        if (!q || dist3(o->pos, q->pos) > ENH_JUMP) continue;
        double Q[3];
        for (int i = 0; i < 3; i++) Q[i] = q->pos[i] + (o->pos[i] - q->pos[i]) * x;
        if (o->shadow) {                                  /* flattened: carried, not turned */
            double t[3] = { Q[0] - o->pos[0] + off[0], Q[1] - o->pos[1] + off[1], Q[2] - o->pos[2] + off[2] };
            vec_mat(t, Mt, xf[k].b);
            continue;
        }
        double tr = 0;
        for (int i = 0; i < 9; i++) tr += q->R[i] * o->R[i];    /* trace(Rq^T Rn) = 1 + 2 cos */
        double Rt[9];
        if (tr > 1.0) lerp_mat(q->R, o->R, x, Rt);
        else memcpy(Rt, o->R, sizeof Rt);
        double RnT[9], Aw[9];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) RnT[3 * i + j] = o->R[3 * j + i];
        mat_mul(RnT, Rt, Aw);                             /* local turn from frame n to the view time */
        double dobj[3] = { o->pos[0] - n->cam[0], o->pos[1] - n->cam[1], o->pos[2] - n->cam[2] }, da[3];
        vec_mat(dobj, Aw, da);
        double b[3] = { Q[0] - P[0] - da[0], Q[1] - P[1] - da[1], Q[2] - P[2] - da[2] };
        mat_mul(Aw, Mt, xf[k].A);
        vec_mat(b, Mt, xf[k].b);
    }

    double lw = SA * 0.5 < 1.0 ? 1.0 : SA * 0.5;          /* outline / line width in samples */
    double e[64][3], c[130][3], U[130], V[130], Fa[130];
    for (u32 k = 0; k < n->npoly; k++) {
        const EPoly *pl = &n->poly[k];
        const Xf *t = pl->obj >= 0 && (u32)pl->obj < n->nobj ? &xf[pl->obj] : &base;
        int m = pl->n > 64 ? 64 : pl->n;
        for (int i = 0; i < m; i++) {
            const EV3 *d = &n->vert[pl->first + i];
            double dv[3] = { d->x, d->y, d->z };
            vec_mat(dv, t->A, e[i]);
            for (int a = 0; a < 3; a++) e[i][a] += t->b[a];
        }
        /* bullet_draw: set_near(n - 4), one step nearer than draw_object */
        bool tracer = pl->obj >= 0 && (u32)pl->obj < n->nobj && n->obj[pl->obj].tracer;
        double near_d = tracer ? n->near_d * 0.5 : n->near_d;
        int q = m >= 2 ? near_clip(e, m, near_d, c) : 0;
        if (m == 1 && e[0][1] >= near_d) { memcpy(c[0], e[0], sizeof c[0]); q = 1; }
        if (q == 0) continue;
        double fs = 0;
        for (int i = 0; i < q; i++) {
            double iy = n->f / c[i][1];
            U[i] = (n->cx + c[i][0] * iy + ox) * SA;
            V[i] = (n->cy - c[i][2] * iy) * SA;
            Fa[i] = fog_on ? haze_at(sqrt(c[i][0] * c[i][0] + c[i][1] * c[i][1] + c[i][2] * c[i][2])) : 0;
            fs += Fa[i];
        }
        fs /= q;
        if (m == 1) continue;                             /* gfx_polygon draws nothing for one point */
        if (m == 2) { stroke(U[0], V[0], U[q - 1], V[q - 1], lw, pl->edge, fs); continue; }
        if (pl->fill_on) prim_fill(U, V, q, pl->fill, Fa);
        if (!pl->fill_on || pl->edge != pl->fill)
            for (int i = 0, l = q - 1; i < q; l = i++) stroke(U[l], V[l], U[i], V[i], lw, pl->edge, fs);
    }
    host_parallel((ry1 - ry0 + BAND_ROWS - 1) / BAND_ROWS, draw_band, &bc);
}

/* pal[c] mixed with pal[t] by a / 255 */
static inline u32 sample(size_t o, const u32 pal[256])
{
    u32 c = pal[sbuf[o]], a = abuf[o];
    if (!a) return c;
    u32 t = pal[tbuf[o]], ia = 255 - a;
    u32 r = ((c >> 16 & 0xFF) * ia + (t >> 16 & 0xFF) * a + 127) / 255;
    u32 g = ((c >> 8 & 0xFF) * ia + (t >> 8 & 0xFF) * a + 127) / 255;
    u32 b = ((c & 0xFF) * ia + (t & 0xFF) * a + 127) / 255;
    return r << 16 | g << 8 | b;
}

static inline u32 resolve(int X, int Y, const u32 pal[256])
{
    int AA = enh_aa;
    if (AA == 1) return sample((size_t)Y * sbw + X, pal);
    u32 r = 0, g = 0, b = 0;
    for (int j = 0; j < AA; j++) {
        size_t o = (size_t)(Y * AA + j) * sbw + (size_t)X * AA;
        for (int i = 0; i < AA; i++) {
            u32 c = sample(o + (size_t)i, pal);
            r += c >> 16 & 0xFF; g += c >> 8 & 0xFF; b += c & 0xFF;
        }
    }
    u32 n = (u32)(AA * AA);
    return (r / n) << 16 | (g / n) << 8 | (b / n);
}

typedef struct {
    const u32 *pic; const u8 *tag; const u32 *pal; u32 *out;
    int W, H, x0, sw; bool active; int rX0, rX1, rY0, rY1;
} ComposeCtx;
static void compose_band(int b, void *ctx);

bool enh_compose(const u32 *pic, const u8 *tag, const u32 pal[256], u16 shown, bool changed, u32 *out)
{
    u64 tc0 = host_ns();
    int W, H, x0, sw;
    enh_frame_size(&W, &H, &x0, &sw);
    int S = enh_scale, AA = enh_aa;
    double ox = (double)x0 / S;

    const ESnap *n = enh_snap_for_page(shown);
    bool active = false;
    if (n) for (int i = 0; i < 320 * 200; i++) if (tag[i] == ENH_TAG_WORLD) { active = true; break; }

    double x = 1.0;
    if (active) {
        u64 now = host_ns();
        if (n->seq != shown_seq) { shown_seq = n->seq; shown_since = now; }
        const ESnap *p = enh_snap_before(n);
        if (p && continuous(p, n)) {
            double P = (double)(n->t_ns - p->t_ns) / 1e9;
            if (P > 0.001 && P < 0.5) {
                x = (double)(now - shown_since) / 1e9 / P;
                if (x > 1.0) x = 1.0;
                if (x < 0.0) x = 0.0;
            }
        } else p = NULL;
        if (!changed && last_active && x == last_x) return false;
        u64 tr0 = host_ns();
        render(n, x < 1.0 ? p : NULL, x, W, H, ox, tag);
        enh_prof(PROF_RENDER, host_ns() - tr0);
    } else if (!changed && !last_active) return false;
    last_active = active;
    last_x = x;

    u64 tq0 = host_ns();
    ComposeCtx cc = { pic, tag, pal, out, W, H, x0, sw, active,
                      rx0 / AA, rx1 / AA, ry0 / AA, ry1 / AA };  /* rectangle in output pixels */
    host_parallel((H + BAND_ROWS - 1) / BAND_ROWS, compose_band, &cc);
    u64 tq1 = host_ns();
    enh_prof(PROF_RESOLVE, tq1 - tq0);
    enh_prof(PROF_COMPOSE, tq1 - tc0);
    return true;
}

static void compose_band(int b, void *ctx)
{
    const ComposeCtx *c = ctx;
    const u32 *pic = c->pic, *pal = c->pal;
    const u8 *tag = c->tag;
    u32 *out = c->out;
    int S = enh_scale, W = c->W, x0 = c->x0, sw = c->sw;
    int rX0 = c->rX0, rX1 = c->rX1, rY0 = c->rY0, rY1 = c->rY1;
    bool active = c->active;
    int Y1 = (b + 1) * BAND_ROWS < c->H ? (b + 1) * BAND_ROWS : c->H;
    for (int Y = b * BAND_ROWS; Y < Y1; Y++) {
        int py = Y / S;
        const u32 *prow = pic + py * 320;
        const u8 *trow = tag + py * 320;
        u32 *o = out + (size_t)Y * W;
        bool yin = active && Y >= rY0 && Y < rY1;
        for (int X = 0; X < W; X++) {
            bool in = yin && X >= rX0 && X < rX1;
            int sx = X - x0;
            if (sx >= 0 && sx < sw) {
                int px = sx / S;
                o[X] = in && trow[px] == ENH_TAG_WORLD ? resolve(X, Y, pal) : prow[px];
            } else o[X] = in ? resolve(X, Y, pal) : 0;
        }
    }
}
