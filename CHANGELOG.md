# Changelog

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
