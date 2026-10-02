# CHANGES_LIVING — المتغيرات الحيّة

إضافة اختيارية بالكامل (additive): `stone` `gauge` `tape` `lens` `fuse` `bell` `trial` + `undo` `redo` `rearm` `unbell` `abort`.
الشرح: `docs/living-variables.md`.

## ملفات المحرّك (app/src/main/cpp)
- `diagnostics/diagnostic.h|.cpp` — أكواد E0043..E0047.
- `rin_ast.h` — `LiveDeclStmt/BellStmt/TrialStmt/LiveActionStmt`.
- `rin_parser.h|.cpp` — `liveDeclaration/bellDeclaration/trialStatement/liveActionStatement` + التوزيع في `declaration()`.
- `rin_interpreter.h|.cpp` — `LiveMeta` داخل `Environment::live`، وخمس نقاط ربط (قراءة/إسناد/خاصية/كتابة محتوى/تنفيذ عبارات) خلف `liveActive_`.

## أدوات وتوثيق
`syntaxes/rin-keywords.json` و`syntaxes/rin.tmLanguage.json` و`RinSyntax.kt` و`RinSyntaxHighlighter.kt` (تلوين)،
`docs/living-variables.md` (جديد) + روابط في `variables.md` `language-reference.md` `ERROR_SYSTEM.md`،
`examples/living_variables_demo.rin`، و`tests/verification/living_*.rin|.expected` (8 اختبارات).

## التحقق
`tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 8/9 (الفرق الوحيد اسم الملف في موقع الخطأ بأداة `rin_run`).
بُني على سطح المكتب (g++). لم يُبنَ APK.
