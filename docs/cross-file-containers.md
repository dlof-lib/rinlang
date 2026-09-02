# Calling a Container or Element From Another File (`use ... from`)

> This page is written in simple English on purpose. It introduces one new,
> easy keyword — `use` — that lets one `.rin` file call a container or a UI
> element that lives in a different `.rin` file, by name, with a single line.
>
> See also: [`containers.md`](./containers.md) for `@container`, and
> [`loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md`](./loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md)
> for `@view.<Kind>=name` UI elements. This page assumes you already know what
> those two things are.

## 1. The problem

You already have two separate pieces that both give something a **name**:

- `@container=Name ... .end/container` — a named container of data/logic.
- `@view.Kind=Name ... .end/view` — a named UI element (Button, Card, Column, ...).

And you already have a way to pull a whole *file* into another file:

```rin
@import "path/to/file.rin" as box;
```

But `@import ... as box;` only gives you a **namespace**. To actually reach
one named thing inside it, you still need a second step
(`link to=box;` / `tying with=box;`) that pulls in *everything* the file
defines, whether you wanted all of it or not.

`use ... from` skips both steps and does one very specific, very readable
thing: **bring in just the one container or element you asked for, by its
own name.**

## 2. The basic form

```rin
use Header from "components/header.rin";
```

Read it out loud: **use `Header` from `"components/header.rin"`.**

After this line, `Header` is usable in the current file exactly as if you
had typed it here yourself — as a data container, or dropped straight into
a `@view` tree as a UI element.

```rin
// components/header.rin
@view.Row=Header
    @view.Text=title text = "My App";
    @view.Button=menuBtn label = "Menu";
.end/view
```

```rin
// main.rin
use Header from "components/header.rin";

@view.Column=root
    Header;                       // the imported element, called by name
    @view.Text=body text = "Welcome!";
.end/view
```

## 3. Calling more than one name

Separate names with a comma. Each one is looked up independently in the
same file:

```rin
use Header, Footer from "components/shell.rin";
```

## 4. Renaming on the way in (`as`)

If the name you are importing clashes with something you already have,
give it a local name with `as`:

```rin
use Card from "components/card.rin" as ProductCard;

@view.Column=root
    ProductCard;
.end/view
```

## 5. Rules (keep these in mind)

| Rule | Why |
|---|---|
| The path is always a string, exactly like `@import`. | Same file-resolution rules as `@import` — no new lookup logic to learn. |
| The name after `use` must exist in the target file as an `@container=Name` or `@view.Kind=Name`. | If it does not exist, this is a normal, reported error (not a silent empty result) — the same `ImportError`/`ModuleNotFound` family already used by `@import`. |
| `use` only brings in the name(s) you list. | Nothing else from the target file leaks into your current file. That is the whole point versus `link`/`tying`. |
| `use` can appear anywhere `@import` can appear (top of file is the normal place). | Keeps file layout predictable and easy to scan. |
| Using the same name twice in one file (without `as`) is an error, just like re-declaring any other variable. | Prevents two different files silently overwriting the same name. |

## 6. How this maps onto what already exists

`use` is not a new engine — it is a short, readable spelling for two steps
you could already do by hand. This table shows the exact equivalence, the
same way [`containers.md`](./containers.md#صياغة-إنجليزية-مبسّطة-وسهلة-التعلّم-عائلة-make)
documents the `make`/`show`/`create`/`run` simple-English family:

| Simple form | Exactly equal to |
|---|---|
| `use X from "file.rin";` | `@import "file.rin" as __tmp;` then `link to=__tmp;` restricted to only `X` |
| `use X, Y from "file.rin";` | the same, restricted to only `X` and `Y` |
| `use X from "file.rin" as Y;` | the same, then the pulled-in name is called `Y` instead of `X` |

Because it is defined as a direct equivalence, `use` never behaves
differently from `@import` + `link`/`tying` — it is just the short way to
say "I only want this one named piece."

## 7. Full example

> A runnable copy of this exact example is at
> [`examples/use_from_demo/`](../examples/use_from_demo/) —
> `sidebar.rin` (defines) + `main.rin` (calls).

```rin
// components/sidebar.rin
@view.Column=Sidebar width = 220 bg = "#0F0F14"
    @view.Text=navHome text = "Home";
    @view.Text=navSettings text = "Settings";
.end/view

@container=SidebarState
    let open = true;
.end/container
```

```rin
// main.rin
use Sidebar, SidebarState from "components/sidebar.rin";

@view.Scaffold=page
    @view.Row=top role = "topbar"
        @view.Text=title text = "My App";
    .end/view

    Sidebar role = "sidebar" open = SidebarState.open;

    @view.Column=content role = "content"
        @view.Text=hello text = "Hello!";
    .end/view
.end/view
```

One file defines a piece once. Any other file can call it by name. That is
the whole concept.

## انظر أيضاً / See also

- [`containers.md`](./containers.md) — `@container`, `link`, `tying`, and the
  `make` simple-English family this page follows the same style as.
- [`loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md`](./loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md) —
  `@view.<Kind>=name` UI elements.
- [`syntax.md`](./syntax.md) — the shared syntax rules (`;`, `{}`, comments)
  that `use ... from` follows too.
- [`language-reference.md`](./language-reference.md) — the full concept map.
