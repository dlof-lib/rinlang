// tools/rin_run.cpp
// ============================================================================
// مُشغِّل سطر أوامر مستقل لمحرّك Rin (بلا أندرويد وبلا JNI). الغاية الأساسية منه:
// تمكين ميزة "▶ Preview rin" في ملفات README/التوثيق — أي كتلة كود Rin موسومة
// بالكلمة المفتاحية "view Preview rin" تُنفَّذ فعلياً وتُحقن نتيجتها الحقيقية
// أسفلها (راجع tools/rin_md_preview.py وقسم "المعاينة الحية" في README.md).
//
// الاستخدام:
//   rin_run path/to/file.rin      -> يشغّل محتوى الملف
//   rin_run < snippet.rin         -> يقرأ الكود من stdin (لا وسيطة)
//   rin_run -c "print 1+1;"       -> يشغّل نصاً مُمرَّراً مباشرة
// ============================================================================
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

static std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    std::string source;
    std::string sourceName = "<stdin>";

    if (argc >= 3 && std::strcmp(argv[1], "-c") == 0) {
        source = argv[2];
        sourceName = "<inline>";
    } else if (argc >= 2) {
        std::ifstream f(argv[1]);
        if (!f) {
            std::cerr << "rin_run: تعذّر فتح الملف '" << argv[1] << "'" << std::endl;
            return 2;
        }
        source = readAll(f);
        sourceName = argv[1];
    } else {
        source = readAll(std::cin);
    }

    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        std::cout << out;
    } catch (rin::RinError& e) {
        std::cerr << "خطأ في " << sourceName << " عند السطر " << e.line << ": " << e.message << std::endl;
        return 1;
    }
    return 0;
}
