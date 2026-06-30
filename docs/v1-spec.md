# OpenYSF v1 Specification — "Playable Flight"

Last updated: 2026-06-30

## Goal

A playable YS Flight Simulator clone you can personally enjoy flying. v1 milestone: **take off → fly with believable feel → land.** Everything beyond that is v2.

---

## Flight Model

Extend the existing custom C++ physics. No physics engine dependency.

| Decision | Choice | Rationale |
|---|---|---|
| Orientation | `glm::quat` internally, YS Euler at boundaries | Clean rotation integration, no gimbal lock. glm already a dep. |
| Inertia tensor | Uniform-density box from aircraft AABB | Works for all types automatically, one-line calc on load |
| Control torques | Per-axis torque ∝ ρv² × control deflection | Reuses existing `EFFICIENCY` constants as torque gains |
| Thrust-pitch coupling | Thrust offset from CG (moment arm × thrust) | One-line cross product, captures primary effect |
| Ground handling | Rolling friction, rudder-steer, rotate at speed, brake | Gets you off ground and back. No tire models or shock absorbers |
| Stall/spin | v2 | Deferred |
| Multiple aircraft tuning | v2 | Tune only YS-11 for v1 |

## Audio

| Decision | Choice | Rationale |
|---|---|---|
| Mixing approach | Rewrite SDL callback: float accum → format convert | Fixes propeller gain being too quiet. ~1 day change |

## HUD

| Decision | Choice |
|---|---|
| v1 HUD | Minimal: airspeed, altitude, throttle %, gear state via existing canvas text |
| Full instruments (ADI, heading, VSI) | v2 |

## Camera

| Decision | Choice |
|---|---|
| v1 camera | Orbit (track aircraft) + free-fly (FPS) — existing modes |
| Cockpit, tower, fly-by | v2 |

## Data Model Changes

In `Aircraft` struct (aircraft.h):

- **Replace** `LocalEulerAngles angles` → `glm::quat orientation`
- **Add** `glm::vec3 angular_velocity` (rad/s)
- **Add** `glm::vec3 torque` (Nm, per-frame)
- **Add** `glm::mat3 inertia_tensor_inv` (computed from AABB on load, pre-inverted)
- **Retain** `LocalEulerAngles` as a derived read-only compute for rendering code

## Rendering

No new shaders or render passes for v1. Existing canvas pipeline unchanged.

## Quality

| Decision | Choice |
|---|---|
| Testing | Inline `test_rotational_physics()` in `main()` startup. Known inputs, assert expected outputs. Same pattern as existing `test_parser()`, `test_aabbs_intersection()`. |

## Tuning Target

YS-11 (already the default aircraft loaded at startup). Moderate flight envelope, stable, good for iterating rotational dynamics.

---

## Scoped Out (v2)

- Stall / spin / flat spin aerodynamics
- Multiple aircraft tuning and DAT rebalancing
- Terrain textures (TEXMAN, TEX MAIN, TXL/TXC parsing)
- Cockpit / tower / fly-by camera views
- Full instrument panel HUD
- Collision detection (AABB/OBB, crash, ground collision)
- Damage model / system failures
- Multiplayer / networking
- Graphics improvements (shaders, post-processing, shadows)
- Water rendering
- Other terrain/surface rendering (side colors, SPEC)
- AIRROUTE parsing (AI traffic)
- Landing gear IPO animation
- quaternion-based editor/axis gizmo
