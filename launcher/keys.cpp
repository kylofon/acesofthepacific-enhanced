// keys.cpp -- the game's keys, their names and the "Press new key" prompt.
#include "keys.h"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>

#include "settings.h"

namespace keys {

const Action ACTIONS[] = {
    {"stick_forward", "Stick forward (nose down)", GREY | 0x48, FLYING},
    {"stick_back", "Stick back (nose up)", GREY | 0x50, FLYING},
    {"stick_left", "Stick left", GREY | 0x4B, FLYING},
    {"stick_right", "Stick right", GREY | 0x4D, FLYING},
    {"rudder_left", "Rudder left", 0x33, FLYING},
    {"rudder_right", "Rudder right", 0x34, FLYING},
    {"throttle_down", "Throttle down", 0x0C, FLYING},
    {"throttle_up", "Throttle up", 0x0D, FLYING},
    {"throttle_full", "Full throttle", 0x37, FLYING},
    {"flaps_down", "Flaps down", 0x21, FLYING},
    {"flaps_up", "Flaps up", SHIFT | 0x21, FLYING},
    {"gear", "Landing gear", 0x26, FLYING},
    {"dive_brakes", "Dive brakes", 0x30, FLYING},
    {"wheel_brakes", "Wheel brakes", 0x11, FLYING},
    {"autopilot", "Autopilot / skip ahead", 0x1E, FLYING},
    {"guns", "Guns", 0x39, WEAPONS},
    {"bombs", "Bombs / torpedo", 0x0E, WEAPONS},
    {"rockets", "Rockets", 0x13, WEAPONS},
    {"weapon_station", "Select weapon station", 0x22, WEAPONS},
    {"drop_tanks", "Drop tanks", 0x20, WEAPONS},
    {"unjam", "Unjam guns", 0x16, WEAPONS},
    {"bail_out", "Bail out", CTRL | 0x30, WEAPONS},
    {"radio", "Radio (then F1 - F7)", 0x1F, VIEWS},
    {"chase_view", "Chase view", 0x41, VIEWS},
    {"weapon_camera", "Weapon camera", 0x42, VIEWS},
    {"zoom_out", "Zoom out (outside views)", 0x1A, VIEWS},
    {"zoom_in", "Zoom in (outside views)", 0x1B, VIEWS},
    {"map", "Map", 0x32, VIEWS},
    {"pause", "Pause", 0x19, VIEWS},
    {"preferences", "Preferences", 0x44, VIEWS},
    {"realism", "Realism", ALT | 0x13, VIEWS},
    {"sound", "Sound on / off", ALT | 0x1F, VIEWS},
    {"joystick", "Joystick on / off", ALT | 0x24, VIEWS},
    {"mouse", "Mouse on / off", ALT | 0x20, VIEWS},
    {"quit", "Quit the game", ALT | 0x10, VIEWS},
};
const int ACTION_COUNT = static_cast<int>(sizeof ACTIONS / sizeof ACTIONS[0]);

const char* const GROUP_TITLES[GROUP_COUNT] = {"Flying", "Weapons", "Views and game"};

const FixedKey FIXED[] = {
    {"1 - 9", "throttle settings"},
    {"Keypad", "stick, diagonals too"},
    {"F1 - F6", "cockpit views; outside views after Enter"},
    {"Enter + stick keys", "look around"},
    {"Ctrl / Alt+F1 - F10", "save / recall a view"},
    {"Esc", "end the mission"},
    {"Alt+Enter", "full screen"},
};
const int FIXED_COUNT = static_cast<int>(sizeof FIXED / sizeof FIXED[0]);

namespace {

const char* const SECTION = "Keys";

// Names of the XT make codes, and of the E0 ones.
const char* const NAMES[0x59] = {
    nullptr, "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Ctrl", "A", "S",
    "D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "Shift", "\\", "Z", "X", "C", "V",
    "B", "N", "M", ",", ".", "/", "Right Shift", "Keypad *", "Alt", "Space", "Caps Lock", "F1", "F2", "F3",
    "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Num Lock", "Scroll Lock", "Keypad 7", "Keypad 8", "Keypad 9",
    "Keypad -", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad +", "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 0",
    "Keypad .", nullptr, nullptr, nullptr, "F11", "F12",
};

const char* GreyName(int code) {
    switch (code) {
    case 0x1C: return "Keypad Enter";
    case 0x1D: return "Right Ctrl";
    case 0x35: return "Keypad /";
    case 0x38: return "Right Alt";
    case 0x47: return "Home";
    case 0x48: return "Up";
    case 0x49: return "Page Up";
    case 0x4B: return "Left";
    case 0x4D: return "Right";
    case 0x4F: return "End";
    case 0x50: return "Down";
    case 0x51: return "Page Down";
    case 0x52: return "Insert";
    case 0x53: return "Delete";
    default: return nullptr;
    }
}

const char* BaseName(int key) {
    const int code = key & 0x7F;
    if (key & GREY) return GreyName(code);
    return code < 0x59 ? NAMES[code] : nullptr;
}

// Why `key` can't be given to an action, or empty if it can.
wxString Reserved(int key) {
    const int code = key & 0x7F;
    const bool grey = (key & GREY) != 0;
    switch (code) {
    case 0x1D: case 0x2A: case 0x36: case 0x38:
        return "Shift, Ctrl and Alt can only be held with another key.";
    case 0x3A: case 0x45: case 0x46:
        return "The lock keys can't be used.";
    case 0x01: return "Esc keeps its job in the game (end the mission, cancel).";
    case 0x0F: return "Tab keeps its job in the game.";
    case 0x1C: return "Enter keeps its job in the game (look around, OK).";
    default: break;
    }
    if (!grey && code >= 0x02 && code <= 0x0B) return "The number keys keep their job: throttle settings.";
    if (!grey && code >= 0x3B && code <= 0x40) return "F1 - F6 keep their job: cockpit and outside views.";
    if (!grey && code >= 0x3B && code <= 0x44 && (key & (CTRL | ALT)))
        return "Ctrl and Alt with F1 - F10 keep their job: saved views.";
    return wxString();
}

// The XT key of a key press, 0 if the game can't be sent it, -1 for a modifier alone.
int FromEvent(const wxKeyEvent& event) {
    switch (event.GetKeyCode()) {
    case WXK_SHIFT: case WXK_CONTROL: case WXK_ALT: case WXK_WINDOWS_LEFT: case WXK_WINDOWS_RIGHT:
    case WXK_WINDOWS_MENU:
#ifdef __WXOSX__
    case WXK_RAW_CONTROL:
#endif
        return -1;
    case WXK_PAUSE: case WXK_PRINT: case WXK_SNAPSHOT:
        return 0;
    case WXK_NUMLOCK:
        return 0x45;
    default:
        break;
    }
    int key = 0;
#ifdef __WXMSW__
    // The scan code and the extended-key flag of WM_KEYDOWN.
    const wxUint32 flags = event.GetRawKeyFlags();
    key = static_cast<int>((flags >> 16) & 0xFF) | ((flags >> 24) & 1 ? GREY : 0);
#elif defined(__WXGTK__)
    // X keycodes are Linux evdev codes + 8; those below 0x59 are the XT codes.
    const int evdev = static_cast<int>(event.GetRawKeyFlags()) - 8;
    switch (evdev) {
    case 96: key = GREY | 0x1C; break;
    case 97: key = GREY | 0x1D; break;
    case 98: key = GREY | 0x35; break;
    case 100: key = GREY | 0x38; break;
    case 102: key = GREY | 0x47; break;
    case 103: key = GREY | 0x48; break;
    case 104: key = GREY | 0x49; break;
    case 105: key = GREY | 0x4B; break;
    case 106: key = GREY | 0x4D; break;
    case 107: key = GREY | 0x4F; break;
    case 108: key = GREY | 0x50; break;
    case 109: key = GREY | 0x51; break;
    case 110: key = GREY | 0x52; break;
    case 111: key = GREY | 0x53; break;
    default: key = evdev > 0 && evdev < 0x59 ? evdev : 0; break;
    }
#endif
    if (!BaseName(key)) return 0;
    if (event.ShiftDown()) key |= SHIFT;
    if (event.ControlDown()) key |= CTRL;
    if (event.AltDown()) key |= ALT;
    return key;
}

}  // namespace

Bindings Defaults() {
    Bindings b;
    for (int i = 0; i < ACTION_COUNT; ++i) b.push_back(ACTIONS[i].def);
    return b;
}

Bindings Load() {
    Bindings b;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const int key = settings::GetInt(SECTION, ACTIONS[i].name, ACTIONS[i].def);
        b.push_back(key == 0 || (BaseName(key) && Reserved(key).empty()) ? key : ACTIONS[i].def);
    }
    return b;
}

void Save(const Bindings& bindings) {
    for (int i = 0; i < ACTION_COUNT; ++i) settings::SetInt(SECTION, ACTIONS[i].name, bindings[i]);
}

wxString Name(int key) {
    const char* base = BaseName(key);
    if (!base) return "(none)";
    wxString name;
    if (key & CTRL) name += "Ctrl+";
    if (key & ALT) name += "Alt+";
    if (key & SHIFT) name += "Shift+";
    return name + base;
}

wxString Argument(const Bindings& bindings) {
    wxString arg;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (bindings[i] == ACTIONS[i].def) continue;
        if (!arg.empty()) arg += ",";
        arg += wxString::Format("%s=0x%X", ACTIONS[i].name, bindings[i]);
    }
    return arg;
}

int Find(const char* name) {
    for (int i = 0; i < ACTION_COUNT; ++i)
        if (wxStrcmp(ACTIONS[i].name, name) == 0) return i;
    return -1;
}

int AskKey(wxWindow* parent, const wxString& action) {
    wxDialog dialog(parent, wxID_ANY, "Press new key");
    const int pad = dialog.FromDIP(12);
    auto* all = new wxBoxSizer(wxVERTICAL);
    auto* prompt = new wxStaticText(&dialog, wxID_ANY, "Press the new key for " + action + ".");
    prompt->SetFont(dialog.GetFont().Bold());
    all->Add(prompt, 0, wxLEFT | wxRIGHT | wxTOP, pad);
    auto* hint = new wxStaticText(&dialog, wxID_ANY, "Shift, Ctrl or Alt can be held with it. Esc cancels.");
    hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    all->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(6));
    auto* problem = new wxStaticText(&dialog, wxID_ANY, wxEmptyString);
    problem->SetForegroundColour(*wxRED);
    all->Add(problem, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(6));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* none = new wxButton(&dialog, wxID_ANY, "&No key");
    none->SetToolTip("Leave this action without a key.");
    buttons->Add(none);
    buttons->AddStretchSpacer();
    buttons->Add(new wxButton(&dialog, wxID_CANCEL));
    all->Add(buttons, 0, wxEXPAND | wxALL, pad);

    int result = -1;
    none->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        result = 0;
        dialog.EndModal(wxID_OK);
    });
    // Every key comes here first, before the buttons or the dialog could act on it.
    dialog.Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE && !event.HasAnyModifiers()) {
            dialog.EndModal(wxID_CANCEL);
            return;
        }
        const int key = FromEvent(event);
        if (key < 0) return;  // a modifier alone: wait for the key
        const wxString why = key == 0 ? wxString("The game can't use that key.") : Reserved(key);
        if (!why.empty()) {
            problem->SetLabel((key ? Name(key) + ": " : wxString()) + why);
            dialog.Layout();
            dialog.Fit();
            return;
        }
        result = key;
        dialog.EndModal(wxID_OK);
    });

    dialog.SetSizerAndFit(all);
    dialog.SetSize(wxSize(std::max(dialog.GetSize().x, dialog.FromDIP(380)), -1));
    dialog.CentreOnParent();
    dialog.ShowModal();
    return result;
}

}  // namespace keys
