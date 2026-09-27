/* shell_data: emd\convoy.c (PS 29ae), emd\ship.c (29c2), emd\form.c (2a56), shell_data_emd2.md §9-11. */
#include "shell_data/sd_priv.h"

/* ================================================================================================
 * 29ae convoy.c — CONVOY<n>.DAT slot DS:5F24 (13-byte records)
 * ================================================================================================ */

#define CVDB ((s16)DSW(PS_DS_convoy_table))

/* PS 29ae:0001 convoy_open — "convoy" DS:1F7B "dat" DS:1F82 */
void convoy_open(void)
{
    if (DSC(0x1F7A)) return;
    DSW(PS_DS_convoy_table) = (u16)table_load(0x1F7B, 0x1F82, 0x0D, 10, 0x11, 1, far_null(), far_null(), far_null());
    if (!DSW(PS_DS_convoy_table)) sd_fatal(FS_CONVOY, 0x36);
    DSW(PS_DS_convoy_count_ids) = (u16)table_count(CVDB, 0);
    DSB(0x1F7A) = 1;
}

/* PS 29ae:0078 convoy_count (uncalled) */
s16 convoy_count(void) { return (s16)DSW(PS_DS_convoy_count_ids); }

/* PS 29ae:0082 convoy_get(dst, id) — 13-byte struct copy */
FarPtr convoy_get(FarPtr dst, s16 id)
{
    FarPtr r = table_rec(CVDB, id);
    if (far_is_null(r)) sd_fatal(FS_CONVOY, 0x58);
    for (u16 k = 0; k < 13; k++) fwb(dst, k, fb(r, k));
    return dst;
}

/* PS 29ae:00e3 convoy_weight */
u8 convoy_weight(s16 id)
{
    FarPtr r = table_rec(CVDB, id);
    if (far_is_null(r)) sd_fatal(FS_CONVOY, 0x68);
    return fb(r, 0xC);
}

/* PS 29ae:0131 convoy_close */
void convoy_close(void)
{
    if (!DSB(0x1F7A)) return;
    table_free(CVDB);
    DSB(0x1F7A) = 0;
}

/* ================================================================================================
 * 29c2 ship.c — SHIP<n>.DAT list DS:5F28 (slot DS:5F32), end date DS:5F34
 * ================================================================================================ */

#define SHDB ((s16)DSW(PS_DS_ship_table))

/* PS 29c2:000c ship_read_cb */
static void ship_read_cb(FarPtr r)
{
    FarPtr mk = codeptr_far(PS_FN_list_dated_marker);
    emd_read_far(r, 1, 1);
    emd_read_far(far_add(r, 1), 1, 1);
    list_read(far_add(r, 0x02), 5, mk);
    list_read(far_add(r, 0x0C), 5, mk);
    list_read(far_add(r, 0x16), 5, mk);
    list_read(far_add(r, 0x20), 6, mk);
    list_read(far_add(r, 0x2A), 6, mk);
    list_read(far_add(r, 0x34), 6, mk);
}

/* PS 29c2:00fc ship_free_cb */
static void ship_free_cb(FarPtr r)
{
    for (u16 o = 0x02; o <= 0x34; o = (u16)(o + 0xA)) list_free(far_add(r, o), far_null());
}

/* PS 29c2:01a2 / 01f8 / 024e ship_class_w0/1/2(rec, date) — RNG over 3 weights */
static s8 ship_class_w(FarPtr rec, u16 listoff, u16 date)
{
    FarPtr e = list_find_dated(far_add(rec, listoff), date);   /* NULL before the first date: IVT */
    u16 m = stk_mark();
    u16 w = stk_alloc(6);
    for (u16 k = 0; k < 3; k++) DSW(w + k * 2) = (u16)(s16)fc(e, (u16)(2 + k));
    s8 r = rng_pick_weighted(w, 3);
    stk_release(m);
    return r;
}
s8 ship_class_w0(FarPtr rec, u16 date) { return ship_class_w(rec, 0x02, date); }
s8 ship_class_w1(FarPtr rec, u16 date) { return ship_class_w(rec, 0x0C, date); }
s8 ship_class_w2(FarPtr rec, u16 date) { return ship_class_w(rec, 0x16, date); }

/* PS 29c2:02a4 / 03cc / 04f4 ship_convoys_s0/1/2(rec, side, date, u8 out[3]) — only a real match */
static s8 ship_convoys_s(FarPtr rec, u16 listoff, s8 side, u16 date, u16 out3_ds)
{
    int found;
    FarPtr e = keyed_lookup(far_add(rec, listoff), 5, (u8)side, date, DSW(PS_DS_ship_enddate), &found);
    if (!found) return 0;
    for (u16 k = 0; k < 3; k++) DSB(out3_ds + k) = fb(e, (u16)(2 + k));
    return 1;
}
s8 ship_convoys_s0(FarPtr rec, s8 side, u16 date, u16 out3_ds) { return ship_convoys_s(rec, 0x20, side, date, out3_ds); }
s8 ship_convoys_s1(FarPtr rec, s8 side, u16 date, u16 out3_ds) { return ship_convoys_s(rec, 0x2A, side, date, out3_ds); }
s8 ship_convoys_s2(FarPtr rec, s8 side, u16 date, u16 out3_ds) { return ship_convoys_s(rec, 0x34, side, date, out3_ds); }

/* PS 29c2:061c ship_open — "ship" DS:1F8A "dat" DS:1F8F; end date with force 0 */
void ship_open(void)
{
    if (!DSC(0x1F86)) {
        DSW(PS_DS_ship_table) = (u16)table_load(0x1F8A, 0x1F8F, 0x3E, 10, 0x12, 2, ds_ptr(PS_DS_ship_list),
                                                codeptr_far(SD_FN(0x29c2, 0x000c)), codeptr_far(SD_FN(0x29c2, 0x00fc)));
        if (!DSW(PS_DS_ship_table)) sd_fatal(FS_SHIP, 0x190);
        DSB(0x1F86) = 1;
    }
    DSW(PS_DS_ship_enddate) = (u16)enddate_get((s8)module_active(), 0);
}

/* PS 29c2:06a0 ship_count */
s16 ship_count(void) { return table_count(SHDB, 0); }

/* PS 29c2:06b5 ship_area — zero-extended byte */
s8 ship_area(s16 i)
{
    FarPtr r = table_rec(SHDB, i);
    if (far_is_null(r)) sd_fatal(FS_SHIP, 0x1B5);
    return (s8)fb(r, 0);
}

/* PS 29c2:0704 ship_kind */
s8 ship_kind(s16 i)
{
    FarPtr r = table_rec(SHDB, i);
    if (far_is_null(r)) sd_fatal(FS_SHIP, 0x1C5);
    return fc(r, 1);
}

/* PS 29c2:0752 ship_pick_class(i, q, date).
 * PORT: q < 0 returns an uninitialised local in the original; the port returns FFh. */
s8 ship_pick_class(s16 i, s8 q, u16 date)
{
    FarPtr r = table_rec(SHDB, i);
    if (far_is_null(r)) sd_fatal(FS_SHIP, 0x1D6);
    switch (q) {
    case 0: return ship_class_w0(r, date);
    case 1: return ship_class_w1(r, date);
    case 2: return ship_class_w2(r, date);
    default:
        if (q >= 0) sd_fatal(FS_SHIP, 0x1E7);
        return (s8)0xFF;
    }
}

/* PS 29c2:080c ship_convoys(i, cls, side, date, out[3]) -> class used (FFh none) */
s8 ship_convoys(s16 i, s8 cls, s8 side, u16 date, u16 out3_ds)
{
    u8 tried[3];
    for (int k = 0; k < 3; k++) tried[k] = DSB(0x1F87 + k);
    FarPtr r = table_rec(SHDB, i);
    if (far_is_null(r)) sd_fatal(FS_SHIP, 0x1FD);
    for (;;) {
        s8 ok;
        switch (cls) {
        case 0: ok = ship_convoys_s0(r, side, date, out3_ds); break;
        case 1: ok = ship_convoys_s1(r, side, date, out3_ds); break;
        case 2: ok = ship_convoys_s2(r, side, date, out3_ds); break;
        default: return (s8)0xFF;
        }
        if (ok) return cls;
        tried[(u8)cls] = 1;
        cls = (s8)0xFF;
        for (s8 k = 0; k < 3; k++) if (!tried[k]) cls = k;
    }
}

/* PS 29c2:092b ship_close */
void ship_close(void)
{
    if (!DSB(0x1F86)) return;
    table_free(SHDB);
    DSB(0x1F86) = 0;
}

/* ================================================================================================
 * 2a56 form.c — FORM<n>.DAT list DS:5F36 (slot DS:5F40)
 * ================================================================================================ */

#define FMDB ((s16)DSW(PS_DS_form_table))

/* PS 2a56:0006 form_pos_read_cb */
static void form_pos_read_cb(FarPtr p) { emd_read_far(p, 1, 1); }

/* PS 2a56:002d form_el_read_cb */
static void form_el_read_cb(FarPtr el)
{
    emd_read_far(el, 1, 1);
    list_read(far_add(el, 1), 1, codeptr_far(SD_FN(0x2a56, 0x0006)));
}

/* PS 2a56:0070 form_read_cb */
static void form_read_cb(FarPtr r)
{
    emd_read_far(r, 2, 1);
    list_read(far_add(r, 2), 0xB, codeptr_far(SD_FN(0x2a56, 0x002d)));
}

/* PS 2a56:00b8 form_el_free_cb */
static void form_el_free_cb(FarPtr el) { list_free(far_add(el, 1), far_null()); }

/* PS 2a56:00e4 form_free_cb */
static void form_free_cb(FarPtr r) { list_free(far_add(r, 2), codeptr_far(SD_FN(0x2a56, 0x00b8))); }

/* PS 2a56:0114 form_open — "form" DS:1F95 "dat" DS:1F9A */
s16 form_open(void)
{
    if (!DSC(0x1F94)) {
        DSW(PS_DS_form_table) = (u16)table_load(0x1F95, 0x1F9A, 0x0C, 10, 0x12, 6, ds_ptr(PS_DS_form_list),
                                                codeptr_far(SD_FN(0x2a56, 0x0070)), codeptr_far(SD_FN(0x2a56, 0x00e4)));
        if (!DSW(PS_DS_form_table)) sd_fatal(FS_FORM, 0x94);
        DSB(0x1F94) = 1;
    }
    return 1;
}

/* PS 2a56:0185 form_count */
s16 form_count(void) { return table_count(FMDB, 0); }

/* PS 2a56:019a form_name */
FarPtr form_name(s16 id)
{
    FarPtr r = table_rec(FMDB, id);
    if (far_is_null(r)) sd_fatal(FS_FORM, 0xB7);
    return table_text(FMDB, id, (u32)(s32)fs(r, 0));
}

/* PS 2a56:01fa form_layout(id, planes, u8 *pos, char *n) */
void form_layout(s16 id, s8 planes, u16 p2_ds, u16 n_ds)
{
    FarPtr r = table_rec(FMDB, id);
    if (far_is_null(r)) sd_fatal(FS_FORM, 0xCC);
    FarPtr l = far_add(r, 2);
    FarPtr el = list_first(l);
    for (s16 i = 0; i < (s16)fw(r, 4); i++) {
        if (fc(el, 0) == planes) {
            FarPtr pl = far_add(el, 1);
            FarPtr pe = list_first(pl);
            u16 p = p2_ds;
            for (s16 k = 0; k < (s16)fw(el, 3); k++) {
                DSB(p++) = fb(pe, 0);
                pe = ps_list_next(pl);
            }
            DSB(n_ds) = fb(el, 3);
            return;
        }
        el = ps_list_next(l);
    }
}

/* PS 2a56:02db form_close */
void form_close(void)
{
    if (!DSB(0x1F94)) return;
    table_free(FMDB);
    DSB(0x1F94) = 0;
}

__attribute__((constructor)) static void convoy_codeptrs(void)
{
    codeptr_register(PROG_PS, SD_FN(0x29c2, 0x000c), (CodeFn)ship_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x29c2, 0x00fc), (CodeFn)ship_free_cb);
    codeptr_register(PROG_PS, SD_FN(0x2a56, 0x0006), (CodeFn)form_pos_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x2a56, 0x002d), (CodeFn)form_el_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x2a56, 0x0070), (CodeFn)form_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x2a56, 0x00b8), (CodeFn)form_el_free_cb);
    codeptr_register(PROG_PS, SD_FN(0x2a56, 0x00e4), (CodeFn)form_free_cb);
}
