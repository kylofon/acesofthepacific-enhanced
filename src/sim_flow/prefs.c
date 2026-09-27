/* sim_flow: prefs.c — the in-flight preferences dialog, device selection and calibration (6b4d
 * control.c, prefsim.BOX: 25 items at far 3974:0015) and the text layout of the map info boxes
 * (6c5f, drawtext) — sim_flow_ui.md §5, §6. Item k is at 3974:0015 + k*16h; the flight / rudder /
 * throttle device items are 4 / 6 / 8 (+10h the selection, +0Eh its label id). */
#include "sim_flow/sfb_priv.h"

#define PREF_SEG 0x3974
#define PIT_W(k, o) SEGW(PREF_SEG, (u16)((k) * 0x16 + 0x15 + (o)))
#define PIT_B(k, o) SEGB(PREF_SEG, (u16)((k) * 0x16 + 0x15 + (o)))
#define DEV_FLIGHT   SEGW(PREF_SEG, 0x7D)
#define DEV_RUDDER   SEGW(PREF_SEG, 0xA9)
#define DEV_THROTTLE SEGW(PREF_SEG, 0xD5)

/* ---- calibration and messages --------------------------------------------------------------------- */

/* AP 6b4d:0000 calib_center() -> 1 when a joystick button was pressed, 0 on Esc / quit */
s16 calib_center(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x26);
    s16 r;
    for (;;) {
        vcr_sound_frame(0);
        input_poll(in);
        u8 b = DSB(in + 0x24);
        if (b & 2) { clear_button_latches(0); r = 1; break; }
        if (b & 4) { clear_button_latches(1); r = 1; break; }
        if (b & 8) { clear_button_latches(2); r = 1; break; }
        if (b & 0x10) { clear_button_latches(3); r = 1; break; }
        if (DSB(in + 0x1B) == 1 || DSB(0x0B80) != 0) { r = 0; break; }
        host_pump();
    }
    stk_release(m);
    return r;
}

/* AP 6b4d:00a0 calib_corners() -> 1 after a key / button (click and 1/10 s), 0 on Esc / quit */
s16 calib_corners(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x2A), t = stk_alloc(4);
    for (;;) {
        vcr_sound_frame(0);
        input_poll(in);
        if ((DSB(in + 0x23) & 0xE0) || (DSB(in + 0x24) & 1)) break;
        if (DSB(in + 0x1B) == 1 || DSB(0x0B80) != 0) { stk_release(m); return 0; }
        host_pump();
    }
    sx_play(0x0F, 0, 0, 0);
    rtl_gettime(t);
    u16 start = DSB(t + 2);
    for (;;) {
        vcr_sound_frame(0);
        rtl_gettime(t);
        u16 h = DSB(t + 2);
        if (h < start) h = (u8)(h + 0x64);
        if (h > start + 0x0A) break;
        host_pump();
    }
    stk_release(m);
    return 1;
}

/* AP 6b4d:0158 calib_msg() -> both steps done */
s16 calib_msg(void)
{
    return calib_center() && calib_corners();
}

/* ---- dialog callbacks ----------------------------------------------------------------------------- */

/* AP 6b4d:0174 prefs_cb_labels() — callback: inset box, centred value text */
void prefs_cb_labels(FarPtr unused)
{
    (void)unused;
    inset_box();
    DSB(0x26C0) = DSB(0x0646);
    DSB(0x26C2) = 1;
    gfx_font_select(DSW(0x0648) != 0 ? DSS(0x3F32) : DSS(0x3F2E));
    FarPtr it = CUR_ITEM();
    s16 tx = ap_text_center_x(IT_W(it, 0x0E), RX, RW);
    gfx_text(IT_W(it, 0x0E), tx, (s16)(RY + 4));
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
}

/* AP 6b4d:01e2 prefs_cb_devices() — callback: device name under STR0 0Fh */
void prefs_cb_devices(FarPtr unused)
{
    (void)unused;
    inset_box();
    DSB(0x26C0) = DSB(0x0646);
    DSB(0x26C2) = 1;
    gfx_font_select(DSW(0x0648) != 0 ? DSS(0x3F32) : DSS(0x3F2E));
    FarPtr it = CUR_ITEM();
    gfx_text(IT_W(it, 0x0E), (s16)(RX + 4), (s16)(RY + 0x0A));
    gfx_text(str0(0x0F).off, (s16)(RX + 4), (s16)(RY + 3));
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
}

/* AP 6b4d:0257 prefs_cb_button() — callback: the large frame with rivet columns */
void prefs_cb_button(FarPtr unused)
{
    (void)unused;
    dlg_frame(far_make(0, 0));
    item_frame_bitmap((s16)(RX + 3), (s16)(RY + 0x18), 0x12);
    item_frame_bitmap((s16)(RX + RW - 6), (s16)(RY + 0x18), 0x12);
}

/* AP 6b4d:0295 prefs_slider0() — callback: TIME SCALE */
void prefs_slider0(FarPtr unused) { (void)unused; slider_draw(0); }
/* AP 6b4d:02a4 prefs_slider1() — callback: AIRCRAFT */
void prefs_slider1(FarPtr unused) { (void)unused; slider_draw(2); }
/* AP 6b4d:02b3 prefs_slider2() — callback: WORLD */
void prefs_slider2(FarPtr unused) { (void)unused; slider_draw(1); }

/* AP 6b4d:02c2 prefs_cb_a() — callback: ACCEPT -> 9 */
void prefs_cb_a(FarPtr unused)
{
    (void)unused;
    if (toggle_press(far_make(0, 0))) DSW(0x3BC1) = 9;
}

/* AP 6b4d:02d6 prefs_cb_b() — callback: engraved label at the item origin */
void prefs_cb_b(FarPtr unused)
{
    (void)unused;
    gfx_font_select(DSS(0x3F2E));
    text_engrave(IT_W(CUR_ITEM(), 0x0E), 0xC1, 0xB1, 0xC0, (u16)RX, (u16)RY);
}

/* AP 6b4d:0308 prefs_init(): prefsim.BOX, the toggle and device labels */
void prefs_init(void)
{
    box_load(far_make(SEG(PREF_SEG), 0x15), res_name(0x1168 /* "prefsim" */, 0x05C0 /* "BOX" */), 0x19);
    for (s16 i = 2; --i >= 0;) {                     /* twice */
        toggle(0x10, 0x11, 9);
        toggle(0x0E, 0x0F, 0x0B);
        toggle(0x0C, 0x0D, 0x0D);
        toggle(0x16, 0x17, 0x10);
    }
    device_label(4, 0x18, DEV_FLIGHT);
    device_label(6, 0x1E, DEV_RUDDER);
    device_label(8, 0x22, DEV_THROTTLE);
}

/* AP 6b4d:03b0 toggle(a, b, base) -> new value (near): flip item a's +10h, item b's label id = base + v */
s16 toggle(s16 a, s16 b, s16 base)
{
    s16 v = PIT_W(a, 0x10) == 0;
    PIT_W(a, 0x10) = (u16)v;
    PIT_W(b, 0x0E) = (u16)(base + v);
    PIT_B(b, 0x0C) = 0x43;
    return v;
}
/* AP 6b4d:03b1 */
s16 toggle_body(s16 a, s16 b, s16 base) { return toggle(a, b, base); }

/* AP 6b4d:0408 toggle_joystick() -> joystick flight flag (0 without a joystick) */
s16 toggle_joystick(void)
{
    if (DSB(0x1B4A) == 0) return 0;
    toggle(0x0C, 0x0D, 0x0D);
    DSW(0x3BC1) = 1;
    return joy_flight_toggle();
}

/* AP 6b4d:0431 toggle_joystick_cb() — callback */
void toggle_joystick_cb(FarPtr unused)
{
    (void)unused;
    if (toggle_press(far_make(0, 0))) toggle_joystick();
}

/* AP 6b4d:0443 toggle_mouse() -> mouse flag (0 without a mouse) */
s16 toggle_mouse(void)
{
    if (!mouse_active()) return 0;
    toggle(0x0E, 0x0F, 0x0B);
    DSW(0x3BC1) = 2;
    return mouse_flag_toggle();
}

/* AP 6b4d:046e toggle_mouse_cb() — callback */
void toggle_mouse_cb(FarPtr unused)
{
    (void)unused;
    if (toggle_press(far_make(0, 0))) toggle_mouse();
}

/* AP 6b4d:0480 toggle_sound() -> sounds on */
s16 toggle_sound(void)
{
    if (toggle(0x10, 0x11, 9)) { sx_on(); return 1; }
    sx_off();
    return 0;
}

/* AP 6b4d:04a8 toggle_sound_cb() — callback */
void toggle_sound_cb(FarPtr unused)
{
    (void)unused;
    if (toggle_press(far_make(0, 0))) {
        toggle_sound();
        DSW(0x3BC1) = 4;
    }
}

/* AP 6b4d:04c0 toggle_ground() — callback: GROUND DETAIL OFF / ON IN COMBAT -> 11h */
void toggle_ground(void)
{
    if (toggle_press(far_make(0, 0))) {
        toggle(0x16, 0x17, 0x10);
        DSW(0x3BC1) = 0x11;
    }
}

/* AP 6b4d:04e2 prefs_cb_c() — callback: device button -> 12h */
void prefs_cb_c(FarPtr unused) { (void)unused; if (button_press(0)) DSW(0x3BC1) = 0x12; }
/* AP 6b4d:04fb prefs_cb_d() — callback -> 13h */
void prefs_cb_d(FarPtr unused) { (void)unused; if (button_press(0)) DSW(0x3BC1) = 0x13; }
/* AP 6b4d:0514 prefs_cb_e() — callback -> 14h */
void prefs_cb_e(FarPtr unused) { (void)unused; if (button_press(0)) DSW(0x3BC1) = 0x14; }
/* AP 6b4d:052d prefs_cb_f() — callback -> 15h */
void prefs_cb_f(FarPtr unused) { (void)unused; if (button_press(0)) DSW(0x3BC1) = 0x15; }

/* AP 6b4d:0546 calib_slider(a, b): "<a> <26h> <b>" incompatibility box until a button */
void calib_slider(s16 a, s16 b)
{
    u16 m = stk_mark();
    u16 l = stk_alloc(0x10);
    FarPtr s;
    s = str(0, a); DSW(l) = s.off; DSW(l + 2) = s.seg;
    s = str(0, 0x26); DSW(l + 4) = s.off; DSW(l + 6) = s.seg;
    s = str(0, b); DSW(l + 8) = s.off; DSW(l + 0x0A) = s.seg;
    DSL(l + 0x0C) = 0;
    ap_dlg_redraw_all_606e();
    u32 h = quit_box(l);
    if (h != 0) {
        calib_msg();
        quit_box_close(h);
        quit_check();
    }
    stk_release(m);
}

/* AP 6b4d:05c1 device_check(item, value) -> 1 when the combination is allowed */
s16 device_check(s16 item, s16 value)
{
    switch (item) {
    case 4:
        if (value != 3) return 1;
        if (DEV_RUDDER == 1) { calib_slider(0x27, 0x28); return 0; }
        if (DEV_THROTTLE == 1) { calib_slider(0x27, 0x29); return 0; }
        if (DEV_THROTTLE == 2) { calib_slider(0x27, 0x2A); return 0; }
        return 1;
    case 6:
        if (value == 1) {
            if (DEV_FLIGHT == 3) { calib_slider(0x28, 0x27); return 0; }
            if (DEV_THROTTLE == 2) { calib_slider(0x28, 0x2A); return 0; }
            return 1;
        }
        if (value == 2) {
            if (DEV_THROTTLE == 1) { calib_slider(0x2B, 0x29); return 0; }
            return 1;
        }
        return 1;
    case 8:
        if (value == 1) {
            if (DEV_FLIGHT == 3) { calib_slider(0x29, 0x27); return 0; }
            if (DEV_RUDDER == 2) { calib_slider(0x29, 0x2B); return 0; }
            return 1;
        }
        if (value == 2) {
            if (DEV_FLIGHT == 3) { calib_slider(0x2A, 0x27); return 0; }
            if (DEV_RUDDER == 1) { calib_slider(0x2A, 0x28); return 0; }
            return 1;
        }
        return 1;
    default:
        return 1;
    }
}

/* AP 6b4d:0751 controls_apply(): the input set-up from the device selections */
void controls_apply(void)
{
    device_enable_set(0, (DEV_FLIGHT == 1 || DEV_FLIGHT == 2 || DEV_FLIGHT == 3) ? 1 : 0);
    device_enable_set(1, (DEV_RUDDER == 1 || DEV_RUDDER == 2) ? 1 : 0);
    device_enable_set(2, (DEV_THROTTLE == 1 || DEV_THROTTLE == 2) ? 1 : 0);
    hat_views_set(DEV_FLIGHT == 3 ? 1 : 0);
    abs_throttle_set(DEV_THROTTLE == 2 ? 1 : 0);
    mouse_flight_set(DEV_FLIGHT == 4 ? 1 : 0);
}

/* AP 6b4d:085f device_label(item, base, value) (near) */
void device_label(s16 item, s16 base, s16 value)
{
    PIT_W(item, 0x10) = (u16)value;
    PIT_W(item, 0x0E) = (u16)(base + value);
    PIT_B(item, 0x0C) = 0x43;
}
/* AP 6b4d:0860 */
void device_label_body(s16 item, s16 base, s16 value) { device_label(item, base, value); }

static void redraw_view(void)
{
    quit_block();
    ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
    wipe_in(2);
    quit_unblock();
}

/* AP 6b4d:089b device_select(label, n, item) -> the chosen device / -1 */
s16 device_select(s16 label, s16 n, s16 item)
{
    u16 m = stk_mark();
    u16 texts = stk_alloc(8), buttons = stk_alloc(0x2C), codes = stk_alloc(0x14);
    FarPtr s = str(0, label);
    DSW(texts) = s.off; DSW(texts + 2) = s.seg;
    DSL(texts + 4) = 0;
    s16 i = 0;
    for (; i < n; i++) {
        s = str(0, (s16)(label + 1 + i));
        DSW(buttons + 4 * i) = s.off;
        DSW(buttons + 4 * i + 2) = s.seg;
        DSW(codes + 2 * i) = (u16)(i + 0x65);
    }
    s = str(0, 0x16);
    DSW(buttons + 4 * i) = s.off;
    DSW(buttons + 4 * i + 2) = s.seg;
    DSW(codes + 2 * i) = 0x64;
    i++;
    DSL(buttons + 4 * i) = 0;
    redraw_view();
    s16 k = alert_build(texts, buttons, codes);
    dlg_activate(far_make(SEG(0x38BF), 0x16), k);
    DSB(0x3BB8) = 1;
    dlg_force_redraw();
    for (;;) {
        s16 r = dlg_poll();
        if (r == 0x64 || r == 9) {
            redraw_view();
            stk_release(m);
            return -1;
        }
        if (r < 0x65 || r >= n + 0x65) continue;
        if (device_check(item, (s16)(r - 0x65))) {
            device_label(item, (s16)(label + 1), (s16)(r - 0x65));
            controls_apply();
            redraw_view();
            stk_release(m);
            return (s16)(r - 0x65);
        }
        DSB(0x3BB8) = 1;
        dlg_force_redraw();
    }
}

/* AP 6b4d:0a41 ap_calibrate(first, n, mode, axes): n corner prompts (STR0 first, first+1, ...),
 * then the sorted readings through calib_set */
void ap_calibrate(s16 first, s16 n, s16 mode, u16 axes)
{
    u16 m = stk_mark();
    u16 list = stk_alloc(0x0C), xs = stk_alloc(6), ys = stk_alloc(6);
    s16 ok = 1;
    DSL(list + 8) = 0;
    ap_dlg_redraw_all_606e();
    s16 si = 0, id = first;
    while (si < n && ok) {
        FarPtr s = str(0, id);
        DSW(list) = s.off; DSW(list + 2) = s.seg;
        s = str(0, (s16)(id + 1));
        DSW(list + 4) = s.off; DSW(list + 6) = s.seg;
        u32 h = quit_box(list);
        if (h == 0) { stk_release(m); return; }
        ok = calib_center();
        if (ok) {
            if (mode == 0) {
                if (si == 0) joy_detect();
                joy_read(0, (u16)(xs + 2 * si), (u16)(ys + 2 * si));
            } else {
                if (axes & 1) DSW(xs + 2 * si) = (u16)joy_axis_raw(2);
                if (axes & 2) DSW(ys + 2 * si) = (u16)joy_axis_raw(3);
            }
            ok = calib_corners();
        }
        quit_box_close(h);
        quit_check();
        id = (s16)(id + 2);
        si++;
    }
    if (mode == 0 && si > 1) {
        FarPtr s = str(0, first);
        DSW(list) = s.off; DSW(list + 2) = s.seg;
        s = str(0, (s16)(first + 1));
        DSW(list + 4) = s.off; DSW(list + 6) = s.seg;
        u32 h = quit_box(list);
        if (h == 0) { stk_release(m); return; }
        calib_msg();
        quit_box_close(h);
    }
    if (!ok) { stk_release(m); return; }
    s16 *x = (s16 *)DSP(xs), *y = (s16 *)DSP(ys), t;
    if (n == 2) {
        if (axes & 1) { x[2] = x[1]; x[1] = (s16)((x[0] + x[1]) / 2); }
        if (axes & 2) { y[2] = y[1]; y[1] = (s16)((y[0] + y[1]) / 2); }
    }
    if (x[1] < x[0]) { t = x[0]; x[0] = x[1]; x[1] = t; }
    if (x[2] < x[1]) { t = x[1]; x[1] = x[2]; x[2] = t; }
    if (x[1] < x[0]) { t = x[0]; x[0] = x[1]; x[1] = t; }
    if (y[1] < y[0]) { t = y[0]; y[0] = y[1]; y[1] = t; }
    if (y[2] < y[1]) { t = y[1]; y[1] = y[2]; y[2] = t; }
    if (y[1] < y[0]) { t = y[0]; y[0] = y[1]; y[1] = t; }
    if (axes & 1) calib_set(mode, 1, xs);
    if (axes & 2) calib_set(mode, 2, ys);
    stk_release(m);
}

/* AP 6b4d:0c74 devices_dialog(): the calibration choices for the selected devices */
void devices_dialog(void)
{
    u16 m = stk_mark();
    u16 title = stk_alloc(8), buttons = stk_alloc(0x20), codes = stk_alloc(0x10);
    s16 k = 0;
    vcr_sound_frame(0);
    FarPtr s = str(0, 0x2C);
    DSW(title) = s.off; DSW(title + 2) = s.seg;
    DSL(title + 4) = 0;
#define ADD(id, code) do { FarPtr s_ = str(0, (id)); DSW(buttons + 4 * k) = s_.off; DSW(buttons + 4 * k + 2) = s_.seg; DSW(codes + 2 * k) = (code); k++; } while (0)
    u16 fl = SEGW(PREF_SEG, 0x7B);
    if (fl == 0x19) ADD(0x19, 0x65);
    else if (fl == 0x1B) { ADD(0x1B, 0x65); ADD(0x43, 0x6A); }
    else if (fl == 0x1A) ADD(0x1A, 0x66);
    if (SEGW(PREF_SEG, 0xA7) == 0x20) ADD(0x20, 0x67);
    if (SEGW(PREF_SEG, 0xD3) == 0x24) ADD(0x24, 0x68);
    if (SEGW(PREF_SEG, 0xA7) == 0x1F || SEGW(PREF_SEG, 0xD3) == 0x23) ADD(0x1F, 0x69);
    if (k == 0) { stk_release(m); return; }
    if (k > 1) {
        s16 n = k;
        ADD(0x16, 0x64);
        DSL(buttons + 4 * k) = 0;
        ap_dlg_redraw_all_606e();
        s16 items = alert_build(title, buttons, codes);
        dlg_activate(far_make(SEG(0x38BF), 0x16), items);
        DSB(0x3BB8) = 1;
        dlg_force_redraw();
        k = n;
    }
#undef ADD
    for (;;) {
        s16 r = k == 1 ? DSS(codes) : dlg_poll();
        switch (r) {
        case 0x65: ap_calibrate(0x2D, 3, 0, 3); stk_release(m); return;
        case 0x66: ap_calibrate(0x33, 3, 0, 3); stk_release(m); return;
        case 0x67: ap_calibrate(0x39, 3, 1, 1); stk_release(m); return;
        case 0x68: ap_calibrate(0x3F, 2, 1, 2); stk_release(m); return;
        case 0x69: ap_calibrate(0x2D, 3, 1, 3); stk_release(m); return;
        case 0x6A: ap_calibrate(0x44, 2, 2, 2); stk_release(m); return;
        case 0x09: case 0x64: ap_dlg_redraw_all_606e(); stk_release(m); return;
        default: break;
        }
    }
}

/* AP 6b4d:0f95 prefs_dialog() -> the TIME SCALE changed (F10) */
s16 prefs_dialog(void)
{
    s16 old = slider_value(0);
    joy_detect();
    s16 sel = 3;
    for (;;) {
        if (sel != -1) {
            dlg_activate(far_make(SEG(PREF_SEG), 0x15), 0x19);
            dlg_select(sel);
            DSB(0x3BB8) = 1;
            dlg_force_redraw();
            sel = -1;
        }
        switch (dlg_poll()) {
        case 0x05: time_scale_apply(); break;
        case 0x06:
        case 0x11:
            dlg_force_redraw();
            world_detail_apply(slider_value(1));
            ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
            break;
        case 0x08:
            dlg_force_redraw();
            aircraft_detail_apply(slider_value(2));
            ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
            break;
        case 0x09:
            ap_draw_frame(DSS(AP_DS_redraw_pages), 0);
            wipe_in(2);
            return slider_value(0) != old;
        case 0x12: device_select(0x17, 5, 4); sel = 3; break;
        case 0x13: device_select(0x1D, 3, 6); sel = 5; break;
        case 0x14: device_select(0x21, 4, 8); sel = 7; break;
        case 0x15: devices_dialog(); sel = 9; break;
        default: break;
        }
    }
}

/* ---- 6c5f: text layout (drawtext) ---------------------------------------------------------------- */

/* AP 6c5f:0000 tl_init(engrave, line_height, draw) */
void tl_init(u16 engrave, u16 lh, u16 draw)
{
    DSW(0x5217) = engrave;
    DSW(0x5215) = lh;
    DSB(0x5214) = (u8)draw;
}

/* AP 6c5f:0017 tl_reset() -> line height — no callers */
u16 tl_reset(void)
{
    return DSW(0x5215);
}

static void tl_put(u16 s, s16 x, s16 y)
{
    if (DSB(0x5214) == 0) return;
    if (DSW(0x5217)) text_engrave(s, 0xC7, 0xB3, 0xC0, (u16)x, (u16)y);
    else gfx_text(s, x, y);
}

/* AP 6c5f:001f tl_measure(s, &x, &y): a word, leading ',' / '.' drawn separately; wraps at DS:5210 */
void tl_measure(u16 s, u16 px, u16 py)
{
    u16 p = (u16)(s + 1);
    while (DSB(s) == ',' || DSB(s) == '.') {
        u8 c = DSB(p);
        DSB(p) = 0;
        tl_put(s, DSS(px), DSS(py));
        DSW(px) += (u16)gfx_text_width(s);
        DSB(p) = c;
        p++;
        s++;
    }
    s16 w = gfx_text_width(s);
    if (DSS(px) + w > DSS(0x5210)) {
        DSW(py) += DSW(0x5215);
        DSW(px) = DSW(0x5212);
    }
    if (DSB(s) == ' ' && DSW(px) == DSW(0x5212)) while (DSB(s) == ' ') s++;
    gfx_text_width(s);
    tl_put(s, DSS(px), DSS(py));
    DSW(px) += (u16)gfx_text_width(s);
}

/* AP 6c5f:0124 tl_wrap(s, &x, &y): word by word; '$' breaks the line */
void tl_wrap(u16 s, u16 px, u16 py)
{
    while (DSB(s) != 0) {
        u8 *sp = memchr(DSP(s), ' ', (size_t)(rtl_strlen(s) + 1));
        if (sp) {
            u16 q = (u16)(sp - DSP(0) + 1);
            u8 c = DSB(q);
            DSB(q) = 0;
            tl_measure(s, px, py);
            DSB(q) = c;
            s = q;
            continue;
        }
        u8 *dp = memchr(DSP(s), '$', (size_t)(rtl_strlen(s) + 1));
        if (dp) {
            u16 q = (u16)(dp - DSP(0));
            DSB(q) = 0;
            tl_measure(s, px, py);
            s = (u16)(q + 1);
            DSW(py) += DSW(0x5215);
            DSW(px) = DSW(0x5212);
            continue;
        }
        if (DSB(s) != 0) {
            tl_measure(s, px, py);
            s = (u16)(s + rtl_strlen(s));
        }
    }
}

/* AP 6c5f:0202 tl_line_width(far dst, far src) -> far pointer to the copied NUL */
FarPtr tl_line_width(FarPtr dst, FarPtr src)
{
    for (;;) {
        u8 c = rd8(src.seg, src.off);
        src.off++;
        wr8(dst.seg, dst.off, c);
        if (c == 0) return dst;
        dst.off++;
    }
}

/* AP 6c5f:0225 tl_align(far dst, far lines, n, far cb) -> dst: n NUL-separated lines joined with ' ';
 * "%c" expands through cb (sic: the first character of each line is skipped) — no callers */
FarPtr tl_align(FarPtr dst, FarPtr lines, s16 n, FarPtr cb)
{
    FarPtr d = dst, l = lines;
    s16 k = 0;
    do {
        FarPtr s = l;
        if (k) { wr8(d.seg, d.off, ' '); d.off++; }
        for (;;) {
            u8 c = rd8(s.seg, s.off);
            s.off++;
            if (c == 0) break;
            if (rd8(s.seg, s.off) == '%' && !far_is_null(cb)) {
                s.off++;
                u8 ch = rd8(s.seg, s.off);
                FarPtr r = ((FarPtr (*)(u16))codeptr_lookup(cb))(ch);
                d = tl_line_width(d, r);
            } else {
                wr8(d.seg, d.off, rd8(s.seg, s.off));
                d.off++;
            }
        }
        k++;
        l.off = (u16)(l.off + rtl__fstrlen(l) + 1);
    } while ((s16)(s8)n > k);
    return dst;
}

/* AP 6c5f:02d1 tl_format(far fmt, nlines, far cb) -> height used (AL): the text after the first '|'
 * laid out in the item rectangle */
u16 tl_format(u32 fmt_, u16 nlines, u32 cb_)
{
    FarPtr s = far_make((u16)(fmt_ >> 16), (u16)fmt_);
    FarPtr cb = far_make((u16)(cb_ >> 16), (u16)cb_);
    u16 m = stk_mark();
    u16 px = stk_alloc(2), py = stk_alloc(2);
    s16 top = RY;
    DSW(py) = (u16)RY;
    DSW(px) = (u16)RX;
    DSW(0x5212) = (u16)RX;
    DSW(0x5210) = (u16)(RX + RW - 1);
    for (;;) { u8 c = rd8(s.seg, s.off); s.off++; if (c == '|') break; }
    for (s16 di = 0; (s16)(s8)nlines > di; di++) {
        u16 si = 0x51FC;
        if (di) { DSB(0x51FC) = ' '; si++; }
        for (;;) {
            u8 c = rd8(s.seg, s.off);
            if (c == 0 || c == '|') break;
            if (c == '%') {
                if (si != 0x51FC) { DSB(si) = 0; tl_wrap(0x51FC, px, py); si = 0x51FC; }
                if (!far_is_null(cb)) {
                    s.off++;
                    if (rd8(s.seg, s.off) == 0) continue;
                    u8 ch = rd8(s.seg, s.off);
                    s.off++;
                    FarPtr r = ((FarPtr (*)(u16))codeptr_lookup(cb))(ch);
                    if (!far_is_null(r))
                        for (; rd8(r.seg, r.off) != 0; r.off++) DSB(si++) = rd8(r.seg, r.off);
                } else {
                    DSB(si++) = c;
                    s.off++;
                }
            } else if (c == '$' || (c == ' ' && si != 0x51FC)) {
                DSB(si++) = c;
                s.off++;
                DSB(si) = 0;
                tl_wrap(0x51FC, px, py);
                si = 0x51FC;
            } else {
                DSB(si++) = c;
                s.off++;
            }
        }
        DSB(si) = 0;
        tl_wrap(0x51FC, px, py);
        s.off = (u16)(s.off + rtl__fstrlen(s) + 1);
    }
    if (DSS(px) > DSS(0x5212)) DSW(py) += DSW(0x5215);
    u16 r = (u8)(DSB(py) - (u8)top);
    stk_release(m);
    return r;
}

/* AP 6c5f:0443 tl_draw(items, base, far cb, list, x, y, xmax) -> lines: texts with "%c" buttons laid
 * out as dialog items — no callers */
s16 tl_draw(u16 items, s16 base, FarPtr cb, u16 list, s16 x, s16 y, s16 xmax)
{
    u16 m = stk_mark();
    u16 px = stk_alloc(2), py = stk_alloc(2), line = stk_alloc(0x130);
    s16 y0 = y, k = 0;
    DSW(px) = (u16)x;
    DSW(py) = (u16)y;
    gfx_font_select(DSS(0x3F2E));
    DSB(0x26C2) = 1;
    DSW(0x5212) = (u16)x;
    DSW(0x5210) = (u16)xmax;
    tl_init(0xFFFD, 0x0B, 1);
    for (u16 lp = list; DSW(lp) != 0; lp = (u16)(lp + 2), k++) {
        rtl_strcpy(line, DSW(lp));
        u16 di = line, si = 0x51FC;
        if (k) { DSB(0x51FC) = ' '; si++; }
        while (DSB(di) != 0) {
            u8 c = DSB(di);
            if (c == '%') {
                if (si != 0x51FC) { DSB(si) = 0; tl_wrap(0x51FC, px, py); si = 0x51FC; }
                di++;
                u8 ch = DSB(di);
                u16 it = (u16)(items + base * 0x16 + (s16)(s8)ch * 0x16 - 0x856);
                u16 r = ((u16 (*)(u16, u16))codeptr_lookup(cb))(ch, it);
                di++;
                if (r) {
                    for (; DSB(r) != 0; r++) DSB(si++) = DSB(r);
                } else {
                    s16 w = (s16)(gfx_text_width(DSW(it + 0x0E)) + 5);
                    if (DSS(px) + w > DSS(0x5210)) { DSW(py) += DSW(0x5215); DSW(px) = DSW(0x5212); }
                    DSW(it) = (u16)(DSS(px) - 1 - DSS(items));
                    DSW(it + 2) = (u16)(DSS(py) - 3 - DSS(items + 2));
                    DSW(it + 4) = (u16)(w - 1);
                    DSW(it + 6) = (u16)(DSS(0x5215) - 2);
                    DSW(it + 0x10) = 1;
                    DSB(it + 0x0C) = 0x43;
                    DSW(px) += (u16)w;
                }
            } else if (c == '$' || (c == ' ' && si != 0x51FC)) {
                DSB(si++) = c;
                di++;
                DSB(si) = 0;
                tl_wrap(0x51FC, px, py);
                si = 0x51FC;
            } else {
                DSB(si++) = c;
                di++;
            }
        }
        DSB(si) = 0;
        tl_wrap(0x51FC, px, py);
    }
    if (DSS(px) > DSS(0x5212)) DSW(py) += DSW(0x5215);
    s16 r = (s16)((s16)(DSS(py) - y0) / DSS(0x5215));
    stk_release(m);
    return r;
}

__attribute__((constructor)) static void prefs_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x6B4D0174u, (CodeFn)prefs_cb_labels);
    codeptr_register(PROG_AP, 0x6B4D01E2u, (CodeFn)prefs_cb_devices);
    codeptr_register(PROG_AP, 0x6B4D0257u, (CodeFn)prefs_cb_button);
    codeptr_register(PROG_AP, 0x6B4D0295u, (CodeFn)prefs_slider0);
    codeptr_register(PROG_AP, 0x6B4D02A4u, (CodeFn)prefs_slider1);
    codeptr_register(PROG_AP, 0x6B4D02B3u, (CodeFn)prefs_slider2);
    codeptr_register(PROG_AP, 0x6B4D02C2u, (CodeFn)prefs_cb_a);
    codeptr_register(PROG_AP, 0x6B4D02D6u, (CodeFn)prefs_cb_b);
    codeptr_register(PROG_AP, 0x6B4D0431u, (CodeFn)toggle_joystick_cb);
    codeptr_register(PROG_AP, 0x6B4D046Eu, (CodeFn)toggle_mouse_cb);
    codeptr_register(PROG_AP, 0x6B4D04A8u, (CodeFn)toggle_sound_cb);
    codeptr_register(PROG_AP, 0x6B4D04C0u, (CodeFn)toggle_ground);
    codeptr_register(PROG_AP, 0x6B4D04E2u, (CodeFn)prefs_cb_c);
    codeptr_register(PROG_AP, 0x6B4D04FBu, (CodeFn)prefs_cb_d);
    codeptr_register(PROG_AP, 0x6B4D0514u, (CodeFn)prefs_cb_e);
    codeptr_register(PROG_AP, 0x6B4D052Du, (CodeFn)prefs_cb_f);
}
