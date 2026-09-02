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

The editor provides insertion snippets for the new UI layer and syntax highlighting for the new keywords.
