/* sim_world part A developer aid (not game code): AOTP_TEST=fm | ballistics, run in AP (--start 3).
 *
 *   fm          loads an aircraft .FMD (AOTP_FMD, default F4U) through the volume layer, sets up one Plane
 *               record in DGROUP and flies a scripted flight through the ported flight model (fm_step,
 *               realistic mode 3 and the AI model 4): level cruise, climb, turn, stall, belly landing.
 *               Prints the state per frame (AOTP_FM_EVERY, default 8) and checks it against the spec's
 *               formulas (sim_world_fm.md §3): equilibrium speed = fm_cruise_speed, per-frame velocity
 *               = v·70/1024, roll-rate target, stall flag vs AoA, half-step split (DS:711E=200h once ==
 *               100h twice, byte for byte), landing damage level.
 *   ballistics  (weapons.c) see swa_test_wpn below.
 *
 * Entry: sim_world_a_dev_test() — -1 when AOTP_TEST names none of these. Called through
 * `-Wl,--wrap=platform_dev_test` in the module's own build (SWA_WRAP_DEVTEST) until platform calls it. */
#include "sim_world/swa_priv.h"
#include "platform/rtl_priv.h"
#include "prog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static u16 ds_cstr(const char *s) { u16 p = stk_alloc((u16)(strlen(s) + 1)); memcpy(DSP(p), s, strlen(s) + 1); return p; }

static u16 load_file(const char *name, u16 size)
{
    u16 m = stk_mark();
    u16 f = my_fopen(ds_cstr(name), ds_cstr("rb"));
    stk_release(m);
    if (!f) return 0;
    u16 buf = stk_alloc(size);
    memset(DSP(buf), 0, size);
    s16 n = my_fread(buf, 1, (s16)size, f);
    my_fclose(f);
    return n > 0 ? buf : 0;
}

/* test plane: Plane block + matrix cache; returns p */
static u16 make_plane(u16 fmd, s32 alt, s16 thr)
{
    u16 p = stk_alloc(0x108), m = stk_alloc(0x2C), blk = stk_alloc(8);
    memset(DSP(p), 0, 0x108); memset(DSP(m), 0, 0x2C); memset(DSP(blk), 0, 8);
    OW(p, 0x16) = blk;
    OW(p, 0x20) = m;
    OW(m, 0x28) = (u16)(p + 0x10);           /* angles of the matrix cache */
    OSL(p, 0x04) = 0; OSL(p, 0x08) = 0; OSL(p, 0x0C) = alt;
    OW(p, 0x10) = 0; OW(p, 0x12) = 0; OW(p, 0x14) = 0;
    OW(p, 0xBB) = 0x2C;                      /* the player's pilot index */
    fm_init((u16)(p + 0x64), (u16)(p + 2), fmd, m, (u16)0xE6);
    OS(p, 0xAC + 6) = thr;                   /* throttle */
    OW(p, 0xAC + 0x0A) = 0x400;              /* mass (the FM's force divisor; 400h as mode 1 uses) */
    OW(p, 0xAC + 0x0C) = 0;                  /* ground height */
    OB(p, 0xAC + 0x0E) = 0;                  /* gear up, no flaps */
    return p;
}

static void print_state(int fr, u16 p, const char *tag)
{
    u16 fm = (u16)(p + 0x64);
    printf("%-6s %5d  pos %8ld %8ld %7ld  pit %6d rol %6d hdg %5u  vb %5d %5d %5d  vw %5d %5d %5d  "
           "a %6d rr %5d pr %5d fl %02X dmg %d\n", tag, fr, (long)OSL(p, 4), (long)OSL(p, 8), (long)OSL(p, 0x0C),
           OS(p, 0x10), OS(p, 0x12), OW(p, 0x14), OS(fm, 0x2E), OS(fm, 0x30), OS(fm, 0x32),
           OS(fm, 0x34), OS(fm, 0x36), OS(fm, 0x38), OS(fm, 0x16), OS(fm, 0x12), OS(fm, 0x14),
           OB(fm, 0x46), OC(fm, 0x47));
}

/* one frame of the model as plane_fly runs it (mode: 3 realistic, 4 AI) */
static void step(u16 p, s16 mode)
{
    obj_matrix(OW(p, 0x20));
    s16 coord = 0x400;
    fm_step((u16)(p + 0x64), (u16)(p + 0xAC), mode, coord, 0, 0);
}

/* scripted pilot: hold a vertical speed with the elevator, damped by the pitch rate (test aid) */
static void hold_vz(u16 p, s16 vz_target)
{
    s32 e = (s32)(vz_target - OS(p, 0x64 + 0x38)) / 2 - OS(p, 0x64 + 0x14) / 8;
    if (e > 0x100) e = 0x100;
    if (e < -0x100) e = -0x100;
    OS(p, 0xAC + 2) = (s16)e;
}

/* spec formula (sim_world_fm.md §3.2 steps 1-2): roll-rate target for aileron a at the plane's altitude */
static s16 roll_target(u16 p, u16 fmd, s16 a, s16 mode)
{
    s32 z = OSL(p, 0x0C);
    s16 band = alt_band(z);
    if (band >= 8) band--;
    s16 w = (s16)(DSSL(0x1446 + band * 4) - DSSL(0x1442 + band * 4));
    s16 y0 = OS(fmd, 0x62 + band * 2), y1 = OS(fmd, 0x64 + band * 2);
    s16 e = (s16)((s32)(s16)z * (y1 - y0) / w + y0);
    s16 c0 = (s16)(((s32)a * e) >> 10);
    s16 v = OS(p, 0x64 + 0x30);
    if (OS(fmd, 0xC6) != 0 && v > OS(fmd, 0xC6)) c0 >>= 2;                   /* aileron limit speed */
    if (mode != 4 && OS(fmd, 0xC0) != 0 && v > OS(fmd, 0xC0)) c0 >>= 2;      /* all controls limit */
    if (c0 > 0x100) c0 = 0x100;
    s16 b = (s16)(((s32)c0 * OS(fmd, 0x82)) >> 10);
    if (c0 <= 0) b = (s16)-b;
    return (s16)(((s32)(c0 + b) * OS(fmd, 0x7C)) >> 8);
}

static int every;

static void fm_flight(u16 fmd, s16 mode, const char *name)
{
    u16 mk = stk_mark();
    printf("--- %s, flight model mode %d ---\n", name, mode);
    DT = 0x100;
    int fr = 0;
    /* 0. zero gravity, no controls, full throttle: the body forward speed must settle where thrust =
     *    forward drag, i.e. fm_cruise_speed (FMD+28 << 10 / interp(FMD+2C)) */
    {
        u16 m0 = stk_mark();
        u16 p = make_plane(fmd, 20000, 0x100);
        u16 fm = (u16)(p + 0x64);
        OW(fm, 0x30) = 1000;
        DSW(0x3F24) = 0;
        for (int i = 0; i < 2500; i++) step(p, mode);
        DSW(0x3F24) = 0x42;
        s16 v_eq = (s16)fm_cruise_speed(fmd, (u32)OSL(p, 0x0C));
        s16 v = OS(fm, 0x30);
        CHECK(v >= v_eq - v_eq / 50 && v <= v_eq + v_eq / 50, "zero-g equilibrium speed %d = fm_cruise_speed(z=%ld) %d (thrust = drag)",
              v, (long)OSL(p, 0x0C), v_eq);
        stk_release(m0);
    }
    u16 p = make_plane(fmd, 20000, 0x100);
    u16 fm = (u16)(p + 0x64);
    printf("  fm_cruise_speed(20000) = %d (FMD+28 thrust %d)\n", (s16)fm_cruise_speed(fmd, 20000), OS(fmd, 0x28));
    /* 1. level cruise at full throttle */
    for (int i = 0; i < 1500; i++, fr++) {
        hold_vz(p, 0);
        step(p, mode);
        if (fr % every == 0) print_state(fr, p, "cruise");
    }
    CHECK(OS(fm, 8) == (s16)(((s32)OS(fm, 0x30) * 70) >> 10) && OS(fm, 0x0C) == (s16)(((s32)OS(fm, 0x34) * 70) >> 10),
          "per-frame velocity = v*70/1024 (%d, %d)", OS(fm, 8), OS(fm, 0x0C));
    {   /* position advance: dt(trapezoid of the world velocity)·70/1024, 10-bit remainder carried */
        s32 y0 = OSL(p, 8);
        s16 vy0 = OS(fm, 0x36);
        DT = 0x80;                            /* below 100h: one integration step per frame */
        hold_vz(p, 0); step(p, mode); fr++;
        DT = 0x100;
        s32 dy = OSL(p, 8) - y0;
        s32 expect = ((s32)(s16)((((s32)(s16)((s16)(OS(fm, 0x36) + vy0) >> 1)) * 0x80) >> 8) * 70) >> 10;
        CHECK(dy >= expect && dy <= expect + 1, "position step %ld = dt((vy0+vy1)/2)*70/1024 = %ld (+carry)", (long)dy, (long)expect);
    }
    /* 2. the half-step split: DS:711E >= 100h runs fm_full twice with DS:711E/2, so one frame at 100h
     *    must equal two frames at 80h byte for byte (realistic model); the AI model never splits */
    {
        static u8 save_p[0x108], save_m[0x2C], out1[0x108];
        memcpy(save_p, DSP(p), 0x108); memcpy(save_m, DSP(OW(p, 0x20)), 0x2C);
        DT = 0x100; step(p, mode);
        memcpy(out1, DSP(p), 0x108);
        memcpy(DSP(p), save_p, 0x108); memcpy(DSP(OW(p, 0x20)), save_m, 0x2C);
        DT = 0x80; step(p, mode); step(p, mode);
        int nd = 0;
        for (int k = 0; k < 0x108; k++) if (out1[k] != DSB(p + k)) nd++;
        if (mode == 4) CHECK(nd != 0, "AI model: one frame at 100h != two at 80h (no split, %d bytes differ)", nd);
        else CHECK(nd == 0, "half-step split: one frame at 100h == two frames at 80h (%d bytes differ)", nd);
        memcpy(DSP(p), save_p, 0x108); memcpy(DSP(OW(p, 0x20)), save_m, 0x2C);
        DT = 0x100;
    }
    /* 3. climb */
    s32 z0 = OSL(p, 0x0C);
    for (int i = 0; i < 400; i++, fr++) {
        hold_vz(p, 60);
        step(p, mode);
        if (fr % every == 0) print_state(fr, p, "climb");
    }
    CHECK(OSL(p, 0x0C) > z0 + 1000, "climb: altitude %ld -> %ld, vz %d", (long)z0, (long)OSL(p, 0x0C), OS(fm, 0x38));
    /* 4. turn: aileron 80h until 45 degrees of bank; the roll rate must reach the spec target */
    u16 h0 = OW(p, 0x14);
    int hit = 0; s16 last_tgt = 0, max_rr = 0;
    for (int i = 0; i < 600; i++, fr++) {
        s16 ail = OS(p, 0x12) < 0x2000 ? 0x80 : 0;
        OS(p, 0xAC) = ail;
        s16 tgt = roll_target(p, fmd, ail, mode);
        hold_vz(p, 0);
        step(p, mode);
        if (ail && OS(fm, 0x12) == tgt) hit++;
        if (ail) last_tgt = tgt;
        if (OS(fm, 0x12) > max_rr) max_rr = OS(fm, 0x12);
        if (fr % every == 0) print_state(fr, p, "turn");
    }
    if (mode == 4)   /* fixed acceleration: reaches the target exactly */
        CHECK(hit > 0 && max_rr <= last_tgt, "roll rate reached the spec target %d (%d frames at it, peak %d)", last_tgt, hit, max_rr);
    else             /* proportional approach: never beyond the target */
        CHECK(max_rr > last_tgt / 2 && max_rr <= last_tgt, "roll rate peak %d approaches the spec target %d (%d frames at it)", max_rr, last_tgt, hit);
    CHECK(OW(p, 0x14) != h0, "turn: heading %u -> %u, roll %d", h0, OW(p, 0x14), OS(p, 0x12));
    for (int i = 0; i < 300; i++, fr++) {   /* roll out */
        OS(p, 0xAC) = (s16)(-OS(p, 0x12) >> 6);
        hold_vz(p, 0);
        step(p, mode);
        if (fr % every == 0) print_state(fr, p, "level");
    }
    OS(p, 0xAC) = 0;
    /* 5. stall: engine off, full up elevator */
    OS(p, 0xAC + 6) = 0;
    int stalled_at = -1;
    for (int i = 0; i < 1500 && stalled_at < 0; i++, fr++) {
        OS(p, 0xAC + 2) = 0x100;
        step(p, mode);
        if (fr % every == 0) print_state(fr, p, "stall");
        if (OB(fm, 0x46) & 1) { stalled_at = fr; print_state(fr, p, "STALL"); }
    }
    CHECK(stalled_at >= 0 && abs16(OS(fm, 0x16)) > OS(fmd, 2), "stall flag at frame %d with |alpha| %d > FMD+02 %d",
          stalled_at, abs16(OS(fm, 0x16)), OS(fmd, 2));
    /* 6. belly landing: descend onto the ground (gear up) */
    OW(p, 0xAC + 0x0C) = (u16)(OSL(p, 0x0C) - 3000);
    OS(p, 0xAC + 6) = 0x40;
    DSB(0x5764) = 0;
    int landed = -1;
    for (int i = 0; i < 3000 && landed < 0; i++, fr++) {
        hold_vz(p, -20);
        step(p, mode);
        if (fr % every == 0) print_state(fr, p, "land");
        if (OSL(p, 0x0C) <= (s32)(s16)(OS(fmd, 0x98) + OS(p, 0xAC + 0x0C))) { landed = fr; print_state(fr, p, "GROUND"); }
    }
    CHECK(landed >= 0 && OC(fm, 0x47) >= 1 && DSB(0x5764) == 1,
          "ground contact at frame %d: landing damage level %d (gear up => >= 1), belly latch %d", landed, OC(fm, 0x47), DSB(0x5764));
    CHECK(OSL(p, 0x0C) == (s32)(s16)(OS(fmd, 0x98) + OS(p, 0xAC + 0x0C)) && OS(fm, 0x38) >= 0, "on the ground: z = gear height + ground, vz %d >= 0", OS(fm, 0x38));
    stk_release(mk);
}

static int test_fm(void)
{
    const char *list = getenv("AOTP_FMD");
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list && *list ? list : "F4U,ZERO_22,B17_D");
    const char *ev = getenv("AOTP_FM_EVERY");
    every = ev && *ev ? atoi(ev) : 8;
    if (every < 1) every = 1;
    DSW(0x3F22) = 0x46;                      /* scenario_module_init's constants */
    DSW(0x3F24) = 0x42;
    DSW(0x576B) = 1;                         /* overspeed warnings on */
    DT = 0x100;
    {   /* dt helper sanity: the time base must be the specified one, not an identity stub */
        s16 r = dt_scale(0x100);
        DT = 0x80;
        s16 r2 = dt_scale(0x100);
        DT = 0x100;
        if (r != 0x100 || r2 != 0x80) { printf("dt_scale is a stub returning its argument: link order problem\n"); return 4; }
    }
    printf("alt bands DS:1442:");
    for (int i = 0; i < 9; i++) printf(" %ld", (long)DSSL(0x1442 + 4 * i));
    printf("\n");
    for (char *tok = strtok(buf, " ,"); tok; tok = strtok(NULL, " ,")) {
        u16 mk = stk_mark();
        char nm[32];
        snprintf(nm, sizeof nm, "%s.FMD", tok);
        u16 fmd = load_file(nm, 0xD6);
        if (!fmd) { printf("%s not found\n", nm); fails++; stk_release(mk); continue; }
        printf("%s: stall AoA %d, thrust %d, engines %d, gear height %d, Vne %d, ceiling %ld\n", nm, OS(fmd, 2),
               OS(fmd, 0x28), OS(fmd, 0x90), OS(fmd, 0x98), OS(fmd, 0xAE), (long)OSL(fmd, 0xB0));
        fm_flight(fmd, 3, tok);
        fm_flight(fmd, 4, tok);
        stk_release(mk);
    }
    printf("AOTP_TEST=fm: %d failure(s)\n", fails);
    return fails ? 4 : 0;
}

/* ---- ballistics: guns (gun_fire → bullet_spawn → bullet_update through the ordnance pool) and player bombs
 * (bomb_release → bomb_update), checked per frame against sim_world_wpn.md §3.2–3.4 / §4.1–4.2. ---------- */

static u16 load_mec(const char *name, u16 recsize)
{
    u16 buf = load_file(name, 0x400);
    if (!buf) return 0;
    printf("%s: %u records of %u bytes\n", name, DSW(buf), recsize);
    return (u16)(buf + 2);
}

static FarPtr wpn_next(FarPtr o) { u32 v = pool_next(DSW(0x00E6), o); return far_make((u16)(v >> 16), (u16)v); }

static int weapons_alive(void)
{
    int n = 0;
    for (FarPtr o = wpn_next(far_make(0, 0)); !far_is_null(o); o = wpn_next(o)) n++;
    return n;
}

static void test_guns(u16 fmd)
{
    printf("--- guns ---\n");
    u16 p = make_plane(fmd, 20000, 0x100);
    PLAYER = 0;                                        /* an AI shooter: rounds are consumed */
    OW(p, 0x14) = 0x2000;                              /* heading 45 degrees */
    OW(p, 0x10) = 0x0800;                              /* pitch 11.25 degrees up */
    u16 gl = stk_alloc(4 + 0x14), st = stk_alloc(0x0C), src = stk_alloc(6);
    memset(DSP(gl), 0, 4 + 0x14); memset(DSP(st), 0, 0x0C); memset(DSP(src), 0, 6);
    DSW(gl) = 1;
    DSW(gl + 2) = 0x2000;                              /* group A, a pair firing alternately */
    DSW(gl + 4) = 0;                                   /* bullet type 0 */
    DSW(gl + 6) = 1;                                   /* bullets per round (+2 of the record) */
    DSW(gl + 0x0A) = 100;                              /* gun offset x (mirrored for the other side) */
    OW(p, 0x5E) = gl; OW(p, 0x60) = st;
    DSW(st) = 100;                                     /* rounds */
    DSW(st + 0x0A) = 0x8000;                           /* first shot from side 0 */
    DSW(src) = p;                                      /* owner; src+2 flags 0, src+4 = 0: no object hits */
    u16 g = DSW(0x1346);
    s16 speed = DSS(g + 8);
    printf("bullet type 0: damage %d, refire %d, speed %d, life %d, range %ld\n", DSS(g + 4), DSS(g + 6), speed,
           DSS(g + 0x0A), (long)DSSL(g + 0x0C));
    DSL(0x4086) = 100000;                              /* mission clock */
    s16 r0 = DSS(st);
    u16 fired = gun_fire(1, p, (u16)(p + 0x10), src);
    CHECK(fired == 1 && weapons_alive() == 1, "gun_fire: one bullet spawned (fired %u, alive %d)", fired, weapons_alive());
    CHECK(DSS(st) == r0 - DSS(0x134E), "rounds %d -> %d (DS:134E[0] = %d per burst)", r0, DSS(st), DSS(0x134E));
    CHECK(DSW(st + 0x0A) == 0x4000 && DSW(0x1360) == 1, "side toggled (st+0A %04X), DS:1360 = %u", DSW(st + 0x0A), DSW(0x1360));
    u16 again = gun_fire(1, p, (u16)(p + 0x10), src);
    CHECK(again == 0 && weapons_alive() == 1, "refire interval: a second shot at the same clock is refused");
    FarPtr b = wpn_next(far_make(0, 0));
    s16 dx = FS(b, 0x22), dy = FS(b, 0x24), dz = FS(b, 0x26);
    printf("bullet: pos %ld %ld %ld (plane %ld %ld %ld)  angles %04X %04X %04X  dir %d %d %d  timer %d\n",
           (long)FSL(b, 4), (long)FSL(b, 8), (long)FSL(b, 0x0C), (long)OSL(p, 4), (long)OSL(p, 8), (long)OSL(p, 0x0C),
           FW(b, 0x10), FW(b, 0x12), FW(b, 0x14), dx, dy, dz, FS(b, 0x28));
    s32 len2 = (s32)dx * dx + (s32)dy * dy + (s32)dz * dz;
    CHECK(len2 > 0x0FF00000L && len2 < 0x10100000L, "dir is a unit vector << 14 (|d|^2 = %ld)", (long)len2);
    CHECK(dx < 0 && dy > 0 && dz > 0 && abs16((s16)(-dx - dy)) < 64, "dir points at heading 45 (x<0, y>0), pitch up");
    /* per frame through ordnance_update (class dispatch via codeptr), at two frame rates */
    s16 dts[2] = { 0x100, 0xC0 };
    int frames = 0, bad = 0;
    for (int k = 0; k < 2; k++) {
        DT = dts[k];
        for (int i = 0; i < 4; i++, frames++) {
            s32 x0 = FSL(b, 4), y0 = FSL(b, 8), z0 = FSL(b, 0x0C);
            s16 t0 = FS(b, 0x28);
            ordnance_update();
            s16 ds = dt_scale(speed);
            s32 ex = ((s32)dx * ds) >> 14, ey = ((s32)dy * ds) >> 14, ez = ((s32)dz * ds) >> 14;
            s32 mx = FSL(b, 4) - x0, my = FSL(b, 8) - y0, mz = FSL(b, 0x0C) - z0;
            int ok = mx == ex && my == ey && mz == ez && FS(b, 0x28) == (s16)(t0 - DT < 0 ? 0 : t0 - DT) &&
                     FS(b, 0x1C) == (s16)-mx && FS(b, 0x1E) == (s16)-my && FS(b, 0x20) == (s16)-mz;
            if (!ok) bad++;
            printf("  frame %d dt %03X: step %ld %ld %ld (dir*dt(speed)>>14 = %ld %ld %ld)  timer %d -> %d  +1C %d %d %d%s\n",
                   frames, DT, (long)mx, (long)my, (long)mz, (long)ex, (long)ey, (long)ez, t0, FS(b, 0x28),
                   FS(b, 0x1C), FS(b, 0x1E), FS(b, 0x20), ok ? "" : "  <- differs");
        }
    }
    CHECK(bad == 0, "bullet steps = dir*dt(speed)>>14, timer -= DS:711E, +1C = old - new (%d frames, %d mismatches)", frames, bad);
    DT = 0x100;
    s16 t = FS(b, 0x28);
    int n = 0;
    while (weapons_alive() && n < 1000) { ordnance_update(); n++; }
    int expect = (t + 0xFF) / 0x100 + 1;
    CHECK(weapons_alive() == 0 && n == expect, "bullet deleted when its timer runs out: %d more frames (expected %d)", n, expect);
    /* second shot: the other gun of the pair */
    DSL(0x4086) += 1000;
    fired = gun_fire(1, p, (u16)(p + 0x10), src);
    CHECK(fired == 1 && DSW(0x1360) == 2 && DSW(st + 0x0A) == 0x8000, "second shot from side 1 (DS:1360 = %u)", DSW(0x1360));
    while (weapons_alive()) ordnance_update();
}

/* spec model of a player bomb (sim_world_wpn.md §4.2, with the cdq sign quirk of 0688:0557) */
typedef struct { s32 x, y, z; s16 vx, vy, vz, g, pitch; } BombModel;
static void bomb_model_step(BombModel *m)
{
    s32 eax = (s32)m->g * -0x2000;
    m->pitch = ldiv_sat(eax, 0x3C);
    if ((s16)(eax < 0 ? -m->vx : m->vx) > 2) m->vx = (s16)(m->vx - (m->vx >> 3));
    if ((s16)(eax < 0 ? -m->vy : m->vy) > 2) m->vy = (s16)(m->vy - (m->vy >> 3));
    m->vz = (s16)(m->vz - m->g);
    if (m->g <= 0x3C) m->g = (s16)(m->g + 0x0D);
    m->x += dt_scale(m->vx); m->y += dt_scale(m->vy); m->z += dt_scale(m->vz);
}

static void test_bombs(u16 fmd)
{
    printf("--- bombs (player) ---\n");
    u16 p = make_plane(fmd, 20000, 0x100);
    PLAYER = p;
    OW(p, 0x14) = 0x4000;
    u16 stick = stk_alloc(4), src = stk_alloc(0x16);
    memset(DSP(src), 0, 0x16);
    DSB(stick) = 1; DSB(stick + 1) = 5;                /* one 1000 lb bomb */
    DSW(src) = p;
    OS(p, 0x70) = -120; OS(p, 0x72) = 200; OS(p, 0x74) = 25;   /* world velocity per frame (FmState +0C) */
    for (int i = 0; i < 0x38; i++) DSW((u16)(RNG_ST + 2 * i)) = (u16)(0x1234 + i * 0x0F31);
    DSB(RNG_ST + 0x70) = 0; DSB(RNG_ST + 0x71) = 0x18;
    u16 rng_copy = stk_alloc(0x72);
    memcpy(DSP(rng_copy), DSP(RNG_ST), 0x72);
    bomb_release(stick, (u16)(p + 4), OW(p, 0x20), src);
    FarPtr b = wpn_next(far_make(0, 0));
    /* the expected gravity step: rng_mod(1) draws nothing, then (rng & 7) + 0Dh */
    u16 save = stk_alloc(0x72);
    memcpy(DSP(save), DSP(RNG_ST), 0x72);
    memcpy(DSP(RNG_ST), DSP(rng_copy), 0x72);
    u16 r = ap_rng_next(RNG_ST);
    memcpy(DSP(RNG_ST), DSP(save), 0x72);
    BombModel m = { FSL(b, 4), FSL(b, 8), FSL(b, 0x0C), -120, 200, -25, (s16)((r & 7) + 0x0D), 0 };
    printf("bomb: kind %04X shape %u flags %04X weight %04X  pos %ld %ld %ld  v %d %d %d  g %d\n", FW(b, 0), FW(b, 2),
           FW(b, 0x18), FW(b, 0x1A), (long)FSL(b, 4), (long)FSL(b, 8), (long)FSL(b, 0x0C), FS(b, 0x1C), FS(b, 0x1E),
           FS(b, 0x20), FS(b, 0x22));
    CHECK(FS(b, 0x1C) == -120 && FS(b, 0x1E) == 200 && FS(b, 0x20) == -25 && FS(b, 0x22) == m.g,
          "release: velocity = plane velocity with vz = -|vz|, g = (rng & 7) + 0Dh = %d", m.g);
    CHECK((FW(b, 0x18) & 0x6000) == 0x6000 && FW(b, 0x1A) == (DSW((u16)(DSW(0x1342) + 5 * 10 + 2)) | 0x0F),
          "flags: first frame 2000h + lead 4000h; +1A = MEC weight | 0Fh");
    s32 x0 = FSL(b, 4);
    ordnance_update();                                 /* first frame: only clears 2000h */
    CHECK(FSL(b, 4) == x0 && !(FW(b, 0x18) & 0x2000), "first frame skipped");
    int bad = 0, fr = 0;
    s16 dts[3] = { 0x100, 0x80, 0x1C2 };
    for (;;) {
        DT = dts[(fr / 16) % 3];
        BombModel n = m;
        bomb_model_step(&n);
        if (n.z <= 0 || fr >= 600) break;              /* stop before the impact (no world loaded) */
        m = n;
        ordnance_update();
        int ok = FSL(b, 4) == m.x && FSL(b, 8) == m.y && FSL(b, 0x0C) == m.z && FS(b, 0x1C) == m.vx &&
                 FS(b, 0x1E) == m.vy && FS(b, 0x20) == m.vz && FS(b, 0x22) == m.g && FS(b, 0x10) == m.pitch;
        if (!ok) bad++;
        if (fr % every == 0 || !ok)
            printf("  frame %3d dt %03X: pos %ld %ld %ld  v %d %d %d g %d pitch %d%s\n", fr, DT, (long)FSL(b, 4),
                   (long)FSL(b, 8), (long)FSL(b, 0x0C), FS(b, 0x1C), FS(b, 0x1E), FS(b, 0x20), FS(b, 0x22), FS(b, 0x10),
                   ok ? "" : "  <- model differs");
        fr++;
    }
    DT = 0x100;
    CHECK(bad == 0 && fr > 10, "%d frames of fall (pos, v, g, pitch = satdiv(g*-2000h, 60)) match the spec model (%d mismatches)", fr, bad);
    CHECK(m.vx >= -2 && m.vx < 0 && m.vy == 200 && m.g > 0x3C, "horizontal: vx %d decayed (negative), vy %d kept (sign quirk); g capped %d",
          m.vx, m.vy, m.g);
    PLAYER = 0;
    while (weapons_alive()) { FarPtr o = wpn_next(far_make(0, 0)); ap_pool_free_3546(DSW(0x00E6), o.off, o.seg); }
}

int swa_test_ballistics(void)
{
    arena_init(0x3000);
    weapons_init(40);
    DSW(0x1346) = load_mec("BULLETS.MEC", 16);
    DSW(0x1348) = weapon_class_register(far_make(SEG(0x0769), 0x0637), far_make(SEG(0x0769), 0x08A3));
    DSW(0x1342) = load_mec("BOMBS.MEC", 10);
    DSW(0x1344) = weapon_class_register(far_make(SEG(0x0688), 0x04E3), far_make(0, 0));
    if (!DSW(0x1346) || !DSW(0x1342)) { printf("MEC files not found\n"); return 4; }
    DSW(0x474D) = 0;
    DSW(0x4776) = 0;                                   /* no planes in the object list (bomb blast) */
    u16 fmd = load_file("F4U.FMD", 0xD6);
    if (!fmd) { printf("F4U.FMD not found\n"); return 4; }
    DSW(0x3F22) = 0x46; DSW(0x3F24) = 0x42;
    DT = 0x100;
    test_guns(fmd);
    test_bombs(fmd);
    printf("AOTP_TEST=ballistics: %d failure(s)\n", fails);
    return fails ? 4 : 0;
}

int sim_world_a_dev_test(void)
{
    const char *t = getenv("AOTP_TEST");
    if (!t || (strcmp(t, "fm") && strcmp(t, "ballistics"))) return -1;
    if (prog_current_id() != PROG_AP) { printf("AOTP_TEST=%s: run AP (--start 3)\n", t); return 4; }
    DSW(0x1EBC) = 0x0583;                    /* as platform_dev_test does for AP */
    vol_init(0, 0, 0);
    const char *ev = getenv("AOTP_FM_EVERY");
    every = ev && *ev ? atoi(ev) : 8;
    if (every < 1) every = 1;
    int r = !strcmp(t, "fm") ? test_fm() : swa_test_ballistics();
    fflush(stdout);
    return r;
}

#ifdef SWA_WRAP_DEVTEST
int __real_platform_dev_test(void);
int __wrap_platform_dev_test(void)
{
    int r = sim_world_a_dev_test();
    if (r >= 0) { fflush(stdout); exit(r); }
    return __real_platform_dev_test();
}
#endif
