# The Loomtime Engine
### An Original Rendering Engine & Live Preview System for RIN

> Grounded in the actual RIN codebase: the C++ core (`rin_lexer`, `rin_parser`, `rin_ast`, `rin_interpreter`),
> the `@container=name … .end/container` block grammar, the `style="style://dark"` URI convention,
> and the Android host app (`RinEngine.kt`, `CodeEditorController.kt`, VS extension classifier).
> This document extends that foundation with a full UI rendering stack instead of replacing it.

---

## 0. Naming Philosophy — Why "Loom"

RIN already speaks in **containers, groups, volumes, sections, links, tyings, merges** — a vocabulary
of assembly and binding. The rendering layer continues that vocabulary through a single coherent
metaphor: **weaving**. This is not decoration — it gives every subsystem a precise, non-borrowed
identity so nobody can call it "Flutter with new names":

| Loomtime term | Role | Why the metaphor fits |
|---|---|---|
| **Fabric** | The scene graph (the whole tree) | A tree of rendered nodes *is* a woven fabric |
| **Strand** | A single scene-graph node | One thread contributes to the fabric |
| **Loom** | The layout engine | A loom takes strands and gives them position/size |
| **Warp** | The state management engine | Warp threads are the fixed, structural threads |
| **Weft** | The animation/timeline engine | Weft threads move across the warp over time |
| **Shuttle** | The diff/reconciliation engine | The shuttle carries weft back and forth — comparing, not rebuilding |
| **Dye** | The paint engine | Coloring/painting the finished fabric |
| **Bobbin** | The resource manager | Holds thread (assets) ready for the shuttle to consume |
| **Pattern Book** | The theme engine | A weaver's book of reusable patterns/palettes |
| **Needle** | The event/gesture/hit-test engine | The needle is what *points* at a precise spot in the fabric |
| **Bolt** | The plugin API | A "bolt of fabric" — a new roll of material added to the loom |
| **Loupe** | The inspector mode | A weaver's/jeweler's magnifier for inspecting fine detail |
| **Tension** | The performance optimizer | Tension control keeps weaving efficient and taut |
| **Mirror Loom** | The Live Preview engine | A live reflection of the loom's current work, updated stitch by stitch |
| **Snag** | The error recovery system | A snag is an isolated flaw — contained, not fabric-destroying |

Together: **The Loomtime Engine**. Nothing here is a Widget, Element, View-Controller, Fiber, or
QQuickItem by another name — each concept is defined from RIN's own grammar outward.

---

## 1. Pipeline Overview

```
 RIN Source (.rin)
      │
      ▼
 ① Lexer  (rin_lexer)  ── tokens (extends TokenType with AT/DOT/COLON already present)
      │
      ▼
 ② Parser (rin_parser) ── recursive-descent, extends @container grammar family
      │
      ▼
 ③ AST     (rin_ast)   ── new ViewStmt hierarchy alongside ContainerStmt/StyleStmt
      │
      ▼
 ④ Semantic Analyzer   ── resolves Warp bindings, Pattern Book refs, Bolt (plugin) tags,
      │                    validates attribute schemas per Strand kind, type-checks bindings
      ▼
 ⑤ Renderer Engine      ── walks AST → builds/updates the Fabric (Virtual Scene Graph)
      │
      ▼
 ⑥ Fabric (Scene Graph) ── persistent, versioned tree of Strand nodes (retained-mode)
      │
      ▼
 ⑦ Loom (Layout Engine)  ── two-pass Propose/Settle constraint solving → Geometry map
      │
      ▼
 ⑧ Dye (Paint Engine)    ── Geometry + Style → Draw Command Lists → GPU/Canvas backend
      │
      ▼
 ⑨ Mirror Loom (Live Preview) ── incremental re-run of ⑤–⑧ scoped to Shuttle-diffed Strands
```

Two pipelines coexist at runtime:

- **Cold pipeline** (①→⑨): full source → full Fabric. Runs once per file load / Hot Restart.
- **Hot pipeline** (Shuttle-scoped): a source edit re-runs only ②→⑨ for the smallest possible
  AST subtree, diffs it against the previous Fabric, and paints only the Strands that changed.
  This is the core of instant, non-reloading Live Preview.

---

## 2. Folder Structure

```
rinlang-main/
├── app/src/main/cpp/                     # existing native core (unchanged responsibilities)
│   ├── rin_lexer.{h,cpp}
│   ├── rin_parser.{h,cpp}
│   ├── rin_ast.h                         # + new ViewStmt / view-attribute nodes (§3)
│   ├── rin_interpreter.{h,cpp}
│   │
│   └── loom/                             # NEW: the entire rendering stack lives here
│       ├── core/
│       │   ├── rin_strand.h              # Strand base type + StrandKind enum
│       │   ├── rin_fabric.h              # Fabric tree, StrandId, versioning
│       │   ├── rin_fabric_builder.h      # Renderer Engine: AST -> Fabric
│       │   └── rin_semantic.h            # attribute-schema validation per Strand kind
│       │
│       ├── layout/
│       │   ├── rin_loom.h                # Loom layout engine (Propose/Settle solver)
│       │   ├── rin_constraint.h          # RinConstraints value type
│       │   └── rin_geometry.h            # Rect/Offset/Size, Geometry map
│       │
│       ├── paint/
│       │   ├── rin_dye.h                 # Dye paint engine
│       │   ├── rin_draw_command.h        # DrawCommand list types
│       │   └── rin_backend_gpu.h         # Vulkan/Skia/Canvas backend adapters
│       │
│       ├── motion/
│       │   ├── rin_weft.h                # animation/timeline engine
│       │   └── rin_curve.h               # easing curve library
│       │
│       ├── state/
│       │   ├── rin_warp.h                # reactive state cells + dependency graph
│       │   └── rin_binding.h             # Strand <-> Warp cell bindings
│       │
│       ├── diff/
│       │   ├── rin_shuttle.h             # Shuttle diff/reconciliation algorithm
│       │   └── rin_dirty_set.h           # dirty-Strand tracking, incremental scheduling
│       │
│       ├── input/
│       │   ├── rin_needle.h              # hit-testing + gesture/event dispatch
│       │   └── rin_gesture_arena.h       # gesture disambiguation (tap vs drag vs long-press)
│       │
│       ├── resources/
│       │   ├── rin_bobbin.h              # asset loading/cache (images, fonts, video, svg)
│       │   └── rin_pattern_book.h        # Theme Engine: tokens, palettes, style:// resolver
│       │
│       ├── plugins/
│       │   ├── rin_bolt_api.h            # Plugin API surface (custom Strand kinds)
│       │   └── rin_bolt_registry.h
│       │
│       ├── inspect/
│       │   └── rin_loupe.h               # Inspector Mode data model
│       │
│       └── perf/
│           └── rin_tension.h             # Performance Optimizer (batching, culling, caches)
│
├── preview-host/                         # NEW: Mirror Loom — the Live Preview process
│   ├── protocol/
│   │   └── mirror_protocol.md            # JSON-over-socket protocol spec (§10)
│   ├── server/                           # runs the cold+hot pipeline, owns the Fabric
│   │   └── mirror_server.cpp
│   └── client/                           # renders Fabric snapshots to screen
│       ├── android/  (Kotlin, Jetpack Canvas surface — reuses RinEngine.kt bridge)
│       └── desktop/  (Skia/GL window, for the VS-family IDE extension)
│
└── src/ (existing VSIX)                  # IDE integration point (§14 Live Preview panel)
    └── Preview/
        ├── RinPreviewToolWindow.cs
        └── RinPreviewViewModel.cs
```

---

## 3. RIN UI Grammar & AST (extends `rin_ast.h`)

RIN's existing container grammar already reads as:

```rin
@container.object=card
  text title = "Hello";
  style value="style://dark";
.end/container
```

Loomtime introduces a sibling family, `@view.<Kind>=name`, so the language stays self-consistent —
views are simply a `ContainerKind`-like concept with layout/paint semantics instead of data semantics:

```rin
@view.Column=root
  gap = 12;
  padding = 16;

  @view.Text=title
    text = "Welcome to Rin";
    style = "style://title";
  .end/view

  @view.Button=cta
    label = "Continue";
    onTap = go_next;
    style = "style://primary.button";
  .end/view

  @view.Card=preview
    elevation = 4;
    @view.Image=thumb src="poster.png" fit="cover";
    .end/view
  .end/view
.end/view
```

### 3.1 New AST nodes (additive — does not alter existing `ContainerStmt`)

```cpp
// rin_ast.h  — appended alongside ContainerStmt et al.

enum class StrandKind {
    TEXT, IMAGE, BUTTON, CARD, LIST, GRID,
    COLUMN, ROW, STACK,                    // structural layout Strands
    NAV_HOST, TOP_BAR, BOTTOM_BAR, DRAWER, // navigation shell Strands
    DIALOG, VIDEO, CANVAS, SVG,
    ANIMATED, TRANSITION,                  // Weft-driving wrapper Strands
    EFFECT_BLUR, EFFECT_SHADOW, EFFECT_GRADIENT,
    CUSTOM                                 // resolved via Bolt (plugin) registry
};

// key = expr;  attribute inside a @view block (mirrors ObjectStyleFieldKind's free-field idea)
struct ViewAttr {
    std::string key;
    ExprPtr value;       // literal, Warp binding (VariableExpr), or CallExpr (e.g. onTap=go_next)
    int line = 0;
};

// @view.<Kind>=name  <attrs><children>  .end/view
struct ViewStmt : Stmt {
    std::string name;             // may be empty (anonymous Strand)
    StrandKind kind = StrandKind::CUSTOM;
    std::string customTag;        // set when kind == CUSTOM, resolved via Bolt registry
    std::vector<ViewAttr> attrs;
    std::vector<StmtPtr> children; // nested ViewStmt / control-flow (if/for over Warp lists)
};

// warp name = expr;   -> declares a reactive state cell (see rin_warp.h)
struct WarpStmt : Stmt {
    std::string name;
    ExprPtr initializer;
};

// weft name { from: a, to: b, over: 300, curve: "ease.out" }  -> declares a Weft timeline
struct WeftStmt : Stmt {
    std::string name;
    std::vector<ViewAttr> params;   // from/to/over/curve/repeat/onEnd
};
```

`StyleStmt` (already in the language: `style value="style://dark";`) is reused unchanged inside
`@view` blocks — Loomtime's Pattern Book resolves the same `style://` URI scheme the table/object
containers already use, so theming syntax is identical everywhere in RIN.

### 3.2 Semantic Analyzer additions

- **Attribute schema check**: each `StrandKind` owns a static attribute table (required/optional,
  expected `Value` type). `@view.Button` requires `label` OR a child `Text` Strand; rejects unknown
  keys with a `RinError` carrying line info (existing `RinError{message,line}` type reused).
- **Warp resolution**: any `ViewAttr.value` that is a bare `VariableExpr` is resolved against the
  nearest enclosing `Warp` scope chain (mirrors RIN's existing `link/tying/merge` container-binding
  resolution, generalized to state).
- **Bolt resolution**: `StrandKind::CUSTOM` tags are looked up in the `BoltRegistry`; unresolved tags
  become a recoverable `Snag` (§9), not a hard compile failure — this is what lets Live Preview keep
  rendering everything *except* the broken Strand.

---

## 4. The Fabric — Virtual Scene Graph

The Fabric is a **persistent, versioned, immutable-snapshot tree**. Every edit produces a new Fabric
*version* that structurally shares unchanged Strands with the previous version (à la persistent data
structures) — this is what makes Shuttle diffing O(changed nodes) instead of O(tree size).

```cpp
// rin_strand.h
struct StrandId { uint64_t value; };   // stable across versions if node identity is preserved

struct Strand {
    StrandId id;
    StrandKind kind;
    std::string debugName;             // from ViewStmt.name, for Loupe/Inspector
    std::vector<ViewAttr> attrs;       // resolved (Warp bindings already substituted with values)
    std::vector<std::shared_ptr<Strand>> children;

    // filled in by later pipeline stages, cached on the Strand itself:
    RinConstraints lastConstraints;    // for Loom memoization
    Geometry geometry;                 // for Dye / Needle hit-testing
    uint64_t contentHash = 0;          // for Shuttle fast-reject
    uint32_t fabricVersion = 0;        // which Fabric build produced this Strand
};

using FabricRoot = std::shared_ptr<Strand>;
```

**Identity rule** (critical for correct diffing and animation continuity): a Strand's `StrandId` is
derived from `(parent path, ViewStmt.name if present, else positional index, kind)`. Naming a Strand
(`@view.Card=preview`) pins its identity across edits/reorders — exactly like a `key` concept, but
expressed through RIN's existing naming convention rather than a foreign prop.

### 4.1 Renderer Engine — AST → Fabric

```
buildFabric(ViewStmt node, ParentContext ctx) -> Strand:
    id      = deriveId(ctx.path, node.name, node.kind)
    attrs   = resolveAttrs(node.attrs, ctx.warpScope)      # evaluate exprs, bind Warp cells
    strand  = Strand{id, node.kind, node.name, attrs}
    for each child in node.children:
        strand.children.push(buildFabric(child, ctx.descend(strand)))
    strand.contentHash = hash(strand.kind, strand.attrs, childHashes)
    return strand
```

This function is pure given `(AST subtree, Warp snapshot)` — which is exactly what allows it to be
re-invoked on just the edited subtree during Live Preview.

---

## 5. The Loom — Layout Engine

### 5.1 Model: Propose/Settle (an original two-pass model)

Unlike a single top-down constraint pass, the Loom runs layout in two named passes per Strand,
matching how a physical loom actually behaves — tension is *proposed* down the warp threads, then
*settled* once the shuttle has passed:

1. **Propose pass (top-down):** a parent hands each child a `RinConstraints{minW,maxW,minH,maxH}`
   plus a **Weave Mode** — `TIGHT` (parent dictates exact size, e.g. a `Stack` filling its bounds),
   `LOOSE` (child may be smaller, e.g. `Column` children), or `WRAP` (child sizes to its own content,
   e.g. `Text`).
2. **Settle pass (bottom-up):** each child returns its chosen `Size` (never larger than the given
   constraints); the parent then positions children according to its own Strand-kind rule (`Column`
   stacks vertically with `gap`, `Row` horizontally, `Stack` overlays with alignment, `Grid` uses a
   track solver, `List` virtualizes — see §5.3) and computes **its own** size bottom-up.

```cpp
// rin_loom.h
Size layout(Strand& s, RinConstraints c, WeaveMode mode) {
    if (s.lastConstraints == c && !s.dirty) return s.geometry.size;  // Tension cache hit (§13)

    switch (s.kind) {
      case StrandKind::TEXT:   return measureText(s, c);             // WRAP leaf
      case StrandKind::IMAGE:  return measureImage(s, c);            // WRAP/aspect leaf
      case StrandKind::COLUMN: return layoutLinear(s, c, Axis::Y);
      case StrandKind::ROW:    return layoutLinear(s, c, Axis::X);
      case StrandKind::STACK:  return layoutStack(s, c);
      case StrandKind::GRID:   return layoutGrid(s, c);
      case StrandKind::LIST:   return layoutVirtualList(s, c);       // §5.3
      default:                 return layoutDelegateToBolt(s, c);    // plugin Strands
    }
}
```

### 5.2 Linear layout algorithm (Column/Row) — the core case

```
layoutLinear(strand, constraints, axis):
    gap      = strand.attr("gap", default=0)
    flexible = children where attr("grow") is set
    fixed    = children where attr("grow") is not set

    # pass 1: measure fixed children with LOOSE constraints along axis
    usedMain = 0
    for child in fixed:
        size = layout(child, constraints.loosenMain(axis), LOOSE)
        usedMain += size.along(axis) + gap

    # pass 2: distribute remaining main-axis space across flexible children by `grow` weight
    remaining = constraints.maxAlong(axis) - usedMain
    totalGrow = sum(child.attr("grow") for child in flexible)
    for child in flexible:
        share = remaining * (child.attr("grow") / totalGrow)
        size  = layout(child, constraints.tightAlong(axis, share), TIGHT)
        usedMain += size.along(axis) + gap

    # pass 3: settle — position children along main axis, align along cross axis
    cursor = 0
    for child in strand.children:
        child.geometry.offset = axisOffset(cursor, strand.crossAlign(child))
        cursor += child.geometry.size.along(axis) + gap

    return Size(main = usedMain, cross = maxCrossOfChildren)
```

This single algorithm — parameterized by `axis` — implements both `Column` and `Row`, matching how a
loom's warp/weft are the same mechanism rotated 90°.

### 5.3 List/Grid — the Virtual Weave

`List`/`Grid` Strands do **not** materialize off-screen children into the Fabric at all. They hold a
**Weave Window**: `[firstVisibleIndex, lastVisibleIndex]` plus an item-template `ViewStmt` and a
**recycle pool** of already-built Strand instances keyed by template identity (not by index) — so
scrolling reuses Strands rather than rebuilding them, and only calls `buildFabric` for the delta of
indices entering the window. This is Loomtime's answer to "don't render offscreen"; it is driven
directly by the Shuttle (§7) treating window-slide as a same-kind reorder diff.

---

## 6. The Dye — Paint Engine

Dye consumes the Loom's `Geometry` map and each Strand's resolved style (via Pattern Book) and
produces an ordered, backend-agnostic **Draw Command List** — never touches a GPU/canvas API
directly, so the same Dye output can be consumed by the Android Canvas backend, a desktop Skia
backend, or a headless PNG exporter used by the IDE for thumbnail previews.

```cpp
// rin_draw_command.h
enum class DrawOp {
    FILL_RECT, ROUND_RECT, TEXT_RUN, IMAGE, PATH, SVG_PATH,
    PUSH_CLIP, POP_CLIP, PUSH_TRANSFORM, POP_TRANSFORM,
    BLUR_LAYER, SHADOW, GRADIENT_FILL, VIDEO_FRAME
};
struct DrawCommand { DrawOp op; Rect bounds; StyleSnapshot style; /* op-specific payload */ };
using DrawList = std::vector<DrawCommand>;
```

### 6.1 Paint algorithm — layered, not immediate

```
paint(strand) -> DrawList:
    list = []
    if strand.needsLayer():                 # blur/shadow/opacity < 1/gradient mask
        list.push(PUSH_TRANSFORM/CLIP as needed)

    list += emitBackground(strand)           # fill / gradient / shadow (drawn under content)
    list += emitStrandContent(strand)        # TEXT_RUN / IMAGE / SVG_PATH / VIDEO_FRAME / children
    for child in strand.children (z-order per Stack.align or document order):
        list += paint(child)                 # recursively appended, in a single flat command list

    if strand.needsLayer():
        list.push(POP_*)
    return list
```

Flattening to a single command list (rather than a nested draw-call tree) lets Dye's backend do a
single linear pass with a **command-batcher** (§13) that merges adjacent `FILL_RECT`/`TEXT_RUN`
commands sharing a clip/transform into fewer GPU state changes.

### 6.2 Effects

- **Blur**: `BLUR_LAYER` renders the strand's subtree to an offscreen layer once, then a two-pass
  separable Gaussian kernel is applied by the backend; the layer is cached and only re-rendered if
  the subtree's `contentHash` changes.
- **Shadow**: computed analytically for rect/round-rect Strands (no offscreen pass needed) via an
  inset+blurred alpha mask; falls back to a layer for arbitrary `Canvas`/`SVG` shapes.
- **Gradient**: `GRADIENT_FILL` carries a stop list resolved from the Pattern Book (`style://` may
  reference a named gradient token, e.g. `"style://gradient.sunset"`).

---

## 7. The Shuttle — Diff Engine & Incremental Rendering

This is the heart of "instant Live Preview, no full reload."

### 7.1 What triggers it

Every keystroke in the editor produces a **source delta** (line-range edit). The parser re-parses
only the smallest enclosing `@view` block whose byte-range contains the edit (RIN's `.end/view`
terminator makes block boundaries trivial to re-scan without re-lexing the whole file). This yields
a **new AST subtree** rooted at the same block; everything else in the AST is reused by reference.

### 7.2 The diff algorithm (Shuttle Pass)

A keyed, single-pass tree diff — O(n) in the size of the changed subtree, not the whole Fabric:

```
shuttlePass(oldStrand, newAstNode) -> Patch[]:
    if oldStrand is null:
        return [Insert(buildFabric(newAstNode))]
    if newAstNode is null:
        return [Remove(oldStrand.id)]

    if oldStrand.kind != newAstNode.kind:
        return [Replace(oldStrand.id, buildFabric(newAstNode))]   # kind change = new identity

    resolvedAttrs = resolveAttrs(newAstNode.attrs)
    attrPatch     = diffAttrs(oldStrand.attrs, resolvedAttrs)      # field-level, not whole-node

    # --- keyed child reconciliation ---
    oldByKey = index(oldStrand.children, key = child.debugName or positional-index)
    newByKey = index(newAstNode.children, key = child.name or positional-index)

    patches = []
    if attrPatch is not empty:
        patches.push(UpdateAttrs(oldStrand.id, attrPatch))

    for key in unionOrderPreserving(oldByKey.keys, newByKey.keys):
        patches += shuttlePass(oldByKey[key], newByKey[key])       # recurse

    if childOrderChanged(oldByKey, newByKey):
        patches.push(Reorder(oldStrand.id, newOrderOfKeys))

    return patches
```

A `Patch` is one of `{Insert, Remove, Replace, UpdateAttrs, Reorder}`. The full list of patches for
an edit is typically 1–3 entries for a single-attribute edit (e.g. typing inside a string literal),
which is what gives Loomtime its "render only changed nodes" property.

### 7.3 Applying patches — Incremental Rendering

```
applyPatches(fabric, patches):
    for p in patches:
        mutate fabric in place at p.targetId               # O(1) per patch, tree-shared elsewhere
        markDirty(p.targetId)                                # feeds Loom's Tension cache invalidation
    dirtyRoots = topmostDirtyAncestors(patches)              # collapse to minimal re-layout roots
    for root in dirtyRoots:
        layout(root, root.lastConstraints, root.lastMode)     # re-run Loom only here
        paint(root)                                           # re-run Dye only here
    compose(fabric)                                            # single flat DrawList reused elsewhere
```

Only `dirtyRoots` re-enter Loom/Dye; every other Strand's cached `Geometry`/`DrawList` fragment is
reused verbatim. For a typical "change this button's label" edit, this is a single Strand's text
measurement + repaint — sub-millisecond, no scene teardown.

### 7.4 Diff class summary (UML, textual)

```
┌────────────────────┐        ┌───────────────────┐
│      Shuttle        │──uses──▶│      Patch         │
├────────────────────┤        ├───────────────────┤
│ + diff(old,new)      │        │ + kind: PatchKind   │
│ + applyPatches(...)  │        │ + targetId          │
└─────────┬──────────┘        │ + payload           │
          │ produces           └───────────────────┘
          ▼
┌────────────────────┐
│     DirtySet         │  (rin_dirty_set.h)
├────────────────────┤
│ + mark(StrandId)      │
│ + minimalRoots()       │──▶ feeds Loom + Dye re-entry points
│ + clear()              │
└────────────────────┘
```

---

## 8. Warp — State Management & Binding

`warp` cells are reactive values with automatic dependency tracking, resolved by the Semantic
Analyzer and re-evaluated by the Shuttle whenever they change (independent of source edits — this is
also what powers **runtime** UI updates, not just editor-time ones):

```rin
warp count = 0;

@view.Column=root
  @view.Text=label text = "Count: " + count;
  .end/view
  @view.Button=inc label="+1" onTap = { count = count + 1; };
  .end/view
.end/view
```

```cpp
// rin_warp.h
struct WarpCell {
    Value value;
    std::vector<StrandId> subscribers;   // Strands whose resolveAttrs() read this cell
};
struct WarpScope {
    std::unordered_map<std::string, WarpCell> cells;
    void set(const std::string& name, Value v) {
        cells[name].value = v;
        for (auto id : cells[name].subscribers) DirtySet::mark(id);   // -> Shuttle re-diff, not full rebuild
    }
};
```

Subscription is gathered automatically: `resolveAttrs` records which `WarpCell`s were read while
evaluating a Strand's attribute expressions (a simple "reader set" captured during evaluation),
exactly the same mechanism used for hot-reload of edited source — **both editor edits and runtime
state changes flow through the identical Shuttle → DirtySet → incremental Loom/Dye path.** This
unification (one incremental pipeline for both authoring-time and run-time updates) is the
architecture's central original idea.

---

## 9. Weft — Animation Engine

A `weft` is a named, resolvable timeline value, not an imperative "animate this widget" call —
mirroring `warp` as a first-class binding rather than a side effect:

```rin
weft fade { from: 0.0, to: 1.0, over: 300, curve: "ease.out" }

@view.Card=preview
  opacity = fade;
  @view.Transition=intro kind="slide.up" drivenBy=fade;
  .end/view
.end/view
```

- The **Weft Scheduler** runs on the render thread's vsync tick (§11), advancing every active `Weft`
  by `dt`, evaluating its `curve`, and calling `WarpScope::set`-equivalent updates on any Strand
  attribute bound to it — which routes straight back through the Shuttle/DirtySet path from §7/§8,
  so animation frames are just very-high-frequency incremental diffs, not a separate code path.
- `StrandKind::ANIMATED`/`TRANSITION` Strands additionally get **geometry-interpolation**: when the
  Shuttle detects a `Replace`/`Reorder` patch for a Strand wrapped in `Transition`, it captures the
  *old* geometry before removal and the *new* geometry after insertion, and Weft interpolates between
  them automatically (this is how reordering a List item animates instead of popping).
- Curve library (`rin_curve.h`): linear, ease.in/out/inOut, spring (damping+stiffness params),
  bounce, step. All are pure `f(t) -> t'` functions, backend-independent.

---

## 10. Needle — Event & Gesture Engine

```cpp
// rin_needle.h
struct HitTestEntry { StrandId id; Rect boundsInRoot; int zOrder; };

std::vector<HitTestEntry> hitTest(Fabric& fabric, Offset point) {
    // reverse-paint-order walk (topmost Strand first), clip-aware
}
```

- **Dispatch**: pointer-down runs `hitTest`, builds an ordered target list from leaf to root; a
  **Gesture Arena** (`rin_gesture_arena.h`) lets multiple recognizers (tap, long-press, drag, pinch)
  attached along that path *compete* — each recognizer claims/rejects based on movement thresholds
  and timers, exactly one wins per pointer stream (arena pattern, generalized rather than copied
  from any one framework's specific recognizer set).
- **Bubbling & capture**: RIN attribute handlers (`onTap=go_next`) are resolved as `CallExpr`
  references into the interpreter (`rin_interpreter`), so gesture callbacks are ordinary RIN
  functions — no separate event-object DSL to learn.
- **Focus & keyboard**: a separate `FocusChain` (linear, next/previous) walks the Fabric in document
  order, restricted to Strands with `focusable=true` (Button, TextField-like custom Bolts, etc.)

---

## 11. Thread Model

```
┌───────────────────────────┐   source edits    ┌───────────────────────────────┐
│      Editor / IDE Thread     │ ─────────────────▶ │        Analysis Thread          │
│  (keystrokes, VSIX classify)  │                     │  Lexer/Parser/Semantic re-run   │
└───────────────────────────┘                     └───────────────┬───────────────┘
                                                                       │ new/patched AST subtree
                                                                       ▼
┌───────────────────────────┐   patches (§7)     ┌───────────────────────────────┐
│      Render Thread            │ ◀───────────────── │      Shuttle/Warp Thread         │
│  Loom -> Dye -> DrawList        │                     │  diff, Warp updates, Weft tick   │
│  (owns the Fabric mutably)      │ ── DrawList ──────▶ │                                  │
└──────────────┬────────────┘                     └───────────────────────────────┘
               │ composed frame
               ▼
┌───────────────────────────┐
│     GPU/Canvas Thread          │  (platform surface: Android Canvas / Skia / desktop GL)
└───────────────────────────┘

┌───────────────────────────┐
│    Resource (Bobbin) Thread    │  async image/font/video decode, feeds Render Thread via cache
└───────────────────────────┘

┌───────────────────────────┐
│  Mirror Loom Transport Thread  │  serializes DrawList/Fabric diffs to the Preview client (socket)
└───────────────────────────┘
```

Rules:
1. The **Fabric is single-writer** (Render Thread) — the Shuttle/Warp thread produces immutable
   `Patch` lists that the Render Thread applies; no cross-thread node mutation.
2. **Weft ticks** are generated by the Render Thread's vsync callback and posted to the Shuttle/Warp
   thread as ordinary Warp-set events — animation is not a special thread, it's a frequent producer.
3. **Bobbin** decodes are always async; a Strand renders a placeholder (blur-hash or solid color from
   Pattern Book) until its resource promise resolves, at which point resolution posts a targeted
   `UpdateAttrs` patch — no re-diff of anything else.

---

## 12. Bobbin — Resource Manager

- **Content-addressed cache**: keys are `(sourcePath|url, decodeParams)`; two `Image` Strands
  referencing the same file at the same target size share one decoded bitmap.
- **Tiers**: Memory (LRU, size-budgeted) → Disk (decoded-bitmap cache for cold start) → Network/Asset
  fetch. Video/SVG get dedicated decoders but the same three-tier cache shape.
- **Preview-specific budget**: the Mirror Loom process runs with a smaller memory tier and aggressive
  downsampling (Live Preview never needs full-resolution decodes at editor zoom levels — see §15 Zoom).

## 13. Pattern Book — Theme Engine

Reuses RIN's existing `style="style://<token>"` URI convention verbatim:

```
style://dark                 -> a full theme (color roles, elevation scale, type scale)
style://title                 -> a named text style within the active theme
style://primary.button         -> a named component style
style://gradient.sunset        -> a named gradient token
```

```cpp
struct PatternBook {
    std::unordered_map<std::string, StyleToken> tokens;   // flat namespace, dotted paths
    StyleSnapshot resolve(const std::string& uri, const StyleSnapshot& inherited);
};
```

Resolution is **cascading**: an unresolved token falls back to the nearest ancestor Strand's resolved
style (inheritance), then to the active theme's defaults, then to engine hard defaults — so a Bolt
(plugin) Strand with no explicit style still renders coherently.

## 14. Tension — Performance Optimizer

| Technique | Where | Effect |
|---|---|---|
| **Constraint memoization** | Loom | skip re-measure if `(constraints, contentHash)` unchanged (§5.1) |
| **Command batching** | Dye | merge adjacent same-clip/transform draw ops before backend submit |
| **Layer caching** | Dye | blur/shadow/opacity layers cached by `contentHash`, not re-rasterized per frame |
| **Weave Window virtualization** | Loom (List/Grid) | never build/layout/paint offscreen Strands (§5.3) |
| **Dirty-root collapsing** | Shuttle | multiple patches under one ancestor collapse to a single re-layout root |
| **Structural sharing** | Fabric | unedited subtrees are the *same pointer* across Fabric versions — zero copy |
| **Bobbin downsampling** | resources | decode to display size, never full asset resolution, in Preview |
| **Off-thread decode** | resources | never blocks Render Thread |

---

## 15. Mirror Loom — Live Preview Engine

### 15.1 Feature mapping to the architecture

| Feature | Mechanism |
|---|---|
| Instant rendering while typing | Analysis Thread re-parses only the touched `@view` block (§7.1) |
| Render only changed nodes | Shuttle Patch list + Dirty-root collapsing (§7.3, §14) |
| No full reload | Fabric is versioned/persistent, never discarded (§4, §7.3) |
| Error highlighting | Snag markers carry `line` from `RinError`, rendered as squiggles in editor + inline badge in preview at the broken Strand's last-known geometry (§16) |
| Layout inspector | Loupe overlay reads `Strand.geometry` directly from the live Fabric (§17) |
| Element selection | Needle `hitTest` on the preview surface maps a tap back to `StrandId` → source `line`/`ViewStmt` |
| Drag & Drop editing | Loupe computes a target parent/index from drop point via `hitTest`; emits a **source-level AST edit** (reorders/reparents the `ViewStmt`), which itself flows back through the normal Shuttle pipeline as if the user had typed it |
| Responsive Preview | Loom is re-run with a different root `RinConstraints` per registered device profile — same Fabric, N geometry results |
| Multi-device preview | N parallel Loom+Dye passes over one shared Fabric (cheap: layout/paint are the only per-device cost, not parsing/diffing) |
| Zoom | Client-side transform on the composed frame; Bobbin serves the same downsampled decode across zoom levels (re-decodes only past a threshold) |
| Grid / Guidelines / Safe Area | Loupe-drawn overlay layer, composited *after* Dye's DrawList, never part of the Fabric itself |
| FPS / Memory / Render Time / Paint Time | Tension instrumentation counters exposed via the Mirror protocol (§15.3) |

### 15.2 Live Preview workflow

```
1. Developer types in the editor.
2. VSIX/editor host sends a source-delta over the Mirror transport.
3. Mirror Server (native process, owns the real Fabric):
     a. Analysis Thread re-parses touched @view block -> new AST subtree.
     b. Shuttle diffs old Strand vs new AST subtree -> Patch[].
     c. If parse/semantic error: emit a Snag (does NOT apply destructive patches;
        keeps last-good Strand alive so the preview never blanks).
     d. Render Thread applies Patch[], re-enters Loom/Dye only for dirty roots.
     e. Tension counters updated (frame time, layout time, paint time, memory).
4. Mirror Server sends a compact frame update to the client:
     - either a full DrawList (first frame / after Hot Restart), or
     - a DrawList *delta* keyed by StrandId (steady-state typing).
5. Preview client composites the delta onto its retained surface — visually "instant."
6. Loupe overlay (grid/guidelines/inspector highlights) is drawn client-side, independent
   of step 4, so toggling it never touches the Fabric or triggers a re-render of it.
```

### 15.3 Mirror protocol (transport-agnostic, JSON control + binary frame payloads)

```jsonc
// client -> server
{ "type": "sourceDelta", "file": "home.rin", "range": [120, 148], "text": "..." }
{ "type": "setDeviceProfiles", "profiles": ["pixel8", "iphone15", "desktop.1440"] }
{ "type": "select", "point": { "x": 212, "y": 88 }, "profile": "pixel8" }
{ "type": "dragDrop", "strandId": 44, "targetParent": 12, "index": 2 }
{ "type": "hotRestart" }

// server -> client
{ "type": "framePatch", "profile": "pixel8", "patches": [ { "op":"updateText","id":9,"text":"Count: 3" } ] }
{ "type": "snag", "line": 14, "message": "unknown attribute 'colr' on view.Card", "strandId": 30 }
{ "type": "selectionResolved", "strandId": 9, "sourceLine": 6, "geometry": {"x":16,"y":40,"w":300,"h":48} }
{ "type": "stats", "fps": 118, "memMB": 42.3, "layoutMs": 0.4, "paintMs": 0.6 }
```

### 15.4 Hot Reload vs Hot Restart

- **Hot Reload** (default, the whole point of §7): source edit → Shuttle patch → in-place Fabric
  mutation. **Warp state is preserved** because `WarpScope` lives independently of the AST and is
  only ever *read* by `resolveAttrs`, never rebuilt by it.
- **Hot Restart** (explicit action, or automatic fallback when a Snag is judged structural — e.g. a
  `warp` declaration itself was deleted): Mirror Server discards `WarpScope` and the Fabric, re-runs
  the full cold pipeline (①→⑨) from scratch. Still doesn't restart the *process* — the Bobbin cache
  and device-profile registry survive, so images don't re-decode.
- **Selection between the two** is automatic: the Shuttle classifies a Patch batch as *structural*
  (touches a `warp` declaration, a `@view` root's `StrandKind`, or a `Bolt` registration) vs
  *cosmetic* (attribute/text/style edits, child reordering). Structural ⇒ Hot Restart; cosmetic ⇒
  Hot Reload. The developer never has to choose manually, but the "Hot Restart" button remains
  available for a manual full reset.

---

## 16. Snag — Error Recovery

- Every stage (Lexer/Parser/Semantic/Renderer) can raise a `RinError{message, line}`. Loomtime wraps
  these as `Snag` records instead of aborting the pipeline:
  ```cpp
  struct Snag { std::string message; int line; std::optional<StrandId> strand; SnagSeverity sev; };
  ```
- **Containment radius**: a Snag inside one `@view` subtree only removes/freezes *that* subtree's
  contribution to the Fabric (rendered as a dashed "broken Strand" placeholder holding its last-known
  geometry) — siblings and ancestors keep rendering normally. This directly reuses the same
  "smallest enclosing block" re-parse boundary from §7.1.
- **Recovery**: once the offending edit is corrected, the next Shuttle pass naturally resolves the
  Snag and re-inserts a real Strand — no special "clear error" step needed.
- **Editor integration**: Snags stream to the VSIX classifier/tool window as squiggle diagnostics
  (`RinClassifier`/`RinClassificationFormats` already provide the token-classification plumbing this
  hooks into) plus the inline preview badge from §15.3.

---

## 17. Loupe — Inspector Mode

Reads directly from the live Fabric — never a separate shadow tree:

- **Strand tree view**: mirrors `Fabric` structure, labeled by `debugName`/`kind`; clicking a row
  highlights `Strand.geometry` in the preview and jumps the editor caret to the originating source
  line (tracked per-Strand since `buildFabric`, §4.1).
- **Geometry box**: shows Loom's `RinConstraints` in vs. `Size` out, plus padding/margin, for the
  selected Strand — a live view into §5's Propose/Settle values, not a recomputation.
- **Style panel**: shows the resolved `StyleSnapshot` (post Pattern-Book cascade) alongside the
  literal `style://` token, so authors see both what they wrote and what it resolved to.
- **Paint layer view**: lists the `DrawCommand`s attributed to the selected Strand, with per-command
  timing (from Tension instrumentation) — useful for spotting an unnecessary `BLUR_LAYER`.
- **Overlay toggles**: Grid / Guidelines / Safe-Area / bounding boxes — all client-side compositing
  over the last DrawList, costing nothing on the Fabric/Loom/Dye side (§15.1).

---

## 18. Bolt — Plugin API

Third parties (or later, first-party) extend RIN's UI vocabulary without touching the core pipeline:

```cpp
// rin_bolt_api.h
struct BoltStrandSpec {
    std::string tag;                                  // "Bolt.Lottie", "Bolt.MapView", ...
    AttrSchema schema;                                 // validated by Semantic Analyzer like built-ins
    Size (*measure)(const Strand&, RinConstraints);    // plugs into Loom's layoutDelegateToBolt
    DrawList (*paint)(const Strand&);                  // plugs into Dye
    void (*hitTest)(const Strand&, Offset, HitTestEntry&); // plugs into Needle
};
struct BoltRegistry {
    void registerStrand(BoltStrandSpec spec);
    const BoltStrandSpec* resolve(const std::string& tag) const;
};
```

- A missing/unregistered Bolt tag becomes a contained Snag (§16), not a hard failure — so installing
  a plugin later "heals" existing source that already referenced it.
- Bolts can also register **Pattern Book contributions** (new style tokens) and **Weft curves**,
  keeping the extension surface aligned with the engine's own subsystem boundaries rather than one
  monolithic "plugin" bag.
- Distribution reuses RIN's existing extension/package infrastructure already present in the repo
  (`ExtensionManager.kt`, `RinexPackager.kt`, `PackageRepository.kt`) — a Bolt ships as a `.rinex`
  package with a manifest entry declaring the `BoltStrandSpec` tags it provides.

---

## 19. Engine Feature Coverage

| Requested | StrandKind / mechanism |
|---|---|
| Text | `TEXT` — Loom WRAP leaf, Dye `TEXT_RUN` |
| Image | `IMAGE` — Bobbin-backed, Dye `IMAGE`, aspect-ratio measure |
| Button | `BUTTON` — composite Strand (background + label Text + Needle tap recognizer) |
| Card | `CARD` — `Stack`-like container + shadow/elevation via Dye effects |
| List | `LIST` — Weave Window virtualization (§5.3) |
| Grid | `GRID` — track-based Loom solver, same virtualization |
| Navigation | `NAV_HOST` — maintains a Warp-backed route stack; swaps active child subtree, driving Weft transitions on change |
| TopBar / BottomBar | `TOP_BAR`/`BOTTOM_BAR` — fixed-slot Row/Stack compositions with safe-area insets |
| Drawer | `DRAWER` — Stack overlay + Weft slide-in, dismiss via Needle scrim tap |
| Dialog | `DIALOG` — rendered in a top-level overlay Fabric layer above the main root Strand |
| Video | `VIDEO` — Bobbin decoder + `VIDEO_FRAME` Dye op, own presentation clock feeding Weft-independent frame pacing |
| Animation / Transition | `ANIMATED`/`TRANSITION` — Weft-driven (§9) |
| Effects (Blur/Shadow/Gradient) | Dye effect ops (§6.2) |
| SVG | `SVG` — parsed to a path list once (cached by content hash), Dye `SVG_PATH` |
| Canvas | `CANVAS` — exposes a raw `DrawList`-building callback to RIN code for imperative drawing, still diffed by content hash like any other Strand |
| Custom Components | `CUSTOM` via Bolt (§18) |

---

## 20. Future Roadmap

1. **Server-driven Fabric** — ship a serialized Fabric fragment (not RIN source) from a backend to
   enable "server-driven UI" for RIN apps, reusing the exact same Shuttle patch format as Live Preview.
2. **Multi-window Fabric** — one Warp scope, multiple independent Fabric roots (desktop multi-window,
   or split-screen), sharing Bobbin/Pattern Book.
3. **Loom GPU offload** — move the linear/grid solvers to a compute-shader batch pass for very deep
   Fabrics (an OS-shell-scale use case, per the prompt's ambition).
3. **Declarative Bolt DSL** — let plugin authors write `Bolt` specs in RIN itself (self-hosting the
   plugin surface), rather than requiring native code.
4. **Time-travel Loupe** — since the Fabric is versioned/persistent, keep the last N versions and let
   the Inspector scrub backward through recent Shuttle patches for debugging.
5. **Cross-device Mirror sessions** — one Mirror Server, many simultaneous device-profile clients
   (phone + tablet + desktop reviewers watching the same edit session live).
6. **Static Tension analysis** — a build-time linter that flags Strands whose `measure`/`paint`
   cannot be memoized (e.g. impure Bolt callbacks), surfaced as Loupe warnings before they cost frames.

---

## Appendix A — Where this lives relative to the existing repo (implemented, not hypothetical)

This section originally described a plan; it now describes what is actually in the repo, verified
by compiling and running each piece against the real files.

- `rin_common.h` / `rin_lexer.h` / `rin_lexer.cpp`: **unchanged**. `@`, `.`, `/`, `=`, `;` were
  already tokenized (needed for `.end/container.pipe` etc.), so `@view.<Kind>=name` and
  `.end/view` required zero lexer changes.
- `rin_ast.h`: purely additive — `ViewAttr`, `ViewStmt`, `WarpStmt` appended after `ImportStmt`.
  No existing struct was touched.
- `rin_parser.h` / `rin_parser.cpp`: additive — `viewDeclaration()` and `warpDeclaration()`, hooked
  into `declaration()` with the exact same lookahead pattern the parser already uses for
  `@import`/`route`/`row`/`style`/`document` (contextual, non-reserved keywords). One real bug was
  caught while integrating: `text` is already a reserved `TokenType::TEXT` keyword token in this
  language, so a naive `check(TokenType::IDENT)` attribute-key check silently rejected
  `text="...";` inside `@view.Text`. Fixed by matching on lookahead-for-`=` instead of token type.
- `app/src/main/cpp/loom/`: the engine itself — `rin_loom_eval.h` (attribute-expression evaluator
  over real `rin::Expr`), `rin_loom_strand.h` (Fabric + Renderer Engine), `rin_loom_layout.h`
  (Loom), `rin_loom_paint.h` (Dye + JSON serialization), `rin_loom_shuttle.h` (Shuttle, including a
  genuine Warp-driven runtime-update path that re-evaluates stored `rin::ExprPtr`s — not a stub),
  `rin_loom_pipeline.h` (`runColdPipeline`/`runHotPipeline`, the "LoomtimeRuntime" glue), and
  `rin_loom_c_api.{h,cpp}` (flat C ABI, matching `rin_c_api.h`'s existing conventions exactly).
  `loomc.cpp` is a standalone CLI (added as its own CMake executable target, same pattern as
  `rinc`) for testing against real `.rin` files without a full Android build.
- `jni_bridge.cpp` / `RinEngine.kt`: gained `renderView(source, rootWidth)` →
  `renderViewNative()` → `rin_loom_render_json()`, returning the Fabric as JSON for a
  Compose/Canvas layer to draw, or `{"error":..., "line":...}` on a Snag.
- `CMakeLists.txt` (app) and `bindings/CMakeLists.txt`: both updated to build `loom/rin_loom_c_api.cpp`
  into their respective shared libraries; `loomc` added as a second standalone executable target.
- `samples/loom_showcase.rin`: a real sample program in the new grammar, used as the integration
  test's input.

**Verified, not asserted:** the real `rin::Lexer`/`rin::Parser` were compiled together with the new
`loom/` engine and run against `samples/loom_showcase.rin` end-to-end (Lexer → Parser → Fabric →
Loom → Dye → PPM), and separately against a hot-edit + Warp-tap scenario confirming: (1) a
single-attribute source edit produces exactly one `UpdateAttrs` patch via the Shuttle, (2) a
runtime `warp.set(...)` (simulating a button tap) re-resolves only the Strands that actually read
that cell, using their stored expression — not a template placeholder — and (3) an unrelated
sibling subtree's geometry is provably untouched by either kind of update.
