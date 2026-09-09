// loom/pitok_loom_bridge.h — Phase 2 of the PITOK separation: feeds Loomtime's existing
// Fabric/layout/paint pipeline from pitok::Program directly, instead of from rin::ViewStmt
// (which today only comes from parsing a *full* Rin program with rin::Parser).
//
// What this does NOT change: rin_loom_layout.h, rin_loom_paint.h, rin_loom_needle.h all keep
// consuming the exact same loom::Strand tree they always have. This file is only a second way
// to *produce* that tree — pitok::ViewNode -> loom::Strand — that never touches rin::Lexer,
// rin::Parser, or rin::Interpreter. A source file whose UI is written in the @view/@loop dialect
// can now go: pitok::Lexer -> pitok::Parser -> (this file) -> loom::Strand -> existing
// layout()/paint(), with zero Rin-general-purpose parsing anywhere on that path.
//
// Honest scope note (see pitok/README.md "ما لم يُنجَز بعد"): loom::Strand::role is
// rin::UiRole and loom::ResolvedAttr::rawExpr is rin::ExprPtr — both still literally *named*
// types from rin_ast.h. That coupling is shallow (plain enum + a nullable pointer we simply
// leave null below, since PITOK-built Strands don't support the Shuttle hot-reload re-eval path
// yet — see TODO below) but it is real: a fully Rin-independent Strand type would need those two
// fields generalized too. Flagging this rather than glossing over it.

#pragma once
#include "rin_loom_strand.h"
#include "../../../../../pitok/include/pitok_ast.h"
#include <sstream>

namespace pitok_bridge {

// Mirrors loom::evalAttrExpr (rin_loom_eval.h) field-for-field, but walks pitok::Expr instead
// of rin::Expr. Same four cases, same semantics (including the onTap=fn(arg); "capture as
// descriptor string, don't invoke" rule), so Needle's existing dispatch-by-descriptor-string
// logic (rin_loom_needle.h) needs no changes to handle PITOK-sourced Strands.
inline loom::Value evalPitokExpr(const pitok::ExprPtr& e, const loom::WarpScope& warp,
                                  std::vector<std::string>* readNames = nullptr) {
    if (!e) return loom::Value::txt("");
    switch (e->kind) {
        case pitok::ExprKind::Number:
            return loom::Value::num(e->numValue);
        case pitok::ExprKind::String:
            return loom::Value::txt(e->strValue);
        case pitok::ExprKind::Identifier: {
            if (readNames) readNames->push_back(e->name);
            if (warp.has(e->name)) return warp.get(e->name);
            // Not a known Warp cell -- e.g. a bare event-handler reference (onTap=increment;).
            return loom::Value::txt(e->name);
        }
        case pitok::ExprKind::Binary: {
            loom::Value l = evalPitokExpr(e->left, warp, readNames);
            loom::Value r = evalPitokExpr(e->right, warp, readNames);
            if (e->op == "+") {
                if (l.kind == loom::Value::Kind::NUMBER && r.kind == loom::Value::Kind::NUMBER)
                    return loom::Value::num(l.number + r.number);
                return loom::Value::txt(l.asString() + r.asString()); // '+' string concat, same as Rin
            }
            if (e->op == "-") return loom::Value::num(l.asNumber() - r.asNumber());
            if (e->op == "*") return loom::Value::num(l.asNumber() * r.asNumber());
            if (e->op == "/") return loom::Value::num(r.asNumber() != 0 ? l.asNumber() / r.asNumber() : 0.0);
            return loom::Value::txt(l.asString() + r.asString());
        }
        case pitok::ExprKind::Call: {
            // Captured as a raw "name(args)" descriptor, exactly like evalAttrExpr's CallExpr
            // branch -- Needle matches against this string; nothing here invokes anything.
            std::ostringstream os; os << e->name << "(";
            for (size_t i = 0; i < e->args.size(); i++) {
                if (i) os << ",";
                if (e->args[i]->kind == pitok::ExprKind::Identifier) os << e->args[i]->name;
                else os << evalPitokExpr(e->args[i], warp, readNames).asString();
            }
            os << ")";
            return loom::Value::txt(os.str());
        }
    }
    return loom::Value::txt("<expr>");
}

// Mirrors loom::buildFabric (rin_loom_strand.h) field-for-field for the @view/@loop dialect,
// plus a minimal mapping for @container/@element (see isContainer handling below) now that
// pitok::Parser unifies both dialects under one grammar/AST. Still skips the @element
// child-inheritance defaults (element_width, etc.) -- that convenience is Loop-specific styling
// sugar, not part of the core grammar unification this covers.
//
// TODO (Phase 3): ResolvedAttr::rawExpr is left null for every attr produced here, so a
// PITOK-sourced Strand can be *painted* but not yet *hot-reloaded* via Shuttle::diff (which
// re-walks rawExpr as rin::Expr to detect what changed). Closing this gap means either (a)
// generalizing ResolvedAttr::rawExpr to a variant<rin::ExprPtr, pitok::ExprPtr>, or (b) giving
// Shuttle a content-hash-only diff path that doesn't need rawExpr at all. Not attempted here --
// it changes a type shared with the Rin-sourced path and deserves its own review.
inline loom::StrandPtr buildFabricFromPitok(const pitok::ViewNodePtr& node, loom::WarpScope& warp,
                                             loom::WarpSubscriptions& subs,
                                             const std::string& parentPath, int idx) {
    auto s = std::make_shared<loom::Strand>();
    bool isLoop = (node->kind == "__loop__");
    bool isContainer = (node->kind == "__container__");
    // A Loop is the canvas -> COLUMN (implicit top-to-bottom root), matching Loomtime convention.
    // A Container (older @container/@element dialect) has no visual StrandKind of its own either
    // -- mapped to BOX so it still lays out as a plain box around its @element children, same
    // spirit as Loop's COLUMN fallback. Neither mapping changes anything for genuine @view/@loop
    // source, which never produces these two kind strings.
    if (isLoop) s->kind = loom::StrandKind::COLUMN;
    else if (isContainer) s->kind = loom::StrandKind::BOX;
    else s->kind = loom::strandKindFromTag(node->kind);
    s->role = isLoop ? rin::UiRole::LOOP : rin::UiRole::VIEW;
    s->sourceTag = isLoop ? std::string("Loop") : (isContainer ? std::string("Container") : node->kind);
    if (s->kind == loom::StrandKind::CUSTOM) s->customTag = node->kind;
    s->name = node->name;
    s->mask = "";
    s->sourceLine = node->line;
    std::string myPath = parentPath + "/" + (node->name.empty() ? ("#" + std::to_string(idx)) : node->name);
    s->id = loom::deriveId(parentPath, node->name, idx, s->kind);

    uint64_t attrHash = 1469598103934665603ULL;
    for (auto& a : node->attributes) {
        std::vector<std::string> reads;
        loom::Value v = evalPitokExpr(a.value, warp, &reads);
        for (auto& w : reads) subs.record(w, s->id);
        s->attrs.push_back({a.key, nullptr, v}); // rawExpr left null -- see TODO above
        if (a.key == "mask") s->mask = v.asString();
        attrHash = loom::fnv1a(a.key + "=" + v.asString(), attrHash);
    }
    // on.target.event=action; bindings (older @container/@element dialect): folded into attrs as
    // synthesized "on.<event>" keys (Value holds the captured action descriptor, same string
    // shape evalPitokExpr's Call case already produces for onTap=...). This is a deliberately
    // minimal bridge -- it makes the binding visible/inspectable on the Strand, but does NOT wire
    // it into rin_loom_needle.h's dispatch table yet (Needle today only recognizes onTap/etc.
    // attribute keys straight off the Strand it hit-tests, not a separate bindings list scoped to
    // a *different* named target Strand within the same Container). Real dispatch needs Needle
    // taught to resolve `target` to the sibling Strand it names -- flagged as follow-up, not
    // attempted here since it touches input-handling code this task didn't ask to change.
    for (auto& b : node->bindings) {
        loom::Value v = evalPitokExpr(b.action, warp, nullptr);
        std::string key = "on." + b.target + "." + b.event;
        s->attrs.push_back({key, nullptr, v});
        attrHash = loom::fnv1a(key + "=" + v.asString(), attrHash);
    }

    uint64_t childHashAcc = 0; int i = 0;
    for (auto& c : node->children) {
        auto child = buildFabricFromPitok(c, warp, subs, myPath, i++);
        childHashAcc = loom::fnv1a(std::to_string(child->contentHash), childHashAcc);
        s->children.push_back(child);
    }
    s->contentHash = loom::fnv1a(loom::strandKindName(s->kind), loom::fnv1a(std::to_string(attrHash), childHashAcc));
    return s;
}

struct PitokPipelineResult {
    loom::StrandPtr fabric;
    loom::WarpScope warp;
    loom::WarpSubscriptions subs;
    bool ok = false;
    std::string errorMessage;
    int errorLine = 0;
};

// Full PITOK-only cold pipeline: pitok::Program (already lexed+parsed by pitok::Lexer/Parser,
// see pitok/tools/pitok_parse.cpp for the standalone CLI equivalent) -> loom::Strand ready for
// rin_loom_layout.h's layout() and rin_loom_paint.h's paint(). No rin::Lexer/Parser/Interpreter
// call anywhere in this function.
//
// Limitation (matches PITOK Phase 1's own scope decision): warp initializers are evaluated
// against an empty WarpScope in declaration order, so a later `warp b = a + 1;` referencing an
// earlier `warp a = ...;` on the same file works, but forward references don't (same restriction
// runColdPipeline avoids only because it runs the full rin::Interpreter -- out of scope here by
// design, since pulling in Interpreter would defeat the point of this bridge).
inline PitokPipelineResult runPitokColdPipeline(const pitok::Program& prog) {
    PitokPipelineResult result;
    try {
        for (auto& w : prog.warps) {
            loom::Value v = evalPitokExpr(w.initValue, result.warp, nullptr);
            result.warp.set(w.name, v);
        }
        if (prog.roots.empty()) {
            result.ok = false;
            result.errorMessage = "no top-level @view/@loop root found";
            result.errorLine = 1;
            return result;
        }
        // Same "first top-level view/loop wins" convention as runColdPipeline's fallback scan.
        result.fabric = buildFabricFromPitok(prog.roots.front(), result.warp, result.subs, "", 0);
        result.ok = true;
    } catch (std::exception& e) {
        result.ok = false;
        result.errorMessage = e.what();
        result.errorLine = 0;
    }
    return result;
}

} // namespace pitok_bridge
