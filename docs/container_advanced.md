# Rin Container Advanced Built-ins

The Container API now includes introspection, hierarchy navigation, search, field utilities, and runtime statistics.

## Introspection

```rin
container.exists("Users");
container.kind("Users");
container.size("Users");
container.empty("Users");
container.info("Users");
container.fields("Users");
container.fieldNames("Users");
container.fieldType("Users", "name");
container.snapshot("Users");
```

## Hierarchy

```rin
container.parent("Profile");
container.childrenOf("Users");
container.descendants("App");
container.ancestors("Profile");
container.siblings("Profile");
container.depth("Profile");
container.hasChildren("Users");
container.childCount("Users");
container.roots();
container.leaves();
container.path("Profile");
```

## Search

```rin
container.byKind("container.doc");
container.find("container.doc");
container.find("container.doc", {active: true});
container.findField("name", "Ali");
```

## Field operations

```rin
container.set("Users", "name", "Ali");
container.get("Users", "name");
container.has("Users", "name");
container.renameField("Users", "name", "displayName");
container.deleteField("Users", "displayName");
container.mergeFields("Users", {city: "Beirut", active: true});
container.clearFields("Users");
```

## Runtime statistics

```rin
container.count();
container.stats();
container.current();
```
