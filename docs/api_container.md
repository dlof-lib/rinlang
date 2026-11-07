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
