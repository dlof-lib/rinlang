# CHANGES_PRE_POST — `pre` و`post` (ما قبل / ما بعد): عقد انتقال حول الإسناد

إضافة اختيارية بالكامل (additive) من عائلة المتغيرات الحيّة، فوق `base`/`accept`: كلمتان سياقيتان غير محجوزتين
(`let pre = 1;` و`fun f(post){...}` ما زالا صالحين). الشرح الكامل: `docs/pre-post.md`.

- `pre  x = v if RULE [post RULE2] [strict];` — يُسأل **قبل** الكتابة (x = القيمة القديمة)؛ إن كُذِّب لا تُكتب.
- `post x = v if RULE [pre  RULE2] [strict];` — يُسأل **بعد** الكتابة (x = الجديدة)؛ إن كُذِّب تُلغى الكتابة.
- في الشرطين: `old` = السابقة، `new` = المكتوبة. القيمة الابتدائية لا تُسأل عنها `pre`، وتُسأل عنها `post` (old = new).
- خصائص: `x.pre` `x.post` (آخر محاولة: قبل ← بعد) + `x.ok` `x.accepted` `x.rejected` `x.refused` (مشتركة مع accept) + `x.kind` `x.vitals`.
- الافتراضي صامت (`x.ok` تصير false)؛ `strict` يرمي الكود الجديد `E0051`.
- يعمل مع `inc`/`dec`، والأجراس ترنّ عند النجاح فقط، و`trial/abort` يعيد القيمة والعدّادات.

## الملفات
**جديدة لهذه الميزة:** `docs/pre-post.md` · `examples/pre_post_demo.rin` · `tests/verification/pre_post_basic.rin|.expected` · `CHANGES_PRE_POST.md`
**معدَّلة:** `app/src/main/cpp/` ← `rin_ast.h` (LiveKind::Pre/Post + preRule/postRule) · `rin_parser.cpp` (التوزيع + `if`/`pre`/`post`/`strict` + رسالة الخطأ) ·
`rin_interpreter.h|.cpp` (حقول LiveMeta، liveGateEval/liveGateRefuse، الحارس في liveAssign، التصريح، الخصائص و vitals) · `diagnostics/diagnostic.h|.cpp` (E0051).
التلوين: `syntaxes/rin-keywords.json` · `syntaxes/rin.tmLanguage.json` (+ `editors/vscode-indsin/...`) · `RinSyntax.kt` · `RinSyntaxHighlighter.kt`.
توثيق: `docs/variables.md` `language-reference.md` `living-variables.md` `ERROR_SYSTEM.md`. لا ملفات جديدة في المحرّك (لا تعديل على CMake/Gradle).

## ملاحظة دمج
الحزمة **تشمل أيضًا** ملفات `base`/`accept` (`docs/base-accept.md`، الاختبارات، E0049/E0050...) لأن نسخة `rinlang-main_3` لا تحتويها،
و`pre/post` مبنيّ فوقها (نفس الملفات). ضع الحزمة فوق المشروع كما هي.

## التحقق
بُني CLI على لينكس (g++/CMake) · `tests/rintests.rin` 83/83 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 43/58
(الـ15 الفاشلة نفسها قبل التعديل: `input_*` و`let_err_*`، والاختبار الجديد ينجح) · جُرِّبت أخطاء: غياب الشرط، شرط مكرّر، `else`، `strict` قبل/بعد، قيمة ابتدائية تخالف `post`، trial/abort.
لم يُبنَ APK. `rinc` (Rin → C) لا يدعم الصيغة بعد.
> ملاحظة بناء قديمة غير مرتبطة: `rin_extra_natives7.cpp` يستعمل `unix` كاسم متغيّر فلا يُترجَم بـ `-std=gnu++17` على لينكس (يلزم `-DCMAKE_CXX_EXTENSIONS=OFF`)؛ لم أغيّره.

---
# إضافة: `label` و`define` (تسمية وتعريف)
`label x = v as TITLE [about T] [unit U];` و`define x = v as TEXT [label T] [unit U];` — بيانات وصفية للمتغيّر بلا أثر على الإسناد.
خصائص: `x.label` `x.about` `x.unit` `x.id` `x.info` (+ vitals). الشرح: `docs/label-define.md`.
الملفات: نفس ملفات المحرّك أعلاه (`rin_ast.h` `rin_parser.cpp` `rin_interpreter.h|.cpp`) + التلوين + `docs/label-define.md` + `tests/verification/label_define_basic.rin|.expected`.
