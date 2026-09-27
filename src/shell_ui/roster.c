/* shell_ui part B: roster.c — pilot roster records (PS 0f67, shell_ui_career.md §5), the pilot
 * record screen (PS 135d, §9; screen 12 PRECORD, table 4bf2:0000, 27 items) and the roster screen
 * (PS 13be, §7; screen 50 PARTS/FULL, table 4c18:0000, 13 items).
 * Roster: far array 4b4c:000e + 122·i (count DS:4A18), event-letter blob DS:47CE; current pilot DS:4754. */
#include "shell_ui/sui_b_priv.h"

#define RS_SEG      0x4b4c
#define NROSTER     ((s16)DSW(0x4A18))
#define BLOB        0x47CE
static FarPtr roster(s16 i) { return far_make(SEG(RS_SEG), (u16)(i * 0x7A + 0x0E)); }
static void rcopy(FarPtr dst, FarPtr src) { for (u16 k = 0; k < 0x7A; k++) uwb(dst, k, ub(src, k)); }   /* SCOPY@ 122 */

/* PS 0f67:0001 pilot_store — roster[DS:4774] = DS:4754 */
void pilot_store(void)
{
    rcopy(roster((s8)DSB(0x4774)), ds_ptr(0x4754));
}

/* PS 0f67:0024 pilot_delete(i) — keeps the original's offset bug: every moved pilot gets the deleted
 * pilot's event-list offset (+77h), so the 2nd and later followers point at the wrong place */
static void pilot_delete(s8 idx)
{
    s16 i = idx;
    u16 off = uw(roster(i), 0x77);
    u16 len = (u16)(s16)uc(roster(i), 0x79);
    u16 acc = 0;
    for (s16 j = (s16)(i + 1); j < NROSTER; j++) {
        acc = (u16)(acc + (s16)uc(roster(j), 0x79));
        uww(roster(j), 0x77, uw(roster((s16)(j - 1)), 0x77));
        rcopy(roster((s16)(j - 1)), roster(j));
        uwb(roster((s16)(j - 1)), 0x20, (u8)(ub(roster((s16)(j - 1)), 0x20) - 1));
    }
    if (len != 0) rtl_memmove((u16)(BLOB + off), (u16)(BLOB + off + len), acc);
    DSW(0x4A18) = (u16)(DSW(0x4A18) - 1);
}

/* PS 0f67:00f9 pilot_backup(char *name) — BACKUP CAREER */
void pilot_backup(u16 name_ds)
{
    if (NROSTER >= 13) return;
    s16 s = (s16)DSW(0x47CB);
    for (s16 j = (s8)DSB(0x4774); j < NROSTER; j++) s = (s16)(s + (s16)uc(roster(j), 0x79));
    if ((s16)(s + (s16)(s8)DSB(0x47CD)) > 0x200) ufatal(RS_SEG, 0x62);
    rtl_memmove((u16)(BLOB + s), (u16)(BLOB + DSW(0x47CB)), (u16)(s16)(s8)DSB(0x47CD));
    s8 old = (s8)DSB(0x4774);
    DSB(0x4774) = DSB(0x4A18);
    DSW(0x47CB) = (u16)s;
    rtl_strcpy(0x4764, name_ds);                  /* second text = backup label */
    pilot_store();                                /* the copy */
    rcopy(ds_ptr(0x4754), roster(old));           /* back to the original */
    DSW(0x4A18) = (u16)(DSW(0x4A18) + 1);
    for (s16 i = 0; i < NROSTER; i++)
        if ((s16)uc(roster(i), 0x20) != i) ufatal(RS_SEG, 0x79);
    roster_write();
}

/* PS 0f67:021a pilot_event_seen(c) -> u8 */
u16 pilot_event_seen(u16 c)
{
    u16 p = (u16)(BLOB + DSW(0x47CB));
    u16 n = (u16)(s16)(s8)DSB(0x47CD);
    for (u16 i = 0; n > i; i++)
        if (DSB(p + i) == (u8)c) return 1;
    return 0;
}

/* PS 0f67:0252 pilot_event_add(c) */
void pilot_event_add(u16 c)
{
    u8 j8 = DSB(0x4774);
    u16 acc = 0;
    for (s16 j = j8; j < NROSTER; j8++, j = j8) {
        acc = (u16)(acc + (s16)uc(roster(j), 0x79));
        uww(roster(j), 0x77, (u16)(uw(roster(j), 0x77) + 1));
    }
    if ((u16)(DSW(0x47CB) + acc) >= 0x200) ufatal(RS_SEG, 0x9A);
    u16 p = (u16)(DSW(0x47CB) + BLOB);
    rtl_memmove((u16)(p + 1), p, acc);
    DSB(p) = (u8)c;
    DSB(0x47CD) = (u8)(DSB(0x47CD) + 1);
    pilot_store();                                /* restores the current pilot's own offset */
}

/* PS 0f67:02ed service_side(s) -> s8: 0-2 USA (1), 3-4 Japan (0), 5-7 Britain, 8 China, 9 USSR, 10 NL */
u16 service_side(u16 s)
{
    s8 c = (s8)s, r;
    if (c <= 2) r = 1;
    else if (c <= 4) r = 0;
    else if (c <= 7) r = 2;
    else if (c == 8) r = 3;
    else if (c == 9) r = 4;
    else if (c == 10) r = 5;
    else r = -1;
    return (u16)(s16)r;          /* AL; callers sign-extend it */
}

/* PS 0f67:033b service_available(s) */
u16 service_available(u16 s)
{
    s16 v = (s8)s;
    for (s16 i = 0; i < 11; i++)
        if (v == (s16)DSW((u16)(0x49CE + i * 2))) return 1;
    return 0;
}

/* PS 0f67:036b nation_available(n) */
u16 nation_available(u16 n)
{
    for (s16 i = 0; i < 11; i++) {
        if ((s16)DSW((u16)(0x49CE + i * 2)) == -1) return 0;
        if ((u8)service_side(DSB((u16)(0x49CE + i * 2))) == (u8)n) return 1;
    }
    return 0;
}

/* ================================================================== PS 135d: pilot record */

#define PR_TBL      0x4bf2, 0x0000
#define PR_INSIG    0x4D50
#define PR_MEDALS   0x4D52
#define PR_SVCNAMES 0x4D54      /* 11 × 30 bytes */
#define PR_MEDLOCS  0x4E9E      /* 20 × {s16 y, s16 w} */

/* PS 135d:0009 pr_btn_cb */
static void pr_btn_cb(void)
{
    if (btn_cb()) menu_set_result(1);
}

/* PS 135d:0021 pr_draw_insignia */
static void pr_draw_insignia(void)
{
    tile_fill_rect();
    draw_header_panel();
    u16 f = bmp_frame(DSW(PR_INSIG), (s8)DSB(0x4776));
    s16 dx = (s16)((DSS(RW) >> 1) - (frame_w(f) >> 1) + 3);
    s16 dy = (s16)((DSS(RH) >> 1) - (frame_h(f) >> 1));
    gfx_sprite_flip(f, (s16)(DSS(RX) + dx), (s16)(DSS(RY) + dy), 0);
}

/* PS 135d:0093 pr_draw_nameplate */
static void pr_draw_nameplate(void)
{
    s16 sym = (s16)((s8)DSB(0x4775) + 8);
    u16 r = (u16)((s16)(s8)DSB(0x4777) << 2);
    rtl_sprintf(STRBUF, 0x0E1E /* "%Fs %s" */, (unsigned)DSW(0x57A2 + r), (unsigned)DSW(0x57A4 + r), (unsigned)0x4754);
    str_upper(STRBUF);
    uww(cur_item(), HS_LABEL, STRBUF);
    draw_header_panel();
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), sym), (s16)(DSS(RX) + 9), (s16)(DSS(RY) + 1), 0);
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), sym), (s16)(DSS(RX) + DSS(RW) - 0x16), (s16)(DSS(RY) + 1), 0);
}

/* PS 135d:012c pr_draw_label */
static void pr_draw_label(void)
{
    u16 s = uw(cur_item(), HS_LABEL);
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    text_engrave(s, 0xC7, 0xB1, 0xC0, DSW(RX), (u16)(DSW(RY) + 1));
}

/* PS 135d:0170 pr_draw_field */
static void pr_draw_field(void)
{
    u16 s = uw(cur_item(), HS_LABEL);
    if (s == 0) return;
    draw_inset_box2();
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    DSB(TXT_COL) = 0xCC;
    gfx_text(s, (s16)(DSS(RX) + 5), (s16)(DSS(RY) + 3));
}

static void pr_itoa_cap(u16 v) { rtl_itoa((s16)v < 999 ? (s16)v : 999, STRBUF, 10); }

/* PS 135d:01bb pr_draw_value — switch 135d:02e9 on the item index */
static void pr_draw_value(void)
{
    switch (DSW(CUR_IDX)) {
    case 2:  rtl_ltoa((s32)DSL(0x47B9), STRBUF, 10); break;                /* score */
    case 4:  pr_itoa_cap(DSW(0x4784)); break;                              /* missions flown */
    case 6:  date_text_short(DSW(0x477F), STRBUF); break;                  /* date */
    case 8:  rtl_strcpy(STRBUF, (u16)(PR_SVCNAMES + (s16)(s8)DSB(0x4776) * 0x1E)); break;
    case 10: strcpy_far_src(STRBUF, squad_name((s16)DSW(0x478E))); break;
    case 12: pr_itoa_cap(DSW(0x4788)); break;                              /* campaigns completed */
    case 14: pr_itoa_cap(DSW(0x4786)); break;                              /* successful missions */
    case 16: pr_itoa_cap(DSW(0x47BF)); break;                              /* air victories */
    case 18: pr_itoa_cap((u16)(DSW(0x47C1) + DSW(0x47C3))); break;         /* successful strikes */
    case 20: rtl_itoa((s16)DSW(0x47C1), STRBUF, 10); break;                /* ships sunk (no cap) */
    default: break;                         /* odd items are labels: STRBUF as left */
    }
    str_upper(STRBUF);
    uww(cur_item(), HS_LABEL, STRBUF);
    pr_draw_field();
}

/* PS 135d:030f pr_draw_medals */
static void pr_draw_medals(void)
{
    draw_popup_box();
    uline(0xC7, 0x23, 0x7B, 0x75, 0x7B);
    uline(0xC7, 0x34, 0x7D, 0x75, 0x7D);
    uline(0xC7, 0xC4, 0x7B, 0x119, 0x7B);
    uline(0xC7, 0xC4, 0x7D, 0x108, 0x7D);
    uline(0xB1, 0x23, 0x7C, 0x75, 0x7C);
    uline(0xB1, 0x34, 0x7E, 0x75, 0x7E);
    uline(0xB1, 0xC4, 0x7C, 0x119, 0x7C);
    uline(0xB1, 0xC4, 0x7E, 0x108, 0x7E);
    draw_rivets((u16)(DSS(RX) + 3), (u16)(DSS(RY) + 2), 1);
    draw_rivets((u16)(DSS(RX) + 3), (u16)(DSS(RY) + DSS(RH) - 6), 1);
    draw_rivets((u16)(DSS(RX) + DSS(RW) - 6), (u16)(DSS(RY) + 2), 1);
    draw_rivets((u16)(DSS(RX) + DSS(RW) - 6), (u16)(DSS(RY) + DSS(RH) - 6), 1);
    s16 x = (s16)(DSS(RX) + 8);
    for (s16 m = 0; m < 20; m++) {
        if (DSB((u16)(0x4795 + m)) == 0) continue;
        gfx_sprite_flip(bmp_frame(DSW(PR_MEDALS), m), x, (s16)DSW((u16)(PR_MEDLOCS + m * 4)), 0);
        x = (s16)(x + (s16)DSW((u16)(PR_MEDLOCS + 2 + m * 4)) + 2);
    }
}

/* PS 135d:04a6 pr_draw_bg — sic: the mission header's service and date, not the pilot's */
static void pr_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for((u16)(s16)(s8)DSB(0x62C5), DSW(0x6277), str_get(0x0D));
}

/* PS 135d:04cb pr_frame */
static void pr_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
}

/* PS 135d:04ec pilot_record — callers: flight deck, roster VIEW, best careers */
void pilot_record(void)
{
    DSW(PR_MEDALS) = bmp_load_planes(make_name(0x0E25 /* "medals" */, -1, 0x0E2C /* "bmp" */));
    DSW(PR_INSIG) = bmp_load_planes(make_name(0x0E30 /* "insignia" */, -1, 0x0E39 /* "bmp" */));
    file_read_n(PR_MEDLOCS, 4, 0x14, make_name(0x0E3D /* "medlocs" */, -1, 0x0E45 /* "dat" */));
    strtab_load(make_name(0x0E49 /* "sftbtns" */, 0, 0x0E51 /* "dat" */));
    for (s16 k = 0; k < 11; k++) strcpy_far_src((u16)(PR_SVCNAMES + k * 0x1E), strings_get(0, (s16)(k + 0x85)));
    strtab_free();
    screen_open(0x0C, UFP(pr_draw_bg), 0xDA, 1);
    hs_set_table(fdat(PR_TBL), 0x1B);
    hs_draw_all();
    menu_run(UFP(pr_frame));
    ui_busy();
    bmp_free(DSW(PR_INSIG));
    bmp_free(DSW(PR_MEDALS));
}

/* PS 135d:0600 pr_init — dialogs_init */
void pr_init(void)
{
    hs_default_links(fdat(PR_TBL), 0x1B);
}

/* ================================================================== PS 13be: roster screen */

#define RO_SEG      0x4c18
#define RO_TBL      RO_SEG, 0x0000
#define RO_TITLES   0x4EEE      /* near: 2 title string ids */
#define RO_PRESSED  0x4EF0
#define RO_KEEP     0x4EF1      /* s8: the pilot in use (FFh none) */
#define RO_REOPEN   0x0E56

/* PS 13be:0009 ro_btn_cb */
static void ro_btn_cb(void)
{
    if (btn_cb()) {
        DSB(RO_PRESSED) = DSB(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 13be:0027 ro_draw_window */
static void ro_draw_window(void)
{
    tile_fill_rect();
    draw_bevel_box();
    draw_rivets((u16)(DSS(RX) + 4), (u16)(DSS(RY) + 4), 0x0E);
    draw_rivets((u16)(DSS(RX) + DSS(RW) - 6), (u16)(DSS(RY) + 4), 0x0E);
}

/* PS 13be:006e ro_draw_scroll */
static void ro_draw_scroll(void)
{
    draw_bevel_box();
    DSB(LINE_C) = 0xC0;
    gfx_rect((s16)(DSS(RX) + 4), (s16)(DSS(RY) + 0x19), 0x12, 0x23);
}

/* PS 13be:009b ro_draw_title — two lines str_get(DS:4EEE[k]) */
static void ro_draw_title(void)
{
    gfx_font_select((s16)DSW(F_FONT4));
    DSB(TXT_FLG) = 1;
    s16 y = (s16)(DSS(RY) + 5);
    for (s16 k = 0; k < 2; k++) {
        u16 s = str_get((s16)DSW((u16)(DSW(RO_TITLES) + k * 2)));
        if (s != 0) {
            u16 tx = ps_text_center_x(s, DSW(RX), DSW(RW));
            text_engrave(s, 0xC7, 0xB1, 0xC0, tx, (u16)y);
            y = (s16)(y + 0x0F);
        }
        gfx_font_select((s16)DSW(F_FONT3));
    }
}

/* PS 13be:011c ro_draw_header */
static void ro_draw_header(void)
{
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    u16 s = str_get((s16)DSW((u16)(0x0E57 + uw(cur_item(), HS_USER) * 2)));
    text_engrave(s, 0xC7, 0xB1, 0xC0, DSW(RX), DSW(RY));
}

/* PS 13be:0161 ro_draw_row(i, y) */
static void ro_draw_row(s16 i, s16 y)
{
    FarPtr r = roster(i);
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), (s16)uc(r, 0x21) + 0x10), (s16)(DSS(RX) + 5), y, 0);
    strcpy_far_src(STRBUF, strings_get(3, uc(roster(i), 0x23)));     /* rank abbreviation */
    rtl_strcat(STRBUF, 0x0E91 /* " " */);
    strcpy_far_src((u16)(STRBUF + rtl_strlen(STRBUF)), roster(i));
    gfx_text(STRBUF, (s16)(DSS(RX) + 0x0F), y);
    date_text_short(uw(roster(i), 0x2B), STRBUF);
    gfx_text(STRBUF, (s16)(DSS(RX) + 0x78), y);
    if (ub(roster(i), 0x24) == 0) {
        strcpy_far_src(STRBUF, far_add(roster(i), 0x10));            /* backup label */
        gfx_text(STRBUF, (s16)(DSS(RX) + 0xAF), y);
    } else {
        strcpy_far_src(STRBUF, strings_get(1, uc(roster(i), 0x24)));  /* Retired / Discharged / Deceased */
        gfx_text(STRBUF, (s16)(DSS(RX) + 0xC8), y);
    }
}

/* PS 13be:02c5 ro_subst -> "<rank abbr> <name>" of the selected pilot (request-box substitution) */
static FarPtr ro_subst(s8 c)
{
    (void)c;
    strcpy_far_src(STRBUF, strings_get(3, uc(roster((s16)lbox_selected()), 0x23)));
    rtl_strcat(STRBUF, 0x0E93 /* " " */);
    strcpy_far_src((u16)(STRBUF + rtl_strlen(STRBUF)), roster((s16)lbox_selected()));
    return ds_ptr(STRBUF);
}

/* PS 13be:0339 ro_frame */
static void ro_frame(void)
{
    FarPtr subst = UFP(ro_subst);
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 1) {
        s8 st;
        switch ((s8)DSB(RO_PRESSED)) {
        case 10:                                                 /* DELETE */
            if ((s16)(s8)DSB(RO_KEEP) == (s16)lbox_selected()) {  /* the pilot in use */
                req_ok(subst.off, subst.seg, 0x0E, 0xFFFF);
                menu_set_result(0);
            } else if ((s8)req_yes_no(subst.off, subst.seg, 0x0F, 3, 4) == 0) {
                menu_set_result(0);
            }
            break;
        case 12:                                                 /* ACCEPT */
            rcopy(ds_ptr(0x4754), roster((s16)lbox_selected()));
            if (DSB(0x4778) == 1) {                              /* retired */
                if ((u8)cset_scan((u16)(s16)(s8)DSB(0x4776), DSW(0x477F), 0xFF)) {
                    if ((u8)req_yes_no_wide(subst.off, subst.seg, 0x0B, 0, 1)) {
                        st = (s8)cset_choose(0);
                        if (st != -1) {
                            DSB(0x4778) = 0;
                            DSB(0x4779) = (u8)st;
                            DSW(0x4790) = 0xFFFF;
                            best_career_remove();
                        } else {
                            st = (s8)0xFE;
                            DSB(RO_REOPEN) = 1;                   /* (the accept still goes through) */
                        }
                    } else {
                        st = (s8)0xFE;
                        menu_set_result(0);
                    }
                } else {
                    st = (s8)0xFE;
                    req_ok(subst.off, subst.seg, 0x0C, 8);
                    menu_set_result(0);
                }
            } else if (DSB(0x4778) == 2 || DSB(0x4778) == 3) {   /* discharged / deceased */
                st = -1;
            } else {
                st = (s8)0xFE;
            }
            if (st == -1) {
                req_ok(subst.off, subst.seg, 0x0D, 8);
                menu_set_result(0);
            }
            break;
        default:
            break;
        }
    }
    if (menu_get_result() == 2) DSB(RO_PRESSED) = 9;
}

/* PS 13be:04e0 ro_draw_bg */
static void ro_draw_bg(void)
{
    screen_clear_panel();
    u16 t = str_get(0x0B);                                       /* "CAREER ROSTER" */
    u16 date = uw(roster((s16)lbox_selected()), 0x2B);
    u8 svc = ub(roster((s16)lbox_selected()), 0x22);
    draw_title_bar_for((u16)(s16)(s8)svc, date, t);
}

static void ro_open_list(void)
{
    lbox_setup(0, 0, DSW(0x4A18), 0x0F, fnull(), UFP(ro_draw_row));
    lbox_bind(fdat(RO_TBL), 5, 7);
}

static void ro_open_screen(void)
{
    screen_open(0x32, UFP(ro_draw_bg), 0x396, 1);
}

/* PS 13be:052a roster_run(delete_once) -> u8 */
static u8 roster_run(s8 delete_once)
{
    ro_open_list();
    ro_open_screen();
    for (;;) {
        hs_set_table(fdat(RO_TBL), 0x0D);
        hs_focus((u16)(s16)(s8)DSB(RO_PRESSED));
        hs_draw_all();
        menu_run(UFP(ro_frame));
        ui_busy();
        switch ((s8)DSB(RO_PRESSED)) {
        case 9:                                                  /* CANCEL */
            if (DSB(RO_REOPEN) == 0) return 0;
            ro_open_list();
            ro_open_screen();
            DSB(RO_REOPEN) = 0;
            break;
        case 10: {                                               /* DELETE */
            pilot_delete((s8)lbox_selected());
            if (DSB(RO_KEEP) != 0xFF && (s16)(s8)DSB(RO_KEEP) > (s16)lbox_selected())
                DSB(RO_KEEP) = (u8)(DSB(RO_KEEP) - 1);
            if (DSW(0x4A18) == 0) return 0;
            ro_open_list();
            set_mask(hs_item(RO_TBL, 5), 3);
            pages_bg_to_back();
            FarPtr b = hs_item(RO_TBL, 10);
            gfx_copy_rect(us(b, HS_X), 0xAD, us(b, HS_W), 0x1B);
            if (delete_once) return 1;
            break;
        }
        case 11:                                                 /* VIEW */
            rcopy(ds_ptr(0x4754), roster((s16)lbox_selected()));
            if ((s8)DSB(0x4774) >= 0) {
                strtab_free();
                pilot_record();
                strtab_load(make_name(0x0E95 /* "strings_" */, -1, 0x0E9E /* "dat" */));
                ro_open_screen();
            }
            break;
        case 12:                                                 /* ACCEPT */
            module_set_active(DSB(0x4779));
            pilot_store();
            return 1;
        default:
            break;
        }
    }
}

/* PS 13be:075a ro_links_manage */
static void ro_links_manage(void)
{
    for (s16 k = 0; k < 4; k++) {
        uwb(hs_item(RO_TBL, 7), (u16)(HS_LINKS + k), DSB((u16)(0x0E5D + k * 2)));
        uwb(hs_item(RO_TBL, 8), (u16)(HS_LINKS + k), DSB((u16)(0x0E65 + k * 2)));
        uwb(hs_item(RO_TBL, 9), (u16)(HS_LINKS + k), DSB((u16)(0x0E6D + k * 2)));
        uwb(hs_item(RO_TBL, 11), (u16)(HS_LINKS + k), DSB((u16)(0x0E75 + k * 2)));
        uwb(hs_item(RO_TBL, 10), (u16)(HS_LINKS + k), DSB((u16)(0x0E7D + k * 2)));
    }
}

/* PS 13be:07c3 roster_manage(keep) -> u8 — "THE ROSTER IS FULL" */
u16 roster_manage(u16 keep)
{
    strtab_load(make_name(0x0EA2 /* "strings_" */, -1, 0x0EAB /* "dat" */));
    DSB(RO_KEEP) = (u8)keep ? DSB(0x4774) : 0xFF;
    DSW(RO_TITLES) = 0x0E85;                                     /* {0, 1} */
    uww(hs_item(RO_TBL, 9), HS_X, 0x10);
    uww(hs_item(RO_TBL, 10), HS_X, 0xE4);
    uww(hs_item(RO_TBL, 11), HS_X, 0x7A);
    uwb(hs_item(RO_TBL, 12), HS_MASK, 0);
    uwb(hs_item(RO_TBL, 12), HS_FLAGS, 0);
    DSB(RO_PRESSED) = 10;
    ro_links_manage();
    u8 r = roster_run(1);
    if ((u8)keep) rcopy(ds_ptr(0x4754), roster((s8)DSB(RO_KEEP)));   /* its index may have moved */
    strtab_free();
    return r;
}

/* PS 13be:086e roster_pick -> u8 — "CONTINUE A CAREER..." */
u16 roster_pick(void)
{
    squad_open();
    strtab_load(make_name(0x0EAF /* "strings_" */, -1, 0x0EB8 /* "dat" */));
    DSB(RO_KEEP) = 0xFF;
    DSW(RO_TITLES) = 0x0E89;                                     /* {2, 3} */
    uww(hs_item(RO_TBL, 9), HS_X, 7);
    uww(hs_item(RO_TBL, 10), HS_X, 0x55);
    uww(hs_item(RO_TBL, 11), HS_X, 0xA2);
    uwb(hs_item(RO_TBL, 12), HS_MASK, 0x74);
    uwb(hs_item(RO_TBL, 12), HS_FLAGS, 0x82);
    DSB(RO_PRESSED) = 12;
    hs_default_links(fdat(RO_TBL), 0x0D);
    u8 r = roster_run(0);
    strtab_free();
    squad_close();
    return r;
}

__attribute__((constructor)) static void roster_codeptrs(void)
{
    UREG(pr_btn_cb);
    UREG(pr_draw_insignia);
    UREG(pr_draw_nameplate);
    UREG(pr_draw_label);
    UREG(pr_draw_field);
    UREG(pr_draw_value);
    UREG(pr_draw_medals);
    UREG(pr_draw_bg);
    UREG(pr_frame);
    UREG(ro_btn_cb);
    UREG(ro_draw_window);
    UREG(ro_draw_scroll);
    UREG(ro_draw_title);
    UREG(ro_draw_header);
    UREG(ro_draw_row);
    UREG(ro_subst);
    UREG(ro_frame);
    UREG(ro_draw_bg);
}
