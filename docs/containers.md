# containers.md — دليل `@container` / `@element` / `@loop`

> كان هذا الملف مُشارًا إليه من `indsin.md`، ومن تعليق التوثيق أعلى `ContainerStmt`
> في `app/src/main/cpp/rin_ast.h` مباشرة، لكنه لم يكن موجودًا فعليًا. المحتوى هنا
> مبنيّ على قراءة فعلية للمحلِّل (`rin_parser.cpp`) وشجرة AST (`rin_ast.h`) والأمثلة
> الحقيقية الموجودة في `examples/`، وتحقّقتُ من الأمثلة القابلة للتحقق عبر بناء
> واختبار فعليَّين قبل كتابتها هنا.

## الفكرة: فصل ثلاثي

كل واجهة في Rin مبنية من ثلاث طبقات منفصلة عمدًا، كل واحدة "تملك" شيئًا واحدًا فقط:

| البيان | يملك | لا يملك |
|---|---|---|
| `@container` | **السلوك**: دوال، حالة (`state`)، خُطّاف دورة الحياة، ربط الأحداث | أي شكل بصري |
| `@loop` | **الشكل**: حجم الشاشة، الألوان، الخطوط، الافتراضيات الموروثة | السلوك/المنطق |
| `@element` | **الوظيفة فقط**: أي عنصر UI هو (زر، حقل، مخطط...) وبياناته الوظيفية | أي تنسيق بصري في مصدره |

هذا الفصل ليس تعليقًا فقط — المحلِّل والمحرّك يفرضانه فعليًا: أي خاصية "بصرية"
(`color=`, `radius=`, `padding=`...) مكتوبة داخل `@element` تُحذَف عند البناء
(`sanitizeElements()` في `rin_indsin_pipeline.h`)، بينما `@loop` نفسه حرّ تمامًا في
حمل أي خاصية بصرية يريدها (`width=`, `background=`...) لأن دوره بالتحديد هو الشكل.

## `@element`

```rin
@element.<kind>=<name>
    attribute = value;
    ...
    // يمكن تعشيش @element أخرى بداخله أيضًا
.end/element
```

- `<kind>` هو نوع العنصر (`button`, `text`, `input`, `donutchart`, ...) — القائمة
  الكاملة مع كل الأسماء البديلة في [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md).
- `<name>` اختياري — يُستخدَم لربط الأحداث لاحقًا (`on.<name>.<event>=...;`) ولتمييز
  العنصر عند القراءة/التتبّع.
- غير حسّاس لحالة الأحرف: `@element.Button` و`@element.button` متطابقان تمامًا.
- **يعمل كجذر شاشة مستقل الآن أيضًا** (وليس فقط متداخلاً داخل `@view`/`@loop`) —
  إن كان `@element` هو البيان الوحيد على المستوى الأعلى في الملف، يصبح هو الشاشة
  نفسها. عند وجود `@view`/`@loop` معه، هما يفوزان دائمًا كجذر (سلوك لم يتغيّر).
  التفاصيل الكاملة والاختبارات في قسم "دمج @element بـ indsin" أسفل
  [`../indsin.md`](../indsin.md).

## `@loop`

```rin
@loop=<name>              // النوع الافتراضي: Column
@loop.<kind>=<name>        // أو حدِّد نوع تخطيط الشاشة صراحةً (مثلًا Column/Row/Stack)
    width = 390;
    height = 700;
    background = "#ffffff";

    // element_<key>= يُورَّث تلقائيًا لكل @element مباشر أو متداخل بداخل هذا @loop
    // (ما لم يحدِّد العنصر نفسه نفس المفتاح صراحةً — الأولوية دائمًا للعنصر):
    element_width = 320;
    element_height = 44;
    element_color = "#111111";
    element_background = "#1e1f28";
    element_text_size = 16;
    element_radius = 8;
    element_padding = 12;
    element_font = "Inter";
    element_margin = 8;

    @element.text=title
        text = "شاشتي";
    .end/element
.end/loop
```

`@loop` هو "اللوحة/الشاشة" التي تحمل حجمها ولونها وخطوطها الافتراضية، ثم تُورِّث كل
ذلك تلقائيًا لأي `@element` بداخلها عبر مفاتيح `element_*` — دون أن يحتاج كل عنصر
لتكرار نفس القيم. `screen=` اختصار حجم شاشة جاهز (`phone_small` 360×640،
`phone` 390×844، `phone_large` 430×932، `tablet` 768×1024، `tablet_large`
1024×1366، `desktop` 1280×800، `desktop_wide` 1440×900) — يُستخدَم فقط على `@loop`
نفسه (`screen=` مرفوضة داخل `@element`، انظر القائمة السوداء في RIN_ELEMENTS.md).

## `@container`

```rin
@container=<name>              // النوع الافتراضي: PLAIN
@container.<kind>=<name>        // أو نوع حاوية متخصِّص (جدول أدناه)
    // حالة قابلة للمراقبة: أي إسناد لاحق لها يُطلق تلقائيًا on update(prev) إن وُجدت
    state counter = 0;

    // خُطّاف دورة الحياة (كلها اختيارية):
    on init()    { print "created"; }
    on mount()   { print "mounted"; }
    on update(prev) { print "counter changed from " + prev; }
    on destroy() { print "destroyed"; }
    on error(e)  { print "error: " + e; }

    fun increment() { counter = counter + 1; }

    @element.button=inc
        text = "+1";
    .end/element

    on.inc.click = increment();
.end/container
```

- **الحالة (`state`)**: نفس `let` تمامًا، إلا أن أي إسناد لاحق لنفس الاسم (من داخل
  الحاوية أو من دالة معرَّفة بداخلها) يُطلق تلقائيًا خُطّاف `on update(prevState)` إن
  كان معرَّفًا لنفس الحاوية.
- **الأقنعة (`mask=`)**: هوية منطقية قابلة لإعادة الاستخدام للحاوية، مستقلة عن الاسم
  — تُستخدَم لربط استعلامات RCSQL المسمّاة (`sql(mask)`) وغيرها.
- **كتلة السياسة (Policy، اختيارية بالكامل)**: نفس أربع كلمات Make Unit تمامًا —
  `use`/`need`/`allow`/`deny` + `strict`/`version`/`description`. حاوية لا تستخدم أيًا
  منها تسلك تمامًا كما تسلك اليوم (permissive) — التحقّق (`validateContainerPolicy`
  في `rin_make.cpp`) يُطبَّق فقط إن استُخدمت كلمة واحدة منها فعليًا.

### أنواع الحاويات المتخصِّصة (`ContainerKind`)

كل نوع أدناه هو `@container.<kind>=name ... .end/container.<kind>` (بديل مختصر:
بعضها يقبل أيضًا `@<kind>=name` مباشرة — انظر التعليق الخاص بكل نوع في `rin_ast.h`
إن احتجت الصياغة المختصرة بالضبط). هذا سرد موجز لدور كل نوع، وليس مرجعًا كاملاً
لسلوكه — للتفاصيل الكاملة والقيود الدقيقة لكل نوع، اقرأ `rin_interpreter.cpp`
(تنفيذ `ContainerStmt`) و`rin_container_sql.h` (لنوع `sql`) مباشرة، فهما المصدر
الوحيد الموثوق لكل التفاصيل الدقيقة لكل نوع في هذا الإصدار:

| النوع | الدور المختصر |
|---|---|
| `pipe` | خط أنابيب بيانات/إحصاء. |
| `data` | حاوية بيانات نقية — بلا دوال ولا حاويات متداخلة. |
| `api` | نقاط API وهمية (`route ...`)، تُستدعى عبر `call()`. |
| `import` | يستورد وينفّذ فعليًا محتوى ملف `.rin` آخر. |
| `table` | جدول (صفوف `row` + `style` اختياري). |
| `doc` | حاوية NoSQL (مستندات `document`). |
| `object` | كائن ببيانات نقية (حقول حرة + `style` اختياري). |
| `portal` | حاوية تنسيق تحمل نمطًا (`style`) عامًا. |
| `block` | كتلة واجهة جاهزة (شريط علوي/سفلي/زر...). |
| `sticker` | بطاقة هوية بصرية جاهزة. |
| `aukt` | جداول معرفة آلية (Automated Knowledge Tables). |
| `chatbot` | حاوية روبوت محادثة. |
| `sql` | استعلام RCSQL مُسمّى — يُستدعى لاحقًا بقناع الحاوية عبر `sql(mask)`/`sqlOne(mask)`/`sqlCount(mask)` بدل تكرار نص الاستعلام. |
| `make` | بنفس أسلوب `container.xxx` الأخرى، لكن بمعنى Make Unit. |
| `everything` | حاوية تجميعية شاملة. |

### `@Containers.Group` و`@Volume`

`@Containers.Group=name ... .end/Containers.Group` يجمع عدة حاويات/مجموعات فرعية
تحت سياسة واحدة (نفس كلمات `use`/`need`/`allow`/`deny` لكن مُجمَّعة على مستوى الشجرة
كاملةً بدل حاوية واحدة). `@Volume=name ... .end/Volume` تعشيش كامل بنفس حرّية
`@container` العادية — غرضها تنظيمي بحت (تجميع منطقي)، لا قيد إضافي على المحتوى.

## مثال كامل مُتحقَّق منه (الفصل الثلاثي كاملاً)

```rin
@container=app
    @element.button=saveBtn
        text = "حفظ";
    .end/element

    on.saveBtn.click = saveForm();
.end/container

fun saveForm() {
    print "Saved";
}

@loop=canvas
    width = 390;
    height = 900;
    background = "#ffffff";
    element_width = 320;
    element_height = 44;
    element_color = "#111111";
    element_text_size = 16;

    @element.text=title
        text = "Ready Elements";
    .end/element

    @element.row=actions
        direction = "rtl";
        @element.button=saveBtn
            text = "حفظ";
        .end/element
    .end/element
.end/loop
```

(نفس بنية `examples/elements_extended_demo.rin` الموجود فعليًا في المستودع.)

## انظر أيضًا
- [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md) — كل أنواع `@element.*` وبدائلها الاسمية.
- [`../indsin.md`](../indsin.md) — كيف يحوّل المحرّك (indsin) هذه البيانات إلى
  شجرة مقاسة ثم بكسلات فعلية، وقسم "دمج @element بـ indsin" لتفاصيل جذر الشاشة.
- `app/src/main/cpp/rin_ast.h` — البنية الكاملة (`ContainerStmt`, `ViewStmt`,
  `LifecycleHookStmt`, `StateDeclStmt`) بتعليقات مصدرية مفصَّلة لكل حقل.
