/* shell_ui: PREFLIGHT (PS segment 1513, screen 26, table 4c43:0000, 32 items) and the service / nation
 * chooser (1612, screen 48, table 4c6f:0000) — shell_ui_mission.md §8, §9.
 * (The DS offsets of the preflight state were checked in the disassembly; several in the spec are one
 * byte off: 0F4C/0F4D message flags, 0F4E/0F50 name tables, 0F52 plane name, 0F54/0F58 ranges, 0F5C
 * formation count, 0F5E allowed formations, 0F5F loadout count, 0F61 unit width.) */
#include "shell_ui/sui_a_priv.h"

#define TBL_PF      fd(0x4C43, 0x0000)
#define TBL_FF      fd(0x4C6F, 0x0000)
#define PF_TAB_X    0x0F44
#define PF_TAB_Y    0x0F46
#define PF_TAB_W    0x0F48
#define PF_TAB_H    0x0F4A
#define PF_MSG_LEAD 0x0F4C               /* u8: "only the flight leader may change it" */
#define PF_MSG_FIX  0x0F4D               /* u8: "cannot change in this mission" */
#define PF_FNAMES   0x0F4E               /* near char **: formation names */
#define PF_LNAMES   0x0F50               /* near char **: loadout names */
#define PF_PLANE    0x0F52               /* near char *: aircraft name */
#define PF_RANGE    0x0F54               /* s32 */
#define PF_RANGE_T  0x0F58               /* s32: with drop tank */
#define PF_NFORM    0x0F5C               /* s16 */
#define PF_NALLOW   0x0F5E               /* s8 */
#define PF_NLOAD    0x0F5F               /* s16 */
#define PF_UNIT_W   0x0F61               /* s16 */
#define PF_FIRST    0x4F22               /* u8: first entry (set by the fly menu) */
#define PF_FIXED    0x4F23               /* u8: fixed mission variant */
#define PF_FMAP     0x4F24               /* near: fmap_prepare result */
#define PF_BMP      0x4F26               /* near: FORMPLNS.BMP frames */
#define PF_NA_TEXT  0x4F28               /* near char *: "n/a" range text */
#define PF_TYPE     0x4F2A               /* s16: aircraft type */
#define PF_FCUR     0x4F2C               /* s8 */
#define PF_FLIST    0x4F2D               /* s8[] */
#define PF_MSG_KIND 0x4F37               /* u8: 1 formation, 0 loadout */
#define PF_LEADER   0x4F38               /* u8 */
#define PF_TANK     0x4F39               /* u8 */
#define PF_TANK0    0x4F3A               /* u8 */
#define LOADOUT     0x62C8               /* s8 */
#define FORMATION   0x62C6               /* s8 */
#define FLIGHT0     PS_DS_flights        /* 0x62C5 */
#define FLY_NOW     0x43D6
#define REAL_CAREER 0x4356
#define FF_MODE     0x4F6A               /* s8 */
#define FF_ROWS     0x4F54               /* near char *[11] */
#define FF_N        0x4F3C               /* s16 */
#define FF_IDS      0x4F3E               /* s16[] */
#define FF_RESULT   0x0FA2               /* u8 */

/* PS 1513:0009 pf_draw_window: panel and the mission-type tab */
void pf_draw_window(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(50);
    gfx_font_select((s16)DSW(T_FONT4));
    strcpy_far_src(buf, scd_string((s16)(DSC(0x626C) + 18)));
    draw_side_riveted_panel();
    rect_save();
    RXs = DSS(PF_TAB_X);
    RWs = DSS(PF_TAB_W);
    RHs = DSS(PF_TAB_H);
    u32 n = gfx_rect_size((u16)RWs, (u16)RHs);
    FarPtr save = xfarmalloc((u16)n);
    gfx_get_rect(save, RXs, (s16)(RYs + 2), RWs, RHs);
    RYs = DSS(PF_TAB_Y);
    gfx_put_rect(save, RXs, RYs, RWs, RHs);
    line_c(0xC2, RXs, RYs, (s16)(RXs + RWs), RYs);
    line_c(0xB4, (s16)(RXs + 1), RYs, (s16)(RXs + RWs - 1), RYs);
    line_c(0xC0, RXs, RYs, RXs, (s16)(RYs + RHs));
    s16 r = (s16)(RXs + RWs);
    gfx_line(r, RYs, r, (s16)(RYs + RHs));
    DSB(DRAW_COL) = 0xBF;
    gfx_line((s16)(RXs + 1), (s16)(RYs + 1), (s16)(RXs + 1), (s16)(RYs + RHs));
    r = (s16)(RXs + RWs - 1);
    gfx_line(r, (s16)(RYs + 1), r, (s16)(RYs + RHs));
    xfarfree(save);
    text_engrave(buf, 0xC1, 0xB1, 0xC0, ps_text_center_x(buf, (u16)RXs, (u16)RWs), (u16)(RYs + 2));
    ps_rect_restore();
    stk_release(m);
}

/* PS 1513:01d0 pf_draw_plane */
void pf_draw_plane(void)
{
    draw_inset_box2();
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FG) = 0xCC;
    DSB(TXT_FLAGS) = 1;
    gfx_text(DSW(PF_PLANE), (s16)(RXs + 4), (s16)(RYs + 3));
}

/* "<weapon type> <weapon name>" of ordnance item (type t, id) into buf */
static void pf_weapon_text(u16 buf, u16 fmt, s16 t, s16 id)
{
    FarPtr name = t == 1 ? ordtxt_name(id) : ordtxt_plural(id);
    FarPtr kind = strings_get(4, (s16)(t + 2));
    rtl_sprintf(buf, fmt, kind.off, kind.seg, name.off, name.seg);
}

/* PS 1513:0208 pf_draw_guns: armament and rounds per gun */
void pf_draw_guns(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(50), rounds = stk_alloc(10), types = stk_alloc(10), ids = stk_alloc(10);
    draw_inset_box2();
    s16 n = ord_n((s16)DSW(PF_TYPE), 0);
    ord_items((s16)DSW(PF_TYPE), 0, types, ids);
    ord_gun_rounds((s16)DSW(PF_TYPE), rounds);
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FG) = 0xCC;
    DSB(TXT_FLAGS) = 1;
    s16 y = (s16)(RYs + 3);
    for (s16 i = 0; i < n; i++, y = (s16)(y + 7)) {
        s16 id = DSS((u16)(ids + i * 2));
        if (id < 0) continue;
        pf_weapon_text(buf, 0x0F63 /* "%Fs %Fs" */, DSS((u16)(types + i * 2)), id);
        gfx_text(buf, (s16)(RXs + 4), y);
        rtl_sprintf(buf, 0x0F6B /* "%d" */, DSW((u16)(rounds + i * 2)));
        gfx_text(buf, (s16)(RXs + RWs - gfx_text_width(buf) - 4), y);
    }
    stk_release(m);
}

/* PS 1513:0348 pf_loadout_cb: next valid loadout (leader only, not in a fixed mission) */
void pf_loadout_cb(void)
{
    if (!btn_small_cb(0)) {
        fbw(cur_item(), IT_MASK, 0x74);
        return;
    }
    if (DSB(PF_FIXED)) {
        DSB(PF_MSG_FIX) = 1;
        return;
    }
    if (DSB(PF_LEADER) == 0) {
        DSB(PF_MSG_LEAD) = 1;
        DSB(PF_MSG_KIND) = 0;
        return;
    }
    u16 m = stk_mark();
    u16 types = stk_alloc(12), ids = stk_alloc(10);
    s8 k = DSC(LOADOUT);
    do {
        k = (s8)((s16)(k + 1) % DSS(PF_NLOAD));
        if (k == 0) k = 1;
        ord_items((s16)DSW(PF_TYPE), k, types, ids);
    } while (DSS(ids) == -1 || (DSB(PF_TANK0) != 0 && !ord_has_droptank((s16)DSW(PF_TYPE), k)));
    stk_release(m);
    DSB(LOADOUT) = (u8)k;
    FarPtr it = cur_item();
    fww(it, IT_LABEL, DSW((u16)(DSW(PF_LNAMES) + (k - 1) * 2)));
    fbw(it, IT_MASK, 3);
    FarPtr t = TBL_PF;
    fbw(t, 0xFE, 3);
    fbw(t, 0x140, 3);
    fbw(t, 0x16C, 3);
}

/* PS 1513:0432 pf_draw_ordnance */
void pf_draw_ordnance(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(50), types = stk_alloc(10), ids = stk_alloc(10);
    s8 L = DSC(LOADOUT);
    draw_inset_box2();
    s16 n = ord_n((s16)DSW(PF_TYPE), L);
    ord_items((s16)DSW(PF_TYPE), L, types, ids);
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FG) = 0xCC;
    DSB(TXT_FLAGS) = 1;
    s16 y = (s16)(RYs + 3);
    DSB(PF_TANK) = 0;
    for (s16 i = 0; i < n; i++, y = (s16)(y + 7)) {
        s16 id = DSS((u16)(ids + i * 2));
        if (id == 0) {
            DSB(PF_TANK) = 1;
        } else if (id > -1) {
            pf_weapon_text(buf, 0x0F6E /* "%Fs %Fs" */, DSS((u16)(types + i * 2)), id);
            gfx_text(buf, (s16)(RXs + 4), y);
        }
    }
    fbw(cur_item(), IT_MASK, 0x40);
    stk_release(m);
}

/* PS 1513:054c pf_draw_tank: yes / no */
void pf_draw_tank(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(6);
    draw_inset_box2();
    strcpy_far_src(buf, strings_get(4, DSB(PF_TANK) == 0));
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FG) = 0xCC;
    DSB(TXT_FLAGS) = 1;
    gfx_text(buf, (s16)(RXs + 4), (s16)(RYs + 3));
    fbw(cur_item(), IT_MASK, 0x40);
    stk_release(m);
}

/* PS 1513:05ba pf_draw_range */
void pf_draw_range(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(34);
    DSB(TXT_FG) = 0xCC;
    gfx_font_select((s16)DSW(T_FONT3));
    draw_inset_box2();
    s16 x;
    u16 s;
    if (DSB(REAL_CAREER) == 0) {
        x = (s16)(RXs + 4);
        s = DSW(PF_NA_TEXT);
    } else {
        rtl_ltoa(DSB(PF_TANK) ? DSSL(PF_RANGE_T) : DSSL(PF_RANGE), buf, 10);
        gfx_text(buf, (s16)(RXs + 4), (s16)(RYs + 3));
        strcpy_far_src(buf, strings_get(4, 13));
        if (DSW(PF_UNIT_W) == 0) DSS(PF_UNIT_W) = gfx_text_width(buf);
        x = (s16)(RXs + RWs - DSS(PF_UNIT_W) - 2);
        s = buf;
    }
    gfx_text(s, x, (s16)(RYs + 3));
    stk_release(m);
}

/* PS 1513:068c pf_draw_roster: flight 0's members */
void pf_draw_roster(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(50);
    u16 rec = FLIGHT0;
    s16 y = (s16)(RYs + 3);
    for (s8 k = 0; k < DSS((u16)(rec + 0x1A)); k++, y = (s16)(y + 7)) {
        rtl_sprintf(buf, 0x0F76 /* "%d." */, (unsigned)(s16)(k + 1));
        text_engrave(buf, 0xC7, 0xB1, 0xC0, (u16)RXs, (u16)y);
        if (k == 0) {
            strcpy_far_src(buf, strings_get(4, 15));
            text_engrave(buf, 0xC7, 0xB1, 0xC0, (u16)(RXs + 10), (u16)y);
        }
        s8 code = DSC((u16)(rec + 10 + k));
        if (code == -1) {
            strcpy_far_src(buf, ace_short_name(DSS((u16)(rec + k * 2 + 0x0E))));
            str_upper(buf);
        } else {
            strcpy_far_src(buf, strings_get(4, (s16)(code + 16)));
        }
        text_engrave(buf, 0xC7, 0xB1, 0xC0, (u16)(RXs + 52), (u16)y);
    }
    stk_release(m);
}

/* PS 1513:07c4 pf_formation_cb */
void pf_formation_cb(void)
{
    FarPtr it = cur_item();
    if (!btn_small_cb(0)) {
        fbw(it, IT_MASK, 0x74);
        return;
    }
    if (DSB(PF_FIXED)) {
        DSB(PF_MSG_FIX) = 1;
        return;
    }
    if (DSB(PF_LEADER) == 0) {
        DSB(PF_MSG_LEAD) = 1;
        DSB(PF_MSG_KIND) = 1;
        return;
    }
    DSB(PF_FCUR) = (u8)((s16)(DSC(PF_FCUR) + 1) % (s16)DSC(PF_NALLOW));
    DSB(FORMATION) = DSB((u16)(PF_FLIST + DSC(PF_FCUR)));
    fww(it, IT_LABEL, DSW((u16)(DSW(PF_FNAMES) + DSC(FORMATION) * 2)));
    fbw(it, IT_MASK, 3);
    fbw(TBL_PF, 0x248, 3);
}

/* PS 1513:084f pf_draw_formation: the flight's positions (FORMPLNS.BMP) */
void pf_draw_formation(void)
{
    static const s8 dx[8] = { 0, 0, -16, 16, 16, 16, -16, -16 };
    static const s8 dy[8] = { -11, 11, 0, 0, -11, 11, -11, 11 };
    u16 codes = 0x62CF;
    s8 n = DSC(0x62DF);
    s16 form = DSC(FORMATION);
    draw_inset_box2();
    s8 p = -1;
    for (s8 k = 0; k < n; k++)
        if (DSB((u16)(codes + k)) == 4) { p = k; break; }
    DSB(TXT_FG) = 0xDC;
    u16 frames = DSW(PF_BMP);
    sprite(p == 0 ? DSW(frames) : DSW((u16)(frames + 2)), (s16)(RXs + 55), (s16)(RYs + 4), 0);
    sprite(DSW((u16)(frames + 4)), (s16)(RXs + 50), (s16)(RYs + 3), 0);
    u16 m = stk_mark();
    u16 steps = stk_alloc(10), cnt = stk_alloc(2);
    for (s8 i = 2; i <= n; i++) {
        form_layout(form, i, steps, cnt);
        s16 x = (s16)(RXs + 55), y = (s16)(RYs + 4);
        for (s8 c = 0; c < DSC(cnt); c++) {
            u16 d = (u16)(s16)DSC((u16)(steps + c));
            if (d <= 7) { x = (s16)(x + dx[d]); y = (s16)(y + dy[d]); }
        }
        sprite((s16)p == (s16)(i - 1) ? DSW(frames) : DSW((u16)(frames + 2)), x, y, 0);
        sprite(DSW((u16)(frames + (i + 1) * 2)), (s16)(x - 5), (s16)(y - 1), 0);
    }
    stk_release(m);
    fbw(cur_item(), IT_MASK, 0x40);
}

/* PS 1513:09ee pf_btn_cb */
void pf_btn_cb(void)
{
    if (btn_cb()) {
        DSW(T_DISP_RES) = DSW(T_IDX);
        menu_set_result(1);
    }
}

/* PS 1513:0a0c pf_draw_label */
void pf_draw_label(void)
{
    u16 l = fw(cur_item(), IT_LABEL);
    gfx_font_select((s16)DSW(T_FONT3));
    text_engrave(l, 0xC7, 0xB1, 0xC0, (u16)RXs, (u16)RYs);
}

/* PS 1513:0a4a pf_draw_rule */
void pf_draw_rule(void)
{
    line_c(0xC7, RXs, RYs, (s16)(RXs + RWs), RYs);
    line_c(0xB1, RXs, (s16)(RYs + 1), (s16)(RXs + RWs), (s16)(RYs + 1));
}

/* PS 1513:0a99 pf_plane_init */
static void pf_plane_init(void)
{
    if (DSB(PF_FIRST)) DSW(PF_TYPE) = DSW(0x62DD);
    FarPtr name = ac_name((s16)DSW(PF_TYPE));
    DSW(PF_PLANE) = xmalloc((u16)(rtl__fstrlen(name) + 1));
    strcpy_far_src(DSW(PF_PLANE), ac_name((s16)DSW(PF_TYPE)));
    str_upper(DSW(PF_PLANE));
    if (DSB(PF_FIRST)) {
        DSSL(PF_RANGE) = ac_range((s16)DSW(PF_TYPE));
        DSSL(PF_RANGE_T) = DSSL(PF_RANGE) + ac_range_ext((s16)DSW(PF_TYPE));
    }
}

/* PS 1513:0b21 pf_names_init */
static void pf_names_init(void)
{
    FarPtr t = TBL_PF;
    DSS(PF_NFORM) = form_count();
    DSW(PF_FNAMES) = xmalloc((u16)(DSS(PF_NFORM) << 1));
    for (s16 k = 0; k < DSS(PF_NFORM); k++) {
        DSW((u16)(DSW(PF_FNAMES) + k * 2)) = xmalloc((u16)(rtl__fstrlen(form_name(k)) + 1));
        strcpy_far_src(DSW((u16)(DSW(PF_FNAMES) + k * 2)), form_name(k));
    }
    fww(t, 0x234, DSW((u16)(DSW(PF_FNAMES) + DSC(FORMATION) * 2)));
    DSW(PF_NLOAD) = 11;
    DSW(PF_LNAMES) = xmalloc(0x16);
    for (s16 k = 0; k < DSS(PF_NLOAD); k++) {
        DSW((u16)(DSW(PF_LNAMES) + k * 2)) = xmalloc((u16)(rtl__fstrlen(strings_get(4, (s16)(k + 21))) + 1));
        strcpy_far_src(DSW((u16)(DSW(PF_LNAMES) + k * 2)), strings_get(4, (s16)(k + 21)));
    }
    fww(t, 0xEA, DSW((u16)(DSW(PF_LNAMES) + (DSC(LOADOUT) - 1) * 2)));
    FarPtr na = strings_get(4, 14);
    DSW(PF_NA_TEXT) = xmalloc((u16)(rtl__fstrlen(na) + 1));
    strcpy_far_src(DSW(PF_NA_TEXT), na);
}

/* PS 1513:0c71 pf_formations_init */
static void pf_formations_init(void)
{
    u16 date = DSW(PS_DS_mission_date);
    if (!DSB(PF_FIRST)) return;
    if (DSB(PF_FIXED) == 0) {
        flight_open();
        DSB(PF_NALLOW) = (u8)flight_formations((s8)DSB(FLIGHT0), (s8)DSB(0x62C7), date, PF_FLIST);
        DSB(PF_FCUR) = 0;
        DSB(FORMATION) = DSB(PF_FLIST);
        flight_close();
    } else {
        DSB(PF_FLIST) = DSB(FORMATION);
        DSB(PF_NALLOW) = 1;
    }
}

/* PS 1513:0cd3 pf_names_free */
static void pf_names_free(void)
{
    for (s16 k = 0; k < DSS(PF_NFORM); k++) xfree(DSW((u16)(DSW(PF_FNAMES) + k * 2)));
    xfree(DSW(PF_FNAMES));
    for (s16 k = 0; k < DSS(PF_NLOAD); k++) xfree(DSW((u16)(DSW(PF_LNAMES) + k * 2)));
    xfree(DSW(PF_LNAMES));
    xfree(DSW(PF_PLANE));
    xfree(DSW(PF_NA_TEXT));
}

/* PS 1513:0d44 pf_draw_bg */
void pf_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for(DSB(FLIGHT0), DSW(PS_DS_mission_date), str_get(0));
}

/* PS 1513:0d68 pf_msg_subst: formation / loadout */
FarPtr pf_msg_subst(s8 c)
{
    (void)c;
    return scd_string(DSB(PF_MSG_KIND) == 0 ? 20 : 19);
}

/* PS 1513:0d8e pf_frame */
void pf_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
    if (DSB(PF_MSG_LEAD)) {
        FarPtr s = FNP(pf_msg_subst);
        req_ok(s.off, s.seg, 0x12, 8);
        DSB(PF_MSG_LEAD) = 0;
    }
    if (DSB(PF_MSG_FIX)) {
        req_ok(0, 0, 0x16, 8);
        DSB(PF_MSG_FIX) = 0;
    }
    if (menu_get_result() == 2) DSW(T_DISP_RES) = 0x1B;
}

/* PS 1513:0e05 preflight(variant) */
void preflight(u16 variant)
{
    DSB(PF_FIXED) = (s16)variant >= 0;
    pf_plane_init();
    ordtxt_open();
    form_open();
    ps_strings_load_24e3(make_name(0x0F7A /* "misc" */, -1, 0x0F7F /* "dat" */));
    pf_formations_init();
    pf_names_init();
    DSW(PF_BMP) = bmp_load_planes(make_name(0x0F83 /* "formplns" */, -1, 0x0F8C /* "bmp" */));
    DSB(PF_LEADER) = DSB(0x62CF) == 4;
    if (DSB(PF_FIRST)) {
        DSB(PF_TANK) = (u8)ord_has_droptank((s16)DSW(PF_TYPE), DSC(LOADOUT));
        DSB(PF_FIRST) = 0;
        DSB(PF_TANK0) = DSB(PF_TANK);
    }
    for (;;) {
        screen_open(26, FNP(pf_draw_bg), 0x396, 1);
        hs_set_table(TBL_PF, 32);
        hs_draw_all();
        menu_run(FNP(pf_frame));
        ui_busy();
        if (DSW(T_DISP_RES) == 0x1C) {                          /* REALISM PANEL */
            strings_free();
            realism_run();
            ps_strings_load_24e3(make_name(0x0F90, -1, 0x0F95));
            continue;
        }
        if (DSW(T_DISP_RES) != 0x1D) break;                     /* FLIGHT MAP */
        u8 from_file = (s16)variant >= 0 || DSB(PS_DS_mission_header) == 2;
        strings_free();
        DSW(PF_FMAP) = fmap_prepare(from_file);
        u16 p = DSW(PF_FMAP);
        fltmap_show(DSW(p), DSW((u16)(p + 2)), DSW((u16)(p + 4)), DSW((u16)(p + 6)), DSW(0x626D));
        fmap_release(from_file);
        ps_strings_load_24e3(make_name(0x0F99, -1, 0x0F9E));
    }
    pf_names_free();
    ordtxt_close();
    form_close();
    strings_free();
    bmp_free(DSW(PF_BMP));
    if (DSW(T_DISP_RES) == 0x1E) briefing(variant);             /* MISSION BRIEFING */
    else if (DSW(T_DISP_RES) == 0x1F) DSB(FLY_NOW) = 1;         /* BEGIN MISSION */
    else if (DSW(T_DISP_RES) == 0x1B) DSB(FLY_NOW) = 0;         /* DECLINE / Esc */
}

/* PS 1513:0fdc pf_init */
void pf_init(void) { hs_default_links(TBL_PF, 32); }

/* ======================================================================== chooser (1612) */

/* PS 1612:0005 ff_build(buf): the rows (services, nations or data sets) */
static void ff_build(u16 buf)
{
    for (s16 k = 0; k < 11; k++) DSW((u16)(FF_ROWS + k * 2)) = (u16)(k * 30 + buf);
    s8 mode = DSC(FF_MODE);
    bool own = mode != 1 && mode != 5 && mode != 4;
    if (own) ps_strings_load_24e3(make_name(0x0FB9 /* "sftbtns" */, 0, 0x0FC1 /* "dat" */));
    if (mode == 4) {
        s8 c = module_iter_reset();
        DSW(FF_N) = 0;
        for (;;) {
            s16 k = c;
            if (module_highest() < k) break;
            rtl_strcpy(DSW((u16)(FF_ROWS + DSS(FF_N) * 2)), DSW((u16)(k * 2 + 0x0FA3)));
            DSW((u16)(FF_IDS + DSS(FF_N) * 2)) = (u16)k;
            DSW(FF_N)++;
            c = module_iter_next();
        }
    } else {
        DSW(FF_N) = 0;
        for (s16 k = 0; k < 12; k++) {
            bool ok = mode == 3 ? (u8)nation_available((u16)(u8)k) != 0 : (u8)service_available((u16)(u8)k) != 0;
            if (!ok) continue;
            FarPtr s = strings_get(0, (s16)(mode == 3 ? k + 0x90 : k + 0x85));
            if (far_mp(s)[0] == 0) continue;
            strcpy_far_src(DSW((u16)(FF_ROWS + DSS(FF_N) * 2)), s);
            DSW((u16)(FF_IDS + DSS(FF_N) * 2)) = (u16)k;
            DSW(FF_N)++;
        }
    }
    if (own) strings_free();
}

/* PS 1612:0170 ff_draw_title */
void ff_draw_title(void)
{
    s8 mode = DSC(FF_MODE);
    gfx_font_select((s16)DSW(T_FONT4));
    s16 a, b;
    if (mode == 2) { a = 0; b = 2; }
    else if (mode == 3) { a = 1; b = 3; }
    else if (mode == 4) { a = 4; b = 5; }
    else if (mode == 5) { a = 10; b = 11; }
    else { a = 6; b = 7; }
    u16 s1 = str_get(a);
    u16 s2 = str_get(b);
    DSB(TXT_FLAGS) = 3;
    text_engrave(s1, 0xC1, 0xB1, 0xC0, ps_text_center_x(s1, (u16)RXs, (u16)RWs), (u16)(RYs + 4));
    text_engrave(s2, 0xC1, 0xB1, 0xC0, ps_text_center_x(s2, (u16)RXs, (u16)RWs), (u16)(RYs + 14));
    if (mode != 2 && mode != 3) {
        draw_rivets((u16)(RXs + 24), (u16)(RYs + 5), 3);
        draw_rivets((u16)(RXs + 16), (u16)(RYs + 12), 2);
        draw_rivets((u16)(RXs + RWs - 24), (u16)(RYs + 5), 3);
        draw_rivets((u16)(RXs + RWs - 16), (u16)(RYs + 12), 2);
    }
}

/* PS 1612:02dd ff_draw_subtitle */
void ff_draw_subtitle(void)
{
    gfx_font_select((s16)DSW(T_FONT3));
    text_engrave(str_get(DSC(FF_MODE) == 3 ? 12 : 13), 0xC1, 0xB1, 0xC0, (u16)RXs, (u16)RYs);
}

/* PS 1612:031f ff_btn_cb */
void ff_btn_cb(void)
{
    if (btn_small_cb(0)) menu_set_result(DSW(T_IDX) == 8 ? 1 : 2);
}

/* PS 1612:0347 ff_draw_row(i, y) */
void ff_draw_row(s16 i, s16 y)
{
    gfx_text(DSW((u16)(FF_ROWS + i * 2)), (s16)(RXs + 5), y);
}

/* PS 1612:0367 ff_frame */
void ff_frame(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), DSW(T_BTN), 0);
    hs_present();
}

/* PS 1612:0388 fly_for_choose(mode) -> service / nation / set, FFh cancelled */
u16 fly_for_choose(u16 mode)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(330);
    DSB(FF_MODE) = (u8)mode;
    ff_build(buf);
    lbox_setup(0, 0, DSW(FF_N), 5, fnul(), FNP(ff_draw_row));
    lbox_bind(TBL_FF, 3, 5);
    screen_show(48, 0xE4, 1);
    hs_set_table(TBL_FF, 9);
    hs_focus(8);
    hs_draw_all();
    if (menu_run(FNP(ff_frame)) == 2) DSB(FF_RESULT) = 0xFF;
    else DSB(FF_RESULT) = DSB((u16)(FF_IDS + (s16)lbox_selected() * 2));
    ui_busy();
    stk_release(m);
    return DSB(FF_RESULT);
}

__attribute__((constructor)) static void preflight_codeptrs(void)
{
    REG(pf_draw_window);
    REG(pf_draw_plane);
    REG(pf_draw_guns);
    REG(pf_loadout_cb);
    REG(pf_draw_ordnance);
    REG(pf_draw_tank);
    REG(pf_draw_range);
    REG(pf_draw_roster);
    REG(pf_formation_cb);
    REG(pf_draw_formation);
    REG(pf_btn_cb);
    REG(pf_draw_label);
    REG(pf_draw_rule);
    REG(pf_draw_bg);
    REG(pf_msg_subst);
    REG(pf_frame);
    REG(ff_draw_title);
    REG(ff_draw_subtitle);
    REG(ff_btn_cb);
    REG(ff_draw_row);
    REG(ff_frame);
}
