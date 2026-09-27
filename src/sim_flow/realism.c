/* sim_flow: realism.c — the realism settings and the score multiplier (522c), the grace timer
 * (6e9b), the in-flight realism dialog simreal.BOX (6e9f: 36 items at far 39A4:0015) —
 * sim_flow_ui.md §3. DS:42C8 = the 16 realism bytes, DS:4298 the credited baseline, DS:42A8 the
 * "credited" flags, DS:42B8 the "not changeable" flags, DS:4294 the grace timer (u32),
 * DS:0636 the weights, DS:5488 the bound settings pointer, DS:5478 the copy for RESTORE. */
#include "sim_flow/sfb_priv.h"

#define REAL_SEG 0x39A4
#define RLB(o) SEGB(REAL_SEG, o)
#define RLW(o) SEGW(REAL_SEG, o)
#define RSET() DSW(0x5488)

/* ---- 522c ----------------------------------------------------------------------------------------- */

/* AP 522c:0000 min16(out, a, b): byte-wise signed minimum */
u16 min16(u16 out, u16 a, u16 b)
{
    for (s16 i = 0; i < 0x10; i++) {
        s8 v = DSC(a + i);
        if (DSC(b + i) < v) v = DSC(b + i);
        DSC(out + i) = v;
    }
    return out;
}

/* AP 522c:0038 realism_percent() */
s16 ap_realism_percent(void)
{
    if (DSL(0x4294) != 0) return realism_pct(AP_DS_realism_prf);
    u16 m = stk_mark();
    u16 t = stk_alloc(0x10);
    s16 r = realism_pct(min16(t, 0x4298, AP_DS_realism_prf));
    stk_release(m);
    return r;
}

/* AP 522c:006f realism_pct(r[16]) -> lo + (hi - lo)·s / Σw */
s16 realism_pct(u16 r)
{
    s16 hi, lo;
    if (DSB(r + 7) == 0) { hi = 0x14; lo = 0; }
    else { hi = 100; lo = 0x14; }
    s16 s = 0;
    for (s16 i = 0; i < 0x10; i++) {
        if (DSB(r + i) != 0) {
            if (i == 0x0B || i == 0x0A) s = (s16)(s + ((s16)DSC(0x0636 + i) * (s16)DSC(r + i) >> 1));
            else s = (s16)(s + DSC(0x0636 + i));
        }
    }
    if (DSB(0x477B) == 0) s = (s16)(s + DSC(0x063C));
    return (s16)(lo + (s16)((hi - lo) * s) / realism_weight_sum());
}

/* AP 522c:0100 realism_weight_sum() */
s16 realism_weight_sum(void)
{
    s16 s = 0;
    for (s16 i = 0; i < 0x10; i++) s = (s16)(s + DSC(0x0636 + i));
    return s;
}

/* AP 522c:011c realism_init() (mission start) */
void realism_init(void)
{
    DSL(0x4294) = 39000;
    memset(DSP(0x42B8), 0, 0x10);
    memset(DSP(0x42A8), 0, 0x10);
    DSW(0x576B) = (u16)(s16)DSC(0x42CF);
    if (DSB(0x477B) == 0) {
        DSB(0x42CE) = 0;
        DSB(0x42BE) = 1;
        DSB(0x42AE) = 1;
    }
}

/* AP 522c:0162 realism_changed() */
void ap_realism_changed_522c(void)
{
    if (DSL(0x4294) == 0) min16(0x4298, 0x4298, AP_DS_realism_prf);
    DSW(0x576B) = (u16)(s16)DSC(0x42CF);
}

/* AP 522c:0186 realism_uncredited(i) */
s16 realism_uncredited(s16 i)
{
    if (DSL(0x4294) != 0) return 0;
    return DSB(0x4298 + i) == 0;
}

/* ---- 6e9b ----------------------------------------------------------------------------------------- */

/* AP 6e9b:0000 realism_timer() (sim step): at the end of the grace time the baseline is taken */
void realism_timer(void)
{
    if (DSL(0x4294) == 0) return;
    if (timer32_expired(0x4294)) {
        memcpy(DSP(0x4298), DSP(AP_DS_realism_prf), 0x10);
        memset(DSP(0x42A8), 1, 0x10);
    }
}

/* ---- 6e9f: the realism dialog --------------------------------------------------------------------- */

/* AP 6e9f:0000 realism_dlg_init(): simreal.BOX into 39A4:0015 */
void realism_dlg_init(void)
{
    box_load(far_make(SEG(REAL_SEG), 0x15), res_name(0x123E /* "simreal" */, 0x05C0 /* "BOX" */), 0x24);
}

/* AP 6e9f:0024 realism_labels() (near): flight model / combat level button frames */
void realism_labels(void)
{
    RLW(0x02A1) = (u16)((s16)DSC(RSET() + 0x0B) + 0x49);
    RLB(0x029F) = 0x43;
    RLW(0x028B) = (u16)((s16)DSC(RSET() + 0x0A) + 0x4C);
    RLB(0x0289) = 0x43;
    RLB(0x02B5) = 0x43;
}

/* AP 6e9f:0064 realism_label_cb() (near): the lamps (items 18..29) from the settings */
void realism_label_cb(void)
{
    for (u16 o = 0x99; o != 0x18B; o = (u16)(o + 0x16)) {
        u16 v = DSB(RSET() + (RLW(o + 0x10) & 0xDFFF)) != 0;
        RLW(o + 0x108 + 0x10) = v;
        RLB(o + 0x108 + 0x0C) = 0x43;
    }
    realism_labels();
}
/* AP 6e9f:0065 */
void realism_label_cb_body(void) { realism_label_cb(); }

/* AP 6e9f:00ba realism_preset() — callback: EASY / STANDARD / HARD on the changeable items */
void realism_preset(FarPtr unused)
{
    (void)unused;
    if (!toggle_press(far_make(0, 0))) return;
    u16 si = RSET();
    u16 di = (u16)(((IT_W(CUR_ITEM(), 0x10) & 0xDFFF) << 4) + 0x120E);
    for (s16 i = 0; i < 0x10; i++, si++, di++)
        if (DSB(0x42B8 + i) == 0) DSB(si) = DSB(di);
    realism_label_cb();
    if (view_get() == 0) DSW(0x3BC1) = 0x0E;
}

/* AP 6e9f:0112 realism_preset_cb(i) -> released (near): toggles of credited items look pressed */
u16 realism_preset_cb(s16 i)
{
    if (i > -1 && DSB(0x42A8 + i) != 0) button_bitmap(far_make(0, 0));
    else button_press(0);
    return STATE & 0x10;
}
/* AP 6e9f:0113 */
u16 realism_preset_cb_body(s16 i) { return realism_preset_cb(i); }

/* AP 6e9f:0141 realism_toggle() — callback: flip a setting unless it is fixed */
void realism_toggle(FarPtr unused)
{
    (void)unused;
    FarPtr it = CUR_ITEM();
    u16 i = IT_W(it, 0x10) & 0xDFFF;
    FarPtr lamp = far_make(it.seg, (u16)(it.off + 0x108));
    if (!realism_preset_cb((s16)i)) return;
    if (DSB(0x42B8 + i) == 0) {
        u16 p = (u16)(RSET() + i);
        DSB(p) = DSB(p) == 0 ? 1 : 0;
        wr16(lamp.seg, (u16)(lamp.off + 0x10), (u16)(s16)DSC(p));
        if (i == 0) DSW(0x3BC1) = 0x0E;
        RLB(0x02B5) = 0x43;
    }
    wr8(lamp.seg, (u16)(lamp.off + 0x0C), 0x43);
}

/* AP 6e9f:01b5 realism_cb(v, n) (near) -> (v + 1) % n */
s16 realism_cb(s16 v, s16 n)
{
    return (s16)((v + 1) % n);
}
/* AP 6e9f:01b6 */
s16 realism_cb_body(s16 v, s16 n) { return realism_cb(v, n); }

/* AP 6e9f:01c6 realism_cycle_fm() — callback: flight model easy / standard / hard */
void realism_cycle_fm(FarPtr unused)
{
    (void)unused;
    if (!realism_preset_cb(-1)) return;
    DSB(RSET() + 0x0B) = (u8)realism_cb((s16)DSC(RSET() + 0x0B), 3);
    realism_labels();
}

/* AP 6e9f:01f2 realism_cycle_combat() — callback: combat level */
void realism_cycle_combat(FarPtr unused)
{
    (void)unused;
    if (!realism_preset_cb(-1)) return;
    DSB(RSET() + 0x0A) = (u8)realism_cb((s16)DSC(RSET() + 0x0A), 3);
    realism_labels();
}

/* AP 6e9f:021e realism_accept() — callback: ACCEPT -> result 9 */
void realism_accept(FarPtr unused)
{
    (void)unused;
    if (toggle_press(far_make(0, 0))) DSW(0x3BC1) = 9;
}

/* AP 6e9f:0232 realism_bind(r) */
void realism_bind(u16 r_ds)
{
    DSW(0x5488) = r_ds;
}

/* AP 6e9f:023d realism_restore() — callback: RESTORE (back to the copy) */
void realism_restore(FarPtr unused)
{
    (void)unused;
    if (!toggle_press(far_make(0, 0))) return;
    memcpy(DSP(RSET()), DSP(0x5478), 0x10);
    realism_label_cb();
    if (view_get() == 0) DSW(0x3BC1) = 0x0E;
}

/* AP 6e9f:026f realism_cb_frame() — callback: lamp (inset box, lit bitmap when +10h) */
void realism_cb_frame(FarPtr unused)
{
    (void)unused;
    inset_box_item(far_make(0, 0));
    if (IT_W(CUR_ITEM(), 0x10) != 0) gfx_sprite_flip(slider_bitmaps(), (s16)(RX + 2), (s16)(RY + 2), 0);
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
}

/* AP 6e9f:02ab realism_title() — callback: the realism percentage */
void realism_title(FarPtr unused)
{
    (void)unused;
    u16 s = itoa_static(ap_realism_percent());
    FarPtr it = CUR_ITEM();
    wr16(it.seg, (u16)(it.off + 0x0E), s);
    dlg_title_text(far_make(0, 0));
    wr16(it.seg, (u16)(it.off + 0x0E), 0xFFFF);
}

/* AP 6e9f:02d5 realism_open(): keep a copy, refresh the lamps, mark the uncredited items (2000h) */
void realism_open(void)
{
    memcpy(DSP(0x5478), DSP(RSET()), 0x10);
    realism_label_cb();
    u16 o = 0x99 + 0x10;
    for (s16 k = 6; k <= 0x11; k++, o = (u16)(o + 0x16)) {
        if (realism_uncredited((s16)(RLW(o) & 0xDFFF))) RLW(o) |= 0x2000;
        else RLW(o) &= 0xDFFF;
    }
}

/* AP 6e9f:033c realism_cb_panel() — callback: framed panel with STR0 4Fh */
void realism_cb_panel(FarPtr unused)
{
    (void)unused;
    framed_panel(far_make(0, 0));
    gfx_font_select(DSS(0x3F2E));
    u16 s = str0(0x4F).off;
    s16 tx = ap_text_center_x(s, RX, RW);
    text_engrave(s, 0xC1, 0xB1, 0xC0, (u16)tx, (u16)(RY + 3));
}

/* AP 6e9f:0394 realism_cb_a() — callback: rivets 1 + 2 */
void realism_cb_a(FarPtr unused)
{
    (void)unused;
    item_frame_bitmap(RX, RY, 1);
    item_frame_bitmap((s16)(RX + 7), RY, 2);
}

/* AP 6e9f:03c0 realism_cb_b() — callback: rivets 2 + 2 */
void realism_cb_b(FarPtr unused)
{
    (void)unused;
    item_frame_bitmap(RX, RY, 2);
    item_frame_bitmap((s16)(RX + 7), RY, 2);
}

/* AP 6e9f:03ec realism_cb_c() — callback: rivets 2 + 1 */
void realism_cb_c(FarPtr unused)
{
    (void)unused;
    item_frame_bitmap(RX, RY, 2);
    item_frame_bitmap((s16)(RX + 7), RY, 1);
}

static void ln(u8 c, s16 x0, s16 y0, s16 x1, s16 y1)
{
    DSB(0x26CE) = c;
    gfx_line(x0, y0, x1, y1);
}

/* AP 6e9f:0418 realism_bar() — callback: the panel background and the groove of the percentage bar */
void realism_bar(FarPtr unused)
{
    (void)unused;
    rect_save();
    RX = (s16)(RX + 2);
    RY = (s16)(RY + 2);
    RW = (s16)(RW - 3);
    RH = (s16)(RH - 0x21);
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xB8;
    DSB(0x26CE) = 0xB8;
    gfx_rect(RX, RY, RW, RH);
    ap_rect_restore(0);
    rect_save();
    RX = (s16)(RX + 0x0D);
    RY = (s16)(RY + RH - 0x20);
    RW = (s16)(RW - 0x19);
    RH = 0x1F;
    DSB(0x26CC) = 1;
    DSB(0x26CD) = 0xB8;
    DSB(0x26CE) = 0xB8;
    gfx_rect(RX, RY, RW, 0x1F);
    ap_rect_restore(0);
    s16 x = RX, y = RY, xr = (s16)(RX + RW), yb = (s16)(RY + RH);
    ln(0xC2, x, y, xr, y);
    ln(0xB5, (s16)(x + 1), (s16)(y + 1), (s16)(xr - 1), (s16)(y + 1));
    DSB(0x26CE) = 0xC0;
    gfx_line(x, (s16)(y + 1), x, (s16)(yb - 0x1F));
    gfx_line(xr, (s16)(y + 1), xr, (s16)(yb - 0x1F));
    gfx_line((s16)(x + 0x0B), (s16)(yb - 0x1E), (s16)(x + 0x0B), yb);
    gfx_line((s16)(xr - 0x0B), (s16)(yb - 0x1E), (s16)(xr - 0x0B), yb);
    DSB(0x26CE) = 0xBF;
    gfx_line((s16)(x + 1), (s16)(y + 2), (s16)(x + 1), (s16)(yb - 0x1F));
    gfx_line((s16)(xr - 1), (s16)(y + 2), (s16)(xr - 1), (s16)(yb - 0x1F));
    gfx_line((s16)(x + 0x0C), (s16)(yb - 0x1E), (s16)(x + 0x0C), yb);
    gfx_line((s16)(xr - 0x0C), (s16)(yb - 0x1E), (s16)(xr - 0x0C), yb);
    DSB(0x26CE) = 0xC4;
    gfx_line((s16)(x + 0x0B), yb, (s16)(xr - 0x0B), yb);
    gfx_line(x, (s16)(yb - 0x1D), (s16)(x + 0x0A), (s16)(yb - 0x1D));
    gfx_line(xr, (s16)(yb - 0x1D), (s16)(xr - 0x0A), (s16)(yb - 0x1D));
    DSB(0x26CE) = 0xC1;
    gfx_line((s16)(x + 0x0B), (s16)(yb - 1), (s16)(xr - 0x0B), (s16)(yb - 1));
    gfx_line(x, (s16)(yb - 0x1E), (s16)(x + 0x0B), (s16)(yb - 0x1E));
    gfx_line(xr, (s16)(yb - 0x1E), (s16)(xr - 0x0B), (s16)(yb - 0x1E));
    DSB(0x26CE) = 0xC0;
    gfx_line((s16)(x + 0x0B), (s16)(yb - 2), (s16)(xr - 0x0B), (s16)(yb - 2));
    gfx_line((s16)(x + 2), (s16)(yb - 0x1F), (s16)(x + 0x0C), (s16)(yb - 0x1F));
    gfx_line((s16)(xr - 2), (s16)(yb - 0x1F), (s16)(xr - 0x0C), (s16)(yb - 0x1F));
}

/* AP 6e9f:078a realism_cb_d() — callback: the large frame and two rivet columns */
void realism_cb_d(FarPtr unused)
{
    (void)unused;
    dlg_frame(far_make(0, 0));
    item_frame_bitmap((s16)(RX + 2), (s16)(RY + 0x18), 0x16);
    item_frame_bitmap((s16)(RX + RW - 5), (s16)(RY + 0x18), 0x16);
}

__attribute__((constructor)) static void realism_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x6E9F00BAu, (CodeFn)realism_preset);
    codeptr_register(PROG_AP, 0x6E9F0141u, (CodeFn)realism_toggle);
    codeptr_register(PROG_AP, 0x6E9F01C6u, (CodeFn)realism_cycle_fm);
    codeptr_register(PROG_AP, 0x6E9F01F2u, (CodeFn)realism_cycle_combat);
    codeptr_register(PROG_AP, 0x6E9F021Eu, (CodeFn)realism_accept);
    codeptr_register(PROG_AP, 0x6E9F023Du, (CodeFn)realism_restore);
    codeptr_register(PROG_AP, 0x6E9F026Fu, (CodeFn)realism_cb_frame);
    codeptr_register(PROG_AP, 0x6E9F02ABu, (CodeFn)realism_title);
    codeptr_register(PROG_AP, 0x6E9F033Cu, (CodeFn)realism_cb_panel);
    codeptr_register(PROG_AP, 0x6E9F0394u, (CodeFn)realism_cb_a);
    codeptr_register(PROG_AP, 0x6E9F03C0u, (CodeFn)realism_cb_b);
    codeptr_register(PROG_AP, 0x6E9F03ECu, (CodeFn)realism_cb_c);
    codeptr_register(PROG_AP, 0x6E9F0418u, (CodeFn)realism_bar);
    codeptr_register(PROG_AP, 0x6E9F078Au, (CodeFn)realism_cb_d);
}
