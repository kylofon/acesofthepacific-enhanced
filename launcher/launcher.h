// launcher.h -- the launcher window: choose the game folder, the game's speed and the display and
// sound options, then Play. Settings > Key Bindings shows the game's keys in the window to change them.
#pragma once

#include <wx/frame.h>

#include <vector>

#include "game.h"
#include "keys.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSimplebook;
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
    // Key Bindings: the page, a new key for action i, the buttons from edited_.
    void OpenKeyBindings();
    void CloseKeyBindings();
    void ChangeKey(int action);
    void ShowEditedKeys();

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
    // The Key Bindings page: one button per action.
    std::vector<wxButton*> keyButtons_;
    keys::Bindings bindings_;  // saved
    keys::Bindings edited_;    // on the Key Bindings page
    bool topmost_ = false;
    bool loading_ = true;  // no edits are recorded while the window is built
};
