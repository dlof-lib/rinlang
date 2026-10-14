# قوالب الحلقات `@tmpl`

بدل أن تكتب حلقة جديدة في كل مرة، تعرّف **قالبًا مرة واحدة** وتعيد استخدامه مع مجموعات مختلفة.
يبدأ التعريف بـ `@tmpl` ويبدأ الاستعمال بـ `@اسم_القالب(...)`.

القوالب تُفكَّك على مستوى التوكنات قبل التحليل النحوي، لذلك تعمل مع **كل** أنواع الحلقات
(`each` `for` `while` `loop` `repeat` `retry` + `limit` + التسميات) على المفسّر الشجري وRinVM وأندرويد والويب،
ولا تغيّر المفسّر ولا الآلة الافتراضية. برنامج بلا `@tmpl` لا يُمَسّ إطلاقاً.

## 1) قالب ترويسة
القالب = ترويسة حلقة تنتهي بـ `;`، وجسم الحلقة تكتبه عند الاستعمال:
```rin
@tmpl evens(xs) each x in xs where x % 2 == 0;

@evens([1, 2, 3, 4, 5, 6]) { print x; }      // 2 4 6
@evens(1 to 10) as n { print n; }            // as = تسمية متغيّر الحلقة
@evens(nums) take 2 { print x; }             // مرشّحات إضافية بعد الاستعمال تُضاف طبيعياً
```

## 2) معاملات وقيم افتراضية
```rin
@tmpl top(xs, k = 2) each v in xs sorted desc take k;
@top(scores) { print v; }           // k = 2
@top(scores, 5) as s { print s; }
```
المعامل الذي يأتي مباشرة بعد `in` يُعوَّض كما هو (فيعمل `1 to 10` وسلاسل `chain`)،
وغيره يُغلَّف بأقواس تلقائياً إن كان أكثر من توكن (فلا يتغيّر ترتيب العمليات).

## 3) جسم افتراضي
```rin
@tmpl show(xs) each item in xs { print "item " + item; }
@show(["a", "b"]);                  // بلا جسم → يُستعمل الافتراضي
@show(["a"]) { print "mine"; }      // جسمك يحلّ محلّه
```

## 4) `as` — تسمية متغيّرات الحلقة
تُستنتج المتغيّرات القابلة للتسمية تلقائياً من الترويسة: `each x, i in` ← `x, i` · `each x in a, y in b` ← `x, y` ·
`repeat n as i` ← `i` · `retry n as a` ← `a`.
```rin
@tmpl numbered(xs) each x, i in xs from 1;
@numbered(names) as name, no { print no + ". " + name; }
```
يمكن تسمية عدد أقل من المتغيّرات، لا أكثر (خطأ واضح يعرض الأسماء المتاحة).

## 5) كل أنواع الحلقات
```rin
@tmpl times(n) repeat n as i;           @times(3) { print i; }
@tmpl attempts(n) retry n as a;         @attempts(5) { callFlaky(); }
@tmpl up(n) for (let i = 0; i < n; i++) limit 1000;
@tmpl until0(n) while (n > 0) limit 50;
outer: @evens(xs) { ... continue outer; ... }   // التسميات تعمل
```

## 6) داخل التعابير
```rin
[@evens(xs) yield x * x]            sum @evens(xs) yield x
count @evens(1 to 9)                {@evens(xs) yield x : x * x}
```

## 7) قوالب الكتلة و`@body`
حين يلزم أكثر من ترويسة (حلقات متداخلة، تغليف) اكتب القالب بين `{ }` وضع `@body` مكان جسم المستدعي،
واكشف المتغيّرات بـ `as`:
```rin
@tmpl grid(r, c) as i, j { each i in 1 to r { each j in 1 to c { @body } } }
@grid(2, 3) { print i + "," + j; }
@grid(4, 4) as row, col { if (row == col) { print row; } }
```

## 8) قالب يستعمل قالباً آخر
```rin
@tmpl evenSquares(xs) @evens(xs) take 2;
```

## قواعد وأخطاء
- يُعرَّف القالب قبل استعماله في الملف، وفي أي مكان (حتى داخل دالة) لكنه عام للملف كله؛ لا يعبر `@import`.
- الأسماء المحجوزة: `tmpl body import view element container Containers make loop Volume volume link portal route object install`.
- أخطاء واضحة: عدد الوسائط، وسيط ناقص، `as` أكثر من المتاح، قالب مُعرَّف مرتين، قالب كتلة بلا `@body`، قالب بلا جسم افتراضي
  استُعمل بلا جسم، استدعاء ذاتي (`loop template expansion is too deep`).
- الأسماء داخل القالب غير معزولة (غير hygienic): اختر أسماء متغيّرات لا تصطدم بمتغيّراتك أو استعمل `as`.
- اختبار: `tests/loops/loops_tmpl.rin`.
