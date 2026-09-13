// tools/test_loom_actions.cpp — tests for the Loom Actions / Container Context increment added
// alongside the professional UI upgrade work (see docs/loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md):
//   - Needle's built-in show()/hide() Warp actions (rin_loom_needle.h), used by
//     Dialog/Drawer/Banner's open=/visible= binding exactly like toggle() already was.
//   - Container Context helpers findById/findByKind/findAllByKind/findParentOf
//     (rin_loom_strand.h), which resolve the spec's find(id)/findByKind(kind)/parent() APIs
//     without crashing on a missing match.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_actions.cpp rin_lexer.cpp rin_parser.cpp \
//       rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz -o test_loom_actions
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
static void layoutAt(loom::PipelineResult& r, double width = 1200) {
    loom::Loom engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}

int main() {
    // 1. show()/hide() on a bare Warp cell -------------------------------------------------
    {
        std::cout << "-- show()/hide() built-in actions --\n";
        std::string src = R"(
warp dialogOpen = false;

@view.Column=root
  @view.Dialog=confirm open=dialogOpen; .end/view
  @view.Button=openBtn label="Open"; onTap=show(dialogOpen); .end/view
  @view.Button=closeBtn label="Close"; onTap=hide(dialogOpen); .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(r.warp.get("dialogOpen").asString() == "false", "starts closed");

            auto openBtn = byName(r.fabric, "openBtn");
            double ox = openBtn->geometry.x + 1, oy = openBtn->geometry.y + 1;
            auto res1 = loom::dispatchTap(r.fabric, r.warp, r.program, ox, oy);
            CHECK(res1.handled && res1.error.empty(), "show() dispatches without error");
            CHECK(r.warp.get("dialogOpen").asString() == "true", "show() sets cell to true");

            auto closeBtn = byName(r.fabric, "closeBtn");
            double cx = closeBtn->geometry.x + 1, cy = closeBtn->geometry.y + 1;
            auto res2 = loom::dispatchTap(r.fabric, r.warp, r.program, cx, cy);
            CHECK(res2.handled && res2.error.empty(), "hide() dispatches without error");
            CHECK(r.warp.get("dialogOpen").asString() == "false", "hide() sets cell back to false");
        }
    }

    // 2. Container Context find helpers ----------------------------------------------------
    {
        std::cout << "-- findById / findByKind / findAllByKind / findParentOf --\n";
        std::string src = R"(
@view.Column=root
  @view.Card=profile
    @view.Text=name text="Droy"; .end/view
    @view.Button=toggle label="Toggle"; .end/view
  .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            auto nameNode = loom::findById(r.fabric, "name");
            CHECK(nameNode != nullptr, "findById finds an existing name");
            CHECK(loom::findById(r.fabric, "doesNotExist") == nullptr, "findById is nil-safe for a missing name");
            CHECK(loom::findById(nullptr, "name") == nullptr, "findById is nil-safe for a null root");

            auto btn = loom::findByKind(r.fabric, loom::StrandKind::BUTTON);
            CHECK(btn != nullptr && btn->name == "toggle", "findByKind finds the Button");
            CHECK(loom::findByKind(r.fabric, loom::StrandKind::VIDEO) == nullptr, "findByKind is nil-safe when no match exists");

            auto allText = loom::findAllByKind(r.fabric, loom::StrandKind::TEXT);
            CHECK(allText.size() == 1, "findAllByKind returns exactly the matching Strands");

            auto parent = loom::findParentOf(r.fabric, nameNode->id);
            CHECK(parent != nullptr && parent->name == "profile", "findParentOf resolves the immediate parent");
            CHECK(loom::findParentOf(r.fabric, 999999) == nullptr, "findParentOf is nil-safe for an unknown id");
            CHECK(loom::findParentOf(r.fabric, r.fabric->id) == nullptr, "findParentOf on the root itself has no parent");
        }
    }

    // 3. Action Engine verb pairs (enable/disable, select/unselect, focus/blur) --------------
    {
        std::cout << "-- Action Engine: bool-flag verb pairs --\n";
        std::string src = R"(
warp fieldEnabled = true;
warp itemSelected = false;
warp inputFocused = false;

@view.Column=root
  @view.Input=field enabled=fieldEnabled; .end/view
  @view.Card=item selected=itemSelected; .end/view
  @view.Input=box focused=inputFocused; .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::ActionRegistry& reg = loom::ActionRegistry::shared();

            std::vector<rin::ExprPtr> args;
            auto v = std::make_shared<rin::VariableExpr>(); v->name = "fieldEnabled"; args.push_back(v);
            auto o1 = reg.invoke("disable", args, r.warp);
            CHECK(o1.recognized && o1.error.empty(), "disable() recognized");
            CHECK(r.warp.get("fieldEnabled").asString() == "false", "disable() sets cell false");

            std::vector<rin::ExprPtr> args2;
            auto v2 = std::make_shared<rin::VariableExpr>(); v2->name = "itemSelected"; args2.push_back(v2);
            auto o2 = reg.invoke("select", args2, r.warp);
            CHECK(o2.recognized, "select() recognized");
            CHECK(r.warp.get("itemSelected").asString() == "true", "select() sets cell true");

            std::vector<rin::ExprPtr> args3;
            auto v3 = std::make_shared<rin::VariableExpr>(); v3->name = "inputFocused"; args3.push_back(v3);
            reg.invoke("focus", args3, r.warp);
            CHECK(r.warp.get("inputFocused").asString() == "true", "focus() sets cell true");
            auto o3 = reg.invoke("blur", args3, r.warp);
            CHECK(o3.recognized, "blur() recognized");
            CHECK(r.warp.get("inputFocused").asString() == "false", "blur() sets cell back to false");
        }
    }

    // 4. Media actions: play/pause/stop + seek(), and scrollTo() --------------------------------
    {
        std::cout << "-- Action Engine: play/pause/stop/seek/scrollTo --\n";
        std::string src = R"(
warp playing = false;
warp playing_position = 40;
warp scrollY = 0;

@view.Video=clip playing=playing; .end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            loom::ActionRegistry& reg = loom::ActionRegistry::shared();
            auto cellArg = [](const std::string& name) {
                std::vector<rin::ExprPtr> a;
                auto v = std::make_shared<rin::VariableExpr>(); v->name = name; a.push_back(v);
                return a;
            };

            reg.invoke("play", cellArg("playing"), r.warp);
            CHECK(r.warp.get("playing").asString() == "true", "play() sets playing true");

            auto stopOut = reg.invoke("stop", cellArg("playing"), r.warp);
            CHECK(stopOut.recognized, "stop() recognized");
            CHECK(r.warp.get("playing").asString() == "false", "stop() sets playing false");
            CHECK(r.warp.get("playing_position").asNumber(-1) == 0, "stop() resets the matching _position cell to 0");

            std::vector<rin::ExprPtr> seekArgs;
            auto v = std::make_shared<rin::VariableExpr>(); v->name = "playing_position"; seekArgs.push_back(v);
            auto lit = std::make_shared<rin::LiteralExpr>(); lit->kind = rin::LiteralExpr::Kind::NUMBER; lit->number = 75;
            seekArgs.push_back(lit);
            auto seekOut = reg.invoke("seek", seekArgs, r.warp);
            CHECK(seekOut.recognized, "seek() recognized");
            CHECK(r.warp.get("playing_position").asNumber(-1) == 75, "seek() sets numeric position");

            std::vector<rin::ExprPtr> scrollArgs;
            auto sv = std::make_shared<rin::VariableExpr>(); sv->name = "scrollY"; scrollArgs.push_back(sv);
            auto slit = std::make_shared<rin::LiteralExpr>(); slit->kind = rin::LiteralExpr::Kind::NUMBER; slit->number = 300;
            scrollArgs.push_back(slit);
            reg.invoke("scrollTo", scrollArgs, r.warp);
            CHECK(r.warp.get("scrollY").asNumber(-1) == 300, "scrollTo() sets numeric offset");
        }
    }

    // 5. onTap: seq(...) composite action (spec §6's block workaround) ------------------------
    {
        std::cout << "-- onTap: seq(...) composite action --\n";
        std::string src = R"(
warp progress = 0;
warp dialogOpen = false;

@view.Column=root
  @view.Dialog=dlg open=dialogOpen; .end/view
  @view.Button=go label="Go"; onTap=seq(set(progress, 50), show(dialogOpen)); .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto btn = byName(r.fabric, "go");
            auto res = loom::dispatchTap(r.fabric, r.warp, r.program, btn->geometry.x + 1, btn->geometry.y + 1);
            CHECK(res.handled && res.error.empty(), "seq() dispatches without error");
            CHECK(r.warp.get("progress").asNumber(-1) == 50, "seq() applied its first step (set)");
            CHECK(r.warp.get("dialogOpen").asString() == "true", "seq() applied its second step (show)");
        }

        // A seq() step that names an unknown action is a hard error, not a silent skip.
        std::string badSrc = R"(
warp x = 0;
@view.Button=go label="Go"; onTap=seq(bogusAction(x)); .end/view
)";
        auto rb = loom::runColdPipeline(badSrc);
        if (rb.ok) {
            layoutAt(rb);
            auto btn = byName(rb.fabric, "go");
            auto res = loom::dispatchTap(rb.fabric, rb.warp, rb.program, btn->geometry.x + 1, btn->geometry.y + 1);
            CHECK(!res.error.empty(), "seq() surfaces an error for an unrecognized step instead of silently skipping it");
        }
    }

    // 6. NavigationManager: navigate/back/replace/reload, crash-safe on empty/at-start ----------
    {
        std::cout << "-- NavigationManager --\n";
        loom::NavigationManager nav;
        CHECK(nav.current() == "", "current() is empty before any navigate()");
        CHECK(nav.back() == false, "back() on empty history is a safe no-op");

        nav.navigate("home.rin");
        nav.navigate("settings.rin");
        CHECK(nav.current() == "settings.rin", "navigate() pushes and becomes current");
        CHECK(nav.canGoBack(), "canGoBack() true after two navigate()s");

        CHECK(nav.back(), "back() succeeds");
        CHECK(nav.current() == "home.rin", "back() restores the previous route");
        CHECK(nav.back() == false, "back() at the start of history is a safe no-op");

        nav.navigate("profile.rin"); // navigating from a back()'d position discards the forward branch
        CHECK(nav.current() == "profile.rin", "navigate() after back() replaces the forward branch");
        CHECK(nav.history.size() == 2, "the discarded forward entry (settings.rin) is gone");

        nav.replace("profile_edit.rin");
        CHECK(nav.current() == "profile_edit.rin", "replace() swaps the current entry in place");
        CHECK(nav.history.size() == 2, "replace() does not grow history");

        CHECK(nav.reload() == "profile_edit.rin", "reload() reports the current route unchanged");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\n" + std::to_string(failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}
