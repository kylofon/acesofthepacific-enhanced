// keys.h -- the game's keys that can be changed (Game settings > Key bindings), kept in settings.ini and
// handed to the game with --keys.
//
// A key is an XT make code (the PC keyboard's own code, as the game reads it), GREY for the keys sent
// with an E0 prefix (arrows, Home, Right Ctrl...), with the modifiers held with it. The game's list
// (src/enhanced/enh_keys.c) has the same names and defaults.
#pragma once

#include <wx/string.h>

#include <vector>

class wxKeyEvent;
class wxWindow;

namespace keys {

enum : int { GREY = 0x100, SHIFT = 0x1000, CTRL = 0x2000, ALT = 0x4000 };

enum Group { FLYING, WEAPONS, VIEWS, GROUP_COUNT };

struct Action {
    const char* name;   // in settings.ini and on the game's command line
    const char* label;
    int def;            // the default key
    Group group;
};

extern const Action ACTIONS[];
extern const int ACTION_COUNT;
extern const char* const GROUP_TITLES[GROUP_COUNT];

// Keys that keep their job and can't be changed: {keys, what they do}.
struct FixedKey {
    const char* keys;
    const char* what;
};
extern const FixedKey FIXED[];
extern const int FIXED_COUNT;

// The key of each action, in the order of ACTIONS; 0 = none.
using Bindings = std::vector<int>;

Bindings Defaults();
Bindings Load();
void Save(const Bindings& bindings);

// "Shift+F", "Up"; "(none)" for 0.
wxString Name(int key);

// The --keys argument: the actions not on their default keys, "name=0x21,..."; empty if none.
wxString Argument(const Bindings& bindings);

// Index of the action called `name`, or -1.
int Find(const char* name);

// Asks for a new key for `action`. Returns the key, 0 for "no key", or -1 if cancelled.
int AskKey(wxWindow* parent, const wxString& action);

}  // namespace keys
