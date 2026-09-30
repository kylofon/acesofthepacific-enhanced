/* ENH: key bindings (the launcher's Game settings > Key bindings) and the flaps up / flaps down keys.
 *
 * Keys are XT set-1 make codes as host.c sends them (ENH_KEY_GREY = sent after an E0 prefix) with the
 * modifiers held with them (ENH_KEY_SHIFT / CTRL / ALT). While AP runs, a key bound to an action is sent
 * to the game as the action's own key (with the modifiers that key needs); a key that is the default of
 * an action bound elsewhere is dropped; every other key goes through as it is. INTRO and PS (menus,
 * typed names) always get the keys as they are. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enhanced.h"
#include "prog.h"

#define SHIFT ENH_KEY_SHIFT
#define CTRL  ENH_KEY_CTRL
#define ALT   ENH_KEY_ALT
#define GREY  ENH_KEY_GREY
#define MODS  (SHIFT | CTRL | ALT)

/* The launcher's list (launcher/keys.cpp): the same names and defaults. */
typedef struct {
    const char *name;
    u16 def;          /* the key it has by default */
    u16 game;         /* what the game is sent */
    u16 key;          /* the key it has now (0 = none) */
} Action;

static Action actions[] = {
    { "stick_forward",   GREY | 0x48,  GREY | 0x48,         0 },
    { "stick_back",      GREY | 0x50,  GREY | 0x50,         0 },
    { "stick_left",      GREY | 0x4B,  GREY | 0x4B,         0 },
    { "stick_right",     GREY | 0x4D,  GREY | 0x4D,         0 },
    { "rudder_left",     0x33,         0x33,                0 },
    { "rudder_right",    0x34,         0x34,                0 },
    { "throttle_down",   0x0C,         0x0C,                0 },
    { "throttle_up",     0x0D,         0x0D,                0 },
    { "throttle_full",   0x37,         0x37,                0 },
    { "flaps_down",      0x21,         ENH_KEY_FLAPS_DOWN,  0 },
    { "flaps_up",        SHIFT | 0x21, ENH_KEY_FLAPS_UP,    0 },
    { "gear",            0x26,         0x26,                0 },
    { "dive_brakes",     0x30,         0x30,                0 },
    { "wheel_brakes",    0x11,         0x11,                0 },
    { "autopilot",       0x1E,         0x1E,                0 },
    { "guns",            0x39,         0x39,                0 },
    { "bombs",           0x0E,         0x0E,                0 },
    { "rockets",         0x13,         0x13,                0 },
    { "weapon_station",  0x22,         0x22,                0 },
    { "drop_tanks",      0x20,         0x20,                0 },
    { "unjam",           0x16,         0x16,                0 },
    { "bail_out",        CTRL | 0x30,  CTRL | 0x30,         0 },
    { "radio",           0x1F,         0x1F,                0 },
    { "chase_view",      0x41,         0x41,                0 },
    { "weapon_camera",   0x42,         0x42,                0 },
    { "zoom_out",        0x1A,         0x1A,                0 },
    { "zoom_in",         0x1B,         0x1B,                0 },
    { "map",             0x32,         0x32,                0 },
    { "pause",           0x19,         0x19,                0 },
    { "preferences",     0x44,         0x44,                0 },
    { "realism",         ALT | 0x13,   ALT | 0x13,          0 },
    { "sound",           ALT | 0x1F,   ALT | 0x1F,          0 },
    { "joystick",        ALT | 0x24,   ALT | 0x24,          0 },
    { "mouse",           ALT | 0x20,   ALT | 0x20,          0 },
    { "quit",            ALT | 0x10,   ALT | 0x10,          0 },
};
#define NACTIONS ((int)(sizeof actions / sizeof actions[0]))

static u16 mods;               /* the modifiers held now (every modifier key goes through) */
static u8 held_shift, held_ctrl, held_alt;   /* bit 0 = left, bit 1 = right */
/* per physical key (x & 0x1FF) while it is down: the game key sent for it, PASS = the key itself,
 * DROP = nothing; NONE = up, or down since before AP */
#define NONE 0xFFFF
#define PASS 0xFFFE
#define DROP 0
static u16 sent[0x200];

static void init_once(void)
{
    static bool done;
    if (done) return;
    done = true;
    for (int i = 0; i < NACTIONS; i++) actions[i].key = actions[i].def;
    for (int i = 0; i < 0x200; i++) sent[i] = NONE;
}

bool enh_keys_parse(const char *spec)
{
    init_once();
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *item = strtok(buf, ","); item; item = strtok(NULL, ",")) {
        char *eq = strchr(item, '=');
        if (!eq) return false;
        *eq = 0;
        char *end;
        unsigned long code = strtoul(eq + 1, &end, 0);
        if (*end || code > 0xFFFF || (code & ~(u16)(MODS | GREY | 0x7F))) return false;
        int i = 0;
        while (i < NACTIONS && strcmp(actions[i].name, item)) i++;
        if (i == NACTIONS) return false;
        actions[i].key = (u16)code;
    }
    return true;
}

static int push(u16 *out, int n, u16 key, bool down)
{
    out[n] = (u16)((key & (GREY | 0x7F)) | (down ? 0 : 0x80));
    return n + 1;
}

/* Keeps track of the modifiers; returns true for a modifier key. */
static bool modifier(u16 x, bool down)
{
    u8 *held = NULL, side = (x & GREY) ? 2 : 1;
    switch (x) {
    case 0x2A: held = &held_shift; side = 1; break;
    case 0x36: held = &held_shift; side = 2; break;
    case 0x1D: case GREY | 0x1D: held = &held_ctrl; break;
    case 0x38: case GREY | 0x38: held = &held_alt; break;
    default: return false;
    }
    if (down) *held |= side; else *held &= (u8)~side;
    mods = (u16)((held_shift ? SHIFT : 0) | (held_ctrl ? CTRL : 0) | (held_alt ? ALT : 0));
    return true;
}

/* The key `game` pressed with exactly its own modifiers: the ones held that it doesn't want are let
 * go around it, the ones it wants that aren't held are pressed around it (the game reads the
 * modifiers when the key goes down). */
static int press(u16 *out, u16 game)
{
    const u16 want = game & MODS;
    int n = 0;
    static const struct { u16 bit; u16 left, right; const u8 *held; } M[] = {
        { SHIFT, 0x2A, 0x36,        &held_shift },
        { CTRL,  0x1D, GREY | 0x1D, &held_ctrl },
        { ALT,   0x38, GREY | 0x38, &held_alt },
    };
    for (int i = 0; i < 3; i++) {
        if ((want & M[i].bit) && !*M[i].held) n = push(out, n, M[i].left, true);
        if (!(want & M[i].bit) && (*M[i].held & 1)) n = push(out, n, M[i].left, false);
        if (!(want & M[i].bit) && (*M[i].held & 2)) n = push(out, n, M[i].right, false);
    }
    n = push(out, n, game, true);
    for (int i = 0; i < 3; i++) {
        if ((want & M[i].bit) && !*M[i].held) n = push(out, n, M[i].left, false);
        if (!(want & M[i].bit) && (*M[i].held & 1)) n = push(out, n, M[i].left, true);
        if (!(want & M[i].bit) && (*M[i].held & 2)) n = push(out, n, M[i].right, true);
    }
    return n;
}

int enh_key_map(u16 x, bool down, u16 *out)
{
    init_once();
    if (!enh_enabled()) return -1;
    if (modifier(x, down)) return -1;
    u16 *s = &sent[x & 0x1FF];
    if (!down) {                                   /* the key goes up: as it went down */
        u16 was = *s;
        *s = NONE;
        if (was == NONE || was == PASS) return -1;
        return was == DROP ? 0 : push(out, 0, was, false);
    }
    if (*s == PASS) return -1;                     /* typematic repeat */
    if (*s != NONE && *s != DROP) return press(out, *s);
    if (prog_current_id() != PROG_AP) return -1;
    const u16 key = (u16)(x | mods);
    for (int i = 0; i < NACTIONS; i++)
        if (actions[i].key == key) {
            *s = actions[i].game;
            return press(out, actions[i].game);
        }
    for (int i = 0; i < NACTIONS; i++)
        if (actions[i].def == key && actions[i].key != key) {
            *s = DROP;                             /* its action moved to another key */
            return 0;
        }
    *s = PASS;
    return -1;
}

void enh_keys_focus_lost(void)
{
    init_once();
    held_shift = held_ctrl = held_alt = 0;
    mods = 0;
    for (int i = 0; i < 0x200; i++) sent[i] = NONE;
}
