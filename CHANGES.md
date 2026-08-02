# ملفات مُعدَّلة / مُنشأة حتى الآن — نظام API حقيقي

هذه ليست المشروع كاملاً، فقط الملفات التي لمستها حتى الآن (المسارات كما هي داخل رينلانغ، انسخها
فوق نفس المسارات في نسختك من المشروع):

## ملفات جديدة (Real HTTP layer)
- `app/src/main/cpp/rin_http.h`   — تعريف عميل HTTP حقيقي (HttpResult + performRequest + نقطة حقن جسر أندرويد)
- `app/src/main/cpp/rin_http.cpp` — التنفيذ الفعلي: curl حقيقي عبر fork/exec (لينكس/macOS) أو _popen (ويندوز)، وجسر Android مُهيَّأ لاحقاً من jni_bridge.cpp
- `app/src/main/cpp/rin_json.h`   — محوّل Value <-> JSON حقيقي (encode/decode فعليين، وليس تمثيل عرض فقط)

## ملفات مُعدَّلة
- `app/src/main/cpp/rin_interpreter.h`  — أُضيف: `struct ApiEndpoint`، `apiEndpoints`، `defaultHttpTimeoutMs`، وتصريح `performRealApiCall`
- `app/src/main/cpp/rin_interpreter.cpp` — أُضيفت natives الجديدة:
  - `httpGet/httpPost/httpPut/httpPatch/httpDelete/httpRequest`
  - `apiRegister(name, baseUrl)` / `apiHeader(name, key, value)` — تسجيل API حقيقي خاص بالمبرمج (رابط + ترويسات/مفتاح API)
  - `apiGet/apiPost/apiPut/apiPatch/apiDelete/apiCall` — استدعاء الـAPI المسجَّل فعلياً عبر الشبكة
  - `jsonEncode/jsonDecode`, `httpSetTimeout`
  - كل النظام القديم (container.api / route / call / callApi المحاكى) بقي كما هو بلا أي تغيير — إضافة فقط.

## ⚠️ لم يكتمل بعد (الخطوة القادمة)
هذه الملفات وحدها **لن تُبنى/تعمل على أندرويد بعد** لأن الربط JNI <-> Kotlin لم يُنجز:
1. `app/src/main/cpp/jni_bridge.cpp` — يحتاج `JNI_OnLoad` يخزّن `JavaVM*` ويُسجِّل `rin::http::setAndroidBridge(...)` لينادي فعلياً `RinHttpBridge.request(...)` في Kotlin.
2. ملف Kotlin جديد `RinHttpBridge.kt` — تنفيذ الطلب الفعلي عبر `HttpURLConnection`.
3. `app/src/main/cpp/CMakeLists.txt` — إضافة `rin_http.cpp` إلى قائمة مصادر `rinengine`.
4. تحسينات preview loop/style (`LoomPreviewManager.kt`, `LoomFabricView.kt`) — لم تبدأ بعد.

سأكمل هذه في الرسالة/الحزمة القادمة.

---

## ✅ اكتمل (تحديث لاحق)

الأجزاء التي كانت "لم تكتمل بعد" أعلاه اكتملت جميعاً واختُبرت (lexer→parser→interpreter→rin_http
كاملاً عبر apiRegister/apiHeader/apiGet + jsonEncode/jsonDecode، ببناء g++ حقيقي):

1. `app/src/main/cpp/jni_bridge.cpp` — أُضيف `JNI_OnLoad` (يخزّن `JavaVM*`، يجلب صف/توقيع
   `RinHttpBridge.request` كـ global ref) + `callKotlinHttpBridge` (المُسجَّل عبر
   `rin::http::setAndroidBridge`)، مع Attach/Detach صحيح للترد الحالي.
2. `app/src/main/java/com/dlof/rinlang/RinHttpBridge.kt` (جديد) — تنفيذ فعلي عبر
   `HttpURLConnection`.
3. `app/src/main/cpp/CMakeLists.txt` — أُضيف `rin_http.cpp` إلى مصادر `rinengine`.
4. **إصلاح كان لازماً لتصحيح البناء**: `headersFromValue`/`bodyToString`/`httpResultToValue` في
   `rin_interpreter.cpp` كانت تُستخدَم قبل تعريفها (use-before-declaration) — أُضيفت تصريحات أمامية
   (forward declarations) أعلى الملف.
5. تحسينات preview loop/style:
   - `LoomPreviewManager.kt`: `Listener.onBusyChanged(Boolean)` و `onSlowOperation()` (كلاهما
     افتراضياً no-op) — مؤشر انتظار مُنقَّى (debounced 150ms) حول كل استدعاء أصلي (start/
     pushLiveEdit/tap)، مع تنبيه إضافي بعد 4 ثوانٍ (يعني عملياً استدعاء شبكة حقيقياً عبر apiCall/
     httpGet وليس حساباً محلياً).
   - `LoomFabricView.kt`: شارة (badge) صغيرة + قوس دوّار في الزاوية العلوية اليمنى من الشاشة
     (مثبتة بمعزل عن zoom)، تتغيّر نصّها/لونها إلى "بانتظار رد شبكة حقيقي…" بعد تجاوز 4 ثوانٍ.
   - `LoomPreviewActivity.kt`: يربط الاثنين أعلاه بـ `fabricView.isBusy` /
     `fabricView.isWaitingOnNetwork`.
   - `res/values{,​-en,-ar}/strings.xml`: نصوص `loom_busy_hint` و `loom_busy_network_hint`
     (لم تُضَف لـ values-es لأنها أصلاً لا تحتوي مفاتيح loom_ الأخرى — تتساقط تلقائياً إلى
     values الافتراضية).
