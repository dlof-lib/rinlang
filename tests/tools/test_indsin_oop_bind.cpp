// tests/tools/test_indsin_oop_bind.cpp — ربط كائنات OOP بخلايا Warp الخاصة بـ Indsin (oop.bindWarp*, oop.bindView)
// عبر مسار Indsin الحقيقي: runColdPipelineWithRuntime (تشغيل البرنامج على مفسّر الجلسة) ثم dispatchTap
// الذي ينقل خلايا Warp إلى المفسّر قبل المعالج ويقرؤها بعده (rin_indsin_needle.h).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iindsin ../../../../tests/tools/test_indsin_oop_bind.cpp \
//       rin_lexer.cpp rin_parser.cpp rin_make.cpp rin_container_sql.cpp rin_artifact.cpp rin_interpreter.cpp \
//       toolchain/rin_toolchain.cpp binfmt/*.cpp loader_ui/library_loader_ui.cpp rin_http.cpp diagnostics/*.cpp \
//       clc/clc_container.cpp clc/clc_compress.cpp clc/clc_security.cpp clc/clc_rin_opt.cpp clc/clc_zip_import.cpp \
//       clc/sha256.cpp ../../../../cli/linux/src/pkg/*.cpp -lz -ldl -o test_indsin_oop_bind
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include <algorithm>
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static indsin::StrandPtr byName(const indsin::StrandPtr& root, const std::string& n) {
    return indsin::findAny(root, [&](const indsin::StrandPtr& s) { return s->name == n; });
}
static bool has(const std::vector<std::string>& v, const std::string& n) { return std::find(v.begin(), v.end(), n) != v.end(); }

static const char* SRC = R"RIN(
warp count = 0;
warp label = "idle";
warp ui_title = "T";
warp ui_size = 3;

class Model {
    let n = 0;
    let msg = "idle";
    fun init() {}
    fun bump() { self.n = self.n + 1; self.msg = "bumped " + str(self.n); }
}
class Form { let title = ""; let size = 0; fun init() {} }

let model = Model();
let form = Form();
oop.bindWarpTwoWay(model, "n", "count");
oop.bindWarp(model, "msg", "label");
oop.bindView(form, "ui_");

fun inc()      { model.bump(); }
fun peek()     { model.msg = "n=" + str(model.n); }
fun editForm() { form.title = "Hello"; form.size = form.size + 10; }
fun readForm() { model.msg = form.title + ":" + str(form.size); }

@view.Column=root
  @view.Button=incBtn label="inc"; onTap=inc(); .end/view
  @view.Button=peekBtn label="peek"; onTap=peek(); .end/view
  @view.Button=editBtn label="edit"; onTap=editForm(); .end/view
  @view.Button=readBtn label="read"; onTap=readForm(); .end/view
.end/view
)RIN";

int main() {
    rin::Interpreter interp;
    auto r = indsin::runColdPipelineWithRuntime(SRC, interp);
    CHECK(r.ok, "program runs on the session interpreter (error: " + r.errorMessage + ")");
    if (!r.ok) return 1;
    {
        indsin::Indsin engine;
        r.fabric->screenRoot = true;
        engine.layout(r.fabric, indsin::Constraints{390, 390, 0, 1e9}, 0, 0);
    }
    bool seeded = true; // runColdPipelineWithRuntime already ran the program on `interp`
    auto tap = [&](const std::string& btn) {
        auto b = byName(r.fabric, btn);
        return indsin::dispatchTap(r.fabric, r.warp, r.program, b->geometry.x + 1, b->geometry.y + 1,
                                   nullptr, nullptr, &interp, &seeded);
    };
    auto objField = [&](const std::string& var, const std::string& f) -> rin::Value {
        auto g = interp.exportGlobals();
        auto it = g.find(var);
        if (it == g.end() || it->second.type != rin::Value::Type::INSTANCE) return rin::Value::nil();
        auto fit = it->second.instance->fields.find(f);
        return fit == it->second.instance->fields.end() ? rin::Value::nil() : fit->second;
    };

    std::cout << "-- cold render: bindings did their initial sync (object -> cell) --\n";
    CHECK(r.warp.get("count").number == 0, "warp count = object.n (0)");
    CHECK(r.warp.get("label").str == "idle", "warp label = object.msg");
    CHECK(r.warp.get("ui_title").str == "", "bindView: object.title ('') overwrote cell ui_title");
    CHECK(r.warp.get("ui_size").number == 0, "bindView: object.size (0) overwrote cell ui_size");

    std::cout << "-- handler mutates the object -> cells change -> Indsin sees them --\n";
    {
        auto t = tap("incBtn");
        CHECK(t.error.empty(), "inc handler ran without error (" + t.error + ")");
        CHECK(r.warp.get("count").number == 1, "cell count follows object.n after bump()");
        CHECK(r.warp.get("label").str == "bumped 1", "cell label follows object.msg");
        CHECK(has(t.changedWarpNames, "count") && has(t.changedWarpNames, "label"),
              "changedWarpNames lists both cells (this is what triggers the UI re-resolve)");
        CHECK(objField("model", "n").number == 1, "object state intact");
    }

    std::cout << "-- UI changes a cell -> object sees it before the next handler runs --\n";
    {
        r.warp.set("count", indsin::Value::num(7)); // e.g. an input/stepper wrote the cell
        auto t = tap("peekBtn");
        CHECK(t.error.empty(), "peek handler ran without error (" + t.error + ")");
        CHECK(r.warp.get("label").str == "n=7", "handler read object.n == 7 (pulled from the cell)");
        CHECK(objField("model", "n").number == 7, "object.n updated");
        CHECK(r.warp.get("count").number == 7, "no echo: cell stays 7");
    }

    std::cout << "-- chained: bump after a UI edit continues from the UI value --\n";
    {
        tap("incBtn");
        CHECK(r.warp.get("count").number == 8, "bump() continued from 7 -> 8");
        CHECK(r.warp.get("label").str == "bumped 8", "label reflects it");
    }

    std::cout << "-- bindView (two-way, prefix mode) --\n";
    {
        auto t = tap("editBtn");
        CHECK(t.error.empty(), "edit handler ran (" + t.error + ")");
        CHECK(r.warp.get("ui_title").str == "Hello", "cell ui_title <- form.title");
        CHECK(r.warp.get("ui_size").number == 10, "cell ui_size <- form.size");
        CHECK(has(t.changedWarpNames, "ui_title") && has(t.changedWarpNames, "ui_size"), "both reported as changed");

        r.warp.set("ui_size", indsin::Value::num(99)); // UI edits one cell
        tap("readBtn");
        CHECK(objField("form", "size").number == 99, "form.size pulled from the cell");
        CHECK(r.warp.get("label").str == "Hello:99", "handler saw the pulled state");
    }

    std::cout << (failures == 0 ? "\nALL PASSED\n" : "\nFAILURES: ") ;
    if (failures) std::cout << failures << "\n";
    return failures == 0 ? 0 : 1;
}
