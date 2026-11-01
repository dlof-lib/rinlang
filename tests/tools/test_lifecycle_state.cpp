// RCS-1.0 §3.2 Lifecycle + §3.3 State (Phase 1) — يغطي هذا الاختبار: (1) on init()/on mount()
// يُطلَقان تلقائياً بالترتيب بعد انتهاء جسم الحاوية، (2) state تُقرأ/تُهيَّأ مثل let تماماً،
// (3) إسناد لحقل state يُطلق on update(prevState) تلقائياً بقيمته السابقة، (4) إسناد لمتغيّر
// let عادي (غير state) لا يُطلق أي شيء، (5) destroyContainer يستدعي on destroy() قبل الحذف
// الفعلي (وحقول state لا تزال قابلة للقراءة أثناء تنفيذه)، (6) on error(err) يلتقط استثناءً وقع
// أثناء تنفيذ خُطّاف آخر لنفس الحاوية، (7) توافق عكسي: "on"/"state" كأسماء متغيرات عادية (بلا
// الشكل النحوي الجديد) لا تزالان تعملان بلا أي كسر.
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

static int run(const char* label, const std::string& source, bool expectError,
                const std::string& expectSubstring = "") {
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
        if (!expectSubstring.empty() && out.find(expectSubstring) == std::string::npos) {
            std::cout << "[FAIL] " << label << ": expected output to contain: " << expectSubstring << "\n";
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

    // (1) init() ثم mount() تلقائياً، بالترتيب، بعد نهاية جسم الحاوية.
    failures += run("on init() then on mount() fire automatically in order", R"(
        @container=Widget
            on init() { print "init"; }
            on mount() { print "mount"; }
            print "body";
        .end/container
    )", /*expectError=*/false, "body\ninit\nmount");

    // (2) state تُقرأ وتُهيَّأ كأي متغيّر عادي.
    failures += run("state field reads like a normal variable", R"(
        @container=Counter
            state count = 0;
            print count;
        .end/container
    )", /*expectError=*/false, "0");

    // (3) إسناد لحقل state يُطلق on update(prevState) تلقائياً، بقيمته السابقة داخل الخريطة.
    failures += run("assigning to a state field fires on update(prevState)", R"(
        @container=Counter
            state count = 0;
            on update(prevState) { print "prev was " + prevState["count"]; }

            fun increment() { count = count + 1; }
        .end/container
        callFn(getField("Counter", "increment"));
        print getField("Counter", "count");
    )", /*expectError=*/false, "prev was 0\n1");

    // (4) إسناد لمتغيّر let عادي (غير state) لا يُطلق أي خُطّاف update، حتى لو كان معرَّفاً.
    failures += run("assigning to a plain 'let' does not fire on update", R"(
        @container=Plain
            let x = 0;
            on update(prevState) { print "should not print"; }
            fun bump() { x = x + 1; }
        .end/container
        callFn(getField("Plain", "bump"));
        print getField("Plain", "x");
    )", /*expectError=*/false, "1");

    // (5) destroyContainer يستدعي on destroy() قبل الحذف الفعلي؛ الحقول لا تزال قابلة للقراءة بداخله.
    failures += run("destroyContainer fires on destroy() before removal", R"(
        @container=Session
            state active = true;
            on destroy() { print "destroying, active=" + active; }
        .end/container
        destroyContainer("Session");
        print hasContainer("Session");
    )", /*expectError=*/false, "destroying, active=true\nfalse");

    // (6) on error(err) يلتقط استثناءً وقع أثناء تنفيذ خُطّاف init() لنفس الحاوية.
    failures += run("on error(err) catches an exception from another hook", R"(
        @container=Risky
            on init() { let bad = undefinedThing + 1; }
            on error(err) { print "caught: " + err["message"]; }
        .end/container
    )", /*expectError=*/false, "caught:");

    // (7) توافق عكسي: "on"/"state" كأسماء متغيرات عادية (بلا الشكل النحوي الجديد) تعملان بلا كسر.
    failures += run("'on' and 'state' as ordinary variable names still work", R"(
        @container=NotHooks
            let on = 1;
            let state = 2;
            print on + state;
        .end/container
    )", /*expectError=*/false, "3");

    if (failures == 0) {
        std::cout << "ALL PASSED\n";
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
    }
    return failures == 0 ? 0 : 1;
}
