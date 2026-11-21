# `from` — مصدر واحد، وأسماء تُسحب منه

`from` مع `let` تقرأ كجملة: «ليكن `name` و`age` **من** `user`». المصدر إما **ملف** (استيراد) أو **قيمة** (تفكيك)،
وتتحوّل داخلياً إلى عقد موجودة (`ImportSelected`/`Import`/`LetPattern`) فلا يتغيّر المُفسِّر ولا VM ولا rinc.

| الصيغة | المصدر | المعنى |
|---|---|---|
| `let a, b as c from "p";` | ملف | استيراد أسماء مُصدَّرة (مع إعادة تسمية) |
| `let * from "p";` | ملف | دمج كل المُصدَّر في النطاق الحالي |
| `let * as ns from "p";` | ملف | نطاق مسمّى `ns` |
| `let name, age from user;` | قيمة (قاموس/كائن) | سحب مفاتيح بأسمائها |
| `let [x, y] from pair;` | قيمة (مصفوفة) | سحب بالموضع |

القاعدة الوحيدة للتمييز: **نص حرفي ثم `;`** بعد `from` = مسار ملف. أي تعبير آخر (متغيّر، نداء، قاموس...) = قيمة.
مسار محفوظ في متغيّر لا يُستورَد بهذه الصيغة (المسار ثابت)، بل يُعامَل كقيمة.

## 1) من ملف
```kotlin
let double, VERSION as V from "./util.rin";
let { double, triple } from "./util.rin";     // الأقواس اختيارية
let * as util from "./util.rin";
```
الأسماء يجب أن تكون `export`، وإلا خطأ واضح. الافتراضيات و`...rest` والأنماط المتداخلة **لا** تُقبل مع الملفات (E0013).

## 2) من قيمة (قاموس/كائن)
```kotlin
let user = {"name": "Sara", "age": 30, "city": "Riyadh"};
let name, age from user;                        // name = "Sara", age = 30
let city as town, role = "guest" from user;     // as = إعادة تسمية، = افتراضي عند الغياب أو nil
let name as nm, ...others from user;            // ...others = باقي المفاتيح
let "my key" as k from user;                    // مفتاح نصي يحتاج as
let profile: {lang, tz = "UTC"} from data;      // تعشيش بنمط key: {...}
let x, y from Pt();                             // حقول كائن (بلا ...rest)
```
تعمل **داخل الدوال والكتل** كأي `let`. نفس سلوك `let {…} = m` تماماً (docs/let-plus.md): افتراضي يُقيَّم عند الحاجة،
ويرى ما رُبط قبله، وغير المطابق للشكل يرمي E0004.

## 3) من قيمة (مصفوفة)
```kotlin
let [a, b = 5, ...rest] from [1, nil, 3, 4];    // a=1  b=5  rest=[3,4]
```

## 4) الحارس والقيمة البديلة
```kotlin
fun f(m) {
    let id, tag = "-" from m else { return "bad shape"; };   // كتلة else يجب أن تغادر النطاق (E0048 وإلا)
    return id + tag;
}
let c from {"c": "red"} else "blue";           // else <قيمة>: بديل إن كان المصدر nil
```

## أسلوب الكتابة
```kotlin
let name, age from user;                       // سطر واحد
let {name, age} from user;                     // بأقواس
let name as who,                               // عدة أسطر وفاصلة أخيرة
    age = 0,
    ...others
    from user
    else { return; };
```
المسافات وأسطر الفصل حرة. القائمة تنتهي عند `from`، وكل عنصر: `اسم` | `اسم as محلي` | `اسم: نمط` | `اسم = افتراضي`، ثم `...rest` أخيراً.

## 5) تواصل الملفات: مسار ديناميكي، استيراد اختياري، إعادة تصدير، JSON

```kotlin
// مسار يُحسب وقت التشغيل: file(<تعبير>)   ('file' كلمة محجوزة فلا تعارض مع دوال المستخدم)
let dir = "./plugins";
let run from file(dir + "/" + name + ".rin");
let * as plugin from file(dir + "/main.rin");

// ملفات JSON بنفس الصيغة (مفاتيح الجذر)
let host, port from "./config.json";

// استيراد اختياري: إن لم يوجد الملف تُؤخذ القيم من القاموس بعد else
let host, port from "./local.json" else {"host": "localhost", "port": 8080};

// إعادة تصدير (واجهة تجميعية): تكافئ export { } from و export * from
export let double as twice, VERSION from "./util.rin";
export let * from "./deep.rin";
```
- `else {…}` مع مصدر ملف **قيمة** (قاموس يُعطي قيمة لكل اسم)، لا كتلة. يُفعَّل **فقط** عند غياب الملف (`E0029`)؛
  خطأ داخل الوحدة أو اسم غير مُصدَّر يبقى خطأً كما هو. مفتاح ناقص في البديل ← `E0028`، وبديل ليس قاموساً ← `E0028`.
- `else` لا يُقبل مع `*` (لا أسماء تُملأ) ← `E0013`. و`export let * as ns from` غير مدعوم ← `E0013`.
- `file(...)` يعمل مع القائمة المحددة ومع `*`؛ الأخطاء نفسها (E0029 لملف غائب).
- الفرق عن المصدر **قيمة**: `let a from cfg;` تفكّك متغيّراً، أما `from "p"` أو `from file(..)` فيستورد وحدة.

## قواعد وأخطاء
- `from` كلمة **سياقية**: لا تُعدّ مفهوماً إلا على المستوى الأعلى بعد نهاية عنصر وقبل أول `;`. لذلك تبقى
  `let from = 5;` و`let x = from;` و`let x = f(from);` و`each x in xs from 1` و`Set.from(...)` كما هي.
- `let a, b from;` (لا مصدر) · `let * from user;` (علامة `*` تحتاج مسار ملف) · `let [a] from "p";` · `let a = 1 from "p";` (نمط/افتراضي/`...rest` مع مسار ملف) ← E0013.
- نسيان `;` بعد المصدر ← E0012 · اسم مكرر `let a, a from u;` ← E0002.
- `let a, b;` (بلا `from`) تبقى تصريحين عاديين كما كانت.
- النسخة الأولى (`let a, b from "p";` للملفات) تعمل كما هي دون أي تغيير.
