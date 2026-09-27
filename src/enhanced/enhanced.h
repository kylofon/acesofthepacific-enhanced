#pragma once
/* Enhanced view (ENHANCED.md, Stage 1). Not bound by the faithful-engine rules of ENGINE.md, but it only
 * observes the engine: nothing here writes mem[], the VGA memory or any game state.
 *
 * The faithful engine keeps running and drawing its 320x200 picture exactly as before. This module
 *   - records, for every 3D frame AP draws (render_scene), the camera and every polygon the 3Space part
 *     routines hand to the fill, in eye space, plus the horizon's parameters (a "snapshot");
 *   - tags every pixel written into the VGA memory with the kind of code that wrote it (2D art, the
 *     polygon world / horizon, other 3D-scene primitives such as sprites and dots);
 *   - at every displayed frame draws the polygon world and the horizon again on the host at a higher
 *     resolution with anti-aliasing, with the camera and the moving objects interpolated between the
 *     last two snapshots, and lays it under the scaled VGA picture wherever the picture shows polygon /
 *     horizon pixels (cockpit, HUD, text and every 2D screen stay the original art, scaled).
 * The hooks below are called from the engine at the places marked `ENH:`; all of them do nothing with
 * --classic (enh_init(false, ...)). */
#include "types.h"
#include "platform/vga.h"

#define ENH_DEFAULT_RES_SCALE 4        /* output = 320x200 times this */
#define ENH_MAX_RES_SCALE     8
#define ENH_DEFAULT_AA        2        /* samples per output pixel along each axis */
#define ENH_MAX_AA            4
#define ENH_WIDE_W            426      /* widescreen picture width in original pixels (16:9 at 4:3 pixels) */

/* Pixel tags (who wrote a VGA pixel last). */
enum { ENH_TAG_2D = 0, ENH_TAG_WORLD = 1, ENH_TAG_SCENE = 2 };

#define ENH_DEFAULT_VIEW_DIST 3        /* Stage 2: engine view ranges x this in the enhanced pass */
#define ENH_MAX_VIEW_DIST     8

/* main.c. enabled = false is --classic: every hook is a no-op and the picture is the faithful port's. */
void enh_init(bool enabled, int res_scale, int aa, bool widescreen);
/* Stage 2 options (after enh_init): view_dist = range multiplier (1 = the original ranges), lod = finest
 * LOD in the enhanced pass, haze = distance haze / gradient sky / blended horizon in the host render. */
void enh_init_stage2(int view_dist, bool lod, bool haze);
bool enh_enabled(void);
/* Output frame size (320x200 with --classic). x0 = where the 320-pixel screen starts (widescreen). */
void enh_frame_size(int *w, int *h, int *screen_x0, int *screen_w);

/* ---- pixel tags (video/vid_priv.h vput, sim_flow/init.c span_fill_solid, platform/vga.c) ------------ */
extern u8 *enh_vtag;                   /* parallel to vga_vram(); NULL with --classic */
extern u8  enh_vclass;                 /* the tag stamped by the current writer */
static inline void enh_tag(const u8 *vram_byte)
{
    if (enh_vtag) enh_vtag[vram_byte - vga_vram()] = enh_vclass;
}
void enh_vram_cleared(void);           /* mode set / unchained: the whole memory was cleared */

/* ---- 3D scene capture (render3d) ---------------------------------------------------------------------- */
void enh_scene_begin(void);            /* scene.c render_scene: camera, window and light are set */
void enh_scene_end(void);              /* scene.c render_scene: end of the 3D frame */
void enh_sky(u16 sky, u16 sea);        /* horizon.c draw_sky: base colours (bands from the detail level) */
void enh_sky_end(void);
void enh_sky_callback(bool inside);    /* horizon.c call_cb: sun, moon, stars are not re-drawn */
void enh_object(u32 key);              /* scene.c draw_object: identity of the object drawn (0 = none) */
void enh_ts_object(u16 eye_shift);     /* ts_draw.c: an instance is set up; eye coordinates are >> eye_shift */
/* r3d_poly.c / r3d_clip.c part routines: enh_poly = a polygon's vertex list (CS:0A51) with its eye
 * coordinates and colours is complete (before the near-plane clip, which rewrites the list); wide =
 * 32-bit eye coordinates. enh_poly_fill = the polygon goes to the fill: it is recorded and the fill's
 * pixels are tagged as world; enh_poly_done = after the fill. */
void enh_poly(bool wide);
void enh_poly_fill(void);
void enh_poly_done(void);

/* ---- Stage 2: the enhanced scene pass (render3d/scene.c, effects.c, ts_draw.c) ----------------------- *
 * After AP's own render_scene, enh_pass_begin() saves mem[], the VGA memory, the pixel tags, the DGROUP
 * stack pointer and the EMS state and returns true when render_scene is to be run once more with
 * enh_pass set: the snapshot is then re-captured with longer ranges, the finest LOD and real aircraft
 * instead of the far dots. enh_pass_end() puts everything back, so that pass leaves no trace in the game's
 * state or its picture. The hooks below only change anything while enh_pass is set. */
extern bool enh_pass;
extern int  enh_view_dist;             /* 1 with --classic */
bool enh_pass_begin(void);
void enh_pass_end(void);
/* a distance compared with an engine range: divided by the view multiplier in the enhanced pass */
static inline u32 enh_dist(u32 d) { return enh_pass ? d / (u32)enh_view_dist : d; }
/* a range: multiplied in the enhanced pass */
static inline u32 enh_range(u32 r) { return enh_pass ? r * (u32)enh_view_dist : r; }
bool enh_lod_finest(void);             /* ts_draw: take the last (finest) LOD */
void enh_far_dot(bool inside);         /* scene.c draw_object around far_dot_draw: the dot is world */

/* ---- composition (platform/vga.c) --------------------------------------------------------------------- */
/* pic = the 320x200 VGA picture (XRGB), tag = the pixel tags of the same pixels, pal = the DAC as XRGB,
 * shown = the page segment displayed, changed = pic differs from the last call. Writes the output frame
 * and returns true if it changed (the enhanced view moves between the game's frames). */
bool enh_compose(const u32 *pic, const u8 *tag, const u32 pal[256], u16 shown, bool changed, u32 *out);
