#pragma once
/* render3d internals (not part of the module interface).
 *
 * The 3Space core keeps its state in its own code segment (render3d_math.md §2.3): AP 277a, PS 3d50
 * (video_ts.md §2.2). Both segments are resident (not VROOMM overlays), so the bytes live in mem[] at
 * their load addresses and every access here goes to mem[] (other code, e.g. the sky code, reads the
 * block through segment 277a). Offsets in this module are written as the AP offsets (render3d_math.md
 * §2.3); r3d_cs_off() maps them to the PS copy when PS is running. */
#include "render3d/render3d.h"
#include "prog.h"
#include "symbols_ap.h"
#include "symbols_ps.h"
#include <string.h>

/* ---- Little-endian accessors on host pointers into mem[] ----------------------------------------- */
static inline u16  g16(const u8 *p, int o) { return (u16)(p[o] | p[o + 1] << 8); }
static inline s16  gs16(const u8 *p, int o) { return (s16)g16(p, o); }
static inline u32  g32(const u8 *p, int o) { return (u32)g16(p, o) | (u32)g16(p, o + 2) << 16; }
static inline s32  gs32(const u8 *p, int o) { return (s32)g32(p, o); }
static inline void p16(u8 *p, int o, u16 v) { p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); }
static inline void p32(u8 *p, int o, u32 v) { p16(p, o, (u16)v); p16(p, o + 2, (u16)(v >> 16)); }

/* ---- The 3Space code-segment block --------------------------------------------------------------- */
static inline bool r3d_is_ps(void) { return prog_current_id() == PROG_PS; }
static inline u16 r3d_cs_seg(void) { return r3d_is_ps() ? SEG(0x3D50) : SEG(0x277A); }
/* AP 277a offset -> offset in the current program's copy (video_ts.md §2.2). */
static inline u16 r3d_cs_off(u16 o)
{
    if (!r3d_is_ps()) return o;
    if (o < 0x0012) return (u16)(o + 0x2330);              /* light_world, near_z, near_shift */
    if (o >= 0x03B7 && o <= 0x03BA) return (u16)(o + 0x53F8);   /* part_draw16 counters */
    if (o >= 0x09A2 && o < 0x2200) return (u16)(o + 0x3446);    /* object state, vertex arrays */
    if (o >= 0x361A && o <= 0x361C) return (u16)(o - 0x0CB0);   /* part_draw32 counter / flag */
    if (o >= 0x3A28 && o < 0x3A48) return (u16)(o - 0x3956);    /* 4^i tables of isqrt32 */
    if (o >= 0x5664 && o < 0x5E66) return (u16)(o - 0x3EEA);    /* cos_tab */
    if (o >= 0x6028 && o < 0x6228) return (u16)(o - 0x406E);    /* atan_corr (and beyond: code) */
    return o;                                                 /* PORT: AP-only data, unused in PS */
}
static inline u8 *csp(u16 o) { return mp(r3d_cs_seg(), r3d_cs_off(o)); }
#define CSB(o)  (*(u8  *)csp(o))
#define CSC(o)  (*(s8  *)csp(o))
#define CSW(o)  (*(u16 *)csp(o))
#define CSS(o)  (*(s16 *)csp(o))
#define CSL(o)  (*(u32 *)csp(o))
#define CSSL(o) (*(s32 *)csp(o))

/* AP 277a offsets of the state block (render3d_math.md §2.3; AP_CS_* in symbols_ap.h). */
enum {
    CS_LIGHT_WORLD = 0x0008, CS_NEAR_Z = 0x000E, CS_NEAR_SHIFT = 0x0010,
    CS_PD16_COUNT = 0x03B7, CS_PD16_BEHIND = 0x03B9, CS_PDS_BEHIND = 0x03BA,
    CS_VTX_KEY = 0x09A2, CS_VTX_OFS = 0x09A6, CS_VTX_COUNT = 0x09A8, CS_OBJ_SHIFT = 0x09AA,
    CS_VTX_SHIFT = 0x09AC, CS_OBJ_DOWN = 0x09AE, CS_DRAW_MODE = 0x09B0, CS_09B1 = 0x09B1,
    CS_FORCE_CLIP = 0x09B2, CS_NEED_CLIP = 0x09B3, CS_BACKLIT = 0x09B5,
    CS_M_CAM = 0x09B6, CS_M_OBJ_EYE = 0x09C9, CS_M_OBJ = 0x09DC, CS_D_WORLD = 0x09EF,
    CS_E_POS = 0x09FB, CS_EYE_IN_OBJ = 0x0A07, CS_E_POS16 = 0x0A0D, CS_CAM_POS = 0x0A13,
    CS_CAM_ANG = 0x0A1F, CS_SHAPE_TBL = 0x0A25, CS_SHAPE_PTR = 0x0A27, CS_STATE_PTR = 0x0A2B,
    CS_RADIUS = 0x0A2D, CS_E_DIST = 0x0A31, CS_E_NEAR = 0x0A35, CS_APP_SIZE = 0x0A39,
    CS_LIGHT_OBJ = 0x0A3B, CS_E_DEPTH = 0x0A41, CS_E_DEPTH16 = 0x0A45, CS_FOCAL = 0x0A47,
    CS_PROJ_SHIFT = 0x0A49, CS_CX = 0x0A4B, CS_CY = 0x0A4D, CS_POLY_N = 0x0A4F, CS_POLY_IDX = 0x0A51,
    CS_VTX_STATE = 0x0A79, CS_PX = 0x0C77, CS_PY = 0x0C9F, CS_TX = 0x0CC7, CS_TY = 0x0CEF,
    CS_EX = 0x0D17, CS_EY = 0x0F15, CS_EZ = 0x1113, CS_VX = 0x1311, CS_VY = 0x150F,
    CS_PXH = 0x170D, CS_PYH = 0x1735, CS_TXH = 0x175D, CS_TYH = 0x1785,
    CS_EXH = 0x17AD, CS_EYH = 0x19AB, CS_EZH = 0x1BA9, CS_VXH = 0x1DA7, CS_VYH = 0x1FA5,
    CS_M_LOOK = 0x21A3, CS_SCR_VEC = 0x21B6, CS_SCR_NORMAL = 0x21BC, CS_SCR_RESULT = 0x21C2,
    CS_SCR_L32 = 0x21E0, CS_SHADES = 0x21EC, CS_REMAP = 0x21EE, CS_SHADE_OFS = 0x21F8,
    CS_PD32_COUNT = 0x361A, CS_PD32_BEHIND = 0x361C,
    CS_POW4_LO = 0x3A28, CS_POW4_HI = 0x3A38, CS_LNORM_EXP = 0x3E1A,
    CS_COS_TAB = 0x5664, CS_ATAN_CORR = 0x6028, CS_ASIN_TAB = 0x63A6,
};

/* Program DGROUP offsets used by the core that are not in the layout table (video_ts.md §7). */
static inline u16 r3d_ds(u16 ap_ofs, u16 ps_ofs) { return r3d_is_ps() ? ps_ofs : ap_ofs; }
#define DS_LMID_RESULT  r3d_ds(0x25F5, 0x3889)
#define DS_PROJ32_SCR   r3d_ds(0x260E, 0x38A2)
#define DS_TABLES       r3d_ds(0x647C, 0x76D2)    /* near ptr to the 12-byte shape table records */
#define DS_TOTAL        r3d_ds(0x647E, 0x76D4)    /* AP: total shapes (fast cache size), PS: fast count */
#define DS_NTABLES      r3d_ds(0x6480, 0x76D6)
#define DS_SETS         r3d_ds(0x6498, 0x76CE)    /* near ptr to the 4-byte sprite set records */
#define DS_NSETS        r3d_ds(0x649A, 0x76D0)
#define DS_FAST_PTR     r3d_ds(0x2294, 0x39E6)    /* far: fast shape pointer cache */
#define DS_FAST_TBL     0x2298                    /* AP only: far, table index per id */
#define DS_TS_INIT      r3d_ds(0x24B4, 0x3BBC)
#define DS_BM_ELEV      r3d_ds(0x6482, 0x76D8)
#define DS_BM_ROLL      r3d_ds(0x6484, 0x76DA)
#define DS_BM_AZIM      r3d_ds(0x6486, 0x76DC)
#define DS_BMD_EXT1     r3d_ds(0x6488, 0x76DE)
#define DS_BMD_EXT0     r3d_ds(0x648A, 0x76E0)
#define DS_BM_BASE      r3d_ds(0x648C, 0x76E2)
#define DS_BM_HOT       r3d_ds(0x648E, 0x76E4)    /* far */
#define DS_LIGHT_COPY   r3d_ds(0x6492, 0x76E8)    /* i16[3] copy of CS:0008 */
#define DS_BM_CAP       r3d_ds(0x23BC, 0x3B0A)
#define DS_BM_MULT      r3d_ds(0x23BE, 0x3B0E)
#define DS_SHADOW_COL   r3d_ds(0x2402, 0x3B52)
#define DS_SHADOW_MODE  r3d_ds(0x2403, 0x3B53)
#define DS_SHADOW_LOD   r3d_ds(0x2405, 0x3B55)
/* driver block (drv[], video.md): +03 clip flag, +04..+0A clip window, +0C fill on, +0D fill colour,
 * +0E edge colour, +1F VGA colours (26DF); fill arrays +AC/+D4, count +19C (G_poly_x/y/n) */
#define DRV_CLIP_ON  LB(G_drv_clip_on)
#define DRV_X1       LS(G_drv_clip_x1)
#define DRV_X2       LS(G_drv_clip_x2)
#define DRV_Y1       LS(G_drv_clip_y1)
#define DRV_Y2       LS(G_drv_clip_y2)
#define DRV_FILL_ON  LB(G_drv_fill_on)
#define DRV_FILL_COL LB(G_drv_fill_col)
#define DRV_EDGE_COL LB(G_drv_draw_col)
#define DRV_VGA      LB(G_drv_dac_flag)

/* The object state block 09A2..21FF as one host pointer (both programs keep it contiguous). */
static inline u8 *cs_blk(void) { return mp(r3d_cs_seg(), r3d_is_ps() ? 0x3446 : 0); }
#define KW(o)  (*(u16 *)(K + (o)))
#define KS(o)  (*(s16 *)(K + (o)))
#define KL(o)  (*(u32 *)(K + (o)))
#define KSL(o) (*(s32 *)(K + (o)))
#define KB(o)  (*(u8  *)(K + (o)))

/* assert helpers: AP assert_fail0 (2494:01ca) / PS assert_fail (227c:0143); the file string and line
 * differ per program (the libraries were compiled separately). */
void r3d_assert(u16 ap_file_ds, s16 ap_line, u16 ps_file_ds, s16 ps_line);

/* ---- r3d_math.c internal core (pointer forms of the register routines) --------------------------- */
s16  r3d_cos(u16 a);                                  /* inline cos16 of the core (277a:5e71) */
static inline s16 r3d_sin(u16 a) { return r3d_cos((u16)(a + 0xC000)); }
static inline s16 fx14(s32 sum) { return (s16)((u32)sum >> 14); }     /* high word of sum << 2 */
void r3d_mat_mul(u8 *a, u8 *b, u8 *c);
void r3d_mat_transpose(u8 *m);
void r3d_mat_from_angles(const u8 *ang, u8 *m);
void r3d_mat_to_angles(const u8 *m, u8 *ang);
u16  r3d_isqrt32(u32 n);
u16  r3d_atan2_16(s16 x, s16 y);
u16  r3d_atan2_32(s32 x, s32 y);
s16  r3d_asin16(s16 v);
s16  r3d_acos16(s16 v);
/* 277a:3f6e: x and z replaced by x·f/y, z·f/y (render3d_math.md §4.8, quirks kept). */
void r3d_proj32(s32 *x, s32 *z, s32 y, s32 f);
u32  r3d_ldist3v(u32 a, u32 b, u32 c);
u32  r3d_ldist2v(u32 a, u32 b);
/* file-local routines of the original with no caller in another file (declared for -Wall) */
u32  ldist3v(u32 a, u32 b, u32 c);                    /* AP 277a:3b8b */
u16  lsub_far(u16 a, FarPtr b);                       /* AP 277a:50f4 */
void clip2d32a(void);                                 /* AP 277a:2caa */
void clip2d32b(void);                                 /* AP 277a:3242 */
void near_clip32a(void);                              /* AP 277a:2f4e */
void near_clip32b(void);                              /* AP 277a:3530 */
void poly_add_centre32(void);                         /* AP 277a:35d0 */
void poly_fill_bitmap4(u16 es, u16 di);               /* AP 277a:02e4 */
void lswap_order(u16 a, u16 b);                       /* AP 277a:384e */
s16  lbox_overlap(u16 amin, u16 amax, u16 bmin, u16 bmax);   /* AP 277a:38d0 */
u8   shape_flags(s16 id);                             /* PS 47c8:0196 / AP 271b:02b5 */
void shape_box(s16 id, u16 mn, u16 mx);               /* PS 47c8:0490 / AP 271b:0584 */
FarPtr ts_load_chunk_h(u16 name_ds, u16 tag_ds, u16 flag);   /* AP 7039:02a4 */
u16  shape_count(void);                               /* AP 7032:0000 */
u16  sprite_set_load(u16 set, u16 name);              /* AP 243d:0009 */
void sprite_set_free2(u16 set);                       /* AP 243d:00cd */
/* ts_draw.c / ts_bm.c file-local routines of the original */
u16  vtx_eye16(u8 idx, u16 out);                      /* PS 46d0:005b / AP 2edf:005e */
void sprite_draw(FarPtr var);                         /* PS 46d0:0117 / AP 2edf:0119 */
void disc_draw(FarPtr var);                           /* PS 4915:0007 / AP 70e8:0000 */
void nop_2ec2(void);                                  /* AP 2ec2:0002 */
void set_shadow_colour(u8 c);                         /* AP 2ecb:013c */
u16  part_list(u16 self, s16 n, u16 parts_ofs, u16 *out);   /* PS 4708:0715 / AP 2f0e:06eb */
s16  part_sort(u16 self, s16 n, u16 *list);           /* PS 4708:034c / AP 2f0e:033d */
void part_walk(u16 self, s16 n, u16 *list);           /* PS 4708:074b / AP 2f0e:071f */
void bmd_free(s16 id);                                /* AP 241c:01f6 */
void bm_reset_size_cap(void);                         /* AP 2e33:0011 */
void bm_set_mult(s16 n);                              /* PS 4818:092f / AP 2e33:08e7 */
u8   bm_visible(void);                                /* AP 2e33:0208 */
void bm_view_angles(void);                            /* PS 4818:0468 / AP 2e33:0333 */
void bm_draw_item(FarPtr it, s16 a, s16 b, s16 x, s16 y, s16 k, s16 c);   /* PS 4818:0152 / AP 2e33:001c */
void bm_draw_frame(FarPtr p, s16 frame);              /* PS 4818:063d / AP 2e33:0508 */
/* vecmath.c file-local routines of the original */
u8   in_range(s16 v, s16 lo, s16 hi);                 /* AP 35ff:0029 */
u16  vscale(u16 v, s16 s);                            /* AP 3604:0009 */
u16  l2v_sar(u16 out, u16 in, u16 n);                 /* AP 3666:0009 */
u16  vec_outside_mask(u16 v, u16 lo, u16 hi);         /* AP 3684:0042 */
u16  lneg(u16 out, u16 in);                           /* AP 4edb:0000 */
void nop_4edb(void);                                  /* AP 4edb:0030 */
s16  ratio10(s16 a, s16 b);                           /* AP 5137:0034 */
FarPtr v2l_c(FarPtr out, u16 in);                     /* AP 5137:0084 */
/* pool.c file-local routines */
void pool_init(u16 hdr, u16 off, u16 seg, u16 count, u16 size);   /* AP 3546:0118 */
u32  pool_alloc2(u16 hdr);                            /* AP 3546:02bb */
u16  pool_misc(u16 hdr);                              /* AP 3546:04a4 */

/* ---- AP DGROUP globals of the scene / world / sky code (render3d_world.md §1.5, render3d_sky.md;
 * raw offsets, most are not in symbols.csv) ---------------------------------------------------------- */
enum {
    W_STATIC_OBJS = 0x06F3,         /* far: pointers to all static objects */
    W_DRAW_REC = 0x3666, W_LIST_CAP = 0x367C, W_LIST_RESERVE = 0x367E, W_GATHER_FORCE = 0x3680,
    W_SCENE_VIEW = 0x3682, W_GATHER_EYE = 0x3688, W_LIST_FRONT = 0x3694, W_LIST_BACK = 0x3698,
    W_GATHER_OBJ = 0x369C, W_BIG_RANGE_MIN = 0x36A0, W_FLATTEN_PTR = 0x36A4, W_TMP_ARENA = 0x36A8,
    W_MACRO_CAP = 0x36AC, W_MACRO_TAB = 0x36AE, W_MACRO_COUNT = 0x36B2, W_ZERO_VEC = 0x36B4,
    W_RB_STR = 0x36C0, W_REGATHER_DIST = 0x36C4, W_GID_BASE = 0x36C6, W_GID_N = 0x36CA,
    W_DRAW_CB = 0x36CC, W_AFTER_GROUND_CB = 0x36D0, W_DRAW_LIST = 0x36D4, W_LIST_N = 0x36D8,
    W_LIST_GROUND_N = 0x36DA, W_LIST_STATIC_N = 0x36DC, W_FLT_N = 0x36DE, W_FLT_TAB = 0x36E0,
    W_FLT_RANGE = 0x36E2, W_ATTR_CLASS = 0x3700, W_ATTR_LAYER = 0x3704, W_ATTR_SHIFT = 0x3708,
    W_ATTR_RADIUS = 0x370C, W_ATTR_VALID = 0x3710,
    W_WORLD_N = 0x43D6, W_ARENA_TOP = 0x710A, W_ARENA_BOTTOM = 0x710C, W_ARENA_SIZE1 = 0x710E,
    W_WD3_HANDLER = 0x7110, W_WHD_TAB = 0x7114,
};
/* world_load.c / wdata.c file-local routines */
u16  fx_in_range(FarPtr e);                           /* AP 05a9:025f */
void fx_release_list(u16 list, s16 n);                /* AP 05a9:0233 */
void fx_recull(void);                                 /* AP 05a9:01d1 */
u16  lerp_step_init(u16 y, s16 dx);                   /* AP 33a1:00b3 */
void lerp_setup_copy(u16 s, u16 xa, u16 ya, u16 xb, u16 yb);   /* AP 333b:024c */
u16  lerp_eval_copy(u16 s, u16 x);                    /* AP 333b:0290 */
void flt_record(u16 r);                               /* AP 333b:0098 */
void flt_load(u16 name);                              /* AP 333b:000a */
u8   flt_valid(s16 c);                                /* AP 333b:0143 */
u16  point_in_region(FarPtr region, s16 x, s16 y);    /* AP 35d1:000e */
void obj_angles(FarPtr o, u16 out);                   /* AP 35a4:000f */
u16  gid_region(FarPtr entry, s16 i);                 /* AP 3368:0128 */
s16  gid_slope_height(FarPtr region, s16 x, s16 y);   /* AP 3368:02f4 */
u8   ground_query_obj(u16 out, u16 p, FarPtr entry, FarPtr o);   /* AP 3368:0169 */
void ob_init(u16 unused, u16 n);                      /* AP 3591:0001 */
u16  wd3_open(u16 name);                              /* AP 35ad:000c */
void wd3_close(void);                                 /* AP 35ad:00ce */
void wd3_read_objects(u16 name);                      /* AP 35ad:00f8 */
u16  shape_attr_refresh(void);                        /* AP 35d6:000c */
u16  shape_shift_cached(u16 ax);                      /* AP 35d6:01b8 */
void shape_set_class_cached(u16 ax, u8 dl);           /* AP 35d6:0236 */
u16  fwrite_word(u16 w, u16 f);                       /* AP 51ca:00ce */
u16  fwrite_long(u32 v, u16 f);                       /* AP 51ca:010c */
u16  fwrite_str(u16 s, u16 f);                        /* AP 51ca:0180 */
u16  bitlen_long(s32 v);                              /* AP 62d2:0000 */
u32  group_record(FarPtr dst, u16 r);                 /* AP 62d2:001d */
u32  instance_build(FarPtr dst, u16 r);               /* AP 62d2:0133 */
u16  object_record_handler(u16 r);                    /* AP 62d2:0199 */
u16  area_read_groups(u16 name);                      /* AP 62d2:0a2e */
void wd_list0(s16 i, u16 v);                          /* AP 628f:0000 */
void wd_list1(u16 v);                                 /* AP 628f:0024 */
void wd_list2(u16 w);                                 /* AP 628f:0039 */
void wd_list_insert(u16 date);                        /* AP 628f:0048 */
void wd_list3(u16 l);                                 /* AP 628f:00aa */
void wd_list_insert2(u16 id);                         /* AP 628f:00bb */
u32  macro_entry(u16 i);                              /* AP 34f1:02f5 */

/* ---- far pointers returned in DX:AX ---------------------------------------------------------------- */
static inline FarPtr fp_u32(u32 v) { return far_make((u16)(v >> 16), (u16)v); }
static inline u32 u32_fp(FarPtr p) { return (u32)p.seg << 16 | p.off; }
static inline s32 r3d_labs(s32 v) { return v < 0 ? (s32)(0u - (u32)v) : v; }      /* RTL labs 0000:1fff */

/* ---- scene.c (render3d_world.md §3) ---------------------------------------------------------------- */
/* sort tree node (§1.4; PORT: host array, the original keeps it in the caller's stack frame) */
typedef struct { FarPtr ptr; u8 parent, nearer, farther, flags; } R3dNode;
typedef struct { u16 tn, tc; s32 diff; FarPtr nw, cur; } R3dCmp;
void draw_entry(FarPtr o);                            /* AP 33b4:0004 */
void draw_entry_default(FarPtr o);                    /* AP 33b4:002a */
u16  draw_record(FarPtr o);                           /* AP 33b4:010e */
u8   scene_eye_moved(u16 v);                          /* AP 33d1:00a8 */
s16  list_add(FarPtr o, u32 key);                     /* AP 33d1:00f2 */
u8   static_test_add(void);                           /* AP 33d1:02ea */
u8   static_test_add_body(void);                      /* AP 33d1:02eb */
void tree_flatten(R3dNode *T);                        /* AP 33d1:0e69 */
void tree_flatten_body(R3dNode *T);                   /* AP 33d1:0e6a */
void scene_draw(void);                                /* AP 34bf:0006 */
void tree_build(s16 n, FarPtr list, R3dNode *T);      /* AP 34bf:0084 */
void tree_draw(R3dNode *T);                           /* AP 34bf:02a3 */
void tree_draw_body(R3dNode *T);                      /* AP 34bf:02a4 */
s16  sort_compare(const R3dCmp *c);                   /* AP 465c:0589 */
void draw_object(FarPtr o);                           /* AP 5440:0269 */
/* horizon.c (render3d_sky.md §2) */
void span_left_const(FarPtr L, s16 v);                /* AP 0a88:000a */
void span_right_const(FarPtr L, s16 v);               /* AP 0a88:000f */
void span_left_step(FarPtr L, s16 v);                 /* AP 0a88:0030 */
void span_right_step(FarPtr L, s16 v);                /* AP 0a88:0035 */
void span_left_edge(FarPtr L);                        /* AP 0a88:0059 (xr = xl) */
void span_right_edge(FarPtr L);                       /* AP 0a88:007c (xl = xr) */
void span_left_dda(FarPtr L, u16 d);                  /* AP 0a88:009a */
void span_right_dda(FarPtr L, u16 d);                 /* AP 0a88:009f */
void span_fill(s16 y, u16 seg, u16 left, u16 right, s16 n, FarPtr D);   /* AP 0a88:00cf */
void span_flush(FarPtr L, FarPtr D);                  /* AP 0a88:0145 */
void dda_setup(u16 d, s16 a, s16 b, s16 n);           /* AP 1abf:0008 */
s16  dda_step(u16 d);                                 /* AP 1abf:00c6 */
void dda_fill_edge(FarPtr dst, s16 a, s16 b, u16 n);  /* AP 1abf:00de */
void dda_const_edge(FarPtr dst, s16 v, u16 n);        /* AP 1abf:011d */
s16  horizon_line(u16 p, u16 r);                      /* AP 4d6e:015c */
void horizon_fill_rest(s16 flag, s16 ya, s16 yb, FarPtr A, FarPtr B, FarPtr C, FarPtr E, FarPtr D);   /* AP 4d6e:029e */
void horizon_fill_rows(void);                         /* AP 4d6e:03be */
void horizon_fill_cols(void);                         /* AP 4d6e:0724 */
/* effects.c */
void ground_speed_dots(void);                         /* AP 675d:0000 */
/* sky.c (render3d_sky.md §3) */
void   stars_load_thunk(void);                        /* AP 55e8:0000 */
FarPtr ap_date_split(u16 day, FarPtr out);            /* AP 55e8:0028 */
s16    day_of_year(s16 m, s16 d);                     /* AP 55e8:028f */
s16    declination(s16 v);                            /* AP 55e8:02a3 */
s16    moon_phase(u16 date);                          /* AP 4b91:0000 */
s16    moon_phase_days(s32 x);                        /* AP 4b91:008d */
void   stars_reset(u16 t);                            /* AP 59eb:0097 */
u16    sky_dir(u16 out, u16 ang);                     /* AP 1a9a:01da */
void   draw_stars(void);                              /* AP 1a9a:000a */
u16    sun_in_window(u16 m);                          /* AP 6964:05a3 */
u16    sun_angle(void);                               /* AP 6964:059b */
void   choose_light(void);                            /* AP 6964:05d2 */
void   celestial_project(u8 sun);                     /* AP 6964:0298 */
void   palfx_delta_table_body(s16 k);                 /* AP 1792:038b */
