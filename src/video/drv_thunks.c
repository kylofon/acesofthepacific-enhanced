/* The library's jump-table thunks (video_library.md §1): each is `ljmp/lcall [DS:tab + 4n]`, so it
 * calls whatever far pointer the table holds at call time (AP patches entries: clip hooks, VCR sets). */
#include "video/vid_priv.h"

#include "platform/platform.h"

/* codeptr_lookup with a small host-side cache (per-pixel paths call through far pointers). The cache is
 * dropped whenever the running program changes (the driver block is registered anew by each gfx_init). */
CodeFn vid_lookup(FarPtr p)
{
    static struct { u32 key; CodeFn fn; } cache[32];
    static unsigned next;
    static const ProgDesc *owner;
    if (prog_current() != owner || gfx_init_generation() != 0) {
        for (int i = 0; i < 32; i++) cache[i].fn = NULL;
        owner = prog_current();
    }
    u32 key = (u32)p.seg << 16 | p.off;
    for (int i = 0; i < 32; i++)
        if (cache[i].fn && cache[i].key == key) return cache[i].fn;
    CodeFn f = codeptr_lookup(p);
    if (f) { cache[next & 31].key = key; cache[next & 31].fn = f; next++; }
    return f;
}

CodeFn drv_entry(int n) { return vid_lookup(drv_entry_ptr(n)); }

/* INTRO 05da:06e5 / PS 16e3:0637 / AP 1d32:3de5 — entry 29 */
void gfx_sprite_flip(u16 f_ds, s16 x, s16 y, s16 flags) { ((DrvSpriteFlipFn)drv_entry(29))(f_ds, x, y, (u16)flags); }
/* INTRO 05da:06e9 / PS 16e3:063b / AP 1d32:3de9 — entry 33 */
void gfx_sprite_opaque(u16 f_ds, s16 x, s16 y) { ((DrvSpriteFn)drv_entry(33))(f_ds, x, y); }
/* INTRO 05da:0b6b / PS 16e3:117f / AP 1d32:09a8 — ES:SI = list, entry 27 */
void gfx_poly_spans(FarPtr spans) { ((DrvFarFn)drv_entry(27))(spans); }
/* INTRO 05da:0b7e / PS 16e3:1192 / AP 1d32:09bb — ES:SI = list, entry 23 */
void gfx_span_copy(FarPtr spans) { ((DrvFarFn)drv_entry(23))(spans); }
/* INTRO 05da:1e1d / PS 16e3:2431 / AP 1d32:426b — entry 30 */
void gfx_cache_blit(u16 f_ds, s16 x, s16 y) { ((DrvSpriteFn)drv_entry(30))(f_ds, x, y); }
/* INTRO 05da:1e21 / PS 16e3:2435 / AP 1d32:426f — entry 31 */
s16 gfx_cache_upload(u16 list_ds) { return (s16)((DrvCacheUploadFn)drv_entry(31))(list_ds); }
/* INTRO 05da:1e25 / PS 16e3:2439 / AP 1d32:4273 — entry 32 (arg pushed, ignored), then bmp_free_table */
void gfx_cache_reset(u16 list_ds)
{
    ((DrvWord1Fn)drv_entry(32))(list_ds);
    bmp_free_table(list_ds);
}
/* INTRO 05da:1e38 / PS 16e3:244c / AP 1d32:4286 — ljmp [DS:15BA] (drv[B3A] = gfx_sprite_scn) */
void gfx_sprite_scn_ind(u16 f_ds, s16 x, s16 y, s16 flags)
{
    ((void (*)(u16, s16, s16, s16))codeptr_lookup(ds_far(lds(G_scn_draw_fn))))(f_ds, x, y, flags);
}
/* INTRO 05da:24eb / PS 16e3:2aff / AP 1d32:4939 — entry 17 */
void gfx_nib_put(FarPtr src, u16 xb, u16 y, u16 w, u16 rows) { ((DrvNibFn)drv_entry(17))(src, xb, y, w, rows); }
/* INTRO 05da:24ef / PS 16e3:2b03 / AP 1d32:493d — entry 18 */
void gfx_nib_or_hi(FarPtr src, u16 xb, u16 y, u16 w, u16 rows) { ((DrvNibFn)drv_entry(18))(src, xb, y, w, rows); }
/* INTRO 05da:2fb0 / PS 16e3:35c4 / AP 1d32:53fe — entry 4 */
void gfx_copy_rect(s16 x, s16 y, s16 w, s16 h) { ((DrvRect4Fn)drv_entry(4))(x, y, w, h); }
/* INTRO 05da:2fb5 / PS 16e3:35c9 — entry 25 (retf on VGA) */
void gfx_entry25(u16 a0) { ((DrvWord1Fn)drv_entry(25))(a0); }
/* INTRO 05da:2fbd / PS 16e3:35d1 / AP 1d32:540b — entry 8 */
void gfx_flip(s16 wait) { ((DrvWord1Fn)drv_entry(8))((u16)wait); }
/* INTRO 05da:35d8 / PS 16e3:3bec / AP 1d32:5a26 — entry 5 */
void gfx_get_rect(FarPtr dst, s16 x, s16 y, s16 w, s16 h) { ((DrvRectBufFn)drv_entry(5))(dst, x, y, w, h); }
/* INTRO 05da:35dc / PS 16e3:3bf0 / AP 1d32:5a2a — entry 6 */
u32 gfx_rect_size(u16 w, u16 h) { return ((DrvRectSizeFn)drv_entry(6))(w, h); }
/* INTRO 05da:3cfd / PS 16e3:4021 / AP 1d32:5e5b — ljmp [DS:182C] (rot_fn = gfx_sprite_rotated) */
void gfx_sprite_rotated_ind(u16 f_ds, s16 x, s16 y, u16 angle, s16 w, s16 h, s16 px, s16 py)
{
    ((void (*)(u16, s16, s16, u16, s16, s16, s16, s16))codeptr_lookup(ds_far(lds(G_rot_fn))))(f_ds, x, y, angle, w, h, px, py);
}
/* INTRO 05da:492f / PS 16e3:4c53 / AP 1d32:6a8d — entry 7 */
void gfx_put_rect(FarPtr src, s16 x, s16 y, s16 w, s16 h) { ((DrvRectBufFn)drv_entry(7))(src, x, y, w, h); }
