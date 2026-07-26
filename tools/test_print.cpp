#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // السلوك الأصلي: قيمة واحدة + سطر جديد تلقائي (بلا أي تغيير)
        print "hello";
        print 42;

        // ميزة 1: عدّة قيم مفصولة بفواصل في نفس أمر print (تُفصل افتراضياً بمسافة واحدة)
        let name = "سارة";
        let age = 28;
        print "name=", name, " age=", age;

        // ميزة 2: sep= يستبدل الفاصل الافتراضي بين القيم المتعددة
        print 1, 2, 3 sep=" - ";
        print "a", "b", "c" sep="";

        // ميزة 3: end= يستبدل نهاية السطر الافتراضية "\n" (end="" يمنع السطر الجديد كلياً)
        print "a" end="";
        print "b" end="";
        print "c";

        // الميزتان معاً في نفس الأمر
        print "x", "y", "z" sep="," end="!\n";

        // sep/end يقبلان أي تعبير (مو فقط حرفاً نصياً)، طالما يُقيَّم إلى نص وقت التشغيل
        let dash = "-";
        print 10, 20 sep=dash;
    )";

    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::cout << interp.run(statements);
    } catch (rin::RinError& e) {
        std::cerr << "[Error line " << e.line << "]: " << e.message << std::endl;
        return 1;
    }
    return 0;
}
