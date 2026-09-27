#pragma once
/* video module: private helpers shared by the video/ C files (not part of the module interface).
 *
 * The driver/library shared variable block (video.md §2.4): drv[X] = DS:(base + X) with base INTRO
 * 0A80 / PS 2300 / AP 26C0 (= the layout id G_drv_text_fg, i.e. drv[00]). The block is addressed
 * through that base for every field inside 0000..0B48, exactly as the driver does (its DS is
 * DGROUP + base/16); the polygon scratch drv[B20..] lives elsewhere in AP (G_poly_scratch). */
#include "codeptr.h"
#include "host.h"
#include "mem.h"
#include "platform/vga.h"
#include "prog.h"
#include "video/video.h"
#include "enhanced/enhanced.h"   /* ENH: pixel tags */

static inline u16 drv_base(void) { return lds(G_drv_text_fg); }
#define DRVB(x) DSB((u16)(drv_base() + (x)))
#define DRVC(x) DSC((u16)(drv_base() + (x)))
#define DRVW(x) DSW((u16)(drv_base() + (x)))
#define DRVS(x) DSS((u16)(drv_base() + (x)))

/* drv[] field offsets (video.md §2.4) */
enum {
    D_TEXT_FG = 0x00, D_TEXT_BG = 0x01, D_TEXT_FLAGS = 0x02, D_CLIP_ON = 0x03,
    D_CLIP_X1 = 0x04, D_CLIP_X2 = 0x06, D_CLIP_Y1 = 0x08, D_CLIP_Y2 = 0x0A,
    D_FILL_ON = 0x0C, D_FILL_COL = 0x0D, D_DRAW_COL = 0x0E, D_FILL_COL2 = 0x0F,
    D_PAGE_BG = 0x10, D_PAGE_BACK = 0x12, D_PAGE_FRONT = 0x14, D_PAGE_READ = 0x16, D_PAGE_WRITE = 0x18,
    D_ADAPTER = 0x1D, D_INITS = 0x1F, D_20 = 0x20, D_EGA_MODE = 0x21,
    D_FONT_W = 0x34, D_FONT_H = 0x48, D_FONT_FIRST = 0x5C, D_FONT_COUNT = 0x70,
    D_POLY_X = 0xAC, D_POLY_Y = 0xD4, D_POLY_X2 = 0xFC, D_POLY_Y2 = 0x124, D_POLY_OX = 0x14C,
    D_POLY_OY = 0x174, D_POLY_N = 0x19C, D_PAL_BUF = 0x19E,
    D_XLAT_TAB = 0x6BC, D_USE_E28 = 0x6E2, D_YSHIFT = 0x6E8, D_SCR_W = 0x6EA, D_SCR_H = 0x6EC,
    D_ROW_OFS = 0x6F2, D_SPAN_SEG = 0xAB2, D_HW_PRESENT = 0xAB4, D_AB8 = 0xAB8,
    D_SCN_FN = 0xB3A, D_AA_MAP = 0xDAC
};

static inline u16 drv_row(s16 y) { return DRVW((u16)(D_ROW_OFS + 2 * y)); }   /* driver row table (DS-relative) */

/* ---- VGA page memory (platform/vga.h): planar address -> byte. A planar offset inside a page segment
 * wraps at 16 bits like the CPU; addresses beyond the 64 KB VGA window (A000-AFFF) hit nothing. */
static inline u8 *vmem(u16 seg, u16 ofs, int plane)
{
    u32 a = (u32)(u16)(seg - 0xA000) * 16u + ofs;
    if ((u16)(seg - 0xA000) >= 0x1000 || a >= 0x10000u) return NULL;
    return vga_vram() + a * 4u + (u32)plane;
}
/* ENH: every driver write tags the pixel with its writer (enhanced/enhanced.h; no-op with --classic) */
static inline void vput(u16 seg, u16 ofs, int plane, u8 v) { u8 *p = vmem(seg, ofs, plane); if (p) { *p = v; enh_tag(p); } }
static inline u8 vget(u16 seg, u16 ofs, int plane) { u8 *p = vmem(seg, ofs, plane); return p ? *p : 0; }
/* Pixel x of the row whose planar offset is `row` (x may be negative: planar byte (x >> 2) signed). */
static inline void vpx_put(u16 seg, u16 row, s16 x, u8 v) { vput(seg, (u16)(row + (x >> 2)), x & 3, v); }
static inline u8 vpx_get(u16 seg, u16 row, s16 x) { return vget(seg, (u16)(row + (x >> 2)), x & 3); }

/* ---- jump table (DS:1938 / 3174 / 351A): entry n as the C function registered for its far pointer */
static inline u16 drv_tab(void) { return lds(G_drv_jump_table); }
static inline FarPtr drv_entry_ptr(int n) { return ds_far((u16)(drv_tab() + 4 * n)); }

/* ---- per-program address helpers */
static inline int pid(void) { return prog_current_id(); }
#define BY_PROG(i, p, a) (pid() == PROG_INTRO ? (i) : pid() == PROG_PS ? (p) : (a))

/* Library globals the layout lacks for one program (named differently in symbols.csv; see report):
 * INTRO pal_slots DS:0C1E, font_ptr DS:2ED4. */
u16 ds_pal_slot(void);
u16 ds_font_glyphs(void);

CodeFn vid_lookup(FarPtr p);                 /* codeptr_lookup with a host-side cache */
int gfx_init_generation(void);               /* 1 once after gfx_init registered a driver block */

/* present the displayed page now (PORT: effects that draw straight into the front page) */
void vid_present(void);

/* file-local helpers used across video files */
u16  vid_cos(u16 a);                          /* INTRO 1739:080d / PS 3d50:1f87 / AP 277a:5e71 */
s16  vid_sin(u16 a);
void gfx_sprite_scn(u16 f_ds, s16 x, s16 y, s16 flags);                       /* 05da:1e3c */
void gfx_sprite_rotated(u16 f_ds, s16 x, s16 y, u16 angle, s16 w, s16 h, s16 px, s16 py); /* 05da:3d01 */
s16  gfx_dda_init(u16 s_ds_unused, s16 n);    /* see gfx_bitmap.c: DDA on a host struct */
void gfx_sprite_scaled_centered(u16 f_ds, s16 xc, s16 yc, s16 flags, s16 scale);
void gfx_cache_reset(u16 list_ds);
s16  gfx_text_width_far(FarPtr s);
void gfx_text_far(FarPtr s, s16 x, s16 y);
s16  gfx_char_metrics(u8 c, u16 w_ds, u16 h_ds);
void poly_outline_lines(u16 xs_ds, u16 ys_ds, s16 k);
void gfx_poly_clip(void);
void circle_span_plot(s16 a, s16 b);
void circle_outline_plot(s16 a, s16 b);
void vqt_leaf(s16 x, s16 y, s16 w, s16 h);
void vqt_node(s16 x, s16 y, s16 w, s16 h);
void vqt_root(s16 x, s16 y, s16 w, s16 h);
u16  vqt_bits(s16 n);
u16  vqt_tab_read(s16 n);
void vqt_fill_h(s16 x0, s16 y0, s16 x1, s16 y1);
void vqt_fill_v(s16 x0, s16 y0, s16 x1, s16 y1);
void vqt_fill_hv(s16 x0, s16 y0, s16 x1, s16 y1);
void vqt_mem_leaf(u16 x, u16 y, u16 w, u16 h);
void vqt_scr_leaf(u16 x, u16 y, u16 w, u16 h);
void ttm_save_bg(void);
void ttm_copy_back_to_bg(s16 x, s16 y, s16 w, s16 h);
void upd_flush_list(u16 src, u16 dst, u16 key);
u16  upd_find(u16 src, u16 dst, u16 key);
void upd_free_lists(void);
s16  upd_count(void);
void ttm_timer_expire(u16 t_ds);
s16  ttm_rand_skip(s16 n);
