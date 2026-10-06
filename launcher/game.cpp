// game.cpp -- the files the game needs and starting it.
#include "game.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <string>
#include <vector>

namespace {

// A file of `dir` whose name matches `name` as written, in upper or in lower case
// (case matters outside Windows); empty if there is none.
wxString FindFile(const wxString& dir, const wxString& name) {
    for (const wxString& n : {name, name.Upper(), name.Lower()}) {
        const wxString path = wxFileName(dir, n).GetFullPath();
        if (wxFileName::FileExists(path)) return path;
    }
    return wxString();
}

}  // namespace

bool FilePresent(const wxString& dir, const wxString& name) { return !dir.empty() && !FindFile(dir, name).empty(); }

wxString MissingGameFile(const wxString& dir) {
    for (const char* name : {"INTRO.EXE", "PS.EXE", "AP.EXE", "COMMON1.DYN", "SHELL1.DYN", "SIM1.DYN", "SIM2.DYN"})
        if (!FilePresent(dir, name)) return name;
    return wxString();
}

wxString LauncherDir() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }

wxString DefaultGameDir() { return wxFileName(LauncherDir(), "Game").GetFullPath(); }

wxString DefaultProgram() {
    wxFileName name(LauncherDir(), "aotp-enhanced");
#ifdef __WXMSW__
    name.SetExt("exe");
#endif
    return name.GetFullPath();
}

bool LaunchGame(const GameOptions& o, wxString& error) {
    if (!wxFileName::FileExists(o.program)) {
        error = wxString::Format("The game's program isn't there:\n\n%s", o.program);
        return false;
    }
    std::vector<wxString> args{o.program,
                               "--game-dir", o.gameDir,
                               "--scale", wxString::Format("%d", o.scale),
                               "--aspect", o.aspect43 ? "4:3" : "1:1",
                               "--frame-ticks", wxString::Format("%d", o.frameTicks)};
    if (o.fullscreen) args.push_back("--fullscreen");
    if (!o.graphics.smoothDots) {
        args.push_back("--no-dots-enh");
    } else {
        args.insert(args.end(), {"--dots-range", wxString::Format("%d", o.graphics.dotsRange),
                                 "--dots-alt", wxString::Format("%d", o.graphics.dotsAlt),
                                 "--dots-size", wxString::Format("%d", o.graphics.dotsSize),
                                 "--dots-full", wxString::Format("%d", o.graphics.dotsFull),
                                 "--dots-near", wxString::Format("%d", o.graphics.dotsNear),
                                 "--dots-scatter", wxString::Format("%d", o.graphics.dotsScatter),
                                 "--dots-far", wxString::Format("%d", o.graphics.dotsFar),
                                 "--dots-fade", wxString::Format("%d", o.graphics.dotsFade),
                                 "--dots-speckle", wxString::Format("%d", o.graphics.dotsSpeckle),
                                 "--dots-far-bright", wxString::Format("%d", o.graphics.dotsFarBright),
                                 "--dots-edge", wxString::Format("%d", o.graphics.dotsEdge),
                                 "--dots-cover", wxString::Format("%d", o.graphics.dotsCover)});
    }
    if (o.debug.planeSize != 100) {
        args.push_back("--plane-size");
        args.push_back(wxString::Format("%d", o.debug.planeSize));
    }
    if (o.debug.hitboxSize != 100) {
        args.push_back("--hitbox-size");
        args.push_back(wxString::Format("%d", o.debug.hitboxSize));
    }
    if (!o.keys.empty()) {
        args.push_back("--keys");
        args.push_back(o.keys);
    }

    std::vector<std::wstring> wide;
    for (const wxString& a : args) wide.push_back(a.ToStdWstring());
    std::vector<const wchar_t*> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    argv.push_back(nullptr);

    wxExecuteEnv env;  // an empty variable map: the game inherits the launcher's environment
    env.cwd = wxFileName(o.program).GetPath();
    if (!o.sound) {
        // Sound off: SDL opens its silent audio driver, the game runs as with a sound card.
        wxGetEnvMap(&env.env);
        env.env["SDL_AUDIO_DRIVER"] = "dummy";
    }
    long pid;
    {
        wxLogNull quiet;  // wxExecute would show its own error box
        pid = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &env);
    }
    if (pid == 0) {
        error = wxString::Format("Couldn't start %s.\n\n%s", wxFileName(o.program).GetFullName(),
                                 wxSysErrorMsgStr(wxSysErrorCode()));
        return false;
    }
    return true;
}
