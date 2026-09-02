# Rin Elements / Container / Loop

Rin UI is split into three strict layers:

- `@element.*`: ready-made functional element. It does not own visual styling.
- `@container`: behavior layer. It owns event bindings and application logic.
- `@loop`: canvas layer. It owns geometry, colors, sizes, layout and visual presentation.

## Example

```rin
@container=app
    @element.button=run
        text="Run";
    .end/element

    on.run.click=runCode();
.end/container

@loop=appCanvas
    width=390;
    height=700;
    background="#ffffff";
    element_width=320;
    element_height=48;
    element_color="#111111";
    element_text_size=16;

    @element.button=run
        text="Run";
    .end/element
.end/loop
```

`on.run.click=...` belongs to the container. `width`, `height`, `background`, etc. belong to the loop. The element only describes the semantic component and its functional data such as `text`, `source`, `placeholder`, `value`, and `max`.

## Ready elements

`text`, `button`, `input`, `search`, `image`, `video`, `audio`, `link`, `progress`, `checkbox`, `radio`, `switch`, `slider`, `select`, `list`, `row`, `column`, `box`, `card`, `sidebar`, `popup`, `modal`, `tabs`, `table`, `file`, `date`, `time`, `divider`, `code_editor`, `calculator`.

Every one of these is now backed end-to-end (parser tag → Fabric `StrandKind` → Loom measure/paint → JSON export), not just accepted syntax. A few are pure vocabulary aliases onto an existing kind rather than a new one — `sidebar` is a `Drawer`, `popup` is a `Dialog`, `range`/`dropdown`/`textfield` are accepted alongside `slider`/`select`/`input` — so `.rin` source can use whichever name reads best without losing any behavior.

Element attributes stay functional-only, per the split at the top of this doc — visual styling for all of the below still comes only from the enclosing `@loop`'s `element_*` defaults or an explicit `color=`/`tone=`/`width=`/`height=` override, same as `button`/`input` already work:

- `link` — `text=`. Renders as tinted text with no box (like a Button's `variant="link"`).
- `radio` — `label=`, `checked=`. Same box as `checkbox`, drawn as a ring with a filled center dot when checked, not a filled square.
- `slider` — `min=`, `max=`, `value=`. A full-width track with a filled portion and a thumb at the current value (same idea as `progress`, but draggable).
- `select` — `value=`, `placeholder=`. A bordered field box, same rules as `input`; the option list itself is application/behavior data, not layout.
- `search` — `value=`, `placeholder=`. Same field box as `input`, semantically distinct for a11y (`searchbox` role) and for a search-specific default icon in a themed renderer.
- `file` — `value=`, `placeholder=` (defaults read as "no file chosen" when left unset by the author).
- `date` / `time` — `value=`, `placeholder=`. Field boxes; the actual native picker UI is a host-app concern, same relationship `video`/`audio` already have to real playback.
- `code_editor` — `value=`. A taller field box (default height 220 vs. `input`'s 40) for a few lines of code; real syntax highlighting/editing is a host-app concern.
- `calculator` — no required attributes. Ships a sensible default box (280×360) when given no children; nest real children (e.g. a `box`/`row` keypad grid) to size it around your own content instead.
- `list` / a nested `list` item — `list` is a column of children; nest ordinary elements (or a future dedicated list-item kind) inside it for rows.

### Row direction (RTL/LTR)

`row` (and anything else that shares its layout, like `tabs`) accepts `direction="rtl"`:

```rin
@element.row=actions
    direction="rtl";

    @element.button=save
        text="حفظ";
    .end/element

    @element.button=cancel
        text="إلغاء";
    .end/element
.end/element
```

With `direction="rtl"`, `save` (first child in source order) is placed flush against the right edge and `cancel` follows to its left — matching reading order for Arabic UIs without reversing the children in source or touching `gap=`/alignment. `direction` defaults to `"ltr"` (today's existing behavior, unchanged) when omitted; `column` is unaffected either way — direction only ever changes horizontal (main-axis-X) flow.

The editor provides insertion snippets for the new UI layer (including the newly-backed elements above) and syntax highlighting for the new keywords.
