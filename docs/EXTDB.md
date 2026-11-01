# extdb 1.0.0 — قواعد بيانات خارجية للحاويات و indsin (مزوّد اتصال: rtdb / pgrest)

![extdb](../assets/branding/extdb_icon.png)

مكتبة `lib/extdb.og.rin/` بلغة Rin الصرفة فوق `httpRequest` و`jsonEncode` و`spawn("doc")` و`insertDoc/findDoc/docIds`
— **بلا أي دالة أصلية جديدة في المفسّر**. تربط حاويات `container.doc` بقاعدة بيانات بعيدة، ويُختار نوع القاعدة
بـ**اسم مزوّد الاتصال** (`"rtdb"` أو `"pgrest"`، غير حساس لحالة الأحرف).

```rin
@import "extdb";
let fb = xdb_connect("rtdb", "shop", {"url": "https://my-app.example-rtdb.com", "auth": "<token>"});
let sb = xdb_connect("pgrest", "shop2", {"url": "https://xyz.example-pgrest.com", "key": "<anon-or-service-key>"});

xdb_put(fb, "users", "u1", {"name": "Sara", "age": 30});
print xdb_get(sb, "users", "u1");
xdb_pull(sb, "users", "Users", nil);     // الجدول البعيد → حاوية container.doc محلية اسمها Users
xdb_push(fb, "users", "Users", nil);     // الحاوية المحلية → القاعدة البعيدة
```
نفس الدوال تعمل مع المزوّدين؛ ما يتغير هو إعداد الاتصال فقط.

## الاتصال
| الدالة | الوصف |
|---|---|
| `xdb_providers()` | أسماء المزوّدين المتاحة: `["rtdb","pgrest"]` |
| `xdb_connect(provider, name, cfg)` | يفتح اتصالاً باسم فريد `name`. مزوّد مجهول ← خطأ `extdb: unknown connection provider` |
| `xdb_rtdb(name, cfg)` / `xdb_pgrest(name, cfg)` | اختصاران |
| `xdb_open(provider, name, cfg)` | يفتح ويُرجع كائناً مربوطاً (انظر «داخل حاوية») |
| `xdb_ping(c)` · `xdb_info(c)` · `xdb_close(c)` · `xdb_connections()` | فحص الوصول / معلومات / إغلاق / أسماء الاتصالات المفتوحة |

الاتصالات المفتوحة تُسجَّل في المجموعة `ExtDB` (الحاوية `catalog`، الحقل `openCount`).

### إعداد `rtdb` (Realtime Database عبر REST)
| المفتاح | المعنى |
|---|---|
| `url` (إلزامي) | `https://<proj>.example-rtdb.com` |
| `auth` | database secret أو ID token (يُرسَل `?auth=`) |
| `accessToken` | OAuth2 (يُرسَل `Authorization: Bearer`) |
| `root` | جذر اختياري داخل القاعدة |
| `serverQuery` | `true` ← `find` عبر `orderBy/equalTo` على الخادم (يحتاج `.indexOn`)؛ الافتراضي تصفية عند العميل |

الجدول = عقدة `/<table>` والمستند = `/<table>/<id>`. المفاتيح لا تحوي `. $ # [ ] /`، ويُسمح بجدول متداخل `a/b`.

### إعداد `pgrest` (PostgREST — `/rest/v1`)
| المفتاح | المعنى |
|---|---|
| `url` (إلزامي) | `https://<ref>.example-pgrest.com` |
| `key` (إلزامي) | anon أو service_role key (يُرسَل `apikey` و`Bearer`) |
| `token` | JWT المستخدم بدل المفتاح في `Bearer` (لتفعيل RLS باسمه) |
| `schema` | الافتراضي `public` |
| `idColumn` | عمود المعرّف، الافتراضي `id` |
| `pageSize` | حجم صفحة القراءة، الافتراضي 1000 |

الجدول = جدول Postgres حقيقي تنشئه أنت في pgrest، والمستند = صف.

## المستندات (نفس الأسماء مع المزوّدين)
`xdb_get(c,t,id)` (nil إن غاب) · `xdb_has` · `xdb_put(c,t,id,doc)` (rtdb: يستبدل — pgrest: upsert يدمج الأعمدة) ·
`xdb_insert` (يرفض المكرر) · `xdb_add(c,t,doc)` (معرّف يولّده المزوّد ويُرجَع) · `xdb_update(c,t,id,patch)` (false إن غاب) ·
`xdb_delete` (false إن غاب) · `xdb_ids` · `xdb_count` · `xdb_all` · `xdb_find(c,t,field,value)` ·
`xdb_list(c,t,{"where":{..},"order":"f","desc":true,"limit":n,"offset":n})`.
كل مستند يعود وفيه `_id`. الأخطاء تُرمى كنص يبدأ بـ`extdb:` ويُلتقط بـ`catch (e)` ← `e["message"]`.

مزوّد واحد فقط: `xdb_rpc(c, fn, args)` ← دالة Postgres (pgrest). وللطلبات الخاصة: `xdb_raw(c, method, path, query, body)`
(rtdb: المسار نسبي للجذر + `query` مثل `orderBy="age"&limitToFirst=3`؛ pgrest: المسار نسبي لـ`/rest/v1` مثل `users?age=gt.25`).

## الحاويات
- `xdb_pull(c, table, containerName, opts)` ينشئ `container.doc` محلية إن لم توجد وينسخ إليها كل مستندات الجدول البعيد. يُرجع العدد. `{"clear": true}` يفرّغ الحاوية أولاً.
- `xdb_push(c, table, containerName, opts)` يرفع مستندات الحاوية إلى الجدول (put). `{"prune": true}` يحذف من البعيد ما ليس في الحاوية.

### داخل حاوية / مجموعة / حجم
```rin
@import "extdb";
@container=app
    let db = xdb_open("pgrest", "shop", {"url": "https://xyz.example-pgrest.com", "key": "<key>"});
    let users = db.table("users");
    fun sync() { return users.pull("Users", nil); }
.end/container
print app.sync();
```
الكائن المربوط يعرض نفس الدوال بلا تمرير الاتصال: `db.get(t,id)` و`db.table(t).put(id,doc)` و`.pull/.push/.list/.find/...`.
نداء دالة مخزَّنة في حقل عبر مسار طويل مباشرةً غير مدعوم في اللغة، فانسخ الكائن لمتغير أولاً (كما في rindb).

## حدود صريحة
- كل عملية طلب HTTP مستقل: لا معاملات عبر عدة عمليات، و`xdb_insert`/`xdb_update`/`xdb_delete` في rtdb تفحص الوجود بطلب منفصل (غير ذرّية).
- `xdb_push`/`xdb_pull` ينقلان مستنداً مستنداً؛ للجداول الكبيرة جداً الأنسب أدوات الاستيراد الخاصة بالمزوّد.
- لا تضع المفاتيح السرّية (service_role، database secret) في كود يُوزَّع للمستخدمين؛ استعمل anon key مع RLS أو قواعد rtdb.
- **الاختبار:** `python3 tests/tools/extdb_mock.py 8765` يشغّل محاكياً محلياً لـواجهة rtdb REST وواجهة PostgREST، ثم `rin tests/verification/extdb_lib.rin` ويقارَن بـ`extdb_lib.expected`. لم يُجرَّب مع خدمات rtdb/pgrest الحقيقية هنا (لا وصول شبكي).

---
# التقوية والحاويات و indsin

## التقوية
| الميزة | الاستعمال |
|---|---|
| **مهلة وإعادة محاولة** | في `cfg`: `timeoutMs`، `retries` (الافتراضي 2)، `retryDelayMs` (200، يتضاعف حتى 5 ثوانٍ). `GET/PUT/DELETE` تُعاد عند فشل الاتصال أو 429/500/502/503/504؛ `POST/PATCH` فقط عند 429/503. |
| **كاش القراءة** | `cacheTtl` بالثواني (الافتراضي 0 = معطّل) لـ`get/all/ids`؛ يُبطَل تلقائياً عند أي كتابة في الجدول؛ النتيجة المخزَّنة تُنسخ فلا يعدّل المستدعي الكاش. `xdb_clearCache(c[, t])`. |
| **عوامل الاستعلام** | `{"where": {"age": {"gte": 25, "lt": 40}, "name": {"in": ["Sara","Lina"]}}}` — العوامل: `eq ne gt gte lt lte like ilike in is` (`like` بنجمة: `"S*"`). على الخادم في pgrest وعند العميل في rtdb. |
| **`select`** | `xdb_list(c, t, {"select": ["name"]})` — يُبقي `_id` دائماً. |
| **دفعات** | `xdb_putMany(c, t, {id: doc} \| [docs بـ _id])` و`xdb_deleteMany(c, t, ids)` — طلب واحد في الغالب. `xdb_push` صار يستعمل `putMany`. |
| **أخرى** | `xdb_findOne` · `xdb_incr(c,t,id,field,by)` (قراءة ثم كتابة، غير ذرّية) · `xdb_info(c)` يعرض `requests/retries/cacheHits`. |
| **مفاتيح دوّارة** | `cfg["key"]` / `"auth"` / `"token"` / `"accessToken"` قد تكون **دالة** تُستدعى في كل طلب (لتجديد الرمز). |
| **مزامنة** | `xdb_sync(c, t, cname, {"prefer": "remote" \| "local"})` ← يسحب الناقص محلياً ويرفع الناقص بعيداً ويحسم المختلف حسب `prefer`، ويُرجع `{pulled, pushed, conflicts, same}`. |

## الاستيراد داخل حاوية / مجموعة
```rin
@Containers.Group=Shop
    @container=Store
        @import "extdb";                                   // الاستيراد داخل جسم الحاوية
        let db = nil;
        fun open() { db = xdb_open("pgrest", "shop", {"url": "...", "key": "..."}); return db.provider; }
        fun names() { let out = []; for (let d in db.list("users", {"order": "age"})) { push(out, d["name"]); } return out; }
    .end/container
.end/Containers.Group
print Shop.Store.open();
print Shop.Store.names();
```
ملاحظات (مُختبَرة في `tests/verification/extdb_container.rin`):
- دوال المكتبة تصير **خاصة بتلك الحاوية** (`Shop.Store.xdb_...` داخلياً)؛ استيراد ثانٍ على المستوى الأعلى يُتجاهل كمكرَّر، فاستورد حيث تحتاج.
- لا تسمِّ دالة بحاوية باسم دالة أصلية (مثل `seed`).
- المسار الطويل لنداء دالة مخزَّنة في حقل غير مدعوم (`Shop.Store.db.get(...)`)؛ ضعه داخل دالة في الحاوية.

## في indsin
```rin
<Import src="extdb" />          <!-- يعمل على المستوى الأعلى وكذلك داخل <Container> -->
```
(مثالان جاهزان: `examples/samples/indsin_extdb_demo.rin` و`indsin_extdb_container.rin`، اختُبرا بنقر حقيقي عبر `rin_indsin_preview`.)

قواعد مهمة:
1. **كل `onTap` يُنفَّذ في مفسّر جديد**: لا اتصال محفوظاً بين نقرتين. افتح الاتصال داخل المعالج بـ`xdb_session(provider, cfg)` (غير مسجَّل في السجل العام فلا تصادم أسماء) ثم اكتب النتيجة في خلايا `warp`.
2. **قيم الخصائص `{ }` نقية**: الشبكة ممنوعة فيها (حارس النقاء). اقرأ في المعالج وعرض الناتج في الخاصية.
3. مساعدات نقية تصلح داخل الخصائص: `xdb_cell(rows, i, "field")` (نص آمن، فارغ إن غاب) · `xdb_len(rows)` · `xdb_page(rows, page, size)` · `xdb_msg(e)` (نص خطأ بلا بادئة `extdb:`).
4. دالة `fun` تستعمل خلية warp كمتغير عام داخل خاصية قد لا ترى قيمتها الحالية؛ مرّر الخلايا كوسائط: `{shown(rows, page, 0)}`.

```rin
<Fun#load>
  try { rows = xdb_list(xdb_session("pgrest", {"url": "...", "key": "..."}), "users", {"order": "age"}); status = "ok"; }
  catch (e) { status = xdb_msg(e); }
</Fun>
<Text#t>{xdb_cell(rows, 0, "name")}</Text>
<Button#b onTap={load()}>Load</Button>
```

## الاختبارات
`extdb_lib` (الأساس) · `extdb_pro` (كاش، عوامل، دفعات، إعادة محاولة، مزامنة، مساعدات indsin، مفتاح دالة) · `extdb_container` (استيراد داخل مجموعة/حاوية). كلها مع المحاكي `python3 tests/tools/extdb_mock.py 8765`، والمقارنة بملفات `.expected`.

---
# الميزات الكاملة (1.0.0)

## جلسات المستخدمين
`xdb_signIn(c, email, password)` · `xdb_signUp` · `xdb_refresh(c)` · `xdb_signOut(c)` · `xdb_user(c)` · `xdb_isSignedIn(c)` · `xdb_setToken(c, token, refresh, expiresInSec)`.
الرمز يُستعمل تلقائياً في كل طلب بدل مفتاح التهيئة، ويُجدَّد قبل انتهائه بـ30 ثانية، ويُفرَّغ الكاش عند تغيّر الجلسة.
- **pgrest:** نقاط `/auth/v1` على المضيف نفسه (أو `cfg.authUrl`).
- **rtdb:** خدمة هوية بنمط `accounts:signInWithPassword`: تحتاج `cfg.authUrl` و`cfg.apiKey`، وللتجديد `cfg.refreshUrl`. بلا هذا الإعداد يُرمى خطأ يوضّح الناقص (لا تُخمَّن عناوين).

## مخطط محلي
`xdb_schema(c, "users", {"name": "string!", "age": "number", "tags": "array"})` — `!` إلزامي؛ الأنواع `string number bool map array any`. يُفحص قبل `put/insert/add/putMany/tx.put` (و`update` للأنواع فقط)، فلا يخرج طلب بمستند فاسد. `xdb_schema(c, t, nil)` يزيله.

## طابور العمل دون اتصال
`cfg.offlineQueue = true`: عند **فشل الاتصال** (وليس ردّ الخادم) تُحفظ `put/update/delete/putMany/deleteMany` وتعود `"queued"`. `cfg.queueFile` يحفظ الطابور في ملف JSON ويستعيده عند الاتصال التالي. `xdb_flushQueue(c)` ← `{sent, left, failed}` (يتوقف عند أول فشل اتصال، وما يرفضه الخادم يذهب إلى `c["dead"]`) · `xdb_queueSize` · `xdb_clearQueue`.

## مراقبة التغييرات (polling — بلا خيوط)
`xdb_watch(c, t, fun(ev){...}, {"initial": false})` ← معرّف · `xdb_tick(c)` يفحص ويستدعي المعالجات (يُرجع عدد الأحداث) · `xdb_watchLoop(c, intervalMs, rounds)` · `xdb_unwatch(c, id)` · `xdb_diff(oldSnap, newSnap)`.
الحدث: `{type: added|changed|removed, table, id, doc, old}`.

## دفعة كتابة، تصدير، تجميع
- `let tx = xdb_tx(c); tx.put(t,id,doc); tx.delete(t,id); tx.commit()` يدمج العمليات المتجاورة على الجدول نفسه في طلب واحد؛ `tx.rollback()` · `tx.size()` · `tx.pending()`. **ليست ذرّية** عبر الطلبات.
- `xdb_export(c,t,path)` · `xdb_import(c,t,path,{"chunk":500,"idField":"_id"})` · `xdb_backup(c,[tables],dir)`.
- `xdb_agg(c,t,field,opts)` ← `{count,sum,avg,min,max}` · `xdb_each(c,t,pageSize,fun(doc){...},opts)` (أرجع `false` للتوقف).

## المراقبة والأخطاء
`xdb_setLogger(c, fun(e){...})` (أو `cfg.logger`) يستلم `{method,url,status,ok,ms,attempt}` والأسرار (`auth=`/`key=`) مُخفاة · `xdb_health(c)` ← `{ok,ms,error}` · `xdb_info(c)` · أخطاء مصنَّفة: `xdb_errKind(e)` (`connection|http|schema|other`) و`xdb_errStatus(e)`.

## تطبيق RinStudio (أندرويد)
المكتبة **مدمجة** في المفسّر نفسه (`rin_stdlib_libs.h` ← `lib/extdb.og.rin`)، فيعمل `@import "extdb";` على أي جهاز بلا ملفات إضافية، وتظهر في شاشة «المكتبات» (فئة البيانات) بأيقونتها وزر الإدراج السريع. بعد أي تعديل في `lib/extdb.og.rin/` شغّل `python3 tools/embed_extdb.py` لإعادة التضمين. أيقونة الألوان: `assets/branding/extdb_icon.svg|png`، وأيقونة الشاشة: `res/drawable/ic_lib_extdb.xml`.
تذكير: طلبات الشبكة على أندرويد تمرّ عبر جسر HTTP الأصلي، وتحتاج صلاحية الإنترنت في المشروع/التطبيق المُصدَّر.

## الاختبارات
`extdb_lib` · `extdb_pro` · `extdb_container` · `extdb_full` (الجلسات، المخطط، الطابور، المراقبة، tx، التصدير، التجميع، المسجّل). كلها مع `python3 tests/tools/extdb_mock.py 8765` وتقارَن بملفات `.expected`.
