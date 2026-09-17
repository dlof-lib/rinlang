// tools/test_loom_group_object.cpp — §21b: Containers.Group/Volume as a data source for the
// existing @view.Object widget (source="<group or volume name>" renders one title + field-rows
// card per member container, stacked under the group's own title).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_group_object.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_interpreter.cpp rin_http.cpp rin_make.cpp \
//       diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz -o test_loom_group_object
#include "rin_loom_pipeline.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static loom::StrandPtr byName(const loom::StrandPtr& root, const std::string& n) {
    return loom::findAny(root, [&](const loom::StrandPtr& s){ return s->name == n; });
}
// Collects every Text strand's rendered text= under a root, in tree order (depth-first) --
// enough to check the synthesized title/card/field rows appeared in the right order.
static void collectTexts(const loom::StrandPtr& s, std::vector<std::string>& out) {
    if (!s) return;
    if (s->kind == loom::StrandKind::TEXT) out.push_back(s->attrStr("text", ""));
    for (auto& c : s->children) collectTexts(c, out);
}

int main() {
    // 1. @view.Object source="<Containers.Group name>" -> one card per member container --------
    {
        std::cout << "-- Containers.Group as @view.Object source --\n";
        std::string src = R"(
@Containers.Group=team
    @container=alice
        text role = "lead";
    .end/container
    @container=bob
        text role = "dev";
    .end/container
.end/Containers.Group

@view.Object=roster source="team";
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            auto roster = byName(r.fabric, "roster");
            CHECK(roster != nullptr, "the @view.Object strand exists");
            std::vector<std::string> texts;
            collectTexts(r.fabric, texts);
            std::string joined;
            for (auto& t : texts) joined += t + "\n";
            CHECK(joined.find("team") != std::string::npos, "group title mentions the group name");
            CHECK(joined.find("alice") != std::string::npos, "alice's card title appears");
            CHECK(joined.find("role: lead") != std::string::npos, "alice's field row appears");
            CHECK(joined.find("bob") != std::string::npos, "bob's card title appears");
            CHECK(joined.find("role: dev") != std::string::npos, "bob's field row appears");
        }
    }

    // 2. Nested Containers.Group inside the group is flattened (no intermediate row for it) -----
    {
        std::cout << "-- nested sub-group flattens to its containers --\n";
        std::string src = R"(
@Containers.Group=company
    @container=hq
        text city = "cairo";
    .end/container
    @Containers.Group=finance
        @container=payroll
            text status = "active";
        .end/container
    .end/Containers.Group
.end/Containers.Group

@view.Object=chart source="company";
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            std::vector<std::string> texts;
            collectTexts(r.fabric, texts);
            std::string joined;
            for (auto& t : texts) joined += t + "\n";
            CHECK(joined.find("hq") != std::string::npos, "top-level container row appears");
            CHECK(joined.find("payroll") != std::string::npos, "nested sub-group's container flattens through");
            CHECK(joined.find("status: active") != std::string::npos, "flattened container's own field appears");
            CHECK(joined.find("finance") == std::string::npos, "the intermediate sub-group name itself is NOT rendered as its own row");
        }
    }

    // 3. @Volume works exactly the same way as Containers.Group -------------------------------
    {
        std::cout << "-- Volume as @view.Object source --\n";
        std::string src = R"(
@Volume=archive
    @container=logs
        text label = "old logs";
    .end/container
.end/Volume

@view.Object=v source="archive";
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            std::vector<std::string> texts;
            collectTexts(r.fabric, texts);
            std::string joined;
            for (auto& t : texts) joined += t + "\n";
            CHECK(joined.find("logs") != std::string::npos, "Volume's member container row appears");
            CHECK(joined.find("label: old logs") != std::string::npos, "Volume member's field row appears");
        }
    }

    // 4. A single .object(...) id still takes priority / still works unaffected -----------------
    {
        std::cout << "-- plain .object(...) source is unaffected --\n";
        std::string src = R"(
.object("u1")
    name("Sam");
    container.();
.end/object

@view.Object=card source="u1";
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            std::vector<std::string> texts;
            collectTexts(r.fabric, texts);
            std::string joined;
            for (auto& t : texts) joined += t + "\n";
            CHECK(joined.find("u1") != std::string::npos, "single-object title still renders as before");
            CHECK(joined.find("name: Sam") != std::string::npos, "single-object field still renders as before");
        }
    }

    // 5. An unknown source= still degrades to the explanatory placeholder -----------------------
    {
        std::cout << "-- unknown source= still shows the missing placeholder --\n";
        std::string src = R"(
@view.Object=ghost source="nope";
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            std::vector<std::string> texts;
            collectTexts(r.fabric, texts);
            bool sawPlaceholder = false;
            for (auto& t : texts) if (t.find("nope") != std::string::npos) sawPlaceholder = true;
            CHECK(sawPlaceholder, "unknown source= still degrades to a visible placeholder, not a crash");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL PASSED" : (std::to_string(failures) + " FAILURE(S)")) << "\n";
    return failures == 0 ? 0 : 1;
}
