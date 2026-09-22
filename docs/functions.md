# functions.md — `fun`/`return`، التكرار الذاتي

## تعريف واستدعاء
```rin
fun add(x, y) {
    return x + y;
}
print add(2, 3); // 5
```
دالة بلا `return` صريح تُرجع `nil` تلقائيًا عند انتهاء جسمها:
```rin
fun noReturn() { print "side effect"; }
let r = noReturn();
print r; // nil
```

## التكرار الذاتي (Recursion)
تعمل بشكل طبيعي تمامًا — لا قيد خاص:
```rin
fun fact(n) {
    if (n <= 1) { return 1; }
    return n * fact(n - 1);
}
print fact(5); // 120
```

## أنواع اختيارية للوسائط والإرجاع (Type System — إضافي بحت)
```rin
fun typed(a: Number, b: Number): Number {
    return a + b;
}
```
تمامًا كنوع `let` الاختياري (انظر [`variables.md`](./variables.md)) — وسيط بلا نوع
معلَن يبقى بلا أي فحص، ونوع الإرجاع اختياري بالمثل.

## دوال كقيَم (Lambda literals)
```rin
let square = fun(x) { return x * x; };
print square(4); // 16
```
`fun(...) { ... }` بلا اسم بعد `fun` هو تعبير قيمته دالة — يمكن إسناده لمتغيّر أو
تمريره كوسيط مباشرة (كأي دالة رتبة أولى عادية).

## دوال داخل حاويات (Methods)
داخل `class`/`struct`، كل `fun` معرَّفة في جسمها تصبح طريقة (method) تُستدعى عبر
`instance.method()`، وتصل لحقول نفس النسخة عبر `self.field` (انظر
[`objects.md`](./objects.md#class-struct)):
```rin
class Counter {
    let value = 0;
    fun init(start) { self.value = start; } // "init" = المُنشئ (constructor)
    fun inc() { self.value = self.value + 1; }
}
let c = Counter(10);
c.inc();
print c.value; // 11
```

## انظر أيضًا
- [`control-flow.md`](./control-flow.md) — `goal`/`achieve` كخروج مبكر بقيمة بلا دالة كاملة.
- [`objects.md`](./objects.md) — `class`/`struct` والطرق (methods).
- [`variables.md`](./variables.md) — نظام الأنواع الاختياري المشترك مع `let`.
