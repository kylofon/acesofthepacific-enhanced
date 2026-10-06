// launcher.cpp -- the launcher window.
#include "launcher.h"

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef __WXMSW__
#include <wx/msw/wrapcctl.h>
#include <shellapi.h>
#endif

#include "icon.h"
#include "settings.h"
#include "version.h"

const char* const APP_TITLE = "Aces of the Pacific Enhanced";

namespace {

const char* const WEBSITE = "https://kkania.com";
const char* const SUPPORT = "https://buymeacoffee.com/krzysztofkania";
const char* const SECTION = "Game";

const int ID_KEY_BINDINGS = wxID_HIGHEST + 1, ID_ABOUT = wxID_HIGHEST + 2, ID_GRAPHICS = wxID_HIGHEST + 3,
          ID_DEBUG = wxID_HIGHEST + 4;
// The book's pages.
const int PAGE_MAIN = 0, PAGE_KEYS = 1, PAGE_GRAPHICS = 2, PAGE_DEBUG = 3;
const char* const GRAPHICS = "Graphics";
const char* const DEBUG_SECTION = "Debug";
// Debug page limits (aotp --plane-size, --hitbox-size: src/enhanced/enhanced.h).
const int MIN_PLANE_SIZE = 10, MAX_PLANE_SIZE = 100, MIN_HITBOX_SIZE = 10, MAX_HITBOX_SIZE = 400;

// The ground dots' values on the Graphics page: the settings.ini key, the field, the limits and the
// slider's page step (as aotp's --dots-* options, src/enhanced/enhanced.h), and the row's texts.
struct DotValue {
    const char* key;
    int Graphics::*field;
    int min, max, step;
    const char* label;
    const char* note;
    const char* tip;
};
const DotValue DOT_VALUES[] = {
    {"GroundDotsRange", &Graphics::dotsRange, 1, 64, 4, "&Range:", "cells of 1024 (original about 4)",
     "How far the dots reach from the view, in grid cells (the dots are 1024 units apart). Beyond, only "
     "the faint speckle (Horizon speckle) goes on to the horizon."},
    {"GroundDotsFade", &Graphics::dotsFade, 0, 100, 10, "Fade &out from:", "% of the range",
     "The dots thin out from this share of the range to its edge, where none are left, so the field has no "
     "hard edge. 100 = no fade."},
    {"GroundDotsSpeckle", &Graphics::dotsSpeckle, 0, 40, 1, "&Horizon speckle:", "% of the ground (0 = none)",
     "Beyond the range, out to the horizon: a faint speckle in the dots' colours, fixed on the ground, over "
     "this share of it. It fades in as the dots fade out."},
    {"GroundDotsFarBrightness", &Graphics::dotsFarBright, 0, 100, 10, "Far &brightness:", "% of the dots' colour",
     "Farther away the dots fade into the ground under them; only some, the larger wave crests, stay lighter, "
     "as does the horizon speckle: they show this share of the dots' colour over the ground (100 = the full "
     "dot colour, lower = closer to the sea's own colour)."},
    {"GroundDotsFarDensity", &Graphics::dotsFar, 0, 3, 1, "Far densit&y:", "x2 steps at the edge (0 = even)",
     "The dots get denser with the distance, densest at the edge of the range: their spacing is halved this "
     "many times from the view out to the edge (1 = twice as close, 2 = four times, 3 = eight times). "
     "0 keeps them evenly spaced, as the original."},
    {"GroundDotsMaxCoverage", &Graphics::dotsCover, 1, 50, 1, "&Max coverage:", "% of the ground",
     "Where the dots would crowd together (far away, low over the ground), some are skipped so they cover "
     "at most this share of the ground. Lower = fewer dots in the distance."},
    {"GroundDotsAltitude", &Graphics::dotsAlt, 0, 30000, 1000, "Al&titude:", "highest (original 4000)",
     "The dots are shown while the view is no higher than this."},
    {"GroundDotsSize", &Graphics::dotsSize, 10, 400, 50, "Si&ze:", "% of an original pixel",
     "The size of a dot at the full-size distance and beyond: 100 is one pixel of the original 320x200 picture."},
    {"GroundDotsFullSizeFrom", &Graphics::dotsFull, 0, 16384, 1024, "&Full size from:", "units away",
     "Dots nearer than this are drawn smaller, the nearer the smaller. 0 = every dot has the full size."},
    {"GroundDotsNearSize", &Graphics::dotsNear, 5, 100, 10, "&Nearest size:", "% of the size",
     "The size of the nearest dots, in percent of the full size."},
    {"GroundDotsEdgeSize", &Graphics::dotsEdge, 5, 100, 5, "&Edge size:", "% of the size at the range's edge",
     "Beyond the full-size distance the dots get smaller again with the distance, down to this percent of "
     "the size at the edge of the range. 100 = they keep the full size."},
    {"GroundDotsScatter", &Graphics::dotsScatter, 0, 100, 10, "S&catter:", "% (0 = straight rows)",
     "Moves each dot off its place in the grid, by up to this percent of half a cell, so the dots don't line "
     "up in straight rows. Each dot keeps its place: they don't jump around."},
};
const int DOT_VALUE_COUNT = static_cast<int>(sizeof DOT_VALUES / sizeof DOT_VALUES[0]);

const int MIN_SCALE = 1, MAX_SCALE = 6, DEFAULT_SCALE = 3;
// The game's speed: timer ticks (236.7 a second) per simulator frame (aotp --frame-ticks).
const int MIN_TICKS = 4, MAX_TICKS = 40, DEFAULT_TICKS = 16;

#ifdef __WXMSW__
HRESULT CALLBACK AboutCallback(HWND hwnd, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(hwnd, L"open", reinterpret_cast<LPCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
#else
// A label and a link on one line, for the portable About box.
void AddLink(wxWindow* parent, wxSizer* sizer, const wxString& label, const wxString& text, const wxString& url) {
    auto* line = new wxBoxSizer(wxHORIZONTAL);
    line->Add(new wxStaticText(parent, wxID_ANY, label + " "), 0, wxALIGN_CENTER_VERTICAL);
    line->Add(new wxHyperlinkCtrl(parent, wxID_ANY, text, url), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(line);
}
#endif

wxStaticText* GreyText(wxWindow* parent, const wxString& text = wxEmptyString) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return label;
}

// A label, a text field and a Browse button in a row of a two-column grid.
wxTextCtrl* PathRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip,
                    wxButton** browse) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(300), -1));
    text->SetToolTip(tip);
    *browse = new wxButton(parent, wxID_ANY, "B&rowse...");
    row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    row->Add(*browse, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(row, 1, wxEXPAND);
    return text;
}

// A label, a slider, a number field and a note in a row of a four-column grid; the slider and the field
// follow each other.
void SliderRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& note,
               const wxString& tip, int min, int max, int step, wxSlider** slider, wxSpinCtrl** spin) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    *slider = new wxSlider(parent, wxID_ANY, min, min, max, wxDefaultPosition, wxSize(parent->FromDIP(220), -1));
    (*slider)->SetPageSize(step);
    (*slider)->SetLineSize(step > 10 ? step / 10 : 1);
    (*slider)->SetToolTip(tip);
    *spin = new wxSpinCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(80), -1),
                           wxSP_ARROW_KEYS, min, max, min);
    (*spin)->SetIncrement(step > 10 ? step / 10 : 1);
    (*spin)->SetToolTip(tip);
    grid->Add(*slider, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND);
    grid->Add(*spin, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(GreyText(parent, note), 0, wxALIGN_CENTER_VERTICAL);
    wxSlider* s = *slider;
    wxSpinCtrl* n = *spin;
    s->Bind(wxEVT_SLIDER, [s, n](wxCommandEvent&) { n->SetValue(s->GetValue()); });
    n->Bind(wxEVT_SPINCTRL, [s, n](wxSpinEvent&) { s->SetValue(n->GetValue()); });
    // A typed number moves the slider as it is typed (the field keeps it in range when it loses focus).
    n->Bind(wxEVT_TEXT, [s, n](wxCommandEvent& event) {
        long v;
        if (event.GetString().ToLong(&v) && v >= n->GetMin() && v <= n->GetMax()) s->SetValue(static_cast<int>(v));
        event.Skip();
    });
}

}  // namespace

LauncherFrame::LauncherFrame()
    : wxFrame(nullptr, wxID_ANY, APP_TITLE, wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)) {
    SetIcons(AppIcons());
    topmost_ = settings::GetInt("Preferences", "AlwaysOnTop", 0) != 0;
    if (topmost_) SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
    bindings_ = keys::Load();
    edited_ = bindings_;
    graphics_.smoothDots = settings::GetInt(GRAPHICS, "SmoothGroundDots", 1) != 0;
    for (const DotValue& d : DOT_VALUES) {
        int& v = graphics_.*d.field;
        v = wxMax(d.min, wxMin(d.max, settings::GetInt(GRAPHICS, d.key, v)));
    }
    const int planeSize = settings::GetInt(DEBUG_SECTION, "AircraftSize", debug_.planeSize);
    const int hitboxSize = settings::GetInt(DEBUG_SECTION, "HitBoxSize", debug_.hitboxSize);
    debug_.planeSize = wxMax(MIN_PLANE_SIZE, wxMin(MAX_PLANE_SIZE, planeSize));
    debug_.hitboxSize = wxMax(MIN_HITBOX_SIZE, wxMin(MAX_HITBOX_SIZE, hitboxSize));

    // The launcher and, in its place while one is open, a settings page (PAGE_*).
    book_ = new wxSimplebook(this);
    book_->AddPage(CreateMainPage(book_), "Game");
    book_->AddPage(CreateKeysPage(book_), "Key bindings");
    book_->AddPage(CreateGraphicsPage(book_), "Graphics");
    book_->AddPage(CreateDebugPage(book_), "Debug");
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(book_, 1, wxEXPAND);
    SetSizer(all);
    CreateMenus();

    // Settings from the last run.
    wxString dir = settings::GetString(SECTION, "GameFolder", "");
    wxString program = settings::GetString(SECTION, "Program", "");
    folder_->ChangeValue(dir.empty() ? DefaultGameDir() : dir);
    program_->ChangeValue(program.empty() ? DefaultProgram() : program);
    speed_->SetValue(wxMax(MIN_TICKS, wxMin(MAX_TICKS, settings::GetInt(SECTION, "FrameTicks", DEFAULT_TICKS))));
    aspect_->SetSelection(settings::GetInt(SECTION, "Aspect43", 1) != 0 ? 0 : 1);
    FillScales();
    scale_->SetSelection(
        wxMax(MIN_SCALE, wxMin(MAX_SCALE, settings::GetInt(SECTION, "Scale", DEFAULT_SCALE))) - MIN_SCALE);
    fullscreen_->SetValue(settings::GetInt(SECTION, "Fullscreen", 0) != 0);
    sound_->SetValue(settings::GetInt(SECTION, "Sound", 1) != 0);
    ShowEditedKeys();
    ShowGraphics(graphics_);
    ShowDebug(debug_);
    loading_ = false;
    UpdateState();

    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (event.GetActive()) UpdateState();  // files may have been copied in meanwhile
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        Save();
        Destroy();
    });

    FitPage();
    if (!settings::RestoreWindowPosition(SECTION, this)) Centre();
}

wxWindow* LauncherFrame::CreateMainPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    // Game files
    auto* filesBox = new wxStaticBoxSizer(wxVERTICAL, page, "Game files");
    wxWindow* fb = filesBox->GetStaticBox();
    auto* filesGrid = new wxFlexGridSizer(2, gap, gap);
    filesGrid->AddGrowableCol(1);
    wxButton* browseFolder = nullptr;
    wxButton* browseProgram = nullptr;
    folder_ = PathRow(fb, filesGrid, "&Folder:",
                      "The folder with the original game's files (INTRO.EXE, PS.EXE, AP.EXE and the .DYN files).",
                      &browseFolder);
    program_ = PathRow(fb, filesGrid, "&Program:", "aotp-enhanced, the game.", &browseProgram);
    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusIcon_ = new wxStaticBitmap(fb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    statusNote_ = new wxStaticText(fb, wxID_ANY, wxEmptyString);
    statusRow->Add(statusIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    statusRow->Add(statusNote_, 1, wxALIGN_CENTER_VERTICAL);
    filesBox->Add(filesGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(statusRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(GreyText(fb, "The game saves its pilot roster, preferences and films in this folder,\n"
                               "so it must be writable (not under Program Files)."),
                  0, wxALL, gap);
    folder_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    program_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    browseFolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseFolder(); });
    browseProgram->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseProgram(); });

    // Options
    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, page, "Options");
    wxWindow* ob = optionsBox->GetStaticBox();
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Game &speed:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* speedRow = new wxBoxSizer(wxHORIZONTAL);
    speed_ = new wxSpinCtrl(ob, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(64), -1),
                            wxSP_ARROW_KEYS, MIN_TICKS, MAX_TICKS, DEFAULT_TICKS);
    speed_->SetToolTip("Timer ticks (236.7 a second) per simulator frame. The simulator moves the world by a "
                       "step that grows with the ticks per frame, up to a limit, so fewer ticks per frame make "
                       "the world run faster than real time. 16 is about real time with the in-game TIME SCALE "
                       "at normal; 8 is what the original gives on a fast PC (the world about 1.45 times too "
                       "fast, the TIME SCALE slider without effect).");
    speedRow->Add(speed_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    speedRow->Add(GreyText(ob, "ticks a frame: 16 = real time, lower = faster world"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(speedRow, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&Aspect:"), 0, wxALIGN_CENTER_VERTICAL);
    aspect_ = new wxChoice(ob, wxID_ANY);
    aspect_->Append("4:3 (as on a VGA monitor)");
    aspect_->Append("1:1 (square pixels)");
    aspect_->SetToolTip("4:3 stretches the 320x200 picture to a 4:3 area like the monitors of 1992; 1:1 shows "
                        "it with square pixels.");
    grid->Add(aspect_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&Window size:"), 0, wxALIGN_CENTER_VERTICAL);
    scale_ = new wxChoice(ob, wxID_ANY);
    scale_->SetToolTip("The window's size when the game starts. Alt+Enter switches to full screen.");
    grid->Add(scale_, 0, wxALIGN_CENTER_VERTICAL);
    optionsBox->Add(grid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    fullscreen_ = new wxCheckBox(ob, wxID_ANY, "Start in f&ull screen (Alt+Enter switches)");
    optionsBox->Add(fullscreen_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    sound_ = new wxCheckBox(ob, wxID_ANY, "S&ound (AdLib music and effects)");
    sound_->SetToolTip("Off runs the game silently. Whatever sound device the game's install chose is played "
                       "as AdLib in this version.");
    optionsBox->Add(sound_, 0, wxALL, gap);
    aspect_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { FillScales(); });

    // Buttons
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    play_ = new wxButton(page, wxID_ANY, "&Play");
    auto* close = new wxButton(page, wxID_CLOSE, "Close");
    buttons->AddStretchSpacer();
    buttons->Add(play_, 0, wxRIGHT, gap);
    buttons->Add(close);
    play_->SetDefault();
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Play(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(filesBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(optionsBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

wxWindow* LauncherFrame::CreateKeysPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    auto* heading = new wxStaticText(page, wxID_ANY, "Key bindings");
    heading->SetFont(GetFont().Bold().Scaled(1.2f));
    auto* intro = GreyText(page, "Click a key to change it. Keys are the keyboard's places, whatever its layout. "
                                 "A connected gamepad acts as the joystick.");

    // A box per group with a label and a key button per action. Weapons shares its column with the keys
    // that can't be changed.
    wxStaticBoxSizer* boxes[keys::GROUP_COUNT];
    wxFlexGridSizer* grids[keys::GROUP_COUNT];
    for (int g = 0; g < keys::GROUP_COUNT; ++g) {
        boxes[g] = new wxStaticBoxSizer(wxVERTICAL, page, keys::GROUP_TITLES[g]);
        grids[g] = new wxFlexGridSizer(2, small, gap);
        grids[g]->AddGrowableCol(1);
        boxes[g]->Add(grids[g], 0, wxEXPAND | wxALL, gap);
    }
    for (int i = 0; i < keys::ACTION_COUNT; ++i) {
        const keys::Action& a = keys::ACTIONS[i];
        wxWindow* box = boxes[a.group]->GetStaticBox();
        grids[a.group]->Add(new wxStaticText(box, wxID_ANY, a.label), 0, wxALIGN_CENTER_VERTICAL);
        auto* button = new wxButton(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1));
        button->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { ChangeKey(i); });
        grids[a.group]->Add(button, 0, wxEXPAND);
        keyButtons_.push_back(button);
    }
    auto* fixedBox = new wxStaticBoxSizer(wxVERTICAL, page, "Keys that keep their job");
    wxWindow* xb = fixedBox->GetStaticBox();
    auto* fixedGrid = new wxFlexGridSizer(2, small, FromDIP(16));
    for (int i = 0; i < keys::FIXED_COUNT; ++i) {
        fixedGrid->Add(new wxStaticText(xb, wxID_ANY, keys::FIXED[i].keys));
        fixedGrid->Add(GreyText(xb, keys::FIXED[i].what));
    }
    fixedBox->Add(fixedGrid, 0, wxALL, gap);

    auto* middle = new wxBoxSizer(wxVERTICAL);
    middle->Add(boxes[keys::WEAPONS], 0, wxEXPAND);
    middle->Add(fixedBox, 0, wxEXPAND | wxTOP, margin);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    columns->Add(boxes[keys::FLYING], 0, wxEXPAND);
    columns->Add(middle, 0, wxEXPAND | wxLEFT, margin);
    columns->Add(boxes[keys::VIEWS], 0, wxEXPAND | wxLEFT, margin);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* defaults = new wxButton(page, wxID_ANY, "&Default");
    defaults->SetToolTip("Put every key back as the game has it.");
    auto* apply = new wxButton(page, wxID_APPLY, "&Apply");
    apply->SetToolTip("Keep these keys and go back.");
    auto* cancel = new wxButton(page, wxID_CANCEL, "Cancel");
    cancel->SetToolTip("Go back without changing the keys.");
    buttons->Add(defaults);
    buttons->AddStretchSpacer();
    buttons->Add(apply, 0, wxRIGHT, gap);
    buttons->Add(cancel);
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        edited_ = keys::Defaults();
        ShowEditedKeys();
    });
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        bindings_ = edited_;
        keys::Save(bindings_);
        ClosePage();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClosePage(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, small);
    all->Add(columns, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

wxWindow* LauncherFrame::CreateGraphicsPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    auto* heading = new wxStaticText(page, wxID_ANY, "Graphics");
    heading->SetFont(GetFont().Bold().Scaled(1.2f));
    auto* intro = GreyText(page, "Settings of the enhanced view. They take effect the next time the game starts.");

    // Ground dots
    auto* dotsBox = new wxStaticBoxSizer(wxVERTICAL, page, "Ground proximity dots");
    wxWindow* db = dotsBox->GetStaticBox();
    dotsBox->Add(GreyText(db, "Near the ground the game draws a grid of dots on it, so that height and speed\n"
                              "can be judged over the sea."),
                 0, wxLEFT | wxRIGHT | wxTOP, gap);
    smoothDots_ = new wxCheckBox(db, wxID_ANY, "&Smooth ground dots (drawn at the display's frame rate)");
    smoothDots_->SetToolTip("On: the enhanced view draws the dots itself at every displayed frame, moving with "
                            "the view, with the settings below. Off: the original's one-pixel "
                            "dots, moved only at the game's own frame rate, out to about 4 cells and below 4000.");
    dotsBox->Add(smoothDots_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    auto* grid = new wxFlexGridSizer(4, small, gap);
    grid->AddGrowableCol(1);
    for (const DotValue& d : DOT_VALUES) {
        wxSlider* slider;
        wxSpinCtrl* field;
        SliderRow(db, grid, d.label, d.note, d.tip, d.min, d.max, d.step, &slider, &field);
        dotSliders_.push_back(slider);
        dotFields_.push_back(field);
    }
    dotsBox->Add(grid, 0, wxEXPAND | wxALL, gap);
    smoothDots_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateGraphicsState(); });

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* defaults = new wxButton(page, wxID_ANY, "&Default");
    defaults->SetToolTip("Put every setting back to its default.");
    auto* apply = new wxButton(page, wxID_APPLY, "&Apply");
    apply->SetToolTip("Keep these settings and go back.");
    auto* cancel = new wxButton(page, wxID_CANCEL, "Cancel");
    cancel->SetToolTip("Go back without changing the settings.");
    buttons->Add(defaults);
    buttons->AddStretchSpacer();
    buttons->Add(apply, 0, wxRIGHT, gap);
    buttons->Add(cancel);
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowGraphics(Graphics()); });
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        graphics_ = EditedGraphics();
        settings::SetInt(GRAPHICS, "SmoothGroundDots", graphics_.smoothDots ? 1 : 0);
        for (const DotValue& d : DOT_VALUES) settings::SetInt(GRAPHICS, d.key, graphics_.*d.field);
        ClosePage();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClosePage(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, small);
    all->Add(dotsBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

wxWindow* LauncherFrame::CreateDebugPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    auto* heading = new wxStaticText(page, wxID_ANY, "Debug");
    heading->SetFont(GetFont().Bold().Scaled(1.2f));
    auto* intro = GreyText(page, "Tests, not part of the game as released. They take effect the next time the game "
                                 "starts.");

    // Aircraft size test
    auto* planesBox = new wxStaticBoxSizer(wxVERTICAL, page, "Aircraft size test");
    wxWindow* pb = planesBox->GetStaticBox();
    planesBox->Add(GreyText(pb, "The game's aircraft models are about 2.4 times the size of the real aircraft\n"
                                "(an F4U is 97 ft wide instead of 41): 42 % is true scale. To make them easier to\n"
                                "hit, the original also enlarges the hit boxes: your bullets x1 to x2 by the\n"
                                "gunnery level, the computer's x4. The hit box size applies on top of that."),
                   0, wxLEFT | wxRIGHT | wxTOP, gap);
    auto* grid = new wxFlexGridSizer(4, small, gap);
    grid->AddGrowableCol(1);
    SliderRow(pb, grid, "A&ircraft size:", "% (42 = true scale)",
              "Every aircraft is drawn at this percent of its size in the game, in the enhanced view. Only the "
              "picture changes. 100 = as in the original.",
              MIN_PLANE_SIZE, MAX_PLANE_SIZE, 10, &planeSlider_, &planeField_);
    SliderRow(pb, grid, "&Hit box size:", "% of the original's",
              "The aircraft's hit boxes against bullets, cannon and rockets, at this percent of the original's "
              "(after its own enlargement). Applies to everyone's shots. 100 = as in the original.",
              MIN_HITBOX_SIZE, MAX_HITBOX_SIZE, 10, &hitboxSlider_, &hitboxField_);
    planesBox->Add(grid, 0, wxEXPAND | wxALL, gap);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* defaults = new wxButton(page, wxID_ANY, "&Default");
    defaults->SetToolTip("Put every setting back to its default (as in the original).");
    auto* apply = new wxButton(page, wxID_APPLY, "&Apply");
    apply->SetToolTip("Keep these settings and go back.");
    auto* cancel = new wxButton(page, wxID_CANCEL, "Cancel");
    cancel->SetToolTip("Go back without changing the settings.");
    buttons->Add(defaults);
    buttons->AddStretchSpacer();
    buttons->Add(apply, 0, wxRIGHT, gap);
    buttons->Add(cancel);
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowDebug(Debug()); });
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        debug_ = EditedDebug();
        settings::SetInt(DEBUG_SECTION, "AircraftSize", debug_.planeSize);
        settings::SetInt(DEBUG_SECTION, "HitBoxSize", debug_.hitboxSize);
        ClosePage();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClosePage(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, small);
    all->Add(planesBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

void LauncherFrame::ShowDebug(const Debug& d) {
    planeField_->SetValue(d.planeSize);
    planeSlider_->SetValue(d.planeSize);
    hitboxField_->SetValue(d.hitboxSize);
    hitboxSlider_->SetValue(d.hitboxSize);
}

Debug LauncherFrame::EditedDebug() const {
    Debug d;
    // The sliders have the numbers as typed, even while a field hasn't taken its number yet.
    d.planeSize = planeSlider_->GetValue();
    d.hitboxSize = hitboxSlider_->GetValue();
    return d;
}

void LauncherFrame::ShowGraphics(const Graphics& g) {
    smoothDots_->SetValue(g.smoothDots);
    for (int i = 0; i < DOT_VALUE_COUNT; ++i) {
        dotFields_[i]->SetValue(g.*DOT_VALUES[i].field);
        dotSliders_[i]->SetValue(g.*DOT_VALUES[i].field);
    }
    UpdateGraphicsState();
}

Graphics LauncherFrame::EditedGraphics() const {
    Graphics g;
    g.smoothDots = smoothDots_->GetValue();
    // The sliders have the numbers as typed, even while a field hasn't taken its number yet.
    for (int i = 0; i < DOT_VALUE_COUNT; ++i) g.*DOT_VALUES[i].field = dotSliders_[i]->GetValue();
    return g;
}

// The values are for the smooth dots only.
void LauncherFrame::UpdateGraphicsState() {
    const bool on = smoothDots_->GetValue();
    for (int i = 0; i < DOT_VALUE_COUNT; ++i) {
        dotSliders_[i]->Enable(on);
        dotFields_[i]->Enable(on);
    }
}

void LauncherFrame::CreateMenus() {
    auto* file = new wxMenu;
    file->Append(wxID_PREFERENCES, wxString::FromUTF8("&Preferences…"));
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    auto* settingsMenu = new wxMenu;
    settingsMenu->Append(ID_KEY_BINDINGS, wxString::FromUTF8("&Key bindings…"));
    settingsMenu->Append(ID_GRAPHICS, wxString::FromUTF8("&Graphics…"));
    auto* debugMenu = new wxMenu;
    debugMenu->Append(ID_DEBUG, wxString::FromUTF8("&Aircraft size test…"));

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(settingsMenu, "&Game settings");
    bar->Append(debugMenu, "&Debug");
#ifndef __WXMSW__
    auto* about = new wxMenu;
    about->Append(ID_ABOUT, wxString::FromUTF8("&About ") + APP_TITLE + wxString::FromUTF8("…"));
    bar->Append(about, "A&bout");
#endif
    SetMenuBar(bar);
#ifdef __WXMSW__
    // A top-level About item that acts straight away, which wxWidgets menus
    // can't express; MSWWindowProc handles its command.
    const HMENU native = static_cast<HMENU>(bar->GetHMenu());
    AppendMenuW(native, MF_STRING, ID_ABOUT, L"A&bout");
    DrawMenuBar(static_cast<HWND>(GetHWND()));
#endif

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Preferences(); }, wxID_PREFERENCES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPage(PAGE_KEYS); }, ID_KEY_BINDINGS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPage(PAGE_GRAPHICS); }, ID_GRAPHICS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPage(PAGE_DEBUG); }, ID_DEBUG);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { About(); }, ID_ABOUT);
}

#ifdef __WXMSW__
WXLRESULT LauncherFrame::MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) {
    if (msg == WM_COMMAND && LOWORD(wParam) == ID_ABOUT && lParam == 0) {
        CallAfter([this] { About(); });
        return 0;
    }
    return wxFrame::MSWWindowProc(msg, wParam, lParam);
}
#endif

void LauncherFrame::FillScales() {
    const int pick = scale_->GetSelection();
    const int h = aspect_->GetSelection() == 1 ? 200 : 240;
    scale_->Clear();
    for (int s = MIN_SCALE; s <= MAX_SCALE; ++s) scale_->Append(wxString::Format(L"%d × %d", 320 * s, h * s));
    scale_->SetSelection(pick >= 0 ? pick : DEFAULT_SCALE - MIN_SCALE);
}

void LauncherFrame::UpdateState() {
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    const bool haveProgram = wxFileName::FileExists(program);
    const wxString missing = MissingGameFile(dir);
    wxString note;
    if (!missing.empty())
        note = wxString::Format("This folder needs the game's files: %s is missing.", missing);
    else if (!haveProgram)
        note = wxString::Format("%s isn't there.", wxFileName(program).GetFullName());
    else
        note = "Found the game's files.";
    const bool ok = haveProgram && missing.empty();
    statusIcon_->Show(!ok);
    statusNote_->SetLabel(note);
    play_->Enable(ok);
    play_->GetParent()->Layout();
}

void LauncherFrame::BrowseFolder() {
    wxDirDialog dialog(this, "Choose the folder with the game's files", folder_->GetValue(),
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) folder_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherFrame::BrowseProgram() {
    wxFileName current(program_->GetValue());
#ifdef __WXMSW__
    const char* const filter = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
#else
    const char* const filter = "All files|*";
#endif
    wxFileDialog dialog(this, "Choose aotp-enhanced", current.GetPath(), current.GetFullName(), filter,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) program_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherFrame::Play() {
    Save();
    GameOptions options;
    options.program = wxFileName(program_->GetValue()).GetFullPath();
    options.gameDir = wxFileName(folder_->GetValue()).GetFullPath();
    options.frameTicks = speed_->GetValue();
    options.aspect43 = aspect_->GetSelection() != 1;
    options.scale = scale_->GetSelection() + MIN_SCALE;
    options.fullscreen = fullscreen_->GetValue();
    options.sound = sound_->GetValue();
    options.keys = keys::Argument(bindings_);
    options.graphics = graphics_;
    options.debug = debug_;
    wxString error;
    if (!LaunchGame(options, error)) wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
}

void LauncherFrame::Save() {
    // A folder or program left at its default is stored empty, so it follows the launcher if it moves.
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    settings::SetString(SECTION, "GameFolder",
                        wxFileName(dir).SameAs(wxFileName(DefaultGameDir())) ? wxString() : dir);
    settings::SetString(SECTION, "Program",
                        wxFileName(program).SameAs(wxFileName(DefaultProgram())) ? wxString() : program);
    settings::SetInt(SECTION, "FrameTicks", speed_->GetValue());
    settings::SetInt(SECTION, "Aspect43", aspect_->GetSelection() != 1 ? 1 : 0);
    settings::SetInt(SECTION, "Scale", scale_->GetSelection() + MIN_SCALE);
    settings::SetInt(SECTION, "Fullscreen", fullscreen_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "Sound", sound_->GetValue() ? 1 : 0);
    settings::SaveWindowPosition(SECTION, this);
}

void LauncherFrame::Preferences() {
    wxDialog dialog(this, wxID_ANY, "Preferences");
    auto* all = new wxBoxSizer(wxVERTICAL);
    auto* topmost = new wxCheckBox(&dialog, wxID_ANY, "Always on &top");
    topmost->SetValue(topmost_);
    topmost->SetToolTip("Keep the launcher above other windows.");
    all->Add(topmost, 0, wxALL, dialog.FromDIP(12));
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
             dialog.FromDIP(12));
    dialog.SetSizerAndFit(all);
    dialog.SetMinSize(wxSize(dialog.FromDIP(300), -1));
    dialog.Fit();
    dialog.CentreOnParent();
    topmost->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return;
    if (topmost->GetValue() != topmost_) SetTopmost(topmost->GetValue());
}

void LauncherFrame::SetTopmost(bool on) {
    topmost_ = on;
    const long style = GetWindowStyleFlag();
    SetWindowStyleFlag(on ? style | wxSTAY_ON_TOP : style & ~wxSTAY_ON_TOP);
    settings::SetInt("Preferences", "AlwaysOnTop", on ? 1 : 0);
}

// The window takes the size of the page shown (the book on its own would take the largest page's).
void LauncherFrame::FitPage() {
    SetClientSize(book_->GetCurrentPage()->GetBestSize());
}

void LauncherFrame::ShowEditedKeys() {
    for (int i = 0; i < keys::ACTION_COUNT; ++i) {
        const wxString name = keys::Name(edited_[i]);
        // Buttons treat '&' as a mnemonic marker.
        wxString label = name;
        label.Replace("&", "&&");
        keyButtons_[i]->SetLabel(label);
        keyButtons_[i]->SetToolTip(wxString::Format("%s: %s. Click to change it.", keys::ACTIONS[i].label, name));
        // A key that differs from the game's own stands out.
        wxFont font = keyButtons_[i]->GetParent()->GetFont();
        if (edited_[i] != keys::ACTIONS[i].def) font.MakeBold();
        keyButtons_[i]->SetFont(font);
    }
    book_->GetPage(PAGE_KEYS)->Layout();
}

// A settings page starts from the saved settings; the menu's pages wait until it is closed.
void LauncherFrame::OpenPage(int page) {
    if (book_->GetSelection() == page) return;
    if (page == PAGE_KEYS) {
        edited_ = bindings_;
        ShowEditedKeys();
    } else if (page == PAGE_DEBUG) {
        ShowDebug(debug_);
    } else {
        ShowGraphics(graphics_);
    }
    book_->ChangeSelection(page);
    FitPage();
    GetMenuBar()->Enable(ID_KEY_BINDINGS, false);
    GetMenuBar()->Enable(ID_GRAPHICS, false);
    GetMenuBar()->Enable(ID_DEBUG, false);
    SetDefaultItem(book_->GetPage(page)->FindWindow(wxID_APPLY));
    if (page == PAGE_KEYS) keyButtons_.front()->SetFocus();
    else if (page == PAGE_DEBUG) planeSlider_->SetFocus();
    else smoothDots_->SetFocus();
}

void LauncherFrame::ClosePage() {
    book_->ChangeSelection(PAGE_MAIN);
    FitPage();
    GetMenuBar()->Enable(ID_KEY_BINDINGS, true);
    GetMenuBar()->Enable(ID_GRAPHICS, true);
    GetMenuBar()->Enable(ID_DEBUG, true);
    SetDefaultItem(play_);
    play_->SetFocus();
}

void LauncherFrame::ChangeKey(int action) {
    const wxString label = keys::ACTIONS[action].label;
    const int key = keys::AskKey(this, label);
    if (key < 0 || key == edited_[action]) return;
    if (key != 0) {
        for (int other = 0; other < keys::ACTION_COUNT; ++other) {
            if (other == action || edited_[other] != key) continue;
            const wxString question = wxString::Format(
                "%s is already the key for %s.\n\nUse it for %s instead? %s will have no key.", keys::Name(key),
                keys::ACTIONS[other].label, label, keys::ACTIONS[other].label);
            if (wxMessageBox(question, "Key bindings", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
            edited_[other] = 0;
        }
    }
    edited_[action] = key;
    ShowEditedKeys();
}

void LauncherFrame::About() {
    const wxString title = wxString("About ") + APP_TITLE;
    const wxString heading = wxString(APP_TITLE) + " " + APP_VERSION_TEXT;
    const wxString blurb = "Starts aotp-enhanced, the enhanced SDL3 port of Aces of the Pacific (Dynamix, 1992).";
#ifdef __WXMSW__
    // The Windows task dialog.
    const wxString content = wxString::Format(
        "%s\n\n"
        "Author: Krzysztof Kania\n"
        "Website: <a href=\"%s\">kkania.com</a>\n"
        "Support: <a href=\"%s\">buymeacoffee.com/krzysztofkania</a>",
        blurb, WEBSITE, SUPPORT);
    wxIcon icon;
    icon.CopyFromBitmap(AppBitmap(FromDIP(32)));
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof dialog;
    dialog.hwndParent = static_cast<HWND>(GetHWND());
    dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons = TDCBF_OK_BUTTON;
    dialog.pszWindowTitle = title.wc_str();
    dialog.hMainIcon = static_cast<HICON>(icon.GetHICON());
    dialog.pszMainInstruction = heading.wc_str();
    dialog.pszContent = content.wc_str();
    dialog.pfCallback = AboutCallback;
    TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr);
#else
    wxDialog dialog(this, wxID_ANY, title);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(new wxStaticBitmap(&dialog, wxID_ANY, AppBitmap(dialog.FromDIP(48))), 0, wxALL, dialog.FromDIP(12));

    auto* text = new wxBoxSizer(wxVERTICAL);
    auto* headingText = new wxStaticText(&dialog, wxID_ANY, heading);
    headingText->SetFont(dialog.GetFont().Bold().Scaled(1.3f));
    text->Add(headingText, 0, wxBOTTOM, dialog.FromDIP(8));
    text->Add(new wxStaticText(&dialog, wxID_ANY, blurb), 0, wxBOTTOM, dialog.FromDIP(12));
    text->Add(new wxStaticText(&dialog, wxID_ANY, "Author: Krzysztof Kania"));
    AddLink(&dialog, text, "Website:", "kkania.com", WEBSITE);
    AddLink(&dialog, text, "Support:", "buymeacoffee.com/krzysztofkania", SUPPORT);
    body->Add(text, 1, wxTOP | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(body, 1, wxEXPAND);
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
    dialog.SetSizerAndFit(all);
    dialog.CentreOnParent();
    dialog.ShowModal();
#endif
}
