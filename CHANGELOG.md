# Changelog

## [Unreleased]

### Added
- **`match (subject) { case v1, v2 { .. } ... else { .. } }`** — pattern-matching
  statement, additive over `if`/`else` (see `docs/control-flow.md` §2.1). Compares
  `subject` (evaluated once) against each `case` value using the same equality as
  `==`; first matching `case` runs and matching stops (no C-style fallthrough).
  Pairs naturally with `enum` values.
- **`goal { ... }` / `achieve expr;`** — a block *expression* that evaluates to
  `nil` unless `achieve expr;` fires somewhere inside it, in which case the block
  short-circuits and evaluates to that value — a scoped early-exit smaller than a
  full function `return` (see `docs/control-flow.md` §6). `achieve` outside any
  enclosing `goal` block is a parse-time error, exactly like `break`/`continue`
  outside a loop; `goal`/`loop` depth tracking correctly resets across nested
  function bodies (mirrors the existing `loopDepth` mechanism).
- **`when (cond) { .. } otherwise { .. }`** now actually works in the real
  interpreter (`app/src/main/cpp`). It was documented in `docs/control-flow.md`
  §1.1 and even had a `Parser::whenStatement()` declaration, but the function was
  never implemented/wired — the only working copy lived in the separate
  `compiler/rinc.cpp` transpiler. Fixed: `whenStatement()` now builds the exact
  same `IfStmt` as `if`/`else`, wired into `Parser::declaration()`.
- **`docs/enums.md`** — new page documenting `enum` (options: a closed list of
  named cases, e.g. `enum Status { Active, Paused, Done }`, accessed as
  `Status.Active`). This language feature already existed fully in
  `rin_ast.h`/`rin_parser.cpp`/`rin_interpreter.cpp` but had no documentation
  page anywhere; this fills that gap and cross-links it from `control-flow.md`.

- `rinVersion()` / `rinEdition()` — the engine version (from
  `rin_version.h`) is now readable from inside `.rin` scripts themselves, not
  just from the host side (`rin --version`, `rin_engine_version()`,
  `RinEngine.engineVersion()`). Implemented identically in both the
  interpreter (`rin_interpreter.cpp`'s `registerNatives()`) and the native
  compiler (`rinc.cpp`'s codegen, both copies), verified to return the same
  value (`"1.0.0"`/`"2026"`) through both execution paths. Because `rinc.cpp`
  is deliberately a self-contained single file with no project includes, its
  two native functions hold the version as manually-synced literals rather
  than `#include`ing `rin_version.h` — see the checklist in
  [`docs/VERSIONING.md`](docs/VERSIONING.md).
- The app's built-in "مثال" (example) starter-project template
  (`ProjectManager.kt` → `mainRinTemplateFor()`, `ProjectType.FREE` with
  `freeOptions.template == "example"`) now includes a line using
  `rinVersion()`/`rinEdition()`, so anyone creating a new example project in
  the editor sees the version-reading feature demonstrated immediately.
- Official versioning system: `app/src/main/cpp/rin_version.h` is now the
  single source of truth for the Rin engine version, replacing four
  independently hardcoded numbers that had already drifted apart (`rin
  --version` printed `0.2.0` on Linux vs `0.1.0` on macOS/Windows for the
  identical CLI tool; the Android app and the C API reported `1.1` from two
  separate hardcoded strings; the root `VERSION` file said `1.0.0` and was
  read by nothing). `jni_bridge.cpp`, `rin_c_api.cpp`, and all three
  `cli/*/src/main.cpp` now include `rin_version.h` instead of defining their
  own version string. `app/build.gradle` now reads `versionName` from the
  root `VERSION` file and derives `versionCode` from it
  (`major*10000+minor*100+patch`). See
  [`docs/VERSIONING.md`](docs/VERSIONING.md) for the policy, the release
  checklist, and why `rinc`, embedded `.rin` libraries, and RinPM package
  versions are intentionally kept separate from this number.
- `use Name from "path.rin";` — simple-English syntax for calling one named
  container (`@container=Name`) or UI element (`@view.Kind=Name`) from
  another `.rin` file by name, without pulling in everything else that file
  defines. See [`docs/cross-file-containers.md`](docs/cross-file-containers.md)
  and [`examples/use_from_demo/`](examples/use_from_demo/).
- `lib/movingmask.og.rin` v1.0.0 — moving masks over containers/loops now
  ships **embedded** inside the RinStudio interpreter (`@import
  "lib/movingmask.og.rin";` works instantly on any device, no manual upload),
  and appears in the in-app "Libraries" browser with its own icon and
  description. Five new integrated concepts were added on top of the
  existing physics/paths/containers/sliding-window/Loom-bridge feature set:
  - Flocking (separation/alignment/cohesion) and rigid formations
    (`mm_flockStep`, `mm_setFormationOffset`, `mm_applyFormations`).
  - Per-mask finite state machines (`mm_fsmDefine`, `mm_fsmAddTransition`,
    `mm_fsmFire`).
  - JSON serialization / restore of a whole engine (`mm_serialize`,
    `mm_deserialize`, `mm_deserializeInto`).
  - Spatial grid indexing for fast neighbor queries (`mm_buildSpatialIndex`,
    `mm_spatialNeighbors`).
  - Per-mask timers/cooldowns (`mm_setTimer`, `mm_tickTimers`).
  See [`docs/moving-mask.md`](docs/moving-mask.md) and
  [`examples/moving_mask_extended_demo.rin`](examples/moving_mask_extended_demo.rin).
- `lib/movingmask.og.rin` v1.1.0 — added an FPS meter and a fixed-timestep
  accumulator, since none of the library's motion functions read a real
  clock: `mm_fpsInit`/`mm_fpsUpdate`/`mm_fps` compute a smoothed
  frames-per-second reading over a 0.5s window from a host-supplied real
  delta-time, and `mm_fixedStepInit`/`mm_fixedStepRun`/`mm_fixedStepAlpha`
  decouple deterministic physics ticks from a variable render rate (with a
  `maxSteps` cap against the classic "spiral of death").
- `lib/movingmask.og.rin` v1.2.0 — five more integrated concepts, rounding
  the library out into a small mobile mini-game toolkit:
  - Viewport / responsive sizing: `mm_setViewport`, `mm_toViewportPercent` /
    `mm_fromViewportPercent` / `mm_setPositionPercent`, `mm_viewportClass`
    (Material Design 3 compact/medium/expanded breakpoints), and
    `mm_scaleForViewport` for scaling values across screen sizes.
  - Loading/progress bar kinds: `mm_progressCreate` with `"linear"`,
    `"circular"`, `"indeterminate"`, `"segmented"`, and `"buffer"` kinds,
    ticked via `mm_progressTick` and exported via `mm_progressWarp`.
  - Touch gestures and framerate-independent motion smoothing:
    `mm_touchBegin`/`mm_touchMove`/`mm_touchEnd` (tap vs swipe
    classification), `mm_touchDrag`, and `mm_smoothFollow`/
    `mm_smoothVelocity` (exponential decay smoothing).
  - Coins/collectibles and score: `mm_spawnCoin`, `mm_collectCoinsNear`
    (auto-collects nearby coins and fires `"coinCollected"`), and
    `mm_addScore`/`mm_score`/`mm_resetScore`.
  - Virtual joystick and control buttons: `mm_joystickCreate`/
    `mm_joystickUpdate`/`mm_joystickApplyToVelocity`, and
    `mm_buttonDefine`/`mm_buttonPress`/`mm_buttonConsumeJustPressed`
    (edge-triggered button state).

## [1.0.0] — 2026-08-28

### Added
- First stable Rin release.
- Core language and runtime documentation.
- Variables, functions, conditions, loops, arrays, maps and objects.
- Containers and data processing.
- Pipelines and RinFlow foundation.
- Standard libraries.
- HTTP, storage and validation capabilities.
- UI/Loom and Android runtime documentation.
- Open-source MIT licensing.
- Contribution and security policies.

### Status
Rin 1.0.0 — Stable Release

## Core language completion pass

- Added expression-level conditional operator `condition ? whenTrue : whenFalse`.
- Added structured `try { ... } catch (name) { ... }` handling.
- Added `throw expr;` with arbitrary Rin values and structured caught-error maps.
- Unhandled user exceptions now terminate through the normal interpreter error channel.
- Added `samples/core_completion.rin` and `tools/test_core_completion.cpp` regression coverage.
