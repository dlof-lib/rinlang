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

    static const char* seedDocs2 = R"RIN(
        @container.doc=members
            mask="app.members";
            document id="m1" fields={ name: "Ali", role: "admin", age: 25, active: true };
            document id="m2" fields={ name: "Sara", role: "user", age: 30, active: true };
            document id="m3" fields={ name: "Omar", role: "owner", age: 40, active: true };
            document id="m4" fields={ name: "Lina", role: "user", age: 22, active: false };
        .end/container.doc
    )RIN";

    // (12) مجموعة or(...) -- تطابق role=admin أو role=owner.
    failures += run("or(...) group matches either predicate", std::string(seedDocs2) + R"RIN(
        print sql("#app.members & or(role:eq(admin) & role:eq(owner))");
    )RIN", false, "\"name\": \"Omar\"");

    // (13) order:desc + limit -- أكبر عمرين فقط، الأكبر أولاً.
    failures += run("order:desc + limit", std::string(seedDocs2) + R"RIN(
        print sql("#app.members & order:desc(age) & limit:eq(1)");
    )RIN", false, "\"name\": \"Omar\"");

    // (14) order:asc + offset + limit -- تصفّح صفحة ثانية.
    failures += run("order:asc + offset + limit (pagination)", std::string(seedDocs2) + R"RIN(
        print sql("#app.members & order:asc(age) & offset:eq(1) & limit:eq(1)");
    )RIN", false, "\"name\": \"Ali\"");

    // (15) select:has -- إسقاط حقل واحد فقط (+ _id دوماً).
    failures += run("select:has(...) projects a single field", std::string(seedDocs2) + R"RIN(
        print sql("#app.members & role:eq(user) & select:has(name)");
    )RIN", false, "{\"_id\": \"m2\", \"name\": \"Sara\"}");

    // (16) distinct:eq -- أول ظهور فقط لكل قيمة role.
    failures += run("distinct:eq(...) dedupes by field", std::string(seedDocs2) + R"RIN(
        print sqlCount("#app.members & distinct:eq(role)");
    )RIN", false, "3");

    // (17) ieq / starts / ends / exists / missing.
    failures += run("ieq/starts/ends/exists/missing ops", std::string(seedDocs2) + R"RIN(
        print sql("#app.members & name:ieq(ali)");
        print sql("#app.members & name:starts(Sa)");
        print sql("#app.members & name:ends(mar)");
        print sqlCount("#app.members & active:exists()");
        print sqlCount("#app.members & nickname:missing()");
    )RIN", false, "\"name\": \"Ali\"");

    // (18) sqlExists / sqlIds / sqlPluck.
    failures += run("sqlExists / sqlIds / sqlPluck", std::string(seedDocs2) + R"RIN(
        print sqlExists("#app.members & role:eq(admin)");
        print sqlExists("#app.members & role:eq(ghost)");
        print sqlIds("#app.members & active:eq(true)");
        print sqlPluck("#app.members & active:eq(true)", "name");
    )RIN", false, "true");

    // (19) sqlSum / sqlAvg / sqlMin / sqlMax.
    failures += run("sqlSum / sqlAvg / sqlMin / sqlMax", std::string(seedDocs2) + R"RIN(
        print sqlSum("#app.members", "age");
        print sqlAvg("#app.members", "age");
        print sqlMin("#app.members", "age");
        print sqlMax("#app.members", "age");
    )RIN", false, "117");

    // (20) sqlUpdate -- يضبط حقلاً واحداً على كل المطابقات، بشكل ذرّي (كل شيء أو لا شيء عند خرق مخطط).
    failures += run("sqlUpdate sets a field on all matches", std::string(seedDocs2) + R"RIN(
        let n = sqlUpdate("#app.members & role:eq(user)", "tier", "gold");
        print n;
        print sql("#app.members & tier:eq(gold)");
    )RIN", false, "\"name\": \"Sara\"");

    // (21) sqlDelete -- يحذف كل المطابقات فعلياً ويعيد العدد المحذوف.
    failures += run("sqlDelete removes all matches", std::string(seedDocs2) + R"RIN(
        let d = sqlDelete("#app.members & active:eq(false)");
        print d;
        print sqlCount("#app.members");
    )RIN", false, "3");

    // (22) مُعدِّل بشكل غير صالح (limit بوسيط غير رقمي) -> خطأ تركيبي E0042 واضح، لا فلترة صامتة خاطئة.
    failures += run("invalid modifier argument raises E0042", R"RIN(
        print sql("users & limit:eq(abc)");
    )RIN", true, "E0042");

    // (23) not(...) -- نفي شرط واحد.
    failures += run("not(...) negates a single predicate", std::string(seedDocs2) + R"RIN(
        print sqlCount("#app.members & not(role:eq(user))");
    )RIN", false, "2");

    // (24) تركيب منطقي متداخل: (A AND B) OR (C AND D) عبر or(and(...) & and(...)).
    failures += run("nested and(...)/or(...) composition", std::string(seedDocs2) + R"RIN(
        print sql("#app.members & or(and(role:eq(admin) & active:eq(true)) & and(role:eq(owner) & active:eq(true)))");
    )RIN", false, "\"name\": \"Omar\"");

    // (25) تفريد مركّب: تكرار distinct:eq(...) يبني مفتاح تفريد من عدّة حقول معاً.
    failures += run("composite distinct:eq(...) x2", std::string(seedDocs2) + R"RIN(
        print sqlCount("#app.members & distinct:eq(role)");
        print sqlCount("#app.members & distinct:eq(role) & distinct:eq(active)");
    )RIN", false, "3");

    // (26) sqlValidate -- لا يرمي أبداً، يعيد {ok,...} حتى مع نص خاطئ تماماً.
    failures += run("sqlValidate never throws, reports ok/error", R"RIN(
        print sqlValidate("users & role:eq(admin)");
        print sqlValidate("users|bad");
    )RIN", false, "\"ok\": true");

    // (27) sqlExplain -- يبني شجرة تشخيصية للاستعلام المُحلَّل (هدف/شروط/فرز/حد...).
    failures += run("sqlExplain returns a parsed-query breakdown", std::string(seedDocs2) + R"RIN(
        print sqlExplain("#app.members & role:eq(user) & order:desc(age) & limit:eq(2)");
    )RIN", false, "\"container\": \"members\"");

    // (28) sqlGroupBy -- تجميع حسب حقل، مع عدد ومعرّفات كل مجموعة.
    failures += run("sqlGroupBy groups with count + ids", std::string(seedDocs2) + R"RIN(
        print sqlGroupBy("#app.members", "role");
    )RIN", false, "\"key\": \"user\"");

    // (29) sqlGroupSum -- تجميع مع مجموع رقمي لكل مجموعة.
    failures += run("sqlGroupSum groups with numeric sum", std::string(seedDocs2) + R"RIN(
        print sqlGroupSum("#app.members", "role", "age");
    )RIN", false, "\"sum\"");

    if (failures == 0) {
        std::cout << "ALL PASSED\n";
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
    }
    return failures == 0 ? 0 : 1;
}
