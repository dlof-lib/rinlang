// tools/test_indsin_events_effects.cpp — tests for the Events & Effects features added to Indsin UI:
//   - Needle's new gesture dispatchers (rin_indsin_needle.h): dispatchLongPress/dispatchDoubleTap
//     for onLongPress=/onDoubleTap=, and dispatchHover for onHoverEnter=/onHoverExit= -- all three
//     share the exact handler-execution engine (real `fun`, or a built-in Action) dispatchTap's
//     onTap= already used, verified here via the same real-`fun`/built-in-Action/disabled-Strand
//     rules dispatchTap already has tests for elsewhere.
//   - The Effects Engine (rin_indsin_effects.h): easing curves, per-kind EffectFrame math (fade/
//     scale/slide*), and EffectRuntime's animation clock (starts on first mark(), holds delay=,
//     reaches 1.0 at duration=, resets when a Strand stops appearing).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iindsin ../../../../tools/test_indsin_events_effects.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp \
//       diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp \
//       diagnostics/source_manager.cpp -lz -o test_indsin_events_effects
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include "rin_indsin_effects.h"
#include <cassert>
#include <iostream>
#include <cmath>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static indsin::StrandPtr byName(const indsin::StrandPtr& root, const std::string& n) {
    return indsin::findAny(root, [&](const indsin::StrandPtr& s){ return s->name == n; });
}
static void layoutAt(indsin::PipelineResult& r, double width = 1200) {
    indsin::Indsin engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}
static bool nearlyEq(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    // 1. onLongPress: real `fun` handler --------------------------------------------------------
    {
        std::cout << "-- dispatchLongPress: real fun handler --\n";
        std::string src = R"(
warp count = 0;
fun bump() { count = count + 1; }
@view.Column=root
  @view.Card=card w="200"; h="80"; onLongPress=bump(); .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        layoutAt(r);
        auto card = byName(r.fabric, "card");
        CHECK(card != nullptr, "card found");

        auto res = indsin::dispatchLongPress(r.fabric, r.warp, r.program, card->geometry.x + 5, card->geometry.y + 5);
        CHECK(res.handled, "long-press on the card is handled");
        CHECK(res.error.empty(), "no runtime error");
        CHECK(r.warp.get("count").asNumber(-1) == 1, "bump() ran for real (count == 1)");

        // A tap at the same point must NOT run onLongPress's handler -- it has no onTap=.
        auto tapRes = indsin::dispatchTap(r.fabric, r.warp, r.program, card->geometry.x + 5, card->geometry.y + 5);
        CHECK(!tapRes.handled, "a plain tap on a Strand with only onLongPress= is unhandled");
        CHECK(r.warp.get("count").asNumber(-1) == 1, "count unchanged by the tap");
    }

    // 2. onDoubleTap: built-in Action (no `fun` involved) ----------------------------------------
    {
        std::cout << "-- dispatchDoubleTap: built-in increment() action --\n";
        std::string src = R"(
warp likes = 0;
@view.Button=btn label="Like"; onDoubleTap=increment(likes); .end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        layoutAt(r);
        auto btn = byName(r.fabric, "btn");
        auto res = indsin::dispatchDoubleTap(r.fabric, r.warp, r.program, btn->geometry.x + 2, btn->geometry.y + 2);
        CHECK(res.handled && res.error.empty(), "double-tap dispatches without error");
        CHECK(r.warp.get("likes").asNumber(-1) == 1, "increment() applied via the Action Engine");
    }

    // 3. onHoverEnter/onHoverExit ------------------------------------------------------------------
    {
        std::cout << "-- dispatchHover: onHoverEnter/onHoverExit --\n";
        std::string src = R"(
warp hovered = "false";
fun enter() { hovered = "true"; }
fun leave() { hovered = "false"; }
@view.Card=card w="150"; h="60"; onHoverEnter=enter(); onHoverExit=leave(); .end/view
)";
        auto r = indsin::runColdPipeline(src);
        layoutAt(r);
        auto card = byName(r.fabric, "card");
        double px = card->geometry.x + 3, py = card->geometry.y + 3;

        auto enterRes = indsin::dispatchHover(r.fabric, r.warp, r.program, px, py, /*entering=*/true);
        CHECK(enterRes.handled, "hover-enter handled");
        CHECK(r.warp.get("hovered").asString() == "true", "onHoverEnter ran");

        auto exitRes = indsin::dispatchHover(r.fabric, r.warp, r.program, px, py, /*entering=*/false);
        CHECK(exitRes.handled, "hover-exit handled");
        CHECK(r.warp.get("hovered").asString() == "false", "onHoverExit ran");
    }

    // 4. Disabled Strand consumes no gesture (same rule dispatchTap already enforces) -------------
    {
        std::cout << "-- dispatchLongPress respects state=disabled --\n";
        std::string src = R"(
warp fired = "false";
fun mark() { fired = "true"; }
@view.Button=btn label="X"; state="disabled"; onLongPress=mark(); .end/view
)";
        auto r = indsin::runColdPipeline(src);
        layoutAt(r);
        auto btn = byName(r.fabric, "btn");
        auto res = indsin::dispatchLongPress(r.fabric, r.warp, r.program, btn->geometry.x + 1, btn->geometry.y + 1);
        CHECK(!res.handled, "disabled Strand's onLongPress is never run");
        CHECK(r.warp.get("fired").asString() == "false", "handler did not fire");
    }

    // 5. Easing curves --------------------------------------------------------------------------
    {
        std::cout << "-- easing curves --\n";
        CHECK(nearlyEq(indsin::applyEasing(indsin::Easing::LINEAR, 0.5), 0.5), "linear(0.5) == 0.5");
        CHECK(nearlyEq(indsin::applyEasing(indsin::Easing::EASE_IN, 0.5), 0.25), "easeIn(0.5) == 0.25 (t*t)");
        CHECK(nearlyEq(indsin::applyEasing(indsin::Easing::EASE_OUT, 0.5), 0.75), "easeOut(0.5) == 0.75");
        CHECK(nearlyEq(indsin::applyEasing(indsin::Easing::EASE_IN_OUT, 0.25), 0.125), "easeInOut(0.25) == 0.125");
        CHECK(indsin::applyEasing(indsin::Easing::LINEAR, -1) == 0.0, "clamps below 0");
        CHECK(indsin::applyEasing(indsin::Easing::LINEAR, 2) == 1.0, "clamps above 1");
        for (double t : {0.0, 0.3, 0.7, 1.0}) {
            for (auto e : {indsin::Easing::LINEAR, indsin::Easing::EASE_IN, indsin::Easing::EASE_OUT, indsin::Easing::EASE_IN_OUT}) {
                double v = indsin::applyEasing(e, t);
                if (v < -1e-9 || v > 1 + 1e-9) { CHECK(false, "easing stays within [0,1]"); }
            }
        }
        CHECK(true, "all easing curves stay within [0,1] across sampled t");
    }

    // 6. EffectFrame per kind ---------------------------------------------------------------------
    {
        std::cout << "-- evaluateEffect: fade/scale/slide* --\n";
        indsin::EffectSpec spec;
        spec.easing = indsin::Easing::LINEAR; // isolate the per-kind math from easing curvature

        spec.kind = indsin::EffectKind::FADE;
        auto f0 = indsin::evaluateEffect(spec, 0.0), f1 = indsin::evaluateEffect(spec, 1.0);
        CHECK(nearlyEq(f0.opacity, 0.0) && nearlyEq(f1.opacity, 1.0), "fade: opacity 0 -> 1");
        CHECK(nearlyEq(f0.dx, 0) && nearlyEq(f0.dy, 0) && nearlyEq(f0.scale, 1.0), "fade: no translate/scale");

        spec.kind = indsin::EffectKind::SCALE;
        auto s0 = indsin::evaluateEffect(spec, 0.0), s1 = indsin::evaluateEffect(spec, 1.0);
        CHECK(nearlyEq(s0.scale, 0.85) && nearlyEq(s1.scale, 1.0), "scale: 0.85 -> 1.0");
        CHECK(nearlyEq(s0.opacity, 0.0) && nearlyEq(s1.opacity, 1.0), "scale: opacity fades in too");

        spec.kind = indsin::EffectKind::SLIDE_UP;
        auto u0 = indsin::evaluateEffect(spec, 0.0), u1 = indsin::evaluateEffect(spec, 1.0);
        CHECK(u0.dy > 0 && nearlyEq(u1.dy, 0.0), "slideUp: starts below (positive dy), ends at 0");

        spec.kind = indsin::EffectKind::SLIDE_DOWN;
        CHECK(indsin::evaluateEffect(spec, 0.0).dy < 0, "slideDown: starts above (negative dy)");

        spec.kind = indsin::EffectKind::SLIDE_LEFT;
        CHECK(indsin::evaluateEffect(spec, 0.0).dx > 0, "slideLeft: starts to the right (positive dx)");

        spec.kind = indsin::EffectKind::SLIDE_RIGHT;
        CHECK(indsin::evaluateEffect(spec, 0.0).dx < 0, "slideRight: starts to the left (negative dx)");

        spec.kind = indsin::EffectKind::NONE;
        auto n = indsin::evaluateEffect(spec, 0.3);
        CHECK(nearlyEq(n.opacity, 1.0) && nearlyEq(n.dx, 0) && nearlyEq(n.dy, 0) && nearlyEq(n.scale, 1.0),
              "NONE: identity frame regardless of t");
    }

    // 7. effectKindFromString / easingFromString parsing -----------------------------------------
    {
        std::cout << "-- attribute-string parsing --\n";
        CHECK(indsin::effectKindFromString("fade") == indsin::EffectKind::FADE, "\"fade\"");
        CHECK(indsin::effectKindFromString("zoom") == indsin::EffectKind::SCALE, "\"zoom\" aliases scale");
        CHECK(indsin::effectKindFromString("slideUp") == indsin::EffectKind::SLIDE_UP, "\"slideUp\"");
        CHECK(indsin::effectKindFromString("slide-left") == indsin::EffectKind::SLIDE_LEFT, "\"slide-left\" (dash form)");
        CHECK(indsin::effectKindFromString("bogus") == indsin::EffectKind::NONE, "unknown -> NONE (safe default)");
        CHECK(indsin::effectKindFromString("") == indsin::EffectKind::NONE, "empty -> NONE");
        CHECK(indsin::easingFromString("linear") == indsin::Easing::LINEAR, "\"linear\"");
        CHECK(indsin::easingFromString("easeIn") == indsin::Easing::EASE_IN, "\"easeIn\"");
        CHECK(indsin::easingFromString("bogus") == indsin::Easing::EASE_OUT, "unknown easing -> EASE_OUT default");
    }

    // 8. EffectRuntime animation clock -------------------------------------------------------------
    {
        std::cout << "-- EffectRuntime: progress over time, delay, reset on disappearance --\n";
        indsin::EffectRuntime rt;
        indsin::StrandId id = 42;

        CHECK(nearlyEq(rt.progress(id, 1000.0), 1.0), "an untracked id reads as fully settled (1.0)");

        rt.mark(id, 1000.0, /*durationMs=*/200, /*delayMs=*/0, /*baseOpacity=*/1.0);
        CHECK(nearlyEq(rt.progress(id, 1000.0), 0.0), "progress == 0 at the exact start");
        CHECK(nearlyEq(rt.progress(id, 1100.0), 0.5), "progress == 0.5 halfway through duration");
        CHECK(nearlyEq(rt.progress(id, 1200.0), 1.0), "progress == 1.0 exactly at duration");
        CHECK(nearlyEq(rt.progress(id, 5000.0), 1.0), "progress clamps at 1.0 well past duration");
        CHECK(rt.anyAnimating(1050.0), "anyAnimating() true mid-animation");
        CHECK(!rt.anyAnimating(1300.0), "anyAnimating() false once settled");

        // Re-marking the SAME id doesn't restart the clock (only duration/delay refresh) --
        // matches "an edit doesn't restart an in-flight animation" from the header's own comment.
        rt.mark(id, 1150.0, 200, 0, 1.0);
        CHECK(nearlyEq(rt.progress(id, 1200.0), 1.0), "re-mark mid-flight keeps the original start time");

        // delay: held at 0 progress until delayMs elapses, THEN counts up over duration.
        indsin::EffectRuntime rt2;
        indsin::StrandId id2 = 7;
        rt2.mark(id2, 0.0, /*durationMs=*/100, /*delayMs=*/50, /*baseOpacity=*/1.0);
        CHECK(nearlyEq(rt2.progress(id2, 30.0), 0.0), "still held during delay");
        CHECK(nearlyEq(rt2.progress(id2, 50.0), 0.0), "progress == 0 exactly as delay ends");
        CHECK(nearlyEq(rt2.progress(id2, 100.0), 0.5), "halfway through duration AFTER the delay");
        CHECK(nearlyEq(rt2.progress(id2, 150.0), 1.0), "settled once delay+duration have elapsed");

        // clearMissing(): an id not in "stillPresent" is dropped -> re-marking it later restarts
        // its clock from that later timestamp, i.e. it animates in again.
        indsin::EffectRuntime rt3;
        indsin::StrandId id3 = 99;
        rt3.mark(id3, 0.0, 100, 0, 1.0);
        CHECK(nearlyEq(rt3.progress(id3, 100.0), 1.0), "settled the first time");
        rt3.clearMissing({}); // id3 not present this tick -> cleared
        rt3.mark(id3, 500.0, 100, 0, 1.0); // reappears later
        CHECK(nearlyEq(rt3.progress(id3, 500.0), 0.0), "reappearing after clearMissing() restarts the animation");
    }

    // 9. applyEffectsToFabric: end-to-end over a real Fabric tree, incl. inheritance ---------------
    {
        std::cout << "-- applyEffectsToFabric: opacity+geometry mutation, effect= inheritance --\n";
        std::string src = R"(
@view.Column=root
  @view.Card=card
    w="200"; h="100"; effect="fade"; duration="200"; easing="linear";
    @view.Text=label text="Hi"; .end/view
  .end/view
  @view.Text=plain text="No effect"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        layoutAt(r);
        auto card = byName(r.fabric, "card");
        auto label = byName(r.fabric, "label"); // inherits effect="fade" from its Card ancestor
        auto plain = byName(r.fabric, "plain");  // no effect anywhere in its ancestry
        double plainYBefore = plain->geometry.y;

        indsin::EffectRuntime rt;
        bool animating = indsin::applyEffectsToFabric(r.fabric, rt, 0.0); // first tick: t=0
        CHECK(animating, "still animating right at t=0");
        CHECK(nearlyEq(card->attrNum("opacity", -1), 0.0), "card starts at opacity 0");
        CHECK(nearlyEq(label->attrNum("opacity", -1), 0.0), "label inherited the Card's effect and also starts at 0");
        CHECK(nearlyEq(plain->attrNum("opacity", 1.0), 1.0), "plain (no effect in its ancestry) is untouched");
        CHECK(nearlyEq(plain->geometry.y, plainYBefore), "plain's geometry is untouched");

        animating = indsin::applyEffectsToFabric(r.fabric, rt, 100.0); // halfway
        CHECK(animating, "still animating halfway through duration=200");
        CHECK(nearlyEq(card->attrNum("opacity", -1), 0.5, 1e-3), "card at 0.5 opacity halfway (easing=\"linear\")");

        animating = indsin::applyEffectsToFabric(r.fabric, rt, 300.0); // past duration
        CHECK(!animating, "no longer animating once past duration=200");
        CHECK(nearlyEq(card->attrNum("opacity", -1), 1.0), "card fully opaque once settled");
        CHECK(nearlyEq(label->attrNum("opacity", -1), 1.0), "label fully opaque once settled");
    }

    if (failures == 0) std::cout << "\nALL PASSED\n";
    else std::cout << "\n" << failures << " FAILURE(S)\n";
    return failures == 0 ? 0 : 1;
}
