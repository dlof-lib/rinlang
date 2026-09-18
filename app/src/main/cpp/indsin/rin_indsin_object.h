// indsin/rin_indsin_object.h — §21: Object Inspector.
//
// Links the language's `.object("id") ... container.(); .end/object` data literal (rin_ast.h's
// ObjectLiteralStmt, executed by rin::Interpreter — see rin_interpreter.cpp) into an actual
// visible Strand in the Indsin live preview, instead of the console-only text box
// `view.print/object(...)` draws today.
//
// Deliberately NOT sharing rin::Interpreter's `objectRegistry` (rin_interpreter.h): Indsintime runs
// its own parallel cold/hot pipeline straight off the parsed `program` (see rin_indsin_pipeline.h,
// same reasoning as registerThemesFromProgram in rin_indsin_tokens.h) and never runs the
// interpreter at all for a plain `Run`. `indsin::objectLiteralRegistry()` below is that pipeline's
// own equivalent, populated by re-scanning the same top-level ObjectLiteralStmt nodes — same
// data, second registry, exactly like Pattern Book's ThemeRegistry duplicates nothing from the
// interpreter either.
//
// Grammar/semantics stay identical to the console preview on purpose: only an object whose body
// contains `container.();` is registered (mirrors the E0035 "لا يوجد كائن مسجَّل بهذا المعرّف"
// check in rin_interpreter.cpp's ViewPrintObjectStmt handling), so `@view.Object=... source="x";`
// and `view.print/object("x");` behave the same way for the same source file.
#pragma once
#include "rin_indsin_strand.h"
#include "../rin_ast.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace indsin {

// One registered object: its fields in declaration order (field name -> resolved display Value).
// evalAttrExpr() (rin_indsin_eval.h) is intentionally the same minimal literal evaluator every other
// @view attribute goes through -- plenty for the field values object literals actually carry in
// practice (strings/numbers/bools/nil), and keeps this file from needing rin::Interpreter at all.
struct ObjectRegistry {
    std::unordered_map<std::string, std::vector<std::pair<std::string, Value>>> objects;
    void clear() { objects.clear(); }
};
// Process-wide, one per parse (re-populated by registerObjectsFromProgram on every cold/hot
// parse) -- same lifetime/scope convention as indsin::themeRegistry() in rin_indsin_tokens.h.
inline ObjectRegistry& objectLiteralRegistry() { static ObjectRegistry reg; return reg; }

// Called once by the cold pipeline (rin_indsin_pipeline.h), right where registerThemesFromProgram
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

// ---- §21b: Containers.Group / Volume as a data source for the same @view.Object widget -------
// Extends the Object Inspector above so `source=` can point at a whole `@Containers.Group=name`
// or `@Volume=name` block, not just a single `.object("id")` -- rendering one title + field-rows
// card per member container, stacked under one shared group title. Deliberately reuses the exact
// same StrandKind::OBJECT widget and `source=` attribute (no new @view tag, no new StrandKind):
// from the author's perspective a Group/Volume is just a richer thing to point `source=` at.
//
// Same "own parallel registry, static AST walk, no interpreter" philosophy as objectLiteralRegistry
// above: Containers.Group/Volume already carry this exact membership/nesting shape at the
// interpreter level (see groupMembers/volumeMembers + collectGroupContainerNames/
// collectVolumeContainerNames in rin_interpreter.cpp, and the groupSnapshot()/volumeSnapshot()
// natives built on top of them) -- registerGroupsFromProgram below is that same shape, computed
// once more directly from the parsed AST for Indsintime's benefit, exactly like
// registerObjectsFromProgram is a second, independent copy of ObjectLiteralStmt's own data.
struct GroupRegistry {
    // اسم Containers.Group أو Volume -> صفوفها (كل صف: اسم الحاوية العضو + حقولها المباشرة
    // بترتيب الإعلان)، مفلطحة عبر أي مجموعة/Volume فرعية متداخلة بداخلها (بنفس تفرّع
    // collectGroupContainerNames/collectVolumeContainerNames في rin_interpreter.cpp، لكن هنا
    // مباشرة على شجرة AST المُحلَّلة بدل containers/groupMembers وقت التشغيل).
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::vector<std::pair<std::string, Value>>>>> groups;
    void clear() { groups.clear(); }
};
inline GroupRegistry& groupLiteralRegistry() { static GroupRegistry reg; return reg; }

// حقول حاوية عضو واحدة: TextStmt/LetStmt المباشرة فقط في جسمها (لا تكرار على حاويات متداخلة
// بداخلها -- تلك تُمثَّل كصفوف منفصلة بأسمائها هي)، مُقيَّمة بنفس evalAttrExpr المصغَّر المستخدَم
// لكل سمة @view أخرى.
inline std::vector<std::pair<std::string, Value>> indsinExtractContainerFields(const std::vector<rin::StmtPtr>& body, WarpScope& warp) {
    std::vector<std::pair<std::string, Value>> fields;
    for (auto& st : body) {
        if (auto t = std::dynamic_pointer_cast<rin::TextStmt>(st)) {
            fields.push_back({t->name, t->initializer ? evalAttrExpr(t->initializer, warp, nullptr) : Value::txt("nil")});
        } else if (auto l = std::dynamic_pointer_cast<rin::LetStmt>(st)) {
            fields.push_back({l->name, l->initializer ? evalAttrExpr(l->initializer, warp, nullptr) : Value::txt("nil")});
        }
    }
    return fields;
}

// يتفرّع بشكل متكرر عبر جسم Containers.Group/Volume: أي @container مباشرة بداخله يصبح صفاً
// (اسمه + حقوله)، وأي @Containers.Group/@Volume فرعية متداخلة بداخله تُفرَّط بنفس الآلية (بلا
// أي صف خاص بها هي -- تماماً كـ collectGroupContainerNames وقت التشغيل، الذي يُرجع الحاويات
// الفعلية فقط لا أسماء المجموعات الوسيطة).
inline void indsinCollectGroupRows(const std::vector<rin::StmtPtr>& body, WarpScope& warp,
                                  std::vector<std::pair<std::string, std::vector<std::pair<std::string, Value>>>>& out) {
    for (auto& st : body) {
        if (auto g = std::dynamic_pointer_cast<rin::ContainerGroupStmt>(st)) {
            indsinCollectGroupRows(g->body, warp, out);
        } else if (auto v = std::dynamic_pointer_cast<rin::VolumeStmt>(st)) {
            indsinCollectGroupRows(v->body, warp, out);
        } else if (auto c = std::dynamic_pointer_cast<rin::ContainerStmt>(st)) {
            out.push_back({c->name.empty() ? std::string("#") + std::to_string(out.size()) : c->name,
                            indsinExtractContainerFields(c->body, warp)});
        }
    }
}

// يُستدعى من الأنبوب البارد/الساخن (rin_indsin_pipeline.h)، بنفس مكان استدعاء
// registerObjectsFromProgram/registerThemesFromProgram تماماً -- قبل buildFabric، حتى يُحلَّ
// source= لأي @view.Object يشير إلى مجموعة/Volume بأحدث حالة من الملف بصرف النظر عن مكان
// تعريفها فيه. فقط @Containers.Group/@Volume المُسمّاة (name غير فارغ) على مستوى الملف الأعلى
// تُسجَّل هنا (بنفس قيد `container.();` على .object أعلاه: بلا اسم يعني بلا source= ممكن أصلاً).
inline void registerGroupsFromProgram(const std::vector<rin::StmtPtr>& program, WarpScope& warp) {
    groupLiteralRegistry().clear();
    for (auto& stmt : program) {
        std::string name; const std::vector<rin::StmtPtr>* body = nullptr;
        if (auto g = std::dynamic_pointer_cast<rin::ContainerGroupStmt>(stmt)) { name = g->name; body = &g->body; }
        else if (auto v = std::dynamic_pointer_cast<rin::VolumeStmt>(stmt)) { name = v->name; body = &v->body; }
        if (name.empty() || !body) continue;
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, Value>>>> rows;
        indsinCollectGroupRows(*body, warp, rows);
        groupLiteralRegistry().groups[name] = std::move(rows);
    }
}

// ---- Object Inspector conveniences (§21): source= -> synthesized title + field-row Text children
// Same shape/limitation as applyBannerConveniences (rin_indsin_strand.h): runs once on the
// cold-pipeline build path only (see rin_indsin_pipeline.h), not on Shuttle::diff's hot-reload path
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
            // §21b: لم يوجَد ككائن مفرد -- جرّب المجموعة/Volume قبل عرض رسالة "مفقود".
            auto git = groupLiteralRegistry().groups.find(sourceId);
            if (git != groupLiteralRegistry().groups.end()) {
                std::string title = s->attrStr("title", "");
                if (title.empty()) title = "\xF0\x9F\x93\x82 " + sourceId; // "📂 <id>"
                rows.push_back(makeText("title", title, "subtitle", "text"));
                if (git->second.empty()) {
                    rows.push_back(makeText("empty", "(لا حاويات في هذه المجموعة)", "body", "text_muted"));
                } else {
                    for (auto& member : git->second) {
                        rows.push_back(makeText(member.first + "_card_title", "\xF0\x9F\xA7\xA9 " + member.first, "body", "text"));
                        for (auto& kv : member.second) {
                            rows.push_back(makeText(member.first + "_" + kv.first,
                                "   " + kv.first + ": " + kv.second.asString(), "body", "text_muted"));
                        }
                    }
                }
            } else {
            // Snag-style containment (§16): a missing/unlinked object degrades to a visible,
            // explanatory placeholder Strand instead of an empty box or a hard render failure --
            // same message shape as the interpreter's own E0035 for view.print/object.
            std::string msg = sourceId.empty()
                ? "\xE2\x9A\xA0 @view.Object: missing 'source=\"<id>\"' attribute"
                : "\xE2\x9A\xA0 لا يوجد كائن أو مجموعة/Volume مسجَّلة باسم \"" + sourceId + "\" (يحتاج container.(); داخل .object(\"" + sourceId + "\") -- أو @Containers.Group/@Volume=\"" + sourceId + "\")";
            rows.push_back(makeText("missing", msg, "body", "text_muted"));
            }
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

} // namespace indsin
