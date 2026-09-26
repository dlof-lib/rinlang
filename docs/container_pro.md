# Rin Container Pro

The Container Pro API extends the existing `container`, `Containers.Group`, `Volume`, `link`, `tying`, `merge`, and `container.doc` systems without replacing them.

## Introspection

```rin
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

```rin
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
