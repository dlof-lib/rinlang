#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // 1) قاعدة بيانات لاعلاقية: Containers.Group = قاعدة بيانات، container.doc = مجموعة مستندات (collection)
        @Containers.Group=shop_db
            @container.doc=users
                document id="u1" fields={ name: "سارة", age: 28, city: "الرياض" };
                document id="u2" fields={ name: "أحمد", age: 35, city: "جدة" };
                document id="u3" fields={ name: "منى", age: 28, city: "الرياض" };
            .end/container.doc

            @container.doc=orders
                document id="o1" fields={ user: "u1", total: 120 };
                document id="o2" fields={ user: "u2", total: 80 };
            .end/container.doc
        .end/Containers.Group

        print "مجموعات المستندات (collections) داخل قاعدة البيانات shop_db:";
        print groupContainers("shop_db");

        print "كل مستندات users:";
        print allDocs("users");

        print "عدد المستندات في users:";
        print countDocs("users");

        print "بحث عن مستند بمعرّف u2:";
        print findDoc("users", "u2");

        print "استعلام: كل من مدينته الرياض:";
        print queryDocs("users", "city", "الرياض");

        // 2) upsert وتحديث حيّ (runtime) خارج التصريح الوصفي
        print "إدراج مستند جديد (u4) وقت التشغيل:";
        print insertDoc("users", "u4", { name: "خالد", age: 41, city: "الدمام" });
        print "تحديث جزئي (patch) على u1 (تغيير المدينة فقط):";
        print updateDoc("users", "u1", { city: "مكة" });
        print findDoc("users", "u1");

        print "حذف مستند o2 من orders:";
        print deleteDoc("orders", "o2");
        print docIds("orders");

        // 3) نفس المفهوم بشكل مستقل (بلا بادئة container.) عبر @doc
        @doc=logs
            document id="l1" fields={ level: "info", msg: "بدء التشغيل" };
        .end/doc
        print allDocs("logs");
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
