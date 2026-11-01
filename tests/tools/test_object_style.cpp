#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

static void runOk(const std::string& label, const std::string& source) {
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::cout << "-- " << label << " --\n" << interp.run(statements) << "\n";
    } catch (rin::RinError& e) {
        std::cerr << "[UNEXPECTED ERROR][" << label << "][line " << e.line << "]: " << e.message << std::endl;
    }
}

// ملاحظة: Interpreter::run() يلتقط RinError داخلياً ويُلحق رسالته بنص المخرجات (لا يُعيد رميها)،
// لذا نتحقق من احتواء المخرجات على "[Error" بدلاً من انتظار استثناء يصل إلى هذا المستوى.
static void runExpectError(const std::string& label, const std::string& source) {
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        std::cout << "-- " << label << " --\n" << out << "\n";
        if (out.find("[Error") == std::string::npos) {
            std::cerr << "[MISSING EXPECTED ERROR][" << label << "]" << std::endl;
        }
    } catch (rin::RinError& e) {
        std::cout << "-- " << label << " (parse-time error, as expected) --\n[Error line " << e.line << "]: " << e.message << "\n\n";
    }
}

int main() {
    // 1) الاستخدام الصحيح: كل الحقول الستة الجديدة داخل @Object، بالإضافة إلى style وtext العاديين.
    runOk("txt/img/object.file/Fonts/background/css3 dakhil @Object", R"RIN(
        @Object=card
            text name = "بطاقة";
            txt title = "عنوان البطاقة";
            img cover = "assets/cover.png";
            object.file data = "assets/card.json";
            Fonts family = "Cairo";
            background bg = "linear-gradient(#111, #333)";
            css3 extra = "border-radius: 12px;";
            style value="style://dark";
        .end/Object
    )RIN");

    // 2) نفس الفكرة داخل @container.object (الصيغة البديلة لنفس ContainerKind::OBJECT).
    runOk("nafs alhuqul 3abr @container.object", R"RIN(
        @container.object=button
            txt label = "اضغط هنا";
            background bg = "#3498db";
        .end/container.object
    )RIN");

    // 3) خطأ متوقّع: استخدام 'txt' خارج @Object (مثلاً داخل container.table) يجب أن يُرفض.
    runExpectError("txt kharij @Object (yajib an yurfad)", R"RIN(
        @table=t
            txt label = "لا يجوز هنا";
        .end/table
    )RIN");

    // 4) خطأ متوقّع: استخدام 'css3' خارج أي حاوية بيانات (على مستوى البرنامج مباشرة).
    runExpectError("css3 kharij ay haawiya (yajib an yurfad)", R"RIN(
        css3 extra = "color: red;";
    )RIN");

    // 5) التأكد أن 'file' الأصلية (داخل container.import) ما زالت تعمل بلا أي تعارض مع 'object.file'.
    runOk("'file' al2asliya dakhil container.import maazalat ta3mal", R"RIN(
        @container.import
            file path="lib/math.og.rin";
        .end/container.import
        print "import ok";
    )RIN");

    // 6) unified OOP + loop container:
    // @container.open/object permits members, methods, and all existing loop constructs.
    // ".object=text" is the compact member declaration, while "rinopen" is the unified loop name.
    runOk("container.open/object + .object + rinopen", R"RIN(
        @container.open/object=counter
            .object=text title = "Rin";
            let i = 0;
            fun greet() { print title; }
            rinopen(i < 3) {
                print i;
                i = i + 1;
            }
            while (i < 5) {
                i = i + 1;
            }
            for (let j = 0; j < 2; j = j + 1) {
                print j;
            }
        .end/container.open/object
    )RIN");

    return 0;
}
