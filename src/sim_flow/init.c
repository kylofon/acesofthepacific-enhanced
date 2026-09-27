/* sim_flow: init.c (4e0d: sim_init, mission set-up and tear-down, SIMPREFS.PRF, ui_load), the
 * gauge/cockpit state reset (4963) and the player advisories (10e5) — sim_flow.md §3, §6.1,
 * sim_flow_cockpit.md §3, §6.4. main, prefs_load/save and sim_exit are platform's (ap_main.c). */
#include "sim_flow/sfa_priv.h"
#include "platform/vga.h"

#define ASSERT_INIT_C far_make(SEG(0x386B), 0)   /* "init.c" */

/* AP 1660:1302 (not indexed in modules.csv; radio orders segment, sim_flow part): the radio
 * order page = -2 (top). Called once by sim_init. */
static void radio_orders_init(void)
{
    DSW(0x5BD0) = 0xFFFE;
}

/* AP 4e0d:0c29 ui_load — the "alerts" bitmaps, the sliders, the dialog pointer buffer */
static void ui_load(void)
{
    DSW(0x3BD7) = bmp_load_planes(res_name(make_filename_dev(0x03A8 /* "alerts" */, 0x05B4 /* "BMP" */), 0x05B4));
    if (DSW(0x3BD7) == 0) assert_fail(ASSERT_INIT_C, 0x363, 0);
    sliders_load(res_name(make_filename_dev(0x03AF /* "sliders" */, 0x05B4), 0x05B4));
    if (pointer_buf_alloc(DSW((u16)(DSW(0x3BD7) + 0x18))) == 0) assert_fail(ASSERT_INIT_C, 0x366, 0);
}

/* AP 4e0d:0835 cpu_benchmark. PORT (sim_flow.md §9 note 2): the original times 32 x 64 rounds of
 * matrix products on the PIT; the port returns a fixed "fast machine" result so that the slider
 * defaults do not depend on the host. */
static s16 cpu_benchmark(void)
{
    return 100;
}

/* AP 4e0d:09d0 simprefs_load — SIMPREFS.PRF (10 bytes + the saved views), first-run defaults */
static void simprefs_load(void)
{
    u16 f = my_fopen(0x0344 /* "simprefs.prf" */, 0x05EB /* "rb" */);
    if (f) {
        my_fread(AP_DS_simprefs, 10, 1, f);
        views_save(f);                        /* 1c30:0007: reads the 10 saved views (see views.c) */
        my_fclose(f);
    } else {
        rtl_memcpy(AP_DS_simprefs, AP_DS_simprefs_default, 10);
    }
    if (!ap_file_exists(0x0351 /* "sim.out" */)) {
        s16 t = cpu_benchmark();
        s16 lo1 = slider_lo(1), hi1 = slider_hi(1), mid1 = (s16)(lo1 + ((s16)(hi1 - lo1) >> 1));
        s16 lo2 = slider_lo(2), hi2 = slider_hi(2), mid2 = (s16)(lo2 + ((s16)(hi2 - lo2) >> 1));
        s16 lo0 = slider_lo(0), hi0 = slider_hi(0), mid0 = (s16)(lo0 + ((s16)(hi0 - lo0) >> 1));
        if (t > 0xED)      { DSW(0x43A9) = (u16)lo1;  DSW(0x43AB) = (u16)lo2;  DSW(0x43A7) = (u16)hi0; }
        else if (t > 0xAF) { DSW(0x43A9) = (u16)lo1;  DSW(0x43AB) = (u16)mid2; DSW(0x43A7) = (u16)mid0; }
        else if (t > 0x70) { DSW(0x43A9) = (u16)mid1; DSW(0x43AB) = (u16)hi2;  DSW(0x43A7) = (u16)mid0; }
        else               { DSW(0x43A9) = (u16)hi1;  DSW(0x43AB) = (u16)hi2;  DSW(0x43A7) = (u16)mid0; }
    }
    slider_set(1, DSS(0x43A9));
    slider_set(2, DSS(0x43AB));
    slider_set(0, DSS(0x43A7));
    SEGW(0x3974, 0x0209) = DSW(0x43AD);
    director_mode(1);
}

/* AP 4e0d:0b3e — sim_flow.md §3.2 */
void simprefs_save(void)
{
    DSW(0x43A9) = (u16)slider_value(1);
    DSW(0x43AB) = (u16)slider_value(2);
    DSW(0x43A7) = (u16)slider_value(0);
    DSW(0x43AD) = SEGW(0x3974, 0x0209);
    u16 f = my_fopen(0x0344 /* "simprefs.prf" */, 0x03A5 /* "wb" */);
    if (f) {
        my_fwrite(AP_DS_simprefs, 10, 1, f);
        views_load(f);                        /* 1c30:0020: writes the 10 saved views */
        my_fclose(f);
    }
}

/* AP 4e0d:0000 — sim_flow.md §3.1 */
void sim_init(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x20);
    arena_init(0x2F44);
    prefs_load();
    if (!is_debug()) DSW(AP_DS_drv_hw_present) = 0;
    s16 ok = gfx_init((s16)DSW(AP_DS_control_prf), 0x80, res_name(0x0359 /* "vmcode2" */, 0x05DC /* "OVL" */));
    DSW(0x4012) = DSW(AP_DS_drv_page_front);
    if (ok == 0) assert_fail(ASSERT_INIT_C, 0x8E, 0);
    ds_far_wr(0x3586, far_make(SEG(0x0AA4), 0x0024));          /* text vector for the driver */
    if (ap_fonts_load() == 0) assert_fail(ASSERT_INIT_C, 0x92, 0);
    u32 p = pal_load(res_name(0x0361 /* "ACES" */, 0x05CC /* "PAL" */));
    DSL(AP_DS_sim_palette) = p;
    p = pal_load(res_name(0x0366 /* "clouds" */, 0x05CC));
    DSL(AP_DS_clouds_palette) = p;
    if (DSL(AP_DS_sim_palette) == 0) assert_fail(ASSERT_INIT_C, 0x97, 0);
    if (is_debug() || DSC(AP_DS_drv_dac_flag) == 0) {
        pal_set(ds_far(AP_DS_sim_palette));
    } else {
        read_front();
        write_back();
        copy_screen();
    }
    DSW(0x408E) = 1;
    DSW(AP_DS_redraw_pages) = 2;
    divzero_install();
    if (DSS(AP_DS_opt_sound_drv) != -1) DSW(AP_DS_cfg_sound_drv) = DSW(AP_DS_opt_sound_drv);
    if (DSW(AP_DS_opt_no_sound)) {
        if (DSW(AP_DS_sx_init_off) == 0) {
            timer_install(13);
            timer_add(codeptr_far(0x000002DBu), 1);
        } else {
            sim_tick_cb();
        }
    } else {
        rtl_strcpy(buf, res_name(0x036D /* "snd1" */, 0x05BC /* "DAT" */));
        sx_init(res_name(0x0372 /* "sx1" */, 0x05DC /* "OVL" */), buf, 0x0376 /* "simsnds1.sng" */,
                DSW(AP_DS_cfg_sound_drv), (u16)(u8)DSB(AP_DS_sx_init_off));
        if (DSW(AP_DS_cfg_sound_drv) == 0x88 && !is_debug()) assert_fail(ASSERT_INIT_C, 0xE4, 0);
    }
    floppy_off();
    input_init();
    ap_modules_detect();
    sx_enable((s16)(s8)DSB(AP_DS_cfg_sfx_on));
    savebuf_alloc();
    ui_load();
    simprefs_load();
    prefs_init();
    realism_dlg_init();
    /* world/object subsystems, one init each (sim_flow.md §3.1 step 10) */
    scenario_module_init();
    damage_init();
    para_load();
    stars_load();
    paltrick_load();
    str_pool_load();
    gauges_init_stub();
    goal_init();
    detail_table_init();
    bullets_load(0x0383 /* "bullets" */);
    bombs_load(0x038B /* "bombs" */);
    torps_load(0x0391 /* "torps" */);
    rockets_load(0x0397 /* "rockets" */);
    shadows_default();
    flak_register();
    damage_tables_load();
    death_classes_register();
    ovl_init_nop();
    manoeuvres_init();
    area_init();
    proximity_nop();
    forms_load();
    ship_classes_register();
    flight_states_register();
    rand_table_init();
    fly_module_end();
    hold_register();
    /* 1142:1421: empty */
    combat_end();
    dogfight_register();
    /* 0ac2:03fa: empty */
    spotting_nop();
    flights_nop();
    radio_init();
    objects_module_end();
    clouds_load_boxes();
    tactic1027_register();
    brain_module_init();
    autopilot_init();
    tactics_register();
    nop_5b91();
    radio_orders_init();
    g_attack_init();
    spawn_nop();
    end_init();
    wp_init();
    sky_alloc();
    mouse_enable_set((s16)(s8)DSB(AP_DS_control_prf + 7));
    stk_release(m);
}

/* AP 4e0d:035c — sim_flow.md §3.4 */
void sim_shutdown(void)
{
    end_screen();
    arena_done();
    savebuf_free();
    sprite_set_free(0);
    sprite_set_free(1);
    macros_free();
    ob_done();
}

/* AP 4e0d:038e — sim_flow.md §3.3 */
void ap_mission_load(FarPtr script, u16 len)
{
    DSW(0x0326) = arena_alloc(0);
    scenario_scan(script, (s16)len);
    far_dot_alloc();
}

/* AP 4e0d:03b4 */
void mission_unload(void)
{
    arena_release(DSW(0x0326));
    damage_nop();
    macros_clear();
    gid_free();
    scenario_unload();
}

/* AP 4e0d:03d9 */
void mission_request_dissolve(void)
{
    DSW(0x032C) = 1;
}

/* AP 4e0d:03e4 rng_hook_nop — the (empty) far hook stored in the RNG states */
static void rng_hook_nop(void) {}

/* AP 4e0d:03e9 — sim_flow.md §3.3 */
void mission_start(void)
{
    module_flags(0);
    ap_rng_seed(AP_DS_rng_state);
    ds_far_wr(0x44C6, far_make(SEG(0x3CD3), 0x0061));          /* stub of 4e0d:03e4 */
    ap_rng_seed(AP_DS_rng_aux);
    ds_far_wr(0x4450, far_make(SEG(0x3CD3), 0x0061));
    DSW(0x0328) = arena_alloc(0);
    DSL(AP_DS_mission_time2) = 0;
    DSL(AP_DS_mission_time) = 0;
    sky_init(DSW(0x4787), (u16)(s16)(s8)DSB(0x4789), DSW(0x478D), DSW(0x478B));
    class_reset();
    scenario_build();
    vcr_world_reset();
    results_init();
    map_reset();
    near_lists_alloc();
    if (DSW(0x032C)) {
        DSW(0x032C) = 0;
        wipe_black();
    }
    timebase_reset();
    input_reset();
    realism_bind(AP_DS_realism_prf);
    views_init();
    sliders_apply();
    realism_init();
    fx_pool_create();
    objects_clamp_world();
    sx_enable(-1);
    vcr_world_init_hook();
    view_setup();
    frame_reset();
    clouds_init();
    palette_effects_init();
    g_load_reset();
    weapons_init(0x28);
    msg_init();
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_front);
    DSB(AP_DS_drv_fill_col) = 0;
    DSB(AP_DS_drv_draw_col) = 0;
    gfx_rect(0, 0, 320, 200);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    gfx_rect(0, 0, 320, 200);
    pal_set(ds_far(AP_DS_sim_palette));
    clock_reset();
    objects_module_init();
    class_reset();
    fly_module_init();
    elements_init();
    damage_reset();
    bullets_reset();
    wp_flag_set();
    lso_reset();
    advice_init();
    sky_invalidate();
    end_reset();
    area_free();
    ships_land_check();
    help_msg_reset();
    midair_grace_reset();
    radio_menu_reset();
    flak_reset();
    DSB(AP_DS_step_flags) |= 0x80;
}

/* AP 4e0d:058e — sim_flow.md §3.3 */
void mission_end(void)
{
    arena_release(DSW(0x0328));
    results_cleanup();
    scenario_free();
    fx_pool_free();
    weapons_free();
    elements_free();
    movers_reset();
    ap_view_free(0);
    sky_free();
    gunlist_loadout_free();
    near_lists_free();
    frames_stop();
    wp_buffers_init();
    heap_hook();
}

/* ---- 4963: gauge / cockpit state -------------------------------------------------------------------- */

/* AP 4963:0000 */
void gauges_init_stub(void)
{
    gauges_stub();
}

/* AP 4963:000a — sim_flow_cockpit.md §3 */
void cockpit_reset(void)
{
    gauges_reset();
    DSW(0x3ED6) = 0xFFFF;
    DSW(0x3ED9) = 0;
    DSB(0x3EDB) = 0;
    DSW(0x3EDC) = 0;
    DSB(0x3EDE) = 1;
    DSW(0x3EDF) = 0;
    DSW(0x3EE1) = 0;
    DSW(0x3EE3) = 0;
    DSW(0x3EE5) = 0;
    DSW(0x3EE7) = 0;
    DSW(0x3EE9) = 0;
    DSW(0x3EF5) = 0;
    DSB(0x3EF7) = 0;
    DSB(0x3EF8) = 0;
    DSW(0x3EF9) = 0;
    DSW(0x3EFB) = 0;
}

/* ---- 10e5: player advisories ------------------------------------------------------------------------ */

/* AP 10e5:0005 — sim_flow_cockpit.md §6.4 */
void advice_init(void)
{
    memset(DSP(0x57C6), 0, 23);
    memset(DSP(0x57DD), 0, 13);
    DSW(0x57DB) = 0xFFFF;
    DSW(0x57E8) = 0xFFFF;
    u16 p = PLAYER();
    if (DSW(p + 0x4A) != 0x3F4 && DSW(p + 0x4A) != 0x408) {
        DSB(0x57CF) = 1;
    } else {
        DSL(0x57D3) = 0x1900;
        if (!(DSB(0x3FD1) & 8)) DSB(0x57CD) = 1;
    }
    warn_init(0x57B2, 1, 8, 3);
    warn_init(0x57BC, 1, 0x11, 3);
    warn_init(0x57A8, 1, 0x0F, 3);
    warn_init(0x579E, 1, 0x10, 2);
}

static void advise(s16 id, s16 prio)
{
    msg_show(str(1, id), prio, far_make(0, 0));
}

/* AP 10e5:00ac — sim_flow_cockpit.md §6.4: the first matching advisory of this step */
void advice_step(void)
{
    u16 fm = (u16)(PLAYER() + 0x64);
    u16 cb = (u16)(PLAYER() + 0xAC);
    u8 f46 = DSB(fm + 0x46);
    if (DSB(0x57E7)) { advise(0x45, 3); goto done; }
    if (DSB(0x57DD)) { warn_tick(0x57A8); goto done; }
    if (DSB(0x57DE)) { warn_tick(0x57BC); goto done; }
    if (DSB(0x57E0) && !DSB(0x57D2)) { advise(7, 3); DSB(0x57D2) = 1; goto done; }
    if (DSB(0x57E1)) { warn_tick(0x57B2); goto done; }
    if (DSB(0x57C8)) DSB(0x57C8) = (f46 >> 1) & 1;
    if ((f46 >> 2) & 1) {
        if (!DSB(0x57C9)) { DSB(0x57C9) = 1; advise(0x0D, 3); goto done; }
    } else {
        DSB(0x57C9) = 0;
    }
    if (f46 & 1) {
        if (!DSB(0x57C7)) { DSB(0x57C7) = 1; advise(0x0C, 3); goto done; }
    } else if (DSB(0x57C7)) {
        msg_clear_top();
        advise(0x0E, 1);
        DSB(0x57C7) = 0;
        DSB(0x57C8) = 1;
        goto done;
    }
    if (((f46 >> 1) & 1) && !DSB(0x57C8)) {
        if (!DSB(0x57C6)) { DSB(0x57C6) = 1; advise(0x0B, 3); goto done; }
    } else {
        DSB(0x57C6) = 0;
    }
    if (DSW(0x57E8) != DSW(0x57DB)) {
        DSW(0x57DB) = DSW(0x57E8);
        if (DSS(0x57E8) >= 0) { advise((s16)(DSS(0x57E8) + 0x3C), 2); goto done; }
    }
    if (DSB(0x57E2)) { warn_tick(0x579E); goto done; }
    if (DSB(0x57DF) && !DSB(0x57D1)) { advise(0x0A, 1); DSB(0x57D1) = 1; goto done; }
    if (DSB(0x57E5)) { advise(0x3B, 1); goto done; }
    if (DSB(0x57E6)) { advise(0x44, 1); goto done; }
    if (DSB(0x57E4)) { advise(1, 1); goto done; }
    if (DSB(0x57E3)) { advise(0, 1); goto done; }
    if (DSB(0x57CF)) goto done;
    DSB(0x57CB) = DSS(fm + 8) > 0;
    if (!DSB(0x57CB) && DSL(AP_DS_mission_time) > DSL(0x57D3)) { advise(0x16, 1); goto done; }
    if (!DSB(0x57CC) && DSS(fm + 8) > 0x50) {
        s16 g = (s16)(DSW(cb + 0x0C) + DSW((u16)(DSW(fm + 2) + 0x98)));
        if ((s32)g == DSSL(PLAYER() + 0x0C)) { advise(0x17, 1); goto done; }
        DSB(0x57CC) = 1;
        DSL(0x57D3) = DSL(AP_DS_mission_time) + 0x3200;
        DSL(0x57D7) = DSL(AP_DS_mission_time) + 0x4B00;
        goto done;
    }
    if (DSB(0x57CC) && !DSB(0x57CD) && DSL(AP_DS_mission_time) > DSL(0x57D3)) {
        if ((DSB(cb + 0x0E) >> 4) & 1) { advise(0x18, 1); goto done; }
        DSB(0x57CD) = 1;
        goto done;
    }
    if (DSB(0x57CC) && !DSB(0x57CE) && DSL(AP_DS_mission_time) > DSL(0x57D7)) {
        if (DSB(cb + 0x0E) & 3) { advise(0x1A, 1); goto done; }
        DSB(0x57CE) = 1;
        DSB(0x57CF) = 1;
    }
done:
    memset(DSP(0x57DD), 0, 11);
}

/* AP 0aa4:0024 (asm, not indexed; installed as driver entry 27 by sim_init) � a span-list fill in
 * one colour (DS:26CD, no dither) on the write page DS:26D8: list {y0, n, n x (x0, x1)}; a row with
 * x1 < x0 is skipped; quirk kept: the row loop is a do-while (n = 0 still draws one row). */
static void span_fill_solid(FarPtr sp)
{
    u8 c = DSB(0x26CD);
    u8 *pg = vga_page(DSW(AP_DS_drv_page_write));
    s16 y = (s16)rd16(sp.seg, sp.off);
    s16 n = (s16)rd16(sp.seg, (u16)(sp.off + 2));
    u16 p = (u16)(sp.off + 4);
    do {
        s16 x0 = (s16)rd16(sp.seg, p), x1 = (s16)rd16(sp.seg, (u16)(p + 2));
        p = (u16)(p + 4);
        if (x1 >= x0) {
            for (s32 x = x0; x <= x1; x++) {
                s32 a = (s32)y * 320 + x;
                if (pg && a >= 0 && a < 0x10000) pg[a] = c;
            }
        }
        y++;
    } while (--n > 0);
}

__attribute__((constructor)) static void init_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x4E0D03E4u, rng_hook_nop);
    codeptr_register(PROG_AP, 0x0AA40024u, (CodeFn)span_fill_solid);
}
