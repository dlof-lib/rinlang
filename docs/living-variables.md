# living-variables.md — المتغيرات الحيّة (Living Variables)

> كل مثال هنا جُرِّب فعليًا عبر المفسِّر (انظر `tests/verification/living_*.rin` و`examples/living_variables_demo.rin`).

`let` متغيّر «ميت»: يخزّن قيمة فقط. المتغيرات الحيّة تضيف **سلوكًا** إلى الاسم نفسه، بكلمة واحدة لكل سلوك.
كلها **كلمات سياقية غير محجوزة**: `let tape = 5;` و`fun lens(x){...}` ما زالا صالحين تمامًا.
برنامج لا يستعملها لا يدفع أي كلفة (الكود كله خلف راية لا تُفعَّل إلا عند أول استعمال).

## الخريطة (من الأسهل إلى الأعمق)

| المستوى | الصيغة | الفكرة |
|---|---|---|
| سهل | `stone x = v;` | ثابت |
| سهل | `gauge x = v within A to B [strict\|wrap] [keep N];` | مقياس محصور في مدى |
| متوسط | `tape x = v [keep N];` | يتذكّر: `undo` / `redo` |
| متوسط | `lens x = expr;` | مشتق يُحسب عند كل قراءة |
| متوسط | `fuse x = v [burns N];` | يحترق (`nil`) بعد N قراءة |
| عميق | `bell x [(old, new)] { ... }` | يرنّ عند كل تغيّر فعلي |
| عميق | `trial { ... } [else { ... }]` | معاملة: تراجع تلقائي |

الأنواع **تتركّب**: `gauge hp = 50 within 0 to 100 keep 5;` مقياس بذاكرة، و`bell` يعمل على أي متغيّر حتى `let`.

## `stone` — ثابت
```rin
stone PI = 3.14159;
PI = 3;                    // error[E0043]
stone cfg = {"mode": "dark"};
cfg["mode"] = "light";     // error[E0043] — يحمي الربط والكتابة المباشرة في المحتوى
```
> لا يمنع دوال تعدّل في المكان مثل `push()`.

## `gauge` — مقياس
```rin
gauge hp = 80 within 0 to 100;      // clamp (الافتراضي)
hp = hp + 50;  print hp;            // 100
gauge ammo = 5 within 0 to 10 strict;
ammo = 11;                          // error[E0044]
gauge angle = 350 within 0 to 360 wrap;
angle = angle + 30; print angle;    // 20   (النطاق نصف مفتوح [lo, hi))
```
يقبل أرقامًا فقط. خصائص: `x.min x.max x.mode x.fill` (0..1) `x.full x.empty`.

## `tape` — شريط ذاكرة
```rin
tape score = 0 keep 3;          // الافتراضي keep 10
score = 10; score = 20; score = 30; score = 40;
print score.past;               // [10, 20, 30]
undo score;      print score;   // 30
undo score 2;    print score;   // 10
redo score;      print score;   // 20
```
- إسناد قيمة **مختلفة** فقط يُسجَّل؛ كتابة جديدة تقطع مسار `redo`.
- خصائص: `x.past x.undone x.canUndo x.canRedo x.keep`.
- `undo`/`redo` بلا شيء متاح: لا أثر (اسأل `canUndo` قبلها).

## `lens` — مشتق
```rin
let price = 10; let qty = 3;
lens total = price * qty;
qty = 5; print total;           // 50
total = 1;                      // error[E0045] للقراءة فقط
lens a = b + 1; lens b = a + 1; print a;  // error[E0045]: يعتمد على نفسه
```
يُحسب من جديد عند كل قراءة داخل النطاق الذي صُرِّح فيه (بلا تخزين مؤقت).

## `fuse` — فتيل
```rin
fuse otp = "4821" burns 2;
print otp; print otp; print otp;   // 4821  4821  nil
rearm otp;                          // يعود للقيمة المسلَّحة
otp = "9999";                       // إسناد جديد يعيد التسليح
```
خصائص: `x.left x.burnt`. الافتراضي `burns 1`.

## `bell` — جرس
```rin
let hp = 100;
bell hp (old, new) { print old + " -> " + new; }
hp = 90; hp = 90;       // يرنّ مرة واحدة (التغيّر الفعلي فقط)
unbell hp;              // يزيل كل الأجراس
```
وسائط الجرس: بلا وسائط، أو `(new)`، أو `(old, new)`. يرنّ أيضًا بعد `undo`/`redo`. لا يُوضع على `lens`.
جرس يعيد تفعيل نفسه بلا نهاية يوقفه `E0047` عند عمق 16.

## `trial` — تجربة
```rin
let a = 100; let b = 0;
trial { a = a - 30; b = b + 30; abort; }   // يتراجع كل شيء
trial { a = 0; fail(); } else { print "rolled back"; }
```
عند `abort;` أو خطأ أو `throw` يُعاد كل متغيّر مُسنَد داخلها (حتى حالة `tape` و`gauge` و`fuse`)، ثم يُنفَّذ `else` إن وُجد
(وبدونه يُعاد رمي الخطأ). تعمل متداخلة. `return`/`break`/`continue` خروج عادي **يُثبّت** التغييرات.
> تتراجع عن **إسنادات المتغيرات فقط** — لا عن `push()` ولا `print` ولا الملفات.

## خصائص عامة لأي متغيّر حيّ
`x.kind` (`stone|gauge|tape|lens|fuse|plain`) · `x.reads` · `x.writes` · `x.bells` · `x.vitals` (خريطة بكل شيء).
حقل بيانات حقيقي بنفس الاسم (مفتاح `kind` في قاموس مثلًا) يغلب الخاصية.

## الأخطاء
| الكود | المعنى |
|---|---|
| E0043 | تعديل `stone` |
| E0044 | خارج مدى `strict` / غير رقم في `gauge` / مدى فارغ |
| E0045 | إسناد إلى `lens` أو اعتماده على نفسه |
| E0046 | استعمال خاطئ: `undo` بلا `tape`، `abort` خارج `trial`، `bell` على `lens`، عدّاد غير صالح |
| E0047 | جرس لا نهائي |

## حدود معروفة
`lens` بلا تخزين مؤقت · `trial` للمتغيرات فقط · مترجم `rinc` (Rin → C) لا يدعم هذه الصيغ بعد · لم تُختبر مع `export`.

## انظر أيضًا
[`variables.md`](./variables.md) · [`errors.md`](./errors.md) · [`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) · `examples/living_variables_demo.rin`
