// اختبار indsin::Value: g++ -std=c++20 -I app/src/main/cpp tests/indsin_value/value_test.cpp -o /tmp/value_test && /tmp/value_test
#include "indsin/rin_indsin_eval.h"
#include <cstdio>
using indsin::Value;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    // formatNumber: لا فقدان دقة
    CHECK(Value::num(1234567).asString() == "1234567");
    CHECK(Value::num(0.1).asString() == "0.1");
    CHECK(Value::num(-0.0).asString() == "0");
    CHECK(Value::num(1.5).asString() == "1.5");
    CHECK(Value::num(1e20).asString() != "");
    // parseNumber صارم
    CHECK(Value::txt(" 42 ").asNumber(-1) == 42);
    CHECK(Value::txt("nan").asNumber(-1) == -1);
    CHECK(Value::txt("inf").asNumber(-1) == -1);
    CHECK(Value::txt("0x10").asNumber(-1) == -1);
    CHECK(Value::txt("12px").asNumber(-1) == -1);
    CHECK(Value::txt("").asNumber(7) == 7);
    // asInt
    CHECK(Value::txt("2.6").asInt() == 3);
    CHECK(Value::num(1e18).asInt() == 2147483647);
    // truthiness (قاعدة Rin) مقابل asBool المتسامح
    CHECK(!Value::txt("").isTruthy() && !Value::txt("false").isTruthy() && Value::txt("0").isTruthy());
    CHECK(!Value::num(0).isTruthy() && Value::num(2).isTruthy());
    CHECK(Value::txt("YES").asBool() && !Value::txt("off").asBool() && Value::txt("maybe").asBool(true));
    CHECK(Value::boolean(true).isBoolText() && Value::boolean(false).str == "false");
    // asMeasure
    auto m = Value::txt("12px").asMeasure(); CHECK(m.ok && m.value == 12 && m.unit == "px");
    m = Value::txt("-1.5EM").asMeasure();    CHECK(m.ok && m.value == -1.5 && m.unit == "em");
    m = Value::txt("50%").asMeasure();       CHECK(m.ok && m.value == 50 && m.unit == "%");
    m = Value::txt("px").asMeasure();        CHECK(!m.ok);
    m = Value::txt("12 p-x").asMeasure();    CHECK(!m.ok);
    m = Value::num(8).asMeasure();           CHECK(m.ok && m.value == 8 && m.unit.empty());
    // المقارنة
    CHECK(Value::txt("3").looseEquals(Value::num(3)));
    CHECK(!Value::txt("a").looseEquals(Value::num(3)));
    CHECK(Value::txt("10").compare(Value::txt("9")) > 0);   // رقمياً لا معجمياً
    CHECK(Value::txt("apple").compare(Value::txt("banana")) < 0);
    CHECK(Value::num(1) == Value::num(1) && !(Value::num(1) == Value::txt("1")));
    std::printf(fails ? "SOME FAILED\n" : "ALL PASSED\n");
    return fails ? 1 : 0;
}
