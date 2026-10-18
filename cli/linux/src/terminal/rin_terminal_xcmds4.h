// ============================================================================
//  rin terminal — batch 4 (part 1/4): framework + units + text tools.
//  Header-only, POSIX + std only. No process is spawned (works on Android).
//  Style:  $ cmd args …   * globs   ~/   A:B ranges      (output text is English)
//
//  Batch 4 (500 commands) is split in four headers so each stays reviewable:
//    rin_terminal_xcmds4.h  framework, units (convert + shortcuts), text tools
//    rin_terminal_xcmds5.h  encodings, hashes, colors, network/reference tables
//    rin_terminal_xcmds6.h  math, statistics, finance, geometry, dates
//    rin_terminal_xcmds7.h  generators, file tools, Rin source tools, data tools, registry4()
// ============================================================================
#pragma once

#include "rin_terminal_xcmds3.h"

#include <deque>
#include <numeric>

namespace rin {
namespace terminal {
namespace xcmds {

// ----------------------------------------------------------------------------
// registration builder (names/usages are heap-stable so Spec can keep const char*)
// ----------------------------------------------------------------------------
struct Reg {
    std::vector<Entry> v;
    std::deque<std::string> keep;
    const char* K(const std::string& s) { keep.push_back(s); return keep.back().c_str(); }
    void add(const std::string& name, const std::string& usage, const std::string& desc, bool files, Fn fn) {
        v.push_back({{K(name), K(usage), K(desc), files}, std::move(fn)});
    }
};

// ----------------------------------------------------------------------------
// shared helpers for batch 4
// ----------------------------------------------------------------------------
inline std::string fnum(double v, int prec = 12) {
    if (v == 0) v = 0;                       // no "-0"
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char b[64];
    std::snprintf(b, sizeof(b), "%.*g", prec, v);
    return b;
}
// text operand: `-f FILE`, or all words joined by one space (raw, unexpanded; \n \t escapes decoded)
inline bool textIn(Ctx& c, std::string& out, size_t skip = 0, bool needNonEmpty = true) {
    out.clear();
    if (c.toks.size() > skip && c.toks[skip].s == "-f") {
        if (c.toks.size() <= skip + 1) { c.fail("option requires a file name -- 'f'"); return false; }
        std::string why;
        if (!readFileCapped(c.toks[skip + 1].s, out, why)) { c.fail("cannot read '" + c.toks[skip + 1].s + "': " + why); return false; }
        return true;
    }
    for (size_t i = skip; i < c.toks.size(); ++i) { if (i > skip) out += ' '; out += c.toks[i].s; }
    out = decodeEsc(out);
    if (needNonEmpty && out.empty()) { c.fail(std::string("missing text   (usage: $ ") + c.name + " <text…>   or   -f FILE)"); return false; }
    return true;
}
inline bool parseNum(const std::string& s, double& out) {
    if (s.empty()) return false;
    std::string t;
    for (char ch : s) if (ch != '_' && ch != ',') t += ch;     // 1_000_000 and 1,000
    char* e = nullptr;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { out = static_cast<double>(std::strtoull(t.c_str() + 2, &e, 16)); return e && !*e; }
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B')) { out = static_cast<double>(std::strtoull(t.c_str() + 2, &e, 2)); return e && !*e; }
    out = std::strtod(t.c_str(), &e);
    return e && !*e && std::isfinite(out);
}
// numbers from the words (or -f FILE); whitespace/comma separated
inline bool numsIn(Ctx& c, std::vector<double>& out, size_t minCount = 1) {
    std::string t;
    if (!textIn(c, t, 0, false)) return false;
    std::string cur;
    auto flush = [&]() -> bool {
        if (cur.empty()) return true;
        double d;
        if (!parseNum(cur, d)) { c.fail("not a number: '" + cur + "'"); return false; }
        out.push_back(d); cur.clear(); return true;
    };
    for (char ch : t) { if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ';') { if (!flush()) return false; } else cur += ch; }
    if (!flush()) return false;
    if (out.size() < minCount) { c.fail("expected " + std::to_string(minCount) + " number(s), got " + std::to_string(out.size())); return false; }
    return true;
}
inline std::vector<std::string> wordsOf(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char ch : s) { if (std::isspace(static_cast<unsigned char>(ch))) { if (!cur.empty()) { w.push_back(cur); cur.clear(); } } else cur += ch; }
    if (!cur.empty()) w.push_back(cur);
    return w;
}
inline std::string joinStr(const std::vector<std::string>& v, const std::string& sep) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; }
    return o;
}
inline std::vector<std::string> u8chars(const std::string& s) {
    std::vector<std::string> v;
    for (size_t i = 0; i < s.size();) {
        size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        v.push_back(s.substr(i, j - i));
        i = j;
    }
    return v;
}
inline uint32_t u8cp(const std::string& ch) {
    unsigned char b0 = static_cast<unsigned char>(ch[0]);
    if (b0 < 0x80 || ch.size() == 1) return b0;
    if (ch.size() == 2) return ((b0 & 0x1Fu) << 6) | (static_cast<unsigned char>(ch[1]) & 0x3Fu);
    if (ch.size() == 3) return ((b0 & 0x0Fu) << 12) | ((static_cast<unsigned char>(ch[1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(ch[2]) & 0x3Fu);
    return ((b0 & 0x07u) << 18) | ((static_cast<unsigned char>(ch[1]) & 0x3Fu) << 12) | ((static_cast<unsigned char>(ch[2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(ch[3]) & 0x3Fu);
}
inline std::string cpToU8(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    return o;
}
inline std::string lowerStr(std::string s) { for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; }
inline std::string upperStr(std::string s) { for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return s; }

using TextFn = std::function<std::string(const std::string&)>;
// text -> text command:   $ name <text…>   |   $ name -f FILE
inline void addText(Reg& r, const std::string& name, const std::string& desc, TextFn f, const std::string& args = "") {
    r.add(name, "$ " + name + (args.empty() ? "" : " " + args) + " <text…|-f FILE>", desc, true, [f](Ctx& c) -> int {
        std::string t;
        if (!textIn(c, t)) return 1;
        std::string o = f(t);
        c.say(o);
        return 0;
    });
}
// params + text:  $ name P1 [P2…] <text…>
inline void addParamText(Reg& r, const std::string& name, const std::string& usageParams, const std::string& desc, size_t nparams,
                         std::function<std::string(const std::vector<std::string>&, const std::string&)> f) {
    r.add(name, "$ " + name + " " + usageParams + " <text…|-f FILE>", desc, true, [f, nparams, name](Ctx& c) -> int {
        if (c.toks.size() < nparams + 1) return c.fail("usage: $ " + name + " …   (needs " + std::to_string(nparams) + " parameter(s) then text)");
        std::vector<std::string> p;
        for (size_t i = 0; i < nparams; ++i) p.push_back(c.toks[i].s);
        std::string t;
        if (!textIn(c, t, nparams)) return 1;
        c.say(f(p, t));
        return 0;
    });
}
// numbers -> text:   $ name <numbers…>
inline void addNums(Reg& r, const std::string& name, const std::string& desc, size_t minCount, std::function<std::string(const std::vector<double>&)> f,
                    const std::string& argHint = "<numbers…>") {
    r.add(name, "$ " + name + " " + argHint, desc, false, [f, minCount](Ctx& c) -> int {
        std::vector<double> v;
        if (!numsIn(c, v, minCount)) return 1;
        c.say(f(v));
        return 0;
    });
}
// one number -> one line each
inline void addNum1(Reg& r, const std::string& name, const std::string& desc, std::function<std::string(double)> f) {
    addNums(r, name, desc, 1, [f](const std::vector<double>& v) {
        std::string o;
        for (size_t i = 0; i < v.size(); ++i) { if (i) o += '\n'; o += f(v[i]); }
        return o;
    }, "<number…>");
}

// ----------------------------------------------------------------------------
// A. units
// ----------------------------------------------------------------------------
struct UnitDef { const char* name; const char* dim; double f; };   // f: factor to the dimension's base unit
inline const std::vector<UnitDef>& unitTable() {
    static const std::vector<UnitDef> t = {
        // length (m)
        {"m", "length", 1}, {"km", "length", 1000}, {"cm", "length", 0.01}, {"mm", "length", 0.001}, {"um", "length", 1e-6}, {"nm", "length", 1e-9},
        {"mi", "length", 1609.344}, {"yd", "length", 0.9144}, {"ft", "length", 0.3048}, {"in", "length", 0.0254}, {"nmi", "length", 1852},
        {"ly", "length", 9460730472580800.0}, {"au", "length", 149597870700.0},
        // mass (kg)
        {"kg", "mass", 1}, {"g", "mass", 0.001}, {"mg", "mass", 1e-6}, {"ug", "mass", 1e-9}, {"t", "mass", 1000}, {"lb", "mass", 0.45359237},
        {"oz", "mass", 0.028349523125}, {"st", "mass", 6.35029318}, {"ct", "mass", 0.0002},
        // volume (l)
        {"l", "volume", 1}, {"ml", "volume", 0.001}, {"m3", "volume", 1000}, {"cm3", "volume", 0.001}, {"gal", "volume", 3.785411784}, {"qt", "volume", 0.946352946},
        {"pt", "volume", 0.473176473}, {"cup", "volume", 0.2365882365}, {"floz", "volume", 0.0295735295625}, {"tbsp", "volume", 0.01478676478125},
        {"tsp", "volume", 0.00492892159375}, {"galuk", "volume", 4.54609},
        // area (m2)
        {"sqm", "area", 1}, {"sqkm", "area", 1e6}, {"sqcm", "area", 1e-4}, {"ha", "area", 1e4}, {"acre", "area", 4046.8564224}, {"sqft", "area", 0.09290304},
        {"sqin", "area", 0.00064516}, {"sqmi", "area", 2589988.110336}, {"sqyd", "area", 0.83612736},
        // speed (m/s)
        {"mps", "speed", 1}, {"kmh", "speed", 1.0 / 3.6}, {"mph", "speed", 0.44704}, {"kn", "speed", 1852.0 / 3600.0}, {"fps", "speed", 0.3048},
        // time (s)
        {"sec", "time", 1}, {"msec", "time", 1e-3}, {"usec", "time", 1e-6}, {"min", "time", 60}, {"hr", "time", 3600}, {"day", "time", 86400}, {"wk", "time", 604800},
        {"yr", "time", 31557600},
        // data (byte)
        {"byte", "data", 1}, {"kb", "data", 1e3}, {"mb", "data", 1e6}, {"gb", "data", 1e9}, {"tb", "data", 1e12}, {"kib", "data", 1024}, {"mib", "data", 1048576},
        {"gib", "data", 1073741824.0}, {"tib", "data", 1099511627776.0}, {"bit", "data", 0.125}, {"kbit", "data", 125}, {"mbit", "data", 125000}, {"gbit", "data", 125000000},
        // pressure (Pa)
        {"pa", "pressure", 1}, {"kpa", "pressure", 1000}, {"mpa", "pressure", 1e6}, {"bar", "pressure", 1e5}, {"mbar", "pressure", 100}, {"atm", "pressure", 101325},
        {"psi", "pressure", 6894.757293168}, {"mmhg", "pressure", 133.322387415}, {"torr", "pressure", 101325.0 / 760.0}, {"inhg", "pressure", 3386.388640341},
        // energy (J)
        {"j", "energy", 1}, {"kj", "energy", 1000}, {"mj", "energy", 1e6}, {"cal", "energy", 4.184}, {"kcal", "energy", 4184}, {"wh", "energy", 3600}, {"kwh", "energy", 3.6e6},
        {"btu", "energy", 1055.05585262}, {"ev", "energy", 1.602176634e-19}, {"ftlb", "energy", 1.3558179483314},
        // power (W)
        {"w", "power", 1}, {"kw", "power", 1000}, {"mw", "power", 1e6}, {"hp", "power", 745.69987158227}, {"ps", "power", 735.49875}, {"btuh", "power", 0.29307107},
        // angle (rad)
        {"rad", "angle", 1}, {"deg", "angle", M_PI / 180.0}, {"grad", "angle", M_PI / 200.0}, {"turn", "angle", 2 * M_PI}, {"arcmin", "angle", M_PI / 10800.0},
        {"arcsec", "angle", M_PI / 648000.0},
        // force (N)
        {"n", "force", 1}, {"kn_", "force", 1000}, {"lbf", "force", 4.4482216152605}, {"kgf", "force", 9.80665}, {"dyn", "force", 1e-5},
        // frequency (Hz)
        {"hz", "frequency", 1}, {"khz", "frequency", 1e3}, {"mhz", "frequency", 1e6}, {"ghz", "frequency", 1e9}, {"rpm", "frequency", 1.0 / 60.0},
        // temperature (handled specially)
        {"c", "temp", 1}, {"f", "temp", 1}, {"k", "temp", 1}, {"r", "temp", 1},
    };
    return t;
}
inline const UnitDef* findUnit(const std::string& n) {
    std::string l = lowerStr(n);
    if (l == "kn_" ) return nullptr;
    for (const auto& u : unitTable()) if (l == u.name) return &u;
    if (l == "knt" || l == "knot" || l == "knots") return findUnit("kn");
    if (l == "celsius") return findUnit("c");
    if (l == "fahrenheit") return findUnit("f");
    if (l == "kelvin") return findUnit("k");
    if (l == "kilonewton") { for (const auto& u : unitTable()) if (std::string(u.name) == "kn_") return &u; }
    if (l == "newton") return findUnit("n");
    if (l == "inch" || l == "inches") return findUnit("in");
    if (l == "feet" || l == "foot") return findUnit("ft");
    if (l == "mile" || l == "miles") return findUnit("mi");
    if (l == "pound" || l == "pounds" || l == "lbs") return findUnit("lb");
    if (l == "ounce") return findUnit("oz");
    if (l == "liter" || l == "litre" || l == "liters") return findUnit("l");
    return nullptr;
}
inline bool convertUnits(double v, const std::string& from, const std::string& to, double& out, std::string& err) {
    const UnitDef* a = findUnit(from);
    const UnitDef* b = findUnit(to);
    if (!a) { err = "unknown unit '" + from + "'   (see: $ units)"; return false; }
    if (!b) { err = "unknown unit '" + to + "'   (see: $ units)"; return false; }
    if (std::string(a->dim) != b->dim) { err = std::string("cannot convert ") + a->dim + " to " + b->dim; return false; }
    if (std::string(a->dim) == "temp") {
        auto toC = [](char u, double x) { switch (u) { case 'c': return x; case 'f': return (x - 32) * 5.0 / 9.0; case 'k': return x - 273.15; default: return (x - 491.67) * 5.0 / 9.0; } };
        double cc = toC(a->name[0], v);
        switch (b->name[0]) { case 'c': out = cc; break; case 'f': out = cc * 9.0 / 5.0 + 32; break; case 'k': out = cc + 273.15; break; default: out = (cc + 273.15) * 9.0 / 5.0; }
        return true;
    }
    out = v * a->f / b->f;
    return true;
}
inline void addUnits(Reg& r) {
    r.add("convert", "$ convert <value> <from> <to>   e.g. $ convert 5 km mi", "convert between ~100 units (length mass volume area speed time data pressure energy power angle force frequency temperature)", false,
          [](Ctx& c) -> int {
              if (c.toks.size() != 3) return c.fail("usage: $ convert <value> <from> <to>   e.g. $ convert 72 f c");
              double v;
              if (!parseNum(c.toks[0].s, v)) return c.fail("not a number: '" + c.toks[0].s + "'");
              double o; std::string err;
              if (!convertUnits(v, c.toks[1].s, c.toks[2].s, o, err)) return c.fail(err);
              c.say(fnum(v) + " " + c.toks[1].s + " = " + fnum(o, 10) + " " + c.toks[2].s);
              return 0;
          });
    r.add("units", "$ units [dimension]", "list the units known to $ convert (by dimension)", false, [](Ctx& c) -> int {
        std::string want = c.toks.empty() ? "" : lowerStr(c.toks[0].s);
        std::map<std::string, std::vector<std::string>> by;
        std::vector<std::string> order;
        for (const auto& u : unitTable()) {
            std::string nm = std::string(u.name) == "kn_" ? "kN" : u.name;
            if (!by.count(u.dim)) order.push_back(u.dim);
            by[u.dim].push_back(nm);
        }
        bool any = false;
        for (const auto& d : order) { if (!want.empty() && want != d) continue; any = true; c.say(d + ": " + joinStr(by[d], " ")); }
        return any ? 0 : c.fail("unknown dimension '" + want + "'");
    });
    // shortcuts:  $ km2mi 5 10 20  →  "5 km = 3.10685596119 mi" …
    static const char* pairs[] = {
        // length
        "km2mi", "mi2km", "m2ft", "ft2m", "cm2in", "in2cm", "m2yd", "yd2m", "km2m", "m2km", "cm2m", "m2cm", "mm2in", "in2mm", "nmi2km", "km2nmi", "ly2km", "au2km", "ft2in", "in2ft", "mi2ft", "ft2mi",
        // mass
        "kg2lb", "lb2kg", "g2oz", "oz2g", "kg2g", "g2kg", "t2kg", "kg2t", "st2kg", "kg2st", "lb2oz", "oz2lb", "mg2g", "g2mg", "ct2g",
        // volume
        "l2gal", "gal2l", "ml2floz", "floz2ml", "l2ml", "ml2l", "cup2ml", "ml2cup", "tbsp2ml", "tsp2ml", "l2qt", "qt2l", "pt2l", "l2pt", "gal2galuk", "galuk2gal", "m32l", "l2m3",
        // area
        "sqm2sqft", "sqft2sqm", "ha2acre", "acre2ha", "sqkm2sqmi", "sqmi2sqkm", "sqm2ha", "ha2sqm", "acre2sqm", "sqcm2sqin", "sqin2sqcm",
        // speed
        "kmh2mph", "mph2kmh", "kmh2mps", "mps2kmh", "mph2mps", "mps2mph", "kn2kmh", "kmh2kn", "mps2kn", "kn2mph",
        // temperature
        "c2f", "f2c", "c2k", "k2c", "f2k", "k2f",
        // time
        "sec2min", "min2sec", "min2hr", "hr2min", "hr2day", "day2hr", "day2wk", "wk2day", "day2yr", "yr2day", "sec2hr", "hr2sec", "day2sec",
        // data
        "mb2gb", "gb2mb", "gb2tb", "tb2gb", "kb2mb", "mb2kb", "byte2kb", "kb2byte", "mb2byte", "byte2mb", "gib2mib", "mib2gib", "mib2kib", "kib2mib", "gb2gib", "gib2gb", "mb2mib", "mib2mb",
        "bit2byte", "byte2bit", "mbit2mb", "mb2mbit",
        // pressure
        "psi2bar", "bar2psi", "atm2pa", "pa2atm", "atm2psi", "psi2atm", "bar2kpa", "kpa2bar", "mmhg2kpa", "kpa2mmhg",
        // energy / power
        "kcal2kj", "kj2kcal", "cal2j", "j2cal", "kwh2j", "j2kwh", "kwh2kcal", "kcal2kwh", "ev2j", "btu2kj", "kj2btu", "hp2kw", "kw2hp", "w2hp", "hp2w", "btuh2w",
        // angle / force / frequency
        "deg2rad", "rad2deg", "deg2grad", "grad2deg", "deg2turn", "turn2deg", "lbf2n", "n2lbf", "kgf2n", "n2kgf", "rpm2hz", "hz2rpm",
    };
    for (const char* p : pairs) {
        std::string name = p;
        size_t k = std::string::npos;
        // split "a2b" at a '2' such that both sides are known units (m2ft: "m" "ft"; m32l: "m3" "l")
        for (size_t i = 1; i + 1 < name.size(); ++i)
            if (name[i] == '2' && findUnit(name.substr(0, i)) && findUnit(name.substr(i + 1))) { k = i; break; }
        if (k == std::string::npos) continue;
        std::string from = name.substr(0, k), to = name.substr(k + 1);
        r.add(name, "$ " + name + " <number…>", "convert " + from + " to " + to + " (shortcut for $ convert <n> " + from + " " + to + ")", false, [from, to](Ctx& c) -> int {
            std::vector<double> v;
            if (!numsIn(c, v)) return 1;
            for (double x : v) {
                double o; std::string err;
                if (!convertUnits(x, from, to, o, err)) return c.fail(err);
                c.say(fnum(x) + " " + from + " = " + fnum(o, 10) + " " + to);
            }
            return 0;
        });
    }
}

// ----------------------------------------------------------------------------
// B. text tools
// ----------------------------------------------------------------------------
inline std::vector<std::string> splitIdent(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    auto flush = [&]() { if (!cur.empty()) { w.push_back(cur); cur.clear(); } };
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (std::isalnum(ch) || ch >= 0x80) {
            bool boundary = i > 0 && std::isupper(ch) && (std::islower(static_cast<unsigned char>(s[i - 1])) || std::isdigit(static_cast<unsigned char>(s[i - 1])) ||
                                                           (i + 1 < s.size() && std::islower(static_cast<unsigned char>(s[i + 1])) && std::isupper(static_cast<unsigned char>(s[i - 1]))));
            if (boundary) flush();
            cur += s[i];
        } else flush();
    }
    flush();
    return w;
}
inline std::string capWord(const std::string& w) { std::string o = lowerStr(w); if (!o.empty()) o[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(o[0]))); return o; }
inline std::string mapLines(const std::string& t, const std::function<std::string(const std::string&)>& f) {
    std::string o;
    auto ls = textLines(t);
    for (size_t i = 0; i < ls.size(); ++i) { if (i) o += '\n'; o += f(ls[i]); }
    return o;
}
inline std::string romanOf(long n) {
    static const std::pair<int, const char*> T[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    std::string o;
    for (const auto& p : T) while (n >= p.first) { o += p.second; n -= p.first; }
    return o;
}
inline long unRoman(const std::string& s) {
    auto val = [](char ch) -> int { switch (std::toupper(static_cast<unsigned char>(ch))) { case 'I': return 1; case 'V': return 5; case 'X': return 10; case 'L': return 50; case 'C': return 100; case 'D': return 500; case 'M': return 1000; default: return -1; } };
    long t = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        int a = val(s[i]);
        if (a < 0) return -1;
        int b = i + 1 < s.size() ? val(s[i + 1]) : 0;
        if (b < 0) return -1;
        t += a < b ? -a : a;
    }
    return t;
}
inline std::string numToWords(long long n) {
    static const char* ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char* tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n < 0) return "minus " + numToWords(-n);
    if (n < 20) return ones[n];
    if (n < 100) return std::string(tens[n / 10]) + (n % 10 ? "-" + std::string(ones[n % 10]) : "");
    if (n < 1000) return std::string(ones[n / 100]) + " hundred" + (n % 100 ? " " + numToWords(n % 100) : "");
    static const std::pair<long long, const char*> S[] = {{1000000000000LL, "trillion"}, {1000000000LL, "billion"}, {1000000LL, "million"}, {1000LL, "thousand"}};
    for (const auto& p : S) if (n >= p.first) return numToWords(n / p.first) + " " + p.second + (n % p.first ? " " + numToWords(n % p.first) : "");
    return "";
}
inline size_t levenshteinDist(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}
inline double jaroSim(const std::string& s1, const std::string& s2) {
    if (s1.empty() && s2.empty()) return 1.0;
    if (s1.empty() || s2.empty()) return 0.0;
    long md = std::max<long>(0, static_cast<long>(std::max(s1.size(), s2.size())) / 2 - 1);
    std::vector<bool> m1(s1.size()), m2(s2.size());
    long matches = 0;
    for (size_t i = 0; i < s1.size(); ++i) {
        long lo = std::max<long>(0, static_cast<long>(i) - md), hi = std::min<long>(static_cast<long>(s2.size()) - 1, static_cast<long>(i) + md);
        for (long j = lo; j <= hi; ++j) if (!m2[static_cast<size_t>(j)] && s1[i] == s2[static_cast<size_t>(j)]) { m1[i] = m2[static_cast<size_t>(j)] = true; ++matches; break; }
    }
    if (!matches) return 0.0;
    long t = 0;
    size_t k = 0;
    for (size_t i = 0; i < s1.size(); ++i) if (m1[i]) { while (!m2[k]) ++k; if (s1[i] != s2[k]) ++t; ++k; }
    double m = static_cast<double>(matches);
    return (m / static_cast<double>(s1.size()) + m / static_cast<double>(s2.size()) + (m - static_cast<double>(t) / 2.0) / m) / 3.0;
}
inline std::string mapChars(const std::string& t, const std::function<std::string(uint32_t, const std::string&)>& f) {
    std::string o;
    for (const auto& ch : u8chars(t)) o += f(u8cp(ch), ch);
    return o;
}
inline void addText4(Reg& r) {
    // ---- identifier / case styles
    addText(r, "camel", "convert to camelCase", [](const std::string& t) {
        auto w = splitIdent(t); std::string o;
        for (size_t i = 0; i < w.size(); ++i) o += i ? capWord(w[i]) : lowerStr(w[i]);
        return o; });
    addText(r, "pascal", "convert to PascalCase", [](const std::string& t) { std::string o; for (const auto& w : splitIdent(t)) o += capWord(w); return o; });
    addText(r, "snake", "convert to snake_case", [](const std::string& t) { auto w = splitIdent(t); for (auto& x : w) x = lowerStr(x); return joinStr(w, "_"); });
    addText(r, "kebab", "convert to kebab-case", [](const std::string& t) { auto w = splitIdent(t); for (auto& x : w) x = lowerStr(x); return joinStr(w, "-"); });
    addText(r, "constant", "convert to CONSTANT_CASE", [](const std::string& t) { auto w = splitIdent(t); for (auto& x : w) x = upperStr(x); return joinStr(w, "_"); });
    addText(r, "dotcase", "convert to dot.case", [](const std::string& t) { auto w = splitIdent(t); for (auto& x : w) x = lowerStr(x); return joinStr(w, "."); });
    addText(r, "pathcase", "convert to path/case", [](const std::string& t) { auto w = splitIdent(t); for (auto& x : w) x = lowerStr(x); return joinStr(w, "/"); });
    addText(r, "titlecase", "Capitalize Each Word", [](const std::string& t) {
        std::string o; bool start = true;
        for (char ch : t) { if (std::isspace(static_cast<unsigned char>(ch))) { start = true; o += ch; } else { o += static_cast<char>(start ? std::toupper(static_cast<unsigned char>(ch)) : std::tolower(static_cast<unsigned char>(ch))); start = false; } }
        return o; });
    addText(r, "sentence", "Sentence case (first letter of each sentence)", [](const std::string& t) {
        std::string o; bool start = true;
        for (char ch : t) { if (start && std::isalpha(static_cast<unsigned char>(ch))) { o += static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); start = false; } else { o += static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); } if (ch == '.' || ch == '!' || ch == '?') start = true; }
        return o; });
    addText(r, "swapcase", "swap upper and lower case", [](const std::string& t) { std::string o = t; for (auto& ch : o) { unsigned char u = static_cast<unsigned char>(ch); ch = static_cast<char>(std::isupper(u) ? std::tolower(u) : std::toupper(u)); } return o; });
    addText(r, "slugify", "URL slug: lower-case words joined by '-'", [](const std::string& t) {
        std::string o; bool dash = false;
        for (unsigned char ch : t) { if (std::isalnum(ch)) { if (dash && !o.empty()) o += '-'; dash = false; o += static_cast<char>(std::tolower(ch)); } else dash = true; }
        return o; });
    // ---- whitespace / shaping
    addText(r, "squeeze", "collapse runs of spaces/tabs into one space", [](const std::string& t) {
        return mapLines(t, [](const std::string& l) { std::string o; bool sp = false; for (char ch : l) { if (ch == ' ' || ch == '\t') { if (!sp) o += ' '; sp = true; } else { o += ch; sp = false; } } return o; }); });
    addText(r, "ltrim", "strip leading whitespace of every line", [](const std::string& t) { return mapLines(t, [](const std::string& l) { size_t a = l.find_first_not_of(" \t"); return a == std::string::npos ? std::string() : l.substr(a); }); });
    addText(r, "rtrim", "strip trailing whitespace of every line", [](const std::string& t) { return mapLines(t, [](const std::string& l) { size_t b = l.find_last_not_of(" \t"); return b == std::string::npos ? std::string() : l.substr(0, b + 1); }); });
    addText(r, "trimlines", "strip both ends of every line and drop empty lines", [](const std::string& t) {
        std::string o; bool first = true;
        for (const auto& l : textLines(t)) { size_t a = l.find_first_not_of(" \t"); if (a == std::string::npos) continue; size_t b = l.find_last_not_of(" \t"); if (!first) o += '\n'; o += l.substr(a, b - a + 1); first = false; }
        return o; });
    addText(r, "dedent", "remove the common leading indentation", [](const std::string& t) {
        size_t common = std::string::npos;
        auto ls = textLines(t);
        for (const auto& l : ls) { size_t a = l.find_first_not_of(" \t"); if (a != std::string::npos) common = std::min(common, a); }
        if (common == std::string::npos) common = 0;
        std::string o;
        for (size_t i = 0; i < ls.size(); ++i) { if (i) o += '\n'; o += ls[i].size() >= common ? ls[i].substr(common) : std::string(); }
        return o; });
    addParamText(r, "indent", "<N>", "indent every line by N spaces", 1, [](const std::vector<std::string>& p, const std::string& t) {
        long n = std::strtol(p[0].c_str(), nullptr, 10); if (n < 0 || n > 200) n = 2;
        return mapLines(t, [n](const std::string& l) { return l.empty() ? l : std::string(static_cast<size_t>(n), ' ') + l; }); });
    addParamText(r, "unindent", "<N>", "remove up to N leading spaces from every line", 1, [](const std::vector<std::string>& p, const std::string& t) {
        long n = std::strtol(p[0].c_str(), nullptr, 10);
        return mapLines(t, [n](const std::string& l) { size_t k = 0; while (k < l.size() && static_cast<long>(k) < n && l[k] == ' ') ++k; return l.substr(k); }); });
    addText(r, "unwrap", "join the lines of each paragraph into one line", [](const std::string& t) {
        std::string o, cur; bool first = true;
        auto flush = [&]() { if (cur.empty()) return; if (!first) o += '\n'; o += cur; cur.clear(); first = false; };
        for (const auto& l : textLines(t)) { size_t a = l.find_first_not_of(" \t"); if (a == std::string::npos) { flush(); continue; } if (!cur.empty()) cur += ' '; cur += l.substr(a, l.find_last_not_of(" \t") - a + 1); }
        flush();
        return o; });
    addParamText(r, "center", "<WIDTH>", "center every line in WIDTH columns", 1, [](const std::vector<std::string>& p, const std::string& t) {
        size_t w = static_cast<size_t>(std::max(1L, std::strtol(p[0].c_str(), nullptr, 10)));
        return mapLines(t, [w](const std::string& l) { size_t n = u8len(l); if (n >= w) return l; size_t left = (w - n) / 2; return std::string(left, ' ') + l; }); });
    addParamText(r, "ljust", "<WIDTH>", "pad every line on the right to WIDTH", 1, [](const std::vector<std::string>& p, const std::string& t) {
        size_t w = static_cast<size_t>(std::max(1L, std::strtol(p[0].c_str(), nullptr, 10))); return mapLines(t, [w](const std::string& l) { return padU8(l, w, false); }); });
    addParamText(r, "rjust", "<WIDTH>", "pad every line on the left to WIDTH", 1, [](const std::vector<std::string>& p, const std::string& t) {
        size_t w = static_cast<size_t>(std::max(1L, std::strtol(p[0].c_str(), nullptr, 10))); return mapLines(t, [w](const std::string& l) { return padU8(l, w, true); }); });
    addParamText(r, "repeat", "<N>", "repeat the text N times (max 1000)", 1, [](const std::vector<std::string>& p, const std::string& t) {
        long n = std::max(0L, std::min(1000L, std::strtol(p[0].c_str(), nullptr, 10))); std::string o; for (long i = 0; i < n; ++i) o += t; return o; });
    addParamText(r, "prefix", "<PREFIX>", "add PREFIX to the start of every line", 1, [](const std::vector<std::string>& p, const std::string& t) { return mapLines(t, [&](const std::string& l) { return p[0] + l; }); });
    addParamText(r, "suffix", "<SUFFIX>", "add SUFFIX to the end of every line", 1, [](const std::vector<std::string>& p, const std::string& t) { return mapLines(t, [&](const std::string& l) { return l + p[0]; }); });
    addParamText(r, "surround", "<LEFT> <RIGHT>", "wrap every line in LEFT … RIGHT", 2, [](const std::vector<std::string>& p, const std::string& t) { return mapLines(t, [&](const std::string& l) { return p[0] + l + p[1]; }); });
    addText(r, "quote", "wrap every line in double quotes (escaping \" and \\)", [](const std::string& t) {
        return mapLines(t, [](const std::string& l) { std::string o = "\""; for (char ch : l) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; } return o + "\""; }); });
    addText(r, "unquote", "remove one pair of surrounding quotes from every line", [](const std::string& t) {
        return mapLines(t, [](const std::string& l) { if (l.size() >= 2 && ((l.front() == '"' && l.back() == '"') || (l.front() == '\'' && l.back() == '\''))) return l.substr(1, l.size() - 2); return l; }); });
    // ---- split / join / order
    addParamText(r, "join", "<SEP>", "join the lines with SEP", 1, [](const std::vector<std::string>& p, const std::string& t) { return joinStr(textLines(t), p[0]); });
    addParamText(r, "splitby", "<SEP>", "split text at SEP, one piece per line", 1, [](const std::vector<std::string>& p, const std::string& t) {
        std::string o; size_t i = 0; const std::string& s = p[0];
        if (s.empty()) return t;
        for (;;) { size_t j = t.find(s, i); o += t.substr(i, j == std::string::npos ? std::string::npos : j - i); if (j == std::string::npos) break; o += '\n'; i = j + s.size(); }
        return o; });
    addParamText(r, "chunk", "<N>", "split text every N characters, one chunk per line", 1, [](const std::vector<std::string>& p, const std::string& t) {
        size_t n = static_cast<size_t>(std::max(1L, std::strtol(p[0].c_str(), nullptr, 10))); std::string o; auto cs = u8chars(t);
        for (size_t i = 0; i < cs.size(); i += n) { if (i) o += '\n'; for (size_t k = i; k < std::min(cs.size(), i + n); ++k) o += cs[k]; }
        return o; });
    addParamText(r, "wordwrap", "<WIDTH>", "wrap at spaces so lines are at most WIDTH characters", 1, [](const std::vector<std::string>& p, const std::string& t) {
        size_t w = static_cast<size_t>(std::max(2L, std::strtol(p[0].c_str(), nullptr, 10))); std::string o, line;
        for (const auto& wd : wordsOf(t)) { if (!line.empty() && u8len(line) + 1 + u8len(wd) > w) { o += line + "\n"; line.clear(); } if (!line.empty()) line += ' '; line += wd; }
        return o + line; });
    addText(r, "revwords", "reverse the order of the words", [](const std::string& t) { auto w = wordsOf(t); std::reverse(w.begin(), w.end()); return joinStr(w, " "); });
    addText(r, "sortwords", "sort the words alphabetically", [](const std::string& t) { auto w = wordsOf(t); std::sort(w.begin(), w.end()); return joinStr(w, " "); });
    addText(r, "uniqwords", "remove repeated words (keeps first occurrence)", [](const std::string& t) { std::set<std::string> seen; std::vector<std::string> o; for (const auto& w : wordsOf(t)) if (seen.insert(w).second) o.push_back(w); return joinStr(o, " "); });
    addText(r, "sortchars", "sort the characters of the text", [](const std::string& t) { auto c = u8chars(t); std::sort(c.begin(), c.end()); return joinStr(c, ""); });
    addText(r, "dedupechars", "remove repeated characters (keeps first occurrence)", [](const std::string& t) { std::set<std::string> seen; std::string o; for (const auto& c : u8chars(t)) if (seen.insert(c).second) o += c; return o; });
    addText(r, "shufflewords", "random order of the words", [](const std::string& t) { auto w = wordsOf(t); std::shuffle(w.begin(), w.end(), rng()); return joinStr(w, " "); });
    addText(r, "shufflechars", "random order of the characters", [](const std::string& t) { auto c = u8chars(t); std::shuffle(c.begin(), c.end(), rng()); return joinStr(c, ""); });
    // ---- analysis
    addText(r, "lenlines", "length (characters) of every line", [](const std::string& t) { std::string o; auto ls = textLines(t); for (size_t i = 0; i < ls.size(); ++i) { if (i) o += '\n'; o += std::to_string(u8len(ls[i])) + "  " + ls[i]; } return o; });
    addText(r, "longest", "the longest line", [](const std::string& t) { std::string b; for (const auto& l : textLines(t)) if (u8len(l) > u8len(b)) b = l; return std::to_string(u8len(b)) + "  " + b; });
    addText(r, "shortest", "the shortest non-empty line", [](const std::string& t) { std::string b; bool any = false; for (const auto& l : textLines(t)) { if (l.empty()) continue; if (!any || u8len(l) < u8len(b)) b = l; any = true; } return std::to_string(u8len(b)) + "  " + b; });
    addText(r, "avglen", "average line length and word length", [](const std::string& t) {
        auto ls = textLines(t); double tl = 0; for (const auto& l : ls) tl += static_cast<double>(u8len(l));
        auto w = wordsOf(t); double tw = 0; for (const auto& x : w) tw += static_cast<double>(u8len(x));
        return "lines: " + std::to_string(ls.size()) + "  avg line: " + fnum(ls.empty() ? 0 : tl / static_cast<double>(ls.size()), 6) + "\nwords: " + std::to_string(w.size()) + "  avg word: " + fnum(w.empty() ? 0 : tw / static_cast<double>(w.size()), 6); });
    addText(r, "charfreq", "frequency of every character (most common first)", [](const std::string& t) {
        std::map<std::string, size_t> m; for (const auto& c : u8chars(t)) if (c != "\n") m[c]++;
        std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        std::string o; for (size_t i = 0; i < v.size() && i < 40; ++i) { if (i) o += '\n'; o += (v[i].first == " " ? "' '" : v[i].first) + "  " + std::to_string(v[i].second); }
        return o; });
    addText(r, "vowels", "count vowels (a e i o u)", [](const std::string& t) { size_t n = 0; for (char ch : lowerStr(t)) if (std::strchr("aeiou", ch) && ch) ++n; return std::to_string(n); });
    addText(r, "consonants", "count consonants (a-z letters that are not vowels)", [](const std::string& t) { size_t n = 0; for (char ch : lowerStr(t)) if (ch >= 'a' && ch <= 'z' && !std::strchr("aeiou", ch)) ++n; return std::to_string(n); });
    addText(r, "digits", "extract only the digits", [](const std::string& t) { std::string o; for (char ch : t) if (std::isdigit(static_cast<unsigned char>(ch))) o += ch; return o; });
    addText(r, "letters", "extract only the letters (ASCII and non-ASCII)", [](const std::string& t) { std::string o; for (unsigned char ch : t) if (std::isalpha(ch) || ch >= 0x80) o += static_cast<char>(ch); return o; });
    addText(r, "alnumonly", "keep letters and digits only", [](const std::string& t) { std::string o; for (unsigned char ch : t) if (std::isalnum(ch) || ch >= 0x80) o += static_cast<char>(ch); return o; });
    addText(r, "onlyascii", "drop every non-ASCII character", [](const std::string& t) { std::string o; for (char ch : t) if (static_cast<unsigned char>(ch) < 0x80) o += ch; return o; });
    // ---- word games / classic algorithms
    addText(r, "palindrome", "is the text a palindrome? (letters and digits only, case-insensitive)", [](const std::string& t) {
        std::string a; for (unsigned char ch : t) if (std::isalnum(ch)) a += static_cast<char>(std::tolower(ch));
        std::string b(a.rbegin(), a.rend()); return std::string(!a.empty() && a == b ? "yes" : "no"); });
    addParamText(r, "anagram", "<WORD>", "are the two words anagrams? (case-insensitive)", 1, [](const std::vector<std::string>& p, const std::string& t) {
        auto norm = [](const std::string& s) { std::string o; for (unsigned char ch : s) if (std::isalnum(ch)) o += static_cast<char>(std::tolower(ch)); std::sort(o.begin(), o.end()); return o; };
        return std::string(norm(p[0]) == norm(t) ? "yes" : "no"); });
    addParamText(r, "levenshtein", "<WORD>", "edit distance between two strings", 1, [](const std::vector<std::string>& p, const std::string& t) { return std::to_string(levenshteinDist(u8chars(p[0]), u8chars(t))); });
    addParamText(r, "hamming", "<WORD>", "Hamming distance (equal length strings)", 1, [](const std::vector<std::string>& p, const std::string& t) {
        auto a = u8chars(p[0]), b = u8chars(t); if (a.size() != b.size()) return std::string("error: strings must have equal length");
        size_t d = 0; for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++d; return std::to_string(d); });
    addParamText(r, "jaro", "<WORD>", "Jaro similarity 0..1 between two strings", 1, [](const std::vector<std::string>& p, const std::string& t) { return fnum(jaroSim(p[0], t), 6); });
    addParamText(r, "similar", "<WORD>", "similarity percentage (edit-distance based)", 1, [](const std::vector<std::string>& p, const std::string& t) {
        auto a = u8chars(p[0]), b = u8chars(t); size_t m = std::max(a.size(), b.size());
        return fnum(m ? 100.0 * (1.0 - static_cast<double>(levenshteinDist(a, b)) / static_cast<double>(m)) : 100.0, 5) + "%"; });
    addText(r, "soundex", "Soundex phonetic code of each word", [](const std::string& t) {
        std::vector<std::string> out;
        for (const auto& w : wordsOf(t)) {
            std::string up; for (unsigned char ch : w) if (std::isalpha(ch)) up += static_cast<char>(std::toupper(ch));
            if (up.empty()) continue;
            auto code = [](char ch) { switch (ch) { case 'B': case 'F': case 'P': case 'V': return '1'; case 'C': case 'G': case 'J': case 'K': case 'Q': case 'S': case 'X': case 'Z': return '2'; case 'D': case 'T': return '3'; case 'L': return '4'; case 'M': case 'N': return '5'; case 'R': return '6'; default: return '0'; } };
            std::string o(1, up[0]); char last = code(up[0]);
            for (size_t i = 1; i < up.size() && o.size() < 4; ++i) { char cd = code(up[i]); if (cd != '0' && cd != last) o += cd; if (up[i] != 'H' && up[i] != 'W') last = cd; }
            while (o.size() < 4) o += '0';
            out.push_back(o);
        }
        return joinStr(out, " "); });
    addText(r, "pig", "Pig Latin", [](const std::string& t) {
        std::vector<std::string> out;
        for (const auto& w : wordsOf(t)) {
            std::string l = lowerStr(w); size_t k = 0; while (k < l.size() && !std::strchr("aeiou", l[k])) ++k;
            out.push_back(k == 0 ? l + "way" : (k >= l.size() ? l + "ay" : l.substr(k) + l.substr(0, k) + "ay"));
        }
        return joinStr(out, " "); });
    addText(r, "leet", "1337 speak", [](const std::string& t) { std::string o = t; for (auto& ch : o) switch (std::tolower(static_cast<unsigned char>(ch))) { case 'a': ch = '4'; break; case 'e': ch = '3'; break; case 'i': ch = '1'; break; case 'o': ch = '0'; break; case 's': ch = '5'; break; case 't': ch = '7'; break; default: break; } return o; });
    static const char* MORSE[][2] = {{"a", ".-"}, {"b", "-..."}, {"c", "-.-."}, {"d", "-.."}, {"e", "."}, {"f", "..-."}, {"g", "--."}, {"h", "...."}, {"i", ".."}, {"j", ".---"}, {"k", "-.-"}, {"l", ".-.."}, {"m", "--"}, {"n", "-."}, {"o", "---"}, {"p", ".--."}, {"q", "--.-"}, {"r", ".-."}, {"s", "..."}, {"t", "-"}, {"u", "..-"}, {"v", "...-"}, {"w", ".--"}, {"x", "-..-"}, {"y", "-.--"}, {"z", "--.."}, {"0", "-----"}, {"1", ".----"}, {"2", "..---"}, {"3", "...--"}, {"4", "....-"}, {"5", "....."}, {"6", "-...."}, {"7", "--..."}, {"8", "---.."}, {"9", "----."}, {".", ".-.-.-"}, {",", "--..--"}, {"?", "..--.."}, {"!", "-.-.--"}, {"/", "-..-."}};
    addText(r, "morse", "text to Morse code (letters separated by space, words by ' / ')", [](const std::string& t) {
        std::vector<std::string> words;
        for (const auto& w : wordsOf(lowerStr(t))) { std::vector<std::string> cs; for (char ch : w) for (const auto& m : MORSE) if (m[0][0] == ch) { cs.push_back(m[1]); break; } words.push_back(joinStr(cs, " ")); }
        return joinStr(words, " / "); });
    addText(r, "unmorse", "Morse code (letters by space, words by '/') to text", [](const std::string& t) {
        std::string o;
        for (const auto& tok : wordsOf(t)) { if (tok == "/") { o += ' '; continue; } bool f = false; for (const auto& m : MORSE) if (tok == m[1]) { o += m[0]; f = true; break; } if (!f) o += '?'; }
        return o; });
    static const char* NATO[] = {"Alfa", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India", "Juliett", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo", "Sierra", "Tango", "Uniform", "Victor", "Whiskey", "X-ray", "Yankee", "Zulu"};
    addText(r, "nato", "spell out with the NATO phonetic alphabet", [](const std::string& t) {
        std::vector<std::string> o; static const char* DIG[] = {"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
        for (unsigned char ch : t) { if (std::isalpha(ch)) o.push_back(NATO[std::tolower(ch) - 'a']); else if (std::isdigit(ch)) o.push_back(DIG[ch - '0']); else if (ch == ' ') o.push_back("/"); }
        return joinStr(o, " "); });
    addNum1(r, "roman", "integer 1..3999 to Roman numerals", [](double v) { long n = static_cast<long>(v); return (n < 1 || n > 3999) ? std::string("error: 1..3999 only") : romanOf(n); });
    addText(r, "unroman", "Roman numerals to integer", [](const std::string& t) { long n = unRoman(wordsOf(t).empty() ? "" : wordsOf(t)[0]); return n < 0 ? std::string("error: not a Roman numeral") : std::to_string(n); });
    addNum1(r, "numword", "integer to English words", [](double v) { if (std::fabs(v) > 9e15) return std::string("error: too large"); return numToWords(static_cast<long long>(v)); });
    addNum1(r, "ordinal", "1 -> 1st, 2 -> 2nd, 3 -> 3rd, 11 -> 11th …", [](double v) { long long n = static_cast<long long>(v); long long m = std::llabs(n) % 100; const char* s = (m >= 11 && m <= 13) ? "th" : (n % 10 == 1 ? "st" : (n % 10 == 2 ? "nd" : (n % 10 == 3 ? "rd" : "th"))); return std::to_string(n) + s; });
    addText(r, "acronym", "acronym of the words (first letters, upper case)", [](const std::string& t) { std::string o; for (const auto& w : wordsOf(t)) o += upperStr(u8chars(w)[0]); return o; });
    addText(r, "initials", "initials with dots: John Ronald Tolkien -> J.R.T.", [](const std::string& t) { std::string o; for (const auto& w : wordsOf(t)) o += upperStr(u8chars(w)[0]) + "."; return o; });
    addParamText(r, "caesar", "<SHIFT>", "Caesar cipher (negative SHIFT decodes)", 1, [](const std::vector<std::string>& p, const std::string& t) {
        long k = ((std::strtol(p[0].c_str(), nullptr, 10) % 26) + 26) % 26; std::string o = t;
        for (auto& ch : o) { if (ch >= 'a' && ch <= 'z') ch = static_cast<char>('a' + (ch - 'a' + k) % 26); else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>('A' + (ch - 'A' + k) % 26); }
        return o; });
    addText(r, "atbash", "Atbash cipher (a<->z)", [](const std::string& t) { std::string o = t; for (auto& ch : o) { if (ch >= 'a' && ch <= 'z') ch = static_cast<char>('z' - (ch - 'a')); else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>('Z' - (ch - 'A')); } return o; });
    addParamText(r, "vigenere", "<KEY> [-d]", "Vigenere cipher with a letter KEY (put -d as the KEY's next word to decode: $ vigenere KEY -d text)", 1, [](const std::vector<std::string>& p, const std::string& t0) {
        std::string t = t0; bool dec = false;
        if (t.size() >= 3 && t.compare(0, 3, "-d ") == 0) { dec = true; t = t.substr(3); }
        std::string key; for (unsigned char ch : p[0]) if (std::isalpha(ch)) key += static_cast<char>(std::tolower(ch));
        if (key.empty()) return std::string("error: key must contain letters");
        std::string o = t; size_t ki = 0;
        for (auto& ch : o) {
            bool lo = ch >= 'a' && ch <= 'z', up = ch >= 'A' && ch <= 'Z'; if (!lo && !up) continue;
            int k = key[ki++ % key.size()] - 'a'; if (dec) k = 26 - k;
            ch = static_cast<char>((lo ? 'a' : 'A') + ((ch - (lo ? 'a' : 'A')) + k) % 26);
        }
        return o; });
    addText(r, "rot47", "ROT47 (all printable ASCII)", [](const std::string& t) { std::string o = t; for (auto& ch : o) if (ch >= 33 && ch <= 126) ch = static_cast<char>(33 + ((ch - 33 + 47) % 94)); return o; });
    addText(r, "rot5", "ROT5 (digits only)", [](const std::string& t) { std::string o = t; for (auto& ch : o) if (ch >= '0' && ch <= '9') ch = static_cast<char>('0' + ((ch - '0' + 5) % 10)); return o; });
    // ---- Arabic helpers
    addText(r, "arabicdigits", "0-9 -> Arabic-Indic digits (٠-٩)", [](const std::string& t) { return mapChars(t, [](uint32_t cp, const std::string& ch) { return (cp >= '0' && cp <= '9') ? cpToU8(0x0660 + (cp - '0')) : ch; }); });
    addText(r, "westdigits", "Arabic-Indic and Persian digits -> 0-9", [](const std::string& t) { return mapChars(t, [](uint32_t cp, const std::string& ch) -> std::string { if (cp >= 0x0660 && cp <= 0x0669) return std::string(1, static_cast<char>('0' + (cp - 0x0660))); if (cp >= 0x06F0 && cp <= 0x06F9) return std::string(1, static_cast<char>('0' + (cp - 0x06F0))); return ch; }); });
    addText(r, "tashkeel", "remove Arabic diacritics (harakat)", [](const std::string& t) { return mapChars(t, [](uint32_t cp, const std::string& ch) { return ((cp >= 0x064B && cp <= 0x065F) || cp == 0x0670) ? std::string() : ch; }); });
    addText(r, "tatweel", "remove the Arabic elongation character (kashida)", [](const std::string& t) { return mapChars(t, [](uint32_t cp, const std::string& ch) { return cp == 0x0640 ? std::string() : ch; }); });
    addText(r, "arnorm", "normalize Arabic: alef forms -> ا, ى -> ي, ة -> ه, remove diacritics and tatweel", [](const std::string& t) {
        return mapChars(t, [](uint32_t cp, const std::string& ch) -> std::string {
            if ((cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || cp == 0x0640) return "";
            if (cp == 0x0622 || cp == 0x0623 || cp == 0x0625) return cpToU8(0x0627);
            if (cp == 0x0649) return cpToU8(0x064A);
            if (cp == 0x0629) return cpToU8(0x0647);
            return ch; }); });
    addText(r, "isarabic", "percentage of Arabic letters in the text", [](const std::string& t) {
        size_t ar = 0, letters = 0;
        for (const auto& ch : u8chars(t)) { uint32_t cp = u8cp(ch); bool a = (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F); if (a) ++ar; if (a || std::isalpha(static_cast<int>(cp < 0x80 ? cp : 0))) ++letters; }
        return fnum(letters ? 100.0 * static_cast<double>(ar) / static_cast<double>(letters) : 0.0, 4) + "% Arabic (" + std::to_string(ar) + " of " + std::to_string(letters) + " letters)"; });
    // ---- Unicode styles
    auto styled = [&r](const std::string& name, const std::string& desc, std::function<std::string(uint32_t, const std::string&)> m) {
        addText(r, name, desc, [m](const std::string& t) { return mapChars(t, m); });
    };
    styled("fancy", "bold mathematical letters: 𝐇𝐞𝐥𝐥𝐨", [](uint32_t cp, const std::string& ch) -> std::string { if (cp >= 'A' && cp <= 'Z') return cpToU8(0x1D400 + (cp - 'A')); if (cp >= 'a' && cp <= 'z') return cpToU8(0x1D41A + (cp - 'a')); if (cp >= '0' && cp <= '9') return cpToU8(0x1D7CE + (cp - '0')); return ch; });
    styled("smallcaps", "small capitals: ʜᴇʟʟᴏ", [](uint32_t cp, const std::string& ch) -> std::string {
        static const char* sc[] = {"ᴀ", "ʙ", "ᴄ", "ᴅ", "ᴇ", "ꜰ", "ɢ", "ʜ", "ɪ", "ᴊ", "ᴋ", "ʟ", "ᴍ", "ɴ", "ᴏ", "ᴘ", "ǫ", "ʀ", "ꜱ", "ᴛ", "ᴜ", "ᴠ", "ᴡ", "x", "ʏ", "ᴢ"};
        if (cp >= 'a' && cp <= 'z') return sc[cp - 'a'];
        if (cp >= 'A' && cp <= 'Z') return sc[cp - 'A'];
        return ch; });
    styled("fullwidth", "full-width letters: Ｈｅｌｌｏ", [](uint32_t cp, const std::string& ch) -> std::string { if (cp >= 33 && cp <= 126) return cpToU8(0xFF01 + (cp - 33)); if (cp == ' ') return cpToU8(0x3000); return ch; });
    styled("circled", "circled letters and digits: Ⓗⓔⓛⓛⓞ", [](uint32_t cp, const std::string& ch) -> std::string { if (cp >= 'A' && cp <= 'Z') return cpToU8(0x24B6 + (cp - 'A')); if (cp >= 'a' && cp <= 'z') return cpToU8(0x24D0 + (cp - 'a')); if (cp >= '1' && cp <= '9') return cpToU8(0x2460 + (cp - '1')); if (cp == '0') return cpToU8(0x24EA); return ch; });
    addText(r, "upsidedown", "flip the text upside down: ɥǝllo", [](const std::string& t) {
        static const std::map<std::string, std::string> M = {{"a", "ɐ"}, {"b", "q"}, {"c", "ɔ"}, {"d", "p"}, {"e", "ǝ"}, {"f", "ɟ"}, {"g", "ƃ"}, {"h", "ɥ"}, {"i", "ᴉ"}, {"j", "ɾ"}, {"k", "ʞ"}, {"l", "l"}, {"m", "ɯ"}, {"n", "u"}, {"o", "o"}, {"p", "d"}, {"q", "b"}, {"r", "ɹ"}, {"s", "s"}, {"t", "ʇ"}, {"u", "n"}, {"v", "ʌ"}, {"w", "ʍ"}, {"x", "x"}, {"y", "ʎ"}, {"z", "z"}, {"?", "¿"}, {"!", "¡"}, {".", "˙"}, {",", "'"}, {"(", ")"}, {")", "("}, {"[", "]"}, {"]", "["}, {"{", "}"}, {"}", "{"}, {"<", ">"}, {">", "<"}, {"_", "‾"}, {"1", "Ɩ"}, {"2", "ᄅ"}, {"3", "Ɛ"}, {"4", "ㄣ"}, {"5", "ϛ"}, {"6", "9"}, {"7", "ㄥ"}, {"8", "8"}, {"9", "6"}, {"0", "0"}};
        auto cs = u8chars(lowerStr(t)); std::string o; for (auto it = cs.rbegin(); it != cs.rend(); ++it) { auto f = M.find(*it); o += f == M.end() ? *it : f->second; } return o; });
}

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin
