/* sim_flow: sliders.c — the sliders (5613 slider.c), the IFF screenshot writer (565f), the .BXL box
 * lists (48af boxlist.c) and the cockpit exit (6b3d, 6de0) — sim_flow_ui.md §1, §7.
 * Slider records DS:073C + 20h·i: +0 item id, +2 label (STR0 id, -1 none), +4/+6/+8 frames of the left
 * end, right end, knob in the bitmap set DS:44F6, +0A s8 label y, +0B/+0C left end x/y, +0D/+0E right
 * end x/y, +0F knob y, +10 knob x origin, +11 right margin, +12 lo, +14 hi, +16 last position, +18
 * position, +1A drawn position, +1C far knob save-under buffer. */
#include "sim_flow/sfb_priv.h"

#define SL(i) ((u16)(0x073C + 0x20 * (i)))

/* ---- 48af: .BXL box lists ------------------------------------------------------------------------ */

/* AP 48af:0000 bxl_table(n): DS:1160 = n pointers (8 bytes each: arena) */
void bxl_table(s16 n)
{
    DSW(0x1160) = arena_alloc((u16)(n << 3));
}

/* AP 48af:0017 bxl_load_slot(slot, name): {u8 count, u8 flags, 4 bytes, near boxes} */
void bxl_load_slot(s16 slot, u16 name_ds)
{
    u16 f = my_fopen(name_ds, 0x00FA /* "rb" */);
    if (!f) return;
    u16 r = arena_alloc(8);
    DSB(r) = (u8)my_fgetc(f);
    DSB(r + 1) = (u8)my_fgetc(f);
    my_fread((u16)(r + 2), 4, 1, f);
    DSW(r + 6) = arena_alloc((u16)((s16)DSC(r) * 0x0C));
    my_fread(DSW(r + 6), 0x0C, (s16)DSC(r), f);
    my_fclose(f);
    DSW(DSW(0x1160) + slot * 2) = r;
}

/* AP 48af:00a8 bxl_load(name) -> record / 0 (a near pointer in AX; the header keeps the FarPtr form
 * callers use: .off is the record) */
FarPtr bxl_load(u16 name_ds)
{
    u16 f = my_fopen(name_ds, 0x00FD /* "rb" */);
    if (!f) return far_make(0, 0);
    u16 r = mem_new(8, 0, far_to_near_str(far_make(SEG(0x3834), 0)), 0x7B);
    DSB(r) = (u8)my_fgetc(f);
    DSB(r + 1) = (u8)my_fgetc(f);
    my_fread((u16)(r + 2), 4, 1, f);
    DSW(r + 6) = mem_new((u16)((s16)DSC(r) * 0x0C), 0, far_to_near_str(far_make(SEG(0x3834), 0)), 0x80);
    my_fread(DSW(r + 6), 0x0C, (s16)DSC(r), f);
    my_fclose(f);
    return far_make(cur_dgroup, r);
}

/* AP 48af:015a bxl_get(slot, &out) */
void bxl_get(s16 slot, u16 out_ds)
{
    DSW(out_ds) = DSW(DSW(0x1160) + slot * 2);
}

/* ---- 5613: sliders -------------------------------------------------------------------------------- */

/* AP 5613:0000 slider_alloc(i) -> ok: the knob save-under buffer */
s16 slider_alloc(s16 i)
{
    u16 knob = DSW(DSW(0x44F6));                      /* frame 0 of the set */
    u32 n = gfx_rect_size(DSW(knob + 6), DSW(knob + 8));
    FarPtr p = mem_far_new((u32)(u16)n, 0);
    ds_far_wr((u16)(SL(i) + 0x1C), p);
    if (far_is_null(p) && assert_fail(far_make(SEG(0x38A6), 0), 0x5B, 0) == 0) return 0;
    return 1;
}

/* AP 5613:0068 sliders_load(name) */
void sliders_load(u16 name_ds)
{
    DSW(0x44F6) = bmp_load_planes(name_ds);
    if (DSW(0x44F6) == 0) assert_fail(far_make(SEG(0x38A6), 0), 0x63, 0);
    for (s16 i = 0; i < 4; i++) slider_alloc(i);
    DSW(0x074E) = 0x50;                               /* lo(0) = 80 */
    DSW(0x0750) = 0x14;                               /* hi(0) = 20 */
}

/* AP 5613:00bc slider_map(v, a0, a1, b0, b1): rounds away from b0 when the value range is narrower */
s16 slider_map(s16 v, s16 a0, s16 a1, s16 b0, s16 b1)
{
    u16 da = (u16)(a1 - a0), db = (u16)(b1 - b0);
    s16 ada = da == 0x8000 ? 0x7FFF : ((s16)da < 0 ? (s16)-(s16)da : (s16)da);
    s16 adb = db == 0x8000 ? 0x7FFF : ((s16)db < 0 ? (s16)-(s16)db : (s16)db);
    if (adb < ada) {
        s16 d = (s16)(ada - 1);
        if ((s16)db < 0) d = (s16)-d;
        return (s16)(ldiv_sat((s32)(s16)(v - a0) * (s32)(s16)db + d, (s16)da) + b0);
    }
    return lerp_map(v, a0, a1, b0, b1);
}

/* AP 5613:0155 slider_bitmaps() -> DS:44F6->+0E (frame 7 of the set, a near frame pointer) */
u16 slider_bitmaps(void)
{
    return DSW(DSW(0x44F6) + 0x0E);
}

/* AP 5613:0161 slider_value(i) */
s16 slider_value(s16 i)
{
    return slider_map(DSS(SL(i) + 0x18), 0, DSS(SL(i) + 0x16), DSS(SL(i) + 0x12), DSS(SL(i) + 0x14));
}

/* AP 5613:0196 slider_drawn_value(i) — no callers */
s16 slider_drawn_value(s16 i)
{
    return slider_map(DSS(SL(i) + 0x1A), 0, DSS(SL(i) + 0x16), DSS(SL(i) + 0x12), DSS(SL(i) + 0x14));
}

/* AP 5613:01cb slider_lo(i) */
s16 slider_lo(s16 i)
{
    return DSS(SL(i) + 0x12);
}

/* AP 5613:01da slider_hi(i) */
s16 slider_hi(s16 i)
{
    return DSS(SL(i) + 0x14);
}

/* AP 5613:01e9 slider_set(i, v) -> position changed */
s16 slider_set(s16 i, s16 v)
{
    s16 old = DSS(SL(i) + 0x18);
    DSS(SL(i) + 0x18) = slider_map(v, DSS(SL(i) + 0x12), DSS(SL(i) + 0x14), 0, DSS(SL(i) + 0x16));
    return DSS(SL(i) + 0x18) != old;
}

/* AP 5613:0247 slider_settled(i) */
s16 slider_settled(s16 i)
{
    return DSS(SL(i) + 0x18) == DSS(SL(i) + 0x1A);
}

/* AP 5613:026a slider_range(i, lo, hi) */
void slider_range(s16 i, s16 lo, s16 hi)
{
    DSS(SL(i) + 0x12) = lo;
    DSS(SL(i) + 0x14) = hi;
}

/* AP 5613:028a slider_frame(rec): label and both ends */
void slider_frame(u16 r)
{
    if (DSS(r + 2) != -1) {
        u16 s = str0(DSS(r + 2)).off;
        if (s != 0) {
            gfx_font_select(DSS(0x3F2E));
            s16 tx = ap_text_center_x(s, RX, RW);
            text_engrave(s, 0xC1, 0xB1, 0xC0, (u16)tx, (u16)(RY + DSC(r + 0x0A)));
        }
    }
    gfx_sprite_flip(DSW(DSW(0x44F6) + DSW(r + 4) * 2), (s16)(RX + DSC(r + 0x0B)), (s16)(RY + DSC(r + 0x0C)), 0);
    gfx_sprite_flip(DSW(DSW(0x44F6) + DSW(r + 6) * 2), (s16)(RX + DSC(r + 0x0D)), (s16)(RY + DSC(r + 0x0E)), 0);
}

/* AP 5613:034a slider_knob(rec) -> released after a drag */
s16 slider_knob(u16 r)
{
    u16 knob = DSW(DSW(0x44F6) + DSW(r + 8) * 2);
    s16 ky = (s16)(RY + DSC(r + 0x0F));
    s16 x0 = (s16)(RX + DSC(r + 0x10));
    s16 xmax = (s16)(RX + RW - DSS(knob + 6) - DSC(r + 0x11));
    FarPtr it = CUR_ITEM();
    s16 x, released;
    if (!(IT_B(it, 0x0D) & 0x40)) {
        s16 min = (IT_B(it, 0x0D) & 0x10) ? DSS(r + 0x18) : 0;
        if (STATE & 0x1C) x = (s16)(DSS(0x3BBF) - (DSS(knob + 6) >> 1));
        else x = (s16)(DSS(r + 0x18) + x0);
        s16 lo = (s16)(x0 + min);
        if (lo > x) x = lo;
        else if (x > xmax) x = xmax;
        if (STATE & 0x10) { DSS(r + 0x18) = (s16)(x - x0); released = 1; }
        else released = 0;
    } else {
        x = (s16)(x0 + DSS(r + 0x18));
        released = 0;
    }
    if (!(STATE & 0x40)) {
        DSW(0x26D8) = DSW(0x26D2);
        gfx_put_rect(ds_far((u16)(r + 0x1C)), (s16)(x0 + DSS(r + 0x1A)), ky, DSS(knob + 6), DSS(knob + 8));
    }
    DSW(0x26D6) = DSW(0x26D2);
    gfx_get_rect(ds_far((u16)(r + 0x1C)), x, ky, DSS(knob + 6), DSS(knob + 8));
    DSS(r + 0x1A) = (s16)(x - x0);
    wr16(it.seg, (u16)(it.off + 0x0E), (u16)x);
    gfx_sprite(knob, x, ky);
    return released;
}

/* AP 5613:0489 slider_draw(i) — the slider item callback body */
void slider_draw(s16 i)
{
    u16 r = SL(i);
    if (STATE & 0x40) slider_frame(r);
    if (slider_knob(r)) DSW(0x3BC1) = DSW(r);
}

/* ---- 565f: IFF ILBM screenshot -------------------------------------------------------------------- */

/* AP 565f:0000 be_write(p, count, size, f): count values of size 1/2/4 bytes, big-endian */
void be_write(u16 p, s16 count, s16 size, u16 f)
{
    while (count-- != 0) {
        if (size == 4) {
            be_write((u16)(p + 2), 1, 2, f);
            be_write(p, 1, 2, f);
        } else if (size == 2) {
            rtl_fwrite((u16)(p + 1), 1, 1, f);
            rtl_fwrite(p, 1, 1, f);
        } else if (size == 1) {
            rtl_fwrite(p, 1, 1, f);
        }
        p = (u16)(p + size);
    }
}

/* AP 565f:008a iff_write(name): FORM ILBM, BMHD 320x200x8 (uncompressed), CMAP, BODY */
void iff_write(u16 name_ds)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50), l = stk_alloc(4), w = stk_alloc(2);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, name_ds);
    u16 f = rtl_fopen(path, 0x07D5 /* "wb" */);
    DSW(0x26D6) = DSW(0x26D4);
    if (f != 0) {
        u8 hi = DSB(0x2DA8) != 0;
        rtl_fwrite(0x07BC, 4, 1, f);                  /* "FORM" */
        DSL(l) = hi ? 0x3EB30 : 0xFD30;
        be_write(l, 1, 4, f);
        rtl_fwrite(0x07C1, 4, 1, f);                  /* "ILBM" */
        rtl_fwrite(0x07C6, 4, 1, f);                  /* "BMHD" */
        DSL(l) = 0x14;
        be_write(l, 1, 4, f);
        DSW(w) = hi ? 0x280 : 0x140; be_write(w, 1, 2, f);
        DSW(w) = hi ? 0x190 : 0xC8; be_write(w, 1, 2, f);
        DSL(l) = 0; be_write(l, 1, 4, f);
        DSL(l) = 0x8000000; be_write(l, 1, 4, f);
        DSL(l) = 0x101; be_write(l, 1, 4, f);
        DSW(w) = 0x140; be_write(w, 1, 2, f);
        DSW(w) = 0xC8; be_write(w, 1, 2, f);
        iff_cmap(f);
        iff_body(f);
        rtl_fclose(f);
    }
    stk_release(m);
}

/* AP 565f:022a iff_cmap(f): the DAC (6-bit -> 8-bit), or the EGA attribute palette */
void iff_cmap(u16 f)
{
    u16 m = stk_mark();
    u16 pal = stk_alloc(0x300), l = stk_alloc(4);
    rtl_fwrite(0x07CB, 1, 4, f);                      /* "CMAP" */
    DSL(l) = 0x300;
    be_write(l, 1, 4, f);
    if (DSB(0x26DF) != 0) {
        dac_read(pal, 0, 0x100);
        for (s16 i = 0; i < 0x300; i++) DSB(pal + i) = (u8)(DSB(pal + i) << 2);
        rtl_fwrite(pal, 0x300, 1, f);
    } else {
        /* PORT: the EGA path reads a local table the original leaves uninitialised (zeros here); it
         * is never reached with the VGA driver */
        memset(DSP(pal), 0, 0x300);
        for (s16 i = 0; i < 0x10; i++) {
            u16 e = (i == 0 && atc_get_reg(0) == 0) ? 15 : (u16)(atc_get_reg((u16)i) & 0x3F);
            rtl_fwrite((u16)(pal + e * 3), 3, 1, f);
        }
        for (s16 i = 0x10; i < 0x100; i++) rtl_fwrite((u16)(pal + 0x30), 3, 1, f);
    }
    stk_release(m);
}

/* AP 565f:030d byterun1(src, n, dst): the 640-wide double-pixel expansion (hi-res path) */
void byterun1(u16 src, s16 n, u16 dst)
{
    while (n != 0) {
        u8 b = DSB(src++);
        u16 v = 0;
        if (b & 0x80) v = 0xC0;
        if (b & 0x40) v |= 0x30;
        if (b & 0x20) v |= 0x0C;
        if (b & 0x10) v |= 0x03;
        if (b & 0x08) v |= 0xC000;
        if (b & 0x04) v |= 0x3000;
        if (b & 0x02) v |= 0x0C00;
        if (b & 0x01) v |= 0x0300;
        DSW(dst) = v;
        dst = (u16)(dst + 2);
        n--;
    }
}

/* AP 565f:0377 iff_body(f): 200 rows read from the front page, split into 8 planes (c2p_row) */
void iff_body(u16 f)
{
    u16 m = stk_mark();
    u16 l = stk_alloc(4);
    DSB(0x26C3) = 0;
    rtl_fwrite(0x07D0, 4, 1, f);                      /* "BODY" */
    DSL(l) = DSB(0x2DA8) ? 0x3E800 : 64000;
    be_write(l, 1, 4, f);
    u16 buf = safe_malloc(0x0A00);
    for (s16 y = 0; y < 200; y++) {
        if (DSB(0x2DA8) == 0) {
            for (s16 x = 0; x < 0x140; x++) DSB(buf + x) = (u8)gfx_get_pixel(x, y);
            c2p_row(buf, (u16)(buf + 0x140));
            rtl_fwrite((u16)(buf + 0x140), 0x140, 1, f);
        } else {
            gfx_get_rect(far_make(cur_dgroup, buf), 0, y, 0x140, 1);
            c2p_row(buf, (u16)(buf + 0x280));
            c2p_row((u16)(buf + 0x140), (u16)(buf + 0x3C0));
            byterun1((u16)(buf + 0x280), 0x140, buf);
            rtl_fwrite(buf, 0x280, 1, f);
            byterun1((u16)(buf + 0x3C0), 0x140, buf);
            rtl_fwrite(buf, 0x280, 1, f);
        }
    }
    lib_free(buf);
    stk_release(m);
}

/* ---- 6b3d / 6de0: cockpit exit -------------------------------------------------------------------- */

/* AP 6b3d:0000 cockpit_leave(): release the current cockpit view (edges, TTM tags) */
void cockpit_leave(void)
{
    switch (DSW(0x3ED6)) {
    case 0:
        edge_unload();
        ttm_run(7);
        break;
    case 1:
        edge_unload();
        ttm_run(0x15);
        if (DSB(0x3ED8)) {
            u32 pl = pal_load(res_name(0x1162 /* "ACES" */, 0x05CC /* "PAL" */));
            FarPtr p = far_make((u16)(pl >> 16), (u16)pl);
            rtl__fmemcpy(ds_far(0x408A), p, 0x300);
            pal_free(p);
        }
        break;
    case 2:
        edge_unload();
        ttm_run(0x36);
        break;
    case 3:
        edge_unload();
        ttm_run(0x2A);
        break;
    default:
        break;
    }
    cockpit_leave_stub();
}

/* AP 6b3d:00ac cockpit_unload(): the three EMS view caches, gauges, edges, TTM */
void cockpit_unload(void)
{
    if (DSB(0x3EEB)) {
        DSB(0x3EEB) = 0;
        lib_ems_free(DSW(0x3EEC));
        lib_ems_free(DSW(0x3EEE));
        lib_ems_free(DSW(0x3EF0));
    }
    gauges_free();
    edge_arena_free();
    ttm_run(0x54);
    ttm_shutdown();
}

/* AP 6de0:0000 cockpit_leave_stub() — empty */
void cockpit_leave_stub(void)
{
}

/* AP 6de0:0005 gauges_free(): common bitmap DS:3F4A, gauge array DS:3F4E */
void gauges_free(void)
{
    if (DSW(0x3F4A) != 0) bmp_free_w(DSW(0x3F4A));
    if (DSL(0x3F4E) != 0) mem_far_delete(ds_far(0x3F4E));
}
