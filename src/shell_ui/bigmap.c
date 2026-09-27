/* shell_ui part B: bigmap.c (PS 0d16) — "THE PACIFIC THEATER" war map (shell_ui_flight.md §4).
 * Screen 13 BIGMAP, hot-spot table 4b12:000f (8 items). */
#include "shell_ui/sui_b_priv.h"

#define BM_SEG      0x4b12
#define BM_TBL      BM_SEG, 0x000f
#define BM_X0       0x0958      /* x of frame 0 / frame 1 (y is always 20) */
#define BM_X1       0x095A
#define BM_BOXGONE  0x095C      /* u8 station box dismissed */
#define BM_LASTVIEW 0x095D      /* u8 view at the last label pass */
#define BM_LABELS   0x4418      /* near array of 18-byte label records */
#define BM_BMP      0x441A      /* BIGMAP.BMP frame table */
#define BM_POS      0x441C      /* station position: long x (441C), long z (4420) */
#define BM_CY       0x4428
#define BM_CX       0x442A
#define BM_AREA     0x442C
#define BM_NLABELS  0x442E
#define BM_CARRIER  0x4430
#define BM_VIEW     0x4431      /* u8: 2 = left part, 4 = right part */
#define BM_SCROLL   0x4432      /* u8: 1 initial, 2 left, 4 right, 0 none */
#define BM_STNAME   0x4433
#define BM_TEXT     0x4451      /* text blob */
#define BM_SHOWN    0x4453      /* u8 per label */

/* PS 0d16:000f bm_to_screen(cx, cy, w, h, long x, long z, int *sx, int *sy) */
static void bm_to_screen(s16 cx, s16 cy, s16 w, s16 h, s32 x, s32 z, u16 sx_ds, u16 sy_ds)
{
    s16 hw = (s16)(w >> 1);
    s16 bx = (s16)(cx + hw);
    s16 hh = (s16)(h >> 1);
    s16 by = (s16)(cy + hh);
    DSW(sx_ds) = (u16)(bx + (s16)(lxmul(hw, x) / 12672000));
    DSW(sy_ds) = (u16)(by - (s16)(lxmul(hh, z) / 12672000));
    DSW(sx_ds) = (u16)(DSW(sx_ds) + DSW(BM_X0));
    DSW(sy_ds) = (u16)(DSW(sy_ds) + 0x14);
}

static void bm_station_screen(u16 sx_ds, u16 sy_ds)
{
    bm_to_screen((s16)DSW(BM_CX), (s16)DSW(BM_CY), 0x40, 0x35, (s32)DSL(BM_POS), (s32)DSL(BM_POS + 4),
                 sx_ds, sy_ds);
}

static u16 bm_rec(s16 i) { return (u16)(DSW(BM_LABELS) + i * 0x12); }

/* PS 0d16:009c bm_load_labels(name) */
static void bm_load_labels(u16 name_ds)
{
    u16 f = my_fopen(name_ds, 0x095E /* "rb" */);
    if (f == 0) ufatal(BM_SEG, 0xBA);
    u16 mk = stk_mark();
    u16 len = stk_alloc(2);
    my_fread(BM_NLABELS, 2, 1, f);
    DSW(BM_LABELS) = xmalloc((u16)((s16)DSW(BM_NLABELS) * 0x12));
    my_fread(DSW(BM_LABELS), 0x12, (s16)DSW(BM_NLABELS), f);
    my_fread(len, 2, 1, f);
    DSW(BM_TEXT) = xmalloc(DSW(len));
    my_fread(DSW(BM_TEXT), 1, (s16)DSW(len), f);
    stk_release(mk);
    my_fclose(f);
    gfx_font_select((s16)DSW(F_FONT3));
    for (s16 i = 0; i < (s16)DSW(BM_NLABELS); i++) {
        u16 r = bm_rec(i);
        DSW(r + 8) = (u16)(DSW(BM_TEXT) + DSW(r + 8));
        s16 tw = gfx_text_width(DSW(bm_rec(i) + 8));
        u16 sz = (u16)gfx_rect_size((u16)tw, 10);            /* AX only: the high word is 0 */
        FarPtr b = xfarmalloc((u32)sz);
        far_wr(cur_dgroup, (u16)(bm_rec(i) + 0x0E), b);
    }
}

/* PS 0d16:01d7 bm_draw_map — item 3 */
static void bm_draw_map(void)
{
    u16 mk = stk_mark();
    u16 sx = stk_alloc(2), sy = stk_alloc(2);
    DSB(CLIP_ON) = 1;
    DSW(0x2304) = 0x11; DSW(0x2308) = 0x14; DSW(0x2306) = 0x131; DSW(0x230A) = 0xBD;
    if (DSB(BM_SCROLL) == 4 && DSB(BM_VIEW) == 2) {              /* scroll to the right part */
        s16 i;
        for (i = 0; (s16)(frame_w(bmp_frame(DSW(BM_BMP), 1)) + 0x1E) >= i; i = (s16)(i + 10)) {
            pages_front_to_back();
            page_copy_full();
            gfx_sprite_opaque(bmp_frame(DSW(BM_BMP), 0), (s16)(DSW(BM_X0) - i), 0x14);
            gfx_sprite_opaque(bmp_frame(DSW(BM_BMP), 1), (s16)(DSW(BM_X1) - i), 0x14);
            gfx_flip(1);
        }
        u16 d = (u16)(i - 10);
        DSW(BM_X0) = (u16)(DSW(BM_X0) - d);
        DSW(BM_X1) = (u16)(DSW(BM_X1) - d);
        DSB(BM_SCROLL) = 0;
        DSB(BM_VIEW) = 4;
        bm_station_screen(sx, sy);
        if (DSB(BM_BOXGONE) == 0 && (s16)DSW(sx) < 0x11) DSB(BM_BOXGONE) = 1;   /* station scrolled out */
        hs_draw_all();
    } else if (DSB(BM_SCROLL) == 2 && DSB(BM_VIEW) == 4) {       /* scroll back left */
        s16 i;
        for (i = 0; (s16)(frame_w(bmp_frame(DSW(BM_BMP), 1)) + 0x1E) >= i; i = (s16)(i + 10)) {
            pages_front_to_back();
            page_copy_full();
            gfx_sprite_opaque(bmp_frame(DSW(BM_BMP), 0), (s16)(DSW(BM_X0) + i), 0x14);
            gfx_sprite_opaque(bmp_frame(DSW(BM_BMP), 1), (s16)(DSW(BM_X1) + i), 0x14);
            gfx_flip(1);
        }
        u16 d = (u16)(i - 10);
        DSW(BM_X0) = (u16)(DSW(BM_X0) + d);
        DSW(BM_X1) = (u16)(DSW(BM_X1) + d);
        DSB(BM_SCROLL) = 0;
        DSB(BM_VIEW) = 2;
        bm_station_screen(sx, sy);
        if (DSB(BM_BOXGONE) == 0 && (s16)DSW(sx) > 0x131) DSB(BM_BOXGONE) = 1;
        hs_draw_all();
    } else if (DSB(BM_SCROLL) == 1) {                            /* first draw: open on the station's half */
        bm_station_screen(sx, sy);
        if ((s16)DSW(sx) > 0x131) {
            s16 s = (s16)(frame_w(bmp_frame(DSW(BM_BMP), 1)) + 0x1E);
            s16 d = (s16)(s - s % 10);
            DSB(BM_VIEW) = 4;
            DSW(BM_X0) = (u16)(DSW(BM_X0) - (u16)d);
            DSW(BM_X1) = (u16)(DSW(BM_X1) - (u16)d);
        } else {
            DSB(BM_VIEW) = 2;
        }
        DSB(BM_SCROLL) = 0;
    }
    gfx_sprite_opaque(bmp_frame(DSW(BM_BMP), 0), (s16)DSW(BM_X0), 0x14);
    gfx_sprite_opaque(bmp_frame(DSW(BM_BMP), 1), (s16)DSW(BM_X1), 0x14);
    ps_clip_full();
    stk_release(mk);
}

/* PS 0d16:0445 bm_btn_cb */
static void bm_btn_cb(void)
{
    if (btn_small_cb(0)) {
        DSW(HS_RES) = DSW(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0d16:0467 bm_draw_title */
static void bm_draw_title(void)
{
    u16 s = str_get(0);
    gfx_font_select((s16)DSW(F_FONT2));
    DSB(TXT_FLG) = 1;
    ps_text_emboss(s, DSW(RX), DSW(RY), 0xC1, 0xC0, 0xB1);
}

/* PS 0d16:04ab bm_draw_station — item 7 */
static void bm_draw_station(void)
{
    u8 mirror = 0;
    bm_station_screen(RX, RY);
    gfx_font_select((s16)DSW(F_FONT3));
    s16 x = DSS(RX), y = DSS(RY);
    if (!(x > 0x11 && x < 0x131 && y > 0x14 && y < 0xBD)) return;
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), (s16)(s8)DSB(0x4775) + 16), x, y, 0);
    s16 bx, ax, by, ay, arrow;
    if (DSS(RX) > 0xA1) {
        bx = (s16)(DSS(RX) - DSS(RW) + 10);
        ax = (s16)(bx + DSS(RW) - 15);
        mirror = 1;
    } else {
        bx = (s16)(DSS(RX) - 10);
        ax = (s16)(bx + 11);
    }
    if (DSS(RY) > 0x68) {
        by = (s16)(DSS(RY) - DSS(RH) - 18);
        ay = (s16)(by + DSS(RH) - 1);
        arrow = 5;
        mirror = (u8)(mirror == 0);
    } else {
        by = (s16)(DSS(RY) + 22);
        ay = (s16)(DSS(RY) + 7);
        arrow = 4;
    }
    DSS(RX) = bx > 0x11 ? bx : 0x11;
    if ((s16)(DSS(RX) + DSS(RW)) > 0x131) DSS(RX) = (s16)(0x131 - DSS(RW));
    DSS(RY) = by;
    if (DSB(BM_BOXGONE)) return;
    tile_fill_rect();
    ps_draw_frame();
    gfx_sprite_flip(bmp_frame(DSW(BM_BMP), arrow), ax, ay, mirror ? 2 : 0);
    u16 s2 = str_get(DSB(BM_CARRIER) ? 2 : 3);             /* evaluated first (pushed last) */
    u16 s1 = str_get(1);
    rtl_sprintf(STRBUF, 0x0961 /* "%s%s" */, s1, s2);
    text_engrave(STRBUF, 0xC1, 0xB1, 0xC0, (u16)(DSS(RX) + 7), (u16)(DSS(RY) + 3));
    rtl_sprintf(STRBUF, 0x0966 /* "%s." */, BM_STNAME);
    str_upper(STRBUF);
    text_engrave(STRBUF, 0xC1, 0xB1, 0xC0, (u16)(DSS(RX) + 7), (u16)(DSS(RY) + 13));
}

/* PS 0d16:06c1 bm_hover_labels */
static void bm_hover_labels(void)
{
    s16 S = (s16)(frame_w(bmp_frame(DSW(BM_BMP), 1)) + 0x19);
    gfx_font_select((s16)DSW(F_FONT3));
    for (s16 i = 0; i < (s16)DSW(BM_NLABELS); i++) {
        s16 tw = gfx_text_width(DSW(bm_rec(i) + 8));
        u16 r = bm_rec(i);
        s16 x0, y0, x1, y1, tx, ty;
        if (DSB(BM_VIEW) == 2) {
            x0 = DSS(r); y0 = DSS(r + 2); x1 = DSS(r + 4); y1 = DSS(r + 6); tx = DSS(r + 0x0A);
        } else {
            x0 = (s16)(DSS(r) - S); y0 = DSS(r + 2); x1 = (s16)(DSS(r + 4) - S); y1 = DSS(r + 6);
            tx = (s16)(DSS(r + 0x0A) - S);
        }
        ty = DSS(r + 0x0C);
        s16 sx = (s16)(tx + 0x11), sy = (s16)(ty + 0x14);
        FarPtr buf = far_rd(cur_dgroup, (u16)(r + 0x0E));
        if (DSB(BM_LASTVIEW) != DSB(BM_VIEW) && sx > 0x11 && sx < 0x131 && sy > 0x14 && sy < 0xBD) {
            DSW(PG_SRC) = DSW(PG_WORK);
            gfx_get_rect(buf, sx, sy, tw, 10);
        }
        s16 mx = (s16)DSW(PTR_X), my = (s16)DSW(PTR_Y);
        u16 shown = (u16)(DSW(BM_SHOWN) + i);
        if ((s16)(x0 + 0x11) < mx && (s16)(x1 + 0x11) > mx && (s16)(y0 + 0x14) < my && (s16)(y1 + 0x14) > my) {
            if (DSB(shown) != 1) {
                pages_back_to_front();
                DSB(TXT_COL) = 0xA4;
                gfx_text(DSW(bm_rec(i) + 8), sx, sy);
                pages_front_to_back();
                gfx_text(DSW(bm_rec(i) + 8), sx, sy);
                DSB(shown) = 1;
            }
        } else if (DSB(shown) == 1) {
            pages_back_to_front();
            gfx_put_rect(far_rd(cur_dgroup, (u16)(bm_rec(i) + 0x0E)), sx, sy, tw, 10);
            pages_front_to_back();
            gfx_put_rect(far_rd(cur_dgroup, (u16)(bm_rec(i) + 0x0E)), sx, sy, tw, 10);
            DSB(shown) = 0xFF;
        }
    }
    DSB(BM_LASTVIEW) = DSB(BM_VIEW);
}

/* PS 0d16:0901 bm_frame */
static void bm_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    s16 mx = (s16)DSW(PTR_X), my = (s16)DSW(PTR_Y);
    if (mx > 0x11 && mx < 0x131 && my > 0x14 && my < 0xBD) {
        if (DSB(BM_BOXGONE) == 0) {                   /* first time: redraw the map without the box */
            DSB(BM_BOXGONE) = 1;
            set_mask(hs_item(BM_TBL, 3), 3);
            set_mask(hs_item(BM_TBL, 7), 3);
        } else {
            set_mask(hs_item(BM_TBL, 3), 0x40);
            set_mask(hs_item(BM_TBL, 7), 0x40);
            bm_hover_labels();
        }
    }
    hs_present();
    if (menu_get_result() == 1) {
        if (DSW(HS_RES) == 4) DSB(BM_SCROLL) = 2;
        else if (DSW(HS_RES) == 5) DSB(BM_SCROLL) = 4;
    } else if (menu_get_result() == 2) {
        DSW(HS_RES) = 6;
    }
}

/* PS 0d16:09d8 bm_station */
static void bm_station(void)
{
    u16 mk = stk_mark();
    u16 area = stk_alloc(2);
    u16 file = stk_alloc(0x12);
    station_get((s16)DSW(0x478C), area, BM_AREA);
    DSB(BM_CARRIER) = station_is_carrier((s16)DSW(0x478C));
    rtl_strcpy(file, DSB(BM_CARRIER) ? 0x096E /* "CARRIER" */ : 0x0976 /* "TARGET" */);
    rtl_itoa((s16)DSW(BM_AREA), LBLBUF, 10);
    wd_load_database(make_name(file, -1, LBLBUF));          /* CARRIER.<n> / TARGET.<n> */
    wd_position((s16)DSW(area), DSW(0x477B), BM_POS, 1);
    wd_close();
    stattxt_open();
    strcpy_far_src(BM_STNAME, station_name((s16)DSW(0x478C)));
    stattxt_close();
    s16 s = table_load(0x097D /* "wldlocs" */, 0x0985 /* "dat" */, 4, 3, 0x11, 1, fnull(), fnull(), fnull());
    FarPtr rec = table_rec(s, (s16)DSW(BM_AREA));
    if (far_is_null(rec)) ufatal(BM_SEG, 0x2BA);
    DSW(BM_CX) = uw(rec, 0);
    DSW(BM_CY) = uw(rec, 2);
    table_free(s);
    stk_release(mk);
}

/* PS 0d16:0b03 bm_free_label_buffers */
static void bm_free_label_buffers(void)
{
    for (s16 i = 0; i < (s16)DSW(BM_NLABELS); i++) xfarfree(far_rd(cur_dgroup, (u16)(bm_rec(i) + 0x0E)));
}

/* PS 0d16:0b33 bigmap_show — flight deck VIEW THE WAR MAP, career events */
void bigmap_show(void)
{
    bm_load_labels(make_name(0x0989 /* "bigmap" */, -1, 0x0990 /* "dat" */));
    DSW(BM_BMP) = bmp_load_planes(make_name(0x0994 /* "bigmap" */, -1, 0x099B /* "bmp" */));
    if (DSW(BM_BMP) == 0) ufatal(BM_SEG, 0x2DA);
    /* the shown flags are xmalloc'ed uninitialised: a stray 1 skips a label's first highlight (L) */
    DSW(BM_SHOWN) = xmalloc(DSW(BM_NLABELS));
    DSW(BM_X0) = 0x11;
    DSW(BM_X1) = (u16)(frame_w(bmp_frame(DSW(BM_BMP), 0)) + 0x11);
    DSB(BM_SCROLL) = 1;
    DSB(BM_VIEW) = 2;
    DSB(BM_BOXGONE) = 0;
    DSB(BM_LASTVIEW) = 0;
    bm_station();
    screen_open(0x0D, codeptr_far(PS_FN_screen_clear_black), 0x4E, 1);
    DSW(HS_RES) = 6;
    do {
        hs_set_table(hs_item(BM_TBL, 0), 8);
        hs_focus(DSW(HS_RES));
        hs_draw_all();
        menu_run(UFP(bm_frame));
    } while (DSW(HS_RES) != 6);
    xfree(DSW(BM_LABELS));
    xfree(DSW(BM_SHOWN));
    xfree(DSW(BM_TEXT));
    bm_free_label_buffers();
    bmp_free(DSW(BM_BMP));
}

/* PS 0d16:0c5d bm_init — dialogs_init */
void bm_init(void)
{
    hs_default_links(hs_item(BM_TBL, 0), 8);
}

__attribute__((constructor)) static void bigmap_codeptrs(void)
{
    UREG(bm_draw_map);
    UREG(bm_btn_cb);
    UREG(bm_draw_title);
    UREG(bm_draw_station);
    UREG(bm_frame);
}
