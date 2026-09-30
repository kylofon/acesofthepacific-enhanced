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
/* ground dots: a sample under a dot shows pal[sbuf] mixed with pal[dbuf] (the dot's colour) by ibuf / 255
 * before the haze; ibuf = 0 = no dot (cleared by the horizon and by every primitive drawn over it) */
static u8 *dbuf, *ibuf;
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
/* per original pixel row, the runs of samples under "world" pixels (and the widescreen sides), in order:
 * the horizon and the far field fill only these (the other samples are never shown); at most MAXRUN,
 * or one run from the first to the last */
#define MAXRUN 24
static int runs[200][MAXRUN][2], nruns[200], run_sai = 1;
static inline int row_runs(int j, const int (**r)[2])
{
    int py = j / run_sai;
    if (py < 0 || py >= 200) return 0;
    *r = (const int (*)[2])runs[py];
    return nruns[py];
}

/* the frame's primitives in screen space (samples), drawn in order by every band: n >= 3 = polygon,
 * n = 1 = a single sample (a polygon smaller than a sample, as the original always draws a pixel) */
typedef struct { u32 first; u16 n; u8 c; } Prim;
static Prim *prim; static u32 nprim, cprim;
static double *pu, *pv, *pa;
static u32 npv, cpv;

/* ground dots of the frame: centres in samples, drawn after primitive dots_prim (the ground layer) as
 * squares of 2 dh samples */
static double *du, *dv, *dh;
static u8 *dI;                         /* each dot's intensity (ibuf) */
static u32 ndot, cdot, dots_prim;
/* the far field of the frame (draw_far_dots): camera, eye axes in world coordinates, handover, coverage */
static bool far_on;
static double far_P[3], far_ax[3][3], far_ox, far_SA, far_r0, far_rmax, far_p;
static u8 far_i;                       /* the far field's intensity (enh_dots_far_bright) */
/* the camera of the rendered frame, for the dots' debug overlay (compose_band) */
static struct { bool on; double P[3], ax[3][3], ox, cx, cy, f, r0, rmax; } dbg;
static bool dbg_last;

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
    free(sbuf); free(tbuf); free(abuf); free(dbuf); free(ibuf);
    sbuf = malloc((size_t)w * h);
    tbuf = malloc((size_t)w * h);
    abuf = malloc((size_t)w * h);
    dbuf = malloc((size_t)w * h);
    ibuf = calloc((size_t)w * h, 1);
    if (!sbuf || !tbuf || !abuf || !dbuf || !ibuf) host_fatal("enhanced view: out of memory");
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
        sbuf[o] = c; tbuf[o] = fog_col; abuf[o] = (u8)(a + 0.5); ibuf[o] = 0;
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
            memset(ibuf + ro + i0, 0, (size_t)(i1 - i0));
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

/* ------------------------------------------------------------------------------------------ ground dots */

#define DOT_GRID 1024.0                /* world units between two dots (ground_speed_dots) */

/* -1..1, the same for a grid point (x, y) at every frame */
static double dot_rand(s64 x, s64 y, u64 salt)
{
    u64 h = (u64)x * 0x9E3779B97F4A7C15ull ^ (u64)y * 0xC2B2AE3D27D4EB4Full ^ salt;
    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return (double)(h >> 11) / 4503599627370496.0 - 1.0;
}

/* one dot at d (world offset from the camera) seen through Mt */
/* Size: the full size at enh_dots_full units of eye distance; nearer, smaller down to enh_dots_near %;
 * farther, smaller again, geometrically, down to enh_dots_edge % at the range's edge.
 * Of the dots, keep * min(1, cmax / c) are kept, the coarser levels first (so the finest levels are not
 * needed at all where the coarser ones already give enough): c = the share of the samples around a dot
 * the dots would cover (dens dots per unit of ground, over the ground one sample sees there, times the
 * samples a dot covers), so where they would crowd together (far, low) they are thinned to cover
 * cmax = enh_dots_cover % of the ground. */
/* Intensity: with the distance (w = 0 near, 1 at the range's edge) the dots fade into the ground under
 * them, except the "crests", which fade only to the far field's intensity; a dot is a crest with the
 * chance far_p / (the share of the ground the dots cover there), so the crests, with the far field
 * fading in over the dots' fade out, cover far_p of the ground throughout, as the far field beyond. */
static void dot_add(const ESnap *s, const double *d, const double *Mt, double ox, double SA, double full_h,
                    double near, double dens, double dlo, double dl, double keep, double rnd, double w_i,
                    double rnd2)
{
    double e[3];
    vec_mat(d, Mt, e);
    if (e[1] < s->near_d) return;
    double iy = s->f / e[1];
    double u = (s->cx + e[0] * iy + ox) * SA, v = (s->cy - e[2] * iy) * SA;
    double D = sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    double full = enh_dots_full > 1 ? enh_dots_full : 1.0, h;
    if (D < full) h = full_h * (D / full < near ? near : D / full);
    else {
        double rm = enh_dots_range * DOT_GRID, Dm = sqrt(rm * rm + d[2] * d[2]);
        double q = Dm > full * 1.001 ? log(D / full) / log(Dm / full) : 1.0;
        h = full_h * pow(enh_dots_edge / 100.0, q > 1.0 ? 1.0 : q);
    }
    if (h < 0.5) h = 0.5;
    /* the ground one sample sees there: depth^3 / (A (f SA)^2); a dot counts as at least one output
     * pixel (AA samples across), since a pixel it touches shows it */
    double A = -d[2] < 1.0 ? 1.0 : -d[2], fs = s->f * SA, w = floor(2.0 * h + 0.5);
    if (w < enh_aa) w = enh_aa;
    double c = dens / (DOT_GRID * DOT_GRID) * e[1] * e[1] * e[1] / (A * fs * fs) * w * w;   /* dens: per cell */
    double cmax = enh_dots_cover / 100.0, cov = c > cmax ? cmax : c;
    if (c > cmax) keep *= cmax / c;
    /* the coarse levels first: of the density dens, keep * dens is kept, the levels in order (dlo = the
     * density of the coarser levels there, dl = this level's) */
    if (dlo + rnd * dl >= keep * dens) return;
    bool crest = cov > 0 && rnd2 * cov < far_p;
    double I = crest ? 1.0 - w_i * (1.0 - far_i / 255.0) : 1.0 - w_i;
    if (I <= 1.0 / 255.0) return;
    if (u + h < rx0 || u - h > rx1 || v + h < ry0 || v - h > ry1) return;
    if (ndot >= cdot) {
        u32 c2 = cdot, c3 = cdot, c4 = cdot;
        du = grow(du, &cdot, ndot + 1, sizeof *du);
        dv = grow(dv, &c2, ndot + 1, sizeof *dv);
        dh = grow(dh, &c3, ndot + 1, sizeof *dh);
        dI = grow(dI, &c4, ndot + 1, sizeof *dI);
    }
    du[ndot] = u; dv[ndot] = v; dh[ndot] = h; dI[ndot] = (u8)(I * 255.0 + 0.5); ndot++;
}

/* The ground dots seen from the camera at the view time (position P, world -> eye Mt): the grid points
 * on the ground (z = 0) within enh_dots_range cells of the camera, as long as it is no higher than
 * enh_dots_alt.
 *
 * Density: level 0 is the original's grid (DOT_GRID apart) everywhere. With enh_dots_far = F > 0, levels
 * 1..F add the points of grids DOT_GRID / 2^l apart that the coarser ones lack; a point of level l shows
 * from the horizontal distance rmax (l - 1 + t) / F on, t in 0..1 fixed per point, so the spacing halves
 * F times from the camera out to the range's edge, where the dots are densest, and grows smoothly (dots
 * appear one by one at fixed places, as the camera nears them they stay).
 *
 * Each dot is moved off its grid point by up to enh_dots_scatter % of half its level's cell along x and y
 * (the same at every frame), so they do not line up in rows. A dot has enh_dots_size from enh_dots_full
 * units away and shrinks nearer the eye, down to enh_dots_near % of it, so the dots in front do not turn
 * into large blocks.
 *
 * From enh_dots_fade % of the range on (r0) the dots fade out: a point there is kept with the chance
 * smoothstep((rmax - r) / (rmax - r0)), so none are left at the range's edge, while the far field
 * (draw_far_dots) fades in over the same stretch; beyond the edge it is alone. */
#define DOT_TINT0 0.25               /* the dots start fading into the ground from this share of the range */
static double smooth01(double x)
{
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    return x * x * (3.0 - 2.0 * x);
}

/* The part of the ground (z = 0) that can show on the screen: the square around the camera cut by the
 * half-planes of the view (near, the sides, the top and bottom of the drawn rows, with a margin), each
 * a x + b y + c >= 0 from a combination of the eye coordinates (linear in x, y on the ground). */
typedef struct { int n; double v[16][2]; } GPoly;

static void gpoly_cut(GPoly *g, double a, double b, double c)
{
    GPoly o = { 0 };
    for (int k = 0, l = g->n - 1; k < g->n; l = k++) {
        double fk = a * g->v[k][0] + b * g->v[k][1] + c, fl = a * g->v[l][0] + b * g->v[l][1] + c;
        if ((fk >= 0) != (fl >= 0) && o.n < 16) {
            double t = fl / (fl - fk);
            o.v[o.n][0] = g->v[l][0] + (g->v[k][0] - g->v[l][0]) * t;
            o.v[o.n][1] = g->v[l][1] + (g->v[k][1] - g->v[l][1]) * t;
            o.n++;
        }
        if (fk >= 0 && o.n < 16) { o.v[o.n][0] = g->v[k][0]; o.v[o.n][1] = g->v[k][1]; o.n++; }
    }
    *g = o;
}

/* the x range of the polygon within ya <= y <= yb; false = none */
static bool gpoly_strip(const GPoly *g, double ya, double yb, double *xa, double *xb)
{
    double lo = 1e300, hi = -1e300;
    for (int k = 0, l = g->n - 1; k < g->n; l = k++) {
        const double *p = g->v[k], *q = g->v[l];
        if (p[1] >= ya && p[1] <= yb) { if (p[0] < lo) lo = p[0]; if (p[0] > hi) hi = p[0]; }
        double ys[2] = { ya, yb };
        for (int e = 0; e < 2; e++) {
            double y = ys[e];
            if ((p[1] - y) * (q[1] - y) < 0) {
                double x = p[0] + (q[0] - p[0]) * (y - p[1]) / (q[1] - p[1]);
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
    }
    if (lo > hi) return false;
    *xa = lo; *xb = hi;
    return true;
}

static void project_dots(const ESnap *s, const double *P, const double *Mt, double ox, double SA)
{
    ndot = 0;
    far_on = false;
    if (!s->dots || P[2] > enh_dots_alt) return;
    double full_h = enh_dots_size / 100.0 * SA * 0.5, near = enh_dots_near / 100.0;
    double scatter = enh_dots_scatter / 100.0 * 0.5;
    double rmax = enh_dots_range * DOT_GRID, r0 = rmax * enh_dots_fade / 100.0;
    int F = enh_dots_far;

    /* the far field: the eye axes in world coordinates (the columns of Mt), where it starts, its coverage */
    far_p = enh_dots_speckle / 100.0;
    far_i = (u8)(enh_dots_far_bright * 255 / 100);
    far_on = far_p > 0 && far_i > 0;
    memcpy(far_P, P, sizeof far_P);
    for (int k = 0; k < 3; k++) { far_ax[0][k] = Mt[3 * k]; far_ax[1][k] = Mt[3 * k + 1]; far_ax[2][k] = Mt[3 * k + 2]; }
    far_ox = ox; far_SA = SA;
    far_r0 = r0; far_rmax = rmax;

    /* the drawn samples' rectangle (rows and columns under world pixels), with a dot's margin */
    int cx0 = sbw, cx1 = 0, cy0 = -1, cy1 = -1;
    for (int j = ry0; j < ry1; j++)
        if (rlo[j] < rhi[j]) {
            if (cy0 < 0) cy0 = j;
            cy1 = j + 1;
            if (rlo[j] < cx0) cx0 = rlo[j];
            if (rhi[j] > cx1) cx1 = rhi[j];
        }
    if (cy0 < 0) return;
    double A = P[2] < 1.0 ? 1.0 : P[2], M = full_h + 2.0, f = s->f;
    double XL = (cx0 - M) / SA - ox - s->cx, XR = (cx1 + M) / SA - ox - s->cx;
    double ZT = s->cy - (cy0 - M) / SA, ZB = s->cy - (cy1 + M) / SA;
    /* e_k = a_k x + b_k y + c_k on the ground */
    double ea[3], eb[3], ec[3];
    for (int k = 0; k < 3; k++) {
        ea[k] = Mt[k]; eb[k] = Mt[3 + k];
        ec[k] = -P[0] * Mt[k] - P[1] * Mt[3 + k] - A * Mt[6 + k];
    }
    double rout_all = rmax + scatter * DOT_GRID;
    GPoly g = { 4, { { P[0] - rout_all, P[1] - rout_all }, { P[0] + rout_all, P[1] - rout_all },
                     { P[0] + rout_all, P[1] + rout_all }, { P[0] - rout_all, P[1] + rout_all } } };
    /* sum of lam_k e_k + mu >= 0 */
    double lam[5][3] = { { 0, 1, 0 }, { f, -XL, 0 }, { -f, XR, 0 }, { 0, ZT, -f }, { 0, -ZB, f } };
    double mu[5] = { -s->near_d, 0, 0, 0, 0 };
    for (int h = 0; h < 5 && g.n; h++) {
        double a = 0, b = 0, c = mu[h];
        for (int k = 0; k < 3; k++) { a += lam[h][k] * ea[k]; b += lam[h][k] * eb[k]; c += lam[h][k] * ec[k]; }
        gpoly_cut(&g, a, b, c);
    }
    if (g.n < 3) return;

    double cmax = enh_dots_cover / 100.0, fs = f * SA, aa = enh_aa < 1 ? 1 : enh_aa;
    for (int l = 0; l <= F; l++) {
        double gs = DOT_GRID / (double)(1 << l), jit = scatter * gs;
        double rin = l ? rmax * (l - 1) / F - jit : 0;    /* no point of the level is nearer */
        double rout = rmax + jit;
        /* A level l >= 1 is needed only where the coarser levels (at least 4^(l-1) per cell there) do not
         * already give all the dots kept: nearer than the eye depth dmax (see dot_add). */
        double dmax = 1e300;
        if (l) dmax = cbrt(cmax * A * fs * fs * DOT_GRID * DOT_GRID / (aa * aa * (double)(1 << (2 * (l - 1)))))
                      + 1.5 * jit;
        u64 salt = 4ull * (u64)l;
        s64 y0 = (s64)floor((P[1] - rout) / gs), y1 = (s64)ceil((P[1] + rout) / gs);
        for (s64 gy = y0; gy <= y1; gy++) {
            double yw = gy * gs, dy = yw - P[1], w2 = rout * rout - dy * dy;
            if (w2 < 0) continue;
            double xa, xb;
            if (!gpoly_strip(&g, yw - jit - 1e-6, yw + jit + 1e-6, &xa, &xb)) continue;
            xa -= jit; xb += jit;
            /* depth along the row (un-moved points) below dmax */
            if (dmax < 1e299) {
                double e0 = eb[1] * yw + ec[1], k = ea[1];            /* depth = k x + e0 */
                if (fabs(k) < 1e-12) { if (e0 >= dmax) continue; }
                else if (k > 0) { double xm = (dmax - e0) / k; if (xm < xb) xb = xm; }
                else { double xm = (dmax - e0) / k; if (xm > xa) xa = xm; }
                if (xa > xb) continue;
            }
            double w = sqrt(w2), hw2 = rin > 0 ? rin * rin - dy * dy : -1;
            if (P[0] - w > xa) xa = P[0] - w;
            if (P[0] + w < xb) xb = P[0] + w;
            if (xa > xb) continue;
            s64 x0 = (s64)floor(xa / gs), x1 = (s64)ceil(xb / gs);
            s64 h0 = x1 + 1, h1 = x0 - 1;                  /* the part of the row inside rin */
            if (hw2 > 0) {
                double hw = sqrt(hw2);
                h0 = (s64)ceil((P[0] - hw) / gs);
                h1 = (s64)floor((P[0] + hw) / gs);
            }
            for (s64 gx = x0; gx <= x1; gx++) {
                if (gx >= h0 && gx <= h1) { gx = h1; continue; }
                if (l && !(gx & 1) && !(gy & 1)) continue;   /* a point of a coarser level */
                double d[3] = { gx * gs - P[0], dy, -P[2] };
                if (jit > 0) {
                    d[0] += jit * dot_rand(gx, gy, salt + 1);
                    d[1] += jit * dot_rand(gx, gy, salt + 2);
                }
                double r2 = d[0] * d[0] + d[1] * d[1];
                if (r2 > rmax * rmax) continue;
                double r = sqrt(r2);
                if (l) {
                    double t = (dot_rand(gx, gy, salt + 3) + 1.0) * 0.5;
                    if (r < rmax * (l - 1 + t) / F) continue;
                }
                double keep = r > r0 ? smooth01((rmax - r) / (rmax - r0)) : 1.0;
                /* the dots per cell at r: all levels, the coarser ones, this one */
                double dens = 1.0, dlo = 0, dl = 1.0;
                for (int m = 1; m <= F; m++) {
                    double a = F * r / rmax - (m - 1), q = 3.0 * (double)(1 << (2 * (m - 1))) * (a < 0 ? 0 : a > 1 ? 1 : a);
                    if (m < l) dlo += q;
                    if (m == l) dl = q;
                    dens += q;
                }
                if (l) dlo += 1.0;
                dot_add(s, d, Mt, ox, SA, full_h, near, dens, dlo, dl, keep,
                        (dot_rand(gx, gy, salt + 4) + 1.0) * 0.5, smooth01((r - rmax * DOT_TINT0) / (rmax * (1.0 - DOT_TINT0))),
                        (dot_rand(gx, gy, salt + 5) + 1.0) * 0.5);
            }
        }
    }
}

/* The far field, out to the horizon: a faint speckle. The ground seen by each sample (the ray from the
 * camera to z = 0) is split into cells fixed on the ground, about one sample big (per world axis a power
 * of two at least the sample's footprint along it, between two sizes chosen per cell, so the size
 * changes without a seam), and a cell is a dot with the chance far_p (enh_dots_speckle %), faded in from
 * r0 to the range's edge as the dots fade out. */
/* floor to an integer, without the library call */
static inline s64 ffloor(double x)
{
    s64 i = (s64)x;
    return i - (x < (double)i);
}

/* 2^k as a double (|k| < 1000) */
static inline double pow2(int k)
{
    u64 b = (u64)(1023 + k) << 52;
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

#define FAR_CELL 0.7                   /* the cells' size, times the sample's footprint */
/* a 32-bit number for a cell, the same at every frame */
static inline u32 cell_hash(s64 x, s64 y, u32 salt)
{
    u64 h = (u64)x * 0x9E3779B97F4A7C15ull + (u64)y * 0xC2B2AE3D27D4EB4Full + salt;
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32; h *= 0x94D049BB133111EBull;
    return (u32)(h >> 32);
}

/* the cell along one axis: size 2^k with k = floor(log2 ext), or k + 1 with the chance of the fraction
 * (chosen per cell of size 2^(k+1)), so the size changes without a seam */
static inline int far_level(double pos, double ext, double other, s64 *cell)
{
    int k = 0;
    u32 fr = 0;
    if (ext > 1.0) {                                      /* log2 ext ~ exponent + mantissa fraction */
        u64 b;
        memcpy(&b, &ext, sizeof b);
        k = (int)(b >> 52 & 0x7FF) - 1023;
        fr = (u32)((b & 0xFFFFFFFFFFFFFull) >> 20);
    }
    double ib = pow2(-(k + 1));
    if (cell_hash(ffloor(pos * ib), ffloor(other * ib), 0x5EEDu + (u32)k) < fr) k += 1;
    *cell = ffloor(pos * pow2(-k));
    return k;
}

/* the samples of a row where the ray meets the ground (bz + i az < 0) farther than R horizontally:
 * Q(i) = A^2 ((bx + i ax)^2 + (by + i ay)^2) - R^2 (bz + i az)^2 > 0; up to two ranges [lo, hi), with a
 * sample's margin (the samples are tested again one by one) */
static int far_ranges(const double *b, const double *a, double A, double R, int L, int H, int out[2][2])
{
    double lo1 = -1e18, hi1 = 1e18;                       /* bz + i az < 0 */
    if (fabs(a[2]) < 1e-15) { if (b[2] >= 0) return 0; }
    else if (a[2] > 0) hi1 = -b[2] / a[2];
    else lo1 = -b[2] / a[2];
    double A2 = A * A, R2 = R * R;
    double qa = A2 * (a[0] * a[0] + a[1] * a[1]) - R2 * a[2] * a[2];
    double qb = 2.0 * (A2 * (b[0] * a[0] + b[1] * a[1]) - R2 * b[2] * a[2]);
    double qc = A2 * (b[0] * b[0] + b[1] * b[1]) - R2 * b[2] * b[2];
    double iv[2][2];
    int n = 0;
    double sc = fabs(qa) + fabs(qb) + fabs(qc);
    if (fabs(qa) <= 1e-12 * sc) {
        if (fabs(qb) <= 1e-12 * sc) { if (qc > 0) { iv[n][0] = -1e18; iv[n][1] = 1e18; n++; } }
        else if (qb > 0) { iv[n][0] = -qc / qb; iv[n][1] = 1e18; n++; }
        else { iv[n][0] = -1e18; iv[n][1] = -qc / qb; n++; }
    } else {
        double disc = qb * qb - 4.0 * qa * qc;
        if (disc < 0) { if (qa > 0) { iv[n][0] = -1e18; iv[n][1] = 1e18; n++; } }
        else {
            double sq = sqrt(disc), x1 = (-qb - sq) / (2.0 * qa), x2 = (-qb + sq) / (2.0 * qa);
            if (x1 > x2) { double t = x1; x1 = x2; x2 = t; }
            if (qa > 0) { iv[n][0] = -1e18; iv[n][1] = x1; n++; iv[n][0] = x2; iv[n][1] = 1e18; n++; }
            else { iv[n][0] = x1; iv[n][1] = x2; n++; }
        }
    }
    int m = 0;
    for (int k = 0; k < n; k++) {
        double x = iv[k][0] > lo1 ? iv[k][0] : lo1, y = iv[k][1] < hi1 ? iv[k][1] : hi1;
        if (x >= y) continue;
        double fx = floor(x) - 1, fy = ceil(y) + 1;
        int ia = fx < L ? L : fx > H ? H : (int)fx, ib = fy > H ? H : fy < L ? L : (int)fy;
        if (ia < ib) { out[m][0] = ia; out[m][1] = ib; m++; }
    }
    return m;
}

static void draw_far_dots(const ESnap *s)
{
    const double *P = far_P, *c0 = far_ax[0], *c1 = far_ax[1], *c2 = far_ax[2];
    double A = P[2] < 1.0 ? 1.0 : P[2], SA = far_SA, span = far_rmax - far_r0, r0sq = far_r0 * far_r0;
    double di[3], dj[3];
    u32 pthr = (u32)(far_p * 4294967295.0);
    for (int k = 0; k < 3; k++) { di[k] = c0[k] / SA; dj[k] = -c2[k] / SA; }
    for (int j = bj0; j < bj1; j++) {
        const int (*run)[2];
        int nr = row_runs(j, &run);
        if (!nr) continue;
        size_t ro = (size_t)j * sbw;
        u8 *row = sbuf + ro;
        double X0 = 0.5 / SA - far_ox - s->cx, Y = s->cy - (j + 0.5) / SA, b[3];
        for (int k = 0; k < 3; k++) b[k] = X0 * c0[k] + s->f * c1[k] + Y * c2[k];
        for (int q = 0; q < nr; q++) {
            int rg[2][2], ng = far_ranges(b, di, A, far_r0, run[q][0], run[q][1], rg);
            for (int g = 0; g < ng; g++)
                for (int i = rg[g][0]; i < rg[g][1]; i++) {
                    if (row[i] > 0x87 || abuf[ro + i] > 250) continue;   /* not ground, or lost in the haze */
                    double d[3] = { b[0] + i * di[0], b[1] + i * di[1], b[2] + i * di[2] };
                    if (d[2] > -1e-9) continue;                /* the ray does not reach the ground */
                    double inv = 1.0 / d[2], t = -A * inv, h2 = t * t * (d[0] * d[0] + d[1] * d[1]);
                    if (h2 <= r0sq) continue;
                    u32 thr = pthr;
                    if (h2 < far_rmax * far_rmax && span > 0)
                        thr = (u32)(pthr * (1.0 - smooth01((far_rmax - sqrt(h2)) / span)));
                    double wx = P[0] + t * d[0], wy = P[1] + t * d[1];
                    /* the ground under the sample: dW/di, dW/dj */
                    double ai = di[2] * inv, aj = dj[2] * inv;
                    double ix = t * (di[0] - d[0] * ai), iy = t * (di[1] - d[1] * ai);
                    double jx = t * (dj[0] - d[0] * aj), jy = t * (dj[1] - d[1] * aj);
                    s64 cx, cy;
                    int kx = far_level(wx, FAR_CELL * (fabs(ix) + fabs(jx)), wy, &cx);
                    int ky = far_level(wy, FAR_CELL * (fabs(iy) + fabs(jy)), wx, &cy);
                    if (cell_hash(cx, cy, 0xD075u + (u32)(kx * 64 + ky)) < thr) {
                        dbuf[ro + i] = s->dot_col[row[i]];
                        ibuf[ro + i] = far_i;
                    }
                }
        }
    }
}

/* the dots in the rows of the band: as the original, each sample under a dot of a colour up to 87h
 * takes the colour the frame's table gives it (the haze of the sample is kept); the far field first */
static void draw_dots(const ESnap *s)
{
    if (far_on) draw_far_dots(s);
    for (u32 k = 0; k < ndot; k++) {
        int w = (int)(2.0 * dh[k] + 0.5);
        if (w < 1) w = 1;
        int i0 = (int)floor(du[k] - dh[k] + 0.5), j0 = (int)floor(dv[k] - dh[k] + 0.5);
        int ja = j0 < bj0 ? bj0 : j0, jb = j0 + w > bj1 ? bj1 : j0 + w;
        for (int j = ja; j < jb; j++) {
            int ia = i0 < rlo[j] ? rlo[j] : i0, ib = i0 + w > rhi[j] ? rhi[j] : i0 + w;
            size_t ro = (size_t)j * sbw;
            const u8 *row = sbuf + ro;
            for (int i = ia; i < ib; i++)
                if (row[i] <= 0x87) {
                    dbuf[ro + i] = s->dot_col[row[i]];
                    if (ibuf[ro + i] < dI[k]) ibuf[ro + i] = dI[k];
                }
        }
    }
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

#define SEA_STEP 8                     /* samples between two haze evaluations along a row */
#define SEA_LONG 32                    /* ... where the haze changes by no more than SEA_FLAT over them */
#define SEA_FLAT 3.0

/* the haze of the sea seen through sample i of a row (eye ray X0 + i dX, f, Y; world-up part dz0 + i ddz) */
static inline double sea_haze(double X0, double dX, double r2, double dz0, double ddz, double alt, int i)
{
    double dz = dz0 + ddz * i, X = X0 + dX * i;
    return dz < 0 ? haze_at(alt * sqrt(X * X + r2) / -dz) : 255.0;
}

/* where along a row the ray turns from sea to sky (dz0 + i ddz = 0): [lo, hi) splits into the sea part
 * and the sky part, returned as the first sample of the second part and whether that part is the sky */
static int sky_split(double dz0, double ddz, int lo, int hi, bool *sky_second)
{
    if (fabs(ddz) < 1e-15) { *sky_second = dz0 >= 0; return lo; }
    double x = -dz0 / ddz;
    if (x < lo - 1) x = lo - 1;
    if (x > hi + 1) x = hi + 1;
    int k = ddz > 0 ? (int)ceil(x) : (int)floor(x) + 1;
    *sky_second = ddz > 0;
    return k < lo ? lo : k > hi ? hi : k;
}

/* Stage 2 horizon: the sky as a gradient through the sky ramp (the bands' colours are the ramp at the
 * band centres: index sky + step·d/spacing, from the lowest band's colour up to sky + 15), the sea hazed
 * toward the horizon colour with the distance of the sea point (camera height alt, world units), so that
 * both meet in the horizon colour. Only the runs of world samples; each run split once into sky and sea;
 * the sea's haze evaluated every SEA_STEP samples and interpolated between. */
static void draw_horizon_haze(const ESnap *s, const double *Mt, double ox, double SA, double alt)
{
    double ux = Mt[6], uy = Mt[7], uz = Mt[8];            /* world up in eye coordinates */
    double nrm = sqrt(ux * ux + uz * uz);
    double f = s->f, lo = s->bands_on ? s->step * 0.5 : 15.0;
    double kd = s->bands_on ? (double)s->step / s->spacing : 0;
    if (alt < 1) alt = 1;
    for (int j = bj0; j < bj1; j++) {
        const int (*run)[2];
        int nr = row_runs(j, &run);
        if (!nr) continue;
        size_t ro = (size_t)j * sbw;
        u8 *sr = sbuf + ro, *tr = tbuf + ro, *ar = abuf + ro;
        double Y = s->cy - (j + 0.5) / SA, r2 = Y * Y + f * f;
        double X0 = (0.5 / SA) - ox - s->cx, dX = 1.0 / SA;   /* X = X0 + i dX */
        double dz0 = ux * X0 + uy * f + uz * Y, ddz = ux * dX; /* world-up component of the eye ray */
        double v0 = nrm > 1e-9 ? kd * dz0 / nrm : 1e30, dv = nrm > 1e-9 ? kd * ddz / nrm : 0;
        for (int q = 0; q < nr; q++) {
            int L = run[q][0], H = run[q][1];
            bool sky2;
            int k = sky_split(dz0, ddz, L, H, &sky2);
            int parts[2][2] = { { L, k }, { k, H } };
            for (int pi = 0; pi < 2; pi++) {
                int a = parts[pi][0], b = parts[pi][1];
                if (a >= b) continue;
                if ((pi == 1) == sky2) {                  /* sky */
                    for (int i = a; i < b; i++) {
                        double v = v0 + dv * i;
                        if (v < lo) v = lo;
                        if (v >= 15.0) { sr[i] = (u8)(s->sky + 15); ar[i] = 0; continue; }
                        int vi = (int)v;
                        sr[i] = (u8)(s->sky + vi);
                        tr[i] = (u8)(s->sky + vi + 1);
                        ar[i] = (u8)((v - vi) * 255.0 + 0.5);
                    }
                    continue;
                }
                memset(sr + a, s->sea, (size_t)(b - a));
                memset(tr + a, fog_col, (size_t)(b - a));
                double h0 = sea_haze(X0, dX, r2, dz0, ddz, alt, a);
                for (int i = a; i < b;) {
                    /* a long step where the haze changes little, SEA_STEP where it changes fast */
                    int e = i + SEA_LONG < b ? i + SEA_LONG : b;
                    double h1 = sea_haze(X0, dX, r2, dz0, ddz, alt, e);
                    if (e - i > SEA_STEP && fabs(h1 - h0) > SEA_FLAT) {
                        e = i + SEA_STEP;
                        h1 = sea_haze(X0, dX, r2, dz0, ddz, alt, e);
                    }
                    int a16 = (int)(h0 * 256.0 + 128.0), st = (int)((h1 - h0) * 256.0 / (e - i));
                    for (int x = i; x < e; x++, a16 += st) {
                        int av = a16 >> 8;
                        ar[x] = (u8)(av < 0 ? 0 : av > 255 ? 255 : av);
                    }
                    h0 = h1; i = e;
                }
            }
        }
    }
}

static void draw_horizon(const ESnap *s, const double *Mt, double ox, double SA, double alt)
{
    for (int j = bj0; j < bj1; j++) {
        const int (*run)[2];
        int nr = row_runs(j, &run);
        for (int q = 0; q < nr; q++) {
            size_t o = (size_t)j * sbw + (size_t)run[q][0], n = (size_t)(run[q][1] - run[q][0]);
            memset(ibuf + o, 0, n);
            if (!s->has_sky) { memset(sbuf + o, 0, n); memset(abuf + o, 0, n); }
            else if (!fog_on) memset(abuf + o, 0, n);
        }
    }
    if (!s->has_sky) return;
    if (fog_on) { draw_horizon_haze(s, Mt, ox, SA, alt); return; }
    double ux = Mt[6], uy = Mt[7], uz = Mt[8];            /* world up in eye coordinates */
    double nrm = sqrt(ux * ux + uz * uz);
    for (int j = bj0; j < bj1; j++) {
        const int (*run)[2];
        int nr = row_runs(j, &run);
        if (!nr) continue;
        u8 *row = sbuf + (size_t)j * sbw;
        double Y = s->cy - (j + 0.5) / SA;
        double B = uy * s->f + uz * Y, A = 0;
        if (nrm > 1e-9) {
            B = (B + ux * ((0.5 / SA) - ox - s->cx)) / nrm;
            A = ux / (SA * nrm);
        } else B = B >= 0 ? 1e30 : -1e30;
        for (int q = 0; q < nr; q++) {
            int L = run[q][0], H = run[q][1];
            if (fabs(A) < 1e-12) { memset(row + L, sky_colour(s, B), (size_t)(H - L)); continue; }
            int i = L;
            while (i < H) {
                double d = A * i + B, lo, hi;
                u8 c = sky_colour(s, d);
                sky_range(s, d, &lo, &hi);
                double e = A > 0 ? (hi - B) / A : (lo - B) / A;  /* where d leaves [lo, hi) */
                int next;
                if (!isfinite(e) || e >= H) next = H;
                else { next = (int)ceil(e); if (next <= i) next = i + 1; }
                if (next > H) next = H;
                memset(row + i, c, (size_t)(next - i));
                i = next;
            }
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
    for (u32 k = 0; k <= nprim; k++) {
        if (k == dots_prim && (ndot || far_on)) draw_dots(c->s);
        if (k == nprim) break;
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
    /* the runs: world pixels of each row (gaps under 2 pixels closed), the sides with the widescreen */
    run_sai = SAi;
    for (int py = 0; py < 200; py++) {
        int (*r)[2] = runs[py], m = 0;
        const u8 *t = tag + py * 320;
        int yj = py * SAi;
        nruns[py] = 0;
        if (yj + SAi <= ry0 || yj >= ry1) continue;
        int tmp[160 + 2][2], k = 0;
        if (sides) { tmp[k][0] = rx0; tmp[k][1] = (int)floor(ox * SA); k++; }
        for (int a = 0; a < 320;) {
            if (t[a] != ENH_TAG_WORLD) { a++; continue; }
            int z = a;
            while (z < 320 && t[z] == ENH_TAG_WORLD) z++;
            tmp[k][0] = (int)floor((a + ox) * SA); tmp[k][1] = (int)ceil((z + ox) * SA);
            if (k < 161) k++;
            a = z;
        }
        if (sides) { tmp[k][0] = (int)ceil((320 + ox) * SA); tmp[k][1] = rx1; k++; }
        for (int q = 0; q < k; q++) {
            int a = tmp[q][0] < rx0 ? rx0 : tmp[q][0], b = tmp[q][1] > rx1 ? rx1 : tmp[q][1];
            if (a >= b) continue;
            if (m && a - r[m - 1][1] < 2 * SAi) { if (b > r[m - 1][1]) r[m - 1][1] = b; continue; }
            if (m == MAXRUN) { r[m - 1][1] = b; continue; }   /* too many: the last one reaches on */
            r[m][0] = a; r[m][1] = b; m++;
        }
        nruns[py] = m;
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

    project_dots(n, P, Mt, ox, SA);
    dots_prim = 0;
    dbg.on = enh_dots_debug;
    memcpy(dbg.P, P, sizeof dbg.P);
    for (int k = 0; k < 3; k++) { dbg.ax[0][k] = Mt[3 * k]; dbg.ax[1][k] = Mt[3 * k + 1]; dbg.ax[2][k] = Mt[3 * k + 2]; }
    dbg.ox = ox; dbg.cx = n->cx; dbg.cy = n->cy; dbg.f = n->f;
    dbg.rmax = enh_dots_range * DOT_GRID; dbg.r0 = dbg.rmax * enh_dots_fade / 100.0;

    double lw = SA * 0.5 < 1.0 ? 1.0 : SA * 0.5;          /* outline / line width in samples */
    double e[64][3], c[130][3], U[130], V[130], Fa[130];
    for (u32 k = 0; k < n->npoly; k++) {
        const EPoly *pl = &n->poly[k];
        if (n->dots && k == n->dots_at) dots_prim = nprim;
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
    if (n->dots && n->dots_at >= n->npoly) dots_prim = nprim;
    host_parallel((ry1 - ry0 + BAND_ROWS - 1) / BAND_ROWS, draw_band, &bc);
}

/* c mixed with t by a / 255 (two channels at a time) */
static inline u32 mix(u32 c, u32 t, u32 a)
{
    u32 w = a + (a >> 7), iw = 256 - w;                   /* 0..256 */
    u32 rb = ((c & 0xFF00FF) * iw + (t & 0xFF00FF) * w + 0x800080) >> 8 & 0xFF00FF;
    u32 g = ((c & 0x00FF00) * iw + (t & 0x00FF00) * w + 0x008000) >> 8 & 0x00FF00;
    return rb | g;
}

/* pal[c] (mixed with a dot's pal[dbuf] by ibuf / 255) mixed with pal[t] by a / 255; neighbouring
 * samples are mostly alike, so the last colour without a dot is kept (per thread, reset per band) */
static _Thread_local u32 last_key, last_col;
static inline u32 sample(size_t o, const u32 pal[256])
{
    u32 a = abuf[o];
    if (!ibuf[o]) {
        u32 key = a ? 0x1000000u | a << 16 | (u32)tbuf[o] << 8 | sbuf[o] : 0x2000000u | sbuf[o];
        if (key != last_key) { last_key = key; last_col = a ? mix(pal[sbuf[o]], pal[tbuf[o]], a) : pal[sbuf[o]] & 0xFFFFFF; }
        return last_col;
    }
    u32 c = mix(pal[sbuf[o]], pal[dbuf[o]], ibuf[o]);
    return a ? mix(c, pal[tbuf[o]], a) : c;
}

static inline u32 resolve(int X, int Y, const u32 pal[256])
{
    int AA = enh_aa;
    if (AA == 1) return sample((size_t)Y * sbw + X, pal);
    if (AA == 2) {
        size_t o = (size_t)(2 * Y) * sbw + (size_t)(2 * X);
        u32 c0 = sample(o, pal), c1 = sample(o + 1, pal), c2 = sample(o + sbw, pal), c3 = sample(o + sbw + 1, pal);
        u32 rb = (c0 & 0xFF00FF) + (c1 & 0xFF00FF) + (c2 & 0xFF00FF) + (c3 & 0xFF00FF);
        u32 g = (c0 & 0xFF00) + (c1 & 0xFF00) + (c2 & 0xFF00) + (c3 & 0xFF00);
        return (rb >> 2 & 0xFF00FF) | (g >> 2 & 0xFF00);
    }
    u32 rb = 0, g = 0;                                    /* r, b in 16-bit lanes; up to 16 samples */
    for (int j = 0; j < AA; j++) {
        size_t o = (size_t)(Y * AA + j) * sbw + (size_t)X * AA;
        for (int i = 0; i < AA; i++) {
            u32 c = sample(o + (size_t)i, pal);
            rb += c & 0xFF00FF; g += c & 0xFF00;
        }
    }
    if (AA == 2) return (rb >> 2 & 0xFF00FF) | (g >> 2 & 0xFF00);
    if (AA == 4) return (rb >> 4 & 0xFF00FF) | (g >> 4 & 0xFF00);
    u32 n = (u32)(AA * AA);
    return ((rb >> 16) / n) << 16 | ((g >> 8) / n) << 8 | ((rb & 0xFFFF) / n);
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
        if (enh_dots_debug != dbg_last) { dbg_last = enh_dots_debug; changed = true; }
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

/* The dots' debug overlay over an output pixel (X, Y) of the world view, or the pixel as it is: the ray
 * through the pixel's centre met with the ground z = 0; a line is drawn where the pixel is within hw
 * pixels of it (the distance on the ground over the ground's change per pixel). */
static u32 debug_pixel(int X, int Y, u32 c)
{
    const double *P = dbg.P, *c0 = dbg.ax[0], *c1 = dbg.ax[1], *c2 = dbg.ax[2];
    int S = enh_scale;
    double hw = S > 2 ? S * 0.25 + 0.1 : 0.6;
    double Xe = (X + 0.5) / S - dbg.ox - dbg.cx, Ye = dbg.cy - (Y + 0.5) / S, d[3], di[3], dj[3];
    for (int k = 0; k < 3; k++) {
        d[k] = Xe * c0[k] + dbg.f * c1[k] + Ye * c2[k];
        di[k] = c0[k] / S; dj[k] = -c2[k] / S;
    }
    double gz = sqrt(di[2] * di[2] + dj[2] * dj[2]);
    if (gz > 1e-12 && fabs(d[2]) / gz < hw * 1.5) return 0xFF0000;        /* the horizon */
    if (d[2] > -1e-9) return c;
    double A = P[2] < 1.0 ? 1.0 : P[2], t = -A / d[2];
    double wx = P[0] + t * d[0], wy = P[1] + t * d[1];
    double ai = di[2] / d[2], aj = dj[2] / d[2];
    double ix = t * (di[0] - d[0] * ai), iy = t * (di[1] - d[1] * ai);
    double jx = t * (dj[0] - d[0] * aj), jy = t * (dj[1] - d[1] * aj);
    /* rings: where the dots start fading out, the range's edge */
    double rx = wx - P[0], ry = wy - P[1], r = sqrt(rx * rx + ry * ry);
    if (r > 1e-9) {
        double gi = (rx * ix + ry * iy) / r, gj = (rx * jx + ry * jy) / r, gr = sqrt(gi * gi + gj * gj);
        if (gr > 1e-12 && (fabs(r - dbg.r0) / gr < hw || fabs(r - dbg.rmax) / gr < hw)) return 0x00FFFF;
    }
    if (r > dbg.rmax) return c;                       /* no dots beyond the range's edge, no grid */
    /* the grid lines: x = const and y = const, green for the family that runs along the view; where the
     * lines of a family would be under 3 pixels apart only every 2nd, 4th, ... one is kept, so some reach
     * their vanishing point on the horizon */
    double gx = sqrt(ix * ix + jx * jx), gy = sqrt(iy * iy + jy * jy);
    bool along_x = fabs(c1[1]) >= fabs(c1[0]);       /* looking along y: lines x = const run along the view */
    double Gx = DOT_GRID, Gy = DOT_GRID;
    while (Gx < gx * 3.0 && Gx < 1e12) Gx *= 2.0;
    while (Gy < gy * 3.0 && Gy < 1e12) Gy *= 2.0;
    double ex = fabs(wx - Gx * floor(wx / Gx + 0.5)), ey = fabs(wy - Gy * floor(wy / Gy + 0.5));
    bool lx = gx > 1e-12 && ex / gx < hw;
    bool ly = gy > 1e-12 && ey / gy < hw;
    if (lx) return along_x ? 0x00FF00 : 0xFFFF00;
    if (ly) return along_x ? 0xFFFF00 : 0x00FF00;
    return c;
}

static void compose_band(int b, void *ctx)
{
    const ComposeCtx *c = ctx;
    const u32 *pic = c->pic, *pal = c->pal;
    const u8 *tag = c->tag;
    u32 *out = c->out;
    int S = enh_scale, W = c->W, x0 = c->x0, sw = c->sw;
    int rX0 = c->rX0, rX1 = c->rX1, rY0 = c->rY0, rY1 = c->rY1;
    bool active = c->active, dbg_on = active && dbg.on;
    last_key = 0;
    int Y1 = (b + 1) * BAND_ROWS < c->H ? (b + 1) * BAND_ROWS : c->H;
    for (int Y = b * BAND_ROWS; Y < Y1; Y++) {
        int py = Y / S;
        const u32 *prow = pic + py * 320;
        const u8 *trow = tag + py * 320;
        u32 *o = out + (size_t)Y * W;
        bool yin = active && Y >= rY0 && Y < rY1;
        /* the widescreen sides, then the picture one original pixel (S output pixels) at a time */
        for (int X = 0; X < W; X++) {
            if (X == x0) {
                for (int px = 0; px < 320 && X < x0 + sw; px++) {
                    bool world = yin && trow[px] == ENH_TAG_WORLD;
                    for (int k = 0; k < S; k++, X++) {
                        if (world && X >= rX0 && X < rX1) {
                            o[X] = resolve(X, Y, pal);
                            if (dbg_on) o[X] = debug_pixel(X, Y, o[X]);
                        } else o[X] = prow[px];
                    }
                }
                X = x0 + sw - 1;
                continue;
            }
            if (yin && X >= rX0 && X < rX1) {
                o[X] = resolve(X, Y, pal);
                if (dbg_on) o[X] = debug_pixel(X, Y, o[X]);
            } else o[X] = 0;
        }
    }
}
