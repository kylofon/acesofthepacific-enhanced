/* shell_ui: PREFERENCES / controls (PS segment 0b48, screen 46, table 4ad6:0000, 19 items), joystick
 * calibration, and the calibration device chooser (0eb4, table 4b2e:0000) — shell_ui_options.md §4,
 * shell_ui_career.md §3. Settings bytes DS:43B6..43BC (control.prf): 0 joystick, 1 mouse, 2 effects,
 * 3 music, 4 flight device, 5 rudder device, 6 throttle device. */
#include "shell_ui/sui_a_priv.h"

s16 mouse_toggle(void);                  /* shell_data/glue.c (PS 230d:0018) */
s16 joy_toggle(void);                    /* shell_data/glue.c (PS 230d:008b) */
u16 cal_choose(void);

#define TBL_PREF    fd(0x4AD6, 0x0000)
#define TBL_CAL     fd(0x4B2E, 0x0000)   /* 8 items */
#define SET         0x43B6               /* u8[7] */
#define PF_OPEN     0x085A               /* u8: the preferences screen is open */
#define PF_PENDING  0x085B               /* u8: conflict messages queued */
#define PF_CALIB    0x085C               /* u8: CALIBRATE pressed */
#define PF_LABELS   0x085D               /* near char **[7] */
#define PF_COUNTS   0x086B               /* s16[3] */
#define PF_ROWS     0x0871               /* s16[3]: SFTBTNS0.DAT rows */
#define PF_CHOICE   0x0877               /* s16 */
#define PF_Q0       0x0879               /* s16: pending setting */
#define PF_Q1       0x087B               /* s16 */
#define PF_VARIANT  0x087D               /* s16 */
#define PF_TOGGLES  0x087F               /* far fn[4] */
#define PF_CHOSEN   0x088F               /* far fn[3] */
#define PF_TOGITEM  0x089B               /* s8[4] */
#define CAL_N       0x4472               /* s8: devices listed */
#define CAL_DEV     0x4473               /* s8[4] */
#define CAL_OK      0x4477               /* u8 */

/* PS 0b48:0006 pref_remap(setting, v): rudder / throttle choices with the FCS */
static s16 pref_remap(s16 k, s16 v)
{
    if ((k == 6 || k == 5) && DSB(SET + 4) == 3) v = DSS((u16)(v * 2 + (k == 6 ? 0x089F : 0x08A3)));
    return v;
}

/* PS 0b48:0043 pref_msg_subst(c) -> far text for the conflict message (REQUEST 17) */
FarPtr pref_msg_subst(s8 c)
{
    u8 b = DSB((u16)(DSW(PF_Q0) + SET));
    u16 arr = DSW(PF_Q0) == 6 ? 0x4370 : 0x4384;
    u16 r;
    switch (c) {
    case 'k': r = DSW(PF_Q0) == 6 ? DSW(0x436E) : DSW(0x436C); break;
    case 'l': r = DSW((u16)(DSW(PF_VARIANT) * 2 + 0x436A)); break;
    case 't': r = DSW((u16)(arr + (s8)b * 2)); break;
    case 'u': r = DSW(PF_VARIANT) != 0 ? DSW((u16)(DSW(PF_VARIANT) * 2 + 0x4370)) : DSW(0x4394); break;
    default: return fnul();
    }
    return far_make(cur_dgroup, r);
}

/* PS 0b48:00e5 pref_set_device(item) */
static void pref_set_device(FarPtr item)
{
    s16 k = fs(item, IT_USER);
    u16 arr = DSW((u16)(k * 2 + PF_LABELS));
    DSB((u16)(SET + k)) = (u8)pref_remap(k, DSS(PF_CHOICE));
    fww(item, IT_LABEL, DSW((u16)(arr + DSS(PF_CHOICE) * 2)));
    fbw(item, IT_MASK, 3);
}

/* PS 0b48:0133 pref_flight_chosen */
u16 pref_flight_chosen(void)
{
    if (DSS(PF_CHOICE) == 3) {                         /* Thrustmaster FCS */
        pref_set_device(far_add(TBL_PREF, 0x134));
        DSW(0x0875) = 0xAA;
        DSW(0x0873) = 0xA2;
        DSW(0x086F) = 2;
        DSW(0x086D) = 2;
        DSW(0x0867) = 0x438A;
        DSW(0x0869) = 0x4378;
        DSW(PF_Q1) = 0xFFFF;
        DSW(PF_Q0) = 0xFFFF;
        if (DSB(SET + 6) == 1 || DSB(SET + 6) == 2) {
            DSB(PF_PENDING) = 1;
            DSW(PF_Q0) = 6;
        }
        if (DSB(SET + 5) == 1) {
            DSB(PF_PENDING) = 1;
            DSW((u16)((DSS(PF_Q0) != -1) * 2 + PF_Q0)) = 5;
        }
        DSW(PF_VARIANT) = 0;
    } else {
        DSW(0x0873) = 0x9E;
        DSW(0x0875) = 0xA5;
        DSW(0x086D) = 3;
        DSW(0x086F) = 4;
        DSW(0x0867) = 0x438E;
        DSW(0x0869) = 0x437C;
        pref_set_device(far_add(TBL_PREF, 0x134));
    }
    return 1;
}

/* PS 0b48:0201 pref_rudder_chosen */
u16 pref_rudder_chosen(void)
{
    if (pref_remap(fs(TBL_PREF, 0x15A), DSS(PF_CHOICE)) == 1 && DSB(SET + 6) == 2) {
        DSB(SET + 5) = 1;
        DSW(PF_Q0) = 5;
        DSW(PF_Q1) = 0xFFFF;
        DSW(PF_VARIANT) = 2;
        DSB(PF_PENDING) = 1;
    } else {
        pref_set_device(far_add(TBL_PREF, 0x14A));
    }
    return 1;
}

/* PS 0b48:0261 pref_throttle_chosen */
u16 pref_throttle_chosen(void)
{
    if (pref_remap(fs(TBL_PREF, 0x170), DSS(PF_CHOICE)) == 2 && DSB(SET + 5) == 1) {
        DSB(SET + 6) = 2;
        DSW(PF_Q0) = 6;
        DSW(PF_Q1) = 0xFFFF;
        DSW(PF_VARIANT) = 1;
        DSB(PF_PENDING) = 1;
    } else {
        pref_set_device(far_add(TBL_PREF, 0x160));
    }
    return 1;
}

/* PS 0b48:02c1 pref_draw_window */
void pref_draw_window(void)
{
    draw_titled_panel();
    s16 y = (s16)(RYs + 20);
    draw_rivets((u16)(RXs + 4), (u16)y, 22);
    draw_rivets((u16)(RXs + RWs - 6), (u16)y, 22);
}

/* PS 0b48:0307 pref_draw_group */
void pref_draw_group(void)
{
    draw_bevel_box();
    draw_corner_rivets();
    gfx_font_select((s16)DSW(T_FONT3));
    text_engrave_center(fw(cur_item(), IT_LABEL), 0xC1, 0xB1, 0xC0);
}

/* PS 0b48:03c7 pref_toggle(k): also the Alt-J/D/S/M hotkeys */
void pref_toggle(u16 k_)
{
    s8 k = (s8)k_;
    FarPtr item = fnul();
    u16 arr = 0;
    if (DSB(PF_OPEN)) {
        item = far_add(TBL_PREF, (u16)(DSC((u16)(k + PF_TOGITEM)) * IT_SIZE));
        arr = DSW((u16)(fs(item, IT_USER) * 2 + PF_LABELS));
    }
    FarPtr fn = ds_far((u16)(k * 4 + PF_TOGGLES));
    if (!far_is_null(fn)) call_far(fn);
    if (DSB(PF_OPEN)) {
        fww(item, IT_LABEL, DSW((u16)(arr + DSC((u16)(SET + k)) * 2)));
        fbw(item, IT_MASK, 3);
    }
}

/* PS 0b48:044e pref_toggle_cb */
void pref_toggle_cb(void)
{
    if (btn_cb()) pref_toggle((u16)(s8)(DSB(T_IDX) - 3));
}

/* PS 0b48:0467 pref_choice(v): CHOOSE callback */
void pref_choice(s16 v) { DSS(PF_CHOICE) = v; }

/* PS 0b48:0472 pref_device_cb: flight / rudder / throttle device lists */
void pref_device_cb(void)
{
    if (!btn_small_cb(0)) return;
    s16 k = fs(cur_item(), IT_USER);
    ps_strings_load_24e3(make_name(0x08DF /* "sftbtns" */, 0, 0x08E7 /* "dat" */));
    fww(cur_item(), IT_USER, DSW((u16)(k * 2 + PF_COUNTS)));
    DSW(PF_CHOICE) = 0xFFFF;
    choose_run(FNP(pref_choice), (u16)(s16)DSC((u16)(SET + 4 + k)), DSW((u16)(k * 2 + PF_ROWS)));
    hs_restart();
    FarPtr fn = ds_far((u16)(k * 4 + PF_CHOSEN));
    if (DSS(PF_CHOICE) != -1 && !far_is_null(fn)) call_far(fn);
    fww(cur_item(), IT_USER, (u16)k);
    strings_free();
}

/* PS 0b48:0510 pref_draw_label */
void pref_draw_label(void)
{
    draw_inset_box2();
    gfx_font_select((s16)DSW(T_FONT3));
    u16 l = fw(cur_item(), IT_LABEL);
    s16 tx = (s16)ps_text_center_x(l, (u16)RXs, (u16)RWs);
    DSB(TXT_FLAGS) = 1;
    DSB(TXT_FG) = 0xCC;
    gfx_text(l, tx, (s16)(RYs + 4));
    fbw(cur_item(), IT_MASK, 0x40);
}

/* PS 0b48:0574 cal_wait_button -> 1 after a joystick button, 0 on Esc */
static u8 cal_wait_button(void)
{
    menu_flush();
    for (;;) {
        host_pump();
        if (DSB(PS_DS_mouse_on) && (mouse_button_dos3(0) || mouse_button_dos3(1))) continue;
        if (DSB(PS_DS_joy_on) && (joy_button(0) || joy_button(1))) continue;
        break;
    }
    while (!joy_button(0) && !joy_button(1)) {
        host_pump();
        if (key_held(1)) return 0;
    }
    do {
        do host_pump(); while (joy_button(0));
    } while (joy_button(1));
    return 1;
}

/* PS 0b48:0616 cal_prompt(i, row) */
static u8 cal_prompt(s16 i, s16 row)
{
    DSW(PG_WRITE) = DSW(PG_BACK);
    DSW(PG_READ) = DSW(PG_BACK);
    draw_popup_box();
    ps_rect_shrink(6);
    flow_text(strings_get(row, i), 1, fnul());
    rect_grow(6);
    pages_back_to_front();
    gfx_copy_rect(0x50, 0x32, 0xA0, 0x1E);
    return cal_wait_button();
}

/* PS 0b48:0680 cal_erase */
static void cal_erase(void)
{
    pages_bg_to_back();
    gfx_copy_rect(0x50, 0x32, 0xA0, 0x1E);
    pages_back_to_front();
    gfx_copy_rect(0x50, 0x32, 0xA0, 0x1E);
}

/* PS 0b48:06bf cal_sort(v[3], n, throttle) */
static void cal_sort(u16 v, s16 n, u8 throttle)
{
#define V(i) DSS((u16)(v + 2 * (i)))
    if (n == 2) {
        V(2) = V(1);
        V(1) = (s16)((s16)(V(0) + V(2)) / 2);
    }
    s16 t = V(0);
    if (V(1) < t) { V(0) = V(1); V(1) = t; }
    t = V(0);
    if (V(2) < t) { V(0) = V(2); V(2) = t; }
    if (V(2) < V(1)) { t = V(1); V(1) = V(2); V(2) = t; }
    if (throttle) {
        t = V(2);
        s16 d = (s16)(t - V(0));
        if (d < 20) d = 20;
        V(2) = d;
        V(1) = t;
    }
#undef V
}

/* PS 0b48:0740 cal_params(dev, &n, &axes, &raw, &xs, &ys) -> CALBRATE.DAT row */
static s16 cal_params(s16 dev, s16 *n, u16 *axes, u8 *raw, u16 *xs, u16 *ys)
{
    *n = DSS((u16)(dev * 2 + 0x08B5));
    *axes = DSW((u16)(dev * 2 + 0x08C3));
    *raw = DSB((u16)(dev + 0x08D1));
    if (DSB((u16)(dev + 0x08D8)) == 0) { *xs = 0x43C9; *ys = 0x43CF; }
    else { *xs = 0x43BD; *ys = 0x43C3; }
    return DSS((u16)(dev * 2 + 0x08A7));
}

/* PS 0b48:079c calibrate(dev) */
static void ps_calibrate(s16 dev)
{
    s16 n;
    u16 axes, xs, ys;
    u8 raw;
    s16 row = cal_params(dev, &n, &axes, &raw, &xs, &ys);
    copy_back_to_bg();
    joy_calibrate();
    RXs = 0x50; RYs = 0x32; RWs = 0xA0; RHs = 0x1E;
    gfx_font_select((s16)DSW(T_FONT3));
    flow_setup(3, 8, 1);
    DSB(TXT_FLAGS) = 1;
    s16 i = 0;
    u8 ok = 1;
    while (i < n && ok) {
        ok = cal_prompt(i, row);
        if (ok) {
            if (i == 0) joy_calibrate();
            if (raw == 0) {
                joy_read(0, (u16)(xs + i * 2), (u16)(ys + i * 2));
            } else {
                if (axes & 1) DSS((u16)(xs + i * 2)) = joy_axis_raw(2);
                if (axes & 2) DSS((u16)(ys + i * 2)) = joy_axis_raw(3);
            }
        }
        cal_erase();
        i++;
    }
    if (raw == 0 && i > 1) {
        cal_prompt(0, row);
        cal_erase();
    }
    if (ok) {
        if (axes & 1) cal_sort(xs, n, 0);
        if (axes & 2) cal_sort(ys, n, dev == 2);
    }
}

/* PS 0b48:0902 pref_calibrate_cb */
void pref_calibrate_cb(void)
{
    if (btn_small_cb(0) && DSB(PS_DS_joy_on)) DSB(PF_CALIB) = 1;
}

/* PS 0b48:0920 pref_accept_cb */
void pref_accept_cb(void)
{
    if (btn_cb()) menu_set_result(1);
}

/* PS 0b48:0938 pref_dup_strings(first, n, dst) */
static void pref_dup_strings(s16 first, s16 n, u16 dst)
{
    for (s8 k = 0; k < n; k++) {
        u16 s = str_get((s16)(first + k));
        DSW((u16)(k * 2 + dst)) = xmalloc((u16)(rtl_strlen(s) + 1));
        rtl_strcpy(DSW((u16)(k * 2 + dst)), s);
    }
}

/* PS 0b48:099d pref_labels_load */
static void pref_labels_load(void)
{
    pref_dup_strings(10, 2, 0x43AC);
    pref_dup_strings(12, 2, 0x43A8);
    pref_dup_strings(14, 2, 0x43A4);
    pref_dup_strings(16, 2, 0x43A0);
    pref_dup_strings(18, 5, 0x4396);
    pref_dup_strings(23, 3, 0x438E);
    pref_dup_strings(26, 4, 0x437C);
    DSW(0x4378) = DSW(0x437C);
    DSW(0x437A) = DSW(0x4382);
    DSW(0x438A) = DSW(0x438E);
    DSW(0x438C) = DSW(0x4392);
    DSW(0x4388) = 0;
    DSW(0x4384) = 0;
    DSW(0x4376) = 0;
    DSW(0x4370) = 0;
    pref_dup_strings(30, 2, 0x4372);
    pref_dup_strings(30, 1, 0x4386);
    pref_dup_strings(32, 1, 0x4394);
    pref_dup_strings(7, 3, 0x436A);
}

/* PS 0b48:0a99 pref_free_strings(a, n) */
static void pref_free_strings(u16 a, s16 n)
{
    for (s8 k = 0; k < n; k++) xfree(DSW((u16)(a + k * 2)));
}

/* PS 0b48:0acf _free: pref_labels_free */
static void pref_labels_free(void)
{
    pref_free_strings(0x43AC, 2);
    pref_free_strings(0x43A8, 2);
    pref_free_strings(0x43A4, 2);
    pref_free_strings(0x43A0, 2);
    pref_free_strings(0x4396, 5);
    pref_free_strings(0x438E, 3);
    pref_free_strings(0x437C, 4);
    pref_free_strings(0x4372, 2);
    pref_free_strings(0x4394, 1);
    pref_free_strings(0x4386, 1);
    pref_free_strings(0x436A, 3);
}

/* PS 0b48:0b6e pref_apply_all: re-applies each toggle (from sys_init) */
void pref_apply_all(void)
{
    for (s16 k = 0; k < 4; k++) {
        DSB((u16)(SET + k)) ^= 1;
        FarPtr fn = ds_far((u16)(k * 4 + PF_TOGGLES));
        if (!far_is_null(fn)) call_far(fn);
    }
    if (DSB(SET + 4) == 1) {
        DSB(0x0FF8) = 0;
        joy_calibrate();
        if (DSB(0x0FF8) == 0) DSB(SET + 4) = 0;
    }
}

/* PS 0b48:0bb9 pref_labels_init */
static void pref_labels_init(void)
{
    FarPtr t = TBL_PREF;
    for (s16 k = 0; k < 7; k++) {
        s16 it = k < 4 ? (s16)(k + 7) : (s16)(k + 10);
        fww(t, (u16)(it * IT_SIZE + IT_LABEL), DSW((u16)(DSW((u16)(k * 2 + PF_LABELS)) + DSC((u16)(SET + k)) * 2)));
    }
    if (DSB(SET + 4) == 3) {
        DSW(PF_CHOICE) = 3;
        pref_flight_chosen();
    }
}

/* PS 0b48:0c22 pref_frame: queued conflict messages, the calibration request */
void pref_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
    if (DSB(PF_PENDING)) {
        while (DSS(PF_Q0) >= 0) {
            u16 it = DSW(PF_Q0) == 6 ? 0x160 : 0x14A;
            DSW(PF_CHOICE) = 0;
            FarPtr subst = FNP(pref_msg_subst);
            req_ok(subst.off, subst.seg, 0x11, 8);
            pref_set_device(far_add(TBL_PREF, it));
            DSW(PF_Q0) = DSW(PF_Q1);
            DSW(PF_Q1) = 0xFFFF;
        }
        DSB(PF_PENDING) = 0;
    }
    if (DSB(PF_CALIB)) {
        ps_strings_load_24e3(make_name(0x08EB /* "calbrate" */, -1, 0x08F4 /* "dat" */));
        s16 d = (s16)cal_choose();
        if (d != 0) ps_calibrate((s16)(d - 1));
        DSB(PF_CALIB) = 0;
        strings_free();
    }
}

/* PS 0b48:0cf0 prefs_screen */
void prefs_screen(void)
{
    DSB(PF_OPEN) = 1;
    DSB(PF_PENDING) = 0;
    screen_show(46, 0x396, 1);
    pref_labels_load();
    pref_labels_init();
    hs_set_table(TBL_PREF, 19);
    hs_draw_all();
    menu_run(FNP(pref_frame));
    ui_busy();
    pref_labels_free();
    DSB(PF_OPEN) = 0;
}

/* ======================================================================== device chooser (0eb4) */

/* PS 0eb4:000c cal_draw_title */
void cal_draw_title(void)
{
    u16 l = fw(cur_item(), IT_LABEL);
    gfx_font_select((s16)DSW(T_FONT4));
    text_engrave(l, 0xC1, 0xB1, 0xC0, (u16)RXs, (u16)RYs);
}

/* PS 0eb4:004a cal_btn_cb */
void cal_btn_cb(void)
{
    if (btn_small_cb(0)) {
        DSB(CAL_OK) = DSW(T_IDX) == 7;
        menu_set_result(1);
    }
}

/* PS 0eb4:0076 cal_draw_row(i, y) */
void cal_draw_row(s16 i, s16 y)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(20);
    strcpy_far_src(buf, strings_get(0, DSC((u16)(CAL_DEV + i))));
    gfx_text(buf, (s16)(RXs + 5), y);
    stk_release(m);
}

/* PS 0eb4:00b9 cal_build -> any device to calibrate */
static u8 cal_build(void)
{
    DSB(CAL_DEV + 3) = 0xFF;
    DSB(CAL_DEV + 2) = 0xFF;
    DSB(CAL_DEV + 1) = 0xFF;
    DSB(CAL_DEV) = 0xFF;
    DSB(CAL_N) = 0;
    if (DSB(SET + 4) == 1) {
        DSB(CAL_DEV) = 0;
        DSB(CAL_N)++;
    } else if (DSB(SET + 4) == 3) {
        DSB(CAL_DEV) = 1;
        DSB(CAL_N) = 1;
        DSB(CAL_DEV + 1) = 2;
        DSB(CAL_N)++;
    } else if (DSB(SET + 4) == 2) {
        DSB(CAL_DEV) = 3;
        DSB(CAL_N)++;
    }
    if (DSB(SET + 5) == 1) {
        DSB((u16)(CAL_DEV + DSC(CAL_N))) = 6;
        DSB(CAL_N)++;
    } else if (DSB(SET + 5) == 2) {
        DSB((u16)(CAL_DEV + DSC(CAL_N))) = 4;
        DSB(CAL_N)++;
    }
    if (DSB(SET + 6) == 1) {
        if (DSB(SET + 5) != 1) {
            DSB((u16)(CAL_DEV + DSC(CAL_N))) = 6;
            DSB(CAL_N)++;
        }
    } else if (DSB(SET + 6) == 2) {
        DSB((u16)(CAL_DEV + DSC(CAL_N))) = 5;
        DSB(CAL_N)++;
    }
    if (DSB(CAL_N) == 0) return 0;
    if (DSB(CAL_N) != 1) {
        req_open(61 /* CWHICH */, 0x46);
        lbox_setup(0, 0, (u16)(s16)DSC(CAL_N), 5, fnul(), FNP(cal_draw_row));
        lbox_bind(TBL_CAL, 2, 4);
        DSB(CAL_OK) = 0;
    }
    return 1;
}

/* PS 0eb4:01e4 cal_frame */
void cal_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 0eb4:0205 cal_choose -> device + 1, 0 = none */
u16 cal_choose(void)
{
    if (!cal_build()) return 0;
    s8 c = DSC(CAL_DEV);
    if (DSB(CAL_N) != 1) {
        dlg_run(TBL_CAL, 8, FNP(cal_frame));
        if (DSB(CAL_OK) == 0) return 0;
        c = DSC((u16)(CAL_DEV + (s16)lbox_selected()));
    }
    return (u16)(s16)(c + 1);
}

__attribute__((constructor)) static void prefs_codeptrs(void)
{
    REG(pref_msg_subst);
    REG(pref_flight_chosen);
    REG(pref_rudder_chosen);
    REG(pref_throttle_chosen);
    REG(pref_draw_window);
    REG(pref_draw_group);
    REG(pref_toggle_cb);
    REG(pref_choice);
    REG(pref_device_cb);
    REG(pref_draw_label);
    REG(pref_calibrate_cb);
    REG(pref_accept_cb);
    REG(pref_frame);
    REG(cal_draw_title);
    REG(cal_btn_cb);
    REG(cal_draw_row);
    REG(cal_frame);
    /* the toggles DS:087F points at (shell_data's and ps_start.c's) */
    codeptr_register(PROG_PS, PS_FN_joy_toggle, (CodeFn)joy_toggle);
    codeptr_register(PROG_PS, PS_FN_mouse_toggle, (CodeFn)mouse_toggle);
    REG(snd_fx_toggle);
    REG(snd_music_toggle);
}
