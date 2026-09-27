/* shell_data: emd\date.c (PS 284f), emd\campaign.c (287b), emd\missinfo.c (28e4), emd\wldfgen.c
 * (2968), shell_data_emd2.md §5-8. */
#include "shell_data/sd_priv.h"

/* ================================================================================================
 * 284f date.c — day numbers since 1 Jan 1900 (base DS:1E52 = 2415033), month lengths DS:1E5C
 * ================================================================================================ */

#define MLEN(m) DSC((u16)(0x1E5C + (m)))

/* PS 284f:0007 date_make(far {month 0..11, day, year-1900}) */
u16 date_make(FarPtr mdy)
{
    s16 y = (s16)(fc(mdy, 2) + 0x19D4);
    if (y < 0) sd_fatal(FS_DATE, 0x37);
    DSB(0x1E5D) = (y % 4 == 0) ? 29 : 28;
    s8 m = fc(mdy, 0), d = fc(mdy, 1);
    if (m < 0 || m > 11 || d < 1 || MLEN(m) < d) sd_fatal(FS_DATE, 0x3E);
    s16 days = d;
    for (s16 i = 0; i < m; i++) days = (s16)(days + MLEN(i));
    s32 t = 365L * y + days + (s16)((y - 1) / 4);
    return (u16)((u16)t - DSW(0x1E52));
}

/* PS 284f:0109 date_weekday (uncalled) */
u16 date_weekday(u16 date) { return (u16)(((u32)date + DSL(0x1E52) + 1) % 7); }

/* PS 284f:0131 date_split(date, far out[3]) -> out */
FarPtr ps_date_split(u16 date, FarPtr out)
{
    u32 L = (u32)date + DSL(0x1E52);
    if ((s32)L < 0) sd_fatal(FS_DATE, 0x5F);
    s16 y = (s16)((s32)(L << 2) / 1461);
    s16 d = (s16)((u16)L - (u16)(365L * y));
    d = (s16)(d - (s16)((y - 1) / 4));
    s16 hi = (s16)(L >> 16);
    if (!(hi > 0 || (hi == 0 && (u16)L > 0x16D))) d++;
    DSB(0x1E5D) = (y % 4 == 0) ? 29 : 28;
    s16 m = 0;
    while (d > 0) { d = (s16)(d - MLEN(m)); m++; }
    m--;
    fwb(out, 0, (u8)m);
    fwb(out, 1, (u8)((u8)d + (u8)MLEN(m)));
    fwb(out, 2, (u8)((u8)y + 0x2C));
    return out;
}

/* PS 284f:021c date_text(date, buf) — "%s %d, %d" (DS:1F10), upper case: "DEC. 7, 1941" */
FarPtr date_text(u16 date, u16 buf_ds)
{
    u16 m = stk_mark();
    u16 t = stk_alloc(4);
    ps_date_split(date, ds_ptr(t));
    rtl_sprintf(buf_ds, 0x1F10, (unsigned)DSW((u16)(0x1E68 + (s16)DSC(t) * 2)),
                (unsigned)(s16)DSC(t + 1), (unsigned)(s16)((s16)DSC(t + 2) + 0x76C));
    str_upper(buf_ds);
    stk_release(m);
    return ds_ptr(buf_ds);
}

/* PS 284f:026b date_text_short — "%s %d, '%d" (DS:1F1A) */
FarPtr date_text_short(u16 date, u16 buf_ds)
{
    u16 m = stk_mark();
    u16 t = stk_alloc(4);
    ps_date_split(date, ds_ptr(t));
    rtl_sprintf(buf_ds, 0x1F1A, (unsigned)DSW((u16)(0x1E80 + (s16)DSC(t) * 2)),
                (unsigned)(s16)DSC(t + 1), (unsigned)(s16)DSC(t + 2));
    str_upper(buf_ds);
    stk_release(m);
    return ds_ptr(buf_ds);
}

/* PS 284f:02b7 date_vj_day (uncalled) */
u16 date_vj_day(void) { return date_make(ds_ptr(0x1E59)); }

/* ================================================================================================
 * 287b campaign.c — CMP<n>.DAT (slot DS:5EFF, list DS:5EF4), CMPTEXT<n>.DAT (slot DS:1F3D)
 * ================================================================================================ */

#define CMP_LOADED 0x1F3C
#define CMPDB ((s16)DSW(PS_DS_campaign_table))

static FarPtr cmp_rec(s16 id, u16 line)
{
    FarPtr r = table_rec(CMPDB, id);
    if (far_is_null(r)) sd_fatal(FS_CAMPAIGN, line);
    return r;
}

/* PS 287b:0009 campaign_read_cb */
static void campaign_read_cb(FarPtr r)
{
    emd_read_far(r, 2, 1);
    emd_read_far(far_add(r, 2), 1, 1);
    emd_read_far(far_add(r, 3), 2, 1);
    emd_read_far(far_add(r, 5), 3, 1);
    emd_read_far(far_add(r, 8), 3, 1);
    u16 m = stk_mark();
    u16 n = stk_alloc(2);
    emd_read(n, 2, 1);
    u16 p = xmalloc((u16)((DSW(n) + 1) * 2));
    fww(r, 0xB, p);
    DSW(p) = DSW(n);
    emd_read((u16)(p + 2), 2, (s16)DSW(n));
    stk_release(m);
}

/* PS 287b:00e1 campaign_free_cb */
static void campaign_free_cb(FarPtr r) { xfree(fw(r, 0xB)); }

/* PS 287b:0104 campaign_load(set) — "cmp" DS:1F3F "dat" DS:1F43 */
void campaign_load(s8 set)
{
    if (set <= -1 && DSC(CMP_LOADED)) sd_fatal(FS_CAMPAIGN, 0x66);
    DSB(PS_DS_campaign_all) = ((u8)set == 0xFF) ? 1 : 0;
    if (!DSC(CMP_LOADED)) {
        u8 mode = (u8)((DSB(PS_DS_campaign_all) ? 0x10 : 0x08) | 2);
        DSW(PS_DS_campaign_table) = (u16)table_load(0x1F3F, 0x1F43, 0x0D, 10, mode, 6, ds_ptr(PS_DS_campaign_list),
                                                    codeptr_far(SD_FN(0x287b, 0x0009)), codeptr_far(SD_FN(0x287b, 0x00e1)));
        if (!DSW(PS_DS_campaign_table)) sd_fatal(FS_CAMPAIGN, 0x76);
        DSW(PS_DS_campaign_total_ids) = (u16)table_count(CMPDB, PS_DS_campaign_file_counts);
    }
    if (!DSC(PS_DS_campaign_all)) {
        DSW(PS_DS_campaign_set_count) = DSW((u16)(PS_DS_campaign_file_counts + (s16)set * 2));
        DSW(PS_DS_campaign_first_id) = set ? (u16)(DSW(PS_DS_campaign_file_counts) + (u16)(((s16)set - 1) * 10)) : 0;
        if (DSW(PS_DS_campaign_set_count)) {
            FarPtr l = table_file_data(CMPDB, set);
            ds_far_wr(PS_DS_campaign_items, fp(l, 6));
        } else ds_far_wr(PS_DS_campaign_items, far_null());
    }
    DSB(CMP_LOADED) = 1;
}

/* PS 287b:024c campaign_loaded */
s8 campaign_loaded(void) { return DSC(CMP_LOADED); }

/* PS 287b:0256 campaign_count */
s16 campaign_count(void)
{
    return (s16)(DSB(PS_DS_campaign_all) ? DSW(PS_DS_campaign_total_ids) : DSW(PS_DS_campaign_set_count));
}

/* PS 287b:026e campaign_name — string pool copy */
FarPtr campaign_name(s16 id)
{
    FarPtr r = cmp_rec(id, 0xB7);
    return strpool_copy(table_text(CMPDB, id, (u32)(s32)fs(r, 0)));
}

/* PS 287b:02d7 campaign_b02 (uncalled) */
u8 campaign_b02(s16 id) { return fb(cmp_rec(id, 0xC6), 2); }

/* PS 287b:0325 campaign_has_service */
s8 campaign_has_service(s16 id, s8 s)
{
    FarPtr r = cmp_rec(id, 0xD5);
    return (DSW((u16)(0x1F26 + (s16)s * 2)) & fw(r, 3)) != 0;
}

/* PS 287b:0389 campaign_start / 03e1 campaign_end */
u16 campaign_start(s16 id) { return date_make(far_add(cmp_rec(id, 0xE8), 5)); }
u16 campaign_end(s16 id) { return date_make(far_add(cmp_rec(id, 0xF8), 8)); }

/* PS 287b:0439 campaign_has_squad — scans all n entries (near list at +0B) */
s8 campaign_has_squad(s16 id, s16 sq)
{
    FarPtr r = cmp_rec(id, 0x10A);
    u16 p = fw(r, 0xB);
    s8 f = 0;
    s16 n = (s16)DSW(p);
    for (s16 i = 1; i <= n; i++) if ((s16)DSW(p + i * 2) == sq) f = 1;
    return f;
}

/* PS 287b:04b5 campaign_available_after(date, s) */
s16 campaign_available_after(u16 date, s8 s)
{
    if (DSC(PS_DS_campaign_all)) sd_fatal(FS_CAMPAIGN, 0x11D);
    for (s16 i = 0; i < (s16)DSW(PS_DS_campaign_set_count); i++) {
        s16 id = (s16)(i + DSW(PS_DS_campaign_first_id));
        if (campaign_start(id) >= date && campaign_has_service(id, s)) return 1;
    }
    return 0;
}

/* PS 287b:051f campaign_exists */
s16 campaign_exists(s16 id) { return !far_is_null(table_rec(CMPDB, id)); }

/* PS 287b:053f campaign_first */
s16 campaign_first(void) { return DSB(PS_DS_campaign_all) ? 0 : (s16)DSW(PS_DS_campaign_first_id); }

/* PS 287b:0556 campaign_close */
void campaign_close(void)
{
    table_free(CMPDB);
    DSB(CMP_LOADED) = 0;
}

/* PS 287b:056a cmptext_open — "cmptext" DS:1F47 "dat" DS:1F4F */
s16 cmptext_open(void)
{
    if (DSW(0x1F3D)) sd_fatal(FS_CAMPAIGN, 0x159);
    DSW(0x1F3D) = (u16)table_load(0x1F47, 0x1F4F, 4, 10, 0x0A, 5, far_null(), far_null(), far_null());
    if (!DSW(0x1F3D)) sd_fatal(FS_CAMPAIGN, 0x163);
    return 1;
}

/* PS 287b:05ec cmptext_intro */
FarPtr cmptext_intro(s16 id)
{
    if (!DSW(0x1F3D)) sd_fatal(FS_CAMPAIGN, 0x16E);
    return extdb_text((s16)DSW(0x1F3D), id);
}

/* PS 287b:062a cmptext_end — the second string */
FarPtr cmptext_end(s16 id)
{
    if (!DSW(0x1F3D)) sd_fatal(FS_CAMPAIGN, 0x17E);
    FarPtr p = extdb_text((s16)DSW(0x1F3D), id);
    return far_add(p, (u16)(rtl__fstrlen(p) + 1));
}

/* PS 287b:0686 cmptext_close */
void cmptext_close(void)
{
    table_free((s16)DSW(0x1F3D));
    DSW(0x1F3D) = 0;
}

/* ================================================================================================
 * 28e4 missinfo.c — MISSINF<n>.DAT list DS:5F08 (slot DS:5F14), end date DS:5F12
 * ================================================================================================ */

#define MIDB ((s16)DSW(PS_DS_missinf_table))

/* PS 28e4:000b missinf_read_cb */
static void missinf_read_cb(FarPtr r)
{
    FarPtr mk = codeptr_far(PS_FN_list_dated_marker);
    emd_read_far(r, 1, 1);
    list_read(far_add(r, 0x01), 7, mk);
    list_read(far_add(r, 0x0B), 0xB, mk);
    list_read(far_add(r, 0x15), 0xB, mk);
    list_read(far_add(r, 0x1F), 0x19, mk);
    list_read(far_add(r, 0x29), 6, mk);
}

/* PS 28e4:00c6 missinf_free_cb */
static void missinf_free_cb(FarPtr r)
{
    list_free(far_add(r, 0x01), far_null());
    list_free(far_add(r, 0x0B), far_null());
    list_free(far_add(r, 0x15), far_null());
    list_free(far_add(r, 0x1F), far_null());
    list_free(far_add(r, 0x29), far_null());
}

/* PS 28e4:0152 missinf_area */
s8 missinf_area(s16 i)
{
    FarPtr r = table_rec(MIDB, i);
    if (far_is_null(r)) sd_fatal(FS_MISSINFO, 0x55);
    return fc(r, 0);
}

/* PS 28e4:019f missinf_open — "missinf" DS:1F55 "dat" DS:1F5D */
void missinf_open(void)
{
    if (!DSC(0x1F54)) {
        DSW(PS_DS_missinf_table) = (u16)table_load(0x1F55, 0x1F5D, 0x33, 0, 0x12, 2, ds_ptr(PS_DS_missinf_list),
                                                   codeptr_far(SD_FN(0x28e4, 0x000b)), codeptr_far(SD_FN(0x28e4, 0x00c6)));
        if (!DSW(PS_DS_missinf_table)) sd_fatal(FS_MISSINFO, 0x6F);
        DSB(0x1F54) = 1;
    }
    DSW(PS_DS_missinf_enddate) = (u16)enddate_get((s8)module_active(), 1);
}

/* PS 28e4:0222 missinf_count */
s16 missinf_count(void) { return table_count(MIDB, 0); }

/* PS 28e4:0237 missinf_find(area) -> record index or -1 */
s16 missinf_find(s8 area)
{
    s16 n = (s16)(s8)missinf_count();
    for (s16 i = 0; i < n; i++) if (missinf_area(i) == area) return i;
    return -1;
}

/* The record of a failed missinf_find (-1) is NULL: the lists are then read from the IVT, as in the
 * original (table_rec returns 0000:0000). */
static FarPtr mi_list(s16 i, u16 off) { return far_add(table_rec(MIDB, i), off); }

static s8 mi_pick(FarPtr e, s16 n)
{
    u16 m = stk_mark();
    u16 w = stk_alloc((u16)(n * 2));
    for (s16 k = 0; k < n; k++) DSW(w + k * 2) = fw(e, (u16)(2 + k * 2));
    s8 r = rng_pick_weighted(w, n);
    stk_release(m);
    return r;
}

/* PS 28e4:0270 mi_target_kind(i, service, date) — RNG 2 weights (0 ships, 1 land) */
s8 mi_target_kind(s16 i, s8 service, u16 date)
{
    int f;
    FarPtr e = keyed_lookup(mi_list(i, 0x01), 6, (u8)service, date, DSW(PS_DS_missinf_enddate), &f);
    return mi_pick(e, 2);
}

/* PS 28e4:03a2 mi_role_land — L3 (+15), key +0A, 4 weights */
s8 mi_role_land(s16 i, s8 service, u16 date)
{
    int f;
    FarPtr e = keyed_lookup(mi_list(i, 0x15), 0xA, (u8)service, date, DSW(PS_DS_missinf_enddate), &f);
    return mi_pick(e, 4);
}

/* PS 28e4:04da mi_role_ships — L2 (+0B), key +0A, 4 weights */
s8 mi_role_ships(s16 i, s8 service, u16 date)
{
    int f;
    FarPtr e = keyed_lookup(mi_list(i, 0x0B), 0xA, (u8)service, date, DSW(PS_DS_missinf_enddate), &f);
    return mi_pick(e, 4);
}

/* PS 28e4:0612 mi_enemy_service — L4 (+1F), key +18 (side group), 11 weights */
s8 mi_enemy_service(s16 i, s8 side, u16 date)
{
    int f;
    FarPtr e = keyed_lookup(mi_list(i, 0x1F), 0x18, (u8)side, date, DSW(PS_DS_missinf_enddate), &f);
    return mi_pick(e, 11);
}

/* PS 28e4:074a mi_ground_weights(i, date, u8 out[4]) — no key, no RNG */
void mi_ground_weights(s16 i, u16 date, u16 out4_ds)
{
    FarPtr l = mi_list(i, 0x29);
    u16 endd = DSW(PS_DS_missinf_enddate);
    FarPtr cur = list_first(l);
    FarPtr nxt = ps_list_next(l);
    u16 end = far_is_null(nxt) ? endd : fw(nxt, 0);
    for (s16 k = 0; k < (s16)fw(l, 2); k++) {
        if (fw(cur, 0) <= date && date <= end) break;
        if (end == endd) break;
        cur = nxt;
        nxt = ps_list_next(l);
        end = far_is_null(nxt) ? endd : fw(nxt, 0);
    }
    for (u16 k = 0; k < 4; k++) DSB(out4_ds + k) = fb(cur, (u16)(2 + k));
}

/* PS 28e4:0820 missinf_close */
void missinf_close(void)
{
    if (!DSB(0x1F54)) return;
    list_free(ds_ptr(PS_DS_missinf_list), codeptr_far(SD_FN(0x28e4, 0x00c6)));
    DSB(0x1F54) = 0;
}

/* ================================================================================================
 * 2968 wldfgen.c — WLDFGEN<n>.DAT list DS:5F16 (slot DS:5F20), end date DS:5F22
 * ================================================================================================ */

#define WFDB ((s16)DSW(PS_DS_wldfgen_table))

/* PS 2968:0006 wldfgen_read_cb */
static void wldfgen_read_cb(FarPtr r)
{
    emd_read_far(r, 1, 1);
    list_read(far_add(r, 1), 0x17, codeptr_far(PS_FN_list_dated_marker));
}

/* PS 2968:0049 wldfgen_free_cb */
static void wldfgen_free_cb(FarPtr r) { list_free(far_add(r, 1), far_null()); }

/* PS 2968:0075 wldfgen_open — "wldfgen" DS:1F6E "dat" DS:1F76 */
void wldfgen_open(void)
{
    if (!DSC(0x1F62)) {
        DSW(PS_DS_wldfgen_table) = (u16)table_load(0x1F6E, 0x1F76, 0x0B, 10, 0x12, 2, ds_ptr(PS_DS_wldfgen_list),
                                                   codeptr_far(SD_FN(0x2968, 0x0006)), codeptr_far(SD_FN(0x2968, 0x0049)));
        if (!DSW(PS_DS_wldfgen_table)) sd_fatal(FS_WLDFGEN, 0x59);
        DSB(0x1F62) = 1;
    }
    DSW(PS_DS_wldfgen_enddate) = (u16)enddate_get((s8)module_active(), 1);
}

/* PS 2968:00f9 wf_count */
s16 wf_count(void) { return table_count(WFDB, 0); }

/* PS 2968:010e wf_area */
s8 wf_area(s16 i)
{
    FarPtr r = table_rec(WFDB, i);
    if (far_is_null(r)) sd_fatal(FS_WLDFGEN, 0x7F);
    return fc(r, 0);
}

/* PS 2968:015b wf_aircraft(i, service, role, date) — RNG over 3 or 2 weights */
s8 wf_aircraft(s16 i, s8 service, s8 role, u16 date)
{
    u16 m = stk_mark();
    u16 w = stk_alloc(6);
    for (int k = 0; k < 6; k++) DSB(w + k) = DSB(0x1F63 + k);
    FarPtr r = table_rec(WFDB, i);
    if (far_is_null(r)) sd_fatal(FS_WLDFGEN, 0x9C);
    int f;
    FarPtr e = keyed_lookup(far_add(r, 1), 0x16, (u8)service, date, DSW(PS_DS_wldfgen_enddate), &f);
    s16 start = (s16)DSC((u16)(0x1F69 + role));
    s16 off = start > 0 ? (s16)(start * 2 + 2) : (s16)(start + 3);
    s16 n = (s16)(s8)(u8)(DSB((u16)(0x1F6A + role)) - DSB((u16)(0x1F69 + role)));
    for (s16 k = 0; k < n; k++) DSW(w + k * 2) = (u16)(s16)fc(e, (u16)(off + k + 2));
    s8 c = rng_pick_weighted(w, n);
    s8 t = fc(e, (u16)(off - n + c + 2));
    stk_release(m);
    return t;
}

/* PS 2968:0313 wf_quality(i, service, date) */
s8 wf_quality(s16 i, s8 service, u16 date)
{
    FarPtr r = table_rec(WFDB, i);
    if (far_is_null(r)) sd_fatal(FS_WLDFGEN, 0x101);
    int f;
    FarPtr e = keyed_lookup(far_add(r, 1), 0x16, (u8)service, date, DSW(PS_DS_wldfgen_enddate), &f);
    return fc(e, 0x14);
}

/* PS 2968:0446 wldfgen_close */
void wldfgen_close(void)
{
    if (!DSB(0x1F62)) return;
    table_free(WFDB);
    DSB(0x1F62) = 0;
}

__attribute__((constructor)) static void campaign_codeptrs(void)
{
    codeptr_register(PROG_PS, SD_FN(0x287b, 0x0009), (CodeFn)campaign_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x287b, 0x00e1), (CodeFn)campaign_free_cb);
    codeptr_register(PROG_PS, SD_FN(0x28e4, 0x000b), (CodeFn)missinf_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x28e4, 0x00c6), (CodeFn)missinf_free_cb);
    codeptr_register(PROG_PS, SD_FN(0x2968, 0x0006), (CodeFn)wldfgen_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x2968, 0x0049), (CodeFn)wldfgen_free_cb);
}
