# container.datalist — قائمة بيانات + RCSQL القوي

`@container.datalist=name` (أو `@datalist` أو `@container.list`) حاوية **قائمة بيانات مرتّبة**: سجلات بمعرّفات
تلقائية تصاعدية. تُخزَّن في `docStore` كمجموعة مستندات، فتعمل معها **كل دوال المستندات وRCSQL الموجودة أصلاً**
(`insertDoc/findDoc/updateDoc/deleteDoc/docIds/sql*/defineSchema/createIndex/beginTransaction...`) بلا أي دالة جديدة.

```rin
@container.datalist=sales
    document fields={"item": "pen", "qty": 10, "day": "2026-01-05"};     // id تلقائي: "1"
    document fields={"item": "book", "qty": 2, "day": "2026-01-20"};    // "2"
    document id="x9" fields={"item": "bag", "qty": 1, "day": "2026-03-15"}; // id صريح مسموح
.end/container.datalist
```
- `document fields={...};` بلا `id` مسموحة **في datalist فقط** (في `container.doc` الـ id مطلوب كما كان).
- المعرّف التلقائي = أصغر رقم غير مستعمل بعد عدد السجلات الحالي.
- `spawn("datalist", name)` ينشئها وقت التشغيل؛ تُحفَظ/تُقرأ بنفس صيغة `document` (`container.datalist`).

## RCSQL القوي (يعمل على container.doc وdatalist معاً، ومتوافق مع الاستعلامات القديمة)
بنفس الرموز الخمسة المسموحة (`/ : & ( ) #`) — القيم المتعددة تُفصل بـ `/`.

| عملية | مثال | المعنى |
|---|---|---|
| `in` / `nin` | `item:in(pen/bag)` | ضمن/خارج قائمة قيم |
| `between` | `qty:between(2/7)` · `day:between(2026-01-01/2026-02-28)` | نطاق شامل (أرقام أو نصوص/تواريخ ISO) |
| `before` / `after` / `on` | `day:before(2026-02)` · `day:on(2026-03)` | تواريخ ISO بمقارنة بطول القيمة (`2026-03` = كل مارس)؛ للأرقام مقارنة عادية |
| `notlike` / `nstarts` / `nends` | `item:notlike(pe)` | نفي النص |
| `isa` | `qty:isa(number)` | نوع الحقل: number/string/bool/array/map/nil |
| `empty` / `filled` | `tags:empty()` | نص/مصفوفة/قاموس فارغ أو لا |

### التجميع
```
sales & group:by(item) & agg:sum(qty) & agg:avg(price) & agg:min(day) & agg:max(day)
      & having(count:gt(1) & sum_qty:gte(5)) & order:desc(sum_qty) & limit:eq(10)
```
- `group:by(f)` (يتكرر لمفتاح مركّب)، `agg:sum|avg|min|max(f)`؛ `count` يُضاف دوماً.
- صف الناتج: `{f.., count, sum_qty, avg_price, min_day, max_day}` و`_id` = مفتاح المجموعة. `min/max` تعمل على أرقام ونصوص (تواريخ).
- `having(...)` شروط AND على حقول الناتج؛ بعدها تعمل `distinct/order/offset/limit/select` على الصفوف المجمَّعة.
- `agg:` بلا `group:by` = تجميع كل الصفوف في صف واحد (`_id: "all"`).
- `by/sum/avg/min/max` لا تصلح كشرط فلترة (خطأ تركيبي صريح).
- أسماء `group` و`agg` كحقول عادية تبقى تعمل بالعمليات القديمة (`group:eq(x)`)؛ المُعدِّل يُفعَّل فقط بـ`group:by` و`agg:sum|avg|min|max`.

## حدود
- لا joins بين حاويات؛ التواريخ نصوص ISO (لا تحليل لمناطق زمنية أو صيغ أخرى).
- `regex` غير مدعوم لأن رموزه خارج الرموز الخمسة المسموحة.

## إمكانات إضافية (إصدار 2)
**حقول افتراضية** (بنفس أسلوب الجدول، عبر `container.get/set/has/deleteField/fields`): `items` (سجلات مع `_id`) · `ids` · `size` · `first` · `last` · `header` · وأي اسم عمود يعيد مصفوفة قيمه. `container.set(name, "items", [..])` تستبدل القائمة كاملة (معرّف صريح `_id` أو تلقائي، مع فحص schema)، و`container.deleteField(name, "items")` و`container.clearFields` تفرّغانها. `container.empty/info` تعكسان السجلات (`items`, `columns`).

**دوال `container.*` التحليلية الموجودة أصلاً تعمل على القائمة** (قراءة فقط): `sum/avg/min/max/pluck/distinct/countBy/groupBy/sortBy/top/paginate/search/stats/toCsv/toJson/exportCsv/exportToFile/checksum/contains`. ما يُعيد معرّفات يُعيد المعرّفات الحقيقية (مثل `"x9"`).

**RCSQL إضافي:** `group:year|month|day(dateField)` (تجميع حسب الفترة؛ الناتج `month_day`…) · `agg:count|distinct|median(f)` · `page:of(P/S)` (الصفحة P بحجم S) · `search:any(text)` (في أي حقل) و`field:any(text)` (نص أو عنصر في مصفوفة).

# Easy_key — مفتاح لكل حاوية
المشكلة: جداول المفسّر الداخلية (`docStore`، schema، فهارس، `container.*`) تُفهرَس باسم الحاوية، فحاويتان بنفس الاسم في مجموعتين مختلفتين كانتا تدهس إحداهما الأخرى بصمت.

- **المفتاح الافتراضي (تلقائي):** الحاوية الأولى تحتفظ باسمها مفتاحاً. إن وُجدت حاوية حيّة بنفس الاسم في **مسار مختلف** فمفتاح الثانية = مسارها الكامل (`B.items`). إعادة تعريف نفس المسار تستبدل كما كان.
- **المفتاح المكتوب:** داخل جسم الحاوية `easy_key = "main";` (نص حرفي). فريد على كل الحاويات؛ تكراره أو مطابقته لاسم حاوية = خطأ صريح.
- **الاستعمال:** `container.get/set/has/info/...` و`insertDoc/findDoc/...` و`hasContainer` و`sql("key&...")` تقبل الاسم أو المفتاح المكتوب أو المسار الكامل. `container.info(name)["key"]` و`["path"]` يعرضان المفتاح والمسار.
- **القيم والمتغيرات:** كل متغير يُنسب لمفتاح حاويته (`container.get("main","v")` أو `main`-path `B.app.v`)، فلا يتعارض متغيران بنفس الاسم في حاويتين. لا توجد صياغة مفتاح لمتغير منفرد.
- **المجموعات والأحجام:** `easy_key = "shp";` يعمل في جسم `Containers.Group` و`Volume` أيضاً، والتعارض التلقائي (مجموعتان باسم واحد في حجمين مختلفين) يُحلّ بنفس القاعدة (مفتاح الثانية = مسارها). المفتاح المكتوب فريد على الحاويات والمجموعات والأحجام معاً.
- **الوصول:** بالنقطة `shp.app.v` أو `st.Shop.app.v` أو `app1.v`، وبالدوال `container.get("app1","v")` و`groupMembers("shp")` و`volumeMembers("st")` وبقية دوال `group*/volume*` التي تأخذ الاسم.
- **الحدود:** لا مفتاح لمتغير منفرد (يُنسب لمفتاح حاويته)؛ وحل التعارض الافتراضي للحاويات والمجموعات والأحجام المعرَّفة في الشيفرة.

# RCSQL: الربط join
```
orders & join:to(users/user/_id) & users_city:eq(Cairo) & group:by(users_name) & agg:sum(total)
```
- `join:to(other/localField/otherField)` ربط داخلي، و`join:left(...)` يُبقي الصفوف بلا مطابق. `_id` يعني معرّف السجل. الحاوية الأخرى: doc أو datalist أو table (تقبل اسماً أو مفتاحاً مكتوباً أو مساراً).
- حقول الطرف الآخر تُضاف بالبادئة `other_`: `users_name`, `users_city`, ومعرّفه `users__id`. معرّف الصف الناتج `id+otherId`.
- الشروط (WHERE) تُطبَّق **بعد** الربط فتصلح على الحقول المربوطة، ثم `group/agg/having/order/page`.
- ربط واحد أو أكثر متتابع؛ المطابقة بالتساوي فقط (hash غير مستعمل: خطّي O(n×m)).
