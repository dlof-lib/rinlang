# Rin Container Advanced Built-ins

The Container API now includes introspection, hierarchy navigation, search, field utilities, and runtime statistics.

## Introspection

```kotlin
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

```kotlin
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

```kotlin
container.byKind("container.doc");
container.find("container.doc");
container.find("container.doc", {active: true});
container.findField("name", "Ali");
```

## Field operations

```kotlin
container.set("Users", "name", "Ali");
container.get("Users", "name");
container.has("Users", "name");
container.renameField("Users", "name", "displayName");
container.deleteField("Users", "displayName");
container.mergeFields("Users", {city: "Beirut", active: true});
container.clearFields("Users");
```

## Runtime statistics

```kotlin
container.count();
container.stats();
container.current();
```
