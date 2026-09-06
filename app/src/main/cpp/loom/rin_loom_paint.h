// loom/rin_loom_paint.h — Dye: paint engine (Strand geometry -> DrawList -> raster/JSON).
#pragma once
#include "rin_loom_strand.h"
#include "rin_loom_tokens.h"
#include "rin_loom_overlay.h" // OverlayLayer -- see paintWithOverlay() below
#include "rin_loom_icons.h"   // IconRegistry -- §18
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <zlib.h>

namespace loom {

// Color, parseHexColor, and the semantic Theme/Pattern-Book machinery now live in
// rin_loom_tokens.h (Rin Loom's Color Engine) — this file only resolves a Strand's final
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
// below) -- the Kotlin/Compose preview renderer (LoomFabricView.kt) has no access to the native
// Color Engine (tone=/color=<role> resolution, the active @theme=), so without this the preview
// can only ever understand a literal "color=\"#RRGGBB\"" written straight into the .rin source
// and otherwise falls back to its own hardcoded palette -- silently ignoring tone=, a semantic
// color=<role> name, and any non-default @theme= entirely. Emitting the already-resolved color
// here makes fabricToJson's output the same single source of truth for paint color that
// Dye::paintInto() already uses to rasterize the "real" (PNG-export) path.
inline std::string colorToHex(Color c) {
    static const char* digits = "0123456789ABCDEF";
    std::string out = "#";
    for (unsigned char v : {c.r, c.g, c.b}) {
        out += digits[(v >> 4) & 0xF];
        out += digits[v & 0xF];
    }
    return out;
}

// Resolves a Strand's paint color, in order:
//   1. tone="<role>"   — semantic Theme role (primary/success/danger/...), the Color Engine's
//                         intended everyday spelling — see spec §8/§9 ("Button { tone: primary; }").
//   2. type="<...>"    — Banner-only notification-kind shorthand (backward compatible).
//   3. color="#RRGGBB" — explicit hex, still supported for one-off overrides.
//   4. color="<role>"  — a semantic role name is *also* accepted through `color=`, so existing
//                         .rin sources that already use `color=` don't need to switch to `tone=`.
//   5. per-kind theme-based default (colorForKind / bannerTypeColor).
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

    if (auto tone = s->attr("tone")) {
        Color c;
        if (tone->kind == Value::Kind::STRING && resolveSemanticColor(tone->str, c)) return c;
    }
    if (auto c = s->attr("color")) {
        if (c->kind == Value::Kind::STRING && !c->str.empty()) {
            if (looksLikeHexColor(c->str)) return parseHexColor(c->str, fallback);
            Color sem;
            if (resolveSemanticColor(c->str, sem)) return sem;
        }
    }
    return fallback;
}

// SCRIM_RECT (Overlay Engine, rin_loom_overlay.h): visually a FILL_RECT, but distinguished so a
// renderer applies partial opacity regardless of the RGB it carries -- Color has no alpha channel
// (see the gradient note below: "no alpha/multi-stop primitive in this toy rasterizer yet"), so a
// real scrim's translucency is a renderer-side convention keyed on the op, the same way a real
// GPU/Canvas backend already has to special-case GRADIENT's variant= from the JSON export.
enum class DrawOp { FILL_RECT, STROKE_RECT, TEXT_RUN, SCRIM_RECT };
struct DrawCommand { DrawOp op; Rect bounds; Color color; std::string text; StrandId owner; double radius = 0; double strokeWidth = 0; };
using DrawList = std::vector<DrawCommand>;

// The scrim's RGB (a renderer applies its own opacity on top, per the SCRIM_RECT note above) —
// a fixed near-black rather than a Theme role, since a scrim dims *whatever theme is active*
// rather than participating in it the way primary/surface/etc. do.
inline Color scrimColor() { return {0, 0, 0}; }

struct Dye {
    DrawList paint(const StrandPtr& s) { DrawList list; paintInto(s, list); return list; }

    // Overlay Engine (rin_loom_overlay.h): paints the main document exactly as paint() above
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
    void paintInto(const StrandPtr& s, DrawList& list) {
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
                // Link's own box is already measured to fit its text (see rin_loom_layout.h's
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

        if (s->kind != StrandKind::TEXT)
            list.push_back({DrawOp::FILL_RECT, s->geometry, resolveColor(s), "", s->id, r, 0});
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
                textColor = tone; // no box at all, ever -- see docs/loomtime/RIN_LOOM_TOKENS.md
                break;
        }
        list.push_back({DrawOp::TEXT_RUN, s->geometry, textColor, buttonDisplayLabel(s), s->id, 0, 0});
    }

    // IconButton (§18/§2): prefixes the resolved icon glyph to whatever text paintButton would
    // otherwise draw (label=, possibly empty) -- one glyph + one space + label, or just the glyph
    // alone with no label=. Plain Button keeps drawing label= exactly as before (unaffected).
    std::string buttonDisplayLabel(const StrandPtr& s) const {
        std::string label = s->attrStr("label", "");
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
// ONE rasterizer in the engine (§21/§36: "لا تنشئ Renderer منفصلاً يكرر منطق Loom") -- both
// rasterizeToPPM() and writePNG()/exportFabricToPNG() below consume this exact buffer, so a PPM
// and a PNG of the same Fabric are always pixel-identical; PNG is purely an encoding added on top.
inline std::vector<unsigned char> rasterizeToBuffer(const DrawList& list, int W, int H) {
    std::vector<unsigned char> buf((size_t)W*H*3, 18);
    if (W <= 0 || H <= 0) return buf;
    auto setPx = [&](int x,int y, Color c){ if (x<0||y<0||x>=W||y>=H) return; size_t i=((size_t)y*W+x)*3; buf[i]=c.r; buf[i+1]=c.g; buf[i+2]=c.b; };
    for (auto& cmd : list) {
        if (cmd.op == DrawOp::FILL_RECT) {
            int x0=(int)cmd.bounds.x, y0=(int)cmd.bounds.y, x1=(int)(cmd.bounds.x+cmd.bounds.w), y1=(int)(cmd.bounds.y+cmd.bounds.h);
            for (int y=y0;y<y1;y++) for (int x=x0;x<x1;x++) setPx(x,y,cmd.color);
        } else if (cmd.op == DrawOp::STROKE_RECT) {
            int x0=(int)cmd.bounds.x, y0=(int)cmd.bounds.y, x1=(int)(cmd.bounds.x+cmd.bounds.w), y1=(int)(cmd.bounds.y+cmd.bounds.h);
            int t = std::max(1, (int)cmd.strokeWidth);
            for (int y=y0;y<y1;y++) for (int x=x0;x<x1;x++)
                if (x<x0+t || x>=x1-t || y<y0+t || y>=y1-t) setPx(x,y,cmd.color);
        } else {
            int x0=(int)cmd.bounds.x+4, y=(int)(cmd.bounds.y + cmd.bounds.h/2);
            for (size_t i=0;i<cmd.text.size();i++) for (int dx=0; dx<6; dx++) setPx(x0 + (int)i*8 + dx, y, cmd.color);
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
// C API / JNI boundary to a Kotlin Canvas client (see rin_loom_c_api.cpp / jni_bridge.cpp).
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
    os << "{\"kind\":\"" << strandKindName(s->kind) << "\",\"name\":\"" << jsonEscape(s->name) << "\""
       << ",\"line\":" << s->sourceLine
       << ",\"x\":" << s->geometry.x << ",\"y\":" << s->geometry.y
       << ",\"w\":" << s->geometry.w << ",\"h\":" << s->geometry.h
       // The engine's own resolved paint color for this Strand -- tone=/color=<role>/the active
       // @theme= already baked in, exactly as Dye::paintInto() would paint it. See colorToHex()
       // above for why this is needed at all.
       << ",\"resolvedColor\":\"" << colorToHex(resolveColor(s)) << "\""
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
       << "},\"children\":[";
    for (size_t i=0;i<s->children.size();i++) { if (i) os << ","; fabricToJson(s->children[i], os); }
    os << "]}";
}
inline std::string fabricToJsonString(const StrandPtr& s) {
    std::ostringstream os; fabricToJson(s, os); return os.str();
}

} // namespace loom
