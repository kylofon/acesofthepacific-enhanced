// app.cpp -- the Aces of the Pacific launcher: the game folder, the options
// and the options, then td3port.
#include <wx/app.h>

#include "launcher.h"

class LauncherApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Aces of the Pacific Enhanced");
        SetVendorName("Krzysztof Kania");
        if (!wxApp::OnInit()) return false;
        auto* dialog = new LauncherDialog;
        SetTopWindow(dialog);
        dialog->Show();
        return true;
    }
};

wxIMPLEMENT_APP(LauncherApp);
