# CHANGES_BASE_ACCEPT — `base` و`accept`: متغيّران يخدمان `if`

إضافة اختيارية بالكامل (additive) من عائلة المتغيرات الحيّة: كلمات سياقية غير محجوزة، فبرنامج قديم يعمل كما كان حرفيًا
(`let base = 5;` و`fun inc(x){...}` و`let accept = 1;` ما زالت صالحة). الشرح: `docs/base-accept.md`.

- `base x = v [to GOAL] [step N];` — خط أساس: `x.base` `x.same` `x.changed` `x.up` `x.down` `x.delta` `x.goal` `x.reached` `x.togo` `x.progress` `x.step`.
- `accept x = v if RULE [else FALLBACK] [strict];` — حارس قبول: `x.ok` `x.accepted` `x.rejected` `x.refused`. الاسم داخل الشرط = القيمة المرشّحة.
- أفعال: `inc x [by n];` `dec x [by n];` (على أي متغيّر رقمي وتمرّ من حرّاسه) و`rebase x;` `rewind x;` (لـ base).
- أكواد جديدة: `E0049` (رفض accept) و`E0050` (استعمال خاطئ لـ base).

## الملفات (app/src/main/cpp)
`diagnostics/diagnostic.h|.cpp` (E0049/E0050) · `rin_ast.h` (LiveKind::Base/Accept + حقول LiveDeclStmt) ·
`rin_parser.cpp` (التوزيع في declaration() + مُعدِّلات to/step/if/else/strict + inc/dec/rebase/rewind) ·
`rin_interpreter.h|.cpp` (حقول LiveMeta، liveCheckAccept، حارس في liveAssign، أفعال، خصائص).
لا ملفات جديدة في المحرّك (لا تعديل على CMake/Gradle).

## أدوات وتوثيق
`syntaxes/rin-keywords.json` و`syntaxes/rin.tmLanguage.json` (+ نسخة `editors/vscode-indsin`) و`RinSyntax.kt` و`RinSyntaxHighlighter.kt` (تلوين) ·
`docs/base-accept.md` (جديد) + روابط في `variables.md` `language-reference.md` `living-variables.md` `ERROR_SYSTEM.md` ·
`examples/base_accept_demo.rin` · `tests/verification/base_basic|accept_basic|base_accept_if`.

## التحقق
`tests/rintests.rin` 83/83 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 42/57 (الـ15 الفاشلة نفسها قبل التعديل وبعده بالضبط:
`input_*` و`let_err_*`، اسم الملف في موقع الخطأ بأداة `rin_run`)، والاختبارات الثلاثة الجديدة تنجح.
بُني على سطح المكتب (g++). لم يُبنَ APK. `rinc` (Rin → C) لا يدعم الصيغ الجديدة بعد.
