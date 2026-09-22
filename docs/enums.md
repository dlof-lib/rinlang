# enums.md — `enum`: قوائم اختيار مغلقة

## التعريف
```rin
enum Status {
    Active,
    Paused,
    Done = 99   // قيمة صريحة اختيارية بعد '='
}
```
`Status` نفسها تُعرَّف كمتغيّر عادي من نوع قاموس يضم كل الحالات — يُصَل إليها عبر
وصول حقل عادي: `Status.Active`.

## القيمة والاسم
كل حالة (case) قيمتها الفعلية وقت التشغيل شكل ثابت `{__enum__, name, value}`:
```rin
print Status.Active.name;   // Active
print Status.Active.value;  // nil  -- لم تُعطَ قيمة صريحة
print Status.Done.value;    // 99
```
**لا يوجد ترقيم تلقائي (auto-increment)** كما في لغات مثل C: كل حالة قيمتها إما ما
كُتب صراحة بعد `=`، أو `nil` إن غابت — لا علاقة لقيمتها بترتيبها في التعريف.

## المقارنة
```rin
let s = Status.Paused;
print s == Status.Paused; // true
print s == Status.Active; // false
```

## الاستخدام مع `match`/`case`
هذا هو الاستخدام الأكثر شيوعًا لـ`enum` — انظر
[`control-flow.md`](./control-flow.md#match--case) للتفاصيل الكاملة:
```rin
match (s) {
    case Status.Active { print "نشط"; }
    case Status.Paused, Status.Done { print "متوقف"; } // أكثر من قيمة بفاصلة
    else { print "غير معروف"; }
}
```

## انظر أيضًا
- [`control-flow.md`](./control-flow.md) — `match`/`case`.
- [`variables.md`](./variables.md) — القواميس (`enum` مبني فوق نفس فكرة القاموس).
- [`objects.md`](./objects.md) — `class`/`struct` لبيانات أكثر مرونة من enum المغلق.
