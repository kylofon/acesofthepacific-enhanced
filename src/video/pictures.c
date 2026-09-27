/* bmp_draw / bmp_free wrappers and the PS SNAPSHOT.LBM (IFF ILBM) writer (video_pictures.md §6-7).
 * The picture loaders themselves are platform's (platform.md §7). */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "platform/rtl.h"

/* INTRO 0f2c:0002 / PS 39a1:0002 / AP 26d2:0001 */
void bmp_free_w(u16 bmp) { bmp_free(bmp); }

/* INTRO 0f2c:0032 / PS 39a1:0032 / AP 26d2:0031 — by frame kind (+4): -3 opaque (entry 33, flip ignored),
 * -2 SCN (the indirect 05da:1e38), -1 VQT, else entry 29 */
void bmp_draw(u16 fr_ds, s16 x, s16 y, s16 flip)
{
    switch (DSS(fr_ds + 4)) {
    case -3: gfx_sprite_opaque(fr_ds, x, y); break;
    case -2: gfx_sprite_scn_ind(fr_ds, x, y, flip); break;
    case -1: vqt_draw(fr_ds, x, y, flip); break;
    default: gfx_sprite_flip(fr_ds, x, y, flip); break;
    }
}

/* ---------------------------------------------------------------- PS SNAPSHOT.LBM (§7) */

/* PS 3a13:0000 — n items of size 4/2/1 written big-endian */
static void put_be(u16 ptr_ds, s16 n, s16 size, u16 f)
{
    while (n-- != 0) {
        if (size == 4) {
            put_be((u16)(ptr_ds + 2), 1, 2, f);
            put_be(ptr_ds, 1, 2, f);
        } else if (size == 2) {
            rtl_fwrite((u16)(ptr_ds + 1), 1, 1, f);
            rtl_fwrite(ptr_ds, 1, 1, f);
        } else if (size == 1) {
            rtl_fwrite(ptr_ds, 1, 1, f);
        }
        ptr_ds = (u16)(ptr_ds + size);
    }
}

static void put_be32(u32 v, u16 f)
{
    u16 m = stk_mark(), p = stk_alloc(4);
    DSL(p) = v;
    put_be(p, 1, 4, f);
    stk_release(m);
}
static void put_be16(u16 v, u16 f)
{
    u16 m = stk_mark(), p = stk_alloc(2);
    DSW(p) = v;
    put_be(p, 1, 2, f);
    stk_release(m);
}

/* PS 3a13:0078 — "CMAP", 768, the hardware DAC (INT 10h/1017h) shifted to 8 bits */
static void ilbm_cmap(u16 f)
{
    rtl_fwrite(0x359B /* "CMAP" */, 1, 4, f);
    u16 m = stk_mark(), buf = stk_alloc(0x300);
    dac_get_regs(buf, 0, 0x100);
    put_be32(0x300, f);
    for (u16 i = 0; i < 0x300; i++) DSB((u16)(buf + i)) = (u8)(DSB((u16)(buf + i)) << 2);
    rtl_fwrite(buf, 0x300, 1, f);
    stk_release(m);
}

/* PS 3a13:00fa — "BODY", 64000, 200 rows of 8 interleaved bit-planes (the read page through gfx_get_pixel) */
static void ilbm_body(u16 f)
{
    rtl_fwrite(0x35A0 /* "BODY" */, 4, 1, f);
    put_be32(0xFA00, f);
    u16 row = rtl_malloc(0x280);
    for (s16 y = 0; y < 200; y++) {
        u16 p = row;
        for (s16 x = 0; x < 320; x++) DSB(p++) = (u8)gfx_get_pixel(x, y);
        c2p_row(row, (u16)(row + 0x140));
        rtl_fwrite((u16)(row + 0x140), 0x140, 1, f);
    }
    rtl_free(row);
}

/* PS 3a13:01af — the ILBM file */
static void ilbm_write(u16 name_ds)
{
    u16 f = rtl_fopen(name_ds, 0x35A5 /* "wb" */);
    if (f == 0) return;
    rtl_fwrite(0x358C /* "FORM" */, 4, 1, f);
    put_be32(0xFD30, f);
    rtl_fwrite(0x3591 /* "ILBM" */, 4, 1, f);
    rtl_fwrite(0x3596 /* "BMHD" */, 4, 1, f);
    put_be32(0x14, f);
    put_be16(0x140, f);
    put_be16(0xC8, f);
    put_be32(0, f);
    put_be32(0x08000000u, f);
    put_be32(0x00000101u, f);
    put_be16(0x140, f);
    put_be16(0xC8, f);
    ilbm_cmap(f);
    ilbm_body(f);
    rtl_fclose(f);
}

/* PS 3a13:031e — write page := read page, clipping off, around ilbm_write */
void snap_save(u16 name_ds)
{
    u16 w = DRVW(D_PAGE_WRITE);
    u8 c = DRVB(D_CLIP_ON);
    DRVB(D_CLIP_ON) = 0;
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_READ);
    ilbm_write(name_ds);
    DRVW(D_PAGE_WRITE) = w;
    DRVB(D_CLIP_ON) = c;
}
