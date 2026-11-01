// web/rin_http_wasm_stub.cpp
// =============================================================================
// تنفيذ حقيقي وفعلي لـ rin_http.h خاص ببناء WebAssembly. رغم اسم الملف (أُبقي
// عليه بلا تغيير لتفادي تعديل قائمة مصادر em++ في .github/workflows/pages.yml)،
// هذا ليس "محاكاة" — الدالتان أدناه تُجريان اتصال شبكة حقيقي عبر fetch() الفعلية
// في المتصفح نفسه، بنفس فلسفة عميل أندرويد (native -> Kotlin -> HttpURLConnection
// حقيقي عبر jni_bridge.cpp): هنا الجسر هو native (C++/WASM) -> JavaScript -> fetch()
// حقيقي في نفس صفحة المتصفح التي يعمل بداخلها WASM.
//
// لماذا EM_ASYNC_JS (Asyncify) تحديداً؟
//   fetch() في JS دائماً غير متزامنة (تُعيد Promise)، بينما rin::http::performRequest
//   هنا دالة C++ عادية "تحظر" (blocking) حتى تعود بنتيجة — تماماً كنظيرتها في
//   rin_http.cpp (subprocess/curl حقيقي يحظر حتى ينتهي). Asyncify (مفعّلة عبر
//   -s ASYNCIFY=1 في em++، انظر .github/workflows/pages.yml) تسمح لكود JS مضمَّن
//   بعبارة `await` حقيقية أن "يُجمّد" تنفيذ WASM فعلياً حتى يُحلّ الـ Promise ثم
//   يستأنفه من نفس النقطة — فيبقى النموذج البرمجي لبقية اللغة (natives httpGet/
//   httpPost/apiCall/fetchImage/fetchIcon في rin_interpreter.cpp) موحّداً تماماً بين
//   كل المنصات (CLI/أندرويد/متصفح): استدعاء يحظر ثم يُعيد HttpResult حقيقياً.
//
// آلية تمرير البيانات بين C++ وJS: بما أن EM_ASYNC_JS تُعيد قيمة عددية واحدة فقط
// (int/double)، تُمرَّر مؤشرات مخارج (out-parameters: int*) يكتب فيها كود JS
// مباشرة عبر HEAP32/HEAPU8 (نفس ذاكرة WASM الخطية). أي بيانات ثنائية متغيرة الطول
// (جسم الرد/رسالة الخطأ/الترويسات المُجمَّعة) تُخصَّص فعلياً عبر _malloc() من داخل
// JS ثم تُقرَأ وتُحرَّر (free()) من جانب C++ — تماماً بنفس اتفاقية التخصيص التي
// تستخدمها بقية اللغة، بلا أي تسريب ذاكرة.
// =============================================================================
#include "rin_http.h"
#include <emscripten.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace rin {
namespace http {

void setAndroidBridge(std::function<HttpResult(const std::string&,
                                                const std::string&,
                                                const HeaderList&,
                                                const std::string&,
                                                int)> bridge) {
    (void)bridge; // نقطة حقن خاصة بأندرويد فقط — لا معنى لها هنا؛ الجسر الحقيقي أدناه ثابت
}

void setAndroidBinaryGetBridge(std::function<HttpResult(const std::string&, int)> bridge) {
    (void)bridge; // نفس الشيء: خاصة بأندرويد فقط
}

extern "C" {

// يُجري fetch() حقيقية في المتصفح وينتظرها فعلياً (Asyncify) حتى يكتمل الرد بالكامل
// (بما فيه جسم الرد الخام كـ ArrayBuffer، للحفاظ على البايتات الثنائية سليمة —
// صور PNG/JPEG وغيرها — تماماً كما يفعل مسار requestBinaryGet المنفصل على أندرويد).
// كل مخارج out* اختيارية الطول 0 إن لم تُكتب (مثال: لا جسم/لا خطأ).
EM_ASYNC_JS(int, rin_web_http_fetch, (
    const char* methodC, const char* urlC, const char* headersJoinedC,
    const char* bodyPtr, int bodyLen, int timeoutMs,
    int* outOk, int* outStatus,
    int* outErrorPtr, int* outErrorLen,
    int* outHeadersPtr, int* outHeadersLen,
    int* outBodyPtr, int* outBodyLen
), {
    return (async () => {
        function writeBytes(u8, outPtrAddr, outLenAddr) {
            var p = 0;
            if (u8.length > 0) {
                p = _malloc(u8.length);
                HEAPU8.set(u8, p);
            }
            HEAP32[outPtrAddr >> 2] = p;
            HEAP32[outLenAddr >> 2] = u8.length;
        }
        function writeStr(str, outPtrAddr, outLenAddr) {
            writeBytes(new TextEncoder().encode(str || ""), outPtrAddr, outLenAddr);
        }

        var methodStr = UTF8ToString(methodC);
        var urlStr = UTF8ToString(urlC);
        var headersStr = headersJoinedC ? UTF8ToString(headersJoinedC) : "";
        var reqHeaders = {};
        if (headersStr.length > 0) {
            headersStr.split("\n").forEach(function (line) {
                var idx = line.indexOf(": ");
                if (idx > 0) reqHeaders[line.substring(0, idx)] = line.substring(idx + 2);
            });
        }
        var reqBody = undefined;
        if (bodyLen > 0) {
            // نسخة حقيقية من بايتات الجسم من ذاكرة WASM (وليس مرجعاً حياً قد يتغيّر
            // إن نمت الذاكرة أثناء await لاحقاً — ALLOW_MEMORY_GROWTH=1 مفعّلة).
            reqBody = HEAPU8.slice(bodyPtr, bodyPtr + bodyLen);
        }

        var controller = new AbortController();
        var timer = null;
        if (timeoutMs > 0) {
            timer = setTimeout(function () { controller.abort(); }, timeoutMs);
        }

        try {
            var resp = await fetch(urlStr, {
                method: methodStr,
                headers: reqHeaders,
                body: reqBody,
                signal: controller.signal,
            });
            if (timer) clearTimeout(timer);

            HEAP32[outOk >> 2] = 1;
            HEAP32[outStatus >> 2] = resp.status;

            var hdrLines = [];
            resp.headers.forEach(function (v, k) { hdrLines.push(k + ": " + v); });
            writeStr(hdrLines.join("\n"), outHeadersPtr, outHeadersLen);

            var buf = await resp.arrayBuffer();
            writeBytes(new Uint8Array(buf), outBodyPtr, outBodyLen);
            writeStr("", outErrorPtr, outErrorLen);
        } catch (e) {
            if (timer) clearTimeout(timer);
            HEAP32[outOk >> 2] = 0;
            HEAP32[outStatus >> 2] = 0;
            writeStr("", outHeadersPtr, outHeadersLen);
            writeStr("", outBodyPtr, outBodyLen);
            var msg = (e && e.name === "AbortError")
                ? "انتهت مهلة الاتصال (timeout)"
                : String(e && e.message ? e.message : e);
            writeStr(msg, outErrorPtr, outErrorLen);
        }
        return 0;
    })();
});

} // extern "C"

// يقرأ بايتات من ذاكرة WASM كتبتها rin_web_http_fetch عبر _malloc، ينسخها إلى
// std::string حقيقية، ثم يُحرِّر الذاكرة الأصلية (free() — نفس مخصِّص _malloc في
// نفس الوحدة، بلا أي تسريب).
static std::string takeOwnedBytes(int ptr, int len) {
    std::string s;
    if (ptr && len > 0) {
        s.assign(reinterpret_cast<const char*>(static_cast<intptr_t>(ptr)), static_cast<size_t>(len));
    }
    if (ptr) {
        std::free(reinterpret_cast<void*>(static_cast<intptr_t>(ptr)));
    }
    return s;
}

HttpResult performRequest(const std::string& method,
                           const std::string& url,
                           const HeaderList& headers,
                           const std::string& body,
                           int timeoutMs) {
    std::string headersJoined;
    for (auto& h : headers) {
        headersJoined += h.first;
        headersJoined += ": ";
        headersJoined += h.second;
        headersJoined += "\n";
    }

    int ok = 0, status = 0;
    int errPtr = 0, errLen = 0;
    int hdrPtr = 0, hdrLen = 0;
    int bodyOutPtr = 0, bodyOutLen = 0;

    rin_web_http_fetch(method.c_str(), url.c_str(),
                        headersJoined.empty() ? nullptr : headersJoined.c_str(),
                        body.empty() ? nullptr : body.data(), static_cast<int>(body.size()),
                        timeoutMs,
                        &ok, &status,
                        &errPtr, &errLen,
                        &hdrPtr, &hdrLen,
                        &bodyOutPtr, &bodyOutLen);

    HttpResult r;
    r.ok = (ok != 0);
    r.status = status;
    r.error = takeOwnedBytes(errPtr, errLen);
    r.body = takeOwnedBytes(bodyOutPtr, bodyOutLen);

    std::string hdrStr = takeOwnedBytes(hdrPtr, hdrLen);
    size_t start = 0;
    while (start < hdrStr.size()) {
        size_t nl = hdrStr.find('\n', start);
        if (nl == std::string::npos) nl = hdrStr.size();
        std::string line = hdrStr.substr(start, nl - start);
        size_t colon = line.find(": ");
        if (colon != std::string::npos) {
            r.headers.emplace_back(line.substr(0, colon), line.substr(colon + 2));
        }
        start = nl + 1;
    }

    if (!r.ok && r.error.empty()) {
        r.error = "فشل اتصال fetch() في المتصفح لسبب غير معروف";
    }
    return r;
}

// نفس فلسفة rin_http.cpp الأصلي بالضبط (انظر التعليق أعلى الإعلان في rin_http.h):
// تنزيل GET ثنائي حقيقي وآمن للبايتات لا يحتاج مساراً منفصلاً هنا لأن fetch() + ArrayBuffer
// في rin_web_http_fetch أعلاه يحافظان على البايتات الخام سليمة بالفعل (بخلاف مسار jstring
// النصي على أندرويد) — فيُعاد التوجيه مباشرة إلى performRequest العادي، كما يفعل CLI/سطح المكتب.
HttpResult performBinaryGet(const std::string& url, int timeoutMs) {
    return performRequest("GET", url, {}, "", timeoutMs);
}

} // namespace http
} // namespace rin
