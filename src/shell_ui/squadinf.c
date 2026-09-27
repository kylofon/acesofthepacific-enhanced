/* shell_ui part B: squadinf.c (PS 07ba) — squadron info / squadron choice (shell_ui_flight.md §3).
 * Screens 19 SQD_A / 20 SQD_J, hot-spot table 49ef:0011 (16 items). */
#include "shell_ui/sui_b_priv.h"

#define SI_SEG      0x49ef
#define SI_TBL      SI_SEG, 0x0011
#define SI_CURSOR   0x0021           /* word inside the table segment: item 0's user word */
#define SI_LIST     0x0624           /* far array of {u16 squadron, u16 date} */
#define SI_NALLOC   0x0628
#define SI_NUSED    0x062A
#define SI_LOADED   0x062C           /* s8: squadron in the buffers (FFh none) */
#define SI_FLAGS    0x42C4           /* FLAGS.BMP */
#define SI_FHDL     0x42C6           /* SIMQ_HDL.FNT */
#define SI_FSML     0x42C8           /* SIMQ_SML.FNT */
#define SI_DATE0    0x42CA
#define SI_DATE1    0x42CC
#define SI_FIELDS   0x42CE           /* char *[3]: aircraft, station, pilot quality */
#define SI_ACES     0x42D4           /* char *[4] */
#define SI_NAME     0x42DC           /* char *: name, name2, "A %s SQUADRON", "FOR THE %s" */

static u16 si_cursor(void) { return SEGW(SI_SEG, SI_CURSOR); }
static void si_set_cursor(u16 v) { SEGW(SI_SEG, SI_CURSOR) = v; }
static FarPtr si_entry(u16 i) { return far_add(ds_far(SI_LIST), (u16)(i << 2)); }

/* PS 07ba:0004 si_draw_prompt */
static void si_draw_prompt(void)
{
    draw_popup_box();
    gfx_font_select((s16)DSW(F_FONT3));
    text_engrave_center(uw(cur_item(), HS_LABEL), 0xC1, 0xB1, 0xC0);
}

/* PS 07ba:0034 si_btn_cb */
static void si_btn_cb(void)
{
    if (btn_small_cb(0)) {
        DSW(HS_RES) = DSW(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 07ba:0056 si_draw_header */
static void si_draw_header(void)
{
    DSB(TXT_COL) = 0x18;
    DSB(LINE_C) = 0xC3;
    gfx_font_select((s16)DSW(SI_FHDL));
    s16 x = (s16)ps_text_center_x(DSW(SI_NAME), DSW(RX), DSW(RW));
    gfx_text(DSW(SI_NAME), x, (s16)(DSS(RY) + 4));
    x = (s16)ps_text_center_x(DSW(SI_NAME + 2), DSW(RX), DSW(RW));
    gfx_text(DSW(SI_NAME + 2), x, (s16)(DSS(RY) + 16));
    gfx_font_select((s16)DSW(SI_FSML));
    x = (s16)ps_text_center_x(DSW(SI_NAME + 4), DSW(RX), DSW(RW));
    s16 y = (s16)(DSS(RY) + 32);
    gfx_text(DSW(SI_NAME + 4), x, y);
    x = (s16)ps_text_center_x(DSW(SI_NAME + 6), DSW(RX), DSW(RW));
    y = (s16)(y + gfx_font_height((s16)DSW(F_FONT3)));
    gfx_text(DSW(SI_NAME + 6), x, y);
    s16 lx = (s16)(DSS(RX) + 6), ly = (s16)(DSS(RY) + 27);
    gfx_line(lx, ly, (s16)(lx + (DSB(0x4775) ? 271 : 263)), ly);
    y = (s16)(DSS(RY) + 5);
    x = (s16)(DSS(RX) + 6);
    u16 flag = bmp_frame(DSW(SI_FLAGS), (s8)DSB(0x4775));
    gfx_sprite_flip(flag, x, y, 0);
    x = (s16)(DSS(RX) + (DSB(0x4775) ? 238 : 230));
    gfx_sprite_flip(bmp_frame(DSW(SI_FLAGS), (s8)DSB(0x4775)), x, y, 0);
}

/* PS 07ba:01b4 si_draw_cell */
static void si_draw_cell(void)
{
    DSB(LINE_C) = 0xC3;
    s16 x = DSS(RX), y = DSS(RY);
    s16 r = (s16)(x + DSS(RW)), b = (s16)(y + DSS(RH));
    gfx_line(x, y, r, y);
    gfx_line(x, y, x, b);
    gfx_line(r, y, r, b);
    gfx_line(x, b, r, b);
    gfx_line((s16)(x + 0x48), y, (s16)(x + 0x48), b);
}

/* PS 07ba:0230 si_draw_field */
static void si_draw_field(void)
{
    si_draw_cell();
    gfx_font_select((s16)DSW(SI_FSML));
    s16 y = (s16)(DSS(RY) + 2);
    DSB(TXT_COL) = 0xC3;
    u16 lbl = uw(cur_item(), HS_LABEL);
    s16 x = (s16)(DSS(RX) + 0x45 - gfx_text_width(lbl));
    gfx_text(uw(cur_item(), HS_LABEL), x, y);
    DSB(TXT_COL) = 0x18;
    s16 k = (s16)(DSW(CUR_IDX) - (DSB(0x4775) ? 7 : 12));
    gfx_text(DSW((u16)(SI_FIELDS + k * 2)), (s16)(DSS(RX) + 0x4C), y);
}

/* PS 07ba:02ad si_draw_aces */
static void si_draw_aces(void)
{
    si_draw_cell();
    gfx_font_select((s16)DSW(SI_FSML));
    DSB(TXT_COL) = 0xC3;
    u16 lbl = uw(cur_item(), HS_LABEL);
    s16 x = (s16)(DSS(RX) + 0x45 - gfx_text_width(lbl));
    s16 y = (s16)(DSS(RY) + 2);
    gfx_text(uw(cur_item(), HS_LABEL), x, y);
    DSB(TXT_COL) = 0x18;
    x = (s16)(DSS(RX) + 0x4B);
    y = (s16)(DSS(RY) + 3);
    s16 step = gfx_font_height((s16)DSW(F_FONT3));
    for (s16 k = 0; k < 4 && DSB(DSW((u16)(SI_ACES + k * 2))) != 0; k++, y = (s16)(y + step))
        gfx_text(DSW((u16)(SI_ACES + k * 2)), x, y);
}

/* PS 07ba:0351 / 037a / 03a3 si_str_row4/5/7(buf, idx) */
static u16 si_str_row(s16 row, u16 buf, s8 idx)
{
    strcpy_far_src(buf, strings_get(row, idx));
    return buf;
}
static u16 si_str_row4(u16 buf, s8 idx) { return si_str_row(4, buf, idx); }
static u16 si_str_row5(u16 buf, s8 idx) { return si_str_row(5, buf, idx); }
static u16 si_str_row7(u16 buf, s8 idx) { return si_str_row(7, buf, idx); }

/* PS 07ba:03cc si_format(str_id, arg, dst) */
static void si_format(s16 str_id, u16 arg, u16 dst)
{
    u16 fmt = str_get(str_id);
    rtl_sprintf(dst, fmt, arg);
    str_upper(dst);
}

/* PS 07ba:03fb si_load(first) — shell_ui_flight.md §3 */
static void si_load(s8 first)
{
    FarPtr e = si_entry(si_cursor());
    if ((s16)(s8)DSB(SI_LOADED) == (s16)uw(e, 0)) return;
    DSB(SI_LOADED) = ub(e, 0);
    u16 date = uw(si_entry(si_cursor()), 2);
    s16 sq = (s8)DSB(SI_LOADED);
    strcpy_far_src(DSW(SI_NAME), squad_name(sq));
    str_upper(DSW(SI_NAME));
    strcpy_far_src(DSW(SI_NAME + 2), squad_name2((s8)DSB(SI_LOADED)));
    str_upper(DSW(SI_NAME + 2));
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x32);
    si_format(1, si_str_row4(tmp, (s8)squad_role((s8)DSB(SI_LOADED))), DSW(SI_NAME + 4));
    if (first) si_format(2, si_str_row5(tmp, (s8)DSB(0x4776)), DSW(SI_NAME + 6));
    strcpy_far_src(DSW(SI_FIELDS), ac_name(squad_aircraft((s8)DSB(SI_LOADED), date)));
    str_upper(DSW(SI_FIELDS));
    s16 st = squad_station((s8)DSB(SI_LOADED), date);
    stattxt_open();
    strcpy_far_src(DSW(SI_FIELDS + 2), station_name(st));
    str_upper(DSW(SI_FIELDS + 2));
    stattxt_close();
    si_str_row7(DSW(SI_FIELDS + 4), (s8)squad_quality((s8)DSB(SI_LOADED)));
    stk_release(m);
    s8 count = (s8)ace_count();
    s8 k = 0, n = 0;
    if (k < count) {
        do {
            if (ace_valid(k) && ace_status(k, date) == 0 &&
                ace_squadron(k, date) == (s16)(s8)DSB(SI_LOADED) && (s16)ace_has_successor(k) == 0) {
                /* PORT: more than 4 living aces would write past DS:42D4[3] into DS:42DC.. (the
                 * shipped data never has more than 4); the index wraps into the next pointers as
                 * in the original, which stay inside DGROUP. */
                strcpy_far_src(DSW((u16)(SI_ACES + n * 2)), ace_name(k));
                n++;
                str_upper(DSW((u16)(SI_ACES + n * 2)));     /* sic: uppercases the NEXT slot */
            }
            k++;
        } while (k < count);
    }
    for (; n < 4; n++) DSB(DSW((u16)(SI_ACES + n * 2))) = 0;
}

/* PS 07ba:0618 si_layout(mode) */
static void si_layout(s8 mode)
{
    u8 m14, f14, m5, f5;
    if (mode == 0) {
        m14 = 0x74; f14 = 0x82; m5 = 0; f5 = 0x80;
        set_mask(hs_item(SI_TBL, 0), 0x40);
    } else {
        m14 = 0; f14 = 0x80; m5 = 0x74; f5 = 0x82;
        set_mask(hs_item(SI_TBL, 0), 0);
    }
    for (int i = 1; i <= 4; i++) {
        FarPtr it = hs_item(SI_TBL, i);
        uwb(it, HS_MASK, m14);
        uwb(it, HS_FLAGS, f14);
    }
    uwb(hs_item(SI_TBL, 5), HS_MASK, m5);
    uwb(hs_item(SI_TBL, 5), HS_FLAGS, f5);
    hs_default_links(hs_item(SI_TBL, 0), 16);
}

/* PS 07ba:06bd si_side_items */
static void si_side_items(void)
{
    u8 a = DSB(0x4775) ? 0x40 : 0, j = DSB(0x4775) ? 0 : 0x40;
    for (int i = 6; i <= 10; i++) set_mask(hs_item(SI_TBL, i), a);
    for (int i = 11; i <= 15; i++) set_mask(hs_item(SI_TBL, i), j);
}

/* PS 07ba:0729 si_build_list(mode) */
static void si_build_list(s8 mode)
{
    DSW(SI_NALLOC) = (u16)squad_count();
    ds_far_wr(SI_LIST, xfarmalloc((u32)(u16)(DSW(SI_NALLOC) << 2)));
    s16 n = 0;
    if (mode == 0) {
        for (s16 sq = 0; sq < (s16)DSW(SI_NALLOC); sq++) {
            if (!squad_valid(sq)) continue;
            if (!campaign_has_squad((s16)DSW(0x4790), sq)) continue;
            if ((u8)squad_service(sq) != DSB(0x4776)) continue;
            u16 d = squad_next_mission_date(sq, DSW(SI_DATE0), DSW(SI_DATE1));
            if (d == 0) continue;
            FarPtr e = si_entry((u16)n);
            uww(e, 0, (u16)sq);
            uww(e, 2, d);
            n++;
        }
    } else {
        FarPtr e = si_entry(0);
        uww(e, 0, DSW(0x478E));
        uww(e, 2, DSW(0x477F));
        n = 1;
    }
    DSW(SI_NUSED) = (u16)n;
    if (n == 0) ufatal(SI_SEG, 0x289);
    for (; n < (s16)DSW(SI_NALLOC); n++) uww(si_entry((u16)n), 0, 0xFFFF);
}

/* PS 07ba:0822 si_set_buffers(a, b) */
static void si_set_buffers(u16 a, u16 b)
{
    DSW(SI_NAME) = a;
    DSW(SI_NAME + 2) = (u16)(a + 50);
    DSW(SI_NAME + 4) = (u16)(a + 100);
    DSW(SI_NAME + 6) = (u16)(a + 150);
    u16 c = 0;
    for (int i = 0; i < 3; i++, c += 40) DSW((u16)(SI_FIELDS + i * 2)) = (u16)(b + c);
    for (int i = 0; i < 4; i++, c += 40) DSW((u16)(SI_FIELDS + 6 + i * 2)) = (u16)(b + c);
}

/* PS 07ba:0891 si_frame */
static void si_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 2) DSW(HS_RES) = 3;
}

/* PS 07ba:08c2 squadinf(mode) -> u8 — shell_ui_flight.md §3 */
u16 squadinf(u16 mode_w)
{
    s8 mode = (s8)mode_w;
    u8 loop = 1, first = 1;
    u8 result = 0;           /* PORT: uninitialised in the original until a case sets it (every exit sets it) */
    DSB(SI_LOADED) = 0xFF;
    u16 mk = stk_mark();
    u16 b = stk_alloc(0x140);     /* bp-20Ch: 320 bytes */
    u16 a = stk_alloc(0xC8);      /* bp-0CCh: 200 bytes */
    si_set_buffers(a, b);
    if (mode == 0) {
        DSW(SI_DATE1) = campaign_end((s16)DSW(0x4790));
        DSW(SI_DATE0) = campaign_start((s16)DSW(0x4790));
    }
    strtab_load(make_name(0x062D /* "strings_" */, -1, 0x0636 /* "dat" */));
    si_build_list(mode);
    si_set_cursor(0);
    DSW(SI_FLAGS) = bmp_load_planes(make_name(0x063A /* "flags" */, -1, 0x0640 /* "bmp" */));
    si_layout(mode);
    si_side_items();
    DSW(SI_FSML) = font_load(make_name(0x0644 /* "simq_sml" */, -1, 0x064D /* "fnt" */));
    DSW(SI_FHDL) = font_load(make_name(0x0651 /* "simq_hdl" */, -1, 0x065A /* "fnt" */));
    hs_set_table(hs_item(SI_TBL, 0), 16);
    hs_focus(mode ? 5 : 4);
    while (loop) {
        screen_show(DSB(0x4775) ? 0x13 : 0x14, 0xCA, 1);
        si_load((s8)first);
        if (mode == 2 && first) {                             /* the "transfer" text box */
            strtab_free();
            strtab_load(make_name(0x065E /* "misc" */, -1, 0x0663 /* "dat" */));
            tbox_over_screen(strings_get(2, 0));
            strtab_free();
            strtab_load(make_name(0x0667 /* "strings_" */, -1, 0x0670 /* "dat" */));
        }
        if (first) first = 0;
        hs_draw_all();
        menu_run(UFP(si_frame));
        ui_busy();
        switch (DSW(HS_RES)) {
        case 1:                                                /* PREVIOUS */
            si_set_cursor(si_cursor() ? (u16)(si_cursor() - 1) : (u16)(DSW(SI_NUSED) - 1));
            break;
        case 2:                                                /* NEXT */
            si_set_cursor((u16)(si_cursor() + 1));
            if (si_cursor() == DSW(SI_NUSED)) si_set_cursor(0);
            break;
        case 3:                                                /* CANCEL */
            result = 0; loop = 0;
            break;
        case 4: {                                              /* SELECT */
            loop = 0; result = 1;
            FarPtr e = si_entry(si_cursor());
            DSW(0x478E) = uw(e, 0);
            DSW(0x477B) = uw(e, 2);
            DSW(0x477D) = 0xFFFF;
            squad_sched_init((s16)DSW(0x478E), 0x477B, DSW(0x477B));
            DSW(0x478A) = (u16)squad_aircraft((s16)DSW(0x478E), DSW(0x477B));
            DSW(0x478C) = (u16)squad_station((s16)DSW(0x478E), DSW(0x477B));
            break;
        }
        case 5:                                                /* CONTINUE */
            loop = 0; result = 1;
            break;
        }
    }
    strtab_free();
    bmp_free(DSW(SI_FLAGS));
    xfarfree(ds_far(SI_LIST));
    font_free(DSW(SI_FSML));
    font_free(DSW(SI_FHDL));
    stk_release(mk);
    return result;
}

__attribute__((constructor)) static void squadinf_codeptrs(void)
{
    UREG(si_draw_prompt);
    UREG(si_btn_cb);
    UREG(si_draw_header);
    UREG(si_draw_cell);
    UREG(si_draw_field);
    UREG(si_draw_aces);
    UREG(si_frame);
}
