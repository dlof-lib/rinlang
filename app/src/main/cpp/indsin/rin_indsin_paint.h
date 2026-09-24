// indsin/rin_indsin_paint.h — Dye: paint engine (Strand geometry -> DrawList -> raster/JSON).
#pragma once
#include "rin_indsin_font.h"
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include "rin_indsin_layout.h" // splitCsv() -- reused here for DonutChart's data=/colors= parsing, same as Breadcrumb/Pagination already reuse it via rin_indsin_components_ext.h
#include "rin_indsin_overlay.h" // OverlayLayer -- see paintWithOverlay() below
#include "rin_indsin_icons.h"   // IconRegistry -- §18
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <zlib.h>

namespace indsin {

// Color, parseHexColor, and the semantic Theme/Pattern-Book machinery now live in
// rin_indsin_tokens.h (Rin Indsin's Color Engine) — this file only resolves a Strand's final
// paint color from it, keeping the per-kind fallback palette below as the last resort when
// neither `tone=`, `type=` (Banner), nor an explicit `color=` attribute is present.
inline Color colorForKind(StrandKind k) {
    switch (k) {
        case StrandKind::CARD:   return themeRegistry().active().surface;
        case StrandKind::BUTTON: return themeRegistry().active().primary;
        case StrandKind::TEXT:   return themeRegistry().active().text;
        case StrandKind::IMAGE:  return {70,70,90};
        case StrandKind::DIVIDER:return themeRegistry().active().border;
        case StrandKind::BANNER: return themeRegistry().active().neutral; // "custom"/unset default; see bannerTypeColor()
        case StrandKind::BOX:    return themeRegistry().active().surface;
        case StrandKind::AVATAR: return themeRegistry().active().neutral;
        case StrandKind::INPUT:
        case StrandKind::TEXTAREA: return themeRegistry().active().surface;
        case StrandKind::DIALOG: return themeRegistry().active().surface;
        case StrandKind::TOOLTIP: return themeRegistry().active().neutral;
        case StrandKind::PROGRESS: return themeRegistry().active().primary; // filled portion default; track uses border
        case StrandKind::BADGE:  return themeRegistry().active().primary;
        case StrandKind::ICON:   return themeRegistry().active().text; // a plain glyph reads as text-toned by default
        case StrandKind::ICONBUTTON: return themeRegistry().active().primary; // same default as Button
        case StrandKind::OBJECT: return themeRegistry().active().surface; // §21: Card-like panel

        // Ready-elements expansion:
        case StrandKind::LINK:   return themeRegistry().active().primary; // reads as a link, not plain text
        case StrandKind::RADIO:  return themeRegistry().active().primary; // same tone role as Checkbox/Switch
        case StrandKind::SLIDER: return themeRegistry().active().primary; // filled portion + thumb, same as Progress
        case StrandKind::SEARCH:
        case StrandKind::SELECT:
        case StrandKind::FILE:
        case StrandKind::DATE:
        case StrandKind::TIME:
        case StrandKind::CODE_EDITOR: return themeRegistry().active().surface; // same field box as Input/TextArea
        case StrandKind::CALCULATOR:  return themeRegistry().active().surface; // self-contained widget panel

        // UI/UX Library Expansion:
        case StrandKind::TAG:      return themeRegistry().active().primary; // same tone role as Badge
        case StrandKind::KBD:      return themeRegistry().active().surface;
        case StrandKind::SKELETON: return themeRegistry().active().border; // muted placeholder block
        case StrandKind::SPINNER:  return themeRegistry().active().primary;
        case StrandKind::DONUT_CHART: return themeRegistry().active().surface; // segments carry their own colors

        default: return themeRegistry().active().background;
    }
}
// Banner-specific palette for its `type` attr (info/success/warning/error/action/progress/custom):
// these map onto the same semantic Theme roles a `tone=` attribute would use elsewhere, so a
// Banner's "warning" type and a Button's tone="warning" always render the same color.
inline Color bannerTypeColor(const std::string& type) {
    const Theme& th = themeRegistry().active();
    if (type == "success")  return th.success;
    if (type == "warning")  return th.warning;
    if (type == "error")    return th.danger;
    if (type == "action")   return th.primary;
    if (type == "progress") return th.info;
    if (type == "info")     return th.info;
    return colorForKind(StrandKind::BANNER); // "custom" / unset -> neutral
}
// Hex-formats a resolved Color for the JSON bridge (see fabricToJson's "resolvedColor" field
// below) -- the Kotlin/Compose preview renderer (IndsinFabricView.kt) has no access to the native
// Color Engine (tone=/color=<role> resolution, the active @theme=), so without this the preview
// can only ever understand a literal "color=\"#RRGGBB\"" written straight into the .rin source
// and otherwise falls back to its own hardcoded palette -- silently ignoring tone=, a semantic
// color=<role> name, and any non-default @theme= entirely. Emitting the already-resolved color
// here makes fabricToJson's output the same single source of truth for paint color that
// Dye::paintInto() already uses to rasterize the "real" (PNG-export) path.
// Hex-formats a resolved Color for the JSON bridge (see fabricToJson's "resolvedColor" field
// below) -- the Kotlin/Compose preview renderer (IndsinFabricView.kt) has no access to the native
// Color Engine (tone=/color=<role> resolution, the active @theme=), so without this the preview
// can only ever understand a literal "color=\"#RRGGBB\"" written straight into the .rin source
// and otherwise falls back to its own hardcoded palette -- silently ignoring tone=, a semantic
// color=<role> name, and any non-default @theme= entirely. Emitting the already-resolved color
// here makes fabricToJson's output the same single source of truth for paint color that
// Dye::paintInto() already uses to rasterize the "real" (PNG-export) path.
//
// Always 6-digit RGB (alpha is carried separately as its own JSON field -- see resolvedAlpha /
// fabricToJson below) so every existing consumer of a 7-char "#RRGGBB" string keeps working
// unchanged even now that Color itself carries alpha.
inline std::string colorToHex(Color c) { return rincolor::toHex6(c); }
// 0..1 alpha as a plain fraction, for JSON consumers that want to composite/opacity a fill
// themselves (Kotlin's Color.argb(), a Canvas/GPU backend's own paint alpha, etc.).
inline double colorAlphaUnit(Color c) { return rincolor::unitFromAlpha(c.a); }

// Reads a color-family attribute's raw value as either a literal (#hex/rgb()/rgba()/hsl()/
// hsla()/named) or a semantic Theme role name ("primary"/"danger"/...) -- the one bit of
// "does this string mean a literal or a role" logic every color= / background= / borderColor=
// attribute needs, factored out so all three share it instead of re-deriving it.
inline bool resolveColorAttr(const StrandPtr& s, const std::string& key, Color& out) {
    auto v = s->attr(key);
    if (!v || v->kind != Value::Kind::STRING || v->str.empty()) return false;
    if (looksLikeHexColor(v->str)) { out = parseHexColor(v->str, out); return true; }
    return resolveSemanticColor(v->str, out);
}

// Resolves a Strand's paint color, in order:
//   1. tone="<role>"   — semantic Theme role (primary/success/danger/...), the Color Engine's
//                         intended everyday spelling — see spec §8/§9 ("Button { tone: primary; }").
//   2. type="<...>"    — Banner-only notification-kind shorthand (backward compatible).
//   3. color="..."     — any literal (#RRGGBB, #RRGGBBAA, rgb()/rgba(), hsl()/hsla(), a named
//                         CSS color like "tomato") or a semantic role name — still supported for
//                         one-off overrides, now with real alpha and the engine's full syntax.
//   4. background="..."— same literal/role syntax as color=, checked when color= is absent, for
//                         sources that spell a fill this way (matches the `background`/`element_
//                         background` attribute the schema already recognized but never painted).
//   5. per-kind theme-based default (colorForKind / bannerTypeColor).
// Finally, opacity="0..1" (or "0%..100%"), if present, scales whatever alpha the color above
// already carries -- so `color="rgba(255,0,0,0.5)" opacity="0.5"` composes to alpha 0.25, the
// same layering CSS's own `opacity` gives a color that already has its own alpha.
inline Color resolveColor(const StrandPtr& s) {
    Color fallback = (s->kind == StrandKind::BANNER)
        ? bannerTypeColor(s->attrStr("type", ""))
        // Link concepts (docs/link.md): `visited="true"` swaps the default (untoned) link color
        // for a muted violet, the same convention the web uses to tell an already-followed link
        // apart from a fresh one — checked at the same fallback precedence level as colorForKind's
        // plain LINK->primary default, so an explicit tone=/color= below still overrides either.
        : (s->kind == StrandKind::LINK && s->attrStr("visited", "false") == "true")
            ? Color{160, 130, 255}
            : colorForKind(s->kind);

    Color resolved = fallback;
    bool found = false;
    if (auto tone = s->attr("tone")) {
        Color c;
        if (tone->kind == Value::Kind::STRING && resolveSemanticColor(tone->str, c)) { resolved = c; found = true; }
    }
    // `bg=` is the explicit fill spelling: it wins over color= (which, on a text-bearing box, is then its text color).
    if (!found && resolveColorAttr(s, "bg", resolved)) found = true;
    if (!found && resolveColorAttr(s, "color", resolved)) found = true;
    if (!found && resolveColorAttr(s, "background", resolved)) found = true;

    if (auto op = s->attr("opacity")) {
        double amount = op->asNumber(1.0);
        if (op->kind == Value::Kind::STRING && !op->str.empty() && op->str.back() == '%') amount = op->asNumber(100.0) / 100.0;
        resolved.a = rincolor::alphaFromUnit(amount * rincolor::unitFromAlpha(resolved.a));
    }
    return resolved;
}

// Resolves a Strand's border/stroke color: an explicit `borderColor=` literal or role name,
// else the active Theme's `border` slot -- the same two-tier fallback resolveColor() uses for
// fill, just anchored on the border role instead of a per-kind default.
inline Color resolveBorderColor(const StrandPtr& s) {
    Color out = themeRegistry().active().border;
    resolveColorAttr(s, "borderColor", out);
    return out;
}

// SCRIM_RECT (Overlay Engine, rin_indsin_overlay.h): visually a FILL_RECT, distinguished only so a
// renderer that wants to special-case scrims still can — Color now carries real alpha (see
// rin_color.h), so this DrawCommand's own .color.a IS the scrim's actual translucency, not a
// renderer-side convention keyed on the op the way it had to be before the Color Engine existed.
// rasterizeToBuffer() below alpha-blends every DrawCommand the same way regardless of op.
enum class DrawOp { FILL_RECT, STROKE_RECT, TEXT_RUN, SCRIM_RECT, STROKE_ARC };
// startAngleDeg/sweepAngleDeg only mean anything for STROKE_ARC (0deg = 3 o'clock, clockwise,
// matching Android's Canvas.drawArc convention exactly so IndsinFabricView.kt can pass them
// straight through -- see its drawArcCommand()/drawSpinner()/drawDonutChart()). Every other op
// ignores them (left at their defaults), same as radius/strokeWidth already sit unused on
// TEXT_RUN today.
struct DrawCommand { DrawOp op; Rect bounds; Color color; std::string text; StrandId owner; double radius = 0; double strokeWidth = 0; double startAngleDeg = 0; double sweepAngleDeg = 360;
    // TEXT_RUN style (filled by Dye::paintInto's post-pass; 0 = unset): px font size, align 0=start(auto RTL) 1=center 2=end, bold, single-line (ellipsize) vs wrapped.
    double fontSize = 0; int align = 0; bool bold = false; bool singleLine = false; };
using DrawList = std::vector<DrawCommand>;

// The scrim's color, ~55% black — a fixed near-black rather than a Theme role, since a scrim
// dims *whatever theme is active* rather than participating in it the way primary/surface/etc.
// do. The alpha here (140/255) matches IndsinFabricView.kt's own scrimColor exactly, so the two
// renderers dim a modal's backdrop by the same amount now that both understand real alpha.
inline Color scrimColor() { return {0, 0, 0, 140}; }

struct Dye {
    DrawList paint(const StrandPtr& s) { DrawList list; paintInto(s, list); return list; }

    // Overlay Engine (rin_indsin_overlay.h): paints the main document exactly as paint() above
    // always has, then appends the overlay layer strictly afterward, in its own back-to-front
    // order (see OverlayLayer's doc comment) -- a real second z-layer, not a hope that tree order
    // happened to put Dialog/Tooltip last. Each modal entry's scrim is pushed immediately before
    // that entry's own subtree, so scrim-then-box-then-its-children is the actual paint order for
    // every entry, exactly like a native overlay compositor draws a dimmed backdrop then its sheet.
    DrawList paintWithOverlay(const StrandPtr& root, OverlayLayer& layer) {
        DrawList list;
        paintInto(root, list);
        for (auto& entry : layer.entries) {
            if (entry.scrim) list.push_back({DrawOp::SCRIM_RECT, entry.scrimRect, scrimColor(), "", entry.strand ? entry.strand->id : 0, 0, 0});
            paintInto(entry.strand, list);
        }
        return list;
    }
    // Post-pass wrapper: every TEXT_RUN a strand emits gets the strand's real text style so no backend
    // (native rasterizer, HTML export) has to guess font size / alignment / weight from the box height.
    void paintInto(const StrandPtr& s, DrawList& list) {
        size_t first = list.size();
        paintIntoImpl(s, list);
        bool centered = s->kind == StrandKind::BUTTON || s->kind == StrandKind::ICONBUTTON || s->kind == StrandKind::TABITEM ||
                        s->kind == StrandKind::BADGE || s->kind == StrandKind::AVATAR || s->kind == StrandKind::TAG;
        bool single = centered || s->kind == StrandKind::INPUT || s->kind == StrandKind::SEARCH || s->kind == StrandKind::SELECT ||
                      s->kind == StrandKind::LINK;
        for (size_t i = first; i < list.size(); i++) {
            auto& d = list[i];
            if (d.op != DrawOp::TEXT_RUN || d.owner != s->id || d.fontSize > 0) continue;
            d.fontSize = resolveFontSize(*s, s->kind == StrandKind::BUTTON ? "labelSize" : "size", 14);
            d.align = centered ? 1 : 0;
            std::string ta = s->attrStr("textAlign", s->attrStr("text_align", ""));
            if (ta == "center") d.align = 1; else if (ta == "right" || ta == "end") d.align = 2; else if (ta == "left" || ta == "start") d.align = 0;
            std::string wt = s->attrStr("weight", s->attrStr("fontWeight", ""));
            d.bold = centered || wt == "bold" || wt == "700" || wt == "600" || s->attrStr("bold", "") == "true";
            d.singleLine = single;
        }
    }
    void paintIntoImpl(const StrandPtr& s, DrawList& list) {
        double r = std::min(resolveRadius(*s, 0), std::min(s->geometry.w, s->geometry.h) / 2.0);

        if (s->kind == StrandKind::BUTTON || s->kind == StrandKind::TABITEM) { paintButton(s, list, r); for (auto& c : s->children) paintInto(c, list); return; }
        if (s->kind == StrandKind::ICONBUTTON) { paintButton(s, list, r); for (auto& c : s->children) paintInto(c, list); return; }
        if (s->kind == StrandKind::ICON) { paintIcon(s, list); return; }

        // Missing-components pass: Spacer never draws anything (it's pure layout space) — same
        // "skip the generic FILL_RECT" carve-out TEXT already gets below, just with no TEXT_RUN
        // to replace it with either.
        if (s->kind == StrandKind::SPACER) { for (auto& c : s->children) paintInto(c, list); return; }

        if (s->kind == StrandKind::BADGE)  { paintBadge(s, list, r); return; } // no children — Badge is a leaf
        if (s->kind == StrandKind::PROGRESS) { paintProgress(s, list, r); return; }
        if (s->kind == StrandKind::CHECKBOX) { paintCheckbox(s, list, r); return; }
        if (s->kind == StrandKind::SWITCH)   { paintSwitch(s, list); return; }
        if (s->kind == StrandKind::AVATAR)   { paintAvatar(s, list, r); return; }
        if (s->kind == StrandKind::INPUT || s->kind == StrandKind::TEXTAREA ||
            s->kind == StrandKind::SEARCH || s->kind == StrandKind::SELECT || s->kind == StrandKind::FILE ||
            s->kind == StrandKind::DATE || s->kind == StrandKind::TIME || s->kind == StrandKind::CODE_EDITOR)
            { paintField(s, list, r); return; } // same bordered box + value/placeholder text as Input

        // Ready-elements expansion: Radio/Slider get their own dedicated shapes (a ring+dot and a
        // track+thumb respectively) rather than reusing Checkbox/Progress's plain-filled look.
        if (s->kind == StrandKind::RADIO)  { paintRadio(s, list); return; }
        if (s->kind == StrandKind::SLIDER) { paintSlider(s, list, r); return; }
        // Link concepts (docs/link.md): a plain text run in the link tone (visited-aware via
        // resolveColor above), no box at all — same convention Button's `variant="link"`
        // treatment already uses (see paintButton's ButtonTreatment::LINK case) — plus a real
        // underline by default, since that's the one visual cue that actually reads as "this is
        // a hyperlink" rather than plain colored text. `underline="false"` opts out (e.g. a Link
        // used as a plain nav item that shouldn't look clickable-in-body-text).
        if (s->kind == StrandKind::LINK) {
            Color linkColor = resolveColor(s);
            std::string text = s->attrStr("text", "");
            list.push_back({DrawOp::TEXT_RUN, s->geometry, linkColor, text, s->id, 0, 0});
            if (!text.empty() && s->attrStr("underline", "true") == "true") {
                // Link's own box is already measured to fit its text (see rin_indsin_layout.h's
                // `case StrandKind::LINK: size = measureText(s, c2);`), so the geometry's width
                // doubles as the underline's span without a separate text-measurement pass here.
                Rect underlineRect = s->geometry;
                underlineRect.y = s->geometry.y + s->geometry.h - 2;
                underlineRect.h = 1.5;
                list.push_back({DrawOp::FILL_RECT, underlineRect, linkColor, "", s->id, 0, 0});
            }
            for (auto& c : s->children) paintInto(c, list);
            return;
        }

        // UI/UX Library Expansion — Tag: pill background painted here, then its synthesized
        // Text label + optional close Button children (see applyTagConveniences() in
        // rin_indsin_components_ext.h) paint themselves through the normal recursive path below
        // -- same "draw my own box, then recurse" shape Card/Box already use.
        if (s->kind == StrandKind::TAG) {
            double pillRadius = std::min(resolveRadius(*s, 999.0), s->geometry.h / 2.0);
            list.push_back({DrawOp::FILL_RECT, s->geometry, resolveColor(s), "", s->id, pillRadius, 0});
            for (auto& c : s->children) paintInto(c, list);
            return;
        }
        if (s->kind == StrandKind::KBD) { paintKbd(s, list); return; }
        if (s->kind == StrandKind::RATING) { paintRating(s, list); return; }
        if (s->kind == StrandKind::SKELETON) { paintSkeleton(s, list); return; }
        if (s->kind == StrandKind::SPINNER) { paintSpinner(s, list); return; }
        if (s->kind == StrandKind::DONUT_CHART) { paintDonutChart(s, list); return; }
        if (s->kind == StrandKind::STEPITEM) { paintStepItem(s, list); return; }
        if (s->kind == StrandKind::STEPS) {
            paintStepsConnector(s, list);
            for (auto& c : s->children) paintInto(c, list);
            return;
        }
        if (s->kind == StrandKind::TIMELINEITEM) { paintTimelineItem(s, list); return; }
        if (s->kind == StrandKind::TIMELINE) {
            paintTimelineConnector(s, list);
            for (auto& c : s->children) paintInto(c, list);
            return;
        }
        // Breadcrumb/Pagination: both are just Rows of synthesized Text/Button children (see
        // rin_indsin_components_ext.h) once buildFabric+conveniences have run -- no dedicated
        // paint case needed, the generic container path right below (background+border, then
        // recurse) already does the right thing for a plain Row-shaped box.

        // Card conveniences: title=/subtitle=/value=/description= were parsed onto the Strand's
        // attrs but never painted anywhere -- there was no dedicated CARD case here at all, so a
        // Card fell into the generic container path below (background+border only) and rendered
        // as an empty, unlabeled colored box, no matter what title=/subtitle=/value=/description=
        // the .rin source gave it. Mirrors the "draw my own box, then stack plain text runs, then
        // recurse into any hand-authored children" shape Tag/Banner already use just above: a bold
        // title first, then an optional large value= (the stat-card idiom used throughout the
        // dashboard demo), then a muted subtitle=/description= wrapped into whatever vertical
        // space remains. IndsinFabricView.kt's Kind.CARD case mirrors this exactly.
        if (s->kind == StrandKind::CARD) {
            list.push_back({DrawOp::FILL_RECT, s->geometry, resolveColor(s), "", s->id, r, 0});
            double borderWidth = s->attrNum("border", 0);
            if (borderWidth > 0)
                list.push_back({DrawOp::STROKE_RECT, s->geometry, resolveBorderColor(s), "", s->id, r, borderWidth});

            const Theme& th = themeRegistry().active();
            double pad = 14;
            double innerX = s->geometry.x + pad;
            double innerW = std::max(0.0, s->geometry.w - pad * 2);
            double bottom = s->geometry.y + s->geometry.h - pad;
            double cursorY = s->geometry.y + pad;

            std::string title = s->attrStr("title", "");
            std::string value = s->attrStr("value", "");
            std::string subtitle = s->attrStr("subtitle", "");
            if (subtitle.empty()) subtitle = s->attrStr("description", "");

            if (!title.empty() && cursorY < bottom) {
                double lineH = 15 * 1.4;
                list.push_back({DrawOp::TEXT_RUN, {innerX, cursorY, innerW, lineH}, th.text, title, s->id, 0, 0});
                cursorY += lineH + 4;
            }
            if (!value.empty() && cursorY < bottom) {
                double lineH = 22 * 1.4;
                list.push_back({DrawOp::TEXT_RUN, {innerX, cursorY, innerW, lineH}, th.text, value, s->id, 0, 0});
                cursorY += lineH + 2;
            }
            if (!subtitle.empty() && cursorY < bottom) {
                list.push_back({DrawOp::TEXT_RUN, {innerX, cursorY, innerW, std::max(0.0, bottom - cursorY)},
                                 th.text_muted, subtitle, s->id, 0, 0});
            }

            for (auto& c : s->children) paintInto(c, list);
            return;
        }

        if (s->kind != StrandKind::TEXT) {
            list.push_back({DrawOp::FILL_RECT, s->geometry, resolveColor(s), "", s->id, r, 0});
            // `border=` was already a real padding contributor (rin_indsin_layout.h) but never
            // actually painted anything -- a Strand with border="2" reserved space for a stroke
            // that never appeared. Now it draws one, in borderColor= (or the Theme's border
            // role) — the generic path every kind without its own dedicated stroke logic
            // (Checkbox/Radio/Field/Button-OUTLINE all already draw their own) falls through to.
            double borderWidth = s->attrNum("border", 0);
            if (borderWidth > 0)
                list.push_back({DrawOp::STROKE_RECT, s->geometry, resolveBorderColor(s), "", s->id, r, borderWidth});
        }
        if (s->kind == StrandKind::TEXT)
            list.push_back({DrawOp::TEXT_RUN, s->geometry, resolveColor(s), s->attrStr("text"), s->id, 0, 0});
        if (s->kind == StrandKind::TOOLTIP)
            list.push_back({DrawOp::TEXT_RUN, s->geometry, themeRegistry().active().background, s->attrStr("text"), s->id, 0, 0});
        for (auto& c : s->children) paintInto(c, list);
    }

    // ---- Missing-components pass: paint functions for the new leaf-ish StrandKinds. Each
    // follows paintButton's own pattern above (resolve tone/state first, then draw), so a new
    // Renderer backend reading the DrawList doesn't need any kind-specific knowledge beyond what
    // it already needs for Button/Card/Text.
    void paintBadge(const StrandPtr& s, DrawList& list, double /*radius*/) {
        Color bg = resolveColor(s);
        double pillRadius = std::min(s->geometry.w, s->geometry.h) / 2.0;
        list.push_back({DrawOp::FILL_RECT, s->geometry, bg, "", s->id, pillRadius, 0});
        list.push_back({DrawOp::TEXT_RUN, s->geometry, themeRegistry().active().background, s->attrStr("text"), s->id, 0, 0});
    }
    // ---- UI/UX Library Expansion: paint functions for the new leaf-ish/composite StrandKinds.
    // Same rule the missing-components pass above already follows: resolve tone/state first, then
    // draw, using only FILL_RECT/STROKE_RECT/TEXT_RUN (no new DrawOp) -- see the StrandKind enum's
    // own doc comment in rin_indsin_strand.h for why that's a real limit here, not laziness.

    void paintKbd(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        double pillRadius = std::min(4.0, std::min(s->geometry.w, s->geometry.h) / 3.0);
        list.push_back({DrawOp::FILL_RECT, s->geometry, th.surface, "", s->id, pillRadius, 0});
        list.push_back({DrawOp::STROKE_RECT, s->geometry, th.border, "", s->id, pillRadius, 1.5});
        list.push_back({DrawOp::TEXT_RUN, s->geometry, th.text, s->attrStr("text", ""), s->id, 0, 0});
    }

    // Rating: `max=` equal-width cells, each a single filled/unfilled star glyph drawn through
    // the ordinary TEXT_RUN path (same technique the Icon Registry uses for every other glyph —
    // see rin_indsin_icons.h's own doc comment on why this rasterizer draws symbols this way).
    // The filled color is a fixed amber rather than a Theme role deliberately: a star rating's
    // "filled" look is its own convention (gold stars), not something tone=/the active Theme
    // should reskin the way a Badge's or Button's fill does.
    void paintRating(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        int maxStars = (int)std::max(1.0, s->attrNum("max", 5));
        double value = std::max(0.0, std::min((double)maxStars, s->attrNum("value", 0)));
        double starW = s->geometry.w / maxStars;
        Color filledColor = {245, 176, 65};
        for (int i = 0; i < maxStars; ++i) {
            Rect cell{ s->geometry.x + i*starW, s->geometry.y, starW, s->geometry.h };
            bool filled = (i + 1) <= (int)std::round(value);
            std::string glyph = filled ? "\xE2\x98\x85" : "\xE2\x98\x86"; // "★" / "☆"
            list.push_back({DrawOp::TEXT_RUN, cell, filled ? filledColor : th.border, glyph, s->id, 0, 0});
        }
    }

    // Skeleton: a placeholder block. A real shimmer sweep needs a time-varying gradient this
    // rasterizer's three static DrawOps can't express (same honest-scope limit as Icon/GRADIENT
    // above) -- this paints the static placeholder box itself; a host renderer (IndsinFabricView.kt,
    // the desktop/CLI hosts) is free to layer its own shimmer animation on top of any Strand
    // tagged SKELETON, the same way a real design tool's skeleton loader is just CSS on a div.
    void paintSkeleton(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        double radius = std::min(resolveRadius(*s, 4.0), std::min(s->geometry.w, s->geometry.h) / 2.0);
        list.push_back({DrawOp::FILL_RECT, s->geometry, th.border, "", s->id, radius, 0});
    }

    // Spinner: now a genuinely round, partially-swept ring via DrawOp::STROKE_ARC (Rect{x,y,w,h}
    // as the arc's bounding box, startAngleDeg/sweepAngleDeg in Android's own Canvas.drawArc
    // convention -- 0deg = 3 o'clock, clockwise) -- this rasterizer's actual first arc primitive,
    // not the earlier two-stroked-squares illusion this function used before. It's still one
    // static frame (Dye never animates anything -- see paintSkeleton's own note above), so a host
    // renderer that wants a real rotating spin still has to animate this Strand's rotation itself
    // (IndsinFabricView.kt's drawSpinner() re-derives the same sweep and spins it via a
    // ValueAnimator on its own render loop); this paints one frame of that spin, at rotation 0.
    void paintSpinner(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        double thickness = std::max(2.0, s->geometry.w * 0.12);
        Rect ring{ s->geometry.x + thickness/2.0, s->geometry.y + thickness/2.0,
                   s->geometry.w - thickness, s->geometry.h - thickness };
        // A faint full-circle track first (so the "gap" in the active sweep below doesn't read
        // as a missing chunk of the ring), then a shorter, tone-colored sweep over the top.
        DrawCommand track{DrawOp::STROKE_ARC, ring, th.border, "", s->id, 0, thickness, 0, 360};
        list.push_back(track);
        DrawCommand sweep{DrawOp::STROKE_ARC, ring, resolveColor(s), "", s->id, 0, thickness, -90, 270};
        list.push_back(sweep);
    }

    // DonutChart: `data="Label:Value,Label2:Value2,..."` (colon/comma convention, same shorthand
    // shape Breadcrumb's items= and Pagination's current=/total= already use) -> one
    // DrawOp::STROKE_ARC segment per entry, each segment's sweepAngleDeg proportional to its
    // share of the total. `colors="#..,#.."` assigns colors by position; without it, segments
    // cycle through a small fixed palette (same "a sensible default, not a hard requirement"
    // relationship Rating's fixed amber fill color has to tone=). `centerLabel=` (or the running
    // total, formatted as an integer, when unset) is drawn as TEXT_RUN in the donut's hole.
    void paintDonutChart(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        static const Color kPalette[] = {
            {124, 92, 255}, {34, 200, 142}, {232, 178, 61}, {241, 76, 76}, {95, 211, 255}, {145, 152, 163}
        };
        std::string dataAttr = s->attrStr("data", "");
        std::vector<std::pair<std::string,double>> segments;
        for (auto& entry : splitCsv(dataAttr)) {
            size_t colon = entry.find(':');
            if (colon == std::string::npos) continue;
            std::string label = entry.substr(0, colon);
            double value = std::atof(entry.substr(colon + 1).c_str());
            if (value > 0) segments.push_back({label, value});
        }
        double thickness = std::max(4.0, s->geometry.w * 0.22);
        Rect ring{ s->geometry.x + thickness/2.0, s->geometry.y + thickness/2.0,
                   s->geometry.w - thickness, s->geometry.h - thickness };
        double total = 0; for (auto& seg : segments) total += seg.second;
        if (total <= 0) {
            // No data= (or all-zero) -> an empty track, same "draw the box, not a guess" honesty
            // Skeleton/Progress-with-no-value already follow.
            list.push_back({DrawOp::STROKE_ARC, ring, th.border, "", s->id, 0, thickness, 0, 360});
        } else {
            auto colorList = splitCsv(s->attrStr("colors", ""));
            double cursor = -90; // 12 o'clock start, same convention a real chart library uses
            for (size_t i = 0; i < segments.size(); ++i) {
                double sweep = segments[i].second / total * 360.0;
                Color c = (i < colorList.size()) ? parseHexColor(colorList[i], kPalette[i % 6])
                                                  : kPalette[i % 6];
                list.push_back({DrawOp::STROKE_ARC, ring, c, "", s->id, 0, thickness, cursor, sweep});
                cursor += sweep;
            }
        }
        std::string center = s->attrStr("centerLabel", "");
        if (center.empty() && total > 0) {
            std::ostringstream oss; oss << (long long)std::llround(total);
            center = oss.str();
        }
        if (!center.empty()) {
            Rect hole{ s->geometry.x, s->geometry.y, s->geometry.w, s->geometry.h };
            list.push_back({DrawOp::TEXT_RUN, hole, th.text, center, s->id, 0, 0});
        }
    }

    // StepItem: circular marker (state= "done"/"active"/"upcoming", index= 1-based -- both
    // injected by applyStepsConveniences() in rin_indsin_components_ext.h from the parent Steps'
    // current= attribute) centered above its label=. "done" fills solid with a checkmark, "active"
    // is a thicker outlined ring with its step number, "upcoming" is a muted thin ring.
    void paintStepItem(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        std::string state = s->attrStr("state", "upcoming");
        double circle = std::min(s->attrNum("markerSize", 28), std::min(s->geometry.w, s->geometry.h));
        Rect marker{ s->geometry.x + (s->geometry.w - circle)/2.0, s->geometry.y, circle, circle };
        Color tone = resolveColor(s);
        if (state == "done") {
            list.push_back({DrawOp::FILL_RECT, marker, tone, "", s->id, circle/2.0, 0});
            list.push_back({DrawOp::TEXT_RUN, marker, th.background, "\xE2\x9C\x93", s->id, 0, 0}); // "✓"
        } else if (state == "active") {
            list.push_back({DrawOp::FILL_RECT, marker, th.background, "", s->id, circle/2.0, 0});
            list.push_back({DrawOp::STROKE_RECT, marker, tone, "", s->id, circle/2.0, 2.5});
            list.push_back({DrawOp::TEXT_RUN, marker, tone, s->attrStr("index", ""), s->id, 0, 0});
        } else {
            list.push_back({DrawOp::FILL_RECT, marker, th.background, "", s->id, circle/2.0, 0});
            list.push_back({DrawOp::STROKE_RECT, marker, th.border, "", s->id, circle/2.0, 1.5});
            list.push_back({DrawOp::TEXT_RUN, marker, th.text_muted, s->attrStr("index", ""), s->id, 0, 0});
        }
        Rect labelRect{ s->geometry.x, marker.y + circle + 4, s->geometry.w, s->geometry.h - circle - 4 };
        list.push_back({DrawOp::TEXT_RUN, labelRect, (state == "upcoming") ? th.text_muted : th.text,
                         s->attrStr("label", ""), s->id, 0, 0});
    }
    // Steps' own connector: a thin line spanning from the first StepItem's marker center to the
    // last one's, painted BEFORE the StepItem children (see the StrandKind::STEPS branch in
    // paintInto above) so every circle draws on top of it, exactly like a real stepper's
    // background track. "done" segments (both endpoints already completed) draw in tone=;
    // everything else stays in the Theme's border color.
    void paintStepsConnector(const StrandPtr& s, DrawList& list) {
        if (s->children.size() < 2) return;
        const Theme& th = themeRegistry().active();
        double circle = std::min(s->children.front()->attrNum("markerSize", 28),
                                  std::min(s->children.front()->geometry.w, s->children.front()->geometry.h));
        double lineY = s->children.front()->geometry.y + circle/2.0 - 1.0;
        for (size_t i = 0; i + 1 < s->children.size(); ++i) {
            auto& a = s->children[i]; auto& b = s->children[i+1];
            double ax = a->geometry.x + a->geometry.w/2.0, bx = b->geometry.x + b->geometry.w/2.0;
            bool segmentDone = a->attrStr("state", "") == "done";
            Rect seg{ ax, lineY, bx - ax, 2.0 };
            list.push_back({DrawOp::FILL_RECT, seg, segmentDone ? resolveColor(a) : th.border, "", s->id, 1.0, 0});
        }
    }

    // TimelineItem: a small dot at the left edge, then date=/title=/desc= stacked to its right —
    // an attribute-driven leaf (see measureTimelineItem's doc comment), so all three lines are
    // painted directly here rather than through real child Text Strands.
    void paintTimelineItem(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        double dot = 12;
        Rect dotRect{ s->geometry.x, s->geometry.y + 2, dot, dot };
        Color tone = resolveColor(s);
        list.push_back({DrawOp::FILL_RECT, dotRect, tone, "", s->id, dot/2.0, 0});

        double textX = s->geometry.x + dot + 12;
        double textW = std::max(0.0, s->geometry.w - dot - 12);
        double fontSize = resolveFontSize(*s, "size", 14);
        double lineH = fontSize*1.35;
        double y = s->geometry.y;
        std::string date = s->attrStr("date", "");
        if (!date.empty()) {
            list.push_back({DrawOp::TEXT_RUN, {textX, y, textW, lineH}, th.text_muted, date, s->id, 0, 0});
            y += lineH;
        }
        list.push_back({DrawOp::TEXT_RUN, {textX, y, textW, lineH}, th.text, s->attrStr("title", ""), s->id, 0, 0});
        y += lineH;
        std::string desc = s->attrStr("desc", "");
        if (!desc.empty())
            list.push_back({DrawOp::TEXT_RUN, {textX, y, textW, lineH}, th.text_muted, desc, s->id, 0, 0});
    }
    // Timeline's own connector: one continuous vertical line behind every TimelineItem's dot
    // (first dot's center to last dot's center), painted before the children for the same
    // "background track under the markers" reason paintStepsConnector uses above.
    void paintTimelineConnector(const StrandPtr& s, DrawList& list) {
        if (s->children.size() < 2) return;
        const Theme& th = themeRegistry().active();
        double dot = 12;
        double lineX = s->children.front()->geometry.x + dot/2.0 - 1.0;
        double top = s->children.front()->geometry.y + 2 + dot/2.0;
        double bottom = s->children.back()->geometry.y + 2 + dot/2.0;
        Rect line{ lineX, top, 2.0, bottom - top };
        list.push_back({DrawOp::FILL_RECT, line, th.border, "", s->id, 1.0, 0});
    }

    void paintProgress(const StrandPtr& s, DrawList& list, double radius) {
        const Theme& th = themeRegistry().active();
        double pillRadius = std::min(radius, s->geometry.h / 2.0);
        list.push_back({DrawOp::FILL_RECT, s->geometry, th.border, "", s->id, pillRadius, 0}); // track
        double value = std::max(0.0, std::min(100.0, s->attrNum("value", 0)));
        Rect fill = s->geometry; fill.w = s->geometry.w * (value / 100.0);
        Color tone = resolveColor(s);
        list.push_back({DrawOp::FILL_RECT, fill, tone, "", s->id, pillRadius, 0}); // filled portion
    }
    void paintCheckbox(const StrandPtr& s, DrawList& list, double radius) {
        const Theme& th = themeRegistry().active();
        Color tone = resolveColor(s);
        bool checked = s->attrStr("checked", "false") == "true";
        double soft = std::min(radius > 0 ? radius : 4.0, s->geometry.h / 2.0);
        if (checked) {
            list.push_back({DrawOp::FILL_RECT, s->geometry, tone, "", s->id, soft, 0});
        } else {
            list.push_back({DrawOp::FILL_RECT, s->geometry, th.background, "", s->id, soft, 0});
            list.push_back({DrawOp::STROKE_RECT, s->geometry, th.border, "", s->id, soft, 1.5});
        }
    }
    // Ready-elements expansion: Radio (a ring that gets an inner filled dot when checked=true,
    // the standard native radio-button look — deliberately not just paintCheckbox with a round
    // radius, since a checkbox stays filled-square when checked while a radio's outer ring never
    // fills) and Slider (Progress's own track+fill, plus a round thumb drawn at the current value's
    // position so it reads as draggable rather than as a plain progress bar).
    void paintRadio(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        Color tone = resolveColor(s);
        bool checked = s->attrStr("checked", "false") == "true";
        double circleRadius = std::min(s->geometry.w, s->geometry.h) / 2.0;
        list.push_back({DrawOp::FILL_RECT, s->geometry, th.background, "", s->id, circleRadius, 0});
        list.push_back({DrawOp::STROKE_RECT, s->geometry, checked ? tone : th.border, "", s->id, circleRadius, checked ? 2.0 : 1.5});
        if (checked) {
            double inset = std::max(4.0, std::min(s->geometry.w, s->geometry.h) * 0.28);
            Rect dot{ s->geometry.x + inset, s->geometry.y + inset, s->geometry.w - inset*2, s->geometry.h - inset*2 };
            list.push_back({DrawOp::FILL_RECT, dot, tone, "", s->id, std::min(dot.w,dot.h)/2.0, 0});
        }
    }
    void paintSlider(const StrandPtr& s, DrawList& list, double radius) {
        const Theme& th = themeRegistry().active();
        double trackRadius = std::min(radius, s->geometry.h / 2.0);
        list.push_back({DrawOp::FILL_RECT, s->geometry, th.border, "", s->id, trackRadius, 0}); // track
        double minV = s->attrNum("min", 0), maxV = s->attrNum("max", 100);
        double value = std::max(minV, std::min(maxV, s->attrNum("value", minV)));
        double frac = (maxV > minV) ? (value - minV) / (maxV - minV) : 0.0;
        Rect fill = s->geometry; fill.w = s->geometry.w * frac;
        Color tone = resolveColor(s);
        list.push_back({DrawOp::FILL_RECT, fill, tone, "", s->id, trackRadius, 0}); // filled portion
        double thumbSize = std::min(s->geometry.h * 1.6, s->geometry.h + 6);
        Rect thumb{ s->geometry.x + fill.w - thumbSize/2.0, s->geometry.y + (s->geometry.h - thumbSize)/2.0, thumbSize, thumbSize };
        list.push_back({DrawOp::FILL_RECT, thumb, tone, "", s->id, thumbSize/2.0, 0});
    }
    void paintSwitch(const StrandPtr& s, DrawList& list) {
        const Theme& th = themeRegistry().active();
        Color tone = resolveColor(s);
        bool checked = s->attrStr("checked", "false") == "true";
        double trackRadius = s->geometry.h / 2.0;
        list.push_back({DrawOp::FILL_RECT, s->geometry, checked ? tone : th.border, "", s->id, trackRadius, 0});
        double thumbSize = s->geometry.h - 4;
        Rect thumb{ checked ? s->geometry.x + s->geometry.w - thumbSize - 2 : s->geometry.x + 2,
                    s->geometry.y + 2, thumbSize, thumbSize };
        list.push_back({DrawOp::FILL_RECT, thumb, th.background, "", s->id, thumbSize/2.0, 0});
    }
    void paintAvatar(const StrandPtr& s, DrawList& list, double /*radius*/) {
        double circleRadius = std::min(s->geometry.w, s->geometry.h) / 2.0;
        list.push_back({DrawOp::FILL_RECT, s->geometry, resolveColor(s), "", s->id, circleRadius, 0});
        std::string initials = s->attrStr("initials", "");
        if (!initials.empty())
            list.push_back({DrawOp::TEXT_RUN, s->geometry, themeRegistry().active().background, initials, s->id, 0, 0});
    }
    void paintField(const StrandPtr& s, DrawList& list, double radius) {
        const Theme& th = themeRegistry().active();
        StrandState state = resolveState(*s);
        Color border = (state == StrandState::FOCUSED) ? th.primary : th.border;
        list.push_back({DrawOp::FILL_RECT, s->geometry, th.surface, "", s->id, radius, 0});
        list.push_back({DrawOp::STROKE_RECT, s->geometry, border, "", s->id, radius, state == StrandState::FOCUSED ? 2.0 : 1.0});
        std::string value = s->attrStr("value", "");
        bool showingPlaceholder = value.empty();
        list.push_back({DrawOp::TEXT_RUN, s->geometry,
                         showingPlaceholder ? th.text_muted : th.text,
                         showingPlaceholder ? s->attrStr("placeholder", "") : value, s->id, 0, 0});
    }

    // Button Library (§4): renders the 6 treatments a `variant=` selects, each combined with
    // whatever `tone=` role and `state=`/`disabled=`/`loading=` says. This is genuinely not a
    // single "fill the rect" path anymore -- outline/ghost/link deliberately draw no filled
    // background at all, matching real native button variants rather than a CSS `background:
    // transparent` override bolted onto one shared box.
    void paintButton(const StrandPtr& s, DrawList& list, double radius) {
        Color tone = resolveColor(s);
        StrandState state = resolveState(*s);
        ButtonTreatment treatment = resolveButtonTreatment(*s);

        if (state == StrandState::DISABLED) tone = dimTowardBackground(tone, 0.55);
        else if (state == StrandState::LOADING) tone = dimTowardBackground(tone, 0.25);

        const Theme& th = themeRegistry().active();
        Color textColor = {245, 245, 250};   // legible on every FILL tone today (see paint.h note below)
        bool selected = (state == StrandState::SELECTED);

        switch (treatment) {
            case ButtonTreatment::FILL:
                list.push_back({DrawOp::FILL_RECT, s->geometry, tone, "", s->id, radius, 0});
                break;
            case ButtonTreatment::GRADIENT: {
                // No alpha/multi-stop primitive in this toy rasterizer yet (see architecture doc's
                // Dye backend note) -- approximated as a tone/secondary blend rather than a true
                // 2-stop gradient. A real GPU/Canvas backend reads variant="gradient" from the
                // JSON export directly and can render the real thing.
                auto mix = [](unsigned char a, unsigned char b) { return (unsigned char)((a + b) / 2); };
                Color blended = {mix(tone.r, th.secondary.r), mix(tone.g, th.secondary.g), mix(tone.b, th.secondary.b)};
                list.push_back({DrawOp::FILL_RECT, s->geometry, blended, "", s->id, radius, 0});
                break;
            }
            case ButtonTreatment::GLASS: {
                Color blended = dimTowardBackground(tone, 0.5); // translucency approximated as a lighter blend (no real alpha channel here)
                list.push_back({DrawOp::FILL_RECT, s->geometry, blended, "", s->id, radius, 0});
                textColor = tone;
                break;
            }
            case ButtonTreatment::OUTLINE:
                list.push_back({DrawOp::FILL_RECT, s->geometry, th.background, "", s->id, radius, 0});
                list.push_back({DrawOp::STROKE_RECT, s->geometry, tone, "", s->id, radius, selected ? 3.0 : 1.5});
                textColor = tone;
                break;
            case ButtonTreatment::GHOST:
                if (selected) list.push_back({DrawOp::FILL_RECT, s->geometry, dimTowardBackground(tone, 0.85), "", s->id, radius, 0});
                textColor = tone;
                break;
            case ButtonTreatment::LINK:
                textColor = tone; // no box at all, ever -- see docs/indsintime/RIN_INDSIN_TOKENS.md
                break;
        }
        { Color tc = textColor; if ((s->attr("bg") && resolveColorAttr(s, "color", tc)) || resolveColorAttr(s, "textColor", tc)) textColor = tc; }
        list.push_back({DrawOp::TEXT_RUN, s->geometry, textColor, buttonDisplayLabel(s), s->id, 0, 0});
    }

    // IconButton (§18/§2): prefixes the resolved icon glyph to whatever text paintButton would
    // otherwise draw (label=, possibly empty) -- one glyph + one space + label, or just the glyph
    // alone with no label=. Plain Button keeps drawing label= exactly as before (unaffected).
    std::string buttonDisplayLabel(const StrandPtr& s) const {
        std::string label = s->attrStr("label", "");
        if (label.empty()) label = s->attrStr("text", "");
        if (s->kind != StrandKind::ICONBUTTON) return label;
        std::string iconName = s->attrStr("icon", "");
        if (iconName.empty()) return label; // an IconButton with no icon= just behaves like Button
        std::string glyph = iconRegistry().resolve(iconName);
        return label.empty() ? glyph : (glyph + " " + label);
    }

    // Icon (§18/§2): a small standalone glyph leaf -- resolveColor() already covers ICON via the
    // default-fallback branch below, so tone=/color= work on it exactly like every other kind.
    void paintIcon(const StrandPtr& s, DrawList& list) {
        std::string iconName = s->attrStr("icon", "");
        std::string glyph = iconRegistry().resolve(iconName);
        list.push_back({DrawOp::TEXT_RUN, s->geometry, resolveColor(s), glyph, s->id, 0, 0});
    }
};

// Rasterizes a DrawList into a flat RGB888 pixel buffer (row-major, top-to-bottom). This is the
// ONE rasterizer in the engine (§21/§36: "لا تنشئ Renderer منفصلاً يكرر منطق Indsin") -- both
// rasterizeToPPM() and writePNG()/exportFabricToPNG() below consume this exact buffer, so a PPM
// and a PNG of the same Fabric are always pixel-identical; PNG is purely an encoding added on top.
inline std::vector<unsigned char> rasterizeToBuffer(const DrawList& list, int W, int H) {
    std::vector<unsigned char> buf((size_t)W*H*3, 18);
    if (W <= 0 || H <= 0) return buf;
    // Coverage-based compositing: `cov` (0..1, from anti-aliasing) times the color's own alpha,
    // blended through rincolor::alphaBlend so opacity=/rgba()/scrims compose exactly as before.
    auto plot = [&](int x, int y, Color c, double cov) {
        if (x<0||y<0||x>=W||y>=H || cov <= 0.0 || c.a == 0) return;
        size_t i=((size_t)y*W+x)*3;
        double a = cov * (c.a / 255.0);
        if (a >= 0.999) { buf[i]=c.r; buf[i+1]=c.g; buf[i+2]=c.b; return; }
        buf[i]   = (unsigned char)(buf[i]  *(1-a) + c.r*a + 0.5);
        buf[i+1] = (unsigned char)(buf[i+1]*(1-a) + c.g*a + 0.5);
        buf[i+2] = (unsigned char)(buf[i+2]*(1-a) + c.b*a + 0.5);
    };
    auto clamp01 = [](double v){ return v < 0 ? 0.0 : (v > 1 ? 1.0 : v); };
    // Signed distance (px, negative inside) from a rounded rect centered (cx,cy) with half-size (hw,hh).
    auto sdRound = [](double px, double py, double cx, double cy, double hw, double hh, double r) {
        double dx = std::max(std::fabs(px-cx) - (hw - r), 0.0), dy = std::max(std::fabs(py-cy) - (hh - r), 0.0);
        double inside = std::min(std::max(std::fabs(px-cx) - (hw - r), std::fabs(py-cy) - (hh - r)), 0.0);
        return std::sqrt(dx*dx + dy*dy) + inside - r;
    };
    auto fillRound = [&](const Rect& b, double radius, Color c) {
        double x0=b.x, y0=b.y, x1=b.x+b.w, y1=b.y+b.h;
        if (radius <= 0.5) { // square corners: snap to whole pixels so neighbours never show seams
            x0=std::round(x0); y0=std::round(y0); x1=std::round(x1); y1=std::round(y1);
            for (int y=(int)y0;y<(int)y1;y++) for (int x=(int)x0;x<(int)x1;x++) plot(x,y,c,1.0);
            return;
        }
        double cx=(x0+x1)/2, cy=(y0+y1)/2, hw=(x1-x0)/2, hh=(y1-y0)/2, r=std::min(radius, std::min(hw,hh));
        for (int y=(int)std::floor(y0);y<(int)std::ceil(y1);y++) for (int x=(int)std::floor(x0);x<(int)std::ceil(x1);x++)
            plot(x,y,c,clamp01(0.5 - sdRound(x+0.5,y+0.5,cx,cy,hw,hh,r)));
    };
    auto strokeRound = [&](const Rect& b, double radius, double t, Color c) {
        double x0=b.x, y0=b.y, x1=b.x+b.w, y1=b.y+b.h; t = std::max(1.0, t);
        double cx=(x0+x1)/2, cy=(y0+y1)/2, hw=(x1-x0)/2, hh=(y1-y0)/2, r=std::min(radius, std::min(hw,hh));
        for (int y=(int)std::floor(y0);y<(int)std::ceil(y1);y++) for (int x=(int)std::floor(x0);x<(int)std::ceil(x1);x++) {
            double d = sdRound(x+0.5,y+0.5,cx,cy,hw,hh,r);
            plot(x,y,c,clamp01(0.5 - d) - clamp01(0.5 - (d + t)));
        }
    };
    auto arc = [&](const DrawCommand& cmd) {
        double cx=cmd.bounds.x+cmd.bounds.w/2, cy=cmd.bounds.y+cmd.bounds.h/2, sw=std::max(1.0,cmd.strokeWidth);
        double R = std::min(cmd.bounds.w, cmd.bounds.h)/2 - sw/2;
        if (R <= 0) return;
        for (int y=(int)std::floor(cmd.bounds.y);y<(int)std::ceil(cmd.bounds.y+cmd.bounds.h);y++)
            for (int x=(int)std::floor(cmd.bounds.x);x<(int)std::ceil(cmd.bounds.x+cmd.bounds.w);x++) {
                double dx=x+0.5-cx, dy=y+0.5-cy, dist=std::sqrt(dx*dx+dy*dy);
                double cov = clamp01(sw/2 + 0.5 - std::fabs(dist - R));
                if (cov <= 0) continue;
                if (cmd.sweepAngleDeg < 359.9) {
                    double ang = std::atan2(dy,dx)*180.0/3.14159265358979; // 0 = 3 o'clock, clockwise (y is down)
                    double rel = std::fmod(ang - cmd.startAngleDeg + 720.0, 360.0);
                    if (rel > cmd.sweepAngleDeg) continue;
                }
                plot(x,y,cmd.color,cov);
            }
    };
    // ---- text ------------------------------------------------------------------------------------
    auto asciiFor = [](uint32_t cp) -> int {
        if (cp >= 0x20 && cp <= 0x7E) return (int)cp;
        switch (cp) {
            case 0xD7: return 'x'; case 0x2013: case 0x2014: case 0x2212: return '-'; case 0x2190: return '<'; case 0x2192: return '>';
            case 0x2605: case 0x2606: case 0x2022: return '*'; case 0x2713: return 'v'; case 0xF7: return '/';
            case 0x2018: case 0x2019: return '\''; case 0x201C: case 0x201D: return '"'; case 0xA0: return ' ';
        }
        return -1;
    };
    auto drawGlyph = [&](int ascii, double gx, double gy, double unit, Color c, bool bold) {
        const uint8_t* g = font::glyph5x7((unsigned char)ascii);
        if (!g) return;
        int px0=(int)std::floor(gx), px1=(int)std::ceil(gx + 5*unit + (bold?unit*0.5:0)), py0=(int)std::floor(gy), py1=(int)std::ceil(gy + 7*unit);
        for (int y=py0;y<py1;y++) for (int x=px0;x<px1;x++) {
            int hit=0;
            for (int sy=0;sy<4;sy++) for (int sx=0;sx<4;sx++) {
                double u=((x+(sx+0.5)/4.0)-gx)/unit, v=((y+(sy+0.5)/4.0)-gy)/unit;
                for (int pass=0; pass<(bold?2:1); pass++) {
                    double uu = u - pass*0.5;
                    int col=(int)std::floor(uu), row=(int)std::floor(v);
                    if (col>=0 && col<5 && row>=0 && row<7 && ((g[col]>>row)&1)) { hit++; break; }
                }
            }
            if (hit) plot(x,y,c,hit/16.0);
        }
    };
    auto drawText = [&](const DrawCommand& cmd) {
        if (cmd.text.empty() || cmd.bounds.w <= 0 || cmd.bounds.h <= 0) return;
        double fs = cmd.fontSize > 0 ? cmd.fontSize : std::min(28.0, std::max(8.0, cmd.bounds.h * 0.35));
        double unit = font::unitFor(fs), adv = font::advanceFor(fs), lineH = fs * 1.4, pad = (cmd.align == 1 ? 4.0 : 1.0);
        double avail = std::max(4.0, cmd.bounds.w - pad*2);
        bool rtl = font::isRtlText(cmd.text);
        auto cps = font::decodeUtf8(cmd.text);
        // Split into words (on space / newline) and greedily wrap by the real advance.
        std::vector<std::vector<uint32_t>> lines; std::vector<uint32_t> cur;
        auto width = [&](size_t n){ return n * adv; };
        size_t maxLines = cmd.singleLine ? 1 : std::max<size_t>(1, (size_t)(cmd.bounds.h / lineH + 0.0001));
        std::vector<uint32_t> word;
        auto flushWord = [&]() {
            if (word.empty()) return;
            size_t need = cur.empty() ? word.size() : cur.size() + 1 + word.size();
            if (!cmd.singleLine && !cur.empty() && width(need) > avail) { lines.push_back(cur); cur.clear(); }
            if (!cur.empty()) cur.push_back(' ');
            cur.insert(cur.end(), word.begin(), word.end()); word.clear();
        };
        for (uint32_t cp : cps) {
            if (cp == '\n') { flushWord(); lines.push_back(cur); cur.clear(); }
            else if (cp == ' ') flushWord();
            else word.push_back(cp);
        }
        flushWord(); lines.push_back(cur);
        if (lines.size() > maxLines) { lines.resize(maxLines); lines.back().push_back(0x2026); }
        for (size_t li=0; li<lines.size(); li++) {
            auto& ln = lines[li];
            if (width(ln.size()) > avail) { // ellipsize with "..." so long single lines don't spill
                size_t keep = (size_t)std::max(0.0, std::floor(avail/adv) - 3);
                if (keep < ln.size()) { ln.resize(keep); ln.push_back('.'); ln.push_back('.'); ln.push_back('.'); }
            }
            double w = width(ln.size());
            double x = cmd.bounds.x + pad;
            int al = cmd.align == 0 && rtl ? 2 : cmd.align;
            if (al == 1) x = cmd.bounds.x + (cmd.bounds.w - w)/2; else if (al == 2) x = cmd.bounds.x + cmd.bounds.w - pad - w;
            double blockH = cmd.singleLine ? lineH : lineH * lines.size();
            double top = cmd.singleLine ? cmd.bounds.y + (cmd.bounds.h - lineH)/2 : cmd.bounds.y;
            (void)blockH;
            double gy = top + li*lineH + (lineH - 7*unit)/2;
            for (size_t k=0;k<ln.size();k++) {
                uint32_t cp = ln[k]; int a = asciiFor(cp);
                double gx = x + k*adv;
                if (cp == 0x2026) { for (int d=0;d<3;d++) drawGlyph('.', gx + d*adv, gy, unit, cmd.color, false); continue; }
                if (a > 0) drawGlyph(a, gx, gy, unit, cmd.color, cmd.bold);
                else { // no embedded glyph (Arabic/CJK/emoji...): soft "skeleton" bar so layout stays readable
                    Rect bar{gx + adv*0.12, gy + 7*unit*0.38, adv*0.76, std::max(1.5, 7*unit*0.3)};
                    Color sc = cmd.color; sc.a = (unsigned char)(sc.a * 0.55);
                    fillRound(bar, bar.h/2, sc);
                }
            }
        }
    };
    for (auto& cmd : list) {
        switch (cmd.op) {
            case DrawOp::FILL_RECT: case DrawOp::SCRIM_RECT: fillRound(cmd.bounds, cmd.radius, cmd.color); break;
            case DrawOp::STROKE_RECT: strokeRound(cmd.bounds, cmd.radius, cmd.strokeWidth, cmd.color); break;
            case DrawOp::STROKE_ARC: arc(cmd); break;
            case DrawOp::TEXT_RUN: drawText(cmd); break;
        }
    }
    return buf;
}

inline void rasterizeToPPM(const DrawList& list, int W, int H, const std::string& path) {
    auto buf = rasterizeToBuffer(list, W, H);
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    f.write((char*)buf.data(), buf.size());
}

// ---- Minimal, dependency-free (beyond zlib, already linked for the diagnostics gzip path) real
// PNG encoder: standard PNG CRC32 + zlib deflate for IDAT. No stb_image/libpng -- this is the
// smallest correct implementation of the PNG spec (signature, IHDR, one IDAT, IEND), not a fake
// "renamed PPM" (see §40: "تستخدم حلولاً وهمية للتنزيل أو PNG" is explicitly forbidden).
namespace png_detail {
inline uint32_t crc32(const unsigned char* buf, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
inline void putBE32(std::vector<unsigned char>& out, uint32_t v) {
    out.push_back((unsigned char)((v >> 24) & 0xff));
    out.push_back((unsigned char)((v >> 16) & 0xff));
    out.push_back((unsigned char)((v >> 8) & 0xff));
    out.push_back((unsigned char)(v & 0xff));
}
inline void writeChunk(std::ostream& f, const char* type, const unsigned char* data, size_t len) {
    std::vector<unsigned char> lenBuf; putBE32(lenBuf, (uint32_t)len);
    f.write((char*)lenBuf.data(), 4);
    std::vector<unsigned char> crcInput(4 + len);
    memcpy(crcInput.data(), type, 4);
    if (len) memcpy(crcInput.data() + 4, data, len);
    f.write((char*)crcInput.data(), 4 + (std::streamsize)len);
    std::vector<unsigned char> crcBuf; putBE32(crcBuf, crc32(crcInput.data(), crcInput.size()));
    f.write((char*)crcBuf.data(), 4);
}
} // namespace png_detail

// Encodes an RGB888 buffer (as produced by rasterizeToBuffer) as a real, spec-valid PNG file.
// Returns false (never throws, never crashes -- §28) if the file can't be opened or W/H are
// non-positive; callers surface that as a normal [Exxxx] Snag rather than letting it propagate.
inline bool writePNG(const std::string& path, int W, int H, const std::vector<unsigned char>& rgb) {
    if (W <= 0 || H <= 0) return false;
    if (rgb.size() < (size_t)W * H * 3) return false;

    // Build the unfiltered scanline stream: one filter-type byte (0 = None) per row + RGB bytes.
    std::vector<unsigned char> raw;
    raw.reserve((size_t)H * (1 + (size_t)W * 3));
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        const unsigned char* row = rgb.data() + (size_t)y * W * 3;
        raw.insert(raw.end(), row, row + (size_t)W * 3);
    }

    uLongf boundLen = compressBound((uLong)raw.size());
    std::vector<unsigned char> compressed(boundLen);
    if (compress2(compressed.data(), &boundLen, raw.data(), (uLong)raw.size(), 6) != Z_OK) return false;
    compressed.resize(boundLen);

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const unsigned char sig[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    f.write((char*)sig, 8);

    unsigned char ihdr[13];
    ihdr[0]=(W>>24)&0xff; ihdr[1]=(W>>16)&0xff; ihdr[2]=(W>>8)&0xff; ihdr[3]=W&0xff;
    ihdr[4]=(H>>24)&0xff; ihdr[5]=(H>>16)&0xff; ihdr[6]=(H>>8)&0xff; ihdr[7]=H&0xff;
    ihdr[8]=8;   // bit depth
    ihdr[9]=2;   // color type: truecolor (RGB)
    ihdr[10]=0; ihdr[11]=0; ihdr[12]=0; // compression, filter, interlace
    png_detail::writeChunk(f, "IHDR", ihdr, 13);
    png_detail::writeChunk(f, "IDAT", compressed.data(), compressed.size());
    png_detail::writeChunk(f, "IEND", nullptr, 0);
    return f.good();
}

// §22/§23: screenshot the whole Fabric, or export any subtree (a specific Container by Strand)
// as its own PNG -- both walk through the *same* Dye::paint() as normal rendering, they don't
// re-implement painting. For a subtree, geometry is in root-relative coordinates, so every draw
// command is translated by the subtree's own top-left before rasterizing into a buffer sized to
// exactly that subtree's bounds -- e.g. exportPNG(profile, "profile.png") is not a full-screen
// image with the rest blanked out, it is a WxH image that IS the profile Container.
inline bool exportFabricToPNG(Dye& dye, const StrandPtr& subtreeRoot, const std::string& path) {
    if (!subtreeRoot) return false;
    int W = (int)std::ceil(subtreeRoot->geometry.w);
    int H = (int)std::ceil(subtreeRoot->geometry.h);
    if (W <= 0 || H <= 0) return false;
    auto drawList = dye.paint(subtreeRoot);
    double ox = subtreeRoot->geometry.x, oy = subtreeRoot->geometry.y;
    if (ox != 0.0 || oy != 0.0) {
        for (auto& cmd : drawList) { cmd.bounds.x -= ox; cmd.bounds.y -= oy; }
    }
    auto buf = rasterizeToBuffer(drawList, W, H);
    return writePNG(path, W, H, buf);
}

// JSON serialization of the full Fabric (geometry + resolved attrs) — this is what crosses the
// C API / JNI boundary to a Kotlin Canvas client (see rin_indsin_c_api.cpp / jni_bridge.cpp).
inline std::string jsonEscape(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (c=='"'||c=='\\') { out+='\\'; out+=c; }
        else if (c=='\n') out += "\\n";
        else out += c;
    }
    return out;
}
inline void fabricToJson(const StrandPtr& s, std::ostringstream& os) {
    Color resolvedFill = resolveColor(s); // computed once, reused for both JSON fields below
    os << "{\"kind\":\"" << strandKindName(s->kind) << "\",\"name\":\"" << jsonEscape(s->name) << "\""
       << ",\"role\":\"" << uiRoleName(s->role) << "\""
       << ",\"sourceTag\":\"" << jsonEscape(s->sourceTag.empty() ? strandKindName(s->kind) : s->sourceTag) << "\""
       << ",\"line\":" << s->sourceLine
       << ",\"x\":" << s->geometry.x << ",\"y\":" << s->geometry.y
       << ",\"w\":" << s->geometry.w << ",\"h\":" << s->geometry.h
       // The engine's own resolved paint color for this Strand -- tone=/color=<role>/the active
       // @theme= already baked in, exactly as Dye::paintInto() would paint it. See colorToHex()
       // above for why this is needed at all. resolvedAlpha (0..1) is a separate field rather
       // than folded into an 8-digit hex so old clients reading resolvedColor as a plain
       // "#RRGGBB" (IndsinFabricView.kt's original parseColor, still used elsewhere) keep working
       // unchanged -- only a client that opts into reading resolvedAlpha sees any transparency.
       << ",\"resolvedColor\":\"" << colorToHex(resolvedFill) << "\""
       << ",\"resolvedAlpha\":" << colorAlphaUnit(resolvedFill)
       << ",\"resolvedBorderColor\":\"" << colorToHex(resolveBorderColor(s)) << "\""
       << ",\"attrs\":{";
    for (size_t i=0;i<s->attrs.size();i++) {
        if (i) os << ",";
        os << "\"" << jsonEscape(s->attrs[i].key) << "\":\"" << jsonEscape(s->attrs[i].value.asString()) << "\"";
    }
    // Additive a11y block (§20): role + accessible name + interaction state, computed rather
    // than requiring the .rin source to declare every one of these on every Strand. A client
    // renderer that doesn't know this key yet can ignore it safely -- nothing above changed.
    os << "},\"a11y\":{\"role\":\"" << jsonEscape(accessibleRole(s->kind)) << "\""
       << ",\"name\":\"" << jsonEscape(accessibleName(*s)) << "\""
       << ",\"state\":\"" << stateName(resolveState(*s)) << "\""
       << ",\"disabled\":" << (resolveState(*s) == StrandState::DISABLED ? "true" : "false")
       << "},\"interaction\":{\"clickable\":"
       << ((s->attr("onTap") || (s->kind == StrandKind::LINK && !s->attrStr("href", "").empty())) ? "true" : "false")
       << ",\"onTap\":\"" << jsonEscape(s->attr("onTap") ? s->attr("onTap")->asString() : "") << "\""
       << ",\"href\":\"" << jsonEscape(s->attrStr("href", "")) << "\""
       // Events (spec §events): same "is there a handler / what is it" shape as onTap/clickable
       // above, one pair per gesture -- lets a client (the Inspector panel, an a11y reader, a
       // future codegen pass) discover onLongPress=/onDoubleTap=/onHoverEnter=/onHoverExit=
       // without re-walking s->attrs itself. Purely additive: a client that only reads
       // "clickable"/"onTap" (every existing consumer) is unaffected.
       << ",\"longPressable\":" << (s->attr("onLongPress") ? "true" : "false")
       << ",\"onLongPress\":\"" << jsonEscape(s->attr("onLongPress") ? s->attr("onLongPress")->asString() : "") << "\""
       << ",\"doubleTappable\":" << (s->attr("onDoubleTap") ? "true" : "false")
       << ",\"onDoubleTap\":\"" << jsonEscape(s->attr("onDoubleTap") ? s->attr("onDoubleTap")->asString() : "") << "\""
       << ",\"hoverable\":" << ((s->attr("onHoverEnter") || s->attr("onHoverExit")) ? "true" : "false")
       << ",\"onHoverEnter\":\"" << jsonEscape(s->attr("onHoverEnter") ? s->attr("onHoverEnter")->asString() : "") << "\""
       << ",\"onHoverExit\":\"" << jsonEscape(s->attr("onHoverExit") ? s->attr("onHoverExit")->asString() : "") << "\""
       << "},\"children\":[";
    for (size_t i=0;i<s->children.size();i++) { if (i) os << ","; fabricToJson(s->children[i], os); }
    os << "]}";
}
inline const char* drawOpName(DrawOp op) {
    switch (op) {
        case DrawOp::FILL_RECT: return "fill_rect";
        case DrawOp::STROKE_RECT: return "stroke_rect";
        case DrawOp::TEXT_RUN: return "text";
        case DrawOp::SCRIM_RECT: return "scrim";
        case DrawOp::STROKE_ARC: return "stroke_arc";
        default: return "unknown";
    }
}

// Serializes the exact Dye output, not a second approximation made by the host UI. This makes
// Indsin's native renderer output inspectable and gives every host backend the same geometry/color/
// text primitives. Kotlin may still use its Canvas adapter, but it can now consume this as the
// canonical render plan when a pixel-identical/native path is desired.
inline std::string drawListToJsonString(const DrawList& list) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& d = list[i];
        if (i) os << ",";
        os << "{\"op\":\"" << drawOpName(d.op) << "\""
           << ",\"owner\":" << d.owner
           << ",\"x\":" << d.bounds.x << ",\"y\":" << d.bounds.y
           << ",\"w\":" << d.bounds.w << ",\"h\":" << d.bounds.h
           << ",\"color\":\"" << colorToHex(d.color) << "\""
           << ",\"alpha\":" << colorAlphaUnit(d.color)
           << ",\"radius\":" << d.radius
           << ",\"strokeWidth\":" << d.strokeWidth;
        if (d.op == DrawOp::STROKE_ARC) os << ",\"start\":" << d.startAngleDeg << ",\"sweep\":" << d.sweepAngleDeg;
        if (d.op == DrawOp::TEXT_RUN) os << ",\"fontSize\":" << d.fontSize << ",\"align\":" << d.align << ",\"bold\":" << (d.bold?"true":"false") << ",\"singleLine\":" << (d.singleLine?"true":"false");
        if (!d.text.empty()) os << ",\"text\":\"" << jsonEscape(d.text) << "\"";
        os << "}";
    }
    os << "]";
    return os.str();
}

inline std::string fabricToJsonString(const StrandPtr& s) {
    std::ostringstream os; fabricToJson(s, os); return os.str();
}

} // namespace indsin
