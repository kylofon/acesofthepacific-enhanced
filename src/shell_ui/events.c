/* shell_ui part B: events.c — career events and campaign progress (PS 0874 "events.c",
 * shell_ui_debrief.md §7) and the post-mission notifications (PS 1656, §0.3 / shell_ui_mission.md §10).
 * EVENTS<n>.EVT: u16 n; n × 66-byte records; u16 len; text block (debrief §7.1). */
#include "shell_ui/sui_b_priv.h"

#define EV_SEG      0x49b8          /* no assert in this module; unused */
#define SQ_LOADED   0x0674          /* u8: this module loaded the squadron table */
#define CAMP_LOADED 0x0675
#define TXT_FILE    0x0676          /* s8: EVENTS file whose text block is cached */
#define TXT_CAP     0x0677
#define TEXT1       0x0679          /* far */
#define TEXT2       0x067D          /* far */
#define CURRENT     0x0681          /* far: event being played (for the TTM hook) */
#define F_NP_HDLN   0x0685
#define F_NP_TXT    0x0687
#define F_SQ_SML    0x0689
#define F_SQ_HDL    0x068B
#define LINE_H      0x068D
#define FONT_1      0x068F
#define FONT_2      0x0691
#define TTM_DREW    0x0693
#define EV_FONTS    0x42E4          /* u16[9] */
#define EV_CAP      0x42F6
#define EV_COUNT    0x42F8
#define STATION     0x42FA          /* near: current station name (a local of career_continue) */
#define TEXT_BUF    0x42FC          /* far */
#define DATE_TO     0x4300
#define DATE_FROM   0x4302
#define DAY         0x4304
#define REC         0x42            /* record size */

/* PS 0874:0003 career_pick_squadron(u16 end) -> squadron / -1 */
static s16 career_pick_squadron(u16 end)
{
    if (DSB(SQ_LOADED) == 0) { squad_open(); DSB(SQ_LOADED) = 1; }
    s16 cur = (s16)DSW(0x478E), best = cur;
    u16 bestd = 0;
    s8 n = (s8)squad_count();
    for (s8 k = 0; k < n; k++) {
        if (!campaign_has_squad((s16)DSW(0x4790), k)) continue;
        u16 d = squad_next_mission_date(k, (u16)(DSW(0x477B) + 1), end);
        if (d == 0) continue;
        if (best == cur || rng_below(2) != 0) {             /* RNG only after a first pick */
            best = k;
            bestd = d;
        }
    }
    if (best != cur) {
        squad_sched_init(best, 0x477B, bestd);
        DSW(0x478A) = (u16)squad_aircraft(best, DSW(0x477B));
        DSW(0x478C) = (u16)squad_station(best, DSW(0x477B));
    }
    return best == cur ? -1 : best;
}

static FarPtr misc_text(u16 buf, u16 base, u16 ext, s16 table)
{
    strtab_load(make_name(base, -1, ext));
    strcpy_far_src(buf, strings_get(table, 0));
    strtab_free();
    return ds_ptr(buf);
}

/* PS 0874:00d7 misc_tour_text(buf) — MISC.DAT 0 "You have successfully completed your tour..." */
static FarPtr misc_tour_text(u16 buf) { return misc_text(buf, 0x0694 /* "misc" */, 0x0699, 0); }

/* PS 0874:011d event_conditions(evt far *e) — marks the event seen when it is collected */
static u8 event_conditions(FarPtr e)
{
    u8 id = ub(e, 0x19);
    if ((u8)pilot_event_seen(id)) return 0;
    if (uw(e, 1) != 0 && !(uw(e, 1) & (u16)(1 << (DSB(0x4775) & 0x1F)))) return 0;
    if (uw(e, 3) != 0 && !(uw(e, 3) & (u16)(1 << (DSB(0x4776) & 0x1F)))) return 0;
    if (uw(e, 7) > DSW(DATE_TO)) return 0;
    if (uw(e, 9) != 0 && uw(e, 9) < DSW(DATE_FROM)) return 0;
    if (us(e, 0x0B) != -1 && us(e, 0x0B) > (s16)DSW(0x4784)) return 0;
    if (us(e, 0x0D) != -1 && us(e, 0x0D) < (s16)DSW(0x4784)) return 0;
    s16 v = (s16)DSW(0x47BF);
    if (us(e, 0x0F) != -1 && us(e, 0x0F) > v) return 0;
    if (us(e, 0x11) != -1 && us(e, 0x11) < v) return 0;
    v = (s16)DSW(0x47C3);
    if (us(e, 0x13) != -1 && us(e, 0x13) > v) return 0;
    if (us(e, 0x15) != -1 && us(e, 0x15) < v) return 0;
    if (us(e, 0x17) != -1 && (u16)DSB(0x4B8C) != uw(e, 0x17)) return 0;
    if (uw(e, 5) != 0) {
        if (DSB(SQ_LOADED) == 0) { squad_open(); DSB(SQ_LOADED) = 1; }
        u8 role = squad_role((s16)DSW(0x478E));
        if (!(uw(e, 5) & (u16)(1u << (role & 0x1F)))) return 0;
    }
    pilot_event_add(id);
    return 1;
}

/* PS 0874:0291 event_matches(evt far *e) */
static u8 event_matches(FarPtr e)
{
    switch ((u16)((s16)(s8)ub(e, 0) - 1)) {
    case 0: return (s16)(s8)DSB(0x0501) == us(e, 1);            /* promotion this mission */
    case 1: return (s16)(s8)DSB(0x0500) == us(e, 1);            /* medal */
    case 2:                                                      /* SIM.OUT +06 event ids */
        for (s8 k = 0; k < 4 && (s16)DSW((u16)(0x422D + k * 2)) != -1; k++)
            if (DSW((u16)(0x422D + k * 2)) == uw(e, 1)) return 1;
        return 0;
    case 3: return uw(e, 1) == DSW(0x4235);
    case 4: return event_conditions(e);
    default: return 0;
    }
}

/* PS 0874:0337 evt_read_u16(FILE *f) */
static u16 evt_read_u16(u16 f)
{
    u16 mk = stk_mark();
    u16 w = stk_alloc(2);
    my_fread(w, 2, 1, f);
    u16 v = DSW(w);
    stk_release(mk);
    return v;
}

/* PS 0874:035d evt_read_matching(buf far, f, at, n) -> kept */
static u16 evt_read_matching(FarPtr buf, u16 f, u16 at, u16 n)
{
    u16 mk = stk_mark();
    u16 rec = stk_alloc(REC);
    u16 kept = 0;
    for (u16 i = 0; i < n; i++) {
        if (my_fread(rec, REC, 1, f) == 0) continue;
        if (!event_matches(ds_ptr(rec))) continue;
        FarPtr d = far_add(buf, (u16)((at + kept) * REC));
        for (u16 b = 0; b < REC; b++) uwb(d, b, DSB(rec + b));
        kept++;
    }
    stk_release(mk);
    return kept;
}

/* PS 0874:03cc evt_load_file(buf far, f, have) -> buf' (grows by 10 records) */
static FarPtr evt_load_file(FarPtr buf, u16 f, u16 have)
{
    if (f == 0) return buf;
    u16 n = evt_read_u16(f);
    if (far_is_null(buf)) DSW(EV_CAP) = 0;
    u16 need = (u16)(have + n);
    FarPtr nb;
    if (need > DSW(EV_CAP)) {
        DSW(EV_CAP) = (u16)(need + 10);
        DSW(EV_CAP) = (u16)(DSW(EV_CAP) - (u16)((u16)(need + 10) % 10));
        nb = xfarmalloc((u32)(u16)(DSW(EV_CAP) * REC));
        for (u16 i = 0; i < have; i++) {
            FarPtr d = far_add(nb, (u16)(i * REC)), s = far_add(buf, (u16)(i * REC));
            for (u16 b = 0; b < REC; b++) uwb(d, b, ub(s, b));
        }
        if (!far_is_null(buf)) xfarfree(buf);
    } else {
        nb = buf;
    }
    DSW(EV_COUNT) = (u16)(DSW(EV_COUNT) + evt_read_matching(nb, f, have, n));
    return nb;
}

/* PS 0874:04b0 events_collect -> far list (exact copy) or NULL */
static FarPtr events_collect(void)
{
    u16 mk = stk_mark();
    u16 fh = stk_alloc(0x12);
    FarPtr buf = fnull(), out = fnull();
    DSW(EV_COUNT) = 0;
    open_module_files(fh, 0x069D /* "events" */, 0x06A4 /* "evt" */);
    for (s8 m = module_iter_reset(); module_highest() >= m; m = module_iter_next())
        buf = evt_load_file(buf, DSW((u16)(fh + m * 2)), DSW(EV_COUNT));
    files_close(fh);
    if (DSW(EV_COUNT) != 0) {
        out = xfarmalloc((u32)(u16)(DSW(EV_COUNT) * REC));
        for (s8 k = 0; (u16)(s16)k < DSW(EV_COUNT); k++) {
            FarPtr d = far_add(out, (u16)((s16)k * REC)), s = far_add(buf, (u16)((s16)k * REC));
            for (u16 b = 0; b < REC; b++) uwb(d, b, ub(s, b));
        }
        xfarfree(buf);
    }
    /* the growth buffer leaks when files had records but none matched (as in the original) */
    stk_release(mk);
    return DSW(EV_COUNT) != 0 ? out : fnull();
}

/* PS 0874:05aa event_compare(evt *a, evt *b) — qsort on the near copy */
static s16 event_compare(u16 a, u16 b)
{
    s8 ta = (s8)DSB(a);
    if (ta != (s8)DSB(b)) return (s16)((s16)ta - (s16)(s8)DSB(b));
    if (DSB(a) == 5) {
        if (DSW(a + 7) != DSW(b + 7)) return (s16)(DSW(a + 7) - DSW(b + 7));
        return (s16)(DSW(a + 9) - DSW(b + 9));
    }
    return (s16)(DSW(a + 1) - DSW(b + 1));
}

/* PS 0874:05fb events_sort(list far) */
static void events_sort(FarPtr l)
{
    u16 n = DSW(EV_COUNT);
    u16 copy = xmalloc((u16)(n * REC));
    for (u16 i = 0; i < n; i++)
        for (u16 b = 0; b < REC; b++) DSB((u16)(copy + i * REC + b)) = ub(l, (u16)(i * REC + b));
    rtl_qsort(copy, n, REC, UFP(event_compare));
    for (u16 i = 0; i < n; i++)
        for (u16 b = 0; b < REC; b++) uwb(l, (u16)(i * REC + b), DSB((u16)(copy + i * REC + b)));
    xfree(copy);
}

/* PS 0874:0691 events_collect_sorted */
static FarPtr events_collect_sorted(void)
{
    FarPtr l = events_collect();
    if (DSW(EV_COUNT) != 0) events_sort(l);
    return l;
}

/* PS 0874:06bc event_load_texts(e) */
static void event_load_texts(FarPtr e)
{
    s8 n = (s8)((ub(e, 0x19) >> 6) & 3);
    if ((s8)DSB(TXT_FILE) != n) {
        DSB(TXT_FILE) = (u8)n;
        u16 f = my_fopen(make_name(0x06A8 /* "events" */, n, 0x06AF /* "evt" */), 0x06B3 /* "rb" */);
        u16 c = evt_read_u16(f);
        my_fseek(f, (s32)(u16)(c * REC), 1);
        u16 len = evt_read_u16(f);
        if (DSW(TXT_CAP) < len) {
            if (DSW(TXT_CAP) != 0) xfarfree(ds_far(TEXT_BUF));
            ds_far_wr(TEXT_BUF, xfarmalloc((u32)len));
            DSW(TXT_CAP) = len;
        }
        ps_fread_far_1193(ds_far(TEXT_BUF), 1, len, f);
        my_fclose(f);
    }
    ds_far_wr(TEXT1, far_add(ds_far(TEXT_BUF), uw(e, 0x2E)));      /* offsets added even when FFFF */
    ds_far_wr(TEXT2, far_add(ds_far(TEXT_BUF), uw(e, 0x3C)));
}

/* PS 0874:079f event_font(id) -> handle */
static u16 event_font(s16 id)
{
    u16 slot, base, ext;
    switch (id) {
    case 5: slot = F_SQ_SML;  base = 0x06B6; ext = 0x06BF; break;     /* "simq_sml" */
    case 6: slot = F_SQ_HDL;  base = 0x06C3; ext = 0x06CC; break;     /* "simq_hdl" */
    case 7: slot = F_NP_TXT;  base = 0x06D0; ext = 0x06D7; break;     /* "np_txt" */
    case 8: slot = F_NP_HDLN; base = 0x06DB; ext = 0x06E3; break;     /* "np_hdln" */
    default: {
        s16 r;
        idiv16_retry(id, 9, &r);
        return DSW((u16)(EV_FONTS + r * 2));
    }
    }
    if (DSW(slot) == 0) DSW(slot) = font_load(make_name(base, -1, ext));
    return DSW(slot);
}

/* PS 0874:0886 event_select_fonts(e) */
static void event_select_fonts(FarPtr e)
{
    s16 f1 = us(e, 0x30), f2 = us(e, 0x3E);
    if (us(e, 0x2E) != -1) DSW(FONT_1) = event_font(f1);
    if (us(e, 0x3C) != -1) DSW(FONT_2) = event_font(f2);
}

/* PS 0874:08c8 event_set_box1(e, frame) */
static void event_set_box1(FarPtr e, u8 frame)
{
    DSW(RX) = uw(e, 0x25);
    DSW(RY) = uw(e, 0x27);
    DSW(RW) = uw(e, 0x29);
    DSW(RH) = uw(e, 0x2B);
    pages_front_to_back();
    if (frame) {
        tile_fill_rect();
        ps_draw_frame();
    }
}

/* PS 0874:0901 event_draw_box1(e, present) — ids >= 9: engraved style */
static void event_draw_box1(FarPtr e, u8 present)
{
    DSB(TXT_FLG) = 1;
    DSB(TXT_COL) = ub(e, 0x32);
    gfx_font_select((s16)DSW(FONT_1));
    DSW(LINE_H) = (u16)gfx_font_height((s16)DSW(FONT_1));
    flow_setup(us(e, 0x30) >= 9 ? 1 : 0, DSW(LINE_H), 1);
    DSW(RX) = (u16)(DSW(RX) + 7);
    DSW(RY) = (u16)(DSW(RY) + 4);
    DSW(RW) = (u16)(DSW(RW) - 14);
    DSW(RH) = (u16)(DSW(RH) - 8);
    DSW(PG_DST) = DSW(PG_WORK);
    DSW(PG_SRC) = DSW(PG_WORK);
    flow_text(ds_far(TEXT1), 1, fnull());
    DSW(RX) = (u16)(DSW(RX) - 7);
    DSW(RY) = (u16)(DSW(RY) - 4);
    DSW(RW) = (u16)(DSW(RW) + 14);
    DSW(RH) = (u16)(DSW(RH) + 8);
    if (present) {
        pages_back_to_front();
        gfx_copy_rect(DSS(RX), DSS(RY), DSS(RW), DSS(RH));
    }
}

/* PS 0874:09b7 event_draw_box2(e, present) — tests the box-1 font id for the style (quirk) */
static void event_draw_box2(FarPtr e, u8 present)
{
    s16 sx = DSS(RX), sy = DSS(RY), sw = DSS(RW), sh = DSS(RH);
    DSB(TXT_FLG) = 1;
    DSB(TXT_COL) = ub(e, 0x40);
    gfx_font_select((s16)DSW(FONT_2));
    DSW(LINE_H) = (u16)gfx_font_height((s16)DSW(FONT_2));
    flow_setup(us(e, 0x30) >= 9 ? 1 : 0, DSW(LINE_H), 1);
    DSW(RX) = uw(e, 0x34);
    DSW(RY) = uw(e, 0x36);
    DSW(RW) = uw(e, 0x38);
    DSW(RH) = uw(e, 0x3A);
    DSW(PG_DST) = DSW(PG_WORK);
    DSW(PG_SRC) = DSW(PG_WORK);
    s16 hh = (s16)(s8)flow_text(ds_far(TEXT2), 1, fnull());
    if (present) {
        pages_back_to_front();
        gfx_copy_rect(us(e, 0x34), us(e, 0x36), us(e, 0x38), hh);
        if (us(e, 0x2E) != -1) gfx_copy_rect(sx, sy, sw, sh);
    }
}

/* PS 0874:0aa5 event_ttm_text_hook — TTM tag callback (shell_data glue 2221): draws the event's
 * boxes on the work page and dissolves them in from the spare page */
void event_ttm_text_hook(void)
{
    FarPtr e = ds_far(CURRENT);
    event_set_box1(e, ub(e, 0x2D));
    e = ds_far(CURRENT);
    if (us(e, 0x2E) != -1) event_draw_box1(e, 0);
    e = ds_far(CURRENT);
    if (us(e, 0x3C) != -1) event_draw_box2(e, 0);
    DSW(PG_DST) = DSW(PG_SPARE);
    DSW(PG_SRC) = DSW(PG_WORK);
    e = ds_far(CURRENT);
    gfx_copy_rect(us(e, 0x25), us(e, 0x27), us(e, 0x29), (s16)(us(e, 0x2B) + 1));
    upd_add(uw(e, 0x25), uw(e, 0x27), uw(e, 0x29), uw(e, 0x2B), 1, DSW(PG_SPARE), DSW(PG_WORK), 0, fnull());
    DSW(PG_DST) = DSW(PG_WORK);
    DSB(TTM_DREW) = 1;
}

/* PS 0874:0b57 event_play(evt far *e) */
static void event_play(FarPtr e)
{
    u8 texts = us(e, 0x2E) != -1 || us(e, 0x3C) != -1;
    if (texts) {
        event_load_texts(e);
        event_select_fonts(e);
    }
    pages_front_to_back();
    screen_clear_black();
    pages_back_to_front();
    wipe_rect(0, 0, 0x140, 0xC8);
    ds_far_wr(CURRENT, e);
    snd_stop(-1);
    DSB(0x4226) = 1;
    u16 mk = stk_mark();
    u16 name = stk_alloc(0x0A);
    strcpy_far_src(name, far_add(e, 0x1A));
    ttm_play_file(make_name(name, -1, 0x06E7 /* "ttm" */), -1, 0, 0, us(e, 0x23), codeptr_far(PS_FN_ttm_skip_check));
    stk_release(mk);
    if (DSB(TTM_DREW)) {
        DSB(TTM_DREW) = 0;                       /* the hook drew the texts during the animation */
    } else {
        event_set_box1(e, ub(e, 0x2D));
        if (us(e, 0x2E) != -1) event_draw_box1(e, us(e, 0x3C) == -1);
        if (us(e, 0x3C) != -1) event_draw_box2(e, 1);
    }
    if (us(e, 0x2E) != -1 || us(e, 0x3C) != -1) wait_ticks_or_key(10000);   /* 42.2 s */
}

/* PS 0874:0c8d misc_relocated_text(buf) — MISC.DAT row 3 "Your squadron, %p, is being relocated." */
static FarPtr misc_relocated_text(u16 buf) { return misc_text(buf, 0x06EB /* "misc" */, 0x06F0, 3); }

/* PS 0874:0cd6 career_squadron_day — station move / new aircraft of the squadron on DS:4304 */
static void career_squadron_day(void)
{
    if (DSB(SQ_LOADED) == 0) { squad_open(); DSB(SQ_LOADED) = 1; }
    if (squad_status((s16)DSW(0x478E), DSW(DAY)) != 0) return;
    s16 st = squad_station((s16)DSW(0x478E), DSW(DAY));
    if (st != (s16)DSW(0x478C)) {
        DSW(0x478C) = (u16)st;
        stattxt_open();
        u16 name = strpool_copy(station_name(st)).off;
        if (rtl_strcmp(DSW(STATION), name) != 0) {
            snd_stop(-1);
            snd_play(0x3F2);
            DSB(0x4226) = 1;
            u16 mk = stk_mark();
            u16 buf = stk_alloc(0x64);
            tbox_squadron_move(misc_relocated_text(buf));
            stk_release(mk);
            rtl_strcpy(DSW(STATION), name);
            stattxt_close();
            station_open();
            bigmap_show();
            station_close();
        } else {
            stattxt_close();
        }
    }
    s16 ac = squad_aircraft((s16)DSW(0x478E), DSW(DAY));
    if (ac != (s16)DSW(0x478A)) {
        DSW(0x478A) = (u16)ac;
        snd_stop(-1);
        snd_play(0x3F2);
        DSB(0x4226) = 1;
        strtab_load(make_name(0x06F4 /* "misc" */, -1, 0x06F9 /* "dat" */));
        air_open();
        view_vehicle(DSW(0x478A), DSB(0x4776), 1);                  /* "NEW AIRPLANE" */
        air_close();
        strtab_free();
    }
}

/* PS 0874:0e0f events_init_fonts — dialogs_init */
void events_init_fonts(void)
{
    DSW(EV_FONTS + 0) = DSW(F_FONT3);
    DSW(EV_FONTS + 2) = DSW(F_FONT2);
    DSW(EV_FONTS + 4) = DSW(F_FONT1);
    DSW(EV_FONTS + 6) = DSW(F_FONT4);
    DSW(EV_FONTS + 8) = DSW(F_TALK);
    DSW(EV_FONTS + 0x10) = 0;
    DSW(EV_FONTS + 0x0E) = 0;
    DSW(EV_FONTS + 0x0C) = 0;
    DSW(EV_FONTS + 0x0A) = 0;
}

/* PS 0874:0e40 events_free(list far) — the SIMQ fonts stay loaded */
static void events_free(FarPtr l)
{
    if (DSW(EV_COUNT) != 0) xfarfree(l);
    if (DSB(TXT_FILE) != 0xFF) xfarfree(ds_far(TEXT_BUF));
    DSW(TXT_CAP) = 0;
    DSB(TXT_FILE) = 0xFF;
    ds_far_wr(TEXT1, fnull());
    if (DSB(SQ_LOADED)) { squad_close(); DSB(SQ_LOADED) = 0; }
    if (DSW(F_NP_HDLN)) { font_free(DSW(F_NP_HDLN)); DSW(F_NP_HDLN) = 0; }
    if (DSW(F_NP_TXT)) { font_free(DSW(F_NP_TXT)); DSW(F_NP_TXT) = 0; }
}

/* PS 0874:0ec5 events_run(from, to, advance) — triggered events first, then dated ones by date */
static void events_run(u16 from, u16 to, s8 advance)
{
    DSW(DATE_FROM) = from;
    DSW(DATE_TO) = to;
    u16 d = from;
    FarPtr l = events_collect_sorted();
    u16 i = 0;
    while (i < DSW(EV_COUNT) && ub(far_add(l, (u16)(i * REC)), 0) != 5) {
        event_play(far_add(l, (u16)(i * REC)));
        i++;
    }
    while (i < DSW(EV_COUNT)) {
        if (advance)            /* career_squadron_day looks at DS:4304, not at d (faithful) */
            while (uw(far_add(l, (u16)(i * REC)), 7) > d) { career_squadron_day(); d++; }
        event_play(far_add(l, (u16)(i * REC)));
        i++;
    }
    if (advance)
        while (DSW(DAY) <= DSW(0x477F)) { career_squadron_day(); DSW(DAY) = (u16)(DSW(DAY) + 1); }
    events_free(l);
}

/* PS 0874:0f7d career_exit_hook — run by shell_exit when the player quits during the events */
static void career_exit_hook(void)
{
    if ((s16)DSW(0x4790) > -1) {
        DSB(CAMP_LOADED) = (u8)campaign_loaded();
        if (DSB(CAMP_LOADED) == 0) campaign_load((s8)DSB(0x4779));
        if (DSB(SQ_LOADED) == 0) squad_open();                  /* (flag not set) */
        u16 end = campaign_end((s16)DSW(0x4790));
        if (end <= DSW(0x477B)) {
            DSW(0x4790) = 0xFFFF;
        } else {
            u8 ok = 1;
            if (DSW(0x478E) == 0xFFFF || squad_status((s16)DSW(0x478E), DSW(0x477B)) != 0) {
                DSW(0x478E) = (u16)career_pick_squadron(end);
                if (DSW(0x478E) == 0xFFFF) { DSW(0x4790) = 0xFFFF; ok = 0; }
                else squad_sched_init((s16)DSW(0x478E), 0x477B, DSW(0x477B));
            }
            if (ok) {
                DSW(0x478A) = (u16)squad_aircraft((s16)DSW(0x478E), DSW(0x477B));
                DSW(0x478C) = (u16)squad_station((s16)DSW(0x478E), DSW(0x477B));
            }
        }
    }
    pilot_store();
}

/* PS 0874:1039 career_continue(sq) — sq > -1: transfer to squadron sq */
static void career_continue(s16 sq)
{
    u8 wounded = DSB(0x4229) & 4;
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x50);
    stattxt_open();
    DSW(STATION) = buf;
    strcpy_far_src(buf, station_name((s16)DSW(0x478C)));
    stattxt_close();
    campaign_close();
    DSB(CAMP_LOADED) = 0;
    if (sq > -1) {
        DSW(0x478E) = 0xFFFF;
        if (!wounded) events_run(DSW(DAY), DSW(0x477F), 0);
        DSW(0x478E) = (u16)sq;
        if (DSB(SQ_LOADED) == 0) { squad_open(); DSB(SQ_LOADED) = 1; }
        air_open();
        ace_open();
        station_open();
        squadinf(2);                                            /* the "transfer" text */
        bigmap_show();
        station_close();
        ace_close();
        air_close();
    } else if (!wounded) {
        events_run(DSW(DAY), DSW(0x477F), 1);
    }
    if (DSB(SQ_LOADED)) { squad_close(); DSB(SQ_LOADED) = 0; }
    stk_release(mk);                /* DS:42FA keeps pointing here: only used inside this call */
}

/* PS 0874:1113 career_tour_end(end) */
static void career_tour_end(u16 end)
{
    u8 jump = DSW(0x4235) == 1;
    u8 wounded = DSB(0x4229) & 4;
    u16 saved = DSW(0x478E);
    DSW(0x4788) = (u16)(DSW(0x4788) + 1);                        /* campaigns completed */
    if (DSW(0x477B) < end) { DSW(0x477F) = end; DSW(0x477B) = end; }
    DSW(0x478E) = 0xFFFF;
    if (!wounded && !jump) events_run(DSW(DAY), DSW(0x477B), 0);
    DSW(0x478E) = saved;
    u16 mk = stk_mark();
    u16 buf = stk_alloc(0x68);
    FarPtr t = misc_tour_text(buf);
    tbox_end_of_campaign(t.off, t.seg);
    stk_release(mk);
    cmptext_open();
    campsel_show_text(cmptext_end((s16)DSW(0x4790)));             /* "CAMPAIGN REVIEW" */
}

/* PS 0874:11a9 career_war_over -> u8 */
static u8 career_war_over(void)
{
    u8 w = (s16)(s8)campaign_available_after(DSW(0x477B), (s8)DSB(0x4776)) == 0;
    if (w) {
        u16 e = (u16)enddate_get((s8)DSB(0x4779), 1);
        events_run(DSW(0x477B) < e ? DSW(0x477B) : e, e, 0);     /* e.g. WARENDA */
    }
    return w;
}

/* PS 0874:11ff career_next_campaign(w) -> u8 */
static u8 career_next_campaign(u8 w)
{
    u8 c = (u8)cset_scan((u16)(s16)(s8)DSB(0x4776), DSW(0x477B), DSB(0x4779));
    u8 d = (u8)career_decision(w, c);
    if (d == 1) {                                                /* transfer to another campaign set */
        DSB(0x4779) = (u8)cset_choose(0);
        d = DSB(0x4779) != 0xFF ? 2 : 0;
    }
    if (d == 0) {                                                /* retire */
        DSB(0x4778) = 1;
        best_career_insert();
        return 0;
    }
    u8 r = (u8)campsel_run(0);
    DSB(CAMP_LOADED) = 0;
    return r;
}

/* PS 0874:1270 career_campaign_end(end) -> u8 */
static u8 career_campaign_end(u16 end)
{
    if (DSB(SQ_LOADED)) { squad_close(); DSB(SQ_LOADED) = 0; }
    career_tour_end(end);
    u8 w = career_war_over();
    cmptext_close();
    campaign_close();
    u8 r = career_next_campaign(w);
    if ((s8)r == 0) DSW(0x4790) = 0xFFFF;
    return r;
}

/* PS 0874:12be career_after_mission(squad_done) -> 1: fly the next mission (flight deck) */
s8 career_after_mission(s8 squad_done)
{
    s16 sq = -1;
    set_exit_hook(codeptr_far(PS_FN_career_exit_hook));
    campaign_load((s8)DSB(0x4779));
    DSB(CAMP_LOADED) = 1;
    u16 end = campaign_end((s16)DSW(0x4790));
    u8 cont = DSW(0x477B) < end;
    if (squad_done && cont) {
        sq = career_pick_squadron(end);
        cont = sq > -1;
    }
    if (cont) career_continue(sq);
    else cont = career_campaign_end(end);
    pilot_store();
    clear_exit_hook();
    return (s8)cont;
}

/* ================================================================== PS 1656: notifications */

/* PS 1656:0008 note_new(kind, str_id) -> 5-byte record {u8 kind, far text} */
u16 note_new(u16 kind, u16 str_id)
{
    u16 rec = xmalloc(5);
    DSB(rec) = (u8)kind;
    FarPtr s = strings_get(0, (s16)str_id);
    u16 n = (u16)(rtl__fstrlen(s) + 1);
    far_wr(cur_dgroup, (u16)(rec + 1), xfarmalloc((u32)(s32)(s16)n));
    fmemcpy(far_rd(cur_dgroup, (u16)(rec + 1)), s, n);
    return rec;
}

/* PS 1656:006f note_free(rec) */
static void note_free(u16 rec)
{
    FarPtr t = far_rd(cur_dgroup, (u16)(rec + 1));
    if (!far_is_null(t)) xfarfree(t);
    xfree(rec);
}

/* PS 1656:0095 note_show(rec) — shows and frees it */
void note_show(u16 rec)
{
    FarPtr t = far_rd(cur_dgroup, (u16)(rec + 1));
    switch ((u16)((s16)(s8)DSB(rec) - 1)) {
    case 0:                                                      /* killed in action */
        snd_play(0x3EC);
        tbox_kia(t);
        snd_stop(0x3EC);
        break;
    case 1:                                                      /* captured */
        award_ceremony((s8)DSB(0x4775), 0, t);
        snd_stop(-1);
        break;
    case 2:                                                      /* hospital */
        award_ceremony((s8)DSB(0x4775), 1, t);
        snd_stop(-1);
        break;
    case 3:                                                      /* board of inquiry */
        snd_play(0x3F7);
        tbox_inquiry(t);
        snd_stop(0x3F7);
        break;
    case 4:                                                      /* medal */
        medabbr_open();
        medals_open();
        award_ceremony((s8)DSB(0x4775), 2, t);
        medabbr_close();
        medals_close();
        snd_stop(-1);
        break;
    case 5:                                                      /* promotion */
        ranks_load();
        award_ceremony((s8)DSB(0x4775), 3, t);
        ranks_free();
        snd_stop(-1);
        break;
    default:
        break;
    }
    note_free(rec);
}

__attribute__((constructor)) static void events_codeptrs(void)
{
    UREG(event_compare);
    UREG(event_ttm_text_hook);
    UREG(career_exit_hook);
}
