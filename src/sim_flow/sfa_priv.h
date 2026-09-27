#pragma once
/* sim_flow part A private header: the module headers every part-A file needs, plus declarations of
 * part-A functions that only part-A files call (sim_flow.h lists the module's [file] and x
 * functions; the ones below have no iface in modules.csv but are shared between part-A files). */
#include <string.h>

#include "codeptr.h"
#include "host.h"
#include "mem.h"
#include "prog.h"
#include "platform/platform.h"
#include "platform/rtl.h"
#include "render3d/render3d.h"
#include "shell_ui/shell_ui.h"
#include "sim_flow/sim_flow.h"
#include "sim_world/sim_world.h"
#include "sound/sound.h"
#include "symbols_ap.h"
#include "video/video.h"

/* flight_loop.c */
u32  sim_ticks_read(void);                    /* 0000:02e7 */
void dt_set(s16 dt);                          /* 362b:000d */
void timebase_set_div(s16 div);               /* 362b:0048 */
s16  timebase_ticks(void);                    /* 362b:0053 */
s16  timer16_inc(u16 t_ds);                   /* 362b:01bb */
s16  timer16_inc_far(FarPtr t);               /* 362b:01e7 */
s16  timer32_inc2(u16 t_ds);                  /* 362b:023b */
void flight_pause(void);                      /* 5cde:000b */

/* init.c */
void gauges_init_stub(void);                  /* 4963:0000 */

/* frame.c */
void camera_follow(u16 obj);                  /* 5440:0000 */
void skip_cancel(void);                       /* 5440:12b9 */

/* input.c */
s16  hat_views_get(void);                     /* 5f2c:015e */
s16  abs_throttle_get(void);                  /* 5f2c:0171 */
void mouse_warp(s16 x, s16 y, s16 force);     /* 60a9:0898 */
void mouse_pos(u16 x_ds, u16 y_ds);           /* 60a9:085d */
s16  ap_mouse_button(s16 i);                  /* 60a9:087c */
s16  axis_shape(s16 v, s16 k);                /* 5f2c:0ba3 */
void pointer_get(u16 in_ds, u16 x_ds, u16 y_ds, u16 b_ds);   /* 5f2c:00dc */
s16  pointer_clamp(u16 x_ds, u16 y_ds);       /* 5f2c:0179 */
void cursor_box_init(void);                   /* 5f2c:0000 */

/* views.c */
void view_set_head(u16 rr_ds);                /* 1c30:0a73 */
u16  next_view_object(void);                  /* 637f:00af */
u8   wcam_is_active(void);                    /* 1c30:1021 */
u16  wcam_object(void);                       /* 05d6:0381 */
void wcam_release(void);                      /* 05d6:03fc */
s16  approach(u16 v_ds, s16 target, s16 step);   /* 0590:0002 */

/* cockpit.c */
void palette_aces(void);                      /* 644a:0912 */

/* cockpit_mask.c */
FarPtr edge_alloc(s16 n);                     /* 0962:0095 */
void edge_mark(void);                         /* 0962:00cb */
void edge_release(void);                      /* 0962:00de */
FarPtr span_intersect(u16 a_seg, u16 a_off, u16 b_seg, u16 b_off, FarPtr out);   /* 0a7b:000c */
FarPtr span_intersect_w(FarPtr a, FarPtr b, FarPtr out);   /* 0962:0502 */
s16  span_in_rows(FarPtr sp);                 /* 0962:0520 */
void span_through_windows(FarPtr sp);         /* 0962:079a */
void pixel_rows(s16 x, s16 y, u16 c);         /* 0962:0819 */
void masked_span_fill(s16 bx_x1, s16 cx_y1, s16 dx_x2, s16 si_y2);   /* 0962:08de */
void masked_vec2(u16 fr, s16 x, s16 y, u16 flags);                   /* 0962:0cc6 */
void masked_vec3(u16 fr, s16 x, s16 y, u16 angle, s16 w, s16 h, s16 px, s16 py);   /* 0962:0e47 */
void masked_vec4(u16 fr, s16 x, s16 y, s16 flags);                   /* 0962:1023 */

/* fltmap.c */
void world_to_map(u16 mx_ds, u16 my_ds, s32 x, s32 y);   /* 6cdc:006a */
void map_to_world(u16 x_ds, u16 y_ds, s16 sx, s16 sy);   /* 6cdc:00db */
s16  map_icons(u16 ic);                                  /* 6cdc:0161 */
void map_icons_draw(u16 ic, s16 n);                      /* 6cdc:0408 */
s16  route_points(u16 pts);                              /* 6cdc:04fd */
s16  input_abort(void);                                  /* 6cdc:077a */
u8   map_route(void);                                    /* 6cdc:0c11 */
void fltmap_background(void);                            /* 6cdc:0ce7 */

/* radio.c */
void order_text(s16 code, s16 index, u16 dst);    /* 1660:0685 */
s16  targets_list(u16 items, s16 n, s16 air);     /* 1660:07bb */
s16  targets_exist(s16 bombers);                  /* 1660:0bae */
s16  add_attack_item(u16 items, s16 n, s16 bombers);   /* 1660:0c12 */
s16  orders_list(u16 items, s16 max, u16 nearl, s16 k); /* 1660:0c47 */
s16  orders_title(void);                          /* 1660:0f5f */
void radio_menu_rebuild(void);                    /* 6e5a:0396 */

/* messages.c */
void radio_show(s16 group, s16 id, u16 c_ds); /* 1815:0546 */
void radio_queue(s16 group, s16 id, u16 c_ds, s16 delay);   /* 1815:05e0 */
s16  msg_box(u16 slot_ds);                    /* 6939:0000 */
s16  msg_unbox(u16 slot_ds);                  /* 6939:020f */
void advice_init(void);                       /* 10e5:0005 */

/* results.c */
s16  rng_chance(u16 st_ds, s16 n);            /* 1889:00db */
u8   targets(u16 f, u16 x);                   /* 1899:10f8 */
u8   crash_imminent(u16 o);                   /* 1899:139f */
u8   objective_done(u16 o);                   /* 1899:11aa */
s16  goal_fraction(u16 f);                    /* 1899:152e */
u8   captured(void);                          /* 5254:0663 */
u8   reached_area(void);                      /* 5254:081e */
void sim_out_write(u16 buf);                  /* 5254:0859 */
s16  quartile(s16 f);                         /* 5254:1301 */
s16  flight_size(u16 g);                      /* 5254:1332 */
s16  ap_flight_active_5254(u16 g);            /* 5254:1348 */
s16  flight_lost(u16 g);                      /* 5254:1473 */
s16  aircraft_lost(void);                     /* 5254:149c */
void mission_result(u16 code_ds, u16 frac_ds);   /* 5254:08d9 */

/* misc.c */
s32  res_exists(u16 name_ds);                 /* 55a4:0167 -> the file length */

/* The player's aircraft (near pointer) and its control block */
#define PLAYER()   DSW(AP_DS_player)
