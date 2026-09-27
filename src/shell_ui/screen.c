/* shell_ui: screens, palettes and string tables ("screen.c", PS segment 2e8a) — shell_ui_toolkit.md §5. */
#include "shell_ui/sui_a_priv.h"

void scd_load_new(u16 name_ds, s16 size);          /* shell_data/strp.c (PS 24e3:0177, file-local there) */

static u16 scr_bg(s16 id) { return DSW((u16)(DSW(T_SCREENS) + id * 4)); }
static u16 scr_scd(s16 id) { return DSW((u16)(DSW(T_SCREENS) + id * 4 + 2)); }

/* PS 2e8a:0009 screen_draw_bg_sentinel: the "draw the background" value of screen_open's draw_fn */
void screen_draw_bg_sentinel(void) { }

/* PS 2e8a:000e scr_is_parts(id) */
s16 scr_is_parts(s16 id) { return rtl_strcmp(scr_bg(id), 0x2088 /* "PARTS" */) == 0; }

/* PS 2e8a:0034 scr_is_mainmenu(id) */
static s16 scr_is_mainmenu(s16 id) { return rtl_strcmp(scr_bg(id), 0x208E /* "MAINMENU" */) == 0; }

/* PS 2e8a:005a scr_pal_load(id) */
static FarPtr scr_pal_load(s16 id)
{
    u32 p = pal_load(make_name(scr_bg(id), -1, 0x2099 /* "pal" */));
    return far_make((u16)(p >> 16), (u16)p);
}

/* PS 2e8a:0087 scr_palette(id) */
FarPtr scr_palette(s16 id)
{
    DSS(T_SCR_CUR) = id;
    if (scr_is_parts(id)) return ds_far(T_PARTS_PAL);
    if (!scr_is_mainmenu(id)) return scr_pal_load(id);
    if (id != DSS(T_MM_PAL_ID)) {
        pal_free(ds_far(T_MM_PAL));
        DSS(T_MM_PAL_ID) = id;
        ds_far_wr(T_MM_PAL, scr_pal_load(id));
    }
    return ds_far(T_MM_PAL);
}

/* PS 2e8a:00ee scr_pal_release(pal): frees it unless it is one of the two resident palettes */
void scr_pal_release(u16 off, u16 seg)
{
    FarPtr p = far_make(seg, off);
    if (!far_eq(p, ds_far(T_MM_PAL)) && !far_eq(p, ds_far(T_PARTS_PAL))) pal_free(p);
}

/* PS 2e8a:0124 scr_init */
void scr_init(void)
{
    ds_far_wr(T_MM_PAL, scr_pal_load(11));
    ds_far_wr(T_PARTS_PAL, scr_pal_load(10));
    screen_open(10, fnul(), 0, 0);
    copy_bg_to_back();
}

/* PS 2e8a:0164 screens_load: SCREENS<n>.DAT -> DS:601C */
void screens_load(void)
{
    u16 m = stk_mark();
    u16 hd = stk_alloc(4), cnt = stk_alloc(2);
    s8 set = module_latest();
    u16 f = my_fopen(make_name(0x209D /* "screens" */, set, 0x20A5 /* "dat" */), 0x20A9 /* "rb" */);
    if (!f) assert_line(AF_SCREEN, 0x7D);
    my_fread(hd, 4, 1, f);
    my_fread(cnt, 2, 1, f);
    s16 n = DSS(cnt);
    u16 text = xmalloc(DSW(hd));
    DSW(T_SCREENS) = xmalloc((u16)(n << 2));
    u16 ofs = xmalloc((u16)(n << 2));
    my_fread(ofs, 4, n, f);
    my_fread(text, 1, (s16)DSW(hd), f);
    my_fclose(f);
    for (s16 i = 0; i < n; i++) {
        DSW((u16)(DSW(T_SCREENS) + i * 4)) = (u16)(text + DSW((u16)(ofs + i * 4)));
        DSW((u16)(DSW(T_SCREENS) + i * 4 + 2)) = (u16)(text + DSW((u16)(ofs + i * 4 + 2)));
    }
    xfree(ofs);
    stk_release(m);
}

/* PS 2e8a:0281 scr_file(id) -> "<bg>.scr" */
static u16 scr_file(s16 id) { return make_name(scr_bg(id), -1, 0x20AC /* "scr" */); }

/* PS 2e8a:02a7 scr_touch(id): opens and closes the picture (pre-read) */
static void scr_touch(s16 id)
{
    my_fclose(my_fopen(scr_file(id), 0x20B0 /* "rb" */));
}

/* PS 2e8a:02c7 scr_load(id): the picture into page DS:2318 */
static void ps_scr_load(s16 id)
{
    DSB(TXT_FLAGS) = 0;
    scr_load(scr_file(id), 0);
}

/* PS 2e8a:02e0 scr_cached_mainmenu(id): main-menu pictures are kept in page AC00h (adapter 2 or 8) */
static s16 scr_cached_mainmenu(s16 id)
{
    if (!scr_is_mainmenu(id) || (DSB(PS_DS_drv_adapter) != 2 && DSB(PS_DS_drv_adapter) != 8)) return 0;
    DSW(PG_WRITE) = 0xAC00;
    DSW(PG_READ) = 0xAC00;
    if (DSS(T_SCR_CACHED) != id) {
        DSS(T_SCR_CACHED) = id;
        ps_scr_load(id);
    }
    DSW(PG_WRITE) = DSW(PG_BACK);
    page_copy_full();
    return 1;
}

/* PS 2e8a:0331 scr_draw_bg(id) */
void scr_draw_bg(s16 id)
{
    if (scr_is_parts(id)) {
        screen_clear_black();
        return;
    }
    DSW(PG_WRITE) = DSW(PG_BACK);
    if (!scr_cached_mainmenu(id)) ps_scr_load(id);
}

/* PS 2e8a:0363 scr_prefetch(id) */
static void scr_prefetch(s16 id)
{
    if (scr_is_parts(id)) return;
    if (!scr_is_mainmenu(id) || DSS(T_SCR_CACHED) != id) scr_touch(id);
}

/* PS 2e8a:038e screen_open(id, draw_fn, scd_arg, defer_pal) */
void screen_open(u16 id_, FarPtr draw_fn, u16 scd_arg, u16 defer_pal)
{
    s16 id = (s16)id_;
    bool sentinel = far_eq(draw_fn, FNP(screen_draw_bg_sentinel));
    if (scd_arg != 0 && DSB(scr_scd(id)) != 0)
        scd_load(make_name(scr_scd(id), -1, 0x20B3 /* "scd" */), (s16)scd_arg);
    if (sentinel) scr_prefetch(id);
    FarPtr pal = scr_palette(id);
    if (!far_is_null(draw_fn)) {
        if (sentinel) scr_draw_bg(id);
        else call_far(draw_fn);
    }
    if ((u8)defer_pal == 0) {
        gfx_flip(0);
        pal_set(pal);
        scr_pal_release(pal.off, pal.seg);
    } else {
        ui_set_pending_palette(pal);
    }
}

/* PS 2e8a:0454 screen_show(id, scd_arg, defer_pal): every screen starts with this */
void screen_show(u16 id_, u16 scd_arg, u16 defer_pal)
{
    s16 id = (s16)id_;
    if (id == -1) id = DSS(T_SCR_CACHED);
    if (DSS(T_SCR_CUR) == id) {
        scr_draw_bg(id);
        if ((u8)defer_pal == 0) gfx_flip(0);
    } else {
        screen_open((u16)id, FNP(screen_draw_bg_sentinel), scd_arg, defer_pal);
    }
}

/* PS 2e8a:049d scr_load_scd(id, arg): a second string table (dialogs) */
void scr_load_scd(u16 id, u16 arg)
{
    scd_load_new(make_name(scr_scd((s16)id), -1, 0x20B7 /* "scd" */), (s16)arg);
}

__attribute__((constructor)) static void screen_codeptrs(void)
{
    REG(screen_draw_bg_sentinel);
}
