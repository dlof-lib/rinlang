// loom/rin_loom_shuttle.h — The Shuttle: keyed tree diff producing incremental Patch[].
// Diffs a live Strand subtree against a freshly re-parsed rin::ViewStmt subtree — the "new"
// side is real AST, evaluated against the current WarpScope, exactly like the Renderer Engine's
// first (cold) build.
#pragma once
#include "rin_loom_strand.h"

namespace loom {

enum class PatchKind { Insert, Remove, Replace, UpdateAttrs, Reorder };
struct Patch { PatchKind kind; StrandId targetId=0; std::string debugName; std::vector<std::string> changedAttrKeys; };

struct Shuttle {
    std::vector<Patch> patches;

    void diff(StrandPtr oldStrand, const std::shared_ptr<rin::ViewStmt>& newNode,
              WarpScope& warp, WarpSubscriptions& subs, const std::string& parentPath, int idx) {
        StrandKind newKind = strandKindFromTag(newNode->kindTag);
        if (oldStrand->kind != newKind) {
            auto fresh = buildFabric(newNode, warp, subs, parentPath, idx);
            *oldStrand = *fresh;
            patches.push_back({PatchKind::Replace, oldStrand->id, oldStrand->name, {}});
            return;
        }
        std::vector<std::string> changed;
        std::vector<ResolvedAttr> newAttrs;
        for (auto& a : newNode->attrs) {
            std::vector<std::string> reads;
            Value v = evalAttrExpr(a.value, warp, &reads);
            for (auto& w : reads) subs.record(w, oldStrand->id);
            newAttrs.push_back({a.key, a.value, v});
            auto old = oldStrand->attr(a.key);
            if (!old || !(*old == v)) changed.push_back(a.key);
        }
        if (!changed.empty()) {
            oldStrand->attrs = newAttrs;
            patches.push_back({PatchKind::UpdateAttrs, oldStrand->id, oldStrand->name, changed});
        }

        std::string myPath = parentPath + "/" + (newNode->name.empty() ? ("#"+std::to_string(idx)) : newNode->name);
        std::unordered_map<std::string, StrandPtr> oldByKey;
        for (size_t i=0;i<oldStrand->children.size();i++) {
            auto& ch = oldStrand->children[i];
            oldByKey[ch->name.empty() ? ("#"+std::to_string(i)) : ch->name] = ch;
        }
        std::vector<StrandPtr> newChildren;
        int i = 0;
        for (auto& nc : newNode->children) {
            std::string key = nc->name.empty() ? ("#"+std::to_string(i)) : nc->name;
            auto it = oldByKey.find(key);
            if (it != oldByKey.end()) {
                diff(it->second, nc, warp, subs, myPath, i);
                newChildren.push_back(it->second);
                oldByKey.erase(it);
            } else {
                auto fresh = buildFabric(nc, warp, subs, myPath, i);
                patches.push_back({PatchKind::Insert, fresh->id, fresh->name, {}});
                newChildren.push_back(fresh);
            }
            i++;
        }
        for (auto& [key, removed] : oldByKey) patches.push_back({PatchKind::Remove, removed->id, removed->name, {}});
        oldStrand->children = newChildren;

        uint64_t childHashAcc = 0;
        for (auto& c : oldStrand->children) childHashAcc = fnv1a(std::to_string(c->contentHash), childHashAcc);
        uint64_t attrHash = 1469598103934665603ULL;
        for (auto& a : oldStrand->attrs) attrHash = fnv1a(a.key + "=" + a.value.asString(), attrHash);
        oldStrand->contentHash = fnv1a(strandKindName(oldStrand->kind), fnv1a(std::to_string(attrHash), childHashAcc));
    }

    // Runtime (Warp) update path: NOT a source edit. Finds every Strand that read `warpName`
    // while resolving its attributes (recorded in WarpSubscriptions during the last buildFabric/
    // diff pass), genuinely re-evaluates just those attributes' stored rawExpr against the updated
    // WarpScope, and reports which attribute keys actually changed value — proving edit-time and
    // run-time updates share the same incremental machinery (see architecture doc §8).
    void applyWarpChange(const std::string& warpName, WarpScope& warp, WarpSubscriptions& subs,
                          const std::unordered_map<StrandId, StrandPtr>& index) {
        auto it = subs.byName.find(warpName);
        if (it == subs.byName.end()) return;
        for (StrandId id : it->second) {
            auto found = index.find(id);
            if (found == index.end()) continue;
            StrandPtr s = found->second;
            std::vector<std::string> changed;
            for (auto& a : s->attrs) {
                if (!a.rawExpr) continue;
                Value newVal = evalAttrExpr(a.rawExpr, warp, nullptr);
                if (!(newVal == a.value)) { a.value = newVal; changed.push_back(a.key); }
            }
            if (!changed.empty())
                patches.push_back({PatchKind::UpdateAttrs, s->id, s->name, changed});
        }
    }
};

} // namespace loom
