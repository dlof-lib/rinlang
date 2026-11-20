# نظام التوكنات (`token`) — صلاحيات وجلسات ورسائل موقَّعة

إضافة **بحتة** بالكامل. لا تغيّر سلوك أي كود قائم.
تتكامل مباشرة مع:

| النظام | التكامل |
|---|---|
| `message` | رسالة موقَّعة / محمولة على توكن |
| الحاويات | `use` / `allow` / `deny` + فحص صلاحية بالتوكن |
| `throw` / الأخطاء | رفض صلاحية ← رسالة خطأ غنية |
| Banner / Dialog | إشعارات متعلقة بالجلسة أو انتهاء الصلاحية |
| السجل | `emit` لتوكنات منتهية أو مرفوضة |

---

## 1. ما هو التوكن في Rin؟

قيمة منظمة تحمل **هوية + صلاحيات + عمر + توقيع بسيط**:

```rin
{
  kind:     "token",
  id:       "tok_...",          // فريد
  subject:  "user:42",          // صاحب التوكن
  scopes:   ["read", "write"],  // الصلاحيات
  meta:     {},                 // بيانات إضافية
  issued:   1732...,            // وقت الإصدار (ms)
  expires:  1732... + 3600000,  // وقت الانتهاء (ms) أو nil = لا ينتهي
  issuer:   "app",              // من أصدره
  sig:      "...",              // توقيع بسيط (HMAC-like داخلي)
  revoked:  false
}
```

---

## 2. إنشاء التوكنات

### الدالة الأساسية
```rin
token(subject, scopes, opts?)
```

`opts` خريطة اختيارية:
- `expires: ms` أو `ttl: seconds`
- `meta: {}`
- `issuer: "name"`
- `id: "custom-id"` (وإلا يُولَّد)

```rin
let t = token("user:7", ["read", "profile"], {ttl: 3600, meta: {role: "admin"}});
print t.id;
print t.scopes;
print token_ok(t);          // true
```

### اختصارات سياقية
| الصيغة | المعنى |
|---|---|
| `tok subject scopes` | إنشاء سريع |
| `tok subject scopes ttl N` | مع مدة بالثواني |
| `tok subject scopes {meta}` | مع meta |

`tok` كلمة **سياقية فقط** (مثل `msg`).

---

## 3. التحقق والفحص

```rin
token_ok(t)                 // صالح الآن؟ (غير منتهٍ + غير ملغى + توقيع صحيح)
token_expired(t)
token_revoked(t)
token_has(t, "write")       // هل يملك الصلاحية؟
token_has_all(t, ["read", "write"])
token_has_any(t, ["admin", "mod"])
token_age(t)                // بالثواني منذ الإصدار
token_remaining(t)          // ثوانٍ متبقية (أو nil)
token_subject(t)
token_scopes(t)
token_meta(t)
token_id(t)
```

### فحص صارم (يرمي عند الفشل)
```rin
token_require(t, "write");           // يرمي msg! إن لم يملكها
token_require_ok(t);                 // يرمي إن كان منتهياً أو ملغى
```

الخطأ الناتج `message` بمستوى error وكود مناسب (`E_TOKEN_EXPIRED` · `E_TOKEN_SCOPE` · `E_TOKEN_REVOKED` · `E_TOKEN_INVALID`).

---

## 4. الإلغاء والتجديد

```rin
token_revoke(t)                 // يعلّمه ملغى (محلياً + في السجل إن وُجد)
token_refresh(t, ttl?)          // يصدر توكن جديد بنفس subject/scopes مع عمر جديد
token_narrow(t, ["read"])       // توكن أضيق صلاحية (مشتق)
```

---

## 5. التكامل مع `message`

```rin
// رسالة محمولة على توكن (موقَّعة)
let m = message("info", "تم التحديث", {action: "save"}, null, t);
 // أو
let m = msg+ "تم" with t;

// التحقق عند الاستلام
if (message_token(m) && token_ok(message_token(m))) {
    // موثوق
}
```

- `message(..., token)` أو `with t` يضيف حقل `token` للرسالة.
- `message_token(m)` يستخرج التوكن المرفق.
- عند `emit m` يمكن للسجل أن يتحقق تلقائياً إن رُفع `message_require_token(true)`.

---

## 6. التكامل مع الحاويات والصلاحيات

```rin
@api.container
    need token;                         // تتطلب توكن صالح للدخول
    allow ["read", "profile"];          // الصلاحيات المقبولة
    deny  ["admin"];

    fun getProfile(t) {
        token_require(t, "profile");
        return {name: "رين", id: token_subject(t)};
    }
.end;
```

- `need token` يجعل استدعاء دوال الحاوية يتطلب توكن صالحاً كأول وسيط أو عبر سياق.
- يمكن تمرير التوكن صراحةً: `api.getProfile(t)`.
- الفشل يُنتج `message` خطأ واضح.

---

## 7. السجل والبث

```rin
emit token_event(t, "issued");
emit token_event(t, "revoked");
emit token_event(t, "expired");
emit token_event(t, "denied", {scope: "write"});

// في on message أو on token
on token (ev) {
    if (ev.action == "expired") {
        banner(msg? "انتهت صلاحية الجلسة");
    }
}
```

`token_event(t, action, meta?)` يُنشئ رسالة من نوع خاص `kind: "token-event"`.

---

## 8. التخزين البسيط (اختياري)

```rin
token_store(t)                  // يحفظ في مخزن الجلسة الحالي
token_load(id)                  // يسترجع أو nil
token_list()                    // كل التوكنات المخزّنة
token_clear()
token_purge_expired()
```

المخزن محلي لعملية التشغيل (أو مرتبط بالحاوية إن وُجدت). لا يدّعي أنه آمن ضد الهجمات المتقدمة — هو أداة تطبيقية واضحة.

---

## 9. التوقيع والتحقق الداخلي

- عند الإنشاء يُحسب `sig` من `id + subject + scopes + expires + issuer + secret`.
- الـ secret افتراضي من بيئة التشغيل أو `token_secret("...")`.
- `token_ok` يتحقق من التوقيع + الوقت + حالة الإلغاء.
- لا يوجد تشفير ثقيل — الهدف وضوح وسلامة داخلية للتطبيقات، لا بديل عن TLS/JWT خارجي إن لزم.

---

## 10. دوال مساعدة سريعة

| الدالة | الوظيفة |
|---|---|
| `is_token(v)` | هل القيمة توكن |
| `token(...)` | إنشاء |
| `token_ok` / `token_expired` / `token_revoked` | حالة |
| `token_has` / `token_has_all` / `token_has_any` | صلاحيات |
| `token_require` / `token_require_ok` | فحص صارم (يرمي) |
| `token_revoke` / `token_refresh` / `token_narrow` | إدارة |
| `token_event` | حدث قابل للبث |
| `as_message(t, level, body)` | حوّل حالة التوكن إلى رسالة |

---

## 11. أمثلة متكاملة

```rin
// إصدار جلسة
let session = token("user:42", ["read", "write", "profile"], {
    ttl: 7200,
    meta: {device: "mobile"},
    issuer: "auth"
});

// استخدام مع رسالة
emit msg+ "تسجيل دخول" with session;

// فحص صلاحية
try {
    token_require(session, "admin");   // سيفشل
} catch (e) {
    banner(e);                         // ❌ E_TOKEN_SCOPE
}

// تجديد
let fresh = token_refresh(session, 3600);

// تضييق
let readonly = token_narrow(session, ["read"]);

// مع حاوية
@files.container
    need token;
    allow ["read", "write"];
    fun save(t, name, data) {
        token_require(t, "write");
        // ...
    }
.end;
```

---

## 12. قواعد

- إضافة بحتة → لا كلمات محجوزة حقيقية (`let tok = 1;` صالح).
- التوكن قيمة عادية (خريطة) يمكن طباعتها وفحصها.
- التوقيع داخلي بسيط؛ للإنتاج الحساس يُفضّل دمج مكتبة خارجية.
- كل الأخطاء الناتجة عن `token_require*` هي قيم `message` متوافقة مع النظام السابق.
- الاختبارات: `tests/print/token.rin` و`tests/verification/token_integration.rin`.
