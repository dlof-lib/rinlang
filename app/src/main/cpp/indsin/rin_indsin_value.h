// indsin/rin_indsin_value.h — نظام القيم المُنمَّطة (typed values) فوق indsin::Value.
//
// indsin::Value (rin_indsin_eval.h) يحمل رقماً أو نصاً فقط. قيم خصائص @view في الواقع أنواع أغنى:
// ألوان (#6C5CE7 · rgb() · hsl()) · أزمنة (300ms · 1.5s) · زوايا (45deg · 0.25turn) ·
// أطوال (12px · 50% · 2rem · 1.5em) · قوائم (a, b, c) · تعدادات (start | center | end).
// هذا الملف يضيف لها: تحليلاً صارماً مع رسائل خطأ، تحويل وحدات، حسابات ألوان (دمج/تفتيح/تباين WCAG)،
// استيفاء للحركة (lerp)، تحقق مخطّطي (schema)، وتنسيق للعرض.
//
// لا يغيّر شيئاً في Value ولا في المقيِّم: ملف إضافي بحت (header-only، بلا اعتماد خارج indsin_eval.h).
// الشرح الكامل: docs/indsin_value.md · الاختبار: tests/indsin_value/value_ext_test.cpp
#pragma once
#include "rin_indsin_eval.h"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace indsin::vx {

// ---------------------------------------------------------------------------------------------
// Result: نتيجة تحليل مع سبب الفشل (بدل الرجوع الصامت إلى قيمة افتراضية).
// ---------------------------------------------------------------------------------------------
template <class T>
struct Result {
    bool ok = false;
    T value{};
    std::string error;
    explicit operator bool() const { return ok; }
    T valueOr(T def) const { return ok ? value : def; }
    static Result good(T v) { Result r; r.ok = true; r.value = std::move(v); return r; }
    static Result bad(std::string why) { Result r; r.error = std::move(why); return r; }
};

namespace detail {
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}
// يفصل على فاصل من المستوى الأعلى فقط (يتجاهل ما داخل () [] {} وعلامات الاقتباس).
inline std::vector<std::string> splitTop(const std::string& s, char sep, bool whitespace) {
    std::vector<std::string> out; std::string cur; int depth = 0; char quote = 0;
    auto flush = [&] { std::string t = trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); };
    for (char c : s) {
        if (quote) { cur += c; if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') depth++;
        if (c == ')' || c == ']' || c == '}') depth = depth > 0 ? depth - 1 : 0;
        bool split = depth == 0 && (whitespace ? std::isspace(static_cast<unsigned char>(c)) != 0 : c == sep);
        if (split) flush(); else cur += c;
    }
    flush();
    return out;
}
inline bool hasTopLevelComma(const std::string& s) { return splitTop(s, ',', false).size() > 1; }
} // namespace detail

// ---------------------------------------------------------------------------------------------
// قوائم وتعدادات
// ---------------------------------------------------------------------------------------------
// "a, b, c" أو "a b c" (الفاصلة تغلب المسافة إن وُجدت على المستوى الأعلى) → عناصر مقصوصة.
inline std::vector<std::string> splitList(const std::string& text) {
    return detail::hasTopLevelComma(text) ? detail::splitTop(text, ',', false)
                                          : detail::splitTop(text, ' ', true);
}
inline std::vector<Value> toList(const Value& v) {
    std::vector<Value> out;
    if (v.isNumber()) { out.push_back(v); return out; }
    for (auto& p : splitList(v.str)) {
        double d;
        out.push_back(Value::parseNumber(p, d) ? Value::num(d) : Value::txt(p));
    }
    return out;
}
inline std::string joinList(const std::vector<Value>& items, const std::string& sep = ", ") {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) { if (i) out += sep; out += items[i].asString(); }
    return out;
}
// مفتاح موحَّد للمقارنة: حروف صغيرة و _ و مسافة → '-'  ("Center_Left" == "center-left").
inline std::string normalizeKey(const std::string& s) {
    std::string t = detail::lower(detail::trim(s));
    for (auto& c : t) if (c == '_' || c == ' ') c = '-';
    return t;
}
// تعداد: يعيد الشكل المعتمد (كما كُتب في options) أو خطأ يذكر الخيارات.
inline Result<std::string> toEnum(const Value& v, const std::vector<std::string>& options) {
    std::string key = normalizeKey(v.asString());
    for (auto& o : options) if (normalizeKey(o) == key) return Result<std::string>::good(o);
    std::string why = "'" + v.asString() + "' not one of: ";
    for (size_t i = 0; i < options.size(); i++) { if (i) why += " | "; why += options[i]; }
    return Result<std::string>::bad(why);
}

// ---------------------------------------------------------------------------------------------
// وحدات: زمن · زاوية · طول
// ---------------------------------------------------------------------------------------------
enum class UnitClass { NONE, TIME, ANGLE, LENGTH, UNKNOWN };

inline UnitClass unitClass(const std::string& unitRaw) {
    std::string u = detail::lower(unitRaw);
    if (u.empty()) return UnitClass::NONE;
    static const std::unordered_set<std::string> time{"ms", "s", "min", "h"};
    static const std::unordered_set<std::string> angle{"deg", "rad", "grad", "turn"};
    static const std::unordered_set<std::string> len{"px", "dp", "dip", "pt", "pc", "in", "cm", "mm", "em", "rem",
                                                     "%", "vw", "vh", "vmin", "vmax"};
    if (time.count(u)) return UnitClass::TIME;
    if (angle.count(u)) return UnitClass::ANGLE;
    if (len.count(u)) return UnitClass::LENGTH;
    return UnitClass::UNKNOWN;
}

// زمن → ميلي ثانية. رقم بلا وحدة = ms. وحدات: ms · s · min · h.
inline Result<double> toDurationMs(const Value& v) {
    auto m = v.asMeasure();
    if (!m.ok) return Result<double>::bad("'" + v.asString() + "' is not a duration");
    if (m.unit.empty() || m.unit == "ms") return Result<double>::good(m.value);
    if (m.unit == "s")   return Result<double>::good(m.value * 1000.0);
    if (m.unit == "min") return Result<double>::good(m.value * 60000.0);
    if (m.unit == "h")   return Result<double>::good(m.value * 3600000.0);
    return Result<double>::bad("unit '" + m.unit + "' is not a time unit (ms, s, min, h)");
}

// زاوية → درجات. رقم بلا وحدة = deg. وحدات: deg · rad · grad · turn.
inline Result<double> toAngleDeg(const Value& v) {
    auto m = v.asMeasure();
    if (!m.ok) return Result<double>::bad("'" + v.asString() + "' is not an angle");
    if (m.unit.empty() || m.unit == "deg") return Result<double>::good(m.value);
    if (m.unit == "rad")  return Result<double>::good(m.value * 180.0 / 3.14159265358979323846);
    if (m.unit == "grad") return Result<double>::good(m.value * 0.9);
    if (m.unit == "turn") return Result<double>::good(m.value * 360.0);
    return Result<double>::bad("unit '" + m.unit + "' is not an angle unit (deg, rad, grad, turn)");
}
// يطبّع الزاوية إلى [0, 360).
inline double normalizeDeg(double d) { d = std::fmod(d, 360.0); return d < 0 ? d + 360.0 : d; }

// سياق تحويل الأطوال إلى بكسل.
struct LengthContext {
    double emPx = 16.0;      // font-size الحالي
    double remPx = 16.0;     // font-size الجذر
    double parentPx = 0.0;   // أساس النسبة المئوية (0 = غير معروف → % تفشل)
    double viewportW = 0.0;  // 0 = غير معروف → vw/vmin/vmax تفشل
    double viewportH = 0.0;
    double density = 1.0;    // بكسل لكل dp
};

inline Result<double> toPx(const Value& v, const LengthContext& c = {}) {
    auto m = v.asMeasure();
    if (!m.ok) return Result<double>::bad("'" + v.asString() + "' is not a length");
    const std::string& u = m.unit; const double x = m.value;
    if (u.empty() || u == "px") return Result<double>::good(x);
    if (u == "dp" || u == "dip") return Result<double>::good(x * c.density);
    if (u == "pt") return Result<double>::good(x * 96.0 / 72.0);
    if (u == "pc") return Result<double>::good(x * 16.0);
    if (u == "in") return Result<double>::good(x * 96.0);
    if (u == "cm") return Result<double>::good(x * 96.0 / 2.54);
    if (u == "mm") return Result<double>::good(x * 96.0 / 25.4);
    if (u == "em") return Result<double>::good(x * c.emPx);
    if (u == "rem") return Result<double>::good(x * c.remPx);
    if (u == "%") {
        if (c.parentPx <= 0) return Result<double>::bad("percentage needs a parent size");
        return Result<double>::good(x / 100.0 * c.parentPx);
    }
    if (u == "vw" || u == "vh" || u == "vmin" || u == "vmax") {
        if (c.viewportW <= 0 || c.viewportH <= 0) return Result<double>::bad("viewport size unknown for '" + u + "'");
        double base = u == "vw" ? c.viewportW : u == "vh" ? c.viewportH
                    : u == "vmin" ? std::min(c.viewportW, c.viewportH) : std::max(c.viewportW, c.viewportH);
        return Result<double>::good(x / 100.0 * base);
    }
    return Result<double>::bad("unit '" + u + "' is not a length unit");
}

// ---------------------------------------------------------------------------------------------
// ألوان
// ---------------------------------------------------------------------------------------------
struct Color {
    uint8_t r = 0, g = 0, b = 0;
    double a = 1.0; // 0..1
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && std::fabs(a - o.a) < 1e-9; }
};

namespace detail {
struct Hsl { double h, s, l; }; // h بالدرجات، s و l في [0,1]
inline Hsl rgbToHsl(const Color& c) {
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b}), l = (mx + mn) / 2.0, h = 0, s = 0, d = mx - mn;
    if (d > 1e-12) {
        s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if (mx == g) h = (b - r) / d + 2.0;
        else h = (r - g) / d + 4.0;
        h *= 60.0;
    }
    return {h, s, l};
}
inline double hueToRgb(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 1.0 / 2) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}
inline Color hslToRgb(double h, double s, double l, double a) {
    h = normalizeDeg(h) / 360.0; s = clampd(s, 0, 1); l = clampd(l, 0, 1);
    double r, g, b;
    if (s == 0) r = g = b = l;
    else {
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        r = hueToRgb(p, q, h + 1.0 / 3); g = hueToRgb(p, q, h); b = hueToRgb(p, q, h - 1.0 / 3);
    }
    auto to8 = [](double x) { return static_cast<uint8_t>(std::lround(clampd(x, 0, 1) * 255.0)); };
    return Color{to8(r), to8(g), to8(b), clampd(a, 0, 1)};
}
// وسيط رقمي أو نسبة: "50%" → 0.5 · "128" → 128/scale.
inline bool parseChannel(const std::string& t, double scale, double& out) {
    std::string s = trim(t);
    if (s.empty()) return false;
    if (s.back() == '%') { double d; if (!Value::parseNumber(s.substr(0, s.size() - 1), d)) return false; out = d / 100.0; return true; }
    double d; if (!Value::parseNumber(s, d)) return false;
    out = d / scale; return true;
}
inline std::vector<std::string> funcArgs(const std::string& inner) {
    std::string t = inner;
    for (auto& c : t) if (c == ',' || c == '/') c = ' ';
    return splitTop(t, ' ', true);
}
} // namespace detail

inline Result<Color> parseColor(const std::string& textRaw) {
    using R = Result<Color>;
    std::string text = detail::trim(textRaw), t = detail::lower(text);
    if (t.empty()) return R::bad("empty color");

    if (t[0] == '#') { // #rgb · #rgba · #rrggbb · #rrggbbaa
        std::string h = t.substr(1);
        if (h.size() != 3 && h.size() != 4 && h.size() != 6 && h.size() != 8) return R::bad("hex color must have 3, 4, 6 or 8 digits: " + text);
        for (char ch : h) if (detail::hexDigit(ch) < 0) return R::bad("invalid hex digit in color: " + text);
        auto byteAt = [&](size_t i) { return h.size() <= 4 ? detail::hexDigit(h[i]) * 17
                                                           : detail::hexDigit(h[2 * i]) * 16 + detail::hexDigit(h[2 * i + 1]); };
        Color c; c.r = static_cast<uint8_t>(byteAt(0)); c.g = static_cast<uint8_t>(byteAt(1)); c.b = static_cast<uint8_t>(byteAt(2));
        if (h.size() == 4 || h.size() == 8) c.a = byteAt(3) / 255.0;
        return R::good(c);
    }

    size_t lp = t.find('(');
    if (lp != std::string::npos) { // rgb() rgba() hsl() hsla()
        if (t.back() != ')') return R::bad("missing ')' in color: " + text);
        std::string fn = detail::trim(t.substr(0, lp));
        auto args = detail::funcArgs(t.substr(lp + 1, t.size() - lp - 2));
        if (args.size() != 3 && args.size() != 4) return R::bad(fn + "() takes 3 or 4 arguments: " + text);
        double a = 1.0;
        if (args.size() == 4 && !detail::parseChannel(args[3], 1.0, a)) return R::bad("invalid alpha in color: " + text);
        if (fn == "rgb" || fn == "rgba") {
            double ch[3];
            for (int i = 0; i < 3; i++) {
                if (!detail::parseChannel(args[i], 255.0, ch[i])) return R::bad("invalid channel in color: " + text);
                ch[i] = detail::clampd(ch[i], 0, 1);
            }
            auto to8 = [](double x) { return static_cast<uint8_t>(std::lround(x * 255.0)); };
            return R::good(Color{to8(ch[0]), to8(ch[1]), to8(ch[2]), detail::clampd(a, 0, 1)});
        }
        if (fn == "hsl" || fn == "hsla") {
            auto hue = toAngleDeg(Value::txt(args[0]));
            double s, l;
            if (!hue.ok) return R::bad("invalid hue in color: " + text);
            if (!detail::parseChannel(args[1], 100.0, s) || !detail::parseChannel(args[2], 100.0, l)) return R::bad("invalid saturation/lightness in color: " + text);
            return R::good(detail::hslToRgb(hue.value, s, l, a));
        }
        return R::bad("unknown color function '" + fn + "'");
    }

    struct Named { const char* n; uint8_t r, g, b; };
    static const Named names[] = {
        {"black", 0, 0, 0}, {"white", 255, 255, 255}, {"red", 255, 0, 0}, {"green", 0, 128, 0}, {"blue", 0, 0, 255},
        {"yellow", 255, 255, 0}, {"cyan", 0, 255, 255}, {"magenta", 255, 0, 255}, {"gray", 128, 128, 128},
        {"grey", 128, 128, 128}, {"orange", 255, 165, 0}, {"purple", 128, 0, 128}, {"pink", 255, 192, 203},
        {"brown", 165, 42, 42}, {"teal", 0, 128, 128}, {"navy", 0, 0, 128}, {"lime", 0, 255, 0},
        {"maroon", 128, 0, 0}, {"olive", 128, 128, 0}, {"silver", 192, 192, 192}};
    if (t == "transparent") return R::good(Color{0, 0, 0, 0.0});
    for (auto& n : names) if (t == n.n) return R::good(Color{n.r, n.g, n.b, 1.0});
    return R::bad("unknown color '" + text + "'");
}
inline Result<Color> toColor(const Value& v) {
    if (v.isNumber()) return Result<Color>::bad("a number is not a color");
    return parseColor(v.str);
}

inline std::string toHex(const Color& c, bool withAlpha = false) {
    static const char* d = "0123456789abcdef";
    std::string s = "#";
    auto put = [&](uint8_t b) { s += d[b >> 4]; s += d[b & 15]; };
    put(c.r); put(c.g); put(c.b);
    if (withAlpha || c.a < 1.0) put(static_cast<uint8_t>(std::lround(detail::clampd(c.a, 0, 1) * 255.0)));
    return s;
}
inline std::string toCssRgb(const Color& c) {
    std::string s = (c.a < 1.0 ? "rgba(" : "rgb(") + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b);
    if (c.a < 1.0) s += ", " + Value::formatNumber(std::round(c.a * 1000.0) / 1000.0);
    return s + ")";
}
inline Color withAlpha(Color c, double a) { c.a = detail::clampd(a, 0, 1); return c; }

// دمج خطي في RGB: t=0 → a ، t=1 → b.
inline Color mix(const Color& a, const Color& b, double t) {
    t = detail::clampd(t, 0, 1);
    auto ch = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
    return Color{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), a.a + (b.a - a.a) * t};
}
inline Color lighten(const Color& c, double amount) { auto h = detail::rgbToHsl(c); return detail::hslToRgb(h.h, h.s, h.l + amount, c.a); }
inline Color darken(const Color& c, double amount) { return lighten(c, -amount); }
inline Color saturate(const Color& c, double amount) { auto h = detail::rgbToHsl(c); return detail::hslToRgb(h.h, h.s + amount, h.l, c.a); }
inline Color rotateHue(const Color& c, double degrees) { auto h = detail::rgbToHsl(c); return detail::hslToRgb(h.h + degrees, h.s, h.l, c.a); }

// سطوع نسبي (WCAG 2.x) ونسبة التباين (1..21).
inline double relativeLuminance(const Color& c) {
    auto lin = [](uint8_t v) { double s = v / 255.0; return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}
inline double contrastRatio(const Color& a, const Color& b) {
    double la = relativeLuminance(a), lb = relativeLuminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}
// مستوى WCAG لنسبة تباين: نص عادي AA ≥ 4.5 و AAA ≥ 7؛ نص كبير AA ≥ 3 و AAA ≥ 4.5.
enum class Wcag { FAIL, AA, AAA };
inline Wcag wcagLevel(double ratio, bool largeText = false) {
    if (ratio >= (largeText ? 4.5 : 7.0)) return Wcag::AAA;
    if (ratio >= (largeText ? 3.0 : 4.5)) return Wcag::AA;
    return Wcag::FAIL;
}
// يختار أسود/أبيض الأعلى تبايناً فوق خلفية معيّنة.
inline Color readableOn(const Color& background) {
    const Color black{0, 0, 0, 1}, white{255, 255, 255, 1};
    return contrastRatio(background, black) >= contrastRatio(background, white) ? black : white;
}

// ---------------------------------------------------------------------------------------------
// نوع القيمة المستنتج
// ---------------------------------------------------------------------------------------------
enum class Type { ANY, EMPTY, NUMBER, INT, BOOL, TEXT, COLOR, DURATION, ANGLE, LENGTH, ENUM, LIST };

inline const char* typeName(Type t) {
    switch (t) {
        case Type::ANY: return "any"; case Type::EMPTY: return "empty"; case Type::NUMBER: return "number";
        case Type::INT: return "int"; case Type::BOOL: return "bool"; case Type::TEXT: return "text";
        case Type::COLOR: return "color"; case Type::DURATION: return "duration"; case Type::ANGLE: return "angle";
        case Type::LENGTH: return "length"; case Type::ENUM: return "enum"; case Type::LIST: return "list";
    }
    return "any";
}
// قيمة تُفهَم منطقياً بلا لبس: رقم أو true/false/1/0/yes/no/on/off.
inline bool isBoolLike(const Value& v) {
    return v.asBool(true) == v.asBool(false);
}
// يستنتج أدقّ نوع ممكن. ملاحظة: كلمة مثل "red" تُعدّ لوناً (أسماء الألوان محجوزة)؛ استعمل Spec لتثبيت النوع.
inline Type inferType(const Value& v) {
    if (v.isNumber()) return Type::NUMBER;
    std::string t = detail::trim(v.str);
    if (t.empty()) return Type::EMPTY;
    if (v.isNumeric()) return Type::NUMBER;
    if (v.isBoolText()) return Type::BOOL;
    if (detail::hasTopLevelComma(t)) return Type::LIST;
    if (parseColor(t).ok) return Type::COLOR;
    auto m = v.asMeasure();
    if (m.ok) {
        switch (unitClass(m.unit)) {
            case UnitClass::TIME: return Type::DURATION;
            case UnitClass::ANGLE: return Type::ANGLE;
            case UnitClass::LENGTH: return Type::LENGTH;
            default: break;
        }
    }
    return Type::TEXT;
}

// ---------------------------------------------------------------------------------------------
// استيفاء (للحركة والانتقالات): أرقام · أطوال بنفس الوحدة · ألوان · غير ذلك يقفز عند t >= 0.5
// ---------------------------------------------------------------------------------------------
inline Value lerp(const Value& a, const Value& b, double t) {
    t = detail::clampd(t, 0, 1);
    if (a.isNumeric() && b.isNumeric()) return Value::num(a.asNumber() + (b.asNumber() - a.asNumber()) * t);
    auto ma = a.asMeasure(), mb = b.asMeasure();
    if (ma.ok && mb.ok && !ma.unit.empty() && ma.unit == mb.unit && unitClass(ma.unit) != UnitClass::UNKNOWN)
        return Value::txt(Value::formatNumber(ma.value + (mb.value - ma.value) * t) + ma.unit);
    auto ca = toColor(a), cb = toColor(b);
    if (ca.ok && cb.ok) return Value::txt(toHex(mix(ca.value, cb.value, t)));
    return t < 0.5 ? a : b;
}

// تقييد قيمة رقمية أو بوحدة ضمن [lo, hi] مع بقاء الوحدة.
inline Value clampValue(const Value& v, double lo, double hi) {
    if (lo > hi) std::swap(lo, hi);
    auto m = v.asMeasure();
    if (!m.ok) return v;
    double c = detail::clampd(m.value, lo, hi);
    return m.unit.empty() ? Value::num(c) : Value::txt(Value::formatNumber(c) + m.unit);
}

// ---------------------------------------------------------------------------------------------
// تنسيق للعرض
// ---------------------------------------------------------------------------------------------
inline std::string formatFixed(double n, int decimals) {
    if (!std::isfinite(n)) return Value::formatNumber(n);
    decimals = decimals < 0 ? 0 : (decimals > 12 ? 12 : decimals);
    char buf[64]; std::snprintf(buf, sizeof buf, "%.*f", decimals, n);
    std::string s = buf;
    if (s.find_first_not_of("-0.") == std::string::npos && s[0] == '-') s.erase(0, 1); // "-0.00" → "0.00"
    return s;
}
inline std::string formatPercent(double ratio, int decimals = 0) { return formatFixed(ratio * 100.0, decimals) + "%"; }
// 1234 → 1.2K · 1234567 → 1.2M · 1e9 → 1B
inline std::string formatCompact(double n) {
    if (!std::isfinite(n)) return Value::formatNumber(n);
    double a = std::fabs(n); const char* suf = ""; double div = 1;
    if (a >= 1e12) { suf = "T"; div = 1e12; } else if (a >= 1e9) { suf = "B"; div = 1e9; }
    else if (a >= 1e6) { suf = "M"; div = 1e6; } else if (a >= 1e3) { suf = "K"; div = 1e3; }
    if (div == 1) return Value::formatNumber(n);
    std::string s = formatFixed(n / div, 1);
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.erase(s.size() - 2);
    return s + suf;
}
inline std::string formatBytes(double bytes) {
    static const char* u[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = std::fabs(bytes); int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; i++; }
    std::string s = i == 0 ? Value::formatNumber(std::floor(v)) : formatFixed(v, v < 10 ? 1 : 0);
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.erase(s.size() - 2);
    return (bytes < 0 ? "-" : "") + s + " " + u[i];
}
// 90061000 ms → "1d 1h 1m 1s" (يحذف الأجزاء الصفرية)؛ أقل من ثانية → "250ms".
inline std::string formatDuration(double ms) {
    if (!std::isfinite(ms)) return Value::formatNumber(ms);
    bool neg = ms < 0; double a = std::fabs(ms);
    if (a < 1000.0) return (neg ? "-" : "") + Value::formatNumber(std::round(a)) + "ms";
    long long total = static_cast<long long>(a / 1000.0);
    long long d = total / 86400, h = total % 86400 / 3600, m = total % 3600 / 60, s = total % 60;
    std::string out;
    if (d) out += std::to_string(d) + "d ";
    if (h) out += std::to_string(h) + "h ";
    if (m) out += std::to_string(m) + "m ";
    if (s || out.empty()) out += std::to_string(s) + "s ";
    out.pop_back();
    return (neg ? "-" : "") + out;
}

// ---------------------------------------------------------------------------------------------
// تحقق مخطّطي (schema) لخصائص عنصر
// ---------------------------------------------------------------------------------------------
struct Spec {
    std::string key;
    Type type = Type::ANY;
    bool required = false;
    bool hasMin = false, hasMax = false;
    double min = 0, max = 0;            // رقم/INT: القيمة · DURATION: ms · ANGLE: درجات · LENGTH: الرقم الخام
    std::vector<std::string> options;   // لـ ENUM
    std::string def;                    // قيمة افتراضية (تُعاد من withDefaults)

    Spec& req() { required = true; return *this; }
    Spec& range(double lo, double hi) { hasMin = hasMax = true; min = lo; max = hi; return *this; }
    Spec& atLeast(double lo) { hasMin = true; min = lo; return *this; }
    Spec& atMost(double hi) { hasMax = true; max = hi; return *this; }
    Spec& oneOf(std::initializer_list<const char*> opts) { options.assign(opts.begin(), opts.end()); return *this; }
    Spec& withDefault(std::string d) { def = std::move(d); return *this; }
};
inline Spec spec(std::string key, Type type) { Spec s; s.key = std::move(key); s.type = type; return s; }

struct Issue {
    std::string key;
    std::string message;
    bool error = true; // false = تحذير
};

// يتحقق من نوع كل قيمة ونطاقها؛ المفاتيح غير المعرّفة تُبلَّغ تحذيراً (warnUnknown).
inline std::vector<Issue> validate(const std::unordered_map<std::string, Value>& attrs, const std::vector<Spec>& specs,
                                   bool warnUnknown = true) {
    std::vector<Issue> out;
    std::unordered_set<std::string> known;
    auto err = [&](const std::string& k, const std::string& m) { out.push_back({k, m, true}); };
    for (auto& s : specs) {
        known.insert(s.key);
        auto it = attrs.find(s.key);
        if (it == attrs.end() || it->second.isEmpty()) {
            if (s.required && s.def.empty()) err(s.key, "required attribute is missing");
            continue;
        }
        const Value& v = it->second;
        double num = 0; bool check = false; std::string unitNote;
        switch (s.type) {
            case Type::ANY: case Type::TEXT: case Type::EMPTY: break;
            case Type::NUMBER: case Type::INT:
                if (!v.isNumeric()) { err(s.key, "expected a number, got '" + v.asString() + "'"); break; }
                num = v.asNumber(); check = true;
                if (s.type == Type::INT && num != std::floor(num)) err(s.key, "expected an integer, got " + Value::formatNumber(num));
                break;
            case Type::BOOL:
                if (!isBoolLike(v)) err(s.key, "expected true/false, got '" + v.asString() + "'");
                break;
            case Type::COLOR: { auto r = toColor(v); if (!r.ok) err(s.key, r.error); break; }
            case Type::DURATION: { auto r = toDurationMs(v); if (!r.ok) err(s.key, r.error); else { num = r.value; check = true; } break; }
            case Type::ANGLE: { auto r = toAngleDeg(v); if (!r.ok) err(s.key, r.error); else { num = r.value; check = true; } break; }
            case Type::LENGTH: {
                auto m = v.asMeasure();
                if (!m.ok || (unitClass(m.unit) != UnitClass::NONE && unitClass(m.unit) != UnitClass::LENGTH))
                    err(s.key, "expected a length (12px, 50%, 2rem), got '" + v.asString() + "'");
                else { num = m.value; check = true; }
                break;
            }
            case Type::ENUM: { auto r = toEnum(v, s.options); if (!r.ok) err(s.key, r.error); break; }
            case Type::LIST: if (toList(v).empty()) err(s.key, "expected a non-empty list"); break;
        }
        if (check) {
            if (s.hasMin && num < s.min) err(s.key, Value::formatNumber(num) + " is below the minimum " + Value::formatNumber(s.min));
            if (s.hasMax && num > s.max) err(s.key, Value::formatNumber(num) + " is above the maximum " + Value::formatNumber(s.max));
        }
    }
    if (warnUnknown) {
        std::vector<std::string> keys;
        for (auto& kv : attrs) if (!known.count(kv.first)) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& k : keys) out.push_back({k, "unknown attribute", false});
    }
    return out;
}

// يملأ القيم الافتراضية للمفاتيح الغائبة/الفارغة (لا يلمس الموجود).
inline std::unordered_map<std::string, Value> withDefaults(std::unordered_map<std::string, Value> attrs,
                                                           const std::vector<Spec>& specs) {
    for (auto& s : specs) {
        if (s.def.empty()) continue;
        auto it = attrs.find(s.key);
        if (it == attrs.end() || it->second.isEmpty()) {
            double d; attrs[s.key] = Value::parseNumber(s.def, d) ? Value::num(d) : Value::txt(s.def);
        }
    }
    return attrs;
}

// ---------------------------------------------------------------------------------------------
// Typed: واجهة موحّدة فوق Value — v.as(...) بدل تذكّر الدالة المناسبة.
// ---------------------------------------------------------------------------------------------
class Typed {
public:
    explicit Typed(Value v) : v_(std::move(v)) {}
    const Value& value() const { return v_; }
    Type type() const { return inferType(v_); }
    const char* typeName() const { return vx::typeName(type()); }

    bool toBool(bool def = false) const { return v_.asBool(def); }
    double toNumber(double def = 0) const { return v_.asNumber(def); }
    int toInt(int def = 0) const { return v_.asInt(def); }
    Result<Color> color() const { return toColor(v_); }
    Result<double> durationMs() const { return toDurationMs(v_); }
    Result<double> angleDeg() const { return toAngleDeg(v_); }
    Result<double> px(const LengthContext& c = {}) const { return toPx(v_, c); }
    Result<std::string> enumOf(const std::vector<std::string>& options) const { return toEnum(v_, options); }
    std::vector<Value> list() const { return toList(v_); }

private:
    Value v_;
};

} // namespace indsin::vx
