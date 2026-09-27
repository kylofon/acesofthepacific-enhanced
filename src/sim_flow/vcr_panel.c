/* sim_flow: vcr_panel.c — the VCR panel draw callbacks and state setters (5ea5 vcr_gfx.c) and the
 * dialog / flight helpers of 606e (rlsmgfx.c) — sim_flow_vcr.md §10. The panel is the VCR.BOX item
 * list at far 38E6:0015 (37 items of 16h bytes; item k at 0015 + k*16h). */
#include "sim_flow/sfb_priv.h"

#define PANEL_SEG 0x38E6
#define PB(o) SEGB(PANEL_SEG, o)
#define PW(o) SEGW(PANEL_SEG, o)

/* AP 5ea5:0000 panel_cb_0000() -> enabled (item +10h has no 7000h disable bits); event mask 74h */
u16 panel_cb_0000(void)
{
    FarPtr it = CUR_ITEM();
    u16 r = (IT_W(it, 0x10) & 0x7000) == 0;
    wr8(it.seg, (u16)(it.off + 0x0C), 0x74);
    return r;
}

/* AP 5ea5:0020 panel_cb_0020() — callback: bevel box */
void panel_cb_0020(void)
{
    bevel_box(0, 0, 0, 0);
}

/* AP 5ea5:002a panel_cb_slider() — callback: the tape position slider */
void panel_cb_slider(void)
{
    slider_draw(3);
}

/* AP 5ea5:0039 panel_slider_enable(on): item 15h flag 40h (disabled) */
void panel_slider_enable(s16 on)
{
    if (on) PB(0x01F0) &= 0xBF;
    else PB(0x01F0) |= 0x40;
}

/* AP 5ea5:005c panel_cb_click1() — callback: arrow-style button, result = item index */
void panel_cb_click1(void)
{
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x74);
    if (button_style_a(far_make(0, 0))) DSW(0x3BC1) = (u16)CUR_IDX;
}

/* AP 5ea5:0079 panel_cb_click2() — callback: plain button */
void panel_cb_click2(void)
{
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x74);
    if (button_press(0)) DSW(0x3BC1) = (u16)CUR_IDX;
}

/* AP 5ea5:009b panel_cb_click3() — callback: diamond camera button (pressed: index, released: +0Eh) */
void panel_cb_click3(void)
{
    if (lib2_cb_c(far_make(0, 0)) != 0) {
        if (STATE & 4) DSW(0x3BC1) = (u16)CUR_IDX;
        else if (STATE & 0x30) DSW(0x3BC1) = (u16)(CUR_IDX + 0x0E);
    }
}

/* AP 5ea5:00ca hms(out[3], secs) -> out: hours, minutes, seconds */
u16 hms(u16 out_ds, s32 secs)
{
    s16 rem;
    s16 m = ldiv_sat_rem(secs, 0x3C, &rem);
    DSW(out_ds + 4) = (u16)rem;
    s16 h = ldiv_sat_rem((s32)m, 0x3C, &rem);
    DSW(out_ds + 2) = (u16)rem;
    DSW(out_ds) = (u16)h;
    return out_ds;
}

/* AP 5ea5:0123 panel_draw_time() — callback: "hh:mm:ss" DS:0ACB at the x offsets DS:0AD4 */
void panel_draw_time(void)
{
    clear_26c3();
    inset_box();
    DSB(0x26C0) = 0xCC;
    DSB(0x26C2) = 1;
    gfx_font_select(DSS(0x3F2E));
    u16 m = stk_mark();
    u16 ch = stk_alloc(2);
    DSB(ch + 1) = 0;
    for (s16 i = 0; i < 8; i++) {
        DSB(ch) = DSB(0x0ACB + i);
        gfx_text(ch, (s16)(RX + DSC(0x0AD4 + i)), (s16)(RY + 3));
    }
    stk_release(m);
}

/* AP 5ea5:017f panel_draw_lamp() — callback: play/record lamp by 38E6:012D */
void panel_draw_lamp(void)
{
    u8 col = 0xCF;
    if (PW(0x012D) == 0) { col = 0x8F; PW(0x012B) = 0x0B0A; }
    else if (PW(0x012D) == 1) PW(0x012B) = 0x0B0C;
    else PW(0x012B) = 0;
    inset_box();
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xC5;
    DSB(0x26CE) = 0xC5;
    gfx_rect((s16)(RX + 1), (s16)(RY + 1), (s16)(RW - 2), (s16)(RH - 2));
    FarPtr it = CUR_ITEM();
    u16 s = IT_W(it, 0x0E);
    if (s != 0) {
        DSB(0x26C0) = col;
        DSB(0x26C2) = 1;
        gfx_font_select(DSS(0x3F2C));
        gfx_text(s, ap_text_center_x(s, RX, RW), (s16)(RY + 1));
    }
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
    if (PW(0x012D) == 3) {
        item_center(far_make(0, 0xCF));
        gfx_rect((s16)(RX + 4), (s16)(RY + 3), (s16)(RW - 8), (s16)(RH - 6));
    }
}

/* AP 5ea5:027e blink_timer() */
void blink_timer(void)
{
    timeout_start(0x0166, 0x3B);
}

/* AP 5ea5:0291 put_2digits(v, dst): a single digit keeps the template's leading '0' */
void put_2digits(s16 v, u16 dst)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(6);
    rtl_itoa(v, b, 10);
    if (DSB(b + 1) == 0) dst++;
    for (u16 p = b; DSB(p); p++) DSB(dst++) = DSB(p);
    stk_release(m);
}

/* AP 5ea5:02ca panel_time() -> mission clock while the slider is settled, else the slider value */
s32 panel_time(void)
{
    if (slider_settled(3)) return clock_get();
    return (s32)slider_value(3);
}

/* AP 5ea5:02f7 panel_update() -> changed (counter, time display, lamp) */
s16 panel_update(void)
{
    s16 changed = 0;
    if (slider_set(3, (s16)((s32)DSL(0x4E3B) >> (DSB(0x088D) & 0x1F)))) {
        PB(0x01EF) = 0x43;
        changed = 1;
    }
    if (timeout_expired(0x0160)) {
        timeout_start(0x0160, 0x0F);
        u8 mode = (u8)vcr_mode();
        s32 t = memchr(DSP(0x0ADC), mode, 4) ? clock_get() : panel_time();
        if (t != DSSL(0x0ABE)) {
            DSSL(0x0ABE) = t;
            memcpy(DSP(0x0ACB), DSP(0x0AC2), 9);
            u16 m = stk_mark();
            u16 o = stk_alloc(6);
            hms(o, t);
            put_2digits(DSS(o + 4), 0x0AD1);
            put_2digits(DSS(o + 2), 0x0ACE);
            put_2digits(DSS(o), 0x0ACB);
            stk_release(m);
            PB(0x013F) = 0x43;
            changed = 1;
        }
    }
    u16 di = PW(0x012D), si = di;
    switch (vcr_mode()) {
    case 1: si = 0; break;
    case 2: si = 1; break;
    case 3: case 7:
        if (di == 1 || di == 2) {
            if (timeout_expired(0x0166)) { blink_timer(); si = di ^ 3; }
        } else {
            blink_timer();
            si = 1;
        }
        break;
    case 4: case 5: si = 3; break;
    default: break;
    }
    if (si != di) {
        PW(0x012D) = si;
        PB(0x0129) = 0x43;
        changed = 1;
    }
    return changed;
}

/* AP 5ea5:047a panel_draw_frame() — callback: B8h box and the engraved title */
void panel_draw_frame(void)
{
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
    item_center(far_make(0, 0xB8));
    gfx_rect(RX, RY, RW, RH);
    dlg_title_shadow(far_make(0, 0));
}

/* AP 5ea5:04b0 panel_view_labels(): camera button labels by DS:19F2 */
void panel_view_labels(void)
{
    if (DSW(0x19F2) == 0) { PW(0x00BD) = 0x61; PW(0x00D3) = 0x62; }
    else { PW(0x00BD) = 0x5F; PW(0x00D3) = 0x60; }
    PB(0x00D1) = 0x43;
    PB(0x00BB) = 0x43;
}

/* AP 5ea5:04ff panel_end_label(ended) */
void panel_end_label(s16 ended)
{
    PW(0x02E3) = ended ? 0x63 : 0x64;
    PB(0x02E1) = 0x43;
}

/* AP 5ea5:052f view_name_cpy(s) (near) -> DS:0AE0 */
u16 view_name_cpy(u16 s)
{
    rtl_strcpy(0x0AE0, s);
    return 0x0AE0;
}
/* AP 5ea5:0530 */
u16 view_name_cpy_body(u16 s) { return view_name_cpy(s); }

/* AP 5ea5:055f view_name_cat(s) (near) */
u16 view_name_cat(u16 s)
{
    rtl_strcat(0x0AE0, s);
    return 0x0AE0;
}
/* AP 5ea5:0560 */
u16 view_name_cat_body(u16 s) { return view_name_cat(s); }

/* AP 5ea5:0597 panel_view_name() -> DS:0AE0: the name of the current view; disables the buttons that
 * do not apply to it */
u16 panel_view_name(void)
{
    u16 m = stk_mark();
    u16 old = stk_alloc(0x2A);
    rtl_strcpy(old, 0x0AE0);
    s16 k = view_get();
    u16 list;
    if ((u16)k <= 5) {
        list = 0x09D3;
        view_name_cpy(str0((s16)(k + 0x65)).off);
        view_name_cat(str0(0x6B).off);
    } else if ((u16)k >= 0x0C && (u16)k <= 0x15) {
        list = 0x09D6;
        view_name_cpy(str0(0x6C).off);
        view_name_cat(str0((s16)(k + 0x61)).off);
    } else if (k == 0x17) {
        list = 0x09C9;
        view_name_cpy(str0(0x76).off);
    } else if (k == 0x18) {
        list = 0x09C7;
        view_name_cpy(str0(0x77).off);
    } else {
        list = 0x09D6;
        if (wcam_is_active()) {
            view_name_cpy(str0(0x8E).off);
        } else {
            u16 o = view_outside_target();
            view_name_cpy(str0(0x78).off);
            view_name_cat(str0((s16)((s16)DSC(o + 0x26) + 0x8C)).off);
            if (DSB(o + 0x18) == 0)
                view_name_cat((u16)(DSW(DSW(0x55D2) + 2 * DSB(0x54BC + DSB(o + 0x19))) + 0x0A));
            else if (DSB(o + 0x18) == 1)
                view_name_cat((u16)(DSW(DSW(0x55D0) + 2 * DSB(0x555B + DSB(o + 0x19))) + 0x4E));
            else
                view_name_cat(0x0B0E);                /* "a Ghost" */
        }
    }
    DSB(0x0B09) = 0;
    panel_disable(list, 0x4000);
    if (rtl_strcmp(old, 0x0AE0) != 0) PB(0x0155) = 0x43;
    stk_release(m);
    return 0x0AE0;
}

/* AP 5ea5:0784 panel_set_title(name): DS:4EC8 = name cut at the '.' */
void panel_set_title(u16 name_ds)
{
    rtl_strcpy(0x4EC8, name_ds);
    u16 d = rtl_strchr(0x4EC8, '.');
    if (d) DSB(d) = 0;
    PB(0x016B) = 0x43;
}

/* AP 5ea5:07df panel_draw_title() — callback */
void panel_draw_title(void)
{
    FarPtr it = CUR_ITEM();
    u16 s = IT_W(it, 0x0E);
    gfx_font_select(DSS(0x3F2C));
    s16 tx = ap_text_center_x(s, RX, RW);
    s16 y = RY;
    if (DSB(0x26DF) == 0) y++;
    text_engrave(s, 0xC1, 0xB1, 0xC0, (u16)tx, (u16)y);
}

/* AP 5ea5:0839 panel_cb_click4() — callback: plain button */
void panel_cb_click4(void)
{
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x74);
    if (button_press(0)) DSW(0x3BC1) = (u16)CUR_IDX;
}

/* AP 5ea5:085b panel_init_lamp() */
void panel_init_lamp(void)
{
    PW(0x012D) = 3;
}

/* ---- 606e ------------------------------------------------------------------------------------------ */

/* AP 606e:0000 tape_tmp_name() -> "_tape_.tmp" (DS:0B74) */
u16 tape_tmp_name(void)
{
    return 0x0B74;
}

/* AP 606e:0008 dlg_stub() — empty */
void dlg_stub(void)
{
}

/* AP 606e:000d dlg_force_redraw() */
void dlg_force_redraw(void)
{
    DSB(0x4F08) = 1;
}

/* AP 606e:0018 panel_click(mouse): a button 1..4 -> click sound, keyboard focus handling */
void panel_click(u16 m_ds)
{
    s16 b = DSS(m_ds + 6);
    if (b > 0 && b < 5) {
        sx_play(0x10, 0, 0, 0);
        dlg_click(DSS(m_ds), DSS(m_ds + 2), (s16)(b - 1));
    }
}

/* AP 606e:0057 dlg_redraw_all() */
void ap_dlg_redraw_all_606e(void)
{
    quit_block();
    vcr_refresh_screen(0);
    wipe_in(2);
    quit_unblock();
}

/* AP 606e:007a dlg_poll() -> the hit item's result, 9 for Esc */
s16 dlg_poll(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x26), ms = stk_alloc(8);
    s16 r;
    if (quit_pending()) {
        ap_dlg_redraw_all_606e();
        quit_prompt();
        DSB(0x3BB8) = 1;
        dlg_force_redraw();
    }
    vcr_sound_frame(0);
    input_poll(in);
    host_pump();                                      /* PORT: every dialog loop polls through here */
    if (DSB(in + 0x1B) == 1) {
        pointer_store(0xA0, 0x64);
        r = 9;
    } else {
        pointer_input(ms, in);
        vcr_debug_mouse(ms);
        panel_click(ms);
        write_back();
        ap_dlg_redraw_all_53a6(DSB(in + 0x1B), (u16)(s16)DSC(in + 0x1C));
        r = dlg_hit_dispatch(DSS(ms), DSS(ms + 2), DSS(ms + 4), (s16)DSB(0x4F08));
        dlg_redraw_dirty();
        DSB(0x4F08) = 0;
    }
    vcr_sound_frame(0);
    stk_release(m);
    return r;
}

/* AP 606e:012d realism_changed() */
void ap_realism_changed_606e(void)
{
    ap_realism_changed_522c();
}

/* AP 606e:0137 realism_dialog_in_flight() -> changed (Alt+R) */
s16 realism_dialog_in_flight(void)
{
    u16 m = stk_mark();
    u16 old = stk_alloc(0x20);
    memcpy(DSP(old), DSP(AP_DS_realism_prf), 0x20);
    realism_open();
    dlg_activate(far_make(SEG(0x39A4), 0x15), 0x24);
    dlg_force_redraw();
    for (;;) {
        s16 r = dlg_poll();
        if (r == 9) break;
        if (r == 0x0F) dlg_force_redraw();
    }
    /* sic: bytes of the settings against words of the copy (the copy is an int[16]) */
    if ((s16)DSC(0x42CA) != DSS(old + 4) || (s16)DSC(0x42CB) != DSS(old + 6) ||
        (s16)DSC(0x42D1) != DSS(old + 0x12) || (s16)DSC(0x42CF) != DSS(old + 0x0E))
        msg_clear_top();
    if (DSB(0x42D1) == 0) {
        memset(DSP(0x57DD), 0, 13);
        DSW(0x57E8) = 0xFFFF;
    }
    ap_realism_changed_606e();
    redraw_all();
    s16 r = 0;
    for (u16 i = 0; i < 0x20; i++) {
        if (DSB(old + i) != DSB(AP_DS_realism_prf + i)) { r = (s16)(DSB(old + i) - DSB(AP_DS_realism_prf + i)); break; }
    }
    stk_release(m);
    return r;
}

/* AP 606e:01e6 flight_message(s) — draw, wait 30·13 ticks or a key, full redraw (the message itself
 * is shown by the caller) */
void flight_message(u16 s_ds)
{
    (void)s_ds;
    ap_draw_frame(DSS(AP_DS_redraw_pages), 1);
    delay18(0x1E);
    redraw_all();
}

/* AP 606e:0208 key_or_click() -> 1 on a key or a mouse button (input +23h bits 5, 6) */
s16 key_or_click(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x26);
    input_poll(in);
    s16 r = DSB(in + 0x1B) != 0 || (DSB(in + 0x23) & 0x20) || (DSB(in + 0x23) & 0x40);
    stk_release(m);
    return r;
}

/* AP 606e:0243 wait_key_release() */
void wait_key_release(void)
{
    u16 m = stk_mark();
    u16 in = stk_alloc(0x26);
    for (;;) {
        u16 r = input_poll(in);
        if (DSB(r + 0x1B) == 0) break;
        host_pump();
    }
    stk_release(m);
}

/* AP 606e:025f banner_text(s): right-aligned at y 5Eh in font 4, colour CFh */
void banner_text(u16 s_ds)
{
    DSB(0x26C0) = 0xCF;
    DSB(0x26C2) = 1;
    gfx_font_select(DSS(0x3F32));
    s16 w = gfx_text_width(s_ds);
    gfx_text(s_ds, (s16)(DSS(0x2DAA) - w - 0x14), 0x5E);
}

/* AP 606e:0396 banner_w(s) = banner 606e:029d (video/palette_fx.c) */
void banner_w(u16 s_ds)
{
    vcr_message(s_ds);
}

__attribute__((constructor)) static void vcr_panel_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x5EA50000u, (CodeFn)panel_cb_0000);
    codeptr_register(PROG_AP, 0x5EA50020u, panel_cb_0020);
    codeptr_register(PROG_AP, 0x5EA5002Au, panel_cb_slider);
    codeptr_register(PROG_AP, 0x5EA5005Cu, panel_cb_click1);
    codeptr_register(PROG_AP, 0x5EA50079u, panel_cb_click2);
    codeptr_register(PROG_AP, 0x5EA5009Bu, panel_cb_click3);
    codeptr_register(PROG_AP, 0x5EA50123u, panel_draw_time);
    codeptr_register(PROG_AP, 0x5EA5017Fu, panel_draw_lamp);
    codeptr_register(PROG_AP, 0x5EA5047Au, panel_draw_frame);
    codeptr_register(PROG_AP, 0x5EA507DFu, panel_draw_title);
    codeptr_register(PROG_AP, 0x5EA50839u, panel_cb_click4);
}
