/* sim_flow: vcr_runtime.c — the VCR runtime (5b93: playback step, rewind, views, UI modes), vcr2.c
 * (5c5f: SIM.IN, loading and saving films), the session driver vcr3.c (5c9b) and the demo loop
 * vcr_demo.c (5d56) — sim_flow_vcr.md §6, §8, §9, §12. */
#include <stdio.h>
#include "sim_flow/sfb_priv.h"

#define QUEUE 0x088F

/* PORT (sim_flow_vcr.md §15): the original plays films as fast as it draws. The port waits per
 * played frame until the recorded tick count (ctrl +12h, times 4 PIT ticks) has elapsed since the
 * previous played frame, in play mode, full-screen play and the demo loop (fast forward and seek
 * stay unpaced). This does not affect the simulation. AOTP_VCR_FAST=1 (developer aid) disables it. */
static void vcr_pace(void)
{
    static uint64_t last_ns;                          /* host-side pacing state only */
    static int fast = -1;
    if (fast < 0) {
        const char *e = getenv("AOTP_VCR_FAST");
        fast = e && *e && *e != '0';
    }
    if (fast) { host_pump(); return; }
    uint64_t want = (uint64_t)DSW(0x4DFB) * 4 * 5041u * 1000000000ull / 1193182u;
    uint64_t now = host_ns();
    if (now - last_ns > 1000000000ull) last_ns = now; /* after a pause: do not catch up */
    host_wait_until_ns(last_ns + want);
    last_ns += want;
}

/* Developer aid (host-side): AOTP_VCR_LOG=<file> appends one line per simulated frame, while
 * recording (vcr_put of a control record) and while playing (after the recorded step): the frame
 * number, mission clock, the player's position / attitude and hashes of the two RNG states, so a
 * recording and its playback can be compared line by line. */
static u32 fnv(const u8 *p, int n)
{
    u32 h = 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}
void vcr_debug_log(const char *tag, u32 frame)
{
    static FILE *f;
    static int checked;
    if (!checked) {
        const char *e = getenv("AOTP_VCR_LOG");
        if (e && *e) f = fopen(e, "a");
        checked = 1;
    }
    if (!f) return;
    u16 p = DSW(AP_DS_player);
    if (p) fprintf(f, "%s %lu clk=%ld pos=%ld,%ld,%ld att=%u,%u,%u rng=%08lx,%08lx end=%u\n", tag,
                   (unsigned long)frame, (long)DSSL(AP_DS_mission_clock), (long)DSSL(p + 4), (long)DSSL(p + 8),
                   (long)DSSL(p + 0x0C), DSW(p + 0x10), DSW(p + 0x12), DSW(p + 0x14),
                   (unsigned long)fnv(DSP(AP_DS_rng_state), 0x72), (unsigned long)fnv(DSP(AP_DS_rng_aux), 0x72),
                   DSB(AP_DS_end_alert_state));
    else fprintf(f, "%s %lu (no player)\n", tag, (unsigned long)frame);
    fflush(f);
}

/* Developer aid (host-side): AOTP_MOUSE="t:x,y[,d];..." overrides the pointer read by the dialog and
 * VCR panel polls (dlg_poll, vcr_poll_panel): from t seconds after start the pointer is at (x, y) with
 * the button held for d seconds (default 0.3), then released there. */
void vcr_debug_mouse(u16 ms)
{
    static struct { double t, d; int x, y; } ev[64];
    static int n = -1;
    if (n < 0) {
        n = 0;
        const char *e = getenv("AOTP_MOUSE");
        while (e && *e && n < 64) {
            char *end;
            ev[n].t = strtod(e, &end);
            if (*end != ':') break;
            ev[n].x = (int)strtol(end + 1, &end, 10);
            if (*end != ',') break;
            ev[n].y = (int)strtol(end + 1, &end, 10);
            ev[n].d = 0.3;
            if (*end == ',') ev[n].d = strtod(end + 1, &end);
            n++;
            if (*end != ';') break;
            e = end + 1;
        }
    }
    double now = (double)host_ns() / 1e9;
    int k = -1;
    for (int i = 0; i < n; i++) if (now >= ev[i].t) k = i;
    if (k < 0) return;
    DSW(ms) = (u16)ev[k].x;
    DSW(ms + 2) = (u16)ev[k].y;
    DSW(ms + 4) = now < ev[k].t + ev[k].d ? 1 : 0;
}

/* ---- 5b93 ----------------------------------------------------------------------------------------- */

/* AP 5b93:0000 vcr_world_reset() (from the world init) */
void vcr_world_reset(void)
{
    DSB(0x0886) = 1;
    DSL(0x4E1C) = 0;
    DSW(0x4E1A) = 0;
    DSL(0x0887) = 0;
    DSB(0x4E02) = 0;
    DSB(0x088B) = 1;
    DSB(0x088C) = 0;
}

/* AP 5b93:0032 vcr_queue_reset() */
void vcr_queue_reset(void)
{
    DSB(0x0892) = 0;
    DSB(0x0891) = 0;
}

/* AP 5b93:003f vcr_clip_full(): the clip rectangle below the VCR panel */
void vcr_clip_full(void)
{
    DSW(0x26C4) = 0;
    DSB(0x26C3) = 1;
    DSW(0x26C6) = (u16)(DSW(0x2DAA) - 1);
    DSW(0x26C8) = 0x79;
    DSW(0x26CA) = (u16)(DSW(0x2DAC) - 1);
}

/* AP 5b93:0063 vcr_panel_show() */
void vcr_panel_show(void)
{
    vcr_panel_reset_snap();
    vcr_panel_region();
    dlg_activate(far_make(SEG(0x38E6), 0x15), 0x25);
    u32 n = DSL(0x50A2);
    DSW(0x088D) = 0;
    while (n > 0x7FFF) { n >>= 1; DSW(0x088D) += 1; }
    slider_range(3, 0, (s16)(DSSL(0x50A2) >> (DSB(0x088D) & 0x1F)));
    timeout_start(0x016C, 0);
    vcr_panel_first();
    DSB(0x08D5) = 1;
    set_panel_top(0x78);
    panel_view_labels();
    panel_view_name();
}

/* AP 5b93:00f1 vcr_panel_hide() */
void vcr_panel_hide(void)
{
    DSB(0x08D5) = 0;
    vcr_panel_region0();
    set_panel_top((s16)(DSS(0x2DAC) - 1));
}

/* AP 5b93:010d vcr_panel_region() */
void vcr_panel_region(void)
{
    DSW(0x0B16) = 0;
    DSW(0x0B18) = (u16)(DSW(0x2DAA) - 10);
    DSW(0x0B1A) = 0x79;
    DSW(0x0B1C) = (u16)(DSW(0x2DAC) - 10);
}

/* AP 5b93:0130 vcr_panel_region0() */
void vcr_panel_region0(void)
{
    DSW(0x0B16) = 0;
    DSW(0x0B18) = (u16)(DSW(0x2DAA) - 10);
    DSW(0x0B1A) = 0;
    DSW(0x0B1C) = (u16)(DSW(0x2DAC) - 10);
}

/* AP 5b93:0153 vcr_is_playing() */
s16 vcr_is_playing(void)
{
    return DSB(AP_DS_vcr_session_mode) == 2;
}

/* AP 5b93:0166 vcr_view(v) -> flags of the view change (re-recorded in director mode) */
s16 vcr_view(u16 v)
{
    u16 f19 = DSW(0x19F2);
    s16 kind = view_get();
    u16 tgt = view_outside_target();
    s16 r;
    if (view_input_pending(v)) {
        r = view_update(v);
        if (DSB(0x092D)) vcr_put(1, v);
    } else {
        r = 0;
    }
    if (f19 != DSW(0x19F2)) panel_view_labels();
    if (view_get() != kind || view_outside_target() != tgt) panel_view_name();
    return r;
}

/* AP 5b93:01e2 vcr_redraw_all() */
void vcr_redraw_all(void)
{
    DSW(0x4DFF) = DSW(AP_DS_redraw_pages);
}

/* AP 5b93:01ed vcr_step_recorded(no_sim, rerecord): simulate the frame read last time */
void vcr_step_recorded(s16 no_sim, s16 rerecord)
{
    if (DSW(0x4DFD) != 0) return;
    if (!no_sim) sim_step(0x4DE9);
    if (rerecord) vcr_put(0, 0x4DE9);
    DSL(0x4E3B) += 1;
    vcr_debug_log("P", DSL(0x4E3B));
    timebase_frame((s16)(DSW(0x4DFB) << 2));
}

/* AP 5b93:0231 vcr_play_frame(ignore_views, unused, no_sim, rerecord) -> 1 at the end of the film */
s16 vcr_play_frame(s16 ignore_views, s16 unused, s16 no_sim, s16 rerecord)
{
    (void)unused;
    u16 m = stk_mark();
    u16 sub = stk_alloc(2), len = stk_alloc(2), v = stk_alloc(8), s1 = stk_alloc(2),
        buf16 = stk_alloc(0x10), buf12 = stk_alloc(0x0C);
    s16 done = 0, type;
    vcr_step_recorded(no_sim, rerecord);
    do {
        type = vcr_get(sub, len);
        switch ((u16)type) {
        case 0:
            vcr_take(0x4DE9, 0x14);
            done = 1;
            break;
        case 1:
            vcr_take(v, 8);
            if (!ignore_views) {
                if (vcr_view(v) & 1) vcr_redraw_all();
            } else {
                memcpy(DSP(v), DSP(0x0918), 8);
            }
            break;
        case 2:
            switch (DSW(sub)) {
            case 0:
                vcr_take(buf16, 0x10);
                if (!no_sim) {
                    memcpy(DSP(AP_DS_realism_prf), DSP(buf16), 16);
                    ap_realism_changed_606e();
                }
                if (rerecord) vcr_put_data(0, 0x10, buf16);
                break;
            case 1:
                vcr_take(s1, 2);
                if (!no_sim) {
                    slider_set(0, DSS(s1));
                    time_scale_apply();
                }
                if (rerecord) vcr_put_data(1, 2, s1);
                break;
            case 2:
                vcr_take(buf12, 0x0C);
                if (!no_sim) map_set_target(buf12);
                if (rerecord) vcr_put_data(2, 0x0C, buf12);
                break;
            case 3:
                if (!no_sim) map_set_target(0);
                if (rerecord) vcr_put_data(3, 0, 0);
                break;
            default:
                break;
            }
            break;
        case 4:
            done = 1;
            if (getenv("AOTP_VCR_LOG"))
                fprintf(stderr, "[vcr] END of %s: frames %lu/%lu clock %ld/%ld end_state %u player_ended %d\n",
                        (const char *)DSP(0x4E04), (unsigned long)DSL(0x4E3B), (unsigned long)DSL(0x50A2),
                        (long)clock_get(), (long)DSSL(0x50A6), DSB(AP_DS_end_alert_state), player_ended());
            break;
        default:
            break;
        }
    } while (!done);
    vcr_sound_frame(1);
    stk_release(m);
    return type == 4;
}

/* AP 5b93:0406 vcr_draw() */
void vcr_draw(void)
{
    ap_draw_frame(DSS(0x4DFF), 1);
    dec_floor(0x4DFF);
    static int every = -1;                            /* developer aid: AOTP_VCR_SNAP=n saves every */
    if (every < 0) {                                  /* n-th played frame as a tagged snapshot */
        const char *e = getenv("AOTP_VCR_SNAP");
        every = e ? atoi(e) : 0;
    }
    if (every > 0 && DSB(AP_DS_vcr_session_mode) == 2 && DSL(0x4E3B) % (u32)every == 0) {
        char tag[40];
        snprintf(tag, sizeof tag, "%.8s_f%04lu", (const char *)DSP(0x4E04), (unsigned long)DSL(0x4E3B));
        host_snapshot_now(tag);
    }
}

/* AP 5b93:0424 vcr_draw_pending() */
void vcr_draw_pending(void)
{
    while (DSW(0x4DFF) != 0) vcr_draw();
}

/* AP 5b93:0436 vcr_play() -> 1 at the end of the film */
s16 vcr_play(void)
{
    quit_check();
    return vcr_play_frame((s16)(s8)DSB(0x092B), 0, 0, (s16)(s8)DSB(0x092E));
}

/* AP 5b93:045a vcr_apply_director_request() */
void vcr_apply_director_request(void)
{
    if (DSB(0x092C)) {
        vcr_set_director(1);
        DSB(0x092C) = 0;
    }
}

/* AP 5b93:0475 vcr_prime(): read the view records before the first control record and control 1 */
void vcr_prime(void)
{
    vcr_redraw_all();
    DSL(0x4E3B) = 0;
    DSB(0x092B) = 0;
    DSB(0x0930) = 0;
    DSW(0x08D6) = 0;
    DSB(0x0928) = 0;
    DSB(0x0929) = 0;
    DSB(0x092A) = 0;
    memset(DSP(0x4DE9), 0, 0x14);
    DSW(0x4DFD) = 1;
    vcr_play_frame(0, 0, 0, (s16)(s8)DSB(0x092E));
    DSW(0x4DFD) = 0;
    panel_end_label(0);
}

/* AP 5b93:04dd vcr_finish_recording() -> ok: END record + finalise */
s16 vcr_finish_recording(void)
{
    s16 a = vcr_put(4, 0);
    s16 b = rec_finish();
    return a & b;
}

/* AP 5b93:04fa vcr_set_name(n) -> DS:4E04 */
u16 vcr_set_name(u16 n_ds)
{
    rtl_strcpy(0x4E04, n_ds);
    return 0x4E04;
}

/* AP 5b93:0528 vcr_rewind(reopen) -> 4: re-initialise the world and re-simulate from frame 0 */
s16 vcr_rewind(s16 reopen)
{
    DSB(0x26C3) = 0;
    DSW(0x26D8) = DSW(0x26D2);
    DSB(0x26CD) = 0;
    DSB(0x26CE) = 0;
    gfx_rect(0, 0, 0x140, 0xC8);
    DSW(0x26D8) = DSW(0x26D4);
    gfx_rect(0, 0, 0x140, 0xC8);
    vcr_queue_reset();
    vcr_apply_director_request();
    mission_end();
    if (reopen) play_open(0x4E04);
    else play_rewind();
    mission_start();
    vcr_panel_show();
    if (DSB(0x092F)) rec_begin(tape_tmp_name());
    vcr_prime();
    return 4;
}

/* AP 5b93:05b2 vcr_mark_edited() */
void vcr_mark_edited(void)
{
    DSB(0x092B) = 1;
    DSB(0x0930) = 1;
}

/* AP 5b93:05c1 vcr_view_button(e): camera buttons of the panel (and view keys typed in the VCR) */
void vcr_view_button(u16 e)
{
    u16 m = stk_mark();
    u16 v = stk_alloc(8);
    u16 preset = 0;
    u16 key = DSW(e + 2);
    s16 r = 0;
    memcpy(DSP(v), DSP(0x0918), 8);
    u16 t = DSW(e);
    s16 ext = DSW(0x19F2) != 0;
    switch (t) {
    case 0x18: if (ext) key = 0x1C; else preset = 0x0908; break;
    case 0x19: if (ext) key = 0x1E; else if (view_get() == 0x18) preset = 0x08F8; break;
    case 0x1A: if (ext) key = 0x1F; else if (view_get() == 0x18) preset = 0x0900; break;
    case 0x1B: if (ext) key = 0x1D; else preset = 0x0910; break;
    case 0x1C: if (ext) key = 0x20; else preset = 0x08E8; break;
    case 0x1D: if (ext) key = 0x22; else preset = 0x08D8; break;
    case 0x1E: if (ext) key = 0x23; else preset = 0x08E0; break;
    case 0x1F: if (ext) key = 0x21; else preset = 0x08F0; break;
    case 0x21: key = 0x25; break;
    case 0x22: key = 0x26; break;
    case 0x23: key = 0x27; break;
    case 0x24: key = 0x28; break;
    case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
        DSW(0x08D6) = 0;
        break;
    default:
        break;
    }
    if (preset) {
        DSW(v + 2) = DSW(preset + 2);
        r = vcr_view(v);
        r |= vcr_view(preset);
        DSB(0x0929) = 1;
        DSW(0x08D6) = preset;
        vcr_mark_edited();
    } else if (key) {
        DSW(v) = key;
        r = vcr_view(v);
        vcr_mark_edited();
    }
    if (r & 1) DSB(0x0928) = 1;
    stk_release(m);
}

/* AP 5b93:074b vcr_track_views(&redraw, flag) -> draw once */
s16 vcr_track_views(u16 redraw_ds, u8 flag)
{
    s16 r = 0, ret = 0;
    DSB(0x092A) = flag;
    DSW(redraw_ds) = 0;
    if (DSB(0x0930)) {
        if (DSW(0x08D6) == 0) {
            if (DSB(0x092A) && view_get()) {
                memset(DSP(0x0918), 0, 8);
                r = vcr_view(0x0918);
            }
        } else {
            if (!DSB(0x0929) && DSW(0x19F2) == 0) r = vcr_view(DSW(0x08D6));
            ret = 1;
        }
        if (DSB(0x0928)) {
            DSW(redraw_ds) = 1;
            ret = 1;
            DSB(0x0928) = 0;
        }
        if ((r | 1) != 0) DSW(redraw_ds) = 1;         /* sic: always true */
        DSB(0x0929) = 0;
    }
    return ret;
}

/* AP 5b93:07ee vcr_track_views_draw() */
void vcr_track_views_draw(void)
{
    u16 m = stk_mark();
    u16 c = stk_alloc(2);
    s16 r = vcr_track_views(c, 0);
    if (DSW(c)) vcr_redraw_all();
    if (r) {
        ap_draw_frame(DSS(0x4DFF), 1);
        dec_floor(0x4DFF);
    }
    stk_release(m);
}

/* AP 5b93:082f vcr_track_views_quiet() */
void vcr_track_views_quiet(void)
{
    u16 m = stk_mark();
    u16 c = stk_alloc(2);
    vcr_track_views(c, 1);
    if (DSW(c)) vcr_redraw_all();
    stk_release(m);
}

/* AP 5b93:084e vcr_refresh_screen(full) */
void vcr_refresh_screen(s16 full)
{
    dlg_force_redraw();
    if (ap_draw_frame(DSS(AP_DS_redraw_pages), 0) == 0) {
        write_back();
        ap_clip_full();
        DSB(0x26CE) = 0;
        DSB(0x26CD) = 0;
        DSB(0x26CC) = 1;
        gfx_rect(0, 0, DSS(0x2DAA), DSS(0x2DAC));
        if (full) end_screen();
    }
    if (DSB(0x08D5)) {
        DSW(0x26D6) = DSW(0x26D2);
        DSW(0x26D8) = DSW(0x26D2);
        vcr_clip_full();
        dlg_force_redraw();
        dlg_key_filter(far_make(SEG(0x38E6), 0x15), 0x25);
    }
}

/* AP 5b93:08c8 vcr_fullscreen(playing) -> 8 */
s16 vcr_fullscreen(u8 playing)
{
    DSB(0x4DE8) = playing;
    return 8;
}

/* AP 5b93:08d6 vcr_view_is_edit(v) */
u8 vcr_view_is_edit(u16 v)
{
    s16 b0clear = (DSW(v + 2) & 1) == 0;
    s16 px = DSS(v + 4);
    s16 py = DSW(v + 6) != 0;
    u8 r = 0;
    if ((DSW(v + 2) & 2) == 0) {
        if (DSW(v) != 0) r = 1;
    } else if (view_get() < 6) {
        if (b0clear) r = px != 0 || py;
    } else {
        r = (u8)py;
        if (b0clear) r = px != 0 || py;
    }
    return r;
}

/* AP 5b93:096c vcr_mode_fullscreen() — panel hidden, Esc returns */
s16 vcr_mode_fullscreen(void)
{
    s16 m = 0, redraw = 1, drew = 0;
    u16 mk = stk_mark();
    u16 in = stk_alloc(0x28), vw = stk_alloc(8);
    vcr_redraw_all();
    vcr_panel_poll_off();
    vcr_panel_hide();
    s16 end = DSL(0x4E3B) == DSL(0x50A2);
    if (end) DSB(0x4DE8) = 0;
    while (m == 0) {
        quit_check();
        if (DSB(0x4DE8)) {
            vcr_pace();
            end = vcr_play();
            redraw = 1;
        }
        if (end) { DSB(0x4DE8) = 0; m = 5; }
        input_poll(in);
        host_pump();                                  /* PORT: this loop pumps the host */
        view_input(vw, in, 1);
        if (DSB(in + 0x1B) == 1) m = end ? 5 : (DSB(0x4DE8) ? 1 : 4);
        if (view_input_pending(vw) && (DSB(0x0930) || vcr_view_is_edit(vw))) {
            vcr_mark_edited();
            s16 r = vcr_view(vw);
            if (r & 5) {
                redraw = 1;
                if (r & 1) vcr_redraw_all();
            }
        }
        if (redraw || DSW(0x4DFF) != 0) { vcr_draw(); drew = 1; }
        else drew = 0;
        redraw = 0;
    }
    if (drew) {
        read_front();
        write_back();
        copy_screen();
    }
    vcr_panel_show();
    vcr_panel_poll_on();
    vcr_redraw_all();
    stk_release(mk);
    return m;
}

/* AP 5b93:0aaf vcr_eject() -> tape_save_changes() result (0 = cancelled) */
s16 vcr_eject(void)
{
    s16 r = (s16)(s8)tape_save_changes();
    if (r != 0 && r != 3) play_close();
    return r;
}

/* AP 5b93:0ace vcr_mode_play(continuous) — play (1) or single step (11) */
s16 vcr_mode_play(u8 continuous)
{
    s16 m = 0;
    panel_disable(0x09FE, 0x2000);
    panel_slider_enable(0);
    vcr_queue_reset();
    while (m == 0) {
        vcr_poll_panel();
        if (continuous) vcr_pace();
        s16 end = vcr_play();
        if (end) m = 5;
        else if (!continuous) m = 4;
        u16 e;
        while ((e = q_pop(QUEUE)) != 0 && m == 0) {
            u16 mk = stk_mark();
            u16 ev = stk_alloc(6);
            memcpy(DSP(ev), DSP(e), 6);
            switch (DSW(ev)) {
            case 0x11: m = 4; break;
            case 0x14: m = 2; break;
            case 0x20: m = 0x0A; break;
            case 0x16: if (vcr_eject()) { stk_release(mk); return 9; } break;
            case 0x17: if (tape_menu()) m = 4; break;
            default: vcr_view_button(ev); break;
            }
            if (!end && m == 0 && DSW(ev + 4) != 0 && DSW(0x08D6) == 0) m = vcr_fullscreen(1);
            stk_release(mk);
        }
        vcr_track_views_quiet();
        vcr_draw();
    }
    return m;
}

/* AP 5b93:0be9 vcr_mode() */
s16 vcr_mode(void)
{
    return DSS(0x4DE6);
}

/* AP 5b93:0bf1 vcr_ui(mode) -> 0 exit / 1 enter simulation */
s16 vcr_ui(s16 mode)
{
    s16 exit = 0, ret = 0;
    DSB(AP_DS_vcr_session_mode) = 2;
    DSW(0x4DE6) = (u16)mode;
    vcr_panel_show();
    vcr_panel_poll_on();
    while (!exit) {
        DSW(0x4DE6) = (u16)mode;
        switch (mode) {
        case 1: mode = vcr_mode_play(1); break;
        case 2: mode = vcr_mode_ff(); break;
        case 3: mode = vcr_mode_ff_blank(); break;
        case 4: mode = vcr_mode_paused(); break;
        case 5: mode = vcr_mode_end(); break;
        case 7: mode = vcr_mode_seek(); break;
        case 8: mode = vcr_mode_fullscreen(); break;
        case 9: exit = 1; ret = 0; break;
        case 10: vcr_view(0x0920); exit = 1; ret = 1; break;
        case 11: mode = vcr_mode_play(0); break;
        default: host_pump(); break;                  /* mode 6: never produced (endless loop) */
        }
    }
    vcr_panel_poll_off();
    return ret;
}

/* ---- 5c5f vcr2.c ---------------------------------------------------------------------------------- */

/* AP 5c5f:0000 vcr_read_simin() */
void vcr_read_simin(void)
{
    file_load(0x4E2E, 0x0D, 1, 0x0932 /* "sim.in" */);
    DSW(0x51EC) = (DSB(0x4E35) & 1) != 0;
}

/* AP 5c5f:002a vcr_write_simin(): the film's SIM.IN copy back when a review was requested */
void vcr_write_simin(void)
{
    if (!DSB(0x4E01)) return;
    u16 f = my_fopen(0x0932, 0x0939 /* "wb" */);
    if (f == 0) assert_fail(far_make(SEG(0x38D4), 0), 0x57, 0);
    my_fwrite(0x4E21, 0x0D, 1, f);
    my_fclose(f);
}

/* AP 5c5f:0083 vcr_world_init_hook() (from the world init) */
void vcr_world_init_hook(void)
{
    redraw_all();
    DSW(0x4E56) = 0;
    set_panel_top((s16)(DSS(0x2DAC) - 1));
    controls_reset();
    DSL(0x4E1C) = 30000;
    clock_reset();
    DSB(0x4E6F) = 0;
    end_prompt_reset();
}

/* AP 5c5f:00bd vcr_gfx_init(): VCR.BOX into 38E6:0015, the list boxes, the decode buffer */
void vcr_gfx_init(void)
{
    drv_set_record();
    box_load(far_make(SEG(0x38E6), 0x15), res_name(0x060A /* "VCR" */, 0x05C0 /* "BOX" */), 0x25);
    list_boxes_load();
    alloc_decode_buffer(0x10);
}

/* AP 5c5f:00f5 vcr_choose_tape() -> DS:4E40 / 0 */
u16 vcr_choose_tape(void)
{
    if ((u32)disk_free(0) < 30000) alert_modal(far_make(SEG(0x3967), 0), far_make(SEG(0x3957), 0));
    u16 name = tape_pick(0x96, 0x97);
    if (name == 0) return 0;
    rtl_strcpy(0x4E40, name);
    return 0x4E40;
}

/* AP 5c5f:0165 vcr_offer_save() -> name / 0: finalise and rename the temp film */
u16 vcr_offer_save(void)
{
    u16 name = 0;
    sx_stop_all();
    ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
    wipe_in(2);
    if (DSSL(0x0887) >= 3) {
        name = tape_ask_name();
        if (name != 0) {
            if (vcr_finish_recording() == 0) assert_fail(far_make(SEG(0x38D4), 0), 0xBD, 0);
            if (ap_file_exists(tape_path(name))) tape_unlink(tape_path(name));
            u16 to = tape_path(name);
            tape_rename(tape_path(tape_tmp_name()), to);
        }
    } else {
        tape_unlink(tape_path(tape_tmp_name()));
    }
    return name;
}

/* AP 5c5f:0238 vcr_exit_gfx() */
void vcr_exit_gfx(void)
{
    write_front();
}

/* AP 5c5f:0242 vcr_reopen(name) -> 4: the same mission, re-simulated from frame 0 */
s16 vcr_reopen(u16 name_ds)
{
    vcr_apply_director_request();
    panel_set_title(name_ds);
    mission_end();
    play_open(name_ds);
    vcr_set_name(name_ds);
    mission_start();
    vcr_panel_show();
    if (DSB(0x092F)) rec_begin(tape_tmp_name());
    vcr_prime();
    return 4;
}

/* AP 5c5f:0298 vcr_load(name) -> ok: load the film's mission, world init, prime */
s16 vcr_load(u16 name_ds)
{
    u16 m = stk_mark();
    u16 p = stk_alloc(4), n = stk_alloc(2);
    vcr_apply_director_request();
    panel_set_title(name_ds);
    s16 ok = tape_read_script(name_ds, p, n);
    if (ok == 0) assert_fail(far_make(SEG(0x38D4), 0), 0x101, name_ds);
    ap_mission_load(ds_far(p), DSW(n));
    ok &= play_open(name_ds);
    if (ok == 0) assert_fail(far_make(SEG(0x38D4), 0), 0x105, name_ds);
    mission_start();
    vcr_panel_show();
    if (DSB(0x092F)) {
        ok &= rec_begin(tape_tmp_name());
        if (ok == 0) assert_fail(far_make(SEG(0x38D4), 0), 0x10B, 0);
    }
    vcr_prime();
    vcr_set_name(name_ds);
    stk_release(m);
    return ok;
}

/* AP 5c5f:0378 vcr_unload(close) -> ok */
s16 vcr_unload(s16 close)
{
    s16 r = 1;
    if (close) { play_close(); r = 1; }
    mission_end();
    mission_unload();
    free_script2();
    return r;
}

/* AP 5c5f:03a0 vcr_switch(name, close) -> 1 */
s16 vcr_switch(u16 name_ds, s16 close)
{
    vcr_unload(close);
    vcr_load(name_ds);
    return 1;
}

/* ---- 5c9b vcr3.c: the session driver -------------------------------------------------------------- */

/* AP 5c9b:0000 vcr_session(sim_mode) -> 1 normal / 0 aborted or VCR exited */
s16 vcr_session(s16 sim_mode)
{
    u16 mk = stk_mark();
    u16 p = stk_alloc(4), n = stk_alloc(2), ids = stk_alloc(0x30), buf = stk_alloc(0x100);
    s16 done = 0, ret = 1, first = 1;
    DSW(0x4010) = 0;
    s16 state = (s16)(s8)DSB(0x093C + sim_mode);
    DSB(AP_DS_vcr_session_mode) = (u8)sim_mode;
    DSB(0x4E01) = 0;
    if (sim_mode == 0 || sim_mode == 1) vcr_read_simin();
    load_huff_tables();
    vcr_gfx_init();
    panel_init_lamp();
    if (alloc_bit_buffers() == 0) assert_fail(far_make(SEG(0x38D6), 0), 0x6F, 0);
    while (!done) {
        switch (state) {
        case 0: {                                     /* choose a tape (-w) */
            u16 name = vcr_choose_tape();
            if (name) {
                vcr_set_name(name);
                vcr_load(name);
                state = 2;
            } else {
                done = 1;
                ret = 0;
            }
            break;
        }
        case 1: {                                     /* fly and record */
            DSW(0x4010) = 1;
            if (first) {
                load_script_file(DSW(0x0534), p, n);
                ap_mission_load(ds_far(p), DSW(n));
                first = 0;
            }
            if ((u32)disk_free(0) < 30000) alert_modal(far_make(SEG(0x3966), 0), far_make(SEG(0x3957), 0));
            DSB(0x0886) = 1;
            if (rec_begin(tape_tmp_name()) == 0) assert_fail(far_make(SEG(0x38D6), 0), 0xB6, 0);
            mission_start();
            demo_fade_once();
            if (flight_loop() == 0) ret = 0;
            memcpy(DSP(0x506E), DSP(0x4E2E), 13);
            s16 saved = 0, leave = 0;
            ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
            wipe_in(2);
            results_text(ids, buf);
            while (!leave) {
                leave = 1;
                s16 c = alert_modal(ds_ptr(ids), far_make(SEG(0x3965), (u16)(saved * 2)));
                if (c == 0) c = 2;
                switch (c + saved) {
                case 1:
                    if (vcr_offer_save()) saved = 1;
                    leave = 0;
                    break;
                case 2:
                    vcr_queue_reset();
                    vcr_apply_director_request();
                    rec_abort();
                    mission_end();
                    break;
                case 3:
                    results_write();
                    mission_end();
                    mission_unload();
                    free_script();
                    done = 1;
                    break;
                default:
                    break;
                }
            }
            break;
        }
        case 2: {                                     /* the VCR */
            demo_fade_once();
            s16 r = vcr_ui(4);
            if (r == 0) {
                mission_end();
                mission_unload();
                ret = 0;
                done = 1;
            } else if (r == 1) {
                state = 3;
            }
            break;
        }
        case 3: {                                     /* enter simulation */
            DSW(0x4010) = 1;
            rtl_strcpy(0x4E58, 0x4E04);
            memcpy(DSP(0x4E21), DSP(0x50B0), 13);
            DSB(0x0886) = DSB(0x092F);
            DSL(0x0887) = DSL(0x4E3B);
            play_close();
            vcr_panel_hide();
            redraw_all();
            ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
            wipe_in(2);
            s16 r = flight_loop();
            memcpy(DSP(0x506E), DSP(0x4E21), 13);
            results_text(ids, buf);
            s16 k = 0;
            for (u16 q = (u16)(ids + 2); DSW(q) != 0; q = (u16)(q + 2)) k++;
            DSW(ids + 2 * k) = 0;
            vcr_refresh_screen(1);
            alert_modal(ds_ptr(ids), far_make(SEG(0x3957), 0));
            u16 name = vcr_offer_save();
            if (!name) {
                if (vcr_finish_recording() == 0) assert_fail(far_make(SEG(0x38D6), 0), 0x154, 0);
                vcr_refresh_screen(1);
            }
            s16 c = alert_modal(far_make(SEG(0x3963), 0), far_make(SEG(0x3964), 0));
            if (c == 1) {
                DSB(0x4E01) = 1;
                DSB(0x4E22) = 0;
                results_write();
                mission_end();
                mission_unload();
                free_script2();
                done = 1;
                if (r == 0) ret = 0;
            } else {
                if (!name) name = 0x4E58;
                vcr_reopen(name);
                state = 2;
                DSW(0x4010) = 0;
            }
            break;
        }
        case 4:                                       /* demo (-d) */
            demo_loop();
            done = 1;
            ret = 0;
            break;
        default:
            done = 1;                                 /* PORT: an unknown state ended the original's
                                                       * loop only through the jump table's range check */
            break;
        }
    }
    vcr_tape_exit();
    vcr_exit_gfx();
    vcr_write_simin();
    stk_release(mk);
    return ret;
}

/* ---- 5d56 vcr_demo.c ------------------------------------------------------------------------------ */

/* AP 5d56:0000 demo_fade_once(): the first-time palette set-up after the shell */
void demo_fade_once(void)
{
    if (DSB(0x0A0C)) return;
    DSB(0x0A0C) = 1;
    if (is_debug() == 0 && DSB(0x26DF) != 0) {
        wipe_black();
        read_front();
        write_back();
        copy_screen();
        if (DSL(0x408A) != 0) pal_set(ds_far(0x408A));
    }
}

/* AP 5d56:004b demo_play(name) -> 1 = played to the end without a key */
s16 demo_play(u16 name_ds)
{
    s16 key = 0, end = 0;
    heap_hook();
    mission_request_dissolve();
    DSB(AP_DS_vcr_session_mode) = 2;
    vcr_set_director(0);
    heap_hook();
    vcr_load(name_ds);
    vcr_panel_hide();
    demo_fade_once();
    while (!key && !end) {
        vcr_pace();
        end = vcr_play();
        vcr_draw();
        key = key_or_click();
    }
    sx_stop_all();
    vcr_unload(1);
    heap_hook();
    return key == 0;
}

/* AP 5d56:00c2 demo_title() -> 1 = no key: DEMO.SCR / DEMO.PAL for about 2 s */
s16 demo_title(void)
{
    s16 key = 0;
    DSW(0x26D8) = DSW(0x26D2);
    heap_hook();
    u16 m = stk_mark();
    u16 name = stk_alloc(0x20);
    rtl_strcpy(name, res_name(0x0A0D /* "DEMO" */, 0x0606 /* "SCR" */));
    if (scr_load_planes(name) == 0) assert_fail(far_make(SEG(0x38DC), 0), 0x86, name);
    u32 pl = pal_load(res_name(0x0A0D, 0x05CC /* "PAL" */));
    FarPtr pal = far_make((u16)(pl >> 16), (u16)pl);
    wipe_black();
    pal_set(pal);
    wipe_in(4);
    DSW(0x31E5) = 0x1D8;
    while (countdown_get() && key == 0) {
        key = key_or_click();
        host_pump();
    }
    pal_free(pal);
    heap_hook();
    stk_release(m);
    return key == 0;
}

/* AP 5d56:01b5 demo_loop(): the TAPES\DEMO*.VCR films in turn, until a key */
void demo_loop(void)
{
    list_files_all(0x0A12 /* "TAPES\DEMO*.VCR" */);
    s16 n = validate_tape_list();
    if (n == 0) return;
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50), rec = stk_alloc(0x20);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, files_out_name());
    u16 in = rtl_fopen(path, 0x0A2F /* "rb" */);
    if (in == 0) in = rtl_fopen(files_out_name(), 0x0A32);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, 0x0A22 /* "___demo_.out" */);
    u16 out = rtl_fopen(path, 0x0A35 /* "wb" */);
    if (out == 0) out = rtl_fopen(0x0A22, 0x0A38);
    if (in == 0 || out == 0) assert_fail(far_make(SEG(0x38DC), 0), 0xB5, 0);
    s16 k = 0;
    while (rtl_fread(rec, 0x20, 1, in) == 1)
        if (!is_marked(rec)) k = (s16)(k + rtl_fwrite(rec, 0x20, 1, out));
    rtl_fclose(out);
    rtl_fclose(in);
    if (k != n) assert_fail(far_make(SEG(0x38DC), 0), 0xBC, 0);
    s16 i = 0;
    const char *first = getenv("AOTP_VCR_DEMO");       /* developer aid: start with film n */
    if (first && *first) i = (s16)(atoi(first) % n);
    for (;;) {
        rtl_strcpy(path, 0x4032);
        rtl_strcat(path, 0x0A22);
        u16 f = rtl_fopen(path, 0x0A3B /* "rb" */);
        if (f == 0) f = rtl_fopen(0x0A22, 0x0A3E);
        if (f == 0) assert_fail(far_make(SEG(0x38DC), 0), 0xC8, 0x0A22);
        rtl_fseek(f, (s32)i << 5, 0);
        rtl_fread(rec, 0x20, 1, f);
        rtl_fclose(f);
        i = (s16)((i + 1) % n);
        rtl_strcpy(rec, make_filename(rec, 0x060A /* "VCR" */));
        if (!demo_title()) break;
        if (!demo_play(rec)) break;
    }
    rtl_unlink(0x0A22);
    stk_release(m);
}
