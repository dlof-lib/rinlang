# Twinkling Stars 1.0.0

Professional app-building toolkit for Rin + Indsin.

- Design tokens and reusable component models
- Screen descriptors, navigation and app metadata
- Form models and validation helpers
- JSON-compatible data helpers and export manifest
- Safe text-to-HTML helpers
- Explicit native capability contracts for Android/C++ host adapters

## Import

```rin
@import "lib/twinkling-stars.og.rin";
let app = ts_app("Stellar Notes", "1.0.0", "com.example.stellarnotes", "portrait");
let home = ts_screen("home", "Welcome", "dashboard");
let title = ts_text("welcome-title", "Hello, universe", "headline");
home = ts_add_child(home, title);
print ts_export_manifest(app);
```

The flat `.og.rin` file is the directly importable engine entry. The folder package contains `.zpp` source modules for RinPM/Disconom distribution. Native camera, notifications, storage, media and sharing require a host implementation; this library returns typed bridge requests and never pretends to execute unavailable device APIs.

See `docs/twinkling-stars.md` for API groups, examples and integration notes.
