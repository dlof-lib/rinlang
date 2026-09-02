# قواعد صياغة Rin (Syntax)

> هذه الصفحة تحدّد الأساس النحوي المشترك الذي تُبنى عليه كل صفحات المفاهيم في
> [`docs/`](./README.md). ابدأ من هنا إن كنت تريد نظرة سريعة قبل الغوص في كل
> مفهوم على حدة عبر [`language-reference.md`](./language-reference.md).

## عناصر أساسية مشتركة بين كل المفاهيم

| العنصر | الصيغة | التفصيل |
|---|---|---|
| نهاية الجملة | `;` | كل جملة (`let`, `print`, استدعاء دالة, ...) تنتهي بفاصلة منقوطة |
| الكتلة | `{ ... }` | تُستخدم في [الشروط والحلقات](./control-flow.md) و[الدوال](./functions.md) |
| التعليقات | `// ...` | سطر واحد |
| الاستيراد | `@import "path";` أو `@import "path" as alias;` | يربط [المكتبة القياسية](./standard-library.md) بالملف الحالي |
| الفهرسة | `value["key"]` | تعمل على [القواميس](./variables.md#4-القواميسالخرائط-maps) و[الكائنات](./objects.md) و[المصفوفات](./variables.md#3-المصفوفات-arrays) |

## عوامل التعبير

```
+  -  *  /  %        عمليات حسابية
== != < <= > >=       مقارنة  (انظر control-flow.md)
and  or  !            منطق    (انظر control-flow.md)
|>                    أنبوب   (انظر pipelines.md)
```

## الكلمات المفتاحية للجمل

| كلمة | الصفحة |
|---|---|
| `let`, `text`, `set ... to ...` (سهل التعلّم) | [`variables.md`](./variables.md) |
| `if`, `else`, `when ... otherwise ...` (سهل التعلّم), `plus.condition`, `while`, `for`, `rinopen`, `break`, `continue` | [`control-flow.md`](./control-flow.md) |
| `fun`, `return` | [`functions.md`](./functions.md) |
| `@Object`, `.object("id")` | [`objects.md`](./objects.md) |
| `@container`, `@container.pipe`, `@container.doc`, `Containers.Group`, `Section` | [`containers.md`](./containers.md) |
| `use Name from "path";` (سهل التعلّم) | [`cross-file-containers.md`](./cross-file-containers.md) |

كل هذه الكلمات المفتاحية تشترك في نفس قواعد الكتل `{}` والفواصل المنقوطة أعلاه —
وهذا هو الرابط النحوي الذي يجعل مفاهيم اللغة متجانسة ومترابطة بدل أن تكون جزراً
منفصلة. للاطّلاع على كيفية تفاعل هذه المفاهيم مع بعضها عملياً، راجع
[`language-reference.md`](./language-reference.md).

## انظر أيضاً

- [`language-reference.md`](./language-reference.md) — الخريطة الشاملة والمرجع الكامل.
- [`getting-started.md`](./getting-started.md) — تشغيل أول برنامج Rin.
