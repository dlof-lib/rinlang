// indsin/rin_indsin_system.h — Design System v2 لمحرّك Indsin.
//
// طبقة إضافية (additive) فوق rin_indsin_tokens.h: لا تغيّر معنى أي token أو attribute موجود،
// وتضيف ما يحتاجه نظام تصميم كامل:
//
//   1) توكنز موسَّعة: elevation (ظلال)، z-layers، breakpoints (استجابة)، motion (مدد وحركة)،
//      opacity، border width، حجم هدف اللمس (touch target).
//   2) أدوات ألوان: tonalPalette (سلّم 50..900)، ensureContrast (WCAG)، onColor، harmony،
//      elevatedSurface (سطح أفتح عند الارتفاع في الثيمات الداكنة).
//   3) مولّد ثيمات: themeFromSeed (ثيم كامل من لون واحد)، deriveTheme (وراثة مع تجاوزات)،
//      validateTheme (تقرير تباين WCAG لكل أزواج الألوان الأساسية) + themeToJson.
//   4) tokensToJson: تصدير كل مقاييس التوكنز للمضيف (أندرويد/ويب/أدوات).
//
// كل دالة هنا «نقية» (pure) ولا تعتمد على حالة عامة سوى themeRegistry() حيث يُذكر ذلك صراحةً.
// كل المقاييس الرقمية مطابقة لما يُوثَّق في docs/indsin_expansion.md وتُختبر في
// tests/tools/test_indsin_system.cpp.
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include "rin_indsin_paint.h" // jsonEscape
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace indsin {

// ============================================================================================
// 1) التوكنز الموسَّعة
// ============================================================================================

// ---- Elevation (ظلال) ---------------------------------------------------------------------
// سلّم من 0..5. كل مستوى يصف ظلاً واحداً (blur/offsetY/spread بالبكسل + شفافية الظل 0..1).
// الأسماء: none/xs/sm/md/lg/xl (أو رقم 0..5 مباشرة).
struct Elevation {
    int level = 0;
    double blur = 0, offsetY = 0, spread = 0, opacity = 0;
};
inline Elevation elevationForLevel(int level) {
    static const Elevation table[6] = {
        {0, 0,  0, 0, 0.00},
        {1, 2,  1, 0, 0.12},
        {2, 4,  2, 0, 0.14},
        {3, 8,  4, 0, 0.16},
        {4, 16, 8, 1, 0.18},
        {5, 24, 12, 2, 0.22},
    };
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    return table[level];
}
inline bool resolveElevationToken(const std::string& name, Elevation& out) {
    static const std::map<std::string, int> names = {
        {"none", 0}, {"xs", 1}, {"sm", 2}, {"md", 3}, {"lg", 4}, {"xl", 5},
    };
    auto it = names.find(name);
    if (it != names.end()) { out = elevationForLevel(it->second); return true; }
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '5') { out = elevationForLevel(name[0] - '0'); return true; }
    return false;
}
// قراءة `elevation=` من Strand: رقم (0..5) أو اسم توكن. الغياب => none.
inline Elevation resolveElevation(const Strand& s) {
    const Value* v = s.attr("elevation");
    if (!v) return elevationForLevel(0);
    if (v->kind == Value::Kind::NUMBER) return elevationForLevel((int)std::lround(v->number));
    Elevation e;
    if (resolveElevationToken(v->str, e)) return e;
    return elevationForLevel((int)std::lround(v->asNumber(0)));
}
// في الثيمات الداكنة الظل لا يُرى، فالأسطح الأعلى تصبح أفتح (نفس قاعدة Material). في الفاتحة يبقى
// السطح كما هو (الظل يكفي).
inline Color elevatedSurface(const Color& surface, int level, bool darkTheme) {
    if (!darkTheme) return surface;
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    return rincolor::lighten(surface, 0.03 * level);
}

// ---- Z layers ----------------------------------------------------------------------------
// طبقات عمودية مسمّاة بدل أرقام عشوائية. الترتيب: base < raised < dropdown < sticky < drawer <
// dialog < toast < tooltip.
inline bool resolveZLayer(const std::string& name, int& out) {
    static const std::map<std::string, int> layers = {
        {"base", 0}, {"raised", 10}, {"dropdown", 100}, {"sticky", 200},
        {"drawer", 300}, {"dialog", 400}, {"toast", 500}, {"tooltip", 600},
    };
    auto it = layers.find(name);
    if (it == layers.end()) return false;
    out = it->second;
    return true;
}
// `layer=` أو `z=`: اسم طبقة أو رقم.
inline int resolveZIndex(const Strand& s, int def = 0) {
    for (const char* key : {"layer", "z"}) {
        const Value* v = s.attr(key);
        if (!v) continue;
        if (v->kind == Value::Kind::NUMBER) return (int)std::lround(v->number);
        int z;
        if (resolveZLayer(v->str, z)) return z;
        return (int)std::lround(v->asNumber(def));
    }
    return def;
}

// ---- Breakpoints (استجابة) -----------------------------------------------------------------
// xs < 360 <= sm < 600 <= md < 840 <= lg < 1200 <= xl. (مطابقة لفئات Material: compact/medium/expanded.)
enum class Breakpoint { XS = 0, SM, MD, LG, XL };
inline Breakpoint breakpointFor(double width) {
    if (width < 360)  return Breakpoint::XS;
    if (width < 600)  return Breakpoint::SM;
    if (width < 840)  return Breakpoint::MD;
    if (width < 1200) return Breakpoint::LG;
    return Breakpoint::XL;
}
inline const char* breakpointName(Breakpoint b) {
    switch (b) {
        case Breakpoint::XS: return "xs";
        case Breakpoint::SM: return "sm";
        case Breakpoint::MD: return "md";
        case Breakpoint::LG: return "lg";
        case Breakpoint::XL: return "xl";
    }
    return "xs";
}
inline bool breakpointFromName(const std::string& n, Breakpoint& out) {
    static const std::map<std::string, Breakpoint> m = {
        {"xs", Breakpoint::XS}, {"sm", Breakpoint::SM}, {"md", Breakpoint::MD},
        {"lg", Breakpoint::LG}, {"xl", Breakpoint::XL},
    };
    auto it = m.find(n);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}
inline double breakpointMinWidth(Breakpoint b) {
    switch (b) {
        case Breakpoint::XS: return 0;
        case Breakpoint::SM: return 360;
        case Breakpoint::MD: return 600;
        case Breakpoint::LG: return 840;
        case Breakpoint::XL: return 1200;
    }
    return 0;
}
// فئة الجهاز المبسّطة التي تستخدمها أنظمة التصميم: compact (هاتف) / medium (تابلت صغير) / expanded.
inline const char* windowSizeClass(double width) {
    if (width < 600) return "compact";
    if (width < 840) return "medium";
    return "expanded";
}

// ---- Motion tokens -------------------------------------------------------------------------
// (resolveDurationToken معرَّفة في rin_indsin_tokens.h كي يستخدمها محرّك الحركات أيضاً.)
// duration= رقم (ms) أو اسم توكن.
inline double resolveDurationMs(const Strand& s, double def = 250) {
    const Value* v = s.attr("duration");
    if (!v) return def;
    if (v->kind == Value::Kind::NUMBER) return v->number;
    double d;
    if (resolveDurationToken(v->str, d)) return d;
    return v->asNumber(def);
}

// ---- Opacity / border-width / icon-size / touch-target tokens -----------------------------
inline bool resolveOpacityToken(const std::string& name, double& out) {
    static const std::map<std::string, double> m = {
        {"invisible", 0.0}, {"faint", 0.08}, {"subtle", 0.16}, {"disabled", 0.38},
        {"medium", 0.6}, {"strong", 0.87}, {"opaque", 1.0},
    };
    auto it = m.find(name);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}
inline bool resolveBorderWidthToken(const std::string& name, double& out) {
    static const std::map<std::string, double> m = {
        {"none", 0}, {"hairline", 1}, {"thin", 1}, {"medium", 2}, {"thick", 4},
    };
    auto it = m.find(name);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}
inline bool resolveIconSizeToken(const std::string& name, double& out) {
    static const std::map<std::string, double> m = {
        {"xs", 12}, {"sm", 16}, {"md", 20}, {"lg", 24}, {"xl", 32}, {"xxl", 48},
    };
    auto it = m.find(name);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}
// الحدّ الأدنى لهدف اللمس: 44px (iOS HIG / WCAG 2.5.5) والموصى به 48px (Material/Android).
constexpr double kMinTouchTarget = 44.0;
constexpr double kRecommendedTouchTarget = 48.0;

// ---- مسح موحَّد لكل الفئات -----------------------------------------------------------------
// resolveToken("elevation", "md", out) ... يُستخدم من أدوات الـ CLI والمضيف لاستعلام أي توكن
// بالاسم من غير معرفة أي دالة متخصصة. القيمة تُعاد كرقم (double).
inline bool resolveToken(const std::string& category, const std::string& name, double& out) {
    if (category == "spacing")     return resolveSpacingToken(name, out);
    if (category == "radius")      return resolveRadiusToken(name, out);
    if (category == "duration")    return resolveDurationToken(name, out);
    if (category == "opacity")     return resolveOpacityToken(name, out);
    if (category == "border")      return resolveBorderWidthToken(name, out);
    if (category == "icon")        return resolveIconSizeToken(name, out);
    if (category == "elevation")   { Elevation e; if (!resolveElevationToken(name, e)) return false; out = e.level; return true; }
    if (category == "z")           { int z; if (!resolveZLayer(name, z)) return false; out = z; return true; }
    if (category == "breakpoint")  { Breakpoint b; if (!breakpointFromName(name, b)) return false; out = breakpointMinWidth(b); return true; }
    if (category == "typography")  { TypographyPreset p; if (!resolveTypographyToken(name, p)) return false; out = p.size; return true; }
    return false;
}

// ============================================================================================
// 2) أدوات الألوان
// ============================================================================================

// سلّم لوني 50..900 حول لون بذرة: الخطوة 500 هي البذرة نفسها تماماً؛ الخطوات الأفتح تُمزَج نحو
// الأبيض والأغمق نحو الأسود، فالإضاءة النسبية (relative luminance) تتناقص بشكل رتيب (monotonic)
// من 50 إلى 900 دائماً.
struct TonalPalette {
    static constexpr int kSteps = 10;
    static constexpr int kStepNames[kSteps] = {50, 100, 200, 300, 400, 500, 600, 700, 800, 900};
    std::array<Color, kSteps> tones;
    const Color& at(int stepName) const {
        for (int i = 0; i < kSteps; i++) if (kStepNames[i] == stepName) return tones[i];
        return tones[5];
    }
};
inline TonalPalette tonalPalette(const Color& seed) {
    TonalPalette p;
    static const double lighten[5] = {0.92, 0.82, 0.66, 0.48, 0.24};  // 50,100,200,300,400
    static const double darken[4]  = {0.14, 0.30, 0.46, 0.62};        // 600,700,800,900
    for (int i = 0; i < 5; i++) p.tones[i] = rincolor::lighten(seed, lighten[i]);
    p.tones[5] = seed;
    for (int i = 0; i < 4; i++) p.tones[6 + i] = rincolor::darken(seed, darken[i]);
    return p;
}

// يعدّل `fg` (بمزجه نحو الأبيض أو الأسود) حتى يبلغ تباينه مع `bg` الحدّ `minRatio` (افتراضياً 4.5 =
// WCAG AA للنص العادي). يختار الاتجاه الذي يعطي أعلى تباين ممكن، ويستخدم بحثاً ثنائياً لأقرب
// تعديل ممكن (أقل تغيير بصري). إن استحال بلوغ الحدّ يعيد أفضل نتيجة ممكنة (أبيض/أسود).
inline Color ensureContrast(const Color& fg, const Color& bg, double minRatio = 4.5) {
    if (rincolor::contrastRatio(fg, bg) >= minRatio) return fg;
    Color target = rincolor::bestTextColor(bg);
    if (rincolor::contrastRatio(target, bg) < minRatio) { target.a = fg.a; return target; }
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 24; i++) {
        double mid = (lo + hi) / 2.0;
        Color c = rincolor::mix(fg, target, mid);
        if (rincolor::contrastRatio(c, bg) >= minRatio) hi = mid; else lo = mid;
    }
    Color out = rincolor::mix(fg, target, hi);
    out.a = fg.a;
    return out;
}
// اللون المناسب للنص/الأيقونة فوق خلفية `bg` (أبيض أو أسود شبه-كامل).
inline Color onColor(const Color& bg) { return rincolor::bestTextColor(bg); }

enum class Harmony { COMPLEMENTARY, ANALOGOUS, TRIADIC, SPLIT_COMPLEMENTARY, TETRADIC };
inline bool harmonyFromName(const std::string& n, Harmony& out) {
    static const std::map<std::string, Harmony> m = {
        {"complementary", Harmony::COMPLEMENTARY}, {"analogous", Harmony::ANALOGOUS},
        {"triadic", Harmony::TRIADIC}, {"split", Harmony::SPLIT_COMPLEMENTARY},
        {"split_complementary", Harmony::SPLIT_COMPLEMENTARY}, {"tetradic", Harmony::TETRADIC},
    };
    auto it = m.find(n);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}
// ألوان متناغمة مع `seed` (لا تتضمّن البذرة نفسها). تدوير في فضاء HSL مع إبقاء S/L.
inline std::vector<Color> harmonyColors(const Color& seed, Harmony kind) {
    rincolor::HSL h = rincolor::rgbToHsl(seed);
    std::vector<double> offsets;
    switch (kind) {
        case Harmony::COMPLEMENTARY:       offsets = {180}; break;
        case Harmony::ANALOGOUS:           offsets = {-30, 30}; break;
        case Harmony::TRIADIC:             offsets = {120, 240}; break;
        case Harmony::SPLIT_COMPLEMENTARY: offsets = {150, 210}; break;
        case Harmony::TETRADIC:            offsets = {90, 180, 270}; break;
    }
    std::vector<Color> out;
    for (double o : offsets) out.push_back(rincolor::hslToRgb(h.h + o, h.s, h.l, seed.a));
    return out;
}

// ============================================================================================
// 3) مولّد الثيمات
// ============================================================================================

inline bool isDarkTheme(const Theme& t) { return !rincolor::isLight(t.background); }

// ثيم كامل (كل الأدوار الاثني عشر) من لون بذرة واحد:
//  - الخلفية/السطح/النص مشتقة من تدرّج البذرة (tint خفيف) مع ضمان تباين نص/خلفية >= 7:1 تقريباً.
//  - text_muted يُضبط ليبلغ تباين >= 4.5 مع الخلفية.
//  - primary = البذرة، لكن إن كان تباينه مع الخلفية < 3:1 (حدّ مكوّنات الواجهة في WCAG 1.4.11) يُعدَّل.
//  - ألوان الحالة (success/danger/warning/info) ثابتة الألوان لكنها تُضبط لتباين >= 3:1 مع السطح.
inline Theme themeFromSeed(const std::string& name, const Color& seed, bool dark) {
    Theme t; t.name = name;
    rincolor::HSL h = rincolor::rgbToHsl(seed);
    double hue = h.h;
    Color bg, surface, text;
    if (dark) {
        bg      = rincolor::hslToRgb(hue, 0.18, 0.07);
        surface = rincolor::hslToRgb(hue, 0.16, 0.12);
        text    = rincolor::hslToRgb(hue, 0.12, 0.93);
    } else {
        bg      = rincolor::hslToRgb(hue, 0.20, 0.96);
        surface = Color{255, 255, 255};
        text    = rincolor::hslToRgb(hue, 0.25, 0.09);
    }
    t.background = bg; t.surface = surface; t.text = text;
    t.text_muted = ensureContrast(rincolor::mix(text, bg, 0.35), bg, 4.5);
    t.border     = rincolor::mix(surface, text, dark ? 0.16 : 0.12);
    t.neutral    = ensureContrast(rincolor::hslToRgb(hue, 0.08, 0.5), surface, 3.0);

    t.primary    = ensureContrast(seed, bg, 3.0);
    t.secondary  = ensureContrast(rincolor::hslToRgb(hue + 30, std::max(0.45, h.s), dark ? 0.62 : 0.42), bg, 3.0);
    t.success    = ensureContrast(Color{46, 160, 67},  surface, 3.0);
    t.danger     = ensureContrast(Color{209, 69, 69},  surface, 3.0);
    t.warning    = ensureContrast(Color{212, 167, 44}, surface, 3.0);
    t.info       = ensureContrast(Color{58, 110, 196}, surface, 3.0);
    return t;
}
inline Theme themeFromSeed(const std::string& name, const std::string& seedLiteral, bool dark) {
    return themeFromSeed(name, parseHexColor(seedLiteral, Color{108, 78, 245}), dark);
}

// ثيم جديد بالوراثة: ينسخ `base` بالكامل ويستبدل فقط الأدوار الموجودة في `overrides`
// (مفاتيحها أسماء الأدوار: primary/secondary/success/danger/warning/info/neutral/surface/
// background/text/text_muted/border). أدوار غير معروفة تُتجاهَل وتُعاد في `unknownRoles` إن طُلب.
inline Theme deriveTheme(const Theme& base, const std::string& newName,
                         const std::map<std::string, Color>& overrides,
                         std::vector<std::string>* unknownRoles = nullptr) {
    Theme t = base;
    t.name = newName;
    for (auto& kv : overrides) {
        if (t.slot(kv.first)) t.setSlot(kv.first, kv.second);
        else if (unknownRoles) unknownRoles->push_back(kv.first);
    }
    return t;
}

// ---- فحص الثيم (Theme linting) ---------------------------------------------------------------
struct ThemeIssue {
    std::string code;     // THEME-TEXT-BG ...
    std::string severity; // "error" أو "warning"
    std::string message;
    double ratio = 0;     // التباين المقاس
    double required = 0;  // التباين المطلوب
};
struct ThemeReport {
    std::vector<ThemeIssue> issues;
    int checks = 0;
    int passed = 0;
    // 0..100: نسبة الفحوص الناجحة (الأخطاء تُحسب كاملة والتحذيرات بنصف وزن).
    int score() const {
        if (checks == 0) return 100;
        double penalty = 0;
        for (auto& i : issues) penalty += (i.severity == "error") ? 1.0 : 0.5;
        double s = 100.0 * (1.0 - penalty / checks);
        return (int)std::lround(std::max(0.0, std::min(100.0, s)));
    }
    bool ok() const { for (auto& i : issues) if (i.severity == "error") return false; return true; }
};
inline void themeCheckPair(ThemeReport& r, const std::string& code, const std::string& severity,
                           const std::string& what, const Color& fg, const Color& bg, double required) {
    r.checks++;
    double ratio = rincolor::contrastRatio(fg, bg);
    if (ratio + 1e-9 >= required) { r.passed++; return; }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: contrast %.2f:1 < required %.1f:1", what.c_str(), ratio, required);
    r.issues.push_back({code, severity, buf, ratio, required});
}
// القواعد: النص العادي >= 4.5 (WCAG AA 1.4.3)، مكوّنات الواجهة/ألوان الحالة >= 3 (1.4.11)،
// الحدّ مقابل السطح >= 1.1 (تحذير فقط: حدّ بالكاد مرئي).
inline ThemeReport validateTheme(const Theme& t) {
    ThemeReport r;
    themeCheckPair(r, "THEME-TEXT-BG",      "error",   "text on background",       t.text,       t.background, 4.5);
    themeCheckPair(r, "THEME-TEXT-SURFACE", "error",   "text on surface",          t.text,       t.surface,    4.5);
    themeCheckPair(r, "THEME-MUTED-BG",     "error",   "text_muted on background", t.text_muted, t.background, 4.5);
    themeCheckPair(r, "THEME-MUTED-SURFACE","warning", "text_muted on surface",    t.text_muted, t.surface,    4.5);
    themeCheckPair(r, "THEME-PRIMARY-BG",   "error",   "primary on background",    t.primary,    t.background, 3.0);
    themeCheckPair(r, "THEME-SECONDARY-BG", "warning", "secondary on background",  t.secondary,  t.background, 3.0);
    themeCheckPair(r, "THEME-SUCCESS",      "warning", "success on surface",       t.success,    t.surface,    3.0);
    themeCheckPair(r, "THEME-DANGER",       "warning", "danger on surface",        t.danger,     t.surface,    3.0);
    themeCheckPair(r, "THEME-WARNING",      "warning", "warning on surface",       t.warning,    t.surface,    3.0);
    themeCheckPair(r, "THEME-INFO",         "warning", "info on surface",          t.info,       t.surface,    3.0);
    themeCheckPair(r, "THEME-BORDER",       "warning", "border on surface",        t.border,     t.surface,    1.1);
    // النص الموضوع فوق primary (زر ممتلئ): اللون الأفضل دائماً متاح، فنتحقق أنه يبلغ 4.5.
    themeCheckPair(r, "THEME-ON-PRIMARY",   "error",   "on-primary text",          onColor(t.primary), t.primary, 4.5);
    return r;
}

// ============================================================================================
// 4) JSON للتصدير
// ============================================================================================
inline std::string colorJson(const Color& c) { return "\"" + rincolor::toHexAuto(c) + "\""; }

inline std::string themeToJson(const Theme& t) {
    static const char* roles[] = {"primary", "secondary", "success", "danger", "warning", "info",
                                  "neutral", "surface", "background", "text", "text_muted", "border"};
    std::ostringstream os;
    os << "{\"name\":\"" << jsonEscape(t.name) << "\",\"dark\":" << (isDarkTheme(t) ? "true" : "false") << ",\"colors\":{";
    bool first = true;
    for (const char* role : roles) {
        const Color* c = t.slot(role);
        if (!c) continue;
        if (!first) os << ",";
        first = false;
        os << "\"" << role << "\":" << colorJson(*c);
    }
    os << "}}";
    return os.str();
}
inline std::string themeReportToJson(const ThemeReport& r) {
    std::ostringstream os;
    os << "{\"ok\":" << (r.ok() ? "true" : "false") << ",\"score\":" << r.score()
       << ",\"checks\":" << r.checks << ",\"passed\":" << r.passed << ",\"issues\":[";
    for (size_t i = 0; i < r.issues.size(); i++) {
        auto& is = r.issues[i];
        if (i) os << ",";
        os << "{\"code\":\"" << is.code << "\",\"severity\":\"" << is.severity << "\",\"message\":\""
           << jsonEscape(is.message) << "\",\"ratio\":" << is.ratio << ",\"required\":" << is.required << "}";
    }
    os << "]}";
    return os.str();
}
inline std::string paletteToJson(const TonalPalette& p) {
    std::ostringstream os;
    os << "{";
    for (int i = 0; i < TonalPalette::kSteps; i++) {
        if (i) os << ",";
        os << "\"" << TonalPalette::kStepNames[i] << "\":" << colorJson(p.tones[i]);
    }
    os << "}";
    return os.str();
}
// كل مقاييس التوكنز + الثيم النشط + أسماء الثيمات المسجّلة، كـ JSON واحد يستهلكه المضيف.
inline std::string tokensToJson() {
    std::ostringstream os;
    os << "{";
    auto scale = [&](const char* label, const std::vector<std::pair<std::string, double>>& items) {
        os << "\"" << label << "\":{";
        for (size_t i = 0; i < items.size(); i++) {
            if (i) os << ",";
            os << "\"" << items[i].first << "\":" << items[i].second;
        }
        os << "}";
    };
    auto collect = [&](const char* category, std::initializer_list<const char*> names) {
        std::vector<std::pair<std::string, double>> v;
        for (const char* n : names) { double d; if (resolveToken(category, n, d)) v.push_back({n, d}); }
        return v;
    };
    scale("spacing",    collect("spacing",    {"compact", "normal", "comfortable", "spacious"})); os << ",";
    scale("radius",     collect("radius",     {"sharp", "soft", "round", "pill"}));               os << ",";
    scale("typography", collect("typography", {"caption", "body", "label", "subtitle", "title", "heading", "code"})); os << ",";
    scale("duration",   collect("duration",   {"instant", "fast", "normal", "slow", "slower"}));  os << ",";
    scale("opacity",    collect("opacity",    {"invisible", "faint", "subtle", "disabled", "medium", "strong", "opaque"})); os << ",";
    scale("border",     collect("border",     {"none", "hairline", "thin", "medium", "thick"}));  os << ",";
    scale("icon",       collect("icon",       {"xs", "sm", "md", "lg", "xl", "xxl"}));            os << ",";
    scale("z",          collect("z",          {"base", "raised", "dropdown", "sticky", "drawer", "dialog", "toast", "tooltip"})); os << ",";
    scale("breakpoint", collect("breakpoint", {"xs", "sm", "md", "lg", "xl"}));                   os << ",";
    os << "\"elevation\":[";
    for (int i = 0; i <= 5; i++) {
        Elevation e = elevationForLevel(i);
        if (i) os << ",";
        os << "{\"level\":" << e.level << ",\"blur\":" << e.blur << ",\"offsetY\":" << e.offsetY
           << ",\"spread\":" << e.spread << ",\"opacity\":" << e.opacity << "}";
    }
    os << "],\"touchTarget\":{\"min\":" << kMinTouchTarget << ",\"recommended\":" << kRecommendedTouchTarget << "}";
    os << ",\"activeTheme\":" << themeToJson(themeRegistry().active()) << ",\"themes\":[";
    std::vector<std::string> names;
    for (auto& kv : themeRegistry().themes) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size(); i++) { if (i) os << ","; os << "\"" << jsonEscape(names[i]) << "\""; }
    os << "]}";
    return os.str();
}

} // namespace indsin
