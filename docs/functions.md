# الدوال (Functions)

> راجع أيضاً: [`variables.md`](./variables.md) للوسائط والقيم المُعادة،
> [`control-flow.md`](./control-flow.md) لاستخدام `if`/الحلقات داخل جسم الدالة،
> [`standard-library.md`](./standard-library.md) لعشرات الدوال الجاهزة المبنية
> بنفس القواعد، و[`language-reference.md`](./language-reference.md) للخريطة الكاملة.

## 1) تعريف دالة: `fun`

```rin
fun fib(n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
print fib(10);
```

هذا المثال (`fib`) يلخّص كيف تترابط المفاهيم: [**متغيّر**](./variables.md) الوسيط `n`،
[**شرط**](./control-flow.md#1-العامل-الأساسي-للشرط-if--else) `if (n < 2)`،
`return` لإنهاء الدالة بقيمة، واستدعاء الدالة **لنفسها** (recursion).

## 2) الوسائط والقيمة المُعادة

- الوسائط (parameters) هي متغيّرات محلية لجسم الدالة (انظر [النطاق](./variables.md#5-النطاق-scope)).
- `return` تُنهي تنفيذ الدالة فوراً وتُعيد قيمة؛ دالة بلا `return` صريح تُعيد `nil`.
- يمكن استخدام `return` داخل أي فرع من فروع [`if`/`else`](./control-flow.md#1-العامل-الأساسي-للشرط-if--else)
  لإنهاء الدالة بمخرجات مختلفة حسب الشرط، كما في `factorial`:

```rin
fun factorial(n) {
    if (n < 0) { print "n يجب أن يكون >= 0"; return nil; }
    if (n < 2) { return 1; }
    return n * factorial(n - 1);
}
```

## 3) الدوال والحلقات معاً

الدوال كثيراً ما تُبنى داخلياً من [حلقات](./control-flow.md#3-الحلقات-التكرارية)
بدل التكرار الذاتي (خصوصاً عند الحاجة لأداء أفضل)، كما في `gcd`:

```rin
fun gcd(a, b) {
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) {
        let t = b;
        b = a % b;
        a = t;
    }
    return a;
}
```

## 4) الدوال كقيم تُمرَّر عبر خط الأنابيب

نتيجة استدعاء دالة (أو الدالة نفسها كخطوة) تدخل مباشرة في
[الأنابيب](./pipelines.md) `|>`:

```rin
let result = data |> normalize() |> mean();
```

## انظر أيضاً

- [`variables.md`](./variables.md) — نطاق الوسائط والقيم المحلية.
- [`control-flow.md`](./control-flow.md) — `if`/`while`/`for` داخل جسم الدالة.
- [`standard-library.md`](./standard-library.md) — أمثلة حقيقية إضافية (`isPrime`, `clamp`, `lerp`, ...).
- [`pipelines.md`](./pipelines.md) — تركيب الدوال في سلسلة `|>`.
- [`language-reference.md`](./language-reference.md) — الخريطة الكاملة للغة.
