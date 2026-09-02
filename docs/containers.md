# الحاويات (Containers)

> راجع أيضاً: [`objects.md`](./objects.md) للفرق بين كائن مفرد وحاوية كاملة،
> [`variables.md`](./variables.md) و[`control-flow.md`](./control-flow.md) لما
> يمكن كتابته داخل قسم (`Section`) من الحاوية، و[`pipelines.md`](./pipelines.md)
> لحاوية الأنابيب `@container.pipe`.

## 1) حاوية بيانات: `@container`

```rin
@container=my_data
    text title = "بيانات ريـن";
    print title;

    Section=numbers
        let a = 10;
        let b = 4;
        print Addition(a, b);
        print Subtraction(a, b);
    .end/Section

    Translations
        translation lang="ar" text="مرحبا";
        translation lang="en" text="Hello";
    .end/Translations

    file path="data/output.rin";
    installation my_data;
    simplified save path="data/output.min.rin";
.end/container
```

- داخل `@container` يمكن استخدام أي شيء من اللغة الأساسية بلا قيود: [`let`/`text`](./variables.md)،
  [`if`/`while`/`for`](./control-flow.md)، واستدعاء [الدوال](./functions.md).
- `Section=name ... .end/Section` يقسّم الحاوية منطقياً (مثال: قسم للأرقام، قسم للنصوص).
- `Translations ... .end/Translations` يعرّف ترجمات نصية مرتبطة بالحاوية.
- `file`, `installation`, `save` عمليات إخراج/تثبيت للحاوية على القرص.

## 2) خط أنابيب/إحصاء: `@container.pipe`

```rin
@container.pipe=stats
    let data = [10, 20, 30, 40, 50];
    let result = data |> normalize() |> mean();
.end/container.pipe
```

يجمع بين حاوية اسمية وبين [صياغة الأنابيب `|>`](./pipelines.md).

## 3) مجموعات NoSQL: `Containers.Group` / `container.doc`

```rin
@Containers.Group=shop_db
    @container.doc=users
        document id="u1" fields={ name: "سارة", age: 28, city: "الرياض" };
        document id="u2" fields={ name: "أحمد", age: 35, city: "جدة" };
    .end/container.doc
.end/Containers.Group

print queryDocs("users", "city", "الرياض");
print insertDoc("users", "u3", { name: "منى", city: "الرياض" });
```

- كل `document` هو [قاموس/كائن](./objects.md#3-الكائن-كقاموس-حرفي) بمعرّف `id` وحقول `fields={...}`.
- `queryDocs` و`insertDoc` دوال مكتبة قياسية للبحث والإدراج (انظر [`standard-library.md`](./standard-library.md)).

## 4) الحاويات والشروط معاً

بما أن جسم أي `Section` أو `@container` هو مجموعة جمل عادية، يمكن استخدام
[الشروط والحلقات](./control-flow.md) كاملة بداخله:

```rin
@container=report
    Section=summary
        for (let i = 0; i < count; i = i + 1) {
            if (items[i]["active"]) { print items[i]["name"]; }
        }
    .end/Section
.end/container
```

## 5) المفهوم الجامع: `@make` (سابقاً `@Everything`)

> **تسمية:** كان هذا المفهوم يُسمّى `Everything`. الاسم الرسمي الحالي هو `make` (بمعنى "أنشئ شيئًا
> قابلًا للتنفيذ أو الاستخدام")، مع ثلاث صيغ متكافئة تماماً بلا أي فرق دلالي:
> `@make=name`، `@Rin.make=name` (بمساحة اسم صريحة)، و`@container.make=name` (بنفس أسلوب بقية
> `container.xxx`). الاسمان القديمان `@Everything` و`@container.everything` ما زالا مقبولين بالكامل
> كاسم بديل (alias) للتوافق العكسي، بلا أي خطة لإزالتهما — الكود القديم يستمر بالعمل بلا أي تعديل.

```rin
@make=my_app
    fun greet(name) {
        return "Hello " + name;
    }

    @table=users
        row cells=["1", "سارة"];
    .end/table

    @chatbot=bot
        text model = "rin-chat-1";
    .end/chatbot

    make msg = greet("World");
    show msg;
.end/make
```

- `@make=name ... .end/make` (أو `@Rin.make=name ... .end/Rin.make`، أو `@container.make=name ...
  .end/container.make`، أو الاسم القديم `@Everything=name ... .end/Everything`) هي نقطة الدخول
  "الجامعة": بلا أي قيود على جسمها، تماماً كـ [`@container`](#1-حاوية-بيانات-container)
  العادية — يمكن أن تحوي منطقاً كاملاً (`fun`, `let`/`make`, `if`, `while`, `for`)، وأي حاوية أخرى متداخلة
  بأي عدد وترتيب: `table`/`doc`/`Object`/`portal`/`block`/`sticker`/`chatbot`/`AUKT`/`container.pipe`/
  `container.api`/`container.import`، وأي واجهة `view.*`.
- الفرق الجوهري عن الأنواع الأخرى: `make` لا تملك أي منطق تنفيذ خاص بها — هي فقط اسم/غلاف مميّز
  فوق آلية `container` القياسية نفسها (نفس التحليل، نفس التنفيذ). لهذا فهم `@container` أولاً هو شرط
  مسبق لفهم `make`، تماماً كما أن AUKT مبنية فوق نفس المبدأ.
- استخدمها حين تريد بناء "تطبيق/عالم" واحد يجمع منطقاً وبيانات وواجهة وحاويات فرعية متعددة معاً، دون
  تسمية مسبقة لما سيكون بداخله.

### صياغة إنجليزية مبسّطة وسهلة التعلّم (عائلة `make`)

مرافقةً لإعادة التسمية، تتوفر كلمات مرادفة بإنجليزية مبسّطة — كل واحدة مرادف كامل 100% لما يقابلها
(نفس السلوك تماماً، بلا أي فرق)، مصمَّمة لتكون أسهل قراءة للمبتدئ ومتّسقة مع اسم `make` نفسه:

| السهلة | مرادفة تماماً لـ | مثال |
|---|---|---|
| `make x = expr;` | `let x = expr;` | `make total = 10;` |
| `show expr;` | `print expr;` (بكل سماته، حتى `show.log(...)`) | `show "Hello";` |
| `create(kind, name?)` | `spawn(kind, name?)` | `create("table", "t1");` |
| `run(fn, args?)` | `callFn(fn, args?)` | `run(myFn, [1, 2]);` |

هذه الكلمات متاحة في كل البرنامج (وليست حصرية على `@make`)، لكنها تحمل نفس روح المفهوم: كتابة أبسط
وأقرب للإنجليزية العادية لمن يتعلّم اللغة حديثاً.

### إنشاء/برمجة أي شيء وقت التشغيل (دوال ديناميكية)

الصياغة الثابتة أعلاه (`@table=...` إلخ) تتطلّب معرفة الاسم والنوع مسبقاً أثناء الكتابة. للحالات التي
تحتاج فيها اسم/نوع الحاوية أن يُحسبا وقت التشغيل (مثلاً بداخل حلقة)، تتوفر مجموعة دوال قياسية تعمل من
أي مكان في البرنامج (وليس فقط بداخل `@make`، تماماً كـ `callApi`):

```rin
for (make i = 0; i < 3; i = i + 1) {
    make kind = "table";
    if (i == 1) { kind = "doc"; }
    if (i == 2) { kind = "chatbot"; }
    make name = "dyn_" + i;

    create(kind, name);                     // ينشئ حاوية جديدة بنوع/اسم محسوبَين (= spawn)
    setField(name, "index", i);             // يبرمج حقلاً بداخلها (أي قيمة، حتى دالة)
}

show containerNames();                      // كل الحاويات المسجَّلة حالياً
show kindOf("dyn_1");                       // "doc"
show getField("dyn_1", "index");            // 1
show hasField("dyn_1", "missing");          // false
destroyContainer("dyn_2");                  // يحذفها بالكامل
```

- `spawn(kind, name?)` / `create(kind, name?)` — ينشئ حاوية جديدة بنوع حرّ (نص) واسم اختياري، ويُعيد
  اسمها الفعلي. مرِّر `"make"` كـ `kind` لإنشاء حاوية `make` ديناميكياً وقت التشغيل.
- `destroyContainer(name)` / `hasContainer(name)` — حذف/فحص وجود حاوية.
- `setField(container, key, value)` / `getField(container, key)` / `hasField(container, key)` —
  كتابة/قراءة/فحص أي حقل بداخل أي حاوية بالاسم (القيمة قد تكون حتى دالة Rin كاملة).
- `kindOf(name)` — نوع الحاوية (النص الذي مُرِّر لِـ `spawn`/`create`، أو الوسم الرسمي لحاوية عادية،
  مثال `"container.make"`).
- `callFn(fn, args?)` / `run(fn, args?)` — يستدعي أي قيمة دالة (مثلاً واحدة خرجت من `getField`) مع
  مصفوفة وسائط اختيارية.

هذه الدوال هي ما يجعل `@make` "تُنشئ أي شيء وتُبرمج أي شيء" فعلياً وقت التشغيل، لا فقط عبر تعشيش
ثابت مكتوب مسبقاً.

## انظر أيضاً

- [`objects.md`](./objects.md) — الوحدة الأساسية (كائن/مستند) التي تُبنى منها الحاويات.
- [`variables.md`](./variables.md) و[`control-flow.md`](./control-flow.md) — كل ما يُكتب داخل حاوية.
- [`pipelines.md`](./pipelines.md) — `@container.pipe` وسلاسل `|>`.
- [`standard-library.md`](./standard-library.md) — دوال NoSQL/ملفات (`queryDocs`, `insertDoc`, ...).
- [`cross-file-containers.md`](./cross-file-containers.md) — استدعاء حاوية أو عنصر واجهة (`@view.*`) واحد بالاسم من ملف آخر عبر `use ... from` (صفحة مكتوبة بإنجليزية مبسّطة).
- [`language-reference.md`](./language-reference.md) — الخريطة الكاملة للغة.
