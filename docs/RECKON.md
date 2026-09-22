# RECKON.md — `reckon`: مفهوم حسابي بسطرين

`reckon` صياغة مختصرة **دائمًا سطران بالضبط** لتشغيل إحصاء/تحويل على مصفوفة واحدة
كاملة، مبنية فوق [`pipelines.md`](./pipelines.md) (`|>`) — بلا حاجة لكتابة
`filter`/`map` يدويًا.

## الصياغة
```rin
reckon <name>(<collection>)
    [where <condition>] |> <stage1>() [|> <stage2>() ...];
```
- `<name>` اسم النتيجة — يُعرَّف كمتغيّر عادي بعد انتهاء `reckon`.
- `<collection>` تعبير المصفوفة/المجموعة (من السطر الأول).
- `where <condition>` اختياري — شرط منطقي يُقيَّم مرة لكل عنصر، مع رابط ضمني اسمه
  `item`؛ العناصر التي يكون الشرط لها خاطئًا (falsy) تُستبعَد قبل أي مرحلة.
- كل `|>` مرحلة تالية تستقبل القيمة (أو المصفوفة المُرشَّحة) القادمة من قبلها
  تلقائيًا كأول وسيط — تمامًا بنفس دلالة `|>` العادية.

## مثال حقيقي (من `examples/reckon_demo.rin`، تحقّقتُ من تشغيله فعليًا)
```rin
let scores = [-5, 42, 88, 0, 73, 91, -12, 60];

reckon average(scores)
    where item > 0 |> mean();
print average; // 70.8

reckon normalizedScores(scores)
    |> normalize() |> scale();
print normalizedScores; // [0.0679612, 0.524272, 0.970874, ...]

let weights = [1, 1, 2, 2, 1, 3, 2];
let grades = [55, 62, 78, 91, 40, 100, 67];
reckon weightedGrade(grades)
    |> weightedMean(weights);
print weightedGrade; // 77.4167

reckon smoothedScores(scores)
    |> movingAverage(3) |> clamp(0, 100);
print smoothedScores; // [41.6667, 43.3333, 53.6667, ...]
```

## جدول الدوال الإحصائية/التحويلية الجاهزة
كل هذه دوال أصلية (native) عادية — تعمل خارج `reckon`/`|>` أيضًا (`mean(arr)` تعمل
تمامًا كـ`arr |> mean()`)، لكن الاستخدام الشائع لها هو كمراحل داخل `reckon`.

| الدالة | الوصف |
|---|---|
| `sum(arr)` | مجموع العناصر. |
| `mean(arr)` | المتوسط الحسابي. |
| `median(arr)` | الوسيط. |
| `mode(arr)` | القيمة الأكثر تكرارًا. |
| `variance(arr)` | التباين. |
| `stddev(arr)` | الانحراف المعياري. |
| `geometricMean(arr)` | المتوسط الهندسي. |
| `harmonicMean(arr)` | المتوسط التوافقي. |
| `minOf(arr)` / `maxOf(arr)` | أصغر/أكبر عنصر. |
| `percentile(arr, p)` | المئين رقم `p`. |
| `iqr(arr)` | المدى الربيعي (Interquartile Range). |
| `count(arr)` | عدد العناصر (بعد أي `where` مُطبَّق). |
| `weightedMean(arr, weights)` | متوسط مرجَّح بمصفوفة أوزان موازية. |
| `normalize(arr)` | تطبيع القيم إلى مدى [0, 1]. |
| `scale(arr)` | نفس فكرة `normalize` بصياغة مرحلة أنبوب. |
| `cumulativeSum(arr)` | مجموع تراكمي (مصفوفة ناتجة بنفس الطول). |
| `movingAverage(arr, window)` | متوسط متحرّك بحجم نافذة `window`. |
| `clamp(arr, lo, hi)` | يقصّ كل قيمة إلى المدى `[lo, hi]`. |

## `reckon` داخل `@make.(name)`
`reckon` تعمل بلا أي فرق داخل جسم [`@make.(name)`](./MAKE_UNIT.md) أيضًا — مفيد
عندما تريد تقييد القدرات (`use reckon; need reckon; strict;`) على حساب مُحدَّد
بدل السماح به في أي مكان بالملف:
```rin
@make.(reportCard)
    kind data;
    use reckon;
    need reckon;
    strict;

    reckon topAverage(grades)
        where item >= 60 |> mean();

    show topAverage;
.end/make=reportCard
```

## انظر أيضًا
- [`pipelines.md`](./pipelines.md) — عامل `|>` الذي يُبنى `reckon` فوقه.
- [`MAKE_UNIT.md`](./MAKE_UNIT.md) — `@make.(name)` وسياسة القدرات.
- [`variables.md`](./variables.md) — المصفوفات التي يعمل عليها `reckon`.
