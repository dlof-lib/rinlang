// indsin/rin_indsin_rinlib.h — نظام تصميم indsin كدوال Rin (`indsin.*`) (docs/indsin_rin.md)
//
// حتى الآن كانت التوكنز والثيمات وأدوات الألوان موجودة داخل المحرّك فقط (C++/JSON للمضيف). هذه المكتبة
// تعرضها لكود Rin نفسه، فيمكن بناء الواجهة من متغيّرات ودوال وحلقات Rin العادية، وتُستعمل أيضاً داخل
// قيم خصائص @view (النقية منها):
//
//   let c = indsin.palette("#6C5CE7");                // map: "50".."900" -> "#rrggbb"
//   @view.Text=t  color=indsin.ensureContrast("#888", indsin.themeColor("background"));
//
// القراءة (نقية، مسموحة داخل الخصائص):
//   indsin.theme() · indsin.themes() · indsin.themeColor(role[, theme]) · indsin.themeColors([theme])
//   indsin.token(category, name) · indsin.tokens(category) · indsin.breakpoint(width) · indsin.sizeClass(width)
//   indsin.palette(seed) · indsin.harmony(seed, kind) · indsin.contrast(fg, bg) · indsin.ensureContrast(fg, bg[, min])
//   indsin.onColor(bg) · indsin.mix(a, b, t) · indsin.lighten(c, k) · indsin.darken(c, k) · indsin.isLight(c)
//   indsin.themeScore(theme)  (0..100 فحص WCAG)
// التعديل (للبرنامج فقط؛ ممنوعة داخل الخصائص):
//   indsin.useTheme(name) -> bool · indsin.defineTheme(name, seed[, dark]) -> score
#pragma once
#include "rin_indsin_system.h"
#include "../rin_interpreter.h"
#include <algorithm>

namespace indsin {
namespace rinlib {

using Args = std::vector<rin::Value>;

[[noreturn]] inline void fail(int line, const std::string& msg) { throw rin::RinError(msg, line); }

inline void needArgs(const char* fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi)
        fail(line, std::string("'") + fn + "' expects " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + "-" + std::to_string(hi)) +
                       " argument(s) but got " + std::to_string(a.size()));
}
inline std::string needStr(const char* fn, const Args& a, size_t i, int line) {
    if (a[i].type != rin::Value::Type::STRING)
        fail(line, std::string("'") + fn + "' expects argument " + std::to_string(i + 1) + " to be a string, found `" + a[i].typeName() + "`");
    return a[i].str;
}
inline double needNum(const char* fn, const Args& a, size_t i, int line) {
    if (a[i].type != rin::Value::Type::NUMBER)
        fail(line, std::string("'") + fn + "' expects argument " + std::to_string(i + 1) + " to be a number, found `" + a[i].typeName() + "`");
    return a[i].number;
}
inline Color needColor(const char* fn, const Args& a, size_t i, int line) {
    std::string s = needStr(fn, a, i, line);
    if (!rincolor::looksLikeColorLiteral(s))
        fail(line, std::string("'") + fn + "': `" + s + "` is not a color literal (use #hex, rgb(), hsl() or a CSS color name)");
    return rincolor::parseColor(s, {0, 0, 0, 255});
}
inline rin::Value hex(const Color& c) { return rin::Value::string(rincolor::toHexAuto(c)); }

inline rin::Value mapOf(const std::vector<std::pair<std::string, rin::Value>>& kv) {
    auto m = std::make_shared<rin::MapData>();
    for (auto& p : kv) m->push_back({rin::Value::string(p.first), p.second});
    return rin::Value::makeMap(m);
}
inline rin::Value arrayOfStrings(const std::vector<std::string>& v) {
    auto a = std::make_shared<rin::ArrayData>();
    for (auto& s : v) a->push_back(rin::Value::string(s));
    return rin::Value::makeArray(a);
}

inline const std::vector<std::pair<const char*, std::vector<const char*>>>& tokenCategories() {
    static const std::vector<std::pair<const char*, std::vector<const char*>>> c = {
        {"spacing", {"compact", "normal", "comfortable", "spacious"}},
        {"radius", {"sharp", "soft", "round", "pill"}},
        {"typography", {"caption", "body", "label", "subtitle", "title", "heading", "code"}},
        {"duration", {"instant", "fast", "normal", "slow", "slower"}},
        {"opacity", {"invisible", "faint", "subtle", "disabled", "medium", "strong", "opaque"}},
        {"border", {"none", "hairline", "thin", "medium", "thick"}},
        {"icon", {"xs", "sm", "md", "lg", "xl", "xxl"}},
        {"elevation", {"none", "xs", "sm", "md", "lg", "xl"}},
        {"z", {"base", "raised", "dropdown", "sticky", "drawer", "dialog", "toast", "tooltip"}},
        {"breakpoint", {"xs", "sm", "md", "lg", "xl"}},
    };
    return c;
}
inline const std::vector<const char*>& roleNames() {
    static const std::vector<const char*> r = {"primary", "secondary", "success", "danger", "warning", "info",
                                               "neutral", "surface", "background", "text", "text_muted", "border"};
    return r;
}
inline std::string joinNames(const std::vector<const char*>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) { if (i) s += ", "; s += v[i]; }
    return s;
}
inline const Theme& themeOrFail(const char* fn, const std::string& name, int line) {
    auto& reg = themeRegistry();
    auto it = reg.themes.find(name);
    if (it == reg.themes.end()) fail(line, std::string("'") + fn + "': unknown theme `" + name + "`");
    return it->second;
}

} // namespace rinlib

// يسجّل دوال indsin.* في المفسّر. آمن للاستدعاء المتكرر (يستبدل بنفس التعريف).
inline void registerIndsinRinLib(rin::Interpreter& interp) {
    using namespace rinlib;
    using rin::Value;
    auto reg = [&](const char* name, std::function<Value(Args&, int)> fn) {
        interp.registerHostNative(std::string("indsin.") + name, std::move(fn));
    };

    reg("theme", [](Args& a, int line) { needArgs("indsin.theme", a, 0, 0, line); return Value::string(themeRegistry().activeName); });
    reg("themes", [](Args& a, int line) {
        needArgs("indsin.themes", a, 0, 0, line);
        std::vector<std::string> names;
        for (auto& kv : themeRegistry().themes) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return arrayOfStrings(names);
    });
    reg("themeColor", [](Args& a, int line) {
        needArgs("indsin.themeColor", a, 1, 2, line);
        std::string role = needStr("indsin.themeColor", a, 0, line);
        const Theme& t = a.size() == 2 ? themeOrFail("indsin.themeColor", needStr("indsin.themeColor", a, 1, line), line) : themeRegistry().active();
        const Color* c = t.slot(role);
        if (!c) fail(line, "'indsin.themeColor': unknown role `" + role + "` (roles: " + joinNames(roleNames()) + ")");
        return hex(*c);
    });
    reg("themeColors", [](Args& a, int line) {
        needArgs("indsin.themeColors", a, 0, 1, line);
        const Theme& t = a.size() == 1 ? themeOrFail("indsin.themeColors", needStr("indsin.themeColors", a, 0, line), line) : themeRegistry().active();
        std::vector<std::pair<std::string, Value>> kv;
        for (const char* r : roleNames()) kv.push_back({r, hex(*t.slot(r))});
        return mapOf(kv);
    });
    reg("token", [](Args& a, int line) {
        needArgs("indsin.token", a, 2, 2, line);
        std::string cat = needStr("indsin.token", a, 0, line), name = needStr("indsin.token", a, 1, line);
        double d = 0;
        if (!resolveToken(cat, name, d)) {
            for (auto& c : tokenCategories())
                if (cat == c.first) fail(line, "'indsin.token': unknown " + cat + " token `" + name + "` (names: " + joinNames(c.second) + ")");
            std::string cats; for (auto& c : tokenCategories()) { if (!cats.empty()) cats += ", "; cats += c.first; }
            fail(line, "'indsin.token': unknown category `" + cat + "` (categories: " + cats + ")");
        }
        return Value::num(d);
    });
    reg("tokens", [](Args& a, int line) {
        needArgs("indsin.tokens", a, 1, 1, line);
        std::string cat = needStr("indsin.tokens", a, 0, line);
        for (auto& c : tokenCategories()) {
            if (cat != c.first) continue;
            std::vector<std::pair<std::string, Value>> kv;
            for (const char* n : c.second) { double d; if (resolveToken(cat, n, d)) kv.push_back({n, Value::num(d)}); }
            return mapOf(kv);
        }
        fail(line, "'indsin.tokens': unknown category `" + cat + "`");
    });
    reg("breakpoint", [](Args& a, int line) {
        needArgs("indsin.breakpoint", a, 1, 1, line);
        return Value::string(breakpointName(breakpointFor(needNum("indsin.breakpoint", a, 0, line))));
    });
    reg("sizeClass", [](Args& a, int line) {
        needArgs("indsin.sizeClass", a, 1, 1, line);
        return Value::string(windowSizeClass(needNum("indsin.sizeClass", a, 0, line)));
    });
    reg("palette", [](Args& a, int line) {
        needArgs("indsin.palette", a, 1, 1, line);
        TonalPalette p = tonalPalette(needColor("indsin.palette", a, 0, line));
        std::vector<std::pair<std::string, Value>> kv;
        for (int i = 0; i < TonalPalette::kSteps; i++) kv.push_back({std::to_string(TonalPalette::kStepNames[i]), hex(p.tones[i])});
        return mapOf(kv);
    });
    reg("harmony", [](Args& a, int line) {
        needArgs("indsin.harmony", a, 2, 2, line);
        Color seed = needColor("indsin.harmony", a, 0, line);
        std::string kind = needStr("indsin.harmony", a, 1, line);
        Harmony h;
        if (!harmonyFromName(kind, h)) fail(line, "'indsin.harmony': unknown kind `" + kind + "` (complementary, analogous, triadic, split, tetradic)");
        auto arr = std::make_shared<rin::ArrayData>();
        for (auto& c : harmonyColors(seed, h)) arr->push_back(hex(c));
        return Value::makeArray(arr);
    });
    reg("contrast", [](Args& a, int line) {
        needArgs("indsin.contrast", a, 2, 2, line);
        return Value::num(rincolor::contrastRatio(needColor("indsin.contrast", a, 0, line), needColor("indsin.contrast", a, 1, line)));
    });
    reg("ensureContrast", [](Args& a, int line) {
        needArgs("indsin.ensureContrast", a, 2, 3, line);
        double minR = a.size() == 3 ? needNum("indsin.ensureContrast", a, 2, line) : 4.5;
        return hex(ensureContrast(needColor("indsin.ensureContrast", a, 0, line), needColor("indsin.ensureContrast", a, 1, line), minR));
    });
    reg("onColor", [](Args& a, int line) { needArgs("indsin.onColor", a, 1, 1, line); return hex(onColor(needColor("indsin.onColor", a, 0, line))); });
    reg("mix", [](Args& a, int line) {
        needArgs("indsin.mix", a, 3, 3, line);
        return hex(rincolor::mix(needColor("indsin.mix", a, 0, line), needColor("indsin.mix", a, 1, line), needNum("indsin.mix", a, 2, line)));
    });
    reg("lighten", [](Args& a, int line) {
        needArgs("indsin.lighten", a, 2, 2, line);
        return hex(rincolor::lighten(needColor("indsin.lighten", a, 0, line), needNum("indsin.lighten", a, 1, line)));
    });
    reg("darken", [](Args& a, int line) {
        needArgs("indsin.darken", a, 2, 2, line);
        return hex(rincolor::darken(needColor("indsin.darken", a, 0, line), needNum("indsin.darken", a, 1, line)));
    });
    reg("isLight", [](Args& a, int line) { needArgs("indsin.isLight", a, 1, 1, line); return Value::boolean_(rincolor::isLight(needColor("indsin.isLight", a, 0, line))); });
    reg("themeScore", [](Args& a, int line) {
        needArgs("indsin.themeScore", a, 1, 1, line);
        return Value::num(validateTheme(themeOrFail("indsin.themeScore", needStr("indsin.themeScore", a, 0, line), line)).score());
    });
    // ---- تعديل (للبرنامج فقط) ----
    reg("useTheme", [](Args& a, int line) {
        needArgs("indsin.useTheme", a, 1, 1, line);
        std::string n = needStr("indsin.useTheme", a, 0, line);
        auto& r = themeRegistry();
        if (!r.themes.count(n)) return Value::boolean_(false);
        r.setActive(n);
        return Value::boolean_(true);
    });
    reg("defineTheme", [](Args& a, int line) {
        needArgs("indsin.defineTheme", a, 2, 3, line);
        std::string name = needStr("indsin.defineTheme", a, 0, line);
        Color seed = needColor("indsin.defineTheme", a, 1, line);
        bool dark = a.size() == 3 ? a[2].isTruthy() : false;
        Theme t = themeFromSeed(name, seed, dark);
        themeRegistry().registerTheme(t);
        return Value::num(validateTheme(t).score());
    });
}

} // namespace indsin
