// ============================================================================
//  rin — Rin Language CLI (Linux)
// ============================================================================
//  مُفسِّر Rin كسطر أوامر مستقل تماماً عن أندرويد/JNI — نفس محرّك التفسير
//  الحقيقي المستخدَم في التطبيق (rin_lexer / rin_parser / rin_interpreter)،
//  مبني هنا كملف تنفيذي عادي (rin) عبر CMake (g++ أو clang++).
//
//  الاستخدام:
//    rin path/to/file.rin      تشغيل ملف
//    rin -c "print 1+1;"       تشغيل كود مُمرَّر مباشرة كنص
//    rin                       وضع تفاعلي (REPL) إن كانت الطرفية تفاعلية
//    rin < snippet.rin         قراءة الكود من stdin (أنبوب/إعادة توجيه)
//    rin --version | -v        رقم الإصدار
//    rin --help    | -h        رسالة المساعدة هذه
// ============================================================================
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>

namespace {

constexpr const char* kVersion = "0.1.0";

std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void printUsage() {
    std::cout <<
        "Rin Language CLI (Linux) - v" << kVersion << "\n\n"
        "الاستخدام:\n"
        "  rin <file.rin>            تشغيل ملف .rin\n"
        "  rin -c \"print 1+1;\"       تشغيل كود مُمرَّر مباشرة كنص\n"
        "  rin                       وضع تفاعلي (REPL) في طرفية تفاعلية\n"
        "  rin < file.rin            قراءة الكود من stdin\n"
        "  rin --version | -v        إظهار رقم الإصدار\n"
        "  rin --help    | -h        هذه الرسالة\n";
}

/** Lexes/parses/interprets [source] against the given (possibly REPL-shared) interpreter.
 *  Returns false (and prints a Rin-style "file:line: message" error) on failure. */
bool runSource(const std::string& source, const std::string& sourceName, rin::Interpreter& interp) {
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        std::string out = interp.run(statements);
        std::cout << out;
        return true;
    } catch (rin::RinError& e) {
        std::cerr << "\n[rin] خطأ في " << sourceName << " عند السطر " << e.line << ": " << e.message << "\n";
        return false;
    } catch (std::exception& e) {
        std::cerr << "\n[rin] خطأ داخلي: " << e.what() << "\n";
        return false;
    }
}

int runRepl() {
    std::cout << "Rin v" << kVersion << " - وضع تفاعلي. اكتب سطر Rin ثم Enter لتنفيذه (exit للخروج).\n";
    rin::Interpreter interp; // نفس المفسِّر عبر كل الأسطر: المتغيرات/الحاويات تبقى محفوظة بينها.
    std::string line;
    int lineNo = 1;
    while (true) {
        std::cout << "rin[" << lineNo << "]> ";
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;
        runSource(line, "<repl>", interp);
        std::cout << "\n";
        ++lineNo;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        std::string arg1 = argv[1];
        if (arg1 == "--version" || arg1 == "-v") {
            std::cout << "rin " << kVersion << "\n";
            return 0;
        }
        if (arg1 == "--help" || arg1 == "-h") {
            printUsage();
            return 0;
        }
    }

    std::string source;
    std::string sourceName = "<stdin>";

    if (argc >= 3 && std::strcmp(argv[1], "-c") == 0) {
        source = argv[2];
        sourceName = "<inline>";
    } else if (argc >= 2) {
        std::ifstream f(argv[1], std::ios::binary);
        if (!f) {
            std::cerr << "rin: تعذّر فتح الملف '" << argv[1] << "'\n";
            return 2;
        }
        source = readAll(f);
        sourceName = argv[1];
    } else if (isatty(fileno(stdin))) {
        // لا وسائط ولا إعادة توجيه: طرفية تفاعلية حقيقية -> REPL بدل انتظار EOF صامت.
        return runRepl();
    } else {
        source = readAll(std::cin);
    }

    rin::Interpreter interp;
    bool ok = runSource(source, sourceName, interp);
    return ok ? 0 : 1;
}
