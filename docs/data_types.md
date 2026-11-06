# أنواع البيانات في الحاويات — `containerType` و`container`

محرّك أنواع واحد (`rin_datatypes.h`) يخدم **حقول `containerType`** و**الحاويات العادية** (`setFieldType`) و`isType`.
إضافة بحتة: الأنواع القديمة (`number` `string` `bool` `array` `map` `set` `any`) تعمل كما كانت، وكل كود قديم لا يتأثر.

## الأنواع
| النوع | يقبل |
|---|---|
| `number` | أي رقم (كما كان) |
| `int` | رقم صحيح (`5` نعم، `5.5` لا) |
| `uint` | صحيح غير سالب |
| `float` | رقم منتهٍ (ليس NaN/inf) |
| `string` · `char` | نص · محرف واحد |
| `bool` · `function` · `nil` | منطقي · دالة · `nil` |
| `array` · `set` · `map` | مصفوفة · مجموعة · خريطة |
| `email` `url` `uuid` `date` `time` `datetime` `phone` `color` `ip` | **نص** بالتنسيق المطلوب (`date` = `YYYY-MM-DD` مع فحص الكبيس · `color` = `#rgb` / `#rrggbb` ...) |

أسماء مستعارة داخل `{type: ...}`: `integer` `double` `decimal` `text` `str` `boolean` `list` `object` `fun` `null`.

## صيغ مركّبة
| الصيغة | المعنى |
|---|---|
| `int?` | اختياري: يقبل `nil` أيضاً |
| `int\|string` | اتحاد: أحد النوعين |
| `array<int>` · `set<string>` | عناصر من نوع معيّن (يُفحص كل عنصر، والخطأ يذكر موضعه `[1]`) |
| `map<int>` · `map<string,int>` | قيم (أو مفتاح وقيمة) من نوع معيّن |
| `enum(a,b,c)` | قيم محدّدة (نصوص أو أرقام أو منطقية) |
تتداخل بحرية: `array<map<string,int>>?` · `int\|array<int>`.

## في `containerType`
```rin
defineContainerType("User", {
    id:    {type: "uint", required: true},
    email: {type: "email", required: true},
    born:  "date",                                  // نص نوع مباشر
    tags:  {type: "array<string>", default: [], max: 2},   // max هنا = أقصى عدد عناصر
    nick:  {type: "string?", min: 2},               // min هنا = أقل عدد محارف
    level: {type: "int|string", default: 1}
});
newContainer("User", "u1", {id: 1, email: "a@b.com"});
setField("u1", "id", 1.5);      // خطأ E0004: القيمة 1.5 ليست int (عدد صحيح)
```
- نص النوع المباشر (`born: "date"`) يُعدّ نوعاً إن كان اسماً قياسياً أو صيغة مركّبة. **لقيمة افتراضية نصية تطابق اسم نوع** اكتب `{default: "date"}`.
- `min`/`max`: للأرقام = القيمة، ولـ`string`/`array`/`set`/`map` = **الطول** (محارف لا بايتات).
- القيمة الافتراضية تُفحص عند التعريف: `{type:"int", default: 1.5}` يرفض `defineContainerType`.
- `containerTypeInfo` و`fieldTypes` تعيدان النص القياسي للنوع (`boolean` ← `bool`، `integer` ← `int`).

## في الحاويات العادية
```rin
@container=shop
    state price = 10;
.end/container
setFieldType("shop", "price", "float");   // يفشل إن لم تطابق القيمة الحالية
setField("shop", "price", "free");        // خطأ E0004
```
| الدالة | تفعل |
|---|---|
| `setFieldType(container, field, type)` | تعلن نوع حقل (يمكن إعلانه قبل وجوده). إعادة الإعلان تستبدل النوع |
| `removeFieldType(container, field)` | تزيل الإعلان؛ `true` إن كان موجوداً |
| `fieldTypes(container)` | `{حقل: نوع}` — يشمل حقول `containerType` الموروثة |
| `isType(value, type)` | `true` إن طابقت القيمة النوع |
| `typeError(value, type)` | `nil` إن طابقت، وإلا نص السبب |
| `dataTypes()` | أسماء الأنواع الأساسية المدعومة |
| `validateContainer(name)` | يشمل الآن الأنواع المعلنة بـ`setFieldType` |

## أين يُفحص؟
نفس مسارات `containerType`: `setField` / `container.set` / `setState` (ومنها `input(prompt, "name")`) ، كذلك `newContainer` و`@name.Type`.
**حدود:** الإسناد المباشر داخل جسم `@container` (`x = 5;`) لا يمرّ بالفحص؛ استعمل `validateContainer`. تراجع `@tx` لا يعيد الفحص. الأنواع المعلنة بـ`setFieldType` تُزال مع `destroyContainer`.

الاختبار: `tests/print/data_types.rin`.
