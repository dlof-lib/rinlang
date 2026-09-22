# variables.md — `let`، مصفوفات، قواميس، نطاق

## `let`
```rin
let x = 5;
print x; // 5
```
`let` هي الطريقة الوحيدة لتعريف متغيّر جديد. لا يوجد `var`/`const` منفصلَين.

### نوع اختياري (Type System — إضافي بحت)
```rin
let x: Number = 5;
```
كتابة النوع بعد `:` **اختيارية بالكامل** — تركها يعني "بلا نوع معلَن" (نفس السلوك
القديم تمامًا، بلا أي فحص). الأنواع المدمجة المقبولة: `Any`, `Number`, `Int`,
`String`, `Bool`, `Array`, `Map`, `Function`، إضافة إلى أي `class`/`struct` معرَّف
بنفس الاسم. كتابة نوع غير معروف تُنتج خطأً وقت التشغيل عند التصريح:
```
error[E0001]: unknown type `Text`
```
(الاسم الصحيح للنصوص هو `String`، لا `Text`).

## المصفوفات (Array)
```rin
let arr = [1, 2, 3];
print arr[0];     // 1
arr[0] = 99;       // فهرسة قابلة للإسناد
print arr;         // [99, 2, 3]
print len(arr);    // 3
```

## القواميس (Map)
```rin
let m = {"a": 1, "b": 2};
print m["a"];      // 1
m["c"] = 3;         // إضافة مفتاح جديد بنفس صياغة الفهرسة
print m;            // {"a": 1, "b": 2, "c": 3}
for (let key in m) {
    print key + "=" + m[key]; // ترتيب المفاتيح كما تُعيده keys(m)
}
```

## `type()`
يُرجع اسم النوع الفعلي وقت التشغيل كنص صغير الحروف:
```rin
print type(5);          // number
print type("s");        // string
print type(true);       // bool
print type([1]);        // array
print type({"a": 1});   // map
print type(nil);        // nil
```
(لاحظ الفرق: اسم النوع في التصريح `String`/`Number`/... بحرف كبير، بينما `type()`
وقت التشغيل يُرجعها بحروف صغيرة `string`/`number`/...)

## النطاق (Scope)
كل `{ ... }` يفتح نطاقًا جديدًا. `let` بنفس الاسم داخل نطاق داخلي يُظلِّل (shadow) ما
في الخارج بلا أن يغيّره:
```rin
let y = 10;
{
    let y = 20;
    print y; // 20
}
print y; // 10 -- لم يتأثر
```

## انظر أيضًا
- [`syntax.md`](./syntax.md) — القواعد العامة، العوامل.
- [`control-flow.md`](./control-flow.md) — التكرار على مصفوفة/قاموس عبر `for (let x in ...)`.
- [`functions.md`](./functions.md) — تمرير القيم كوسائط.
- [`enums.md`](./enums.md) — قوائم اختيار مغلقة كبديل عن نص/رقم خام.
