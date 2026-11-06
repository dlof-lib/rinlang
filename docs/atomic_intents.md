# النيّات الذرّية (Atomic Intents) — `Intent` ✕ `@tx` ✕ `dialog`

> كل مثال هنا جُرِّب فعلياً (انظر `tests/print/intent_atomic.rin`).

`Intent.atomic` ينفّذ **دفعة نيّات كلها أو لا شيء**: يأخذ لقطة عميقة (مستندات + حقول كل الحاويات المعنيّة، كما في [`@tx`](./merge_concepts.md)) ثم ينفّذ النيّات بالترتيب، وعند أول فشل **يستعيد كل شيء**. وإن أُعطي حوارٌ سُئل المستخدم **مرة واحدة** عن الدفعة كلها ([`guarded_actions.md`](./guarded_actions.md)).

```kotlin
let r = Intent.atomic([
    Intent.core("bank", "take", {"n": 30}),
    Intent.core("bank", "give", {"n": 30}),
], {"dialog": "sure"});
// نجاح : {ok: true, count: 2, results: [70, 80], confirmed: true}
// فشل  : {ok: false, rolledBack: true, failedAt: 1, action: "take", error: "...", results: [85]}
// رفض  : {ok: false, blocked: true, kind: "batch", container, target: "take, give", choice, answer}
```

## `Intent.atomic(list [, opts])`
| الخيار | المعنى |
|---|---|
| `dialog` | حوار معلن في حاوية الدفعة يُسأل **مرة واحدة**؛ رسالته ترى `pending = {kind:"batch", op:"atomic", target:"take, give", data:[extras...]}` إن عرّفت الحاوية الحقل `pending` |
| `confirm` · `when` | كما في الحرّاس (زر التأكيد · شرط السؤال) |
| `container` | حاوية الحوار (الافتراضي: حاوية أول نيّة) |
| `failOnFalse` | إجراء يعيد `false` يُعدّ فشلاً ويُرجِع كل شيء (قاعدة عمل لا خطأ تنفيذ) |
| `throw` | بدل خريطة الفشل: يُعيد رمي الخطأ نفسه **بعد** التراجع |

- **ما يُعدّ فشلاً:** خطأ تنفيذ أو `!?`/`throw` داخل أي إجراء (أو `failOnFalse`). `exit`/`return` لا تُعدّ فشلاً.
- **التحقق المسبق:** كل نيّة تُفحص (نيّة سليمة، حاوية وإجراء موجودان) **قبل** أي سؤال أو لقطة؛ فالخطأ هناك لا يلمس شيئاً.
- **حاويات متعددة:** تُلتقط حقول كل الحاويات المذكورة في النيّات معاً.
- **الحرّاس الفردية:** داخل الدفعة لا تُسأل حرّاس `Intent.guard` لكل نيّة؛ سؤال الدفعة الواحد هو البديل.
- **متداخلة:** تعمل داخل `@tx` أو دفعة أخرى؛ فشل الداخلية يتراجع لنقطتها (انظر `txDepth()`).
- **الأخطاء:** `E0004` (القائمة/النيّة/الخيارات بنوع خاطئ) · `E0006` (إجراء غير معلن) · `E0007` (خيار مجهول) · `E0035` مع `throw` بسبب `failOnFalse`.

## حدود معروفة
- حقول الحاويات الدالّية (`fun`) لا تُلتقط (كما في `@tx`).
- `Navigation` لا يدخل اللقطة (سجل التنقّل خارج الحاوية).
- `rinc` وRinVM لا يدعمانها بعد.

## انظر أيضاً
[`guarded_actions.md`](./guarded_actions.md) · [`intent_algebra.md`](./intent_algebra.md) · [`merge_concepts.md`](./merge_concepts.md)
