// RIN CONTAINER SQL (RCSQL) — نظام استعلام مصغّر خاص بلغة Rin فوق مجموعات مستندات @container.doc/
// @doc القائمة أصلاً (docStore)، بخمسة رموز تركيبية فقط: / : & () # (انظر rin_container_sql.h).
// يغطي هذا الاختبار: (1) استعلام خام مباشر عبر قناع '#'، (2) استعلام خام عبر مسار حاوية مباشر
// بلا قناع، (3) استعلام عبر مسار Group/حاوية متداخل، (4) عدة شروط AND بـ '&' بما فيها حقل متداخل
// عبر '/'، (5) عمليات المقارنة الرقمية gt/gte/lt/lte، (6) has() على مصفوفة/map، (7) like() كمطابقة
// substring، (8) sqlOne/sqlCount، (9) تعريف/استدعاء @sql مُسمّاة عبر قناعها (mask)، (10) حاوية/قناع
// غير موجود -> نتيجة فارغة بصمت (لا خطأ)، (11) رمز خارج القائمة المسموحة -> خطأ E0042 تركيبي.
//
// ملاحظة على الترميز: مصدر Rin هنا مكتوب داخل سلاسل C++ خام R"RIN(...)RIN" بمحدِّد مخصّص "RIN"
// (بدل الافتراضي الفارغ) تحديداً لأن نص RCSQL نفسه ينتهي بـ ...eq(admin)"); أي يحوي التتابع الحرفي
// )" الذي يُنهي أي سلسلة خام R"(...)" افتراضية قبل أوانه.
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

    static const char* seedDocs = R"RIN(
        @container.doc=users
            mask="app.users";
            document id="u1" fields={ name: "Ali", role: "admin", age: 25, address: { city: "Cairo" }, tags: ["vip", "early"] };
            document id="u2" fields={ name: "Sara", role: "user", age: 30, address: { city: "Giza" }, tags: ["new"] };
            document id="u3" fields={ name: "Omar", role: "admin", age: 17, address: { city: "Cairo" }, tags: [] };
        .end/container.doc
    )RIN";

    // (1) استعلام خام عبر قناع '#' مع شرط واحد.
    failures += run("raw query via mask (#) -- single predicate", std::string(seedDocs) + R"RIN(
        print sql("#app.users & role:eq(admin)");
    )RIN", false, "\"name\": \"Ali\"");

    // (2) استعلام خام عبر اسم حاوية مباشر (بلا قناع).
    failures += run("raw query via direct container name (no mask)", std::string(seedDocs) + R"RIN(
        print sql("users & role:eq(user)");
    )RIN", false, "\"name\": \"Sara\"");

    // (3) مسار Group/حاوية متداخل بـ '/'.
    failures += run("nested Group/container path target", std::string(R"RIN(
        @Containers.Group=Store
            @container.doc=users
                document id="u1" fields={ name: "Ali", role: "admin" };
            .end/container.doc
        .end/Containers.Group
        print sql("Store/users & role:eq(admin)");
    )RIN"), false, "\"name\": \"Ali\"");

    // (4) عدة شروط AND متصلة بـ '&' بما فيها حقل متداخل عبر '/'.
    failures += run("multiple AND predicates incl. nested field", std::string(seedDocs) + R"RIN(
        print sql("#app.users & role:eq(admin) & address/city:eq(Cairo) & age:gte(18)");
    )RIN", false, "\"name\": \"Ali\"");

    // (5) عمليات مقارنة رقمية: gt/gte/lt/lte.
    failures += run("numeric comparisons gt/gte/lt/lte", std::string(seedDocs) + R"RIN(
        print sql("#app.users & age:gt(20)");
        print sql("#app.users & age:lte(17)");
    )RIN", false, "\"name\": \"Ali\"");

    // (6) has() على مصفوفة (tags) وعلى map (address).
    failures += run("has() on array and on map", std::string(seedDocs) + R"RIN(
        print sql("#app.users & tags:has(vip)");
        print sql("#app.users & address:has(city)");
    )RIN", false, "\"name\": \"Ali\"");

    // (7) like() كمطابقة substring غير حسّاسة لحالة الأحرف.
    failures += run("like() substring match", std::string(seedDocs) + R"RIN(
        print sql("#app.users & name:like(ar)");
    )RIN", false, "\"name\": \"Sara\"");

    // (8) sqlOne يعيد أول تطابق فقط؛ sqlCount يعيد العدد فقط.
    failures += run("sqlOne / sqlCount", std::string(seedDocs) + R"RIN(
        print sqlOne("#app.users & role:eq(admin)");
        print sqlCount("#app.users & role:eq(admin)");
    )RIN", false, "\"name\": \"Ali\"");

    // (9) @sql مُسمّاة تُستدعى لاحقاً بقناعها فقط (بلا تكرار النص الخام).
    failures += run("named @sql view invoked by its mask", std::string(seedDocs) + R"RIN(
        @sql=activeAdmins
            mask="q.activeAdmins";
            text query = "#app.users & role:eq(admin) & age:gte(18)";
        .end/sql
        print sql("q.activeAdmins");
        print sqlCount("q.activeAdmins");
    )RIN", false, "\"name\": \"Ali\"");

    // (10) قناع/حاوية غير موجودة -> مصفوفة فارغة بصمت (لا خطأ) -- بنفس سلوك بقية دوال docStore.
    failures += run("unknown mask/container -> empty result, no error", R"RIN(
        print sql("#no.such.mask & x:eq(1)");
        print sql("noSuchContainer & x:eq(1)");
    )RIN", false, "[]");

    // (11) رمز خارج القائمة المسموحة (/ : & () #) -> خطأ تركيبي E0042.
    failures += run("disallowed symbol raises E0042 syntax error", R"RIN(
        print sql("users|role:eq(admin)");
    )RIN", true, "E0042");

    if (failures == 0) {
        std::cout << "ALL PASSED\n";
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
    }
    return failures == 0 ? 0 : 1;
}
