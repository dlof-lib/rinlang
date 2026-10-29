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
// النماذج وتسجيل الدخول (نقية، تُستعمل في error=/visible=/value= مباشرة):
//   indsin.isEmail(s) · indsin.isPhone(s) · indsin.isUrl(s) · indsin.passwordScore(s) (0..4)
//   indsin.passwordLabel(s) ("empty|weak|fair|good|strong") · indsin.passwordTone(s) (دور ثيم: danger|warning|info|success)
//   indsin.maskEmail(s) · indsin.initials(name) · indsin.slug(s) · indsin.fieldError(kind, value) -> "" أو رسالة
//     kind: email | password | phone | url | required
// التعديل (للبرنامج فقط؛ ممنوعة داخل الخصائص):
//   indsin.useTheme(name) -> bool · indsin.defineTheme(name, seed[, dark]) -> score
// التأثيرات (Effects Studio — rin_indsin_effects.h):
//   indsin.defineEffect(name, frames[, options]) -> true     frames = "0%{opacity:0,y:20} 100%{opacity:1,y:0}"
//        options: map — duration · delay · easing · iterations · direction · fill · base · x_fn/y_fn/scale_fn/opacity_fn/glow_fn
//   indsin.playEffect(target, effect[, options]) · indsin.stopEffect(target[, effect]) · indsin.removeEffect(name)
//   indsin.effects() -> أسماء · indsin.hasEffect(name) (نقيّتان، تُستعملان داخل الخصائص)
#pragma once
#include "rin_indsin_system.h"
#include "rin_indsin_effects.h" // Effects Studio: indsin.defineEffect/playEffect…
#include "../rin_interpreter.h"
#include <algorithm>
#include <cstring>

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


// ---- أدوات النماذج (نقية) ----
inline bool looksEmail(const std::string& s) {
    size_t at = s.find('@');
    if (at == std::string::npos || at == 0 || s.find('@', at + 1) != std::string::npos) return false;
    for (unsigned char c : s) if (c <= ' ') return false;
    size_t dot = s.find('.', at + 1);
    return dot != std::string::npos && dot > at + 1 && dot + 1 < s.size() && s.back() != '.';
}
inline bool looksPhone(const std::string& s) {
    int digits = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') digits++;
        else if (!(c == ' ' || c == '-' || c == '(' || c == ')' || (c == '+' && i == 0))) return false;
    }
    return digits >= 7 && digits <= 15;
}
inline bool looksUrl(const std::string& s) {
    auto st = [&](const char* p) { return s.rfind(p, 0) == 0 && s.size() > strlen(p) + 2 && s.find(' ') == std::string::npos; };
    return st("http://") || st("https://");
}
inline int passwordScore(const std::string& s) {
    if (s.empty()) return 0;
    bool lo = false, up = false, dg = false, sy = false;
    for (unsigned char c : s) { if (c >= 'a' && c <= 'z') lo = true; else if (c >= 'A' && c <= 'Z') up = true; else if (c >= '0' && c <= '9') dg = true; else sy = true; }
    int classes = (int)lo + (int)up + (int)dg + (int)sy;
    int pts = (s.size() >= 8) + (s.size() >= 12) + (classes >= 2) + (classes >= 3 ? 1 : 0) + (classes >= 4 ? 1 : 0);
    if (s.size() < 6) pts = std::min(pts, 1);
    return std::min(4, pts);
}
inline const char* passwordLabelOf(int sc, bool empty) {
    if (empty) return "empty";
    static const char* n[] = {"weak", "weak", "fair", "good", "strong"};
    return n[sc];
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
    // ---- النماذج وتسجيل الدخول ----
    reg("isEmail", [](Args& a, int line) { needArgs("indsin.isEmail", a, 1, 1, line); return Value::boolean_(looksEmail(needStr("indsin.isEmail", a, 0, line))); });
    reg("isPhone", [](Args& a, int line) { needArgs("indsin.isPhone", a, 1, 1, line); return Value::boolean_(looksPhone(needStr("indsin.isPhone", a, 0, line))); });
    reg("isUrl", [](Args& a, int line) { needArgs("indsin.isUrl", a, 1, 1, line); return Value::boolean_(looksUrl(needStr("indsin.isUrl", a, 0, line))); });
    reg("passwordScore", [](Args& a, int line) { needArgs("indsin.passwordScore", a, 1, 1, line); return Value::num(passwordScore(needStr("indsin.passwordScore", a, 0, line))); });
    reg("passwordLabel", [](Args& a, int line) {
        needArgs("indsin.passwordLabel", a, 1, 1, line);
        std::string p = needStr("indsin.passwordLabel", a, 0, line);
        return Value::string(passwordLabelOf(passwordScore(p), p.empty()));
    });
    reg("passwordTone", [](Args& a, int line) {
        needArgs("indsin.passwordTone", a, 1, 1, line);
        static const char* t[] = {"danger", "danger", "warning", "info", "success"};
        return Value::string(t[passwordScore(needStr("indsin.passwordTone", a, 0, line))]);
    });
    reg("maskEmail", [](Args& a, int line) {
        needArgs("indsin.maskEmail", a, 1, 1, line);
        std::string e = needStr("indsin.maskEmail", a, 0, line);
        size_t at = e.find('@');
        if (at == std::string::npos || at < 2) return Value::string(e);
        return Value::string(e.substr(0, 1) + std::string(at - 1, '*') + e.substr(at));
    });
    reg("initials", [](Args& a, int line) {
        needArgs("indsin.initials", a, 1, 1, line);
        std::string n = needStr("indsin.initials", a, 0, line), out; bool start = true; int words = 0;
        for (size_t i = 0; i < n.size() && words < 3;) {
            unsigned char c = (unsigned char)n[i];
            size_t len = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2));
            if (c == ' ') { start = true; i++; continue; }
            if (start) { out += (c >= 'a' && c <= 'z') ? std::string(1, (char)(c - 32)) : n.substr(i, len); start = false; words++; }
            i += len;
        }
        return Value::string(out);
    });
    reg("slug", [](Args& a, int line) {
        needArgs("indsin.slug", a, 1, 1, line);
        std::string n = needStr("indsin.slug", a, 0, line), out; bool dash = false;
        for (unsigned char c : n) {
            if (std::isalnum(c) || c >= 0x80) { if (dash && !out.empty()) out += '-'; dash = false; out += (char)(c < 0x80 ? std::tolower(c) : c); }
            else dash = true;
        }
        return Value::string(out);
    });
    reg("fieldError", [](Args& a, int line) {
        needArgs("indsin.fieldError", a, 2, 2, line);
        std::string k = needStr("indsin.fieldError", a, 0, line), v = needStr("indsin.fieldError", a, 1, line);
        if (k == "required") return Value::string(v.empty() ? "Required" : "");
        if (v.empty()) return Value::string("");   // لا خطأ قبل الكتابة (استعمل required للإلزام)
        if (k == "email") return Value::string(looksEmail(v) ? "" : "Enter a valid email");
        if (k == "phone") return Value::string(looksPhone(v) ? "" : "Enter a valid phone number");
        if (k == "url") return Value::string(looksUrl(v) ? "" : "Enter a valid URL");
        if (k == "password") return Value::string(v.size() >= 6 ? "" : "At least 6 characters");
        fail(line, "'indsin.fieldError': unknown kind `" + k + "` (email, password, phone, url, required)");
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
    // ---- Effects Studio ----
    auto optsOf = [](const Args& a, size_t i, fx::KV& kv, int line) {
        if (a.size() <= i || a[i].type == rin::Value::Type::NIL) return;
        if (a[i].type != rin::Value::Type::MAP || !a[i].map) fail(line, "effect options must be a map, e.g. {duration: 600, easing: \"spring\"}");
        for (auto& p : *a[i].map) {
            std::string v;
            if (p.second.type == rin::Value::Type::NUMBER) { std::ostringstream os; os << p.second.number; v = os.str(); }
            else if (p.second.type == rin::Value::Type::BOOL) v = p.second.boolean ? "true" : "false";
            else v = p.second.toDisplayString();
            kv.push_back({p.first.toDisplayString(), v});
        }
    };
    reg("defineEffect", [optsOf](Args& a, int line) {
        needArgs("indsin.defineEffect", a, 2, 3, line);
        std::string name = needStr("indsin.defineEffect", a, 0, line), frames = needStr("indsin.defineEffect", a, 1, line);
        fx::KV kv; if (!frames.empty()) kv.push_back({"frames", frames});
        optsOf(a, 2, kv, line);
        std::string err;
        if (!effectLibrary().define(name, kv, err)) fail(line, "indsin.defineEffect: " + err);
        return Value::boolean_(true);
    });
    reg("removeEffect", [](Args& a, int line) {
        needArgs("indsin.removeEffect", a, 1, 1, line);
        return Value::boolean_(effectLibrary().remove(needStr("indsin.removeEffect", a, 0, line)));
    });
    reg("playEffect", [optsOf](Args& a, int line) {
        needArgs("indsin.playEffect", a, 2, 3, line);
        fx::PlayRequest r; r.target = needStr("indsin.playEffect", a, 0, line); r.effect = needStr("indsin.playEffect", a, 1, line);
        optsOf(a, 2, r.opts, line);
        effectLibrary().queue(std::move(r));
        return Value::boolean_(true);
    });
    reg("stopEffect", [](Args& a, int line) {
        needArgs("indsin.stopEffect", a, 1, 2, line);
        fx::PlayRequest r; r.stop = true; r.target = needStr("indsin.stopEffect", a, 0, line);
        if (a.size() > 1) r.effect = needStr("indsin.stopEffect", a, 1, line);
        effectLibrary().queue(std::move(r));
        return Value::boolean_(true);
    });
    reg("effects", [](Args& a, int line) { needArgs("indsin.effects", a, 0, 0, line); return arrayOfStrings(effectLibrary().names()); });
    reg("hasEffect", [](Args& a, int line) {
        needArgs("indsin.hasEffect", a, 1, 1, line);
        return Value::boolean_(effectLibrary().has(needStr("indsin.hasEffect", a, 0, line)));
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
