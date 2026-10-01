// tests/tools/test_indsin_system.cpp — اختبارات توسعة Indsin (Design System v2):
//   rin_indsin_system.h      توكنز موسَّعة + أدوات ألوان + مولّد/مدقّق ثيمات
//   rin_indsin_tokens.h      خمسة ثيمات مدمجة جديدة + duration tokens
//   rin_indsin_effects.h     easings جديدة + PULSE/SHAKE/BOUNCE/GROW + stagger
//   rin_indsin_actions.h     copy/swap/cycle/clamp/multiply/negate/append/backspace
//   rin_indsin_query.h       بحث/مسارات/إحصاءات/outline/كتالوج
//   rin_indsin_audit.h       مدقّق إمكانية الوصول والتخطيط
//   rin_indsin_responsive.h  تجاوزات <key>_<breakpoint>
//   rin_indsin_c_api.cpp     الدوال المصدَّرة الجديدة
//
// Build (from app/src/main/cpp) — نفس قائمة المصادر التي يستخدمها desktop target في scripts/build_all.sh:
//   g++ -std=c++17 -I. -Iindsin ../../../../tests/tools/test_indsin_system.cpp \
//       indsin/rin_indsin_c_api.cpp rin_c_api.cpp rin_lexer.cpp rin_parser.cpp rin_make.cpp \
//       rin_interpreter.cpp loader_ui/library_loader_ui.cpp rin_http.cpp diagnostics/*.cpp \
//       clc/clc_container.cpp clc/clc_compress.cpp clc/clc_security.cpp clc/clc_rin_opt.cpp \
//       clc/clc_zip_import.cpp clc/sha256.cpp -lz -o test_indsin_system
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include "rin_indsin_effects.h"
#include "rin_indsin_actions.h"
#include "rin_indsin_system.h"
#include "rin_indsin_query.h"
#include "rin_indsin_audit.h"
#include "rin_indsin_responsive.h"
#include "rin_indsin_c_api.h"
#include <cmath>
#include <iostream>
#include <set>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

using indsin::Color;
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
static bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static indsin::StrandPtr byName(const indsin::StrandPtr& root, const std::string& n) {
    return indsin::findAny(root, [&](const indsin::StrandPtr& s) { return s->name == n; });
}
static void layoutAt(indsin::PipelineResult& r, double width = 390) {
    indsin::Indsin engine;
    r.fabric->screenRoot = true;
    engine.layout(r.fabric, indsin::Constraints{width, width, 0, 1e9}, 0, 0);
}
static std::string takeC(char* p) { std::string s = p ? p : ""; if (p) rin_free_string(p); return s; }

int main() {
    // ====================================================================== 1. tokens
    {
        std::cout << "-- extended tokens --\n";
        indsin::Elevation e;
        CHECK(indsin::resolveElevationToken("md", e) && e.level == 3, "elevation 'md' -> level 3");
        CHECK(indsin::resolveElevationToken("5", e) && e.level == 5, "elevation '5' numeric string");
        CHECK(!indsin::resolveElevationToken("huge", e), "unknown elevation rejected");
        CHECK(indsin::elevationForLevel(99).level == 5 && indsin::elevationForLevel(-4).level == 0, "elevation clamps to 0..5");
        bool mono = true;
        for (int i = 1; i <= 5; i++) mono = mono && indsin::elevationForLevel(i).blur > indsin::elevationForLevel(i - 1).blur;
        CHECK(mono, "elevation blur strictly increases with level");

        int z = 0;
        CHECK(indsin::resolveZLayer("dialog", z) && z == 400, "z layer 'dialog' = 400");
        int zt = 0; indsin::resolveZLayer("tooltip", zt);
        CHECK(zt > z, "tooltip layer above dialog");

        CHECK(indsin::breakpointFor(359) == indsin::Breakpoint::XS, "359 -> xs");
        CHECK(indsin::breakpointFor(360) == indsin::Breakpoint::SM, "360 -> sm");
        CHECK(indsin::breakpointFor(600) == indsin::Breakpoint::MD, "600 -> md");
        CHECK(indsin::breakpointFor(840) == indsin::Breakpoint::LG, "840 -> lg");
        CHECK(indsin::breakpointFor(1200) == indsin::Breakpoint::XL, "1200 -> xl");
        CHECK(std::string(indsin::windowSizeClass(390)) == "compact" && std::string(indsin::windowSizeClass(700)) == "medium" &&
              std::string(indsin::windowSizeClass(1280)) == "expanded", "window size classes");

        double d = -1;
        CHECK(indsin::resolveDurationToken("slow", d) && d == 400, "duration 'slow' = 400ms");
        CHECK(indsin::resolveToken("radius", "pill", d) && d == 9999, "generic resolveToken(radius,pill)");
        CHECK(indsin::resolveToken("breakpoint", "lg", d) && d == 840, "generic resolveToken(breakpoint,lg) = min width");
        CHECK(!indsin::resolveToken("nonsense", "x", d), "unknown token category rejected");
        CHECK(indsin::resolveOpacityToken("disabled", d) && near(d, 0.38), "opacity 'disabled' = 0.38");
        CHECK(indsin::resolveIconSizeToken("lg", d) && d == 24, "icon size 'lg' = 24");
        CHECK(indsin::kMinTouchTarget == 44.0 && indsin::kRecommendedTouchTarget == 48.0, "touch target constants");

        Color surf{40, 42, 54};
        Color up = indsin::elevatedSurface(surf, 4, true);
        CHECK(up.r > surf.r && up.g > surf.g && up.b > surf.b, "dark theme: higher elevation = lighter surface");
        CHECK(indsin::elevatedSurface(surf, 4, false) == surf, "light theme: surface unchanged by elevation");
    }

    // ====================================================================== 2. color tools
    {
        std::cout << "-- color tools --\n";
        Color seed{108, 78, 245};
        auto pal = indsin::tonalPalette(seed);
        CHECK(pal.at(500) == seed, "palette step 500 is exactly the seed");
        bool dec = true;
        for (int i = 1; i < indsin::TonalPalette::kSteps; i++)
            dec = dec && rincolor::relativeLuminance(pal.tones[i]) <= rincolor::relativeLuminance(pal.tones[i - 1]) + 1e-12;
        CHECK(dec, "palette luminance decreases monotonically 50 -> 900");
        CHECK(rincolor::contrastRatio(pal.at(50), Color{255, 255, 255}) < 1.25, "step 50 is near-white");

        Color bg{255, 255, 255}, weak{200, 200, 200};
        Color fixed = indsin::ensureContrast(weak, bg, 4.5);
        CHECK(rincolor::contrastRatio(weak, bg) < 4.5, "precondition: weak grey fails AA on white");
        CHECK(rincolor::contrastRatio(fixed, bg) >= 4.5, "ensureContrast reaches 4.5:1");
        CHECK(rincolor::contrastRatio(fixed, bg) < 5.2, "ensureContrast makes the MINIMAL adjustment");
        Color good{10, 10, 10};
        CHECK(indsin::ensureContrast(good, bg, 4.5) == good, "already-compliant color returned unchanged");
        Color darkBg{20, 20, 28};
        Color fixedDark = indsin::ensureContrast(Color{60, 60, 70}, darkBg, 4.5);
        CHECK(rincolor::contrastRatio(fixedDark, darkBg) >= 4.5, "ensureContrast works on dark backgrounds (goes lighter)");
        CHECK(fixedDark.r > 60, "...by lightening the foreground");

        CHECK(indsin::onColor(Color{250, 250, 250}) == Color({0, 0, 0, 255}), "onColor(light) = black");
        CHECK(indsin::onColor(Color{10, 10, 40}) == Color({255, 255, 255, 255}), "onColor(dark) = white");

        auto comp = indsin::harmonyColors(Color{255, 0, 0}, indsin::Harmony::COMPLEMENTARY);
        CHECK(comp.size() == 1 && comp[0].r < 5 && comp[0].g > 250 && comp[0].b > 250, "complementary of red is cyan");
        CHECK(indsin::harmonyColors(seed, indsin::Harmony::TRIADIC).size() == 2, "triadic gives 2 colors");
        CHECK(indsin::harmonyColors(seed, indsin::Harmony::TETRADIC).size() == 3, "tetradic gives 3 colors");
        indsin::Harmony h;
        CHECK(indsin::harmonyFromName("split", h) && h == indsin::Harmony::SPLIT_COMPLEMENTARY, "harmony name parsing");
    }

    // ====================================================================== 3. themes
    {
        std::cout << "-- themes --\n";
        auto& reg = indsin::themeRegistry();
        for (const char* n : {"HighContrastDark", "HighContrastLight", "Sepia", "Forest", "Rose"})
            CHECK(reg.themes.count(n) == 1, std::string("built-in theme registered: ") + n);

        for (const char* n : {"HighContrastDark", "HighContrastLight"}) {
            const auto& t = reg.themes.at(n);
            CHECK(rincolor::contrastRatio(t.text, t.background) >= 7.0, std::string(n) + ": text/background >= 7:1 (AAA)");
            CHECK(rincolor::contrastRatio(t.text_muted, t.background) >= 7.0, std::string(n) + ": muted text >= 7:1 (AAA)");
        }
        for (const char* n : {"HighContrastDark", "HighContrastLight", "Sepia", "Forest", "Rose", "Dark", "Light", "Midnight", "Ocean"}) {
            auto rep = indsin::validateTheme(reg.themes.at(n));
            CHECK(rep.ok(), std::string("validateTheme has no ERRORS for ") + n);
        }

        // Every theme slot is filled (no default-black leftovers) for the new themes.
        bool allFilled = true;
        for (const char* n : {"HighContrastDark", "HighContrastLight", "Sepia", "Forest", "Rose"}) {
            const auto& t = reg.themes.at(n);
            for (const char* role : {"primary", "secondary", "success", "danger", "warning", "info", "neutral", "surface", "background", "text", "text_muted", "border"}) {
                const Color* c = t.slot(role);
                if (!c) { allFilled = false; continue; }
                // pure black is a legitimate value (HighContrastDark background, HighContrastLight text);
                // "filled" means every role is distinct from a theme's untouched default (all-zero) only
                // when it is NOT one of those two intentional roles.
                bool intentionalBlack = (std::string(n) == "HighContrastDark" && std::string(role) == "background") ||
                                        (std::string(n) == "HighContrastLight" && (std::string(role) == "text" || std::string(role) == "border"));
                if (*c == Color{0, 0, 0, 255} && !intentionalBlack) allFilled = false;
            }
        }
        CHECK(allFilled, "new built-in themes fill every role");

        // themeFromSeed: a spread of seeds, both modes, must all produce error-free themes.
        const char* seeds[] = {"#6C4EF5", "#E53935", "#00897B", "#FFC107", "#1E88E5", "#8E24AA", "#FF7043", "#607D8B", "#00E5FF", "#FFEB3B"};
        bool allOk = true; int checked = 0;
        for (const char* sd : seeds) {
            for (bool dark : {false, true}) {
                auto t = indsin::themeFromSeed("Gen", std::string(sd), dark);
                auto rep = indsin::validateTheme(t);
                checked++;
                if (!rep.ok()) { allOk = false; std::cout << "    seed " << sd << (dark ? " dark" : " light") << " -> " << indsin::themeReportToJson(rep) << "\n"; }
                if (indsin::isDarkTheme(t) != dark) allOk = false;
            }
        }
        CHECK(allOk && checked == 20, "themeFromSeed: 10 seeds x light/dark all pass WCAG checks and keep their mode");

        auto base = reg.themes.at("Dark");
        std::vector<std::string> unknown;
        auto derived = indsin::deriveTheme(base, "Brand", {{"primary", Color{255, 0, 128}}, {"bogusRole", Color{1, 2, 3}}}, &unknown);
        CHECK(derived.name == "Brand" && derived.primary == Color({255, 0, 128, 255}), "deriveTheme overrides primary");
        CHECK(derived.surface == base.surface && derived.text == base.text, "deriveTheme inherits everything else");
        CHECK(unknown.size() == 1 && unknown[0] == "bogusRole", "deriveTheme reports unknown roles");

        // validateTheme must actually catch a bad theme.
        auto bad = base; bad.text = bad.background; // invisible text
        auto badRep = indsin::validateTheme(bad);
        CHECK(!badRep.ok(), "validateTheme flags invisible text as an error");
        bool hasCode = false; for (auto& i : badRep.issues) if (i.code == "THEME-TEXT-BG") hasCode = true;
        CHECK(hasCode, "...with code THEME-TEXT-BG");
        CHECK(badRep.score() < indsin::validateTheme(base).score(), "bad theme scores lower than a good one");

        std::string tj = indsin::themeToJson(reg.themes.at("Sepia"));
        CHECK(contains(tj, "\"name\":\"Sepia\"") && contains(tj, "\"primary\":\"#") && contains(tj, "\"dark\":false"), "themeToJson shape");

        // @theme activation of a new built-in theme via real .rin source
        auto r = indsin::runColdPipeline("@theme=Sepia\n  active=true;\n.end/theme\n\n@view.Column=root\n  @view.Text=t\n  text=\"hi\";\n  .end/view\n.end/view\n");
        CHECK(r.ok && reg.activeName == "Sepia", "@theme=Sepia active=true activates the new built-in theme");
        reg.setActive("Dark");
    }

    // ====================================================================== 4. effects
    {
        std::cout << "-- effects / easing --\n";
        using indsin::Easing;
        Easing all[] = {Easing::LINEAR, Easing::EASE_IN, Easing::EASE_OUT, Easing::EASE_IN_OUT, Easing::CUBIC_IN, Easing::CUBIC_OUT,
                        Easing::CUBIC_IN_OUT, Easing::SINE_IN_OUT, Easing::BACK_OUT, Easing::BOUNCE_OUT, Easing::ELASTIC_OUT, Easing::SPRING};
        bool ends = true;
        for (auto e : all) ends = ends && near(indsin::applyEasing(e, 0.0), 0.0, 1e-9) && near(indsin::applyEasing(e, 1.0), 1.0, 1e-9);
        CHECK(ends, "every easing maps 0->0 and 1->1");

        bool mono = true; double prev = -1;
        for (int i = 0; i <= 100; i++) { double v = indsin::applyEasing(Easing::CUBIC_IN_OUT, i / 100.0); mono = mono && v >= prev - 1e-12; prev = v; }
        CHECK(mono, "cubicInOut is monotonic");
        double maxBack = 0; for (int i = 0; i <= 200; i++) maxBack = std::max(maxBack, indsin::applyEasing(Easing::BACK_OUT, i / 200.0));
        CHECK(maxBack > 1.05 && maxBack < 1.2, "backOut overshoots ~10% (by design)");
        double maxBounce = 0; for (int i = 0; i <= 200; i++) maxBounce = std::max(maxBounce, indsin::applyEasing(Easing::BOUNCE_OUT, i / 200.0));
        CHECK(maxBounce <= 1.0 + 1e-9, "bounceOut never exceeds 1.0");
        CHECK(near(indsin::applyEasing(Easing::SINE_IN_OUT, 0.5), 0.5, 1e-9), "sine midpoint = 0.5");
        CHECK(near(indsin::applyEasing(Easing::CUBIC_OUT, 0.5), 0.875, 1e-9), "cubicOut(0.5) = 0.875");

        CHECK(indsin::easingFromString("spring") == Easing::SPRING && indsin::easingFromString("elastic") == Easing::ELASTIC_OUT &&
              indsin::easingFromString("cubic-in-out") == Easing::CUBIC_IN_OUT, "new easing names parse");
        CHECK(indsin::easingFromString("easeOut") == Easing::EASE_OUT && indsin::easingFromString("???") == Easing::EASE_OUT, "old easing names / default unchanged");
        CHECK(indsin::effectKindFromString("pulse") == indsin::EffectKind::PULSE && indsin::effectKindFromString("pop") == indsin::EffectKind::GROW &&
              indsin::effectKindFromString("fade") == indsin::EffectKind::FADE, "new effect kinds parse, old ones unchanged");

        indsin::EffectSpec sp; sp.easing = Easing::SPRING;
        bool opOk = true;
        for (auto k : {indsin::EffectKind::FADE, indsin::EffectKind::SCALE, indsin::EffectKind::GROW}) {
            sp.kind = k;
            for (auto e : all) { sp.easing = e; for (int i = 0; i <= 50; i++) { double o = indsin::evaluateEffect(sp, i / 50.0).opacity; opOk = opOk && o >= 0 && o <= 1; } }
        }
        CHECK(opOk, "opacity stays within [0,1] even with overshooting easings");

        sp.easing = Easing::LINEAR;
        sp.kind = indsin::EffectKind::PULSE;
        CHECK(near(indsin::evaluateEffect(sp, 0.0).scale, 1.0) && near(indsin::evaluateEffect(sp, 1.0).scale, 1.0, 1e-9), "pulse starts and ends at scale 1");
        CHECK(indsin::evaluateEffect(sp, 0.5).scale > 1.07, "pulse peaks mid-way");
        sp.kind = indsin::EffectKind::SHAKE;
        CHECK(near(indsin::evaluateEffect(sp, 1.0).dx, 0.0, 1e-9) && std::fabs(indsin::evaluateEffect(sp, 0.04).dx) > 0.5, "shake damps to 0");
        sp.kind = indsin::EffectKind::BOUNCE;
        CHECK(indsin::evaluateEffect(sp, 0.1).dy < 0 && near(indsin::evaluateEffect(sp, 1.0).dy, 0.0, 1e-9), "bounce hops up (negative dy) then settles");
        sp.kind = indsin::EffectKind::GROW;
        auto g0 = indsin::evaluateEffect(sp, 0.0), g1 = indsin::evaluateEffect(sp, 1.0);
        CHECK(g0.scale < 0.05 && near(g0.opacity, 0.0) && near(g1.scale, 1.0) && near(g1.opacity, 1.0), "grow goes 0 -> 1 in scale and opacity");

        // duration tokens + stagger through real fabric ---------------------------------------
        auto r = indsin::runColdPipeline(R"(
@view.Column=root
  effect="fade"; duration="slow"; stagger=100;
  @view.Text=a
    text="A";
  .end/view
  @view.Text=b
    text="B";
  .end/view
  @view.Text=c
    text="C";
  .end/view
.end/view
)");
        CHECK(r.ok, "effect+stagger source parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(indsin::effectSpecOf(*r.fabric).durationMs == 400, "duration=\"slow\" -> 400ms");
            CHECK(indsin::effectSpecOf(*r.fabric).staggerMs == 100, "stagger attr read");
            indsin::EffectRuntime rt;
            indsin::applyEffectsToFabric(r.fabric, rt, 0.0);
            // t=250ms: A (delay 0) is well under way; B (delay 100) less; C (delay 200) barely started.
            indsin::applyEffectsToFabric(r.fabric, rt, 250.0);
            auto op = [&](const char* n) { return byName(r.fabric, n)->attrNum("opacity", 1.0); };
            CHECK(op("a") > op("b") && op("b") > op("c"), "stagger: children reveal in order (a > b > c at t=250ms)");
            CHECK(op("c") < 0.5, "last child still early in its animation");
            indsin::applyEffectsToFabric(r.fabric, rt, 2000.0);
            CHECK(near(op("a"), 1.0) && near(op("b"), 1.0) && near(op("c"), 1.0), "all settle to opacity 1 after the stagger window");
        }
    }

    // ====================================================================== 5. actions
    {
        std::cout << "-- new actions --\n";
        auto run = [](const std::string& src, const std::string& button) {
            auto r = indsin::runColdPipeline(src);
            struct Out { indsin::PipelineResult r; indsin::TapResult t; };
            Out o; o.r = std::move(r);
            if (!o.r.ok) return o;
            layoutAt(o.r, 390);
            auto b = byName(o.r.fabric, button);
            o.t = indsin::dispatchTap(o.r.fabric, o.r.warp, o.r.program, b->geometry.x + 1, b->geometry.y + 1);
            return o;
        };
        auto mk = [](const std::string& warps, const std::string& handler) {
            return warps + "\n@view.Column=root\n  @view.Button=go label=\"Go\"; onTap=" + handler + "; .end/view\n.end/view\n";
        };
        {
            auto o = run(mk("warp a = 1;\nwarp b = 2;", "swap(a,b)"), "go");
            CHECK(o.r.ok && o.t.handled && o.t.error.empty(), "swap() dispatches");
            CHECK(o.r.warp.get("a").asNumber() == 2 && o.r.warp.get("b").asNumber() == 1, "swap(a,b) exchanges values");
            CHECK(o.t.changedWarpNames.size() == 2, "swap reports both cells changed");
        }
        {
            auto o = run(mk("warp src = \"hello\";\nwarp dst = \"\";", "copy(dst,src)"), "go");
            CHECK(o.t.handled && o.r.warp.get("dst").asString() == "hello", "copy(dst,src) copies the value");
        }
        {
            auto o = run(mk("warp mode = \"list\";", "cycle(mode,\"list\",\"grid\",\"table\")"), "go");
            CHECK(o.t.handled && o.r.warp.get("mode").asString() == "grid", "cycle() list -> grid");
            auto o2 = run(mk("warp mode = \"table\";", "cycle(mode,\"list\",\"grid\",\"table\")"), "go");
            CHECK(o2.r.warp.get("mode").asString() == "list", "cycle() wraps table -> list");
            auto o3 = run(mk("warp mode = \"zzz\";", "cycle(mode,\"list\",\"grid\",\"table\")"), "go");
            CHECK(o3.r.warp.get("mode").asString() == "list", "cycle() unknown current -> first value");
        }
        {
            auto hi = run(mk("warp n = 150;", "clamp(n,0,100)"), "go");
            CHECK(hi.r.warp.get("n").asNumber() == 100, "clamp() caps at max");
            auto lo = run(mk("warp n = -7;", "clamp(n,0,100)"), "go");
            CHECK(lo.r.warp.get("n").asNumber() == 0, "clamp() raises to min");
            auto in = run(mk("warp n = 42;", "clamp(n,0,100)"), "go");
            CHECK(in.r.warp.get("n").asNumber() == 42, "clamp() leaves in-range values");
            auto sw = run(mk("warp n = 500;", "clamp(n,100,0)"), "go");
            CHECK(sw.r.warp.get("n").asNumber() == 100, "clamp() tolerates swapped bounds");
        }
        {
            auto m = run(mk("warp n = 6;", "multiply(n,7)"), "go");
            CHECK(m.r.warp.get("n").asNumber() == 42, "multiply(n,7)");
            auto m2 = run(mk("warp n = 6;", "multiply(n)"), "go");
            CHECK(m2.r.warp.get("n").asNumber() == 12, "multiply(n) defaults to x2");
            auto ng = run(mk("warp n = 5;", "negate(n)"), "go");
            CHECK(ng.r.warp.get("n").asNumber() == -5, "negate(n)");
        }
        {
            auto ap = run(mk("warp s = \"ab\";", "append(s,\"cd\")"), "go");
            CHECK(ap.r.warp.get("s").asString() == "abcd", "append(s,\"cd\")");
            auto bs = run(mk("warp s = \"abc\";", "backspace(s)"), "go");
            CHECK(bs.r.warp.get("s").asString() == "ab", "backspace removes last ASCII char");
            auto ar = run(mk("warp s = \"سلام\";", "backspace(s)"), "go");
            CHECK(ar.r.warp.get("s").asString() == "سلا", "backspace is UTF-8 aware (Arabic letter removed whole)");
            auto em = run(mk("warp s = \"\";", "backspace(s)"), "go");
            CHECK(em.t.handled && em.r.warp.get("s").asString().empty(), "backspace on empty string is a no-op");
        }
        {
            auto seqr = run(mk("warp n = 10;\nwarp s = \"x\";", "seq(multiply(n,3),append(s,\"y\"),clamp(n,0,20))"), "go");
            CHECK(seqr.t.handled && seqr.t.error.empty() && seqr.r.warp.get("n").asNumber() == 20 && seqr.r.warp.get("s").asString() == "xy",
                  "new actions compose inside seq()");
        }
        // pre-existing actions untouched
        {
            auto inc = run(mk("warp n = 1;", "increment(n,4)"), "go");
            CHECK(inc.r.warp.get("n").asNumber() == 5, "existing increment() still works");
        }
    }

    // ====================================================================== 6. query
    const std::string kScreen = R"(
@view.Column=root
  @view.Text=title
    text="Welcome"; size="title";
  .end/view
  @view.Card=panel
    @view.Text=body
      text="Hello world";
    .end/view
    @view.Button=savebtn
      label="Save"; width=120; height=48;
    .end/view
    @view.Input=email
      placeholder="Email";
    .end/view
  .end/view
  @view.Image=logo
    alt="Logo"; width=40; height=40;
  .end/view
.end/view
)";
    {
        std::cout << "-- query --\n";
        auto r = indsin::runColdPipeline(kScreen);
        CHECK(r.ok, "query fixture parses");
        if (r.ok) {
            layoutAt(r);
            auto st = indsin::fabricStats(r.fabric);
            CHECK(st.nodes == 7, "fabricStats counts all 7 nodes");
            CHECK(st.maxDepth == 2, "fabricStats maxDepth = 2");
            CHECK(st.leaves == 5, "fabricStats leaves = 5 (title, body, save, email, logo)");
            CHECK(st.byKind["Text"] == 2 && st.byKind["Button"] == 1, "byKind counts");
            CHECK(st.byCategory["input"] == 1 && st.byCategory["action"] == 1 && st.interactive == 2, "byCategory + interactive counts (Button, Input)");

            auto save = byName(r.fabric, "savebtn");
            CHECK(indsin::pathString(r.fabric, save.get()) == "Column:root/Card:panel/Button:savebtn", "pathString root->target");
            CHECK(indsin::findByName(r.fabric, "email") != nullptr && indsin::findByName(r.fabric, "nope") == nullptr, "findByName hit/miss");
            CHECK(indsin::findByStrandId(r.fabric, save->id) == save, "findByStrandId");
            CHECK(indsin::findAllByCategory(r.fabric, indsin::KindCategory::TEXT).size() == 2, "findAllByCategory(TEXT) = 2");
            CHECK(indsin::findAllByAttr(r.fabric, "alt").size() == 1 && indsin::findAllByAttr(r.fabric, "alt", "Logo").size() == 1 &&
                  indsin::findAllByAttr(r.fabric, "alt", "Other").empty(), "findAllByAttr key / key+value");
            CHECK(!indsin::findAllByKind(r.fabric, indsin::StrandKind::TEXT).empty(), "existing findAllByKind still reused");

            double px = save->geometry.x + save->geometry.w / 2, py = save->geometry.y + save->geometry.h / 2;
            auto hit = indsin::deepestAt(r.fabric, px, py);
            CHECK(hit && hit->name == "savebtn", "deepestAt picks the Button under the point");
            CHECK(indsin::deepestAt(r.fabric, -50, -50) == nullptr, "deepestAt outside the screen = null");

            std::string outline = indsin::fabricOutline(r.fabric);
            CHECK(contains(outline, "Column 'root' [layout]") && contains(outline, "  Card 'panel'") && contains(outline, "\"Save\""), "outline shows structure, category and accessible name");

            std::string cat = indsin::kindCatalogJson();
            CHECK(contains(cat, "{\"kind\":\"Button\",\"category\":\"action\",\"role\":\"button\",\"interactive\":true") &&
                  contains(cat, "{\"kind\":\"Dialog\",\"category\":\"overlay\""), "catalog JSON has categories/roles/flags");
            // every non-CUSTOM kind is classified (none fall through to "custom" by accident)
            bool classified = true;
            for (int i = 0; i < (int)indsin::StrandKind::CUSTOM; i++)
                if (indsin::kindCategoryOf((indsin::StrandKind)i) == indsin::KindCategory::CUSTOM) classified = false;
            CHECK(classified, "every built-in StrandKind has a real category");
            CHECK(contains(indsin::fabricStatsJson(st), "\"nodes\":7"), "stats JSON");
        }
    }

    // ====================================================================== 7. audit
    {
        std::cout << "-- audit --\n";
        auto r = indsin::runColdPipeline(kScreen);
        if (r.ok) {
            layoutAt(r);
            auto rep = indsin::auditFabric(r.fabric);
            CHECK(rep.passed() && rep.count("A11Y-001") == 0 && rep.count("A11Y-002") == 0, "clean screen: no label/alt errors");
            CHECK(rep.score() >= 90, "clean screen scores >= 90");
        }
        std::string bad = R"(
@view.Column=root
  @view.Button=nolabel
    width=30; height=20;
  .end/view
  @view.Button=nolabel2
    label="OK";
  .end/view
  @view.Image=pic
    width=50; height=50;
  .end/view
  @view.Image=deco
    decorative=true; width=10; height=10;
  .end/view
  @view.Text=faint
    text="barely visible"; color="#d6d6e0";
  .end/view
  @view.Text=empty
    text="";
  .end/view
  @view.Text=dup
    text="one";
  .end/view
  @view.Text=dup
    text="two";
  .end/view
  @view.Dialog=dlg
    open=true;
  .end/view
.end/view
)";
        auto b = indsin::runColdPipeline(bad);
        CHECK(b.ok, "bad fixture parses");
        if (b.ok) {
            indsin::themeRegistry().setActive("Light");
            layoutAt(b);
            auto rep = indsin::auditFabric(b.fabric);
            CHECK(rep.count("A11Y-001") == 1, "A11Y-001 flags exactly the Button with no label");
            CHECK(rep.count("A11Y-002") == 1, "A11Y-002 flags the Image without alt (decorative one exempt)");
            CHECK(rep.count("A11Y-003") >= 1, "A11Y-003 flags the 30x20 touch target");
            CHECK(rep.count("A11Y-004") >= 1, "A11Y-004 flags low-contrast text on the Light theme");
            CHECK(rep.count("A11Y-005") == 1, "A11Y-005 flags the untitled Dialog");
            CHECK(rep.count("STRUCT-001") == 1, "STRUCT-001 flags duplicate sibling name");
            CHECK(rep.count("STRUCT-002") == 1, "STRUCT-002 flags the empty Text");
            CHECK(!rep.passed() && rep.score() < 60, "bad screen fails with a low score");
            bool hintOk = false;
            for (auto& i : rep.issues) if (i.code == "A11Y-004") hintOk = contains(i.hint, "#");
            CHECK(hintOk, "contrast issue suggests a compliant color");
            bool pathOk = false;
            for (auto& i : rep.issues) if (i.code == "A11Y-001") pathOk = (i.path == "Column:root/Button:nolabel");
            CHECK(pathOk, "issues carry a readable path");
            std::string js = indsin::auditReportToJson(rep);
            CHECK(contains(js, "\"ok\":false") && contains(js, "\"code\":\"A11Y-001\"") && contains(js, "\"hint\":"), "audit JSON shape");
            indsin::themeRegistry().setActive("Dark");
        }
        // overflow detection
        auto o = indsin::runColdPipeline("@view.Column=root\n  @view.Row=r\n    @view.Box=wide\n  width=600; height=40;\n  .end/view\n  .end/view\n.end/view\n");
        if (o.ok) {
            layoutAt(o, 390);
            auto rep = indsin::auditFabric(o.fabric);
            CHECK(rep.count("LAYOUT-001") >= 1, "LAYOUT-001 flags a 600px box on a 390px screen");
        }
        // dark-theme contrast must be evaluated against the dark background (no false positive)
        auto d = indsin::runColdPipeline("@view.Column=root\n  @view.Text=t\n  text=\"readable\";\n  .end/view\n.end/view\n");
        if (d.ok) {
            indsin::themeRegistry().setActive("Dark");
            layoutAt(d);
            CHECK(indsin::auditFabric(d.fabric).count("A11Y-004") == 0, "default text on the Dark theme passes contrast (no false positive)");
        }
    }

    // ====================================================================== 8. responsive
    {
        std::cout << "-- responsive attrs --\n";
        std::string src = R"(
@view.Grid=cards
  columns=1; columns_md=2; columns_lg=3; columns_xl=4;
  gap=8; gap_lg=24;
  @view.Box=c1
    height=40;
  .end/view
  @view.Box=c2
    height=40;
  .end/view
  @view.Box=c3
    height=40;
  .end/view
  @view.Box=c4
    height=40;
  .end/view
.end/view
)";
        auto mkGrid = [&](double w) {
            auto r = indsin::runColdPipeline(src);
            r.fabric->screenRoot = true;
            indsin::applyResponsiveAttrs(r.fabric, w);
            indsin::Indsin eng;
            eng.layout(r.fabric, indsin::Constraints{w, w, 0, 1e9}, 0, 0);
            return r;
        };
        auto narrow = mkGrid(390);
        auto mid = mkGrid(700);
        auto wide = mkGrid(1000);
        auto huge = mkGrid(1400);
        CHECK(narrow.ok, "responsive fixture parses");
        CHECK(narrow.fabric->attrNum("columns", 0) == 1, "390px (sm): base columns=1");
        CHECK(mid.fabric->attrNum("columns", 0) == 2, "700px (md): columns_md applies");
        CHECK(wide.fabric->attrNum("columns", 0) == 3, "1000px (lg): columns_lg applies");
        CHECK(huge.fabric->attrNum("columns", 0) == 4, "1400px (xl): columns_xl applies");
        CHECK(narrow.fabric->attrNum("gap", 0) == 8 && mid.fabric->attrNum("gap", 0) == 8 && wide.fabric->attrNum("gap", 0) == 24,
              "gap: base until lg, then 24 (mobile-first cascade)");
        // real layout consequence: 1 column stacks vertically, 4 columns put all cells on one row.
        double yN = byName(narrow.fabric, "c4")->geometry.y, yH = byName(huge.fabric, "c4")->geometry.y;
        double yH1 = byName(huge.fabric, "c1")->geometry.y;
        CHECK(yN > byName(narrow.fabric, "c1")->geometry.y + 40, "1 column: c4 is below c1");
        CHECK(near(yH, yH1), "4 columns: c4 shares c1's row");

        // idempotent + reversible: re-applying at the same width changes nothing; going back restores the base
        auto r = indsin::runColdPipeline(src);
        int first = indsin::applyResponsiveAttrs(r.fabric, 1000);
        int again = indsin::applyResponsiveAttrs(r.fabric, 1000);
        CHECK(first > 0 && again == 0, "applying twice at the same width is a no-op");
        indsin::applyResponsiveAttrs(r.fabric, 390);
        CHECK(r.fabric->attrNum("columns", 0) == 1 && r.fabric->attrNum("gap", 0) == 8, "shrinking back restores base values");
        // attribute that exists ONLY as an override is removed again when it stops applying
        auto r2 = indsin::runColdPipeline("@view.Column=root\n  padding_lg=30;\n  @view.Text=t\n  text=\"x\";\n  .end/view\n.end/view\n");
        indsin::applyResponsiveAttrs(r2.fabric, 1000);
        CHECK(r2.fabric->attr("padding") != nullptr && r2.fabric->attrNum("padding", 0) == 30, "override-only attr is created at lg");
        indsin::applyResponsiveAttrs(r2.fabric, 390);
        CHECK(r2.fabric->attr("padding") == nullptr, "...and removed again below lg");
    }

    // ====================================================================== 9. C API
    {
        std::cout << "-- C API --\n";
        const char* src = "@view.Column=root\n  @view.Button=nolabel\n  width=30; height=20;\n  .end/view\n  @view.Text=t\n  text=\"hi\";\n  .end/view\n.end/view\n";
        void* sess = rin_indsin_session_create(src, 390);
        CHECK(sess != nullptr, "session created");
        std::string audit = takeC(rin_indsin_session_audit_json(sess));
        CHECK(contains(audit, "\"code\":\"A11Y-001\"") && contains(audit, "\"ok\":false"), "session audit JSON reports the unlabeled Button");
        std::string stats = takeC(rin_indsin_session_stats_json(sess));
        CHECK(contains(stats, "\"nodes\":3"), "session stats JSON");
        std::string outline = takeC(rin_indsin_session_outline(sess));
        CHECK(contains(outline, "Column 'root'") && contains(outline, "Button 'nolabel'"), "session outline text");
        rin_indsin_session_free(sess);

        std::string nullAudit = takeC(rin_indsin_session_audit_json(nullptr));
        CHECK(contains(nullAudit, "\"ok\":false"), "null session audit returns an error object, no crash");

        std::string tokens = takeC(rin_indsin_tokens_json());
        CHECK(contains(tokens, "\"breakpoint\":{\"xs\":0,\"sm\":360,\"md\":600,\"lg\":840,\"xl\":1200}"), "tokens JSON: breakpoints");
        CHECK(contains(tokens, "\"elevation\":[{\"level\":0") && contains(tokens, "\"touchTarget\":{\"min\":44"), "tokens JSON: elevation + touch target");
        CHECK(contains(tokens, "\"Sepia\"") && contains(tokens, "\"HighContrastDark\""), "tokens JSON lists the new themes");

        std::string cat = takeC(rin_indsin_catalog_json());
        CHECK(contains(cat, "\"kind\":\"Rating\""), "catalog JSON via C API");

        std::string pal = takeC(rin_indsin_palette_json("#6C4EF5"));
        CHECK(contains(pal, "\"500\":\"#6c4ef5\"") || contains(pal, "\"500\":\"#6C4EF5\""), "palette JSON: step 500 is the seed");
        CHECK(contains(takeC(rin_indsin_palette_json("not-a-color")), "invalid color"), "palette JSON rejects a bad seed");

        std::string gen = takeC(rin_indsin_theme_from_seed_json("#00897B", 1));
        CHECK(contains(gen, "\"theme\":{\"name\":\"GeneratedDark\",\"dark\":true") && contains(gen, "\"report\":{\"ok\":true"), "theme-from-seed JSON: dark theme, report ok");
        std::string val = takeC(rin_indsin_validate_theme_json("HighContrastDark"));
        CHECK(contains(val, "\"ok\":true") && contains(val, "\"score\":100"), "validate JSON: HighContrastDark is perfect");
        CHECK(contains(takeC(rin_indsin_validate_theme_json("Nope")), "unknown theme"), "validate JSON: unknown theme");

        // responsive attrs flow through the session API (real relayout path)
        const char* grid = "@view.Grid=g\n  columns=1; columns_md=2;\n  @view.Box=a\n  height=30;\n  .end/view\n  @view.Box=b\n  height=30;\n  .end/view\n.end/view\n";
        void* sNarrow = rin_indsin_session_create(grid, 390);
        void* sMid = rin_indsin_session_create(grid, 700);
        auto yOf = [](void* s, const char* name) {
            std::string js = takeC(rin_indsin_session_render_json(s));
            size_t p = js.find(std::string("\"name\":\"") + name + "\"");
            size_t yp = js.find("\"y\":", p);
            return std::atof(js.c_str() + yp + 4);
        };
        CHECK(yOf(sNarrow, "b") > yOf(sNarrow, "a"), "session @390: two cells stacked (1 column)");
        CHECK(near(yOf(sMid, "b"), yOf(sMid, "a")), "session @700: two cells side by side (columns_md=2)");
        rin_indsin_session_free(sNarrow);
        rin_indsin_session_free(sMid);
    }

    std::cout << (failures == 0 ? "\nALL PASSED (indsin_system)\n" : "\nFAILURES: " + std::to_string(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
