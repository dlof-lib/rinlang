# table.md — الجدول (`@table` / `@container.table`) ودوال اللغة عليه

الجدول حاوية صفوف: `row cells=[...]` لكل صف، و`style value="..."` لنمط العرض، و`save format=png`
لتصدير صورة. **لا توجد دوال `table*` جديدة**: الجدول يُقرأ ويُعدَّل بدوال اللغة الموجودة أصلاً
(دوال الحاويات + دوال المصفوفات + عائلة `#`)، لأنه يُكشَف كحقول افتراضية داخل الحاوية.

```rin
@table=scores
    style value="style://dark";
    row cells=["name", "score"];
    row cells=["sara", 95];
    row cells=["yusuf", 88];
.end/table
```

## الحقول الافتراضية

| الحقل | المعنى | كتابة؟ |
|---|---|---|
| `rows` | **مقبض حيّ** على مصفوفة الصفوف نفسها (كل صف مصفوفة خلايا) | نعم (`container.set` = مصفوفة صفوف) |
| `header` | أول صف إن كانت كل خلاياه نصوصاً (وإلا مصفوفة فارغة) | نعم (مصفوفة نصوص؛ يستبدل الرأس أو يُدرَج) |
| `body` | صفوف الجسم (بعد الرأس) — نسخة | نعم: مصفوفة صفوف تستبدل الجسم وتُبقي الرأس |
| `records` | الجسم كقواميس `{عنوان: خلية}` — نسخة | نعم: قائمة قواميس → جدول (الرأس = اتحاد المفاتيح بترتيب ظهورها، الغائب `nil`) |
| `columns` | الأعمدة كمصفوفة مصفوفات (نسخة؛ الخلية الناقصة `nil`) | نعم: مصفوفة أعمدة تُقلَب إلى صفوف — بها تُضاف الأعمدة وتُحذف وتُرتَّب |
| *اسم عمود* | قيم الجسم لذلك العمود (نسخة) — العمود حقل بحدّ ذاته، انظر «الأعمدة كحقول» | نعم (للعمود الموجود) |
| `style` | آخر `style value=...` (يظهر فقط إن وُجد) | نعم (نص) |

الأولوية: **متغيّر حقيقي** داخل الجدول (`text rows = ...` مثلاً) ثم **الحقول المحجوزة** (`rows header body records columns style`) ثم **أسماء الأعمدة**.
أسماء الأعمدة لا تظهر في `fieldNames`/`fields`/`snapshot` (حتى لا تتكرّر البيانات)؛ اكتشفها من `container.get(t, "header")`.

## دوال الحاويات الموجودة أصلاً (أصبحت تفهم الجدول)

```rin
let rows = getField("scores", "rows");          // = container.get
container.has("scores", "rows");                // true
container.fieldNames("scores");                 // ["rows", "header", "body", "records", "columns", "style"]
container.fieldType("scores", "rows");          // "array"
container.fields("scores");                     // map يضمّ rows/columns/style
container.info("scores").rows;                  // عدد الصفوف
container.info("scores").columns;               // عرض الجدول (أكبر صف)
container.set("scores", "rows", [["a", 1]]);    // استبدال الصفوف (المقابض القديمة تبقى صالحة)
container.set("scores", "style", "style://light");
container.deleteField("scores", "rows");        // تفريغ الصفوف  (و"style" يزيل النمط)
container.clearFields("scores");                // يفرّغ المتغيّرات والصفوف والنمط
container.empty("scores");                      // true فقط بلا متغيّرات وبلا صفوف
```

## دوال المصفوفات الموجودة أصلاً على `rows`

لأن `rows` مقبض حيّ، كل دالة تعدّل المصفوفة في مكانها تعدّل الجدول نفسه:
`push` · `pop` · `#add` · `#swap` (و`sort` تفرز مصفوفة أعمدة/خلايا رقمية أو نصية، لا مصفوفة صفوف).
والدوال التي ترجع نسخة (`reverse` · `slice` · `concat`) تُكتب للجدول عبر `container.set`:

```rin
container.set("scores", "rows", reverse(getField("scores", "rows")));
```

## عائلة `#` على الجدول

```rin
let rows = getField("scores", "rows");
#add(rows, ["omar", 70]);                     // صف جديد في مكانه (يظهر في save/png/zip)
#swap(rows, 1, 3);                            // تبديل صفّين فعلاً
#diff(rowsA, rowsB);                          // الصفوف الموجودة في A وغائبة عن B
let col = container.get("scores", "columns")[1];
#sum(slice(col, 1));                          // مجموع عمود (slice لتخطّي صف الرأس)
for (let i in 0 #to len(rows) - 1) { print rows[i]; }
```

> `#sum` على صفوف فيها نص يرمي `E0004` كما هو معتاد في `#sum` — خُذ عموداً رقمياً بعد `slice`.
> `columns` وقيم الأعمدة نسخ لحظية: أعد قراءتها بعد أي تعديل على `rows`.

## الرأس و`row` بقاموس

أول صف نصي بالكامل = رأس الجدول. بعده يمكن كتابة الصف بقاموس فيُرتَّب حسب الرأس (الغائب `nil`):

```rin
@table=staff
    row cells=["name", "dept", "score"];
    row cells=["sara", "dev", 95];
    row cells={"name": "huda", "score": 99, "dept": "ops"};   // = ["huda", "ops", 99]
.end/table
```

قاموس بلا رأس نصي → `E0004`، وعمود غير موجود في الرأس → `E0007`.
جدول بلا رأس نصي (كله أرقام مثلاً) تُسمّى أعمدته `c1, c2, ...` لأغراض الاستعلام، وكل صفوفه جسم.

## استعلامات RCSQL الموجودة أصلاً

دوال `sql*` الموجودة تقرأ الجدول كمجموعة مستندات: الأعمدة = الرأس، و`_id` = رقم صف الجسم (1-based).
لا تحتاج `@doc` ولا نسخ البيانات:

```rin
sqlCount("staff & dept:eq(dev)");
sqlOne("staff & order:desc(score) & limit:eq(1)");
sqlSum("staff & dept:eq(ops)", "score");
sqlGroupBy("staff", "dept");
sql("staff & score:gte(90) & order:asc(name) & select:has(name)");
sqlUpdate("staff & name:eq(omar)", "score", 75);   // يكتب الخلية في الجدول (العمود يجب أن يكون في الرأس، وإلا E0035)
sqlDelete("staff & dept:eq(ops)");                  // يحذف الصفوف المطابقة من rows
```

الأرقام `_id` لقطة لحظة الاستعلام؛ بعد `sqlDelete` أعد الاستعلام قبل استعمال معرّفات قديمة.
`groupSnapshot`/`volumeSnapshot` تضيف الآن `rows` لأي جدول عضو.

## الأعمدة كحقول

اسم العمود (من الرأس النصي) يعمل كحقل في دوال الحقول الموجودة نفسها:

```rin
getField("staff", "score");                     // [95, 88, 99]  (قيم الجسم)
container.has("staff", "dept");                 // true
setField("staff", "score", [1, 2, 3]);          // بطول الجسم، وإلا E0007
setField("staff", "dept", "all");               // قيمة مفردة تملأ العمود كله
container.renameField("staff", "score", "points");   // يغيّر عنوان العمود (false إن غاب أو كان الاسم الجديد موجوداً)
container.deleteField("staff", "dept");         // يحذف العمود: عنوانه وخلاياه
container.contains("staff", "name", "sara");    // هل القيمة في العمود؟
container.getOr("staff", "nope", "none");
```

**إضافة عمود** أو ترتيب الأعمدة: عدّل `columns` (أو `records`):

```rin
let cols = container.get("staff", "columns");
container.set("staff", "columns", concat(cols, [["flag", true, false, true]]));
```

`container.push(t, "rows", صف)` يُلحق صفاً (مصفوفة أو قاموساً مرتّباً حسب الرأس) ويرجع الطول الجديد، و`container.pop(t, "rows")` يزيل آخر صف ويرجعه.

## دوال المستندات الموجودة (insertDoc / updateDoc / ...)

الجدول مجموعة مستندات: **المعرّف = رقم صف الجسم (1-based)** كما في RCSQL. المعرّفات موضعية: تنزاح بعد الحذف.

```rin
allDocs("staff");                       // كل الصفوف كقواميس
countDocs("staff");   docIds("staff");  // 3 · ["1","2","3"]
findDoc("staff", "2");                  // قاموس أو nil
queryDocs("staff", "dept", "dev");      queryOneDoc("staff", "name", "sara");
insertDoc("staff", "9", {"name": "omar", "score": 70});  // معرّف خارج النطاق = إلحاق (true) · داخله = استبدال (false)
updateDoc("staff", "1", {"score": 99}); // دمج جزئي؛ false إن لم يوجد الصف
deleteDoc("staff", "2");
```

عمود غير موجود في الرأس → `E0007` · وسيط ليس قاموساً → `E0020` (`insertDoc`) أو `E0004` (`updateDoc`) · جدول بلا رأس نصي → `E0004`.

## دوال الحاويات على عمود

دوال `container.*` التي تأخذ «نوعاً وحقلاً» تقبل أيضاً **اسم جدول وعموداً** (أي أول وسيط اسم جدول موجود):

| الدالة | الناتج على جدول |
|---|---|
| `sum` `avg` `min` `max` | على الخلايا الرقمية في العمود (`avg/min/max` = `nil` إن لم توجد أرقام) |
| `pluck` `distinct` `countBy` | مصفوفة العمود · قيمه المميّزة · `{قيمة: تكرار}` |
| `groupBy` | `{قيمة: [معرّفات الصفوف]}` |
| `sortBy(t, col, desc?)` · `top(t, col, n)` | معرّفات الصفوف مرتّبة (الخلايا الفارغة أخيراً) · أعلى `n` |
| `query(t, شروط?, خيارات?)` | معرّفات الصفوف؛ الشروط `{col: قيمة}` أو `{col: {gt, gte, lt, lte, in, contains, ne, exists}}` والخيارات `sortBy/desc/limit` |
| `search(نص, t)` | معرّفات الصفوف التي تحوي النص في أي خلية |
| `paginate(t, page, size)` | `{items, page, size, total, pages}` — العناصر سجلّات فيها `_id` |
| `stats(t)` | `{count, fields: {col: {count, numeric, min, max, sum, avg}}}` |
| `toRows(t, أعمدة?)` | سجلّات `[{_id, ...}]` |
| `equals(a, b)` · `diff(a, b)` · `checksum(t)` | مقارنة جدولين · `{same, added, removed}` بسجلّات · SHA-256 للصفوف |
| `mergeFrom(dst, src)` | يُلحق سجلّات `src` بـ `dst` (الأعمدة الجديدة تُضاف للرأس) ويرجع عددها |
| `clone` `rename` `remove` | تحمل الصفوف والنمط معها (كانت تُفقد) |

عمود غير موجود → `E0007`.

## التصدير والاستيراد

`save` يقبل الآن صيغاً نصية للجدول (مثل `png`): `csv` · `json` · `md` · `html` · `txt`:

```rin
@table=staff
    row cells=["name", "score"];
    row cells=["sara", 95];
    save path="staff.csv" format=csv;     // بلا path: staff.csv
    save format=md;                       // | name | score | …
.end/table
```

- `csv`: يقرأ الصفوف كما هي (الخلايا النصية التي تبدأ بـ `= + - @` تُسبق بـ `'` حماية من حقن الصيغ، وتُعكس عند الاستيراد).
- `json`: سجلّ لكل سطر `[{"name":"sara","score":95}, …]` (مصفوفة مصفوفات إن لم يوجد رأس نصي).
- `md` · `html` (`<table class="rin-table" data-style=…>` بهروب كامل) · `txt` (إطار ASCII).

وبدون `save`، بدوال الحاويات الموجودة:

```rin
container.toCsv("staff", ["name", "score"]);      container.exportCsv("staff", "out.csv");
container.toJson("staff");                          container.exportToFile("staff", "out.json");   // الامتداد csv/md/html/txt يغيّر الصيغة
container.fromJson("t", "[{\"a\":1},{\"a\":2,\"b\":3}]");  // مصفوفة قواميس -> جدول (overwrite=false: يُلحِق)
container.importFromFile("t", "in.csv");            // CSV: الصف الأول رأس · الأرقام أرقام · الفارغ nil
```

`fromJson` لغير مصفوفة سجلّات يرجع `false` بلا تغيير.

## تصدير الملف المحفوظ

`save` (ملف `.rin`) و`save format=png` و`installation <name> format=zip` تقرأ **الصفوف الحالية**،
فأي تعديل عبر ما سبق ينعكس عليها مباشرة.

## الأخطاء

| الحالة | الكود |
|---|---|
| `container.set(t, "rows", غير-مصفوفة)` أو صف ليس مصفوفة | `E0004` |
| `container.set(t, "columns"/"body", غير-مصفوفة)` أو عمود/صف ليس مصفوفة | `E0004` |
| `setField(t, عمود, مصفوفة)` بطول يخالف عدد صفوف الجسم | `E0007` |
| عمود غير موجود في `insertDoc`/`updateDoc`/`container.sum`... | `E0007` |
| `insertDoc` بوسيط ليس قاموساً | `E0020` |
| `save format=csv/json/md/html/txt` خارج جدول | `E0014` |
| `header` ليست نصوصاً · `records` ليست قواميس | `E0004` |
| `row` بقاموس بلا رأس نصي | `E0004` |
| `row` بقاموس فيه عمود غير موجود في الرأس | `E0007` |
| `sqlUpdate` على عمود غير موجود في رأس الجدول | `E0035` |
| `container.set(t, "style", غير-نص)` | `E0004` |

اختبارات: `tests/verification/table_bridge.rin` · `table_sql.rin` · `table_columns.rin` · `table_docs.rin` · `table_aggregate.rin` · `table_export.rin` · `table_lifecycle.rin`.
التنفيذ: `app/src/main/cpp/rin_table.cpp` (يُضمَّن في `rin_interpreter.cpp`)؛ ومعها خطّاف سطر واحد `tableNative(...)` في أول كل دالة موجودة معنية.
