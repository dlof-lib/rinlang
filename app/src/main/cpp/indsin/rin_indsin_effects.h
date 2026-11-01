// indsin/rin_indsin_effects.h — Effects Studio: إنشاء تأثيرات حركية كاملة من Rin ومن indsin.
//
// ما الجديد مقارنةً بالمحرّك القديم (الذي كان يُغيّر هندسة الـFabric في مكانها ويُراكم الإزاحة مع كل tick):
//   1) التأثير لم يعد يعدّل الـFabric إطلاقاً: يُحسَب «إطار» لكل Strand ثم يُطبَّق على DrawList وقت الطلاء
//      (rin_indsin_effects_paint.h). لا انجراف، ولا تعارض مع كاش التخطيط، والتأثير يحمل كل أبناء العنصر معه.
//   2) التأثيرات تعريفات (EffectDef) في مكتبة (EffectLibrary) — المدمجة نفسها معرَّفة بنفس لغة التعريف:
//        keyframes     «0%{opacity:0,y:24} 60%{y:-4} 100%{opacity:1,y:0}»
//        معادلات        x_fn="sin(t*pi*6)*(1-t)*8"   (t = تقدّم الدورة 0..1، n = رقم الدورة، w/h = حجم العنصر)
//        توقيت          duration / delay / iterations (رقم | infinite) / direction / fill / easing (cubic-bezier, steps)
//   3) القنوات: opacity · x · y (px أو %) · scale · scaleX · scaleY · glow (توهّج) · glowColor · bg · color
//   4) مصادر التعريف:
//        indsin   <Effect#name duration=600 …><Keyframe at=50% y=-8 /></Effect>   |   @effect=name … .end/effect
//        Rin      indsin.defineEffect("name", "0%{…} 100%{…}", {duration: 600})
//        C API    rin_indsin_effect_define(name, spec, options)
//   5) طرق التشغيل:
//        دخول        effect="fadeUp"  (قائمة: effect="fadeUp glow"، أو تعريف مضمَّن effect="from{opacity:0} to{opacity:1}")
//        إيماءات      effect_tap= / effect_hover= / effect_longpress= / effect_doubletap=
//        تفاعلي       effect_on_change="pulse" (+ effect_key={expr}) — يُشغَّل كلما تغيّرت القيمة المراقَبة
//        من Rin       indsin.playEffect("name", "shake") · onTap=effect(card, "shake")
//        تتابع        stagger=80 على الحاوية: أبناؤها المباشرون يبدؤون تأثيرهم تباعاً
//
// الملف header-only ولا يعتمد على الرسم؛ تطبيق الإطارات على DrawList في rin_indsin_effects_paint.h.
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h" // resolveDurationToken / resolveSemanticColor / parseHexColor
#include "../rin_ast.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace indsin {
namespace fx {

constexpr double kPi = 3.14159265358979323846;

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
// يُسقط '-' و'_' والفراغ ويحوّل لأحرف صغيرة: "ease-in-out" == "easeInOut" == "ease_in_out"
inline std::string squash(const std::string& s) {
    std::string o; for (char c : s) if (c != '-' && c != '_' && c != ' ') o += (char)std::tolower((unsigned char)c); return o;
}
// يقرأ رقماً كاملاً (يرفض البقايا): "12", "-6.5", ".5"
inline bool parseNumber(const std::string& in, double& out) {
    std::string s = trim(in);
    if (s.empty()) return false;
    char* end = nullptr; double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = v; return true;
}
inline std::string jsonStr(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; }
    return o;
}

// ================================================================================================
// 1) Easing
// ================================================================================================
// الأسماء القديمة بقيت بنفس القيم؛ المضاف: ease (CSS)، expo/circ، cubic-bezier(a,b,c,d)، steps(n).
enum class Easing { LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT,
                    CUBIC_IN, CUBIC_OUT, CUBIC_IN_OUT, SINE_IN_OUT, BACK_OUT, BOUNCE_OUT, ELASTIC_OUT, SPRING,
                    EXPO_OUT, EXPO_IN_OUT, CIRC_OUT, BEZIER, STEPS };

struct EasingSpec {
    Easing kind = Easing::EASE_OUT;
    double p[4] = {0, 0, 1, 1}; // BEZIER: x1,y1,x2,y2
    int steps = 1;              // STEPS (jump-end)
};

inline double bezierAt(double a, double b, double c, double d, double x) { // CSS cubic-bezier(a,b,c,d) عند تقدّم x
    auto bx = [&](double t) { double u = 1 - t; return 3 * u * u * t * a + 3 * u * t * t * c + t * t * t; };
    auto by = [&](double t) { double u = 1 - t; return 3 * u * u * t * b + 3 * u * t * t * d + t * t * t; };
    auto dbx = [&](double t) { double u = 1 - t; return 3 * u * u * a + 6 * u * t * (c - a) + 3 * t * t * (1 - c); };
    double t = x;
    for (int i = 0; i < 8; i++) { // Newton
        double e = bx(t) - x, de = dbx(t);
        if (std::fabs(e) < 1e-6) return by(t);
        if (std::fabs(de) < 1e-6) break;
        t -= e / de;
    }
    double lo = 0, hi = 1; t = x; // bisection fallback
    for (int i = 0; i < 24; i++) { double e = bx(t); if (std::fabs(e - x) < 1e-6) break; if (e < x) lo = t; else hi = t; t = (lo + hi) / 2; }
    return by(t);
}

inline double applyEasing(const EasingSpec& e, double t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    switch (e.kind) {
        case Easing::LINEAR: return t;
        case Easing::EASE_IN: return t * t;
        case Easing::EASE_OUT: return 1.0 - (1.0 - t) * (1.0 - t);
        case Easing::EASE_IN_OUT: return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2) / 2.0;
        case Easing::CUBIC_IN: return t * t * t;
        case Easing::CUBIC_OUT: return 1.0 - std::pow(1.0 - t, 3);
        case Easing::CUBIC_IN_OUT: return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3) / 2.0;
        case Easing::SINE_IN_OUT: return -(std::cos(kPi * t) - 1.0) / 2.0;
        case Easing::BACK_OUT: { const double c1 = 1.70158, c3 = c1 + 1.0; return 1.0 + c3 * std::pow(t - 1.0, 3) + c1 * std::pow(t - 1.0, 2); }
        case Easing::BOUNCE_OUT: {
            const double n1 = 7.5625, d1 = 2.75;
            if (t < 1.0 / d1) return n1 * t * t;
            if (t < 2.0 / d1) { t -= 1.5 / d1; return n1 * t * t + 0.75; }
            if (t < 2.5 / d1) { t -= 2.25 / d1; return n1 * t * t + 0.9375; }
            t -= 2.625 / d1; return n1 * t * t + 0.984375;
        }
        case Easing::ELASTIC_OUT: {
            if (t <= 0.0) return 0.0;
            if (t >= 1.0) return 1.0;
            const double c4 = (2.0 * kPi) / 3.0;
            return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
        }
        case Easing::SPRING: { if (t >= 1.0) return 1.0; return 1.0 - std::exp(-6.0 * t) * std::cos(10.0 * t); }
        case Easing::EXPO_OUT: return t >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
        case Easing::EXPO_IN_OUT:
            if (t <= 0.0) return 0.0;
            if (t >= 1.0) return 1.0;
            return t < 0.5 ? std::pow(2.0, 20.0 * t - 10.0) / 2.0 : (2.0 - std::pow(2.0, -20.0 * t + 10.0)) / 2.0;
        case Easing::CIRC_OUT: return std::sqrt(1.0 - std::pow(t - 1.0, 2));
        case Easing::BEZIER: return bezierAt(e.p[0], e.p[1], e.p[2], e.p[3], t);
        case Easing::STEPS: { int n = e.steps < 1 ? 1 : e.steps; return std::floor(t * n) / n; }
    }
    return t;
}

// "easeOut" · "ease-in-out" · "linear" · "spring" · "cubic-bezier(.4,0,.2,1)" · "steps(4)" · …
inline EasingSpec parseEasing(const std::string& in, bool* ok = nullptr) {
    EasingSpec e; if (ok) *ok = true;
    std::string s = trim(in), q = squash(s);
    auto args = [&](const char* fn, std::vector<double>& v) {
        size_t n = std::string(fn).size();
        if (q.compare(0, n, fn) != 0) return false;
        size_t lp = s.find('('), rp = s.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp < lp) return false;
        std::string body = s.substr(lp + 1, rp - lp - 1), cur; v.clear();
        for (char c : body + ",") { if (c == ',') { double d; if (!parseNumber(cur, d)) return false; v.push_back(d); cur.clear(); } else cur += c; }
        return true;
    };
    std::vector<double> v;
    if (args("cubicbezier", v) && v.size() == 4) { e.kind = Easing::BEZIER; for (int i = 0; i < 4; i++) e.p[i] = v[i]; return e; }
    if (args("steps", v) && v.size() >= 1) { e.kind = Easing::STEPS; e.steps = (int)v[0]; return e; }
    if (q == "linear") e.kind = Easing::LINEAR;
    else if (q == "easein" || q == "quadin") e.kind = Easing::EASE_IN;
    else if (q == "easeout" || q == "quadout" || q.empty()) e.kind = Easing::EASE_OUT;
    else if (q == "easeinout" || q == "quadinout") e.kind = Easing::EASE_IN_OUT;
    else if (q == "cubicin") e.kind = Easing::CUBIC_IN;
    else if (q == "cubicout") e.kind = Easing::CUBIC_OUT;
    else if (q == "cubicinout") e.kind = Easing::CUBIC_IN_OUT;
    else if (q == "sine" || q == "sineinout") e.kind = Easing::SINE_IN_OUT;
    else if (q == "back" || q == "backout") e.kind = Easing::BACK_OUT;
    else if (q == "bounce" || q == "bounceout") e.kind = Easing::BOUNCE_OUT;
    else if (q == "elastic" || q == "elasticout") e.kind = Easing::ELASTIC_OUT;
    else if (q == "spring") e.kind = Easing::SPRING;
    else if (q == "expo" || q == "expoout") e.kind = Easing::EXPO_OUT;
    else if (q == "expoinout") e.kind = Easing::EXPO_IN_OUT;
    else if (q == "circ" || q == "circout") e.kind = Easing::CIRC_OUT;
    else if (q == "ease") { e.kind = Easing::BEZIER; e.p[0] = .25; e.p[1] = .1; e.p[2] = .25; e.p[3] = 1; } // CSS ease
    else if (ok) *ok = false;
    return e;
}

// ================================================================================================
// 2) معادلات التأثير (x_fn="sin(t*pi*6)*8") — محلّل تعابير صغير وآمن (لا حلقات ولا استدعاءات خارجية)
// ================================================================================================
struct FxEnv { double t = 0, n = 0, w = 0, h = 0; }; // t: تقدّم الدورة 0..1 · n: رقم الدورة · w/h: حجم العنصر px
struct FxNode;
using FxNodePtr = std::shared_ptr<const FxNode>;
struct FxNode {
    enum Kind { NUM, VAR, NEG, BIN, CALL } kind = NUM;
    double num = 0; char op = 0; int var = 0; int fn = 0;
    std::vector<FxNodePtr> args;
};

namespace fxfn {
enum { SIN = 1, COS, TAN, ABS, SQRT, FLOOR, CEIL, ROUND, EXP, LOG, SIGN, FRACT, TRI, NOISE,
       POW, MIN, MAX, MOD, STEP, ATAN2, CLAMP, MIX, SMOOTH };
inline int arity(int f) { return f <= NOISE ? 1 : (f <= ATAN2 ? 2 : 3); }
inline int lookup(const std::string& n) {
    static const std::map<std::string, int> m = {
        {"sin", SIN}, {"cos", COS}, {"tan", TAN}, {"abs", ABS}, {"sqrt", SQRT}, {"floor", FLOOR}, {"ceil", CEIL},
        {"round", ROUND}, {"exp", EXP}, {"log", LOG}, {"sign", SIGN}, {"fract", FRACT}, {"tri", TRI}, {"noise", NOISE},
        {"pow", POW}, {"min", MIN}, {"max", MAX}, {"mod", MOD}, {"step", STEP}, {"atan2", ATAN2},
        {"clamp", CLAMP}, {"mix", MIX}, {"smooth", SMOOTH}};
    auto it = m.find(n); return it == m.end() ? 0 : it->second;
}
} // namespace fxfn

class FxParser {
public:
    explicit FxParser(const std::string& s) : s_(s) {}
    FxNodePtr parse(std::string& err) {
        FxNodePtr n = expr();
        skip();
        if (err_.empty() && i_ < s_.size()) err_ = "unexpected '" + std::string(1, s_[i_]) + "'";
        if (!err_.empty()) { err = err_; return nullptr; }
        return n;
    }
private:
    std::string s_; size_t i_ = 0; std::string err_;
    void skip() { while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) i_++; }
    bool eat(char c) { skip(); if (i_ < s_.size() && s_[i_] == c) { i_++; return true; } return false; }
    static FxNodePtr mk(FxNode n) { return std::make_shared<const FxNode>(std::move(n)); }
    static FxNodePtr num(double v) { FxNode n; n.kind = FxNode::NUM; n.num = v; return mk(n); }
    FxNodePtr bin(char op, FxNodePtr a, FxNodePtr b) { FxNode n; n.kind = FxNode::BIN; n.op = op; n.args = {a, b}; return mk(n); }
    FxNodePtr expr() {
        FxNodePtr l = term();
        for (;;) { if (eat('+')) l = bin('+', l, term()); else if (eat('-')) l = bin('-', l, term()); else break; }
        return l;
    }
    FxNodePtr term() {
        FxNodePtr l = unary();
        for (;;) {
            if (eat('*')) l = bin('*', l, unary());
            else if (eat('/')) l = bin('/', l, unary());
            else if (eat('%')) l = bin('%', l, unary());
            else break;
        }
        return l;
    }
    FxNodePtr unary() {
        if (eat('-')) { FxNode n; n.kind = FxNode::NEG; n.args = {unary()}; return mk(n); }
        if (eat('+')) return unary();
        return power();
    }
    FxNodePtr power() { FxNodePtr b = atom(); if (eat('^')) return bin('^', b, unary()); return b; }
    FxNodePtr atom() {
        skip();
        if (!err_.empty()) return num(0);
        if (i_ >= s_.size()) { err_ = "unexpected end of expression"; return num(0); }
        char c = s_[i_];
        if (c == '(') { i_++; FxNodePtr e = expr(); if (!eat(')') && err_.empty()) err_ = "missing ')'"; return e; }
        if (std::isdigit((unsigned char)c) || c == '.') {
            char* end = nullptr; double v = std::strtod(s_.c_str() + i_, &end);
            i_ = (size_t)(end - s_.c_str()); return num(v);
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t b = i_; while (i_ < s_.size() && (std::isalnum((unsigned char)s_[i_]) || s_[i_] == '_')) i_++;
            std::string id = lower(s_.substr(b, i_ - b));
            skip();
            if (i_ < s_.size() && s_[i_] == '(') {
                i_++;
                int f = fxfn::lookup(id);
                if (!f) { err_ = "unknown function '" + id + "'"; return num(0); }
                FxNode n; n.kind = FxNode::CALL; n.fn = f;
                if (!eat(')')) {
                    for (;;) { n.args.push_back(expr()); if (!err_.empty()) break; if (eat(',')) continue; if (eat(')')) break; err_ = "missing ')' in " + id + "()"; break; }
                }
                if (err_.empty() && (int)n.args.size() != fxfn::arity(f)) err_ = id + "() expects " + std::to_string(fxfn::arity(f)) + " argument(s)";
                return mk(n);
            }
            if (id == "pi") return num(kPi);
            if (id == "e") return num(2.718281828459045);
            FxNode n; n.kind = FxNode::VAR;
            if (id == "t" || id == "p") n.var = 0; else if (id == "n") n.var = 1; else if (id == "w") n.var = 2; else if (id == "h") n.var = 3;
            else { err_ = "unknown name '" + id + "' (use t, n, w, h, pi)"; return num(0); }
            return mk(n);
        }
        err_ = std::string("unexpected '") + c + "'"; return num(0);
    }
};

inline double fxHash(double x) { double s = std::sin(x * 127.1 + 311.7) * 43758.5453; return s - std::floor(s); }
inline double evalFx(const FxNode& n, const FxEnv& e) {
    switch (n.kind) {
        case FxNode::NUM: return n.num;
        case FxNode::VAR: return n.var == 0 ? e.t : (n.var == 1 ? e.n : (n.var == 2 ? e.w : e.h));
        case FxNode::NEG: return -evalFx(*n.args[0], e);
        case FxNode::BIN: {
            double a = evalFx(*n.args[0], e), b = evalFx(*n.args[1], e);
            switch (n.op) {
                case '+': return a + b; case '-': return a - b; case '*': return a * b;
                case '/': return b == 0 ? 0 : a / b;
                case '%': return b == 0 ? 0 : std::fmod(a, b);
                case '^': return std::pow(a, b);
            }
            return 0;
        }
        case FxNode::CALL: {
            double a = evalFx(*n.args[0], e);
            double b = n.args.size() > 1 ? evalFx(*n.args[1], e) : 0, c = n.args.size() > 2 ? evalFx(*n.args[2], e) : 0;
            switch (n.fn) {
                case fxfn::SIN: return std::sin(a); case fxfn::COS: return std::cos(a); case fxfn::TAN: return std::tan(a);
                case fxfn::ABS: return std::fabs(a); case fxfn::SQRT: return a < 0 ? 0 : std::sqrt(a);
                case fxfn::FLOOR: return std::floor(a); case fxfn::CEIL: return std::ceil(a); case fxfn::ROUND: return std::round(a);
                case fxfn::EXP: return std::exp(a); case fxfn::LOG: return a <= 0 ? 0 : std::log(a);
                case fxfn::SIGN: return a > 0 ? 1 : (a < 0 ? -1 : 0);
                case fxfn::FRACT: return a - std::floor(a);
                case fxfn::TRI: { double f = a - std::floor(a); return 1.0 - 4.0 * std::fabs(f - 0.5); } // موجة مثلثية -1..1 بدورة 1 (تبدأ من -1)
                case fxfn::NOISE: { double i = std::floor(a), f = a - i, u = f * f * (3 - 2 * f); return (fxHash(i) * (1 - u) + fxHash(i + 1) * u) * 2.0 - 1.0; }
                case fxfn::POW: return std::pow(a, b); case fxfn::MIN: return std::min(a, b); case fxfn::MAX: return std::max(a, b);
                case fxfn::MOD: return b == 0 ? 0 : std::fmod(a, b);
                case fxfn::STEP: return b < a ? 0 : 1;
                case fxfn::ATAN2: return std::atan2(a, b);
                case fxfn::CLAMP: return std::max(b, std::min(c, a));
                case fxfn::MIX: return a + (b - a) * c;
                case fxfn::SMOOTH: { if (b == a) return 0; double u = std::max(0.0, std::min(1.0, (c - a) / (b - a))); return u * u * (3 - 2 * u); }
            }
            return 0;
        }
    }
    return 0;
}

// ================================================================================================
// 3) تعريف التأثير: قنوات + keyframes + معادلات + توقيت
// ================================================================================================
enum Ch { CH_OPACITY = 0, CH_X, CH_XR, CH_Y, CH_YR, CH_SX, CH_SY, CH_GLOW, NCH };
enum ColCh { COL_BG = 0, COL_FG, COL_GLOW, NCOL };
inline double channelIdentity(int ch) { return (ch == CH_OPACITY || ch == CH_SX || ch == CH_SY) ? 1.0 : 0.0; }
inline const char* channelName(int ch) {
    static const char* n[] = {"opacity", "x", "x%", "y", "y%", "scaleX", "scaleY", "glow"}; return n[ch];
}

struct Keyframe {
    double t = 0;
    double v[NCH] = {0}; bool has[NCH] = {false};
    std::string col[NCOL]; bool hasCol[NCOL] = {false}; // لون حرفي (#hex) أو دور ثيم (primary…) — يُحلّ وقت التقييم
    bool hasEase = false; EasingSpec ease;             // easing من هذا الإطار إلى التالي
};

enum class Direction { NORMAL, REVERSE, ALTERNATE, ALTERNATE_REVERSE };
enum Fill { FILL_NONE = 0, FILL_BACKWARDS = 1, FILL_FORWARDS = 2, FILL_BOTH = 3 };

struct EffectDef {
    std::string name;
    std::vector<Keyframe> frames; // مرتّبة بـ t
    FxNodePtr fn[NCH];            // معادلة تحلّ محلّ keyframes القناة
    std::string fnSrc[NCH];
    double durationMs = 250, delayMs = 0;
    double iterations = 1;        // < 0 => لا نهائي
    Direction dir = Direction::NORMAL;
    int fill = -1;                // -1 => غير محدّد (الدخول: both، الإيماءات: none)
    EasingSpec easing;            // easeOut افتراضياً
    bool builtin = false;
    bool usesChannel(int ch) const {
        if (fn[ch]) return true;
        for (auto& k : frames) if (k.has[ch]) return true;
        return false;
    }
    bool usesColor(int c) const { for (auto& k : frames) if (k.hasCol[c]) return true; return false; }
};
using EffectDefPtr = std::shared_ptr<EffectDef>;

// ---- قراءة قيم النصوص ------------------------------------------------------------------------
inline bool parseDurationMs(const std::string& in, double& out) {
    std::string s = trim(in);
    if (s.empty()) return false;
    if (resolveDurationToken(s, out)) return true;
    std::string q = lower(s); double mult = 1;
    if (q.size() > 2 && q.compare(q.size() - 2, 2, "ms") == 0) q.resize(q.size() - 2);
    else if (q.size() > 1 && q.back() == 's') { q.pop_back(); mult = 1000; }
    double v; if (!parseNumber(q, v)) return false;
    out = v * mult; return true;
}
inline bool parseIterations(const std::string& in, double& out) {
    std::string q = squash(in);
    if (q == "infinite" || q == "inf" || q == "loop" || q == "forever") { out = -1; return true; }
    double v; if (!parseNumber(in, v) || v < 0) return false;
    out = v; return true;
}
inline bool parseDirection(const std::string& in, Direction& out) {
    std::string q = squash(in);
    if (q == "normal" || q.empty()) out = Direction::NORMAL;
    else if (q == "reverse") out = Direction::REVERSE;
    else if (q == "alternate" || q == "yoyo" || q == "pingpong") out = Direction::ALTERNATE;
    else if (q == "alternatereverse") out = Direction::ALTERNATE_REVERSE;
    else return false;
    return true;
}
inline bool parseFill(const std::string& in, int& out) {
    std::string q = squash(in);
    if (q == "none") out = FILL_NONE; else if (q == "forwards" || q == "forward") out = FILL_FORWARDS;
    else if (q == "backwards" || q == "backward") out = FILL_BACKWARDS; else if (q == "both") out = FILL_BOTH;
    else return false;
    return true;
}

// ---- keyframes: «from{opacity:0} 50%{y:-8} to{opacity:1}» -------------------------------------
inline Keyframe& frameAt(EffectDef& d, double t) {
    for (auto& k : d.frames) if (std::fabs(k.t - t) < 1e-9) return k;
    Keyframe k; k.t = t;
    auto it = std::upper_bound(d.frames.begin(), d.frames.end(), t, [](double v, const Keyframe& a) { return v < a.t; });
    return *d.frames.insert(it, k);
}
inline bool parseLength(const std::string& in, double& v, bool& pct) { // "12" "12px" "-6" "50%"
    std::string s = trim(in); pct = false;
    if (!s.empty() && s.back() == '%') { pct = true; s.pop_back(); }
    else if (s.size() > 2 && lower(s.substr(s.size() - 2)) == "px") s.resize(s.size() - 2);
    return parseNumber(s, v);
}
inline bool setFrameProp(Keyframe& k, const std::string& keyIn, const std::string& valIn, std::string& err) {
    std::string key = squash(keyIn), val = trim(valIn);
    auto num = [&](int ch) {
        double v; bool pct;
        if (!parseLength(val, v, pct)) { err = "bad value '" + val + "' for " + keyIn; return false; }
        k.v[ch] = pct ? v / 100.0 : v; k.has[ch] = true; return true;
    };
    auto pos = [&](int chPx, int chPct) {
        double v; bool pct;
        if (!parseLength(val, v, pct)) { err = "bad value '" + val + "' for " + keyIn; return false; }
        if (pct) { k.v[chPct] = v / 100.0; k.has[chPct] = true; } else { k.v[chPx] = v; k.has[chPx] = true; }
        return true;
    };
    if (key == "opacity" || key == "alpha") return num(CH_OPACITY);
    if (key == "x" || key == "tx" || key == "translatex") return pos(CH_X, CH_XR);
    if (key == "y" || key == "ty" || key == "translatey") return pos(CH_Y, CH_YR);
    if (key == "scale") {
        double v; bool pct; if (!parseLength(val, v, pct)) { err = "bad value '" + val + "' for scale"; return false; }
        if (pct) v /= 100.0;
        k.v[CH_SX] = k.v[CH_SY] = v; k.has[CH_SX] = k.has[CH_SY] = true; return true;
    }
    if (key == "scalex" || key == "sx") return num(CH_SX);
    if (key == "scaley" || key == "sy") return num(CH_SY);
    if (key == "glow" || key == "shadow") return num(CH_GLOW);
    auto col = [&](int c) { if (val.empty()) { err = "empty color for " + keyIn; return false; } k.col[c] = val; k.hasCol[c] = true; return true; };
    if (key == "bg" || key == "background" || key == "fill") return col(COL_BG);
    if (key == "color" || key == "fg" || key == "textcolor") return col(COL_FG);
    if (key == "glowcolor") return col(COL_GLOW);
    if (key == "easing" || key == "ease") { bool ok; k.ease = parseEasing(val, &ok); if (!ok) { err = "unknown easing '" + val + "'"; return false; } k.hasEase = true; return true; }
    err = "unknown property '" + keyIn + "' (opacity, x, y, scale, scaleX, scaleY, glow, glowColor, bg, color, easing)";
    return false;
}
inline bool parseSelector(const std::string& tok, double& t) {
    std::string s = lower(trim(tok));
    if (s == "from") { t = 0; return true; }
    if (s == "to") { t = 1; return true; }
    double v; bool pct;
    if (!parseLength(s, v, pct)) return false;
    t = v / 100.0; // الرقم المجرّد يُقرأ نسبة مئوية أيضاً (at=50 == 50%)
    return t >= -1e-9 && t <= 1 + 1e-9;
}
// "decls" = «opacity:0, y:24px» (فاصل: , أو ; خارج الأقواس)
inline bool applyDecls(Keyframe& k, const std::string& body, std::string& err) {
    std::string cur; int depth = 0;
    auto flush = [&]() -> bool {
        std::string d = trim(cur); cur.clear();
        if (d.empty()) return true;
        size_t c = d.find(':');
        if (c == std::string::npos) { err = "expected 'property:value' but found '" + d + "'"; return false; }
        return setFrameProp(k, trim(d.substr(0, c)), d.substr(c + 1), err);
    };
    for (char ch : body) {
        if (ch == '(') depth++; else if (ch == ')') depth--;
        if ((ch == ',' || ch == ';') && depth == 0) { if (!flush()) return false; } else cur += ch;
    }
    return flush();
}
inline bool parseFramesInto(const std::string& spec, EffectDef& d, std::string& err) {
    size_t i = 0, n = spec.size();
    while (i < n) {
        while (i < n && (std::isspace((unsigned char)spec[i]) || spec[i] == ',' || spec[i] == ';')) i++;
        if (i >= n) break;
        size_t ob = spec.find('{', i);
        if (ob == std::string::npos) { err = "expected '{' after keyframe selector near '" + spec.substr(i, 16) + "'"; return false; }
        std::string sel = spec.substr(i, ob - i);
        int depth = 0; size_t j = ob + 1;
        for (; j < n; j++) { if (spec[j] == '(') depth++; else if (spec[j] == ')') depth--; else if (spec[j] == '}' && depth <= 0) break; }
        if (j >= n) { err = "missing '}' in keyframes"; return false; }
        std::string body = spec.substr(ob + 1, j - ob - 1);
        std::vector<double> ts; std::string tok;
        for (char c : sel + " ") {
            if (std::isspace((unsigned char)c) || c == ',') {
                if (!tok.empty()) { double t; if (!parseSelector(tok, t)) { err = "bad keyframe selector '" + tok + "' (from, to, 0%..100%)"; return false; } ts.push_back(t); tok.clear(); }
            } else tok += c;
        }
        if (ts.empty()) { err = "keyframe block without selector"; return false; }
        for (double t : ts) if (!applyDecls(frameAt(d, std::max(0.0, std::min(1.0, t))), body, err)) return false;
        i = j + 1;
    }
    return true;
}

inline bool setFormula(EffectDef& d, int ch, const std::string& src, std::string& err) {
    std::string e; FxNodePtr p = FxParser(src).parse(e);
    if (!p) { err = std::string(channelName(ch)) + "_fn: " + e; return false; }
    d.fn[ch] = p; d.fnSrc[ch] = src; return true;
}

using KV = std::vector<std::pair<std::string, std::string>>;

// يطبّق خياراً واحداً (duration/easing/…/frames/from/to/atNN/x_fn…) على تعريف.
inline bool applyEffectOption(EffectDef& d, const std::string& keyIn, const std::string& val, std::string& err) {
    std::string key = squash(keyIn);
    if (key == "duration" || key == "dur") { double v; if (!parseDurationMs(val, v) || v < 0) { err = "bad duration '" + val + "'"; return false; } d.durationMs = v; return true; }
    if (key == "delay") { double v; if (!parseDurationMs(val, v)) { err = "bad delay '" + val + "'"; return false; } d.delayMs = v; return true; }
    if (key == "iterations" || key == "iteration" || key == "repeat") { double v; if (!parseIterations(val, v)) { err = "bad iterations '" + val + "' (a number or infinite)"; return false; } d.iterations = v; return true; }
    if (key == "direction") { if (!parseDirection(val, d.dir)) { err = "bad direction '" + val + "' (normal, reverse, alternate, alternate-reverse)"; return false; } return true; }
    if (key == "fill") { if (!parseFill(val, d.fill)) { err = "bad fill '" + val + "' (none, forwards, backwards, both)"; return false; } return true; }
    if (key == "easing" || key == "ease") { bool ok; d.easing = parseEasing(val, &ok); if (!ok) { err = "unknown easing '" + val + "'"; return false; } return true; }
    if (key == "loop") { d.iterations = (lower(trim(val)) == "false" ? 1 : -1); return true; }
    if (key == "frames" || key == "keyframes") return parseFramesInto(val, d, err);
    if (key == "from" || key == "to") return parseFramesInto(key + "{" + val + "}", d, err);
    {   // at50 · at12_5 (من @effect و<Keyframe at=…>): الأرقام بعد "at"، و'_' = فاصلة عشرية
        std::string raw = lower(trim(keyIn));
        if (raw.size() > 2 && raw.compare(0, 2, "at") == 0 && std::isdigit((unsigned char)raw[2])) {
            std::string num = raw.substr(2); for (auto& c : num) if (c == '_') c = '.';
            return parseFramesInto(num + "%{" + val + "}", d, err);
        }
    }
    // معادلات: x_fn · y_fn · opacity_fn · scale_fn · scaleX_fn · scaleY_fn · glow_fn
    if (key.size() > 2 && key.compare(key.size() - 2, 2, "fn") == 0) {
        std::string ch = key.substr(0, key.size() - 2);
        if (ch == "opacity" || ch == "alpha") return setFormula(d, CH_OPACITY, val, err);
        if (ch == "x" || ch == "translatex") return setFormula(d, CH_X, val, err);
        if (ch == "y" || ch == "translatey") return setFormula(d, CH_Y, val, err);
        if (ch == "scale") return setFormula(d, CH_SX, val, err) && setFormula(d, CH_SY, val, err);
        if (ch == "scalex" || ch == "sx") return setFormula(d, CH_SX, val, err);
        if (ch == "scaley" || ch == "sy") return setFormula(d, CH_SY, val, err);
        if (ch == "glow") return setFormula(d, CH_GLOW, val, err);
    }
    err = "unknown effect option '" + keyIn + "'";
    return false;
}

class EffectLibrary;
inline EffectLibrary& effectLibrary();
// يبني تعريفاً من أزواج (مفتاح، قيمة): `base` أولاً (نسخة من تعريف موجود)، ثم باقي الخيارات بالترتيب.
inline bool buildEffect(const std::string& name, const KV& kv, EffectDef& out, std::string& err);

// ================================================================================================
// 4) مكتبة التأثيرات (عامة للعملية، مثل themeRegistry) + طابور أوامر التشغيل القادمة من Rin
// ================================================================================================
struct PlayRequest {
    std::string target;  // اسم Strand (يطابق كل العناصر بهذا الاسم)
    std::string effect;  // قائمة تأثيرات أو تعريف مضمَّن؛ فارغ مع stop => أوقف الكل
    KV opts;             // duration/delay/easing/iterations/direction/fill
    bool stop = false;
};

class EffectLibrary {
public:
    EffectLibrary();
    EffectDefPtr find(const std::string& name) const {
        auto it = defs_.find(squash(name));
        return it == defs_.end() ? nullptr : it->second;
    }
    bool has(const std::string& name) const { return find(name) != nullptr; }
    bool define(const std::string& name, const KV& kv, std::string& err) {
        if (trim(name).empty()) { err = "effect name is empty"; return false; }
        EffectDef d;
        if (!buildEffect(name, kv, d, err)) return false;
        d.builtin = false;
        defs_[squash(name)] = std::make_shared<EffectDef>(std::move(d));
        inline_.clear(); // التعريفات المضمَّنة لا تعتمد على الأسماء، لكن نبقيها نظيفة
        return true;
    }
    bool remove(const std::string& name) { return defs_.erase(squash(name)) > 0; }
    std::vector<std::string> names() const {
        std::vector<std::string> v; for (auto& kv : defs_) v.push_back(kv.second->name); std::sort(v.begin(), v.end()); return v;
    }
    // قائمة تأثيرات: "fadeUp glow" · "fadeUp,glow" · "fadeUp+glow" أو تعريف مضمَّن (فيه '{'): «from{opacity:0} to{opacity:1}»
    std::vector<EffectDefPtr> resolveList(const std::string& spec) {
        std::vector<EffectDefPtr> out; std::string s = trim(spec);
        if (s.empty()) return out;
        if (s.find('{') != std::string::npos) {
            auto it = inline_.find(s);
            if (it == inline_.end()) {
                EffectDef d; std::string err; d.name = "inline";
                EffectDefPtr p;
                if (parseFramesInto(s, d, err)) p = std::make_shared<EffectDef>(std::move(d));
                it = inline_.emplace(s, p).first; // null يُخزَّن أيضاً كي لا نعيد التحليل
            }
            if (it->second) out.push_back(it->second);
            return out;
        }
        std::string tok;
        for (char c : s + " ") {
            if (std::isspace((unsigned char)c) || c == ',' || c == '+') {
                if (!tok.empty()) { if (auto d = find(tok)) out.push_back(d); tok.clear(); }
            } else tok += c;
        }
        return out;
    }
    // ---- طابور الأوامر (تملؤه دوال Rin وأفعال onTap؛ تستهلكه الجلسة في relayout) ----
    void queue(PlayRequest r) { pending_.push_back(std::move(r)); }
    bool hasPending() const { return !pending_.empty(); }
    std::vector<PlayRequest> takePending() { std::vector<PlayRequest> v; v.swap(pending_); return v; }
    std::string lastError; // آخر خطأ في تعريف @effect/<Effect> (لا يوقف العرض)
private:
    std::unordered_map<std::string, EffectDefPtr> defs_;
    std::unordered_map<std::string, EffectDefPtr> inline_;
    std::vector<PlayRequest> pending_;
};
inline EffectLibrary& effectLibrary() { static EffectLibrary lib; return lib; }

inline bool buildEffect(const std::string& name, const KV& kv, EffectDef& out, std::string& err) {
    EffectDef d; d.name = name;
    for (auto& p : kv) if (squash(p.first) == "base") {
        auto b = effectLibrary().find(trim(p.second));
        if (!b) { err = "effect '" + name + "': base effect '" + p.second + "' is not defined"; return false; }
        d = *b; d.name = name; d.builtin = false;
    }
    for (auto& p : kv) {
        if (squash(p.first) == "base") continue;
        std::string e;
        if (!applyEffectOption(d, p.first, p.second, e)) { err = "effect '" + name + "': " + e; return false; }
    }
    if (d.frames.empty() && std::none_of(std::begin(d.fn), std::end(d.fn), [](const FxNodePtr& f) { return (bool)f; })) {
        err = "effect '" + name + "' has no keyframes and no formulas"; return false;
    }
    out = std::move(d);
    return true;
}

// المدمجات معرَّفة بنفس لغة التعريف (تستعمل نفس المحلّل — أي أن أي مستخدم يستطيع كتابة مثلها تماماً).
inline EffectLibrary::EffectLibrary() {
    auto add = [&](const std::string& name, const KV& kv) {
        std::string err; EffectDef d;
        if (!buildEffect(name, kv, d, err)) { lastError = "builtin " + name + ": " + err; return; }
        d.builtin = true; d.name = name;
        defs_[squash(name)] = std::make_shared<EffectDef>(std::move(d));
    };
    auto kf = [&](const char* name, const char* frames, const char* dur, const char* ease, KV extra = {}) {
        KV kv = {{"frames", frames}, {"duration", dur}, {"easing", ease}};
        for (auto& e : extra) kv.push_back(e);
        add(name, kv);
    };
    auto alias = [&](const char* name, const char* of) {
        auto it = defs_.find(squash(of)); if (it == defs_.end()) return;
        auto d = std::make_shared<EffectDef>(*it->second); d->name = name; defs_[squash(name)] = d;
    };
    // ---- دخول (نفس منحنيات الإصدار القديم) ----
    kf("fade", "from{opacity:0} to{opacity:1}", "250", "easeOut");
    kf("scale", "from{opacity:0,scale:.85} to{opacity:1,scale:1}", "250", "easeOut"); alias("zoom", "scale");
    kf("slideUp", "from{opacity:0,y:24} to{opacity:1,y:0}", "250", "easeOut");
    kf("slideDown", "from{opacity:0,y:-24} to{opacity:1,y:0}", "250", "easeOut");
    kf("slideLeft", "from{opacity:0,x:24} to{opacity:1,x:0}", "250", "easeOut");
    kf("slideRight", "from{opacity:0,x:-24} to{opacity:1,x:0}", "250", "easeOut");
    kf("grow", "from{opacity:0,scale:.01} to{opacity:1,scale:1}", "250", "easeOut"); alias("pop", "grow");
    // ---- دخول جديدة ----
    kf("fadeUp", "from{opacity:0,y:40} to{opacity:1,y:0}", "520", "cubicOut");
    kf("fadeDown", "from{opacity:0,y:-40} to{opacity:1,y:0}", "520", "cubicOut");
    kf("fadeLeft", "from{opacity:0,x:40} to{opacity:1,x:0}", "520", "cubicOut");
    kf("fadeRight", "from{opacity:0,x:-40} to{opacity:1,x:0}", "520", "cubicOut");
    kf("zoomIn", "from{opacity:0,scale:.3} to{opacity:1,scale:1}", "450", "backOut");
    kf("zoomOut", "from{opacity:0,scale:1.4} to{opacity:1,scale:1}", "400", "cubicOut");
    kf("slideInFull", "from{x:-100%} to{x:0}", "420", "cubicOut");
    kf("rise", "from{opacity:0,y:100%} to{opacity:1,y:0}", "480", "expoOut");
    kf("drop", "0%{opacity:0,y:-60} 60%{opacity:1,y:8} 80%{y:-3} 100%{y:0}", "620", "easeOut");
    kf("reveal", "from{opacity:0,scaleX:.6} to{opacity:1,scaleX:1}", "420", "cubicOut");
    // ---- انتباه/إيماءات ----
    add("pulse", {{"scale_fn", "1+0.08*sin(pi*t)"}, {"duration", "250"}, {"easing", "linear"}});
    add("shake", {{"x_fn", "sin(t*pi*6)*(1-t)*8"}, {"duration", "250"}, {"easing", "linear"}});
    add("bounce", {{"y_fn", "-abs(sin(t*pi*3))*(1-t)*16"}, {"duration", "250"}, {"easing", "linear"}});
    kf("flash", "0%{opacity:1} 25%{opacity:0} 50%{opacity:1} 75%{opacity:0} 100%{opacity:1}", "800", "linear");
    kf("heartbeat", "0%{scale:1} 14%{scale:1.3} 28%{scale:1} 42%{scale:1.3} 70%{scale:1}", "1100", "easeInOut");
    kf("rubber", "0%{scaleX:1,scaleY:1} 30%{scaleX:1.25,scaleY:.75} 40%{scaleX:.75,scaleY:1.25} 50%{scaleX:1.15,scaleY:.85} 65%{scaleX:.95,scaleY:1.05} 75%{scaleX:1.05,scaleY:.95} 100%{scaleX:1,scaleY:1}", "700", "linear");
    add("jelly", {{"scaleX_fn", "1+0.2*sin(t*pi*4)*(1-t)"}, {"scaleY_fn", "1-0.2*sin(t*pi*4)*(1-t)"}, {"duration", "650"}, {"easing", "linear"}});
    add("wobble", {{"x_fn", "sin(t*pi*6)*(1-t)*10"}, {"duration", "700"}, {"easing", "linear"}});
    add("tick", {{"y_fn", "-sin(t*pi)*6"}, {"duration", "260"}, {"easing", "linear"}});
    kf("highlight", "from{bg:warning} to{}", "900", "easeOut");
    kf("ping", "0%{glow:0} 40%{glow:22,glowColor:primary} 100%{glow:0}", "800", "easeOut");
    // ---- حلقات ----
    add("float", {{"y_fn", "sin(t*pi*2)*6"}, {"duration", "2400"}, {"easing", "linear"}, {"iterations", "infinite"}});
    add("breathe", {{"scale_fn", "1+0.03*sin(t*pi*2)"}, {"duration", "3000"}, {"easing", "linear"}, {"iterations", "infinite"}});
    add("glow", {{"glow_fn", "8+6*sin(t*pi*2)"}, {"frames", "0%{glowColor:primary}"}, {"duration", "1800"}, {"easing", "linear"}, {"iterations", "infinite"}});
    add("blink", {{"opacity_fn", "0.6+0.4*cos(t*pi*2)"}, {"duration", "1000"}, {"easing", "linear"}, {"iterations", "infinite"}});
    add("flicker", {{"opacity_fn", "0.75+0.25*noise(t*14)"}, {"duration", "1200"}, {"easing", "linear"}, {"iterations", "infinite"}});
    add("sway", {{"x_fn", "sin(t*pi*2)*8"}, {"duration", "2600"}, {"easing", "linear"}, {"iterations", "infinite"}});
    // ---- خروج (تبقى على آخر إطار) ----
    kf("fadeOut", "from{opacity:1} to{opacity:0}", "250", "easeIn", {{"fill", "forwards"}});
    kf("scaleOut", "from{opacity:1,scale:1} to{opacity:0,scale:.8}", "250", "easeIn", {{"fill", "forwards"}});
    kf("slideOutUp", "from{opacity:1,y:0} to{opacity:0,y:-24}", "250", "easeIn", {{"fill", "forwards"}});
    kf("slideOutDown", "from{opacity:1,y:0} to{opacity:0,y:24}", "250", "easeIn", {{"fill", "forwards"}});
    kf("slideOutLeft", "from{opacity:1,x:0} to{opacity:0,x:-24}", "250", "easeIn", {{"fill", "forwards"}});
    kf("slideOutRight", "from{opacity:1,x:0} to{opacity:0,x:24}", "250", "easeIn", {{"fill", "forwards"}});
}

// ================================================================================================
// 5) التشغيل: توقيت + تقييم إطار
// ================================================================================================
struct Timing {
    double durationMs = 250, delayMs = 0, iterations = 1;
    Direction dir = Direction::NORMAL; int fill = FILL_BOTH;
    EasingSpec easing;
};

inline double dirPhase(Direction d, int idx, double ph) {
    switch (d) {
        case Direction::NORMAL: return ph;
        case Direction::REVERSE: return 1.0 - ph;
        case Direction::ALTERNATE: return (idx & 1) ? 1.0 - ph : ph;
        case Direction::ALTERNATE_REVERSE: return (idx & 1) ? ph : 1.0 - ph;
    }
    return ph;
}
// localMs: الزمن منذ بدء الدورة الأولى (بعد delay). يعيد true إن كان للتأثير أثر في هذا الإطار.
inline bool playPhase(const Timing& tm, double localMs, double& phase, int& iter, bool& finished) {
    finished = false; iter = 0;
    if (localMs < 0) { phase = dirPhase(tm.dir, 0, 0.0); return (tm.fill & FILL_BACKWARDS) != 0; }
    const bool inf = tm.iterations < 0;
    if (tm.durationMs <= 0 || (!inf && localMs >= tm.durationMs * tm.iterations)) {
        finished = true;
        if (!(tm.fill & FILL_FORWARDS)) return false;
        double it = tm.iterations < 0 ? 1 : tm.iterations; double fr = it - std::floor(it);
        int idx; double ph;
        if (fr == 0) { idx = (int)it - 1; ph = 1.0; } else { idx = (int)std::floor(it); ph = fr; }
        if (idx < 0) idx = 0;
        phase = dirPhase(tm.dir, idx, ph); iter = idx; return true;
    }
    double x = localMs / tm.durationMs; int idx = (int)std::floor(x); double ph = x - idx;
    iter = idx; phase = dirPhase(tm.dir, idx, ph); return true;
}

struct Sample {
    double v[NCH]; Color col[NCOL]; double amt[NCOL]; bool colSet[NCOL];
    Sample() {
        for (int i = 0; i < NCH; i++) v[i] = channelIdentity(i);
        for (int c = 0; c < NCOL; c++) { amt[c] = 0; col[c] = Color{0, 0, 0, 255}; colSet[c] = false; }
    }
};

inline Color lerpColor(const Color& a, const Color& b, double u) {
    auto m = [&](unsigned char x, unsigned char y) { return (unsigned char)std::max(0.0, std::min(255.0, std::round(x + (y - x) * u))); };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
inline bool resolveColorSpec(const std::string& spec, Color& out) {
    std::string s = trim(spec);
    if (s.empty()) return false;
    if (resolveSemanticColor(s, out)) return true;
    if (looksLikeHexColor(s)) { out = parseHexColor(s, Color{0, 0, 0, 255}); return true; }
    return false;
}

inline double evalNumChannel(const EffectDef& d, int ch, double t, const EasingSpec& ease) {
    struct P { double t, v; const Keyframe* k; };
    std::vector<P> pts;
    for (auto& k : d.frames) if (k.has[ch]) pts.push_back({k.t, k.v[ch], &k});
    const double id = channelIdentity(ch);
    if (pts.empty()) return id;
    if (pts.front().t > 1e-9) pts.insert(pts.begin(), P{0.0, id, nullptr});
    if (pts.back().t < 1 - 1e-9) pts.push_back(P{1.0, id, nullptr});
    if (t <= pts.front().t) return pts.front().v;
    if (t >= pts.back().t) return pts.back().v;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        if (t >= pts[i].t && t <= pts[i + 1].t) {
            double span = pts[i + 1].t - pts[i].t;
            if (span <= 1e-12) return pts[i + 1].v;
            double u = (t - pts[i].t) / span;
            const EasingSpec& e = (pts[i].k && pts[i].k->hasEase) ? pts[i].k->ease : ease;
            double ue = applyEasing(e, u);
            return pts[i].v + (pts[i + 1].v - pts[i].v) * ue;
        }
    }
    return id;
}
inline bool evalColorChannel(const EffectDef& d, int c, double t, const EasingSpec& ease, Color& outCol, double& outAmt) {
    struct P { double t; Color c; double a; const Keyframe* k; };
    std::vector<P> pts;
    for (auto& k : d.frames) if (k.hasCol[c]) { Color cc; if (resolveColorSpec(k.col[c], cc)) pts.push_back(P{k.t, cc, 1.0, &k}); }
    if (pts.empty()) return false;
    if (pts.front().t > 1e-9) { Color c0 = pts.front().c; pts.insert(pts.begin(), P{0.0, c0, 0.0, nullptr}); }
    if (pts.back().t < 1 - 1e-9) { Color c1 = pts.back().c; pts.push_back(P{1.0, c1, 0.0, nullptr}); }
    if (t <= pts.front().t) { outCol = pts.front().c; outAmt = pts.front().a; return true; }
    if (t >= pts.back().t) { outCol = pts.back().c; outAmt = pts.back().a; return true; }
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        if (t >= pts[i].t && t <= pts[i + 1].t) {
            double span = pts[i + 1].t - pts[i].t;
            if (span <= 1e-12) { outCol = pts[i + 1].c; outAmt = pts[i + 1].a; return true; }
            const EasingSpec& e = (pts[i].k && pts[i].k->hasEase) ? pts[i].k->ease : ease;
            double u = applyEasing(e, (t - pts[i].t) / span);
            outCol = lerpColor(pts[i].c, pts[i + 1].c, u); outAmt = pts[i].a + (pts[i + 1].a - pts[i].a) * u; return true;
        }
    }
    return false;
}
// phase: تقدّم الدورة (بعد الاتجاه) 0..1
inline void sampleEffect(const EffectDef& d, const EasingSpec& ease, double phase, double iter, double w, double h, Sample& s) {
    FxEnv env; env.t = phase; env.n = iter; env.w = w; env.h = h;
    for (int ch = 0; ch < NCH; ch++) {
        if (d.fn[ch]) s.v[ch] = evalFx(*d.fn[ch], env);
        else if (d.usesChannel(ch)) s.v[ch] = evalNumChannel(d, ch, phase, ease);
    }
    for (int c = 0; c < NCOL; c++) {
        Color cc; double a;
        if (evalColorChannel(d, c, phase, ease, cc, a)) { s.col[c] = cc; s.amt[c] = a; s.colSet[c] = true; }
    }
}

// ================================================================================================
// 6) EffectRuntime: حالة الجلسة + حساب إطارات كل Strand + الإيماءات
// ================================================================================================
// الإطار الناتج لـ Strand (مجمّعاً مع أسلافه): p' = (sx*p.x + tx, sy*p.y + ty) + opacity + ألوان.
struct StrandFx {
    double opacity = 1, sx = 1, sy = 1, tx = 0, ty = 0;
    bool hasFg = false; Color fg; double fgAmt = 0;                    // موروث للأبناء (لون النص)
    bool hasBg = false; Color bg; double bgAmt = 0;                    // للعنصر نفسه فقط
    double glow = 0; bool hasGlowColor = false; Color glowColor;       // للعنصر نفسه فقط
    bool isIdentity() const {
        return opacity >= 0.9999 && sx == 1 && sy == 1 && tx == 0 && ty == 0 && !hasFg && !hasBg && glow <= 0;
    }
};

class EffectRuntime {
public:
    enum Origin { ENTER = 0, TRIGGER = 1 };
    struct Play {
        EffectDefPtr def; double startMs = 0, extraDelay = 0; Origin origin = ENTER;
        Timing timing; // للإيماءات (مثبَّت وقت التشغيل)؛ للدخول يُعاد حسابه كل tick من سمات العنصر (تعديل حيّ)
        std::string tag;
    };
    struct State { std::vector<Play> plays; std::string enterKey; std::string watch; bool watchSeen = false; };

    // يُستدعى بعد التخطيط في كل relayout/tick. يحسب الإطارات (لا يمسّ الـFabric). true => ما زال شيء يتحرّك.
    bool update(const StrandPtr& root, double nowMs) {
        frames_.clear(); present_.clear(); animating_ = false;
        walk(root, StrandFx{}, "", false, 0.0, nowMs);
        for (auto it = st_.begin(); it != st_.end();) { if (!present_.count(it->first)) it = st_.erase(it); else ++it; }
        return animating_;
    }
    const StrandFx* frameOf(StrandId id) const { auto it = frames_.find(id); return it == frames_.end() ? nullptr : &it->second; }
    bool empty() const { return frames_.empty(); }
    bool animating() const { return animating_; }
    void reset() { st_.clear(); frames_.clear(); present_.clear(); animating_ = false; }

    // تشغيل قائمة تأثيرات على Strand (إيماءة/Rin). prefix: بادئة سمات التوقيت ("effect_tap_") أو "" (+ opts).
    bool play(const StrandPtr& s, const std::string& effectList, double nowMs, const std::string& prefix = "", const KV* opts = nullptr) {
        if (!s) return false;
        auto defs = effectLibrary().resolveList(effectList);
        if (defs.empty()) return false;
        State& stt = st_[s->id];
        for (auto& d : defs) {
            Play p; p.def = d; p.startMs = nowMs; p.origin = TRIGGER; p.tag = d->name;
            p.timing = timingFor(*d, s.get(), prefix, opts, false);
            // نفس التأثير يعمل => يُعاد من البداية
            stt.plays.erase(std::remove_if(stt.plays.begin(), stt.plays.end(), [&](const Play& o) { return o.origin == TRIGGER && o.tag == p.tag; }), stt.plays.end());
            stt.plays.push_back(p);
        }
        return true;
    }
    // يوقف تأثيرات عنصر (الكل إن لم يُذكر اسم). تأثير الدخول المنتهي لا يُعاد تلقائياً (enterKey ثابت).
    void stop(const StrandPtr& s, const std::string& effectName = "") {
        auto it = st_.find(s->id); if (it == st_.end()) return;
        std::string q = squash(effectName);
        auto& v = it->second.plays;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Play& p) { return q.empty() || squash(p.tag) == q; }), v.end());
    }
    // إيماءة: أقرب Strand (من الورقة نحو الجذر) يحمل attrKey (مثل "effect_tap") يُشغَّل تأثيره. true إن شُغّل شيء.
    bool fireTrigger(const std::vector<StrandPtr>& pathLeafToRoot, const std::string& attrKey, double nowMs) {
        for (auto& s : pathLeafToRoot) {
            if (!s) continue;
            const Value* v = s->attr(attrKey);
            if (!v) continue;
            std::string list = trim(v->asString());
            if (list.empty() || squash(list) == "none") continue;
            return play(s, list, nowMs, attrKey + "_");
        }
        return false;
    }
    // أوامر Rin المتراكمة (indsin.playEffect / effect() / stop). يعيد true إن غيّرت شيئاً.
    bool processPending(const StrandPtr& root, double nowMs) {
        auto reqs = effectLibrary().takePending();
        bool any = false;
        for (auto& r : reqs) {
            std::vector<StrandPtr> hits; findByName(root, r.target, hits);
            for (auto& s : hits) {
                if (r.stop) { stop(s, r.effect); any = true; }
                else if (play(s, r.effect, nowMs, "", &r.opts)) any = true;
            }
        }
        return any;
    }

private:
    std::unordered_map<StrandId, State> st_;
    std::unordered_map<StrandId, StrandFx> frames_;
    std::unordered_set<StrandId> present_;
    bool animating_ = false;

    static void findByName(const StrandPtr& s, const std::string& name, std::vector<StrandPtr>& out) {
        if (!s) return;
        if (s->name == name || ("#" + s->name) == name) out.push_back(s);
        for (auto& c : s->children) findByName(c, name, out);
    }
    // تجميع التوقيت: التعريف ← سمات العنصر ← opts
    //   الدخول (prefix فارغ): effect_<k> أو (للتوافق) duration/delay/easing المجرّدة. الإيماءات: <prefix><k>.
    //   direction/fill/iterations تُقرأ بالبادئة فقط (effect_direction…) لأن direction= اسم سمة أخرى في Box.
    static Timing timingFor(const EffectDef& d, const Strand* s, const std::string& prefix, const KV* opts, bool isEnter) {
        Timing t; t.durationMs = d.durationMs; t.delayMs = d.delayMs; t.iterations = d.iterations; t.dir = d.dir;
        t.fill = d.fill >= 0 ? d.fill : (isEnter ? FILL_BOTH : FILL_NONE); t.easing = d.easing;
        auto get = [&](const char* key, bool legacyBare, std::string& out) {
            if (s) {
                if (const Value* v = s->attr(prefix + key)) { out = v->asString(); return true; }
                if (prefix.empty()) {
                    if (const Value* v = s->attr(std::string("effect_") + key)) { out = v->asString(); return true; }
                    if (legacyBare) if (const Value* v = s->attr(key)) { out = v->asString(); return true; }
                }
            }
            if (opts) for (auto& kv : *opts) if (squash(kv.first) == key) { out = kv.second; return true; }
            return false;
        };
        std::string v; double x;
        if (get("duration", true, v) && parseDurationMs(v, x) && x >= 0) t.durationMs = x;
        if (get("delay", true, v) && parseDurationMs(v, x)) t.delayMs = x;
        if (get("iterations", false, v) && parseIterations(v, x)) t.iterations = x;
        if (get("direction", false, v)) parseDirection(v, t.dir);
        if (get("fill", false, v)) parseFill(v, t.fill);
        if (get("easing", true, v)) { bool ok; EasingSpec e = parseEasing(v, &ok); if (ok) t.easing = e; }
        return t;
    }
    // مخفي => يُعتبر غائباً (فيُعاد تشغيل دخوله حين يظهر): visible=false، Dialog/Drawer مغلق، Tooltip مغلق.
    static bool isHidden(const Strand& s) {
        if (s.attrStr("visible", "true") == "false") return true;
        if ((s.kind == StrandKind::DIALOG || s.kind == StrandKind::DRAWER) && s.attrStr("open", "false") != "true") return true;
        if (s.kind == StrandKind::TOOLTIP && s.attrStr("open", "true") == "false") return true;
        return false;
    }

    void walk(const StrandPtr& s, const StrandFx& parent, const std::string& inheritKey, bool hasInherit, double extraDelay, double now) {
        if (!s || isHidden(*s)) return;
        present_.insert(s->id);
        State& stt = st_[s->id];

        // ---- مفتاح تأثير الدخول: effect= الخاص، وإلا ما ورثه من حاوية بـ stagger ----
        const Value* ev = s->attr("effect");
        std::string key = ev ? trim(ev->asString()) : (hasInherit ? inheritKey : "");
        if (squash(key) == "none") key.clear();
        const double stagger = s->attrNum("stagger", 0);
        std::string childInherit; bool childHas = false;
        if (stagger > 0 && ev && !key.empty()) { childInherit = key; childHas = true; key.clear(); } // الحاوية لا تتحرّك؛ أبناؤها المباشرون يتتابعون
        if (key != stt.enterKey) {
            stt.plays.erase(std::remove_if(stt.plays.begin(), stt.plays.end(), [](const Play& p) { return p.origin == ENTER; }), stt.plays.end());
            stt.enterKey = key;
            if (!key.empty()) for (auto& d : effectLibrary().resolveList(key)) {
                Play p; p.def = d; p.startMs = now; p.extraDelay = extraDelay; p.origin = ENTER; p.tag = d->name;
                stt.plays.push_back(p);
            }
        } else {
            for (auto& p : stt.plays) if (p.origin == ENTER) p.extraDelay = extraDelay;
        }

        // ---- effect_on_change: تشغيل كلما تغيّرت القيمة المراقَبة (effect_key، وإلا text/value/label) ----
        if (const Value* oc = s->attr("effect_on_change")) {
            const Value* w = s->attr("effect_key"); if (!w) w = s->attr("text"); if (!w) w = s->attr("value"); if (!w) w = s->attr("label");
            std::string cur = w ? w->asString() : "";
            if (stt.watchSeen && cur != stt.watch) play(s, trim(oc->asString()), now, "effect_on_change_");
            stt.watch = cur; stt.watchSeen = true;
        }

        // ---- تقييم كل الـplays لهذا العنصر ----
        double opacity = 1, sx = 1, sy = 1, dx = 0, dy = 0, glow = 0;
        bool hasBg = false, hasGlowC = false, hasFg = false; Color bgC, glowC, fgC; double bgA = 0, fgA = 0;
        const double W = s->geometry.w, H = s->geometry.h;
        for (auto it = stt.plays.begin(); it != stt.plays.end();) {
            Play& p = *it;
            Timing tm = (p.origin == ENTER) ? timingFor(*p.def, s.get(), "", nullptr, true) : p.timing;
            double local = now - p.startMs - tm.delayMs - p.extraDelay;
            double phase; int iter; bool finished;
            bool applies = playPhase(tm, local, phase, iter, finished);
            if (!finished) animating_ = true;
            if (applies) {
                Sample sm; sampleEffect(*p.def, tm.easing, phase, iter, W, H, sm);
                opacity *= std::max(0.0, std::min(1.0, sm.v[CH_OPACITY]));
                sx *= sm.v[CH_SX]; sy *= sm.v[CH_SY];
                dx += sm.v[CH_X] + sm.v[CH_XR] * W; dy += sm.v[CH_Y] + sm.v[CH_YR] * H;
                glow += sm.v[CH_GLOW];
                if (sm.colSet[COL_BG] && sm.amt[COL_BG] > 0) { hasBg = true; bgC = sm.col[COL_BG]; bgA = sm.amt[COL_BG]; }
                if (sm.colSet[COL_FG] && sm.amt[COL_FG] > 0) { hasFg = true; fgC = sm.col[COL_FG]; fgA = sm.amt[COL_FG]; }
                if (sm.colSet[COL_GLOW]) { hasGlowC = true; glowC = sm.col[COL_GLOW]; }
            }
            // انتهى ولا يحتفظ بآخر إطار => أزله (enterKey يمنع إعادة الدخول)
            if (finished && !(tm.fill & FILL_FORWARDS)) it = stt.plays.erase(it); else ++it;
        }

        // ---- تركيب مع الأب: p' = parent( centre + s*(p-centre) + d ) ----
        StrandFx fx;
        const double cx = s->geometry.x + W / 2, cy = s->geometry.y + H / 2;
        const double ownTx = cx * (1 - sx) + dx, ownTy = cy * (1 - sy) + dy;
        fx.opacity = parent.opacity * opacity;
        fx.sx = parent.sx * sx; fx.sy = parent.sy * sy;
        fx.tx = parent.sx * ownTx + parent.tx; fx.ty = parent.sy * ownTy + parent.ty;
        fx.hasFg = parent.hasFg; fx.fg = parent.fg; fx.fgAmt = parent.fgAmt;
        if (hasFg) { fx.hasFg = true; fx.fg = fgC; fx.fgAmt = fgA; }
        if (hasBg) { fx.hasBg = true; fx.bg = bgC; fx.bgAmt = bgA; }
        if (glow > 0.01) fx.glow = glow;
        if (hasGlowC) { fx.hasGlowColor = true; fx.glowColor = glowC; }
        // الأبناء يرثون التحويل/الشفافية/لون النص فقط (bg وglow للعنصر نفسه)
        StrandFx inheritFx = fx; inheritFx.hasBg = false; inheritFx.glow = 0; inheritFx.hasGlowColor = false;
        if (!fx.isIdentity()) frames_[s->id] = fx;

        int idx = 0;
        for (auto& c : s->children) { walk(c, inheritFx, childInherit, childHas, stagger * idx, now); idx++; }
    }
};

// ================================================================================================
// 7) تعريف التأثيرات من البرنامج: @effect=name … .end/effect  (و <Effect> في صيغة الوسوم)
// ================================================================================================
// تُستدعى من الـpipeline بعد التحليل وقبل بناء الـFabric (نفس مكان registerThemesFromProgram).
inline void registerEffectsFromProgram(const std::vector<rin::StmtPtr>& program, WarpScope& warp) {
    for (auto& stmt : program) {
        auto e = std::dynamic_pointer_cast<rin::EffectStmt>(stmt);
        if (!e) continue;
        KV kv;
        for (auto& a : e->attrs) {
            Value v = evalAttrExprKeyed(a.key, a.value, warp, nullptr);
            kv.push_back({a.key, v.asString()});
        }
        std::string err;
        if (!effectLibrary().define(e->name, kv, err)) effectLibrary().lastError = "line " + std::to_string(e->line) + ": " + err;
        else effectLibrary().lastError.clear();
    }
}

inline std::string effectsJson() {
    auto& lib = effectLibrary();
    std::ostringstream os; os << "[";
    bool first = true;
    for (auto& n : lib.names()) {
        auto d = lib.find(n); if (!d) continue;
        os << (first ? "" : ",") << "{\"name\":\"" << jsonStr(d->name) << "\",\"builtin\":" << (d->builtin ? "true" : "false")
           << ",\"duration\":" << d->durationMs << ",\"iterations\":";
        if (d->iterations < 0) os << "\"infinite\""; else os << d->iterations;
        os << ",\"channels\":[";
        bool f2 = true;
        for (int ch = 0; ch < NCH; ch++) if (d->usesChannel(ch)) { os << (f2 ? "" : ",") << "\"" << channelName(ch) << "\""; f2 = false; }
        static const char* cn[] = {"bg", "color", "glowColor"};
        for (int c = 0; c < NCOL; c++) if (d->usesColor(c)) { os << (f2 ? "" : ",") << "\"" << cn[c] << "\""; f2 = false; }
        os << "]}"; first = false;
    }
    os << "]";
    return os.str();
}

} // namespace fx

// الأسماء العامة (للتوافق مع بقية المحرّك: c_api وغيره)
using fx::EffectRuntime;
using fx::EffectLibrary;
using fx::effectLibrary;
using fx::registerEffectsFromProgram;

} // namespace indsin
