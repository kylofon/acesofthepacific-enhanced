/* Developer aids of the video module (not game code):
 *   AOTP_TEST=pictures  every BMP frame and SCR listed in work/img/report.txt (tools/dynimg.py all) is
 *                       loaded with the platform loaders and drawn through the ported driver/library
 *                       (bmp_draw, scr_load -> VQT / nibble-plane writers). Per resource: <out>/<NAME>_<EXT>.png
 *                       in dynimg's strip layout (frames side by side, wrapped at 320, 2 px gaps, background
 *                       80,0,80; palette from report.txt), and <NAME>_<EXT>.idx = the raw frame indices.
 *   AOTP_TEST=ttm       plays AOTP_TTM (default INTACE1.TTM) headless with the §14 frame loop (run, flip,
 *                       update-list flush; frame delays skipped) and saves the displayed page with the
 *                       DAC palette after every frame: <out>/ttm_NNN.png.
 * Output directory: AOTP_OUT (default "vid_test_out"); report list: AOTP_IMG_REPORT (default
 * ../../work/img/report.txt). Entry: video_dev_test() — returns -1 when AOTP_TEST names none of these. */
#include "video/vid_priv.h"

#include "platform/platform.h"
#include "platform/vga.h"
#include "codeptr.h"
#include "symbols_intro.h"
#include "symbols_ps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0777)
#endif

/* ---------------------------------------------------------------- minimal PNG writer (stored deflate) */

static u32 crc_tab[256];
static u32 crc32_upd(u32 c, const u8 *p, size_t n)
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
static void chunk(FILE *f, const char *type, const u8 *d, u32 n)
{
    u8 h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    u32 c = crc32_upd(0xFFFFFFFFu, (const u8 *)type, 4);
    c = crc32_upd(c, d, n) ^ 0xFFFFFFFFu;
    be32(h, c);
    fwrite(h, 1, 4, f);
}
/* rgb: w*h*3 bytes */
static bool png_write_rgb(const char *path, int w, int h, const u8 *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    u8 ih[13];
    be32(ih, (u32)w); be32(ih + 4, (u32)h);
    ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13);
    size_t raw_n = (size_t)h * (size_t)(w * 3 + 1);
    u8 *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (w * 3 + 1)] = 0;
        memcpy(raw + (size_t)y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    size_t nblk = raw_n / 65535 + 1;
    u8 *z = malloc(raw_n + nblk * 5 + 6);
    size_t zn = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;
    u32 a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    size_t pos = 0;
    do {
        size_t n = raw_n - pos > 65535 ? 65535 : raw_n - pos;
        z[zn++] = pos + n == raw_n ? 1 : 0;
        z[zn++] = (u8)n; z[zn++] = (u8)(n >> 8); z[zn++] = (u8)~n; z[zn++] = (u8)(~n >> 8);
        memcpy(z + zn, raw + pos, n);
        zn += n; pos += n;
    } while (pos < raw_n);
    be32(z + zn, (b << 16) | a); zn += 4;
    chunk(f, "IDAT", z, (u32)zn);
    chunk(f, "IEND", NULL, 0);
    free(raw); free(z);
    fclose(f);
    return true;
}

/* ---------------------------------------------------------------- helpers */

static const char *out_dir(void) { const char *o = getenv("AOTP_OUT"); return o && *o ? o : "vid_test_out"; }

static u16 ds_str(const char *s)                     /* a string on the emulated stack */
{
    u16 p = stk_alloc((u16)(strlen(s) + 1));
    memcpy(DSP(p), s, strlen(s) + 1);
    return p;
}

static void pal_rgb(const char *name, u8 pal[768])   /* 6-bit -> c*255/63, grey if the load fails */
{
    for (int i = 0; i < 256; i++) pal[3 * i] = pal[3 * i + 1] = pal[3 * i + 2] = (u8)i;
    if (!name || !*name) return;
    u16 m = stk_mark();
    u32 p = pal_load(ds_str(name));
    stk_release(m);
    if (!p) return;
    FarPtr f = far_make((u16)(p >> 16), (u16)p);
    for (int i = 0; i < 768; i++) pal[i] = (u8)((rd8(f.seg, (u16)(f.off + i)) & 63) * 255 / 63);
    pal_free(f);
}

static void video_setup(void)
{
    if (!LW(G_drv_page_front)) gfx_init(8, 0xFF, 0);
    DRVB(D_CLIP_ON) = 1; DRVB(D_FILL_ON) = 1;
    DRVW(D_CLIP_X1) = 0; DRVW(D_CLIP_Y1) = 0; DRVW(D_CLIP_X2) = 319; DRVW(D_CLIP_Y2) = 199;
    DRVW(D_PAGE_READ) = DRVW(D_PAGE_BACK);
    DRVW(D_PAGE_WRITE) = DRVW(D_PAGE_BACK);
}

/* ---------------------------------------------------------------- AOTP_TEST=pictures */

typedef struct { int x, y, w, h; } Cell;

/* dynimg.strip(): frames side by side, wrap when a row would pass 320, 2 px gaps */
static int strip_layout(const int *ws, const int *hs, int n, Cell *c, int *W, int *H)
{
    int y = 0, rw = 0, rh = 0, row0 = 0, maxw = 0;
    for (int i = 0; i < n; i++) {
        if (i > row0 && rw + ws[i] > 320) {
            if (rw > maxw) maxw = rw;
            y += rh + 2; rw = 0; rh = 0; row0 = i;
        }
        c[i].x = rw; c[i].y = y; c[i].w = ws[i]; c[i].h = hs[i];
        rw += ws[i] + 2;
        if (hs[i] > rh) rh = hs[i];
    }
    if (n) { if (rw > maxw) maxw = rw; y += rh + 2; }
    *W = maxw > 0 ? maxw : 1;
    *H = y > 0 ? y : 1;
    return n;
}

static void save_strip(const char *base, const u8 *const *px, const int *ws, const int *hs, int n, const u8 pal[768])
{
    Cell *c = calloc((size_t)(n ? n : 1), sizeof *c);
    int W, H;
    strip_layout(ws, hs, n, c, &W, &H);
    u8 *rgb = malloc((size_t)W * H * 3);
    for (int i = 0; i < W * H; i++) { rgb[3 * i] = 80; rgb[3 * i + 1] = 0; rgb[3 * i + 2] = 80; }
    for (int i = 0; i < n; i++)
        for (int y = 0; y < c[i].h; y++)
            for (int x = 0; x < c[i].w; x++) {
                u8 v = px[i][y * c[i].w + x];
                u8 *d = rgb + ((size_t)(c[i].y + y) * W + c[i].x + x) * 3;
                d[0] = pal[3 * v]; d[1] = pal[3 * v + 1]; d[2] = pal[3 * v + 2];
            }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", out_dir(), base);
    png_write_rgb(path, W, H, rgb);
    snprintf(path, sizeof path, "%s/%s.idx", out_dir(), base);
    FILE *f = fopen(path, "wb");
    if (f) {
        for (int i = 0; i < n; i++) fwrite(px[i], 1, (size_t)ws[i] * hs[i], f);
        fclose(f);
    }
    free(rgb); free(c);
}

/* reads w*h pixels (clamped to the page) from the write page */
static u8 *grab(int w, int h)
{
    u8 *b = calloc((size_t)(w * h > 0 ? w * h : 1), 1);
    const u8 *pg = vga_page(DRVW(D_PAGE_WRITE));
    for (int y = 0; y < h && y < 200; y++)
        for (int x = 0; x < w && x < 320; x++) b[y * w + x] = pg[y * 320 + x];
    return b;
}
static void clear_page(void) { memset(vga_page(DRVW(D_PAGE_WRITE)), 0, 64000); }

static int test_pictures(void)
{
    const char *rp = getenv("AOTP_IMG_REPORT");
    if (!rp || !*rp) rp = "../../work/img/report.txt";
    FILE *rf = fopen(rp, "r");
    if (!rf) { fprintf(stderr, "AOTP_TEST=pictures: %s not found (run python tools/dynimg.py all)\n", rp); return 4; }
    video_setup();
    char line[256], done[1024][16];
    int ndone = 0, nbmp = 0, nscr = 0, nfail = 0, nframes = 0;
    while (fgets(line, sizeof line, rf)) {
        char vol[32], name[32], kind[8], sub[16], palname[32] = "";
        if (sscanf(line, "%31s %31s %7s %15s", vol, name, kind, sub) < 4) continue;
        const char *pp = strstr(line, " pal ");
        if (pp) sscanf(pp + 5, "%31s", palname);
        bool scr = !strcmp(kind, "scr");
        if (!scr && strcmp(kind, "bmp")) continue;
        bool dup = false;
        for (int i = 0; i < ndone; i++) dup |= !strcmp(done[i], name);
        if (dup || ndone >= 1024) continue;
        snprintf(done[ndone++], 16, "%s", name);
        char base[40];
        snprintf(base, sizeof base, "%s", name);
        for (char *c = base; *c; c++) if (*c == '.') *c = '_';
        u8 pal[768];
        pal_rgb(palname, pal);
        u16 m = stk_mark(), nm = ds_str(name);
        if (scr) {
            clear_page();
            scr_load(nm, 0);                             /* PORT: the name is scr_load's first word */
            u8 *px = grab(320, 200);
            const u8 *pl[1] = { px };
            int w = 320, h = 200;
            save_strip(base, pl, &w, &h, 1, pal);
            free(px);
            nscr++;
        } else {
            u16 bmp = bmp_load(nm);
            if (!bmp) { fprintf(stderr, "pictures: %s: bmp_load failed\n", name); nfail++; stk_release(m); continue; }
            int n = 0;
            while (DSW((u16)(bmp + 2 * n))) n++;
            u8 **px = calloc((size_t)(n ? n : 1), sizeof *px);
            int *ws = calloc((size_t)(n ? n : 1), sizeof *ws), *hs = calloc((size_t)(n ? n : 1), sizeof *hs);
            for (int i = 0; i < n; i++) {
                u16 fr = DSW((u16)(bmp + 2 * i));
                ws[i] = DSW(fr + 6); hs[i] = DSW(fr + 8);
                clear_page();
                bmp_draw(fr, 0, 0, 0);
                px[i] = grab(ws[i], hs[i]);
            }
            save_strip(base, (const u8 *const *)px, ws, hs, n, pal);
            for (int i = 0; i < n; i++) free(px[i]);
            free(px); free(ws); free(hs);
            bmp_free_w(bmp);
            nbmp++; nframes += n;
        }
        stk_release(m);
    }
    fclose(rf);
    printf("AOTP_TEST=pictures: %d BMP (%d frames), %d SCR drawn, %d failed -> %s\n", nbmp, nframes, nscr, nfail, out_dir());
    return nfail ? 4 : 0;                                /* 1..3 would chain to another program */
}

/* ---------------------------------------------------------------- AOTP_TEST=ttm */

static void snap_front(int f)
{
    const u8 *pg = vga_page(vga_shown_seg());
    u8 *rgb = malloc(64000 * 3);
    u8 dac[768];
    for (int i = 0; i < 256; i++) {
        u8 r, g, b;
        vga_dac_get((u8)i, &r, &g, &b);
        dac[3 * i] = (u8)(r * 255 / 63); dac[3 * i + 1] = (u8)(g * 255 / 63); dac[3 * i + 2] = (u8)(b * 255 / 63);
    }
    for (int i = 0; i < 64000; i++) memcpy(rgb + 3 * i, dac + 3 * pg[i], 3);
    char path[512];
    snprintf(path, sizeof path, "%s/ttm_%03d.png", out_dir(), f);
    png_write_rgb(path, 320, 200, rgb);
    free(rgb);
}

static int test_ttm(void)
{
    const char *name = getenv("AOTP_TTM");
    if (!name || !*name) name = "INTACE1.TTM";
    video_setup();
    /* the caller's 59.18 Hz TTM counter (INTRO 0460:00d3 / PS 21f4:00cd set it up) */
    if (pid() == PROG_INTRO) timer_add(codeptr_far(INTRO_FN_intro_tick_cb), 4);
    else if (pid() == PROG_PS) { timer_add(codeptr_far(PS_FN_shell_tick_cb), 4); DSL(PS_DS_shell_ticks) = 0; }
    ttm_init();
    u16 m = stk_mark();
    s16 r = ttm_load(ds_str(name));
    stk_release(m);
    if (r != 1) { fprintf(stderr, "AOTP_TEST=ttm: ttm_load(%s) = %d\n", name, r); return 4; }
    if (ttm_index(0) != 1) { fprintf(stderr, "AOTP_TEST=ttm: ttm_index failed\n"); return 4; }
    int f = 0;
    for (; f < 2000; f++) {
        LW(G_ttm_delay) = 0xFFFF;
        if (!ttm_run((u16)f)) break;
        gfx_flip(1);
        upd_flush(0);
        snap_front(f);
    }
    ttm_shutdown();
    printf("AOTP_TEST=ttm: %s: %d frames -> %s/ttm_*.png\n", name, f, out_dir());
    return 0;
}

int video_dev_test(void)
{
    const char *t = getenv("AOTP_TEST");
    if (!t) return -1;
    bool pics = !strcmp(t, "pictures"), ttm = !strcmp(t, "ttm");
    if (!pics && !ttm) return -1;
    make_dir(out_dir());                                  /* may exist already */
    vol_init(0, 0x4000, 1);
    return pics ? test_pictures() : test_ttm();
}
