/* Aces of the Pacific SDL3 port — entry point: GCD.COM's chain loop (platform.md §2.1, §9.1).
 *
 * usage: aotp [--game-dir DIR] [--scale N] [--fullscreen] [--aspect 4:3|1:1] [--frame-ticks N]
 *             [--start 1|2|3] [--intro-args S] [--ps-args S] [--ap-args S] [--check [AP_FLAT]]
 *   --game-dir     folder with the original game files (default "Game"); the port never ships game data
 *   --scale        initial window scale: 320x240 (4:3) times N (default 3)
 *   --fullscreen   start in full screen (Alt+Enter switches)
 *   --aspect       4:3 (default: the 200-line picture fills a 4:3 area, as on a VGA monitor) or 1:1
 *   --frame-ticks  sim frame pacing in 236.69 Hz ticks (default HOST_DEFAULT_FRAME_TICKS = 16; the
 *                  original minimum is 8; PLAN.md decision 4)
 *   --start        first program as a GCD exit code (default 1 = INTRO, as GCD.COM)
 *   --*-args       words put in front of the " -n<dir>\" tail of that program (e.g. --ap-args nlkle,
 *                  --intro-args -pi, --ps-args override)
 *   --classic      ENH: every enhancement off: the faithful port's picture and behaviour
 *   --res-scale    ENH: output resolution, 320x200 times N (default 4, 1..8)
 *   --aa           ENH: anti-aliasing of the 3D view, N x N samples per output pixel (default 2, 1..4)
 *   --widescreen   ENH: 16:9 picture; the 3D view reaches the sides when it fills the screen
 *   --view-dist    ENH: view ranges of the enhanced 3D view x N (default 3, 1..8; 1 = the original's)
 *   --no-lod-enh   ENH: keep the original's LOD choice (default: the finest model at every distance)
 *   --no-haze-enh  ENH: no distance haze, banded sky and hard horizon as the original
 *   --no-dots-enh  ENH: the ground dots as the original's pixels (sim frame rate, original range)
 *   --dots-range   ENH: ground dots out to N grid cells of 1024 units (default 12, 1..64; ~4 = original)
 *   --dots-alt     ENH: ground dots up to N units of altitude (default 8000, 0..30000; original 4000)
 *   --dots-size    ENH: ground dot size, % of an original pixel (default 100, 10..400)
 *   --dots-full    ENH: ground dots have their full size from N units of eye distance (default 4096, 0..16384)
 *   --dots-near    ENH: the nearest ground dots' size, % of the full size (default 25, 5..100)
 *   --dots-scatter ENH: ground dots moved off the grid by up to N % of half a cell (default 40, 0..100)
 *   --dots-far     ENH: ground dots denser with the distance, spacing halved N times at the range's edge
 *   --dots-fade    ENH: ground dots fade out from N % of the range to its edge (default 60, 0..100)
 *   --dots-speckle ENH: beyond the range, a faint speckle over N % of the ground (default 3, 0..40)
 *   --dots-far-bright ENH: far speckle and wave crests: N % of the dots' colour over the ground (default 60)
 *   --dots-edge    ENH: beyond the full-size distance dots shrink to N % at the range's edge (default 35)
 *   --dots-cover   ENH: crowded dots are skipped to cover at most N % of the ground (default 8, 1..50)
 *   --dots-debug   ENH: start with the dots' debug overlay on (Ctrl+Alt+G toggles it)
 *   --plane-size   ENH: aircraft models at N % of their size (default 100, 10..100; 42 = true scale)
 *   --hitbox-size  ENH: aircraft hit boxes against weapons at N % (default 100, 10..400)
 *                  (default 2, 0..3; 0 = even spacing as the original)
 *   --keys         ENH: key bindings, "name=code,..." for the actions not on their default keys (the
 *                  launcher's Game settings > Key bindings; enhanced/enh_keys.c). With the enhancements on,
 *                  F lowers the flaps a step and Shift+F raises them (the original's F cycles them)
 *   --check        load and verify the three executables (AP flattened), print a summary and exit;
 *                  with a path, also compare the flattened AP with that file (work/AP_flat.exe)
 * AOTP_TEST (developer aids): "dro" (sound, no program is loaded) runs here; the tests that need a
 * loaded program (platform: volumes, chunks, loaders; video: pictures, ttm) run from prog_run through
 * platform_dev_test() in the program --start selects.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ems.h"
#include "enhanced/enhanced.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/vga.h"
#include "prog.h"

/* GCD.COM stores the largest free DOS block after shrinking itself in 0000:0512 (paragraphs). Any value
 * >= 94BBh passes INTRO's (609 200 bytes) and PS's (590 000) memory checks (platform.md §2.2, §9.3). */
#define GCD_FREE_PARAS 0x9C00

int snd_test_dro(void);     /* sound/snd_test.c */

static int usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--aspect 4:3|1:1] [--frame-ticks N]\n"
            "          [--start 1|2|3] [--intro-args S] [--ps-args S] [--ap-args S] [--check [AP_FLAT]]\n"
            "          [--classic] [--res-scale N] [--aa N] [--widescreen] [--view-dist N]\n"
            "          [--no-lod-enh] [--no-haze-enh] [--no-dots-enh] [--dots-range N] [--dots-alt N]\n"
            "          [--dots-size N] [--dots-full N] [--dots-near N] [--dots-scatter N]\n"
            "          [--dots-far N] [--dots-fade N] [--dots-speckle N] [--dots-far-bright N]\n"
            "          [--dots-edge N] [--dots-cover N] [--dots-debug] [--plane-size N] [--hitbox-size N]\n"
            "          [--keys NAME=CODE,...]\n", prog);
    return 2;
}

int main(int argc, char **argv)
{
    const char *dir = "Game";
    const char *flat_ref = NULL;
    const char *prog_args[4] = { NULL, "", "", "" };
    int scale = 3, start = PROG_INTRO;
    bool check = false, fullscreen = false, aspect43 = true;
    bool classic = false, widescreen = false;                  /* ENH */
    int res_scale = ENH_DEFAULT_RES_SCALE, aa = ENH_DEFAULT_AA;
    int view_dist = ENH_DEFAULT_VIEW_DIST;
    bool lod_enh = true, haze_enh = true, dots_enh = true;
    int dots_range = ENH_DEFAULT_DOTS_RANGE, dots_alt = ENH_DEFAULT_DOTS_ALT, dots_size = ENH_DEFAULT_DOTS_SIZE;
    int dots_full = ENH_DEFAULT_DOTS_FULL, dots_near = ENH_DEFAULT_DOTS_NEAR, dots_scatter = ENH_DEFAULT_DOTS_SCATTER;
    int dots_far = ENH_DEFAULT_DOTS_FAR, dots_fade = ENH_DEFAULT_DOTS_FADE, dots_speckle = ENH_DEFAULT_DOTS_SPECKLE;
    int dots_far_bright = ENH_DEFAULT_DOTS_FAR_BRIGHT, dots_edge = ENH_DEFAULT_DOTS_EDGE;
    int dots_cover = ENH_DEFAULT_DOTS_COVER;
    int plane_size = ENH_DEFAULT_PLANE_SIZE, hitbox_size = ENH_DEFAULT_HITBOX_SIZE;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--aspect") && v) {
            if (!strcmp(v, "4:3")) aspect43 = true;
            else if (!strcmp(v, "1:1")) aspect43 = false;
            else return usage(argv[0]);
            i++;
        }
        else if (!strcmp(a, "--frame-ticks") && v) { host_set_frame_ticks(atoi(v)); i++; }
        else if (!strcmp(a, "--start") && v) { start = atoi(v); i++; if (start < 1 || start > 3) return usage(argv[0]); }
        else if (!strcmp(a, "--intro-args") && v) { prog_args[PROG_INTRO] = v; i++; }
        else if (!strcmp(a, "--ps-args") && v) { prog_args[PROG_PS] = v; i++; }
        else if (!strcmp(a, "--ap-args") && v) { prog_args[PROG_AP] = v; i++; }
        else if (!strcmp(a, "--classic")) classic = true;                                  /* ENH */
        else if (!strcmp(a, "--res-scale") && v) { res_scale = atoi(v); i++; }
        else if (!strcmp(a, "--aa") && v) { aa = atoi(v); i++; }
        else if (!strcmp(a, "--widescreen")) widescreen = true;
        else if (!strcmp(a, "--view-dist") && v) { view_dist = atoi(v); i++; }
        else if (!strcmp(a, "--no-lod-enh")) lod_enh = false;
        else if (!strcmp(a, "--no-haze-enh")) haze_enh = false;
        else if (!strcmp(a, "--no-dots-enh")) dots_enh = false;
        else if (!strcmp(a, "--dots-range") && v) { dots_range = atoi(v); i++; }
        else if (!strcmp(a, "--dots-alt") && v) { dots_alt = atoi(v); i++; }
        else if (!strcmp(a, "--dots-size") && v) { dots_size = atoi(v); i++; }
        else if (!strcmp(a, "--dots-full") && v) { dots_full = atoi(v); i++; }
        else if (!strcmp(a, "--dots-near") && v) { dots_near = atoi(v); i++; }
        else if (!strcmp(a, "--dots-scatter") && v) { dots_scatter = atoi(v); i++; }
        else if (!strcmp(a, "--dots-far") && v) { dots_far = atoi(v); i++; }
        else if (!strcmp(a, "--dots-fade") && v) { dots_fade = atoi(v); i++; }
        else if (!strcmp(a, "--dots-speckle") && v) { dots_speckle = atoi(v); i++; }
        else if (!strcmp(a, "--dots-far-bright") && v) { dots_far_bright = atoi(v); i++; }
        else if (!strcmp(a, "--dots-edge") && v) { dots_edge = atoi(v); i++; }
        else if (!strcmp(a, "--dots-cover") && v) { dots_cover = atoi(v); i++; }
        else if (!strcmp(a, "--dots-debug")) enh_dots_debug = true;
        else if (!strcmp(a, "--plane-size") && v) { plane_size = atoi(v); i++; }
        else if (!strcmp(a, "--hitbox-size") && v) { hitbox_size = atoi(v); i++; }
        else if (!strcmp(a, "--keys") && v) {
            if (!enh_keys_parse(v)) { fprintf(stderr, "bad --keys list: %s\n", v); return usage(argv[0]); }
            i++;
        }
        else if (!strcmp(a, "--check")) {
            check = true;
            if (v && v[0] != '-') { flat_ref = v; i++; }
        }
        else return usage(argv[0]);
    }
    if (!prog_args[PROG_INTRO]) prog_args[PROG_INTRO] = "";

    const char *test = getenv("AOTP_TEST");
    if (test && !strcmp(test, "dro")) return snd_test_dro();

    if (check) {
        /* no window: only the file lookup of the host is needed */
        if (!host_init_files(dir)) return 1;
        char err[256];
        bool ok = prog_check_all(flat_ref, err, sizeof err);
        if (!ok) fprintf(stderr, "%s\n", err);
        else printf("load segment %04X, frame pacing %d ticks\n", LOAD_SEG, host_frame_ticks());
        host_shutdown();
        return ok ? 0 : 1;
    }

    if (!host_init(dir, scale, fullscreen, aspect43)) return 1;
    mem_init_machine();       /* IVT, BIOS data area */
    ems_init();               /* EMM device header, INT 67h */
    enh_init(!classic, res_scale, aa, widescreen);   /* ENH: before the frame source is installed */
    enh_init_stage2(view_dist, lod_enh, haze_enh);   /* ENH: Stage 2 (all off with --classic) */
    enh_init_dots(dots_enh, dots_range, dots_alt, dots_size, dots_full, dots_near, dots_scatter,
                  dots_far, dots_fade, dots_speckle, dots_far_bright,
                  dots_edge, dots_cover);   /* ENH */
    enh_init_planes(plane_size, hitbox_size);        /* ENH */
    vga_init();               /* frame source, text mode */
    platform_init();          /* host tick / keyboard / mouse -> platform */

    /* GCD.COM: 0000:0512 = free paragraphs, 0000:0510 = 29Ah (once), then EXEC INTRO and loop on the
     * child's exit code: 1 INTRO, 2 PS, 3 AP, anything else quits (PORT: codes >= 4 read garbage in
     * GCD's table and fail with "Unable to chain"; the port quits). */
    wr16(0, 0x512, GCD_FREE_PARAS);
    wr16(0, 0x510, 0x29A);
    int code = start;
    while (code >= PROG_INTRO && code <= PROG_AP) code = prog_run(code, prog_args[code]);
    if (host_trace()) fprintf(stderr, "[gcd] quit (exit code %d)\n", code);
    host_shutdown();
    return 0;
}
