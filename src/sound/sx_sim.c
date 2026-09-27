/* AP's positional sound manager sx_sim.c (overlay 5a75, stub segment 3d6e) — sound.md §12,
 * sound_extra.md §2. Each frame sx_update collects the sources registered by the game (continuous and
 * one-shot), keeps the audible ones, sorts them by priority and volume, fits them into the channel /
 * voice budget and drives the 7 pool nodes through the player stubs. All state is AP DGROUP data.
 *
 * Fragments listed in port/modules.csv (sx_frag_00fa, _01dd, _0243, _030a, _034c, _0383, _05d7, _0697,
 * _06b2, _0777, _0797, _0db5) are the "inc bp" prologue byte of the function at the next address
 * (VROOMM far-frame convention): they are part of those functions, not code of their own. */
#include "sound/snd_priv.h"

#include "codeptr.h"
#include "host.h"
#include "platform/platform.h"
#include "prog.h"
#include "render3d/render3d.h"
#include "sim_flow/sim_flow.h"
#include "symbols_ap.h"

/* ---- calls into other modules, in one place so a header fix there is a one-line change here */
static u32 sx_dist3(u16 a_ds, u16 b_ds) { return ldist3(a_ds, b_ds); }   /* 277a:3b3c (a, b) */
static void sx_timeout_start(u16 t_ds, s32 n) { timeout_start(t_ds, (u16)n); }      /* 4b31:0041 (t, long) */

/* types: T[type] = 8 bytes (sound.md §12.3), maxdist[type] words */
static inline u16 T(int type) { return (u16)(DSW(AP_DS_sx_type_table) + type * 8); }
static inline u16 maxdist(int type) { return DSW((u16)(DSW(AP_DS_sx_maxdist) + type * 2)); }
static inline FarPtr pool(int i) { return ds_far((u16)(AP_DS_snd_pool + 4 * i)); }
static inline u8 *np(FarPtr n, u16 o) { return mp(n.seg, (u16)(n.off + o)); }

/* source (14 bytes): +0 pos, +2 vel, +4 volume, +6 bend, +8 age, +A type, +B prio, +C pool index,
 * +D flags (bit 0 continuous, bit 1 silent, bit 2 full budget) */

/* 5a75:0000: take n tokens from a list and start each one's timer */
void tok_hold(u16 list, s16 ticks, s16 n)
{
    while (--n >= 0) {
        u16 t = pool_alloc_zero_near(list);
        if (!t) break;
        sx_timeout_start(t, ticks);
    }
}

/* 5a75:0034: after a sound ends its channel and voice tokens cool down for T.7 ticks */
void sx_release(s16 type)
{
    u16 t = T(type);
    s16 ticks = DSB((u16)(t + 7));
    tok_hold(DSW(AP_DS_sx_voice_tokens), ticks, (s8)DSB((u16)(t + 6)));
    tok_hold(DSW(AP_DS_sx_chan_tokens), ticks, (s8)DSB((u16)(t + 5)));
}

/* 5a75:008a: count the tokens still cooling down, free the expired ones */
s16 tok_busy(u16 list)
{
    u16 expired[32];
    s16 n = 0, busy = 0;
    u16 t = 0;
    while ((t = pool_next_near(list, t)) != 0) {
        if (timeout_expired(t)) {
            if (n < 32) expired[n] = t;
            n++;
        } else {
            busy++;
        }
    }
    for (s16 i = (s16)(n > 32 ? 32 : n) - 1; i >= 0; i--) pool_free2(list, expired[i]);
    return busy;
}

/* 5a75:00fb: one-shots may wait 3 frames for a node; finished ones release their budget */
static void sx_age_oneshots(void)
{
    s16 w = 0;
    u16 dst = AP_DS_sx_oneshots;
    u16 src = AP_DS_sx_oneshots;
    for (s16 i = 0; i < (s16)DSW(AP_DS_sx_n_oneshot); i++, src = (u16)(src + 14)) {
        bool keep;
        u8 k = DSB((u16)(src + 0xC));
        if ((s16)DSW((u16)(src + 8)) < (s16)DSW(AP_DS_sx_oneshot_wait) && k == 0xFF) {
            keep = true;
            DSW((u16)(src + 8)) = (u16)(DSW((u16)(src + 8)) + 1);
        } else if (k == 0xFF) {
            keep = false;
        } else if (*np(pool((s8)k), 0x158) == 0xFF) {
            keep = false;
            sx_timeout_start(AP_DS_sx_start_timer, (s16)DSW(AP_DS_sx_end_delay));
            sx_release((s8)DSB((u16)(src + 0xA)));
        } else {
            keep = true;
        }
        if (keep) {
            if (w++ < i)
                for (int b = 0; b < 14; b++) DSB((u16)(dst + b)) = DSB((u16)(src + b));
            dst = (u16)(dst + 14);
        }
    }
    DSW(AP_DS_sx_n_oneshot) = (u16)w;
}

/* 5a75:01de: volume from the distance, once (a one-shot keeps its first audible volume) */
static void sx_volume(u16 src, s16 dist)
{
    if (DSW((u16)(src + 4)) != 0) return;
    s8 type = (s8)DSB((u16)(src + 0xA));
    s16 cx;
    if (maxdist(type) == 0) cx = 0x7F;
    else cx = (s16)(0x7F - ldiv_sat((s32)dist << 7, (s16)maxdist(type)));
    if (cx > 0x7F) cx = 0x7F;
    DSW((u16)(src + 4)) = (u16)cx;
}

/* 5a75:0244: the audible sources of an array -> dst (near pointers); returns their count */
static s16 sx_collect(u16 dst, u16 src, s16 n)
{
    s16 cnt = 0;
    for (; --n >= 0; src = (u16)(src + 14)) {
        s32 d = 0;
        if (DSW(src)) d = (s32)sx_dist3(AP_DS_sx_listener, DSW(src));
        if (d >= 200000) continue;
        d >>= 3;
        s16 dx = (s16)d;
        s8 type = (s8)DSB((u16)(src + 0xA));
        if (maxdist(type) == 0 || (s16)maxdist(type) > dx) {
            DSW(dst) = src;
            dst = (u16)(dst + 2);
            sx_volume(src, dx);
            cnt++;
        }
    }
    return cnt;
}

/* 5a75:02df: one-shots, then continuous sources */
static void sx_collect_all(void)
{
    DSW(AP_DS_sx_n_audible) = (u16)sx_collect(AP_DS_sx_audible, AP_DS_sx_oneshots, (s16)DSW(AP_DS_sx_n_oneshot));
    s16 k = sx_collect((u16)(AP_DS_sx_audible + 2 * DSW(AP_DS_sx_n_audible)), AP_DS_sx_continuous,
                       (s16)DSW(AP_DS_sx_n_continuous));
    DSW(AP_DS_sx_n_audible) = (u16)(DSW(AP_DS_sx_n_audible) + k);
}

/* 5a75:030b: outside the cockpit some types sound different (T.3) */
static void sx_substitute(u16 flag)
{
    if (!flag) return;
    for (s16 i = (s16)DSW(AP_DS_sx_n_audible) - 1; i >= 0; i--) {
        u16 src = DSW((u16)(AP_DS_sx_audible + 2 * i));
        u8 t = DSB((u16)(T((s8)DSB((u16)(src + 0xA))) + 3));
        if (t) DSB((u16)(src + 0xA)) = t;
    }
}

/* 5a75:034d */
static void sx_node_stop(FarPtr node)
{
    sx_timeout_start(AP_DS_sx_start_timer, (s16)DSW(AP_DS_sx_stop_delay));
    snd_node_stop_far(node);
    sx_release((s8)*np(node, 0x170));
}

/* 5a75:0384: looping nodes follow their source; orphans are queued for stopping */
static void sx_match_nodes(void)
{
    for (s16 cx = 0; cx < (s16)DSW(AP_DS_sx_pool_n); cx++) {
        FarPtr node = pool(cx);
        if (*np(node, 0x158) == 0xFF || *np(node, 0x15D) == 0) continue;
        u16 owner = (u16)(np(node, 0x16E)[0] | np(node, 0x16E)[1] << 8);
        u8 type = *np(node, 0x170);
        s16 dx;
        for (dx = 0; dx < (s16)DSW(AP_DS_sx_n_audible); dx++) {
            u16 si = DSW((u16)(AP_DS_sx_audible + 2 * dx));
            if (DSB((u16)(si + 0xA)) == type && DSW(si) == owner) {
                DSB((u16)(si + 0xC)) = (u8)cx;
                break;
            }
        }
        if (dx == (s16)DSW(AP_DS_sx_n_audible)) {
            ds_far_wr((u16)(AP_DS_sx_stop_list + 4 * DSW(AP_DS_sx_n_stop)), node);
            DSW(AP_DS_sx_n_stop) = (u16)(DSW(AP_DS_sx_n_stop) + 1);
        }
    }
}

/* 5a75:042a: qsort comparator: priority, then volume (both descending), then address */
s16 sx_cmp(u16 a_ds, u16 b_ds)
{
    u16 si = DSW(a_ds), di = DSW(b_ds);
    s16 r = (s16)((s8)DSB((u16)(di + 0xB)) - (s8)DSB((u16)(si + 0xB)));
    if (r) return r;
    r = (s16)(DSW((u16)(di + 4)) - DSW((u16)(si + 4)));
    if (r) return r;
    r = (s16)(si - di);
    if (r) return r;
    return -1;
}

/* 5a75:0478 */
static void sx_sort(void)
{
    if ((s16)DSW(AP_DS_sx_n_audible) > 1) {
        codeptr_register(PROG_AP, AP_FN_sx_cmp, (CodeFn)sx_cmp);
        rtl_qsort(AP_DS_sx_audible, DSW(AP_DS_sx_n_audible), 2, far_make(SEG(0x3D6E), 0x0070));   /* stub 3d6e:0070 */
    }
}

/* 5a75:0497 */
void sx_reserve(s16 type)
{
    DSW(AP_DS_sx_channels_used) = (u16)(DSW(AP_DS_sx_channels_used) + (s8)DSB((u16)(T(type) + 5)));
    DSW(AP_DS_sx_voices_used) = (u16)(DSW(AP_DS_sx_voices_used) + (s8)DSB((u16)(T(type) + 6)));
}

/* 5a75:04c5 */
void sx_budget_start(void)
{
    DSB(AP_DS_sx_may_start) = 1;
    DSW(AP_DS_sx_channels_used) = (u16)tok_busy(DSW(AP_DS_sx_chan_tokens));
    DSW(AP_DS_sx_voices_used) = (u16)tok_busy(DSW(AP_DS_sx_voice_tokens));
    for (s16 i = 0; i < (s16)DSW(AP_DS_sx_n_audible); i++) {
        u16 src = DSW((u16)(AP_DS_sx_audible + 2 * i));
        if (DSB((u16)(src + 0xC)) != 0xFF) sx_reserve((s8)DSB((u16)(src + 0xA)));
    }
}

/* 5a75:051c: does the source fit in the channel / voice budget? */
s16 sx_fits(u16 src)
{
    bool full = DSB((u16)(src + 0xD)) & 4;
    s16 type = (s8)DSB((u16)(src + 0xA));
    s16 lim = (s16)(DSW(AP_DS_sx_max_channels) - (full ? 0 : 1));
    if ((s16)(DSW(AP_DS_sx_channels_used) + (s8)DSB((u16)(T(type) + 5))) > lim) return 0;
    lim = (s16)(DSW(AP_DS_sx_max_voices) - (full ? 0 : 6));
    if ((s16)(DSW(AP_DS_sx_voices_used) + (s8)DSB((u16)(T(type) + 6))) > lim) return 0;
    return 1;
}

/* 5a75:05a3 */
s16 sx_can_start(u16 src)
{
    if (DSB(AP_DS_sx_may_start) && sx_fits(src) && timeout_expired(AP_DS_sx_start_timer)) return 1;
    DSB(AP_DS_sx_may_start) = 0;
    return 0;
}

/* 5a75:05d8: sources beyond the pool or the budget lose their node (quirk: still reserved) */
static void sx_trim(void)
{
    DSW(AP_DS_sx_voices_used) = 0;
    DSW(AP_DS_sx_channels_used) = 0;
    for (s16 di = 0; di < (s16)DSW(AP_DS_sx_n_audible); di++) {
        u16 si = DSW((u16)(AP_DS_sx_audible + 2 * di));
        s16 type = (s8)DSB((u16)(si + 0xA));
        if (DSB((u16)(si + 0xC)) != 0xFF) {
            if (di >= (s16)DSW(AP_DS_sx_pool_n) || !sx_fits(si)) {
                ds_far_wr((u16)(AP_DS_sx_stop_list + 4 * DSW(AP_DS_sx_n_stop)), pool((s8)DSB((u16)(si + 0xC))));
                DSW(AP_DS_sx_n_stop) = (u16)(DSW(AP_DS_sx_n_stop) + 1);
                DSB((u16)(si + 0xC)) = 0xFF;
            }
            sx_reserve(type);
        } else if (di < (s16)DSW(AP_DS_sx_pool_n)) {
            sx_reserve(type);
        }
    }
}

/* 5a75:066f: first free pool node, -1 */
static s16 sx_free_node(void)
{
    for (s16 i = 0; i < (s16)DSW(AP_DS_sx_pool_n); i++)
        if (*np(pool(i), 0x158) == 0xFF) return i;
    return -1;
}

/* 5a75:0698 */
static void node_clear(FarPtr node)
{
    huge_memset(node.off, node.seg, 0, 0x17A, 0);
}

/* 5a75:06b3: Doppler pitch bend from the change of distance between two frames */
static void sx_doppler(u16 src, FarPtr node)
{
    s32 d;
    if (DSW((u16)(src + 2))) {
        u16 m = stk_mark();
        u16 p = stk_alloc(12);
        for (int b = 0; b < 12; b++) DSB((u16)(p + b)) = DSB((u16)(DSW(src) + b));
        ladd_v(p, DSW((u16)(src + 2)));
        s32 d1 = (s32)sx_dist3(AP_DS_sx_listener_prev, DSW(src));
        s32 d2 = (s32)sx_dist3(AP_DS_sx_listener, p);
        d = (s32)(u32)(d1 - d2);
        d = clamp((s16)d, -1000, 1000);                  /* the low 16 bits, result sign-extended */
        stk_release(m);
    } else {
        d = 0;
    }
    s16 bend = clamp((s16)(DSW((u16)(src + 6)) + (u16)d), 100, 16000);
    snd_node_pitch_bend(node, (u16)bend, DSW(AP_DS_sx_bend_chan));
}

/* 5a75:0778 */
static u16 sx_eff_vol(u16 src)
{
    return (DSB((u16)(src + 0xD)) & 2) ? 0 : DSW((u16)(src + 4));
}

/* 5a75:0798: stop the queued nodes, start / update the first 7 sources (priority 7..1) */
static void sx_apply(void)
{
    s16 n = (s16)DSW(AP_DS_sx_n_audible) > (s16)DSW(AP_DS_sx_pool_n) ? (s16)DSW(AP_DS_sx_pool_n)
                                                                       : (s16)DSW(AP_DS_sx_n_audible);
    for (s16 i = 0; i < (s16)DSW(AP_DS_sx_n_stop); i++) sx_node_stop(ds_far((u16)(AP_DS_sx_stop_list + 4 * i)));
    sx_budget_start();
    s16 prio = n;
    for (s16 i = 0; i < n; i++, prio--) {
        u16 src = DSW((u16)(AP_DS_sx_audible + 2 * i));
        if (DSB((u16)(src + 0xC)) == 0xFF) {
            if (!sx_can_start(src)) continue;
            DSB((u16)(src + 0xC)) = (u8)sx_free_node();
            if (DSB((u16)(src + 0xC)) == 0xFF) continue;
            s8 type = (s8)DSB((u16)(src + 0xA));
            sx_reserve(type);
            FarPtr node = pool((s8)DSB((u16)(src + 0xC)));
            node_clear(node);
            if (DSB((u16)(src + 0xD)) & 1) *np(node, 0x15D) = 1;
            *np(node, 0x16E) = DSB(src);
            *np(node, 0x16F) = DSB((u16)(src + 1));
            *np(node, 0x170) = (u8)type;
            FarPtr res = ds_far((u16)(DSW(AP_DS_sx_sng_table) + 4 * type));
            far_wr(node.seg, (u16)(node.off + 0x166), res);
            far_wr(node.seg, (u16)(node.off + 0x16A), res_skip_header(res));
            *np(node, 0x15E) = (u8)sx_eff_vol(src);
            *np(node, 0x15C) = (u8)prio;
            *np(node, 0x158) = 0xFF;
            far_wr(node.seg, (u16)(node.off + 0x008), far_add(node, 0x16A));
            snd_node_play(node, 0);
            sx_doppler(src, node);
        } else {
            FarPtr node = pool((s8)DSB((u16)(src + 0xC)));
            snd_node_volume(node, sx_eff_vol(src));
            snd_node_set_priority(node, (u16)prio);
            sx_doppler(src, node);
        }
    }
}

/* 5a75:095e: the per-frame update (from the VCR glue 5d3c:005a with the camera position) */
void sx_update(u16 listener, u16 flag)
{
    if (!DSB(AP_DS_sx_active)) return;
    DSW(AP_DS_sx_n_stop) = 0;
    sx_age_oneshots();
    sx_collect_all();
    sx_substitute(flag);
    sx_match_nodes();
    sx_sort();
    sx_trim();
    sx_apply();
    for (int b = 0; b < 12; b++) DSB((u16)(AP_DS_sx_listener_prev + b)) = DSB((u16)(AP_DS_sx_listener + b));
    for (int b = 0; b < 12; b++) DSB((u16)(AP_DS_sx_listener + b)) = DSB((u16)(listener + b));
}

/* 5a75:09a7 */
static void sx_stop_nodes(void)
{
    for (s16 i = 0; i < (s16)DSW(AP_DS_sx_pool_n); i++)
        if (*np(pool(i), 0x158) != 0xFF) sx_node_stop(pool(i));
}

/* 5a75:09d5 */
void sx_off(void)
{
    if (DSB(AP_DS_sx_active) && DSB(AP_DS_sx_on)) {
        sx_stop_nodes();
        DSB(AP_DS_sx_on) = 0;
    }
}

/* 5a75:09f0 */
void sx_on(void)
{
    if (DSB(AP_DS_sx_active)) DSB(AP_DS_sx_on) = 1;
}

/* 5a75:0a01 */
u16 sx_is_on(void) { return (u16)(s8)DSB(AP_DS_sx_on); }

/* 5a75:0a0a */
void sx_clear_continuous(void)
{
    if (DSB(AP_DS_sx_active)) DSW(AP_DS_sx_n_continuous) = 0;
}

/* 5a75:0a1c */
s16 sx_type_valid(s16 t) { return t < 0x26; }

/* 5a75:0a2e (no caller) */
s16 sx_type_continuous(s16 t) { return (s8)DSB(T(t)); }

/* 5a75:0a42 PC speaker: delays 5/5, bend channel 0, some priorities 40 */
void sx_setup_std(void)
{
    static const u16 offs[] = { 0x99, 0x9A, 0xC9, 0xCA, 0xD9, 0xDA, 0xD1, 0xD2, 0xE1, 0xE2,
                                0x102, 0x10A, 0x112, 0x119, 0x11A, 0x121, 0x122, 0x12A };
    DSW(AP_DS_sx_end_delay) = 5;
    DSW(AP_DS_sx_stop_delay) = 5;
    for (size_t i = 0; i < sizeof offs / sizeof offs[0]; i++) DSB((u16)(DSW(AP_DS_sx_type_table) + offs[i])) = 0x28;
    DSW(AP_DS_sx_bend_chan) = 0;
}

/* 5a75:0ab3 MT-32 */
void sx_setup_m32(void)
{
    DSW(AP_DS_sx_end_delay) = 10;
    DSW(AP_DS_sx_stop_delay) = 10;
    DSW(AP_DS_sx_bend_chan) = 1;
}

/* 5a75:0ac6 AdLib (and 4) */
void sx_setup_adl(void)
{
    DSW(AP_DS_sx_end_delay) = 8;
    DSW(AP_DS_sx_stop_delay) = 8;
}

/* 5a75:0ad3 Tandy */
void sx_setup_tan(void)
{
    DSW(AP_DS_sx_end_delay) = 8;
    DSW(AP_DS_sx_stop_delay) = 8;
    DSW(AP_DS_sx_bend_chan) = 0;
}

/* 5a75:0ae6 */
s16 sx_is_mt32(void)
{
    return DSB(AP_DS_sx_active) ? (s8)DSB(AP_DS_sx_mt32) : 0;
}

/* 5a75:0afa: driver, token lists, SND1.DAT, pool, SNG bank, timer clients */
s16 sx_init(u16 ovl, u16 datname, u16 sngname, u16 drv, u16 off)
{
    if ((u8)off) return 0;
    drv = (u16)snd_port_driver((s16)drv);  /* PORT: AdLib for every configuration (sound.md §13) */
    DSB(AP_DS_sx_mt32) = drv == 3;
    if (!snd_load_driver((s16)drv, ovl)) return 0;
    DSW(AP_DS_sx_voice_tokens) = pool_create_near(0x20, 6);
    DSW(AP_DS_sx_chan_tokens) = pool_create_near(0x08, 6);
    u16 f = my_fopen(datname, 0x0881);                          /* "rb" */
    FarPtr file_str = far_make(SEG(0x38CC), 0x0000);            /* "sx_sim.c" (assert file name) */
    if (!f) assert_fail(file_str, 0x3C0, 0);
    s16 n = (s16)(my_filelength(f) / 10);
    DSW(AP_DS_sx_maxdist) = mem_new((u16)(n * 2), 0, far_to_near_str(file_str), 0x3C2);
    if (!DSW(AP_DS_sx_maxdist)) assert_fail(file_str, 0x3C4, 0);
    my_fread(DSW(AP_DS_sx_maxdist), 2, n, f);
    DSW(AP_DS_sx_type_table) = mem_new((u16)(n * 8), 0, far_to_near_str(file_str), 0x3C6);
    if (!DSW(AP_DS_sx_type_table)) assert_fail(file_str, 0x3C8, 0);
    my_fread(DSW(AP_DS_sx_type_table), 8, n, f);
    my_fclose(f);
    snd_pool_alloc();
    DSW(AP_DS_sx_sng_table) = sng_load(sngname);
    timer_install(13);
    snd_register_service();
    timer_add(far_make(SEG(0x3060), 0x17E2), 4);               /* player_service (handle discarded) */
    timer_add(far_make(SEG(0x0000), 0x02DB), 1);               /* sim_tick_cb: 236.7 Hz tick counter */
    DSB(AP_DS_sx_active) = 1;
    switch (drv) {                                              /* table cs:0d1d */
    case 0: sx_setup_std(); break;
    case 1: sx_setup_tan(); break;
    case 2: case 4: sx_setup_adl(); break;
    case 3: sx_setup_m32(); break;
    }
    if (drv != 3 && drv != 0x88) {
        for (int t = 0; t < 38; t++) {
            DSB((u16)(T(t) + 7)) = 4;
            DSB((u16)(T(t) + 6)) = 2;
            DSB((u16)(T(t) + 5)) = 1;
        }
    }
    if (DSB(AP_DS_drv_adapter) == 3) ps1_audio_off();
    return 1;
}

/* 5a75:0d27 */
u16 sx_countdown_get(void) { return DSW(AP_DS_timer_countdown); }

/* 5a75:0d2b: 10 PIT ticks (42 ms) for the voices to fade */
void sx_wait10(void)
{
    DSW(AP_DS_timer_countdown) = 10;
    while (sx_countdown_get()) host_pump();
}

/* 5a75:0d39 */
void sx_shutdown(void)
{
    if (!DSB(AP_DS_sx_active)) return;
    DSB(AP_DS_sx_active) = 0;
    sx_stop_nodes();
    snd_driver_close();
    sng_free(DSW(AP_DS_sx_sng_table));
    sx_wait10();
    timer_restore();
    snd_host_detach();                     /* PORT: the audio thread stops with the timer */
}

/* 5a75:0d66 */
s16 sx_enable(s16 on)
{
    if (!DSB(AP_DS_sx_active)) return 0;
    if (on == 1) { if (!DSB(AP_DS_sx_on)) sx_on(); }
    else if (on == 0 && DSB(AP_DS_sx_on)) sx_off();
    DSW(0x48BD) = 0xFFFF;                  /* (never read) */
    timeout_expire(AP_DS_sx_start_timer);
    return 1;
}

/* 5a75:0db0: empty far function, no caller */
void sx_nop(void) {}

/* 5a75:0db6: register a source for this frame (continuous: slot of 4983, <= 50; one-shot: 4C3F, <= 30) */
static u16 sx_add(s16 type, u16 pos, u16 flags, u8 player)
{
    u16 si = 0;
    if (!DSB(AP_DS_sx_on)) return 0;
    if (!sx_type_valid(type)) return 0;
    if (DSB(T(type))) {
        if ((s16)DSW(AP_DS_sx_n_continuous) < 0x32) {
            si = (u16)(AP_DS_sx_continuous + (s16)DSW(AP_DS_sx_n_continuous) * 14);
            DSW(AP_DS_sx_n_continuous) = (u16)(DSW(AP_DS_sx_n_continuous) + 1);
            for (int b = 0; b < 14; b++) DSB((u16)(si + b)) = 0;
            DSB((u16)(si + 0xD)) |= 1;
        }
    } else if ((s16)DSW(AP_DS_sx_n_oneshot) < 0x1E) {
        si = (u16)(AP_DS_sx_oneshots + (s16)DSW(AP_DS_sx_n_oneshot) * 14);
        DSW(AP_DS_sx_n_oneshot) = (u16)(DSW(AP_DS_sx_n_oneshot) + 1);
        for (int b = 0; b < 14; b++) DSB((u16)(si + b)) = 0;
    }
    if (si) {
        DSW((u16)(si + 6)) = 0x2000;
        DSB((u16)(si + 0xA)) = (u8)type;
        DSB((u16)(si + 0xC)) = 0xFF;
        DSW(si) = pos;
        s8 t = (s8)DSB((u16)(si + 0xA));
        DSB((u16)(si + 0xB)) = DSB((u16)(T(t) + (player ? 1 : 2)));
        if (flags & 1) DSB((u16)(si + 0xD)) |= 2;
    }
    return si;
}

/* 5a75:0e96 */
void sx_stop_all(void)
{
    if (!DSB(AP_DS_sx_active)) return;
    sx_stop_nodes();
    sx_clear_continuous();
}

/* 5a75:0ea9: non-positional source with volume and bend */
s16 sx_play_vol(u16 type, u16 vol, u16 bend, u16 player)
{
    if (!DSB(AP_DS_sx_active)) return 0;
    u16 si = sx_add((s16)type, 0, 0, (u8)player);
    if (si) {
        DSW((u16)(si + 4)) = vol;
        DSW((u16)(si + 6)) = bend;
    }
    return si != 0;
}

/* 5a75:0eea */
s16 sx_play(u16 type, u16 pos, u16 vel, u16 player)
{
    if (!DSB(AP_DS_sx_active)) return 0;
    u16 si = sx_add((s16)type, pos, 0, (u8)player);
    if (si) {
        DSW((u16)(si + 2)) = vel;
        DSW((u16)(si + 6)) = 0x2000;
    }
    return si != 0;
}

/* 5a75:0f2c: as sx_play, with the full budget */
s16 sx_play_rsv(u16 type, u16 pos, u16 vel, u16 player)
{
    if (!DSB(AP_DS_sx_active)) return 0;
    u16 si = sx_add((s16)type, pos, 0, (u8)player);
    if (si) {
        DSW((u16)(si + 2)) = vel;
        DSW((u16)(si + 6)) = 0x2000;
        DSB((u16)(si + 0xD)) |= 4;
    }
    return si != 0;
}

/* 5a75:0f72 */
s16 sx_play_ex(u16 type, u16 pos, u16 bend, u16 vel, u16 flags, u16 player)
{
    if (!DSB(AP_DS_sx_active)) return 0;
    u16 si = sx_add((s16)type, pos, flags, (u8)player);
    if (si) {
        DSW((u16)(si + 6)) = bend;
        DSW((u16)(si + 2)) = vel;
    }
    return si != 0;
}
