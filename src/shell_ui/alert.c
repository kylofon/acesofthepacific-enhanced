/* shell_ui: the hot-spot manager ("alert.c", PS segment 2b4f) — shell_ui_toolkit.md §1. */
#include "shell_ui/sui_a_priv.h"

/* PS 2b4f:0007 ui_set_pending_palette — toolkit §1.2 */
void ui_set_pending_palette(FarPtr p) { ds_far_wr(T_PEND_PAL, p); }

/* PS 2b4f:0019 cursor_show */
void cursor_show(void) { DSW(T_CUR_VIS) = 1; }

/* PS 2b4f:0024 cursor_hide */
void cursor_hide(void) { DSW(T_CUR_VIS) = 0; }

/* PS 2b4f:002f cursor_toggle */
void cursor_toggle(void) { DSW(T_CUR_VIS) = DSW(T_CUR_VIS) == 0; }

/* PS 2b4f:003f cursor_set(frame) -> 1, 0 when the save buffer cannot be allocated */
u16 cursor_set(u16 frame)
{
    DSW(T_CUR_FRAME) = 0;
    if (!far_is_null(ds_far(T_CUR_SAVE))) xfarfree(ds_far(T_CUR_SAVE));
    if (frame) {
        u32 n = gfx_rect_size((u16)frame_w(frame), (u16)frame_h(frame));
        FarPtr b = xfarmalloc((u16)n);                 /* (the size is passed as a 16-bit word, 0) */
        ds_far_wr(T_CUR_SAVE, b);
        if (far_is_null(b)) return 0;
    }
    DSW(T_CUR_FRAME) = frame;
    return 1;
}

/* PS 2b4f:009d hs_load_rect: the current rectangle from the current item */
void hs_load_rect(void)
{
    FarPtr it = cur_item();
    DSS(RX) = fs(it, IT_X);
    DSS(RY) = fs(it, IT_Y);
    if (DSW(T_IDX) == 0) {
        DSS(T_ORG_X) = fs(it, IT_X);
        DSS(T_ORG_Y) = fs(it, IT_Y);
    } else {
        DSS(RX) = (s16)(DSS(RX) + DSS(T_ORG_X));
        DSS(RY) = (s16)(DSS(RY) + DSS(T_ORG_Y));
    }
    DSS(RW) = fs(it, IT_W);
    DSS(RH) = fs(it, IT_H);
}

/* PS 2b4f:00e9 hs_load(tbl, idx) */
void hs_load(FarPtr tbl, u16 idx)
{
    if (idx != 0) {
        DSW(T_IDX) = 0;
        ds_far_wr(T_ITEM, tbl);
        hs_load_rect();
    }
    DSW(T_IDX) = idx;
    ds_far_wr(T_ITEM, far_add(tbl, (u16)(idx * IT_SIZE)));
    hs_load_rect();
}

/* PS 2b4f:01ff hs_center_mouse */
void hs_center_mouse(void)
{
    DSS(T_PTR_X) = (s16)(RXs + (RWs >> 1));
    DSS(T_PTR_Y) = (s16)(RYs + (RHs >> 1));
    mouse_move_to(DSS(T_PTR_X), DSS(T_PTR_Y));
}

/* PS 2b4f:012d hs_focus(idx) */
void hs_focus(u16 idx)
{
    s16 i = (s16)idx;
    if (i < 0 || i >= DSS(T_COUNT)) return;
    DSW(T_FOCUS) = idx;
    hs_load(ds_far(T_TBL), idx);
    if (!(fb(cur_item(), IT_FLAGS) & 2)) assert_line(AF_ALERT, 0x92);
    hs_center_mouse();
}

/* PS 2b4f:0186 hs_hit(x, y) — the current item's rectangle or diamond */
u16 hs_hit(u16 x_, u16 y_)
{
    s16 x = (s16)x_, y = (s16)y_;
    if (!(fb(cur_item(), IT_FLAGS) & 8)) {
        if (x < RXs || (s16)(RXs + RWs - 1) < x || y < RYs || (s16)(RYs + RHs - 1) < y) return 0;
        return 1;
    }
    s16 r = (s16)(RWs >> 1);
    s16 dx = (s16)(x - (s16)(RXs + r));
    s16 dy = (s16)(y - (s16)(RYs + r));
    s16 ad = dx < 0 ? (s16)-dx : dx, bd = dy < 0 ? (s16)-dy : dy;
    return (s16)(ad + bd) < r ? 1 : 0;
}

/* PS 2b4f:022e hs_focus_nearest -> 1 when the pointer is on a focusable item */
s16 hs_focus_nearest(void)
{
    if (DSW(T_COUNT) == 0) return 0;
    s16 best = -1, best_i = 0;
    ds_far_wr(T_ITEM, ds_far(T_TBL));
    for (DSW(T_IDX) = 0; DSS(T_IDX) < DSS(T_COUNT); DSW(T_IDX)++) {
        hs_load_rect();
        if (fb(cur_item(), IT_FLAGS) & 2) {
            if (hs_hit(DSW(T_PTR_X), DSW(T_PTR_Y))) {
                DSW(T_FOCUS) = DSW(T_IDX);
                hs_center_mouse();
                return 1;
            }
            s16 dx = (s16)((s16)(RXs + (RWs >> 1)) - DSS(T_PTR_X));
            s16 dy = (s16)((s16)(RYs + (RHs >> 1)) - DSS(T_PTR_Y));
            s16 d = (s16)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
            if (best == -1 || d < best) { best_i = DSS(T_IDX); best = d; }
        }
        ds_far_wr(T_ITEM, far_add(cur_item(), IT_SIZE));
    }
    if (best != -1) {
        DSW(T_FOCUS) = (u16)best_i;
        hs_load(ds_far(T_TBL), (u16)best_i);
        hs_center_mouse();
    }
    return 0;
}

/* PS 2b4f:0317 hs_navigate(dir) */
void hs_navigate(u16 dir)
{
    if (!hs_focus_nearest()) return;
    FarPtr t = ds_far(T_TBL);
    DSW(T_IDX) = fb(t, (u16)(DSS(T_FOCUS) * IT_SIZE + dir + IT_LINKS));
    if (DSW(T_IDX) != 0) {
        DSW(T_FOCUS) = DSW(T_IDX);
        hs_load(t, DSW(T_IDX));
        hs_center_mouse();
    }
}

/* PS 2b4f:035c hs_focus_first */
void hs_focus_first(void)
{
    ds_far_wr(T_ITEM, ds_far(T_TBL));
    for (DSW(T_IDX) = 0; DSS(T_IDX) < DSS(T_COUNT); DSW(T_IDX)++) {
        if (fb(cur_item(), IT_FLAGS) & 2) {
            DSW(T_FOCUS) = DSW(T_IDX);
            hs_load(ds_far(T_TBL), DSW(T_IDX));
            hs_center_mouse();
            return;
        }
        ds_far_wr(T_ITEM, far_add(cur_item(), IT_SIZE));
    }
    DSW(T_ITEM + 2) = DSW(T_TBL + 2);
}

/* PS 2b4f:03b7 hs_set_table(tbl, n) */
void hs_set_table(FarPtr p, u16 n)
{
    if ((s16)n > 40) assert_line(AF_ALERT, 0xFD);
    ds_far_wr(T_TBL, p);
    DSW(T_COUNT) = 0;
    if ((s16)n > 0) {
        for (u16 i = 0; i < n; i++) DSB((u16)(T_HELD + i)) = 0;
        DSW(T_COUNT) = n;
    }
    hs_focus_first();
}

/* PS 2b4f:041b hs_restart */
void hs_restart(void) { DSW(T_RESTART) = 1; }

/* PS 2b4f:0426 hs_dispatch(mx, my, button, redraw) -> DS:5FD2 — toolkit §1.3 */
u16 hs_dispatch(u16 mx, u16 my, u16 button, u16 redraw)
{
    DSW(PG_READ) = DSW(PG_BACK);
    DSW(PG_WRITE) = DSW(PG_BACK);
    DSW(T_LAST_MX) = mx;
    DSW(T_LAST_MY) = my;
    bool any_clickable = false;
    bool click_nothing = button != 0 && DSW(T_PREV_BTN) == 0;
    for (;;) {
        DSW(T_RESTART) = 0;
        DSW(T_IDX) = 0;
        ds_far_wr(T_ITEM, ds_far(T_TBL));
        while (DSS(T_IDX) < DSS(T_COUNT)) {
            FarPtr it = cur_item();
            u16 i = DSW(T_IDX);
            any_clickable |= (fb(it, IT_MASK) & 0x1C) != 0;
            hs_load_rect();
            DSW(T_EVT) = redraw ? 0x40 : 0;
            if (!hs_hit(mx, my)) {
                DSW(T_EVT) |= 2;
                if (DSB((u16)(T_HELD + i))) {
                    DSW(T_EVT) |= 0x22;
                    DSB((u16)(T_HELD + i)) = 0;
                }
            } else {
                if (click_nothing && (fb(it, IT_MASK) & 0x1C)) click_nothing = false;
                if (button == 0) {
                    DSW(T_EVT) |= DSB((u16)(T_HELD + i)) ? 0x11 : 0x01;
                } else if (!DSB((u16)(T_HELD + i))) {
                    DSW(T_EVT) |= 5;
                } else {
                    DSW(T_EVT) |= 9;
                }
                DSB((u16)(T_HELD + i)) = (u8)button;
            }
            if ((DSW(T_EVT) & fb(it, IT_MASK)) == 0 || far_is_null(fp(it, IT_CB))) {
                DSB((u16)(T_DIRTY + i)) = 0;
            } else {
                DSB((u16)(T_DIRTY + i)) = 1;
                if (!(fb(it, IT_FLAGS) & 0x80)) {
                    call_far(fp(it, IT_CB));
                } else {
                    u16 save = fw(it, IT_LABEL);
                    fww(it, IT_LABEL, str_get((s16)save));
                    fbw(it, IT_FLAGS, (u8)(fb(it, IT_FLAGS) ^ 0x80));
                    call_far(fp(it, IT_CB));
                    it = cur_item();
                    fww(it, IT_LABEL, save);
                    fbw(it, IT_FLAGS, (u8)(fb(it, IT_FLAGS) ^ 0x80));
                }
                if (DSW(T_RESTART)) break;
            }
            DSW(T_IDX)++;
            ds_far_wr(T_ITEM, far_add(cur_item(), IT_SIZE));
        }
        if (DSW(T_RESTART) == 0) break;
    }
    DSW(T_RESTART) = redraw;
    if (any_clickable && click_nothing) snd_play(0x1A);
    return DSW(T_DISP_RES);
}

/* PS 2b4f:05f4 cursor_draw(x, y): puts the cursor on the work page, saving what is under it */
void cursor_draw(u16 x_, u16 y_)
{
    s16 x = (s16)x_, y = (s16)y_;
    DSW(PG_READ) = DSW(PG_BACK);
    DSW(PG_WRITE) = DSW(PG_BACK);
    ps_clip_full();
    u16 f = DSW(T_CUR_FRAME);
    if (f && DSW(T_CUR_VIS)) {
        DSS(T_CUR_CW) = (s16)(320 - x);
        if (frame_w(f) <= DSS(T_CUR_CW)) DSS(T_CUR_CW) = frame_w(f);
        DSS(T_CUR_CH) = (s16)(200 - y);
        if (frame_h(f) <= DSS(T_CUR_CH)) DSS(T_CUR_CH) = frame_h(f);
        gfx_get_rect(ds_far(T_CUR_SAVE), x, y, DSS(T_CUR_CW), DSS(T_CUR_CH));
        sprite(f, x, y, 0);
    }
}

/* PS 2b4f:0686 cursor_flush(x, y): moves the cursor on the front page, removes it from the work page */
void cursor_flush(u16 x, u16 y)
{
    DSW(PG_READ) = DSW(PG_BACK);
    DSW(PG_WRITE) = DSW(PG_FRONT);
    if (DSW(T_CUR_W) != 0)
        gfx_copy_rect(DSS(T_CUR_X), DSS(T_CUR_Y), DSS(T_CUR_W), DSS(T_CUR_H));
    if (DSW(T_CUR_FRAME) == 0 || DSW(T_CUR_VIS) == 0) {
        DSW(T_CUR_W) = 0;
    } else {
        DSW(T_CUR_W) = DSW(T_CUR_CW);
        DSW(T_CUR_H) = DSW(T_CUR_CH);
        DSW(T_CUR_Y) = y;
        DSW(T_CUR_X) = x;
        gfx_copy_rect((s16)x, (s16)y, DSS(T_CUR_CW), DSS(T_CUR_CH));
        DSW(PG_WRITE) = DSW(PG_BACK);
        gfx_put_rect(ds_far(T_CUR_SAVE), DSS(T_CUR_X), DSS(T_CUR_Y), DSS(T_CUR_W), DSS(T_CUR_H));
    }
}

/* PS 2b4f:0719 hs_present — toolkit §1.4 */
void hs_present(void)
{
    cursor_draw(DSW(T_LAST_MX), DSW(T_LAST_MY));
    if (DSW(T_RESTART) == 0 && DSB(T_DIRTY) == 0) {
        DSW(PG_READ) = DSW(PG_BACK);
        DSW(PG_WRITE) = DSW(PG_FRONT);
        DSW(T_IDX) = 0;
        ds_far_wr(T_ITEM, ds_far(T_TBL));
        while (DSS(T_IDX) < DSS(T_COUNT)) {
            hs_load_rect();
            if (DSB((u16)(T_DIRTY + DSW(T_IDX)))) {
                if ((s16)(RXs + RWs) > 319) assert_line(AF_ALERT, 0x1BD);
                if ((s16)(RYs + RHs) > 199) assert_line(AF_ALERT, 0x1BE);
                gfx_copy_rect(RXs, RYs, (s16)(RWs + 1), (s16)(RHs + 1));
            }
            DSW(T_IDX)++;
            ds_far_wr(T_ITEM, far_add(cur_item(), IT_SIZE));
        }
    } else {
        gfx_flip(0);
        FarPtr pal = ds_far(T_PEND_PAL);
        if (!far_is_null(pal)) {
            pal_set(pal);
            scr_pal_release(pal.off, pal.seg);
            ds_far_wr(T_PEND_PAL, fnul());
        }
        copy_front_to_back();
    }
    cursor_flush(DSW(T_LAST_MX), DSW(T_LAST_MY));
}

/* PS 2b4f:0852 hs_default_links(tbl, n): a ring over the focusable items */
void hs_default_links(FarPtr p, u16 n_)
{
    s16 n = (s16)n_;
    for (s16 i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) fbw(p, (u16)(i * IT_SIZE + IT_LINKS + k), 0);
    s16 prev = -1, first = -1;
    for (s16 i = 0; i < n; i++) {
        if (!(fb(p, (u16)(i * IT_SIZE + IT_FLAGS)) & 2)) continue;
        if (first == -1) first = i;
        if (prev != -1) {
            u16 pi = (u16)(prev * IT_SIZE);
            fbw(p, (u16)(pi + 0x15), (u8)i);
            fbw(p, (u16)(pi + 0x13), (u8)i);
            fbw(p, (u16)(i * IT_SIZE + 0x14), (u8)prev);
            fbw(p, (u16)(i * IT_SIZE + 0x12), (u8)prev);
        }
        prev = i;
    }
    if (first != -1 && first != prev) {
        fbw(p, (u16)(prev * IT_SIZE + 0x15), (u8)first);
        fbw(p, (u16)(prev * IT_SIZE + 0x13), (u8)first);
        fbw(p, (u16)(first * IT_SIZE + 0x14), (u8)prev);
        fbw(p, (u16)(first * IT_SIZE + 0x12), (u8)prev);
    }
}

/* PS 2b4f:0959 hs_load_layout(name, tbl, n): rectangles from <name>.BOX, links from <name>.BL */
void hs_load_layout(u16 name_ds, FarPtr tbl, s16 n)
{
    u16 m = stk_mark();
    u16 rec = stk_alloc(8), lk = stk_alloc(4);
    u16 f = my_fopen(make_name(name_ds, -1, 0x200E /* "box" */), 0x2012 /* "rb" */);
    if (!f) assert_line(AF_ALERT, 0x1F7);
    if ((u16)((u16)my_filelength(f) >> 3) != (u16)n) assert_line(AF_ALERT, 0x1F9);
    for (s16 i = 0; i < n; i++) {
        ds_far_wr(T_ITEM, far_add(tbl, (u16)(i * IT_SIZE)));
        my_fread(rec, 8, 1, f);
        FarPtr it = cur_item();
        for (int k = 0; k < 4; k++) fww(it, (u16)(2 * k), DSW((u16)(rec + 2 * k)));
    }
    my_fclose(f);
    f = my_fopen(make_name(name_ds, -1, 0x2015 /* "bl" */), 0x2018 /* "rb" */);
    if (!f) {
        hs_default_links(tbl, (u16)n);
    } else {
        if ((u16)((u16)my_filelength(f) >> 2) != (u16)n) assert_line(AF_ALERT, 0x209);
        for (s16 i = 0; i < n; i++) {
            my_fread(lk, 1, 4, f);
            ds_far_wr(T_ITEM, far_add(tbl, (u16)(i * IT_SIZE)));
            for (int k = 0; k < 4; k++) fbw(cur_item(), (u16)(IT_LINKS + k), DSB((u16)(lk + k)));
        }
        my_fclose(f);
    }
    stk_release(m);
}

/* PS 2b4f:0b15 hs_push: one-level save of the table state */
void hs_push(void)
{
    DSW(0x5F6A) = DSW(T_PTR_X);
    DSW(0x5F68) = DSW(T_PTR_Y);
    DSW(0x5F66) = DSW(T_ITEM + 2);
    DSW(0x5F64) = DSW(T_ITEM);
    DSW(0x5F62) = DSW(T_IDX);
    DSW(0x5F60) = DSW(T_FOCUS);
    DSW(0x5F5E) = DSW(T_TBL + 2);
    DSW(0x5F5C) = DSW(T_TBL);
    DSW(0x5F5A) = DSW(T_COUNT);
    DSW(0x5F58) = DSW(T_DISP_RES);
    DSW(0x5F56) = menu_get_result();
}

/* PS 2b4f:0b62 hs_pop */
void hs_pop(void)
{
    hs_set_table(far_make(DSW(0x5F5E), DSW(0x5F5C)), DSW(0x5F5A));
    DSW(T_PTR_X) = DSW(0x5F6A);
    DSW(T_PTR_Y) = DSW(0x5F68);
    mouse_move_to(DSS(0x5F6A), DSS(0x5F68));
    DSW(T_ITEM + 2) = DSW(0x5F66);
    DSW(T_ITEM) = DSW(0x5F64);
    DSW(T_IDX) = DSW(0x5F62);
    DSW(T_FOCUS) = DSW(0x5F60);
    DSW(T_DISP_RES) = DSW(0x5F58);
    menu_set_result(DSW(0x5F56));
}

/* PS 2b4f:0bbc ui_busy: busy cursor, work page to the front */
void ui_busy(void)
{
    if (DSW(T_CUR_VIS)) {
        cursor_set(parts(7));
        cursor_draw(DSW(T_PTR_X), DSW(T_PTR_Y));
    }
    copy_back_to_front();
}

/* PS 2b4f:0be7 hs_draw_all: full redraw of every item, arrow cursor, flip */
void hs_draw_all(void)
{
    hs_dispatch(DSW(T_PTR_X), DSW(T_PTR_Y), 0, 1);
    cursor_set(parts(6));
    mouse_move_to(DSS(T_PTR_X), DSS(T_PTR_Y));
    hs_present();
}
