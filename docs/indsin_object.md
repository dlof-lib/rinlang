# indsin-object — API reference

## Input

| Form | Detected by | Mode |
|---|---|---|
| Engine frame `{ok, fabric, overlays, …}` | presence of node geometry (`x/y/w/h`) | **absolute** — pixel-exact positions from the engine |
| Logical tree `{kind, attrs, children}` | no geometry | **flow** — responsive flex layout |
| JSON string of either | — | parsed first |

## Functions

| Function | Returns |
|---|---|
| `translate(input, options)` | `{ html, css, mode, width, height, theme, issues, unknown }` |
| `toHtml(input, options)` | fragment: `<style>` + markup (+ interpreter script) |
| `toDocument(input, options)` | complete `<!doctype html>` page |
| `mount(target, input?, options)` | live controller: `render/applyFrame, setTheme, configure, on, setOpen, toHtml, attachSession, attachWasm, detach, destroy, issues` |
| `validate(input)` | `{ ok, errors[], warnings[] }` (unknown kinds, invalid colours/tones, …) |
| `registerTheme / themeFromSeed / themes / theme` | theme management |
| `registerIcon / icons` | icon registry |
| `registerComponent(tag, fn)` | renderer for a `Custom` tag or an additional element |
| `registerObject(id, data)` | data for `Object` elements |
| `kinds / catalog / aliases / supports / resolveKind / tokens` | introspection |
| `runtimeSource()`, `stylesheet()`, `sample()`, `selfTest()`, `glue(lang)` | embedding helpers |

## Options

`theme` (name · `auto` · object, default `dark`), `dir` (`ltr|rtl|auto`), `width`, `responsive`, `lang`, `title`, `urlPolicy(url, purpose)`, `allowFileUrls`, `objects`, `animations`, `interactive`, `script`, `fit`, `navigate`, `className`, `targetOrigin`, `onEvent`.

## Events

Every interaction is normalised to `{ type, id, path, kind, value?, x?, y?, … }` with `type` ∈ `tap · doubletap · longpress · hoverenter · hoverexit · change · select · remove · page · close · error`, and delivered on all bridges at once:

`onEvent` callback · window event `indsin:event` · `IndsinAndroid.onEvent(json)` · `webkit.messageHandlers.indsin` · `IndsinHost.onEvent(json)` · `ReactNativeWebView.postMessage` · `chrome.webview.postMessage` · `parent.postMessage({indsin:'event', event})`.

## Host page URL parameters

`?theme=dark|light|auto|<ThemeName>` · `?dir=ltr|rtl|auto` · `?lang=ar` · `?demo=0` · `?animations=0` · `?frame=<url-encoded JSON>`

## Platform hosts

| Platform | File |
|---|---|
| Android | `app/src/main/java/com/dlof/rinlang/IndsinObjectHost.kt` (+ asset `app/src/main/assets/indsin/indsin_object.html`) |
| iOS / macOS | `tools/indsin-object/hosts/swift/IndsinObjectHost.swift` |
| C++ / Emscripten | `app/src/main/cpp/indsin/rin_indsin_object_web.h` |
| Node | `tools/indsin-object/hosts/node/example.js` |
| Electron / React Native / WebView2 | `IndsinObject.glue('electron' | 'react-native' | 'webview2')` |

## Engine fidelity notes

* Colour precedence follows the engine: `tone` → `bg` → `color` → `background` → kind default; the default fill of a kind is the theme background (invisible), so `Step`/`Timeline` markers default to the primary tone.
* Default gaps are 0, as in the engine; use `gap=` explicitly.
* Pagination renders a window of pages around the current page, like the engine.
