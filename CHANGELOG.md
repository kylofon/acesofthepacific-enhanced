# Changelog

## Unreleased

- Enhanced view: frames are presented once per refresh of the display (they were capped at 60–80 fps whatever the view), and the sky, the sea haze, the ground dots and the final mix skip what the cockpit covers and what cannot show (looking up no longer walks the dot grid).
- Ground proximity dots are drawn by the enhanced view at the display's rate, farther (`--dots-range`, 12 cells by default) and up to a higher altitude (`--dots-alt`, 8000 by default), with a chosen size (`--dots-size`), smaller up close (`--dots-full`, `--dots-near`) and in the distance (`--dots-edge`), thinned where they crowd (`--dots-cover`), and scattered off the straight rows of the grid (`--dots-scatter`), denser with the distance (`--dots-far`), fading out toward the range's edge (`--dots-fade`) with a faint speckle beyond it out to the horizon (`--dots-speckle`), fading into the sea with the distance except the larger wave crests (`--dots-far-bright`), and a debug overlay (`--dots-debug`, Ctrl+Alt+G); `--no-dots-enh` keeps the original's.
- Launcher: the Settings menu is now Game settings, with Key bindings and Graphics (the ground dots, with sliders and number fields).

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
