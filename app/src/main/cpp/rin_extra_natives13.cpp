// ============================================================================
//  rin_extra_natives13.cpp — بايتات مسمّاة · مدد زمنية · أول/آخر · نصوص utf8 · (state fixed في المفسّر)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp ويُسجَّل عبر Interpreter::registerNativesExtra13().
//  التوثيق: docs/EXTRA13.md
//
//  bytes.encode(spec, قيم... | قاموس)   bytes.decode(spec, بايتات [, إزاحة])   bytes.size(spec)
//      spec = "magic:str4 ver:u16le size:u32be ratio:f32 pad2 name:str*"   (الافتراضي little-endian)
//      الأنواع: u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 (+ le|be) · strN · str* · hexN · padN · تكرار: u8*4
//  dur.parse("1h30m") · dur.of({h,m,..}) · dur.parts(ms) · dur.text(ms) · dur.fmt(ms, "{hh}:{mm}:{ss}")
//      dur.add · dur.sub · dur.scale · dur.cmp · dur.abs · dur.valid   (المدة رقم بالميلي ثانية)
//  firstOf(x, افتراضي?) · lastOf(x, افتراضي?) · firstKey(x) · lastKey(x) · findKeyIf(x, fn)
//  utf8Trim · utf8TrimStart · utf8TrimEnd · utf8CapFirst · utf8LowFirst · utf8Pad(s, n, حشو?, "end"|"start"|"both")
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace rin {
namespace x13 {
using Args = std::vector<Value>;

[[noreturn]] static void bad(int line, const std::string& m) { throw diagErr(diag::Code::E0035_RuntimeError, line, m); }
static Value mkNum(double d) { return Value::num(d); }
static Value mkStr(std::string s) { return Value::string(std::move(s)); }
static Value mkMap() { return Value::makeMap(std::make_shared<MapData>()); }

// ------------------------------------------------------------------ bytes.*
struct Field { std::string name; std::string kind; int bits = 0; bool sign = false, be = false, star = false; size_t n = 0; size_t rep = 1; };

static std::vector<Field> parseSpec(const std::string& spec, int line, const char* who) {
    std::vector<Field> out;
    std::string tok;
    auto flush = [&]() {
        if (tok.empty()) return;
        Field f; std::string t = tok; tok.clear();
        size_t c = t.find(':');
        if (c != std::string::npos) { f.name = t.substr(0, c); t = t.substr(c + 1); }
        size_t st = t.find('*');
        if (st != std::string::npos && st + 1 < t.size()) {   // تكرار u8*4
            f.rep = static_cast<size_t>(std::strtoul(t.c_str() + st + 1, nullptr, 10));
            if (f.rep < 1 || f.rep > 65536) bad(line, std::string(who) + ": عدد تكرار غير صالح في '" + tok + t + "'");
            t = t.substr(0, st);
        }
        auto numTail = [&](size_t from) -> long { return t.size() > from ? std::strtol(t.c_str() + from, nullptr, 10) : -1; };
        if (t.rfind("str", 0) == 0 || t.rfind("hex", 0) == 0 || t.rfind("pad", 0) == 0) {
            f.kind = t.substr(0, 3);
            if (t.size() == 4 && t[3] == '*' && f.kind == "str") { f.star = true; }
            else { long n = numTail(3); if (n < 0 || n > 1 << 24) bad(line, std::string(who) + ": حجم غير صالح في '" + t + "'"); f.n = static_cast<size_t>(n); }
        } else {
            std::string base = t; bool be = false;
            if (base.size() > 2 && base.compare(base.size() - 2, 2, "be") == 0) { be = true; base.resize(base.size() - 2); }
            else if (base.size() > 2 && base.compare(base.size() - 2, 2, "le") == 0) base.resize(base.size() - 2);
            static const char* ok[] = {"u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "f32", "f64"};
            bool found = false; for (auto o : ok) if (base == o) found = true;
            if (!found) bad(line, std::string(who) + ": نوع غير معروف '" + t + "' (المتاح: u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 [le|be] · strN · str* · hexN · padN)");
            if ((base == "u8" || base == "i8") && be) bad(line, std::string(who) + ": 'be/le' لا تنطبق على " + base);
            f.kind = base[0] == 'f' ? "f" : "int"; f.sign = base[0] == 'i'; f.bits = std::atoi(base.c_str() + 1); f.be = be;
        }
        out.push_back(f);
    };
    for (char ch : spec) { if (ch == ' ' || ch == ',' || ch == '\t' || ch == '\n') flush(); else tok += ch; }
    flush();
    if (out.empty()) bad(line, std::string(who) + ": المواصفة فارغة");
    for (size_t i = 0; i + 1 < out.size(); i++) if (out[i].star) bad(line, std::string(who) + ": 'str*' يجب أن يكون آخر حقل");
    return out;
}
static size_t fieldSize(const Field& f) { return f.kind == "int" ? static_cast<size_t>(f.bits / 8) : f.kind == "f" ? static_cast<size_t>(f.bits / 8) : f.kind == "hex" ? f.n : f.n; }

static void putInt(std::string& o, uint64_t v, int bytes, bool be) {
    for (int i = 0; i < bytes; i++) { int sh = be ? (bytes - 1 - i) * 8 : i * 8; o += static_cast<char>((v >> sh) & 0xFF); }
}
static uint64_t getInt(const std::string& s, size_t at, int bytes, bool be) {
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) { uint64_t b = static_cast<unsigned char>(s[at + i]); int sh = be ? (bytes - 1 - i) * 8 : i * 8; v |= b << sh; }
    return v;
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void encodeOne(std::string& o, const Field& f, const Value& v, int line) {
    if (f.kind == "int") {
        if (v.type != Value::Type::NUMBER || std::floor(v.number) != v.number) bad(line, "bytes.encode: '" + f.name + "' يتطلب عدداً صحيحاً");
        double lo = f.sign ? -std::ldexp(1.0, f.bits - 1) : 0, hi = f.sign ? std::ldexp(1.0, f.bits - 1) - 1 : std::ldexp(1.0, f.bits) - 1;
        if (v.number < lo || v.number > hi) bad(line, "bytes.encode: القيمة " + v.toDisplayString() + " خارج مدى " + std::string(f.sign ? "i" : "u") + std::to_string(f.bits));
        uint64_t u = f.sign ? static_cast<uint64_t>(static_cast<int64_t>(v.number)) : static_cast<uint64_t>(v.number);
        putInt(o, u, f.bits / 8, f.be);
    } else if (f.kind == "f") {
        if (v.type != Value::Type::NUMBER) bad(line, "bytes.encode: '" + f.name + "' يتطلب رقماً");
        if (f.bits == 32) { float x = static_cast<float>(v.number); uint32_t u; std::memcpy(&u, &x, 4); putInt(o, u, 4, f.be); }
        else { double x = v.number; uint64_t u; std::memcpy(&u, &x, 8); putInt(o, u, 8, f.be); }
    } else if (f.kind == "str") {
        std::string s = v.type == Value::Type::STRING ? v.str : v.toDisplayString();
        if (f.star) { o += s; return; }
        if (s.size() > f.n) bad(line, "bytes.encode: النص أطول من str" + std::to_string(f.n));
        o += s; o.append(f.n - s.size(), '\0');
    } else if (f.kind == "hex") {
        if (v.type != Value::Type::STRING || v.str.size() != f.n * 2) bad(line, "bytes.encode: hex" + std::to_string(f.n) + " يتطلب نصاً سداسيّاً من " + std::to_string(f.n * 2) + " رمزاً");
        for (size_t i = 0; i < v.str.size(); i += 2) { int h = hexv(v.str[i]), l = hexv(v.str[i + 1]); if (h < 0 || l < 0) bad(line, "bytes.encode: رمز سداسي غير صالح"); o += static_cast<char>(h * 16 + l); }
    }
}

static Value decodeOne(const std::string& s, size_t& at, const Field& f, int line) {
    if (f.kind == "int") {
        int nb = f.bits / 8; uint64_t u = getInt(s, at, nb, f.be); at += static_cast<size_t>(nb);
        if (f.sign) { int64_t x = nb == 8 ? static_cast<int64_t>(u) : static_cast<int64_t>(u << (64 - f.bits)) >> (64 - f.bits); return mkNum(static_cast<double>(x)); }
        return mkNum(static_cast<double>(u));
    }
    if (f.kind == "f") {
        if (f.bits == 32) { uint32_t u = static_cast<uint32_t>(getInt(s, at, 4, f.be)); float x; std::memcpy(&x, &u, 4); at += 4; return mkNum(x); }
        uint64_t u = getInt(s, at, 8, f.be); double x; std::memcpy(&x, &u, 8); at += 8; return mkNum(x);
    }
    if (f.kind == "str") {
        size_t n = f.star ? s.size() - at : f.n; std::string t = s.substr(at, n); at += n;
        if (!f.star) { size_t z = t.find('\0'); if (z != std::string::npos) t.resize(z); }
        return mkStr(t);
    }
    if (f.kind == "hex") {
        static const char* d = "0123456789abcdef"; std::string t;
        for (size_t i = 0; i < f.n; i++) { unsigned char c = static_cast<unsigned char>(s[at + i]); t += d[c >> 4]; t += d[c & 15]; }
        at += f.n; return mkStr(t);
    }
    (void)line; return Value::nil();
}

// ------------------------------------------------------------------ dur.*
struct Unit { const char* name; double ms; };
static const Unit kUnits[] = {{"ms", 1}, {"s", 1000}, {"sec", 1000}, {"min", 60000}, {"m", 60000}, {"h", 3600000}, {"d", 86400000}, {"w", 604800000}};

static bool parseDur(const std::string& in, double& out) {
    std::string s; for (char c : in) if (c != ' ' && c != '_' && c != ',') s += c;
    if (s.empty()) return false;
    bool neg = false; size_t i = 0;
    if (s[i] == '-') { neg = true; i++; } else if (s[i] == '+') i++;
    if (i >= s.size()) return false;
    double total = 0;
    if (s[i] == 'P' || s[i] == 'p') {           // ISO-8601: P1DT2H30M5S
        i++; bool timePart = false; bool any = false;
        while (i < s.size()) {
            if (s[i] == 'T' || s[i] == 't') { timePart = true; i++; continue; }
            char* e; double v = std::strtod(s.c_str() + i, &e); if (e == s.c_str() + i) return false;
            i = static_cast<size_t>(e - s.c_str()); if (i >= s.size()) return false;
            char u = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i++])));
            double m = u == 'W' ? 604800000 : u == 'D' ? 86400000 : (u == 'H' && timePart) ? 3600000 : (u == 'M' && timePart) ? 60000 : (u == 'S' && timePart) ? 1000 : -1;
            if (m < 0) return false; total += v * m; any = true;
        }
        if (!any) return false;
        out = neg ? -total : total; return true;
    }
    bool any = false;
    while (i < s.size()) {
        char* e; double v = std::strtod(s.c_str() + i, &e); if (e == s.c_str() + i) return false;
        i = static_cast<size_t>(e - s.c_str());
        size_t j = i; while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) j++;
        std::string u = s.substr(i, j - i); i = j; double m = -1;
        if (u.empty()) { if (any || i < s.size()) return false; m = 1; }   // رقم بلا وحدة = ms
        else for (auto& x : kUnits) if (u == x.name) m = x.ms;
        if (m < 0) return false; total += v * m; any = true;
    }
    if (!any) return false;
    out = neg ? -total : total; return true;
}
static double durArg(const Value& v, const char* who, int line) {
    if (v.type == Value::Type::NUMBER) return v.number;
    double d; if (v.type == Value::Type::STRING && parseDur(v.str, d)) return d;
    bad(line, std::string(who) + ": مدة غير صالحة (رقم بالميلي ثانية أو نص مثل \"1h30m\")");
}
static std::string pad(long long v, int w) { std::string s = std::to_string(v < 0 ? -v : v); while (static_cast<int>(s.size()) < w) s = "0" + s; return s; }
struct DP { bool neg; long long d, h, m, s, ms; };
static DP split(double ms) {
    DP p; p.neg = ms < 0; long long t = static_cast<long long>(std::llround(std::fabs(ms)));
    p.ms = t % 1000; t /= 1000; p.s = t % 60; t /= 60; p.m = t % 60; t /= 60; p.h = t % 24; p.d = t / 24; return p;
}
static std::string durText(double ms) {
    DP p = split(ms); std::string o;
    auto add = [&](long long v, const char* u) { if (v) { if (!o.empty()) o += ' '; o += std::to_string(v) + u; } };
    add(p.d, "d"); add(p.h, "h"); add(p.m, "m"); add(p.s, "s"); add(p.ms, "ms");
    if (o.empty()) o = "0ms"; return (p.neg ? "-" : "") + o;
}

// ------------------------------------------------------------------ utf8
static bool isTrimCp(uint32_t c) {
    return c == 0 || (c >= 9 && c <= 13) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200B) ||
           c == 0x200E || c == 0x200F || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0x061C ||
           c == 0xFEFF || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}
static std::vector<uint32_t> decodeU(const std::string& s) {
    std::vector<uint32_t> o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]); size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + n > s.size()) n = 1;
        uint32_t cp = n == 1 ? c : (c & (0xFF >> (n + 1)));
        for (size_t k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        o.push_back(cp); i += n;
    }
    return o;
}
static void encU(std::string& o, uint32_t c) {
    if (c < 0x80) o += static_cast<char>(c);
    else if (c < 0x800) { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += static_cast<char>(0xE0 | (c >> 12)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else { o += static_cast<char>(0xF0 | (c >> 18)); o += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
}
static std::string encodeU(const std::vector<uint32_t>& v, size_t b, size_t e) { std::string o; for (size_t i = b; i < e; i++) encU(o, v[i]); return o; }
static uint32_t upCp(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool oddLower = (c < 0x139 || c > 0x148) && (c < 0x179); if (c >= 0x139 && c <= 0x148) return (c % 2 == 0) ? c - 1 : c; if (c >= 0x179) return (c % 2 == 0) ? c : c - 1;
        return (oddLower && c % 2 == 1) ? c - 1 : c;
    }
    if (c >= 0x3B1 && c <= 0x3C9) return c == 0x3C2 ? 0x3A3 : c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
static uint32_t lowCp(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
static std::string trimU(const std::string& s, bool left, bool right) {
    auto v = decodeU(s); size_t b = 0, e = v.size();
    if (left) while (b < e && isTrimCp(v[b])) b++;
    if (right) while (e > b && isTrimCp(v[e - 1])) e--;
    return encodeU(v, b, e);
}
} // namespace x13

void Interpreter::registerNativesExtra13() {
    using namespace x13;
    auto range = [](const char* fn, Args& a, size_t lo, size_t hi, int line) { expectArgsRange(fn, a, lo, hi, line); };

    // ---------------------------------------------------------------- bytes.*
    natives["bytes.encode"] = [](Args& a, int line) -> Value {
        if (a.size() < 2) bad(line, "bytes.encode: (spec، قيم... | قاموس)");
        auto fs = parseSpec(asString(a[0], "bytes.encode", line), line, "bytes.encode");
        std::string o; size_t vi = 1;
        const bool byName = a.size() == 2 && a[1].type == Value::Type::MAP && a[1].map;
        for (auto& f : fs) {
            if (f.kind == "pad") { o.append(f.n * f.rep, '\0'); continue; }
            for (size_t r = 0; r < f.rep; r++) {
                Value v;
                if (byName) {
                    if (f.name.empty()) bad(line, "bytes.encode: الإدخال بقاموس يتطلب اسماً لكل حقل (name:type)");
                    const Value* p = a[1].map->findValue(mkStr(f.name));
                    if (!p) bad(line, "bytes.encode: الحقل '" + f.name + "' غير موجود في القاموس");
                    if (f.rep > 1) { if (p->type != Value::Type::ARRAY || !p->array || p->array->size() != f.rep) bad(line, "bytes.encode: '" + f.name + "' يتطلب مصفوفة من " + std::to_string(f.rep)); v = (*p->array)[r]; }
                    else v = *p;
                } else {
                    if (vi >= a.size()) bad(line, "bytes.encode: قيم أقل من حقول المواصفة");
                    v = a[vi++];
                }
                encodeOne(o, f, v, line);
            }
        }
        if (!byName && vi != a.size()) bad(line, "bytes.encode: قيم أكثر من حقول المواصفة");
        return mkStr(o);
    };
    natives["bytes.decode"] = [](Args& a, int line) -> Value {
        if (a.size() < 2 || a.size() > 3) bad(line, "bytes.decode: (spec، بايتات، إزاحة?)");
        auto fs = parseSpec(asString(a[0], "bytes.decode", line), line, "bytes.decode");
        std::string s = asString(a[1], "bytes.decode", line);
        size_t at = a.size() == 3 ? static_cast<size_t>(std::max(0.0, asNumber(a[2], "bytes.decode", line))) : 0;
        Value out = mkMap(); int anon = 0;
        for (auto& f : fs) {
            size_t need = f.star ? 0 : fieldSize(f) * f.rep;
            if (at + need > s.size()) bad(line, "bytes.decode: البيانات قصيرة — يلزم " + std::to_string(need) + " بايت عند الموضع " + std::to_string(at) + " (المتوفر " + std::to_string(s.size() - std::min(at, s.size())) + ")");
            if (f.kind == "pad") { at += need; continue; }
            std::string key = f.name.empty() ? "_" + std::to_string(++anon) : f.name;
            if (f.rep == 1) out.map->put(mkStr(key), decodeOne(s, at, f, line));
            else { auto arr = std::make_shared<ArrayData>(); for (size_t r = 0; r < f.rep; r++) arr->push_back(decodeOne(s, at, f, line)); out.map->put(mkStr(key), Value::makeArray(arr)); }
        }
        out.map->put(mkStr("_end"), mkNum(static_cast<double>(at)));   // الموضع بعد آخر حقل (لقراءة ما يليه)
        return out;
    };
    natives["bytes.size"] = [](Args& a, int line) -> Value {
        if (a.size() != 1) bad(line, "bytes.size: (spec)");
        double n = 0;
        for (auto& f : parseSpec(asString(a[0], "bytes.size", line), line, "bytes.size")) {
            if (f.star) bad(line, "bytes.size: الحجم غير ثابت مع 'str*'");
            n += static_cast<double>(fieldSize(f) * f.rep);
        }
        return mkNum(n);
    };

    // ---------------------------------------------------------------- dur.*
    natives["dur.parse"] = [](Args& a, int line) -> Value {
        if (a.size() != 1) bad(line, "dur.parse: (نص)");
        double d; if (a[0].type != Value::Type::STRING || !parseDur(a[0].str, d)) bad(line, "dur.parse: مدة غير صالحة \"" + a[0].toDisplayString() + "\" (مثل 1h30m · 90s · 250ms · PT1H30M)");
        return mkNum(d);
    };
    natives["dur.valid"] = [](Args& a, int line) -> Value { if (a.size() != 1) bad(line, "dur.valid: (نص)"); double d; return Value::boolean_(a[0].type == Value::Type::STRING && parseDur(a[0].str, d)); };
    natives["dur.of"] = [](Args& a, int line) -> Value {
        if (a.size() != 1 || a[0].type != Value::Type::MAP || !a[0].map) bad(line, "dur.of: (قاموس {w,d,h,m,s,ms})");
        double t = 0;
        for (auto& kv : *a[0].map) {
            std::string k = kv.first.toDisplayString(); double m = -1;
            for (auto& u : kUnits) if (k == u.name) m = u.ms;
            if (m < 0) bad(line, "dur.of: وحدة غير معروفة '" + k + "'");
            t += asNumber(kv.second, "dur.of", line) * m;
        }
        return mkNum(t);
    };
    natives["dur.parts"] = [](Args& a, int line) -> Value {
        if (a.size() != 1) bad(line, "dur.parts: (مدة)");
        double ms = durArg(a[0], "dur.parts", line); DP p = split(ms); Value m = mkMap();
        m.map->put(mkStr("negative"), Value::boolean_(p.neg));
        m.map->put(mkStr("d"), mkNum(static_cast<double>(p.d))); m.map->put(mkStr("h"), mkNum(static_cast<double>(p.h)));
        m.map->put(mkStr("m"), mkNum(static_cast<double>(p.m))); m.map->put(mkStr("s"), mkNum(static_cast<double>(p.s)));
        m.map->put(mkStr("ms"), mkNum(static_cast<double>(p.ms))); m.map->put(mkStr("totalMs"), mkNum(ms));
        return m;
    };
    natives["dur.text"] = [](Args& a, int line) -> Value { if (a.size() != 1) bad(line, "dur.text: (مدة)"); return mkStr(durText(durArg(a[0], "dur.text", line))); };
    natives["dur.fmt"] = [](Args& a, int line) -> Value {
        if (a.size() != 2) bad(line, "dur.fmt: (مدة، صيغة)");
        double ms = durArg(a[0], "dur.fmt", line); std::string pat = asString(a[1], "dur.fmt", line); DP p = split(ms);
        long long tms = static_cast<long long>(std::llround(std::fabs(ms)));
        struct R { const char* k; std::string v; } rep[] = {
            {"{d}", std::to_string(p.d)}, {"{hh}", pad(p.h, 2)}, {"{h}", std::to_string(p.h)}, {"{mm}", pad(p.m, 2)}, {"{m}", std::to_string(p.m)},
            {"{ss}", pad(p.s, 2)}, {"{s}", std::to_string(p.s)}, {"{ms3}", pad(p.ms, 3)}, {"{ms}", std::to_string(p.ms)},
            {"{totalH}", std::to_string(tms / 3600000)}, {"{totalM}", std::to_string(tms / 60000)}, {"{totalS}", std::to_string(tms / 1000)}, {"{sign}", p.neg ? "-" : ""}};
        std::string o;
        for (size_t i = 0; i < pat.size();) {
            bool hit = false;
            if (pat[i] == '{') for (auto& r : rep) { size_t n = std::strlen(r.k); if (pat.compare(i, n, r.k) == 0) { o += r.v; i += n; hit = true; break; } }
            if (!hit) o += pat[i++];
        }
        return mkStr(o);
    };
    natives["dur.add"] = [](Args& a, int line) -> Value { if (a.size() < 2) bad(line, "dur.add: (مدة، مدة، ...)"); double t = 0; for (auto& v : a) t += durArg(v, "dur.add", line); return mkNum(t); };
    natives["dur.sub"] = [](Args& a, int line) -> Value { if (a.size() != 2) bad(line, "dur.sub: (أ، ب)"); return mkNum(durArg(a[0], "dur.sub", line) - durArg(a[1], "dur.sub", line)); };
    natives["dur.scale"] = [](Args& a, int line) -> Value { if (a.size() != 2) bad(line, "dur.scale: (مدة، معامل)"); return mkNum(durArg(a[0], "dur.scale", line) * asNumber(a[1], "dur.scale", line)); };
    natives["dur.abs"] = [](Args& a, int line) -> Value { if (a.size() != 1) bad(line, "dur.abs: (مدة)"); return mkNum(std::fabs(durArg(a[0], "dur.abs", line))); };
    natives["dur.cmp"] = [](Args& a, int line) -> Value {
        if (a.size() != 2) bad(line, "dur.cmp: (أ، ب)");
        double x = durArg(a[0], "dur.cmp", line), y = durArg(a[1], "dur.cmp", line); return mkNum(x < y ? -1 : x > y ? 1 : 0);
    };

    // ---------------------------------------------------------------- firstOf / lastOf / firstKey / lastKey / findKeyIf
    auto items = [](const Value& v, const char* fn, int line) -> std::vector<std::pair<Value, Value>> {
        std::vector<std::pair<Value, Value>> o;
        if (v.type == Value::Type::ARRAY && v.array) { for (size_t i = 0; i < v.array->size(); i++) o.push_back({mkNum(static_cast<double>(i)), (*v.array)[i]}); }
        else if (v.type == Value::Type::MAP && v.map) { for (auto& kv : *v.map) o.push_back({kv.first, kv.second}); }
        else if (v.type == Value::Type::SET && v.set) { for (size_t i = 0; i < v.set->items.size(); i++) o.push_back({mkNum(static_cast<double>(i)), v.set->items[i]}); }
        else throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "' يتطلب مصفوفة أو قاموساً أو مجموعة");
        return o;
    };
    natives["firstOf"] = [=](Args& a, int line) -> Value { range("firstOf", a, 1, 2, line); auto it = items(a[0], "firstOf", line); return it.empty() ? (a.size() == 2 ? a[1] : Value::nil()) : it.front().second; };
    natives["lastOf"]  = [=](Args& a, int line) -> Value { range("lastOf", a, 1, 2, line); auto it = items(a[0], "lastOf", line); return it.empty() ? (a.size() == 2 ? a[1] : Value::nil()) : it.back().second; };
    natives["firstKey"] = [=](Args& a, int line) -> Value { range("firstKey", a, 1, 1, line); auto it = items(a[0], "firstKey", line); return it.empty() ? Value::nil() : it.front().first; };
    natives["lastKey"]  = [=](Args& a, int line) -> Value { range("lastKey", a, 1, 1, line); auto it = items(a[0], "lastKey", line); return it.empty() ? Value::nil() : it.back().first; };
    natives["findKeyIf"] = [=, this](Args& a, int line) -> Value {
        range("findKeyIf", a, 2, 2, line);
        if (a[1].type != Value::Type::FUNCTION || !a[1].function) throw diagErr(diag::Code::E0004_InvalidType, line, "'findKeyIf' يتطلب دالة كوسيط ثانٍ");
        const bool two = a[1].function->declaration && a[1].function->declaration->params.size() >= 2;   // fun(قيمة, مفتاح) اختيارياً
        for (auto& kv : items(a[0], "findKeyIf", line)) {
            std::vector<Value> args{kv.second}; if (two) args.push_back(kv.first);
            if (callFunction(a[1].function, args, line).isTruthy()) return kv.first;
        }
        return Value::nil();
    };

    // ---------------------------------------------------------------- utf8Trim / CapFirst / Pad
    natives["utf8Trim"]      = [=](Args& a, int line) -> Value { range("utf8Trim", a, 1, 1, line); return mkStr(trimU(asString(a[0], "utf8Trim", line), true, true)); };
    natives["utf8TrimStart"] = [=](Args& a, int line) -> Value { range("utf8TrimStart", a, 1, 1, line); return mkStr(trimU(asString(a[0], "utf8TrimStart", line), true, false)); };
    natives["utf8TrimEnd"]   = [=](Args& a, int line) -> Value { range("utf8TrimEnd", a, 1, 1, line); return mkStr(trimU(asString(a[0], "utf8TrimEnd", line), false, true)); };
    natives["utf8CapFirst"]  = [=](Args& a, int line) -> Value { range("utf8CapFirst", a, 1, 1, line); auto v = decodeU(asString(a[0], "utf8CapFirst", line)); if (!v.empty()) v[0] = upCp(v[0]); return mkStr(encodeU(v, 0, v.size())); };
    natives["utf8LowFirst"]  = [=](Args& a, int line) -> Value { range("utf8LowFirst", a, 1, 1, line); auto v = decodeU(asString(a[0], "utf8LowFirst", line)); if (!v.empty()) v[0] = lowCp(v[0]); return mkStr(encodeU(v, 0, v.size())); };
    natives["utf8Pad"] = [=](Args& a, int line) -> Value {
        range("utf8Pad", a, 2, 4, line);
        auto v = decodeU(asString(a[0], "utf8Pad", line)); double w = asNumber(a[1], "utf8Pad", line);
        auto fill = decodeU(a.size() > 2 ? asString(a[2], "utf8Pad", line) : " "); std::string mode = a.size() > 3 ? asString(a[3], "utf8Pad", line) : "end";
        if (fill.empty()) bad(line, "utf8Pad: الحشو فارغ");
        if (mode != "end" && mode != "start" && mode != "both") bad(line, "utf8Pad: النمط يجب أن يكون end أو start أو both");
        long total = static_cast<long>(w) - static_cast<long>(v.size());
        if (total <= 0) return a[0];
        long left = mode == "start" ? total : mode == "both" ? total / 2 : 0, right = total - left;
        std::vector<uint32_t> o;
        for (long i = 0; i < left; i++) o.push_back(fill[static_cast<size_t>(i) % fill.size()]);
        o.insert(o.end(), v.begin(), v.end());
        for (long i = 0; i < right; i++) o.push_back(fill[static_cast<size_t>(i) % fill.size()]);
        return mkStr(encodeU(o, 0, o.size()));
    };
}

} // namespace rin
