/* AP start-up (platform.md §2.6, §2.8): main 50c8:0008 up to the hand-over to sim_flow (sim_init,
 * vcr_session, sim_shutdown), is_debug 50c8:0000, the empty heap hook 4b9e:0000, and the preference
 * files of 4e0d (realism_name 05e0, prefs_load 05f5, prefs_save 06fd, sim_exit 0bb0). */
#include "platform/rtl_priv.h"

#include "game/game.h"
#include "host.h"
#include "prog.h"
#include "sim_flow/sim_flow.h"
#include "sound/sound.h"
#include "symbols_ap.h"
#include "video/video.h"

#define AP_work_dir     0x4032
#define AP_debug        0x053F
#define AP_sim_mode     0x43DC
#define AP_ems_tier     0x0539
#define AP_ems_spare    0x4030
#define AP_sim_in       0x439A
#define AP_extract_name 0x0534
#define AP_realism      0x42C8   /* 16 bytes */
#define AP_ctl          0x43B1   /* control_prf, 37 bytes */
#define AP_real_name    0x032E   /* "?real.prf" */
#define AP_opts_seg     0x3974   /* far copies of the CONTROL.PRF option bytes (sim_flow) */
#define AP_assert_seg   0x387B   /* "main.c" */
#define AP_assert_seg2  0x386B   /* "init.c" */

void lib_ems_free_all(void);   /* ems_lib.c */

/* AP 4b9e:0000 heap_hook: empty */
void heap_hook(void) {}

/* AP 50c8:0000 is_debug */
s16 is_debug(void) { return DSS(AP_debug); }

/* AP 4e0d:05e0 realism_name: "creal.prf" in career mode, else "mreal.prf" */
static u16 realism_name(void)
{
    DSB(AP_real_name) = 'm';
    if (DSW(AP_sim_mode) == 1) DSB(AP_real_name) = 'c';
    return AP_real_name;
}

static void opt_w(u16 off, u8 v) { wr16(SEG(AP_opts_seg), off, (u16)(s16)(s8)v); }
static u8 opt_b(u16 off) { return rd8(SEG(AP_opts_seg), off); }

/* AP 4e0d:05f5 prefs_load: realism file, CONTROL.PRF (defaults DS:06C4), then hand the options and
 * the joystick calibration to sim_flow */
void prefs_load(void)
{
    u16 f = my_fopen(realism_name(), 0x05EB /* "rb" */);
    if (f) { my_fread(AP_realism, 1, 0x10, f); my_fclose(f); }
    f = my_fopen(0x0338 /* "control.prf" */, 0x05EB);
    if (f) {
        my_fread(AP_ctl, 1, 0x25, f);
        my_fclose(f);
    } else {
        rtl_memcpy(AP_ctl, 0x06C4, 0x25);
    }
    opt_w(0x185, DSB(AP_ctl + 8));
    opt_w(0x12D, DSB(AP_ctl + 6));
    opt_w(0x159, DSB(AP_ctl + 7));
    opt_w(0x07D, DSB(AP_ctl + 0x0A));
    opt_w(0x0A9, DSB(AP_ctl + 0x0B));
    opt_w(0x0D5, DSB(AP_ctl + 0x0C));
    rtl_memcpy(calib_ptr(0), AP_ctl + 0x0D, 12);
    rtl_memcpy(calib_ptr(1), AP_ctl + 0x19, 12);
    joy_flight_set((s16)(s8)DSB(AP_ctl + 6));
    mouse_flag_set((s16)(s8)DSB(AP_ctl + 7));
    controls_apply();
}

/* AP 4e0d:06fd prefs_save: the realism file (not in the recorder modes 2/3), then CONTROL.PRF */
static void prefs_save(void)
{
    if (DSW(AP_sim_mode) != 2 && DSW(AP_sim_mode) != 3) {
        u16 f = my_fopen(realism_name(), 0x039F /* "wb" */);
        if (!f) assert_fail(far_make(SEG(AP_assert_seg2), 0), 0x26F, 0);
        if (f) { my_fwrite(AP_realism, 1, 0x10, f); my_fclose(f); }
    }
    DSB(AP_ctl + 8) = opt_b(0x185);
    DSB(AP_ctl + 6) = opt_b(0x12D);
    DSB(AP_ctl + 7) = opt_b(0x159);
    DSB(AP_ctl + 0x0A) = opt_b(0x07D);
    DSB(AP_ctl + 0x0B) = opt_b(0x0A9);
    DSB(AP_ctl + 0x0C) = opt_b(0x0D5);
    rtl_memcpy(AP_ctl + 0x0D, calib_ptr(0), 12);
    rtl_memcpy(AP_ctl + 0x19, calib_ptr(1), 12);
    u16 f = my_fopen(0x0338 /* "control.prf" */, 0x03A2 /* "wb" */);
    if (!f) assert_fail(far_make(SEG(AP_assert_seg2), 0), 0x280, 0);
    if (f) { my_fwrite(AP_ctl, 1, 0x25, f); my_fclose(f); }
}

/* AP 4e0d:0bb0 sim_exit(code, text_mode) */
void sim_exit(s16 code, s16 text_mode)
{
    sx_shutdown();                                    /* (5a75:0d39 with an ignored 0) */
    if (DSW(0x4010)) { prefs_save(); simprefs_save(); }
    vol_shutdown();
    read_front();
    write_back();
    copy_screen();
    if (DSW(0x4012) != DSW(0x26D4)) gfx_flip(1);
    int24_restore();
    sys_shutdown();
    if (is_debug() || text_mode) gfx_restore_text();
    wr16(0, 0x510, 0x3E7);
    prog_exit(code);
}

/* AP 50c8:0008 main */
int ap_main(int argc, u16 argv_ds)
{
    u16 m = stk_mark();
    s16 mode = 0, skip_mem = 0;
    DSB(AP_work_dir) = 0;
    DSB(0x5EE1) = 0;
    DSB(0x5EE0) = 0;
    /* -n<dir>: the work directory; a trailing '\' is added, a doubled one removed */
    for (int i = 1; i < argc; i++) {
        u16 a = DSW((u16)(argv_ds + 2 * i));
        if (DSB(a) == '-' && DSB((u16)(a + 1)) == 'n') rtl_strcpy(AP_work_dir, (u16)(a + 2));
    }
    if (DSB((u16)(AP_work_dir + rtl_strlen(AP_work_dir) - 1)) != '\\') rtl_strcat(AP_work_dir, 0x0576 /* "\\" */);
    if (DSB((u16)(AP_work_dir + rtl_strlen(AP_work_dir) - 1)) == '\\' &&
        DSB((u16)(AP_work_dir + rtl_strlen(AP_work_dir) - 2)) == '\\')
        DSB((u16)(AP_work_dir + rtl_strlen(AP_work_dir) - 1)) = 0;
    if (rtl_strcmp(DSW((u16)(argv_ds + 2)), 0x0578 /* "nlkle" */) == 0) DSW(AP_debug) = 1;
    for (int i = 1; i < argc; i++) {
        u16 a = DSW((u16)(argv_ds + 2 * i));
        if ((DSB(a) == '-' || DSB(a) == '/') && DSB((u16)(a + 1)) == 'm' && is_debug()) {
            FarPtr v = mem_far_new(0xFFFFFFFFu, 0);
            rtl_printf(0x057E /* "%ld\n" */, (unsigned)v.off, (unsigned)v.seg);
            prog_exit(1);
        }
    }
    DSW(0x1EBC) = 0x0583;                             /* "SH" (volume exclusion) */
    vol_init(0, 0, 0);
    if (lib_ems_detect()) {
        prog_atexit(lib_ems_free_all);
        DSW(AP_ems_spare) = lib_ems_free_pages();
        if (DSS(AP_ems_spare) < 0x13) assert_fail(far_make(SEG(AP_assert_seg), 0), 0xBD, 0x0541);
        if (DSS(AP_ems_spare) < 0x21) { DSB(AP_ems_tier) = 3; DSW(AP_ems_spare) -= 0x15; }
        else                          { DSB(AP_ems_tier) = 4; DSW(AP_ems_spare) -= 0x21; }
    } else {
        assert_fail(far_make(SEG(AP_assert_seg), 0), 0xDB, 0x0541);
    }
    int24_install();
    if (!is_debug()) {
        u16 mk = rd16(0, 0x510);
        if (mk != 0x29A && mk != 0x3E7) prog_exit(1);   /* not started by PS */
        file_load(AP_sim_in, 0x0D, 1, 0x0586 /* "sim.in" */);
        mode = (s16)(s8)DSB(AP_sim_in + 1);
    }
    wr16(0, 0x510, 0);
    for (int i = 1; i < argc; i++) {
        u16 a = DSW((u16)(argv_ds + 2 * i));
        if (DSB(a) != '-' && DSB(a) != '/') continue;
        u16 si = (u16)(a + 1);
        switch ((s16)(s8)DSB(si)) {
        case 'S': case 'x':
            si++;
            if (!DSB(si)) break;
            if (is_debug()) {
                /* debug: remember the name, check that it exists */
                DSW(AP_extract_name) = si;
                u16 buf = stk_alloc(0xD2);
                rtl_sprintf(buf, 0x058D /* "Could not find %s" */, (unsigned)si);
                if (!ap_file_exists(DSW(AP_extract_name)))
                    assert_fail(far_make(SEG(AP_assert_seg), 0), 0x106, buf);
            } else if (ap_file_exists(si)) {
                /* copy the (volume) file to extract_name with the RTL stdio */
                u16 in = rtl_fopen(si, 0x05EB /* "rb" */);
                u16 out = rtl_fopen(DSW(AP_extract_name), 0x059F /* "wb" */);
                if (!in || !out) assert_fail(far_make(SEG(AP_assert_seg), 0), 0x114, 0);
                s16 c;
                while ((c = rtl_fgetc(in)) != -1) rtl_fputc(c, out);
                rtl_fclose(out);
                rtl_fclose(in);
            }
            break;
        case 't':
            if (is_debug()) {
                DSW(0x053C) = (u16)rtl_atol((u16)(si + 1));
                DSW(0x053A) = 1;
                DSW(0x0884) = 1;
            }
            break;
        case 'o': skip_mem = 1; break;
        case 'W': if (is_debug()) DSW(0x0536) = (u16)rtl_atol((u16)(si + 1)); break;
        case 's':
            if (is_debug()) {
                if (DSB((u16)(si + 1))) { DSW(0x0532) = 0; DSW(0x052E) = (u16)rtl_atol((u16)(si + 1)); }
                else DSW(0x0532) = 1;
            }
            break;
        case 'w': if (is_debug()) mode = 2; break;
        case 'd': if (is_debug()) mode = 3; break;
        default: break;
        }
    }
    if (!skip_mem) {
        FarPtr v = mem_far_new(0xFFFFFFFFu, 0);
        s32 left = (s32)((u32)v.seg << 16 | v.off) - 140000;
        if (left < 0) assert_fail(far_make(SEG(AP_assert_seg), 0), 0x1B8, 0x05A2 /* "MEMORY TOO LOW" */);
    }
    /* PORT: _OvrInitEms(0, 0, ems_spare_pages) (36f6:08ed) is dropped: there is no overlay manager */
    heap_hook();
    DSW(AP_sim_mode) = (u16)mode;
    sim_init();
    vcr_session(mode);
    sim_shutdown();
    wr16(0, 0x510, 0x3E7);
    stk_release(m);
    sim_exit(2, 0);
    return 2;
}
