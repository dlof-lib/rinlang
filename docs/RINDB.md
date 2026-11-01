# rindb — محرك قاعدة بيانات مكتوب بلغة Rin

مكتبة `lib/rindb.og.rin/` بلغة Rin نفسها، **بلا أي دالة أو مفهوم أو متغير جديد في المفسّر**. تستعمل ما في اللغة أصلاً:

| حاجة المحرك | ما تستعمله المكتبة من اللغة |
|---|---|
| جدول يُنشأ وقت التشغيل | `spawn("doc", name)` ← حاوية `container.doc` حقيقية؛ `destroyContainer` للحذف |
| المستندات | `insertDoc / findDoc / deleteDoc / docIds / queryDocs / findByIndex` |
| schema وفهارس | `defineSchema` و`createIndex` |
| الاستعلام | RCSQL (`sql / sqlCount / sqlOne / sqlIds`) |
| المعاملات | `beginTransaction / commitTransaction / rollbackTransaction` |
| التخزين | `readFile / writeFile / appendFile / deleteFile / fileExists` + `json.stringify/tryParse` + `hash` (sha256) |
| تسجيل القواعد المفتوحة | `@Containers.Group=RinDB` + `@container=catalog` (حقل `openCount`) |

```rin
@import "rindb";
let db = rdb_open("shop", "shop.rdb");                 // أو rdb_open("shop", nil) في الذاكرة
rdb_createTableWith(db, "users", {"schema": {"name": "string", "age": "number"}, "indexes": ["name"]});
rdb_put(db, "users", "u1", {"name": "Sara", "age": 30});
print rdb_query(db, "users&age:gt(25)");
rdb_transaction(db, fun() { rdb_update(db, "users", "u1", {"age": 31}); });
rdb_close(db);
```
الجدول `users` في القاعدة `shop` هو الحاوية `shop__users`.

## الواجهة
- **قواعد:** `rdb_open(name, path)`, `rdb_openWith(name, path, {readonly, recover, autoCheckpoint})`, `rdb_close`, `rdb_checkpoint`, `rdb_backup(db, dest)`, `rdb_dropDatabase(db, true)`, `rdb_info`, `rdb_list`, `rdb_verify(path)`
- **جداول:** `rdb_createTable(db, t)`, `rdb_createTableWith(db, t, {schema, indexes, ifNotExists})`, `rdb_dropTable`, `rdb_tables`, `rdb_hasTable`
- **مستندات:** `rdb_put` (يستبدل) / `rdb_insert` (يرفض المكرر) / `rdb_add` (معرّف تلقائي) / `rdb_get` / `rdb_has` / `rdb_update` (دمج) / `rdb_incr` / `rdb_delete` / `rdb_count` / `rdb_ids` / `rdb_all` / `rdb_find` / `rdb_truncate` / `rdb_bulk`
- **استعلام:** `rdb_query / rdb_queryCount / rdb_queryOne / rdb_queryIds` بصيغة RCSQL مع اسم الجدول أولاً: `"users&age:gt(30)"`
- **معاملات (متداخلة):** `rdb_begin / rdb_commit / rdb_rollback / rdb_depth / rdb_transaction(db, fun)`؛ كل begin نقطة حفظ، وrollback الداخلية تتراجع لنقطتها فقط، وcommit الداخلية تدمج في الأب؛ تُكتب في الملف مرة واحدة كسطر واحد (ذرّية) عند commit الخارجية

كل مستند يحمل `_id`. الأخطاء تُرمى كنص يبدأ بـ`rindb:` ويُلتقط بـ`catch (e)` ← `e["message"]`.

## داخل حاوية / مجموعة / حجم
القاعدة تُخزَّن في متغير داخل أي مستوى، والاختصارات `rdb_connect/rdb_bind/rdb_tableOf` تُرجع كائنات مربوطة فلا يُمرَّر `db` في كل نداء:
```rin
@import "rindb";                       // يصحّ أيضاً داخل جسم الحاوية نفسها
@Volume=Store
    @Containers.Group=Shop
        @container=app
            let db = rdb_connect("shop", "shop.rdb");
            let users = nil;
            fun setup() {
                db.createTable("users", {"indexes": ["name"]});
                users = db.table("users");
                users.put("u1", {"name": "Sara"});
                db.transaction(fun() { users.update("u1", {"name": "Sara K"}); });
                return users.count();
            }
        .end/container
    .end/Containers.Group
.end/Volume
print Store.Shop.app.setup();
let u = Store.Shop.app.users;          // الكائن يُنسخ إلى متغير ثم تُستدعى دوالّه
print u.query("name:like(sara)");
```
ملاحظات: نداء دالة مخزَّنة في حقل عبر مسار طويل مباشرةً (`Store.Shop.app.users.get(...)`) لا تدعمه اللغة، فانسخ الكائن لمتغير أولاً.
الجداول تبقى حاويات `قاعدة__جدول` عامة الاسم، فاسم القاعدة فريد عبر كل المستويات.
في مجلد المشروع: ضع `rindb.og.rin/` في `lib/` بجوار البرنامج، أو استعمل `@import "libs/rindb.og.rin";`.

## صيغة الملف والسلامة
ملف نصي، سطر لكل عملية: `<16 خانة من sha256> <JSON>`.
- سطر ناقص/تالف في **الذيل** (انقطاع أثناء الكتابة) ← يُقصّ تلقائياً عند الفتح.
- تالف في **الوسط** ← يُرفض الفتح؛ `{"recover": true}` يحتفظ بما قبله ويحفظ نسخة `.corrupt`.
- `rdb_checkpoint` يكتب لقطة كاملة في `path.tmp` تنتهي بسطر `E` ثم تحلّ محل الملف؛ إن انقطع بينهما تُستعمل اللقطة الكاملة عند الفتح.
- لا يمكن فتح قاعدة باسم غير اسمها المخزَّن.

## المتانة والقفل (تتطلب دوال `file*` الجديدة — docs/FILEDB.md)
- `rdb_openWith(name, path, {"sync": ...})`: `"commit"` (الافتراضي) = `fileSync` عند commit الخارجية وعند checkpoint · `"always"` = بعد كل سجل · `"off"` = بلا fsync.
- نقطة التفتيش ذرّية فعلاً: كتابة `path.tmp` ← `fileSync` ← `fileRename` فوق الملف.
- الفتح للكتابة يأخذ قفلاً حصرياً على `path.lock` (`fileLock`)؛ فتح ثانٍ من عملية أخرى يفشل بـ«in use by another process». الفتح `{"readonly": true}` لا يحتاج قفلاً. يُحرَّر القفل عند `rdb_close`/`rdb_dropDatabase` أو بانتهاء العملية. ملف `.lock` يبقى بعد الإغلاق (لا يضرّ) ويُحذف مع `rdb_dropDatabase`.
- `rdb_findRange(db, t, field, lo, hi)` = `findByRange` (nil = حدّ مفتوح).

## حدود (مقصودة وصريحة)
- كل البيانات في الذاكرة؛ الحجم محدود بالـ RAM، وفتح الملف يعيد تشغيل السجل كله.
- `beginTransaction` في اللغة تلتقط لقطة عميقة لكل المستندات (كلفة تتناسب مع حجم كل الجداول).
- تغيير البنية (إنشاء/حذف جدول) ممنوع داخل معاملة.
- المتانة عند انقطاع الكهرباء تعتمد على وضع `sync` (انظر أدناه)؛ مع `"off"` قد تضيع آخر الكتابات.
- لا joins ولا فهارس نطاق؛ الاستعلام هو RCSQL كما في اللغة.
- القفل استشاري: يمنع عملية rindb أخرى فقط، لا أي برنامج يكتب في الملف مباشرةً.
- `rdb_findRange` مسح مرتّب (O(n))، وليس فهرس نطاق.
