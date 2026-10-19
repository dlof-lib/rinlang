# CHANGES_TOKEN.md — تطوير مفهوم token

إضافة بحتة: لا تغيير في `tokens`/`tokenType`. (يتضمّن هذا الأرشيف أيضاً ملفات ضمانات النطاق finally/defer/catch-when/rethrow لأن `rin_interpreter.cpp` مدموج معها.)

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_extra_natives8.cpp` | جديد: 11 دالة token |
| `app/src/main/cpp/rin_interpreter.cpp/.h` | تسجيل `registerNativesExtra8` وتضمين الملف |
| `app/src/main/cpp/rinc.cpp`، `RinSyntax.kt` | الأسماء الجديدة |
| `syntaxes/rin-keywords.json`، `scripts/intellisense_docs.json` | الأسماء والتوثيق |
| `app/src/main/cpp/editor/rin_intellisense_data.h` | مُعاد توليده بالمولّد (+11 سطراً) |
| `docs/TOKEN.md` | الوثيقة |
| `tests/verification/token_concept.rin/.expected` | الاختبار |
