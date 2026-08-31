// object_literal_selftest.cpp — standalone harness (same pattern as flow_selftest.cpp) to verify
// the new `.object("id") ... .end/object` / `view.print/object(...)` syntax end-to-end, outside
// Android/JNI.
// Build: g++ -std=c++17 -I. object_literal_selftest.cpp rin_lexer.cpp rin_parser.cpp \
//        rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp diagnostics/source_manager.cpp \
//        diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp -o object_literal_selftest
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

using namespace rin;

int main() {
    int failures = 0;

    auto run = [&](const std::string& name, const std::string& src, bool expectError = false) {
        std::cout << "== " << name << " ==\n";
        try {
            Lexer lexer(src);
            auto tokens = lexer.scanTokens();
            Parser parser(tokens);
            auto program = parser.parse();
            Interpreter interp;
            std::string out = interp.run(program);
            std::cout << out;
            if (interp.hadError()) throw RinError(interp.lastErrorMessage().value_or("unknown error"), interp.lastErrorLine());
            if (expectError) {
                std::cout << "FAIL: expected an error but none was thrown\n";
                failures++;
            }
        } catch (RinError& e) {
            std::cout << "error: " << e.message << "\n";
            if (!expectError) { std::cout << "FAIL: unexpected error\n"; failures++; }
        }
        std::cout << "\n";
    };

    run("basic object literal + print", R"(
.object("user01")
    name("ABOO");
    age(19);
    online(true);
.end/object

print user01;
print user01["name"];
)");

    run("typed fields + container.() + view.print/object by id", R"(
.object("user01")
    name("ABOO");
    age(19);
    online(true);
    image:("img.png");
    number:();
    container.();
.end/object

view.print/object("user01");
)");

    run("view.print/object with a direct map value", R"(
.object("p1")
    name("Sara");
.end/object

view.print/object(p1);
)");

    run("view.print/object on unregistered id should error", R"(
.object("noreg")
    name("X");
.end/object

view.print/object("noreg");
)", /*expectError=*/true);

    run("old `.object=text` form still works unchanged", R"(
@container.open/object=user
    .object=text
    greeting = "hi";
.end/container.open/object
print "ok";
)");

    std::cout << (failures == 0 ? "ALL PASSED\n" : (std::to_string(failures) + " FAILURE(S)\n"));
    return failures == 0 ? 0 : 1;
}
