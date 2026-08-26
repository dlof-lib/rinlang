// ============================================================================
//  rin — Rin Language CLI (macOS)
// ============================================================================
//  مُفسِّر Rin كسطر أوامر مستقل تماماً عن أندرويد/JNI — نفس محرّك التفسير
//  الحقيقي المستخدَم في التطبيق (rin_lexer / rin_parser / rin_interpreter)،
//  مبني هنا كملف تنفيذي عادي (rin) عبر CMake + clang++ (Xcode Command Line Tools).
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
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/source_manager.h"

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
        "Rin Language CLI (macOS) - v" << kVersion << "\n\n"
        "الاستخدام:\n"
        "  rin <file.rin>            تشغيل ملف .rin\n"
        "  rin -c \"print 1+1;\"       تشغيل كود مُمرَّر مباشرة كنص\n"
        "  rin                       وضع تفاعلي (REPL) في طرفية تفاعلية\n"
        "  rin < file.rin            قراءة الكود من stdin\n"
        "  rin check <file.rin> [--format=plain|short|json|lsp]\n"
        "                            فحص الملف فقط (lex+parse، بلا تنفيذ) وطباعة كل الأخطاء\n"
        "  rin --version | -v        إظهار رقم الإصدار\n"
        "  rin --help    | -h        هذه الرسالة\n";
}

/** Lexes/parses/interprets [source] against the given (possibly REPL-shared) interpreter.
 *  Returns false (and prints a rustc-style Diagnostic, or a legacy "file:line: message"
 *  fallback for any error not yet migrated to the new Diagnostics system) on failure. */
bool runSource(const std::string& source, const std::string& sourceName, rin::Interpreter& interp) {
    try {
        rin::Lexer lexer(source, sourceName);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, sourceName);
        auto statements = parser.parse();
        interp.setSourceFile(sourceName);
        std::string out = interp.run(statements);
        std::cout << out;
        return true;
    } catch (rin::RinError& e) {
        if (e.diagnostic) {
            std::cerr << "\n" << rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());
        } else {
            std::cerr << "\n[rin] خطأ في " << sourceName << " عند السطر " << e.line << ": " << e.message << "\n";
        }
        return false;
    } catch (std::exception& e) {
        std::cerr << "\n[rin] خطأ داخلي: " << e.what() << "\n";
        return false;
    }
}

// `rin check <file> [--format=...]` — lex+parse فقط (بلا تنفيذ)، مع جمع كل أخطاء التحليل
// الممكن جمعها بأمان دفعة واحدة (multi-error + recovery). راجع docs/ERROR_SYSTEM.md.
int runCheck(const std::string& path, rin::diag::OutputFormat fmt) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "rin check: تعذّر فتح الملف '" << path << "'\n";
        return 2;
    }
    std::string source = readAll(f);

    rin::diag::DiagnosticEngine engine;
    try {
        rin::Lexer lexer(source, path);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, path);
        parser.parseCollectingDiagnostics(engine);
    } catch (rin::RinError& e) {
        if (e.diagnostic) engine.emit(*e.diagnostic);
    }

    std::cout << rin::diag::renderAll(engine, rin::diag::globalSourceManager(), fmt);
    return engine.hasErrors() ? 1 : 0;
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
        if (arg1 == "check" && argc >= 3) {
            rin::diag::OutputFormat fmt = rin::diag::OutputFormat::Plain;
            std::string filePath = argv[2];
            for (int i = 3; i < argc; ++i) {
                std::string opt = argv[i];
                if (opt == "--format=json") fmt = rin::diag::OutputFormat::Json;
                else if (opt == "--format=short") fmt = rin::diag::OutputFormat::Short;
                else if (opt == "--format=lsp") fmt = rin::diag::OutputFormat::Lsp;
                else if (opt == "--format=plain") fmt = rin::diag::OutputFormat::Plain;
            }
            return runCheck(filePath, fmt);
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
