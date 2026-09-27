#pragma once
/* shell_ui part A private header (PS.EXE DGROUP 4D08 unless noted): the UI toolkit's globals
 * (shell_ui_toolkit.md §0.3), far-data helpers and the code-pointer callback conventions. Not part of
 * the module interface. */
#include "shell_ui/shell_ui.h"

#include "codeptr.h"
#include "host.h"
#include "platform/platform.h"
#include "platform/rtl.h"
#include "prog.h"
#include "shell_data/shell_data.h"
#include "sound/sound.h"
#include "symbols_ps.h"
#include "video/video.h"
#include "render3d/render3d.h"

/* ---- far data (hot-spot tables live in far data segments of the image) */
static inline FarPtr fd(u16 fseg, u16 off) { return far_make(SEG(fseg), off); }
static inline u8  fb(FarPtr p, u16 o) { return rd8(p.seg, (u16)(p.off + o)); }
static inline u16 fw(FarPtr p, u16 o) { return rd16(p.seg, (u16)(p.off + o)); }
static inline s16 fs(FarPtr p, u16 o) { return (s16)rd16(p.seg, (u16)(p.off + o)); }
static inline FarPtr fp(FarPtr p, u16 o) { return far_rd(p.seg, (u16)(p.off + o)); }
static inline void fbw(FarPtr p, u16 o, u8 v)  { wr8(p.seg, (u16)(p.off + o), v); }
static inline void fww(FarPtr p, u16 o, u16 v) { wr16(p.seg, (u16)(p.off + o), v); }
static inline void fpw(FarPtr p, u16 o, FarPtr v) { far_wr(p.seg, (u16)(p.off + o), v); }
static inline FarPtr fnul(void) { FarPtr p = { 0, 0 }; return p; }
static inline bool far_eq(FarPtr a, FarPtr b) { return a.off == b.off && a.seg == b.seg; }

/* hot-spot item fields (22 bytes, toolkit §1.1) */
enum { IT_X = 0, IT_Y = 2, IT_W = 4, IT_H = 6, IT_CB = 8, IT_MASK = 0x0C, IT_FLAGS = 0x0D,
       IT_LABEL = 0x0E, IT_USER = 0x10, IT_LINKS = 0x12, IT_SIZE = 22 };
static inline FarPtr item_at(u16 fseg, u16 off, int i) { return far_make(SEG(fseg), (u16)(off + IT_SIZE * i)); }

/* ---- toolkit globals (toolkit §0.3) */
#define T_TBL        0x1FFA   /* far: current hot-spot table */
#define T_COUNT      0x1FFE   /* s16: its item count (<= 40) */
#define T_CUR_FRAME  0x2000   /* near: current cursor frame */
#define T_CUR_VIS    0x2002   /* u16: cursor visible */
#define T_CUR_W      0x2004   /* s16: cursor rect last put on the front page: w */
#define T_CUR_SAVE   0x2006   /* far: cursor save buffer */
#define T_PEND_PAL   0x200A   /* far: palette installed by the next full present */
#define T_EDGE_C3    0x201C   /* u16: text_edge replacement colour */
#define T_EDGE_C1    0x201E   /* u16: text_edge "already ink" colour */
#define T_TODAY      0x2020   /* 4 bytes: today's date (m, d, y) for date_make */
#define T_NAVKEYS    0x2024   /* u8[6] Tab, Shift-Tab, Up, Down, Left, Right */
#define T_NAVDIRS    0x202A   /* u8[6] */
#define T_SPACE_TYPES 0x2030  /* u8: Space types a space (text entry), not a button */
#define T_PREV_BTN   0x2031   /* u16 */
#define T_BTN        0x2033   /* u16: button state (mouse / Enter / Space / joystick) */
#define T_NAV_ON     0x2035   /* u8: keyboard navigation */
#define T_VERSION    0x2036   /* char[]: version string */
#define T_SCR_CUR    0x2084   /* s16: screen id whose palette/background is current */
#define T_SCR_CACHED 0x2086   /* s16: main-menu screen cached in page AC00 */
#define T_MM_PAL_ID  0x2097   /* s16: screen id of the cached main-menu palette */
#define T_LB_TOP     0x20FE   /* s16: list top index */
#define T_ED_CUR     0x2100   /* s8: text field cursor */
#define T_ED_LEN     0x2101   /* s8: text field length */
#define T_ED_BLINK   0x2102   /* u8: caret phase */
#define T_ED_INS     0x2103   /* u8: insert mode */
#define T_REQ_OPEN   0x2120   /* u8: request string table pushed */
#define T_FONT_TALK  0x421A
#define T_FONT4      0x421C
#define T_FONT3      0x421E
#define T_FONT2      0x4220
#define T_FONT1      0x4222
#define T_STRBUF     0x419A   /* scratch text buffer (text flow, labels) */
#define T_PUSH       0x5F56   /* hs_push save area 5F56..5F6A */
#define T_CUR_CH     0x5F6C   /* s16: clipped cursor h */
#define T_CUR_CW     0x5F6E   /* s16: clipped cursor w */
#define T_RESTART    0x5F70   /* u16 */
#define T_ORG_Y      0x5F72
#define T_ORG_X      0x5F74
#define T_CUR_H      0x5F76
#define T_CUR_Y      0x5F78
#define T_CUR_X      0x5F7A
#define T_FOCUS      0x5F7C   /* s16 */
#define T_DIRTY      0x5F7E   /* u8[40] */
#define T_HELD       0x5FA6   /* u8[40] */
#define T_LAST_MY    0x5FCE
#define T_LAST_MX    0x5FD0
#define T_DISP_RES   0x5FD2
#define RH           0x5FD4   /* current rectangle h, w, y, x (s16) */
#define RW           0x5FD6
#define RY           0x5FD8
#define RX           0x5FDA
#define T_EVT        0x5FDC   /* u16: event bits of the item being dispatched */
#define T_IDX        0x5FDE   /* s16: index of the current item */
#define T_ITEM       0x5FE0   /* far: current item */
#define T_SYMBOLS    0x5FE4   /* near: SYMBOLS.BMP frame table */
#define T_PARTS      0x5FE6   /* near: PARTS.BMP frame table */
#define T_DIA_Y      0x5FE8   /* s16[4] diamond vertices y */
#define T_DIA_X      0x5FF0   /* s16[4] diamond vertices x */
#define T_SAVE_H     0x5FF8
#define T_SAVE_W     0x5FFA
#define T_SAVE_Y     0x5FFC
#define T_SAVE_X     0x5FFE
#define T_PLATE_BUF  0x6000   /* far: 10x10 save buffer */
#define T_FL_RIGHT   0x6004   /* s16: text flow right margin */
#define T_FL_LEFT    0x6006   /* s16: left margin */
#define T_FL_DRAW    0x6008   /* u8 */
#define T_FL_LINEH   0x6009   /* s16 */
#define T_FL_STYLE   0x600B   /* s16 */
#define T_RESULT     0x600E   /* u16: modal result */
#define T_JOY_TMR    0x6010   /* timer record {u32, u16} */
#define T_SCAN       0x6016   /* u8: last key scan code */
#define T_ASCII      0x6017   /* u8: last key ASCII */
#define T_PTR_Y      0x6018   /* s16 */
#define T_PTR_X      0x601A   /* s16 */
#define T_SCREENS    0x601C   /* near: screen table (n x {char *bg, char *scd}) */
#define T_PARTS_PAL  0x601E   /* far */
#define T_MM_PAL     0x6022   /* far */

/* pages and driver state (video.md; PS DS offsets) */
#define PG_BG     0x2310
#define PG_BACK   0x2312
#define PG_FRONT  0x2314
#define PG_READ   0x2316
#define PG_WRITE  0x2318
#define TXT_FG    0x2300
#define TXT_BG    0x2301
#define TXT_FLAGS 0x2302
#define CLIP_ON   0x2303
#define CLIP_X1   0x2304
#define CLIP_X2   0x2306
#define CLIP_Y1   0x2308
#define CLIP_Y2   0x230A
#define FILL_ON   0x230C
#define FILL_COL  0x230D
#define DRAW_COL  0x230E

/* accessors */
#define RXs  DSS(RX)
#define RYs  DSS(RY)
#define RWs  DSS(RW)
#define RHs  DSS(RH)
static inline FarPtr cur_item(void) { return ds_far(T_ITEM); }
static inline u16 parts(int i) { return DSW((u16)(DSW(T_PARTS) + 2 * i)); }
static inline u16 symbols(int i) { return DSW((u16)(DSW(T_SYMBOLS) + 2 * i)); }
static inline s16 frame_w(u16 f) { return DSS((u16)(f + 6)); }
static inline s16 frame_h(u16 f) { return DSS((u16)(f + 8)); }
static inline void line_c(u8 c, s16 x0, s16 y0, s16 x1, s16 y1) { DSB(DRAW_COL) = c; gfx_line(x0, y0, x1, y1); }
static inline void box_c(u8 c, s16 x, s16 y, s16 w, s16 h)
{
    DSB(FILL_ON) = 1; DSB(FILL_COL) = c; DSB(DRAW_COL) = c; gfx_rect(x, y, w, h);
}
static inline void sprite(u16 f, s16 x, s16 y, s16 flags) { gfx_sprite_flip(f, x, y, flags); }
static inline void assert_line(u16 fseg, s16 line) { assert_fail(far_make(SEG(fseg), 0), line, 0); }
/* the far data segments naming the source files in assertions */
#define AF_ALERT    0x4CAB
#define AF_DRAWTEXT 0x4CAC
#define AF_MENU     0x4CAE
#define AF_SCREEN   0x4CAF
#define AF_SOFT     0x4CB0
#define AF_CHOOSE   0x4CC3
#define AF_LISTBOX  0x4CD4
#define AF_FLTALERT 0x4CE8

/* string table (24e3:021f): DX:AX, the callers keep AX as a near pointer into DGROUP */
static inline u16 str_get(s16 id) { return scd_string(id).off; }

/* A C string copied onto the emulated DGROUP stack (released with the caller's stk mark). */
static inline u16 cstr_stk(const char *s)
{
    u16 n = 0;
    while (s[n]) n++;
    u16 p = stk_alloc((u16)(n + 1));
    for (u16 i = 0; i <= n; i++) DSB((u16)(p + i)) = (u8)s[i];
    return p;
}

/* ---- callback signatures through code pointers (shared with part B):
 *   hot-spot callbacks, frames, "on ok" without argument   void f(void)
 *   chooser / picker "on ok"                                void f(s16 sel)
 *   list-box formatter (text of entry i: AX = near char *)  FarPtr f(s16 i)
 *   list-box custom row                                     void f(s16 i, s16 y)
 *   flow_text substitution (DX:AX far char *)               FarPtr f(s8 c)
 *   flow_text_buttons substitution (AX = near char *)       FarPtr f(s8 c, FarPtr item) */
typedef void   (*UiOkFn)(s16 sel);
typedef FarPtr (*UiFmtFn)(s16 i);
typedef void   (*UiRowFn)(s16 i, s16 y);
typedef FarPtr (*UiSubstFn)(s8 c);
typedef FarPtr (*UiSubst2Fn)(s8 c, FarPtr item);
static inline void call_far(FarPtr f) { CodeFn fn = codeptr_lookup(f); if (fn) fn(); }
#define REG(fn) codeptr_register(PROG_PS, PS_FN_##fn, (CodeFn)(fn))
#define FNP(fn) codeptr_far(PS_FN_##fn)

/* ---- part A functions shared between its files but not in shell_ui.h */
void cursor_toggle(void);
void hs_load_rect(void);
void hs_center_mouse(void);
s16  hs_focus_nearest(void);
void hs_focus_first(void);
void hs_load_layout(u16 name_ds, FarPtr tbl, s16 n);
void glyph_edge_dummy(void);
void text_edge(u16 s, s16 x, s16 y);
void draw_groove(u8 no_bottom);
void ps_tile_fill(s16 tx, s16 ty, s16 tw, s16 th);
void screen_clear(u8 c);
void rect_fill_c1(void);
void draw_title_bar(s8 sym, u16 title);
void draw_frame_lines(s16 x0, s16 y0, s16 x1, s16 y1, s16 bx0, s16 by1, s16 bx1, s16 by);
void draw_frame_inset(s16 n, s16 top);
void draw_plate_corners(s16 top);
void draw_plate_bottom(void);
void draw_title_plate(void);
void draw_button_face(u8 pressed);
void draw_button_up(void);
void draw_button_down(void);
void draw_corner_rivets(void);
void draw_riveted_panel(void);
void draw_small_button_up(void);
void draw_small_button_down(void);
void draw_diamond(u8 fill, u16 phase);
void diamond_label(s16 q, u8 pressed);
u16  diamond_cb(void);
void panel_fill_frame(void);
s16  flow_line_h(void);
void menu_nav_off(void);
void menu_nav_on(void);
void menu_key_nav(void);
u16  menu_message(u16 s, u8 yes_no);
void menu_wait_timer(void);
void menu_delay(s16 n);
void req_close(void);
void req_layout(FarPtr subst, u16 str_id, u8 nstr, s16 extra_h);
u16  dlg_modal(FarPtr tbl, s16 n, s16 focus, FarPtr frame, FarPtr on_ok);
u16  dlg_modal2(FarPtr tbl, s16 n, FarPtr frame, FarPtr on_ok);
void dlg_open(FarPtr tbl, s16 n, s16 focus, u8 links);
void dlg_close(void);
void pick_list(u16 title, s8 n, s8 sel, FarPtr fmt, FarPtr on_ok);
void lbox_draw(void);
void lbox_arrow_cb(void);
void lbox_edit_draw(void);
void lbox_run(void);
void choose_frame(void);
void choose_on_ok(void);
void req_frame(void);
void req_btn_cb(void);
s16  scr_is_parts(s16 id);
FarPtr scr_palette(s16 id);
void scr_draw_bg(s16 id);
void req_init(void);
void flt_init(void);
FarPtr acelist_row_name(s16 row);   /* shell_data/ace.c (PS 2550:0c99, file-local there) */
void entry_init(void);
