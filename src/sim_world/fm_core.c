/* sim_world — flight model core (AP segment 0b01, sim_world_fm.md §2, §3).
 *
 * fm_full (player model) and fm_simple (AI model) integrate one step of an aircraft: control surfaces
 * -> angular rates -> orientation matrix -> AoA / sideslip -> forces -> body velocity (10-bit
 * fractional carries) -> world velocity -> position (trapezoid) -> ground contact. Ported from the
 * disassembly (x86dis 0b01:0134 5932 / 0b01:1d33 4201); every product/shift keeps the original width.
 * All pointers are DGROUP offsets: fm = FmState (Plane+64), ctl = Controls (Plane+AC), P = posture
 * (Plane+02), F = the (copied) .FMD. Locals whose address the original passes to the 277a matrix
 * routines live on the emulated DGROUP stack. */
#include "sim_world/swa_priv.h"

/* FmState words */
#define FM_P      0x00
#define FM_FMD    0x02
#define FM_MAT    0x04
#define FM_ALPHA  0x16
#define FM_ROLLR  0x12
#define FM_PITCHR 0x14
#define FM_LIFT   0x1A
#define FM_SIDE   0x1C
#define FM_YAWR   0x2A
#define FM_YAWC   0x2C
#define FM_VB     0x2E          /* body velocity lat, fwd, vert */
#define FM_VW     0x34          /* world velocity x, y, z */
#define FM_FLAGS  0x46
/* posture */
#define P_Z       0x0A
#define P_PITCH   0x0E
#define P_ROLL    0x10
#define P_HEAD    0x12

/* band interpolation: the altitude's low 16 bits against the band width (sim_world_fm.md §3.2 quirk) */
static s16 band_interp(u16 P, s16 band, u16 tbl)
{
    s16 w = (s16)(DSW((u16)(0x1446 + band * 4)) - DSW((u16)(0x1442 + band * 4)));
    return lerp_map(DSS(P + P_Z), 0, w, DSS(tbl), DSS((u16)(tbl + 2)));
}
static s16 band_interp6(u16 P, s16 band, u16 tbl)   /* drag triples: entries 6 bytes apart */
{
    s16 w = (s16)(DSW((u16)(0x1446 + band * 4)) - DSW((u16)(0x1442 + band * 4)));
    return lerp_map(DSS(P + P_Z), 0, w, DSS(tbl), DSS((u16)(tbl + 6)));
}

/* AP 0b01:000a — sim_world_fm.md §3.5 */
void fm_module_init(void) {}

/* AP 0b01:000f */
void fm_reset_globals(void)
{
    DSW(0x576B) = 0; DSW(0x5769) = 0; DSW(0x5767) = 0; DSW(0x5765) = 0;
    DSB(0x5764) = 0;
}

/* AP 0b01:1b78 — number of band breakpoints DS:1446[k] <= z (k < 8) */
s16 alt_band(s32 z)
{
    s16 n = 0;
    for (u16 si = 0x1446; si != 0x1466; si += 4, n++)
        if (DSSL(si) > z) break;
    return n;
}

/* AP 0b01:0031 — body forward speed where thrust 4k/256 equals the forward drag */
void fm_set_cruise(u16 fm, u16 fmd, s16 k, s32 alt)
{
    s16 cx = alt_band(alt);
    if (cx >= 8) cx--;
    u16 bx = (u16)(fmd + cx * 6);
    s16 w = (s16)(DSW((u16)(0x1446 + cx * 4)) - DSW((u16)(0x1442 + cx * 4)));
    s16 d = lerp_map((s16)alt, 0, w, DSS(bx + 0x2C), DSS(bx + 0x32));
    if (d != 0)
        OS(fm, 0x30) = ldiv_sat((s32)(s16)(k << 2) * OS(fmd, 0x28), d);
    vec_rotate((u16)(fm + FM_VB), OW(fm, FM_MAT), (u16)(fm + FM_VW));
    fm_per_frame_velocity(fm);
}

/* AP 0b01:00d0 — fm_init(FmState*, Posture*, Fmd*, Matrix*, int k) */
void fm_init(u16 fm, u16 P, u16 fmd, u16 mat, u16 k)
{
    memset(DSP(fm), 0, 0x48);
    OW(fm, FM_P) = P;
    OW(fm, FM_FMD) = fmd;
    OW(fm, FM_MAT) = mat;
    OW(fm, FM_SIDE) = 0x8000;
    OW(fm, FM_LIFT) = 0x8000;
    mat_from_angles((u16)(P + P_PITCH), OW(fm, FM_MAT));
    OW(fm, 0x40) = 0x400;
    OW(fm, 0x42) = 0x400;
    fm_set_cruise(fm, fmd, (s16)k, OSL(P, P_Z));
}

/* AP 0b01:19ff — copies 4 x 6 bytes DS:028E -> DS:02AE (no caller) */
void copy_vec4(void)
{
    for (u16 a = 0; a != 0x18; a += 6) memmove(DSP(0x2AE + a), DSP(0x28E + a), 6);
}

/* AP 0b01:1a28 — level speed at full throttle at altitude z */
u16 fm_cruise_speed(u16 fmd, u32 alt)
{
    s16 cx = alt_band((s32)alt);
    if (cx >= 8) cx--;
    u16 bx = (u16)(fmd + cx * 6);
    s16 w = (s16)(DSW((u16)(0x1446 + cx * 4)) - DSW((u16)(0x1442 + cx * 4)));
    s16 d = lerp_map((s16)alt, 0, w, DSS(bx + 0x2C), DSS(bx + 0x32));
    return (u16)ldiv_sat((s32)OS(fmd, 0x28) << 10, d);
}

/* AP 0b01:1a95 */
u16 fm_cruise_speed_here(u16 fm)
{
    return fm_cruise_speed(OW(fm, FM_FMD), OL(OW(fm, FM_P), P_Z));
}

/* AP 0b01:1c22 — fraction accumulator with carry */
u16 frac_carry(u16 acc, u16 add)
{
    s16 dx = 0;
    DSW(acc) = (u16)(DSW(acc) + add);
    if (DSS(acc) > 0x3FF) { dx = 1; DSW(acc) = (u16)(DSW(acc) - 0x400); }
    else if (DSS(acc) < (s16)0xFC01) { dx = -1; DSW(acc) = (u16)(DSW(acc) + 0x400); }
    return (u16)dx;
}

/* AP 0b01:1aaf — drag of one axis: -(v·min(k,800) >> 10) with the 10-bit remainder carried */
s16 fm_drag(s16 v, s16 k, u16 frac)
{
    if (k > 0x320) k = 0x320;
    s32 p = (s32)v * k;
    s32 a = p < 0 ? -p : p;
    s16 dx = (s16)((u16)a & 0x3FF);
    s16 si = (s16)(a >> 10);
    if (p < 0) { dx = (s16)-dx; si = (s16)-si; }
    si = (s16)(si + (s16)frac_carry(frac, (u16)dx));
    return (s16)-si;
}

/* AP 0b01:1b14 — clamp to [-m, m] */
s16 ap_clamp_sym_0b01(s16 x, s16 m)
{
    s16 nm = (s16)-m;
    if (nm > x) return nm;
    if (x > m) return m;
    return x;
}

/* AP 0b01:1b3b — b - a limited to [-m, m] */
s16 step_toward(s16 a, s16 b, s16 m)
{
    s16 d = (s16)(b - a);
    if (a < b) return d < m ? d : m;
    s16 nm = (s16)-m;
    return d > nm ? d : nm;
}

/* AP 0b01:1b9b — v[i] += dt'(f[i]) + carry(frac[i]); f[] is scaled in place (362b:0256) */
void fm_integrate_vel(u16 v, u16 frac, u16 f)
{
    u16 m = stk_mark(), tmp = stk_alloc(2);
    for (int i = 0; i < 6; i += 2) {
        dt_scale_frac((u16)(f + i), tmp);
        s16 c = (s16)frac_carry((u16)(frac + i), DSW(tmp));
        DSW(v + i) = (u16)(DSW(v + i) + (u16)(DSS(f + i) + c));
    }
    stk_release(m);
}

/* AP 0b01:1c4f — pos (i32) += v·DS:3F22 >> 10 with the 10-bit remainder carried */
void pos_step(u16 pos, u16 frac, s16 v)
{
    s32 p = (s32)v * (s32)DSS(0x3F22);
    s32 a = p < 0 ? -p : p;
    s16 dx = (s16)((u16)a & 0x3FF);
    if (v < 0) dx = (s16)-dx;
    s16 si = (s16)(p >> 10);
    s16 c = (s16)frac_carry(frac, (u16)dx);
    DSSL(pos) += (s32)(s16)(si + c);
}

/* AP 0b01:1cb5 — per-nominal-frame copies of the body and world velocities */
void fm_per_frame_velocity(u16 fm)
{
    s32 k = DSS(0x3F22);
    for (int i = 0; i < 12; i += 2)
        OW(fm, 6 + i) = (u16)(((s32)OS(fm, FM_VB + i) * k) >> 10);
}

/* rate approach of fm_full: rate -> target with acceleration a_same / a_back scaled by the error */
static void rate_approach(u16 rate_ds, s16 target, s16 a_same, s16 a_back, s16 maxr)
{
    s16 r = DSS(rate_ds);
    if (r == target) return;
    s16 a;
    if (r < target) a = r >= 0 ? a_same : a_back;
    else a = (s16)-(r > 0 ? a_back : a_same);
    s16 s = abs16(ldiv_sat((s32)(s16)(target - r) << 8, maxr));
    if (s <= 0x20) s = 0x20;
    if (s >= 0x100) s = 0x100;
    a = mulsh(a, s, 8);
    DSW(rate_ds) = (u16)(DSW(rate_ds) + (u16)dt_scale(a));
    r = DSS(rate_ds);
    if ((r < target && a < 0) || (r > target && a > 0)) DSS(rate_ds) = target;
}

/* AP 0b01:0134 — fm_full(FmState*, Controls*) — sim_world_fm.md §3.2 */
void fm_full(u16 fm, u16 ctl)
{
    u16 mk = stk_mark();
    u16 Mt   = stk_alloc(0x14);        /* bp-7C: transposed matrix M' (19 bytes) */
    u16 Md   = stk_alloc(0x14);        /* bp-68: delta matrix */
    u16 vb   = stk_alloc(6);           /* bp-16: body velocity lat, fwd, vert */
    u16 frc  = stk_alloc(6);           /* bp-1C: forces Fx (lat), Fy (fwd), Fz (vert) */
    u16 ang  = stk_alloc(6);           /* bp-0A: angles / gravity vector */
    u16 grav = stk_alloc(6);           /* bp-10: gravity in body axes */

    u16 F = OW(fm, FM_FMD);
    u16 P = OW(fm, FM_P);
    u16 old_pitch = OW(P, P_PITCH), old_roll = OW(P, P_ROLL), old_head = OW(P, P_HEAD);
    s16 c1 = OS(ctl, 2);                               /* elevator */
    s16 c0 = OS(ctl, 0);                               /* aileron */
    s16 c2 = (s16)-OS(ctl, 4);                         /* -rudder */
    s16 flaps = (s16)(OB(ctl, 0x0E) & 3);
    s16 yaw_s = 0, pitch_s = 0, roll_s = 0, yaw_in = 0, lat_acc = 0;
    s16 vw_old0 = OS(fm, FM_VW), vw_old1 = OS(fm, FM_VW + 2), vw_old2 = OS(fm, FM_VW + 4);
    DSW(frc + 4) = 0; DSW(frc + 2) = 0; DSW(frc) = 0;

    s16 alpha = (s16)atan2_16(OW(fm, FM_VB + 2), (u16)-OS(fm, FM_VB + 4));
    OW(fm, FM_ALPHA) = (u16)alpha;
    s16 beta = (s16)atan2_16(OW(fm, FM_VB + 2), OW(fm, FM_VB));
    if (abs16(beta) < 0xB6) beta = 0;
    s16 gnd = (s16)(OS(F, 0x98) + OS(ctl, 0x0C));
    s16 on_ground = (s32)gnd >= OSL(P, P_Z);
    s16 band = alt_band(OSL(P, P_Z));
    if (band >= 8) band--;

    /* 0. elevator shaping */
    if (c1 < 0 && abs16(OS(P, P_PITCH)) > 0x1000) {
        c1 >>= 1;
    } else if (OS(F, 0xBE) != 0 && c1 > 0) {
        s16 a = (s16)(abs16(OS(P, P_PITCH)) >> 3);
        if (a >= 0x400) a = 0x400;
        s16 di = mulsh(OS(F, 0xBE), (s16)(0x400 - a), 10);
        s16 r = abs16(OS(P, P_ROLL));
        if (r > 0x4000) r = (s16)(0x7FFF - r);
        r >>= 4;
        di = mulsh(di, r, 10);
        di = (s16)(((s32)di * 600) >> 10);
        c1 = (s16)(c1 - mulsh(c1, di, 10));
    }

    /* 1. control effectiveness */
    s32 e = band_interp(P, band, (u16)(F + 0x62 + band * 2));
    c0 = (s16)(((s32)c0 * e) >> 10);
    c1 = (s16)(((s32)c1 * e) >> 10);
    c2 = (s16)(((s32)c2 * e) >> 10);
    if (OS(F, 0xC6) != 0 && OS(fm, FM_VB + 2) > OS(F, 0xC6)) c0 >>= 2;
    if (OS(F, 0xC0) != 0 && OS(fm, FM_VB + 2) > OS(F, 0xC0)) { c0 >>= 2; c1 >>= 2; c2 >>= 2; }
    c0 = ap_clamp_sym_0b01(c0, 0x100);
    c1 = ap_clamp_sym_0b01(c1, 0x100);

    /* 2. roll rate and roll levelling */
    if (!(OB(fm, FM_FLAGS) & 1) && !on_ground) {
        s16 b = mulsh(c0, OS(F, 0x82), 10);
        if (c0 <= 0) b = (s16)-b;
        s16 target = mulsh((s16)(c0 + b), OS(F, 0x7C), 8);
        rate_approach((u16)(fm + FM_ROLLR), target, OS(F, 0x74), OS(F, 0x76), OS(F, 0x7C));
        if (OS(F, 0x86) != 0 && abs16(OS(P, P_PITCH)) < 0x2000) {
            s16 roll = OS(P, P_ROLL);
            s16 ls = roll > 0 ? (s16)-OS(F, 0x86) : OS(F, 0x86);
            ls = dt_scale(ls);
            if (OS(P, P_ROLL) > 0) {
                s16 lim = (s16)-OS(P, P_ROLL);
                ls = lim >= ls ? lim : ls;
            } else {
                s16 lim = (s16)-(s16)(OS(P, P_ROLL) + 1);
                ls = lim <= ls ? lim : ls;
            }
            if (abs16(ls) < 10) ls = 0;
            roll_s = ls;
        }
    }

    /* 3. pitch rate */
    if (!on_ground || OS(fm, FM_VB + 2) > OS(F, 0xAA)) {
        s16 elev = 0, corr;
        s16 aa = abs16(alpha);
        if (aa < 12000) {
            elev = c1;
            s16 k = 0x4B0;
            s16 r = fold16(OS(P, P_ROLL));
            if (r > 0x800) k = (s16)(k - (r >> 3));
            k = k > 0 ? k : 0;
            if (on_ground) k = 0;
            if (aa < k) corr = 0;
            else corr = (s16)(alpha - (alpha > 0 ? k : (s16)-k));
        } else {
            corr = (s16)(0x7FFF - aa);
            if (corr <= 200) corr = 200;
            if (alpha < 0) corr = (s16)-corr;
            elev = 0;
        }
        if (!(OB(fm, FM_FLAGS) & 1)) corr = mulsh(corr, OS(F, 0x84), 10);
        elev = (s16)(elev - (corr >> 2));
        elev = ap_clamp_sym_0b01(elev, 0x200);
        s16 target = mulsh(elev, OS(F, 0x7E), 8);
        rate_approach((u16)(fm + FM_PITCHR), target, OS(F, 0x78), OS(F, 0x7A), OS(F, 0x7E));
    }

    /* 4. roll -> yaw coupling */
    if (!(OB(fm, FM_FLAGS) & 1) && !on_ground && OS(F, 0xB6) != 0) {
        s16 p = abs16(OS(P, P_PITCH));
        p = (s16)((p >> 1) + (p >> 3));
        if (p < 0x2000) {
            s16 r = fold16(OS(P, P_ROLL));
            r = (s16)(r + (r >> 1));
            r = r < 0x4000 ? r : 0x4000;
            s16 k = sin16((u16)r);
            k = mulsh(k, (s16)(0x2000 - p), 14);
            s16 coef = OS(P, P_ROLL) > 0 ? (s16)-OS(F, 0xB6) : OS(F, 0xB8);
            if (k > 0) {
                lat_acc = mulsh(coef, k, 13);
                lat_acc = (s16)(((s32)lat_acc * 800) >> 10);
            }
        }
    }

    /* 5. rudder: stick steering on the ground, prop torque, bank fade */
    if (c2 == 0 && on_ground) c2 = (s16)-c0;
    if (!(OB(fm, FM_FLAGS) & 4) && OS(fm, 0x44) != 0)
        c2 = (s16)(c2 + mulsh(OS(fm, 0x44), OS(ctl, 6), 8));
    c2 = (s16)(((s32)c2 * 500) >> 10);
    {
        s16 r = abs16(OS(P, P_ROLL));
        r = r < 0x2000 ? r : 0x2000;
        s16 f = (s16)(0x2000 - r);
        c2 = (s16)(c2 + mulsh(c2, f, 14));
    }
    c2 = ap_clamp_sym_0b01(c2, 0x100);

    /* 6. weathervane yaw from sideslip */
    {
        s16 fr = fold16(OS(P, P_ROLL));
        s16 y;
        if (abs16(beta) < 12000) {
            s16 b = beta;
            if (OS(F, 0x88) != 0 && !(OB(fm, FM_FLAGS) & 1)) {
                if (OS(F, 0x88) < fr) b = 0;
                else b = ldiv_sat((s32)beta * (s16)(OS(F, 0x88) - fr), OS(F, 0x88));
            }
            s16 si = on_ground ? (s16)(c2 << 1) : c2;
            y = (s16)(b - (s16)(si << 4));
        } else {
            s16 cx = (s16)(0x7FFF - abs16(beta));
            if (cx <= 0x32) cx = 0x32;
            if (cx >= 0xBB8) cx = 0xBB8;
            cx = (s16)(cx << 1);
            if (beta < 0) cx = (s16)-cx;
            y = cx;
        }
        s16 yaw = mulsh(OS(F, 0x80), (s16)-y, 10);
        s16 cx = yaw;
        if (on_ground && yaw != 0) {
            s16 di = (s16)(OS(F, 0xAA) >> 2);
            if (OS(fm, FM_VB + 2) < di) cx = ldiv_sat((s32)yaw * OS(fm, FM_VB + 2), di);
        }
        if (on_ground || (abs16(OS(P, P_PITCH)) < 0x800 && abs16(OS(P, P_ROLL)) < 0x800))
            lat_acc = (s16)(lat_acc + cx);
        else
            yaw_in = cx;
    }

    /* 7. torque yaw */
    if (OS(ctl, 6) != 0) {
        s16 di = 0;
        if (lat_acc != 0) di = abs16(mulsh(lat_acc, fold16(OS(P, P_ROLL)), 14));
        di = (s16)(di + OS(fm, FM_PITCHR));
        if (OS(F, 0x8A) != 0 && di != 0) {
            s16 si = mulsh(OS(F, 0x8A), OS(ctl, 6), 8);
            yaw_s = mulsh(di, si, 10);
        }
    }

    /* 8. on the ground: roll and pitch return to the ground attitude */
    if (on_ground) {
        OB(fm, FM_FLAGS) &= 0xFE;
        s16 d = step_toward(OS(P, P_ROLL), 0, OS(F, 0xA8));
        if (abs16(d) < OS(F, 0xBC)) OW(fm, FM_ROLLR) = 0;
        else OW(fm, FM_ROLLR) = (u16)(OW(fm, FM_ROLLR) +
                                      (u16)dt_scale(step_toward(OS(fm, FM_ROLLR), d, (s16)(OS(F, 0xA8) >> 2))));
        if (OS(fm, FM_VB + 2) <= OS(F, 0xAA)) {
            d = step_toward(OS(P, P_PITCH), OS(F, 0x9A), OS(F, 0xA8));
            if (d == 0) OW(fm, FM_PITCHR) = 0;
            else OW(fm, FM_PITCHR) = (u16)(OW(fm, FM_PITCHR) +
                                           (u16)dt_scale(step_toward(OS(fm, FM_PITCHR), d, (s16)(OS(F, 0xA8) >> 2))));
        }
    }

    /* 9. rotate */
    if (OS(fm, FM_ROLLR) != 0 || OS(fm, FM_PITCHR) != 0 || yaw_in != 0 || pitch_s != 0 || yaw_s != 0 ||
        roll_s != 0) {
        DSW(ang) = (u16)dt_scale((s16)(OS(fm, FM_PITCHR) + pitch_s));
        DSW(ang + 2) = (u16)(dt_scale(OS(fm, FM_ROLLR)) + roll_s);
        DSW(ang + 4) = (u16)dt_scale((s16)(yaw_in + yaw_s));
        mat_from_angles(ang, Md);
        mat_mul(Md, OW(fm, FM_MAT), Mt);
        mat_to_angles(Mt, (u16)(P + P_PITCH));
    }
    if (OS(fm, FM_ROLLR) == 0 && OS(fm, FM_PITCHR) == 0 && yaw_in == 0 && !(OB(fm, FM_FLAGS) & 1) &&
        abs16(OS(P, P_PITCH)) < 0xE38 && abs16(OS(P, P_ROLL)) < OS(F, 0xBC))
        OW(P, P_ROLL) = 0;

    /* 10. heading */
    if (abs16(lat_acc) > abs16(OS(fm, FM_YAWR)))
        lat_acc = (s16)((s16)((u16)(OS(fm, FM_YAWR) << 2) - OW(fm, FM_YAWR) + (u16)lat_acc) >> 2);
    OS(fm, FM_YAWR) = lat_acc;
    OW(P, P_HEAD) = (u16)(OW(P, P_HEAD) + (u16)dt_scale(lat_acc));
    if (on_ground && OS(P, P_PITCH) > OS(F, 0x9A)) {
        OW(P, P_PITCH) = OW(F, 0x9A);
        OW(fm, FM_PITCHR) = 0;
    }

    /* 11. matrix and body velocity */
    if ((OW(P, P_PITCH) & 0xFFF0) == (old_pitch & 0xFFF0) && (OW(P, P_ROLL) & 0xFFF0) == (old_roll & 0xFFF0) &&
        (OW(P, P_HEAD) & 0xFFF0) == (old_head & 0xFFF0)) {
        DSW(vb) = OW(fm, FM_VB); DSW(vb + 2) = OW(fm, FM_VB + 2); DSW(vb + 4) = OW(fm, FM_VB + 4);
        memmove(DSP(Mt), DSP(OW(fm, FM_MAT)), 19);
        mat_transpose(Mt);
    } else {
        mat_from_angles((u16)(P + P_PITCH), OW(fm, FM_MAT));
        memmove(DSP(Mt), DSP(OW(fm, FM_MAT)), 19);
        mat_transpose(Mt);
        vec_rotate((u16)(fm + FM_VW), Mt, vb);
    }

    /* 12. stall logic */
    s16 st = abs16(alpha) > OS(F, 0x02);
    if (OS(F, 0x8E) == 0 || on_ground) {
        OB(fm, FM_FLAGS) &= 0xFE;
    } else if (!(OB(fm, FM_FLAGS) & 1)) {
        u8 v = (st && (OB(fm, FM_FLAGS) & 2)) ? 1 : 0;
        OB(fm, FM_FLAGS) &= 0xFE;
        OB(fm, FM_FLAGS) |= v;
        if (v) { OW(fm, FM_PITCHR) = 0; OB(fm, FM_FLAGS) &= 0xFB; }
    } else if (!(OB(fm, FM_FLAGS) & 4)) {
        OB(fm, FM_FLAGS) &= 0xFE;
        OB(fm, FM_FLAGS) |= (u8)(st & 1);
    }
    if (OB(fm, FM_FLAGS) & 0x10) { OB(fm, FM_FLAGS) |= 1; OB(fm, FM_FLAGS) &= 0xFB; }
    {
        u8 w = 0;
        if (!(OB(fm, FM_FLAGS) & 1) && !on_ground &&
            abs16(alpha) > (s16)(OS(F, 0x02) - (OS(F, 0x02) >> 2))) w = 1;
        OB(fm, FM_FLAGS) &= 0xFD;
        OB(fm, FM_FLAGS) |= (u8)(w << 1);
    }

    /* 13. ceiling */
    s16 cf;
    if (OL(F, 0xB0) == 0 || OSL(P, P_Z) <= (s32)(OL(F, 0xB0) + 0xFFFFF5D8u)) cf = -1;
    else {
        cf = (s16)(OW(F, 0xB0) + 0x5D8 - OW(P, P_Z));
        if (cf <= 0) cf = 0;
    }

    /* 14. lift */
    s16 cond = ((s16)(OS(F, 0xAA) << 1) < DSS(vb + 2) && c1 > 0) || !on_ground;
    if (!(OB(fm, FM_FLAGS) & 1) && !st && cond && cf != 0) {
        s16 a = ap_clamp_sym_0b01(alpha, 0x1000);
        s16 v = (s16)(DSS(vb + 2) + OS(F, 0xD4));
        v = v >= 0 ? v : 0;
        s16 L = ldiv_sat((s32)v << 14, (s16)cos16((u16)a));
        s16 cl = band_interp(P, band, (u16)(F + 4 + band * 2));
        L = (s16)(((s32)L * cl) >> 10);
        if (cf != -1) L = mulsh(L, cf, 12);
        s16 dx = alpha;
        if (flaps == 2) dx = (s16)(dx + 0x444);
        else if (flaps == 1) dx = (s16)(dx + 0x222);
        s16 k = (s16)((s16)(((s32)dx * 600) >> 10) + (OS(F, 0x02) >> 2));
        k = ap_clamp_sym_0b01(k, 0x1000);
        L = mulsh(L, k, 12);
        if (OW(fm, FM_LIFT) != 0x8000) L = (s16)((s16)(OS(fm, FM_LIFT) + L) >> 1);
        OS(fm, FM_LIFT) = L;
        DSW(frc + 4) = (u16)(DSW(frc + 4) + (u16)L);
    } else {
        OW(fm, FM_LIFT) = 0x8000;
    }

    /* 15. side force */
    if (OS(F, 0x16 + band * 2) != 0 && OS(F, 0x18 + band * 2) != 0 && !(OB(fm, FM_FLAGS) & 1) &&
        beta != 0 && !on_ground) {
        s16 cs = band_interp(P, band, (u16)(F + 0x16 + band * 2));
        s16 S = (s16)(((s32)DSS(vb + 2) * cs) >> 10);
        s16 b = ap_clamp_sym_0b01(beta, 0x5DC);
        S = mulsh(S, (s16)-b, 12);
        if (OW(fm, FM_SIDE) != 0x8000) S = (s16)((s16)(OS(fm, FM_SIDE) + S) >> 1);
        OS(fm, FM_SIDE) = S;
        DSW(frc) = (u16)(DSW(frc) + (u16)S);
    }

    /* 16. thrust */
    {
        s16 T = mulsh(OS(F, 0x28), OS(ctl, 6), 8);
        if (OS(F, 0xCC) != 0 && OS(fm, FM_VB + 2) >= OS(F, 0xCC)) {
            s16 di = lerp_map(OS(fm, FM_VB + 2), OS(F, 0xCC), OS(F, 0xCE), 0x400, 0);
            di = di >= 0 ? di : 0;
            T = mulsh(T, di, 10);
        }
        if (OS(fm, 0x40) < 0) OW(fm, 0x40) = 0;
        if (OS(fm, 0x42) < 0) OW(fm, 0x42) = 0;
        if (OS(F, 0x90) > 1) T = mulsh(T, (s16)((s16)(OW(fm, 0x40) + OW(fm, 0x42)) >> 1), 10);
        else T = mulsh(T, OS(fm, 0x40), 10);
        DSW(frc + 2) = (u16)(DSW(frc + 2) + (u16)T);
    }

    /* 17. gravity */
    DSW(ang) = 0; DSW(ang + 2) = 0;
    DSW(ang + 4) = (u16)(s16)(((s32)(s16)-DSS(0x3F24) * OS(ctl, 0x0A)) >> 10);
    vec_rotate(ang, Mt, grav);
    DSW(frc + 2) = (u16)(DSW(frc + 2) + DSW(grav + 2));
    DSW(frc) = (u16)(DSW(frc) + DSW(grav));
    DSW(frc + 4) = (u16)(DSW(frc + 4) + DSW(grav + 4));

    /* 18. drag */
    {
        s16 k = band_interp6(P, band, (u16)(F + band * 6 + 0x2A));
        DSW(frc) = (u16)(DSW(frc) + (u16)fm_drag(DSS(vb), k, (u16)(fm + 0x24)));
        s16 si = 0;
        if (on_ground) {
            s16 p = abs16(OS(P, P_PITCH));
            p = p < 0x800 ? p : 0x800;
            si = (s16)(mulsh(OS(F, 0xAC), p, 11) + OS(F, 0x60));
        } else if (!st && OS(F, 0xBA) != 0 && abs16(OS(P, P_PITCH)) < 6000) {
            s16 di = abs16((s16)(OW(P, P_HEAD) - old_head));
            di = di < 0x12C ? di : 0x12C;
            si = (s16)(si + mulsh(di, OS(F, 0xBA), 10));
        }
        si = (s16)(band_interp6(P, band, (u16)(F + band * 6 + 0x2C)) + si);
        if (OB(ctl, 0x0E) & 4) si = (s16)(si + OS(F, 0xC8));
        if (flaps == 2) si = (s16)(si + OS(F, 0xC4));
        else if (flaps == 1) si = (s16)(si + (OS(F, 0xC4) >> 1));
        if ((OB(ctl, 0x0E) & 8) && on_ground) si = (s16)(si + OS(F, 0xCA));
        if (OB(ctl, 0x0E) & 0x10) si = (s16)(si + OS(F, 0xC2));
        if (OS(F, 0xD0) != 0 && OS(fm, FM_VB + 2) >= OS(F, 0xD0)) {
            s16 di = lerp_map(OS(fm, FM_VB + 2), OS(F, 0xD0), OS(F, 0xD2), 0x400, 0x800);
            si = mulsh(si, di, 10);
        }
        DSW(frc + 2) = (u16)(DSW(frc + 2) + (u16)fm_drag(DSS(vb + 2), si, (u16)(fm + 0x26)));
        k = band_interp6(P, band, (u16)(F + band * 6 + 0x2E));
        DSW(frc + 4) = (u16)(DSW(frc + 4) + (u16)fm_drag(DSS(vb + 4), k, (u16)(fm + 0x28)));
    }

    /* 19. acceleration and body velocity integration */
    for (int i = 0; i < 6; i += 2)
        DSS(frc + i) = ldiv_sat((s32)DSS(frc + i) << 10, OS(ctl, 0x0A));
    fm_integrate_vel(vb, (u16)(fm + 0x1E), frc);
    if (DSS(vb) > 0) DSW(vb) = (u16)(DSW(vb) - 1);
    else if (DSS(vb) < 0) DSW(vb) = (u16)(DSW(vb) + 1);
    OW(fm, FM_VB) = DSW(vb); OW(fm, FM_VB + 2) = DSW(vb + 2); OW(fm, FM_VB + 4) = DSW(vb + 4);

    /* 20. world velocity and position */
    vec_rotate(vb, OW(fm, FM_MAT), (u16)(fm + FM_VW));
    if (!on_ground || OS(ctl, 6) > 0x28 || OS(fm, FM_VB + 2) > 0x0C) {
        DSW(ang) = (u16)dt_scale((s16)((s16)(OW(fm, FM_VW) + (u16)vw_old0) >> 1));
        DSW(ang + 2) = (u16)dt_scale((s16)((s16)(OW(fm, FM_VW + 2) + (u16)vw_old1) >> 1));
        DSW(ang + 4) = (u16)dt_scale((s16)((s16)(OW(fm, FM_VW + 4) + (u16)vw_old2) >> 1));
        pos_step((u16)(P + 2), (u16)(fm + 0x3A), DSS(ang));
        pos_step((u16)(P + 6), (u16)(fm + 0x3C), DSS(ang + 2));
        pos_step((u16)(P + 0x0A), (u16)(fm + 0x3E), DSS(ang + 4));
    } else {
        OW(fm, FM_VB + 4) = 0; OW(fm, FM_VB + 2) = 0; OW(fm, FM_VB) = 0;
        OW(fm, FM_VW + 4) = 0; OW(fm, FM_VW + 2) = 0; OW(fm, FM_VW) = 0;
    }

    /* 21. ground contact */
    if ((s32)gnd >= OSL(P, P_Z)) {
        fm_ground_contact(F, fm, ctl, P);
        OSL(P, P_Z) = gnd;
        OS(fm, FM_VW + 4) = OS(fm, FM_VW + 4) > 0 ? OS(fm, FM_VW + 4) : 0;
        vec_rotate((u16)(fm + FM_VW), Mt, (u16)(fm + FM_VB));
        OW(fm, FM_VB) = 0;
        OB(fm, FM_FLAGS) &= 0xFE; OB(fm, FM_FLAGS) &= 0xFB; OB(fm, FM_FLAGS) &= 0xFD;
    }

    /* 22. overspeed */
    OB(fm, FM_FLAGS) &= 0xF7;
    s16 vne = OS(F, 0xAE);
    if (vne != 0 && DSW(0x576B) != 0) {
        if (OS(fm, FM_VB + 2) > vne) OB(fm, FM_FLAGS) |= 0x10;
        else if ((s16)(vne - (vne >> 3)) < OS(fm, FM_VB + 2)) OB(fm, FM_FLAGS) |= 8;
    }
    fm_per_frame_velocity(fm);
    stk_release(mk);
}

/* simple-model rate approach: fixed accelerations (a_back doubled), overshoot clamped */
static void rate_approach_simple(u16 rate_ds, s16 target, s16 a_same, s16 a_back)
{
    s16 r = DSS(rate_ds);
    if (r == target) return;
    s16 a;
    if (r < target) a = r >= 0 ? a_same : (s16)(a_back << 1);
    else a = r > 0 ? (s16)-(s16)(a_back << 1) : (s16)-a_same;
    DSW(rate_ds) = (u16)(DSW(rate_ds) + (u16)dt_scale(a));
    r = DSS(rate_ds);
    if ((r < target && a < 0) || (r > target && a > 0)) DSS(rate_ds) = target;
}

/* AP 0b01:1d33 — fm_simple(FmState*, Controls*, level_hold, coord, p5): the AI model
 * (sim_world_fm.md §3.3; details from the disassembly) */
void fm_simple(u16 fm, u16 ctl, s16 level_hold, s16 coord, s16 p5)
{
    u16 mk = stk_mark();
    u16 Mt   = stk_alloc(0x14);        /* bp-72 */
    u16 Md   = stk_alloc(0x14);        /* bp-5E */
    u16 vb   = stk_alloc(6);           /* bp-16 */
    u16 frc  = stk_alloc(6);           /* bp-1C */
    u16 ang  = stk_alloc(6);           /* bp-0A */
    u16 grav = stk_alloc(6);           /* bp-10 */

    u16 F = OW(fm, FM_FMD);
    u16 P = OW(fm, FM_P);
    u16 old_pitch = OW(P, P_PITCH), old_roll = OW(P, P_ROLL), old_head = OW(P, P_HEAD);
    s16 c0 = OS(ctl, 0);
    s16 c1 = OS(ctl, 2);
    s16 c2 = (s16)-OS(ctl, 4);
    s16 flaps = (s16)(OB(ctl, 0x0E) & 3);
    s16 yaw_in = 0, lat_acc = 0;
    s16 vw_old0 = OS(fm, FM_VW), vw_old1 = OS(fm, FM_VW + 2), vw_old2 = OS(fm, FM_VW + 4);
    DSW(frc + 4) = 0; DSW(frc + 2) = 0; DSW(frc) = 0;

    s16 alpha = (s16)atan2_16(OW(fm, FM_VB + 2), (u16)-OS(fm, FM_VB + 4));
    OW(fm, FM_ALPHA) = (u16)alpha;
    s16 beta = (s16)atan2_16(OW(fm, FM_VB + 2), OW(fm, FM_VB));
    s16 y = beta;
    s16 gnd = (s16)(OS(F, 0x98) + OS(ctl, 0x0C));
    s16 on_ground = (s32)gnd >= OSL(P, P_Z);
    s16 band = alt_band(OSL(P, P_Z));
    if (band >= 8) band--;

    s32 e = band_interp(P, band, (u16)(F + 0x62 + band * 2));
    c0 = (s16)(((s32)c0 * e) >> 10);
    c1 = (s16)(((s32)c1 * e) >> 10);
    c2 = (s16)(((s32)c2 * e) >> 10);
    if (OS(F, 0xC6) != 0 && OS(fm, FM_VB + 2) > OS(F, 0xC6)) c0 >>= 2;
    c0 = ap_clamp_sym_0b01(c0, 0x100);
    c1 = ap_clamp_sym_0b01(c1, 0x100);
    c2 = ap_clamp_sym_0b01(c2, 0x100);

    /* roll rate */
    if (!(OB(fm, FM_FLAGS) & 1) && !on_ground) {
        s16 b = mulsh(c0, OS(F, 0x82), 10);
        if (c0 <= 0) b = (s16)-b;
        s16 target = mulsh((s16)(c0 + b), OS(F, 0x7C), 8);
        rate_approach_simple((u16)(fm + FM_ROLLR), target, OS(F, 0x74), OS(F, 0x76));
    }
    /* pitch rate */
    if (!on_ground || OS(fm, FM_VB + 2) > OS(F, 0xAA)) {
        s16 cx = c1, dx = 0;
        if (OB(fm, FM_FLAGS) & 1) {
            cx = 0;
            s16 si = abs16(alpha);
            if (si > 12000) {
                dx = (s16)(0x7FFF - si);
                if (dx <= 200) dx = 200;
                if (alpha < 0) dx = (s16)-dx;
            } else dx = alpha;
        }
        cx = (s16)(cx - (dx >> 2));
        cx = ap_clamp_sym_0b01(cx, 0x200);
        s16 target = mulsh(cx, OS(F, 0x7E), 8);
        rate_approach_simple((u16)(fm + FM_PITCHR), target, OS(F, 0x78), OS(F, 0x7A));
    }
    /* roll -> yaw coupling with the pilot's coordination factor */
    if (!(OB(fm, FM_FLAGS) & 1) && !on_ground) {
        s16 di = abs16(OS(P, P_PITCH));
        if (OS(P, P_PITCH) < 0) di = (s16)((di >> 1) + (di >> 3));
        if (di < 0x2000) {
            s16 k = sin16((u16)fold16(OS(P, P_ROLL)));
            k = mulsh(k, (s16)(0x2000 - di), 14);
            k = (s16)(k + (k >> 1));
            s16 coef = OS(P, P_ROLL) > 0 ? (s16)-OS(F, 0xB6) : OS(F, 0xB8);
            coef = mulsh(coef, coord, 10);
            if (k > 0) lat_acc = mulsh(coef, k, 13);
        }
    }
    OS(fm, FM_YAWC) = lat_acc;
    /* weathervane */
    if (abs16(beta) < 12000) {
        if (!(OB(fm, FM_FLAGS) & 1)) {
            if (OS(F, 0x88) < fold16(OS(P, P_ROLL)) || abs16(beta) < 0xB6) y = 0;
        }
        y = (s16)(y - (s16)(c2 << 4));
    } else {
        y = (s16)(0x7FFF - abs16(beta));
        if (y <= 0x32) y = 0x32;
        if (y >= 0xBB8) y = 0xBB8;
        y = (s16)(y << 2);
        if (beta < 0) y = (s16)-y;
    }
    {
        s16 si = (s16)-(y >> 4);
        si = (s16)(si + (si >> 1));
        if (on_ground || (abs16(OS(P, P_PITCH)) < 0x800 && abs16(OS(P, P_ROLL)) < 0x800))
            lat_acc = (s16)(lat_acc + si);
        else
            yaw_in = si;
    }
    /* on the ground */
    if (on_ground) {
        s16 d = step_toward(OS(P, P_ROLL), 0, OS(F, 0xA8));
        if (abs16(d) < OS(F, 0xBC)) OW(fm, FM_ROLLR) = 0;
        else OW(fm, FM_ROLLR) = (u16)(OW(fm, FM_ROLLR) +
                                      (u16)dt_scale(step_toward(OS(fm, FM_ROLLR), d, (s16)(OS(F, 0xA8) >> 2))));
        if (OS(fm, FM_VB + 2) <= OS(F, 0xAA)) {
            d = step_toward(OS(P, P_PITCH), OS(F, 0x9A), OS(F, 0xA8));
            if (d == 0) OW(fm, FM_PITCHR) = 0;
            else OW(fm, FM_PITCHR) = (u16)(OW(fm, FM_PITCHR) +
                                           (u16)dt_scale(step_toward(OS(fm, FM_PITCHR), d, (s16)(OS(F, 0xA8) >> 2))));
        }
    }
    /* rotate */
    if (OS(fm, FM_ROLLR) != 0 || OS(fm, FM_PITCHR) != 0 || yaw_in != 0) {
        DSW(ang) = (u16)dt_scale(OS(fm, FM_PITCHR));
        DSW(ang + 2) = (u16)dt_scale(OS(fm, FM_ROLLR));
        DSW(ang + 4) = (u16)dt_scale(yaw_in);
        mat_from_angles(ang, Md);
        mat_mul(Md, OW(fm, FM_MAT), Mt);
        mat_to_angles(Mt, (u16)(P + P_PITCH));
    }
    if (OS(fm, FM_ROLLR) == 0 && OS(fm, FM_PITCHR) == 0 && yaw_in == 0 && !(OB(fm, FM_FLAGS) & 1) &&
        abs16(OS(P, P_PITCH)) < 0xE38 && abs16(OS(P, P_ROLL)) < OS(F, 0xBC))
        OW(P, P_ROLL) = 0;
    /* heading (no smoothing in the simple model) */
    OS(fm, FM_YAWR) = lat_acc;
    OW(P, P_HEAD) = (u16)(OW(P, P_HEAD) + (u16)dt_scale(lat_acc));
    if (on_ground && OS(P, P_PITCH) > OS(F, 0x9A)) {
        OW(P, P_PITCH) = OW(F, 0x9A);
        OW(fm, FM_PITCHR) = 0;
    }
    /* matrix and body velocity */
    if ((OW(P, P_PITCH) & 0xFFF0) == (old_pitch & 0xFFF0) && (OW(P, P_ROLL) & 0xFFF0) == (old_roll & 0xFFF0) &&
        (OW(P, P_HEAD) & 0xFFF0) == (old_head & 0xFFF0)) {
        DSW(vb) = OW(fm, FM_VB); DSW(vb + 2) = OW(fm, FM_VB + 2); DSW(vb + 4) = OW(fm, FM_VB + 4);
        memmove(DSP(Mt), DSP(OW(fm, FM_MAT)), 19);
        mat_transpose(Mt);
    } else {
        mat_from_angles((u16)(P + P_PITCH), OW(fm, FM_MAT));
        memmove(DSP(Mt), DSP(OW(fm, FM_MAT)), 19);
        mat_transpose(Mt);
        vec_rotate((u16)(fm + FM_VW), Mt, vb);
    }
    /* stall */
    s16 st = on_ground ? 0 : abs16(alpha) > OS(F, 0x02);
    if (p5 != 0) {
        OB(fm, FM_FLAGS) &= 0xFE;
    } else {
        if (!(OB(fm, FM_FLAGS) & 1) && st) OW(fm, FM_PITCHR) = 0;
        OB(fm, FM_FLAGS) &= 0xFE;
        OB(fm, FM_FLAGS) |= (u8)(st & 1);
    }
    if (OB(fm, FM_FLAGS) & 0x10) OB(fm, FM_FLAGS) |= 1;
    {
        u8 w = 0;
        if (!(OB(fm, FM_FLAGS) & 1) && abs16(alpha) > (s16)(OS(F, 0x02) - (OS(F, 0x02) >> 2))) w = 1;
        OB(fm, FM_FLAGS) &= 0xFD;
        OB(fm, FM_FLAGS) |= (u8)(w << 1);
    }
    /* ceiling */
    s16 cf;
    if (OL(F, 0xB0) == 0 || OSL(P, P_Z) <= (s32)(OL(F, 0xB0) + 0xFFFFF5D8u)) cf = -1;
    else {
        cf = (s16)(OW(F, 0xB0) + 0x5D8 - OW(P, P_Z));
        if (cf <= 0) cf = 0;
    }
    /* lift (magnitude from the band coefficient, no 1/cos) */
    if (!(OB(fm, FM_FLAGS) & 1) && level_hold == 0 && cf != 0) {
        s16 v = (s16)(DSS(vb + 2) + OS(F, 0xD4));
        v = v >= 0 ? v : 0;
        s16 cl = band_interp(P, band, (u16)(F + 4 + band * 2));
        s16 L = (s16)(((s32)v * cl) >> 10);
        if (flaps == 2) L = (s16)(L + (L >> 2));
        else if (flaps == 1) L = (s16)(L + (L >> 3));
        if (cf != -1) L = mulsh(L, cf, 12);
        s16 k = (s16)((s16)(((s32)alpha * 600) >> 10) + (OS(F, 0x02) >> 2));
        k = ap_clamp_sym_0b01(k, 0x1000);
        L = mulsh(L, k, 12);
        if (OW(fm, FM_LIFT) != 0x8000) L = (s16)((s16)(OS(fm, FM_LIFT) + L) >> 1);
        OS(fm, FM_LIFT) = L;
        DSW(frc + 4) = (u16)(DSW(frc + 4) + (u16)L);
    } else {
        OW(fm, FM_LIFT) = 0x8000;
    }
    /* side force (coefficient doubled) */
    if (OS(F, 0x16 + band * 2) != 0 && OS(F, 0x18 + band * 2) != 0 && beta != 0 && !on_ground &&
        level_hold == 0) {
        s16 cs = (s16)(band_interp(P, band, (u16)(F + 0x16 + band * 2)) << 1);
        s16 S = (s16)(((s32)DSS(vb + 2) * cs) >> 10);
        s16 b = ap_clamp_sym_0b01(beta, 0x5DC);
        S = mulsh(S, (s16)-b, 12);
        if (OW(fm, FM_SIDE) != 0x8000) S = (s16)((s16)(OS(fm, FM_SIDE) + S) >> 1);
        OS(fm, FM_SIDE) = S;
        DSW(frc) = (u16)(DSW(frc) + (u16)S);
    }
    /* thrust (engine 1 factor only) */
    {
        s16 T = mulsh(OS(F, 0x28), OS(ctl, 6), 8);
        if (OS(F, 0xCC) != 0 && OS(fm, FM_VB + 2) >= OS(F, 0xCC)) {
            s16 di = lerp_map(OS(fm, FM_VB + 2), OS(F, 0xCC), OS(F, 0xCE), 0x400, 0);
            di = di >= 0 ? di : 0;
            T = mulsh(T, di, 10);
        }
        if (OS(fm, 0x40) < 0) OW(fm, 0x40) = 0;
        T = mulsh(T, OS(fm, 0x40), 10);
        DSW(frc + 2) = (u16)(DSW(frc + 2) + (u16)T);
    }
    /* gravity: lateral and vertical parts only without the level hold */
    DSW(ang) = 0; DSW(ang + 2) = 0;
    DSW(ang + 4) = (u16)(s16)(((s32)(s16)-DSS(0x3F24) * OS(ctl, 0x0A)) >> 10);
    vec_rotate(ang, Mt, grav);
    DSW(frc + 2) = (u16)(DSW(frc + 2) + DSW(grav + 2));
    if (level_hold == 0) {
        DSW(frc) = (u16)(DSW(frc) + DSW(grav));
        DSW(frc + 4) = (u16)(DSW(frc + 4) + DSW(grav + 4));
    }
    /* drag */
    {
        s16 k = band_interp6(P, band, (u16)(F + band * 6 + 0x2A));
        DSW(frc) = (u16)(DSW(frc) + (u16)fm_drag(DSS(vb), k, (u16)(fm + 0x24)));
        s16 si = 0;
        if (on_ground) {
            si = OS(F, 0x60);
            si = (s16)(si + (si >> 1));
        } else if (!st && OS(F, 0xBA) != 0 && abs16(OS(P, P_PITCH)) < 6000) {
            s16 di = abs16((s16)(OW(P, P_HEAD) - old_head));
            di = di < 0x12C ? di : 0x12C;
            si = (s16)(si + mulsh(di, OS(F, 0xBA), 10));
        }
        si = (s16)(band_interp6(P, band, (u16)(F + band * 6 + 0x2C)) + si);
        if (OB(ctl, 0x0E) & 4) si = (s16)(si + OS(F, 0xC8));
        if (flaps == 2) si = (s16)(si + OS(F, 0xC4));
        else if (flaps == 1) si = (s16)(si + (OS(F, 0xC4) >> 1));
        if ((OB(ctl, 0x0E) & 8) && on_ground) si = (s16)(si + OS(F, 0xCA));
        if (OB(ctl, 0x0E) & 0x10) si = (s16)(si + OS(F, 0xC2));
        if (OS(F, 0xD0) != 0 && OS(fm, FM_VB + 2) >= OS(F, 0xD0)) {
            s16 di = lerp_map(OS(fm, FM_VB + 2), OS(F, 0xD0), OS(F, 0xD2), 0x400, 0x800);
            si = mulsh(si, di, 10);
        }
        DSW(frc + 2) = (u16)(DSW(frc + 2) + (u16)fm_drag(DSS(vb + 2), si, (u16)(fm + 0x26)));
        k = band_interp6(P, band, (u16)(F + band * 6 + 0x2E));
        DSW(frc + 4) = (u16)(DSW(frc + 4) + (u16)fm_drag(DSS(vb + 4), k, (u16)(fm + 0x28)));
    }
    /* integrate */
    for (int i = 0; i < 6; i += 2)
        DSS(frc + i) = ldiv_sat((s32)DSS(frc + i) << 10, OS(ctl, 0x0A));
    fm_integrate_vel(vb, (u16)(fm + 0x1E), frc);
    OW(fm, FM_VB) = DSW(vb); OW(fm, FM_VB + 2) = DSW(vb + 2); OW(fm, FM_VB + 4) = DSW(vb + 4);
    vec_rotate(vb, OW(fm, FM_MAT), (u16)(fm + FM_VW));
    if (p5 != 0 && st) OS(fm, FM_VW + 4) = vw_old2 > 0 ? vw_old2 : 0;
    if (!on_ground || OS(ctl, 6) > 0x28 || OS(fm, FM_VB + 2) > 0x0C) {
        DSW(ang) = (u16)dt_scale((s16)((s16)(OW(fm, FM_VW) + (u16)vw_old0) >> 1));
        DSW(ang + 2) = (u16)dt_scale((s16)((s16)(OW(fm, FM_VW + 2) + (u16)vw_old1) >> 1));
        DSW(ang + 4) = (u16)dt_scale((s16)((s16)(OW(fm, FM_VW + 4) + (u16)vw_old2) >> 1));
        pos_step((u16)(P + 2), (u16)(fm + 0x3A), DSS(ang));
        pos_step((u16)(P + 6), (u16)(fm + 0x3C), DSS(ang + 2));
        pos_step((u16)(P + 0x0A), (u16)(fm + 0x3E), DSS(ang + 4));
    } else {
        OW(fm, FM_VB + 4) = 0; OW(fm, FM_VB + 2) = 0; OW(fm, FM_VB) = 0;
        OW(fm, FM_VW + 4) = 0; OW(fm, FM_VW + 2) = 0; OW(fm, FM_VW) = 0;
    }
    if ((s32)gnd >= OSL(P, P_Z)) {
        fm_ground_contact(F, fm, ctl, P);
        OSL(P, P_Z) = gnd;
        OS(fm, FM_VW + 4) = OS(fm, FM_VW + 4) > 0 ? OS(fm, FM_VW + 4) : 0;
        vec_rotate((u16)(fm + FM_VW), Mt, (u16)(fm + FM_VB));
        OW(fm, FM_VB) = 0;
        OB(fm, FM_FLAGS) &= 0xFE; OB(fm, FM_FLAGS) &= 0xFD;
    }
    fm_per_frame_velocity(fm);
    stk_release(mk);
}

/* AP 0b01:1860 — fm_step(FmState*, Controls*, mode, coord, p5, level_hold) — sim_world_fm.md §3.1.
 * Modes 1..3 edit a copy of the .FMD at DS:568E (realism "flight model"), then run fm_full once with
 * DS:711E below 100h, else twice with DS:711E halved (the half-step split). */
void fm_step(u16 fm, u16 ctl, s16 mode, s16 coord, s16 p5, s16 level_hold)
{
    u16 saved_mass = OW(ctl, 0x0A);
    if (mode == 4) { fm_simple(fm, ctl, level_hold, coord, p5); return; }
    u16 fmd_saved = OW(fm, FM_FMD);
    memmove(DSP(0x568E), DSP(fmd_saved), 0xD6);              /* SCOPY@ */
    OW(fm, FM_FMD) = 0x568E;
    DSW(0x571C) = p5 == 0;
    if (mode == 1) {
        for (u16 o = 0x572C; o <= 0x5734; o += 2) DSW(o) = (u16)(DSW(o) << 1);   /* +9E..+A6 */
        OW(ctl, 0x0A) = 0x400;
        for (u16 si = 0; si != 0x12; si += 2) {
            DSW(0x56A4 + si) = (u16)(DSW(0x56A4 + si) << 1);                     /* +16.. side force */
            DSW(0x56F0 + si) = 0x400;                                             /* +62.. effectiveness */
        }
        DSW(0x5704) = (u16)(DSW(0x5704) + (u16)(DSS(0x5704) >> 1));               /* +76 */
        DSW(0x5708) = (u16)(DSW(0x5708) + (u16)(DSS(0x5708) >> 1));               /* +7A */
        DSW(0x571A) = 0; DSW(0x5718) = 0; DSW(0x5710) = 0;                        /* +8C, +8A, +82 */
        DSW(0x5722) = 0; DSW(0x5720) = 0;                                         /* +94, +92 */
        DSW(0x573C) = 0; DSW(0x574E) = 0; DSW(0x5754) = 0;                        /* +AE, +C0, +C6 */
    } else if (mode == 2) {
        DSW(0x572C) = (u16)(DSW(0x572C) + (u16)(DSS(0x572C) >> 2));
        DSW(0x572E) = (u16)(DSW(0x572E) + (u16)(DSS(0x572E) >> 2));
        DSW(0x5730) = (u16)(DSW(0x5730) + (u16)(DSS(0x5730) >> 1));
        DSW(0x5732) = (u16)(DSW(0x5732) + (u16)(DSS(0x5732) >> 1));
        DSW(0x5734) = (u16)(DSW(0x5734) + (u16)(DSS(0x5734) >> 1));
        DSW(0x5716) = 0;                                                          /* +88 */
        DSW(0x571A) = 0; DSW(0x5718) = 0;
        DSW(0x5722) = 0; DSW(0x5720) = 0;
        DSW(0x573C) = 0; DSW(0x574E) = 0; DSW(0x5754) = 0;
    } else if (mode == 3) {
        DSW(0x5716) = 0;
    }
    s16 t = DT;
    if (DT > 0xFF) {
        DT = (s16)(DT >> 1);
        fm_full(fm, ctl);
        fm_full(fm, ctl);
        DT = t;
    } else {
        fm_full(fm, ctl);
    }
    if (DSB(0x5764) != 0 && mode == 1) { OB(fm, 0x47) = 0; DSB(0x5764) = 0; }
    OW(fm, FM_FMD) = fmd_saved;
    OW(ctl, 0x0A) = saved_mass;
}

/* AP 0b01:2d9c — landing damage level from attitude, sink rate and gear (sim_world_fm.md §3.4) */
void fm_ground_contact(u16 F, u16 fm, u16 ctl, u16 P)
{
    s16 cx = OS(F, 0x9E), lvl;
    s16 pitch = OS(P, P_PITCH);
    if (pitch > OS(F, 0xA4) || pitch < OS(F, 0xA6) || abs16(OS(P, P_ROLL)) > OS(F, 0xA2)) {
        if ((cx >> 1) >= OS(fm, FM_VB + 2)) lvl = 1;
        else lvl = OS(fm, FM_VB + 2) > cx ? 3 : 2;
        if (OC(fm, 0x47) < lvl) OB(fm, 0x47) = (u8)lvl;
    }
    s16 s = (s16)-OS(fm, FM_VW + 4), d = OS(F, 0xA0);
    if (s > d) {
        if ((s16)(d << 1) >= s) lvl = 1;
        else lvl = (s16)(d << 2) >= s ? 2 : 3;
        if (OC(fm, 0x47) < lvl) OB(fm, 0x47) = (u8)lvl;
    }
    if (!(OB(ctl, 0x0E) & 0x10)) {
        if (OC(fm, 0x47) < 1) OB(fm, 0x47) = 1;
        DSB(0x5764) = 1;
    }
}
