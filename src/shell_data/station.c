/* shell_data: emd\station.c (PS 2a85), emd\ordnance.c (2ab3), 2-D geometry (2b1f) and the 64-bit
 * multiply (2b42), shell_data_emd2.md §12-14. */
#include "shell_data/sd_priv.h"
#include "render3d/render3d.h"

/* ================================================================================================
 * 2a85 station.c — STAT<n>.DAT slot DS:5F44 (6-byte records), STATTXT text far DS:1F9F
 * ================================================================================================ */

#define STDB ((s16)DSW(PS_DS_station_table))

/* PS 2a85:0006 station_open — "stat" DS:1FA3 "dat" DS:1FA8 */
void station_open(void)
{
    if (DSC(0x1F9E)) return;
    DSW(PS_DS_station_table) = (u16)table_load(0x1FA3, 0x1FA8, 6, 100, 0x12, 1, far_null(), far_null(), far_null());
    if (!DSW(PS_DS_station_table)) sd_fatal(FS_STATION, 0x3E);
    DSW(PS_DS_station_count_ids) = (u16)table_count(STDB, 0);
    DSB(0x1F9E) = 1;
}

/* PS 2a85:007d station_count (uncalled) */
s16 ps_station_count(void) { return (s16)DSW(PS_DS_station_count_ids); }

/* PS 2a85:0087 station_count_area (uncalled) */
s16 station_count_area(u8 area)
{
    s16 n = 0;
    for (s16 i = 0; i < (s16)DSW(PS_DS_station_count_ids); i++) {
        FarPtr r = table_rec(STDB, i);
        if (far_is_null(r)) sd_fatal(FS_STATION, 0x68);
        if ((u16)area == fw(r, 0)) n++;
    }
    return n;
}

/* PS 2a85:00f1 station_get(st, u16 *rec, u16 *area) — no NULL check */
void station_get(s16 st, u16 rec_ds, u16 area_ds)
{
    FarPtr r = table_rec(STDB, st);
    DSW(area_ds) = fw(r, 0);
    DSW(rec_ds) = fw(r, 2);
}

/* PS 2a85:0127 station_area */
u8 station_area(s16 st) { return fb(table_rec(STDB, st), 0); }

/* PS 2a85:014d station_in_area (uncalled) */
s8 station_in_area(s16 st, u8 area) { return (u16)area == fw(table_rec(STDB, st), 0); }

/* PS 2a85:0180 station_is_carrier */
u8 station_is_carrier(s16 st) { return fb(table_rec(STDB, st), 4); }

/* PS 2a85:01a7 station_find(rec, area, carrier) */
s16 station_find(u16 rec, u8 area, s8 carrier)
{
    for (s16 i = 0; i < (s16)DSW(PS_DS_station_count_ids); i++) {
        FarPtr r = table_rec(STDB, i);
        if (fw(r, 2) != rec || fw(r, 0) != (u16)area) continue;
        if ((fw(r, 4) != 0 && carrier != 0) || (fw(r, 4) == 0 && carrier == 0)) return i;
    }
    return -1;
}

/* PS 2a85:0216 station_close */
void station_close(void)
{
    if (!DSB(0x1F9E)) return;
    table_free(STDB);
    DSB(0x1F9E) = 0;
}

/* PS 2a85:0231 stattxt_open — "stattxt" DS:1FAC "dat" DS:1FB4, stride 100 */
void stattxt_open(void)
{
    if (!far_is_null(ds_far(0x1F9F))) sd_fatal(FS_STATION, 0x104);
    ds_far_wr(0x1F9F, text_load(0x1FAC, 0x1FB4, 100));
}

/* PS 2a85:027c station_name */
FarPtr station_name(s16 st)
{
    if (far_is_null(ds_far(0x1F9F))) sd_fatal(FS_STATION, 0x111);
    return text_get(ds_far(0x1F9F), st);
}

/* PS 2a85:02c1 stattxt_close */
void stattxt_close(void)
{
    text_free(ds_far(0x1F9F));
    ds_far_wr(0x1F9F, far_null());
}

/* ================================================================================================
 * 2ab3 ordnance.c — ORDNCE<n>.DAT list DS:5F48 (slot DS:5F52), ORDTXT<n>.DAT slot DS:5F46
 * record: +00 id, +02 u16 n[11], +18 far items[11] (6-byte slots: count, code, extra)
 * ================================================================================================ */

#define ORDB ((s16)DSW(PS_DS_ord_table))

/* PS 2ab3:0001 ord_read_cb */
static void ord_read_cb(FarPtr r)
{
    emd_read_far(r, 2, 1);
    u16 m = stk_mark();
    u16 n = stk_alloc(2);
    for (s16 L = 0; L < 11; L++) {
        emd_read_far(ds_ptr(n), 2, 1);
        fww(r, (u16)(2 + L * 2), DSW(n));
        FarPtr items = xfarmalloc((u32)(u16)(DSW(n) * 6));
        fwp(r, (u16)(0x18 + L * 4), items);
        for (s16 i = 0; i < (s16)DSW(n); i++) {
            FarPtr it = far_add(fp(r, (u16)(0x18 + L * 4)), (u16)(i * 6));
            emd_read_far(it, 2, 1);
            emd_read_far(far_add(it, 2), 2, 1);
            if (L == 0) emd_read_far(far_add(it, 4), 2, 1);
        }
    }
    stk_release(m);
}

/* PS 2ab3:0101 ord_free_cb */
static void ord_free_cb(FarPtr r)
{
    for (s16 L = 0; L < 11; L++) xfarfree(fp(r, (u16)(0x18 + L * 4)));
}

/* PS 2ab3:013b ord_open — "ordnce" DS:1FE3 "dat" DS:1FEA */
void ord_open(void)
{
    if (DSC(0x1FB8)) return;
    DSW(PS_DS_ord_table) = (u16)table_load(0x1FE3, 0x1FEA, 0x44, 15, 0x12, 2, ds_ptr(PS_DS_ord_list),
                                           codeptr_far(SD_FN(0x2ab3, 0x0001)), codeptr_far(SD_FN(0x2ab3, 0x0101)));
    if (!DSW(PS_DS_ord_table)) sd_fatal(FS_ORDNANCE, 0x6A);
    DSW(PS_DS_ord_count_ids_v) = (u16)table_count(ORDB, 0);
    DSB(0x1FB8) = 1;
}

/* PS 2ab3:01bd ord_count_ids (uncalled) */
s16 ord_count_ids(void) { return (s16)DSW(PS_DS_ord_count_ids_v); }

/* PS 2ab3:01c7 ord_n(type, L) */
s16 ord_n(s16 type, s8 L)
{
    FarPtr r = table_rec(ORDB, type);
    if (far_is_null(r)) sd_fatal(FS_ORDNANCE, 0x94);
    return fs(r, (u16)(2 + (s16)L * 2));
}

/* PS 2ab3:021d ord_items(type, L, s16 *cnt, s16 *code) */
void ord_items(s16 type, s8 L, u16 cnt_ds, u16 code_ds)
{
    FarPtr r = table_rec(ORDB, type);
    if (far_is_null(r)) sd_fatal(FS_ORDNANCE, 0xA8);
    s16 n = fs(r, (u16)(2 + (s16)L * 2));
    for (s16 i = 0; i < n; i++) {
        FarPtr it = far_add(fp(r, (u16)(0x18 + (s16)L * 4)), (u16)(i * 6));
        DSW(cnt_ds) = fw(it, 0); cnt_ds = (u16)(cnt_ds + 2);
        DSW(code_ds) = fw(it, 2); code_ds = (u16)(code_ds + 2);
    }
}

/* PS 2ab3:02d1 ord_gun_rounds(type, s16 *out) */
s16 ord_gun_rounds(s16 type, u16 out_ds)
{
    FarPtr r = table_rec(ORDB, type);
    if (far_is_null(r)) sd_fatal(FS_ORDNANCE, 0xC0);
    s16 n = fs(r, 2);
    for (s16 i = 0; i < n; i++) {
        DSW(out_ds) = fw(far_add(fp(r, 0x18), (u16)(i * 6)), 4);
        out_ds = (u16)(out_ds + 2);
    }
    return n;
}

/* PS 2ab3:0348 ord_mode(mtype, side) — table DS:1FB9 u8[11][3] */
static s8 ord_mode(s8 mtype, s8 side)
{
    s8 v = DSC((u16)(0x1FB9 + (s16)mtype * 3 + side));
    switch ((u16)(s16)v) {
    case 0: case 3: return 0;
    case 1: return 1;
    case 2: return 2;
    case 4: return DSC(0x63ED) == 1 ? 2 : 1;
    case 5: return DSC(0x635D) == 1 ? 2 : 1;
    default: return v;
    }
}

/* PS 2ab3:03a7 ord_pick(mtype, type, longrange, side) -> loadout — RNG: one pick({50,30,20}) */
s8 ord_pick(s8 mtype, s8 type, s8 longrange, s8 side)
{
    u16 m = stk_mark();
    u16 w = stk_alloc(6), cnt = stk_alloc(10), code = stk_alloc(10);
    for (int k = 0; k < 6; k++) DSB(w + k) = DSB(0x1FDA + k);
    u8 tried[3];
    for (int k = 0; k < 3; k++) tried[k] = DSB(0x1FE0 + k);
    u8 done = 0;
    for (int k = 0; k < 10; k++) DSB(code + k) = 0;   /* PORT: code[] uninitialised in the original */
    s8 cls = ac_class((s16)type);
    DSW(cnt) = 0;
    s8 mode = ord_mode(mtype, side);
    s16 k = (s16)rng_pick_weighted(w, 3);
    s8 L = 0;
    while (!done) {
        if (k < 0) break;               /* PORT: all three tried: the original indexes tried[-1] and may loop */
        tried[k] = 1;
        L = (s8)(DSB((u16)(PS_DS_tab_ordsel + (s16)cls * 0x18 + ((s16)mode * 3 + k) * 2)) + 1);
        ord_items((s16)type, L, cnt, code);
        if ((s16)DSW(cnt) > 0 || (s16)DSW(code) == -2) {
            if (!longrange) done = 1;
            else {
                if (L == 1) L = 8;
                else if (L == 5) L = 9;
                else if (L == 6) L = 10;
                ord_items((s16)type, L, cnt, code);
                s16 n = ord_n((s16)type, L);
                for (s16 i = 0; i < n; i++) if (DSW(code + i * 2) == 0) { done = 1; break; }
            }
        }
        if (!done) {
            k = -1;
            for (s16 i = 0; i < 3; i++) if (!tried[i]) k = i;
        }
    }
    stk_release(m);
    return L;
}

/* PS 2ab3:0504 ord_has_droptank(type, L) */
s8 ord_has_droptank(s16 type, s8 L)
{
    u16 m = stk_mark();
    u16 cnt = stk_alloc(10), code = stk_alloc(10);
    s16 n = ord_n(type, L);
    ord_items(type, L, cnt, code);
    s8 r = 0;
    for (s16 i = 0; i < n; i++) if (DSW(code + i * 2) == 0) { r = 1; break; }
    stk_release(m);
    return r;
}

/* PS 2ab3:0557 ord_any_droptank */
s8 ord_any_droptank(s16 type)
{
    for (s8 L = 1; L < 11; L++) if (ord_has_droptank(type, L)) return 1;
    return 0;
}

/* PS 2ab3:057f ord_close */
void ord_close(void)
{
    if (!DSB(0x1FB8)) return;
    table_free(ORDB);
    DSB(0x1FB8) = 0;
}

/* PS 2ab3:059a ordtxt_open — "ordtxt" DS:1FEE "dat" DS:1FF5 */
s16 ordtxt_open(void)
{
    if (DSW(PS_DS_ordtxt_table)) sd_fatal(FS_ORDNANCE, 0x19E);
    DSW(PS_DS_ordtxt_table) = (u16)table_load(0x1FEE, 0x1FF5, 4, 10, 0x0A, 5, far_null(), far_null(), far_null());
    if (!DSW(PS_DS_ordtxt_table)) sd_fatal(FS_ORDNANCE, 0x1A8);
    return 1;
}

/* PS 2ab3:061c ordtxt_name */
FarPtr ordtxt_name(s16 code)
{
    if (!DSW(PS_DS_ordtxt_table)) sd_fatal(FS_ORDNANCE, 0x1B3);
    return extdb_text((s16)DSW(PS_DS_ordtxt_table), code);
}

/* PS 2ab3:065a ordtxt_plural */
FarPtr ordtxt_plural(s16 code)
{
    if (!DSW(PS_DS_ordtxt_table)) sd_fatal(FS_ORDNANCE, 0x1BF);
    FarPtr p = extdb_text((s16)DSW(PS_DS_ordtxt_table), code);
    return far_add(p, (u16)(rtl__fstrlen(p) + 1));
}

/* PS 2ab3:06b7 ordtxt_close */
void ordtxt_close(void)
{
    table_free((s16)DSW(PS_DS_ordtxt_table));
    DSW(PS_DS_ordtxt_table) = 0;
}

/* ================================================================================================
 * 2b1f 2-D geometry on 12-byte positions {s32 x, s32 z, s32 alt}; 2b42 64-bit multiply
 * ================================================================================================ */

/* PS 2b42:0002 imul32x32 — signed 32x32 -> 64 (sign by negation of operands and result) */
static int64_t imul32x32(s32 a, s32 b) { return (int64_t)a * (int64_t)b; }

/* PS 2b42:00ae fixmul14(long a, int b) — bits 14..45 of the product */
s32 fixmul14(s32 a, s16 b) { return (s32)(u32)((uint64_t)imul32x32(a, b) >> 14); }

/* PS 2b1f:000c ang_to_deg(u16 a) */
s16 ang_to_deg(u16 a) { return (s16)(((u32)a * 90u) >> 14); }

/* PS 2b1f:0029 deg_to_ang (uncalled) */
s16 deg_to_ang(s16 d) { return (s16)(((s32)d << 14) / 90); }

/* PS 2b1f:0049 deg_norm (uncalled) */
s16 deg_norm(s16 d)
{
    while (d < 0) d = (s16)(d + 360);
    return (s16)(d % 360);
}

static u16 atan2_l(s32 x, s32 y)
{
    return atan2_32((u16)x, (u16)((u32)x >> 16), (u16)y, (u16)((u32)y >> 16));
}

/* PS 2b1f:021d dist(a, b) — 3d50:01e6 (near pointers) */
s32 dist(u16 a_ds, u16 b_ds) { return (s32)ldist2(a_ds, b_ds); }

/* PS 2b1f:0069 pitch(a, b) — only the offsets reach dist (both positions are in DGROUP) */
s16 pitch(FarPtr a, FarPtr b)
{
    s32 d = dist(a.off, b.off);
    s32 dz = (s32)(fl(b, 8) - fl(a, 8));
    return (s16)-(s16)(u16)(atan2_l(dz, d) - 0x4000);
}

/* PS 2b1f:00b4 heading(a, b) — binary angle, -4000h */
u16 heading(FarPtr a, FarPtr b)
{
    s32 dx = (s32)(fl(b, 0) - fl(a, 0));
    s32 dz = (s32)(fl(b, 4) - fl(a, 4));
    return (u16)(atan2_l(dx, dz) - 0x4000);
}

/* PS 2b1f:00f9 heading_deg */
s16 heading_deg(FarPtr a, FarPtr b) { return ang_to_deg(heading(a, b)); }

/* PS 2b1f:0121 project(a, ang, d, near out) — x and z; the altitude is untouched */
void project(FarPtr a, u16 ang, s32 d, u16 out_ds)
{
    s32 vx = fixmul14(d, (s16)-sin16(ang));
    DSSL(out_ds) = (s32)(fl(a, 0) + (u32)vx);
    s32 vz = fixmul14(d, (s16)cos16(ang));
    DSSL(out_ds + 4) = (s32)(fl(a, 4) + (u32)vz);
}

/* PS 2b1f:0189 rotate2(s16 v[2], ang) (uncalled) */
u16 rotate2(u16 v_ds, u16 ang)
{
    s16 s = (s16)cos16(ang), c = sin16(ang);
    s16 v0 = DSS(v_ds), v1 = DSS(v_ds + 2);
    s32 r0 = (s32)((u32)mul32((u16)v0, (u16)s) + (u32)mul32((u16)v1, (u16)(s16)-c));
    s32 r1 = (s32)((u32)mul32((u16)v0, (u16)c) + (u32)mul32((u16)v1, (u16)s));
    DSW(v_ds) = (u16)(r0 >> 14);
    DSW(v_ds + 2) = (u16)(r1 >> 14);
    return v_ds;
}

__attribute__((constructor)) static void station_codeptrs(void)
{
    codeptr_register(PROG_PS, SD_FN(0x2ab3, 0x0001), (CodeFn)ord_read_cb);
    codeptr_register(PROG_PS, SD_FN(0x2ab3, 0x0101), (CodeFn)ord_free_cb);
}
