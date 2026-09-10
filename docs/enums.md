# الخيارات (Options): `enum`

> ميزة موجودة في محرّك Rin منذ فترة (`rin_ast.h` / `rin_parser.cpp` / `rin_interpreter.cpp`) لكنها
> لم تكن موثّقة في أي صفحة سابقاً. هذه الصفحة تسدّ تلك الفجوة.
> راجع أيضاً: [`control-flow.md`](./control-flow.md) لاستخدام قيم enum داخل `match`/`if`،
> [`objects.md`](./objects.md) للقواميس (map) التي يُبنى enum فوقها داخلياً.

## 1) الفكرة

`enum` يعرّف **قائمة اختيار مغلقة**: مجموعة ثابتة من القيم المسمّاة (حالات/cases) التي يُسمح لمتغيّر
أن يأخذ إحداها فقط — بديل أوضح وأكثر أماناً من الاعتماد على نصوص/أرقام حرّة (`"active"`, `1`, ...)
منتشرة في الكود بلا رقابة.

```rin
enum Status {
    Active,
    Paused,
    Done
}

let s = Status.Active;
print s.name;   // "Active"
```

## 2) الصياغة

```rin
enum Name {
    Case1,
    Case2 = expr,   // قيمة مخصّصة اختيارية بعد '='
    Case3,          // فاصلة زائدة قبل '}' مسموحة
}
```

- `enum` كلمة سياقية غير محجوزة (بنفس أسلوب `class`/`struct`): لا تتحوّل إلى إعلان enum إلا إذا
  ظهرت مباشرة قبل اسم (IDENT) في بداية عبارة، فلا تتعارض مع استخدامها اسم متغيّر عادي.
- كل حالة (case) قيمتها الفعلية اختيارية عبر `= expr` بعد اسمها؛ إن غابت تكون `nil`.
- `Name` نفسها تُعرَّف كمتغيّر عادي من نوع قاموس (map) يضمّ كل الحالات — هذا ما يجعل `Name.Case1`
  يعمل عبر نفس قراءة الحقول (`GetExpr`) المستخدمة لأي كائن/قاموس آخر في اللغة، بلا نوع بيانات جديد
  كلياً في وقت التشغيل.

## 3) شكل قيمة الحالة وقت التشغيل

كل حالة (`Name.CaseX`) هي قاموس (map) بثلاثة حقول ثابتة:

```rin
enum Priority { Low, Medium, High = 100 }

print Priority.Low.name;    // "Low"
print Priority.High.value;  // 100
print Priority.Low.value;   // nil (لم تُحدَّد قيمة صريحة)
```

| الحقل | المعنى |
|---|---|
| `name`  | اسم الحالة كما كُتب في الإعلان (نص) |
| `value` | القيمة بعد `=` إن وُجدت، وإلا `nil` |

## 4) المقارنة بين حالات enum

المقارنة بـ `==` تُقارن القاموس بالكامل تركيبياً (كل الحقول)، لذا `Status.Active == Status.Active`
تعطي `true` دائماً (نفس الاسم من نفس enum) — هذا ما يجعلها صالحة مباشرة داخل `if`/`match`
(انظر [`control-flow.md`](./control-flow.md#6-مطابقة-الأنماط-match--case)):

```rin
enum Status { Active, Paused, Done }
let s = Status.Active;

if (s == Status.Active) { print "running"; }

match (s) {
    case Status.Active { print "running"; }
    case Status.Paused, Status.Done { print "not running"; }
}
```

## 5) لماذا enum بدل نص/رقم خام؟

- **قائمة مغلقة واضحة**: كل القيم الممكنة مجمّعة في مكان واحد (`enum Status { ... }`) بدل انتشارها
  كسلاسل نصية حرّة عبر الملف كله.
- **أخطاء إملائية أوضح**: `Status.Activ` (خطأ إملائي) يفشل فوراً بخطأ "لا يوجد حقل" بدل أن يمرّ
  صامتاً كسلسلة نصية جديدة غير مقصودة (`"activ"`).
- **قيمة مرتبطة اختيارية**: `value=` يسمح بربط كل حالة برمز/رقم عند الحاجة (مثال: `HttpStatus.OK
  = 200`) بلا فقدان اسمها القابل للقراءة.

## انظر أيضاً

- [`control-flow.md`](./control-flow.md) — استخدام قيم enum داخل `if`/`match`.
- [`objects.md`](./objects.md) — القواميس (map) التي يُبنى enum فوقها داخلياً.
- [`variables.md`](./variables.md) — `let` لتخزين قيمة حالة enum في متغيّر.
