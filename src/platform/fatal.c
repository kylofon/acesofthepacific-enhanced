/* Fatal errors and asserts (platform.md §2.7): INTRO 05bb, PS 227c (fatal_error / assert_fail),
 * AP 2494. Every path shuts the system down, restores text mode, prints its message (the RTL printf:
 * text written to the console goes to the host's stderr) and exits with 0 (GCD then quits).
 * PORT: the final "wait for a key" (kbhit loop) is dropped - the port shows no text screen, and the
 * message is on stderr. */
#include "platform/rtl_priv.h"

#include <string.h>

#include "host.h"
#include "prog.h"
#include "sound/sound.h"
#include "symbols_ap.h"
#include "symbols_intro.h"
#include "symbols_ps.h"
#include "video/video.h"

static void wait_key(void)
{
    /* PORT: while (!kbhit()); dropped (see the file comment). */
}

/* A DS copy of a string constant on the emulated stack (the caller releases the stack mark). */
static u16 lit(const char *s)
{
    u16 n = (u16)strlen(s);
    u16 o = stk_alloc((u16)(n + 1));
    memcpy(DSP(o), s, n + 1);
    return o;
}

/* INTRO 05bb:0001 nop_far: empty, no callers */
void nop_far(void) { }

/* INTRO 05bb:0006 kbd_break_hook: the (empty) Ctrl+Esc hook of INTRO's keyboard ISR */
void kbd_break_hook(s16 a0) { (void)a0; }

/* INTRO 05bb:000b shutdown_all (PS: 227c:004d ps_shutdown) */
static void shutdown_all(void)
{
    vol_shutdown();
    sys_shutdown();
}

/* INTRO 05bb:001a quit(code) (dead) */
void quit(s16 code)
{
    shutdown_all();
    if (code) {
        if (DSW(INTRO_DS_control_prf + 2) != 3) snd_shutdown();   /* sound_drv 3 = MT-32 */
        gfx_restore_text();
        prog_exit(3);
    }
    snd_shutdown();
    gfx_restore_text();
    prog_exit(0);
}

/* INTRO 05bb:0052 / PS 227c:00f8 fatal_error(fmt, ...) */
void fatal_error(u16 a0, u16 a1, ...)
{
    if (prog_current_id() == PROG_PS) ps_shutdown();
    else shutdown_all();
    snd_shutdown();
    gfx_restore_text();
    if (a0) {
        /* the variadic words start with a1: rebuild the list a1, ... */
        int n = rtl_va_count(a0);
        u16 w[32];
        va_list ap;
        va_start(ap, a1);
        for (int i = 0; i < n && i < 32; i++) w[i] = i == 0 ? a1 : (u16)va_arg(ap, unsigned);
        va_end(ap);
        u16 m = stk_mark();
        u16 va = rtl_va_words(a0, w, n);
        rtl_vprintf(a0, va);
        rtl_printf(lit("\n"));
        stk_release(m);
    }
    wait_key();
    prog_exit(0);
}

/* AP 2494:0009 far_to_near_str: s if it is in DGROUP, else a copy in the 512-byte ring DS:5EE2 */
u16 far_to_near_str(FarPtr a0)
{
    if (a0.seg == cur_dgroup) return a0.off;
    u16 len = (u16)(rtl__fstrlen(a0) + 1);
    u16 idx = DSW(AP_DS_str_ring_pos);
    if ((s16)(idx + len) > 0x200) DSW(AP_DS_str_ring_pos) = 0;
    u16 si = (u16)(DSW(AP_DS_str_ring_pos) + AP_DS_str_ring);
    DSW(AP_DS_str_ring_pos) = (u16)(DSW(AP_DS_str_ring_pos) + len);
    rtl__fstrcpy(ds_ptr(si), a0);
    return si;
}

/* AP 2494:0057 fatal_lines(char **lines, int n): prints n+1 lines */
static void fatal_lines(u16 lines_ds, s16 n)
{
    sx_shutdown();                                   /* AP pushes 1 (header: void) */
    gfx_restore_text();
    u16 m = stk_mark();
    u16 fmt = lit("%s\n");
    while (--n >= -1) {
        rtl_printf(fmt, (unsigned)DSW(lines_ds));
        lines_ds = (u16)(lines_ds + 2);
    }
    stk_release(m);
    int24_restore();
    sys_shutdown();
    wait_key();
    prog_exit(0);
}

/* AP 2494:00a9 fatal_disk_full: "The mission recorder has run out of disk space..." */
void fatal_disk_full(void) { fatal_lines(0x1D7E /* char *lines[5]: "The mission recorder has run out of disk space..." */, 4); }

/* AP 2494:00ba basename_upper(char far *path): upper-case base name without extension (in the
 * near copy - or in place when path is in DGROUP) */
static u16 basename_upper(FarPtr path)
{
    u16 dx = far_to_near_str(path);
    u16 q = rtl_strrchr(dx, '\\');
    if (q && (u16)(q + 1)) dx = (u16)(q + 1);
    u16 dot = rtl_strchr(dx, '.');
    if (dot) DSB(dot) = 0;
    return rtl_strupr(dx);
}

/* INTRO 05bb:009f / PS 227c:0143 / AP 2494:0127 assert_fail(char far *file, int line, char *msg) */
s16 assert_fail(FarPtr a0, s16 a1, u16 p2_ds)
{
    int id = prog_current_id();
    u16 m = stk_mark();
    if (id == PROG_AP) {
        sx_shutdown();                               /* AP pushes 1 (header: void) */
        gfx_restore_text();
        rtl_printf(0x1E42 /* "A SHELL system error has occured..." */);
        rtl_printf(0x1E76 /* "...contact Sierra Customer Support:" */);
        u16 msg = p2_ds ? p2_ds : 0x1EB7 /* " " */;
        u16 name = basename_upper(a0);
        rtl_printf(0x1EAB /* "%s:%d (%s)\n" */, (unsigned)name, (unsigned)(u16)a1, (unsigned)msg);
        int24_restore();
        sys_shutdown();
    } else if (id == PROG_PS) {
        ps_shutdown();
        snd_unload(0);
        gfx_restore_text();
        rtl_printf(0x1BEA /* "A SHELL system error has occured..." */);
        rtl__fstrcpy(ds_ptr(PS_DS_emd_bounce), a0);
        rtl_printf(0x1C59 /* "%s:%d (%s)\n" */, (unsigned)PS_DS_emd_bounce, (unsigned)(u16)a1,
                   (unsigned)(p2_ds ? p2_ds : 0x1C65 /* " " */));
        rtl_printf(0x1C67 /* "RSEED = %d\n" */, (unsigned)DSW(PS_DS_mission_rseed));
        rtl_printf(0x1C73 /* "MODE = %d\n" */, (unsigned)(u16)(s16)DSC(0x626B));
        rtl_printf(0x1C7E /* "MTYPE = %d\n" */, (unsigned)(u16)(s16)DSC(0x626C));
        rtl_printf(0x1C8A /* "DATE = %d\n" */, (unsigned)DSW(0x6277));
        rtl_printf(0x1C95 /* "WORLD = %d\n" */, (unsigned)DSB(0x626D));
        rtl_printf(0x1CA1 /* "PLYR SINDEX = %d\n" */, (unsigned)DSW(0x62DB));
        rtl_printf(0x1CB3 /* "ENEMY SERVICE = %d\n" */, (unsigned)(u16)(s16)DSC(0x6355));
    } else {
        shutdown_all();
        snd_unload(0);
        gfx_restore_text();
        rtl_printf(0x0848 /* "A SHELL system error has occured..." */);
        u16 loc = stk_alloc(0x32);
        rtl__fstrcpy(ds_ptr(loc), a0);
        rtl_printf(0x08B9 /* "%s:%d (%s)\n" */, (unsigned)loc, (unsigned)(u16)a1,
                   (unsigned)(p2_ds ? p2_ds : 0x08C5 /* " " */));
    }
    stk_release(m);
    wait_key();
    prog_exit(0);
}

/* AP 2494:019b stack_left: (&local + _stklen) + 1 if that passes 64 KB, else 0 */
u16 stack_left(void)
{
    u32 t = (u32)(u16)(dg_sp - 4) + DSW(AP_DS__stklen);
    return t > 0xFFFF ? (u16)(t + 1) : 0;
}

/* AP 2494:01ca assert_fail0(file, line) = assert_fail(file, line, NULL) */
s16 assert_fail0(FarPtr a0, s16 a1) { return assert_fail(a0, a1, 0); }

/* AP 2494:01df: empty */
void empty_2494_01df(void) { }
