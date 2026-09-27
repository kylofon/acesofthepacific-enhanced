/* shell_data: sys\threed.c (PS 217d) and its depth sort (21d4) — the 3-D object viewer of the reference
 * screen, shell_data_sys.md §10 (re-read from the disassembly: view3d_draw_scene also clears the copy's
 * +14 attribute pointer before the shadow pass). */
#include "shell_data/sd_priv.h"
#include "render3d/render3d.h"
#include "shell_ui/shell_ui.h"
#include "video/video.h"

#define V_IS_AC   0x528E      /* u8 */
#define V_SIDE    0x528F      /* u8 */
#define V_DIST    0x5290      /* far s32[n] */
#define V_ALT     0x5294      /* s16 */
#define V_N       0x5296
#define V_OBJS    0x5298
#define V_ORDER   0x529A
#define V_BGTAB   0x529C      /* far */
#define V_VIEW    0x52A0
#define V_TARGET  0x52A2
#define V_MAIN    0x52A4
#define V_FIRST   0x52A6
#define V_LOADED  0x1B45
#define TS_DESC   0x76D2      /* render3d shape-table descriptor (near ptr) */

/* ---------------------------------------------------------------- 21d4 depth sort */

/* PS 21d4:0008 sort_by_distance(view, order, n, far dist) — selection sort, farthest first */
static void sort_by_distance(u16 view, u16 order, s16 n, FarPtr dist_fp)
{
    if (n < 2) return;
    u16 eye = (u16)(DSW(view + 2) + 2);
    for (s16 i = 0; i < n; i++)
        fwl(dist_fp, (u16)(i * 4), (u32)dist(eye, (u16)(DSW(order + i * 2) + 2)));
    for (s16 i = 0; i < n - 1; i++) {
        s16 best = i;
        s32 bv = (s32)fl(dist_fp, (u16)(i * 4));
        for (s16 j = i + 1; j < n; j++) {
            s32 d = (s32)fl(dist_fp, (u16)(j * 4));
            if (d > bv) { best = j; bv = d; }
        }
        if (best != i) {
            fwl(dist_fp, (u16)(best * 4), fl(dist_fp, (u16)(i * 4)));
            fwl(dist_fp, (u16)(i * 4), (u32)bv);
            u16 t = DSW(order + best * 2);
            DSW(order + best * 2) = DSW(order + i * 2);
            DSW(order + i * 2) = t;
        }
    }
}

/* PS 21d4:0150 cmp_priority(a, b) — shape priority, descending */
static s16 cmp_priority(u16 a_ds, u16 b_ds)
{
    s16 pb = (s16)shape_subclass((s16)DSW(DSW(b_ds)));
    s16 pa = (s16)shape_subclass((s16)DSW(DSW(a_ds)));
    return (s16)(pb - pa);
}

/* PS 21d4:017f presort(order, n) -> first index with priority 0 */
static s16 presort(u16 order, s16 n)
{
    rtl_qsort(order, (u16)n, 2, codeptr_far(PS_FN_cmp_priority));
    s16 i;
    for (i = 0; i < n; i++)
        if (shape_subclass((s16)DSW(DSW(order + i * 2))) == 0) break;
    return i;
}

/* PS 21d4:01cd view3d_presort(order, n, dist) */
static void view3d_presort(u16 order, s16 n, FarPtr dist_fp)
{
    (void)dist_fp;
    DSW(V_FIRST) = (u16)presort(order, n);
}

/* PS 21d4:01e1 view3d_sort(view, order, n, dist) */
static void view3d_sort(u16 view, u16 order, s16 n, FarPtr dist_fp)
{
    sort_by_distance(view, (u16)(order + DSW(V_FIRST) * 2), (s16)(n - (s16)DSW(V_FIRST)), dist_fp);
}

/* ---------------------------------------------------------------- 217d viewer */

/* PS 217d:000a view3d_load_model(type, alt) — "tbl" DS:1B46 */
void view3d_load_model(u8 type, s16 alt)
{
    DSW(V_ALT) = DSB(V_IS_AC) ? (u16)alt : 0;
    if (DSC(V_LOADED)) return;
    u16 name = make_name(vw_model_name((u16)(s16)(s8)type), -1, 0x1B46);
    if (ts_load_table_far(0, name)) {   /* DX:AX far table pointer */
        u16 d = DSW(TS_DESC);
        DSW(d + 6) = 0xFFFF;
        DSW(d + 8) = 0x100;
        if (DSB(V_IS_AC) && shape_nslots(0) > 1) {
            s16 k = shape_nvariants(0, 1);
            DSB(DSW(DSW(V_MAIN) + 0x14) + 1) = (u8)(k - 1);
        }
        DSB(V_LOADED) = 1;
    }
}

/* PS 217d:009f view3d_unload_model */
void view3d_unload_model(void)
{
    if (DSB(V_LOADED)) {
        ts_free_table(0);
        DSB(V_LOADED) = 0;
    }
}

/* PS 217d:00b9 view3d_set_window — current window x, y, w, h */
void view3d_set_window(void)
{
    if (!DSB(V_LOADED)) return;
    u16 v = DSW(V_VIEW);
    DSW(v + 0x0A) = DSW(0x5FDA);
    DSW(v + 0x0C) = DSW(0x5FD8);
    DSW(v + 0x0E) = DSW(0x5FD6);
    DSW(v + 0x10) = DSW(0x5FD4);
}

/* PS 217d:00e1 view3d_distance */
s16 view3d_distance(void)
{
    s32 r = (s32)shape_radius((s16)DSW(DSW(V_MAIN)));
    r = (s32)((u32)r << (DSB(DSW(V_VIEW)) & 31));          /* LXLSH */
    s16 v = (s16)(r / (s32)(s16)DSW(0x5FD6));
    return (s16)((v >> 1) + (v >> 2) + (v >> 4));
}

/* PS 217d:0130 view3d_draw_model */
void view3d_draw_model(void)
{
    if (!DSB(V_LOADED)) return;
    s16 d = view3d_distance();
    DSSL(DSW(V_TARGET) + 6) = (s32)(s16)-d;
    view3d_set_window();
    DSW(0x2318) = DSW(0x2312);
    cam_begin_frame(DSW(V_VIEW));
    ts_draw(DSW(V_MAIN));
    DSB(0x2303) = 0;
}

/* PS 217d:0170 view3d_init(side, is_aircraft) — "shades" DS:1B4A "dat" DS:1B51 */
void view3d_init(u8 side, u8 is_aircraft)
{
    DSB(V_IS_AC) = is_aircraft;
    DSW(0x3B55) = 4;
    DSB(V_SIDE) = side;
    DSW(V_MAIN) = xmalloc(0x16);
    memzero(DSW(V_MAIN), 0x16);
    DSW(DSW(V_MAIN) + 0x14) = DSB(V_IS_AC) ? 0x1B38 : 0x1B3D;
    DSW(V_TARGET) = xmalloc(0x16);
    memzero(DSW(V_TARGET), 0x16);
    DSW(V_VIEW) = view_new((s16)DSW(V_TARGET));
    DSW(DSW(V_VIEW)) = 7;
    DSW(DSW(V_VIEW) + 0x12) = xmalloc(0x100);
    file_read_n(DSW(DSW(V_VIEW) + 0x12), 1, 0x100, make_name(0x1B4A, -1, 0x1B51));
}

/* PS 217d:0227 view3d_done */
void view3d_done(void)
{
    xfree(DSW(DSW(V_VIEW) + 0x12));
    view_free(DSW(V_VIEW));
    xfree(DSW(V_TARGET));
    xfree(DSW(V_MAIN));
}

/* PS 217d:0257 view3d_load_scene(scene) — "sea" DS:1B55 "land" DS:1B59, "a_ship" DS:1B5E "j_ship"
 * DS:1B65 "land" DS:1B6C, "tbl" DS:1B71, "wld" DS:1B75 "rb" DS:1B79 */
void view3d_load_scene(s8 scene)
{
    shadow_set_colour(scene ? 0x28 : 2);
    DSB(0x1B3F) = DSB(V_SIDE);
    u16 world = scene ? 0x1B55 : 0x1B59;
    u16 shapes = scene ? (DSB(V_SIDE) == 1 ? 0x1B5E : 0x1B65) : 0x1B6C;
    if (scene == 2) {
        DSW(V_OBJS) = 0;
        DSW(V_ORDER) = 0;
        ds_far_wr(V_DIST, far_null());
        DSW(V_N) = 0;
        return;
    }
    u32 t = ts_load_table_far(1, make_name(shapes, -1, 0x1B71));   /* DX:AX (the chunk offset is often 0) */
    ds_far_wr(V_BGTAB, far_make((u16)(t >> 16), (u16)t));
    if (!t) sd_fatal(FS_THREED, 0xE7);
    u16 d = DSW(TS_DESC);
    DSW(d + 0x12) = 0xFFFF;
    DSW(d + 0x14) = 0x100;
    u16 f = my_fopen(make_name(world, -1, 0x1B75), 0x1B79);
    if (!f) sd_fatal(FS_THREED, 0xED);
    s16 n = (s16)((s32)my_filelength(f) / 20);
    DSW(V_N) = (u16)n;
    DSW(V_ORDER) = xmalloc((u16)(n * 2));
    u16 obj = DSW(V_OBJS) = xmalloc((u16)(n * 0x16));
    u16 m = stk_mark();
    u16 rec = stk_alloc(20);
    for (s16 i = 0; i < n; i++, obj += 0x16) {
        my_fread(rec, 20, 1, f);
        memzero(obj, 0x16);
        DSW(obj) = (u16)(DSW(rec) + 1);
        DSW(obj + 0x14) = 0x1B3E;
        for (u16 k = 0; k < 6; k++) DSB(obj + 0x0E + k) = DSB(rec + 2 + k);
        for (u16 k = 0; k < 12; k++) DSB(obj + 2 + k) = DSB(rec + 8 + k);
        DSW(DSW(V_ORDER) + i * 2) = obj;
    }
    stk_release(m);
    my_fclose(f);
    ds_far_wr(V_DIST, xfarmalloc((u32)(u16)(DSW(V_N) << 2)));
    view3d_presort(DSW(V_ORDER), (s16)DSW(V_N), ds_far(V_DIST));
}

/* PS 217d:045d view3d_sort_scene */
void view3d_sort_scene(void)
{
    view3d_sort(DSW(V_VIEW), DSW(V_ORDER), (s16)DSW(V_N), ds_far(V_DIST));
}

/* PS 217d:047e view3d_unload_scene */
void view3d_unload_scene(void)
{
    xfarfree(ds_far(V_DIST));
    xfree(DSW(V_OBJS));
    xfree(DSW(V_ORDER));
    ts_free_table(1);
}

/* PS 217d:04b0 view3d_draw_scene */
void view3d_draw_scene(void)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x16);
    u16 mo = DSW(V_MAIN);
    for (u16 k = 0; k < 0x16; k++) DSB(tmp + k) = DSB(mo + k);
    for (s16 i = 0; i < (s16)DSW(V_N); i++) ts_draw(DSW(DSW(V_ORDER) + i * 2));
    s16 di = 0;
    if (DSB(V_IS_AC)) {
        mo = DSW(V_MAIN);
        di = (s16)DSW(mo + 0x0A);
        DSSL(mo + 0x0A) = (s32)(s16)DSW(V_ALT);
        DSW(tmp + 0x14) = 0;
        if (shadow_pos((u16)(DSW(V_MAIN) + 2), (u16)(tmp + 2))) {
            DSSL(tmp + 0x0A) += di;
            DSW(0x3B53) = 1;
            ts_draw(tmp);
            DSW(0x3B53) = 0;
        }
        mo = DSW(V_MAIN);
        DSSL(mo + 0x0A) += di;
    }
    DSW(DSW(V_MAIN)) = 0;
    ts_draw(DSW(V_MAIN));
    if (DSB(V_IS_AC)) DSSL(DSW(V_MAIN) + 0x0A) = (s32)di;
    DSB(0x2303) = 0;
    stk_release(m);
}

__attribute__((constructor)) static void threed_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_cmp_priority, (CodeFn)cmp_priority);
}
