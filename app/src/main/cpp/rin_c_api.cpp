// rin_c_api.cpp
// تنفيذ واجهة C المسطّحة المُعرَّفة في rin_c_api.h — نفس منطق jni_bridge.cpp
// تماماً (lex -> parse -> interpret) لكن بدون أي اعتماد على JNI/أندرويد، حتى
// تُبنى كمكتبة مشتركة عامة تستدعيها أي لغة برمجة أخرى.
#include "rin_c_api.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"

#include <cstring>
#include <string>

namespace {

// ينسخ std::string إلى char* مخصَّص بـ malloc (وليس new[]) عمداً: هذا يجعل
// التحرير متوافقاً مع free() القياسية في C، وهي الأسهل استدعاءً من لغات
// أخرى (ctypes في بايثون مثلاً يستخدم libc's free مباشرة عبر rin_free_string).
char* dupToC(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

std::string runOnce(const std::string& source, const std::string& basePath) {
    std::string result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interpreter;
        if (!basePath.empty()) {
            interpreter.setBasePath(basePath);
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
    return result;
}

} // namespace

extern "C" {

RIN_API char* rin_run(const char* source, const char* basePath) {
    std::string src = source ? source : "";
    std::string base = basePath ? basePath : "";
    return dupToC(runOnce(src, base));
}

RIN_API void rin_free_string(char* s) {
    if (s) std::free(s);
}

RIN_API const char* rin_engine_version(void) {
    // سلسلة ثابتة (static)، لا تحتاج تحريراً من المستدعي.
    static const char* kVersion = "Rin Engine 1.1 (C++17) - C ABI";
    return kVersion;
}

// ---- الجلسات (Sessions) ----

struct RinSession {
    std::string basePath;
};

RIN_API RinSession* rin_session_create(const char* basePath) {
    auto* s = new (std::nothrow) RinSession();
    if (!s) return nullptr;
    s->basePath = basePath ? basePath : "";
    return s;
}

RIN_API char* rin_session_run(RinSession* session, const char* source) {
    if (!session) {
        return dupToC("[Internal error]: null session");
    }
    std::string src = source ? source : "";
    return dupToC(runOnce(src, session->basePath));
}

RIN_API void rin_session_free(RinSession* session) {
    delete session;
}

} // extern "C"
