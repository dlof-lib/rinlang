# `message.layer` ↔ `@container.api`

ربط مباشر بين طبقة الرسائل وحاوية الـ API الرسمية في Rin.

`@container.api` يبقى كما هو (`callApi` · `apiInfo` · `route` · `baseUrl` · `token` · …).
`message.layer` يضيف فوقه فقط: كل استدعاء يصبح **message**، مع توكن وتدقيق وبث.

---

## 1. تفعيل الطبقة على حاوية API

```rin
@container.api=svc
    let baseUrl = "https://api.example.com";
    let headers = {"X-App": "rin"};
    let token = "T1";              // أو قيمة token() من نظام الصلاحيات
    let timeout = 5s;
    let retry = 1;
    route method="GET"  path="/items" status=200 body={"ok": true};
    route method="POST" path="/items" status=201 body={"id": 1};
.end/container.api

message.layer("svc", {
    audit: true,
    emit:  true,
    // auth: session   // اختياري: يغلّب على svc.token إن وُجد
});
```

بعد `message.layer("svc")` يمكن استخدام:

```rin
let m = message.get("svc", "/items");
// بدل:
// let r = callApi("svc", "GET", "/items");
```

النتيجة `message` بمستوى حسب `status` / فشل الشبكة، و`meta` تحتوي حقول استجابة `callApi`.

---

## 2. جدول التوافق

| `callApi` / الحاوية | `message.layer` |
|---------------------|-----------------|
| `callApi(name, method, path, opts?)` | `message.call(name, method, path, body?, opts?)` |
| `callApi(..., "GET", path)` | `message.get(name, path, opts?)` |
| `callApi(..., "POST", path, body)` | `message.post(name, path, body, opts?)` |
| `apiInfo(name)` | `message.layer_info(name)` (+ حقول layer) |
| `route` وهمي / mock | يعمل كما هو؛ النتيجة تُغلَّف كـ message |
| `baseUrl` + شبكة حقيقية | نفس المسار؛ الفشل → `level: error` · `code: NET_FAIL` |
| حقل `token` في الحاوية | يُمرَّر كـ Bearer إن لم يُمرَّر `opts.token` / `auth` في layer |
| `retry` / `backoff` / `timeout` | تبقى مسؤولية `callApi`؛ الطبقة لا تعيد تنفيذها |

---

## 3. شكل الرسالة من `callApi`

```rin
let r = callApi("svc", "GET", "/items");   // التقليدي
let m = message.from_callapi(r, {api: "svc", method: "GET", path: "/items"});

// أو مباشرة:
let m = message.get("svc", "/items");
```

```rin
{
  kind:  "message",
  level: "success" | "warning" | "error" | "info",
  body:  r.body أو r.json,
  code:  "HTTP_200" | "NET_FAIL" | ...,
  meta: {
    status, ok, attempts,          // من callApi
    api: "svc",
    method, path,
    mode: apiInfo.mode,            // mock | live
    duration_ms?
  }
}
```

---

## 4. توكن الحاوية × نظام token

```rin
let session = token("user:1", ["read", "write"], {ttl: 3600});

@container.api=svc
    let baseUrl = "https://api.example.com";
    // token نصي قديم ما زال يعمل
.end/container.api

message.layer("svc", {auth: session, audit: true});

// عند message.get:
// 1) token_ok(session) اختياري
// 2) Authorization من session (id أو meta.jwt إن وُجد)
// 3) audit_log للطلب
```

إن وُجد `svc.token` نصاً و`auth` في layer، **layer.auth يغلب**.

---

## 5. معالجات `route` كرسائل

عند `mock = true` أو عدم وجود `baseUrl`، الـ `route` يعيد body/status.
الطبقة تلفّ ذلك تلقائياً.

يمكن أيضاً تسجيل معالج Rin فوق نفس المسار:

```rin
message.handle("POST", "/items", fun (req) {
    // req = message واردة
    audit_require(message_token(req), "write");
    return msg+ "created" {id: 99};
});

// message.dispatch يستخدم المعالج إن وُجد، وإلا يسقط على callApi/route
```

أولوية `dispatch` / الاستدعاء المغلّف:

1. `message.handle` مسجّل لنفس method+path  
2. وإلا `callApi` على `@container.api` بنفس الاسم  

---

## 6. دوال الربط الإضافية

| الدالة | الوظيفة |
|--------|---------|
| `message.from_callapi(r, opts?)` | تحويل نتيجة `callApi` → message |
| `message.layer_info(name)` | `apiInfo(name)` + خيارات layer |
| `message.layer_bind(name)` | يفعّل layer ويضبط defaults من حقول الحاوية (`token`, …) |
| `message.get/post/...` | إن وُجد `@container.api=name` يستدعي `callApi` ثم `from_callapi` |

---

## 7. مثال كامل

```rin
@container.api=shop
    let baseUrl = "https://httpbin.org";
    let timeout = 5s;
    let retry = 0;
    route method="GET" path="/get" status=200 body={"mock": false};
.end/container.api

let session = token("user:42", ["read"], {ttl: 600});
message.layer_bind("shop");                 // يقرأ الحقول
message.layer("shop", {auth: session, audit: true, emit: true});

let m = message.get("shop", "/get");
print message_level(m);                     // success أو error حسب الشبكة
print message_meta(m).status;
banner(m);

// تقليدي ما زال يعمل
let r = callApi("shop", "GET", "/get");
print r.ok;
```

---

## 8. قواعد

- لا تغيير على صيغة `@container.api` أو `callApi` / `apiInfo`.
- بدون `message.layer` / `message.get` السلوك القديم كامل.
- الطبقة تفضّل `callApi` عند وجود حاوية بنفس الاسم؛ وإلا `apiCall` / `httpRequest`.
- أخطاء الصلاحيات من `token_require` / `audit_require` تبقى `message` متوافقة.
- الاختبار: `tests/print/message_layer_api.rin`.
