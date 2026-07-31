# Mirror Loom — Live Preview reference client

`mirror_loom_live_preview.html` is a **self-contained JS reference implementation** of the
Loomtime pipeline (its own small Lexer/Parser/Fabric/Loom/Dye/Shuttle, all in one file) used to
prototype and demo the Live Preview UX (grid, guidelines, safe-area, multi-device, inspector,
zoom, FPS/memory/layout/paint stats) without needing a native build.

The **real** engine now lives in `app/src/main/cpp/loom/` and is written in C++ against RIN's
actual lexer/parser/AST (see `docs/loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md`, Appendix A). A
production Mirror Loom server would run that native engine (compiled to WASM for a desktop/VSIX
client, or called via the JNI bridge already wired in `jni_bridge.cpp` /
`RinEngine.kt#renderView()` for the Android client) instead of this JS reference pipeline, and
speak the same JSON Fabric format that `rin_loom_c_api.cpp`'s `rin_loom_render_json()` already
produces. Swapping the transport is the only remaining step — the wire format matches on both
sides today.

Open `mirror_loom_live_preview.html` directly in a browser to try it.

## Tests
```
cd tests
python3 extract_core.py   # regenerates core_test.js from the HTML file
node test_core.js         # runs the correctness suite (layout, Shuttle, Warp, error recovery)
```
