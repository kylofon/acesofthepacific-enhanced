# Aces of the Pacific Enhanced — plan

Built on the faithful port (`../AcesOfThePacific`, `aotpport/` at its v0.1.0 state, commit ff88596). The
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
- 2026-09-27: **Stage 1 implemented** (smooth motion, high-resolution anti-aliased 3D view, optional
  widescreen), see "Stage 1 design" below. Verified headless on the DEMOAG film (external, chase and
  cockpit views, `--widescreen`) and that `--classic` plays it as the faithful port does.
- 2026-09-27: **Stage 2 implemented** (longer view distance, finest LOD, real far aircraft instead of the
  dots, distance haze, gradient sky, blended horizon), see "Stage 2 design" below. Verified headless on
  the DEMOAG film (cockpit and external views, `--widescreen`): the film plays the same events at the same
  tape times with Stage 2 on, Stage 2 off (`--view-dist 1 --no-lod-enh --no-haze-enh`) and `--classic`;
  `--check` passes; no new build warnings.
- 2026-09-27: **Stage 2 fixes after playtesting.** (1) Fatal "call through unregistered code pointer" on
  leaving a mission: the enhanced pass saved and restored `mem[]` while the audio thread kept running the
  game's sound service on it, so the service's writes made during the pass were undone (its state then
  pointed at stale data); the pass now holds the audio lock (`host_audio_lock`) from save to restore.
  That race is real, but it was **not** the cause of the reported crash (see the next entry). (2) Frame rate: the build has no build type
  (`-O0`); `src/enhanced` is now always compiled `-O2`, the host render and the composition run on all
  cores (`host_parallel`, bands of 32 rows), primitives are projected once and rasterised per band, and
  only the sample rows/columns under "world" pixels are drawn. Measured on the DEMOAG film (default
  settings, 1280×800, 2×2 AA, 16 threads, `AOTP_ENH_PROF=1`): per displayed frame compose 63–80 ms →
  4.2–4.8 ms (render 40–56 → 2.7–3.2 ms, resolve 22–25 → 1.5–1.6 ms); per sim frame the enhanced pass
  3.4–3.8 ms (of which 0.25 ms state copies), AP's own scene 0.6–1.4 ms. Headless the presentation now
  runs at the host's ~58 fps cap (was ~13). (3) The horizontal lines were only seen under the fatal
  dialog (a frozen, half-presented frame).
- 2026-09-27: the mission-end crash ("ap: call through unregistered code pointer 048B:417D (file
  028B:417D)") is **still open**: it was reported again after the audio-lock fix, when ending a flight
  from the cockpit. 028B:417D is the middle of an instruction in AP (`idiv` code of a routine that calls
  `277a:4f38`), so this is a garbage far pointer read from data, not a missing registration. It could not
  be reproduced headless in either mode (Scramble on deck, Dogfight a Famous Ace in the air; Esc → END
  MISSION ANYWAY / END MISSION → debrief → main menu; film exit; dummy and real audio): all reach the
  menu with `--classic`, `--view-dist 1 --no-lod-enh` and the defaults. The temporary pass-abort safety
  net was removed (it did not catch the crash and could hide bugs). The fatal message now also names
  the C caller (`caller codeptr_lookup±N`: add N to `nm aotp-enhanced.exe | grep " codeptr_lookup"`) to
  locate the call site at the next occurrence.
- 2026-09-27: **mission-end crash fixed** (a faithful-port bug, reproduced with `--classic`). It happens
  only in missions that load the LSO signal bitmaps (carrier missions: Single Mission → Training Mission →
  US Navy). `lso_free` (AP `50c0:004a`) was ported as `sprite_set_free(DS:11F0)`, but `DS:11F0` holds the
  bitmap table returned by `bmp_load_ems` and the original frees it directly with `bmp_free_ems`
  (`1d32:2170`). The port indexed the sprite-set table with that near pointer, freed an unrelated word
  (`DS:4744`, below the heap) into the near-heap free list, the next `calloc` (the packed-stream record
  opened by `end_screen` → `scr_load_planes("clouds")`) came from there and was overwritten, the stream
  then read past its limit (`pk_getbytes` with n ≈ FFxxh into `lzw_buf`, `DS:25A0`) and overwrote the
  video driver's jump table (`DS:351A`), so `gfx_nib_put` (entry 17) called 028B:417D. Fix in
  `sim_flow/cockpit.c` (`bmp_free_ems(DSW(0x11F0))`). Verified headless in `--classic` and enhanced:
  Training Mission (US Navy) Esc → END MISSION → END MISSION → debrief → main menu; Scramble Esc → END
  MISSION → END MISSION → debrief → main menu; DEMOAG film plays without a fatal; `--check` passes. The
  same fix belongs upstream in `../AcesOfThePacific` (`aotpport/src/sim_flow/cockpit.c` `lso_free`).

- 2026-09-29: launcher menus (File: Preferences with Always on top, Exit; Settings: Key Bindings; About),
  key bindings passed to the game with `--keys`, flaps down / flaps up keys instead of the one that cycles
  them (see "Input").

## Stage 1 design

Files: `src/enhanced/enhanced.h` (hooks), `enhanced.c` (options, pixel tags, capture of the 3D frame),
`enh_render.c` (host renderer, interpolation, composition), `enh_internal.h`.

**Options** (`main.c`): `--classic` (every enhancement off: 320×200 frame, all hooks no-ops, the faithful
port's picture and behaviour), `--res-scale N` (output 320×200 × N, default 4), `--aa N` (N×N samples per
output pixel for the 3D view, default 2), `--widescreen` (426×200 original pixels, shown 16:9).

**Capture** (read-only; nothing in `mem[]`, VGA memory or game state is written). Hooks, marked `ENH:`:

| Hook | Where | Records |
|---|---|---|
| `enh_scene_begin/end` | `scene.c render_scene` | a new snapshot (ring of 4): camera position `CS:0A13`, camera matrix `CS:09B6`, window, centre, focal, the page drawn into, the time |
| `enh_sky`/`enh_sky_end`/`enh_sky_callback` | `horizon.c draw_sky`, `call_cb` | sky/sea colours and haze bands (`031A..0320`); the sun/moon/star callback is not horizon |
| `enh_object` | `scene.c draw_object` | the object's far address = identity key for interpolation |
| `enh_ts_object` | `ts_draw.c ts_draw` after the draw mode | instance position (`CS:09EF` + camera), orientation (`m_obj_eye · m_cam^T`), eye scale (`>> k` of mode 3 / the E_POS16 path), shadow flag |
| `enh_poly`/`enh_poly_fill`/`enh_poly_done` | `r3d_poly.c part_draw_common`, `r3d_clip.c part_draw32` | each polygon's eye-space vertices (before the near clip) turned back to world axes, fill flag, fill/edge colours, in drawing order |
| `enh_tag` | `video/vid_priv.h vput`, `drv_vga.c drv_nib_or_hi`, `sim_flow/init.c span_fill_solid` | every VGA pixel write stamps a tag: 2D art, world (polygon / horizon fill), other scene primitive |
| `enh_compose` | `platform/vga.c compose` | composition (below) |

**Rendering** (`enh_render.c`), for every presented frame: the snapshot of the page on screen (n) and the
one before it (p). `x = (now − time n was first shown) / (t_n − t_p)`, clamped to 1: the view runs one sim
frame behind and only interpolates (the sim keeps its own step, `DS:711E` untouched). Camera position lerp,
camera matrix lerp + Gram-Schmidt; objects matched by (key, occurrence) are carried and turned to their
interpolated pose (shadows only carried); unmatched geometry is drawn as in frame n. Tracers (`bullet_draw`, hook `enh_tracer`) are not matched: each is
moved back along its own step (old − new) by 1 − x and drawn with its nearer near plane (focal/16), so a new
bullet is placed right too and tracers keep their original relation to the (lagging) view. No interpolation across
a window/projection change, a camera jump (> 30000 units or > ~45°) or a gap > 0.5 s. The horizon is drawn
analytically from the interpolated camera (sea below the eye plane, bands `spacing` pixels apart
perpendicular to the horizon, then sky+15). Polygons are near-clipped in eye space (focal/8), projected with
the original's perspective (`x = cx + ex·f/ey`, `y = cy − ez·f/ey`) in floating point, filled
at output × AA samples (palette indices; outlines and 2-point lines as ½-pixel strokes; sub-sample
polygons keep one sample, as the original always draws a pixel), resolved with the current DAC.

**Composition**: the 320×200 picture is scaled (nearest); where a pixel's tag is "world" and it lies inside
the snapshot's window, the high-resolution render is shown instead. Cockpit, HUD, VCR panel, text, dialogs
and every 2D screen therefore stay the original art, scaled; everything drawn over the 3D view keeps
covering it. With `--widescreen` the side columns show the render when the 3D window spans the full width,
black otherwise; the mouse is mapped back to the 320×200 screen (`host_set_screen_rect`).

Engine-side changes outside `src/enhanced/`: `host.c/h` (frame of any size up to 4096×2048, 200 lines
shown as 240 with `--aspect 4:3` whatever the size, linear filtering of a high-resolution frame, screen
rectangle for the mouse, developer aid `AOTP_CLICKS="<s>:<x>:<y>,…"` scripted left clicks), `vga.c`
(tags gathered next to the picture, output size), `main.c` (options), `CMakeLists.txt` (module).

Headless check used (film playback, needs the clicks): `SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
AOTP_SNAPSHOT_DIR=… AOTP_KEYS="10:e050,10.5:e050,11:e050,11.5:e050,12:1cp,12.4:1cr"
AOTP_CLICKS="25:172:153,32:50:132,38:222:156" ./aotp-enhanced.exe --game-dir Game --start 2` (Mission
Recorder → DEMOAG → Load, Play, Cockpit).

### Known limitations (Stage 1)

* Only the polygon world and the horizon are drawn again. Sprites/bitmaps (explosions, smoke, clouds drawn as
  bitmaps), discs, the sun, moon and stars, one-pixel far aircraft dots and the ground-speed dots are kept
  as the original's pixels (scaled, tag "scene") at the sim frame's position: they do not move smoothly and
  hide the high-resolution view under them.
* The composition mask is the 320×200 tag map of the sim frame: an object carried by the interpolation
  near the cockpit frame shows through at the frame's (low-resolution) edges only; the view lags the
  instruments by up to one sim frame (interpolation only, no extrapolation).
* `--widescreen`: the original culls objects against its 320-pixel window, so objects outside it are not
  in the side columns (they appear when their bounding sphere reaches the window); cockpit art ends at the
  320-pixel edges.
* The PS program's 3D (aircraft viewer) and the INTRO are not re-rendered (scaled only).
* Polygon outlines/lines are drawn ½ original pixel wide; the dither colour `DS:26CF` is ignored (AP's
  span fill does not dither).
* The launcher has no controls for the new options yet (pass them on the command line).

## Stage 2 design

Files: as Stage 1 (`src/enhanced/*`), plus hooks in `render3d/scene.c`, `effects.c`, `ts_draw.c` and an
undo journal in `ems.c/h`, all marked `ENH:`.

**Options** (`main.c`; `--classic` still turns everything off): `--view-dist N` (the engine's view ranges ×
N in the enhanced pass, 1..8, default 3; 1 = the original ranges), `--no-lod-enh` (keep the original LOD
choice), `--no-haze-enh` (no haze: Stage 1's banded sky, flat sea and hard horizon). With `--view-dist 1
--no-lod-enh` the enhanced pass is not run and the capture is Stage 1's.

**The enhanced pass** (rendering only). After AP's own `render_scene` (which draws the faithful 320×200
picture and the pixel tags as before), `render_scene` runs its body a second time with `enh_pass` set:

* `enh_pass_begin` saves all of `mem[]` (1.06 MB), the VGA memory, the pixel tags and the DGROUP stack
  pointer, and starts the EMS undo journal (`ems_journal_begin`: every logical page that a write-back
  would overwrite is saved first; the mapping and the handle table are saved); `enh_pass_end` puts all of
  it back. So the second pass leaves no trace in the game state, the VGA picture or the tags: the sim,
  the VCR and the faithful picture are unchanged (the film plays identically). `vcr_poll_panel` (input)
  is not called in the pass; clouds and bitmap instances (`bm_draw`) are skipped (not re-drawn by the host
  anyway).
* The snapshot of the frame is captured again in the same slot (same number and page), so the host
  renderer only sees the enhanced pass's geometry.
* Ranges (`enh_dist(d)` = d / N, `enh_range(r)` = r × N, only in the pass): aircraft (`R << 1 > d`),
  ships (250000), the aircraft range cull (`range_cull`, 200000), the class ranges / size tests of
  `class_visible` (moving and static objects), the static gather radii (2000000, radius + 4000000, the
  macro groups 1300000 / 2000000). The pass always gathers the static list anew (its list is not kept).
  `ts_visible_lod` keeps objects smaller than one original pixel (app size 0).
* Aircraft beyond the dot distance are drawn as their models (no one-pixel proxies). In AP's own pass the
  far dots' pixels are tagged "world", so the host render (with the model) shows through them: the Stage 1
  gap of the far dots keeping their original pixels is closed.
* LOD: the finest LOD of the shape (LODs are ordered coarse → fine, `formats/shapes.md` §3) at every
  distance, except the shadows' fixed LOD.

**Haze, sky, horizon** (`enh_render.c`). Each sample holds two palette indices and a mix (`pal[c]` mixed
with `pal[t]` by a/255, resolved with the current DAC):

* Haze colour = the horizon colour of the frame: the lowest band's colour (`sky + step/2`), `sky + 15`
  without bands. Haze amount `1 − exp(−dist / (250000 · N))` (world units, eye distance), per vertex,
  interpolated linearly across the polygon; lines and outlines take the polygon's mean.
* Sky: the bands are replaced by a gradient through the sky ramp: index `sky + step · d / spacing` (d =
  pixels above the horizon; equal to the original band colour at every band centre), from the lowest band's
  colour up to `sky + 15`, interpolated between neighbouring ramp entries.
* Sea: hazed with the distance of the sea point (camera height / ray), evaluated every 8 samples along a
  row and interpolated, so it fades into the horizon colour where the sky starts: the horizon line is
  blended.

### Known limitations (Stage 2)

* The draw list keeps the original's capacity (250 entries, 150 for statics) and the depth tree 256 nodes:
  with long ranges over dense areas the farthest statics may be left out of the enhanced pass.
* Beyond the original ranges only what the pass gathers is shown; bitmaps (clouds, smoke, explosions) and
  the sun/moon/stars are still the original's pixels, at their original ranges.
* The haze is a fixed exponential of the eye distance, not tied to the mission's weather; the ground is not
  shaded by distance other than the haze. Objects that the original culled pop in at the extended range
  (hidden by the haze for statics, visible as sub-pixel samples for aircraft).
* A far aircraft of less than one sample is a single sample (faint at 2×2 AA), where the original drew a
  full original pixel; it can be harder to see.
* The pass costs a second run of the scene code (~3.5 ms per sim frame), 2 × ~1.6 MB of copies and the
  finest LODs; the audio thread waits for it (the sound service is delayed by at most that long).
* Developer aid: `AOTP_ENH_PROF=1` prints mean times (AP's scene, the pass, the copies, host render,
  resolve, composition) every 2 s.
* If the EMS allocation changed during the pass (not seen), it cannot be undone; a warning is printed once.

## Input

* Direction keys (`platform/kbd.c kbd_dir_bits`): the original keeps one direction per key set (the last
  key pressed), so e.g. Down held with Left does nothing until Left is released. Enhanced: every held key
  of the set is combined (up 1, down 2, left 4, right 8; opposite keys cancel), which the flight input
  already accepts as diagonals. `--classic` keeps the original behaviour.
* Flaps (`sim_flow/frame.c commands`): the original's F cycles the flaps up, half, full, up. Enhanced: a
  flaps down key (F) and a flaps up key (Shift+F), one step each, stopping at full / up. They reach AP as
  scancodes 56h / 55h, which no keyboard sends and which have no ASCII or direction bits in AP's tables;
  `controls_map` turns them into commands 61h / 60h. `--classic` keeps F cycling.
* Key bindings (`enhanced/enh_keys.c`, hook in `host.c key_event`; the launcher's Settings > Key Bindings):
  `--keys name=code,...` lists the actions moved from their default keys (code = XT make code, 100h = E0
  prefix, 1000h Shift, 2000h Ctrl, 4000h Alt; 0 = no key). While AP runs, a key bound to an action is sent
  as the action's own key, with Shift / Ctrl / Alt pressed or let go around it as that key needs; a key
  that is the default of an action bound elsewhere is dropped; any other key goes through. Modifier keys
  always go through. INTRO and PS (menus, typed names) get every key as it is. The launcher keeps Esc, Tab,
  Enter, the number keys, F1 - F6 and Ctrl / Alt + F1 - F10 on their jobs; the keypad stays a second
  stick.
