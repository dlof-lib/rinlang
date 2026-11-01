// rin_jni_utf.h
//
// تحويل آمن بين jstring (UTF-16) ونص C++ (UTF-8 القياسي) لكل جسور JNI في المشروع.
//
// لماذا؟ دوال JNI GetStringUTFChars/NewStringUTF تستخدم "Modified UTF-8" لا UTF-8 القياسي:
//  - المحارف خارج BMP (الإيموجي مثلاً) تُرمَّز كزوج بديل بـ 6 بايت (CESU-8) بدل 4 بايت، بينما
//    Kotlin يحسب إزاحات الأعمدة بـ toByteArray(UTF_8) (4 بايت) => إزاحات التلوين/المؤشر تنحرف بعد
//    أي إيموجي في السطر.
//  - NewStringUTF على UTF-8 قياسي فيه 4 بايت أو بايتات غير صالحة يُسقط CheckJNI (abort) أو يُنتج نصاً تالفاً.
//  - U+0000 يُرمَّز C0 80 في MUTF-8 فيقطع النص أو يفسده.
// هنا: تحويل UTF-16 <-> UTF-8 القياسي بنفسنا، وأي بايتات غير صالحة تُستبدَل بـ U+FFFD بدل الانهيار.
//
// القسم الأول (rin_jni::utf8 ...) بلا أي اعتماد على jni.h ليُختبَر على الجهاز (tools/test_jni_utf.cpp).
#pragma once

#include <cstdint>
#include <string>

namespace rin_jni {
namespace utf {

// UTF-8 -> UTF-16 (code units). بايتات غير صالحة/مقطوعة/overlong/surrogates مرمَّزة => U+FFFD.
inline std::u16string toUtf16(const std::string& in) {
    std::u16string out;
    out.reserve(in.size());
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        const size_t start = i;
        const unsigned char c = static_cast<unsigned char>(in[i]);
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < n && (static_cast<unsigned char>(in[i + 1]) & 0xC0) == 0x80) {
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3Fu);
                len = 2;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < n && (static_cast<unsigned char>(in[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(in[i + 2]) & 0xC0) == 0x80) {
                uint32_t v = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3Fu) << 6) |
                             (static_cast<unsigned char>(in[i + 2]) & 0x3Fu);
                if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) { cp = v; len = 3; }
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < n && (static_cast<unsigned char>(in[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(in[i + 2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(in[i + 3]) & 0xC0) == 0x80) {
                uint32_t v = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3Fu) << 12) |
                             ((static_cast<unsigned char>(in[i + 2]) & 0x3Fu) << 6) |
                             (static_cast<unsigned char>(in[i + 3]) & 0x3Fu);
                if (v >= 0x10000 && v <= 0x10FFFF) { cp = v; len = 4; }
            }
        }
        i = start + len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

// UTF-16 -> UTF-8 القياسي. surrogate يتيم => U+FFFD (EF BF BD).
inline std::string toUtf8(const char16_t* s, size_t len) {
    std::string out;
    out.reserve(len + len / 2);
    auto put = [&](uint32_t cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    for (size_t i = 0; i < len; ++i) {
        uint32_t u = s[i];
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
                put(0x10000 + ((u - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00));
                ++i;
            } else {
                put(0xFFFD);
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            put(0xFFFD);
        } else {
            put(u);
        }
    }
    return out;
}

} // namespace utf
} // namespace rin_jni

#ifdef RIN_JNI_UTF_WITH_JNI
#include <jni.h>

namespace rin_jni {

// jstring -> std::string (UTF-8 قياسي). null أو فشل => "".
inline std::string toStd(JNIEnv* env, jstring s) {
    if (env == nullptr || s == nullptr) return std::string();
    const jsize len = env->GetStringLength(s);
    if (len <= 0) return std::string();
    const jchar* chars = env->GetStringChars(s, nullptr);
    if (chars == nullptr) { env->ExceptionClear(); return std::string(); }
    std::string out = utf::toUtf8(reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(len));
    env->ReleaseStringChars(s, chars);
    return out;
}

// std::string (UTF-8 قياسي) -> jstring. لا يُرجع null أبداً إلا عند نفاد الذاكرة (مع تنظيف أي استثناء معلّق).
inline jstring newJString(JNIEnv* env, const std::string& s) {
    std::u16string u = utf::toUtf16(s);
    jstring r = env->NewString(reinterpret_cast<const jchar*>(u.data()), static_cast<jsize>(u.size()));
    if (r == nullptr && env->ExceptionCheck()) env->ExceptionClear();
    return r;
}
inline jstring newJString(JNIEnv* env, const char* s) {
    return newJString(env, std::string(s ? s : ""));
}

} // namespace rin_jni
#endif // RIN_JNI_UTF_WITH_JNI
