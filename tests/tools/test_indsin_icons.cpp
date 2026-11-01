// tools/test_indsin_icons.cpp — tests for the Icon Registry (spec §18) and the
// Icon/IconButton StrandKinds it feeds (rin_indsin_icons.h, rin_indsin_strand.h,
// rin_indsin_layout.h, rin_indsin_paint.h).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iindsin ../../../../tools/test_indsin_icons.cpp rin_lexer.cpp rin_parser.cpp \
//       rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz -o test_indsin_icons
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
static void layoutAt(indsin::PipelineResult& r, double width = 400) {
    indsin::Indsin engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}
static const indsin::DrawCommand* findTextCmdFor(const indsin::DrawList& list, indsin::StrandId id) {
    for (auto& c : list) if (c.owner == id && c.op == indsin::DrawOp::TEXT_RUN) return &c;
    return nullptr;
}

int main() {
    // 1. IconRegistry: known names, unknown-name safety, extensibility --------------------------
    {
        std::cout << "-- IconRegistry: lookups + extensibility --\n";
        auto& reg = indsin::iconRegistry();
        CHECK(reg.has("home"), "built-in 'home' is registered");
        CHECK(reg.has("arrow-left") && reg.has("arrow-right"), "both arrow directions registered");
        CHECK(reg.has("download") && reg.has("upload"), "download/upload both registered");
        CHECK(!reg.has("totally-not-a-real-icon"), "an unregistered name correctly reports as absent");

        bool found = true;
        std::string glyph = reg.resolve("totally-not-a-real-icon", &found);
        CHECK(!found, "resolve() flags an unknown name via found=false, not a crash");
        CHECK(!glyph.empty(), "resolve() still returns a safe non-empty placeholder glyph");

        reg.registerIcon("custom-star", "*");
        CHECK(reg.has("custom-star"), "registerIcon() adds a new name at runtime");
        bool found2 = false;
        CHECK(reg.resolve("custom-star", &found2) == "*" && found2, "the newly-registered glyph resolves correctly");

        int emptyCount = 0;
        for (auto& kv : reg.glyphs) if (kv.second.empty()) emptyCount++;
        CHECK(emptyCount == 0, "no built-in icon resolves to an empty glyph");
    }

    // 2. @view.Icon: parses, sized, painted with the right glyph ---------------------------------
    {
        std::cout << "-- @view.Icon --\n";
        std::string src = R"(
@view.Row=root
  @view.Icon=star icon="home"; size=32; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto star = byName(r.fabric, "star");
            CHECK(star && star->kind == indsin::StrandKind::ICON, "resolves to StrandKind::ICON");
            CHECK(star->geometry.w == 32 && star->geometry.h == 32, "size= attribute is honored");

            indsin::Dye dye;
            auto list = dye.paint(r.fabric);
            auto cmd = findTextCmdFor(list, star->id);
            CHECK(cmd != nullptr, "Icon paints a TEXT_RUN command");
            if (cmd) CHECK(cmd->text == indsin::iconRegistry().resolve("home"), "painted glyph matches the registry's 'home' glyph");
        }
    }

    // 3. @view.Icon with default size (no size= given) -------------------------------------------
    {
        std::cout << "-- @view.Icon: default size --\n";
        std::string src = R"(
@view.Row=root
  @view.Icon=plain icon="check"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto plain = byName(r.fabric, "plain");
            CHECK(plain->geometry.w == 24 && plain->geometry.h == 24, "defaults to a 24x24 box when size= is omitted");
        }
    }

    // 4. @view.IconButton: icon-only, icon+label, tappable ----------------------------------------
    {
        std::cout << "-- @view.IconButton --\n";
        std::string src = R"(
warp menuOpen = false;
@view.Row=root
  @view.IconButton=iconOnly icon="menu"; onTap=toggle(menuOpen); .end/view
  @view.IconButton=iconLabel icon="download"; label="Download"; .end/view
  @view.IconButton=noIcon label="Plain"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            indsin::Dye dye;
            auto list = dye.paint(r.fabric);

            auto iconOnly = byName(r.fabric, "iconOnly");
            CHECK(iconOnly->kind == indsin::StrandKind::ICONBUTTON, "resolves to StrandKind::ICONBUTTON");
            CHECK(iconOnly->geometry.w > 0, "icon-only IconButton still measures a non-zero tap width");
            auto cmdOnly = findTextCmdFor(list, iconOnly->id);
            CHECK(cmdOnly && cmdOnly->text == indsin::iconRegistry().resolve("menu"), "icon-only IconButton paints just the glyph");

            auto iconLabel = byName(r.fabric, "iconLabel");
            auto cmdBoth = findTextCmdFor(list, iconLabel->id);
            std::string expected = indsin::iconRegistry().resolve("download") + " Download";
            CHECK(cmdBoth && cmdBoth->text == expected, "icon+label IconButton paints 'glyph label'");
            CHECK(iconLabel->geometry.w > iconOnly->geometry.w, "icon+label IconButton measures wider than icon-only");

            auto noIcon = byName(r.fabric, "noIcon");
            auto cmdNoIcon = findTextCmdFor(list, noIcon->id);
            CHECK(cmdNoIcon && cmdNoIcon->text == "Plain", "IconButton with no icon= falls back to plain label behavior");

            double x = iconOnly->geometry.x + 1, y = iconOnly->geometry.y + 1;
            auto res = indsin::dispatchTap(r.fabric, r.warp, r.program, x, y);
            CHECK(res.handled && res.error.empty(), "onTap on an IconButton dispatches through Needle normally");
            CHECK(r.warp.get("menuOpen").asString() == "true", "toggle() actually ran and flipped the bound Warp cell");
        }
    }

    // 5. Backward compatibility: plain Button unaffected -------------------------------------------
    {
        std::cout << "-- backward compatibility: plain Button --\n";
        std::string src = R"(
@view.Row=root
  @view.Button=b label="Save"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            indsin::Dye dye;
            auto list = dye.paint(r.fabric);
            auto b = byName(r.fabric, "b");
            auto cmd = findTextCmdFor(list, b->id);
            CHECK(cmd && cmd->text == "Save", "a plain Button (no icon= involved at all) still paints just its label=");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
