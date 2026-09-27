/* shell_ui: the event loop and input ("menu.c", PS segment 2e25) — shell_ui_toolkit.md §4. */
#include "shell_ui/sui_a_priv.h"

/* PS 2e25:0008 menu_nav_off */
void menu_nav_off(void) { DSB(T_NAV_ON) = 0; }

/* PS 2e25:0012 menu_nav_on */
void menu_nav_on(void) { DSB(T_NAV_ON) = 1; }

static s16 navkey_index(void)
{
    s16 i;
    for (i = 0; i < 6; i++)
        if (DSB((u16)(T_NAVKEYS + i)) == DSB(T_SCAN)) break;
    return i;
}

/* PS 2e25:001c menu_key_nav: arrows / Tab move the focus */
void menu_key_nav(void)
{
    s16 i = navkey_index();
    if (i < 6) hs_navigate(DSB((u16)(T_NAVDIRS + i)));
}

/* PS 2e25:0049 menu_poll — toolkit §4.1 */
void menu_poll(void)
{
    host_pump();                     /* PORT: every busy loop of the shell polls through here */
    DSW(T_PREV_BTN) = DSW(T_BTN);
    u16 k = key_read();
    DSB(T_ASCII) = (u8)k;
    DSB(T_SCAN) = (u8)(k >> 8);
    if (DSB(T_SCAN) == 0x0F && (key_held(0x2A) || key_held(0x36))) DSB(T_SCAN) = 0x29;   /* Shift-Tab */
    kbd_dir_bits(0);
    if (DSB(T_SPACE_TYPES) == 0) DSW(T_BTN) = (key_held(0x1C) || key_held(0x39)) ? 1 : 0;
    else DSW(T_BTN) = (u16)key_held(0x1C);
    if (DSB(PS_DS_joy_on)) {
        DSW(T_BTN) = (DSW(T_BTN) || joy_button(0) || joy_button(1)) ? 1 : 0;
        u16 len;
        if (DSB(T_SCAN) == 0) {
            u8 d = (u8)joy_dir_bits(0);
            if (d == 0 || !tmr_expired(T_JOY_TMR)) goto mouse;
            if (d & 2) DSB(T_SCAN) = 0x50;
            else if (d & 1) DSB(T_SCAN) = 0x48;
            else if (d & 4) DSB(T_SCAN) = 0x4B;
            else if (d & 8) DSB(T_SCAN) = 0x4D;
            len = 0x2D;
        } else {
            len = 0;
        }
        tmr_set(T_JOY_TMR, len);
    }
mouse:
    if (DSB(PS_DS_mouse_on)) {
        mouse_get_pos_dos3(T_PTR_X, T_PTR_Y);
        DSW(T_BTN) = (DSW(T_BTN) || mouse_button_dos3(0) || mouse_button_dos3(1)) ? 1 : 0;
    }
    if (navkey_index() < 6) snd_play(0x19);
    if (DSS(T_PTR_X) < 0) DSS(T_PTR_X) = 0;
    else if (DSS(T_PTR_X) > 319) DSS(T_PTR_X) = 319;
    if (DSS(T_PTR_Y) < 0) DSS(T_PTR_Y) = 0;
    else if (DSS(T_PTR_Y) > 199) DSS(T_PTR_Y) = 199;
    if (DSB(PS_DS_mouse_on)) mouse_move_to(DSS(T_PTR_X), DSS(T_PTR_Y));
}

/* PS 2e25:0246 menu_message(s, yes_no) -> 1 (plain) / 'Y' pressed (yes/no): a pop-up at the bottom */
u16 menu_message(u16 s, u8 yes_no)
{
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FLAGS) = 1;
    RWs = (s16)(gfx_text_width(s) + 10);
    RHs = 20;
    RXs = (s16)(160 - (RWs >> 1));
    RYs = 170;
    rect_grow(3);
    u32 n = gfx_rect_size((u16)RWs, (u16)RHs);
    FarPtr buf = xfarmalloc((u16)n);
    if (far_is_null(buf)) assert_line(AF_MENU, 0x9D);
    DSW(PG_READ) = DSW(PG_BACK);
    gfx_get_rect(buf, RXs, RYs, RWs, RHs);
    DSW(PG_WRITE) = DSW(PG_BACK);
    ps_rect_shrink(3);
    draw_popup_box();
    DSB(TXT_FG) = 0xC5;
    text_engrave(s, 0xC7, 0xB3, 0xC0, ps_text_center_x(s, (u16)RXs, (u16)RWs), (u16)(RYs + 7));
    rect_grow(3);
    DSW(PG_WRITE) = DSW(PG_FRONT);
    rect_copy();
    menu_flush();
    if (yes_no == 0) {
        do {
            menu_poll();
            if (DSB(T_SCAN) != 0) break;
        } while (DSW(T_BTN) == 0);
        while (DSW(T_BTN) != 0) menu_poll();
    } else {
        do {
            menu_poll();
            if (DSB(T_SCAN) == 0x15) break;
        } while (DSB(T_SCAN) != 0x31);
    }
    DSW(PG_WRITE) = DSW(PG_BACK);
    gfx_put_rect(buf, RXs, RYs, RWs, RHs);
    DSW(PG_READ) = DSW(PG_BACK);
    DSW(PG_WRITE) = DSW(PG_FRONT);
    rect_copy();
    xfarfree(buf);
    cursor_draw(DSW(T_LAST_MX), DSW(T_LAST_MY));
    cursor_flush(DSW(T_LAST_MX), DSW(T_LAST_MY));
    return yes_no == 0 ? 1 : DSB(T_SCAN) == 0x15;
}

/* PS 2e25:0419 menu_hotkey(scan) -> handled: Alt/Ctrl combinations */
u16 menu_hotkey(u16 scan)
{
    if (key_held(0x38)) {                              /* Alt: table 2e25:04C3 */
        switch (scan) {
        case 0x10: case 0x2D: set_quit_flag(); break;  /* Q, X */
        case 0x14: snapshot(); break;                  /* T */
        case 0x1F: pref_toggle(2); break;              /* S */
        case 0x20: pref_toggle(1); break;              /* D */
        case 0x24: pref_toggle(0); break;              /* J */
        case 0x32: pref_toggle(3); break;              /* M */
        case 0x2E: joy_recheck(); break;               /* C */
        case 0x2F: menu_message(T_VERSION, 0); break;  /* V */
        default: break;
        }
        return 1;
    }
    if (!key_held(0x1D)) return 0;
    if (scan == 0x10) set_quit_flag();
    else if (scan == 0x14) snapshot();
    else if (scan == 0x2F) menu_message(T_VERSION, 0);
    return 1;
}

/* PS 2e25:04e7 menu_set_result */
void menu_set_result(u16 v) { DSW(T_RESULT) = v; }

/* PS 2e25:04f2 menu_get_result */
u16 menu_get_result(void) { return DSW(T_RESULT); }

/* PS 2e25:04fc menu_run(frame) -> result (1 OK, 2 Esc) — toolkit §4.3 */
u16 menu_run(FarPtr frame)
{
    DSW(T_RESULT) = 0;
    menu_flush();
    while (menu_busy()) { }
    DSB(T_SCAN) = 0;
    DSB(T_ASCII) = 0;
    DSW(T_BTN) = 0;
    if (!far_is_null(frame)) call_far(frame);
    DSW(T_RESULT) = 0;
    while (DSW(T_RESULT) == 0) {
        if (win_detect() && DSB(0x4F6D) && !DSB(0x4F6C)) win_task_switch();
        else DSB(0x4F6D) = 0;
        if (DSB(PS_DS_quit_flag)) {
            if (menu_message(0x2071 /* "Exit to DOS? (Y/N)" */, 1)) shell_exit(0);
            else DSB(PS_DS_quit_flag) = 0;
        }
        menu_poll();
        if (DSB(T_SCAN) == 1) {
            menu_set_result(2);
        } else {
            if (DSB(T_NAV_ON)) menu_key_nav();
            if (menu_hotkey(DSB(T_SCAN))) {
                DSB(T_SCAN) = 0;
                DSB(T_ASCII) = 0;
            }
        }
        if (!far_is_null(frame)) call_far(frame);
    }
    return DSW(T_RESULT);
}

/* PS 2e25:05d7 menu_busy -> button down or Esc */
u16 menu_busy(void)
{
    menu_poll();
    menu_hotkey(DSB(T_SCAN));
    return (DSW(T_BTN) == 0 && DSB(T_SCAN) != 1) ? 0 : 1;
}

/* PS 2e25:0602 menu_wait_timer: waits for the PIT countdown unless input interrupts */
void menu_wait_timer(void)
{
    while (timer_countdown() != 0 && menu_busy() == 0) { }
    timer_countdown();
}

/* PS 2e25:0621 menu_flush */
void menu_flush(void)
{
    while (key_read() != 0) { }
    DSW(T_BTN) = 0;
    DSW(T_PREV_BTN) = 0;
    DSB(T_ASCII) = 0;
    DSB(T_SCAN) = 0;
}

/* PS 2e25:063f menu_delay(n): n BIOS ticks */
void menu_delay(s16 n)
{
    DSW(PS_DS_timer_countdown) = (u16)(n * 13);
    menu_flush();
    menu_wait_timer();
}

__attribute__((constructor)) static void menu_codeptrs(void)
{
    REG(menu_nav_off);
    REG(menu_nav_on);
}
