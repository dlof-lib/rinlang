# Rin Loom Desktop

A real native window for the Loomtime UI engine — the third real Loomtime host, alongside the
Kotlin/Compose Android renderer (`LoomFabricView.kt`, driven over JNI) and the CLI
(`rin_loom_run.cpp`, driven over stdin).

It is **not** a mock or a re-implementation of paint/layout logic. Every pixel on screen comes
from `rin_loom_session_render_rgb()`, which runs the exact same `Dye::paintWithOverlay()` +
`rasterizeToBuffer()` pass the engine's own PNG export already uses (see `rin_loom_paint.h`'s
"the ONE rasterizer in the engine" comment) — this tool only blits that buffer to an X11 window
with `XPutImage`. Taps go through `rin_loom_session_tap()`, the same Needle hit-test/dispatch path
a real `onTap` handler (including a `fun` with a `while` loop) runs through on Android.

Only **Xlib** is used — no SDL/GTK/Qt/Skia/Cairo — matching the rest of `tools/`'s
dependency-light convention, just applied to a real window instead of stdout.

## What's new (native core)

Two small additions to the flat C API (`app/src/main/cpp/loom/rin_loom_c_api.h/.cpp`), used only
by this tool so far but available to any future host the same way `rin_loom_session_render_json`
already is:

- `rin_loom_session_render_rgb(session, &w, &h)` — raw RGB888 buffer of the session's current
  Fabric+Overlay. Free with `rin_loom_free_buffer()`.
- `rin_loom_session_export_png(session, path)` — writes the same frame straight to a real PNG,
  no window/X server involved.

## Usage

```
rin_loom_desktop window <file.rin> [rootWidth=390]
```
Opens a live X11 window against whatever `DISPLAY` is already set (works under Xvfb too).
- **Left-click** anywhere → dispatches a real tap at that pixel (`onTap` runs for real; Warp
  state changes and the window re-lays-out + repaints).
- **`r`** → force re-reads the file from disk and hot-diffs it in (Shuttle), keeping current Warp
  state — same as the CLI's `edit` command.
- the file is also polled for mtime changes every ~400ms, so saving the `.rin` file in an editor
  hot-reloads the window automatically with no keypress.
- **`q`** / **Esc** / the window's close button → quit.

```
rin_loom_desktop shot <file.rin> <out.png> [rootWidth=390]
```
Headless one-shot: renders the file and writes a real PNG, no X server touched at all. Useful
for CI/screenshots or plain headless machines where `window` mode's event loop can't run.

## Building

Same canonical source list the project's own CI already uses for `loomc_native_check`
(`.github/workflows/pages.yml`), plus `-lX11`:

```bash
g++ -std=c++17 -O2 -I app/src/main/cpp -o rin_loom_desktop tools/rin_loom_desktop.cpp \
    app/src/main/cpp/loom/rin_loom_c_api.cpp \
    app/src/main/cpp/rin_c_api.cpp \
    app/src/main/cpp/rin_lexer.cpp \
    app/src/main/cpp/rin_parser.cpp \
    app/src/main/cpp/rin_make.cpp \
    app/src/main/cpp/rin_interpreter.cpp \
    app/src/main/cpp/loader_ui/library_loader_ui.cpp \
    app/src/main/cpp/rin_http.cpp \
    app/src/main/cpp/diagnostics/diagnostic.cpp \
    app/src/main/cpp/diagnostics/source_manager.cpp \
    app/src/main/cpp/diagnostics/diagnostic_engine.cpp \
    app/src/main/cpp/diagnostics/diagnostic_renderer.cpp \
    app/src/main/cpp/clc/clc_container.cpp \
    app/src/main/cpp/clc/clc_compress.cpp \
    app/src/main/cpp/clc/clc_security.cpp \
    app/src/main/cpp/clc/clc_rin_opt.cpp \
    app/src/main/cpp/clc/clc_zip_import.cpp \
    app/src/main/cpp/clc/sha256.cpp \
    -lz -lX11
```

Needs the X11 dev headers (`libx11-dev` on Debian/Ubuntu — already present on most desktop Linux
dev boxes; not needed at all for `shot` mode's headless build path, but the binary above always
links `-lX11` since both modes share one executable).

## Running headless (no real display)

```bash
Xvfb :99 -screen 0 500x900x24 &
DISPLAY=:99 ./rin_loom_desktop window my_app.rin
```

## Verification performed this session

Built and linked clean against the full real engine, then exercised end-to-end under Xvfb:

- `shot` mode produced a correct PNG from a real repo sample (`samples/loom_missing_components_demo.rin`,
  390×644, matching the engine's own layout).
- `window` mode opened a real X11 window; a screenshot (ImageMagick `import`) showed the actual
  Column/Text/Button Strands with the correct resolved colors (`#7C5CFF` primary button, dark
  surface background).
- A synthetic click (small `XSendEvent`-based test helper, not part of this tool) sent to the
  window's real button returned `{"handled":true,"changed":["count"]}` from
  `rin_loom_session_tap` — confirming the click reaches Needle's real hit-test and runs real Rin
  code (`onTap=bump()`), not a stub.
- Editing the `.rin` file on disk while the window was running triggered an automatic hot-reload
  (mtime poll → `rin_loom_session_update_source`), confirmed by a before/after screenshot diff
  localized exactly to the changed text.

## Kotlin/Android parity check (this session)

Went through `LoomFabricView.kt`'s per-`StrandKind` drawing dispatch against the native
`StrandKind` enum (`rin_loom_strand.h`) looking for kinds the Android renderer doesn't know how
to draw. Everything is covered: `Box`/`Grid`/`Wrap` have no dedicated `Kind.*` case, but that's
intentional — the file's own comment on its generic `else` branch already documents them as
falling through to the same plain-box treatment as `Column`/`Row`/`Stack`/`Custom`, which is
correct because their actual on-screen geometry always comes from the native Loom layout in the
Fabric JSON (Kotlin never re-implements grid/wrap layout itself). No gap found, so no Kotlin
changes were made this session.
