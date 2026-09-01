# توثيق Rin

## Rin 1.0.0

الهيكل الرسمي لتوثيق لغة وبيئة تشغيل Rin 1.x. جميع صفحات "اللغة" أدناه مترابطة
بروابط "انظر أيضاً" في نهاية كل صفحة — ابدأ من
[`language-reference.md`](./language-reference.md) للاطّلاع على خريطة الترابط
الكاملة بين كل المفاهيم.

### اللغة
- [`syntax.md`](./syntax.md) — القواعد النحوية العامة (فواصل، كتل، عوامل).
- [`language-reference.md`](./language-reference.md) — المرجع الشامل وخريطة ترابط كل المفاهيم.
- [`variables.md`](./variables.md) — `let`/`text`، مصفوفات، قواميس، نطاق.
- [`control-flow.md`](./control-flow.md) — **الشروط** (`if`/`else`/`plus.condition`) والحلقات (`while`/`for`).
- [`functions.md`](./functions.md) — `fun`/`return`، التكرار الذاتي (recursion).
- [`objects.md`](./objects.md) — `@Object`، `.object("id")`، القاموس الحرفي.
- [`containers.md`](./containers.md) — `@container`، أقسام، ترجمات، مستندات NoSQL.
- [`boat.md`](./boat.md)

### بيئة التشغيل
- [`standard-library.md`](./standard-library.md) — دوال جاهزة (`lib/*.og.rin`).
- [`errors.md`](./errors.md) و[`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) — نظام التشخيص.
- [`storage.md`](./storage.md) — تخزين دائم.
- [`http.md`](./http.md) — شبكات.

### التنفيذ
- [`pipelines.md`](./pipelines.md) — عامل الأنابيب `|>`.
- [`rinflow.md`](./rinflow.md) — طبقة تنفيذ التدفّق المهيكل.

### الواجهة
- [`banner.md`](./banner.md)
- [`android.md`](./android.md)

### API
- [`api.md`](./api.md)

### البداية
- [`getting-started.md`](./getting-started.md)
