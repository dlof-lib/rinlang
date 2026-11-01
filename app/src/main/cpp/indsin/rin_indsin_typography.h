// indsin/rin_indsin_typography.h — الخطوط والطباعة: FontLoad · FontDisplay · FontVariant · FontFeature ·
// VariableFont · SystemFont · CustomFont  (+ TextTransform · TextShadow · LanguageTag كمدخلات طباعية).
//
// طبقة نقية (بلا رسم): تقرأ سمات Strand وتُعيد قراراً طباعياً (TextTypography) يستهلكه
//   rin_indsin_paint.h  (يملأ DrawCommand)  ·  rin_indsin_html.h (CSS/@font-face)  ·  fabricToJson (كتلة "typo")  ·
//   rin_indsin_c_api.cpp (rin_indsin_font_load / rin_indsin_fonts_json).
//
// المفاهيم ↔ السمات (كلها إضافية؛ ملف بلا هذه السمات يُرسم حرفياً كما كان):
//   CustomFont     <Font#Brand src="fonts/brand.woff2" weight="100 900" /> ثم font="Brand, system"
//   FontLoad       load="eager|lazy|preload" · rin_indsin_font_load() · rin_indsin_font_set_status() · حالة: idle→loading→loaded|error
//   FontDisplay    font_display="auto|block|swap|fallback|optional" (على <Font> أو على النص)؛ يحدّد متى يُخفى النص/يُستبدل بالاحتياطي
//   SystemFont     font="system|sans|serif|mono|rounded|cursive|emoji|arabic|kufi|…" (أو اسم خط مثبّت "Helvetica Neue")
//   VariableFont   font_variation="wght 650, wdth 85" · weight=650 · font_stretch=125% · slant=-8 · optical_size=14؛ تُقصّ إلى محاور <Font axes="…">
//   FontVariant    font_variant="small-caps tabular-nums" (→ font-variant + وسوم OpenType المقابلة)
//   FontFeature    font_feature="liga, kern=0, ss01, -dlig, 'tnum' on"
//   TextTransform  text_transform="uppercase|lowercase|capitalize|full-width|none" (يُطبَّق على النص نفسه فيتطابق في كل الواجهات)
//   TextShadow     text_shadow="0 1 2 #0008, 0 0 12 primary"
//   LanguageTag    lang="ar" / "zh-Hans-CN" (يورَّث للأبناء؛ على CodeEditor يبقى لغة الشيفرة)، dir="auto|ltr|rtl"
#pragma once
#include "rin_indsin_font.h"
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include "rin_indsin_webcore.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace indsin {

// ---- بيانات تُحمَل على DrawCommand ---------------------------------------------------------------
struct TextShadow { double dx = 0, dy = 0, blur = 0; Color color{0, 0, 0, 255}; };
struct GlyphFx { float alpha = 1.f, dx = 0.f, dy = 0.f; };  // تأثير وحدة نصية واحدة (حرف أو كلمة)

namespace typo {

// ================================================================================================
// أدوات نصية صغيرة
// ================================================================================================
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
// "smallCaps" / "small_caps" / "Small-Caps" → "small-caps"
inline std::string kebab(const std::string& in) {
    std::string o;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '_' || c == ' ') { if (!o.empty() && o.back() != '-') o.push_back('-'); continue; }
        if (std::isupper(c)) { if (!o.empty() && o.back() != '-' && i > 0 && !std::isupper((unsigned char)in[i - 1])) o.push_back('-'); o.push_back((char)std::tolower(c)); }
        else o.push_back((char)c);
    }
    return o;
}
inline std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) s = s.substr(1, s.size() - 2);
    return s;
}
// تقسيم على فاصل بمستوى أعلى فقط (يحترم الأقواس والاقتباس): "a, rgba(1,2,3), 'b, c'"
inline std::vector<std::string> splitTop(const std::string& s, char sep = ',') {
    std::vector<std::string> out; std::string cur; int depth = 0; char q = 0;
    for (char c : s) {
        if (q) { cur.push_back(c); if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') { q = c; cur.push_back(c); continue; }
        if (c == '(') depth++;
        if (c == ')') depth = std::max(0, depth - 1);
        if (c == sep && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur.push_back(c);
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& x) { return x.empty(); }), out.end());
    return out;
}
inline std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out; std::string cur; int depth = 0; char q = 0;
    for (char c : s) {
        if (q) { cur.push_back(c); if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') { q = c; cur.push_back(c); continue; }
        if (c == '(') depth++;
        if (c == ')') depth = std::max(0, depth - 1);
        if ((std::isspace((unsigned char)c) || c == ',') && depth == 0) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } continue; }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
inline bool parseNum(const std::string& in, double& out) {
    std::string s = trim(in);
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "px") == 0) s.resize(s.size() - 2);
    if (s.size() > 3 && s.compare(s.size() - 3, 3, "deg") == 0) s.resize(s.size() - 3);
    if (s.empty()) return false;
    char* end = nullptr; double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = v; return true;
}
inline bool truthy(const Value* v) { return webTruthy(v); }
inline std::string jsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) o.push_back(' ');
        else o.push_back((char)c);
    }
    return o;
}
inline std::string cssQuote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o.push_back('\\'); o.push_back(c); }
    return o + "\"";
}

// ---- UTF-8 ---------------------------------------------------------------------------------------
inline void appendUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o.push_back((char)cp);
    else if (cp < 0x800) { o.push_back((char)(0xC0 | (cp >> 6))); o.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { o.push_back((char)(0xE0 | (cp >> 12))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
    else { o.push_back((char)(0xF0 | (cp >> 18))); o.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
}
inline std::string encodeUtf8(const std::vector<uint32_t>& cps, size_t from = 0, size_t to = (size_t)-1) {
    std::string o; to = std::min(to, cps.size());
    for (size_t i = from; i < to; i++) appendUtf8(o, cps[i]);
    return o;
}
// نصّ ذو وصل متّصل (عربي/سرياني…): تقسيمه حرفاً حرفاً بعناصر منفصلة يكسر الاتصال، فتُستعمل الكلمات بدلاً منه.
inline bool isCursiveScript(const std::vector<uint32_t>& cps) {
    for (uint32_t c : cps)
        if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0700 && c <= 0x074F) || (c >= 0x0750 && c <= 0x077F) ||
            (c >= 0x08A0 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) || (c >= 0x1800 && c <= 0x18AF)) return true;
    return false;
}

// ================================================================================================
// LanguageTag
// ================================================================================================
inline std::vector<std::string> splitSub(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == '-' || c == '_') { out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    out.push_back(cur);
    return out;
}
inline bool allAlpha(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalpha(c); }); }
inline bool allDigit(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); }
inline bool allAlnum(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c); }); }
// BCP-47 مبسّط: language[-script][-region](-variant)*(-singleton-ext)*(-x-private)
inline bool isValidLangTag(const std::string& tag) {
    if (tag.empty()) return false;
    auto sub = splitSub(lower(tag));
    if (sub.empty()) return false;
    const std::string& l = sub[0];
    if (l == "x") return sub.size() > 1;
    if (!allAlpha(l) || !((l.size() >= 2 && l.size() <= 3) || (l.size() >= 5 && l.size() <= 8))) return false;
    size_t i = 1;
    // امتدادات اللغة (extlang) نادرة؛ لا تُقبل هنا عمداً.
    if (i < sub.size() && sub[i].size() == 4 && allAlpha(sub[i])) i++;                       // script
    if (i < sub.size() && ((sub[i].size() == 2 && allAlpha(sub[i])) || (sub[i].size() == 3 && allDigit(sub[i])))) i++; // region
    while (i < sub.size() && ((sub[i].size() >= 5 && sub[i].size() <= 8 && allAlnum(sub[i])) ||
                              (sub[i].size() == 4 && std::isdigit((unsigned char)sub[i][0]) && allAlnum(sub[i])))) i++; // variants
    while (i < sub.size() && sub[i].size() == 1 && sub[i] != "x" && allAlnum(sub[i])) {      // extensions
        i++; size_t n = 0;
        while (i < sub.size() && sub[i].size() >= 2 && sub[i].size() <= 8 && allAlnum(sub[i])) { i++; n++; }
        if (n == 0) return false;
    }
    if (i < sub.size() && sub[i] == "x") {                                                   // private use
        i++; size_t n = 0;
        while (i < sub.size() && sub[i].size() >= 1 && sub[i].size() <= 8 && allAlnum(sub[i])) { i++; n++; }
        if (n == 0) return false;
    }
    return i == sub.size();
}
// الشكل القانوني: ar · zh-Hans-CN · en-US (لغة صغيرة، نص بحرف كبير أولاً، منطقة كبيرة)
inline std::string normalizeLangTag(const std::string& tag) {
    if (!isValidLangTag(tag)) return "";
    auto sub = splitSub(lower(tag)); std::string o = sub[0];
    bool priv = (sub[0] == "x");
    for (size_t i = 1; i < sub.size(); i++) {
        std::string t = sub[i];
        if (!priv && t.size() == 4 && allAlpha(t)) t[0] = (char)std::toupper((unsigned char)t[0]);
        else if (!priv && t.size() == 2 && allAlpha(t)) for (auto& c : t) c = (char)std::toupper((unsigned char)c);
        if (t == "x") priv = true;
        o += "-" + t;
    }
    return o;
}
inline std::string langPrimary(const std::string& tag) { auto s = splitSub(lower(tag)); return s.empty() ? "" : s[0]; }
inline bool isRtlLang(const std::string& tag) {
    static const char* rtl[] = {"ar", "he", "iw", "fa", "ur", "ps", "sd", "ug", "yi", "dv", "ckb", "ku", "az-arab", "ks", "arc", "syr", "nqo"};
    std::string p = langPrimary(tag), low = lower(tag);
    if (p == "ku") return low.find("arab") != std::string::npos || low.find("iq") != std::string::npos || low.find("ir") != std::string::npos;
    for (auto r : rtl) if (p == r) return true;
    return false;
}

// ================================================================================================
// TextTransform
// ================================================================================================
inline uint32_t cpUpper(uint32_t c, bool turkic = false) {
    if (c < 0x80) { if (turkic && c == 'i') return 0x130; return (c >= 'a' && c <= 'z') ? c - 32 : c; }
    if (c == 0xB5) return 0x39C;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c == 0x131) return 'I';
    if (c >= 0x100 && c <= 0x137 && c != 0x130 && c != 0x131) return (c & 1) ? c - 1 : c;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c : c - 1;
    if (c >= 0x14A && c <= 0x177) return (c & 1) ? c - 1 : c;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c : c - 1;
    if (c == 0x3C2) return 0x3A3;
    if (c >= 0x3B1 && c <= 0x3C9) return c - 32;
    if (c >= 0x3AC && c <= 0x3AF) { static const uint32_t m[] = {0x386, 0x388, 0x389, 0x38A}; return m[c - 0x3AC]; }
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
inline uint32_t cpLower(uint32_t c, bool turkic = false) {
    if (c < 0x80) { if (turkic && c == 'I') return 0x131; return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x178) return 0xFF;
    if (c == 0x130) return turkic ? 'i' : 'i';
    if (c >= 0x100 && c <= 0x137 && c != 0x130 && c != 0x131) return (c & 1) ? c : c + 1;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return (c & 1) ? c : c + 1;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return c - 0x388 + 0x3AD;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
inline bool cpIsLetter(uint32_t c) { return cpUpper(c) != c || cpLower(c) != c || (c >= 0x5D0 && c <= 0x5EA) || (c >= 0x600 && c <= 0x6FF); }

inline std::string normalizeTransform(const std::string& raw) {
    std::string t = kebab(trim(raw));
    if (t == "upper" || t == "uppercase" || t == "all-caps" || t == "caps") return "uppercase";
    if (t == "lower" || t == "lowercase") return "lowercase";
    if (t == "capitalize" || t == "title" || t == "title-case") return "capitalize";
    if (t == "full-width" || t == "fullwidth") return "full-width";
    if (t == "none" || t == "normal" || t.empty()) return "none";
    return "";
}
// lang يخصّ التركية/الأذرية (i→İ، I→ı).
inline std::string applyTextTransform(const std::string& text, const std::string& transform, const std::string& lang = "") {
    std::string tr = normalizeTransform(transform);
    if (tr.empty() || tr == "none" || text.empty()) return text;
    std::string p = langPrimary(lang);
    bool turkic = (p == "tr" || p == "az" || p == "crh" || p == "tt");
    auto cps = font::decodeUtf8(text);
    std::string out;
    if (tr == "uppercase") {
        for (uint32_t c : cps) { if (c == 0xDF) { out += "SS"; continue; } appendUtf8(out, cpUpper(c, turkic)); }
    } else if (tr == "lowercase") {
        for (uint32_t c : cps) appendUtf8(out, cpLower(c, turkic));
    } else if (tr == "capitalize") {
        bool start = true;
        for (uint32_t c : cps) {
            if (c == ' ' || c == '\n' || c == '\t' || c == 0xA0) { start = true; appendUtf8(out, c); continue; }
            if (start && cpIsLetter(c)) { appendUtf8(out, cpUpper(c, turkic)); start = false; }
            else { appendUtf8(out, c); if (c != '(' && c != '"' && c != '\'' && c != '[') start = false; }
        }
    } else if (tr == "full-width") {
        for (uint32_t c : cps) {
            if (c == ' ') appendUtf8(out, 0x3000);
            else if (c >= 0x21 && c <= 0x7E) appendUtf8(out, c + 0xFEE0);
            else appendUtf8(out, c);
        }
    }
    return out;
}

// ================================================================================================
// TextShadow
// ================================================================================================
inline bool parseColorToken(const std::string& tok, Color& out) {
    std::string t = trim(tok);
    if (t.empty()) return false;
    if (resolveSemanticColor(t, out)) return true;
    if (looksLikeHexColor(t)) { out = parseHexColor(t, Color{0, 0, 0, 255}); return true; }
    return false;
}
// "2 2 4 #000" · "#0008 0 1 2" · "0 0 12px primary" · "none"
inline bool parseOneShadow(const std::string& spec, TextShadow& out) {
    TextShadow sh; std::vector<double> nums; bool haveColor = false;
    for (auto& w : words(spec)) {
        double v;
        if (parseNum(w, v)) { nums.push_back(v); continue; }
        Color c; if (!haveColor && parseColorToken(w, c)) { sh.color = c; haveColor = true; continue; }
        return false;
    }
    if (nums.size() < 2 || nums.size() > 3) return false;
    sh.dx = nums[0]; sh.dy = nums[1]; sh.blur = nums.size() > 2 ? std::max(0.0, nums[2]) : 0.0;
    if (!haveColor) sh.color = Color{0, 0, 0, 160};
    out = sh; return true;
}
inline std::vector<TextShadow> parseTextShadows(const std::string& spec, bool* ok = nullptr) {
    std::vector<TextShadow> out; bool good = true;
    std::string s = trim(spec);
    if (ok) *ok = true;
    if (s.empty() || lower(s) == "none") return out;
    for (auto& part : splitTop(s)) { TextShadow sh; if (parseOneShadow(part, sh)) out.push_back(sh); else good = false; }
    if (ok) *ok = good;
    return out;
}
inline Color lerpC(const Color& a, const Color& b, double u) {
    auto m = [&](unsigned char x, unsigned char y) { return (unsigned char)std::max(0.0, std::min(255.0, std::round(x + (y - x) * u))); };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
inline std::string cssColor(const Color& c) {
    std::ostringstream os; os << "rgba(" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (c.a / 255.0) << ")"; return os.str();
}
inline std::string shadowsToCss(const std::vector<TextShadow>& v) {
    if (v.empty()) return "none";
    std::ostringstream os;
    for (size_t i = 0; i < v.size(); i++) os << (i ? "," : "") << v[i].dx << "px " << v[i].dy << "px " << v[i].blur << "px " << cssColor(v[i].color);
    return os.str();
}

// ================================================================================================
// SystemFont — العائلات العامة (generic) → حزم احتياطية عبر المنصات (+ تغطية عربية)
// ================================================================================================
struct GenericFont { const char* name; const char* stack; const char* android; };
inline const std::vector<GenericFont>& genericFonts() {
    static const std::vector<GenericFont> t = {
        {"system",     "system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans Arabic','Noto Sans','Helvetica Neue',Arial,sans-serif", "sans-serif"},
        {"sans-serif", "'Segoe UI',Roboto,'Helvetica Neue','Noto Sans Arabic','Noto Sans',Arial,sans-serif", "sans-serif"},
        {"serif",      "Georgia,'Times New Roman','Noto Naskh Arabic','Noto Serif',serif", "serif"},
        {"monospace",  "ui-monospace,'SF Mono',Menlo,Consolas,'Roboto Mono','DejaVu Sans Mono',monospace", "monospace"},
        {"rounded",    "ui-rounded,'SF Pro Rounded','Nunito','Varela Round','Noto Sans Arabic',system-ui,sans-serif", "sans-serif-medium"},
        {"cursive",    "'Segoe Script','Comic Sans MS','Noto Sans Arabic',cursive", "cursive"},
        {"fantasy",    "Impact,'Noto Sans Arabic',fantasy", "casual"},
        {"emoji",      "'Apple Color Emoji','Segoe UI Emoji','Noto Color Emoji',emoji", "sans-serif"},
        {"math",       "'Cambria Math','STIX Two Math','Latin Modern Math',math", "serif"},
        {"arabic",     "'Noto Naskh Arabic','Traditional Arabic','Amiri','Geeza Pro',serif", "serif"},
        {"kufi",       "'Noto Kufi Arabic','Segoe UI','Tahoma',sans-serif", "sans-serif"},
        {"condensed",  "'Roboto Condensed','Arial Narrow','Helvetica Neue Condensed',sans-serif", "sans-serif-condensed"},
        {"ui-serif",   "ui-serif,Georgia,serif", "serif"},
        {"ui-monospace", "ui-monospace,Menlo,Consolas,monospace", "monospace"},
    };
    return t;
}
// أسماء بديلة → الاسم القانوني للعائلة العامة. يُعيد "" إن لم يكن الاسم عاماً.
inline std::string genericCanonical(const std::string& raw) {
    std::string t = kebab(unquote(raw));
    if (t == "system" || t == "system-ui" || t == "default" || t == "native" || t == "ui-sans-serif" || t == "-apple-system") return "system";
    if (t == "sans" || t == "sans-serif" || t == "ui-sans") return "sans-serif";
    if (t == "serif") return "serif";
    if (t == "mono" || t == "monospace" || t == "code") return "monospace";
    if (t == "rounded" || t == "ui-rounded") return "rounded";
    if (t == "cursive" || t == "handwriting" || t == "script") return "cursive";
    if (t == "fantasy" || t == "display") return "fantasy";
    if (t == "emoji") return "emoji";
    if (t == "math") return "math";
    if (t == "arabic" || t == "naskh") return "arabic";
    if (t == "kufi") return "kufi";
    if (t == "condensed" || t == "narrow") return "condensed";
    if (t == "ui-serif") return "ui-serif";
    if (t == "ui-monospace") return "ui-monospace";
    return "";
}
inline const GenericFont* genericByCanonical(const std::string& c) {
    for (auto& g : genericFonts()) if (c == g.name) return &g;
    return nullptr;
}

// ================================================================================================
// CustomFont / FontLoad / FontDisplay — سجل الخطوط
// ================================================================================================
enum class FontStatus { IDLE, LOADING, LOADED, ERROR };
inline const char* statusName(FontStatus s) {
    switch (s) { case FontStatus::IDLE: return "idle"; case FontStatus::LOADING: return "loading"; case FontStatus::LOADED: return "loaded"; default: return "error"; }
}
struct FontSrc { std::string url, format; bool local = false; };
struct FontAxis { std::string tag; double min = 0, max = 0, def = 0; };
using FontKV = std::vector<std::pair<std::string, std::string>>;

struct FontFace {
    std::string family;
    std::vector<FontSrc> src;
    int weightMin = 400, weightMax = 400;
    std::string style = "normal";      // normal | italic | oblique
    std::string display = "swap";      // auto | block | swap | fallback | optional
    std::string stretch;               // "75% 125%"
    std::string unicodeRange;
    std::vector<FontAxis> axes;
    std::vector<std::string> fallback; // احتياط خاص بهذا الخط
    bool variable = false, preload = false, lazy = false;
    FontStatus status = FontStatus::IDLE;
    std::string error;
    double requestedMs = -1;
    int line = 0;
    bool coversWeight(int w) const { return w >= weightMin && w <= weightMax; }
    const FontAxis* axis(const std::string& tag) const { for (auto& a : axes) if (a.tag == tag) return &a; return nullptr; }
};

inline int weightFromName(const std::string& raw, bool* ok = nullptr) {
    std::string t = kebab(trim(raw));
    static const std::pair<const char*, int> names[] = {
        {"thin", 100}, {"hairline", 100}, {"extra-light", 200}, {"extralight", 200}, {"ultra-light", 200}, {"light", 300},
        {"regular", 400}, {"normal", 400}, {"book", 400}, {"medium", 500}, {"semi-bold", 600}, {"semibold", 600}, {"demi-bold", 600},
        {"bold", 700}, {"extra-bold", 800}, {"extrabold", 800}, {"ultra-bold", 800}, {"black", 900}, {"heavy", 900}, {"extra-black", 950}};
    for (auto& n : names) if (t == n.first) { if (ok) *ok = true; return n.second; }
    double v;
    if (parseNum(t, v) && v >= 1 && v <= 1000) { if (ok) *ok = true; return (int)std::lround(v); }
    if (ok) *ok = false; return 0;
}
inline double stretchFromName(const std::string& raw, bool* ok = nullptr) {
    std::string t = kebab(trim(raw));
    static const std::pair<const char*, double> names[] = {{"ultra-condensed", 50}, {"extra-condensed", 62.5}, {"condensed", 75}, {"semi-condensed", 87.5},
        {"normal", 100}, {"semi-expanded", 112.5}, {"expanded", 125}, {"extra-expanded", 150}, {"ultra-expanded", 200}};
    for (auto& n : names) if (t == n.first) { if (ok) *ok = true; return n.second; }
    std::string p = t; if (!p.empty() && p.back() == '%') p.pop_back();
    double v; if (parseNum(p, v) && v > 0) { if (ok) *ok = true; return v; }
    if (ok) *ok = false; return 100;
}

inline bool validTag(const std::string& t) {
    if (t.size() != 4) return false;
    for (unsigned char c : t) if (c < 0x20 || c > 0x7E) return false;
    return true;
}
// "wght 100 900, wdth 75..125 100, 'opsz' 8 144"  → محاور
inline std::vector<FontAxis> parseAxes(const std::string& spec, std::string* err = nullptr) {
    std::vector<FontAxis> out;
    for (auto& part : splitTop(spec)) {
        auto w = words(part);
        if (w.empty()) continue;
        std::string tag = unquote(w[0]);
        if (!validTag(tag)) { if (err) *err = "invalid axis tag '" + tag + "' (must be 4 characters, e.g. wght)"; continue; }
        FontAxis a; a.tag = tag;
        std::vector<double> nums;
        for (size_t i = 1; i < w.size(); i++) {
            size_t dots = w[i].find("..");
            if (dots != std::string::npos) { double lo, hi; if (parseNum(w[i].substr(0, dots), lo) && parseNum(w[i].substr(dots + 2), hi)) { nums.push_back(lo); nums.push_back(hi); } continue; }
            double v; if (parseNum(w[i], v)) nums.push_back(v);
        }
        if (nums.size() >= 2) { a.min = std::min(nums[0], nums[1]); a.max = std::max(nums[0], nums[1]); a.def = nums.size() > 2 ? nums[2] : a.min; }
        else if (nums.size() == 1) { a.min = a.max = a.def = nums[0]; }
        else { if (err) *err = "axis '" + tag + "' needs a range, e.g. " + tag + " 100 900"; continue; }
        out.push_back(a);
    }
    return out;
}
// محاور معروفة بحدود افتراضية (حين لا يصرّح <Font axes=…> بحدود)
inline bool builtinAxisRange(const std::string& tag, double& lo, double& hi) {
    if (tag == "wght") { lo = 1; hi = 1000; return true; }
    if (tag == "wdth") { lo = 25; hi = 200; return true; }
    if (tag == "slnt") { lo = -90; hi = 90; return true; }
    if (tag == "ital") { lo = 0; hi = 1; return true; }
    if (tag == "opsz") { lo = 1; hi = 2048; return true; }
    return false;
}

class FontRegistry {
public:
    // مصدر الوقت (ms) — قابل للاستبدال في الاختبارات.
    std::function<double()> clock = [] {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    std::string lastError;

    // يُعرِّف وجهاً (family + weight + style). تصريح مكرّر لنفس (family, weights, style) يستبدل القديم ويحتفظ بالحالة.
    bool declare(const std::string& family, const FontKV& kv, std::string& err, int line = 0) {
        err.clear();
        std::string fam = unquote(family);
        if (fam.empty()) { err = "font needs a name"; return false; }
        FontFace f; f.family = fam; f.line = line;
        std::string srcSpec, axesSpec, weightSpec, load;
        for (auto& p : kv) {
            std::string k = kebab(p.first); const std::string& v = p.second;
            if (k == "family" || k == "name") f.family = unquote(v);
            else if (k == "src" || k == "url" || k == "file" || k == "source") srcSpec = v;
            else if (k == "local") { FontSrc s; s.url = unquote(v); s.local = true; f.src.push_back(s); }
            else if (k == "weight" || k == "font-weight") weightSpec = v;
            else if (k == "style" || k == "font-style") f.style = kebab(unquote(v));
            else if (k == "display" || k == "font-display") f.display = kebab(unquote(v));
            else if (k == "stretch" || k == "font-stretch") f.stretch = unquote(v);
            else if (k == "unicode-range" || k == "unicode") f.unicodeRange = unquote(v);
            else if (k == "axes" || k == "variations") axesSpec = v;
            else if (k == "variable") { Value tv = Value::txt(v); f.variable = truthy(&tv); }
            else if (k == "preload") f.preload = webLower(v) == "true" || v == "1";
            else if (k == "load") load = kebab(unquote(v));
            else if (k == "fallback") { for (auto& x : splitTop(v)) f.fallback.push_back(unquote(x)); }
        }
        static const char* displays[] = {"auto", "block", "swap", "fallback", "optional"};
        if (std::find_if(std::begin(displays), std::end(displays), [&](const char* d) { return f.display == d; }) == std::end(displays)) {
            err = "font_display must be auto|block|swap|fallback|optional (got '" + f.display + "')"; f.display = "swap";
        }
        if (f.style != "normal" && f.style != "italic" && f.style != "oblique") f.style = "normal";
        if (!weightSpec.empty()) {
            auto w = words(unquote(weightSpec));
            bool ok1 = true, ok2 = true;
            int lo = weightFromName(w.empty() ? "400" : w[0], &ok1), hi = lo;
            if (w.size() > 1) hi = weightFromName(w[1], &ok2);
            if (ok1 && ok2) { f.weightMin = std::min(lo, hi); f.weightMax = std::max(lo, hi); }
            else err = "invalid weight '" + weightSpec + "'";
        }
        if (!axesSpec.empty()) { std::string e; f.axes = parseAxes(axesSpec, &e); if (!e.empty() && err.empty()) err = e; }
        if (f.weightMin != f.weightMax) f.variable = true;
        if (!f.axes.empty()) f.variable = true;
        if (f.variable && f.weightMin == f.weightMax) { if (auto* a = f.axis("wght")) { f.weightMin = (int)std::lround(a->min); f.weightMax = (int)std::lround(a->max); } }
        if (f.variable && !f.axis("wght") && f.weightMin != f.weightMax) f.axes.push_back({"wght", (double)f.weightMin, (double)f.weightMax, 400.0});
        f.lazy = (load == "lazy");
        if (load == "preload") f.preload = true;
        for (auto& part : splitTop(srcSpec)) {
            FontSrc s; std::string rest = part;
            auto grab = [&](const std::string& fn, std::string& into) {
                size_t p = rest.find(fn + "(");
                if (p == std::string::npos) return false;
                size_t q = rest.find(')', p);
                if (q == std::string::npos) return false;
                into = unquote(rest.substr(p + fn.size() + 1, q - p - fn.size() - 1));
                rest.erase(p, q - p + 1);
                return true;
            };
            std::string u, fmt;
            if (grab("local", u)) { s.url = u; s.local = true; }
            else if (grab("url", u)) s.url = u;
            if (grab("format", fmt)) s.format = lower(fmt);
            if (s.url.empty()) s.url = unquote(trim(rest));
            if (s.url.empty()) continue;
            if (s.format.empty() && !s.local) {
                std::string low = lower(s.url); size_t q = low.find_first_of("?#"); if (q != std::string::npos) low.resize(q);
                size_t dot = low.rfind('.');
                std::string ext = dot == std::string::npos ? "" : low.substr(dot + 1);
                if (ext == "woff2") s.format = "woff2"; else if (ext == "woff") s.format = "woff"; else if (ext == "ttf") s.format = "truetype";
                else if (ext == "otf") s.format = "opentype"; else if (ext == "ttc") s.format = "collection"; else if (ext == "eot") s.format = "embedded-opentype";
            }
            f.src.push_back(s);
        }
        if (f.src.empty()) { err = "font '" + f.family + "' needs src=\"path/or/url\" (or local=\"System Font Name\")"; return false; }
        const double now = clock();
        auto& vec = faces_[lower(f.family)];
        for (auto& e : vec) {
            if (e.weightMin == f.weightMin && e.weightMax == f.weightMax && e.style == f.style) { f.status = e.status; f.error = e.error; f.requestedMs = e.requestedMs; e = f; return finish(e, now); }
        }
        vec.push_back(f);
        return finish(vec.back(), now);
    }
    // FontLoad: يطلب التحميل (idle → loading). يُستدعى من declare (eager) ومن أول استعمال (lazy) ومن المضيف.
    bool requestLoad(const std::string& family) {
        auto it = faces_.find(lower(unquote(family))); if (it == faces_.end()) return false;
        for (auto& f : it->second) if (f.status == FontStatus::IDLE) { f.status = FontStatus::LOADING; f.requestedMs = clock(); }
        return true;
    }
    // المضيف يبلّغ بالنتيجة: loaded | error | loading | idle. يعيد false إن لم يوجد الخط.
    bool setStatus(const std::string& family, const std::string& status, const std::string& error = "") {
        auto it = faces_.find(lower(unquote(family))); if (it == faces_.end()) return false;
        std::string s = lower(status);
        FontStatus st = s == "loaded" || s == "ready" || s == "ok" ? FontStatus::LOADED : s == "error" || s == "failed" ? FontStatus::ERROR
                      : s == "loading" ? FontStatus::LOADING : FontStatus::IDLE;
        for (auto& f : it->second) { f.status = st; f.error = st == FontStatus::ERROR ? error : ""; if (st == FontStatus::LOADING && f.requestedMs < 0) f.requestedMs = clock(); }
        return true;
    }
    void markAllLoaded() { for (auto& kv : faces_) for (auto& f : kv.second) { f.status = FontStatus::LOADED; f.error.clear(); } }
    bool has(const std::string& family) const { return faces_.count(lower(unquote(family))) != 0; }
    const std::vector<FontFace>* faces(const std::string& family) const {
        auto it = faces_.find(lower(unquote(family))); return it == faces_.end() ? nullptr : &it->second;
    }
    // أنسب وجه لـ (weight, style): نفس النمط ثم الأقرب وزناً.
    const FontFace* best(const std::string& family, int weight, const std::string& style) const {
        auto* v = faces(family); if (!v || v->empty()) return nullptr;
        const FontFace* b = nullptr; long bs = -1;
        for (auto& f : *v) {
            long score = 0;
            if (f.style == style) score += 100000; else if (style != "normal" && f.style != "normal") score += 50000;
            if (f.coversWeight(weight)) score += 10000;
            else score += 5000 - std::min(5000, std::min(std::abs(weight - f.weightMin), std::abs(weight - f.weightMax)));
            if (score > bs) { bs = score; b = &f; }
        }
        return b;
    }
    // الوجه الذي استُعمل فعلاً: lazy → يبدأ التحميل. (يغيّر الحالة؛ لذلك غير const)
    const FontFace* use(const std::string& family, int weight, const std::string& style) {
        const FontFace* f = best(family, weight, style);
        if (f && f->status == FontStatus::IDLE) { requestLoad(family); f = best(family, weight, style); }
        return f;
    }
    // FontDisplay: هل يُستعمل الخط المخصّص الآن؟ هل يُخفى النص (فترة الحجب)؟
    struct Decision { bool useCustom = false; bool blocked = false; };
    Decision decide(const FontFace& f, const std::string& displayOverride = "") const {
        Decision d;
        if (f.status == FontStatus::LOADED) { d.useCustom = true; return d; }
        std::string disp = displayOverride.empty() ? f.display : displayOverride;
        if (disp == "auto") disp = "block";
        double elapsed = f.requestedMs < 0 ? 0.0 : std::max(0.0, clock() - f.requestedMs);
        bool loading = f.status == FontStatus::LOADING;
        if (disp == "block") d.blocked = loading && elapsed < 3000;
        else if (disp == "fallback" || disp == "optional") d.blocked = loading && elapsed < 100;
        return d; // swap: احتياطي فوراً (والتبديل عند اكتمال التحميل)
    }
    std::vector<std::string> families() const { std::vector<std::string> o; for (auto& kv : faces_) o.push_back(kv.second.front().family); return o; }
    void clear() { faces_.clear(); lastError.clear(); }
    bool empty() const { return faces_.empty(); }
    template <class Fn> void each(Fn fn) const { for (auto& kv : faces_) for (auto& f : kv.second) fn(f); }

private:
    bool finish(FontFace& f, double now) {
        if (f.status == FontStatus::IDLE && !f.lazy) { f.status = FontStatus::LOADING; f.requestedMs = now; }
        return true;
    }
    std::map<std::string, std::vector<FontFace>> faces_;
};
inline FontRegistry& fontRegistry() { static FontRegistry r; return r; }

// ---- قاعدة @font-face لكل وجه (للتصدير HTML/CSS) ---------------------------------------------
inline std::string fontFaceCss(const FontFace& f) {
    std::ostringstream os;
    os << "@font-face{font-family:" << cssQuote(f.family) << ";src:";
    for (size_t i = 0; i < f.src.size(); i++) {
        os << (i ? "," : "");
        if (f.src[i].local) os << "local(" << cssQuote(f.src[i].url) << ")";
        else { os << "url(" << cssQuote(f.src[i].url) << ")"; if (!f.src[i].format.empty()) os << " format(" << cssQuote(f.src[i].format) << ")"; }
    }
    os << ";font-weight:" << f.weightMin; if (f.weightMax != f.weightMin) os << " " << f.weightMax;
    os << ";font-style:" << f.style << ";font-display:" << f.display;
    if (!f.stretch.empty()) os << ";font-stretch:" << f.stretch;
    if (!f.unicodeRange.empty()) os << ";unicode-range:" << f.unicodeRange;
    os << "}";
    return os.str();
}
inline std::string fontFacesCss() {
    std::string o; fontRegistry().each([&](const FontFace& f) { o += fontFaceCss(f) + "\n"; }); return o;
}
inline std::string fontPreloadLinks(const std::function<std::string(const std::string&)>& esc) {
    std::string o;
    fontRegistry().each([&](const FontFace& f) {
        if (!f.preload) return;
        for (auto& s : f.src) if (!s.local) { o += "<link rel=\"preload\" as=\"font\" crossorigin href=\"" + esc(s.url) + "\"" + (s.format.empty() ? "" : " type=\"font/" + (s.format == "truetype" ? std::string("ttf") : s.format == "opentype" ? std::string("otf") : s.format) + "\"") + ">\n"; break; }
    });
    return o;
}

// ================================================================================================
// FontVariant / FontFeature
// ================================================================================================
using FeatureList = std::vector<std::pair<std::string, int>>;
inline bool variantFeatures(const std::string& token, FeatureList& out) {
    static const std::map<std::string, FeatureList> m = {
        {"small-caps", {{"smcp", 1}}}, {"all-small-caps", {{"smcp", 1}, {"c2sc", 1}}}, {"petite-caps", {{"pcap", 1}}},
        {"all-petite-caps", {{"pcap", 1}, {"c2pc", 1}}}, {"unicase", {{"unic", 1}}}, {"titling-caps", {{"titl", 1}}},
        {"lining-nums", {{"lnum", 1}}}, {"oldstyle-nums", {{"onum", 1}}}, {"proportional-nums", {{"pnum", 1}}}, {"tabular-nums", {{"tnum", 1}}},
        {"diagonal-fractions", {{"frac", 1}}}, {"stacked-fractions", {{"afrc", 1}}}, {"ordinal", {{"ordn", 1}}}, {"slashed-zero", {{"zero", 1}}},
        {"common-ligatures", {{"liga", 1}, {"clig", 1}}}, {"no-common-ligatures", {{"liga", 0}, {"clig", 0}}},
        {"discretionary-ligatures", {{"dlig", 1}}}, {"no-discretionary-ligatures", {{"dlig", 0}}},
        {"historical-ligatures", {{"hlig", 1}}}, {"no-historical-ligatures", {{"hlig", 0}}},
        {"contextual", {{"calt", 1}}}, {"no-contextual", {{"calt", 0}}}, {"sub", {{"subs", 1}}}, {"super", {{"sups", 1}}}, {"ruby", {{"ruby", 1}}},
        {"jis78", {{"jp78", 1}}}, {"jis83", {{"jp83", 1}}}, {"jis90", {{"jp90", 1}}}, {"jis04", {{"jp04", 1}}},
        {"simplified", {{"smpl", 1}}}, {"traditional", {{"trad", 1}}}, {"full-width", {{"fwid", 1}}}, {"proportional-width", {{"pwid", 1}}},
        {"normal", {}}, {"none", {{"liga", 0}, {"clig", 0}}},
    };
    auto it = m.find(token); if (it == m.end()) return false;
    out.insert(out.end(), it->second.begin(), it->second.end()); return true;
}
// "small-caps tabular-nums" → css قيمة font-variant + وسوم OpenType. bad = رموز مجهولة.
inline void parseVariant(const std::string& spec, std::string& css, FeatureList& feats, std::vector<std::string>& bad) {
    std::vector<std::string> ok;
    for (auto& w : words(spec)) {
        std::string t = kebab(unquote(w));
        if (t == "smallcaps") t = "small-caps";
        FeatureList f;
        if (variantFeatures(t, f)) { ok.push_back(t); feats.insert(feats.end(), f.begin(), f.end()); }
        else bad.push_back(w);
    }
    css.clear();
    for (size_t i = 0; i < ok.size(); i++) css += (i ? " " : "") + ok[i];
}
// "liga, kern=0, 'ss01', smcp on, -dlig, tnum 1" → قائمة (tag, value)
inline FeatureList parseFeatures(const std::string& spec, std::vector<std::string>& bad) {
    FeatureList out;
    for (auto& part : splitTop(spec)) {
        auto w = words(part);
        if (w.empty()) continue;
        std::string tag = w[0]; int val = 1; bool haveVal = false;
        size_t eq = tag.find('=');
        if (eq != std::string::npos) { std::string vs = tag.substr(eq + 1); tag = tag.substr(0, eq); double v; if (parseNum(vs, v)) { val = (int)v; haveVal = true; } }
        tag = unquote(tag);
        if (!tag.empty() && (tag[0] == '-' || tag[0] == '!')) { tag = tag.substr(1); val = 0; haveVal = true; }
        else if (tag.rfind("no-", 0) == 0 && tag.size() == 7) { tag = tag.substr(3); val = 0; haveVal = true; }
        if (!haveVal && w.size() > 1) {
            std::string vs = lower(w[1]);
            if (vs == "on") val = 1; else if (vs == "off") val = 0; else { double v; if (parseNum(vs, v)) val = (int)v; }
        }
        if (!validTag(tag)) { bad.push_back(part); continue; }
        bool replaced = false;
        for (auto& e : out) if (e.first == tag) { e.second = val; replaced = true; }
        if (!replaced) out.push_back({tag, val});
    }
    return out;
}
inline std::string featuresToCss(const FeatureList& f) {
    if (f.empty()) return "";
    std::string o;
    for (size_t i = 0; i < f.size(); i++) o += (i ? "," : "") + cssQuote(f[i].first) + " " + std::to_string(f[i].second);
    return o;
}
// يدمج: variant أولاً ثم font_feature (الصريح يتغلّب).
inline FeatureList mergeFeatures(const FeatureList& a, const FeatureList& b) {
    FeatureList out = a;
    for (auto& e : b) { bool r = false; for (auto& x : out) if (x.first == e.first) { x.second = e.second; r = true; } if (!r) out.push_back(e); }
    return out;
}

// ================================================================================================
// TextTypography — قرار طباعي لعنصر واحد
// ================================================================================================
struct AttrScope {
    const Strand* self = nullptr;
    const std::vector<const Strand*>* ancestors = nullptr; // من الجذر نحو الأب
    const Strand* rootDefaults = nullptr;
    const Value* get(const char* key, bool inherit = true) const {
        if (self) if (const Value* v = self->attr(key)) return v;
        if (!inherit) return nullptr;
        if (ancestors) for (auto it = ancestors->rbegin(); it != ancestors->rend(); ++it) if (const Value* v = (*it)->attr(key)) return v;
        if (rootDefaults && rootDefaults != self) if (const Value* v = rootDefaults->attr(key)) return v;
        return nullptr;
    }
    std::string str(const char* key, const std::string& def = "", bool inherit = true) const { const Value* v = get(key, inherit); return v ? v->asString() : def; }
};

struct TextTypography {
    bool any = false;
    // الخط
    std::string family;                 // حزمة CSS كاملة ("" = افتراضي المضيف)
    std::string primary;                // أول اسم غير عام (للمضيفات بلا CSS)
    std::string generic;                // الاسم القانوني للعائلة العامة الأولى (system/serif/monospace…)
    std::string androidFamily;          // مرادف Android للعامة
    bool custom = false;                // يستعمل خطاً مسجّلاً (CustomFont)
    std::string customFamily;
    std::string status;                 // idle|loading|loaded|error (للخط المخصّص)
    std::string display;                // FontDisplay الفعلي
    bool blocked = false;               // فترة الحجب: لا يُرسم النص بعد
    bool fallbackActive = false;        // الخط المخصّص غير جاهز فيُستعمل الاحتياطي
    int weight = 0; bool italic = false; double oblique = 0;
    std::string variant, features, variations, stretch, kerning, opticalSizing;
    FeatureList featureList; std::vector<std::pair<std::string, double>> variationList;
    std::string transform = "none";
    std::vector<TextShadow> shadows;
    std::string lang, dir;
    double rotate = 0;
    std::vector<std::string> warnings;
    bool hasFont() const { return !family.empty() || weight || italic || oblique != 0 || !variant.empty() || !features.empty() || !variations.empty() || !stretch.empty() || !kerning.empty(); }
    // قواعد CSS للنص (بلا الإزاحة/الدوران/الظل التي تُكتب منفصلة)
    std::string css() const {
        std::string o;
        if (!family.empty()) o += "font-family:" + family + ";";
        if (weight) o += "font-weight:" + std::to_string(weight) + ";";
        if (italic) o += "font-style:italic;"; else if (oblique != 0) { std::ostringstream t; t << "font-style:oblique " << oblique << "deg;"; o += t.str(); }
        if (!variant.empty()) o += "font-variant:" + variant + ";";
        if (!features.empty()) o += "font-feature-settings:" + features + ";";
        if (!variations.empty()) o += "font-variation-settings:" + variations + ";";
        if (!stretch.empty()) o += "font-stretch:" + stretch + ";";
        if (!kerning.empty()) o += "font-kerning:" + kerning + ";";
        if (!opticalSizing.empty()) o += "font-optical-sizing:" + opticalSizing + ";";
        return o;
    }
};

// يبني حزمة العائلات من font="Brand, system". reg قابل للتعديل (lazy loading).
struct StackResult { std::string stack, primary, generic, android, customFamily; bool endsWithGeneric = false; };
inline StackResult buildStack(const std::string& spec, FontRegistry& reg, const FontFace* customFallbackSrc = nullptr) {
    StackResult r; std::vector<std::string> parts;
    auto push = [&](const std::string& p) { if (std::find(parts.begin(), parts.end(), p) == parts.end()) parts.push_back(p); };
    for (auto& item : splitTop(spec)) {
        std::string name = unquote(item);
        if (name.empty()) continue;
        std::string g = genericCanonical(name);
        if (!g.empty()) {
            if (const GenericFont* gf = genericByCanonical(g)) { push(gf->stack); if (r.generic.empty()) { r.generic = g; r.android = gf->android; } r.endsWithGeneric = true; }
            continue;
        }
        r.endsWithGeneric = false;
        if (r.primary.empty()) r.primary = name;
        if (reg.has(name) && r.customFamily.empty()) r.customFamily = name;
        push(cssQuote(name));
    }
    (void)customFallbackSrc;
    if (parts.empty()) return r;
    if (!r.endsWithGeneric) {
        std::vector<std::string> fb;
        if (!r.customFamily.empty()) if (auto* fs = reg.faces(r.customFamily)) if (!fs->empty()) fb = fs->front().fallback;
        bool added = false;
        for (auto& f : fb) {
            std::string g = genericCanonical(f);
            if (!g.empty()) { if (const GenericFont* gf = genericByCanonical(g)) { push(gf->stack); if (r.generic.empty()) { r.generic = g; r.android = gf->android; } added = true; } }
            else push(cssQuote(unquote(f)));
        }
        if (!added || true) { const GenericFont* sys = genericByCanonical("system"); push(sys->stack); if (r.generic.empty()) { r.generic = "system"; r.android = sys->android; } }
    }
    for (size_t i = 0; i < parts.size(); i++) r.stack += (i ? "," : "") + parts[i];
    return r;
}

inline bool isTextual(StrandKind k) { return k != StrandKind::CODE_EDITOR; }

// الحلّ الكامل. reg غير const لأن lazy يبدأ التحميل عند أول استعمال.
inline TextTypography resolveTypography(const AttrScope& sc, FontRegistry& reg = fontRegistry()) {
    TextTypography t;
    const Strand& s = *sc.self;
    // ---- family ----
    std::string famSpec;
    for (const char* k : {"font", "font_family", "typeface"}) { const Value* v = sc.get(k); if (v && !trim(v->asString()).empty()) { famSpec = v->asString(); break; } }
    // التوافق مع font_local= / font_url= (Android): وجه ضمني اسمه المسار نفسه.
    if (famSpec.empty()) {
        std::string fl = trim(sc.str("font_local", "", false)), fu = trim(sc.str("font_url", "", false));
        std::string path = !fl.empty() ? fl : fu;
        if (!path.empty()) {
            std::string fam = "file:" + path;
            if (!reg.has(fam)) { std::string e; FontKV kv{{"src", path}, {"display", "swap"}}; if (!fl.empty()) kv.push_back({"load", "lazy"}); reg.declare(fam, kv, e); }
            famSpec = fam;
        }
    }
    // ---- weight / style ----
    int w = 0;
    for (const char* k : {"font_weight", "weight", "fontWeight"}) {
        const Value* v = (std::string(k) == "font_weight") ? sc.get(k) : s.attr(k);
        if (!v) continue;
        bool ok = false; int n = v->kind == Value::Kind::NUMBER ? (int)std::lround(v->number) : weightFromName(v->asString(), &ok);
        if (v->kind == Value::Kind::NUMBER) ok = n >= 1 && n <= 1000;
        if (ok) { w = n; break; }
        t.warnings.push_back("invalid font weight '" + v->asString() + "'");
    }
    if (w == 0 && webLower(s.attrStr("bold", "")) == "true") w = 700;
    std::string fstyle = kebab(sc.str("font_style", ""));
    bool italic = fstyle == "italic" || truthy(sc.get("italic"));
    double oblique = 0;
    if (fstyle.rfind("oblique", 0) == 0) { oblique = 14; if (fstyle.size() > 8 && fstyle[7] == '-') parseNum(fstyle.substr(8), oblique); }
    if (const Value* sl = sc.get("slant")) { double v; if (parseNum(sl->asString(), v)) { oblique = -v; } } // slant موجب = ميل لليمين
    t.italic = italic; t.oblique = italic ? 0 : oblique; t.weight = w;

    // ---- الحزمة + الوجه المخصّص ----
    const FontFace* face = nullptr;
    if (!famSpec.empty()) {
        StackResult sr = buildStack(famSpec, reg);
        t.family = sr.stack; t.primary = sr.primary; t.generic = sr.generic; t.androidFamily = sr.android;
        if (!sr.customFamily.empty()) {
            t.custom = true; t.customFamily = sr.customFamily;
            face = reg.use(sr.customFamily, w ? w : 400, italic ? "italic" : (oblique != 0 ? "oblique" : "normal"));
            if (face) {
                std::string over = kebab(sc.str("font_display", ""));
                t.display = over.empty() ? face->display : over;
                auto d = reg.decide(*face, over);
                t.status = statusName(face->status);
                t.blocked = d.blocked;
                t.fallbackActive = !d.useCustom;
            }
        }
    }
    // ---- variant / feature / variation ----
    std::vector<std::string> bad;
    FeatureList vf;
    if (const Value* v = sc.get("font_variant")) {
        std::string css; parseVariant(v->asString(), css, vf, bad);
        t.variant = css == "normal" ? "" : css;
        for (auto& b : bad) t.warnings.push_back("unknown font_variant value '" + b + "'");
    }
    FeatureList ff;
    {
        std::vector<std::string> bad2;
        for (const char* k : {"font_feature", "font_features", "font_feature_settings", "features"})
            if (const Value* v = sc.get(k)) { ff = parseFeatures(v->asString(), bad2); break; }
        for (auto& b : bad2) t.warnings.push_back("invalid font_feature '" + b + "' (tags are 4 characters, e.g. liga, ss01)");
    }
    t.featureList = mergeFeatures(vf, ff);
    t.features = featuresToCss(ff); // font-variant يُكتب منفصلاً؛ font-feature-settings للصريح فقط
    if (const Value* v = sc.get("font_kerning")) { std::string k = lower(trim(v->asString())); if (k == "normal" || k == "none" || k == "auto") t.kerning = k; else t.warnings.push_back("font_kerning must be auto|normal|none"); }
    if (const Value* v = sc.get("font_optical_sizing")) { std::string k = lower(trim(v->asString())); if (k == "auto" || k == "none") t.opticalSizing = k; }

    // المحاور المتغيّرة: font_variation صريح + مختصرات (weight عددي/stretch/slant/optical_size)
    std::vector<std::pair<std::string, double>> var;
    auto setVar = [&](const std::string& tag, double v) { for (auto& e : var) if (e.first == tag) { e.second = v; return; } var.push_back({tag, v}); };
    bool variableCtx = face && face->variable;
    if (const Value* v = sc.get("font_variation")) {
        for (auto& part : splitTop(v->asString())) {
            auto ws = words(part); std::string tag; double val = 0; bool haveVal = false;
            if (ws.empty()) continue;
            size_t eq = ws[0].find('=');
            if (eq != std::string::npos) { tag = unquote(ws[0].substr(0, eq)); haveVal = parseNum(ws[0].substr(eq + 1), val); }
            else { tag = unquote(ws[0]); if (ws.size() > 1) haveVal = parseNum(ws[1], val); }
            if (!validTag(tag) || !haveVal) { t.warnings.push_back("invalid font_variation '" + part + "' (use: wght 650, wdth 85)"); continue; }
            setVar(tag, val);
        }
    }
    bool stretchSet = false; double stretchVal = 100;
    if (const Value* v = sc.get("font_stretch")) {
        bool ok = false; stretchVal = stretchFromName(v->asString(), &ok);
        if (ok) { stretchSet = true; std::ostringstream o; o << stretchVal << "%"; t.stretch = o.str(); } else t.warnings.push_back("invalid font_stretch '" + v->asString() + "'");
    }
    if (variableCtx || !var.empty()) {
        if (w && !std::any_of(var.begin(), var.end(), [](auto& e) { return e.first == "wght"; })) setVar("wght", w);
        if (stretchSet && !std::any_of(var.begin(), var.end(), [](auto& e) { return e.first == "wdth"; })) setVar("wdth", stretchVal);
        if (oblique != 0 && !italic && !std::any_of(var.begin(), var.end(), [](auto& e) { return e.first == "slnt"; })) setVar("slnt", -oblique);
        if (const Value* o = sc.get("optical_size")) { double v; std::string ov = lower(trim(o->asString())); if (ov != "auto" && parseNum(ov, v)) setVar("opsz", v); }
    }
    // قصّ كل محور إلى مداه (محاور الوجه أولاً ثم الحدود المعروفة)
    for (auto& e : var) {
        double lo, hi; bool have = false;
        if (face) if (const FontAxis* a = face->axis(e.first)) { lo = a->min; hi = a->max; have = true; }
        if (!have) have = builtinAxisRange(e.first, lo, hi);
        if (have && (e.second < lo || e.second > hi)) {
            t.warnings.push_back("axis '" + e.first + "' value " + std::to_string((int)e.second) + " is outside " + std::to_string((int)lo) + ".." + std::to_string((int)hi) + " (clamped)");
            e.second = std::max(lo, std::min(hi, e.second));
        }
    }
    t.variationList = var;
    for (size_t i = 0; i < var.size(); i++) { std::ostringstream o; o << (i ? "," : "") << cssQuote(var[i].first) << " " << var[i].second; t.variations += o.str(); }
    if (!var.empty() && !t.variations.empty()) {
        // وزن CSS يتبع محور wght كي لا يتعارض الاثنان
        for (auto& e : var) if (e.first == "wght") t.weight = (int)std::lround(e.second);
    }

    // ---- TextTransform / TextShadow / Lang / Dir ----
    if (const Value* v = sc.get("text_transform")) {
        std::string n = normalizeTransform(v->asString());
        if (n.empty()) t.warnings.push_back("unknown text_transform '" + v->asString() + "'"); else t.transform = n;
    }
    if (const Value* v = sc.get("text_shadow")) {
        bool ok = true; t.shadows = parseTextShadows(v->asString(), &ok);
        if (!ok) t.warnings.push_back("invalid text_shadow '" + v->asString() + "' (use: dx dy blur color)");
    }
    if (isTextual(s.kind)) {
        std::string lang = sc.str("lang", "");
        if (lang.empty()) lang = sc.str("locale", "");
        if (lang.empty()) lang = sc.str("language_tag", "");
        if (!lang.empty()) {
            std::string n = normalizeLangTag(unquote(lang));
            if (n.empty()) t.warnings.push_back("invalid language tag '" + lang + "'"); else t.lang = n;
        }
    }
    {
        std::string d = lower(trim(sc.str("dir", "")));
        if (d.empty()) { std::string dd = lower(trim(sc.str("direction", ""))); if (dd == "rtl" || dd == "ltr") d = dd; }
        if (d.empty() && !t.lang.empty() && isRtlLang(t.lang)) d = "rtl";
        if (d == "rtl" || d == "ltr" || d == "auto") t.dir = d;
    }
    if (const Value* v = sc.get("text_rotate", false)) { double r; if (parseNum(v->asString(), r)) t.rotate = r; }
    t.any = t.hasFont() || t.transform != "none" || !t.shadows.empty() || !t.lang.empty() || !t.dir.empty() || t.rotate != 0;
    return t;
}

} // namespace typo

// تعريف الخطوط من البرنامج: @font=Family … .end/font  (و <Font#Family …/> في صيغة الوسوم). تُستدعى من الـpipeline
// قبل بناء الـFabric (بجانب registerEffectsFromProgram). تصريح مكرّر يستبدل ويحتفظ بحالة التحميل.
inline void registerFontsFromProgram(const std::vector<rin::StmtPtr>& program, WarpScope& warp) {
    for (auto& stmt : program) {
        auto f = std::dynamic_pointer_cast<rin::FontStmt>(stmt);
        if (!f) continue;
        typo::FontKV kv;
        for (auto& a : f->attrs) { Value v = evalAttrExprKeyed(a.key, a.value, warp, nullptr); kv.push_back({a.key, v.asString()}); }
        std::string err;
        if (!typo::fontRegistry().declare(f->name, kv, err, f->line)) typo::fontRegistry().lastError = "line " + std::to_string(f->line) + ": " + err;
        else if (!err.empty()) typo::fontRegistry().lastError = "line " + std::to_string(f->line) + ": " + err;
        else typo::fontRegistry().lastError.clear();
    }
}

} // namespace indsin
