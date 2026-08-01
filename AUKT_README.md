# AUKT — Automated Knowledge Tables (جداول المعرفة الآلية)

مفهوم جديد فوق لغة Rin الحالية لبناء **جداول عمل وقواميس معرفة** قابلة
للتخصيص بالكامل (شكل/ستايل، خطوط، أيقونات، شريط أدوات)، محفوظة بامتداد
ملف مميّز: **`.aak.rin`**.

---

## 1) الفكرة

AUKT ليست لغة/محرّكاً جديداً منفصلاً — هي **كتلة مُجمِّعة (composite)** فوق
مفاهيم موجودة أصلاً في Rin (`table` / `doc` / `Object` / `sticker` / `block` /
`style`)، تُغلِّفها كلها تحت اسم واحد ومظلّة واحدة:

```
@AUKT=name
    text title = "...";       // عنوان/وصف حرّ لحزمة المعرفة
    style value="style://dark";  // نمط عرض عام افتراضي للحزمة كلها

    @table=...   ... .end/table    // جدول (أو عدّة جداول)
    @doc=...     ... .end/doc      // قاموس/قواميس NoSQL
    @Object=fonts ... .end/Object  // مكتبة خطوط
    @sticker=icons ... .end/sticker // أيقونات + هوية بصرية
    @block="toolbar" ... .end/block // شريط أدوات قابل للتخصيص
.end/AUKT
```

يمكن أيضاً كتابتها بالصيغة المدمجة `@container.aukt=name ... .end/container.aukt`
(نفس الشيء تماماً، تماماً كما `@table=` و`@container.table=` ينتجان نفس النوع).

## 2) الامتداد `.aak.rin`

- **الترميز/البنية الداخلية للملف تبقى نص Rin عادياً 100%** — لا صيغة ثنائية
  جديدة ولا محلّل نحوي منفصل. أي محرّر نصوص عادي أو محرّك Rin الحالي يقرأه
  بلا أي تعديل.
- الامتداد `.aak.rin` هو إشارة **بصرية/تصنيفية** فقط: يُستخدم ليعرف التطبيق
  أن هذا الملف تحديداً يفتح تلقائياً في "محرِّر AUKT" المخصَّص (جداول +
  قواميس + منتقي خطوط/أيقونات + شريط أدوات) بدل المحرِّر النصي العادي —
  تماماً كما يميّز نظام التشغيل `.docx` عن `.txt` رغم أن كليهما ملفات مضغوطة/
  نصية في الأصل.
- `save`/`installation` لحاوية من نوع `AUKT` بلا مسار صريح تُنتج تلقائياً
  `name.aak.rin` (أو `name.min.aak.rin` مبسّطة) بدل `name.rin` العامة.

## 3) عناصر التخصيص

| العنصر               | الآلية                                          |
|----------------------|--------------------------------------------------|
| شكل/ستايل الجدول      | `style value="style://<theme>";` (داخل `@table` أو عام داخل `@AUKT`) |
| مكتبة خطوط            | `@Object=fonts` + حقول `text` حرة (`primary`, `secondary`, `monospace`, ...) |
| أيقونات               | `@sticker=icons` + حقول `text icon/colors/edges/animation/...` |
| شريط أدوات قابل للتخصيص | `@block="toolbar"` + حقل `text items = "جديد,حذف,فرز,...";` |
| قاموس/بيانات حرة       | `@doc=name` + `document id="..." fields={...};` (schema-less) |

راجع المثال الكامل: [`samples/aukt_demo.aak.rin`](samples/aukt_demo.aak.rin).

## 4) التعديلات البرمجية المُدخلة على محرّك Rin

كل التعديلات **إضافية بحتة** (additive) — لا حذف ولا تغيير سلوك لأي كتلة
موجودة أصلاً (`container`/`table`/`doc`/`Object`/`portal`/`block`/`sticker`
كلها كما هي حرفياً).

- **`app/src/main/cpp/rin_ast.h`**
  - إضافة `AUKT` إلى `enum class ContainerKind`.
  - تعليق تصميمي كامل يشرح المفهوم فوق التعريف مباشرة.

- **`app/src/main/cpp/rin_parser.cpp`**
  - `readTagKeyword()`: التعرّف على `aukt` ككلمة سياقية بعد `container.`
    (بنفس أسلوب `table`/`doc`/`object`/`portal`/`block`).
  - `atBlock()`: إضافة `"container.aukt"` و`"AUKT"` إلى قائمة الوسوم
    الصالحة، وربطها بـ `ContainerKind::AUKT`.
  - **مهم:** على عكس `table/doc/object/portal/block/sticker`، لا تُستدعى
    `validateDataContainerBody()` لأجسام AUKT — أي يُسمح بالتعشيش الكامل
    بداخلها (حاويات `@table`/`@doc`/`@Object`/`@sticker`/`@block` فرعية)،
    تماماً كـ `container` العادية.

- **`app/src/main/cpp/rin_interpreter.cpp`**
  - `containerTagName()` / `containerIcon()`: حالة جديدة لـ
    `ContainerKind::AUKT` (الوسم `container.aukt`، الأيقونة 📚).
  - دالة مساعدة جديدة `defaultRinExtension(kind, simplified)` تُرجع
    `.aak.rin` (أو `.min.aak.rin`) لحاويات AUKT، و`.rin`/`.min.rin` لبقية
    الأنواع كما كانت — تُستخدم في مسارات `save`/`installation` الافتراضية
    (بلا `path` صريح).
  - السماح بعبارة `style` مباشرة داخل `@AUKT`/`@container.aukt` (أُضيفت
    `ContainerKind::AUKT` إلى قائمة الأنواع المسموحة لـ `StyleStmt`).

## 5) ملاحظة صادقة

لم أتمكن من بناء (compile) أو تشغيل أي من هذا فعلياً في هذه البيئة (لا
Android SDK/NDK متاح هنا) — بالضبط نفس القيد المذكور أصلاً في
`README_شامل.md` لهذا المشروع. كل الأكواد رُوجعت يدوياً بعناية مقابل بقية
أنماط الحاويات المطابقة (`table`/`doc`/`object`/`portal`/`block`/`sticker`)
التي تتبع كلها نفس الأسلوب البرمجي بالضبط في هذا الملف.

بالإضافة، لاحظتُ أثناء المراجعة أن `rin_parser.h` يعلن دالة
`objectStyleFieldDeclaration()` (لكلمات `txt`/`img`/`object.file`/`Fonts`/
`background`/`css3` كبديل لـ `text` داخل `@Object`) لكنها **غير مُنفَّذة ولا
مُستدعاة فعلياً** في `rin_parser.cpp` حالياً — أي أن استخدام `txt`/`Fonts`/
`background` كما ورد في `tools/test_object_style.cpp` سيفشل في التحليل
النحوي حالياً (ثغرة موجودة مسبقاً، غير متعلقة بتعديلات AUKT). لذلك اعتمدتُ
عمداً على `text` العادية (المُختبرة والفعّالة) في كل حقول الخطوط/الأيقونات/
شريط الأدوات أعلاه وفي الملف النموذجي، بدل تلك الكلمات غير المفعَّلة.

## 6) أفكار لتطوير مفهوم الجداول لاحقاً

- ربط `@table` داخل AUKT مباشرة بمحرّك العرض الحي `Loomtime`
  (`@view.Column` / `@view.Text` ...) لعرض الجدول كواجهة حقيقية قابلة
  للتفاعل، لا نصاً فقط.
- دعم فرز/تصفية الصفوف كعبارات مضمَّنة (`sort by=...;` / `filter where=...;`)
  بدل تركها كلياً لواجهة التطبيق.
- السماح بربط `@sticker=icons` بمكتبة أيقونات فعلية (assets) بدل أسماء ملفات
  نصية فقط، عبر منتقي أيقونات في المحرِّر.
