/* sim_flow: vcr_modes.c — the VCR panel event pump (1c0f), the event queue (51e7), panel button states
 * (5d46), fast forward / seek (5dd0), paused / end of tape (6163) — sim_flow_vcr.md §9. */
#include <stdio.h>
#include "sim_flow/sfb_priv.h"

#define QUEUE 0x088F                                  /* +0 size 6, +1 capacity 10, +2 read, +3 write */

/* ---- 1c0f: the panel event pump ------------------------------------------------------------------- */

/* AP 1c0f:000a vcr_panel_poll_on() */
void vcr_panel_poll_on(void)
{
    DSB(0x19EE) = 1;
}

/* AP 1c0f:0014 vcr_panel_poll_off() */
void vcr_panel_poll_off(void)
{
    DSB(0x19EE) = 0;
}

/* AP 1c0f:001e vcr_panel_force() */
void vcr_panel_force(void)
{
    DSB(0x19EF) = 1;
}

/* AP 1c0f:0028 vcr_panel_first() */
void vcr_panel_first(void)
{
    DSB(0x5E04) = 1;
}

/* AP 1c0f:0032 vcr_panel_reset_snap(): force the change detection */
void vcr_panel_reset_snap(void)
{
    memset(DSP(0x5E0D), 0xFF, 0x25);
    memset(DSP(0x5E05), 0xFF, 8);
}

/* AP 1c0f:0053 vcr_poll_panel(): input update; with the panel shown, every 4 ticks (or when forced)
 * update the display and turn a click / view key / Esc into a queued panel event */
void vcr_poll_panel(void)
{
    buttons_poll();
    if (!DSB(0x19EE)) return;
    if (!timeout_expired(0x016C) && !DSB(0x19EF)) return;
    timeout_start(0x016C, 4);
    if (panel_update()) DSB(0x19EF) = 1;
    u16 m = stk_mark();
    u16 in = stk_alloc(0x44), ms = stk_alloc(8), vw = stk_alloc(8), ev = stk_alloc(6), st = stk_alloc(0x1E);
    input_poll(in);
    pointer_input(ms, in);
    vcr_debug_mouse(ms);
    s16 inch = memcmp(DSP(in), DSP(0x5E0D), 0x25) != 0;
    s16 mch = memcmp(DSP(ms), DSP(0x5E05), 8) != 0;
    if (!inch && !mch && !DSB(0x19EF)) { stk_release(m); return; }
    memcpy(DSP(0x5E0D), DSP(in), 0x25);
    memcpy(DSP(0x5E05), DSP(ms), 8);
    view_input(vw, in, 0);
    s16 key = DSW(vw) != 0;
    s16 esc = DSB(in + 0x1B) == 1;
    DSW(ev + 4) = (u16)esc;
    if (!key && !mch && !esc && !DSB(0x19EF)) { stk_release(m); return; }
    DSB(0x19EF) = 0;
    drv_set_select();
    gfx_state_save(st);
    mask_off();
    vcr_clip_full();
    panel_click(ms);
    DSW(ev) = (u16)dlg_hit_dispatch(DSS(ms), DSS(ms + 2), DSS(ms + 4), (s16)(s8)DSB(0x5E04));
    vcr_sound_frame(1);
    if (DSW(ev) != 0 || key || esc) {
        DSW(ev + 2) = DSW(vw);
        q_push(QUEUE, ev);
        if (getenv("AOTP_VCR_EVDBG")) fprintf(stderr, "[vcr] event %02X key %02X esc %u\n", DSW(ev), DSW(ev + 2), DSW(ev + 4));
    }
    dlg_redraw_dirty();
    DSB(0x5E04) = 0;
    mask_on();
    gfx_state_restore(st);
    drv_set_unselect();
    stk_release(m);
}

/* ---- 51e7: the event queue (indices count down, with wrap) ---------------------------------------- */

/* AP 51e7:0000 q_push(q, src) -> 0 if full */
s16 q_push(u16 q, u16 src)
{
    if (q_full(q)) return 0;
    u16 d = q_slot(q, DSB(q + 3));
    memcpy(DSP(d), DSP(src), DSB(q));
    q_dec_wrap(q, 3);
    return 1;
}

/* AP 51e7:0052 q_peek(q) -> element / 0 */
u16 q_peek(u16 q)
{
    if (DSB(q + 2) == DSB(q + 3)) return 0;
    return q_slot(q, DSB(q + 2));
}

/* AP 51e7:0076 q_pop(q) -> element / 0 */
u16 q_pop(u16 q)
{
    u16 e = q_peek(q);
    if (e != 0) q_dec_wrap(q, 2);
    return e;
}

/* AP 51e7:009e q_full(q) (quirk: with read > write the full state is not detected) */
s16 q_full(u16 q)
{
    u16 r = DSB(q + 2), w = DSB(q + 3);
    if (r <= w) return (u16)(r + 1) == w;
    return DSB(q + 1) == (u16)(r - w);
}

/* AP 51e7:00e2 q_slot(q, i) (near) */
u16 q_slot(u16 q, u16 i)
{
    return (u16)(q + 4 + (u8)i * DSB(q));
}
/* AP 51e7:00e3 */
u16 q_slot_body(u16 q, u16 i) { return q_slot(q, i); }

/* AP 51e7:0109 q_dec_wrap(q, off) (near) */
void q_dec_wrap(u16 q, u16 off)
{
    if (DSB(q + off) == 0) DSB(q + off) = (u8)(DSB(q + 1) - 1);
    else DSB(q + off) -= 1;
}
/* AP 51e7:010a */
void q_dec_wrap_body(u16 q, u16 off) { q_dec_wrap(q, off); }

/* ---- 5d46: panel button states -------------------------------------------------------------------- */

#define PANEL_ITEM_W(id) SEGW(0x38E6, (u16)((s16)(s8)(id) * 0x16 + 0x25))    /* item +10h */

/* AP 5d46:0000 panel_stub(ids, bits, set) — the "inc bp" entry of panel_set_bits */
void panel_stub(u16 ids, u16 bits, s16 set)
{
    panel_set_bits(ids, bits, set);
}

/* AP 5d46:0001 panel_set_bits(ids, bits, set) (near): set / clear bits in item +10h of each listed id */
void panel_set_bits(u16 ids, u16 bits, s16 set)
{
    for (; DSB(ids) != 0; ids++) {
        if (set == 0) PANEL_ITEM_W(DSB(ids)) &= (u16)~bits;
        else PANEL_ITEM_W(DSB(ids)) |= bits;
    }
}

/* AP 5d46:004b panel_item_enabled(id) */
s16 panel_item_enabled(s16 id)
{
    return (PANEL_ITEM_W(id) & 0x7000) == 0;
}

/* AP 5d46:006c panel_item_dirty(id) */
void panel_item_dirty(s16 id)
{
    SEGB(0x38E6, (u16)(id * 0x16 + 0x21)) = 0x43;
}

/* AP 5d46:0086 panel_disable(ids, bits): the listed ids get the bits, all others of DS:09B2 lose them */
void panel_disable(u16 ids_ds, u16 bits)
{
    u8 was[20];                                       /* host copy of the 20 enable states */
    for (s16 i = 0; i < 0x14; i++) was[i] = (u8)panel_item_enabled((s16)(s8)DSB(0x09B2 + i));
    panel_stub(0x09B2, bits, 0);
    panel_stub(ids_ds, bits, 1);
    for (s16 i = 0; i < 0x14; i++)
        if ((s16)(s8)was[i] != panel_item_enabled((s16)(s8)DSB(0x09B2 + i))) panel_item_dirty((s16)(s8)DSB(0x09B2 + i));
    vcr_panel_force();
}

/* ---- 5dd0: fast forward / seek -------------------------------------------------------------------- */

/* AP 5dd0:0000 vcr_seek_setup() -> 1 when the slider is ahead of the tape */
s16 vcr_seek_setup(void)
{
    s32 t = (s32)slider_value(3);
    DSSL(0x4E82) = t;
    if (t <= DSSL(0x4E3B)) return 0;
    DSSL(0x4E7E) = t - DSSL(0x4E3B);
    DSSL(0x4E7A) = 0;
    DSSL(0x4E76) = DSSL(0x4E7E) / 0x28;
    rtl_ltoa(DSSL(0x4E82), 0x0AA0, 10);
    return 1;
}

/* AP 5dd0:005e seek_progress() -> 0..20 */
s16 seek_progress(void)
{
    return (s16)(((DSSL(0x4E7E) - DSSL(0x4E7A) + DSSL(0x4E76)) * 0x14) / DSSL(0x4E7E));
}

/* AP 5dd0:008a seek_draw_bar(n) */
void seek_draw_bar(s16 n)
{
    write_front();
    DSB(0x26C3) = 0;
    s16 x = 0xAA;
    for (s16 i = 0x13; i >= 0; i--) {
        item_center(far_make(0, i >= n ? 0xC5 : 0xCF));
        gfx_rect(x, 0x70, 4, 4);
        x = (s16)(x + 7);
    }
}

/* AP 5dd0:00d2 vcr_mode_seek() — simulate without drawing up to the slider position */
s16 vcr_mode_seek(void)
{
    s16 m = 0;
    if (DSL(0x4E82) == 0) return 4;
    panel_disable(0x09EB, 0x2000);
    panel_slider_enable(0);
    banner_w(0x0A8F);                                 /* "SEEKING LOCATION" */
    s16 p = seek_progress();
    seek_draw_bar(p);
    vcr_queue_reset();
    while (m == 0) {
        vcr_poll_panel();
        if (vcr_play()) m = 5;
        else {
            DSSL(0x4E7A) += 1;
            if (DSL(0x4E3B) == DSL(0x4E82)) m = 4;
            else {
                s16 q = seek_progress();
                if (q != p) { p = q; seek_draw_bar(q); }
            }
        }
        u16 e;
        while ((e = q_pop(QUEUE)) != 0 && m == 0) {
            if (DSW(e) == 0x11) m = 4;
            else if (DSW(e) == 0x12) m = 1;
        }
        vcr_track_views_quiet();
    }
    vcr_redraw_all();
    return m;
}

/* AP 5dd0:01bb vcr_mode_ff() — every 12th frame drawn */
s16 vcr_mode_ff(void)
{
    s32 k = 0, every = 0x0C;
    s16 m = 0;
    panel_disable(0x09DB, 0x2000);
    panel_slider_enable(0);
    vcr_queue_reset();
    while (m == 0) {
        vcr_poll_panel();
        s16 draw = 0;
        if (k-- == 0) { draw = 1; k = every; }
        if (vcr_play()) { vcr_redraw_all(); m = 5; }
        u16 e;
        while ((e = q_pop(QUEUE)) != 0 && m == 0) {
            switch (DSW(e)) {
            case 0x12: m = 1; break;
            case 0x20: m = 0x0A; break;
            case 0x11: draw = 1; m = 4; break;
            case 0x16: if (vcr_eject()) return 9; break;
            default: break;
            }
        }
        vcr_track_views_quiet();
        if (draw) vcr_draw();
    }
    return m;
}

/* AP 5dd0:02b3 vcr_mode_ff_blank() — "FAST FORWARDING TAPE", no drawing */
s16 vcr_mode_ff_blank(void)
{
    s16 m = 0;
    panel_disable(0x09D9, 0x2000);
    panel_slider_enable(0);
    vcr_queue_reset();
    banner_w(0x0A7A);                                 /* "FAST FORWARDING TAPE" */
    while (m == 0) {
        vcr_poll_panel();
        if (vcr_play()) { vcr_redraw_all(); m = 5; }
        u16 e;
        while ((e = q_pop(QUEUE)) != 0 && m == 0) {
            if (DSW(e) == 0x11) { vcr_redraw_all(); m = 4; }
            else if (DSW(e) == 0x12) { vcr_redraw_all(); m = 1; }
        }
        vcr_track_views_quiet();
    }
    return m;
}

/* ---- 6163: paused / end of tape ------------------------------------------------------------------- */

/* AP 6163:0000 vcr_rewind_prompt() -> 1 when rewound */
s16 vcr_rewind_prompt(void)
{
    if (DSL(0x4E3B) != 0) {
        s16 r = (s16)(s8)tape_save_changes();
        if (r != 0) {
            vcr_rewind(r == 3);
            return 1;
        }
    }
    return 0;
}

/* AP 6163:003c vcr_sound_idle() */
void vcr_sound_idle(void)
{
    sx_clear_continuous();
    vcr_sound_frame(1);
}

/* AP 6163:0050 vcr_mode_paused() */
s16 vcr_mode_paused(void)
{
    s16 m = 0;
    panel_disable(DSL(0x4E3B) != 0 ? 0x0A07 : 0x0A09, 0x2000);
    SEGB(0x38E6, 0x01F0) &= 0xBF;
    vcr_queue_reset();
    vcr_sound_idle();
    vcr_poll_panel();
    vcr_draw_pending();
    while (m == 0) {
        vcr_poll_panel();
        quit_check();
        host_pump();                                  /* PORT: the paused loop pumps the host */
        u16 e;
        while ((e = q_pop(QUEUE)) != 0 && m == 0) {
            u16 m_ = stk_mark();
            u16 ev = stk_alloc(6);
            memcpy(DSP(ev), DSP(e), 6);
            switch (DSW(ev)) {
            case 0x10: if (vcr_rewind_prompt()) m = 4; break;
            case 0x12: m = 1; break;
            case 0x13: m = 0x0B; break;
            case 0x14: m = 3; break;
            case 0x15: if (vcr_seek_setup()) m = 7; break;
            case 0x16: if (vcr_eject()) m = 9; break;
            case 0x17: if (tape_menu()) m = 4; break;
            case 0x20: m = 0x0A; break;
            default: vcr_view_button(ev); break;
            }
            if (m == 0 && DSW(ev + 4) != 0 && DSW(0x08D6) == 0) m = vcr_fullscreen(0);
            stk_release(m_);
        }
        vcr_track_views_draw();
    }
    return m;
}

/* AP 6163:0187 vcr_mode_end() */
s16 vcr_mode_end(void)
{
    s16 m = 0;
    vcr_queue_reset();
    panel_disable(0x0A02, 0x2000);
    SEGB(0x38E6, 0x01F0) &= 0xBF;
    s16 ended = player_ended();
    panel_end_label(ended);
    vcr_sound_idle();
    vcr_draw_pending();
    while (m == 0) {
        vcr_poll_panel();
        quit_check();
        host_pump();                                  /* PORT: the end-of-tape loop pumps the host */
        u16 e;
        while ((e = q_pop(QUEUE)) != 0 && m == 0) {
            u16 m_ = stk_mark();
            u16 ev = stk_alloc(6);
            memcpy(DSP(ev), DSP(e), 6);
            switch (DSW(ev)) {
            case 0x10: if (vcr_rewind_prompt()) m = 4; break;
            case 0x15: if (vcr_seek_setup()) m = 7; break;
            case 0x17: if (tape_menu()) m = 4; break;
            case 0x16: if (vcr_eject()) m = 9; break;
            case 0x20:
                if (ended) alert_modal(far_make(SEG(0x3958), 0), far_make(SEG(0x3957), 0));
                else m = 0x0A;
                break;
            default: vcr_view_button(ev); break;
            }
            stk_release(m_);
        }
        vcr_track_views_draw();
    }
    return m;
}
