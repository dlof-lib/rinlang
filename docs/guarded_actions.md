# حرّاس التأكيد (Guarded Actions) — `dialog` ✕ `Intent` / `Navigation`

> كل مثال هنا جُرِّب فعلياً عبر المفسِّر (انظر `tests/print/guard_intent.rin` · `guard_nav.rin` و`examples/guarded_actions_demo.rin`).

كان لدينا مفهومان منفصلان: **النيّة/التنقّل** («نفّذ هذا الإجراء»، «انتقل إلى هناك») و**الحوار الحيّ** (`dialog`: رسالة تحسب نفسها ونتيجة تختارها المستخدم). دمجهما يعطي ما تحتاجه أي تطبيق احترافي: **لا يُنفَّذ الإجراء الخطِر ولا يُغادَر ما فيه تغييرات غير محفوظة قبل أن يقرّر المستخدم** — بسطر واحد، ومن دون تغيير أي كود موجود.

لا كلمات جديدة في اللغة: دوال أصلية فقط (`rin_intent_nav.cpp`)، فكل برنامج قديم يعمل كما كان.

```kotlin
@shop.container
    let stock = 10;
    let pending = nil;                       // اختياري: يحمل وصف ما ينتظر التأكيد
    dialog sure = "بيع " + (pending == nil ? "؟" : pending["data"]["n"]) + " قطعة؟ (المخزون " + stock + ")"
        title "تأكيد البيع" tone "ask" buttons ["بيع", "إلغاء"] focus 2;
    fun sell(extras) { stock = stock - extras["n"]; return stock; }
.end;

Intent.guard("shop", "sell", "sure");                                 // كل sell يمرّ بالحوار
let r = Intent.send(Intent.core("shop", "sell", {"n": 3}));
// المستخدم اختار «بيع»  =>  r = {ok: true, container, action, result: 7, confirmed: true}
// المستخدم اختار «إلغاء» =>  r = {ok: false, blocked: true, kind, container, target, choice: 2, answer: "إلغاء"}
```

## الدوال
| الدالة | المعنى |
|---|---|
| `Intent.guard(c, action, dialog [, opts])` | حارس **دائم**: كل `Intent.send` لهذا الإجراء يسأل أولاً. `action = "*"` = كل إجراءات الحاوية |
| `Intent.confirm(intent, dialog [, opts])` | تأكيد **لمرة واحدة** ثم تنفيذ (بلا تسجيل حارس) |
| `Intent.send(intent, {force: true})` | يتجاوز الحارس (لاستدعاءات النظام لا المستخدم) |
| `Intent.unguard(c [, action])` · `Intent.guards(c)` | إزالة · استعلام (مصفوفة خرائط مع العدّادات) |
| `Navigation.guard(c, dialog [, opts])` | حارس دائم على `re` و`replace` و`back` و`reload` |
| `Navigation.confirm(c, route, dialog [, opts])` | انتقال بتأكيد لمرة واحدة |
| `Navigation.unguard(c)` · `Navigation.guardInfo(c)` | إزالة · استعلام (`nil` إن لم يوجد) |
| `Intent.pause(c [, action])` · `Intent.resume(c [, action])` | **تعليق/استئناف** الحرّاس مؤقتاً (يبقى مسجَّلاً وعدّاداته)؛ تعيد عدد ما تغيّر |
| `Navigation.pause(c)` · `Navigation.resume(c)` | كذلك لحارس التنقّل (`false` إن لم يوجد حارس) |

الحوار **معلن داخل الحاوية نفسها** (`dialog name = ...`)، فتبقى رسالته حيّة تتبع حقولها كما في [`dialog.md`](./dialog.md).

## الخيارات `opts`
| الخيار | المعنى |
|---|---|
| `confirm` | زر التأكيد: رقمه (1..) أو نصه. الافتراضي `1`. **أي اختيار آخر، أو التجاهل = رفض** |
| `when` | اسم حقل أو دالة في الحاوية؛ **لا يُسأل إلا إن كانت قيمته صادقة** (مثل `"dirty"` لتغييرات غير محفوظة) |
| `cooldown` | اسم `timer` في الحاوية: ما دام جارياً يُمنع الإجراء بلا سؤال (`reason: "cooldown"`)، ويُعاد تشغيله كلما سُمح — انظر `TIMER.md` |
| `on` | (للتنقّل) مصفوفة من `"re" "replace" "back" "reload"`؛ الافتراضي الكل |
| `params` | (لـ `Navigation.confirm`) وسائط الانتقال |

خيار مجهول، أو زرّ غير موجود، أو `when` لا يُشير إلى حقل/دالة، أو حوار غير معلن ← خطأ واضح **عند تسجيل الحارس** لا عند وقوع الحدث.

## ما الذي يحدث عند الرفض؟
لا شيء يتغيّر (لا تنفيذ، لا كتابة `route`، لا تعديل للسجل)، والنتيجة:
| العملية | نتيجة الرفض |
|---|---|
| `Intent.send` / `Intent.confirm` | `{ok: false, blocked: true, kind, container, target, choice, answer}` |
| `Navigation.re` / `Navigation.confirm` | الخريطة نفسها |
| `Navigation.back` | `false` |
| `Navigation.replace` / `Navigation.reload` | `nil` |

عند القبول تُضاف `confirmed: true` إلى خريطة النتيجة (وتبقى النتيجة كما هي تماماً حين لا يُسأل أحد).

## الحقل الاختياري `pending` — رسالة تعرف ما ينتظر
إن عرّفت الحاوية حقلاً باسم `pending`، كتب الحارس فيه وصف ما ينتظر التأكيد **قبل فتح الحوار** وأعاده `nil` بعده:
`{kind: "intent"|"navigation", op, target, data, dialog}` — حيث `target` اسم الإجراء أو المسار، و`data` هي `extras` أو `params`.
وبما أن رسالة الحوار قيمة حيّة (`val`) فهي تتحدّث بـ `pending` تلقائياً: «بيع 3 قطعة؟»، «المغادرة إلى page3.rin؟».

## سجل المراجعة
كل حارس يعدّ `asked` و`confirmed` و`declined` (تظهر في `Intent.guards(c)` و`Navigation.guardInfo(c)`).

## السلامة (الفشل مغلق)
- **Enter / نهاية الإدخال** يختار زر `focus` في الحوار. لذا ضع `focus` على الزر **الآمن** (الإلغاء) في الحوارات الخطِرة، كما في الأمثلة. التجاهل (إغلاق بلا اختيار) رفض دائماً.
- **لا حلقات:** إن استدعى `reply` الحوار أو دالة محروسة العملية نفسها فلا يُسأل ثانيةً.
- يعمل مع **`when`** لتقليل الإزعاج: اسأل فقط حين يكون هناك ما يستحق السؤال.

## حدود معروفة
- الحوار نصّي في الطرفية (كما في `dialog.md`)؛ ربط `@view.Dialog` يبقى في المضيف.
- الحرّاس مشتركة على مستوى العملية (مفتاحها اسم الحاوية) كسجل التنقّل.
- `Navigation.clear` لا يزيل الحارس؛ استعمل `Navigation.unguard`.
- `rinc` وRinVM لا يدعمانها بعد (كباقي الحيّة).

## الأخطاء
| الكود | المعنى |
|---|---|
| E0006 | الإجراء غير معلن في الحاوية |
| E0007 | خيار مجهول / زرّ غير موجود / `when` أو `on` غير صالح |
| E0014 | حاوية غير موجودة |
| E0046 | الاسم ليس حواراً في هذه الحاوية |
| E0004 | نوع خاطئ (`opts` ليست خريطة...) |

## انظر أيضاً
[`intent_algebra.md`](./intent_algebra.md) · [`dialog.md`](./dialog.md) · [`intent_navigation.md`](./intent_navigation.md) · [`val.md`](./val.md) · `examples/guarded_actions_demo.rin`
