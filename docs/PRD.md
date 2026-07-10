# OpenYSF Product Requirements Document

Last updated: 2026-07-10

## Problem Statement

OpenYSF is a YS Flight Simulator 2000 clone implemented as a single-file C++ monolith with SDL2 + OpenGL 3.3 + ImGui. It reads native YS formats (FLD, STP, DAT, DNM, SRF) and currently implements a playable flight model with full rigid-body rotational dynamics, ground handling, and audible engine sounds. The YS-11 is flyable — you can take off, fly with believable feel, and land.

But the sim still feels incomplete. You fly from outside the plane watching a flat-colored mesh with a four-line text overlay and a few graphical instruments. There is no camera mode diversity (only orbit and cockpit EXCAMERA views), no chase-cam to watch the aircraft in flight, no tower view at airports, no terrain textures (terrain meshes are flat-shaded), and no angle-of-attack gauge on the HUD. Flying the YS-11 works, but the visual feedback and camera flexibility of the original YS Flight Simulator are missing.

Beyond these gaps, the original YS shipped with 150+ aircraft spanning fighters, heavy transports, small props, and helicopters. OpenYSF currently only tunes the YS-11. The flight envelope is incomplete — no stall/spin behavior, no altitude-dependent dynamic pressure torque, no ground effect, no compressibility. There is no AI traffic, no mission system, no multiplayer, no collision or damage model, and no water rendering.

The long-term goal is parity with the original YS Flight Simulator experience: a complete flyable sim with textured terrain, diverse camera views, full instrument panel, multiple flyable aircraft, AI traffic, and a mission system.

## Solution

OpenYSF is built incrementally across milestones. Each milestone delivers a demoable vertical slice that makes the sim feel more complete:

- **v1 (delivered)**: Replace the point-mass Euler model with quaternion rigid-body dynamics. Ground handling, thrust-pitch coupling, audible engine+propeller sounds, minimal text HUD (airspeed, altitude, throttle, gear, brakes).
- **v2 (delivered)**: Graphical HUD instruments (ADI, heading compass, VSI) using new canvas primitives. EXCAMERA cockpit view mode (F10/F11) for flying from inside the aircraft.
- **v3 (in progress)**: Chase-cam (F9), tower view cycling (F12), terrain textures from FLD TEXMAN blocks, AoA indicator on HUD.
- **v4+ (future)**: Cockpit mesh rendering, stall/spin aerodynamics, full dynamic pressure torque, nav instruments, multiple aircraft tuning, collision/damage, AI missions, multiplayer.

## User Stories

### Physics & Flight Model

1. As a pilot, I want the aircraft to rotate smoothly in pitch/roll/yaw with believable inertia, so that maneuvers feel weighty and physical rather than snapped.
2. As a pilot, I want the control stick to produce torque proportional to airspeed, so that the aircraft feels responsive at high speed and sluggish at low speed.
3. As a pilot, I want adding power to pitch the nose up/down appropriately, so that throttle changes affect attitude as they would in a real aircraft.
4. As a pilot, I want to accelerate down the runway and rotate the aircraft at a reasonable speed, so that takeoff is a distinct phase of flight.
5. As a pilot, I want to touch down and roll out with brakes and rudder steering, so that landing feels complete.
6. As a pilot, I want torque to vary with altitude through air density changes, so that high-altitude handling feels different from sea level.
7. As a pilot, I want the aircraft to stall at high angle of attack with realistic post-stall behavior, so that the flight envelope has a dangerous edge.
8. As a pilot, I want asymmetric stalls to produce spin/flat-spin behavior, so that departure from controlled flight is physically credible.
9. As a pilot, I want ground effect to produce extra lift and reduced induced drag near the runway, so that flare and touchdown feel realistic.
10. As a pilot, I want P-factor and propeller torque effects at high power and low airspeed, so that the aircraft yaws and rolls with throttle changes.

### Audio

11. As a pilot, I want engine and propeller sounds that scale with throttle, so that the cockpit feels alive and speed changes are audible.
12. As a pilot, I want speed-dependent wind noise in cockpit view, so that I can feel the aircraft's velocity aerodynamically.
13. As a pilot, I want a stall warning horn that sounds at high angle of attack, so that I have an audible cue before departure.
14. As a pilot, I want mechanical sounds for gear and flap extension/retraction, so that control changes are confirmed audibly.

### HUD & Instruments

15. As a pilot, I want to see my airspeed, altitude, throttle position, landing gear state, and brake state on screen, so that I can fly without guessing.
16. As a pilot, I want an attitude director indicator (ADI) showing pitch ladder and bank angle, so that I can fly straight and level, climb, descend, and turn without looking at numbers.
17. As a pilot, I want a heading compass rose with cardinal directions and tick marks, so that I know which way the aircraft is pointed at a glance.
18. As a pilot, I want a vertical speed indicator (VSI) showing climb and descent rate on an analog gauge, so that I can manage altitude changes smoothly.
19. As a pilot, I want an angle of attack indicator with a vertical strip gauge, so that I can monitor proximity to stall visually.
20. As a pilot, I want the text HUD and graphical instruments displayed together in all camera modes, so that I always have flight data regardless of the view.
21. As a pilot, I want a radar altimeter for low-altitude precision, so that I know exact height above terrain during landing.
22. As a pilot, I want nav instruments (VOR, ILS, ADF needles) for instrument approaches, so that I can fly procedures in low visibility.
23. As a pilot, I want the ADI to show a clear sky/ground split that rotates with the aircraft roll, so that I can instantly tell my attitude relative to the horizon.
24. As a pilot, I want the pitch ladder on the ADI to have markings at 5 and 10 degree increments, so that I can make precise pitch adjustments.
25. As a pilot, I want a bank indicator chevron at the top of the ADI, so that I can set and hold a specific bank angle.
26. As a pilot, I want the heading compass to show N/S/E/W labels with 10 degree tick marks, so that I can read heading precisely.
27. As a pilot, I want the VSI to show an arc from -6000 to +6000 ft/min with labeled ticks at key values (1, 2, 3, 4, 6 thousands), so that I can set a specific climb rate.

### Camera & Views

28. As a pilot, I want the orbit camera to track the aircraft correctly, so that I can see what I'm doing during maneuvers.
29. As a pilot, I want a free-flying (FPS) camera for exploring the scenery without an aircraft, so that I can look around the world.
30. As a pilot, I want to press a key and view the aircraft from behind and above (chase-cam), so that I can see the aircraft in flight from a formation perspective.
31. As a pilot, I want to press a key and view the scene from a tower position in the scenery, so that I can see the aircraft approach from the airport's perspective.
32. As a pilot, I want to press a key and switch to the cockpit view, so that I can fly from inside the aircraft when I prefer immersion.
33. As a pilot, I want to cycle through all available EXCAMERA positions (pilot, co-pilot, rear) by pressing the same key, so that I can switch seats without leaving the sim.
34. As a pilot, I want the camera to cycle back to orbit after the last camera position, so that the camera cycle is complete round-trip.
35. As a pilot, I want the cockpit view camera to track the aircraft's orientation (pitch/roll/yaw), so that when the aircraft banks the view banks with it realistically.
36. As a pilot, I want mouse-wheel zoom to work in chase-cam mode, so that I can adjust the following distance.
37. As a pilot, I want a cinematic fly-by camera that smoothly tracks the aircraft from a fixed external point, so that I can record or watch dramatic passes.

### Scenery & Terrain

38. As a pilot, I want terrain meshes to show road-mask textures from the original YS scenery files, so that the world looks like it did in the original sim.
39. As a pilot, I want terrain side colors (BOT/RIG/LEF) rendered on the sides of terrain blocks, so that terrain cliffs and edges are visible.
40. As a pilot, I want water surfaces rendered where FLD files declare water areas, so that lakes and oceans are visually distinct from land.
41. As a pilot, I want airport markings (runway lines, taxiway markings) rendered as 2D primitives on the ground, so that airports look complete.
42. As a pilot, I want fog to obscure distant terrain gradually, so that the world has atmospheric depth.

### Aircraft & Tuning

43. As a pilot, I want to fly multiple aircraft types (fighters, heavies, small props) with tuned flight models, so that I can experience different handling characteristics.
44. As a pilot, I want the F-16C to feel responsive and fast with afterburner effects, so that fighter-type aircraft are distinguishable from the YS-11.
45. As a pilot, I want helicopters with rotor-physics to be flyable, so that the full YS aircraft roster is supported.

### Quality of Life

46. As a pilot, I want the application to start up quickly and validate its core math on launch, so that I catch regressions early.
47. As a developer, I want the source code structured into logical files per domain, so that the codebase is navigable and maintainable.
48. As a developer, I want the large monolithic headers (assets.h, main.cpp) split into smaller domain-specific files, so that compilation is faster and code is easier to find.
49. As a developer, I want unit tests for the physics math (AoA, lift/drag, AABB) to prevent regressions, so that the flight model stays reliable across changes.

### Multiplayer & Content

50. As a pilot, I want to fly online with other players and see their aircraft interpolated smoothly, so that the sim supports group flying.
51. As a pilot, I want AI aircraft traffic following AIRROUTE waypoints from FLD files, so that the world feels alive.
52. As a pilot, I want a mission/campaign system with scoring and objectives like the original YS, so that there is structured content beyond free flight.

## Implementation Decisions

### Architecture

OpenYSF is a single `World` struct with free-function systems (`sys::name(World&)`) called in a fixed update order each frame. The main loop runs: timer → events → projection → camera → cached matrices → scenery → aircraft → ground objects → collision → canvas rendering → ImGui → swap.

There is no entity-component system, no scripting language binding, and no external physics engine. The entire codebase is C++20 using GLM for math, SDL2 for window/input/audio, OpenGL 3.3 via GLAD for rendering, ImGui + ImPlot for debug UI, and FreeType for text rendering.

### Physics — Implemented

**Orientation**: `glm::quat` internally. YS Euler angles (uint16 0x0000–0xFFFF) are converted to quaternion at load time and back to Euler for rendering code. A read-only `LocalEulerAngles` accessor on Aircraft provides the backward-compatible view. No parser or file format changes were needed.

**Inertia tensor**: Computed once at aircraft load from the bounding box AABB (uniform-density box). Pre-inverted and stored as `glm::mat3`. Formula: `diag( (w²+h²)/12 * mass, (d²+h²)/12 * mass, (w²+d²)/12 * mass )`.

**Control torque model**: Per-axis torque proportional to dynamic pressure. Torque = control deflection × efficiency × 0.5 × ρ × v² × wing_area. The initial implementation uses v²/max_v² scaling instead of full dynamic pressure — air density and wing area factors are absent from the torque path. Functionally equivalent at sea level for YS-11 but torque doesn't change with altitude.

**Thrust-pitch coupling**: Thrust applied with a moment arm offset from CG: torque += cross(thrust_arm, thrust_vector). Thrust arm is a constant per aircraft type read from the DAT file.

**Ground handling**: Rolling friction opposes horizontal velocity. Rudder-steer on ground (yaw torque scaled by a ground efficiency factor). Elevator pitch boost at takeoff speed simulates main-gear lever arm rotation. Brakes mapped to B key (state-based). Landing gear toggle to G key (edge-triggered). All ground effects deactivate 1m above ground for clean flight behavior.

**Adverse yaw**: Right aileron deflection induces left yaw via an ADVERSE_YAW_COEFF constant. This captures the primary yaw-roll coupling without a full drag-asymmetry model.

**AoA**: Computed from the aircraft's orientation quaternion via `aircraft_angle_of_attack()` in aircraft.h. Uses the front and up vectors to determine angle between nose and velocity vector.

### Audio — Implemented

The SDL callback was rewritten from `SDL_MixAudioFormat` to float-sample accumulation. Per-source gain scaling with output conversion to the device's native format (U16, stereo, 22050Hz). No new dependencies. Engine sounds map throttle position to a blend of engine0-9.wav and prop0-9.wav samples loaded from assets/sound/.

### HUD — Implemented

**Text layer (v1)**: Airspeed (km/h), altitude (ft), throttle %, landing gear state, and brake indicator via existing `canvas::hud::Text`. No new shaders.

**Graphical instruments (v2)**: Three instruments rendered via new `canvas::hud` primitives, visible in all camera modes:
- ADI: artificial horizon with pitch ladder (5°/10° increments), bank indicator chevron at top, sky/ground half with gradient. Uses FilledArc + Line + FilledTriangle + Circle.
- Heading compass: circular rose with N/S/E/W labels, 10° tick marks, current heading displayed. Uses Circle + Line + Text.
- VSI: analog gauge from -6000 to +6000 ft/min, needle at current rate, ticks at 1/2/3/4/6 thousands. Uses FilledArc + Line + Text + Circle.

**Canvas primitives added**: Circle (stroked), Line/LineStrip (screen-space 2D), FilledArc/FilledTriangle (filled shapes). All use the existing orthographic projection from hud::Text. No new shaders. Render order: after hud_text, before ImGui. Depth test disabled, blending enabled.

**Instrument layout** (normalized [0,1] screen coordinates, adjustable):
- ADI: (0.25, 0.50), radius ~0.08
- Heading compass: (0.50, 0.90), radius ~0.06
- VSI: (0.75, 0.50), radius ~0.05

### HUD — Planned (v3+)

**AoA indicator (v3)**: Vertical strip gauge — thin bar (~15px wide) with scale from -5° to +25°, labeled at 0°, 10°, 20°. Moving marker (filled triangle) pointing at current AoA value. Positioned near existing airspeed/altitude text. Uses existing Line, Text, and FilledTriangle primitives.

**Future instruments (v4+)**: Radar altimeter, VOR/ILS/ADF nav instrument needles, AoA indexer with approach reference markers.

### Camera — Implemented

Four camera modes exist, dispatched by testing fields on the tracked Aircraft:

1. **Orbit (tracking)**: Camera orbits around the aircraft. Yaw/pitch controlled by arrow keys. Zoom distance proportional to AABB size × zoom_multiplier. Camera up follows aircraft up.
2. **EXCAMERA**: Camera positioned at DAT-defined EXCAMERA locations (pilot, co-pilot, rear). Cycle with F10. Camera position = aircraft translation + EXCAMERA offset rotated by aircraft quaternion. Camera front = aircraft front direction.
3. **Cockpit**: Camera at pilot eyepoint (COCKPITP position). Near clip plane reduced to 0.01. Camera tracks aircraft orientation. Toggle with F11.
4. **Flying (FPS)**: Free WASD movement with mouse-look when no aircraft is tracked.

Camera mode tracking currently uses two fields on Aircraft (`excamera_index`, `cockpit_mode`) inferred at runtime in `camera_update()`. This is an ad-hoc approach that doesn't scale well to new modes.

### Camera — Planned (v3+)

**Data model refactor (v3)**: Replace ad-hoc mode tracking with a single `CameraMode` enum on the Camera struct:
```cpp
enum class CameraMode { Orbit, Chase, Tower, EXCAMERA, Cockpit };
struct Camera {
    CameraMode mode = CameraMode::Orbit;
    int camera_index = -1;
    // ... existing fields ...
};
```
The Aircraft fields `excamera_index` and `cockpit_mode` will be removed (moved to Camera).

**Chase-cam (v3)**: Camera positioned behind and above the aircraft tail, always looking at the aircraft. Offset = orientation × (-front × distance + up × height_offset). Initial distance from zoom_multiplier × AABB_height. Mouse wheel adjusts distance. Toggle with F9.

**Tower view (v3)**: Collect VIEW_POINT FieldRegion entries from scenery Field hierarchy. Camera at viewpoint position, looking toward the aircraft. Cycle with F12. No viewpoints available → F12 does nothing.

**Future camera features (v4+)**: Cockpit mesh rendering (SRF loaded but not rendered due to GL_CULL_FACE/alignment). Head bobbing. Cinematic fly-by camera.

### Scenery & Terrain — Implemented

FLD files are parsed into a recursive Field tree containing:
- Ground: infinite raycast plane with field-specified color, tiled with groundtile.png as greyscale overlay
- TerrMesh: heightfield meshes with vertex colors (per-face or gradient-interpolated)
- Picture2D: 2D colored primitives (runway markings, taxiway lines, etc.)
- FieldRegion: runways and viewpoints with min/max XZ bounds
- GroundObjSpawn: ground vehicle spawn definitions

The infinite ground plane uses a single-channel (GL_RED) tiled texture from groundtile.png. Terrain meshes are purely vertex-colored — no texture support. The TEXMAN and TEX MAIN blocks in FLD files are currently skipped with parser stubs.

### Scenery & Terrain — Planned (v3+)

**Terrain textures (v3)**: Parse TEXMAN blocks from FLD files. These embed PNG images as inline base64-encoded data:
```
TEXMAN TEXTURE "TextureName"
TEXMAN TEXTUREFORMAT .png
TEXMAN TEXTUREFILE "filename.png"
TEXMAN TEXTUREFILTER LINEAR
TEXMAN TEXTUREDATA <base64_length>
<base64 data lines...>
TEXMAN ENDTEXTURE
```
TerrMesh assignment via `TEX MAIN "TextureName"`. Decode base64 → PNG → OpenGL texture via IMG_Load_RW. Store on Field. Generate XZ-projected UVs. Modify mesh shader for multiply-blend: frag_color = vertex_color × texture(terrain_tex, uv).

Known textures across shipped scenery:
- Hawaii: OahuRoadMask, HawaiiNorth, HawaiiSouth (road-mask overlays)
- Aomori: Hummingbird (decorative)

**Future terrain features (v4+)**: Multi-texture blending by altitude/slope. Terrain side color rendering. Water surfaces for AreaKind::WATER. Airport marking DST primitives. SPEC flag per terrain patch. Fog tuning.

### EXCAMERA — Implemented

`datmap_get_excameras()` in assets.h fully parses EXCAMERA blocks from DAT files into a `Vec<ExternalCameraLocation>` with position, angles, and inside/outside flag. This function existed as dead code and is now wired up: called on aircraft load, stored on Aircraft. F10 cycles through the positions.

### Cockpit Mesh — Loaded, Not Rendered

Every aircraft has a cockpit SRF file on disk, referenced in AircraftTemplate::cockpit (4th column of air*.lst). The SRF is loaded into Aircraft::cockpit_model at aircraft load time. However, rendering is deferred due to unresolved visibility issues with GL_CULL_FACE and mesh alignment to the pilot eyepoint. The render guard checks `cockpit_view_index >= 0 && !cockpit_model.meshes.empty()`.

### Controls

| Key | Action | Status |
|---|---|---|
| F9 | Toggle chase-cam | v3 planned |
| F10 | Cycle EXCAMERA views (orbit → pilot → co-pilot → ... → orbit) | v2 implemented |
| F11 | Toggle cockpit view | v2 implemented |
| F12 | Cycle tower viewpoints | v3 planned |
| Arrow keys | Orbit camera yaw/pitch (tracking mode) | implemented |
| WASD | FPS camera movement (flying mode) | implemented |
| Q/A | Throttle increase/decrease | implemented |
| Tab | Afterburner toggle | implemented |
| G | Landing gear toggle | implemented |
| B | Brakes toggle | implemented |
| C/c | Rudder control (yaw) | implemented |
| Mouse | Tracking camera rotation (flying mode) | implemented |
| Mouse wheel | Chase distance (v3) / zoom | v3 planned |

### Data Model

**Current implemented state:**

On `Aircraft` struct:
- `glm::quat orientation` — replaces old LocalEulerAngles
- `glm::vec3 angular_velocity, torque` — rotational dynamics state
- `glm::mat3 inertia_tensor_inv` — pre-inverted inertia tensor
- `glm::vec3 thrust_offset` — moment arm for thrust-pitch coupling
- `float wheelbase` — main gear position for ground pitch boost
- `int excamera_index` — current EXCAMERA position (-1 = orbit)
- `mu::Vec<ExternalCameraLocation> excameras` — parsed EXCAMERA positions
- `bool cockpit_mode` — cockpit view active
- `glm::vec3 cockpit_pos` — cockpit eyepoint offset
- `Model cockpit_model` — loaded cockpit SRF data
- `float landing_gear_alpha` — 0 = down, 1 = up
- `bool braking` — brake state
- `engine` struct: speed_percent, burner_enabled, cutoff, max/idle power, fuel flow
- `forces` struct: thrust, airlift, drag, weight
- `mass` struct: clean, load, fuel (tons)
- CL/CD coefficient structs with quadratic functions and critical AoA values
- Animation flags: has_propellers, has_afterburner, has_high_throttle_mesh
- Should/loaded/removed flags, debug rendering flags

On `Events` struct:
- `bool camera_cycle` — F10 key to cycle camera modes
- `bool cockpit_toggle` — F11 key to toggle cockpit view

**Planned changes (v3):**

- Add `CameraMode` enum and `mode` + `camera_index` to `Camera` struct (camera.h)
- Remove `excamera_index` and `cockpit_mode` from `Aircraft` struct (aircraft.h)
- Add `mu::Map<Str, GLuint> textures` to `Field` struct (assets.h)
- Add `mu::Str tex_name` to `TerrMesh` struct (assets.h)
- Add `GLuint texture_id` and `bool tex_enabled` to `canvas::Mesh` (canvas.h)

### Startup

Startup tests run when `--test` CLI flag is passed. Five tests exist:

1. `test_parser()` in parser.h — generic parser acceptance tests
2. `test_aabbs_intersection()` in math.h — AABB overlap detection
3. `test_polygons_to_triangles()` in math.h — polygon triangulation
4. `test_line_segments_to_lines()` in math.h — line segment conversion
5. `test_rotational_physics()` in math.h — quaternion math, torque pipeline, angular acceleration (484 lines, the most extensive)

These follow the existing pattern: inline functions, mu_assert on failure, no framework, no new build target.

### Rendering Pipeline

The Canvas is a deferred render command buffer (arena-allocated, reset each frame). The render order is:

1. `canvas_rendering_begin` (clear, depth/blend state, polygon mode)
2. `canvas_render_ground` — infinite ground plane (raycast shader, tiled groundtile.png)
3. `canvas_render_gnd_pictures` — 2D field primitives (runways, lines, markings)
4. `canvas_render_zlpoints` — billboard light sprites (rwlight.png)
5. `canvas_render_meshes` — all 3D meshes (aircraft, terrain, ground objects, cockpit)
6. `canvas_render_axes` — debug coordinate axes
7. `canvas_render_boxes` — AABB debug boxes
8. `canvas_render_lines` — force vectors with debug labels
9. `canvas_render_text` — world-space billboard text (FreeType glyphs)
10. `canvas_render_hud_text` — screen-space text HUD
11. `canvas_render_hud_geoms` — graphical HUD instruments (v2)
12. ImGui rendering (debug UI, logs, perf monitors, settings)
13. `canvas_rendering_end` — swap buffers, reset arena

All 3D meshes use a single GLSL shader with branching for: gradient interpolation, lighting, fog, and (planned v3) texture sampling.

## Testing Decisions

### Testing Principle

Test the math, not the frame integration. The rotational dynamics math is deterministic and covered by startup assertions. The simulator IS the integration test for rendering.

### Current Testing Seams

The highest existing seam is the startup assertion framework (`--test` CLI flag). This is an inline function pattern in `main()` with `mu_assert` on failure — no test framework, no new build target. The prior art includes:

- `test_parser()` — validates the generic text parser (accept/peek/expect/panic)
- `test_aabbs_intersection()` — validates AABB overlap detection
- `test_polygons_to_triangles()` — validates polygon triangulation
- `test_line_segments_to_lines()` — validates line segment decomposition
- `test_rotational_physics()` — validates quaternion identity, 90-degree rotations, sequential integration, torque pipeline, angular acceleration, yaw sign

### What's Tested

- Physics math: rotational dynamics, quaternion operations, torque/acceleration pipeline
- Parser: generic parser tokenization, accept/peek/expect behavior
- Geometry: AABB intersection, polygon triangulation, line decomposition

### What's Not Tested

- Per-aircraft tuning values (tested by flying)
- Rendering code (visual — sim is the integration test)
- Audio mixing (auditory — verified by listening)
- HUD instruments (visual — manual verification)
- Camera math (partially — new modes verified by flying)
- Cockpit mesh rendering (pure asset loading)
- Terrain texturing (visual — manual verification with Hawaii scenery)

### Seams for Planned Features (v3)

| Feature | Testing Seam | Approach |
|---|---|---|
| CameraMode enum | Startup assertion | Test that enum dispatch selects the correct camera function for each mode value |
| Chase-cam | Manual | Fly with F9, verify camera behind aircraft |
| Tower view | Manual | Load airport scenery, press F12, verify viewpoint |
| TEXMAN parser | Startup assertion | Encode known 1x1 PNG to base64, parse back, verify pixel match |
| Texture rendering | Manual | Load Hawaii scenery, verify road masks on terrain |
| AoA indicator | Manual | Fly at different AoA, verify gauge movement |

### Prior Art

All tests follow the pattern established by the existing test functions: inline in source files, `mu_assert` on failure, gated behind `--test` CLI flag. No framework, no new build target. This pattern is preferred for new tests.

## Out of Scope

- Cockpit mesh rendering (SRF loaded but GL_CULL_FACE / alignment issues unresolved)
- Stall / spin aerodynamics and recovery
- Full dynamic pressure torque (v²/max_v² scaling used instead)
- P-factor / propeller torque modeling
- Ground effect simulation (lift/drag near runway)
- Compressibility / Mach effects
- Multiple aircraft tuning beyond YS-11
- Helicopter rotor physics
- Water rendering
- Multi-texture terrain blending by altitude/slope
- TXL/TXC texture layer blocks in FLD (TEXMAN/TEX MAIN only)
- SPEC flag per terrain patch
- Airport marking DST primitives
- Head bobbing / cockpit panel interaction
- Fly-by cinematic camera
- Radar altimeter, VOR/ILS/ADF nav instruments
- AoA indexer / stall warning audio
- Wind sound / gear sounds / flap sounds
- Collision detection (AABB per mesh, OBB, collision meshes, crash detection)
- Damage model / system failures
- AIRROUTE parsing and AI aircraft traffic
- Ground vehicle AI
- Mission / campaign system
- Multiplayer networking
- Graphics polish (bloom, HDR, shadows, reflections, particles, anti-aliasing)
- Codebase refactoring (extract systems from main.cpp, split assets.h)
- Unit test framework beyond startup assertions
- Platform build fixes (Windows/MSVC parity, macOS line width)
- Build optimization

## Further Notes

- **Current target aircraft**: YS-11 only (default at startup). Its DAT data, cockpit SRF, and EXCAMERA data exercise all implemented features. No other aircraft has been tuned.
- **Hawaii scenery** is the primary test case for terrain features (contains TEXMAN road-mask textures across ~150 lines of base64 data).
- **v1 implementation gap**: The torque pipeline uses v²/max_v² scaling instead of full dynamic pressure (0.5 × ρ × v² × wing_area). This means torque does not change with altitude. Full dynamic pressure remains deferred.
- **v2 implementation gap**: Cockpit SRF mesh is loaded to GPU but rendering is disabled due to invisible mesh issues (likely GL_CULL_FACE direction reversed for interior geometry, and mesh may not align correctly to the pilot eyepoint in the local coordinate system).
- **Single-file monolith**: The total codebase is 7527 lines across 7 source files. main.cpp alone is 3889 lines. assets.h is 2261 lines. This is a known structural debt that makes navigation harder but changes faster.
- **Assets read-only rule**: The assets/ directory contains original YS Flight Simulator game files and must never be modified. All work is reverse-engineering: reading existing formats, implementing support for what the actual files contain.
- **Physics source areas**: Physics changes are confined to src/math.h and src/aircraft.h/.cpp. Never modify src/parser.h or src/assets.h for physics.
- **Audio source area**: The audio subsystem is self-contained in src/audio.h and src/main.cpp.
- **HUD source area**: Canvas primitives live in canvas.h/canvas.cpp. Instrument rendering is in the aircraft render-prep phase.
- **Camera source area**: Camera lives in camera.h/camera.cpp with fields on both Camera and Aircraft structs.
