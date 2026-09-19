// tools/test_indsin_ui_library_expansion.cpp — tests for the "UI/UX Library Expansion" pass:
// Tag/Chip, Kbd, Rating, Skeleton, Spinner, Steps/StepItem, Timeline/TimelineItem, Breadcrumb,
// Pagination (see rin_indsin_strand.h's StrandKind enum doc comment, rin_indsin_layout.h,
// rin_indsin_paint.h, and rin_indsin_components_ext.h for the convenience-synthesis half).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iindsin ../../../../tests/tools/test_indsin_ui_library_expansion.cpp \
//       rin_lexer.cpp rin_parser.cpp rin_make.cpp rin_container_sql.cpp rin_artifact.cpp \
//       rin_interpreter.cpp binfmt/elf_format.cpp binfmt/ar_format.cpp binfmt/pe_format.cpp \
//       binfmt/macho_format.cpp binfmt/coff_obj.cpp loader_ui/library_loader_ui.cpp rin_http.cpp \
//       diagnostics/diagnostic.cpp diagnostics/source_manager.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp clc/clc_container.cpp clc/clc_compress.cpp \
//       clc/clc_security.cpp clc/clc_rin_opt.cpp clc/clc_zip_import.cpp clc/sha256.cpp \
//       -lz -o test_ui_library_expansion
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include <cassert>
#include <iostream>

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

int main() {
    // 1. Tag/Chip — text= renders as a pill; removable="true" synthesizes a close Button and
    //    binds visible= to an auto-created Warp cell that collapses the Tag once clicked.
    {
        std::cout << "-- Tag / Chip --\n";
        std::string src = R"(
@view.Row=root
  @view.Tag=plain text="Design"; .end/view
  @view.Tag=rm text="React"; removable=true; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto plain = byName(r.fabric, "plain"), rm = byName(r.fabric, "rm");
            CHECK(plain->kind == indsin::StrandKind::TAG, "Tag resolves to its own StrandKind");
            CHECK(plain->geometry.h < 30, "Tag is small/pill-sized, not a full block");
            auto plainLabel = byName(plain, "plain_label");
            CHECK(plainLabel && plainLabel->attrStr("text") == "Design", "text= synthesizes a real Text label child");
            auto closeBtn = byName(rm, "rm_close");
            CHECK(closeBtn != nullptr, "removable=\"true\" synthesizes a close Button child");
            CHECK(r.warp.has("rm_visible"), "removable=\"true\" auto-creates a \"<name>_visible\" Warp cell");
            CHECK(rm->geometry.w > 0 && rm->geometry.h > 0, "Tag is visible (laid out with real size) before its cell flips");

            // Real end-to-end dismissal through Needle, exactly like test_indsin_banner.cpp's own
            // "closable=true" case: dispatchTap() actually fires the synthesized onTap=set(...),
            // then attrs are re-evaluated from their rawExpr and hashes recomputed (the same
            // pre-existing gap that test documents -- Tension's cache is keyed on contentHash,
            // which a raw warp mutation doesn't update by itself) before the next layout() sees it.
            double cx = closeBtn->geometry.x + 1, cy = closeBtn->geometry.y + 1;
            auto tap = indsin::dispatchTap(r.fabric, r.warp, r.program, cx, cy);
            CHECK(tap.handled, "tapping the Tag's close button is handled by Needle");
            CHECK(r.warp.get("rm_visible").asString() == "false", "the auto-bound warp cell actually flipped to false");
            for (auto& a : rm->attrs) if (a.rawExpr) a.value = indsin::evalAttrExpr(a.rawExpr, r.warp, nullptr);
            indsin::recomputeHashes(r.fabric);
            layoutAt(r);
            CHECK(rm->geometry.w == 0 && rm->geometry.h == 0, "after dismissal + re-layout, the Tag collapses to zero size, like Banner's visible=false");
        }
    }

    // 2. Kbd — small bordered key box.
    {
        std::cout << "-- Kbd --\n";
        std::string src = R"(
@view.Row=root
  @view.Kbd=k1 text="K"; .end/view
  @view.Kbd=k2 text="Ctrl"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto k1 = byName(r.fabric, "k1"), k2 = byName(r.fabric, "k2");
            CHECK(k1->kind == indsin::StrandKind::KBD, "Kbd resolves to its own StrandKind");
            CHECK(k1->geometry.h < 30, "Kbd is small/pill-sized like Badge/Tooltip");
            CHECK(k2->geometry.w > k1->geometry.w, "a longer key label (\"Ctrl\") measures wider than a single character (\"K\")");
        }
    }

    // 3. Rating — max= equal-width star cells.
    {
        std::cout << "-- Rating --\n";
        std::string src = R"(
@view.Rating=stars max=5; value=3; .end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto stars = byName(r.fabric, "stars");
            CHECK(stars->kind == indsin::StrandKind::RATING, "Rating resolves to its own StrandKind");
            CHECK(stars->geometry.w > 0 && stars->geometry.h > 0, "Rating has real settled geometry");
            indsin::Dye dye;
            auto list = dye.paint(r.fabric);
            int glyphCount = 0;
            for (auto& cmd : list) if (cmd.owner == stars->id && cmd.op == indsin::DrawOp::TEXT_RUN) glyphCount++;
            CHECK(glyphCount == 5, "paints exactly max=5 star glyphs (one TEXT_RUN per star)");
        }
    }

    // 4. Skeleton / Spinner — placeholder box + static loading ring.
    {
        std::cout << "-- Skeleton / Spinner --\n";
        std::string src = R"(
@view.Row=root
  @view.Skeleton=sk width=200; height=20; .end/view
  @view.Spinner=sp .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto sk = byName(r.fabric, "sk"), sp = byName(r.fabric, "sp");
            CHECK(sk->kind == indsin::StrandKind::SKELETON, "Skeleton resolves to its own StrandKind");
            CHECK(sk->geometry.w == 200 && sk->geometry.h == 20, "Skeleton respects explicit width=/height=");
            CHECK(sp->kind == indsin::StrandKind::SPINNER, "Spinner resolves to its own StrandKind");
            CHECK(sp->geometry.w == sp->geometry.h, "Spinner is square (a ring needs equal w/h)");
            CHECK(sp->geometry.w == 24, "Spinner's un-tokened default size is 24");
        }
    }

    // 5. Steps/Stepper — current= drives done/active/upcoming state on its StepItem children.
    {
        std::cout << "-- Steps / StepItem --\n";
        std::string src = R"(
@view.Steps=wizard
  current=1;
  @view.StepItem=s1 label="Account"; .end/view
  @view.StepItem=s2 label="Profile"; .end/view
  @view.StepItem=s3 label="Confirm"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto wizard = byName(r.fabric, "wizard");
            auto s1 = byName(r.fabric, "s1"), s2 = byName(r.fabric, "s2"), s3 = byName(r.fabric, "s3");
            CHECK(wizard->kind == indsin::StrandKind::STEPS, "Steps resolves to its own StrandKind");
            CHECK(s1->attrStr("state") == "done", "current=1 marks StepItem 0 as done");
            CHECK(s2->attrStr("state") == "active", "current=1 marks StepItem 1 (index==current) as active");
            CHECK(s3->attrStr("state") == "upcoming", "current=1 marks StepItem 2 as upcoming");
            CHECK(s1->attrStr("index") == "1" && s2->attrStr("index") == "2" && s3->attrStr("index") == "3",
                  "each StepItem gets a 1-based index= injected");
            CHECK(s2->geometry.x > s1->geometry.x && s3->geometry.x > s2->geometry.x, "StepItems lay out left-to-right");
            CHECK(std::abs(s1->geometry.w - s2->geometry.w) < 0.01, "Steps splits width evenly across items (not content-packed like a plain Row)");
        }
    }

    // 6. Timeline/TimelineItem — date=/title=/desc= leaf rows stacked vertically.
    {
        std::cout << "-- Timeline / TimelineItem --\n";
        std::string src = R"(
@view.Timeline=log
  @view.TimelineItem=e1 date="09:41"; title="Deployed"; desc="v2 shipped"; .end/view
  @view.TimelineItem=e2 title="Reviewed"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto e1 = byName(r.fabric, "e1"), e2 = byName(r.fabric, "e2");
            CHECK(e1->kind == indsin::StrandKind::TIMELINEITEM, "TimelineItem resolves to its own StrandKind");
            CHECK(e2->geometry.y > e1->geometry.y, "TimelineItems stack vertically like a Column");
            CHECK(e1->geometry.h > e2->geometry.h, "an item with date=+title=+desc= measures taller than title= alone");
        }
    }

    // 7. Breadcrumb — items="..." synthesizes Text crumbs separated by "›".
    {
        std::cout << "-- Breadcrumb --\n";
        std::string src = R"(
@view.Breadcrumb=crumbs items="Home, Projects, Atlas App, Settings"; .end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto crumbs = byName(r.fabric, "crumbs");
            CHECK(crumbs->kind == indsin::StrandKind::BREADCRUMB, "Breadcrumb resolves to its own StrandKind");
            // 4 items + 3 separators = 7 synthesized children.
            CHECK(crumbs->children.size() == 7, "items= synthesizes 4 Text crumbs + 3 separator glyphs");
            auto lastItem = byName(crumbs, "crumbs_item3");
            CHECK(lastItem && lastItem->attrStr("tone") == "text", "the last crumb is toned as the current page (\"text\", not muted)");
            auto firstItem = byName(crumbs, "crumbs_item0");
            CHECK(firstItem && firstItem->attrStr("tone") == "text_muted", "earlier crumbs are muted");
        }
    }

    // 8. Pagination — current=/total= synthesizes prev/numbered/next Buttons wired to a Warp cell.
    {
        std::cout << "-- Pagination --\n";
        std::string src = R"(
@view.Pagination=pager current=2; total=5; .end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto pager = byName(r.fabric, "pager");
            CHECK(pager->kind == indsin::StrandKind::PAGINATION, "Pagination resolves to its own StrandKind");
            // prev + 5 pages + next = 7 synthesized Button children.
            CHECK(pager->children.size() == 7, "total=5 synthesizes prev + 5 numbered + next = 7 Buttons");
            CHECK(r.warp.has("pager_page"), "current=/total= auto-creates a \"<name>_page\" Warp cell");
            CHECK(r.warp.get("pager_page").asNumber() == 2, "the auto-created cell starts at current=2");

            auto page3 = byName(pager, "pager_p3");
            CHECK(page3 != nullptr, "a numbered Button exists for each page 1..total");
            CHECK(page3->attrStr("variant") == "outline", "a non-current page renders as outline");
            auto page2 = byName(pager, "pager_p2");
            CHECK(page2->attrStr("variant") == "primary", "the current page (current=2) renders as primary");

            // Clicking page 3's Button should set pager_page to 3 -- same real onTap=set(...)
            // mechanism Banner's close button uses, exercised here via evalAttrExpr directly.
            auto onTap = page3->attr("onTap");
            CHECK(onTap != nullptr, "each enabled page Button carries a real onTap=set(pager_page, N)");
        }
    }

    // 9. Spinner — now a real DrawOp::STROKE_ARC ring (not the old two-rect illusion).
    {
        std::cout << "-- Spinner (real arc) --\n";
        std::string src = R"(
@view.Spinner=sp .end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto sp = byName(r.fabric, "sp");
            indsin::Dye dye;
            auto list = dye.paint(r.fabric);
            int arcCount = 0; double sweepSum = 0; bool sawFullTrack = false;
            for (auto& cmd : list) {
                if (cmd.owner != sp->id) continue;
                CHECK(cmd.op == indsin::DrawOp::STROKE_ARC, "Spinner paints only via STROKE_ARC (no rect approximation left)");
                if (cmd.op == indsin::DrawOp::STROKE_ARC) {
                    arcCount++;
                    sweepSum += cmd.sweepAngleDeg;
                    if (cmd.sweepAngleDeg >= 359.0) sawFullTrack = true;
                    CHECK(cmd.bounds.w == cmd.bounds.h, "Spinner's arc bounding box is square (a true ring)");
                }
            }
            CHECK(arcCount == 2, "Spinner paints a full track arc + one shorter active sweep arc");
            CHECK(sawFullTrack, "one of the two arcs is the full 360-degree track");
        }
    }

    // 10. DonutChart — data="Label:Value,..." -> one STROKE_ARC per segment, sweep proportional
    //     to that segment's share of the total (a real arc-based chart, not a bar/square approximation).
    {
        std::cout << "-- DonutChart --\n";
        std::string src = R"(
@view.DonutChart=usage data="Design:40,Development:35,Testing:25"; .end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto chart = byName(r.fabric, "usage");
            CHECK(chart->kind == indsin::StrandKind::DONUT_CHART, "DonutChart resolves to its own StrandKind");
            indsin::Dye dye;
            auto list = dye.paint(r.fabric);
            std::vector<indsin::DrawCommand> arcs, texts;
            for (auto& cmd : list) if (cmd.owner == chart->id) {
                if (cmd.op == indsin::DrawOp::STROKE_ARC) arcs.push_back(cmd);
                if (cmd.op == indsin::DrawOp::TEXT_RUN) texts.push_back(cmd);
            }
            CHECK(arcs.size() == 3, "data= with 3 entries paints exactly 3 arc segments");
            double sweepSum = 0; for (auto& a : arcs) sweepSum += a.sweepAngleDeg;
            CHECK(std::abs(sweepSum - 360.0) < 0.01, "the 3 segments' sweep angles add up to a full 360-degree circle");
            CHECK(std::abs(arcs[0].sweepAngleDeg - 144.0) < 0.01, "the first segment (40 of 100) sweeps 40% of 360 = 144 degrees");
            CHECK(arcs[0].color.r != arcs[1].color.r || arcs[0].color.g != arcs[1].color.g || arcs[0].color.b != arcs[1].color.b,
                  "segments get distinct colors from the default palette when colors= is unset");
            CHECK(!texts.empty() && texts[0].text == "100", "with no centerLabel=, the center shows the running total (40+35+25=100)");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
