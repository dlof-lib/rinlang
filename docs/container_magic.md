# Magic hooks — خُطّافات الأعضاء الغائبة

> الخطوة 3 من خارطة الحاويات (بعد Services وAutoload). المحرّك: `app/src/main/cpp/rin_container_magic.cpp`.
> إضافة بحتة: العضو **الموجود** يغلب الخُطّاف دائماً، وحاوية بلا خُطّافات تتصرّف كما كانت.

داخل أي `@container` تُعرَّف بنفس شكل `on init/update/...`:

| الخُطّاف | يُستدعى عند | يعيد |
|---|---|---|
| `on get(name)` | قراءة `C.name` لحقل غير معرَّف | قيمة القراءة |
| `on set(name, value)` | كتابة `C.name = v` لحقل غير معرَّف | (يُتجاهَل) |
| `on call(name, args)` | نداء `C.name(...)` لدالة غير معرَّفة (`args` مصفوفة) | نتيجة النداء |
| `on invoke(args)` | نداء الحاوية نفسها `C(1, 2)` | نتيجة النداء |
| `on has(name)` | `container.has("C", name)` لحقل غير معرَّف | يُحوَّل إلى منطقي |
| `on drop(name)` | `container.deleteField("C", name)` لحقل غير معرَّف | (يعيد `true`) |

```rin
@container=Bag
    state items = {};
    on get(name)        { return items[name]; }
    on set(name, value) { items[name] = value; }
    on call(name, args) { return name + "/" + len(args); }
.end/container
Bag.color = "red";      // on set
print Bag.color;        // on get  ← "red"
print Bag.anything(1);  // on call ← "anything/1"
```

## قواعد
- **حارس إعادة الدخول:** داخل الخُطّاف، لمس نفس (حاوية، خُطّاف، اسم) مرة أخرى يعطي الخطأ العادي بدل دوران لانهائي.
- الأخطاء داخل الخُطّاف تصعد كما هي (لا تُبتلع).
- تعمل في المفسّر الشجري وRinVM معاً (الاعتراض في `nsGet/nsSet/nsCall/invokeCallee`).
- لا كلفة قبل تعريف أول خُطّاف (علم `magicActive_`).
- `on get/set/...` كلمات سياقية داخل `on`؛ لا تتعارض مع أسماء المتغيّرات.

الاختبار: `tests/print/container_magic.rin`.
