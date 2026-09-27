/* sim_flow: views and cameras (1c30), servo helpers (0590), weapon camera (05d6:036c/0381/03fc) and
 * the next view object (637f:00af) — sim_flow_cockpit.md §2. */
#include "sim_flow/sfa_priv.h"

#define VIEW            DSS(AP_DS_view)            /* 5EBB */
#define VIEW_TARGET     DSW(0x5E4B)
#define VIEW_INSIDE     DSW(AP_DS_view_inside)     /* 19F2 */
#define VIEW_CLASS      DSW(0x19F6)
#define VIEW_FORCE      DSW(0x1AF8)
#define VIEW_SET        DSW(0x1B20)
#define PAN_MOVED       DSW(0x1B22)
#define PAN_HELD        DSW(0x1B24)
#define ORBIT_PAN       DSW(0x1B26)
#define VIEW_REQ        DSS(0x1B28)
#define VIEW_RESET_REQ  DSW(0x1B2A)
#define VIEW_OVERRIDE   DSW(0x1AFA)
#define VIEW_FORCED_CMD DSS(0x1AFC)
#define WCAM_IMPACT     DSB(0x1AFE)
#define DEATHCAM_REQ    DSB(0x1AFF)
#define WCAM_ACTIVE     DSB(0x5E3C)
#define CAM_POS         0x5E51
#define CAM_ANG         0x5E5D                     /* pitch, roll (5E5F), heading (5E61) */
#define ORBIT_DIST      DSS(0x5E65)
#define ORBIT_HEADING   DSS(0x5E67)
#define ORBIT_ROLL      DSS(0x5E69)
#define ORBIT_PITCH     DSS(0x5E6B)
#define SERVO_DIST      0x5E6D
#define SERVO_HEADING   0x5E7F
#define SERVO_ROLL      0x5E91
#define SERVO_PITCH     0x5EA3
#define DEATHCAM_FIRST  DSW(0x5EB5)
#define TRAIL_SEG       0x3A73
#define TRAIL_ANG       0x0016
#define TRAIL_POS       0x026E

/* the per-frame camera function DS:1AF4 (far) */
static void set_frame_fn(u16 off) { ds_far_wr(0x1AF4, far_make(SEG(0x1C30), off)); }
static bool frame_fn_set(void) { return DSL(0x1AF4) != 0; }

/* ---- 0590: rate-limited servos ------------------------------------------------------------------------ */

/* AP 0590:0002 approach(v, target, step) -> remaining distance */
s16 approach(u16 v, s16 target, s16 step)
{
    s16 old = DSS(v);
    if (old > target) {
        DSS(v) = (s16)(DSS(v) - step);
        if (DSS(v) <= target) { DSS(v) = target; return 0; }
        return (s16)(DSS(v) - target);
    }
    if (old < target) {
        DSS(v) = (s16)(DSS(v) + step);
        if (DSS(v) >= target) { DSS(v) = target; return 0; }
        return (s16)(target - DSS(v));
    }
    return 0;
}

/* AP 0590:004b */
s16 approach_dt(u16 v, s16 target, s16 step)
{
    return approach(v, target, dt_scale(step));
}

/* AP 0590:0069 servo_step(s) — sim_flow_cockpit.md §2.6 */
s16 servo_step(u16 s)
{
    s16 e = (s16)(DSS(s + 8) - DSS(s + 6));
    u8 sh = DSB(s + 0x10);
    if (e <= 0 && DSS(s + 0x0A) <= 0) {
        if ((u16)-e < DSW(s + 4)) {
            DSS(s + 0x0A) = (s16)(e >> sh);
            DSW(s + 0x0C) = 0;
        } else {
            approach((u16)(s + 0x0C), DSS(s), DSS(s + 2));
            approach((u16)(s + 0x0A), (s16)-DSS(s + 0x0E), DSS(s + 0x0C));
        }
    } else if (e >= 0 && DSS(s + 0x0A) >= 0) {
        if (DSW(s + 4) > (u16)e) {
            DSS(s + 0x0A) = (s16)(e >> sh);
            DSW(s + 0x0C) = 0;
        } else {
            approach((u16)(s + 0x0C), DSS(s), DSS(s + 2));
            approach((u16)(s + 0x0A), DSS(s + 0x0E), DSS(s + 0x0C));
        }
    } else {
        approach((u16)(s + 0x0C), DSS(s), DSS(s + 2));
        if (e > 0) {
            DSS(s + 0x0A) = (s16)(DSS(s + 0x0A) + DSS(s + 0x0C));
            if (DSS(s + 0x0A) > e) DSS(s + 0x0A) = e;
        } else {
            DSS(s + 0x0A) = (s16)(DSS(s + 0x0A) - DSS(s + 0x0C));
            if (DSS(s + 0x0A) < e) DSS(s + 0x0A) = e;
        }
    }
    DSS(s + 6) = (s16)(DSS(s + 6) + dt_scale(DSS(s + 0x0A)));
    return DSS(s + 6);
}

/* AP 0590:015b */
void servo_init(u16 s)
{
    u8 sh = DSB(s + 0x10);
    DSW(s) = (u16)(DSW(s + 0x0E) >> sh);
    if (DSW(s) == 0) DSW(s) = 1;
    DSW(s + 2) = (u16)(DSW(s) >> sh);
    if (DSW(s + 2) == 0) DSW(s + 2) = 1;
    DSW(s + 4) = (u16)(DSW(s + 0x0E) << sh);
}

/* ---- 05d6 split: weapon camera -------------------------------------------------------------------------- */

/* AP 05d6:036c — remember the last dropped weapon */
void wcam_note_weapon(FarPtr w)
{
    ds_far_wr(0x00F0, w);
    DSL(0x00F4) = DSL(AP_DS_mission_time);
}

/* AP 05d6:0381 -> the proxy object DS:3C6E or 0 */
u16 wcam_object(void)
{
    FarPtr w = ds_far(0x00F0);
    if (far_is_null(w)) return 0;
    if (DSL(AP_DS_mission_time) - DSL(0x00F4) > 0x0C80) return 0;
    u8 *p = far_mp(w);
    u16 f = (u16)(p[0x18] | p[0x19] << 8);
    if (f & 0x8000) return 0;
    f |= 0x8000;
    p[0x18] = (u8)f;
    p[0x19] = (u8)(f >> 8);
    memcpy(DSP(0x3C6E), p, 0x18);
    ds_far_wr(0x3C6A, w);
    DSB(0x3CD2) = 0;
    DSW(0x3C90) = 0x3CCC;
    DSL(0x3CB0) &= DSL(0x3CB4);
    DSL(0x3CB4) = 0;
    return 0x3C6E;
}

/* AP 05d6:03fc */
void wcam_release(void)
{
    FarPtr w = ds_far(0x3C6A);
    u8 *p = far_mp(w);
    u16 f = (u16)((p[0x18] | p[0x19] << 8) & 0x7FFF);
    p[0x18] = (u8)f;
    p[0x19] = (u8)(f >> 8);
    DSL(0x3C6A) = 0;
    DSB(0x3CD2) = 0;
}

/* ---- 637f split: next object for the view key --------------------------------------------------------- */

/* AP 637f:00af */
u16 next_view_object(void)
{
    s16 i = (s16)ap_list_next(DSW(0x3B01));
    s16 start = i, tries = 0;
    for (;;) {
        u16 o = DSW((u16)(DSW(0x475D) + 2 * i));
        if (obj_alive(o) && !flight_is_carrier_group(o) &&
            (DSB(o + 0x18) == 1 ? DSB(o + 0x19) != 0 : DSB(o + 0xBB) == 0x2C))
            break;
        if (++tries >= DSS(0x4778)) {
            i = start;
            if (flight_is_carrier_group(DSW((u16)(DSW(0x475D) + 2 * start))))
                i = (s16)find_int(DSW(0x475B), PLAYER(), DSW(0x4776));
            break;
        }
        i = (s16)ap_list_next((u16)i);
    }
    DSW(0x3B01) = (u16)i;
    return DSW((u16)(DSW(0x475D) + 2 * i));
}

/* ---- 1c30: views -------------------------------------------------------------------------------------- */

/* AP 1c30:0007 views_save(fp). NOTE: the code reads (my_fread) the 10 saved views: it is called by
 * simprefs_load (the modules.csv names of 0007/0020 are swapped). */
void views_save(u16 fp)
{
    my_fread(0x1AB8, 10, 6, fp);
}

/* AP 1c30:0020 views_load(fp): writes the 10 saved views (called by simprefs_save) */
void views_load(u16 fp)
{
    my_fwrite(0x1AB8, 10, 6, fp);
}

/* AP 1c30:0039 trail_record(pl) */
void trail_record(u16 pl)
{
    if (++DSS(0x5E36) >= 100) DSW(0x5E36) = 0;
    s16 wr = DSS(0x5E36);
    memcpy(mp(SEG(TRAIL_SEG), (u16)(TRAIL_POS + 12 * wr)), DSP(pl + 4), 12);
    memcpy(mp(SEG(TRAIL_SEG), (u16)(TRAIL_ANG + 6 * wr)), DSP(pl + 0x10), 6);
    s16 want = (s16)(wr - DSS(0x5E32));
    s16 rd = DSS(0x5E34);
    if (DSS(0x5E34) > wr) rd = (s16)(rd - 100);
    if (want > rd) {
        s16 n = (s16)(rd + 2);
        DSS(0x5E34) = n > want ? want : n;
        if (DSS(0x5E34) < 0) DSS(0x5E34) = (s16)(DSS(0x5E34) + 100);
    }
}

/* AP 1c30:00d0 */
static void trail_set_lag(void)
{
    s16 n = steps_to_frames(0x18);
    DSS(0x5E32) = n < 0x62 ? n : 0x62;
}

/* AP 1c30:00ee */
s16 orbit_min_dist(u16 o)
{
    s16 r = (s16)shape_radius_world((s16)DSW(o + 2));
    s16 d = (s16)(r + (r >> 1));
    return d < 0x190 ? 0x190 : d;
}

/* AP 1c30:04c2 (near) */
static s16 view_attached(void)
{
    return VIEW == 0x18 || VIEW == 0x17 || VIEW_INSIDE;
}

/* AP 1c30:04df (near) */
static void orbit_store(void)
{
    if (view_attached() || WCAM_ACTIVE) return;
    u16 r = DSW((u16)(VIEW_TARGET + 0x22));
    DSS(r) = ORBIT_PITCH;
    DSS(r + 2) = ORBIT_HEADING;
    DSS(r + 4) = ORBIT_DIST;
}

/* AP 1c30:050a (near) */
static void cam_min_alt(u16 pos)
{
    if (DSSL(pos + 8) < 100) DSSL(pos + 8) = 100;
}

/* AP 1c30:0525 (near) orbit_ground_clamp(pitch, dist) */
static s16 orbit_ground_clamp(s16 pitch, s16 dist)
{
    s16 h = 100;
    u16 t = VIEW_TARGET;
    if (DSB(t + 0x18) == 0) {
        s16 g = (s16)(DSW(t + 0xB8) + DSW((u16)(DSW(t + 0x66) + 0x98)));
        if ((s32)g >= DSSL(t + 0x0C)) h = (s16)(h + g);
    }
    s32 above = DSSL(t + 0x0C) - (s32)h;
    if ((s32)dist <= above) return pitch;
    if ((s32)(s16)-dist >= above) return (s16)0xC180;
    s16 a = (s16)asin16((u16)ldiv_sat(above << 14, dist));
    return a >= pitch ? pitch : a;
}

/* AP 1c30:0117 (near) */
static void orbit_pan_begin(void)
{
    set_frame_fn(0x01FC);
    VIEW_FORCE = 1;
    ORBIT_PITCH = DSS(CAM_ANG);
    DSW(CAM_ANG + 2) = 0;
    ORBIT_HEADING = (s16)(DSS(CAM_ANG + 4) - DSS(VIEW_TARGET + 0x14));
}

/* AP 1c30:0143 (body 0144) orbit_pan(r) — near */
static void orbit_pan(u16 r)
{
    if (DSW(r + 2) & 1) {
        ORBIT_DIST = (s16)(ORBIT_DIST + dt_scale_real((s16)(DSW(r + 6) << 5)));
        s16 mn = orbit_min_dist(VIEW_TARGET);
        if (ORBIT_DIST < mn) ORBIT_DIST = mn;
        else if (ORBIT_DIST > 10000) ORBIT_DIST = 10000;
        return;
    }
    s16 a = dt_scale_real((s16)(DSW(r + 4) << 8));
    s16 b = dt_scale_real((s16)(DSW(r + 6) << 8));
    ORBIT_HEADING = (s16)(ORBIT_HEADING + clamp(a, -4000, 4000));
    ORBIT_PITCH = (s16)(ORBIT_PITCH + clamp(b, -4000, 4000));
    ORBIT_PITCH = clamp(ORBIT_PITCH, -16000, 16000);
}

/* place the camera at dist around an object (the tail of 01fc) */
static void orbit_place_around(u16 obj)
{
    ORBIT_PITCH = orbit_ground_clamp(ORBIT_PITCH, ORBIT_DIST);
    memcpy(DSP(CAM_POS), DSP(obj + 4), 12);
    DSW(CAM_ANG) = (u16)ORBIT_PITCH;
    DSW(CAM_ANG + 2) = 0;
    DSW(CAM_ANG + 4) = (u16)(DSW(obj + 0x14) + ORBIT_HEADING);
    dir_scaled(CAM_POS, CAM_ANG, (u16)-ORBIT_DIST);
}

/* AP 1c30:01fc orbit_pan_frame — per-frame fn in orbit-pan mode */
static void orbit_pan_frame(void)
{
    if (view_attached()) {
        orbit_place_around(PLAYER());
    } else {
        orbit_place_around(VIEW_TARGET);
        orbit_store();
    }
}

/* AP 1c30:02ad */
static void view_save_slot(s16 n)
{
    if (VIEW < 6) return;
    u16 s = (u16)(0x1AB8 + 6 * n);
    DSS(s) = ORBIT_PITCH;
    DSS(s + 2) = ORBIT_HEADING;
    DSS(s + 4) = ORBIT_DIST;
}

/* AP 1c30:02d9 (body 02da) view_preset(v) — near */
static void view_preset(s16 v)
{
    if (v < 0x0C || v == 0x19) {
        u16 p = v == 0x19 ? (DEATHCAM_FIRST ? 0x1A88 : 0x1AA0) : (u16)(0x1968 + 0x18 * v);
        DSW(SERVO_PITCH + 0x0E) = DSW(p);
        DSW(SERVO_ROLL + 0x0E) = DSW(p + 2);
        DSW(SERVO_HEADING + 0x0E) = DSW(p + 4);
        DSW(SERVO_DIST + 0x0E) = DSW(p + 6);
        DSB(SERVO_PITCH + 0x10) = DSB(p + 8);
        DSB(SERVO_ROLL + 0x10) = DSB(p + 0x0A);
        DSB(SERVO_HEADING + 0x10) = DSB(p + 0x0C);
        DSB(SERVO_DIST + 0x10) = DSB(p + 0x0E);
        ORBIT_PITCH = DSS(p + 0x10);
        ORBIT_ROLL = DSS(p + 0x12);
        ORBIT_HEADING = DSS(p + 0x14);
        if (v != 0x19) DSS(p + 0x16) = orbit_min_dist(VIEW_TARGET);
        ORBIT_DIST = DSS(p + 0x16);
    } else {
        s16 mn = orbit_min_dist(VIEW_TARGET);
        DSW(SERVO_HEADING + 0x0E) = DSW(SERVO_ROLL + 0x0E) = DSW(SERVO_PITCH + 0x0E) = 5000;
        DSW(SERVO_DIST + 0x0E) = 5000;
        DSB(SERVO_HEADING + 0x10) = DSB(SERVO_ROLL + 0x10) = DSB(SERVO_PITCH + 0x10) = 2;
        DSB(SERVO_DIST + 0x10) = 2;
        if (v == 0x16) {
            u16 r = DSW((u16)(VIEW_TARGET + 0x22));
            ORBIT_PITCH = DSS(r);
            ORBIT_ROLL = 0;
            ORBIT_HEADING = DSS(r + 2);
            ORBIT_DIST = DSS(r + 4);
        } else {
            u16 s = (u16)(0x1A70 + 6 * v);
            ORBIT_PITCH = DSS(s);
            ORBIT_ROLL = 0;
            ORBIT_HEADING = DSS(s + 2);
            ORBIT_DIST = DSS(s + 4) > mn ? DSS(s + 4) : mn;
            DSS(s + 4) = ORBIT_DIST;
        }
    }
    servo_init(SERVO_PITCH);
    servo_init(SERVO_ROLL);
    servo_init(SERVO_HEADING);
    servo_init(SERVO_DIST);
}

/* AP 1c30:0425 (body 0426) servo_snap(s) — near */
static void servo_snap(u16 s)
{
    DSW(s + 6) = DSW(s + 8);
    DSW(s + 0x0A) = 0;
    DSW(s + 0x0C) = 0;
}

/* AP 1c30:0441 (near) */
static void servos_snap(void)
{
    servo_snap(SERVO_PITCH);
    servo_snap(SERVO_ROLL);
    servo_snap(SERVO_HEADING);
    servo_snap(SERVO_DIST);
}

/* AP 1c30:0466 (near) */
static void orbit_servo_place(void)
{
    memcpy(DSP(CAM_POS), DSP(VIEW_TARGET + 4), 12);
    DSW(CAM_ANG) = (u16)servo_step(SERVO_PITCH);
    DSW(CAM_ANG + 2) = (u16)servo_step(SERVO_ROLL);
    DSW(CAM_ANG + 4) = (u16)servo_step(SERVO_HEADING);
    s16 d = servo_step(SERVO_DIST);
    dir_scaled(CAM_POS, CAM_ANG, (u16)-d);
}

/* AP 1c30:05c1 orbit_frame_rel — views 6, 7 */
static void orbit_frame_rel(void)
{
    DSS(SERVO_DIST + 8) = ORBIT_DIST;
    DSS(SERVO_PITCH + 8) = orbit_ground_clamp((s16)(DSS(VIEW_TARGET + 0x10) + ORBIT_PITCH), ORBIT_DIST);
    DSW(SERVO_ROLL + 8) = 0;
    DSW(SERVO_HEADING + 8) = (u16)(DSW(VIEW_TARGET + 0x14) + ORBIT_HEADING);
    orbit_servo_place();
    orbit_store();
}

/* AP 1c30:05fc orbit_frame_abs — views 8..16h, 19h */
static void orbit_frame_abs(void)
{
    DSS(SERVO_DIST + 8) = ORBIT_DIST;
    DSS(SERVO_PITCH + 8) = orbit_ground_clamp(ORBIT_PITCH, ORBIT_DIST);
    DSW(SERVO_ROLL + 8) = 0;
    DSW(SERVO_HEADING + 8) = (u16)(DSW(VIEW_TARGET + 0x14) + ORBIT_HEADING);
    orbit_servo_place();
    cam_min_alt(CAM_POS);
    orbit_store();
}

/* AP 1c30:0638 cam_attach_target — cockpit views 1..5 */
static void cam_attach_target(void)
{
    u16 t = VIEW_TARGET;
    memcpy(DSP(CAM_POS), DSP(t + 4), 12);
    DSW(CAM_ANG) = DSW(t + 0x10);
    DSW(CAM_ANG + 2) = DSW(t + 0x12);
    DSW(CAM_ANG + 4) = DSW(t + 0x14);
}

/* AP 1c30:0668 trail_frame — view 17h */
static void trail_frame(void)
{
    s16 rd = DSS(0x5E34);
    memcpy(DSP(CAM_POS), mp(SEG(TRAIL_SEG), (u16)(TRAIL_POS + 12 * rd)), 12);
    memcpy(DSP(CAM_ANG), mp(SEG(TRAIL_SEG), (u16)(TRAIL_ANG + 6 * rd)), 6);
}

/* AP 1c30:06a3 freecam_frame — view 18h */
static void freecam_frame(void)
{
    DSW(CAM_ANG + 2) = 0;
    cam_min_alt(ladd(CAM_POS, 0x5E3D));
}

/* AP 1c30:06c3 */
s16 view_stub0(void)
{
    return 0;
}

/* AP 1c30:06ca (body 06cb) freecam_pan(r) — near */
static void freecam_pan(u16 r)
{
    memcpy(DSP(0x5E3D), DSP(DSW(0x3714)), 12);
    if (!(DSW(r + 2) & 1)) {
        DSW(CAM_ANG) = (u16)(DSW(CAM_ANG) + dt_scale_real((s16)(DSW(r + 6) << 7)));
        DSW(CAM_ANG + 4) = (u16)(DSW(CAM_ANG + 4) - dt_scale_real((s16)(DSW(r + 4) << 7)));
        return;
    }
    u16 m = stk_mark();
    u16 ang = stk_alloc(6);
    s16 a = (s16)-dt_scale_real((s16)(DSW(r + 6) << 6));
    s16 b = (s16)-dt_scale_real((s16)(DSW(r + 4) << 6));
    dir_scaled(0x5E3D, CAM_ANG, (u16)a);
    DSW(ang) = 0;
    DSW(ang + 2) = 0;
    DSW(ang + 4) = (u16)(DSW(CAM_ANG + 4) + 0x4000);
    dir_scaled(0x5E3D, ang, (u16)b);
    stk_release(m);
}

/* AP 1c30:0780 */
s16 view_get(void)
{
    return VIEW;
}

/* AP 1c30:0986 */
static void view_restore_rrec(void)
{
    DSL(0x1AF4) = 0;
    DSW((u16)(DSW(0x5E49) + 2)) = DSW(0x5E4D);
}

/* AP 1c30:0788 (body 0789) view_set(v, rr, tgt) — near; 1 = (re)set */
static s16 view_set(s16 v, u16 rr, u16 tgt)
{
    if (v == VIEW && !VIEW_FORCE && tgt == VIEW_TARGET && v != 0x18 && v != 0x19) return 0;
    VIEW_FORCE = 0;
    VIEW = v;
    VIEW_TARGET = tgt;
    if (!frame_fn_set()) {
        DSW(0x5E4D) = DSW(rr + 2);
        DSW(0x5E49) = rr;
        memset(DSP(SERVO_HEADING), 0, 0x12);
        memset(DSP(SERVO_PITCH), 0, 0x12);
        memset(DSP(SERVO_ROLL), 0, 0x12);
    }
    if ((u16)v > 0x19) goto done;
    switch (v) {
    case 0:
        VIEW_CLASS = 0;
        if (frame_fn_set()) view_restore_rrec();
        VIEW_INSIDE = 1;
        return 1;
    case 1:
        VIEW_CLASS = 2;
        set_frame_fn(0x0638);
        cam_attach_target();
        VIEW_INSIDE = 1;
        break;
    case 2: case 3: case 4: case 5:
        VIEW_CLASS = 1;
        set_frame_fn(0x0638);
        cam_attach_target();
        VIEW_INSIDE = 1;
        break;
    case 6: case 7:
        VIEW_CLASS = 3;
        view_preset(v);
        orbit_frame_rel();
        servos_snap();
        set_frame_fn(0x05C1);
        VIEW_INSIDE = 0;
        break;
    case 0x19:
        VIEW_CLASS = 3;
        view_preset(v);
        set_frame_fn(0x05FC);
        orbit_frame_abs();
        VIEW_INSIDE = 0;
        if (DEATHCAM_FIRST) DEATHCAM_FIRST = 0;
        break;
    case 0x17:
        VIEW_CLASS = 3;
        VIEW_INSIDE = 0;
        set_frame_fn(0x0668);
        trail_frame();
        break;
    case 0x18:
        VIEW_CLASS = 3;
        VIEW_INSIDE = 0;
        memcpy(DSP(0x5E3D), DSP(DSW(0x3714)), 12);
        set_frame_fn(0x06A3);
        break;
    default:                                                      /* 8..16h */
        VIEW_CLASS = 3;
        view_preset(v);
        set_frame_fn(0x05FC);
        orbit_frame_abs();
        servos_snap();
        VIEW_INSIDE = 0;
        break;
    }
done:
    DSW(rr + 2) = 0x5E4F;
    return 1;
}

/* AP 1c30:099e */
void view_request_reset(void)
{
    VIEW_RESET_REQ = 1;
}

/* AP 1c30:09a9 */
u16 view_outside_target(void)
{
    return VIEW_INSIDE ? 0 : VIEW_TARGET;
}

/* AP 1c30:09bc (near) */
static s16 view_new_frame(void)
{
    return vcr_frame() != DSL(0x1B2C);
}

/* AP 1c30:09d9 view_input_pending(r) */
s16 view_input_pending(u16 r)
{
    s16 p = VIEW_FORCED_CMD >= 0;
    if (DSW(r + 2) & 2) {
        if (!PAN_HELD) p = 1;
        else if ((DSW(r + 4) || DSW(r + 6)) && (VIEW < 0x17 || VIEW == 0x18)) p = 1;
    } else if (!PAN_MOVED || PAN_HELD) {
        p = 1;
    }
    if (DSW(r)) p = 1;
    if (WCAM_IMPACT) p = 1;
    if (DEATHCAM_REQ) {
        p = 1;
        DSW(r) = 0x24;
        DEATHCAM_REQ = 0;
    }
    if (view_new_frame() && VIEW != 0 && VIEW != 0x18) p = 1;
    return p;
}

/* AP 1c30:0a73 view_set_head(rr) */
void view_set_head(u16 rr)
{
    DSW(rr + 6) = 0;
    if (VIEW_CLASS == 1 || VIEW_CLASS == 2) {
        u16 h = (u16)(0x1B2C + 4 * VIEW);
        DSW(rr + 4) = DSW(h);
        DSW(rr + 8) = DSW(h + 2);
    } else {
        DSW(rr + 4) = 0;
        DSW(rr + 8) = 0;
    }
}

/* AP 1c30:0ab4 */
void view_override_set(s16 on)
{
    VIEW_OVERRIDE = (u16)on;
    if (!on) VIEW_FORCED_CMD = -1;
}

/* AP 1c30:0aca */
void view_force_cmd(s16 cmd)
{
    VIEW_FORCED_CMD = cmd;
}

/* AP 1c30:0ad5 view_update(r) -> bit 0 view/target changed, bit 2 camera moved */
s16 view_update(u16 r)
{
    u16 m = stk_mark();
    u16 rec = stk_alloc(8);
    u16 save = stk_alloc(18);
    memcpy(DSP(rec), DSP(r), 8);
    memcpy(DSP(save), DSP(CAM_POS), 12);
    memcpy(DSP(save + 12), DSP(CAM_ANG), 6);
    s16 res = 0;
    u16 obj = PLAYER();
    if (VIEW_OVERRIDE) {
        memset(DSP(rec), 0, 8);
        if (VIEW_FORCED_CMD >= 0) DSS(rec) = VIEW_FORCED_CMD;
    }
    s16 cmd = DSS(rec);
    if (DSW(rec + 2) & 2) {
        if (!PAN_HELD) {
            PAN_HELD = 1;
            PAN_MOVED = 0;
        } else if (DSW(rec + 4) || DSW(rec + 6)) {
            PAN_MOVED = 1;
            if (VIEW < 6 && !(DSW(rec + 2) & 1)) {
                s16 i = 4;
                if (DSS(rec + 4) > 4) i++;
                else if (DSS(rec + 4) < -4) i--;
                if (DSS(rec + 6) > 4) i = (s16)(i + 3);
                else if (DSS(rec + 6) < -4) i = (s16)(i - 3);
                VIEW_REQ = DSC(0x1B0E + (s16)(VIEW_SET * 9) + i);
            } else if (VIEW < 0x17 && VIEW >= 6 && !ORBIT_PAN) {
                orbit_pan_begin();
                ORBIT_PAN = 1;
            }
        }
    } else {
        if (PAN_HELD && ORBIT_PAN) ORBIT_PAN = 0;
        PAN_HELD = 0;
        if (!PAN_MOVED) {
            VIEW_REQ = VIEW < 6 ? 7 : 0;
            VIEW_SET = VIEW_SET == 0;
            PAN_MOVED = 1;
        }
    }
    if (cmd >= 1 && cmd <= 0x0A) {
        msg_show(far_make(SEG(0x3A73), 0), 1, far_make(0, 0));   /* "Current view saved..." */
        view_save_slot((s16)(cmd - 1));
    } else if (cmd >= 0x0B && cmd <= 0x14) {
        VIEW_REQ = (s16)(cmd + 1);
        VIEW_SET = 1;
    } else if (cmd >= 0x15 && cmd <= 0x1B) {
        VIEW_REQ = DSC(0x1AEB + (s16)(VIEW_SET * 7) + cmd);
    } else if (cmd >= 0x1C && cmd <= 0x23) {
        if (cmd <= 0x1F) { VIEW_REQ = (s16)(cmd - 0x1C); VIEW_SET = 0; }
        else { VIEW_REQ = (s16)(cmd - 0x1A); VIEW_SET = 1; }
    } else {
        switch (cmd) {
        case 0x24:
            VIEW_REQ = 0x19;
            VIEW_SET = 1;
            if (!VIEW_INSIDE) obj = VIEW_TARGET;
            break;
        case 0x25: VIEW_REQ = 0; VIEW_SET = 0; break;
        case 0x26: VIEW_REQ = 7; VIEW_SET = 1; break;
        case 0x27:
            if (!view_attached()) {
                u16 o = next_view_object();
                if (o) {
                    obj = o;
                    VIEW_REQ = DSS((u16)(DSW(o + 0x22) + 4)) > 0 ? 0x16 : 7;
                }
            }
            break;
        case 0x28:
            if (!VIEW_INSIDE) VIEW_REQ = 0x18;
            break;
        case 0x29:
            if (!VIEW_INSIDE) VIEW_REQ = DSS((u16)(DSW(obj + 0x22) + 4)) > 0 ? 0x16 : 7;
            break;
        case 0x2A:
            if (WCAM_ACTIVE) {
                VIEW_REQ = -2;
            } else {
                u16 w = wcam_object();
                if (w) {
                    DSW(0x5E38) = VIEW_TARGET;
                    if (view_attached()) DSW(0x5E3A) = (u16)VIEW;
                    else { DSW(0x5E3A) = 0xFFFF; orbit_store(); }
                    obj = w;
                    VIEW_REQ = 7;
                    WCAM_ACTIVE = 1;
                }
            }
            break;
        default:
            break;
        }
    }
    if (VIEW_RESET_REQ) {
        VIEW_REQ = 0;
        VIEW_RESET_REQ = 0;
        VIEW_SET = 0;
    } else if (WCAM_ACTIVE) {
        if (VIEW_REQ >= 0 && cmd != 0x2A) {
            WCAM_ACTIVE = 0;
        } else if (WCAM_IMPACT) {
            VIEW_REQ = 0x19;
            VIEW_SET = 1;
            obj = 0x3C6E;
            DSL(0x3C7A) = 100;
            WCAM_IMPACT = 0;
        } else if (VIEW_REQ == -2 || (VIEW_REQ == -1 && (DSB(VIEW_TARGET + 0x42) & 4))) {
            wcam_release();
            obj = DSW(0x5E38);
            VIEW_REQ = DSS(0x5E3A) == -1 ? 0x16 : DSS(0x5E3A);
            WCAM_ACTIVE = 0;
        }
    } else if (!view_attached() && (DSB(VIEW_TARGET + 0x42) & 4)) {
        obj = PLAYER();
        VIEW_REQ = DSS((u16)(DSW(obj + 0x22) + 4)) > 0 ? 0x16 : 7;
    }
    if (VIEW_REQ != -1) {
        ORBIT_PAN = 0;
        if (view_set(VIEW_REQ, DSW(0x43D8), obj)) res = 1;
    }
    if (ORBIT_PAN) orbit_pan(rec);
    else if (VIEW == 0x18) freecam_pan(rec);
    if (frame_fn_set()) codeptr_lookup(ds_far(0x1AF4))();
    VIEW_REQ = -1;
    DSL(0x1B2C) = vcr_frame();
    if (memcmp(DSP(save), DSP(CAM_POS), 12) != 0 || memcmp(DSP(save + 12), DSP(CAM_ANG), 6) != 0) res |= 4;
    view_set_head(DSW(0x43D8));
    VIEW_FORCED_CMD = -1;
    stk_release(m);
    return res;
}

/* AP 1c30:0f68 */
void views_init(void)
{
    DEATHCAM_FIRST = 1;
    memcpy(mp(SEG(TRAIL_SEG), TRAIL_POS), DSP(PLAYER() + 4), 12);
    SEGW(TRAIL_SEG, TRAIL_ANG) = DSW(PLAYER() + 0x10);
    SEGW(TRAIL_SEG, TRAIL_ANG + 2) = DSW(PLAYER() + 0x12);
    SEGW(TRAIL_SEG, TRAIL_ANG + 4) = DSW(PLAYER() + 0x14);
    DSW(0x5E36) = 0;
    DSW(0x5E34) = 0;
    trail_set_lag();
    DSW(0x5EB7) = 0;
    VIEW = 0;
    DSW(0x5EB9) = 1;
    DSL(0x1AF4) = 0;
    VIEW_SET = 0;
    PAN_MOVED = 1;
    PAN_HELD = 0;
    ORBIT_PAN = 0;
    VIEW_CLASS = 0;
    VIEW_INSIDE = 1;
    DSL(0x1B2C) = 0xFFFFFFFFu;
    DSW(0x19F4) = 0;
    VIEW_OVERRIDE = 0;
    VIEW_FORCED_CMD = -1;
    VIEW_TARGET = 0;
    WCAM_ACTIVE = 0;
}

/* AP 1c30:1021 */
u8 wcam_is_active(void)
{
    return WCAM_ACTIVE;
}

__attribute__((constructor)) static void views_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x1C3001FCu, orbit_pan_frame);
    codeptr_register(PROG_AP, 0x1C3005C1u, orbit_frame_rel);
    codeptr_register(PROG_AP, 0x1C3005FCu, orbit_frame_abs);
    codeptr_register(PROG_AP, 0x1C300638u, cam_attach_target);
    codeptr_register(PROG_AP, 0x1C300668u, trail_frame);
    codeptr_register(PROG_AP, 0x1C3006A3u, freecam_frame);
}
