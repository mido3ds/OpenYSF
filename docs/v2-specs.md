# OpenYSF v2 Specification — "Cockpit Experience"

Last updated: 2026-07-08

## Goal

Polish the YS-11 into one fully flyable experience with a proper instrument panel and cockpit view. v2 milestone: **sit inside, fly with instruments, see the world from the pilot's seat.**

---

## Feature 1: Full HUD

Replace the current minimal text HUD (SPD/ALT/THR/GEAR) with graphical instruments rendered via new `canvas::hud` primitives. Always visible regardless of camera mode.

### Instruments

| Instrument | Detail | Implementation |
|---|---|---|
| ADI (Attitude Director Indicator) | Full artificial horizon: pitch ladder with 5°/10° increments, bank indicator chevron at top, sky/ground half with gradient | New `canvas::hud::Circle`, `canvas::hud::Line`, `canvas::hud::FilledArc` primitives with orthographic projection |
| Heading indicator | Circular compass rose with N/S/E/W labels, 10° tick marks, current heading displayed | New `canvas::hud` primitives |
| VSI (Vertical Speed Indicator) | Analog gauge with arc from -6 to +6 (1000s ft/min), needle pointing at current rate. Tick marks at 1, 2, 3, 4, 6 | New `canvas::hud` primitives |

### Canvas HUD Primitives

Add to `canvas.h`:

- `canvas::hud::Circle` — ring/stroke
- `canvas::hud::Line` / `canvas::hud::LineStrip` — 2D screen-space lines
- `canvas::hud::FilledArc` / `canvas::hud::FilledTriangle` — filled shapes
- `canvas::hud::Text` (existing) — label instruments

Render pass: after existing `hud_text`, before ImGui. Depth test disabled, blending enabled.

No new shaders — reuse orthographic projection from `hud::Text`.

### Out of Scope for v2 HUD

- AoA indicator, radar altimeter, nav instruments (VOR/ILS/ADF) — deferred to v3
- Gyro drift simulation (heading indicator is perfect for now)

---

## Feature 2: Cockpit View

Camera positioned at pilot eyepoint with full EXCAMERA cycling. Cockpit SRF mesh rendered.

### EXCAMERA

- DAT's EXCAMERA positions are already parsed by `datmap_get_excameras()` — dead code, never called
- Wire it up: call `datmap_get_excameras()` on tracked aircraft's DATMap
- Key press cycles through all EXCAMERA positions (PILOT → CO-PILOT → REAR → back to orbit)
- EXCAMERA offset transformed by aircraft orientation (quaternion) to compute camera position
- Camera front = aircraft front direction (from quaternion)
- Near clip may need adjustment (`PerspectiveProjection::near = 0.1` → possibly `0.01`)

### Cockpit Mesh

- Cockpit SRF files exist on disk for every aircraft (e.g. `ys11cockpit.srf`) and are referenced in `AircraftTemplate::cockpit` — dead field, never loaded
- Load cockpit SRF via `srf_from_file()` at aircraft load time
- Render cockpit mesh as part of `aircrafts_prepare_render()` — separate draw call with reversed face winding (you're inside looking out) or disabled back-face culling

### Camera Mode

- New mode in `camera_update()`: when `excamera_index >= 0`, position camera at EXCAMERA offset instead of orbiting
- Cycle: orbit (default, index=-1) → first EXCAMERA (index=0) → second (index=1) → last → back to orbit
- Camera FOV may need adjustment for cockpit view

### Out of Scope for Cockpit View

- Head movement / bobbing — deferred to v3
- Cockpit instrument panel interaction (clickable switches) — deferred to v3
- Tower view, fly-by camera — deferred to v3

---

## Data Model Changes

In `Aircraft` struct (`aircraft.h`):

- **Add** `int excamera_index = -1` — current EXCAMERA index (-1 = orbit mode)
- **Add** `Model cockpit_model` — loaded cockpit SRF data
- **No changes** to Camera struct (EXCAMERA positions accessed from Aircraft's DATMap, not stored on Camera)

---

## Player Controls

| Action | Key |
|---|---|
| Cycle camera (orbit → cockpit views) | `C` key (new event in `Events`) |

---

## Quality

| Decision | Choice |
|---|---|
| Testing | No new automated tests for v2. HUD is visual, cockpit is camera math — manual testing. Startup assertion pattern from v1 does not extend to rendering. |
| Tuning target | YS-11 only. No other aircraft tuned. |

---

## Out of Scope (v3+)

- Stall / spin aerodynamics
- Terrain textures
- AoA indicator, radar altimeter, nav instruments
- Tower / chase-cam views
- Head bobbing / cockpit interaction
- All code quality / refactoring
- Multiple aircraft tuning
- Collision / damage
- Audio improvements
- AI / missions
- Multiplayer
- Graphics polish
- Platform fixes
