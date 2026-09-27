/* shell_data: emd\wdata.c (PS 2322) world database TARGET.n / CARRIER.n / SHIPPATH.n and the tag
 * (name) list (PS 2455), shell_data_emd1.md §2. Records and entries are far nodes (xfarmalloc):
 * record 1Ah bytes {next, u16 id, 5 far list heads at +06+4L}; entry 0Ah bytes {next, u16 date,
 * payload at +06}; tag node 25h bytes {next, u16 id, char name[31]}. */
#include "shell_data/sd_priv.h"
#include "render3d/render3d.h"

#define CUR_REC   0x1CDA        /* far "current record" */
#define CUR_ENT   0x1CDE        /* far "current entry" */
#define PTS_PER   0x1CD8        /* u16 P (INF:) */
#define TAG_HEAD  far_make(SEG(0x4c8c), 0x000C)

/* ---------------------------------------------------------------- builders */

/* PS 2322:000b wd_set_point(i, far src) */
static void wd_set_point(s16 i, FarPtr src)
{
    if (DSW(PS_DS_wd_cur_list) != 0 || far_is_null(ds_far(CUR_REC)) || far_is_null(ds_far(CUR_ENT)))
        sd_fatal(FS_WDATA, 0x3D);
    if (i < 0 || i >= (s16)DSW(PTS_PER)) sd_fatal(FS_WDATA, 0x3E);
    FarPtr pts = fp(ds_far(CUR_ENT), 6);
    for (u16 k = 0; k < 12; k++) fwb(pts, (u16)(i * 12 + k), fb(src, k));
}

/* PS 2322:00a6 wd_set_pair(u16 v[2]) */
static void wd_set_pair(u16 v_ds)
{
    if (DSW(PS_DS_wd_cur_list) != 1 || far_is_null(ds_far(CUR_REC)) || far_is_null(ds_far(CUR_ENT)))
        sd_fatal(FS_WDATA, 0x4B);
    fww(ds_far(CUR_ENT), 6, DSW(v_ds));
    fww(ds_far(CUR_ENT), 8, DSW(v_ds + 2));
}

/* PS 2322:00fc wd_set_value(v) */
static void wd_set_value(u16 v)
{
    if ((s16)DSW(PS_DS_wd_cur_list) <= 1 || far_is_null(ds_far(CUR_REC)) || far_is_null(ds_far(CUR_ENT)))
        sd_fatal(FS_WDATA, 0x58);
    fww(ds_far(CUR_ENT), 6, v);
}

/* PS 2322:0147 wd_new_entry(date) — sorted (stable) into the current list */
static void wd_new_entry(u16 date)
{
    if (far_is_null(ds_far(CUR_REC))) sd_fatal(FS_WDATA, 0x68);
    FarPtr e = xfarmalloc(10);
    fwp(e, 0, far_null());
    fww(e, 4, date);
    if (DSW(PS_DS_wd_cur_list) == 0) fwp(e, 6, xfarmalloc((u32)(u16)(DSW(PTS_PER) * 12)));
    FarPtr rec = ds_far(CUR_REC);
    u16 lo = (u16)(6 + DSW(PS_DS_wd_cur_list) * 4);
    fwp(rec, lo, flist_insert_sorted(fp(rec, lo), e));
    ds_far_wr(CUR_ENT, e);
}

/* PS 2322:0212 wd_delete_entry */
static void wd_delete_entry(void)
{
    FarPtr rec = ds_far(CUR_REC), e = ds_far(CUR_ENT);
    if (far_is_null(rec) || far_is_null(e)) sd_fatal(FS_WDATA, 0x81);
    u16 lo = (u16)(6 + DSW(PS_DS_wd_cur_list) * 4);
    fwp(rec, lo, flist_unlink(fp(rec, lo), e));
    if (DSW(PS_DS_wd_cur_list) == 0) xfarfree(fp(e, 6));
    xfarfree(e);
    ds_far_wr(CUR_ENT, far_null());
}

/* PS 2322:02bd wd_select_list(L) */
static s16 wd_select_list(s16 L)
{
    if (far_is_null(ds_far(CUR_REC)) || L < 0 || L > 5) sd_fatal(FS_WDATA, 0x96);
    DSW(PS_DS_wd_cur_list) = (u16)L;
    ds_far_wr(CUR_ENT, far_null());
    return 1;
}

/* PS 2322:030b wd_new_record(id) */
static void wd_new_record(u16 id)
{
    FarPtr r = xfarmalloc(0x1A);
    fwp(r, 0, far_null());
    fww(r, 4, id);
    for (int L = 0; L < 5; L++) fwp(r, (u16)(6 + L * 4), far_null());
    ds_far_wr(PS_DS_wd_records, flist_append(ds_far(PS_DS_wd_records), r));
    ds_far_wr(CUR_REC, r);
    ds_far_wr(CUR_ENT, far_null());
    DSW(PS_DS_wd_cur_list) = 0;
}

/* ---------------------------------------------------------------- tag list (2455) */

/* PS 2455:0292 tag_find(id, name, list) */
static FarPtr tag_find(u16 id, FarPtr name, FarPtr list)
{
    for (FarPtr n = list; !far_is_null(n); n = fp(n, 0)) {
        if (id && fw(n, 4) == id) return n;
        if (!far_is_null(name) && rtl__fstricmp(far_add(n, 6), name) == 0) return n;
    }
    return far_null();
}

/* PS 2455:000a tag_add(id, name, list) */
static u16 tag_add(u16 id, FarPtr name, FarPtr list)
{
    FarPtr n = tag_find(0, name, list);
    if (!far_is_null(n) && id) { fww(n, 4, id); return id; }
    if (!far_is_null(n)) id = fw(n, 4);
    if (id == 0 && (far_is_null(n) || fw(n, 4) != id)) {
        s16 v = 1;
        FarPtr p = list;
        while (!far_is_null(fp(p, 0))) {
            if ((s16)fw(p, 4) >= v) v = (s16)(fw(p, 4) + 1);
            p = fp(p, 0);
        }
        if ((s16)fw(p, 4) >= v) v = (s16)(fw(p, 4) + 1);
        id = (u16)v;
    }
    if (!far_is_null(n)) { fww(n, 4, id); return id; }
    FarPtr nn = xfarmalloc(0x25);
    fww(nn, 4, id);
    fwp(nn, 0, far_null());
    rtl__fstrcpy(far_add(nn, 6), name);
    FarPtr p = list;
    while (!far_is_null(fp(p, 0))) p = fp(p, 0);
    fwp(p, 0, nn);
    return id;
}

/* PS 2455:0117 tag_remove(id, list) — never the sentinel */
static s16 tag_remove(u16 id, FarPtr list)
{
    FarPtr prev = far_null(), n = list;
    while (!far_is_null(n)) {
        if (fw(n, 4) == id) break;
        prev = n;
        n = fp(n, 0);
    }
    if (far_is_null(n) || far_is_null(prev)) return 0;
    fwp(prev, 0, fp(n, 0));
    xfarfree(n);
    return 1;
}

/* PS 2455:01a0 tag_free_all(list) */
static void tag_free_all(FarPtr list)
{
    if (far_is_null(list)) return;
    FarPtr n = fp(list, 0);
    fwp(list, 0, far_null());
    while (!far_is_null(n)) {
        FarPtr nx = fp(n, 0);
        fwp(n, 0, far_null());
        xfarfree(n);
        n = nx;
    }
}

/* PS 2455:020e tag_name(id, list) */
static FarPtr tag_name(u16 id, FarPtr list)
{
    for (FarPtr n = list; !far_is_null(n); n = fp(n, 0))
        if (fw(n, 4) == id) return far_add(n, 6);
    return far_null();
}

/* PS 2455:024a tag_id(name, list) */
static u16 tag_id(FarPtr name, FarPtr list)
{
    for (FarPtr n = list; !far_is_null(n); n = fp(n, 0))
        if (rtl__fstricmp(far_add(n, 6), name) == 0) return fw(n, 4);
    return 0;
}

/* PS 2455:02f7 tag_load_chunk(file, tag, list) — returns n (0 on failure) */
static s16 tag_load_chunk(u16 f, u16 tag_ds, FarPtr list)
{
    if (!f || !tag_ds || far_is_null(list)) return 0;
    s16 was_open = chunk_is_open(f);
    if (!was_open) f = chunk_open(f);
    if (!f) return 0;
    if (chunk_find(f, tag_ds, 0) == -1 && chunk_find(f, tag_ds, -1) == -1) return 0;
    s16 n = rd_u16(f);
    for (s16 k = n; k; k--) {
        u16 id = (u16)rd_u16(f);
        FarPtr s = rd_cstring(f);
        if (s.off) tag_add(id, ds_ptr(s.off), list);
    }
    if (!was_open) chunk_close(f);
    return n;
}

/* PS 2455:03c3 tag_write(first, f) (no callers) */
static s16 tag_write(FarPtr first, u16 f)
{
    s16 n = 0;
    for (FarPtr p = first; !far_is_null(p); p = fp(p, 0)) if (fw(p, 4)) n++;
    wr_u16((u16)n, f);
    u16 m = stk_mark();
    u16 buf = stk_alloc(32);
    for (FarPtr p = first; !far_is_null(p); p = fp(p, 0)) {
        if (!fw(p, 4)) continue;
        wr_u16(fw(p, 4), f);
        strcpy_far_src(buf, far_add(p, 6));
        wr_cstring(buf, f);
    }
    stk_release(m);
    return n;
}

/* ---------------------------------------------------------------- records */

/* PS 2322:0399 wd_delete_record */
static void wd_delete_record(void)
{
    FarPtr rec = ds_far(CUR_REC);
    if (far_is_null(rec)) sd_fatal(FS_WDATA, 0xBC);
    ds_far_wr(PS_DS_wd_records, flist_unlink(ds_far(PS_DS_wd_records), rec));
    for (s16 L = 0; L < 5; L++) {
        wd_select_list(L);
        while (!far_is_null(fp(rec, (u16)(6 + L * 4)))) {
            ds_far_wr(CUR_ENT, fp(rec, (u16)(6 + L * 4)));
            wd_delete_entry();
        }
    }
    tag_remove(fw(rec, 4), TAG_HEAD);
    xfarfree(rec);
    ds_far_wr(CUR_REC, far_null());
    ds_far_wr(CUR_ENT, far_null());
}

/* PS 2322:0467 wd_close */
void wd_close(void)
{
    while (!far_is_null(ds_far(PS_DS_wd_records))) {
        ds_far_wr(CUR_REC, ds_far(PS_DS_wd_records));
        wd_delete_record();
    }
    tag_free_all(TAG_HEAD);
    ds_far_wr(CUR_REC, far_null());
    ds_far_wr(CUR_ENT, far_null());
    DSW(PTS_PER) = 0;
}

/* PS 2322:04b1 wd_load_database(name) — chunks INF:/DAT:/TAG: (DS:1D92/1DA8/1DAD) */
void wd_load_database(u16 name_ds)
{
    wd_close();
    u16 f = chunk_open(name_ds);
    if (!f) sd_fatal_msg(FS_WDATA, 0x13C, name_ds);
    if (chunk_find(f, 0x1D92, 0) == -1) fatal_error(0x1D97 /* "wd_load_database" */, 0);
    DSW(PTS_PER) = (u16)rd_u16(f);
    if (chunk_find(f, 0x1DA8, 0) == -1) return;              /* quirk: file left open */
    u16 m = stk_mark();
    u16 xyz = stk_alloc(12), pair = stk_alloc(4);
    s16 nrec = rd_u16(f);
    for (s16 r = 0; r < nrec; r++) {
        wd_new_record((u16)rd_u16(f));
        for (s16 L = 0; L < 5; L++) {
            wd_select_list(L);
            s16 m2 = rd_u16(f);
            for (s16 k = 0; k < m2; k++) {
                wd_new_entry((u16)rd_u16(f));
                if (L == 0) {
                    for (s16 i = 0; i < (s16)DSW(PTS_PER); i++) {
                        DSSL(xyz) = rd_s32(f);
                        DSSL(xyz + 4) = rd_s32(f);
                        DSSL(xyz + 8) = rd_s32(f);
                        wd_set_point(i, ds_ptr(xyz));
                    }
                } else if (L == 1) {
                    DSW(pair) = (u16)rd_u16(f);
                    DSW(pair + 2) = (u16)rd_u16(f);
                    wd_set_pair(pair);
                } else {
                    wd_set_value((u16)rd_u16(f));
                }
            }
        }
    }
    stk_release(m);
    tag_load_chunk(f, 0x1DAD, TAG_HEAD);
    chunk_close(f);
    ds_far_wr(CUR_REC, far_null());
    ds_far_wr(CUR_ENT, far_null());
}

/* PS 2322:0678 wd_count */
s16 wd_count(void)
{
    s16 n = 0;
    for (FarPtr p = ds_far(PS_DS_wd_records); !far_is_null(p); p = fp(p, 0)) n++;
    return n;
}

/* PS 2322:06aa wd_name_array (no callers) */
u16 wd_name_array(void)
{
    if (DSW(PS_DS_wd_names)) xfree(DSW(PS_DS_wd_names));
    s16 n = wd_count();
    u16 a = xmalloc((u16)((n + 1) * 4));
    DSW(PS_DS_wd_names) = a;
    s16 i = 0;
    for (FarPtr p = ds_far(PS_DS_wd_records); !far_is_null(p); p = fp(p, 0), i++)
        ds_far_wr((u16)(a + i * 4), tag_name(fw(p, 4), TAG_HEAD));
    ds_far_wr((u16)(a + i * 4), far_null());
    return a;
}

/* PS 2322:0760 wd_list_length */
s16 wd_list_length(void)
{
    s16 n = 0;
    FarPtr rec = ds_far(CUR_REC);
    for (FarPtr p = fp(rec, (u16)(6 + DSW(PS_DS_wd_cur_list) * 4)); !far_is_null(p); p = fp(p, 0)) n++;
    return n;
}

/* PS 2322:07a2 wd_date_strings (no callers): n+1 near pointers then n 10-byte buffers */
u16 wd_date_strings(void)
{
    if (DSW(PS_DS_wd_date_strs)) xfree(DSW(PS_DS_wd_date_strs));
    s16 n = wd_list_length();
    u16 a = xmalloc((u16)(n * 12 + 2));
    DSW(PS_DS_wd_date_strs) = a;
    FarPtr e = fp(ds_far(CUR_REC), (u16)(6 + DSW(PS_DS_wd_cur_list) * 4));
    s16 i;
    for (i = 0; i < n; i++, e = fp(e, 0)) {
        u16 buf = (u16)(a + (n + 1) * 2 + i * 10);
        DSW(a + i * 2) = buf;
        date_text(fw(e, 4), buf);
    }
    DSW(a + i * 2) = 0;
    return a;
}

/* PS 2322:0874 / 087e */
u16 wd_points_per_pos(void) { return DSW(PTS_PER); }
u16 wd_current_list(void) { return DSW(PS_DS_wd_cur_list); }

/* record lookup shared by the queries: by index (advance rec times) or by id */
static void wd_lookup(s16 rec, s8 by_id, u16 line)
{
    ds_far_wr(CUR_REC, ds_far(PS_DS_wd_records));
    if (by_id) {
        while (!far_is_null(ds_far(CUR_REC)) && (s16)fw(ds_far(CUR_REC), 4) != rec)
            ds_far_wr(CUR_REC, fp(ds_far(CUR_REC), 0));
    } else {
        s16 c = rec;
        while (!far_is_null(ds_far(CUR_REC))) {
            s16 a = c--;
            if (!a) break;
            ds_far_wr(CUR_REC, fp(ds_far(CUR_REC), 0));
        }
    }
    if (far_is_null(ds_far(CUR_REC))) sd_fatal(FS_WDATA, line);
}

static bool wd_valid(FarPtr e, u16 date)
{
    if (fw(e, 4) > date) return false;
    FarPtr nx = fp(e, 0);
    return far_is_null(nx) || fw(nx, 4) > date;
}

/* PS 2322:0888 wd_position(rec, date, long *out (near), by_id) — HEADER FIX: out is near */
u16 wd_position(s16 rec, u16 date, u16 out_ds, s8 by_id)
{
    for (int k = 0; k < 12; k += 2) DSW(out_ds + k) = 0xFFFF;
    wd_lookup(rec, by_id, 0x25C);
    for (ds_far_wr(CUR_ENT, fp(ds_far(CUR_REC), 6)); !far_is_null(ds_far(CUR_ENT));
         ds_far_wr(CUR_ENT, fp(ds_far(CUR_ENT), 0))) {
        if (wd_valid(ds_far(CUR_ENT), date)) {
            FarPtr pts = fp(ds_far(CUR_ENT), 6);
            for (u16 k = 0; k < 12; k++) DSB(out_ds + k) = fb(pts, k);
            break;
        }
    }
    return out_ds;
}

/* PS 2322:09d8 wd_position_mid — midpoint (3d50:16b0 lmid) with the next entry's point */
u16 wd_position_mid(s16 rec, u16 date, u16 out_ds, s8 by_id)
{
    for (int k = 0; k < 12; k += 2) DSW(out_ds + k) = 0xFFFF;
    wd_lookup(rec, by_id, 0x2A4);
    for (ds_far_wr(CUR_ENT, fp(ds_far(CUR_REC), 6)); !far_is_null(ds_far(CUR_ENT));
         ds_far_wr(CUR_ENT, fp(ds_far(CUR_ENT), 0))) {
        FarPtr e = ds_far(CUR_ENT);
        if (!wd_valid(e, date)) continue;
        FarPtr nx = fp(e, 0);
        if (!far_is_null(nx)) {
            /* the original passes the far point arrays; they are in far memory, so copy them to
             * the DGROUP stack for the near-pointer lmid (same arithmetic) */
            u16 m = stk_mark();
            u16 a = stk_alloc(12), b = stk_alloc(12);
            FarPtr pa = fp(e, 6), pb = fp(nx, 6);
            for (u16 k = 0; k < 12; k++) { DSB(a + k) = fb(pa, k); DSB(b + k) = fb(pb, k); }
            u16 r = lmid(a, b);
            for (u16 k = 0; k < 12; k++) DSB(out_ds + k) = DSB(r + k);
            stk_release(m);
        } else {
            FarPtr pts = fp(e, 6);
            for (u16 k = 0; k < 12; k++) DSB(out_ds + k) = fb(pts, k);
        }
        break;
    }
    return out_ds;
}

/* PS 2322:0bd5 wd_side_matches(rec, date, side, by_id) */
s8 wd_side_matches(s16 rec, u16 date, s8 side, s8 by_id)
{
    wd_lookup(rec, by_id, 0x2F7);
    for (ds_far_wr(CUR_ENT, fp(ds_far(CUR_REC), 0x12)); !far_is_null(ds_far(CUR_ENT));
         ds_far_wr(CUR_ENT, fp(ds_far(CUR_ENT), 0))) {
        FarPtr e = ds_far(CUR_ENT);
        if (wd_valid(e, date)) {
            if ((s16)side == (s16)fw(e, 6) || fw(e, 6) == 99) return 1;
        }
    }
    return 0;
}

/* PS 2322:0cfc wd_kind(rec, date, zero_if_99, by_id) */
s16 wd_kind(s16 rec, u16 date, s8 zero_if_99, s8 by_id)
{
    wd_lookup(rec, by_id, 0x344);
    for (ds_far_wr(CUR_ENT, fp(ds_far(CUR_REC), 0x0E)); !far_is_null(ds_far(CUR_ENT));
         ds_far_wr(CUR_ENT, fp(ds_far(CUR_ENT), 0))) {
        FarPtr e = ds_far(CUR_ENT);
        if (wd_valid(e, date)) {
            if (zero_if_99 && fw(e, 6) == 99) return 0;
            if (fw(e, 6) != 99) return (s16)fw(e, 6);
        }
    }
    return -1;
}

/* PS 2322:0e2f wd_id_by_name (no callers) */
u16 wd_id_by_name(FarPtr name) { return tag_id(name, TAG_HEAD); }

/* PS 2322:0e52 wd_record_id(index) */
s16 wd_record_id(s16 index)
{
    wd_lookup(index, 0, 0x392);
    return (s16)fw(ds_far(CUR_REC), 4);
}

/* PS 2322:0ebf wd_service_allowed(code, service) — set table DS:1CCE */
static s8 wd_service_allowed(s16 code, s8 service)
{
    if (code == 99) return 1;
    if (code >= 20) return DSB((u16)(0x1CCE + (s16)service * 16 + code)) != 0;
    return (s16)service == code;
}

/* PS 2322:0efd wd_find_for_service(date, service, skip) — id of the skip-th usable record */
s16 wd_find_for_service(u16 date, s8 service, s16 skip)
{
    s16 k = 0;
    for (ds_far_wr(CUR_REC, ds_far(PS_DS_wd_records)); !far_is_null(ds_far(CUR_REC));
         ds_far_wr(CUR_REC, fp(ds_far(CUR_REC), 0))) {
        bool found = false;
        for (ds_far_wr(CUR_ENT, fp(ds_far(CUR_REC), 0x16)); !far_is_null(ds_far(CUR_ENT));
             ds_far_wr(CUR_ENT, fp(ds_far(CUR_ENT), 0))) {
            FarPtr e = ds_far(CUR_ENT);
            if (!wd_valid(e, date)) continue;
            if (wd_service_allowed((s16)fw(e, 6), service) && k++ >= skip) found = true;
            break;
        }
        if (found) return (s16)fw(ds_far(CUR_REC), 4);
    }
    return -1;
}

/* PS 2322:0fe0 wd_name_by_id (no callers) */
FarPtr wd_name_by_id(u16 id) { return tag_name(id, TAG_HEAD); }

/* PS 2322:0ffa wd_find_near (no callers). PORT: not ported — the original passes only the offset
 * of the far point array to a near box test (shell_data_emd1.md §2.4 "Do not port"). */
s16 wd_find_near(u16 pos_ds, u16 date) { return 0; }

/* silence "defined but not used" for the uncalled originals kept for completeness */
void wdata_unused_refs(void) { (void)tag_write; }
