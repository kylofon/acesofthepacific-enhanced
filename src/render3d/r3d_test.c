/* render3d developer aid (not game code): AOTP_TEST=shapes, run in AP (--start 3).
 *
 * Loads the shape tables AOTP_SHAPES (default "F4U ZERO_52 ESSEX P80", ".TBL" appended) with ts_init /
 * ts_load_table, then draws shape 0 of each through the ported pipeline (cam_begin_frame, set_light,
 * ts_draw → part routines → the library polygon fill) into the back page at the dynshape.py default
 * camera (yaw 210°, pitch 20°, eye at 1.5 radii from the box centre, focal 128, 320×200 window) and
 * saves <out>/<TBL>.png with AOTP_PAL (default ACES.PAL; SIM.PAL is in SHELL1) (out = AOTP_OUT, default "r3d_test_out"). Compare with
 * work/shapes/<TBL>_<name>.png (tools/dynshape.py: independent renderer, different projection scale).
 * Also runs a few self-checks of the math library against closed forms.
 *
 * Entry: render3d_dev_test() — returns -1 when AOTP_TEST is not "shapes". It is called from
 * platform_dev_test (REQUESTS); a scratch build can wrap platform_dev_test with -DR3D_WRAP_DEVTEST
 * -Wl,--wrap=platform_dev_test. */
#include "render3d/r3d_priv.h"
#include "codeptr.h"
#include "video/video.h"
#include "platform/platform.h"
#include "platform/vga.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0777)
#endif

int render3d_dev_test(void);

/* ---- minimal PNG writer (stored deflate) ----------------------------------------------------------- */
static u32 crc_tab[256];
static u32 crc_upd(u32 c, const u8 *p, size_t n)
{
    if (!crc_tab[1])
        for (u32 i = 0; i < 256; i++) {
            u32 v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_tab[i] = v;
        }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void png_chunk(FILE *f, const char *t, const u8 *d, u32 n)
{
    u8 h[8];
    be32(h, n); memcpy(h + 4, t, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    be32(h, crc_upd(crc_upd(0xFFFFFFFFu, (const u8 *)t, 4), d, n) ^ 0xFFFFFFFFu);
    fwrite(h, 1, 4, f);
}
static void png_rgb(const char *path, int w, int h, const u8 *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    u8 ih[13];
    be32(ih, (u32)w); be32(ih + 4, (u32)h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    png_chunk(f, "IHDR", ih, 13);
    size_t rn = (size_t)h * (size_t)(w * 3 + 1);
    u8 *raw = malloc(rn), *z = malloc(rn + rn / 65535 * 5 + 16);
    for (int y = 0; y < h; y++) { raw[(size_t)y * (w * 3 + 1)] = 0; memcpy(raw + (size_t)y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3); }
    size_t zn = 0, pos = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;
    u32 a = 1, b = 0;
    for (size_t i = 0; i < rn; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    do {
        size_t n = rn - pos > 65535 ? 65535 : rn - pos;
        z[zn++] = pos + n == rn; z[zn++] = (u8)n; z[zn++] = (u8)(n >> 8); z[zn++] = (u8)~n; z[zn++] = (u8)(~n >> 8);
        memcpy(z + zn, raw + pos, n); zn += n; pos += n;
    } while (pos < rn);
    be32(z + zn, b << 16 | a); zn += 4;
    png_chunk(f, "IDAT", z, (u32)zn);
    png_chunk(f, "IEND", NULL, 0);
    free(raw); free(z);
    fclose(f);
}

static u16 ds_str(const char *s) { u16 p = stk_alloc((u16)(strlen(s) + 1)); memcpy(DSP(p), s, strlen(s) + 1); return p; }
static const char *out_dir(void) { const char *o = getenv("AOTP_OUT"); return o && *o ? o : "r3d_test_out"; }

/* ---- math self-checks ------------------------------------------------------------------------------ */
static int math_checks(void)
{
    int bad = 0;
    for (u32 n = 0; n < 0x40000; n += 7) {
        u32 r = (u32)floor(sqrt((double)n));
        if (r3d_isqrt32(n) != r) { if (bad++ < 5) printf("isqrt32(%u) = %u, want %u\n", n, r3d_isqrt32(n), r); }
    }
    for (u32 n = 0xFFFF0000u; n; n += 0x1234567) { u32 r = (u32)floor(sqrt((double)n)); if (r3d_isqrt32(n) != r) bad++; if (n > 0xFFFFFFFFu - 0x1234567) break; }
    for (int i = 0; i <= 1024; i++) {
        s16 c = (s16)cos16((u16)(i << 4));
        s16 want = (s16)trunc(16384.0 * cos(i * M_PI / 2048));
        if (c != want && !(i == 1024 && c == 0)) { if (bad++ < 10) printf("cos16(%d) = %d, want %d\n", i << 4, c, want); }
    }
    u16 a = atan2_16(1000, 1000);
    if (a != 0x2000) { bad++; printf("atan2_16(1000,1000) = %04x\n", a); }
    printf("render3d math checks: %d mismatches\n", bad);
    return bad;
}

/* ---- AOTP_TEST=shapes ------------------------------------------------------------------------------ */
static int draw_table(const char *tbl, u16 view, const u8 pal[768])
{
    u16 m = stk_mark();
    char name[32];
    snprintf(name, sizeof name, "%s.TBL", tbl);
    u32 t = ts_load_table_far(0, ds_str(name));
    if (!t) { printf("%s: ts_load_table failed\n", name); stk_release(m); return 1; }
    FarPtr hdr = shape_ptr(0);
    u8 *h = far_mp(hdr);
    s16 sh = h[3];
    double r = (double)gs16(h, 0x0C) * (1 << sh);
    double cxo = 0, cyo = 0, czo = 0;
    if (!(h[0] & 0x20)) {
        cxo = (gs16(h, 0x0E) + gs16(h, 0x14)) / 2.0 * (1 << sh);
        cyo = (gs16(h, 0x10) + gs16(h, 0x16)) / 2.0 * (1 << sh);
        czo = (gs16(h, 0x12) + gs16(h, 0x18)) / 2.0 * (1 << sh);
    }
    double ya = 210 * M_PI / 180, pa = 20 * M_PI / 180, dist = r * 1.5;
    const char *ds = getenv("AOTP_DIST");
    if (ds) dist = r * atof(ds);
    u16 eye = DSW(view + 2);
    DSSL(eye + 2) = (s32)lround(cxo + sin(ya) * cos(pa) * dist);
    DSSL(eye + 6) = (s32)lround(cyo + cos(ya) * cos(pa) * dist);
    DSSL(eye + 0x0A) = (s32)lround(czo + sin(pa) * dist);
    DSW(eye + 0x0E) = (u16)lround(-20 * 65536.0 / 360);             /* pitch down */
    DSW(eye + 0x10) = 0;
    DSW(eye + 0x12) = (u16)lround((180.0 - 210.0) * 65536.0 / 360);   /* heading = 180° − yaw */
    u16 o = stk_alloc(0x16);
    memset(DSP(o), 0, 0x16);                                           /* id 0 at the origin, angles 0 */
    u8 *pg = vga_page(LW(G_drv_page_write));
    memset(pg, 0, 64000);
    cam_begin_frame(view);
    /* dynshape's light 512·norm(−0.4, 0.5, 0.75) = pitch 49.5°, heading 38.7° (AOTP_LIGHT="p,h" degrees) */
    double lp = 49.5, lh = 38.7;
    const char *ls = getenv("AOTP_LIGHT");
    if (ls) sscanf(ls, "%lf,%lf", &lp, &lh);
    set_light((u16)lround(lp * 65536 / 360), (u16)lround(lh * 65536 / 360));
    set_near((u16)(get_near_n() - 3));                                 /* as 5440:0269 for world objects */
    ts_draw(o);
    reset_near();
    u8 *rgb = malloc(64000 * 3);
    for (int i = 0; i < 64000; i++) memcpy(rgb + 3 * i, pal + 3 * pg[i], 3);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", out_dir(), tbl);
    png_rgb(path, 320, 200, rgb);
    free(rgb);
    int n = 0;
    for (int i = 0; i < 64000; i++) n += pg[i] != 0;
    printf("%s: radius %.0f, shift %d, draw mode %u, app size %u, %d pixels -> %s\n", name, r, sh,
           CSB(CS_DRAW_MODE), CSW(CS_APP_SIZE), n, path);
    ts_free_table(0);
    stk_release(m);
    return n == 0;
}

/* ---- depth order: tree_build + tree_flatten of same-type entries must give descending keys */
static int scene_checks(void)
{
    static const s32 keys[] = { 500, 12000, 3, 900000, 12000, 77, 450000, 1, 65536, 800 };
    const int n = (int)(sizeof keys / sizeof keys[0]);
    FarPtr objs = mem_far_new((u32)(n * 0x16), 0), list = mem_far_new((u32)(n * 8), 0), out = mem_far_new((u32)(n * 8), 0);
    for (int i = 0; i < n; i++) {
        FarPtr o = far_add(objs, (u16)(i * 0x16));
        wr16(o.seg, o.off, 0x0001);                                    /* same type: plain key order */
        FarPtr e = far_add(list, (u16)(i * 8));
        far_wr(e.seg, e.off, o);
        wr32(e.seg, (u16)(e.off + 4), (u32)keys[i]);
    }
    static R3dNode T[256];
    memset(T, 0, sizeof T);
    tree_build((s16)n, list, T);
    for (int j = 0; j < n; j++) T[j].ptr = far_rd(T[j].ptr.seg, T[j].ptr.off);
    ds_far_wr(W_FLATTEN_PTR, out);
    tree_flatten(T);
    int bad = 0;
    s32 prev = 0x7FFFFFFF;
    for (int i = 0; i < n; i++) {
        FarPtr o = far_rd(out.seg, (u16)(out.off + 8 * i));
        s32 k = keys[(o.off - objs.off) / 0x16];
        if (k > prev) bad++;
        prev = k;
    }
    printf("render3d depth order check: %d errors\n", bad);
    mem_far_delete(objs); mem_far_delete(list); mem_far_delete(out);
    return bad != 0;
}

/* ---- horizon fill (draw_sky, render3d_sky.md §2) at a few attitudes: <out>/horizon_<pitch>_<roll>.png */
static int draw_horizons(u16 view, const u8 pal[768])
{
    static const int att[][2] = { { 0, 0 }, { -10, 20 }, { 15, -35 }, { 5, 70 }, { -20, 120 }, { 70, 0 }, { -70, 0 } };
    if (!sky_alloc()) { printf("sky_alloc failed\n"); return 1; }
    if (!DSW(0x3172)) { printf("span buffer segment DS:3172 not set\n"); return 1; }
    sky_set_detail(0xFF);
    DSW(0x0320) = 1;
    int bad = 0;
    for (size_t k = 0; k < sizeof att / sizeof att[0]; k++) {
        u16 eye = DSW(view + 2);
        memset(DSP(eye + 2), 0, 12);
        DSSL(eye + 0x0A) = 1000;
        DSW(eye + 0x0E) = (u16)lround(att[k][0] * 65536.0 / 360);
        DSW(eye + 0x10) = (u16)lround(att[k][1] * 65536.0 / 360);
        DSW(eye + 0x12) = 0;
        u8 *pg = vga_page(LW(G_drv_page_write));
        memset(pg, 0, 64000);
        cam_begin_frame(view);
        DRV_CLIP_ON = 1; DRV_X1 = 0; DRV_X2 = 319; DRV_Y1 = 0; DRV_Y2 = 199;
        sky_invalidate();
        draw_sky(0x70, 0x80, CSW(CS_CAM_ANG), CSW(CS_CAM_ANG + 2), 0);
        int sky = 0, sea = 0, other = 0;
        for (int i = 0; i < 64000; i++) {
            if (pg[i] >= 0x70 && pg[i] <= 0x7F) sky++;
            else if (pg[i] == 0x80) sea++;
            else other++;
        }
        u8 *rgb = malloc(64000 * 3);
        for (int i = 0; i < 64000; i++) memcpy(rgb + 3 * i, pal + 3 * pg[i], 3);
        char path[512];
        snprintf(path, sizeof path, "%s/horizon_%d_%d.png", out_dir(), att[k][0], att[k][1]);
        png_rgb(path, 320, 200, rgb);
        free(rgb);
        printf("horizon pitch %d roll %d: sky %d, sea %d, unfilled %d -> %s\n", att[k][0], att[k][1], sky, sea, other, path);
        bad |= other != 0;
    }
    return bad;
}

int render3d_dev_test(void)
{
    const char *t = getenv("AOTP_TEST");
    if (!t || strcmp(t, "shapes")) return -1;
    make_dir(out_dir());
    int bad = math_checks() ? 1 : 0;
    if (prog_current_id() != PROG_AP) { printf("AOTP_TEST=shapes: run AP (--start 3)\n"); return 4; }
    DSW(0x1EBC) = 0x0583;                                             /* as platform_dev_test for AP */
    vol_init(0, 0, 0);
    u16 m = stk_mark();
    gfx_init(8, 0xFF, ds_str("vmcode.ovl"));
    if (!DRV_VGA) DRV_VGA = 1;                                        /* VGA colours (the driver's info call) */
    u8 pal[768];
    for (int i = 0; i < 768; i++) pal[i] = (u8)(i / 3);
    const char *pn = getenv("AOTP_PAL");                            /* SIM.PAL is a shell (SHELL1) file */
    u32 pp = pal_load(ds_str(pn && *pn ? pn : "ACES.PAL"));
    if (pp) {
        FarPtr f = far_make((u16)(pp >> 16), (u16)pp);
        for (int i = 0; i < 768; i++) pal[i] = (u8)((rd8(f.seg, (u16)(f.off + i)) & 63) * 255 / 63);
    } else printf("SIM.PAL not loaded\n");
    /* SHADES.DAT (256 bytes) as the sim's DS:4092 */
    u16 shades = stk_alloc(256);
    memset(DSP(shades), 0, 256);
    u16 f = my_fopen(ds_str("SHADES.DAT"), ds_str("rb"));
    if (f) { my_fread(shades, 1, 256, f); my_fclose(f); } else printf("SHADES.DAT not found\n");
    ts_init(4, 4);
    u16 eye = stk_alloc(0x20);
    memset(DSP(eye), 0, 0x20);
    u16 view = view_new((s16)eye);
    DSW(view + 0x12) = shades;
    const char *list = getenv("AOTP_SHAPES");
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list && *list ? list : "F4U ZERO_52 ESSEX P80");
    for (char *tok = strtok(buf, " ,"); tok; tok = strtok(NULL, " ,")) bad |= draw_table(tok, view, pal);
    bad |= draw_horizons(view, pal);
    bad |= scene_checks();
    view_free(view);
    ts_done();
    stk_release(m);
    fflush(stdout);
    return bad ? 4 : 0;                                               /* 1..3 would chain to a program */
}

#ifdef R3D_WRAP_DEVTEST
int __real_platform_dev_test(void);
int __wrap_platform_dev_test(void)
{
    int r = render3d_dev_test();
    if (r >= 0) { fflush(stdout); exit(r); }
    return __real_platform_dev_test();
}
#endif
