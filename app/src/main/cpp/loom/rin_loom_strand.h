// loom/rin_loom_strand.h — The Fabric (virtual scene graph) + Renderer Engine (AST -> Fabric).
// Consumes rin::ViewStmt directly (see the Loomtime extension in rin_ast.h) — no separate
// duplicate AST, unlike a standalone prototype would use.
#pragma once
#include "rin_loom_eval.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>

namespace loom {

enum class StrandKind {
    TEXT, IMAGE, BUTTON, CARD, COLUMN, ROW, STACK, DIVIDER, CUSTOM
};
inline StrandKind strandKindFromTag(const std::string& tag) {
    if (tag == "Text") return StrandKind::TEXT;
    if (tag == "Image") return StrandKind::IMAGE;
    if (tag == "Button") return StrandKind::BUTTON;
    if (tag == "Card") return StrandKind::CARD;
    if (tag == "Column") return StrandKind::COLUMN;
    if (tag == "Row") return StrandKind::ROW;
    if (tag == "Stack") return StrandKind::STACK;
    if (tag == "Divider") return StrandKind::DIVIDER;
    return StrandKind::CUSTOM; // resolved via Bolt (plugin) registry — see architecture doc §18
}
inline std::string strandKindName(StrandKind k) {
    switch (k) {
        case StrandKind::TEXT: return "Text"; case StrandKind::IMAGE: return "Image";
        case StrandKind::BUTTON: return "Button"; case StrandKind::CARD: return "Card";
        case StrandKind::COLUMN: return "Column"; case StrandKind::ROW: return "Row";
        case StrandKind::STACK: return "Stack"; case StrandKind::DIVIDER: return "Divider";
        default: return "Custom";
    }
}

struct Rect { double x=0,y=0,w=0,h=0; };
struct Constraints { double minW=0,maxW=1e9,minH=0,maxH=1e9; };
using StrandId = uint64_t;

struct ResolvedAttr { std::string key; rin::ExprPtr rawExpr; Value value; };

struct Strand {
    StrandId id = 0;
    StrandKind kind;
    std::string name, customTag;
    int sourceLine = 0;
    std::vector<ResolvedAttr> attrs;
    std::vector<std::shared_ptr<Strand>> children;

    Rect geometry;
    Constraints lastConstraints; bool hasLastConstraints=false;
    uint64_t contentHash=0, lastContentHash=0; bool hasLastContentHash=false;

    const Value* attr(const std::string& k) const {
        for (auto& a : attrs) if (a.key == k) return &a.value;
        return nullptr;
    }
    double attrNum(const std::string& k, double def) const { auto a=attr(k); return a? a->asNumber(def) : def; }
    std::string attrStr(const std::string& k, std::string def="") const { auto a=attr(k); return a? a->asString() : def; }
};
using StrandPtr = std::shared_ptr<Strand>;

inline uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ULL) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
inline StrandId deriveId(const std::string& parentPath, const std::string& name, int idx, StrandKind kind) {
    return fnv1a(parentPath + "/" + (name.empty() ? ("#" + std::to_string(idx)) : name) + ":" + strandKindName(kind));
}

// Tracks, per Warp cell name, which Strands read it while resolving their attributes —
// this is how a runtime warp.set(...) finds exactly the Strands to re-resolve (see rin_loom_shuttle.h).
struct WarpSubscriptions {
    std::unordered_map<std::string, std::vector<StrandId>> byName;
    void record(const std::string& warpName, StrandId id) { byName[warpName].push_back(id); }
};

// ---- Renderer Engine: pure function AST subtree -> Strand subtree ----
inline StrandPtr buildFabric(const std::shared_ptr<rin::ViewStmt>& node, WarpScope& warp,
                              WarpSubscriptions& subs, const std::string& parentPath, int idx) {
    auto s = std::make_shared<Strand>();
    s->kind = strandKindFromTag(node->kindTag);
    if (s->kind == StrandKind::CUSTOM) s->customTag = node->kindTag;
    s->name = node->name;
    s->sourceLine = node->line;
    std::string myPath = parentPath + "/" + (node->name.empty() ? ("#" + std::to_string(idx)) : node->name);
    s->id = deriveId(parentPath, node->name, idx, s->kind);

    uint64_t attrHash = 1469598103934665603ULL;
    for (auto& a : node->attrs) {
        std::vector<std::string> reads;
        Value v = evalAttrExpr(a.value, warp, &reads);
        for (auto& w : reads) subs.record(w, s->id);
        s->attrs.push_back({a.key, a.value, v});
        attrHash = fnv1a(a.key + "=" + v.asString(), attrHash);
    }
    uint64_t childHashAcc = 0; int i = 0;
    for (auto& c : node->children) {
        auto child = buildFabric(c, warp, subs, myPath, i++);
        childHashAcc = fnv1a(std::to_string(child->contentHash), childHashAcc);
        s->children.push_back(child);
    }
    s->contentHash = fnv1a(strandKindName(s->kind), fnv1a(std::to_string(attrHash), childHashAcc));
    return s;
}

// Recomputes contentHash bottom-up for a whole subtree — used after a Warp-driven attribute
// change (which mutates a leaf's Value directly) so ancestor Tension caches invalidate correctly,
// exactly mirroring what the Shuttle does automatically after a source-edit diff.
inline void recomputeHashes(const StrandPtr& s) {
    uint64_t childHashAcc = 0;
    for (auto& c : s->children) { recomputeHashes(c); childHashAcc = fnv1a(std::to_string(c->contentHash), childHashAcc); }
    uint64_t attrHash = 1469598103934665603ULL;
    for (auto& a : s->attrs) attrHash = fnv1a(a.key + "=" + a.value.asString(), attrHash);
    s->contentHash = fnv1a(strandKindName(s->kind), fnv1a(std::to_string(attrHash), childHashAcc));
}

inline int countStrands(const StrandPtr& s) {
    int n = 1;
    for (auto& c : s->children) n += countStrands(c);
    return n;
}
inline StrandPtr findAny(const StrandPtr& s, const std::function<bool(const StrandPtr&)>& pred) {
    if (pred(s)) return s;
    for (auto& c : s->children) { auto r = findAny(c, pred); if (r) return r; }
    return nullptr;
}
inline void buildIndex(const StrandPtr& s, std::unordered_map<StrandId, StrandPtr>& out) {
    out[s->id] = s;
    for (auto& c : s->children) buildIndex(c, out);
}

} // namespace loom
