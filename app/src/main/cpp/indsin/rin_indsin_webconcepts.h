// indsin/rin_indsin_webconcepts.h — مفاهيم تخطيط الويب: المرور اللاحق للتخطيط + أدوات الاستعلام.
//
//   webPostLayout()      position=absolute/fixed/sticky، anchor، bounds، حلّ التصادم (collision_policy/avoid/overlap/priority)
//   webMinContentWidth() قياس min-content
//   webDetectCollisions  كشف التداخل بين الإخوة (CollisionDetection)
//   webSpaceReport       InnerSpace/OuterSpace/AvailableSpace/OccupiedSpace/EmptySpace (SpaceManager)
//   webQuery             Selector: Type #name .class [attr=v] :first-child :nth-child(n)  + > ~ و«،»
//   webSetScroll         ScrollTo (يُبطل الكاش ويعيد التخطيط)
//   webClipDrawList      overflow/clip على قائمة الرسم (يُستعمل من rin_indsin_paint.h)
// وثائق: docs/indsin_web_layout.md
#pragma once
#include "rin_indsin_layout.h"
#include <map>
#include <sstream>

namespace indsin {

namespace web_detail {
inline std::string esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) {}
        else o.push_back((char)c);
    }
    return o;
}
inline StrandPtr findByName(const StrandPtr& n, const std::string& name) {
    if (!n || name.empty()) return nullptr;
    if (n->name == name) return n;
    for (auto& c : n->children) if (auto f = findByName(c, name)) return f;
    return nullptr;
}
inline bool overlapRect(const Rect& a, const Rect& b, Rect* out = nullptr) {
    double x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y), x2 = std::min(a.x + a.w, b.x + b.w), y2 = std::min(a.y + a.h, b.y + b.h);
    if (x2 - x1 <= 0.5 || y2 - y1 <= 0.5) return false;
    if (out) *out = Rect{x1, y1, x2 - x1, y2 - y1};
    return true;
}
inline bool inFlowVisible(const Strand& s) {
    return !webCollapsed(s) && !isOverlayStrand(s) && s.geometry.w > 0.5 && s.geometry.h > 0.5;
}
} // namespace web_detail

// ---- MinContent ---------------------------------------------------------------------------------------
// أضيق عرض بلا كسر: النص = أطول كلمة، صف = مجموع الأبناء، عمود/ورقة = أعرض ابن. يُستدعى بعد قياس طبيعي للشجرة.
inline double webMinContentWidth(const Strand& s) {
    double pad = resolveSpacing(s, "padding", 0) + s.attrNum("border", 0);
    switch (s.kind) {
        case StrandKind::TEXT: case StrandKind::LINK: {
            double fs = resolveFontSize(s, "size", 16), best = 0;
            std::string t = s.attrStr("text", ""), word;
            for (size_t i = 0; i <= t.size(); i++) {
                if (i == t.size() || t[i] == ' ' || t[i] == '\n') { best = std::max(best, measureTextWidth(word, fs)); word.clear(); }
                else word.push_back(t[i]);
            }
            return best + pad * 2;
        }
        default: break;
    }
    if (s.children.empty()) return s.geometry.w;
    bool row = (s.kind == StrandKind::ROW || s.kind == StrandKind::TOPBAR || s.kind == StrandKind::BOTTOMBAR ||
                s.kind == StrandKind::HEADER || s.kind == StrandKind::LISTITEM || s.kind == StrandKind::TABS || s.kind == StrandKind::TAG);
    double gap = resolveSpacing(s, "column_gap", resolveSpacing(s, "gap", 0));
    double acc = 0; int n = 0;
    for (auto& c : s.children) {
        if (webCollapsed(*c) || webOutOfFlow(*c)) continue;
        double w = webMinContentWidth(*c);
        acc = row ? acc + w : std::max(acc, w); n++;
    }
    if (row && n > 1) acc += gap * (n - 1);
    return acc + pad * 2;
}

// ---- Collision ---------------------------------------------------------------------------------------
struct WebCollision { StrandId a = 0, b = 0; std::string nameA, nameB; Rect overlap; };

// كل أزواج الإخوة المتداخلة (هندسة نهائية). scope: الجذر؛ recursive=true يفحص كل المستويات.
inline void webDetectCollisions(const StrandPtr& root, std::vector<WebCollision>& out, bool recursive = true) {
    if (!root) return;
    auto& ch = root->children;
    for (size_t i = 0; i < ch.size(); i++) {
        if (!web_detail::inFlowVisible(*ch[i]) || webInvisible(*ch[i])) continue;
        for (size_t j = i + 1; j < ch.size(); j++) {
            if (!web_detail::inFlowVisible(*ch[j]) || webInvisible(*ch[j])) continue;
            Rect o;
            if (web_detail::overlapRect(ch[i]->geometry, ch[j]->geometry, &o))
                out.push_back({ch[i]->id, ch[j]->id, ch[i]->name, ch[j]->name, o});
        }
    }
    if (recursive) for (auto& c : ch) webDetectCollisions(c, out, true);
}

namespace web_detail {
inline std::string policyOf(const Strand& child, const Strand& parent) {
    std::string p = webLower(child.attrStr("collision_policy", ""));
    if (!p.empty()) return p;
    if (child.attr("avoid")) return "push";
    if (webTruthy(child.attr("overlap")) || webLower(child.attrStr("overlap", "")) == "true") return "none";
    std::string po = webLower(parent.attrStr("overlap", ""));
    if (po == "false" || po == "none" || po == "0" || po == "no") return "push";
    std::string pd = webLower(parent.attrStr("collision_detection", ""));
    if (pd == "push" || pd == "hide" || pd == "shift") return pd;
    return "none";
}
inline void resolveCollisionsIn(Indsin& ix, const StrandPtr& parent) {
    struct Kid { StrandPtr s; std::string policy; double prio; size_t order; };
    std::vector<Kid> kids; std::vector<StrandPtr> obstacles;
    for (size_t i = 0; i < parent->children.size(); i++) {
        auto& c = parent->children[i];
        if (webCollapsed(*c) || isOverlayStrand(*c)) continue;
        std::string pol = policyOf(*c, *parent);
        // overlap=true صريح: العنصر معفى تماماً (لا يُزاح ولا يُزيح غيره)؛ none الافتراضي = عائق ثابت.
        if (pol == "none" && webTruthy(c->attr("overlap"))) continue;
        if (pol == "none") { if (inFlowVisible(*c)) obstacles.push_back(c); }
        else kids.push_back({c, pol, c->attrNum("priority", 0), i});
    }
    if (kids.empty()) return;
    std::stable_sort(kids.begin(), kids.end(), [](const Kid& a, const Kid& b) { return a.prio > b.prio; });
    std::vector<StrandPtr> placed = obstacles;
    for (auto& k : kids) {
        if (!inFlowVisible(*k.s)) continue;
        std::vector<std::string> avoidNames; bool hasAvoid = false;
        if (const Value* av = k.s->attr("avoid")) { hasAvoid = true; avoidNames = webTokens(av->asString(), " ,"); }
        auto isTarget = [&](const StrandPtr& o) {
            if (!hasAvoid) return true;
            for (auto& n : avoidNames) if (o->name == n) return true;
            return false; };
        std::string axisAttr = webLower(k.s->attrStr("collision_axis", k.policy == "shift" ? "x" : "y"));
        bool alongX = (axisAttr == "x");
        double gap = k.s->attrNum("collision_gap", 0);
        bool hidden = false;
        for (int iter = 0; iter < 48; iter++) {
            const Strand* hit = nullptr;
            for (auto& o : placed) if (o.get() != k.s.get() && isTarget(o) && overlapRect(k.s->geometry, o->geometry)) { hit = o.get(); break; }
            if (!hit) break;
            if (k.policy == "hide") { hidden = true; break; }
            double d = alongX ? (hit->geometry.x + hit->geometry.w + gap - k.s->geometry.x)
                              : (hit->geometry.y + hit->geometry.h + gap - k.s->geometry.y);
            if (d <= 0.01) break;
            ix.translate(k.s, alongX ? d : 0, alongX ? 0 : d);
        }
        if (hidden) {
            bool has = false;
            for (auto& a : k.s->attrs) if (a.key == "_collision_hidden") { a.value = Value::txt("true"); has = true; }
            if (!has) k.s->attrs.push_back({"_collision_hidden", nullptr, Value::txt("true")});
        } else placed.push_back(k.s);
    }
}
inline void clearCollisionFlags(const StrandPtr& n) {
    for (size_t i = 0; i < n->attrs.size(); i++) if (n->attrs[i].key == "_collision_hidden") { n->attrs.erase(n->attrs.begin() + i); break; }
    for (auto& c : n->children) clearCollisionFlags(c);
}

struct PosCtx { Indsin* ix; StrandPtr root; Rect rootBox; Rect viewport; double sx, sy; };

inline void clampToBounds(Indsin& ix, const StrandPtr& s, const StrandPtr& parent, const Rect& cb, const PosCtx& cx) {
    const Value* bv = s->attr("bounds");
    if (!bv) bv = s->attr("constraints");
    if (!bv) return;
    std::string b = webLower(bv->asString());
    Rect box;
    if (b == "viewport" || b == "screen") box = cx.viewport;
    else if (b == "parent") box = parent->geometry;
    else if (b == "root" || b == "page") box = cx.rootBox;
    else if (b == "none" || b == "false" || b.empty()) return;
    else box = cb;
    Rect g = s->geometry;
    double dx = 0, dy = 0;
    if (g.w >= box.w) dx = box.x - g.x; else if (g.x < box.x) dx = box.x - g.x; else if (g.x + g.w > box.x + box.w) dx = box.x + box.w - g.w - g.x;
    if (g.h >= box.h) dy = box.y - g.y; else if (g.y < box.y) dy = box.y - g.y; else if (g.y + g.h > box.y + box.h) dy = box.y + box.h - g.h - g.y;
    if (dx != 0 || dy != 0) ix.translate(s, dx, dy);
}

inline void placeOutOfFlow(const StrandPtr& s, const StrandPtr& parent, const Rect& cbIn, PosCtx& cx) {
    Indsin& ix = *cx.ix;
    Rect box = cbIn;
    bool fixed = webPosition(*s) == WebPosition::FIXED;
    if (fixed) box = Rect{cx.viewport.x + cx.sx, cx.viewport.y + cx.sy, cx.viewport.w, cx.viewport.h};
    StrandPtr anchor = s->attr("anchor") ? findByName(cx.root, s->attrStr("anchor", "")) : nullptr;
    std::string side = webLower(s->attrStr("anchor_side", ""));
    if (anchor && side.empty()) box = anchor->geometry;
    WebOffsets o = webReadOffsets(*s, box.w, box.h);
    // الحجم الأقصى = الصندوق المرجعي كاملاً (يجوز أن يتجاوزه العنصر)، إلا عند تحديد الطرفين فيُقيَّد بينهما (تمدّد).
    double availW = std::max(0.0, (o.hasL && o.hasR) ? box.w - o.l - o.r : box.w);
    double availH = std::max(0.0, (o.hasT && o.hasB) ? box.h - o.t - o.b : box.h);
    if (anchor && !side.empty()) { availW = cx.viewport.w > 0 ? cx.viewport.w : box.w; availH = cx.viewport.h > 0 ? cx.viewport.h : box.h; }
    Constraints c{0, availW, 0, availH};
    if (o.hasL && o.hasR && !s->attr("width") && !anchor)  c.minW = c.maxW;
    if (o.hasT && o.hasB && !s->attr("height") && !anchor) c.minH = c.maxH;
    Rect r = ix.layout(s, c, 0, 0);
    double w = r.w, h = r.h, X, Y;
    if (anchor && !side.empty()) {
        const Rect& t = anchor->geometry;
        std::string al = webLower(s->attrStr("anchor_align", "start"));
        double gx = o.hasL ? o.l : (o.hasR ? o.r : 0), gy = o.hasT ? o.t : (o.hasB ? o.b : 0);
        auto alongX = [&](void) { return al == "center" ? t.x + (t.w - w) / 2 : (al == "end" ? t.x + t.w - w : t.x); };
        auto alongY = [&](void) { return al == "center" ? t.y + (t.h - h) / 2 : (al == "end" ? t.y + t.h - h : t.y); };
        if (side == "top")         { X = alongX(); Y = t.y - h - gy; }
        else if (side == "left")   { X = t.x - w - gx; Y = alongY(); }
        else if (side == "right")  { X = t.x + t.w + gx; Y = alongY(); }
        else                       { X = alongX(); Y = t.y + t.h + gy; }
    } else {
        double padP = resolveSpacing(*parent, "padding", 0);
        X = o.centerX ? box.x + (box.w - w) / 2 : o.hasL ? box.x + o.l : o.hasR ? box.x + box.w - o.r - w : parent->geometry.x + padP;
        Y = o.centerY ? box.y + (box.h - h) / 2 : o.hasT ? box.y + o.t : o.hasB ? box.y + box.h - o.b - h : parent->geometry.y + padP;
    }
    WebTransform wt = webReadTransform(*s, w, h);
    X += o.dx + wt.tx; Y += o.dy + wt.ty;
    ix.layout(s, c, X, Y);
}

inline void applySticky(Indsin& ix, const StrandPtr& s, const StrandPtr& parent, const PosCtx& cx) {
    WebOffsets o = webReadOffsets(*s, parent->geometry.w, parent->geometry.h);
    double baseX = s->geometry.x - s->stickyDx, baseY = s->geometry.y - s->stickyDy;
    double dX = baseX, dY = baseY;
    const Rect& P = parent->geometry;
    if (o.hasT) dY = std::max(baseY, cx.sy + o.t);
    else if (o.hasB && cx.viewport.h > 0) dY = std::min(baseY, cx.sy + cx.viewport.h - o.b - s->geometry.h);
    if (o.hasL) dX = std::max(baseX, cx.sx + o.l);
    else if (o.hasR && cx.viewport.w > 0) dX = std::min(baseX, cx.sx + cx.viewport.w - o.r - s->geometry.w);
    dY = std::min(dY, P.y + P.h - s->geometry.h); dY = std::max(dY, std::min(baseY, P.y + P.h - s->geometry.h));
    dX = std::min(dX, P.x + P.w - s->geometry.w); dX = std::max(dX, std::min(baseX, P.x + P.w - s->geometry.w));
    double mx = dX - s->geometry.x, my = dY - s->geometry.y;
    if (mx != 0 || my != 0) ix.translate(s, mx, my);
    s->stickyDx = dX - baseX; s->stickyDy = dY - baseY;
}

inline void walkPositioned(const StrandPtr& n, const Rect& cb, PosCtx& cx) {
    for (auto& c : n->children) {
        if (webCollapsed(*c)) continue;
        WebPosition pos = webPosition(*c);
        if (pos == WebPosition::ABSOLUTE || pos == WebPosition::FIXED) placeOutOfFlow(c, n, cb, cx);
        else if (pos == WebPosition::STICKY) applySticky(*cx.ix, c, n, cx);
        Rect childCb = (pos == WebPosition::STATIC) ? cb : c->geometry;
        walkPositioned(c, childCb, cx);
        if (c->attr("bounds") || c->attr("constraints")) clampToBounds(*cx.ix, c, n, cb, cx);
    }
    resolveCollisionsIn(*cx.ix, n);
}
} // namespace web_detail

// المرور اللاحق: يُستدعى تلقائياً من Indsin::layout() للجذر (screenRoot). يمكن للمضيف استدعاؤه مجدداً مع إزاحة
// التمرير الحالية (scrollX/scrollY) ليتحدّث fixed/sticky دون إعادة تخطيط كاملة. آمن للتكرار.
inline void webPostLayout(Indsin& ix, const StrandPtr& root, double scrollX, double scrollY) {
    if (!root) return;
    using namespace web_detail;
    bool any = false;
    std::vector<const Strand*> stack{root.get()};
    while (!stack.empty() && !any) {
        const Strand* n = stack.back(); stack.pop_back();
        if (n->attr("position") || n->attr("collision_policy") || n->attr("avoid") || n->attr("overlap") ||
            n->attr("collision_detection") || n->attr("bounds") || n->attr("constraints") || n->attr("_collision_hidden")) { any = true; break; }
        for (auto& c : n->children) stack.push_back(c.get());
    }
    if (!any) return;
    clearCollisionFlags(root);
    PosCtx cx;
    cx.ix = &ix; cx.root = root; cx.sx = scrollX; cx.sy = scrollY;
    cx.rootBox = root->geometry;
    double vw = viewportWidthPx() > 0 ? viewportWidthPx() : root->geometry.w;
    double vh = viewportHeightPx() > 0 ? viewportHeightPx() : root->geometry.h;
    cx.viewport = Rect{0, 0, vw, vh};
    Rect rootCb = root->geometry; rootCb.h = std::max(rootCb.h, vh);
    walkPositioned(root, rootCb, cx);
}

// ---- Space report -----------------------------------------------------------------------------------------
struct WebSpaceReport {
    double innerW = 0, innerH = 0;          // InnerSpace: صندوق المحتوى (بعد padding+border)
    double outerW = 0, outerH = 0;          // OuterSpace: الصندوق مع margin
    double contentW = 0, contentH = 0;      // امتداد الأبناء (من بداية المحتوى)
    double occupied = 0;                    // OccupiedSpace: مساحة اتحاد مستطيلات الأبناء داخل الصندوق الداخلي
    double empty = 0;                       // EmptySpace = مساحة داخلية - المشغول
    double availableW = 0, availableH = 0;  // AvailableSpace: الشريط الحرّ بعد آخر ابن (يمين/أسفل)
    double occupiedRatio = 0;
    int children = 0;
};
inline WebSpaceReport webSpaceReport(const Strand& s) {
    WebSpaceReport r;
    double pad = resolveSpacing(s, "padding", 0) + s.attrNum("border", 0);
    const Rect& g = s.geometry;
    Rect inner{g.x + pad, g.y + pad, std::max(0.0, g.w - 2 * pad), std::max(0.0, g.h - 2 * pad)};
    r.innerW = inner.w; r.innerH = inner.h;
    r.outerW = s.geometryOuter.w > 0 ? s.geometryOuter.w : g.w; r.outerH = s.geometryOuter.h > 0 ? s.geometryOuter.h : g.h;
    std::vector<Rect> rs; double maxR = inner.x, maxB = inner.y;
    for (auto& c : s.children) {
        if (!web_detail::inFlowVisible(*c) || webOutOfFlow(*c)) continue;
        Rect cr = c->geometry;
        double x1 = std::max(cr.x, inner.x), y1 = std::max(cr.y, inner.y), x2 = std::min(cr.x + cr.w, inner.x + inner.w), y2 = std::min(cr.y + cr.h, inner.y + inner.h);
        if (x2 <= x1 || y2 <= y1) continue;
        rs.push_back({x1, y1, x2 - x1, y2 - y1});
        maxR = std::max(maxR, x2); maxB = std::max(maxB, y2);
    }
    r.children = (int)rs.size();
    r.contentW = maxR - inner.x; r.contentH = maxB - inner.y;
    // مساحة الاتحاد بضغط الإحداثيات
    std::vector<double> xs; for (auto& q : rs) { xs.push_back(q.x); xs.push_back(q.x + q.w); }
    std::sort(xs.begin(), xs.end()); xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    double area = 0;
    for (size_t i = 0; i + 1 < xs.size(); i++) {
        std::vector<std::pair<double, double>> iv;
        for (auto& q : rs) if (q.x <= xs[i] + 1e-9 && q.x + q.w >= xs[i + 1] - 1e-9) iv.push_back({q.y, q.y + q.h});
        std::sort(iv.begin(), iv.end());
        double cov = 0, cur = -1e18, end = -1e18;
        for (auto& v : iv) { if (v.first > end) { cov += end - cur > 0 ? end - cur : 0; cur = v.first; end = v.second; } else end = std::max(end, v.second); }
        cov += end - cur > 0 ? end - cur : 0;
        area += cov * (xs[i + 1] - xs[i]);
    }
    r.occupied = area;
    double total = inner.w * inner.h;
    r.empty = std::max(0.0, total - area);
    r.occupiedRatio = total > 0 ? area / total : 0;
    r.availableW = std::max(0.0, inner.x + inner.w - maxR); r.availableH = std::max(0.0, inner.y + inner.h - maxB);
    return r;
}
inline std::string webSpaceReportJson(const Strand& s) {
    WebSpaceReport r = webSpaceReport(s);
    std::ostringstream os;
    os << "{\"name\":\"" << web_detail::esc(s.name) << "\",\"innerW\":" << r.innerW << ",\"innerH\":" << r.innerH
       << ",\"outerW\":" << r.outerW << ",\"outerH\":" << r.outerH << ",\"contentW\":" << r.contentW << ",\"contentH\":" << r.contentH
       << ",\"occupied\":" << r.occupied << ",\"empty\":" << r.empty << ",\"availableW\":" << r.availableW << ",\"availableH\":" << r.availableH
       << ",\"occupiedRatio\":" << r.occupiedRatio << ",\"children\":" << r.children << "}";
    return os.str();
}

// ---- Selector ---------------------------------------------------------------------------------------------
namespace web_detail {
struct SelAttr { std::string key, op, val; };
struct SelPseudo { std::string name; int n = 0; };
struct SelSimple { std::string type, id; std::vector<std::string> classes; std::vector<SelAttr> attrs; std::vector<SelPseudo> pseudos; char comb = ' '; };
struct PathEl { const Strand* s; const Strand* parent; size_t idx; };

inline bool parseCompound(const std::string& t, SelSimple& out) {
    size_t i = 0;
    auto ident = [&]() { size_t a = i; while (i < t.size() && (std::isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '-' || (unsigned char)t[i] >= 0x80)) i++; return t.substr(a, i - a); };
    if (i < t.size() && t[i] == '*') { out.type = "*"; i++; } else if (i < t.size() && (std::isalpha((unsigned char)t[i]) || t[i] == '_')) out.type = webLower(ident());
    while (i < t.size()) {
        char c = t[i];
        if (c == '#') { i++; out.id = ident(); }
        else if (c == '.') { i++; out.classes.push_back(ident()); }
        else if (c == '[') {
            size_t e = t.find(']', i); if (e == std::string::npos) return false;
            std::string body = t.substr(i + 1, e - i - 1); i = e + 1;
            SelAttr a; size_t p = body.find_first_of("=^$*~");
            if (p == std::string::npos) a.key = body;
            else { a.key = body.substr(0, p); size_t q = p; if (body[q] != '=') q++; a.op = body.substr(p, q - p + 1); a.val = body.substr(q + 1); }
            if (a.val.size() >= 2 && (a.val.front() == '"' || a.val.front() == '\'')) a.val = a.val.substr(1, a.val.size() - 2);
            a.key = webCanonicalKey(a.key);
            out.attrs.push_back(a);
        } else if (c == ':') {
            i++; SelPseudo ps; ps.name = ident();
            if (i < t.size() && t[i] == '(') { size_t e = t.find(')', i); if (e == std::string::npos) return false; try { ps.n = std::stoi(t.substr(i + 1, e - i - 1)); } catch (...) { return false; } i = e + 1; }
            out.pseudos.push_back(ps);
        } else return false;
    }
    return true;
}
inline bool parseSelectorList(const std::string& sel, std::vector<std::vector<SelSimple>>& out) {
    for (auto& part : webTokens(sel, ",")) {
        std::vector<SelSimple> chain; std::string cur; char comb = ' ';
        auto flush = [&]() { if (cur.empty()) return true; SelSimple sp; if (!parseCompound(cur, sp)) return false; sp.comb = comb; chain.push_back(sp); cur.clear(); comb = ' '; return true; };
        bool inBr = false;
        for (char ch : part) {
            if (ch == '[') inBr = true; if (ch == ']') inBr = false;
            if (!inBr && (ch == '>' || ch == '+' || ch == '~')) { if (!flush()) return false; comb = ch; }
            else if (!inBr && ch == ' ') { if (!flush()) return false; }
            else cur.push_back(ch);
        }
        if (!flush() || chain.empty()) return false;
        out.push_back(chain);
    }
    return !out.empty();
}
inline bool matchSimple(const SelSimple& sp, const PathEl& pe) {
    const Strand& s = *pe.s;
    if (!sp.type.empty() && sp.type != "*") {
        if (webLower(strandKindName(s.kind)) != sp.type && webLower(s.sourceTag) != sp.type) return false;
    }
    if (!sp.id.empty() && s.name != sp.id) return false;
    if (!sp.classes.empty()) {
        auto have = webTokens(s.attrStr("class", ""), " ,");
        for (auto& c : sp.classes) if (std::find(have.begin(), have.end(), c) == have.end()) return false;
    }
    for (auto& a : sp.attrs) {
        const Value* v = s.attr(a.key);
        if (!v) return false;
        if (a.op.empty()) continue;
        std::string x = v->asString();
        if (a.op == "=") { if (x != a.val) return false; }
        else if (a.op == "^=") { if (x.rfind(a.val, 0) != 0) return false; }
        else if (a.op == "$=") { if (x.size() < a.val.size() || x.compare(x.size() - a.val.size(), a.val.size(), a.val) != 0) return false; }
        else if (a.op == "*=") { if (x.find(a.val) == std::string::npos) return false; }
        else if (a.op == "~=") { auto tk = webTokens(x, " "); if (std::find(tk.begin(), tk.end(), a.val) == tk.end()) return false; }
        else return false;
    }
    for (auto& ps : sp.pseudos) {
        size_t n = pe.parent ? pe.parent->children.size() : 1;
        if (ps.name == "first-child") { if (pe.idx != 0) return false; }
        else if (ps.name == "last-child") { if (pe.idx + 1 != n) return false; }
        else if (ps.name == "nth-child") { if ((int)pe.idx + 1 != ps.n) return false; }
        else if (ps.name == "empty") { if (!s.children.empty()) return false; }
        else if (ps.name == "only-child") { if (n != 1) return false; }
        else return false;
    }
    return true;
}
// يطابق chain[k] مقابل path[pi] ثم يتراجع يساراً حسب الرابط.
inline bool matchChain(const std::vector<SelSimple>& chain, int k, const std::vector<PathEl>& path, int pi) {
    if (!matchSimple(chain[k], path[pi])) return false;
    if (k == 0) return true;
    char comb = chain[k].comb;
    if (comb == '>') return pi > 0 && matchChain(chain, k - 1, path, pi - 1);
    if (comb == ' ') { for (int a = pi - 1; a >= 0; a--) if (matchChain(chain, k - 1, path, a)) return true; return false; }
    // + و ~ : الإخوة السابقون (مسار مصطنع بعنصر الأخ)
    const Strand* parent = path[pi].parent; if (!parent) return false;
    size_t idx = path[pi].idx;
    for (int j = (int)idx - 1; j >= 0; j--) {
        std::vector<PathEl> alt(path.begin(), path.begin() + pi);
        alt.push_back({parent->children[j].get(), parent, (size_t)j});
        if (matchChain(chain, k - 1, alt, pi)) return true;
        if (comb == '+') break;
    }
    return false;
}
inline void queryWalk(const StrandPtr& n, const Strand* parent, size_t idx, std::vector<PathEl>& path,
                      const std::vector<std::vector<SelSimple>>& list, std::vector<StrandPtr>& out) {
    path.push_back({n.get(), parent, idx});
    for (auto& chain : list) if (matchChain(chain, (int)chain.size() - 1, path, (int)path.size() - 1)) { out.push_back(n); break; }
    for (size_t i = 0; i < n->children.size(); i++) queryWalk(n->children[i], n.get(), i, path, list, out);
    path.pop_back();
}
} // namespace web_detail

// Selector: "Column > Text.title", "#card [onTap]", "Row :first-child", "Grid, Wrap", "Text + Button". false = صياغة خاطئة.
inline bool webQuery(const StrandPtr& root, const std::string& selector, std::vector<StrandPtr>& out) {
    out.clear();
    std::vector<std::vector<web_detail::SelSimple>> list;
    if (!root || !web_detail::parseSelectorList(selector, list)) return false;
    std::vector<web_detail::PathEl> path;
    web_detail::queryWalk(root, nullptr, 0, path, list, out);
    return true;
}
inline StrandPtr webParentOf(const StrandPtr& root, const Strand* target) {
    if (!root) return nullptr;
    for (auto& c : root->children) { if (c.get() == target) return root; if (auto p = webParentOf(c, target)) return p; }
    return nullptr;
}

// ---- ScrollTo --------------------------------------------------------------------------------------------
// يضبط scroll_top/scroll_left لحاوية بالاسم ويُبطل كاش التخطيط؛ على المضيف استدعاء relayout بعده. false إن لم توجد حاوية تمرير.
inline bool webSetScroll(const StrandPtr& root, const std::string& name, double x, double y) {
    StrandPtr n = web_detail::findByName(root, name);
    if (!n || !(webScrollsX(*n) || webScrollsY(*n))) return false;
    auto set = [&](const char* k, double v) {
        for (auto& a : n->attrs) if (a.key == k) { a.value = Value::num(v); a.rawExpr = nullptr; return; }
        n->attrs.push_back({k, nullptr, Value::num(v)});
    };
    set("scroll_left", x); set("scroll_top", y);
    recomputeHashes(root);
    return true;
}

// ---- Clip (overflow) ---------------------------------------------------------------------------------------
// يقصّ أوامر الرسم التي أنتجها الأحفاد (owner != s) على صندوق s على المحاور المطلوبة. المستطيلات تُقطع هندسياً؛
// أوامر النص لا تُقصّ حرفاً بحرف: تبقى إن كان ≥ نصفها ظاهراً وإلا تُحذف.
template <typename DrawList_, typename Op_>
inline void webClipDrawList(DrawList_& list, size_t first, const Strand& s, int axes, Op_ textOp) {
    if (!axes) return;
    const Rect& g = s.geometry;
    const double INF = 1e12;
    double cx1 = (axes & 1) ? g.x : -INF, cx2 = (axes & 1) ? g.x + g.w : INF;
    double cy1 = (axes & 2) ? g.y : -INF, cy2 = (axes & 2) ? g.y + g.h : INF;
    size_t w = first;
    for (size_t i = first; i < list.size(); i++) {
        auto& d = list[i];
        if (d.owner == s.id) { if (w != i) list[w] = std::move(list[i]); w++; continue; }
        double x1 = std::max(d.bounds.x, cx1), y1 = std::max(d.bounds.y, cy1);
        double x2 = std::min(d.bounds.x + d.bounds.w, cx2), y2 = std::min(d.bounds.y + d.bounds.h, cy2);
        bool empty = x2 <= x1 || y2 <= y1;
        if (!empty && d.op == textOp) {
            double vis = (x2 - x1) * (y2 - y1), tot = std::max(1e-9, d.bounds.w * d.bounds.h);
            if (vis / tot < 0.5) empty = true;
        } else if (!empty && d.op != textOp) { d.bounds.x = x1; d.bounds.y = y1; d.bounds.w = x2 - x1; d.bounds.h = y2 - y1; }
        if (empty) continue;
        if (w != i) list[w] = std::move(list[i]);
        w++;
    }
    list.resize(w);
}

} // namespace indsin
