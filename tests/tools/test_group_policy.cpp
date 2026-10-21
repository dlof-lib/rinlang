#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

static void run(const std::string& label, const std::string& source, bool expectError) {
    std::cout << "=== " << label << " ===\n";
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        std::cout << out;
        // run() reports RinError diagnostics inline in its returned string (formatted banner
        // beginning with "error[") rather than letting them escape as a C++ exception -- so we
        // must check the output text, not rely on a caught exception, to know whether it failed.
        bool sawError = out.find("error[") != std::string::npos;
        if (sawError == expectError) std::cout << "✅ OK\n";
        else std::cout << (expectError ? "❌ FAIL: expected an error but none was thrown\n" : "❌ FAIL: unexpected error\n");
    } catch (rin::RinError& e) {
        std::cout << "Error at line " << e.line << ": " << e.message << "\n";
        std::cout << (expectError ? "✅ OK (error thrown, as expected)\n" : "❌ FAIL: unexpected error\n");
    }
}

int main() {
    // 1) allow لا تشمل "api" -> استخدام container.api بداخل المجموعة يجب أن يُرفَض فوراً
    run("group deny via missing allow", R"(
        @Containers.Group=restricted
            allow doc;
            @container.doc=notes
                text label = "ok";
            .end/container.doc
            @container.api=external
                route method="GET" path="/x" status=200 body={ ok: true };
            .end/container.api
        .end/Containers.Group
    )", true);

    // 2) allow تشمل كل ما يُستخدَم فعلياً (doc + container [من المجموعة الفرعية المتداخلة]) -> يمر بسلام
    run("group allow covers nested usage", R"(
        @Containers.Group=ok_group
            allow doc;
            allow container;
            @container.doc=notes
                text label = "ok";
            .end/container.doc
            @Containers.Group=nested
                @container.doc=more
                    text label = "also ok";
                .end/container.doc
            .end/Containers.Group
        .end/Containers.Group
        print "أعضاء ok_group:";
        print groupContainers("ok_group");
    )", false);

    // 3) مجموعة بلا أي سياسة (hasPolicy=false) -> تسلك كما كانت دوماً (permissive)، بلا أي رفض
    run("group without policy stays permissive", R"(
        @Containers.Group=legacy
            @container.api=whatever
                route method="GET" path="/z" status=200 body={ ok: true };
            .end/container.api
        .end/Containers.Group
        print "ok";
    )", false);

    return 0;
}
