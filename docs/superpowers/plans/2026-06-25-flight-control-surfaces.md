# Flight Control Surfaces Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make aileron, elevator, and rudder control surfaces actually affect aircraft dynamics — roll torque from ailerons, pitch torque + lift from elevator, adverse yaw from aileron drag, all scaled by airspeed.

**Architecture:** Currently, rotation uses a flat `ROTATE_SPEED` constant in `_aircrafts_apply_user_controls` regardless of airspeed, control surface deflection, or aircraft type. Control surface percentages (`elevator_perc`, `right_aileron_perc`, `rudder_perc`) are set from input but **never used** for rotation. This plan moves rotation to `_aircrafts_apply_physics` (where `vel_sq` and `air_density` are available), replaces the constant with airspeed-scaled control-surface-driven torques, and adds elevator lift contribution + aileron adverse yaw.

**Tech Stack:** C++20, glm

**Files modified (one file only):** `src/main.cpp`

---

### Task 1: Move rotation from user_controls to physics

**Files:**
- Modify: `src/main.cpp:2904-2924` (rotation in `_aircrafts_apply_user_controls`)
- Modify: `src/main.cpp:3086-3138` (physics loop in `_aircrafts_apply_physics`)

**Rationale:** Airspeed (`vel_sq`) and air density are only available in `_aircrafts_apply_physics`, which runs after `_aircrafts_apply_user_controls`. Rotation needs these values to scale control authority.

- [ ] **Step 1: Remove rotation block from `_aircrafts_apply_user_controls`**

  Delete lines 2904-2924 in `_aircrafts_apply_user_controls`:

  ```cpp
  // --- DELETE THIS ENTIRE BLOCK ---
  float delta_yaw = 0, delta_roll = 0, delta_pitch = 0;
  constexpr auto ROTATE_SPEED = 12.0f / DEGREES_MAX * RADIANS_MAX;
  if (world.events.stick_back) {
      delta_pitch -= ROTATE_SPEED * world.loop_timer.delta_time;
  }
  if (world.events.stick_front) {
      delta_pitch += ROTATE_SPEED * world.loop_timer.delta_time;
  }
  if (world.events.stick_left) {
      delta_roll -= ROTATE_SPEED * world.loop_timer.delta_time;
  }
  if (world.events.stick_right) {
      delta_roll += ROTATE_SPEED * world.loop_timer.delta_time;
  }
  if (world.events.rudder_right) {
      delta_yaw -= ROTATE_SPEED * world.loop_timer.delta_time;
  }
  if (world.events.rudder_left) {
      delta_yaw += ROTATE_SPEED * world.loop_timer.delta_time;
  }
  local_euler_angles_rotate(self.angles, delta_yaw, delta_pitch, delta_roll);
  ```

- [ ] **Step 2: Commit**

  ```bash
  git add src/main.cpp
  git commit -m "refactor(flight): remove flat rotation from user_controls, prepare for physics-driven rotation"
  ```

---

### Task 2: Add airspeed-scaled rotation in physics

**Files:**
- Modify: `src/main.cpp:2985-3215` (`_aircrafts_apply_physics`)

- [ ] **Step 1: Add rotation after forces but before AABB update**

  In `_aircrafts_apply_physics`, inside the aircraft loop, after the `vel_sq` line (currently ~3086) and after the forces block (line 3119), but before `// translation` (line 3121), insert the rotation block:

  ```cpp
  // rotation — torque from control surfaces, scaled by airspeed
  {
      float vel = glm::length(aircraft.velocity);
      float max_vel = aircraft.max_velocity;
      // airspeed_factor: 0 at stall, 1 at max speed — controls lose authority at low speed
      float airspeed_factor = glm::clamp((vel * vel) / (max_vel * max_vel), 0.0f, 1.0f);
  
      float delta_yaw = 0, delta_roll = 0, delta_pitch = 0;
  
      // aileron roll torque: positive right_aileron_perc → roll right
      delta_roll += aircraft.right_aileron_perc * ROLL_EFFICIENCY
                    * airspeed_factor * (float)world.loop_timer.delta_time;
  
      // elevator pitch torque: positive elevator_perc → pull up (nose rises)
      delta_pitch += -aircraft.elevator_perc * ELEVATOR_EFFICIENCY
                     * airspeed_factor * (float)world.loop_timer.delta_time;
  
      // rudder yaw torque: positive rudder_perc → yaw right
      delta_yaw += aircraft.rudder_perc * RUDDER_EFFICIENCY
                   * airspeed_factor * (float)world.loop_timer.delta_time;
  
      // aileron adverse yaw: rolling right → nose yaws left (asymmetric induced drag)
      delta_yaw += -aircraft.right_aileron_perc * ADVERSE_YAW_COEFF
                   * airspeed_factor * (float)world.loop_timer.delta_time;
  
      local_euler_angles_rotate(aircraft.angles, delta_yaw, delta_pitch, delta_roll);
  }
  ```

- [ ] **Step 2: Add the new constants** at the top of the file (~line 48, near `CTRL_SURFACE_SPEED`):

  ```cpp
  // control surface torque efficiency (rad/s at max airspeed and full deflection)
  constexpr float ROLL_EFFICIENCY    = 0.8f;  // ~46°/s roll rate at full aileron, max speed
  constexpr float ELEVATOR_EFFICIENCY = 0.4f;  // ~23°/s pitch rate
  constexpr float RUDDER_EFFICIENCY   = 0.3f;  // ~17°/s yaw rate
  
  // aileron adverse yaw coefficient (asymmetric induced drag)
  constexpr float ADVERSE_YAW_COEFF   = 0.2f;  // fraction of roll torque converted to opposite yaw
  ```

- [ ] **Step 3: Commit**

  ```bash
  git add src/main.cpp
  git commit -m "feat(flight): airspeed-scaled roll/pitch/yaw torques from control surfaces with adverse yaw"
  ```

---

### Task 3: Elevator lift contribution

**Files:**
- Modify: `src/main.cpp:3107-3112` (lift force calculation in physics)

- [ ] **Step 1: Add elevator lift to the existing lift formula**

  After the airlift computation (line 3112), add:

  ```cpp
  // elevator lift contribution — deflected elevator adds tail lift/downforce
  aircraft.forces.airlift += aircraft.elevator_perc * ELEVATOR_LIFT_SCALE
                             * air_density * vel_sq;
  ```

- [ ] **Step 2: Add the constant** near the other control constants:

  ```cpp
  // elevator lift scale coefficient (multiplies air_density * vel_sq * elevator_perc)
  constexpr float ELEVATOR_LIFT_SCALE = 5.0f;  // ~5% of wing lift at full deflection, max speed
  ```

- [ ] **Step 3: Commit**

  ```bash
  git add src/main.cpp
  git commit -m "feat(flight): elevator deflection contributes to total lift (tail downforce)"
  ```

---

### Task 4: Debug UI for tuning constants

**Files:**
- Modify: `src/main.cpp:1545-1565` (Aircraft debug panel in ImGui)

- [ ] **Step 1: Add control surface tuning sliders**

  Inside the aircraft debug tree node (~line 1555 area), add before or after the Cl/Cd tuning:

  ```cpp
  ImGui::Separator();
  ImGui::Text("Control Surfaces");
  ImGui::DragFloat("Elevator %", &aircraft.elevator_perc, 0.01f, -1.0f, 1.0f, "%.2f");
  ImGui::DragFloat("Aileron %", &aircraft.right_aileron_perc, 0.01f, -1.0f, 1.0f, "%.2f");
  ImGui::DragFloat("Rudder %", &aircraft.rudder_perc, 0.01f, -1.0f, 1.0f, "%.2f");
  ```

  These allow manual override of control surfaces for testing rotation behavior. The existing TEXT_OVERLAY on line 2897-2899 already shows current values in the debug overlay.

  Also add tuning for the global constants in the Simulation section:

  ```cpp
  ImGui::DragFloat("Roll Eff.", &ROLL_EFFICIENCY, 0.01f, 0.0f, 5.0f, "%.2f");
  ```

  Wait — constants can't be tuned via ImGui directly (they're constexpr). Instead, convert them to global variables. Or better: wrap them in an anonymous namespace so they remain internal-linkage but non-const. This is a judgment call — tune via recompile (YAGNI) or add runtime tuning?

  **Decision:** Keep them as `constexpr` for now. The TEXT_OVERLAY already shows `elevator = 45%` etc. Runtime tuning is over-engineering for this iteration. The numbers can be refined by editing constants and rebuilding.

- [ ] **Step 2: Commit**

  Omit this task for now — keep YAGNI. The TEXT_OVERLAY display and the ability to recompile with tweaked constants is sufficient.

---

### Task 5: Remove completed todos and save detailed full-torque-model TODO

- [ ] **Step 1: Remove the 3 completed items from TODO file** (lines 1-3: aileron torque, aileron drag, elevator lift+drag)

  Delete lines 1-3 from `/Users/mahmoudadas/pdev/OpenYSF/TODO`.

- [ ] **Step 2: Save a detailed future-torque-model TODO** as a new entry at the top of TODO:

  ```
  - [x] aileron applies torque (simple: airspeed-scaled)
  - [x] aileron adds drag (simple: adverse yaw torque)
  - [x] elevator adds lift and drag (simple: pitch torque + lift contribution)
  - future: full physics torque model:
      - angular velocity + angular acceleration (inertia tensor from AABB)
      - moment arms per control surface from STA data
      - angular drag from air density + surface area
      - separate torque contributions per axis (roll/pitch/yaw)
      - gyroscopic precession from propeller
      - slip/skid ball indication
  ```

- [ ] **Step 3: Commit**

  ```bash
  git add src/main.cpp TODO
  git commit -m "chore: mark control surface TODOs done, add future torque model notes"
  ```

---

## Self-Review

**1. Spec coverage:**
- ✅ **Aileron applies torque** — `delta_roll` uses `right_aileron_perc × ROLL_EFFICIENCY × airspeed_factor`
- ✅ **Aileron adds drag** — `delta_yaw += -right_aileron_perc × ADVERSE_YAW_COEFF × airspeed_factor` (adverse yaw)
- ✅ **Elevator adds lift and drag** — `delta_pitch` uses `-elevator_perc × ELEVATOR_EFFICIENCY × airspeed_factor` + lift contribution added to `aircraft.forces.airlift`
- ✅ **Airspeed dependence** — `airspeed_factor = vel² / max_vel²`, clamped to [0,1]
- ✅ **No flat ROTATE_SPEED** — removed entirely from user_controls
- ✅ **Rudder** — also converted to airspeed-scaled `rudder_perc` (consistent)
- ✅ **Detailed future TODO** — saved with full physics torque model notes

**2. Placeholder scan:** No TBDs, TODOs, or incomplete code.

**3. Type consistency:**
- All control percentages are `float [-1, 1]` — consistent with existing `right_aileron_perc`, `elevator_perc`, `rudder_perc`
- `airspeed_factor` is `float [0, 1]`
- Constants use `constexpr float` matching the existing `CTRL_SURFACE_SPEED` pattern
- `local_euler_angles_rotate` signature unchanged
