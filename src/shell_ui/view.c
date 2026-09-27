/* shell_ui: the vehicle viewer (PS segment 0ddd, "view.c", screen 25, table 4b1e:000d, 11 items) —
 * shell_ui_career.md §2. The 3D part is shell_data's view3d module on top of the PS 3Space copy. */
#include "shell_ui/sui_a_priv.h"

#define TBL_VW      fd(0x4B1E, 0x000D)
#define VW_ITEM1_Y  0x25                 /* offset in 4b1e of item 1's y: the view's lower edge */
#define VW_PITCH_T  0x4456               /* s16: PITCH_.DAT table slot */
#define VW_NAMES    0x4458               /* near: name table (far char *[]) */
#define VW_COUNT    0x445A               /* s16 */
#define VW_TEXT     0x445C               /* far char *: description */
#define VW_NAME     0x4460               /* near char *: title (a caller's stack buffer) */
#define VW_DATE     0x4462               /* u16 */
#define VW_DIST0    0x4464               /* s16: default distance */
#define VW_HEIGHT   0x4466               /* s16 */
#define VW_DIST     0x4468               /* s16 */
#define VW_YAW      0x446A               /* u16 */
#define VW_SIDE     0x446C               /* s8 */
#define VW_SERVICE  0x446D               /* s8 */
#define VW_SEL      0x446E               /* s8 */
#define VW_LOADED   0x446F               /* s8 */
#define VW_AIRCRAFT 0x4470               /* u8 */
#define VW_GROUND   0x09A0               /* u8 */
#define VW_MODEL_ALT 0x09A2              /* s16 */
#define VW_MODEL_HDG 0x09A4              /* u16 */
#define VW_MODEL_PITCH 0x09A6            /* s16 */
#define VW_HBASE    0x09A8               /* s16 */
#define VW_YAW0     0x09AA               /* u16 */
#define VW_DMIN     0x09AC               /* s16 */
#define VW_DMAX     0x09AE               /* s16 */
#define VW_DSTEP    0x09B0               /* s16 */
#define VW_YSTEP    0x09B2               /* s16 */
#define V3D_VIEW    PS_DS_v3d_view       /* 0x52A0 near */
#define V3D_TARGET  PS_DS_v3d_target     /* 0x52A2 near: the camera */
#define V3D_MODEL   PS_DS_v3d_model      /* 0x52A4 near */
/* The PS 3Space copy's projection centre and draw flag live in its code segment (render3d, 3d50) */
#define R3D_CX      SEGS(0x3D50, 0x3E91)
#define R3D_CY      SEGS(0x3D50, 0x3E93)
#define R3D_FLAG    SEGW(0x3D50, 0x3DF8)

static s16 item1_y(void) { return fs(fd(0x4B1E, 0), VW_ITEM1_Y); }

/* PS 0ddd:0006 vw_pitch(t) */
static s16 vw_pitch(s16 t)
{
    if (DSB(VW_AIRCRAFT) == 0) return 0;
    return fs(table_rec(DSS(VW_PITCH_T), t), 0);
}

/* PS 0ddd:0039 vw_viewable(t) */
static s16 vw_viewable(s16 t) { return fs(table_rec(DSS(VW_PITCH_T), t), 4); }

/* PS 0ddd:0060 vw_variant(t) */
static s16 vw_variant(s16 t)
{
    if (DSB(VW_AIRCRAFT) == 0) return 0;
    return fs(table_rec(DSS(VW_PITCH_T), t), 2);
}

/* PS 0ddd:0094 vw_model_name(t): base name of the 3D model */
u16 vw_model_name(u16 t)
{
    u16 p = (u16)(DSW(VW_NAMES) + (s16)(s8)t * 4);
    return strpool_copy(ds_far(p)).off;
}

static void vw_render(s16 y0);

/* PS 0ddd:00b5 vw_draw_view: 3D view and title bar */
void vw_draw_view(void)
{
    vw_render(0);
    draw_title_bar_for(DSB(VW_SERVICE), DSW(VW_DATE), DSW(VW_NAME));
    fbw(cur_item(), IT_MASK, 0x40);
    hs_load(TBL_VW, 1);
    draw_bottom_bar();
    hs_load(TBL_VW, 0);
}

/* PS 0ddd:010c vw_draw_panel */
void vw_draw_panel(void)
{
    tile_fill_rect();
    ps_draw_frame();
    draw_bottom_bar();
}

/* PS 0ddd:04de vw_save_strips: title strip and bottom panel */
static void vw_save_strips(void)
{
    gfx_copy_rect(0, 0, 320, 28);
    gfx_copy_rect(0, item1_y(), 320, (s16)(200 - item1_y()));
}

static void vw_camera(u8 recompute, u8 full);

/* PS 0ddd:0120 vw_diamond_cb: zoom / rotate while the button is held */
void vw_diamond_cb(void)
{
    if (!(DSW(T_EVT) & 4)) {
        draw_diamond_up();
        return;
    }
    pages_back_to_bg();
    vw_save_strips();
    pages_front_to_back();
    draw_diamond_down();
    cursor_draw(DSW(T_PTR_X), DSW(T_PTR_Y));
    pages_back_to_front();
    vw_save_strips();
    u16 view = DSW(V3D_VIEW);
    u16 saved = DSW((u16)(view + 0x0C));
    while (DSW(T_BTN) != 0 && hs_hit(DSW(T_PTR_X), DSW(T_PTR_Y))) {
        switch (DSS(T_IDX) - 3) {
        case 0:                                                    /* zoom in */
            DSS(VW_DIST) = (s16)(DSS(VW_DIST) - DSS(VW_DSTEP));
            if (DSS(VW_DIST) <= DSS(VW_DMIN)) DSS(VW_DIST) = DSS(VW_DMIN);
            DSS(VW_HEIGHT) = (s16)(DSS(VW_HBASE) + (DSS(VW_DIST) >> 3));
            break;
        case 1: DSW(VW_YAW) = (u16)(DSW(VW_YAW) + DSW(VW_YSTEP)); break;
        case 2:                                                    /* zoom out */
            DSS(VW_DIST) = (s16)(DSS(VW_DIST) + DSS(VW_DSTEP));
            if (DSS(VW_DIST) >= DSS(VW_DMAX)) DSS(VW_DIST) = DSS(VW_DMAX);
            DSS(VW_HEIGHT) = (s16)(DSS(VW_HBASE) + (DSS(VW_DIST) >> 3));
            break;
        case 3: DSW(VW_YAW) = (u16)(DSW(VW_YAW) - DSW(VW_YSTEP)); break;
        default: break;
        }
        vw_camera(0, 0);
        pages_front_to_back();
        vw_render(0x1C);
        s16 y = RYs;
        RYs = item1_y();
        draw_bottom_bar();
        RYs = y;
        gfx_flip(1);
        menu_poll();
    }
    DSW((u16)(DSW(V3D_VIEW) + 0x0C)) = saved;
    pages_bg_to_back();
    gfx_copy_rect(0x19, 0x8C, 0x32, 0x28);
    DSW(PG_READ) = DSW(PG_BACK);
    fbw(fd(0x4B1E, 0), 0x19, 3);
}

/* PS 0ddd:0286 vw_draw_text: the description */
void vw_draw_text(void)
{
    draw_inset_box();
    ps_rect_shrink(3);
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FG) = 0xCC;
    flow_setup(0, 6, 1);
    flow_text(ds_far(VW_TEXT), 1, fnul());
}

/* PS 0ddd:02d4 vw_btn_cb: PREVIOUS / NEXT / CONTINUE */
void vw_btn_cb(void)
{
    u16 r = 1;
    if (!btn_cb()) return;
    if (DSW(T_IDX) == 8) {
        DSB(VW_SEL)--;
        fbw(fd(0x4B1E, 0), 0x19, 3);
    } else if (DSW(T_IDX) == 9) {
        DSB(VW_SEL)++;
        fbw(fd(0x4B1E, 0), 0x19, 3);
    } else if (DSW(T_IDX) == 10) {
        r = 2;
    }
    menu_set_result(r);
}

static bool vw_valid(s16 i)
{
    if (DSB(VW_AIRCRAFT) != 0 && (s8)ac_exists(i) && ac_has_service(i, DSC(VW_SERVICE)) && vw_viewable(i))
        return true;
    return DSB(VW_AIRCRAFT) == 0 && (s8)shiptxt_is_nation(i, DSC(VW_SIDE));
}

/* PS 0ddd:0323 vw_select(force): steps to the next valid type, loads its model and texts */
static void vw_select(u8 force)
{
    if (DSC(VW_LOADED) != DSC(VW_SEL)) {
        if (DSC(VW_LOADED) < DSC(VW_SEL)) {
            for (;;) {
                DSB(VW_SEL) = (u8)((s16)DSC(VW_SEL) % DSS(VW_COUNT));
                if (DSC(VW_SEL) == DSC(VW_LOADED)) break;
                if (vw_valid(DSC(VW_SEL))) break;
                DSB(VW_SEL)++;
            }
        } else {
            for (DSB(VW_SEL) = (u8)((s16)(DSS(VW_COUNT) + DSC(VW_SEL)) % DSS(VW_COUNT));
                 DSC(VW_SEL) != DSC(VW_LOADED); DSB(VW_SEL)--) {
                if (vw_valid(DSC(VW_SEL))) break;
                if (DSC(VW_SEL) == 0) DSB(VW_SEL) = (u8)DSW(VW_COUNT);
            }
        }
    }
    if (DSC(VW_LOADED) == DSC(VW_SEL) && !force) return;
    DSB(VW_LOADED) = DSB(VW_SEL);
    s16 t = DSC(VW_SEL);
    view3d_unload_model();
    view3d_load_model((u8)t, vw_variant(t));
    if (DSB(VW_AIRCRAFT) == 0) rtl_strcpy(DSW(VW_NAME), shiptxt_name(t));
    else strcpy_far_src(DSW(VW_NAME), ac_name(t));
    str_upper(DSW(VW_NAME));
    ds_far_wr(VW_TEXT, DSB(VW_AIRCRAFT) == 0 ? shiptxt_text(t) : pltxt_text(t));
}

/* PS 0ddd:051e vw_set_center(y0): the 3Space projection centre */
static void vw_set_center(s16 y0)
{
    u16 v = DSW(V3D_VIEW);
    R3D_CX = (s16)(DSS((u16)(v + 0x0A)) + (DSS((u16)(v + 0x0E)) >> 1));
    R3D_CY = (s16)((s16)(DSS((u16)(v + 0x0C)) - y0) + (DSS((u16)(v + 0x10)) >> 1));
}

/* PS 0ddd:0552 vw_draw_sky(y0): bands 7Fh, 7Eh... from the horizon up, then the ground */
static void vw_draw_sky(s16 y0)
{
    u16 m = stk_mark();
    u16 xs = stk_alloc(8), ys = stk_alloc(8);
    for (int k = 0; k < 8; k++) {
        DSB((u16)(xs + k)) = DSB((u16)(0x09B4 + k));
        DSB((u16)(ys + k)) = DSB((u16)(0x09BC + k));
    }
#define YS(i) DSS((u16)(ys + 2 * (i)))
    u8 c = 0x7F;
    if (y0 < 15) y0 = 14;
    DSB(FILL_ON) = 1;
    s16 i = 0;
    for (;;) {
        DSB(FILL_COL) = c;
        DSB(DRAW_COL) = c;
        if (!(i < 15 && y0 < YS(0))) break;
        gfx_polygon(4, xs, ys);
        YS(1) = YS(2);
        YS(0) = YS(2);
        YS(3) = (s16)(YS(3) - 2);
        if (YS(3) < y0) YS(3) = y0;
        YS(2) = YS(3);
        i++;
        c--;
    }
    if (y0 < YS(0)) {
        YS(2) = y0;
        YS(3) = y0;
        gfx_polygon(4, xs, ys);
    }
    YS(1) = 0x55;
    YS(0) = 0x55;
    YS(2) = (s16)(item1_y() - 1);
    DSB(FILL_COL) = DSB(VW_GROUND);
    DSB(DRAW_COL) = DSB(VW_GROUND);
    YS(3) = YS(2);
    gfx_polygon(4, xs, ys);
#undef YS
    stk_release(m);
}

/* PS 0ddd:064b vw_camera(recompute, full) */
static void vw_camera(u8 recompute, u8 full)
{
    if (recompute) {
        s32 d = view3d_distance();
        s32 far3 = (d * 0xC00) >> 10;
        if (DSB(VW_AIRCRAFT)) {
            s32 d0 = DSS(VW_DIST0);
            DSS(VW_DMIN) = (s16)(d0 < d ? d : d0);
            DSS(VW_DMAX) = far3 < 0x352 ? (s16)far3 : 0x352;
        } else {
            DSS(VW_DMIN) = (s16)d;
            DSS(VW_DMAX) = (s16)far3;
        }
        if (DSS(VW_DMIN) > DSS(VW_DMAX)) DSS(VW_DMIN) = DSS(VW_DMAX);
        DSS(VW_DIST) = DSS(VW_DMIN);
        DSS(VW_DSTEP) = (s16)((d * 0x32) >> 10);
        DSS(VW_MODEL_PITCH) = vw_pitch(DSC(VW_SEL));
        DSS((u16)(DSW(V3D_MODEL) + 0x0E)) = DSS(VW_MODEL_PITCH);
        DSS(VW_HEIGHT) = (s16)(DSS(VW_HBASE) + (DSS(VW_DIST) >> 3));
    }
    u16 cam = DSW(V3D_TARGET), model = DSW(V3D_MODEL);
    DSSL((u16)(cam + 0x0A)) = DSS(VW_HEIGHT);
    project(ds_ptr((u16)(model + 2)), DSW(VW_YAW), DSS(VW_DIST), (u16)(cam + 2));
    if (full) DSS((u16)(cam + 0x0E)) = pitch(ds_ptr((u16)(cam + 2)), ds_ptr((u16)(model + 2)));
    DSW((u16)(cam + 0x12)) = heading(ds_ptr((u16)(cam + 2)), ds_ptr((u16)(model + 2)));
    if (full) view3d_sort_scene();
}

/* PS 0ddd:07c1 vw_render(y0) */
static void vw_render(s16 y0)
{
    cam_begin_frame(DSW(V3D_VIEW));
    DSS((u16)(DSW(V3D_VIEW) + 0x0C)) = y0;
    vw_set_center(y0);
    DSS(CLIP_X1) = 0;
    DSS(CLIP_X2) = 319;
    DSS(CLIP_Y1) = y0;
    DSS(CLIP_Y2) = (s16)(item1_y() - 1);
    DSB(CLIP_ON) = 0;
    vw_draw_sky(y0);
    DSB(CLIP_ON) = 1;
    R3D_FLAG = 1;
    view3d_draw_scene();
    DSB(CLIP_ON) = 0;
}

/* PS 0ddd:0824 vw_pitch_load */
static void vw_pitch_load(void)
{
    if (DSB(VW_AIRCRAFT) == 0) return;
    DSS(VW_PITCH_T) = table_load(0x09CC /* "pitch_" */, 0x09D3 /* "dat" */, 6, 0x0F, 9, 1, fnul(), fnul(), fnul());
    if (DSW(VW_PITCH_T) == 0) assert_line(0x4B1E, 0x2D5);
}

/* PS 0ddd:0884 vw_pitch_free */
static void vw_pitch_free(void)
{
    if (DSB(VW_AIRCRAFT)) table_free(DSS(VW_PITCH_T));
}

/* PS 0ddd:089a vw_names_aircraft(t) */
static void vw_names_aircraft(s16 t)
{
    if (t == -1) air_open();
    pltxt_open();
    DSS(VW_COUNT) = ac_count();
    DSW(VW_NAMES) = ps_namelist_load(0x09D7 /* "pfname" */, (u16)(s16)DSC(PS_DS_modules_mask), 0x9F, 0x0F);
    vw_pitch_load();
}

/* PS 0ddd:08d7 vw_names_ships */
static void vw_names_ships(void)
{
    shiptxt_open();
    DSS(VW_COUNT) = shiptxt_count();
    DSW(VW_NAMES) = ps_namelist_load(0x09DE /* "sfname" */, (u16)(s16)DSC(PS_DS_modules_mask), 0x73, 10);
}

/* PS 0ddd:0905 vw_links(browse) */
static void vw_links(u8 browse)
{
    FarPtr t = fd(0x4B1E, 0);
    fbw(t, 0x90, browse ? 8 : 10);
    fbw(t, 0xFD, browse ? 9 : 5);
    fbw(t, 0xFB, fb(t, 0xFD));
}

/* PS 0ddd:094c vw_nav(t): one type (CONTINUE only) or browsing */
static void vw_nav(s8 t)
{
    FarPtr tb = fd(0x4B1E, 0);
    if (t < 0) {
        fww(tb, 0xE9, 0xDF);
        fbw(tb, 0xC9, 0x74);
        fbw(tb, 0xCA, 0x82);
        fbw(tb, 0xDF, 0x74);
        fbw(tb, 0xE0, 0x82);
        DSB(VW_SEL) = 0;
        DSB(VW_LOADED) = 0xFF;
        vw_links(1);
    } else {
        fww(tb, 0xE9, 0x7B);
        fbw(tb, 0xC9, 0);
        fbw(tb, 0xCA, 0);
        fbw(tb, 0xDF, 0);
        fbw(tb, 0xE0, 0);
        DSB(VW_LOADED) = (u8)t;
        DSB(VW_SEL) = (u8)t;
        vw_links(0);
    }
}

/* PS 0ddd:09cf vw_limits(navy, side) */
static void vw_limits(u8 navy, s8 side)
{
    if (DSB(VW_AIRCRAFT) == 0) {
        DSS(VW_MODEL_ALT) = 0;
        DSW(VW_MODEL_HDG) = 0x4000;
        DSS(VW_HBASE) = 0;
        DSW(VW_YAW0) = 0;
        DSS(VW_DMIN) = 4000;
        DSS(VW_DMAX) = 8000;
        return;
    }
    DSS(VW_DMAX) = 0x352;
    if (!navy) {
        DSS(VW_DMIN) = 0x15E;
        DSS(VW_DIST0) = 0x15E;
        DSS(VW_MODEL_ALT) = 0;
        DSW(VW_MODEL_HDG) = 0x4000;
        DSS(VW_HBASE) = 2;
        DSW(VW_YAW0) = 0;
    } else {
        DSS(VW_DMIN) = 0x177;
        DSS(VW_DIST0) = 0x177;
        DSS(VW_MODEL_ALT) = side == 1 ? 0x151 : 499;
        DSW(VW_MODEL_HDG) = 0x8000;
        DSS(VW_HBASE) = side == 1 ? 0x151 : 499;
        DSW(VW_YAW0) = 0x4000;
    }
}

static bool vw_navy(void) { return DSB(VW_SERVICE) == 0 || DSB(VW_SERVICE) == 3; }

/* PS 0ddd:0a6c vw_open(t, aircraft) */
static void vw_open(s16 t, u8 aircraft)
{
    DSB(VW_AIRCRAFT) = aircraft;
    if (!aircraft) {
        vw_names_ships();
        DSB(VW_SEL) = 0;
        DSB(VW_LOADED) = 0xFF;
        DSB(VW_GROUND) = 0x84;
        DSW(VW_DATE) = 0;
    } else {
        vw_names_aircraft(t);
        vw_nav((s8)t);
        DSB(VW_GROUND) = vw_navy() ? 0x84 : 0x6B;
        DSW(VW_DATE) = DSW(0x477F);
        if (t < 0) DSW(VW_DATE) = 0;
    }
    vw_limits(vw_navy(), DSC(VW_SIDE));
    DSW(VW_DIST) = DSW(VW_DMIN);
    DSW(VW_YAW) = DSW(VW_YAW0);
    ts_init(2, 1);
    set_light(0x4000, 0x4000);
    view3d_init(DSB(VW_SIDE), aircraft);
    u16 model = DSW(V3D_MODEL);
    DSW((u16)(model + 0x12)) = DSW(VW_MODEL_HDG);
    DSSL((u16)(model + 0x0A)) = DSS(VW_MODEL_ALT);
    hs_load(TBL_VW, 0);
    RHs = (s16)(item1_y() - 3);
    view3d_set_window();
    vw_select(1);
    view3d_load_scene(DSB(VW_AIRCRAFT) == 0 ? 2 : vw_navy() ? 1 : 0);
}

/* PS 0ddd:0b99 vw_close(t) */
static void vw_close(s16 t)
{
    if (DSB(VW_AIRCRAFT) == 0) {
        shiptxt_close();
    } else {
        if (t == -1) air_close();
        pltxt_close();
    }
    namelist_free(DSW(VW_NAMES));
    view3d_done();
    view3d_unload_model();
    ts_done();
    if (DSB(VW_AIRCRAFT)) view3d_unload_scene();
    vw_pitch_free();
}

/* PS 0ddd:0be5 vw_frame */
void vw_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 0ddd:0c06 vw_run(message) */
static void vw_run(u8 message)
{
    u8 first = 1;
    u16 r = 1;
    while (r != 2) {
        vw_select(0);
        vw_camera(1, first);
        first = 0;
        fbw(fd(0x4B1E, 0), 0x19, 3);
        hs_draw_all();
        hs_push();
        if (message) {
            copy_back_to_bg();
            tbox_titled3(strings_get(1, 0));
            copy_bg_to_back();
            copy_back_to_front();
        }
        hs_pop();
        r = menu_run(FNP(vw_frame));
        ui_busy();
    }
}

/* PS 0ddd:0c95 vw_screen(browse) */
static void vw_screen(u8 browse)
{
    screen_open(25, codeptr_far(PS_FN_screen_clear_black), 0x30, 1);
    hs_set_table(TBL_VW, 11);
    hs_focus(browse ? 9 : 10);
}

/* PS 0ddd:0cde view_vehicle(t, service, message): t = -1 browses every aircraft */
void view_vehicle(u16 t_, u16 service, u16 message)
{
    s16 t = (s16)t_;
    u16 m = stk_mark();
    DSW(VW_NAME) = stk_alloc(80);
    DSB(VW_SERVICE) = (u8)service;
    DSB(VW_SIDE) = (u8)service_side((u16)(s16)(s8)service);
    vw_open(t, 1);
    vw_screen(t == -1);
    vw_run((u8)message);
    vw_close(t);
    stk_release(m);
}

/* PS 0ddd:0d2f view_ship(nation) */
void view_ship(u16 nation)
{
    u16 m = stk_mark();
    DSW(VW_NAME) = stk_alloc(80);
    DSB(VW_SIDE) = (u8)nation;
    DSB(PS_DS_career_side) = (u8)nation;          /* (overwrites the career side group, as the original) */
    DSB(VW_SERVICE) = (s8)nation == 1 ? 0 : 3;
    vw_open(0, 0);
    vw_screen(1);
    vw_run(0);
    vw_close(0);
    stk_release(m);
}

__attribute__((constructor)) static void view_codeptrs(void)
{
    REG(vw_draw_view);
    REG(vw_draw_panel);
    REG(vw_diamond_cb);
    REG(vw_draw_text);
    REG(vw_btn_cb);
    REG(vw_frame);
}
