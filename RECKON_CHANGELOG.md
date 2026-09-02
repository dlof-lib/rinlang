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
