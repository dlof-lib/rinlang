# CHANGES_HASH — عائلة `#`

إضافة اختيارية بالكامل: `#sed #for #in #done #while #sum #do #swap #return #diff #add #ban #to #ignorance`.
الشرح: `docs/hash-family.md` · مثال: `examples/hash_demo.rin`.

## الملفات
- `rin_lexer.cpp|.h` — `scanHashWord()`؛ `#for/#while/#return` ← نفس توكنات for/while/return، و`#in` ← in، وغيرها IDENT يحتفظ بـ `#`.
- `rin_ast.h` — حقل `doneBlock` في WhileStmt وForStmt وForInStmt.
- `rin_parser.h|.cpp` — `hashStatement` (#do #done #ban #ignorance #swap)، `attachDoneBlock`، `maybeHashRange` (`A #to B step C`).
- `rin_interpreter.cpp|.h` — تنفيذ كتلة `#done` في الحلقات الثلاث، و`#ban` عبر آلية stone (E0043)، وتسجيل `registerNativesExtra5`.
- `rin_extra_natives5.cpp` (جديد) — `#sed #sum #diff #add #to #swap`.
- التلوين: `syntaxes/rin-keywords.json` و`syntaxes/rin.tmLanguage.json` و`RinSyntax.kt` و`RinSyntaxHighlighter.kt`.
- اختبارات: `tests/verification/hash_*.rin|.expected` (6).

## التحقق
`tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 36/37
(الفرق الوحيد `input_forms`: اسم الملف في موقع الخطأ بأداة `rin_run`، وكان كذلك قبل التعديل).
بُني على سطح المكتب (g++). لم يُبنَ APK ولم تُترجَم ملفات Kotlin.
