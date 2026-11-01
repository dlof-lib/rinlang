# indsin tooling — icon, file association, syntax highlighting

## Icon set (`assets/branding/indsin/`)

| File | Use |
|---|---|
| `indsin_icon.svg` | Product icon (master, 512×512) |
| `indsin_file_icon.svg` | Document icon for `.indsin` files (page + layout motif + `.indsin` label) |
| `indsin_icon_{16,32,48,128,256,512,1024}.png`, `indsin_file_icon_*.png` | Raster sizes (Linux hicolor, web, stores) |
| `indsin.ico`, `indsin_file.ico` | Windows (16–256 px) |
| `indsin.icns`, `indsin_file.icns` | macOS |
| `app/src/main/res/drawable/ic_indsin_file.xml` | Android file-list icon (used by `FileIconResolver` for `.indsin`) |
| `app/src/main/res/drawable/ic_indsin_logo.xml` | Android product logo (also the "Open with" icon) |

## `.indsin` file association (`tools/indsin-object/integration/`)

| Platform | Install | Remove |
|---|---|---|
| Linux | `sh linux/install.sh` — MIME type `text/x-indsin`, icons, `indsin Viewer` launcher, default handler | `sh linux/uninstall.sh` |
| Windows | `powershell -ExecutionPolicy Bypass -File windows\install.ps1` (per-user, no admin) — icon, **Open**, **View source**, **Edit** verbs; `indsin.reg` is a manual alternative | `windows\uninstall.ps1` |
| macOS | `sh macos/install.sh` — builds `~/Applications/indsin Viewer.app` with the document type, UTI `dev.rin.indsin` and icons | `sh macos/uninstall.sh` |
| Android | Built in: the `IndsinFileViewer` activity-alias in `AndroidManifest.xml` opens `.indsin` files from any file manager, mail or chat app in the live preview | — |

The launcher (`bin/indsin-open.js`) renders a live preview when `RIN_INDSIN_RUN` points to the engine binary, and otherwise opens a syntax-highlighted view of the file. Requires Node.js 14+ on desktop.

## Syntax highlighting

| Target | File |
|---|---|
| TextMate grammar (`source.indsin`, includes `source.rin`) | `syntaxes/indsin.tmLanguage.json` |
| Lexicon (kinds, attributes, token values) | `syntaxes/indsin-lexicon.json` |
| VS Code extension (grammar, snippets, folding, file icon) | `editors/vscode-indsin/` |
| In-app Android editor | `RinSyntaxHighlighter.kt` — profile for the `indsin` extension |
| Web / Node | `tools/indsin-object/src/indsin_highlight.js` — `IndsinHighlight.highlight(code)` + `stylesheet()` |
| GitHub Linguist | `linguist-submission/indsin.*.patch` |

Colour roles: structure tags (`@view`, `@container`, `.end/view`) · element kinds · attributes · responsive suffixes · design-token values · names · colours and lengths · Rin code (inherited).
The grammar, lexicon and highlighter lists are generated from the translator (`npm run syntax`) so they stay in sync with the supported elements.
