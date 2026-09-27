/* sim_flow: the time base (362b), tick timeouts (4b31), the tick counter callback (0000:02db/02e7),
 * slider effects (4b9f control.c), the mission clock (4ca7) and the flight loop (5cde) —
 * sim_flow.md §4. */
#include "sim_flow/sfa_priv.h"

#include "host.h"

/* ---- 0000:02db / 02e7: the 236.69 Hz tick counter DS:18D2 ------------------------------------- */

/* AP 0000:02db — sim_flow.md §0: timer callback (period 1), +1 per tick (a 32-bit increment done as
 * two words) */
void sim_tick_cb(void)
{
    DSL(AP_DS_tick_count) += 1;
}

/* AP 0000:02e7 — sim_flow.md §4.2: the counter read with interrupts off (DX:AX) */
u32 sim_ticks_read(void)
{
    return DSL(AP_DS_tick_count);
}

/* ---- 4b31: timeouts {u32 start; u16 duration} ----------------------------------------------------- */

/* AP 4b31:0000 — sim_flow.md §4.2 */
s16 timeout_expired(u16 t_ds)
{
    if (DSB(AP_DS_timer_installed) == 0) return 1;
    u32 el = sim_ticks_read() - DSL(t_ds);
    return (u32)DSW(t_ds + 4) < el ? 1 : 0;              /* movzx + cmp, jae -> 0 */
}

/* AP 4b31:0041 — sim_flow.md §4.2. PORT: the callers push a long; only its low word is stored
 * (REQUESTS.md: the parameter is s32). */
void timeout_start(u16 t_ds, s32 n)
{
    DSL(t_ds) = sim_ticks_read();
    DSW(t_ds + 4) = (u16)n;
}

/* AP 4b31:0062 — sim_flow.md §4.2 */
void timeout_expire(u16 t_ds)
{
    DSW(t_ds + 4) = 0;
    DSL(t_ds) = sim_ticks_read() - 1;
}

/* ---- 362b: time base ------------------------------------------------------------------------------ */

/* AP 362b:000d — sim_flow.md §4.3 */
void dt_set(s16 dt)
{
    DSS(AP_DS_dt_scale) = dt;
    DSS(AP_DS_dt_log) = (s16)(lbits((u32)(s32)dt) - 7);
}

/* AP 362b:002d */
void timebase_reset(void)
{
    dt_set(0x100);
    DSS(AP_DS_dt_real) = 0x100;
    DSS(AP_DS_time_div) = 1;
}

/* AP 362b:0048 */
void timebase_set_div(s16 div)
{
    DSS(AP_DS_time_div) = div;
}

/* AP 362b:0053 */
s16 timebase_ticks(void)
{
    return DSS(AP_DS_frame_ticks);
}

/* AP 362b:005b (body 005c) dt_from_ticks(div, ticks) — near */
static s16 dt_from_ticks(s16 div, s16 ticks)
{
    s16 d = ldiv_sat((s32)ticks << 8, div);
    if (d > 0x1C2) d = 0x1C2;
    else if (d < 0x40) d = 0x40;
    return d;
}

/* AP 362b:008f */
void timebase_frame(s16 ticks)
{
    dt_set(dt_from_ticks(DSS(AP_DS_time_div), ticks));
    DSS(AP_DS_dt_real) = dt_from_ticks(DSS(AP_DS_time_div_real), ticks);
    DSS(AP_DS_frame_ticks) = ticks;
}

/* AP 362b:00be */
s16 dt_scale(s16 x)
{
    s32 v = ((s32)x * DSS(AP_DS_dt_scale)) >> 8;
    if (v < -0x7FFF) v = -0x7FFF;
    else if (v > 0x7FFF) v = 0x7FFF;
    return (s16)v;
}

/* AP 362b:0106 */
s16 dt_scale_real(s16 x)
{
    s32 v = ((s32)x * DSS(AP_DS_dt_real)) >> 8;
    if (v < -0x7FFF) return (s16)0x8001;
    if (v > 0x7FFF) return 0x7FFF;
    return (s16)v;
}

/* AP 362b:0146 — 386 idiv; dt >= 40h, so no overflow */
s16 steps_to_frames(s16 n)
{
    return (s16)idiv32_386((s64)((s32)n << 8), DSS(AP_DS_dt_scale), NULL);
}

/* AP 362b:0165 */
s16 timer16_dec(u16 t_ds)
{
    s16 dt = DSS(AP_DS_dt_scale);
    if (DSS(t_ds) < dt) DSS(t_ds) = 0;
    else DSS(t_ds) = (s16)(DSS(t_ds) - dt);
    return DSS(t_ds);
}

/* AP 362b:017c — returns the low word (DX = the high word) */
s16 timer32_dec(u16 t_ds)
{
    s32 dt = DSS(AP_DS_dt_scale);
    if (DSSL(t_ds) < dt) DSSL(t_ds) = 0;
    else DSSL(t_ds) -= dt;
    return (s16)DSSL(t_ds);
}

/* AP 362b:01a0 */
s16 timer16_dec_far(FarPtr t)
{
    s16 dt = DSS(AP_DS_dt_scale);
    s16 *p = (s16 *)far_mp(t);
    if (*p < dt) *p = 0;
    else *p = (s16)(*p - dt);
    return *p;
}

/* AP 362b:01bb */
s16 timer16_inc(u16 t_ds)
{
    DSS(t_ds) = (s16)(DSS(t_ds) + DSS(AP_DS_dt_scale));
    return DSS(t_ds);
}

/* AP 362b:01cc */
s16 timer32_inc(u16 t_ds)
{
    DSSL(t_ds) += DSS(AP_DS_dt_scale);
    return (s16)DSSL(t_ds);
}

/* AP 362b:01e7 */
s16 timer16_inc_far(FarPtr t)
{
    s16 *p = (s16 *)far_mp(t);
    *p = (s16)(*p + DSS(AP_DS_dt_scale));
    return *p;
}

/* AP 362b:01f8 */
s16 timer32_expired(u16 t_ds)
{
    s32 dt = DSS(AP_DS_dt_scale);
    if (DSSL(t_ds) <= dt) { DSSL(t_ds) = 0; return 1; }
    DSSL(t_ds) -= dt;
    return 0;
}

/* AP 362b:021c */
s16 timer16_expired_far(FarPtr t)
{
    s16 dt = DSS(AP_DS_dt_scale);
    s16 *p = (s16 *)far_mp(t);
    if (*p <= dt) { *p = 0; return 1; }
    *p = (s16)(*p - dt);
    return 0;
}

/* AP 362b:023b — the same code as 01cc */
s16 timer32_inc2(u16 t_ds)
{
    return timer32_inc(t_ds);
}

/* AP 362b:0256 */
void dt_scale_frac(u16 x_ds, u16 frac_ds)
{
    s32 p = (s32)DSS(x_ds) * DSS(AP_DS_dt_scale);
    u32 a = p < 0 ? (u32)-p : (u32)p;
    s32 q = p >> 8;
    DSW(frac_ds) = 0x3FF;
    if (q > 0x7FFF) q = 0x7FFF;
    else if (q < -0x7FFF) q = -0x7FFF;
    else DSW(frac_ds) = (u16)((a & 0xFF) << 2);
    if (q < 0) DSW(frac_ds) = (u16)-DSW(frac_ds);
    DSS(x_ds) = (s16)q;
}

/* AP 362b:02e3 — 16-bit product (DX = the high word of the 32-bit product) */
s16 dt_mul(s16 x)
{
    return (s16)((s32)x * DSS(AP_DS_dt_scale));
}

/* ---- 4b9f control.c: slider effects -------------------------------------------------------------- */

/* AP 4b9f:0000 */
void time_scale_apply(void)
{
    timebase_set_div(slider_value(0));
}

/* AP 4b9f:0018 — LOD thresholds of the shape banks from the AIRCRAFT slider, shadow detail */
void aircraft_detail_apply(s16 v)
{
    s16 w = (s16)(v + 1);
    s16 k = (s16)(w >> 5);
    u16 a6 = (u16)((w >> 2) + 0xBF), a8 = DSW(0x0222 + 2 * k);
    u16 b6 = (u16)((w >> 1) + 0x7F), b8 = DSW(0x0234 + 2 * k);
    u16 tab = DSW(AP_DS_shape_tables);
    for (s16 i = DSS(0x3F0E); i <= DSS(0x3F0C); i++) {       /* aircraft shape range */
        DSW(tab + 12 * i + 6) = a6;
        DSW(tab + 12 * i + 8) = a8;
    }
    for (s16 i = DSS(0x3F0A); i <= DSS(0x3F08); i++) {
        DSW(tab + 12 * i + 6) = b6;
        DSW(tab + 12 * i + 8) = b8;
    }
    shadows_set_detail(lerp_eval(0x3F10, (u16)v));
    DSW(0x515A) = 2000;
}

/* AP 4b9f:00d0 */
void world_detail_apply(s16 v)
{
    if (DSB(AP_DS_combat_active) && SEGW(0x3974, 0x0209) == 0) flt_set_detail(0);
    else flt_set_detail((u16)v);
    scene_force_rebuild();
    sky_set_detail((u16)v);
    flak_set_range((u16)v);
    DSB(0x51C4) = v >= 0x55;
}

/* AP 4b9f:012c */
void sliders_apply(void)
{
    aircraft_detail_apply(slider_value(2));
    world_detail_apply(slider_value(1));
    time_scale_apply();
}

/* AP 4b9f:0159 */
void detail_table_init(void)
{
    lerp_setup(0x3F10, 0, 0, 0xFF, 0x14);
}

/* ---- 4ca7: mission clock (game seconds) ------------------------------------------------------------ */

/* AP 4ca7:0000 */
void clock_reset(void)
{
    DSL(AP_DS_mission_clock) = 0;
    DSW(AP_DS_mission_clock_frac) = 0;
}

/* AP 4ca7:0014 */
void clock_tick(void)
{
    DSS(AP_DS_mission_clock_frac) = (s16)(DSS(AP_DS_mission_clock_frac) + dt_scale(200));
    if (DSS(AP_DS_mission_clock_frac) >= 0x400) {
        DSSL(AP_DS_mission_clock) += (s16)(DSS(AP_DS_mission_clock_frac) >> 10);
        DSW(AP_DS_mission_clock_frac) &= 0x3FF;
    }
}

/* AP 4ca7:0045 */
s32 clock_get(void)
{
    return DSSL(AP_DS_mission_clock);
}

/* ---- 5cde: the flight loop ------------------------------------------------------------------------ */

/* AP 5cde:0000 — full redraw of all pages */
void redraw_all(void)
{
    DSW(AP_DS_redraw_pending) = DSW(AP_DS_redraw_pages);
}

/* AP 5cde:000b — pause until a key or click */
void flight_pause(void)
{
    sx_stop_all();
    msg_show(str(1, 6), 2, far_make(0, 0));
    ap_draw_frame(DSS(AP_DS_redraw_pages), 1);
    wait_key_release();
    while (!key_or_click()) host_pump();
    redraw_all();
    msg_clear_top();
}

/* AP 5cde:0057 */
static s16 alert_to_loopcode(s16 r)
{
    if (r == 5) return 3;
    if (r == 8) return 2;
    return 4;
}

/* AP 5cde:0076 */
void end_prompt_reset(void)
{
    DSB(AP_DS_end_prompt_shown) = 0;
}

/* Input record bits (sim_flow.md §5.2) */
#define IN_BIT(in, off, bit) ((DSB((in) + (off)) >> (bit)) & 1)

/* AP 5cde:0080 — flight_keys(in) -> 2 abandon / 3 end / 4 continue (sim_flow.md §4.5) */
static s16 flight_keys(u16 in)
{
    u8 s1 = DSB(AP_DS_end_alert_state) == 1;
    u8 s4 = DSB(AP_DS_end_alert_state) == 4;
    if (s1) return 3;
    if (s4) {
        DSS(AP_DS_end_timer) = (s16)(DSS(AP_DS_end_timer) + dt_scale_real(0x100));
        if (DSS(AP_DS_end_timer) > 0x500) return 3;
    } else if (!DSB(AP_DS_end_prompt_shown)) {
        u8 s23 = DSB(AP_DS_end_alert_state) == 2 || DSB(AP_DS_end_alert_state) == 3;
        if (DSB(AP_DS_mission_over) || s23) {
            s16 any = IN_BIT(in, 0x23, 5) || IN_BIT(in, 0x23, 6) || IN_BIT(in, 0x23, 7) ||
                      IN_BIT(in, 0x24, 0) || DSB(in + 0x1B);
            if (DSB(AP_DS_end_delay_started) == 0) {
                DSB(AP_DS_end_delay_started) = 1;
                DSSL(AP_DS_end_prompt_delay) = s23 ? 0x2800 : 0x9600;
            }
            DSSL(AP_DS_end_prompt_delay) -= dt_scale_real(0x100);
            if (DSSL(AP_DS_end_prompt_delay) <= 0) {
                if (any) {
                    DSSL(AP_DS_end_prompt_delay) = 0;
                } else {
                    DSB(AP_DS_end_prompt_shown) = 1;
                    if (end_danger(DSW(AP_DS_player)) == 0) {
                        sx_stop_all();
                        return alert_to_loopcode(end_prompt());
                    }
                }
            }
        }
    }

    u8 k = DSB(in + 0x1B);
    if (k == 0) return 4;
    u8 alt = IN_BIT(in, 0x22, 0), ctrl = IN_BIT(in, 0x21, 7);
    if (!alt && !ctrl) {
        switch (k) {
        case 0x19:                                           /* P */
            flight_pause();
            break;
        case 0x01: case 0x10:                                /* Esc, Q */
            if (DSB(AP_DS_radio_menu_open) || s1 || s4) break;
            sx_stop_all();
            return alert_to_loopcode(end_prompt());
        case 0x44: {                                         /* F10: preferences */
            sx_stop_all();
            if (prefs_dialog() && DSB(AP_DS_vcr_recording)) {
                u16 m = stk_mark();
                u16 v = stk_alloc(2);
                DSS(v) = slider_value(0);
                vcr_put_data(1, 2, v);
                stk_release(m);
            }
            return 4;
        }
        case 0x32: {                                         /* M: map */
            u16 m = stk_mark();
            u16 saved = stk_alloc(12);
            u16 t = map_target();
            if (t) rtl_memcpy(saved, t, 12);
            sx_stop_all();
            map_mode_on();
            fltmap();
            map_mode_off();
            if (DSB(AP_DS_vcr_recording)) {
                u16 n = map_target();
                if (n) {
                    if (t == 0 || memcmp(DSP(n), DSP(saved), 12) != 0) vcr_put_data(2, 0x0C, n);
                } else if (t != 0) {
                    vcr_put_data(3, 0, 0);
                }
            }
            stk_release(m);
            break;
        }
        default:
            break;
        }
    } else if (alt && !ctrl) {
        switch (k) {
        case 0x13:                                           /* Alt+R: realism */
            sx_stop_all();
            if (realism_dialog_in_flight()) {
                if (DSB(AP_DS_vcr_recording)) vcr_put_data(0, 0x10, AP_DS_realism_prf);
                palfx_frame();
            }
            return 4;
        case 0x24:                                           /* Alt+J */
            flight_message(DSW(0x0948 + 2 * joy_flight_get()));
            break;
        case 0x1F:                                           /* Alt+S */
            flight_message(DSW(0x094C + 2 * (s16)sx_is_on()));
            break;
        case 0x20:                                           /* Alt+D */
            flight_message(DSW(0x0950 + 2 * mouse_flag_get()));
            break;
        case 0x19:
            flight_pause();
            break;
        default:
            break;
        }
    }
    return 4;
}

/* AP 5cde:03fd */
static s16 pit_countdown(void)
{
    return DSS(AP_DS_timer_countdown);
}

/* AP 5cde:0405 — sim_flow.md §4.4. PORT (PLAN.md decision 4): the minimum frame length is
 * host_frame_ticks() (--frame-ticks, default 16) instead of 8 ticks, so the step is not clamped at the
 * default TIME SCALE; ticks = elapsed & ~3 and the dt clamp are the original's. */
s16 flight_loop(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x26);                 /* bp-34h: input record (25h bytes) */
    u16 ctl = stk_alloc(0x14);                /* bp-48h: control record */
    u16 vw = stk_alloc(8);                    /* bp-8: view record */
    u16 tail = stk_alloc(2);                  /* bp-0Ch */
    s16 ended = 0, ret;
    DSW(tail) = 0x0FA0;
    DSB(AP_DS_vcr_session_mode) = 0;
    for (;;) {
        input_poll(in);
        s16 r = flight_keys(in);
        if (r == 3 || r == 2) {
            if (DSB(AP_DS_vcr_recording)) vcr_put(4, 0);
            DSL(0x5060) = DSL(AP_DS_frame_count);
            DSL(0x5064) = (u32)clock_get();
            ret = r == 3;
            break;
        }
        DSS(AP_DS_timer_countdown) = 0x1FF;
        s16 t0 = 0x1FF;
        controls_map(ctl, in);
        if (!ended) view_input(vw, in, 1);
        else for (int i = 0; i < 4; i++) DSW(vw + 2 * i) = DSW(0x0940 + 2 * i);
        ended = (s16)(sim_step(ctl) & 2);
        DSL(AP_DS_frame_count) += 1;
        s16 vchg = view_input_pending(vw);
        if (vchg && (view_update(vw) & 1)) DSW(AP_DS_redraw_pending) = DSW(AP_DS_redraw_pages);
        vcr_sound_frame(1);
        ap_draw_frame(DSS(AP_DS_redraw_pending), 1);
        if (--DSS(AP_DS_redraw_pending) < 0) DSW(AP_DS_redraw_pending) = 0;
        s16 ticks;
        if (DSW(AP_DS_sx_init_off)) {
            ticks = 0x19;
        } else {
            s16 min = (s16)host_frame_ticks();
            for (;;) {
                ticks = (s16)(t0 - pit_countdown());
                if (ticks >= min) break;
                host_pump();
            }
        }
        ticks >>= 2;
        DSW(ctl + 0x12) = (u16)ticks;
        ticks = (s16)(ticks << 2);
        if (DSB(AP_DS_vcr_recording)) {
            vcr_put(0, ctl);
            if (vchg) vcr_put(1, vw);
            if (ended && DSW(tail) == 0) vcr_put(4, 0);
        }
        timebase_frame(ticks);
        if (!ended) continue;
        if (DSW(tail) != 0) { timer16_dec(tail); continue; }
        DSL(0x5060) = DSL(AP_DS_frame_count);
        DSL(0x5064) = (u32)clock_get();
        ret = 1;
        break;
    }
    stk_release(m);
    return ret;
}

__attribute__((constructor)) static void flight_loop_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x000002DBu, sim_tick_cb);
}
