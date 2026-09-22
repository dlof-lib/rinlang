# control-flow.md — الشروط، الحلقات، `goal`/`achieve`

## `if` / `else`
```rin
if (a > 3) {
    print "big";
} else {
    print "small";
}
```
`else` اختيارية. `else if` تعمل بشكل طبيعي (سلسلة `if` متداخلة عاديًا).

## `when` / `otherwise`
صياغة إنجليزية مبسَّطة مرادفة كاملة لـ `if`/`else` (بلا أي فرق في السلوك):
```rin
when (len([1, 2, 3]) == 3) {
    print "when/otherwise يعمل";
} otherwise {
    print "لن يظهر هذا";
}
```

## `plus.condition` — شرط ثلاثي على مستوى العبارات
```rin
plus.condition (5 > 3) {
    print "yes branch";
} / {
    print "no branch";
}
```
بخلاف `if`/`else`، الكتلتان هنا **إلزاميتان دائمًا** (لا يوجد شكل بلا الفرع الثاني)،
فهي "ثلاثية" فعليًا على مستوى العبارة، لا التعبير. الفاصل بين الكتلتين هو `/` فقط —
يظهر حصرًا بين `}` الأولى و`{` الثانية.

## `while`
```rin
let i = 0;
while (i < 3) {
    print "w" + i;
    i = i + 1;
}
```

## `for` (على طراز C)
```rin
for (let j = 0; j < 3; j = j + 1) {
    print "c" + j;
}
```
الأجزاء الثلاثة (التهيئة، الشرط، الزيادة) اختيارية تمامًا كما في C — تركها كلها فارغة
ينتج حلقة لا نهائية إلا بـ `break`.

## `for ... in` — تكرار حقيقي
```rin
let arr = [1, 2, 3];
for (let x in arr) { print x; }       // كل عنصر بدوره

let m = {"a": 1, "b": 2};
for (let key in m) { print key; }     // كل مفتاح بدوره (نفس ترتيب keys(m))

for (let ch in "abc") { print ch; }   // كل حرف كنص من محرف واحد
```

## `break` / `continue`
تعملان داخل `while`/`for`/`for ... in` بنفس المعنى المعتاد:
```rin
let i = 0;
while (i < 5) {
    i = i + 1;
    if (i == 3) { continue; }
    if (i == 5) { break; }
    print i; // 1, 2, 4
}
```

## `match` / `case`
مطابقة أنماط، غالبًا على قيمة `enum` (انظر [`enums.md`](./enums.md)):
```rin
enum Status { Active, Paused, Done }
let current = Status.Paused;

match (current) {
    case Status.Active {
        print "الحالة: نشط";
    }
    case Status.Paused, Status.Done {  // أكثر من قيمة بفاصلة لنفس case
        print "الحالة: متوقف";
    }
    else {
        print "الحالة: غير معروفة";
    }
}
```

## `goal` / `achieve` — خروج مبكر بقيمة
`goal { ... }` تعبير يُقيَّم إلى قيمة: إن نُفِّذ `achieve <expr>;` بداخله يتوقف فورًا
بتلك القيمة؛ إن لم يُنفَّذ أي `achieve` تكون القيمة النهائية `nil`. مفيد لخروج مبكر
من منطق متعدد الخطوات بلا الحاجة لدالة منفصلة كاملة فقط لأجل `return`:
```rin
fun findFirstEven(arr) {
    let result = goal {
        let i = 0;
        while (i < len(arr)) {
            if (arr[i] % 2 == 0) {
                achieve arr[i]; // يوقف goal فورًا بهذه القيمة
            }
            i = i + 1;
        }
        // لا achieve هنا => النتيجة nil
    };
    return result;
}
print findFirstEven([1, 3, 5, 8, 9]); // 8
print findFirstEven([1, 3, 5]);       // nil
```

## انظر أيضًا
- [`enums.md`](./enums.md) — تعريف `enum` المستخدَم مع `match`.
- [`functions.md`](./functions.md) — `fun`/`return`.
- [`syntax.md`](./syntax.md) — العوامل المنطقية (`and`/`or`/`!`، **ليست** `&&`/`||`).
