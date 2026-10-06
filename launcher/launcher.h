// launcher.h -- the launcher window: choose the game folder, the game's speed and the display and
// sound options, then Play. Game settings > Key bindings, > Graphics and > Aircraft size show their page in
// the window.
#pragma once

#include <wx/frame.h>

#include <vector>

#include "game.h"
#include "keys.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSimplebook;
class wxSlider;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherFrame : public wxFrame {
public:
    LauncherFrame();

private:
    wxWindow* CreateMainPage(wxWindow* parent);
    wxWindow* CreateKeysPage(wxWindow* parent);
    wxWindow* CreateGraphicsPage(wxWindow* parent);
    wxWindow* CreateAircraftPage(wxWindow* parent);
    void CreateMenus();
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    // Checks the folder and the program and enables Play to match.
    void UpdateState();
    // Relabels the window sizes for the chosen aspect.
    void FillScales();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void Preferences();
    void SetTopmost(bool on);
    void Save();

    void FitPage();
    // A settings page in place of the main one, and back.
    void OpenPage(int page);
    void ClosePage();
    // Key bindings: a new key for action i, the buttons from edited_.
    void ChangeKey(int action);
    void ShowEditedKeys();
    // Graphics: the controls from / to a Graphics.
    void ShowGraphics(const Graphics& g);
    Graphics EditedGraphics() const;
    void UpdateGraphicsState();
    // Aircraft size: the controls from / to an AircraftSize.
    void ShowAircraft(const AircraftSize& d);
    AircraftSize EditedAircraft() const;

    wxSimplebook* book_ = nullptr;
    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxSpinCtrl* speed_ = nullptr;
    wxChoice* aspect_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxCheckBox* sound_ = nullptr;
    wxButton* play_ = nullptr;
    // The Key bindings page: one button per action.
    std::vector<wxButton*> keyButtons_;
    keys::Bindings bindings_;  // saved
    keys::Bindings edited_;    // on the Key bindings page
    // The Graphics page: the ground dots; a slider and a number field for each value (DOT_VALUES).
    wxCheckBox* smoothDots_ = nullptr;
    std::vector<wxSlider*> dotSliders_;
    std::vector<wxSpinCtrl*> dotFields_;
    Graphics graphics_;  // saved
    // The Aircraft size page.
    wxSlider* planeSlider_ = nullptr;
    wxSpinCtrl* planeField_ = nullptr;
    wxSlider* hitboxSlider_ = nullptr;
    wxSpinCtrl* hitboxField_ = nullptr;
    AircraftSize aircraft_;  // saved
    bool topmost_ = false;
    bool loading_ = true;  // no edits are recorded while the window is built
};
