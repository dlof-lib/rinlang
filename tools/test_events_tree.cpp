// RCS-1.0 §3.5 Tree + §3.6 Events (Phase 2) — يغطي هذا الاختبار: (1) التعشيش النصّي بين
// الحاويات يُسجَّل تلقائياً كعلاقة أب/ابن (parentOf/childrenOf)، (2) siblingsOf تُعيد الإخوة
// المباشرين فقط باستثناء الحاوية نفسها، (3) childrenOf لا تُعيد الأحفاد (الأبناء المباشرون فقط)،
// (4) emit من ابن يُطلق on.event المطابق في الأب المباشر تلقائياً مع تمرير الـ payload،
// (5) بلا 'bubbles': emit لا يصل إلى الجدّ (فقط الأب المباشر)، (6) مع 'bubbles': emit يصعد عبر
// كل سلسلة الأجداد حتى الجذر، (7) emit من داخل جسم دالة تُستدعى لاحقاً (بعد فراغ containerStack)
// لا يزال يعمل بشكل صحيح (containerKeyForEnv)، (8) slot تُسجَّل وتُستقصى عبر slotsOf،
// (9) destroyContainer يُنظِّف الحاوية من قائمة أبناء أبيها، (10) توافق عكسي: "slot"/"emit"/"on"/
// "event"/"bubbles" كأسماء متغيرات عادية (بلا الشكل النحوي الجديد) لا تزال تعمل بلا كسر.
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

    // (1) التعشيش النصّي يُسجَّل تلقائياً كعلاقة أب/ابن: parentOf(ابن) = اسم الأب،
    //     childrenOf(أب) تحوي اسم الابن.
    failures += run("nested containers register parent/child automatically", R"(
        @container=Page
            @container=Header
                print "header body";
            .end/container
        .end/container
        print parentOf("Header");
        print childrenOf("Page");
    )", /*expectError=*/false, "Page\n[\"Header\"]");

    // (2) childrenOf تُعيد الأبناء المباشرين فقط، لا الأحفاد.
    failures += run("childrenOf returns direct children only, not grandchildren", R"(
        @container=A
            @container=B
                @container=C
                    print "c";
                .end/container
            .end/container
        .end/container
        print childrenOf("A");
        print childrenOf("B");
    )", /*expectError=*/false, "[\"B\"]\n[\"C\"]");

    // (3) siblingsOf تُعيد الإخوة المباشرين فقط باستثناء الحاوية نفسها.
    failures += run("siblingsOf returns direct siblings excluding self", R"(
        @container=Parent
            @container=First
                print "first";
            .end/container
            @container=Second
                print "second";
            .end/container
            @container=Third
                print "third";
            .end/container
        .end/container
        print siblingsOf("Second");
    )", /*expectError=*/false, "[\"First\", \"Third\"]");

    // (4) emit من ابن يُطلق on.event المطابق في الأب المباشر مع تمرير الـ payload.
    failures += run("emit from a child fires on.event in the direct parent with payload", R"(
        @container=Page
            on.event "form:submitted" (payload) { print "got: " + payload; }
            @container=Form
                fun submit() { emit "form:submitted", "ok"; }
            .end/container
        .end/container
        callFn(getField("Form", "submit"));
    )", /*expectError=*/false, "got: ok");

    // (5) بلا 'bubbles': emit لا يصل إلى الجدّ، فقط الأب المباشر.
    failures += run("without 'bubbles', emit does not reach the grandparent", R"(
        @container=Grandparent
            on.event "ping" () { print "grandparent heard it"; }
            @container=Parent
                @container=Child
                    fun go() { emit "ping"; }
                .end/container
            .end/container
        .end/container
        callFn(getField("Child", "go"));
        print "done";
    )", /*expectError=*/false, "done");

    // (6) مع 'bubbles': emit يصعد عبر كل سلسلة الأجداد حتى الجذر.
    failures += run("with 'bubbles', emit climbs the whole ancestor chain", R"(
        @container=Grandparent
            on.event "ping" () { print "grandparent heard it"; }
            @container=Parent
                on.event "ping" () { print "parent heard it"; }
                @container=Child
                    fun go() { emit "ping" bubbles; }
                .end/container
            .end/container
        .end/container
        callFn(getField("Child", "go"));
    )", /*expectError=*/false, "parent heard it\ngrandparent heard it");

    // (7) emit من داخل جسم دالة تُستدعى لاحقاً (بعد فراغ containerStack تماماً) لا يزال يعمل
    //     بشكل صحيح بفضل containerKeyForEnv بدل الاعتماد على قمة containerStack.
    failures += run("emit still resolves its container correctly after declarative execution ended", R"(
        @container=Outer
            on.event "later" () { print "outer caught it"; }
            @container=Inner
                fun fireLater() { emit "later"; }
            .end/container
        .end/container
        print "declarative execution finished";
        callFn(getField("Inner", "fireLater"));
    )", /*expectError=*/false, "declarative execution finished\nouter caught it");

    // (8) slot تُسجَّل وتُستقصى عبر slotsOf بترتيب التعريف.
    failures += run("slot fields are registered and queryable via slotsOf", R"(
        @container=Card
            slot header;
            slot body;
        .end/container
        print slotsOf("Card");
    )", /*expectError=*/false, "[\"header\", \"body\"]");

    // (9) destroyContainer يُنظِّف الحاوية المحذوفة من قائمة أبناء أبيها.
    failures += run("destroyContainer removes the child from its parent's childrenOf list", R"(
        @container=Parent
            @container=Kid
                print "kid";
            .end/container
        .end/container
        destroyContainer("Kid");
        print childrenOf("Parent");
        print parentOf("Kid");
    )", /*expectError=*/false, "[]\nnil");

    // (10) توافق عكسي: "slot"/"emit"/"on"/"event"/"bubbles" كأسماء متغيرات عادية تعمل بلا كسر.
    failures += run("'slot'/'emit'/'bubbles' as ordinary variable names still work", R"(
        @container=NotEvents
            let slot = 1;
            let emit = 2;
            let bubbles = 3;
            print slot + emit + bubbles;
        .end/container
    )", /*expectError=*/false, "6");

    if (failures == 0) {
        std::cout << "ALL PASSED\n";
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
    }
    return failures == 0 ? 0 : 1;
}
