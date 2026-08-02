// jni_bridge.cpp
// Exposes the Rin C++ engine to Kotlin through JNI.
// Kotlin side: RinEngine.kt declares the matching `external fun` signatures.
#include <jni.h>
#include <string>
#include <vector>
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "rin_http.h"
#include "loom/rin_loom_c_api.h"

// runSourceNative(source, baseDir) -> baseDir هو جذر حقيقي على القرص (عادة filesDir الخاص بالتطبيق
// على أندرويد) تُبنى فوقه كل عمليات save/file/installation/writeFile/readFile الحقيقية. RinEngine.kt
// يمرّر هذا الباراميتر تلقائياً (فارغ إن لم يُستدعَ RinEngine.init(context) بعد)، لذا لا حاجة لتغيير
// أي كود Kotlin قديم يستدعي RinEngine.runSource(source) بباراميتر واحد.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring baseDirJStr) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);

    std::string baseDir;
    if (baseDirJStr != nullptr) {
        const char* cBaseDir = env->GetStringUTFChars(baseDirJStr, nullptr);
        baseDir = cBaseDir ? cBaseDir : "";
        env->ReleaseStringUTFChars(baseDirJStr, cBaseDir);
    }

    std::string result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interpreter;
        if (!baseDir.empty()) {
            interpreter.setBasePath(baseDir);
        }
        result = interpreter.run(statements);
        if (result.empty()) {
            result = "(no output)";
        }
    } catch (rin::RinError& e) {
        result = "[Syntax error, line " + std::to_string(e.line) + "]: " + e.message;
    } catch (std::exception& e) {
        result = std::string("[Internal error]: ") + e.what();
    } catch (...) {
        result = "[Unknown internal error]";
    }

    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_engineVersion(JNIEnv* env, jobject /* this */) {
    return env->NewStringUTF("Rin Engine 1.1 (C++17) — save/file/installation حقيقية على القرص");
}

// renderViewNative(source, rootWidth) -> Loomtime: يحلّل @view.<Kind>=name، يبني الـ Fabric،
// يُخطِّطه (Loom) عند العرض rootWidth (بالبكسل)، ويُعيد تفريغ JSON كامل (kind/name/سطر المصدر/
// هندسة/سمات مُحلَّلة، تكرارياً) يستهلكه جانب Kotlin/Canvas لرسم الواجهة فعلياً. عند فشل التحليل
// يُعاد JSON بالشكل {"error": "...", "line": N} بدل رمي استثناء عبر حدود JNI.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_renderViewNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jint rootWidth) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);

    char* json = rin_loom_render_json(source.c_str(), (int)rootWidth);
    jstring result = env->NewStringUTF(json ? json : "{\"error\":\"null result\",\"line\":0}");
    rin_free_string(json);
    return result;
}

// ---- Loomtime session (Needle): a persistent Fabric+Warp session so a live-preview tap can
// actually run its onTap handler (real fun/while loop or a built-in Warp op) and see the result,
// instead of renderViewNative's stateless one-shot render. The native session pointer is boxed as
// a jlong handle on the Kotlin side (see RinEngine.kt's LoomSession wrapper) -- standard JNI
// pattern for opaque native resources that must outlive a single call.

extern "C" JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionCreateNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jint rootWidth) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);
    void* session = rin_loom_session_create(source.c_str(), (int)rootWidth);
    return reinterpret_cast<jlong>(session);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionRenderJsonNative(JNIEnv* env, jobject /* this */, jlong handle) {
    char* json = rin_loom_session_render_json(reinterpret_cast<void*>(handle));
    jstring result = env->NewStringUTF(json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionTapNative(JNIEnv* env, jobject /* this */, jlong handle, jdouble x, jdouble y) {
    char* json = rin_loom_session_tap(reinterpret_cast<void*>(handle), (double)x, (double)y);
    jstring result = env->NewStringUTF(json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionUpdateSourceNative(JNIEnv* env, jobject /* this */, jlong handle, jstring newSourceJStr) {
    const char* cSource = env->GetStringUTFChars(newSourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(newSourceJStr, cSource);
    char* json = rin_loom_session_update_source(reinterpret_cast<void*>(handle), source.c_str());
    jstring result = env->NewStringUTF(json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionFreeNative(JNIEnv* /* env */, jobject /* this */, jlong handle) {
    rin_loom_session_free(reinterpret_cast<void*>(handle));
}

// ================= جسر HTTP الحقيقي: JNI_OnLoad + native -> Kotlin (RinHttpBridge) =================
// لماذا هنا تحديداً وليس داخل rin_http.cpp؟ rin_http.h/.cpp مصمَّمان عمداً بلا أي اعتماد على
// <jni.h> (انظر تعليق rin_http.h) حتى يبقيا قابلين للبناء كأداة سطر أوامر عادية بلا NDK. كل ما
// يخص JNI فعلياً — بما فيه تخزين JavaVM* واستدعاء RinHttpBridge.request(...) في Kotlin —
// محصور بالكامل هنا، ويُسجَّل لمرة واحدة عند تحميل المكتبة عبر JNI_OnLoad (يُستدعى تلقائياً من
// نظام أندرويد فور System.loadLibrary("rinengine") في RinEngine.kt، قبل أي كود Rin يعمل).

namespace {

JavaVM* g_javaVm = nullptr;
jclass g_httpBridgeClass = nullptr;          // global ref لصف Kotlin com.dlof.rinlang.RinHttpBridge
jmethodID g_httpBridgeRequestMethod = nullptr; // MethodID لـ RinHttpBridge.request(...) الثابتة (static)

// FindClass لا يعمل بأمان إلا من الترد (thread) الذي استُدعي منه System.loadLibrary أصلاً (أي هنا
// داخل JNI_OnLoad نفسه) لأنه وقتها فقط يملك سياق مُحمِّل الأصناف (ClassLoader) الخاص بالتطبيق؛ أي
// استدعاء FindClass لاحقاً من ترد خلفي (worker thread) مُرفَق عبر AttachCurrentThread سيفشل غالباً
// لأنه يستخدم مُحمِّل الأصناف الجذري (bootstrap classloader) الذي لا يعرف أصناف التطبيق. لذا نجلب
// الصف والتوقيع مرة واحدة هنا ونُبقيهما كـ global ref صالحين من أي ترد لاحقاً.
bool ensureHttpBridgeAttached(JNIEnv* env) {
    jclass local = env->FindClass("com/dlof/rinlang/RinHttpBridge");
    if (local == nullptr) {
        env->ExceptionClear();
        return false;
    }
    g_httpBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (g_httpBridgeClass == nullptr) return false;

    g_httpBridgeRequestMethod = env->GetStaticMethodID(
        g_httpBridgeClass, "request",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;");
    if (g_httpBridgeRequestMethod == nullptr) {
        env->ExceptionClear();
        env->DeleteGlobalRef(g_httpBridgeClass);
        g_httpBridgeClass = nullptr;
        return false;
    }
    return true;
}

// يُعيد JNIEnv* صالحاً للترد الحالي، مُرفِقاً هذا الترد بـ JavaVM أولاً إن لم يكن مُرفَقاً بعد
// (طلبات httpGet/apiCall... قد تُنفَّذ من ترد خلفي مثل worker الخاص بـ LoomPreviewManager أو
// RinJobScheduler، وليس بالضرورة الترد الذي استدعى JNI_OnLoad). [didAttach] يُعاد true إن قمنا نحن
// بالإرفاق، حتى يُفصَل (Detach) الترد بعد الاستدعاء ولا يبقى مُرفَقاً بلا داعٍ.
JNIEnv* attachEnv(bool* didAttach) {
    *didAttach = false;
    if (g_javaVm == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    jint status = g_javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) return env;
    if (status == JNI_EDETACHED) {
        if (g_javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        *didAttach = true;
        return env;
    }
    return nullptr; // JNI_EVERSION أو خطأ آخر غير قابل للتعافي
}

std::string jstringToStd(JNIEnv* env, jstring s) {
    if (s == nullptr) return std::string();
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars ? chars : "");
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return out;
}

jobjectArray buildStringArray(JNIEnv* env, const std::vector<std::string>& items) {
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(items.size()), stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    for (size_t i = 0; i < items.size(); i++) {
        jstring js = env->NewStringUTF(items[i].c_str());
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), js);
        env->DeleteLocalRef(js);
    }
    return arr;
}

// التنفيذ الفعلي المُسجَّل عبر rin::http::setAndroidBridge (انظر rin_http.h): يبني وسائط JNI،
// يستدعي RinHttpBridge.request(...) الحقيقية في Kotlin (java.net.HttpURLConnection حقيقي)،
// ويحوّل ناتجها (String[4]: ok/status/body/error) إلى rin::http::HttpResult.
rin::http::HttpResult callKotlinHttpBridge(const std::string& method, const std::string& url,
                                            const rin::http::HeaderList& headers, const std::string& body,
                                            int timeoutMs) {
    rin::http::HttpResult result;

    if (g_httpBridgeClass == nullptr || g_httpBridgeRequestMethod == nullptr) {
        result.ok = false;
        result.error = "جسر HTTP الخاص بأندرويد غير مُهيَّأ (تعذّر إيجاد RinHttpBridge.kt عند تحميل المكتبة)";
        return result;
    }

    bool didAttach = false;
    JNIEnv* env = attachEnv(&didAttach);
    if (env == nullptr) {
        result.ok = false;
        result.error = "تعذّر الوصول إلى JNIEnv لتنفيذ طلب HTTP حقيقي من هذا الترد";
        return result;
    }

    std::vector<std::string> keys;
    std::vector<std::string> values;
    keys.reserve(headers.size());
    values.reserve(headers.size());
    for (auto& h : headers) { keys.push_back(h.first); values.push_back(h.second); }

    jstring jMethod = env->NewStringUTF(method.c_str());
    jstring jUrl = env->NewStringUTF(url.c_str());
    jobjectArray jKeys = buildStringArray(env, keys);
    jobjectArray jValues = buildStringArray(env, values);
    jstring jBody = env->NewStringUTF(body.c_str());

    auto jResult = static_cast<jobjectArray>(env->CallStaticObjectMethod(
        g_httpBridgeClass, g_httpBridgeRequestMethod, jMethod, jUrl, jKeys, jValues, jBody, (jint)timeoutMs));

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        result.ok = false;
        result.error = "استثناء غير متوقَّع في RinHttpBridge.request (انظر logcat)";
    } else if (jResult == nullptr || env->GetArrayLength(jResult) < 4) {
        result.ok = false;
        result.error = "رد غير صالح من RinHttpBridge.request";
    } else {
        auto jOk = (jstring)env->GetObjectArrayElement(jResult, 0);
        auto jStatus = (jstring)env->GetObjectArrayElement(jResult, 1);
        auto jRespBody = (jstring)env->GetObjectArrayElement(jResult, 2);
        auto jError = (jstring)env->GetObjectArrayElement(jResult, 3);

        std::string okStr = jstringToStd(env, jOk);
        std::string statusStr = jstringToStd(env, jStatus);
        result.body = jstringToStd(env, jRespBody);
        result.error = jstringToStd(env, jError);
        result.ok = (okStr == "1");
        try { result.status = std::stol(statusStr); } catch (...) { result.status = 0; }

        env->DeleteLocalRef(jOk);
        env->DeleteLocalRef(jStatus);
        env->DeleteLocalRef(jRespBody);
        env->DeleteLocalRef(jError);
    }

    if (jResult) env->DeleteLocalRef(jResult);
    env->DeleteLocalRef(jMethod);
    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(jKeys);
    env->DeleteLocalRef(jValues);
    env->DeleteLocalRef(jBody);

    if (didAttach) g_javaVm->DetachCurrentThread();
    return result;
}

} // namespace (anonymous)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    g_javaVm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_VERSION_1_6; // نادر جداً؛ لن يُسجَّل جسر HTTP لكن باقي المحرّك يعمل طبيعياً
    }
    if (ensureHttpBridgeAttached(env)) {
        rin::http::setAndroidBridge(callKotlinHttpBridge);
    }
    // else: RinHttpBridge.kt غير موجود بعد في هذه الحزمة/هذا البناء — httpGet/apiCall... ستُعيد
    // خطأً واضحاً بدل الانهيار (انظر رسالة "جسر HTTP ... غير مُهيَّأ بعد" في rin_http.cpp).
    return JNI_VERSION_1_6;
}
