/* The RTL-segment DAC / attribute helpers of PS and AP (video_palette.md §8.1-8.2, video_pictures.md §7).
 * PORT: INT 10h / port I/O on the VGA model (platform/vga.h); the dummy 3C7h-state accesses are dropped. */
#include "video/vid_priv.h"

/* PS 0000:1664 — INT 10h AX=1012h: set count DAC registers from first, data at DS:buf (no caller known) */
void dac_set_regs(u16 buf_ds, u16 first, u16 count)
{
    for (u16 i = 0; i < count; i++)
        vga_dac_set((u8)(first + i), DSB(buf_ds + 3 * i), DSB(buf_ds + 3 * i + 1), DSB(buf_ds + 3 * i + 2));
}

/* PS 0000:167b — INT 10h AX=1017h: read count DAC registers from first into DS:buf */
void dac_get_regs(u16 buf_ds, u16 first, u16 count)
{
    for (u16 i = 0; i < count; i++)
        vga_dac_get((u8)(first + i), DSP(buf_ds + 3 * i), DSP(buf_ds + 3 * i + 1), DSP(buf_ds + 3 * i + 2));
}

/* PS 0000:1692 / AP 0000:18fc — chunky to 8 bit-planes: for each of 40 groups g of 8 pixels at DS:src,
 * dst[p*40 + g] = bit p of the 8 pixels, first pixel in bit 7. */
void c2p_row(u16 src_ds, u16 dst_ds)
{
    for (u16 g = 0; g < 40; g++) {
        for (int p = 0; p < 8; p++) {
            u8 v = 0;
            for (int k = 0; k < 8; k++) v = (u8)(v << 1 | ((DSB(src_ds + 8 * g + k) >> p) & 1));
            DSB(dst_ds + 40 * p + g) = v;
        }
    }
}

/* AP 0000:185e — video_palette.md §8.2: wait for retrace, read count colours from first (the DAC model) */
void dac_read(u16 dst, s16 first, s16 count)
{
    vga_wait_vretrace_active();
    vga_out(0x3C7, (u8)first);
    for (u16 i = 0; i < (u16)(count * 3); i++) DSB((u16)(dst + i)) = vga_in(0x3C9);
}

/* AP 0000:189b — video_palette.md §8.1: the driver's entry 35, AP's own copy */
void dac_write(u16 src, s16 first, s16 count)
{
    vga_wait_vretrace_active();
    vga_out(0x3C8, (u8)first);
    for (u16 i = 0; i < (u16)(count * 3); i++) vga_out(0x3C9, DSB((u16)(src + i)));
}

/* AP 0000:1a4e — INT 10h AX=1007h (read attribute palette register BL): (v & 7) | ((v & 10h) >> 1).
 * PORT: the attribute palette registers keep the mode-13h identity values (0..15). */
u16 atc_get_reg(u16 reg)
{
    u8 v = (u8)(reg & 0x0F);
    return (u16)((v & 7) | ((v & 0x10) >> 1));
}
