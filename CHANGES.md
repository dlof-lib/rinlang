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
