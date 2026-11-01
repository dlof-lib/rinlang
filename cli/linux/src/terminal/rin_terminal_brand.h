// ============================================================================
//  Rin Shell — official names and visual identity of the Rin terminal.
//
//    Rin Shell    the interactive terminal of the Rin language  (:  and  $  commands)
//    Rin Toolbox  the built-in command library of Rin Shell     ($ cmd … ~/)
//
//  Icons: assets/branding/shell/rin-shell.svg and rin-toolbox.svg.
//  The 24x24 logo below is the same drawing in pixels (xterm-256 numbers), printed
//  two pixels per character with half-block glyphs. Header-only, std only.
// ============================================================================
#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace rin {
namespace terminal {
namespace brand {

inline constexpr const char* kShell = "Rin Shell";
inline constexpr const char* kToolbox = "Rin Toolbox";
inline constexpr const char* kShellEn = "the interactive terminal of the Rin language";
inline constexpr const char* kShellAr = "الطرفية التفاعلية للغة Rin";
inline constexpr const char* kToolboxEn = "built-in command library";
inline constexpr const char* kToolboxAr = "مكتبة الأوامر المدمجة";

// 24x24 pixel logo of Rin Shell (xterm-256 color numbers, 0 = transparent). Drawn by hand to match assets/branding/shell/rin-shell.svg
static const unsigned char kLogo[24][24] = {
    {0,0,0,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,0,0,0},
    {0,0,61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61,0,0},
    {0,61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61,0},
    {61,234,234,234,203,234,234,221,234,234,77,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,234,61},
    {61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,141,141,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,141,141,234,234,234,234,234,234,60,60,60,60,60,60,60,234,234,61},
    {61,234,234,234,234,234,234,141,141,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,141,141,234,234,234,234,238,238,238,238,238,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,141,141,141,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,234,99,141,141,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,99,99,99,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,99,99,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,99,99,234,234,234,234,234,78,78,78,78,78,78,234,234,234,61},
    {61,234,234,234,234,234,99,99,234,234,234,234,234,234,35,35,35,35,35,35,234,234,234,61},
    {61,234,234,234,234,99,99,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61},
    {0,61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61,0},
    {0,0,61,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,61,0,0},
    {0,0,0,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,0,0,0},
};

inline void xtermRgb(int n, int& r, int& g, int& b) {
    static const int cube[6] = {0, 95, 135, 175, 215, 255};
    if (n >= 232) { r = g = b = 8 + 10 * (n - 232); }
    else if (n >= 16) { int k = n - 16; r = cube[k / 36]; g = cube[k / 6 % 6]; b = cube[k % 6]; }
    else { r = g = b = 128; }
}
// nearest of the 8 basic ANSI colors (30..37) for terminals without 256 colors
inline int basicColor(int n) {
    int r, g, b; xtermRgb(n, r, g, b);
    static const int pal[8][3] = {{0, 0, 0}, {205, 49, 49}, {13, 188, 121}, {229, 229, 16}, {36, 114, 200}, {188, 63, 188}, {17, 168, 205}, {229, 229, 229}};
    int best = 0; double bd = 1e18;
    for (int i = 0; i < 8; ++i) { double d = std::pow(r - pal[i][0], 2) + std::pow(g - pal[i][1], 2) + std::pow(b - pal[i][2], 2); if (d < bd) { bd = d; best = i; } }
    return best;
}
inline std::string fgCode(int n, bool c256) { return c256 ? "\x1b[38;5;" + std::to_string(n) + "m" : "\x1b[" + std::to_string(30 + basicColor(n)) + "m"; }
inline std::string bgCode(int n, bool c256) { return c256 ? "\x1b[48;5;" + std::to_string(n) + "m" : "\x1b[" + std::to_string(40 + basicColor(n)) + "m"; }

// colored logo: 12 lines of 24 columns (two pixel rows per line)
inline std::vector<std::string> logoColor(bool c256) {
    std::vector<std::string> out;
    for (int y = 0; y < 24; y += 2) {
        std::string line;
        for (int x = 0; x < 24; ++x) {
            int t = kLogo[y][x], b = kLogo[y + 1][x];
            if (!t && !b) line += ' ';
            else if (t && !b) line += fgCode(t, c256) + "\xE2\x96\x80" + "\x1b[0m";          // ▀
            else if (!t && b) line += fgCode(b, c256) + "\xE2\x96\x84" + "\x1b[0m";          // ▄
            else if (t == b) line += fgCode(t, c256) + "\xE2\x96\x88" + "\x1b[0m";           // █
            else line += fgCode(t, c256) + bgCode(b, c256) + "\xE2\x96\x80" + "\x1b[0m";     // ▀ on a background
        }
        out.push_back(line);
    }
    return out;
}
// monochrome logo (no colors): 5 lines, 14 columns; ASCII when the terminal is not UTF-8
inline std::vector<std::string> logoMono(bool utf8) {
    if (utf8) return {"┌──────────┐", "│ ● ● ●    │", "│ ❯   ──   │", "│      ▂▂▂ │", "└──────────┘"};
    return {"+----------+", "| o o o    |", "| >   --   |", "|      ___ |", "+----------+"};
}
inline int logoColorWidth() { return 24; }
inline int logoMonoWidth() { return 12; }

// terminal window / tab title (OSC 0)
inline std::string windowTitle(const std::string& cwd) { return std::string("\x1b]0;") + kShell + (cwd.empty() ? "" : " - " + cwd) + "\x07"; }

}  // namespace brand
}  // namespace terminal
}  // namespace rin
