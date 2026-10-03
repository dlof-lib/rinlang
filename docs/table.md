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
| `body` | صفوف الجسم (بعد الرأس) — نسخة | لا (`E0004`) |
| `records` | الجسم كقواميس `{عنوان: خلية}` — نسخة | نعم: قائمة قواميس → جدول (الرأس = اتحاد المفاتيح بترتيب ظهورها، الغائب `nil`) |
| `columns` | الأعمدة كمصفوفة مصفوفات (نسخة؛ الخلية الناقصة `nil`) | لا — مشتقّة من `rows` (`E0004`) |
| `style` | آخر `style value=...` (يظهر فقط إن وُجد) | نعم (نص) |

متغيّر حقيقي بنفس الاسم داخل الجدول (`text rows = ...` مثلاً) له الأولوية دائماً ولا يُحجَب.

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
> `columns` نسخة لحظية: أعد قراءتها بعد أي تعديل على `rows`.

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

## التصدير

`save` (ملف `.rin`) و`save format=png` و`installation <name> format=zip` تقرأ **الصفوف الحالية**،
فأي تعديل عبر ما سبق ينعكس عليها مباشرة.

## الأخطاء

| الحالة | الكود |
|---|---|
| `container.set(t, "rows", غير-مصفوفة)` أو صف ليس مصفوفة | `E0004` |
| `container.set(t, "columns"/"body", ...)` | `E0004` |
| `header` ليست نصوصاً · `records` ليست قواميس | `E0004` |
| `row` بقاموس بلا رأس نصي | `E0004` |
| `row` بقاموس فيه عمود غير موجود في الرأس | `E0007` |
| `sqlUpdate` على عمود غير موجود في رأس الجدول | `E0035` |
| `container.set(t, "style", غير-نص)` | `E0004` |

اختبارات: `tests/verification/table_bridge.rin` و`tests/verification/table_sql.rin`.
