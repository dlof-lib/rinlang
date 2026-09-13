#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // مصفوفات
        let arr = [1, 2, 3];
        print arr;
        print arr[0];
        arr[1] = 20;
        print arr;
        push(arr, 4);
        print arr;
        print len(arr);
        print pop(arr);
        print arr;
        print sort([3, 1, 2]);

        // قواميس
        let m = {name: "Rin", age: 2};
        print m;
        print m["name"];
        m["age"] = 3;
        m["lang"] = "ar";
        print m;
        print keys(m);
        print values(m);
        print has(m, "name");
        print has(m, "missing");
        remove(m, "lang");
        print m;

        // دوال رياضية
        print sqrt(16);
        print pow(2, 10);
        print abs(-5);
        print floor(3.7);
        print ceil(3.2);
        print min(3, 7);
        print max(3, 7);
        print round(3.5);

        // معالجة نصوص
        let s = "  Hello, Rin World  ";
        print trim(s);
        print upper("hello");
        print lower("WORLD");
        print substr("Hello, Rin!", 7, 3);
        print split("a,b,c", ",");
        print join(["a", "b", "c"], "-");
        print indexOf("hello world", "world");
        print replace("foo bar foo", "foo", "baz");
        print contains("hello", "ell");
        print contains([1,2,3], 2);
        print charAt("hello", 1);
        print toString(42);
        print toNumber("3.14");

        // Boolean: تحويل صريح وفحص نوع
        print toBool("true");
        print toBool("FALSE");
        print toBool(0);
        print toBool(5);
        print toBool(nil);
        print isBool(true);
        print isBool(1);
        print isBool(toBool("true"));

        // Boolean: عمليات منطقية أساسية (موجودة أصلاً في اللغة)
        print true and false;
        print true or false;
        print !true;
        print 3 > 2;
        print 3 == 3;

        // فهرسة النص
        print "abc"[1];

        // تعادل تركيبي
        print [1,2,3] == [1,2,3];
        print {a: 1} == {a: 1};
        print [1,2] == [1,3];

        // دالة تأخذ مصفوفة وتُرجع مجموع عناصرها
        fun sum(a) {
            let total = 0;
            let i = 0;
            while (i < len(a)) {
                total = total + a[i];
                i = i + 1;
            }
            return total;
        }
        print sum([10, 20, 30]);
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
