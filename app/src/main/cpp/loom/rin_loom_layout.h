// loom/rin_loom_layout.h — The Loom: two-pass Propose/Settle layout engine.
// (Same verified algorithm as the standalone prototype, operating on the real Strand type.)
//
// ---------------------------------------------------------------------------------------------
// EXTENSION NOTE (added to satisfy: top bar / bottom bar / side drawer / dropdown menus / video
// & audio / web-embeds (e.g. YouTube) / printable tables / header / align+valign / splash-screen
// / page navigation main.rin -> mu.rin on tap or timer):
//
// This header only owns *geometry* (measuring/placing rectangles). It cannot by itself add new
// widgets end-to-end — that also requires (not included in this patch, since those files weren't
// provided):
//   1. rin_loom_strand.h  — StrandKind enum must gain: HEADER, TOPBAR, BOTTOMBAR, DRAWER, MENU,
//      MENUITEM, TABLE, TABLEROW, VIDEO, AUDIO, WEBVIEW, SCAFFOLD, SPLASH.
//   2. The .rin parser     — must recognize the new tag names and the new attributes used below
//      (align, valign, role, open, src, columns, duration, navigate, onTap="navigate:...").
//   3. loom::fabricToJsonString (native) + RinEngine/JNI bridge — must serialize the new kinds'
//      attrs the same way existing ones are (no change needed if it's already generic/attr-map
//      based; only add the new enum-to-string names in one place).
//   4. The host app (Activity/Fragment that owns LoomPreviewManager) — must actually perform the
//      navigation (loading a new .rin file, e.g. "mu.rin") when it sees an onTap/timer navigate
//      instruction, and must drive a real <video>/<audio>/<WebView> player + YouTube embed and a
//      real print/export routine for <Table>. The Loom only computes where those boxes sit and
//      LoomFabricView only draws a *placeholder* box for them in the design-time preview — exactly
//      how Image already works today (see drawImagePlaceholder in LoomFabricView.kt).
//
// New attributes this file adds support for:
//   align   = "left" | "center" | "right" | "stretch"   (Column's cross-axis, Row/Table/Stack too)
//   valign  = "top"  | "center" | "bottom" | "stretch"   (Row's cross-axis, Stack too)
//   role    = "topbar" | "bottombar" | "drawer" | "content"  (used only by SCAFFOLD's children)
//   open    = "true"/"false"   (Drawer/Menu visibility — collapses to 0 size when closed)
//   columns = "Name,Age,City" (Table header labels, comma separated)
//   ratio   = "16:9" etc.      (Video/WebView aspect ratio when no explicit height=)
// ---------------------------------------------------------------------------------------------
#pragma once
#include "rin_loom_strand.h"
#include "rin_loom_tokens.h"
#include <algorithm>
#include <sstream>

namespace loom {

enum class Axis { X, Y };

// left/top => 0, center => centered, right/bottom => pushed to far edge, stretch => fills (0 offset,
// caller is expected to have already sized the child to `available` in that case).
inline double alignOffset(double available, double size, const std::string& mode) {
    if (mode == "center") return std::max(0.0, (available - size) / 2.0);
    if (mode == "right" || mode == "bottom" || mode == "end") return std::max(0.0, available - size);
    return 0.0; // left / top / start / stretch
}

// Overlay Engine hook (see rin_loom_overlay.h for the full second pass): whether `s` is going to
// be re-homed onto a separate overlay layer instead of staying in normal document flow. Consulted
// by layoutLinear() and layoutStack() below, which are the actual places flow-exclusion happens
// (skipping a Dialog/Tooltip's contribution to their main-axis budget / cross-axis footprint,
// while still measuring its own content box against the full available space) — NOT inside
// layout()'s own generic tail. An earlier version of this hook tried exactly that (making
// layout() itself report {0,0,0,0} for an overlay Strand while still recording its real box) and
// it silently corrupted layoutLinear's two-pass flex budget: that pass calls layout() on the same
// child twice (an unconstrained probe, then a real placement call built from the probe's
// *returned* main-axis size), so a returned 0 fed back in as a maxH=0 constraint on the second,
// "real" call -- the Dialog's own box came out zeroed instead of merely excluded from its
// parent's size. Keeping layout() itself honest (always returns the Strand's real measured size)
// and doing the skip explicitly, once, in each container helper's own bookkeeping avoids that.
inline bool isOverlayStrand(const Strand& s) {
    if (s.kind == StrandKind::DIALOG)  return s.attrStr("open", "false") == "true";
    if (s.kind == StrandKind::TOOLTIP) return !s.attrStr("anchor", "").empty() && s.attrStr("open", "true") == "true";
    return false;
}

inline std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t a = item.find_first_not_of(' ');
        size_t b = item.find_last_not_of(' ');
        out.push_back(a == std::string::npos ? "" : item.substr(a, b - a + 1));
    }
    return out;
}

// Counts Unicode codepoints in a UTF-8 string (not bytes) — text.size() was counting bytes,
// which silently under-measured any multi-byte text (e.g. Arabic labels/attrs).
inline size_t utf8Length(const std::string& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
        i += len;
        count++;
    }
    return count;
}

// Rough proportional-font width estimate used only to size a Strand *before* paint. The real
// text is later measured/ellipsized against this box with Android's actual font metrics
// (LoomFabricView.drawText), so if this estimate runs narrow, correctly-sized text gets
// truncated even though there was room for it on screen. 0.58 was too tight for real UI fonts
// (bold labels, uppercase-heavy titles, digits); 0.72 plus a small fixed pad biases the box
// slightly *wide* instead — a little extra breathing room beats a clipped "…".
inline double measureTextWidth(const std::string& text, double fontSize) {
    return utf8Length(text) * fontSize * 0.72 + 2.0;
}

// Word-wraps `text` into lines that each fit within `maxWidth` (estimated via measureTextWidth).
// UTF-8 safe: words are split on the ASCII space byte (0x20), which is always a standalone byte
// in UTF-8 (never a continuation byte), so this is safe for Arabic and any other multi-byte text.
// A single word wider than maxWidth is kept on its own line rather than dropped/clipped — the
// paint layer is responsible for the final real-font ellipsis if it still overflows its box.
inline std::vector<std::string> wrapText(const std::string& text, double fontSize, double maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) { lines.push_back(""); return lines; }
    if (maxWidth <= 0) { lines.push_back(text); return lines; }

    std::vector<std::string> words;
    std::string cur;
    for (size_t i = 0; i < text.size(); ) {
        if (text[i] == ' ') {
            if (!cur.empty()) { words.push_back(cur); cur.clear(); }
            i++;
        } else {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
            cur += text.substr(i, len);
            i += len;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    if (words.empty()) { lines.push_back(""); return lines; }

    std::string lineBuf;
    for (auto& w : words) {
        std::string candidate = lineBuf.empty() ? w : (lineBuf + " " + w);
        if (lineBuf.empty() || measureTextWidth(candidate, fontSize) <= maxWidth) {
            lineBuf = candidate;
        } else {
            lines.push_back(lineBuf);
            lineBuf = w;
        }
    }
    if (!lineBuf.empty() || lines.empty()) lines.push_back(lineBuf);
    return lines;
}

// Avatar size (§?): a small named scale (small/medium/large), same dual token-or-number
// convention every other Loom scale (spacing/radius/typography/button-size) already uses.
inline double resolveAvatarSize(const Strand& s) {
    const Value* v = s.attr("size");
    if (!v) return 40;
    if (v->kind == Value::Kind::NUMBER) return v->number;
    if (v->str == "small") return 28;
    if (v->str == "medium") return 40;
    if (v->str == "large") return 56;
    if (v->str == "xl") return 72;
    return v->asNumber(40);
}

struct LoomStats { int strandsMeasured = 0; int cacheHits = 0; };

struct Loom {
    LoomStats stats;

    Rect layout(StrandPtr s, Constraints c, double originX, double originY) {
        bool sameConstraints = s->hasLastConstraints &&
            s->lastConstraints.maxW == c.maxW && s->lastConstraints.maxH == c.maxH;
        bool sameContent = s->hasLastContentHash && s->lastContentHash == s->contentHash;
        if (sameConstraints && sameContent && s->geometry.w > 0) {
            stats.cacheHits++;
            double dx = originX - s->geometry.x, dy = originY - s->geometry.y;
            if (dx != 0.0 || dy != 0.0) translate(s, dx, dy);
            return s->geometry;
        }
        stats.strandsMeasured++;
        s->lastConstraints = c; s->hasLastConstraints = true;
        s->lastContentHash = s->contentHash; s->hasLastContentHash = true;

        // Explicit width/height (real sizing, not just an estimate hint): if the .rin source sets
        // width=/height= on ANY strand kind, that becomes a hard min==max constraint for it — the
        // same "box model" every real UI toolkit uses — clamped so it never exceeds what the
        // parent actually offered (a Card can't demand more room than its Column gave it).
        Constraints c2 = c;
        if (s->attr("width"))  { double w = std::max(0.0, std::min(s->attrNum("width", c.maxW), c.maxW));  c2.minW = w; c2.maxW = w; }
        if (s->attr("height")) { double h = std::max(0.0, std::min(s->attrNum("height", c.maxH), c.maxH)); c2.minH = h; c2.maxH = h; }

        // Constraint System (§6): min/max-*, independent of width=/height= above, so e.g. a
        // content-sized Card can still be given a floor or a ceiling without pinning it exactly.
        if (s->attr("min_width"))  c2.minW = std::max(c2.minW, s->attrNum("min_width", c2.minW));
        if (s->attr("max_width"))  c2.maxW = std::min(c2.maxW, s->attrNum("max_width", c2.maxW));
        if (s->attr("min_height")) c2.minH = std::max(c2.minH, s->attrNum("min_height", c2.minH));
        if (s->attr("max_height")) c2.maxH = std::min(c2.maxH, s->attrNum("max_height", c2.maxH));

        // Sizing modes (§5): fill/expand claim the parent's full *bounded* extent; fixed defers
        // to width=/height= above (a no-op here if neither was actually given -- see the scope
        // note on SizingMode in rin_loom_tokens.h); fit/hug/auto all mean "measure my own
        // content", which is what every per-kind measure function below already does.
        SizingMode sizing = resolveSizingMode(*s);
        // Spacer (§15) defaults to "expand" without requiring sizing="expand" on every use — an
        // explicit sizing= (or width=/height=, handled above via c2 already) still overrides it.
        if (s->kind == StrandKind::SPACER && !s->attr("sizing")) sizing = SizingMode::EXPAND;
        if ((sizing == SizingMode::FILL || sizing == SizingMode::EXPAND) && !s->attr("width") && c.maxW < 1e8)
            { c2.minW = c2.maxW = c.maxW; }
        if ((sizing == SizingMode::FILL || sizing == SizingMode::EXPAND) && !s->attr("height") && c.maxH < 1e8)
            { c2.minH = c2.maxH = c.maxH; }

        Rect size;
        switch (s->kind) {
            case StrandKind::TEXT:    size = measureText(s, c2); break;
            case StrandKind::IMAGE:   size = measureImage(s, c2); break;
            case StrandKind::BUTTON:  size = measureButton(s, c2); break;
            case StrandKind::ICONBUTTON: size = measureButton(s, c2); break; // same box rules as Button (§18/§2)
            case StrandKind::ICON: {
                double sz = std::min(s->attrNum("size", 24), std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            case StrandKind::DIVIDER: size = {0,0, c2.maxW, std::max(1.0, c2.minH)}; break;
            case StrandKind::CARD:    size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 12) + s->attrNum("border", 0), originX, originY); break;
            case StrandKind::COLUMN:  size = layoutLinear(s, c2, Axis::Y, originX, originY); break;
            case StrandKind::ROW:     size = layoutLinear(s, c2, Axis::X, originX, originY); break;
            case StrandKind::STACK:   size = layoutStack(s, c2, originX, originY); break;

            // ---- new: app-chrome bars — a Row of items, pinned by the Scaffold that hosts them,
            // default height 56 (a standard mobile bar height) unless height= overrides it.
            case StrandKind::HEADER:
            case StrandKind::TOPBAR:
            case StrandKind::BOTTOMBAR: {
                Constraints barC = c2;
                if (!s->attr("height")) { barC.minH = barC.maxH = std::min(56.0, c2.maxH); }
                size = layoutLinear(s, barC, Axis::X, originX, originY);
                break;
            }

            // ---- new: side drawer / dropdown menu — a Column of items that collapses to zero
            // size when open=false, so it never consumes layout space while hidden. Width
            // defaults to 280 (drawer) unless width= is set; menus size to their content.
            case StrandKind::DRAWER: {
                bool isOpen = s->attrStr("open", "true") != "false";
                if (!isOpen) { size = {0,0,0,0}; break; }
                Constraints dc = c2;
                if (!s->attr("width")) { dc.minW = dc.maxW = std::min(280.0, c2.maxW); }
                dc.minH = dc.maxH = c2.maxH; // full height overlay
                size = layoutLinear(s, dc, Axis::Y, originX, originY);
                break;
            }
            case StrandKind::MENU: {
                bool isOpen = s->attrStr("open", "false") == "true";
                if (!isOpen) { size = {0,0,0,0}; break; }
                size = layoutLinear(s, c2, Axis::Y, originX, originY);
                break;
            }
            case StrandKind::MENUITEM: size = measureButton(s, c2); break; // same box rules as Button

            // ---- new: Banner — a full-width notice box (Card-like padded box, not pinned like
            // TopBar/BottomBar). Height is content-driven unless height= is set; visible=false
            // collapses it to zero size so it takes no layout space while hidden, same rule as
            // Drawer/Menu's open=false above. Its children are its content (Text/Button/Menu
            // strands built from banner.text/banner.action/banner.menu) — Banner does not draw
            // them itself, it just boxes and pads them like Card does.
            case StrandKind::BANNER: {
                bool isVisible = s->attrStr("visible", "true") != "false";
                if (!isVisible) { size = {0,0,0,0}; break; }
                size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 16) + s->attrNum("border", 0), originX, originY);
                break;
            }

            // ---- new: table — a header row (columns=) plus TABLEROW children of cells.
            case StrandKind::TABLE: size = layoutTable(s, c2, originX, originY); break;
            case StrandKind::TABLEROW: size = layoutLinear(s, c2, Axis::X, originX, originY); break;

            // ---- Missing-components pass ----
            // Box (§5/§6, the resolved "Container" naming collision — see the enum comment in
            // rin_loom_strand.h): the same padded, multi-child, sizing-aware box Card already is,
            // just without Card's elevation-styled default paint (see colorForKind in paint.h).
            case StrandKind::BOX: size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 0) + s->attrNum("border", 0), originX, originY); break;

            case StrandKind::GRID: size = layoutGrid(s, c2, originX, originY); break;
            case StrandKind::WRAP: size = layoutWrap(s, c2, originX, originY); break;

            // Spacer (§15): sizing defaults to "expand" (fills the parent's remaining main-axis
            // extent) unless the .rin source overrides sizing= or gives an explicit width=/height=
            // for a fixed-size gap instead — see the sizing-mode override just above this switch.
            case StrandKind::SPACER: size = {0,0, c2.minW, c2.minH}; break;

            case StrandKind::BADGE: size = measureBadge(s, c2); break;
            case StrandKind::PROGRESS: {
                double h = s->attr("height") ? c2.minH : std::min(8.0, c2.maxH);
                size = {0,0, c2.maxW, std::max(h, c2.minH)};
                break;
            }
            case StrandKind::CHECKBOX: {
                double sz = std::min(s->attrNum("size", 22), std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            case StrandKind::SWITCH: {
                double w = std::min(s->attrNum("width", 44), c2.maxW);
                double h = std::min(s->attrNum("height", 24), c2.maxH);
                size = {0,0, std::max(w,c2.minW), std::max(h,c2.minH)};
                break;
            }
            case StrandKind::AVATAR: {
                double sz = std::min(resolveAvatarSize(*s), std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            case StrandKind::INPUT: size = measureField(s, c2, 40); break;
            case StrandKind::TEXTAREA: size = measureField(s, c2, 96); break;

            // Dialog/Modal (§?): the same collapse-when-closed rule Drawer/Menu/Banner already
            // use (open=false / visible=false -> zero layout space), and the same padded
            // single-child box Card/Box use otherwise. Scope note (kept honest rather than
            // half-faking a real overlay compositor this engine doesn't have): a Dialog lays out
            // in normal document flow, at whatever size its content needs — true screen-centered
            // overlay positioning is achieved the same way Stack's own doc comment already
            // recommends for badges/FABs: wrap it in `@view.Stack align="center" valign="center"`.
            case StrandKind::DIALOG: {
                bool isOpen = s->attrStr("open", "false") == "true";
                if (!isOpen) { size = {0,0,0,0}; break; }
                size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 24) + s->attrNum("border", 0), originX, originY);
                break;
            }

            case StrandKind::TABS: size = layoutLinear(s, c2, Axis::X, originX, originY); break;
            case StrandKind::TABITEM: size = measureButton(s, c2); break; // same box rules as Button/MenuItem

            case StrandKind::TOOLTIP: size = measureTooltip(s, c2); break;

            // ---- new: media placeholders — sized like an Image (explicit width/height, else a
            // sensible default box). Actual playback happens in the real app, not this preview.
            case StrandKind::VIDEO:
            case StrandKind::WEBVIEW: size = measureMedia(s, c2, 16.0/9.0); break;
            case StrandKind::AUDIO:   size = measureMedia(s, c2, -1); break; // fixed-height bar, full width

            // ---- new: scaffold — composes Header/TopBar (fixed top), BottomBar (fixed bottom),
            // Drawer (overlay), and the remaining Content area, via each child's role= attr.
            case StrandKind::SCAFFOLD: size = layoutScaffold(s, c2, originX, originY); break;

            // ---- new: splash/loading screen — just a centered Stack; the *duration* the app
            // shows it for, and the auto-navigate afterwards, are attrs (duration=, navigate=)
            // read and acted on by the host runtime, not by layout.
            case StrandKind::SPLASH: size = layoutStack(s, c2, originX, originY); break;

            default:                  size = layoutLinear(s, c2, Axis::Y, originX, originY); break; // Bolt fallback
        }
        // fill/expand sizing (§5) is a floor, not just a ceiling: several measure functions above
        // (layoutSingleChildBox, layoutLinear) return content-sized results even when c2's min
        // was widened to c.maxW, since they compute min(content, max) without consulting min. This
        // is the one place that applies to every StrandKind uniformly, so individual measure
        // functions don't each need their own copy of the same clamp.
        size.w = std::max(size.w, c2.minW);
        size.h = std::max(size.h, c2.minH);
        size.x = originX; size.y = originY;
        s->geometry = size;
        return size;
    }

    void translate(StrandPtr s, double dx, double dy) {
        s->geometry.x += dx; s->geometry.y += dy;
        for (auto& c : s->children) translate(c, dx, dy);
    }

    // Real multi-line measurement: text now wraps against the actual available width instead of
    // always being sized (and later ellipsized in paint) as if it were one line — that mismatch
    // is what produced boxes too short for their wrapped content and a screen full of "…".
    // `maxLines=` on the Strand caps how many lines are kept; an explicit height= (baked into
    // c.maxH by layout()) caps it the same way, based on how many lines actually fit.
    Rect measureText(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 16);
        double lineHeight = fontSize * 1.4;

        std::vector<std::string> lines = wrapText(text, fontSize, c.maxW);

        double maxLinesAttr = s->attrNum("maxLines", 0);
        if (maxLinesAttr > 0 && lines.size() > (size_t)maxLinesAttr)
            lines.resize((size_t)maxLinesAttr);

        if (c.maxH < 1e9) {
            size_t fitLines = std::max((size_t)1, (size_t)(c.maxH / lineHeight));
            if (lines.size() > fitLines) lines.resize(fitLines);
        }

        double w = 0;
        for (auto& l : lines) w = std::max(w, measureTextWidth(l, fontSize));
        w = std::min(w, c.maxW);
        double h = std::min(lineHeight * (double)std::max((size_t)1, lines.size()), c.maxH);
        return {0,0, std::max(w, c.minW), std::max(h, c.minH)};
    }
    Rect measureImage(StrandPtr s, Constraints c) {
        double w = std::min(s->attrNum("width", 96), c.maxW);
        double h = std::min(s->attrNum("height", 96), c.maxH);
        return {0,0,w,h};
    }
    Rect measureButton(StrandPtr s, Constraints c) {
        std::string label = s->attrStr("label", "");
        // IconButton (§18/§2): the icon glyph takes visual width too, so measurement accounts for
        // it the same way it accounts for label= -- a one-glyph-wide stand-in ("@ ") prefixed
        // when icon= is present, whether or not label= is also set. Uses measureTextWidth(), the
        // same estimator label= already goes through -- no separate icon-measurement code path.
        std::string measureLabel = label;
        if (s->kind == StrandKind::ICONBUTTON && !s->attrStr("icon", "").empty())
            measureLabel = label.empty() ? "@" : ("@ " + label);
        ButtonSizePreset sz = resolveButtonSize(*s);
        double fontSize = resolveFontSize(*s, "labelSize", sz.fontSize); // labelSize= can still override the size-token's font size explicitly
        double w = std::min(measureTextWidth(measureLabel, fontSize) + sz.hPadding * 2, c.maxW);
        double h = std::min(sz.height, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }
    Rect layoutSingleChildBox(StrandPtr s, Constraints c, double pad, double originX, double originY) {
        double innerMaxW = std::max(0.0, c.maxW - pad*2), innerMaxH = std::max(0.0, c.maxH - pad*2);
        double contentW=0, contentH=0;
        for (auto& child : s->children) {
            Rect r = layout(child, {0, innerMaxW, 0, innerMaxH}, originX + pad, originY + pad + contentH);
            contentW = std::max(contentW, r.w); contentH += r.h + 8;
        }
        if (!s->children.empty()) contentH -= 8;
        return {0,0, std::min(contentW + pad*2, c.maxW), std::min(contentH + pad*2, c.maxH)};
    }
    // A child counts as "flexible" on the main axis when its sizing resolves to fill/expand
    // for that axis and it has no explicit width=/height= pinning it (same precedence
    // width=/height= already has over sizing= elsewhere — see layout()'s c2 computation).
    // Spacer (§15) is flexible on the main axis by definition (its own default sizing, applied
    // in layout() before this runs) even without an explicit sizing= attribute.
    bool isMainAxisFlexible(const StrandPtr& child, Axis axis) {
        bool hasExplicitMain = (axis == Axis::Y) ? (child->attr("height") != nullptr) : (child->attr("width") != nullptr);
        if (hasExplicitMain) return false;
        SizingMode sm = resolveSizingMode(*child);
        if (child->kind == StrandKind::SPACER && !child->attr("sizing")) sm = SizingMode::EXPAND;
        return sm == SizingMode::FILL || sm == SizingMode::EXPAND;
    }

    Rect layoutLinear(StrandPtr s, Constraints c, Axis axis, double originX, double originY) {
        double gap = resolveSpacing(*s, "gap", 0), padding = resolveSpacing(*s, "padding", 0) + s->attrNum("border", 0);
        double crossMax=0, cursorMain=padding;
        double innerMaxW = std::max(0.0, c.maxW - padding*2), innerMaxH = std::max(0.0, c.maxH - padding*2);
        double innerMain = (axis == Axis::Y) ? innerMaxH : innerMaxW;
        // Cross-axis alignment: Column (axis=Y) aligns children horizontally via `align`;
        // Row (axis=X) aligns children vertically via `valign`. Default is left/top (offset 0),
        // matching the previous unaligned behavior exactly, so existing .rin files don't move.
        std::string crossMode = (axis == Axis::Y) ? s->attrStr("align", "left") : s->attrStr("valign", "top");
        double innerCross = (axis == Axis::Y) ? innerMaxW : innerMaxH;

        // Two-pass flex distribution (needed for Spacer/fill children to share the main axis
        // correctly with siblings placed *after* them — a single top-to-bottom pass would let an
        // earlier flexible child claim 100% of the remaining space and starve later siblings,
        // e.g. `Row { Text; Spacer; Button; }` would previously give the trailing Button zero
        // width). Pass 1 measures every non-flexible child at its natural size (bounded only by
        // the *unbounded-so-far* main axis, since we don't yet know how much flexible siblings
        // will need) and sums how much main-axis space they + gaps actually consume. Whatever is
        // left over (never negative) is split evenly across the flexible children in pass 2.
        std::vector<bool> flexible(s->children.size());
        std::vector<double> mainSizeOf(s->children.size(), 0.0);
        double fixedMainUsed = 0; int flexCount = 0; size_t flowChildren = 0;
        for (size_t i = 0; i < s->children.size(); i++) {
            // Overlay Engine (rin_loom_overlay.h): an open Dialog / anchored Tooltip is excluded
            // from this container's main-axis budget entirely -- it isn't "flexible" and it isn't
            // "fixed-size-and-counted" either, it simply isn't part of this flow anymore (like a
            // real position:fixed/absolute element). It's still measured, just later, against its
            // own full available box rather than a budget derived from siblings that don't concern
            // it (see the placement loop below) -- an important distinction: measuring it *here*
            // against a probe constraint, the way ordinary fixed-size children are, is exactly
            // what previously corrupted its own box (a stale note in isOverlayStrand's own comment
            // above explains why that approach was abandoned).
            if (isOverlayStrand(*s->children[i])) { flexible[i] = false; mainSizeOf[i] = 0; continue; }
            flowChildren++;
            flexible[i] = isMainAxisFlexible(s->children[i], axis);
            if (flexible[i]) { flexCount++; continue; }
            Constraints probeC = (axis==Axis::Y) ? Constraints{0, innerMaxW, 0, 1e9} : Constraints{0, 1e9, 0, innerMaxH};
            Rect probe = layout(s->children[i], probeC, 0, 0);
            mainSizeOf[i] = (axis==Axis::Y) ? probe.h : probe.w;
            fixedMainUsed += mainSizeOf[i];
        }
        double gapsTotal = flowChildren == 0 ? 0 : gap * (flowChildren - 1);
        double leftover = std::max(0.0, innerMain - fixedMainUsed - gapsTotal);
        double perFlex = flexCount > 0 ? leftover / flexCount : 0.0;

        struct Placed { StrandPtr child; double crossSize; };
        std::vector<Placed> placed;
        double mainUsed = 0;

        for (size_t i = 0; i < s->children.size(); i++) {
            auto& child = s->children[i];
            if (isOverlayStrand(*child)) {
                // Out of flow: measured against the container's own full inner box (not a
                // cursor-derived budget -- there is no "space left for it" question, it doesn't
                // share the axis with its siblings at all), so its real content size is correct
                // and ready for the Overlay Engine's second pass. It advances no cursor, claims no
                // gap, and isn't considered below for cross-axis alignment.
                layout(child, {0, innerMaxW, 0, innerMaxH}, originX + padding, originY + padding);
                continue;
            }
            double mainBudget = flexible[i] ? perFlex : mainSizeOf[i];
            double childMaxW = (axis==Axis::Y) ? innerMaxW : mainBudget;
            double childMaxH = (axis==Axis::Y) ? mainBudget : innerMaxH;
            // An implicit-default Spacer (no explicit sizing= from the .rin source) is only meant
            // to eat slack along the *main* axis, same as every other flex-box-style engine's
            // spacer/expander. Left unguarded, the generic FILL/EXPAND handling in layout() also
            // stretches it across the *cross* axis whenever this container's cross extent happens
            // to be finite -- which it isn't during the Propose (probe) pass (unbounded, ~1e9) but
            // IS during this real Settle pass (bounded to whatever budget the parent already
            // committed to). That Propose/Settle disagreement is exactly what previously let a Row
            // containing a Spacer measure short during Propose (Spacer contributing 0 to cross
            // size) and then grow taller during Settle (Spacer now stretched to the row's full
            // cross budget) -- inflating the row (and any Card/Box stacking it above a sibling)
            // past the box size its own parent already fixed it to, overflowing into whatever sits
            // below. Explicit valign="stretch" / align="stretch" on the container still stretches
            // it on purpose, same as before.
            bool implicitSpacerExpand = (child->kind == StrandKind::SPACER && !child->attr("sizing"));
            if (flexible[i] && implicitSpacerExpand && crossMode != "stretch") {
                if (axis == Axis::Y) childMaxW = 0; else childMaxH = 0;
            }
            Constraints cc = flexible[i]
                ? Constraints{(axis==Axis::Y)?0:mainBudget, childMaxW, (axis==Axis::Y)?mainBudget:0, childMaxH} // flexible: min==max==its share
                : Constraints{0, childMaxW, 0, childMaxH};
            double localX = (axis==Axis::Y) ? padding : cursorMain;
            double localY = (axis==Axis::Y) ? cursorMain : padding;
            Rect r = layout(child, cc, originX + localX, originY + localY);
            double mainSize = (axis==Axis::Y) ? r.h : r.w, crossSize = (axis==Axis::Y) ? r.w : r.h;
            cursorMain += mainSize + gap; mainUsed += mainSize + gap; crossMax = std::max(crossMax, crossSize);
            placed.push_back({child, crossSize});
        }
        if (flowChildren > 0) mainUsed -= gap;

        // Second pass: nudge each child along the cross axis now that innerCross is finalized.
        // (Doesn't touch main-axis position, so gap/order above is untouched — only left/top vs
        // center/right/bottom shifts.) Skipped when mode is "stretch" or default (offset stays 0).
        if (crossMode != "left" && crossMode != "top" && crossMode != "stretch" && !placed.empty()) {
            for (auto& p : placed) {
                double off = alignOffset(innerCross, p.crossSize, crossMode);
                if (off <= 0.0) continue;
                if (axis == Axis::Y) translate(p.child, off, 0); else translate(p.child, 0, off);
            }
        }

        double totalW = (axis==Axis::Y) ? std::min(crossMax+padding*2,c.maxW) : std::min(mainUsed+padding*2,c.maxW);
        double totalH = (axis==Axis::Y) ? std::min(mainUsed+padding*2,c.maxH) : std::min(crossMax+padding*2,c.maxH);
        return {0,0, std::max(totalW,c.minW), std::max(totalH,c.minH)};
    }
    Rect layoutStack(StrandPtr s, Constraints c, double originX, double originY) {
        // align (horizontal) / valign (vertical) position each child within the Stack's full
        // bounds — e.g. align="right" valign="bottom" pins a child to the bottom-right corner,
        // useful for badges/FABs over a Card, or a Splash logo centered with align=center valign=center.
        std::string ha = s->attrStr("align", "left"), va = s->attrStr("valign", "top");
        double w=0,h=0;
        std::vector<Rect> rects; rects.reserve(s->children.size());
        for (auto& child : s->children) {
            Rect r = layout(child, {0, c.maxW, 0, c.maxH}, originX, originY);
            rects.push_back(r);
            // Overlay Engine (rin_loom_overlay.h): an open Dialog / anchored Tooltip is measured
            // (so its content box is ready) but excluded from the Stack's own footprint below --
            // the Stack shouldn't grow (or get treated as if it grew) to fit a modal that's about
            // to be re-homed onto the viewport by the overlay pass, and it's excluded from the
            // alignment loop just below for the same reason: its final position is the overlay
            // pass's job now, not this Stack's align=/valign=.
            if (isOverlayStrand(*child)) continue;
            w = std::max(w, r.w); h = std::max(h, r.h);
        }
        if ((ha != "left" && ha != "stretch") || (va != "top" && va != "stretch")) {
            for (size_t i = 0; i < s->children.size(); i++) {
                if (isOverlayStrand(*s->children[i])) continue;
                double dx = alignOffset(w, rects[i].w, ha);
                double dy = alignOffset(h, rects[i].h, va);
                if (dx != 0.0 || dy != 0.0) translate(s->children[i], dx, dy);
            }
        }
        return {0,0, std::min(w,c.maxW), std::min(h,c.maxH)};
    }

    // ---- new: Table — renders `columns="A,B,C"` as a bold header row, then one TABLEROW per
    // data row (each row's children are the cells for that row, laid out left-to-right). Column
    // widths are the max natural width seen for that column across the header + all rows, summed
    // and clamped to the available width (matching how the rest of the Loom never overflows its
    // parent). This is what backs "printing"/rendering a table — actual paper/PDF export is a
    // host-app concern layered on top of this same geometry.
    Rect layoutTable(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0);
        double innerMaxW = std::max(0.0, c.maxW - padding*2);
        std::vector<std::string> headers = splitCsv(s->attrStr("columns", ""));
        size_t nCols = headers.size();
        for (auto& row : s->children) nCols = std::max(nCols, row->children.size());
        if (nCols == 0) return {0,0, std::max(0.0,c.minW), std::max(0.0,c.minH)};

        std::vector<double> colW(nCols, 0.0);
        double fontSize = resolveFontSize(*s, "size", 14);
        for (size_t i = 0; i < headers.size(); i++) colW[i] = std::max(colW[i], measureTextWidth(headers[i], fontSize) + 16);
        for (auto& row : s->children)
            for (size_t i = 0; i < row->children.size(); i++)
                colW[i] = std::max(colW[i], measureTextWidth(row->children[i]->attrStr("text",""), fontSize) + 16);

        double naturalTotal = 0; for (double w : colW) naturalTotal += w;
        double scale = (naturalTotal > innerMaxW && naturalTotal > 0) ? innerMaxW / naturalTotal : 1.0;
        for (double& w : colW) w *= scale;

        double rowH = fontSize * 1.4 + 12;
        double cursorY = originY + padding;
        double cursorX0 = originX + padding;

        if (!headers.empty()) { cursorY += rowH; } // header row reserved (drawn by the view layer)

        for (auto& row : s->children) {
            double cx = cursorX0;
            for (size_t i = 0; i < row->children.size() && i < nCols; i++) {
                layout(row->children[i], {colW[i], colW[i], 0, rowH}, cx, cursorY);
                cx += colW[i];
            }
            row->geometry = {cursorX0, cursorY, naturalTotal > 0 ? std::min(naturalTotal*scale, innerMaxW) : innerMaxW, rowH};
            cursorY += rowH;
        }

        double totalH = (cursorY - originY - padding) + padding;
        double totalW = std::min(innerMaxW, naturalTotal * scale) + padding*2;
        return {0,0, std::max(totalW, c.minW), std::max(totalH, c.minH)};
    }

    // ---- new: Video/Audio/WebView(YouTube etc.) — sized like Image. `ratio` (e.g. -1 for Audio)
    // means "fixed height bar, full available width" instead of an aspect-ratio box.
    Rect measureMedia(StrandPtr s, Constraints c, double defaultRatio) {
        if (defaultRatio < 0) { // Audio: a slim full-width control bar
            double w = c.maxW;
            double h = std::min(s->attrNum("height", 56), c.maxH);
            return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
        }
        double w = s->attr("width") ? std::min(s->attrNum("width", c.maxW), c.maxW) : c.maxW;
        double ratio = s->attrNum("ratio", defaultRatio);
        double h = s->attr("height") ? std::min(s->attrNum("height", c.maxH), c.maxH) : std::min(w / std::max(0.01, ratio), c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- new: Scaffold — the "page shell": Header/TopBar (role="topbar", fixed to top),
    // BottomBar (role="bottombar", fixed to bottom), Drawer (role="drawer", overlays above
    // content, positioned off-screen at x=-width when its own open=false), and Content
    // (role="content" or unmarked, gets whatever vertical space is left between the bars).
    // This is the piece a whole .rin *page* is expected to be wrapped in once TopBar/BottomBar/
    // Drawer are used, so they stay pinned while Content scrolls (scrolling itself is a host
    // ScrollView concern — Content here is simply given the full remaining height to lay out in).
    Rect layoutScaffold(StrandPtr s, Constraints c, double originX, double originY) {
        double topH = 0, bottomH = 0;
        StrandPtr topbar, bottombar, drawer;
        std::vector<StrandPtr> content;
        for (auto& child : s->children) {
            std::string role = child->attrStr("role", "");
            if (role == "topbar" || role == "header") topbar = child;
            else if (role == "bottombar") bottombar = child;
            else if (role == "drawer") drawer = child;
            else content.push_back(child);
        }
        if (topbar) { Rect r = layout(topbar, {c.maxW, c.maxW, 0, c.maxH}, originX, originY); topH = r.h; }
        if (bottombar) { Rect probe = layout(bottombar, {c.maxW, c.maxW, 0, c.maxH}, originX, originY); bottomH = probe.h; }
        double contentH = std::max(0.0, c.maxH - topH - bottomH);
        double cy = originY + topH;
        // FIX: track the content's own *measured* height instead of assuming it fills the full
        // (often effectively unbounded, c.maxH == 1e9 from the root call) contentH budget. A
        // Scaffold used to always report back {c.maxW, c.maxH} verbatim as its own size, which at
        // the root meant a Fabric height of ~1e9 px — LoomFabricView.setFabric() then tried to
        // measure/allocate a view that tall and nothing drew (fully black preview). The Scaffold's
        // true height is topH + however much vertical space its content actually used + bottomH.
        double contentUsedH = 0;
        for (auto& ch : content) {
            Rect r = layout(ch, {c.maxW, c.maxW, 0, contentH}, originX, cy);
            contentUsedH = std::max(contentUsedH, r.h);
        }
        if (bottombar) layout(bottombar, {c.maxW, c.maxW, bottomH, bottomH}, originX, originY + topH + contentUsedH);
        if (drawer) {
            bool isOpen = drawer->attrStr("open", "false") == "true";
            double dw = drawer->attr("width") ? drawer->attrNum("width", 280) : 280.0;
            double dx = isOpen ? originX : originX - dw; // off-canvas to the left when closed
            layout(drawer, {isOpen ? dw : 0, isOpen ? dw : 0, c.maxH, c.maxH}, dx, originY);
        }
        double totalH = topH + contentUsedH + bottomH;
        return {0,0, c.maxW, std::min(totalH, c.maxH)};
    }

    // ---- Missing-components pass: Grid (§15) — `columns=` (default 2) fixed-column-count grid,
    // row-major placement, `gap=` between both rows and columns (a single spacing token/number
    // for both axes, matching the level of detail Row/Column's own `gap=` already offers — a
    // split row_gap=/column_gap= is easy future work if a real .rin author needs it).
    Rect layoutGrid(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0), gap = resolveSpacing(*s, "gap", 0);
        int columns = std::max(1, (int)s->attrNum("columns", 2));
        double innerMaxW = std::max(0.0, c.maxW - padding*2);
        double cellW = std::max(0.0, (innerMaxW - gap*(columns-1)) / columns);

        double cursorY = padding;
        double usedW = 0;
        for (size_t i = 0; i < s->children.size(); i += columns) {
            double rowH = 0;
            size_t rowEnd = std::min(s->children.size(), i + (size_t)columns);
            for (size_t j = i; j < rowEnd; j++) {
                int col = (int)(j - i);
                double localX = padding + col * (cellW + gap);
                Rect r = layout(s->children[j], {cellW, cellW, 0, 1e9}, originX + localX, originY + cursorY);
                rowH = std::max(rowH, r.h);
                usedW = std::max(usedW, localX + r.w);
            }
            cursorY += rowH + gap;
        }
        if (!s->children.empty()) cursorY -= gap;
        double totalH = cursorY + padding*2;
        double totalW = innerMaxW + padding*2; // Grid always claims its full offered width, like Row/Column do
        return {0,0, std::min(totalW,c.maxW), std::max(std::min(totalH,c.maxH), c.minH)};
    }

    // ---- Missing-components pass: Wrap (§15) — a flow layout: children run left-to-right like
    // Row, but overflow onto a new line instead of being clipped/overlapping once a line's
    // accumulated width would exceed the available width. `gap=` applies to both the horizontal
    // space between items on a line and the vertical space between lines.
    Rect layoutWrap(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0), gap = resolveSpacing(*s, "gap", 0);
        double innerMaxW = std::max(0.0, c.maxW - padding*2);

        double cursorX = 0, cursorY = 0, lineH = 0, usedW = 0;
        bool lineHasItem = false;
        for (auto& child : s->children) {
            // Probe the child's natural width first (unbounded height) so we know whether it
            // fits on the current line before committing to a final position for it.
            Rect probe = layout(child, {0, innerMaxW, 0, 1e9}, 0, 0);
            if (lineHasItem && cursorX + probe.w > innerMaxW) {
                cursorY += lineH + gap; cursorX = 0; lineH = 0; lineHasItem = false;
            }
            Rect r = layout(child, {0, innerMaxW, 0, 1e9}, originX + padding + cursorX, originY + padding + cursorY);
            cursorX += r.w + gap; lineH = std::max(lineH, r.h); lineHasItem = true;
            usedW = std::max(usedW, originX + padding + cursorX - gap - originX);
        }
        double totalH = cursorY + lineH + padding*2;
        double totalW = innerMaxW + padding*2; // Wrap claims the full offered width, same as Row
        return {0,0, std::min(totalW,c.maxW), std::max(std::min(totalH,c.maxH), c.minH)};
    }

    // ---- Missing-components pass: Badge (§?) — a small pill/label, content-sized around its
    // own `text=` attribute (it is not a Text strand itself, same relationship Button's `label=`
    // has to the real TEXT strand kind: one self-contained box that draws its own text in Dye).
    Rect measureBadge(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 12);
        double hPad = 8, vPad = 4;
        double w = std::min(measureTextWidth(text, fontSize) + hPad*2, c.maxW);
        double h = std::min(fontSize*1.4 + vPad*2, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }
    // Tooltip: same idea as Badge (self-contained text box) but rectangular with soft corners
    // and slightly larger padding, matching a tooltip bubble's usual proportions rather than a
    // status pill's. See the DIALOG case above for the same honest scope note this shares: real
    // anchored positioning next to a target is a `@view.Stack align=/valign=` composition job for
    // the .rin author, not something Tooltip does for itself.
    Rect measureTooltip(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 12);
        double pad = resolveSpacing(*s, "padding", 8);
        double w = std::min(measureTextWidth(text, fontSize) + pad*2, c.maxW);
        double h = std::min(fontSize*1.4 + pad*2, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- Missing-components pass: Input/TextArea (§?) — a bordered field box sized around its
    // `value=`/`placeholder=` text (whichever is longer), with a sensible default height per kind
    // (`defaultH`: 40 for a single-line Input, 96 for a multi-line TextArea) unless height=
    // overrides it. There's no real caret/selection/multi-line-wrapping-while-typing model here —
    // this engine only computes the field's box, exactly the same design-time-preview scope
    // Image/Video/WebView already have (see measureMedia's doc comment above).
    Rect measureField(StrandPtr s, Constraints c, double defaultH) {
        std::string value = s->attrStr("value", ""), placeholder = s->attrStr("placeholder", "");
        double fontSize = resolveFontSize(*s, "size", 14);
        double contentW = measureTextWidth(value.empty() ? placeholder : value, fontSize);
        double w = s->attr("width") ? c.minW : std::min(std::max(120.0, contentW + 24.0), c.maxW);
        double h = s->attr("height") ? c.minH : std::min(defaultH, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }
};

} // namespace loom
