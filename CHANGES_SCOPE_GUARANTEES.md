# CHANGES_SCOPE_GUARANTEES.md — ضمانات النطاق (finally / defer / catch-when / rethrow)

إضافة بحتة (additive): لا تغيير في سلوك أي `try/catch` قديم، والكلمات الجديدة سياقية غير محجوزة.

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_ast.h` | `CatchClause`؛ حقول `catchGuard/moreCatches/finallyBranch/needsInterp/fromDefer` في `TryCatchStmt`؛ `ThrowStmt::rethrow` |
| `app/src/main/cpp/rin_parser.cpp` | تحليل `finally` و`catch … when` و`rethrow` و`defer` (+ `applyDefers` يختزل defer إلى try/finally) |
| `app/src/main/cpp/rin_interpreter.h/.cpp` | تنفيذ finally بكل مسارات الخروج، فلترة catch، `rethrow` (`activeCatches_`)، `e.code` لقواميس `throw` |
| `app/src/main/cpp/rin_bcvm_compiler.cpp` | تفويض العبارات الجديدة للمفسِّر (بلا opcodes جديدة) |
| `app/src/main/cpp/rin_make.cpp`، `toolchain/rin_toolchain.cpp` | المرور على `finallyBranch` وبنود catch الإضافية (+ الحارس في المحلّل الدلالي) |
| `rin_editor_engine.cpp`، `RinRepl.kt`، `RinSyntax.kt` | تلوين/إكمال `finally` `defer` `rethrow` |
| `docs/scope-guarantees.md` | الوثيقة |
| `tests/verification/scope_guarantees.rin/.expected` | الاختبار |
