#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // السلوك الأصلي: Section تفتح بيئة خاصة لمتغيراتها وتطبع 🔹/◽ (بلا تغيير)
        Section=intro
            let x = 5;
            print x;
        .end/Section

        // ميزة 1+2: حالة القسم تُحفَظ بعد الإغلاق ويمكن قراءتها عبر sectionVars(name)
        Section=stats
            let total = 42;
            let label = "ok";
        .end/Section
        print sectionVars("stats");

        // اسم غير معروف -> map فارغ (وليس خطأ)
        print sectionVars("nope");

        // ميزة 3: sectionNames() ترجع كل الأسماء المُنفَّذة بترتيب أول ظهور، بلا تكرار حتى لو
        // أُعيد تنفيذ نفس الاسم (هنا 'a' يُنفَّذ مرتين، لكنه يظهر مرة واحدة فقط بالترتيب)
        Section=a
            let x = 1;
        .end/Section
        Section=b
            let y = 2;
        .end/Section
        Section=a
            let x = 99;
        .end/Section
        print sectionNames();
        print sectionVars("a"); // آخر تنفيذ يفوز

        // hasSection(name) -> فحص وجود قسم بالاسم دون الحاجة لقراءة كل المتغيرات
        print hasSection("known") == false;
        Section=known
            let z = 1;
        .end/Section
        print hasSection("known");

        // النطاق (scoping) لم يتغيّر: متغيرات القسم ما زالت لا تتسرّب للخارج
        Section=leaky
            let secret = 123;
        .end/Section
        print hasSection("leaky");

        // الأقسام المجهولة (بلا اسم) تبقى زخرفية تماماً كالسابق: غير قابلة للاستعلام
        Section
            let anon = 1;
        .end/Section
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
