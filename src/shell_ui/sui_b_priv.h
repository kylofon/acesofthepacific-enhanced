#pragma once
/* shell_ui part B private helpers (PS.EXE only, DGROUP 4D08): career, roster, flight map/deck,
 * squadron, single-mission set-up, briefing, debrief and award screens. Not part of the module
 * interface; part A (the toolkit) has its own private header. */
#include "shell_ui/shell_ui.h"
#include "shell_data/shell_data.h"
#include "platform/platform.h"
#include "platform/rtl.h"
#include "video/video.h"
#include "sound/sound.h"
#include "render3d/render3d.h"
#include "symbols_ps.h"
#include "codeptr.h"
#include "prog.h"
#include "host.h"

/* ---- far memory accessors (offset arithmetic wraps inside the segment, like far pointers) */
static inline u8  ub(FarPtr p, u16 o) { return rd8(p.seg, (u16)(p.off + o)); }
static inline s8  uc(FarPtr p, u16 o) { return (s8)rd8(p.seg, (u16)(p.off + o)); }
static inline u16 uw(FarPtr p, u16 o) { return rd16(p.seg, (u16)(p.off + o)); }
static inline s16 us(FarPtr p, u16 o) { return (s16)rd16(p.seg, (u16)(p.off + o)); }
static inline u32 ul(FarPtr p, u16 o) { return rd32(p.seg, (u16)(p.off + o)); }
static inline FarPtr ufp(FarPtr p, u16 o) { return far_rd(p.seg, (u16)(p.off + o)); }
static inline void uwb(FarPtr p, u16 o, u8 v)  { wr8(p.seg, (u16)(p.off + o), v); }
static inline void uww(FarPtr p, u16 o, u16 v) { wr16(p.seg, (u16)(p.off + o), v); }
static inline void uwl(FarPtr p, u16 o, u32 v) { wr32(p.seg, (u16)(p.off + o), v); }
static inline void uwp(FarPtr p, u16 o, FarPtr v) { far_wr(p.seg, (u16)(p.off + o), v); }
static inline FarPtr fnull(void) { FarPtr p = { 0, 0 }; return p; }

/* A far data segment of the image (hot-spot tables, records): file segment -> FarPtr */
static inline FarPtr fdat(u16 fseg, u16 off) { return far_make(SEG(fseg), off); }
/* hot-spot item i of a table (22-byte items, shell_ui_toolkit.md §1.1) */
static inline FarPtr hs_item(u16 fseg, u16 off, int i) { return far_make(SEG(fseg), (u16)(off + 22 * i)); }
enum { HS_X = 0, HS_Y = 2, HS_W = 4, HS_H = 6, HS_CB = 8, HS_MASK = 0x0C, HS_FLAGS = 0x0D,
       HS_LABEL = 0x0E, HS_USER = 0x10, HS_LINKS = 0x12 };

/* ---- toolkit globals (shell_ui_toolkit.md §0.3) */
#define RX      0x5FDA   /* current rectangle x, y, w, h (s16) */
#define RY      0x5FD8
#define RW      0x5FD6
#define RH      0x5FD4
#define CUR_ITEM 0x5FE0  /* far pointer to the current item */
#define CUR_IDX 0x5FDE   /* its index */
#define HS_EVT  0x5FDC   /* event bits of the item being dispatched */
#define HS_RES  0x5FD2   /* the value hs_dispatch returns (screens use it as "button pressed") */
#define PTR_X   0x601A
#define PTR_Y   0x6018
#define BUTTON  0x2033
#define TXT_COL 0x2300
#define TXT_FLG 0x2302
#define CLIP_ON 0x2303
#define FILL_ON 0x230C
#define FILL_C  0x230D
#define LINE_C  0x230E
#define PG_SRC  0x2316
#define PG_DST  0x2318
#define PG_WORK 0x2312
#define PG_FRONT 0x2314
#define PG_SPARE 0x2310
#define F_TALK  0x421A   /* font handles */
#define F_FONT4 0x421C
#define F_FONT3 0x421E
#define F_FONT2 0x4220
#define F_FONT1 0x4222
#define PARTS_TBL   0x5FE6
#define SYMBOLS_TBL 0x5FE4
#define STRBUF  0x419A   /* shared scratch text buffer */
#define LBLBUF  0x411A   /* inline-button label buffer */
#define COUNTDOWN 0x2E3F /* PIT countdown (u16) */

static inline FarPtr cur_item(void) { return ds_far(CUR_ITEM); }
static inline s16 cur_idx(void) { return (s16)(s8)DSB(CUR_IDX); }
static inline void set_mask(FarPtr item, u8 m) { uwb(item, HS_MASK, m); }
/* frame of a bitmap frame table (near array of near pointers) */
static inline u16 bmp_frame(u16 tbl_ds, int i) { return DSW((u16)(tbl_ds + 2 * i)); }
static inline s16 frame_w(u16 f_ds) { return (s16)DSW((u16)(f_ds + 6)); }
static inline s16 frame_h(u16 f_ds) { return (s16)DSW((u16)(f_ds + 8)); }

/* "line c: (x0,y0)-(x1,y1)" */
static inline void uline(u8 c, s16 x0, s16 y0, s16 x1, s16 y1) { DSB(LINE_C) = c; gfx_line(x0, y0, x1, y1); }
static inline u16 font3(void) { return DSW(F_FONT3); }

/* A C string literal copied onto the emulated DGROUP stack (for strings the port builds itself;
 * released with the caller's stk mark). Strings of the original are used at their DS offsets. */
static inline u16 ulit(const char *s)
{
    u16 n = 0;
    while (s[n]) n++;
    u16 p = stk_alloc((u16)(n + 1));
    for (u16 i = 0; i <= n; i++) DSB(p + i) = (u8)s[i];
    return p;
}

/* assert (227c:0143 fatal_line) with the far data segment naming the source file */
static inline void ufatal(u16 fseg, u16 line) { assert_fail(far_make(SEG(fseg), 0), (s16)line, 0); }

/* code pointers of this part */
#define UFN(seg, off) ((u32)(seg) << 16 | (off))
#define UREG(fn) codeptr_register(PROG_PS, PS_FN_##fn, (CodeFn)(fn))
#define UFP(fn) codeptr_far(PS_FN_##fn)

/* the PS C library / helpers used under their spec names */
/* str_get (24e3:021f) returns a string-pool copy in DGROUP: DX:AX, callers use AX as a near pointer */
static inline u16 str_get(s16 id) { return scd_string(id).off; }
#define str_get_far(id)        scd_string(id)
#define strtab_get(t, i)       strings_get(t, i)
#define strtab_free()          strings_free()
#define strtab_load(name_ds)   ps_strings_load_24e3(name_ds)
#define res_name(b, n, e)      make_name(b, n, e)

/* Callback signatures used through code pointers (the toolkit casts them back):
 *   hot-spot callbacks, frames, TTM hooks     void f(void)
 *   list-box custom rows                      void f(s16 i, s16 y)
 *   flow_text substitutions                   FarPtr f(s8 c)
 *   flow_text_buttons substitutions           FarPtr f(s8 c, FarPtr item)
 *   chooser / roster "on ok" callbacks        void f(s16 sel)
 */
typedef void   (*URowFn)(s16 i, s16 y);
typedef FarPtr (*USubstFn)(s8 c);
typedef FarPtr (*USubst2Fn)(s8 c, FarPtr item);
typedef void   (*UOkFn)(s16 sel);

/* ---- functions shared between the files of part B that modules.csv lists as file-local but that
 * another part-B file calls (none so far) */

/* LXMUL@: 32-bit multiply keeping the low 32 bits (no C signed-overflow UB) */
static inline s32 lxmul(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }

/* ---- toolkit calls whose shell_ui.h declarations are "stack words" (part A may retype them): one
 * place to adapt. flow_text_buttons(tbl, first, subst, strings, x, y, right) -> lines */
static inline void ui_flow_text_buttons(FarPtr tbl, u16 first, FarPtr subst, u16 strings_ds,
                                        s16 x, s16 y, s16 right)
{
    flow_text_buttons(tbl, (s16)first, subst, strings_ds, x, y, right);
}
