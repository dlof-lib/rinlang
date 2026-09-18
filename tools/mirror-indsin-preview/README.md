# Mirror Indsin — Live Preview reference client

`mirror_indsin_live_preview.html` is a **self-contained JS reference implementation** of the
Indsintime pipeline (its own small Lexer/Parser/Fabric/Indsin/Dye/Shuttle, all in one file) used to
prototype and demo the Live Preview UX (grid, guidelines, safe-area, multi-device, inspector,
zoom, FPS/memory/layout/paint stats) without needing a native build.

The **real** engine now lives in `app/src/main/cpp/indsin/` and is written in C++ against RIN's
actual lexer/parser/AST (see `docs/indsintime/RIN_INDSIN_ENGINE_ARCHITECTURE.md`, Appendix A). A
production Mirror Indsin server would run that native engine (compiled to WASM for a desktop/VSIX
client, or called via the JNI bridge already wired in `jni_bridge.cpp` /
`RinEngine.kt#renderView()` for the Android client) instead of this JS reference pipeline, and
speak the same JSON Fabric format that `rin_indsin_c_api.cpp`'s `rin_indsin_render_json()` already
produces. Swapping the transport is the only remaining step — the wire format matches on both
sides today.

Open `mirror_indsin_live_preview.html` directly in a browser to try it.

## Tests
```
cd tests
python3 extract_core.py   # regenerates core_test.js from the HTML file
node test_core.js         # runs the correctness suite (layout, Shuttle, Warp, error recovery)
```
