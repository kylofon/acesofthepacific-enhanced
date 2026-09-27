/* shell_ui part B: dogace.c (PS 0c8a) — "DOGFIGHT A FAMOUS ACE" (shell_ui_career.md §1).
 * Screen 14 DOGACE, hot-spot table 4b01:000f (11 items). */
#include "shell_ui/sui_b_priv.h"

#define DG_SEG      0x4b01
#define DG_TBL      DG_SEG, 0x000f
#define DG_CHANGE   0x0928      /* u8: "change service" request */
#define DG_NACES    0x43EE
#define DG_HANDLES  0x43F0      /* near: ACE<n>.BMP handles per ACEFACE record */
#define DG_TABLE    0x43F2      /* ACEFACE.DAT table slot: {u16 service, bitmap, first, last} */
#define DG_NRECS    0x43F4
#define DG_LOADED   0x43F6
#define DG_ACE      0x43F7      /* s8: chosen ace */
#define DG_SERVICE  0x43F8      /* s8: enemy service */
#define DG_PRESSED  0x43F9

static u16 dg_handle_at(s16 i) { return (u16)(DSW(DG_HANDLES) + i * 2); }

/* PS 0c8a:0009 dg_load_faces */
static void dg_load_faces(void)
{
    if (DSB(DG_LOADED) == 0) {
        DSW(DG_TABLE) = (u16)table_load(0x0929 /* "aceface" */, 0x0931 /* "dat" */, 8, 0, 0x11, 1,
                                        fnull(), fnull(), fnull());
        if (DSW(DG_TABLE) == 0) ufatal(DG_SEG, 0x66);
        DSW(DG_NRECS) = (u16)table_count((s16)DSW(DG_TABLE), 0);
        DSW(DG_HANDLES) = xmalloc((u16)(DSW(DG_NRECS) << 1));
        for (s16 i = 0; i < (s16)DSW(DG_NRECS); i++) DSW(dg_handle_at(i)) = 0;
        DSB(DG_LOADED) = 1;
    }
    for (s16 i = 0; i < (s16)DSW(DG_NRECS); i++) {
        FarPtr rec = table_rec((s16)DSW(DG_TABLE), i);
        if (!far_is_null(rec)) {
            if ((s16)(s8)DSB(DG_SERVICE) == (s16)uw(rec, 0)) {
                s16 bm = us(rec, 2);
                DSW(dg_handle_at(i)) = bmp_load_planes(make_name(0x0935 /* "ace" */, bm, 0x0939 /* "bmp" */));
                if (DSW(dg_handle_at(i)) == 0)
                    assert_fail(far_make(SEG(DG_SEG), 0), 0x7A, make_name(0x093D /* "ace" */, bm, 0x0941 /* "bmp" */));
                continue;
            }
            if (DSW(dg_handle_at(i)) != 0) bmp_free(DSW(dg_handle_at(i)));
        }
        DSW(dg_handle_at(i)) = 0;
    }
}

/* PS 0c8a:018d dg_free_faces */
static void dg_free_faces(void)
{
    if (DSB(DG_LOADED) == 0) return;
    table_free((s16)DSW(DG_TABLE));
    for (s16 i = 0; i < (s16)DSW(DG_NRECS); i++)
        if (DSW(dg_handle_at(i)) != 0) bmp_free(DSW(dg_handle_at(i)));
    xfree(DSW(DG_HANDLES));
    DSB(DG_LOADED) = 0;
}

/* PS 0c8a:01e4 dg_change_service — "WHO WILL YOU FLY AGAINST?" */
static void dg_change_service(void)
{
    u8 s = (u8)fly_for_choose(5);
    if (s != 0xFF) DSB(DG_SERVICE) = s;
    dg_load_faces();
}

/* PS 0c8a:0205 dg_subst(c, item far *it) -> 0 */
static FarPtr dg_subst(s8 c, FarPtr it)
{
    if (c == 'a') {
        strcpy_far_src(LBLBUF, strings_get(0, (s16)((s8)DSB(DG_SERVICE) + 0x85)));
        uww(it, HS_LABEL, LBLBUF);
    }
    return fnull();
}

/* PS 0c8a:0248 dg_btn_cb */
static void dg_btn_cb(void)
{
    if (btn_cb()) {
        DSB(DG_PRESSED) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 0c8a:0266 dg_service_cb — refreshes the label with the item index as the letter (no effect) */
static void dg_service_cb(void)
{
    dg_subst((s8)DSB(CUR_IDX), cur_item());
    set_mask(cur_item(), 0x74);
    if (choose_entry_cb()) DSB(DG_CHANGE) = 1;
}

/* PS 0c8a:0295 dg_draw_scroll */
static void dg_draw_scroll(void)
{
    draw_bevel_box();
    DSB(LINE_C) = 0xC0;
    gfx_rect((s16)(DSS(RX) + 4), (s16)(DSS(RY) + 0x19), 0x12, 0x0F);
}

/* PS 0c8a:02c2 dg_draw_prompt — "Aces from the %a." with the service as an inline button */
static void dg_draw_prompt(void)
{
    u16 mk = stk_mark();
    u16 list = stk_alloc(4);
    DSW(list) = str_get(3);
    DSW(list + 2) = 0;
    gfx_font_select((s16)DSW(F_FONT3));
    ui_flow_text_buttons(hs_item(DG_TBL, 0), 2, UFP(dg_subst), list, DSS(RX), DSS(RY), (s16)(DSS(RX) + DSS(RW)));
    set_mask(cur_item(), 0x40);
    stk_release(mk);
}

/* PS 0c8a:0321 dg_draw_face */
static void dg_draw_face(void)
{
    s16 rec_i = -1, frame = -1;
    acelist_select(0);
    DSB(DG_ACE) = (u8)acelist_id((s16)lbox_selected());
    for (s16 i = 0; i < (s16)DSW(DG_NRECS); i++) {
        FarPtr rec = table_rec((s16)DSW(DG_TABLE), i);
        s16 id = (s8)DSB(DG_ACE);
        if (id >= us(rec, 4) && id <= us(rec, 6)) {
            rec_i = i;
            frame = (s16)(id - us(rec, 4));
            break;
        }
    }
    if (rec_i <= -1 || frame <= -1) ufatal(DG_SEG, 0x12A);
    draw_inset_box2();
    if (DSW(dg_handle_at(rec_i)) == 0) ufatal(DG_SEG, 0x12E);
    gfx_sprite_flip(bmp_frame(DSW(dg_handle_at(rec_i)), frame), (s16)(DSS(RX) + 2), (s16)(DSS(RY) + 2), 0);
    set_mask(cur_item(), 0x40);
}

/* PS 0c8a:042a dg_draw_bio */
static void dg_draw_bio(void)
{
    FarPtr bio = acetxt_bio((s8)DSB(DG_ACE));
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    rect_grow(4);
    draw_popup_box();
    ps_rect_shrink(4);
    flow_setup((u16)-3, 8, 1);
    flow_text(bio, 1, fnull());
    set_mask(cur_item(), 0x40);
}

/* PS 0c8a:04a0 dg_draw_row(i, y) */
static void dg_draw_row(s16 i, s16 y)
{
    acelist_select(0);
    s16 a = acelist_id(i);
    s16 sym = (s16)((s16)(s8)service_side((u16)(s16)ace_service(a)) + 0x10);
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), sym), (s16)(DSS(RX) + 5), y, 0);
    gfx_text(strcpy_far_src(STRBUF, ace_name(a)).off, (s16)(DSS(RX) + 0x0F), y);
    gfx_text(rtl_itoa(ace_kills(a), STRBUF, 10), (s16)(DSS(RX) + DSS(RW) - 0x0F), y);
    set_mask(hs_item(DG_TBL, 6), 3);                /* the face and the bio follow the list */
    set_mask(hs_item(DG_TBL, 5), 3);
}

/* PS 0c8a:0571 dg_draw_bg */
static void dg_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for(DSB(DG_SERVICE), 0, str_get(0));
}

/* PS 0c8a:0594 dg_frame — after the service chooser its own modal result ends this menu_run */
static void dg_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (DSB(DG_CHANGE)) {
        dg_change_service();
        DSB(DG_PRESSED) = 2;
        DSB(DG_CHANGE) = 0;
    } else if (menu_get_result() == 2) {
        DSB(DG_PRESSED) = 9;
    }
}

/* PS 0c8a:05db dogace_choose -> s8 ace id / -1 — caller flymenu_run */
u16 dogace_choose(void)
{
    acetxt_open();
    strtab_load(make_name(0x0949 /* "sftbtns" */, 0, 0x0951 /* "dat" */));
    DSB(DG_SERVICE) = (u8)fly_for_choose(5);
    if (DSB(DG_SERVICE) == 0xFF) {
        DSB(DG_ACE) = 0xFF;
    } else {
        DSB(DG_PRESSED) = 10;
        dg_load_faces();
        for (;;) {
            acelist_build((s8)DSB(DG_SERVICE), 0, 0, 1);
            acelist_select(0);
            DSW(DG_NACES) = (u16)acelist_enabled_count();
            lbox_setup(0, 0, DSW(DG_NACES), 10, fnull(), UFP(dg_draw_row));
            lbox_bind(hs_item(DG_TBL, 0), 3, 7);
            screen_open(0x0E, UFP(dg_draw_bg), 0x44, 1);
            hs_set_table(hs_item(DG_TBL, 0), 11);
            hs_focus((u16)(s16)(s8)DSB(DG_PRESSED));
            hs_draw_all();
            menu_run(UFP(dg_frame));
            ui_busy();
            if (DSB(DG_PRESSED) == 9) { DSB(DG_ACE) = 0xFF; break; }       /* CANCEL / Esc */
            if (DSB(DG_PRESSED) == 10) {                                    /* SELECT */
                for (s16 d = 0; d <= 8; d++) {                              /* the ace's data set */
                    s16 hi = (s16)(d * 15 + 0x1D), lo = (s16)((d - 1) * 15 + 0x1D);
                    s16 a = (s8)DSB(DG_ACE);
                    if (a < hi && a >= lo) module_set_active((u8)d);
                }
                break;
            }
        }
    }
    dg_free_faces();
    strtab_free();
    acetxt_close();
    return DSB(DG_ACE);
}

__attribute__((constructor)) static void dogace_codeptrs(void)
{
    UREG(dg_subst);
    UREG(dg_btn_cb);
    UREG(dg_service_cb);
    UREG(dg_draw_scroll);
    UREG(dg_draw_prompt);
    UREG(dg_draw_face);
    UREG(dg_draw_bio);
    UREG(dg_draw_row);
    UREG(dg_draw_bg);
    UREG(dg_frame);
}
