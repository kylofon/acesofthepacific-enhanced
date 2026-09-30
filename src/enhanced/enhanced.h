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
/* weapons.c bullet_draw around its ts_draw: the tracer is drawn at the bullet's old position, back =
 * old - new (one sim step, world units). The host places it on that line at the view time instead of
 * matching it with the frame before (a new bullet has none, and a bullet flies straight). */
void enh_tracer(bool on, s16 back_x, s16 back_y, s16 back_z);
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

/* ---- ground dots (render3d/effects.c ground_speed_dots) --------------------------------------------- *
 * The original draws a 9 x 9 grid of one-pixel dots on the ground (1024 units apart) below 4000 units of
 * altitude, into the sim frame's picture. With the host dots on, the host renderer draws them itself at
 * every displayed frame from the interpolated camera, out to `range` grid cells, up to `alt` units of
 * altitude, `size` percent of an original pixel wide; the original's dot pixels are then tagged world. */
#define ENH_DEFAULT_DOTS_RANGE 12
#define ENH_MAX_DOTS_RANGE     64
#define ENH_DEFAULT_DOTS_ALT   8000
#define ENH_MAX_DOTS_ALT       30000
#define ENH_DEFAULT_DOTS_SIZE  100
#define ENH_MAX_DOTS_SIZE      400
#define ENH_DEFAULT_DOTS_FULL  4096    /* eye distance from which a dot has its full size */
#define ENH_MAX_DOTS_FULL      16384
#define ENH_DEFAULT_DOTS_NEAR  25      /* the size up close, percent of the full size */
#define ENH_DEFAULT_DOTS_SCATTER 40    /* each dot moved from its grid point by up to this % of half a cell */
#define ENH_DEFAULT_DOTS_FAR   2       /* the spacing halves this many times out to the range's edge */
#define ENH_MAX_DOTS_FAR       3
#define ENH_DEFAULT_DOTS_FADE  60      /* the dots fade out from this % of the range to its edge */
#define ENH_DEFAULT_DOTS_SPECKLE 3     /* beyond the range: a speckle over this % of the ground */
#define ENH_MAX_DOTS_SPECKLE   40
#define ENH_DEFAULT_DOTS_FAR_BRIGHT 60 /* the far speckle and crests: % of the dots' colour over the ground */
#define ENH_DEFAULT_DOTS_EDGE  35      /* beyond the full-size distance the dots shrink to this % at the edge */
#define ENH_DEFAULT_DOTS_COVER 8       /* the dots are thinned to cover at most this % of the ground */
#define ENH_MAX_DOTS_COVER     50
/* full, near: dots nearer than `full` units shrink with the distance, down to `near` % of their size;
 * scatter: the dots are moved (the same way at every frame) off the straight rows of the grid;
 * far: the dots get denser with the distance, their spacing halved `far` times at the range's edge;
 * fade: the dots fade out from `fade` % of the range to its edge;
 * speckle: beyond the range, out to the horizon, a faint speckle over `speckle` % of the ground;
 * far_bright: the far dots fade into the ground, but for some "crests" which, as the speckle, keep
 * `far_bright` % of the dots' colour;
 * edge: beyond `full` the dots shrink again with the distance, to `edge` % of the size at the range's edge;
 * cover: where the dots would crowd together, some are skipped so they cover at most `cover` % of it */
void enh_init_dots(bool on, int range, int alt, int size, int full, int near, int scatter, int far, int fade,
                   int speckle, int far_bright, int edge, int cover);
/* ground_speed_dots, before its altitude test: records the dots' place in the frame (after the ground
 * layer) and their colour table (0x88 bytes: the colour under the dot -> the dot's colour). True = the
 * host draws the dots: the original's pixels are to be tagged world. */
bool enh_ground_dots(const u8 *col);
/* debug overlay of the dots (--dots-debug, Ctrl+Alt+G toggles it): the ground grid of the dots, green
 * along the view, yellow across it, out to the range's edge (thinned out where they would be under 3
 * pixels apart), cyan rings where the dots start fading out and at the range's edge, the horizon red */
extern bool enh_dots_debug;

/* ---- key bindings and the flaps keys (enh_keys.c) --------------------------------------------------- *
 * A key is an XT set-1 make code, ENH_KEY_GREY = sent after an E0 prefix, with the modifiers held with
 * it. The flaps up / down keys reach AP as two scancodes no keyboard sends (no ASCII, no direction
 * bits in its tables), which controls_map turns into two new commands. */
#define ENH_KEY_GREY  0x0100
#define ENH_KEY_SHIFT 0x1000
#define ENH_KEY_CTRL  0x2000
#define ENH_KEY_ALT   0x4000
#define ENH_KEY_FLAPS_UP    0x55
#define ENH_KEY_FLAPS_DOWN  0x56
#define ENH_CMD_FLAPS_UP    0x60
#define ENH_CMD_FLAPS_DOWN  0x61
/* main.c --keys "name=code,...": the actions not on their default keys (code 0 = no key). False if the
 * list can't be read. */
bool enh_keys_parse(const char *spec);
/* host.c key_event: key x went down / up. Returns -1 to send x as it is, or the number of keys put in
 * out[] (at most 16; 0x80 = break, ENH_KEY_GREY = E0 prefix) to send instead. */
int enh_key_map(u16 x, bool down, u16 *out);
void enh_keys_focus_lost(void);        /* kbd.c: every key counts as up */

/* ---- composition (platform/vga.c) --------------------------------------------------------------------- */
/* pic = the 320x200 VGA picture (XRGB), tag = the pixel tags of the same pixels, pal = the DAC as XRGB,
 * shown = the page segment displayed, changed = pic differs from the last call. Writes the output frame
 * and returns true if it changed (the enhanced view moves between the game's frames). */
bool enh_compose(const u32 *pic, const u8 *tag, const u32 pal[256], u16 shown, bool changed, u32 *out);
