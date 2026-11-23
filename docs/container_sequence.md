# حاوية تُفهرَس وتُعَدّ وتُكرَّر — `on index / store / count / each`

> الخطوة 4 من خارطة الحاويات (بعد Services وAutoload والخُطّافات السحرية). المحرّك: `rin_container_magic.cpp` + نقاط الاعتراض في المفسّر.
> إضافة بحتة: لا يتغيّر أي كود قديم، والخُطّاف غير المعرَّف يترك السلوك الأصلي كما هو.

الحاوية نفسها (الاسم المجرّد `Shelf`) تستطيع أن تتصرف كمجموعة:

| الصيغة | الخُطّاف | يعيد |
|---|---|---|
| `Shelf[key]` | `on index(key)` | قيمة القراءة |
| `Shelf[key] = v` | `on store(key, value)` | (يُتجاهَل؛ التعبير يُرجِع `v`) |
| `len(Shelf)` | `on count()` | العدد |
| `contains(Shelf, key)` | `on has(key)` | يُحوَّل إلى منطقي |
| `for x in Shelf` | `on each()` | مصفوفة أو قاموس أو Set — يُكرَّر بقواعد `for` المعتادة |

```rin
@container=Shelf
    state books = [];
    on index(key)         { return books[key]; }
    on store(key, value)  { books[key] = value; }
    on count()            { return len(books); }
    on each()             { return books; }
    on has(title)         { return contains(books, title); }
.end/container

Shelf[0] = "Dune";
Shelf[1] = "Emma";
print Shelf[1], len(Shelf);          // Emma 2
for (let b in Shelf) { print b; }    // Dune ، Emma
```

## قواعد
- **`each()` قاموس:** `for (let k in C)` يمرّ على المفاتيح، و`for (let [k, v] in C)` على أزواج `[مفتاح، قيمة]` — كالقواميس العادية تماماً.
- **المتغيّر الحقيقي يغلب:** `let Shelf = [...]` يُخفي الحاوية بنفس الاسم كما في بقية اللغة.
- **التحميل الكسول:** أول إشارة `Shelf[0]` تحمّل الحاوية عبر `Autoload` إن كان مفعّلاً (الخطوة 2).
- **حارس إعادة الدخول:** لمس الحاوية بنفس الصيغة داخل خُطّافها (مثلاً `Shelf[k]` داخل `on index`) يسقط للسلوك العادي بدل دوران لانهائي.
- **يتجاور مع `on get/set/call/invoke`:** `Mix["k"]` للفهرس و`Mix.name` للحقول الديناميكية — صيغتان مستقلتان.
- حاوية بلا `on index/…` تتصرف كما كانت تماماً (خطأ المتغيّر غير المعرَّف).

## حدود صريحة
- يعمل في المفسّر الشجري (الافتراضي). مسار `--vm` (RinVM) لا يعرف الاسم المجرّد للحاوية كقيمة بعد، فلا تُستعمل هذه الصيغ معه.
- `len(...)` و`contains(...)` تُلتقطان حين تكون الحاوية **الوسيط الأول مباشرة** (لا عبر متغيّر وسيط).

الاختبار: `tests/print/container_sequence.rin`.
