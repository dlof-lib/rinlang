# RIN CONTAINER SQL (RCSQL)

نظام استعلام مصغّر ومميّز خاص بلغة Rin، للبحث/الفلترة/الفرز/التصفّح/التجميع/التعديل بالجملة/
التشخيص داخل مجموعات المستندات القائمة أصلاً في اللغة عبر `@container.doc` / `@doc` (RCSQL
يستعلم/يعدِّل بيانات موجودة، لا يعرِّف نوع تخزين جديداً).

## الفكرة

- **بنية البيانات المُستعلَمة تبقى `@` كما هي دوماً**: تُعرَّف المستندات كالمعتاد بـ
  `@container.doc=name ... document id="..." fields={...}; ... .end/container.doc`.
- **الاستعلام نفسه يُكتَب بخمسة رموز تركيبية فقط**: `/` `:` `&` `()` `#` — بالإضافة لحروف/أرقام
  المعرّفات (لاتينية أو عربية). أي رمز آخر (مثل `|` أو `=` أو `,`) يُرفَض فوراً بخطأ `E0042`.
- **الاستدعاء يكون بالأقنعة (masks)**: يمكن تعريف استعلام RCSQL كحاوية `@` مستقلة قابلة لإعادة
  الاستخدام عبر `@sql`/`@container.sql`، ثم استدعاؤه لاحقاً بقناعها فقط دون تكرار نص الاستعلام.

## الصياغة

```
query      := target ( "&" clause )*
target     := "#" IDENT                  -- قناع (mask) يُحلّ إلى اسم حاوية
            | IDENT ( "/" IDENT )*        -- اسم حاوية مباشر، أو مسار Group/حاوية متداخل
clause     := predicate                   -- شرط فلترة (AND مع بقية الشروط)
            | group                       -- مجموعة منطقية: or(...) / and(...) / not(...)
            | modifier                    -- order/limit/offset/select/distinct
group      := ("or"|"and") "(" pg ("&" pg)* ")"   -- pg = predicate أو مجموعة متداخلة أخرى
            | "not" "(" pg ")"                     -- نفي شرط/مجموعة فرعية واحدة (بلا '&' بداخلها)
predicate  := field ":" OP "(" ARG? ")"
field      := IDENT ( "/" IDENT )*        -- يدعم حقول map متداخلة: address/city
OP         := eq | ne | ieq | gt | gte | lt | lte
            | has | like | starts | ends | exists | missing
ARG        := IDENT                       -- وسيط واحد فقط (لا فاصلة: ',' ليست من الرموز المسموحة)
```

### الأهداف (target)

| الشكل            | المعنى                                                                 |
|-------------------|--------------------------------------------------------------------------|
| `#mask`          | يُحلّ عبر سجل الأقنعة (`mask="...";`) إلى اسم الحاوية الفعلي              |
| `name`           | اسم حاوية `@container.doc`/`@doc` مباشر                                  |
| `group/name`     | حاوية متداخلة داخل `@Containers.Group` — يُتحقَّق من العضوية خطوة بخطوة   |

### العمليات (predicate OP)

| العملية   | المعنى                                                                    |
|-----------|-------------------------------------------------------------------------------|
| `eq`      | يساوي (`valuesEqual`)                                                         |
| `ne`      | لا يساوي                                                                       |
| `ieq`     | يساوي نصّاً بغضّ النظر عن حالة الأحرف                                          |
| `gt`/`gte`/`lt`/`lte` | مقارنة رقمية (يتطلّب أن يكون الحقل والوسيط رقمَين، وإلا فالنتيجة false)  |
| `has`     | الحقل مصفوفة تحوي الوسيط، أو map يحوي مفتاحاً بنفس اسم الوسيط                  |
| `like`    | الحقل نص، ويحوي الوسيط كنص جزئي (substring) بغضّ النظر عن حالة الأحرف          |
| `starts`  | الحقل نص يبدأ بالوسيط (بغضّ النظر عن حالة الأحرف)                              |
| `ends`    | الحقل نص ينتهي بالوسيط (بغضّ النظر عن حالة الأحرف)                             |
| `exists`  | الحقل موجود في المستند (بأي قيمة، بلا وسيط: `field:exists()`)                 |
| `missing` | الحقل غير موجود في المستند (بلا وسيط: `field:missing()`)                      |

الوسيط (ARG) يُفسَّر تلقائياً (لبقية العمليات): `true`/`false` → منطقي، `null`/`nil` → nil، نص
يُقرأ كاملاً كرقم صالح → رقم، وإلا → نص كما كُتب (لا توجد علامات اقتباس ضمن الصياغة المسموحة).

### مجموعات منطقية: OR / AND / NOT

بما أن `&` هي AND ضمنية بين شروط الاستعلام مباشرة، تُستخدَم `or(...)`/`and(...)`/`not(...)`
لتركيب منطق أعقد (ويمكن تداخلها ببعضها بحرّية):

```rin
// OR بسيطة
sql("#app.users & or(role:eq(admin) & role:eq(owner))")

// NOT -- نفي شرط واحد (أو مجموعة واحدة)
sql("#app.users & not(role:eq(banned))")

// تركيب متداخل: (admin AND active) OR (owner AND active)
sql("#app.users & or(and(role:eq(admin) & active:eq(true)) & and(role:eq(owner) & active:eq(true)))")
```

`not(...)` تأخذ شرطاً/مجموعة واحدة بالضبط (بلا `&` بداخلها) — لنفي عدّة شروط معاً استخدم
`not(and(...))`.

### المُعدِّلات (تشكيل النتيجة، لا تُعتبَر شروط فلترة)

| المُعدِّل                 | المعنى                                                             |
|----------------------------|-------------------------------------------------------------------|
| `order:asc(field)`         | فرز تصاعدي حسب `field` (تُكرَّر لعدّة مفاتيح فرز متتالية)          |
| `order:desc(field)`        | فرز تنازلي حسب `field`                                             |
| `limit:eq(N)`               | أقصى عدد نتائج                                                     |
| `offset:eq(N)`               | تخطّي أول N نتيجة (بعد الفرز)                                      |
| `select:has(field)`         | إسقاط: إبقاء حقل واحد فقط (تُكرَّر لعدّة حقول؛ `_id` يبقى دوماً)     |
| `distinct:eq(field)`         | إزالة التكرار (تُكرَّر لبناء **مفتاح تفريد مركّب** من عدّة حقول معاً) |

**ترتيب التنفيذ الفعلي دوماً**: فلترة (AND/OR/NOT) → `distinct` → `order` → `offset` → `limit` →
`select`. المستندات المفقود منها حقل الفرز تُدفَع دوماً إلى آخر النتائج.

```rin
// أحدث 5 حسابات فعّالة، الاسم فقط
sql("#app.users & active:eq(true) & order:desc(createdAt) & limit:eq(5) & select:has(name)")

// الصفحة الثانية (10 لكل صفحة) مرتّبة بالاسم
sql("#app.users & order:asc(name) & offset:eq(10) & limit:eq(10)")

// تفريد مركّب: أول مستند فقط لكل تركيبة (مدينة، دور) معاً
sql("#app.users & distinct:eq(city) & distinct:eq(role)")
```

## أمثلة أساسية

```rin
@container.doc=users
    mask="app.users";
    document id="u1" fields={ name: "Ali",  role: "admin", age: 25, address: { city: "Cairo" } };
    document id="u2" fields={ name: "Sara", role: "user",  age: 30, address: { city: "Giza" } };
.end/container.doc

print sql("#app.users & role:eq(admin) & age:gte(18)");
print sqlOne("#app.users & address/city:eq(Cairo)");
print sqlCount("#app.users & role:eq(admin)");
```

### تعريف استعلام مُسمّى واستدعاؤه بالقناع

```rin
@sql=activeAdmins
    mask="q.activeAdmins";
    text query = "#app.users & role:eq(admin) & age:gte(18) & order:desc(age)";
.end/sql

print sql("q.activeAdmins"); // بلا تكرار نص RCSQL في كل مرة
```

`sql()` وكل النواتج أدناه تتعامل مع الوسيط الممرَّر بشكل موحَّد: إن كان قناعاً لحاوية
`@sql`/`@container.sql` معرَّفة مسبقاً تُستبدَل تلقائياً بنص RCSQL المخزَّن بداخلها؛ وإلا يُعامَل
كنص RCSQL خام فوري.

## دوال RCSQL (natives)

### قراءة/فحص

| الدالة                              | تُعيد                                                                    |
|--------------------------------------|---------------------------------------------------------------------------|
| `sql(query)`                        | مصفوفة كل المستندات المطابقة (كل عنصر map يحمل `_id` + الحقول)             |
| `sqlOne(query)`                     | أول مستند مطابق فقط، أو `nil`                                              |
| `sqlCount(query)`                   | عدد المستندات المطابقة (رقم)                                              |
| `sqlExists(query)`                  | `true`/`false` — أخفّ من `sqlCount` حين يهمّك الوجود فقط                    |
| `sqlIds(query)`                     | مصفوفة معرّفات (`_id`) فقط                                                 |
| `sqlPluck(query, field)`            | مصفوفة قيمة حقل واحد عبر كل مطابقة (`nil` لمن لا يملكه؛ `field` يقبل `/`)   |

### تجميع

| الدالة                              | تُعيد                                                                    |
|--------------------------------------|---------------------------------------------------------------------------|
| `sqlSum(query, field)`              | مجموع القيم الرقمية للحقل (0 إن لم توجد قيم رقمية)                          |
| `sqlAvg(query, field)`              | متوسط القيم الرقمية للحقل (`nil` إن لم توجد)                               |
| `sqlMin(query, field)` / `sqlMax`   | أصغر/أكبر قيمة رقمية للحقل (`nil` إن لم توجد)                              |
| `sqlGroupBy(query, field)`          | مصفوفة `{key, count, ids}` — تجميع (GROUP BY) حسب قيمة `field`             |
| `sqlGroupSum(query, groupField, sumField)` | مصفوفة `{key, sum, count}` — GROUP BY ثم SUM لكل مجموعة             |

### تعديل بالجملة

| الدالة                              | تُعيد                                                                    |
|--------------------------------------|---------------------------------------------------------------------------|
| `sqlUpdate(query, field, value)`    | يضبط `field` (يقبل `/` لحقل متداخل) إلى `value` على كل المطابقات؛ يعيد عدد المُحدَّث |
| `sqlDelete(query)`                  | يحذف كل المطابقات فعلياً من حاويتها؛ يعيد عدد المحذوف                       |

### تطوير/تشخيص

| الدالة              | تُعيد                                                                                   |
|----------------------|--------------------------------------------------------------------------------------------|
| `sqlValidate(query)` | `{ok:true, container}` أو `{ok:false, error}` — **لا يرمي أبداً**، مناسب لمحرّر استعلامات تفاعلي |
| `sqlExplain(query)`  | map تشخيصي كامل: الهدف، اسم الحاوية المحلول، شجرة الشروط (بما فيها or/and/not)، الفرز، limit/offset، select/distinct |

مثال `sqlExplain`:
```rin
print sqlExplain("#app.users & role:eq(admin) & order:desc(age) & limit:eq(5)");
// {"queryText": "...", "targetMask": "app.users", "container": "users",
//  "predicates": [{"field": "role", "op": "eq", "arg": "admin"}],
//  "orderBy": [{"field": "age", "desc": true}], "limit": 5, "offset": 0,
//  "selectFields": [], "distinctFields": []}
```

ملاحظة: الوسيط `field`/`groupField`/`sumField` في `sqlPluck`/`sqlSum`/`sqlAvg`/`sqlMin`/`sqlMax`/
`sqlUpdate`/`sqlGroupBy`/`sqlGroupSum` وسيط Rin عادي (نص Value منفصل عن نص الاستعلام)، وليس جزءاً
من نص RCSQL — لذا غير مقيَّد بمجموعة رموزها.

`sqlUpdate` **ذرّي على مستوى الدفعة**: يتحقّق من مخطط كل مستند مُعدَّل أولاً على نسخ منفصلة؛ إن
خالف أيّ مستند واحد المخطط بعد التعديل تُرفَض العملية بالكامل بخطأ `E0019` (schema violation)،
ولا يُحدَّث أي مستند جزئياً. `sqlDelete` بلا تراجع (لا استرجاع)، تماماً كـ `deleteDoc()` الحالية.

حاوية/قناع غير موجود، أو بلا أي تطابق، يعيد قيمة فارغة مناسبة بصمت (`[]`/`0`/`nil`/`false` حسب
الدالة) — بلا خطأ، بنفس سلوك بقية دوال `docStore` الحالية.

## الأخطاء

رمز خارج القائمة المسموحة، حقل بلا عملية، عملية غير معروفة، قوس ناقص، `not(...)` بأكثر من شرط
واحد، أو مُعدِّل بشكل غير صالح (مثال: `limit:eq(abc)` أو `order:up(x)`) — كل ذلك يُرفَض بخطأ
تشخيصي `E0042` (`InvalidSql`) يوضّح موضع/سبب الخلل بالضبط داخل نص الاستعلام (باستثناء
`sqlValidate` التي تُعيد هذا الخطأ كقيمة `{ok:false, error}` بدل رميه).

## الملفات

- `rin_container_sql.h` / `rin_container_sql.cpp` — المحلّل النحوي البحت (`Query`/`Predicate`/
  `SortKey`/`parse`، بما فيه مجموعات `or`/`and`/`not` المتداخلة)، بلا أي اعتماد على
  `Interpreter`/`Value`.
- `rin_interpreter.h`/`.cpp` — الربط الفعلي: `sqlViews`، `sqlResolveQueryText`/
  `sqlResolveTargetContainer`/`sqlRunRaw` (الأنبوب الكامل: فلترة→distinct→order→offset/limit)،
  `sqlExecute` (يبني عرض `sql()` النهائي)، وكل النواتج أعلاه.
- `rin_ast.h` — `ContainerKind::SQL`.
- `rin_parser.cpp` — التعرّف على وسمَي `@sql`/`@container.sql` (حاوية بيانات نقية، بنفس قيود
  `container.data`/`table`/`doc`/...).
- `diagnostics/diagnostic.h`/`.cpp` — `E0042_InvalidSql`.
- `tools/test_container_sql.cpp` (ومثلها في `tests/tools/`) — 29 حالة اختبار عبر مسار CLI الحقيقي.
