// ============================================================================
//  rin.exe — Rin Language CLI (Windows)
// ============================================================================
//  مُفسِّر Rin كسطر أوامر مستقل تماماً عن أندرويد/JNI — نفس محرّك التفسير
//  الحقيقي المستخدَم في التطبيق (rin_lexer / rin_parser / rin_interpreter)،
//  مبني هنا كملف تنفيذي (rin.exe) عادي عبر CMake (MSVC أو MinGW).
//
//  الاستخدام:
//    rin.exe path\to\file.rin     تشغيل ملف
//    rin.exe -c "print 1+1;"      تشغيل كود مُمرَّر مباشرة كنص
//    rin.exe                      وضع تفاعلي (REPL) إن كان الطرفية تفاعلية
//    rin.exe < snippet.rin        قراءة الكود من stdin (عند عدم التفاعلية، مثل
//                                 إعادة توجيه ملف أو أنبوب)
//    rin.exe --version | -v       رقم الإصدار
//    rin.exe --help    | -h       رسالة المساعدة هذه
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
#include <vector>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define RIN_ISATTY(fd) _isatty(fd)
    #define RIN_FILENO(stream) _fileno(stream)
#else
    #include <unistd.h>
    #define RIN_ISATTY(fd) isatty(fd)
    #define RIN_FILENO(stream) fileno(stream)
#endif

namespace {

constexpr const char* kVersion = "0.1.0";

std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void printUsage() {
    std::cout <<
        "Rin Language CLI (Windows) - v" << kVersion << "\n\n"
        "الاستخدام:\n"
        "  rin <file.rin>            تشغيل ملف .rin\n"
        "  rin -c \"print 1+1;\"       تشغيل كود مُمرَّر مباشرة كنص\n"
        "  rin                       وضع تفاعلي (REPL) في طرفية تفاعلية\n"
        "  rin < file.rin            قراءة الكود من stdin\n"
        "  rin check <file.rin> [--format=plain|short|json|lsp]\n"
        "                            فحص الملف فقط (lex+parse، بلا تنفيذ) وطباعة كل الأخطاء\n"
        "  rin --version | -v        إظهار رقم الإصدار\n"
        "  rin --help    | -h        هذه الرسالة\n"
        "  --import-progress         (يُضاف لأي استدعاء أعلاه) شريط تحميل حي لـ @import\n";
}

/** Enables UTF-8 input/output on the Windows console so Arabic text (source, output,
 *  error messages) renders correctly instead of as mojibake in the legacy code page.
 *  No-op on other platforms, where the terminal is already UTF-8 by default. */
void enableWindowsUtf8Console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
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
    enableWindowsUtf8Console();

    // --import-progress: علم اختياري يعمل مع أي شكل استدعاء أدناه (ملف/-c/stdin)، فيُزال من
    // قائمة الوسائط قبل أي معالجة أخرى حتى لا يتعارض مع argv[1]==check/-c/... الحالية. غيابه
    // (الحالة الافتراضية) يعني سلوكاً مطابقاً تماماً لما قبل إضافة هذه الميزة — بلا أي فرق.
    std::vector<std::string> args;
    bool importProgress = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--import-progress") importProgress = true;
        else args.push_back(std::move(a));
    }

    if (!args.empty()) {
        const std::string& arg1 = args[0];
        if (arg1 == "--version" || arg1 == "-v") {
            std::cout << "rin " << kVersion << "\n";
            return 0;
        }
        if (arg1 == "--help" || arg1 == "-h") {
            printUsage();
            return 0;
        }
        if (arg1 == "check" && args.size() >= 2) {
            rin::diag::OutputFormat fmt = rin::diag::OutputFormat::Plain;
            std::string filePath = args[1];
            for (size_t i = 2; i < args.size(); ++i) {
                const std::string& opt = args[i];
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

    if (args.size() >= 2 && args[0] == "-c") {
        source = args[1];
        sourceName = "<inline>";
    } else if (!args.empty()) {
        std::ifstream f(args[0], std::ios::binary);
        if (!f) {
            std::cerr << "rin: تعذّر فتح الملف '" << args[0] << "'\n";
            return 2;
        }
        source = readAll(f);
        sourceName = args[0];
    } else if (RIN_ISATTY(RIN_FILENO(stdin))) {
        // لا وسائط ولا إعادة توجيه: طرفية تفاعلية حقيقية -> REPL بدل انتظار EOF صامت.
        return runRepl();
    } else {
        source = readAll(std::cin);
    }

    rin::Interpreter interp;
    if (importProgress) interp.setImportUIMode(rin::loaderui::Mode::Verbose);
    bool ok = runSource(source, sourceName, interp);
    return ok ? 0 : 1;
}
