// tools/test_loom_overlay.cpp — tests for the Overlay Engine (rin_loom_overlay.h): a real
// z-layer for Dialog/anchored-Tooltip, a scrim for modal Dialogs, and hit-test event blocking.
// This is the follow-up to test_loom_missing_components.cpp's honest scope note ("Dialog/Tooltip
// only compute their own box; real overlay positioning is a `Stack align=/valign=` job for the
// .rin author") -- it verifies that note no longer applies.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_overlay.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp \
//       diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp \
//       diagnostics/source_manager.cpp -lz -o test_loom_overlay
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

// Lays out `r.fabric` at rootWidth x (unbounded content height, matching the real
// rin_loom_c_api.cpp constraints), then runs the overlay pass against viewportW x viewportH.
static loom::OverlayLayer layoutAndBuildOverlay(loom::PipelineResult& r, loom::Loom& engine,
                                                 double rootWidth, double viewportW, double viewportH) {
    engine.layout(r.fabric, {0, rootWidth, 0, 1e9}, 0, 0);
    return loom::buildOverlayLayer(engine, r.fabric, viewportW, viewportH);
}

int main() {
    // 1. Flow exclusion: an open Dialog no longer inflates its parent Column's height. This is
    //    the actual bug the old "honest scope note" was covering for -- a Dialog measured a real
    //    box but that box just sat inert in the document, pushing later siblings down.
    {
        std::cout << "-- Flow exclusion (Dialog no longer occupies parent flow space) --\n";
        std::string srcClosed = R"(
@view.Column=root
  @view.Text=before text="before"; .end/view
  @view.Dialog=d
    open=false;
    @view.Text=t text="Some fairly long dialog content that would be tall"; .end/view
  .end/view
  @view.Text=after text="after"; .end/view
.end/view
)";
        std::string srcOpen = R"(
@view.Column=root
  @view.Text=before text="before"; .end/view
  @view.Dialog=d
    open=true;
    @view.Text=t text="Some fairly long dialog content that would be tall"; .end/view
  .end/view
  @view.Text=after text="after"; .end/view
.end/view
)";
        auto rc = loom::runColdPipeline(srcClosed);
        auto ro = loom::runColdPipeline(srcOpen);
        CHECK(rc.ok && ro.ok, "both parse");
        if (rc.ok && ro.ok) {
            loom::Loom ec, eo;
            ec.layout(rc.fabric, {0, 300, 0, 1e9}, 0, 0);
            eo.layout(ro.fabric, {0, 300, 0, 1e9}, 0, 0);
            double closedRootH = byName(rc.fabric, "root")->geometry.h;
            double openRootH   = byName(ro.fabric, "root")->geometry.h;
            CHECK(closedRootH == openRootH,
                  "Column height is identical whether the Dialog inside it is open or closed (Dialog is out of flow)");
            CHECK(byName(ro.fabric, "d")->geometry.w > 0 && byName(ro.fabric, "d")->geometry.h > 0,
                  "the open Dialog's own content box is still measured correctly (just not counted by its parent)");
        }
    }

    // 2. Viewport centering: an open Dialog is centered against the *viewport*, independent of
    //    where in the tree it was declared and independent of the (unbounded) content height.
    {
        std::cout << "-- Viewport centering --\n";
        std::string src = R"(
@view.Column=root
  @view.Dialog=d
    open=true; width=200; height=100;
    @view.Text=t text="hi"; .end/view
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            CHECK(layer.entries.size() == 1, "exactly one overlay entry for the open Dialog");
            if (!layer.entries.empty()) {
                auto& box = layer.entries[0].box;
                CHECK(box.w == 200 && box.h == 100, "Dialog keeps its explicit width=/height=");
                CHECK(box.x == (390 - 200) / 2.0, "Dialog is horizontally centered against the viewport width");
                CHECK(box.y == (844 - 100) / 2.0, "Dialog is vertically centered against the viewport height, not the unbounded content height");
            }
        }
    }

    // 3. align=/valign= override still respected — e.g. pinning a Dialog to the top-left corner
    //    instead of dead center (a toast/snackbar-style placement), same alignOffset() semantics
    //    Stack already uses.
    {
        std::cout << "-- align=/valign= override --\n";
        std::string src = R"(
@view.Dialog=d
  open=true; width=100; height=50; align="left"; valign="top";
  @view.Text=t text="hi"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            CHECK(!layer.entries.empty() && layer.entries[0].box.x == 0 && layer.entries[0].box.y == 0,
                  "align=\"left\" valign=\"top\" pins the Dialog to the viewport's top-left corner");
        }
    }

    // 4. Scrim defaults: a Dialog is modal+scrim by default; both are individually overridable.
    {
        std::cout << "-- Scrim defaults and overrides --\n";
        auto def = loom::runColdPipeline(R"(@view.Dialog=d open=true; @view.Text=t text="hi"; .end/view .end/view)");
        auto noScrim = loom::runColdPipeline(R"(@view.Dialog=d open=true; scrim=false; @view.Text=t text="hi"; .end/view .end/view)");
        auto notModal = loom::runColdPipeline(R"(@view.Dialog=d open=true; modal=false; @view.Text=t text="hi"; .end/view .end/view)");
        CHECK(def.ok && noScrim.ok && notModal.ok, "all three parse");
        if (def.ok && noScrim.ok && notModal.ok) {
            loom::Loom e1, e2, e3;
            auto l1 = layoutAndBuildOverlay(def, e1, 390, 390, 844);
            auto l2 = layoutAndBuildOverlay(noScrim, e2, 390, 390, 844);
            auto l3 = layoutAndBuildOverlay(notModal, e3, 390, 390, 844);
            CHECK(!l1.entries.empty() && l1.entries[0].modal && l1.entries[0].scrim,
                  "Dialog defaults to modal=true, scrim=true");
            CHECK(!l2.entries.empty() && l2.entries[0].modal && !l2.entries[0].scrim,
                  "scrim=\"false\" suppresses the scrim while staying modal (still blocks hits, just doesn't dim)");
            CHECK(!l3.entries.empty() && !l3.entries[0].modal && !l3.entries[0].scrim,
                  "modal=\"false\" implies no scrim either (a non-modal Dialog can't block input behind it)");
            CHECK(l1.entries[0].scrimRect.w == 390 && l1.entries[0].scrimRect.h == 844,
                  "the scrim covers the full viewport, not just the Dialog's own box");
        }
    }

    // 5. Event blocking: a tap outside the Dialog's box (but inside the viewport) is consumed by
    //    the scrim -- it must never reach the main document underneath.
    {
        std::cout << "-- Modal event blocking --\n";
        std::string src = R"(
warp dialogOpen = true;
@view.Column=root
  @view.Button=behind label="Behind"; onTap=increment(counter); .end/view
  @view.Dialog=d
    open=dialogOpen; width=100; height=50;
    @view.Button=cancel label="Cancel"; onTap=toggle(dialogOpen); .end/view
  .end/view
.end/view
)";
        // (counter isn't declared as a warp here on purpose -- if the tap incorrectly reaches
        // "behind" it will fail with "no top-level function/built-in", which the CHECK below
        // treats as proof the tap did NOT get blocked as it should have.)
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            CHECK(layer.entries.size() == 1, "one overlay entry for the open Dialog");

            // A point clearly outside the (centered, 100x50) Dialog box but inside the viewport.
            auto oh = loom::hitTestOverlayLayer(layer, 5, 5);
            CHECK(oh.blocked, "a tap far outside the Dialog's box is blocked by its scrim");
            CHECK(oh.scrimOwner != nullptr && oh.scrimOwner->strand->name == "d",
                  "the block is attributed to the right Dialog entry");

            // A point inside the Dialog's own box should hit it, not be blocked.
            double bx = layer.entries[0].box.x + layer.entries[0].box.w / 2.0;
            double by = layer.entries[0].box.y + layer.entries[0].box.h / 2.0;
            auto ohInside = loom::hitTestOverlayLayer(layer, bx, by);
            CHECK(!ohInside.blocked && ohInside.hit != nullptr, "a tap inside the Dialog's own box is not blocked");

            // Full dispatch: tapping the scrim never reaches the Button behind it.
            loom::TapResult tap = loom::dispatchTapWithOverlay(r.fabric, r.warp, r.program, layer, 5, 5);
            CHECK(tap.handled && tap.error.empty(),
                  "scrim tap is handled locally with no dispatch error (never reached the Button behind it)");
            CHECK(r.warp.get("dialogOpen").asString() == "false",
                  "tapping the (dismissible-by-default) scrim closed the Dialog by flipping its bound warp cell");
        }
    }

    // 6. dismissible="false": scrim still blocks, but does not close the Dialog.
    {
        std::cout << "-- Non-dismissible modal --\n";
        std::string src = R"(
warp dialogOpen = true;
@view.Dialog=d open=dialogOpen; dismissible=false; width=80; height=40;
  @view.Text=t text="hi"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            loom::TapResult tap = loom::dispatchTapWithOverlay(r.fabric, r.warp, r.program, layer, 2, 2);
            CHECK(tap.handled, "scrim tap is still consumed (blocks input either way)");
            CHECK(r.warp.get("dialogOpen").asString() == "true",
                  "dismissible=\"false\" means the scrim tap does NOT close the Dialog");
        }
    }

    // 7. Tooltip backward compatibility: without `anchor=`, a Tooltip is NOT pulled into the overlay
    //    layer at all -- it stays exactly the plain inline content-sized box the original
    //    missing-components patch already shipped and tested.
    {
        std::cout << "-- Tooltip backward compatibility (no anchor=) --\n";
        auto r = loom::runColdPipeline(R"(@view.Tooltip=tip text="Save your work"; .end/view)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            CHECK(layer.entries.empty(), "a Tooltip with no anchor= is not treated as an overlay");
            auto tip = byName(r.fabric, "tip");
            CHECK(tip->geometry.w > 0 && tip->geometry.h > 0 && tip->geometry.h < 40,
                  "and still lays out as the original small content-sized box");
        }
    }

    // 8. Tooltip anchoring: with `anchor=`, a Tooltip becomes a real floating overlay positioned
    //    relative to its anchor's on-screen box, and clamps fully on-screen near an edge.
    {
        std::cout << "-- Tooltip anchoring (anchor=) --\n";
        std::string src = R"(
@view.Column=root
  @view.Button=saveBtn label="Save"; width=60; height=30; .end/view
  @view.Tooltip=hint text="Saved!"; anchor="saveBtn"; placement="bottom"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            CHECK(layer.entries.size() == 1, "the anchored Tooltip becomes one overlay entry");
            if (!layer.entries.empty()) {
                auto save = byName(r.fabric, "saveBtn");
                auto& box = layer.entries[0].box;
                CHECK(box.y > save->geometry.y, "placement=\"bottom\" positions the Tooltip below its anchor");
                CHECK(box.x >= 0 && box.y >= 0 && box.x + box.w <= 390 && box.y + box.h <= 844,
                      "the anchored Tooltip is clamped fully within the viewport");
            }
        }

        // Edge case: an anchor pinned to the viewport's top-left corner would push a
        // placement="top" Tooltip to a negative y off-screen without clamping.
        std::string srcEdge = R"(
@view.Column=root
  @view.Button=corner label="X"; width=30; height=20; .end/view
  @view.Tooltip=hint text="Hint"; anchor="corner"; placement="top"; .end/view
.end/view
)";
        auto re = loom::runColdPipeline(srcEdge);
        CHECK(re.ok, "edge-case source parses");
        if (re.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(re, engine, 390, 390, 844);
            CHECK(!layer.entries.empty() && layer.entries[0].box.y >= 0,
                  "a Tooltip that would float above the top edge is clamped to y=0 instead of going negative");
        }
    }

    // 9. Stacking order: with two open Dialogs, the later-declared one is topmost for hit-testing
    //    (document order == stacking order, the same convention the rest of the Loom already uses
    //    for paint order — no separate z-index concept invented).
    {
        std::cout << "-- Stacking order (two simultaneously-open Dialogs) --\n";
        std::string src = R"(
@view.Column=root
  @view.Dialog=first open=true; width=300; height=300; align="left"; valign="top";
    @view.Text=t1 text="first"; .end/view
  .end/view
  @view.Dialog=second open=true; width=100; height=100; align="left"; valign="top";
    @view.Text=t2 text="second"; .end/view
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            auto layer = layoutAndBuildOverlay(r, engine, 390, 390, 844);
            CHECK(layer.entries.size() == 2, "two overlay entries, one per open Dialog");
            // Both are pinned to (0,0); the overlapping point (50,50) falls inside both boxes,
            // so the topmost (later-declared / painted-last) one, "second", must win the hit-test.
            auto oh = loom::hitTestOverlayLayer(layer, 50, 50);
            CHECK(oh.hit != nullptr && oh.hit->name == "second",
                  "the later-declared (topmost) Dialog wins a hit-test over an overlapping earlier one");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : (std::to_string(failures) + " FAILURE(S)")) << "\n";
    return failures == 0 ? 0 : 1;
}
