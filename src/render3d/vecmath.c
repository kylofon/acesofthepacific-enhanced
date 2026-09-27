/* render3d — small C helpers around the 3Space core (AP 35ff, 3604, 3609, 3611, 3614, 361d, 3624,
 * 3629, 365a, 3662, 3666, 3678, 367b, 3684, 4edb, 5137, 5b91). render3d_math.md §5.3 (details from
 * the disassembly where the spec differs: dir_scaled adds to an lvec, angles_to uses from − to for the
 * heading, lmove_dir sets p, 3662:0002 invalidates the matrix cache). */
#include "render3d/r3d_priv.h"
#include "sim_flow/sim_flow.h"

/* AP 35ff:0009 */
s16 clamp(s16 v, s16 lo, s16 hi) { return v < lo ? lo : v > hi ? hi : v; }
/* AP 35ff:0029 */
u8 in_range(s16 v, s16 lo, s16 hi) { return (v < lo || v > hi) ? 0 : 1; }
/* AP 35ff:0041 — heading of (dx, dy): 0 = north, CCW */
u16 bearing32(s32 dx, s32 dy) { return (u16)(r3d_atan2_32(dx, dy) + 0xC000); }

/* AP 3604:0009 — v[i] = (v[i]·s) >> 14 (386) */
u16 vscale(u16 v, s16 s)
{
    for (int i = 0; i < 6; i += 2) DSW(v + i) = (u16)(((s32)DSS(v + i) * s) >> 14);
    return v;
}
/* AP 3609:0001 */
u16 vrot_z(u16 v, u16 a)
{
    s32 c = r3d_cos(a), s = r3d_sin(a);
    s32 x = DSS(v), y = DSS(v + 2);
    s16 nx = (s16)((s32)((u32)(x * c) + (u32)(y * (s32)(s16)-(s16)s)) >> 14);
    s16 ny = (s16)((s32)((u32)(x * s) + (u32)(y * c)) >> 14);
    DSS(v) = nx; DSS(v + 2) = ny;
    return v;
}
/* AP 3611:0007 — out (lvec) += vscale(dir_from_angles(ang), s) */
void dir_scaled(u16 out, u16 ang, u16 s)
{
    u16 m = stk_mark(), t = stk_alloc(6);
    dir_from_angles(t, ang);
    vscale(t, (s16)s);
    ladd_v(out, t);
    stk_release(m);
}
/* AP 3614:0009 — out = (pitch, 0, heading) of the vector between two points; returns the horizontal
 * distance (heading from from − to, pitch from to.z − from.z against the distance) */
u32 angles_to(u16 out, u16 from, u16 to)
{
    s32 dy = (s32)(DSL(from + 4) - DSL(to + 4));
    s32 dx = (s32)(DSL(from) - DSL(to));
    DSW(out + 4) = bearing32(dx, dy);
    u32 d = r3d_ldist2v((u32)(dx < 0 ? -dx : dx), (u32)(dy < 0 ? -dy : dy));
    DSW(out) = bearing32((s32)(DSL(to + 8) - DSL(from + 8)), (s32)d);
    DSW(out + 2) = 0;
    return d;
}
/* AP 361d:000b — p = dir(ang)·dist (p zeroed first, 4000h at a time, then the scaled remainder) */
u16 lmove_dir(u16 p, u16 ang, u32 dist)
{
    u16 m = stk_mark(), t = stk_alloc(6);
    dir_from_angles(t, ang);
    memset(DSP(p), 0, 12);
    s32 d = (s32)dist;
    while (d > 0x4000) { ladd_v(p, t); d -= 0x4000; }
    vscale(t, (s16)d);
    ladd_v(p, t);
    stk_release(m);
    return p;
}
/* AP 3624:0000 — p.x −= (r·sin h) >> 14; p.y += (r·cos h) >> 14 (386) */
void lmove_heading(u16 p, u16 h, u16 r)
{
    DSSL(p) -= ((s32)(s16)r * r3d_sin(h)) >> 14;
    DSSL(p + 4) += ((s32)(s16)r * r3d_cos(h)) >> 14;
}
/* AP 3629:0004 */
u16 bearing_between(u16 a, u16 b) { return bearing32((s32)(DSL(a) - DSL(b)), (s32)(DSL(a + 4) - DSL(b + 4))); }

/* ---- matrix cache of an object record (BX = the cache: +00 matrix, +13h transposed copy, +26h flags,
 * +28h pointer to the angles) ----------------------------------------------------------------------- */
/* AP 365a:000c (header fix: one register argument) */
u16 obj_matrix(u16 bx)
{
    if (!(DSB(bx + 0x26) & 1)) {
        mat_from_angles(DSW(bx + 0x28), bx);
        DSB(bx + 0x26) |= 1;
    }
    return bx;
}
/* AP 365a:0032 */
u16 obj_matrix_t(u16 bx)
{
    if (!(DSB(bx + 0x26) & 2)) {
        memmove(DSP(bx + 0x13), DSP(obj_matrix(bx)), 0x13);            /* SCOPY@ */
        mat_transpose((u16)(bx + 0x13));
        DSB(bx + 0x26) |= 2;
    }
    return (u16)(bx + 0x13);
}
/* AP 365a:0074 */
void obj_set_angles(u16 bx, u16 ax) { DSB(bx + 0x26) = 0; DSW(bx + 0x28) = ax; }
/* AP 3662:0002 (named nop_3662 in the index; it clears the cache flags) */
void nop_3662(u16 bx) { DSB(bx + 0x26) = 0; }
/* AP 3662:000d — v[i] *= s (32-bit) */
u16 lscale(u16 v, u16 s)
{
    for (int i = 0; i < 12; i += 4) DSL(v + i) = (u32)((s64)DSSL(v + i) * (s16)s);
    return v;
}
/* AP 3666:0009 — out[i] = in[i] >> n */
u16 l2v_sar(u16 out, u16 in, u16 n)
{
    for (int i = 0; i < 3; i++) DSW(out + 2 * i) = (u16)(DSSL(in + 4 * i) >> (n & 31));
    return out;
}
/* AP 3666:003b — out[i] = (i32)in[i] << n */
u16 v2l_shl(u16 out, u16 in, u16 n)
{
    for (int i = 0; i < 3; i++) DSL(out + 4 * i) = (u32)(s32)DSS(in + 2 * i) << (n & 31);
    return out;
}
/* AP 3666:0073 — rotation of a long vector at reduced precision */
u16 lrotate(u16 v, u16 m, u16 out)
{
    u16 mk = stk_mark(), zero = stk_alloc(12), t6 = stk_alloc(6), tc = stk_alloc(6);
    memset(DSP(zero), 0, 12);
    s32 d = (s32)ldist3(zero, v);
    s16 e = (s16)(lbits((u32)(d + (d >> 1))) - 15);
    if (e > 0) {
        vec_rotate(l2v_sar(t6, v, (u16)e), m, tc);
        v2l_shl(out, tc, (u16)e);
    } else {
        vec_rotate(l2v(t6, v), m, tc);
        v2l(out, tc);
    }
    stk_release(mk);
    return out;
}
/* AP 3678:0004 — out = pos + vec_rotate(v, obj_matrix(cache)) */
void obj_to_world(u16 out, u16 pos, u16 v, u16 cache)
{
    u16 m = stk_mark(), t = stk_alloc(6);
    vec_rotate(v, obj_matrix(cache), t);
    ladd3_v(pos, t, out);
    stk_release(m);
}
/* AP 367b:0006 — rotate the x/y of a far lvec about z (macro members, 34f1:0155) */
void lrot_z(FarPtr v, u16 a)
{
    s32 c = r3d_cos(a), s = r3d_sin(a);
    s32 x = (s32)rd32(v.seg, v.off), y = (s32)rd32(v.seg, (u16)(v.off + 4));
    s32 nx = (s32)((u32)x * (u32)c + (u32)y * (u32)(s32)(s16)-(s16)s) >> 14;
    s32 ny = (s32)((u32)x * (u32)s + (u32)y * (u32)c) >> 14;
    wr32(v.seg, v.off, (u32)nx);
    wr32(v.seg, (u16)(v.off + 4), (u32)ny);
}
/* AP 3684:000c */
u32 vdist(u16 a, u16 b)
{
    u16 m = stk_mark(), la = stk_alloc(12), lb = stk_alloc(12);
    v2l(la, a); v2l(lb, b);
    u32 d = ldist3(la, lb);
    stk_release(m);
    return d;
}
/* AP 3684:0042 — bit i when component i lies in [lo_i, hi_i] */
u16 vec_outside_mask(u16 v, u16 lo, u16 hi)
{
    u16 r = in_range(DSS(v), DSS(lo), DSS(hi));
    r = (u16)(r + (in_range(DSS(v + 2), DSS(lo + 2), DSS(hi + 2)) << 1));
    r = (u16)(r + (in_range(DSS(v + 4), DSS(lo + 4), DSS(hi + 4)) << 2));
    return r;
}
/* AP 3684:00a1 */
u16 lbits(u32 v) { return v ? (u16)(lbitlen(v) + 1) : 0; }

/* AP 4edb:0000 — out = −in; 4edb:0030 empty */
u16 lneg(u16 out, u16 in) { for (int i = 0; i < 12; i += 4) DSL(out + i) = (u32)-DSL(in + i); return out; }
void nop_4edb(void) { }

/* AP 5137:0000 — out[i] = steps_to_frames(in[i]) */
void vec_map_timebase(u16 in, u16 out)
{
    for (int i = 0; i < 3; i++) DSW(out + 2 * i) = (u16)steps_to_frames(DSS(in + 2 * i));
}
/* AP 5137:0034 — ldiv_sat((i32)a << 10, b) */
s16 ratio10(s16 a, s16 b) { return ldiv_sat((s32)((u32)(s32)a << 10), b); }
/* AP 5137:004f */
void vratio10(u16 v, u16 b, u16 out)
{
    for (int i = 0; i < 3; i++) DSS(out + 2 * i) = ratio10(DSS(v + 2 * i), (s16)b);
}
/* AP 5137:0084 — far lvec out = sext(in) */
FarPtr v2l_c(FarPtr out, u16 in)
{
    for (int i = 0; i < 3; i++) wr32(out.seg, (u16)(out.off + 4 * i), (u32)(s32)DSS(in + 2 * i));
    return out;
}
/* AP 5137:00b3 — far lvec a += sext(b) */
void ladd_v_c(FarPtr a, u16 b)
{
    for (int i = 0; i < 3; i++) {
        u16 o = (u16)(a.off + 4 * i);
        wr32(a.seg, o, rd32(a.seg, o) + (u32)(s32)DSS(b + 2 * i));
    }
}
/* AP 5137:00e2 — the forward vector of far angles into a far vec */
void dir_from_angles_far(FarPtr out, u32 ang)
{
    FarPtr a = { (u16)ang, (u16)(ang >> 16) };
    u16 m = stk_mark(), la = stk_alloc(6), t = stk_alloc(6);
    for (int i = 0; i < 6; i += 2) DSW(la + i) = rd16(a.seg, (u16)(a.off + i));
    dir_from_angles(t, la);
    for (int i = 0; i < 6; i += 2) wr16(out.seg, (u16)(out.off + i), DSW(t + i));
    stk_release(m);
}
/* AP 5137:0128 — v[i] = dt_scale(v[i]) */
void vec_dt(u16 v) { for (int i = 0; i < 6; i += 2) DSW(v + i) = (u16)dt_scale(DSS(v + i)); }
/* AP 5137:015c — lvec x/y rotation about z */
void lrot_z2(u16 v, u16 a)
{
    s32 c = r3d_cos(a), s = r3d_sin(a);
    s32 x = DSSL(v), y = DSSL(v + 4);
    s32 nx = (s32)((u32)x * (u32)c - (u32)y * (u32)s) >> 14;
    s32 ny = (s32)((u32)x * (u32)s + (u32)y * (u32)c) >> 14;
    DSSL(v) = nx; DSSL(v + 4) = ny;
}
/* AP 5b91:0000 — ldiv_sat(v, 4000); 5b91:0014 empty */
u16 per4000(u32 v) { return (u16)ldiv_sat((s32)v, 4000); }
void nop_5b91(void) { }
