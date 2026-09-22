# http.md — شبكات

## طلبات HTTP مباشرة
```rin
let r = httpGet("https://example.com");
print r.ok;      // true/false -- نجح الاتصال فعلياً (بصرف النظر عن status)
print r.status;  // رمز الحالة (403 مثلًا)
print type(r.body); // string -- نص الجسم الخام
print r.json;    // نفس الجسم مُفكَّكًا كـ JSON تلقائيًا إن أمكن، وإلا القيمة الخام كنص
print r.error;   // نص خطأ إن فشل الاتصال نفسه (فارغ إن نجح، بصرف النظر عن status)
```
كل استدعاء يُجري **طلب شبكة حقيقيًا فعليًا** (عبر `http::performRequest`، خلف
`rin_http.cpp`) — وليس محاكاة. فشل الاتصال (رابط غير موجود، DNS، رفض اتصال، مهلة)
لا يرمي خطأً يوقف البرنامج — يُرجع نتيجة بـ`ok: false` ورسالة عربية واضحة في
`error` (تحقّقتُ من هذا فعليًا: `"فشل الاتصال (DNS/رفض الاتصال/انتهاء المهلة) —
تحقق من الرابط والشبكة"`).

| الدالة | الوصف |
|---|---|
| `httpGet(url)` | GET. |
| `httpPost(url, body)` | POST. |
| `httpPut(url, body)` | PUT. |
| `httpPatch(url, body)` | PATCH. |
| `httpDelete(url)` | DELETE. |
| `httpRequest(method, url, headers, body)` | طلب عام بأي فعل HTTP، وترويسات مخصَّصة (قاموس). |
| `httpSetTimeout(ms)` | يضبط مهلة كل الطلبات التالية بالمللي ثانية. |

`body` لأي طلب POST/PUT/PATCH: قيمة نصية تُرسَل كما هي، أو قاموس/مصفوفة يُحوَّل
تلقائيًا إلى JSON (مع ترويسة `Content-Type` مناسبة تُضاف تلقائيًا عند الحاجة).

## طبقة API مسمّاة (لتجميع رابط أساسي + ترويسات ثابتة)
مفيدة عندما تستدعي نفس الخدمة عدّة مرات (مفتاح API/توكن واحد لا يتكرر كتابته):
```rin
apiRegister("myapi", "https://api.example.com");
apiHeader("myapi", "Authorization", "Bearer xyz");

let r = apiGet("myapi", "/users/1");
print r.ok;
// apiRegister بلا تسجيل مسبق -> خطأ واضح (E0037):
// "لا يوجد API حقيقي مسجَّل باسم 'x' — سجِّله أولاً: apiRegister("x", "https://...");"
```
| الدالة | الوصف |
|---|---|
| `apiRegister(name, baseUrl)` | يسجِّل خدمة باسم + رابط أساسي. |
| `apiHeader(name, key, value)` | يضيف ترويسة ثابتة تُرسَل مع كل طلب لهذا الاسم. |
| `apiGet(name, path)` / `apiPost(name, path, body)` / `apiPut(...)` / `apiPatch(...)` / `apiDelete(name, path)` | يبني الرابط الكامل (`baseUrl + path`) ويُجري الطلب بترويسات الخدمة المسجَّلة. |
| `apiCall(name, method, path, body)` | نفس الفكرة بفعل HTTP عام صريح. |

## JSON
```rin
let s = jsonEncode({"name": "Ali", "tags": [1, 2, 3]});
print s; // {"name":"Ali","tags":[1,2,3]}
print jsonDecode(s)["name"]; // Ali
```
`jsonDecode` على نص غير صالح كـJSON يُرجع النص الخام كما هو (`decodeOrRaw`) بدل
رمي خطأ — نفس السلوك الذي يستخدمه `r.json` أعلاه تلقائيًا لأي استجابة HTTP.

## انظر أيضًا
- [`standard-library.md`](./standard-library.md) — `httpkit.og.rin`/`urlkit.og.rin` (مساعدات إضافية فوق هذه الدوال).
- [`errors.md`](./errors.md) — E0037 (خطأ شبكة)، الكود المستخدَم عند استدعاء API غير مسجَّل.
- [`storage.md`](./storage.md) — تخزين نتيجة استجابة على القرص إن احتجت الاحتفاظ بها.
