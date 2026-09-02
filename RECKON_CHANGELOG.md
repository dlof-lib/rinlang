# Reckon — new computational concept

- Added `reckon <name>(<collection>)` — a computed value declared in exactly two lines,
  replacing the manual loop + accumulator + condition pattern. See `docs/RECKON.md`.
- Added the `where <condition>` filter clause and the implicit per-element binding `item`,
  both scoped to a Reckon's body line.
- A Reckon's body is a normal `|>` pipeline, so every existing and future pipeline-compatible
  function (`mean`, `variance`, `stddev`, `sum`, `normalize`, `scale`, user-defined `fun`s, ...)
  works inside it unchanged.
- Registered `reckon` as a new Make Unit capability (`docs/MAKE_UNIT.md`), usable with
  `use`/`need`/`allow`/`deny` like `loop`, `condition`, or `function`.
- Added `examples/reckon_demo.rin`.
- Added editor support in all three editor surfaces:
  - VS Code grammar (`syntaxes/rin.tmLanguage.json`): new `reckon-unit` scope highlighting
    `reckon`, the result name, `where`, and the implicit `item`.
  - Visual Studio classic extension (`src/Classification`): new `Rin.ReckonKeyword`,
    `Rin.ReckonName`, and `Rin.ReckonItem` classification types/formats, tokenizer rules, and a
    new `reckon` snippet (`src/Snippets/Rin/reckon.snippet`).
  - Android app editor (`RinSyntaxHighlighter.kt`): `reckon`/`where` colored as core keywords,
    new `syntax_reckon_item` color for the implicit `item` binding.

## Update: Reckon is now a real, executing feature (not just docs/highlighting)

Everything above this line described Reckon's *design* — docs, an example file, and editor
syntax highlighting only. The lexer/parser/interpreter (`app/src/main/cpp/rin_*.cpp`) and the
standalone native compiler (`compiler/rinc.cpp`) had no code path for `reckon` at all, so any
`.rin` program using it would fail to parse. This update makes it actually run:

- **Parser** (`rin_parser.h`/`.cpp`): `reckon` is now a contextual keyword, recognized the same
  way `route`/`row`/`document`/`warp` already are (never reserved, so it can't collide with a
  variable named `reckon`). `Parser::reckonDeclaration()` parses the full two-line form —
  header, optional `where <condition>`, and the `|>` chain — into a new `ReckonStmt` AST node
  (`rin_ast.h`).
- **Interpreter** (`rin_interpreter.cpp`): `Interpreter::execute(ReckonStmt)` evaluates the
  collection, filters it with `item` bound per-element in a scoped child environment when
  `where` is present, then runs the (optionally filtered) array through the pipeline stages via
  the same `invokeCallee()` dispatch ordinary pipelines use — so native functions, user `fun`s,
  and future pipeline-compatible functions all work identically inside a Reckon body. The final
  value is bound under the Reckon's name exactly like `let` would.
- **Make Unit capability** (`rin_make.cpp`): a `ReckonStmt` now actually tags the `reckon`
  capability during capability collection, and `reckon` was added to the default-allow list for
  every non-`app` Make kind (`component`, `page`, `library`/`module`, `service`, `task`, `data`,
  `plugin`) — previously `use reckon;`/`need reckon;` had no effect and a strict `data` Make Unit
  using Reckon would have been rejected as "uses forbidden capability: reckon".
- **Bugfix**: `scale(nums, factor)` required exactly 2 arguments, so the demo's own
  `|> normalize() |> scale()` (no factor) would have thrown at runtime. `factor` is now optional
  and defaults to `1.0`.
- **Bugfix**: `examples/reckon_demo.rin`'s strict Make Unit declared `use reckon;`/`need reckon;`
  but never declared `use io;` despite calling `show` inside — added.
- Verified by building the interpreter from source and running `examples/reckon_demo.rin` end to
  end, including inside the strict Make Unit.
- **Native compiler (`rinc`) is not yet updated.** `compiler/rinc.cpp` is a fully separate,
  self-contained lexer/parser/C-codegen stack (it doesn't reuse `rin_parser.cpp`), so Reckon and
  the new functions below are, for now, interpreter-only — the same status as several other
  advanced features (see `kInterpreterOnlyNatives` in `rinc.cpp`).

## Update: expanded arithmetic — 13 new pipeline functions

`docs/RECKON.md` gained a full reference table (aggregation vs. transformation) covering both
the pre-existing stat functions and the following 13 new natives, all registered in
`Interpreter::registerNatives()` (`rin_interpreter.cpp`) and usable after `|>` anywhere,
including inside a Reckon body:

- **Aggregation** (array -> one number): `product`, `count`, `range`, `geometricMean`,
  `harmonicMean`, `rms`, `percentile(p)`, `iqr`, `weightedMean(weights)`.
- **Transformation** (array -> array): `zscore`, `cumulativeSum`, `movingAverage(windowSize)`,
  `clamp(lo, hi)`.

All 13 are recognized by every editor surface's syntax highlighting (VS Code grammar, VS classic
tokenizer, Android highlighter); the Android highlighter's builtin list was also backfilled with
the pre-existing stat functions (`sum`, `mean`, `variance`, ...) that it was missing entirely.
`examples/reckon_demo.rin` and `docs/RECKON.md` were extended with worked examples using several
of the new functions (`count`, `iqr`, `weightedMean`, `movingAverage` + `clamp`).
