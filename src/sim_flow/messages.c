/* sim_flow: english.c strings (0ab0, 4bb7:007f), message lines (1614, 6939), radio message text
 * (1815) and repeating warnings (1ba4) — sim_flow_cockpit.md §6. 4bb7:0000 (fread_far) is platform's
 * (platform/loaders.c). */
#include "sim_flow/sfa_priv.h"

#define ASSERT_ENGLISH_C  far_make(SEG(0x3852), 0x0000)
#define ASSERT_ENGLISH_C2 far_make(SEG(0x3852), 0x0020)

/* ---- 0ab0 / 4bb7: STR_POOL ------------------------------------------------------------------------- */

/* AP 0ab0:000b str(group, id) -> a near copy of the string (DS:ax) */
FarPtr str(s16 group, s16 id)
{
    FarPtr base = pool_map_save(DSW(0x3F20));
    u16 gi = rd16(base.seg, (u16)(base.off + 2 * group));
    u16 off = rd16(base.seg, (u16)(base.off + 2 * (u16)(gi + id)));
    u16 s = far_to_near_str(far_make(base.seg, (u16)(base.off + off)));
    pool_unmap(DSW(0x3F20));
    return far_make(cur_dgroup, s);
}

/* AP 0ab0:0066 */
FarPtr str0(s16 id)
{
    return str(0, id);
}

/* AP 0ab0:0080 word_wrap(s, width) -> lines; '\n' and the break spaces become NULs */
s16 word_wrap(u16 s, s16 width)
{
    s16 lines = 1;
    u16 si = s, brk = s, line = s;
    for (; DSB(si); si++) {
        if (DSB(si) == '\n') {
            DSB(si) = 0;
            brk = si;
            line = (u16)(si + 1);
            lines++;
        } else if (DSB(si) == ' ') {
            DSB(si) = 0;
            if (gfx_text_width(line) > width) {
                if (DSB(brk) == ' ') DSB(brk) = 0;
                line = (u16)(brk + 1);
                lines++;
                if (gfx_text_width(line) >= width) assert_fail(ASSERT_ENGLISH_C2, 0xDB, line);
            } else {
                brk = si;
            }
            DSB(si) = ' ';
        }
    }
    if (gfx_text_width(line) > width) {
        DSB(brk) = 0;
        lines++;
    }
    return lines;
}

/* AP 4bb7:007f */
void str_pool_load(void)
{
    u16 m = stk_mark();
    u16 sz = stk_alloc(2);
    u16 f = my_fopen(res_name(0x0246 /* "str_pool" */, 0x024F /* "dat" */), 0x05EB /* "rb" */);
    if (!f) assert_fail(ASSERT_ENGLISH_C, 0x5C, 0);
    my_fread(sz, 2, 1, f);
    DSW(0x3F20) = ap_pool_alloc_1d32(1, DSW(sz));
    FarPtr p = pool_map_save(DSW(0x3F20));
    s32 n = (s32)(u32)DSW(sz);
    if (ap_fread_far_4bb7(p, 1, n, f) != n) assert_fail(ASSERT_ENGLISH_C, 0x65, 0);
    pool_unmap(DSW(0x3F20));
    my_fclose(f);
    stk_release(m);
}

/* ---- 1614: message lines --------------------------------------------------------------------------- */

#define MSG_SLOTS 0x5B1A           /* 2 x 21 bytes */

/* AP 1614:000e */
void msg_init(void)
{
    memset(DSP(MSG_SLOTS), 0, 0x2A);
}

/* AP 1614:0023 */
void msg_tick(void)
{
    u16 s = MSG_SLOTS;
    for (int i = 0; i < 2; i++, s = (u16)(s + 0x15)) {
        if (DSW(s + 0x0A) == 0) continue;
        timer16_dec((u16)(s + 8));
        if (DSS(s + 8) <= 0) DSW(s + 0x0A) = 0xFFFF;
    }
}

/* AP 1614:005b */
void msg_draw(s16 redraw)
{
    u16 s = MSG_SLOTS;
    for (int i = 0; i < 2; i++, s = (u16)(s + 0x15)) {
        s16 prio = DSS(s + 0x0A);
        if (prio == 0 || prio == -1) continue;
        if (DSB(s + 0x0C) || redraw == 2) {
            DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
            msg_box(s);
            if (DSB(s + 0x0C) == 2) {
                if (DSS(s + 0x0D) < DSS(0x3BC9)) DSW(s + 0x0D) = DSW(0x3BC9);
                if (DSS(s + 0x0F) < DSS(0x3BC7)) DSW(s + 0x0F) = DSW(0x3BC7);
                if (DSS(s + 0x11) < DSS(0x3BC5)) DSW(s + 0x11) = DSW(0x3BC5);
                if (DSS(s + 0x13) < DSS(0x3BC3)) DSW(s + 0x13) = DSW(0x3BC3);
            } else {
                DSW(s + 0x0D) = DSW(0x3BC9);
                DSW(s + 0x0F) = DSW(0x3BC7);
                DSW(s + 0x11) = DSW(0x3BC5);
                DSW(s + 0x13) = DSW(0x3BC3);
            }
            DSB(s + 0x0C) = 0;
        } else {
            DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
            DSW(0x26D6) = DSW(AP_DS_drv_page_front);
            gfx_copy_rect(DSS(s + 0x0D), DSS(s + 0x0F), DSS(s + 0x11), DSS(s + 0x13));
        }
    }
}

/* AP 1614:019a */
void msg_erase(void)
{
    if (DSW(0x18CE)) DSW(0x18CE) = (u16)msg_unbox(0);
    u16 s = MSG_SLOTS;
    for (int i = 0; i < 2; i++, s = (u16)(s + 0x15)) {
        if (DSS(s + 0x0A) == -1) {
            DSW(0x18CE) = (u16)msg_unbox(s);
            DSW(s + 0x0A) = 0;
            return;
        }
        if (DSB(s + 0x0C) != 2) continue;
        if ((s8)DSB(0x08D5) && DSS(s + 0x0F) >= 0x64) continue;
        u16 rd = DSW(0x26D6), wr = DSW(AP_DS_drv_page_write);
        DSW(0x26D6) = DSW(0x26D0);
        DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
        gfx_copy_rect(DSS(s + 0x0D), DSS(s + 0x0F), DSS(s + 0x11), DSS(s + 0x13));
        if (DSW(AP_DS_view_inside)) gauges_dirty(DSS(s + 0x0F), (s16)(DSS(s + 0x0F) + DSS(s + 0x13)));
        DSW(0x26D6) = rd;
        DSW(AP_DS_drv_page_write) = wr;
    }
}

/* AP 1614:024e msg_show(s, prio, speaker) */
void msg_show(FarPtr s, s16 prio, FarPtr speaker)
{
    if (far_is_null(s)) return;
    u16 slot = prio > 3 ? 0x5B2F : MSG_SLOTS;
    if (DSS(slot + 0x0A) > prio) return;
    u16 k = DSW(0x18D0);
    DSW(0x18D0) = (u16)(k + 1);
    FarPtr t = rtl__fstrcpy(far_make(SEG(0x3A31), (u16)(0x15 + 0x96 * k)), s);
    ds_far_wr(slot, t);
    if (DSW(0x18D0) == 4) DSW(0x18D0) = 0;
    DSW(slot + 8) = DSW(0x18CC);
    ds_far_wr((u16)(slot + 4), speaker);
    DSB(slot + 0x0C) = DSW(slot + 0x0A) ? 2 : 1;
    DSS(slot + 0x0A) = prio;
}

/* AP 1614:02c3 */
void msg_clear_top(void)
{
    DSW(0x5B24) = 0xFFFF;
}

/* AP 1614:02ce */
void msg_clear_all(void)
{
    DSW(0x5B39) = 0xFFFF;
    DSW(0x5B24) = 0xFFFF;
}

/* ---- 6939: message boxes ----------------------------------------------------------------------------- */

/* AP 6939:0000 msg_box(slot) — draws the slot's box and text into the write page; box in DS:3BC3.. */
s16 msg_box(u16 slot)
{
    u8 speaker = DSL(slot + 4) != 0;
    gfx_font_select((s16)DSW(0x3F2E));
    u16 s = far_to_near_str(ds_far(slot));
    s16 lines = word_wrap(s, 0x13C);
    if (speaker) lines++;
    DSW(0x3BC3) = (u16)(lines * gfx_font_height((s16)DSW(0x3F2E)) + 2);
    if (DSS(slot + 0x0A) < 4) {
        DSB(0x26C0) = DSS(slot + 0x0A) == 3 ? 0xCF : 0xFF;
        DSW(0x3BC7) = 0;
    } else {
        DSB(0x26C0) = 0xFE;
        DSW(0x3BC7) = (u16)(0xC8 - DSW(0x3BC3));
    }
    DSW(0x3BC5) = 0x140;
    DSW(0x3BC9) = 0;
    rect_save();
    s16 x1 = (s16)(DSS(0x3BC9) + DSS(0x3BC5) - 1);
    s16 y1 = (s16)(DSS(0x3BC7) + DSS(0x3BC3) - 1);
    DSB(AP_DS_drv_draw_col) = 0xC6;
    gfx_line(DSS(0x3BC9), DSS(0x3BC7), x1, DSS(0x3BC7));
    DSB(AP_DS_drv_draw_col) = 0xC9;
    gfx_line(DSS(0x3BC9), y1, x1, y1);
    DSB(AP_DS_drv_draw_col) = 0xC7;
    gfx_line(DSS(0x3BC9), DSS(0x3BC7), DSS(0x3BC9), y1);
    gfx_line(x1, DSS(0x3BC7), x1, y1);
    ap_rect_shrink(1, 0);                         /* 47a8:005d(1): the frame inset by one pixel */
    DSB(0x26CC) = 1;
    DSB(AP_DS_drv_fill_col) = 0xC8;
    DSB(AP_DS_drv_draw_col) = 0xC8;
    gfx_rect(DSS(0x3BC9), DSS(0x3BC7), DSS(0x3BC5), DSS(0x3BC3));
    DSB(0x26C2) = 1;
    DSW(0x3BC7) += 1;
    if (speaker) {
        DSW(0x3BC7) = (u16)(DSW(0x3BC7) + gfx_font_height((s16)DSW(0x3F2E)));
        lines--;
    }
    for (s16 i = 0; i < lines; i++) {
        gfx_text(s, (s16)(DSS(0x3BC9) + 4), DSS(0x3BC7));
        DSW(0x3BC7) = (u16)(DSW(0x3BC7) + gfx_font_height((s16)DSW(0x3F2E)));
        s = (u16)(s + rtl_strlen(s) + 1);
    }
    if (speaker) {
        u16 n = far_to_near_str(ds_far((u16)(slot + 4)));
        u16 len = rtl_strlen(n);
        DSB(n + len) = ':';
        DSB(n + len + 1) = 0;
        gfx_text(n, (s16)(DSS(0x3BC9) + 4), DSS(0x3BC7));
    }
    ap_rect_restore(0);
    return 0;
}

/* AP 6939:020f msg_unbox(slot) -> 1 when a bottom box must be erased again next frame */
s16 msg_unbox(u16 slot)
{
    s16 r;
    DSW(AP_DS_drv_page_write) = DSW(AP_DS_drv_page_back);
    DSW(0x26D6) = DSW(0x26D0);
    if (slot) {
        if (!DSW(AP_DS_view_inside)) return 0;
        DSW(0x51AA) = DSW(slot + 0x0D);
        DSW(0x51AC) = DSW(slot + 0x0F);
        if (DSW(0x51AC)) DSW(0x51AC) = (u16)(DSW(0x51AC) - DSW(slot + 0x13));
        DSW(0x51AE) = DSW(slot + 0x11);
        DSW(0x51B0) = (u16)(DSW(slot + 0x13) << 1);
        r = 1;
    } else {
        r = 0;
    }
    if (!(s8)DSB(0x08D5) || DSS(0x51AC) < 0x64)
        gfx_copy_rect(DSS(0x51AA), DSS(0x51AC), DSS(0x51AE), DSS(0x51B0));
    if (DSW(AP_DS_view_inside)) gauges_dirty(DSS(0x51AC), (s16)(DSS(0x51AC) + DSS(0x51B0)));
    return r;
}

/* ---- 1815: radio message text ----------------------------------------------------------------------- */

/* AP 1815:0004 clock_pos(bearing) -> 1..12 */
static u16 clock_pos(s16 a)
{
    return (u16)((u16)((s16)-a - 0x0AAB) / 0x1556 + 1);
}

/* AP 1815:0019 itoa10(buf, n) */
static u16 itoa10(u16 buf, s32 n)
{
    return rtl_ltoa(n, buf, 10);
}

/* AP 1815:002f pluralize(s). Quirk: it tests s[len] (the NUL) for 's', so an 's' is always appended. */
static u16 pluralize(u16 s)
{
    u16 len = rtl_strlen(s);
    u8 c = 's';
    if (DSB(s + len) == 's') c = '\'';
    DSB(s + len) = c;
    DSB(s + len + 1) = 0;
    return s;
}

/* AP 1815:0068 number_word(buf, n, max): STR(4, n) when n <= max and n <= 12, else digits */
static u16 number_word(u16 buf, s32 n, s16 max)
{
    if ((s32)max >= n && n <= 12) rtl__fstrcpy(ds_ptr(buf), str(4, (s16)n));
    else itoa10(buf, n);
    return buf;
}

/* AP 1815:00ad units_text(buf, n): digits + " UNITS" */
static u16 units_text(u16 buf, s32 n)
{
    itoa10(buf, n);
    memcpy(DSP(buf + rtl_strlen(buf)), DSP(0x1924), 7);
    return buf;
}

/* AP 1815:00dc radio_expand(buf, ctx) -> 1 if escapes remain (one pass) */
s16 radio_expand(u16 buf, u16 c)
{
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x1E);                   /* bp-0BCh */
    u16 out = stk_alloc(0x98);                   /* bp-9Eh */
    u16 src = buf, dst = out, start;
    u16 numflag = 0;
    s16 ch;
    while ((ch = (s8)DSB(src++)) != 0) {
        if (ch == DSS(0x5C8C)) {
            DSB(dst) = 0;
            start = dst;
            s16 code = (s8)DSB(src++);
            switch (code) {
            case 'e': {
                s16 id = -1;
                if (DSSL(c + 2) > 0xFA0) id = 0x1A;
                else if (DSSL(c + 2) < -0xFA0) id = 0x19;
                if (id >= 0) {
                    DSB(dst) = ' ';
                    dst++;
                    rtl__fstrcpy(ds_ptr(dst), str(5, id));
                }
                break;
            }
            case 'n':
                number_word(dst, (s32)(s16)clock_pos(DSS(c)), 0x0C);
                break;
            case 'a':
                rtl__fstrcpy(ds_ptr(dst), ds_ptr(number_word(tmp, (s32)(s16)per4000(DSL(c + 6)), 0x0C)));
                break;
            case '#':
                numflag = src;
                rtl__fstrcpy(ds_ptr(dst), ds_ptr(number_word(tmp, (s32)(s8)DSB(c + 0x0C), 0x0A)));
                break;
            case 'p': {
                u16 model = DSW((u16)(DSW(0x55D2) + 2 * DSB(0x54BC + DSW(c + 0x0A))));
                rtl_strcpy(tmp, (u16)(model + 0x0A));
                if (numflag && (s8)DSB(c + 0x0C) > 1) pluralize(tmp);
                rtl__fstrcpy(ds_ptr(dst), ds_ptr(tmp));
                break;
            }
            case 'o':
                rtl__fstrcpy(ds_ptr(dst), str(5, 0x1C));
                break;
            case 'r': case 'R':
                rtl__fstrcpy(ds_ptr(dst), str(7, (s16)((s8)DSB(0x1912 + player_rec7()) + 0x4B)));
                if (DSB(src - 1) == 'R') DSB(dst) = (u8)rtl_toupper((s8)DSB(dst));
                break;
            case 't':
                rtl__fstrcpy(ds_ptr(dst), ds_far(c + 0x0E));
                break;
            case 'd':
                units_text(dst, DSSL(c + 0x12));
                break;
            case 'b':
                rtl__fstrcpy(ds_ptr(dst), str(7, DSS(c) > 0 ? 0x21 : 0x22));
                break;
            case 'B':
                rtl__fstrcpy(ds_ptr(dst), str(7, DSSL(c + 2) < 0 ? 0x21 : 0x22));
                break;
            case 'E':
                rtl__fstrcpy(ds_ptr(dst), str(7, DSB(PLAYER() + 0x26) ? 0 : 1));
                break;
            case 'w': {
                u16 who = DSW(c + 0x16);
                if (!who) break;
                u16 lead = DSW(PLAYER() + 0xDD) ? DSW(PLAYER() + 0xDD) : PLAYER();
                s16 id;
                if (who == PLAYER()) id = 0x27;
                else if (who == lead) id = 0x24;
                else if (DSW(who + 0xDD) == lead) id = 0x23;
                else {
                    if (DSW(who + 0xDD)) who = DSW(who + 0xDD);
                    id = (s8)DSB(who + 0xC9) < (s8)DSB(lead + 0xC9) ? 0x25 : 0x26;
                }
                rtl__fstrcpy(ds_ptr(dst), str(7, id));
                memcpy(DSP(dst + rtl_strlen(dst)), DSP(0x192B), 4);
                break;
            }
            default:
                break;
            }
            dst = (u16)(start + rtl_strlen(start));
        } else if (ch == DSS(0x5C8A)) {
            DSB(dst++) = '"';
        } else {
            DSB(dst++) = (u8)ch;
        }
    }
    DSB(dst) = 0;
    rtl_strcpy(buf, out);
    u16 len = (u16)(rtl_strlen(buf) + 1);
    s16 r = memchr(DSP(buf), (u8)DSW(0x5C8C), len) != NULL || memchr(DSP(buf), (u8)DSW(0x5C8A), len) != NULL;
    stk_release(m);
    return r;
}

/* AP 1815:0546 radio_show(group, id, ctx) */
void radio_show(s16 group, s16 id, u16 c)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x96);
    rtl__fstrcpy(ds_ptr(buf), str(group, id));
    while (radio_expand(buf, c)) {}
    msg_show(ds_ptr(buf), 5, far_make(0, 0));
    stk_release(m);
}

/* AP 1815:0593 */
void radio_tick(void)
{
    u16 s = 0x5C26;
    for (int i = 0; i < 2; i++, s = (u16)(s + 0x32)) {
        if (DSW(s + 0x1A) == 0) continue;
        if (timer16_dec((u16)(s + 0x1A)) != 0) continue;
        radio_show((s8)DSB(s + 0x1C), DSS(s + 0x18), DSB(s + 0x1D) ? s : 0);
    }
}

/* AP 1815:05e0 radio_queue(group, id, ctx, delay) */
void radio_queue(s16 group, s16 id, u16 c, s16 delay)
{
    if (delay == 0) {
        radio_show(group, id, c);
        return;
    }
    u16 pick = 0;
    s16 best = 0x7D1, found = 0;
    u16 s = 0x5C26;
    for (int i = 0; i < 2; i++, s = (u16)(s + 0x32)) {
        if (DSW(s + 0x1A) == 0) { pick = s; found = 1; break; }
        if (DSS(s + 0x1A) < best) { best = DSS(s + 0x1A); pick = s; }
    }
    if (!found) radio_show(group, DSS(pick + 0x18), pick);   /* quirk: the new group, the old slot as ctx */
    DSS(pick + 0x18) = id;
    DSW(pick + 0x1A) = 0x7D0;
    DSB(pick + 0x1C) = (u8)group;
    DSB(pick + 0x1D) = c != 0;
    if (DSB(pick + 0x1D)) memcpy(DSP(pick), DSP(c), 0x18);
    if (c && DSL(c + 0x0E) != 0) rtl__fstrcpy(ds_ptr((u16)(pick + 0x1E)), ds_far(c + 0x0E));
    if (c) ds_far_wr((u16)(pick + 0x0E), ds_ptr((u16)(pick + 0x1E)));
}

/* AP 1815:06d0 */
void radio_queue5(u16 id, u16 c, u16 delay)
{
    radio_queue(5, (s16)id, c, (s16)delay);
}

/* AP 1815:06e7 — only while the player is alive */
void radio_queue7(u16 id, u16 c, u16 delay)
{
    if (!plane_is_down(PLAYER())) radio_queue(7, (s16)id, c, (s16)delay);
}

/* AP 1815:070e */
void radio_init(void)
{
    FarPtr e = str(5, 0);
    DSS(0x5C8C) = (s8)far_mp(e)[0];
    FarPtr q = str(5, 0x1E);
    DSS(0x5C8A) = (s8)far_mp(q)[0];
}

/* ---- 1ba4: repeating warnings ------------------------------------------------------------------------ */

/* AP 1ba4:0003 */
void warn_init(u16 w, s16 group, s16 id, s16 prio)
{
    DSL(w) = DSL(AP_DS_mission_time);
    DSS(w + 4) = group;
    DSS(w + 6) = id;
    DSS(w + 8) = prio;
}

/* AP 1ba4:0026 */
void warn_tick(u16 w)
{
    if (DSL(w) > DSL(AP_DS_mission_time)) return;
    DSL(w) = DSL(AP_DS_mission_time) + 0x6400;
    msg_show(str(DSS(w + 4), DSS(w + 6)), DSS(w + 8), far_make(0, 0));
}
