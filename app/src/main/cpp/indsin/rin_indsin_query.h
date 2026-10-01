// indsin/rin_indsin_query.h — استعلام وفحص شجرة الـ Fabric (Introspection API).
//
// أدوات «للقراءة فقط» فوق شجرة Strand الناتجة (لا تُعدِّل أي شيء): البحث بالاسم/النوع/الخاصية،
// مسار أي عنصر من الجذر، إحصاءات الشجرة، مخطط نصي (outline) للتشخيص، الضرب بالإحداثيات
// (أعمق عنصر تحت نقطة)، وكتالوج المكوّنات المصنَّفة (taxonomy) الذي تستهلكه أدوات المحرّر
// والتوثيق. يُستخدم أيضاً من rin_indsin_audit.h.
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace indsin {

// ---- تصنيف المكوّنات (Component taxonomy) ---------------------------------------------------
enum class KindCategory {
    LAYOUT, STRUCTURE, TEXT, MEDIA, INPUT, ACTION, FEEDBACK, NAVIGATION, DATA, OVERLAY, CUSTOM
};
inline const char* kindCategoryName(KindCategory c) {
    switch (c) {
        case KindCategory::LAYOUT:     return "layout";
        case KindCategory::STRUCTURE:  return "structure";
        case KindCategory::TEXT:       return "text";
        case KindCategory::MEDIA:      return "media";
        case KindCategory::INPUT:      return "input";
        case KindCategory::ACTION:     return "action";
        case KindCategory::FEEDBACK:   return "feedback";
        case KindCategory::NAVIGATION: return "navigation";
        case KindCategory::DATA:       return "data";
        case KindCategory::OVERLAY:    return "overlay";
        case KindCategory::CUSTOM:     return "custom";
    }
    return "custom";
}
inline KindCategory kindCategoryOf(StrandKind k) {
    switch (k) {
        case StrandKind::COLUMN: case StrandKind::ROW: case StrandKind::STACK: case StrandKind::BOX:
        case StrandKind::GRID: case StrandKind::WRAP: case StrandKind::SPACER: case StrandKind::DIVIDER:
        case StrandKind::CARD:
            return KindCategory::LAYOUT;
        case StrandKind::HEADER: case StrandKind::TOPBAR: case StrandKind::BOTTOMBAR: case StrandKind::DRAWER:
        case StrandKind::SCAFFOLD: case StrandKind::SPLASH:
            return KindCategory::STRUCTURE;
        case StrandKind::TEXT: case StrandKind::LINK: case StrandKind::KBD:
            return KindCategory::TEXT;
        case StrandKind::IMAGE: case StrandKind::VIDEO: case StrandKind::AUDIO: case StrandKind::WEBVIEW:
        case StrandKind::AVATAR: case StrandKind::ICON:
            return KindCategory::MEDIA;
        case StrandKind::INPUT: case StrandKind::TEXTAREA: case StrandKind::CHECKBOX: case StrandKind::SWITCH:
        case StrandKind::RADIO: case StrandKind::SLIDER: case StrandKind::SELECT: case StrandKind::FILE:
        case StrandKind::DATE: case StrandKind::TIME: case StrandKind::CODE_EDITOR: case StrandKind::SEARCH:
        case StrandKind::CALCULATOR:
            return KindCategory::INPUT;
        case StrandKind::BUTTON: case StrandKind::ICONBUTTON: case StrandKind::MENUITEM: case StrandKind::TABITEM:
            return KindCategory::ACTION;
        case StrandKind::BADGE: case StrandKind::PROGRESS: case StrandKind::SKELETON: case StrandKind::SPINNER:
        case StrandKind::BANNER: case StrandKind::TOOLTIP: case StrandKind::RATING: case StrandKind::TAG:
            return KindCategory::FEEDBACK;
        case StrandKind::TABS: case StrandKind::BREADCRUMB: case StrandKind::PAGINATION: case StrandKind::STEPS:
        case StrandKind::STEPITEM: case StrandKind::MENU:
            return KindCategory::NAVIGATION;
        case StrandKind::TABLE: case StrandKind::TABLEROW: case StrandKind::LIST: case StrandKind::LISTITEM:
        case StrandKind::TIMELINE: case StrandKind::TIMELINEITEM: case StrandKind::DONUT_CHART: case StrandKind::OBJECT:
            return KindCategory::DATA;
        case StrandKind::DIALOG:
            return KindCategory::OVERLAY;
        default:
            return KindCategory::CUSTOM;
    }
}
// يتفاعل معه المستخدم مباشرةً (يحتاج اسماً مُتاحاً للقارئات وهدف لمس كافياً).
inline bool isInteractiveKind(StrandKind k) {
    KindCategory c = kindCategoryOf(k);
    return c == KindCategory::INPUT || c == KindCategory::ACTION || k == StrandKind::LINK;
}
// يتجاوز حدود أبيه عمداً (طبقات منبثقة) فلا يُعدّ تجاوزه «فائض تخطيط».
inline bool isOverlayKind(StrandKind k) {
    return k == StrandKind::DIALOG || k == StrandKind::TOOLTIP || k == StrandKind::DRAWER ||
           k == StrandKind::MENU || k == StrandKind::BANNER || k == StrandKind::SPLASH;
}

// ---- السير على الشجرة ------------------------------------------------------------------------
using WalkFn = std::function<void(const StrandPtr& strand, int depth, const Strand* parent)>;
inline void walkFabric(const StrandPtr& root, const WalkFn& fn, int depth = 0, const Strand* parent = nullptr) {
    if (!root) return;
    fn(root, depth, parent);
    for (auto& c : root->children) walkFabric(c, fn, depth + 1, root.get());
}

inline std::vector<StrandPtr> findAllWhere(const StrandPtr& root, const std::function<bool(const Strand&)>& pred) {
    std::vector<StrandPtr> out;
    walkFabric(root, [&](const StrandPtr& s, int, const Strand*) { if (pred(*s)) out.push_back(s); });
    return out;
}
inline StrandPtr findByName(const StrandPtr& root, const std::string& name) {
    auto hits = findAllWhere(root, [&](const Strand& s) { return s.name == name; });
    return hits.empty() ? nullptr : hits.front();
}
// (findById(root, name) / findByKind / findAllByKind / findAllByMask already exist in
// rin_indsin_strand.h and are reused as-is; this one searches by the numeric StrandId.)
inline StrandPtr findByStrandId(const StrandPtr& root, StrandId id) {
    auto hits = findAllWhere(root, [&](const Strand& s) { return s.id == id; });
    return hits.empty() ? nullptr : hits.front();
}
inline std::vector<StrandPtr> findAllByCategory(const StrandPtr& root, KindCategory cat) {
    return findAllWhere(root, [&](const Strand& s) { return kindCategoryOf(s.kind) == cat; });
}
// كل العناصر التي تحمل الخاصية `key` (وبقيمة نصية تساوي `value` إن لم تكن فارغة).
inline std::vector<StrandPtr> findAllByAttr(const StrandPtr& root, const std::string& key, const std::string& value = "") {
    return findAllWhere(root, [&](const Strand& s) {
        const Value* v = s.attr(key);
        if (!v) return false;
        return value.empty() || v->asString() == value;
    });
}

// مسار الجذر -> العنصر (يشمل الطرفين)؛ فارغ إن لم يكن `target` في الشجرة.
inline bool pathTo(const StrandPtr& root, const Strand* target, std::vector<const Strand*>& out) {
    if (!root || !target) return false;
    out.push_back(root.get());
    if (root.get() == target) return true;
    for (auto& c : root->children) if (pathTo(c, target, out)) return true;
    out.pop_back();
    return false;
}
inline std::string pathString(const StrandPtr& root, const Strand* target) {
    std::vector<const Strand*> p;
    if (!pathTo(root, target, p)) return "";
    std::string out;
    for (size_t i = 0; i < p.size(); i++) {
        if (i) out += "/";
        out += strandKindName(p[i]->kind);
        if (!p[i]->name.empty()) out += ":" + p[i]->name;
    }
    return out;
}

// ---- إحصاءات -------------------------------------------------------------------------------
struct FabricStats {
    int nodes = 0;
    int maxDepth = 0;
    int leaves = 0;
    int interactive = 0;
    std::map<std::string, int> byKind;
    std::map<std::string, int> byCategory;
};
inline FabricStats fabricStats(const StrandPtr& root) {
    FabricStats st;
    walkFabric(root, [&](const StrandPtr& s, int depth, const Strand*) {
        st.nodes++;
        if (depth > st.maxDepth) st.maxDepth = depth;
        if (s->children.empty()) st.leaves++;
        if (isInteractiveKind(s->kind)) st.interactive++;
        st.byKind[strandKindName(s->kind)]++;
        st.byCategory[kindCategoryName(kindCategoryOf(s->kind))]++;
    });
    return st;
}

// ---- الضرب بالإحداثيات ------------------------------------------------------------------------
inline bool rectContains(const Rect& r, double x, double y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}
// أعمق عنصر يحوي النقطة (الأخير رسماً يفوز عند التداخل). لا يعتمد على الـ Needle كي يعمل بلا جلسة.
inline StrandPtr deepestAt(const StrandPtr& root, double x, double y) {
    if (!root || !rectContains(root->geometry, x, y)) return nullptr;
    for (auto it = root->children.rbegin(); it != root->children.rend(); ++it) {
        if (auto hit = deepestAt(*it, x, y)) return hit;
    }
    return root;
}

// ---- مخطط نصي (outline) -----------------------------------------------------------------------
// سطر لكل عنصر: Kind 'name' [category] (x,y wxh) مع أهم الخصائص النصية. مفيد للتشخيص والـ CI.
inline std::string fabricOutline(const StrandPtr& root, bool withGeometry = true, int maxDepth = 64) {
    std::ostringstream os;
    walkFabric(root, [&](const StrandPtr& s, int depth, const Strand*) {
        if (depth > maxDepth) return;
        os << std::string(depth * 2, ' ') << strandKindName(s->kind);
        if (!s->name.empty()) os << " '" << s->name << "'";
        os << " [" << kindCategoryName(kindCategoryOf(s->kind)) << "]";
        if (withGeometry)
            os << " (" << (long)s->geometry.x << "," << (long)s->geometry.y << " "
               << (long)s->geometry.w << "x" << (long)s->geometry.h << ")";
        std::string label = accessibleName(*s);
        if (!label.empty()) os << " \"" << label << "\"";
        os << "\n";
    });
    return os.str();
}

// ---- كتالوج المكوّنات كـ JSON (للمحرّر/التوثيق) --------------------------------------------------
inline std::string kindCatalogJson() {
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (int i = 0; i <= (int)StrandKind::CUSTOM; i++) {
        StrandKind k = (StrandKind)i;
        if (!first) os << ",";
        first = false;
        os << "{\"kind\":\"" << strandKindName(k) << "\",\"category\":\"" << kindCategoryName(kindCategoryOf(k))
           << "\",\"role\":\"" << accessibleRole(k) << "\",\"interactive\":" << (isInteractiveKind(k) ? "true" : "false")
           << ",\"overlay\":" << (isOverlayKind(k) ? "true" : "false") << "}";
    }
    os << "]";
    return os.str();
}

inline std::string fabricStatsJson(const FabricStats& st) {
    std::ostringstream os;
    os << "{\"nodes\":" << st.nodes << ",\"maxDepth\":" << st.maxDepth << ",\"leaves\":" << st.leaves
       << ",\"interactive\":" << st.interactive << ",\"byKind\":{";
    bool first = true;
    for (auto& kv : st.byKind) { if (!first) os << ","; first = false; os << "\"" << kv.first << "\":" << kv.second; }
    os << "},\"byCategory\":{";
    first = true;
    for (auto& kv : st.byCategory) { if (!first) os << ","; first = false; os << "\"" << kv.first << "\":" << kv.second; }
    os << "}}";
    return os.str();
}

} // namespace indsin
