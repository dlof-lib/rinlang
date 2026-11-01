#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        @container.doc=users
            document id="u1" fields={ name: "سارة", age: 28, city: "الرياض" };
            document id="u2" fields={ name: "أحمد", age: 35, city: "جدة" };
        .end/container.doc

        @container.doc=orders
            document id="o1" fields={ user: "u1", total: 120 };
        .end/container.doc

        print "== schema ==";
        print defineSchema("users", { name: "string", age: "number" });
        print validateDoc("users", { name: "منى" });
        print insertDoc("users", "u3", { name: "منى", age: 28, city: "الرياض" });

        print "== index ==";
        print createIndex("users", "city");
        print findByIndex("users", "city", "الرياض");
        print listIndexes("users");

        print "== relation ==";
        print defineRelation("userOrders", "users", "id_ref", "orders", "user");
        insertDoc("users", "u1", { name: "سارة", age: 28, city: "الرياض", id_ref: "u1" });
        print relatedDocs("userOrders", "u1");

        print "== transaction ==";
        print beginTransaction();
        print insertDoc("users", "u4", { name: "خالد", age: 41 });
        print countDocs("users");
        print rollbackTransaction();
        print countDocs("users");

        print "== migration ==";
        fun addField() {
            insertDoc("users", "u5", { name: "test-migration", age: 1 });
        }
        fun removeField() {
            deleteDoc("users", "u5");
        }
        print defineMigration("m1", addField, removeField);
        print runMigration("m1");
        print runMigration("m1");
        print appliedMigrations();
        print rollbackMigration("m1");
        print countDocs("users");

        print "== cache ==";
        print cacheSet("greeting", "hello");
        print cacheGet("greeting");
        print cacheSet("temp", 42, 0);
        print cacheHas("temp");
        print cacheKeys();

        print "== watch/subscribe ==";
        fun onUserChange(id, doc, event) {
            print "watcher: " + event + " -> " + id;
        }
        watch("users", onUserChange);
        insertDoc("users", "u6", { name: "watched", age: 5 });
        deleteDoc("users", "u6");

        fun onNotify(payload) {
            print "sub got: " + payload;
        }
        subscribe("news", onNotify);
        print publish("news", "hello subscribers");
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
