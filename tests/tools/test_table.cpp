#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // مستقلة: @table=name ... .end/table
        @table=scores
            text title = "نتائج الطلاب";
            style value="style://dark";
            row cells=["اسم", "الدرجة"];
            row cells=["سارة", 95];
            row cells=["يوسف", 88];
            save format=png;
            save;
        .end/table

        // مدمجة داخل container: @container.table=name ... .end/container.table
        @container.table=inventory
            row cells=["تفاح", 10];
            row cells=["موز", 20];
            style value="style://light";
        .end/container.table

        @Containers.Group=reports
            @table=q1
                row cells=[1, 2, 3];
            .end/table
            @container.data=meta
                text note = "بيانات وصفية عادية";
            .end/container.data
        .end/Containers.Group

        installation reports format=zip;
        installation scores format=zip;
    )";

    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        interp.setBasePath("/tmp/rin_table_test");
        std::string out = interp.run(statements);
        std::cout << out;
    } catch (rin::RinError& e) {
        std::cout << "Parse/Lex error at line " << e.line << ": " << e.message << std::endl;
        return 1;
    }
    return 0;
}
