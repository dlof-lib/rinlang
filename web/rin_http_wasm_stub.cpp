// web/rin_http_wasm_stub.cpp
// =============================================================================
// بديل (stub) لـ rin_http.cpp خاص ببناء WebAssembly فقط.
//
// التنفيذ الحقيقي في app/src/main/cpp/rin_http.cpp يشغّل عملية فرعية فعلية
// (fork/exec لـ curl) لإجراء اتصال شبكة حقيقي. هذا غير ممكن داخل بيئة WASM
// المعزولة في المتصفح (لا fork، لا exec، لا مقابس TCP خام).
//
// site.rin (صفحة اللغة الرسمية) لا يستخدم إطلاقاً أي دالة شبكة
// (httpGet/httpPost/apiCall...)، لذا هذا الملف موجود فقط لإرضاء الرابط
// (linker) عند بناء rin_wasm_bridge.cpp، وليس لأنه مطلوب فعلياً لتوليد الصفحة.
// إن استُدعيت هذه الدوال فعلاً من كود Rin يُنفَّذ داخل المتصفح، تُعاد رسالة
// خطأ واضحة بدل فشل صامت أو سلوك غير معرَّف.
// =============================================================================
#include "rin_http.h"

namespace rin {
namespace http {

void setAndroidBridge(std::function<HttpResult(const std::string&,
                                                const std::string&,
                                                const HeaderList&,
                                                const std::string&,
                                                int)> bridge) {
    (void)bridge; // غير مستخدم في بناء المتصفح
}

HttpResult performRequest(const std::string& method,
                           const std::string& url,
                           const HeaderList& headers,
                           const std::string& body,
                           int timeoutMs) {
    (void)method; (void)url; (void)headers; (void)body; (void)timeoutMs;
    HttpResult r;
    r.ok = false;
    r.status = 0;
    r.error = "طلبات الشبكة (http*) غير مدعومة في نسخة المتصفح (WebAssembly) من مفسّر Rin؛ "
              "استخدم المفسّر الأصلي (rin_run/native) لبرامج تحتاج اتصال شبكة حقيقي.";
    return r;
}

} // namespace http
} // namespace rin
