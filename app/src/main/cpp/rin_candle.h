#pragma once
// rin_candle.h — سجل Candle (شمعة): طبقة علاقات id-to-id تقع فوق mask/id مباشرة.
// راجع docs/candle.md للتصميم الكامل والمبرر. هذا الملف يطابق ذلك التصميم حرفياً:
//   - light(from, to, relation?) موجَّهة، ∞ من جهة الصادر (fan-out غير محدود برمجياً).
//   - self-loop مرفوضة افتراضياً (light(x, x) == false).
//   - كل استعلامات المسار (chain/depth/tree) محمية بـ visited set لمنع الحلقات اللانهائية
//     حتى لو كوَّن المستخدم دورة فعلية في الرسم البياني (a يشعل b يشعل a).
// هذا الملف مستقل تماماً عن rin_interpreter.*: لا يعرف شيئاً عن mask أو Value أو natives؛
// حلّ (mask|id) -> id يتم في طبقة الربط داخل rin_interpreter.cpp (candleResolve)، تماماً كما
// يفعل maskResolveInternal/maskTarget4 لعلاقات mask v3/v4 الحالية.

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <algorithm>
#include <utility>

namespace rin {

struct CandleEdge {
    std::string to;
    std::string relation; // قد تكون فارغة (بلا وسم)
};

struct CandleTreeNode {
    std::string id;
    std::string relation; // العلاقة من الأب إلى هذه العقدة؛ فارغة للجذر نفسه
    std::vector<CandleTreeNode> children;
};

struct CandleInfo {
    std::vector<std::string> targets;
    std::vector<std::string> sources;
    bool isRoot = false;
    bool isLeaf = false;
};

// سجل عمومي (Candle Registry) على غرار سجلات mask الحالية (containerMasks/maskParents/...).
// المفاتيح هنا هي المعرّفات النهائية بعد الحل (mask -> id أو id مباشرة)، أي نصوص عادية —
// تماماً كما تُعامَل الأقنعة والأسماء في بقية المفسر (لا يوجد نوع StrandId مكشوف لسكربت Rin).
class CandleRegistry {
public:
    // light(from, to, relation) — ينشئ/يحدّث وصلة موجَّهة from -> to.
    // يرفض self-loop (from == to) ويرجع false. استدعاء متكرر لنفس (from,to) يحدّث الوسم فقط.
    bool light(const std::string& from, const std::string& to, const std::string& relation = std::string()) {
        if (from.empty() || to.empty() || from == to) return false;
        auto& out = outgoing_[from];
        for (auto& e : out) {
            if (e.to == to) { e.relation = relation; return true; }
        }
        out.push_back(CandleEdge{to, relation});
        incoming_[to].push_back(from);
        return true;
    }

    // extinguish(from, to) — يحذف وصلة محددة. يرجع true إن وُجدت وحُذفت.
    bool extinguish(const std::string& from, const std::string& to) {
        auto it = outgoing_.find(from);
        if (it == outgoing_.end()) return false;
        auto before = it->second.size();
        it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                              [&](const CandleEdge& e) { return e.to == to; }),
                          it->second.end());
        bool removed = it->second.size() != before;
        if (it->second.empty()) outgoing_.erase(it);
        if (removed) {
            auto it2 = incoming_.find(to);
            if (it2 != incoming_.end()) {
                it2->second.erase(std::remove(it2->second.begin(), it2->second.end(), from), it2->second.end());
                if (it2->second.empty()) incoming_.erase(it2);
            }
        }
        return removed;
    }

    // extinguishAll(from) — يطفئ كل الوصلات الصادرة من عنصر. يرجع عددها.
    int extinguishAll(const std::string& from) {
        auto it = outgoing_.find(from);
        if (it == outgoing_.end()) return 0;
        int n = static_cast<int>(it->second.size());
        for (auto& e : it->second) {
            auto it2 = incoming_.find(e.to);
            if (it2 == incoming_.end()) continue;
            it2->second.erase(std::remove(it2->second.begin(), it2->second.end(), from), it2->second.end());
            if (it2->second.empty()) incoming_.erase(it2);
        }
        outgoing_.erase(it);
        return n;
    }

    bool exists(const std::string& from, const std::string& to) const {
        auto it = outgoing_.find(from);
        if (it == outgoing_.end()) return false;
        for (auto& e : it->second) if (e.to == to) return true;
        return false;
    }

    std::vector<std::string> targets(const std::string& from) const {
        std::vector<std::string> out;
        auto it = outgoing_.find(from);
        if (it != outgoing_.end()) for (auto& e : it->second) out.push_back(e.to);
        return out;
    }

    std::vector<std::string> sources(const std::string& to) const {
        auto it = incoming_.find(to);
        return it == incoming_.end() ? std::vector<std::string>() : it->second;
    }

    int count(const std::string& from) const {
        auto it = outgoing_.find(from);
        return it == outgoing_.end() ? 0 : static_cast<int>(it->second.size());
    }

    bool isRoot(const std::string& id) const {
        return outgoing_.count(id) > 0 && incoming_.count(id) == 0;
    }

    bool isLeaf(const std::string& id) const {
        return incoming_.count(id) > 0 && outgoing_.count(id) == 0;
    }

    CandleInfo info(const std::string& id) const {
        CandleInfo ci;
        ci.targets = targets(id);
        ci.sources = sources(id);
        ci.isRoot = isRoot(id);
        ci.isLeaf = isLeaf(id);
        return ci;
    }

    std::string relation(const std::string& from, const std::string& to) const {
        auto it = outgoing_.find(from);
        if (it == outgoing_.end()) return std::string();
        for (auto& e : it->second) if (e.to == to) return e.relation;
        return std::string();
    }

    // كل الوصلات (from, to) الموسومة بعلاقة معينة (مطابقة تامة للنص).
    std::vector<std::pair<std::string, std::string>> byRelation(const std::string& relation) const {
        std::vector<std::pair<std::string, std::string>> out;
        for (auto& kv : outgoing_)
            for (auto& e : kv.second)
                if (e.relation == relation) out.emplace_back(kv.first, e.to);
        return out;
    }

    // candleChain(from, to) — BFS مع visited set؛ يعيد المسار الكامل [from,...,to] إن وُجد،
    // أو مصفوفة فارغة إن لم يكن to قابلاً للوصول من from عبر أي سلسلة إشعالات.
    std::vector<std::string> chain(const std::string& from, const std::string& to) const {
        if (from.empty() || to.empty()) return {};
        if (from == to) return {from};
        std::unordered_map<std::string, std::string> parent;
        std::unordered_set<std::string> visited{from};
        std::deque<std::string> q{from};
        while (!q.empty()) {
            std::string cur = q.front();
            q.pop_front();
            auto it = outgoing_.find(cur);
            if (it == outgoing_.end()) continue;
            for (auto& e : it->second) {
                if (visited.count(e.to)) continue;
                visited.insert(e.to);
                parent[e.to] = cur;
                if (e.to == to) {
                    std::vector<std::string> path{to};
                    std::string p = cur;
                    while (true) {
                        path.push_back(p);
                        if (p == from) break;
                        p = parent[p];
                    }
                    std::reverse(path.begin(), path.end());
                    return path;
                }
                q.push_back(e.to);
            }
        }
        return {};
    }

    // candleDepth(id) — أقصر عدد قفزات من أقرب جذر يصل إلى id (BFS عكسي عبر incoming،
    // محمي بـ visited set). يرجع 0 إن كان id نفسه جذراً، -1 إن كان غير معروف تماماً أو لا
    // يمكن الوصول إليه من أي جذر فعلي (مثلاً عقدة داخل دورة معزولة بلا جذر خارجي).
    int depth(const std::string& id) const {
        if (id.empty()) return -1;
        if (!outgoing_.count(id) && !incoming_.count(id)) return -1;
        if (isRoot(id)) return 0;
        std::unordered_set<std::string> visited{id};
        std::deque<std::pair<std::string, int>> q;
        q.push_back({id, 0});
        while (!q.empty()) {
            auto [cur, d] = q.front();
            q.pop_front();
            auto it = incoming_.find(cur);
            if (it == incoming_.end()) continue;
            for (auto& src : it->second) {
                if (isRoot(src)) return d + 1;
                if (visited.count(src)) continue;
                visited.insert(src);
                q.push_back({src, d + 1});
            }
        }
        return -1;
    }

    // candleRoots() — عناصر تُشعل غيرها فقط ولم تُشعَل من أحد.
    std::vector<std::string> roots() const {
        std::vector<std::string> out;
        for (auto& kv : outgoing_) if (incoming_.count(kv.first) == 0) out.push_back(kv.first);
        return out;
    }

    // candleLeaves() — عناصر أُشعلت فقط ولا تُشعل غيرها.
    std::vector<std::string> leaves() const {
        std::vector<std::string> out;
        for (auto& kv : incoming_) if (outgoing_.count(kv.first) == 0) out.push_back(kv.first);
        return out;
    }

    // candleTree(root) — تمثيل شجري كامل بدءاً من root، محمي بـ visited set لمنع أي دورة
    // من التسبب بانتشار لانهائي فعلي (عقدة تظهر أكثر من مرة في نفس المسار تُقطع).
    CandleTreeNode tree(const std::string& root) const {
        std::unordered_set<std::string> visited{root};
        return buildTree(root, std::string(), visited);
    }

    bool empty() const { return outgoing_.empty() && incoming_.empty(); }

    void clear() { outgoing_.clear(); incoming_.clear(); }

private:
    CandleTreeNode buildTree(const std::string& id, const std::string& rel,
                              std::unordered_set<std::string>& visited) const {
        CandleTreeNode node{id, rel, {}};
        auto it = outgoing_.find(id);
        if (it == outgoing_.end()) return node;
        for (auto& e : it->second) {
            if (visited.count(e.to)) continue; // منع الحلقات: لا يُعاد زيارة نفس id في نفس المسار
            visited.insert(e.to);
            node.children.push_back(buildTree(e.to, e.relation, visited));
        }
        return node;
    }

    std::unordered_map<std::string, std::vector<CandleEdge>> outgoing_; // id -> وصلاته الصادرة
    std::unordered_map<std::string, std::vector<std::string>> incoming_; // id -> مصادره (لتسريع candleSources)
};

} // namespace rin
