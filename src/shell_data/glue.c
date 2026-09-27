/* shell_data: UI / video glue in the sys range (PS 2171, 21f4, 2221, 229e, 22a0, 230d),
 * shell_data_sys.md §8 and §11 — re-read from the disassembly. The busy waits pump the host (PORT). */
#include "shell_data/sd_priv.h"
#include "shell_ui/shell_ui.h"
#include "video/video.h"
#include "host.h"

#define TTM_TICKS  0x52AA      /* u32, 59.17 Hz */
#define TTM_TIMER  0x52A8
#define TTM_DELAY  0x76A4
#define SH_TICKS   0x1CC8      /* u32, 236.7 Hz */
#define JOY_ON     0x43B6
#define MOUSE_ON   0x43B7

/* ---------------------------------------------------------------- 2171 */

/* PS 2171:000a wait_ticks_or_key(ticks) */
void wait_ticks_or_key(s16 ticks)
{
    s16 hit = 0;
    DSW(0x2E3F) = (u16)ticks;
    while (DSW(0x2E3F) && !hit) {
        host_pump();                                    /* PORT: the countdown runs from host_pump */
        u16 k = key_read();
        DSB(0x6017) = (u8)k;
        DSB(0x6016) = (u8)((s16)k >> 8);
        if (menu_hotkey(DSB(0x6016)) != 0) continue;
        hit = DSB(0x6016) == 0x1C || DSB(0x6016) == 1 || DSB(0x6016) == 0x39;
        if (DSB(JOY_ON) && !hit) hit = joy_button(0) || joy_button(1);
        if (DSB(MOUSE_ON) && !hit) hit = mouse_button_dos3(0) || mouse_button_dos3(1);
    }
}

/* ---------------------------------------------------------------- 21f4 TTM player */

/* PS 21f4:0018 ttm_skip_check -> 1 = continue */
static s16 ttm_skip_check(void)
{
    if (menu_hotkey(0) != 0) return 1;    /* TODO(verify): 2e25:0419 is called with no pushed argument */
    s16 skip = key_held(0x1C) || key_held(1) || key_held(0x39);
    key_read();
    if (DSB(JOY_ON) && !skip) skip = joy_button(0) || joy_button(1);
    if (DSB(MOUSE_ON) && !skip) skip = mouse_button_dos3(0) || mouse_button_dos3(1);
    return skip ? 0 : 1;
}

/* PS 21f4:00cd ttm_open(name) */
static s8 ttm_open(u16 name_ds)
{
    ps_clip_full();
    DSW(TTM_TIMER) = (u16)timer_add(codeptr_far(PS_FN_shell_tick_cb), 4);
    DSL(TTM_TICKS) = 0;
    ttm_init();
    if (ttm_load(name_ds) == -1) {
        ttm_shutdown();
        timer_remove((s16)DSW(TTM_TIMER));
        return 0;
    }
    return ttm_index(0) >= 1 ? 1 : 0;
}

/* PS 21f4:0134 ttm_close */
static void ttm_close(void)
{
    ttm_free(0);
    ttm_shutdown();
    timer_remove((s16)DSW(TTM_TIMER));
}

static s8 call_cb(FarPtr cb)
{
    return (s8)((s16 (*)(void))codeptr_lookup(cb))();
}

/* PS 21f4:0151 ttm_play(first, last, cb) -> -1 cb stop, -2 script end, else next frame */
static s16 ttm_play(s16 first, s16 last, FarPtr cb)
{
    s16 si = first;
    s8 cont = 1;
    s16 run = 1;
    s32 delay = 0;
    while (cont && run && (last == -1 || si < last)) {
        DSW(TTM_DELAY) = 0xFFFF;
        u32 t0 = DSL(TTM_TICKS);
        run = ttm_run((u16)si);
        if (run) {
            gfx_flip(1);
            upd_flush(0);
        }
        if (DSW(TTM_DELAY) != 0xFFFF) delay = (s32)(s16)DSW(TTM_DELAY);
        if (delay) {
            u32 until = t0 + (u32)delay;
            while (until > DSL(TTM_TICKS)) {
                if (!far_is_null(cb)) {
                    cont = call_cb(cb);
                    if (!cont) break;
                }
                host_pump();                            /* PORT: the tick counter runs from host_pump */
            }
        } else if (!far_is_null(cb)) cont = call_cb(cb);
        si++;
    }
    if (!cont) return -1;
    if (!run) return -2;
    return si;
}

/* PS 21f4:0245 ttm_play_file(name, last, nrand, span, end_frame, cb) — "TTD" DS:1B7C */
void ttm_play_file(u16 name_ds, s16 last, s16 nrand, s16 span, s16 end_frame, FarPtr cb)
{
    if (!ttm_open(name_ds)) return;
    dev_chdir(0x1B7C);
    s16 r = ttm_play(0, last, cb);
    if (r != -2) {
        if (r != -1) {
            r = (s16)(r + rng_below(nrand) * span);
            r = ttm_play(r, (s16)(r + span), cb);
        }
        if (r != -2) ttm_run((u16)(end_frame - 1));
    }
    dev_chdir_up();
    ttm_close();
}

/* ---------------------------------------------------------------- 2221 TTM hooks */

/* PS 2221:0003 hook_draw_medal — "medals" DS:1B8C "bmp" DS:1B93, "medlocs" DS:1B97 "dat" DS:1B9F,
 * "TTD" DS:1BA3 */
static void hook_draw_medal(void)
{
    u16 mk = stk_mark();
    u16 loc = stk_alloc(0x50);
    dev_chdir_up();
    DSW(0x0A16) = bmp_load_planes(make_name(0x1B8C, -1, 0x1B93));
    file_read_n(loc, 4, 0x14, make_name(0x1B97, -1, 0x1B9F));
    dev_chdir(0x1BA3);
    s16 m = (s16)DSC(0x0500);
    s16 w = (s16)DSW((u16)(loc + 2 + m * 4));
    s16 h = (s16)DSW(DSW((u16)(DSW(0x0A16) + m * 2)) + 8);
    s16 x = (s16)(0x6A - (w >> 1));
    s16 y = (s16)(0x69 - (h >> 1));
    pages_front_to_back();
    gfx_sprite_flip(DSW((u16)(DSW(0x0A16) + (s16)DSC(0x0500) * 2)), x, y, 0);
    pages_back_to_bg();
    gfx_copy_rect(x, y, w, h);
    upd_add((u16)x, (u16)y, (u16)w, (u16)h, 1, DSW(0x2310), DSW(0x2314), 0, far_null());
    bmp_free(DSW(0x0A16));
    stk_release(mk);
}

/* PS 2221:0107 hook_1 */
static void hook_1(void) { event_ttm_text_hook(); }

/* PS 2221:0111 ttm_hook(n) — far table DS:1B80 (no bound check) */
void ttm_hook(s16 n)
{
    FarPtr f = ds_far((u16)(0x1B80 + n * 4));
    if (!far_is_null(f)) codeptr_lookup(f)();
}

/* ---------------------------------------------------------------- 229e / 22a0 timers and dir list */

/* PS 229e:0013 shell_tick — timer callback */
static void shell_tick(void) { DSL(SH_TICKS) += 1; }

/* PS 229e:001f shell_ticks */
u32 shell_ticks(void) { return DSL(SH_TICKS); }

/* PS 22a0:0009 tmr_expired(t) — {u32 start, u16 len} */
s16 tmr_expired(u16 t_ds)
{
    if (DSB(0x2E3E) == 0) return 1;
    u32 el = shell_ticks() - DSL(t_ds);
    return (u32)DSW(t_ds + 4) < el ? 1 : 0;
}

/* PS 22a0:0050 tmr_set(t, len) */
void tmr_set(u16 t_ds, u16 len)
{
    DSL(t_ds) = shell_ticks();
    DSW(t_ds + 4) = len;
}

/* PS 22a0:006a tmr_set_expired(t) */
void tmr_set_expired(u16 t_ds)
{
    DSW(t_ds + 4) = 0;
    DSL(t_ds) = shell_ticks() - 1;
}

/* PS 22a0:01cb dir_first(pattern, attr) */
void dir_first(u16 pattern_ds, s16 attr)
{
    DSW(0x540F) = 1;
    DSW(0x540B) = (u16)attr;
    DSW(0x540D) = pattern_ds;
}

/* PS 22a0:01e2 dir_next -> ffblk name or 0 (ffblk DS:52B4) */
u16 dir_next(void)
{
    s16 r = DSW(0x540F) ? rtl_findfirst(DSW(0x540D), 0x52B4, (s16)DSW(0x540B)) : rtl_findnext(0x52B4);
    DSW(0x540F) = 0;
    return r == 0 ? 0x52D2 : 0;
}

/* PS 22a0:0225 strip_ext(names, n) */
void strip_ext(u16 names_ds, s16 n)
{
    while (--n >= 0) {
        u16 p = rtl_strchr(DSW(names_ds), '.');
        names_ds += 2;
        if (p) DSB(p) = 0;
    }
}

/* PS 22a0:025d dir_list(pattern, &n) -> names DS:53E3 (20 x 13 bytes at DS:52DF) or 0 */
u16 dir_list(u16 pattern_ds, u16 n_ds)
{
    s16 si = 0;
    for (s16 i = 0; i < 0x14; i++) DSW((u16)(0x53E3 + i * 2)) = (u16)(0x52DF + i * 0x0D);
    dir_first(pattern_ds, 0);
    u16 p;
    while (si < 0x14 && (p = dir_next()) != 0) {
        rtl_strcpy(DSW((u16)(0x53E3 + si * 2)), p);
        si++;
    }
    DSW(n_ds) = (u16)si;
    if (!si) return 0;
    strip_ext(0x53E3, si);
    return 0x53E3;
}

/* PS 22a0:02cb ps_file_exists(pattern) */
s16 ps_file_exists(u16 pattern_ds)
{
    dir_first(pattern_ds, 0);
    return dir_next() ? 1 : 0;
}

/* ---------------------------------------------------------------- 230d input toggles */

/* PS 230d:000d mouse_enabled */
s16 mouse_enabled(void) { return (s16)DSC(MOUSE_ON); }

/* PS 230d:0018 mouse_toggle */
s16 mouse_toggle(void)
{
    DSB(MOUSE_ON) ^= 1;
    if (mouse_enabled()) {
        DSB(MOUSE_ON) = DSB(0x308E) != 0;
        if (mouse_enabled()) mouse_move_to((s16)DSW(0x601A), (s16)DSW(0x6018));
    }
    return mouse_enabled();
}

/* PS 230d:0058 joy_enabled */
s16 joy_enabled(void) { return (s16)DSC(JOY_ON); }

/* PS 230d:0063 joy_recheck */
void joy_recheck(void)
{
    if (joy_enabled()) {
        DSB(0x0FF8) = 0;
        joy_calibrate();
        DSB(JOY_ON) = DSB(0x0FF8) != 0;
    }
}

/* PS 230d:008b joy_toggle */
s16 joy_toggle(void)
{
    DSB(JOY_ON) ^= 1;
    joy_recheck();
    return joy_enabled();
}

__attribute__((constructor)) static void glue_codeptrs(void)
{
    codeptr_register(PROG_PS, PS_FN_ttm_skip_check, (CodeFn)ttm_skip_check);
    codeptr_register(PROG_PS, PS_FN_hook_draw_medal, (CodeFn)hook_draw_medal);
    codeptr_register(PROG_PS, PS_FN_hook_1, (CodeFn)hook_1);
    codeptr_register(PROG_PS, PS_FN_shell_tick, (CodeFn)shell_tick);
}
