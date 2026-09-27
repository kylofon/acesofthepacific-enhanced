# Aces of the Pacific Enhanced — plan

Built on the faithful port (`../AcesOverPacific`, `aotpport/` at its v0.1.0 state, commit ff88596). The
faithful engine runs unchanged (see `ENGINE.md` for its rules); enhancements live in `src/enhanced/` and hook
into the engine at marked `ENH:` points, as in TestDrive3Enhanced. `--classic` turns every enhancement off.

Why this game suits it: the sim is already a polygon world (3Space, `port/spec/render3d*.md` in the
faithful repo), so the 3D view can be drawn again on the host from the same geometry every frame, at any
resolution, instead of scaling the 320×200 picture.

## Stages (to be confirmed with the user)

1. Smooth motion at the display rate (the sim keeps its own step, `DS:711E`; the view interpolates between
   sim frames) and high-resolution / widescreen 3D view with anti-aliasing; cockpit, HUD and 2D screens
   scaled from the original art.
2. Longer view distance, more detail (LOD), better haze/sky.
3. New assets (hi-res cockpits, textures) — only if asked.

## Status

- 2026-09-27: repository set up from the faithful port; exe `aotp-enhanced`, launcher
  "Aces of the Pacific Enhanced" (built by default), game data copy in `build/Game` (ignored).
