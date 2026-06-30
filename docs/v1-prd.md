# OpenYSF v1 PRD — Playable Flight

## Problem Statement

OpenYSF currently has a basic point-mass flight model (forces → velocity → position, with Euler angles set directly). The aircraft responds like a puppet on strings, not a physical object with mass and momentum. Four gaps prevent it from feeling like a real flight sim: bad rotational feel (no torque/inertia), missing stall/spin behavior, no thrust-pitch coupling, and broken ground handling. The audio mixing also makes engine sounds barely audible. Together these make the sim feel like a tech demo rather than a flyable game.

## Solution

Replace the point-mass/instant-Euler control model with full rigid-body rotational dynamics. The aircraft becomes a `glm::quat`-driven rigid body: control surfaces produce torques, torques integrate to angular velocity, angular velocity integrates to orientation. Ground handling gets a basic rolling-friction + rudder-steer model. Audio mixing gets rewritten so engine sounds are audible. A minimal HUD (airspeed, altitude, throttle, gear) replaces the debug overlay for usable flight info.

## User Stories

1. As a pilot, I want the aircraft to rotate smoothly in pitch/roll/yaw with believable inertia, so that maneuvers feel weighty and physical rather than snapped.
2. As a pilot, I want the control stick to produce torque proportional to airspeed, so that the aircraft feels responsive at high speed and sluggish at low speed.
3. As a pilot, I want adding power to pitch the nose up/down appropriately, so that throttle changes affect attitude as they would in a real aircraft.
4. As a pilot, I want to accelerate down the runway and rotate the aircraft at a reasonable speed, so that takeoff is a distinct phase of flight.
5. As a pilot, I want to touch down and roll out with brakes and rudder steering, so that landing feels complete.
6. As a pilot, I want to see my airspeed, altitude, throttle position, and landing gear state on screen, so that I can fly without guessing.
7. As a pilot, I want engine and propeller sounds that scale with throttle, so that the cockpit feels alive and speed changes are audible.
8. As a pilot, I want the existing camera modes (orbit and free-fly) to track the aircraft correctly after the new orientation math, so that I can see what I'm doing.
9. As a developer, I want a startup self-test that validates rotational dynamics math, so that physics regressions are caught on launch.
10. As a pilot, I want to fly the YS-11 with all four gaps addressed (rotation feel, ground handling, thrust-pitch, stall behavior at least in basic form), so that there is at least one flyable aircraft to enjoy.

## Implementation Decisions

**Physics engine**: No external library. Extend the existing custom C++ physics in `src/math.h` and `src/aircraft.h`/`.cpp` with rigid-body rotational dynamics.

**Orientation representation**: `glm::quat` internally. YS Euler angles (uint16 0x0000–0xFFFF) converted to quaternion at load time and back to Euler for rendering code. A read-only `LocalEulerAngles` accessor on Aircraft provides the backward-compatible view. No parser or file format changes.

**Inertia tensor**: Computed once at aircraft load from the bounding box AABB (uniform-density box). Pre-inverted and stored as `glm::mat3`. Formula: `diag( (w²+h²)/12 * mass, (d²+h²)/12 * mass, (w²+d²)/12 * mass )`.

**Control torque model**: Per-axis torque proportional to dynamic pressure. `torque = control_perc * efficiency * 0.5 * ρ * v² * wing_area`. Existing `EFFICIENCY` constants in `src/settings.h` reinterpreted as torque gains.

**Thrust-pitch coupling**: Thrust applied with a moment arm offset from CG: `torque += cross(thrust_arm, thrust_vector)`. Thrust arm is a constant per aircraft type.

**Ground handling model**: Rolling friction, rudder-steer on ground (yaw torque), elevator-up at speed triggers rotation, brakes on key. ~50 lines.

**Audio mixing rewrite**: SDL callback switches from `SDL_MixAudioFormat` to float-sample accumulation. Per-source gain scaling. Output converted to device format. ~80 lines, no new deps.

**Minimal HUD**: Airspeed (km/h), altitude (ft), throttle %, gear state via existing `canvas::hud::Text`. No new shaders.

## Testing Decisions

**Testing principle**: Only test the math, not the frame integration. The rotational dynamics math is deterministic and covered by startup assertions. The simulator IS the integration test.

**Startup test** (`test_rotational_physics()` in `main()`):
- Known torque on known inertia produces expected angular acceleration
- Quaternion from identity with angular velocity × dt produces expected orientation
- Control surface torque formula produces expected sign
- Thrust-offset cross product direction matches expected pitch

**Not tested**: Per-aircraft tuning values, rendering code, audio mixing.

**Prior art**: Follows existing pattern of `test_parser()`, `test_aabbs_intersection()` etc. — inline functions in `main()`, `mu_assert` on failure. No framework, no new build target.

## Out of Scope

- Stall / spin aerodynamics and recovery
- Multiple aircraft tuning beyond YS-11
- Terrain textures and scenery rendering
- Cockpit / tower camera views
- Full instrument panel HUD
- Collision detection and damage
- Multiplayer networking
- Graphics polish (shaders, post-processing)
- Water rendering
- Codebase refactoring
- Platform build fixes
- Editor / axis gizmo
- Mission / AI / AIRROUTE system

## Further Notes

- v1 target aircraft is **YS-11** (default at startup). Its DAT data exercises the new physics and it's a stable tuning target.
- Physics changes are confined to `src/math.h` and `src/aircraft.h`/`.cpp`. No changes to `src/assets.h`, `src/parser.h`, or rendering code.
- Audio mixing is self-contained in `src/audio.h` and `src/main.cpp`.
- HUD rendering lives in the aircraft render-prep phase in `src/main.cpp`.
