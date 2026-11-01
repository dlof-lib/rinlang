// indsin/rin_indsin_a11y.h — إمكانية الوصول: AriaLabel · AltText · AccessibleName · ScreenReader · SemanticText ·
// LanguageTag · ReadingOrder (+ ReduceMotion لحركة النص).
//
// شجرة وصول كاملة تُحسَب من الـFabric (بعد التخطيط) وتخدم ثلاثة مستهلكين:
//   1) rin_indsin_session_a11y_json / screenreader  — للمضيفات والأدوات (CI، المحرّر، قارئ شاشة محاكى)
//   2) a11yOutlineHtml — مرآة HTML دلالية مخفية بصرياً تُضمَّن في export_html (القارئ يقرأ هذه لا مربعات الرسم)
//   3) المدقّق (rin_indsin_audit.h): A11Y-006…A11Y-012
//
// السمات (كلها اختيارية؛ البرنامج القديم يُدقَّق ويُرسم كما كان):
//   aria_label= / accessible_name= / a11y_label=   اسم صريح (أعلى أولوية بعد aria_labelledby)
//   aria_labelledby="id1 id2"  aria_describedby="id"  aria_description=  أسماء/أوصاف مأخوذة من عناصر أخرى بالاسم (#id)
//   alt= / alt_text=  (Image/Icon/Avatar/Video) · decorative=true  · alt_long= وصف مطوّل
//   aria_hidden=true   خارج شجرة القارئ (يبقى مرسوماً)   ·  sr_only=true  لا يُرسم لكن يقرؤه القارئ   ·  sr_text=  نص للقارئ فقط
//   aria_live="polite|assertive|off"  aria_role / a11y_role  aria_expanded|checked|selected|pressed|disabled|current|level|valuenow|…
//   semantic="heading|paragraph|strong|emphasis|mark|code|quote|cite|abbr|time|address|caption|small|sub|sup|del|ins|label|…"
//   heading_level=1..6 (أو semantic="h2")      lang="ar" يورَّث · dir="rtl|ltr|auto"
//   reading_order=N على العنصر (ترتيب القارئ بين إخوته) · reading_order="visual|source" على الحاوية
#pragma once
#include "rin_indsin_paint.h"
#include "rin_indsin_query.h"
#include "rin_indsin_typography.h"
#include <map>
#include <sstream>

namespace indsin {
namespace a11y {

using typo::lower; using typo::trim; using typo::kebab;

inline bool flag(const Strand& s, const char* k) { return webTruthy(s.attr(k)); }
inline std::string jesc(const std::string& s) { return typo::jsonEsc(s); }

// ---- SemanticText ---------------------------------------------------------------------------------
struct Semantic { std::string tag; std::string role; int level = 0; bool known = false; };
inline Semantic semanticOf(const Strand& s) {
    Semantic r;
    std::string v = kebab(trim(s.attrStr("semantic", "")));
    if (v.empty() && s.attr("heading_level")) v = "heading";
    if (v.empty()) return r;
    r.known = true;
    if (v.size() == 2 && v[0] == 'h' && v[1] >= '1' && v[1] <= '6') { r.tag = v; r.role = "heading"; r.level = v[1] - '0'; return r; }
    if (v == "heading" || v == "title" || v == "header") {
        int lv = (int)s.attrNum("heading_level", s.attrNum("level", 2));
        r.level = std::max(1, std::min(6, lv)); r.tag = "h" + std::to_string(r.level); r.role = "heading"; return r;
    }
    struct M { const char* name; const char* tag; const char* role; };
    static const M t[] = {
        {"paragraph", "p", "paragraph"}, {"p", "p", "paragraph"}, {"strong", "strong", "strong"}, {"bold", "strong", "strong"},
        {"emphasis", "em", "emphasis"}, {"em", "em", "emphasis"}, {"italic", "em", "emphasis"}, {"mark", "mark", "mark"}, {"highlight", "mark", "mark"},
        {"code", "code", "code"}, {"quote", "blockquote", "blockquote"}, {"blockquote", "blockquote", "blockquote"}, {"cite", "cite", ""},
        {"abbr", "abbr", ""}, {"abbreviation", "abbr", ""}, {"time", "time", "time"}, {"address", "address", ""}, {"caption", "figcaption", "caption"},
        {"small", "small", ""}, {"sub", "sub", "subscript"}, {"sup", "sup", "superscript"}, {"del", "del", "deletion"}, {"ins", "ins", "insertion"},
        {"label", "label", ""}, {"pre", "pre", ""}, {"kbd", "kbd", ""}, {"term", "dfn", "term"}, {"definition", "dd", "definition"},
        {"list", "ul", "list"}, {"listitem", "li", "listitem"}, {"nav", "nav", "navigation"}, {"navigation", "nav", "navigation"}, {"main", "main", "main"},
        {"article", "article", "article"}, {"aside", "aside", "complementary"}, {"section", "section", "region"}, {"header", "header", "banner"},
        {"footer", "footer", "contentinfo"}, {"status", "output", "status"}, {"alert", "div", "alert"}, {"note", "div", "note"}, {"separator", "hr", "separator"},
    };
    for (auto& m : t) if (v == m.name) { r.tag = m.tag; r.role = m.role; return r; }
    r.known = false; return r;
}

// ---- AccessibleName / Description ----------------------------------------------------------------------------
inline StrandPtr byName(const StrandPtr& root, const std::string& name) {
    return findAny(root, [&](const StrandPtr& s) { return s->name == name; });
}
inline std::string textContentOf(const Strand& s) {
    std::string t = accessibleName(s);
    if (!t.empty()) return t;
    std::string acc;
    for (auto& c : s.children) { std::string x = textContentOf(*c); if (!x.empty()) acc += (acc.empty() ? "" : " ") + x; }
    return acc;
}
inline std::vector<std::string> idRefs(const Strand& s, const char* key) {
    std::vector<std::string> out; if (const Value* v = s.attr(key)) for (auto& w : typo::words(v->asString())) out.push_back(typo::unquote(w)); return out;
}
// ترتيب الأولوية (مبسّط عن accname 1.2): labelledby → aria_label/a11y_label → alt/label/text/placeholder → title → محتوى الأبناء → rotator
inline std::string nameOf(const StrandPtr& root, const Strand& s, int depth = 0) {
    if (flag(s, "decorative") && (s.kind == StrandKind::IMAGE || s.kind == StrandKind::ICON || s.kind == StrandKind::AVATAR)) return "";
    if (root && depth < 3) {
        auto refs = idRefs(s, "aria_labelledby");
        if (!refs.empty()) {
            std::string acc;
            for (auto& r : refs) if (auto t = byName(root, r)) { std::string x = nameOf(root, *t, depth + 1); if (x.empty()) x = textContentOf(*t); if (!x.empty()) acc += (acc.empty() ? "" : " ") + x; }
            if (!acc.empty()) return acc;
        }
    }
    std::string n = accessibleName(s);
    if (!n.empty()) return n;
    if (const Value* r = s.attr("text_rotator")) {   // نص دوّار: الاسم = كل العناصر (وليس ما يظهر الآن)
        std::string spec = r->asString(), out;
        auto parts = spec.find('|') != std::string::npos ? typo::splitTop(spec, '|') : typo::splitTop(spec, ',');
        for (auto& p : parts) out += (out.empty() ? "" : ", ") + typo::unquote(p);
        if (!out.empty()) return out;
    }
    if (isInteractiveKind(s.kind) || s.kind == StrandKind::CARD || s.kind == StrandKind::LISTITEM) { std::string c = textContentOf(s); if (!c.empty()) return c; }
    return "";
}
inline std::string descriptionOf(const StrandPtr& root, const Strand& s) {
    std::string d = s.attrStr("a11y_description", "");
    if (!d.empty()) return d;
    std::string acc;
    for (auto& r : idRefs(s, "aria_describedby")) if (auto t = byName(root, r)) { std::string x = textContentOf(*t); if (!x.empty()) acc += (acc.empty() ? "" : " ") + x; }
    return acc;
}

// ---- Role --------------------------------------------------------------------------------------------------
inline std::string roleOf(const Strand& s) {
    for (const char* k : {"aria_role", "a11y_role", "role_name"}) { std::string v = lower(trim(s.attrStr(k, ""))); if (!v.empty()) return v; }
    if (flag(s, "decorative") && (s.kind == StrandKind::IMAGE || s.kind == StrandKind::ICON)) return "presentation";
    Semantic sm = semanticOf(s);
    if (sm.known && !sm.role.empty()) return sm.role;
    std::string r = accessibleRole(s.kind);
    if (r.empty()) {
        switch (s.kind) {
            case StrandKind::HEADER: case StrandKind::TOPBAR: return "banner";
            case StrandKind::BOTTOMBAR: return "contentinfo";
            case StrandKind::DRAWER: return "complementary";
            case StrandKind::DIVIDER: return "separator";
            case StrandKind::SPINNER: return "progressbar";
            case StrandKind::BREADCRUMB: return "navigation";
            default: break;
        }
    }
    return r;
}

// ---- LanguageTag -------------------------------------------------------------------------------------------
// لغة العنصر الفعلية (تُورَّث من الأسلاف). CodeEditor: lang= هو لغة الشيفرة فلا يُعدّ وسماً للّغة البشرية.
inline std::string langAt(const std::vector<const Strand*>& path /* الجذر … العنصر */) {
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if ((*it)->kind == StrandKind::CODE_EDITOR) continue;
        for (const char* k : {"lang", "locale", "language_tag"}) { std::string v = trim((*it)->attrStr(k, "")); if (!v.empty()) { std::string n = typo::normalizeLangTag(typo::unquote(v)); return n; } }
    }
    return "";
}
inline std::string dirAt(const std::vector<const Strand*>& path) {
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        std::string d = lower(trim((*it)->attrStr("dir", "")));
        if (d == "rtl" || d == "ltr" || d == "auto") return d;
    }
    std::string l = langAt(path);
    return (!l.empty() && typo::isRtlLang(l)) ? "rtl" : "";
}

// ---- ReadingOrder ------------------------------------------------------------------------------------------
// ترتيب قراءة أبناء عنصر: reading_order=N على الأبناء (ثابت تصاعدياً)، أو reading_order="visual" على الأب (حسب الهندسة).
inline std::vector<StrandPtr> readingOrderChildren(const Strand& parent, bool rtl = false) {
    std::vector<StrandPtr> v(parent.children.begin(), parent.children.end());
    std::string mode = lower(trim(parent.attrStr("reading_order", "")));
    if (mode == "visual") {
        std::stable_sort(v.begin(), v.end(), [&](const StrandPtr& a, const StrandPtr& b) {
            double ay = a->geometry.y, by = b->geometry.y;
            if (std::fabs(ay - by) > std::min(a->geometry.h, b->geometry.h) * 0.5) return ay < by;
            return rtl ? a->geometry.x > b->geometry.x : a->geometry.x < b->geometry.x;
        });
        return v;
    }
    bool any = false;
    for (auto& c : v) if (c->attr("reading_order") && lower(c->attrStr("reading_order", "")) != "visual" && lower(c->attrStr("reading_order", "")) != "source") any = true;
    if (any) std::stable_sort(v.begin(), v.end(), [](const StrandPtr& a, const StrandPtr& b) { return a->attrNum("reading_order", 0) < b->attrNum("reading_order", 0); });
    return v;
}
// هل ترتيب القراءة يخالف الترتيب البصري؟ (WCAG 1.3.2) — للحاويات الخطية (Row/Column) فقط.
inline bool visualOrderDiffers(const Strand& s) {
    if (s.children.size() < 2) return false;
    bool row = (s.kind == StrandKind::ROW || s.kind == StrandKind::TOPBAR || s.kind == StrandKind::BOTTOMBAR || s.kind == StrandKind::HEADER);
    bool col = (s.kind == StrandKind::COLUMN || s.kind == StrandKind::LIST || s.kind == StrandKind::BOX || s.kind == StrandKind::CARD);
    if (!row && !col) return false;
    bool rtl = lower(s.attrStr("direction", s.attrStr("dir", ""))) == "rtl";
    auto ord = readingOrderChildren(s, rtl);
    std::vector<const Strand*> vis;
    for (auto& c : ord) if (!webOutOfFlow(*c) && !webCollapsed(*c) && c->geometry.w > 0.5 && c->geometry.h > 0.5) vis.push_back(c.get());
    for (size_t i = 1; i < vis.size(); i++) {
        double a = row ? vis[i - 1]->geometry.x : vis[i - 1]->geometry.y, b = row ? vis[i]->geometry.x : vis[i]->geometry.y;
        bool wrongWay = row ? (rtl ? b > a + 1.0 : b < a - 1.0) : (b < a - 1.0);
        if (wrongWay) return true;
    }
    return false;
}

// ---- شجرة الوصول + ScreenReader -------------------------------------------------------------------------------
struct Node {
    StrandId id = 0; std::string sname, kind, role, name, description, lang, dir, live, text, href;
    int level = 0, depth = 0, order = 0;
    bool interactive = false, srOnly = false, decorative = false, disabled = false, hasChecked = false, checked = false, hasExpanded = false, expanded = false;
    std::vector<std::pair<std::string, std::string>> aria;   // aria-* الحرفية
};
inline bool hiddenFromAT(const Strand& s) {
    return webTruthy(s.attr("aria_hidden")) || webCollapsed(s) || (webInvisible(s) && !flag(s, "sr_only"));
}
inline void buildNodes(const StrandPtr& root, const StrandPtr& s, std::vector<const Strand*>& path, int depth, std::vector<Node>& out) {
    if (!s) return;
    path.push_back(s.get());
    if (s->kind != StrandKind::BANNER && s->kind != StrandKind::TAG && s->attrStr("visible", "true") == "false" && !flag(*s, "sr_only")) { path.pop_back(); return; }
    if (hiddenFromAT(*s)) { path.pop_back(); return; }
    Node n; n.id = s->id; n.sname = s->name; n.kind = strandKindName(s->kind); n.role = roleOf(*s); n.depth = depth;
    n.name = nameOf(root, *s); n.description = descriptionOf(root, *s);
    n.lang = langAt(path); n.dir = dirAt(path);
    Semantic sm = semanticOf(*s); n.level = sm.level ? sm.level : (int)s->attrNum("aria_level", 0);
    n.interactive = isInteractiveKind(s->kind); n.srOnly = flag(*s, "sr_only"); n.decorative = flag(*s, "decorative");
    n.disabled = resolveState(*s) == StrandState::DISABLED || flag(*s, "disabled") || lower(s->attrStr("aria_disabled", "")) == "true";
    n.live = lower(s->attrStr("aria_live", "")); n.text = s->attrStr("sr_text", ""); n.href = s->attrStr("href", "");
    auto tri = [&](const char* k, bool& has, bool& val) { if (const Value* v = s->attr(k)) { has = true; val = webTruthy(v); } };
    tri("aria_checked", n.hasChecked, n.checked); if (!n.hasChecked) tri("checked", n.hasChecked, n.checked);
    tri("aria_expanded", n.hasExpanded, n.expanded); if (!n.hasExpanded) tri("expanded", n.hasExpanded, n.expanded);
    for (auto& a : s->attrs) if (a.key.rfind("aria_", 0) == 0) n.aria.push_back({"aria-" + a.key.substr(5), a.value.asString()});
    n.order = (int)out.size();
    bool skipSelf = (n.role.empty() && n.name.empty() && s->children.size() > 0 && !n.srOnly && sm.known == false);
    if (!skipSelf || n.interactive) out.push_back(n);
    bool rtl = n.dir == "rtl";
    // عناصر الأبناء تُقرأ بترتيب القراءة؛ ورقة بنص: الاسم يكفي (لا نكرّر أبناءها)
    bool leafNamed = !n.name.empty() && (s->kind == StrandKind::TEXT || s->kind == StrandKind::BUTTON || s->kind == StrandKind::LINK);
    if (!leafNamed) for (auto& c : readingOrderChildren(*s, rtl)) buildNodes(root, c, path, depth + (skipSelf ? 0 : 1), out);
    path.pop_back();
}
inline std::vector<Node> buildTree(const StrandPtr& root) {
    std::vector<Node> out; std::vector<const Strand*> path;
    buildNodes(root, root, path, 0, out);
    for (size_t i = 0; i < out.size(); i++) out[i].order = (int)i;
    return out;
}
inline std::string nodeLine(const Node& n) {
    std::string role = n.role.empty() ? "group" : n.role;
    std::string o = role;
    if (n.level) o += " level " + std::to_string(n.level);
    std::string name = n.name.empty() ? n.text : n.name;
    if (!name.empty()) o += ": " + name;
    std::vector<std::string> st;
    if (n.hasChecked) st.push_back(n.checked ? "checked" : "not checked");
    if (n.hasExpanded) st.push_back(n.expanded ? "expanded" : "collapsed");
    if (n.disabled) st.push_back("disabled");
    if (!n.description.empty()) st.push_back("description: " + n.description);
    for (auto& a : n.aria) if (a.first == "aria-current") st.push_back("current " + a.second);
    for (auto& x : st) o += ", " + x;
    if (!n.lang.empty()) o += " [" + n.lang + "]";
    return o;
}
// ScreenReader: ما يقوله القارئ بالترتيب، سطراً لكل عنصر. عناصر بلا اسم ولا دور مفيد تُتخطّى.
inline std::string screenReaderTranscript(const StrandPtr& root) {
    std::ostringstream os;
    for (auto& n : buildTree(root)) {
        if (n.role == "presentation" || n.role == "separator") continue;
        if (n.name.empty() && n.text.empty() && !n.interactive && n.role != "heading") continue;
        os << nodeLine(n) << "\n";
    }
    return os.str();
}
inline std::string treeToJson(const StrandPtr& root, const std::string& rootLang = "") {
    auto nodes = buildTree(root);
    std::ostringstream os;
    os << "{\"lang\":\"" << jesc(rootLang) << "\",\"count\":" << nodes.size() << ",\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); i++) {
        auto& n = nodes[i];
        os << (i ? "," : "") << "{\"id\":" << n.id << ",\"name\":\"" << jesc(n.sname) << "\",\"kind\":\"" << n.kind << "\",\"role\":\"" << jesc(n.role)
           << "\",\"label\":\"" << jesc(n.name) << "\",\"depth\":" << n.depth << ",\"order\":" << n.order;
        if (!n.description.empty()) os << ",\"description\":\"" << jesc(n.description) << "\"";
        if (n.level) os << ",\"level\":" << n.level;
        if (!n.lang.empty()) os << ",\"lang\":\"" << jesc(n.lang) << "\"";
        if (!n.dir.empty()) os << ",\"dir\":\"" << n.dir << "\"";
        if (!n.live.empty()) os << ",\"live\":\"" << jesc(n.live) << "\"";
        if (n.interactive) os << ",\"interactive\":true";
        if (n.srOnly) os << ",\"srOnly\":true";
        if (n.disabled) os << ",\"disabled\":true";
        if (n.hasChecked) os << ",\"checked\":" << (n.checked ? "true" : "false");
        if (n.hasExpanded) os << ",\"expanded\":" << (n.expanded ? "true" : "false");
        if (!n.href.empty()) os << ",\"href\":\"" << jesc(n.href) << "\"";
        if (!n.aria.empty()) { os << ",\"aria\":{"; for (size_t k = 0; k < n.aria.size(); k++) os << (k ? "," : "") << "\"" << jesc(n.aria[k].first) << "\":\"" << jesc(n.aria[k].second) << "\""; os << "}"; }
        os << "}";
    }
    os << "],\"transcript\":\"" << jesc(screenReaderTranscript(root)) << "\"}";
    return os.str();
}
inline std::string rootLangOf(const StrandPtr& root) {
    std::string found;
    walkFabric(root, [&](const StrandPtr& s, int, const Strand*) {
        if (!found.empty() || s->kind == StrandKind::CODE_EDITOR) return;
        for (const char* k : {"lang", "locale", "language_tag"}) { std::string v = trim(s->attrStr(k, "")); if (!v.empty()) { found = typo::normalizeLangTag(typo::unquote(v)); return; } }
    });
    return found;
}

// ---- Live regions: ما يعلنه القارئ عند تغيّر النص ------------------------------------------------------------------
struct LiveAnnouncement { StrandId id; std::string text, politeness; };
class LiveRegionTracker {
public:
    std::vector<LiveAnnouncement> update(const StrandPtr& root) {
        std::vector<LiveAnnouncement> out; std::map<StrandId, std::string> now;
        walkFabric(root, [&](const StrandPtr& s, int, const Strand*) {
            std::string live = lower(s->attrStr("aria_live", ""));
            if (live.empty() && s->kind == StrandKind::BANNER) live = "assertive";
            if (live.empty() && roleOf(*s) == "alert") live = "assertive";
            if (live.empty() && roleOf(*s) == "status") live = "polite";
            if (live.empty() || live == "off") return;
            std::string t = nameOf(root, *s); if (t.empty()) t = textContentOf(*s);
            now[s->id] = t;
            auto it = prev_.find(s->id);
            if (primed_ && (it == prev_.end() ? !t.empty() : it->second != t) && !t.empty()) out.push_back({s->id, t, live});
        });
        prev_ = now; primed_ = true; return out;
    }
private:
    std::map<StrandId, std::string> prev_; bool primed_ = false;
};

// ---- مرآة HTML دلالية (SemanticText + ARIA + Lang + ReadingOrder) ------------------------------------------------------
inline std::string esc(const std::string& s) {
    std::string o; for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; } return o;
}
inline void outlineInto(const StrandPtr& root, const StrandPtr& s, std::vector<const Strand*>& path, const std::string& inheritedLang, std::ostringstream& os, int depth) {
    path.push_back(s.get());
    if (hiddenFromAT(*s) || (s->attrStr("visible", "true") == "false" && !flag(*s, "sr_only") && s->kind != StrandKind::BANNER && s->kind != StrandKind::TAG)) { path.pop_back(); return; }
    std::string role = roleOf(*s), name = nameOf(root, *s), desc = descriptionOf(root, *s), lang = langAt(path), dir = dirAt(path);
    Semantic sm = semanticOf(*s);
    std::string tag; std::string attrs;
    if (!lang.empty() && lang != inheritedLang) attrs += " lang=\"" + esc(lang) + "\"";
    if (!dir.empty()) { std::string parentDir; if (path.size() > 1) { std::vector<const Strand*> pp(path.begin(), path.end() - 1); parentDir = dirAt(pp); } if (dir != parentDir) attrs += " dir=\"" + dir + "\""; }
    for (auto& a : s->attrs) if (a.key.rfind("aria_", 0) == 0 && a.key != "aria_hidden" && a.key != "aria_labelledby" && a.key != "aria_describedby") attrs += " aria-" + esc(a.key.substr(5)) + "=\"" + esc(a.value.asString()) + "\"";
    bool leaf = (s->kind == StrandKind::TEXT || s->kind == StrandKind::BUTTON || s->kind == StrandKind::LINK);
    if (sm.known && !sm.tag.empty()) tag = sm.tag;
    else if (s->kind == StrandKind::BUTTON || s->kind == StrandKind::ICONBUTTON) tag = "button";
    else if (s->kind == StrandKind::LINK) tag = "a";
    else if (s->kind == StrandKind::IMAGE) tag = "img";
    else if (s->kind == StrandKind::LIST) tag = "ul"; else if (s->kind == StrandKind::LISTITEM) tag = "li";
    else if (s->kind == StrandKind::TEXT) tag = "p";
    else if (role == "navigation") tag = "nav"; else if (role == "banner") tag = "header"; else if (role == "contentinfo") tag = "footer";
    else if (role == "main") tag = "main"; else if (role == "dialog") tag = "dialog";
    else if (s->kind == StrandKind::INPUT || s->kind == StrandKind::TEXTAREA || s->kind == StrandKind::SEARCH) tag = s->kind == StrandKind::TEXTAREA ? "textarea" : "input";
    else if (s->kind == StrandKind::CHECKBOX || s->kind == StrandKind::SWITCH || s->kind == StrandKind::RADIO) tag = "input";
    else tag = (role.empty() && name.empty()) ? "" : "div";
    bool explicitRole = s->attr("aria_role") || s->attr("a11y_role");
    if (explicitRole && !role.empty()) attrs += " role=\"" + esc(role) + "\"";
    else if (tag == "div" && !role.empty()) attrs += " role=\"" + esc(role) + "\"";
    if (role == "presentation") attrs += " role=\"presentation\"";
    if (!desc.empty()) attrs += " aria-description=\"" + esc(desc) + "\"";
    if (flag(*s, "disabled") || resolveState(*s) == StrandState::DISABLED) attrs += " aria-disabled=\"true\"";
    std::string live = lower(s->attrStr("aria_live", ""));
    if (!live.empty()) attrs += " aria-live=\"" + esc(live) + "\"";
    if (s->kind == StrandKind::CHECKBOX || s->kind == StrandKind::SWITCH || s->kind == StrandKind::RADIO) attrs += std::string(" type=\"") + (s->kind == StrandKind::RADIO ? "radio" : "checkbox") + "\"" + (webTruthy(s->attr("checked")) ? " checked" : "");
    if (tag == "img") { os << "<img" << attrs << " alt=\"" << esc(role == "presentation" ? "" : name) << "\">\n"; path.pop_back(); return; }
    if (tag == "input" || tag == "textarea") { os << "<" << tag << attrs << " aria-label=\"" << esc(name) << "\">" << (tag == "textarea" ? "</textarea>" : "") << "\n"; path.pop_back(); return; }
    if (tag == "a" && !s->attrStr("href", "").empty()) attrs += " href=\"" + esc(s->attrStr("href", "")) + "\"";
    if (tag == "abbr" && !s->attrStr("title", "").empty()) attrs += " title=\"" + esc(s->attrStr("title", "")) + "\"";
    if (tag == "time" && !s->attrStr("datetime", "").empty()) attrs += " datetime=\"" + esc(s->attrStr("datetime", "")) + "\"";
    if (tag.empty()) { for (auto& c : readingOrderChildren(*s, dir == "rtl")) outlineInto(root, c, path, lang.empty() ? inheritedLang : lang, os, depth); path.pop_back(); return; }
    bool hasLabelAttr = (tag == "div" || tag == "nav" || tag == "header" || tag == "footer" || tag == "main" || tag == "dialog" || tag == "section" || tag == "ul") && !name.empty() && !leaf;
    if (hasLabelAttr) attrs += " aria-label=\"" + esc(name) + "\"";
    os << "<" << tag << attrs << ">";
    if (leaf || !name.empty() && s->children.empty()) os << esc(name);
    else if (s->kind == StrandKind::TEXT) os << esc(name);
    if (!leaf) { os << (s->children.empty() ? "" : "\n"); for (auto& c : readingOrderChildren(*s, dir == "rtl")) outlineInto(root, c, path, lang.empty() ? inheritedLang : lang, os, depth + 1); }
    os << "</" << tag << ">\n";
    path.pop_back();
}
inline std::string outlineHtml(const StrandPtr& root) {
    std::ostringstream os; std::vector<const Strand*> path;
    outlineInto(root, root, path, rootLangOf(root), os, 0);
    return os.str();
}

} // namespace a11y
} // namespace indsin
