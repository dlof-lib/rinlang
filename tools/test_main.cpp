#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        fun fib(n) {
            if (n < 2) { return n; }
            return fib(n - 1) + fib(n - 2);
        }

        let i = 0;
        while (i < 8) {
            print "fib(" + i + ") = " + fib(i);
            i = i + 1;
        }

        let msg = "Hello, " + "Rin!";
        print msg;

        if (5 > 3 and 2 < 4) {
            print "logic works";
        } else {
            print "logic broken";
        }
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
