// tools/test_loom_banner.cpp — tests for Rin Loom's Banner (§13/§14): title=/message=
// shorthand synthesis, nested actions composition, and closable= (auto warp cell + close
// button + visible= wiring, dismissed for real through Needle's dispatchTap).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_banner.cpp rin_lexer.cpp rin_parser.cpp \
//       rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz -o test_loom_banner
#include "rin_loom_pipeline.h"
#include "rin_loom_needle.h"
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
    // 1. title=/message= synthesize real Text children with the right typography tokens --------
    {
        std::cout << "-- title=/message= shorthand --\n";
        std::string src = R"(
@view.Banner=b
  tone="info"; title="Hello"; message="World";
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            auto title = byName(r.fabric, "b_title");
            auto message = byName(r.fabric, "b_message");
            CHECK(title != nullptr, "synthesized title Text strand exists");
            CHECK(message != nullptr, "synthesized message Text strand exists");
            if (title)   CHECK(title->attrStr("text") == "Hello" && title->attrStr("size") == "title", "title text + typography token correct");
            if (message) CHECK(message->attrStr("text") == "World" && message->attrStr("size") == "body", "message text + typography token correct");
        }
    }

    // 2. Manual composition (nested actions) still works alongside the shorthand ----------------
    {
        std::cout << "-- nested actions composition --\n";
        std::string src = R"(
@view.Banner=b
  title="Update"; message="Ready";
  @view.Button=go label="Go"; tone="primary"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            auto go = byName(r.fabric, "go");
            CHECK(go != nullptr, "manually-nested action Button survives alongside title=/message=");
            // Order matters for layout: title, then message, then the manually nested action.
            CHECK(r.fabric->children.size() == 3, "banner has exactly 3 children: title, message, action");
            if (r.fabric->children.size() == 3)
                CHECK(r.fabric->children[2]->name == "go", "manual children come after the synthesized title/message");
        }
    }

    // 3. closable=true: real end-to-end dismissal through Needle --------------------------------
    {
        std::cout << "-- closable=true: real dismissal via Needle --\n";
        std::string src = R"(
@view.Column=root
  @view.Banner=notice
    tone="warning"; message="Something happened"; closable=true;
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r, 400);
            auto banner = byName(r.fabric, "notice");            auto closeBtn = byName(r.fabric, "notice_close");
            CHECK(banner != nullptr, "banner strand exists");
            CHECK(closeBtn != nullptr, "auto-synthesized close button exists");
            CHECK(banner->geometry.h > 0, "banner is visible (non-zero height) before dismissal");
            CHECK(r.warp.has("notice_open") && r.warp.get("notice_open").asString() == "true",
                  "closable=true auto-creates a warp cell defaulting to open");

            double cx = closeBtn->geometry.x + 1, cy = closeBtn->geometry.y + 1;
            auto tap = loom::dispatchTap(r.fabric, r.warp, r.program, cx, cy);
            CHECK(tap.handled, "tapping the close button is handled by Needle");
            CHECK(r.warp.get("notice_open").asString() == "false", "the warp cell actually flipped to false");

            // The Fabric's *attribute* updates immediately (mirroring rin_loom_c_api.cpp's
            // session tap handler, minus the pre-existing gap noted below); Tension's cache is
            // keyed on each Strand's contentHash, which a raw a.value mutation does NOT update by
            // itself, so recomputeHashes() (already provided by rin_loom_strand.h specifically for
            // this) has to run before the next layout() or the cache will short-circuit and the
            // banner will keep reporting its old geometry. NOTE: this is a real, pre-existing gap
            // this test surfaced -- rin_loom_c_api.cpp's rin_loom_session_tap() calls
            // Shuttle::applyWarpChange() + relayout() but never recomputeHashes(), so a Warp-driven
            // attribute change that should resize a Strand (not just this Banner's visible=, but
            // e.g. loom_showcase.rin's counter text too) may not visibly resize through the real
            // session API today. Flagged in docs/loomtime/RIN_LOOM_TOKENS.md; not silently patched
            // here since c_api.cpp is outside this slice's tested surface.
            for (auto& a : banner->attrs) {
                if (a.rawExpr) a.value = loom::evalAttrExpr(a.rawExpr, r.warp, nullptr);
            }
            loom::recomputeHashes(r.fabric); // must recompute from the ROOT: layout()'s cache check
                                              // happens top-down, so a stale root contentHash short-
                                              // circuits before ever reaching the banner's own check
            layoutAt(r, 400);
            CHECK(banner->geometry.h == 0 && banner->geometry.w == 0,
                  "after dismissal + re-layout, the banner collapses to zero size (same rule as visible=false anywhere else)");
        }
    }

    // 4. Backward compatibility: a plain Banner with no title=/message=/closable= at all --------
    {
        std::cout << "-- backward compatibility: plain Banner, no new attrs --\n";
        std::string src = R"(
@view.Banner=b
  type="error";
  @view.Text=t text="Something broke"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            CHECK(r.fabric->children.size() == 1, "no synthesized children when title=/message=/closable= are all absent");
            CHECK(r.fabric->children[0]->name == "t", "the one manually-nested Text child is untouched");
            CHECK(r.fabric->attr("visible") == nullptr, "no synthesized visible= attr when closable= is absent");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
