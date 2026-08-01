// loom/rin_loom_layout.h — The Loom: two-pass Propose/Settle layout engine.
// (Same verified algorithm as the standalone prototype, operating on the real Strand type.)
#pragma once
#include "rin_loom_strand.h"
#include <algorithm>

namespace loom {

enum class Axis { X, Y };

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
        }
        if (!s->children.empty()) mainUsed -= gap;
        double totalW = (axis==Axis::Y) ? std::min(crossMax+padding*2,c.maxW) : std::min(mainUsed+padding*2,c.maxW);
        double totalH = (axis==Axis::Y) ? std::min(mainUsed+padding*2,c.maxH) : std::min(crossMax+padding*2,c.maxH);
        return {0,0, std::max(totalW,c.minW), std::max(totalH,c.minH)};
    }
    Rect layoutStack(StrandPtr s, Constraints c, double originX, double originY) {
        double w=0,h=0;
        for (auto& child : s->children) {
            Rect r = layout(child, {0, c.maxW, 0, c.maxH}, originX, originY);
            w = std::max(w, r.w); h = std::max(h, r.h);
        }
        return {0,0, std::min(w,c.maxW), std::min(h,c.maxH)};
    }
};

} // namespace loom
