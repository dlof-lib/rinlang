# جبر النيّة (Intent algebra) — إضافة وحذف وإخفاء وحساب على `extras`

> كل مثال هنا جُرِّب فعلياً (انظر `tests/print/intent_algebra.rin` و`guard_pause.rin`).

النيّة (`Intent.core`) تحمل `extras`. هذه الدوال تعدّلها **بلا تغيير الأصل**: كل واحدة تعيد **نيّة جديدة**، فتُسلسَل بحرية ثم تُرسَل أو تُؤكَّد ([`guarded_actions.md`](./guarded_actions.md)).

```kotlin
let i = Intent.core("cart", "buy", {"price": 10, "qty": 2});
let j = Intent.mul(Intent.sum(i, "qty", 3), "price", 1.5);      // qty=5, price=15   (i كما هي)
Intent.send(Intent.hide(Intent.add(j, "pin", 1234), "pin"));    // pin يبقى في النيّة لكنه لا يُمرَّر
```

## إضافة / حذف
| الدالة | المعنى |
|---|---|
| `Intent.add(i, k, v)` | يضيف مفتاحاً **جديداً**؛ إن وُجد ← E0007 (استعمل `Intent.with` للكتابة فوقه) |
| `Intent.remove(i, k)` · `Intent.delete(i, k)` | يحذف المفتاح (E0007 إن لم يوجد) |

## إخفاء / إظهار
المخفيّ **يبقى في النيّة** (`Intent.extra` تقرؤه صراحةً) لكنه **لا يُمرَّر للإجراء** ولا يدخل `pending.data` (فلا تكشفه رسالة الحوار) ولا `Intent.total`.
| الدالة | المعنى |
|---|---|
| `Intent.hide(i, k)` | يخفي المفتاح (E0007 إن لم يوجد) |
| `Intent.unhide(i [, k])` · `Intent.reveal(i [, k])` | يُظهر المفتاح، وبلا مفتاح يُظهر الكل |
| `Intent.hidden(i)` | أسماء المخفيّ |

> `show` كلمة محجوزة في Rin ولا تصحّ بعد النقطة، لذلك الإظهار `unhide`/`reveal`.

## الحساب
`Intent.OP(i, key, n [, default])` ← القيمة الحالية **OP** `n`. المفتاح المفقود يحتاج `default` وإلا E0007؛ قيمة غير رقمية E0004.
| الدالة | الناتج |
|---|---|
| `sum` · `sub` · `mul` | `x+n` · `x-n` · `x*n` |
| `div` · `mod` | `x/n` · باقي القسمة — القسمة على صفر ← E0035 |
| `pow` | `x^n` |
| `min` · `max` | الأصغر/الأكبر بين `x` و`n` |
| `Intent.clamp(i, k, lo, hi [, default])` | يقيّد القيمة في `[lo, hi]` (`lo > hi` ← E0007) |
| `Intent.total(i [, keys])` | **مجموع** extras الرقمية الظاهرة (أو المفاتيح المسمّاة فقط) — يعيد رقماً لا نيّة |

## انظر أيضاً
[`guarded_actions.md`](./guarded_actions.md) · [`intent_navigation.md`](./intent_navigation.md)
