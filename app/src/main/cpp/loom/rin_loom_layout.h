// loom/rin_loom_layout.h — The Loom: two-pass Propose/Settle layout engine.
// (Same verified algorithm as the standalone prototype, operating on the real Strand type.)
#pragma once
#include "rin_loom_strand.h"
#include <algorithm>

namespace loom {

enum class Axis { X, Y };
inline double measureTextWidth(const std::string& text, double fontSize) { return text.size() * fontSize * 0.58; }

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

        Rect size;
        switch (s->kind) {
            case StrandKind::TEXT:    size = measureText(s, c); break;
            case StrandKind::IMAGE:   size = measureImage(s, c); break;
            case StrandKind::BUTTON:  size = measureButton(s, c); break;
            case StrandKind::DIVIDER: size = {0,0, c.maxW, 1}; break;
            case StrandKind::CARD:    size = layoutSingleChildBox(s, c, s->attrNum("padding", 12), originX, originY); break;
            case StrandKind::COLUMN:  size = layoutLinear(s, c, Axis::Y, originX, originY); break;
            case StrandKind::ROW:     size = layoutLinear(s, c, Axis::X, originX, originY); break;
            case StrandKind::STACK:   size = layoutStack(s, c, originX, originY); break;
            default:                  size = layoutLinear(s, c, Axis::Y, originX, originY); break; // Bolt fallback
        }
        size.x = originX; size.y = originY;
        s->geometry = size;
        return size;
    }

    void translate(StrandPtr s, double dx, double dy) {
        s->geometry.x += dx; s->geometry.y += dy;
        for (auto& c : s->children) translate(c, dx, dy);
    }

    Rect measureText(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = s->attrNum("size", 16);
        double w = std::min(measureTextWidth(text, fontSize), c.maxW);
        double h = std::min(fontSize * 1.4, c.maxH);
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
        double gap = s->attrNum("gap", 0), padding = s->attrNum("padding", 0);
        double mainUsed=0, crossMax=0, cursorMain=padding;
        double innerMaxW = std::max(0.0, c.maxW - padding*2), innerMaxH = std::max(0.0, c.maxH - padding*2);
        for (auto& child : s->children) {
            Constraints cc{0, innerMaxW, 0, innerMaxH};
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
