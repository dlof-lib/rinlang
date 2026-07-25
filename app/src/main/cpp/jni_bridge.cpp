// jni_bridge.cpp
// Exposes the Rin C++ engine to Kotlin through JNI.
// Kotlin side: RinEngine.kt declares the matching `external fun` signatures.
#include <jni.h>
#include <string>
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSource(JNIEnv* env, jobject /* this */, jstring sourceJStr) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);

    std::string result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interpreter;
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
    return env->NewStringUTF("Rin Engine 1.0 (C++17)");
}
