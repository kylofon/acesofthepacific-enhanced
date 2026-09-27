/* shell_ui part B: fltmap.c — the flight map (PS 06c7, shell_ui_flight.md §1; screen 27 FLTMAP,
 * hot-spot table 49db:000f, 5 items) and its data (PS 12c4, shell_ui_mission.md §5). */
#include "shell_ui/sui_b_priv.h"

#define FM_SEG      0x49db
#define FM_TBL      FM_SEG, 0x000f
#define FM_WPS      0x4270      /* near: waypoint array */
#define FM_BMP      0x4272      /* FLTMAP.BMP frame table */
#define FM_AREA     0x4274      /* u8 */
#define FM_AREANAME 0x4275      /* near char * */
#define FM_S_ALT    0x4277      /* "Altitude" */
#define FM_S_BRG    0x4279      /* "Bearing" */
#define FM_S_DIST   0x427B      /* "Distance" */
#define FM_ROUTE    0x05B6      /* u8: route is drawn */
#define FM_ABORT    0x05B7      /* u8: Esc during the route */
#define FM_POPBUF   0x05B8      /* far save buffer of the popup */
#define FM_POPSIZE  0x05BC

/* PS 06c7:0000 fm_poll_skip -> u8 */
static u8 fm_poll_skip(void)
{
    u8 skip;
    u16 k = key_read();
    DSB(0x6017) = (u8)(k & 0xFF);
    DSB(0x6016) = (u8)((s16)k >> 8);
    if (menu_hotkey(DSB(0x6016)))
        return 0;           /* PORT: the original returns an uninitialised local here */
    skip = (DSB(0x6016) == 0x1C || DSB(0x6016) == 0x01 || DSB(0x6016) == 0x39) ? 1 : 0;
    if (DSB(0x6016) == 0x01) DSB(FM_ABORT) = 1;
    if (DSB(0x43B6)) skip = (skip || joy_button(0) || joy_button(1)) ? 1 : 0;
    if (DSB(0x43B7)) skip = (skip || mouse_button_dos3(0) || mouse_button_dos3(1)) ? 1 : 0;
    return skip;
}

/* PS 06c7:00c5 fm_draw_area_name */
static void fm_draw_area_name(void)
{
    draw_inset_box2();
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    DSB(TXT_COL) = 0xCC;
    gfx_text(DSW(FM_AREANAME), (s16)(DSS(RX) + 3), (s16)(DSS(RY) + 3));
}

/* PS 06c7:00fd fm_btn_cb */
static void ps_fm_btn_cb_06c7(void)
{
    if (btn_small_cb(0)) menu_set_result(DSW(CUR_IDX) == 3 ? 1 : 2);
}

/* PS 06c7:0125 fm_bearing(wp *a, wp *b) -> degrees */
u16 fm_bearing(u16 a_ds, u16 b_ds)
{
    u32 dz = DSL(b_ds + 4) - DSL(a_ds + 4);
    u32 dx = DSL(b_ds) - DSL(a_ds);
    s16 ang = (s16)atan2_32((u16)dx, (u16)(dx >> 16), (u16)dz, (u16)(dz >> 16));
    u32 t = (u32)((s32)ang - 0x4000);
    t = 0x10000u - t;                                   /* 1:0000h - t (mov ax,1; xor dx,dx; sub; sbb) */
    u32 p = t * 360u;                                   /* LXMUL */
    return (u16)((u32)(u16)(p >> 16) % 360u);            /* high word, LUMOD 360 */
}

/* PS 06c7:019e fm_miles(wp *a, wp *b) */
static u16 fm_miles(u16 a_ds, u16 b_ds)
{
    return (u16)(ldist2(a_ds, b_ds) / 21128u);
}

/* PS 06c7:01d2 fm_feet(long alt) */
static s32 fm_feet(s32 alt) { return alt / 4; }

/* PS 06c7:01eb fm_to_screen(int *sx, int *sy, long x, long z) */
static void fm_to_screen(u16 sx_ds, u16 sy_ds, s32 x, s32 z)
{
    DSW(sx_ds) = (u16)(s16)(lxmul(x, 0x93) / 25344000);
    DSW(sy_ds) = (u16)(s16)(lxmul(z, 0x93) / 25344000);
    DSW(sy_ds) = (u16)(-(s16)DSW(sy_ds));
    DSW(sx_ds) = (u16)(DSW(sx_ds) + 0x1A);
    DSW(sy_ds) = (u16)(DSW(sy_ds) + 0x10);
    DSW(sx_ds) = (u16)(DSW(sx_ds) + 0x49);
    DSW(sy_ds) = (u16)(DSW(sy_ds) + 0x49);
}

/* PS 06c7:0251 fm_heading_frame(u8 heading) -> 0..7 */
static u16 fm_heading_frame(u8 heading) { return (u16)((u8)(heading + 0x10) >> 5); }

/* PS 06c7:026b fm_frame */
static void ps_fm_frame_06c7(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
}

/* PS 06c7:028c fm_draw_flight(rec *f) */
static void fm_draw_flight(u16 f)
{
    u16 mk = stk_mark();
    u16 sx = stk_alloc(2), sy = stk_alloc(2);
    u8 hf = 0;
    fm_to_screen(sx, sy, (s32)DSL(f + 2), (s32)DSL(f + 6));
    s8 c = (s8)DSB(f);
    if (c >= 0 && c <= 2) {
        hf = (u8)fm_heading_frame(DSB(f + 1));
        DSW(sx) = (u16)(DSW(sx) - 4);
        DSW(sy) = (u16)(DSW(sy) - 4);
    } else if (c >= 5) {
        DSW(sx) = (u16)(DSW(sx) - 4);
        DSW(sy) = (u16)(DSW(sy) - 2);
    } else {
        DSW(sx) = (u16)(DSW(sx) - 8);
        DSW(sy) = (u16)(DSW(sy) - 3);
    }
    u16 frame;
    if (c >= 0 && c <= 2) frame = bmp_frame(DSW(FM_BMP), (s16)c * 8 + (s8)hf);
    else frame = bmp_frame(DSW(FM_BMP), (s16)c + 0x15);
    gfx_sprite_flip(frame, (s16)DSW(sx), (s16)DSW(sy), 0);
    stk_release(mk);
}

/* PS 06c7:033a fm_draw_flights(rec *f, n) — last to first */
static void fm_draw_flights(u16 f, u16 n)
{
    DSW(FM_BMP) = bmp_load_planes(make_name(0x05C7 /* "fltmap" */, -1, 0x05CE /* "bmp" */));
    u16 i = (u16)(n - 1);
    u16 old;
    do {
        fm_draw_flight((u16)(f + (u16)(i << 4)));
        old = i;
        i--;
    } while (old != 0);     /* n = 0 would loop 65536 times: never happens (the player's flight) */
}

/* PS 06c7:037b fm_popup_measure(far text, int *h, has_nav, has_alt) */
static void fm_popup_measure(FarPtr text, u16 h_ds, u8 has_nav, u8 has_alt)
{
    flow_setup(0, 7, 0);
    DSW(RW) = (u16)(DSW(RW) - 10);
    DSW(h_ds) = far_is_null(text) ? 0 : (u16)(s16)(s8)flow_text(text, 1, fnull());
    if (!has_nav && !has_alt) {
        DSW(h_ds) = (u16)(DSW(h_ds) + 10);
    } else {
        if (has_nav) DSW(h_ds) = (u16)(DSW(h_ds) + 0x17);
        if (has_alt) DSW(h_ds) = (u16)(DSW(h_ds) + 9);
    }
    DSW(RW) = (u16)(DSW(RW) + 10);
}

/* PS 06c7:03ec fm_popup(far text, bearing, miles, long feet) */
static void fm_popup(FarPtr text, u16 bearing, s16 miles, s32 feet)
{
    s16 yl = 5;
    DSW(RX) = 0xB4;
    DSW(RY) = 0x62;
    DSW(RW) = 0x78;
    fm_popup_measure(text, RH, miles > 0, feet > 0);
    if ((s16)(DSS(RY) + DSS(RH)) > 0xC5) DSS(RY) = (s16)(0xC5 - DSS(RH));
    u16 size = (u16)gfx_rect_size((u16)(DSW(RW) + 1), (u16)(DSW(RH) + 1));
    if (size > DSW(FM_POPSIZE)) {
        if (DSW(FM_POPSIZE) != 0) xfarfree(ds_far(FM_POPBUF));
        DSW(FM_POPSIZE) = size;
        ds_far_wr(FM_POPBUF, xfarmalloc((u32)size));
    }
    DSW(PG_DST) = DSW(PG_WORK);
    DSW(PG_SRC) = DSW(PG_WORK);
    gfx_get_rect(ds_far(FM_POPBUF), DSS(RX), DSS(RY), (s16)(DSW(RW) + 1), (s16)(DSW(RH) + 1));
    draw_popup_box();
    if (!far_is_null(text)) {
        DSW(RX) = (u16)(DSW(RX) + 5);
        DSW(RW) = (u16)(DSW(RW) - 10);
        DSW(RY) = (u16)(DSW(RY) + 5);
        DSB(TXT_COL) = 0xC5;
        flow_setup(1, 7, 1);
        yl = (s16)(yl + (s16)(s8)flow_text(text, 1, fnull()));
        DSW(RX) = (u16)(DSW(RX) - 5);
        DSW(RW) = (u16)(DSW(RW) + 10);
        DSW(RY) = (u16)(DSW(RY) - 5);
    }
    if (miles != 0) {
        u16 mk = stk_mark();
        u16 t = stk_alloc(0x14);          /* bp-6Ch */
        u16 s = stk_alloc(0x50);          /* bp-58h */
        s16 x = (s16)(DSS(RX) + 5);
        s16 y = (s16)(DSS(RY) + yl);
        if (feet != 0) {
            s16 th = (s16)(feet / 1000);
            if (th) rtl_sprintf(t, 0x05D2 /* "%d," */, (unsigned)(u16)th);
            else DSB(t) = 0;
            s16 rem = (s16)(feet % 1000);
            rtl_sprintf(s, 0x05D6 /* "%s: %s%03d feet" */, (unsigned)DSW(FM_S_ALT), (unsigned)t, (unsigned)(u16)rem);
            text_engrave(s, 0xC7, 0xB3, 0xC0, (u16)x, (u16)y);
            y = (s16)(y + 7);
        }
        rtl_sprintf(s, 0x05E6 /* "%s: %03d`" */, (unsigned)DSW(FM_S_BRG), (unsigned)bearing);
        text_engrave(s, 0xC7, 0xB3, 0xC0, (u16)x, (u16)y);
        y = (s16)(y + 7);
        rtl_sprintf(s, 0x05F0 /* "%s: %d mile%s" */, (unsigned)DSW(FM_S_DIST), (unsigned)(u16)miles,
                    (unsigned)(miles == 1 ? 0x05FE /* "" */ : 0x05FF /* "s" */));
        text_engrave(s, 0xC7, 0xB3, 0xC0, (u16)x, (u16)y);
        stk_release(mk);
    }
    copy_back_to_front();
    DSW(COUNTDOWN) = 0x58C;                     /* up to 1420 ticks = 6.0 s */
    while (DSW(COUNTDOWN) != 0 && !fm_poll_skip()) host_pump();   /* PORT: busy waits pump the host */
    while (fm_poll_skip()) host_pump();
    pages_front_to_back();
    gfx_put_rect(ds_far(FM_POPBUF), DSS(RX), DSS(RY), (s16)(DSW(RW) + 1), (s16)(DSW(RH) + 1));
    copy_back_to_front();
}

static void fm_leg_pixel(s16 x, s16 y, u8 *skip)
{
    DSW(COUNTDOWN) = 0x0F;                      /* 15 PIT ticks per pixel */
    pages_front_to_back();
    gfx_put_pixel(x, y, 0xA4);
    pages_back_to_front();
    gfx_put_pixel(x, y, 0xA4);
    *skip = *skip ? 1 : fm_poll_skip();
    if (!*skip) while (DSW(COUNTDOWN) != 0) host_pump();
}

/* PS 06c7:068f fm_draw_leg(x0, y0, x1, y1) */
static void fm_draw_leg(s16 x0, s16 y0, s16 x1, s16 y1)
{
    s16 dx = (s16)(x1 - x0), dy = (s16)(y1 - y0);
    u8 skip = 0;
    if (dx == 0 && dy == 0) return;
    while (fm_poll_skip()) host_pump();         /* wait for the key release */
    s16 adx = dx < 0 ? (s16)-dx : dx, ady = dy < 0 ? (s16)-dy : dy;
    if (adx > ady) {
        s16 i = 0;
        while (dx > 0 ? i <= dx : i >= dx) {
            s16 j = idiv16_retry((s32)(s16)(dy * i), dx, NULL);      /* imul; cdq keeps the low word */
            fm_leg_pixel((s16)(x0 + i), (s16)(y0 + j), &skip);
            if (dx > 0) i++; else i--;
        }
    } else {
        s16 i = 0;
        while (dy > 0 ? i <= dy : i >= dy) {
            s16 j = idiv16_retry((s32)(s16)(dx * i), dy, NULL);
            fm_leg_pixel((s16)(x0 + j), (s16)(y0 + i), &skip);
            if (dy > 0) i++; else i--;
        }
    }
    while (fm_poll_skip()) host_pump();
}

static FarPtr wp_text(u16 w) { return far_rd(cur_dgroup, (u16)(w + 0x0C)); }

/* PS 06c7:0854 fm_route(wp *w, n) */
static void fm_route(u16 w, s16 n)
{
    u16 mk = stk_mark();
    u16 x0 = stk_alloc(2), y0 = stk_alloc(2), x1 = stk_alloc(2), y1 = stk_alloc(2);
    hs_push();
    gfx_font_select((s16)DSW(F_FONT3));
    fm_to_screen(x0, y0, (s32)DSL(w), (s32)DSL(w + 4));
    DSB(FM_ABORT) = 0;
    fm_popup(wp_text(w), 0, 0, 0);                         /* start point: text only */
    for (s16 i = 1; i < n && !DSB(FM_ABORT); i++) {
        u16 wi = (u16)(w + (i << 4)), wp = (u16)(w + ((i - 1) << 4));
        fm_to_screen(x1, y1, (s32)DSL(wi), (s32)DSL(wi + 4));
        fm_draw_leg((s16)DSW(x0), (s16)DSW(y0), (s16)DSW(x1), (s16)DSW(y1));
        if (!DSB(FM_ABORT)) {
            s32 ft = fm_feet((s32)DSL(wi + 8));
            s16 mi = (s16)fm_miles(wp, wi);
            u16 br = fm_bearing(wp, wi);
            fm_popup(wp_text(wi), br, mi, ft);
        }
        DSW(x0) = DSW(x1);
        DSW(y0) = DSW(y1);
    }
    if (DSW(FM_POPSIZE) != 0) {
        xfarfree(ds_far(FM_POPBUF));
        DSW(FM_POPSIZE) = 0;
    }
    hs_pop();
    if (DSB(FM_ABORT)) menu_set_result(2);                /* Esc during the route leaves the map */
    stk_release(mk);
}

/* PS 06c7:099b fm_clear_route — the clean map was cached in the spare page */
static void fm_clear_route(void)
{
    copy_bg_to_back();
    copy_back_to_front();
}

/* PS 06c7:09aa fm_draw_map(flights, n, area) */
static void fm_draw_map(u16 flights, u16 n, u16 area)
{
    u16 mk = stk_mark();
    u16 name = stk_alloc(10);
    for (int i = 0; i < 9; i++) DSB(name + i) = DSB(0x05BE + i);   /* SCOPY "w6" (overwritten) */
    pages_front_to_back();
    rtl_sprintf(name, 0x0601 /* "w%d" */, (unsigned)area);
    u16 bmp = bmp_load_planes(make_name(name, -1, 0x0605 /* "bmp" */));
    gfx_sprite_flip(bmp_frame(bmp, 0), 0x1A, 0x10, 0);
    bmp_free(bmp);
    fm_draw_flights(flights, n);
    stk_release(mk);
}

/* PS 06c7:0a25 fm_area_name(char *buf) */
static void fm_area_name(u16 buf)
{
    DSW(FM_S_DIST) = str_get(3);
    DSW(FM_S_BRG) = str_get(4);
    DSW(FM_S_ALT) = str_get(2);
    s16 s = table_load(0x0609 /* "wldnms" */, 0x0610 /* "dat" */, 2, 3, 0x12, 5, fnull(), fnull(), fnull());
    if (s == 0) ufatal(FM_SEG, 0x36E);
    FarPtr rec = table_rec(s, (s8)DSB(FM_AREA));
    if (far_is_null(rec)) ufatal(FM_SEG, 0x371);
    DSW(FM_AREANAME) = buf;
    strcpy_far_src(DSW(FM_AREANAME), table_text(s, (s8)DSB(FM_AREA), (u32)(s32)us(rec, 0)));
    table_free(s);
}

/* PS 06c7:0b14 fltmap_show(rec *flights, n_flights, wp *wps, n_wps, area) */
void fltmap_show(u16 flights_ds, u16 n_flights, u16 wps_ds, u16 n_wps, u16 area)
{
    u8 first = 1;
    u16 r = 0;
    DSB(FM_AREA) = (u8)area;
    DSW(FM_WPS) = wps_ds;
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x34);
    screen_show(0x1B, 0x39, 1);
    fm_draw_map(flights_ds, n_flights, area);
    fm_area_name(buf);
    hs_set_table(hs_item(FM_TBL, 0), 5);
    while (r != 2) {
        hs_draw_all();
        if (first) {
            copy_back_to_bg();                  /* cache the clean map */
            first = 0;
        }
        r = menu_run(codeptr_far(UFN(0x06c7, 0x026b)));
        if (r == 1 && DSB(FM_ROUTE) == 0) cursor_hide();
        ui_busy();
        if (r == 1) {                           /* FLIGHT PLAN */
            cursor_show();
            if ((s16)n_wps <= 1) DSB(FM_ROUTE) = 0;
            if (DSB(FM_ROUTE)) fm_clear_route();
            else fm_route(wps_ds, (s16)n_wps);
            r = menu_get_result();              /* 2 if the route was aborted with Esc */
            DSB(FM_ROUTE) = (u8)(DSB(FM_ROUTE) == 0);   /* the button toggles route / clean map */
        }
    }
    DSB(FM_ROUTE) = 0;
    bmp_free(DSW(FM_BMP));
    stk_release(mk);
}

/* ================================================================== PS 12c4: flight-map data */

#define FMP_FLIGHTS  0x0E10     /* {flights ptr, count, waypoints ptr, count} */

/* PS 12c4:0009 fmap_count_flights */
static s16 fmap_count_flights(void)
{
    s16 n = (s16)(DSW(0x6261) + DSW(0x6269));
    if (DSW(0x6265) != 0) n++;
    for (s16 i = 0; i < (s16)DSW(0x6267); i++)
        if (DSB((u16)(0x64B4 + i * 0x12)) != 0) n++;
    return n;
}

/* PS 12c4:0044 fmap_rec(dst, cls, flags, src12, id) */
static void fmap_rec(u16 dst, u8 cls, u8 flags, u16 src, u16 id)
{
    DSB(dst) = cls;
    DSB(dst + 1) = flags;
    for (int i = 0; i < 12; i++) DSB(dst + 2 + i) = DSB(src + i);
    DSW(dst + 0x0E) = id;
}

/* PS 12c4:0072 fmap_fill_flights(dst) */
static void fmap_fill_flights(u16 dst)
{
    u8 own_f, own_c, own_p, en_f, en_c, en_p;
    s16 side = (s16)(s8)service_side(DSB(0x62C5));
    if (side != 0) { own_f = 1; own_c = 3; own_p = 5; en_f = 2; en_c = 4; en_p = 6; }
    else           { own_f = 2; own_c = 4; own_p = 6; en_f = 1; en_c = 3; en_p = 5; }
    u16 f = 0x62C5;
    fmap_rec(dst, 0, DSB(f + 5), DSW(f + 0x22), DSW(f + 0x18));    /* the player's flight */
    dst = (u16)(dst + 0x10);
    side = (s16)(s8)service_side((u8)(s8)DSB(f));
    for (s16 k = 0; k < (s16)DSW(0x6269); k++) {                    /* convoys */
        u16 c = (u16)(0x6475 + k * 0x14);
        u8 cls = (s16)(s8)service_side(DSB(c)) == side ? own_c : en_c;
        fmap_rec(dst, cls, 0, DSW(c + 0x12), (u16)(s16)(s8)DSB(c + 4));
        dst = (u16)(dst + 0x10);
    }
    for (s16 k = 0; k < (s16)DSW(0x6267); k++) {                    /* places */
        u16 p = (u16)(0x64B1 + k * 0x12);
        if (DSB(p + 3) == 0) continue;
        u8 cls = (s16)(s8)service_side(DSB(p + 2)) == side ? own_p : en_p;
        fmap_rec(dst, cls, 0, (u16)(p + 6), (u16)(s16)(s8)DSB(p + 1));
        dst = (u16)(dst + 0x10);
    }
    for (s16 k = 1; k < (s16)DSW(0x6261); k++) {                    /* other flights: all own class (sic) */
        u16 fl = (u16)(0x62C5 + k * 0x24);
        fmap_rec(dst, own_f, DSB(fl + 5), DSW(fl + 0x22), DSW(fl + 0x18));
        dst = (u16)(dst + 0x10);
    }
    if (DSW(0x6265) != 0) {                                         /* escorted / extra flight */
        u16 fl = 0x63E5;
        u8 cls = (s16)(s8)service_side(DSB(fl)) == side ? own_f : en_f;
        fmap_rec(dst, cls, DSB(fl + 5), DSW(fl + 0x22), DSW(fl + 0x18));
    }
}

/* PS 12c4:023c fmap_wp_text(i) -> far text between the first and the second '|' */
static FarPtr fmap_wp_text(s16 i)
{
    s16 r = (s16)wp_message((u16)i);
    if (r < 0 || far_is_null(ds_far(0x5276)) || far_is_null(ds_far(0x5272))) return fnull();
    FarPtr ofs = ds_far(0x5272);
    FarPtr p = far_add(ds_far(0x5276), uw(ofs, (u16)(r * 2)));
    FarPtr q = rtl__fstrchr(p, '|');
    p = q;
    p.off = (u16)(p.off + 1);          /* no NULL check on the first '|' (as the original) */
    q = rtl__fstrchr(p, '|');
    if (!far_is_null(q)) uwb(q, 0, 0);
    return p;
}

/* PS 12c4:02d4 fmap_fill_waypoints(dst, from_file) */
static void fmap_fill_waypoints(u16 dst, s8 from_file)
{
    u16 msgs = 0;
    u16 fl = 0x62C5;
    if (from_file == 0) {
        stattxt_open();
        fltplan_sim_off();
        fltplan_build();
        stattxt_close();
        msgs = fltplan_messages();
    }
    s16 n = (s16)DSW(fl + 0x1C);
    if (DSB(0x626C) == 1) n--;                  /* CAP */
    for (s16 i = 0; i < n; i++, dst = (u16)(dst + 0x10)) {
        u16 src = (u16)(DSW(fl + 0x22) + i * 12);
        for (int k = 0; k < 12; k++) DSB(dst + k) = DSB(src + k);
        FarPtr t = from_file ? fmap_wp_text(i) : far_rd(cur_dgroup, (u16)(msgs + (i << 2)));
        far_wr(cur_dgroup, (u16)(dst + 0x0C), t);
    }
}

/* PS 12c4:0377 fmap_prepare(from_file) -> DS:0E10 */
u16 fmap_prepare(u16 from_file)
{
    DSW(FMP_FLIGHTS + 2) = (u16)fmap_count_flights();
    DSW(FMP_FLIGHTS) = xmalloc((u16)(DSW(FMP_FLIGHTS + 2) << 4));
    fmap_fill_flights(DSW(FMP_FLIGHTS));
    DSW(FMP_FLIGHTS + 6) = DSW(0x62E1);
    if (DSB(0x626C) == 1) DSW(FMP_FLIGHTS + 6) = (u16)(DSW(FMP_FLIGHTS + 6) - 1);
    DSW(FMP_FLIGHTS + 4) = xmalloc((u16)(DSW(FMP_FLIGHTS + 6) << 4));
    fmap_fill_waypoints(DSW(FMP_FLIGHTS + 4), (s8)from_file);
    return FMP_FLIGHTS;
}

/* PS 12c4:03cc fmap_release(from_file) */
void fmap_release(u16 from_file)
{
    if (DSW(FMP_FLIGHTS + 2) != 0) xfree(DSW(FMP_FLIGHTS));
    DSW(FMP_FLIGHTS + 2) = 0;
    DSW(FMP_FLIGHTS) = 0;
    if (DSW(FMP_FLIGHTS + 6) != 0) xfree(DSW(FMP_FLIGHTS + 4));
    DSW(FMP_FLIGHTS + 6) = 0;
    DSW(FMP_FLIGHTS + 4) = 0;
    if ((s8)from_file == 0) fltplan_free();
}

__attribute__((constructor)) static void fltmap_codeptrs(void)
{
    UREG(fm_draw_area_name);
    codeptr_register(PROG_PS, UFN(0x06c7, 0x00fd), (CodeFn)ps_fm_btn_cb_06c7);
    codeptr_register(PROG_PS, UFN(0x06c7, 0x026b), (CodeFn)ps_fm_frame_06c7);
}
