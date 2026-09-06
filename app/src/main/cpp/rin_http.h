// rin_http.h — عميل HTTP حقيقي وفعلي (اتصال شبكة حقيقي، وليس محاكاة) تستخدمه اللغة عبر
// natives: httpGet/httpPost/httpPut/httpPatch/httpDelete/httpRequest و apiRegister/apiCall
// (انظر تسجيلها في rin_interpreter.cpp، ضمن registerNatives()).
//
// لماذا التنفيذ مختلف حسب المنصة؟
//   * على أندرويد (__ANDROID__): NDK القياسي لا يشحن مكتبة TLS/HTTP (لا libcurl ولا OpenSSL)،
//     لكن جهاز أندرويد نفسه يملك عميل HTTPS كامل وحقيقي جاهزاً على مستوى JVM
//     (java.net.HttpURLConnection يستخدم فعلياً محرّك TLS النظام). فبدل إعادة اختراع/شحن مكتبة
//     TLS كاملة داخل native، نعبر حدود JNI عكسياً (native -> Kotlin) عبر androidHttpBridge
//     أدناه، الذي يُسجَّله jni_bridge.cpp عند تحميل المكتبة (JNI_OnLoad) ويستدعي فعلياً
//     RinHttpBridge.request(...) في Kotlin — طلب شبكة حقيقي 100%، بنفس ثقة/شهادات النظام.
//   * على سطح المكتب/CLI (لينكس/macOS/ويندوز، أدوات compiler/ و tools/): لا توجد JVM لنعبر
//     إليها، لذا نُنفِّذ الطلب فعلياً عبر تشغيل عملية فرعية حقيقية لأداة curl الموجودة أصلاً على
//     كل هذه المنصات (بما فيها ويندوز 10 1803+) — دون المرور عبر أي shell (fork/exec بمصفوفة
//     argv مباشرة على POSIX)، لتفادي أي ثغرة حقن أوامر عبر URL/جسم الطلب القادمين من كود المستخدم.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace rin {
namespace http {

struct HttpResult {
    bool ok = false;           // true فقط إن تم الاتصال فعلياً وحصلنا على رد (بغض النظر عن status)
    long status = 0;           // رمز حالة HTTP الفعلي (0 إن فشل الاتصال نفسه: DNS/timeout/refused...)
    std::string body;          // جسم الرد الخام كما وصل فعلياً من الخادوم
    std::string error;         // رسالة خطأ بشرية إن ok == false (أو نص تحذيري إن نجح الاتصال لكن status خطأ)
    std::vector<std::pair<std::string, std::string>> headers; // ترويسات الرد (إن توفرت)
};

using HeaderList = std::vector<std::pair<std::string, std::string>>;

// نقطة الحقن التي يستخدمها jni_bridge.cpp على أندرويد فقط لتسجيل الجسر الحقيقي إلى Kotlin.
// إبقاء هذا التوقيع (std::function) هنا يعني أن هذا الملف نفسه لا يعتمد على <jni.h> إطلاقاً —
// كل تفاصيل JNI تبقى محصورة داخل jni_bridge.cpp.
void setAndroidBridge(std::function<HttpResult(const std::string& method,
                                                const std::string& url,
                                                const HeaderList& headers,
                                                const std::string& body,
                                                int timeoutMs)> bridge);

// تنفيذ طلب HTTP حقيقي فعلي. [timeoutMs] <= 0 يعني استخدام المهلة الافتراضية (15 ثانية).
HttpResult performRequest(const std::string& method,
                           const std::string& url,
                           const HeaderList& headers,
                           const std::string& body,
                           int timeoutMs = 0);

// نقطة حقن ثانية مستقلة، خاصة بأندرويد فقط، لتنزيل ثنائي حقيقي (صور/أيقونات) — انظر
// jni_bridge.cpp لماذا هذه منفصلة عن setAndroidBridge أعلاه: الجسر العادي يمرّ بايتات الرد عبر
// jstring (NewStringUTF/GetStringUTFChars)، وهذا التمثيل نصّي (Modified UTF-8) يُفسد أي بايت غير
// صالح كنص UTF-8 — أي صورة PNG/JPEG حقيقية تقريباً. هذا الجسر البديل يمرّر جسم الرد كـ jbyteArray
// خام بلا أي تحويل نصي في الاتجاهين، فيبقى كل بايت كما وصل فعلياً من الخادوم.
void setAndroidBinaryGetBridge(std::function<HttpResult(const std::string& url, int timeoutMs)> bridge);

// تنزيل GET ثنائي حقيقي وآمن للبايتات: يستخدم setAndroidBinaryGetBridge أعلاه على أندرويد
// (تفادياً لإفساد UTF-8)، ويُعاد توجيهه إلى performRequest العادي على سطح المكتب/CLI (curl عبر
// pipe/subprocess حقيقي هناك أصلاً آمن للبايتات الثنائية، فلا حاجة لمسار منفصل).
HttpResult performBinaryGet(const std::string& url, int timeoutMs = 0);

} // namespace http
} // namespace rin
