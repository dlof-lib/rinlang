# api_container.md — `container.api` الحقيقي والكامل

حاوية `api` تصير **حقيقية** بمجرد وجود الحقل `baseUrl` (أو `apiRegister` بنفس اسمها). بلا `baseUrl` تبقى محاكاة `route` كما كانت تماماً.

```kotlin
@container.api=shop
    let baseUrl = "https://api.example.com";
    let headers = {"X-App": "rin"};      // ترويسات ثابتة
    let token   = "T1";                   // يصير Authorization: Bearer T1 (ما لم تكن ترويسة Authorization معرّفة)
    let timeout = 5s;                     // مهلة كل طلب
    let retry   = 2;  let backoff = 1s;   // إعادة محاولة عند الفشل أو status >= 500
    let cache   = 30s;                    // كاش لطلبات GET
    let mock    = false;                  // true = يفرض route الوهمية بنفس الكود (اختبار بلا شبكة)
    route method="GET" path="/items" status=200 body={"mock": true};   // عقد/محاكاة
.end/container.api
```
**الحقول تُقرأ وقت كل استدعاء**: `shop.token = "T2"` أو `shop.mock = true` يسري فوراً. (الحقل يجب أن يكون مُعرَّفاً داخل الحاوية.)

## الاستدعاء
`callApi(c, method, path, [body], [opts])` و`call(method, path, [body], [opts])` (من داخل الحاوية):
- **GET/DELETE**: الوسيط الرابع = `opts`. **POST/PUT/PATCH**: الرابع = `body` (خريطة تُرمَّز JSON تلقائياً) والخامس = `opts`.
- `opts`: `params: {id: 7}` يستبدل `:id` في المسار (يُرفض مسار فيه معامل غير مملوء) · `query: {page: 2}` يلحق `?page=2` (بترميز URL) · وكل خيارات timer: `retry backoff timeout cache after at every times then` (انظر `TIMER.md`). ما في `opts` يغلب الحقول.
- الرد: `{ok, status, body, json, error}` (+ `attempts` مع retry، + `cached` عند إصابة الكاش).

## `apiInfo(name)`
`{name, mode: "real"|"mock", baseUrl, routes, headers (أسماء)، calls, failed, lastStatus}` — الإحصاءات للاستدعاءات المتزامنة.

## التوافق
`apiRegister/apiHeader/apiGet/apiPost…` تعمل كما كانت (وتقبل خيارات timer). الاختبار: `tests/print/container_api_real.rin` (حتمي بلا شبكة خارجية).

## الربط مع indsin
- **حالة تُنشَر تلقائياً**: إن عرّفتَ داخل حاوية api أيّاً من الحقول `data` · `status` · `error` · `updated` · `loading` فكل استدعاء (متزامن أو مجدوَل بـ `after/every`) يملؤها: `data` = JSON الردّ (يبقى آخر ناجح عند الفشل)، `status`، `error` (nil عند النجاح)، `updated` (لحظة الردّ)، `loading` (true أثناء الطلب/إعادة المحاولة).
- **تقرؤها الواجهة مباشرة**: `<Text text={"s=" + shop.status} />`. بعد كل لمسة (`onTap={load()}` الذي يستدعي `callApi`) وكل نبضة `rin_indsin_session_tick` تُعاد تلقائياً كل خاصية تقرأ اسماً ليس خلية warp (حاوية، متغيّر عام) — ويظهر اسم الحاوية في `changed`.
- **الاستدعاء المجدوَل/الدوري** (`{every: 30s}` أو `{after: 5s}`) يُنفَّذ في `session_tick` فتتحدّث الواجهة دون لمس؛ والمضيف يواصل النداء ما دام `"ticking":true`.
- **نقاء الخصائص**: `callApi/apiGet…` ممنوعة داخل خصائص الواجهة (تُستدعى من المعالجات فقط)؛ وصارت `timers.after/every/…` و`timerStart…` و`time.freeze/advance` ممنوعة أيضاً، أمّا قراءاتها (`timers.list/pending/ttl`، `timerInfo`، `apiInfo`) فمسموحة.
- مثال: `examples/samples/container_api_indsin_demo.rin` · اختبار: `tests/tools/test_container_api_indsin.cpp`.

## صورة معاينة (preview) لاستجابة api
- **ردّ صورة حقيقي**: إن كان `Content-Type` من نوع `image/*` أو بصمة الجسم PNG/JPEG/GIF/WebP/BMP، يحصل الردّ على مفاتيح إضافية: `image` (= `data:image/…;base64,…` جاهز لـ `<Image src={…}/>`) و`contentType` و`bytes`؛ و`json` = `nil`، ويبقى `body` كما وصل (للحفظ بـ `writeFile`). أكبر من 10MB ⇒ بلا `image` مع `error`. غير الصور: لا مفاتيح جديدة. إن أفسد الجسر النصّي (أندرويد) بايتات صورة GET أُعيد الجلب بمسار ثنائي آمن.
- **حقل `preview` في الحاوية**: إن عرّفتَه (`let preview = nil;`) يُملأ تلقائياً بعد كل استدعاء ناجح: data URI الصورة؛ أو جسم mock نصّي `data:image/…`؛ أو أول رابط في JSON بالترتيب `preview · image · thumbnail · avatar · icon · picture · photo`؛ وإلا `nil`. الفشل لا يمسّ آخر معاينة. وفي ردّ الصورة يصير `data` = data URI أيضاً.
- **في الواجهة**: `<Image#Pic src={shop.preview} width={64} height={64} />` — تتحوّل من بلا مصدر إلى `src_kind: data_uri` أو `url` بعد استدعاء `callApi` من معالج، أو بنبضة للاستدعاء المجدول.
- الاختبارات: `tests/print/container_api_preview.rin` · `tests/tools/test_container_api_indsin.cpp`.

## مثال كامل: متصفّح مستودعات GitHub
`examples/samples/github_browser.rin` — `container.api` + indsin: قائمة مستودعات مستخدم (`GET /users/:u/repos?per_page=4&sort=updated`)، وعند اختيار مستودع: المحتويات (`/contents`) وREADME (base64 ← `sec.base64Decode`) ونسب اللغات (`/languages`)، مع صورة المالك من `owner.avatar_url` تلقائياً في `preview`.
- الترويسات `Accept: application/vnd.github+json` و`User-Agent` حقل `headers`؛ و`let token = "ghp_…"` يضيف `Authorization: Bearer` (يرفع حدّ المعدّل). `retry/backoff/timeout/cache` حقول أيضاً.
- **GitHub الحقيقي**: بدّل `baseUrl` إلى `https://api.github.com`. (بيئة التطوير هذه لا تصل إلى `api.github.com` إلا لمستودعات مرتبطة بالجلسة، فاختُبر المتصفّح على `tools/mock_github_server.py` الذي يحاكي نقاط GitHub وأشكال ردودها.)
- **اختبار بلا شبكة**: `tests/print/github_browser_mock.rin` (نفس الحاوية بـ `mock = true` ومسارات `route`) — `params`/`query` تعمل في الحقيقي والمحاكاة (في المحاكاة يُطابَق المسار بلا الـ query).
- **قاعدة indsin**: خصائص الواجهة تتبع المتغيّرات المذكورة في تعبيرها، فمرّر الحالة وسائط: `text={repoLine(k, repos)}` لا `repoLine(k)`.
- **أخطاء HTTP**: `ok` = تمّ الاتصال؛ حقول الحاوية تعتبر النجاح 2xx فقط: عند 404 يُملأ `error = "HTTP 404: Not Found"` ويبقى `data` آخر نتيجة ناجحة. `apiInfo().failed` يعدّ الاتصال الفاشل وأي status ≥ 400. ويعمل `preview` أيضاً مع مصفوفة أو `owner.avatar_url`.
