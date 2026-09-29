/* sim_flow: the sim frame step, draw order and command handler (5440) and the VCR glue (5d3c) —
 * sim_flow.md §6, §7. 5440:03aa/0269 are render3d's, 5440:07bd/07e1 video's. */
#include "sim_flow/sfa_priv.h"
#include "enhanced/enhanced.h"   /* ENH: flaps up / down commands */

#define P()   PLAYER()
#define CB()  ((u16)(PLAYER() + 0xAC))

/* model data of an object: word DS:55D2[DS:54BC[obj+19h]] */
static u16 obj_model(u16 obj)
{
    return DSW((u16)(DSW(0x55D2) + 2 * DSB(0x54BC + DSB(obj + 0x19))));
}

/* AP 5440:0000 camera_follow(obj) — sim_flow.md §6.1: the camera at the object; in view 0 offset
 * by the model's eye point (model+77h rotated by the object's matrix) */
void camera_follow(u16 obj)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(6);
    u16 eye = (u16)(obj_model(obj) + 0x77);
    memcpy(DSP(AP_DS_camera_pos), DSP(obj + 4), 12);
    DSW(AP_DS_camera_ang) = DSW(obj + 0x10);
    DSW(AP_DS_camera_ang + 2) = DSW(obj + 0x12);
    DSW(AP_DS_camera_ang + 4) = DSW(obj + 0x14);
    if (view_get() == 0) {
        vec_rotate(eye, obj_matrix(DSW(obj + 0x20)), tmp);
        ladd_v(AP_DS_camera_pos, tmp);
    }
    stk_release(m);
}

/* AP 5440:008d */
s16 camera_obj_ref(void)
{
    return (s16)(DSW((u16)(DSW(AP_DS_sim_view) + 2)) + 2);
}

/* AP 5440:009c — sim_flow.md §6.1 */
void view_setup(void)
{
    u16 v = view_new((s16)AP_DS_sim_eye);
    DSW(0x43D8) = v;
    DSW(AP_DS_sim_view) = v;
    DSW(v + 0x12) = 0x4092;
    DSW(v + 0x0A) = DSW(0x06AD);
    DSW(v + 0x0C) = DSW(0x06AF);
    DSW(v + 0x0E) = DSW(0x06B1);
    DSW(v + 0x10) = DSW(0x06B3);
    camera_follow(P());
    scene_begin(DSW(AP_DS_sim_view));
    scene_force_rebuild();
    scene_gather_static(0);
    memcpy(DSP(0x582C), DSP(AP_DS_camera_pos), 12);
    DSW(AP_DS_gun_jam1) = 0;
    DSW(AP_DS_gun_jam2) = 0;
    DSW(AP_DS_unjam_chance) = 0;
    DSW(AP_DS_unjam_timer) = 0;
    DSW(0x435E) = 0;
    DSW(0x435C) = 0;
    DSW(AP_DS_gear_anim) = 0;
}

/* AP 5440:012c */
void ap_view_free(u16 unused)
{
    view_free(DSW(AP_DS_sim_view));
}

/* AP 5440:013d */
void frame_reset(void)
{
    DSL(AP_DS_steps_done) = 0;
    DSB(AP_DS_autopilot_state) = 0;
    DSW(AP_DS_autopilot_timer) = 0;
    DSB(0x069D) = 0;
    DSW(0x4380) = 0;
    DSB(AP_DS_radio_menu_open) = 0;
    DSW(0x069F) = 0;
    DSW(0x06A1) = 0;
    DSW(0x06A3) = 0;
    DSW(0x06A9) = 0;
}

/* AP 5440:017e */
void frames_stop(void)
{
    DSSL(AP_DS_steps_done) = -1;
}

/* AP 5440:018c draw_frame(redraw, flip) — sim_flow.md §6.2 */
s16 ap_draw_frame(s16 redraw, s16 flip)
{
    quit_check();
    if (DSSL(AP_DS_steps_done) < 0) return 0;
    read_back();
    write_back();
    while (!timeout_expired(AP_DS_flip_timeout)) host_pump();   /* >= 3 ticks since the last flip */
    fx_update_ref((u16)camera_obj_ref());
    if (!overcast()) shadows_spawn();
    gun_flashes();
    cockpit_frame_bg(redraw);
    msg_erase();
    lso_erase();
    if ((u16)view_get() <= 3) {
        mask_on();
        render_scene();
        mask_off();
    } else {
        render_scene();
    }
    cockpit_frame_fg(redraw);
    msg_draw(redraw);
    lso_draw();
    if (DSB(AP_DS_radio_menu_open)) radio_menu_draw(redraw);
    if (DSW(0x069F) == 0) palfx_frame();
    frame_present(flip);
    return 1;
}

/* AP 5440:083a end_screen — the "clouds" picture after the session */
void end_screen(void)
{
    DSW(0x26C8) = 0;
    DSW(0x26C4) = 0;
    DSW(0x26C6) = 0x140;
    DSW(0x26CA) = 0xC8;
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_front);
    DSB(AP_DS_drv_draw_col) = 0;
    DSB(AP_DS_drv_fill_col) = 0;
    gfx_rect(0, 0, 320, 200);
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    DSW(0x26D6) = DSW(AP_DS_drv_page_front);
    gfx_copy_rect(0, 0, 0x13F, 0xC7);
    scr_load_planes(res_name(0x06BD /* "clouds" */, 0x0606 /* "SCR" */));
    while (!timeout_expired(AP_DS_flip_timeout)) host_pump();
    pal_set(ds_far(AP_DS_clouds_palette));
    wipe_in(4);
    pal_fx_show_base();
}

/* AP 5440:08d1 (body 08d2) throttle_add(v) — near */
static void throttle_add(s16 v)
{
    u16 cb = CB();
    DSW(cb + 6) = (u16)(DSW(cb + 6) + v);
    if (DSS(cb + 6) > 0xFF) DSW(cb + 6) = 0xFF;
    else if (DSS(cb + 6) < 0) DSW(cb + 6) = 0;
}

/* AP 5440:0904 */
static void throttle_apply(u16 ctl)
{
    if (DSB(ctl + 8) != 0xFF) {
        DSW(P() + 0xB2) = (u16)((s16)DSC(ctl + 8) << 2);
    } else if (DSB(ctl + 9) != 0) {
        throttle_add(dt_scale((s16)((s16)DSC(ctl + 9) * DSS(0x06AB))));
    }
}

/* AP 5440:0944 */
s16 player_ended(void)
{
    return DSB(P() + 0x1A) & 1;
}

/* AP 5440:0955 */
s16 player_flight_flag(void)
{
    return DSB((u16)(DSW(P() + 0x34) + 0x3B)) & 1;
}

static void msg1(s16 id, s16 prio)
{
    msg_show(str(1, id), prio, far_make(0, 0));
}

/* AP 5440:0969 (body 096a) guns_and_ordnance(ctl) — near */
static void guns_and_ordnance(u16 ctl)
{
    u8 f2 = (DSW(ctl + 0x0E) & 2) != 0;
    if ((DSW(ctl + 0x0E) & 1) && !f2) {
        u16 m = stk_mark();
        u16 src = stk_alloc(6);
        DSW(src) = P();
        DSW(src + 2) = 0x0F;
        DSW(src + 4) = 1;
        if ((u8)gun_fire(0, P(), (u16)(P() + 0x10), src)) {
            DSB(P() + 0x44) |= 1;
            DSB(P() + 0x48) |= 1;
        }
        stk_release(m);
    }
    if (DSW(ctl + 0x10) == 0x0C) {
        if (rockets_left(P())) {
            sx_play(0x15, (u16)(P() + 4), (u16)(P() + 0x70), 1);
            for (s16 i = 2; i--;) rocket_release(P());
        } else if (!DSB(0x42D1)) {
            msg1(0x15, 1);
        }
    } else if (DSW(ctl + 0x10) == 0x0B) {
        if (has_ordnance(P())) release_ordnance(P(), 0);
        else if (!DSB(0x42D1)) msg1(0x14, 1);
    }
}

/* AP 5440:0a8d — called by the gun code */
void guns_jammed(void)
{
    msg1(2, 1);
    DSW(AP_DS_unjam_timer) = 0x0FA0;
    DSW(AP_DS_unjam_chance) = 0;
}

/* AP 5440:0abb stick_curve(v): the square law */
static s16 stick_curve(s16 v)
{
    s16 r = ldiv_sat((s32)v * v, 0xF0);
    return v < 0 ? (s16)-r : r;
}

/* ground contact test used by the command handler: CB+0C + gear height >= altitude */
static bool on_ground(void)
{
    s16 g = (s16)(DSW(CB() + 0x0C) + DSW((u16)(DSW(P() + 0x66) + 0x98)));
    return (s32)g >= DSSL(P() + 0x0C);
}

/* AP 5440:0aee commands(ctl) — sim_flow.md §7; returns 0 */
static s16 commands(u16 ctl)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x10);                /* bp-1Ch: recipients list */
    u16 cb = CB();
    if (DSB(AP_DS_blacked_out)) {
        DSW(cb + 4) = 0;
        DSW(cb + 2) = 0;
        DSW(cb) = 0;
        goto menu_request;
    }
    if (DSW(ctl + 0x0E) & 2) {
        DSW(cb + 2) = 0;
        DSW(cb) = 0;
    } else {
        DSW(cb) = (u16)stick_curve((s16)(DSW(ctl) << 3));
        DSW(cb + 2) = (u16)stick_curve((s16)(DSW(ctl + 2) << 3));
    }
    DSW(cb + 4) = (u16)(DSW(ctl + 4) << 4);
    /* stopped on the ground / deck */
    if (DSW(P() + 0x72) == 0 && DSW(P() + 0x70) == 0 && DSW(P() + 0x74) == 0 && DSW(P() + 0x4A) == 0x458 &&
        on_ground() && DSW(P() + 0x12) == 0 && !(DSB(P() + 0x1A) & 1)) {
        u16 fl = DSW(P() + 0x34);
        if (DSC(fl + 3) == 0) {
            u16 base = DSW(fl + 4);
            if (ldist3((u16)(P() + 4), (u16)(base + 4)) <= 0x4268) {
                DSW(AP_DS_landed_base) = 1;
                end_request(3);
            }
        } else if (DSS(cb + 0x0C) > 0 && DSW(P() + 0xEF) != 0) {
            if ((DSB(P() + 0x1C) & 8) && !(DSB(P() + 0x1C) & 0x10)) DSW(AP_DS_carrier_landing) = 2;
            else DSW(AP_DS_carrier_landing) = 1;
            carrier_rel_save();
            end_request(2);
        }
    }
    /* radio menu */
    if (DSB(AP_DS_radio_menu_open)) {
        if (DSW(0x4348) == 0) {
            s16 n = orders_items(buf, 8);
            if (n != DSS(0x434A) || memcmp(DSP(buf), DSP(0x434C), (size_t)(u16)(n * 2)) != 0) {
                memcpy(DSP(0x434C), DSP(buf), (size_t)(u16)(n * 2));
                radio_menu_build(n, buf);
                DSW(AP_DS_redraw_pages) = 2;
                vcr_redraw_all();
                redraw_all();
                DSW(0x434A) = (u16)n;
            }
        }
        s16 r = radio_menu_key((s16)DSW(ctl + 0x10));
        if (r > -1) DSW(ctl + 0x10) = (u16)(r + 0x18);
    }
menu_request:
    if (DSW(ctl + 0x10) == 9 && !DSB(AP_DS_radio_menu_open)) {
        orders_reset();
        DSW(0x434A) = (u16)orders_items(0x434C, 8);
        if (DSW(0x434A)) {
            radio_menu_build((s16)DSW(0x434A), 0x434C);
            DSW(0x4348) = 0;
        }
    }
    if (DSW(AP_DS_unjam_timer)) timer16_dec(AP_DS_unjam_timer);
    /* gear forced down on the ground */
    if (!(DSB(0x42D3) && DSB(0x42CF)) && !(DSB(P() + 0x1A) & 1) && !((DSB(cb + 0x0E) >> 4) & 1) &&
        DSB(0x0FEC) == 0 && on_ground()) {
        DSB(cb + 0x0E) |= 0x10;
        u8 last = (u8)(shape_nvariants((s16)DSW(P() + 2), 1) + 0xFF);
        DSB((u16)(DSW(P() + 0x16) + 1)) = last;
        msg1(0x20, 1);
    }
    switch (DSW(ctl + 0x10)) {
    case 2:                                                      /* U: unjam */
        if ((DSW(AP_DS_gun_jam2) || DSW(AP_DS_gun_jam1)) && DSW(AP_DS_unjam_timer) == 0) {
            s16 r = (s16)(ap_rng_next(AP_DS_rng_state) & 0x1023);
            if (r < DSS(AP_DS_unjam_chance)) {
                DSW(AP_DS_gun_jam1) = 0;
                DSW(AP_DS_gun_jam2) = 0;
                msg1(3, 5);
            } else {
                DSW(AP_DS_unjam_chance) = (u16)(DSW(AP_DS_unjam_chance) + 0x19);
                DSW(AP_DS_unjam_timer) = 0x0FA0;
                msg1(4, 5);
            }
        }
        break;
    case 4:                                                      /* L: gear */
        if (DSW(AP_DS_gear_anim) != 0) break;
        if (DSB(obj_model(P()) + 0x76) != 0) break;
        if ((DSB(cb + 0x0E) >> 4) & 1) {
            if (on_ground()) break;
            DSB(cb + 0x0E) &= 0xEF;
            DSW(AP_DS_gear_anim) = 0xFFFF;
        } else {
            DSW(AP_DS_gear_anim) = 1;
            DSB(cb + 0x0E) = (u8)((DSB(cb + 0x0E) & 0xEF) | 0x10);
        }
        break;
    case 5: {                                                    /* F: flaps */
        u8 fl = DSB(cb + 0x0E) & 3;
        fl = fl == 2 ? 0 : (u8)((fl + 1) & 3);
        DSB(cb + 0x0E) = (u8)((DSB(cb + 0x0E) & 0xFC) | fl);
        if (!DSB(0x42D1)) msg1((s16)(0x22 + (DSB(cb + 0x0E) & 3)), 1);
        DSW(0x435C) = 4;
        break;
    }
    case ENH_CMD_FLAPS_DOWN:                                     /* ENH: flaps one step down / up */
    case ENH_CMD_FLAPS_UP: {
        u8 fl = DSB(cb + 0x0E) & 3, to = fl;
        if (DSW(ctl + 0x10) == ENH_CMD_FLAPS_DOWN) { if (fl < 2) to = (u8)(fl + 1); }
        else if (fl > 0) to = (u8)(fl - 1);
        if (to == fl) break;
        DSB(cb + 0x0E) = (u8)((DSB(cb + 0x0E) & 0xFC) | to);
        if (!DSB(0x42D1)) msg1((s16)(0x22 + to), 1);
        DSW(0x435C) = 4;
        break;
    }
    case 6:                                                      /* B: dive brakes */
        if (!(DSB(0x3FD1) & 2)) break;
        DSB(cb + 0x0E) = (u8)((DSB(cb + 0x0E) & 0xFB) | ((((DSB(cb + 0x0E) >> 2) & 1) ? 0 : 1) << 2));
        if (!DSB(0x42D1)) msg1((s16)(0x1C + ((DSB(cb + 0x0E) >> 2) & 1)), 1);
        DSW(0x435E) = 4;
        break;
    case 7:                                                      /* W: wheel brakes */
        DSB(cb + 0x0E) = (u8)((DSB(cb + 0x0E) & 0xF7) | ((((DSB(cb + 0x0E) >> 3) & 1) ? 0 : 1) << 3));
        msg1((s16)(0x1E + ((DSB(cb + 0x0E) >> 3) & 1)), 1);
        break;
    case 8:                                                      /* D: drop tanks */
        if (DSB(P() + 0x1A) & 4) {
            msg1(9, 2);                                          /* STR(1, 9), prio 2 */
            drop_tank_release(P());
        }
        break;
    case 0x0A:
        station_cycle((u16)(P() + 0x5E));
        break;
    case 0x0D:                                                   /* Ctrl+B: bail out */
        if (DSSL(P() + 0x0C) < 6000) {
            msg1(0x3A, 1);
        } else if (DSS(AP_DS_blackout_level) < 0x3000) {
            if (DSS(AP_DS_g_load) >= 0x280) msg1(0x47, 1);
            else {
                DSB(AP_DS_player_bailed) = 1;
                end_request(4);
            }
        }
        break;
    case 3: case 9: case 0x0B: case 0x0C:
        break;
    default: {                                                   /* 18h..1Eh: radio menu lines */
        s16 c = (s16)DSW(ctl + 0x10);
        if (c >= 0x18 && c <= 0x1E && DSB(AP_DS_radio_menu_open)) {
            s16 i = (s16)(c - 0x18);
            if (i < DSS(0x434A) && order_send((s16)DSW(0x434C + 2 * i), i)) DSW(0x4348) = 1;
        }
        break;
    }
    }
    /* timed run (-t) */
    if (DSW(AP_DS_timed_run) && timeout_expired(AP_DS_timed_run_timeout)) {
        s16 n;
        if (DSS(AP_DS_timed_run_units) > 100) n = 100;
        else if (DSS(AP_DS_timed_run_units) > 0) n = DSS(AP_DS_timed_run_units);
        else { sim_exit(1, 0); n = 0; }
        DSW(AP_DS_timed_run_units) = (u16)(DSW(AP_DS_timed_run_units) - n);
        timeout_start(AP_DS_timed_run_timeout, (s32)n * 236);
    }
    /* gear animation */
    if (DSW(AP_DS_gear_anim) != 0) {
        u16 st = (u16)(DSW(P() + 0x16) + 1);
        u8 s = (u8)(DSB(st) + (u8)DSW(AP_DS_gear_anim));
        DSB(st) = s;
        if (s != 0 && (s16)s < (s16)(shape_nvariants((s16)DSW(P() + 2), 1) - 1)) {
            if (DSW(AP_DS_view_inside)) sx_play(0x0D, (u16)(P() + 4), (u16)(P() + 0x70), 1);
        } else {
            if (!DSB(0x42D1)) msg1(DSW(AP_DS_gear_anim) == 1 ? 0x20 : 0x21, 1);
            if (DSW(AP_DS_view_inside)) sx_play(0x0E, (u16)(P() + 4), (u16)(P() + 0x70), 1);
            DSW(AP_DS_gear_anim) = 0;
        }
    }
    /* flap / dive brake motor sounds */
    if (DSW(0x435E)) {
        if (DSW(AP_DS_view_inside)) sx_play(0x0D, (u16)(P() + 4), (u16)(P() + 0x70), 1);
        DSW(0x435E) -= 1;
    }
    if (DSW(0x435C)) {
        if (DSW(AP_DS_view_inside)) sx_play(0x0D, (u16)(P() + 4), (u16)(P() + 0x70), 1);
        DSW(0x435C) -= 1;
    }
    stk_release(m);
    return 0;
}

/* AP 5440:12b9 — leave the autopilot */
void skip_cancel(void)
{
    view_force_cmd(0x1C);
    DSB(AP_DS_autopilot_state) = 0;
    redraw_all();
    camera_follow(P());
    DSW(0x06A9) = 3;
}

/* AP 5440:12e3 */
static void mission_time_tick(void)
{
    timer32_inc(AP_DS_mission_time);
}

/* AP 5440:12f3 long_eq (register helper: BX, AX): the first two longs equal */
static s16 long_eq(u16 bx, u16 ax)
{
    return DSL(bx) == DSL(ax) && DSL(bx + 4) == DSL(ax + 4);
}

/* AP 5440:1317 sim_step(ctl) -> 0 / 2 — sim_flow.md §6.1 */
s16 sim_step(u16 ctl)
{
    s16 result = 0;
    vcr_poll_panel();
    mission_time_tick();
    DSB(AP_DS_step_flags) &= 0x7F;
    if (DSB(AP_DS_autopilot_state)) {
        u16 t = map_target();
        bool cancel = false;
        if ((DSW(0x4346) == 0 && t != 0) || (DSW(0x4346) != 0 && t == 0)) cancel = true;
        else if (t != 0 && !(u8)long_eq(t, 0x433A)) cancel = true;
        if (cancel) {
            skip_cancel();
            msg1(0x34, 1);
            DSB(0x069D) = 2;
        }
    }
    if (DSB(AP_DS_autopilot_state) == 2) {
        autopilot_jump();
        skip_cancel();
        goto end;
    }
    if (DSW(0x06A9)) DSW(0x06A9) -= 1;
    sx_clear_continuous();
    if (DSB(AP_DS_autopilot_state) == 0) {
        if (DSB(0x069D)) {
            DSB(0x069D) -= 1;
            if (DSB(0x069D) == 0) view_override_set(0);
        }
        if (DSW(ctl + 0x10) == 3 && DSW(0x06A9) == 0) {
            s16 r = autopilot_check();
            if (r == 8) {
                u16 wp_cur = (u16)(DSW(P() + 0x34) + 0x16);
                u16 wp = wp_cursor_pos(wp_cur, 0xFFFF);
                if (wp && lnear((u16)(P() + 4), wp, 50000) == 0 && wp_cursor_advance(wp_cur, 0xFFFF)) r = 0;
            }
            if (r) {
                autopilot_msg(r, 0);
            } else {
                view_override_set(1);
                view_force_cmd(0x21);
                DSB(AP_DS_autopilot_state) = 1;
                autopilot_announce();
                DSB(0x069D) = 1;
                DSB(0x069E) = 2;
                DSW(AP_DS_autopilot_timer) = 0x0BB8;
                midair_grace_set(0x0FA0);
                DSW(0x4346) = map_target();
                if (DSW(0x4346)) memcpy(DSP(0x433A), DSP(DSW(0x4346)), 12);
            }
        }
    } else {
        s16 r = autopilot_check();
        if (r) {
            DSB(AP_DS_autopilot_state) = 0;
            view_force_cmd(0x1C);
            autopilot_msg(r, 1);
        } else {
            if (DSB(0x069E)) {
                view_force_cmd(0x28);
                DSB(0x069E) -= 1;
            }
            if (timer16_dec(AP_DS_autopilot_timer) == 0) DSB(AP_DS_autopilot_state) = 2;
        }
    }
    fx_run();
    throttle_apply(ctl);
    result = commands(ctl);
    gauges_update();
    world_ai_update();
    vcr_poll_panel();
    camera_follow(P());
    clouds_nop2();
    sky_update();
    realism_timer();
    if (DSB(0x42CF) == 0) DSB(P() + 0xAB) = 0;
    msg_tick();
    if (DSB(0x477C) != 5 && DSW(0x4791) != 0x2F) flak_fire_all();
    spotting_update();
    g_load_update();
    vcr_poll_panel();
    if (near_grid_update()) near_objects_build();
    if (!(DSB(P() + 0x1A) & 1)) guns_and_ordnance(ctl);
    ordnance_update();
    proximity_update();
    impacts_update();
    vcr_poll_panel();
    goal_update();
    damage_apply();
    lso_frame();
    clock_tick();
    radio_tick();
    if (!DSB(0x0FEC) && !DSB(0x42D1) && !(DSB(P() + 0x1A) & 1)) advice_step();
    midair_grace_tick();
    if (DSB(P() + 0x1A) & 1) result = 2;
    trail_record(P());
end:
    DSB(AP_DS_step_flags) |= 0x80;
    DSL(AP_DS_steps_done) += 1;
    return result;
}

/* ---- 5d3c: VCR glue --------------------------------------------------------------------------------- */

/* AP 5d3c:0000 — the VCR panel's pointer over the drawn page; 1 = panel shown (the flip then waits
 * for the vertical retrace, HANDOFFS C3) */
s16 vcr_panel_mouse_show(void)
{
    if (!DSB(0x08D5)) return 0;
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    vcr_clip_full();
    mouse_show_over();
    return 1;
}

/* AP 5d3c:0023 */
void vcr_panel_mouse_hide(void)
{
    if (!DSB(0x08D5)) return;
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    mouse_over_panel();
}

/* AP 5d3c:003a — the sound listener = the camera */
void vcr_sound_frame(s16 on)
{
    u16 flag = on ? (u16)(DSW(AP_DS_view_inside) == 0) : 0;
    sx_update((u16)(DSW((u16)(DSW(0x43D8) + 2)) + 2), flag);
}

/* AP 5d3c:0064 */
u32 vcr_frame(void)
{
    if (DSB(AP_DS_vcr_session_mode) == 2) return DSL(0x4E3B);
    return DSL(AP_DS_frame_count);
}

/* AP 5d3c:0084 */
void vcr_set_director(u8 on)
{
    DSB(0x092F) = on;
    DSB(0x092E) = on;
    DSB(0x092D) = on;
    DSB(0x088B) = on;
}
