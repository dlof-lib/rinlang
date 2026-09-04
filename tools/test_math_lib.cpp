// اختبار lib/math.og.rin بعد توسيعها الاحترافي (نظرية أعداد/توافيقيات/مثلثات
// عبر سلاسل تايلور + اختزال مجال/أسّية-لوغاريتمات/متجهات/Easing/إحصاء إضافي/
// عشوائية). كل الاختبارات تُنفَّذ داخل Rin نفسها عبر دالة assertEq مساعدة
// تطبع "PASS <name>" أو "FAIL <name>: expected=... got=..."، ثم يتحقّق هذا
// الملف من غياب أي "FAIL" في المخرجات (بلا حاجة لإعادة تطبيق منطق المطابقة
// في C++). شغّله عبر:
//
//   g++ -std=c++17 -o rin_math_test tools/test_math_lib.cpp \
//     app/src/main/cpp/rin_lexer.cpp app/src/main/cpp/rin_parser.cpp \
//     app/src/main/cpp/rin_interpreter.cpp app/src/main/cpp/rin_make.cpp \
//     app/src/main/cpp/rin_diag.cpp app/src/main/cpp/clc/*.cpp \
//     app/src/main/cpp/http/*.cpp app/src/main/cpp/loader_ui/*.cpp \
//     -I app/src/main/cpp -lz
//   ./rin_math_test
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"RINSRC(
        @import "lib/math.og.rin";

        // مساعد اختبار: مقارنة تقريبية (للنتائج العشرية) أو تامة (للمصفوفات/المنطقية)
        fun assertClose(name, expected, actual) {
            if (abs(expected - actual) < 0.00001) {
                print "PASS " + name;
            } else {
                print "FAIL " + name + ": expected=" + toString(expected) + " got=" + toString(actual);
            }
        }
        fun assertEq(name, expected, actual) {
            if (expected == actual) {
                print "PASS " + name;
            } else {
                print "FAIL " + name + ": expected=" + toString(expected) + " got=" + toString(actual);
            }
        }

        // ---- مثلثات ----
        assertClose("sin(PI/6)", 0.5, sin(PI / 6));
        assertClose("sin(PI/2)", 1, sin(PI / 2));
        assertClose("cos(0)", 1, cos(0));
        assertClose("cos(PI)", -1, cos(PI));
        assertClose("tan(PI/4)", 1, tan(PI / 4));
        assertClose("atan(1)", PI / 4, atan(1));
        assertClose("atan2(1,1)", PI / 4, atan2(1, 1));
        assertClose("asin(0.5)", PI / 6, asin(0.5));
        assertClose("acos(0.5)", PI / 3, acos(0.5));
        assertClose("sin(-1000)", -0.8268795405320025, sin(-1000));
        assertClose("atan(1000000)", 1.5707953267948966, atan(1000000));

        // ---- أسّية ولوغاريتمات ----
        assertClose("exp(1)", E, exp(1));
        assertClose("exp(20)", 485165195.4097903, exp(20));
        assertClose("exp(-20)", 0.000000002061153622438558, exp(-20));
        assertClose("ln(E)", 1, ln(E));
        assertClose("ln(1000000)", 13.815510557964274, ln(1000000));
        assertClose("log2(8)", 3, log2(8));
        assertClose("log10(1000)", 3, log10(1000));
        assertClose("logBase(27,3)", 3, logBase(27, 3));

        // ---- دوال زائدية ----
        assertClose("sinh(0)", 0, sinh(0));
        assertClose("cosh(0)", 1, cosh(0));
        assertClose("tanh(0)", 0, tanh(0));

        // ---- نظرية أعداد وتوافيقيات ----
        assertEq("primeFactors(360)", "[2, 2, 2, 3, 3, 5]", toString(primeFactors(360)));
        assertEq("divisors(28)", "[1, 2, 4, 7, 14, 28]", toString(divisors(28)));
        assertEq("isPerfectSquare(49)", true, isPerfectSquare(49));
        assertEq("isPerfectSquare(50)", false, isPerfectSquare(50));
        assertEq("isPerfectCube(27)", true, isPerfectCube(27));
        assertEq("modPow(4,13,497)", 445, modPow(4, 13, 497));
        assertEq("nextPrime(14)", 17, nextPrime(14));
        assertEq("digitSum(12345)", 15, digitSum(12345));
        assertEq("reverseDigits(12345)", 54321, reverseDigits(12345));
        assertEq("isPalindromeNumber(12321)", true, isPalindromeNumber(12321));
        assertEq("combinationsCount(10,3)", 120, combinationsCount(10, 3));
        assertEq("permutationsCount(10,3)", 720, permutationsCount(10, 3));
        assertEq("binomialCoefficient(52,5)", 2598960, binomialCoefficient(52, 5));
        assertEq("catalanNumber(5)", 42, catalanNumber(5));
        assertEq("isCoprime(8,15)", true, isCoprime(8, 15));
        let eg = extendedGcd(240, 46);
        assertEq("extendedGcd(240,46) identity", eg[0], 240 * eg[1] + 46 * eg[2]);

        // ---- توافقية: الدوال القديمة الأساسية ما زالت تعمل بلا تغيير ----
        assertEq("factorial(5)", 120, factorial(5));
        assertEq("gcd(48,18)", 6, gcd(48, 18));
        assertEq("lcm(4,6)", 12, lcm(4, 6));
        assertEq("isPrime(17)", true, isPrime(17));
        assertEq("fibonacci(10)", 55, fibonacci(10));
        assertEq("gcdArr([48,60,36])", 12, gcdArr([48, 60, 36]));
        assertEq("lcmArr([4,6,10])", 60, lcmArr([4, 6, 10]));

        // ---- دوال مساعدة عامة ----
        assertEq("clampNum(15,0,10)", 10, clampNum(15, 0, 10));
        assertClose("invLerp(0,10,2.5)", 0.25, invLerp(0, 10, 2.5));
        assertClose("remap(5,0,10,0,100)", 50, remap(5, 0, 10, 0, 100));
        assertClose("wrap(370,0,360)", 10, wrap(370, 0, 360));
        assertEq("trunc(-3.7)", -3, trunc(-3.7));
        assertClose("fract(3.75)", 0.75, fract(3.75));
        assertEq("safeDiv(1,0,-1)", -1, safeDiv(1, 0, -1));
        assertEq("approxEqual(0.1+0.2,0.3)", true, approxEqual(0.1 + 0.2, 0.3));
        assertClose("moveToward(0,10,3)", 3, moveToward(0, 10, 3));
        assertClose("moveToward(8,10,3)", 10, moveToward(8, 10, 3));

        // ---- متجهات ----
        assertClose("vec2Length([3,4])", 5, vec2Length([3, 4]));
        assertEq("vec3Cross", "[0, 0, 1]", toString(vec3Cross([1, 0, 0], [0, 1, 0])));
        assertClose("vec2Angle([1,1])", PI / 4, vec2Angle([1, 1]));
        assertClose("vec2Distance", 5, vec2Distance([0, 0], [3, 4]));
        let n3 = vec3Normalize([1, 1, 1]);
        assertClose("vec3Normalize length", 1, vec3Length(n3));

        // ---- Easing ----
        assertClose("easeInOutCubic(0.5)", 0.5, easeInOutCubic(0.5));
        assertClose("smoothstep(0.5)", 0.5, smoothstep(0.5));
        assertClose("easeOutBounce(1)", 1, easeOutBounce(1));
        assertClose("easeOutQuad(1)", 1, easeOutQuad(1));

        // ---- إحصاء إضافي ----
        assertClose("sampleVariance", 4.571428571428571, sampleVariance([2, 4, 4, 4, 5, 5, 7, 9]));
        assertClose("correlation perfect", 1, correlation([1, 2, 3, 4, 5], [2, 4, 6, 8, 10]));

        // ---- عشوائية: نتحقق من الحدود فقط (النتيجة عشوائية بطبيعتها) ----
        let ri = randomInt(1, 6);
        assertEq("randomInt bounds", true, ri >= 1 and ri <= 6);
        let rr = randomRange(0, 1);
        assertEq("randomRange bounds", true, rr >= 0 and rr < 1);
        let arrToShuffle = [1, 2, 3, 4, 5];
        shuffle(arrToShuffle);
        assertEq("shuffle preserves length", 5, len(arrToShuffle));

        // ---- تأكيد عدم تصادم clamp الفطرية (مصفوفة) مع clampNum (رقم) ----
        assertEq("native clamp still array-based", "[0, 5, 10]", toString(clamp([-1, 5, 20], 0, 10)));
    )RINSRC";

    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        std::string out = interp.run(statements);
        std::cout << out;

        int passCount = 0, failCount = 0;
        size_t pos = 0;
        while ((pos = out.find("PASS ", pos)) != std::string::npos) { passCount++; pos += 5; }
        pos = 0;
        while ((pos = out.find("FAIL ", pos)) != std::string::npos) { failCount++; pos += 5; }

        std::cout << "\n==== " << passCount << " PASS, " << failCount << " FAIL ====\n";
        if (interp.hadError() || failCount > 0) {
            std::cout << "[RESULT] test_math_lib FAILED\n";
            return 1;
        }
        std::cout << "[RESULT] test_math_lib OK\n";
        return 0;
    } catch (rin::RinError& e) {
        std::cerr << "[Error line " << e.line << "]: " << e.message << "\n";
        return 1;
    }
}
