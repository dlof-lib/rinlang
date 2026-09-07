# RinLang — Build System

One entry point that drives the three build paths this repo already has
(Gradle for the Android app, Emscripten for the browser WASM engine, g++ for
the Desktop client) and collects every output under `dist/`. It doesn't
replace or duplicate any of those — each target runs the exact same commands
the project's own CI/scripts already use.

## Quick start

```bash
make apk            # -> dist/apk/*.apk            (debug; needs Gradle + Android SDK/NDK)
make apk-release     # -> dist/apk/*.apk            (release; also needs a signing keystore, see below)
make web             # -> dist/web/rin_engine.{js,wasm}  (needs Emscripten's emcc)
make desktop         # -> dist/desktop/rin_loom_desktop  (needs g++/clang++ + libx11-dev)
make all             # builds every target whose toolchain is present; skips the rest with a
                      # clear message instead of failing the whole run
make clean           # removes dist/, app/build/, web/rin_engine.{js,wasm}
```

Or call the script directly (`make` just wraps it):

```bash
./scripts/build_all.sh apk [debug|release]
./scripts/build_all.sh web
./scripts/build_all.sh desktop
./scripts/build_all.sh all [debug|release]
./scripts/build_all.sh clean
```

## Per-target requirements

| Target  | Needs | Detection |
|---|---|---|
| **apk** | `./gradlew` or system `gradle`; `ANDROID_HOME`/`ANDROID_SDK_ROOT` pointing at an SDK+NDK install (see `.github/workflows/build_apk.yml`'s setup steps for the exact NDK/CMake package versions the CI uses) | checked before invoking Gradle; missing either → skip with instructions, no partial run |
| **web** | `emcc` (Emscripten) on `PATH` | checked before running `web/build_rinhtml_wasm.sh` |
| **desktop** | `g++`/`clang++`, X11 dev headers (`libx11-dev` / `libX11-devel`) | verified with a real one-line compile+link probe, not just "is g++ installed" |

`release` APKs are signed using `signing/release.keystore` /
`signing/keystore-credentials.txt` exactly as `.github/workflows/build_apk.yml`
does — see that workflow's comments for how those files are generated and the
private-repo security note attached to them. `debug` needs no keystore.

## What's actually new here vs. before this session

Nothing about *how* any individual target builds changed — `gradle
assembleRelease`, `web/build_rinhtml_wasm.sh`, and the desktop g++ command
documented in `tools/README_DESKTOP.md` are unchanged and still work
standalone. What's new is:

- `scripts/build_all.sh` — the unified CLI, with real toolchain detection per
  target (not just "did the command exit 0", but "is the tool even on PATH"
  up front, with an actionable install hint) and a single `dist/` output
  layout across all three.
- `Makefile` — a thin `make apk` / `make web` / `make desktop` / `make all` /
  `make clean` wrapper around that script.

## Verified this session

Run inside this sandbox (no Android SDK/NDK or Emscripten available here, no
network to install them):

- `./scripts/build_all.sh apk` → correctly detected the missing Gradle
  wrapper/SDK and skipped with the install hint (exit 1, as a single
  explicitly-requested target should).
- `./scripts/build_all.sh web` → correctly detected the missing `emcc` and
  skipped the same way.
- `./scripts/build_all.sh desktop` → **actually compiled and linked** the
  real desktop client end-to-end via the script (not manually), producing
  `dist/desktop/rin_loom_desktop`; ran it (`shot` mode) against a real repo
  sample and got a correct 390×644 PNG back.
- `./scripts/build_all.sh all` → ran all three, skipped apk/web with
  warnings, built desktop, printed a summary, exited 0 (at least one target
  succeeded).
- `./scripts/build_all.sh clean` → removed `dist/`.
- `make desktop` → same successful build via the Makefile wrapper.

apk/web themselves could not be built end-to-end in this sandbox (their
toolchains require an Android SDK/NDK and an Emscripten install respectively,
neither present here and no network to fetch them) — the two workflow files
already in `.github/workflows/` (`build_apk.yml`, `pages.yml`) are the
place those two continue to actually run and produce artifacts on every
push, exactly as before this session.
