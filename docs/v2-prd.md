# OpenYSF v2 PRD — Cockpit Experience

## Problem Statement

OpenYSF v1 delivered a playable flight model with believable rotational dynamics, ground handling, thrust-pitch coupling, and audible engine sounds. Flying the YS-11 works. But the sim still feels like a tech demo because you're flying from outside the plane watching a flat-colored mesh, with a four-line text overlay for instruments. There is no cockpit view, no artificial horizon, no attitude indicator — you cannot fly instruments, you cannot see the world from inside the aircraft, and the HUD gives you numbers instead of the visual cues real pilots use.

Two gaps prevent v1 from feeling like a complete sim: no instrument panel (you're guessing attitude from the outside view) and no cockpit view (you're always orbiting yourself). Together they keep the player outside the experience rather than inside it.

## Solution

Replace the minimal text HUD with a full graphical instrument panel rendered via new 2D primitives on the existing canvas. Add a cockpit camera mode that positions the view at the pilot's eyepoint using EXCAMERA data already parsed from DAT files, and renders the cockpit SRF mesh that already ships with every aircraft.

The aircraft stays on the quaternion rigid-body model from v1. No physics changes. The v2 value is entirely in what the player sees and where the player sits.

## User Stories

1. As a pilot, I want an attitude director indicator (ADI) showing pitch ladder and bank angle, so that I can fly straight and level, climb, descend, and turn without looking at numbers.
2. As a pilot, I want a heading compass rose with cardinal directions and tick marks, so that I know which way the aircraft is pointed at a glance.
3. As a pilot, I want a vertical speed indicator (VSI) showing climb and descent rate on an analog gauge, so that I can manage altitude changes smoothly.
4. As a pilot, I want the new instruments to replace the current text-only HUD (SPD/ALT/THR/GEAR), so that the screen is not cluttered with both.
5. As a pilot, I want the instruments visible in all camera modes (orbit and cockpit), so that I always have flight data regardless of the view I choose.
6. As a pilot, I want to press a key and switch from the orbit camera to the cockpit view, so that I can fly from inside the aircraft.
7. As a pilot, I want the cockpit view to use the pilot eyepoint position defined in the aircraft's DAT file (EXCAMERA), so that the view matches the real aircraft geometry.
8. As a pilot, I want to cycle through all available EXCAMERA positions (pilot, co-pilot, rear) by pressing the same key again, so that I can switch seats without leaving the sim.
9. As a pilot, I want to see the cockpit interior mesh rendered around me in cockpit view, so that I feel like I am sitting in the actual aircraft.
10. As a pilot, I want the cockpit view camera to track the aircraft's orientation (pitch/roll/yaw), so that when the aircraft banks the view banks with it realistically.
11. As a pilot, I want to press the camera key one more time after the last EXCAMERA position to return to the orbit view, so that the camera cycle is complete round-trip.
12. As a pilot, I want the ADI to show a clear sky/ground split that rotates with the aircraft roll, so that I can instantly tell my attitude relative to the horizon.
13. As a pilot, I want the pitch ladder on the ADI to have markings at 5 and 10 degree increments, so that I can make precise pitch adjustments.
14. As a pilot, I want a bank indicator chevron at the top of the ADI, so that I can set and hold a specific bank angle.
15. As a pilot, I want the heading compass to show N/S/E/W labels with 10 degree tick marks, so that I can read heading precisely.
16. As a pilot, I want the VSI to show an arc from -6000 to +6000 ft/min with labeled ticks at key values (1, 2, 3, 4, 6 thousands), so that I can set a specific climb rate.
17. As a developer, I want HUD rendering to use new canvas::hud primitives (circles, lines, filled shapes) not ImGui, so that the decoupled canvas pipeline is preserved and HUD rendering stays in the deferred render pass.
18. As a developer, I want no new shaders for the HUD — reuse the existing orthographic projection used by canvas::hud::Text — so that the rendering footprint does not grow.

## Implementation Decisions

### Instrument Rendering — New canvas::hud Primitives

The current Canvas pipeline has one screen-space HUD primitive: `canvas::hud::Text`, rendered via orthographic projection. v2 adds three new primitive types:

- `canvas::hud::Circle` — stroked circle (center, radius, color, line_width)
- `canvas::hud::Line` / `canvas::hud::LineStrip` — 2D screen-space line (p0, p1, color, line_width)
- `canvas::hud::FilledArc` / `canvas::hud::FilledTriangle` — filled shapes (color)

All new primitives use the existing orthographic projection from `hud::Text`. No new shaders. The render order slots these after existing `hud_text` and before ImGui, with depth test disabled and blending enabled.

Each instrument is a function that computes positions from aircraft state (attitude, heading, VSI rate) and pushes the appropriate primitives to the canvas. Instrument layout is fixed: ADI left-center, compass top-center, VSI right-center. The current minimial text HUD (SPD/ALT/THR/GEAR) is replaced entirely — the instruments make the text overlay redundant.

### EXCAMERA — Already Parsed, Never Consumed

`datmap_get_excameras()` in `assets.h` fully parses EXCAMERA blocks from DAT files into a `Vec<ExternalCameraLocation>` with position, angles, and inside/outside flag. This function exists but is never called by any system code. v2 wires it up: on aircraft load or camera cycle, call `datmap_get_excameras()` on the tracked aircraft's DATMap and store the result.

### Cockpit Mesh — Already Referenced, Never Loaded

Every aircraft has a cockpit SRF file on disk (e.g. `ys11cockpit.srf`, `f1cockpit.srf`), referenced in `AircraftTemplate::cockpit` (the 4th column of `air*.lst`). This field is parsed and stored but the file is never opened. v2 loads the cockpit SRF via `srf_from_file()` at aircraft load time and stores it as a `Model` on the Aircraft struct.

### Camera — Third Mode

The camera currently has two modes selected in `camera_update()`: tracking (orbit) when `camera.aircraft != nullptr`, and flying (FPS) otherwise. v2 adds a third mode: cockpit view. The Aircraft struct gains `excamera_index` (-1 for orbit, 0..N for EXCAMERA positions). When `excamera_index >= 0`:

- Camera position = aircraft translation + EXCAMERA offset rotated by aircraft quaternion
- Camera front = aircraft front direction (from quaternion)
- Cockpit mesh rendered as a mesh draw call with reversed face winding (viewer is inside looking out)
- Near clip plane reduced from 0.1 to 0.01 to prevent cockpit interior clipping

The `C` key (a new event in the Events struct) cycles the index: -1 → 0 → 1 → ... → N → -1. The camera key is added alongside existing camera control events in the keyboard handler.

### Data Model Changes

On `Aircraft` struct:
- **Add** `int excamera_index = -1` — which EXCAMERA position is active
- **Add** `Model cockpit_model` — loaded cockpit SRF mesh data

On `Events` struct:
- **Add** `bool camera_cycle` — key press to cycle camera modes

No changes to `Camera` struct. EXCAMERA positions live on the Aircraft's DATMap and are accessed via `datmap_get_excameras()` on demand.

### Instrument Layout

Fixed positions in normalized [0,1] screen coordinates:

- **ADI**: centered at (0.25, 0.50), radius ~0.08 — left side
- **Heading compass**: centered at (0.50, 0.90), radius ~0.06 — top center
- **VSI**: centered at (0.75, 0.50), radius ~0.05 — right side

These are initial positions and can be adjusted during implementation.

## Testing Decisions

### What to Test

Test the EXCAMERA parsing and camera positioning math — deterministic, can be verified with startup assertions. Same pattern as `test_rotational_physics()` from v1.

### What Not to Test

HUD instruments are visual — not worth automated testing. Cockpit mesh rendering is pure asset loading with no derived behavior. The simulator is the integration test for rendering.

### Prior Art

`test_rotational_physics()` in `src/math.h:484-665` — inline function in `main()`, `mu_assert` on failure, no framework, no new build target. v2 tests follow the same pattern.

## Out of Scope

- Stall / spin aerodynamics and recovery
- Terrain textures (TEXMAN, TEX MAIN, TXL/TXC)
- AoA indicator, radar altimeter, nav instruments (VOR/ILS/ADF)
- Tower camera view, chase-cam refinements
- Head bobbing / cockpit panel interaction
- Multiple aircraft tuning beyond YS-11
- Collision detection and damage
- Audio improvements (wind sound, stall horn)
- AI / mission / AIRROUTE system
- Multiplayer networking
- Graphics polish (shaders, post-processing, shadows)
- Codebase refactoring (extract systems, split assets.h)
- Platform build fixes
- Editor / axis gizmo

## Further Notes

- v2 target aircraft is **YS-11** (default at startup). Its cockpit SRF and EXCAMERA data exercise both new features completely. No other aircraft will be tuned.
- HUD changes are confined to new primitives in `canvas.h`/`canvas.cpp` and a new rendering function in `src/hud.cpp` (or inline in `aircraft.cpp` if it stays small). No changes to physics, assets.h, or parser code.
- Cockpit view changes touch `camera.h`/`camera.cpp` (new mode), `aircraft.h` (new fields), and `aircraft.cpp` (load cockpit mesh, render it). EXCAMERA consumption is a few lines in the camera system. No changes to DAT parsing, the parser, or existing physics.
- The full dynamic pressure torque gap from v1 (`v²/max_v²` instead of `0.5 * ρ * v² * wing_area`) remains deferred to v3. v2 does not touch the torque pipeline.
