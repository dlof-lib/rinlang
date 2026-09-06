# Changelog

## [Unreleased]

### Added
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
