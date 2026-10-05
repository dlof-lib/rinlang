# Rin KEY_TERMS and Built-in Conditions

## Key conditions

Rin supports compact key-condition forms. Each one is pure syntax sugar over the normal `IfStmt` / `WhileStmt` runtime, so they behave exactly like their long forms (including `break`/`continue`, `#done`, scoping and short-circuiting).

```rin
=if=(score >= 80) {
    print "passed";
}

=unless=(empty(name)) {
    print name;
}

=when=(typeIs(value, "number")) {
    print "number";
}
```

### Forms

| Form | Meaning |
| --- | --- |
| `=if=(c)` / `=when=(c)` | `if (c)` |
| `=unless=(c)` / `=ifnot=(c)` | `if (!c)` |
| `=elif=(c)` / `=elseif=(c)` | continues a chain: `else if (c)` |
| `=else=` | alternative spelling of `else` |
| `=while=(c)` | `while (c)` |
| `=until=(c)` | `while (!c)` |
| `=all=(a, b, ...)` | `a and b and ...` (short-circuit) |
| `=any=(a, b, ...)` | `a or b or ...` (short-circuit) |
| `=none=(a, b, ...)` | `!(a or b or ...)` |
| `=nil=(x)` / `=notnil=(x)` | `x == nil` / `x != nil` |
| `=empty=(x)` / `=present=(x)` | `empty(x)` / `present(x)` |

`key_conditions()` returns this table at runtime (form name -> canonical meaning).

### Chains

Any branching form accepts an optional tail: a plain `else`, `=else=`, or any number of `=elif=` links.

```rin
=if=(score > 95) { print "A+"; }
=elif=(score > 80) { print "B"; }
=elif=(score > 60) { print "C"; }
=else= { print "F"; }
```

`=elif=` / `=elseif=` are only valid as a continuation; starting a statement with one is a parse error that says so.

### Loops

```rin
let i = 0;
=while=(i < 3) { i = i + 1; }
=until=(i == 0) { i = i - 1; }
```

### Several conditions at once

```rin
=all=(age >= 18, has(roles, "admin"), present(token)) { grant(); }
=any=(isNil(a), isNil(b)) { print "something is missing"; }
=none=(failed, timedOut) { print "clean run"; }
```

## Using conditions inside other concepts

The same condition vocabulary works inside the constructs that need to decide something.

### Trailing guards: `return` / `break` / `continue` / `achieve` / `throw`

Append `when (c)`, `unless (c)` or `if (c)` before the `;`. Sugar for wrapping the statement in an `if`.

```rin
fun label(n) {
    return "neg"  when (n < 0);
    return "zero" if (n == 0);
    return "pos";
}

while (true) {
    n = n + 1;
    continue when (isOdd(n));
    break    when (n >= 6);
}

throw "bad input" unless (present(name));
let r = goal { achieve found when (isTrue(ok)); };
```

`return;` with a guard is written `return if (c);` (after `return`, `when(...)` / `unless(...)` still means a normal call unless a value precedes it).

### `match` guards

```rin
match (level) {
    case Level.High when (vip)     { print "high-vip"; }
    case Level.High                { print "high"; }
    case Level.Low, Level.Mid unless (vip) { print "plain"; }
    else                           { print "other"; }
}
```

A guarded `case` only runs when its value matches **and** the guard is truthy; otherwise matching continues with the next `case`.

### Subject-less `match`: a readable if / else-if chain

```rin
match {
    case (score >= 90)            { print "A"; }
    case (between(score, 80, 89)) { print "B"; }
    case (oneOf(score, 70, 71))   { print "C"; }
    else                          { print "F"; }
}
```

The first `case` whose condition is truthy runs. `else` must be last.

### Already works

Every helper above is an ordinary function, so it can be used anywhere an expression is accepted: `print ... if=`, `reckon ... where`, `for (...) when (c) {...}`, ternaries, `plus.condition`, container queries and template strings.

## KEY_TERMS registry

`KEY_TERMS` is runtime vocabulary metadata. It does not change the global lexer keyword table.

```rin
.KEY_TERMS = {
    "when": "if",
    "unless": "if",
    "otherwise": "else"
};

print key_terms();

.KEY_TERMS == {
    "when": "if",
    "unless": "if",
    "otherwise": "else"
};
```

`=` replaces the current registry. `==` validates that the supplied map matches the current registry.

## Built-in condition helpers

Original helpers:

- `is(a, b)` — structural equality.
- `isNot(a, b)` — structural inequality.
- `empty(value)` — true for nil, empty string, array, map, or set.
- `typeIs(value, "number")` — runtime type check.
- `has(collection, value)` — membership in a map key set, array, or substring in a string.
- `between(value, min, max)` — inclusive numeric range.
- `all(array)` — every item is truthy.
- `any(array)` — at least one item is truthy.
- `none(array)` — no item is truthy.
- `coalesce(a, b, ...)` — first non-nil value.
- `toBool(value)` — explicit truthiness conversion.
- `key_terms()` — returns the current KEY_TERMS registry as a map.

Extended helpers (`rin_conditions.cpp`):

| Group | Helper | Meaning |
| --- | --- | --- |
| Presence | `notNil(x)` | `x` is not nil |
| | `present(x)` | has a real value: not nil, not empty, not whitespace-only text |
| | `blank(x)` | opposite of `present` |
| Strict booleans | `isTrue(x)` / `isFalse(x)` | exactly `true` / `false` (no truthiness: `isTrue(1)` is false) |
| | `xor(a, b)` | exactly one of `a`, `b` is truthy |
| | `implies(p, q)` | `p => q` |
| Choices | `oneOf(x, a, b, ...)` / `oneOf(x, [a, b])` | `x` equals one of the options |
| | `noneOf(x, ...)` | `x` equals none of the options |
| Counting | `exactly(arr, n)` / `atLeast(arr, n)` / `atMost(arr, n)` | number of truthy items in `arr` |
| | `lengthIs(x, n)` | length of text / array / map / set equals `n` |
| Numbers | `outside(x, lo, hi)` | not in the closed range `[lo, hi]` (the opposite of `between`) |
| | `isInt(x)` | a number with no fractional part (text is not converted) |
| | `isZero(x)` | `x == 0` |
| | `closeTo(a, b, eps = 1e-9)` | `abs(a - b) <= eps` |
| Text | `matches(text, pattern)` | the whole text matches the ECMAScript regex |
| Predicates | `every(items, fn)` / `some(items, fn)` | all / at least one item satisfies `fn` (array or set; `every([])` is true) |
| | `countIf(items, fn)` / `findIf(items, fn)` | how many satisfy `fn` / the first one that does (or nil) |
| Introspection | `key_conditions()` | the supported `=term=` forms |

`isNumeric`, `isEven`, `isOdd`, `isPositive`, `isNegative`, `inRange`, `startsWith` and `endsWith` are intentionally **not** native: they already exist as Rin functions in `lib/*.og.rin`, and Rin does not allow a function to share a name with a built-in.

All of these are ordinary native functions, so they compose with existing `if`, `while`, pipelines, functions, and containers.
