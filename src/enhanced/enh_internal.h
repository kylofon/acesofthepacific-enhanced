#pragma once
/* Enhanced view internals (see enhanced.h, ENHANCED.md). */
#include "enhanced/enhanced.h"

/* A vertex: eye coordinates of the frame turned back to world axes, relative to the frame's camera
 * position (world units). */
typedef struct { float x, y, z; } EV3;

/* An instance drawn by ts_draw: identity (draw_object's object, shadow bit 31), its occurrence number
 * among the instances with the same key in this frame, world position and orientation (row-vector
 * convention: world offset = local · R). */
typedef struct {
    u32 key;
    u16 ord;
    bool shadow;
    double pos[3];
    double R[9];
} EObj;

/* A polygon (render3d_poly.md §2): n vertices from vert[first], the object it belongs to (-1 = none),
 * the fill flag and colours. n = 1 is a point, 2 a line. */
typedef struct {
    u32 first;
    u16 n;
    s16 obj;
    u8 fill_on, fill, edge;
} EPoly;

/* One 3D frame (render_scene). */
typedef struct {
    bool complete;
    u32 seq;
    u16 page;                          /* the VGA page segment it was drawn into */
    u64 t_ns;
    double cam[3];                     /* camera position, world units */
    double Mc[9];                      /* world -> eye, row vectors: e = d · Mc */
    s16 wx1, wx2, wy1, wy2;            /* the 3D window, inclusive (original pixels) */
    double cx, cy, f, near_d;
    bool has_sky, bands_on;
    u8 sky, sea, bands, step;
    u16 spacing;
    EPoly *poly; u32 npoly, cpoly;
    EV3 *vert; u32 nvert, cvert;
    EObj *obj; u32 nobj, cobj;
} ESnap;

#define ENH_NSNAP 4
extern ESnap enh_snaps[ENH_NSNAP];
extern int enh_scale, enh_aa;
extern bool enh_wide;
extern bool enh_haze;

/* developer aid AOTP_ENH_PROF=1: mean times per slot printed every 2 s */
enum { PROF_SCENE, PROF_PASS, PROF_COPY, PROF_RENDER, PROF_RESOLVE, PROF_COMPOSE, PROF_N };
void enh_prof(int slot, u64 ns);                  /* Stage 2 haze / sky / horizon */

/* enhanced.c: the complete snapshot drawn into page `page` (NULL if none), and the one before it. */
const ESnap *enh_snap_for_page(u16 page);
const ESnap *enh_snap_before(const ESnap *s);
/* Gram-Schmidt on the columns of a 3x3 matrix (row-major; the columns are axes in world coordinates). */
void enh_orthonormalize(double M[9]);
