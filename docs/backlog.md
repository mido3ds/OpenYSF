# OpenYSF Roadmap — Backlog

Last updated: 2026-07-10

This documents everything deferred beyond the v3 "visual breadth" milestone, organized by domain. Items marked **[v3]** are in-progress for v3. The rest are deferred to v4+.

---

## 1. Flight Envelope Depth

Current v1 physics covers basic takeoff→fly→land with rotational dynamics. v2 fills the edges:

| Item | Detail | Priority |
|---|---|---|
| Full dynamic pressure torque | Torque uses `0.5 * ρ * v² * wing_area` instead of v²/max_v² ratio — adds altitude-dependent control feel | High |
| Stall aerodynamics | Post-stall lift curve, deep stall behavior | High |
| Spin / flat spin | Asymmetric stall across wings, autorotation | High |
| P-factor / propeller torque | Gyroscopic precession from spinning mass, asymmetric blade loading at high AoA | Medium |
| Ground effect | Increased lift and reduced induced drag near runway | Medium |
| Compressibility / Mach effects | Drag rise near Mach 1 | Low |
| Aeroelastic effects | Flutter, divergence (probably not — true hardcore sim) | Low |

## 2. Multiple Aircraft

| Item | Detail |
|---|---|
| Tune flight model for fighter types | F-1, Tornado, F-15, etc. — high thrust-to-weight, responsive controls |
| Tune for heavy / transport | C-130, etc. — high inertia, slow response |
| Tune for small prop | Cessna 172, etc. — low inertia, low-speed handling |
| Tune for helos | Rotor physics entirely different. Significant new work |
| Rebalance DAT values | Current defaults are placeholders. Need real YS vs behavior parity |

## 3. Scenery & Terrain Rendering

| Item | Detail | Blocked By |
|---|---|---|
| ~~Terrain textures~~ | **[v3]** TEXMAN/TEX MAIN parsed, multiply-blend shader. TXL/TXC deferred. | `v3-spec.md` |
| Terrain side colors | Render BOT/RIG/LEF parsed side colors | Code exists but untested |
| SPEC flag on TerMesh | Specular flag per terrain patch — affects rendering path | Parser skip at assets.h:1444 |
| Water rendering | AreaKind::WATER parsed but no water surface drawn | New render pass needed |
| Airport markings (Pict2 DST) | DST in Pict2 primitives has unknown format (heathrow.fld) | Reverse engineering needed |
| Small.fld tessellation bug | "biggest pic doesn't tessellate correctly from left side" | Needs debugging |
| Fog tuning | Fog exists but may need tuning for different sceneries | Low priority |

## 4. Camera Views

| Item | Detail |
|---|---|
| ~~Tower view~~ | **[v3]** Cycle through VIEW_POINT FieldRegions from scenery | `v3-spec.md` |
| ~~Chase-cam~~ | **[v3]** Toggleable camera behind aircraft tail, follows orientation | `v3-spec.md` |
| Fly-by camera | Smooth cinematic fly-by camera | v4+ |
| Chase-cam refinements | Smooth transitions, adjustable follow distance via mouse wheel | v4+ |

## 5. HUD / Instruments

| Item | Detail |
|---|---|
| ~~AOA indicator~~ | **[v3]** Vertical strip gauge on HUD | `v3-spec.md` |
| Radar altimeter | Low-altitude precision |
| Nav instruments | VOR, ILS, ADF needles for instrument approaches |

## 6. Collision & Damage

| Item | Detail |
|---|---|
| AABB per mesh | Currently one AABB per entire model | Split in assets.h |
| OBB instead of AABB | Oriented bounding boxes for tighter fit | math.h extension |
| Collision meshes | coll.dnm files exist but unused | Load and use for ground/aircraft collision |
| Crash detection | Hard impact → damage/fire/explosion | |
| Damage model | System failures: engine, hydraulics, flight controls | |
| Ground collision | Terrain height probing from FLD mesh | |

## 7. Audio

| Item | Detail |
|---|---|
| Wind sound | Speed-dependent wind noise in cockpit |
| Stall warning horn | AoA-based audible warning |
| Gear / flap sounds | Mechanical sounds for animation events |
| Multi-source mixing improvements | Same-sound concurrency policy, per-source gain limits |

## 8. AI & Mission

| Item | Detail |
|---|---|
| AIRROUTE parsing | AI aircraft flight routes in FLD files | Parser skip at assets.h:1420 |
| AI aircraft | Computer-controlled traffic following AIRROUTE waypoints | |
| Ground vehicle AI | GOB spawns with movement along taxi paths | |
| Mission / campaign system | Original YS has mission scoring, objectives | |

## 9. Multiplayer

| Item | Detail |
|---|---|
| Network sync | Position/state interpolation of other players |
| Lobby / session management | Server browser, room creation |
| Voice chat | In-sim audio communication |

## 10. Graphics & Polish

| Item | Detail |
|---|---|
| Post-processing | Bloom, HDR, tone mapping |
| Shadows | Shadow mapping for terrain and aircraft |
| Reflections | Water reflections, environmental reflections |
| Particle system | Smoke, fire, contrails, tire smoke |
| Anti-aliasing | MSAA or FXAA |
| Axis gizmo labels | World-space labels for debug axes |
| Mesh selection / manipulation | Editor features for debugging |

## 11. Code Quality

| Item | Detail |
|---|---|
| Extract systems from main.cpp | Separate files per domain (sys_physics, sys_render, etc.) |
| Split assets.h | ~2261 lines — extract into field.h, model.h, dat.h, parsers/ |
| Split parser.h per format | fld_parser, dnm_parser, dat_parser, srf_parser |
| Dedicated render backend | Canvas → proper abstraction |
| Quaternion everywhere | Replace remaining Euler angle usage in rendering / camera |
| Strict integer tokenization | Parser enhancement for performance |
| Unit tests for physics math | Extract and test AoA, lift/drag, AABB math |

## 12. Platform / Build

| Item | Detail |
|---|---|
| Windows/MSVC parity | Test and fix Windows build issues |
| Line width on macOS | Known broken — fix OpenGL line width on Metal translation layer |
| Build optimization | Smaller GL primitives (strip/fan), reduce startup time (Concorde.dnm slow) |
