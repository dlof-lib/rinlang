# Indsin — Interface Design

`indsin` هو الاسم الموحَّد لكل ما كان يُعرَف سابقاً باسم "Loom": محرّك عرض الواجهات
(rendering engine) في Rin، بكل مضيفيه الثلاثة (أندرويد، سطح المكتب، CLI). الاسم
اختصار لـ **Interface Design**، ويحلّ محل الاستعارة القديمة "النَّسج" كعنوان موحَّد
لكل طبقات تنفيذ الواجهة، بحيث يصبح كل ما يخصّ الـ UI في المشروع تحت مظلّة واحدة
بدل تعدّد الأسماء (Loom / Fabric / Loomtime...).

## ما الذي يغطّيه indsin

`indsin` هو طبقة **التنفيذ (runtime)** التي تُحوّل مفاهيم اللغة الوصفية —
`@element` / `@loop` / `@container` (انظر [`docs/containers.md`](./docs/containers.md)
للدليل الكامل، و[`docs/RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) لكتالوج
`@element.*` كاملاً) —
إلى شجرة عناصر مقاسة (measured tree)، ثم إلى عمليات رسم (paint ops)، ثم إلى بكسلات
فعلية على الشاشة. اللغة نفسها لا تتغيّر: `@element.button`، `@loop`، `on.x.click=`
كلها كما هي. ما تغيّر هو اسم المحرّك الذي يُنفِّذها والمكوّنات المبنية عليه.

### `@element` كجذر شاشة مستقل

`@element` كان يعمل فقط متداخلاً داخل `@view` (أو `@loop`) — أي برنامج يحتوي على
`@element` وحده بدون `@view`/`@loop` كان يفشل بخطأ "no top-level ... root found"،
رغم أن المحرّك (`buildFabric`/`layout`/`paint`) لا يميّز أصلاً بين عنصر من نوع
`role=VIEW` وآخر من نوع `role=ELEMENT` عند بناء الشجرة أو قياسها أو رسمها — التمييز
كان فقط في *اختيار الجذر*. الآن `pickRootFromProgram()`
(`rin_indsin_pipeline.h`) يقبل `@element` كجذر شاشة مستقل عند غيابِ أي `@view`/`@loop`
على المستوى الأعلى، مع حفظ الأولوية الكاملة لـ`@view`/`@loop` عند وجودهما معًا (سلوك
قديم لم يتغيّر). `sanitizeElements()` تبقى تُطبَّق كما هي — كون العنصر جذرًا لا يمنحه
تنسيقًا بصريًا مدمجًا لم يكن يملكه أصلاً؛ يبقى "وظيفيًا بحتًا" حتى وهو الشاشة كاملةً.

| الطبقة | الموقع | ملاحظات |
|---|---|---|
| المحرّك الأساسي (C++) | `app/src/main/cpp/indsin/` | `rin_indsin_layout.h`, `rin_indsin_paint.h`, `rin_indsin_strand.h`, `rin_indsin_needle.h` (hit-test)، `rin_indsin_shuttle.h` (hot-reload)، إلخ. الواجهة العامة المستقرة: `rin_indsin_c_api.h/.cpp`. |
| أندرويد (Kotlin) | `app/src/main/java/com/dlof/rinlang/Indsin*.kt` | `IndsinFabricView` (المُصيِّر)، `IndsinPreviewActivity`/`IndsinPreviewManager` (المعاينة الحيّة)، `IndsinViewTracer`. مربوطة بالمحرّك عبر `RinEngine.IndsinSession` (JNI). |
| سطح المكتب | `tools/rin_indsin_desktop.cpp` | نافذة X11 حقيقية فوق نفس مسار الرسم (`rin_indsin_session_render_rgb`). |
| CLI | `tools/rin_indsin_run.cpp` | يُخرج JSON (fabric + paint ops) لأي ملف `.rin`، بلا أي واجهة رسومية — مفيد للاختبار والـ CI. |

هذه المضيفة الثلاثة (أندرويد/سطح المكتب/CLI) تُشغِّل **نفس** مسار القياس والرسم في
المحرّك — لا يوجد تكرار أو إعادة تنفيذ منطق لكل منصّة.

## المصطلحات الداخلية

المصطلحات الاستعارية الداخلية للمحرّك (Strand، Needle، Shuttle، Warp، Fabric) بقيت
كما هي — هذه أسماء لبُنى داخل `indsin` نفسه (شجرة القياس، البحث بالإحداثيات،
إعادة التحميل الحيّ، حالة التفاعل، شجرة العرض الناتجة) وليست أسماء بديلة للمحرّك
ذاته. `indsin` هو الاسم الوحيد للمحرّك/الماركة؛ وما دونه تفاصيل تنفيذ داخلية.

## خريطة إعادة التسمية (للمُساهمين)

| القديم | الجديد |
|---|---|
| `Loom` (كاسم للمحرّك) | `Indsin` |
| `Loomtime` | `Indsintime` |
| `LoomSession` / `loomSession*Native` | `IndsinSession` / `indsinSession*Native` |
| `LoomFabricView` | `IndsinFabricView` |
| `LoomPreviewActivity` / `LoomPreviewManager` | `IndsinPreviewActivity` / `IndsinPreviewManager` |
| `rin_loom_*.h/.cpp` | `rin_indsin_*.h/.cpp` |
| `app/src/main/cpp/loom/` | `app/src/main/cpp/indsin/` |

## انظر أيضاً
- [`docs/containers.md`](./docs/containers.md) — `@container`، `@element`، `@loop` بالتفصيل.
- [`docs/RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) — كتالوج `@element.*` الكامل.
- [`docs/language.html`](./docs/language.html) — "جولة في لغة Rin": جولة عامة في اللغة
  من المتغيّر الأول إلى الحاوية الكاملة (لم يكن يغطي `@element`/`@loop` قبل هذا
  الملف — `docs/containers.md` أعلاه هو المكمِّل له لهذا الجزء تحديدًا).
