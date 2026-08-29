// tools/test_loom_sizing.cpp — tests for Rin Loom's Constraint System (§5/§6): sizing=
// (fit/hug/fill/expand/fixed/auto) and min_width/max_width/min_height/max_height.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_sizing.cpp rin_lexer.cpp rin_parser.cpp \
//       diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -o test_loom_sizing
#include "rin_loom_pipeline.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static loom::StrandPtr byName(const loom::StrandPtr& root, const std::string& n) {
    return loom::findAny(root, [&](const loom::StrandPtr& s){ return s->name == n; });
}
static void layoutAt(loom::PipelineResult& r, double width) {
    loom::Loom engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}

int main() {
    // 1. sizing="fill" claims full bounded parent width; a plain Card (auto) hugs its content --
    {
        std::cout << "-- fill vs auto width --\n";
        std::string src = R"(
@view.Column=root
  @view.Card=auto_card
    @view.Text=t text="hi"; .end/view
  .end/view
  @view.Card=fill_card
    sizing="fill";
    @view.Text=t2 text="hi"; .end/view
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r, 500);
            auto autoCard = byName(r.fabric, "auto_card");
            auto fillCard = byName(r.fabric, "fill_card");
            CHECK(autoCard->geometry.w < 200, "auto (content-hugging) Card stays narrow around its short text");
            CHECK(fillCard->geometry.w == 500, "sizing=\"fill\" Card claims the full 500px root width");
        }
    }

    // 2. Backward compatibility: no sizing= attribute anywhere behaves exactly as before --------
    {
        std::cout << "-- backward compatibility: no sizing= at all --\n";
        std::string src = R"(
@view.Column=root
  @view.Card=c
    @view.Text=t text="short"; .end/view
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r, 500);
            auto c = byName(r.fabric, "c");
            CHECK(c->geometry.w < 150, "a plain Card with no sizing= still hugs its content, unaffected by the new attribute");
        }
    }

    // 3. min_width / max_width clamp a content-sized Strand ------------------------------------
    {
        std::cout << "-- min_width / max_width --\n";
        std::string src = R"(
@view.Column=root
  @view.Card=floored
    min_width="200";
    @view.Text=t text="hi"; .end/view
  .end/view
  @view.Card=capped
    max_width="60";
    @view.Text=t2 text="a much much much longer piece of text than that"; .end/view
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r, 500);
            auto floored = byName(r.fabric, "floored");
            auto capped = byName(r.fabric, "capped");
            CHECK(floored->geometry.w >= 200, "min_width=200 floors a Card narrower than that by content alone");
            CHECK(capped->geometry.w <= 60, "max_width=60 caps a Card that would otherwise be much wider");
        }
    }

    // 4. explicit width= still wins over sizing="fill" (fixed takes precedence per spec §5) -----
    {
        std::cout << "-- explicit width= takes precedence over sizing=\"fill\" --\n";
        std::string src = R"(
@view.Card=c
  sizing="fill"; width="120";
  @view.Text=t text="hi"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r, 500);
            CHECK(r.fabric->geometry.w == 120, "an explicit width= is honored even though sizing=\"fill\" is also present");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
