# max / min family

`max` and `min` used to take exactly two numbers. They now accept more, and the family has helpers for picking by key or position.

```kotlin
max(3, 9)                 // 9      (old form, unchanged)
max(1, 8, 3, 5)           // 8      any number of arguments
min([2, 9, 4])            // 2      a single array
max("pear", "zoo")        // "zoo"  strings compare by code point
max([])                   // nil    empty array
max(1, "a")               // error: cannot compare a number with a string
```

| Function | Returns |
|---|---|
| `max(a, b, ...)` / `max(arr)` | largest value (numbers or strings; nil if the array is empty) |
| `min(a, b, ...)` / `min(arr)` | smallest value |
| `maxBy(arr, fn)` / `minBy(arr, fn)` | the element whose `fn(item)` key is largest/smallest; first one on ties; nil if empty |
| `argmax(arr)` / `argmin(arr)` | index of the largest/smallest element (first on ties); nil if empty |
| `minmax(arr)` | `[min, max]` in one call; nil if empty |

```kotlin
let oldest = maxBy(people, fun(p) { return p["age"]; });
let i = argmin(prices);
let [lo, hi] = minmax(readings);
```

Mixed types are an error (no silent coercion). For loops, `max each x in xs yield ...` still exists (see loops.md).
