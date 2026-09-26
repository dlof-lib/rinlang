# Rin KEY_TERMS and Built-in Conditions

## Key conditions

Rin supports compact key-condition forms that reuse the normal `IfStmt` runtime:

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

`=if=` and `=when=` have the same semantics as `if`. `=unless=` negates the condition. `=else=` can follow these forms as an alternative spelling for `else`.

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

- `is(a, b)` — structural equality.
- `isNot(a, b)` — structural inequality.
- `empty(value)` — true for nil, empty string, array, or map.
- `typeIs(value, "number")` — runtime type check.
- `has(collection, value)` — membership in a map key set, array, or substring in a string.
- `between(value, min, max)` — inclusive numeric range.
- `all(array)` — every item is truthy.
- `any(array)` — at least one item is truthy.
- `none(array)` — no item is truthy.
- `coalesce(a, b, ...)` — first non-nil value.
- `toBool(value)` — explicit truthiness conversion.
- `key_terms()` — returns the current KEY_TERMS registry as a map.

All of these are ordinary native functions, so they compose with existing `if`, `while`, pipelines, functions, and containers.
