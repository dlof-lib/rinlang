# indsin-object 4.0.0 — change summary

## Translator & interpreter
* `tools/indsin-object/src/indsin_object.js` — rewritten as the official translator + interpreter: all 65 element kinds and aliases, design tokens, 11 themes, responsive overrides, effects, overlays, RTL, accessibility, absolute (engine frame) and flow (logical tree) modes, sandboxed media, normalised event bridges for every platform.
* Verified against real frames from the compiled C++ engine and in Chromium.
* `bin/indsin-object.js` (CLI), `build.js`, `src/host.template.html` → `indsin_object.html` (+ Android asset), `package.json`, `types/*.d.ts`, `tests/` (52 tests + real-frame fixtures), `README.md`, docs.

## Icon
* New SVG/PNG/ICO/ICNS set in `assets/branding/indsin/`; Android `ic_indsin_file.xml` redrawn, new `ic_indsin_logo.xml`.

## `.indsin` file association
* Linux (MIME, `.desktop`, icons), Windows (per-user registry, icon, verbs), macOS (app bundle + UTI), Android (`IndsinFileViewer` alias + `IndsinPreviewActivity` reads the document URI).
* `bin/indsin-open.js` launcher: live preview with the engine, highlighted source otherwise.

## Syntax highlighting
* `syntaxes/indsin.tmLanguage.json`, `syntaxes/indsin-lexicon.json`, VS Code extension, Linguist patches, `src/indsin_highlight.js`, Kotlin `indsin` profile in `RinSyntaxHighlighter.kt`.

## Platform hosts
* `IndsinObjectHost.kt` (Android), `IndsinObjectHost.swift` (iOS/macOS), `rin_indsin_object_web.h` (C++/Emscripten), Node example.

## Notes
* Kotlin and Swift sources were reviewed but not compiled in this environment (no Android/Xcode toolchain); the regular expressions used by the Kotlin highlighter were verified with the JVM, the C++ header with `g++ -std=c++17`.
* The npm registry was unreachable during the work, so the TextMate grammar was validated structurally (all regexes compile, scopes exercised) rather than through `vscode-textmate`.
