/* Enhanced view: options, pixel tags and the capture of AP's 3D frames (ENHANCED.md "Stage 1").
 * Reads the 3Space state (render3d_math.md §2.3) at the ENH: hooks; never writes game state. */
#include "enhanced/enh_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ems.h"
#include "mem.h"

#include "host.h"
#include "render3d/r3d_priv.h"

int enh_scale = 1, enh_aa = 1;
bool enh_wide;
static bool enabled;

/* Stage 2 */
bool enh_pass;
int enh_view_dist = 1;
bool enh_haze;
static bool lod_on, pass_on;
/* ground dots */
static bool dots_on;
int enh_dots_range = ENH_DEFAULT_DOTS_RANGE, enh_dots_alt = ENH_DEFAULT_DOTS_ALT, enh_dots_size = ENH_DEFAULT_DOTS_SIZE;
int enh_dots_full = ENH_DEFAULT_DOTS_FULL, enh_dots_near = ENH_DEFAULT_DOTS_NEAR, enh_dots_scatter = ENH_DEFAULT_DOTS_SCATTER;
int enh_dots_far = ENH_DEFAULT_DOTS_FAR, enh_dots_fade = ENH_DEFAULT_DOTS_FADE, enh_dots_speckle = ENH_DEFAULT_DOTS_SPECKLE;
int enh_dots_far_bright = ENH_DEFAULT_DOTS_FAR_BRIGHT;
int enh_dots_edge = ENH_DEFAULT_DOTS_EDGE, enh_dots_cover = ENH_DEFAULT_DOTS_COVER;
bool enh_dots_debug;
static double plane_scale = 1.0;      /* --plane-size / 100 */
int enh_hitbox_size = 100;
static ESnap *last_main;               /* the snapshot of AP's own pass, re-captured by the enhanced pass */
static u8 save_mem[MEM_SIZE], save_vram[VGA_VRAM_SIZE], save_tag[VGA_VRAM_SIZE];
static u16 save_sp;
static u64 scene_t0;

u8 *enh_vtag;
u8 enh_vclass = ENH_TAG_2D;
static u8 vtag_store[VGA_VRAM_SIZE];

ESnap enh_snaps[ENH_NSNAP];
static u32 snap_seq;
static ESnap *cur;                     /* the snapshot being captured (inside render_scene) */
static u32 cur_key;                    /* draw_object's object */
static s32 cur_obj = -1;
static bool cur_aircraft;              /* draw_object's object is an aircraft */
static double cur_eye_scale = 1.0;
static double cur_R[9];
static bool tr_on;                     /* enh_tracer */
static double tr_back[3];

/* the polygon announced by enh_poly, recorded by enh_poly_fill */
#define PEND_MAX 64
static EV3 pend_v[PEND_MAX];
static u16 pend_n;
static bool pend_ok;

void enh_init(bool on, int res_scale, int aa, bool widescreen)
{
    enabled = on;
    if (res_scale < 1) res_scale = 1;
    if (res_scale > ENH_MAX_RES_SCALE) res_scale = ENH_MAX_RES_SCALE;
    if (aa < 1) aa = 1;
    if (aa > ENH_MAX_AA) aa = ENH_MAX_AA;
    enh_scale = on ? res_scale : 1;
    enh_aa = on ? aa : 1;
    enh_wide = on && widescreen;
    enh_vtag = on ? vtag_store : NULL;
    memset(vtag_store, ENH_TAG_2D, sizeof vtag_store);
}

void enh_init_stage2(int view_dist, bool lod, bool haze)
{
    if (view_dist < 1) view_dist = 1;
    if (view_dist > ENH_MAX_VIEW_DIST) view_dist = ENH_MAX_VIEW_DIST;
    enh_view_dist = enabled ? view_dist : 1;
    lod_on = enabled && lod;
    enh_haze = enabled && haze;
    pass_on = enabled && (enh_view_dist > 1 || lod_on);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void enh_init_planes(int size, int hitbox)
{
    /* larger than 100 % would be cut off at the edge of the original's (world-tagged) model */
    plane_scale = enabled ? clampi(size, ENH_MIN_PLANE_SIZE, 100) / 100.0 : 1.0;
    enh_hitbox_size = enabled ? clampi(hitbox, ENH_MIN_HITBOX_SIZE, ENH_MAX_HITBOX_SIZE) : 100;
}

void enh_init_dots(bool on, int range, int alt, int size, int full, int near, int scatter, int far, int fade,
                   int speckle, int far_bright, int edge, int cover)
{
    dots_on = enabled && on;
    enh_dots_range = clampi(range, 1, ENH_MAX_DOTS_RANGE);
    enh_dots_alt = clampi(alt, 0, ENH_MAX_DOTS_ALT);
    enh_dots_size = clampi(size, 10, ENH_MAX_DOTS_SIZE);
    enh_dots_full = clampi(full, 0, ENH_MAX_DOTS_FULL);
    enh_dots_near = clampi(near, 5, 100);
    enh_dots_scatter = clampi(scatter, 0, 100);
    enh_dots_far = clampi(far, 0, ENH_MAX_DOTS_FAR);
    enh_dots_fade = clampi(fade, 0, 100);
    enh_dots_speckle = clampi(speckle, 0, ENH_MAX_DOTS_SPECKLE);
    enh_dots_far_bright = clampi(far_bright, 0, 100);
    enh_dots_edge = clampi(edge, 5, 100);
    enh_dots_cover = clampi(cover, 1, ENH_MAX_DOTS_COVER);
}

bool enh_enabled(void) { return enabled; }

static u64 prof_sum[PROF_N], prof_cnt[PROF_N], prof_t0;
void enh_prof(int slot, u64 ns)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("AOTP_ENH_PROF"); on = e && *e && *e != '0'; }
    if (!on) return;
    prof_sum[slot] += ns; prof_cnt[slot]++;
    u64 now = host_ns();
    if (!prof_t0) prof_t0 = now;
    if (now - prof_t0 < 2000000000ull) return;
    static const char *names[PROF_N] = { "scene", "pass", "copy", "render", "resolve", "compose" };
    fprintf(stderr, "[enh prof]");
    for (int i = 0; i < PROF_N; i++) {
        fprintf(stderr, " %s %.2fms x%llu", names[i], prof_cnt[i] ? prof_sum[i] / 1e6 / prof_cnt[i] : 0.0,
                (unsigned long long)prof_cnt[i]);
        prof_sum[i] = prof_cnt[i] = 0;
    }
    fprintf(stderr, "%s", "\n");
    prof_t0 = now;
}

void enh_frame_size(int *w, int *h, int *x0, int *sw)
{
    int W = enh_wide ? ENH_WIDE_W : 320;
    *w = W * enh_scale;
    *h = 200 * enh_scale;
    *x0 = (W - 320) / 2 * enh_scale;
    *sw = 320 * enh_scale;
}

void enh_vram_cleared(void)
{
    if (enh_vtag) memset(enh_vtag, ENH_TAG_2D, VGA_VRAM_SIZE);
}

/* ------------------------------------------------------------------------------------------ snapshots */

const ESnap *enh_snap_for_page(u16 page)
{
    const ESnap *best = NULL;
    for (int i = 0; i < ENH_NSNAP; i++) {
        const ESnap *s = &enh_snaps[i];
        if (s->complete && s->page == page && (!best || s->seq > best->seq)) best = s;
    }
    return best;
}

const ESnap *enh_snap_before(const ESnap *s)
{
    for (int i = 0; i < ENH_NSNAP; i++)
        if (enh_snaps[i].complete && enh_snaps[i].seq + 1 == s->seq) return &enh_snaps[i];
    return NULL;
}

static void *grow(void *p, u32 *cap, u32 need, size_t elem)
{
    if (need <= *cap) return p;
    u32 c = *cap ? *cap : 256;
    while (c < need) c *= 2;
    void *q = realloc(p, (size_t)c * elem);
    if (!q) host_fatal("enhanced view: out of memory");
    *cap = c;
    return q;
}

/* 2.14 matrix of the 3Space core (render3d_math.md §3.1): 9 words + type byte; type <= 0 = identity. */
static void mat_read(const u8 *m, double out[9])
{
    if ((s8)m[0x12] <= 0) {
        for (int i = 0; i < 9; i++) out[i] = (i % 4 == 0) ? 1.0 : 0.0;
        return;
    }
    for (int i = 0; i < 9; i++) out[i] = (double)gs16(m, 2 * i) / 16384.0;
}

void enh_orthonormalize(double M[9])
{
    double c[3][3];
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) c[j][i] = M[3 * i + j];
    int order[3] = { 1, 0, 2 };                          /* forward first, then right, then up */
    for (int a = 0; a < 3; a++) {
        double *v = c[order[a]];
        for (int b = 0; b < a; b++) {
            double *u = c[order[b]];
            double d = v[0] * u[0] + v[1] * u[1] + v[2] * u[2];
            for (int i = 0; i < 3; i++) v[i] -= d * u[i];
        }
        double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l < 1e-9) { for (int i = 0; i < 3; i++) v[i] = (i == order[a]) ? 1.0 : 0.0; continue; }
        for (int i = 0; i < 3; i++) v[i] /= l;
    }
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) M[3 * i + j] = c[j][i];
}

static bool capturing(void) { return cur != NULL; }

void enh_scene_begin(void)
{
    cur = NULL;
    if (!enabled || prog_current_id() != PROG_AP) return;
    ESnap *s;
    if (!enh_pass) scene_t0 = host_ns();
    if (enh_pass) {                                       /* the same frame again: same slot and number */
        if (!last_main) return;
        s = last_main;
    } else {
        s = &enh_snaps[(snap_seq + 1) % ENH_NSNAP];
        s->seq = ++snap_seq;
    }
    s->complete = false;
    s->page = LW(G_drv_page_write);
    s->t_ns = host_ns();
    u8 *K = cs_blk();
    for (int i = 0; i < 3; i++) s->cam[i] = (double)KSL(CS_CAM_POS + 4 * i);
    mat_read(K + CS_M_CAM, s->Mc);
    enh_orthonormalize(s->Mc);
    s->wx1 = DRV_X1; s->wx2 = DRV_X2; s->wy1 = DRV_Y1; s->wy2 = DRV_Y2;
    s->cx = (double)KS(CS_CX);
    s->cy = (double)KS(CS_CY);
    s->f = (double)KW(CS_FOCAL);
    s->near_d = s->f / 8.0;                               /* draw_object: set_near(n - 3) */
    s->has_sky = false;
    s->dots = false;
    s->npoly = s->nvert = s->nobj = 0;
    cur = s;
    cur_key = 0;
    cur_obj = -1;
    cur_aircraft = false;
    cur_eye_scale = 1.0;
    enh_vclass = ENH_TAG_SCENE;
}

void enh_scene_end(void)
{
    if (!cur) return;
    cur->complete = true;
    if (!enh_pass) { last_main = cur; enh_prof(PROF_SCENE, host_ns() - scene_t0); }
    cur = NULL;
    enh_vclass = ENH_TAG_2D;
}

/* ------------------------------------------------------------------------------ Stage 2: enhanced pass */

static u64 pass_t0;

bool enh_pass_begin(void)
{
    if (!pass_on || enh_pass || !last_main || prog_current_id() != PROG_AP) return false;
    /* The audio thread runs the game's sound service on mem[] (host.c audio_callback): it must not run
     * while mem[] is saved and restored, or its writes made meanwhile would be undone. */
    host_audio_lock();
    pass_t0 = host_ns();
    memcpy(save_mem, mem, MEM_SIZE);
    memcpy(save_vram, vga_vram(), VGA_VRAM_SIZE);
    memcpy(save_tag, vtag_store, VGA_VRAM_SIZE);
    save_sp = dg_sp;
    ems_journal_begin();
    enh_prof(PROF_COPY, host_ns() - pass_t0);
    enh_pass = true;
    return true;
}

void enh_pass_end(void)
{
    if (!enh_pass) return;
    u64 t1 = host_ns();
    enh_pass = false;
    last_main = NULL;
    cur = NULL;
    if (!ems_journal_end()) {
        static bool warned;
        if (!warned) fprintf(stderr, "enhanced view: EMS allocation changed during the enhanced pass\n");
        warned = true;
    }
    memcpy(mem, save_mem, MEM_SIZE);
    memcpy(vga_vram(), save_vram, VGA_VRAM_SIZE);
    memcpy(vtag_store, save_tag, VGA_VRAM_SIZE);
    dg_sp = save_sp;
    enh_vclass = ENH_TAG_2D;
    host_audio_unlock();
    u64 t2 = host_ns();
    enh_prof(PROF_COPY, t2 - t1);
    enh_prof(PROF_PASS, t2 - pass_t0);
}

bool enh_lod_finest(void) { return enh_pass && lod_on; }

void enh_far_dot(bool inside)
{
    /* AP's own pass: with the enhanced pass on, the aircraft behind a far dot is drawn as a model there */
    if (capturing() && !enh_pass && pass_on) enh_vclass = inside ? ENH_TAG_WORLD : ENH_TAG_SCENE;
}

bool enh_ground_dots(const u8 *col)
{
    if (!capturing() || !dots_on) return false;
    cur->dots = true;
    cur->dots_at = cur->npoly;
    memcpy(cur->dot_col, col, sizeof cur->dot_col);
    return true;
}

/* horizon.c: H_SPACING 031A, H_BANDS 031C, H_STEP 031E, H_ON 0320 (render3d_sky.md §2) */
void enh_sky(u16 sky, u16 sea)
{
    if (!capturing()) return;
    cur->has_sky = true;
    cur->sky = (u8)sky;
    cur->sea = (u8)sea;
    cur->spacing = DSW(0x031A);
    s16 bands = (s16)DSW(0x031C);
    cur->bands = (u8)(bands < 0 ? 0 : bands > 15 ? 15 : bands);
    cur->step = (u8)DSW(0x031E);
    cur->bands_on = DSW(0x0320) != 0 && cur->bands > 0 && cur->spacing > 0;
    enh_vclass = ENH_TAG_WORLD;
}

void enh_sky_end(void)
{
    if (capturing()) enh_vclass = ENH_TAG_SCENE;
}

void enh_sky_callback(bool inside)
{
    if (capturing()) enh_vclass = inside ? ENH_TAG_SCENE : ENH_TAG_WORLD;
}

void enh_object(u32 key, bool aircraft)
{
    if (!capturing()) return;
    cur_key = key;
    cur_obj = -1;
    cur_aircraft = aircraft;
}

void enh_tracer(bool on, s16 bx, s16 by, s16 bz)
{
    tr_on = on;
    tr_back[0] = bx; tr_back[1] = by; tr_back[2] = bz;
}

void enh_ts_object(u16 eye_shift)
{
    if (!capturing()) return;
    u8 *K = cs_blk();
    bool shadow = DSW(DS_SHADOW_MODE) != 0;
    cur_eye_scale = ldexp(1.0, eye_shift & 31);
    double Moe[9];
    mat_read(K + CS_M_OBJ_EYE, Moe);
    /* R = Moe · Mc^T: local -> world axes */
    const double *Mc = cur->Mc;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double a = 0;
            for (int k = 0; k < 3; k++) a += Moe[3 * i + k] * Mc[3 * j + k];
            cur_R[3 * i + j] = a;
        }
    enh_orthonormalize(cur_R);
    if (!cur_key) { cur_obj = -1; return; }
    cur->obj = grow(cur->obj, &cur->cobj, cur->nobj + 1, sizeof(EObj));
    EObj *o = &cur->obj[cur->nobj];
    o->key = cur_key ^ (shadow ? 0x80000000u : 0);
    o->shadow = shadow;
    o->ord = 0;
    o->tracer = tr_on;
    memcpy(o->back, tr_back, sizeof o->back);
    for (u32 i = 0; i < cur->nobj; i++) if (cur->obj[i].key == o->key) o->ord++;
    for (int i = 0; i < 3; i++) o->pos[i] = cur->cam[i] + (double)KSL(CS_D_WORLD + 4 * i);
    memcpy(o->R, cur_R, sizeof o->R);
    cur_obj = (s32)cur->nobj++;
}

void enh_poly(bool wide)
{
    pend_ok = false;
    if (!capturing()) return;
    u8 *K = cs_blk();
    u16 n = KW(CS_POLY_N);
    if (n == 0 || n > PEND_MAX) return;
    const double *Mc = cur->Mc;
    for (u16 i = 0; i < n; i++) {
        u16 bx = KW(CS_POLY_IDX + 2 * i);
        double e[3];
        if (wide) {
            e[0] = (double)(s32)((u32)KW(CS_EX + bx) | (u32)KW(CS_EXH + bx) << 16);
            e[1] = (double)(s32)((u32)KW(CS_EY + bx) | (u32)KW(CS_EYH + bx) << 16);
            e[2] = (double)(s32)((u32)KW(CS_EZ + bx) | (u32)KW(CS_EZH + bx) << 16);
        } else {
            e[0] = KS(CS_EX + bx);
            e[1] = KS(CS_EY + bx);
            e[2] = KS(CS_EZ + bx);
        }
        for (int k = 0; k < 3; k++) e[k] *= cur_eye_scale;
        /* d = e · Mc^T */
        pend_v[i].x = (float)(e[0] * Mc[0] + e[1] * Mc[1] + e[2] * Mc[2]);
        pend_v[i].y = (float)(e[0] * Mc[3] + e[1] * Mc[4] + e[2] * Mc[5]);
        pend_v[i].z = (float)(e[0] * Mc[6] + e[1] * Mc[7] + e[2] * Mc[8]);
    }
    pend_n = n;
    pend_ok = true;
}

void enh_poly_fill(void)
{
    if (!capturing()) return;
    enh_vclass = ENH_TAG_WORLD;
    if (!pend_ok) return;
    pend_ok = false;
    cur->vert = grow(cur->vert, &cur->cvert, cur->nvert + pend_n, sizeof(EV3));
    cur->poly = grow(cur->poly, &cur->cpoly, cur->npoly + 1, sizeof(EPoly));
    EPoly *p = &cur->poly[cur->npoly++];
    p->first = cur->nvert;
    p->n = pend_n;
    p->obj = (s16)cur_obj;
    p->fill_on = DRV_FILL_ON;
    p->fill = DRV_FILL_COL;
    p->edge = DRV_EDGE_COL;
    if (cur_aircraft && plane_scale != 1.0 && cur_obj >= 0) {   /* --plane-size: about the model's origin */
        const double *op = cur->obj[cur_obj].pos;
        double c[3] = { op[0] - cur->cam[0], op[1] - cur->cam[1], op[2] - cur->cam[2] };
        for (u16 i = 0; i < pend_n; i++) {
            pend_v[i].x = (float)(c[0] + (pend_v[i].x - c[0]) * plane_scale);
            pend_v[i].y = (float)(c[1] + (pend_v[i].y - c[1]) * plane_scale);
            pend_v[i].z = (float)(c[2] + (pend_v[i].z - c[2]) * plane_scale);
        }
    }
    memcpy(cur->vert + cur->nvert, pend_v, sizeof(EV3) * pend_n);
    cur->nvert += pend_n;
}

void enh_poly_done(void)
{
    if (capturing()) enh_vclass = ENH_TAG_SCENE;
}
