# Services — حاوية الخدمات

> تكمّل مفهوم `container` (خارطة الطريق في آخر الملف).
> إضافة بحتة: لا يتغيّر شيء في الحاويات ولا في الكود القديم. المحرّك في `app/src/main/cpp/rin_container_services.cpp`.

## الفكرة
`@container` في Rin يحمل **السلوك والحالة**. أما «حاوية الخدمات» فتحمل **طريقة بناء الأشياء وربطها ببعض**:
تسجّل «وصفة» لكل خدمة، ثم تطلبها بالاسم فتُبنى عند الحاجة فقط، وتجد تبعياتها بنفسها.
الاثنان يلتقيان في `Services.fromContainer(...)`: خدمة = لقطة حيّة من أي `@container`.

```rin
Services.singleton("config", fun() { return {env: "prod"}; });
Services.singleton("db", fun(config) { return {env: config["env"]}; });   // config يُحقَن تلقائياً
Services.bind("repo", fun(db) { return {db: db}; });

let repo = Services.get("repo");      // يبني config ثم db ثم repo (كسولاً)
```

## التسجيل
| الدالة | تفعل |
|---|---|
| `Services.bind(name, factory)` | خدمة **عابرة**: يُنفَّذ المصنع عند كل `get`/`make` |
| `Services.singleton(name, factory)` | خدمة **مشتركة**: تُبنى كسولاً عند أول طلب ثم تُخزَّن |
| `Services.instance(name, value)` | قيمة جاهزة (تعيد القيمة) |
| `Services.alias(alias, name)` | اسم بديل؛ سلاسل البدائل مسموحة، والبديل الذاتي/الدائري يُرفض (`E0030`) |
| `Services.fromContainer(name, container?)` | خدمة = **لقطة حيّة** (`container.snapshot`) من `@container`؛ الاسم الثاني اختياري (الافتراضي = نفس الاسم). تُقرأ الحاوية وقت الطلب فيمكن تعريفها لاحقاً |

إعادة تسجيل اسم موجود تستبدله بالكامل (وتُسقط النسخة المخزَّنة والمُزخرِفات)، وتبقى وسومه.

## الحل
| الدالة | تفعل |
|---|---|
| `Services.has(name)` | هل الخدمة مسجَّلة؟ — تتبع البدائل؛ بديل يشير إلى اسم غائب = `false` |
| `Services.get(name)` | يبني الخدمة أو يعيد المخزَّنة. غير مسجَّلة ⇒ خطأ `E0015` مع اقتراح: `الخدمة 'confg' غير مسجّلة — هل تقصد 'config'؟` |
| `Services.make(name, params?)` | مثل `get` مع `params` = `{اسم_المعامل: قيمة}` تغلب الحقن التلقائي (لهذا المصنع فقط). مع singleton مُنشأ سابقاً يعيد المخزَّن |
| `Services.call(fn, params?)` | ينادي أي دالة ويحقن معاملاتها: `Services.call(fun(db, version) { ... })` |

### الحقن التلقائي (Autowiring)
معاملات المصنع تُحلّ **بأسمائها**: أولاً من `params` الصريحة، ثم من الخدمة المسجَّلة بنفس الاسم (أو بديلها).
معامل بلا قيمة ولا خدمة ⇒ خطأ واضح يسمّي المعامل (`E0035`). لا قيم افتراضية: عدد الوسائط يطابق المعاملات كالمعتاد في Rin.

### كشف الدوران
`a ← b ← c ← a` لا يُسبِّب Stack Overflow؛ يُرمى `E0030` بسلسلة كاملة:
`تبعية دائرية أثناء حل الخدمة 'a': a → b → c → a`. المكدّس يُنظَّف بعد الخطأ فتبقى الحاوية صالحة.

## المُزخرِفات (Decorators)
`Services.extend(name, fn)` — `fn(value, ...)`: المعامل الأول القيمة المبنية والبقية تُحقَن. تُطبَّق بترتيب التسجيل بعد كل بناء.
- ترجع قيمة ⇒ تحلّ محلّ الخدمة · ترجع `nil` ⇒ التعديل بالمرجع (map/array) والقيمة كما هي.
- خدمة مُنشأة فعلاً (instance أو singleton محلول) ⇒ يُطبَّق المُزخرِف **فوراً** على النسخة الحالية (ويُحفظ للـsingleton لإعادة البناء بعد `reset`).

## الوسوم
| الدالة | تفعل |
|---|---|
| `Services.tag(tag, names)` | يضيف اسماً أو مصفوفة أسماء للوسم (بلا تكرار)؛ يعيد عدد أعضائه. الخدمة يمكن ألا تكون مسجَّلة بعد |
| `Services.tagged(tag)` | مصفوفة **القيم المحلولة** بترتيب الإضافة؛ وسم مجهول = `[]` |
| `Services.tags()` | أسماء الوسوم غير الفارغة |

## الإدارة والاستعلام
| الدالة | تفعل |
|---|---|
| `Services.forget(name)` | يحذف التعريف (والنسخة، ويُزيله من الوسوم)؛ `true`/`false` |
| `Services.reset(name)` | يُسقط **النسخة المخزَّنة** فقط ويُبقي التعريف (لإعادة بناء singleton) |
| `Services.flush()` | يمسح كل شيء ويعيد عدد ما مُسح |
| `Services.names()` · `Services.count()` | كل الأسماء (بدائل أيضاً) مرتّبة · عددها |
| `Services.info(name)` | `{name, kind, shared, resolved, target, tags, extenders}`؛ `kind` ∈ `bind singleton instance alias container`؛ `nil` إن غابت |

## ملاحظات
- **حالة كل مفسّر مستقلة**: لا تعارض بين جلسات المضيف المتعددة (indsin/RinVM).
- الأخطاء كلها `RinError` عادية تُلتقط بـ `try/catch` وبـ `container.attempt`.
- اسم `Services` (حرف كبير) كأسماء `Set` و`Env`: متغيّر عادي بنفس الاسم يغلب (لا يكسر أي كود قديم).
- الاختبار: `tests/print/container_services.rin`.

## خارطة الطريق
1. ✅ **Services** — حاوية الخدمات (هذا الملف)
2. ✅ `Autoload` — تحميل كسول للحاويات/المكتبات `.rcl`
3. ✅ **خُطّافات سحرية** — `on get/set/has/drop/call/invoke` (docs/container_magic.md)
4. `ArrayAccess` / `Countable` / `IteratorAggregate` — الحاوية تُفهرَس وتُعَدّ وتُكرَّر
5. Traits وعقود (interfaces) للحاويات — `container.use` / `container.implements`
6. حقول `readonly` وتعدادات (enum) — حماية حقول الحاوية (✅ الجزء الأول: `state fixed` · docs/EXTRA13.md)
7. `filter_var` — تحقّق وتنقية قيم الحقول (email/int/url/ip/regex)
8. `serialize` / `var_export` / `__sleep` / `__wakeup` / `__clone`
9. هياكل SPL — `SplStack/Queue/Heap/PriorityQueue/ObjectStorage/WeakMap`
10. مشغّل اختبارات مخصّص لمجلدات الاختبار الكبيرة
