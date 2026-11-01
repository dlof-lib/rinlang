#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>
#include <string>

static bool runCase(const std::string& source, const std::string& expected) {
    try {
        rin::Lexer lexer(source, "core_completion_test.rin");
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, "core_completion_test.rin");
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        if (out != expected) {
            std::cerr << "Expected:\n" << expected << "Got:\n" << out << std::endl;
            return false;
        }
        return true;
    } catch (const rin::RinError& e) {
        std::cerr << "RinError line " << e.line << ": " << e.message << std::endl;
        return false;
    }
}

int main() {
    const std::string source = R"(
        let a = true ? "A" : "B";
        let b = false ? "BAD" : "B";
        print a + b;
        try { throw "boom"; } catch (e) { print e["message"]; }
        try { let x = 1 / 0; } catch (e) { print e["code"]; }
    )";

    // The final line intentionally exercises catching a native RinError as well as ThrowSignal.
    const std::string expected = "AB\nboom\nE0035\n";
    return runCase(source, expected) ? 0 : 1;
}
