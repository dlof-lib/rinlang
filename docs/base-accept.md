# base-accept.md — `base` و`accept`: متغيّران يخدمان `if`

> كل مثال هنا جُرِّب فعليًا عبر المفسِّر (انظر `tests/verification/base_*.rin` و`accept_basic.rin` و`examples/base_accept_demo.rin`).

`if` يسأل سؤالًا: *«هل هذا صحيح؟»*. لكن السؤال يحتاج غالبًا إلى **مرجع** يُقاس منه، وإلى **حارس** يمنع القيم الفاسدة قبل أن تصل إلى الشرط أصلًا.
هذان هما النوعان الجديدان. كلاهما من عائلة المتغيرات الحيّة ([`living-variables.md`](./living-variables.md)): **كلمتان سياقيتان غير محجوزتان**،
و`let base = 5;` و`fun inc(x){...}` ما زالا صالحين تمامًا. برنامج لا يستعملهما لا يدفع أي كلفة.

## الطبقات

```
let     قيمة فقط                       «ما هو الآن؟»
 └─ if      قرار                        «هل الشرط صحيح؟»
     └─ base    مرجع للقرار             «صحيح بالنسبة إلى ماذا؟»   x.up  x.delta  x.reached ...
     └─ accept  حارس للقرار             «هل تُقبل هذه القيمة أصلًا؟» x.ok  x.refused ...
```

| الصيغة | الفكرة |
|---|---|
| `base x = v [to GOAL] [step N];` | يتذكّر قيمته الأولى (**خط الأساس**) ويقيس البعد عنها، وله هدف اختياري |
| `accept x = v if RULE [else FALLBACK] [strict];` | لا تدخله إلا القيم التي يقبلها الشرط |
| `inc x [by n];` · `dec x [by n];` | زيادة/إنقاص (تعمل على أي متغيّر رقمي، وتمرّ من كل حرّاسه) |
| `rebase x;` · `rewind x;` | القيمة الحالية تصير خط الأساس · الرجوع إلى خط الأساس |

## `base` — خط أساس

```rin
base temp = 20 to 30 step 2;     // خط الأساس 20، الهدف 30، خطوة inc/dec الافتراضية 2
inc temp;                        // 22   (الخطوة الافتراضية)
inc temp by 3;                   // 25
print temp.delta;                // 5    (الآن − الأساس)
if (temp.up) { print "warmer than base"; }
print temp.progress;             // 0.5  (نصف الطريق نحو الهدف)
print temp.reached;              // false
inc temp 5;  print temp.reached; // true (30)
rewind temp; print temp.same;    // true (رجعنا إلى 20)
```

خصائص `base` (للقراءة فقط):

| الخاصية | المعنى |
|---|---|
| `x.base` | خط الأساس |
| `x.same` / `x.changed` | هل تساوي القيمةُ خطَّ الأساس؟ (تصلح لأي نوع قيمة) |
| `x.up` / `x.down` / `x.delta` | فوق/تحت/فرق الأساس (أرقام فقط وإلا E0050) |
| `x.goal` | الهدف (`nil` إن لم يُحدَّد) |
| `x.reached` | هل بلغنا الهدف؟ **الاتجاه يحدّده خط الأساس**: `base d = 100 to 0` يبلغ الهدف حين `d <= 0` |
| `x.togo` / `x.progress` | المتبقي إلى الهدف / نسبة التقدّم (يتطلّبان `to GOAL`) |
| `x.step` | الخطوة الافتراضية |

`rebase x;` تجعل القيمة الحالية خط أساس جديدًا (فيصير `x.delta` صفرًا). و`rewind x;` تعيد القيمة إلى خط الأساس عبر الإسناد العادي،
فتمرّ من أي `bell` أو `trial` أو حارس.

## `accept` — حارس قبول

```rin
accept age = 20 if age >= 0 and age < 150;
age = 30;   print age.ok;        // true
age = 500;                       // مرفوضة: تُتجاهل بصمت
print age;                       // 30
print age.ok;                    // false
print age.refused;               // 500
if (!age.ok) { print "rejected"; }
```

داخل الشرط **اسم المتغيّر نفسه يعني القيمة المرشّحة** (لا القيمة الحالية)، ويُقيَّم الشرط في النطاق الذي صُرِّح فيه المتغيّر،
فيرى أي متغيرات أخرى هناك. القيمة الابتدائية تخضع للشرط نفسه.

ثلاث سياسات للرفض:

| السياسة | الصيغة | ما يحدث عند الرفض |
|---|---|---|
| صامتة (الافتراضي) | `accept x = 1 if x > 0;` | تبقى القيمة القديمة، و`x.ok` تصير `false` |
| بديل | `accept x = 1 if x > 0 else 100;` | يحلّ البديل محلّ القيمة المرفوضة (ويجب أن يمرّ من الشرط نفسه) |
| صارمة | `accept x = 1 if x > 0 strict;` | خطأ `E0049` |

`else` و`strict` لا يجتمعان. خصائص `accept`: `x.ok` (هل قُبل آخر إسناد؟) · `x.accepted` · `x.rejected` (عدّادان) · `x.refused` (آخر قيمة رُفضت).

## يعملان مع بقية اللغة

- `inc`/`dec` تمرّان من الحرّاس: `inc level;` على `accept level = 3 if level <= 3` يُرفض بصمت (أو `E0049` إن كان `strict`).
- `bell x { ... }` يرنّ فقط عند **القبول** الفعلي؛ الإسناد المرفوض لا يُرنّ ولا يكتب.
- `trial { ... abort; }` يعيد قيمة `accept` وعدّاداته إلى حالها.
- النوعان محليّان للنطاق: `accept` داخل دالة يحرس متغيّرها المحلي فقط.

## مثال: ترموستات

```rin
accept reading = 21 if reading > -50 and reading < 60;   // حارس المدخلات
base target = 21 to 25 step 1;                            // مرجع الهدف
let log = [];
for (let i = 0; i < 6; i = i + 1) {
  inc target;
  if (target.reached) { push(log, "reached"); }
  else if (target.up) { push(log, "heating"); }
  else { push(log, "idle"); }
}
print log;     // ["heating", "heating", "heating", "reached", "reached", "reached"]
reading = 99;  // مرفوضة (فوق 60)
if (reading.ok) { print "good"; } else { print "bad reading kept at " + str(reading); }
```

## الأخطاء

| الكود | متى |
|---|---|
| `E0049` | رفض في `strict`، أو قيمة ابتدائية مرفوضة بلا `else`، أو البديل مرفوض أيضًا |
| `E0050` | `inc`/`dec` على غير رقم · `rebase`/`rewind` على غير `base` · `step` = 0 · `delta`/`up`/`down` على غير رقم |
| `E0046` | خاصية لا تخص هذا النوع (مثل `x.up` على متغيّر عادي) |
| `E0012` | `accept` بلا `if` · `E0013` `strict` مع `else`، أو مُعدِّل في غير مكانه |

## حدود معروفة
مترجم `rinc` (Rin → C) لا يدعم هذه الصيغ بعد (مثل بقية المتغيرات الحيّة) · الشرط يُقيَّم عند كل إسناد (كلفته كلفة تعبيره) ·
لا اختبار «هل كانت ستُقبل؟» دون إسناد بعد.

## انظر أيضًا
[`living-variables.md`](./living-variables.md) · [`variables.md`](./variables.md) · [`control-flow.md`](./control-flow.md) · [`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md)
