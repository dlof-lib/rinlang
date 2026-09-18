# Indsin — Interface Design

`indsin` هو الاسم الموحَّد لكل ما كان يُعرَف سابقاً باسم "Loom": محرّك عرض الواجهات
(rendering engine) في Rin، بكل مضيفيه الثلاثة (أندرويد، سطح المكتب، CLI). الاسم
اختصار لـ **Interface Design**، ويحلّ محل الاستعارة القديمة "النَّسج" كعنوان موحَّد
لكل طبقات تنفيذ الواجهة، بحيث يصبح كل ما يخصّ الـ UI في المشروع تحت مظلّة واحدة
بدل تعدّد الأسماء (Loom / Fabric / Loomtime...).

## ما الذي يغطّيه indsin

`indsin` هو طبقة **التنفيذ (runtime)** التي تُحوّل مفاهيم اللغة الوصفية —
`@element` / `@loop` / `@container` (انظر [`containers.md`](./containers.md)) —
إلى شجرة عناصر مقاسة (measured tree)، ثم إلى عمليات رسم (paint ops)، ثم إلى بكسلات
فعلية على الشاشة. اللغة نفسها لا تتغيّر: `@element.button`، `@loop`، `on.x.click=`
كلها كما هي. ما تغيّر هو اسم المحرّك الذي يُنفِّذها والمكوّنات المبنية عليه.

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
- [`containers.md`](./containers.md) — `@container`، `@element`، `@loop`.
- [`language-reference.md`](./language-reference.md) — خريطة ترابط المفاهيم.
