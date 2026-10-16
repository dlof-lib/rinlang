// مشغّل اختبار على لينكس لـ rin::terminal::EmbeddedTerminal — نفس الواجهة التي يستدعيها جسر JNI في أندرويد
// (app/src/main/cpp/terminal/rin_terminal_jni.cpp). يقرأ أسطراً من stdin ويطبع مخرجات كل سطر ونتيجته
// (Ok / Failed / NeedMore / Quit)، ويتحقق أن مجلد العملية لم يتغيّر بعد :cd داخل الجلسة.
//
// البناء (بعد ترجمة ملفات المحرّك إلى .o كما في CMakeLists.txt):
//   g++ -std=c++17 -DRIN_TERMINAL_EMBED -Iapp/src/main/cpp -Icli/linux/src -Icli/linux/src/terminal \
//       tests/terminal/embed_harness.cpp cli/linux/src/terminal/rin_terminal.cpp <engine .o files> -lz -ldl -o embed_harness
// التشغيل:
//   printf 'let x = 40;\n:vars\n:cd ..\n:pwd\n' | ./embed_harness /path/to/project
// مشغّل اختبار على جهاز التطوير لـ EmbeddedTerminal (نفس ما يستدعيه JNI)
#include "terminal/rin_terminal.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <unistd.h>
using namespace rin::terminal;
static const char* nm(EmbeddedTerminal::Result r){ switch(r){case EmbeddedTerminal::Result::Ok:return "Ok";case EmbeddedTerminal::Result::Failed:return "Failed";case EmbeddedTerminal::Result::NeedMore:return "NeedMore";default:return "Quit";} }
int main(int argc, char** argv) {
    EmbedConfig cfg; cfg.cwd = argv[1]; cfg.english = true;
    EmbeddedTerminal t(cfg);
    char start[4096]; getcwd(start, sizeof start);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string out;
        auto r = t.feed(line, [&](const std::string& s){ out += s; },
                        [&](const std::string& p, std::string& a){ out += "[INPUT? " + p + "]"; a = "Ali"; return true; });
        std::printf("> %s\n%s%s=> %s\n", line.c_str(), out.c_str(), (out.empty()||out.back()=='\n')?"":"\n", nm(r));
    }
    char end[4096]; getcwd(end, sizeof end);
    std::printf("process cwd unchanged: %s\n", std::string(start)==std::string(end) ? "yes" : "NO");
}
