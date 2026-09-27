/* shell_data: sys\extens.c (PS 2008) — numbered table files per installed module ("extensions"),
 * shell_data_sys.md §4. 20 slots of 7Dh bytes at DS:10A8 (slot 0 unused). */
#include "shell_data/sd_priv.h"

#define SLOT(s) ((u16)(0x10A8 + (s) * 0x7D))
/* slot fields (shell_data_sys.md §4.3) */
enum {
    SL_DATA = 0x00, SL_TAIL = 0x04, SL_BASE = 0x08, SL_EXT = 0x11, SL_RECFILE = 0x15, SL_TAILFILE = 0x16,
    SL_OWNED = 0x17, SL_FAR = 0x18, SL_RECSIZE = 0x19, SL_FIXED = 0x1B, SL_READCB = 0x1D,
    SL_FREECB = 0x21, SL_COUNT = 0x25, SL_CUM = 0x37, SL_TAILPOS = 0x47, SL_TAILSIZE = 0x59
};
#define S_COUNT(sl, i)    DSW((sl) + SL_COUNT + (i) * 2)
#define S_CUM(sl, i)      DSS((sl) + SL_CUM + (i) * 2)
#define S_TAILPOS(sl, i)  DSW((sl) + SL_TAILPOS + (i) * 2)
#define S_TAILSIZE(sl, i) DSL((sl) + SL_TAILSIZE + (i) * 4)

typedef void (*FileCbFn)(u16 f, s16 i);
typedef s8 (*LoaderFn)(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb);

/* PS 2008:0003 module_set_active (= 1f85:0833) */
void module_set_active(u8 m) { DSB(PS_DS_module_active_id) = m; }

/* PS 2008:000e module_active */
u8 module_active(void) { return DSB(PS_DS_module_active_id); }

/* PS 2008:0018 module_latest */
s8 module_latest(void) { return DSC(PS_DS_modules_latest); }

/* PS 2008:0022 module_iter_reset */
s8 module_iter_reset(void) { DSB(PS_DS_module_iter) = 0; return 0; }

/* PS 2008:002e module_iter_next — next installed module number, 9 at the end */
s8 module_iter_next(void)
{
    s16 mask = (s16)DSC(PS_DS_modules_mask);
    for (s8 b = (s8)DSB(PS_DS_module_iter); b < 8; b++)
        if ((mask >> b) & 1) {
            DSB(PS_DS_module_iter) = (u8)(b + 1);
            return (s8)(b + 1);
        }
    return 9;
}

/* PS 2008:0064 module_highest — cached in DS:1B01 (init FFh) */
s8 module_highest(void)
{
    if (DSC(0x1B01) == -1) {
        s16 mask = (s16)DSC(PS_DS_modules_mask);
        DSB(0x1B01) = 8;
        while (!((mask >> (u8)(DSB(0x1B01) - 1)) & 1) && DSB(0x1B01) != 0) DSB(0x1B01)--;
    }
    return DSC(0x1B01);
}

/* PS 2008:00a8 open_module_files(FILE *fp[9], base, ext) — "rb" DS:1B02/1B05/1B08 */
s8 open_module_files(u16 p0_ds, u16 base_ds, u16 ext_ds)
{
    for (int i = 0; i < 9; i++) DSW(p0_ds + i * 2) = 0;
    DSW(p0_ds) = my_fopen(make_name(base_ds, module_iter_reset(), ext_ds), 0x1B02);
    if (!DSW(p0_ds)) DSW(p0_ds) = my_fopen(make_name(base_ds, -1, ext_ds), 0x1B05);
    s16 m;
    while ((m = module_iter_next()) <= module_highest())
        DSW(p0_ds + m * 2) = my_fopen(make_name(base_ds, m, ext_ds), 0x1B08);
    return module_latest();
}

/* PS 2008:0143 files_total (uncalled): sum of fn(fp) or flength(fp) over the open files */
u16 files_total(u16 p0_ds, FarPtr fn)
{
    u32 sum = 0;
    for (int i = 0; i <= 8; i++) {
        u16 f = DSW(p0_ds + i * 2);
        if (!f) continue;
        if (!far_is_null(fn)) sum += (u32)((s32 (*)(u16))codeptr_lookup(fn))(f);
        else sum += (u32)my_filelength(f);
    }
    return (u16)sum;
}

/* PS 2008:019e files_foreach(fp[9], fn): fn(fp[i], i) for i 0..8 (NULL entries too) */
void files_foreach(u16 p0_ds, FarPtr fn)
{
    FileCbFn f = (FileCbFn)codeptr_lookup(fn);
    for (s16 i = 0; i <= 8; i++) f(DSW(p0_ds + i * 2), i);
}

/* PS 2008:01c0 file_close_nz */
static void file_close_nz(u16 f, s16 i)
{
    (void)i;
    if (f) my_fclose(f);
}

/* PS 2008:01d3 files_close */
void files_close(u16 p0_ds) { files_foreach(p0_ds, codeptr_far(PS_FN_file_close_nz)); }

/* PS 2008:01ea load_fail */
static s8 load_fail(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb) { return 0; }

/* PS 2008:01f3 load_one — swap mode, fp[0] only */
static s8 load_one(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb, u8 far_)
{
    u16 sl = SLOT(s);
    u16 f0 = DSW(fp9);
    DSB(sl + SL_RECFILE) = DSB(sl + SL_TAILFILE) = 0;
    DSB(sl + SL_FAR) = far_;
    if (flags & 1) {
        DSB(sl + SL_OWNED) = 1;
        u16 n = (u16)(S_COUNT(sl, 0) * DSW(sl + SL_RECSIZE));
        if (far_) {
            FarPtr d = xfarmalloc((u32)(s32)(s16)n);
            ds_far_wr(sl + SL_DATA, d);
            ps_fread_far_1193(d, DSW(sl + SL_RECSIZE), S_COUNT(sl, 0), f0);
        } else {
            u16 p = xmalloc(n);
            ds_far_wr(sl + SL_DATA, ds_ptr(p));
            my_fread(p, (s16)DSW(sl + SL_RECSIZE), (s16)S_COUNT(sl, 0), f0);
        }
    }
    if (flags & 2) {
        DSB(sl + SL_OWNED) = 0;
        ds_far_wr(sl + SL_DATA, list);
        my_fseek(f0, 4, 0);
        emd_set_stream(f0);
        list_read(list, (s16)DSW(sl + SL_RECSIZE), read_cb);
    }
    if (flags & 4) {
        S_TAILPOS(sl, 0) = (u16)rtl_ftell(f0);
        if (far_) {
            FarPtr t = xfarmalloc(S_TAILSIZE(sl, 0));
            ds_far_wr(sl + SL_TAIL, t);
            ps_fread_far_1193(t, 1, (u16)S_TAILSIZE(sl, 0), f0);
        } else {
            u16 p = xmalloc((u16)S_TAILSIZE(sl, 0));
            ds_far_wr(sl + SL_TAIL, ds_ptr(p));
            my_fread(p, 1, (s16)(u16)S_TAILSIZE(sl, 0), f0);
        }
    }
    return 1;
}

/* PS 2008:0334 / 035b load_one_near / load_one_far */
static s8 load_one_near(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb) { return load_one(s, fp9, flags, list, read_cb, 0); }
static s8 load_one_far(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb) { return load_one(s, fp9, flags, list, read_cb, 1); }

/* PS 2008:0382 load_records(s, fp, far) — a present file with 0 records aborts (fread 0) */
static s8 load_records(s16 s, u16 fp9, u8 far_)
{
    u16 sl = SLOT(s);
    DSB(sl + SL_FAR) = far_;
    DSB(sl + SL_OWNED) = 1;
    u16 total = 0;
    for (int i = 0; i < 9; i++) total = (u16)(total + S_COUNT(sl, i));
    s16 ok = 1;
    if (far_) {
        FarPtr p = xfarmalloc((u32)(s32)(s16)(u16)(DSW(sl + SL_RECSIZE) * total));
        ds_far_wr(sl + SL_DATA, p);
        for (int i = 0; i < 9 && ok; i++) {
            u16 f = DSW(fp9 + i * 2);
            if (!f) continue;
            ok = (s16)ps_fread_far_1193(p, DSW(sl + SL_RECSIZE), S_COUNT(sl, i), f);
            S_TAILPOS(sl, i) = (u16)my_ftell(f);
            p.off = (u16)(p.off + S_COUNT(sl, i) * DSW(sl + SL_RECSIZE));
        }
    } else {
        u16 p = xmalloc((u16)(DSW(sl + SL_RECSIZE) * total));
        ds_far_wr(sl + SL_DATA, ds_ptr(p));
        for (int i = 0; i < 9 && ok; i++) {
            u16 f = DSW(fp9 + i * 2);
            if (!f) continue;
            ok = my_fread(p, (s16)DSW(sl + SL_RECSIZE), (s16)S_COUNT(sl, i), f);
            S_TAILPOS(sl, i) = (u16)my_ftell(f);
            p = (u16)(p + S_COUNT(sl, i) * DSW(sl + SL_RECSIZE));
        }
    }
    return ok ? 1 : 0;
}

/* PS 2008:04d9 load_list(s, fp, far, list_hdr, read_cb) — list header +0 recsize, +2 count, +6 items */
static s8 load_list(s16 s, u16 fp9, u8 far_, FarPtr list, FarPtr read_cb)
{
    u16 sl = SLOT(s);
    ds_far_wr(sl + SL_DATA, list);
    DSB(sl + SL_OWNED) = 0;
    rtl__fmemset(list, 0, 10);
    fww(list, 0, DSW(sl + SL_RECSIZE));
    for (int i = 0; i < 9; i++) fww(list, 2, (u16)(fw(list, 2) + S_COUNT(sl, i)));
    if (fw(list, 2) == 0) {
        fwp(list, 6, far_null());
        return 1;
    }
    FarPtr q = xfarmalloc((u32)(u16)(fw(list, 2) * fw(list, 0)));
    fwp(list, 6, q);
    FarPtr marker = codeptr_far(PS_FN_list_dated_marker);
    for (int i = 0; i < 9; i++) {
        u16 f = DSW(fp9 + i * 2);
        if (!f) continue;
        emd_set_stream(f);
        for (s16 k = 0; k < (s16)S_COUNT(sl, i); k++) {
            if (far_eq(read_cb, marker)) {
                emd_read_date(q);
                emd_read_far(far_add(q, 2), (s16)(fw(list, 0) - 2), 1);
            } else if (!far_is_null(read_cb)) {
                ((void (*)(FarPtr))codeptr_lookup(read_cb))(q);
            } else {
                emd_read_far(q, (s16)fw(list, 0), 1);
            }
            q.off = (u16)(q.off + fw(list, 0));
        }
        S_TAILPOS(sl, i) = (u16)my_ftell(f);
    }
    return 1;
}

/* PS 2008:064d load_tails(s, fp, far) */
static s8 load_tails(s16 s, u16 fp9, u8 far_)
{
    u16 sl = SLOT(s);
    DSB(sl + SL_FAR) = far_;
    u32 sum = 0;
    for (int i = 0; i < 9; i++) sum += S_TAILSIZE(sl, i);
    bool all = (s32)sum <= 0x10000 && ((s32)sum <= 0x8000 || far_);
    if (!all) {
        DSB(sl + SL_TAILFILE) = 0;
        u16 f0 = DSW(fp9);
        if (far_) {
            FarPtr t = xfarmalloc(S_TAILSIZE(sl, 0));
            ds_far_wr(sl + SL_TAIL, t);
            ps_fread_far_1193(t, 1, (u16)S_TAILSIZE(sl, 0), f0);
        } else {
            u16 p = xmalloc((u16)S_TAILSIZE(sl, 0));
            ds_far_wr(sl + SL_TAIL, ds_ptr(p));
            my_fread(p, 1, (s16)(u16)S_TAILSIZE(sl, 0), f0);
        }
        return 1;
    }
    if (far_) {
        FarPtr p = xfarmalloc(sum);
        ds_far_wr(sl + SL_TAIL, p);
        for (int i = 0; i < 9; i++) {
            u16 f = DSW(fp9 + i * 2);
            if (!f) continue;
            ps_fread_far_1193(p, 1, (u16)S_TAILSIZE(sl, i), f);
            p.off = (u16)(p.off + (u16)S_TAILSIZE(sl, i));
        }
    } else {
        u16 p = xmalloc((u16)sum);
        ds_far_wr(sl + SL_TAIL, ds_ptr(p));
        for (int i = 0; i < 9; i++) {
            u16 f = DSW(fp9 + i * 2);
            if (!f) continue;
            my_fread(p, 1, (s16)(u16)S_TAILSIZE(sl, i), f);
            p = (u16)(p + (u16)S_TAILSIZE(sl, i));
        }
    }
    return 1;
}

/* PS 2008:07e6 load_all(s, fp, flags, list, read_cb, far) */
static s8 load_all(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb, u8 far_)
{
    u16 sl = SLOT(s);
    DSB(sl + SL_RECFILE) = DSB(sl + SL_TAILFILE) = 0xFF;
    DSB(sl + SL_FAR) = far_;
    s8 r = 1;   /* PORT: CL is uninitialised when flags has neither bit 0 nor bit 1 (never used) */
    if (flags & 1) r = load_records(s, fp9, far_);
    if (flags & 2) r = load_list(s, fp9, far_, list, read_cb);
    if ((flags & 4) && r) r = load_tails(s, fp9, far_);
    return r;
}

/* PS 2008:0862 / 0889 load_all_far / load_all_near */
static s8 load_all_far(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb) { return load_all(s, fp9, flags, list, read_cb, 1); }
static s8 load_all_near(s16 s, u16 fp9, u8 flags, FarPtr list, FarPtr read_cb) { return load_all(s, fp9, flags, list, read_cb, 0); }

/* PS 2008:08b0 flags_valid */
static s8 flags_valid(u8 f)
{
    if (!((f & 1) || (f & 2) || (f & 4))) return 0;
    return !(f & 1) || !(f & 2);
}

/* PS 2008:08e9 slot_free — first free slot 1..20, else 0 */
static s16 slot_free(void)
{
    for (s16 s = 1; s < 0x15; s++)
        if (far_is_null(ds_far(SLOT(s) + SL_DATA))) return s;
    return 0;
}

/* PS 2008:0914 loader_index(mode) — index into the far function table DS:1AE9 */
static s8 loader_index(u8 mode)
{
    if (mode & 8) return (mode & 1) ? 0 : (mode & 4) ? 2 : 1;
    return (mode & 1) ? 3 : (mode & 4) ? 5 : 4;
}

/* PS 2008:0952 slot_init */
static void slot_init(u16 base_ds, u16 ext_ds, s16 s, u16 fp9, s16 recsize, s16 fixed, FarPtr read_cb, FarPtr free_cb)
{
    u16 sl = SLOT(s);
    rtl_strcpy(sl + SL_BASE, base_ds);
    rtl_strcpy(sl + SL_EXT, ext_ds);
    DSW(sl + SL_RECSIZE) = (u16)recsize;
    DSW(sl + SL_FIXED) = (u16)fixed;
    ds_far_wr(sl + SL_READCB, read_cb);
    ds_far_wr(sl + SL_FREECB, free_cb);
    for (int i = 0; i < 9; i++) {
        S_TAILPOS(sl, i) = 0xFFFF;
        u16 f = DSW(fp9 + i * 2);
        if (f) {
            my_fread((u16)(sl + SL_TAILSIZE + i * 4), 4, 1, f);
            my_fread((u16)(sl + SL_COUNT + i * 2), 2, 1, f);
        } else {
            S_COUNT(sl, i) = 0;
            S_TAILSIZE(sl, i) = 0;
        }
    }
    S_CUM(sl, 0) = (s16)S_COUNT(sl, 0);
    for (int i = 1; i < 8; i++)
        S_CUM(sl, i) = (s16)(S_CUM(sl, i - 1) + (DSW(sl + SL_FIXED) ? DSS(sl + SL_FIXED) : (s16)S_COUNT(sl, i)));
}

/* PS 2008:0a71 open_all_files — every <base><i>.<ext>, i 1..8 ("rb" DS:1B0B/1B0E/1B11) */
static void open_all_files(u16 fp9, u16 base_ds, u16 ext_ds)
{
    DSW(fp9) = my_fopen(make_name(base_ds, 0, ext_ds), 0x1B0B);
    if (!DSW(fp9)) DSW(fp9) = my_fopen(make_name(base_ds, -1, ext_ds), 0x1B0E);
    for (s16 i = 1; i < 9; i++) DSW(fp9 + i * 2) = my_fopen(make_name(base_ds, i, ext_ds), 0x1B11);
}

/* PS 2008:0aec table_load(base, ext, recsize, fixed, mode, flags, list_hdr, read_cb, free_cb) -> slot */
s16 table_load(u16 base_ds, u16 ext_ds, s16 recsize, s16 fixed, u8 mode, u8 flags, FarPtr list, FarPtr read_cb, FarPtr free_cb)
{
    u16 m = stk_mark();
    u16 fp9 = stk_alloc(18);
    open_all_files(fp9, base_ds, ext_ds);
    int i;
    for (i = 0; i < 9; i++) if (DSW(fp9 + i * 2)) break;
    if (i == 9) { stk_release(m); return 0; }
    s16 s = slot_free();
    if (s) {
        if (!flags_valid(flags)) s = 0;
        else {
            slot_init(base_ds, ext_ds, s, fp9, recsize, fixed, read_cb, free_cb);
            /* the far function table DS:1AE9 holds 2008:0334 035b 01ea 0889 0862 01ea */
            s8 li = loader_index(mode);
            FarPtr fn = ds_far((u16)(0x1AE9 + li * 4));
            LoaderFn f = (LoaderFn)codeptr_lookup(fn);
            if (!f(s, fp9, flags, list, read_cb)) s = 0;
        }
    }
    files_close(fp9);
    stk_release(m);
    return s;
}

/* PS 2008:0bbd table_free */
void table_free(s16 s)
{
    u16 sl = SLOT(s);
    if (s <= 0 || s >= 0x15) sd_fatal(FS_EXTENS, 0x3AA);
    if (DSB(sl + SL_OWNED)) {
        if (DSB(sl + SL_FAR)) {
            if (!far_is_null(ds_far(sl + SL_DATA))) xfarfree(ds_far(sl + SL_DATA));
            if (!far_is_null(ds_far(sl + SL_TAIL))) xfarfree(ds_far(sl + SL_TAIL));
        } else {
            if (!far_is_null(ds_far(sl + SL_DATA))) xfree(DSW(sl + SL_DATA));
            if (!far_is_null(ds_far(sl + SL_TAIL))) xfree(DSW(sl + SL_TAIL));
        }
    } else {
        if (!far_is_null(ds_far(sl + SL_TAIL))) {
            if (DSB(sl + SL_FAR)) xfarfree(ds_far(sl + SL_TAIL));
            else xfree(DSW(sl + SL_TAIL));
        }
        list_free(ds_far(sl + SL_DATA), ds_far(sl + SL_FREECB));
    }
    ds_far_wr(sl + SL_TAIL, far_null());
    ds_far_wr(sl + SL_DATA, far_null());
}

/* PS 2008:0ca2 table_count(s, u16 *counts) */
s16 table_count(s16 s, u16 counts_ds)
{
    u16 sl = SLOT(s);
    if (s <= 0 || s >= 0x15) sd_fatal(FS_EXTENS, 0x3E9);
    if (far_is_null(ds_far(sl + SL_DATA))) return 0;
    s16 i = 8;
    while (i > 0 && S_COUNT(sl, i) == 0) i--;
    s16 n;
    if (i == 8) n = (s16)(S_CUM(sl, 7) + (DSW(sl + SL_FIXED) ? DSS(sl + SL_FIXED) : (s16)S_COUNT(sl, 8)));
    else n = S_CUM(sl, i);
    if (counts_ds)
        for (int k = 0; k < 9; k++) DSW(counts_ds + k * 2) = S_COUNT(sl, k);
    return n;
}

/* PS 2008:0d61 table_tail_size (uncalled) */
u32 table_tail_size(s16 s, u16 sizes_ds)
{
    u16 sl = SLOT(s);
    if (s <= 0 || s >= 0x15) sd_fatal(FS_EXTENS, 0x40A);
    u16 sum = 0;
    for (int i = 0; i < 9; i++) {
        sum = (u16)(sum + (u16)S_TAILSIZE(sl, i));
        if (sizes_ds) DSL(sizes_ds + i * 4) = S_TAILSIZE(sl, i);
    }
    return (u32)(s32)(s16)sum;
}

/* opens <base><f>.<ext> (f == 0: also <base>.<ext>), fatal with the name otherwise */
static u16 swap_open(u16 sl, s8 f, u16 rb1, u16 rb2, u16 line)
{
    u16 fh = my_fopen(make_name(sl + SL_BASE, f, sl + SL_EXT), rb1);
    if (!fh && f == 0) fh = my_fopen(make_name(sl + SL_BASE, -1, sl + SL_EXT), rb2);
    if (!fh) sd_fatal_msg(FS_EXTENS, line, make_name(sl + SL_BASE, f, sl + SL_EXT));
    return fh;
}

/* PS 2008:0df6 swap_records(slot*, char f) */
static void swap_records(u16 sl, s8 f)
{
    if ((s16)S_COUNT(sl, f) <= 0) return;
    u16 fh = swap_open(sl, f, 0x1B14, 0x1B17, 0x42F);
    if (!DSB(sl + SL_OWNED)) {
        list_free(ds_far(sl + SL_DATA), ds_far(sl + SL_FREECB));
        my_fseek(fh, 4, 0);
        emd_set_stream(fh);
        list_read(ds_far(sl + SL_DATA), (s16)DSW(sl + SL_RECSIZE), ds_far(sl + SL_READCB));
    } else {
        my_fseek(fh, 6, 0);
        u16 n = (u16)(S_COUNT(sl, f) * DSW(sl + SL_RECSIZE));
        if (DSB(sl + SL_FAR)) {
            if (!far_is_null(ds_far(sl + SL_DATA))) xfarfree(ds_far(sl + SL_DATA));
            FarPtr d = xfarmalloc((u32)(s32)(s16)n);
            ds_far_wr(sl + SL_DATA, d);
            ps_fread_far_1193(d, DSW(sl + SL_RECSIZE), S_COUNT(sl, f), fh);
        } else {
            if (!far_is_null(ds_far(sl + SL_DATA))) xfree(DSW(sl + SL_DATA));
            u16 p = xmalloc(n);
            ds_far_wr(sl + SL_DATA, ds_ptr(p));
            my_fread(p, (s16)DSW(sl + SL_RECSIZE), (s16)S_COUNT(sl, f), fh);
        }
    }
    DSB(sl + SL_RECFILE) = (u8)f;
    S_TAILPOS(sl, f) = (u16)my_ftell(fh);
    my_fclose(fh);
}

/* PS 2008:0fc7 id_split(slot*, id, int *local, int *file) */
static void id_split(u16 sl, s16 id, s16 *local, s16 *file)
{
    s16 f, acc = 0;
    if (DSC(sl + SL_RECFILE) == -1) {
        for (f = 0; f < 8 && S_CUM(sl, f) <= id; f++) acc = (s16)(acc + S_COUNT(sl, f));
    } else {
        for (f = 0; f < 8 && S_CUM(sl, f) <= id; f++) ;
    }
    *file = f;
    if (f == 0) { *local = id; return; }
    s16 rel = (s16)(id - S_CUM(sl, f - 1));
    if (rel < (s16)S_COUNT(sl, f)) *local = (s16)(rel + (DSC(sl + SL_RECFILE) == -1 ? acc : 0));
    else *local = -1;
}

/* PS 2008:105c rec_locate(slot*, id) */
static s16 rec_locate(u16 sl, s16 id)
{
    s16 local, file;
    id_split(sl, id, &local, &file);
    if (DSC(sl + SL_RECFILE) != -1 && (s16)DSC(sl + SL_RECFILE) != file) swap_records(sl, (s8)file);
    return local;
}

/* PS 2008:109d swap_tail(slot*, char f) — quirk: tail_file is not updated */
static void swap_tail(u16 sl, s8 f)
{
    if ((s32)S_TAILSIZE(sl, f) <= 0) return;
    u16 fh = swap_open(sl, f, 0x1B1A, 0x1B1D, 0x4A5);
    if ((s16)S_TAILPOS(sl, f) <= 0) sd_fatal(FS_EXTENS, 0x4A6);
    my_fseek(fh, (s32)(s16)S_TAILPOS(sl, f), 0);
    if (DSB(sl + SL_FAR)) {
        if (!far_is_null(ds_far(sl + SL_TAIL))) xfarfree(ds_far(sl + SL_TAIL));
        FarPtr t = xfarmalloc(S_TAILSIZE(sl, f));
        ds_far_wr(sl + SL_TAIL, t);
        ps_fread_far_1193(t, 1, (u16)S_TAILSIZE(sl, f), fh);
    } else {
        if (!far_is_null(ds_far(sl + SL_TAIL))) xfree(DSW(sl + SL_TAIL));
        u16 p = xmalloc((u16)S_TAILSIZE(sl, f));
        ds_far_wr(sl + SL_TAIL, ds_ptr(p));
        my_fread(p, 1, (s16)(u16)S_TAILSIZE(sl, f), fh);
    }
    my_fclose(fh);
}

/* PS 2008:1258 tail_base(slot*, f, u32 ofs) */
static u32 tail_base(u16 sl, s16 f, u32 ofs)
{
    for (s16 i = 0; i < f; i++) ofs += S_TAILSIZE(sl, i);
    return ofs;
}

/* PS 2008:128e tail_locate(slot*, id, u32 *ofs) */
static s16 tail_locate(u16 sl, s16 id, u32 *ofs)
{
    s16 local, file;
    id_split(sl, id, &local, &file);
    if (DSC(sl + SL_TAILFILE) == -1) *ofs = tail_base(sl, file, *ofs);
    else if ((s16)DSC(sl + SL_TAILFILE) != file) swap_tail(sl, (s8)file);
    return local;
}

/* PS 2008:12eb table_rec(s, id) */
FarPtr table_rec(s16 s, s16 id)
{
    u16 sl = SLOT(s);
    if (s <= 0 || s >= 0x15) sd_fatal(FS_EXTENS, 0x502);
    if (far_is_null(ds_far(sl + SL_DATA))) return far_null();
    s16 local = rec_locate(sl, id);
    if (local == -1) return far_null();
    if (DSB(sl + SL_OWNED)) return far_add(ds_far(sl + SL_DATA), (u16)(DSW(sl + SL_RECSIZE) * (u16)local));
    return list_at(ds_far(sl + SL_DATA), local);
}

/* PS 2008:138b table_text(s, id, u32 ofs) — tail + low word of the (rebased) offset.
 * HEADER FIX: the third argument is the u32 offset the callers push (was "u16 ofs, ..."). */
FarPtr table_text(s16 s, s16 id, u32 ofs)
{
    u16 sl = SLOT(s);
    if (s <= 0 || s >= 0x15) sd_fatal(FS_EXTENS, 0x523);
    if (far_is_null(ds_far(sl + SL_DATA))) return far_null();
    if (tail_locate(sl, id, &ofs) == -1) return far_null();
    return far_add(ds_far(sl + SL_TAIL), (u16)ofs);
}

/* PS 2008:13fa table_file_data(s, char f) */
FarPtr table_file_data(s16 s, s8 f)
{
    u16 sl = SLOT(s);
    if (s <= 0 || s >= 0x15) sd_fatal(FS_EXTENS, 0x53A);
    if (DSC(sl + SL_RECFILE) != f) swap_records(sl, f);
    return ds_far(sl + SL_DATA);
}

__attribute__((constructor)) static void extens_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_file_close_nz, (CodeFn)file_close_nz);
    codeptr_register(PROG_PS, SD_FN(0x2008, 0x01ea), (CodeFn)load_fail);
    codeptr_register(PROG_PS, SD_FN(0x2008, 0x0334), (CodeFn)load_one_near);
    codeptr_register(PROG_PS, SD_FN(0x2008, 0x035b), (CodeFn)load_one_far);
    codeptr_register(PROG_PS, SD_FN(0x2008, 0x0862), (CodeFn)load_all_far);
    codeptr_register(PROG_PS, SD_FN(0x2008, 0x0889), (CodeFn)load_all_near);
}
