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
- [`input.md`](./input.md) — إدخال المستخدم: `input` · `inputNumber` · `confirm` · `choose` — تستدعي enum وOOP والحاويات والمُدقِّقات (validator, target/key).
- [`control-flow.md`](./control-flow.md) — **الشروط** (`if`/`else`/`when`/`otherwise`/`plus.condition`/`match`/`case`)، الحلقات (`while`/`for`)، و`goal`/`achieve`.
- [`functions.md`](./functions.md) — `fun`/`return`، التكرار الذاتي (recursion).
- [`enums.md`](./enums.md) — `enum`: قوائم اختيار (options) مغلقة.
- [`objects.md`](./objects.md) — `@Object`، `.object("id")`، القاموس الحرفي.
- [`indsin_expansion.md`](./indsin_expansion.md) — Indsin Design System v2: توكنز، ثيمات، تجاوب، حركات، actions، مدقّق.
- [`oop.md`](./oop.md) — OOP الموسَّع (Rin 1.0): `interface` · `trait` · `abstract/final/override` · `static` · `private/protected` · `get/set` · عوامل سحرية · دوال `oop.*`.
- [`containers.md`](./containers.md) — `@container`، أقسام، ترجمات، مستندات NoSQL.
- [`cross-file-containers.md`](./cross-file-containers.md) — `use ... from` (English) — calling a container/UI element from another file.
- [`boat.md`](./boat.md)

### بيئة التشغيل
- [`standard-library.md`](./standard-library.md) — دوال جاهزة (`lib/*.og.rin`).
- [`errors.md`](./errors.md) و[`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) — نظام التشخيص.
- [`storage.md`](./storage.md) — تخزين دائم.
- [`http.md`](./http.md) — شبكات.

### التنفيذ
- [`pipelines.md`](./pipelines.md) — عامل الأنابيب `|>`.
- [`RECKON.md`](./RECKON.md) — `reckon`: مفهوم حسابي بسطرين، مبني فوق `|>`.
- [`MAKE_UNIT.md`](./MAKE_UNIT.md) — `@make.(name)`، سياسة القدرات (`use`/`need`/`allow`/`deny`/`strict`).
- [`rinflow.md`](./rinflow.md) — طبقة تنفيذ التدفّق المهيكل.

### الواجهة
- [`banner.md`](./banner.md)
- [`android.md`](./android.md)

### API
- [`api.md`](./api.md)

### البداية
- [`getting-started.md`](./getting-started.md)

### مراجع إضافية
- [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md) — دليل `@element.*` الكامل.
- [`events-effects.md`](./events-effects.md) — الأحداث والتأثيرات (محرك Indsin UI).
- [`container_advanced.md`](./container_advanced.md) · [`container_pro.md`](./container_pro.md) · [`container_sql.md`](./container_sql.md) — دوال الحاويات المتقدمة وRCSQL.
- [`loops.md`](./loops.md) · [`key_terms_and_builtins.md`](./key_terms_and_builtins.md) · [`number_builtins.md`](./number_builtins.md) — الحلقات، الشروط المبنية، دوال الأرقام.
- [`native-additions.md`](./native-additions.md) — الدوال الأصلية الجديدة (Rin 1.0).
- [`packages-and-interop.md`](./packages-and-interop.md) — المكتبات والحزم وجسر C++ وJSON واختبارات rintest.
- [`rdoc.md`](./rdoc.md) · [`rdoc-reference.md`](./rdoc-reference.md) — حاوية التوثيق rdoc.
- [`simple-style.md`](./simple-style.md) — Rin Simple Style.
- [`TOOLCHAIN.md`](./TOOLCHAIN.md) · [`TOOLCHAIN_UPGRADE.md`](./TOOLCHAIN_UPGRADE.md) — سلسلة أدوات Rin.
- [`VERSIONING.md`](./VERSIONING.md) — سياسة الإصدارات.
- مكتبات Rin الرسمية: [`../lib/README.md`](../lib/README.md) (مُولَّد آلياً).
