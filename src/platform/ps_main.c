/* PS start-up and exit (platform.md §2.5): main 048e:0259, ps_sys_init 048e:0009, fly_mission
 * 048e:01a0, the exit/quit helpers 227c:0007..0072. */
#include "platform/rtl_priv.h"

#include "game/game.h"
#include "prog.h"
#include "shell_data/shell_data.h"
#include "shell_ui/shell_ui.h"
#include "sound/sound.h"
#include "symbols_ps.h"
#include "video/video.h"

#define PS_nlkle             0x00C2   /* u8: argv "nlkle" (debug) */
#define PS_returned_from_sim 0x00C3   /* u8 */
#define PS_music_started     0x00C4   /* u8 (set by ps_sys_init) */
#define PS_rand_seed_opt     0x00C5   /* u16: -r<n> */
#define PS_mem_override      0x00AA   /* u8 */
#define PS_mem_msg           0x00C7   /* char *[]: "Your computer does not have enough memory..." */
#define PS_control_prf       0x43B0
#define PS_video_drv         (PS_control_prf + 0)
#define PS_sound_drv         (PS_control_prf + 2)
#define PS_work_dir          0x40CA
#define PS_career_dirty      0x051E

/* PS 227c:0007 set_exit_hook(fn) */
void set_exit_hook(FarPtr a0) { ds_far_wr(PS_DS_exit_hook, a0); }

/* PS 227c:0019 clear_exit_hook */
void clear_exit_hook(void) { ds_far_wr(PS_DS_exit_hook, far_make(0, 0)); }

/* PS 227c:002a set_quit_flag: DS:00C1 = 1 */
void set_quit_flag(void) { DSB(PS_DS_quit_flag) = 1; }

/* PS 227c:0034 ctrl_esc_hook(x): the keyboard ISR's Ctrl+Esc hook (called with 0) */
void ctrl_esc_hook(s16 x)
{
    if (x) shell_exit(0);
    DSB(PS_DS_quit_flag) = 1;
}

/* PS 227c:004d ps_shutdown */
void ps_shutdown(void)
{
    if (DSB(PS_career_dirty)) {
        DSB(0x4778) = 1;
        best_career_insert();
        pilot_store();
    }
    vol_shutdown();
    sys_shutdown();
}

/* PS 227c:0072 shell_exit(to_sim) */
void shell_exit(s16 to_sim)
{
    FarPtr hook = ds_far(PS_DS_exit_hook);
    if (!far_is_null(hook)) codeptr_lookup(hook)();
    ps_shutdown();
    shell_data_save();
    s16 code;
    if (to_sim) {
        snd_shutdown();
        if (DSB(PS_nlkle)) {
            gfx_restore_text();
        } else if (LW(G_drv_page_front) != 0xA000) {      /* make the shown page visible (video) */
            LW(G_drv_page_back) = 0xA000;                 /* DS:2312 */
            LW(G_drv_page_read) = LW(G_drv_page_front);   /* DS:2316 = DS:2314 */
            LW(G_drv_page_write) = LW(G_drv_page_back);   /* DS:2318 = DS:2312 */
            gfx_flip(0);
        }
        wr16(0, 0x510, DSB(PS_returned_from_sim) ? 0x3E7 : 0x29A);
        code = 3;
    } else {
        snd_shutdown();
        gfx_restore_text();
        code = 0;
    }
    prog_exit(code);
}

/* PS 048e:0009 ps_sys_init */
void ps_sys_init(void)
{
    s16 v = gfx_init((s16)DSW(PS_video_drv), 0xFF, 0x00AB /* "vmcode.ovl" */);
    DSW(PS_video_drv) = (u16)v;
    if (!v) {
        v = (s16)(s8)gfx_init(0, 0xFF, 0x00AB);          /* (CBW of the result) */
        DSW(PS_video_drv) = (u16)v;
        if (!v) fatal_error(0x00D3 /* "Cannot initialize graphics card!..." */, 0);
    }
    if (DSW(PS_sound_drv) == 4) DSW(PS_sound_drv) = 2;
    s16 drv = (DSW(PS_sound_drv) == 3 && !DSB(PS_nlkle)) ? 0x88 : (s16)DSW(PS_sound_drv);
    if (!snd_init(drv, 0x00B6 /* "sxcode.ovl" */)) fatal_error(0x0112 /* "Cannot initialize sound..." */, 0);
    timer_add(far_make(SEG(0x229E), 0x0013), 1);          /* the 236.69 Hz step clock (shell_ui) */
    divzero_install();
    kbd_install(0);
    mouse_init_dos3();
    screens_load();
    ps_fonts_load();
    parts_load();
    scr_init();
    pref_apply_all();
    snd_load_resource(make_name(0x0158 /* "shell" */, -1, 0x015E /* "sx" */), 0);
    if (!DSB(PS_returned_from_sim)) snd_play(0x3EA);
    DSB(PS_music_started) = 1;
    plate_buf_alloc();
    dialogs_init();
    DSB(PS_career_dirty) = 0;
}

/* PS 048e:01a0 fly_mission(kind) */
void fly_mission(s16 a0)
{
    screen_show(0x25, 0, 0);
    please_wait();
    script_write((s8)a0);
    sim_in_build_write();
    realism_save_current();
    if (DSB(0x626B) != 2 && DSB(0x626B) != 3) {           /* mission header kind */
        air_close();
        ace_close();
        station_close();
        ord_close();
    }
    plate_buf_free();
    shell_exit(1);
}

/* The -n<dir> pass and the trailing '\' normalisation of the three mains (the no -n quirk: the byte
 * before the empty work_dir is tested, so it becomes "\"). */
void main_work_dir(int argc, u16 argv_ds, u16 work)
{
    DSB(work) = 0;
    for (int i = 1; i < argc; i++) {
        u16 a = DSW((u16)(argv_ds + 2 * i));
        if (DSB(a) == '-' && DSB((u16)(a + 1)) == 'n') rtl_strcpy(work, (u16)(a + 2));
    }
    u16 m = stk_mark();
    u16 bs = stk_alloc(2);
    DSW(bs) = '\\';
    if (DSB((u16)(work + rtl_strlen(work) - 1)) != '\\') rtl_strcat(work, bs);
    if (DSB((u16)(work + rtl_strlen(work) - 1)) == '\\' && DSB((u16)(work + rtl_strlen(work) - 2)) == '\\')
        DSB((u16)(work + rtl_strlen(work) - 1)) = 0;
    stk_release(m);
}

/* PS 048e:0259 main */
/* PORT: weak until game/placeholder.c (a strong ps_main) is removed by the lead. */
__attribute__((weak)) int ps_main(int argc, u16 argv_ds)
{
    main_work_dir(argc, argv_ds, PS_work_dir);
    DSB(0x4F6D) = 0;                                       /* Windows switch request flags */
    DSB(0x4F6C) = 0;
    vol_init(0, 0x4000, 0x0FFF);
    shell_data_load();
    for (int i = 1; i < argc; i++) {
        u16 a = DSW((u16)(argv_ds + 2 * i));
        if (DSB(a) == '-') {
            s16 c = rtl_tolower((s16)(s8)DSB((u16)(a + 1)));
            switch (c) {
            case 's': DSW(PS_sound_drv) = (u16)rtl_atol((u16)(a + 2)); break;
            case 'r': DSW(PS_rand_seed_opt) = (u16)rtl_atol((u16)(a + 2)); break;
            case 'p': rtl_tolower((s16)(s8)DSB((u16)(a + 2))); break;   /* the -p options: compiled out */
            case '?': case 'h': {
                static const u16 help[15] = { 0x027E, 0x0289, 0x02A9, 0x02C9, 0x02EC, 0x030F, 0x032F,
                                              0x035C, 0x037A, 0x03A2, 0x03C4, 0x03EA, 0x0405, 0x0420, 0x0439 };
                for (int k = 0; k < 15; k++) rtl_printf(help[k]);
                shell_exit(0);
                break;
            }
            default: break;
            }
        } else if (rtl_stricmp(a, 0x0455 /* "nlkle" */) == 0) {
            DSB(PS_nlkle) = 1;
        } else if (rtl_stricmp(a, 0x045B /* "override" */) == 0) {
            DSB(PS_mem_override) = 1;
        }
    }
    mem_check();
    if (!DSB(PS_nlkle)) {
        u16 mk = rd16(0, 0x510);
        if (mk == 0x29A) DSB(PS_returned_from_sim) = 0;
        else if (mk == 0x3E7) DSB(PS_returned_from_sim) = 1;
        else fatal_error(0x0464 /* "You can run Aces of the Pacific by typing 'AOTPCD'..." */, 0);
    }
    wr16(0, 0x510, 0);
    ps_sys_init();
    if (DSB(PS_returned_from_sim)) sim_end();
    main_menu();
    shell_exit(0);
    return 0;
}
