# containerType — أنواع حاويات يعرّفها المستخدم

قالب يُعرَّف مرة واحدة (حقول، أنواع، افتراضيات، إلزام، مدى، قواعد، وراثة) ثم تُنشأ منه حاويات **تُفحَص عند كل كتابة**.
إضافة بحتة: لا تتأثر أنواع الحاويات المبنية (`doc` `table` `data` ...) ولا أي كود قديم.

```rin
defineContainerType("Person", {
    name: {type: "string", required: true},          // إلزامي
    age:  {type: "number", default: 0, min: 0, max: 150},
    role: {default: "user", check: #["user" "admin"]},   // Set = القيم المسموحة
    tags: []                                          // قيمة = افتراضية، ونوعها يُستنتج (array)
});
defineContainerType("Employee", {
    salary: {default: 100, check: fun(v) { return v > 0 or "الراتب يجب أن يكون موجباً"; }},
    role:   {default: "staff", check: #["staff" "manager"]}     // الابن يطغى على حقل أبيه بالاسم
}, {extends: "Person", onCreate: fun(n) { $ "أُنشئت", n; }});

newContainer("Employee", "sara", {name: "سارة"});
setField("sara", "salary", -5);       // خطأ E0004: الراتب يجب أن يكون موجباً
```

## `defineContainerType(name, spec, opts?)`
**مواصفة الحقل:** نص نوع (`"number"` `"string"` `"bool"` `"array"` `"map"` `"set"` `"any"` + أنواع البيانات الموسّعة `"int"` `"uint"` `"float"` `"email"` `"date"` `"array<int>"` `"int?"` `"enum(a,b)"` ... انظر [data_types.md](./data_types.md)) · أو قيمة = افتراضية (يُستنتج نوعها) ·
أو `{type, default, required, min, max, check}`. لقيمة افتراضية من نوع `map` اكتب `{default: {...}}`.
`check` = `Set` (قيم مسموحة) أو `fun(v)` تعيد `true` أو `false` أو **نصاً** (= سبب الرفض).
**الخيارات:** `extends:"Base"` (وراثة، الحقول والقواعد و`onCreate` تُورَّث) · `strict:true` (يُرفض أي حقل غير معرَّف، يشمل الأنواع الوارثة) · `onCreate: fun(name)`.
إعادة تعريف نوع بالاسم نفسه تستبدله (الحاويات القائمة تُفحَص بالتعريف الجديد من الكتابة التالية).

## الإنشاء بصيغة `@اسم.Type`
```rin
@sara.Employee { name: "سارة" salary: 500 }     // ≡ newContainer("Employee", "sara", {...})
@omar.Employee { name: "عمر", age: 30 };        // الفاصلة و`;` اختياريتان
@tmp.Employee;                                   // بلا قيم: الافتراضيات فقط (تفشل إن كان حقل إلزامي بلا قيمة)
```
تُفحَص الحقول عند الإنشاء وتُنفَّذ `onCreate` كما في `newContainer`. لا تعارض: `@اسم.كلمة` لا تُفعَّل للأنواع المبنية (`doc` `table` `data` ...) ولا لأسماء الوسوم المحجوزة. الاختبار: `tests/print/container_typed_decl.rin`.

## الإنشاء والفحص
| الدالة | تفعل |
|---|---|
| `newContainer(type, name, init?)` | تنشئ حاوية: الافتراضيات ثم `init`، وتفحص الكل **قبل** الإنشاء (لا حاوية ناقصة عند الخطأ)، ثم `onCreate` (الأب أولاً) |
| `containerType(name)` | نوعها المعرَّف، وإلا نوعها المبني (`container.doc` ...)، و`nil` إن لم توجد |
| `isContainerType(name, type)` | `true` إن كانت من النوع أو من نوع يرثه |
| `containersOfType(type)` | أسماء حاوياته (تشمل الأنواع الوارثة) مرتبة |
| `containerTypes()` | أسماء الأنواع المعرَّفة بترتيب التعريف |
| `containerTypeInfo(type)` | `{name, parent, strict, fields:[{name,type,required,default,min,max}]}` (مع الموروث) |
| `validateContainer(name)` | مصفوفة مشاكل (فارغة = سليمة) — لما يُعدَّل مباشرة داخل `@container` متجاوزاً `setField` |

`kindOf(name)` و`container.kind` و`container.byKind("Employee")` تعيد/تطابق اسم النوع المعرَّف أيضاً.

## أين يُفحَص؟
كل كتابة عبر `setField` / `container.set` / `setState` — ومنها **`input(prompt, "name", ...)`** (يمرّ عبر `setState`) فيُرفض إدخال المستخدم المخالف بخطأ E0004 (لا يعيد السؤال؛ لإعادة السؤال مرّر `validator` للدالة).
تراجع `@tx` يستعيد القيم السابقة (كانت صالحة أصلاً) بلا فحص.

## حدود
- الإسناد المباشر داخل جسم `@container` (`x = 5;`) لا يمرّ بالفحص؛ استعمل `validateContainer`.
- `required` يعني: لا `nil` لحظة الإنشاء أو الكتابة. حذف الحقل نفسه لا يُمنع.
- الاختبار: `tests/print/container_types.rin`.
