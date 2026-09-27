#pragma once
/* Host services on top of SDL3: window/present, the 236.69 Hz PIT tick, keyboard (XT set-1 byte stream
 * for the programs' own INT 9 logic), mouse, gamepad, audio (OPL2 through Nuked-OPL3, the sound
 * service at 59.17 Hz on the audio thread), game file lookup, fatal errors and the headless developer
 * aids. No game logic lives here; only host.c, mem.c and main.c include SDL. */
#include "types.h"

#define PIT_HZ        1193182u
#define PIT_DIV_GAME  5041u          /* timer_install(13): 0FFFFh / 13 -> 236.69 Hz (platform.md §4.1) */
#define SOUND_SERVICE_PIT (PIT_DIV_GAME * 4u)   /* timer_add(service, 4): 59.17 Hz (sound.md §13) */

bool host_init(const char *game_dir, int window_scale, bool fullscreen, bool aspect_4_3);
bool host_init_files(const char *game_dir);  /* no window/audio: --check */
void host_shutdown(void);

/* Called once per 236.69 Hz tick from host_pump() (the port's replacement for IRQ0 / INT 8). The
 * platform timer module installs timer_isr's body (platform.md §4.5). */
void host_set_tick_handler(void (*handler)(void));

/* Source of the displayed image: fills a w x h XRGB8888 frame and returns true if it changed since the
 * last call. Installed by the VGA model (platform/vga.c). Shown with 4:3 aspect unless --aspect 1:1. */
#define HOST_FRAME_MAX_W 320
#define HOST_FRAME_MAX_H 200
void host_set_frame_source(bool (*compose)(u32 *xrgb), int w, int h);

/* Runs due timer ticks, handles window/input events, runs the scripted keys and presents the screen
 * when it changed. Every busy-wait loop of the original (tick waits, key polls, retrace polls) must
 * call this (or a helper that does). Sleeps briefly when nothing was due. */
void host_pump(void);

/* Nanoseconds since host_init (the time base of the tick clock and of the VGA retrace model). */
uint64_t host_ns(void);
/* Pumps until host_ns() >= t. */
void host_wait_until_ns(uint64_t t);

/* Sim frame pacing (PLAN.md decision 4, sim_flow.md §4.1): the flight loop waits until this many
 * 236.69 Hz ticks have passed since the frame started. The original's minimum is 8 ticks (29.6 fps,
 * dt clamped, 1.445x real time); the port default is 16, where the TIME SCALE slider acts. */
#define HOST_DEFAULT_FRAME_TICKS 16
void host_set_frame_ticks(int ticks);
int  host_frame_ticks(void);

/* ---- Keyboard: the byte sequence an XT keyboard would put on port 60h, in event order: normal keys
 * sc / sc|80h, grey keys E0 sc / E0 sc|80h, Pause E1 1D 45 E1 9D C5, Print Screen E0 2A E0 37 /
 * E0 B7 E0 AA. Key repeats feed the make code again. The platform keyboard module runs the ported
 * INT 9 logic on it (platform.md §3.3). */
void host_set_kbd_handler(void (*handler)(u8 byte));
void host_set_focus_lost_handler(void (*handler)(void));   /* keys released outside the window */

/* ---- Mouse (INT 33h model, platform/int33.c): position in 320x200 screen pixels, buttons bit0 left,
 * bit1 right. The handler is called from host_pump() whenever position or buttons changed. */
void host_set_mouse_handler(void (*handler)(int x, int y, u8 buttons));
void host_mouse_read(int *x, int *y, u8 *buttons);
void host_mouse_warp(int x, int y);                         /* INT 33h fn 4 */

/* ---- Joystick (port 201h model, platform/gameport.c): first gamepad. Stick 0 = left stick (A X/Y),
 * stick 1 = right stick (B X/Y), axes -32768..32767; buttons bit0 A1 (south), bit1 A2 (east),
 * bit2 B1 (west), bit3 B2 (north). */
bool host_joy_present(void);
void host_joy_axes(s16 axes[4]);
u8   host_joy_buttons(void);

/* ---- Audio. OPL2 = one Nuked-OPL3 in OPL2 mode, rendered at 49716 Hz. host_opl_write takes the audio
 * lock. The sound service (the SCI player, sound.md §13) runs on the audio thread every 20164 PIT
 * clocks of produced audio (59.17 Hz) with the lock held, so its register writes are sample-exact.
 * Game-side sound API calls take the lock around their work (the original's cli sections). */
void host_opl_write(u8 reg, u8 value);
u8   host_opl_status(void);                                  /* port 388h read (timer flags) */
void host_audio_lock(void);
void host_audio_unlock(void);
void host_set_sound_service(void (*service)(void));

/* ---- Game files: case-insensitive lookup inside the game directory. Returns a malloc'd path (free with
 * host_free) or NULL if the file does not exist; with create = true the path of a new file. */
char *host_game_path(const char *name, bool create);
const char *host_game_dir(void);
void  host_free(void *p);

/* ---- Errors: prints (and shows unless headless) a message, shuts down and exits with code 3. */
_Noreturn void host_fatal(const char *fmt, ...);

/* ---- Developer aids (environment variables; work headless with SDL_VIDEO_DRIVER=dummy and
 *      SDL_AUDIO_DRIVER=dummy):
 *   AOTP_SNAPSHOT_DIR=dir     every presented frame >= AOTP_SNAPSHOT_MS (default 1000) after the
 *                             previous snapshot is saved as snapNNNN.png
 *   AOTP_KEYS="<seconds>:<xt>[+<xt>...],..."  presses (in order) and releases (in reverse) the XT keys
 *                             that many seconds after start-up; "p" after the codes only presses (held),
 *                             "r" only releases: "9:48p,20:48r" holds Up for 11 s. Grey keys are written
 *                             with their E0 prefix, e.g. e048.
 *   AOTP_TRACE=1              prints program switches and exit codes to stderr */
void host_snapshot_now(const char *tag);                     /* forces a snapshot (if enabled) */
bool host_trace(void);
