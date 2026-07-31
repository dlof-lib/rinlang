// jni_bridge.cpp
// Exposes the Rin C++ engine to Kotlin through JNI.
// Kotlin side: RinEngine.kt declares the matching `external fun` signatures.
#include <jni.h>
#include <string>
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
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
