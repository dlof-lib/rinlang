// اختبار نظام القيم المُنمَّطة: g++ -std=c++20 -Wall -Wextra -I app/src/main/cpp tests/indsin_value/value_ext_test.cpp -o /tmp/vx && /tmp/vx
#include "indsin/rin_indsin_value.h"
#include <cstdio>
using indsin::Value;
using namespace indsin::vx;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }

int main() {
    // ---- قوائم وتعدادات ----
    CHECK(splitList("a, b ,c").size() == 3);
    CHECK(splitList("a b  c").size() == 3);
    CHECK(splitList("rgb(1, 2, 3), red").size() == 2);              // الفاصلة داخل () لا تفصل
    CHECK(splitList("\"a, b\", c").size() == 2);                     // ولا داخل الاقتباس
    CHECK(splitList("").empty());
    { auto l = toList(Value::txt("1, two, 3.5")); CHECK(l.size() == 3 && l[0].isNumber() && l[1].isString() && l[2].number == 3.5); }
    CHECK(joinList(toList(Value::txt("1 2 3")), "-") == "1-2-3");
    CHECK(toEnum(Value::txt("Center_Left"), {"start", "center-left"}).ok);
    CHECK(toEnum(Value::txt("center-left"), {"start", "center-left"}).value == "center-left");
    { auto r = toEnum(Value::txt("x"), {"a", "b"}); CHECK(!r.ok && r.error.find("a | b") != std::string::npos); }

    // ---- زمن وزاوية ----
    CHECK(near(toDurationMs(Value::txt("1.5s")).value, 1500));
    CHECK(near(toDurationMs(Value::txt("2min")).value, 120000));
    CHECK(near(toDurationMs(Value::num(250)).value, 250));
    CHECK(!toDurationMs(Value::txt("3px")).ok);
    CHECK(!toDurationMs(Value::txt("soon")).ok);
    CHECK(near(toAngleDeg(Value::txt("0.25turn")).value, 90));
    CHECK(near(toAngleDeg(Value::txt("3.14159265358979rad")).value, 180, 1e-6));
    CHECK(near(toAngleDeg(Value::txt("100grad")).value, 90));
    CHECK(near(normalizeDeg(-90), 270) && near(normalizeDeg(720), 0));

    // ---- أطوال ----
    LengthContext c; c.emPx = 20; c.parentPx = 400; c.viewportW = 800; c.viewportH = 600; c.density = 2;
    CHECK(near(toPx(Value::txt("12px"), c).value, 12));
    CHECK(near(toPx(Value::txt("2em"), c).value, 40));
    CHECK(near(toPx(Value::txt("2rem"), c).value, 32));
    CHECK(near(toPx(Value::txt("50%"), c).value, 200));
    CHECK(near(toPx(Value::txt("10dp"), c).value, 20));
    CHECK(near(toPx(Value::txt("10vw"), c).value, 80));
    CHECK(near(toPx(Value::txt("10vmin"), c).value, 60));
    CHECK(near(toPx(Value::txt("1in")).value, 96));
    CHECK(near(toPx(Value::txt("72pt")).value, 96));
    CHECK(!toPx(Value::txt("50%")).ok);       // لا أساس للنسبة
    CHECK(!toPx(Value::txt("5vw")).ok);       // لا viewport
    CHECK(!toPx(Value::txt("3s")).ok);        // وحدة زمن

    // ---- ألوان: تحليل ----
    { auto r = parseColor("#6C5CE7"); CHECK(r.ok && r.value.r == 0x6C && r.value.g == 0x5C && r.value.b == 0xE7 && r.value.a == 1.0); }
    { auto r = parseColor("#f0a"); CHECK(r.ok && r.value.r == 0xff && r.value.g == 0 && r.value.b == 0xaa); }
    { auto r = parseColor("#ff000080"); CHECK(r.ok && near(r.value.a, 128 / 255.0)); }
    { auto r = parseColor("rgb(255, 128, 0)"); CHECK(r.ok && r.value.g == 128); }
    { auto r = parseColor("rgb(100% 0% 0% / 50%)"); CHECK(r.ok && r.value.r == 255 && near(r.value.a, 0.5)); }
    { auto r = parseColor("rgba(0,0,0,0.25)"); CHECK(r.ok && near(r.value.a, 0.25)); }
    { auto r = parseColor("hsl(120, 100%, 25%)"); CHECK(r.ok && r.value.r == 0 && r.value.g == 128 && r.value.b == 0); }
    { auto r = parseColor("hsl(0.5turn 100% 50%)"); CHECK(r.ok && r.value.r == 0 && r.value.g == 255 && r.value.b == 255); }
    { auto r = parseColor("NAVY"); CHECK(r.ok && r.value.b == 128); }
    { auto r = parseColor("transparent"); CHECK(r.ok && r.value.a == 0.0); }
    CHECK(!parseColor("#12").ok && !parseColor("#gggggg").ok && !parseColor("rgb(1,2)").ok);
    CHECK(!parseColor("rgb(1,2,3").ok && !parseColor("foo(1,2,3)").ok && !parseColor("nocolor").ok && !parseColor("").ok);
    CHECK(!toColor(Value::num(5)).ok);
    // ---- ألوان: إخراج وحسابات ----
    CHECK(toHex(Color{255, 0, 170, 1}) == "#ff00aa");
    CHECK(toHex(Color{255, 0, 0, 0.5}) == "#ff000080");
    CHECK(toCssRgb(Color{1, 2, 3, 1}) == "rgb(1, 2, 3)");
    CHECK(toCssRgb(Color{1, 2, 3, 0.5}) == "rgba(1, 2, 3, 0.5)");
    { Color m = mix(Color{0, 0, 0, 1}, Color{255, 255, 255, 1}, 0.5); CHECK(m.r == 128 && m.g == 128); }
    { Color l = lighten(Color{0, 0, 255, 1}, 0.2); Color d = darken(Color{0, 0, 255, 1}, 0.2);
      CHECK(relativeLuminance(l) > relativeLuminance(Color{0, 0, 255, 1}) && relativeLuminance(d) < relativeLuminance(Color{0, 0, 255, 1})); }
    { Color r = rotateHue(Color{255, 0, 0, 1}, 120); CHECK(r.r == 0 && r.g == 255 && r.b == 0); }
    CHECK(near(contrastRatio(Color{0, 0, 0, 1}, Color{255, 255, 255, 1}), 21.0, 1e-6));
    CHECK(near(contrastRatio(Color{255, 255, 255, 1}, Color{255, 255, 255, 1}), 1.0));
    CHECK(wcagLevel(21) == Wcag::AAA && wcagLevel(5) == Wcag::AA && wcagLevel(3.2) == Wcag::FAIL && wcagLevel(3.2, true) == Wcag::AA);
    CHECK(readableOn(Color{0, 0, 0, 1}).r == 255 && readableOn(Color{255, 255, 255, 1}).r == 0);
    CHECK(withAlpha(Color{1, 2, 3, 1}, 2.0).a == 1.0);

    // ---- استنتاج النوع ----
    CHECK(inferType(Value::num(3)) == Type::NUMBER && inferType(Value::txt("3.5")) == Type::NUMBER);
    CHECK(inferType(Value::txt("")) == Type::EMPTY && inferType(Value::txt("true")) == Type::BOOL);
    CHECK(inferType(Value::txt("#fff")) == Type::COLOR && inferType(Value::txt("rgb(1,2,3)")) == Type::COLOR);
    CHECK(inferType(Value::txt("300ms")) == Type::DURATION && inferType(Value::txt("45deg")) == Type::ANGLE);
    CHECK(inferType(Value::txt("12px")) == Type::LENGTH && inferType(Value::txt("50%")) == Type::LENGTH);
    CHECK(inferType(Value::txt("a, b")) == Type::LIST && inferType(Value::txt("hello")) == Type::TEXT);
    CHECK(inferType(Value::txt("12zz")) == Type::TEXT);

    // ---- lerp / clamp ----
    CHECK(near(lerp(Value::num(0), Value::num(10), 0.25).number, 2.5));
    CHECK(lerp(Value::txt("10px"), Value::txt("20px"), 0.5).str == "15px");
    CHECK(lerp(Value::txt("#000000"), Value::txt("#ffffff"), 0.5).str == "#808080");
    CHECK(lerp(Value::txt("10px"), Value::txt("2em"), 0.4).str == "10px");        // وحدتان مختلفتان → قفزة
    CHECK(lerp(Value::txt("a"), Value::txt("b"), 0.6).str == "b");
    CHECK(near(lerp(Value::num(0), Value::num(10), 5).number, 10));               // t يُقيَّد
    CHECK(clampValue(Value::txt("120%"), 0, 100).str == "100%");
    CHECK(near(clampValue(Value::num(-5), 0, 1).number, 0));
    CHECK(clampValue(Value::txt("abc"), 0, 1).str == "abc");

    // ---- تنسيق ----
    CHECK(formatFixed(3.14159, 2) == "3.14" && formatFixed(-0.001, 2) == "0.00" && formatFixed(2, 0) == "2");
    CHECK(formatPercent(0.256, 1) == "25.6%");
    CHECK(formatCompact(999) == "999" && formatCompact(1234) == "1.2K" && formatCompact(2000000) == "2M" && formatCompact(-1500) == "-1.5K");
    CHECK(formatCompact(3.2e9) == "3.2B");
    CHECK(formatBytes(512) == "512 B" && formatBytes(1536) == "1.5 KB" && formatBytes(1048576) == "1 MB");
    CHECK(formatDuration(250) == "250ms" && formatDuration(90061000) == "1d 1h 1m 1s" && formatDuration(60000) == "1m");
    CHECK(formatDuration(0) == "0ms");

    // ---- validate ----
    std::vector<Spec> specs = {
        spec("color", Type::COLOR).req(),
        spec("duration", Type::DURATION).range(0, 5000),
        spec("size", Type::INT).atLeast(1),
        spec("align", Type::ENUM).oneOf({"start", "center", "end"}).withDefault("start"),
        spec("width", Type::LENGTH),
        spec("open", Type::BOOL),
    };
    {
        std::unordered_map<std::string, Value> ok{{"color", Value::txt("#fff")}, {"duration", Value::txt("2s")}, {"size", Value::num(3)},
                                                  {"align", Value::txt("Center")}, {"width", Value::txt("50%")}, {"open", Value::txt("yes")}};
        CHECK(validate(ok, specs).empty());
    }
    {
        std::unordered_map<std::string, Value> bad{{"duration", Value::txt("9s")}, {"size", Value::num(2.5)}, {"align", Value::txt("middle")},
                                                   {"width", Value::txt("3s")}, {"open", Value::txt("maybe")}, {"extra", Value::txt("x")}};
        auto issues = validate(bad, specs);
        int errors = 0, warns = 0; bool missing = false;
        for (auto& i : issues) { (i.error ? errors : warns)++; if (i.key == "color") missing = true; }
        CHECK(missing && errors == 6 && warns == 1);
        CHECK(validate(bad, specs, false).size() == 6);
    }
    {
        auto d = withDefaults({}, specs);
        CHECK(d.count("align") && d["align"].str == "start" && !d.count("size"));
        CHECK(validate({{"color", Value::txt("red")}}, specs).empty());     // align له افتراضي → لا يُطلب
    }

    // ---- Typed ----
    { Typed t(Value::txt("#6C5CE7")); CHECK(t.type() == Type::COLOR && std::string(t.typeName()) == "color" && t.color().ok); }
    { Typed t(Value::txt("1.5s")); CHECK(near(t.durationMs().value, 1500) && t.type() == Type::DURATION); }
    { Typed t(Value::txt("on")); CHECK(t.toBool() && t.list().size() == 1); }
    { Typed t(Value::txt("2.6")); CHECK(t.toInt() == 3 && near(t.toNumber(), 2.6)); }
    { Typed t(Value::txt("a b")); CHECK(t.list().size() == 2 && t.enumOf({"a b"}).ok); }
    { Result<int> r = Result<int>::bad("no"); CHECK(!r && r.valueOr(7) == 7); }

    std::printf(fails ? "SOME FAILED\n" : "ALL PASSED\n");
    return fails ? 1 : 0;
}
