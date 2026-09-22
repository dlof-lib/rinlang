# RIN_ELEMENTS.md — دليل `@element.*` الكامل

> هذا الملف كان مُشارًا إليه من عدة تعليقات في الكود (`rin_indsin_strand.h`،
> `examples/elements_extended_demo.rin`) على أنه المرجع الكامل لعناصر `@element.*`،
> لكنه لم يكن موجودًا فعليًا. كل جدول هنا مبنيّ على قراءة فعلية لـ
> `strandKindFromTag()` (`app/src/main/cpp/indsin/rin_indsin_strand.h`) — أسماء
> الوسوم والبدائل (aliases) هنا هي نفسها المقبولة في الكود حرفيًا، وليست تخمينًا.

## الفكرة الأساسية

`@element.<kind>=<name>` ... `.end/element` يصف **عنصر واجهة وظيفيًا بحتًا** — بلا أي
تنسيق بصري مدمج في مصدره. أي محاولة لكتابة `color=`/`radius=`/`padding=`/`size=`
(وأخواتها، انظر القائمة الكاملة أدناه) داخل `@element` تُحذَف فعليًا عند البناء عبر
`sanitizeElements()` — التنسيق البصري مكانه `@loop` (عبر خصائص `element_*` التي
تُورَّث لكل عنصر بداخله) لا `@element` نفسه. راجع
[`containers.md`](./containers.md#element-و-loop-و-التنسيق-البصري) لشرح كامل لهذا
الفصل الثلاثي (`@container` يملك السلوك، `@loop` يملك الشكل، `@element` يملك الوظيفة
فقط).

`@element` يعمل الآن **كجذر شاشة مستقل** أيضًا (وليس فقط متداخلاً داخل `@view`/`@loop`)
— انظر قسم "دمج @element بـ indsin" في [`indsin.md`](../indsin.md).

```rin
@element.button=run
  text="Run";
.end/element
```

الوسم بعد `@element.` غير حسّاس لحالة الأحرف تمامًا مثل `@view.` (`Button`/`button`
كلاهما يُقبَل). كل صف في الجداول أدناه يسرد كل الأسماء البديلة (aliases) المقبولة
لنفس العنصر، كما يقبلها `strandKindFromTag()` فعليًا.

## الفئة الأساسية

| الوسم (وبدائله) | الوصف |
|---|---|
| `text` | نص. |
| `image` | صورة. |
| `button` | زر. |
| `card` | بطاقة (حاوية بصرية). |
| `column` | تكديس عمودي. |
| `row` | تكديس أفقي. |
| `stack` | تكديس فوق بعضه (Z-order). |
| `divider` | خط فاصل. |
| `box` / `container` / `panel` / `frame` | حاوية تخطيط عامة عديمة الشكل. |
| `grid` | شبكة أعمدة/صفوف. |
| `wrap` | صف يلتف تلقائيًا عند نفاد العرض. |
| `spacer` | مسافة مرنة فارغة (لا يرسم شيئًا). |

## هيكل التطبيق والوسائط

| الوسم | الوصف |
|---|---|
| `header` / `topbar` / `bottombar` | أشرطة أعلى/أسفل الشاشة. |
| `drawer` / `sidebar` | درج جانبي منزلق (`sidebar` بديل اسمي لـ`drawer`). |
| `menu`, `menuitem` | قائمة منسدلة وعناصرها. |
| `table`, `tablerow` | جدول وصفوفه. |
| `video`, `audio`, `webview` | وسائط ومتصفّح مضمَّن. |
| `scaffold` | حاوية الصفحة الكاملة (شريط علوي + محتوى + شريط سفلي + درج). |
| `splash` | شاشة بداية. |
| `banner` | شريط إشعار قابل للإغلاق (`closable=true` يُصنِّع زر إغلاق حقيقيًا). |
| `dialog` / `popup` | نافذة حوار منبثقة (`popup` بديل اسمي لـ`dialog`). |
| `tooltip` | تلميح صغير. |
| `object` | بطاقة عرض حيّة لقيمة `.object(...)` مُسجَّلة (`source=`). |

## الحقول والإدخال

| الوسم | الوصف |
|---|---|
| `input` | حقل إدخال سطر واحد. |
| `textarea` | حقل إدخال متعدد الأسطر. |
| `badge` | شارة صغيرة (Badge). |
| `progress` | شريط تقدّم. |
| `checkbox` | مربّع اختيار. |
| `switch` | مفتاح تبديل. |
| `avatar` | صورة رمزية دائرية/مربّعة. |
| `tabs`, `tabitem` | تبويبات وعناصرها. |
| `icon`, `iconbutton` | أيقونة، وزر يحمل أيقونة. |
| `link` | نص قابل للنقر (يُقاس كصندوق Text). |
| `radio` | زر اختيار دائري (نسخة دائرية من Checkbox). |
| `slider` | شريط تمرير قيمة (Progress بمقبض). |
| `search`, `select`, `file`, `date`, `time` | نفس صندوق Field الحدودي الذي يستخدمه Input/TextArea، بدلالات مختلفة. |
| `code_editor` | محرّر أكواد (صندوق Field متعدد الأسطر بخط أحادي المسافة). |
| `calculator` | آلة حاسبة تفاعلية جاهزة. |
| `list`, `listitem` | قائمة وصفوفها. |

## توسعة مكتبة UI/UX (الأحدث)

| الوسم (وبدائله) | الوصف |
|---|---|
| `tag` / `chip` | شريحة (Chip) نصّية، `removable=true` يُصنِّع زر إغلاق حقيقيًا. |
| `kbd` / `key` | صندوق مفتاح لوحة مفاتيح صغير. |
| `rating` | تقييم نجوم (`max=`, `value=`)، للقراءة فقط. |
| `skeleton` | صندوق نائب تحميل ثابت. |
| `spinner` / `loader` | حلقة تحميل (قوس حقيقي — انظر §القوس أدناه). |
| `steps` / `stepper`, و`stepitem` / `step` | مؤشّر خطوات (`current=` على الأب يُحدِّد حالة كل StepItem تلقائيًا). |
| `timeline`, و`timelineitem` / `timelineevent` | خط زمني عمودي (`date=`/`title=`/`desc=` لكل عنصر). |
| `breadcrumb` / `breadcrumbs` | مسار تنقّل (`items="A,B,C"`). |
| `pagination` / `pager` | ترقيم صفحات تفاعلي (`current=`/`total=`). |
| `donutchart` / `donut` / `piechart` | مخطط دائري حقيقي (`data="Label:Value,..."`, `colors=`, `centerLabel=`). |

> **ملاحظة أمانة على القوس/المنحنى**: راسم المحرّك (Dye) يملك الآن عملية قوس حقيقية
> (`DrawOp::STROKE_ARC`) تستخدمها `spinner`/`donutchart` — لم تعد تقريبًا بمربعات.
> الدوران الفعلي في Spinner مصدره مضيف العرض (مثلًا `IndsinFabricView.kt` على
> أندرويد عبر `Canvas.drawArc` الحقيقي + مؤقّت دوران)، بينما محرّك C++ نفسه (Dye)
> ينتج إطارًا ثابتًا واحدًا فقط في كل استدعاء (لا يوجد أنيميشن داخل Dye نفسه).

## ما لا يُقبَل داخل `@element`

هذه المفاتيح تُحذَف تلقائيًا من أي `@element` (سواء كان جذر شاشة أو متداخلاً) —
ضعها على `@loop` الأب بدلًا من ذلك (عبر `element_<key>=` لتوريثها لكل عنصر، أو
مباشرة على `@loop` نفسه لخصائص الشاشة العامة مثل `width=`/`height=`/`background=`):

```
x, y, width, height, min_width, max_width, min_height, max_height,
color, background, border, radius, padding, margin, opacity, shadow,
font, font_size, text_size, size, align, valign,
margin_left, margin_top, margin_right, margin_bottom, screen
```

## مثال حقيقي (مُتحقَّق منه: يبني ويُصمَّم فعليًا)

```rin
@container=app
    @element.button=run
        text="Run";
    .end/element

    on.run.click=runCode();
.end/container

fun runCode() {
    print "Rin UI";
}

@loop=canvas
    width=390;
    height=700;
    background="#ffffff";
    element_color="#111111";
    element_text_size=16;

    @element.text=title
        text="Rin Elements";
    .end/element

    @element.donutchart=usage
        data="Design:40,Development:60";
    .end/element
.end/loop
```

## انظر أيضًا
- [`containers.md`](./containers.md) — `@container`/`@element`/`@loop` بالتفصيل، والفصل
  الثلاثي بين السلوك/الشكل/الوظيفة.
- [`../indsin.md`](../indsin.md) — محرّك التنفيذ الذي يحوّل هذه العناصر إلى بكسلات فعلية.
