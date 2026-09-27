/* render3d — palfx.c: the per-frame palette effect channels (AP 17ce), PAL_MAPS.DAT (51b4 paltrick.c)
 * and the step-table body of 1792:038a. render3d_sky.md §5. The channel engine (1792 pal_fx_*) is
 * video's. */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"
#include "video/video.h"
#include "sim_world/sim_world.h"
#include "sim_flow/sim_flow.h"

enum {
    P_BLOCK = 0x5BD8,                            /* near: channel block */
    P_NO_CEL = 0x5BDA, P_SKY = 0x5BDB, P_SEA = 0x5BDD, P_PREV_HIDDEN = 0x5BDF, P_PREV_INCLOUD = 0x5BE0,
    P_ZONE = 0x5BE1, P_SHADOW_GOAL = 0x5BE3, P_IN_CLOUD = 0x5BE5, P_PREV_NIGHT = 0x5BE6,
    P_PREV_CLOUDS = 0x5BE8, P_HAZE_ON = 0x0320, P_BLACKOUT = 0x5798, P_OPT_GLARE = 0x42C8,
    P_OPT_CLOUDS = 0x42C9, P_CAMERA = 0x437E, P_BASE_PAL = 0x408A,
    P_NAME = 0x062C, P_DAT = 0x05BC, P_RB = 0x05EB,
};
#define PALTRICK_TAG far_make(SEG(0x3888), 0)   /* "paltrick.c" */

/* AP 1792:038b — body of the near routine 1792:038a (video's pal_fx_build_delta, called with the
 * `inc bp` entry): T[c] = ((c − 1)·k) >> 6, T[−c] = −T[c] at block + 347h */
void palfx_delta_table_body(s16 k) { pal_fx_build_delta(k); }   /* same block: DS:5BD8 = AP_DS_pal_fx */

/* AP 17ce:0008 — force the night levels to be re-sent (map screen) */
void palfx_force(void) { DSW(P_PREV_NIGHT) = 0xFFFF; }

/* AP 17ce:0013 — channel goals of the frame, then pal_fx_update */
void palfx_frame(void)
{
    s32 alt = DSSL(DSW(DSW(P_CAMERA) + 2) + 0x0A);
    s16 fog = 0;
    u8 in_cloud = 0;
    pal_fx_set(11, 1, (s16)DSW(P_BLACKOUT));
    if (DSB(P_PREV_CLOUDS) != DSB(P_OPT_CLOUDS)) {         /* clouds option changed */
        DSB(P_PREV_CLOUDS) = DSB(P_OPT_CLOUDS);
        if (!DSB(P_OPT_CLOUDS)) {
            DSB(P_NO_CEL) = 0; DSW(P_SEA) = 0x80; DSW(P_SKY) = 0x70; DSW(P_HAZE_ON) = 1;
            pal_fx_set(5, 0, 0);
        }
        DSW(P_PREV_NIGHT) = 0xFFFF;
    }
    s16 n = (s16)darkness_lerp(0, 0x2280);                 /* night level */
    if ((s16)DSW(P_PREV_NIGHT) != n) {
        s16 day = (s16)(0x4000 - ldiv_sat((s32)n << 14, 0x2280));
        s16 q = ldiv_sat((s32)n << 14, 0x2280);
        pal_fx_set(0, 1, n);
        pal_fx_set(4, 1, (s16)(n >> 1));
        if (day < 0) day = 0;
        pal_fx_set(9, 1, day);
        pal_fx_set(10, 1, day);
        if (overcast()) { pal_fx_set(2, 1, q); pal_fx_set(3, 1, n); pal_fx_set(1, 0, 0); }
        else { pal_fx_set(1, 1, q); pal_fx_set(2, 0, 0); pal_fx_set(3, 0, 0); }
        DSW(P_PREV_NIGHT) = (u16)n;
    }
    if (overcast()) {
        if (alt < 0x34BC) DSW(P_ZONE) = 0;
        else if (alt > 0x4844) DSW(P_ZONE) = 2;
        else {
            DSW(P_ZONE) = 1;
            fog = (s16)((u16)(0x9C4 - (u16)r3d_labs(0x3E80 - alt)) << 3);
            if (fog > 0x3D00) fog = 0x3D00;
        }
        DSB(P_NO_CEL) = alt < 0x3E80;                      /* below the layer: no sun, moon, stars */
        DSW(P_SEA) = 0x80; DSW(P_SKY) = 0x70; DSW(P_HAZE_ON) = 1;
        if (DSB(P_OPT_CLOUDS)) {
            if (alt < 0x3E80) { DSW(P_SKY) = 0x61; DSW(P_HAZE_ON) = 0; }
            else DSW(P_SEA) = 0x70;                        /* cloud deck instead of the sea */
            in_cloud = DSW(P_ZONE) == 1;
            pal_fx_set(5, in_cloud, fog);                  /* whiteout */
        }
    } else if (DSB(P_IN_CLOUD) != DSB(P_PREV_INCLOUD)) {
        pal_fx_set(6, DSB(P_IN_CLOUD), -1);
        DSB(P_PREV_INCLOUD) = DSB(P_IN_CLOUD);
    }
    u8 hidden = (u8)sun_in_cloud(0x14A20);
    s16 g = (s16)ap_sun_glare_6964();
    if (overcast() && alt < 0x34BC) g = 0;
    if (!in_cloud && hidden != DSB(P_PREV_HIDDEN)) {
        DSW(P_SHADOW_GOAL) = hidden ? 0x1000 : 0;
        pal_fx_set(7, hidden, (s16)DSW(P_SHADOW_GOAL));
        DSB(P_PREV_HIDDEN) = hidden;
    }
    if (DSB(P_OPT_GLARE) && !in_cloud && !hidden) pal_fx_set(8, 1, g);
    else pal_fx_set(8, 1, 0);
    pal_fx_update(0);
}

/* AP 51b4:0000 — the 13 channels of PAL_MAPS.DAT (sim_init) */
void paltrick_load(void)
{
    pal_fx_create(13, ds_far(P_BASE_PAL));
    u16 fp = my_fopen(res_name(P_NAME, P_DAT), P_RB);
    if (!fp) assert_fail(PALTRICK_TAG, 0x55, 0);
    u16 mk = stk_mark(), rec = stk_alloc(0x26);
    for (s16 i = 0; i < 13; i++) {
        my_fread(rec, 0x25, 1, fp);
        pal_fx_add(rec, i);
    }
    stk_release(mk);
    my_fclose(fp);
}

/* AP 51b4:0088 — mission start */
void palette_effects_init(void)
{
    pal_fx_set(0, 0, 0);
    pal_fx_set(9, 1, 0x4000);
    pal_fx_set(10, 1, 0x3800);
    pal_fx_set(5, 0, 0);
    pal_fx_set(6, 0, -1);
    pal_fx_set(7, 0, -1);
    pal_fx_set(8, 0, 0);
    pal_fx_set(11, 0, 0);
    pal_fx_set(12, 0, -1);
    pal_fx_reset_levels();
    DSB(P_NO_CEL) = 0; DSW(P_SEA) = 0x80; DSW(P_SKY) = 0x70; DSW(P_HAZE_ON) = 1;
    DSW(P_SHADOW_GOAL) = 0; DSW(P_ZONE) = 2; DSB(P_PREV_INCLOUD) = 0; DSB(P_PREV_HIDDEN) = 0;
    DSB(P_IN_CLOUD) = 0; DSB(P_PREV_CLOUDS) = 0xFF; DSW(P_PREV_NIGHT) = 0xFFFF; DSW(P_BLACKOUT) = 0;
}
