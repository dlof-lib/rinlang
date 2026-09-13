#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // 1) مجموعة تضم حاويتين مباشرة
        @Containers.Group=team_alpha
            @container=alpha_1
                text label = "أول";
            .end/container
            @container=alpha_2
                text label = "ثاني";
            .end/container
        .end/Containers.Group

        // 2) مجموعة أب تضم حاوية مباشرة + مجموعة فرعية متداخلة
        @Containers.Group=team_root
            @container=root_c
                text label = "جذر";
            .end/container
            @Containers.Group=team_nested
                @container=nested_c
                    text label = "متداخل";
                .end/container
            .end/Containers.Group
        .end/Containers.Group

        // 3) استعلام عن أعضاء المجموعات عبر الدوال الجاهزة
        print "أعضاء team_alpha المباشرون:";
        print groupMembers("team_alpha");        // ["alpha_1", "alpha_2"]
        print "حاويات team_alpha (مفلطحة):";
        print groupContainers("team_alpha");      // ["alpha_1", "alpha_2"]

        print "أعضاء team_root المباشرون (تحتوي على مجموعة فرعية):";
        print groupMembers("team_root");          // ["root_c", "team_nested"]
        print "كل حاويات team_root (بما فيها المتداخلة):";
        print groupContainers("team_root");       // ["root_c", "nested_c"]

        // 4) tying تستهدف مجموعة كاملة دفعة واحدة (تنسخ متغيرات كل أعضائها بالترتيب)
        @container=summary
            tying with=team_alpha; // ينسخ label من alpha_1 ثم من alpha_2 (يفوز الأخير عند تعارض الاسم)
            print label;           // "ثاني"
        .end/container

        // 5) link يقبل اسم مجموعة أيضاً (بدون نسخ، فقط تحقّق من وجودها)
        @container=viewer
            link to=team_root;
        .end/container
    )";

    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        std::cout << out;
    } catch (rin::RinError& e) {
        std::cout << "Parse/Lex error at line " << e.line << ": " << e.message << std::endl;
        return 1;
    }
    return 0;
}
