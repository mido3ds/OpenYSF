# OpenYSF v3 Specification — "Visual Breadth"

Last updated: 2026-07-10

## Goal

Feature breadth: spread effort across 3 small/medium camera and visual features. v3 milestone: **more ways to see the aircraft, terrain that looks like the real YS, and AoA on the HUD.**

---

## Feature 1: Chase-Cam + Tower View

Two new camera modes alongside existing orbit, EXCAMERA, cockpit, and flying modes.

### Camera Mode Data Model

Replace the current ad-hoc mode tracking (`excamera_index`, `cockpit_mode` booleans on `Aircraft`) with a single enum on `Camera`:

```cpp
enum class CameraMode { Orbit, Chase, Tower, EXCAMERA, Cockpit };
struct Camera {
    CameraMode mode = CameraMode::Orbit;
    int camera_index = -1;         // EXCAMERA index or tower viewpoint index
    // ... existing fields ...
};
```

### Chase-Cam

- Camera positioned behind and above the aircraft tail, always looking at the aircraft
- Offset calculated from aircraft orientation: `-front * distance + up * height_offset`
- Mouse wheel adjusts chase distance
- Toggle on/off

### Tower View

- Collect `FieldRegion` entries with `id == VIEW_POINT` from the loaded scenery's Field hierarchy
- Camera placed at viewpoint position, looking toward the aircraft
- Cycle through all viewpoints with a key (no viewpoints available → key does nothing)
- `camera_index` tracks which viewpoint is active

### Controls

| Key | Action |
|---|---|
| `F9` | Toggle chase-cam (on/off) |
| `F10` | Cycle EXCAMERA views (existing, unchanged) |
| `F11` | Toggle cockpit view (existing, unchanged) |
| `F12` | Cycle tower viewpoints (next → next → … → back to previous mode) |

The camera mode cycle is exclusive: activating chase-cam, tower, EXCAMERA, or cockpit deactivates the others. Orbit is the default fallback when no special mode is active.

### Out of Scope

- Head movement / bobbing — deferred to v4
- Cockpit SRF rendering (deferred from v2) — still v4+

---

## Feature 2: Terrain Textures

Parse the existing `TEXMAN` / `TEX MAIN` blocks from `.fld` files to apply textures to terrain meshes.

### TEXMAN Format

FLD files embed textures as inline base64-encoded PNG data:

```
TEXMAN TEXTURE "TextureName"
TEXMAN TEXTUREFORMAT .png
TEXMAN TEXTUREFILE "filename.png"
TEXMAN TEXTUREFILTER LINEAR
TEXMAN TEXTUREDATA <base64_length>
<base64 data lines...>
TEXMAN ENDTEXTURE
```

Per-TerrMesh assignment:

```
TEX MAIN "TextureName"
```

Known textures across shipped scenery:

| Scenery | Textures |
|---|---|
| hawaii.fld | `Texture0000` (icon), `Texture0001` (random), `OahuRoadMask`, `HawaiiNorth`, `HawaiiSouth` |
| aomori.fld | `Hummingbird` (decorative) |

The Hawaii textures (`HawaiiNorth`, `HawaiiSouth`, `OahuRoadMask`) are road-mask overlays applied to vertex-colored terrain.

### Implementation

**Parser (`assets.h`)**:
- Parse `TEXMAN` block headers (name, format, file, filter, data length)
- Read base64 lines until `ENDTEXTURE`
- Decode base64 → raw bytes
- Load PNG via `IMG_Load_RW(SDL_RWFromMem(data, len))` → `SDL_Surface`
- Upload as `GLuint` texture with `glTexImage2D` (RGBA)
- Store in `mu::Map<Str, GLuint> textures` on the `Field` struct that contains the TEXMAN block

**TerrMesh**:
- Add `mu::Str tex_name` field — set by `TEX MAIN "name"`
- In `terr_mesh_load_to_gpu()`, generate UV coordinates via XZ projection (normalized to mesh world-space bounds)
- Extend vertex format: `{ vec3 position, vec4 color, vec3 normal, vec2 uv }`

**Rendering (shader)**:
- Mesh shader gets `uniform sampler2D terrain_tex` and `uniform bool tex_enabled`
- When `tex_enabled`: `frag_color = vertex_color * texture(terrain_tex, uv)`
- Multiply blend: texture overlays on vertex color (matches road-mask use case)

### Rendering Pipeline

In `scenery_prepare_render()`, when pushing each `TerrMesh` to canvas:
- Look up texture from `Field::textures` by `terr_mesh.tex_name`
- If found, pass `GLuint texture_id` to `canvas::Mesh`

No new shader — extend the existing mesh shader with a texture sampler branch.

### Out of Scope

- Multi-texture blending by altitude/slope — deferred to v4+
- `TEXMAN TEXTUREFILTER` parsing (LINEAR assumed for now)
- `GNDSPECULAR` / per-terrain specular toggles
- Texture atlasing / texture arrays

---

## Feature 3: AoA Indicator

Add a graphical Angle of Attack indicator to the HUD. AoA is already computed by `aircraft_angle_of_attack()` in `math.h` — this is purely a visual addition.

### Instrument

- **Style**: Vertical strip gauge — thin bar (~15px wide) with scale markings
- **Scale**: -5° to +25°, labeled at 0°, 10°, 20°
- **Indicator**: Moving marker (filled triangle) pointing at current AoA value on the scale
- **Position**: Near existing airspeed/altitude text HUD elements
- **Visibility**: Always visible (same as other HUD elements), regardless of camera mode

### Implementation

Reuse existing `canvas::hud` primitives:
- `canvas::hud::Line` — scale tick marks and bar
- `canvas::hud::Text` — degree labels
- `canvas::hud::FilledTriangle` — moving marker

Draw call added after existing `hud_text` rendering, before ImGui. Depth test disabled, blending enabled.

### Out of Scope

- AoA indexer (approach reference markers) — deferred to v4
- AoA tone / stall warning audio — deferred to v4
- Radar altimeter, VOR/ILS/ADF nav instruments — deferred to v4+

---

## Data Model Changes Summary

| Struct | Change |
|---|---|
| `Camera` (`camera.h`) | Add `CameraMode mode = Orbit; int camera_index = -1`. Remove `enable_rotating_around`? (existing field, may still be used) |
| `Aircraft` (`aircraft.h`) | Remove `int excamera_index` and `bool cockpit_mode` (moved to Camera) |
| `Field` (`assets.h`) | Add `mu::Map<Str, GLuint> textures` |
| `TerrMesh` (`assets.h`) | Add `mu::Str tex_name` |
| `canvas::Mesh` (`canvas.h`) | Add `GLuint texture_id = 0` and `bool tex_enabled = false` |

---

## Quality

| Decision | Choice |
|---|---|
| Testing | Manual visual testing against Hawaii scenery with YS-11. No automated test framework. |
| Assertions | Startup assertion verifying base64→PNG decode on a known TEXMAN data fragment |
| Tuning target | YS-11 only. Hawaii scenery primary test case. |

---

## Out of Scope (v4+)

- Cockpit SRF mesh rendering
- Head bobbing / cockpit interaction
- Stall / spin aerodynamics
- Collision / damage
- Audio improvements
- Multi-texture terrain blending
- All code quality / refactoring
- Multiple aircraft tuning
- AI / missions
- Multiplayer
- Nav instruments (VOR/ILS/ADF)
- AoA indexer / stall warning audio
