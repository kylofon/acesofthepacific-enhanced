/* shell_data: emd\aircraft.c (PS 2777), emd\medals.c (PS 27f0), flight sizes/formations (PS 280a),
 * shell_data_emd2.md §1-4. */
#include "shell_data/sd_priv.h"
#include "shell_ui/shell_ui.h"

#define AIR_LOADED 0x1E1E
#define AIRDB ((s16)DSW(PS_DS_air_table))
#define ACL(slot)  ((u16)(PS_DS_acl + (s16)(slot) * 0x143))

/* ================================================================================================
 * the "dated keyed list" lookup (shell_data_emd2.md §1), shared by 280a, 28e4, 2968, 29c2.
 * KEY(NULL) reads the real-mode IVT exactly like the original (mem[] at 0000:000x).
 * ================================================================================================ */
FarPtr keyed_lookup(FarPtr l, u16 keyoff, u8 key, u16 date, u16 end_default, int *found)
{
    FarPtr cur = list_first(l), nxt;
    do nxt = ps_list_next(l); while (fb(nxt, keyoff) != key && !far_is_null(nxt));
    u16 end = far_is_null(nxt) ? end_default : fw(nxt, 0);
    bool last = far_is_null(nxt);
    for (s16 i = 0; i < (s16)fw(l, 2); i++) {
        if (fb(cur, keyoff) == key && fw(cur, 0) <= date && date <= end) { *found = 1; return cur; }
        if (last) break;
        cur = nxt;
        do nxt = ps_list_next(l); while (fb(nxt, keyoff) != key && !far_is_null(nxt));
        end = far_is_null(nxt) ? end_default : fw(nxt, 0);
        last = far_is_null(nxt);
    }
    *found = 0;
    return cur;
}

/* ================================================================================================
 * 2777 aircraft.c
 * ================================================================================================ */

static FarPtr ac_rec(s16 id, u16 line)
{
    FarPtr r = table_rec(AIRDB, id);
    if (far_is_null(r)) sd_fatal(FS_AIRCRAFT, line);
    return r;
}

/* PS 2777:0000 air_open — "air" DS:1E23 "dat" DS:1E27 */
void air_open(void)
{
    if (DSC(AIR_LOADED)) return;
    DSW(PS_DS_air_table) = (u16)table_load(0x1E23, 0x1E27, 0x18, 15, 0x12, 5, far_null(), far_null(), far_null());
    if (!DSW(PS_DS_air_table)) sd_fatal(FS_AIRCRAFT, 0x4D);
    DSW(PS_DS_ac_id_count) = (u16)table_count(AIRDB, 0);
    DSB(AIR_LOADED) = 1;
}

/* PS 2777:0077 ac_exists */
s16 ac_exists(s16 id) { return !far_is_null(table_rec(AIRDB, id)); }

/* PS 2777:0097 ac_count */
s16 ac_count(void) { return (s16)DSW(PS_DS_ac_id_count); }

/* PS 2777:00a1 ac_name */
FarPtr ac_name(s16 id)
{
    FarPtr r = ac_rec(id, 0x7C);
    return table_text(AIRDB, id, (u32)(s32)fs(r, 0));
}

/* PS 2777:0101 ac_plural — the string after the name */
FarPtr ac_plural(s16 id)
{
    FarPtr p = ac_name(id);
    return far_add(p, (u16)(rtl__fstrlen(p) + 1));
}

/* PS 2777:012f ac_has_service(id, s) — mask & DS:1E08[s] */
s8 ac_has_service(s16 id, s8 s)
{
    FarPtr r = ac_rec(id, 0x98);
    return (DSW((u16)(0x1E08 + (s16)s * 2)) & fw(r, 2)) != 0;
}

/* PS 2777:0193 ac_side(id) — side group of the first service in the mask.
 * PORT: no service bit leaves the local uninitialised in the original; the port returns 0. */
s8 ac_side(s16 id)
{
    FarPtr r = ac_rec(id, 0xAD);
    for (s16 s = 0; s < 11; s++)
        if (fw(r, 2) & DSW((u16)(0x1E08 + s * 2))) return (s8)service_side((u16)s);
    return 0;
}

/* PS 2777:0204 ac_class */
s8 ac_class(s16 id) { return fc(ac_rec(id, 0xC4), 4); }

/* PS 2777:0252 ac_selfid */
s8 ac_selfid(s16 id) { return fc(ac_rec(id, 0xD4), 5); }

/* PS 2777:02a0 ac_date (uncalled) */
u16 ac_date(s16 id) { return date_make(far_add(ac_rec(id, 0xE4), 6)); }

/* PS 2777:02f8 ac_alt_band */
s8 ac_alt_band(s16 id) { return fc(ac_rec(id, 0xF4), 9); }

/* PS 2777:0346 ac_multiseat / 0394 ac_b0b (uncalled) */
s8 ac_multiseat(s16 id) { return fc(ac_rec(id, 0x104), 0xA); }
s8 ac_b0b(s16 id) { return fc(ac_rec(id, 0x114), 0xB); }

/* PS 2777:03e2 ac_range / 0434 ac_range_ext */
s32 ac_range(s16 id) { return (s32)fl(ac_rec(id, 0x124), 0xC); }
s32 ac_range_ext(s16 id) { return (s32)fl(ac_rec(id, 0x134), 0x10); }

/* PS 2777:0486 ac_cruise_alt — u32 +14 << 2 (LXLSH) */
s32 ac_cruise_alt(s16 id) { return (s32)(fl(ac_rec(id, 0x144), 0x14) << 2); }

/* PS 2777:04df air_close */
void air_close(void)
{
    if (!DSB(AIR_LOADED)) return;
    table_free(AIRDB);
    DSB(AIR_LOADED) = 0;
}

/* PS 2777:05c0 acl_next(char id) — first acceptable id >= id of the current list */
static s8 acl_next(s8 id)
{
    u16 a = ACL(DSC(PS_DS_acl_slot));
    s8 side = (s8)service_side((u16)(s16)DSC(a));
    if (!DSB(a + 2)) return id;
    for (; (s16)id < (s16)DSW(PS_DS_ac_id_count); id++) {
        if (!ac_exists(id)) continue;
        if (DSB(PS_DS_acl_need_tanks) && !ord_any_droptank(id)) continue;
        a = ACL(DSC(PS_DS_acl_slot));
        u8 f = DSB(a + 2);
        if (f & 2) {
            if (DSB(0x1E1F + (s16)ac_class(id)) & DSB(ACL(DSC(PS_DS_acl_slot)) + 2)) return id;
            continue;
        }
        if (f & 1) {
            if (ac_has_service(id, DSC(a)) &&
                (DSB(0x1E1F + (s16)ac_class(id)) & DSB(ACL(DSC(PS_DS_acl_slot)) + 2))) return id;
            continue;
        }
        if (ac_side(id) == side && (DSB(0x1E1F + (s16)ac_class(id)) & DSB(ACL(DSC(PS_DS_acl_slot)) + 2)))
            return id;
    }
    return id;
}

/* PS 2777:04fa acl_build(service, flags, slot, need_tanks) */
void acl_build(s8 service, u8 flags, s8 slot, u8 need_tanks)
{
    s16 n = 0, id = 0;
    DSB(PS_DS_acl_slot) = (u8)slot;
    DSB(PS_DS_acl_need_tanks) = need_tanks;
    u16 a = ACL(DSC(PS_DS_acl_slot));
    rtl_memset(a, 0, 0x143);
    DSW(a) = (u16)(s16)service;
    DSW(a + 0x141) = 0;
    DSB(a + 2) = flags;
    while (id < (s16)DSW(PS_DS_ac_id_count)) {
        if (DSB(ACL(DSC(PS_DS_acl_slot)) + 2)) id = (s16)acl_next((s8)id);
        if (id < (s16)DSW(PS_DS_ac_id_count)) {
            DSW(ACL(DSC(PS_DS_acl_slot)) + 3 + n * 2) = (u16)id;
            id++;
            n++;
        }
    }
    DSW(ACL(DSC(PS_DS_acl_slot)) + 0x141) = (u16)n;
}

/* PS 2777:06fd acl_select */
void acl_select(s8 slot, s8 plural)
{
    DSB(PS_DS_acl_slot) = (u8)slot;
    DSB(PS_DS_acl_plural) = (u8)plural;
}

/* PS 2777:070e acl_count */
s16 acl_count(void) { return (s16)DSW(ACL(DSC(PS_DS_acl_slot)) + 0x141); }

/* PS 2777:0724 acl_id */
s16 acl_id(s16 i) { return (s16)DSW((u16)(ACL(DSC(PS_DS_acl_slot)) + 3 + i * 2)); }

/* PS 2777:0741 acl_name — string pool copy of the name or plural */
FarPtr acl_name(s16 i)
{
    s16 id = acl_id(i);
    FarPtr s = DSB(PS_DS_acl_plural) ? ac_plural(id) : ac_name(id);
    return strpool_copy(s);
}

/* ================================================================================================
 * 27f0 medals.c — MEDALS.DAT, 11-byte records at DS:5ECE, text DS:5ECC
 * ================================================================================================ */

/* PS 27f0:0004 medals_open — "medals" DS:1E37 "dat" DS:1E3E, "rb" DS:1E42 */
void medals_open(void)
{
    u16 f = my_fopen(make_name(0x1E37, -1, 0x1E3E), 0x1E42);
    if (!f) sd_fatal(FS_MEDALS, 0x49);
    my_fread(PS_DS_medal_count, 2, 1, f);
    DSW(PS_DS_medal_recs) = xmalloc((u16)(DSW(PS_DS_medal_count) * 11));
    my_fread(DSW(PS_DS_medal_recs), 11, (s16)DSW(PS_DS_medal_count), f);
    u16 m = stk_mark();
    u16 len = stk_alloc(2);
    my_fread(len, 2, 1, f);
    DSW(PS_DS_medal_text) = xmalloc(DSW(len));
    my_fread(DSW(PS_DS_medal_text), 1, (s16)DSW(len), f);
    stk_release(m);
    my_fclose(f);
    for (s16 i = 0; i < (s16)DSW(PS_DS_medal_count); i++) {
        u16 r = (u16)(DSW(PS_DS_medal_recs) + i * 11);
        DSW(r) = (u16)(DSW(PS_DS_medal_text) + DSW(r));
    }
}

/* PS 27f0:00f5 medal_name */
u16 medal_name(s8 m) { return DSW((u16)(DSW(PS_DS_medal_recs) + (s16)m * 11)); }

/* PS 27f0:010d medal_for_service — mask & DS:1E2C[s] */
s8 medal_for_service(s8 m, s8 s)
{
    u16 r = (u16)(DSW(PS_DS_medal_recs) + (s16)m * 11);
    return (DSB(r + 2) & DSB((u16)(0x1E2C + (s16)s))) != 0;
}

/* PS 27f0:013c medal_is_wound */
s16 medal_is_wound(s8 m) { return m == 0 || m == 11; }

/* PS 27f0:0155 medal_req(m, s16 out[4]) */
void medal_req(s8 m, u16 out4_ds)
{
    u16 r = (u16)(DSW(PS_DS_medal_recs) + (s16)m * 11);
    for (int k = 0; k < 4; k++) DSW(out4_ds + k * 2) = DSW(r + 3 + k * 2);
}

/* PS 27f0:0191 medals_close */
void medals_close(void)
{
    xfree(DSW(PS_DS_medal_recs));
    xfree(DSW(PS_DS_medal_text));
}

/* ================================================================================================
 * 280a flight sizes and formations — list DS:5ED2, one 21-byte element per service
 * ================================================================================================ */

/* PS 280a:000a flight_read_cb */
static void flight_read_cb(FarPtr el)
{
    FarPtr mk = codeptr_far(PS_FN_list_dated_marker);
    emd_read_far(el, 1, 1);
    list_read(far_add(el, 1), 4, mk);
    list_read(far_add(el, 0xB), 4, mk);
}

/* PS 280a:006b flight_free_cb */
static void flight_free_cb(FarPtr el)
{
    list_free(far_add(el, 1), far_null());
    list_free(far_add(el, 0xB), far_null());
}

/* PS 280a:00af flight_open — "flight" DS:1E47 "dat" DS:1E4E */
void flight_open(void)
{
    if (!DSC(0x1E46)) {
        emd_open(make_name(0x1E47, (s16)(s8)module_active(), 0x1E4E));
        list_read(ds_ptr(PS_DS_flight_list), 0x15, codeptr_far(SD_FN(0x280a, 0x000a)));
        emd_close_rest_far();
        DSB(0x1E46) = 1;
    }
    DSW(PS_DS_flight_enddate) = (u16)enddate_get((s8)module_active(), 1);
}

/* PS 280a:0110 flight_planes(service, role, date) — list A key +02, value +03 */
u8 flight_planes(s8 service, s8 role, u16 date)
{
    FarPtr el = list_at(ds_ptr(PS_DS_flight_list), service);
    int found;
    FarPtr e = keyed_lookup(far_add(el, 1), 2, (u8)role, date, DSW(PS_DS_flight_enddate), &found);
    return fb(e, 3);
}

/* PS 280a:021b flight_formation(service, role, date) — list B key +03, value +02 */
u8 flight_formation(s8 service, s8 role, u16 date)
{
    FarPtr el = list_at(ds_ptr(PS_DS_flight_list), service);
    int found;
    FarPtr e = keyed_lookup(far_add(el, 0xB), 3, (u8)role, date, DSW(PS_DS_flight_enddate), &found);
    return fb(e, 2);
}

/* PS 280a:032c flight_formations(service, role, date, s8 *out) -> count */
s8 flight_formations(s8 service, s8 role, u16 date, u16 out_ds)
{
    s8 n = 0;
    u16 prev = 0;
    FarPtr el = list_at(ds_ptr(PS_DS_flight_list), service);
    FarPtr l = far_add(el, 0xB);
    for (FarPtr e = list_first(l); !far_is_null(e); e = ps_list_next(l)) {
        if (fc(e, 3) != role) continue;
        if (fw(e, 0) > date) break;
        s16 v = (s16)fc(e, 2);
        if (v == 14 || v == 15) v = 0;
        else if (v > 7) v = (s16)(v - 8);
        if (n == 0 || fw(e, 0) <= prev) {
            DSB(out_ds + n) = (u8)v;
            n++;
        } else {
            s16 old = (s16)DSC(out_ds);
            if (old != v) {
                DSB(out_ds) = (u8)v;
                s8 c;
                for (c = 1; c < n; c++)
                    if ((s16)DSC(out_ds + c) == v) { DSB(out_ds + c) = (u8)old; break; }
                if (c == n) { DSB(out_ds + n) = (u8)old; n++; }
            }
        }
        prev = fw(e, 0);
    }
    return n;
}

/* PS 280a:0431 flight_close */
void flight_close(void)
{
    if (!DSB(0x1E46)) return;
    list_free(ds_ptr(PS_DS_flight_list), codeptr_far(SD_FN(0x280a, 0x006b)));
    DSB(0x1E46) = 0;
}

__attribute__((constructor)) static void aircraft_codeptrs(void)
{
    codeptr_register(PROG_PS, SD_FN(0x280a, 0x000a), (CodeFn)flight_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x280a, 0x006b), (CodeFn)flight_free_cb);
}
