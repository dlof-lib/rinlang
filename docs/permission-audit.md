# تدقيق الصلاحيات (`audit`) — سجل فحص وتتبع الوصول

إضافة **بحتة**. يعمل فوق نظام التوكنات والحاويات دون تغيير سلوك أي كود لا يستخدمه.

الهدف: كل فحص صلاحية (نجاح أو رفض) يُسجَّل بشكل منظم ويمكن الاستعلام عنه لاحقاً، مع إمكانية ربطه بالرسائل والبث.

---

## 1. ماذا يُدقَّق؟

كل عملية من النوع التالي تُنتج **سجل تدقيق** تلقائياً عندما يكون التدقيق مفعّلاً:

| الحدث | متى يُسجَّل |
|---|---|
| `token_require` / `token_require_ok` | نجاح أو فشل |
| `token_has` (عند تفعيل التتبع التفصيلي) | اختياري |
| دخول حاوية فيها `need token` | عند الاستدعاء |
| `allow` / `deny` داخل حاوية | نتيجة الفحص |
| استدعاء `audit_check(...)` يدوياً | دائماً |
| `token_revoke` / `token_refresh` | كأحداث إدارية |

---

## 2. شكل سجل التدقيق

```rin
{
  kind:      "audit",
  id:        "aud_...",
  ts:        1732...,
  action:    "require" | "check" | "enter" | "revoke" | "refresh" | "deny" | "allow",
  result:    "allow" | "deny",
  subject:   "user:42",          // من التوكن
  scope:     "write",            // الصلاحية المطلوبة (أو قائمة)
  token_id:  "tok_...",
  container: "files",            // إن وُجد
  function:  "save",             // إن وُجد
  reason:    "missing scope",    // عند الرفض
  meta:      {},
  message:   <message>           // رسالة مرتبطة (اختياري)
}
```

---

## 3. التفعيل والضبط

```rin
audit_on();                      // تفعيل التسجيل
audit_off();                     // إيقاف
audit_enabled();                 // هل مفعّل؟

audit_level("minimal");          // require + enter فقط (افتراضي)
audit_level("detailed");         // + token_has + فحوصات إضافية
audit_level("full");             // كل شيء + meta موسّع

audit_limit(500);                // أقصى عدد سجلات في الذاكرة (حلقي)
```

التفعيل محلي لعملية التشغيل. يمكن ربطه بحاوية لاحقاً.

---

## 4. الفحص اليدوي والتسجيل

```rin
// فحص + تسجيل تلقائي
audit_check(t, "write");                 // ≡ token_has + سجل
audit_require(t, "write");               // ≡ token_require + سجل (يرمي عند الفشل)

// تسجيل يدوي حر
audit_log({
    action: "custom",
    result: "allow",
    subject: "user:1",
    scope: "export",
    meta: {file: "report.pdf"}
});
```

`audit_require` يرمي نفس أخطاء `token_require` مع إضافة السجل.

---

## 5. الاستعلام عن السجل

```rin
audit_entries()                          // كل السجلات (الأحدث أولاً)
audit_entries({result: "deny"})          // المرفوضة فقط
audit_entries({subject: "user:42"})
audit_entries({scope: "admin"})
audit_entries({container: "files"})
audit_entries({since: t0, until: t1})
audit_entries({limit: 20})

audit_last()                             // آخر سجل
audit_count({result: "deny"})            // عدد
audit_summary()                          // {total, allow, deny, by_scope, by_subject, ...}
```

---

## 6. التكامل مع `message` و`emit`

```rin
// كل رفض يُمكن أن يُبث تلقائياً
audit_emit_denies(true);                 // عند تفعيله: كل deny → emit message

// أو يدوياً
let denies = audit_entries({result: "deny", limit: 5});
each d in denies {
    emit as_message(d, "warning", "رفض صلاحية: " + d.scope);
}
```

`as_message(audit_entry)` يحوّل السجل إلى رسالة بمستوى مناسب (warning للرفض، info للسماح).

---

## 7. التكامل مع الحاويات

```rin
@files.container
    need token;
    allow ["read", "write"];
    audit true;                          // فعّل التدقيق لهذه الحاوية فقط

    fun save(t, name, data) {
        audit_require(t, "write");       // يُسجَّل تلقائياً مع اسم الحاوية والدالة
        // ...
    }
.end;
```

عند وجود `audit true` داخل الحاوية:
- كل `need token` و`allow`/`deny` و`audit_require` تُسجَّل مع `container` و`function`.

---

## 8. التقارير السريعة

```rin
print audit_summary();
// {
//   total: 42,
//   allow: 35,
//   deny: 7,
//   by_scope: {read: 20, write: 15, admin: 7},
//   by_subject: {"user:1": 10, "user:2": 32},
//   recent_denies: [...]
// }

audit_report()                           // نص مقروء للطباعة أو السجل
audit_clear()                            // تفريغ السجل
audit_purge({before: t0})                // حذف القديم
```

---

## 9. دوال مساعدة

| الدالة | الوظيفة |
|---|---|
| `audit_on` / `audit_off` / `audit_enabled` | التحكم |
| `audit_level` / `audit_limit` | الضبط |
| `audit_check` / `audit_require` | فحص + تسجيل |
| `audit_log` | تسجيل حر |
| `audit_entries` / `audit_last` / `audit_count` | استعلام |
| `audit_summary` / `audit_report` | تقارير |
| `audit_clear` / `audit_purge` | تنظيف |
| `audit_emit_denies` | بث تلقائي للرفض |
| `is_audit(v)` | هل القيمة سجل تدقيق |

---

## 10. أمثلة متكاملة

```rin
audit_on();
audit_level("detailed");

let session = token("user:42", ["read", "profile"], {ttl: 3600});

// نجاح
audit_require(session, "read");

// فشل يُسجَّل ويُرمى
try {
    audit_require(session, "admin");
} catch (e) {
    banner(e);
}

// تقرير
print audit_summary().deny;              // 1
print audit_entries({result: "deny"})[0].scope;  // admin

// بث الرفض
audit_emit_denies(true);
```

---

## 11. قواعد

- إضافة بحتة: بدون `audit_on()` لا يُسجَّل شيء ولا تكلفة.
- السجل حلقي محدود الحجم (`audit_limit`).
- كل سجل تدقيق يمكن تحويله إلى `message` عبر `as_message`.
- الأخطاء من `audit_require` متوافقة مع نظام `message` (نفس الأكواد `E_TOKEN_*`).
- لا يخزن كلمات مرور أو أسراراً — فقط معرفات وصلاحيات ونتائج.
- الاختبارات: `tests/print/audit.rin` و`tests/verification/audit_integration.rin`.
