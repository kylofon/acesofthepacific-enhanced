/* render3d — sky.c: sun, moon, stars and time of day (AP 55e8 sky.c, 59eb stars.c, 6964, 4b91, 1a9a).
 * render3d_sky.md §3.
 *
 * Bodies (DS:111E sun, DS:1128 moon): +0 flags (40h on screen this frame), +2 time (hour angle,
 * 10000h = 24 h, decreasing), +4 declination, +6/+8 screen x/y. */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"
#include "codeptr.h"
#include "video/video.h"
#include "sim_flow/sim_flow.h"

enum {
    K_SUN = 0x111E, K_MOON = 0x1128, K_SUN_TIME = 0x1120, K_SUN_DECL = 0x1122,
    K_MOON_TIME = 0x112A, K_MOON_DECL = 0x112C, K_TOD = 0x1132, K_SUNRISE = 0x113A, K_DUSK = 0x1140,
    K_SUNSET = 0x1142, K_NIGHT_LIGHT = 0x1144, K_MOON_BMP = 0x114A, K_NIGHT_SKY = 0x114C, K_TAPS = 0x114D,
    K_CY = 0x51B2, K_CX = 0x51B4, K_RECT = 0x51B6, K_RX = 0x51B6, K_RY = 0x51B8, K_RW = 0x51BA, K_RH = 0x51BC,
    K_SUN_VEC = 0x51BE, K_DETAIL = 0x51C4, K_TICK = 0x51C5, K_DAY_REL = 0x51C9, K_UNUSED = 0x51CD,
    K_LAT = 0x51CF, K_DAWN_LO = 0x51D1, K_DAWN_HI = 0x51D3, K_MOON_H = 0x51D5, K_MOON_W = 0x51D7,
    K_LIGHT = 0x51D9, K_MOON_LIGHT = 0x51DF, K_SUN_LIGHT = 0x51E5,
    K_STAR_ROT0 = 0x5CE2, K_STAR_ROT = 0x5CE4, K_STAR_N = 0x5CE6, K_STARS = 0x5CE8,
    K_CP_MOON_T = 0x44EC, K_CP_MOON_D = 0x44EE, K_CP_SUN_T = 0x44F2, K_CP_SUN_D = 0x44F4,
    K_EPOCH = 0x070E, K_MDAYS = 0x0712, K_MOON_NAME = 0x071E, K_CUMDAYS_M1 = 0x0722, K_MDAYS1 = 0x0214,
    K_DT = 0x711E, K_BMP_EXT = 0x05B4, K_STARS_NAME = 0x0860, K_DAT_EXT = 0x05BC, K_RB = 0x05EB,
    K_PAGE = 0x26D2, K_READ_PAGE = 0x26D6, K_DRAW_PAGE = 0x26D8,
};
#define SKY_TAG   far_make(SEG(0x38A2), 0)       /* "sky.c" */
#define STARS_TAG far_make(SEG(0x38B7), 0)       /* "stars.c" */

static s16 abs16(s16 v) { return (s16)(v < 0 ? (u16)-v : (u16)v); }

/* rotate the equatorial vector by −lat about x (y = (cos·y + sin·z) >> 14, z = (cos·z − sin·y) >> 14) */
static void rot_lat(u16 v)
{
    u16 lat = (u16)-DSW(K_LAT);
    s32 c = (s32)(s16)cos16(lat), s = sin16(lat);
    s16 y = (s16)DSW(v + 2), z = (s16)DSW(v + 4);
    s16 ny = (s16)((c * y + s * z) >> 14);
    s16 nz = (s16)((c * z - s * y) >> 14);
    DSW(v + 4) = (u16)nz;
    DSW(v + 2) = (u16)ny;
}

/* ================================================================================================
 * 55e8 — sky.c */

/* AP 55e8:0000 (no callers) */
void stars_load_thunk(void) { stars_load(); }

/* AP 55e8:000a — mission end */
void sky_free(void)
{
    if (DSW(K_MOON_BMP)) bmp_free(DSW(K_MOON_BMP));
    DSW(K_MOON_BMP) = 0;
}

/* AP 55e8:0028 — mission day count -> {month (0-based), day, year byte} at out */
FarPtr ap_date_split(u16 day, FarPtr out)
{
    s32 X = (s32)day + DSSL(K_EPOCH);
    s16 q = (s16)idiv32_386((int64_t)(s32)((u32)X << 2), 0x5B5, NULL);
    s16 d = (s16)((u16)X - (u16)(s16)((s32)q * 365));
    d = (s16)(d - (s16)((s16)(q - 1) / 4));
    if (X <= 365) d++;
    DSB(K_MDAYS + 1) = (q % 4 == 0) ? 29 : 28;
    s16 m = 0;
    while (d > 0) d = (s16)(d - (s8)DSB(K_MDAYS + m++));
    m--;
    wr8(out.seg, out.off, (u8)m);
    wr8(out.seg, (u16)(out.off + 1), (u8)((u8)d + DSB(K_MDAYS + m)));
    wr8(out.seg, (u16)(out.off + 2), (u8)((u8)q + 0x2C));
    return out;
}

/* AP 55e8:028f — days from the (one-month shifted, quirk kept) September equinox */
s16 day_of_year(s16 m, s16 d) { return (s16)(DSW(K_CUMDAYS_M1 + 2 * m) + d - 0x108); }
/* AP 55e8:02a3 — day to angle (×179, 16-bit) */
s16 declination(s16 v) { return (s16)(v * 0xB3); }

/* AP 55e8:00c4 — date, time-of-day setting, latitude (and a dead word) */
void sky_init(u16 day, u16 tod, u16 lat, u16 unused)
{
    u16 mk = stk_mark(), name = stk_alloc(6), date = stk_alloc(4);
    memcpy(DSP(name), DSP(K_MOON_NAME), 6);                /* "moon " */
    ap_date_split(day, ds_ptr(date));
    DSB(K_NIGHT_SKY) = (s16)tod > 3;
    DSW(K_DAY_REL) = (u16)day_of_year((s8)DSB(date), (s8)DSB(date + 1));
    s16 a = declination((s16)DSW(K_DAY_REL));
    DSW(K_SUN_DECL) = DSW(K_CP_SUN_D) = (u16)mul_sin((s16)0xEF46, (u16)a);   /* ±23.5° */
    u16 t = (u16)mul_sin((s16)mul_sin(0x1304, (u16)a), lat);
    DSW(K_SUNSET) = (u16)(0xC000 - t);
    DSW(K_DUSK) = (u16)(DSW(K_SUNSET) + 0x1E38);
    t = (u16)mul_sin((s16)mul_sin(0x1304, (u16)a), lat);
    DSW(K_SUNRISE) = (u16)(t + 0x4000);
    DSW(K_DAWN_HI) = (u16)(DSW(K_SUNRISE) + 0x190);
    DSW(K_DAWN_LO) = (u16)(DSW(K_DAWN_HI) - 0x640);
    DSW(K_SUNSET) = (u16)(DSW(K_SUNSET) + 0x200);
    DSW(K_SUNRISE) = (u16)(DSW(K_SUNRISE) - 0x100);
    DSW(K_SUN_TIME) = DSW(K_CP_SUN_T) = DSW(K_TOD + 2 * tod);
    DSW(K_MOON_DECL) = DSW(K_CP_MOON_D) = DSW(K_SUN_DECL);
    DSW(K_MOON_TIME) = (u16)(moon_phase(date) + DSW(K_TOD + 2 * tod));
    DSW(K_CP_MOON_T) = DSW(K_MOON_TIME);
    s16 ph = (s16)(DSW(K_MOON_TIME) - DSW(K_SUN_TIME));
    s32 k = abs16(ph) < 0x1555 ? 0 : (s32)(s16)(ph / 0x1999);
    if (k < 0) k += 10;
    if (k) {                                               /* moon shown only away from new moon */
        DSB(name + 4) = (u8)((u8)k + '0');
        DSW(K_MOON_BMP) = bmp_load_planes(res_name(name, K_BMP_EXT));
        if (!DSW(K_MOON_BMP)) assert_fail(SKY_TAG, 0xFB, name);
        u16 f = DSW(DSW(K_MOON_BMP));
        DSW(K_MOON_W) = DSW(f + 6);
        DSW(K_MOON_H) = DSW(f + 8);
    }
    DSW(K_LAT) = lat;
    DSW(K_UNUSED) = unused;
    stars_reset(DSW(K_SUN_TIME));
    sky_update();
    stk_release(mk);
}

/* ================================================================================================
 * 4b91 — moon phase */

/* AP 4b91:0000 — date {month0, day, year8} (one-month table quirk kept) */
s16 moon_phase(u16 date)
{
    s16 Y = (s16)((s8)DSB(date + 2) + 0x1268);
    DSB(K_MDAYS1 + 2) = (Y % 4 == 0) ? 29 : 28;
    s16 D = (s8)DSB(date + 1);
    for (s16 i = 1; (s8)DSB(date) > i; i++) D = (s16)(D + (s8)DSB(K_MDAYS1 + i));
    s32 J = (s32)Y * 365 + D + (s16)((s16)(Y - 1) / 4);
    if (Y == 0) J--;
    return moon_phase_days((s32)((u32)J + 0xFFDB2647u));
}
/* AP 4b91:008d — (x << 14) mod 6ECCCh (exact i32 wraparound), days × 93Fh */
s16 moon_phase_days(s32 x)
{
    s32 r;
    idiv32_386((int64_t)(s32)((u32)x << 14), 0x6ECCC, &r);
    s32 days = r >> 14;
    return (s16)((s32)(s16)days * 0x93F);
}

/* ================================================================================================
 * 59eb — stars.c */

/* AP 59eb:0000 — STARS.DAT: {u16 flags; u16 ra; i16 dec} */
void stars_load(void)
{
    u16 fp = my_fopen(res_name(K_STARS_NAME, K_DAT_EXT), K_RB);
    if (!fp) assert_fail(STARS_TAG, 0x49, 0);
    DSW(K_STAR_N) = (u16)((u16)my_filelength(fp) / 6);
    u16 bytes = (u16)(DSW(K_STAR_N) * 6);
    ds_far_wr(K_STARS, mem_far_new(bytes, 0));
    ap_fread_far_55a4(ds_far(K_STARS), (s32)(u32)bytes, fp);
    my_fclose(fp);
}
/* AP 59eb:0097 — sidereal rotation for time t; clears the skip bits */
void stars_reset(u16 t)
{
    DSW(K_STAR_ROT) = DSW(K_STAR_ROT0) = (u16)(t + (u16)((s16)DSW(K_DAY_REL) * 0xB3));
    FarPtr s = ds_far(K_STARS);
    for (s16 i = 0; i < (s16)DSW(K_STAR_N); i++, s = far_add(s, 6)) wr16(s.seg, s.off, (u16)(rd16(s.seg, s.off) & 0xFF7F));
}

/* ================================================================================================
 * 1a9a — stars, angle vector */

/* AP 1a9a:01da — {decl, time} -> equatorial unit vector (2.14) at out */
u16 sky_dir(u16 out, u16 ang)
{
    if (DSW(ang)) {
        s16 c = (s16)cos16(DSW(ang));
        DSW(out) = (u16)mul_sin((s16)-c, DSW(ang + 2));
        DSW(out + 2) = (u16)sin16(DSW(ang));
        DSW(out + 4) = (u16)mul_cos((s16)-c, DSW(ang + 2));
    } else {
        DSW(out) = (u16)-sin16(DSW(ang + 2));
        DSW(out + 2) = 0;
        DSW(out + 4) = (u16)-(s16)cos16(DSW(ang + 2));
    }
    return out;
}

/* AP 1a9a:000a — angular projection (1024 px per turn across, 873.8 down) */
void draw_stars(void)
{
    FarPtr s = ds_far(K_STARS);
    if (!DSB(K_NIGHT_SKY)) return;
    u8 S = CSB(CS_PROJ_SHIFT);
    s16 f = (s16)(1 << (S & 15));
    u16 mk = stk_mark(), M = stk_alloc(20), ang = stk_alloc(4), v = stk_alloc(6), e = stk_alloc(6);
    memcpy(DSP(M), csp(CS_M_CAM), 19);
    for (s16 i = 0; i < (s16)DSW(K_STAR_N); i++, s = far_add(s, 6)) {
        if (rd16(s.seg, s.off) & 0x80) continue;
        DSW(ang) = rd16(s.seg, (u16)(s.off + 4));
        DSW(ang + 2) = (u16)(rd16(s.seg, (u16)(s.off + 2)) + DSW(K_STAR_ROT));
        sky_dir(v, ang);
        rot_lat(v);
        vec_rotate(v, M, e);
        if ((s16)DSW(e + 2) <= f) continue;
        s16 sy = (s16)((s16)atan2_16(DSW(e + 2), (u16)-DSW(e + 4)) / 75 + (s16)DSW(K_CY));
        if (!((s16)DSW(K_RY) < sy && (s16)(DSW(K_RY) + DSW(K_RH)) > sy)) continue;
        s16 sx = (s16)((s16)atan2_16(DSW(e + 2), DSW(e)) / 64 + (s16)DSW(K_CX));
        if (!((s16)DSW(K_RX) < sx && (s16)(DSW(K_RX) + DSW(K_RW)) > sx)) continue;
        gfx_put_pixel(sx, sy, (s16)(0x8D + (rd16(s.seg, s.off) & 1)));
    }
    stk_release(mk);
}

/* ================================================================================================
 * 6964 — sun, moon, time of day */

/* AP 6964:0000 — skip ahead n (the remainder is dropped, quirk kept) */
void sky_advance(u16 n)
{
    s32 v = (s32)(s16)n << 8;
    if (v < DSSL(K_TICK)) { DSSL(K_TICK) -= (s32)(s16)(u16)v; return; }
    s16 k = ldiv_sat(v - DSSL(K_TICK), 0x1900);
    u16 d = (u16)((k << 2) + 4);
    DSW(K_SUN_TIME) = (u16)(DSW(K_SUN_TIME) - d);
    DSW(K_MOON_TIME) = (u16)(DSW(K_MOON_TIME) - d);
    DSW(K_STAR_ROT) = (u16)(DSW(K_STAR_ROT) - d);
    DSSL(K_TICK) = 0x1900;
}

/* AP 6964:05a3 — sunrise − m < time <= sunset + m (unsigned) */
u16 sun_in_window(u16 m)
{
    u16 lo = (u16)(DSW(K_SUNRISE) - m), hi = (u16)(DSW(K_SUNSET) + m);
    return DSW(K_SUN_TIME) > lo && DSW(K_SUN_TIME) <= hi;
}
/* AP 6964:005b — sun direction (local world frame) when up, else 0 */
u16 sun_vector_if_up(void) { return sun_in_window(0x800) ? K_SUN_VEC : 0; }
/* AP 6964:0633 — sun light angles when up, else 0 */
u16 sun_angles_if_up(void) { return sun_in_window(0x800) ? K_SUN_LIGHT : 0; }
/* AP 6964:059b (no callers) */
u16 sun_angle(void) { return DSW(K_SUN_TIME); }

/* AP 6964:05d2 — sun by day, the moon when up, else the night light */
void choose_light(void)
{
    u16 mt = DSW(K_MOON_TIME);
    u16 src = sun_in_window(0x1000) ? K_SUN_LIGHT : (mt > 0x4000 && mt < 0xC000) ? K_MOON_LIGHT : K_NIGHT_LIGHT;
    DSW(K_LIGHT) = DSW(src); DSW(K_LIGHT + 2) = DSW(src + 2); DSW(K_LIGHT + 4) = DSW(src + 4);
}

/* AP 6964:0076 — per sim step: clock, night flag, light, sun vector */
void sky_update(void)
{
    DSSL(K_TICK) -= (s32)(s16)DSW(K_DT);
    if (DSSL(K_TICK) <= 0) {
        DSW(K_SUN_TIME) = (u16)(DSW(K_SUN_TIME) - 4);
        DSW(K_MOON_TIME) = (u16)(DSW(K_MOON_TIME) - 4);
        DSW(K_STAR_ROT) = (u16)(DSW(K_STAR_ROT) - 4);
        DSSL(K_TICK) += 0x1900;
    }
    DSB(K_NIGHT_SKY) = abs16((s16)DSW(K_SUN_TIME)) > (s16)DSW(K_DAWN_HI) ? 0 : 1;
    DSW(K_SUN_LIGHT) = (u16)-(DSW(K_SUN_TIME) + 0x4000);
    DSW(K_SUN_LIGHT + 2) = 0;
    DSW(K_SUN_LIGHT + 4) = (u16)(DSW(K_SUN_DECL) + 0xC000);
    DSW(K_MOON_LIGHT) = (u16)-(DSW(K_MOON_TIME) + 0x4000);
    DSW(K_MOON_LIGHT + 2) = 0;
    DSW(K_MOON_LIGHT + 4) = (u16)(DSW(K_MOON_DECL) + 0xC000);
    choose_light();
    u16 mk = stk_mark(), ang = stk_alloc(4), v = stk_alloc(6);
    DSW(ang) = DSW(K_SUN_DECL);
    DSW(ang + 2) = DSW(K_SUN_TIME);
    sky_dir(v, ang);
    DSW(K_SUN_VEC) = DSW(v);
    u16 lat = (u16)-DSW(K_LAT);
    s32 c = (s32)(s16)cos16(lat), s = sin16(lat);
    DSW(K_SUN_VEC + 2) = (u16)((c * (s16)DSW(v + 2) + s * (s16)DSW(v + 4)) >> 14);
    DSW(K_SUN_VEC + 4) = (u16)((c * (s16)DSW(v + 4) - s * (s16)DSW(v + 2)) >> 14);
    stk_release(mk);
}

/* AP 6964:0298 — perspective projection of a body (1 = sun) into the view rectangle */
void celestial_project(u8 sun)
{
    u16 b = sun ? K_SUN : K_MOON;
    DSW(b) &= 0xFFBF;
    u16 mk = stk_mark(), M = stk_alloc(20), ang = stk_alloc(4), v = stk_alloc(6), e = stk_alloc(6);
    memcpy(DSP(M), csp(CS_M_CAM), 19);
    DSW(ang) = DSW(b + 4);
    DSW(ang + 2) = DSW(b + 2);
    sky_dir(v, ang);
    rot_lat(v);
    vec_rotate(v, M, e);
    u8 S = CSB(CS_PROJ_SHIFT);
    if ((s16)(1 << (S & 15)) >= (s16)DSW(e + 2)) { stk_release(mk); return; }
    s32 sx = ldiv_sat((s32)(s16)DSW(e) << (S & 31), (s16)DSW(e + 2));
    s32 sy = ldiv_sat((s32)(s16)DSW(e + 4) << (S & 31), (s16)-DSW(e + 2));
    sx += (s16)DSW(K_CX);
    sy += (s16)DSW(K_CY);
    if ((s32)(s16)(DSW(K_RX) - 7) < sx && (s32)(s16)(DSW(K_RX) + DSW(K_RW)) > sx &&
        (s32)(s16)(DSW(K_RY) - 7) < sy && (s32)(s16)(DSW(K_RY) + DSW(K_RH)) > sy) {
        DSW(b + 6) = (u16)sx;
        DSW(b + 8) = (u16)sy;
        DSW(b) |= 0x40;
    }
    stk_release(mk);
}

/* AP 6964:01a2 — the horizon callback: stars, sun disc, moon bitmap */
void draw_celestial(void)
{
    DSW(K_CY) = (u16)cockpit_view_window(K_RECT);
    DSW(K_CX) = (u16)((s16)DSW(K_RW) >> 1);
    DRV_CLIP_ON = 1;
    DRV_X1 = (s16)DSW(K_RX);
    DRV_X2 = (s16)(DSW(K_RX) + DSW(K_RW) - 1);
    DRV_Y1 = (s16)DSW(K_RY);
    DRV_Y2 = (s16)(DSW(K_RY) + DSW(K_RH) - 1);
    clip_to_view();
    DSW(K_DRAW_PAGE) = DSW(K_PAGE);
    if (DSB(K_DETAIL)) draw_stars();
    celestial_project(1);
    celestial_project(0);
    if (DSW(K_SUN) & 0x40) {
        DRV_EDGE_COL = DRV_FILL_COL = 0x8C;
        gfx_circle(7, (s16)DSW(K_SUN + 6), (s16)DSW(K_SUN + 8));
    }
    if (DSB(K_NIGHT_SKY) && DSB(K_DETAIL) && DSW(K_MOON_BMP) && (DSW(K_MOON) & 0x40) && !(DSW(K_MOON) & 0x80)) {
        u16 f = DSW(DSW(K_MOON_BMP));
        if (CSW(CS_CAM_ANG + 2))
            gfx_sprite_rotated_ind(f, (s16)DSW(K_MOON + 6), (s16)DSW(K_MOON + 8), (u16)-CSW(CS_CAM_ANG + 2),
                                   (s16)DSW(K_MOON_W), (s16)DSW(K_MOON_H), 0, 0);
        else gfx_sprite_flip(f, (s16)DSW(K_MOON + 6), (s16)DSW(K_MOON + 8), 0);
    }
}

/* AP 6964:064d — day_v by day, night_v by night, linear through the twilight ramp */
u16 darkness_lerp(u16 day_v, u16 night_v)
{
    s16 t = abs16((s16)DSW(K_SUN_TIME));
    if (DSW(K_SUN_TIME) == 0x8000) t = 0x7FFF;
    if (t > (s16)DSW(K_DAWN_HI)) return day_v;
    if (t < (s16)DSW(K_DAWN_LO)) return night_v;
    s32 k = (s32)abs16((s16)(t - DSW(K_DAWN_HI))) * (s16)(night_v - day_v);
    return (u16)(day_v + ldiv_sat(k, 0x640));
}

/* AP 6964:0458 — sun glare 0..4000h (palfx_frame): centred, unoccluded sun disc (colour 8Ch) */
u16 ap_sun_glare_6964(void)
{
    u16 tm = DSW(K_SUN_TIME);
    s16 hidden = 9;
    if (!(DSW(K_SUN) & 0x40)) return 0;
    if (tm > DSW(K_SUNSET) && tm < DSW(K_SUNRISE)) return 0;       /* never true (quirk, see spec) */
    s16 cx = (s16)(DSW(K_RX) + (s16)DSW(K_RW) / 2), cy = (s16)(DSW(K_RY) + (s16)DSW(K_RH) / 2);
    s16 ax = abs16((s16)(DSW(K_SUN + 6) - cx)), ay = abs16((s16)(DSW(K_SUN + 8) - cy));
    s16 dist = ay > ax ? (s16)((ax >> 2) + (ax >> 3) + ay) : (s16)((ay >> 2) + (ay >> 3) + ax);
    s16 g = (s16)(0x3000 - ldiv_sat((s32)dist * 0x3000, 0x50));
    s16 w = (s16)darkness_lerp(0x4000, 0xE000);
    if (w <= 0) w = 0;
    g = (s16)(((s32)g * w) >> 14);
    DSW(K_READ_PAGE) = DSW(K_PAGE);
    for (int k = 0; k < 9; k++)
        if (gfx_get_pixel((s16)(DSW(K_SUN + 6) + (s8)DSB(K_TAPS + 2 * k)),
                          (s16)(DSW(K_SUN + 8) + (s8)DSB(K_TAPS + 2 * k + 1))) == 0x8C) hidden--;
    g = (s16)(g - ldiv_sat((s32)hidden << 12, 9));
    return (u16)clamp(g, 0, 0x4000);
}

__attribute__((constructor)) static void sky_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x696401A2u, (CodeFn)draw_celestial);
}
