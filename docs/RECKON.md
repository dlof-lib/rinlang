# Reckon

> See also: [`pipelines.md`](./pipelines.md) for `|>`, [`standard-library.md`](./standard-library.md)
> for the math/stat functions used as pipeline steps, [`control-flow.md`](./control-flow.md) for
> the `for`/`if` logic Reckon replaces, and [`MAKE_UNIT.md`](./MAKE_UNIT.md) for the `reckon`
> capability.

**Reckon** is Rin's professional, two-line way to compute a number out of a collection. It
replaces the classic "loop + accumulator + condition + division" pattern with one short,
readable statement — easy to learn, and it reads exactly like plain English:

> *"Reckon the average of scores, where each item is above zero, as the mean."*

## The problem it solves

Without Reckon, computing a filtered average takes a full block:

```rin
let total = 0;
let count = 0;
for (let i = 0; i < len(scores); i = i + 1) {
    if (scores[i] > 0) {
        total = total + scores[i];
        count = count + 1;
    }
}
let average = total / count;
```

7 lines, 2 helper variables, a manual index, and a manual division — for one number.

## The Reckon way — always exactly two lines

```rin
reckon average(scores)
    where item > 0 |> mean();
```

That is the whole feature. Line 1 names the result and its input collection. Line 2 is one
pipeline statement, ended by `;` — nothing to close, nothing to indent, nothing else to learn.

## Syntax

```
reckon <name>(<collection>);
    [where <condition>] |> <function>() [|> <function>() ...];
```

- `reckon` — declares a Reckon. Plain, simple English: "figure it out."
- `<name>(<collection>)` — the result name and the array/collection it reads from, exactly
  like a one-argument [function](./functions.md) header.
- `item` — inside the second line only, `item` is automatically bound to one element of
  `<collection>` at a time. No declaration needed.
- `where <condition>` — optional filter. Any [boolean expression](./control-flow.md#4-عوامل-المقارنة-والمنطق)
  using `item`. Elements that don't match are skipped, same as the `if` in the manual version.
- `|> <function>()` — the same [pipeline operator](./pipelines.md) already used everywhere
  else in Rin. Chain as many steps as you like: `|> normalize() |> mean() |> round()`.
- No closing tag. A Reckon is always exactly two lines — that is the rule, not just a style
  choice, which is what makes it fast to learn and impossible to get "unbalanced."

## More examples

Pass-rate variance, filtered to passing grades:

```rin
reckon passVariance(grades)
    where item >= 60 |> variance();
```

A straight pipeline with no filter at all — still two lines:

```rin
reckon normalizedScores(scores)
    |> normalize() |> scale();
```

Using the result like any other value:

```rin
reckon average(scores)
    where item > 0 |> mean();

show average;
if (average > 75) { show "great turnout"; }
```

## How it fits with the rest of Rin

- **[Pipelines](./pipelines.md)** — Reckon's second line *is* a pipeline; every existing
  [standard-library](./standard-library.md) or user-defined function usable after `|>` works
  here unchanged.
- **[Control flow](./control-flow.md)** — `where` is Reckon's condition and the implicit walk
  over `<collection>` is Reckon's loop; a Reckon is defined as sugar for exactly that `for`/`if`
  pattern, so anything you could filter/loop by hand, you can `reckon`.
- **[Functions](./functions.md)** — a Reckon is called and used exactly like a value produced by
  a `fun`; `reckon average(scores)` behaves like `let average = averageOf(scores);` would.
- **[Make Unit](./MAKE_UNIT.md)** — `reckon` is a first-class capability. A strict Make Unit
  that uses a Reckon must declare `use reckon;` (or `need reckon;` to require it), exactly like
  `loop`, `condition`, or `function`:

```rin
@make.(reportCard)
    kind data;
    use reckon;
    need reckon;
    strict;

    reckon average(scores)
        where item > 0 |> mean();
.end/make=reportCard
```

## See also

- [`pipelines.md`](./pipelines.md) — the `|>` operator Reckon builds on.
- [`standard-library.md`](./standard-library.md) — `mean`, `variance`, `stddev`, `sum`,
  `normalize`, `scale`, and the rest of the functions usable after `|>`.
- [`control-flow.md`](./control-flow.md) — the manual loop/condition pattern Reckon replaces.
- [`MAKE_UNIT.md`](./MAKE_UNIT.md) — declaring the `reckon` capability in a strict Make Unit.
- [`language-reference.md`](./language-reference.md) — the full map of Rin's concepts.
