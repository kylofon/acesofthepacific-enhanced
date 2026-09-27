/* shell_ui: text flow ("drawtext.c", PS segment 2dd0) — shell_ui_toolkit.md §3.
 * The word buffer is DS:419A; the pen position pointers of the original (near pointers to stack
 * locals) are host pointers here (they never leave this file). */
#include "shell_ui/sui_a_priv.h"

#define WBUF T_STRBUF

/* PS 2dd0:0009 flow_setup(style, line_h, draw) */
void flow_setup(u16 style, u16 line_h, u16 draw)
{
    DSW(T_FL_STYLE) = style;
    DSW(T_FL_LINEH) = line_h;
    DSB(T_FL_DRAW) = (u8)draw;
}

/* PS 2dd0:0020 flow_line_h */
s16 flow_line_h(void) { return DSS(T_FL_LINEH); }

static void flow_draw(u16 s, s16 x, s16 y)
{
    if (!DSB(T_FL_DRAW)) return;
    if (DSW(T_FL_STYLE) == 0) gfx_text(s, x, y);
    else text_engrave(s, 0xC7, 0xB3, 0xC0, (u16)x, (u16)y);
}

/* PS 2dd0:002a flow_word(w, &x, &y) */
static void flow_word(u16 w, s16 *x, s16 *y)
{
    for (; DSB(w) == ',' || DSB(w) == '.'; w++) {
        u8 c = DSB((u16)(w + 1));
        DSB((u16)(w + 1)) = 0;
        flow_draw(w, *x, *y);
        *x = (s16)(*x + gfx_text_width(w));
        DSB((u16)(w + 1)) = c;
    }
    if (DSS(T_FL_RIGHT) < (s16)(*x + gfx_text_width(w))) {
        *y = (s16)(*y + DSS(T_FL_LINEH));
        *x = DSS(T_FL_LEFT);
    }
    if (DSB(w) == ' ' && *x == DSS(T_FL_LEFT)) {
        while (DSB(w) == ' ') w++;
        gfx_text_width(w);                           /* (result unused) */
    }
    flow_draw(w, *x, *y);
    *x = (s16)(*x + gfx_text_width(w));
}

/* PS 2dd0:0123 flow_emit(buf, &x, &y): the buffer word by word ('$' = new line) */
static void flow_emit(u16 p, s16 *x, s16 *y)
{
    while (DSB(p)) {
        u16 q = rtl_strchr(p, ' ');
        if (!q) q = rtl_strchr(p, 9);
        if (!q) q = rtl_strchr(p, 10);
        if (q) {
            u16 n = (u16)(q + 1);
            u8 c = DSB(n);
            DSB(n) = 0;
            flow_word(p, x, y);
            DSB(n) = c;
            p = n;
            continue;
        }
        q = rtl_strchr(p, '$');
        if (!q) {
            if (DSB(p)) {
                flow_word(p, x, y);
                p = (u16)(p + rtl_strlen(p));
            }
        } else {
            DSB(q) = 0;
            flow_word(p, x, y);
            p = (u16)(q + 1);
            *y = (s16)(*y + DSS(T_FL_LINEH));
            *x = DSS(T_FL_LEFT);
        }
    }
}

static bool is_space(u8 c) { return (DSB((u16)(0x3CD1 + (s16)(s8)c)) & 1) != 0; }   /* _ctype */

/* PS 2dd0:01eb flow_text(txt, nstr, subst) -> height in pixels (byte): nstr consecutive strings
 * flowed into the current rectangle; %c calls subst(c) */
u16 flow_text(FarPtr txt, s8 nstr, FarPtr subst)
{
    s16 y0 = RYs, y = RYs, x = RXs;
    DSS(T_FL_LEFT) = RXs;
    DSS(T_FL_RIGHT) = (s16)(RXs + RWs - 1);
    FarPtr p = txt;
    for (s16 k = 0; k < nstr; k++) {
        u16 si = WBUF;
        if (k != 0) DSB(si++) = ' ';
        while (far_mp(p)[0] != 0) {
            u8 c = far_mp(p)[0];
            if (c == '%') {
                if (far_is_null(subst)) goto copy;
                p = far_add(p, 1);
                if (far_mp(p)[0] != 0) {
                    s8 ch = (s8)far_mp(p)[0];
                    p = far_add(p, 1);
                    FarPtr r = ((UiSubstFn)codeptr_lookup(subst))(ch);
                    if (!far_is_null(r))
                        for (; far_mp(r)[0]; r = far_add(r, 1)) DSB(si++) = far_mp(r)[0];
                }
                continue;
            }
            if (c == '$' || (is_space(c) && si != WBUF)) {
                DSB(si++) = c;
                p = far_add(p, 1);
                DSB(si) = 0;
                flow_emit(WBUF, &x, &y);
                si = WBUF;
                continue;
            }
            if (is_space(c)) {
                while (is_space(far_mp(p)[0])) p = far_add(p, 1);
                continue;
            }
        copy:
            DSB(si++) = far_mp(p)[0];
            p = far_add(p, 1);
        }
        DSB(si) = 0;
        flow_emit(WBUF, &x, &y);
        p = far_add(p, (u16)(rtl__fstrlen(p) + 1));
    }
    if (x > DSS(T_FL_LEFT)) y = (s16)(y + DSS(T_FL_LINEH));
    return (u8)((u8)y - (u8)y0);
}

/* PS 2dd0:0355 flow_text_buttons(tbl, first, subst, strings, x, y, right) -> lines: a paragraph whose
 * %c markers either insert text or become inline buttons (items first + c - 'a' of tbl) */
u16 flow_text_buttons(FarPtr tbl, s16 first, FarPtr subst, u16 strings, s16 x, s16 y, s16 right)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(300);
    s16 k = 0, y0 = y;
    gfx_font_select((s16)DSW(T_FONT3));
    DSB(TXT_FLAGS) = 1;
    DSS(T_FL_LEFT) = x;
    DSS(T_FL_RIGHT) = right;
    flow_setup(0xFFFD, 11, 1);
    while (DSW((u16)(strings + k * 2)) != 0) {
        rtl_strcpy(buf, DSW((u16)(strings + k * 2)));
        u16 di = buf, si = WBUF;
        if (k != 0) DSB(si++) = ' ';
        k++;
        while (DSB(di)) {
            u8 c = DSB(di);
            if (c == '%') {
                if (si != WBUF) {
                    DSB(si) = 0;
                    flow_emit(WBUF, &x, &y);
                    si = WBUF;
                }
                di++;
                if (DSB(di) == 0) assert_line(AF_DRAWTEXT, 0x123);
                s8 ch = (s8)DSB(di);
                FarPtr item = far_add(tbl, (u16)(first * IT_SIZE + (s16)(ch - 'a') * IT_SIZE));
                u16 r = ((UiSubst2Fn)codeptr_lookup(subst))(ch, item).off;
                di++;
                if (r) {
                    for (; DSB(r); r++) DSB(si++) = DSB(r);
                } else {
                    s16 tw = (s16)(gfx_text_width(fw(item, IT_LABEL)) + 5);
                    if ((s16)(x + tw) > DSS(T_FL_RIGHT)) {
                        y = (s16)(y + DSS(T_FL_LINEH));
                        x = DSS(T_FL_LEFT);
                    }
                    fww(item, IT_X, (u16)(x - 1 - fs(tbl, IT_X)));
                    fww(item, IT_Y, (u16)(y - 3 - fs(tbl, IT_Y)));
                    fww(item, IT_W, (u16)(tw - 1));
                    fww(item, IT_H, (u16)(DSS(T_FL_LINEH) - 2));
                    fww(item, IT_USER, 1);
                    fbw(item, IT_MASK, 3);
                    x = (s16)(x + tw);
                }
            } else if (c == '$' || (c == ' ' && si != WBUF)) {
                DSB(si++) = c;
                di++;
                DSB(si) = 0;
                flow_emit(WBUF, &x, &y);
                si = WBUF;
            } else {
                DSB(si++) = c;
                di++;
            }
        }
        DSB(si) = 0;
        flow_emit(WBUF, &x, &y);
    }
    if (DSS(T_FL_LEFT) < x) y = (s16)(y + DSS(T_FL_LINEH));
    stk_release(m);
    return (u16)((s16)(y - y0) / DSS(T_FL_LINEH));
}
