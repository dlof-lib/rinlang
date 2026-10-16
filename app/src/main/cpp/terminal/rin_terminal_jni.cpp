// ============================================================================
//  جسر JNI لطرفية Linux (rin terminal) داخل محرّر RinStudio
// ============================================================================
//  يُضمِّن نفس Session المستعملة في `rin terminal` (cli/linux/src/terminal/rin_terminal.cpp، مبنيّة هنا بالعلم
//  RIN_TERMINAL_EMBED) خلف EmbeddedTerminal: مفسّر دائم الحالة + أوامر `:` (:load :vars :type :time :ocr :img …)
//  بلا tty. تستعملها RinNativeTerminal.kt. الخيط: نداء feed واحد في كل مرة لكل جلسة (يضمنه Kotlin بخيط
//  واحد)؛ المخرجات وطلبات input() تُسلَّم متزامنة على نفس الخيط (نفس عقد runSourceInteractiveNative:
//  لا AttachCurrentThread ولا GlobalRef).
#define RIN_JNI_UTF_WITH_JNI   // يفعّل toStd/newJString (UTF-8 قياسي) في rin_jni_utf.h كما في jni_bridge.cpp
#include <jni.h>

#include <exception>
#include <memory>
#include <string>

#include "../rin_jni_utf.h"
#include "rin_terminal.h"   // مسار cli/linux/src/terminal يُضاف في CMakeLists.txt

namespace {

// UTF-8 القياسي (لا Modified UTF-8): نفس مسار بقية جسور المشروع، فلا يفسد الإيموجي ولا U+0000.
std::string jstr(JNIEnv* env, jstring s) {
    if (!s) return std::string();
    return rin_jni::toStd(env, s);
}

rin::terminal::EmbeddedTerminal* term(jlong h) { return reinterpret_cast<rin::terminal::EmbeddedTerminal*>(h); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinNativeTerminal_nativeCreate(JNIEnv* env, jobject, jstring cwd, jboolean english) {
    try {
        rin::terminal::EmbedConfig cfg;
        cfg.cwd = jstr(env, cwd);
        cfg.english = english == JNI_TRUE;
        return reinterpret_cast<jlong>(new rin::terminal::EmbeddedTerminal(cfg));
    } catch (...) {
        return 0;
    }
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeTerminal_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete term(handle);
}

// 0 = نجح · 1 = فشل · 2 = المدخل ناقص (أرسل السطر التالي) · 3 = خروج (exit/quit) · -1 = خطأ داخلي
JNIEXPORT jint JNICALL
Java_com_dlof_rinlang_RinNativeTerminal_nativeFeed(JNIEnv* env, jobject, jlong handle, jstring line,
                                                   jobject listener, jobject inputHandler) {
    auto* t = term(handle);
    if (!t) return -1;

    jmethodID onChunk = nullptr;
    if (listener) {
        jclass c = env->GetObjectClass(listener);
        onChunk = env->GetMethodID(c, "onChunk", "(ILjava/lang/String;)V");
        env->DeleteLocalRef(c);
        if (!onChunk) env->ExceptionClear();
    }
    jmethodID onInput = nullptr;
    if (inputHandler) {
        jclass c = env->GetObjectClass(inputHandler);
        onInput = env->GetMethodID(c, "onInput", "(Ljava/lang/String;)Ljava/lang/String;");
        env->DeleteLocalRef(c);
        if (!onInput) env->ExceptionClear();
    }

    int seq = 0;
    rin::terminal::EmbeddedTerminal::OutputFn out;
    if (onChunk) {
        out = [&](const std::string& s) {
            jstring js = rin_jni::newJString(env, s);
            env->CallVoidMethod(listener, onChunk, static_cast<jint>(++seq), js);
            env->DeleteLocalRef(js);
            if (env->ExceptionCheck()) env->ExceptionClear();
        };
    }
    rin::terminal::EmbeddedTerminal::InputFn in;
    if (onInput) {
        in = [&](const std::string& prompt, std::string& answer) -> bool {
            jstring jp = rin_jni::newJString(env, prompt);
            jobject res = env->CallObjectMethod(inputHandler, onInput, jp);
            env->DeleteLocalRef(jp);
            if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
            if (!res) return false;   // null = ألغى المستخدم
            answer = jstr(env, static_cast<jstring>(res));
            env->DeleteLocalRef(res);
            return true;
        };
    }

    try {
        switch (t->feed(jstr(env, line), out, in)) {
            case rin::terminal::EmbeddedTerminal::Result::Ok: return 0;
            case rin::terminal::EmbeddedTerminal::Result::Failed: return 1;
            case rin::terminal::EmbeddedTerminal::Result::NeedMore: return 2;
            case rin::terminal::EmbeddedTerminal::Result::Quit: return 3;
        }
    } catch (const std::exception&) {
        return -1;
    } catch (...) {
        return -1;
    }
    return -1;
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeTerminal_nativeCancelPending(JNIEnv*, jobject, jlong handle) {
    if (auto* t = term(handle)) t->cancelPending();
}

JNIEXPORT jboolean JNICALL
Java_com_dlof_rinlang_RinNativeTerminal_nativeContinuing(JNIEnv*, jobject, jlong handle) {
    auto* t = term(handle);
    return (t && t->continuing()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinNativeTerminal_nativeVariables(JNIEnv* env, jobject, jlong handle) {
    std::string joined;
    if (auto* t = term(handle)) {
        for (const auto& n : t->variableNames()) { if (!joined.empty()) joined += '\n'; joined += n; }
    }
    return rin_jni::newJString(env, joined);
}

}  // extern "C"
