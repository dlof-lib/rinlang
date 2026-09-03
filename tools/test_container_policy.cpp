// RCS-1.0 §3.13 Security (Phase 0) — تعميم policy_block (use/need/allow/deny/strict/
// version/description) من @make.(name) الحصرية إلى أي @container عادية.
// يغطي هذا الاختبار: (1) حاوية بلا سياسة تسلك تماماً كاليوم (permissive)، (2) allow
// كقائمة بيضاء صريحة ترفض قدرة غير مصرَّح بها، (3) deny يمنع قدرة محدَّدة، (4) need
// يطلب قدرة إلزامية، (5) strict يمنع أي قدرة غير مُعلَنة بـ use، (6) توافق عكسي: كلمة
// "use" كاسم متغيّر عادي (بلا الشكل النحوي use <capability>;) لا تزال تعمل بلا أي كسر.
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

static int run(const char* label, const std::string& source, bool expectError) {
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        bool hadError = interp.hadError();
        std::cout << "== " << label << " ==\n" << out << "\n";
        if (hadError != expectError) {
            std::cout << "[FAIL] " << label << ": expected error=" << expectError
                       << " but got error=" << hadError << "\n";
            return 1;
        }
        std::cout << "[OK] " << label << "\n\n";
        return 0;
    } catch (rin::RinError& e) {
        std::cout << "== " << label << " ==\n";
        std::cout << "Parse/Lex error at line " << e.line << ": " << e.message << "\n";
        if (!expectError) {
            std::cout << "[FAIL] " << label << ": unexpected parse/lex error\n";
            return 1;
        }
        std::cout << "[OK] " << label << "\n\n";
        return 0;
    }
}

int main() {
    int failures = 0;

    // (1) لا سياسة إطلاقاً -> permissive تماماً كاليوم، مهما كانت القدرات المستخدمة.
    failures += run("no policy at all (backward compatible)", R"(
        @container=Plain
            fun greet(name) { return "hi " + name; }
            let i = 0;
            while (i < 2) { i = i + 1; }
            print greet("world");
        .end/container
    )", /*expectError=*/false);

    // (2) allow كقائمة بيضاء: هذه الحاوية تستخدم "loop" (while) لكنها لم تسمح إلا بـ function.
    failures += run("allow whitelist rejects undeclared capability", R"(
        @container=Restricted
            allow function;
            fun greet(name) { return "hi " + name; }
            let i = 0;
            while (i < 2) { i = i + 1; }
        .end/container
    )", /*expectError=*/true);

    // (2b) نفس القائمة البيضاء لكن بلا استخدام أي قدرة محظورة -> يجب أن تنجح.
    failures += run("allow whitelist accepts declared-only capabilities", R"(
        @container=RestrictedOk
            allow function;
            allow io;
            allow return;
            fun greet(name) { return "hi " + name; }
            print greet("ok");
        .end/container
    )", /*expectError=*/false);

    // (3) deny يمنع قدرة "loop" صراحة حتى لو لم تُذكر allow إطلاقاً.
    failures += run("deny blocks a specific capability", R"(
        @container=NoLoops
            deny loop;
            let i = 0;
            while (i < 1) { i = i + 1; }
        .end/container
    )", /*expectError=*/true);

    // (4) need يطلب قدرة إلزامية غير مستخدمة فعلياً -> خطأ.
    failures += run("need requires a capability that is missing", R"(
        @container=NeedsFunction
            need function;
            let x = 1;
        .end/container
    )", /*expectError=*/true);

    // (5) strict: أي قدرة غير مُعلَنة بـ use تُرفض، حتى لو لم تكن denied صراحة.
    failures += run("strict rejects any undeclared capability", R"(
        @container=StrictOne
            strict;
            use function;
            fun greet(name) { return "hi " + name; }
            let i = 0;
            while (i < 1) { i = i + 1; }
        .end/container
    )", /*expectError=*/true);

    // (6) توافق عكسي: "use" كاسم متغيّر عادي (لا يطابق شكل "use <capability>;") يجب ألا يُفسَّر
    //     كتوجيه سياسة إطلاقاً -> يبقى برنامجاً عادياً صحيحاً بلا أي سياسة مفعّلة.
    failures += run("'use' as an ordinary variable name still works", R"(
        @container=NotAPolicyWord
            let use = 5;
            print use;
            let i = 0;
            while (i < 1) { i = i + 1; }
        .end/container
    )", /*expectError=*/false);

    // (7) @make.(name) القديمة (Make Unit الأصلية) يجب أن تستمر بالعمل بلا أي تغيير في سلوكها.
    failures += run("legacy @make.(name) Make Unit still enforces its own policy", R"(
        @make.(LegacyUnit)
            kind app;
            allow function;
            fun greet(name) { return "hi " + name; }
            let i = 0;
            while (i < 1) { i = i + 1; }
        .end/make=LegacyUnit
    )", /*expectError=*/true);

    if (failures == 0) {
        std::cout << "ALL PASSED\n";
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
    }
    return failures == 0 ? 0 : 1;
}
