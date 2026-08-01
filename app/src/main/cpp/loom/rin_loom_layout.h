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

        Rect size;
        switch (s->kind) {
            case StrandKind::TEXT:    size = measureText(s, c2); break;
            case StrandKind::IMAGE:   size = measureImage(s, c2); break;
            case StrandKind::BUTTON:  size = measureButton(s, c2); break;
            case StrandKind::DIVIDER: size = {0,0, c2.maxW, std::max(1.0, c2.minH)}; break;
            case StrandKind::CARD:    size = layoutSingleChildBox(s, c2, s->attrNum("padding", 12) + s->attrNum("border", 0), originX, originY); break;
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

            // ---- new: table — a header row (columns=) plus TABLEROW children of cells.
            case StrandKind::TABLE: size = layoutTable(s, c2, originX, originY); break;
            case StrandKind::TABLEROW: size = layoutLinear(s, c2, Axis::X, originX, originY); break;

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
        double fontSize = s->attrNum("size", 16);
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
        double w = std::min(measureTextWidth(label, 16) + 32, c.maxW);
        double h = std::min(44.0, c.maxH);
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
    Rect layoutLinear(StrandPtr s, Constraints c, Axis axis, double originX, double originY) {
        double gap = s->attrNum("gap", 0), padding = s->attrNum("padding", 0) + s->attrNum("border", 0);
        double mainUsed=0, crossMax=0, cursorMain=padding;
        double innerMaxW = std::max(0.0, c.maxW - padding*2), innerMaxH = std::max(0.0, c.maxH - padding*2);
        // Cross-axis alignment: Column (axis=Y) aligns children horizontally via `align`;
        // Row (axis=X) aligns children vertically via `valign`. Default is left/top (offset 0),
        // matching the previous unaligned behavior exactly, so existing .rin files don't move.
        std::string crossMode = (axis == Axis::Y) ? s->attrStr("align", "left") : s->attrStr("valign", "top");
        double innerCross = (axis == Axis::Y) ? innerMaxW : innerMaxH;

        struct Placed { StrandPtr child; double crossSize; };
        std::vector<Placed> placed;

        for (auto& child : s->children) {
            double remainingMain = std::max(0.0, (axis==Axis::Y ? innerMaxH : innerMaxW) - cursorMain);
            double childMaxW = (axis==Axis::Y) ? innerMaxW : remainingMain;
            double childMaxH = (axis==Axis::Y) ? remainingMain : innerMaxH;
            Constraints cc{0, childMaxW, 0, childMaxH};
            double localX = (axis==Axis::Y) ? padding : cursorMain;
            double localY = (axis==Axis::Y) ? cursorMain : padding;
            Rect r = layout(child, cc, originX + localX, originY + localY);
            double mainSize = (axis==Axis::Y) ? r.h : r.w, crossSize = (axis==Axis::Y) ? r.w : r.h;
            cursorMain += mainSize + gap; mainUsed += mainSize + gap; crossMax = std::max(crossMax, crossSize);
            placed.push_back({child, crossSize});
        }
        if (!s->children.empty()) mainUsed -= gap;

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
            w = std::max(w, r.w); h = std::max(h, r.h);
            rects.push_back(r);
        }
        if ((ha != "left" && ha != "stretch") || (va != "top" && va != "stretch")) {
            for (size_t i = 0; i < s->children.size(); i++) {
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
        double padding = s->attrNum("padding", 0);
        double innerMaxW = std::max(0.0, c.maxW - padding*2);
        std::vector<std::string> headers = splitCsv(s->attrStr("columns", ""));
        size_t nCols = headers.size();
        for (auto& row : s->children) nCols = std::max(nCols, row->children.size());
        if (nCols == 0) return {0,0, std::max(0.0,c.minW), std::max(0.0,c.minH)};

        std::vector<double> colW(nCols, 0.0);
        double fontSize = s->attrNum("size", 14);
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
        for (auto& ch : content) { Rect r = layout(ch, {c.maxW, c.maxW, 0, contentH}, originX, cy); }
        if (bottombar) layout(bottombar, {c.maxW, c.maxW, bottomH, bottomH}, originX, originY + topH + contentH);
        if (drawer) {
            bool isOpen = drawer->attrStr("open", "false") == "true";
            double dw = drawer->attr("width") ? drawer->attrNum("width", 280) : 280.0;
            double dx = isOpen ? originX : originX - dw; // off-canvas to the left when closed
            layout(drawer, {isOpen ? dw : 0, isOpen ? dw : 0, c.maxH, c.maxH}, dx, originY);
        }
        return {0,0, c.maxW, c.maxH};
    }
};

} // namespace loom
