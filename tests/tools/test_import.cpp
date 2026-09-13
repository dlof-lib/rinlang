// اختبار @import: المكتبات المدمجة (embedded) الخمس + الدمج المباشر + الاستيراد باسم مستعار (alias)
// + تجاهل الاستيراد المكرر. شغّله عبر:
//
//   g++ -std=c++17 -o rin_import_test tools/test_import.cpp app/src/main/cpp/rin_lexer.cpp
//     app/src/main/cpp/rin_parser.cpp app/src/main/cpp/rin_interpreter.cpp -I app/src/main/cpp
//   ./rin_import_test
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // ---- دمج مباشر لكل المكتبات الخمس المدمجة ----
        @import "lib/math.og.rin";
        @import "lib/strings.og.rin";
        @import "lib/data.og.rin";
        @import "lib/validate.og.rin";
        @import "lib/functional.og.rin";

        print factorial(5);                 // 120
        print isPrime(17);                  // true
        print capitalize("rin");            // Rin
        print unique([1, 2, 2, 3]);         // [1, 2, 3]
        print isEmail("a@b.com");           // true
        fun double(x) { return x * 2; }
        print mapArr([1, 2, 3], double);    // [2, 4, 6]
        print filterArr([1, 2, 3, 4], isEven); // [2, 4]

        // ---- إعادة استيراد نفس المكتبة: يجب أن تُتجاهَل بلا أخطاء ولا تعريف مكرر ----
        @import "lib/math.og.rin";
        print factorial(6); // 720 (ما زالت تعمل بشكل طبيعي)

        // ---- استيراد باسم مستعار: يُسجَّل كحاوية، فيمكن ربطها بـ tying كأي حاوية أخرى ----
        @import "lib/math.og.rin" as mathx;
        @container=viewer
            tying with=mathx;
            print gcd(48, 18); // 6
        .end/container
    )";

    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::cout << interp.run(statements);
    } catch (rin::RinError& e) {
        std::cerr << "[Error line " << e.line << "]: " << e.message << "\n";
        return 1;
    }
    return 0;
}
