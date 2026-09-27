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
            "          [--no-lod-enh] [--no-haze-enh]\n", prog);
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
    bool lod_enh = true, haze_enh = true;
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
