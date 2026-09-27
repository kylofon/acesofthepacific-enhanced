/* Graphics initialisation (video.md §2): gfx_init / driver "loading", adapter detection, BIOS mode
 * helpers, and AP's graphics-state record and driver-entry sets (video_library.md §9.9).
 *
 * PORT (video.md §6): the OVL:VGA: chunk is not read. gfx_load_driver allocates the DOS block the driver
 * would occupy (9424 bytes) and writes the driver's jump table (cs:00DA, 50 far pointers with segment 0)
 * into it, so that gfx_init copies and patches it exactly like the original; the C entries of
 * video/drv_vga.c are registered for the resulting far pointers (codeptr_register_abs). Only the VGA
 * driver exists: other adapter codes fail to load and the callers retry with auto-detection (VGA). */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "symbols_ap.h"
#include "symbols_intro.h"

#define DRV_BLOCK_SIZE 9424u                      /* OVL:VGA: */

/* Library globals the layout lacks for INTRO (named pal_slots / font_ptr in symbols.csv). */
u16 ds_pal_slot(void) { return lds(G_pal_slot); }
u16 ds_font_glyphs(void) { return lds(G_font_glyphs); }

/* PORT: effects that draw straight into the displayed page show it at once (the host presents at its own
 * vsync when the composed picture changed). */
void vid_present(void) { host_pump(); }

static int regen;
int gfx_init_generation(void) { int r = regen; regen = 0; return r; }

/* The driver's jump table (VMCODE_OVL_VGA.bin cs:00DA): entry offsets and the C functions. */
static const struct { u16 off; CodeFn fn; } drv_table[50] = {
    { 0x0000, (CodeFn)drv_init },        { 0x0E72, (CodeFn)drv_glyph1 },      { 0x0753, (CodeFn)drv_line },
    { 0x0E58, (CodeFn)drv_sprite },      { 0x1103, (CodeFn)drv_copy_rect },   { 0x0F67, (CodeFn)drv_get_rect },
    { 0x0FCA, (CodeFn)drv_rect_size },   { 0x0FD6, (CodeFn)drv_put_rect },    { 0x10AD, (CodeFn)drv_flip },
    { 0x01EE, (CodeFn)drv_nop },         { 0x02B1, (CodeFn)drv_hspan },       { 0x0205, (CodeFn)drv_hspan_dither },
    { 0x064D, (CodeFn)drv_nop },         { 0x0CD6, (CodeFn)drv_bm_size },     { 0x0D09, (CodeFn)drv_bm_layout },
    { 0x0E04, (CodeFn)drv_bm_merge_hi }, { 0x064C, (CodeFn)drv_nop },         { 0x126D, (CodeFn)drv_nib_put },
    { 0x131C, (CodeFn)drv_nib_or_hi },   { 0x050E, (CodeFn)drv_cols_xlat },   { 0x0C16, (CodeFn)drv_set_palette },
    { 0x103F, (CodeFn)drv_get_pixel },   { 0x1075, (CodeFn)drv_put_pixel },   { 0x1173, (CodeFn)drv_span_copy },
    { 0x1252, (CodeFn)drv_span_copy_ofs },{ 0x01EE, (CodeFn)drv_nop },        { 0x01EE, (CodeFn)drv_nop },
    { 0x0932, (CodeFn)drv_poly_spans },  { 0x01EE, (CodeFn)drv_nop },         { 0x1412, (CodeFn)drv_sprite_flip },
    { 0x1FCC, (CodeFn)drv_cache_blit },  { 0x2067, (CodeFn)drv_cache_upload },{ 0x2113, (CodeFn)drv_cache_reset },
    { 0x2128, (CodeFn)drv_sprite_opaque },{ 0x0C58, (CodeFn)drv_fade_step },  { 0x0BC2, (CodeFn)drv_dac_upload },
    { 0x0334, (CodeFn)drv_span_tex },    { 0x0438, (CodeFn)drv_span_cols },   { 0x064E, (CodeFn)drv_row_copy },
    { 0x24CF, (CodeFn)drv_nop },         { 0x01EE, (CodeFn)drv_nop },         { 0x01EE, (CodeFn)drv_nop },
    { 0x01EE, (CodeFn)drv_nop },         { 0x01EE, (CodeFn)drv_nop },         { 0x01EE, (CodeFn)drv_nop },
    { 0x01EE, (CodeFn)drv_nop },         { 0x01EE, (CodeFn)drv_nop },         { 0x01EE, (CodeFn)drv_nop },
    { 0x01EE, (CodeFn)drv_nop },         { 0x0000, (CodeFn)drv_init },
};

static u16 ds_driver_block(void)                  /* the loaded driver block far pointer */
{
    return lds(G_gfx_driver_block);
}

/* INTRO 05da:4933 / PS 16e3:4c57 / AP 70ff:0000 — video.md §2.1. Returns the driver's far address. */
static FarPtr gfx_load_driver(s16 code, u16 name_ds)
{
    (void)name_ds;
    if (code == 4) { code = 1; DRVW(D_SCR_W) = 640; }            /* Hercules -> CGA: */
    else if (code == 12) { code = 11; DRVW(D_SCR_H) = 350; }
    else if (code == 13) { code = 11; DRVW(D_SCR_H) = 480; }
    /* PORT: the file (vmcode.ovl / vmcode2.ovl) and its OVL:<tag> chunk are not read; only VGA exists. */
    if (code != 8) return far_make(0, 0);
    u16 blk = ds_driver_block();
    FarPtr old = ds_far(blk);
    if (!far_is_null(old)) dos_free(old);
    FarPtr p = dos_alloc(DRV_BLOCK_SIZE, 0, 0);
    ds_far_wr(blk, p);
    if (far_is_null(p)) return p;
    for (int n = 0; n < 50; n++) far_wr(p.seg, (u16)(0x00DA + 4 * n), far_make(0, drv_table[n].off));
    return p;
}

/* INTRO 05da:5f6b / PS 16e3:5cc5 / AP 1d32:02e1 (near) — video_library.md §9.3 */
static u16 bios_get_video_bits(void) { return (u16)((rd8(0x40, 0x10) & 0x30) >> 4); }

/* INTRO 05da:5f83 / PS 16e3:5cdd / AP 1d32:02f9 (near) */
static void bios_set_colour_text(void)
{
    wr8(0x40, 0x10, (u8)((rd8(0x40, 0x10) & 0xCF) | 0x20));
    vga_set_mode(0x03);                                          /* INT 10h AX=0003h */
}

/* INTRO 05da:5f2e / PS 16e3:5c88 / AP 1d32:02a4 (near, DX = CRTC index port): CF = 1 if no CRTC there.
 * PORT: the VGA CRTC answers at 3D4h only. */
static bool crtc_probe(u16 dx) { return dx != 0x3D4; }

/* INTRO 05da:5de1 / PS 16e3:5b3b / AP 1d32:0157 (near) — video.md §2.2 step 3.
 * PORT: the machine is a VGA (display combination code 8): codes 0, 8, 10, 13 take the VGA branch
 * (0 -> 8); the MCGA/EGA/CGA/Hercules branches return the requested code (their drivers do not exist in
 * the port, so gfx_init fails and the caller retries with 0). */
static s16 gfx_detect_adapter(void)
{
    u8 code = DSB(lds(G_gfx_req_adapter));
    if (DRVB(D_HW_PRESENT) == 0) return code;
    if (code == 0 || code == 10 || code == 8 || code == 13) return code ? code : 8;
    if (code == 9) { bios_set_colour_text(); return 9; }
    if (code == 1 || code == 3 || code == 4 || code >= 14) {
        (void)crtc_probe(0x3B4);
        return 0;
    }
    return code;
}

/* INTRO 05da:5c93 / PS 16e3:59ed / AP 1d32:000a — video.md §2.2 */
s16 gfx_init(s16 code, s16 unused, u16 file_ds)
{
    (void)unused;
    DSB(lds(G_gfx_req_adapter)) = (u8)code;
    DRVB(D_YSHIFT) = 0;
    DRVB(D_INITS) = 0;
    DRVW(D_SCR_W) = 320;
    DRVW(D_SCR_H) = 200;
    u16 ps = ds_pal_slot();
    FarPtr pb = ds_far(ps);
    if (!far_is_null(pb)) { dos_free(pb); ds_far_wr(ps, far_make(0, 0)); }
    DSB(lds(G_gfx_bios_mode)) = (u8)bios_get_video_bits();
    s16 a = gfx_detect_adapter();
    DRVB(D_ADAPTER) = (u8)a;
    if (a != 0) {
        FarPtr drv = gfx_load_driver(a, file_ds);
        if (drv.seg == 0) {
            DRVB(D_ADAPTER) = 0;
        } else {
            ds_far_wr(lds(G_gfx_driver_ptr), drv);
            /* the jump table entries of this block -> the C driver (registered before the init call:
             * drv_init itself is reached through the far pointer, as `lcall [1A52]`) */
            for (int n = 0; n < 50; n++)
                codeptr_register_abs(far_make(drv.seg, drv_table[n].off), drv_table[n].fn);
            regen = 1;
            FarPtr (*init)(u16, FarPtr) = (FarPtr (*)(u16, FarPtr))codeptr_lookup(drv);
            FarPtr tab = init(drv_base(), ds_ptr(lds(G_drv_callbacks)));
            int count = pid() == PROG_AP ? 40 : 50;
            for (int n = 0; n < count; n++) {
                FarPtr e = far_rd(tab.seg, (u16)(tab.off + 4 * n));
                e.seg = tab.seg;                                 /* the stored segments are 0 */
                ds_far_wr((u16)(drv_tab() + 4 * n), e);
            }
        }
    }
    wr16(0, 0x04F0, cur_dgroup);                                 /* BIOS inter-application area */
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_FRONT);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
    s16 ret = DRVB(D_ADAPTER);
    if (ret != 0) {
        if (DRVW(D_SPAN_SEG) != 0) dos_free(far_make((u16)(DRVW(D_SPAN_SEG) - 1), 0));
        FarPtr sb = dos_alloc((u32)((u16)(0x340 << DRVB(D_YSHIFT))), 0, 0);   /* 0x340 bytes */
        if (sb.seg != 0) {
            DRVW(D_SPAN_SEG) = (u16)(sb.seg + 1);
            /* INT 10h AX=1130h BH=3: ES:BP = the ROM 8x8 font (chars 0-127).
             * PORT: the ROM font is not modelled; the pointer is the usual F000:FA6E (zero glyphs). */
            FarPtr rom = far_make(0xF000, 0xFA6E);
            u16 fg = ds_font_glyphs();
            ds_far_wr(fg, rom);
            ds_far_wr((u16)(fg + 4), rom);
            DRVW(D_FONT_H) = 0x0808;                             /* font_h[0], font_h[1] */
            DRVW(D_FONT_W) = 0x0808;
            DRVW(D_FONT_FIRST) = 0x0000;
            DRVW(D_FONT_COUNT) = 0xFFFF;
        }
    }
    return ret;
}

/* INTRO 05da:5dc9 / PS 16e3:5b23 / AP 1d32:013f — video.md §2.2 */
s16 gfx_restore_text(void)
{
    u16 m = lds(G_gfx_bios_mode);
    u8 b = DSB(m);
    if (b != 0xFF) {
        bios_set_text_mode(b);
        DSB(m) = 0xFF;
    }
    return b;
}

/* INTRO 05da:06f1 / PS 16e3:0643 / AP 1d32:3df1 — EGA write-mode reset; a no-op on VGA (drv[21] = FFh). */
void gfx_ega_restore(void)
{
    if (DRVB(D_EGA_MODE) == 0x10) {
        vga_outw(0x3CE, 0x0205);
        vga_outw(0x3CE, 0xFF08);
        vga_outw(0x3C4, 0x0F02);
    }
}

/* ------------------------------------------------------------ AP 244d: graphics state (§9.9) */

/* AP 244d:000b — 28-byte record */
void gfx_state_save(u16 st_ds)
{
    DSB(st_ds + 0x16) = DRVB(D_TEXT_FG);
    DSB(st_ds + 0x17) = DRVB(D_TEXT_BG);
    DSB(st_ds + 0x18) = DRVB(D_TEXT_FLAGS);
    DSW(st_ds + 0x00) = DRVW(D_CLIP_X1);
    DSW(st_ds + 0x02) = DRVW(D_CLIP_X2);
    DSW(st_ds + 0x04) = DRVW(D_CLIP_Y1);
    DSW(st_ds + 0x06) = DRVW(D_CLIP_Y2);
    DSB(st_ds + 0x08) = DRVB(D_CLIP_ON);
    DSB(st_ds + 0x19) = DRVB(D_DRAW_COL);
    DSB(st_ds + 0x1A) = DRVB(D_FILL_COL);
    DSB(st_ds + 0x1B) = DRVB(D_FILL_ON);
    DSW(st_ds + 0x0C) = DRVW(D_PAGE_BACK);
    DSW(st_ds + 0x0E) = DRVW(D_PAGE_FRONT);
    DSW(st_ds + 0x10) = DRVW(D_PAGE_BG);
    DSW(st_ds + 0x12) = DRVW(D_PAGE_READ);
    DSW(st_ds + 0x14) = DRVW(D_PAGE_WRITE);
    DSB(st_ds + 0x09) = DRVB(D_20);
    DSW(st_ds + 0x0A) = (u16)gfx_font_select(0);
}

/* AP 244d:0087 */
void gfx_state_restore(u16 st_ds)
{
    DRVB(D_TEXT_FG) = DSB(st_ds + 0x16);
    DRVB(D_TEXT_BG) = DSB(st_ds + 0x17);
    DRVB(D_TEXT_FLAGS) = DSB(st_ds + 0x18);
    DRVW(D_CLIP_X1) = DSW(st_ds + 0x00);
    DRVW(D_CLIP_X2) = DSW(st_ds + 0x02);
    DRVW(D_CLIP_Y1) = DSW(st_ds + 0x04);
    DRVW(D_CLIP_Y2) = DSW(st_ds + 0x06);
    DRVB(D_CLIP_ON) = DSB(st_ds + 0x08);
    DRVB(D_DRAW_COL) = DSB(st_ds + 0x19);
    DRVB(D_FILL_COL) = DSB(st_ds + 0x1A);
    DRVB(D_FILL_ON) = DSB(st_ds + 0x1B);
    DRVW(D_PAGE_BACK) = DSW(st_ds + 0x0C);
    DRVW(D_PAGE_FRONT) = DSW(st_ds + 0x0E);
    DRVW(D_PAGE_BG) = DSW(st_ds + 0x10);
    DRVW(D_PAGE_READ) = DSW(st_ds + 0x12);
    DRVW(D_PAGE_WRITE) = DSW(st_ds + 0x14);
    DRVB(D_20) = DSB(st_ds + 0x09);
    gfx_font_select((s16)DSW(st_ds + 0x0A));
}

/* AP driver-entry sets: entries 27 (DS:3586), 2 (3522), 10 (3542), 22 (3572). */
#define AP_SET_ACTIVE AP_DS_drv_set_active       /* recorded set AP_DS_drv_set_rec 5ECE.., saved 5EBE.. */

static void copy_far(u16 dst, u16 src) { DSL(dst) = DSL(src); }

/* AP 244d:0101 */
void drv_set_record(void)
{
    copy_far(0x5EDA, 0x3586);
    copy_far(0x5ED6, 0x3522);
    copy_far(0x5ED2, 0x3542);
    copy_far(0x5ECE, 0x3572);
}

/* AP 244d:0126 */
void drv_set_select(void)
{
    DSB(AP_SET_ACTIVE) = 1;
    copy_far(0x5ECA, 0x3586);
    copy_far(0x5EC6, 0x3522);
    copy_far(0x5EC2, 0x3542);
    copy_far(0x5EBE, 0x3572);
    copy_far(0x3586, 0x5EDA);
    copy_far(0x3522, 0x5ED6);
    copy_far(0x3542, 0x5ED2);
    copy_far(0x3572, 0x5ECE);
}

/* AP 244d:0170 (no caller) */
void drv_set_unselect(void)
{
    DSB(AP_SET_ACTIVE) = 0;
    copy_far(0x3586, 0x5ECA);
    copy_far(0x3522, 0x5EC6);
    copy_far(0x3542, 0x5EC2);
    copy_far(0x3572, 0x5EBE);
}
