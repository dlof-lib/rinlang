// indsin/rin_indsin_webcore.h — مفاهيم تخطيط الويب: القراءة والتفسير (بلا هندسة).
//
// كل ما هنا دوال نقية تقرأ خصائص Strand وتُعيد قراراً (مخفي؟ موقعه؟ ترتيبه؟ طبقته؟ هل ينطبق
// استعلام الوسائط؟) — يستهلكها rin_indsin_layout.h (التخطيط) وrin_indsin_paint.h (الرسم)
// وrin_indsin_webconcepts.h (المرور اللاحق: position/collision/selector).
// وثائق المفاهيم: docs/indsin_web_layout.md
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace indsin {

inline std::string webLower(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    s = s.substr(a, b - a);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline std::vector<std::string> webTokens(const std::string& s, const char* seps = " ,") {
    std::vector<std::string> out; std::string cur;
    for (char c : s) {
        if (std::string(seps).find(c) != std::string::npos) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
// قيمة منطقية متسامحة: true/1/yes/on (أي رقم ≠ 0).
inline bool webTruthy(const Value* v) {
    if (!v) return false;
    if (v->kind == Value::Kind::NUMBER) return v->number != 0.0;
    std::string t = webLower(v->str);
    return t == "true" || t == "1" || t == "yes" || t == "on";
}
inline bool webHasAttr(const Strand& s, const char* k) { return s.attr(k) != nullptr; }

// ---- Responsive: Viewport / ScreenWidth / ScreenHeight / DeviceSize / MediaQuery ---------------
inline double webScreenWidth()  { return viewportWidthPx(); }
inline double webScreenHeight() { return viewportHeightPx(); }
inline const char* webDeviceSize(double w) { return w < 600 ? "phone" : (w < 1024 ? "tablet" : "desktop"); }
inline const char* webBreakpointName(double w) { return w < 360 ? "xs" : w < 600 ? "sm" : w < 840 ? "md" : w < 1200 ? "lg" : "xl"; }

namespace webcore_detail {
inline bool parseLenOrBp(const std::string& t, double& out) {
    static const std::pair<const char*, double> bps[] = {{"xs", 0}, {"sm", 360}, {"md", 600}, {"lg", 840}, {"xl", 1200},
                                                         {"phone", 0}, {"tablet", 600}, {"desktop", 1024}};
    for (auto& b : bps) if (t == b.first) { out = b.second; return true; }
    std::string n = t;
    if (n.size() > 2 && n.compare(n.size() - 2, 2, "px") == 0) n.resize(n.size() - 2);
    try { size_t i = 0; out = std::stod(n, &i); return i == n.size(); } catch (...) { return false; }
}
inline bool featureHolds(const std::string& feature, const std::string& value, double w, double h) {
    double v = 0;
    if (feature == "min-width" || feature == "min_width")   return parseLenOrBp(value, v) && w >= v;
    if (feature == "max-width" || feature == "max_width")   { if (!parseLenOrBp(value, v)) return false; return w <= v; }
    if (feature == "min-height" || feature == "min_height") return parseLenOrBp(value, v) && h >= v;
    if (feature == "max-height" || feature == "max_height") { if (!parseLenOrBp(value, v)) return false; return h <= v; }
    if (feature == "orientation") return value == (w >= h ? "landscape" : "portrait");
    if (feature == "device" || feature == "device-size" || feature == "device_size") return value == webDeviceSize(w);
    if (feature == "breakpoint") return value == webBreakpointName(w);
    return true; // ميزة مجهولة: لا تُسقط العنصر
}
} // namespace webcore_detail

// "(min-width: 600px) and (max-width: 900px), (orientation: portrait)" — الفاصلة = أو، and = و.
// كذلك الاختصارات: "md" (= min-width: md)، ">=lg"، "<md"، "phone"/"tablet"/"desktop".
inline bool webMediaMatches(const std::string& query, double w, double h) {
    using namespace webcore_detail;
    std::string q = webLower(query);
    if (q.empty()) return true;
    size_t start = 0;
    while (start <= q.size()) {
        size_t comma = q.find(',', start);
        std::string alt = q.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? q.size() + 1 : comma + 1;
        // and-parts
        bool ok = true; size_t i = 0;
        while (i < alt.size() && ok) {
            size_t open = alt.find('(', i);
            if (open == std::string::npos) break;
            size_t close = alt.find(')', open);
            if (close == std::string::npos) { ok = false; break; }
            std::string body = alt.substr(open + 1, close - open - 1);
            size_t colon = body.find(':');
            if (colon == std::string::npos) ok = false;
            else ok = featureHolds(webLower(body.substr(0, colon)), webLower(body.substr(colon + 1)), w, h);
            i = close + 1;
        }
        if (alt.find('(') == std::string::npos) {
            std::string t = webLower(alt);
            while (!t.empty() && t.front() == ' ') t.erase(t.begin());
            while (!t.empty() && t.back() == ' ') t.pop_back();
            if (t.empty()) { ok = true; }
            else if (t.rfind(">=", 0) == 0) { double v; ok = parseLenOrBp(t.substr(2), v) && w >= v; }
            else if (t.rfind("<=", 0) == 0) { double v; ok = parseLenOrBp(t.substr(2), v) && w <= v; }
            else if (t.rfind('>', 0) == 0)  { double v; ok = parseLenOrBp(t.substr(1), v) && w > v; }
            else if (t.rfind('<', 0) == 0)  { double v; ok = parseLenOrBp(t.substr(1), v) && w < v; }
            else if (t == "phone" || t == "tablet" || t == "desktop") ok = (t == webDeviceSize(w));
            else { double v; ok = parseLenOrBp(t, v) && w >= v; } // "md" = min-width: md
        }
        if (ok) return true;
    }
    return false;
}

// ---- Display / Visibility / Hidden / Visible ---------------------------------------------------
// الحالة المخفية التي تطوي العنصر إلى حجم صفر (لا تشغل مساحة ولا تُرسم):
//   display="none" · hidden=true · visible=false (القديمة تُعالَج في مكانها) · media="(min-width: md)" غير مطابق ·
//   device_size="phone,tablet" لا يضمّ الجهاز الحالي.
inline bool webCollapsed(const Strand& s) {
    if (const Value* d = s.attr("display")) { if (webLower(d->asString()) == "none") return true; }
    if (webTruthy(s.attr("hidden"))) return true;
    double W = viewportWidthPx(), H = viewportHeightPx();
    if (W > 0) {
        if (const Value* m = s.attr("media")) { if (!webMediaMatches(m->asString(), W, H > 0 ? H : W)) return true; }
        if (const Value* dv = s.attr("device_size")) {
            bool hit = false; std::string cur = webDeviceSize(W);
            for (auto& t : webTokens(webLower(dv->asString()))) if (t == cur) hit = true;
            if (!hit) return true;
        }
    }
    return false;
}
// يحجز مساحته لكنه لا يُرسم: visibility="hidden"|"collapse" أو أخفاه حلّ التصادم (collision_policy="hide").
inline bool webInvisible(const Strand& s) {
    if (const Value* v = s.attr("visibility")) { std::string t = webLower(v->asString()); if (t == "hidden" || t == "collapse") return true; }
    return webTruthy(s.attr("_collision_hidden"));
}

// ---- ZIndex / Layer / LayerIndex / Isolation ----------------------------------------------------
inline int webZIndex(const Strand& s) {
    for (const char* key : {"z", "layer"}) {
        const Value* v = s.attr(key);
        if (!v) continue;
        if (v->kind == Value::Kind::NUMBER) return (int)std::lround(v->number);
        static const std::pair<const char*, int> names[] = {{"base", 0}, {"raised", 10}, {"dropdown", 100}, {"sticky", 200},
                                                            {"drawer", 300}, {"dialog", 400}, {"toast", 500}, {"tooltip", 600}};
        std::string t = webLower(v->str);
        for (auto& n : names) if (t == n.first) return n.second;
        return (int)std::lround(v->asNumber(0));
    }
    return 0;
}
// ترتيب الرسم: z تصاعدياً مع ثبات الترتيب المصدري عند التساوي. يعيد true إن اختلف عن ترتيب المصدر.
inline std::vector<size_t> webPaintOrder(const Strand& s, bool* reordered = nullptr) {
    std::vector<size_t> idx(s.children.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    bool any = false;
    for (auto& c : s.children) if (c->attr("z") || c->attr("layer")) { any = true; break; }
    if (any) std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return webZIndex(*s.children[a]) < webZIndex(*s.children[b]); });
    if (reordered) { *reordered = false; for (size_t i = 0; i < idx.size(); i++) if (idx[i] != i) *reordered = true; }
    return idx;
}

// ---- Order --------------------------------------------------------------------------------------
// order=<n> على الأبناء (flex/grid/wrap/stack): ترتيب مستقر تصاعدي. يُنفَّذ على مصفوفة الأبناء نفسها قبل
// التخطيط (فيتبعها الرسم أيضاً كما في CSS). آمن للتكرار.
inline void webApplyOrder(Strand& s) {
    bool any = false;
    for (auto& c : s.children) if (c->attr("order")) { any = true; break; }
    if (!any) return;
    std::stable_sort(s.children.begin(), s.children.end(), [](const StrandPtr& a, const StrandPtr& b) {
        return a->attrNum("order", 0) < b->attrNum("order", 0);
    });
}

// ---- Position / Relative / Absolute / Fixed / Sticky / Static ------------------------------------
enum class WebPosition { STATIC, RELATIVE, ABSOLUTE, FIXED, STICKY };
inline WebPosition webPosition(const Strand& s) {
    const Value* v = s.attr("position");
    if (!v) return WebPosition::STATIC;
    std::string t = webLower(v->asString());
    if (t == "relative") return WebPosition::RELATIVE;
    if (t == "absolute") return WebPosition::ABSOLUTE;
    if (t == "fixed")    return WebPosition::FIXED;
    if (t == "sticky")   return WebPosition::STICKY;
    return WebPosition::STATIC;
}
inline bool webOutOfFlow(const Strand& s) {
    WebPosition p = webPosition(s);
    return (p == WebPosition::ABSOLUTE || p == WebPosition::FIXED);
}

struct WebOffsets {
    bool hasL = false, hasT = false, hasR = false, hasB = false;
    double l = 0, t = 0, r = 0, b = 0;
    double dx = 0, dy = 0;          // offset_x / offset_y / offset="x,y": إزاحة بصرية إضافية فوق الموضع
    bool centerX = false, centerY = false;
};
// يقرأ top/left/right/bottom/inset/inset_x/inset_y/start/end/top_left/... مقابل صندوق مرجعي (cbW × cbH).
inline WebOffsets webReadOffsets(const Strand& s, double cbW, double cbH) {
    WebOffsets o;
    auto len = [&](const char* key, double base, double& out) {
        if (!s.attr(key)) return false;
        out = resolveSpacing(s, key, 0, base);
        return true;
    };
    double v;
    if (len("inset", cbW, v)) { o.hasL = o.hasT = o.hasR = o.hasB = true; o.l = o.r = v; o.t = o.b = len("inset", cbH, v) ? v : o.l; }
    if (len("inset_x", cbW, v)) { o.hasL = o.hasR = true; o.l = o.r = v; }
    if (len("inset_y", cbH, v)) { o.hasT = o.hasB = true; o.t = o.b = v; }
    if (len("left", cbW, v))   { o.hasL = true; o.l = v; }
    if (len("right", cbW, v))  { o.hasR = true; o.r = v; }
    if (len("top", cbH, v))    { o.hasT = true; o.t = v; }
    if (len("bottom", cbH, v)) { o.hasB = true; o.b = v; }
    // الاتجاه المنطقي: start/end حسب direction (rtl يعكس)
    bool rtl = webLower(s.attrStr("direction", "ltr")) == "rtl";
    if (len("start", cbW, v)) { if (rtl) { o.hasR = true; o.r = v; } else { o.hasL = true; o.l = v; } }
    if (len("end", cbW, v))   { if (rtl) { o.hasL = true; o.l = v; } else { o.hasR = true; o.r = v; } }
    // زوايا: top_left=8 (أو true = 0)
    auto corner = [&](const char* key, bool top, bool left) {
        const Value* cv = s.attr(key); if (!cv) return;
        double n = 0;
        if (cv->kind == Value::Kind::NUMBER) n = cv->number;
        else if (webTruthy(cv)) n = 0; else { n = cv->asNumber(0); if (webLower(cv->str) == "false") return; }
        if (top) { o.hasT = true; o.t = n; } else { o.hasB = true; o.b = n; }
        if (left) { o.hasL = true; o.l = n; } else { o.hasR = true; o.r = n; }
    };
    corner("top_left", true, true); corner("top_right", true, false);
    corner("bottom_left", false, true); corner("bottom_right", false, false);
    o.centerX = webTruthy(s.attr("center"));
    o.centerY = o.centerX || webTruthy(s.attr("middle"));
    // offset: رقم واحد = x و y معاً، أو "x,y"
    if (const Value* ov = s.attr("offset")) {
        if (ov->kind == Value::Kind::NUMBER) { o.dx = o.dy = ov->number; }
        else { auto t = webTokens(ov->str); try { if (!t.empty()) o.dx = std::stod(t[0]); o.dy = t.size() > 1 ? std::stod(t[1]) : o.dx; } catch (...) {} }
    }
    if (s.attr("offset_x")) o.dx = s.attrNum("offset_x", o.dx);
    if (s.attr("offset_y")) o.dy = s.attrNum("offset_y", o.dy);
    return o;
}

// ---- Flex / Flow / FlexDirection / FlexWrap -------------------------------------------------------
struct WebFlex { bool column = false; bool reverse = false; bool wrap = false; };
inline WebFlex webReadFlex(const Strand& s) {
    WebFlex f;
    std::string dir = webLower(s.attrStr("flex_direction", s.attrStr("direction", "")));
    std::string flow = webLower(s.attrStr("flow", ""));
    for (auto& t : webTokens(flow)) {
        if (t == "wrap" || t == "wrap-reverse") f.wrap = true;
        else if (t == "nowrap") f.wrap = false;
        else if (dir.empty()) dir = t;
    }
    if (dir == "column" || dir == "vertical") f.column = true;
    else if (dir == "column-reverse") { f.column = true; f.reverse = true; }
    else if (dir == "row-reverse") f.reverse = true;
    std::string w = webLower(s.attrStr("flex_wrap", ""));
    if (w == "wrap" || w == "wrap-reverse" || w == "true") f.wrap = true;
    else if (w == "nowrap" || w == "false") f.wrap = false;
    return f;
}

// ---- Alignment / Justify normalizers (CSS spellings -> engine spellings) ------------------------
inline std::string webAlignWord(const std::string& raw) {
    std::string t = webLower(raw);
    if (t == "flex-start" || t == "self-start" || t == "normal" || t == "baseline") return "start";
    if (t == "flex-end" || t == "self-end") return "end";
    return t;
}
inline std::string webJustifyWord(const std::string& raw) {
    std::string t = webLower(raw);
    if (t == "space-between") return "between";
    if (t == "space-around") return "around";
    if (t == "space-evenly" || t == "equal") return "evenly";
    if (t == "flex-start" || t == "left" || t == "top") return "start";
    if (t == "flex-end" || t == "right" || t == "bottom") return "end";
    return t;
}
// place="center" | place="top right" : أول كلمة = الاتجاه العمودي، الثانية = الأفقي (الافتراضي = الأولى).
inline bool webReadPlace(const Strand& s, std::string& v, std::string& h) {
    const Value* pv = s.attr("place");
    if (pv) {
        auto t = webTokens(webLower(pv->asString()), " ,");
        if (!t.empty()) { v = t[0]; h = t.size() > 1 ? t[1] : t[0]; return true; }
    }
    if (webTruthy(s.attr("center"))) { v = h = "center"; return true; }
    return false;
}


// ---- Width/Height keywords: IntrinsicSize / MinContent / MaxContent / FitContent -----------------
// 1 = min-content · 2 = max-content · 3 = fit-content · 0 = ليست كلمة محتوى.
inline int webIntrinsicKeyword(const Value* v) {
    if (!v || v->kind != Value::Kind::STRING) return 0;
    std::string t = webLower(v->str);
    if (t == "min-content" || t == "min_content" || t == "mincontent") return 1;
    if (t == "max-content" || t == "max_content" || t == "maxcontent") return 2;
    if (t == "fit-content" || t == "fit_content" || t == "fitcontent" || t == "fit") return 3;
    return 0;
}

// ---- Scroll / ScrollX / ScrollY / ScrollContainer / ScrollSnap -----------------------------------
inline bool webScrollWord(const Value* v) {
    if (!v) return false;
    if (webTruthy(v)) return true;
    std::string t = webLower(v->asString());
    return t == "scroll" || t == "auto";
}
inline bool webScrollsY(const Strand& s) {
    return webScrollWord(s.attr("scroll_y")) || webTruthy(s.attr("scroll_container")) || webScrollWord(s.attr("overflow_y")) ||
           webScrollWord(s.attr("overflow"));
}
inline bool webScrollsX(const Strand& s) {
    if (webScrollWord(s.attr("scroll_x")) || webScrollWord(s.attr("overflow_x"))) return true;
    std::string o = webLower(s.attrStr("overflow", ""));
    return o == "scroll-x" || o == "scroll-both";
}
inline double webReadScroll(const Strand& s, char axis) {
    return s.attrNum(axis == 'x' ? "scroll_left" : "scroll_top", 0.0);
}
// scroll_snap="start|center|end" (اختيارياً مع mandatory/proximity): أقرب نقطة التقاط لإزاحة التمرير. تُستدعى بعد
// أن توضع هندسة الأبناء بإحداثيات غير مزاحة؛ s.geometry للحاوية صالحة.
inline double webSnapScroll(const Strand& s, char axis, double offset) {
    const Value* sv = s.attr("scroll_snap");
    if (!sv) return offset;
    std::string mode = "none";
    for (auto& t : webTokens(webLower(sv->asString()))) if (t == "start" || t == "center" || t == "end") mode = t;
    if (webTruthy(sv)) mode = "start";
    if (mode == "none") return offset;
    double view = axis == 'x' ? s.geometry.w : s.geometry.h, origin = axis == 'x' ? s.geometry.x : s.geometry.y;
    double best = offset, bestD = 1e18;
    for (auto& ch : s.children) {
        if (webOutOfFlow(*ch) || webCollapsed(*ch)) continue;
        double pos = (axis == 'x' ? ch->geometry.x : ch->geometry.y) - origin;
        double len = axis == 'x' ? ch->geometry.w : ch->geometry.h;
        double snap = mode == "start" ? pos : (mode == "center" ? pos + len / 2 - view / 2 : pos + len - view);
        double d = std::fabs(snap - offset);
        if (d < bestD) { bestD = d; best = snap; }
    }
    return best;
}

// ---- Transform / Rotate / Translate / Scale -------------------------------------------------------
struct WebTransform { double tx = 0, ty = 0, sx = 1, sy = 1, rot = 0; bool any = false; };
inline WebTransform webReadTransform(const Strand& s, double w, double h) {
    WebTransform t;
    auto num = [](std::string a, double base, double& out) {
        a = webLower(a); if (a.empty()) return false;
        try {
            if (a.back() == '%') { out = base * std::stod(a.substr(0, a.size() - 1)) / 100.0; return true; }
            if (a.size() > 2 && a.compare(a.size() - 2, 2, "px") == 0) a.resize(a.size() - 2);
            if (a.size() > 3 && a.compare(a.size() - 3, 3, "deg") == 0) a.resize(a.size() - 3);
            size_t i = 0; out = std::stod(a, &i); return i == a.size();
        } catch (...) { return false; }
    };
    if (const Value* tv = s.attr("transform")) {
        std::string src = webLower(tv->asString());
        size_t pos = 0;
        while (pos < src.size()) {
            size_t open = src.find('(', pos); if (open == std::string::npos) break;
            size_t close = src.find(')', open); if (close == std::string::npos) break;
            std::string fn = src.substr(pos, open - pos);
            while (!fn.empty() && (fn.front() == ' ' || fn.front() == ',')) fn.erase(fn.begin());
            while (!fn.empty() && fn.back() == ' ') fn.pop_back();
            auto args = webTokens(src.substr(open + 1, close - open - 1), ",");
            for (auto& a : args) while (!a.empty() && a.front() == ' ') a.erase(a.begin());
            double v1 = 0, v2 = 0;
            if (fn == "translate") { if (!args.empty() && num(args[0], w, v1)) t.tx += v1; if (args.size() > 1 && num(args[1], h, v2)) t.ty += v2; }
            else if (fn == "translatex" || fn == "translate-x") { if (!args.empty() && num(args[0], w, v1)) t.tx += v1; }
            else if (fn == "translatey" || fn == "translate-y") { if (!args.empty() && num(args[0], h, v1)) t.ty += v1; }
            else if (fn == "rotate") { if (!args.empty() && num(args[0], 360, v1)) t.rot += v1; }
            else if (fn == "scale") { if (!args.empty() && num(args[0], 1, v1)) { t.sx *= v1; t.sy *= (args.size() > 1 && num(args[1], 1, v2)) ? v2 : v1; } }
            else if (fn == "scalex" || fn == "scale-x") { if (!args.empty() && num(args[0], 1, v1)) t.sx *= v1; }
            else if (fn == "scaley" || fn == "scale-y") { if (!args.empty() && num(args[0], 1, v1)) t.sy *= v1; }
            pos = close + 1;
        }
    }
    double v;
    if (const Value* a = s.attr("translate_x")) { if (num(a->asString(), w, v)) t.tx += v; }
    if (const Value* a = s.attr("translate_y")) { if (num(a->asString(), h, v)) t.ty += v; }
    if (const Value* a = s.attr("rotate"))      { if (num(a->asString(), 360, v)) t.rot += v; }
    if (const Value* a = s.attr("scale"))       { if (num(a->asString(), 1, v)) { t.sx *= v; t.sy *= v; } }
    if (const Value* a = s.attr("scale_x"))     { if (num(a->asString(), 1, v)) t.sx *= v; }
    if (const Value* a = s.attr("scale_y"))     { if (num(a->asString(), 1, v)) t.sy *= v; }
    t.any = t.tx != 0 || t.ty != 0 || t.rot != 0 || t.sx != 1 || t.sy != 1;
    return t;
}

// ---- Overflow / Clip --------------------------------------------------------------------------------
// يعيد أي محور يُقصّ: bit0 = X، bit1 = Y (0 = بلا قصّ). overflow=hidden|clip|scroll|auto، overflow_x/_y، clip=true.
inline int webClipAxes(const Strand& s) {
    auto clips = [](const Value* v) { if (!v) return false; std::string t = webLower(v->asString()); return t == "hidden" || t == "clip" || t == "scroll" || t == "auto" || t == "scroll-x" || t == "scroll-both"; };
    int a = 0;
    bool all = clips(s.attr("overflow")) || webTruthy(s.attr("clip")) || webTruthy(s.attr("scroll_container")) ||
               webLower(s.attrStr("clip", "")) == "all";
    if (all || clips(s.attr("overflow_x")) || webScrollsX(s)) a |= 1;
    if (all || clips(s.attr("overflow_y")) || webScrollsY(s)) a |= 2;
    return a;
}

} // namespace indsin
