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

## 5) المفهوم الجامع: `@Everything`

```rin
@Everything=my_app
    fun greet(name) {
        return "Hello " + name;
    }

    @table=users
        row cells=["1", "سارة"];
    .end/table

    @chatbot=bot
        text model = "rin-chat-1";
    .end/chatbot

    print greet("World");
.end/Everything
```

- `@Everything=name ... .end/Everything` (أو `@container.everything=name ... .end/container.everything`)
  هي نقطة الدخول "الجامعة": بلا أي قيود على جسمها، تماماً كـ [`@container`](#1-حاوية-بيانات-container)
  العادية — يمكن أن تحوي منطقاً كاملاً (`fun`, `let`, `if`, `while`, `for`)، وأي حاوية أخرى متداخلة
  بأي عدد وترتيب: `table`/`doc`/`Object`/`portal`/`block`/`sticker`/`chatbot`/`AUKT`/`container.pipe`/
  `container.api`/`container.import`، وأي واجهة `view.*`.
- الفرق الجوهري عن الأنواع الأخرى: `Everything` لا تملك أي منطق تنفيذ خاص بها — هي فقط اسم/غلاف مميّز
  فوق آلية `container` القياسية نفسها (نفس التحليل، نفس التنفيذ). لهذا فهم `@container` أولاً هو شرط
  مسبق لفهم `Everything`، تماماً كما أن AUKT مبنية فوق نفس المبدأ.
- استخدمها حين تريد بناء "تطبيق/عالم" واحد يجمع منطقاً وبيانات وواجهة وحاويات فرعية متعددة معاً، دون
  تسمية مسبقة لما سيكون بداخله.

## انظر أيضاً

- [`objects.md`](./objects.md) — الوحدة الأساسية (كائن/مستند) التي تُبنى منها الحاويات.
- [`variables.md`](./variables.md) و[`control-flow.md`](./control-flow.md) — كل ما يُكتب داخل حاوية.
- [`pipelines.md`](./pipelines.md) — `@container.pipe` وسلاسل `|>`.
- [`standard-library.md`](./standard-library.md) — دوال NoSQL/ملفات (`queryDocs`, `insertDoc`, ...).
- [`language-reference.md`](./language-reference.md) — الخريطة الكاملة للغة.
