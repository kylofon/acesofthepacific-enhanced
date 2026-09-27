/* render3d — the ts2 shape library: shape tables and accessors (ts_util.c AP 271b / PS 47c8), table
 * loading (ts_utilo.c AP 7074 / PS 48ea), chunk reading (ts_file.c AP 7039 / PS 48c0), the fast shape
 * cache (AP 7017, 7032) and the sprite sets (AP 243d). render3d_shapes.md §1, §5; video_ts.md §4.
 *
 * PORT: shape tables loaded through memory handles (AP ts_load_table_h with EMS) use the platform's
 * handle functions; the tables' pointers point into mem[]. */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"
#include "platform/rtl.h"
#include "video/video.h"      /* ts_fatal (PS 4960:0094) */

/* ---- assertions ---------------------------------------------------------------------------------- */
void r3d_assert(u16 ap_file_ds, s16 ap_line, u16 ps_file_ds, s16 ps_line)
{
    if (r3d_is_ps()) assert_fail(ds_ptr(ps_file_ds), ps_line, 0);   /* PS 227c:0143 (3 words pushed) */
    else assert_fail0(ds_ptr(ap_file_ds), ap_line);                 /* AP 2494:01ca */
}
/* AP assert_fail (2494:0127) with a message / PS ts_fatal (4960:0094) */
static void r3d_fatal(u16 ap_file, s16 ap_line, u16 ap_msg, u16 ps_file, s16 ps_line, u16 ps_msg)
{
    if (r3d_is_ps()) ts_fatal(ds_ptr(ps_file), ps_line, ps_msg);
    else assert_fail(ds_ptr(ap_file), ap_line, ap_msg);
}
static u16 r3d_malloc(u16 n) { return r3d_is_ps() ? lib_realloc(n) : safe_malloc(n); }   /* PS 0000:03c1 / AP 0000:035c */

static inline u16 tbl(s16 t) { return (u16)(DSW(DS_TABLES) + 12 * t); }   /* 12-byte table record */

/* ================================================================================================
 * Shape pointers (render3d_shapes.md §5)
 * ============================================================================================== */

/* PS 47c8:0000 / AP 271b:0007 — far pointer of a global shape id; sets CS:0A25 = table index */
FarPtr shape_ptr(s16 id)
{
    FarPtr none = { 0, 0 };
    FarPtr fast = ds_far(DS_FAST_PTR);
    if (!far_is_null(fast)) {
        if (r3d_is_ps()) {
            if (id < (s16)DSW(DS_TOTAL)) return far_rd(fast.seg, (u16)(fast.off + 4 * id));
            return none;                                          /* PS: no walk when the cache exists */
        }
        if (id < (s16)DSW(DS_TOTAL)) {
            FarPtr t = ds_far(DS_FAST_TBL);
            CSW(CS_SHAPE_TBL) = (u16)(s16)(s8)rd8(t.seg, (u16)(t.off + id));
            u16 r = tbl((s16)CSW(CS_SHAPE_TBL));
            if (DSW(r + 2) == 0) pool_map(DSW(r));               /* lock the table's handle */
            return far_rd(fast.seg, (u16)(fast.off + 4 * id));
        }
    }
    for (s16 si = 0; si < (s16)DSW(DS_NTABLES); si++) {
        u16 r = tbl(si);
        if ((s16)DSW(r + 4) > id) {
            CSW(CS_SHAPE_TBL) = (u16)si;
            if (!r3d_is_ps() && DSW(r + 2) == 0) {
                FarPtr p = pool_map(DSW(r));
                return far_rd(p.seg, (u16)(p.off + 4 * id));
            }
            FarPtr p = ds_far(r);
            return far_rd(p.seg, (u16)(p.off + 4 * id));
        }
        id = (s16)(id - (s16)DSW(r + 4));
    }
    return none;
}

/* PS 47c8:008f / AP 271b:011d — store a shape pointer (and into the fast cache); returns it */
void shape_set_ptr(s16 id, FarPtr p)
{
    FarPtr fast = ds_far(DS_FAST_PTR);
    if (!far_is_null(fast) && id < (s16)DSW(DS_TOTAL)) {
        if (!r3d_is_ps()) {
            FarPtr t = ds_far(DS_FAST_TBL);
            CSW(CS_SHAPE_TBL) = (u16)(s16)(s8)rd8(t.seg, (u16)(t.off + id));
            u16 r = tbl((s16)CSW(CS_SHAPE_TBL));
            if (DSW(r + 2) == 0) pool_map(DSW(r));
        }
        far_wr(fast.seg, (u16)(fast.off + 4 * id), p);
        return;
    }
    if (!far_is_null(fast) && r3d_is_ps()) return;
    for (s16 si = 0; si < (s16)DSW(DS_NTABLES); si++) {
        u16 r = tbl(si);
        if ((s16)DSW(r + 4) > id) {
            CSW(CS_SHAPE_TBL) = (u16)si;
            FarPtr base = (!r3d_is_ps() && DSW(r + 2) == 0) ? pool_map(DSW(r)) : ds_far(r);
            far_wr(base.seg, (u16)(base.off + 4 * id), p);
            return;
        }
        id = (s16)(id - (s16)DSW(r + 4));
    }
}

/* PS 47c8:0132 / AP 271b:0251 — camera record (render3d_math.md §2.1) */
u16 view_new(s16 eye)
{
    u16 v = r3d_malloc(0x16);
    if (!v) return 0;
    DSW(v) = 7; DSW(v + 2) = (u16)eye;
    DSW(v + 4) = 0; DSW(v + 6) = 0; DSW(v + 8) = 0;
    DSW(v + 0x0A) = 0; DSW(v + 0x0C) = 0; DSW(v + 0x0E) = 0x140; DSW(v + 0x10) = 0xC8;
    DSW(v + 0x12) = 0; DSW(v + 0x14) = 0;
    return v;
}
/* PS 47c8:0188 / AP 271b:02a6 */
void view_free(u16 v_ds) { lib_free(v_ds); }

/* the accessors assert a non-null header (AP DS:229C + 18h·i, line; PS DS:39EA + 18h·i, line + 0Ah) */
static u8 *hdr_of(s16 id, int i, s16 ap_line)
{
    FarPtr p = shape_ptr(id);
    if (far_is_null(p)) r3d_assert((u16)(0x229C + 0x18 * i), ap_line, (u16)(0x39EA + 0x18 * i), (s16)(ap_line + 10));
    return far_mp(p);
}
/* PS 47c8:0196 / AP 271b:02b5 */
u8 shape_flags(s16 id) { return hdr_of(id, 0, 0x71)[0]; }
/* PS 47c8:01da / AP 271b:02f5 */
u8 shape_class(s16 id) { return hdr_of(id, 1, 0x78)[1]; }
/* PS 47c8:021f / AP 271b:0336 */
void shape_set_class(s16 id, u8 c) { hdr_of(id, 2, 0x7F)[1] = c; }
/* PS 47c8:0267 / AP 271b:037a */
u8 shape_subclass(s16 id) { return hdr_of(id, 3, 0x86)[2]; }
/* PS 47c8:02ac / AP 271b:03bb */
u8 shape_shift(s16 id) { return hdr_of(id, 4, 0x8D)[3]; }
/* PS 47c8:02f1 / AP 271b:03fc */
s16 shape_nslots(s16 id) { return gs16(hdr_of(id, 5, 0x94), 4); }
/* PS 47c8:0336 / AP 271b:043d — hdr[hdr[6] + slot], asserts 0 <= slot < nslots */
s16 shape_nvariants(s16 id, s16 slot)
{
    FarPtr p = shape_ptr(id);
    if (far_is_null(p)) r3d_assert(0x232C, 0x9C, 0x3A7A, 0xA6);
    if (slot < 0 || (s16)rd16(p.seg, (u16)(p.off + 4)) <= slot) r3d_assert(0x2344, 0x9D, 0x3A92, 0xA7);
    u16 o = rd16(p.seg, (u16)(p.off + 6));
    return rd8(p.seg, (u16)(o + slot));                           /* ES = the header segment */
}
/* PS 47c8:03b1 / AP 271b:04b4 */
s16 shape_radius(s16 id) { return gs16(hdr_of(id, 8, 0xA5), 0x0C); }
/* PS 47c8:03f6 / AP 271b:04f5 */
void shape_set_radius(s16 id, s16 r) { p16(hdr_of(id, 9, 0xAC), 0x0C, (u16)r); }
/* PS 47c8:043e / AP 271b:0539 — (i32)radius << shift (LXLSH@) */
s32 shape_radius_world(s16 id)
{
    u8 *h = hdr_of(id, 10, 0xB5);
    u8 cl = h[3];
    return cl >= 32 ? 0 : (s32)((u32)(s32)gs16(h, 0x0C) << cl);
}
/* PS 47c8:0490 / AP 271b:0584 — copy the box min/max when the pointers are non-null */
void shape_box(s16 id, u16 mn, u16 mx)
{
    u8 *h = hdr_of(id, 11, 0xBE);
    if (mn) memcpy(DSP(mn), h + 0x0E, 6);
    if (mx) memcpy(DSP(mx), h + 0x14, 6);
}

/* ================================================================================================
 * ts_file.c (AP 7039 / PS 48c0)
 * ============================================================================================== */

#define DS_TS_OPENED r3d_ds(0x2408, 0x3B58)       /* 1 = the chunk file was opened by ts_open */

/* PS 48c0:0005 / AP 7039:0000 — read n bytes of f into far memory through a malloc'd buffer (16 KB,
 * halving to 2 KB then −256 steps, finally a 256-byte stack buffer); asserts the full count */
void ts_read(FarPtr dst, u32 n, u16 f)
{
    u16 m = stk_mark();
    s16 si = 0x4000;
    u16 buf = 0;
    while (si) {
        buf = r3d_malloc((u16)si);
        if (buf) break;
        if (si > 0x800) si >>= 1; else si = (s16)(si - 0x100);
    }
    bool heap = si != 0;
    if (!si) { buf = stk_alloc(0x100); si = 0x100; }
    u32 lin = far_lin(dst);
    while (n) {
        u16 want = (s32)n < si ? (u16)n : (u16)si;
        s16 got = my_fread(buf, 1, (s16)want, f);
        if (!got) break;
        FarPtr d = far_norm(lin);
        rtl_movedata(cur_dgroup, buf, d.seg, d.off, (u16)got);
        lin += (u16)got;
        n -= (u32)(s32)got;
    }
    if (heap) lib_free(buf);
    stk_release(m);
    if (n) r3d_assert(0x2422, 0x41, 0x3B5A, 0x3F);
}

/* PS 48c0:0149 / AP 7039:0144 */
void ts_close(u16 f)
{
    if ((s16)DSW(DS_TS_OPENED) == -1) r3d_fatal(0x243A, 0x48, 0x2452, 0x3B72, 0x46, 0x3B8A);
    if (DSW(DS_TS_OPENED)) chunk_close(f);
    DSW(DS_TS_OPENED) = 0xFFFF;
}

/* PS 48c0:018b / AP 7039:0187 — f = an open chunk file or the name of one; seek to the chunk `tag` */
u16 ts_open(u16 name_ds, u16 tag_ds)
{
    if ((s16)DSW(DS_TS_OPENED) != -1) r3d_fatal(0x2453, 0x50, 0x246B, 0x3B8B, 0x4E, 0x3BA3);
    u16 f = name_ds;
    if (chunk_is_open(f)) DSW(DS_TS_OPENED) = 0;
    else {
        f = chunk_open(name_ds);
        if (!f) return 0;
        DSW(DS_TS_OPENED) = 1;
    }
    if (chunk_find(f, tag_ds, 0) == -1) { ts_close(f); return 0; }
    return f;
}

/* PS 48c0:020d / AP 7039:020a — the chunk into a DOS block (offset 0 asserted) */
FarPtr ts_load_chunk(u16 name_ds, u16 tag_ds)
{
    FarPtr p = { 0, 0 };
    u16 f = ts_open(name_ds, tag_ds);
    if (!f) return p;
    u32 size = (u32)chunk_size(f);
    p = dos_alloc(size, 0, 0);
    if (!far_is_null(p)) {
        if (p.off) r3d_assert(0x246C, 0x70, 0x3BA4, 0x6E);
        ts_read(p, size, f);
    }
    ts_close(f);
    return p;
}

/* AP 7039:02a4 — the same into a memory handle when EMS is present (DS:1FEC) and the pool allocation
 * (1d32:22f4) succeeds; the size must be <= 1:0000 (asserted). Returns the handle as DX:AX = cwd(handle)
 * or the far pointer. */
FarPtr ts_load_chunk_h(u16 name_ds, u16 tag_ds, u16 flag)
{
    FarPtr p = { 0, 0 };
    u16 f = ts_open(name_ds, tag_ds);
    if (!f) return p;
    s32 size = chunk_size(f);
    if ((size >> 16) > 1 || ((size >> 16) == 1 && (u16)size != 0)) r3d_assert(0x2484, 0x8C, 0x2484, 0x8C);
    u16 si = 0;
    if (DSB(AP_DS_ems_present)) si = ap_pool_alloc_1d32((s16)flag, (u16)size);
    if (si) p = pool_map_save(si);
    else p = dos_alloc((u32)size, 0, 0);
    if (!far_is_null(p)) {
        if (p.off) r3d_assert(0x249C, 0x97, 0x249C, 0x97);
        ts_read(p, (u32)size, f);
    }
    if (si) pool_unmap(si);
    ts_close(f);
    if (si) { p.off = si; p.seg = (s16)si < 0 ? 0xFFFF : 0; }
    return p;
}

/* ================================================================================================
 * ts_utilo.c (AP 7074 / PS 48ea)
 * ============================================================================================== */

/* PS 48ea:0009 / AP 7074:0000 */
void ts_init(s16 ntables, s16 nsets)
{
    if (ntables < 1) ntables = 1;
    if (nsets < 1) nsets = 1;
    DSW(DS_NTABLES) = (u16)ntables;
    DSW(DS_NSETS) = (u16)nsets;
    DSW(DS_TABLES) = r3d_malloc((u16)(DSW(DS_NTABLES) * 12));
    DSW(DS_SETS) = r3d_malloc((u16)(DSW(DS_NSETS) * 4));
    if (!DSW(DS_TABLES) || !DSW(DS_SETS)) r3d_fatal(0x24B5, 0x15, 0x24CE, 0x3BBD, 0x15, 0x3BD6);
    for (s16 i = 0; i < (s16)DSW(DS_NTABLES); i++) {
        u16 r = tbl(i);
        DSW(r + 2) = 0; DSW(r) = 0; DSW(r + 4) = 0; DSW(r + 6) = 0x100; DSW(r + 8) = 0x100; DSW(r + 0x0A) = 0xFFFF;
    }
    for (s16 i = 0; i < (s16)DSW(DS_NSETS); i++) { DSW(DSW(DS_SETS) + 4 * i) = 0; DSW(DSW(DS_SETS) + 4 * i + 2) = 0; }
    if (DSB(DS_TS_INIT)) {                                     /* first call: default light */
        set_light(0x2000, 0xA000);
        DSB(DS_TS_INIT) = 0;
    }
}

/* PS 48ea:0133 / AP 7074:012d */
void ts_done(void)
{
    DSW(DS_NTABLES) = 0;
    DSW(DS_NSETS) = 0;
    lib_free(DSW(DS_TABLES));
    lib_free(DSW(DS_SETS));
}

static void table_reset(u16 r)
{
    DSW(r + 6) = 0x100; DSW(r + 8) = 0x100; DSW(r + 0x0A) = 0xFFFF; DSW(r + 4) = 0;
}
/* relocate the zero-terminated far pointer list at q by the load address */
static void table_relocate(u16 r, FarPtr q, FarPtr base)
{
    while (rd16(q.seg, q.off) | rd16(q.seg, (u16)(q.off + 2))) {
        u32 lo = (u32)base.off + rd16(q.seg, q.off);
        wr16(q.seg, (u16)(q.off + 2), (u16)(base.seg + rd16(q.seg, (u16)(q.off + 2)) + (lo >> 16)));
        wr16(q.seg, q.off, (u16)lo);
        q.off = (u16)(q.off + 4);
        DSW(r + 4)++;
    }
}

/* PS 48ea:0158 / AP 7074:0154 — "DAT:" of name into table t; returns the chunk (DX:AX) or 0 */
u32 ts_load_table_far(s16 t, u16 name_ds)
{
    FarPtr p = ts_load_chunk(name_ds, r3d_ds(0x24E2, 0x3BEA));  /* "DAT:" */
    if (far_is_null(p)) return 0;
    u16 r = tbl(t);
    ds_far_wr(r, p);
    table_reset(r);
    table_relocate(r, p, p);
    return (u32)p.seg << 16 | p.off;
}

s16 ts_load_table(s16 t, u16 name_ds) { return (s16)ts_load_table_far(t, name_ds); }

/* AP 7074:02d8 — the same through a memory handle (ts_load_chunk_h); returns 1/0 */
u16 ts_load_table_h(u16 t, u16 name, u16 flag)
{
    FarPtr p = ts_load_chunk_h(name, 0x24E7, flag);            /* "DAT:" */
    if (far_is_null(p)) return 0;
    u16 r = tbl((s16)t);
    ds_far_wr(r, p);
    table_reset(r);
    u16 h = 0;
    if (p.seg == 0) {
        h = p.off;
        p = pool_map_save(h);
        if (far_is_null(p)) r3d_assert(0x24EC, 0x87, 0x24EC, 0x87);
    }
    DSW(r + 4) = 0;
    table_relocate(r, p, p);
    if (h) pool_unmap(h);
    return 1;
}

/* PS 48ea:0236 / AP 7074:022b */
void ts_free_table(s16 t)
{
    u16 r = tbl(t);
    if (!r3d_is_ps() && DSW(r + 2) == 0) ap_pool_free_1d32(DSW(r));
    else dos_free(ds_far(r));
    DSW(r + 2) = 0; DSW(r) = 0; DSW(r + 4) = 0;
    DSW(r + 6) = 0x100; DSW(r + 8) = 0x100; DSW(r + 0x0A) = 0xFFFF;
}

/* ================================================================================================
 * Fast shape cache (AP 7017, 7032) and sprite sets (AP 243d)
 * ============================================================================================== */

/* AP 7017:0000 */
void shape_cache_free(void)
{
    FarPtr p = ds_far(AP_DS_shape_ptr_cache);
    if (!far_is_null(p)) dos_free(p);
    ds_far_wr(AP_DS_shape_ptr_cache, (FarPtr){ 0, 0 });
    p = ds_far(AP_DS_shape_tbl_cache);
    if (!far_is_null(p)) dos_free(p);
    ds_far_wr(AP_DS_shape_tbl_cache, (FarPtr){ 0, 0 });
}

/* AP 7017:004f — far pointer per id and table index per id */
void shape_cache_build(void)
{
    FarPtr ptrs = { 0, 0 }, idx = { 0, 0 };
    DSW(AP_DS_shape_total) = 0;
    for (s16 t = 0; t < (s16)DSW(AP_DS_shape_ntables); t++) DSW(AP_DS_shape_total) += DSW(tbl(t) + 4);
    ptrs = dos_alloc((u32)(u16)(DSW(AP_DS_shape_total) * 4), 0, 0);
    if (far_is_null(ptrs)) goto done;
    {
        s16 si = 0;
        for (s16 t = 0; si < (s16)DSW(AP_DS_shape_total) && t < (s16)DSW(AP_DS_shape_ntables); t++)
            for (s16 di = 0; (s16)DSW(tbl(t) + 4) > di; di++, si++)
                far_wr(ptrs.seg, (u16)(ptrs.off + 4 * si), shape_ptr(si));
    }
    idx = dos_alloc(DSW(AP_DS_shape_total), 0, 0);
    if (!far_is_null(idx)) {
        s16 si = 0;
        for (s16 t = 0; t < (s16)DSW(AP_DS_shape_ntables); t++)
            for (s16 di = 0; (s16)DSW(tbl(t) + 4) > di; di++, si++) wr8(idx.seg, (u16)(idx.off + si), (u8)t);
    } else {
        dos_free(ptrs);
        ptrs.off = ptrs.seg = 0;
    }
done:
    ds_far_wr(AP_DS_shape_tbl_cache, idx);
    ds_far_wr(AP_DS_shape_ptr_cache, ptrs);
}

/* AP 7032:0000 — total number of shapes */
u16 shape_count(void)
{
    if (!far_is_null(ds_far(AP_DS_shape_ptr_cache))) return DSW(AP_DS_shape_total);
    u16 n = 0;
    for (s16 t = 0; t < (s16)DSW(AP_DS_shape_ntables); t++) n = (u16)(n + DSW(tbl(t) + 4));
    return n;
}
/* AP 7032:0039 — total number of sprites */
u16 sprite_count(void)
{
    u16 n = 0;
    for (s16 s = 0; s < (s16)DSW(AP_DS_sprite_nsets); s++) n = (u16)(n + DSW(DSW(AP_DS_sprite_sets) + 4 * s + 2));
    return n;
}

/* AP 243d:0009 — set = BMP by name (7229:01aa), count (7229:0598); returns the bitmap list */
u16 sprite_set_load(u16 set, u16 name)
{
    u16 b = bmp_load_planes(name);
    if (b) {
        DSW(DSW(AP_DS_sprite_sets) + 4 * set) = b;
        DSW(DSW(AP_DS_sprite_sets) + 4 * set + 2) = (u16)ap_bmp_count(b);
    }
    return b;
}
/* AP 243d:004c — the same through the EMS bitmap loader (1d32:214e) */
u16 sprite_set_load2(u16 set, u16 name)
{
    u16 b = bmp_load_ems(name);
    if (b) {
        DSW(DSW(AP_DS_sprite_sets) + 4 * set) = b;
        DSW(DSW(AP_DS_sprite_sets) + 4 * set + 2) = (u16)ap_bmp_count(b);
    }
    return b;
}
/* AP 243d:008f — free an EMS set (1d32:2170) */
void sprite_set_free(u16 set)
{
    bmp_free_ems(DSW(DSW(AP_DS_sprite_sets) + 4 * set));
    DSW(DSW(AP_DS_sprite_sets) + 4 * set) = 0;
    DSW(DSW(AP_DS_sprite_sets) + 4 * set + 2) = 0;
}
/* AP 243d:00cd — free a conventional set (7229:056a) */
void sprite_set_free2(u16 set)
{
    bmp_free(DSW(DSW(AP_DS_sprite_sets) + 4 * set));
    DSW(DSW(AP_DS_sprite_sets) + 4 * set) = 0;
    DSW(DSW(AP_DS_sprite_sets) + 4 * set + 2) = 0;
}
