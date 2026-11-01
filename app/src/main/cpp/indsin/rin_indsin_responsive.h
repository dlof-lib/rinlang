// indsin/rin_indsin_responsive.h — تصميم متجاوب بلا أي تغيير في القواعد النحوية (grammar).
//
// أي خاصية يمكن تجاوزها حسب عرض الشاشة بإضافة لاحقة breakpoint إلى اسمها:
//
//     @view.Grid=cards
//         columns=1;  columns_md=2;  columns_lg=3;  columns_xl=4;
//         gap=12;  gap_lg=24;
//     .end/view
//
// عند العرض W: يُحدَّد breakpoint الحالي (xs/sm/md/lg/xl — انظر breakpointFor في
// rin_indsin_system.h) ثم تُطبَّق التجاوزات بالتتابع «mobile-first»: القيمة الأساسية، ثم
// _sm، ثم _md ... حتى breakpoint الحالي (الأكبر الأقرب يفوز). فعلى عرض 700 (md) تكون columns=2.
//
// التطبيق يتمّ على الـ Fabric قبل التخطيط ويمكن تكراره بأمان عند كل تغيير عرض: تُحفَظ القيمة
// الأصلية لمرة واحدة في خاصية مخفية `_base_<key>` فيُستعاد الأساس عند العودة لشاشة أصغر.
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_system.h"
#include <string>
#include <vector>

namespace indsin {

namespace responsive_detail {
inline bool splitBreakpointSuffix(const std::string& key, std::string& base, Breakpoint& bp) {
    size_t us = key.rfind('_');
    if (us == std::string::npos || us == 0 || us + 1 >= key.size()) return false;
    if (!breakpointFromName(key.substr(us + 1), bp)) return false;
    base = key.substr(0, us);
    return true;
}
inline ResolvedAttr* findAttr(Strand& s, const std::string& key) {
    for (auto& a : s.attrs) if (a.key == key) return &a;
    return nullptr;
}
} // namespace responsive_detail

// يطبّق التجاوزات على `s` وأبنائه لعرض `width`. يعيد عدد الخصائص التي تغيّرت قيمتها.
inline int applyResponsiveAttrs(const StrandPtr& s, double width) {
    if (!s) return 0;
    using namespace responsive_detail;
    int changed = 0;
    Breakpoint current = breakpointFor(width);

    // أفضل تجاوز لكل خاصية أساسية: الأعلى breakpoint <= الحالي.
    struct Best { Breakpoint bp; Value value; };
    std::vector<std::pair<std::string, Best>> best;
    for (auto& a : s->attrs) {
        std::string base; Breakpoint bp;
        if (a.key.rfind("_base_", 0) == 0) continue;
        if (!splitBreakpointSuffix(a.key, base, bp)) continue;
        if ((int)bp > (int)current) continue;
        bool done = false;
        for (auto& kv : best) {
            if (kv.first == base) {
                if ((int)bp >= (int)kv.second.bp) kv.second = Best{bp, a.value};
                done = true; break;
            }
        }
        if (!done) best.push_back({base, Best{bp, a.value}});
    }
    // كل الخصائص الأساسية التي لها أي لاحقة (حتى لو لا تنطبق الآن) يجب أن تُستعاد لأساسها.
    std::vector<std::string> bases;
    for (auto& a : s->attrs) {
        std::string base; Breakpoint bp;
        if (a.key.rfind("_base_", 0) == 0) continue;
        if (splitBreakpointSuffix(a.key, base, bp)) {
            bool seen = false;
            for (auto& b : bases) if (b == base) seen = true;
            if (!seen) bases.push_back(base);
        }
    }
    for (auto& base : bases) {
        ResolvedAttr* cur = findAttr(*s, base);
        ResolvedAttr* saved = findAttr(*s, "_base_" + base);
        if (!saved) { // أول تطبيق: احفظ الأساس (أو غيابه) قبل أي تعديل
            s->attrs.push_back(ResolvedAttr{"_base_" + base, cur ? cur->rawExpr : nullptr, cur ? cur->value : Value::txt("\x01<absent>")});
            saved = &s->attrs.back();
            cur = findAttr(*s, base); // قد تُبطل push_back المؤشرات
        }
        bool baseAbsent = saved->value.kind == Value::Kind::STRING && saved->value.str == "\x01<absent>";
        Value target = saved->value;
        rin::ExprPtr targetExpr = saved->rawExpr; // الأساس يعود مع ربط الـ Warp الأصلي إن وُجد
        bool hasTarget = !baseAbsent;
        for (auto& kv : best) if (kv.first == base) { target = kv.second.value; targetExpr = nullptr; hasTarget = true; }
        if (!hasTarget) { // لا أساس ولا تجاوز ينطبق: أزل الخاصية إن كنا أضفناها سابقاً
            if (cur) {
                for (size_t i = 0; i < s->attrs.size(); i++) if (s->attrs[i].key == base) { s->attrs.erase(s->attrs.begin() + i); break; }
                changed++;
            }
            continue;
        }
        if (!cur) { s->attrs.push_back(ResolvedAttr{base, targetExpr, target}); changed++; }
        else if (cur->value.asString() != target.asString() || cur->value.kind != target.kind) {
            cur->value = target; cur->rawExpr = targetExpr; changed++;
        }
    }
    for (auto& c : s->children) changed += applyResponsiveAttrs(c, width);
    return changed;
}

} // namespace indsin
