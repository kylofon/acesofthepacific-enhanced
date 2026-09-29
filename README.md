# Aces of the Pacific Enhanced — SDL3

An enhanced native port of Dynamix's *Aces of the Pacific* (1992), built on the faithful
[SDL3 port](https://github.com/kylofon/aces-of-the-pacific-sdl3). Not an emulator. The original game files are
not included.

## Features

* Smooth 3D view at your display's refresh rate.
* High resolution (up to 2560 × 1600) with anti-aliasing; optional 16:9 widescreen.
* Three times the view distance, the finest aircraft and ship models at every distance.
* Distance haze, a smooth sky and a soft horizon.
* Held direction keys combine (Left + Down pulls through a turn).
* Separate flaps down and flaps up keys.
* A launcher with key bindings.

## How to play (Windows)

1. Put your installed DOS game files in a folder named `Game`.
2. Place the program files next to it:

   ```text
   Aces of the Pacific Enhanced\
   ├── Game\                               <- your game files (INTRO.EXE, PS.EXE, AP.EXE, *.DYN, ...)
   ├── Aces of the Pacific Enhanced.exe    <- the launcher
   ├── aotp-enhanced.exe                   <- the game
   └── SDL3.dll and the other DLLs
   ```

3. Run `Aces of the Pacific Enhanced.exe`, pick your options and press **Play**.

* The folder must be writable (roster, preferences and films are saved in `Game`); not under Program Files.
* Alt+Enter: full screen.
* If Windows says "Windows protected your PC": **More info**, then **Run anyway**.
* Launcher settings: `%APPDATA%\Aces of the Pacific Enhanced\settings.ini`.

## Requirements

* Game files: the installed DOS game (`INTRO.EXE`, `PS.EXE`, `AP.EXE`, `COMMON1.DYN`, `SHELL1.DYN`, `SIM1.DYN`,
  `SIM2.DYN` and the rest of the folder).
* A multi-core CPU for the default picture; lower `--res-scale` or `--aa` on slow machines.
* To build: CMake 3.24+, C11, SDL 3; the launcher needs C++17 and wxWidgets 3.2.

## Build

In Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

* `-DAOTP_LAUNCHER=OFF` builds the game only.
* The MSYS2 `SDL3.dll` also needs `libiconv-2.dll` from `C:\msys64\mingw64\bin`.

## Run

```bash
./build/aotp-enhanced.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Game files folder (default `Game`) |
| `--scale N` | Window size, × 320×240 (default 3) |
| `--aspect 4:3\|1:1` | 4:3 as on a VGA monitor (default) or square pixels |
| `--fullscreen` | Start in full screen |
| `--frame-ticks N` | Game speed, timer ticks per simulator frame (default 16 = real time; 8 = the original on a fast PC) |
| `--res-scale N` | Resolution, × 320×200 (default 4, 1–8) |
| `--aa N` | Anti-aliasing, N×N samples (default 2, 1 = off) |
| `--widescreen` | 16:9 picture |
| `--view-dist N` | View distance, × the original's (default 3, 1–8) |
| `--no-lod-enh` | Keep the original's model detail by distance |
| `--no-haze-enh` | No haze; banded sky and hard horizon as the original |
| `--keys NAME=CODE,...` | Changed keys (written by the launcher's Key Bindings) |
| `--classic` | The original picture and behaviour |
| `--check` | Check that the three programs load, then exit |

To run without sound set `SDL_AUDIO_DRIVER=dummy` (the launcher's Sound box does this).

## Controls

As in the original (see its manual), except:

* F lowers the flaps a step, Shift+F raises them.
* Held direction keys combine.
* Settings > Key Bindings in the launcher changes the keys.
* A gamepad acts as the joystick. Alt+Enter: full screen.

## Known limitations

* Explosions, smoke, clouds, the sun, moon and stars are the original's pixels, scaled.
* The INTRO and the shell's aircraft viewer are scaled, not re-rendered.
* The launcher has no controls for the picture options yet (use the command line).
* MT-32, Tandy, PS/1 and PC speaker sound play as AdLib.

## Layout

* `src/` — the game (`ENGINE.md`; changes marked `ENH:`).
* `src/enhanced/` — the enhanced view and the key bindings (`ENHANCED.md`).
* `launcher/` — the launcher (`launcher/README.md`).
* `third_party/nuked-opl3/` — the OPL emulator.

## License

MIT (`LICENSE`). Nuked-OPL3 is LGPL 2.1+. The release's DLLs and their licences: `licenses/THIRD-PARTY.txt`
in the zip. *Aces of the Pacific* is © Dynamix / Sierra On-Line; its files are
not included.

## Support

https://buymeacoffee.com/krzysztofkania
