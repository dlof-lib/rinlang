# link و import — الربط الحيّ والوحدات

هذا الملف يصف تطوير مفهومَي **`link`** و**`import`**. كل الأمثلة مُتحقَّق منها فعلياً
(`tests/modules/`، شغّلها بـ `tests/modules/run.sh`: 11 اختبار سلوك + 10 اختبارات حالات خطأ).

## مرجع سريع
| الميزة | الصيغة |
|---|---|
| ربط حيّ | `link to=X;` · `link id="..";` |
| قراءة فقط | `link to=X readonly;` |
| فلاتر | `only=a,b` · `except=a,b` |
| ربط ملف | `link file="./f.rin" as=ns;` |
| فكّ / استعلام | `unlink to=X;` · `links("Name")` |
| استيراد | `import { a as b } from "p";` · `import * as ns from "p";` |
| إعادة تصدير | `export { a as b } from "p";` · `export * from "p";` |

رموز الأخطاء: `E0014` حاوية غير موجودة/ربط ذاتي أو دائري · `E0016` خيارات متعارضة · `E0028` اسم غير موجود/غير مُصدَّر · `E0029` وحدة غير موجودة · `E0035` كتابة عبر ربط قرائي.

## 1) `link` — ربط حيّ بلا نسخ

قبل التطوير كان `link to=X;` يتحقق من وجود الهدف ويطبع رسالة فقط. الآن هو **ربط حقيقي**:
داخل حاوية، تصبح أسماء الهدف (متغيرات ودوال) مرئية **قراءةً وكتابةً** في الحاوية الحالية
مباشرةً من مصدرها.

| العبارة | الدلالة |
|---|---|
| `link to=X;` / `link id="id";` | **حيّ**: لا نسخ، التعديل في المصدر يظهر فوراً عند الجميع |
| `merge with=X;` / `tying with=X;` | **نسخ لحظي** لقيم X وقت التنفيذ فقط |

```kotlin
@container=Config
    let theme = "dark";
    let size = 10;
.end/container

@container=Ui
    link to=Config;
    let size = 99;       // الاسم المحلي يحجب المرتبط
    print theme;         // dark  (قراءة حيّة من Config)
    theme = "light";     // الكتابة تصل إلى Config نفسها
.end/container

@container=Probe
    link to=Config;
    print theme;         // light
.end/container

Config.size = 500;
print Probe.size;        // 500 — الوصول المؤهَّل يرى الأسماء المرتبطة أيضاً
```

**قواعد الحلّ:** المحلي ← الحاويات المرتبطة (بترتيب `link`) ← النطاق الأب.
**الهدف** حاوية أو `Containers.Group` (تُربط كل حاوياتها الأعضاء) أو معرّف `link.id`.

### أنماط الربط والفكّ والاستعلام
| الصيغة | الأثر |
|---|---|
| `link to=X;` | ربط حيّ قراءةً وكتابةً |
| `link to=X readonly;` | ربط حيّ **قراءة فقط**؛ الكتابة (`a = 1` أو `Cur.a = 1`) تُرفض بخطأ واضح |
| `unlink to=X;` / `unlink id="..";` | يفكّ الربط عن الحاوية الحالية (يعمل مع المجموعات أيضاً) |
| `links("Name")` | يُرجع `[{name, readonly}]` — الحاويات المرتبطة بـ `Name` بترتيب الربط |

**فلاتر الأسماء** (تُجمع مع `readonly` بأي ترتيب):
| الصيغة | الأثر |
|---|---|
| `link to=X only=a,b;` | قائمة سماح: يُكشف `a` و`b` فقط من X |
| `link to=X except=a,b;` | قائمة منع: يُكشف كل شيء عدا `a` و`b` |

لا يمكن الجمع بين `only` و`except` في ربط واحد. الأسماء غير المكشوفة تُعامل كأنها غير موجودة
(قراءةً وكتابةً ووصولاً مؤهَّلاً). `links("Name")` يُظهر الفلتر في حقل `only`/`except`.
إعادة `link` للهدف نفسه بلا فلتر ترفع الفلتر السابق.

إعادة `link` للهدف نفسه بنمط مختلف تنقله بين rw وreadonly بدل التكرار.
```kotlin
@container=Viewer
    link to=Base readonly;
    print links("Viewer");   // [{"name": "Base", "readonly": true}]
    unlink to=Base;
.end/container
```

**الحماية:** ربط حاوية بنفسها، أو ربط يُنشئ دورة (A↔B)، يُرفض بخطأ `E0014`.

**حدود معروفة:** المتغيرات الحيّة (`stone/gauge/...`) المعرَّفة في الهدف تعمل عبر الوصول
المؤهَّل (`Config.x`)؛ أما عبر الاسم المجرَّد المرتبط فتُعامَل كقيمة عادية.

## 2) `import` — أشكال الاستيراد

| الصيغة | الأثر |
|---|---|
| `@import "path";` | دمج مباشر في النطاق الحالي (كما كان؛ مع `export` يُدمَج المُصدَّر فقط) |
| `@import "path" as ns;` | نطاق معزول **`ns`**، وصار الآن نطاقاً مسمّى حقيقياً: `ns.fn()` و`ns.x` و`ns.x = v` |
| `import { a, b } from "path";` | انتقائي — يشترط `export` على كل اسم |
| `import { a as x, b } from "path";` | **جديد:** إعادة تسمية محلية لتفادي التعارض |
| `import * as ns from "path";` | **جديد:** مكافئ `@import "path" as ns;` بصياغة وحدات |

```kotlin
// util.rin
export fun double(x) { return x * 2; }
export let VERSION = "1.0";

// main.rin
import { double as dbl, VERSION as V } from "./util.rin";
print dbl(4), V;                         // 8 1.0
import * as util from "./util.rin";
print util.double(10);                   // 20
```

### إعادة التصدير (واجهة تجميعية)
`export { a, b as c } from "path";` يستورد الأسماء من وحدة أخرى ويُصدّرها من الوحدة الحالية
(الاسم الظاهر للمستورِد هو `c`). مفيد لجمع عدة ملفات خلف ملف واجهة واحد؛ وما لم يُعد تصديره
يبقى غير مرئي لمن يستورد من الواجهة.
```kotlin
// api.rin
export { double as twice, VERSION } from "./util.rin";
// main.rin
import { twice, VERSION } from "./api.rin";
```

`export * from "path";` يعيد تصدير **كل** ما تُصدّره تلك الوحدة دفعة واحدة (الخاص منها يبقى خاصاً).
```kotlin
// all.rin
export * from "./util.rin";
export fun extra() { return "extra"; }
// main.rin
import { double, triple, extra } from "./all.rin";
```

### المسارات النسبية الصريحة (`./` و`../`)
تُحلّ الآن **نسبةً إلى مجلد الملف المستورِد**، وبشكل متداخل: ملف مستورَد يستورد `./x.rin`
يجده بجانبه هو لا بجانب الملف الرئيسي. وتعمل من أي مجلد تشغيل. للتوافق، إن لم يوجد الملف هناك
يُجرَّب مجلد العمل كما كان. المسارات التي تحتوي `/` بلا `./` تبقى نسبة لمجلد العمل، والأسماء
العارية (`"math"`) تبقى مكتبات `lib/*.og.rin`.

## 3) ربط الملفات — `link file=`
`link file="path" [as=ns] [readonly] [only=..|except=..];` يحمّل ملفاً كاملاً كنطاق معزول (مثل
`@import "path" as ns`) ثم يربطه **حياً** بالحاوية الحالية في خطوة واحدة.
```kotlin
// sub/settings.rin:  let theme = "dark"; let size = 12; fun greet() {...}
@container=Ui
    link file="./sub/settings.rin" as=cfg only=theme,greet;
    print theme, greet();   // dark hi dark
    theme = "light";        // يكتب في ملف settings نفسه
.end/container
@container=Other
    link file="./sub/settings.rin" as=cfg readonly;   // نفس الملف = حالة مشتركة
    print theme;            // light
.end/container
print cfg.theme;            // وصول مؤهَّل من أي مكان
```
- الملف يُحمَّل **مرة واحدة** لكل `as`؛ كل ربط لاحق بنفس الاسم يشارك نفس الحالة.
- بلا `as=` يُشتقّ الاسم من اسم الملف (`settings.rin` ← `settings`)؛ استخدم `as=` عند تشابه أسماء ملفات في مجلدات مختلفة.
- على مستوى الملف (خارج أي حاوية) يعمل كاستيراد باسم مستعار فقط.
- المسارات `./` و`../` تُحلّ نسبةً إلى الملف الحالي.

## 4) `link` مع `import`
الاستيراد باسم مستعار ينتج نطاقاً، ويمكن ربطه حياً:
```kotlin
@import "lib/settings.og.rin" as settings;
@container=App
    link to=settings;     // أسماء settings متاحة في App بلا نسخ
.end/container
```
