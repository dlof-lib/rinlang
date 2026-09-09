// rin_color.h — Rin's Color Engine: one real, shared implementation of color parsing,
// formatting, and math, used by BOTH the language runtime (natives in rin_interpreter.cpp,
// so any .rin script can parse/mix/lighten/contrast-check colors) and Loom's renderer
// (loom::Color in loom/rin_loom_tokens.h + rin_loom_paint.h's Dye rasterizer/JSON export).
//
// This header has NO dependency on the interpreter or on loom -- it is pure, self-contained
// color math -- specifically so it can sit underneath both without creating a dependency
// cycle (the CLI / non-Loom build of rin_run.cpp does not need to pull in Loom to get color
// support for its own natives, and Loom does not need to know about rin::Value).
//
// Supported literal syntax (parseColor / tryParseColor):
//   #rgb  #rgba  #rrggbb  #rrggbbaa        (hex, 3/4/6/8 digit, case-insensitive)
//   rgb(r,g,b)        rgba(r,g,b,a)        (r,g,b in 0-255 OR "n%"; a in 0-1 OR "n%")
//   hsl(h,s%,l%)      hsla(h,s%,l%,a)      (h in degrees, s/l as percentages, a in 0-1)
//   named CSS colors (the standard 148-name X11/CSS3 keyword set) + "transparent"
//
// Everything else (semantic role names like "primary"/"danger", resolved against the active
// Theme) is Loom's own concern and stays in loom::resolveSemanticColor() -- this engine only
// ever deals with literal color *values*, which is exactly the layer a language-level color
// API needs too.
#pragma once
#include <string>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <vector>

namespace rincolor {

// ---- Core value type ---------------------------------------------------------------------
// r/g/b/a all 0-255 (alpha included, unlike the old loom-only struct this replaces). A fully
// opaque color has a == 255; a == 0 is fully transparent. Kept as plain bytes (not floats) so
// it stays a trivial, cheap-to-copy aggregate everywhere it already was one.
struct Color {
    unsigned char r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Color& o) const { return r==o.r && g==o.g && b==o.b && a==o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

inline unsigned char clampByte(int v) { return (unsigned char)std::max(0, std::min(255, v)); }
inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
inline unsigned char alphaFromUnit(double a01) { return clampByte((int)std::lround(clamp01(a01) * 255.0)); }
inline double unitFromAlpha(unsigned char a) { return a / 255.0; }

// ---- small string helpers -----------------------------------------------------------------
namespace detail {
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b-1])) b--;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline bool isHexDigit(char c) { return std::isxdigit((unsigned char)c) != 0; }
inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
// Splits "a,b,c,d" (a functional color's argument list) on commas AND/OR whitespace, so both
// "rgb(10,20,30)" and modern "rgb(10 20 30 / 0.5)" spacing survive; a trailing "/ alpha" slash
// is treated as just another separator, matching how every real CSS color parser accepts it.
inline std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == '/' || std::isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
// Parses one rgb() channel: either a plain 0-255 number or an "n%" percentage of 255.
inline bool parseChannel255(const std::string& tok, unsigned char& out) {
    if (tok.empty()) return false;
    try {
        if (tok.back() == '%') {
            double pct = std::stod(tok.substr(0, tok.size() - 1));
            out = clampByte((int)std::lround(pct / 100.0 * 255.0));
        } else {
            out = clampByte((int)std::lround(std::stod(tok)));
        }
        return true;
    } catch (...) { return false; }
}
// Parses an alpha token: either a plain 0-1 fraction or an "n%" percentage.
inline bool parseAlphaUnit(const std::string& tok, double& out) {
    if (tok.empty()) return false;
    try {
        if (tok.back() == '%') out = std::stod(tok.substr(0, tok.size() - 1)) / 100.0;
        else out = std::stod(tok);
        out = clamp01(out);
        return true;
    } catch (...) { return false; }
}
} // namespace detail

// ---- Named CSS colors (the standard 148 keyword set + "transparent") ----------------------
inline const std::unordered_map<std::string, Color>& namedColors() {
    static const std::unordered_map<std::string, Color> table = {
        {"transparent",{0,0,0,0}}, {"black",{0,0,0}}, {"white",{255,255,255}},
        {"red",{255,0,0}}, {"green",{0,128,0}}, {"blue",{0,0,255}},
        {"yellow",{255,255,0}}, {"cyan",{0,255,255}}, {"magenta",{255,0,255}},
        {"gray",{128,128,128}}, {"grey",{128,128,128}}, {"silver",{192,192,192}},
        {"maroon",{128,0,0}}, {"olive",{128,128,0}}, {"lime",{0,255,0}},
        {"aqua",{0,255,255}}, {"teal",{0,128,128}}, {"navy",{0,0,128}},
        {"fuchsia",{255,0,255}}, {"purple",{128,0,128}}, {"orange",{255,165,0}},
        {"pink",{255,192,203}}, {"brown",{165,42,42}}, {"gold",{255,215,0}},
        {"indigo",{75,0,130}}, {"violet",{238,130,238}}, {"turquoise",{64,224,208}},
        {"coral",{255,127,80}}, {"salmon",{250,128,114}}, {"khaki",{240,230,140}},
        {"orchid",{218,112,214}}, {"plum",{221,160,221}}, {"tan",{210,180,140}},
        {"beige",{245,245,220}}, {"ivory",{255,255,240}}, {"lavender",{230,230,250}},
        {"crimson",{220,20,60}}, {"chocolate",{210,105,30}}, {"tomato",{255,99,71}},
        {"orangered",{255,69,0}}, {"hotpink",{255,105,180}}, {"deeppink",{255,20,147}},
        {"skyblue",{135,206,235}}, {"steelblue",{70,130,180}}, {"royalblue",{65,105,225}},
        {"slateblue",{106,90,205}}, {"dodgerblue",{30,144,255}}, {"cornflowerblue",{100,149,237}},
        {"seagreen",{46,139,87}}, {"forestgreen",{34,139,34}}, {"limegreen",{50,205,50}},
        {"darkgreen",{0,100,0}}, {"darkred",{139,0,0}}, {"darkblue",{0,0,139}},
        {"darkorange",{255,140,0}}, {"darkviolet",{148,0,211}}, {"darkslategray",{47,79,79}},
        {"darkslategrey",{47,79,79}}, {"darkgray",{169,169,169}}, {"darkgrey",{169,169,169}},
        {"lightgray",{211,211,211}}, {"lightgrey",{211,211,211}}, {"lightblue",{173,216,230}},
        {"lightgreen",{144,238,144}}, {"lightyellow",{255,255,224}}, {"lightpink",{255,182,193}},
        {"lightcoral",{240,128,128}}, {"lightsalmon",{255,160,122}}, {"lightseagreen",{32,178,170}},
        {"lightskyblue",{135,206,250}}, {"lightslategray",{119,136,153}}, {"lightslategrey",{119,136,153}},
        {"mintcream",{245,255,250}}, {"honeydew",{240,255,240}}, {"aliceblue",{240,248,255}},
        {"azure",{240,255,255}}, {"snow",{255,250,250}}, {"linen",{250,240,230}},
        {"whitesmoke",{245,245,245}}, {"gainsboro",{220,220,220}}, {"seashell",{255,245,238}},
        {"wheat",{245,222,179}}, {"peachpuff",{255,218,185}}, {"navajowhite",{255,222,173}},
        {"moccasin",{255,228,181}}, {"cornsilk",{255,248,220}}, {"lemonchiffon",{255,250,205}},
        {"papayawhip",{255,239,213}}, {"blanchedalmond",{255,235,205}}, {"bisque",{255,228,196}},
        {"antiquewhite",{250,235,215}}, {"mistyrose",{255,228,225}}, {"thistle",{216,191,216}},
        {"mediumpurple",{147,112,219}}, {"mediumorchid",{186,85,211}}, {"mediumvioletred",{199,21,133}},
        {"mediumseagreen",{60,179,113}}, {"mediumspringgreen",{0,250,154}}, {"mediumturquoise",{72,209,204}},
        {"mediumblue",{0,0,205}}, {"mediumslateblue",{123,104,238}}, {"mediumaquamarine",{102,205,170}},
        {"aquamarine",{127,255,212}}, {"springgreen",{0,255,127}}, {"chartreuse",{127,255,0}},
        {"yellowgreen",{154,205,50}}, {"olivedrab",{107,142,35}}, {"darkolivegreen",{85,107,47}},
        {"darkkhaki",{189,183,107}}, {"palegoldenrod",{238,232,170}}, {"goldenrod",{218,165,32}},
        {"darkgoldenrod",{184,134,11}}, {"peru",{205,133,63}}, {"sienna",{160,82,45}},
        {"saddlebrown",{139,69,19}}, {"firebrick",{178,34,34}}, {"indianred",{205,92,92}},
        {"rosybrown",{188,143,143}}, {"palevioletred",{219,112,147}}, {"deepskyblue",{0,191,255}},
        {"cadetblue",{95,158,160}}, {"powderblue",{176,224,230}}, {"paleturquoise",{175,238,238}},
        {"darkcyan",{0,139,139}}, {"darkturquoise",{0,206,209}}, {"lightcyan",{224,255,255}},
        {"lightsteelblue",{176,196,222}}, {"midnightblue",{25,25,112}}, {"darkslateblue",{72,61,139}},
        {"blueviolet",{138,43,226}}, {"darkmagenta",{139,0,139}}, {"darkorchid",{153,50,204}},
        {"darksalmon",{233,150,122}}, {"darkseagreen",{143,188,143}}, {"palegreen",{152,251,152}},
        {"greenyellow",{173,255,47}}, {"lawngreen",{124,252,0}}, {"lightgoldenrodyellow",{250,250,210}},
        {"lightskygray",{135,206,235}}, {"slategray",{112,128,144}}, {"slategrey",{112,128,144}},
        {"dimgray",{105,105,105}}, {"dimgrey",{105,105,105}}, {"lightsalmonpink",{255,160,122}},
        {"lightgoldenrod",{238,221,130}}, {"ghostwhite",{248,248,255}}, {"floralwhite",{255,250,240}},
        {"oldlace",{253,245,230}}, {"lavenderblush",{255,240,245}}, {"lightgrayish",{220,220,220}},
        {"rebeccapurple",{102,51,153}},
    };
    return table;
}

// ---- HSL <-> RGB -----------------------------------------------------------------------
struct HSL { double h = 0, s = 0, l = 0; }; // h: 0-360, s/l: 0-1

inline HSL rgbToHsl(const Color& c) {
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double h = 0, s = 0, l = (mx + mn) / 2.0;
    double d = mx - mn;
    if (d > 1e-9) {
        s = (l > 0.5) ? d / (2.0 - mx - mn) : d / (mx + mn);
        if (mx == r) h = std::fmod((g - b) / d + (g < b ? 6.0 : 0.0), 6.0);
        else if (mx == g) h = (b - r) / d + 2.0;
        else h = (r - g) / d + 4.0;
        h *= 60.0;
    }
    return {h, s, l};
}
inline double hueToRgb(double p, double q, double t) {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}
inline Color hslToRgb(double h, double s, double l, unsigned char a = 255) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 360.0;
    s = clamp01(s); l = clamp01(l);
    double r, g, b;
    if (s < 1e-9) { r = g = b = l; }
    else {
        double q = (l < 0.5) ? l * (1 + s) : l + s - l * s;
        double p = 2 * l - q;
        r = hueToRgb(p, q, h + 1.0/3.0);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0/3.0);
    }
    return { clampByte((int)std::lround(r*255)), clampByte((int)std::lround(g*255)),
             clampByte((int)std::lround(b*255)), a };
}

// ---- Parsing -----------------------------------------------------------------------------
// Fast pre-check used to decide "is this string even trying to be a color literal" without
// fully parsing it -- mirrors (and replaces) the old loom::looksLikeHexColor, but recognizes
// every literal form this engine understands, not just #rrggbb.
inline bool looksLikeColorLiteral(const std::string& raw) {
    std::string s = detail::trim(raw);
    if (s.empty()) return false;
    if (s[0] == '#') return s.size() == 4 || s.size() == 5 || s.size() == 7 || s.size() == 9;
    std::string low = detail::lower(s);
    if (low.rfind("rgb(", 0) == 0 || low.rfind("rgba(", 0) == 0) return true;
    if (low.rfind("hsl(", 0) == 0 || low.rfind("hsla(", 0) == 0) return true;
    return namedColors().count(low) != 0;
}

inline bool tryParseColor(const std::string& raw, Color& out) {
    std::string s = detail::trim(raw);
    if (s.empty()) return false;

    // #rgb / #rgba / #rrggbb / #rrggbbaa
    if (s[0] == '#') {
        for (size_t i = 1; i < s.size(); i++) if (!detail::isHexDigit(s[i])) return false;
        if (s.size() == 4) { // #rgb
            int r = detail::hexVal(s[1]), g = detail::hexVal(s[2]), b = detail::hexVal(s[3]);
            out = { clampByte(r*16+r), clampByte(g*16+g), clampByte(b*16+b), 255 };
            return true;
        }
        if (s.size() == 5) { // #rgba
            int r = detail::hexVal(s[1]), g = detail::hexVal(s[2]), b = detail::hexVal(s[3]), a = detail::hexVal(s[4]);
            out = { clampByte(r*16+r), clampByte(g*16+g), clampByte(b*16+b), clampByte(a*16+a) };
            return true;
        }
        if (s.size() == 7) { // #rrggbb
            out = { clampByte(detail::hexVal(s[1])*16+detail::hexVal(s[2])),
                    clampByte(detail::hexVal(s[3])*16+detail::hexVal(s[4])),
                    clampByte(detail::hexVal(s[5])*16+detail::hexVal(s[6])), 255 };
            return true;
        }
        if (s.size() == 9) { // #rrggbbaa
            out = { clampByte(detail::hexVal(s[1])*16+detail::hexVal(s[2])),
                    clampByte(detail::hexVal(s[3])*16+detail::hexVal(s[4])),
                    clampByte(detail::hexVal(s[5])*16+detail::hexVal(s[6])),
                    clampByte(detail::hexVal(s[7])*16+detail::hexVal(s[8])) };
            return true;
        }
        return false;
    }

    std::string low = detail::lower(s);

    // rgb()/rgba()
    if (low.rfind("rgb(", 0) == 0 || low.rfind("rgba(", 0) == 0) {
        size_t open = low.find('('), close = low.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return false;
        auto args = detail::splitArgs(s.substr(open + 1, close - open - 1));
        if (args.size() < 3) return false;
        unsigned char r, g, b;
        if (!detail::parseChannel255(args[0], r) || !detail::parseChannel255(args[1], g) ||
            !detail::parseChannel255(args[2], b)) return false;
        unsigned char a = 255;
        if (args.size() >= 4) {
            double au; if (!detail::parseAlphaUnit(args[3], au)) return false;
            a = alphaFromUnit(au);
        }
        out = {r, g, b, a};
        return true;
    }

    // hsl()/hsla()
    if (low.rfind("hsl(", 0) == 0 || low.rfind("hsla(", 0) == 0) {
        size_t open = low.find('('), close = low.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return false;
        auto args = detail::splitArgs(s.substr(open + 1, close - open - 1));
        if (args.size() < 3) return false;
        try {
            double h = std::stod(args[0]);
            double sPct = args[1].size() && args[1].back() == '%' ? std::stod(args[1].substr(0, args[1].size()-1)) : std::stod(args[1]) * 100.0;
            double lPct = args[2].size() && args[2].back() == '%' ? std::stod(args[2].substr(0, args[2].size()-1)) : std::stod(args[2]) * 100.0;
            unsigned char a = 255;
            if (args.size() >= 4) {
                double au; if (!detail::parseAlphaUnit(args[3], au)) return false;
                a = alphaFromUnit(au);
            }
            out = hslToRgb(h, sPct / 100.0, lPct / 100.0, a);
            return true;
        } catch (...) { return false; }
    }

    // named colors (includes "transparent")
    auto it = namedColors().find(low);
    if (it != namedColors().end()) { out = it->second; return true; }

    return false;
}

inline Color parseColor(const std::string& raw, Color fallback = {0, 0, 0, 255}) {
    Color c;
    return tryParseColor(raw, c) ? c : fallback;
}

// ---- Formatting --------------------------------------------------------------------------
inline std::string toHexByte(unsigned char v) {
    static const char* digits = "0123456789ABCDEF";
    std::string out(2, '0');
    out[0] = digits[(v >> 4) & 0xF];
    out[1] = digits[v & 0xF];
    return out;
}
inline std::string toHex6(const Color& c) { return "#" + toHexByte(c.r) + toHexByte(c.g) + toHexByte(c.b); }
inline std::string toHex8(const Color& c) { return toHex6(c) + toHexByte(c.a); }
// Canonical serialization: 6-digit hex when fully opaque (matches every existing #RRGGBB
// call site untouched), 8-digit only when the color actually carries transparency -- so
// existing consumers that only understand 6-digit hex keep working unchanged.
inline std::string toHexAuto(const Color& c) { return c.a == 255 ? toHex6(c) : toHex8(c); }

inline std::string toRgbaString(const Color& c) {
    std::ostringstream os;
    os << "rgba(" << (int)c.r << "," << (int)c.g << "," << (int)c.b << ","
       << std::fixed << std::setprecision(3) << unitFromAlpha(c.a) << ")";
    return os.str();
}
inline std::string toHslaString(const Color& c) {
    HSL hsl = rgbToHsl(c);
    std::ostringstream os;
    os << "hsla(" << std::fixed << std::setprecision(1) << hsl.h << ","
       << std::setprecision(1) << (hsl.s * 100.0) << "%," << (hsl.l * 100.0) << "%,"
       << std::setprecision(3) << unitFromAlpha(c.a) << ")";
    return os.str();
}

// ---- Math / manipulation ------------------------------------------------------------------
// Straight linear-channel mix, RGB and alpha both interpolated -- t=0 -> a, t=1 -> b.
inline Color mix(const Color& a, const Color& b, double t) {
    t = clamp01(t);
    auto lerp = [&](unsigned char x, unsigned char y) { return clampByte((int)std::lround(x + (y - x) * t)); };
    return { lerp(a.r,b.r), lerp(a.g,b.g), lerp(a.b,b.b), lerp(a.a,b.a) };
}
inline Color lighten(const Color& c, double amount) { return mix(c, Color{255,255,255,c.a}, clamp01(amount)); }
inline Color darken(const Color& c, double amount)  { return mix(c, Color{0,0,0,c.a}, clamp01(amount)); }
inline Color withAlpha(const Color& c, double alpha01) { Color out = c; out.a = alphaFromUnit(alpha01); return out; }
inline Color grayscale(const Color& c) {
    unsigned char v = clampByte((int)std::lround(0.299*c.r + 0.587*c.g + 0.114*c.b));
    return { v, v, v, c.a };
}
inline Color invert(const Color& c) { return { clampByte(255-c.r), clampByte(255-c.g), clampByte(255-c.b), c.a }; }

// Composites a (possibly translucent) foreground color over an opaque background --
// "what does this actually look like once painted on top of bg" -- straight alpha-over.
inline Color alphaBlend(const Color& fg, const Color& bg) {
    double af = unitFromAlpha(fg.a);
    auto ch = [&](unsigned char f, unsigned char b) { return clampByte((int)std::lround(f*af + b*(1.0-af))); };
    return { ch(fg.r,bg.r), ch(fg.g,bg.g), ch(fg.b,bg.b), 255 };
}

// ---- WCAG accessibility math ----------------------------------------------------------------
inline double srgbChannelToLinear(double c) {
    c = clamp01(c);
    return (c <= 0.03928) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
// WCAG 2.x relative luminance (0 = black, 1 = white). Ignores alpha -- callers that care about
// a translucent color's *effective* luminance against a page should alphaBlend() onto their
// background first, same as a browser's own contrast tooling does.
inline double relativeLuminance(const Color& c) {
    double r = srgbChannelToLinear(c.r / 255.0);
    double g = srgbChannelToLinear(c.g / 255.0);
    double b = srgbChannelToLinear(c.b / 255.0);
    return 0.2126*r + 0.7152*g + 0.0722*b;
}
// WCAG contrast ratio, always >= 1.0 (1:1 identical, 21:1 black-on-white max).
inline double contrastRatio(const Color& a, const Color& b) {
    double la = relativeLuminance(a) + 0.05, lb = relativeLuminance(b) + 0.05;
    return (la > lb) ? la / lb : lb / la;
}
inline bool isLight(const Color& c) { return relativeLuminance(c) > 0.5; }
// Picks near-black or near-white, whichever contrasts better against `bg` -- the standard
// "what text color goes on this fill" rule every design system needs.
inline Color bestTextColor(const Color& bg) {
    Color black{0,0,0,255}, white{255,255,255,255};
    return (contrastRatio(bg, white) >= contrastRatio(bg, black)) ? white : black;
}

} // namespace rincolor
