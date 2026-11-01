// tools/test_indsin_collapsible.cpp — tests for the collapsible lists: Accordion, ExpandableList,
// TreeView, NestedList (rin_indsin_collapsible.h + the accordion/expandAll/collapseAll actions).
//
// Build (from app/src/main/cpp), C++23:
//   g++ -std=c++23 -I. -Iindsin ../../../../tests/tools/test_indsin_collapsible.cpp \
//       rin_lexer.cpp rin_parser.cpp rin_make.cpp rin_container_sql.cpp rin_artifact.cpp \
//       rin_interpreter.cpp binfmt/elf_format.cpp binfmt/ar_format.cpp binfmt/pe_format.cpp \
//       binfmt/macho_format.cpp binfmt/coff_obj.cpp loader_ui/library_loader_ui.cpp rin_http.cpp \
//       diagnostics/diagnostic.cpp diagnostics/source_manager.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp clc/clc_container.cpp clc/clc_compress.cpp \
//       clc/clc_security.cpp clc/clc_rin_opt.cpp clc/clc_zip_import.cpp clc/sha256.cpp \
//       -lz -o test_collapsible
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include "rin_indsin_query.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

using indsin::StrandPtr;
static StrandPtr byName(const StrandPtr& root, const std::string& n) {
    return indsin::findAny(root, [&](const StrandPtr& s){ return s->name == n; });
}
static void layoutAt(indsin::PipelineResult& r, double width = 360) {
    indsin::Indsin engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}
// Re-resolve every expression-backed attribute after a warp change (what the Shuttle does in a
// live session), refresh hashes, and lay out again.
static void refresh(indsin::PipelineResult& r) {
    indsin::walkFabric(r.fabric, [&](const StrandPtr& s, int, const indsin::Strand*) {
        for (auto& a : s->attrs) if (a.rawExpr) a.value = indsin::evalAttrExprKeyed(a.key, a.rawExpr, r.warp, nullptr);
    });
    indsin::recomputeHashes(r.fabric);
    layoutAt(r);
}
// Taps the centre of a strand (Needle hit-tests real geometry).
static indsin::TapResult tap(indsin::PipelineResult& r, const StrandPtr& s) {
    auto res = indsin::dispatchTap(r.fabric, r.warp, r.program, s->geometry.x + s->geometry.w / 2, s->geometry.y + s->geometry.h / 2);
    refresh(r);
    return res;
}
static bool isOpen(indsin::PipelineResult& r, const std::string& cell) { return r.warp.get(cell).asString() == "true"; }
static bool shown(const StrandPtr& s) { return s && s->geometry.w > 0.5 && s->geometry.h > 0.5; }
// A strand is really on screen only if it, and every ancestor, is laid out with size and not visible=false.
// (A collapsed container returns size 0 without laying out its children, so a leaf's own geometry is stale.)
static bool reach(const StrandPtr& cur, const std::string& name) {
    if (!cur || cur->attrStr("visible", "true") == "false" || !shown(cur)) return false;
    if (cur->name == name) return true;
    for (auto& c : cur->children) if (reach(c, name)) return true;
    return false;
}
static bool vis(indsin::PipelineResult& r, const std::string& name) { return reach(r.fabric, name); }

int main() {
    // 1. Accordion (single-open): tapping a header opens it and closes the previously open one.
    {
        std::cout << "-- Accordion (exclusive) --\n";
        auto r = indsin::runColdPipeline(R"(
@view.Column=root
  @view.Accordion=faq
    @view.AccordionItem=q1 title="What is Rin?"; subtitle="Basics"; open=true;
      @view.Text=a1 text="A small language."; .end/view
    .end/view
    @view.AccordionItem=q2 title="Where does it run?";
      @view.Text=a2 text="CLI, phone, browser."; .end/view
    .end/view
    @view.AccordionItem=q3 title="Is it free?"; badge="new";
      @view.Text=a3 text="Yes."; .end/view
    .end/view
  .end/view
.end/view
)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto faq = byName(r.fabric, "faq");
            CHECK(faq->kind == indsin::StrandKind::ACCORDION, "Accordion resolves to its own StrandKind");
            CHECK(r.warp.has("q1_open") && r.warp.has("q2_open") && r.warp.has("q3_open"), "each item auto-creates a <name>_open Warp cell");
            CHECK(isOpen(r, "q1_open") && !isOpen(r, "q2_open"), "open=true seeds the cell; others start closed");
            CHECK(vis(r, "a1") && !vis(r, "a2"), "open body is laid out, closed body collapses to zero size");
            CHECK(byName(r.fabric, "q1_header") && byName(r.fabric, "q1_chevron"), "header and chevron are real synthesized strands");
            CHECK(byName(r.fabric, "q1_chevron")->attrStr("text") == "\xE2\x96\xBE", "open item shows the down chevron");
            CHECK(byName(r.fabric, "q2_chevron")->attrStr("text") == "\xE2\x96\xB8", "closed item shows the right chevron");
            double h0 = faq->geometry.h;

            auto t = tap(r, byName(r.fabric, "q2_header"));
            CHECK(t.handled && t.error.empty(), "tapping a header is handled by Needle without error");
            CHECK(isOpen(r, "q2_open") && !isOpen(r, "q1_open"), "opening q2 closes q1 (single-open accordion)");
            CHECK(vis(r, "a2") && !vis(r, "a1"), "layout follows: a2 shown, a1 collapsed");
            CHECK(byName(r.fabric, "q1_chevron")->attrStr("text") == "\xE2\x96\xB8", "q1's chevron flips back as its cell changes");

            tap(r, byName(r.fabric, "q2_header"));
            CHECK(!isOpen(r, "q2_open") && !isOpen(r, "q1_open"), "tapping the open item closes it (all can be closed)");
            CHECK(faq->geometry.h < h0 + 1, "fully collapsed accordion is no taller than with one body open");
        }
    }

    // 2. Accordion (multiple) + controls=true: independent toggles, Expand all / Collapse all.
    {
        std::cout << "-- Accordion (multiple, controls) --\n";
        auto r = indsin::runColdPipeline(R"(
@view.Column=root
  @view.Accordion=acc multiple=true; controls=true; variant="flush"; chevron="plus";
    @view.AccordionItem=a title="A"; @view.Text=ta text="aa"; .end/view .end/view
    @view.AccordionItem=b title="B"; @view.Text=tb text="bb"; .end/view .end/view
  .end/view
.end/view
)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            tap(r, byName(r.fabric, "a_header"));
            tap(r, byName(r.fabric, "b_header"));
            CHECK(isOpen(r, "a_open") && isOpen(r, "b_open"), "multiple=true lets several sections stay open");
            CHECK(byName(r.fabric, "a_chevron")->attrStr("text") == "-", "chevron=plus uses ASCII +/-");
            auto collapse = byName(r.fabric, "acc_collapse_all");
            CHECK(collapse != nullptr, "controls=true synthesizes a Collapse all button");
            tap(r, collapse);
            CHECK(!isOpen(r, "a_open") && !isOpen(r, "b_open"), "collapseAll() closes every section");
            tap(r, byName(r.fabric, "acc_expand_all"));
            CHECK(isOpen(r, "a_open") && isOpen(r, "b_open"), "expandAll() opens every section");
            CHECK(byName(r.fabric, "acc")->attr("border") == nullptr, "variant=flush draws no outer border");
        }
    }

    // 3. Stand-alone AccordionItem bound to the author's own cell (open=<cell>), plus Collapsible alias.
    {
        std::cout << "-- Collapsible / bound open= --\n";
        auto r = indsin::runColdPipeline(R"(
warp showMore = false;
@view.Column=root
  @view.Collapsible=more title="Advanced"; open=showMore;
    @view.Text=inner text="secret"; .end/view
  .end/view
  @view.Button=ext label="external"; onTap=toggle(showMore); .end/view
.end/view
)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(!r.warp.has("more_open"), "open=<cell> reuses the author's cell instead of inventing one");
            CHECK(!vis(r, "inner"), "starts collapsed");
            tap(r, byName(r.fabric, "ext"));
            CHECK(vis(r, "inner"), "flipping the author's own cell from outside expands the item");
            tap(r, byName(r.fabric, "more_header"));
            CHECK(!isOpen(r, "showMore") && !vis(r, "inner"), "tapping the header toggles the same cell back");
        }
    }

    // 4. ExpandableList: header mode and preview mode (limit=).
    {
        std::cout << "-- ExpandableList --\n";
        auto r = indsin::runColdPipeline(R"(
@view.Column=root
  @view.ExpandableList=inbox title="Inbox"; items="Mail 1, Mail 2, Mail 3"; marker=number;
  .end/view
  @view.ExpandableList=feed title="Feed"; items="a;b;c;d;e"; limit=2;
  .end/view
.end/view
)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(byName(r.fabric, "inbox")->kind == indsin::StrandKind::EXPANDABLE_LIST, "ExpandableList resolves to its own StrandKind");
            CHECK(byName(r.fabric, "inbox_badge")->attrStr("text") == "3", "count=true shows the item count as a badge");
            CHECK(!vis(r, "inbox_label1"), "header mode: items start folded away");
            tap(r, byName(r.fabric, "inbox_header"));
            CHECK(vis(r, "inbox_label1") && vis(r, "inbox_label3"), "header mode: tapping the header reveals every item");
            CHECK(byName(r.fabric, "inbox_mark2")->attrStr("text") == "2.", "marker=number numbers the rows");

            CHECK(vis(r, "feed_label1") && vis(r, "feed_label2"), "preview mode: first `limit` rows always visible");
            CHECK(!vis(r, "feed_label3"), "preview mode: remaining rows hidden until expanded");
            auto more = byName(r.fabric, "feed_more");
            CHECK(more && more->attrStr("label") == "Show more (3)", "footer button says how many rows are hidden");
            tap(r, more);
            CHECK(vis(r, "feed_label5"), "tapping the footer reveals the rest");
            CHECK(byName(r.fabric, "feed_more")->attrStr("label") == "Show less", "footer label flips to 'Show less'");
        }
    }

    // 5. TreeView: nested nodes, selection, expanded=all, controls, paths= shorthand.
    {
        std::cout << "-- TreeView --\n";
        auto r = indsin::runColdPipeline(R"(
@view.Column=root
  @view.TreeView=files controls=true;
    @view.TreeNode=src label="src"; open=true;
      @view.TreeNode=main label="main.rin"; .end/view
      @view.TreeNode=lib label="lib";
        @view.TreeNode=math label="math.rin"; .end/view
      .end/view
    .end/view
    @view.TreeNode=readme label="README.md"; badge="2KB"; .end/view
  .end/view
.end/view
)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(byName(r.fabric, "files")->kind == indsin::StrandKind::TREE_VIEW, "TreeView resolves to its own StrandKind");
            CHECK(r.warp.has("src_open") && r.warp.has("lib_open") && !r.warp.has("main_open"), "only branch nodes get open-cells (leaves don't)");
            CHECK(vis(r, "main_label") && !vis(r, "math_label"), "open branch shows children; closed branch hides grandchildren");
            CHECK(byName(r.fabric, "main_row")->geometry.x > byName(r.fabric, "src_row")->geometry.x + 8, "children are indented under their parent");

            // tapping a folder's chevron toggles only; tapping its row toggles AND selects.
            tap(r, byName(r.fabric, "lib_glyph"));
            CHECK(isOpen(r, "lib_open") && r.warp.get("files_selected").asString().empty(), "glyph tap expands without selecting");
            CHECK(vis(r, "math_label"), "grandchild now visible");
            tap(r, byName(r.fabric, "math_row"));
            CHECK(r.warp.get("files_selected").asString() == "math.rin", "tapping a leaf row selects it (files_selected = value)");
            CHECK(byName(r.fabric, "math_row")->attrStr("bg") == "primary", "selected row is highlighted");
            CHECK(byName(r.fabric, "main_row")->attrStr("bg").empty(), "other rows are not highlighted");
            tap(r, byName(r.fabric, "main_row"));
            CHECK(r.warp.get("files_selected").asString() == "main.rin" && byName(r.fabric, "math_row")->attrStr("bg").empty(), "selection moves to the newly tapped row");
            tap(r, byName(r.fabric, "src_row"));
            CHECK(!isOpen(r, "src_open") && r.warp.get("files_selected").asString() == "src", "folder row tap = toggle + select");
            tap(r, byName(r.fabric, "files_expand_all"));
            CHECK(isOpen(r, "src_open") && isOpen(r, "lib_open"), "Expand all opens every branch");
            tap(r, byName(r.fabric, "files_collapse_all"));
            CHECK(!isOpen(r, "src_open") && !isOpen(r, "lib_open"), "Collapse all closes every branch");
        }

        std::cout << "-- TreeView paths= --\n";
        auto p = indsin::runColdPipeline(R"(
@view.Column=root
  @view.TreeView=fs paths="src/main.rin, src/lib/math.rin, docs/guide.md, LICENSE"; expanded=all; sort=true; selected="LICENSE"; .end/view
.end/view
)");
        CHECK(p.ok, "parses");
        if (p.ok) {
            layoutAt(p);
            auto fs = byName(p.fabric, "fs");
            int top = 0; for (auto& c : fs->children) if (c->kind == indsin::StrandKind::TREE_NODE) ++top;
            CHECK(top == 3, "paths= builds one top-level node per root segment (src, docs, LICENSE)");
            CHECK(byName(p.fabric, "fs_src") && byName(p.fabric, "fs_src_lib") && byName(p.fabric, "fs_src_lib_math_rin"), "nested folders/files are created from the path segments");
            CHECK(vis(p, "fs_src_lib_math_rin_label"), "expanded=all opens every folder");
            CHECK(fs->children[0]->name == "fs_docs" || fs->children[0]->name == "fs_src", "sort=true puts folders first");
            CHECK(p.warp.get("fs_selected").asString() == "LICENSE" && byName(p.fabric, "fs_LICENSE_row")->attrStr("bg") == "primary", "selected= seeds the selection and highlights it");
        }
    }

    // 6. NestedList: items= depth markers, numbering, collapsible.
    {
        std::cout << "-- NestedList --\n";
        auto r = indsin::runColdPipeline(R"(
@view.Column=root
  @view.NestedList=plan items="Fruit, -Apple, -Pear, --Williams, Veg, -Carrot"; marker=number; .end/view
  @view.NestedList=toc collapsible=true; open=false;
    @view.NestedItem=ch1 label="Chapter 1";
      @view.NestedItem=s11 label="Section 1.1"; .end/view
    .end/view
    @view.NestedItem=ch2 label="Chapter 2"; .end/view
  .end/view
.end/view
)");
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(byName(r.fabric, "plan")->kind == indsin::StrandKind::NESTED_LIST, "NestedList resolves to its own StrandKind");
            CHECK(byName(r.fabric, "plan_i1_mark")->attrStr("text") == "1.", "top-level numbering: 1.");
            CHECK(byName(r.fabric, "plan_i2_mark")->attrStr("text") == "1.1.", "second level numbering: 1.1.");
            CHECK(byName(r.fabric, "plan_i4_mark")->attrStr("text") == "1.2.1.", "third level numbering: 1.2.1.");
            CHECK(byName(r.fabric, "plan_i5_mark")->attrStr("text") == "2.", "back at the top level: 2.");
            CHECK(byName(r.fabric, "plan_i4_row")->geometry.x > byName(r.fabric, "plan_i2_row")->geometry.x, "deeper levels are indented further");
            CHECK(r.warp.has("ch1_open") && !isOpen(r, "ch1_open") && !r.warp.has("ch2_open"), "collapsible: only items with children get a cell, starting closed (open=false)");
            CHECK(!vis(r, "s11_label"), "sub-items start collapsed");
            tap(r, byName(r.fabric, "ch1_row"));
            CHECK(vis(r, "s11_label"), "tapping the parent row reveals sub-items");
        }
    }

    // 7. Tag syntax works for all four (and kind aliases resolve).
    {
        std::cout << "-- <Tag> syntax --\n";
        auto r = indsin::runColdPipeline(R"(
<Column#root>
  <Accordion#acc>
    <AccordionItem#one title="One" open=true><Text#t1>hello</Text></AccordionItem>
    <AccordionItem#two title="Two"><Text#t2>world</Text></AccordionItem>
  </Accordion>
  <TreeView#tv paths="a/b.txt" expanded="all" />
  <NestedList#nl items="x,-y" />
  <ExpandableList#el items="p,q" title="L" />
</Column>
)");
        CHECK(r.ok, "tag syntax parses");
        if (r.ok) {
            layoutAt(r);
            CHECK(vis(r, "t1") && !vis(r, "t2"), "<Accordion> works in tag syntax");
            CHECK(byName(r.fabric, "tv_a_b_txt") != nullptr, "<TreeView paths=> works in tag syntax");
            CHECK(byName(r.fabric, "nl_i2_label") != nullptr, "<NestedList items=> works in tag syntax");
            CHECK(byName(r.fabric, "el_header") != nullptr, "<ExpandableList> works in tag syntax");
        }
        CHECK(indsin::strandKindFromTag("Tree") == indsin::StrandKind::TREE_VIEW && indsin::strandKindFromTag("Details") == indsin::StrandKind::ACCORDION_ITEM &&
              indsin::strandKindFromTag("Outline") == indsin::StrandKind::NESTED_LIST, "aliases (Tree/Details/Outline) resolve");
    }

    // 8. Action Engine verbs, in isolation.
    {
        std::cout << "-- actions --\n";
        indsin::WarpScope w; w.set("a", indsin::Value::txt("false")); w.set("b", indsin::Value::txt("true")); w.set("c", indsin::Value::txt("true"));
        auto var = [](const std::string& n){ auto v = std::make_shared<rin::VariableExpr>(); v->name = n; return std::static_pointer_cast<rin::Expr>(v); };
        auto o = indsin::ActionRegistry::shared().invoke("accordion", {var("a"), var("b"), var("c")}, w);
        CHECK(o.recognized && w.get("a").asString() == "true" && w.get("b").asString() == "false" && w.get("c").asString() == "false", "accordion(): opening a closes the others");
        CHECK(o.changedWarpNames.size() == 3, "accordion(): reports exactly the cells that changed");
        o = indsin::ActionRegistry::shared().invoke("accordion", {var("a"), var("b")}, w);
        CHECK(w.get("a").asString() == "false" && o.changedWarpNames.size() == 1, "accordion(): closing touches nothing else");
        o = indsin::ActionRegistry::shared().invoke("expandAll", {var("a"), var("b"), var("zzz")}, w);
        CHECK(o.recognized && w.get("a").asString() == "true" && w.get("b").asString() == "true", "expandAll(): opens listed cells, ignores unknown names");
        o = indsin::ActionRegistry::shared().invoke("collapseAll", {var("a"), var("b")}, w);
        CHECK(w.get("a").asString() == "false" && w.get("b").asString() == "false", "collapseAll(): closes listed cells");
    }

    std::cout << (failures ? "\nFAILURES: " : "\nAll collapsible tests passed. failures=") << failures << "\n";
    return failures ? 1 : 0;
}
