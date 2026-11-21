# container_fetch.md — جلب البيانات في الخلفية وتحديث جزء من الصفحة فقط

الاتصال بالخوادم لجلب بيانات جديدة (إشعارات، بحث فوري، أسعار عملات…) **دون إعادة تحميل الصفحة**، وتحديث **العناصر المرتبطة بالنتيجة فقط**.
في [`indsin2web`](./indsin2web.md) تُكتب كـ `@container.api` بحقل `baseUrl` (نفس حاوية [`api_container.md`](./api_container.md) في المفسّر الأصلي)؛
وفي [RinHTML](#rinhtml-سمات-rin-fetch) كسمات HTML. بلا `baseUrl` تبقى `container.api` محاكاة `route` كما كانت تماماً.

```kotlin
@container.api=rates
    let baseUrl = "https://open.er-api.com/v6";
    let path    = "/latest/USD";     // يُجلب تلقائياً عند فتح الصفحة
    let pick    = "rates";           // مسار منقّط داخل JSON ⇒ يصير data
    let every   = 30s;               // تحديث دوري في الخلفية
    let persist = true;              // آخر data تبقى في المتصفح فتظهر فوراً قبل وصول الجديد
    let data    = {};
.end/container.api
```
```xml
<Text#Eur text={"EUR: " + (rates.updated > 0 ? str(round(rates.data["EUR"], 3)) : "…")} />
<Text#Stamp text={rates.loading ? "جارٍ التحديث…" : (rates.error == nil ? "آخر تحديث" : "خطأ: " + rates.error)} />
```

## كيف يتحدّث «الجزء» فقط؟
كل خاصية في الواجهة تعبير يُعاد تقييمه عند أي تغيّر، ولا يُكتب في الـ DOM إلا إن **تغيّرت قيمته**. فتحديث `rates.data` يغيّر
`Eur` وحدها؛ أمّا العنوان والسلة وبقية الصفحة فلا تُلمَس عقدها (يتحقّق الاختبار بـ `MutationObserver`: 0 تعديلات خارج العنصر المرتبط).

## الحقول التفاعلية
`data` (آخر نتيجة ناجحة؛ **تبقى ظاهرة عند الفشل**) · `status` · `error` (`nil` عند النجاح، وإلا `"HTTP 503: …"` أو `"timeout"`) ·
`updated` (لحظة آخر نجاح بالميلي ثانية) · `loading`. وأي `let` إضافي يصير حقلاً عادياً.

## الإعداد
| الحقل | المعنى |
|---|---|
| `baseUrl` | **مطلوب لتفعيل الجلب الحقيقي** (`https://…` أو مسار نسبي `/api`). يُقبل `http/https` فقط |
| `path` | مسار يُجلب تلقائياً (أو `every`). `autoload = false` يعطّل الجلب الأول |
| `method` · `headers` · `query` · `body` · `token` | `token` ⇒ `Authorization: Bearer …` (ما لم تُعرَّف ترويسة `Authorization`) · `query` تُدمَج مع query كل طلب |
| `every` | تكرار دوري (`30s`، `1m`). **يتوقف والتبويب مخفي أو بلا اتصال** ويُستأنف فور العودة |
| `debounce` | تأجيل الطلب بعد آخر نداء (للبحث الفوري): ضغطات متتالية ⇒ طلب واحد |
| `cache` | مدة كاش لطلبات GET في الذاكرة (`1m`) |
| `retry` · `backoff` · `timeout` | إعادة محاولة عند فشل الشبكة أو 408/429/5xx بتراجع أُسّي (`backoff` الأساس، 500ms) · المهلة الافتراضية 15s |
| `pick` | مسار منقّط داخل الردّ (`"data.items"`) |
| `persist` | `true` ⇒ تُحفَظ آخر `data` و`updated` في `localStorage` (`rin:api:<الاسم>`) وتُعرَض قبل وصول الردّ |
| `pauseHidden` | `false` ⇒ يستمر الدوري والتبويب مخفي |
| `credentials` | `"same-origin"` (الافتراضي) · `"include"` |

كل المدد أرقام بالميلي ثانية في Rin (`30s` ⇒ 30000) وتُقبل أيضاً نصاً `"1h30m"`.

## الاستدعاء
```kotlin
fun find(term) { suggest.get("/search", {"query": {"q": term}}); }      // الدوال: get/post/put/patch/delete/request/refresh
fun send()     { shop.post("/orders", {"id": 7}, {"retry": 2}); }
fun legacy()   { callApi("shop", "GET", "/items", {"params": {"id": 7}}); }   // توقيع المفسّر الأصلي نفسه
```
`opts` لكل طلب: `params` (يستبدل `:id` في المسار؛ مسار فيه معامل غير مملوء يُرفض) · `query` · `headers` · `token` · `retry` · `backoff` · `timeout` ·
`cache` · `debounce` · `pick` · `latest` · `then` (دالة تُستدعى بـ `{ok, status, error, json, body, cached}`).
`rates.refresh()` يعيد آخر طلب · `rates.abort()` · `rates.start()` / `rates.stop()` للدوري · `rates.info()` ⇒ `{calls, failed, cached, aborted, polling…}`.

**الأحدث يربح**: طلب GET جديد يلغي السابق (`AbortController`)، وأي ردّ قديم يصل متأخراً يُتجاهَل — فلا يُكتب نتيجة بحث قديمة فوق أحدث.
طلبات الكتابة (POST/PUT/PATCH/DELETE) لا تُلغى تلقائياً. ربط حقل إدخال: `onInput={find(ev.target.value)}`.

## قيود
- الخادم الهدف يجب أن يسمح بالـ CORS (أو يكون من نفس الأصل).
- `route` (المحاكاة) داخل حاوية بـ `baseUrl` تُهمَل بتحذير: الطلبات تذهب للخادم الحقيقي.
- النتيجة غير متزامنة: اقرأ الحقول (`data/error/loading`) أو مرّر `then`؛ لا يوجد `await` في Rin.
- لا تضع أسراراً في الصفحة (الرمز `token` ظاهر لمن يفتح الصفحة).
- عرض قائمة بطول غير معروف: `<<each feed.data as n, i>> … <</each>>` يعمل في الويب (يُعاد بناء القائمة فقط عند تغيّر بياناتها) — انظر `examples/samples/indsin_live_list.rin` وقيوده في `indsin2web.md`.

## RinHTML: سمات `rin-fetch`
تحتاج الصلاحية `fetch` (`<rin-app allow="dom fetch">`):
```html
<span rin-fetch="/api/rates" rin-pick="rates.EUR" rin-into="eur" rin-every="30s" rin-text="eur"></span>
<input rin-fetch="/api/search?q={q}" rin-fetch-on="input" rin-debounce="250" rin-into="results" rin-model="q">
<ul rin-each="results"><template><li rin-field="name"></li></template></ul>
```
`rin-fetch` الرابط (`{q}` يُستبدل بمتغيّر Rin العام، مرمَّزاً) · `rin-into` اسم النتيجة: قيمة بسيطة ⇒ تُكتب في متغيّر Rin العام بنفس الاسم، وكائن/مصفوفة ⇒ `app.getData(name)` ·
`rin-pick` مسار منقّط · `rin-every` دوري · `rin-fetch-on` حدث يطلق الجلب (وإلا عند التحميل) · `rin-debounce` بالميلي ثانية · `rin-error` متغيّر يستقبل نص الخطأ.
`rin-each` يكرّر `<template>` لكل عنصر، و`rin-field="a.b"` (أو `.` للعنصر نفسه). أثناء الطلب يحمل العنصر `aria-busy="true"`.
`rin-text`/`rin-show`/`rin-each` لا تكتب في الـ DOM إلا ما تغيّر. بلا `allow="fetch"` لا يُرسَل أي طلب.

## الاختبار
`bash tests/indsin2web/run.sh` يشغّل خادماً وهمياً + Chromium: ظهور السعر، التحديث الدوري بلا لمس بقية الصفحة، بقاء آخر قيمة عند فشل الخادم ثم التعافي،
`debounce` (3 ضغطات ⇒ طلب واحد)، «الأحدث يربح» أمام ردّ قديم بطيء. و`node tests/rinhtml/browser_storage_fetch.js` لسمات RinHTML.
