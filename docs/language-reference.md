# المرجع الشامل للغة Rin 1.0.0

مرجع Rin 1.0.0 يغطّي التعبيرات، الجمل، المتغيّرات، الدوال، المجموعات، الكائنات،
الحاويات، وتكامل بيئة التشغيل — **وهذه الصفحة هي المحور الذي يربط كل صفحات
المفاهيم ببعضها**: لا يوجد مفهوم في Rin مستقل تماماً عن البقية؛ كل مفهوم يُستخدم
داخل المفاهيم الأخرى، كما يوضّح الجدول والمخطط أدناه.

## خريطة الترابط بين المفاهيم

```
                         ┌────────────────────┐
                         │   syntax.md         │   القواعد النحوية العامة
                         │ (فواصل، كتل، عوامل)  │   (كل ما يلي يستخدمها)
                         └─────────┬───────────┘
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        │                          │                          │
        ▼                          ▼                          ▼
┌───────────────┐        ┌──────────────────┐        ┌────────────────┐
│ variables.md   │◄──────►│ control-flow.md   │◄──────►│ functions.md    │
│ let / text     │  تُفحص  │ if/else/شروط،      │ return │ fun / return    │
│ arrays / maps  │  داخل   │ while/for، شروط     │ داخل   │ recursion       │
└───────┬────────┘        └─────────┬─────────┘        └────────┬────────┘
        │  أساس القاموس              │ تُستخدم                    │ تُبنى منها
        ▼                          ▼                          ▼
┌───────────────┐        ┌──────────────────┐        ┌────────────────┐
│ objects.md     │◄──────►│ containers.md      │◄──────►│ standard-       │
│ @Object،        │ يُغلَّف │ @container،         │ تستدعي │ library.md      │
│ .object("id")  │  في    │ Section، NoSQL     │ دوالها │ math/data/...   │
└───────┬────────┘        └─────────┬─────────┘        └────────┬────────┘
        │                          │                          │
        └──────────────┬───────────┴──────────────┬───────────┘
                        ▼                          ▼
                 ┌──────────────┐         ┌──────────────────┐
                 │ pipelines.md  │◄───────►│ rinflow.md /       │
                 │ |> بين الدوال  │         │ errors.md / api.md │
                 └──────────────┘         └──────────────────┘
```

## جدول الترابط المباشر

| المفهوم | يعتمد على | يُستخدم داخل |
|---|---|---|
| [المتغيّرات](./variables.md) | [`syntax.md`](./syntax.md) | [الشروط](./control-flow.md)، [الدوال](./functions.md)، [الكائنات](./objects.md) |
| [الشروط والحلقات](./control-flow.md) | [المتغيّرات](./variables.md) (كقيم شرط)، عوامل [`syntax.md`](./syntax.md) | [الدوال](./functions.md) (`if` قبل `return`)، [الحاويات](./containers.md) (شروط داخل `Section`) |
| [الدوال](./functions.md) | [المتغيّرات](./variables.md) (وسائط)، [الشروط](./control-flow.md) (منطق داخلي) | [المكتبة القياسية](./standard-library.md)، [الأنابيب](./pipelines.md) |
| [الكائنات](./objects.md) | [المتغيّرات](./variables.md) (أساس القاموس) | [الحاويات](./containers.md) (كمستندات NoSQL)، [الشروط](./control-flow.md) (فحص حقل) |
| [الحاويات](./containers.md) | [الكائنات](./objects.md)، [المتغيّرات](./variables.md)، [الشروط/الحلقات](./control-flow.md) | تخزين/تصدير عبر [`storage.md`](./storage.md) و[`http.md`](./http.md) |
| [الأنابيب](./pipelines.md) | [الدوال](./functions.md) | [الحاويات](./containers.md) (`@container.pipe`)، [`rinflow.md`](./rinflow.md) |
| [المكتبة القياسية](./standard-library.md) | كل ما سبق (كل دالة فيها مبنية من `if`/`while`/`let`) | أي برنامج Rin عبر `@import` |

## مثال واحد يجمع كل المفاهيم

المقتطف التالي (من `samples/showcase.rin`) يستخدم كل مفهوم موثّق أعلاه في برنامج
واحد مترابط:

```rin
@import "lib/math.og.rin";                 // المكتبة القياسية

let x = 10;                                 // متغيّر (variables.md)
text name = "Rin";

if (x > 5) { print "big"; } else { print "small"; }   // شرط (control-flow.md)

fun fib(n) {                                // دالة (functions.md)
    if (n < 2) { return n; }                // شرط داخل دالة
    return fib(n - 1) + fib(n - 2);
}
print fib(10);
print factorial(5);                         // دالة من المكتبة القياسية

let m = { name: "Rin", age: 2 };            // كائن/قاموس (objects.md)

let data = [10, 20, 30];
let result = data |> normalize() |> mean(); // أنبوب (pipelines.md)

@container=my_data                          // حاوية (containers.md)
    Section=numbers
        let a = 10;
        if (a > 5) { print Addition(a, 4); } // شرط داخل حاوية
    .end/Section
.end/container
```

## بقية صفحات التوثيق

| القسم | الصفحات |
|---|---|
| اللغة الأساسية | [`syntax.md`](./syntax.md) · [`variables.md`](./variables.md) · [`control-flow.md`](./control-flow.md) · [`functions.md`](./functions.md) · [`objects.md`](./objects.md) · [`containers.md`](./containers.md) · [`boat.md`](./boat.md) |
| بيئة التشغيل | [`standard-library.md`](./standard-library.md) · [`errors.md`](./errors.md) · [`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) · [`storage.md`](./storage.md) · [`http.md`](./http.md) |
| التنفيذ | [`pipelines.md`](./pipelines.md) · [`rinflow.md`](./rinflow.md) |
| الواجهة | [`banner.md`](./banner.md) · [`android.md`](./android.md) |
| الواجهة البرمجية | [`api.md`](./api.md) |
| البداية | [`getting-started.md`](./getting-started.md) |

## انظر أيضاً

- [`README.md`](./README.md) — فهرس التوثيق الكامل.
- [`getting-started.md`](./getting-started.md) — لتشغيل أول برنامج Rin عملياً.
