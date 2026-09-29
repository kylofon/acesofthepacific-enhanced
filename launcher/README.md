# Aces of the Pacific Enhanced (the launcher)

`Aces of the Pacific Enhanced.exe` starts `aotp-enhanced`, the enhanced SDL3 port of Aces of the Pacific, with
the options chosen in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the
platform's own controls, like the faithful port's launcher it is modelled on.

## The window

* **Game files**
  * **Folder** (`--game-dir`): the folder with the installed game's files (default `Game` beside the
    launcher). The game writes its roster, preferences, films and `SIM.IN` / `SIM.OUT` there, so it must be
    writable.
  * **Program**: `aotp-enhanced.exe` (default: beside the launcher).
  * The line below says what is missing (`INTRO.EXE`, `PS.EXE`, `AP.EXE`, `COMMON1.DYN`, `SHELL1.DYN`,
    `SIM1.DYN`, `SIM2.DYN`). **Play** stays greyed out until the folder and the program are there.
* **Options**
  * **Game speed** (`--frame-ticks`, default 16): timer ticks (236.7 a second) per simulator frame. Fewer
    ticks make the world run faster; 16 is about real time at the default TIME SCALE, 8 is what the original
    gives on a fast PC (about 1.45 times too fast, the TIME SCALE slider without effect).
  * **Aspect** (`--aspect`): 4:3 as on a VGA monitor, or 1:1 square pixels.
  * **Window size** (`--scale`) and **Start in full screen** (`--fullscreen`; Alt+Enter switches).
  * **Sound**: off starts the game with `SDL_AUDIO_DRIVER=dummy`.
* **Play** starts the game; the launcher stays open.

## Menus

* **File**: **Preferences** (**Always on top** keeps the launcher above other windows) and **Exit**.
* **Settings > Key Bindings** shows the game's keys in the window, in three groups (flying, weapons, views
  and game). Click a key and press the new one ("Press new key"; Shift, Ctrl or Alt can be held with it,
  Esc cancels, **No key** leaves the action without one). A key already used by another action can be
  moved, leaving that one without a key. **Default** puts every key back, **Apply** keeps them and goes
  back, **Cancel** goes back without changing them. The game gets the changed ones with `--keys`. Esc,
  Tab, Enter, the number keys, F1 - F6 and Ctrl / Alt + F1 - F10 keep their jobs. The flaps have a flaps
  down key (F) and a flaps up key (Shift+F) instead of the original's one key that cycles them.
* **About**: version, author and links.

Everything is remembered in `%APPDATA%\Aces of the Pacific Enhanced\settings.ini`
(`~/.config/aces-of-the-pacific-enhanced` on Linux), the keys under `[Keys]`. A folder or program left at its default is stored empty, so it follows the launcher if the whole
folder moves. Delete the file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). The main project builds it beside `aotp-enhanced.exe` (`-DAOTP_LAUNCHER=OFF`
leaves it out):

```bash
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

It also builds on its own (`cmake -S launcher -B launcher/build -G Ninja`); then choose the program in the
window or copy the launcher next to it.

On Windows the build copies every DLL the launcher needs beside it (`copy_dlls.cmake`): the two wxWidgets DLLs
and the MSYS2 libraries they load. Keep them with the `.exe` in a release. The C++ runtime of the launcher
itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `launcher.h`, `launcher.cpp`: the window, its menus, Preferences, Key Bindings and the About box.
* `keys.h`, `keys.cpp`: the game's keys that can be changed, their names and the "Press new key" prompt
  (the game's own list is `src/enhanced/enh_keys.c`).
* `game.h`, `game.cpp`: the file checks and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, a fighter over the sea drawn in code. `make_icon.py` (Pillow) writes the
  same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy.
* `CMakeLists.txt`: the build, standalone or from the main project.
