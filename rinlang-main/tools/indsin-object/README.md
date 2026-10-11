# indsin-object

The official **Indsin Object** translator and interpreter for the [Rin](../../README.md) language.

* **Translator** — turns an Indsin Fabric (the render tree produced by the Indsin engine, or any hand-written logical tree) into semantic, accessible, themed HTML + CSS.
* **Interpreter** — turns user interaction on that HTML (tap, long-press, double-tap, hover, change, select, close, remove, page) into normalised Indsin events for the host and, when attached, the engine.
* **Tooling** — the indsin icon set, `.indsin` file association for Linux / Windows / macOS / Android, and dedicated syntax highlighting (TextMate grammar, VS Code extension, Kotlin and JavaScript highlighters).

Dependency-free, ES5-compatible core. Runs in browsers, Android WebView, WKWebView, Electron, React Native WebView, WebView2, Node, Deno, Bun and workers.

## Coverage

All 65 element kinds (64 built-ins + `Custom`) and every tag alias accepted by the engine; the complete design-token system (spacing, radius, typography, durations, opacity, borders, icon sizes, elevation, z-layers, breakpoints, button sizes); 11 built-in themes plus custom themes and `themeFromSeed`; responsive `_sm/_md/_lg/_xl` overrides; effects (`fade`, `slideUp/Down/Left/Right`, `pulse`, `shake`, `bounce`, `grow`), easings and `stagger`; overlays (Dialog / Drawer / Tooltip + scrim); RTL; accessibility roles.

## Quick start

```js
const IO = require('indsin-object');             // or <script src="src/indsin_object.js">
const html = IO.toDocument(frameJson, { theme: 'dark' });   // standalone, offline page, interpreter included
```

Live view in a browser or WebView:

```js
const view = IndsinObject.mount('#app', null, { theme: 'auto', onEvent: (ev) => console.log(ev) });
view.applyFrame(frameJson);
```

Command line:

```sh
node bin/indsin-object.js frame.json -o page.html --theme light
node bin/indsin-object.js app.indsin --engine ./rin_indsin_run -o page.html   # render a source file via the engine
node bin/indsin-object.js --validate frame.json
node bin/indsin-open.js app.indsin                                            # preview or highlighted source in the browser
```

## Layout

| Path | Purpose |
|---|---|
| `src/indsin_object.js` | Translator + interpreter (single UMD file) |
| `src/indsin_highlight.js` | Dependency-free indsin syntax highlighter |
| `src/host.template.html`, `build.js` | Generate the self-contained `indsin_object.html` host page (also copied to `app/src/main/assets/indsin/`) |
| `bin/indsin-object.js` | CLI translator / validator |
| `bin/indsin-open.js` | Launcher used by the `.indsin` file association |
| `types/` | TypeScript definitions |
| `hosts/` | Platform hosts: Swift, Node example (Android host: `app/src/main/java/com/dlof/rinlang/IndsinObjectHost.kt`; C++: `app/src/main/cpp/indsin/rin_indsin_object_web.h`) |
| `integration/` | `.indsin` file association: Linux, Windows, macOS |
| `scripts/gen_syntax.js` | Regenerates the grammar and highlighter lexicon from the translator's catalogue |
| `tests/` | Zero-dependency test suite (`npm test`) |

## Build and test

```sh
npm run build   # regenerates syntaxes + builds indsin_object.html
npm test        # 52 tests
```

## Security

All text and attributes are escaped; URLs pass a scheme policy (`javascript:`, `vbscript:`, `file:` and non-image `data:` are refused; override with `urlPolicy`); colours and lengths are validated; embedded frames are sandboxed; only trusted `postMessage` sources are accepted.

See [docs/indsin_object.md](../../docs/indsin_object.md) for the API reference and [docs/indsin_tooling.md](../../docs/indsin_tooling.md) for the icon, file association and highlighting setup.
