// indsin/rin_indsin_components_ext.h — UI/UX Library Expansion: build-time convenience synthesis
// for the composite kinds added alongside StrandKind::{TAG,BREADCRUMB,PAGINATION,STEPS,...} (see
// rin_indsin_strand.h's own doc comment on that enum block, and rin_indsin_layout.h/
// rin_indsin_paint.h for their measure/paint halves).
//
// This follows exactly the pattern applyBannerConveniences() (rin_indsin_strand.h) and
// applyObjectConveniences() (rin_indsin_object.h) already established: a shorthand attribute
// (Tag's text=/removable=, Breadcrumb's items=, Pagination's current=/total=) desugars into real
// child Strands (Text/Button) built the same way buildFabric() itself would have built them from
// hand-written .rin source -- not a parallel rendering path. A .rin author who'd rather nest real
// children manually always can (each pass below only synthesizes when the parent has no children
// of its own yet), same "explicit still wins" rule Banner's title=/message= already follows.
//
// Steps' state=/index= injection is pure structural bookkeeping (no Warp cell involved, same
// shape as applyObjectConveniences), so it takes only a `fabric` root. Tag/Pagination need a
// bound Warp cell for their interactive part (a Tag's remove, a Pagination page click), so they
// take (fabric, warp, subs) exactly like applyBannerConveniences does.
#pragma once
#include "rin_indsin_strand.h"
#include "../rin_ast.h"
#include <sstream>
#include <string>
#include <vector>

namespace indsin {

// splitCsv() itself already lives in rin_indsin_layout.h (used by Grid's columns=/Wrap etc.) --
// reused here rather than redefined. It keeps empty items for its own callers' reasons, so this
// wrapper drops the empties Breadcrumb's items="a,,b" shouldn't render as a blank crumb.
inline std::vector<std::string> splitCsvNonEmpty(const std::string& in) {
    std::vector<std::string> out;
    for (auto& part : splitCsv(in)) if (!part.empty()) out.push_back(part);
    return out;
}
inline void recomputeOneHash(const StrandPtr& s) {
    uint64_t childHashAcc = 0;
    for (auto& c : s->children) childHashAcc = fnv1a(std::to_string(c->contentHash), childHashAcc);
    uint64_t attrHash = 1469598103934665603ULL;
    for (auto& a : s->attrs) attrHash = fnv1a(a.key + "=" + a.value.asString(), attrHash);
    s->contentHash = fnv1a(strandKindName(s->kind), fnv1a(std::to_string(attrHash), childHashAcc));
}
// Builds a real `set(cellName, literal)` onTap the same way Banner's synthesized close button
// builds its `toggle(cell)` call (see rin_indsin_strand.h) -- a genuine rin::CallExpr/VariableExpr
// pair evaluated through the ordinary evalAttrExpr() path, not a fake/interpreted-later string, so
// Needle's dispatchTap() (rin_indsin_needle.h) runs it exactly like hand-written `onTap=set(...)`.
inline void attachSetOnTap(const StrandPtr& target, const std::string& cellName,
                            rin::LiteralExpr::Kind litKind, double num, const std::string& str,
                            WarpScope& warp, WarpSubscriptions& subs) {
    auto call = std::make_shared<rin::CallExpr>();
    call->callee = "set";
    auto argCell = std::make_shared<rin::VariableExpr>(); argCell->name = cellName;
    auto argVal = std::make_shared<rin::LiteralExpr>();
    argVal->kind = litKind; argVal->number = num; argVal->str = str;
    call->args.push_back(argCell);
    call->args.push_back(argVal);
    std::vector<std::string> reads;
    Value onTapVal = evalAttrExpr(call, warp, &reads);
    for (auto& w : reads) subs.record(w, target->id);
    target->attrs.push_back({"onTap", call, onTapVal});
}

// ---- Tag/Chip: text= -> a synthesized Text label; removable="true" -> a synthesized close
// Button wired to an auto-created "<name>_visible" Warp cell (same convention Banner's
// "<name>_open" uses), with the Tag's own visible= bound to it unless the source already set one
// explicitly. padding=/gap=/radius= get sensible pill defaults when the source didn't set them
// (an explicit one on the .rin source always wins, checked with s->attr(...) first).
inline void applyTagConveniences(const StrandPtr& s, WarpScope& warp, WarpSubscriptions& subs) {
    if (s->kind == StrandKind::TAG) {
        if (!s->attr("padding")) s->attrs.push_back({"padding", nullptr, Value::txt("6")});
        if (!s->attr("gap"))     s->attrs.push_back({"gap", nullptr, Value::txt("6")});
        if (!s->attr("radius"))  s->attrs.push_back({"radius", nullptr, Value::txt("999")});

        std::vector<StrandPtr> synth;
        std::string text = s->attrStr("text", "");
        if (!text.empty()) {
            auto label = std::make_shared<Strand>();
            label->kind = StrandKind::TEXT; label->name = s->name + "_label"; label->sourceLine = s->sourceLine;
            label->id = deriveId(s->name, "label", 0, StrandKind::TEXT);
            label->attrs.push_back({"text", nullptr, Value::txt(text)});
            label->attrs.push_back({"tone", nullptr, Value::txt("background")}); // reads on the pill's filled tone, like Badge's own label
            label->attrs.push_back({"size", nullptr, Value::txt("12")}); // Badge-sized text, not Text's own (larger) default, so the pill stays compact
            label->contentHash = fnv1a("tag_label_" + text);
            synth.push_back(label);
        }
        for (auto& c : s->children) synth.push_back(c); // any manually-nested children still come through, after the label

        bool removable = s->attrStr("removable", "false") == "true";
        bool userSetVisible = s->attr("visible") != nullptr;
        if (removable) {
            std::string cell = (s->name.empty() ? "tag" : s->name) + "_visible";
            if (!warp.has(cell)) warp.set(cell, Value::txt("true"));

            auto close = std::make_shared<Strand>();
            close->kind = StrandKind::BUTTON; close->name = s->name + "_close"; close->sourceLine = s->sourceLine;
            close->id = deriveId(s->name, "close", 0, StrandKind::BUTTON);
            close->attrs.push_back({"label", nullptr, Value::txt("\xC3\x97")}); // "×"
            close->attrs.push_back({"variant", nullptr, Value::txt("ghost")});
            close->attrs.push_back({"tone", nullptr, Value::txt("neutral")});
            close->attrs.push_back({"size", nullptr, Value::txt("xs")});
            close->attrs.push_back({"a11y_label", nullptr, Value::txt("Remove")});
            attachSetOnTap(close, cell, rin::LiteralExpr::Kind::STRING, 0, "false", warp, subs);
            close->contentHash = fnv1a("tag_close_" + cell);
            synth.push_back(close);

            if (!userSetVisible) {
                auto visVar = std::make_shared<rin::VariableExpr>(); visVar->name = cell;
                std::vector<std::string> visReads;
                Value visVal = evalAttrExpr(visVar, warp, &visReads);
                for (auto& w : visReads) subs.record(w, s->id);
                s->attrs.push_back({"visible", visVar, visVal});
            }
        }
        s->children = synth;
        recomputeOneHash(s);
    }
    for (auto& c : s->children) applyTagConveniences(c, warp, subs);
}

// ---- Breadcrumb: items="Home,Projects,Settings" -> Text items separated by a "›" glyph, the
// last item toned as the current page (text) and the rest muted (text_muted), matching the usual
// breadcrumb convention. Pure structural synthesis (no interaction, no Warp cell needed) -- a
// .rin author who wants a clickable crumb nests real @view.Link children manually instead, which
// this pass leaves alone (only fires when the Breadcrumb has no children of its own yet).
inline void applyBreadcrumbConveniences(const StrandPtr& s) {
    if (s->kind == StrandKind::BREADCRUMB && s->children.empty()) {
        auto items = splitCsvNonEmpty(s->attrStr("items", ""));
        std::vector<StrandPtr> synth;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                auto sep = std::make_shared<Strand>();
                sep->kind = StrandKind::TEXT; sep->name = s->name + "_sep" + std::to_string(i); sep->sourceLine = s->sourceLine;
                sep->id = deriveId(s->name, "sep", (int)i, StrandKind::TEXT);
                sep->attrs.push_back({"text", nullptr, Value::txt("\xE2\x80\xBA")}); // "›"
                sep->attrs.push_back({"tone", nullptr, Value::txt("text_muted")});
                sep->contentHash = fnv1a("breadcrumb_sep_" + std::to_string(i));
                synth.push_back(sep);
            }
            auto item = std::make_shared<Strand>();
            item->kind = StrandKind::TEXT; item->name = s->name + "_item" + std::to_string(i); item->sourceLine = s->sourceLine;
            item->id = deriveId(s->name, "item", (int)i, StrandKind::TEXT);
            item->attrs.push_back({"text", nullptr, Value::txt(items[i])});
            bool isLast = (i + 1 == items.size());
            item->attrs.push_back({"tone", nullptr, Value::txt(isLast ? "text" : "text_muted")});
            item->contentHash = fnv1a("breadcrumb_item_" + items[i]);
            synth.push_back(item);
        }
        if (!synth.empty()) { s->children = synth; recomputeOneHash(s); }
    }
    for (auto& c : s->children) applyBreadcrumbConveniences(c);
}

// ---- Pagination: current=/total= -> a "‹" prev Button, one numbered Button per page, and a "›"
// next Button, each wired with onTap=set(<cell>, N) against an auto-created "<name>_page" Warp
// cell (or bind="<existing cell>" to reuse one the .rin source already declared, e.g. to drive a
// Table/List page from the same value elsewhere). The currently-selected page's Button renders
// with variant="primary"; prev/next disable themselves at the first/last page.
inline void applyPaginationConveniences(const StrandPtr& s, WarpScope& warp, WarpSubscriptions& subs) {
    if (s->kind == StrandKind::PAGINATION && s->children.empty() && s->attr("total")) {
        int total = (int)std::max(1.0, s->attrNum("total", 1));
        std::string cell = s->attrStr("bind", "");
        if (cell.empty()) cell = (s->name.empty() ? "pagination" : s->name) + "_page";
        if (!warp.has(cell)) warp.set(cell, Value::num(std::max(1.0, s->attrNum("current", 1))));
        int current = std::max(1, std::min(total, (int)warp.get(cell).asNumber(1)));

        auto makeButton = [&](const std::string& label, int targetPage, bool disabled, bool selected, int idx) {
            auto btn = std::make_shared<Strand>();
            btn->kind = StrandKind::BUTTON; btn->name = s->name + "_p" + std::to_string(idx); btn->sourceLine = s->sourceLine;
            btn->id = deriveId(s->name, "page", idx, StrandKind::BUTTON);
            btn->attrs.push_back({"label", nullptr, Value::txt(label)});
            btn->attrs.push_back({"variant", nullptr, Value::txt(selected ? "primary" : "outline")});
            btn->attrs.push_back({"size", nullptr, Value::txt("xs")});
            if (disabled) btn->attrs.push_back({"disabled", nullptr, Value::txt("true")});
            else attachSetOnTap(btn, cell, rin::LiteralExpr::Kind::NUMBER, (double)targetPage, "", warp, subs);
            btn->contentHash = fnv1a("pagination_" + label + "_" + std::to_string(targetPage));
            return btn;
        };

        std::vector<StrandPtr> synth;
        synth.push_back(makeButton("\xE2\x80\xB9", std::max(1, current - 1), current <= 1, false, 0)); // "‹"
        for (int p = 1; p <= total; ++p) synth.push_back(makeButton(std::to_string(p), p, false, p == current, p));
        synth.push_back(makeButton("\xE2\x80\xBA", std::min(total, current + 1), current >= total, false, total + 1)); // "›"
        s->children = synth;
        recomputeOneHash(s);
    }
    for (auto& c : s->children) applyPaginationConveniences(c, warp, subs);
}

// ---- Steps/Stepper: derives each StepItem child's state= ("done"/"active"/"upcoming") and
// 1-based index= from its position against the parent Steps' current= attribute (default 0 =
// nothing completed yet). Pure structural bookkeeping, same shape as applyObjectConveniences --
// no Warp cell involved, so this only needs the fabric root (called from the same conveniences
// site as the others, just without a warp/subs argument).
inline void applyStepsConveniences(const StrandPtr& s) {
    if (s->kind == StrandKind::STEPS) {
        int current = (int)s->attrNum("current", 0); // 0-based: how many steps are already done
        for (size_t i = 0; i < s->children.size(); ++i) {
            auto& item = s->children[i];
            if (item->kind != StrandKind::STEPITEM) continue;
            std::string state = ((int)i < current) ? "done" : ((int)i == current) ? "active" : "upcoming";
            item->attrs.push_back({"state", nullptr, Value::txt(state)});
            item->attrs.push_back({"index", nullptr, Value::txt(std::to_string(i + 1))});
        }
    }
    for (auto& c : s->children) applyStepsConveniences(c);
}

// Single entry point the pipeline calls right alongside applyBannerConveniences()/
// applyObjectConveniences() (see rin_indsin_pipeline.h) -- keeps every call site a one-line add
// instead of four.
inline void applyExtendedUiConveniences(const StrandPtr& fabric, WarpScope& warp, WarpSubscriptions& subs) {
    applyTagConveniences(fabric, warp, subs);
    applyBreadcrumbConveniences(fabric);
    applyPaginationConveniences(fabric, warp, subs);
    applyStepsConveniences(fabric);
}

} // namespace indsin
