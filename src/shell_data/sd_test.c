/* shell_data developer aid (AOTP_TEST=missions, run in PS: --start 2). Not game code.
 *
 *   1. For seeds 1..AOTP_SEEDS (default 8), every mission type (0-4, 6-9) and every available service
 *      (type 0: an ace), builds a single mission with the ported generator (mission_build, kind 0) and
 *      writes SCRIPT.DAT (script_write); the file is copied to <out>/gen_s<seed>_t<type>_v<svc>.dat and
 *      the rng_below / rng_below_long draws of the build to <out>/rng_s<seed>_t<type>_v<svc>.txt.
 *   2. Every MISS<n>.DAT / TRN<n>.DAT found through the volume layer is read by the scenario reader
 *      (1f06:015c, parse = 1: prescan + loader callbacks) and written back by 1f06:0269; the SCRIPT.DAT
 *      produced is copied to <out>/rt_<name> for a byte comparison with the extracted volume file.
 *
 * <out> = AOTP_MISSIONS_OUT or "work/sd/missions" (relative to the current directory). The game's own
 * SCRIPT.DAT is saved first and restored at the end. Check the files with work/sd/check_missions.py.
 * Entry: shell_data_dev_test() — -1 when AOTP_TEST is not "missions". */
#include "shell_data/sd_priv.h"
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

int shell_data_dev_test(void);

void (*sd_rng_trace)(s16 n, s32 r);          /* dev aid hook, lowlevel.c (host side, not game state) */

static FILE *trace_f;
static void trace(s16 n, s32 r) { if (trace_f) fprintf(trace_f, "below(%d) = %ld\n", n, (long)r); }

static unsigned char *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)*len + 1);
    if (fread(b, 1, (size_t)*len, f) != (size_t)*len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static bool copy_script(const char *dst)
{
    char *p = host_game_path("SCRIPT.DAT", false);
    if (!p) return false;
    long n;
    unsigned char *b = slurp(p, &n);
    host_free(p);
    if (!b) return false;
    FILE *o = fopen(dst, "wb");
    if (!o) { free(b); return false; }
    fwrite(b, 1, (size_t)n, o);
    fclose(o);
    free(b);
    return true;
}

/* 28e4:019f loads MISSINF into a new extens slot at every open and 28e4:0820 only frees the list, so
 * each built mission keeps one of the 20 slots (faithful: a PS run builds few missions). The harness
 * releases those slots, as a fresh PS start would. */
static void release_missinf_slot(void)
{
    FarPtr l = ds_ptr(PS_DS_missinf_list);
    for (s16 s = 1; s < 0x15; s++) {
        u16 sl = (u16)(0x10A8 + s * 0x7D);
        if (far_eq(ds_far(sl), l)) ds_far_wr(sl, far_null());
    }
}

static bool vol_exists(u16 name_ds)
{
    u16 f = my_fopen(name_ds, sd_lit("rb"));
    if (!f) return false;
    my_fclose(f);
    return true;
}

static int run_missions(const char *out)
{
    int nseeds = getenv("AOTP_SEEDS") ? atoi(getenv("AOTP_SEEDS")) : 8;
    static const s8 types[] = { 0, 1, 2, 3, 4, 6, 7, 8, 9 };
    char path[1024];
    int built = 0;
    sd_rng_trace = trace;
    for (int seed = 1; seed <= nseeds; seed++) {
        for (size_t ti = 0; ti < sizeof types; ti++) {
            s8 t = types[ti];
            for (s16 v = 0; v < 11; v++) {
                s16 svc = (s16)DSW((u16)(0x49CE + v * 2));
                s16 arg = svc;
                if (t == 0) {                            /* dogfight an ace: the argument is the ace */
                    if (v > 0) break;
                    arg = (s16)((seed * 37) % ace_count());
                    ps_rng_seed((u16)seed);             /* type 0 does not reseed: start from the seed */
                } else if (svc < 0) continue;
                DSW(PS_DS_rand_seed_opt) = (u16)seed;
                snprintf(path, sizeof path, "%s/rng_s%d_t%d_v%d.txt", out, seed, t, arg);
                trace_f = fopen(path, "w");
                mission_build(t, (s8)arg, 0);
                script_write(0);
                if (trace_f) fclose(trace_f);
                trace_f = NULL;
                snprintf(path, sizeof path, "%s/gen_s%d_t%d_v%d.dat", out, seed, t, arg);
                if (!copy_script(path)) { fprintf(stderr, "missions: no SCRIPT.DAT for %s\n", path); return 1; }
                ps_mission_free_3164();
                release_missinf_slot();
                built++;
            }
        }
    }
    sd_rng_trace = NULL;
    printf("AOTP_TEST=missions: %d missions generated -> %s\n", built, out);

    int rt = 0;
    static const char *const bases[] = { "MISS", "TRN" };
    for (int b = 0; b < 2; b++) {
        for (int n = 0; n < 100; n++) {
            u16 m = stk_mark();
            char nm[16];
            snprintf(nm, sizeof nm, "%s%d.DAT", bases[b], n);
            u16 name = sd_lit(nm);
            if (vol_exists(name)) {
                ps_mission_load(name, 1);
                mission_write_script();
                ps_mission_free_1f06();
                if (!far_is_null(ds_far(PS_DS_msg_ofs))) xfarfree(ds_far(PS_DS_msg_ofs));
                if (!far_is_null(ds_far(PS_DS_msg_text))) xfarfree(ds_far(PS_DS_msg_text));
                ds_far_wr(PS_DS_msg_ofs, far_null());
                ds_far_wr(PS_DS_msg_text, far_null());
                load_free();
                snprintf(path, sizeof path, "%s/rt_%s", out, nm);
                if (!copy_script(path)) { fprintf(stderr, "missions: round trip of %s wrote nothing\n", nm); return 1; }
                rt++;
            }
            stk_release(m);
        }
    }
    printf("AOTP_TEST=missions: %d mission files read and written back -> %s/rt_*\n", rt, out);
    return 0;
}

int shell_data_dev_test(void)
{
    const char *t = getenv("AOTP_TEST");
    if (!t || strcmp(t, "missions")) return -1;
    const char *out = getenv("AOTP_MISSIONS_OUT") ? getenv("AOTP_MISSIONS_OUT") : "work/sd/missions";
#ifdef _WIN32
    _mkdir(out);
#else
    mkdir(out, 0777);
#endif
    char *sp = host_game_path("SCRIPT.DAT", false);
    long saved_n = 0;
    unsigned char *saved = sp ? slurp(sp, &saved_n) : NULL;
    host_free(sp);

    vol_init(0, 0x4000, 0x0FFF);                 /* PS volumes, as platform_dev_test */
    shell_data_load();                           /* roster, control, strings, TABLES, services, tops */
    station_open();
    ord_open();
    squad_open();
    ace_open();
    air_open();
    int r = run_missions(out);

    char *dp = host_game_path("SCRIPT.DAT", true);
    if (dp) {
        if (saved) { FILE *f = fopen(dp, "wb"); if (f) { fwrite(saved, 1, (size_t)saved_n, f); fclose(f); } }
        else remove(dp);
        host_free(dp);
    }
    free(saved);
    fflush(stdout);
    exit(r);
}
