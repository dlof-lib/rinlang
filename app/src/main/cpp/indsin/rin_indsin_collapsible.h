// indsin/rin_indsin_collapsible.h — Collapsible lists: Accordion · ExpandableList · TreeView · NestedList.
//
// The seven structural kinds (StrandKind::ACCORDION, ACCORDION_ITEM, EXPANDABLE_LIST, TREE_VIEW,
// TREE_NODE, NESTED_LIST, NESTED_ITEM) follow the same "convenience synthesis" strategy as
// Tag/Pagination/Steps (rin_indsin_components_ext.h): a short declarative form desugars, at build
// time, into *real* Row/Column/Text/Divider/Button strands wired to auto-created Warp cells. No new
// measure/paint code exists for them -- layout is the ordinary vertical Column path and every host
// (Android, desktop, CLI, HTML export, live preview) draws them exactly like hand-written .rin.
//
// State model
//   * Every collapsible node owns one boolean Warp cell "<name>_open" ("true"/"false" strings, the
//     same convention Banner's "<name>_open" and Tag's "<name>_visible" use). `open=<cell>` binds an
//     existing cell instead (two-way: the author's cell is the one that gets toggled).
//   * Collapsed bodies are `visible=<cell>` -> they collapse to zero size and leave the layout.
//   * Headers/rows carry an onTap built from real AST nodes (toggle / accordion / seq / set), executed
//     by Needle's Action Engine like hand-written onTap=...; no interpreted-later strings.
//
// Actions added to the Action Engine (rin_indsin_actions.h):
//   accordion(cell, others...)  toggle `cell`; when it ends up open, close every cell in `others`
//   expandAll(cells...) / collapseAll(cells...)
//
// Authoring forms (all work in both `@view.X=name` and `<X#name>` syntax):
//   Accordion         multiple=false  variant=outlined|flush|filled  chevron=arrow|plus|ascii  controls=false
//     AccordionItem   title= subtitle= icon= badge= open= disabled=   (children = body)
//   ExpandableList    title= items="a,b,c" limit=3 open= marker=bullet|number|dash|none count=true
//   TreeView          paths="src/a.rin,src/b.rin" | nested TreeNode children
//                     selectable=true selected= bind=<cell> expanded=all|none controls=false indent=16 sort=false
//     TreeNode        label= value= icon= badge= open= disabled=
//   NestedList        items="Fruits,-Apple,-Pear,--Williams,Veg" marker=bullet|number|alpha|dash|none
//                     collapsible=false open=true indent=16
//     NestedItem      label=  (children = sub-items)
#pragma once
#include "rin_indsin_strand.h"
#include "../rin_ast.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

namespace indsin {
namespace collapsible {

// ---- small AST builders (real rin::Expr nodes, evaluated by the ordinary evalAttrExpr path) ----
inline rin::ExprPtr eVar(const std::string& n) {
    auto v = std::make_shared<rin::VariableExpr>(); v->name = n; return v;
}
inline rin::ExprPtr eStr(const std::string& s) {
    auto l = std::make_shared<rin::LiteralExpr>(); l->kind = rin::LiteralExpr::Kind::STRING; l->str = s; return l;
}
inline rin::ExprPtr eNum(double n) {
    auto l = std::make_shared<rin::LiteralExpr>(); l->kind = rin::LiteralExpr::Kind::NUMBER; l->number = n; return l;
}
inline rin::ExprPtr eCond(rin::ExprPtr c, rin::ExprPtr a, rin::ExprPtr b) {
    auto e = std::make_shared<rin::ConditionalExpr>(); e->condition = std::move(c); e->whenTrue = std::move(a); e->whenFalse = std::move(b); return e;
}
inline rin::ExprPtr ePlus(rin::ExprPtr a, rin::ExprPtr b) {
    auto e = std::make_shared<rin::BinaryExpr>(); e->left = std::move(a); e->op = rin::TokenType::PLUS; e->right = std::move(b); return e;
}
inline rin::ExprPtr eEq(rin::ExprPtr a, rin::ExprPtr b) {
    auto e = std::make_shared<rin::BinaryExpr>(); e->left = std::move(a); e->op = rin::TokenType::EQUAL_EQUAL; e->right = std::move(b); return e;
}
inline rin::ExprPtr eCall(const std::string& callee, std::vector<rin::ExprPtr> args) {
    auto c = std::make_shared<rin::CallExpr>(); c->callee = callee; c->args = std::move(args); return c;
}

// ---- helpers ------------------------------------------------------------------------------------
inline bool truthy(const Value& v) {
    return v.kind == Value::Kind::NUMBER ? v.number != 0.0 : !(v.str.empty() || v.str == "false" || v.str == "0");
}
inline bool boolAttr(const Strand& s, const std::string& k, bool def) {
    const Value* v = s.attr(k);
    return v ? truthy(*v) : def;
}
inline std::string trim(const std::string& in) {
    size_t a = in.find_first_not_of(" \t\r\n"), b = in.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : in.substr(a, b - a + 1);
}
// "a;b;c" when a ';' is present (lets items contain commas), else "a,b,c". Empties are dropped.
inline std::vector<std::string> splitItems(const std::string& in) {
    char sep = in.find(';') != std::string::npos ? ';' : ',';
    std::vector<std::string> out; std::string cur;
    for (char c : in) { if (c == sep) { cur = trim(cur); if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    cur = trim(cur); if (!cur.empty()) out.push_back(cur);
    return out;
}
inline std::string labelOf(const Strand& s) {
    for (const char* k : {"label", "title", "text"}) { std::string v = s.attrStr(k, ""); if (!v.empty()) return v; }
    return "";
}

struct Chev { std::string closed, open; };
inline Chev chevronFor(const std::string& style) {
    if (style == "plus")  return {"+", "-"};
    if (style == "ascii") return {">", "v"};
    return {"\xE2\x96\xB8", "\xE2\x96\xBE"}; // ▸ ▾
}

struct Ctx { WarpScope& warp; WarpSubscriptions& subs; };

inline StrandPtr make(StrandKind k, const Strand& owner, const std::string& base, const std::string& role, int idx = 0) {
    auto s = std::make_shared<Strand>();
    s->kind = k; s->sourceLine = owner.sourceLine; s->role = owner.role;
    s->name = base + "_" + role + (idx ? std::to_string(idx) : "");
    s->id = deriveId(base, role + (idx ? std::to_string(idx) : ""), idx, k);
    s->contentHash = fnv1a("cv_" + s->name);
    return s;
}
inline void put(const StrandPtr& s, const std::string& k, const std::string& v) {
    for (auto& a : s->attrs) if (a.key == k) { a.value = Value::txt(v); a.rawExpr = nullptr; return; }
    s->attrs.push_back({k, nullptr, Value::txt(v)});
}
inline void putIfAbsent(const StrandPtr& s, const std::string& k, const std::string& v) {
    if (!s->attr(k)) s->attrs.push_back({k, nullptr, Value::txt(v)});
}
// Attribute bound to an expression: evaluated now, re-resolved by the Shuttle when a read cell changes.
inline void bindAttr(Ctx& c, const StrandPtr& s, const std::string& k, const rin::ExprPtr& e) {
    std::vector<std::string> reads;
    Value v = evalAttrValue(e, c.warp, &reads);
    for (auto& r : reads) c.subs.record(r, s->id);
    for (auto& a : s->attrs) if (a.key == k) { a.rawExpr = e; a.value = v; return; }
    s->attrs.push_back({k, e, v});
}
// visible=/expanded= bound to a state cell, normalized to "true"/"false" (layout collapses on exactly
// "false", so an author's own cell holding 0 / "" still hides the body).
inline void bindFlag(Ctx& c, const StrandPtr& s, const std::string& k, const std::string& cell) {
    bindAttr(c, s, k, eCond(eVar(cell), eStr("true"), eStr("false")));
}
inline void onTap(Ctx& c, const StrandPtr& s, const rin::ExprPtr& call) {
    Value v = evalAttrExpr(call, c.warp, nullptr); // handler descriptor; Needle runs the AST itself
    for (auto& a : s->attrs) if (a.key == "onTap") { a.rawExpr = call; a.value = v; return; }
    s->attrs.push_back({"onTap", call, v});
}
inline StrandPtr text(Ctx&, const Strand& owner, const std::string& base, const std::string& role, const std::string& t,
                      const std::string& tone = "", const std::string& size = "") {
    auto s = make(StrandKind::TEXT, owner, base, role);
    put(s, "text", t);
    if (!tone.empty()) put(s, "tone", tone);
    if (!size.empty()) put(s, "size", size);
    return s;
}
inline void rehash(const StrandPtr& s) {
    uint64_t childAcc = 0;
    for (auto& c : s->children) childAcc = fnv1a(std::to_string(c->contentHash), childAcc);
    uint64_t attrHash = 1469598103934665603ULL;
    for (auto& a : s->attrs) attrHash = fnv1a(a.key + "=" + a.value.asString(), attrHash);
    s->contentHash = fnv1a(strandKindName(s->kind), fnv1a(std::to_string(attrHash), childAcc));
}
inline void rehashTree(const StrandPtr& s) { for (auto& c : s->children) rehashTree(c); rehash(s); }

// The cell that holds a node's open state: `open=<cell>` binds an existing one; otherwise "<base>_open".
// `init` receives the initial truthiness (from open=true/false or the bound cell).
inline std::string openCellFor(Ctx& c, const Strand& s, const std::string& base, bool defaultOpen) {
    std::string cell = base + "_open";
    bool init = defaultOpen;
    if (const Value* v = s.attr("open")) {
        init = truthy(*v);
        for (auto& a : s.attrs) if (a.key == "open")
            if (auto var = std::dynamic_pointer_cast<rin::VariableExpr>(a.rawExpr))
                if (c.warp.has(var->name)) { cell = var->name; init = truthy(c.warp.get(cell)); }
    }
    if (!c.warp.has(cell)) c.warp.set(cell, Value::txt(init ? "true" : "false"));
    return cell;
}
inline std::string baseName(Strand& s, const std::string& parentBase, int idx, const char* prefix) {
    if (s.name.empty()) s.name = parentBase + "_" + prefix + std::to_string(idx);
    return s.name;
}
// Controls row: "Expand all" / "Collapse all" ghost buttons bound to expandAll()/collapseAll().
inline StrandPtr makeControls(Ctx& c, const Strand& owner, const std::string& base, const std::vector<std::string>& cells) {
    auto row = make(StrandKind::ROW, owner, base, "controls");
    put(row, "gap", "8"); put(row, "padding", "4");
    for (int i = 0; i < 2; ++i) {
        auto b = make(StrandKind::BUTTON, owner, base, i == 0 ? "expand_all" : "collapse_all");
        put(b, "label", i == 0 ? "Expand all" : "Collapse all");
        put(b, "variant", "ghost"); put(b, "size", "xs");
        std::vector<rin::ExprPtr> args; for (auto& n : cells) args.push_back(eVar(n));
        onTap(c, b, eCall(i == 0 ? "expandAll" : "collapseAll", args));
        rehash(b);
        row->children.push_back(b);
    }
    rehash(row);
    return row;
}

// Builds the tappable header Row shared by AccordionItem and ExpandableList(header mode).
//   [icon] Title/Subtitle ........ [badge] [chevron]
inline StrandPtr makeHeader(Ctx& c, const Strand& owner, const std::string& base, const std::string& cell,
                            const std::string& title, const std::string& subtitle, const std::string& icon,
                            const std::string& badge, const Chev& chev, bool disabled, bool filled,
                            const rin::ExprPtr& tapCall) {
    auto h = make(StrandKind::ROW, owner, base, "header");
    put(h, "padding", "12"); put(h, "gap", "8");
    put(h, "a11y_label", title);
    bindFlag(c, h, "expanded", cell);
    if (filled) put(h, "bg", "surface");
    if (disabled) put(h, "disabled", "true");
    else onTap(c, h, tapCall);
    if (!icon.empty()) h->children.push_back(text(c, owner, base, "icon", icon));
    if (subtitle.empty()) {
        h->children.push_back(text(c, owner, base, "title", title, disabled ? "text_muted" : "", "title"));
    } else {
        auto col = make(StrandKind::COLUMN, owner, base, "heading");
        put(col, "gap", "2");
        col->children.push_back(text(c, owner, base, "title", title, disabled ? "text_muted" : "", "title"));
        col->children.push_back(text(c, owner, base, "subtitle", subtitle, "text_muted", "small"));
        rehash(col);
        h->children.push_back(col);
    }
    auto sp = make(StrandKind::SPACER, owner, base, "push"); rehash(sp); h->children.push_back(sp);
    if (!badge.empty()) h->children.push_back(text(c, owner, base, "badge", badge, "text_muted", "small"));
    auto g = make(StrandKind::TEXT, owner, base, "chevron");
    bindAttr(c, g, "text", eCond(eVar(cell), eStr(chev.open), eStr(chev.closed)));
    put(g, "tone", "text_muted");
    rehash(g);
    h->children.push_back(g);
    for (auto& ch : h->children) rehashTree(ch);
    rehash(h);
    return h;
}

// Rebuilds one collapsible block (AccordionItem / header-mode ExpandableList) as
//   Column[ (Divider)? header, Divider(visible=cell), body Column(visible=cell){original children} ]
inline void buildCollapsible(Ctx& c, const StrandPtr& s, const std::string& base, const std::string& cell,
                             const Chev& chev, bool filled, bool separatorAbove, const rin::ExprPtr& tapCall) {
    std::vector<StrandPtr> original = s->children;
    std::vector<StrandPtr> out;
    if (separatorAbove) { auto d = make(StrandKind::DIVIDER, *s, base, "sep"); rehash(d); out.push_back(d); }
    out.push_back(makeHeader(c, *s, base, cell, s->attrStr("title", labelOf(*s)), s->attrStr("subtitle", ""),
                             s->attrStr("icon", ""), s->attrStr("badge", ""), chev,
                             boolAttr(*s, "disabled", false), filled, tapCall));
    auto line = make(StrandKind::DIVIDER, *s, base, "line");
    bindFlag(c, line, "visible", cell); rehash(line); out.push_back(line);
    auto body = make(StrandKind::COLUMN, *s, base, "body");
    put(body, "padding", "12"); put(body, "gap", "8"); put(body, "align", "stretch");
    bindFlag(c, body, "visible", cell);
    body->children = original;
    rehashTree(body);
    out.push_back(body);
    s->children = out;
    putIfAbsent(s, "gap", "0"); putIfAbsent(s, "align", "stretch");
    put(s, "_collapsible", "1");
    bindFlag(c, s, "expanded", cell);
}

// ---- Accordion ----------------------------------------------------------------------------------
inline void processAccordion(Ctx& c, const StrandPtr& acc) {
    std::string base = acc->name.empty() ? "accordion" : acc->name;
    if (acc->name.empty()) acc->name = base;
    bool multiple = boolAttr(*acc, "multiple", false);
    std::string variant = acc->attrStr("variant", "outlined");
    Chev chev = chevronFor(acc->attrStr("chevron", "arrow"));

    std::vector<StrandPtr> items; std::vector<std::string> cells;
    int idx = 0;
    for (auto& ch : acc->children) {
        if (ch->kind != StrandKind::ACCORDION_ITEM || ch->attr("_collapsible")) continue;
        std::string nm = baseName(*ch, base, idx++, "item");
        items.push_back(ch);
        cells.push_back(openCellFor(c, *ch, nm, false));
    }
    if (!multiple) { // exclusive mode: at most one section starts open (the first one asked for)
        bool seen = false;
        for (auto& cell : cells) {
            if (truthy(c.warp.get(cell))) { if (seen) c.warp.set(cell, Value::txt("false")); seen = true; }
        }
    }
    for (size_t i = 0; i < items.size(); ++i) {
        rin::ExprPtr tap;
        if (multiple) tap = eCall("toggle", {eVar(cells[i])});
        else {
            std::vector<rin::ExprPtr> args{eVar(cells[i])};
            for (size_t j = 0; j < cells.size(); ++j) if (j != i) args.push_back(eVar(cells[j]));
            tap = eCall("accordion", args);
        }
        buildCollapsible(c, items[i], items[i]->name, cells[i], chev, variant == "filled", i > 0, tap);
        rehash(items[i]);
    }
    if (acc->attr("controls") && boolAttr(*acc, "controls", false) && multiple && !cells.empty())
        acc->children.insert(acc->children.begin(), makeControls(c, *acc, base, cells));
    if (variant != "flush") { putIfAbsent(acc, "border", "1"); putIfAbsent(acc, "radius", "12"); }
    putIfAbsent(acc, "gap", "0"); putIfAbsent(acc, "align", "stretch");
    put(acc, "_collapsible", "1");
    rehashTree(acc);
}

// Stand-alone AccordionItem (Collapsible/Details/Disclosure): its own independent toggle.
inline void processStandaloneItem(Ctx& c, const StrandPtr& it) {
    std::string base = it->name.empty() ? "collapsible" : it->name;
    if (it->name.empty()) it->name = base;
    std::string cell = openCellFor(c, *it, base, false);
    buildCollapsible(c, it, base, cell, chevronFor(it->attrStr("chevron", "arrow")),
                     it->attrStr("variant", "") == "filled", false, eCall("toggle", {eVar(cell)}));
    if (it->attrStr("variant", "outlined") != "flush") { putIfAbsent(it, "border", "1"); putIfAbsent(it, "radius", "12"); }
    rehashTree(it);
}

// ---- ExpandableList -----------------------------------------------------------------------------
inline StrandPtr makeListRow(Ctx& c, const Strand& owner, const std::string& base, int idx, const std::string& marker,
                             const std::string& label) {
    std::string n = std::to_string(idx + 1);
    auto row = make(StrandKind::ROW, owner, base, "row", idx + 1);
    put(row, "gap", "8"); put(row, "padding", "4");
    if (!marker.empty()) row->children.push_back(text(c, owner, base, "mark" + n, marker, "text_muted"));
    row->children.push_back(text(c, owner, base, "label" + n, label));
    for (auto& ch : row->children) rehash(ch);
    rehash(row);
    return row;
}
inline std::string markerFor(const std::string& style, int idx) {
    if (style == "none")   return "";
    if (style == "number") return std::to_string(idx + 1) + ".";
    if (style == "dash")   return "-";
    return "\xE2\x80\xA2"; // •
}

inline void processExpandableList(Ctx& c, const StrandPtr& el) {
    std::string base = el->name.empty() ? "expandable" : el->name;
    if (el->name.empty()) el->name = base;
    std::string style = el->attrStr("marker", "bullet");
    Chev chev = chevronFor(el->attrStr("chevron", "arrow"));
    int limit = (int)el->attrNum("limit", 0);

    // Rows: real children win; otherwise items="a,b,c" synthesizes one row per entry.
    std::vector<StrandPtr> rows = el->children;
    if (rows.empty()) {
        auto items = splitItems(el->attrStr("items", ""));
        for (size_t i = 0; i < items.size(); ++i) rows.push_back(makeListRow(c, *el, base, (int)i, markerFor(style, (int)i), items[i]));
    }
    int n = (int)rows.size();
    std::string title = el->attrStr("title", labelOf(*el));
    if (boolAttr(*el, "count", true) && !title.empty() && !el->attr("badge")) put(el, "badge", std::to_string(n));

    std::string cell = openCellFor(c, *el, base, false);
    if (limit > 0 && n > limit) {
        // Preview mode: static header, first `limit` rows always shown, the rest follow the cell,
        // and a footer button flips it ("Show more (k)" / "Show less").
        std::vector<StrandPtr> out;
        if (!title.empty()) {
            auto h = make(StrandKind::ROW, *el, base, "header");
            put(h, "padding", "8"); put(h, "gap", "8");
            h->children.push_back(text(c, *el, base, "title", title, "", "title"));
            auto sp = make(StrandKind::SPACER, *el, base, "push"); rehash(sp); h->children.push_back(sp);
            if (!el->attrStr("badge", "").empty()) h->children.push_back(text(c, *el, base, "badge", el->attrStr("badge", ""), "text_muted", "small"));
            for (auto& ch : h->children) rehash(ch);
            rehash(h); out.push_back(h);
        }
        for (int i = 0; i < n; ++i) {
            if (i >= limit) bindFlag(c, rows[i], "visible", cell);
            rehashTree(rows[i]);
            out.push_back(rows[i]);
        }
        auto more = make(StrandKind::BUTTON, *el, base, "more");
        std::string moreLbl = el->attrStr("more", "Show more"), lessLbl = el->attrStr("less", "Show less");
        bindAttr(c, more, "label", eCond(eVar(cell), eStr(lessLbl),
                                      ePlus(eStr(moreLbl + " ("), eStr(std::to_string(n - limit) + ")"))));
        put(more, "variant", "ghost"); put(more, "size", "xs");
        bindFlag(c, more, "expanded", cell);
        onTap(c, more, eCall("toggle", {eVar(cell)}));
        rehash(more); out.push_back(more);
        el->children = out;
        putIfAbsent(el, "gap", "2"); putIfAbsent(el, "align", "stretch");
        put(el, "_collapsible", "1"); bindFlag(c, el, "expanded", cell);
        rehash(el);
        return;
    }
    // Header mode: the whole list folds away.
    el->children = rows;
    buildCollapsible(c, el, base, cell, chev, false, false, eCall("toggle", {eVar(cell)}));
    if (el->attrStr("variant", "outlined") != "flush") { putIfAbsent(el, "border", "1"); putIfAbsent(el, "radius", "12"); }
    rehashTree(el);
}

// ---- TreeView -----------------------------------------------------------------------------------
struct PathNode { std::string name; bool leaf = true; std::vector<std::shared_ptr<PathNode>> kids; };
inline void insertPath(const std::shared_ptr<PathNode>& root, const std::string& path) {
    std::vector<std::string> parts; std::string cur; bool endsSlash = !path.empty() && path.back() == '/';
    for (char ch : path) { if (ch == '/') { if (!cur.empty()) parts.push_back(cur); cur.clear(); } else cur += ch; }
    if (!cur.empty()) parts.push_back(cur);
    auto node = root;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::shared_ptr<PathNode> next;
        for (auto& k : node->kids) if (k->name == parts[i]) { next = k; break; }
        if (!next) { next = std::make_shared<PathNode>(); next->name = parts[i]; node->kids.push_back(next); }
        bool last = (i + 1 == parts.size());
        if (!last || endsSlash) next->leaf = false;
        node = next;
    }
}
inline void sortPathTree(const std::shared_ptr<PathNode>& n) {
    std::stable_sort(n->kids.begin(), n->kids.end(), [](const auto& a, const auto& b) {
        if (a->leaf != b->leaf) return !a->leaf; // folders first
        return a->name < b->name;
    });
    for (auto& k : n->kids) sortPathTree(k);
}
inline std::string sanitizeId(const std::string& in) {
    std::string out;
    for (unsigned char ch : in) out += (std::isalnum(ch) ? (char)ch : '_');
    return out;
}
inline StrandPtr nodeFromPath(const Strand& owner, const std::shared_ptr<PathNode>& pn, const std::string& base, const std::string& prefix) {
    auto s = std::make_shared<Strand>();
    s->kind = StrandKind::TREE_NODE; s->sourceLine = owner.sourceLine; s->role = owner.role;
    s->name = base + "_" + sanitizeId(prefix + pn->name);
    s->id = deriveId(base, s->name, 0, StrandKind::TREE_NODE);
    s->attrs.push_back({"label", nullptr, Value::txt(pn->name)});
    s->attrs.push_back({"value", nullptr, Value::txt(prefix + pn->name)});
    for (auto& k : pn->kids) s->children.push_back(nodeFromPath(owner, k, base, prefix + pn->name + "/"));
    return s;
}

struct TreeOpts {
    std::string selCell; bool selectable = true; bool expandAll = false;
    int indent = 16; Chev chev; std::string folderIcon, fileIcon;
    std::vector<std::string>* branchCells = nullptr;
};

inline void buildTreeNode(Ctx& c, const StrandPtr& node, const std::string& parentBase, int idx, const TreeOpts& o) {
    std::string base = baseName(*node, parentBase, idx, "node");
    std::string label = labelOf(*node);
    std::string value = node->attrStr("value", label);
    bool branch = false;
    std::vector<StrandPtr> kids;
    for (auto& ch : node->children) if (ch->kind == StrandKind::TREE_NODE) { branch = true; kids.push_back(ch); }
    bool disabled = boolAttr(*node, "disabled", false);

    std::string cell;
    if (branch) {
        cell = openCellFor(c, *node, base, o.expandAll);
        if (o.expandAll) c.warp.set(cell, Value::txt("true"));
        if (o.branchCells) o.branchCells->push_back(cell);
    }

    auto row = make(StrandKind::ROW, *node, base, "row");
    put(row, "gap", "6"); put(row, "padding", "5");
    put(row, "a11y_label", label);
    if (o.selectable && !o.selCell.empty()) {
        bindAttr(c, row, "bg", eCond(eEq(eVar(o.selCell), eStr(value)), eStr("primary"), eStr("")));
        bindAttr(c, row, "selected", eCond(eEq(eVar(o.selCell), eStr(value)), eStr("true"), eStr("false")));
    }
    if (branch) bindFlag(c, row, "expanded", cell);

    // glyph slot (fixed width so leaves and branches line up)
    auto glyph = make(StrandKind::TEXT, *node, base, "glyph");
    put(glyph, "width", "16"); put(glyph, "tone", "text_muted");
    if (branch) {
        bindAttr(c, glyph, "text", eCond(eVar(cell), eStr(o.chev.open), eStr(o.chev.closed)));
        if (!disabled) onTap(c, glyph, eCall("toggle", {eVar(cell)}));
    } else put(glyph, "text", " ");
    rehash(glyph);
    row->children.push_back(glyph);

    std::string icon = node->attrStr("icon", branch ? o.folderIcon : o.fileIcon);
    if (!icon.empty()) { auto ic = text(c, *node, base, "icon", icon); rehash(ic); row->children.push_back(ic); }

    auto lbl = make(StrandKind::TEXT, *node, base, "label");
    if (o.selectable && !o.selCell.empty() && !disabled)
        bindAttr(c, lbl, "tone", eCond(eEq(eVar(o.selCell), eStr(value)), eStr("background"), eStr("text")));
    else if (disabled) put(lbl, "tone", "text_muted");
    put(lbl, "text", label);
    rehash(lbl);
    row->children.push_back(lbl);

    std::string badge = node->attrStr("badge", "");
    if (!badge.empty()) {
        auto sp = make(StrandKind::SPACER, *node, base, "push"); rehash(sp); row->children.push_back(sp);
        auto b = text(c, *node, base, "badge", badge, "text_muted", "small"); rehash(b); row->children.push_back(b);
    }

    if (!disabled) {
        std::vector<rin::ExprPtr> steps;
        if (branch) steps.push_back(eCall("toggle", {eVar(cell)}));
        if (o.selectable && !o.selCell.empty()) steps.push_back(eCall("set", {eVar(o.selCell), eStr(value)}));
        if (steps.size() == 1) onTap(c, row, steps[0]);
        else if (steps.size() > 1) onTap(c, row, eCall("seq", steps));
    } else put(row, "disabled", "true");
    rehash(row);

    std::vector<StrandPtr> out{row};
    if (branch) {
        auto sub = make(StrandKind::COLUMN, *node, base, "children");
        put(sub, "margin_left", std::to_string(o.indent)); put(sub, "gap", "0"); put(sub, "align", "stretch");
        bindFlag(c, sub, "visible", cell);
        int i = 0;
        for (auto& k : kids) { buildTreeNode(c, k, base, i++, o); sub->children.push_back(k); }
        rehash(sub);
        out.push_back(sub);
    }
    node->children = out;
    putIfAbsent(node, "gap", "0"); putIfAbsent(node, "align", "stretch");
    put(node, "_collapsible", "1");
    if (branch) bindFlag(c, node, "expanded", cell);
    rehash(node);
}

inline void processTreeView(Ctx& c, const StrandPtr& tv) {
    std::string base = tv->name.empty() ? "tree" : tv->name;
    if (tv->name.empty()) tv->name = base;

    // paths="src/a.rin,src/b.rin,README.md" shorthand -> TreeNode children (only when none were nested by hand).
    if (tv->children.empty() && tv->attr("paths")) {
        auto root = std::make_shared<PathNode>();
        for (auto& p : splitItems(tv->attrStr("paths", ""))) insertPath(root, p);
        if (boolAttr(*tv, "sort", false)) sortPathTree(root);
        for (auto& k : root->kids) tv->children.push_back(nodeFromPath(*tv, k, base, ""));
    }

    TreeOpts o;
    o.selectable = boolAttr(*tv, "selectable", true);
    o.expandAll = tv->attrStr("expanded", "none") == "all";
    o.indent = std::max(0, (int)tv->attrNum("indent", 16));
    o.chev = chevronFor(tv->attrStr("chevron", "arrow"));
    o.folderIcon = tv->attrStr("folderIcon", ""); o.fileIcon = tv->attrStr("fileIcon", "");
    std::string bound = tv->attrStr("bind", "");
    o.selCell = (!bound.empty() && c.warp.has(bound)) ? bound : base + "_selected";
    if (!c.warp.has(o.selCell)) c.warp.set(o.selCell, Value::txt(tv->attrStr("selected", "")));
    std::vector<std::string> branchCells;
    o.branchCells = &branchCells;

    std::vector<StrandPtr> top = tv->children, out;
    int i = 0;
    for (auto& ch : top) {
        if (ch->kind != StrandKind::TREE_NODE) { out.push_back(ch); continue; }
        buildTreeNode(c, ch, base, i++, o);
        out.push_back(ch);
    }
    if (boolAttr(*tv, "controls", false) && !branchCells.empty())
        out.insert(out.begin(), makeControls(c, *tv, base, branchCells));
    tv->children = out;
    putIfAbsent(tv, "gap", "0"); putIfAbsent(tv, "align", "stretch");
    put(tv, "_collapsible", "1");
    bindAttr(c, tv, "selected", eVar(o.selCell));
    rehashTree(tv);
}

// ---- NestedList ---------------------------------------------------------------------------------
struct NestOpts { std::string marker = "bullet"; bool collapsible = false; bool openDefault = true; int indent = 16; Chev chev; };

inline std::string alphaMarker(int depth, int n) { // A. / a. / i. / 1. cycling by depth
    static const char* roman[] = {"i","ii","iii","iv","v","vi","vii","viii","ix","x"};
    switch (depth % 3) {
        case 0: return std::string(1, (char)('A' + (n % 26))) + ".";
        case 1: return std::string(1, (char)('a' + (n % 26))) + ".";
        default: return std::string(roman[n % 10]) + ".";
    }
}

inline void buildNestedItem(Ctx& c, const StrandPtr& it, const std::string& parentBase, int idx, int depth,
                            const std::string& numberPrefix, const NestOpts& o) {
    std::string base = baseName(*it, parentBase, idx, "item");
    std::string label = labelOf(*it);
    std::vector<StrandPtr> kids;
    for (auto& ch : it->children) if (ch->kind == StrandKind::NESTED_ITEM) kids.push_back(ch);
    bool branch = !kids.empty();
    bool fold = o.collapsible && branch;
    std::string cell;
    if (fold) cell = openCellFor(c, *it, base, o.openDefault);

    std::string number = numberPrefix + std::to_string(idx + 1);
    std::string marker;
    if (o.marker == "number") marker = number + ".";
    else if (o.marker == "alpha") marker = alphaMarker(depth, idx);
    else if (o.marker == "dash") marker = "-";
    else if (o.marker == "none") marker = "";
    else { static const char* b[] = {"\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA"}; marker = b[depth % 3]; } // • ◦ ▪

    auto row = make(StrandKind::ROW, *it, base, "row");
    put(row, "gap", "6"); put(row, "padding", "3");
    if (fold) {
        auto g = make(StrandKind::TEXT, *it, base, "glyph");
        put(g, "tone", "text_muted"); put(g, "width", "16");
        bindAttr(c, g, "text", eCond(eVar(cell), eStr(o.chev.open), eStr(o.chev.closed)));
        rehash(g); row->children.push_back(g);
        onTap(c, row, eCall("toggle", {eVar(cell)}));
        bindFlag(c, row, "expanded", cell);
        put(row, "a11y_label", label);
    } else if (!marker.empty()) {
        auto m = text(c, *it, base, "mark", marker, "text_muted"); rehash(m); row->children.push_back(m);
    }
    auto t = text(c, *it, base, "label", label); rehash(t); row->children.push_back(t);
    rehash(row);

    std::vector<StrandPtr> out{row};
    if (branch) {
        auto sub = make(StrandKind::COLUMN, *it, base, "children");
        put(sub, "margin_left", std::to_string(o.indent)); put(sub, "gap", "0"); put(sub, "align", "stretch");
        if (fold) bindFlag(c, sub, "visible", cell);
        int i = 0;
        for (auto& k : kids) { buildNestedItem(c, k, base, i++, depth + 1, number + ".", o); sub->children.push_back(k); }
        rehash(sub);
        out.push_back(sub);
    }
    it->children = out;
    putIfAbsent(it, "gap", "0"); putIfAbsent(it, "align", "stretch");
    put(it, "_collapsible", "1");
    rehash(it);
}

inline void processNestedList(Ctx& c, const StrandPtr& nl) {
    std::string base = nl->name.empty() ? "nested" : nl->name;
    if (nl->name.empty()) nl->name = base;
    NestOpts o;
    o.marker = nl->attrStr("marker", "bullet");
    o.collapsible = boolAttr(*nl, "collapsible", false);
    o.openDefault = boolAttr(*nl, "open", true);
    o.indent = std::max(0, (int)nl->attrNum("indent", 16));
    o.chev = chevronFor(nl->attrStr("chevron", "arrow"));

    // items="Fruits,-Apple,-Pear,--Williams,Veg": each leading '-' is one nesting level.
    if (nl->children.empty() && nl->attr("items")) {
        std::vector<StrandPtr> stack; // stack[d] = last item created at depth d
        int seq = 0;
        for (auto& raw : splitItems(nl->attrStr("items", ""))) {
            int depth = 0; while (depth < (int)raw.size() && raw[depth] == '-') ++depth;
            std::string lbl = trim(raw.substr(depth));
            if (lbl.empty()) continue;
            if (depth > (int)stack.size()) depth = (int)stack.size(); // no skipped levels
            auto it = std::make_shared<Strand>();
            it->kind = StrandKind::NESTED_ITEM; it->sourceLine = nl->sourceLine; it->role = nl->role;
            it->name = base + "_i" + std::to_string(++seq);
            it->id = deriveId(base, it->name, seq, StrandKind::NESTED_ITEM);
            it->attrs.push_back({"label", nullptr, Value::txt(lbl)});
            stack.resize(depth);
            if (depth == 0) nl->children.push_back(it); else stack[depth - 1]->children.push_back(it);
            stack.push_back(it);
        }
    }
    int i = 0;
    for (auto& ch : nl->children) if (ch->kind == StrandKind::NESTED_ITEM) buildNestedItem(c, ch, base, i++, 0, "", o);
    putIfAbsent(nl, "gap", "0"); putIfAbsent(nl, "align", "stretch");
    put(nl, "_collapsible", "1");
    rehashTree(nl);
}

// ---- entry point --------------------------------------------------------------------------------
inline void applyAll(Ctx& c, const StrandPtr& s) {
    // Process children *after* the parent: a body moved into an AccordionItem can itself hold
    // another Accordion/TreeView/NestedList, which this recursion then reaches.
    if (!s->attr("_collapsible")) {
        switch (s->kind) {
            case StrandKind::ACCORDION:       processAccordion(c, s); break;
            case StrandKind::ACCORDION_ITEM:  processStandaloneItem(c, s); break;
            case StrandKind::EXPANDABLE_LIST: processExpandableList(c, s); break;
            case StrandKind::TREE_VIEW:       processTreeView(c, s); break;
            case StrandKind::NESTED_LIST:     processNestedList(c, s); break;
            default: break;
        }
    }
    for (auto& ch : s->children) applyAll(c, ch);
}

} // namespace collapsible

inline void applyCollapsibleConveniences(const StrandPtr& fabric, WarpScope& warp, WarpSubscriptions& subs) {
    std::vector<std::string> before; before.reserve(warp.cells.size());
    for (auto& kv : warp.cells) before.push_back(kv.first);
    std::sort(before.begin(), before.end());
    collapsible::Ctx c{warp, subs};
    collapsible::applyAll(c, fabric);

    // Cells this pass created (<name>_open, <tree>_selected) did not exist when buildFabric first
    // resolved author expressions such as `<Text>Selected: {files_selected}</Text>`; those attrs
    // subscribed to the name but captured a placeholder. Re-resolve exactly those subscribers once.
    std::unordered_map<StrandId, bool> stale;
    for (auto& kv : warp.cells)
        if (!std::binary_search(before.begin(), before.end(), kv.first)) {
            auto it = subs.byName.find(kv.first);
            if (it != subs.byName.end()) for (auto id : it->second) stale[id] = true;
        }
    if (stale.empty()) return;
    std::function<void(const StrandPtr&)> walk = [&](const StrandPtr& s) {
        if (stale.count(s->id))
            for (auto& a : s->attrs)
                if (a.rawExpr && !isHandlerKey(a.key)) a.value = evalAttrExprKeyed(a.key, a.rawExpr, warp, nullptr);
        for (auto& ch : s->children) walk(ch);
    };
    walk(fabric);
    recomputeHashes(fabric);
}

} // namespace indsin
