# Rin official single live preview fix

- The editor now recognizes `@loop=name`, `@loop.Kind=name`, `@view=name`, and `@view.Kind=name` as Loom roots.
- `runProgram()` and manual Live Preview use the same root detector, so `@loop=screen` opens the real preview.
- Native C++ remains the single authoritative Rin execution + Loom layout/runtime.
- `LoomFabricView` remains the only preview surface.
- HTML is an embedded capability inside that same preview: `WebView` nodes with `html=`, `url=` or `src=` can render HTML/CSS/remote web content where Canvas is not suitable.
- Inline HTML clicks are forwarded back to `LoomPreviewManager.tap()` and therefore use the same native Rin handler path; JavaScript never executes Rin code.
- Remote web pages do not receive the Rin bridge.
- C++ pipeline accepts `@loop` as an official root in both cold and hot paths.
