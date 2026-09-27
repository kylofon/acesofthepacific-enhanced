/* sim_flow: stdalert.c — the alert builder and the quit box (5a01), the end-of-mission prompts
 * (5da1), the mission summary text and the id-list alerts (6210) — sim_flow_ui.md §2.
 * The alert item table is at far 38BF:0016: item 0 the frame (callback 47a8:0BA3), item 1 the text
 * lines (5a01:005D, list DS:4890 / count DS:4892), items 2.. the buttons (5a01:0037). */
#include "sim_flow/sfb_priv.h"

#define ALERT_SEG 0x38BF
#define AW(o) SEGW(ALERT_SEG, o)
#define AB(o) SEGB(ALERT_SEG, o)

/* ---- 5a01 ----------------------------------------------------------------------------------------- */

/* AP 5a01:0000 alert_btn_cb(): the button's value (near pointer to a far string pointer) becomes the
 * near string; the pointer is kept in DS:488E */
void alert_btn_cb(void)
{
    FarPtr it = CUR_ITEM();
    DSW(0x488E) = IT_W(it, 0x0E);
    wr16(it.seg, (u16)(it.off + 0x0E), far_to_near_str(ds_far(DSW(0x488E))));
}

/* AP 5a01:0027 alert_text_cb(): restore the button's value */
void alert_text_cb(void)
{
    FarPtr it = CUR_ITEM();
    wr16(it.seg, (u16)(it.off + 0x0E), DSW(0x488E));
}

/* AP 5a01:0037 alert_btn_press() — button callback: result = the button's code (+10h) */
void alert_btn_press(void)
{
    alert_btn_cb();
    if (button_press(0)) DSW(0x3BC1) = IT_W(CUR_ITEM(), 0x10);
    alert_text_cb();
}

/* AP 5a01:005d quit_box_draw() — text callback: framed panel and the engraved lines DS:4890 */
void quit_box_draw(void)
{
    framed_panel(far_make(0, 0));
    gfx_font_select(DSS(0x3F2E));
    s16 fh = gfx_font_height(DSS(0x3F2E));
    for (s16 i = 0; i < DSS(0x4892); i++) {
        u16 s = far_to_near_str(ds_far((u16)(DSW(0x4890) + 4 * i)));
        s16 tx = ap_text_center_x(s, RX, RW);
        text_engrave(s, 0xC1, 0xB1, 0xC0, (u16)tx, (u16)(RY + i * fh + 8));
    }
}

/* AP 5a01:00e9 alert_build(texts, buttons, codes) -> item count (lists of far string pointers, 0-ended;
 * at most 6 buttons) */
s16 alert_build(u16 texts_ds, u16 buttons_ds, u16 codes_ds)
{
    s16 nt = 0, nb = 0, bw = 0;
    gfx_font_select(DSS(0x3F2E));
    for (u16 p = buttons_ds; (DSW(p) != 0 || DSW(p + 2) != 0) && nb < 6; p = (u16)(p + 4)) {
        s16 w = far_text_width(ds_far(p));
        if (w > bw) bw = w;
        nb++;
    }
    s16 cw = gfx_font_width(DSS(0x3F2E));
    bw = (s16)(cw * 5 < bw + 0x0C ? bw + 0x0C : gfx_font_width(DSS(0x3F2E)) * 5);
    s16 tw = bw;
    gfx_font_select(DSS(0x3F2E));
    for (u16 p = texts_ds; DSW(p) != 0 || DSW(p + 2) != 0; p = (u16)(p + 4)) {
        s16 w = far_text_width(ds_far(p));
        if (w > tw) tw = w;
        nt++;
    }
    cw = gfx_font_width(DSS(0x3F2E));
    tw = (s16)(cw * 7 < tw + 0x0C ? tw + 0x0C : gfx_font_width(DSS(0x3F2E)) * 7);
    AW(0x30) = (u16)tw;
    AW(0x32) = (u16)(nt * gfx_font_height(DSS(0x3F2E)) + 0x10);
    AW(0x2C) = 0x0A;
    AW(0x2E) = 5;
    AW(0x36) = SEG(0x3D6A);                               /* text callback 3D6A:002A -> 5a01:005D */
    AW(0x34) = 0x2A;
    AB(0x38) = 0x40;
    AB(0x39) = 0;
    AW(0x1A) = (u16)(AW(0x30) + 0x13);
    AW(0x1C) = (u16)(AW(0x32) + nb * 0x12 + 9);
    AW(0x16) = (u16)((s16)(0x140 - AW(0x1A)) >> 1);
    AW(0x18) = (u16)((s16)(0xC8 - AW(0x1C)) >> 1);
    AW(0x20) = SEG(0x3C6D);                               /* frame callback 3C6D:009D -> 47a8:0BA3 */
    AW(0x1E) = 0x9D;
    AB(0x22) = 0x40;
    AB(0x23) = 0;
    AW(0x24) = 0x0866;
    DSW(0x4890) = texts_ds;
    DSW(0x4892) = (u16)nt;
    u16 it = 0x42, dy = 0, dy2 = 0, bp_ = buttons_ds, cp = codes_ds;
    for (s16 i = 0; i < nb; i++) {
        AW(it + 4) = (u16)bw;
        AW(it + 6) = 0x0D;
        AW(it) = (u16)((s16)(AW(0x1A) - AW(it + 4)) >> 1);
        AW(it + 2) = (u16)(AW(0x2E) + AW(0x32) + dy + dy2 + 5);
        AW(it + 0x0A) = SEG(0x3D6A);                      /* button callback 3D6A:0025 -> 5a01:0037 */
        AW(it + 8) = 0x25;
        AB(it + 0x0C) = 0x74;
        AB(it + 0x0D) = 2;
        AW(it + 0x0E) = bp_;
        AW(it + 0x10) = DSW(cp);
        dy = (u16)(dy + 5);
        dy2 = (u16)(dy2 + 0x0D);
        bp_ = (u16)(bp_ + 4);
        cp = (u16)(cp + 2);
        it = (u16)(it + 0x16);
    }
    return (s16)(nb + 2);
}

/* AP 5a01:0395 quit_box(texts) -> far handle {+0 saved, +2 x, +4 y, +6 w, +8 h, +0A background}: the
 * modal box, drawn on the front page */
u32 quit_box(u16 texts_ds)
{
    s16 n = 0, tw = 0;
    gfx_font_select(DSS(0x3F2E));
    for (u16 p = texts_ds; DSW(p) != 0 || DSW(p + 2) != 0; p = (u16)(p + 4)) {
        s16 w = far_text_width(ds_far(p));
        if (w > tw) tw = w;
        n++;
    }
    s16 cw = gfx_font_width(DSS(0x3F2E));
    tw = (s16)(cw * 7 < tw + 0x0C ? tw + 0x0C : gfx_font_width(DSS(0x3F2E)) * 7);
    RW = tw;
    RH = (s16)(n * gfx_font_height(DSS(0x3F2E)) + 0x10);
    RX = (s16)((0x140 - RW) >> 1);
    RY = (s16)((0xC8 - RH) >> 1);
    u16 sz = (u16)gfx_rect_size((u16)RW, (u16)RH);
    FarPtr h = mem_far_new((u32)sz + 8, 0);
    if (!far_is_null(h)) {
        wr16(h.seg, h.off, 1);
    } else {
        h = mem_far_new(0x0A, 0);
        if (far_is_null(h)) return 0;
        wr16(h.seg, h.off, 0);
    }
    u16 c0 = DSW(0x26C4), c1 = DSW(0x26C6), c2 = DSW(0x26C8), c3 = DSW(0x26CA);
    DSW(0x26C8) = 0;
    DSW(0x26C4) = 0;
    DSW(0x26C6) = 0x13F;
    DSW(0x26CA) = 0xC7;
    if (rd16(h.seg, h.off) != 0) {
        DSW(0x26D6) = DSW(0x26D4);
        gfx_get_rect(far_norm(far_lin(h) + 0x0A), RX, RY, RW, RH);
    }
    wr16(h.seg, (u16)(h.off + 2), (u16)RX);
    wr16(h.seg, (u16)(h.off + 4), (u16)RY);
    wr16(h.seg, (u16)(h.off + 6), (u16)RW);
    wr16(h.seg, (u16)(h.off + 8), (u16)RH);
    u16 sn = DSW(0x4892), sl = DSW(0x4890);
    DSW(0x4892) = (u16)n;
    DSW(0x4890) = texts_ds;
    DSW(0x26D8) = DSW(0x26D4);
    quit_box_draw();
    DSW(0x4892) = sn;
    DSW(0x4890) = sl;
    DSW(0x26C4) = c0;
    DSW(0x26C6) = c1;
    DSW(0x26C8) = c2;
    DSW(0x26CA) = c3;
    return (u32)h.seg << 16 | h.off;
}

/* AP 5a01:05d5 quit_box_close(h): restore the background (or fill colour 1) and free */
void quit_box_close(u32 hh)
{
    FarPtr h = far_make((u16)(hh >> 16), (u16)hh);
    u16 c0 = DSW(0x26C4), c1 = DSW(0x26C6), c2 = DSW(0x26C8), c3 = DSW(0x26CA);
    if (!far_is_null(h)) {
        DSW(0x26C8) = 0;
        DSW(0x26C4) = 0;
        DSW(0x26C6) = 0x13F;
        DSW(0x26CA) = 0xC7;
        DSW(0x26D8) = DSW(0x26D4);
        s16 x = (s16)rd16(h.seg, (u16)(h.off + 2)), y = (s16)rd16(h.seg, (u16)(h.off + 4));
        s16 w = (s16)rd16(h.seg, (u16)(h.off + 6)), hgt = (s16)rd16(h.seg, (u16)(h.off + 8));
        if (rd16(h.seg, h.off) == 0) {
            DSB(0x26CE) = 1;
            DSB(0x26CD) = 1;
            DSB(0x26CC) = 1;
            gfx_rect(x, y, w, hgt);
        } else {
            gfx_put_rect(far_norm(far_lin(h) + 0x0A), x, y, w, hgt);
        }
        mem_far_delete(h);
    }
    DSW(0x26C4) = c0;
    DSW(0x26C6) = c1;
    DSW(0x26C8) = c2;
    DSW(0x26CA) = c3;
}

/* ---- 5da1: end requests and the end prompt ------------------------------------------------------- */

/* AP 5da1:0000 end_request(code): the first request wins */
void end_request(u8 code)
{
    if (DSB(AP_DS_end_alert_state) == 0) DSB(AP_DS_end_alert_state) = code;
}

/* AP 5da1:0012 end_alert(texts, buttons, codes) -> 5 / 6 / 7 / 8 */
s16 end_alert(u16 texts_ds, u16 buttons_ds, u16 codes_ds)
{
    s16 n = alert_build(texts_ds, buttons_ds, codes_ds);
    dlg_activate(far_make(SEG(ALERT_SEG), 0x16), n);
    dlg_force_redraw();
    for (;;) {
        switch (dlg_poll()) {
        case 0x0A: redraw_all(); return 5;
        case 0x0C: redraw_all(); return 7;
        case 0x09: redraw_all(); return 6;
        case 0x10: redraw_all(); return 8;
        default: break;
        }
    }
}

/* AP 5da1:0092 str_list(out, ids): far pointers to STR group 0 texts, until -1; then 0 */
void str_list(u16 out_ds, u16 ids_ds)
{
    for (;;) {
        s16 id = DSS(ids_ds);
        if (id == -1) break;
        FarPtr s = str(0, id);
        DSW(out_ds + 2) = s.seg;
        DSW(out_ds) = s.off;
        out_ds = (u16)(out_ds + 4);
        ids_ds = (u16)(ids_ds + 2);
    }
    DSL(out_ds) = 0;
}

/* AP 5da1:00cb plane_in_trouble(p) (quirk: a single-engine type reads the player's engine) */
s16 plane_in_trouble(u16 p)
{
    if (DSB(0x42CF) == 0) return 0;
    if ((DSB(p + 0xAA) >> 4) & 1) return 1;
    if (DSB(0x0FE8) || DSB(0x0FE9)) return 1;
    if (DSB(0x0FEC)) return 1;
    if (DSB(0x0FEB)) return 1;
    if (DSB(0x0FEA)) return 1;
    if (DSB(p + 0x1A) & 0x40) return 1;
    s16 c;
    if (DSS(DSW(p + 0x66) + 0x90) > 1) c = (s16)((s16)(DSS(p + 0xA4) + DSS(p + 0xA6)) >> 1);
    else c = DSS(DSW(AP_DS_player) + 0xA4);
    return c < 0x32;
}

/* AP 5da1:0165 object_lost(o) — no callers */
s16 object_lost(u16 o)
{
    if (DSB(o + 0x18) == 0) return plane_is_down(o) != 0;
    return (DSB(o + 0x1B) & 2) != 0;
}

/* AP 5da1:0196 in_contact(p) */
s16 in_contact(u16 p)
{
    return DSB(p + 0x1F) & 1;
}

/* AP 5da1:01a8 low_and_diving(p) */
s16 low_and_diving(u16 p)
{
    return DSSL(p + 0x0C) < 4000 && DSS(p + 0x10) < -0x71C;
}

/* AP 5da1:01cb end_danger(p) -> text id list / 0 */
u16 end_danger(u16 p)
{
    if (DSB(AP_DS_end_alert_state) == 2) return 0;
    if (plane_in_trouble(p)) return 0x0A42;
    if (in_contact(p)) return 0x0A48;
    if (low_and_diving(p)) return 0x0A4E;
    if (DSB(0x42CE) != 0 && (u8)can_reach_home(p) == 0) return 0x0A54;
    return 0;
}

/* AP 5da1:022e end_prompt() -> 5 end / 6 continue (Esc) / 7 / 8 end anyway */
s16 end_prompt(void)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(6), texts = stk_alloc(0x28), buttons = stk_alloc(0x28);
    DSW(b) = 0xB9;
    DSW(b + 2) = 0xBB;
    DSW(b + 4) = 0xFFFF;
    u16 codes = 0x0A74, t;
    if (DSB(AP_DS_end_alert_state) == 2 || DSB(AP_DS_end_alert_state) == 3) {
        t = 0x0A6C;
        DSW(b) = 0xBA;
        DSW(b + 2) = 0xFFFF;
        codes = 0x0A78;
    } else if (DSB(0x477C) == 5) {
        t = 0x0A68;
        DSW(b + 2) = 0xBA;
    } else {
        t = end_danger(DSW(AP_DS_player));
        if (t == 0) {
            if (DSB(0x5CC9) == 0 && DSB(AP_DS_end_alert_state) != 2) {
                t = 0x0A5C;
            } else {
                codes = 0x0A70;
                t = 0x0A62;
                DSW(b + 2) = 0xBA;
            }
        }
    }
    str_list(texts, t);
    str_list(buttons, b);
    s16 r = end_alert(texts, buttons, codes);
    stk_release(m);
    return r;
}

/* AP 5da1:02da end_reset() */
void end_reset(void)
{
    DSB(AP_DS_end_alert_state) = 0;
}

/* AP 5da1:02e4 end_init() — empty */
void end_init(void)
{
}

/* ---- 6210: mission summary text, id-list alerts --------------------------------------------------- */

static u16 fs(s16 id) { return str(8, id).off; }

/* printf "%s" of the fate record's far name at +o into buf with STR group 8 format id */
static void fate_sprintf(u16 buf, u16 fate, s16 id, u16 o, u16 tmp, u16 name)
{
    rtl_strcpy(tmp, fs(id));
    if (DSL(fate + o) != 0) rtl__fstrcpy(ds_ptr(name), ds_far((u16)(fate + o)));
    else DSB(name) = 0;
    rtl_sprintf(buf, tmp, name);
}

/* AP 6210:0000 summary_text(buf, fate) -> buf (double-NUL terminated) */
u16 summary_text(u16 buf, u16 fate)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x100), name = stk_alloc(0x100), line = stk_alloc(0x100);
    u16 last = 0;
    s16 si = 0;
    DSB(buf) = 0;
    DSB(buf + 1) = 0;
    u16 code = DSW(fate);
    switch (code & 0x0F) {
    case 1:                                           /* shot down */
        rtl_strcat(buf, fs(0));
        si = (s16)(si + rtl_strlen(fs(0)));
        if ((code & 0xF0) == 0x10) {
            DSB(buf + si) = '\n'; si++;
            DSB(buf + si) = 0; si++;
            fate_sprintf(line, fate, 1, 6, tmp, name);
            rtl_strcat(buf, line);
        }
        break;
    case 2:                                           /* crashed */
        fate_sprintf(buf, fate, 2, 2, tmp, name);
        last = fs(2);
        si = (s16)(si + rtl_strlen(buf));
        DSB(buf + si) = '\n'; si++;
        DSB(buf + si) = 0;
        switch (code & 0xF0) {
        case 0x20: last = fs(3); break;
        case 0x40: last = fs(4); break;
        case 0x50: last = fs(5); break;
        case 0x60: last = fs(6); break;
        default: break;                               /* sic: appends the stale format text */
        }
        rtl_strcat(buf, last);
        break;
    case 3:                                           /* bailed out */
        rtl_strcat(buf, fs(7));
        si = (s16)(si + rtl_strlen(fs(7)));
        DSB(buf + si) = ' '; si++;
        DSB(buf + si) = 0;
        switch (code & 0xF0) {
        case 0x20: rtl_strcat(buf, fs(8)); break;
        case 0x30: rtl_strcat(buf, fs(9)); break;
        case 0x40: rtl_strcat(buf, fs(0x0D)); break;
        case 0x50:
            rtl_strcat(buf, fs(0x0A));
            si = (s16)(si + rtl_strlen(fs(0x0A)));
            DSB(buf + si) = ' '; si++;
            DSB(buf + si) = 0;
            rtl_strcat(buf, fs(0x0B));
            break;
        case 0x60:
            rtl_strcat(buf, fs(0x0A));
            si = (s16)(si + rtl_strlen(fs(0x0A)));
            DSB(buf + si) = ' '; si++;
            DSB(buf + si) = 0;
            rtl_strcat(buf, fs(0x0C));
            break;
        default: break;
        }
        break;
    case 4:                                           /* returned */
        switch (code & 0xF0) {
        case 0x40: fate_sprintf(buf, fate, 0x0E, 2, tmp, name); break;
        case 0x50: fate_sprintf(buf, fate, 0x0F, 2, tmp, name); break;
        case 0x60: fate_sprintf(buf, fate, 0x10, 2, tmp, name); break;
        case 0x70: rtl_strcpy(buf, fs(0x11)); break;
        case 0x80: rtl_strcpy(buf, fs(0x12)); break;
        default: break;
        }
        break;
    default:
        break;
    }
    DSB(buf + rtl_strlen(buf) + 1) = 0;
    stk_release(m);
    return buf;
}

/* AP 6210:05e0 results_text(ids, buf): wrapped summary lines as ids 1..n (DS:50CA[id]), then 5Dh, 0 */
void results_text(u16 ids_ds, u16 buf_ds)
{
    u16 m = stk_mark();
    u16 fate = stk_alloc(0x0A);
    fate_record(fate);
    summary_text(buf_ds, fate);
    s16 n = word_wrap(buf_ds, 200);
    s16 i = 0;
    u16 p = buf_ds;
    for (; i < n; i++) {
        DSW(ids_ds + 2 * i) = (u16)(i + 1);
        DSW(0x50CE + 4 * i + 2) = cur_dgroup;
        DSW(0x50CE + 4 * i) = p;
        p = (u16)(p + rtl_strlen(p) + 1);
    }
    DSW(ids_ds + 2 * i) = 0x5D;
    DSW(ids_ds + 2 * i + 2) = 0;
    stk_release(m);
}

/* the far-pointer list of an id list: ids 1..20 are the summary lines DS:50CA[id], others STR group 9 */
static void id_list(u16 out, FarPtr ids)
{
    s16 k = 0;
    for (; rd16(ids.seg, ids.off) != 0; ids.off = (u16)(ids.off + 2), k++) {
        s16 id = (s16)rd16(ids.seg, ids.off);
        FarPtr s = (id < 1 || id > 0x14) ? str(9, id) : ds_far((u16)(0x50CA + id * 4));
        DSW(out + 4 * k) = s.off;
        DSW(out + 4 * k + 2) = s.seg;
    }
    DSL(out + 4 * k) = 0;
}

/* AP 6210:0675 alert_ids(far texts ids, far button ids) -> button code 1..5, 0 for Esc */
s16 alert_ids(FarPtr texts, FarPtr buttons)
{
    u16 m = stk_mark();
    u16 bl = stk_alloc(0x2C), tl = stk_alloc(0x54);
    id_list(tl, texts);
    id_list(bl, buttons);
    s16 n = alert_build(tl, bl, 0x0C12);
    dlg_activate(far_make(SEG(ALERT_SEG), 0x16), n);
    dlg_force_redraw();
    s16 r;
    for (;;) {
        r = dlg_poll();
        if (r == 9) { r = 0; break; }
        if (r >= 1 && r <= 5) break;
    }
    stk_release(m);
    return r;
}

/* AP 6210:07a8 alert_modal(far texts ids, far button ids) -> button code / 0 */
s16 alert_modal(FarPtr texts, FarPtr buttons)
{
    modal_enter();
    DSB(0x3BB8) = 1;
    s16 r = alert_ids(texts, buttons);
    vcr_refresh_screen(1);
    wipe_in(2);
    modal_leave();
    dlg_force_redraw();
    return r;
}

__attribute__((constructor)) static void stdalert_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x5A010037u, alert_btn_press);
    codeptr_register(PROG_AP, 0x5A01005Du, quit_box_draw);
}
