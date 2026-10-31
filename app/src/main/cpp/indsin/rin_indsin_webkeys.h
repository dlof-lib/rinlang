// indsin/rin_indsin_webkeys.h — مفاهيم تخطيط صفحات الويب (Web Layout Concepts): أسماء الخصائص والوسوم.
//
// طبقة مفردات بحتة بلا أي اعتماد على باقي المحرك (تُضمَّن من rin_indsin_strand.h):
//   1) webCanonicalKey(): أي كتابة لاسم خاصية (MinWidth / min-width / minWidth / min_width) تتحوّل
//      إلى المفتاح القانوني الذي يقرأه المحرك (min_width). تُطبَّق عند بناء الـ Fabric على اسم
//      الخاصية فقط (تبقى قيمتها وربطها بـ Warp كما هما)، وتحفظ لاحقة التجاوب: MinWidth_md → min_width_md.
//   2) webTagKind(): أسماء الوسوم الهيكلية (Body/Page/Main/Article/Aside/Wrapper/...) كمرادفات
//      لأنواع موجودة، فلا يتغيّر أي نوع قائم.
#pragma once
#include <string>
#include <unordered_map>
#include <cctype>

namespace indsin {

namespace webkeys_detail {
inline std::string squash(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (unsigned char c : s) { if (c == '_' || c == '-') continue; o.push_back((char)std::tolower(c)); }
    return o;
}
inline bool isBreakpointSuffix(const std::string& s) {
    return s == "xs" || s == "sm" || s == "md" || s == "lg" || s == "xl";
}
} // namespace webkeys_detail

// المفتاح القانوني لاسم خاصية؛ يعيد الاسم كما هو إن لم يكن مرادفاً معروفاً.
inline std::string webCanonicalKey(const std::string& key) {
    using namespace webkeys_detail;
    if (key.empty() || key[0] == '_') return key;           // مفاتيح داخلية (_base_x ...)
    static const std::unordered_map<std::string, std::string> table = {
        // الأبعاد
        {"minwidth", "min_width"}, {"maxwidth", "max_width"}, {"minheight", "min_height"}, {"maxheight", "max_height"},
        {"aspectratio", "aspect"}, {"boxsizing", "box_sizing"}, {"intrinsicsize", "intrinsic_size"},
        // المساحات
        {"rowgap", "row_gap"}, {"columngap", "column_gap"}, {"colgap", "column_gap"},
        {"innerspace", "padding"}, {"outerspace", "margin"},
        {"spacedistribution", "justify"}, {"distribute", "justify"},
        {"spaceallocation", "space_allocation"}, {"autospace", "auto_space"},
        {"autofit", "auto_fit"}, {"autofill", "auto_fill"},
        // توزيع العناصر والمحاذاة
        {"flexdirection", "flex_direction"}, {"flexwrap", "flex_wrap"}, {"flexflow", "flow"},
        {"gridrow", "grid_row"}, {"gridcolumn", "grid_column"}, {"gridcol", "grid_column"},
        {"alignitems", "align_items"}, {"aligncontent", "align_content"}, {"alignself", "align_self"}, {"selfalign", "align_self"},
        {"justifycontent", "justify"}, {"justifyitems", "justify_items"}, {"justifyself", "justify_self"},
        {"verticalalign", "valign"}, {"horizontalalign", "align"}, {"textalign", "text_align"},
        // المظهر
        {"borderradius", "radius"}, {"boxshadow", "shadow"},
        // الطبقات والتداخل
        {"zindex", "z"}, {"layerindex", "z"},
        {"collisionpolicy", "collision_policy"}, {"collisiondetection", "collision_detection"},
        // المواقع
        {"offsetx", "offset_x"}, {"offsety", "offset_y"},
        {"topleft", "top_left"}, {"topright", "top_right"}, {"bottomleft", "bottom_left"}, {"bottomright", "bottom_right"},
        // التمرير والفيض
        {"scrollx", "scroll_x"}, {"scrolly", "scroll_y"}, {"scrollbar", "scroll_bar"}, {"scrollbehavior", "scroll_behavior"},
        {"scrollsnap", "scroll_snap"}, {"scrollcontainer", "scroll_container"},
        {"overflowx", "overflow_x"}, {"overflowy", "overflow_y"},
        // الاستجابة
        {"mediaquery", "media"}, {"devicesize", "device_size"},
        // المحرك
        {"layoutengine", "layout"}, {"layoutmode", "layout"},
    };
    std::string base = key, suffix;
    size_t us = key.rfind('_');
    if (us != std::string::npos && us > 0 && isBreakpointSuffix(key.substr(us + 1))) { base = key.substr(0, us); suffix = key.substr(us); }
    auto it = table.find(squash(base));
    if (it == table.end()) return key;
    return it->second + suffix;
}

// وسوم هيكلية = مرادفات لأنواع قائمة. kind يُعاد كاسم نوع نصي (COLUMN/ROW/BOX/...) كي لا يعتمد
// هذا الملف على StrandKind؛ strandKindFromTag() يحوّله. الاسم الأصلي يبقى في Strand::sourceTag.
inline const char* webTagKind(const std::string& tag) {
    static const std::unordered_map<std::string, const char*> table = {
        {"Body", "COLUMN"}, {"body", "COLUMN"}, {"Page", "COLUMN"}, {"page", "COLUMN"},
        {"Main", "COLUMN"}, {"main", "COLUMN"}, {"Content", "COLUMN"}, {"content", "COLUMN"},
        {"Article", "COLUMN"}, {"article", "COLUMN"}, {"Aside", "COLUMN"}, {"aside", "COLUMN"},
        {"Group", "COLUMN"}, {"group", "COLUMN"},
        {"Navigation", "ROW"}, {"navigation", "ROW"}, {"Nav", "ROW"}, {"nav", "ROW"},
        {"Flex", "ROW"}, {"flex", "ROW"},                      // الاتجاه الفعلي من flex_direction/flow
        {"Wrapper", "BOX"}, {"wrapper", "BOX"}, {"Widget", "BOX"}, {"widget", "BOX"},
        {"Component", "BOX"}, {"component", "BOX"}, {"Child", "BOX"}, {"child", "BOX"},
    };
    auto it = table.find(tag);
    return it == table.end() ? nullptr : it->second;
}

} // namespace indsin
