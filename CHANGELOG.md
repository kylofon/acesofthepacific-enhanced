# Changelog

## Unreleased

- Torpedoes work again: one dropped into the water was always deleted as broken (the splash test's land/water result was read the wrong way round); it now breaks only on land or after a drop too high, too fast or too steep.

## 0.2.0

- The CD-ROM version 1.2 is linked at the Internet Archive (https://archive.org/details/acesot-p) in the README and `HOW TO RUN.txt`; in the launcher, clicking the warning sign beside a missing-files or other-release note opens that page.
- Game files of another release (the floppy versions 1.0, 1.01 and 1.2 / Patch B, the WWII: 1946 expansion disk) are recognised: the launcher and the game say which release the folder holds and that the CD-ROM version 1.2 is needed, before anything runs (the game used to stop with the loader's sizes, the launcher let it start).
- Enhanced view: frames are presented once per refresh of the display (they were capped at 60–80 fps whatever the view), and the sky, the sea haze, the ground dots and the final mix skip what the cockpit covers and what cannot show (looking up no longer walks the dot grid).
- Ground proximity dots are drawn by the enhanced view at the display's rate, farther (`--dots-range`, 12 cells by default) and up to a higher altitude (`--dots-alt`, 8000 by default), with a chosen size (`--dots-size`), smaller up close (`--dots-full`, `--dots-near`) and in the distance (`--dots-edge`), thinned where they crowd (`--dots-cover`), and scattered off the straight rows of the grid (`--dots-scatter`), denser with the distance (`--dots-far`), fading out toward the range's edge (`--dots-fade`) with a faint speckle beyond it out to the horizon (`--dots-speckle`), fading into the sea with the distance except the larger wave crests (`--dots-far-bright`), and a debug overlay (`--dots-debug`, Ctrl+Alt+G); `--no-dots-enh` keeps the original's.
- Launcher: the Settings menu is now Game settings, with Key bindings and Graphics (the ground dots, with sliders and number fields).
- Wireframes key (Ctrl+Alt+G by default, in the launcher's Key bindings): cycles the hit boxes of the aircraft, ships and ground objects (green for the player's side, red for the enemy), the ground dots' debug overlay, and off.
- Aircraft size (the launcher's Game settings > Aircraft size): `--plane-size` draws the aircraft and their shadows smaller in the enhanced view (10–100 %, default 100; the original's models are about 2.4 times the real aircraft, 42 is real-life size), and `--hitbox-size` scales their hit boxes against weapons (10–400 %, default 100).
- A picture that isn't changing is drawn again after Alt+Enter, a window resize or the window being uncovered (it used to keep the old full-screen picture, cropped, after switching back to a window).

## 0.1.0

- Project started from the faithful Aces of the Pacific SDL3 port, with its launcher renamed "Aces of the Pacific Enhanced".
- Smooth 3D view: the camera and aircraft are interpolated between the simulator's frames at the display's rate.
- High resolution (`--res-scale`) with anti-aliasing (`--aa`) and optional widescreen (`--widescreen`); the cockpit, HUD and menus stay the original art, scaled.
- Longer view distance (`--view-dist`, 3× by default), the finest models at every distance, real aircraft instead of the far dots.
- Distance haze, a smooth sky gradient and a soft horizon.
- Tracers are placed along their flight at the view's time.
- Held direction keys combine, e.g. Left + Down.
- Flaps: F lowers them a step, Shift+F raises them (the original's F cycles them).
- Key bindings: the game's keys can be changed in the launcher (`--keys`).
- Launcher: File (Preferences with Always on top, Exit), Settings > Key Bindings, About.
- `--classic` gives the original picture and behaviour.
