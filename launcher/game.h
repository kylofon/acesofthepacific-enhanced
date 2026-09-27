// game.h -- what a game folder must hold, where aotp is, and starting it.
#pragma once

#include <wx/string.h>

// The folder the launcher runs from.
wxString LauncherDir();

// Where things are by default: "Game" and aotp(.exe) beside the launcher.
wxString DefaultGameDir();
wxString DefaultProgram();

// Whether `dir` holds the file `name` (as written, or in upper or lower case).
bool FilePresent(const wxString& dir, const wxString& name);

// The first of the game's own files missing from `dir` (the three programs and the .DYN volumes),
// or empty if they are all there.
wxString MissingGameFile(const wxString& dir);

struct GameOptions {
    wxString program;         // aotp(.exe)
    wxString gameDir;         // --game-dir
    int frameTicks = 16;      // --frame-ticks: the sim's frame pacing
    int scale = 3;            // --scale: the window is 320x240 (4:3) or 320x200 (1:1) times this
    bool aspect43 = true;     // --aspect 4:3 | 1:1
    bool fullscreen = false;  // --fullscreen
    bool sound = true;        // false: SDL_AUDIO_DRIVER=dummy
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
