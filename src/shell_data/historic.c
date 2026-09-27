/* shell_data: emd\historic.c (PS 1f85) historic missions, shell_data_sys.md §5.
 * HIST_<n>.DAT list DS:527A (slot DS:5284, count DS:5286, loaded DS:1064); text slots
 * histnm_ DS:1065, histbf_ DS:1067, histov_ DS:1069, histsn_ DS:106B. */
#include "shell_data/sd_priv.h"

#define HSLOT ((s16)DSW(PS_DS_hist_slot))

/* PS 1f85:0007 hist_exists */
s8 hist_exists(s16 id) { return !far_is_null(table_rec(HSLOT, id)); }

/* PS 1f85:0027 hist_read_rec — {u16 key, u16 n, near entries[n] of 8 bytes} */
static void hist_read_rec(FarPtr rec)
{
    emd_read_far(rec, 2, 1);
    u16 m = stk_mark();
    u16 n = stk_alloc(2);
    emd_read(n, 2, 1);
    fww(rec, 2, DSW(n));
    fww(rec, 4, xmalloc((u16)(DSW(n) << 3)));
    emd_read(fw(rec, 4), 8, (s16)DSW(n));
    stk_release(m);
}

/* PS 1f85:0094 hist_free_rec */
static void hist_free_rec(FarPtr rec) { xfree(fw(rec, 4)); }

/* PS 1f85:00b7 hist_open — "hist_" DS:106D "dat" DS:1073 */
void hist_open(void)
{
    if (DSC(0x1064)) return;
    DSW(PS_DS_hist_slot) = (u16)table_load(0x106D, 0x1073, 6, 20, 0x12, 2, ds_ptr(PS_DS_hist_list),
                                           codeptr_far(PS_FN_hist_read_rec), codeptr_far(SD_FN(0x1f85, 0x0094)));
    if (!DSW(PS_DS_hist_slot)) sd_fatal(FS_HISTORIC, 0x58);
    DSW(PS_DS_hist_nrec) = (u16)table_count(HSLOT, 0);
    DSB(0x1064) = 1;
}

/* PS 1f85:0139 hist_close */
void hist_close(void)
{
    table_free(HSLOT);
    DSB(0x1064) = 0;
}

/* PS 1f85:014d hist_count */
s16 hist_count(void) { return (s16)DSW(PS_DS_hist_nrec); }

static FarPtr hist_rec(s16 id, u16 line)
{
    FarPtr r = table_rec(HSLOT, id);
    if (far_is_null(r)) sd_fatal(FS_HISTORIC, line);
    return r;
}

/* PS 1f85:0157 hist_key / 01a4 hist_n */
s16 hist_key(s16 id) { return fs(hist_rec(id, 0x8F), 0); }
s16 hist_n(s16 id) { return fs(hist_rec(id, 0x9E), 2); }

/* PS 1f85:01f2 / 024b / 02a5 / 02ff hist_e0/e2/e4/e6(id, i) — no bound check on i */
static s16 hist_e(s16 id, s16 i, u16 line, u16 fld)
{
    FarPtr r = hist_rec(id, line);
    return (s16)DSW((u16)(fw(r, 4) + i * 8 + fld));
}
s16 hist_e0(s16 id, s16 i) { return hist_e(id, i, 0xAD, 0); }
s16 hist_e2(s16 id, s16 i) { return hist_e(id, i, 0xBC, 2); }
s16 hist_e4(s16 id, s16 i) { return hist_e(id, i, 0xCB, 4); }
s16 hist_e6(s16 id, s16 i) { return hist_e(id, i, 0xDA, 6); }

/* PS 1f85:0359 hist_has_side */
s16 hist_has_side(s16 id, s16 v)
{
    if (!hist_exists(id)) return 0;
    s16 n = hist_n(id);
    for (s16 i = 0; i < n; i++) if (hist_e6(id, i) == v) return 1;
    return 0;
}

/* PS 1f85:039f hist_count_side */
s16 hist_count_side(s16 id, s16 v)
{
    s16 c = 0;
    if (!hist_exists(id)) return 0;
    s16 n = hist_n(id);
    for (s16 i = 0; i < n; i++) if (hist_e6(id, i) == v) c++;
    return c;
}

/* PS 1f85:03e9 hist_nth_side(id, v, k) — index of the k-th entry with side v (n if none) */
s16 hist_nth_side(s16 id, s16 v, s16 k)
{
    s16 n = hist_n(id), i = 0, seen = 0;
    while (i < n) {
        if (seen >= k && hist_e6(id, i) == v) break;
        if (hist_e6(id, i) == v) seen++;
        i++;
    }
    return i;
}

static s16 txt_open(u16 slot_ds, u16 base, u16 ext, u8 mode, u16 l1, u16 l2)
{
    if (DSW(slot_ds)) sd_fatal(FS_HISTORIC, l1);
    DSW(slot_ds) = (u16)table_load(base, ext, 4, 20, mode, 5, far_null(), far_null(), far_null());
    if (!DSW(slot_ds)) sd_fatal(FS_HISTORIC, l2);
    return 1;
}
static void txt_close(u16 slot_ds, u16 line)
{
    if (!DSW(slot_ds)) sd_fatal(FS_HISTORIC, line);
    table_free((s16)DSW(slot_ds));
    DSW(slot_ds) = 0;
}
static FarPtr txt_get(u16 slot_ds, s16 id, u16 line)
{
    if (!DSW(slot_ds)) sd_fatal(FS_HISTORIC, line);
    return extdb_text((s16)DSW(slot_ds), id);
}

/* PS 1f85:043b..06fd the four text tables ("histnm_" DS:1077 ... "dat") */
s16 histnm_open(void) { return txt_open(0x1065, 0x1077, 0x107F, 0x12, 0x137, 0x143); }
void histnm_close(void) { txt_close(0x1065, 0x149); }
s16 histbf_open(void) { return txt_open(0x1067, 0x1083, 0x108B, 0x0A, 0x156, 0x162); }
void histbf_close(void) { txt_close(0x1067, 0x168); }
s16 histov_open(void) { return txt_open(0x1069, 0x108F, 0x1097, 0x0A, 0x175, 0x181); }
void histov_close(void) { txt_close(0x1069, 0x188); }
s16 histsn_open(void) { return txt_open(0x106B, 0x109B, 0x10A3, 0x12, 0x195, 0x1A1); }
void histsn_close(void) { txt_close(0x106B, 0x1A7); }

/* PS 1f85:073b..07f5 */
FarPtr histnm_text(s16 id) { return txt_get(0x1065, id, 0x1B0); }
FarPtr histbf_text(s16 id) { return txt_get(0x1067, id, 0x1BE); }
FarPtr histov_text(s16 id) { return txt_get(0x1069, id, 0x1CB); }
FarPtr histsn_text(s16 id) { return txt_get(0x106B, id, 0x1D9); }

__attribute__((constructor)) static void historic_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_hist_read_rec, (CodeFn)hist_read_rec);
    codeptr_register(PROG_PS, SD_FN(0x1f85, 0x0094), (CodeFn)hist_free_rec);
}
