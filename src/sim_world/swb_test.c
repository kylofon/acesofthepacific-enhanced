/* sim_world part B developer aid (not game code): AOTP_TEST=ai, run in AP (--start 3).
 *
 * Builds a minimal world by hand in DGROUP (two one-plane fighter flights of opposite sides, head-on,
 * F4U vs ZERO_22 by default: AOTP_AI_TYPES="F4U,ZERO_22"; pilots SAKAI1/MACDON1 .PLT converted as
 * acedata's pilots_load does; SDEF.DAT loaded; a dummy player far away) and runs the AI frame order of
 * sim_step (world_ai_update, then spotting_update) for AOTP_AI_FRAMES frames (default 400), every plane
 * flying through the ported flight model (plane_fly). Prints every state change (brain_set_state), every
 * RNG draw (DS:4454) and the planes every AOTP_AI_EVERY frames (default 25), and checks each transition
 * against the state descriptors (registered, think timer = period + jitter 0..15, scratch zeroed, a think
 * only when the timer ran out or the descriptor forces it).
 *
 * Entry: sim_world_b_dev_test() — -1 when AOTP_TEST is not "ai". Called through
 * `-Wl,--wrap=platform_dev_test` in the module's own build (SWB_WRAP_DEVTEST) until platform calls it. */
#include "sim_world/swb_priv.h"
#include "prog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void (*swb_rng_hook)(u16 v, int range, s16 n);
extern void (*swb_state_hook)(u16 brain, u16 desc);

static int fails, frame, quiet_rng;
static u16 planes[2];

static const struct { u16 d; const char *n; } desc_names[] = {
    { 0x03B8, "path" }, { 0x03CC, "cv_takeoff" }, { 0x03E0, "rw_takeoff" }, { 0x03F4, "park_cv" },
    { 0x0408, "park_field" }, { 0x041C, "waypoints" }, { 0x0430, "join" }, { 0x0444, "keep_form" },
    { 0x0458, "climb_out" }, { 0x046C, "orbit_home" }, { 0x0480, "orbit_wp" }, { 0x0494, "return" },
    { 0x04A8, "escort_join" }, { 0x04BC, "escort_fly" }, { 0x04D0, "goto_target" }, { 0x12DE, "pair_pursue" },
    { 0x12F2, "spread" }, { 0x1306, "bracket" }, { 0x131A, "pair_attack" }, { 0x132E, "pair_break" },
    { 0x14A5, "divebomb" }, { 0x14B9, "torpedo" }, { 0x14CD, "glide" }, { 0x14E1, "low" }, { 0x14F5, "bomber" },
    { 0x1509, "rocket" }, { 0x151D, "strafe" }, { 0x1560, "tac_A_lag" }, { 0x1574, "tac_B_pass" },
    { 0x1588, "tac_C_extend" }, { 0x159C, "tac_D_zoom" }, { 0x15B0, "tac_E_loop" }, { 0x15F6, "hold" },
    { 0x16B5, "climb" }, { 0x16C9, "pursue" }, { 0x16DD, "dive" }, { 0x16F1, "guns" }, { 0x1705, "dive2" },
    { 0x1719, "headon_a" }, { 0x172D, "headon_b" }, { 0x1741, "manoeuvre" }, { 0x1755, "mnv_extend" },
    { 0x1769, "defend" }, { 0x177D, "dogfight" }, { 0x1791, "level" }, { 0x17A5, "extend" },
    { 0x17B9, "recover" }, { 0x17CD, "headon" }, { 0x17E1, "level2" }, { 0x17F5, "regroup" },
    { 0x1809, "cover" }, { 0x181D, "rejoin" }, { 0x1831, "cross" }, { 0x1845, "weave" }, { 0x1859, "bank" },
    { 0x186D, "pullup" },
};
static const char *dname(u16 d)
{
    for (size_t i = 0; i < sizeof desc_names / sizeof desc_names[0]; i++)
        if (desc_names[i].d == d) return desc_names[i].n;
    return "?";
}
static int pidx(u16 p) { return p == planes[0] ? 0 : p == planes[1] ? 1 : -1; }

static void rng_trace(u16 v, int range, s16 n)
{
    if (quiet_rng) return;
    if (range) printf("    rng f%-4d range(%d) = %d\n", frame, n, (s16)v);
    else printf("    rng f%-4d next = %04X\n", frame, v);
}

static void state_trace(u16 brain, u16 desc)
{
    u16 p = (u16)(brain - 0x4A);
    int i = pidx(p);
    u16 old = DSW(brain);
    printf("  f%-4d plane %c: %s (%04X) -> %s (%04X)  tgt %c\n", frame, i < 0 ? '?' : 'A' + i, dname(old), old,
           dname(desc), desc, OW(p, 0xD7) == 0 ? '-' : pidx(OW(p, 0xD7)) < 0 ? '?' : 'A' + pidx(OW(p, 0xD7)));
    if (!(DSW(desc + 6) & 0x8000)) { fails++; printf("  FAIL: descriptor %04X is not registered\n", desc); }
}

static u16 ds_cstr(const char *s) { u16 p = stk_alloc((u16)(strlen(s) + 1)); memcpy(DSP(p), s, strlen(s) + 1); return p; }

static int load_to(const char *name, u16 buf, u16 size)
{
    u16 m = stk_mark();
    u16 f = my_fopen(ds_cstr(name), ds_cstr("rb"));
    stk_release(m);
    if (!f) { printf("  cannot open %s\n", name); return 0; }
    s16 n = my_fread(buf, 1, (s16)size, f);
    my_fclose(f);
    return n == (s16)size;
}

/* acedata 45cc:0080 / 00a0: the rating conversion of a .PLT */
static s16 lerp10(s16 v, s16 lo, s16 hi) { return (s16)((s16)(((s32)(s16)(hi - lo) * v) >> 10) + lo); }
static void pilot_convert(u16 p)
{
    s16 r0 = DSS(p), r1 = DSS(p + 2), r3 = DSS(p + 6), r4 = DSS(p + 8), r5 = DSS(p + 0x0A);
    s16 r6 = DSS(p + 0x0C), r8 = DSS(p + 0x10), r9 = DSS(p + 0x12), r12 = DSS(p + 0x18);
    DSS(p + 0x00) = lerp10(r0, 0x200, 0x44C);  DSS(p + 0x48) = lerp10(r0, 0x334, 0x3B6);
    DSS(p + 0x02) = lerp10(r1, 0x1F4, 0x400);  DSS(p + 0x04) = lerp10(r1, 0x064, 0x44C);
    DSS(p + 0x06) = lerp10(r3, 0x096, -30);    DSS(p + 0x08) = lerp10(r4, 0x064, 0x3E8);
    DSS(p + 0x0A) = lerp10(r5, 0x258, -200);   DSS(p + 0x0C) = lerp10(r6, 0x1F40, 0x36B0);
    DSS(p + 0x0E) = lerp10(r6, 0x3E8, 0x708);  DSS(p + 0x10) = lerp10(r8, 0x0F3, 0x100);
    DSS(p + 0x12) = lerp10(r9, -300, 0x46);    DSS(p + 0x14) = lerp10(r9, 0x384, -240);
    DSS(p + 0x16) = lerp10(r9, 0x1D4C, 0x2AF8); DSS(p + 0x1E) = lerp10(r9, 0x400, 1);
    DSS(p + 0x18) = lerp10(r12, 0x1F4, -150);  DSS(p + 0x1A) = lerp10(r12, 0x1F4, -150);
    DSS(p + 0x1C) = lerp10(r12, 0x200, 0x400); DSS(p + 0x20) = lerp10(r12, 0x1F4, 0x384);
    if (DSS(p + 0x0A) < 0) DSS(p + 0x0A) = 0;
}

/* a Plane record (107h bytes) with its matrix cache, brain scratch, gun list (none) and FM state */
static u16 make_plane(u16 type, u16 dat, u16 pilot, u16 serial, s16 side, u16 flight, s32 x, s32 y, s32 z, u16 hdg)
{
    u16 p = stk_alloc(0x108), m = stk_alloc(0x2C), blk = stk_alloc(8), scr = stk_alloc(0x20),
        gl = stk_alloc(8), gs = stk_alloc(0x18);
    memset(DSP(p), 0, 0x108); memset(DSP(m), 0, 0x2C); memset(DSP(blk), 0, 8); memset(DSP(scr), 0, 0x20);
    memset(DSP(gl), 0, 8); memset(DSP(gs), 0, 0x18);
    OB(p, 0x18) = 0;                         /* a plane */
    OB(p, 0x19) = (u8)type;
    OW(p, 0x16) = blk;
    OW(p, 0x20) = m;
    OW(m, 0x28) = (u16)(p + 0x10);
    OB(p, 0x26) = (u8)side;
    OW(p, 0x34) = flight;
    OW(p, 0x54) = scr;
    OW(p, 0x5E) = gl;                        /* .GL image: no gun records (aim_solution: out of range) */
    OW(p, 0x60) = gs;
    OSL(p, 4) = x; OSL(p, 8) = y; OSL(p, 0x0C) = z;
    OW(p, 0x14) = hdg;
    OB(p, 0xBB) = (u8)pilot;
    OB(p, 0xC9) = (u8)serial;
    OW(p, 0xA4) = OW(p, 0xA6) = OW(p, 0xA8) = 0x400;
    fm_init((u16)(p + 0x64), (u16)(p + 2), DSW(dat + 0x24), m, 0xE6);
    OW(p, 0xB2) = 0xF0;                      /* throttle */
    OW(p, 0xB6) = 0x400;                     /* mass */
    OW(p, 0xB8) = 0;                         /* ground height */
    OW(p, 0x64 + 0x30) = 1100;               /* body forward speed */
    return p;
}

static u16 make_flight(u16 f, u16 member, s16 side, u16 base)
{
    u16 l = stk_alloc(4);
    memset(DSP(f), 0, 0x56);
    DSW(l) = member;
    DSW(l + 2) = 0;
    OB(f, 0) = 0;                            /* aircraft flight */
    OB(f, 2) = 0;                            /* role 0 */
    OW(f, 6) = 1;
    OW(f, 8) = l;
    OW(f, 4) = base;                         /* home object (its position) */
    OB(f, 0x42) = (u8)side;
    return f;
}

static void show(u16 p, char c)
{
    printf("  f%-4d %c %-12s pos %8ld %8ld %6ld  pit %6d rol %6d hdg %5u  v %5d thr %3d  tgt %c  rng %ld\n",
           frame, c, dname(DESC(p)), (long)OSL(p, 4), (long)OSL(p, 8), (long)OSL(p, 0x0C), OS(p, 0x10), OS(p, 0x12),
           OW(p, 0x14), OS(p, 0x6C), OW(p, 0xB2), OW(p, 0xD7) ? 'A' + pidx(OW(p, 0xD7)) : '-', (long)OSL(p, 0xD3));
}

static int test_ai(void)
{
    const char *ev = getenv("AOTP_AI_FRAMES"), *ee = getenv("AOTP_AI_EVERY"), *et = getenv("AOTP_AI_TYPES");
    int frames = ev && *ev ? atoi(ev) : 400, every = ee && *ee ? atoi(ee) : 25;
    if (every < 1) every = 1;
    char types[64];
    snprintf(types, sizeof types, "%s", et && *et ? et : "F4U,ZERO_22");
    char *t2 = strchr(types, ',');
    if (!t2) { printf("AOTP_AI_TYPES: two names\n"); return 1; }
    *t2++ = 0;
    quiet_rng = getenv("AOTP_AI_QUIET") != NULL;

    /* RNG DS:4454 seeded as 1889:0002 does: the 56 words of far 3A60:0000, j = 37h, k = 18h */
    for (int i = 0; i < 56; i++) DSW((u16)(0x4454 + 2 * i)) = SEGW(0x3A60, 2 * i);
    DSB(0x4454 + 0x70) = 0x37;
    DSB(0x4454 + 0x71) = 0x18;
    DSW(0x711E) = 0x100;                     /* dt: one nominal frame */
    DSW(0x3F22) = 0x46; DSW(0x3F24) = 0x42;  /* scenario_module_init's constants */

    /* aircraft .DAT / .FMD (types 0 and 1 → model slots 0 and 1) */
    u16 dat[2], fmd[2];
    const char *nm[2] = { types, t2 };
    u16 dattab = stk_alloc(4);
    for (int i = 0; i < 2; i++) {
        char fn[32];
        dat[i] = stk_alloc(0x84); fmd[i] = stk_alloc(0xD6);
        snprintf(fn, sizeof fn, "%s.DAT", nm[i]); if (!load_to(fn, dat[i], 0x83)) return 1;
        snprintf(fn, sizeof fn, "%s.FMD", nm[i]); if (!load_to(fn, fmd[i], 0xD6)) return 1;
        DSW(dat[i] + 0x24) = fmd[i];
        DSB((u16)(0x54BC + i)) = (u8)i;
        DSW(dattab + 2 * i) = dat[i];
        printf("  %s: class %d, energy %d, dive pitch %d, climb modes %d/%d\n", nm[i], (s8)DSB(dat[i] + 0x38),
               (s8)DSB(dat[i] + 0x59), DSS(dat[i] + 0x3A), (s8)DSB(dat[i] + 0x3C), (s8)DSB(dat[i] + 0x3D));
    }
    DSW(0x55D2) = dattab;
    /* pilots: index 1 (US), 2 (Japan), 2Ch (the player) */
    u16 ptab = stk_alloc(0xC2 * 2), plt[2];
    memset(DSP(ptab), 0, 0xC2 * 2);
    const char *pn[2] = { "MACDON1.PLT", "SAKAI1.PLT" };
    for (int i = 0; i < 2; i++) {
        plt[i] = stk_alloc(0x70);
        if (!load_to(pn[i], plt[i], 0x70)) return 1;
        pilot_convert(plt[i]);
        DSW(ptab + 2 * (i + 1)) = plt[i];
    }
    DSW(ptab + 2 * 0x2C) = plt[0];
    DSW(0x55CE) = ptab;
    if (!load_to("SDEF.DAT", 0x5CEC, 0x118)) return 1;          /* manoeuvre entry conditions */
    u16 forms = stk_alloc(0x198);
    memset(DSP(forms), 0, 0x198);
    ds_far_wr(0x12A8, ds_ptr(forms));

    /* the state descriptors (59f9:0026 per module) */
    DSW(0x47F2) = 0; DSW(0x47F4) = 0; DSW(0x488C) = 0;
    dogfight_register(); tactics_register(); tactic1027_register(); hold_register(); flight_states_register();
    class_register_n(7, 0x1531);             /* g_attack's descriptors (without loading G_ATTACK.DAT) */
    printf("  descriptors: %u registered, scratch max %u\n", DSW(0x47F2), DSW(0x47F4));

    /* world: flights, planes, a dummy player far away */
    u16 fl = stk_alloc(3 * 0x56), sorted = stk_alloc(4), plist = stk_alloc(4), base = stk_alloc(0x40);
    memset(DSP(base), 0, 0x40);
    OSL(base, 8) = -60000;                    /* home: 60000 south */
    u16 A = make_plane(0, dat[0], 1, 0, 0, fl, 0, -9000, 10000, 0);             /* heading north */
    u16 B = make_plane(1, dat[1], 2, 1, 1, (u16)(fl + 0x56), 600, 9000, 10500, 0x8000);   /* south */
    u16 P = make_plane(0, dat[0], 0x2C, 2, 0, (u16)(fl + 0xAC), 900000, 900000, 5000, 0);
    planes[0] = A; planes[1] = B;
    make_flight(fl, A, 0, base);
    make_flight((u16)(fl + 0x56), B, 1, base);
    make_flight((u16)(fl + 0xAC), P, 0, base);
    DSW(sorted) = fl; DSW(sorted + 2) = (u16)(fl + 0x56);
    DSW(plist) = A; DSW(plist + 2) = B;
    DSW(0x4753) = fl; DSW(0x476B) = 2; DSW(0x4751) = sorted; DSW(0x4763) = 2; DSW(0x4765) = 0;
    DSW(0x475B) = plist; DSW(0x4776) = 2; DSW(0x4774) = 0; DSW(0x476E) = 0;
    DSW(0x3B09) = P;
    DSW(0x5CD4) = DSW(0x5CD8) = 0;          /* hostile lists (player threat) empty */
    /* both know each other (spotted bits by serial) and start on their waypoints (none: next task) */
    OB(A, 0xC3) |= 2; OB(B, 0xC3) |= 1;
    elements_init();
    DSW(0x47EA) = 0;

    swb_rng_hook = rng_trace;
    swb_state_hook = state_trace;
    frame = 0;
    set_state(A, D_WAYPOINTS);
    set_state(B, D_WAYPOINTS);
    printf("--- %d frames ---\n", frames);
    for (frame = 1; frame <= frames; frame++) {
        s32 t0[2];
        u16 d0[2];
        for (int i = 0; i < 2; i++) { t0[i] = OSL(planes[i], 0x4C); d0[i] = DESC(planes[i]); }
        DSSL(0x4086) += DT;                  /* mission clock */
        world_ai_update();
        spotting_update();
        for (int i = 0; i < 2; i++) {
            u16 p = planes[i];
            /* the think of the frame ran only on an expired timer (or the forced think of flag 1) */
            if (DESC(p) == d0[i] && DSW(d0[i] + 0x10) != 0 && t0[i] > 0 && OSL(p, 0x4C) > t0[i] &&
                !(DSW(d0[i] + 6) & 1)) {
                fails++;
                printf("  FAIL: plane %c thought with its timer running (%ld -> %ld)\n", 'A' + i, (long)t0[i],
                       (long)OSL(p, 0x4C));
            }
            if (DESC(p) != d0[i]) {
                s32 per = DSSL(DESC(p)), tm = OSL(p, 0x4C);
                if (DESC(p) == D_LEVEL || DESC(p) == D_EXTEND) per = (s16)(PW(p, 6) << 8);   /* extend_enter */
                /* after a change in this frame the timer is period + jitter, minus at most one countdown */
                if (!(tm == 0 || (tm <= per + 15 && tm >= per - DT))) {
                    fails++;
                    printf("  FAIL: plane %c timer %ld after entering %s (period %ld)\n", 'A' + i, (long)tm,
                           dname(DESC(p)), (long)per);
                }
            }
        }
        if (frame % every == 0 || frame == frames) { show(A, 'A'); show(B, 'B'); }
    }
    swb_rng_hook = 0;
    swb_state_hook = 0;
    printf("AOTP_TEST=ai: %d failure(s)\n", fails);
    return fails != 0;
}

int sim_world_b_dev_test(void)
{
    const char *t = getenv("AOTP_TEST");
    if (!t || strcmp(t, "ai")) return -1;
    if (prog_current_id() != PROG_AP) { printf("AOTP_TEST=ai: run AP (--start 3)\n"); return 4; }
    DSW(0x1EBC) = 0x0583;                    /* as platform_dev_test does for AP */
    vol_init(0, 0, 0);
    u16 mk = stk_mark();
    int r = test_ai();
    stk_release(mk);
    fflush(stdout);
    return r;
}

#ifdef SWB_WRAP_DEVTEST
/* the module's own test build (build_sim_world_b) links with --wrap=platform_dev_test and, as part A's,
 * --wrap for the time-base / RNG helpers that other modules' weak stubs define as identities */
int sim_world_a_dev_test(void);
int __real_platform_dev_test(void);
int __wrap_platform_dev_test(void);
int __wrap_platform_dev_test(void)
{
    int r = sim_world_b_dev_test();
    if (r < 0) r = sim_world_a_dev_test();
    if (r >= 0) { fflush(stdout); exit(r); }
    return __real_platform_dev_test();
}
/* no shapes are loaded in the test world: a fixed shape radius for the spotting ranges */
u16 __wrap_shape_radius_cached(u16 ax);
u16 __wrap_shape_radius_cached(u16 ax) { (void)ax; return 250; }
#ifndef SWA_WRAP_DEVTEST
s16 __wrap_dt_scale(s16 x);
s16 __wrap_dt_scale(s16 x)
{
    s32 v = ((s32)x * DT) >> 8;
    if (v < -0x7FFF) v = -0x7FFF;
    else if (v > 0x7FFF) v = 0x7FFF;
    return (s16)v;
}
s16 __wrap_timer16_dec(u16 p);
s16 __wrap_timer16_dec(u16 p)
{
    s32 full = (s32)DSS(p) - DT;
    DSS(p) = full >= 0 ? (s16)full : 0;
    return DSS(p);
}
u16 __wrap_ap_rng_next(u16 st);
u16 __wrap_ap_rng_next(u16 st)
{
    if ((s8)DSB((u16)(st + 0x71)) < 0) return 0xFFFF;
    s8 i = (s8)DSB((u16)(st + 0x71)); DSB((u16)(st + 0x71))++;
    u16 v = DSW((u16)(st + i * 2));
    s8 j = (s8)DSB((u16)(st + 0x70)); DSB((u16)(st + 0x70))++;
    DSW((u16)(st + j * 2)) = (u16)(DSW((u16)(st + j * 2)) + v);
    u16 dx = DSW((u16)(st + j * 2));
    if (DSB((u16)(st + 0x70)) == 0x38) DSB((u16)(st + 0x70)) = 0;
    else if (DSB((u16)(st + 0x71)) == 0x38) DSB((u16)(st + 0x71)) = 0;
    return dx;
}
#endif
#endif
