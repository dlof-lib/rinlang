// loom/rin_loom_object.h — §21: Object Inspector.
//
// Links the language's `.object("id") ... container.(); .end/object` data literal (rin_ast.h's
// ObjectLiteralStmt, executed by rin::Interpreter — see rin_interpreter.cpp) into an actual
// visible Strand in the Loom live preview, instead of the console-only text box
// `view.print/object(...)` draws today.
//
// Deliberately NOT sharing rin::Interpreter's `objectRegistry` (rin_interpreter.h): Loomtime runs
// its own parallel cold/hot pipeline straight off the parsed `program` (see rin_loom_pipeline.h,
// same reasoning as registerThemesFromProgram in rin_loom_tokens.h) and never runs the
// interpreter at all for a plain `Run`. `loom::objectLiteralRegistry()` below is that pipeline's
// own equivalent, populated by re-scanning the same top-level ObjectLiteralStmt nodes — same
// data, second registry, exactly like Pattern Book's ThemeRegistry duplicates nothing from the
// interpreter either.
//
// Grammar/semantics stay identical to the console preview on purpose: only an object whose body
// contains `container.();` is registered (mirrors the E0035 "لا يوجد كائن مسجَّل بهذا المعرّف"
// check in rin_interpreter.cpp's ViewPrintObjectStmt handling), so `@view.Object=... source="x";`
// and `view.print/object("x");` behave the same way for the same source file.
#pragma once
#include "rin_loom_strand.h"
#include "../rin_ast.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace loom {

// One registered object: its fields in declaration order (field name -> resolved display Value).
// evalAttrExpr() (rin_loom_eval.h) is intentionally the same minimal literal evaluator every other
// @view attribute goes through -- plenty for the field values object literals actually carry in
// practice (strings/numbers/bools/nil), and keeps this file from needing rin::Interpreter at all.
struct ObjectRegistry {
    std::unordered_map<std::string, std::vector<std::pair<std::string, Value>>> objects;
    void clear() { objects.clear(); }
};
// Process-wide, one per parse (re-populated by registerObjectsFromProgram on every cold/hot
// parse) -- same lifetime/scope convention as loom::themeRegistry() in rin_loom_tokens.h.
inline ObjectRegistry& objectLiteralRegistry() { static ObjectRegistry reg; return reg; }

// Called once by the cold pipeline (rin_loom_pipeline.h), right where registerThemesFromProgram
// already runs -- before buildFabric, so an Object Strand's `source=` resolves against a fully
// up-to-date registry regardless of where in the file the `.object(...)` block sits.
inline void registerObjectsFromProgram(const std::vector<rin::StmtPtr>& program, WarpScope& warp) {
    objectLiteralRegistry().clear();
    for (auto& stmt : program) {
        auto o = std::dynamic_pointer_cast<rin::ObjectLiteralStmt>(stmt);
        if (!o || !o->linkToContainer) continue; // same container.(); requirement as view.print/object
        std::vector<std::pair<std::string, Value>> fields;
        fields.reserve(o->fields.size());
        for (auto& f : o->fields) {
            Value v = f.value ? evalAttrExpr(f.value, warp, nullptr) : Value::txt("nil");
            fields.push_back({f.name, v});
        }
        objectLiteralRegistry().objects[o->id] = std::move(fields);
    }
}

// ---- Object Inspector conveniences (§21): source= -> synthesized title + field-row Text children
// Same shape/limitation as applyBannerConveniences (rin_loom_strand.h): runs once on the
// cold-pipeline build path only (see rin_loom_pipeline.h), not on Shuttle::diff's hot-reload path
// yet -- an edit that only changes the referenced object's fields re-synthesizes on the next cold
// build/Run, same as a Banner's title=/message=/closable= would.
inline void applyObjectConveniences(const StrandPtr& s) {
    if (s->kind == StrandKind::OBJECT) {
        std::vector<StrandPtr> rows;
        std::string sourceId = s->attrStr("source", "");
        auto it = objectLiteralRegistry().objects.find(sourceId);

        auto makeText = [&](const std::string& suffix, const std::string& text,
                             const std::string& size, const std::string& tone) {
            auto t = std::make_shared<Strand>();
            t->kind = StrandKind::TEXT; t->name = s->name + "_" + suffix; t->sourceLine = s->sourceLine;
            t->id = deriveId(s->name, suffix, (int)rows.size(), StrandKind::TEXT);
            t->attrs.push_back({"text", nullptr, Value::txt(text)});
            t->attrs.push_back({"size", nullptr, Value::txt(size)});
            t->attrs.push_back({"tone", nullptr, Value::txt(tone)});
            t->contentHash = fnv1a("object_" + suffix + "_" + text);
            return t;
        };

        if (it == objectLiteralRegistry().objects.end()) {
            // Snag-style containment (§16): a missing/unlinked object degrades to a visible,
            // explanatory placeholder Strand instead of an empty box or a hard render failure --
            // same message shape as the interpreter's own E0035 for view.print/object.
            std::string msg = sourceId.empty()
                ? "\xE2\x9A\xA0 @view.Object: missing 'source=\"<id>\"' attribute"
                : "\xE2\x9A\xA0 لا يوجد كائن مسجَّل باسم \"" + sourceId + "\" (يحتاج container.(); داخل .object(\"" + sourceId + "\"))";
            rows.push_back(makeText("missing", msg, "body", "text_muted"));
        } else {
            std::string title = s->attrStr("title", "");
            if (title.empty()) title = "\xF0\x9F\xA7\xA9 " + sourceId; // "🧩 <id>"
            rows.push_back(makeText("title", title, "subtitle", "text"));
            for (auto& kv : it->second) {
                rows.push_back(makeText(kv.first, kv.first + ": " + kv.second.asString(), "body", "text_muted"));
            }
        }
        for (auto& c : s->children) rows.push_back(c); // any hand-authored children render after
        s->children = rows;

        uint64_t childHashAcc = 0;
        for (auto& c : s->children) childHashAcc = fnv1a(std::to_string(c->contentHash), childHashAcc);
        uint64_t attrHash = 1469598103934665603ULL;
        for (auto& a : s->attrs) attrHash = fnv1a(a.key + "=" + a.value.asString(), attrHash);
        s->contentHash = fnv1a(strandKindName(s->kind), fnv1a(std::to_string(attrHash), childHashAcc));
    }
    for (auto& c : s->children) applyObjectConveniences(c);
}

} // namespace loom
