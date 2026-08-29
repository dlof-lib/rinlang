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
    TEXT, IMAGE, BUTTON, CARD, COLUMN, ROW, STACK, DIVIDER,

    // Extended UI kinds required by rin_loom_layout.h
    HEADER, TOPBAR, BOTTOMBAR, DRAWER, MENU, MENUITEM, TABLE, TABLEROW,
    VIDEO, AUDIO, WEBVIEW, SCAFFOLD, SPLASH,

    // Banner: a dismissible, transient-or-persistent notice bar (see docs/banner.md). Deliberately
    // NOT a new parallel system -- it is just another StrandKind produced by the same
    // rin::ViewStmt -> buildFabric() pipeline every other kind above goes through (@view.Banner=...),
    // and it reuses MENU/MENUITEM/DRAWER for its optional popup menu (banner { menu { item ... } }
    // desugars to a child MENU strand, not a separate menu engine).
    BANNER,

    // ---- Missing-components pass (stdlib §27): Grid/Input/Dialog/Tabs + the rest of the easy
    // wins that reuse existing measure/paint primitives instead of inventing new machinery.
    //
    // BOX resolves the "Container naming collision (still open)" note in
    // docs/loomtime/RIN_LOOM_TOKENS.md: `Container` already means a data/storage construct in
    // this codebase (`@container`, `ContainerKind::{...}`), so the spec's layout-Container
    // primitive is named BOX internally (matching CARD/CARD-style naming already in this enum) —
    // option (b) from that note. The tag "Container" (and "Panel"/"Frame") is still accepted as
    // an alias in strandKindFromTag() below so .rin source written against the original spec's
    // vocabulary parses unchanged; there is no grammar collision with `@container=...` (a
    // different top-level statement, matched before `@view.` ever runs), only the conceptual one
    // the note flagged -- so the alias is safe to accept.
    BOX,
    GRID, WRAP, SPACER,
    BADGE, PROGRESS, CHECKBOX, SWITCH, AVATAR,
    INPUT, TEXTAREA,
    DIALOG,
    TABS, TABITEM,
    TOOLTIP,

    CUSTOM
};
inline StrandKind strandKindFromTag(const std::string& tag) {
    if (tag == "Text" || tag == "text") return StrandKind::TEXT;
    if (tag == "Image" || tag == "image") return StrandKind::IMAGE;
    if (tag == "Button" || tag == "button") return StrandKind::BUTTON;
    if (tag == "Card" || tag == "card") return StrandKind::CARD;
    if (tag == "Column" || tag == "column") return StrandKind::COLUMN;
    if (tag == "Row" || tag == "row") return StrandKind::ROW;
    if (tag == "Stack" || tag == "stack") return StrandKind::STACK;
    if (tag == "Divider" || tag == "divider") return StrandKind::DIVIDER;

    // Extended tags
    if (tag == "Header" || tag == "header") return StrandKind::HEADER;
    if (tag == "TopBar" || tag == "topbar" || tag == "topBar") return StrandKind::TOPBAR;
    if (tag == "BottomBar" || tag == "bottombar" || tag == "bottomBar") return StrandKind::BOTTOMBAR;
    if (tag == "Drawer" || tag == "drawer") return StrandKind::DRAWER;
    if (tag == "Menu" || tag == "menu") return StrandKind::MENU;
    if (tag == "MenuItem" || tag == "menuitem" || tag == "menuItem") return StrandKind::MENUITEM;
    if (tag == "Table" || tag == "table") return StrandKind::TABLE;
    if (tag == "TableRow" || tag == "tablerow" || tag == "tableRow") return StrandKind::TABLEROW;
    if (tag == "Video" || tag == "video") return StrandKind::VIDEO;
    if (tag == "Audio" || tag == "audio") return StrandKind::AUDIO;
    if (tag == "WebView" || tag == "webview" || tag == "webView") return StrandKind::WEBVIEW;
    if (tag == "Scaffold" || tag == "scaffold") return StrandKind::SCAFFOLD;
    if (tag == "Splash" || tag == "splash") return StrandKind::SPLASH;
    if (tag == "Banner" || tag == "banner") return StrandKind::BANNER;

    // ---- Missing-components pass ----
    if (tag == "Box" || tag == "box" || tag == "Container" || tag == "container" ||
        tag == "Panel" || tag == "panel" || tag == "Frame" || tag == "frame") return StrandKind::BOX;
    if (tag == "Grid" || tag == "grid") return StrandKind::GRID;
    if (tag == "Wrap" || tag == "wrap") return StrandKind::WRAP;
    if (tag == "Spacer" || tag == "spacer") return StrandKind::SPACER;
    if (tag == "Badge" || tag == "badge") return StrandKind::BADGE;
    if (tag == "Progress" || tag == "progress") return StrandKind::PROGRESS;
    if (tag == "Checkbox" || tag == "checkbox") return StrandKind::CHECKBOX;
    if (tag == "Switch" || tag == "switch") return StrandKind::SWITCH;
    if (tag == "Avatar" || tag == "avatar") return StrandKind::AVATAR;
    if (tag == "Input" || tag == "input" || tag == "TextField" || tag == "textfield" || tag == "textField")
        return StrandKind::INPUT;
    if (tag == "TextArea" || tag == "textarea" || tag == "textArea") return StrandKind::TEXTAREA;
    if (tag == "Dialog" || tag == "dialog" || tag == "Modal" || tag == "modal") return StrandKind::DIALOG;
    if (tag == "Tabs" || tag == "tabs") return StrandKind::TABS;
    if (tag == "TabItem" || tag == "tabitem" || tag == "tabItem") return StrandKind::TABITEM;
    if (tag == "Tooltip" || tag == "tooltip") return StrandKind::TOOLTIP;

    return StrandKind::CUSTOM; // resolved via Bolt (plugin) registry — see architecture doc §18
}
inline std::string strandKindName(StrandKind k) {
    switch (k) {
        case StrandKind::TEXT: return "Text"; case StrandKind::IMAGE: return "Image";
        case StrandKind::BUTTON: return "Button"; case StrandKind::CARD: return "Card";
        case StrandKind::COLUMN: return "Column"; case StrandKind::ROW: return "Row";
        case StrandKind::STACK: return "Stack"; case StrandKind::DIVIDER: return "Divider";

        // Extended names
        case StrandKind::HEADER: return "Header";
        case StrandKind::TOPBAR: return "TopBar";
        case StrandKind::BOTTOMBAR: return "BottomBar";
        case StrandKind::DRAWER: return "Drawer";
        case StrandKind::MENU: return "Menu";
        case StrandKind::MENUITEM: return "MenuItem";
        case StrandKind::TABLE: return "Table";
        case StrandKind::TABLEROW: return "TableRow";
        case StrandKind::VIDEO: return "Video";
        case StrandKind::AUDIO: return "Audio";
        case StrandKind::WEBVIEW: return "WebView";
        case StrandKind::SCAFFOLD: return "Scaffold";
        case StrandKind::SPLASH: return "Splash";
        case StrandKind::BANNER: return "Banner";

        case StrandKind::BOX: return "Box";
        case StrandKind::GRID: return "Grid";
        case StrandKind::WRAP: return "Wrap";
        case StrandKind::SPACER: return "Spacer";
        case StrandKind::BADGE: return "Badge";
        case StrandKind::PROGRESS: return "Progress";
        case StrandKind::CHECKBOX: return "Checkbox";
        case StrandKind::SWITCH: return "Switch";
        case StrandKind::AVATAR: return "Avatar";
        case StrandKind::INPUT: return "Input";
        case StrandKind::TEXTAREA: return "TextArea";
        case StrandKind::DIALOG: return "Dialog";
        case StrandKind::TABS: return "Tabs";
        case StrandKind::TABITEM: return "TabItem";
        case StrandKind::TOOLTIP: return "Tooltip";

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

// ---- Banner conveniences (§13/§14): title=/message=/closable= --------------------------------
// Banner already goes through the exact same buildFabric() above as every other StrandKind (see
// the comment on StrandKind::BANNER) -- composing a Banner by nesting real @view.Text/@view.Button
// children already works with zero extra code, and stays the right tool for anything custom
// (icons via a nested @view.Image, multiple actions, a nested @view.Menu, ...). What's added here
// is pure shorthand for the common case the spec's own example uses (a title, a message, and a
// dismiss control) so a Banner doesn't *require* manually nesting two Text strands and wiring a
// warp cell by hand just to be dismissible.
//
// Scope note: this only runs on the cold-pipeline build path (see rin_loom_pipeline.h's
// runColdPipeline, which calls it right after buildFabric) -- NOT on Shuttle::diff's hot-reload
// path yet (a source-edit that adds/changes title=/message=/closable= won't re-synthesize until
// the next cold build) -- and NOT on Shuttle::applyWarpChange, which doesn't need it (the
// synthesized "visible" attr's rawExpr is a real VariableExpr registered through the same
// WarpSubscriptions mechanism every other Warp-bound attribute uses, so a later warp.set() on the
// auto-created cell already re-resolves it generically, with no Banner-specific code involved).
inline void applyBannerConveniences(const StrandPtr& s, WarpScope& warp, WarpSubscriptions& subs) {
    if (s->kind == StrandKind::BANNER) {
        std::vector<StrandPtr> synthesizedChildren;

        std::string title = s->attrStr("title", "");
        std::string message = s->attrStr("message", "");
        if (!title.empty()) {
            auto t = std::make_shared<Strand>();
            t->kind = StrandKind::TEXT; t->name = s->name + "_title"; t->sourceLine = s->sourceLine;
            t->id = deriveId(s->name, "title", 0, StrandKind::TEXT);
            t->attrs.push_back({"text", nullptr, Value::txt(title)});
            t->attrs.push_back({"size", nullptr, Value::txt("title")});
            t->attrs.push_back({"tone", nullptr, Value::txt("text")});
            t->contentHash = fnv1a("banner_title_" + title);
            synthesizedChildren.push_back(t);
        }
        if (!message.empty()) {
            auto m = std::make_shared<Strand>();
            m->kind = StrandKind::TEXT; m->name = s->name + "_message"; m->sourceLine = s->sourceLine;
            m->id = deriveId(s->name, "message", 0, StrandKind::TEXT);
            m->attrs.push_back({"text", nullptr, Value::txt(message)});
            m->attrs.push_back({"size", nullptr, Value::txt("body")});
            m->attrs.push_back({"tone", nullptr, Value::txt("text_muted")});
            m->contentHash = fnv1a("banner_message_" + message);
            synthesizedChildren.push_back(m);
        }
        // Existing (manually composed) children are kept and rendered after the synthesized
        // title/message, e.g. a nested @view.Row of action Buttons.
        for (auto& c : s->children) synthesizedChildren.push_back(c);

        bool closable = s->attrStr("closable", "false") == "true";
        bool userSetVisible = s->attr("visible") != nullptr;
        if (closable) {
            std::string cell = (s->name.empty() ? "banner" : s->name) + "_open";
            if (!warp.has(cell)) warp.set(cell, Value::txt("true"));

            // Close button: onTap=toggle(<cell>) built the same way buildFabric() would have
            // built it from real parsed source -- rawExpr is a genuine rin::CallExpr/VariableExpr
            // pair, evaluated through the same evalAttrExpr() every ordinary attribute uses, so
            // Needle's dispatchTap() (which pattern-matches on rawExpr, not on any Banner-specific
            // code) dispatches it exactly like a hand-written onTap=toggle(myCell); would.
            auto call = std::make_shared<rin::CallExpr>();
            call->callee = "toggle";
            auto arg = std::make_shared<rin::VariableExpr>(); arg->name = cell;
            call->args.push_back(arg);

            auto close = std::make_shared<Strand>();
            close->kind = StrandKind::BUTTON; close->name = s->name + "_close"; close->sourceLine = s->sourceLine;
            close->id = deriveId(s->name, "close", 0, StrandKind::BUTTON);
            close->attrs.push_back({"label", nullptr, Value::txt("\xC3\x97")}); // "×"
            close->attrs.push_back({"variant", nullptr, Value::txt("ghost")});
            close->attrs.push_back({"tone", nullptr, Value::txt("neutral")});
            close->attrs.push_back({"size", nullptr, Value::txt("small")});
            close->attrs.push_back({"a11y_label", nullptr, Value::txt("Dismiss")});
            std::vector<std::string> reads;
            Value onTapVal = evalAttrExpr(call, warp, &reads);
            for (auto& w : reads) subs.record(w, close->id);
            close->attrs.push_back({"onTap", call, onTapVal});
            close->contentHash = fnv1a("banner_close_" + cell);
            synthesizedChildren.push_back(close);

            // visible= reads the auto-created cell UNLESS the .rin source already set its own
            // visible=... explicitly -- an explicit value always wins, the same precedence rule
            // width=/sizing="fill" already uses elsewhere in this engine.
            if (!userSetVisible) {
                auto visVar = std::make_shared<rin::VariableExpr>(); visVar->name = cell;
                std::vector<std::string> visReads;
                Value visVal = evalAttrExpr(visVar, warp, &visReads);
                for (auto& w : visReads) subs.record(w, s->id);
                s->attrs.push_back({"visible", visVar, visVal});
            }
        }
        s->children = synthesizedChildren;

        uint64_t childHashAcc = 0;
        for (auto& c : s->children) childHashAcc = fnv1a(std::to_string(c->contentHash), childHashAcc);
        uint64_t attrHash = 1469598103934665603ULL;
        for (auto& a : s->attrs) attrHash = fnv1a(a.key + "=" + a.value.asString(), attrHash);
        s->contentHash = fnv1a(strandKindName(s->kind), fnv1a(std::to_string(attrHash), childHashAcc));
    }
    for (auto& c : s->children) applyBannerConveniences(c, warp, subs);
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
