# `message` — نظام الرسائل الموحّد

إضافة **بحتة** بالكامل. كل الصيغ الجديدة كانت خطأ بناء سابقاً.
تُفكَّك مبكراً في المحلّل إلى نداءات (`message` · `__message_make` · `__message_emit`)، فتعمل على المفسّر وRinVM دون أي تعديل فيهما.

`message` يصبح **القيمة الأساسية** التي تفهمها أنظمة Rin الأخرى:

| النظام | التكامل |
|---|---|
| `print` | طباعة جميلة بأيقونة ومستوى |
| `throw` / `try`/`catch` | رسالة ← خطأ غنيّ (و العكس) |
| `bannerInfo/Success/...` | تقبل `message` مباشرة |
| `dialog` | يمكن أن تكون رسالته قيمة `message` |
| الحاويات | `emit` / سجل الرسائل داخل الحاوية |
| الطرفية / المضيف | بث موحّد (Toast / Log / Snackbar) |

---

## 1. بناء الرسالة

### الدالة الأساسية
```rin
message(body)                       // level = "info"
message(level, body)
message(level, body, meta)
message(level, body, meta, code)    // code مثل "E0042" أو "AUTH"
```

القيمة الناتجة خريطة غنية ( orderexerved keys ):

```rin
{
  kind:    "message",          // علامة التعرّف
  level:   "info",             // debug | info | success | warning | error
  body:    "النص",
  meta:    {},                 // أي بيانات إضافية
  code:    nil,                // اختياري
  ts:      1732...,            // ميلي ثانية
  source:  nil,                // يُملأ تلقائياً عند throw أو emit من ملف
  tags:    []                  // اختياري
}
```

### الاختصارات السياقية (غير محجوزة)

| الصيغة | تعادل |
|---|---|
| `msg body` | `message("info", body)` |
| `msg! body` | `message("error", body)` |
| `msg? body` | `message("warning", body)` |
| `msg+ body` | `message("success", body)` |
| `msg# body` | `message("debug", body)` |
| `msg body {k:v}` | مع meta |
| `msg! body code "E1"` | مع code |
| `msg! body {..} code "E1"` | الاثنان معاً |

`msg` كلمة **سياقية فقط**: تُفعَّل حين تأتي في موضع تعبير وتتبعها قيمة.
`let msg = 1;` و`obj.msg` و`fun msg()` تبقى سليمة تماماً.

### قوالب بسيطة
```rin
message("مرحباً {name}، رصيدك {bal}", {name: "رين", bal: 42})
// body يصبح: "مرحباً رين، رصيدك 42"
```
يُستبدل `{key}` من الـ meta (أو من الوسيط الثالث إن وُجد).

---

## 2. المستويات والترتيب

```
debug < info < success < warning < error
```

- `message_level_rank(m)` → رقم (0..4)
- المقارنة: `m1.level > m2.level` أو عبر الدوال
- `message_min_level("warning")` يضبط الحد الأدنى للطباعة/البث (عالمي أو للحاوية)

---

## 3. التكامل مع الأخطاء (`try` / `throw`)

```rin
try {
    throw msg! "فشل التحميل" code "E210" {file: path};
} catch (e) {
    print e.code;          // E210
    print e.message;       // [E210] فشل التحميل
    print is_message(e);   // true  ← الخطأ نفسه يحمل kind:"message"
    print message_body(e);
}
```

- `throw message(...)` أو `throw msg! "..."` يحوّل تلقائياً إلى خطأ غنيّ مع الحفاظ على كل الحقول.
- أي خطأ ملتقَط يمكن معاملته كـ `message` عبر `is_message` / `as_message(e)`.
- `as_error(m)` يجهّزه للرمي إن لم يكن كذلك.

---

## 4. التكامل مع Banner

```rin
banner(msg+ "تم الحفظ");           // يستدعي bannerSuccess تلقائياً
banner(msg! "خطأ" code "E9");     // bannerError + يظهر الـ code
bannerInfo(m);                     // الدوال القديمة تقبل message أيضاً
```

المستوى يحدّد الدالة المناسبة. إن وُجد `code` يُلحق بالنص.

---

## 5. التكامل مع Dialog

```rin
@shop.container
    let stock = 10;
    dialog low = msg? "المخزون منخفض: {stock}" {stock: stock}
        title "تنبيه" buttons ["إعادة طلب", "تجاهل"]
        opens when stock < 5;
    reply low (r) { ... }
.end;
```

`dialog` يقبل قيمة `message` كرسالة: يأخذ `body` ويعرض `level` كنغمة إن لم تُحدد `tone`.

---

## 6. البث والسجل (`emit` + سجل الرسائل)

```rin
emit m;                            // يبث عالمياً + يُسجَّل
emit m to "ui";                    // قناة اختيارية
emit m to shop;                    // إلى حاوية

// داخل حاوية أو نطاق:
on message (m) {                   // يستقبل كل ما يُبث هنا
    if (message_level(m) == "error") { banner(m); }
}

// السجل
print messages();                  // آخر الرسائل (قابلة للتصفية)
print messages(level: "error");
print messages(since: t0);
clear_messages();
```

- السجل له حد افتراضي (مثلاً 200) قابل للضبط بـ `message_log_limit(n)`.
- `emit` يملأ `source` تلقائياً إن أمكن.

---

## 7. دوال مساعدة كاملة

| الدالة | الوظيفة |
|---|---|
| `is_message(v)` | هل هي رسالة (أو خطأ محمّل كرسالة) |
| `message_level(m)` · `message_body(m)` · `message_meta(m)` · `message_code(m)` · `message_ts(m)` | حقول |
| `message_rank(m)` | ترتيب المستوى رقمياً |
| `as_message(v)` | يحوّل نصاً أو خطأ إلى message |
| `as_error(m)` | يجهّز للـ throw |
| `banner(m)` | بث حسب المستوى |
| `emit(m [, to])` | بث + تسجيل |
| `messages([filters])` | قراءة السجل |
| `clear_messages()` | تفريغ |
| `message_min_level(l)` | حد أدنى للعرض/البث |

---

## 8. الطباعة الجميلة

```
ℹ️  [info] تم التحميل
✅  [success] حفظ بنجاح
⚠️  [warning] [W003] قيمة منخفضة   {stock: 3}
❌  [error] [E210] فشل الاتصال     {url: ...}
🔍  [debug] خطوة 4
```

- الأيقونة + المستوى + الـ code (إن وُجد) + الـ body + الـ meta المضغوط.
- يمكن تعطيل الألوان/الأيقونات بـ `message_style("plain")`.

---

## 9. قواعد مهمة

- كل شيء **إضافة بحتة** → لا يتغيّر سلوك أي كود قديم.
- `msg` وملحقاتها سياقية فقط.
- المستويات غير المعروفة تُعامل كـ `info` مع الحفاظ على النص الأصلي في الحقل.
- القوالب `{key}` تستبدل فقط المفاتيح الموجودة؛ الباقي يبقى كما هو.
- `emit` و`on message` اختياريان تماماً (خلف راية تُفعَّل عند أول استخدام).
- الاختبارات: `tests/print/message.rin` و`tests/verification/message_*.rin`.

---

## 10. أمثلة سريعة متكاملة

```rin
// بناء + رمي + التقاط
try {
    let path = "data.json";
    throw msg! "ملف غير موجود: {path}" {path: path} code "E404";
} catch (e) {
    banner(e);                     // ❌ bannerError
    print message_code(e);         // E404
}

// سجل + تصفية
emit msg+ "بدأ التشغيل";
emit msg? "تحذير بسيط";
emit msg! "خطأ حرج" code "E1";
print messages(level: "error");    // فقط الأخطاء

// مع dialog
dialog alert = msg! "انتهى الوقت"
    title "انتهى" buttons ["حسناً"]
    opens when timeout;
```
