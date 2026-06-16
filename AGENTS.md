# OpenYSF Agent Knowledge Base
Last updated: 2026-06-16

## Project Overview
OpenYSF is a YS Flight Simulator clone in C++ (single-file monolith: `src/main.cpp` = 3889 lines).
- Reads native YS formats: `.fld` (terrain), `.stp` (starts), `.dat` (params), `.dnm` (model), `.srf` (mesh)
- Implements full flight model (lift/drag/thrust/weight), CLA/STA animation, scenery hierarchy
- Stack: SDL2 + OpenGL 3.3 + ImGui + ImPlot + GLAD + FreeType + glm + mu (custom util lib)
- Sole contributor: Mahmoud Adas (303/305 commits). Dormant since June 2024 (last: 2024-06-09).

## Golden Rule: Assets Are Read-Only
The `assets/` directory contains original YS Flight Simulator game files. **We must never modify these files.** All work is reverse-engineering: read the formats, parse the keys/values as they exist, and write code that interprets them correctly. We do NOT invent new DAT keys, FLD tokens, DNM blocks, or any other format terms — only implement support for what the actual files contain. If a key/value isn't in the shipped assets, we don't add it.

## Source Files (7527 total lines)

| File | Lines | Role |
|------|-------|------|
| `src/main.cpp` | 3889 | Monolith: all systems, physics, UI, render, Canvas |
| `src/assets.h` | 2164 | Data types (Mesh, Field, DATMap, Model), all asset parsing, template loading |
| `src/math.h` | 654 | Physics math (AABB, intersections, AoA, lift/drag), YS angle format |
| `src/parser.h` | 323 | Generic text parser (str, pos, line) + accept/peek/expect/panic |
| `src/graphics.h` | 242 | OpenGL wrappers (GLProgram, GLBuf, vertex attribs, shader helpers) |
| `src/audio.h` | 200 | AudioDevice, AudioBuffer, SDL audio callback mixing |
| `src/imgui.h` | 55 | ImGui helpers (EnumsCombo, SliderAngle, SliderMultiplier) |

## Build System

```cmake
cmake_minimum_required(VERSION 3.16) → C++20
option: OPENYSF_PEDANTIC_BUILD (OFF)

Dependencies via CPM.cmake:
  - mu (github.com/mido3ds/mu) — custom utility lib (Arena, Vec, Map, string, file I/O, logging)
  - SDL2 (release-2.24.1), SDL_image (release-2.6.2)
  - glm (0.9.9.8), freetype (VER-2-13-2, all features disabled)
  - portable-file-dialogs (0.1.0)
  - _glad (extern/glad), _imgui (extern/imgui-1.85), _implot (extern/implot-0.16)

Compile defs: ASSETS_DIR, OS_WINDOWS/LINUX/MACOS, COMPILER_{CLANG,GNU,MSVC}

Build:
  cmake -S. -Bbuild
  cmake --build build --target open-ysf -j
  ./build/bin/Debug/open-ysf
```

## Architecture
**Single World struct + free-function systems** (`sys::name(World&)`) with `DEF_SYSTEM` macro for SysMon profiling:

```
struct World {
  SDL_Window*, SDL_GLContext

  ImGuiWindowLogger (arena-backed log buffer)
  mu::Str imgui_ini_file_path
  mu::Vec<mu::Str> text_overlay_list     // TEXT_OVERLAY() macro

  LoopTimer                                // delta_time, frame pacing

  // Template maps (loaded from *.lst files in assets/)
  mu::Map<Str, AircraftTemplate>           // from air*.lst (dat, dnm, collision, cockpit)
  mu::Map<Str, SceneryTemplate>           // from sce*.lst (fld, stp, yfs)
  mu::Map<Str, GroundObjTemplate>         // from gro*.lst (dat, main_srf/dnm)

  AudioDevice
  mu::Map<Str, AudioBuffer>               // "engine2" → AudioBuffer

  mu::Vec<Aircraft>                        // active aircraft instances
  mu::Vec<GroundObj>                       // active ground objects
  Scenery scenery                          // current loaded scenery

  Camera                                   // Tracking (orbit) or Flying (FPS)
  PerspectiveProjection                    // near=0.1, far=100000, fovy=45°
  CachedMatrices                           // view, proj, view·proj + inverses

  Signals                                  // quit, wnd_configs_changed, scenery_loaded
  Events                                   // per-frame input (keyboard/mouse)
  Settings                                 // fullscreen, fps limit, rendering options

  Canvas                                   // deferred render command buffer (arena-allocated)
  SysMon                                   // per-system latency profiling
}
```

### Key Data Types (in assets.h)

```
AircraftTemplate {                        // from air*.lst
  Str short_name, dat, dnm, collision, cockpit, coarse
}

SceneryTemplate {                         // from sce*.lst
  Str name, fld, stp, yfs
  bool is_airrace
}

GroundObjTemplate {                       // from gro*.lst
  Str short_name, dat, main, coll_srf, cockpit_srf, coarse_srf
}

Model { mu::Vec<Mesh> meshes }           // DNM: hierarchical mesh tree
Mesh {                                    // SRF/DNM mesh node
  FieldID id, AnimationClass anim_type
  mu::Vec<glm::vec3> vertices
  mu::Vec<Face> faces
  mu::Vec<uint64_t> gfs, zls, zzs
  mu::Vec<Mesh> children
  mu::Vec<AnimationState> animation_states  // STA array
  AnimationState initial_state              // POS
  GLBuf gl_buf
  glm::vec3 cnt, translation, rotation; bool visible
}

Face { mu::Vec<uint32_t> vertices_ids; vec4 color; vec3 center, normal }

TerrMesh {                                // Terrain mesh from .fld
  Str name, tag; FieldID id; vec2 scale
  Vec<Vec<float>> nodes_height            // [z][x] height grid
  Vec<Vec<Block>> blocks                  // RIGHT/LEFT orientation + 2 face colors
  Gradient { enabled, bottom_y/top_y, colors }
  vec4 side_colors[4], vec3 translation/rotation
}

Picture2D {                               // 2D primitives on ground
  mu::Vec<Primitive2D> primitives         // POINTS/LINES/TRIANGLES/QUAD_STRIPS/GRADATION/
  vec3 translation, rotation
}

Field {                                   // recursive FLD node
  FieldID id, AreaKind default_area
  vec3 ground_color, sky_color
  mu::Vec<TerrMesh> terr_meshes
  mu::Vec<Picture2D> pictures
  mu::Vec<FieldRegion> regions            // runway/viewpoint (min/max in XZ)
  mu::Vec<Field> subfields
  mu::Vec<Mesh> meshes
  mu::Vec<GroundObjSpawn> gobs            // spawn definitions
  mat4 transformation; vec3 translation/rotation
}

FieldID: NONE=0, RUNWAY=1, TAXIWAY=2, AIRPORT_AREA=4,
         ENEMY_TANK_GENERATOR=6, FRIENDLY_TANK_GENERATOR=7,
         TOWER=10, VIEW_POINT=20

Aircraft {                                // per-instance
  AircraftTemplate; Model; DATMap
  AudioBuffer* engine_sound
  AABB initial_aabb, current_aabb
  vec3 translation; LocalEulerAngles angles
  vec3 acceleration, velocity; float max_velocity
  float wing_area, friction_coeff=0.032f, thrust_multiplier=500
  float throttle, pitch/yaw/roll_input_max
  float elevator/aileron/rudder_perc
  ClConsts { LinearFuncConsts; QuadraticFuncConsts pos/neg }
  QuadraticFuncConsts cd_consts
  Engine { speed_percent, burner_enabled, power, ... }
}

Scenery { SceneryTemplate; Field root_fld; Vec<StartInfo> }

GroundObj { GroundObjTemplate; Model; DATMap
  AABB initial/current; vec3 translation; LocalEulerAngles angles
  float speed; bool visible
}
```

## Main Loop (fixed/variable timestep)
1. `loop_timer_update` → delta_time
2. `events_collect` → keyboard/mouse → Events
3. `projection_update` → aspect on resize
4. `camera_update` → Tracking (orbit) or Flying (FPS)
5. `cached_matrices_recalc` → view, proj, view·proj
6. `scenery_update` → load Field hierarchy, fire `scenery_loaded`
7. `scenery_prepare_render` → push Ground/Pictures/Terrains/Meshes to Canvas
8. `aircrafts_update` → reload/remove/physics/controls
9. `aircrafts_prepare_render` → push Aircraft meshes + ZL points to Canvas
10. `ground_objs_update` → reload/remove/physics (spawn from Field.gobs)
11. `ground_objs_prepare_render` → push GroundObj meshes to Canvas
12. `models_handle_collision` → AABB broad-phase
13. `canvas_rendering_begin/end` → execute all draw calls + ImGui

## Domain Vocabulary
| Term | Meaning |
|------|---------|
| **Field** | Recursive terrain node (translation/rotation, ID: RUNWAY/TAXIWAY/TOWER/VIEW_POINT) |
| **StartInfo** | Spawn: position + attitude + landing_gear + throttle |
| **Scenery** | Root Field + StartInfo[] |
| **Aircraft** | Controllable vehicle with full physics |
| **GroundObj** | Static/moving ground object (vehicle, building, SAM) |
| **Model/Mesh** | Hierarchical 3D (DNM/SRF) with CLA animation states |
| **DATMap** | Key→Str raw value map (unit-aware parsing on access: ft, kt, km/h, MACH, kg, t, %) |
| **CLA** | Class of Animation (54 enum values: gear, flaps, burner, lights, turret, etc.) |
| **STA** | State Animation (min/max translation+rotation+visibility per CLA) |
| **CNT** | Contra-position — origin offset for correct animation pivot |
| **LocalEulerAngles** | {roll, pitch, yaw} in YS format (0x0000-0xFFFF = 0-360°) |
| **ZL/ZZ/GF** | Face metadata: light sprites (ZL), unknown (ZZ), face groups (GF) |

## Physics Pipeline (per aircraft/frame)
```
_user_controls → elevator/aileron/rudder percents → euler_rotate
             → throttle Q/A [0,1], afterburner TAB
_apply_physics:
  → anti-coll lights (periodic)
  → engine spool (speed_percent → throttle, resistence=15)
  → air density by altitude (ISA model)
  → AoA = aircraft_angle_of_attack(angles)
  → Thrust = engine_power * thrust_multiplier (500)
  → Weight = mass_total * 9.86
  → Drag = Cd(AoA) * ρ * v² * 0.05 * wing_area
  → Airlift = Cl(AoA) * ρ * v² * wing_area
  → Ground friction when y ≥ -1
  → a = F/m → v = clamp(v+a, max_vel) → pos += v·dt
  → AABB update (rotation + translation)
  → Mesh animation (CLA: gear, prop, burner, flaps, etc.)
```

**DAT Keys → Aircraft Fields:**
- `WEIGHCLN/WEIGFUEL/WEIGLOAD` → mass.clean/fuel/load (tons → kg)
- `REALPROP 0 MAXPOWER/IDLEPOWER` → engine.max_power/idle_power (HP)
- `MAXSPEED` → max_velocity (km/h → m/s)
- `WINGAREA` → wing_area (m²)
- `REALPROP 0 CL` → cl_consts.linear (AoA1,Cl1, AoA2,Cl2)
- `CRITAOAP/CRITAOAM` → aoa_crit_pos/neg (deg)
- `REALPROP 0 CD` → cd_consts (quadratic: AoAmin,Cdmin, AoA1,Cd1)
- `MXIPTAOA/MXIPTSSA/MXIPTROL` → pitch/yaw/roll_input_max (deg)
- `FUELMILI` → engine.fuel_mili (kg/s at mil power, "kg" suffix in DAT, divided by 1000 after parse)
- `FUELABRN` → engine.fuel_abrn (kg/s additional with afterburner, "kg" suffix in DAT, divided by 1000 after parse)

**Note:** All aircraft `.dat` files in `assets/aircraft/` define `FUELMILI` and `FUELABRN`. `INITFUEL` (initial fuel %) is also standard YS but not yet parsed. Do NOT invent custom keys — only parse keys that exist in the actual `.dat` files.

## Rendering Pipeline (Canvas — deferred, arena-allocated)
```
canvas_rendering_begin (clear, depth, blend, polygon_mode)
  → canvas_render_ground         (infinite grid shader, last GND set wins)
  → canvas_render_gnd_pictures   (Field Picture2D primitives: points/lines/tris/quads)
  → canvas_render_zlpoints       (billboard lights: rwlight.png sprite)
  → canvas_render_meshes         (regular + gradient meshes; gradient: top/bottom height+color)
  → canvas_render_axes           (debug gizmos — per-mesh POS/CNT)
  → canvas_render_boxes          (AABB collision boxes)
  → canvas_render_lines          (force vectors with debug labels)
  → canvas_render_text           (world-space billboard via FreeType glyphs)
  → canvas_render_hud_text       (screen-space HUD [0,1] coordinates)
  → imgui_rendering_begin/end    (debug UI: logs, perf monitors, settings)
canvas_rendering_end (swap, reset arena via mu::memory::tmp())
```

### Canvas Struct
```
Canvas {
  arena (mu::memory::Arena — per-frame reset)

  meshes:   { GLProgram, list_regular (Mesh[]), list_gradient (GradientMesh[]) }
  ground:   { GLProgram, GLBuf, tile_texture, last_gnd (Ground) }
  gnd_pics: { GLProgram, GndPic[] }
  zlpoints: { GLProgram, GLBuf, sprite_texture, ZLPoint[] }
  axes:     { GLBuf (single axis geo), list (Axis[]), line_width, on_top }
  boxes:    { GLProgram, GLBuf (single box geo), list (Box[]), line_width }
  text:     { GLProgram, GLBuf (char quad), Glyph[128], list_world, list_hud }
  lines:    { GLProgram, GLBuf, list (Line[]), line_width }
}
```

### Canvas Draw Call Types
| Type | Fields | Purpose |
|------|--------|---------|
| `canvas::Mesh` | vao, buf_len, projection_view_matrix | Regular colored mesh |
| `canvas::GradientMesh` | + gradient_bottom/top_y + colors | Height-based gradient mesh |
| `canvas::Ground` | color | Infinite ground plane |
| `canvas::GndPic::Primitive` | vao, buf_len, GLenum primitive_type, color, gradient | 2D field primitives |
| `canvas::ZLPoint` | center, color | Billboard light sprite |
| `canvas::Axis` | transformation | RGB XYZ gizmo |
| `canvas::Box` | translation, scale, color | AABB debug box |
| `canvas::Line` | p0, p1, color | Debug line |
| `canvas::Text` | str, world_pos, scale, color | World-space billboard text |
| `canvas::hud::Text` | str, [0,1] pos, scale, color | Screen-space text |
| `canvas::Vector` | label, origin, dir, len, color | Force vector (→ Line + Text) |

## Camera System
```
struct Camera {
  Aircraft* aircraft          // tracked target
  float zoom_multiplier=5, movement_speed=1000, mouse_sensitivity=1.4
  vec3 position, front, world_up, right, up, target_pos
  float yaw=15°, pitch=0°
  bool enable_rotating_around  // false → Flying (FPS) mode
}

Modes:
  - Tracking (orbit): follows aircraft with zoom, rotates around target
  - Flying (FPS): free WASD movement, mouse-look
```

## Events & Input
```
Events (per-frame, reset each cycle):
  Aircraft: afterburner_toggle, stick_{right/left/front/back},
            rudder_{right/left}, throttle_{increase/decrease}
  Camera:   camera_tracking_{up/down/right/left},
            camera_flying_{up/down/right/left}_rotate_enabled
  vec2 mouse_pos

Signals (persistent until handled):
  quit, wnd_configs_changed, scenery_loaded
```

## Audio System
```
AudioDevice              // SDL audio device handle + spec
AudioBuffer {            // loaded from assets/sound/*.wav
  Str file_path
  uint8_t* data; uint32_t len
}

Format: AUDIO_U16SYS, 22050Hz, stereo, stream size=512
Silence value: 0x7FFF (from silence.wav)
Engine sounds: engine0-9.wav, prop0-9.wav, burner.wav
```
Audio mixing happens in SDL callback — cumulative sample mixing with per-source gain.

## Asset Pipeline
```
assets/
├─ scenery/     → .fld, .stp            (sce*.lst → SceneryTemplate)
├─ aircraft/    → .dat, .dnm            (air*.lst → AircraftTemplate)
├─ ground/      → .dat, .dnm, .srf      (gro*.lst → GroundObjTemplate)
├─ sound/       → engine0-9.wav, prop0-9.wav, burner.wav
├─ fonts/       → zig.ttf
└─ misc/        → groundtile.png, rwlight.png

parser.h → Parser (str, pos, line) + accept/peek/expect/panic/fork
  .fld  → Field tree (recursive: FIELD → GND, SKY, PCK {TerrMesh/Pict2/FIELD})
  .stp  → StartInfo[] (N ← P ← C POSITION/ATTITUDE/INITSPED/CTLTHROT/CTLLDGEA)
  .dat  → DATMap (key → raw Str; REALPROP n KEY and EXCAMERA "name" folded into key)
  .dnm  → Model (PCK → SRF blocks with FIL/CLA/NST/STA/POS/CNT/REL DEP/NCH)
  .srf  → Mesh (single SURF block: V, F, ZA/GF/ZL/ZZ)

OpenGL wrappers (graphics.h):
  GLProgram   → shader compile/link + uniform setters (glm types)
  GLBuf       → VAO/VBO with variadic vertex attribs (template <Vec3, Vec4>)
  gl_buf_new  → static GPU buffer from Vec<T>
  gl_buf_new_dyn → dynamic GPU buffer (GL_DYNAMIC_DRAW)
```

## Hotspots (Risk Areas)
| File | Changes | Bug-fixes | Role |
|------|---------|-----------|------|
| `src/main.cpp` | 271 | 24 | Monolith: all systems, physics, UI, render |
| `src/audio.h` | 23 | 3 | AudioDevice + buffer management |
| `src/parser.h` | 11 | 1 | All asset parsing |
| `src/math.h` | 12 | 1 | Physics math (AABB, intersections, AoA, lift/drag) |
| `src/utils.hpp` | 9 | 2 | Memory (Arena, Vec, Map), string, file I/O |

All owned by Mahmoud Adas (sole contributor). No emergency/revert commits since 2023-01-01.

## Key Files to Read First
1. `src/main.cpp` — entire architecture, physics, render loop (lines 1-3889)
2. `src/assets.h` — all data types, asset parsing, template loading (lines 1-2164)
3. `src/parser.h` — generic text parser foundation (lines 1-323)
4. `src/math.h` — flight math primitives, YS angle format (lines 1-654)
5. `src/graphics.h` — OpenGL wrappers: GLProgram, GLBuf, shader helpers (lines 1-242)
6. `src/audio.h` — audio callback mixing (lines 1-200)
7. `src/imgui.h` — ImGui helper widgets (lines 1-55)

## Refactor Opportunities
- Extract systems from `main.cpp` into separate files (`sys_physics.cpp`, `sys_render.cpp`, `sys_audio.cpp`, `sys_scenery.cpp`, `sys_aircraft.cpp`, `sys_groundobj.cpp`)
- `World` → split into domain contexts (RenderWorld, PhysicsWorld, AudioWorld, UIWorld)
- Parser → separate `.cpp` per format (fld_parser, dnm_parser, dat_parser, srf_parser)
- Canvas → dedicated render backend
- Add unit tests for physics math (AoA, lift/drag, AABB)
- Extract template loading from assets.h into a loader module
- Flatten assets.h (2164 lines) — split into domain headers (field.h, model.h, dat.h)
