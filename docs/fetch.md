# fetch — استدعاء الروابط (HTTP) الحقيقي الكامل

عميل HTTP حقيقي فوق curl (بلا shell، **http/https فقط**؛ `file://` وغيرها مرفوضة). لا يوجد اسم عام `fetch(...)` حتى لا يتعارض مع دوال المستخدم
التي تحمل الاسم نفسه؛ كل شيء تحت `fetch.*`:

| الدالة | الوصف |
|---|---|
| `fetch.request(url[, opts])` | الطلب العام (الطريقة من `opts.method`، أو POST تلقائياً عند وجود جسم) |
| `fetch.get/delete/head(url[, opts])` | اختصارات |
| `fetch.post/put/patch(url[, body[, opts]])` | الجسم نصّ ⇒ كما هو، أي نوع آخر ⇒ JSON |
| `fetch.json(url[, opts])` | يعيد `json` مباشرة ويرمي خطأً عند الفشل (strict) |
| `fetch.text(url[, opts])` | يعيد النص ويرمي عند الفشل |
| `fetch.download(url, path[, opts])` | تنزيل متدفّق إلى ملف (لا يمرّ عبر الذاكرة؛ `path.part` ثم إعادة تسمية) |
| `fetch.clearCookies([jar])` | يحذف جرّة كوكيز (أو كلّها) ويعيد العدد |

## الردّ
`{ ok (2xx)، status، statusText، method، url (النهائي بعد التحويلات)، redirected، redirects، headers (أحرف صغيرة)، cookies (Set-Cookie)،
body، json (مُحلَّل تلقائياً)، contentType، size، time (ms)، attempts، cached، error }` — وللصور `image` (data URI) جاهزة لـ `<Image src=…/>`.
الأخطاء الشبكية (DNS، رفض اتصال، مهلة، TLS) لا ترمي: `ok=false` و`status=0` و`error` نصّ واضح، إلا مع `strict`.

## الخيارات (`opts`)
| الخيار | المعنى |
|---|---|
| `method`, `headers`, `query` | `query` خريطة (المصفوفة تكرّر المفتاح، `nil` يُتجاهل) وتُرمَّز تلقائياً |
| `json` / `form` / `multipart` / `body` | واحد فقط. `multipart`: `{اسم: نص}` أو `{اسم: {path: "ملف", type:, filename:}}` (المفتاح `file` كلمة محجوزة فاستعمل `path` أو `"file"`) |
| `bearer`, `basic` | `basic`: `["u","p"]` أو `{user, pass}` أو `"u:p"`؛ لا تتجاوز ترويسة Authorization صريحة |
| `timeout`, `connectTimeout` | مدد (`500ms`, `10s`) |
| `retry`, `backoff`, `backoffFactor`, `retryOn` | إعادة المحاولة عند فشل الشبكة أو الحالات في `retryOn` (الافتراضي 408 425 429 500 502 503 504)؛ يحترم `Retry-After` (حتى 60s) ويستخدم الساعة المتحكَّم بها (`time.freeze`) |
| `cache` | مدة كاش لطلبات GET الناجحة (`cached=true` عند الإصابة) |
| `follow`, `maxRedirects` | التحويلات (الافتراضي: نعم، 5) |
| `cookies` | `true` أو اسم جرّة: يحفظ ويرسل الكوكيز بين الطلبات |
| `userAgent`, `maxBytes`, `proxy`, `compressed` | gzip/deflate تُفكّ تلقائياً |
| `strict` | يرمي خطأً عند غير 2xx أو فشل الشبكة |
| `into` | اسم حاوية: يملأ `data/status/error/updated/loading/preview` كـ container.api (فيتفاعل indsin) |
| `then`, `after`, `at`, `every`, `times` | جدولة الطلب (وإعادة تشغيله دورياً) واستدعاء `then(res)` عند كل ردّ |

الخيارات المجهولة أو المتناقضة تُرفض فوراً بخطأ واضح (مثلاً `cache` مع POST، أو `json` مع `form`).

## الروابط `url.*`
`url.parse(u)` · `url.build({scheme?, host, port?, path?, params|query, fragment?})` · `url.join(base, rel)` · `url.encode/decode` ·
`url.params("?a=1&a=2")` · `url.query({…})` · `url.valid(u)`.

## ملاحظات
- يتطلب وجود `curl` على الجهاز (على أندرويد/ويندوز يتراجع النقل إلى الطبقة الأبسط: بلا jar كوكيز ولا إحصاءات تحويل).
- الترويسات (Authorization…) تُمرَّر عبر ملف مؤقت 0600 لا عبر سطر الأوامر فلا تظهر في `ps`.
- اختبارات: `tests/print/url_tools.rin` (بلا شبكة) و`tests/tools/test_fetch.sh` مع `tools/mock_fetch_server.py` (خادم محلي).
