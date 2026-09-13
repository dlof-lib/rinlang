// tools/test_loom_missing_components.cpp — tests for the "missing components" pass: Grid, Wrap,
// Spacer, Box (Container), Badge, Progress, Checkbox, Switch, Avatar, Input, TextArea, Dialog,
// Tabs/TabItem, Tooltip.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_missing_components.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp \
//       diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp \
//       diagnostics/source_manager.cpp -lz -o test_loom_missing_components
#include "rin_loom_pipeline.h"
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
static void layoutAt(loom::PipelineResult& r, double width = 1200) {
    loom::Loom engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}

int main() {
    // 1. Box (Container) — plain padded box, accepts the spec's own "Container" tag as an alias.
    {
        std::cout << "-- Box / Container --\n";
        std::string src = R"(
@view.Box=root
  padding="comfortable";
  @view.Text=t text="hello"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto box = byName(r.fabric, "root");
            CHECK(box->kind == loom::StrandKind::BOX, "Box tag resolves to StrandKind::BOX");
            CHECK(box->geometry.w > 0 && box->geometry.h > 0, "Box has real settled geometry");
        }
        std::string src2 = R"(
@view.Container=root
  padding="comfortable";
  @view.Text=t text="hello"; .end/view
.end/view
)";
        auto r2 = loom::runColdPipeline(src2);
        CHECK(r2.ok, "Container alias parses");
        if (r2.ok) CHECK(byName(r2.fabric, "root")->kind == loom::StrandKind::BOX, "Container tag also resolves to StrandKind::BOX (no new StrandKind for it)");
    }

    // 2. Grid — fixed column count, row-major placement.
    {
        std::cout << "-- Grid --\n";
        std::string src = R"(
@view.Grid=g
  columns=3; gap="normal";
  @view.Card=a width=100; height=40; .end/view
  @view.Card=b width=100; height=60; .end/view
  @view.Card=c width=100; height=40; .end/view
  @view.Card=d width=100; height=40; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto a = byName(r.fabric, "a"), b = byName(r.fabric, "b"), c = byName(r.fabric, "c"), d = byName(r.fabric, "d");
            CHECK(a->geometry.y == b->geometry.y && b->geometry.y == c->geometry.y, "first row: a/b/c share the same y");
            CHECK(d->geometry.y > a->geometry.y, "4th item (columns=3) wraps to a new row, below the first");
            CHECK(b->geometry.x > a->geometry.x && c->geometry.x > b->geometry.x, "columns advance left-to-right");
        }
    }

    // 3. Wrap — flows to a new line once a line would overflow available width.
    {
        std::cout << "-- Wrap --\n";
        std::string src = R"(
@view.Wrap=w
  gap="compact";
  @view.Badge=a text="one"; .end/view
  @view.Badge=b text="two"; .end/view
  @view.Badge=c text="three"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::Loom engine;
            engine.layout(r.fabric, {0, 60, 0, 1e9}, 0, 0); // very narrow -> forces wrapping
            auto a = byName(r.fabric, "a"), b = byName(r.fabric, "b");
            CHECK(b->geometry.y > a->geometry.y, "second badge wraps onto a new line under a narrow width");
        }
    }

    // 4. Spacer — expands to consume remaining main-axis space in a Row.
    {
        std::cout << "-- Spacer --\n";
        std::string src = R"(
@view.Row=root
  @view.Button=left label="L"; .end/view
  @view.Spacer=sp .end/view
  @view.Button=right label="R"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r, 1000);
            auto sp = byName(r.fabric, "sp"), right = byName(r.fabric, "right");
            CHECK(sp->geometry.w > 400, "Spacer claims most of the remaining width");
            CHECK(right->geometry.w > 0, "the Button AFTER the Spacer still gets real width, not starved to zero"
                                          " (regression check for the two-pass flex fix)");
            CHECK(right->geometry.x > sp->geometry.x, "trailing Button is positioned after the Spacer, pushed toward the right");
        }
    }

    // 5. Badge / Progress / Checkbox / Switch / Avatar — basic geometry + attr sanity.
    {
        std::cout << "-- Badge/Progress/Checkbox/Switch/Avatar --\n";
        std::string src = R"(
@view.Column=root
  @view.Badge=badge text="New"; tone="success"; .end/view
  @view.Progress=prog value=40; .end/view
  @view.Checkbox=chk checked=true; .end/view
  @view.Switch=sw checked=false; .end/view
  @view.Avatar=av size="large"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto badge = byName(r.fabric, "badge"), prog = byName(r.fabric, "prog"),
                 chk = byName(r.fabric, "chk"), sw = byName(r.fabric, "sw"), av = byName(r.fabric, "av");
            CHECK(badge->geometry.w > 0 && badge->geometry.h < 30, "Badge is small/pill-sized, not a full block");
            CHECK(prog->geometry.w > 500, "Progress fills available width by default");
            CHECK(chk->geometry.w == chk->geometry.h, "Checkbox is square");
            CHECK(sw->geometry.w > sw->geometry.h, "Switch is a wide pill, not square");
            CHECK(av->geometry.w == 56 && av->geometry.h == 56, "Avatar size=\"large\" resolves to 56x56");

            loom::Dye dye;
            auto list = dye.paint(r.fabric);
            bool sawSwitchThumb = false;
            for (auto& cmd : list) if (cmd.owner == sw->id) sawSwitchThumb = true; // track + thumb both tagged with owner=sw->id
            CHECK(sawSwitchThumb, "Switch paints at least its track (owner-tagged draw commands present)");
        }
    }

    // 6. Input / TextArea — placeholder vs value, default heights differ.
    {
        std::cout << "-- Input / TextArea --\n";
        std::string src = R"(
@view.Column=root
  @view.Input=name placeholder="Your name"; .end/view
  @view.Input=email value="a@b.com"; .end/view
  @view.TextArea=notes placeholder="Notes..."; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto name = byName(r.fabric, "name"), notes = byName(r.fabric, "notes");
            CHECK(name->geometry.h == 40, "Input default height is 40");
            CHECK(notes->geometry.h == 96, "TextArea default height is 96 (taller than Input)");
            CHECK(notes->geometry.h > name->geometry.h, "TextArea is taller than a single-line Input");
        }
    }

    // 7. Dialog — collapses when open="false" (or absent), same rule as Drawer/Menu/Banner.
    {
        std::cout << "-- Dialog --\n";
        std::string srcClosed = R"(
@view.Dialog=d
  @view.Text=t text="hi"; .end/view
.end/view
)";
        auto r1 = loom::runColdPipeline(srcClosed);
        CHECK(r1.ok, "parses (closed)");
        if (r1.ok) { layoutAt(r1); CHECK(byName(r1.fabric, "d")->geometry.w == 0 && byName(r1.fabric, "d")->geometry.h == 0, "Dialog with no open= collapses to zero size"); }

        std::string srcOpen = R"(
@view.Dialog=d
  open=true;
  @view.Text=t text="hi"; .end/view
.end/view
)";
        auto r2 = loom::runColdPipeline(srcOpen);
        CHECK(r2.ok, "parses (open)");
        if (r2.ok) { layoutAt(r2); CHECK(byName(r2.fabric, "d")->geometry.w > 0 && byName(r2.fabric, "d")->geometry.h > 0, "Dialog with open=true lays out real content"); }
    }

    // 8. Tabs / TabItem — a Row of button-like items; state="selected" highlights (shared with
    //    Button's own StrandState machinery — no separate "active" concept was invented).
    {
        std::cout << "-- Tabs / TabItem --\n";
        std::string src = R"(
@view.Tabs=t
  @view.TabItem=home label="Home"; variant="ghost"; state="selected"; .end/view
  @view.TabItem=settings label="Settings"; variant="ghost"; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto home = byName(r.fabric, "home"), settings = byName(r.fabric, "settings");
            CHECK(home->kind == loom::StrandKind::TABITEM, "TabItem resolves to its own StrandKind");
            CHECK(settings->geometry.x > home->geometry.x, "tabs lay out left-to-right like a Row");
            CHECK(loom::resolveState(*home) == loom::StrandState::SELECTED, "state=\"selected\" is read generically (no new state concept needed)");
        }
    }

    // 9. Tooltip — small self-contained text box.
    {
        std::cout << "-- Tooltip --\n";
        std::string src = R"(
@view.Tooltip=tip text="Save your work"; .end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto tip = byName(r.fabric, "tip");
            CHECK(tip->geometry.w > 0 && tip->geometry.h > 0 && tip->geometry.h < 40, "Tooltip is a small content-sized box");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : (std::to_string(failures) + " FAILURE(S)")) << "\n";
    return failures == 0 ? 0 : 1;
}
