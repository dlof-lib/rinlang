// tools/rin_check.cpp
// ============================================================================
// `rin check` — أداة سطر أوامر مستقلة تفحص ملف .rin (lex + parse فقط، بلا تنفيذ)
// وتطبع كل Diagnostics التي يمكن جمعها بأمان دفعة واحدة (multi-error + error
// recovery)، بأحد أربعة تنسيقات. هذا هو نفس الهدف "rincheck" المُضاف في
// app/src/main/cpp/CMakeLists.txt (يُبنى كأداة مطوّر مستقلة على جهاز التطوير،
// لا يُحزَم داخل APK ولا يُربط بـ rinengine)، بنفس روح tools/rin_run.cpp.
//
// راجع docs/ERROR_SYSTEM.md لتوثيق كامل لنظام Diagnostics ولكل أكواد الأخطاء.
//
// الاستخدام:
//   rin_check path/to/file.rin                    -> عرض rustc-style كامل (الافتراضي)
//   rin_check path/to/file.rin --format=short      -> سطر واحد لكل خطأ
//   rin_check path/to/file.rin --format=json       -> مصفوفة JSON (rin check --format=json)
//   rin_check path/to/file.rin --format=lsp        -> مصفوفة Diagnostics بصيغة LSP
//
// كود الخروج: 0 إن لم توجد أخطاء (قد توجد تحذيرات)، 1 إن وُجد خطأ واحد أو أكثر،
// 2 إن تعذّر فتح الملف.
// ============================================================================
#include "rin_lexer.h"
#include "rin_parser.h"
#include "diagnostics/diagnostic_engine.h"
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/source_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

using namespace rin;

static std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: rin_check <file.rin> [--format=plain|short|json|lsp]\n";
        return 2;
    }
    std::string path = argv[1];
    diag::OutputFormat fmt = diag::OutputFormat::Plain;
    for (int i = 2; i < argc; ++i) {
        std::string opt = argv[i];
        if (opt == "--format=json") fmt = diag::OutputFormat::Json;
        else if (opt == "--format=short") fmt = diag::OutputFormat::Short;
        else if (opt == "--format=lsp") fmt = diag::OutputFormat::Lsp;
        else if (opt == "--format=plain") fmt = diag::OutputFormat::Plain;
        else {
            std::cerr << "rin_check: unknown option '" << opt << "'\n";
            return 2;
        }
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "rin_check: تعذّر فتح الملف '" << path << "'\n";
        return 2;
    }
    std::string source = readAll(f);

    diag::DiagnosticEngine engine;
    try {
        Lexer lexer(source, path);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens, path);
        // يجمع كل أخطاء parse الممكن جمعها بأمان في تشغيل واحد (بدل التوقف عند أول خطأ)،
        // مستأنفاً من نقاط تزامن (';' / '}' / ')' / '.end/...') بعد كل خطأ — انظر
        // Parser::parseCollectingDiagnostics و Parser::synchronize في rin_parser.cpp.
        parser.parseCollectingDiagnostics(engine);
    } catch (RinError& e) {
        // خطأ Lexer: لا يزال يُرمى مباشرة (لا نقطة تزامن آمنة بعد رمز غير صالح داخل lexing نفسه).
        if (e.diagnostic) {
            engine.emit(*e.diagnostic);
        } else {
            std::cerr << "rin_check: " << path << ":" << e.line << ": " << e.message << "\n";
            return 1;
        }
    }

    std::cout << diag::renderAll(engine, diag::globalSourceManager(), fmt);
    return engine.hasErrors() ? 1 : 0;
}
