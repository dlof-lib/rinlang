# Overlay Engine patch

Closes the honest scope note the missing-components patch shipped with:

> Dialog/Tooltip compute their own box only; there is no real overlay/positioning engine —
> screen-centered positioning is achieved by wrapping in `@view.Stack align=center valign=center`,
> and Tabs reuse the existing `state="selected"` attribute instead of inventing a new concept.

This patch builds the actual overlay engine: a separate z-layer, a real scrim, and hit-test event
blocking, so Dialog/Tooltip are complete.

## What changed

- **`app/src/main/cpp/loom/rin_loom_overlay.h`** (new) — `OverlayLayer`/`buildOverlayLayer()`
  (repositions an open Dialog against the *viewport*, centered via the existing
  `alignOffset`/`Loom::translate`, honoring `align=`/`valign=`/`modal=`/`scrim=`/`dismissible=`;
  anchors a Tooltip via a new `anchor="targetName"` attribute with `placement=` and edge-clamping)
  and `hitTestOverlayLayer()` for modal event blocking.
- **`rin_loom_layout.h`** — `isOverlayStrand()` plus explicit flow-exclusion logic in
  `layoutLinear()`/`layoutStack()`: an open Dialog / anchored Tooltip no longer inflates its
  parent's size, while its own content box is still measured correctly against its full
  available space (not a budget derived from siblings that don't concern it anymore).
- **`rin_loom_paint.h`** — `DrawOp::SCRIM_RECT` and `Dye::paintWithOverlay()`: paints the main
  document, then the overlay layer strictly afterward in its own back-to-front order — a real
  second z-layer instead of relying on tree/declaration order.
- **`rin_loom_needle.h`** — `dispatchTapWithOverlay()`: tries the overlay layer before the normal
  document hit-test; a tap outside a modal Dialog's box is consumed by its scrim and never reaches
  the document underneath, with dismiss-on-scrim-tap wired to the same warp-cell resolution
  `dispatchTap()` already uses for `onTap` arguments.
- **`rin_loom_c_api.h`/`.cpp`** — `rin_loom_session_set_viewport()` (the *visible screen* height,
  distinct from the unbounded scrollable content height everything else lays out against); the
  session's JSON envelope now carries an `"overlays"` array (id/name/kind/modal/scrim/box/
  scrimRect) so any renderer can draw the layer correctly; `rin_loom_session_tap` now dispatches
  through `dispatchTapWithOverlay`.
- **`rin_loom.h`** — includes the new header in the umbrella.
- **`app/src/main/java/com/dlof/rinlang/LoomFabricView.kt`** — consumes the `"overlays"` array:
  skips an overlay node (and its subtree) during the normal recursive walk, then paints the whole
  overlay layer afterward (scrim, then each entry's own subtree) in the same stacking order the
  native `paintWithOverlay()` uses. Adds `Kind.DIALOG`/`Kind.TOOLTIP` paint cases (previously fell
  through to the generic container box with no dedicated look, and Tooltip's `text=` was never
  actually drawn).
- **`app/src/main/java/com/dlof/rinlang/LoomPreviewActivity.kt`** — passes the result JSON's
  `"overlays"` array through to `setFabric()`.
- **`tools/test_loom_overlay.cpp`** (new, 33 checks, all passing) — flow exclusion, viewport
  centering, `align=`/`valign=` overrides, scrim defaults/overrides, modal event blocking,
  dismiss-on-scrim-tap (and `dismissible="false"`), Tooltip backward compatibility (no `anchor=`
  → unchanged inline box) and anchoring/clamping, and multi-overlay stacking order.

## New/changed `.rin` attributes

- `Dialog`: `align=`/`valign=` (default `"center"`/`"center"`), `modal=` (default `"true"`),
  `scrim=` (default `"true"`, ignored if `modal="false"`), `dismissible=` (default `"true"`).
- `Tooltip`: `anchor="targetName"` (opt-in — without it, Tooltip is unchanged, still an inline
  content-sized box) + `placement=` (`"top"` default, or `"bottom"`/`"left"`/`"right"`).

Note: the attribute is `anchor=`, not `for=` — `for` is a reserved keyword in rinlang's lexer
(C-style `for` loops).

## Naming caveat

`isOverlayStrand()`'s own comment documents a dead end worth knowing about if this file is
extended later: an earlier version tried excluding an overlay Strand from its parent's flow by
having `Loom::layout()` itself return `{0,0,0,0}` for it. That silently corrupted
`layoutLinear`'s two-pass flex budget, since that pass calls `layout()` on the same child twice
(an unconstrained probe, then a real placement call built from the probe's *returned* main-axis
size) — a returned 0 fed back in as a `maxH=0` constraint on the "real" call, zeroing the Dialog's
own box instead of merely excluding it from its parent's size. The fix keeps `layout()` itself
honest (always returns the Strand's real measured size) and does the skip explicitly, once, in
each container helper's own bookkeeping (`layoutLinear`/`layoutStack`).

## Known scope limits (not covered by this patch)

- Flow-exclusion is implemented in `layoutLinear` and `layoutStack` (covers Column/Row/Tabs/
  Header/TopBar/BottomBar/Drawer/Menu). `layoutTable`/`layoutScaffold` are not patched — a Dialog
  declared directly inside a Table cell or a Scaffold's own slot layout is an unusual placement
  that still measures correctly but may still occupy flow space in those two containers
  specifically.
- The scrim's translucency is a renderer-side convention keyed on `DrawOp::SCRIM_RECT` /
  `overlays[].scrim`, since `Color` has no alpha channel in this codebase; `LoomFabricView.kt`
  picks a fixed ~55% opacity.

## Build / test

```
cd app/src/main/cpp
g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_overlay.cpp rin_lexer.cpp \
    rin_parser.cpp rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp \
    diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp \
    diagnostics/source_manager.cpp -lz -o test_loom_overlay
./test_loom_overlay
```

All 33 checks pass; `test_loom_missing_components`, `test_loom_banner`, `test_loom_button`,
`test_loom_sizing`, and `test_loom_tokens` still pass unchanged (zero regressions).
