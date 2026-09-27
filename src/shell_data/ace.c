/* shell_data: emd\ace.c (PS 2550) aces, shell_data_emd1.md §5.
 * ACETAB.DAT lists DS:5A8D (4-byte rows, unused) / DS:5A83 (6-byte postings); ACE<n>.DAT extens
 * slot DS:5AA1 (record list DS:5A97, 15h bytes); ACETXT<n>.DAT slot DS:5A81. */
#include "shell_data/sd_priv.h"
#include "shell_ui/shell_ui.h"

#define ACE_LOADED 0x1DCC
#define ACL_IDS(slot, i)  DSW((u16)(PS_DS_acelist_ids + (slot) * 0x8C + (i) * 2))
#define ACL_EN(slot, i)   DSW((u16)(PS_DS_acelist_enabled + (slot) * 0x8C + (i) * 2))
#define ACL_N(slot)       DSW((u16)(PS_DS_acelist_n + (slot) * 2))

static FarPtr ace_rec(s16 ace, u16 line)
{
    FarPtr r = table_rec((s16)DSW(PS_DS_ace_db), ace);
    if (far_is_null(r)) sd_fatal(FS_ACE, line);
    return r;
}

/* PS 2550:000e ace_count */
s16 ace_count(void) { return table_count((s16)DSW(PS_DS_ace_db), 0); }

/* PS 2550:0023 ace_valid */
s8 ace_valid(s16 ace) { return !far_is_null(table_rec((s16)DSW(PS_DS_ace_db), ace)); }

/* PS 2550:0043 ace_name — "Full Name\0Short name\0" */
FarPtr ace_name(s16 ace)
{
    FarPtr r = ace_rec(ace, 0x4E);
    return table_text((s16)DSW(PS_DS_ace_db), ace, (u32)(s32)fs(r, 0));
}

/* PS 2550:00a3 ace_short_name */
FarPtr ace_short_name(s16 ace)
{
    FarPtr s = ace_name(ace);
    return far_add(s, (u16)(rtl__fstrlen(s) + 1));
}

/* PS 2550:00d0 ace_link */
s16 ace_link(s16 ace) { return fs(ace_rec(ace, 0x67), 2); }

/* PS 2550:011e ace_has_successor — no NULL check (ids without a record read 0000:0002) */
s8 ace_has_successor(s16 ace)
{
    for (s16 i = 0; i < (s8)ace_count(); i++) {
        FarPtr r = table_rec((s16)DSW(PS_DS_ace_db), i);
        s16 link = far_is_null(r) ? (s16)rd16(0, 2) : fs(r, 2);   /* PORT: the IVT word as the original reads */
        if (link == ace) return 1;
    }
    return 0;
}

/* PS 2550:0164 ace_kills */
s16 ace_kills(s16 ace) { return fs(ace_rec(ace, 0x8F), 4); }

/* PS 2550:01b2 ace_service */
s8 ace_service(s16 ace) { return fc(ace_rec(ace, 0x98), 6); }

/* PS 2550:0200 ace_duel_date */
u16 ace_duel_date(s16 ace) { return fw(ace_rec(ace, 0xA1), 9); }

/* PS 2550:024e ace_duel_player_type */
s16 ace_duel_player_type(s16 ace) { return fs(ace_rec(ace, 0xAA), 7); }

/* PS 2550:029c ace_first_posting_date (no callers; walks without an end bound) */
u16 ace_first_posting_date(s16 ace, u16 d0, u16 d1)
{
    FarPtr r = ace_rec(ace, 0xB4);
    FarPtr l = far_add(r, 0xB);
    FarPtr e = fp(l, 6);
    for (;;) {
        if (far_is_null(e)) return 0;
        u16 d = fw(e, 0);
        u8 t = fb(e, 2);
        if (d0 <= d && d <= d1 && (t == 0 || t == 1)) return d;
        e.off = (u16)(e.off + 5);
    }
}

/* PS 2550:0338 ace_status(ace, date) — career entry type in force, FFh none */
u8 ace_status(s16 ace, u16 date)
{
    FarPtr r = ace_rec(ace, 0xC3);
    FarPtr e = list_find_dated(far_add(r, 0xB), date);
    return far_is_null(e) ? 0xFF : fb(e, 2);
}

/* PS 2550:03aa ace_squadron(ace, date) */
s16 ace_squadron(s16 ace, u16 date)
{
    FarPtr r = ace_rec(ace, 0xD4);
    FarPtr e = list_find_dated(far_add(r, 0xB), date);
    return (!far_is_null(e) && fb(e, 2) == 0) ? fs(e, 3) : -1;
}

/* PS 2550:0426 ace_posting_station */
s16 ace_posting_station(s16 ace, u16 date)
{
    FarPtr r = ace_rec(ace, 0xEC);
    FarPtr e = list_find_dated(far_add(r, 0xB), date);
    if (far_is_null(e) || fb(e, 2) != 1) sd_fatal(FS_ACE, 0xEF);
    return fs(list_at(ds_ptr(PS_DS_acetab_postings), fs(e, 3)), 0);
}

/* PS 2550:04d9 ace_posting_aircraft */
s16 ace_posting_aircraft(s16 ace, u16 date)
{
    FarPtr r = ace_rec(ace, 0x103);
    FarPtr e = list_find_dated(far_add(r, 0xB), date);
    if (far_is_null(e) || fb(e, 2) != 1) sd_fatal(FS_ACE, 0x106);
    return fs(list_at(ds_ptr(PS_DS_acetab_postings), fs(e, 3)), 3);
}

/* PS 2550:058d ace_read_record(far rec) — list read callback */
static void ace_read_record(FarPtr r)
{
    emd_read_far(r, 2, 1);
    emd_read_far(far_add(r, 2), 2, 1);
    emd_read_far(far_add(r, 4), 2, 1);
    emd_read_far(far_add(r, 6), 1, 1);
    emd_read_far(far_add(r, 7), 2, 1);
    emd_read_date(far_add(r, 9));
    list_read(far_add(r, 0xB), 5, codeptr_far(PS_FN_list_dated_marker));
}

/* PS 2550:064b ace_free_record */
static void ace_free_record(FarPtr r) { list_free(far_add(r, 0xB), far_null()); }

/* PS 2550:0679 ace_open — "acetab" DS:1DCD "dat" DS:1DD4; "ace" DS:1DD8 "dat" DS:1DDC */
s16 ace_open(void)
{
    if (DSC(ACE_LOADED)) return 1;
    emd_open(make_name(0x1DCD, -1, 0x1DD4));
    list_read(ds_ptr(PS_DS_acetab_t1), 4, far_null());
    list_read(ds_ptr(PS_DS_acetab_postings), 6, far_null());
    emd_close(0);
    DSW(PS_DS_ace_db) = (u16)table_load(0x1DD8, 0x1DDC, 0x15, 15, 0x12, 6, ds_ptr(PS_DS_ace_records),
                                        codeptr_far(PS_FN_ace_read_record), codeptr_far(SD_FN(0x2550, 0x064b)));
    if (!DSW(PS_DS_ace_db)) sd_fatal(FS_ACE, 0x13E);
    DSB(ACE_LOADED) = 1;
    return 1;
}

/* PS 2550:073f ace_close */
void ace_close(void)
{
    if (!DSB(ACE_LOADED)) return;
    list_free(ds_ptr(PS_DS_acetab_t1), far_null());
    list_free(ds_ptr(PS_DS_acetab_postings), far_null());
    table_free((s16)DSW(PS_DS_ace_db));
    DSB(ACE_LOADED) = 0;
}

/* PS 2550:0780 acetxt_open — "acetxt" DS:1DE0 "dat" DS:1DE7 */
s16 acetxt_open(void)
{
    if (DSW(PS_DS_acetxt_db)) sd_fatal(FS_ACE, 0x168);
    DSW(PS_DS_acetxt_db) = (u16)table_load(0x1DE0, 0x1DE7, 4, 15, 0x12, 5, far_null(), far_null(), far_null());
    if (!DSW(PS_DS_acetxt_db)) sd_fatal(FS_ACE, 0x172);
    return 1;
}

/* PS 2550:0802 acetxt_close */
void acetxt_close(void)
{
    table_free((s16)DSW(PS_DS_acetxt_db));
    DSW(PS_DS_acetxt_db) = 0;
}

/* PS 2550:0817 acetxt_bio */
FarPtr acetxt_bio(s16 ace)
{
    if (!DSW(PS_DS_acetxt_db)) sd_fatal(FS_ACE, 0x17D);
    return extdb_text((s16)DSW(PS_DS_acetxt_db), ace);
}

/* PS 2550:0855 acetxt_duel_text — the string after the biography */
FarPtr acetxt_duel_text(s16 ace)
{
    if (!DSW(PS_DS_acetxt_db)) sd_fatal(FS_ACE, 0x184);
    FarPtr s = extdb_text((s16)DSW(PS_DS_acetxt_db), ace);
    return far_add(s, (u16)(rtl__fstrlen(s) + 1));
}

/* PS 2550:08b1 ace_cmp_kills(int *a, int *b) — descending kills (qsort comparator) */
static s16 ace_cmp_kills(u16 a_ds, u16 b_ds)
{
    s16 a = (s16)DSW(a_ds), b = (s16)DSW(b_ds);
    if (ace_kills(a) < ace_kills(b)) return 1;
    if (ace_kills(a) == ace_kills(b)) return 0;
    return -1;
}

/* PS 2550:08f8 acelist_build(service, slot, pilot_rows, same_service) */
void acelist_build(s8 service, s8 slot, s8 pilot_rows, s8 same_service)
{
    s8 side = (s8)service_side((u16)(s16)service);
    u8 linked = 0;
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x8C);
    rtl_memset((u16)(PS_DS_acelist_ids + slot * 0x8C), 0, 0x8C);
    rtl_memset((u16)(PS_DS_acelist_enabled + slot * 0x8C), 1, 0x8C);
    rtl_memset(tmp, 0, 0x8C);
    if (pilot_rows) {
        ACL_N(slot) = 4;
        for (s16 i = 0; i < 4; i++) ACL_IDS(slot, i) = (u16)i;
    } else ACL_N(slot) = 0;
    s16 n = 0;
    DSB(PS_DS_acelist_pilot_rows) = (u8)pilot_rows;
    DSB(PS_DS_acelist_total) = (u8)ace_count();
    for (s16 i = 0; i < (s16)DSC(PS_DS_acelist_total); i++) {
        if (!ace_valid(i)) continue;
        if ((s8)service_side((u16)(s16)ace_service(i)) != side) continue;
        s16 link;
        if (same_service) {
            if (ace_service(i) != service) { linked = 0; continue; }
            link = ace_link(i);
        } else {
            link = ace_link(i);
        }
        if (link == -1) { DSW(tmp + n * 2) = (u16)i; n++; continue; }
        linked = 1;
        if (linked) {
            s16 k;
            for (k = 0; k < n && k > -1; k++)
                if ((s16)DSW(tmp + k * 2) == link) { DSW(tmp + k * 2) = (u16)i; k = -2; }
            if (k > -1) { DSW(tmp + n * 2) = (u16)i; n++; }
            linked = 0;
        }
    }
    ACL_N(slot) = (u16)(ACL_N(slot) + n);
    rtl_qsort(tmp, (u16)n, 2, codeptr_far(PS_FN_ace_cmp_kills));
    for (s16 k = 0; k < n; k++) {
        if (pilot_rows) ACL_IDS(slot, 4 + k) = DSW(tmp + k * 2);
        else ACL_IDS(slot, k) = DSW(tmp + k * 2);
    }
    stk_release(m);
}

/* PS 2550:0b00 acelist_disable(code, slot) — starts at 4 even without pilot rows */
void acelist_disable(s16 code, s8 slot)
{
    for (s16 i = 4; i < (s16)ACL_N(slot); i++)
        if ((s16)(ACL_IDS(slot, i) + 4) == code) { ACL_EN(slot, i) = 0; return; }
}

/* PS 2550:0b53 acelist_enabled_count */
s16 acelist_enabled_count(void)
{
    s8 cur = DSC(PS_DS_acelist_slot);
    s16 n = 0;
    for (s16 i = 0; i < (s16)ACL_N(cur); i++) if (ACL_EN(cur, i)) n++;
    return n;
}

/* PS 2550:0b9d acelist_select */
void acelist_select(s8 slot) { DSB(PS_DS_acelist_slot) = (u8)slot; }

/* PS 2550:0ba8 acelist_id */
s16 acelist_id(s16 i) { return (s16)ACL_IDS(DSC(PS_DS_acelist_slot), i); }

/* PS 2550:0bc5 acelist_code(row) */
s16 acelist_code(s16 row)
{
    if (row < 4) return row;
    s8 cur = DSC(PS_DS_acelist_slot);
    s16 j = 0, k = 0;
    while (j < (s16)ACL_N(cur) && (k < row || ACL_EN(cur, j) == 0)) {
        if (ACL_EN(cur, j)) k++;
        j++;
    }
    return (s16)(ACL_IDS(cur, j) + 4);
}

/* PS 2550:0c3b acelist_name (no callers) */
FarPtr acelist_name(s16 i)
{
    s8 cur = DSC(PS_DS_acelist_slot);
    FarPtr s = (DSB(PS_DS_acelist_pilot_rows) && i < 4) ? strings_get(0, (s16)ACL_IDS(cur, i))
                                                        : ace_name((s16)ACL_IDS(cur, i));
    return strpool_copy(s);
}

/* PS 2550:0c99 acelist_row_name (no callers) */
FarPtr acelist_row_name(s16 row)
{
    s8 cur = DSC(PS_DS_acelist_slot);
    s16 j = 0, k = 0;
    while (j < (s16)ACL_N(cur) && ACL_EN(cur, j) == 0) j++;
    while (j < (s16)ACL_N(cur) && (k < row || ACL_EN(cur, j) == 0)) {
        if (ACL_EN(cur, j)) k++;
        j++;
    }
    FarPtr s = (DSB(PS_DS_acelist_pilot_rows) && j < 4) ? strings_get(0, (s16)(ACL_IDS(cur, j) + 0xAD))
                                                        : ace_name((s16)ACL_IDS(cur, j));
    return strpool_copy(s);
}

__attribute__((constructor)) static void ace_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_ace_read_record, (CodeFn)ace_read_record);
    codeptr_register(PROG_PS, SD_FN(0x2550, 0x064b), (CodeFn)ace_free_record);
    codeptr_register(PROG_PS, PS_FN_ace_cmp_kills, (CodeFn)ace_cmp_kills);
}
