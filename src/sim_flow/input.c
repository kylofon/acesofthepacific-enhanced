/* sim_flow: control mapping (5f2c) and input polling (60a9) — sim_flow.md §5. */
#include "sim_flow/sfa_priv.h"
#include "enhanced/enhanced.h"   /* ENH: flaps up / down keys */

#define BIT(off, b) ((DSB(off) >> (b)) & 1)

/* ---- 5f2c: control mapping --------------------------------------------------------------------------- */

/* AP 5f2c:0000 — the pointer box */
void cursor_box_init(void)
{
    DSW(0x0B16) = 0;
    DSW(0x0B18) = 0x136;
    DSW(0x0B1A) = 0;
    DSW(0x0B1C) = 0xBE;
}

/* AP 5f2c:001d (near) — the current dialog list scrolls horizontally (item flags +0Dh bit 2) */
static s16 dialog_list_flag(void)
{
    FarPtr p = ds_far(0x3BD3);
    if (far_is_null(p)) return 0;
    return far_mp(p)[0x0D] & 4;
}

/* AP 5f2c:0036 / 0043 — map mode */
void map_mode_on(void)
{
    DSB(0x4EEB) = 1;
    DSB(0x4EEA) = 1;
}

void map_mode_off(void)
{
    DSB(0x4EEB) = 0;
}

/* AP 5f2c:004d / 0058 / 006c — joystick flight control flag DS:0B20 */
void joy_flight_set(s16 v) { DSW(0x0B20) = (u16)v; }

s16 joy_flight_toggle(void)
{
    if (DSB(AP_DS_joy_present)) DSW(0x0B20) ^= 1;
    return (s16)DSW(0x0B20);
}

s16 joy_flight_get(void) { return (s16)DSW(0x0B20); }

/* AP 5f2c:0074 / 007f / 00ae — mouse flag DS:0B1E */
void mouse_flag_set(s16 v) { DSW(0x0B1E) = (u16)v; }

s16 mouse_flag_toggle(void)
{
    if (mouse_active()) DSW(0x0B1E) ^= 1;
    if (DSW(0x0B1E)) mouse_warp((s16)DSW(0x0B22), (s16)DSW(0x0B24), 1);
    return (s16)DSW(0x0B1E);
}

s16 mouse_flag_get(void) { return (s16)DSW(0x0B1E); }

/* AP 5f2c:00b6 */
void pointer_store(s16 x, s16 y)
{
    DSW(0x0B22) = (u16)x;
    DSW(0x0B24) = (u16)y;
    if (DSW(0x0B1E)) mouse_warp(x, y, 0);
}

/* AP 5f2c:00dc pointer_get(in, &x, &y, &buttons) */
void pointer_get(u16 in, u16 x_ds, u16 y_ds, u16 b_ds)
{
    DSW(b_ds) = (BIT(in + 0x23, 5) || BIT(in + 0x23, 6)) ? 1 : 0;
    if (DSW(0x0B1E)) {
        DSW(x_ds) = DSW(in + 0x10);
        DSW(y_ds) = DSW(in + 0x12);
    } else {
        DSW(x_ds) = DSW(0x0B22);
        DSW(y_ds) = DSW(0x0B24);
    }
}

/* AP 5f2c:0137 — device enables DS:0B26 + 2i (0 stick, 1 rudder, 2 throttle, 3 mouse flight) */
void device_enable_set(s16 i, s16 v) { DSW(0x0B26 + 2 * i) = (u16)v; }

/* AP 5f2c:0148 */
void mouse_flight_set(s16 v) { DSW(0x0B2C) = (u16)v; }

/* AP 5f2c:0153 / 015e — hat / throttle-notch views */
void hat_views_set(s16 v) { DSW(0x0B30) = (u16)v; }
s16 hat_views_get(void) { return (s16)DSW(0x0B30); }

/* AP 5f2c:0166 / 0171 — absolute throttle */
void abs_throttle_set(s16 v) { DSW(0x0B2E) = (u16)v; }
s16 abs_throttle_get(void) { return (s16)DSW(0x0B2E); }

/* AP 5f2c:0179 — clamp to the pointer box; 1 = changed */
s16 pointer_clamp(u16 x_ds, u16 y_ds)
{
    s16 x0 = DSS(x_ds), y0 = DSS(y_ds), x = x0, y = y0;
    if (x < DSS(0x0B16)) x = DSS(0x0B16);
    else if (x > DSS(0x0B18)) x = DSS(0x0B18);
    if (y < DSS(0x0B1A)) y = DSS(0x0B1A);
    else if (y > DSS(0x0B1C)) y = DSS(0x0B1C);
    if (x0 == x && y0 == y) return 0;
    DSS(x_ds) = x;
    DSS(y_ds) = y;
    return 1;
}

/* AP 5f2c:01d8 pointer_input(ptr, in) -> ptr: {x, y, buttons, cmd} for dialogs, the map and the VCR
 * panel (sim_flow.md §5.3) */
u16 pointer_input(u16 ptr, u16 in)
{
    u16 m = stk_mark();
    u16 px = stk_alloc(2), py = stk_alloc(2), pb = stk_alloc(2);      /* bp-4, -6, -8 */
    u16 cx_ = stk_alloc(2), cy_ = stk_alloc(2), cb_ = stk_alloc(2);   /* bp-0Ch, -0Eh, -18h */
    u8 moved = 0;
    memset(DSP(ptr), 0, 8);
    if (DSB(in + 0x1B) == 0x0F) {                                  /* Tab */
        DSW(ptr + 6) = (BIT(in + 0x21, 6) || BIT(in + 0x21, 5)) ? 3 : 4;
    } else if (DSB(0x4EEB)) {                                      /* map mode: stick/keypad cursor */
        s16 dx = DSS(in + 8), dy = DSS(in + 0x0C);
        s16 accel;
        if (dx == 0 && dy == 0 && DSW(0x0B20)) {
            dx = DSS(in);
            dy = DSS(in + 4);
            accel = 0;
        } else {
            accel = 1;
        }
        if (dx == 0 && dy == 0) {
            DSB(0x4EEA) = 1;
        } else {
            if (DSB(0x4EEA)) {
                DSB(0x4EEA) = 0;
                DSL(0x4EDE) = 0;
                pointer_get(in, cx_, cy_, cb_);
                DSSL(0x4EE6) = (s32)DSS(cx_) << 8;
                DSSL(0x4EE2) = (s32)DSS(cy_) << 8;
                if (dx > 0) DSSL(0x4EE6) += 0xFF;
                if (dy > 0) DSSL(0x4EE2) += 0xFF;
                timeout_expire(0x4ED8);
            }
            if (timeout_expired(0x4ED8)) {
                moved = 1;
                DSL(0x4EDE) += 1;
                timeout_start(0x4ED8, 1);
                s16 speed;
                if (accel) {
                    s16 n = DSSL(0x4EDE) >= 0xC8 ? 0xC8 : (s16)DSL(0x4EDE);
                    speed = lerp_map(n, 1, 0xC8, 0, 0x38);
                } else {
                    speed = 0x28;
                }
                DSSL(0x4EE6) += ((s32)dx * speed) >> 3;
                DSS(cx_) = (s16)(DSSL(0x4EE6) >> 8);
                DSSL(0x4EE2) += ((s32)dy * speed) >> 3;
                DSS(cy_) = (s16)(DSSL(0x4EE2) >> 8);
                if (pointer_clamp(cx_, cy_)) {
                    DSSL(0x4EE6) = (s32)DSS(cx_) << 8;
                    DSSL(0x4EE2) = (s32)DSS(cy_) << 8;
                }
                pointer_store(DSS(cx_), DSS(cy_));
            }
        }
    } else {
        s16 idx = -1;
        for (int i = 0; i < 4; i++)
            if (DSB(0x0B32 + i) == DSB(in + 0x1B)) { idx = (s16)i; break; }
        if (idx >= 0) {                                            /* arrow keys: cmd 1..4 */
            DSW(ptr + 6) = (u16)(idx + 1);
            if (DSS(ptr + 6) <= 2 && dialog_list_flag()) DSW(ptr + 6) = 0;
        } else if (DSW(0x0B20) && !BIT(in + 0x21, 1) && timeout_expired(0x014E)) {
            timeout_start(0x014E, 0x17);
            if (DSS(in + 4) >= 0x40) DSW(ptr + 6) = 4;
            if (DSS(in + 4) <= -0x40) DSW(ptr + 6) = 3;
            if (!dialog_list_flag()) {
                if (DSS(in) >= 0x40) DSW(ptr + 6) = 2;
                if (DSS(in) <= -0x40) DSW(ptr + 6) = 1;
            }
        }
    }
    pointer_get(in, px, py, pb);
    if (moved) {
        DSW(px) = DSW(cx_);
        DSW(py) = DSW(cy_);
    }
    if (dialog_list_flag()) {
        s16 hit = 0;
        s16 dx = BIT(in + 0x21, 1) ? DSS(in + 8) : (DSW(0x0B20) ? DSS(in) : 0);
        FarPtr a = ds_far(0x00D6), b = ds_far(0x3BD3);
        s16 lo = (s16)(rd16(a.seg, a.off) + rd16(b.seg, b.off));
        s16 hi = (s16)(lo + rd16(b.seg, (u16)(b.off + 4)) - 1);
        if (dx <= -0x40) {
            hit = 1;
            DSS(px) = (s16)(DSS(px) - 1);
            if (DSS(px) <= lo) DSS(px) = (s16)(lo + 1);
        }
        if (dx >= 0x40) {
            hit = 1;
            DSS(px) = (s16)(DSS(px) + 1);
            if (DSS(px) >= hi) DSS(px) = (s16)(hi - 1);
        }
        if (DSW(ptr + 6) != 4 && DSW(ptr + 6) != 3 && hit) DSW(pb) |= 1;
    }
    pointer_clamp(px, py);
    pointer_store(DSS(px), DSS(py));
    DSW(ptr) = DSW(px);
    DSW(ptr + 2) = DSW(py);
    DSW(ptr + 4) = DSW(pb);
    stk_release(m);
    return ptr;
}

/* AP 5f2c:055b / 056b — key binding callbacks (value, ctl) */
void bind_throttle_abs(s16 v, u16 ctl) { DSB(ctl + 8) = (u8)v; }
void bind_throttle_rel(s16 v, u16 ctl) { DSB(ctl + 9) = (u8)v; }

typedef void (*BindFn)(s16 v, u16 ctl);

/* AP 5f2c:057b binding_lookup(flags, scancode, far table, ctl) -> 1 found */
static s16 binding_lookup(u16 flags, u16 scan, FarPtr t, u16 ctl)
{
    for (;; t.off = (u16)(t.off + 10)) {
        u8 *e = far_mp(t);
        u16 esc = (u16)(e[2] | e[3] << 8);
        if (esc == 0) return 0;
        if (esc != scan || (u16)(e[0] | e[1] << 8) != flags) continue;
        FarPtr fn = far_rd(t.seg, (u16)(t.off + 6));
        u16 v = (u16)(e[4] | e[5] << 8);
        if (!far_is_null(fn)) ((BindFn)codeptr_lookup(fn))((s16)v, ctl);
        else DSW(ctl + 0x10) = v;
        return 1;
    }
}

/* AP 5f2c:0ba3 (body 0ba4) axis_shape(v, k): dead zone +-8 with a slight gain, >> (8 - k) */
s16 axis_shape(s16 v, s16 k)
{
    s16 x;
    if (v > 8) x = (s16)(v + (v >> 4) - 8);
    else if (v < -8) x = (s16)(v + (v >> 4) + 8);
    else x = 0;
    return (s16)(x >> (u8)(8 - k));
}

/* AP 5f2c:0bda (body 0bdb) axis_shape16: dead zone +-16 */
static s16 axis_shape16(s16 v, s16 k)
{
    s16 x;
    if (v > 0x10) x = (s16)(v + (v >> 4) - 0x10);
    else if (v < -0x10) x = (s16)(v + (v >> 4) + 0x10);
    else x = 0;
    return (s16)(x >> (u8)(8 - k));
}

/* AP 5f2c:05d1 controls_map(ctl, in) — sim_flow.md §5.3 */
u16 controls_map(u16 ctl, u16 in)
{
    s16 used = 0;
    memset(DSP(ctl), 0, 0x14);
    DSW(ctl + 0x10) = 0;
    DSB(ctl + 8) = 0xFF;
    DSB(ctl + 9) = 0;
    if (BIT(in + 0x23, 5)) DSW(ctl + 0x0E) = 1;
    if (BIT(in + 0x23, 6)) DSW(ctl + 0x0E) |= 2;
    if (BIT(in + 0x21, 2)) {                                       /* '.' */
        s16 v = (s16)(DSS(0x4EEC) + dt_scale(0x28));
        DSS(0x4EEC) = v > 0x7F ? 0x7F : (s16)(DSS(0x4EEC) + dt_scale(0x28));
    } else if (BIT(in + 0x21, 3)) {                                /* ',' */
        s16 v = (s16)(DSS(0x4EEC) - dt_scale(0x28));
        DSS(0x4EEC) = v < -0x7F ? (s16)0xFF81 : (s16)(DSS(0x4EEC) - dt_scale(0x28));
    } else {
        DSW(0x4EEC) = 0;
    }
    if (BIT(in + 0x24, 3)) DSW(ctl + 0x10) = 0x0B;
    if (BIT(in + 0x24, 4)) DSW(ctl + 0x10) = 0x0A;
    if (BIT(in + 0x21, 0) && DSW(0x0B2C)) {                        /* mouse flight */
        s16 x = ap_clamp_sym_277a((s16)((DSS(in + 0x10) - 0xA0) << 3), 0x7F);
        s16 y = ap_clamp_sym_277a((s16)((DSS(in + 0x12) - 0x64) << 3), 0x7F);
        if (x || y) {
            mouse_warp(0xA0, 0x64, 1);
            DSW(ctl) = (u16)axis_shape(x, 6);
            DSW(ctl + 2) = (u16)axis_shape(y, 6);
            used = 1;
        }
    }
    if (!used && !BIT(in + 0x21, 1) && DSB(in + 0x14) && DSW(0x0B26)) {
        DSW(ctl) = (u16)axis_shape(DSS(in), 6);
        DSW(ctl + 2) = (u16)axis_shape(DSS(in + 4), 6);
    }
    if (DSB(in + 0x15) && DSW(0x0B28)) DSS(0x4EEC) = axis_shape16(DSS(in + 2), 8);
    if (DSB(in + 0x16) && DSW(0x0B2A)) {
        if (abs_throttle_get()) DSB(ctl + 8) = (u8)lerp_map(DSS(in + 6), 0x7F, (s16)0xFF81, 0, 0x40);
        else DSB(ctl + 9) = (u8)-(u8)axis_shape(DSS(in + 6), 4);
    }
    if (!used && BIT(in + 0x21, 1)) {                              /* keypad stick */
        DSW(ctl) = (u16)axis_shape(DSS(in + 8), 6);
        DSW(ctl + 2) = (u16)axis_shape(DSS(in + 0x0C), 6);
    }
    u16 flags = (u16)((BIT(in + 0x21, 7) ? 4 : 0) | (BIT(in + 0x22, 0) ? 8 : 0));
    if (!binding_lookup(flags, DSB(in + 0x1B), far_make(SEG(0x3923), 0), ctl) &&
        DSB(in + 0x1B) >= 0x3B && DSB(in + 0x1B) <= 0x42)
        DSW(ctl + 0x10) = (u16)(DSB(in + 0x1B) - 0x23);
    /* ENH: flaps up / flaps down keys (only sent with the enhancements on, enh_keys.c) */
    if (DSB(in + 0x1B) == ENH_KEY_FLAPS_UP) DSW(ctl + 0x10) = ENH_CMD_FLAPS_UP;
    else if (DSB(in + 0x1B) == ENH_KEY_FLAPS_DOWN) DSW(ctl + 0x10) = ENH_CMD_FLAPS_DOWN;
    if (view_get() > 5 && (DSW(ctl + 0x0E) & 3) == 3 && (DSW(ctl) || DSW(ctl + 2))) {
        DSW(ctl + 2) = 1;
        DSW(ctl) = 1;
    }
    if (DSB(ctl + 8) == 0xFF) {
        DSW(0x4EEE) = 0xFFFF;
    } else if ((s16)DSC(ctl + 8) == DSS(0x4EEE)) {
        DSB(ctl + 8) = 0xFF;
    } else {
        DSS(0x4EEE) = DSC(ctl + 8);
        DSB(ctl + 9) = 0;
    }
    DSW(ctl + 4) = (u16)axis_shape(DSS(0x4EEC), 5);
    return ctl;
}

/* AP 5f2c:08fd view_input(vw, in, allow_mouse) — sim_flow.md §5.4 */
u16 view_input(u16 vw, u16 in, s16 allow_mouse)
{
    u16 k = DSB(in + 0x1B);
    memset(DSP(vw), 0, 8);
    if (view_get() > 5 && !BIT(in + 0x21, 7) && !BIT(in + 0x22, 0) && DSB(in + 0x1A)) {
        DSW(vw + 2) = 2;
        if (DSW(0x4ED6)) {
            DSW(vw + 2) |= 1;
            DSW(vw + 6) = (u16)axis_shape(DSC(in + 0x1A), 4);
        }
        DSW(0x4ED6) = 1;
        return vw;
    }
    DSW(0x4ED6) = 0;
    if (!DSB(AP_DS_radio_menu_open) && k >= 0x3B && k <= 0x44) {
        s16 n = (s16)(k - 0x3B);
        u8 c = BIT(in + 0x21, 7), a = BIT(in + 0x22, 0);
        if (c ^ a) DSW(vw) = (u16)(c ? n + 1 : n + 0x0B);
        else if (k <= 0x41) DSW(vw) = (u16)(n + 0x15);
        else if (k == 0x42) DSW(vw) = 0x2A;
    } else if (hat_views_get()) {
        if (view_get() > 5) {
            switch ((s16)DSC(in + 0x1D) - 1) {
            case 0: DSW(vw + 2) |= 2; DSW(vw + 6) = (u16)axis_shape(0x7F, 4); break;
            case 1: DSW(vw + 2) |= 2; DSW(vw + 4) = (u16)axis_shape(0x7F, 4); break;
            case 2: DSW(vw + 2) |= 2; DSW(vw + 6) = (u16)axis_shape((s16)0xFF81, 4); break;
            case 3: DSW(vw + 2) |= 2; DSW(vw + 4) = (u16)axis_shape((s16)0xFF81, 4); break;
            default: break;
            }
        } else if (DSB(in + 0x1E)) {
            DSW(vw) = DSW(0x0B34 + 2 * (s16)DSC(in + 0x1E));
        }
    }
    if (BIT(in + 0x23, 6) && DSW(vw + 4) == 0 && DSW(vw + 6) == 0) {   /* pan button */
        s16 x, y;
        if (BIT(in + 0x21, 1)) { x = DSS(in + 8); y = DSS(in + 0x0C); }
        else if (DSW(0x0B26)) { x = DSS(in); y = DSS(in + 4); }
        else { x = 0; y = 0; }
        DSW(vw + 2) |= 2;
        DSW(vw + 4) = (u16)axis_shape(x, 4);
        DSW(vw + 6) = (u16)axis_shape(y, 4);
        if (BIT(in + 0x23, 5)) DSW(vw + 2) |= 1;
    }
    if (allow_mouse && DSW(0x0B2C) && DSW(vw + 4) == 0 && DSW(vw + 6) == 0 && BIT(in + 0x21, 0)) {
        s16 x = ap_clamp_sym_277a((s16)((DSS(in + 0x10) - 0xA0) << 3), 0x7F);
        s16 y = ap_clamp_sym_277a((s16)((DSS(in + 0x12) - 0x64) << 3), 0x7F);
        if (x || y) {
            mouse_warp(0xA0, 0x64, 1);
            DSW(vw + 4) = (u16)axis_shape(x, 4);
            DSW(vw + 6) = (u16)axis_shape(y, 4);
        }
    }
    return vw;
}

/* AP 5f2c:0c11 */
void controls_reset(void)
{
    timeout_start(0x014E, 0);
    DSW(0x4EEE) = 0xFFFF;
    DSW(0x4EEC) = 0;
}

/* ---- 60a9: input polling ------------------------------------------------------------------------------ */

/* AP 60a9:0000 — buttons, held keys, fire buttons and their press edges */
void buttons_poll(void)
{
    if (DSB(0x4F5B) || DSB(0x4F5C) || DSB(0x4F5D)) {
        DSB(0x4F87) = (u8)joy_button(0);
        DSB(0x4F86) = (u8)joy_button(1);
        DSB(0x4F85) = (u8)joy_button(2);
        DSB(0x4F84) = (u8)joy_button(3);
    } else {
        DSB(0x4F84) = DSB(0x4F85) = DSB(0x4F86) = DSB(0x4F87) = 0;
    }
    DSB(0x4F83) = (u8)ap_mouse_button(0);
    DSB(0x4F82) = (u8)ap_mouse_button(1);
    DSB(0x4F8B) = (u8)key_held(0x39);
    DSB(0x4F8A) = (u8)key_held(0x1C);
    DSB(0x4F89) = (u8)key_held(0x0E);
    DSB(0x4F88) = (u8)key_held(0x22);
    DSB(0x4F7F) = DSB(0x4F87) | DSB(0x4F8B) | DSB(0x4F83);
    DSB(0x4F7E) = DSB(0x4F86) | DSB(0x4F8A) | DSB(0x4F82);
    DSB(0x4F7D) = DSB(0x4F85) | DSB(0x4F89) | DSB(0x4F81);
    DSB(0x4F7C) = DSB(0x4F84) | DSB(0x4F88) | DSB(0x4F80);
    for (int i = 0; i < 4; i++) {
        if (DSB(0x4F7F - i)) {
            if (!((DSB(0x4FA7) >> i) & 1)) {
                DSB(0x4FA7) |= (u8)(1 << i);
                DSB(0x4F7B - i) = 1;
            }
        } else {
            DSB(0x4FA7) &= (u8)~(1 << i);
        }
    }
}

/* AP 60a9:017c — clear the press edge of fire button n (0..3) */
void clear_button_latches(u16 n)
{
    if (n <= 3) DSB(0x4F7B - n) = 0;
}

/* AP 60a9:01b2 */
static void buttons_to_record(u16 in)
{
    static const u16 b22[7] = { 0x4F87, 0x4F86, 0x4F85, 0x4F84, 0x4F8B, 0x4F8A, 0x4F89 };
    static const u16 b23[8] = { 0x4F88, 0x4F83, 0x4F82, 0x4F81, 0x4F80, 0x4F7F, 0x4F7E, 0x4F7D };
    for (int i = 0; i < 7; i++)
        DSB(in + 0x22) = (u8)((DSB(in + 0x22) & ~(1 << (i + 1))) | ((DSB(b22[i]) & 1) << (i + 1)));
    for (int i = 0; i < 8; i++)
        DSB(in + 0x23) = (u8)((DSB(in + 0x23) & ~(1 << i)) | ((DSB(b23[i]) & 1) << i));
    DSB(in + 0x24) = (u8)((DSB(in + 0x24) & 0xFE) | (DSB(0x4F7C) & 1));
}

/* AP 60a9:097d axis_scale(v, min, ctr, max) */
static s16 axis_scale(s16 v, s16 mn, s16 ctr, s16 mx)
{
    if (v <= mn) return (s16)0xFF81;
    if (v < ctr) return lerp_map(v, mn, ctr, (s16)0xFF81, 0);
    if (v == ctr) return 0;
    if (v < mx) return lerp_map(v, ctr, mx, 0, 0x7F);
    return 0x7F;
}

/* the throttle notch 0..5 from the raw throttle axis (60a9:03ff) */
static s16 throttle_notch(s16 raw)
{
    s16 a = (s16)((s16)(DSS(0x4F5E) - raw) * 3);
    a = (s16)(a + DSS(0x4F76) / 2);
    s16 range = DSS(0x4F76) < 0x14 ? 0x14 : DSS(0x4F76);
    return (s16)(5 - idiv16_retry(a, range, NULL));
}

/* AP 60a9:02c4 input_poll(in) — sim_flow.md §5.2 */
u16 input_poll(u16 in)
{
    u16 m = stk_mark();
    u16 jx = stk_alloc(2), jy = stk_alloc(2);
    memset(DSP(in), 0, 0x25);
    buttons_poll();
    buttons_to_record(in);
    if (DSB(0x4F5B) || DSB(0x4F5C) || DSB(0x4F5D)) {
        if (timeout_expired(AP_DS_input_timeout)) {
            timeout_start(AP_DS_input_timeout, 0x0B);
            if (DSB(0x4F5B)) {
                joy_read(0, jx, jy);
                DSW(in) = (u16)axis_scale(DSS(jx), DSS(0x4F60), DSS(0x4F62), DSS(0x4F64));
                DSW(in + 4) = (u16)axis_scale(DSS(jy), DSS(0x4F66), DSS(0x4F68), DSS(0x4F6A));
                DSB(in + 0x14) = 1;
                DSB(0x4FA4) = 1;
                DSW(0x4F94) = DSW(in);
                DSW(0x4F98) = DSW(in + 4);
            }
            if (DSB(0x4F5C) && DSB(0x4F5D)) {
                joy_read_raw(1, jx, jy);
                DSW(in + 2) = (u16)axis_scale(DSS(jx), DSS(0x4F6C), DSS(0x4F6E), DSS(0x4F70));
                DSW(in + 6) = (u16)axis_scale(DSS(jy), DSS(0x4F72), DSS(0x4F74), DSS(0x4F76));
                DSB(in + 0x15) = 1;
                DSB(in + 0x16) = 1;
                DSB(0x4FA5) = 1;
                DSB(0x4FA6) = 1;
                DSW(0x4F96) = DSW(in + 2);
                DSW(0x4F9A) = DSW(in + 6);
                s16 n = vcr_is_playing() ? 0 : throttle_notch(DSS(jy));
                DSB(in + 0x1D) = (u8)n;
                DSW(0x4FA0) = (u16)n;
            } else if (DSB(0x4F5C)) {
                s16 x = joy_axis_raw(2);
                DSW(in + 2) = (u16)axis_scale(x, DSS(0x4F6C), DSS(0x4F6E), DSS(0x4F70));
                DSB(in + 0x15) = 1;
                DSB(0x4FA5) = 1;
                DSW(0x4F96) = DSW(in + 2);
            } else if (DSB(0x4F5D)) {
                s16 y = joy_axis_raw(3);
                DSW(in + 6) = (u16)axis_scale(y, DSS(0x4F72), DSS(0x4F74), DSS(0x4F76));
                DSB(in + 0x16) = 1;
                DSB(0x4FA6) = 1;
                DSW(0x4F9A) = DSW(in + 6);
                s16 n = vcr_is_playing() ? 0 : throttle_notch(y);
                DSB(in + 0x1D) = (u8)n;
                DSW(0x4FA0) = (u16)n;
            }
        } else {
            DSB(in + 0x14) = DSB(0x4FA4);
            DSB(in + 0x15) = DSB(0x4FA5);
            DSB(in + 0x16) = DSB(0x4FA6);
            DSW(in) = DSW(0x4F94);
            DSW(in + 4) = DSW(0x4F98);
            DSW(in + 2) = DSW(0x4F96);
            DSW(in + 6) = DSW(0x4F9A);
            DSB(in + 0x1D) = DSB(0x4FA0);
        }
    }
    if (key_held(0x33)) DSB(in + 0x21) |= 8;
    if (key_held(0x34)) DSB(in + 0x21) |= 4;
    if (key_held(0x35)) DSB(in + 0x21) |= 0x10;
    if (key_held(0x1A)) DSB(in + 0x1A) = 0x7F;
    else if (key_held(0x1B)) DSB(in + 0x1A) = 0x81;
    u16 k = key_queue_read();
    DSB(in + 0x1B) = (u8)(k >> 8);
    if (k & 1) DSB(in + 0x21) |= 0x20;
    if (k & 2) DSB(in + 0x21) |= 0x40;
    if (k & 4) DSB(in + 0x21) |= 0x80;
    if (k & 8) DSB(in + 0x22) |= 1;
    s16 ax = DSS(in), ay = DSS(in + 4);
    if ((ax < 0 ? -ax : ax) < 8 && (ay < 0 ? -ay : ay) < 8) {
        s16 d = (s16)(kbd_dir_bits(0) & 0x0F);
        if (d < 0x0B) {
            DSW(in + 8) = DSW(0x0B8B + 2 * d);
            DSW(in + 0x0C) = DSW(0x0BA1 + 2 * d);
            DSB(in + 0x21) |= 2;
        }
    }
    u8 ctrl = BIT(in + 0x21, 7), alt = BIT(in + 0x22, 0);
    if (ctrl || alt) {
        if (DSB(in + 0x1B) == 0x10) { DSB(in + 0x1B) = 0; DSB(AP_DS_ap_quit_request) = 1; }
        else if (DSB(in + 0x1B) == 0x24) toggle_joystick();
    }
    if (ctrl && !alt) {
        if (DSB(in + 0x1B) == 1) { DSB(in + 0x1B) = 0; DSB(AP_DS_ap_quit_request) = 1; }
    } else if (alt && !ctrl) {
        switch (DSB(in + 0x1B)) {
        case 0x1F:
            toggle_sound();
            if (DSW(AP_DS_sim_mode) == 3) DSB(in + 0x1B) = 0;
            break;
        case 0x20: toggle_mouse(); break;
        case 0x14: iff_write(screenshot_name()); break;
        case 0x2D: DSB(in + 0x1B) = 0; DSB(AP_DS_ap_quit_request) = 1; break;
        default: break;
        }
    } else if (ctrl && alt) {
        if (DSB(in + 0x1B) == 0x44) iff_write(screenshot_name());
    }
    DSB(in + 0x1C) = key_name_char(DSB(in + 0x1B));
    if (mouse_active()) {
        DSB(in + 0x21) |= 1;
        mouse_pos((u16)(in + 0x10), (u16)(in + 0x12));
    } else {
        DSW(in + 0x10) = DSW(0x4F9C);
        DSW(in + 0x12) = DSW(0x4F9E);
    }
    if (DSB(in + 0x1B) == 0) {
        if (DSB(0x4F7B)) { DSB(0x4F7B) = 0; DSB(in + 0x24) |= 2; }
        else if (DSB(0x4F7A)) { DSB(0x4F7A) = 0; DSB(in + 0x24) |= 4; }
        else if (DSB(0x4F79)) { DSB(0x4F79) = 0; DSB(in + 0x24) |= 8; }
        else if (DSB(0x4F78)) { DSB(0x4F78) = 0; DSB(in + 0x24) |= 0x10; }
        else if ((s16)DSC(in + 0x1D) != DSS(0x4FA2)) {
            DSB(in + 0x1E) = DSB(in + 0x1D);
            DSS(0x4FA2) = DSC(in + 0x1D);
        }
    }
    stk_release(m);
    return in;
}

/* AP 60a9:07d2 */
void mouse_enable_set(s16 v)
{
    if (DSB(AP_DS_mouse_present)) DSB(0x4F5A) = v > 0;
}

/* AP 60a9:07ee */
s16 mouse_enable_toggle(void)
{
    if (DSB(AP_DS_mouse_present)) DSB(0x4F5A) ^= 1;
    return DSB(AP_DS_mouse_present);
}

/* AP 60a9:0804 — DS:0092 (the DOS major version) >= 3 */
static s16 mouse_present(void)
{
    return DSB(0x0092) >= 3;
}

/* AP 60a9:0817 */
s16 mouse_active(void)
{
    return mouse_present() && DSB(AP_DS_mouse_present);
}

/* AP 60a9:0832 */
static s16 mouse_init_w(void)
{
    if (DSB(0x0092) >= 3) return mouse_init();
    return 0;
}

/* AP 60a9:0847 */
s16 mouse_shutdown_w(void)
{
    if (mouse_active()) return mouse_shutdown();
    return 0;
}

/* AP 60a9:085d */
void mouse_pos(u16 x_ds, u16 y_ds)
{
    if (mouse_active()) mouse_get_pos(x_ds, y_ds);
}

/* AP 60a9:087c */
s16 ap_mouse_button(s16 i)
{
    if (mouse_active()) return mouse_button(i);
    return 0;
}

/* AP 60a9:0898 */
void mouse_warp(s16 x, s16 y, s16 force)
{
    DSW(0x4F9C) = (u16)x;
    DSW(0x4F9E) = (u16)y;
    if (!mouse_active()) return;
    if (!force && DSS(0x4F8C) == x && DSS(0x4F8E) == y) return;
    DSW(0x4F8E) = (u16)y;
    DSW(0x4F8C) = (u16)x;
    mouse_set_pos(x, y);
}

/* AP 60a9:08e3 calib_set(axis, flags, int v[3]) */
void calib_set(s16 axis, u16 flags, u16 v)
{
    if (axis >= 2) {
        s16 r = (s16)(DSS(v + 4) - DSS(v));
        DSW(0x4F76) = (u16)(r < 0x14 ? 0x14 : r);
        DSW(0x4F5E) = DSW(v + 4);
        return;
    }
    u16 d = (u16)((flags & 1) ? 0x4F60 : 0x4F66) + (u16)(12 * axis);
    DSW(d) = DSW(v);
    DSW(d + 2) = DSW(v + 2);
    DSW(d + 4) = DSW(v + 4);
}

/* AP 60a9:096d */
u16 calib_ptr(s16 j)
{
    return (u16)(0x4F60 + 12 * j);
}

/* AP 60a9:09d8 */
s16 quit_pending(void)
{
    if (DSW(0x0BB7) || DSW(0x0BB9)) return 0;
    if (!DSB(AP_DS_ap_quit_request)) return 0;
    DSW(0x0BB7) = 1;
    return DSC(AP_DS_ap_quit_request);
}

/* AP 60a9:0a04 — the "quit to DOS?" alert */
void quit_prompt(void)
{
    u16 m = stk_mark();
    u16 st = stk_alloc(0x1C);                                     /* bp-46h: gfx state */
    u16 in = stk_alloc(0x26);                                     /* bp-2Ah */
    gfx_state_save(st);
    vcr_sound_idle();
    if (DSB(AP_DS_ap_quit_request) == 2) sim_exit(0, 1);
    while (!timeout_expired(AP_DS_flip_timeout)) host_pump();
    u32 h = quit_box(0x0BBB);                                     /* DX:AX far handle */
    if (h) {
        for (;;) {
            input_poll(in);
            u8 k = DSB(in + 0x1B);
            if (k == 0x15) sim_exit(0, 1);
            if (k == 0x31 || k == 1) break;
            host_pump();
        }
        quit_box_close(h);
    }
    DSB(AP_DS_ap_quit_request) = 0;
    gfx_state_restore(st);
    DSW(0x0BB7) = 0;
    stk_release(m);
}

/* AP 60a9:0aa7 / 0ab2 */
void quit_block(void) { DSW(0x0BB9) = 1; }
void quit_unblock(void) { DSW(0x0BB9) = 0; }

/* AP 60a9:0abd */
void quit_check(void)
{
    if (DSW(0x0BB9) == 0 && quit_pending()) quit_prompt();
}

/* AP 60a9:0ad5 */
void joy_detect(void)
{
    joy_calibrate();
    DSB(0x4F5B) = DSB(AP_DS_joy_present);
    DSB(0x4F5C) = DSB(AP_DS_joy_axis_ok) & 4;
    DSB(0x4F5D) = DSB(AP_DS_joy_axis_ok) & 8;
    if (DSB(0x4F5D)) {
        s16 raw = joy_axis_raw(3);
        DSW(0x0B83) = (u16)raw;
        DSW(0x4F5E) = (u16)raw;
        DSW(0x0B81) = (u16)((s16)(raw * 11) / 8);
        DSW(0x0B83) = (u16)(raw / 4);
        if (DSS(0x0B83) <= 10) DSW(0x0B83) = 10;
    }
}

/* AP 60a9:0b35 */
void input_init(void)
{
    kbd_install(0);
    floppy_off();
    mouse_init_w();
    joy_detect();
}

/* AP 60a9:0b51 */
void input_reset(void)
{
    timeout_start(AP_DS_input_timeout, 0);
    memset(DSP(0x4F8C), 0, 0x1C);
    DSW(0x4F90) = 0xA0;
    DSW(0x4F92) = 0x64;
    if (mouse_active()) mouse_warp((s16)DSW(0x4F90), (s16)DSW(0x4F92), 1);
}

__attribute__((constructor)) static void input_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x5F2C055Bu, (CodeFn)bind_throttle_abs);
    codeptr_register(PROG_AP, 0x5F2C056Bu, (CodeFn)bind_throttle_rel);
}
