#include "host.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enhanced.h"   /* ENH: key bindings */
#include "opl3.h"

#define OPL_RATE 49716u                /* Nuked-OPL3 native rate; SDL resamples */

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioStream *audio;
static SDL_Mutex *audio_mutex;
static SDL_Gamepad *gamepad;
static char *game_dir;
static bool headless;
static bool aspect43 = true;

static void (*tick_handler)(void);
static void (*kbd_handler)(u8);
static void (*focus_lost_handler)(void);
static void (*mouse_handler)(int, int, u8);
static void (*sound_service)(void);
static bool (*frame_source)(u32 *);
static u32 *frame;                     /* ENH: allocated for the frame source's size */
static int frame_w = 320, frame_h = 200;
static int scr_x0, scr_y0, scr_w = 320, scr_h = 200;   /* ENH: the 320x200 screen inside the frame (mouse) */

/* Logical presentation: frame width x 3/4 of it (4:3, as on a VGA monitor) or square pixels. */
static int view_w(void) { return frame_w; }
static int view_h(void) { return aspect43 ? frame_h * 6 / 5 : frame_h; }   /* ENH: 200 lines -> 240 */

/* Tick clock: tick n is due at start + n * 5041 / 1193182 s (exact rational arithmetic). */
static Uint64 clock_start_ns;
static Uint64 ticks_run;

/* Audio: one OPL3 chip in OPL2 mode, the sound service clock (PIT clocks x OPL_RATE units). */
static opl3_chip opl;
static u64 svc_acc;
static u8 opl_status;

/* Mouse: position in window-logical units, buttons, "changed" flag for the handler. */
static float mouse_x, mouse_y;
static u8 mouse_buttons;
static bool mouse_changed;

static int frame_ticks = HOST_DEFAULT_FRAME_TICKS;
static int trace = -1;

static void process_events(void);

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    (void)userdata; (void)total;
    int frames = additional / 4;
    s16 buf[2 * 1024];
    const u64 period = (u64)SOUND_SERVICE_PIT * OPL_RATE;       /* one service in PIT-clock x rate units */
    SDL_LockMutex(audio_mutex);
    while (frames > 0) {
        u64 to_svc = (period - svc_acc + PIT_HZ - 1) / PIT_HZ; /* samples until the next service */
        int n = frames < 1024 ? frames : 1024;
        if ((u64)n > to_svc) n = (int)to_svc;
        if (n > 0) {
            OPL3_GenerateStream(&opl, buf, (uint32_t)n);
            SDL_PutAudioStreamData(stream, buf, n * 4);
            frames -= n;
            svc_acc += (u64)n * PIT_HZ;
        }
        if (svc_acc >= period) {
            svc_acc -= period;
            if (sound_service) sound_service();
        }
    }
    SDL_UnlockMutex(audio_mutex);
}

bool host_init_files(const char *dir)
{
    if (!SDL_Init(0)) return false;
    game_dir = SDL_strdup(dir);
    return true;
}

bool host_init(const char *dir, int window_scale, bool fullscreen, bool aspect_4_3)
{
    const char *vd = SDL_getenv("SDL_VIDEO_DRIVER");
    headless = vd && SDL_strcasecmp(vd, "dummy") == 0;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    game_dir = SDL_strdup(dir);
    aspect43 = aspect_4_3;
    if (window_scale < 1) window_scale = 3;
    if (!SDL_CreateWindowAndRenderer("Aces of the Pacific", 320 * window_scale,
                                     (aspect43 ? 240 : 200) * window_scale, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (fullscreen) SDL_SetWindowFullscreen(window, true);
    SDL_HideCursor();                                    /* the programs draw their own pointer */
    host_set_frame_source(NULL, 320, 200);

    audio_mutex = SDL_CreateMutex();
    OPL3_Reset(&opl, OPL_RATE);
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, (int)OPL_RATE };
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());

    clock_start_ns = SDL_GetTicksNS();
    ticks_run = 0;
    return true;
}

void host_shutdown(void)
{
    if (audio) SDL_DestroyAudioStream(audio);
    audio = NULL;
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad = NULL;
    if (texture) SDL_DestroyTexture(texture);
    texture = NULL;
    if (renderer) SDL_DestroyRenderer(renderer);
    renderer = NULL;
    if (window) SDL_DestroyWindow(window);
    window = NULL;
    if (audio_mutex) SDL_DestroyMutex(audio_mutex);
    audio_mutex = NULL;
    SDL_free(frame);                                     /* ENH */
    frame = NULL;
    SDL_free(game_dir);
    game_dir = NULL;
    SDL_Quit();
}

void host_set_tick_handler(void (*handler)(void)) { tick_handler = handler; }
void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_focus_lost_handler(void (*handler)(void)) { focus_lost_handler = handler; }
void host_set_mouse_handler(void (*handler)(int, int, u8)) { mouse_handler = handler; }

void host_set_frame_source(bool (*compose)(u32 *), int w, int h)
{
    frame_source = compose;
    int old_vw = view_w(), old_vh = view_h();
    frame_w = SDL_clamp(w, 1, HOST_FRAME_MAX_W);
    frame_h = SDL_clamp(h, 1, HOST_FRAME_MAX_H);
    scr_x0 = scr_y0 = 0; scr_w = frame_w; scr_h = frame_h;
    SDL_free(frame);                                     /* ENH: any size up to HOST_FRAME_MAX */
    frame = SDL_calloc((size_t)frame_w * frame_h, sizeof(u32));
    if (!frame) host_fatal("out of memory (frame %dx%d)", frame_w, frame_h);
    SDL_SetRenderLogicalPresentation(renderer, view_w(), view_h(), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, frame_w, frame_h);
    /* ENH: a high-resolution frame is filtered to the window */
    SDL_SetTextureScaleMode(texture, frame_h > 200 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    /* ENH: a wider picture (widescreen) widens the window at the same height */
    if (window && !headless && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) &&
        (long)view_w() * old_vh != (long)old_vw * view_h()) {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_SetWindowSize(window, wh * view_w() / view_h(), wh);
    }
}

void host_set_screen_rect(int x0, int y0, int w, int h)
{
    scr_x0 = x0; scr_y0 = y0;
    scr_w = w > 0 ? w : frame_w;
    scr_h = h > 0 ? h : frame_h;
}

uint64_t host_ns(void) { return SDL_GetTicksNS() - clock_start_ns; }

static Uint64 tick_due_ns(Uint64 n)
{
    return clock_start_ns + n * PIT_DIV_GAME * SDL_NS_PER_SECOND / PIT_HZ;
}

bool host_trace(void)
{
    if (trace < 0) {
        const char *t = SDL_getenv("AOTP_TRACE");
        trace = t && *t && *t != '0';
    }
    return trace != 0;
}

/* ---------------------------------------------------------------- snapshots (AOTP_SNAPSHOT_DIR) */

static int snap_n;
static Uint64 snap_last_ns;

static void save_snapshot(const char *dir, const char *tag)
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(frame_w, frame_h, SDL_PIXELFORMAT_XRGB8888, frame, frame_w * 4);
    if (!s) return;
    char path[768];
    if (tag) SDL_snprintf(path, sizeof path, "%s/snap%04d_%s.png", dir, snap_n++, tag);
    else SDL_snprintf(path, sizeof path, "%s/snap%04d.png", dir, snap_n++);
    if (!SDL_SavePNG(s, path)) {
        char *bmp = NULL;                                 /* PNG writer unavailable: BMP */
        SDL_asprintf(&bmp, "%.*s.bmp", (int)(strlen(path) - 4), path);
        if (bmp) SDL_SaveBMP(s, bmp);
        SDL_free(bmp);
    }
    SDL_DestroySurface(s);
    snap_last_ns = SDL_GetTicksNS();
    if (host_trace()) fprintf(stderr, "[host] snapshot %s\n", path);
}

static void snapshot(void)
{
    static const char *dir;
    static bool checked;
    static Uint64 interval = SDL_NS_PER_SECOND;
    if (!checked) {
        dir = SDL_getenv("AOTP_SNAPSHOT_DIR");
        const char *ms = SDL_getenv("AOTP_SNAPSHOT_MS");
        if (ms && SDL_atoi(ms) > 0) interval = (Uint64)SDL_atoi(ms) * SDL_NS_PER_MS;
        checked = true;
    }
    if (!dir) return;
    if (snap_n && SDL_GetTicksNS() - snap_last_ns < interval) return;
    save_snapshot(dir, NULL);
}

void host_snapshot_now(const char *tag)
{
    const char *dir = SDL_getenv("AOTP_SNAPSHOT_DIR");
    if (!dir) return;
    if (frame_source) frame_source(frame);
    save_snapshot(dir, tag);
}

static void present(void);
/* Developer aid: an unchanged picture still gets its periodic snapshot (a static screen or a hang). */
static void snapshot_static(void)
{
    const char *dir = SDL_getenv("AOTP_SNAPSHOT_DIR");
    const char *ms = SDL_getenv("AOTP_SNAPSHOT_MS");
    Uint64 interval = (ms && SDL_atoi(ms) > 0) ? (Uint64)SDL_atoi(ms) * SDL_NS_PER_MS : SDL_NS_PER_SECOND;
    if (dir && snap_n && SDL_GetTicksNS() - snap_last_ns >= interval) save_snapshot(dir, NULL);
}

static void present(void)
{
    snapshot();
    if (!texture) return;
    SDL_UpdateTexture(texture, NULL, frame, frame_w * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_FRect dst = { 0, 0, (float)view_w(), (float)view_h() };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

static Uint64 last_present_ns;

/* ---------------------------------------------------------------- scripted keys (AOTP_KEYS) */

static void scripted_keys(void)
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = SDL_getenv("AOTP_KEYS"); checked = true; }
    if (!spec || !*spec || !kbd_handler) return;
    char *end;
    double at = SDL_strtod(spec, &end);
    if (end == spec || *end != ':') { spec = NULL; return; }
    if ((double)(SDL_GetTicksNS() - clock_start_ns) / 1e9 < at) return;
    const char *p = end + 1;
    u16 keys[8];
    int n = 0;
    bool press = true, release = true;
    while (n < 8) {
        keys[n++] = (u16)SDL_strtoul(p, &end, 16);
        p = end;
        if (*p == 'p') { release = false; p++; }        /* held down */
        else if (*p == 'r') { press = false; p++; }      /* let go */
        if (*p != '+') break;
        p++;
    }
    if (host_trace()) fprintf(stderr, "[host] scripted key %04X at %.2f s\n", keys[0], at);
    if (press)
        for (int i = 0; i < n; i++) {
            if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
            kbd_handler((u8)keys[i]);
        }
    if (release)
        for (int i = n - 1; i >= 0; i--) {
            if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
            kbd_handler((u8)(keys[i] | 0x80));
        }
    spec = *p == ',' ? p + 1 : NULL;
}

/* ---------------------------------------------------------------- scripted clicks (AOTP_CLICKS) */

/* ENH (developer aid): "<seconds>:<x>:<y>,..." moves the pointer to (x, y) of the 320x200 screen and
 * clicks the left button (held 0.2 s). */
static void scripted_clicks(void)
{
    static const char *spec;
    static bool checked;
    static double release_at = -1;
    if (!checked) { spec = SDL_getenv("AOTP_CLICKS"); checked = true; }
    double now = (double)(SDL_GetTicksNS() - clock_start_ns) / 1e9;
    if (release_at >= 0 && now >= release_at) {
        mouse_buttons &= (u8)~1;
        mouse_changed = true;
        release_at = -1;
    }
    if (!spec || !*spec || release_at >= 0) return;
    char *end;
    double at = SDL_strtod(spec, &end);
    if (end == spec || *end != ':') { spec = NULL; return; }
    if (now < at) return;
    long x = SDL_strtol(end + 1, &end, 10);
    if (*end != ':') { spec = NULL; return; }
    long y = SDL_strtol(end + 1, &end, 10);
    host_mouse_warp((int)x, (int)y);
    mouse_buttons |= 1;
    mouse_changed = true;
    release_at = now + 0.2;
    if (host_trace()) fprintf(stderr, "[host] scripted click %ld,%ld at %.2f s\n", x, y, at);
    spec = *end == ',' ? end + 1 : NULL;
}

/* ---------------------------------------------------------------- pump */

void host_pump(void)
{
    process_events();
    scripted_keys();
    scripted_clicks();                              /* ENH: developer aid */
    if (mouse_changed && mouse_handler) {
        mouse_changed = false;
        int x, y;
        u8 b;
        host_mouse_read(&x, &y, &b);
        mouse_handler(x, y, b);
    }

    bool worked = false;
    Uint64 now = SDL_GetTicksNS();
    int budget = 118;                               /* at most 0.5 s of catch-up per call */
    while (tick_due_ns(ticks_run + 1) <= now && budget-- > 0) {
        ticks_run++;
        if (tick_handler) tick_handler();
        worked = true;
    }
    if (budget < 0)                                 /* fell too far behind: resynchronise the clock */
        clock_start_ns = now - (tick_due_ns(ticks_run) - clock_start_ns);

    /* Present at most once per ~8 ms; VSync paces it further. */
    if (frame_source && now - last_present_ns >= 8 * SDL_NS_PER_MS) {
        if (!frame_source(frame)) snapshot_static();
        else {
            present();
            last_present_ns = SDL_GetTicksNS();
            worked = true;
        }
    }
    if (!worked) {
        Uint64 next = tick_due_ns(ticks_run + 1);
        now = SDL_GetTicksNS();
        if (next > now) SDL_DelayNS(SDL_min(next - now, SDL_NS_PER_MS));
    }
}

void host_wait_until_ns(uint64_t t)
{
    while (host_ns() < t) host_pump();
}

void host_set_frame_ticks(int ticks) { frame_ticks = ticks < 1 ? 1 : ticks; }
int  host_frame_ticks(void) { return frame_ticks; }

/* ---------------------------------------------------------------- keyboard */

/* XT set-1 make code of an SDL key; bit 8 set = grey key sent with an E0 prefix. 0 = not reported. */
#define GREY 0x100
static u16 xt_scan(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const u8 letter_scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        return letter_scan[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return (u16)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (u16)(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_F11:          return 0x57;
    case SDL_SCANCODE_F12:          return 0x58;
    case SDL_SCANCODE_ESCAPE:       return 0x01;
    case SDL_SCANCODE_MINUS:        return 0x0C;
    case SDL_SCANCODE_EQUALS:       return 0x0D;
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;
    case SDL_SCANCODE_TAB:          return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:       return 0x1C;
    case SDL_SCANCODE_KP_ENTER:     return GREY | 0x1C;
    case SDL_SCANCODE_LCTRL:        return 0x1D;
    case SDL_SCANCODE_RCTRL:        return GREY | 0x1D;
    case SDL_SCANCODE_SEMICOLON:    return 0x27;
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;
    case SDL_SCANCODE_GRAVE:        return 0x29;
    case SDL_SCANCODE_LSHIFT:       return 0x2A;
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;
    case SDL_SCANCODE_COMMA:        return 0x33;
    case SDL_SCANCODE_PERIOD:       return 0x34;
    case SDL_SCANCODE_SLASH:        return 0x35;
    case SDL_SCANCODE_KP_DIVIDE:    return GREY | 0x35;
    case SDL_SCANCODE_RSHIFT:       return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
    case SDL_SCANCODE_LALT:         return 0x38;
    case SDL_SCANCODE_RALT:         return GREY | 0x38;
    case SDL_SCANCODE_SPACE:        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:   return 0x46;
    case SDL_SCANCODE_KP_7:         return 0x47;
    case SDL_SCANCODE_KP_8:         return 0x48;
    case SDL_SCANCODE_KP_9:         return 0x49;
    case SDL_SCANCODE_KP_MINUS:     return 0x4A;
    case SDL_SCANCODE_KP_4:         return 0x4B;
    case SDL_SCANCODE_KP_5:         return 0x4C;
    case SDL_SCANCODE_KP_6:         return 0x4D;
    case SDL_SCANCODE_KP_PLUS:      return 0x4E;
    case SDL_SCANCODE_KP_1:         return 0x4F;
    case SDL_SCANCODE_KP_2:         return 0x50;
    case SDL_SCANCODE_KP_3:         return 0x51;
    case SDL_SCANCODE_KP_0:         return 0x52;
    case SDL_SCANCODE_KP_PERIOD:    return 0x53;
    case SDL_SCANCODE_HOME:         return GREY | 0x47;
    case SDL_SCANCODE_UP:           return GREY | 0x48;
    case SDL_SCANCODE_PAGEUP:       return GREY | 0x49;
    case SDL_SCANCODE_LEFT:         return GREY | 0x4B;
    case SDL_SCANCODE_RIGHT:        return GREY | 0x4D;
    case SDL_SCANCODE_END:          return GREY | 0x4F;
    case SDL_SCANCODE_DOWN:         return GREY | 0x50;
    case SDL_SCANCODE_PAGEDOWN:     return GREY | 0x51;
    case SDL_SCANCODE_INSERT:       return GREY | 0x52;
    case SDL_SCANCODE_DELETE:       return GREY | 0x53;
    default:                        return 0;
    }
}

static void feed(const u8 *bytes, int n)
{
    if (!kbd_handler) return;
    for (int i = 0; i < n; i++) kbd_handler(bytes[i]);
}

static void key_event(SDL_Scancode sc, bool down)
{
    if (sc == SDL_SCANCODE_PAUSE) {                 /* make only, no break sequence */
        static const u8 pause[] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
        if (down) feed(pause, sizeof pause);
        return;
    }
    if (sc == SDL_SCANCODE_PRINTSCREEN) {
        static const u8 make[] = { 0xE0, 0x2A, 0xE0, 0x37 }, brk[] = { 0xE0, 0xB7, 0xE0, 0xAA };
        if (down) feed(make, sizeof make); else feed(brk, sizeof brk);
        return;
    }
    u16 x = xt_scan(sc);
    if (!x) return;
    u16 mapped[16];
    int m = enh_key_map(x, down, mapped);          /* ENH: key bindings */
    if (m >= 0) {
        for (int i = 0; i < m; i++) {
            u8 seq[2];
            int n = 0;
            if (mapped[i] & GREY) seq[n++] = 0xE0;
            seq[n++] = (u8)mapped[i];
            feed(seq, n);
        }
        return;
    }
    u8 seq[2];
    int n = 0;
    if (x & GREY) seq[n++] = 0xE0;
    seq[n++] = (u8)(down ? (x & 0x7F) : (x & 0x7F) | 0x80);
    feed(seq, n);
}

static void process_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            host_shutdown();
            exit(0);
        case SDL_EVENT_KEY_DOWN:
            /* Alt+Enter toggles fullscreen; everything else goes to the game, repeats included. */
            if (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                if (!ev.key.repeat)
                    SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                break;
            }
            key_event(ev.key.scancode, true);
            break;
        case SDL_EVENT_KEY_UP:
            key_event(ev.key.scancode, false);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (focus_lost_handler) focus_lost_handler();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(renderer, &ev);
            mouse_x = ev.motion.x;
            mouse_y = ev.motion.y;
            mouse_changed = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            u8 bit = ev.button.button == SDL_BUTTON_LEFT ? 1 : ev.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_buttons |= bit; else mouse_buttons &= (u8)~bit;
            mouse_changed = true;
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) gamepad = SDL_OpenGamepad(ev.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad && SDL_GetGamepadID(gamepad) == ev.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = NULL;
            }
            break;
        default:
            break;
        }
    }
}

/* ---------------------------------------------------------------- mouse, joystick */

void host_mouse_read(int *x, int *y, u8 *buttons)
{
    /* ENH: window-logical units -> frame pixels -> the 320x200 screen inside the frame */
    float fx = SDL_clamp(mouse_x, 0.0f, (float)view_w() - 1) * frame_w / view_w();
    float fy = SDL_clamp(mouse_y, 0.0f, (float)view_h() - 1) * frame_h / view_h();
    int sx = (int)((fx - scr_x0) * 320 / scr_w), sy = (int)((fy - scr_y0) * 200 / scr_h);
    if (x) *x = SDL_clamp(sx, 0, 319);
    if (y) *y = SDL_clamp(sy, 0, 199);
    if (buttons) *buttons = mouse_buttons;
}

void host_mouse_warp(int x, int y)
{
    mouse_x = ((float)x * scr_w / 320 + scr_x0) * view_w() / frame_w;   /* ENH: screen rect */
    mouse_y = ((float)y * scr_h / 200 + scr_y0) * view_h() / frame_h;
    if (window && !headless) {
        float wx, wy;
        if (SDL_RenderCoordinatesToWindow(renderer, mouse_x, mouse_y, &wx, &wy))
            SDL_WarpMouseInWindow(window, wx, wy);
    }
}

bool host_joy_present(void) { return gamepad != NULL; }

void host_joy_axes(s16 axes[4])
{
    for (int i = 0; i < 4; i++) axes[i] = 0;
    if (!gamepad) return;
    axes[0] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    axes[1] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    axes[2] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    axes[3] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  axes[0] = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) axes[0] = 32767;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    axes[1] = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  axes[1] = 32767;
}

u8 host_joy_buttons(void)
{
    if (!gamepad) return 0;
    u8 b = 0;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 1;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST))  b |= 2;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST))  b |= 4;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH)) b |= 8;
    return b;
}

/* ---------------------------------------------------------------- ENH: parallel loops */

#define PAR_MAX_WORKERS 15
static SDL_Thread *par_thread[PAR_MAX_WORKERS];
static int par_workers = -1;
static SDL_Mutex *par_mutex;
static SDL_Condition *par_go, *par_done;
static u32 par_gen;                              /* job generation, under par_mutex */
static int par_busy;                             /* workers still inside the current job */
static SDL_AtomicInt par_next;
static int par_n;
static void (*par_fn)(int, void *);
static void *par_ctx;

int host_cpu_count(void)
{
    int n = SDL_GetNumLogicalCPUCores();
    return n < 1 ? 1 : n;
}

static void par_run(void)
{
    for (;;) {
        int i = SDL_AddAtomicInt(&par_next, 1);
        if (i >= par_n) return;
        par_fn(i, par_ctx);
    }
}

static int SDLCALL par_worker(void *arg)
{
    (void)arg;
    u32 seen = 0;
    for (;;) {
        SDL_LockMutex(par_mutex);
        while (par_gen == seen) SDL_WaitCondition(par_go, par_mutex);
        seen = par_gen;
        SDL_UnlockMutex(par_mutex);
        par_run();
        SDL_LockMutex(par_mutex);
        if (--par_busy == 0) SDL_SignalCondition(par_done);
        SDL_UnlockMutex(par_mutex);
    }
    return 0;
}

void host_parallel(int n, void (*fn)(int i, void *ctx), void *ctx)
{
    if (par_workers < 0) {
        par_workers = 0;
        int w = host_cpu_count() - 1;
        if (w > PAR_MAX_WORKERS) w = PAR_MAX_WORKERS;
        par_mutex = SDL_CreateMutex();
        par_go = SDL_CreateCondition();
        par_done = SDL_CreateCondition();
        if (par_mutex && par_go && par_done)
            for (int i = 0; i < w; i++) {
                par_thread[i] = SDL_CreateThread(par_worker, "enh-render", NULL);
                if (!par_thread[i]) break;
                SDL_DetachThread(par_thread[i]);
                par_workers++;
            }
    }
    if (par_workers == 0 || n <= 1) {
        for (int i = 0; i < n; i++) fn(i, ctx);
        return;
    }
    par_fn = fn; par_ctx = ctx; par_n = n;
    SDL_SetAtomicInt(&par_next, 0);
    SDL_LockMutex(par_mutex);
    par_busy = par_workers;
    par_gen++;
    SDL_BroadcastCondition(par_go);
    SDL_UnlockMutex(par_mutex);
    par_run();
    SDL_LockMutex(par_mutex);
    while (par_busy > 0) SDL_WaitCondition(par_done, par_mutex);
    SDL_UnlockMutex(par_mutex);
}

/* ---------------------------------------------------------------- audio */

void host_audio_lock(void) { if (audio_mutex) SDL_LockMutex(audio_mutex); }
void host_audio_unlock(void) { if (audio_mutex) SDL_UnlockMutex(audio_mutex); }

void host_opl_write(u8 reg, u8 value)
{
    host_audio_lock();
    if (reg == 0x04) {                               /* timer control: model the status flags only */
        if (value & 0x80) opl_status = 0;
        else {
            if ((value & 0x01) && !(value & 0x40)) opl_status |= 0xC0;   /* timer 1 expires at once */
            if ((value & 0x02) && !(value & 0x20)) opl_status |= 0xA0;   /* timer 2 */
        }
    }
    OPL3_WriteReg(&opl, reg, value);
    host_audio_unlock();
}

u8 host_opl_status(void) { return opl_status; }

void host_set_sound_service(void (*service)(void))
{
    host_audio_lock();
    sound_service = service;
    svc_acc = 0;
    host_audio_unlock();
}

/* ---------------------------------------------------------------- files, errors */

char *host_game_path(const char *name, bool create)
{
    char *direct = NULL;
    SDL_asprintf(&direct, "%s/%s", game_dir, name);
    if (SDL_GetPathInfo(direct, NULL)) return direct;

    int count = 0;
    char **entries = SDL_GlobDirectory(game_dir, NULL, 0, &count);
    char *found = NULL;
    for (int i = 0; entries && i < count; i++) {
        if (SDL_strcasecmp(entries[i], name) == 0) {
            SDL_asprintf(&found, "%s/%s", game_dir, entries[i]);
            break;
        }
    }
    SDL_free(entries);
    if (found) { SDL_free(direct); return found; }
    if (create) return direct;
    SDL_free(direct);
    return NULL;
}

const char *host_game_dir(void) { return game_dir; }

void host_free(void *p) { SDL_free(p); }

_Noreturn void host_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "fatal: %s\n", msg);
    host_set_sound_service(NULL);         /* the audio thread must not run the game's service any more */
    if (window && !headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Aces of the Pacific", msg, window);
    host_shutdown();
    exit(3);
}
