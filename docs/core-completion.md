# Rin Core Completion Pack

This release closes several important gaps in Rin's core language surface without changing the existing container, Loom, database, HTTP, package, or pipeline APIs.

## 1. Conditional expressions

```rin
let title = loggedIn ? "Dashboard" : "Login";
let result = score >= 60 ? "pass" : "fail";
```

The false branch is lazy: only the selected branch is evaluated.

## 2. Structured exceptions

```rin
try {
    throw "network unavailable";
} catch (err) {
    print err["message"];
}
```

`throw` can carry any Rin value. A caught exception is exposed as a map with `message` and `line`; native Rin errors additionally expose `code`.

Exceptions propagate through function calls until a matching `try/catch` is reached. An uncaught `throw` becomes a normal Rin runtime error instead of escaping the interpreter process.

## 3. Existing production subsystems

The core runtime already includes:

- lexical analysis, recursive-descent parsing, and AST execution
- lexical closures and recursive functions
- arrays and ordered maps
- indexing and indexed assignment
- boolean short-circuiting
- while/for control flow plus break/continue
- pipelines and RinFlow execution tracing
- diagnostics with source locations, recovery, suggestions, and JSON/LSP output
- imports, installed packages, and CLC/RCL library containers
- real HTTP client APIs
- file sandboxing and persistent storage
- NoSQL documents, schemas, indexes, relations, transactions, migrations, cache, watch/subscribe
- UI/Loom views, elements, state, lifecycle, events, slots, themes, warp/mask
- APK/export tooling in the Android application
- standard math, string, collection, JSON, compression, and data/statistics libraries

These systems remain additive and compatible with existing Rin syntax.
