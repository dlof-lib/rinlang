# دوال Rin الأصلية الجديدة (Rin 1.0) 
أُضيفت هذه الدوال في `app/src/main/cpp/rin_extra_natives.cpp` (مُضمَّن في نهاية `rin_interpreter.cpp`
فيدخل تلقائياً في كل أهداف البناء). كلها مغطّاة باختبارات فعلية:

```text
rin_run tests/verification/extra_natives.rin
rin_run tests/verification/container_extra.rin
```

كل اختبار يطبع `ALL PASSED (...)` عند النجاح، وأي فشل يوقف التشغيل برمز خروج `1`.

## فحص الأنواع

| الدالة | الناتج |
|---|---|
| `isNil(v)` · `isNumber(v)` · `isString(v)` · `isArray(v)` · `isMap(v)` · `isFunction(v)` | `true`/`false` (وتكمّل `isBool` الموجودة) |

## الاختبار والتأكيد

| الدالة | الوصف |
|---|---|
| `assert(cond, msg?)` | يرمي خطأ `E0035` إن كانت `cond` غير صحيحة |
| `assertEq(actual, expected, msg?)` | مقارنة تركيبية (عميقة)؛ الخطأ يعرض القيمتين |
| `fail(msg?)` | يرمي خطأ تشغيل فوراً |

## النصوص

| الدالة | الوصف |
|---|---|
| `lastIndexOf(s, sub)` | آخر موضع (بالبايت كـ`indexOf`) أو `-1` |
| `padStart(s, n, pad?)` · `padEnd(s, n, pad?)` | حشو حتى طول `n` محرفاً (يدعم UTF-8 والعربية) |
| `repeat(s, n)` | تكرار النص (الحد الأقصى 10MB) |
| `trimStart(s)` · `trimEnd(s)` | قص المسافات من جهة واحدة |

## المصفوفات

| الدالة | الوصف |
|---|---|
| `slice(x, start, end?)` | شريحة من مصفوفة أو نص (UTF-8)؛ الفهارس السالبة من النهاية؛ لا تعدّل الأصل |
| `reverse(x)` | نسخة معكوسة من مصفوفة أو نص |
| `concat(a, b, ...)` | دمج مصفوفات (وغير المصفوفات تُضاف كعناصر) |
| `flatten(arr, depth?)` | تسطيح بعمق `depth` (الافتراضي 1؛ سالب = كامل) |

## القواميس والمسارات

| الدالة | الوصف |
|---|---|
| `entries(m)` / `fromEntries(pairs)` | تحويل قاموس ⇄ `[[k, v], ...]` |
| `mergeMaps(a, b, ...)` | دمج سطحي، الأخير يغلب |
| `deepMerge(a, b)` | دمج عميق للقواميس المتداخلة |
| `deepCopy(v)` | نسخة عميقة من مصفوفات وقواميس |
| `pickKeys(m, keys)` / `omitKeys(m, keys)` | اختيار أو استبعاد مفاتيح |
| `getPath(v, "a.b.0", default?)` | قراءة آمنة بمسار نقطي أو مصفوفة؛ تُرجع `default`/`nil` عند الغياب |
| `setPath(v, "a.b.c", value)` | كتابة في المكان وإنشاء القواميس الوسيطة؛ تُرجع الجذر |

```rin
let cfg = {};
setPath(cfg, "db.host", "localhost");
print getPath(cfg, "db.host");            // localhost
print getPath(cfg, "db.port", 5432);      // 5432
```

## الحاويات (`container.*`)

| الدالة | الوصف |
|---|---|
| `container.ensure(kind, name)` | تنشئ الحاوية إن لم توجد، وتُرجع اسمها دائماً |
| `container.clone(src, dst)` | نسخة عميقة من الحقول والنوع (بلا الأبناء)؛ `false` إن كان الهدف موجوداً |
| `container.rename(old, new)` | تعيد التسمية وتحدّث الشجرة والمجموعات والأحداث ودورة الحياة |
| `container.getOr(name, field, default)` | قراءة حقل بقيمة افتراضية |
| `container.incr(name, field, by?)` | زيادة حقل رقمي (يبدأ من 0) وتُرجع القيمة الجديدة |
| `container.append(name, field, value)` | إضافة لمصفوفة حقل (تُنشأ إن غابت)؛ تُرجع الطول |
| `container.update(name, field, fn)` | `field = fn(field)` وتُرجع القيمة الجديدة |
| `container.pick(name, fields)` / `container.omit(name, fields)` | حقول مختارة/مستبعدة كقاموس |
| `container.toJson(name)` / `container.fromJson(name, json, overwrite?)` | تصدير/استيراد الحقول (الدوال تُتجاهل) |
| `container.diff(a, b)` | `{same, added, removed, changed:{f:{from,to}}}` |
| `container.each(kind, fn)` | تستدعي `fn(name)` لكل حاوية من النوع؛ تُرجع العدد |
| `container.pluck(kind, field)` | `{اسم: قيمة الحقل}` |
| `container.sum(kind, field)` | مجموع حقل رقمي عبر الحاويات |
| `container.groupBy(kind, field)` | `{قيمة: [أسماء]}` |
| `container.query(kind, conds?, opts?)` | استعلام (أدناه) |
| `container.tree(name)` | شجرة متداخلة `{name, kind, fields, children}` |

`kind` قد تكون `"*"` أو `""` لكل الأنواع. الحاوية غير الموجودة تُرجع `nil` لدوال القراءة و`false` لدوال الكتابة
(نفس سياسة `container_pro.md`).

### `container.query`

```rin
container.query("item", {cat:"tech"});                       // مساواة
container.query("item", {price:{gte:10, lt:40}});             // مقارنات
container.query("item", {cat:{in:["food","tech"]}});          // انتماء
container.query("item", {name:{startsWith:"a"}});             // نصوص
container.query("item", nil, {sortBy:"price", desc:true, limit:3});
```

العوامل: `eq` `ne` `gt` `gte` `lt` `lte` `in` `contains` `startsWith` `endsWith` `exists`.
عامل غير معروف يجعل الشرط فاشلاً بدل أن يُتجاهل بصمت. النتائج مرتّبة أبجدياً ما لم يُحدَّد `sortBy`.

## أداة `rin_run`

أصبح `tools/rin_run.cpp` يُرجع رمز خروج `1` عند أي خطأ تشغيل (سابقاً كان `0` دائماً، فتمرّ الاختبارات الفاشلة في CI).
