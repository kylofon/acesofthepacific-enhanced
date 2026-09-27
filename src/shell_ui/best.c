/* shell_ui part B: best.c (PS 09a9) — BEST MISSIONS / BEST CAREERS and the records they show
 * (shell_ui_options.md §5). Screen 42 PARTS/BEST, table 4a08:000d (12 items). Records in far data
 * segment 4a08: best missions 4a08:0115 (10 × 55 bytes, count DS:433F), best careers 4a08:033B
 * (10 × 122-byte pilot records, count DS:4341). */
#include "shell_ui/sui_b_priv.h"

#define BS_SEG      0x4a08
#define BS_TBL      BS_SEG, 0x000d
#define BS_MODE     0x4307      /* 0 missions, 1 careers */
#define BS_RANK     0x4306      /* rank of the new best mission (1-based) */
#define BS_STAGE    0x4308      /* 55-byte staging record */
#define NMISS       0x433F
#define NACE        0x4341

static FarPtr bmiss(s16 i) { return fdat(BS_SEG, (u16)(0x0115 + i * 0x37)); }
static FarPtr bace(s16 i)  { return fdat(BS_SEG, (u16)(0x033B + i * 0x7A)); }
static void fcopy(FarPtr d, FarPtr s, u16 n) { for (u16 k = 0; k < n; k++) uwb(d, k, ub(s, k)); }   /* SCOPY@ */

/* PS 09a9:0002 best_draw_window */
static void best_draw_window(void)
{
    draw_side_riveted_panel();
}

/* PS 09a9:000c best_draw_scroll */
static void best_draw_scroll(void)
{
    draw_bevel_box();
    DSB(LINE_C) = 0xC0;
    gfx_rect((s16)(DSS(RX) + 4), (s16)(DSS(RY) + 0x19), 0x12, 0x10);
}

/* PS 09a9:0039 best_btn_cb */
static void best_btn_cb(void)
{
    if (btn_cb()) {
        DSW(HS_RES) = DSW(CUR_IDX);
        menu_set_result(1);
    }
}

/* PS 09a9:0057 best_draw_colhead — ids DS:06FC[(careers ? 0 : 2) + user] */
static void best_draw_colhead(void)
{
    gfx_font_select((s16)DSW(F_FONT3));
    DSB(TXT_FLG) = 1;
    u16 user = uw(cur_item(), HS_USER);
    s16 id = 0;
    if (user != 0) id = (s16)DSW((u16)(0x06FC + (DSB(BS_MODE) == 1 ? 0 : 1) * 4 + user * 2));
    u16 s = str_get(id);
    text_engrave(s, 0xC7, 0xB1, 0xC0, DSW(RX), DSW(RY));
}

/* PS 09a9:00c4 best_draw_header — record 0 of the current list */
static void best_draw_header(void)
{
    u8 careers = DSB(BS_MODE) == 1;
    s16 sym = (s16)((careers ? uc(bace(0), 0x21) : uc(bmiss(0), 0x2E)) + 8);
    u32 score = careers ? ul(bace(0), 0x65) : ul(bmiss(0), 0x12);
    FarPtr name = careers ? bace(0) : bmiss(0);
    rtl_sprintf(STRBUF, 0x071A /* "%Fs       %ld" */, (unsigned)name.off, (unsigned)name.seg,
                (unsigned)(u16)score, (unsigned)(u16)(score >> 16));
    uww(cur_item(), HS_LABEL, str_get(DSB(BS_MODE) == 0 ? 4 : 5));   /* "ALL TIME BEST MISSION/CAREER" */
    draw_header_panel();
    gfx_font_select((s16)DSW(F_TALK));
    u16 tx = ps_text_center_x(STRBUF, DSW(RX), DSW(RW));
    text_engrave(STRBUF, 0xC7, 0xB1, 0xC0, tx, (u16)(DSS(RY) + DSS(RH) + 3));
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), sym), (s16)(DSS(RX) + 9), (s16)(DSS(RY) + 2), 0);
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), sym), (s16)(DSS(RX) + DSS(RW) - 0x16), (s16)(DSS(RY) + 2), 0);
}

/* PS 09a9:0201 best_draw_row(i, y) */
static void best_draw_row(s16 i, s16 y)
{
    FarPtr rec;
    s8 rank, sym;
    u32 score;
    if (DSB(BS_MODE) == 1) {
        rec = bace(i);
        rank = uc(rec, 0x23);
        sym = uc(rec, 0x21);
        score = ul(rec, 0x65);
    } else {
        rec = bmiss(i);
        rank = uc(rec, 0x30);
        sym = uc(rec, 0x2E);
        score = ul(rec, 0x12);
    }
    FarPtr r = strings_get(3, rank);
    rtl_sprintf(STRBUF, 0x0728 /* "%Fs %Fs" */, (unsigned)r.off, (unsigned)r.seg, (unsigned)rec.off, (unsigned)rec.seg);
    gfx_text(STRBUF, (s16)(DSS(RX) + 0x0F), y);
    FarPtr col2 = DSB(BS_MODE) == 0 ? ac_name((s16)uw(bmiss(i), 0x10))            /* aircraft */
                                    : strings_get(6, uc(bace(i), 0x22));           /* service */
    strcpy_far_src(STRBUF, col2);
    gfx_text(STRBUF, (s16)(DSS(RX) + 0x78), y);
    rtl_ltoa((s32)score, STRBUF, 10);
    gfx_text(STRBUF, (s16)(DSS(RX) + 0xD0), y);
    gfx_sprite_flip(bmp_frame(DSW(SYMBOLS_TBL), (s16)(sym + 0x10)), (s16)(DSS(RX) + 5), y, 0);
}

/* PS 09a9:0381 best_draw_bg */
static void best_draw_bg(void)
{
    screen_clear_panel();
    draw_title_bar_for(0xFF, 0, str_get(DSB(BS_MODE) == 1 ? 9 : 8));   /* "BEST CAREERS" / "BEST MISSIONS" */
}

/* PS 09a9:03b0 best_frame */
static void best_frame(void)
{
    hs_dispatch(DSW(PTR_X), DSW(PTR_Y), DSW(BUTTON), 0);
    hs_present();
    edit_poll();
    if (menu_get_result() == 2) DSW(HS_RES) = 0x0B;
}

/* PS 09a9:03e6 best_load_mission(i) — the mission globals for replaying its debrief */
static void best_load_mission(s16 i)
{
    FarPtr r = bmiss(i);
    DSW(0x477B) = uw(r, 0x35);
    DSB(0x4B8E) = ub(r, 0x2F);
    DSB(0x4B8F) = ub(r, 0x31);
    DSB(0x4B88) = ub(r, 0x33);
    DSB(0x4B87) = ub(r, 0x32);
    DSB(0x4B89) = ub(r, 0x34);
    DSW(0x4247) = uw(r, 0x16);
    DSW(0x4249) = uw(r, 0x18);
    DSW(0x422B) = uw(r, 0x1A);
    for (s16 k = 0; k < 4; k++) {
        DSW((u16)(0x4237 + k * 2)) = uw(r, (u16)(0x1C + k * 2));
        DSW((u16)(0x423F + k * 2)) = uw(r, (u16)(0x24 + k * 2));
    }
    DSW(0x4227) = uw(r, 0x2C);
}

/* PS 09a9:0481 best_run(n) */
static void best_run(u16 n)
{
    for (;;) {
        strtab_load(make_name(0x0734 /* "strings_" */, -1, 0x073D /* "dat" */));
        lbox_setup(0, 0, n, 10, fnull(), UFP(best_draw_row));
        lbox_bind(fdat(BS_TBL), 6, 8);
        screen_open(0x2A, UFP(best_draw_bg), 0x396, 1);
        hs_set_table(fdat(BS_TBL), 0x0C);
        DSW(HS_RES) = 0xFFFF;
        hs_draw_all();
        menu_run(UFP(best_frame));
        ui_busy();
        if (DSW(HS_RES) == 0x0A) {                                   /* VIEW */
            if (DSB(BS_MODE) == 1) {
                fcopy(ds_ptr(0x4754), bace((s16)lbox_selected()), 0x7A);
                strtab_free();
                pilot_record();
            } else {
                best_load_mission((s16)lbox_selected());
                strtab_free();
                debrief_run(0);
            }
            continue;
        }
        if (DSW(HS_RES) == 0x0B) break;                              /* EXIT / Esc */
    }
    strtab_free();
}

/* PS 09a9:058f best_careers — career menu VIEW BEST CAREERS */
void best_careers(void)
{
    DSB(BS_MODE) = 1;
    squad_open();
    best_run(DSW(NACE));
    squad_close();
}

/* PS 09a9:05ac best_missions — fly menu BEST MISSIONS */
void best_missions(void)
{
    DSB(BS_MODE) = 0;
    air_open();
    best_run(DSW(NMISS));
    air_close();
}

/* PS 09a9:05c9 best_rank_subst(c) — %r only */
static FarPtr best_rank_subst(s8 c)
{
    if (c != 'r') ufatal(BS_SEG, 0x19E);
    str_int(STRBUF, (s16)(s8)DSB(BS_RANK));
    return ds_ptr(STRBUF);
}

/* PS 09a9:060a best_store(i) — the staging record DS:4308 from the finished mission, into slot i */
static void best_store(s16 i)
{
    if (DSB(0x4B87) == 1) {                                          /* career */
        rtl_strcpy(BS_STAGE, 0x4754);
        DSW(0x4318) = DSW(0x478A);
        DSB(0x4336) = DSB(0x4775);
        DSB(0x4338) = DSB(0x4777);
        DSW(0x433D) = DSW(0x477B);
    } else {                                                         /* ask for the name */
        u16 mk = stk_mark();
        u16 buf = stk_alloc(0x14);
        for (int b = 0; b < 0x14; b++) DSB(buf + b) = DSB(0x0706 + b);
        text_entry(0, buf, 15, 1);
        rtl_strcpy(BS_STAGE, buf);
        stk_release(mk);
        DSW(0x4318) = DSW(0x4B90);
        DSB(0x4336) = (u8)service_side(DSB(0x4B8E));
        ranks_load();
        DSB(0x4338) = (u8)rank_last((s8)DSB(0x4B8E));
        ranks_free();
        DSW(0x433D) = 0;
    }
    DSB(0x4337) = DSB(0x4B8E);
    DSW(0x431E) = DSW(0x4247);
    DSW(0x4320) = DSW(0x4249);
    DSL(0x431A) = (u32)(s32)(s16)DSW(0x4249);
    DSW(0x4322) = DSW(0x422B);
    for (s16 k = 0; k < 4; k++) {
        DSW((u16)(0x4324 + k * 2)) = DSW((u16)(0x4237 + k * 2));
        DSW((u16)(0x432C + k * 2)) = DSW((u16)(0x423F + k * 2));
    }
    DSW(0x4334) = DSW(0x4227);
    DSB(0x4339) = DSB(0x4B8F);
    DSB(0x433A) = DSB(0x4B87);
    DSB(0x433B) = DSB(0x4B88);
    DSB(0x433C) = DSB(0x4B89);
    fcopy(bmiss(i), ds_ptr(BS_STAGE), 0x37);
}

/* PS 09a9:072e best_mission_insert — after the debrief of a mission */
void best_mission_insert(void)
{
    s32 score = (s16)DSW(0x4249);
    s16 i;
    for (i = 0; i < (s16)DSW(NMISS); i++)
        if ((s32)ul(bmiss(i), 0x12) < score) break;
    if (i >= 10) return;
    for (s16 b = 9; b > i; b--) fcopy(bmiss(b), bmiss((s16)(b - 1)), 0x37);
    if ((s16)DSW(NMISS) < 10) DSW(NMISS) = (u16)(DSW(NMISS) + 1);
    DSB(BS_RANK) = (u8)(i + 1);
    FarPtr subst = UFP(best_rank_subst);
    req_ok(subst.off, subst.seg, 10, 8);                              /* "...ranks %r..." */
    best_store(i);
}

/* PS 09a9:07ee best_career_insert — the current pilot by score into the top-10 careers */
void best_career_insert(void)
{
    s16 si;
    for (si = 0; si < (s16)DSW(NACE); si++)
        if ((s32)ul(bace(si), 0x65) < (s32)DSL(0x47B9)) break;
    if (si >= 10) return;
    for (s16 b = 9; b > si; b--) fcopy(bace(b), bace((s16)(b - 1)), 0x7A);
    if ((s16)DSW(NACE) < 10) DSW(NACE) = (u16)(DSW(NACE) + 1);
    fcopy(bace(si), ds_ptr(0x4754), 0x7A);
}

/* PS 09a9:088e best_career_remove — the entry matching the current pilot (score, service, rank,
 * missions, date); scanning continues over the shifted list, the count drops once */
void best_career_remove(void)
{
    u8 found = 0;
    for (s16 di = 0; di < (s16)DSW(NACE); di++) {
        FarPtr r = bace(di);
        if (ul(r, 0x65) != DSL(0x47B9)) continue;
        if (ub(r, 0x22) != DSB(0x4776)) continue;
        if (ub(r, 0x23) != DSB(0x4777)) continue;
        if (uw(r, 0x30) != DSW(0x4784)) continue;
        if (uw(r, 0x27) != DSW(0x477B)) continue;
        found = 1;
        for (s16 b = di; b < (s16)DSW(NACE); b++) fcopy(bace(b), bace((s16)(b + 1)), 0x7A);
    }
    if (found) DSW(NACE) = (u16)(DSW(NACE) - 1);
    if ((s16)DSW(NACE) < 0) DSW(NACE) = 0;
}

/* PS 09a9:096a best_init — dialogs_init */
void best_init(void)
{
    hs_default_links(fdat(BS_TBL), 0x0C);
}

__attribute__((constructor)) static void best_codeptrs(void)
{
    UREG(best_draw_window);
    UREG(best_draw_scroll);
    UREG(best_btn_cb);
    UREG(best_draw_colhead);
    UREG(best_draw_header);
    UREG(best_draw_row);
    UREG(best_draw_bg);
    UREG(best_frame);
    UREG(best_rank_subst);
}
