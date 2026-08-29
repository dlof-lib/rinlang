// loom/rin_loom_overlay.h — Overlay Engine.
//
// Closes the honest scope note the missing-components patch shipped with:
//   "Dialog/Tooltip lay out their own box only; there is no real overlay/positioning engine --
//    screen-centered positioning is achieved by wrapping in @view.Stack align=center valign=center,
//    the same pattern Stack's own doc comment already recommends for badges/FABs."
//
// This file does not throw that measuring work away -- Dialog's layoutSingleChildBox call and
// Tooltip's measureTooltip call in rin_loom_layout.h still run exactly as before, and still
// produce the correct content-sized box. What was missing is everything after "we have a box":
//
//   1. A separate z-layer.  Paint order in this engine is document order (Dye::paintInto walks
//      the tree parent-then-children, siblings in source order — see rin_loom_paint.h). A Dialog
//      sitting inside a Column only happened to paint "on top" in the demo .rin because it was
//      declared last; a sibling declared after it would have painted over it. isOverlayStrand()
//      below takes an open Dialog (and a `anchor=`-anchored Tooltip) OUT of its parent's normal-flow
//      contribution entirely (see the tail of Loom::layout() in rin_loom_layout.h), and
//      buildOverlayLayer() below is what actually re-homes it: a flat list, built in document
//      order (so relative stacking order among multiple simultaneously-open overlays is still
//      predictable/declaration-order, without inventing a z-index attribute), that the painter
//      draws strictly *after* the whole main tree, regardless of where in that tree the Strand
//      actually lives.
//   2. A scrim. Full-viewport, drawn only for a modal entry, between the main content and the
//      overlay's own box.
//   3. Event blocking. hitTestOverlayLayer() below is tried before Needle's normal hitTestPath
//      (see the dispatchTapWithOverlay wrapper in rin_loom_needle.h): a tap outside an open modal
//      Dialog's box, anywhere else on screen, is consumed by the scrim right here and never
//      reaches hitTestPath against the main fabric -- a real "block input to what's behind me",
//      not just a visual dimming.
//
// Positioning is against the *viewport* (viewportW x viewportH -- the visible screen), which is
// a different quantity from the root Constraints' maxH used everywhere else in this engine
// (Constraints{0, rootWidth, 0, 1e9} in rin_loom_c_api.cpp — deliberately unbounded, since the
// rest of the Loom lays out a scrollable design-time canvas, not a fixed screen). A Dialog
// centered against that unbounded content height would drift to nonsense coordinates the instant
// the page grew taller than one screen; centering against the viewport is what an app's real
// overlay compositor does, and what makes "screen-centered" actually mean the visible screen.
#pragma once
#include "rin_loom_strand.h"
#include "rin_loom_layout.h" // alignOffset() (free function) + Loom::translate() (member) — reused, not reinvented
#include <string>
#include <vector>
#include <algorithm>

namespace loom {

// isOverlayStrand() itself lives in rin_loom_layout.h (Loom::layout()'s own tail needs it to
// decide whether to zero a Strand's contribution to its parent's flow sizing — see the comment
// there); this file only consumes it, to avoid a layout.h <-> overlay.h include cycle.

enum class OverlayKind { DIALOG, TOOLTIP };

// One entry per open Dialog / anchored-and-visible Tooltip found in the fabric, in document
// order (== stacking order: entries found later paint later, i.e. on top -- the same "tree order
// is paint order" convention the rest of the Loom already uses, just applied to this second
// layer instead of inventing a z-index attribute).
struct OverlayEntry {
    StrandPtr strand;
    OverlayKind kind = OverlayKind::DIALOG;
    bool modal = false;              // Dialog only — does it get a scrim + block hits behind it?
    bool scrim = false;              // whether a scrim rect should be painted for this entry
    bool dismissOnScrimTap = true;   // tapping the scrim (outside the box) should close it
    Rect scrimRect;                  // full viewport rect; only meaningful when scrim == true
    Rect box;                        // the overlay's own on-screen rect, post-positioning
};

struct OverlayLayer {
    std::vector<OverlayEntry> entries; // .back() is topmost: painted last, hit-tested first
};

// Finds the first Strand anywhere under `root` whose source `name` equals `target` — resolves a
// Tooltip's `anchor="targetName"` anchor attribute. Same name buildFabric() already uses to derive a
// Strand's id (see deriveId in rin_loom_strand.h), just looked up at runtime instead of parse time.
inline StrandPtr findStrandByName(const StrandPtr& root, const std::string& target) {
    if (!root || target.empty()) return nullptr;
    if (root->name == target) return root;
    for (auto& c : root->children) {
        if (auto found = findStrandByName(c, target)) return found;
    }
    return nullptr;
}

inline void collectOverlayCandidates(const StrandPtr& s, std::vector<StrandPtr>& out) {
    if (!s) return;
    if (isOverlayStrand(*s)) out.push_back(s);
    for (auto& c : s->children) collectOverlayCandidates(c, out);
}

// Runs after Loom::layout(root, ...) has already measured the whole fabric (so every open
// Dialog's / anchored Tooltip's own content box, computed by the ordinary per-kind measure
// functions, is already sitting in s->geometry — just at a stale in-flow position that no longer
// means anything, since isOverlayStrand() made its parent skip it). Repositions each one against
// the viewport and returns the resulting layer for the painter and Needle to use.
//
// `engine` is passed in (rather than this file inventing its own translate()) so geometry
// mutation stays the one place Loom::translate already owns.
inline OverlayLayer buildOverlayLayer(Loom& engine, const StrandPtr& root,
                                       double viewportW, double viewportH) {
    OverlayLayer layer;
    if (!root) return layer;
    std::vector<StrandPtr> candidates;
    collectOverlayCandidates(root, candidates);

    for (auto& s : candidates) {
        OverlayEntry entry;
        entry.strand = s;
        Rect r = s->geometry; // correct box, wrong (irrelevant) position

        if (s->kind == StrandKind::DIALOG) {
            entry.kind = OverlayKind::DIALOG;
            std::string ha = s->attrStr("align", "center");
            std::string va = s->attrStr("valign", "center");
            double x = alignOffset(viewportW, r.w, ha == "left" || ha == "right" ? ha : "center");
            double y = alignOffset(viewportH, r.h, va == "top" || va == "bottom" ? va : "center");
            engine.translate(s, x - r.x, y - r.y);
            entry.box = s->geometry;

            entry.modal = s->attrStr("modal", "true") == "true";
            entry.scrim = entry.modal && s->attrStr("scrim", "true") == "true";
            entry.dismissOnScrimTap = s->attrStr("dismissible", "true") == "true";
            if (entry.scrim) entry.scrimRect = {0, 0, viewportW, viewportH};
        } else { // TOOLTIP
            entry.kind = OverlayKind::TOOLTIP;
            StrandPtr anchor = findStrandByName(root, s->attrStr("anchor", ""));
            double ax = 0, ay = 0, aw = 0, ah = 0;
            if (anchor) { ax = anchor->geometry.x; ay = anchor->geometry.y; aw = anchor->geometry.w; ah = anchor->geometry.h; }
            std::string placement = s->attrStr("placement", "top");
            const double gap = 6;
            double x = ax + (aw - r.w) / 2.0, y = ay - r.h - gap; // default: centered above the anchor
            if (placement == "bottom")      { y = ay + ah + gap; }
            else if (placement == "left")   { x = ax - r.w - gap; y = ay + (ah - r.h) / 2.0; }
            else if (placement == "right")  { x = ax + aw + gap;  y = ay + (ah - r.h) / 2.0; }
            // A floating overlay (unlike the original inline box) can drift off-screen near an
            // edge; clamp fully on-screen the way a real anchored-positioning primitive would.
            x = std::max(0.0, std::min(x, std::max(0.0, viewportW - r.w)));
            y = std::max(0.0, std::min(y, std::max(0.0, viewportH - r.h)));
            engine.translate(s, x - r.x, y - r.y);
            entry.box = s->geometry;
            entry.modal = false; entry.scrim = false; entry.dismissOnScrimTap = false;
        }
        layer.entries.push_back(entry);
    }
    return layer;
}

inline bool pointInRect(const Rect& r, double x, double y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// Result of testing a tap against the overlay layer, tried BEFORE Needle's normal document
// hitTestPath — exactly how a real compositor checks its topmost surface first.
struct OverlayHitResult {
    StrandPtr hit;                       // topmost overlay strand the point landed inside, if any
    bool blocked = false;                // a modal scrim ate this event — caller must stop here
    OverlayEntry* scrimOwner = nullptr;  // the entry whose scrim was hit (for dismiss-on-tap)
};

inline OverlayHitResult hitTestOverlayLayer(OverlayLayer& layer, double x, double y) {
    OverlayHitResult res;
    // Topmost first: entries are in document/declaration order, later == painted later == on top.
    for (auto it = layer.entries.rbegin(); it != layer.entries.rend(); ++it) {
        OverlayEntry& e = *it;
        if (pointInRect(e.box, x, y)) { res.hit = e.strand; return res; }
        if (e.modal) {
            // Inside this modal's full-viewport scrim but outside its own box: consumed here.
            // A dismissible modal treats this as "close"; a non-dismissible one just silently
            // blocks it -- either way nothing further down (including the main document, and
            // any lower overlay entries) ever sees the tap. That is the actual point of a modal.
            if (!e.scrim || pointInRect(e.scrimRect, x, y)) {
                res.blocked = true;
                res.scrimOwner = &e;
                return res;
            }
        }
    }
    return res;
}

} // namespace loom
