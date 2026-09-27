/* render3d — the 3Space math library: scalars, square roots, distances, trigonometry, matrices and
 * vectors (AP 277a, the identical PS 3d50 copies, INTRO's sin/cos copy). render3d_math.md §3–§5,
 * video_ts.md §2–§3. Register routines of the asm core become pointer functions (r3d_priv.h). */
#include "render3d/r3d_priv.h"

/* ================================================================================================
 * sin / cos / atan / asin (render3d_math.md §3, §4.4)
 * ============================================================================================== */

/* The inline cos lookup of the core: i = a >> 4 (arithmetic), |i|, [i] or −[800h − i]. */
s16 r3d_cos(u16 a)
{
    u16 seg, tab;
    if (prog_current_id() == PROG_INTRO) { seg = SEG(0x1739); tab = 0x0000; }   /* video_library.md §9.1 */
    else { seg = r3d_cs_seg(); tab = r3d_cs_off(CS_COS_TAB); }
    s16 i = (s16)((s16)a >> 4);
    if (i < 0) i = (s16)-i;
    if (i < 0x400) return (s16)rd16(seg, (u16)(tab + 2 * i));
    return (s16)-(s16)rd16(seg, (u16)(tab + 2 * (0x800 - i)));
}

/* INTRO 1739:0802 / PS 3d50:1f7c / AP 277a:5e66 — render3d_math.md §3.1 */
s16 sin16(u16 a) { return r3d_cos((u16)(a + 0xC000)); }
/* PS 3d50:1f87 / AP 277a:5e71 — render3d_math.md §3.1 */
u16 cos16(u16 a) { return (u16)r3d_cos(a); }

static s16 atan_corr(u16 q)            /* CS:6028[q] (i8); q can run past the table into the code */
{
    return (s8)rd8(r3d_cs_seg(), (u16)(r3d_cs_off(CS_ATAN_CORR) + q));
}

static u16 atan_fix(u16 a, s16 sx, s16 sy)
{
    if (sx <= 0) a = (u16)(0x800 - a);
    if (sy <= 0) a = (u16)(0x1000 - a);
    return (u16)(a << 4);
}

u16 r3d_atan2_16(s16 x0, s16 y0)
{
    s16 si = 1, di = 1;
    u16 x = (u16)x0, y = (u16)y0, a;
    if (x0 < 0) { si = -1; x = (u16)-x; }
    if (y0 < 0) { di = -1; y = (u16)-y; }
    if (x == 0) {
        if (y == 0) return 0;                   /* ax = 0, no quadrant fix-up */
        a = 0x400;
    } else if (y == 0) a = 0;
    else if (x == y) a = 0x200;
    else if (x > y) {                           /* q = (y·512)/x, unsigned 32/16 */
        u16 q = (u16)(((u32)y << 9) / x);
        a = (u16)(q + atan_corr(q));
    } else {
        u16 q = (u16)(((u32)x << 9) / y);
        a = (u16)(0x400 - (u16)(q + atan_corr(q)));
    }
    return atan_fix(a, si, di);
}

/* PS 3d50:21ba / AP 277a:6228 — render3d_math.md §4.4 */
u16 atan2_16(u16 a0, u16 a1) { return r3d_atan2_16((s16)a0, (s16)a1); }

u16 r3d_atan2_32(s32 x0, s32 y0)
{
    s16 si = 1, di = 1, bp = 9;
    u32 x = (u32)x0, y = (u32)y0;
    u16 a;
    if (x0 < 0) { si = -1; x = (u32)-x; }
    if (y0 < 0) { di = -1; y = (u32)-y; }
    if (x == 0) {
        if (y == 0) return 0;
        a = 0x400;
    } else if (y == 0) a = 0;
    else if (x == y) a = 0x200;
    else if (x > y) {
        /* shift x right while its high word is set, then y left, 9 steps in all */
        while (x >> 16) { x >>= 1; if (--bp == 0) break; }
        while (bp) { y <<= 1; bp--; }
        s32 q = (s32)y / (s32)x;                /* LDIV@ 0000:1d69 (x != 0 here) */
        u16 b = (u16)q;
        a = (u16)(b + atan_corr(b));
    } else {
        while (y >> 16) { y >>= 1; if (--bp == 0) break; }
        while (bp) { x <<= 1; bp--; }
        s32 q = (y == 0) ? (s32)div_unreachable("atan2_32 /0") : (s32)x / (s32)y;
        u16 b = (u16)q;
        a = (u16)(0x400 - (u16)(b + atan_corr(b)));
    }
    return atan_fix(a, si, di);
}

/* PS 3d50:225a / AP 277a:62c8 — render3d_math.md §4.4 (x, y as low/high words) */
u16 atan2_32(u16 a0, u16 a1, u16 a2, u16 a3)
{
    return r3d_atan2_32((s32)((u32)a1 << 16 | a0), (s32)((u32)a3 << 16 | a2));
}

/* AP asin_tab lookup (render3d_math.md §3.3): u = |v| >> 2; i = u >> 3 below 0E00h, else u − 0C40h. */
static s16 asin_lookup(s16 v, s16 *sign)
{
    *sign = 1;
    if (v < 0) { *sign = -1; v = (s16)-v; }
    if (r3d_is_ps()) {                                     /* PS 3d50:1562: asin_ps[|v| >> 5] */
        s16 i = (s16)(v >> 5);
        return (s16)rd16(r3d_cs_seg(), (u16)(0x1160 + 2 * i));
    }
    s16 u = (s16)(v >> 2);                                 /* sar (v = -8000h stays negative) */
    u16 i = (u16)u <= 0x0DFF ? (u16)(u >> 3) : (u16)(u - 0x0C40);
    return (s16)rd16(r3d_cs_seg(), (u16)(CS_ASIN_TAB + 2 * i));
}

s16 r3d_asin16(s16 v)
{
    s16 s, t = asin_lookup(v, &s);
    return s > 0 ? t : (s16)-t;
}
s16 r3d_acos16(s16 v)
{
    s16 s, t = asin_lookup(v, &s);
    return (s16)((s < 0 ? t : (s16)-t) + 0x4000);
}
/* PS 3d50:1562 / AP 277a:6b28 — render3d_math.md §4.4, video_ts.md §3.1 */
u16 asin16(u16 a0) { return (u16)r3d_asin16((s16)a0); }
/* PS 3d50:1589 / AP 277a:6b5d — render3d_math.md §4.4, video_ts.md §3.1 */
u16 acos16(u16 a0) { return (u16)r3d_acos16((s16)a0); }

/* ================================================================================================
 * Scalars (render3d_math.md §5.1)
 * ============================================================================================== */

/* PS 3d50:0012 / AP 277a:3968 — bits 8..23 of the product */
u16 mul_shr8(u16 a, u16 b) { return (u16)((u32)((s32)(s16)a * (s16)b) >> 8); }
/* PS 3d50:0022 / AP 277a:3978 — bits 10..25 of the product */
u16 mul_shr10(u16 a, u16 b) { return (u16)((u32)((s32)(s16)a * (s16)b) >> 10); }

/* PS 3d50:003a / AP 277a:3990 bit_index8: index of the top set bit of CX (0 below 2). */
static u16 bit_index(u16 cx)
{
    u16 bx = 0; u8 cl = (u8)cx;
    if (cx >> 8) { bx = 8; cl = (u8)(cx >> 8); }
    while ((cl >>= 1) != 0) bx++;
    return bx;
}

/* PS 3d50:004c / AP 277a:39a2 isqrt16 — odd-number count up to 200, else the (r+b)^2 trial root.
 * Both give floor(sqrt(n)) exactly (traced: no 16-bit overflow of the trial square). */
static u16 isqrt16(u16 n)
{
    if (n <= 200) {
        u16 cx = 1; s16 ax = (s16)n;
        for (;;) { ax = (s16)(ax - cx); if (ax < 0) break; cx += 2; }
        return (u16)(cx >> 1);
    }
    u16 di = n, cl = (u16)(bit_index(n) >> 1);
    u16 bx = (u16)(1u << cl), dx = (u16)(bx << cl), es = dx, cx = (u16)(bx >> 1), si = dx;
    for (;;) {
        dx >>= 2;
        u16 ax = (u16)(es + si + dx);
        if (ax > di) { si >>= 1; cx >>= 1; if (!cx) break; continue; }
        bx = (u16)(bx + cx); cx >>= 1;
        if (!cx) break;
        es = ax; si = (u16)((si >> 1) + dx);
    }
    return bx;
}

/* PS 3d50:0102 / AP 277a:3a58 — digit-by-digit root with the 4^i tables (CS:3A28/3A38); traced to be
 * the exact floor(sqrt(n)) (the 32-bit trial never overflows). */
u16 r3d_isqrt32(u32 n)
{
    if ((n >> 16) == 0) return isqrt16((u16)n);
    u16 h = (u16)((bit_index((u16)(n >> 16)) + 16) >> 1);
    u32 rem = n - ((u32)1 << (2 * h));
    u16 bx = (u16)(1u << h), cx = (u16)(bx >> 1);
    s16 idx = (s16)(h - 1);
    u32 t = (u32)bx << (idx + 1);
    for (;;) {
        u32 p4 = (u32)1 << (2 * idx);
        if (rem >= p4 && rem - p4 >= t) {
            rem = rem - p4 - t;
            bx = (u16)(bx + cx); cx >>= 1; idx--;
            if (!cx) break;
            t = (u32)bx << (idx + 1);
        } else {
            t >>= 1; cx >>= 1; idx--;
            if (!cx) break;
        }
    }
    return bx;
}
/* PS 3d50:0102 / AP 277a:3a58 — render3d_math.md §5.1 (u32 argument: the same two stack words) */
u16 isqrt32(u32 n) { return r3d_isqrt32(n); }

static u32 labs32_ge(s32 a, s32 b)         /* sub/sbb; jge (signed); neg */
{
    s32 d = (s32)((u32)a - (u32)b);
    return a >= b ? (u32)d : (u32)-(u32)d;
}

u32 r3d_ldist3v(u32 a, u32 b, u32 c)
{
    u32 t;
    if (a <= b) { t = a; a = b; b = t; }        /* a = max(a, b) */
    if (a <= c) { t = a; a = c; c = t; }        /* a = max */
    if (c <= b) { t = c; c = b; b = t; }        /* c = middle, b = min */
    c >>= 2; a += c; c >>= 2; a += c; c >>= 1; a += c;
    b >>= 2; a += b;
    return a;
}
u32 r3d_ldist2v(u32 a, u32 b)
{
    if (a <= b) { u32 t = a; a = b; b = t; }
    b >>= 2; a += b; b >>= 1; a += b;
    return a;
}

/* AP 277a:3b3c — ldist3(lvec *a, lvec *b) (REQUESTS: two near pointers) */
u32 ldist3(u16 a_ds, u16 b_ds)
{
    u8 *a = DSP(a_ds), *b = DSP(b_ds);
    return r3d_ldist3v(labs32_ge(gs32(a, 0), gs32(b, 0)), labs32_ge(gs32(a, 4), gs32(b, 4)),
                       labs32_ge(gs32(a, 8), gs32(b, 8)));
}
/* AP 277a:3b8b — the same on three values */
u32 ldist3v(u32 a, u32 b, u32 c) { return r3d_ldist3v(a, b, c); }
/* PS 3d50:01e6 / AP 277a:3bfc — 2D (x, y) */
u32 ldist2(u16 a_ds, u16 b_ds)
{
    u8 *a = DSP(a_ds), *b = DSP(b_ds);
    return r3d_ldist2v(labs32_ge(gs32(a, 0), gs32(b, 0)), labs32_ge(gs32(a, 4), gs32(b, 4)));
}
/* AP 277a:3c36 */
u32 ldist2v(u32 a0, u32 a1) { return r3d_ldist2v(a0, a1); }

/* PS 3d50:0254 / AP 277a:3c6a */
u16 dist2_16(u16 a0, u16 a1)
{
    u16 a = (s16)a0 < 0 ? (u16)-a0 : a0, c = (s16)a1 < 0 ? (u16)-a1 : a1;
    if (a <= c) { u16 t = a; a = c; c = t; }
    c >>= 2; a = (u16)(a + c); c >>= 1; a = (u16)(a + c);
    return a;
}

/* AP 277a:3c90 — monotonic depth key: top-bit index in the high byte, the next 8 bits below it. */
u16 log_key(u32 v)
{
    u16 hi = (u16)(v >> 16), lo = (u16)v;
    u16 ax; u8 cl;
    if (hi) { ax = 0x1000; cl = (u8)hi; if (hi >> 8) { cl = (u8)(hi >> 8); ax += 0x800; } }
    else {
        if (!lo) return 0;
        ax = 0x0001; cl = (u8)lo; if (lo >> 8) { cl = (u8)(lo >> 8); ax += 0x800; }
    }
    while ((cl >>= 1) != 0) ax = (u16)(ax + 0x100);
    u16 bx = hi;
    if (!(bx >> 8)) {
        bx = (u16)((bx & 0xFF) << 8 | lo >> 8);
        if (!(bx >> 8)) {
            bx = lo;
            if (!(bx >> 8)) bx = (u16)((bx & 0xFF) << 8);
        }
    }
    bool cf;
    do { cf = (bx & 0x8000) != 0; bx = (u16)(bx << 1); } while (!cf);
    return (u16)((ax & 0xFF00) | (bx >> 8));
}

/* PS 3d50:027a / AP 277a:3cfc */
u32 mul32(u16 a0, u16 a1) { return (u32)((s32)(s16)a0 * (s16)a1); }

/* PS 3d50:0292 / AP 277a:3d14 — low 32 bits of a·b (sign-magnitude, same result) */
u32 lmul(s32 a, s16 b) { return (u32)a * (u32)(s32)b; }

/* PS 3d50:02d0 / AP 277a:3d52 — v >> (n & 31), arithmetic */
u32 lsar(s32 v, u16 n) { return (u32)(v >> (n & 31)); }

/* AP 277a:3dc1 — num/den with saturation (DX = remainder, 0 when saturated). */
s16 ldiv_sat_rem(s32 num, s16 den, s16 *dx)
{
    u32 an = num < 0 ? (u32)-(u32)num : (u32)num;
    u32 ad = den < 0 ? (u32)(u16)-(u16)den : (u32)(u16)den;
    if ((u32)(ad * 32767u) < an) {                    /* 277a:3dac: CF = 32767·|den| < |num| */
        if (dx) *dx = 0;
        return ((num < 0) != (den < 0)) ? (s16)0x8000 : 0x7FFF;
    }
    if (den == 0) return (s16)div_unreachable("ldiv_sat 0/0");   /* INT 0 hung the original */
    if (dx) *dx = (s16)(num % den);
    return (s16)(num / den);
}
/* AP 277a:3dc1 — render3d_sky.md §2.4 */
s16 ldiv_sat(s32 a0, s16 a1) { return ldiv_sat_rem(a0, a1, NULL); }

/* AP 277a:3e1c — v ≈ m << e with |m| <= 7FFFh; e also at CS:3E1A and *exp */
s16 lnorm(s32 a0, u16 p1_ds)
{
    bool neg = a0 < 0;
    u32 v = neg ? (u32)-(u32)a0 : (u32)a0;
    u16 cx = 0;
    while (v >> 16) { v >>= 1; cx++; }
    if (v & 0x8000) { v >>= 1; cx++; }
    SEGW(0x277A, CS_LNORM_EXP) = cx;
    if (p1_ds) DSW(p1_ds) = cx;
    return neg ? (s16)-(s16)(u16)v : (s16)(u16)v;
}

/* AP 277a:3e84 — index of the top set bit, −1 for 0 */
s16 lbitlen(u32 a0)
{
    s16 ax = -1; u16 dx = (u16)a0;
    if (a0 >> 16) { ax = 15; dx = (u16)(a0 >> 16); }
    u8 dl = (u8)dx;
    if (dx >> 8) { dl = (u8)(dx >> 8); ax += 8; }
    if (dl) { ax++; while ((dl >>= 1) != 0) ax++; }
    return ax;
}
/* AP 277a:3ec8 — the same for 16 bits (header fix: one argument) */
s16 bitlen16(u16 a0) { return lbitlen(a0); }

/* AP 277a:3ed4 — Σ signed bytes (loop count 0 = 65536, as LOOP) */
s16 sum_i8(u16 p0_ds, s16 a1)
{
    u16 bx = 0, cx = (u16)a1;
    do { bx = (u16)(bx + (s8)DSB(p0_ds)); p0_ds++; } while (--cx);
    return (s16)bx;
}
/* AP 277a:3eed — Σ words → i32 */
s32 sum_i16(u16 p0_ds, s16 a1)
{
    u32 s = 0; u16 cx = (u16)a1;
    do { s += (u32)(s32)DSS(p0_ds); p0_ds += 2; } while (--cx);
    return (s32)s;
}

/* AP 277a:3f0e */
s16 in_open_range(s16 a0, s16 a1)
{
    if (a0 >= a1) return 0;
    if (a0 <= (s16)-a1) return 0;
    return 1;
}
/* AP 277a:3f27 clamp_sym */
s16 ap_clamp_sym_277a(s16 a0, s16 a1)
{
    if (a0 > a1) return a1;
    s16 nb = (s16)-a1;
    return a0 >= nb ? a0 : nb;
}
/* AP 277a:3f3c / 3f55 */
s16 imin(s16 a0, s16 a1) { return a0 < a1 ? a0 : a1; }
s16 imax(s16 a0, s16 a1) { return a0 > a1 ? a0 : a1; }

/* AP 277a:4123 normalize_to_sum_impl */
static u16 normalize_to_sum_impl(u16 p, s16 n, s16 total)
{
    s32 s = (s16)sum_i16(p, n);                       /* cwd of AX only */
    for (s16 i = 0; i < n; i++) {
        s32 prod = (s32)mul32(DSW(p + 2 * i), (u16)total);
        DSW(p + 2 * i) = (u16)(s ? prod / s : (s32)div_unreachable("normalize_to_sum /0"));
    }
    s16 di = (s16)(total - (s16)sum_i16(p, n));
    while (di != 0) {
        bool any = false;
        for (s16 i = 0; i < n && di != 0; i++)
            if (DSW(p + 2 * i) != 0) { DSW(p + 2 * i)++; di--; any = true; }
        if (!any) break;           /* PORT: all-zero input looped forever in the original */
    }
    return p;
}
/* AP 277a:410d — render3d_math.md §5.1 */
u16 normalize_to_sum(u16 p0_ds, s16 a1, s16 a2) { return normalize_to_sum_impl(p0_ds, a1, a2); }

/* AP 277a:41c4 — y0 + (x − x0)·(y1 − y0)/(x1 − x0), 16-bit idiv */
s16 lerp_map(s16 x, s16 x0, s16 x1, s16 y0, s16 y1)
{
    s16 cx = (s16)(x1 - x0), dx = (s16)(y1 - y0), ax = (s16)(x - x0);
    return (s16)(idiv16_retry((s32)ax * dx, cx, NULL) + y0);
}

/* PS 3d50:15b4 / AP 277a:4e76 — (r·cos a) >> 14, 32-bit */
s32 mul_cos(s16 r, u16 a) { return ((s32)r3d_cos(a) * r) >> 14; }
/* PS 3d50:15f4 / AP 277a:4eb6 */
s32 mul_sin(s16 r, u16 a) { return ((s32)r3d_sin(a) * r) >> 14; }

/* ================================================================================================
 * Matrices (render3d_math.md §4.3–§4.5). Pointers into mem[]; writes in the original order.
 * ============================================================================================== */

#define M(p, i) gs16(p, 2 * (i))

void r3d_mat_mul(u8 *a, u8 *b, u8 *c)
{
    u8 ta = a[0x12], tb = b[0x12];
    if (ta == 0 || tb == 0) {
        const u8 *src = ta == 0 ? b : a;
        c[0x12] = ta == 0 ? tb : ta;
        for (int i = 0; i < 18; i += 2) p16(c, i, g16(src, i));     /* rep movsw, 9 words */
        return;
    }
#define P2(i, j, k, l)      fx14((s32)((u32)((s32)M(a, i) * M(b, j)) + (u32)((s32)M(a, k) * M(b, l))))
#define P3(i, j, k, l, m, n) fx14((s32)((u32)((s32)M(a, i) * M(b, j)) + (u32)((s32)M(a, k) * M(b, l)) + \
                                      (u32)((s32)M(a, m) * M(b, n))))
    if (ta == 1 && tb == 1) {
        p16(c, 0, P2(0, 0, 1, 3)); p16(c, 2, P2(0, 1, 1, 4));
        p16(c, 6, P2(3, 0, 4, 3)); p16(c, 8, P2(3, 1, 4, 4));
        p16(c, 4, 0); p16(c, 0xA, 0); p16(c, 0xC, 0); p16(c, 0xE, 0); p16(c, 0x10, 0x4000);
        c[0x12] = 1;
        return;
    }
    if (ta == 1) {                          /* a heading only, b general */
        p16(c, 0xC, g16(b, 0xC)); p16(c, 0xE, g16(b, 0xE)); p16(c, 0x10, g16(b, 0x10));
        p16(c, 4, P2(0, 2, 1, 5)); p16(c, 0xA, P2(3, 2, 4, 5));
    } else if (tb == 1) {                   /* a general, b heading only */
        p16(c, 4, g16(a, 4)); p16(c, 0xA, g16(a, 0xA)); p16(c, 0x10, g16(a, 0x10));
        p16(c, 0xC, P2(6, 0, 7, 3)); p16(c, 0xE, P2(6, 1, 7, 4));
    } else {
        p16(c, 0, P3(0, 0, 1, 3, 2, 6)); p16(c, 2, P3(0, 1, 1, 4, 2, 7)); p16(c, 4, P3(0, 2, 1, 5, 2, 8));
        p16(c, 6, P3(3, 0, 4, 3, 5, 6)); p16(c, 8, P3(3, 1, 4, 4, 5, 7)); p16(c, 0xA, P3(3, 2, 4, 5, 5, 8));
        p16(c, 0xC, P3(6, 0, 7, 3, 8, 6)); p16(c, 0xE, P3(6, 1, 7, 4, 8, 7)); p16(c, 0x10, P3(6, 2, 7, 5, 8, 8));
        c[0x12] = 3;
        return;
    }
    /* 277a:44d3: the upper-left 2×2 block */
    p16(c, 0, P2(0, 0, 1, 3)); p16(c, 2, P2(0, 1, 1, 4));
    p16(c, 6, P2(3, 0, 4, 3)); p16(c, 8, P2(3, 1, 4, 4));
    c[0x12] = 3;
#undef P2
#undef P3
}

void r3d_mat_transpose(u8 *m)
{
    u16 t;
    t = g16(m, 2); p16(m, 2, g16(m, 6)); p16(m, 6, t);
    t = g16(m, 4); p16(m, 4, g16(m, 0xC)); p16(m, 0xC, t);
    t = g16(m, 0xA); p16(m, 0xA, g16(m, 0xE)); p16(m, 0xE, t);
}

void r3d_mat_from_angles(const u8 *ang, u8 *m)
{
    u16 p = g16(ang, 0), r = g16(ang, 2), h = g16(ang, 4);
    u8 type = p ? 3 : r ? 2 : h ? 1 : 0;
    m[0x12] = type;
    if (type == 0) {
        p16(m, 0, 0x4000); p16(m, 8, 0x4000); p16(m, 0x10, 0x4000);
        p16(m, 2, 0); p16(m, 4, 0); p16(m, 6, 0); p16(m, 0xA, 0); p16(m, 0xC, 0); p16(m, 0xE, 0);
        return;
    }
    if (type == 1) {
        s16 c = r3d_cos(h), s = r3d_sin(h);
        p16(m, 0, (u16)c); p16(m, 8, (u16)c);
        p16(m, 2, (u16)s); p16(m, 6, (u16)-s);
        p16(m, 4, 0); p16(m, 0xA, 0); p16(m, 0xC, 0); p16(m, 0xE, 0); p16(m, 0x10, 0x4000);
        return;
    }
#define C2(x) (s16)(r3d_cos((u16)(x)) >> 1)
#define S2(x) (s16)(r3d_sin((u16)(x)) >> 1)
#define MUL(a, b) fx14((s32)(a) * (b))
    s16 sp = r3d_sin(p);
    p16(m, 0xA, (u16)sp);
    s16 d = C2(r - h), e = C2(r + h);
    s16 A = (s16)(d + e), B = (s16)(d - e);
    p16(m, 0, (u16)(A - MUL(sp, B)));
    p16(m, 0xE, (u16)(B - MUL(sp, A)));
    d = S2(h + r); e = S2(h - r);
    A = (s16)(d + e); B = (s16)(d - e);
    p16(m, 2, (u16)(MUL(sp, B) + A));
    p16(m, 0xC, (u16)(MUL(sp, A) + B));
    p16(m, 8, (u16)(C2(p - h) + C2(p + h)));
    p16(m, 0x10, (u16)(C2(p - r) + C2(p + r)));
    p16(m, 4, (u16)-(s16)(S2(r - p) + S2(r + p)));
    p16(m, 6, (u16)-(s16)(S2(h - p) + S2(h + p)));
#undef C2
#undef S2
#undef MUL
}

void r3d_mat_to_angles(const u8 *m, u8 *ang)
{
    s16 a0 = r3d_asin16(gs16(m, 0xA));
    p16(ang, 0, (u16)a0);
    u16 abs0 = a0 < 0 ? (u16)-a0 : (u16)a0;
    if (abs0 == 0x4000) {
        p16(ang, 2, 0);
        p16(ang, 4, r3d_atan2_16(gs16(m, 0), gs16(m, 2)));
    } else {
        p16(ang, 2, r3d_atan2_16(gs16(m, 0x10), (s16)-gs16(m, 4)));
        p16(ang, 4, r3d_atan2_16(gs16(m, 8), (s16)-gs16(m, 6)));
    }
}

/* PS 3d50:04ca / AP 277a:41e0 */
void mat_mul(u16 A, u16 B, u16 out) { r3d_mat_mul(DSP(A), DSP(B), DSP(out)); }
/* PS 3d50:0840 / AP 277a:4556 */
void mat_transpose(u16 M_) { r3d_mat_transpose(DSP(M_)); }
/* PS 3d50:0864 / AP 277a:457a (returns m) */
u16 mat_from_angles(u16 a0, u16 a1) { r3d_mat_from_angles(DSP(a0), DSP(a1)); return a1; }
/* PS 3d50:0c8e / AP 277a:49a4 */
void mat_to_angles(u16 a0, u16 a1) { r3d_mat_to_angles(DSP(a0), DSP(a1)); }

/* PS 3d50:0cf6 / AP 277a:4a0c — out[j] = Σ v[i]·m[i][j] >> 14 (no overflow check); writes in order */
void vec_rotate_n(const u8 *v, const u8 *m, u8 *out)
{
    s8 t = (s8)m[0x12];
    if (t <= 0) { for (int i = 0; i < 6; i += 2) p16(out, i, g16(v, i)); return; }
    if (t == 1) {
        p16(out, 4, g16(v, 4));
        p16(out, 0, (u16)fx14((s32)((u32)((s32)M(m, 0) * gs16(v, 0)) + (u32)((s32)M(m, 3) * gs16(v, 2)))));
        p16(out, 2, (u16)fx14((s32)((u32)((s32)M(m, 1) * gs16(v, 0)) + (u32)((s32)M(m, 4) * gs16(v, 2)))));
        return;
    }
    for (int j = 0; j < 3; j++) {
        u32 s = (u32)((s32)M(m, j) * gs16(v, 0)) + (u32)((s32)M(m, 3 + j) * gs16(v, 2)) +
                (u32)((s32)M(m, 6 + j) * gs16(v, 4));
        p16(out, 2 * j, (u16)fx14((s32)s));
    }
}
/* PS 3d50:0dcf / AP 277a:4ae5 */
u16 vec_rotate(u16 a0, u16 a1, u16 a2) { vec_rotate_n(DSP(a0), DSP(a1), DSP(a2)); return a2; }

/* PS 3d50:0de4 / AP 277a:4afa dot48: Σ v[i]·c[i] (i32 × i16, exact 48-bit; the third term skipped when
 * c2 == 0). The scratch words CS:3840.. (PS CS:0004..) are not read by anyone else: kept local. */
static s64 dot48(const u8 *v, s16 c0, s16 c1, s16 c2)
{
    s64 s = (s64)gs32(v, 0) * c0 + (s64)gs32(v, 4) * c1;
    if (c2) s += (s64)gs32(v, 8) * c2;
    return s;
}
static u32 bits14_45(s64 s) { return (u32)((u64)s >> 14); }

/* PS 3d50:0f11 / AP 277a:4c27 — lvec rotation, bits 14..45 of the 48-bit sums; z first */
void lvec_rotate_n(const u8 *v, const u8 *m, u8 *out)
{
    u8 t = m[0x12];
    if (t == 0) { for (int i = 0; i < 12; i += 2) p16(out, i, g16(v, i)); return; }
    if (t == 1) p32(out, 8, g32(v, 8));
    else p32(out, 8, bits14_45(dot48(v, M(m, 2), M(m, 5), M(m, 8))));
    p32(out, 0, bits14_45(dot48(v, M(m, 0), M(m, 3), M(m, 6))));
    p32(out, 4, bits14_45(dot48(v, M(m, 1), M(m, 4), M(m, 7))));
}
/* PS 3d50:0f9d / AP 277a:4cb3 (returns out) */
u16 lvec_rotate(u16 a0, u16 a1, u16 a2) { lvec_rotate_n(DSP(a0), DSP(a1), DSP(a2)); return a2; }

/* One component of 277a:4cc8: the 48-bit value (sign-extended 32-bit x) shifted left by n, top 32 bits
 * as the result; *cf = the last bit shifted out of bit 47 (unchanged when n == 0). */
static u32 shl48_hi(s32 x, int from_bit16, u16 n, bool *cf)
{
    u64 v = (u64)(s64)x << (from_bit16 ? 16 : 0);        /* 48-bit register DX:AX:BP */
    for (u16 i = 0; i < n; i++) { *cf = (v >> 47) & 1; v <<= 1; }
    return (u32)(v >> 16);
}

/* PS 3d50:0fb2 / AP 277a:4cc8 — out (i32) = v·M scaled by 2^s; returns the carry flag (§4.5). */
bool vtx_rotate_shift(const u8 *v, const u8 *m, u8 *out, u16 s)
{
    bool cf = false;                                 /* "or al, al" clears CF */
    u8 t = m[0x12];
    if (t == 0) {
        for (int i = 0; i < 3; i++) {                /* DX:AX = sext(v[i]) << s */
            u32 x = (u32)(s32)gs16(v, 2 * i);
            for (u16 k = 0; k < s; k++) { cf = (x >> 31) & 1; x <<= 1; }
            p32(out, 4 * i, x);
        }
        return cf;
    }
    int ncol = t == 1 ? 2 : 3;
    for (int j = 0; j < ncol; j++) {
        u32 sum = (u32)((s32)M(m, j) * gs16(v, 0)) + (u32)((s32)M(m, 3 + j) * gs16(v, 2));
        if (t != 1) sum += (u32)((s32)M(m, 6 + j) * gs16(v, 4));
        p32(out, 4 * j, shl48_hi((s32)sum, 0, (u16)(2 + s), &cf));
    }
    if (t == 1) {
        u32 x = (u32)(s32)gs16(v, 4);
        for (u16 k = 0; k < s; k++) { cf = (x >> 31) & 1; x <<= 1; }
        p32(out, 8, x);
    }
    return cf;
}

/* PS 3d50:1636 / AP 277a:4ef8 — Σ a[i]·b[i] (i32, no shift) */
s32 vdot_n(const u8 *a, const u8 *b)
{
    return (s32)((u32)((s32)gs16(a, 0) * gs16(b, 0)) + (u32)((s32)gs16(a, 2) * gs16(b, 2)) +
                 (u32)((s32)gs16(a, 4) * gs16(b, 4)));
}
/* PS 3d50:1656 / AP 277a:4f18 */
u32 vdot(u16 a, u16 b) { return (u32)vdot_n(DSP(a), DSP(b)); }

/* ================================================================================================
 * Vectors (render3d_math.md §5.2): vec = i16[3], lvec = i32[3], near pointers
 * ============================================================================================== */

/* PS 3d50:1664 / AP 277a:4f26 */
u16 vneg(u16 v) { for (int i = 0; i < 6; i += 2) DSW(v + i) = (u16)-DSW(v + i); return v; }
/* AP 277a:4f38 */
u16 vadd(u16 a, u16 b) { for (int i = 0; i < 6; i += 2) DSW(a + i) = (u16)(DSW(a + i) + DSW(b + i)); return a; }
/* PS 3d50:1676 / AP 277a:4f58 */
u16 vadd_far(u16 a, FarPtr b)
{
    for (int i = 0; i < 6; i += 2) DSW(a + i) = (u16)(DSW(a + i) + rd16(b.seg, (u16)(b.off + i)));
    return a;
}
/* AP 277a:4f7c */
u16 vsub(u16 a, u16 b) { for (int i = 0; i < 6; i += 2) DSW(a + i) = (u16)(DSW(a + i) - DSW(b + i)); return a; }
/* AP 277a:4f9c (386: count masked to 5 bits) */
u16 vshl(u16 v, s16 n) { for (int i = 0; i < 6; i += 2) DSW(v + i) = (u16)((u32)DSW(v + i) << (n & 31)); return v; }
/* PS 3d50:169a / AP 277a:4fb2 */
u16 vsar(u16 v, u16 n) { for (int i = 0; i < 6; i += 2) DSW(v + i) = (u16)(DSS(v + i) >> (n & 31)); return v; }
/* AP 277a:4fd3 — isqrt32(z² + y² + x²), 32-bit wrap */
u16 vlen(u16 v)
{
    u32 s = (u32)((s32)DSS(v + 4) * DSS(v + 4));
    s += (u32)((s32)DSS(v + 2) * DSS(v + 2));
    s += (u32)((s32)DSS(v) * DSS(v));
    return r3d_isqrt32(s);
}

/* AP 277a:5004 — a += b */
u16 ladd(u16 a, u16 b) { for (int i = 0; i < 12; i += 4) DSL(a + i) += DSL(b + i); return a; }
/* AP 277a:5038 — *a += b (a far) */
void ladd_far(FarPtr a, u16 b)
{
    for (int i = 0; i < 12; i += 4) wr32(a.seg, (u16)(a.off + i), rd32(a.seg, (u16)(a.off + i)) + DSL(b + i));
}
/* AP 277a:5074 — c = a + b */
u16 ladd3(u16 a, u16 b, u16 c) { for (int i = 0; i < 12; i += 4) DSL(c + i) = DSL(b + i) + DSL(a + i); return c; }
/* AP 277a:50c0 — a −= b */
u16 lsub(u16 a, u16 b) { for (int i = 0; i < 12; i += 4) DSL(a + i) -= DSL(b + i); return a; }
/* AP 277a:50f4 — a −= *b (b far) */
u16 lsub_far(u16 a, FarPtr b)
{
    for (int i = 0; i < 12; i += 4) DSL(a + i) -= rd32(b.seg, (u16)(b.off + i));
    return a;
}
/* AP 277a:512e — c = a − b */
u16 lsub3(u16 a, u16 b, u16 c) { for (int i = 0; i < 12; i += 4) DSL(c + i) = DSL(a + i) - DSL(b + i); return c; }
/* AP 277a:517a — a = min(a, b) per component (signed) */
void lmin(u16 a, u16 b) { for (int i = 0; i < 12; i += 4) if (DSSL(b + i) < DSSL(a + i)) DSL(a + i) = DSL(b + i); }
/* AP 277a:51ce — a = max(a, b) */
void lmax(u16 a, u16 b) { for (int i = 0; i < 12; i += 4) if (DSSL(b + i) > DSSL(a + i)) DSL(a + i) = DSL(b + i); }
/* AP 277a:5224 — per component LXLSH@ (0000:1e17), count = the low byte */
u16 lvshl(u16 a, u16 n)
{
    u8 cl = (u8)n;
    for (int i = 0; i < 12; i += 4) DSL(a + i) = cl >= 32 ? 0 : DSL(a + i) << cl;
    return a;
}
/* AP 277a:526a — per component LXRSH@ (0000:1e38, arithmetic) */
void lvsar(u16 a, u16 n)
{
    u8 cl = (u8)n;
    for (int i = 0; i < 12; i += 4) DSSL(a + i) = DSSL(a + i) >> (cl >= 32 ? 31 : cl);
}
/* AP 277a:52c6 — out = a + sext(s) */
void ladd_scalar16(u16 a, u16 s, u16 out)
{
    u32 v = (u32)(s32)(s16)s;
    for (int i = 0; i < 12; i += 4) DSL(out + i) = DSL(a + i) + v;
}
/* AP 277a:530c — a += sext(b) */
u16 ladd_v(u16 a, u16 b) { for (int i = 0; i < 3; i++) DSL(a + 4 * i) += (u32)(s32)DSS(b + 2 * i); return a; }
/* AP 277a:533a — a −= sext(b) */
void lsub_v(u16 a, u16 b) { for (int i = 0; i < 3; i++) DSL(a + 4 * i) -= (u32)(s32)DSS(b + 2 * i); }
/* AP 277a:5368 — out = a + sext(b) */
u16 ladd3_v(u16 a, u16 b, u16 out)
{
    for (int i = 0; i < 3; i++) DSL(out + 4 * i) = (u32)(s32)DSS(b + 2 * i) + DSL(a + 4 * i);
    return out;
}
/* AP 277a:53ae — out = low words of a − b */
void ldiff16(u16 a, u16 b, u16 out)
{
    for (int i = 0; i < 3; i++) DSW(out + 2 * i) = (u16)(DSW(a + 4 * i) - DSW(b + 4 * i));
}
/* PS 3d50:16b0 / AP 277a:53e2 — (a + b) >> 1 per component into the static result, returns it */
u16 lmid(u16 a, u16 b)
{
    u16 r = DS_LMID_RESULT;
    for (int i = 0; i < 12; i += 4) DSSL(r + i) = (s32)(DSL(b + i) + DSL(a + i)) >> 1;
    return r;
}
/* AP 277a:5434 — only x and y are pre-checked against r, then ldist3 <= r */
s16 lnear(u16 a, u16 b, u32 r)
{
    u32 dx = labs32_ge(DSSL(a), DSSL(b));
    if (dx > r) return 0;
    u32 dy = labs32_ge(DSSL(a + 4), DSSL(b + 4));
    if (dy > r) return 0;
    u32 dz = labs32_ge(DSSL(a + 8), DSSL(b + 8));
    return r3d_ldist3v(dx, dy, dz) <= r;
}
/* PS 3d50:1702 / AP 277a:5510 — all |a_i − b_i| <= r (u32 r: the same two stack words) */
u16 lbox(u16 a, u16 b, u32 r)
{
    for (int i = 0; i < 12; i += 4) if (labs32_ge(DSSL(a + i), DSSL(b + i)) > r) return 0;
    return 1;
}
/* AP 277a:5588 — all |a_i| <= r */
s16 linbox(u16 a, u32 r)
{
    for (int i = 0; i < 12; i += 4) {
        u32 v = DSL(a + i);
        if ((s32)v < 0) v = (u32)-v;
        if (v > r) return 0;
    }
    return 1;
}
/* AP 277a:55f0 — out(vec) = low words of in(lvec) */
u16 l2v(u16 out, u16 in) { for (int i = 0; i < 3; i++) DSW(out + 2 * i) = DSW(in + 4 * i); return out; }
/* AP 277a:5612 — out(lvec) = sext(in) */
u16 v2l(u16 out, u16 in) { for (int i = 0; i < 3; i++) DSL(out + 4 * i) = (u32)(s32)DSS(in + 2 * i); return out; }
/* AP 277a:563a — p.x −= mul_sin(r, h); p.y += mul_cos(r, h) (each result's low word, sign-extended) */
u16 lmove(u16 p, s16 r, u16 h)
{
    DSL(p) -= (u32)(s32)(s16)mul_sin(r, h);
    DSL(p + 4) += (u32)(s32)(s16)mul_cos(r, h);
    return p;
}
/* AP 277a:5ea4 — forward unit vector of (pitch, roll, heading) */
u16 dir_from_angles(u16 out, u16 ang)
{
    u16 p = DSW(ang), h = DSW(ang + 4);
    if (p == 0) {
        DSW(out) = (u16)-r3d_sin(h);
        DSW(out + 2) = (u16)r3d_cos(h);
        DSW(out + 4) = 0;
    } else {
        DSW(out + 4) = (u16)r3d_sin(p);
        s16 cp = r3d_cos(p);
        DSW(out) = (u16)mul_sin((s16)-cp, h);
        DSW(out + 2) = (u16)mul_cos(cp, h);
    }
    return out;
}
/* AP 277a:5fae — (−sin h, cos h, 0) */
u16 dir_from_heading(u16 out, u16 h)
{
    DSW(out) = (u16)-r3d_sin(h);
    DSW(out + 2) = (u16)r3d_cos(h);
    DSW(out + 4) = 0;
    return out;
}

/* ================================================================================================
 * proj32 (render3d_math.md §4.8, video_ts.md §3.2): x·f/y and z·f/y through the normalised 32-bit
 * fraction f/y; the original's quirks are kept:
 *  - |y| == |f|: x and z returned (negated when the signs of y and f differ);
 *  - |y| < 10000h: both shifted by 16 by exchanging words (f's high word lands in its low word) and
 *    NOT normalised further; |y| < 1000000h: shifted by 8 with the low byte duplicated, then 1 bit at
 *    a time until bit 31 of |y|;
 *  - high word of |y| <= high word of |f|: x and z returned unchanged (no projection);
 *  - |v|·R >> 32 from the 16×16 partial products without the lowest one.
 * ============================================================================================== */
static u32 proj_mul(u32 v, u16 *q1, u16 *q2)
{
    u16 ax = (u16)v, dx = (u16)(v >> 16);
    if (*q1 == 0 || dx == 0) {
        if (dx == 0) { u16 t = *q2; *q2 = ax; ax = t; t = *q1; *q1 = dx; dx = t; }  /* xchg with the copies */
        u16 t = dx; dx = ax; ax = t;                          /* xchg dx, ax */
        u32 p = (u32)ax * *q2;
        return (u16)(p >> 16);                                /* ax = high word, dx = 0 */
    }
    u32 p1 = (u32)ax * *q1;                                   /* xl·q1 */
    u32 p2 = (u32)dx * *q2;                                   /* xh·q2 */
    u32 mid = (p1 >> 16) + (p2 >> 16) + ((u32)(u16)p1 + (u16)p2 > 0xFFFF);
    return (u32)dx * *q1 + mid;
}

void r3d_proj32(s32 *x, s32 *z, s32 y, s32 f)
{
    u16 scr = DS_PROJ32_SCR;
    DSL(scr + 4) = (u32)y;
    DSL(scr + 12) = (u32)f;
    bool sgn = ((u32)y ^ (u32)f) >> 31;
    u32 ay = y < 0 ? (u32)-(u32)y : (u32)y, af = f < 0 ? (u32)-(u32)f : (u32)f;
    if (ay == af) {
        if (sgn) { *x = (s32)-(u32)*x; *z = (s32)-(u32)*z; }
        return;
    }
    u16 dx = (u16)(ay >> 16), ax = (u16)ay, cx = (u16)(af >> 16), bx = (u16)af;
    if (dx == 0) {
        dx = ax; ax = 0;                                      /* xchg dx, ax */
        u16 t = cx; cx = bx; bx = t;                          /* xchg cx, bx */
    } else {
        if ((dx >> 8) == 0) {                                 /* byte moves, low bytes kept */
            cx = (u16)(cx << 8 | bx >> 8); bx = (u16)(bx << 8 | (bx & 0xFF));
            dx = (u16)(dx << 8 | ax >> 8); ax = (u16)(ax << 8 | (ax & 0xFF));
        }
        while (!(dx & 0x8000)) {
            cx = (u16)(cx << 1 | bx >> 15); bx = (u16)(bx << 1);
            dx = (u16)(dx << 1 | ax >> 15); ax = (u16)(ax << 1);
        }
    }
    u16 yh = dx, fh = cx, fl = bx;
    if (yh <= fh) return;                                     /* no projection */
    u32 num = (u32)fh << 16 | fl;
    u16 q1 = (u16)(num / yh), rem = (u16)(num % yh);
    u16 q2 = (u16)(((u32)rem << 16) / yh);
    u16 xq1 = q1, xq2 = q2, zq1 = q1, zq2 = q2;
    u32 v = (u32)*x;
    DSL(scr) = v;
    if (v != 0) {
        u32 av = (s32)v < 0 ? (u32)-v : v;
        u32 r = proj_mul(av, &xq1, &xq2);
        if (((v >> 16) ^ (sgn ? 0x8000 : 0)) & 0x8000) r = (u32)-r;
        *x = (s32)r;
    }
    v = (u32)*z;
    DSL(scr + 8) = v;
    if (v != 0) {
        u32 av = (s32)v < 0 ? (u32)-v : v;
        u32 r = proj_mul(av, &zq1, &zq2);
        if (((v >> 16) ^ (sgn ? 0x8000 : 0)) & 0x8000) r = (u32)-r;
        *z = (s32)r;
    }
}
/* PS 3d50:032a / AP 277a:3f6e — render3d_math.md §4.8 (stack-image interface → pointers) */
void proj32(s32 *x, s32 *z, s32 y, s32 f) { r3d_proj32(x, z, y, f); }
