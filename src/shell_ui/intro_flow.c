/* shell_ui: INTRO.EXE's intro sequence (038e:0973) and its TTM player ("intro.c", segment 0460) —
 * shell_ui.md §2.3, §2.4. INTRO DGROUP 17E0. */
#include "shell_ui/sui_a_priv.h"

#include "symbols_intro.h"

#define IN_intro_module 0x00C2   /* u16: add-on intro forced by INTRO<n>.DAT */
#define IN_skipped      0x00C4   /* u8 */
#define IN_joy_on       0x24B7   /* u8 (control.prf) */
#define IN_mouse_on     0x24B8   /* u8 */
#define IN_timer_handle 0x24D6   /* s16 */

/* INTRO 0460:001a intro_skip_check -> continue: Enter, Esc, Space, a stick or mouse button skips */
static u8 intro_skip_check(void)
{
    bool skip = key_held(0x1C) || key_held(0x01) || key_held(0x39);
    key_read();                                        /* one key from the BIOS buffer, discarded */
    if (DSB(IN_joy_on)) skip = skip || joy_button(0) || joy_button(1);
    if (DSB(IN_mouse_on)) skip = skip || mouse_button_dos3(0) || mouse_button_dos3(1);
    DSB(IN_skipped) = skip;
    return !skip;
}

/* INTRO 0460:00d3 intro_ttm_open(name) -> ok */
static u8 intro_ttm_open(u16 name_ds)
{
    LB(G_drv_fill_on) = 1;
    LB(G_drv_clip_on) = 1;
    LW(G_drv_clip_x1) = 0;
    LW(G_drv_clip_y1) = 0;
    LW(G_drv_clip_x2) = 319;
    LW(G_drv_clip_y2) = 199;
    DSW(IN_timer_handle) = (u16)timer_add(codeptr_far(INTRO_FN_intro_tick_cb), 4);   /* 59.17 Hz */
    DSL(INTRO_DS_intro_ticks) = 0;
    ttm_init();
    if (ttm_load(name_ds) == -1) {
        ttm_shutdown();
        timer_remove((s16)DSW(IN_timer_handle));
        return 0;
    }
    return ttm_index(0) > 0;
}

/* INTRO 0460:0157 intro_ttm_close */
static void intro_ttm_close(void)
{
    ttm_free(0);
    ttm_shutdown();
    timer_remove((s16)DSW(IN_timer_handle));
}

/* INTRO 0460:0174 intro_ttm_play(first, last, cb) -> last frame, -1 skipped, -2 script ended */
static s16 intro_ttm_play(s16 f, s16 last, FarPtr cb)
{
    u8 cont = 1;
    s16 running = 1;
    s32 delay = 0;                                    /* persists across frames */
    while (cont && running && (last == -1 || f < last)) {
        if (win_detect() && DSB(0x264B) && !DSB(0x264A)) win_task_switch();
        else DSB(0x264B) = 0;
        u32 t0 = DSL(INTRO_DS_intro_ticks);
        DSW(INTRO_DS_ttm_delay) = 0xFFFF;             /* the script sets its frame delay here */
        running = ttm_run((u16)f);
        if (running) {
            gfx_flip(1);
            upd_flush(0);
        }
        if (DSW(INTRO_DS_ttm_delay) != 0xFFFF) delay = (s16)DSW(INTRO_DS_ttm_delay);
        if (delay == 0) {
            if (!far_is_null(cb)) cont = ((u8 (*)(void))codeptr_lookup(cb))();
        } else {
            u32 end = t0 + (u32)delay;
            while (end > DSL(INTRO_DS_intro_ticks)) {
                host_pump();                          /* PORT: the tick counter runs from host_pump */
                if (!far_is_null(cb) && !(cont = ((u8 (*)(void))codeptr_lookup(cb))())) break;
            }
        }
        f++;
    }
    if (!cont) return -1;
    if (!running) return -2;
    return f;
}

/* INTRO 0460:028c intro_play(name, last, unused, extra, end_frame, cb) */
static void intro_play(u16 name_ds, s16 last, s16 unused, s16 extra, s16 end_frame, FarPtr cb)
{
    (void)unused;
    if (!intro_ttm_open(name_ds)) return;
    chdir_unless(0x0790 /* "TTD" */);                  /* development leftover: no-op with volumes */
    s16 r = intro_ttm_play(0, last, cb);
    if (r != -2) {
        if (r == -1) {
            ttm_run((u16)(end_frame - 1));             /* skipped: the script's exit frame */
        } else {
            r = (s16)(r + extra);
            if (intro_ttm_play(r, (s16)(r + extra), cb) != -2) ttm_run((u16)(end_frame - 1));
        }
    }
    chdir_unless(0x02E4 /* ".." */);                   /* 038e:00bc chdir_up */
    intro_ttm_close();
}

/* INTRO 038e:0973 intro_sequence: INTACE1, INTACE2, then INTACE3/4/5 or the 1946 variants */
void intro_sequence(void)
{
    static const u16 names[5][2] = { { 0x0600, 0x0608 }, { 0x060C, 0x0614 }, { 0x0618, 0x0620 },
                                     { 0x0624, 0x062C }, { 0x0630, 0x0638 } };   /* "intaceN", "ttm" */
    s16 v = (s16)DSW(IN_intro_module);
    if (v == 0) v = (s8)module_pick(DSB(INTRO_DS_cset_mask));
    FarPtr cb = codeptr_far(INTRO_FN_intro_skip_check);
    for (int i = 0; i < 5; i++) {
        if (DSB(IN_skipped)) continue;
        s16 num = -1;
        if (i >= 2) {
            if (v == 0) v = -1;
            num = v;
        }
        intro_play(make_name_n(names[i][0], num, names[i][1]), -1, 0, 0, 0x1CD, cb);
    }
}

__attribute__((constructor)) static void intro_flow_codeptrs(void)
{
    codeptr_register(PROG_INTRO, INTRO_FN_intro_skip_check, (CodeFn)intro_skip_check);
}
