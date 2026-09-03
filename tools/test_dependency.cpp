// RCS-1.0 §3.14 Dependency (Phase 3) — يغطي هذا الاختبار: (1) 'requires' كأول عبارة في جسم حاوية
// ينجح بصمت إن كانت كل الأسماء المطلوبة مسجَّلة مسبقاً (حاويات مُعرَّفة نصّياً قبلها في البرنامج)،
// (2) 'requires' يفشل فوراً (RinError بكود E0041) عند أول اسم غير موجود، فيتوقف تنفيذ جسم الحاوية
// عند تلك النقطة بالضبط (أي عبارة بعدها لا تُنفَّذ)، (3) قائمة أسماء متعددة مفصولة بفواصل تُفحَص
// جميعها بالترتيب، (4) حاويات أُنشئت عبر spawn() تُحسَب أيضاً كاعتماد موجود (containers العامة لا
// تفرّق بين @container النصّية وspawn/create)، (5) 'requires' خارج أي حاوية (على مستوى البرنامج
// الأعلى) يعمل بنفس الآلية (لا علاقة له بوجود حاوية أب محيطة)، (6) توافق عكسي: "requires" كاسم
// متغيّر عادي (بلا الشكل النحوي الجديد: بلا IDENT مباشرة بعدها) لا يزال يعمل بلا كسر.
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

    // (1) requires ينجح بصمت إن كانت كل الحاويات المطلوبة موجودة مسبقاً.
    failures += run("requires succeeds silently when all named containers exist", R"(
        @container=UserSession
            print "session ready";
        .end/container
        @container=Checkout
            requires UserSession;
            print "checkout ready";
        .end/container
    )", /*expectError=*/false, "✅ .end/container (UserSession)\n📦 container = Checkout\ncheckout ready");

    // (2) requires يفشل فوراً عند أول اسم غير موجود، ويتوقف تنفيذ الجسم عند تلك النقطة.
    failures += run("requires fails immediately on the first missing name, halting the body", R"(
        @container=Checkout
            requires Cart;
            print "this should never print";
        .end/container
    )", /*expectError=*/true, "requires: الحاوية المطلوبة 'Cart' غير موجودة");

    // (3) قائمة أسماء متعددة: كلها تُفحَص، والاسم الأول الناقص هو ما يظهر في رسالة الخطأ.
    failures += run("multiple names in one 'requires' are all checked in order", R"(
        @container=Cart
            print "cart ready";
        .end/container
        @container=Checkout
            requires Cart, UserSession;
            print "this should never print either";
        .end/container
    )", /*expectError=*/true, "'UserSession' غير موجودة");

    // (4) حاوية أُنشئت عبر spawn() تُحسَب اعتماداً موجوداً بنفس آلية hasContainer.
    failures += run("a container created via spawn() satisfies 'requires' too", R"(
        spawn("data", "Inventory");
        @container=Checkout
            requires Inventory;
            print "checkout with spawned dependency ready";
        .end/container
    )", /*expectError=*/false, "checkout with spawned dependency ready");

    // (5) requires على مستوى البرنامج الأعلى (بلا أي حاوية محيطة) يعمل بنفس الآلية تماماً.
    failures += run("'requires' works at the top level, outside any container", R"(
        @container=Config
            print "config ready";
        .end/container
        requires Config;
        print "top-level dependency satisfied";
    )", /*expectError=*/false, "top-level dependency satisfied");

    // (6) توافق عكسي: "requires" كاسم متغيّر عادي (بلا IDENT مباشرة بعدها) لا يزال يعمل بلا كسر.
    failures += run("'requires' as an ordinary variable name still works", R"(
        let requires = 42;
        print requires;
    )", /*expectError=*/false, "42");

    if (failures == 0) {
        std::cout << "ALL PASSED\n";
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
    }
    return failures == 0 ? 0 : 1;
}
