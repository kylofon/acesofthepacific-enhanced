/* shell_data: emd\squad.c (PS 2624) squadrons, shell_data_emd1.md §6.
 * SQUADTAB.DAT lists DS:5AAE (unused) / DS:5AA4 (mission-type weight rows, 7 s8); SQUAD<n>.DAT
 * extens slot DS:5AC2 (record list DS:5AB8, 2Dh bytes). The accessors do not check the record for
 * NULL: as in the original, a missing record/element reads the real-mode IVT at 0000:0000. */
#include "shell_data/sd_priv.h"

#define SQ_LOADED 0x1DEC
#define SQDB ((s16)DSW(PS_DS_squad_db))

static FarPtr sq_rec(s16 sq) { return table_rec(SQDB, sq); }

/* PS 2624:000d squad_valid */
s8 squad_valid(s16 sq) { return !far_is_null(sq_rec(sq)); }

/* PS 2624:002d squad_count */
s16 squad_count(void) { return table_count(SQDB, 0); }

/* PS 2624:0042 squad_name */
FarPtr squad_name(s16 sq)
{
    FarPtr r = sq_rec(sq);
    return table_text(SQDB, sq, (u32)(s32)fs(r, 0));
}

/* PS 2624:007b squad_name2 — the second string */
FarPtr squad_name2(s16 sq)
{
    FarPtr s = squad_name(sq);
    return far_add(s, (u16)(rtl__fstrlen(s) + 1));
}

/* PS 2624:00a8 / 00cf / 00f6 */
u8 squad_service(s16 sq) { return fb(sq_rec(sq), 2); }
u8 squad_role(s16 sq)    { return fb(sq_rec(sq), 3); }
u8 squad_quality(s16 sq) { return fb(sq_rec(sq), 4); }

/* PS 2624:011d squad_status(sq, date) */
u8 squad_status(s16 sq, u16 date)
{
    FarPtr e = list_find_dated(far_add(sq_rec(sq), 5), date);
    return far_is_null(e) ? 0xFF : fb(e, 2);
}

/* PS 2624:0293 squad_find_mission(list, date, after_time) */
static FarPtr squad_find_mission(FarPtr l, u16 date, s16 after_time)
{
    s8 last = (s8)fb(l, 2);
    FarPtr cur = fp(l, 6);
    if (far_is_null(cur) || fw(cur, 0) > date) return far_null();
    u16 es = fw(l, 0);
    FarPtr nxt = far_add(cur, es);
    last--;
    s8 i, res = last;
    for (i = 0; i < last; i++) {
        u16 nd = fw(nxt, 0);
        if (nd > date) { res = i; break; }
        if (nd == date && fw(cur, 0) == date && fs(nxt, 5) != -1 &&
            (after_time == -1 || fs(nxt, 5) > after_time)) { res = i; break; }
        cur = nxt;
        nxt = far_add(nxt, es);
    }
    fww(l, 4, (u16)(s16)res);
    return cur;
}

/* PS 2624:0163 squad_next_mission_date(sq, d0, d1) */
u16 squad_next_mission_date(s16 sq, u16 d0, u16 d1)
{
    FarPtr r = sq_rec(sq);
    if (squad_status(sq, d0) == 0) {
        FarPtr e = list_find_dated(far_add(r, 0x23), d0);
        if (!far_is_null(e) && fc(e, 2) > 1) return d0;
    }
    FarPtr e;
    for (e = list_first(far_add(r, 0x23)); !far_is_null(e); e = ps_list_next(far_add(r, 0x23))) {
        u16 d = fw(e, 0);
        if (!(d < d0 || d > d1)) break;
    }
    return far_is_null(e) ? 0 : fw(e, 0);
}

/* PS 2624:0219 squad_aircraft(sq, date) — no NULL check */
s16 squad_aircraft(s16 sq, u16 date)
{
    return fs(list_find_dated(far_add(sq_rec(sq), 0x0F), date), 2);
}

/* PS 2624:0256 squad_station(sq, date) — no NULL check */
s16 squad_station(s16 sq, u16 date)
{
    return fs(list_find_dated(far_add(sq_rec(sq), 0x19), date), 2);
}

/* PS 2624:035d squad_pick_mission_type(sq, date, after_time) — RNG: one rng_pick_weighted(7) */
u8 squad_pick_mission_type(s16 sq, u16 date, s16 after_time)
{
    FarPtr cur = squad_find_mission(far_add(sq_rec(sq), 0x23), date, after_time);
    if (fs(cur, 5) != -1) return 0xFF;
    FarPtr row = far_add(ds_far(PS_DS_squadtab_weights + 6), (u16)(fs(cur, 3) * 7));
    u16 m = stk_mark();
    u16 w = stk_alloc(14);
    for (int k = 0; k < 7; k++) DSW(w + k * 2) = (u16)(s16)fc(row, (u16)k);
    s8 k = rng_pick_weighted(w, 7);
    stk_release(m);
    return rd8(SEG(0x4c9a), (u16)(0x0C + k));
}

/* PS 2624:03fb squad_mission_param(sq, date, after_time) */
s16 squad_mission_param(s16 sq, u16 date, s16 after_time)
{
    FarPtr cur = squad_find_mission(far_add(sq_rec(sq), 0x23), date, after_time);
    return fs(cur, 3);
}

/* the status element after d (list_find_dated then list_next while e.date <= d) */
static FarPtr status_after(FarPtr r, u16 d)
{
    FarPtr e = list_find_dated(far_add(r, 5), d);
    while (!far_is_null(e) && fw(e, 0) <= d) e = ps_list_next(far_add(r, 5));
    return e;
}

/* PS 2624:043a squad_sched_init(sq, sched *s, date) */
void squad_sched_init(s16 sq, u16 s_ds, u16 date)
{
    FarPtr r = sq_rec(sq);
    DSW(s_ds) = DSW(s_ds + 4) = date;
    if (squad_status(sq, DSW(s_ds)) != 0) return;
    FarPtr cur = squad_find_mission(far_add(r, 0x23), DSW(s_ds), (s16)DSW(s_ds + 2));
    if (far_is_null(cur)) return;
    DSB(s_ds + 8) = fb(cur, 2);
    FarPtr nxt = ps_list_next(far_add(r, 0x23));
    FarPtr e = !far_is_null(nxt) ? status_after(r, date) : list_last(far_add(r, 5));
    u16 end = (!far_is_null(nxt) && fw(nxt, 0) <= fw(e, 0)) ? fw(nxt, 0) : fw(e, 0);
    u16 cd = fw(cur, 0);
    if (cd != end) {
        if (cd < date)
            DSB(s_ds + 8) = (u8)(s32)((s32)(u16)(end - date) * (s32)fc(cur, 2) / (s32)(u16)(end - cd));
        DSW(s_ds + 6) = DSC(s_ds + 8) ? div16_retry((u16)(end - date), (u16)(s16)DSC(s_ds + 8), NULL) : 0;
    }
    if (DSB(s_ds + 8)) DSB(s_ds + 8)--;
}

/* PS 2624:05a6 squad_sched_next(sq, sched *s) — 1 if a mission was scheduled */
s8 squad_sched_next(s16 sq, u16 s_ds)
{
    FarPtr r = sq_rec(sq);
    if (squad_status(sq, DSW(s_ds)) != 0) return 0;
    FarPtr cur = squad_find_mission(far_add(r, 0x23), DSW(s_ds), (s16)DSW(s_ds + 2));
    if (far_is_null(cur)) return 0;
    u16 d = DSW(s_ds);
    while (DSC(s_ds + 8) <= 0) {
        cur = ps_list_next(far_add(r, 0x23));
        if (far_is_null(cur)) break;
        d = fw(cur, 0);
        DSW(s_ds + 2) = fw(cur, 5);
        DSB(s_ds + 8) = fb(cur, 2);
    }
    if (far_is_null(cur) || DSC(s_ds + 8) <= 0 || squad_status(sq, d) != 0) {
        DSB(s_ds + 8) = 0;
        DSW(s_ds) = d;
        FarPtr e = status_after(r, d);
        if (far_is_null(e)) e = list_last(far_add(r, 5));
        DSW(s_ds + 4) = fw(e, 0);
        return 0;
    }
    if (DSW(s_ds + 4) >= d) {
        DSW(s_ds + 4) = (u16)(DSW(s_ds + 4) + DSW(s_ds + 6));
    } else {
        DSW(s_ds + 4) = d;
        FarPtr nxt = ps_list_next(far_add(r, 0x23));
        FarPtr e = !far_is_null(nxt) ? status_after(r, d) : list_last(far_add(r, 5));
        u16 left = (u16)(s16)DSC(s_ds + 8);
        if (far_is_null(nxt) || (fw(e, 0) >= d && fw(e, 0) < fw(nxt, 0)))
            DSW(s_ds + 6) = DSC(s_ds + 8) ? div16_retry((u16)(fw(e, 0) - d), left, NULL) : 0;
        else
            DSW(s_ds + 6) = DSC(s_ds + 8) ? div16_retry((u16)(fw(nxt, 0) - d), left, NULL) : 0;
    }
    DSW(s_ds) = d;
    DSB(s_ds + 8)--;
    return 1;
}

/* PS 2624:07a5 squad_read_record */
static void squad_read_record(FarPtr r)
{
    FarPtr mk = codeptr_far(PS_FN_list_dated_marker);
    emd_read_far(r, 2, 1);
    emd_read_far(far_add(r, 2), 1, 1);
    emd_read_far(far_add(r, 3), 1, 1);
    emd_read_far(far_add(r, 4), 1, 1);
    list_read(far_add(r, 0x05), 3, mk);
    list_read(far_add(r, 0x0F), 4, mk);
    list_read(far_add(r, 0x19), 4, mk);
    list_read(far_add(r, 0x23), 7, mk);
}

/* PS 2624:088c squad_free_record */
static void squad_free_record(FarPtr r)
{
    list_free(far_add(r, 0x05), far_null());
    list_free(far_add(r, 0x0F), far_null());
    list_free(far_add(r, 0x19), far_null());
    list_free(far_add(r, 0x23), far_null());
}

/* PS 2624:0902 squad_open — "squadtab" DS:1DED "dat" DS:1DF6; "squad" DS:1DFA "dat" DS:1E00 */
s16 squad_open(void)
{
    if (DSC(SQ_LOADED)) return 1;
    emd_open(make_name(0x1DED, -1, 0x1DF6));
    list_read(ds_ptr(PS_DS_squadtab_t1), 4, far_null());
    list_read(ds_ptr(PS_DS_squadtab_weights), 7, far_null());
    emd_close_rest_far();
    DSW(PS_DS_squad_db) = (u16)table_load(0x1DFA, 0x1E00, 0x2D, 20, 0x12, 6, ds_ptr(PS_DS_squad_records),
                                          codeptr_far(PS_FN_squad_read_record), codeptr_far(SD_FN(0x2624, 0x088c)));
    if (!DSW(PS_DS_squad_db)) sd_fatal(FS_SQUAD, 0x20C);
    DSB(SQ_LOADED) = 1;
    return 1;
}

/* PS 2624:09c4 squad_close */
void squad_close(void)
{
    if (!DSB(SQ_LOADED)) return;
    list_free(ds_ptr(PS_DS_squadtab_t1), far_null());
    list_free(ds_ptr(PS_DS_squadtab_weights), far_null());
    table_free(SQDB);
    DSB(SQ_LOADED) = 0;
}

__attribute__((constructor)) static void squad_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_squad_read_record, (CodeFn)squad_read_record);
    codeptr_register(PROG_PS, SD_FN(0x2624, 0x088c), (CodeFn)squad_free_record);
}
