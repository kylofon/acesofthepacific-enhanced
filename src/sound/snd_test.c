/* Developer aid (AOTP_TEST=dro): renders every sound of SHELL.SX, TTD.SX, SIMSNDS.SNG and SIMSNDS1.SNG
 * through the C player (player.c) and AdLib driver model (adlib.c) into OPL register streams and
 * compares them write-for-write (register, value, millisecond) with the DOSBox raw OPL captures of the
 * Python reference model, work/sound/<BANK>/<id>_<NAME>.dro (tools/aotpsnd.py; regenerate with
 * "python tools/aotpsnd.py all"). The render follows aotpsnd.render(): driver init, play(node, 1) for
 * SX sounds / play(node, 0) for SNG sounds, service ticks at 59.17 Hz until the sound ends, a looping
 * sound is stopped after 2 passes (and at least 8 s), 300 s cap, then 1.5 s of release ticks.
 *
 * Inputs: the files "aotpsnd.py extract" writes (work/sound/index.txt, <BANK>/<id>_<NAME>.snd, the driver and
 * patch chunks <OVL>/ADL.DRV, PATCH.003), read with stdio. Directory: AOTP_DRO_DIR, else ../work/sound
 * or work/sound. Nothing is played (no audio device is opened).
 *
 * Run by main.c's AOTP_TEST dispatch (AOTP_TEST=dro); returns 0 (all equal) or 1. */
#include "sound/snd_priv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* test memory map (no program is loaded) */
#define T_PLAYER_SEG 0x3000
#define T_DRV_SEG    0x3100
#define T_PATCH_SEG  0x3400
#define T_NODE_SEG   0x3600
#define T_RES_SEG    0x3700

typedef struct { double ms; u8 reg, val; } OplW;
static OplW *cap;
static size_t cap_n, cap_max;
static double cap_clock;

static void capture(u8 reg, u8 val)
{
    if (cap_n == cap_max) {
        cap_max = cap_max ? cap_max * 2 : 65536;
        cap = realloc(cap, cap_max * sizeof *cap);
        if (!cap) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    cap[cap_n].ms = cap_clock;
    cap[cap_n].reg = reg;
    cap[cap_n].val = val;
    cap_n++;
}

static u8 *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *d = malloc((size_t)n + 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    if (d) *len = (size_t)n;
    return d;
}

/* The player's variables as in the program image (INTRO 1373:018F..0388; identical in PS/AP). */
static void player_image_init(void)
{
    u8 *p = mp(T_PLAYER_SEG, 0);
    memset(p, 0, 0x400);
    memset(p + 0x020F, 0xFF, 16);            /* map */
    memset(p + 0x024F, 0xFF, 16);            /* t_entry */
    memset(p + 0x028F, 0xFF, 16);            /* t_entry backup */
    memset(p + 0x02DF, 0x0F, 16);            /* old_chan */
    memset(p + 0x032F, 0xFF, 16);            /* pend_vol */
    p[0x0376] = 0xFF;                        /* cur_dev */
    p[0x037E] = 0x0F;                        /* ch_last */
}

static int parse_dro(const u8 *d, size_t len, OplW **out, size_t *n_out, u32 *total_ms)
{
    if (len < 26 || memcmp(d, "DBRAWOPL", 8) || d[8] != 2 || d[9] != 0) return 0;
    u32 pairs = d[12] | d[13] << 8 | d[14] << 16 | (u32)d[15] << 24;
    *total_ms = d[16] | d[17] << 8 | d[18] << 16 | (u32)d[19] << 24;
    u8 sdc = d[23], ldc = d[24], ncode = d[25];
    const u8 *codes = d + 26;
    const u8 *pp = codes + ncode;
    if ((size_t)(pp - d) + pairs * 2 > len) return 0;
    OplW *w = malloc((pairs + 1) * sizeof *w);
    size_t n = 0;
    double t = 0;
    for (u32 i = 0; i < pairs; i++) {
        u8 c = pp[2 * i], v = pp[2 * i + 1];
        if (c == sdc) t += v + 1;
        else if (c == ldc) t += (v + 1) * 256.0;
        else {
            if (c >= ncode) { free(w); return 0; }
            w[n].ms = t; w[n].reg = codes[c]; w[n].val = v; n++;
        }
    }
    *out = w;
    *n_out = n;
    return 1;
}

typedef struct { char bank[8]; int idx, id; char name[16]; int prio; unsigned flags; bool loop; } Entry;

static int render_compare(const char *dir, const Entry *e, const u8 *drvimg, size_t drvlen,
                          const u8 *patch, size_t patchlen, bool verbose)
{
    char path[512], fname[64];
    if (strcmp(e->name, "-")) snprintf(fname, sizeof fname, "%04d_%s", e->id, e->name);
    else snprintf(fname, sizeof fname, "%04d", e->id);
    snprintf(path, sizeof path, "%s/%s/%s.snd", dir, e->bank, fname);
    size_t reslen;
    u8 *res = read_file(path, &reslen);
    if (!res) { printf("  %s: missing %s\n", e->bank, path); return 0; }
    snprintf(path, sizeof path, "%s/%s/%s.dro", dir, e->bank, fname);
    size_t drolen;
    u8 *dro = read_file(path, &drolen);
    if (!dro) { printf("  %s: missing %s\n", e->bank, path); free(res); return 0; }
    OplW *ref; size_t nref; u32 ref_ms;
    if (!parse_dro(dro, drolen, &ref, &nref, &ref_ms)) { printf("  %s: bad DRO\n", path); free(res); free(dro); return 0; }

    /* fresh memory: player variables, driver block, patch, node, resource */
    player_image_init();
    memset(mp(T_DRV_SEG, 0), 0, 0x3000);
    memcpy(mp(T_DRV_SEG, 0), drvimg, drvlen);
    memset(mp(T_PATCH_SEG, 0), 0, 0x1000);
    memcpy(mp(T_PATCH_SEG, 0), patch, patchlen);
    memset(mp(T_NODE_SEG, 0), 0, 0x100);
    memset(mp(T_RES_SEG, 0), 0, 0x8000);
    memcpy(mp(T_RES_SEG, 0), res, reslen);

    cap_n = 0;
    cap_clock = 0.0;
    void (*saved_out)(u8, u8) = snd_opl_write;
    snd_opl_write = capture;
    player_set_test_base(T_PLAYER_SEG, 0);
    FarPtr drv = far_make(T_DRV_SEG, 0);
    player_attach(drv);
    FarPtr pat = far_make(T_PATCH_SEG, 0);
    player_init(far_add(pat, (u16)(2 + patch[1])));          /* res_skip_header: 89 n desc */

    FarPtr node = far_make(T_NODE_SEG, 0);
    far_wr(node.seg, 0x166, far_make(T_RES_SEG, 0));
    far_wr(node.seg, 0x16A, far_make(T_RES_SEG, (u16)(2 + res[1])));
    far_wr(node.seg, 0x008, far_make(T_NODE_SEG, 0x16A));
    wr8(node.seg, 0x15E, 0x7F);
    wr8(node.seg, 0x15C, (u8)e->prio);
    wr8(node.seg, 0x15D, e->loop ? 1 : 0);
    bool sng = !strncmp(e->bank, "SIM", 3);
    snd_node_play(node, sng ? 0 : 1);

    const double tick_s = 4 * 5041 / 1193182.0, tick_ms = tick_s * 1000.0;
    int max_ticks = (int)(300.0 / tick_s), tail = (int)(1.5 / tick_s);
    int ticks = 0;
    u32 restarts0 = snd_dbg_restarts;
    const char *stop = "end";
    while (player_test_listed(node)) {
        ticks++;
        cap_clock = ticks * tick_ms;
        player_service();
        if (snd_dbg_restarts - restarts0 >= 2 && ticks * tick_s >= 8.0) { stop = "passes"; break; }
        if (ticks >= max_ticks) { stop = "cap"; break; }
    }
    if (player_test_listed(node)) snd_node_stop(node);
    for (int i = 0; i < tail; i++) {
        ticks++;
        cap_clock = ticks * tick_ms;
        player_service();
    }
    u32 my_ms = (u32)rint(ticks * tick_ms);
    snd_opl_write = saved_out;

    int ok = 1;
    size_t bad = (size_t)-1;
    size_t n = cap_n < nref ? cap_n : nref;
    for (size_t i = 0; i < n; i++) {
        if ((u32)rint(cap[i].ms) != (u32)ref[i].ms || cap[i].reg != ref[i].reg || cap[i].val != ref[i].val) { bad = i; break; }
    }
    if (bad != (size_t)-1 || cap_n != nref || my_ms != ref_ms) ok = 0;
    if (!ok || verbose) {
        printf("  %-5s %5d %-10s %s: %zu writes (ref %zu), %u ms (ref %u), stop %s",
               e->bank, e->id, e->name, ok ? "OK" : "MISMATCH", cap_n, nref, my_ms, ref_ms, stop);
        if (bad != (size_t)-1)
            printf("; first difference at write %zu: %u ms %02X=%02X, ref %u ms %02X=%02X", bad,
                   (u32)rint(cap[bad].ms), cap[bad].reg, cap[bad].val, (u32)ref[bad].ms, ref[bad].reg, ref[bad].val);
        printf("\n");
    }
    free(ref);
    free(res);
    free(dro);
    return ok;
}

int snd_test_dro(void)
{
    const char *dir = getenv("AOTP_DRO_DIR");
    const char *cands[] = { dir, "../work/sound", "work/sound", "../../work/sound" };
    char path[512];
    size_t len;
    u8 *index = NULL;
    for (int i = 0; i < 4 && !index; i++) {
        if (!cands[i]) continue;
        snprintf(path, sizeof path, "%s/index.txt", cands[i]);
        index = read_file(path, &len);
        if (index) dir = cands[i];
    }
    if (!index) {
        fprintf(stderr, "AOTP_TEST=dro: work/sound/index.txt not found (run python tools/aotpsnd.py all, or set AOTP_DRO_DIR)\n");
        return 1;
    }
    bool verbose = getenv("AOTP_TEST_VERBOSE") != NULL;
    index[len] = 0;
    static const struct { const char *bank, *ovl; } banks[] = {
        { "SHELL", "SXCODE" }, { "TTD", "SXCODE" }, { "SIM", "SX" }, { "SIM1", "SX1" },
    };
    int total = 0, good = 0;
    for (size_t b = 0; b < sizeof banks / sizeof banks[0]; b++) {
        size_t drvlen, patchlen;
        snprintf(path, sizeof path, "%s/%s/ADL.DRV", dir, banks[b].ovl);
        u8 *drvimg = read_file(path, &drvlen);
        snprintf(path, sizeof path, "%s/%s/PATCH.003", dir, banks[b].ovl);
        u8 *patch = read_file(path, &patchlen);
        if (!drvimg || !patch) { printf("%s: driver/patch of %s missing\n", banks[b].bank, banks[b].ovl); free(drvimg); free(patch); continue; }
        int bt = 0, bg = 0;
        for (char *line = (char *)index; *line; ) {
            char *nl = strchr(line, '\n');
            char buf[256];
            size_t ll = nl ? (size_t)(nl - line) : strlen(line);
            if (ll >= sizeof buf) ll = sizeof buf - 1;
            memcpy(buf, line, ll);
            buf[ll] = 0;
            line = nl ? nl + 1 : line + strlen(line);
            Entry e;
            memset(&e, 0, sizeof e);
            char loopw[8] = "";
            if (sscanf(buf, "%7s %d %d %15s %d %x %*d %7s", e.bank, &e.idx, &e.id, e.name, &e.prio, &e.flags, loopw) < 6) continue;
            if (strcmp(e.bank, banks[b].bank)) continue;
            e.loop = (e.flags & 2) != 0;
            bt++;
            bg += render_compare(dir, &e, drvimg, drvlen, patch, patchlen, verbose);
        }
        printf("%-5s (%s): %d/%d sounds identical\n", banks[b].bank, banks[b].ovl, bg, bt);
        total += bt;
        good += bg;
        free(drvimg);
        free(patch);
    }
    free(index);
    free(cap);
    printf("AOTP_TEST=dro: %d/%d sounds identical to the reference DROs\n", good, total);
    return good == total && total > 0 ? 0 : 1;
}
