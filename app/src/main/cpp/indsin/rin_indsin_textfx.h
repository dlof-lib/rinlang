// indsin/rin_indsin_textfx.h — حركة النص: TextAnimation · TypeWriter · FadeText · TextTransition · TextReveal ·
// TextRotate · TextTransform(الثابت في typography) · TextShadowAnimation.
//
// نفس فلسفة Effects Studio: لا يمسّ الـFabric إطلاقاً. كل «إطار» يُحسَب من الزمن ثم يُطبَّق على TEXT_RUN في DrawList
// (applyTextFxToDrawList) — فلا انجراف ولا تعارض مع كاش التخطيط، وتعمل كل المضيفات التي تستهلك DrawList/JSON.
// برنامج بلا أي من السمات أدناه لا يُنشئ أي إطار (كلفة صفر).
//
//   text_animation="typewriter fade-in slide-up reveal-left rotate-in spin zoom-in bounce shake wave blink pulse rainbow glow neon flicker orbit"
//   typewriter=true|<cps>     type_speed=30 type_duration= type_delay= type_cursor="|" type_cursor_blink=530 type_loop type_erase type_hold=1500
//   fade_text="in|out|chars|words|inout"   fade_stagger=ms
//   text_reveal="left|right|up|down|center|start|end|chars|words"
//   text_rotate=30 (ثابت) · text_rotate_to=360 + text_rotate_duration/_iterations · text_rotate="spin"
//   text_rotator="Fast|Simple|Fun"  text_rotator_interval=2000  text_rotator_effect=fade|slide-up|…
//   text_transition="fade|slide-up|slide-down|slide-left|slide-right|zoom|rotate|none"  (يعمل كلما تغيّر النص)
//   text_shadow_animation="pulse|glow|neon|flicker|orbit|drop|from-to"  text_shadow= text_shadow_to= text_shadow_color=
//   توقيت مشترك: text_duration text_delay text_iterations(n|infinite) text_easing text_direction text_fill text_stagger text_key text_restart="change"
//   توقيت خاص يتغلّب: <سمة>_duration … مثل type_delay / fade_duration / text_reveal_duration / text_rotator_interval
#pragma once
#include "rin_indsin_effects.h"
#include "rin_indsin_paint.h"
#include <unordered_map>

namespace indsin {
namespace textfx {

using fx::Timing;
constexpr double kPi = 3.14159265358979323846;

// ---- قراءة السمات ----------------------------------------------------------------------------------
inline Timing readTiming(const Strand& s, std::initializer_list<const char*> prefixes, Timing def) {
    auto get = [&](const char* suf) -> const Value* {
        for (auto p : prefixes) if (const Value* v = s.attr(std::string(p) + "_" + suf)) return v;
        return s.attr(std::string("text_") + suf);
    };
    Timing t = def; double d; int f; fx::Direction dir;
    if (const Value* v = get("duration")) if (fx::parseDurationMs(v->asString(), d)) t.durationMs = std::max(0.0, d);
    if (const Value* v = get("delay")) if (fx::parseDurationMs(v->asString(), d)) t.delayMs = d;
    if (const Value* v = get("iterations")) if (fx::parseIterations(v->asString(), d)) t.iterations = d;
    if (const Value* v = get("direction")) if (fx::parseDirection(v->asString(), dir)) t.dir = dir;
    if (const Value* v = get("fill")) if (fx::parseFill(v->asString(), f)) t.fill = f;
    if (const Value* v = get("easing")) { bool ok = false; auto e = fx::parseEasing(v->asString(), &ok); if (ok) t.easing = e; }
    return t;
}
inline double numAttr(const Strand& s, const char* k, double def) {
    const Value* v = s.attr(k); if (!v) return def;
    double d; if (fx::parseDurationMs(v->asString(), d) && v->kind == Value::Kind::STRING) return d; // "400ms"/"1s"
    return v->asNumber(def);
}
inline std::string sq(const std::string& s) { return fx::squash(s); }

inline std::string visibleText(const Strand& s) {
    if (const Value* v = s.attr("text")) return v->asString();
    if (const Value* v = s.attr("label")) return v->asString();
    if (const Value* v = s.attr("title")) return v->asString();
    return "";
}
inline size_t countUnits(const std::vector<uint32_t>& cps, int unit) {
    if (unit == 1) return cps.size();
    size_t n = 0; bool in = false;
    for (uint32_t c : cps) { bool sp = c == ' ' || c == '\n' || c == '\t'; if (!sp && !in) { n++; in = true; } if (sp) in = false; }
    return n;
}
inline Color hueColor(double h, unsigned char a = 255) {
    h = h - std::floor(h / 360.0) * 360.0;
    double c = 0.78, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = 0.9 - c / 2, r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    auto B = [&](double v) { return (unsigned char)std::lround(std::max(0.0, std::min(1.0, v + m)) * 255); };
    return Color{B(r), B(g), B(b), a};
}

// ---- الإطار -----------------------------------------------------------------------------------------
struct Frame {
    bool hasText = false; std::string text;        // استبدال كامل للنص (Rotator)
    int revealChars = -1; std::string cursor;      // Typewriter
    double opacity = 1, dx = 0, dy = 0, rotate = 0, scale = 1;
    double clip[4] = {0, 0, 0, 0};                 // L,T,R,B
    int unit = 0;                                  // 0 لا · 1 حروف · 2 كلمات
    int ukind = 0;                                 // 1 fade · 2 reveal · 3 wave
    double up = 0, uspread = 0.6, uamp = 0;        // up: التقدّم · uspread: نسبة التتابع · uamp: سعة الموجة
    bool hasShadow = false; std::vector<TextShadow> shadows;
    bool hasColor = false; Color color;
    bool hasOut = false; std::string outText; double outOpacity = 1, outDx = 0, outDy = 0, outScale = 1, outRotate = 0;
    double inScale = 1, inRotate = 0;              // تحويل النص الداخل (Transition) — يُضرب في scale/rotate
    bool any() const { return hasText || revealChars >= 0 || opacity < 0.9999 || dx != 0 || dy != 0 || rotate != 0 || scale != 1 || unit || hasShadow || hasColor || hasOut ||
                              clip[0] > 0 || clip[1] > 0 || clip[2] > 0 || clip[3] > 0; }
};

enum class P { NONE, TYPEWRITER, FADE_IN, FADE_OUT, FADE_CHARS, FADE_WORDS, SLIDE_UP, SLIDE_DOWN, SLIDE_LEFT, SLIDE_RIGHT,
               REVEAL_L, REVEAL_R, REVEAL_U, REVEAL_D, REVEAL_C, REVEAL_START, REVEAL_CHARS, REVEAL_WORDS,
               ROTATE_IN, SPIN, ZOOM_IN, ZOOM_OUT, BOUNCE, SHAKE, WAVE, BLINK, PULSE, RAINBOW,
               GLOW, NEON, FLICKER, ORBIT, DROP };

struct PresetInfo { const char* name; P kind; double dur; double iter; const char* dir; const char* ease; };
inline const PresetInfo* findPreset(const std::string& raw) {
    static const PresetInfo T[] = {
        {"typewriter", P::TYPEWRITER, 0, 1, "normal", "linear"}, {"typing", P::TYPEWRITER, 0, 1, "normal", "linear"}, {"type", P::TYPEWRITER, 0, 1, "normal", "linear"},
        {"fade", P::FADE_IN, 600, 1, "normal", "easeout"}, {"fadein", P::FADE_IN, 600, 1, "normal", "easeout"}, {"fadeout", P::FADE_OUT, 600, 1, "normal", "easein"},
        {"fadechars", P::FADE_CHARS, 900, 1, "normal", "linear"}, {"fadeinchars", P::FADE_CHARS, 900, 1, "normal", "linear"},
        {"fadewords", P::FADE_WORDS, 900, 1, "normal", "linear"}, {"fadeinwords", P::FADE_WORDS, 900, 1, "normal", "linear"},
        {"slideup", P::SLIDE_UP, 500, 1, "normal", "easeout"}, {"slidedown", P::SLIDE_DOWN, 500, 1, "normal", "easeout"},
        {"slideleft", P::SLIDE_LEFT, 500, 1, "normal", "easeout"}, {"slideright", P::SLIDE_RIGHT, 500, 1, "normal", "easeout"},
        {"reveal", P::REVEAL_START, 800, 1, "normal", "easeinout"}, {"revealstart", P::REVEAL_START, 800, 1, "normal", "easeinout"},
        {"revealleft", P::REVEAL_L, 800, 1, "normal", "easeinout"}, {"revealright", P::REVEAL_R, 800, 1, "normal", "easeinout"},
        {"revealup", P::REVEAL_U, 700, 1, "normal", "easeinout"}, {"revealdown", P::REVEAL_D, 700, 1, "normal", "easeinout"},
        {"revealcenter", P::REVEAL_C, 800, 1, "normal", "easeinout"}, {"revealchars", P::REVEAL_CHARS, 900, 1, "normal", "linear"},
        {"revealwords", P::REVEAL_WORDS, 900, 1, "normal", "linear"},
        {"rotatein", P::ROTATE_IN, 600, 1, "normal", "easeout"}, {"rotate", P::ROTATE_IN, 600, 1, "normal", "easeout"},
        {"spin", P::SPIN, 2000, -1, "normal", "linear"}, {"zoomin", P::ZOOM_IN, 500, 1, "normal", "easeout"}, {"zoomout", P::ZOOM_OUT, 500, 1, "normal", "easeout"},
        {"pop", P::ZOOM_IN, 400, 1, "normal", "backout"}, {"bounce", P::BOUNCE, 900, 1, "normal", "linear"}, {"shake", P::SHAKE, 500, 1, "normal", "linear"},
        {"wave", P::WAVE, 1600, -1, "normal", "linear"}, {"blink", P::BLINK, 1000, -1, "normal", "linear"}, {"pulse", P::PULSE, 1200, -1, "alternate", "easeinout"},
        {"rainbow", P::RAINBOW, 3000, -1, "normal", "linear"},
        {"glow", P::GLOW, 1600, -1, "alternate", "easeinout"}, {"neon", P::NEON, 1800, -1, "alternate", "easeinout"},
        {"flicker", P::FLICKER, 1400, -1, "normal", "linear"}, {"orbit", P::ORBIT, 2400, -1, "normal", "linear"}, {"drop", P::DROP, 1200, -1, "alternate", "easeinout"},
    };
    std::string q = sq(raw);
    for (auto& p : T) if (q == p.name) return &p;
    return nullptr;
}
inline bool isShadowPreset(P k) { return k == P::GLOW || k == P::NEON || k == P::FLICKER || k == P::ORBIT || k == P::DROP; }

// ---- انتقال نص (TextTransition / Rotator) ---------------------------------------------------------------
enum class TK { NONE, FADE, SLIDE_UP, SLIDE_DOWN, SLIDE_LEFT, SLIDE_RIGHT, ZOOM, ROTATE };
inline TK transitionKind(const std::string& raw, bool* ok = nullptr) {
    std::string q = sq(raw); if (ok) *ok = true;
    if (q == "fade" || q == "crossfade" || q == "true") return TK::FADE;
    if (q == "slideup" || q == "up") return TK::SLIDE_UP;
    if (q == "slidedown" || q == "down") return TK::SLIDE_DOWN;
    if (q == "slideleft" || q == "left") return TK::SLIDE_LEFT;
    if (q == "slideright" || q == "right") return TK::SLIDE_RIGHT;
    if (q == "zoom" || q == "scale") return TK::ZOOM;
    if (q == "rotate" || q == "flip") return TK::ROTATE;
    if (q == "none" || q == "false" || q.empty()) return TK::NONE;
    if (ok) *ok = false; return TK::NONE;
}
// p: 0 = بداية الانتقال (القديم كاملاً) · 1 = نهايته (الجديد كاملاً). يملأ جانبي الدخول والخروج في الإطار.
inline void applyTransition(TK k, double p, double dist, Frame& f) {
    if (k == TK::NONE) return;
    double e = std::max(0.0, std::min(1.0, p));
    f.opacity *= e; f.hasOut = true; f.outOpacity = 1 - e;
    switch (k) {
        case TK::SLIDE_UP:    f.dy += dist * (1 - e);  f.outDy = -dist * e; break;
        case TK::SLIDE_DOWN:  f.dy -= dist * (1 - e);  f.outDy = dist * e; break;
        case TK::SLIDE_LEFT:  f.dx += dist * (1 - e);  f.outDx = -dist * e; break;
        case TK::SLIDE_RIGHT: f.dx -= dist * (1 - e);  f.outDx = dist * e; break;
        case TK::ZOOM:        f.inScale = 0.8 + 0.2 * e; f.outScale = 1.0 + 0.2 * e; break;
        case TK::ROTATE:      f.inRotate = -60 * (1 - e); f.outRotate = 60 * e; break;
        default: break;
    }
}

// ---- الظل المتحرّك -------------------------------------------------------------------------------------
inline std::vector<TextShadow> padTo(std::vector<TextShadow> v, size_t n, const Color& c) {
    while (v.size() < n) v.push_back(TextShadow{0, 0, 0, Color{c.r, c.g, c.b, 0}});
    return v;
}
inline std::vector<TextShadow> lerpShadows(const std::vector<TextShadow>& a0, const std::vector<TextShadow>& b0, double u) {
    size_t n = std::max(a0.size(), b0.size());
    Color ref = !b0.empty() ? b0[0].color : (!a0.empty() ? a0[0].color : Color{0, 0, 0, 255});
    auto a = padTo(a0, n, ref), b = padTo(b0, n, ref);
    std::vector<TextShadow> o;
    for (size_t i = 0; i < n; i++)
        o.push_back(TextShadow{a[i].dx + (b[i].dx - a[i].dx) * u, a[i].dy + (b[i].dy - a[i].dy) * u, a[i].blur + (b[i].blur - a[i].blur) * u, typo::lerpC(a[i].color, b[i].color, u)});
    return o;
}
// ph: طور خام 0..1 · p: طور مخفَّف · tMs: زمن محلي لوميض مستقر
inline std::vector<TextShadow> shadowPreset(P kind, double ph, double p, double tMs, Color c, double radius) {
    auto A = [](Color x, double a) { x.a = (unsigned char)std::lround(std::max(0.0, std::min(255.0, x.a * a))); return x; };
    switch (kind) {
        case P::GLOW:  return {TextShadow{0, 0, 2 + 14 * p, A(c, 0.35 + 0.65 * p)}};
        case P::NEON: {
            double k = 0.6 + 0.6 * p;
            return {TextShadow{0, 0, 3 * k, A(c, 1.0)}, TextShadow{0, 0, 10 * k, A(c, 0.9)}, TextShadow{0, 0, 22 * k, A(c, 0.7)}};
        }
        case P::FLICKER: {
            double step = std::floor(tMs / 70.0), h = fx::fxHash(step);
            double on = h < 0.18 ? 0.15 : (h < 0.3 ? 0.55 : 1.0);
            return {TextShadow{0, 0, 4, A(c, on)}, TextShadow{0, 0, 14 * on, A(c, on * 0.8)}};
        }
        case P::ORBIT: { double a = ph * 2 * kPi; return {TextShadow{std::cos(a) * radius, std::sin(a) * radius, 3, A(c, 0.9)}}; }
        case P::DROP:  return {TextShadow{0, 1 + 5 * p, 1 + 7 * p, A(Color{0, 0, 0, 255}, 0.25 + 0.3 * p)}};
        default: return {};
    }
}

// ---- الحالة والتشغيل -----------------------------------------------------------------------------------------
class TextFxRuntime {
public:
    struct State {
        double startMs = 0; bool started = false; std::string key, lastText; bool seenText = false;
        double transStart = -1; std::string outText;
    };

    static bool hasFx(const Strand& s) {
        static const char* keys[] = {"text_animation", "typewriter", "fade_text", "text_reveal", "text_rotator", "text_transition", "text_shadow_animation", "text_rotate_to"};
        for (auto k : keys) if (s.attr(k)) return true;
        if (const Value* v = s.attr("text_rotate")) if (sq(v->asString()) == "spin") return true;
        return false;
    }
    // بعد التخطيط في كل relayout/tick. true => ما زال شيء يتحرّك (المضيف يواصل النداء).
    bool reduceMotion = false;   // ReduceMotion (إمكانية الوصول): بلا حركة نصية — يُعرض النص النهائي الثابت
    bool update(const StrandPtr& root, double nowMs) {
        frames_.clear(); present_.clear(); animating_ = false; now_ = nowMs;
        if (reduceMotion) return false;
        walk(root);
        for (auto it = st_.begin(); it != st_.end();) { if (!present_.count(it->first)) it = st_.erase(it); else ++it; }
        return animating_;
    }
    const Frame* frameOf(StrandId id) const { auto it = frames_.find(id); return it == frames_.end() ? nullptr : &it->second; }
    bool empty() const { return frames_.empty(); }
    bool animating() const { return animating_; }
    void reset() { st_.clear(); frames_.clear(); present_.clear(); animating_ = false; }
    // يعيد تشغيل حركة عنصر من البداية (يُستدعى من rin_indsin_session_play_text). true إن وُجد.
    bool restart(StrandId id, double nowMs) {
        auto it = st_.find(id); if (it == st_.end()) return false;
        it->second.startMs = nowMs; it->second.started = true; it->second.transStart = -1; return true;
    }
    std::vector<std::string> diagnostics;   // أخطاء إملائية في قيم السمات (يقرؤها المدقّق)

private:
    std::unordered_map<StrandId, State> st_; std::unordered_map<StrandId, Frame> frames_; std::unordered_set<StrandId> present_;
    bool animating_ = false; double now_ = 0;

    void walk(const StrandPtr& s) {
        if (!s) return;
        if (hasFx(*s)) { Frame f; if (evalStrand(*s, f) && f.any()) frames_[s->id] = std::move(f); present_.insert(s->id); }
        for (auto& c : s->children) walk(c);
    }
    // يطبّق مرحلة زمنية؛ يعيد true إن كانت فعّالة. يضبط animating_ حين لا تنتهي.
    bool phase(const Timing& tm, double startMs, double& ph, double& p, int* iterOut = nullptr, bool* finOut = nullptr) {
        double local = now_ - startMs - tm.delayMs; int iter = 0; bool fin = false;
        bool eff = fx::playPhase(tm, local, ph, iter, fin);
        if (!fin) animating_ = true;
        if (!eff) { ph = tm.dir == fx::Direction::REVERSE ? 1.0 : 0.0; }
        p = fx::applyEasing(tm.easing, ph);
        if (iterOut) *iterOut = iter; if (finOut) *finOut = fin;
        return eff;
    }
    static Timing baseTiming(double dur, double iter, const char* dir, const char* ease) {
        Timing t; t.durationMs = dur; t.iterations = iter; t.fill = fx::FILL_BOTH;
        fx::parseDirection(dir, t.dir); bool ok; t.easing = fx::parseEasing(ease, &ok);
        return t;
    }

    bool evalStrand(const Strand& s, Frame& f) {
        State& st = st_[s.id];
        std::string text = visibleText(s);
        std::string key = s.attrStr("text_key", "");
        bool restartOnChange = sq(s.attrStr("text_restart", "")) == "change";
        if (!st.started) { st.started = true; st.startMs = now_; st.key = key; st.lastText = text; st.seenText = true; }
        else {
            if (key != st.key) { st.key = key; st.startMs = now_; }
            if (restartOnChange && text != st.lastText) st.startMs = now_;
        }
        const double t0 = st.startMs;
        auto cps = font::decodeUtf8(text);
        bool cursive = typo::isCursiveScript(cps);
        bool rtl = font::isRtlText(text);
        double dist = numAttr(s, "text_distance", 24);

        // 1) TextAnimation: قائمة مسبقات
        std::vector<std::pair<const PresetInfo*, std::string>> presets;
        if (const Value* v = s.attr("text_animation"))
            for (auto& w : typo::words(v->asString())) {
                const PresetInfo* pi = findPreset(w);
                if (!pi) { diagnostics.push_back("text_animation: unknown preset '" + w + "'"); continue; }
                presets.push_back({pi, w});
            }
        // اختصارات: typewriter= · fade_text= · text_reveal= · text_shadow_animation= تُعامَل كمسبقات إضافية
        if (const Value* v = s.attr("typewriter")) { if (typo::truthy(v) || v->kind == Value::Kind::NUMBER || v->asNumber(0) > 0) presets.push_back({findPreset("typewriter"), "typewriter"}); }
        if (const Value* v = s.attr("fade_text")) {
            std::string q = sq(v->asString());
            const char* name = q == "out" || q == "fadeout" ? "fadeout" : q == "chars" || q == "char" ? "fadechars" : q == "words" || q == "word" ? "fadewords"
                             : q == "pulse" ? "pulse" : q == "inout" ? "pulse" : "fadein";
            presets.push_back({findPreset(name), name});
        }
        if (const Value* v = s.attr("text_reveal")) {
            std::string q = sq(v->asString());
            const char* name = q == "left" ? "revealleft" : q == "right" ? "revealright" : q == "up" || q == "top" ? "revealup" : q == "down" || q == "bottom" ? "revealdown"
                             : q == "center" || q == "centre" ? "revealcenter" : q == "chars" || q == "char" ? "revealchars" : q == "words" || q == "word" ? "revealwords" : "reveal";
            presets.push_back({findPreset(name), name});
        }
        if (const Value* v = s.attr("text_shadow_animation")) {
            std::string q = sq(v->asString());
            const PresetInfo* pi = findPreset(q);
            if (pi && isShadowPreset(pi->kind)) presets.push_back({pi, q});
            else if (q == "pulse" || q == "breathe" || q == "true" || q == "fromto") presets.push_back({findPreset("glow"), "glow"});
            else if ((q == "custom" || q == "from-to") && s.attr("text_shadow_to")) { /* يُنفَّذ في خطوة from→to أدناه */ }
            else diagnostics.push_back("text_shadow_animation: unknown preset '" + v->asString() + "'");
        }
        for (auto& pr : presets) {
            const PresetInfo* pi = pr.first; if (!pi) continue;
            std::string pref = pi->kind == P::TYPEWRITER ? "type" : (isShadowPreset(pi->kind) ? "text_shadow" : (pi->kind >= P::REVEAL_L && pi->kind <= P::REVEAL_WORDS ? "text_reveal" : (pi->kind <= P::FADE_WORDS ? "fade" : "text_anim")));
            Timing tm = baseTiming(pi->dur, pi->iter, pi->dir, pi->ease);
            double ph = 0, p = 0; int iter = 0; bool fin = false;
            if (pi->kind == P::TYPEWRITER) { evalTypewriter(s, cps, t0, f); continue; }
            tm = readTiming(s, {pref.c_str()}, tm);
            phase(tm, t0, ph, p, &iter, &fin);
            applyPreset(*pi, s, tm, ph, p, cursive, rtl, dist, cps, f);
        }
        // 2) TextRotate المتحرّك
        if (const Value* tv = s.attr("text_rotate_to")) {
            double from = 0; if (const Value* fv = s.attr("text_rotate")) { double d; if (typo::parseNum(fv->asString(), d)) from = d; }
            double to = tv->asNumber(0); Timing tm = readTiming(s, {"text_rotate"}, baseTiming(1200, 1, "normal", "easeinout"));
            double ph, p; phase(tm, t0, ph, p);
            f.rotate += (to - from) * p;   // from موجود أصلاً في rotateDeg الثابت؛ نضيف الفرق فقط
        } else if (const Value* rv = s.attr("text_rotate")) {
            if (sq(rv->asString()) == "spin") {
                Timing tm = readTiming(s, {"text_rotate"}, baseTiming(2000, -1, "normal", "linear"));
                double ph, p; phase(tm, t0, ph, p);
                f.rotate += numAttr(s, "text_angle", 360) * ph;
            }
        }
        // 3) Rotator (يستبدل النص) + 4) Transition (عند تغيّر النص)
        TK tk = TK::FADE;
        if (const Value* v = s.attr("text_rotator_effect")) { bool ok; tk = transitionKind(v->asString(), &ok); if (!ok) diagnostics.push_back("text_rotator_effect: unknown '" + v->asString() + "'"); }
        else if (const Value* v2 = s.attr("text_transition")) { bool ok; tk = transitionKind(v2->asString(), &ok); if (!ok) diagnostics.push_back("text_transition: unknown '" + v2->asString() + "'"); }
        double td = 400;
        { Timing tt = readTiming(s, {"text_transition", "text_rotator"}, baseTiming(400, 1, "normal", "easeinout")); td = std::max(1.0, tt.durationMs); }
        if (const Value* rv = s.attr("text_rotator")) {
            std::vector<std::string> items;
            std::string spec = rv->asString();
            auto parts = spec.find('|') != std::string::npos ? typo::splitTop(spec, '|') : typo::splitTop(spec, ',');
            for (auto& x : parts) items.push_back(typo::unquote(x));
            if (!items.empty()) {
                double hold = numAttr(s, "text_rotator_interval", 2000);
                bool loop = !(sq(s.attrStr("text_rotator_loop", "true")) == "false");
                double cycle = std::max(1.0, hold + td), delay = readTiming(s, {"text_rotator"}, Timing{}).delayMs;
                double local = now_ - t0 - delay;
                size_t n = items.size(); long idx = 0; double within = 0; bool done = false;
                if (local > 0) { idx = (long)std::floor(local / cycle); within = local - idx * cycle; }
                if (!loop && idx >= (long)n - 1) { idx = (long)n - 1; done = true; within = td; }
                size_t cur = (size_t)(idx % (long)n), prev = (size_t)((idx - 1 + (long)n) % (long)n);
                f.hasText = true; f.text = items[cur];
                if (!done) animating_ = true;
                if (idx > 0 && within < td && n > 1) { f.hasOut = false; Frame tmp; applyTransition(tk, within / td, dist, tmp); if (tmp.hasOut) { f.opacity *= tmp.opacity; f.dx += tmp.dx; f.dy += tmp.dy; f.inScale *= tmp.inScale; f.inRotate += tmp.inRotate; f.hasOut = true; f.outText = items[prev]; f.outOpacity = tmp.outOpacity; f.outDx = tmp.outDx; f.outDy = tmp.outDy; f.outScale = tmp.outScale; f.outRotate = tmp.outRotate; } }
            }
        } else if (s.attr("text_transition")) {
            if (st.seenText && text != st.lastText) { st.transStart = now_; st.outText = st.lastText; }
            st.lastText = text;
            if (st.transStart >= 0) {
                double e = (now_ - st.transStart) / td;
                if (e >= 1) st.transStart = -1;
                else { animating_ = true; Frame tmp; applyTransition(tk, e, dist, tmp); if (tmp.hasOut) { f.opacity *= tmp.opacity; f.dx += tmp.dx; f.dy += tmp.dy; f.inScale *= tmp.inScale; f.inRotate += tmp.inRotate; f.hasOut = true; f.outText = st.outText; f.outOpacity = tmp.outOpacity; f.outDx = tmp.outDx; f.outDy = tmp.outDy; f.outScale = tmp.outScale; f.outRotate = tmp.outRotate; } }
            }
        }
        st.lastText = text;
        // 5) ظل متحرّك مخصّص from→to
        if (s.attr("text_shadow_to") && s.attr("text_shadow_animation")) {
            std::vector<TextShadow> a = typo::parseTextShadows(s.attrStr("text_shadow", "")), b = typo::parseTextShadows(s.attrStr("text_shadow_to", ""));
            Timing tm = readTiming(s, {"text_shadow"}, baseTiming(1600, -1, "alternate", "easeinout"));
            double ph, p; phase(tm, t0, ph, p);
            f.hasShadow = true; f.shadows = lerpShadows(a, b, p);
        }
        return true;
    }

    void evalTypewriter(const Strand& s, const std::vector<uint32_t>& cps, double t0, Frame& f) {
        double n = (double)cps.size();
        double cps_ = std::max(1.0, numAttr(s, "type_speed", 30));
        if (const Value* v = s.attr("typewriter")) { if (v->kind == Value::Kind::NUMBER && v->number > 1) cps_ = v->number; else { double d = v->asNumber(0); if (d > 1) cps_ = d; } }
        double dur = s.attr("type_duration") ? numAttr(s, "type_duration", 0) : n / cps_ * 1000.0;
        double delay = numAttr(s, "type_delay", numAttr(s, "text_delay", 0));
        bool loop = typo::truthy(s.attr("type_loop")), erase = typo::truthy(s.attr("type_erase")) || loop;
        double hold = numAttr(s, "type_hold", 1500), eraseDur = erase ? std::max(1.0, numAttr(s, "type_erase_duration", dur * 0.5)) : 0;
        double local = now_ - t0 - delay;
        double blink = numAttr(s, "type_cursor_blink", 530);
        std::string cur;
        if (const Value* cv = s.attr("type_cursor")) { std::string c = cv->asString(); if (typo::truthy(cv) && sq(c) != "true") cur = c; else if (typo::truthy(cv)) cur = "|"; else if (sq(c) != "false" && sq(c) != "none" && !c.empty()) cur = c; }
        double k = n; bool typing = false;
        if (local < 0) { k = 0; animating_ = true; }
        else if (dur <= 0) k = n;
        else {
            double total = dur + (erase ? hold + eraseDur + (loop ? 400 : 0) : 0);
            double lt = local;
            if (loop && total > 0) lt = std::fmod(local, total);
            if (lt < dur) { k = std::floor(n * (lt / dur)); typing = true; animating_ = true; }
            else if (!erase) { k = n; }
            else if (lt < dur + hold) { k = n; animating_ = true; }
            else if (lt < dur + hold + eraseDur) { k = std::floor(n * (1.0 - (lt - dur - hold) / eraseDur)); typing = true; animating_ = true; }
            else { k = 0; animating_ = loop || lt < total; if (!loop) k = 0; }
            if (!loop && erase && local >= total - (loop ? 0 : 400)) { k = 0; }
            if (loop) animating_ = true;
        }
        f.revealChars = (int)std::max(0.0, std::min(n, k));
        if (!cur.empty()) {
            bool show = typing || blink <= 0 || ((long)std::floor((local < 0 ? 0 : local) / blink) % 2 == 0);
            f.cursor = show ? cur : std::string(cur.size() == 1 ? " " : " ");
            if (blink > 0 && !typing) animating_ = true;
        }
    }

    void applyPreset(const PresetInfo& pi, const Strand& s, const Timing& tm, double ph, double p, bool cursive, bool rtl, double dist, const std::vector<uint32_t>& cps, Frame& f) {
        auto setUnits = [&](int unit, int kind, double up, double spread) {
            if (f.unit) return;
            f.unit = (unit == 1 && cursive) ? 2 : unit; f.ukind = kind; f.up = up;
            double st = numAttr(s, "text_stagger", -1);
            size_t n = countUnits(cps, f.unit);
            f.uspread = (st >= 0 && tm.durationMs > 0 && n > 1) ? std::min(0.95, st * (double)(n - 1) / tm.durationMs) : spread;
        };
        switch (pi.kind) {
            case P::FADE_IN: f.opacity *= p; break;
            case P::FADE_OUT: f.opacity *= 1 - p; break;
            case P::FADE_CHARS: setUnits(1, 1, p, 0.7); break;
            case P::FADE_WORDS: setUnits(2, 1, p, 0.7); break;
            case P::SLIDE_UP: f.opacity *= p; f.dy += dist * (1 - p); break;
            case P::SLIDE_DOWN: f.opacity *= p; f.dy -= dist * (1 - p); break;
            case P::SLIDE_LEFT: f.opacity *= p; f.dx += dist * (1 - p); break;
            case P::SLIDE_RIGHT: f.opacity *= p; f.dx -= dist * (1 - p); break;
            case P::REVEAL_L: f.clip[2] = std::max(f.clip[2], 1 - p); break;
            case P::REVEAL_R: f.clip[0] = std::max(f.clip[0], 1 - p); break;
            case P::REVEAL_START: (rtl ? f.clip[0] : f.clip[2]) = std::max(rtl ? f.clip[0] : f.clip[2], 1 - p); break;
            case P::REVEAL_U: f.clip[1] = std::max(f.clip[1], 1 - p); break;
            case P::REVEAL_D: f.clip[3] = std::max(f.clip[3], 1 - p); break;
            case P::REVEAL_C: f.clip[0] = std::max(f.clip[0], (1 - p) / 2); f.clip[2] = std::max(f.clip[2], (1 - p) / 2); break;
            case P::REVEAL_CHARS: setUnits(1, 2, p, 0.9); break;
            case P::REVEAL_WORDS: setUnits(2, 2, p, 0.9); break;
            case P::ROTATE_IN: f.opacity *= p; f.rotate += -numAttr(s, "text_angle", 90) * (1 - p); break;
            case P::SPIN: f.rotate += numAttr(s, "text_angle", 360) * ph; break;
            case P::ZOOM_IN: f.opacity *= p; f.scale *= 0.6 + 0.4 * p; break;
            case P::ZOOM_OUT: f.opacity *= p; f.scale *= 1.5 - 0.5 * p; break;
            case P::BOUNCE: f.dy += -numAttr(s, "text_amplitude", 12) * std::fabs(std::sin(ph * kPi * 3)) * (1 - ph); break;
            case P::SHAKE: f.dx += numAttr(s, "text_amplitude", 6) * std::sin(ph * kPi * 10) * (1 - ph); break;
            case P::WAVE: setUnits(1, 3, ph, 0.12); f.uamp = numAttr(s, "text_amplitude", 6); break;
            case P::BLINK: f.opacity *= ph < 0.5 ? 1.0 : 0.0; break;
            case P::PULSE: f.opacity *= 0.4 + 0.6 * p; break;
            case P::RAINBOW: f.hasColor = true; f.color = hueColor(ph * 360.0); break;
            case P::GLOW: case P::NEON: case P::FLICKER: case P::ORBIT: case P::DROP: {
                Color c = themeRegistry().active().primary;
                if (const Value* cv = s.attr("text_shadow_color")) typo::parseColorToken(cv->asString(), c);
                else { auto base = typo::parseTextShadows(s.attrStr("text_shadow", "")); if (!base.empty()) c = base[0].color; }
                f.hasShadow = true;
                f.shadows = shadowPreset(pi.kind, ph, p, std::max(0.0, (now_ - st_[s.id].startMs - tm.delayMs)), c, numAttr(s, "text_amplitude", 4));
                break;
            }
            default: break;
        }
    }
};

// ---- تطبيق الإطارات على DrawList -----------------------------------------------------------------------------------
inline std::vector<GlyphFx> unitEffects(const Frame& f, size_t n) {
    std::vector<GlyphFx> v(n);
    for (size_t i = 0; i < n; i++) {
        double pos = n > 1 ? (double)i / (double)(n - 1) : 0.0;
        if (f.ukind == 3) { v[i].dy = (float)(-f.uamp * std::sin(2 * kPi * (f.up - (double)i * f.uspread))); continue; }
        double start = f.uspread * pos, span = std::max(1e-6, 1.0 - f.uspread);
        double local = std::max(0.0, std::min(1.0, (f.up - start) / span));
        if (f.ukind == 2) local = local < 0.15 ? local / 0.15 : 1.0;  // كشف شبه فوري لكل وحدة
        v[i].alpha = (float)local;
        if (f.ukind == 1) v[i].dy = (float)((1.0 - local) * 4.0);
    }
    return v;
}
inline void applyTextFxToDrawList(DrawList& list, const TextFxRuntime& rt) {
    if (rt.empty()) return;
    DrawList out; out.reserve(list.size() + 4);
    auto scaleA = [](Color& c, double k) { c.a = (unsigned char)std::lround(std::max(0.0, std::min(255.0, c.a * k))); };
    auto xform = [&](DrawCommand& c, double dx, double dy, double sc, double rot, double op) {
        double cx = c.bounds.x + c.bounds.w / 2, cy = c.bounds.y + c.bounds.h / 2;
        if (sc != 1.0) { c.bounds.w *= sc; c.bounds.h *= sc; c.bounds.x = cx - c.bounds.w / 2; c.bounds.y = cy - c.bounds.h / 2; c.fontSize *= sc; }
        c.bounds.x += dx; c.bounds.y += dy; c.rotateDeg += rot;
        if (op < 0.9999) { scaleA(c.color, op); for (auto& sh : c.textShadows) scaleA(sh.color, op); }
    };
    for (auto& cmd : list) {
        const Frame* f = cmd.op == DrawOp::TEXT_RUN ? rt.frameOf(cmd.owner) : nullptr;
        if (!f) { out.push_back(std::move(cmd)); continue; }
        DrawCommand c = cmd;
        if (f->hasText) { c.text = f->text; if (!c.textTransform.empty()) c.text = typo::applyTextTransform(c.text, c.textTransform, c.lang); }
        auto cps = font::decodeUtf8(c.text);
        if (f->revealChars >= 0) {
            size_t k = std::min((size_t)f->revealChars, cps.size());
            c.text = typo::encodeUtf8(cps, 0, k) + f->cursor;
            cps = font::decodeUtf8(c.text);
        }
        if (f->unit) {
            size_t n = countUnits(cps, f->unit);
            c.glyphUnit = f->unit; c.glyphFx = unitEffects(*f, n);
        }
        for (int i = 0; i < 4; i++) { double* dst = i == 0 ? &c.clipL : i == 1 ? &c.clipT : i == 2 ? &c.clipR : &c.clipB; *dst = std::max(*dst, f->clip[i]); }
        if (f->hasShadow) c.textShadows = f->shadows;
        if (f->hasColor) { Color k = typo::lerpC(c.color, f->color, 1.0); k.a = c.color.a; c.color = k; }
        if (f->hasOut) {
            DrawCommand o = cmd;
            o.text = f->outText;
            if (!o.textTransform.empty()) o.text = typo::applyTextTransform(o.text, o.textTransform, o.lang);
            if (f->hasShadow) o.textShadows = f->shadows;
            xform(o, f->outDx, f->outDy, f->outScale, f->outRotate, f->outOpacity);
            if (o.text.empty()) { /* لا شيء يُرسم */ } else out.push_back(std::move(o));
        }
        xform(c, f->dx, f->dy, f->scale * f->inScale, f->rotate + f->inRotate, f->opacity);
        out.push_back(std::move(c));
    }
    list.swap(out);
}

} // namespace textfx

using textfx::TextFxRuntime;
using textfx::applyTextFxToDrawList;

} // namespace indsin
