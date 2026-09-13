// tools/test_loom_button.cpp — tests for Rin Loom's Button component library: variants
// (fill/outline/ghost/link/glass/gradient over the 7 tone= roles), sizes (xs/small/medium/
// large/xl), states (normal/disabled/loading/selected/...), and Button a11y metadata.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_button.cpp rin_lexer.cpp rin_parser.cpp \
//       diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -o test_loom_button
#include "rin_loom_pipeline.h"
#include "rin_loom_needle.h"
#include <cassert>
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static loom::StrandPtr byName(const loom::StrandPtr& root, const std::string& n) {
    return loom::findAny(root, [&](const loom::StrandPtr& s){ return s->name == n; });
}
// runColdPipeline only builds the Fabric tree; it does not lay it out (loomc.cpp calls
// loom::Loom::layout separately -- see its main()). Every test here needs real settled
// geometry, so this helper does what loomc.cpp does before any assertion runs.
static void layoutAt(loom::PipelineResult& r, double width = 1200) {
    loom::Loom engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}

int main() {
    // 1. Size scale changes real layout geometry, strictly increasing height ------------------
    {
        std::cout << "-- button size scale --\n";
        std::string src = R"(
@view.Column=root
  @view.Button=a label="A"; size="xs"; .end/view
  @view.Button=b label="B"; size="small"; .end/view
  @view.Button=c label="C"; size="medium"; .end/view
  @view.Button=d label="D"; size="large"; .end/view
  @view.Button=e label="E"; size="xl"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            double hxs = byName(r.fabric,"a")->geometry.h, hsmall = byName(r.fabric,"b")->geometry.h,
                   hmed = byName(r.fabric,"c")->geometry.h, hlarge = byName(r.fabric,"d")->geometry.h,
                   hxl = byName(r.fabric,"e")->geometry.h;
            CHECK(hxs < hsmall && hsmall < hmed && hlarge > hmed && hxl > hlarge,
                  "xs < small < medium < large < xl in real settled height");
            CHECK(hmed == 44, "medium height == 44 (matches the pre-Loom hardcoded default exactly)");
        }
    }

    // 2. No size= at all still measures exactly like before Loom's Button library existed -----
    {
        std::cout << "-- backward compatibility: no size=, no variant=, no state= --\n";
        std::string src = R"(
@view.Column=root
  @view.Button=b label="Save"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto b = byName(r.fabric, "b");
            CHECK(b->geometry.h == 44, "height defaults to 44, same as before this file existed");
            CHECK(loom::resolveButtonTreatment(*b) == loom::ButtonTreatment::FILL, "variant defaults to fill");
            CHECK(loom::resolveState(*b) == loom::StrandState::NORMAL, "state defaults to normal");
        }
    }

    // 3. Disabled gates real tap dispatch, not just paint --------------------------------------
    {
        std::cout << "-- disabled state gates Needle dispatch --\n";
        std::string src = R"(
warp count = 0;
@view.Column=root
  @view.Button=live label="Go"; onTap=increment(count); .end/view
  @view.Button=dead label="Go"; disabled=true; onTap=increment(count); .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto live = byName(r.fabric, "live");
            auto dead = byName(r.fabric, "dead");
            double cx = live->geometry.x + 1, cy = live->geometry.y + 1;
            auto tapLive = loom::dispatchTap(r.fabric, r.warp, r.program, cx, cy);
            CHECK(tapLive.handled, "tapping the live (non-disabled) button is handled");
            CHECK(r.warp.get("count").asNumber() == 1, "count actually incremented for the live button");

            double dx = dead->geometry.x + 1, dy = dead->geometry.y + 1;
            auto tapDead = loom::dispatchTap(r.fabric, r.warp, r.program, dx, dy);
            CHECK(!tapDead.handled, "tapping the disabled button is NOT handled (Needle skips it)");
            CHECK(r.warp.get("count").asNumber() == 1, "count did not change from tapping the disabled button");
        }
    }

    // 4. Accessibility: role + accessible name, with and without an explicit a11y_label --------
    {
        std::cout << "-- accessibility metadata --\n";
        std::string src = R"(
@view.Column=root
  @view.Button=plain label="Save"; .end/view
  @view.Button=described label="Save"; a11y_label="Save the current document"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            auto plain = byName(r.fabric, "plain");
            auto described = byName(r.fabric, "described");
            CHECK(loom::accessibleRole(plain->kind) == "button", "Button's accessible role is 'button'");
            CHECK(loom::accessibleName(*plain) == "Save", "accessible name defaults to the visible label");
            CHECK(loom::accessibleName(*described) == "Save the current document",
                  "a11y_label= overrides the accessible name without changing the visible label");
            CHECK(described->attrStr("label") == "Save", "visible label= is untouched by a11y_label=");
        }
    }

    // 5. Variant + tone combine correctly (spot check via resolveColor/treatment, not pixels) --
    {
        std::cout << "-- variant/tone combination --\n";
        std::string src = R"(
@view.Button=b label="X"; tone="danger"; variant="outline"; .end/view
)";
        auto r = loom::runColdPipeline(src);
        // no @view root wrapper needed to be a Column -- a lone top-level @view IS the root
        CHECK(r.ok, "a lone top-level Button can be its own @view root");
        if (r.ok) {
            CHECK(loom::resolveButtonTreatment(*r.fabric) == loom::ButtonTreatment::OUTLINE, "variant=outline resolves");
            loom::Color danger; loom::resolveSemanticColor("danger", danger);
            loom::Color resolved = loom::resolveColor(r.fabric);
            CHECK(resolved.r == danger.r && resolved.g == danger.g && resolved.b == danger.b,
                  "tone=danger resolves to the theme's danger role regardless of variant=");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
