/* shell_data: sys\strp.c (PS 24e3) string tables, shell_data_emd1.md §3.
 * Multi-table files (STRINGS_.DAT, DEBRIEF.DAT, FLTPLAN.DAT ...): DS:5786 ntab, DS:5788 near array of
 * near offset arrays, DS:578A far text, DS:1DC1 loaded flag. Single tables (.SCD): far DS:5792. */
#include "shell_data/sd_priv.h"

#define LOADED  0x1DC1
#define SCD_CAP 0x1DBF

/* PS 24e3:0002 scd_read(fp) — u16 n; u16 off[n]; u16 textsize; text -> buf {n, off[], text} */
static void scd_read(u16 f)
{
    FarPtr b = ds_far(PS_DS_scd_buf);
    u16 m = stk_mark();
    u16 ts = stk_alloc(2);
    ps_fread_far_1193(b, 2, 1, f);
    ps_fread_far_1193(far_add(b, 2), 2, fw(b, 0), f);
    my_fread(ts, 2, 1, f);
    ps_fread_far_1193(far_add(b, (u16)((fw(b, 0) + 1) * 2)), 1, DSW(ts), f);
    stk_release(m);
}

/* PS 24e3:0087 scd_load(name, bufsize) — "rb" DS:1DC2 */
void scd_load(u16 name_ds, s16 bufsize)
{
    if (bufsize == 0) sd_fatal(FS_STRP, 0x60);
    bool do_free = false, do_alloc = false;
    if (DSW(SCD_CAP) == 0) { do_alloc = true; do_free = !far_is_null(ds_far(PS_DS_scd_buf)); }
    else if (bufsize > (s16)DSW(SCD_CAP)) do_free = do_alloc = true;
    if (do_free) xfarfree(ds_far(PS_DS_scd_buf));
    if (do_alloc) {
        DSW(SCD_CAP) = (u16)bufsize;
        ds_far_wr(PS_DS_scd_buf, xfarmalloc((u32)(s32)bufsize));
    }
    if (far_is_null(ds_far(PS_DS_scd_buf))) sd_fatal(FS_STRP, 0x70);
    u16 f = my_fopen(name_ds, 0x1DC2);
    if (!f) sd_fatal(FS_STRP, 0x73);
    scd_read(f);
    my_fclose(f);
}

/* PS 24e3:0164 scd_push */
void scd_push(void) { ds_far_wr(PS_DS_scd_saved, ds_far(PS_DS_scd_buf)); }

/* PS 24e3:0177 scd_load_new (no callers) — "rb" DS:1DC5 */
void scd_load_new(u16 name_ds, s16 size)
{
    ds_far_wr(PS_DS_scd_buf, xfarmalloc((u32)(s32)size));
    if (far_is_null(ds_far(PS_DS_scd_buf))) sd_fatal(FS_STRP, 0x88);
    u16 f = my_fopen(name_ds, 0x1DC5);
    if (!f) sd_fatal(FS_STRP, 0x8B);
    scd_read(f);
    my_fclose(f);
}

/* PS 24e3:01fd scd_pop */
void scd_pop(void)
{
    xfarfree(ds_far(PS_DS_scd_buf));
    ds_far_wr(PS_DS_scd_buf, ds_far(PS_DS_scd_saved));
}

/* PS 24e3:021f scd_string(i) -> string pool copy (no bounds check) */
FarPtr scd_string(s16 idx)
{
    FarPtr b = ds_far(PS_DS_scd_buf);
    u16 text = (u16)(b.off + fw(b, 0) * 2 + 2);
    u16 o = fw(b, (u16)((idx + 1) * 2));
    return strpool_copy(far_make(b.seg, (u16)(text + o)));
}

/* PS 24e3:0268 strings_load(name) — "rb" DS:1DC8 */
void ps_strings_load_24e3(u16 name_ds)
{
    if (DSC(LOADED)) sd_fatal(FS_STRP, 0xBA);
    u16 f = my_fopen(name_ds, 0x1DC8);
    if (!f) sd_fatal(FS_STRP, 0xBE);
    u16 m = stk_mark();
    u16 v = stk_alloc(2), cnt = stk_alloc(2);
    my_fread(v, 2, 1, f);
    DSW(PS_DS_strings_ntab) = DSW(v);
    DSW(PS_DS_strings_offs) = xmalloc((u16)(DSW(v) * 2));
    if (!DSW(PS_DS_strings_offs)) sd_fatal(FS_STRP, 0xC3);
    for (s16 t = 0; t < (s16)DSW(v); t++) {
        my_fread(cnt, 2, 1, f);
        u16 arr = xmalloc((u16)(DSW(cnt) * 2));
        DSW(DSW(PS_DS_strings_offs) + t * 2) = arr;
        if (!arr) sd_fatal(FS_STRP, 0xC9);
        for (s16 k = 0; k < (s16)DSW(cnt); k++) my_fread((u16)(arr + k * 2), 2, 1, f);
    }
    my_fread(v, 2, 1, f);
    DSW(v)++;
    FarPtr p = xfarmalloc((u32)(s32)(s16)DSW(v));
    ds_far_wr(PS_DS_strings_text, p);
    while (!my_feof(f)) {
        fwb(p, 0, (u8)my_fgetc(f));
        p.off++;
    }
    my_fclose(f);
    stk_release(m);
    DSB(LOADED) = 1;
}

/* PS 24e3:0436 strings_free */
void strings_free(void)
{
    if (!DSB(LOADED)) sd_fatal(FS_STRP, 0xE8);
    for (s16 t = 0; t < (s16)DSW(PS_DS_strings_ntab); t++) {
        u16 a = DSW(DSW(PS_DS_strings_offs) + t * 2);
        if (a) xfree(a);
    }
    xfree(DSW(PS_DS_strings_offs));
    xfarfree(ds_far(PS_DS_strings_text));
    DSB(LOADED) = 0;
}

/* PS 24e3:04b4 strings_get(t, i) — no bounds check */
FarPtr strings_get(s16 table, s16 idx)
{
    if (!DSB(LOADED)) sd_fatal(FS_STRP, 0xFF);
    u16 arr = DSW((u16)(DSW(PS_DS_strings_offs) + table * 2));
    u16 o = DSW((u16)(arr + idx * 2));
    return far_add(ds_far(PS_DS_strings_text), o);
}

/* PS 24e3:050c strings_dup_near (unused) */
void strings_dup_near(u16 dst_ds, s16 n, s16 table)
{
    if (!DSB(LOADED)) sd_fatal(FS_STRP, 0x114);
    for (s16 i = 0; i < n; i++) {
        FarPtr s = strings_get(table, i);
        u16 p = xmalloc((u16)(rtl__fstrlen(s) + 1));
        DSW(dst_ds + i * 2) = p;
        strcpy_far_src(p, s);
    }
}

/* PS 24e3:0591 strings_free_near (unused) */
void strings_free_near(u16 a_ds, s16 n)
{
    for (s16 i = 0; i < n; i++) xfree(DSW(a_ds + i * 2));
}

/* PS 24e3:05b9 strings_dup_far(char far **dst, n, t) */
void strings_dup_far(u16 dst_ds, s16 n, s16 table)
{
    if (!DSB(LOADED)) sd_fatal(FS_STRP, 0x13C);
    for (s16 i = 0; i < n; i++) {
        FarPtr s = strings_get(table, i);
        u16 len = (u16)(rtl__fstrlen(s) + 1);
        FarPtr d = xfarmalloc((u32)(s32)(s16)len);
        ds_far_wr((u16)(dst_ds + i * 4), d);
        rtl__fstrncpy(d, s, len);
    }
}

/* PS 24e3:064a strings_free_far */
void strings_free_far(u16 a_ds, s16 n)
{
    for (s16 i = 0; i < n; i++) xfarfree(ds_far((u16)(a_ds + i * 4)));
}

/* PS 24e3:0678 strings_dup_ranks — 42 entries of table 3 into far DS:57A2 */
void strings_dup_ranks(void)
{
    for (s16 i = 0; i < 42; i++) {
        FarPtr s = strings_get(3, i);
        u16 len = (u16)(rtl__fstrlen(s) + 1);
        FarPtr d = xfarmalloc((u32)(s32)(s16)len);
        ds_far_wr((u16)(PS_DS_rank_abbr + i * 4), d);
        rtl__fstrncpy(d, s, len);
    }
}
