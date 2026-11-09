# توحيد الحلقات — Unified loops

كل شكل حلقة يقبل الآن **نفس المعدِّلات** بأي ترتيب قبل `{`، بلا كلمات جديدة (كلها موجودة أصلاً في each/limit):

| المعدِّل | المعنى |
|---|---|
| `where C` | تخطَّ الدورة إن لم يتحقق C |
| `skip N` | تجاهل أول N دورات (بعد where) |
| `take N` | توقّف بعد N دورات منفَّذة |
| `until C` | اخرج عند تحقق C |
| `limit N` | سقف أمان (يرمي خطأ عند التجاوز) |

الأشكال: `each` · `for (let x in xs)` · `for (;;)` · `while` · `repeat N` · `loop` · `retry N` (`limit`) · `#do…#while`.

```
for (let x in xs) where x > 2 take 2 { … }
repeat 6 skip 4 { print i; }
loop take 3 { … }
#do { … } #while (c) else { … }      // الفاصلة اختيارية قبل else
retry 3 limit 9 { … } else { … }
let r = each x in xs { if (x == 4) { achieve x * 10; } };   // حلقة كتعبير
```

`else` للحلقة (بلا break) و`#done` يعملان في كل الأشكال. حدود: `rinc` لا يدعم هذه الصيغ؛ `limit` يرمي عند التجاوز كما كان.
الاختبار: `tests/print/loops_unified.rin`.
