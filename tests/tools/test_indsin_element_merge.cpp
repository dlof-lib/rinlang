// tools/test_indsin_element_merge.cpp — tests for merging `@element` into indsin as a first-class
// root (see pickRootFromProgram() in rin_indsin_pipeline.h). Before this pass, a top-level
// `@element` alone failed with "no top-level '@view/@loop...=name' root found" even though
// buildFabric()/layout()/paint() all already treat an ELEMENT-role Strand identically to a VIEW
// one (strandKindFromTag() never branches on role) -- `@element` was only usable *nested inside* a
// @view, never as the screen itself.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iindsin ../../../../tests/tools/test_indsin_element_merge.cpp \
//       rin_lexer.cpp rin_parser.cpp rin_make.cpp rin_container_sql.cpp rin_artifact.cpp \
//       rin_interpreter.cpp binfmt/elf_format.cpp binfmt/ar_format.cpp binfmt/pe_format.cpp \
//       binfmt/macho_format.cpp binfmt/coff_obj.cpp loader_ui/library_loader_ui.cpp rin_http.cpp \
//       diagnostics/diagnostic.cpp diagnostics/source_manager.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp clc/clc_container.cpp clc/clc_compress.cpp \
//       clc/clc_security.cpp clc/clc_rin_opt.cpp clc/clc_zip_import.cpp clc/sha256.cpp \
//       -lz -o test_element_merge
#include "rin_indsin_pipeline.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

int main() {
    // 1. A standalone top-level @element (no @view/@loop anywhere in the program) now renders as
    //    the screen's own root, instead of failing with "no top-level root found".
    {
        std::cout << "-- standalone @element as root --\n";
        std::string src = R"(
@element.text=hello text="Hi from a standalone element"; .end/element
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses and builds (previously failed: 'no top-level root found')");
        if (r.ok) {
            CHECK(r.fabric != nullptr, "produces a real Fabric root");
            CHECK(r.fabric->kind == indsin::StrandKind::TEXT, "the element's own kindTag resolves to the right StrandKind (role-agnostic, same as @view)");
            CHECK(r.fabric->role == rin::UiRole::ELEMENT, "the root Strand still carries role=ELEMENT (sanitizeElements() still applies to it)");
            indsin::Indsin engine;
            engine.layout(r.fabric, {0, 390, 0, 1e9}, 0, 0);
            CHECK(r.fabric->geometry.w > 0 && r.fabric->geometry.h > 0, "lays out with real, non-zero geometry -- a genuinely usable screen, not a stub");
        }
    }

    // 2. sanitizeElements() still strips visual attrs from an @element used as a root, exactly as
    //    it always has for a nested one -- becoming a root doesn't relax the "functional-only,
    //    no built-in style" rule.
    {
        std::cout << "-- element-as-root still gets sanitized --\n";
        std::string src = R"(
@element.text=hello text="Styled?"; color="#ff0000"; radius=8; .end/element
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            CHECK(r.fabric->attr("color") == nullptr, "color= was stripped, same as it would be nested inside a @view");
            CHECK(r.fabric->attr("radius") == nullptr, "radius= was stripped too");
            CHECK(r.fabric->attrStr("text") == "Styled?", "non-visual, functional attrs (text=) survive sanitization");
        }
    }

    // 3. A real top-level @view still wins over a sibling top-level @element -- this pass only
    //    adds a *fallback*, it never changes which root a program with a real @view already had.
    {
        std::cout << "-- @view still takes priority over a sibling top-level @element --\n";
        std::string src = R"(
@element.text=orphan text="I should not become the root"; .end/element
@view.Card=mainScreen
  @view.Text=heading text="Real screen"; .end/view
.end/view
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            CHECK(r.fabric->name == "mainScreen", "the @view is still picked as root, not the earlier @element");
            CHECK(r.fabric->kind == indsin::StrandKind::CARD, "root kind matches the @view, not the @element");
        }
    }

    // 4. The UI/UX Library Expansion components (added after the original @element.* catalog)
    //    are reachable through @element.* syntax too -- kind resolution was always role-agnostic,
    //    this just proves it end-to-end for the newest additions, including as a standalone root.
    {
        std::cout << "-- new UI/UX components work via @element.* too --\n";
        std::string src = R"(
@element.DonutChart=usage data="Design:40,Development:60"; .end/element
)";
        auto r = indsin::runColdPipeline(src);
        CHECK(r.ok, "parses @element.DonutChart");
        if (r.ok) {
            CHECK(r.fabric->kind == indsin::StrandKind::DONUT_CHART, "resolves to DonutChart's StrandKind via @element, exactly as @view.DonutChart would");
            indsin::Indsin engine;
            engine.layout(r.fabric, {0, 390, 0, 1e9}, 0, 0);
            indsin::Dye dye;
            auto list = dye.paint(r.fabric);
            int arcCount = 0;
            for (auto& cmd : list) if (cmd.op == indsin::DrawOp::STROKE_ARC) arcCount++;
            CHECK(arcCount == 2, "paints its real arc segments (data=\"Design:40,Development:60\" -> 2 arcs) even when used as a standalone @element root");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
