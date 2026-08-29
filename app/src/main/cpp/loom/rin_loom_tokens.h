// loom/rin_loom_tokens.h — Design Tokens + Color Engine ("Dye" foundation) + Pattern Book
// (Theme Engine) for Rin Loom.
//
// This is the first slice of Rin Loom (see @theme in rin_ast.h / rin_parser.cpp, and the
// "Rin Loom" naming decided alongside the pre-existing Loomtime engine — Loom *is* Loomtime's
// design-system layer, not a parallel system). It is purely additive: nothing here changes the
// meaning of any existing token, attribute, or StrandKind. Two things are deliberately NOT CSS:
//   1. Colors are named by role (primary/success/danger/surface/text/...), resolved through an
//      active Theme (a "Pattern Book"), not looked up by selector/cascade.
//   2. Spacing/radius are named scale steps (compact/normal/comfortable/spacious,
//      sharp/soft/round/pill), not raw pixel literals sprinkled through source — though a raw
//      number is still accepted anywhere a token name is, for cases with no good semantic fit.
//
// Known, deliberate scope limit for this slice: the active Theme is process-wide state (a
// function-local static registry below), not threaded explicitly through every call in the
// existing Dye/Loom/Needle call chain. Rin Loom currently runs one Fabric per process (see
// rin_loom_pipeline.h / rin_loom_c_api.cpp), so this doesn't lose anything today; if/when a
// process ever hosts multiple concurrent Fabrics with different active themes, this should
// become an explicit field on PipelineResult threaded through Dye::paint(...) instead.
#pragma once
#include "rin_loom_eval.h"
#include <string>
#include <unordered_map>
#include <cmath>

namespace loom {

// ---- Color -----------------------------------------------------------------------------------
struct Color { unsigned char r = 0, g = 0, b = 0; };

inline Color parseHexColor(const std::string& hex, Color fallback) {
    if (hex.size() < 7 || hex[0] != '#') return fallback;
    auto hx = [&](int i) { return (unsigned char)std::stoul(hex.substr(i, 2), nullptr, 16); };
    try { return {hx(1), hx(3), hx(5)}; } catch (...) { return fallback; }
}
inline bool looksLikeHexColor(const std::string& s) { return s.size() >= 7 && s[0] == '#'; }

// ---- Theme (Pattern Book): semantic color slots -----------------------------------------------
// These are the ten roles §8/§9 of the spec asks for. A Strand never says "#6C5CE7"; it says
// tone="primary" (or color="primary" for the handful of Strand kinds that used a raw color=
// attribute before Loom existed — both are accepted so old .rin sources keep working).
struct Theme {
    std::string name;
    Color primary, secondary, success, danger, warning, info, neutral;
    Color surface, background, text, text_muted, border;

    const Color* slot(const std::string& roleName) const {
        static const std::unordered_map<std::string, const Color Theme::*> table = {
            {"primary", &Theme::primary}, {"secondary", &Theme::secondary},
            {"success", &Theme::success}, {"danger", &Theme::danger},
            {"warning", &Theme::warning}, {"info", &Theme::info},
            {"neutral", &Theme::neutral}, {"surface", &Theme::surface},
            {"background", &Theme::background}, {"text", &Theme::text},
            {"text_muted", &Theme::text_muted}, {"border", &Theme::border},
        };
        auto it = table.find(roleName);
        if (it == table.end()) return nullptr;
        return &(this->*(it->second));
    }
    void setSlot(const std::string& roleName, Color c) {
        static const std::unordered_map<std::string, Color Theme::*> table = {
            {"primary", &Theme::primary}, {"secondary", &Theme::secondary},
            {"success", &Theme::success}, {"danger", &Theme::danger},
            {"warning", &Theme::warning}, {"info", &Theme::info},
            {"neutral", &Theme::neutral}, {"surface", &Theme::surface},
            {"background", &Theme::background}, {"text", &Theme::text},
            {"text_muted", &Theme::text_muted}, {"border", &Theme::border},
        };
        auto it = table.find(roleName);
        if (it != table.end()) this->*(it->second) = c;
    }
};

inline Theme builtinDarkTheme() {
    Theme t; t.name = "Dark";
    t.primary   = {124, 92, 255}; t.secondary = {90, 200, 250};
    t.success   = {46, 160, 67};  t.danger    = {209, 69, 69};
    t.warning   = {212, 167, 44}; t.info      = {58, 110, 196};
    t.neutral   = {120, 120, 135};
    t.surface   = {40, 42, 54};   t.background= {24, 25, 32};
    t.text      = {230, 230, 240};t.text_muted= {139, 137, 156};
    t.border    = {51, 51, 63};
    return t;
}
inline Theme builtinLightTheme() {
    Theme t; t.name = "Light";
    t.primary   = {108, 78, 245}; t.secondary = {24, 144, 200};
    t.success   = {31, 128, 55};  t.danger     = {186, 46, 46};
    t.warning   = {166, 120, 15}; t.info       = {40, 90, 168};
    t.neutral   = {110, 110, 120};
    t.surface   = {255, 255, 255};t.background = {245, 245, 248};
    t.text      = {26, 26, 32};   t.text_muted = {110, 108, 122};
    t.border    = {224, 224, 232};
    return t;
}

// ---- Pattern Book: the registry of themes + which one is active -------------------------------
struct ThemeRegistry {
    std::unordered_map<std::string, Theme> themes;
    std::string activeName = "Dark";

    ThemeRegistry() {
        themes["Dark"] = builtinDarkTheme();
        themes["Light"] = builtinLightTheme();
    }
    void registerTheme(Theme t) { std::string n = t.name; themes[n] = std::move(t); }
    void setActive(const std::string& n) { if (themes.count(n)) activeName = n; }
    const Theme& active() const {
        auto it = themes.find(activeName);
        return it != themes.end() ? it->second : themes.at("Dark");
    }
};
// Process-wide Pattern Book (see the scope note at the top of this file).
inline ThemeRegistry& themeRegistry() { static ThemeRegistry reg; return reg; }

// Resolves a semantic color name ("primary", "danger", ...) against the active Theme.
// Returns false (leaving `out` untouched) if `name` isn't a known role, so callers can fall
// through to a hex parse or a per-kind default.
inline bool resolveSemanticColor(const std::string& name, Color& out) {
    const Color* slot = themeRegistry().active().slot(name);
    if (!slot) return false;
    out = *slot;
    return true;
}

// ---- Spacing tokens (§10/§11) ------------------------------------------------------------------
inline bool resolveSpacingToken(const std::string& name, double& out) {
    if (name == "compact")     { out = 8;  return true; }
    if (name == "normal")      { out = 12; return true; }
    if (name == "comfortable") { out = 16; return true; }
    if (name == "spacious")    { out = 24; return true; }
    return false;
}
// Reads a spacing-family attribute (padding/gap/...): numeric literal used as-is; string token
// name resolved via the spacing scale; anything else (or attribute absent) falls back to `def`.
inline double resolveSpacing(const Strand& s, const std::string& key, double def) {
    const Value* v = s.attr(key);
    if (!v) return def;
    if (v->kind == Value::Kind::NUMBER) return v->number;
    double t;
    if (resolveSpacingToken(v->str, t)) return t;
    return v->asNumber(def); // numeric string ("17") still works, matching old attrNum behavior
}

// ---- Radius tokens (§10) -------------------------------------------------------------------
inline bool resolveRadiusToken(const std::string& name, double& out) {
    if (name == "sharp") { out = 0;  return true; }
    if (name == "soft")  { out = 6;  return true; }
    if (name == "round") { out = 12; return true; }
    if (name == "pill")  { out = 9999; return true; } // clamped to half the strand's own size at paint time
    return false;
}
inline double resolveRadius(const Strand& s, double def = 0) {
    const Value* v = s.attr("radius");
    if (!v) return def;
    if (v->kind == Value::Kind::NUMBER) return v->number;
    double t;
    if (resolveRadiusToken(v->str, t)) return t;
    return v->asNumber(def);
}

// ---- Typography tokens (§12) -----------------------------------------------------------------
struct TypographyPreset { double size; std::string weight; };
inline bool resolveTypographyToken(const std::string& name, TypographyPreset& out) {
    if (name == "caption")  { out = {12, "regular"};  return true; }
    if (name == "body")     { out = {14, "regular"};  return true; }
    if (name == "label")    { out = {13, "medium"};   return true; }
    if (name == "subtitle") { out = {16, "medium"};   return true; }
    if (name == "title")    { out = {20, "semibold"}; return true; }
    if (name == "heading")  { out = {28, "bold"};     return true; }
    if (name == "code")     { out = {13, "regular"};  return true; }
    return false;
}
// A Text/Button strand's `size` attribute may be a token name ("title") instead of a number,
// same dual-form convention as padding/gap/radius above.
inline double resolveFontSize(const Strand& s, const std::string& key, double def) {
    const Value* v = s.attr(key);
    if (!v) return def;
    if (v->kind == Value::Kind::NUMBER) return v->number;
    TypographyPreset p;
    if (resolveTypographyToken(v->str, p)) return p.size;
    return v->asNumber(def);
}

// ---- Registering @theme=... declarations from a parsed program --------------------------------
// Called once by the cold pipeline (see rin_loom_pipeline.h) after parsing, before the Fabric is
// built, so that any @theme block earlier in the source is already active by the time Strand
// colors get resolved. New theme starts as a copy of the currently-active theme (usually the
// built-in Dark) so a source theme only needs to override the roles it actually wants to change —
// e.g. a `@theme=Midnight primary="#7C5CFF"; .end/theme` doesn't need to restate all 12 roles.
inline void registerThemesFromProgram(const std::vector<rin::StmtPtr>& program, WarpScope& warp) {
    for (auto& stmt : program) {
        auto t = std::dynamic_pointer_cast<rin::ThemeStmt>(stmt);
        if (!t) continue;
        Theme theme = themeRegistry().active();
        theme.name = t->name;
        bool makeActive = false;
        for (auto& a : t->attrs) {
            Value v = evalAttrExpr(a.value, warp, nullptr);
            if (a.key == "active") { makeActive = (v.asString() == "true"); continue; }
            if (looksLikeHexColor(v.asString())) theme.setSlot(a.key, parseHexColor(v.asString(), {0,0,0}));
        }
        themeRegistry().registerTheme(theme);
        if (makeActive) themeRegistry().setActive(theme.name);
    }
}

// ---- Button size scale (§4) --------------------------------------------------------------
// xs/small/medium/large/xl, each a {height, horizontal padding, font size} preset. `medium`
// intentionally matches the pre-Loom hardcoded Button defaults (44/16/16) so existing .rin
// sources with no size= attribute at all render pixel-identical to before this file existed.
struct ButtonSizePreset { double height, hPadding, fontSize; };
inline bool resolveButtonSizeToken(const std::string& name, ButtonSizePreset& out) {
    if (name == "xs")     { out = {28, 10, 12}; return true; }
    if (name == "small")  { out = {34, 12, 13}; return true; }
    if (name == "medium") { out = {44, 16, 16}; return true; }
    if (name == "large")  { out = {52, 20, 18}; return true; }
    if (name == "xl")     { out = {60, 26, 20}; return true; }
    return false;
}
inline ButtonSizePreset resolveButtonSize(const Strand& s) {
    ButtonSizePreset def; resolveButtonSizeToken("medium", def);
    const Value* v = s.attr("size");
    if (!v || v->kind != Value::Kind::STRING) return def;
    ButtonSizePreset p;
    return resolveButtonSizeToken(v->str, p) ? p : def;
}

// ---- Button variant (§4) -------------------------------------------------------------------
// The 7 tone variants (primary/secondary/success/danger/warning/info/neutral) are just tone=
// role names and need no special handling here -- they already resolve through resolveColor().
// These 5 are *treatments*, not colors: how the tone gets applied to the button's box.
enum class ButtonTreatment { FILL, OUTLINE, GHOST, LINK, GLASS, GRADIENT };
inline ButtonTreatment resolveButtonTreatment(const Strand& s) {
    std::string v = s.attrStr("variant", "fill");
    if (v == "outline")  return ButtonTreatment::OUTLINE;
    if (v == "ghost")    return ButtonTreatment::GHOST;
    if (v == "link")     return ButtonTreatment::LINK;
    if (v == "glass")    return ButtonTreatment::GLASS;
    if (v == "gradient") return ButtonTreatment::GRADIENT;
    return ButtonTreatment::FILL;
}

// ---- Button state (§4/§16) -----------------------------------------------------------------
// A real Strand-level state machine (not a CSS pseudo-selector): `state=` is one attribute like
// any other, read the same way tone=/size=/variant= are. `disabled`/`loading` also gate real
// runtime behavior (Needle refuses to dispatch onTap for a disabled button — see
// rin_loom_needle.h) rather than being paint-only flags.
enum class StrandState { NORMAL, HOVER, PRESSED, FOCUSED, DISABLED, LOADING, SELECTED };
inline StrandState resolveState(const Strand& s) {
    if (s.attrStr("disabled", "false") == "true") return StrandState::DISABLED;
    if (s.attrStr("loading", "false") == "true")  return StrandState::LOADING;
    std::string v = s.attrStr("state", "normal");
    if (v == "hover")    return StrandState::HOVER;
    if (v == "pressed")  return StrandState::PRESSED;
    if (v == "focused")  return StrandState::FOCUSED;
    if (v == "disabled") return StrandState::DISABLED;
    if (v == "loading")  return StrandState::LOADING;
    if (v == "selected") return StrandState::SELECTED;
    return StrandState::NORMAL;
}
inline const char* stateName(StrandState st) {
    switch (st) {
        case StrandState::HOVER: return "hover"; case StrandState::PRESSED: return "pressed";
        case StrandState::FOCUSED: return "focused"; case StrandState::DISABLED: return "disabled";
        case StrandState::LOADING: return "loading"; case StrandState::SELECTED: return "selected";
        default: return "normal";
    }
}
// Dims a color toward the active theme's background — used for disabled/loading, matching the
// "reduced but still legible" look every native toolkit uses instead of literally graying to 0.
inline Color dimTowardBackground(Color c, double amount /* 0..1 */) {
    const Color& bg = themeRegistry().active().background;
    auto mix = [&](unsigned char a, unsigned char b) { return (unsigned char)(a + (b - a) * amount); };
    return {mix(c.r, bg.r), mix(c.g, bg.g), mix(c.b, bg.b)};
}

// ---- Accessibility (§20) --------------------------------------------------------------------
// Minimal, additive accessibility metadata: a semantic `role` per StrandKind (screen-reader
// element type) and an accessible name. The accessible name defaults to the Strand's own visible
// text (label=/text=), the same default every native a11y framework uses, and can be overridden
// with a dedicated `a11y_label=` attribute *without* redefining what `label=` already means on
// Button (its visible text) — Loomtime already used that name before Loom's tokens existed, and
// silently repurposing it would break every existing Button in source.
inline std::string accessibleRole(StrandKind k) {
    switch (k) {
        case StrandKind::BUTTON:   return "button";
        case StrandKind::MENUITEM: return "menuitem";
        case StrandKind::MENU:     return "menu";
        case StrandKind::BANNER:   return "alert";
        case StrandKind::IMAGE:    return "image";
        case StrandKind::TEXT:     return "text";
        case StrandKind::TABLE:    return "table";
        case StrandKind::WEBVIEW:  return "webview";
        case StrandKind::CHECKBOX: return "checkbox";
        case StrandKind::SWITCH:   return "switch";
        case StrandKind::PROGRESS: return "progressbar";
        case StrandKind::INPUT:    return "textbox";
        case StrandKind::TEXTAREA: return "textbox";
        case StrandKind::DIALOG:   return "dialog";
        case StrandKind::TABS:     return "tablist";
        case StrandKind::TABITEM:  return "tab";
        case StrandKind::AVATAR:   return "image";
        case StrandKind::BADGE:    return "status";
        case StrandKind::TOOLTIP:  return "tooltip";
        default: return "";
    }
}
inline std::string accessibleName(const Strand& s) {
    std::string explicitLabel = s.attrStr("a11y_label", "");
    if (!explicitLabel.empty()) return explicitLabel;
    if (s.kind == StrandKind::BUTTON || s.kind == StrandKind::MENUITEM || s.kind == StrandKind::TABITEM ||
        s.kind == StrandKind::CHECKBOX || s.kind == StrandKind::SWITCH)
        return s.attrStr("label", "");
    if (s.kind == StrandKind::INPUT || s.kind == StrandKind::TEXTAREA) {
        std::string v = s.attrStr("value", "");
        return !v.empty() ? v : s.attrStr("placeholder", "");
    }
    return s.attrStr("text", "");
}

// ---- Sizing modes (§5/§6): semantic replacements for scattering width=/height= everywhere --
// `sizing=` is generic -- it applies to any StrandKind, not just a dedicated "Container" one
// (see docs/loomtime/RIN_LOOM_TOKENS.md's open naming-collision note on why there's no new
// "Container" StrandKind yet). Scope note: only WIDTH is affected by fill/expand today, because
// the root constraint chain in this engine bounds width from the very first `loomc.cpp` call
// (`{0, rootWidth, 0, 1e9}`) but deliberately leaves height unbounded (1e9) almost everywhere,
// so "fill height" has no finite parent extent to fill against in most real layouts. A Strand
// that DOES have a bounded height available (inside something that set an explicit height=)
// still benefits: fill/expand there fills height too. hug/fit/auto are unaffected either way --
// they're what every StrandKind's own measure function already does by default.
enum class SizingMode { AUTO, FIT, HUG, FILL, EXPAND, FIXED };
inline SizingMode resolveSizingMode(const Strand& s) {
    std::string v = s.attrStr("sizing", "auto");
    if (v == "fit")    return SizingMode::FIT;
    if (v == "hug")    return SizingMode::HUG;
    if (v == "fill")   return SizingMode::FILL;
    if (v == "expand") return SizingMode::EXPAND;
    if (v == "fixed")  return SizingMode::FIXED;
    return SizingMode::AUTO;
}

} // namespace loom
