// loom/rin_loom_paint.h — Dye: paint engine (Strand geometry -> DrawList -> raster/JSON).
#pragma once
#include "rin_loom_strand.h"
#include "rin_loom_tokens.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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

enum class DrawOp { FILL_RECT, STROKE_RECT, TEXT_RUN };
struct DrawCommand { DrawOp op; Rect bounds; Color color; std::string text; StrandId owner; double radius = 0; double strokeWidth = 0; };
using DrawList = std::vector<DrawCommand>;

struct Dye {
    DrawList paint(const StrandPtr& s) { DrawList list; paintInto(s, list); return list; }
    void paintInto(const StrandPtr& s, DrawList& list) {
        double r = std::min(resolveRadius(*s, 0), std::min(s->geometry.w, s->geometry.h) / 2.0);

        if (s->kind == StrandKind::BUTTON || s->kind == StrandKind::TABITEM) { paintButton(s, list, r); for (auto& c : s->children) paintInto(c, list); return; }

        // Missing-components pass: Spacer never draws anything (it's pure layout space) — same
        // "skip the generic FILL_RECT" carve-out TEXT already gets below, just with no TEXT_RUN
        // to replace it with either.
        if (s->kind == StrandKind::SPACER) { for (auto& c : s->children) paintInto(c, list); return; }

        if (s->kind == StrandKind::BADGE)  { paintBadge(s, list, r); return; } // no children — Badge is a leaf
        if (s->kind == StrandKind::PROGRESS) { paintProgress(s, list, r); return; }
        if (s->kind == StrandKind::CHECKBOX) { paintCheckbox(s, list, r); return; }
        if (s->kind == StrandKind::SWITCH)   { paintSwitch(s, list); return; }
        if (s->kind == StrandKind::AVATAR)   { paintAvatar(s, list, r); return; }
        if (s->kind == StrandKind::INPUT || s->kind == StrandKind::TEXTAREA) { paintField(s, list, r); return; }

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
        list.push_back({DrawOp::TEXT_RUN, s->geometry, textColor, s->attrStr("label"), s->id, 0, 0});
    }
};

inline void rasterizeToPPM(const DrawList& list, int W, int H, const std::string& path) {
    std::vector<unsigned char> buf(W*H*3, 18);
    auto setPx = [&](int x,int y, Color c){ if (x<0||y<0||x>=W||y>=H) return; int i=(y*W+x)*3; buf[i]=c.r; buf[i+1]=c.g; buf[i+2]=c.b; };
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
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    f.write((char*)buf.data(), buf.size());
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
