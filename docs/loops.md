# حلقات Rin — Loops v2

حلقات Rin القديمة (`while` و`for` و`for..in` و`#do/#while` و`#done`) باقية كما هي. هذا الإصدار يضيف
عائلة حلقات جديدة **تعمل على المفسّر الشجري وRinVM وأندرويد والويب** لأنها تُفكَّك في المحلل النحوي إلى
عُقد موجودة أصلاً (لا تغيير في المفسّر ولا في الآلة الافتراضية).

## `each` — التكرار على أي شيء
```rin
each x in [10, 20, 30] { print x; }                 // عناصر
each ch in "hey" { print ch; }                       // حروف
each x, i in ["a", "b"] { print i + ":" + x; }       // (عنصر، فهرس)
each k, v in {"p": 1, "q": 2} { print k + "=" + v; } // قاموس: (مفتاح، قيمة)
each [a, b] in [[1, 2], [3, 4]] { print a + b; }     // تفكيك
each i in 1 to 10 step 3 { print i; }                // مدى
each n in nums where n % 2 == 0 { print n; }         // مرشّح مدمج
each n in nums reversed { print n; }                 // عكسي (بلا نسخ يدوي)
each n in nums where n > 1 reversed { print n; }     // بأي ترتيب
```
كل تكرار له نطاقه الخاص (الإغلاقات تلتقط قيمة تلك الدورة). `break`/`continue`/`#done { }` تعمل كالمعتاد.

### أنماط التكرار المتقدمة (على `each` و`for` بلا أقواس)
```rin
each c in [1, 2, 3, 4, 5] by 2 { print c; }          // مجموعات: [1,2] [3,4] [5]
each c, n in "abcdef" by 3 { print n + ":" + c; }     // n = رقم المجموعة
each w in [1, 2, 3, 4] window 2 { print w; }          // نوافذ متحركة: [1,2] [2,3] [3,4]
each x, i in letters every 2 { print i + x; }         // كل عنصر ثانٍ (i = الفهرس الحقيقي)
each a, b in xs zip ys { print a + b; }               // تكرار متوازٍ (ينتهي بأقصر قائمة)
each w in xs window 2 where w[0] > 0 { ... }          // تُجمَع مع where / reversed / limit
let sums = [each w in xs window 2 yield w[0] + w[1]]; // وفي التجميع أيضاً
```
تعمل على المصفوفات والنصوص (غيرها = خطأ واضح)، والرقم يجب أن يكون عدداً صحيحاً موجباً.

### `for` بلا أقواس
```rin
for x in [7, 8] { ... }      for i in 1 to 10 { ... }      for k, v in map { ... }
```
مرادف كامل لـ `each`؛ الشكل القديم `for (let x in xs)` و`for (;;)` باقيان كما هما.

## `repeat` — عدد ثابت من التنفيذ
```rin
repeat 3 { print "hi"; }
repeat 5 as i { print i; }          // فهرس من 0
repeat n times as k { ... }         // 'times' اختيارية
repeat (a + b) { ... }
```
العدد يُقيَّم مرة واحدة ويجب أن يكون عدداً صحيحاً غير سالب (خطأ نحوي للقيمة الحرفية، وخطأ وقت التشغيل للمتغيّر).

## `loop` — الحلقة الأبسط
```rin
loop { ...; break when (done); }         // لا نهائية حتى break
loop (cond) { ... }                      // مثل while
loop until (cond) { ... }                // حتى يتحقق الشرط
```

## التسميات: `break name` و`continue name`
```rin
outer: for (let i = 0; i < 3; i = i + 1) {
    each j in 1 to 3 {
        if (j == 2) { continue outer; }   // الدورة التالية من الحلقة الخارجية
        if (i == 2) { break outer; }      // الخروج من الحلقتين
    }
}
```
تعمل مع كل أنواع الحلقات وعبر أي عمق، مع `when`/`unless` (`break outer when (x);`). التسمية لا تعبر حدود الدالة.
تسمية غير موجودة = خطأ نحوي واضح.

## `limit N` — سقف أمان ضد الحلقات الجامحة
```rin
while (cond) limit 1000000 { ... }
each x in items limit 10000 { ... }
loop limit 50 { ... }
```
عند تجاوز N دورة يُرمى الخطأ: `loop limit exceeded: more than N iterations` (قابل للالتقاط بـ `try/catch`).
مفيد للشيفرة التي تعتمد على شرط خارجي (شبكة، إدخال مستخدم، حسابات تقاربية).

## `retry` — حلقة إعادة المحاولة
```rin
retry 5 as attempt {
    callFlakyService();       // أي throw يعيد المحاولة
}
```
يعيد تنفيذ الجسم حتى ينجح (بلا throw). بعد N محاولة يُعاد رمي الخطأ الأخير كما هو. `attempt` رقم المحاولة (يبدأ من 1).

## تجميع النتائج: `[each ... yield ...]`
```rin
let squares = [each x in nums where x % 2 == 0 yield x * x];
let table   = [each a in [1, 2] yield [each b in [10, 20] yield a * b]];
```
تعبير (لا عبارة)، يدعم كل مزايا `each` (فهرس، `where`، `reversed`، تفكيك).

## ملاحظات
- `each`/`repeat`/`loop`/`retry`/`limit`/`where`/`reversed`/`yield` كلمات **سياقية**: تبقى أسماء متغيرات ودوال عادية
  (`let loop = 1;` و`repeat("ab", 2)` يعملان).
- مع `each x, i` الشكل القاموس يعطي (مفتاح، قيمة) وغيره (عنصر، فهرس).
- الاختبارات: `tests/loops/*.rin` (+ `.expected`)، وتمر على المحرّكين: `rin run` و`rin --vm`.


---

# Loops v3 — حلقات بقوة Python وأكثر

كلها additive وتُفكَّك في المحلل (تعمل على المفسّر الشجري وRinVM وأندرويد والويب). اختبارات: `tests/loops/loops_v3_*.rin`.

## مرشّحات التيار (بالترتيب المكتوب، وتُجمَع مع `where`)
```rin
each x in xs skip 1 take 3 { ... }          // تخطَّ 1 ثم خذ 3
each x in xs where x % 2 == 0 take 2 { }    // أول عنصرين زوجيين
each x in xs until x > 7 { ... }            // توقّف عند أول عنصر يحقق الشرط
each x in xs while x < 8 { ... }            // استمر ما دام الشرط صحيحاً
```
`take`/`until`/`while` توقف الحلقة بـ `break` (فلا يُنفَّذ `nobreak` بعدها). `skip`/`take` يجب أن يكونا عدداً صحيحاً غير سالب.

## تحويل المصدر
```rin
each x in xs sorted { }                      // تصاعدي (على نسخة؛ الأصل لا يتغير)
each x in xs sorted desc { }                 // تنازلي
each p in people sorted by p["age"] desc { } // بمفتاح (متغيّر واحد عادي)
each x in xs unique { }                      // حذف التكرار بترتيب أول ظهور
each x in nested flat { }                    // فرد مستوى واحد
each x in a chain b chain c { }              // وصل عدة مصفوفات
each i in 10 downto 1 step 3 { }             // عدّ تنازلي (فارغ إن كان الأول أصغر)
```
`sorted` يتطلب عناصر/مفاتيح كلها أرقام أو كلها نصوص. تعمل على المصفوفات والنصوص والقواميس (مفاتيح) والمجموعات.

## أنماط جديدة (واحد فقط في الحلقة، مع by/window/every/zip)
```rin
each a, b in xs cross ys { }     // ضرب ديكارتي في حلقة واحدة (break يخرج من الكل)
each a, b in xs pairs { }        // (xs[0],xs[1]) (xs[1],xs[2]) ...
each x, i in xs from 1 { }       // الفهرس يبدأ من 1
```

## `nobreak { }` — مثل for..else
اسم مقروء لـ `#done { }`: يُنفَّذ فقط إن انتهت الحلقة دون `break`. يعمل مع كل أنواع الحلقات.
```rin
each n in nums { if (n % 2 == 1) { break; } } nobreak { print "كلها زوجية"; }
```

## تعابير الاختزال
`sum` `product` `count` `avg` `min` `max` `any` `all` `first` `last` ثم `each ... [where ..] [yield e]`:
```rin
sum each x in xs where x > 1 yield x * x
count each x in xs where x == 1            // yield اختياري
max each w in words yield len(w)
any each x in xs where x > 4               // يتوقف عند أول تحقق
all each x in xs yield x > 0               // yield إلزامي
first each x in xs where x > 3             // nil إن لم يوجد
avg each x in [] yield x                   // nil إن كانت فارغة
```
`yield` اختياري حيث يكون العنصر نفسه هو القيمة (مع متغيّر واحد). كلمات سياقية: تبقى أسماء عادية.

## قاموس مفهوم
```rin
let lens = {each w in words yield w : len(w)};
let odd  = {each i in 1 to 6 where i % 2 == 1 yield i : "odd"};
```


---

# إضافات من تحديث Loops (distinct وضرب ديكارتي متعدد المصادر)

```rin
each n in [3, 1, 3, 2, 1] distinct { print n; }            // أول ظهور لكل قيمة (1 و"1" مختلفان)
each w in words distinct on (w[0]) { print w; }             // بمفتاح؛ إلزامي مع أنماط التفكيك
each n in xs where n > 5 distinct take 3 { ... }            // بالترتيب المكتوب مع where/skip/take
print [each n in [4, 4, 5] distinct yield n * 2];           // يعمل في التجميع أيضاً
each x in [1, 2], y in ["a", "b"] { print x + y; }          // ضرب ديكارتي لأي عدد مصادر بحلقة واحدة
each a in "ab", b in "cd", c in [1, 2] where c > 1 { ... }  // break/continue/where تعمل كالمعتاد
```
`distinct` مرحلة تيار مثل `skip/take/where` تُنفَّذ بترتيب كتابتها. الضرب المتعدد يقبل أسماء عادية فقط
(لا فهرس ولا أنماط ولا `from` ولا by/window/every/zip/cross/pairs) والمصادر مصفوفات أو نصوص.
الاختبارات: `tests/loops/loops_distinct.rin` و`loops_pro.rin`.
