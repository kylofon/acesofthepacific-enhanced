/* INTRO start-up (platform.md §2.4): 038e:0004..0ac1, and the CONTROL.PRF load/save and memory
 * check that PS shares (1193:027a/0306, 048e:0200). */
#include "platform/rtl_priv.h"

#include "game/game.h"
#include "host.h"
#include "prog.h"
#include "shell_ui/shell_ui.h"
#include "sound/sound.h"
#include "symbols_intro.h"
#include "video/video.h"

#define IN_prf_loaded   0x2460
#define IN_work_dir     0x2461
#define IN_skip_intro   0x00C0
#define IN_mem_override 0x00C1
#define IN_intro_module 0x00C2
#define IN_vol_active   0x00C5
#define IN_ctl          0x24B1   /* control_prf (37 bytes) */
#define PS_ctl          0x43B0

void main_work_dir(int argc, u16 argv_ds, u16 work);  /* ps_main.c */

/* INTRO 038e:0004 ipow(a, b) (16-bit, 1 for b <= 0) */
static s16 ipow(s16 a, s16 b)
{
    s16 r = 1;
    for (s16 i = 0; i < b; i++) r = (s16)(r * a);
    return r;
}

/* INTRO 038e:0021 module_pick(mask): a random module i in 0..7 with mask & 2^(i-1) */
u8 module_pick(u8 mask)
{
    u16 m = stk_mark();
    u16 list = stk_alloc(16);
    s16 n = 0;
    for (s16 i = 0; i < 8; i++)
        if (mask & (u8)ipow(2, (s16)(i - 1))) DSW((u16)(list + 2 * n++)) = (u16)i;
    rtl_srand((u16)rtl_time(0));
    s32 r = (s32)rtl_rand() * n / 0x8000;
    u8 v = DSB((u16)(list + 2 * (u16)r));
    stk_release(m);
    return v;
}

/* INTRO 038e:0097 chdir_unless(path): 1 while volumes are active, else chdir(path) == 0 */
s16 chdir_unless(u16 p0_ds)
{
    if (DSW(IN_vol_active)) return 1;
    return rtl_chdir(p0_ds) == 0 ? 1 : 0;
}

/* INTRO 038e:00bc chdir_up */
static s16 chdir_up(void) { return chdir_unless(0x02E4 /* ".." */); }

/* INTRO 038e:00cc make_path(s, ext): s + "." + ext (and the unused "<ext>\" developer copy) */
static u16 make_path(u16 s, u16 ext)
{
    u16 r = rtl_strcat(rtl_strcat(s, 0x02E7 /* "." */), ext);
    if (!DSW(IN_vol_active))
        rtl_strcat(rtl_strcat(rtl_strcpy(0x242C, ext), 0x02E9 /* "\\" */), r);
    return r;
}

/* INTRO 038e:0121 make_name_n(base, n, ext) -> DS:244A */
u16 make_name_n(u16 p0_ds, s16 a1, u16 p2_ds)
{
    u16 m = stk_mark();
    rtl_strcpy(0x244A, p0_ds);
    if (a1 > -1) {
        u16 t = stk_alloc(6);
        rtl_itoa(a1, t, 10);
        rtl_strcat(0x244A, t);
    }
    rtl_strcpy(0x244A, make_path(0x244A, p2_ds));
    stk_release(m);
    return 0x244A;
}

static void joy0_defaults(u16 ctl)
{
    DSS(ctl + 0x0D) = -127; DSS(ctl + 0x0F) = 0; DSS(ctl + 0x11) = 127;
    DSS(ctl + 0x13) = -127; DSS(ctl + 0x15) = 0; DSS(ctl + 0x17) = 127;
}

/* INTRO 038e:0181 / PS 1193:027a control_load */
s16 control_load(void)
{
    if (prog_current_id() == PROG_PS) {
        if (file_read(0x0C01 /* "control.prf" */, PS_ctl, 0x25) != 0x25)
            assert_fail(far_make(SEG(0x4BE1), 0), 0x109, 0x0C0D /* "size of control" */);
        if (DSW(PS_ctl + 0x0D) == DSW(PS_ctl + 0x11) || DSW(PS_ctl + 0x13) == DSW(PS_ctl + 0x17))
            joy0_defaults(PS_ctl);
        return 0;
    }
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50);
    rtl_strcpy(path, IN_work_dir);
    rtl_strcat(path, 0x02EB /* "control.prf" */);
    rtl_memset(IN_ctl, 0, 0x25);
    u16 f = rtl_fopen(path, 0x02F7 /* "rb" */);
    if (!f) f = rtl_fopen(0x02FA /* "control.prf" */, 0x0306 /* "rb" */);
    stk_release(m);
    if (!f) return 0;
    u16 n = rtl_fread(IN_ctl, 1, 0x25, f);
    if (n != 0x25) {
        if (n == 0) { DSW(IN_ctl) = 0; DSW(IN_ctl + 2) = 0; }
        for (int i = 0; i < 7; i++) DSB(IN_ctl + 6 + i) = 1;
        joy0_defaults(IN_ctl);
    }
    return 1;                                         /* (the file is not closed: original) */
}

/* INTRO 038e:0687 / PS 1193:0306 control_save */
void control_save(void)
{
    if (prog_current_id() == PROG_PS) { file_write(0x0C1D /* "control.prf" */, PS_ctl, 0x25); return; }
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50);
    rtl_strcpy(path, IN_work_dir);
    rtl_strcat(path, 0x0390 /* "control.prf" */);
    u16 f = rtl_fopen(path, 0x039C /* "wb" */);
    stk_release(m);
    if (!f) assert_fail(far_make(SEG(0x17D5), 0), 0x266, 0);
    rtl_fwrite(IN_ctl, 0x25, 1, f);
    rtl_fclose(f);
}

/* INTRO 038e:0247 cfg_lookup, 0284 config_read, 04d6 / PS 1193:0ad2 config_write: the RESOURCE.CFG
 * reader/writer, unreferenced (platform.md §2.8). PORT: not ported. */
s16 cfg_lookup(u16 tab_ds, u16 line_ds) { (void)tab_ds; (void)line_ds; host_fatal("cfg_lookup: dead code"); }
void config_read(void) { host_fatal("config_read: dead code (RESOURCE.CFG is never read)"); }
void config_write(void) { host_fatal("config_write: dead code"); }

/* INTRO 038e:0704 intro_modules_detect: CSET<n>.DAT -> modules; INTRO<n>.DAT = 1 -> play once */
void intro_modules_detect(void)
{
    DSB(IN_ctl + 4) = 0;
    for (s16 n = 1; n < 8; n++) {
        u16 f = my_fopen(make_name_n(0x039F /* "CSET" */, n, 0x03A4 /* "dat" */), 0x03A8 /* "rb" */);
        if (!f) continue;
        DSB(IN_ctl + 4) |= (u8)ipow(2, (s16)(n - 1));
        DSB(IN_ctl + 5) = (u8)n;
        my_fclose(f);
        u16 m = stk_mark();
        u16 b = stk_alloc(2);
        f = my_fopen(make_name_n(0x03AB /* "INTRO" */, n, 0x03B1 /* "DAT" */), 0x03B5 /* "rb" */);
        if (f) {
            my_fread(b, 1, 1, f);
            if (DSB(b)) DSW(IN_intro_module) = (u16)n;
            my_fclose(f);
        }
        if (DSW(IN_intro_module)) {
            DSB(b) = 0;
            f = my_fopen(make_name_n(0x03B8 /* "INTRO" */, (s16)DSW(IN_intro_module), 0x03BE /* "DAT" */),
                         0x03C2 /* "wb" */);
            my_fwrite(b, 1, 1, f);
            my_fclose(f);
        }
        stk_release(m);
    }
}

/* INTRO 038e:080d sys_init */
void sys_init(void)
{
    s16 v = gfx_init((s16)DSW(IN_ctl), 0xFF, 0x00AA /* "vmcode.ovl" */);
    DSW(IN_ctl) = (u16)v;
    if (!v) {
        v = (s16)(s8)gfx_init(0, 0xFF, 0x00AA);
        DSW(IN_ctl) = (u16)v;
        if (!v) { rtl_printf(0x03C5 /* "Cannot initialize graphics card!..." */); prog_exit(0); }
    }
    if (DSW(IN_ctl + 2) == 4 || DSW(IN_ctl + 2) == 7) DSW(IN_ctl + 2) = 2;
    if (!snd_init((s16)DSW(IN_ctl + 2), 0x00B5 /* "sxcode.ovl" */)) {
        rtl_printf(0x0406 /* "Cannot initialize sound & music driver!..." */);
        prog_exit(0);
    }
    divzero_install();
    kbd_install(0);
    mouse_init_dos3();
    chdir_unless(0x044E /* "TTD" */);
    snd_load_resource(0x0452 /* "ttd.sx" */, 0);
    chdir_up();
    if (!DSB(IN_ctl + 8)) snd_disable(-2);
    if (!DSB(IN_ctl + 9)) snd_disable(-1);
}

/* INTRO 038e:08e7 mem_fail(lines): prints the NULL-terminated table, exit(0) */
static void mem_fail(u16 lines)
{
    for (u16 p = lines; DSW(p); p = (u16)(p + 2)) rtl_printf(0x05FC /* "%s\n" */, (unsigned)DSW(p));
    prog_exit(0);
}

/* INTRO 038e:091b / PS 048e:0200 mem_check: GCD's free paragraphs (0000:0512) and, INTRO, EMS */
void mem_check(void)
{
    u32 free = (u32)rd16(0, 0x512) << 4;
    if (prog_current_id() == PROG_PS) {
        if (DSB(0x00AA) || free >= 0x900B0) return;
        for (u16 p = 0x00C7; DSW(p); p = (u16)(p + 2)) rtl_printf(0x0278 /* "%s\n" */, (unsigned)DSW(p));
        prog_exit(0);
    }
    if (DSB(IN_mem_override)) return;
    if (free < 0x94BB0) mem_fail(0x0148);
    if (!lib_ems_detect()) mem_fail(0x0148);
    if ((s16)lib_ems_free_pages() < 21) mem_fail(0x0148);
}

/* INTRO 038e:0ac1 main. PORT: weak until game/placeholder.c (a strong intro_main) is removed. */
__attribute__((weak)) int intro_main(int argc, u16 argv_ds)
{
    main_work_dir(argc, argv_ds, IN_work_dir);
    DSB(0x264B) = 0;
    DSB(0x264A) = 0;
    vol_init(0, 0x4000, 1);
    DSB(IN_prf_loaded) = (u8)control_load();
    intro_modules_detect();
    for (int i = 1; i < argc; i++) {
        u16 a = DSW((u16)(argv_ds + 2 * i));
        if (DSB(a) == '-') {
            s16 c = rtl_tolower((s16)(s8)DSB((u16)(a + 1)));
            if (c == 's') DSW(IN_ctl + 2) = (u16)rtl_atol((u16)(a + 2));
            else if (c == 'p') { if (rtl_tolower((s16)(s8)DSB((u16)(a + 2))) == 'i') DSB(IN_skip_intro) = 1; }
            else if (c == '?' || c == 'h') {
                static const u16 help[10] = { 0x063E, 0x0649, 0x0669, 0x0689, 0x06AC,
                                              0x06D9, 0x06F7, 0x071F, 0x0741, 0x0767 };
                for (int k = 0; k < 10; k++) rtl_printf(help[k]);
                prog_exit(0);
            }
        } else if (rtl_stricmp(a, 0x0783 /* "override" */) == 0) {
            DSB(IN_mem_override) = 1;
        }
    }
    mem_check();
    if (!DSB(IN_skip_intro)) {
        sys_init();
        intro_sequence();
    }
    snd_shutdown();
    sys_shutdown();
    if (LW(G_drv_page_front) != 0xA000) {            /* keep the last picture shown (video) */
        LW(G_drv_page_back) = 0xA000;
        LW(G_drv_page_read) = LW(G_drv_page_front);
        LW(G_drv_page_write) = LW(G_drv_page_back);
        gfx_flip(0);
    }
    control_save();
    prog_exit(2);
}
