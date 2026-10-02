# CHANGES_LET — `let` المطوَّر

إضافة اختيارية بالكامل: الصيغة القديمة `let x = 5;` لم تتغيّر. الشرح: `docs/let-plus.md`.

- تفكيك المصفوفات والقواميس والكائنات: `let [a, b = 0, ...rest] = arr;` و`let {name, city: town} = m;` (تعشيش، `_`، افتراضيات، `...rest`).
- عدة تصريحات: `let a = 1, b = a + 1;`
- حارس: `let v = f() else { return; };` (كتلة else يجب أن تغادر النطاق وإلا E0048).
- تفكيك في `for`: `for (let [k, v] in pairs)`.
- إسناد تفكيكي: `[a, b] = [b, a];` و`[arr[0], arr[1]] = [arr[1], arr[0]];`، ويعمل مع المتغيرات الحيّة.
- `export let a = 1, b = 2;` يصدّر كل الأسماء.

## الملفات
`diagnostics/diagnostic.h|.cpp` (E0048) · `rin_ast.h` (LetPattern, LetPatternStmt, LetGroupStmt, PatternAssignExpr،
وحقل elseBlock في LetStmt وpattern في ForInStmt) · `rin_parser.h|.cpp` · `rin_interpreter.cpp`.

## التحقق
`tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 30/31
(الفرق الوحيد `input_forms`: اسم الملف في موقع الخطأ بأداة `rin_run`، وكان كذلك قبل التعديل).
بُني على سطح المكتب (g++). لم يُبنَ APK.
