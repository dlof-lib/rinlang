#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        // مثال مطابق للرسم: Input Data -> Step1 (Transformation) -> Step2 (Aggregation) -> Final Output
        @container.pipe=sales_pipeline
            let raw = [10, 20, 30, 40, 50];

            fun transform(data) {
                return normalize(data);
            }

            fun aggregate(data) {
                return mean(data);
            }

            let final_output = raw |> transform() |> aggregate();
            print "Input Data:";
            print raw;
            print "Final Output (mean of normalized data):";
            print final_output;

            print "sum:";       print sum(raw);
            print "mean:";      print mean(raw);
            print "median:";    print median(raw);
            print "variance:";  print variance(raw);
            print "stddev:";    print stddev(raw);
            print "mode:";      print mode([1, 2, 2, 3]);

            // خط أنابيب طويل: تحويل ثم تحويل آخر ثم تجميع
            let scaled_then_mean = raw |> scale(2) |> shift(1) |> mean();
            print "scale(x2) -> shift(+1) -> mean:";
            print scaled_then_mean;
        .end/container.pipe
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
