# الأنابيب (Pipelines)

> راجع أيضاً: [`functions.md`](./functions.md) للدوال المُستخدمة كخطوات أنبوب،
> [`variables.md`](./variables.md) لتخزين نتيجة الأنبوب، و
> [`containers.md`](./containers.md#2-خط-أنابيبإحصاء-containerpipe) لحاوية أنابيب مُسمّاة.

## العامل `|>`

```rin
let data = [10, 20, 30, 40, 50];
let result = data |> normalize() |> mean();
print result;
```

يمرّر `|>` القيمة على يساره كمُدخل ضمني للدالة على يمينه، فتتسلسل عدة
[دوال](./functions.md) في سطر واحد بدل تعشيش الاستدعاءات (`mean(normalize(data))`).
النتيجة النهائية قيمة عادية تُخزَّن بـ [`let`](./variables.md) كأي متغيّر آخر،
ويمكن لاحقاً اختبارها ضمن [شرط](./control-flow.md):

```rin
if (result > 0) { print "طبيعي"; }
```

لتسمية سلسلة أنابيب وإعادة استخدامها ضمن هيكل أكبر، استخدم
[`@container.pipe`](./containers.md#2-خط-أنابيبإحصاء-containerpipe).

## انظر أيضاً

- [`functions.md`](./functions.md) — الدوال التي تُركَّب داخل الأنبوب.
- [`variables.md`](./variables.md) — تخزين مُدخل/مُخرج الأنبوب.
- [`containers.md`](./containers.md) — تغليف أنبوب داخل حاوية مُسمّاة.
- [`standard-library.md`](./standard-library.md) — دوال جاهزة شائعة الاستخدام كخطوات أنبوب.
- [`language-reference.md`](./language-reference.md) — الخريطة الكاملة للغة.
