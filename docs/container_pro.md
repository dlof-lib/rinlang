# Rin Container Pro

The Container Pro API extends the existing `container`, `Containers.Group`, `Volume`, `link`, `tying`, `merge`, and `container.doc` systems without replacing them.

## Introspection

```kotlin
container.info("users");
container.fields("users");
container.fieldNames("users");
container.path("profile");
container.descendants("app");
container.byKind("container.doc");
container.count();
container.current();
```

## Field management

```kotlin
container.set("users", "name", "Ali");
container.deleteField("users", "name");
container.clearFields("users");
container.mergeFields("users", {name: "Ali", age: 20});
container.mergeFields("users", {age: 21}, false);
```

`mergeFields(..., false)` preserves existing fields. The default is overwrite.

## Design

- Existing APIs remain compatible.
- Missing containers return `nil` for inspection/query APIs and `false` for mutation APIs.
- `container.path()` and `container.descendants()` follow the real runtime container tree.
- `container.current()` returns the currently executing container or `nil`.

## Transactions

```kotlin
container.transaction("Acc", fun() {
    container.set("Acc", "balance", container.get("Acc", "balance") - 50);
    container.set("Log", "last", "withdraw");
    if (container.get("Acc", "balance") < 0) { throw "insufficient"; }
});
container.transaction(["Acc", "Log"], fn);   // several containers, all-or-nothing
```

`container.transaction(name | [names], fn)` takes a deep snapshot of the named containers' fields (functions excluded), runs `fn()`,
and returns its result. If `fn` throws, every container is restored exactly (changed fields reverted, fields added inside `fn` removed,
deleted fields brought back) and the same error is rethrown. A missing container is an error raised before `fn` runs, so nothing is half-applied.
Unlike `container.restore`, which only overwrites fields, rollback also removes fields added after the snapshot.

### `container.attempt` — the same, without try/catch

```kotlin
let r = container.attempt(["Alice", "Bob"], fun() { ...; return amount; });
if (r["ok"]) { print "moved " + r["value"]; } else { print r["error"]["message"]; }
```

Returns `{ok, value, error}` and never throws: on success `ok=true` and `value` is `fn`'s result; on failure everything is rolled back
exactly as in `transaction`, `ok=false`, and `error` is `{message, line[, value][, code]}`. Handy in UI handlers and loops.
A full bank-transfer example: `examples/container_transactions.rin`.
