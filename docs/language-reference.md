# language-reference.md — المرجع الشامل وخريطة ترابط المفاهيم

هذا الملف هو نقطة البداية التي يشير إليها `README.md` — خريطة **سردية** (وليست
قائمة مسطَّحة) لكيفية ترابط كل مفاهيم Rin ببعضها، لتقرأها بترتيب منطقي بدل
القفز عشوائيًا بين الملفات. كل الملفات المذكورة هنا **موجودة وموثَّقة فعليًا** (لا
روابط ميتة) بنفس درجة التحقّق: كل مثال كود في كل ملف جُرِّب فعليًا عبر المفسِّر
الحقيقي (أو أداة `rin` الرسمية) قبل كتابته.

## 1) اللغة الأساسية — نقطة البداية
ابدأ بـ[`getting-started.md`](./getting-started.md) لتثبيت/بناء أداة `rin` الرسمية
وتشغيل أول برنامج. بعدها، القواعد الأساسية بالترتيب:

`syntax.md` (القواعد النحوية العامة) → `variables.md` (`let`، مصفوفات، قواميس)
→ `control-flow.md` (`if`/`while`/`for`/`match`/`goal`) → `functions.md` (`fun`)
→ `enums.md` (`enum`) → `objects.md` (`class`/`struct`/`.object()`/`@Object`).

هذه الستة تكفي وحدها لقراءة/كتابة أي برنامج Rin إجرائي عادي.

## 2) الحاويات — حيث يلتقي المنطق بالبيانات والواجهة
[`containers.md`](./containers.md) يقدّم الفصل الثلاثي المحوري في Rin:
`@container` (السلوك) / `@loop` (الشكل) / `@element` (الوظيفة البصرية-الخالية).
[`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md) كتالوج كل أنواع `@element.*` الكامل.
[`cross-file-containers.md`](./cross-file-containers.md) يوضّح صراحةً حدود مشاركة
هذه الإعلانات بين ملفات متعددة اليوم (`@import` فقط — لا استيراد مُسمَّى انتقائي
بعد). [`MAKE_UNIT.md`](./MAKE_UNIT.md) نوع حاوية خاص (`@make.(name)`) بسياسة
قدرات صريحة (`use`/`need`/`allow`/`deny`/`strict`) — نفس كلمات السياسة التي عُمِّمت
لاحقًا لأي `@container` عادي في `containers.md` نفسها.

## 3) طبقة التنفيذ — تحويل البيانات لا مجرد تخزينها
[`pipelines.md`](./pipelines.md) (عامل `|>`) هو الأساس الذي يُبنى عليه
[`RECKON.md`](./RECKON.md) (`reckon`: حساب إحصائي بسطرين). كلاهما يمكن تتبّعهما
خطوة بخطوة (رسم بياني، إلغاء، مهلة زمنية) عبر [`rinflow.md`](./rinflow.md) —
طبقة استضافة اختيارية لا تغيّر النتيجة، فقط تضيف رؤية/تحكّمًا.

## 4) بيئة التشغيل — ما يمنحه المفسِّر جاهزًا
[`standard-library.md`](./standard-library.md) فهرس مكتبات `lib/*.og.rin`
(عبر `@import`). [`http.md`](./http.md) و[`storage.md`](./storage.md) الاتصال
بالخارج (شبكة، ملفات، ذاكرة تخزين مؤقت). [`errors.md`](./errors.md) كيف تلتقط
الأخطاء (`try`/`catch`/`throw`) و[`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) الجدول
الكامل لكل كود خطأ قد تراه في أي ملف آخر من هذه القائمة.

## 5) الواجهة — من الوصف إلى البكسلات
[`../indsin.md`](../indsin.md) هو محرّك التنفيذ الذي يحوّل `@element`/`@loop`/
`@container` (القسم 2 أعلاه) إلى شجرة مقاسة ثم بكسلات فعلية — بما في ذلك القدرة
الجديدة على جعل `@element` جذر شاشة مستقل بذاته. [`banner.md`](./banner.md) مثال
تطبيقي كامل لعنصر واجهة واحد (اختصارات `title=`/`message=`، `closable=` وربط Warp
التلقائي، ودوال Banner الأصلية المستقلة عنه).

## 6) الاستضافة والتضمين — تشغيل Rin داخل تطبيق آخر
[`api.md`](./api.md) واجهة C المسطَّحة (`rin_c_api.h`/`rin_indsin_c_api.h`) التي
يُبنى فوقها أي مضيف. [`android.md`](./android.md) المثال العملي الكامل لذلك:
جسر JNI (`RinEngine.kt`) فوق نفس واجهة `api.md` بالضبط، وعارض (`IndsinFabricView.kt`)
يقرأ شجرة Fabric (من `indsin.md`) برسم `Canvas` أصلي مستقل.

## 7) أخيرًا: مكتبة "Boat"
[`boat.md`](./boat.md) مجموعة أنواع بيانات جاهزة (`list`/`tuple`/`dict`/`set`/
`tmap`/`window`) فوق `array`/`map` الأصليتين — مستقلة عن بقية الأقسام أعلاه، إضافة
اختيارية بحتة (`@import "lib/boat.og.rin";`) لا تُبنى فوقها أي ميزة لغة أخرى.

## خريطة سريعة (كل الملفات، بترتيب `README.md`)
| القسم | الملفات |
|---|---|
| اللغة | `syntax` `language-reference` `variables` `control-flow` `functions` `enums` `objects` `containers` `cross-file-containers` `boat` |
| بيئة التشغيل | `standard-library` `errors` `ERROR_SYSTEM` `storage` `http` |
| التنفيذ | `pipelines` `RECKON` `MAKE_UNIT` `rinflow` |
| الواجهة | `../indsin.md` `RIN_ELEMENTS` `banner` `android` |
| API | `api` |
| البداية | `getting-started` |
