/* sim_world part A — snario.c (56ac): the sim-side scenario (SCRIPT.DAT passes, flights, placement),
 * wind, the byte-stream reader (36ce), misc object-type tables (1641) and the small array utilities
 * (36d6..36ea). sim_world_dmg.md §1.1–1.4, §1.6, §1.9; every function checked against the disassembly
 * (x86dis AP_flat.exe). Locals whose layout matters (arrays that can overflow into neighbours, locals
 * that persist across loop iterations and are read through pointers) live in an emulated stack frame
 * addressed like the original (`L(off)` = bp − off). */
#include "sim_world/swa_priv.h"
#include "platform/rtl.h"

#define SNARIO_C   far_make(SEG(0x38A8), 0)      /* "snario.c" (assert file name); wind blocks at 38A8:0014/005E */
#define WIND_SEG   SEG(0x38A8)
#define ALT_SEG    SEG(0x381C)                   /* altitude band table at 381C:002D */

/* ==== 36ce: byte-stream reader over the in-memory script (DS:715C far cursor, DS:715A bytes left) == */

/* AP 36ce:0008 */
static void rd_open(FarPtr p, s16 n) { ds_far_wr(0x715C, p); DSS(0x715A) = n; }

/* AP 36ce:001b — no bounds check, near offset arithmetic */
static s16 rd_skip(s16 n)
{
    DSS(0x715A) = (s16)(DSS(0x715A) - n);
    DSW(0x715C) = (u16)(DSW(0x715C) + n);
    return n;
}

/* AP 36ce:002d — copies whole records while enough bytes are left; returns the records read */
static s16 rd_read(u16 dst, s16 size, s16 count)
{
    s16 n = 0;
    while (DSS(0x715A) >= size && count-- != 0) {
        rtl__fmemcpy(ds_ptr(dst), ds_far(0x715C), (u16)size);
        DSS(0x715A) = (s16)(DSS(0x715A) - size);
        DSW(0x715C) = (u16)(DSW(0x715C) + size);
        dst = (u16)(dst + size);
        n++;
    }
    return n;
}

/* AP 36ce:0076 */
static FarPtr rd_tell(void) { return ds_far(0x715C); }

/* ==== 36d6..36ea: small array utilities =========================================================== */

/* AP 36d6:0002 — a[i] = v + i·step */
void fill_seq(u16 a_ds, u16 v, u16 step, u16 n)
{
    for (s16 i = 0; i < (s16)n; i++, v = (u16)(v + step)) DSW((u16)(a_ds + 2 * i)) = v;
}

/* AP 36d8:000a — index of v in a[0..n) or −1 */
u16 find_int(u16 a_ds, u16 v, u16 n)
{
    for (s16 i = 0; i < (s16)n; i++)
        if (DSW((u16)(a_ds + 2 * i)) == v) return (u16)i;
    return 0xFFFF;
}

/* AP 36db:0009 */
void fill_int(u16 a, u16 v, u16 n)
{
    for (s16 i = (s16)n; --i >= 0; a = (u16)(a + 2)) DSW(a) = v;
}

/* AP 36dd:0009 — indices of the set bits of (n + 7) / 8 bytes; returns the count */
u16 bits_to_list(u16 out_ds, u16 bits_ds, u16 n)
{
    s16 bytes = (s16)((s16)(n + 7) >> 3);
    u16 o = out_ds, base = 0;
    while (bytes-- != 0) {
        u16 b = DSB(bits_ds++);
        for (u16 c = base; b; c++, b >>= 1)
            if (b & 1) { DSW(o) = c; o = (u16)(o + 2); }
        base = (u16)(base + 8);
    }
    return (u16)((s16)(o - out_ds) / 2);
}

/* AP 36e3:0005 */
void bytes_or(u16 dst, u16 a, u16 b, u16 n)
{
    for (s16 i = 0; i < (s16)n; i++) DSB((u16)(dst + i)) = (u8)(DSB((u16)(a + i)) | DSB((u16)(b + i)));
}

/* AP 36e3:0032 */
void bytes_and(u16 dst, u16 a, u16 b, u16 n)
{
    for (s16 i = 0; i < (s16)n; i++) DSB((u16)(dst + i)) = (u8)(DSB((u16)(a + i)) & DSB((u16)(b + i)));
}

/* AP 36e8:000f */
u16 all_zero(u16 p_ds, u16 n)
{
    for (s16 i = (s16)n; --i >= 0; p_ds++)
        if (DSB(p_ds)) return 0;
    return 1;
}

/* AP 36ea:000e — out[i] = tbl[idx[i]] */
void gather(u16 out_ds, u16 tbl_ds, u16 idx_ds, u16 n)
{
    for (s16 i = (s16)n; --i >= 0; out_ds = (u16)(out_ds + 2), idx_ds = (u16)(idx_ds + 2))
        DSW(out_ds) = DSW((u16)(tbl_ds + DSW(idx_ds) * 2));
}

/* ==== 1641: misc object-type tables ================================================================ */

/* AP 1641:000c — <area>.TBL shape table → far DS:18D6; <area>.DAT (a NUL-terminated title, then 26-byte
 * object type records) → far DS:18DA, count DS:18DE, FFFFh terminator */
void misctbl_load(u16 area_ds)
{
    ds_far_wr(0x18D6, far_make(0, 0));
    ds_far_wr(0x18D6, ts_load_chunk(res_name(area_ds, 0x05B8), 0x18E0));
    ds_far_wr(0x18DA, far_make(0, 0));
    u16 f = my_fopen(res_name(area_ds, 0x05BC), 0x05EB);
    if (!f) assert_fail(far_make(SEG(0x3A58), 0), 0x40, 0);
    s16 si = 0;
    u16 t = 0x5B44;
    for (;;) {
        u8 c = (u8)my_fgetc(f);
        DSB(t++) = c;
        if (!c) break;
        si--;
    }
    si = (s16)(si + (s16)my_filelength(f));
    ds_far_wr(0x18DA, mem_far_new((u32)(u16)(si + 2), 0));
    ap_fread_far_55a4(ds_far(0x18DA), (s32)si, f);
    DSW(0x18DE) = (u16)((u16)si / 0x1A);
    FarPtr e = ds_far(0x18DA);
    wr16(e.seg, (u16)(e.off + DSW(0x18DE) * 0x1A), 0xFFFF);
    my_fclose(f);
}

/* AP 1641:0109 */
void misctbl_free(void)
{
    mem_far_delete(ds_far(0x18D6));
    ds_far_wr(0x18D6, far_make(0, 0));
    if (!far_is_null(ds_far(0x18DA))) mem_far_delete(ds_far(0x18DA));
    ds_far_wr(0x18DA, far_make(0, 0));
}

/* AP 1641:0142 */
u16 objtype_shape(u16 i) { FarPtr t = ds_far(0x18D6); return rd16(t.seg, (u16)(t.off + (i << 2) + 6)); }

/* AP 1641:0157 — the word at +4 of the shape-table entry (left in AX through a dead local) */
u16 objtype_class(u16 i) { FarPtr t = ds_far(0x18D6); return rd16(t.seg, (u16)(t.off + (i << 2) + 4)); }

/* AP 1641:0172 — AA kind: 1 → 3 (heavy flak), 2 → 8 (light AA), else 0 */
u16 objtype_aa(u16 i)
{
    FarPtr t = ds_far(0x18D6);
    u16 v = rd16(t.seg, (u16)(t.off + (i << 2) + 4));
    return v == 1 ? 3 : v == 2 ? 8 : 0;
}

/* AP 1641:01a5 — copy the two 12-byte names of object type `id` */
void objtype_names(u16 id, u16 a, u16 b)
{
    FarPtr p = ds_far(0x18DA);
    for (u32 guard = 0; ; guard++) {
        if (rd16(p.seg, p.off) == id) {
            memmove(DSP(a), mp(p.seg, (u16)(p.off + 2)), 12);        /* SCOPY@ */
            memmove(DSP(b), mp(p.seg, (u16)(p.off + 0x0E)), 12);
            return;
        }
        p.off = (u16)(p.off + 0x1A);
        if (guard > 0x10000) return;      /* PORT: the original has no end test and hangs on an unknown id */
    }
}

/* ==== 56ac: wind ==================================================================================== */

/* AP 56ac:0000 — STILL.WND → far 38A8:0014, the mission's wind → far 38A8:005E (4Ah bytes each) */
static void wind_load(s16 w)
{
    u16 mk = stk_mark(), buf = stk_alloc(0x4A);
    file_load(buf, 0x4A, 1, res_name(make_filename(DSW(0x07F3), 0x07EF), 0x05D4));
    memcpy(mp(WIND_SEG, 0x14), DSP(buf), 0x4A);
    file_load(buf, 0x4A, 1, res_name(make_filename(DSW((u16)(0x07F3 + w * 2)), 0x07EF), 0x05D4));
    memcpy(mp(WIND_SEG, 0x5E), DSP(buf), 0x4A);
    stk_release(mk);
}

/* AP 56ac:009d — the active wind block DS:027C: the mission's with realistic weather, else "still" */
void wind_apply(void)
{
    DSS(0x4743) = (s16)(s8)DSB(0x42C9);
    memmove(DSP(0x027C), mp(WIND_SEG, (u16)((DSW(0x4743) ? 1 : 0) * 0x4A + 0x14)), 0x4A);   /* SCOPY@ */
}

/* AP 56ac:00cf */
static void wind_select(s16 load)
{
    DSB(0x4512) = DSB(0x477F) == 3;
    if (load) {
        DSB(0x477A) = DSB(0x477F);
        wind_load((s16)(s8)DSB(0x477F));
    }
    DSB(0x476D) = DSB(0x477B) == 1;
}

/* ==== 56ac: module init, script records ============================================================= */

/* AP 56ac:010b */
void scenario_module_init(void)
{
    ts_init(0x3C, 0x28);
    u16 nm = res_name(0x0841, 0x05B8);                     /* AOP_COMM.TBL */
    if (!ts_load_table_h(1, nm, 1)) assert_fail(SNARIO_C, 0x10C, nm);
    memset(DSP(0x07DF), 0, 0x10);
    DSB(0x07DF) = 0;
    DSW(0x07E3) = 0xFFFF;
    DSW(0x3F22) = 0x46;
    DSW(0x3F24) = 0x42;
}

/* AP 56ac:0184 */
static void script_open(FarPtr p, s16 n) { rd_open(p, n); DSW(0x450E) = 0; }

/* AP 56ac:019e — next record: {u16 type, u16 count} block headers, then one record per call */
static s16 script_next(void)
{
    if (DSW(0x450E) == 0) {
        if (rd_read(0x4510, 2, 1) != 1) return 0;
        rd_read(0x450E, 2, 1);
    }
    DSW(0x450E)--;
    return 1;
}

/* AP 56ac:01dc — sizes DS:07FB[type]; type 7 (waypoint) by its kind byte */
static void script_skip_record(void)
{
    if (DSW(0x4510) != 7) { rd_skip(DSS((u16)(0x07FB + DSW(0x4510) * 2))); return; }
    u16 mk = stk_mark(), h = stk_alloc(2);
    rd_read(h, 2, 1);
    rd_skip(DSB(h) ? 4 : 12);
    stk_release(mk);
}

/* AP 56ac:0226 */
static u16 identity(u16 x) { return x; }

/* AP 56ac:022e — the player's aircraft record byte +7 */
u16 player_rec7(void) { return DSW(0x44F8); }

/* ==== 56ac:0236 scenario_scan — pass 1: counts, header, models, area =================================== */

void scenario_scan(FarPtr script, s16 len)
{
    u16 mk = stk_mark();
    u16 B = (u16)(stk_alloc(0x18A) + 0x18A);                /* bp */
#define L(o) ((u16)(B - (o)))
    DSW(L(2)) = 0xFFFF;                                      /* player's aircraft type */
    DSL(0x4749) = 0;
    DSL(0x4745) = 0;
    memset(DSP(0x4513), 0, 0x119 * 2);
    memset(DSP(0x477B), 0, 0x2D * 2);
    memset(DSP(L(0xD8)), 0, 0x61 * 2);                       /* pilots used */
    DSW(0x4519) = script.seg;
    DSW(0x4517) = (u16)(script.off + 2);                     /* skip the version word */
    DSS(0x4515) = (s16)(len - 2);
    memset(DSP(0x4763), 0, 8);
    DSW(0x476B) = DSW(0x476E) = DSW(0x4770) = DSW(0x4772) = DSW(0x4774) = DSW(0x4776) = DSW(0x4778) = 0;
    u16 di = 2;                                              /* shape tables: 2 + number of models */
    script_open(ds_far(0x4517), DSS(0x4515));
    while (script_next()) {
        switch (DSW(0x4510)) {
        case 0:
            rd_read(0x477B, 0x5A, 1);
            break;
        case 7:
            rd_read(L(0x0E), 2, 1);
            if (DSB(L(0x0E)) == 0) { rd_skip(12); DSW(0x473F) = (u16)(DSW(0x473F) + 14); }
            else { rd_skip(4); DSW(0x473F) = (u16)(DSW(0x473F) + 8); }
            DSW(0x4741)++;
            break;
        case 1: {
            rd_read(L(0x166), 0x16, 1);
            u16 t = DSB(L(0x166));
            if (DSW((u16)(0x4601 + t * 2))++ == 0) di++;
            DSB((u16)(L(0xD8) + (s8)DSB(L(0x164)))) = 1;
            if (DSB(L(0x164)) == 0x2C) {
                DSW(L(2)) = t;
                DSS(0x44F8) = (s16)(s8)DSB(L(0x15F));
            }
            DSW(0x4776)++;
            break;
        }
        case 3:
            DSW(0x4770)++;
            break;
        case 2: {
            rd_read(L(0x160), 0x10, 1);
            u16 t = DSB(L(0x160));
            if (DSW((u16)(0x451B + t * 2))++ == 0) di++;
            DSW(0x4774)++;
            break;
        }
        case 4: {
            u16 si = (u16)(DSW(0x476E)++ * 0x0C + L(0x150));    /* local place list (10 entries fit) */
            rd_read(L(0x16), 0x0A, 1);
            DSL(si) = DSL(L(0x14));
            DSL(si + 4) = DSL(L(0x10));
            DSL(si + 8) = 0;
            break;
        }
        case 5:
            DSW(0x476B)++;
            rd_read(L(0x18A), 0x3A, 1);
            if (DSB(L(0x18A)) == 1) { DSW(0x4772)++; DSW(0x451B) = 1; }
            DSW((u16)(0x4763 + (s8)DSB(L(0x18A)) * 2))++;
            break;
        case 8:
            if (DSL(0x4745) == 0) ds_far_wr(0x4745, rd_tell());
            script_skip_record();
            break;
        case 9:
            if (DSL(0x4749) == 0) ds_far_wr(0x4749, rd_tell());
            script_skip_record();
            break;
        default:                                             /* 6 and > 9: nothing is skipped */
            break;
        }
    }
    model_slots_assign(0x4601, 0x451B);
    DSW(0x4778) = (u16)(DSW(0x4776) + DSW(0x4774));
    DSW(0x4513) = di;
    wind_select(1);
    pilots_load(L(0xD8), DSB(0x4781));
    if (DSW(0x0536) != 0xFFFF) DSB(0x477D) = DSB(0x0536);   /* -W<n> */
    rtl_itoa(DSB(0x477D), 0x07DA, 10);                       /* "AW" + area number */
    DSW(0x55D6) = res_load_set(0x084A, DSB(0x4781), 0x9F, 0x0F);     /* PFNAME<mask> */
    cockpit_load(far_to_near_str(ds_far((u16)(DSW(0x55D6) + DSW(L(2)) * 4))));
    lso_load();
    ts_load_table_h(0, res_name(0x07D8, 0x05B8), 0);
    macros_load(res_name(0x07D8, 0x0602));
    area_load(0x07D8, L(0x150), 0x44FA, DSW(0x476E));
    g_attack_area_load(DSB(0x477D));
    gid_load(res_name(0x07D8, 0x05B8));
    misctbl_load(0x07D8);
    DSW(0x474D) = effect_frames_sum(0, 0);
    damage_tables_alloc((u16)(DSW(0x4778) + DSW(0x4772)));
    gunlists_alloc((s16)(DSW(0x4778) + DSW(0x4772)));
    bxl_table((s16)(DSW(0x4778) + DSW(0x4772)));
    u16 tbl = 2;                                             /* bp-4: next shape table */
    u16 shp = effect_frames_sum(0, 1);                       /* bp-6: next shape handle */
    aircraft_load_all(0x4601);
    rtl_strcpy(0x3FF0, far_to_near_str(ds_far((u16)(DSW(0x55D6) + DSW(L(2)) * 4))));
    DSW(0x3F0E) = tbl;
    for (u16 i = 0; i < 0x9F; i++) {
        if (DSW((u16)(0x4601 + i * 2)) == 0) continue;
        u16 nm = identity(far_to_near_str(ds_far((u16)(DSW(0x55D6) + i * 4))));
        nm = res_name(nm, 0x05B8);
        if (!ts_load_table_h(tbl++, nm, 2)) assert_fail(SNARIO_C, 0x2B7, nm);
        DSW((u16)(0x4601 + i * 2)) = shp++;
    }
    res_free(DSW(0x55D6));
    DSW(0x3F0C) = (u16)(tbl - 1);
    DSW(0x3F0A) = tbl;
    DSW(0x55D4) = res_load_set(0x0851, DSB(0x4781), 0x73, 0x0A);     /* SFNAME<mask> */
    ship_load_all(0x451B);
    for (u16 i = 0; i < 0x73; i++) {
        if (DSW((u16)(0x451B + i * 2)) == 0 || i == 0) continue;      /* type 0 (group marker) has no shape */
        u16 nm = identity(far_to_near_str(ds_far((u16)(DSW(0x55D4) + i * 4))));
        nm = res_name(nm, 0x05B8);
        if (!ts_load_table_h(tbl++, nm, 3)) assert_fail(SNARIO_C, 0x2D3, nm);
        DSW((u16)(0x451B + i * 2)) = shp++;
    }
    res_free(DSW(0x55D4));
    DSW(0x3F08) = (u16)(tbl - 1);
    shape_cache_build();
    shape_attr_build();
#undef L
    stk_release(mk);
}

/* AP 56ac:07b4 */
void scenario_unload(void)
{
    shape_attr_free();
    shape_cache_free();
    area_free2();
    for (s16 i = (s16)DSW(0x4513); --i > 1; ) ts_free_table(i);
    ts_free_table(0);
    misctbl_free();
    ship_free_all();
    cockpit_unload();
    aircraft_free_all();
    pilots_free();
    lso_free();
}

/* AP 56ac:0809 — places with world targets get the shape and position of their first target */
static void places_link(void)
{
    u16 k = 0, si = DSW(0x4755), tp = 0x44FA;
    for (s16 i = 0; i < DSS(0x476E); i++, tp = (u16)(tp + 2), si = (u16)(si + 0x20)) {
        DSW(si + 0x18) = k;
        DSW(si + 0x1A) = DSW(tp);
        if (DSW(tp)) {
            FarPtr e = ds_far(0x5142);
            FarPtr g = far_rd(e.seg, (u16)(e.off + k * 6));
            DSW(si + 2) = rd16(g.seg, (u16)(g.off + 2));
            DSW(si + 0x14) = rd16(g.seg, (u16)(g.off + 0x10));
            memmove(DSP(si + 4), mp(g.seg, (u16)(g.off + 4)), 12);          /* SCOPY@ */
            k = (u16)(k + DSW(si + 0x1A));
        } else DSW(si + 0x18) = 0;
    }
}

static void flights_place(void);

/* ==== 56ac:089e scenario_build — pass 2: objects, places, waypoints, flights ========================= */

/* AP 56ac:0b78 — one waypoint: kind 0 = a point (14 bytes), else a reference + distance (8 bytes) */
static u16 waypoint_read(u16 out, u16 head)
{
    DSB(out + 1) = DSB(head + 1);
    DSB(out) = DSB(head);
    u16 mk = stk_mark();
    if (DSB(head) == 0) {
        u16 t = stk_alloc(12);
        rd_read(t, 12, 1);
        memcpy(DSP(out + 2), DSP(t), 12);
        stk_release(mk);
        return 14;
    }
    u16 idx = stk_alloc(2), val = stk_alloc(2);
    rd_read(idx, 2, 1);
    rd_read(val, 2, 1);
    u16 i = DSW(idx), di = 0;       /* PORT: kinds > 5 stored the caller's DI (garbage); never in the data */
    switch ((s16)(s8)DSB(head)) {
    case 1: di = (u16)(DSW(0x4753) + i * 0x56); break;
    case 2: di = DSW((u16)(DSW(0x475B) + i * 2)); break;
    case 3: di = DSW((u16)(DSW(0x4759) + i * 2)); break;
    case 4: di = DSW((u16)(DSW(0x4757) + i * 2)); break;
    case 5: di = (u16)(DSW(0x4755) + (i << 5)); break;
    }
    DSW(out + 2) = di;
    DSL(out + 4) = (u32)DSW(val) << 2;
    stk_release(mk);
    return 8;
}

/* AP 56ac:0c6a — the 56h-byte flight record from the 58-byte group record R (script position src) */
static u16 flight_build(u16 out, u16 r_ds, FarPtr src)
{
    if (memcmp(DSP(r_ds + 0x28), DSP(DSW(0x3716)), 6) == 0) {
        /* escort offset unset: default (−6000, 9000, 1500) written into the script copy only */
        wr16(src.seg, (u16)(src.off + 0x28), 0xE890);
        wr16(src.seg, (u16)(src.off + 0x2A), 0x2328);
        wr16(src.seg, (u16)(src.off + 0x2C), 0x05DC);
    }
    u16 mk = stk_mark();
    u16 R = stk_alloc(0x3A), F = stk_alloc(0x56);
    memcpy(DSP(R), DSP(r_ds), 0x3A);
    memset(DSP(F), 0, 0x56);
    DSB(F + 1) = DSB(R + 1);
    DSB(F + 2) = DSB(R + 3);
    DSW(F + 0x1C) = (u16)((s16)(s8)DSB(R + 4) << 8);
    DSB(F + 0x3C) = DSB(R + 0x21);
    far_wr(cur_dgroup, (u16)(F + 0x52), src);
    if (DSS(R + 0x2E) >= 0) DSW(F + 0x3E) = (u16)(DSW(0x4753) + DSW(R + 0x2E) * 0x56);
    DSB(F) = DSB(R);
    if (DSB(F) == 0) {
        if (DSS(R + 8) >= 0) {
            DSB(F + 3) = DSB(R + 5);
            if (DSB(F + 3)) DSW(F + 4) = DSW((u16)(DSW(0x4759) + DSW(R + 8) * 2));
            else DSW(F + 4) = (u16)(DSW(0x4755) + (DSW(R + 8) << 5));
        }
        if ((s8)DSB(R + 2) > 0)
            DSL(F + 0x26) = rd32(ALT_SEG, (u16)(((s16)(s8)DSB(R + 2) << 3) + 0x2D));
    }
    DSW(F + 6) = DSW(R + 0x0A);
    DSW(F + 0x40) = DSW(R + 0x0A);
    if (DSB(F) == 0) {
        u16 n = (u16)(DSW(R + 0x0A) << 1);
        DSW(F + 8) = arena_alloc(n);
        memmove(DSP(DSW(F + 8)), DSP(DSW(0x475B) + (u16)(DSW(R + 0x0C) << 1)), n);
    } else {
        u16 lst = DSW((u16)(0x080F + (s16)(s8)DSB(F) * 2));
        DSW(F + 8) = (u16)(DSW(lst) + (u16)(DSW(R + 0x0C) << 1));
    }
    if (DSB(F) == 1) {
        DSW(F + 0x2E) = ship_create(0x07DF);                 /* the group marker object */
        OB(DSW(F + 0x2E), 0x26) = OB(DSW(DSW(F + 8)), 0x26);
    }
    DSB(F + 0x42) = OB(DSW(DSW(F + 8)), 0x26);
    if (DSW(R + 0x1A) != 0) {
        if (DSB(F) != 2) {
            u16 n = (u16)(DSW(R + 0x1A) << 1);
            u16 extra = (u16)((s16)(s8)DSB(F + 0x3D) << 1);
            DSW(F + 0x1A) = arena_alloc((u16)(n + extra));
            memmove(DSP(DSW(F + 0x1A)), DSP(DSW(0x475F) + (u16)(DSW(R + 0x1C) << 1)), n);
        } else DSW(F + 0x1A) = (u16)(DSW(0x475F) + (u16)(DSW(R + 0x1C) << 1));
        DSW(F + 0x18) = DSW(R + 0x1A);
    }
    if (DSS(R + 0x0E) >= 0) DSW(F + 0x0A) = DSW((u16)(DSW(0x475F) + DSW(R + 0x0E) * 2));
    DSB(F + 0x0E) = DSB(R + 0x12);
    DSB(F + 0x43) = DSB(R + 6);
    for (s16 i = 0; i < (s16)(s8)DSB(F + 0x0E); i++) {
        u16 j = DSW((u16)(R + 0x14 + i * 2)), c = 0;
        switch ((s16)(s8)DSB(R + 6)) {
        case 0: c = (u16)(DSW(0x4753) + j * 0x56); break;
        case 1: c = (u16)(DSW(0x4755) + (j << 5)); break;
        case 2: c = DSW((u16)(DSW(0x4759) + j * 2)); break;
        case 3: c = DSW((u16)(DSW(0x475B) + j * 2)); break;
        }
        DSW((u16)(F + 0x10 + i * 2)) = c;
    }
    heap_hook();
    if (DSB(R + 0x1E)) DSB(F + 0x3B) |= 2;                   /* standby */
    memcpy(DSP(out), DSP(F), 0x56);
    stk_release(mk);
    return out;
}

void scenario_build(void)
{
    u16 mk = stk_mark();
    u16 rec1 = stk_alloc(0x16), rec2 = stk_alloc(0x10), rec4 = stk_alloc(0x0A), head = stk_alloc(2);
    u16 rec5 = stk_alloc(0x3A);
    PLAYER = 0;
    DSW(0x3B07) = 0;
    plane_pool_create(DSW(0x4776));
    ship_pool_create((u16)(DSW(0x4774) + DSW(0x4772)));
    DSW(0x475D) = DSW(0x475B) = arena_alloc((u16)((DSW(0x4778) + DSW(0x4772)) << 1));
    DSW(0x4759) = (u16)(DSW(0x475D) + (DSW(0x4776) << 1));
    u16 wdata = DSW(0x4761) = arena_alloc(DSW(0x473F));
    u16 wptr = DSW(0x475F) = arena_alloc((u16)(DSW(0x4741) << 1));
    DSW(0x4753) = arena_alloc((u16)(DSW(0x476B) * 0x56));
    DSW(0x4751) = arena_alloc((u16)(DSW(0x476B) << 1));
    fill_seq(DSW(0x4751), DSW(0x4753), 0x56, DSW(0x476B));
    DSW(0x4755) = arena_alloc((u16)(DSW(0x476E) << 5));
    movers_alloc(0x28);
    cockpit_reset();
    fm_reset_globals();
    wind_select(0);

    /* pass A: planes, ships, places */
    script_open(ds_far(0x4517), DSS(0x4515));
    u16 di = DSW(0x475B), ships = DSW(0x4759), si = DSW(0x4755);
    while (script_next()) {
        switch (DSW(0x4510)) {
        case 1:
            rd_read(rec1, 0x16, 1);
            heap_hook();
            DSW(di) = plane_create(rec1);
            di = (u16)(di + 2);
            heap_hook();
            break;
        case 2:
            rd_read(rec2, 0x10, 1);
            heap_hook();
            DSW(ships) = ship_create(rec2);
            ships = (u16)(ships + 2);
            heap_hook();
            break;
        case 4:
            rd_read(rec4, 0x0A, 1);
            DSL(si + 4) = DSL(rec4 + 2);
            DSL(si + 8) = DSL(rec4 + 6);
            DSL(si + 0x0C) = 0;
            DSB(si + 0x1C) = DSB(rec4);
            DSB(si + 0x1D) = DSB(rec4 + 1);
            si = (u16)(si + 0x20);
            break;
        default:
            script_skip_record();
        }
    }
    places_link();

    /* pass B: waypoints */
    script_open(ds_far(0x4517), DSS(0x4515));
    while (script_next()) {
        if (DSW(0x4510) != 7) { script_skip_record(); continue; }
        rd_read(head, 2, 1);
        DSW(wptr) = wdata;
        wptr = (u16)(wptr + 2);
        wdata = (u16)(wdata + waypoint_read(wdata, head));
    }

    /* pass C: flights (the script position is taken before the record is read) */
    script_open(ds_far(0x4517), DSS(0x4515));
    si = DSW(0x4753);
    while (script_next()) {
        if (DSW(0x4510) != 5) { script_skip_record(); continue; }
        FarPtr pos = rd_tell();
        rd_read(rec5, 0x3A, 1);
        flight_build(si, rec5, pos);
        si = (u16)(si + 0x56);
    }
    flights_place();
    wind_apply();

    /* the player's carrier group is on standby and the player is near it: launch it */
    u16 bx = DSW(0x3B07);
    if (DSB(bx + 3)) {
        u16 c = DSW(bx + 4);
        if (DSB((u16)(DSW(c + 0x34) + 0x3B)) & 2) {
            if (ldist2((u16)(c + 4), (u16)(PLAYER + 4)) < 0x9C40) launch_intercept(DSW(c + 0x34), 0);
        }
    }
    stk_release(mk);
}

/* AP 56ac:0f26 — qsort comparator of two Flight*: ship groups first, then protectors/escorts after their
 * charges; equal aircraft flights compare −1 (not symmetric: the result depends on Borland's qsort) */
s16 flight_cmp(u16 a, u16 b)
{
    u16 si = DSW(a), di = DSW(b);
    s16 d = (s16)((s16)(s8)DSB((u16)(0x0815 + (s8)DSB(si))) - (s16)(s8)DSB((u16)(0x0815 + (s8)DSB(di))));
    if (d == 0 && DSB(si) == 0) {
        d = -1;
        if (DSB(si + 2) == 6) {
            if ((s16)find_int((u16)(si + 0x10), di, 3) >= 0) d = 1;
        } else if (DSW(si + 0x3E) == di) d = 1;
    }
    return d;
}

/* AP 56ac:0f96 — qsort with the comparator's VROOMM stub 3D50:0061 */
static void flights_sort(void)
{
    rtl_qsort(DSW(0x4751), DSW(0x476B), 2, codeptr_far(0x3D500061u));
    DSW(0x474F) = (u16)(DSW(0x4751) + (DSW(0x4765) << 1));
}

/* AP 56ac:107a */
static void movers_add(u16 o)
{
    DSW((u16)(DSW(0x00BC) + (DSW(0x00BE) << 1))) = o;
    DSW(0x00BE)++;
    memmove(DSP(o + 0x36), DSP(o + 4), 12);
}

/* AP 56ac:10a8 — place the members of an aircraft flight, set up FM, brain and ordnance */
static void flight_place_planes(u16 f)
{
    u16 mk = stk_mark();
    u16 B = (u16)(stk_alloc(0x4A) + 0x4A);
#define L(o) ((u16)(B - (o)))
    DSW(L(2)) = 0;                                           /* member index */
    DSW(L(4)) = 0;
    while (DSS(f + 6) > DSS(L(2))) {
        u16 di = 0;                                          /* throttle */
        DSW(L(0x0A)) = 0;                                    /* parked */
        u16 p = DSW((u16)(DSW(f + 8) + DSW(L(4))));
        DSW(L(6)) = p;
        OW(p, 0x34) = f;
        OB(p, 0xBC) = DSB(L(2));
        u16 dat = DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2));
        OW(p, 0x66) = DSW(dat + 0x24);
        DSB(f + 0x42) = OB(p, 0x26);
        u16 si;
        if (DSL(f + 0x26) != 0) {                            /* airborne */
            OW(p, 0x14) = DSW(f + 0x1C);
            di = 0xE6;
            if (DSW(L(2)) == 0) {
                if (DSW(f + 0x0A)) memmove(DSP(p + 4), DSP(waypoint_pos(DSW(f + 0x0A))), 12);
                else memmove(DSP(p + 4), DSP(flight_home(f)), 12);
                if (OSL(p, 0x0C) <= 0) OL(p, 0x0C) = DSL(f + 0x26);
                DSW(L(8)) = p;                               /* leader */
                obj_set_angles(L(0x3C), (u16)(p + 0x10));
                si = 0x41C;
            } else {
                u16 off = formation_offset_of(p, DSW(L(2)));
                obj_to_world((u16)(p + 4), (u16)(DSW(L(8)) + 4), off, L(0x3C));
                si = 0x430;
            }
            DSB(OW(p, 0x16) + 1) = 0;
        } else {                                             /* parked */
            DSW(L(0x0A)) = 1;
            if (DSB(f + 3)) { park_on_carrier(p, DSS(L(2))); si = 0x3F4; }
            else { park_on_airfield(p, DSS(L(2))); si = 0x408; }
            dat = DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2));
            if (DSB(dat + 0x76) == 0) DSB(OW(p, 0x16) + 1) = (u8)(shape_nvariants((s16)OW(p, 2), 1) - 1);
            else DSB(OW(p, 0x16) + 1) = 0;
            DSB(OW(p, 0x16)) = (u8)(shape_nvariants((s16)OW(p, 2), 0) - 1);
            OB(p, 0xBA) |= 0x10;                             /* gear down */
        }
        if (DSW(L(2)) == 0) DSL(f + 0x4E) = OSL(p, 0x0C) > 0x64 ? OL(p, 0x0C) : 0x2710;
        if (OB(p, 0xBB) == 0x2C && DSW(L(0x0A)) == 0) si = 0x458;
        OW(p, 0xB2) = di;
        u16 m = obj_matrix(OW(p, 0x20));
        dat = DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2));
        fm_init((u16)(p + 0x64), (u16)(p + 2), DSW(dat + 0x24), m, di);
        brain_set_state((u16)(p + 0x4A), si);
        movers_add(p);

        /* ordnance defaults from the group record in the script */
        FarPtr src = far_rd(cur_dgroup, (u16)(f + 0x52));
        u16 r = (u16)(p + 0xEB);
        if ((s8)DSB(r + 1) < 0) {
            u8 v = rd8(src.seg, (u16)(src.off + 0x31));
            DSB(r) = v;
            DSB(r + 1) = v;
            DSW(r + 2) = (u16)((u16)(1u << (v & 0x1F)) - 1);
        }
        if ((s8)OB(p, 0xF1) < 0) OB(p, 0xF1) = rd8(src.seg, (u16)(src.off + 0x33)) ? 1 : 0;
        if (OB(p, 0xF1)) OB(p, 0x1B) |= 0x40;
        u16 st = (u16)(p + 0xF3), so = (u16)(src.off + 0x34);
        for (int k = 0; k < 3; k++, so = (u16)(so + 2), st = (u16)(st + 2)) {
            if ((s8)DSB(st) < 0) DSW(st) = rd16(src.seg, so);
            if (DSB(st + 1)) OB(p, 0x1B) |= 0x40;
        }

        /* mass: 400h + old + (Σ weight[kind]·count + torpedo·TORPS[side]+04) << (10 − DAT[5Ah]) / DAT[0] */
        memcpy(DSP(L(0x4A)), DSP(0x0819), 14);
        dat = DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2));
        s32 mass = (s32)(s16)((s16)(s8)OB(p, 0xF1) * (s16)torp_speed_limit((s16)(s8)OB(p, 0x26)));
        st = (u16)(p + 0xF3);
        for (int k = 0; k < 3; k++, st = (u16)(st + 2))
            mass += (s32)(s16)(DSS((u16)(L(0x4A) + (s8)DSB(st) * 2)) * (s16)(s8)DSB(st + 1));
        if (mass != 0) {
            u8 cl = (u8)(10 - DSB(dat + 0x5A));
            mass = idiv32_386((int64_t)(s32)((u32)mass << (cl & 0x1F)), (s32)DSS(dat), NULL);
        }
        OW(p, 0xB6) = (u16)((u16)mass + OW(p, 0xB6) + 0x400);
        if (OB(p, 0x1A) & 4) OW(p, 0xB6) = (u16)(OW(p, 0xB6) + DSW(dat + 0x7F));   /* drop tank */

        DSW(L(4)) = (u16)(DSW(L(4)) + 2);
        DSW(L(2))++;
    }
#undef L
    stk_release(mk);
}

/* AP 56ac:1483 — ship group: leader at the waypoint, ships at their formation offsets */
static void flight_place_ships(u16 f)
{
    u16 mk = stk_mark();
    u16 mat = stk_alloc(0x14), v = stk_alloc(12);
    s16 row = (s16)((s16)(s8)DSB(f + 1) * 0x0E + (s16)0xFF12);
    FarPtr rp = ds_far(0x12A4);
    rp.off = (u16)(rp.off + (row << 2));
    u16 marker = DSW(f + 0x2E);
    ship_brain_init(marker);
    OW(marker, 0x34) = f;
    u16 leader = 0;
    for (s16 i = 0; DSS(f + 6) > i; i++) {
        u16 p = DSW((u16)(DSW(f + 8) + i * 2));
        OW(p, 0x34) = f;
        OW(p, 0x14) = DSW(f + 0x1C);
        if (i == 0) {
            memset(DSP(p + 0x68), 0, 12);
            memmove(DSP(p + 4), DSP(waypoint_pos(DSW(f + 0x0A))), 12);
            leader = p;
            mat_from_angles((u16)(p + 0x10), mat);
            memcpy(DSP(marker + 4), DSP(leader + 4), 12);
            OW(marker, 0x10) = OW(leader, 0x10);
            OW(marker, 0x12) = OW(leader, 0x12);
            OW(marker, 0x14) = OW(leader, 0x14);
        } else {
            if (rtl_labs(OSL(p, 0x68)) < 2 && rtl_labs(OSL(p, 0x6C)) < 2) {    /* default formation slot */
                OL(p, 0x68) = rd32(rp.seg, rp.off);
                OL(p, 0x6C) = rd32(rp.seg, (u16)(rp.off + 4));
            }
            rp.off = (u16)(rp.off + 8);
            lrotate((u16)(p + 0x68), mat, v);
            ladd3(v, (u16)(leader + 4), (u16)(p + 4));
        }
        if (OB(p, 0x19)) movers_add(p);
        ship_brain_init(p);
    }
    stk_release(mk);
}

/* AP 56ac:1642 */
static u16 flight_protects(u16 a, u16 b)
{
    return DSB(a + 2) == 6 && (s16)find_int((u16)(a + 0x10), b, 3) >= 0;
}

/* AP 56ac:1670 — cover mode 0: b is one of a's references; mode 2: one of a's references is a member of b */
static u8 flight_covers(u16 a, u16 b)
{
    if (DSB(a + 2) != 5) return 0;
    switch ((s16)(s8)DSB(a + 0x43)) {
    case 0:
        return (s16)find_int((u16)(a + 0x10), b, (u16)(s16)(s8)DSB(a + 0x0E)) >= 0;
    case 2:
        for (s16 i = 0; (s16)(s8)DSB(a + 0x0E) > i; i++)
            if ((s16)find_int(DSW(b + 8), DSW((u16)(a + 0x10 + i * 2)), DSW(b + 6)) >= 0) return 1;
        return 0;
    }
    return 0;
}

/* AP 56ac:16f9 */
static u16 flights_linked(u16 a, u16 b)
{
    if ((s16)(s8)DSB(a + 0x42) + (s16)(s8)DSB(b + 0x42) == 1) return 0;
    if (DSW(a + 0x3E) == b) return 1;
    if (DSW(b + 0x3E) == a) return 1;
    if (flight_protects(a, b)) return 1;
    if (flight_protects(b, a)) return 1;
    if (flight_covers(a, b)) return 1;
    if (flight_covers(b, a)) return 1;
    return 0;
}

/* AP 56ac:1783 — every flight transitively linked to f; common cruise speed; linked list → f+44/46 */
static void flight_link(u16 f)
{
    u16 mk = stk_mark();
    /* PORT: the original's list is 5 words at bp−0Ah and grows into its own frame; room for more here */
    u16 list = stk_alloc(0x80);
    DSW(list) = f;
    s16 n = 1, done;
    do {
        done = 1;
        u16 slot = (u16)(list + n * 2);
        u16 di = DSW(0x4753);
        for (s16 k = 0; k < DSS(0x476B); k++, di = (u16)(di + 0x56)) {
            if ((s16)find_int(list, di, (u16)n) >= 0) continue;
            for (s16 s = 0; s < n; s++) {
                if (flights_linked(DSW((u16)(list + s * 2)), di)) {
                    DSW(slot) = di;
                    slot = (u16)(slot + 2);
                    n++;
                    done = 0;
                    break;
                }
            }
        }
    } while (!done);
    s16 cx = 0x2710;
    for (s16 k = 0; k < n; k++) {
        u16 g = DSW((u16)(list + k * 2));
        if (DSB(g) != 0) continue;
        u16 mem = DSW(g + 8);
        for (s16 i = 0; DSS(g + 6) > i; i++) {
            u16 p = DSW((u16)(mem + i * 2));
            s16 cs = DSS((u16)(DSW((u16)(DSW(0x55D2) + DSB((u16)(0x54BC + OB(p, 0x19))) * 2)) + 0x73));
            if (cs < cx) cx = cs;
        }
    }
    for (s16 k = 0; k < n; k++) {
        u16 g = DSW((u16)(list + k * 2));
        if (DSB(g) == 0) DSW(g + 0x30) = (u16)cx;
    }
    if (n > 1) {
        DSW(f + 0x44) = (u16)(n - 1);
        memmove(DSP(f + 0x46), DSP(list + 2), (size_t)(u16)((n - 1) * 2));
    }
    stk_release(mk);
}

/* AP 56ac:0fc2 */
static void flights_place(void)
{
    flights_sort();
    for (s16 i = 0; i < DSS(0x476B); i++) {
        u16 f = DSW((u16)(DSW(0x4751) + i * 2));
        switch ((s16)(s8)DSB(f)) {
        case 0: flight_link(f); flight_place_planes(f); break;
        case 1: flight_link(f); flight_place_ships(f); break;
        }
    }
    DSW(0x3B07) = OW(PLAYER, 0x34);
    for (s16 i = 0; i < DSS(0x4778); i++) {
        u16 o = DSW((u16)(DSW(0x475D) + i * 2));
        u16 g = OW(o, 0x34);
        if (g == DSW(0x3B07) || (s16)find_int((u16)(g + 0x46), DSW(0x3B07), DSW(g + 0x44)) >= 0) OB(o, 0x1C) |= 4;
    }
}

/* AP 56ac:18e5 */
void scenario_free(void)
{
    cockpit_leave();
    ship_pool_destroy();
    plane_pool_destroy();
    DSW(0x475B) = 0;
}

__attribute__((constructor)) static void snario_codeptrs_register(void)
{
    codeptr_register(PROG_AP, 0x56AC0F26u, (CodeFn)flight_cmp);          /* qsort comparator (stub 3D50:0061) */
}
