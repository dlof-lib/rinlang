// indsin/rin_indsin_effects.h — Effects Engine: enter/transition animations for a Strand (fade,
// scale, slide-in from each direction), driven by a per-session animation clock (EffectRuntime).
//
// Deliberately reuses the existing `opacity=` attribute mechanism (see resolveColor() in
// rin_indsin_paint.h, which already composes a Strand's own opacity= into its resolved fill alpha)
// instead of inventing a parallel paint-time transform pipeline: applyEffectsToFabric() below
// mutates each animating Strand's resolved `opacity` attr and its own `geometry` (a plain
// translate/scale, applied in place after layout) directly on the Fabric tree, so the very same
// Dye::paint()/paintWithOverlay() call every session already makes (rin_indsin_c_api.cpp's
// fabricEnvelope()) picks the current animation frame up for free — no new code in
// rin_indsin_paint.h, no new field in the "paint" JSON the Kotlin/WASM/PNG-export consumers already
// read.
//
// A Strand animates when it (or the nearest ancestor that sets one) carries `effect=`:
//   effect="fade"                         -- opacity 0 -> 1
//   effect="scale" (or "zoom")            -- opacity 0 -> 1, plus a small scale-up (0.85 -> 1.0)
//   effect="slideUp"/"slideDown"/
//          "slideLeft"/"slideRight"       -- opacity 0 -> 1, plus a 24px slide-in from that side
// optional alongside it: duration="250" (ms), easing="easeOut" (default; also linear/easeIn/
// easeInOut), delay="0" (ms, held at the start-of-animation frame before it begins).
//
// The animation starts the first tick a Strand is seen carrying an effect (EffectRuntime::mark);
// a Strand that stops appearing in the Fabric (removed by an `if`, a Dialog closing, etc.) has its
// entry cleared, so it animates in again the next time it (re)appears — a real enter transition,
// not a one-shot-per-process fade.
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h" // resolveDurationToken
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace indsin {

// Design System v2 added PULSE/SHAKE/BOUNCE/GROW at the END so existing values never shift:
//   pulse  -- a single soft scale-up-and-back "attention" beat (opacity untouched)
//   shake  -- a damped horizontal wobble (e.g. a rejected form field)
//   bounce -- a damped vertical hop
//   grow   -- opacity 0 -> 1 AND scale 0 -> 1 about the centre (a "pop-in")
enum class EffectKind { NONE, FADE, SCALE, SLIDE_UP, SLIDE_DOWN, SLIDE_LEFT, SLIDE_RIGHT, PULSE, SHAKE, BOUNCE, GROW };
// The original four curves keep their values; v2 appends cubic/sine/back/bounce/elastic/spring.
// BACK/ELASTIC/SPRING deliberately overshoot 1.0 mid-curve (that is the effect); evaluateEffect()
// clamps any *opacity* it derives from them back into [0,1].
enum class Easing { LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT,
                    CUBIC_IN, CUBIC_OUT, CUBIC_IN_OUT, SINE_IN_OUT, BACK_OUT, BOUNCE_OUT, ELASTIC_OUT, SPRING };

inline EffectKind effectKindFromString(const std::string& s) {
    if (s == "fade") return EffectKind::FADE;
    if (s == "scale" || s == "zoom") return EffectKind::SCALE;
    if (s == "slideUp" || s == "slide-up" || s == "slideup") return EffectKind::SLIDE_UP;
    if (s == "slideDown" || s == "slide-down" || s == "slidedown") return EffectKind::SLIDE_DOWN;
    if (s == "slideLeft" || s == "slide-left" || s == "slideleft") return EffectKind::SLIDE_LEFT;
    if (s == "slideRight" || s == "slide-right" || s == "slideright") return EffectKind::SLIDE_RIGHT;
    if (s == "pulse") return EffectKind::PULSE;
    if (s == "shake") return EffectKind::SHAKE;
    if (s == "bounce") return EffectKind::BOUNCE;
    if (s == "grow" || s == "pop") return EffectKind::GROW;
    return EffectKind::NONE; // "none", "", or anything unrecognized -> no effect (safe default)
}
inline const char* effectKindName(EffectKind k) {
    switch (k) {
        case EffectKind::FADE: return "fade";
        case EffectKind::SCALE: return "scale";
        case EffectKind::SLIDE_UP: return "slideUp";
        case EffectKind::SLIDE_DOWN: return "slideDown";
        case EffectKind::SLIDE_LEFT: return "slideLeft";
        case EffectKind::SLIDE_RIGHT: return "slideRight";
        case EffectKind::PULSE: return "pulse";
        case EffectKind::SHAKE: return "shake";
        case EffectKind::BOUNCE: return "bounce";
        case EffectKind::GROW: return "grow";
        default: return "none";
    }
}
inline Easing easingFromString(const std::string& s) {
    if (s == "linear") return Easing::LINEAR;
    if (s == "easeIn" || s == "ease-in") return Easing::EASE_IN;
    if (s == "easeInOut" || s == "ease-in-out") return Easing::EASE_IN_OUT;
    if (s == "cubicIn" || s == "cubic-in") return Easing::CUBIC_IN;
    if (s == "cubicOut" || s == "cubic-out") return Easing::CUBIC_OUT;
    if (s == "cubicInOut" || s == "cubic-in-out") return Easing::CUBIC_IN_OUT;
    if (s == "sine" || s == "sineInOut" || s == "sine-in-out") return Easing::SINE_IN_OUT;
    if (s == "back" || s == "backOut" || s == "back-out") return Easing::BACK_OUT;
    if (s == "bounce" || s == "bounceOut" || s == "bounce-out") return Easing::BOUNCE_OUT;
    if (s == "elastic" || s == "elasticOut" || s == "elastic-out") return Easing::ELASTIC_OUT;
    if (s == "spring") return Easing::SPRING;
    return Easing::EASE_OUT; // default, incl. "easeOut"/unset -- the most natural-feeling default
}
// Standard quadratic easing curves -- clamps `t` to [0,1] first so a caller never has to.
inline double applyEasing(Easing e, double t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    switch (e) {
        case Easing::LINEAR: return t;
        case Easing::EASE_IN: return t * t;
        case Easing::EASE_OUT: return 1.0 - (1.0 - t) * (1.0 - t);
        case Easing::EASE_IN_OUT: return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2) / 2.0;
        case Easing::CUBIC_IN:  return t * t * t;
        case Easing::CUBIC_OUT: return 1.0 - std::pow(1.0 - t, 3);
        case Easing::CUBIC_IN_OUT: return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3) / 2.0;
        case Easing::SINE_IN_OUT: return -(std::cos(3.14159265358979323846 * t) - 1.0) / 2.0;
        case Easing::BACK_OUT: { // overshoots ~10% then settles (easings.net "easeOutBack")
            const double c1 = 1.70158, c3 = c1 + 1.0;
            return 1.0 + c3 * std::pow(t - 1.0, 3) + c1 * std::pow(t - 1.0, 2);
        }
        case Easing::BOUNCE_OUT: { // easings.net "easeOutBounce"
            const double n1 = 7.5625, d1 = 2.75;
            if (t < 1.0 / d1) return n1 * t * t;
            if (t < 2.0 / d1) { t -= 1.5 / d1;   return n1 * t * t + 0.75; }
            if (t < 2.5 / d1) { t -= 2.25 / d1;  return n1 * t * t + 0.9375; }
            t -= 2.625 / d1; return n1 * t * t + 0.984375;
        }
        case Easing::ELASTIC_OUT: { // easings.net "easeOutElastic"
            if (t <= 0.0) return 0.0;
            if (t >= 1.0) return 1.0;
            const double c4 = (2.0 * 3.14159265358979323846) / 3.0;
            return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
        }
        case Easing::SPRING: { // damped spring: settles at 1.0, small overshoot, exact at both ends
            if (t >= 1.0) return 1.0;
            return 1.0 - std::exp(-6.0 * t) * std::cos(10.0 * t);
        }
    }
    return t;
}

struct EffectSpec {
    EffectKind kind = EffectKind::NONE;
    Easing easing = Easing::EASE_OUT;
    double durationMs = 250;
    double delayMs = 0;
    // `stagger=<ms>` on a CONTAINER: its direct children start their (inherited or own) effect
    // i*stagger ms later than the first -- the classic cascading list/grid reveal. Read from the
    // container's own attr by collectAndApplyEffects(); never inherited by grandchildren.
    double staggerMs = 0;
};
// Reads a Strand's OWN effect=/easing=/duration=/delay= attrs -- does not consider inheritance
// from an ancestor; see applyEffectsToFabric() below for that (a Card carrying effect="fade" once
// so its children don't each have to repeat it).
inline EffectSpec effectSpecOf(const Strand& s) {
    EffectSpec spec;
    spec.kind = effectKindFromString(s.attrStr("effect", "none"));
    spec.easing = easingFromString(s.attrStr("easing", "easeOut"));
    spec.durationMs = 250;
    if (const Value* d = s.attr("duration")) {
        double tok;
        if (d->kind == Value::Kind::NUMBER) spec.durationMs = d->number;
        else if (resolveDurationToken(d->str, tok)) spec.durationMs = tok; // duration="fast"/"slow"/...
        else spec.durationMs = d->asNumber(250);
    }
    spec.staggerMs = s.attrNum("stagger", 0);
    spec.delayMs = s.attrNum("delay", 0);
    return spec;
}

// One animation frame's worth of visual adjustment: an opacity multiplier (composed with
// whatever opacity= the Strand already had) and a translate/scale to apply to its geometry.
struct EffectFrame {
    double opacity = 1.0;
    double dx = 0, dy = 0; // px, applied as a straight translate of the Strand's own geometry
    double scale = 1.0;    // uniform, about the Strand's own center
};
// `rawT` is un-eased progress in [0,1] (0 = just started, 1 = fully settled) -- easing is applied
// here so every call site (applyEffectsToFabric below, and the standalone test tool) gets it for
// free rather than having to remember to call applyEasing() itself.
inline EffectFrame evaluateEffect(const EffectSpec& spec, double rawT) {
    EffectFrame f;
    if (spec.kind == EffectKind::NONE) return f;
    double t = applyEasing(spec.easing, rawT);
    static const double kSlideDistance = 24.0; // px -- a small, readable slide-in, not a full-screen swipe
    switch (spec.kind) {
        case EffectKind::FADE:       f.opacity = t; break;
        case EffectKind::SCALE:      f.opacity = t; f.scale = 0.85 + 0.15 * t; break;
        case EffectKind::SLIDE_UP:   f.opacity = t; f.dy = (1.0 - t) * kSlideDistance; break;
        case EffectKind::SLIDE_DOWN: f.opacity = t; f.dy = -(1.0 - t) * kSlideDistance; break;
        case EffectKind::SLIDE_LEFT: f.opacity = t; f.dx = (1.0 - t) * kSlideDistance; break;
        case EffectKind::SLIDE_RIGHT:f.opacity = t; f.dx = -(1.0 - t) * kSlideDistance; break;
        case EffectKind::PULSE:  f.scale = 1.0 + 0.08 * std::sin(3.14159265358979323846 * (rawT < 0 ? 0 : (rawT > 1 ? 1 : rawT))); break;
        case EffectKind::SHAKE: {
            double r = rawT < 0 ? 0 : (rawT > 1 ? 1 : rawT);
            f.dx = std::sin(r * 3.14159265358979323846 * 6.0) * (1.0 - r) * 8.0;
            break;
        }
        case EffectKind::BOUNCE: {
            double r = rawT < 0 ? 0 : (rawT > 1 ? 1 : rawT);
            f.dy = -std::fabs(std::sin(r * 3.14159265358979323846 * 3.0)) * (1.0 - r) * 16.0;
            break;
        }
        case EffectKind::GROW: f.opacity = t; f.scale = t < 0.01 ? 0.01 : t; break;
        default: break;
    }
    // Overshooting easings (back/elastic/spring) may push t outside [0,1]; opacity must not follow.
    if (f.opacity < 0) f.opacity = 0;
    if (f.opacity > 1) f.opacity = 1;
    return f;
}

// Per-session animation clock: remembers, per Strand id, WHEN its current enter-animation began
// (plus the duration/delay it began with) so paint-time can compute elapsed-since-start without
// the caller having to track timestamps itself. A Strand not currently tracked (never marked, or
// cleared because it's no longer in the Fabric) reads back as progress()==1.0 -- fully settled,
// no animation -- which is also the correct answer for a Strand that has no effect= at all.
class EffectRuntime {
public:
    struct AnimState { double startMs; double durationMs; double delayMs; double baseOpacity; };

    // Called once per Strand per tick while it's present in the Fabric and animating. The first
    // call for a given id records `nowMs` as that Strand's animation start AND snapshots
    // `baseOpacityIfNew` as its author-authored opacity (before this engine ever touches it) --
    // subsequent calls only refresh duration/delay (in case the source changed them mid-animation;
    // the start time and the original opacity snapshot never change while the Strand keeps
    // appearing). This snapshot matters because applyEffectsToFabric() below overwrites the same
    // Strand's `opacity` attr every tick with the current animated value -- without remembering the
    // ORIGINAL value separately, tick 2 would read back tick 1's already-animated opacity as if it
    // were the author's base and compound the fade into itself.
    void mark(StrandId id, double nowMs, double durationMs, double delayMs, double baseOpacityIfNew) {
        auto it = states_.find(id);
        if (it == states_.end()) states_[id] = AnimState{nowMs, durationMs, delayMs, baseOpacityIfNew};
        else { it->second.durationMs = durationMs; it->second.delayMs = delayMs; }
    }
    double baseOpacityOf(StrandId id) const {
        auto it = states_.find(id);
        return it == states_.end() ? 1.0 : it->second.baseOpacity;
    }
    // Drops every tracked id NOT in `stillPresent` -- called once per full-tree tick (see
    // applyEffectsToFabric) so a Strand that disappeared (an `if` branch closed, a Dialog
    // dismissed, ...) starts a fresh animation the next time it reappears, instead of resuming
    // mid-fade from whenever it was last seen.
    void clearMissing(const std::unordered_set<StrandId>& stillPresent) {
        for (auto it = states_.begin(); it != states_.end(); ) {
            if (!stillPresent.count(it->first)) it = states_.erase(it); else ++it;
        }
    }
    double progress(StrandId id, double nowMs) const {
        auto it = states_.find(id);
        if (it == states_.end()) return 1.0; // not tracked -> settled
        double elapsed = nowMs - it->second.startMs - it->second.delayMs;
        if (elapsed <= 0) return 0.0;
        if (it->second.durationMs <= 0) return 1.0;
        double t = elapsed / it->second.durationMs;
        return t > 1.0 ? 1.0 : t;
    }
    bool anyAnimating(double nowMs) const {
        for (auto& kv : states_) if (progress(kv.first, nowMs) < 1.0) return true;
        return false;
    }
    void reset() { states_.clear(); }
private:
    std::unordered_map<StrandId, AnimState> states_;
};

// Walks the whole Fabric once: for every Strand whose OWN effect= is set, or that inherits one
// from the nearest ancestor that sets it (so `@view.Card=... effect="fade"; ... .end/view` animates
// every child in together without repeating effect= on each), marks it in `rt`, computes this
// tick's EffectFrame from its progress, and applies it -- opacity composed into the Strand's
// existing `opacity` attr (added if it didn't have one), geometry translated/scaled in place.
// Call AFTER layout (and after the Overlay Engine's re-home pass, if any) so the geometry being
// adjusted is final for this frame; call BEFORE Dye::paint()/paintWithOverlay() so the adjustment
// is what actually gets painted. Returns true if anything is still short of its full duration
// (i.e. the host should keep ticking -- see rin_indsin_session_tick in rin_indsin_c_api.cpp).
inline void collectAndApplyEffects(const StrandPtr& s, EffectRuntime& rt, double nowMs,
                                    std::unordered_set<StrandId>& present, EffectSpec inherited,
                                    double extraDelayMs = 0) {
    if (!s) return;
    EffectSpec own = effectSpecOf(*s);
    EffectSpec effective = (own.kind != EffectKind::NONE) ? own : inherited;

    if (effective.kind != EffectKind::NONE) {
        present.insert(s->id);
        double currentAttrOpacity = s->attrNum("opacity", 1.0); // only meaningful the FIRST tick; see mark()'s doc comment
        rt.mark(s->id, nowMs, effective.durationMs, effective.delayMs + extraDelayMs, currentAttrOpacity);
        double rawT = rt.progress(s->id, nowMs);
        EffectFrame frame = evaluateEffect(effective, rawT);

        double baseOpacity = rt.baseOpacityOf(s->id); // the ORIGINAL author-set opacity, stable across ticks
        double finalOpacity = baseOpacity * frame.opacity;
        bool replaced = false;
        for (auto& a : s->attrs) {
            if (a.key == "opacity") { a.value = Value::num(finalOpacity); a.rawExpr = nullptr; replaced = true; break; }
        }
        if (!replaced) s->attrs.push_back(ResolvedAttr{"opacity", nullptr, Value::num(finalOpacity)});

        s->geometry.x += frame.dx; s->geometry.y += frame.dy;
        s->geometryOuter.x += frame.dx; s->geometryOuter.y += frame.dy;
        if (frame.scale != 1.0) {
            double nw = s->geometry.w * frame.scale, nh = s->geometry.h * frame.scale;
            s->geometry.x -= (nw - s->geometry.w) / 2.0;
            s->geometry.y -= (nh - s->geometry.h) / 2.0;
            s->geometry.w = nw; s->geometry.h = nh;
        }
    }
    double stagger = own.staggerMs; // this container's own stagger= (not inherited)
    int idx = 0;
    for (auto& c : s->children) collectAndApplyEffects(c, rt, nowMs, present, effective, stagger * idx++);
}
inline bool applyEffectsToFabric(const StrandPtr& root, EffectRuntime& rt, double nowMs) {
    std::unordered_set<StrandId> present;
    collectAndApplyEffects(root, rt, nowMs, present, EffectSpec{});
    rt.clearMissing(present);
    return rt.anyAnimating(nowMs);
}

} // namespace indsin
