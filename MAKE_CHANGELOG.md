# Make Unit upgrade

- Added real `@make.(name)` unit syntax.
- Added Make kinds and capability policies.
- Added `use`, `need`, `allow`, `deny`, `strict`.
- Added input/output/API metadata.
- Added version/description metadata.
- Added runtime policy validation before execution.
- Added capability detection for container, loop, function, condition, view, API, data, import and related constructs.
- Kept legacy `@make=name` compatibility.
- Added Make Unit editor support in all three editor surfaces:
  - VS Code grammar (`syntaxes/rin.tmLanguage.json`): highlights the policy directives
    (`kind`/`use`/`need`/`allow`/`deny`/`strict`/`input`/`output`/`public`/`private`/`version`/
    `description`), `kind`'s value list, and capability names after `use`/`need`/`allow`/`deny`.
  - Visual Studio classic extension (`src/Classification`): new `Rin.MakeDirective` and
    `Rin.Capability` classification types/formats, a real `@make.(name)` tokenizer rule, and a
    new `makeunit` snippet (`src/Snippets/Rin/make.unit.snippet`).
  - Android app editor (`app/.../RinSyntaxHighlighter.kt`): new `syntax_make_directive` color for
    policy directives, plus `loop`/`function`/`view`/`chatbot` capability keywords.
