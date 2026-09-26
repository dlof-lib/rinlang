# Rin Loops

Rin now provides three compact loop forms in addition to the existing `while`, `for`, and `for...in` syntax.

## each

Use `each` when the goal is simply to visit every item:

```rin
each item in items {
    print item;
}
```

It supports arrays, maps (keys), and strings (characters). It uses the same `break` and `continue` behavior as `for...in`.

## repeat

Use `repeat` when the goal is a fixed number of executions:

```rin
repeat 5 {
    print "Hello";
}
```

The count is evaluated once and must be a finite non-negative integer.

```rin
repeat 10 {
    if (condition) { continue; }
    if (stop) { break; }
}
```

## loop

Use `loop` as a short condition-first loop:

```rin
loop (online and count < 10) {
    print count;
    count = count + 1;
}
```

`loop` has the same runtime semantics as `while`.

## Existing forms remain

C-style `for` and `for...in`, plus `while`, remain supported. The new forms are additive and are parsed contextually so ordinary identifiers can continue to be used in normal expressions.
