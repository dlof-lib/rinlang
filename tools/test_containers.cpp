#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        @container=my_data
            text title = "بيانات ريـن";
            print title;

            Section=numbers
                let a = 10;
                let b = 4;
                print Addition(a, b);
                print Subtraction(a, b);
                print Multiplication(a, b);
                print Equal(a, b);
            .end/Section

            Translations
                translation lang="ar" text="مرحبا";
                translation lang="en" text="Hello";
            .end/Translations

            file path="data/output.rin";
            installation my_data;
            simplified save path="data/output.min.rin";
        .end/container

        @container=extra
            text note = "بيانات إضافية";
        .end/container

        @container=my_data2
            link to=my_data;
            tying with=extra;
            merge with=extra;
        .end/container

        @Containers.Group=my_group
            @container=g1
                text label = "عنصر داخل مجموعة";
            .end/container
        .end/Containers.Group

        @Volume=vol1
            @Containers.Group=g2
                @container=c2
                    text v = "داخل مجلد";
                .end/container
            .end/Containers.Group
        .end/Volume

        @Everything=my_app
            fun greet(name) {
                return "Hello " + name;
            }

            @table=users
                row cells=["1", "سارة"];
            .end/table

            @doc=notes
                document id="n1" fields={ note: "ملاحظة" };
            .end/doc

            print greet("World");
        .end/Everything

        @Everything=dynamic_demo
            fun makeGreeter(prefix) {
                fun greet(who) { return prefix + " " + who; }
                return greet;
            }
            spawn("table", "dyn_a");
            setField("dyn_a", "note", "spawned at runtime");
            setField("dyn_a", "sayHi", makeGreeter("Hi from dyn_a ->"));
            print "dyn_a kind =", kindOf("dyn_a");
            print "dyn_a note =", getField("dyn_a", "note");
            print callFn(getField("dyn_a", "sayHi"), ["World"]);
            print "hasContainer(dyn_a) =", hasContainer("dyn_a");
            print "destroyContainer(dyn_a) =", destroyContainer("dyn_a");
            print "hasContainer(dyn_a) after destroy =", hasContainer("dyn_a");
        .end/Everything
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
