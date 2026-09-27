/* shell_ui part B: flymiss2.c (PS 100e) — single-mission set-up paragraph (shell_ui_flight.md §5).
 * Screens 0–9 MT_*, hot-spot table 4bbe:0011 (13 items; a pristine copy of items 1–10 follows at
 * 4bbe:012f + 22i). The mission is generated at once and shown as a paragraph with inline buttons. */
#include "shell_ui/sui_b_priv.h"

#define F2_SEG      0x4bbe
#define F2_TBL      F2_SEG, 0x0011
#define F2_SPARE    F2_SEG, 0x012F      /* spare[i] = 0x12F + 22i */

#define F2_STRS     0x4A28      /* near char *[4]: strings given to flow_text_buttons */
#define F2_VAL      0x4A30      /* s16[11]: value of parameter kind 1..10 */
#define F2_ROW0     0x4A46      /* char far *[53]: STRINGS_.DAT row 0 */
#define F2_LCNT2    0x4B1E      /* s16: aircraft list 2 count */
#define F2_LCNT1    0x4B20
#define F2_LCNT0    0x4B22
#define F2_TGT      0x4B24      /* s8: target class (types 8, 9) */
#define F2_ECODES   0x4B25      /* u8[4]: enemy pilot codes */
#define F2_OCODES   0x4B29      /* u8[4]: own pilot codes */
#define F2_SLOT     0x4B2D      /* s8: roster slot being edited (FFh none) */
#define F2_SIDE     0x4B2E      /* u8: side of that slot */
#define F2_TYPE     0x4B2F      /* s8: mission type */
#define F2_SVC      0x4B30      /* s8: service */
#define F2_KIND     0x4B31      /* s8[11]: kind of inline button i */
#define F2_OLDALT   0x4B3C      /* s8 */
#define F2_TRSLOT   0x4B3D      /* s8: training slot */
#define F2_NBTN     0x4B3E      /* s8: number of inline buttons */
#define F2_PRESSED  0x4B3F      /* u8: CANCEL (11) / START (12) */
#define F2_SKILLCHG 0x4B1A
#define F2_ALTOK    0x4B1B
#define F2_ROSTER   0x4B1C
#define F2_RELINK   0x4B1D
#define F2_CHANGED  0x0B25
#define F2_TRAIN    0x0AA1      /* s16[7][11] training rows per service */

static u16 f2_val(s16 kind) { return (u16)(F2_VAL + kind * 2); }
static s16 f2_v(s16 kind) { return (s16)DSW(f2_val(kind)); }
/* the own flight 62C5 and its waypoint list */
static u16 own_wp(void) { return DSW(0x62E7); }
static u16 ene_wp(void) { return DSW(0x6377); }
static void dcopy(u16 d, u16 s, u16 n) { for (u16 k = 0; k < n; k++) DSB((u16)(d + k)) = DSB((u16)(s + k)); } /* SCOPY@ */
static FarPtr dsfar(u16 o) { return far_make(cur_dgroup, o); }

/* PS 100e:0007 fm2_training_slot(k) -> i */
static s16 fm2_training_slot(s16 k)
{
    s16 si = 0, col = (s16)((s8)DSB(F2_SVC) * 2);
    while ((s16)DSW((u16)(F2_TRAIN + si * 22 + col)) == -1) si++;
    for (s16 c = 0; c < k; c++) {
        do si++; while ((s16)DSW((u16)(F2_TRAIN + si * 22 + col)) == -1);
    }
    if (si > 6) ufatal(F2_SEG, 0xC8);
    return si;
}

/* PS 100e:007e fm2_btn_cb — CANCEL / START */
static void fm2_btn_cb(void)
{
    if (btn_cb()) {
        DSB(F2_PRESSED) = DSB(CUR_IDX);
        menu_set_result(DSW(CUR_IDX) == 12 ? 1 : 2);
    }
}

/* PS 100e:00a8 fm2_on_ok(sel) — chooser callback */
static void fm2_on_ok(s16 sel)
{
    if ((s8)DSB(F2_SLOT) > -1 && DSB(F2_SIDE) == 0) {
        acelist_select(0);
        DSB((u16)(F2_OCODES + (s8)DSB(F2_SLOT))) = (u8)acelist_code(sel);
    } else if ((s8)DSB(F2_SLOT) > -1 && DSB(F2_SIDE) == 1) {
        acelist_select(1);
        DSB((u16)(F2_ECODES + (s8)DSB(F2_SLOT))) = (u8)acelist_code(sel);
    } else {
        /* quirk (L): 5FDE is the index inside the roster dialog when called from there */
        DSW(f2_val((s8)DSB((u16)(F2_KIND + DSW(CUR_IDX))))) = (u16)sel;
    }
    DSB(F2_CHANGED) = 1;
}

/* PS 100e:0120 fm2_roster_cb(slot) — roster dialog callback */
static void fm2_roster_cb(s16 slot)
{
    if (slot >= 0) {
        DSB(F2_SLOT) = (u8)slot;
        pick_list2(str_get(13) /* "SELECT PILOT:" */, UFP(fm2_on_ok), acelist_enabled_count());
    } else {
        DSB(F2_SLOT) = 0xFF;
        uww(cur_item(), HS_USER, 4);
        /* quirk (L): the current value is read from DS:4A30[DS:4B3F] */
        choose_run(UFP(fm2_on_ok), DSW(f2_val((s8)DSB(F2_PRESSED))), 0x5D);
    }
}

/* PS 100e:017f fm2_prepare_acelist(side) -> sum of the skill codes */
static s16 fm2_prepare_acelist(s8 side)
{
    s8 svc = side == 0 ? (s8)DSB(F2_SVC) : (s8)DSB(0x6355);
    u16 codes = side == 0 ? F2_OCODES : F2_ECODES;
    u16 f = (u16)(0x62C5 + side * 0x90);
    acelist_build(svc, side, 1, (s8)(side == 0));
    s16 n = side == 0 ? (s16)(DSW((u16)(f + 0x1A)) - 1) : (s16)DSW((u16)(f + 0x1A));
    s16 sum = 0;
    for (s16 i = 0; i < n; i++) {
        s8 c = (s8)DSB((u16)(codes + i));
        if (c >= 4) acelist_disable(c, side);
        else sum = (s16)(sum + c);
    }
    return sum;
}

/* PS 100e:0225 fm2_edit(kind) — opens the chooser of an inline button */
static void fm2_edit(s16 kind)
{
    DSB(F2_SLOT) = 0xFF;
    s16 first;
    switch (kind) {
    case 1:
    case 9: {
        u8 side = kind == 9;
        DSB(F2_SIDE) = side;
        acelist_select((s8)side);
        do {
            uww(cur_item(), HS_USER, (u16)(f2_v(kind) + 1));
            DSB(F2_ROSTER) = (u8)flt_roster(UFP(fm2_roster_cb), side ? F2_ECODES : F2_OCODES, side);
            fm2_prepare_acelist((s8)side);
        } while (DSB(F2_ROSTER));
        return;
    }
    case 2:
        pick_aircraft(str_get(14) /* "SELECT AIRCRAFT:" */, UFP(fm2_on_ok), (s16)DSW(F2_LCNT0), 0,
                      (s8)(DSB(F2_TYPE) != 0), f2_v(kind));
        return;
    case 8:
        pick_aircraft(str_get(14), UFP(fm2_on_ok), (s16)DSW(F2_LCNT2), 2, (s8)(DSB(F2_TYPE) != 0), f2_v(kind));
        return;
    case 10:
        pick_aircraft(str_get(14), UFP(fm2_on_ok), (s16)DSW(F2_LCNT1), 1, (s8)(DSB(F2_TYPE) != 0), f2_v(kind));
        return;
    case 3:
        uww(cur_item(), HS_USER, 4);
        first = 0xAD;
        break;
    case 4:
        DSB(F2_OLDALT) = (u8)f2_v(kind);
        uww(cur_item(), HS_USER, 5);
        first = 4;
        break;
    case 5:
        uww(cur_item(), HS_USER, 4);
        first = 9;
        break;
    case 6: {
        int b = DSB(F2_SVC) == 2 || DSB(F2_SVC) == 4;
        uww(cur_item(), HS_USER, b ? 2 : 5);
        first = b ? 0x27 : 0x22;
        break;
    }
    case 7:
        uww(cur_item(), HS_USER, 3);
        first = DSB(F2_TYPE) == 0 ? 0x1C : 0x1F;
        break;
    default:
        return;
    }
    choose_run(UFP(fm2_on_ok), DSW(f2_val(kind)), (u16)first);
}

/* label = STRINGS row 0 entry copied into the inline-button buffer */
static void fm2_row_label(FarPtr it, s16 row)
{
    strcpy_far_src(LBLBUF, strings_get(0, row));
    uww(it, HS_LABEL, LBLBUF);
}

/* PS 100e:0415 fm2_slot_label(kind, item far *it, reg) -> 0 */
static s16 fm2_slot_label(s16 kind, FarPtr it, u8 reg)
{
    switch (kind) {
    case 1:
    case 9:
        uww(it, HS_LABEL, str_get(15) /* "flight" */);
        break;
    case 2:
    case 8:
    case 10:
        acl_select(kind == 2 ? 0 : kind == 8 ? 2 : 1, (s8)(DSB(F2_TYPE) != 0));
        uww(it, HS_LABEL, acl_name(f2_v(kind)).off);
        break;
    case 3:
        fm2_row_label(it, (s16)(f2_v(kind) + 0xAD));
        DSB(F2_SKILLCHG) = 1;
        break;
    case 4: {
        /* quirk (L): the enemy skill value DS:4A36 is used as the aircraft-list index */
        s16 c = ac_alt_band(acl_id((s16)DSW(0x4A36)));
        if (c < f2_v(kind)) DSW(f2_val(kind)) = (u16)(s16)(s8)DSB(F2_OLDALT);
        else DSB(F2_ALTOK) = 1;
        fm2_row_label(it, (s16)(f2_v(kind) + 4));
        break;
    }
    case 5:
        fm2_row_label(it, (s16)(f2_v(kind) + 9));
        break;
    case 6:
        fm2_row_label(it, (s16)((DSB(F2_SVC) == 2 || DSB(F2_SVC) == 4 ? 0x27 : 0x22) + f2_v(kind)));
        break;
    case 7:
        fm2_row_label(it, (s16)(f2_v(kind) + (DSB(F2_TYPE) == 0 ? 0x1C : 0x1F)));
        break;
    default:
        break;
    }
    if (reg) {
        DSB(F2_NBTN) = (u8)(DSB(F2_NBTN) + 1);
        DSB((u16)(F2_KIND + (s8)DSB(F2_NBTN))) = (u8)kind;
    }
    return 0;
}

/* PS 100e:05c6 fm2_subst(c, item far *it) */
static FarPtr fm2_subst(s8 c, FarPtr it)
{
    s8 side = (s8)service_side(DSB(F2_SVC));
    switch (c) {
    case 'r': return strpool_copy(strings_get(0, (s16)((s8)DSB(F2_TGT) + 0x0D)));
    case 's': return strpool_copy(strings_get(0, (s16)((s8)DSB(F2_TGT) + 0x10)));
    case 't': return strpool_copy(strings_get(0, (s16)DSW(side ? 0x0A9D : 0x0A9F)));
    default:
        fm2_slot_label((s16)(c - 0x60), it, 1);
        return fnull();                                  /* AX = 0: an inline button */
    }
}

/* far copy of STRINGS row 0 entry i into the string pool (near) */
static u16 row0_copy(s16 i) { return strpool_copy(ds_far((u16)(F2_ROW0 + i * 4))).off; }

/* PS 100e:0648 fm2_draw_text — item 0: the paragraph */
static void fm2_draw_text(void)
{
    tile_fill_rect();
    ps_draw_frame();
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    s16 yoff = 8;
    if (DSB(F2_TYPE) == 0) {                             /* dogfight an ace: his own blurb first */
        rect_save();
        DSW(RX) = (u16)(DSW(RX) + 0x14);
        DSW(RY) = (u16)(DSW(RY) + 6);
        DSW(RW) = (u16)(DSW(RW) - 0x23);
        FarPtr t = acetxt_duel_text((s16)DSW(0x6363));
        flow_setup((u16)-3, 7, 1);
        flow_text(t, 1, fnull());
        ps_rect_restore();
        yoff = 0x17;
    }
    DSB(F2_NBTN) = 0;
    DSB(F2_RELINK) = 1;
    DSW(F2_STRS) = row0_copy((s8)DSB(F2_TYPE));
    DSW(F2_STRS + 2) = 0;
    if (DSB(F2_TYPE) == 5) {                             /* training */
        s8 slot = (s8)fm2_training_slot((s16)DSW(0x4A3C));
        DSB(F2_TRSLOT) = (u8)slot;
        DSB(0x4B89) = DSB(F2_TRSLOT) == 5 ? 4 : DSB(F2_TRSLOT);
        s8 r = (s8)DSB((u16)(F2_TRAIN + slot * 22 + (s8)DSB(F2_SVC) * 2));
        DSW(F2_STRS + 2) = row0_copy(r);
        DSW(F2_STRS + 4) = row0_copy((s8)(slot + 12));
        DSW(F2_STRS + 6) = 0;
    }
    ui_flow_text_buttons(hs_item(F2_TBL, 0), 1, UFP(fm2_subst), F2_STRS, (s16)(DSS(RX) + 0x14),
                         (s16)(DSS(RY) + yoff), (s16)(DSS(RX) + DSS(RW) - 0x14));
    draw_bottom_bar();
}

/* PS 100e:07c0 fm2_relink — re-orders the inline buttons into text order and links them */
static void fm2_relink(void)
{
    for (s16 i = 1; i <= (s8)DSB(F2_NBTN); i++) {
        FarPtr d = hs_item(F2_SPARE, i), s = hs_item(F2_TBL, (s8)DSB((u16)(F2_KIND + i)));
        for (u16 k = 0; k < 22; k++) uwb(d, k, ub(s, k));
    }
    for (s16 i = 1; i <= 10; i++) {
        FarPtr d = hs_item(F2_TBL, i), s = hs_item(F2_SPARE, i);
        for (u16 k = 0; k < 22; k++) uwb(d, k, ub(s, k));
        uwb(d, 0x14, (u8)(i - 1));
        uwb(d, 0x12, (u8)(i - 1));
        if (i - 1 != 0) {
            FarPtr p = hs_item(F2_TBL, i - 1);
            uwb(p, 0x15, (u8)i);
            uwb(p, 0x13, (u8)i);
        }
    }
    uwb(hs_item(F2_TBL, 1), 0x14, 12);
    uwb(hs_item(F2_TBL, 1), 0x12, 12);
    FarPtr last = hs_item(F2_TBL, (s8)DSB(F2_NBTN));
    uwb(last, 0x15, 11);
    uwb(last, 0x13, 11);
    /* the spec says item 12; the code writes item 11 (4bbe:0103) */
    uwb(hs_item(F2_TBL, 11), 0x14, DSB(F2_NBTN));
    uwb(hs_item(F2_TBL, 11), 0x12, DSB(F2_NBTN));
}

/* PS 100e:08d4 fm2_slot_cb — items 1–10 (the inline buttons) */
static void fm2_slot_cb(void)
{
    if ((s8)DSB((u16)(F2_KIND + DSW(CUR_IDX))) <= -1) return;
    if (DSB(F2_RELINK)) {
        fm2_relink();
        FarPtr it = cur_item();
        DSW(RX) = uw(it, HS_X);
        DSW(RY) = (u16)(uw(hs_item(F2_TBL, 0), HS_Y) + uw(it, HS_Y));
        DSW(RW) = uw(it, HS_W);
        DSW(RH) = uw(it, HS_H);
        DSB(F2_RELINK) = 0;
    }
    fm2_slot_label((s8)DSB((u16)(F2_KIND + DSW(CUR_IDX))), cur_item(), 0);
    set_mask(cur_item(), 0x74);
    if (choose_entry_cb()) {
        fm2_edit((s8)DSB((u16)(F2_KIND + DSW(CUR_IDX))));
        hs_restart();
    }
}

/* PS 100e:0967 fm2_frame */
static void fm2_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    if (menu_get_result() == 2) DSB(F2_PRESSED) = 0x0B;
}

/* PS 100e:0997 fm2_codes(flight *f, u8 *dst) */
static void fm2_codes(u16 f, u16 dst)
{
    for (s16 i = 0; (s16)DSW((u16)(f + 0x1A)) > i; i++) {
        u8 c = DSB((u16)(f + 0x0A + i));
        DSB((u16)(dst + i)) = c == 0xFF ? (u8)(DSB((u16)(f + 0x0E + i * 2)) + 4) : c;
    }
}

/* PS 100e:09d0 fm2_codes_all */
static void fm2_codes_all(void)
{
    fm2_codes(0x62C5, F2_OCODES);
    fm2_codes(0x6355, F2_ECODES);
}

/* index of aircraft id in the current list, or leaves *val unchanged */
static void fm2_find_ac(s8 list, u16 cnt, u16 id, u16 val)
{
    acl_select(list, 0);
    DSW(cnt) = (u16)acl_count();
    for (s16 i = 0; i < (s16)DSW(cnt); i++)
        if (acl_id(i) == (s16)DSW(id)) { DSW(val) = (u16)i; break; }
}

/* PS 100e:09f1 fm2_init(arg) */
static void fm2_init(u8 arg)
{
    strtab_load(make_name(0x0B2E /* "strings_" */, -1, 0x0B37 /* "dat" */));
    strings_dup_far(F2_ROW0, 0x35, 0);
    strtab_free();
    strtab_load(make_name(0x0B3B /* "sftbtns" */, 0, 0x0B43 /* "dat" */));   /* stays until fm2_apply */
    for (s16 i = 1; i <= 10; i++) {
        DSW(f2_val(i)) = 0xFFFF;
        DSB((u16)(F2_KIND + i)) = 0xFF;
        FarPtr d = hs_item(F2_SPARE, i), s = hs_item(F2_TBL, i);
        for (u16 k = 0; k < 22; k++) uwb(d, k, ub(s, k));
    }
    mission_build((s8)DSB(F2_TYPE), (s8)arg, 0);
    if (DSB(F2_TYPE) != 5) DSB(F2_TYPE) = DSB(0x626C);
    DSW(0x4A32) = (u16)(DSW(0x62DF) - 1);
    DSW(0x4A3A) = (u16)(s16)(s8)DSB(0x626F);
    DSW(0x4A38) = (u16)(s16)(s8)DSB(0x62CC);
    DSW(0x4A3E) = (u16)(s16)(s8)DSB(0x6270);
    DSW(0x4A42) = (u16)(DSW(0x636F) - 1);
    DSW(0x4A3C) = 0;
    DSB(F2_RELINK) = 1;
    DSB(F2_ROSTER) = 0;
    DSB(F2_SKILLCHG) = 0;
    DSB(F2_SVC) = DSB(F2_TYPE) == 0 ? 1 : arg;
    fm2_find_ac(0, F2_LCNT0, 0x62DD, 0x4A34);
    fm2_find_ac(1, F2_LCNT1, 0x636D, 0x4A44);
    fm2_find_ac(2, F2_LCNT2, 0x63FD, 0x4A40);
    fm2_codes_all();
    fm2_prepare_acelist(0);
    s16 s = fm2_prepare_acelist(1);
    DSW(0x4A36) = DSW(0x636F) != 0 ? (u16)(s16)(s / (s16)DSW(0x636F)) : 0;
    if (DSB(0x626C) == 8) DSB(F2_TGT) = DSB((u16)(0x6477 + (s8)DSB(0x62E5) * 20));
    else if (DSB(0x626C) == 9) DSB(F2_TGT) = DSB((u16)(0x64B2 + (s8)DSB(0x62E5) * 18));
}

/* long at DS:o */
static s32 dsl(u16 o) { return (s32)DSL(o); }
static void dsl_wr(u16 o, s32 v) { DSL(o) = (u32)v; }

/* PS 100e:0bff fm2_apply — writes the chosen values into the mission */
static void fm2_apply(void)
{
    if (DSB(F2_CHANGED)) {
        const u16 f = 0x62C5;
        flight_open();
        DSB(0x626F) = (u8)DSW(0x4A3A);
        DSB(0x6270) = (u8)DSW(0x4A3E);
        DSW(f + 0x1A) = (u16)(DSW(0x4A32) + 1);
        if (DSB(F2_TYPE) == 1 || DSB(F2_TYPE) == 0) {
            u16 m = lmid(own_wp(), ene_wp());
            dcopy(own_wp(), m, 12);
            dcopy(ene_wp(), m, 12);
            if (DSB(F2_ALTOK)) {
                DSB(f + 7) = (u8)(DSB(0x4A38) + 1);
                s32 a = dsl((u16)(0x6526 + (s8)DSB(f + 7) * 8));
                u16 w = DSW(f + 0x22);
                dsl_wr((u16)(w + 0x14), a);
                dsl_wr((u16)(w + 8), a);
                DSB(0x635C) = DSB(f + 7);
                dsl_wr((u16)(ene_wp() + 8), a);
                dsl_wr((u16)(ene_wp() + 0x14), a);
            }
            u16 h;
            switch ((s8)DSB(0x6270)) {
            case 0:                                      /* no one surprised: fly at each other */
                dsl_wr(own_wp(), dsl(own_wp()) + 0x4A38); /* sic: 19000 */
                dsl_wr(ene_wp(), dsl(ene_wp()) - 0x4A38);
                dcopy((u16)(own_wp() + 0x0C), ene_wp(), 12);
                dcopy((u16)(ene_wp() + 0x0C), own_wp(), 12);
                h = heading(dsfar(own_wp()), dsfar((u16)(own_wp() + 0x0C)));
                DSB(0x62CA) = (u8)((s16)h >> 8);
                h = heading(dsfar(ene_wp()), dsfar((u16)(ene_wp() + 0x0C)));
                DSB(0x635A) = (u8)((s16)h >> 8);
                goto done_pos;
            case 1:                                      /* you surprise him */
                dsl_wr(own_wp(), dsl(own_wp()) + 5000);
                dsl_wr(ene_wp(), dsl(ene_wp()) - 5000);
                dsl_wr((u16)(DSW(f + 0x22) + 8), dsl((u16)(DSW(f + 0x22) + 8)) + 3000);
                h = heading(dsfar(own_wp()), dsfar(ene_wp()));
                DSB(0x62CA) = (u8)((s16)h >> 8);
                DSB(0x635A) = DSB(f + 5);
                break;
            case 2:                                      /* he surprises you */
                dsl_wr(own_wp(), dsl(own_wp()) + 5000);
                dsl_wr(ene_wp(), dsl(ene_wp()) - 5000);
                dsl_wr((u16)(ene_wp() + 8), dsl((u16)(ene_wp() + 8)) + 3000);
                h = heading(dsfar(ene_wp()), dsfar(own_wp()));
                DSB(0x635A) = (u8)((s16)h >> 8);
                DSB(0x62CA) = DSB(0x635A);
                break;
            default:
                goto done_pos;
            }
            project(dsfar(own_wp()), h, 4000000, (u16)(own_wp() + 0x0C));
            project(dsfar(ene_wp()), h, 4000000, (u16)(ene_wp() + 0x0C));
        }
done_pos:
        DSW(0x636F) = (u16)(DSW(0x4A42) + 1);
        if ((s16)DSW(0x4A34) > -1) {
            acl_select(0, 0);
            DSW(f + 0x18) = (u16)acl_id((s16)DSW(0x4A34));
            DSB(f + 2) = (u8)ac_class((s16)DSW(f + 0x18));
            DSB(f + 1) = flight_formation((s8)DSB(0x62C5), (s8)DSB(0x62C7), DSW(0x6277));
            DSB(f + 3) = (u8)ord_pick((s8)DSB(F2_TYPE), ac_selfid((s16)DSW(f + 0x18)), (s8)DSB(0x0A9C), 0);
            if (DSB(0x626C) != 0 && DSB(0x626C) != 1) {
                for (s16 k = 0; (s16)DSW(f + 0x1C) > k; k++) {
                    u16 wp = (u16)(DSW(f + 0x22) + k * 12);
                    s32 cap = dsl((u16)(0x651E + (s8)DSB(f + 2) * 4));
                    if (dsl((u16)(wp + 8)) > 10000) dsl_wr((u16)(wp + 8), cap);
                    if ((DSB(0x626C) == 8 || DSB(0x626C) == 9) && (s16)DSW(0x6265) > 0) {
                        u16 t = (u16)(DSW(0x6407) + k * 12);
                        if (dsl((u16)(t + 8)) > 10000) dsl_wr((u16)(t + 8), cap);
                    }
                }
            }
        }
        if ((s16)DSW(0x4A44) > -1) {
            acl_select(1, 0);
            DSW(0x636D) = (u16)acl_id((s16)DSW(0x4A44));
            DSB(0x6357) = (u8)ac_class((s16)DSW(0x636D));
            DSB(0x6356) = flight_formation((s8)DSB(0x6355), (s8)DSB(0x6357), DSW(0x6277));
            DSB(0x6358) = (u8)ord_pick((s8)DSB(0x626C), (s8)DSB(0x636D), 0, 1);
            if (DSB(0x626C) == 4) {                      /* scramble */
                s32 a = dsl((u16)(0x651E + (s8)DSB(0x6357) * 4));
                if (DSB(0x6357) == 3) a = 20000;
                dsl_wr((u16)(ene_wp() + 8), a);
                dsl_wr((u16)(ene_wp() + 0x14), a);
            }
        }
        if ((s16)DSW(0x4A40) > -1 && (DSB(0x626C) == 6 || DSB(0x626C) == 7)) {
            acl_select(2, 0);
            DSB(0x63E6) = flight_formation((s8)DSB(0x63E5), (s8)DSB(0x63E7), DSW(0x6277));
            DSW(0x63FD) = (u16)acl_id((s16)DSW(0x4A40));
            DSB(0x63E8) = (u8)ord_pick((s8)DSB(0x626C), (s8)DSB(0x63FD), 0, 2);
        }
        flight_close();
    }
    DSB(0x62CF) = 4;                                     /* slot 0 = the player */
    if (DSB(F2_TYPE) != 5) {
        /* quirk (L): slots 1–3 take DS:4B28[s] = DS:4B29[s-1] (the player's own code in slot 1) */
        for (s16 s = 1; s < 4; s++) {
            s8 c = (s8)DSB((u16)(0x4B28 + s));
            if (c >= 4) { DSW((u16)(0x62D3 + s * 2)) = (u16)(c - 4); DSB((u16)(0x62CF + s)) = 0xFF; }
            else DSB((u16)(0x62CF + s)) = (u8)c;
        }
        for (s16 s = 0; s < 4; s++) {
            s8 c = (s8)DSB((u16)(F2_ECODES + s));
            if (c >= 4) { DSW((u16)(0x6363 + s * 2)) = (u16)(c - 4); DSB((u16)(0x635F + s)) = 0xFF; }
            else DSB((u16)(0x635F + s)) = (u8)c;
        }
    }
    if (DSB(F2_SKILLCHG)) {
        /* the enemy flights are 90h apart; the code steps 24h (sic) */
        for (s16 i = 0; i < (s16)DSW(0x6263); i++)
            for (s16 j = 0; (s16)DSW((u16)(0x636F + i * 0x24)) > j; j++)
                DSB((u16)(0x635F + i * 0x24 + j)) = (u8)DSW(0x4A36);
    }
    strings_free_far(F2_ROW0, 0x35);
    strtab_free();
    if (DSB(F2_TYPE) == 0) acetxt_close();

    /* data sets the mission needs -> SIM.IN flags */
    u8 bits[9], need[9];
    for (int k = 0; k < 8; k++) bits[k] = DSB((u16)(0x0B26 + k));
    /* PORT: bits[8] reads one byte past the 8-byte copy of DS:0B26 on the stack: the low byte of the
     * local holding the last lower bound of the third loop below, (8-1)*10+35 = 69h. */
    bits[8] = 0x69;
    for (int k = 0; k < 9; k++) need[k] = 0;
    for (s16 fl = 0; fl < 3; fl++) {
        u16 fo = (u16)(fl * 0x90);
        for (s16 d = 0; d <= 8; d++) {
            s16 a = (s16)(DSW((u16)(0x62DD + fo)) + 1);
            if (a < d * 15 + 0x27 && a > (d - 1) * 15 + 0x27) need[d] = 1;
            for (s16 k = 0; k < 4; k++) {
                s16 p = (s16)DSW((u16)(0x62D3 + fo + k * 2));
                if (p < d * 15 + 0x1D && p >= (d - 1) * 15 + 0x1D) need[d] = 1;
            }
        }
    }
    for (s16 d = 0; d <= 8; d++)
        for (s16 g = 0; g < 3; g++)
            for (s16 k = 0; (s16)DSW((u16)(0x6483 + g * 20)) > k; k++) {
                s16 t = (s16)((s8)DSB((u16)(0x6479 + g * 20 + k)) + 1);
                if (d * 10 + 0x23 > t && (d - 1) * 10 + 0x23 < t) need[d] = 1;
            }
    need[(s8)module_active()] = 1;
    for (s16 d = 0; d <= 8; d++)
        if (need[d]) {
            DSB(0x6271) |= bits[d];
            DSB(0x4B8D) |= bits[d];
        }
}

/* PS 100e:12c9 flymiss2_setup(type, arg) -> 1 = fly — caller flymenu_run */
u16 flymiss2_setup(u16 type, u16 arg)
{
    u8 ok = 0;
    if ((u8)type == 0) acetxt_open();
    DSB(F2_TYPE) = (u8)type;
    fm2_init((u8)arg);
    for (;;) {
        screen_show((u16)(s16)(s8)type, 0x11A, 1);
        draw_title_bar_for(DSB(F2_SVC), DSW(0x6277), str_get((s16)((s8)DSB(F2_TYPE) + 2)));
        hs_set_table(fdat(F2_TBL), 13);
        hs_focus(12);
        hs_draw_all();
        menu_run(UFP(fm2_frame));
        ui_busy();
        if (DSB(F2_PRESSED) == 0x0B) {                   /* CANCEL / Esc */
            fm2_apply();
            break;
        }
        if (DSB(F2_PRESSED) != 0x0C) continue;
        if ((u8)type == 0) {                             /* dogfight an ace: straight to the sim */
            fm2_apply();
            ok = 1;
        } else if ((u8)type == 5) {                      /* training: the TRN<n>.DAT mission */
            s16 n = (s16)((s8)DSB(F2_TRSLOT) * 5 + (s8)DSB(F2_SVC));
            fm2_apply();
            ok = 1;
            ps_mission_load(make_name(0x0B47 /* "TRN" */, n, 0x0B4B /* "DAT" */), 1);
            mission_write_script();
            ps_mission_free_1f06();
            DSB(0x626C) = 5;
            DSB(0x626B) = 0;
            DSB(0x62C5) = DSB(F2_SVC);
            if (!far_is_null(ds_far(0x5272))) { xfarfree(ds_far(0x5272)); ds_far_wr(0x5272, fnull()); }
            if (!far_is_null(ds_far(0x5276))) { xfarfree(ds_far(0x5276)); ds_far_wr(0x5276, fnull()); }
            load_free();
        } else {
            fm2_apply();
            briefing(0xFFFF);
            if (DSB(0x43D6)) ok = 1;
        }
        break;
    }
    return ok;
}

/* PS 100e:1459 fm2_links_init */
void fm2_links_init(void)
{
    hs_default_links(fdat(F2_TBL), 13);
}

__attribute__((constructor)) static void flymiss2_codeptrs(void)
{
    UREG(fm2_btn_cb);
    UREG(fm2_on_ok);
    UREG(fm2_roster_cb);
    UREG(fm2_subst);
    UREG(fm2_draw_text);
    UREG(fm2_slot_cb);
    UREG(fm2_frame);
}
