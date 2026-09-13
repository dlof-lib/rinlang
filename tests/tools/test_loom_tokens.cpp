// tools/test_loom_tokens.cpp — tests for Rin Loom's Design Tokens + Color Engine
// (loom/rin_loom_tokens.h), plus the @theme grammar (rin_ast.h ThemeStmt / rin_parser.cpp
// themeDeclaration) and its wiring into resolveColor()/resolveSpacing()/resolveFontSize()/
// resolveRadius() (loom/rin_loom_paint.h, loom/rin_loom_layout.h).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_tokens.cpp rin_lexer.cpp rin_parser.cpp \
//       diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -o test_loom_tokens
#include "rin_loom_pipeline.h"
#include <cassert>
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static bool sameColor(loom::Color a, loom::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

int main() {
    // 1. Hex parsing -------------------------------------------------------------------------
    {
        std::cout << "-- hex color parsing --\n";
        loom::Color fallback{1, 2, 3};
        CHECK(sameColor(loom::parseHexColor("#7C5CFF", fallback), loom::Color{124, 92, 255}), "valid hex parses");
        CHECK(sameColor(loom::parseHexColor("not-a-color", fallback), fallback), "invalid hex falls back");
        CHECK(loom::looksLikeHexColor("#112233"), "looksLikeHexColor true for '#112233'");
        CHECK(!loom::looksLikeHexColor("primary"), "looksLikeHexColor false for a role name");
    }

    // 2. Built-in theme roles resolve, unknown roles don't -----------------------------------
    {
        std::cout << "-- semantic color resolution (default Dark theme) --\n";
        loom::themeRegistry().setActive("Dark");
        loom::Color out;
        CHECK(loom::resolveSemanticColor("primary", out) && sameColor(out, loom::builtinDarkTheme().primary),
              "'primary' resolves to Dark theme's primary");
        CHECK(loom::resolveSemanticColor("danger", out), "'danger' is a known role");
        CHECK(!loom::resolveSemanticColor("not_a_role", out), "unknown role name returns false, doesn't crash");
    }

    // 3. Spacing tokens ------------------------------------------------------------------------
    {
        std::cout << "-- spacing tokens --\n";
        double v;
        CHECK(loom::resolveSpacingToken("compact", v) && v == 8, "compact == 8");
        CHECK(loom::resolveSpacingToken("comfortable", v) && v == 16, "comfortable == 16");
        CHECK(!loom::resolveSpacingToken("huge", v), "unknown spacing token rejected");
    }

    // 4. Radius tokens -------------------------------------------------------------------------
    {
        std::cout << "-- radius tokens --\n";
        double v;
        CHECK(loom::resolveRadiusToken("sharp", v) && v == 0, "sharp == 0");
        CHECK(loom::resolveRadiusToken("pill", v) && v == 9999, "pill == sentinel (clamped later at paint time)");
    }

    // 5. Typography tokens ----------------------------------------------------------------------
    {
        std::cout << "-- typography tokens --\n";
        loom::TypographyPreset p;
        CHECK(loom::resolveTypographyToken("title", p) && p.size == 20, "title size == 20");
        CHECK(loom::resolveTypographyToken("heading", p) && p.weight == "bold", "heading weight == bold");
    }

    // 6. Full pipeline: @theme block + tone= actually changes rendered colors -------------------
    {
        std::cout << "-- end-to-end: @theme + tone= through the real pipeline --\n";
        std::string src = R"(
@theme=Midnight
  active=true;
  primary="#123456";
.end/theme

@view.Column=root
  @view.Button=b label="Go"; tone=primary; .end/view
  @view.Card=c padding=comfortable; radius=soft; .end/view
.end/view
)";
        auto result = loom::runColdPipeline(src);
        CHECK(result.ok, "pipeline parses and builds fabric successfully");
        if (result.ok) {
            CHECK(loom::themeRegistry().activeName == "Midnight", "@theme=... active=true; switches the active theme");
            auto btn = loom::findAny(result.fabric, [](const loom::StrandPtr& s){ return s->name == "b"; });
            CHECK(btn != nullptr, "button strand found");
            if (btn) CHECK(sameColor(loom::resolveColor(btn), loom::Color{0x12,0x34,0x56}),
                            "tone=primary resolves through the @theme override, not the built-in default");
            auto card = loom::findAny(result.fabric, [](const loom::StrandPtr& s){ return s->name == "c"; });
            CHECK(card != nullptr, "card strand found");
            if (card) {
                CHECK(loom::resolveSpacing(*card, "padding", -1) == 16, "padding=comfortable resolves to 16 in real layout");
                CHECK(loom::resolveRadius(*card, -1) == 6, "radius=soft resolves to 6");
            }
        }
        loom::themeRegistry().setActive("Dark"); // reset for any test that might run after this one
    }

    // 7. Backward compatibility: a .rin file with no @theme/tone/tokens still parses/paints ------
    {
        std::cout << "-- backward compatibility: plain color=\"#RRGGBB\", no tokens at all --\n";
        std::string src = R"(
@view.Card=legacy
  color="#ABCDEF"; padding=10;
.end/view
)";
        auto result = loom::runColdPipeline(src);
        CHECK(result.ok, "legacy-style source (raw hex, raw padding number) still parses");
        if (result.ok) {
            CHECK(sameColor(loom::resolveColor(result.fabric), loom::Color{0xAB, 0xCD, 0xEF}),
                  "raw color=\"#RRGGBB\" still works exactly as before Loom tokens existed");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
