# MAKE_UNIT.md — `@make.(name)`: وحدة بسياسة قدرات صريحة

`@make.(name) ... .end/make=name` حاوية عادية (`ContainerStmt`/`MakeStmt` في
`rin_ast.h`) بإضافتين: كلمة `kind` لتصنيفها، وحقول إدخال/إخراج/عام/خاص
(`input`/`output`/`public`/`private`) خاصة بها فقط دون أنواع الحاويات الأخرى.

## الصياغة الأساسية (مُتحقَّق منها فعليًا — تعمل)
```rin
@make.(calc)
    kind app;
    use io;
    strict;

    let result = 2 + 2;
    show result;
.end/make=calc
```
```
🛠️ make.app = calc
4
✅ .end/make.app (calc)
```
- `kind <word>;` تصنيف حر (أي معرِّف — `app` هو الافتراضي إن غاب `kind` أصلاً).
- الإغلاق يكتب اسم الوحدة بعد `=`: `.end/make=<name>` (وليس `.end/make` وحدها).

## حقول الوحدة
| الكلمة | الغرض |
|---|---|
| `input <name>;` | اسم مُدخَل مُعلَن (تعريفي/توثيقي). |
| `output <name>;` | اسم مُخرَج مُعلَن. |
| `public <name>;` | اسم عنصر معلَن كعام. |
| `private <name>;` | اسم عنصر معلَن كخاص. |
| `version "..."` | نص إصدار حر. |
| `description "..."` | نص وصف حر. |

كل هذه الكلمات تُكتب بصيغة المفرد (`input`، لا `inputs`) وتُكرَّر سطرًا لكل اسم إن
احتجت أكثر من واحد.

## سياسة القدرات (Policy — RCS-1.0 §3.13)
نفس الكلمات الأربع المُعمَّمة الآن لأي `@container` (انظر
[`containers.md`](./containers.md#container)) لكنها ظهرت أول مرة هنا تحديدًا في
Make Unit:

| الكلمة | الغرض |
|---|---|
| `use <capability>;` | يصرِّح بأن هذه القدرة **متاحة** للاستخدام داخل الوحدة. |
| `need <capability>;` | يصرِّح بأن هذه القدرة **مطلوبة فعليًا** — التحقّق (`validateMakeUnit` في `rin_make.cpp`) يرفض الوحدة إن لم تُستوفَ (رأيتُ فعليًا خطأ `requires capabilities not used: math` عند التصريح بـ`need math;` بلا استيفائها، حتى مع استدعاء دالة إحصائية فعليًا في الجسم — آلية المطابقة الدقيقة بين اسم القدرة واستخدامها الفعلي في الكود مصدرها الوحيد الموثوق هو `validateMakeUnit()` نفسها، لم أُعِد بناءها هنا تخمينًا). |
| `allow <capability>;` | يسمح صراحة بقدرة (دون اشتراطها). |
| `deny <capability>;` | يمنع صراحة قدرة معيَّنة حتى لو ظهرت ضمنيًا. |
| `strict;` | يُفعِّل التحقّق الصارم: أي قدرة تُستخدَم بالجسم (مثل `io` لـ`print`/`show`) يجب أن تكون معلَنة صراحة بـ`use`، وإلا خطأ فوري (رأيتُه فعليًا: `is strict: capability 'io' must be declared with 'use io;'`). بلا `strict;`، الوحدة تسلك بشكل متساهل (permissive) بلا أي تحقّق إضافي — تمامًا كحاوية عادية.

## مثال حقيقي مدمج مع `reckon` (من `examples/reckon_demo.rin`)
```rin
@make.(reportCard)
    kind data;
    use reckon;
    use io;
    need reckon;
    strict;

    reckon topAverage(grades)
        where item >= 60 |> mean();

    show topAverage;
.end/make=reportCard
```

## انظر أيضًا
- [`containers.md`](./containers.md) — `@container` بكل أنواعه، وكيف تُعمَّم نفس
  كلمات السياسة الأربع لأي حاوية عادية، لا فقط Make Unit.
- [`RECKON.md`](./RECKON.md) — `reckon` يعمل بلا أي فرق داخل `@make.(name)`.
- `app/src/main/cpp/rin_make.cpp` — `validateMakeUnit()`، المصدر الوحيد الموثوق
  لآلية المطابقة الدقيقة بين اسم كل قدرة (`math`, `io`, `reckon`, ...) وكشفها الفعلي
  داخل جسم الوحدة.
