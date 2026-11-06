# Intent.core و Navigation.re — نيّات وتنقّل للحاوية و indsin

مساحتا أسماء دوال (مثل `Env.*`) تعملان على **الحاويات** (`@container`) وتتكاملان مع **indsin**. لا كلمات جديدة في اللغة: دوال أصلية فقط، فلا يتأثر أي برنامج قديم. التنفيذ: `app/src/main/cpp/rin_intent_nav.cpp`.

## Intent — «ماذا أريد من هذه الحاوية؟»
`Intent.core(container, action [, extras])` يبني نيّة: خريطة `{kind:"intent", id, container, action, extras}`.
ملاحظة: `kind` كلمة سياقية، فاقرأها بالفهرسة `i["kind"]` (أما `i.container` و`i.action` فتعملان). `action` اسم دالة معلنة في الحاوية.

| الدالة | المعنى |
|---|---|
| `Intent.core(c, action, extras?)` | بناء النيّة (الحاوية يجب أن تكون موجودة) |
| `Intent.send(i)` | ينفّذ دالة الحاوية؛ تُمرَّر `extras` إن كان للدالة معاملات؛ يعيد `{ok, container, action, result}` |
| `Intent.extra(i, key, default?)` | قراءة extra |
| `Intent.with(i, key, value)` | نسخة جديدة من النيّة بـ extra مضاف/معدَّل (الأصل لا يتغيّر) |
| `Intent.valid(x)` | هل هي نيّة سليمة لحاوية موجودة؟ |
| `Intent.actions(c)` | أسماء دوال الحاوية (مرتّبة) |

```kotlin
@shop.container
    let stock = 10;
    fun sell(extras) { stock = stock - extras["n"]; return stock; }
.end;
Intent.send(Intent.core("shop", "sell", {"n": 3}));   // shop.stock == 7
```
action غير موجود ← خطأ E0006 يعرض الإجراءات المتاحة؛ حاوية غير موجودة ← E0014.

## Navigation — «إلى أين؟»
سجل تنقّل لكل حاوية، مبني على `indsin::NavigationManager` نفسه (`rin_indsin_navigation.h`). كل انتقال يكتب الحقل الحيّ **`route`** في الحاوية، فأي خاصية في `@view` تقرؤه تتحدّث تلقائياً.

| الدالة | المعنى |
|---|---|
| `Navigation.re(c, route, params?)` | انتقال (push). يقصّ فرع «الأمام» كسجل المتصفح. يعيد `{ok, route, index, depth, canBack}` |
| `Navigation.replace(c, route, params?)` | يستبدل المسار الحالي بلا إدخال جديد |
| `Navigation.back(c)` | رجوع خطوة؛ `false` عند بداية السجل (لا خطأ) |
| `Navigation.reload(c)` | يعيد كتابة المسار الحالي؛ `nil` إن لم يُنتقَل بعد |
| `Navigation.current(c)` · `history(c)` · `canBack(c)` · `params(c)` | استعلام |
| `Navigation.clear(c)` | تفريغ السجل |

## حدود معروفة
- `route` يُكتب عبر `setState` فيمرّ بحرّاس الحقل وروابطه؛ حقل `route` المعلن بنوع غير نصي يرفض الكتابة.
- السجل مشترك على مستوى العملية (مفتاحه اسم الحاوية)، وليس لكل مفسّر.
- لا تحميل تلقائي لملف `.rin` للمسار: الاستبدال الفعلي للواجهة يبقى في المضيف (`IndsinPreviewActivity`) كما في `rin_indsin_navigation.h`.
- `rinc` وRinVM لا يدعمانهما بعد.
