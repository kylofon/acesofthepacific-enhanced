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

// Game settings > Graphics: the ground dots near the ground (aotp --no-dots-enh, --dots-range,
// --dots-alt, --dots-size, --dots-full, --dots-near, --dots-scatter, --dots-far, --dots-fade,
// --dots-speckle, --dots-far-bright, --dots-edge, --dots-cover).
struct Graphics {
    bool smoothDots = true;  // drawn by the enhanced view at the display rate
    int dotsRange = 12;      // grid cells of 1024 units (the original shows about 4)
    int dotsAlt = 8000;      // highest altitude with dots (the original's 4000)
    int dotsSize = 100;      // percent of an original pixel
    int dotsFull = 4096;     // eye distance from which a dot has its full size (nearer ones are smaller)
    int dotsNear = 25;       // the nearest dots' size, percent of the full size
    int dotsScatter = 40;    // dots moved off the grid by up to this percent of half a cell
    int dotsFar = 2;         // denser with the distance: the spacing halved this many times at the edge
    int dotsFade = 60;       // the dots fade out from this percent of the range to its edge
    int dotsSpeckle = 3;     // beyond the range: a faint speckle over this percent of the ground
    int dotsFarBright = 60;  // the far speckle and wave crests: percent of the dots' colour over the ground
    int dotsEdge = 35;       // beyond the full-size distance the dots shrink to this percent at the edge
    int dotsCover = 8;       // crowded dots are skipped to cover at most this percent of the ground
};

struct GameOptions {
    wxString program;         // aotp(.exe)
    wxString gameDir;         // --game-dir
    int frameTicks = 16;      // --frame-ticks: the sim's frame pacing
    int scale = 3;            // --scale: the window is 320x240 (4:3) or 320x200 (1:1) times this
    bool aspect43 = true;     // --aspect 4:3 | 1:1
    bool fullscreen = false;  // --fullscreen
    bool sound = true;        // false: SDL_AUDIO_DRIVER=dummy
    wxString keys;            // --keys: the keys not on their defaults (keys::Argument)
    Graphics graphics;
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
