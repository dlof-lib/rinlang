# Rin Loom — Design Tokens + Color Engine

This is the first implemented slice of **Rin Loom**, built as an extension of the existing
Loomtime rendering engine (`docs/loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md`), not a parallel
system. It adds semantic color, spacing, radius, and typography tokens on top of Loomtime's
existing `@view.<Kind>=name ... .end/view` strand grammar — no CSS, no selectors, no cascade.

## What's implemented

- **Color Engine** (`app/src/main/cpp/loom/rin_loom_tokens.h`): semantic color roles —
  `primary`, `secondary`, `success`, `danger`, `warning`, `info`, `neutral`, `surface`,
  `background`, `text`, `text_muted`, `border` — resolved against an active **Theme** (Pattern
  Book), instead of raw hex scattered through source.
- **Theme (Pattern Book)**: built-in `Dark` (default) and `Light` themes. New themes are declared
  in source with `@theme=Name ... .end/theme` (see grammar below) and can override any subset of
  the 12 roles; unset roles inherit from the currently-active theme.
- **Spacing tokens**: `compact` (8), `normal` (12), `comfortable` (16), `spacious` (24). Accepted
  anywhere `padding=`/`gap=` was already accepted — a plain number still works.
- **Radius tokens**: `sharp` (0), `soft` (6), `round` (12), `pill` (clamped to half the strand's
  settled size at paint time).
- **Typography tokens**: `caption`/`body`/`label`/`subtitle`/`title`/`heading`/`code`, each a
  `{size, weight}` pair. Accepted anywhere a Text/Button `size=` attribute was already accepted.

## Grammar addition: `@theme`

```
@theme=<Name>
  active=true;        // optional: makes this theme active immediately
  primary="#7C5CFF";  // any of the 12 semantic role names, value is "#RRGGBB"
  danger="#E5484D";
.end/theme
```

This mirrors the existing `@view`/`warp` block forms exactly (see `rin_parser.cpp`,
`Parser::themeDeclaration()`) — flat attributes only, no nesting, closed with `.end/theme`.
`rin_ast.h` adds `ThemeStmt`; nothing existing was changed or renamed.

## Using tokens in a Strand

```
@theme=Midnight
  active=true;
  primary="#7C5CFF";
.end/theme

@view.Card=alert
  padding=comfortable; radius=round;
  @view.Text=title text="Storage almost full"; size=title; .end/view
.end/view

@view.Button=confirm label="Upgrade"; tone=primary; .end/view
```

`tone=` is the primary spelling for semantic color on any Strand kind. `color=` still works for
backward compatibility and now also accepts a role name in addition to `#RRGGBB`. Banner's
existing `type=` attribute (info/success/warning/error/action/progress/custom) maps onto the
same theme roles a `tone=` would use, so a Banner and a Button showing the same severity always
render the same color.

## Backward compatibility

No existing `.rin` source is affected: `color="#RRGGBB"`, numeric `padding=`/`gap=`/`size=`, and
every existing `@view`/`warp`/`container` construct behave exactly as before. Tokens are pure
opt-in sugar resolved on top of the same attribute slots. See
`tools/test_loom_tokens.cpp` for the regression test that pins this down (raw hex + raw padding
number still produce the exact same output with zero `@theme`/`tone`/token usage anywhere).

## Try it

```
samples/loom_tokens_demo.rin   — @theme + tone= + spacing/radius/typography tokens together
samples/loom_showcase.rin      — original Loomtime sample, unmodified, still works

./loomc samples/loom_tokens_demo.rin
```

## Not yet implemented (see the full Rin Loom request)

This slice covers tokens §8/§9/§10 (colors, spacing, radius, typography) plus the Button
component library from §4 — real variants (fill/outline/ghost/link/glass/gradient over the 7
tone= roles), sizes (xs/small/medium/large/xl), states (normal/hover/pressed/focused/disabled/
loading/selected, with disabled/loading gating real Needle tap dispatch, not just paint), and
Button accessibility (role + accessible name via `a11y_label=`, defaulting to the visible label).
Still not started: a Responsive Engine (§7), a real Animation/Weft Engine (§18). `Box` (the
resolved Container naming collision), Grid, and the rest of the missing-components pass are done
— see "The Container naming collision — resolved: `Box`" and "Missing-components pass" below.

## The Container naming collision — resolved: `Box`

`Container` already means a data/storage construct in this codebase (`@container`,
`ContainerKind::{TABLE, DOC, OBJECT, PORTAL, BLOCK, STICKER, AUKT}` — see `rin_ast.h`). The
spec's layout Container (§5/§6: padding/spacing/alignment/sizing/constraints, `fit`/`hug`/`fill`/
`expand`/`fixed`/`auto`) needed a different name in this codebase to avoid a silent semantic
collision.

Resolved as option (b) from the two paths this note used to flag: a new `StrandKind::BOX`
(`rin_loom_strand.h`), distinct from `Card` (which keeps its own elevation-styled default paint).
The tags `Container`/`Panel`/`Frame` are all accepted as aliases for `Box` in
`strandKindFromTag()`, so `.rin` source written against the original spec's own vocabulary
(`@view.Container=...`) parses unchanged — there's no *grammar* collision with `@container=...`
(a different top-level statement, matched before `@view.` ever runs), only the conceptual one
this note originally flagged.

```
@view.Box=panel               // or @view.Container=panel / @view.Panel=panel / @view.Frame=panel
  padding="comfortable"; sizing="fill";
  @view.Text=t text="Plain padded box"; .end/view
.end/view
```

Try it: `samples/loom_missing_components_demo.rin`; `tools/test_loom_missing_components.cpp` is
the regression suite.

## Missing-components pass (§27 stdlib gap)

The rest of the easy, engine-primitive-reusing wins from the stdlib list, added alongside `Box`:

- **Grid** (`columns=`, `gap=`) — fixed-column-count, row-major placement.
- **Wrap** (`gap=`) — a flow Row that breaks onto a new line instead of overflowing.
- **Spacer** — defaults to `sizing="expand"` with no attribute needed; claims a *fair share* of
  the remaining main-axis space in a Row/Column even when siblings follow it (this required
  turning `layoutLinear`'s single top-to-bottom measuring pass into a real two-pass flex
  distribution — see the comment on `isMainAxisFlexible`/`layoutLinear` in
  `rin_loom_layout.h` for why a one-pass version silently starved trailing siblings to zero
  width).
- **Badge**, **Progress**, **Checkbox**, **Switch**, **Avatar** — small self-painting leaves with
  their own `Dye` paint functions (`paintBadge`/`paintProgress`/`paintCheckbox`/`paintSwitch`/
  `paintAvatar` in `rin_loom_paint.h`).
- **Input**/**TextArea** — bordered field boxes sized around `value=`/`placeholder=`; design-time
  box only, same scope as Image/Video/WebView (no real caret/typing model).
- **Dialog**/**Modal** — collapses to zero size unless `open="true"`, the same rule
  Drawer/Menu/Banner already use for `open=`/`visible=`. Scope note: lays out in normal document
  flow at its content size; true screen-centered overlay positioning is a
  `@view.Stack align="center" valign="center"` composition, the same pattern Stack's own doc
  comment already recommends for badges/FABs.
- **Tabs**/**TabItem** — a Row of button-like items. Deliberately reuses Button's existing
  `StrandState` machinery (`state="selected"`) instead of inventing a separate "active tab"
  concept — a real per-tab highlight driven by a warp cell is a normal `onTap=` handler (a real
  `fun`, since switching *multiple* tabs' state needs multi-cell writes Needle's built-in
  increment/decrement/toggle/set shortcuts don't cover) exactly like any other interactive Strand.
- **Tooltip** — a small self-contained text bubble, same overlay-positioning scope note as Dialog.

Not in this pass: a Responsive Engine (§7), a real Animation/Weft engine (§18), and Menu/Dropdown
(already achievable today by composing the existing `Menu`+`Button`+`open=` the same way Banner's
own `menu { item ... }` composes — see §14 above — so it wasn't treated as a missing primitive).

## Button component library (§4)

```
@view.Button=confirm
  label="Upgrade plan";     // visible text (unchanged meaning from before Loom existed)
  tone="primary";            // any of the 12 semantic roles from the Color Engine above
  variant="outline";         // fill (default) | outline | ghost | link | glass | gradient
  size="large";               // xs | small | medium | large | xl
  state="selected";           // normal (default) | hover | pressed | focused | disabled | loading | selected
  disabled=true;               // shorthand for state="disabled" -- also gates Needle's onTap dispatch for real
  loading=true;                 // shorthand for state="loading"
  a11y_label="Upgrade to the paid plan"; // accessible name; defaults to label= if omitted
  onTap=doUpgrade;
.end/view
```

- **Variants** are treatments, not colors: `fill` paints the tone as a solid background;
  `outline` paints no fill and a tone-colored border instead (`app/src/main/cpp/loom/
  rin_loom_paint.h`'s new `STROKE_RECT` draw op); `ghost`/`link` paint no box at all, only
  tone-colored text; `glass`/`gradient` are approximated in the built-in PPM rasterizer (no
  alpha channel or multi-stop gradient primitive there) but still export their real
  `variant=`/`tone=` attributes in the JSON Fabric for a real client renderer to draw properly.
- **Sizes** change real settled layout geometry (height/padding/font size), not just paint — see
  `resolveButtonSize()` in `rin_loom_tokens.h`. `medium` is bit-for-bit identical to the
  pre-Loom hardcoded Button defaults (44px height, 16px padding, 16px font), so a `.rin` source
  with no `size=` at all is unaffected.
- **States**: `disabled=true` isn't just dimmed paint — `rin_loom_needle.h`'s `dispatchTap` now
  skips a disabled Strand's `onTap` entirely and keeps bubbling to whatever's underneath, the
  same way a real disabled native button consumes no tap.
- **Accessibility**: every Strand's JSON export (`fabricToJson`) now carries an additive `a11y`
  object — `role` (per StrandKind: Button→"button", Banner→"alert", Image→"image", ...),
  `name` (the accessible name), `state`, and `disabled`. Existing JSON consumers that don't know
  this key are unaffected; nothing existing was removed or renamed.

Try it: `samples/loom_button_library_demo.rin` exercises all 6 variants × the 5 sizes × 5 states
end to end. `tools/test_loom_button.cpp` is the regression suite (20 checks, including the
Needle-gating and backward-compatibility ones above).

## Sizing modes / Constraint System (§5/§6)

```
@view.Card=panel
  sizing="fill";        // fit | hug | auto (content-sized, default) | fill | expand (claim parent's full bounded extent) | fixed (defer to width=/height=)
  min_width="200";       // independent of width=/height= -- a floor without pinning the exact size
  max_width="600";
  min_height="80";
  max_height="400";
.end/view
```

`sizing=` applies to any StrandKind, not a dedicated new "Container" one — see the naming note
above. `fill`/`expand` only affect **width** in practice today: the root constraint chain bounds
width from the very first call in `loomc.cpp` (`{0, rootWidth, 0, 1e9}`), but leaves height
unbounded (the `1e9` sentinel) almost everywhere, so there's usually no finite parent height to
fill against — a Strand nested inside something with an explicit `height=` still gets real fill
height too. An explicit `width=`/`height=` always wins over `sizing="fill"` if both are present.

Try it: `samples/loom_sizing_demo.rin` — three Cards (content-hugging / fill / min-floored) at
the same root width, each landing at a visibly different measured width.
`tools/test_loom_sizing.cpp` is the regression suite (12 checks).

## Banner (§13/§14)

```
@view.Banner=update
  tone="success"; title="Update ready"; message="Version 2.4 is ready."; closable=true;
  @view.Row=actions        // full composition still works, combined with the shorthand above
    @view.Button=install label="Install now"; tone="success"; .end/view
  .end/view
.end/view
```

Banner already went through the exact same `@view.<Kind>=name` pipeline as every other
StrandKind before this slice — composing one by nesting real `@view.Text`/`@view.Button`
children (for an icon, a title, actions, ...) already worked with zero extra code. What's new
here is pure shorthand for the spec's own common case:

- `title=`/`message=` auto-synthesize two Text children (typography tokens `title`/`body`) —
  prepended before any manually-nested children, so the two approaches combine freely.
- `closable=true` auto-creates a Warp cell (`<bannerName>_open`, defaulting to `"true"`), appends
  a synthesized close Button wired to `onTap=toggle(<cell>)` through the exact same `rawExpr`
  mechanism a hand-written `onTap=` goes through, and binds the Banner's own `visible=` to that
  cell — unless the source already set `visible=` explicitly, which always wins.
- Tapping the close button is a **real** dismissal through Needle, not a cosmetic flag: it
  actually flips the Warp cell, and the Banner's `visible=` (subscribed the same way any
  Warp-bound attribute is) re-resolves and collapses the Banner to zero size on the next layout.

**Scope note**: `applyBannerConveniences` only runs on the cold-pipeline build path
(`runColdPipeline`) today — a source *edit* that adds/changes `title=`/`message=`/`closable=`
via the hot-reload `Shuttle::diff` path won't re-synthesize until the next cold build. Runtime
(Warp-driven) changes to an already-synthesized Banner, including dismissal, work correctly
either way, since they go through the generic Warp-subscription machinery, not
Banner-specific code.

**Pre-existing bug found and fixed while testing this**: `rin_loom_c_api.cpp`'s
`rin_loom_session_tap()` called `Shuttle::applyWarpChange()` + `relayout()` but never
`recomputeHashes()`. Tension's layout cache is keyed on each Strand's `contentHash`, and
`applyWarpChange()` mutates an attribute's resolved value directly without touching it — so any
Warp-driven change that should resize a Strand (a Banner collapsing on dismissal, but just as
much `loom_showcase.rin`'s own counter Text growing from one digit to two) could report stale
geometry through the real session API. Reproduced independently of Banner
(`warp count`; a Text bound to it; 15 taps of an increment Button) before fixing it — width
stayed frozen at the 1-digit measurement through 16 real taps without the fix, and resized
correctly with it. Fixed with one line (`loom::recomputeHashes(sess->state.fabric);` before
`relayout(sess);`); `tools/test_loom_banner.cpp`'s dismissal test would not pass without it.

Try it: `samples/loom_banner_demo.rin` — three Banners (plain title/message, title/message +
composed actions + closable, and closable-only with hand-composed content).
`tools/test_loom_banner.cpp` is the regression suite (18 checks, including the real Needle
dismissal end-to-end).


