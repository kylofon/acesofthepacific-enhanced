/* Library palette functions (video_palette.md §4): pal_set, pal_fade, colour-cycle blocks and their
 * (uncalled) stepper. INTRO 05da / PS 16e3 / AP 1d32. pal_load / pal_free are platform's loaders. */
#include "video/vid_priv.h"

#include "platform/platform.h"

/* INTRO 05da:090e / PS 16e3:0f22 / AP 1d32:400e — video_palette.md §4.2 */
FarPtr pal_set(FarPtr pal)
{
    LW(G_pal_size) = DSW((u16)(lds(G_pal_size_tab) + 2 * (s8)DRVB(D_ADAPTER)));
    u16 ps = ds_pal_slot();
    if (far_is_null(ds_far(ps)) && LW(G_pal_size) != 0)
        ds_far_wr(ps, dos_alloc((u32)(u16)(LW(G_pal_size) << 1), 0, 0));
    if (far_is_null(pal)) return ds_far(lds(G_pal_current));
    ds_far_wr(lds(G_pal_current), pal);
    ((DrvFarFn)drv_entry(20))(pal);
    return pal;
}

/* INTRO 05da:09da / PS 16e3:0fee / AP 1d32:40da — video_palette.md §4.4 */
void pal_fade(s16 first, s16 count, s16 c, s16 l)
{
    LW(G_fade_level) = (u16)l;
    LW(G_fade_colour) = (u16)c;
    ((DrvFadeFn)drv_entry(34))((u16)first, (u16)count, (u16)c, (u16)l);
}

/* INTRO 05da:0a00 / PS 16e3:1014 / AP 1d32:4100 — video_palette.md §4.5 */
s16 pal_cycle_block(s16 first, s16 count, s16 step)
{
    if (first < 0) { count = 0; LW(G_cyc_count) = 0; }
    if (DRVB(D_INITS) == 0 || LS(G_cyc_count) >= 9 || count <= 1) return 0;
    u16 n = LW(G_cyc_count);
    DSS((u16)(lds(G_cyc_start3) + 2 * n)) = (s16)(first * 3);
    DSS((u16)(lds(G_cyc_end3) + 2 * n)) = (s16)((first + count) * 3);
    if (step < 0) step = (s16)(count + step);
    DSS((u16)(lds(G_cyc_step3) + 2 * n)) = (s16)(step * 3);
    return (s16)++LW(G_cyc_count);
}

/* INTRO 05da:0b46 / PS 16e3:115a / AP 1d32:4246 (near) — byte loop, offsets only incremented */
static void far_copy_n(FarPtr src, FarPtr dst, u16 n)
{
    while (n--) wr8(dst.seg, dst.off++, rd8(src.seg, src.off++));
}

/* INTRO 05da:0a7b / PS 16e3:108f / AP 1d32:417b — video_palette.md §4.6; no caller in any program */
void pal_cycle_step(void)
{
    if (DRVB(D_INITS) == 0) return;
    FarPtr cur = ds_far(ds_pal_slot()), tgt = far_add(cur, 0x300);
    far_copy_n(tgt, cur, 0x300);
    for (s16 i = 0; i < LS(G_cyc_count); i++) {
        s16 s = DSS((u16)(lds(G_cyc_start3) + 2 * i)), e = DSS((u16)(lds(G_cyc_end3) + 2 * i));
        s16 st = DSS((u16)(lds(G_cyc_step3) + 2 * i));
        far_copy_n(far_add(cur, (u16)s), far_add(tgt, (u16)(s + st)), (u16)(e - s - st));
        far_copy_n(far_add(cur, (u16)(e - st)), far_add(tgt, (u16)s), (u16)st);
    }
    ((DrvFadeFn)drv_entry(34))(0, 0x100, LW(G_fade_colour), LW(G_fade_level));
}
