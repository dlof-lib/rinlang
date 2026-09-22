# توثيق Rin

## Rin 1.0.0

الهيكل الرسمي لتوثيق لغة وبيئة تشغيل Rin 1.x. جميع صفحات "اللغة" أدناه مترابطة
بروابط "انظر أيضاً" في نهاية كل صفحة — ابدأ من
[`language-reference.md`](./docs/language-reference.md) للاطّلاع على خريطة الترابط
الكاملة بين كل المفاهيم.

### اللغة
- [`syntax.md`](./docs/syntax.md) — القواعد النحوية العامة (فواصل، كتل، عوامل).
- [`language-reference.md`](./docs/language-reference.md) — المرجع الشامل وخريطة ترابط كل المفاهيم.
- [`variables.md`](./docs/variables.md) — `let`/`text`، مصفوفات، قواميس، نطاق.
- [`control-flow.md`](./docs/control-flow.md) — **الشروط** (`if`/`else`/`when`/`otherwise`/`plus.condition`/`match`/`case`)، الحلقات (`while`/`for`)، و`goal`/`achieve`.
- [`functions.md`](./docs/functions.md) — `fun`/`return`، التكرار الذاتي (recursion).
- [`enums.md`](./docs/enums.md) — `enum`: قوائم اختيار (options) مغلقة.
- [`objects.md`](./docs/objects.md) — `@Object`، `.object("id")`، القاموس الحرفي.
- [`containers.md`](./docs/containers.md) — `@container`، أقسام، ترجمات، مستندات NoSQL.
- [`cross-file-containers.md`](./docs/cross-file-containers.md) — `@import` عبر ملفات (تنبيه: عبارة `use ... from` المخطَّط لها أصلاً **غير مُنفَّذة بعد**، موثَّق بالتفصيل داخل الملف).
- [`boat.md`](./docs/boat.md)

### بيئة التشغيل
- [`standard-library.md`](./docs/standard-library.md) — دوال جاهزة (`lib/*.og.rin`).
- [`errors.md`](./docs/errors.md) و[`ERROR_SYSTEM.md`](./docs/ERROR_SYSTEM.md) — نظام التشخيص.
- [`storage.md`](./docs/storage.md) — تخزين دائم.
- [`http.md`](./docs/http.md) — شبكات.

### التنفيذ
- [`pipelines.md`](./docs/pipelines.md) — عامل الأنابيب `|>`.
- [`RECKON.md`](./docs/RECKON.md) — `reckon`: مفهوم حسابي بسطرين، مبني فوق `|>`.
- [`MAKE_UNIT.md`](./docs/MAKE_UNIT.md) — `@make.(name)`، سياسة القدرات (`use`/`need`/`allow`/`deny`/`strict`).
- [`rinflow.md`](./docs/rinflow.md) — طبقة تنفيذ التدفّق المهيكل.

### الواجهة
- [`indsin.md`](./indsin.md) — **Interface Design**: المحرّك الموحَّد لعرض الواجهات (سابقاً "Loom") وكيف يُنفِّذ `@element`/`@loop`/`@container`.
- [`RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) — كتالوج `@element.*` الكامل.
- [`banner.md`](./docs/banner.md)
- [`android.md`](./docs/android.md)

### API
- [`api.md`](./docs/api.md)

### البداية
- [`getting-started.md`](./docs/getting-started.md)
