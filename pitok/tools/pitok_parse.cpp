// pitok_parse.cpp — أداة CLI مستقلة تمامًا: تقرأ ملف واجهة (.rin أو .pitok)، تُشغّل
// pitok::Lexer ثم pitok::Parser (بلا أي علاقة بمصادر Rin العامة)، وتطبع AST كـJSON.
// الغرض: إثبات فعلي (لا افتراضي) أن واجهات PITOK تُحلَّل الآن بمُحلِّل خاص بها، منفصل
// تمامًا عن rin_lexer.cpp/rin_parser.cpp.
//
// بناء: g++ -std=c++17 -I../include ../src/pitok_lexer.cpp ../src/pitok_parser.cpp pitok_parse.cpp -o pitok_parse
// تشغيل: ./pitok_parse path/to/file.rin

#include "pitok_lexer.h"
#include "pitok_parser.h"
#include "pitok_json.h"
#include <fstream>
#include <sstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "الاستخدام: pitok_parse <ملف.rin>\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "تعذّر فتح الملف: " << argv[1] << "\n";
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string source = buf.str();

    try {
        pitok::Lexer lexer(source, argv[1]);
        auto tokens = lexer.scanTokens();

        pitok::Parser parser(tokens, argv[1]);
        pitok::Program prog = parser.parseProgram();

        std::cout << pitok::dumpProgram(prog) << "\n";
        std::cerr << "[pitok] تحليل ناجح — " << tokens.size() << " توكن، "
                  << prog.roots.size() << " عنصر جذر، " << prog.warps.size() << " إعلان warp.\n";
        return 0;
    } catch (const pitok::LexError& e) {
        std::cerr << "[pitok] خطأ معجمي عند " << argv[1] << ":" << e.line << ":" << e.column
                  << " — " << e.what() << "\n";
        return 2;
    } catch (const pitok::ParseError& e) {
        std::cerr << "[pitok] خطأ نحوي عند " << argv[1] << ":" << e.line << ":" << e.column
                  << " — " << e.what() << "\n";
        return 2;
    }
}
