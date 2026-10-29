# indsin — language support for VS Code

Syntax highlighting, snippets, code folding and a dedicated file icon for **indsin** (`.indsin`) UI documents of the [Rin language](../../README.md).

## What is highlighted

| Construct | Example | Scope |
|---|---|---|
| Structure tags | `@container=App`, `@view`, `.end/view` | `keyword.control.*.indsin` |
| Element kinds (all 65 + aliases) | `Column`, `Button`, `DonutChart` | `support.class.element.indsin` |
| Custom elements | `@view.Chart3D=x` | `entity.name.type.custom-element.indsin` |
| Element / container names | `=Root` | `entity.name.tag.view-name.indsin` |
| Attributes | `padding`, `onTap`, `variant` | `variable.parameter.attribute.indsin` |
| Responsive suffixes | `padding_md`, `_sm` `_md` `_lg` `_xl` | `storage.modifier.responsive.indsin` |
| Design tokens | `"spacious"`, `"gradient"`, `"slideUp"` | `constant.language.token.indsin` |
| Colours / lengths | `"#1E1E2E"`, `120px`, `250ms` | `constant.other.color.*`, `constant.numeric.length.indsin` |
| Tag syntax | `<Column#root gap=16>`, `</Column>`, `<Text />`, `{count + 1}`, `<!-- -->` | `punctuation.definition.tag.indsin`, `support.class.element.indsin`, `meta.embedded.expression.indsin` |
| Rin code (handlers, `warp`, strings, comments) | `count = count + 1;` | inherited from `source.rin` |

The grammar is generated from the translator's own catalogue (`node tools/indsin-object/scripts/gen_syntax.js`), so it always matches the elements the interpreter supports.

## Install from source

```sh
cd editors/vscode-indsin
npx @vscode/vsce package      # produces indsin-language-1.0.0.vsix
code --install-extension indsin-language-1.0.0.vsix
```

or copy this folder to `~/.vscode/extensions/rinlang.indsin-language-1.0.0`.

Open `examples/sample.indsin` (classic `@view` syntax) or `examples/sample-tags.indsin` (tag syntax, see [docs/indsin_tags.md](../../docs/indsin_tags.md)) to try it.

## Live Preview

**Indsin: Open Live Preview to the Side** (`Ctrl/Cmd+K V`, or the editor-title button) starts `rin_indsin_live` for the active file and shows the real engine's
output beside the editor, updating on every keystroke (unsaved buffer included). Also: *Open in Browser*, *Share on Local Network* (phone/tablet), *Stop*.
Build the server once with `tools/indsin-live/build.sh` and set `indsin.livePreview.serverPath` (or leave it in `dist/live/`). Details: [docs/indsin_live_preview.md](../../docs/indsin_live_preview.md).
