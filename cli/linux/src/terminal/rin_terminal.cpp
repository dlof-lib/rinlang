// ============================================================================
//  rin terminal — التنفيذ. انظر rin_terminal.h وdocs/terminal.md
// ============================================================================
#include "rin_terminal.h"

#include "rin_version.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_ast.h"
#include "rin_interpreter.h"
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/diagnostic_engine.h"
#include "diagnostics/source_manager.h"
#include "../pkg/cli_pkg.h"
#include "indsin/rin_indsin_c_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace rin {
namespace terminal {
namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// لغة الواجهة: عربي افتراضياً (كبقية rin CLI)، RIN_LANG=en للإنجليزية.
// ---------------------------------------------------------------------------
bool g_english = false;
const char* tr(const char* ar, const char* en) { return g_english ? en : ar; }

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-32 وعرض الخلايا
// ---------------------------------------------------------------------------
using U32 = std::u32string;

U32 toU32(const std::string& s) {
    U32 out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        int n = 1;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
        else if ((c >> 4) == 0xE && i + 2 < s.size()) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
        else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); n = 4;
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string toUtf8(const U32& s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) appendUtf8(out, c);
    return out;
}

int cpWidth(char32_t c) {
    if (c == 0) return 0;
    if (c < 32 || (c >= 0x7f && c < 0xa0)) return 0;
    if ((c >= 0x300 && c <= 0x36f) || (c >= 0x483 && c <= 0x489) || (c >= 0x591 && c <= 0x5bd) ||
        (c >= 0x610 && c <= 0x61a) || (c >= 0x64b && c <= 0x65f) || c == 0x670 ||
        (c >= 0x6d6 && c <= 0x6dc) || (c >= 0x6df && c <= 0x6e4) || (c >= 0x6e7 && c <= 0x6e8) ||
        (c >= 0x6ea && c <= 0x6ed) || (c >= 0x200b && c <= 0x200f) || (c >= 0x20d0 && c <= 0x20ff) ||
        (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xfe20 && c <= 0xfe2f))
        return 0;
    if ((c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) || (c >= 0xac00 && c <= 0xd7a3) ||
        (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe6f) || (c >= 0xff00 && c <= 0xff60) ||
        (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x1f300 && c <= 0x1faff) || (c >= 0x20000 && c <= 0x3fffd))
        return 2;
    return 1;
}

int strWidth(const U32& s) { int w = 0; for (char32_t c : s) w += cpWidth(c); return w; }
int strWidth(const std::string& s) { return strWidth(toU32(s)); }

bool isIdentStart(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
bool isIdentChar(char32_t c)  { return isIdentStart(c) || (c >= '0' && c <= '9'); }
bool isDigit(char32_t c)      { return c >= '0' && c <= '9'; }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string ltrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a);
}
bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::string joinLines(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) { if (i) out += sep; out += v[i]; }
    return out;
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

bool isDirPath(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
bool writeFileAll(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << content;
    return static_cast<bool>(f);
}

void mkdirP(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' && cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
    ::mkdir(path.c_str(), 0755);
}

std::string expandTilde(const std::string& p) {
    if (!p.empty() && p[0] == '~' && (p.size() == 1 || p[1] == '/')) {
        const char* h = std::getenv("HOME");
        if (h) return std::string(h) + p.substr(1);
    }
    return p;
}

// ---------------------------------------------------------------------------
// ألوان ANSI
// ---------------------------------------------------------------------------
struct Style {
    bool on = true;
    bool dark = true;
    bool c256 = true;

    std::string sgr(const std::string& code) const { return on ? "\x1b[" + code + "m" : ""; }
    std::string fg(int n256, int basic) const {
        if (!on) return "";
        return c256 ? "\x1b[38;5;" + std::to_string(n256) + "m" : "\x1b[" + std::to_string(30 + basic) + "m";
    }
    std::string reset() const { return sgr("0"); }
    std::string bold()  const { return sgr("1"); }
    std::string dim()   const { return on ? (c256 ? fg(frameGray(), 7) : sgr("2")) : ""; }
    int frameGray() const { return dark ? 244 : 247; }
    std::string red()     const { return fg(dark ? 203 : 160, 1); }
    std::string green()   const { return fg(dark ? 114 : 28, 2); }
    std::string yellow()  const { return fg(dark ? 221 : 130, 3); }
    std::string blue()    const { return fg(dark ? 75 : 25, 4); }
    std::string magenta() const { return fg(dark ? 176 : 90, 5); }
    std::string cyan()    const { return fg(dark ? 80 : 30, 6); }
    std::string orange()  const { return fg(dark ? 209 : 166, 3); }
};

enum Cls : uint8_t {
    C_PLAIN = 0, C_KEYWORD, C_DECL, C_CONST, C_STRING, C_NUMBER, C_COMMENT, C_DIRECTIVE,
    C_FUNC, C_OP, C_PUNCT, C_HASH, C_ERROR, C_HINT, C_TYPE
};

std::string clsStyle(const Style& st, uint8_t c) {
    switch (c) {
        case C_KEYWORD:   return st.magenta();
        case C_DECL:      return st.blue() + st.bold();
        case C_CONST:     return st.orange();
        case C_STRING:    return st.green();
        case C_NUMBER:    return st.yellow();
        case C_COMMENT:   return st.dim();
        case C_DIRECTIVE: return st.cyan();
        case C_FUNC:      return st.blue();
        case C_OP:        return st.fg(st.dark ? 180 : 95, 3);
        case C_PUNCT:     return st.fg(st.dark ? 250 : 240, 7);
        case C_HASH:      return st.cyan();
        case C_ERROR:     return st.red() + st.bold();
        case C_HINT:      return st.dim();
        case C_TYPE:      return st.cyan();
        default:          return "";
    }
}

// ---------------------------------------------------------------------------
// تحليل نص الشيفرة: تلوين + معرفة اكتمال الجملة (أقواس/نصوص مفتوحة).
// يعمل على نص ناقص أثناء الكتابة ولا يرمي أبداً، ومستقل تماماً عن Lexer
// الحقيقي (فلا يسجّل شيئاً في SourceManager العام عند كل ضغطة مفتاح).
// ---------------------------------------------------------------------------
const std::map<std::string, uint8_t>& keywordClasses() {
    static std::map<std::string, uint8_t> m;
    if (m.empty()) {
        for (const auto& k : rin::keywordList()) m[k] = C_DECL;  // الباقي (حاويات/بيانات) كإعلانات
        for (const char* k : {"if", "else", "while", "for", "return", "break", "continue", "rinopen", "and", "or"}) m[k] = C_KEYWORD;
        for (const char* k : {"true", "false", "nil"}) m[k] = C_CONST;
        for (const char* k : {"print", "show"}) m[k] = C_DECL;
    }
    return m;
}

struct Analysis {
    std::vector<uint8_t> cls;     // صنف لكل code point
    std::string stack;            // أقواس/قوالب مفتوحة: ( [ { ` $
    bool unterminatedString = false;
    bool trailingBackslash = false;
    bool trailingOperator = false;   // سطر ينتهي بمُشغّل ثنائي (1 + ) أو and/or → المتابعة في السطر التالي
    bool needsMore() const { return !stack.empty() || unterminatedString || trailingBackslash || trailingOperator; }
    int braceDepth() const { int d = 0; for (char c : stack) if (c == '{' || c == '$') ++d; return d; }
};

Analysis analyze(const U32& s) {
    Analysis a;
    a.cls.assign(s.size(), C_PLAIN);
    const auto& kw = keywordClasses();
    size_t n = s.size();
    auto at = [&](size_t i) -> char32_t { return i < n ? s[i] : 0; };
    bool prevWasFun = false;

    for (size_t i = 0; i < n;) {
        char32_t c = s[i];
        char top = a.stack.empty() ? 0 : a.stack.back();

        if (top == '`') {  // داخل نص قالبي
            if (c == '\\' && i + 1 < n) { a.cls[i] = a.cls[i + 1] = C_STRING; i += 2; continue; }
            if (c == '`') { a.cls[i] = C_STRING; a.stack.pop_back(); ++i; continue; }
            if (c == '$' && at(i + 1) == '{') { a.cls[i] = a.cls[i + 1] = C_PUNCT; a.stack.push_back('$'); i += 2; continue; }
            a.cls[i] = C_STRING; ++i; continue;
        }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }

        if (c == '/' && at(i + 1) == '/') {
            while (i < n && s[i] != '\n') a.cls[i++] = C_COMMENT;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1; bool closed = false;
            while (j < n) {
                if (s[j] == '\\') { j += 2; continue; }
                if (s[j] == '"') { closed = true; ++j; break; }
                ++j;
            }
            if (j > n) { j = n; a.trailingBackslash = !closed; }
            for (size_t k = i; k < j && k < n; ++k) a.cls[k] = C_STRING;
            if (!closed) a.unterminatedString = true;
            i = j; prevWasFun = false; continue;
        }
        if (c == '`') { a.cls[i] = C_STRING; a.stack.push_back('`'); ++i; continue; }
        if (isDigit(c) || (c == '.' && isDigit(at(i + 1)) && (i == 0 || !isIdentChar(s[i - 1])))) {
            size_t j = i;
            while (j < n && (isDigit(s[j]) || s[j] == '.' || s[j] == '_')) {
                if (s[j] == '.' && !isDigit(at(j + 1))) break;
                ++j;
            }
            if (j < n && (s[j] == 'e' || s[j] == 'E') && (isDigit(at(j + 1)) || ((at(j + 1) == '-' || at(j + 1) == '+') && isDigit(at(j + 2))))) {
                j += 2; while (j < n && isDigit(s[j])) ++j;
            }
            for (size_t k = i; k < j; ++k) a.cls[k] = C_NUMBER;
            i = j; prevWasFun = false; continue;
        }
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < n && isIdentChar(s[j])) ++j;
            std::string word = toUtf8(s.substr(i, j - i));
            uint8_t k = C_PLAIN;
            auto it = kw.find(word);
            if (it != kw.end()) k = it->second;
            else {
                size_t p = j;
                while (p < n && (s[p] == ' ' || s[p] == '\t')) ++p;
                if (prevWasFun || (p < n && s[p] == '(')) k = C_FUNC;
            }
            for (size_t q = i; q < j; ++q) a.cls[q] = k;
            prevWasFun = (word == "fun");
            i = j; continue;
        }
        prevWasFun = false;
        if (c == '@') {
            size_t j = i + 1;
            while (j < n && (isIdentChar(s[j]) || s[j] == '.')) ++j;
            for (size_t k = i; k < j; ++k) a.cls[k] = C_DIRECTIVE;
            i = j; continue;
        }
        if (c == '#' && isIdentStart(at(i + 1))) {
            size_t j = i + 1;
            while (j < n && isIdentChar(s[j])) ++j;
            for (size_t k = i; k < j; ++k) a.cls[k] = C_HASH;
            i = j; continue;
        }
        if (c == '(' || c == '[' || c == '{') { a.stack.push_back(static_cast<char>(c)); a.cls[i] = C_PUNCT; ++i; continue; }
        if (c == ')' || c == ']' || c == '}') {
            char want = c == ')' ? '(' : c == ']' ? '[' : '{';
            char t2 = a.stack.empty() ? 0 : a.stack.back();
            if (t2 == want || (c == '}' && t2 == '$')) { a.stack.pop_back(); a.cls[i] = C_PUNCT; }
            else a.cls[i] = C_ERROR;
            ++i; continue;
        }
        if (c == ',' || c == ';' || c == ':' || c == '.') { a.cls[i] = C_PUNCT; ++i; continue; }
        if (c == '\\' && i + 1 == n) { a.trailingBackslash = true; ++i; continue; }
        a.cls[i] = C_OP; ++i;
    }
    if (a.stack.empty() && !a.unterminatedString) {
        size_t e = n;
        while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r' || a.cls[e - 1] == C_COMMENT)) --e;
        if (e > 0) {
            char32_t last = s[e - 1];
            if (a.cls[e - 1] == C_OP && (last == '+' || last == '-' || last == '*' || last == '/' || last == '%' ||
                                         last == '=' || last == '<' || last == '>' || last == '|' || last == ','))
                a.trailingOperator = true;
            else if (a.cls[e - 1] == C_PUNCT && last == ',')
                a.trailingOperator = true;
            else if (isIdentChar(last)) {
                size_t b = e;
                while (b > 0 && isIdentChar(s[b - 1])) --b;
                std::string w = toUtf8(s.substr(b, e - b));
                if (w == "and" || w == "or") a.trailingOperator = true;
            }
        }
    }
    return a;
}

// يحوّل مدخلاً متعدّد الأسطر إلى سطر واحد للتاريخ (يحذف تعليقات // لأنها تبتلع ما بعدها).
std::string flattenForHistory(const std::string& entry) {
    if (entry.find('\n') == std::string::npos) return entry;
    std::vector<std::string> out;
    for (const auto& line : splitLines(entry)) {
        U32 u = toU32(line);
        Analysis a = analyze(u);
        U32 kept;
        for (size_t i = 0; i < u.size(); ++i) if (a.cls[i] != C_COMMENT) kept.push_back(u[i]);
        std::string t = trim(toUtf8(kept));
        if (!t.empty()) out.push_back(t);
    }
    return joinLines(out, " ");
}

// ---------------------------------------------------------------------------
// التاريخ (محفوظ بين الجلسات؛ كل مدخل في سطر، مع تهريب \\ و \n)
// ---------------------------------------------------------------------------
struct History {
    std::vector<std::string> items;
    std::string path;
    bool persist = false;
    size_t maxItems = 2000;

    static std::string esc(const std::string& s) {
        std::string o;
        for (char c : s) { if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else o += c; }
        return o;
    }
    static std::string unesc(const std::string& s) {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) { ++i; o += (s[i] == 'n') ? '\n' : s[i]; }
            else o += s[i];
        }
        return o;
    }
    void load() {
        items.clear();
        if (!persist || path.empty()) return;
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) if (!line.empty()) items.push_back(unesc(line));
        if (items.size() > maxItems) { items.erase(items.begin(), items.end() - static_cast<long>(maxItems)); rewrite(); }
    }
    void rewrite() const {
        if (!persist || path.empty()) return;
        std::ofstream f(path, std::ios::trunc);
        for (const auto& e : items) f << esc(e) << "\n";
    }
    void add(const std::string& e) {
        if (e.empty() || (!items.empty() && items.back() == e)) return;
        items.push_back(e);
        if (persist && !path.empty()) {
            std::ofstream f(path, std::ios::app);
            f << esc(e) << "\n";
        }
        if (items.size() > maxItems + maxItems / 2) {
            items.erase(items.begin(), items.end() - static_cast<long>(maxItems));
            rewrite();
        }
    }
    void clear() { items.clear(); rewrite(); }
};

// ---------------------------------------------------------------------------
// الطرفية الخام (termios)
// ---------------------------------------------------------------------------
termios g_origTermios;
volatile sig_atomic_t g_haveOrig = 0;
volatile sig_atomic_t g_rawActive = 0;

void restoreTtyFromSignal() {
    if (g_haveOrig && g_rawActive) ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_origTermios);
}

void fatalSignal(int sig) {
    restoreTtyFromSignal();
    static const char seq[] = "\x1b[?2004l\r\n";
    ssize_t r = ::write(STDOUT_FILENO, seq, sizeof(seq) - 1); (void)r;
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

struct RawMode {
    bool active = false;
    bool bracketed = true;

    void enable() {
        if (active) return;
        if (!g_haveOrig) {
            if (::tcgetattr(STDIN_FILENO, &g_origTermios) != 0) return;
            g_haveOrig = 1;
        }
        termios raw = g_origTermios;
        raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return;
        active = true; g_rawActive = 1;
        if (bracketed) { ssize_t r = ::write(STDOUT_FILENO, "\x1b[?2004h", 8); (void)r; }
    }
    void disable() {
        if (!active) return;
        if (bracketed) { ssize_t r = ::write(STDOUT_FILENO, "\x1b[?2004l", 8); (void)r; }
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_origTermios);
        active = false; g_rawActive = 0;
    }
    ~RawMode() { disable(); }
};

int termCols() {
    winsize ws;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    const char* c = std::getenv("COLUMNS");
    if (c && std::atoi(c) > 0) return std::atoi(c);
    return 80;
}

void writeOut(const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t w = ::write(STDOUT_FILENO, s.data() + off, s.size() - off);
        if (w < 0) { if (errno == EINTR) continue; break; }
        off += static_cast<size_t>(w);
    }
}

// ---------------------------------------------------------------------------
// قراءة المفاتيح
// ---------------------------------------------------------------------------
struct Key {
    enum T {
        Char, Enter, Tab, BackTab, Backspace, Delete, Left, Right, Up, Down, Home, End, PgUp, PgDn,
        CtrlLeft, CtrlRight, AltB, AltF, AltD, AltBackspace, Ctrl, Esc, Paste, Eof, Mouse, None
    } t = None;
    char32_t ch = 0;
    std::string text;  // للّصق
    int mb = 0, mx = 0, my = 0;  // Mouse (SGR 1006): الزر، العمود، الصف (1-based)
    bool mrel = false;           // رُفع الزر
};

int readByte(int timeoutMs) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    for (;;) {
        int r = ::poll(&p, 1, timeoutMs);
        if (r < 0) { if (errno == EINTR) continue; return -2; }
        if (r == 0) return -1;
        unsigned char b;
        ssize_t n = ::read(STDIN_FILENO, &b, 1);
        if (n == 0) return -2;
        if (n < 0) { if (errno == EINTR) continue; return -2; }
        return b;
    }
}

Key readKey() {
    Key k;
    int b = readByte(-1);
    if (b == -2 || b == -1) { k.t = Key::Eof; return k; }
    if (b == 27) {
        int b2 = readByte(40);
        if (b2 < 0) { k.t = Key::Esc; return k; }
        if (b2 == '[') {
            std::string params;
            int fin = 0;
            for (;;) {
                int c = readByte(100);
                if (c < 0) { k.t = Key::Esc; return k; }
                if (c >= 0x40 && c <= 0x7E) { fin = c; break; }
                params.push_back(static_cast<char>(c));
                if (params.size() > 24) break;
            }
            if (fin == '~' && params == "200") {  // لصق مُحاط
                std::string data;
                const std::string endSeq = "\x1b[201~";
                for (;;) {
                    int c = readByte(2000);
                    if (c < 0) break;
                    data.push_back(static_cast<char>(c));
                    if (data.size() >= endSeq.size() && data.compare(data.size() - endSeq.size(), endSeq.size(), endSeq) == 0) {
                        data.erase(data.size() - endSeq.size());
                        break;
                    }
                }
                k.t = Key::Paste; k.text = data; return k;
            }
            if (!params.empty() && params[0] == '<' && (fin == 'M' || fin == 'm')) {
                int b = 0, x = 0, y = 0;
                if (std::sscanf(params.c_str() + 1, "%d;%d;%d", &b, &x, &y) == 3) {
                    k.t = Key::Mouse; k.mb = b; k.mx = x; k.my = y; k.mrel = (fin == 'm');
                    return k;
                }
            }
            bool ctrl = params.find(";5") != std::string::npos || params.find(";3") != std::string::npos;
            switch (fin) {
                case 'A': k.t = Key::Up; return k;
                case 'B': k.t = Key::Down; return k;
                case 'C': k.t = ctrl ? Key::CtrlRight : Key::Right; return k;
                case 'D': k.t = ctrl ? Key::CtrlLeft : Key::Left; return k;
                case 'H': k.t = Key::Home; return k;
                case 'F': k.t = Key::End; return k;
                case 'Z': k.t = Key::BackTab; return k;
                case '~': {
                    int num = std::atoi(params.c_str());
                    switch (num) {
                        case 1: case 7: k.t = Key::Home; return k;
                        case 4: case 8: k.t = Key::End; return k;
                        case 3: k.t = Key::Delete; return k;
                        case 5: k.t = Key::PgUp; return k;
                        case 6: k.t = Key::PgDn; return k;
                        default: break;
                    }
                    break;
                }
                default: break;
            }
            k.t = Key::None; return k;
        }
        if (b2 == 'O') {
            int c = readByte(100);
            switch (c) {
                case 'H': k.t = Key::Home; return k;
                case 'F': k.t = Key::End; return k;
                case 'A': k.t = Key::Up; return k;
                case 'B': k.t = Key::Down; return k;
                case 'C': k.t = Key::Right; return k;
                case 'D': k.t = Key::Left; return k;
                default: k.t = Key::None; return k;
            }
        }
        if (b2 == 'b') { k.t = Key::AltB; return k; }
        if (b2 == 'f') { k.t = Key::AltF; return k; }
        if (b2 == 'd') { k.t = Key::AltD; return k; }
        if (b2 == 127 || b2 == 8) { k.t = Key::AltBackspace; return k; }
        k.t = Key::None; return k;
    }
    if (b == '\r' || b == '\n') { k.t = Key::Enter; return k; }
    if (b == 9) { k.t = Key::Tab; return k; }
    if (b == 127 || b == 8) { k.t = Key::Backspace; return k; }
    if (b >= 1 && b <= 26) { k.t = Key::Ctrl; k.ch = static_cast<char32_t>('a' + b - 1); return k; }
    if (b < 32) { k.t = Key::None; return k; }
    if (b < 0x80) { k.t = Key::Char; k.ch = static_cast<char32_t>(b); return k; }
    // UTF-8 متعدد البايتات
    int extra = (b >> 5) == 0x6 ? 1 : (b >> 4) == 0xE ? 2 : (b >> 3) == 0x1E ? 3 : 0;
    std::string bytes(1, static_cast<char>(b));
    for (int i = 0; i < extra; ++i) {
        int c = readByte(100);
        if (c < 0) break;
        bytes.push_back(static_cast<char>(c));
    }
    U32 u = toU32(bytes);
    if (u.empty()) { k.t = Key::None; return k; }
    k.t = Key::Char; k.ch = u[0];
    return k;
}

// ---------------------------------------------------------------------------
// محرّر السطر
// ---------------------------------------------------------------------------
struct EditorConfig {
    bool highlight = true;
    bool hints = true;
};

using CompleteFn = std::function<std::vector<std::string>(const U32& buf, size_t cursor, size_t& wordStart)>;

class LineEditor {
public:
    enum class Result { Line, Eof, Interrupted };

    LineEditor(const Style& st, const EditorConfig& cfg, History& hist, CompleteFn complete)
        : st_(st), cfg_(cfg), hist_(hist), complete_(std::move(complete)) {}

    // يقرأ سطراً واحداً. إن لصق المستخدم نصاً متعدد الأسطر: يعيد أول سطر ويضع الباقي في [rest]
    // (واللصق الذي لا ينتهي بسطر جديد يبقى سطره الأخير في rest أيضاً؛ يقرّر المستدعي).
    Result read(const std::string& promptAnsi, int promptW, const std::string& prefill,
                std::string& out, std::vector<std::string>& rest, bool& restComplete) {
        promptAnsi_ = promptAnsi; promptW_ = promptW;
        buf_ = toU32(prefill); cur_ = buf_.size();
        viewStart_ = 0; histIdx_ = hist_.items.size(); navActive_ = false; lastWasTab_ = false;
        rest.clear(); restComplete = true;
        raw_.enable();
        render();
        for (;;) {
            Key k = readKey();
            bool wasTab = (k.t == Key::Tab);
            if (k.t != Key::Up && k.t != Key::Down) navActive_ = false;
            switch (k.t) {
                case Key::Eof: raw_.disable(); return Result::Eof;
                case Key::Enter:
                    cur_ = buf_.size();
                    renderFinal();
                    raw_.disable();
                    out = toUtf8(buf_);
                    return Result::Line;
                case Key::Paste: {
                    std::string t = k.text;
                    std::string norm;
                    for (size_t i = 0; i < t.size(); ++i) {
                        if (t[i] == '\r') { if (i + 1 < t.size() && t[i + 1] == '\n') continue; norm.push_back('\n'); }
                        else if (t[i] == '\t') norm += "    ";
                        else norm.push_back(t[i]);
                    }
                    if (norm.find('\n') == std::string::npos) { insertText(toU32(norm)); break; }
                    auto lines = splitLines(norm);
                    U32 tail(buf_.begin() + static_cast<long>(cur_), buf_.end());
                    buf_.erase(cur_);
                    insertText(toU32(lines[0]));
                    std::string first = toUtf8(buf_);
                    for (size_t i = 1; i < lines.size(); ++i) rest.push_back(lines[i]);
                    restComplete = norm.back() == '\n';
                    if (restComplete && !rest.empty()) rest.pop_back();  // العنصر الأخير الفارغ بعد \n الختامي
                    if (!tail.empty()) {
                        if (rest.empty()) { /* لصق سطر واحد منتهٍ بـ \n */ }
                        else rest.back() += toUtf8(tail);
                    }
                    cur_ = buf_.size();
                    renderFinal();
                    raw_.disable();
                    out = first;
                    return Result::Line;
                }
                case Key::Char:
                    insertChar(k.ch); break;
                case Key::Tab: doComplete(lastWasTab_); break;
                case Key::BackTab: break;
                case Key::Backspace: if (cur_ > 0) { buf_.erase(cur_ - 1, 1); --cur_; } break;
                case Key::Delete: if (cur_ < buf_.size()) buf_.erase(cur_, 1); break;
                case Key::Left: if (cur_ > 0) --cur_; break;
                case Key::Right:
                    if (cur_ < buf_.size()) ++cur_; else acceptHint();
                    break;
                case Key::Home: cur_ = 0; break;
                case Key::End: if (cur_ < buf_.size()) cur_ = buf_.size(); else acceptHint(); break;
                case Key::CtrlLeft: case Key::AltB: cur_ = wordBack(cur_); break;
                case Key::CtrlRight: case Key::AltF: cur_ = wordForward(cur_); break;
                case Key::AltD: { size_t e = wordForward(cur_); kill_ = toUtf8(buf_.substr(cur_, e - cur_)); buf_.erase(cur_, e - cur_); break; }
                case Key::AltBackspace: { size_t s = wordBack(cur_); kill_ = toUtf8(buf_.substr(s, cur_ - s)); buf_.erase(s, cur_ - s); cur_ = s; break; }
                case Key::Up: historyNav(-1); break;
                case Key::Down: historyNav(+1); break;
                case Key::PgUp: historyNav(-1); break;
                case Key::PgDn: historyNav(+1); break;
                case Key::Esc: break;
                case Key::Ctrl:
                    switch (k.ch) {
                        case 'a': cur_ = 0; break;
                        case 'e': if (cur_ < buf_.size()) cur_ = buf_.size(); else acceptHint(); break;
                        case 'b': if (cur_ > 0) --cur_; break;
                        case 'f': if (cur_ < buf_.size()) ++cur_; else acceptHint(); break;
                        case 'd':
                            if (buf_.empty()) { raw_.disable(); return Result::Eof; }
                            if (cur_ < buf_.size()) buf_.erase(cur_, 1);
                            break;
                        case 'c':
                            cur_ = buf_.size();
                            renderFinal();
                            writeOut(st_.dim() + "^C" + st_.reset());
                            raw_.disable();
                            out.clear();
                            return Result::Interrupted;
                        case 'k': kill_ = toUtf8(buf_.substr(cur_)); buf_.erase(cur_); break;
                        case 'u': kill_ = toUtf8(buf_.substr(0, cur_)); buf_.erase(0, cur_); cur_ = 0; break;
                        case 'w': { size_t s = wordBack(cur_); kill_ = toUtf8(buf_.substr(s, cur_ - s)); buf_.erase(s, cur_ - s); cur_ = s; break; }
                        case 'y': insertText(toU32(kill_)); break;
                        case 't':
                            if (cur_ > 0 && buf_.size() >= 2) {
                                size_t p = cur_ == buf_.size() ? cur_ - 1 : cur_;
                                std::swap(buf_[p - 1], buf_[p]);
                                cur_ = std::min(p + 1, buf_.size());
                            }
                            break;
                        case 'l': writeOut("\x1b[H\x1b[2J"); break;
                        case 'r': {
                            int r = reverseSearch();
                            if (r == 2) {
                                cur_ = buf_.size(); renderFinal(); raw_.disable();
                                out = toUtf8(buf_);
                                return Result::Line;
                            }
                            break;
                        }
                        case 'z':
                            raw_.disable();
                            writeOut("\r\n");
                            ::raise(SIGTSTP);
                            raw_.enable();
                            break;
                        default: break;
                    }
                    break;
                default: break;
            }
            lastWasTab_ = wasTab;
            render();
        }
    }

private:
    const Style& st_;
    const EditorConfig& cfg_;
    History& hist_;
    CompleteFn complete_;
    RawMode raw_;

    std::string promptAnsi_;
    int promptW_ = 0;
    U32 buf_;
    size_t cur_ = 0;
    size_t viewStart_ = 0;
    size_t histIdx_ = 0;
    bool navActive_ = false;
    bool lastWasTab_ = false;
    U32 navPrefix_;
    U32 navSaved_;
    std::string kill_;

    // ---- تحرير ----
    void insertText(const U32& t) {
        for (char32_t c : t) insertChar(c);
    }
    void insertChar(char32_t c) {
        if (c < 32) return;
        if (c == '}') {  // فك المسافة البادئة تلقائياً عند كتابة } في بداية السطر
            bool allSpaces = cur_ >= 4;
            for (size_t i = 0; i < cur_ && allSpaces; ++i) if (buf_[i] != ' ') allSpaces = false;
            if (allSpaces) { buf_.erase(cur_ - 4, 4); cur_ -= 4; }
        }
        buf_.insert(cur_, 1, c);
        ++cur_;
    }
    size_t wordBack(size_t p) const {
        while (p > 0 && !isIdentChar(buf_[p - 1])) --p;
        while (p > 0 && isIdentChar(buf_[p - 1])) --p;
        return p;
    }
    size_t wordForward(size_t p) const {
        while (p < buf_.size() && !isIdentChar(buf_[p])) ++p;
        while (p < buf_.size() && isIdentChar(buf_[p])) ++p;
        return p;
    }

    std::string currentHint() const {
        if (!cfg_.hints || buf_.empty() || cur_ != buf_.size()) return "";
        std::string b = toUtf8(buf_);
        for (size_t i = hist_.items.size(); i-- > 0;) {
            const std::string& h = hist_.items[i];
            if (h.size() > b.size() && h.compare(0, b.size(), b) == 0 && h.find('\n') == std::string::npos) return h.substr(b.size());
        }
        return "";
    }
    void acceptHint() {
        std::string h = currentHint();
        if (!h.empty()) { U32 u = toU32(h); buf_ += u; cur_ = buf_.size(); }
    }

    // ---- التاريخ: بحث بالبادئة إن كان في المحرّر نص ----
    void historyNav(int dir) {
        if (hist_.items.empty()) return;
        if (!navActive_) { navActive_ = true; navPrefix_ = buf_; navSaved_ = buf_; if (histIdx_ > hist_.items.size()) histIdx_ = hist_.items.size(); }
        std::string prefix = toUtf8(navPrefix_);
        long i = static_cast<long>(histIdx_);
        for (;;) {
            i += dir;
            if (i < 0) return;
            if (i >= static_cast<long>(hist_.items.size())) {
                histIdx_ = hist_.items.size();
                buf_ = navSaved_; cur_ = buf_.size();
                return;
            }
            const std::string& h = hist_.items[static_cast<size_t>(i)];
            if (prefix.empty() || startsWith(h, prefix)) {
                histIdx_ = static_cast<size_t>(i);
                buf_ = toU32(h); cur_ = buf_.size();
                return;
            }
        }
    }

    // ---- بحث عكسي Ctrl-R ----  يعيد: 0 إلغاء، 1 قبول للتحرير، 2 قبول وتنفيذ
    int reverseSearch() {
        U32 query;
        long idx = static_cast<long>(hist_.items.size());
        long found = -1;
        auto doSearch = [&](long from) {
            std::string q = toUtf8(query);
            found = -1;
            for (long i = std::min<long>(from, static_cast<long>(hist_.items.size()) - 1); i >= 0; --i) {
                if (q.empty() || hist_.items[static_cast<size_t>(i)].find(q) != std::string::npos) { found = i; break; }
            }
        };
        for (;;) {
            std::string match = found >= 0 ? hist_.items[static_cast<size_t>(found)] : "";
            std::string label = std::string("(") + tr("بحث", "reverse-search") + ")`" + toUtf8(query) + "': ";
            std::string line = label + match;
            U32 ul = toU32(line);
            int cols = termCols();
            std::string shown; int used = 0;
            for (char32_t c : ul) { int w = cpWidth(c); if (used + w > cols - 1) break; appendUtf8(shown, c); used += w; }
            writeOut("\r" + st_.dim() + shown + st_.reset() + "\x1b[K");
            Key k = readKey();
            if (k.t == Key::Char) { query.push_back(k.ch); doSearch(static_cast<long>(hist_.items.size()) - 1); if (found >= 0) idx = found; continue; }
            if (k.t == Key::Backspace) { if (!query.empty()) query.pop_back(); doSearch(static_cast<long>(hist_.items.size()) - 1); if (found >= 0) idx = found; continue; }
            if (k.t == Key::Ctrl && k.ch == 'r') { doSearch(idx - 1); if (found >= 0) idx = found; continue; }
            if (k.t == Key::Enter) { if (found >= 0) { buf_ = toU32(hist_.items[static_cast<size_t>(found)]); cur_ = buf_.size(); return 2; } return 0; }
            if (k.t == Key::Esc || (k.t == Key::Ctrl && (k.ch == 'g' || k.ch == 'c'))) return 0;
            if (found >= 0) { buf_ = toU32(hist_.items[static_cast<size_t>(found)]); cur_ = buf_.size(); }
            return 1;
        }
    }

    // ---- الإكمال ----
    void doComplete(bool second) {
        if (!complete_) return;
        size_t start = 0;
        std::vector<std::string> cands = complete_(buf_, cur_, start);
        if (cands.empty() || start > cur_) return;
        // أطول بادئة مشتركة
        std::string common = cands[0];
        for (const auto& c : cands) {
            size_t n = 0;
            while (n < common.size() && n < c.size() && common[n] == c[n]) ++n;
            common.resize(n);
        }
        U32 word = buf_.substr(start, cur_ - start);
        U32 uc = toU32(common);
        if (cands.size() == 1) {
            buf_.replace(start, cur_ - start, uc);
            cur_ = start + uc.size();
            return;
        }
        if (uc.size() > word.size()) {
            buf_.replace(start, cur_ - start, uc);
            cur_ = start + uc.size();
            if (!second) return;
        }
        if (!second && uc.size() <= word.size()) {
            // أول Tab بلا تقدّم: اعرض القائمة فوراً أيضاً
        }
        showCandidates(cands);
    }

    void showCandidates(const std::vector<std::string>& cands) {
        int cols = termCols();
        size_t maxw = 0;
        for (const auto& c : cands) maxw = std::max<size_t>(maxw, static_cast<size_t>(strWidth(c)));
        size_t colw = maxw + 2;
        size_t perRow = std::max<size_t>(1, static_cast<size_t>(cols) / colw);
        std::string o = "\r\n";
        size_t shownCount = 0;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (shownCount >= 120) { o += st_.dim() + "…" + st_.reset(); break; }
            o += cands[i];
            ++shownCount;
            size_t pad = colw - static_cast<size_t>(strWidth(cands[i]));
            if ((i + 1) % perRow == 0 || i + 1 == cands.size()) o += "\r\n";
            else o += std::string(pad, ' ');
        }
        writeOut(o);
    }

    // ---- الرسم ----
    struct Cell { char32_t c; uint8_t cls; int w; };

    std::vector<Cell> buildCells(bool withHint) const {
        std::vector<Cell> cells;
        std::vector<uint8_t> cls;
        if (cfg_.highlight) cls = analyze(buf_).cls; else cls.assign(buf_.size(), C_PLAIN);
        for (size_t i = 0; i < buf_.size(); ++i) cells.push_back({buf_[i], cls[i], cpWidth(buf_[i])});
        if (withHint) {
            std::string h = currentHint();
            if (!h.empty()) for (char32_t c : toU32(h)) cells.push_back({c, C_HINT, cpWidth(c)});
        }
        return cells;
    }

    void draw(const std::vector<Cell>& cells, bool placeCursor) {
        int cols = termCols();
        int avail = std::max(8, cols - promptW_ - 1);
        if (cur_ < viewStart_) viewStart_ = cur_;
        auto widthRange = [&](size_t a, size_t b) { int w = 0; for (size_t i = a; i < b && i < cells.size(); ++i) w += cells[i].w; return w; };
        while (viewStart_ < cur_ && widthRange(viewStart_, cur_) >= avail) ++viewStart_;
        std::string o = "\r" + promptAnsi_;
        int used = 0;
        int curStyle = -1;
        for (size_t i = viewStart_; i < cells.size(); ++i) {
            if (used + cells[i].w > avail) break;
            if (curStyle != cells[i].cls) {
                o += st_.reset();
                o += clsStyle(st_, cells[i].cls);
                curStyle = cells[i].cls;
            }
            appendUtf8(o, cells[i].c);
            used += cells[i].w;
        }
        o += st_.reset() + "\x1b[K";
        if (placeCursor) {
            int col = promptW_ + widthRange(viewStart_, cur_);
            o += "\r";
            if (col > 0) o += "\x1b[" + std::to_string(col) + "C";
        }
        writeOut(o);
    }

    void render() { draw(buildCells(true), true); }
    void renderFinal() { draw(buildCells(false), false); writeOut("\r\n"); }
};

// ---------------------------------------------------------------------------
// مقاطعة Ctrl-C أثناء تنفيذ برنامج: المحرك لا يملك علَم إلغاء، لكن لديه سقف عدد
// جمل (setExecutionBudget). نضبطه على 1 من معالج الإشارة فيرمي أول execute() قادم
// RinError ويتفكك المكدّس بأمان، ثم نعيد السقف إلى 0 (الحالة العامة محفوظة).
// ضغطتان متتاليتان تُنهيان العملية فوراً (لبرنامج عالق داخل native).
// ---------------------------------------------------------------------------
volatile sig_atomic_t g_interrupted = 0;
volatile sig_atomic_t g_sigintCount = 0;
rin::Interpreter* volatile g_runningInterp = nullptr;

void onSigint(int) {
    g_interrupted = 1;
    g_sigintCount = g_sigintCount + 1;
    if (g_runningInterp) g_runningInterp->setExecutionBudget(1);
    if (g_sigintCount >= 2) {
        static const char msg[] = "\r\n^C^C\r\n";
        ssize_t r = ::write(STDERR_FILENO, msg, sizeof(msg) - 1); (void)r;
        _exit(130);
    }
}

// ---------------------------------------------------------------------------
// indsin داخل الطرفية: نفس مسار المحرّك (rin_indsin_session_*: layout + Dye + rasterizer الوحيد)
// يُعرَض بنصف-كتل ملوّنة (▀ بلونين: أعلى/أسفل) بدل نافذة. النصوص تُرسَم كحروف حقيقية فوق
// الخلفية (من أوامر الرسم TEXT_RUN) فتبقى مقروءة؛ والفأرة (SGR 1006) تُرسل tap/hover/long-press
// إلى جلسة indsin نفسها (Needle)، فتعمل onTap والحالة (warp) فعلياً.
// ---------------------------------------------------------------------------
int termRows() {
    winsize ws;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    const char* r = std::getenv("LINES");
    if (r && std::atoi(r) > 0) return std::atoi(r);
    return 24;
}

struct PaintOp {
    std::string op, color, text;
    double x = 0, y = 0, w = 0, h = 0, fontSize = 0;
    int align = 0;
    bool singleLine = false;
};

std::vector<PaintOp> parsePaint(const std::string& j) {
    std::vector<PaintOp> out;
    size_t p = j.find("\"paint\":[");
    if (p == std::string::npos) return out;
    p += 9;
    const size_t n = j.size();
    auto skipWs = [&]() { while (p < n && (j[p] == ' ' || j[p] == '\n' || j[p] == '\t' || j[p] == '\r')) ++p; };
    auto readStr = [&](std::string& s) {
        ++p; s.clear();
        while (p < n && j[p] != '"') {
            if (j[p] == '\\' && p + 1 < n) {
                char e = j[p + 1]; p += 2;
                if (e == 'n') s += '\n';
                else if (e == 't') s += '\t';
                else if (e == 'u' && p + 4 <= n) { appendUtf8(s, static_cast<char32_t>(std::strtoul(j.substr(p, 4).c_str(), nullptr, 16))); p += 4; }
                else s += e;
            } else s += j[p++];
        }
        if (p < n) ++p;
    };
    for (;;) {
        skipWs();
        if (p >= n || j[p] == ']') break;
        if (j[p] == ',') { ++p; continue; }
        if (j[p] != '{') break;
        ++p;
        PaintOp op;
        for (;;) {
            skipWs();
            if (p >= n) break;
            if (j[p] == '}') { ++p; break; }
            if (j[p] == ',') { ++p; continue; }
            if (j[p] != '"') { ++p; continue; }
            std::string key; readStr(key);
            skipWs(); if (p < n && j[p] == ':') ++p; skipWs();
            if (p < n && j[p] == '"') {
                std::string v; readStr(v);
                if (key == "op") op.op = v; else if (key == "color") op.color = v; else if (key == "text") op.text = v;
            } else {
                size_t st = p;
                while (p < n && j[p] != ',' && j[p] != '}') ++p;
                std::string v = j.substr(st, p - st);
                double d = std::atof(v.c_str());
                if (key == "x") op.x = d; else if (key == "y") op.y = d; else if (key == "w") op.w = d; else if (key == "h") op.h = d;
                else if (key == "fontSize") op.fontSize = d; else if (key == "align") op.align = static_cast<int>(d);
                else if (key == "singleLine") op.singleLine = (v == "true");
            }
        }
        out.push_back(std::move(op));
    }
    return out;
}

std::string takeCString(char* c) {
    if (!c) return "";
    std::string s = c;
    rin_free_string(c);
    return s;
}

struct RGB { uint8_t r = 0, g = 0, b = 0; };
bool operator==(const RGB& a, const RGB& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

struct TCell {
    RGB top, bot, fg;
    std::string txt;       // حرف (مع علاماته) إن كانت خلية نص
    bool hasText = false;
    bool skip = false;     // الخلية الثانية من حرف عريض
};

struct Frame {
    int cols = 0, rows = 0;
    double fx = 1;
    std::vector<TCell> cells;
    TCell& at(int r, int c) { return cells[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c)]; }
};

RGB parseHexColor(const std::string& h) {
    RGB c;
    if (h.size() >= 7 && h[0] == '#') {
        unsigned v = static_cast<unsigned>(std::strtoul(h.substr(1, 6).c_str(), nullptr, 16));
        c.r = static_cast<uint8_t>((v >> 16) & 255); c.g = static_cast<uint8_t>((v >> 8) & 255); c.b = static_cast<uint8_t>(v & 255);
    }
    return c;
}

RGB avgBlock(const unsigned char* rgb, int W, int H, double x0, double y0, double x1, double y1) {
    int ix0 = std::max(0, static_cast<int>(x0)), iy0 = std::max(0, static_cast<int>(y0));
    int ix1 = std::min(W, std::max(ix0 + 1, static_cast<int>(x1))), iy1 = std::min(H, std::max(iy0 + 1, static_cast<int>(y1)));
    if (ix0 >= W || iy0 >= H) return RGB{};
    long r = 0, g = 0, b = 0, n = 0;
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const unsigned char* px = rgb + (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 3;
            r += px[0]; g += px[1]; b += px[2]; ++n;
        }
    if (n == 0) return RGB{};
    return RGB{static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n)};
}

// اللون الأكثر تكراراً داخل مستطيل (الخلفية تحت النص: بكسلات الخط قليلة فلا تُهيمن).
RGB modeColor(const unsigned char* rgb, int W, int H, double x0, double y0, double x1, double y1) {
    int ix0 = std::max(0, static_cast<int>(x0)), iy0 = std::max(0, static_cast<int>(y0));
    int ix1 = std::min(W, static_cast<int>(x1) + 1), iy1 = std::min(H, static_cast<int>(y1) + 1);
    if (ix0 >= ix1 || iy0 >= iy1) return RGB{};
    int nx = std::min(24, ix1 - ix0), ny = std::min(16, iy1 - iy0);
    std::map<uint32_t, int> cnt;
    uint32_t best = 0; int bestN = -1;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            int x = ix0 + (ix1 - ix0) * i / nx, y = iy0 + (iy1 - iy0) * j / ny;
            const unsigned char* px = rgb + (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 3;
            uint32_t k = (static_cast<uint32_t>(px[0]) << 16) | (static_cast<uint32_t>(px[1]) << 8) | px[2];
            int c = ++cnt[k];
            if (c > bestN) { bestN = c; best = k; }
        }
    return RGB{static_cast<uint8_t>(best >> 16), static_cast<uint8_t>((best >> 8) & 255), static_cast<uint8_t>(best & 255)};
}

bool isRtlText(const U32& u) {
    for (char32_t c : u) {
        if ((c >= 0x590 && c <= 0x8FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) return true;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return false;
    }
    return false;
}

std::vector<U32> wrapText(const U32& text, int width, bool single) {
    std::vector<U32> lines;
    std::vector<U32> paras; U32 cur;
    for (char32_t c : text) { if (c == '\n') { paras.push_back(cur); cur.clear(); } else cur.push_back(c); }
    paras.push_back(cur);
    for (auto& para : paras) {
        if (single || strWidth(para) <= width) { lines.push_back(para); continue; }
        U32 line; int lw = 0;
        size_t i = 0;
        while (i < para.size()) {
            size_t j = i;
            while (j < para.size() && para[j] != ' ') ++j;
            U32 word = para.substr(i, j - i);
            int ww = strWidth(word);
            if (lw > 0 && lw + 1 + ww > width) { lines.push_back(line); line.clear(); lw = 0; }
            if (lw > 0) { line.push_back(' '); ++lw; }
            while (ww > width && width > 0) {  // كلمة أطول من السطر
                U32 part; int pw = 0; size_t k = 0;
                while (k < word.size() && pw + cpWidth(word[k]) <= width) { pw += cpWidth(word[k]); part.push_back(word[k]); ++k; }
                if (k == 0) break;
                if (!line.empty()) { lines.push_back(line); line.clear(); lw = 0; }
                lines.push_back(part); word = word.substr(k); ww = strWidth(word);
            }
            line += word; lw += ww;
            i = j + 1;
        }
        lines.push_back(line);
    }
    return lines;
}

U32 clipWidth(const U32& s, int width, bool ellipsis) {
    if (strWidth(s) <= width) return s;
    U32 out; int w = 0;
    int lim = ellipsis ? width - 1 : width;
    for (char32_t c : s) { int cw = cpWidth(c); if (w + cw > lim) break; out.push_back(c); w += cw; }
    if (ellipsis && width > 0) out.push_back(0x2026);
    return out;
}

Frame buildFrame(const unsigned char* rgb, int W, int H, int termCols, const std::vector<PaintOp>& ops) {
    Frame f;
    f.fx = std::max(1.0, static_cast<double>(W) / std::max(1, termCols));
    f.cols = std::min(termCols, std::max(1, static_cast<int>(std::ceil(W / f.fx))));
    f.rows = std::max(1, static_cast<int>(std::ceil(H / (2 * f.fx))));
    f.cells.assign(static_cast<size_t>(f.cols) * static_cast<size_t>(f.rows), TCell{});
    for (int r = 0; r < f.rows; ++r)
        for (int c = 0; c < f.cols; ++c) {
            TCell& t = f.at(r, c);
            double x0 = c * f.fx, x1 = (c + 1) * f.fx;
            double y0 = 2.0 * r * f.fx, ym = y0 + f.fx, y1 = ym + f.fx;
            t.top = avgBlock(rgb, W, H, x0, y0, x1, ym);
            t.bot = avgBlock(rgb, W, H, x0, ym, x1, y1);
        }
    // نصوص حقيقية فوق الخلفية
    for (const auto& op : ops) {
        if (op.op != "text" || op.text.empty()) continue;
        int c0 = std::max(0, static_cast<int>(std::floor(op.x / f.fx + 0.5)));
        int cw = std::max(1, static_cast<int>(std::floor(op.w / f.fx + 0.5)));
        int r0 = std::max(0, static_cast<int>(std::floor(op.y / (2 * f.fx))));
        int rowsAvail = std::max(1, static_cast<int>(std::floor(op.h / (2 * f.fx) + 0.5)));
        if (c0 >= f.cols || r0 >= f.rows) continue;
        cw = std::min(cw, f.cols - c0);
        rowsAvail = std::min(rowsAvail, f.rows - r0);
        RGB bg = modeColor(rgb, W, H, op.x, op.y, op.x + op.w, op.y + op.h);
        for (int r = r0; r < r0 + rowsAvail; ++r)
            for (int c = c0; c < c0 + cw; ++c) { TCell& t = f.at(r, c); t.top = t.bot = bg; t.hasText = false; t.txt.clear(); t.skip = false; }
        U32 text = toU32(op.text);
        bool rtl = isRtlText(text);
        std::vector<U32> lines = wrapText(text, cw, op.singleLine);
        if (static_cast<int>(lines.size()) > rowsAvail) {
            lines.resize(static_cast<size_t>(rowsAvail));
            if (!lines.empty()) lines.back() = clipWidth(lines.back() + U32(1, 0x2026) , cw, true);
        }
        int vOff = std::max(0, (rowsAvail - static_cast<int>(lines.size())) / 2);
        RGB fg = parseHexColor(op.color);
        for (size_t li = 0; li < lines.size(); ++li) {
            U32 line = clipWidth(lines[li], cw, op.singleLine || lines.size() == 1);
            int lw = strWidth(line);
            int startCol;
            bool alignEnd = (op.align == 2);
            if (op.align == 1) startCol = (cw - lw) / 2;
            else if (alignEnd) startCol = rtl ? 0 : cw - lw;
            else startCol = rtl ? cw - lw : 0;
            int r = r0 + vOff + static_cast<int>(li);
            if (r >= f.rows) break;
            int c = c0 + std::max(0, startCol);
            for (char32_t ch : line) {
                int w = cpWidth(ch);
                if (c >= f.cols) break;
                if (w == 0) {
                    if (c > 0) { TCell& prev = f.at(r, c - 1); if (prev.hasText) appendUtf8(prev.txt, ch); }
                    continue;
                }
                TCell& t = f.at(r, c);
                t.hasText = true; t.txt.clear(); appendUtf8(t.txt, ch); t.fg = fg; t.top = t.bot = bg;
                if (w == 2 && c + 1 < f.cols) { TCell& t2 = f.at(r, c + 1); t2.hasText = true; t2.skip = true; t2.txt.clear(); t2.fg = fg; t2.top = t2.bot = bg; }
                c += w;
            }
        }
    }
    return f;
}

struct ColorMode { bool color = true; bool truecolor = true; bool utf8 = true; };

std::string sgrColor(bool fg, const RGB& c, bool truecolor) {
    if (truecolor) return std::string("\x1b[") + (fg ? "38" : "48") + ";2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
    auto lv = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
    int idx = 16 + 36 * lv(c.r) + 6 * lv(c.g) + lv(c.b);
    return std::string("\x1b[") + (fg ? "38" : "48") + ";5;" + std::to_string(idx) + "m";
}

// صفوف [from, from+n) من الإطار كنص ANSI. sep = فاصل الأسطر (\n للتفريغ، أو تموضع مؤشر للـ TUI).
std::string renderRows(Frame& f, int from, int n, const ColorMode& cm, const std::function<std::string(int)>& rowPrefix, const std::string& rowSuffix) {
    std::string out;
    for (int r = from; r < from + n; ++r) {
        out += rowPrefix(r - from);
        if (r < 0 || r >= f.rows) { out += (cm.color ? "\x1b[0m" : "") ; out += rowSuffix; continue; }
        if (!cm.color) {
            std::string line;
            for (int c = 0; c < f.cols; ++c) { TCell& t = f.at(r, c); if (t.skip) continue; line += t.hasText ? t.txt : " "; }
            size_t e = line.find_last_not_of(' ');
            out += (e == std::string::npos ? "" : line.substr(0, e + 1));
            out += rowSuffix;
            continue;
        }
        bool haveFg = false, haveBg = false; RGB cf, cb;
        for (int c = 0; c < f.cols; ++c) {
            TCell& t = f.at(r, c);
            if (t.skip) continue;
            if (t.hasText) {
                if (!haveBg || !(cb == t.top)) { out += sgrColor(false, t.top, cm.truecolor); cb = t.top; haveBg = true; }
                if (!haveFg || !(cf == t.fg)) { out += sgrColor(true, t.fg, cm.truecolor); cf = t.fg; haveFg = true; }
                out += t.txt;
            } else if (cm.utf8) {
                if (!haveFg || !(cf == t.top)) { out += sgrColor(true, t.top, cm.truecolor); cf = t.top; haveFg = true; }
                if (!haveBg || !(cb == t.bot)) { out += sgrColor(false, t.bot, cm.truecolor); cb = t.bot; haveBg = true; }
                out += "\xE2\x96\x80";  // ▀
            } else {
                if (!haveBg || !(cb == t.top)) { out += sgrColor(false, t.top, cm.truecolor); cb = t.top; haveBg = true; }
                out += " ";
            }
        }
        out += "\x1b[0m";
        out += rowSuffix;
    }
    return out;
}

struct IndsinArgs {
    std::string path;
    int width = 0;      // 0 = تلقائي من عرض الطرفية
    bool dump = false;  // إطار واحد إلى stdout بلا TUI
    int cols = 0;       // لـ --dump
    bool plain = false; // بلا ألوان (نص فقط) في --dump
};

bool parseIndsinArgs(const std::vector<std::string>& args, IndsinArgs& o, std::string& err) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--dump") o.dump = true;
        else if (a == "--plain") { o.dump = true; o.plain = true; }
        else if ((a == "--width" || a == "-w") && i + 1 < args.size()) o.width = std::atoi(args[++i].c_str());
        else if (a == "--cols" && i + 1 < args.size()) o.cols = std::atoi(args[++i].c_str());
        else if (!a.empty() && a[0] == '-') { err = std::string(tr("خيار غير معروف: ", "unknown option: ")) + a; return false; }
        else o.path = expandTilde(a);
    }
    return true;
}

long long fileMtimeNs(const std::string& p) {
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) return 0;
    return static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
}

bool inputReady(int ms) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    int r = ::poll(&p, 1, ms);
    return r > 0 && (p.revents & POLLIN);
}

// يشغّل indsin داخل الطرفية. يعيد 0 عند النجاح.
int runIndsin(const IndsinArgs& a, const Style& st, bool utf8) {
    std::string src;
    if (a.path.empty()) { std::fprintf(stderr, "%s\n", tr("الاستخدام: :indsin <ملف.rin> [--width N] [--dump|--plain]", "usage: :indsin <file.rin> [--width N] [--dump|--plain]")); return 2; }
    if (!readFileAll(a.path, src)) { std::fprintf(stderr, "%s%s\n", tr("تعذّر فتح الملف: ", "cannot open file: "), a.path.c_str()); return 2; }

    const bool interactive = !a.dump && ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO);
    int cols = a.cols > 0 ? a.cols : termCols();
    int W = a.width > 0 ? a.width : std::min(480, std::max(200, cols * 2));

    ColorMode cm;
    // --dump صريح = ANSI دائماً (حتى عبر أنبوب) إلا مع --plain أو NO_COLOR؛ التفاعلي يتبع ألوان الجلسة.
    cm.color = a.plain ? false : (interactive ? st.on : std::getenv("NO_COLOR") == nullptr);
    cm.utf8 = utf8;
    {
        const char* ct = std::getenv("COLORTERM");
        const char* tm = std::getenv("TERM");
        cm.truecolor = !(std::getenv("RIN_TRUECOLOR") && std::strcmp(std::getenv("RIN_TRUECOLOR"), "0") == 0) &&
                       !(tm && (std::strcmp(tm, "linux") == 0 || std::strcmp(tm, "vt100") == 0)) &&
                       !(ct == nullptr && tm && std::strstr(tm, "256color") != nullptr && std::strstr(tm, "xterm-256color") == nullptr);
    }

    void* sess = rin_indsin_session_create(src.c_str(), W);
    if (!sess) { std::fprintf(stderr, "indsin: session create failed\n"); return 1; }
    auto fail = [&](const std::string& json) -> bool {
        if (json.compare(0, 9, "{\"error\":") != 0) return false;
        size_t q = json.find('"', 9);
        size_t e = q == std::string::npos ? q : json.find('"', q + 1);
        std::string msg = (q != std::string::npos && e != std::string::npos) ? json.substr(q + 1, e - q - 1) : json;
        size_t l = json.find("\"line\":");
        std::fprintf(stderr, "indsin: %s%s\n", msg.c_str(), l != std::string::npos ? (std::string("  (line ") + std::to_string(std::atoi(json.c_str() + l + 7)) + ")").c_str() : "");
        return true;
    };
    std::string js = takeCString(rin_indsin_session_render_json(sess));
    if (fail(js)) { rin_indsin_session_free(sess); return 1; }

    auto makeFrame = [&](int termC, Frame& out, std::string& jsonOut) -> bool {
        int pw = 0, ph = 0;
        unsigned char* buf = rin_indsin_session_render_rgb(sess, &pw, &ph);
        if (!buf) return false;
        jsonOut = takeCString(rin_indsin_session_render_json(sess));
        out = buildFrame(buf, pw, ph, termC, parsePaint(jsonOut));
        rin_indsin_free_buffer(buf);
        return true;
    };

    Frame frame; std::string curJson;
    if (!makeFrame(cols, frame, curJson)) { std::fprintf(stderr, "indsin: %s\n", tr("لا توجد واجهة (لا جذر @view)", "nothing to render (no @view root)")); rin_indsin_session_free(sess); return 1; }

    if (!interactive) {
        std::string o = renderRows(frame, 0, frame.rows, cm, [](int) { return std::string(); }, "\n");
        std::fwrite(o.data(), 1, o.size(), stdout);
        rin_indsin_session_free(sess);
        return 0;
    }

    // ---------- تفاعلي ----------
    RawMode raw; raw.bracketed = false; raw.enable();
    writeOut("\x1b[?1049h\x1b[?25l\x1b[?1000h\x1b[?1003h\x1b[?1006h");
    int scroll = 0;
    std::string status = std::string(tr("جاهز", "ready"));
    long long mtime = fileMtimeNs(a.path);
    auto lastPoll = Clock::now();
    auto lastClick = Clock::now() - std::chrono::seconds(10);
    int lastClickC = -1, lastClickR = -1;
    int hovC = -1, hovR = -1;
    bool animating = false, dirty = true, running = true;
    int curCols = cols, curRows = termRows();
    std::string baseName = a.path.substr(a.path.find_last_of('/') == std::string::npos ? 0 : a.path.find_last_of('/') + 1);

    auto toPx = [&](int c1, int r1, double& x, double& y) {   // خلية 1-based على الشاشة → بكسل في الجلسة
        x = (c1 - 0.5) * frame.fx;
        y = (static_cast<double>(scroll + (r1 - 1)) * 2 + 1) * frame.fx;
    };
    auto absorb = [&](const std::string& res) {
        if (res.find("\"media\":{") != std::string::npos)   // pickMedia/uploadMedia: يحتاج منتقي ملفات المضيف
            status = std::string("! ") + tr("طلب وسائط (اختيار/رفع ملف) غير مدعوم داخل الطرفية — شغّله من التطبيق", "media request (file pick/upload) is not supported in the terminal - run it in the app");
        size_t e = res.find("\"error\":\"");
        if (e != std::string::npos && res.compare(0, 9, "{\"error\":") != 0) {
            size_t q = res.find('"', e + 9);
            if (q != std::string::npos) status = "! " + res.substr(e + 9, q - e - 9);
        }
        dirty = true;
    };
    auto draw = [&]() {
        curCols = termCols(); curRows = termRows();
        int vrows = std::max(1, curRows - 1);
        Frame nf;
        std::string nj;
        if (makeFrame(curCols, nf, nj)) { frame = std::move(nf); curJson = nj; }
        int maxScroll = std::max(0, frame.rows - vrows);
        scroll = std::min(std::max(0, scroll), maxScroll);
        std::string o = "\x1b[?25l";
        o += renderRows(frame, scroll, vrows, cm, [](int i) { return "\x1b[" + std::to_string(i + 1) + ";1H\x1b[2K"; }, "");
        std::string bar = " q " + std::string(tr("خروج", "quit")) + "  r " + tr("إعادة تحميل", "reload") + "  +/- " + tr("عرض", "width") + "  ↑↓ " + tr("تمرير", "scroll") + "  s png  |  " +
                          baseName + "  " + std::to_string(W) + "px  |  " + status;
        U32 ub = clipWidth(toU32(bar), curCols, false);
        o += "\x1b[" + std::to_string(curRows) + ";1H\x1b[0m\x1b[7m" + toUtf8(ub) + std::string(static_cast<size_t>(std::max(0, curCols - strWidth(ub))), ' ') + "\x1b[0m";
        writeOut(o);
        dirty = false;
    };
    auto recreate = [&](int newW) {
        void* ns = rin_indsin_session_create(src.c_str(), newW);
        if (!ns) return;
        std::string j = takeCString(rin_indsin_session_render_json(ns));
        if (j.compare(0, 9, "{\"error\":") == 0) { rin_indsin_session_free(ns); status = "! " + j; return; }
        rin_indsin_session_free(sess); sess = ns; W = newW;
        status = tr("تغيّر العرض (حالة warp أُعيدت)", "width changed (warp state reset)");
        dirty = true;
    };

    while (running) {
        if (dirty) draw();
        int timeout = animating ? 40 : 100;
        if (inputReady(timeout)) {
            Key k = readKey();
            if (k.t == Key::Eof) break;
            if (k.t == Key::Esc || (k.t == Key::Char && (k.ch == 'q' || k.ch == 'Q')) || (k.t == Key::Ctrl && (k.ch == 'c' || k.ch == 'd'))) break;
            else if (k.t == Key::Char && (k.ch == 'r' || k.ch == 'R')) {
                std::string ns;
                if (readFileAll(a.path, ns)) { src = ns; absorb(takeCString(rin_indsin_session_update_source(sess, src.c_str()))); mtime = fileMtimeNs(a.path); status = tr("أُعيد التحميل", "reloaded"); }
            }
            else if (k.t == Key::Char && (k.ch == '+' || k.ch == '=')) recreate(std::min(1200, W + 40));
            else if (k.t == Key::Char && (k.ch == '-' || k.ch == '_')) recreate(std::max(120, W - 40));
            else if (k.t == Key::Char && (k.ch == 's' || k.ch == 'S')) {
                std::string out = a.path + ".png";
                status = rin_indsin_session_export_png(sess, out.c_str()) ? (std::string(tr("حُفظت ", "saved ")) + out) : std::string("! png");
                dirty = true;
            }
            else if (k.t == Key::Up) { scroll -= 1; dirty = true; }
            else if (k.t == Key::Down) { scroll += 1; dirty = true; }
            else if (k.t == Key::PgUp) { scroll -= std::max(1, curRows - 2); dirty = true; }
            else if (k.t == Key::PgDn || (k.t == Key::Char && k.ch == ' ')) { scroll += std::max(1, curRows - 2); dirty = true; }
            else if (k.t == Key::Home) { scroll = 0; dirty = true; }
            else if (k.t == Key::End) { scroll = 1 << 20; dirty = true; }
            else if (k.t == Key::Ctrl && k.ch == 'l') dirty = true;
            else if (k.t == Key::Mouse) {
                int b = k.mb & ~(4 | 8 | 16);   // نتجاهل مفاتيح التعديل
                int col = k.mx, row = k.my;
                if (row >= curRows) continue;   // شريط الحالة
                double x, y; toPx(col, row, x, y);
                if (b == 64) { scroll -= 3; dirty = true; }
                else if (b == 65) { scroll += 3; dirty = true; }
                else if (b == 0 && !k.mrel) {
                    auto now = Clock::now();
                    bool dbl = (col == lastClickC && row == lastClickR && now - lastClick < std::chrono::milliseconds(350));
                    absorb(takeCString(rin_indsin_session_tap(sess, x, y)));
                    if (dbl) absorb(takeCString(rin_indsin_session_double_tap(sess, x, y)));
                    lastClick = now; lastClickC = col; lastClickR = row;
                    std::string t = takeCString(rin_indsin_session_tick(sess));
                    animating = t.find("\"animating\":true") != std::string::npos;
                }
                else if (b == 2 && !k.mrel) { absorb(takeCString(rin_indsin_session_long_press(sess, x, y))); }
                else if (b == 35 || b == 32) {   // حركة
                    if (col != hovC || row != hovR) {
                        if (hovC > 0) { double px, py; toPx(hovC, hovR, px, py); takeCString(rin_indsin_session_hover(sess, px, py, 0)); }
                        absorb(takeCString(rin_indsin_session_hover(sess, x, y, 1)));
                        hovC = col; hovR = row;
                    }
                }
            }
        } else if (animating) {
            std::string t = takeCString(rin_indsin_session_tick(sess));
            animating = t.find("\"animating\":true") != std::string::npos;
            dirty = true;
        }
        auto now = Clock::now();
        if (now - lastPoll > std::chrono::milliseconds(400)) {   // إعادة تحميل حيّة عند حفظ الملف
            lastPoll = now;
            long long m = fileMtimeNs(a.path);
            if (m != 0 && m != mtime) {
                mtime = m;
                std::string ns;
                if (readFileAll(a.path, ns)) { src = ns; absorb(takeCString(rin_indsin_session_update_source(sess, src.c_str()))); status = tr("إعادة تحميل حيّة", "live reload"); }
            }
            if (termCols() != curCols || termRows() != curRows) dirty = true;
        }
    }
    writeOut("\x1b[?1006l\x1b[?1003l\x1b[?1000l\x1b[?25h\x1b[?1049l\x1b[0m");
    raw.disable();
    rin_indsin_session_free(sess);
    return 0;
}

// ---------------------------------------------------------------------------
// الجلسة
// ---------------------------------------------------------------------------
struct Setting { const char* name; const char* values; const char* ar; const char* en; };

class Session {
public:
    explicit Session(const Options& o) : opt_(o) {
        style_.dark = (o.theme != "light");
        const char* term = std::getenv("TERM");
        style_.c256 = !(term && (std::strcmp(term, "linux") == 0 || std::strcmp(term, "vt100") == 0));
        interactive_ = !o.forceBatch && ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO);
        bool termOk = term && std::strcmp(term, "dumb") != 0;
        style_.on = o.color && ::isatty(STDOUT_FILENO) && std::getenv("NO_COLOR") == nullptr && termOk;
        utf8_ = localeIsUtf8();
        editorCfg_.highlight = o.highlight && style_.on;
        editorCfg_.hints = o.hints && style_.on;
        autoprint_ = o.autoprint;
        timing_ = o.timing;
        history_.persist = o.useHistory && interactive_;
        history_.path = o.historyFile.empty() ? defaultHistoryPath() : o.historyFile;
        if (history_.persist) {
            auto slash = history_.path.find_last_of('/');
            if (slash != std::string::npos && slash > 0) mkdirP(history_.path.substr(0, slash));
            history_.load();
        }
        resetInterpreter();
    }

    int run() {
        if (interactive_) installSigint();
        if (interactive_ && opt_.banner) printBanner();
        runRc();
        for (const auto& code : opt_.evalFirst) { execEntry(code, true); }
        for (const auto& f : opt_.preload) { if (!loadFile(f)) failures_++; }
        int code = interactive_ ? loopInteractive() : loopBatch();
        return code;
    }

private:
    Options opt_;
    Style style_;
    EditorConfig editorCfg_;
    History history_;
    bool interactive_ = false;
    bool utf8_ = true;
    bool autoprint_ = true;
    std::string timing_ = "auto";
    bool indent_ = true;

    std::unique_ptr<rin::Interpreter> interp_;
    bool underscoreDefined_ = false;
    std::set<std::string> baseline_;   // ثوابت/مدمجات المحرك الموجودة قبل أي مدخل (تُخفى من :vars)
    bool lastOk_ = true;
    bool atLineStart_ = true;
    bool quiet_ = false;        // يبتلع مخرجات البرنامج (:time N)
    int counter_ = 1;
    int failures_ = 0;
    int entryCounter_ = 0;
    std::string lastLoaded_;
    std::vector<std::string> accepted_;   // مدخلات نجحت (لـ :save)
    std::vector<std::string> pending_;    // أسطر ملصوقة تنتظر المعالجة
    std::string prefillNext_;
    bool quit_ = false;
    int exitCode_ = 0;
    Clock::time_point started_ = Clock::now();

    // ---------- مساعدات ----------
    static bool localeIsUtf8() {
        for (const char* v : {"LC_ALL", "LC_CTYPE", "LANG"}) {
            const char* e = std::getenv(v);
            if (e && *e) {
                std::string s = e;
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s.find("utf-8") != std::string::npos || s.find("utf8") != std::string::npos;
            }
        }
        return true;
    }

    static std::string defaultHistoryPath() {
        if (const char* e = std::getenv("RIN_HISTFILE")) return e;
        if (const char* x = std::getenv("XDG_STATE_HOME")) if (*x) return std::string(x) + "/rin/history";
        if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state/rin/history";
        return "";
    }
    static std::string rcPath() {
        if (const char* e = std::getenv("RIN_TERMINAL_RC")) return e;
        if (const char* x = std::getenv("XDG_CONFIG_HOME")) if (*x) return std::string(x) + "/rin/terminal.rc";
        if (const char* h = std::getenv("HOME")) return std::string(h) + "/.config/rin/terminal.rc";
        return "";
    }

    void print(const std::string& s) {
        if (s.empty()) return;
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fflush(stdout);
        atLineStart_ = s.back() == '\n';
    }
    void println(const std::string& s) { print(s + "\n"); }
    void ensureLineStart() { if (!atLineStart_) print("\n"); }
    void info(const std::string& s)  { println(style_.dim() + s + style_.reset()); }
    void ok(const std::string& s)    { println(style_.green() + s + style_.reset()); }
    void err(const std::string& s)   { println(style_.red() + s + style_.reset()); }

    const char* sym(const char* uni, const char* ascii) const { return utf8_ ? uni : ascii; }

    void installSigint() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = onSigint;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;  // بلا SA_RESTART: لتُقاطع read/sleep الجارية
        ::sigaction(SIGINT, &sa, nullptr);
        ::signal(SIGTERM, fatalSignal);
        ::signal(SIGHUP, fatalSignal);
    }

    void resetInterpreter() {
        interp_ = std::make_unique<rin::Interpreter>();
        underscoreDefined_ = false;
        baseline_.clear();
        for (const auto& kv : interp_->exportGlobals()) baseline_.insert(kv.first);
        interp_->setStreamSink([this](const std::string& chunk) { onProgramOutput(chunk); });
    }

    void onProgramOutput(const std::string& chunk) {
        if (quiet_) return;
        if (g_interrupted && chunk.find("Execution limit exceeded") != std::string::npos) return;  // رسالة المقاطعة الداخلية
        bool looksError = chunk.find("[Error") != std::string::npos || startsWith(ltrim(chunk), "error[") ||
                          chunk.find("unhandled exception") != std::string::npos;
        if (looksError && style_.on) print(style_.red() + chunk + style_.reset());
        else print(chunk);
    }

    // ---------- الواجهة ----------
    void printBanner() {
        std::string v = opt_.version.empty() ? RIN_VERSION_STRING : opt_.version;
        std::string line1 = std::string("  Rin ") + v + "  " + sym("·", "-") + "  " + tr("طرفية تفاعلية", "interactive terminal");
        println("");
        println(style_.bold() + style_.green() + line1 + style_.reset());
        std::string line2 = std::string("  :help ") + tr("للمساعدة", "for help") + "  " + sym("·", "|") + "  Tab " + tr("إكمال", "complete") +
                            "  " + sym("·", "|") + "  Ctrl-R " + tr("بحث", "search") + "  " + sym("·", "|") + "  Ctrl-D " + tr("خروج", "exit");
        info(line2);
        println("");
    }

    std::string promptMain(int& width) const {
        std::string label = "rin[" + std::to_string(counter_) + "]";
        std::string arrow = sym("❯", ">");
        width = static_cast<int>(label.size()) + strWidth(arrow) + 1;
        return (lastOk_ ? style_.green() : style_.red()) + style_.bold() + label + arrow + style_.reset() + " ";
    }
    std::string promptCont(int& width) const {
        std::string label = "rin[" + std::to_string(counter_) + "]";
        int w = static_cast<int>(label.size()) + strWidth(std::string(sym("❯", ">"))) ;
        std::string dots = std::string(static_cast<size_t>(std::max(0, w - 1)), ' ') + sym("┆", ":");
        width = w + 1;
        return style_.dim() + dots + style_.reset() + " ";
    }

    // ---------- الحلقات ----------
    void runRc() {
        std::string p = rcPath();
        std::string content;
        if (p.empty() || !readFileAll(p, content)) return;
        for (const auto& line : splitLines(content)) {
            std::string t = trim(line);
            if (t.empty() || t[0] == '#' || startsWith(t, "//")) continue;
            if (t[0] == ':') handleCommand(t.substr(1));
        }
    }

    std::vector<std::string> completeFn(const U32& buf, size_t cursor, size_t& start) {
        std::vector<std::string> out;
        std::string pre = toUtf8(buf.substr(0, cursor));
        std::string trimmed = ltrim(pre);
        auto addFiles = [&](const std::string& partial, size_t partialStartInBuf) {
            std::string expanded = expandTilde(partial);
            std::string dir = ".", base = expanded, shownDir;
            auto slash = expanded.find_last_of('/');
            if (slash != std::string::npos) { dir = slash == 0 ? "/" : expanded.substr(0, slash); base = expanded.substr(slash + 1); shownDir = partial.substr(0, partial.find_last_of('/') + 1); }
            if (DIR* d = ::opendir(dir.c_str())) {
                while (dirent* e = ::readdir(d)) {
                    std::string name = e->d_name;
                    if (name == "." || name == "..") continue;
                    if (name[0] == '.' && (base.empty() || base[0] != '.')) continue;
                    if (!startsWith(name, base)) continue;
                    std::string full = (dir == "." && slash == std::string::npos) ? name : (dir == "/" ? "/" + name : dir + "/" + name);
                    out.push_back(shownDir + name + (isDirPath(full) ? "/" : ""));
                }
                ::closedir(d);
            }
            std::sort(out.begin(), out.end());
            start = partialStartInBuf;
        };

        // أوامر :
        if (!trimmed.empty() && trimmed[0] == ':') {
            size_t sp = trimmed.find(' ');
            if (sp == std::string::npos) {
                std::string w = trimmed.substr(1);
                for (const auto& c : commandNames()) if (startsWith(c, w)) out.push_back(":" + c);
                std::sort(out.begin(), out.end());
                start = static_cast<size_t>(pre.size() - trimmed.size());  // بداية ':' (بايتات = أحرف لأن ASCII قبلها)
                start = toU32(pre.substr(0, pre.size() - trimmed.size())).size();
                return out;
            }
            std::string cmd = trimmed.substr(1, sp - 1);
            if (cmd == "load" || cmd == "run" || cmd == "save" || cmd == "cd" || cmd == "edit" || cmd == "ls" || cmd == "source" || cmd == "tree" || cmd == "indsin" || cmd == "test") {
                size_t ws = pre.find_last_of(' ');
                std::string partial = pre.substr(ws + 1);
                addFiles(partial, toU32(pre.substr(0, ws + 1)).size());
                return out;
            }
            if (cmd == "set") {
                std::string rest = trim(trimmed.substr(sp + 1));
                if (rest.find(' ') == std::string::npos) {
                    for (const auto& s : settings()) if (startsWith(std::string(s.name), rest)) out.push_back(s.name);
                    start = toU32(pre).size() - toU32(rest).size();
                    return out;
                }
            }
            if (cmd == "help") {
                std::string rest = trim(trimmed.substr(sp + 1));
                for (const auto& c : commandNames()) if (startsWith(c, rest)) out.push_back(c);
                start = toU32(pre).size() - toU32(rest).size();
                return out;
            }
            return out;
        }

        // داخل نص "..." → مسارات ملفات (مثل @import "lib/ma<Tab>)
        Analysis a = analyze(buf.substr(0, cursor));
        if (cursor > 0 && a.cls[cursor - 1] == C_STRING && a.unterminatedString) {
            size_t q = pre.find_last_of('"');
            if (q != std::string::npos) { addFiles(pre.substr(q + 1), toU32(pre.substr(0, q + 1)).size()); return out; }
        }

        // معرّفات: كلمات محجوزة + متغيرات/دوال الجلسة
        size_t s = cursor;
        while (s > 0 && isIdentChar(buf[s - 1])) --s;
        std::string word = toUtf8(buf.substr(s, cursor - s));
        start = s;
        if (word.empty()) return out;
        std::set<std::string> cand;
        for (const auto& k : rin::keywordList()) if (startsWith(k, word)) cand.insert(k);
        for (const auto& kv : interp_->exportGlobals()) {
            if (startsWith(kv.first, "__repl") || kv.first == "_") continue;
            if (startsWith(kv.first, word)) cand.insert(kv.first);
        }
        out.assign(cand.begin(), cand.end());
        return out;
    }

    int loopInteractive() {
        EditorConfig& cfg = editorCfg_;
        LineEditor editor(style_, cfg, history_,
                          [this](const U32& b, size_t c, size_t& s) { return completeFn(b, c, s); });
        std::vector<std::string> acc;
        while (!quit_) {
            int pw = 0;
            std::string prompt = acc.empty() ? promptMain(pw) : promptCont(pw);
            std::string line;

            if (!pending_.empty()) {   // أسطر ملصوقة: تُعرَض ثم تُعالَج كأنها كُتبت
                line = pending_.front(); pending_.erase(pending_.begin());
                std::string shown = prompt;
                U32 u = toU32(line);
                Analysis a = analyze(u);
                std::string colored; uint8_t cur = 255;
                for (size_t i = 0; i < u.size(); ++i) {
                    if (cfg.highlight && a.cls[i] != cur) { colored += style_.reset() + clsStyle(style_, a.cls[i]); cur = a.cls[i]; }
                    appendUtf8(colored, u[i]);
                }
                print(shown + colored + style_.reset() + "\n");
            } else {
                std::vector<std::string> rest; bool restComplete = true;
                std::string prefill = prefillNext_; prefillNext_.clear();
                if (prefill.empty() && !acc.empty() && indent_) {
                    Analysis a = analyze(toU32(joinLines(acc, "\n")));
                    prefill = std::string(static_cast<size_t>(a.braceDepth()) * 4, ' ');
                }
                auto r = editor.read(prompt, pw, prefill, line, rest, restComplete);
                atLineStart_ = true;
                if (r == LineEditor::Result::Eof) {
                    if (acc.empty()) { println(style_.dim() + tr("خروج", "exit") + style_.reset()); break; }
                    acc.clear(); println(style_.dim() + "^D" + style_.reset()); continue;
                }
                if (r == LineEditor::Result::Interrupted) { acc.clear(); println(""); continue; }
                if (!rest.empty()) {
                    if (!restComplete) { prefillNext_ = rest.back(); rest.pop_back(); }
                    pending_.insert(pending_.end(), rest.begin(), rest.end());
                }
            }

            acc.push_back(line);
            std::string source = joinLines(acc, "\n");
            Analysis a = analyze(toU32(source));
            bool isCommand = acc.size() == 1 && startsWith(ltrim(line), ":");
            if (!isCommand && a.needsMore()) continue;
            acc.clear();
            std::string entry = trim(source);
            if (entry.empty()) continue;
            if (!startsWith(source, " ")) history_.add(flattenForHistory(entry));
            processEntry(entry);
        }
        return exitCode_;
    }

    int loopBatch() {
        std::vector<std::string> acc;
        std::string line;
        while (!quit_ && std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            acc.push_back(line);
            std::string source = joinLines(acc, "\n");
            bool isCommand = acc.size() == 1 && startsWith(ltrim(line), ":");
            if (!isCommand && analyze(toU32(source)).needsMore()) continue;
            acc.clear();
            std::string entry = trim(source);
            if (entry.empty()) continue;
            processEntry(entry);
        }
        if (!acc.empty()) {  // ملف انتهى قبل إغلاق قوس
            std::string source = trim(joinLines(acc, "\n"));
            if (!source.empty()) processEntry(source);
        }
        return quit_ ? exitCode_ : (failures_ > 0 ? 1 : 0);
    }

    void processEntry(std::string entry) {
        if (entry == "exit" || entry == "quit") { quit_ = true; return; }
        if (entry == "help" && !userDefined("help")) { cmdHelp(""); return; }
        if (entry == "clear" && !userDefined("clear")) { cmdClear(); return; }
        if (entry[0] == ':') { handleCommand(entry.substr(1)); return; }
        if (entry == "!!" || (entry[0] == '!' && entry.size() > 1 && std::isdigit(static_cast<unsigned char>(entry[1])))) {
            std::string re;
            if (entry == "!!") { if (history_.items.size() < 2) { err(tr("لا يوجد أمر سابق", "no previous command")); return; } re = history_.items[history_.items.size() - 2]; }
            else {
                size_t n = static_cast<size_t>(std::atoi(entry.c_str() + 1));
                if (n < 1 || n > history_.items.size()) { err(tr("رقم غير موجود في التاريخ", "no such history entry")); return; }
                re = history_.items[n - 1];
            }
            println(style_.dim() + "↻ " + style_.reset() + re);
            entry = re;
            if (!entry.empty() && entry[0] == ':') { handleCommand(entry.substr(1)); return; }
        }
        execEntry(entry, false);
    }

    bool userDefined(const std::string& name) {
        rin::Value v;
        return interp_->lookupGlobal(name, v);
    }

    // ---------- التنفيذ ----------
    struct ParseOut { bool ok = false; std::vector<StmtPtr> stmts; std::string errText; };

    // تعبير وحيد → `let __repl_v = <expr>;` على مستوى الـ AST (يحتفظ بمواقع الأخطاء الأصلية).
    static bool wrapAsLet(const std::vector<StmtPtr>& stmts, std::vector<StmtPtr>& out) {
        if (stmts.size() != 1 || stmts[0]->stmtKind != rin::StmtKind::ExpressionStmt) return false;
        auto es = std::static_pointer_cast<rin::ExpressionStmt>(stmts[0]);
        if (!es->expr) return false;
        auto let = std::make_shared<rin::LetStmt>();
        let->name = "__repl_v";
        let->initializer = es->expr;
        let->line = es->line;
        out.clear();
        out.push_back(let);
        return true;
    }

    ParseOut parseSource(const std::string& src, const std::string& name) {
        ParseOut po;
        try {
            rin::Lexer lexer(src, name);
            auto tokens = lexer.scanTokens();
            rin::Parser parser(tokens, name);
            po.stmts = parser.parse();
            po.ok = true;
        } catch (rin::RinError& e) {
            if (e.diagnostic) po.errText = rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());
            else po.errText = "[Error line " + std::to_string(e.line) + "]: " + e.message;
        } catch (std::exception& e) {
            po.errText = std::string("[internal] ") + e.what();
        }
        return po;
    }

    // ينفّذ نصاً كاملاً؛ يعيد true عند النجاح. يطبع الأخطاء. لا يعدّل العدّاد/التاريخ.
    bool runParsed(const std::vector<StmtPtr>& stmts, const std::string& name, double* elapsedMs) {
        g_interrupted = 0; g_sigintCount = 0;
        g_runningInterp = interp_.get();
        interp_->setSourceFile(name);
        auto t0 = Clock::now();
        std::string ignored = interp_->run(stmts);   // المخرجات تصل عبر stream sink
        auto t1 = Clock::now();
        g_runningInterp = nullptr;
        interp_->setExecutionBudget(0);
        if (elapsedMs) *elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (g_interrupted) {
            ensureLineStart();
            err(std::string("^C ") + tr("أُوقف التنفيذ (الحالة محفوظة)", "interrupted (session state kept)"));
            return false;
        }
        return !interp_->hadError();
    }

    bool execEntry(const std::string& entry, bool silentCounter) {
        ++entryCounter_;
        std::string name = "<repl:" + std::to_string(entryCounter_) + ">";
        std::string trimmedEntry = trim(entry);

        ParseOut po = parseSource(trimmedEntry, name);
        std::string src = trimmedEntry;
        if (!po.ok && !trimmedEntry.empty() && trimmedEntry.back() != ';' && trimmedEntry.back() != '}') {
            ParseOut retry = parseSource(trimmedEntry + ";", name);
            if (retry.ok) { po = retry; src = trimmedEntry + ";"; }
        }
        if (!po.ok) {
            ensureLineStart();
            print(style_.red() + po.errText + style_.reset() + (po.errText.empty() || po.errText.back() != '\n' ? "\n" : ""));
            finish(false, 0, silentCounter);
            return false;
        }
        if (po.stmts.empty()) { return true; }

        // تعبير حرّ بلا ; → اطبع قيمته. نغلّف الـ AST نفسه (let __repl_v = <expr>) بدل إعادة كتابة النص،
        // فتبقى رسائل الأخطاء ومواقعها مطابقة لما كتبه المستخدم حرفياً.
        bool wantValue = false;
        std::vector<StmtPtr> toRun = po.stmts;
        if (autoprint_ && trimmedEntry.back() != ';') {
            std::vector<StmtPtr> wrapped;
            if (wrapAsLet(po.stmts, wrapped)) { toRun = wrapped; wantValue = true; }
        }

        double ms = 0;
        bool okRun = runParsed(toRun, name, &ms);
        if (wantValue && okRun) {
            rin::Value v;
            if (interp_->lookupGlobal("__repl_v", v) && v.type != rin::Value::Type::NIL) {
                ensureLineStart();
                println(formatValue(v));
                // `_` = آخر قيمة
                std::string us = underscoreDefined_ ? "_ = __repl_v;" : "let _ = __repl_v;";
                ParseOut u = parseSource(us, name);
                if (u.ok) { bool q = quiet_; quiet_ = true; runParsed(u.stmts, name, nullptr); quiet_ = q; underscoreDefined_ = !interp_->hadError() || underscoreDefined_; }
            }
        }
        if (okRun) accepted_.push_back(src);
        finish(okRun, ms, silentCounter);
        return okRun;
    }

    void finish(bool okRun, double ms, bool silentCounter) {
        lastOk_ = okRun;
        if (!okRun) failures_++;
        if (!silentCounter) ++counter_;
        bool showTime = timing_ == "on" || (timing_ == "auto" && ms >= 100.0);
        if (showTime && okRun) {
            char b[64];
            if (ms >= 1000) std::snprintf(b, sizeof(b), "%.2f s", ms / 1000.0); else std::snprintf(b, sizeof(b), "%.0f ms", ms);
            ensureLineStart();
            println(style_.dim() + sym("⏱ ", "") + b + style_.reset());
        }
    }

    std::string formatValue(const rin::Value& v) const {
        std::string prefix = style_.dim() + "=> " + style_.reset();
        switch (v.type) {
            case rin::Value::Type::STRING: {
                std::string esc;
                for (char c : v.str) { if (c == '\n') esc += "\\n"; else if (c == '"') esc += "\\\""; else esc.push_back(c); }
                return prefix + style_.green() + "\"" + esc + "\"" + style_.reset();
            }
            case rin::Value::Type::NUMBER: return prefix + style_.yellow() + v.toDisplayString() + style_.reset();
            case rin::Value::Type::BOOL:   return prefix + style_.orange() + v.toDisplayString() + style_.reset();
            default: return prefix + v.toDisplayString();
        }
    }

    bool loadFile(const std::string& pathIn) {
        std::string path = expandTilde(pathIn);
        std::string content;
        if (!readFileAll(path, content)) { err(std::string(tr("تعذّر فتح الملف: ", "cannot open file: ")) + path); return false; }
        ParseOut po = parseSource(content, path);
        if (!po.ok) { ensureLineStart(); print(style_.red() + po.errText + style_.reset() + "\n"); return false; }
        double ms = 0;
        bool okRun = runParsed(po.stmts, path, &ms);
        lastLoaded_ = path;
        if (okRun) { accepted_.push_back(content); ok(std::string(sym("✓ ", "")) + tr("حُمِّل ", "loaded ") + path); }
        return okRun;
    }

    // ---------- الأوامر ----------
    struct Cmd { const char* name; const char* usage; const char* ar; const char* en; };
    static const std::vector<Cmd>& commands() {
        static const std::vector<Cmd> c = {
            {"help",     ":help [أمر|keys]",        "المساعدة (أو شرح أمر / الاختصارات)",            "help (or a command / key bindings)"},
            {"quit",     ":quit",                    "خروج (أو :exit / :q / Ctrl-D)",                 "exit (also :exit / :q / Ctrl-D)"},
            {"clear",    ":clear",                   "مسح الشاشة",                                   "clear the screen"},
            {"reset",    ":reset",                   "بدء جلسة نظيفة (يمسح كل المتغيرات والدوال)",     "fresh session (drops all variables/functions)"},
            {"load",     ":load <ملف.rin>",          "تنفيذ ملف داخل الجلسة (تبقى تعريفاته). :run بلا وسائط = src/main.rin للمشروع", "run a file inside the session. bare :run = the project main (src/main.rin)"},
            {"reload",   ":reload",                  "إعادة تحميل آخر ملف",                          "reload the last loaded file"},
            {"save",     ":save <ملف.rin>",          "حفظ المدخلات الناجحة كسكربت",                  "save successful inputs as a script"},
            {"edit",     ":edit [ملف]",              "فتح $EDITOR ثم تنفيذ الناتج",                  "open $EDITOR, then run the result"},
            {"vars",     ":vars [-a] [نص]",               "عرض المتغيرات والدوال الحالية",                "list current variables and functions"},
            {"type",     ":type <تعبير>",            "نوع قيمة التعبير",                              "type of an expression's value"},
            {"time",     ":time [-n N] <شيفرة>",     "قياس زمن التنفيذ (N مرة)",                      "time a snippet (N runs)"},
            {"check",    ":check <شيفرة>",           "فحص نحوي بلا تنفيذ",                           "syntax check without running"},
            {"history",  ":history [N|clear|بحث]",   "عرض/بحث/مسح التاريخ (و !! و !N)",              "show/search/clear history (and !! / !N)"},
            {"keywords", ":keywords",                "الكلمات المحجوزة في اللغة",                     "language keywords"},
            {"set",      ":set [خيار قيمة]",         "عرض/تغيير إعدادات الطرفية",                    "show/change terminal settings"},
            {"pkg",      ":pkg <أمر>",               "مدير الحزم RinPM من داخل الجلسة",              "RinPM package manager from inside the session"},
            {"sh",       ":sh <أمر>",                "تنفيذ أمر شل",                                 "run a shell command"},
            {"cd",       ":cd [مسار]",               "تغيير المجلد الحالي",                          "change directory"},
            {"pwd",      ":pwd",                     "المجلد الحالي",                                 "print working directory"},
            {"ls",       ":ls [مسار]",               "سرد ملفات",                                    "list files"},
                        {"new",      ":new <اسم> [--template console|indsin|lib]", "إنشاء مشروع جديد والدخول إليه", "create a new project and enter it"},
            {"init",     ":init [console|indsin|lib]", "تحويل المجلد الحالي إلى مشروع Rin",         "turn the current directory into a Rin project"},
            {"project",  ":project",                 "معلومات المشروع الحالي",                       "current project info"},
            {"test",     ":test [مجلد]",             "تشغيل tests/*.rin (كلٌّ في مفسّر نظيف)",        "run tests/*.rin (each in a clean interpreter)"},
            {"tree",     ":tree [مسار]",             "شجرة ملفات",                                   "file tree"},
            {"indsin",   ":indsin [ملف] [--width N] [--dump]", "تشغيل واجهة indsin داخل الطرفية (فأرة + تحميل حيّ)", "run an indsin UI inside the terminal (mouse + live reload)"},
            {"session",  ":session",                 "معلومات الجلسة",                               "session info"},
            {"version",  ":version",                 "إصدار Rin",                                    "Rin version"},
        };
        return c;
    }
    static std::vector<std::string> commandNames() {
        std::vector<std::string> n;
        for (const auto& c : commands()) n.push_back(c.name);
        for (const char* a : {"exit", "q", "run", "source", "cls", "h"}) n.push_back(a);
        return n;
    }
    static const std::vector<Setting>& settings() {
        static const std::vector<Setting> s = {
            {"color",     "on|off",             "الألوان",                      "colors"},
            {"highlight", "on|off",             "تلوين الصيغة أثناء الكتابة",    "syntax highlighting while typing"},
            {"hints",     "on|off",             "اقتراحات التاريخ الباهتة",      "dim history suggestions"},
            {"autoprint", "on|off",             "طباعة قيمة التعبير الحرّ",      "print the value of bare expressions"},
            {"timing",    "auto|on|off",        "إظهار زمن التنفيذ",             "show execution time"},
            {"indent",    "on|off",             "مسافة بادئة تلقائية في الأسطر المتتابعة", "auto-indent continuation lines"},
            {"theme",     "dark|light",         "سمة الألوان",                  "color theme"},
        };
        return s;
    }

    void handleCommand(const std::string& raw) {
        std::string line = trim(raw);
        size_t sp = line.find_first_of(" \t");
        std::string cmd = sp == std::string::npos ? line : line.substr(0, sp);
        std::string arg = sp == std::string::npos ? "" : trim(line.substr(sp + 1));
        if (cmd == "q" || cmd == "quit" || cmd == "exit") { quit_ = true; return; }
        if (cmd == "help" || cmd == "h" || cmd == "?") return cmdHelp(arg);
        if (cmd == "clear" || cmd == "cls") return cmdClear();
        if (cmd == "reset") { resetInterpreter(); accepted_.clear(); lastOk_ = true; ok(tr("جلسة جديدة", "fresh session")); return; }
        if (cmd == "run" && arg.empty()) return cmdRunProject();
        if (cmd == "new") return cmdNew(arg);
        if (cmd == "init") return cmdInit(arg);
        if (cmd == "project") return cmdProject();
        if (cmd == "test") return cmdTest(arg);
        if (cmd == "tree") return cmdTree(arg);
        if (cmd == "indsin") return cmdIndsin(arg);
        if (cmd == "load" || cmd == "run" || cmd == "source") {
            if (arg.empty()) { err(tr("الاستخدام: :load <ملف.rin>", "usage: :load <file.rin>")); return; }
            bool r = loadFile(arg); lastOk_ = r; if (!r) failures_++; return;
        }
        if (cmd == "reload") {
            if (lastLoaded_.empty()) { err(tr("لا يوجد ملف محمَّل", "no file loaded yet")); return; }
            bool r = loadFile(lastLoaded_); lastOk_ = r; if (!r) failures_++; return;
        }
        if (cmd == "save") return cmdSave(arg);
        if (cmd == "edit") return cmdEdit(arg);
        if (cmd == "vars") return cmdVars(arg);
        if (cmd == "type") return cmdType(arg);
        if (cmd == "time") return cmdTime(arg);
        if (cmd == "check") return cmdCheck(arg);
        if (cmd == "history") return cmdHistory(arg);
        if (cmd == "keywords") return cmdKeywords();
        if (cmd == "set") return cmdSet(arg);
        if (cmd == "pkg") { std::vector<std::string> a = splitWords(arg); int rc = rinpm::cli::run(a, RIN_VERSION_STRING); lastOk_ = rc == 0; return; }
        if (cmd == "sh") { if (arg.empty()) { err(tr("الاستخدام: :sh <أمر>", "usage: :sh <command>")); return; } int rc = std::system(arg.c_str()); lastOk_ = rc == 0; atLineStart_ = true; return; }
        if (cmd == "cd") {
            std::string target = arg.empty() ? (std::getenv("HOME") ? std::getenv("HOME") : "/") : expandTilde(arg);
            if (::chdir(target.c_str()) != 0) err(std::string("cd: ") + std::strerror(errno));
            else { char b[4096]; if (::getcwd(b, sizeof(b))) info(b); }
            return;
        }
        if (cmd == "pwd") { char b[4096]; if (::getcwd(b, sizeof(b))) println(b); return; }
        if (cmd == "ls") return cmdLs(arg);
        if (cmd == "session") return cmdSession();
        if (cmd == "version") { println(std::string("Rin ") + RIN_VERSION_STRING); return; }
        err(std::string(tr("أمر غير معروف: :", "unknown command: :")) + cmd + "   " + tr("(جرّب :help)", "(try :help)"));
        lastOk_ = false;
    }

    void cmdClear() { print("\x1b[H\x1b[2J"); atLineStart_ = true; }

    void cmdHelp(const std::string& arg) {
        if (arg == "keys" || arg == "keybindings") {
            const char* ar[][2] = {
                {"Enter", "تنفيذ (أو متابعة السطر إن كانت الأقواس/النص مفتوحة)"},
                {"Tab", "إكمال: كلمات اللغة، متغيراتك، الأوامر، مسارات الملفات (حتى داخل \"...\")"},
                {"↑ / ↓", "التاريخ (يبحث بالبادئة المكتوبة)"},
                {"→ / End", "قبول الاقتراح الباهت"},
                {"Ctrl-R", "بحث عكسي في التاريخ"},
                {"Ctrl-A / E", "بداية / نهاية السطر"},
                {"Ctrl-B / F", "حرف للخلف / للأمام (Alt-B / Alt-F: كلمة)"},
                {"Ctrl-W / U / K", "حذف كلمة / حتى البداية / حتى النهاية (Ctrl-Y للصق)"},
                {"Ctrl-L", "مسح الشاشة"},
                {"Ctrl-C", "إلغاء السطر الحالي — أثناء تشغيل برنامج: يوقفه (والحالة محفوظة)، مرتان: إنهاء الطرفية"},
                {"Ctrl-D", "خروج (على سطر فارغ)"},
                {"لصق", "لصق نص متعدد الأسطر ينفَّذ سطراً سطراً بنفس منطق المتابعة"},
            };
            const char* en[][2] = {
                {"Enter", "run (or continue the line while brackets/strings are open)"},
                {"Tab", "complete: keywords, your variables, commands, file paths (even inside \"...\")"},
                {"Up / Down", "history (prefix search on what you typed)"},
                {"Right / End", "accept the dim suggestion"},
                {"Ctrl-R", "reverse history search"},
                {"Ctrl-A / E", "start / end of line"},
                {"Ctrl-B / F", "char back / forward (Alt-B / Alt-F: word)"},
                {"Ctrl-W / U / K", "kill word / to start / to end (Ctrl-Y yanks)"},
                {"Ctrl-L", "clear screen"},
                {"Ctrl-C", "cancel line - while a program runs: stops it (state kept), twice: quit"},
                {"Ctrl-D", "exit (on an empty line)"},
                {"paste", "multi-line paste runs line by line with the same continuation logic"},
            };
            for (int i = 0; i < 12; ++i) {
                const char* const* row = g_english ? en[i] : ar[i];
                println("  " + style_.cyan() + row[0] + style_.reset() + std::string(std::max(1, 16 - strWidth(std::string(row[0]))), ' ') + row[1]);
            }
            return;
        }
        if (!arg.empty()) {
            std::string a = arg[0] == ':' ? arg.substr(1) : arg;
            for (const auto& c : commands()) if (a == c.name) {
                println("  " + style_.cyan() + c.usage + style_.reset());
                println(std::string("    ") + tr(c.ar, c.en));
                return;
            }
            err(std::string(tr("لا يوجد أمر بهذا الاسم: ", "no such command: ")) + a);
            return;
        }
        println(std::string("  ") + style_.bold() + tr("اكتب شيفرة Rin مباشرة، أو أمراً يبدأ بـ ':'", "Type Rin code directly, or a command starting with ':'") + style_.reset());
        println("");
        for (const auto& c : commands()) {
            std::string u = c.usage;
            int pad = std::max(1, 26 - strWidth(u));
            println("  " + style_.cyan() + u + style_.reset() + std::string(static_cast<size_t>(pad), ' ') + tr(c.ar, c.en));
        }
        println("");
        info(std::string("  ") + tr("أمثلة: 1 + 2   |   let x = 5;   |   x * 2   |   fun f(a) {   |   !!", "examples: 1 + 2   |   let x = 5;   |   x * 2   |   fun f(a) {   |   !!"));
        info(std::string("  ") + tr(":help keys للاختصارات", ":help keys for key bindings"));
    }

    void cmdSave(const std::string& arg) {
        if (arg.empty()) { err(tr("الاستخدام: :save <ملف.rin>", "usage: :save <file.rin>")); return; }
        std::string path = expandTilde(arg);
        std::string content = std::string("// ") + tr("محفوظ من rin terminal", "saved from rin terminal") + "\n";
        for (const auto& e : accepted_) {
            content += e;
            if (e.back() != '\n') content += "\n";
        }
        if (!writeFileAll(path, content)) { err(std::string(tr("تعذّرت الكتابة: ", "cannot write: ")) + path); return; }
        ok(std::string(sym("✓ ", "")) + tr("حُفظت ", "saved ") + std::to_string(accepted_.size()) + tr(" مدخلات في ", " entries to ") + path);
    }

    void cmdEdit(const std::string& arg) {
        std::string path;
        bool temp = arg.empty();
        if (temp) {
            char tmpl[] = "/tmp/rin_edit_XXXXXX.rin";
            int fd = ::mkstemps(tmpl, 4);
            if (fd < 0) { err("mkstemp failed"); return; }
            ::close(fd);
            path = tmpl;
            std::string initial;
            if (!history_.items.empty()) initial = history_.items.back();
            writeFileAll(path, initial + "\n");
        } else path = expandTilde(arg);
        const char* ed = std::getenv("VISUAL"); if (!ed || !*ed) ed = std::getenv("EDITOR"); if (!ed || !*ed) ed = "vi";
        std::string cmdline = std::string(ed) + " '" + path + "'";
        int rc = std::system(cmdline.c_str());
        atLineStart_ = true;
        if (rc != 0) { err(tr("أنهى المحرّر بخطأ", "editor exited with an error")); if (temp) ::unlink(path.c_str()); return; }
        std::string content;
        if (readFileAll(path, content) && !trim(content).empty()) {
            ParseOut po = parseSource(content, path);
            if (!po.ok) { print(style_.red() + po.errText + style_.reset() + "\n"); lastOk_ = false; }
            else { bool r = runParsed(po.stmts, path, nullptr); lastOk_ = r; if (r) accepted_.push_back(content); else failures_++; }
        }
        if (temp) ::unlink(path.c_str());
    }

    static std::string oneLine(const std::string& s, size_t maxw) {
        std::string o;
        for (char c : s) { if (c == '\n') o += "⏎"; else if (c == '\r') {} else o.push_back(c); }
        U32 u = toU32(o);
        if (u.size() > maxw) { u.resize(maxw); return toUtf8(u) + "…"; }
        return o;
    }

    void cmdVars(const std::string& argIn) {
        bool all = false;
        std::string filter = argIn;
        if (filter == "-a" || startsWith(filter, "-a ")) { all = true; filter = trim(filter.substr(2)); }
        auto g = interp_->exportGlobals();
        std::vector<std::string> names;
        for (const auto& kv : g) {
            if (startsWith(kv.first, "__repl") || kv.first == "_") continue;
            if (!all && baseline_.count(kv.first)) continue;
            if (!filter.empty() && kv.first.find(filter) == std::string::npos) continue;
            names.push_back(kv.first);
        }
        std::sort(names.begin(), names.end());
        if (names.empty()) { info(tr("(لا توجد متغيرات)", "(no variables)")); return; }
        size_t w = 0;
        for (const auto& n : names) w = std::max<size_t>(w, static_cast<size_t>(strWidth(n)));
        for (const auto& n : names) {
            const rin::Value& v = g[n];
            std::string tn = v.typeName();
            std::string shown = v.type == rin::Value::Type::FUNCTION ? "" : oneLine(v.toDisplayString(), 60);
            println("  " + style_.cyan() + n + style_.reset() + std::string(w - static_cast<size_t>(strWidth(n)) + 2, ' ') +
                    style_.dim() + tn + style_.reset() + (shown.empty() ? "" : "  " + shown));
        }
    }

    // يقيّم تعبيراً صامتاً ويعيد قيمته
    bool evalSilently(const std::string& expr, rin::Value& out, std::string& errText) {
        ++entryCounter_;
        std::string name = "<repl:" + std::to_string(entryCounter_) + ">";
        std::string src = expr;
        ParseOut po = parseSource(src, name);
        if (!po.ok && src.back() != ';') { ParseOut r = parseSource(src + ";", name); if (r.ok) po = r; }
        if (!po.ok) { errText = po.errText; return false; }
        std::vector<StmtPtr> wrapped;
        if (!wrapAsLet(po.stmts, wrapped)) { errText = tr("يلزم تعبير وحيد", "a single expression is required"); return false; }
        bool q = quiet_; quiet_ = true;
        std::string captured;
        // نلتقط نص الخطأ بدل إخفائه
        interp_->setStreamSink([&](const std::string& c) { captured += c; });
        bool r = runParsed(wrapped, name, nullptr);
        interp_->setStreamSink([this](const std::string& chunk) { onProgramOutput(chunk); });
        quiet_ = q;
        if (!r) { errText = captured; return false; }
        return interp_->lookupGlobal("__repl_v", out);
    }

    void cmdType(const std::string& expr) {
        if (expr.empty()) { err(tr("الاستخدام: :type <تعبير>", "usage: :type <expression>")); return; }
        rin::Value v; std::string e;
        if (!evalSilently(expr, v, e)) { print(style_.red() + e + style_.reset() + (e.empty() || e.back() != '\n' ? "\n" : "")); lastOk_ = false; return; }
        println(style_.cyan() + v.typeName() + style_.reset() + "  " + style_.dim() + oneLine(v.toDisplayString(), 70) + style_.reset());
    }

    void cmdTime(const std::string& argIn) {
        std::string arg = argIn; int n = 1;
        if (startsWith(arg, "-n ")) {
            size_t sp = arg.find(' ', 3);
            n = std::max(1, std::atoi(arg.substr(3, sp == std::string::npos ? std::string::npos : sp - 3).c_str()));
            arg = sp == std::string::npos ? "" : trim(arg.substr(sp + 1));
        }
        if (arg.empty()) { err(tr("الاستخدام: :time [-n N] <شيفرة>", "usage: :time [-n N] <code>")); return; }
        std::string src = arg;
        ParseOut po = parseSource(src, "<time>");
        if (!po.ok && src.back() != ';' && src.back() != '}') { ParseOut r = parseSource(src + ";", "<time>"); if (r.ok) po = r; }
        if (!po.ok) { print(style_.red() + po.errText + style_.reset() + "\n"); lastOk_ = false; return; }
        std::vector<double> times;
        bool allOk = true;
        for (int i = 0; i < n; ++i) {
            bool q = quiet_; quiet_ = (n > 1);
            double ms = 0;
            bool r = runParsed(po.stmts, "<time>", &ms);
            quiet_ = q;
            times.push_back(ms);
            if (!r) { allOk = false; break; }
        }
        lastOk_ = allOk;
        if (times.empty() || !allOk) return;
        double mn = *std::min_element(times.begin(), times.end());
        double mx = *std::max_element(times.begin(), times.end());
        double sum = 0; for (double t : times) sum += t;
        char b[200];
        if (n == 1) std::snprintf(b, sizeof(b), "%.3f ms", times[0]);
        else std::snprintf(b, sizeof(b), "n=%d  min %.3f ms  avg %.3f ms  max %.3f ms", n, mn, sum / times.size(), mx);
        ensureLineStart();
        println(style_.cyan() + b + style_.reset());
    }

    void cmdCheck(const std::string& code) {
        if (code.empty()) { err(tr("الاستخدام: :check <شيفرة>", "usage: :check <code>")); return; }
        rin::diag::DiagnosticEngine engine;
        try {
            rin::Lexer lexer(code, "<check>");
            auto tokens = lexer.scanTokens();
            rin::Parser parser(tokens, "<check>");
            parser.parseCollectingDiagnostics(engine);
        } catch (rin::RinError& e) {
            if (e.diagnostic) engine.emit(*e.diagnostic);
        }
        if (engine.hasErrors()) {
            std::string r = rin::diag::renderAll(engine, rin::diag::globalSourceManager(), rin::diag::OutputFormat::Plain);
            print(style_.red() + r + style_.reset() + (r.empty() || r.back() != '\n' ? "\n" : ""));
            lastOk_ = false;
        } else ok(std::string(sym("✓ ", "")) + tr("لا أخطاء نحوية", "no syntax errors"));
    }

    void cmdHistory(const std::string& arg) {
        if (arg == "clear") { history_.clear(); ok(tr("مُسح التاريخ", "history cleared")); return; }
        size_t from = 0;
        std::string needle;
        if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
            size_t n = static_cast<size_t>(std::atoi(arg.c_str()));
            from = history_.items.size() > n ? history_.items.size() - n : 0;
        } else needle = arg;
        for (size_t i = from; i < history_.items.size(); ++i) {
            if (!needle.empty() && history_.items[i].find(needle) == std::string::npos) continue;
            char idx[32]; std::snprintf(idx, sizeof(idx), "%4zu", i + 1);
            println(style_.dim() + idx + "  " + style_.reset() + oneLine(history_.items[i], 120));
        }
    }

    void cmdKeywords() {
        auto kws = rin::keywordList();
        std::sort(kws.begin(), kws.end());
        const auto& cm = keywordClasses();
        size_t maxw = 0;
        for (const auto& k : kws) maxw = std::max(maxw, k.size());
        size_t colw = maxw + 2;
        size_t perRow = std::max<size_t>(1, static_cast<size_t>(termCols()) / colw);
        std::string o;
        for (size_t i = 0; i < kws.size(); ++i) {
            auto it = cm.find(kws[i]);
            o += clsStyle(style_, it == cm.end() ? static_cast<uint8_t>(C_PLAIN) : it->second) + kws[i] + style_.reset();
            if ((i + 1) % perRow == 0 || i + 1 == kws.size()) o += "\n"; else o += std::string(colw - kws[i].size(), ' ');
        }
        print(o);
    }

    void cmdLs(const std::string& arg) {
        std::string dir = arg.empty() ? "." : expandTilde(arg);
        DIR* d = ::opendir(dir.c_str());
        if (!d) { err(std::string("ls: ") + std::strerror(errno)); return; }
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == ".." || n[0] == '.') continue;
            names.push_back(n + (isDirPath(dir + "/" + n) ? "/" : ""));
        }
        ::closedir(d);
        std::sort(names.begin(), names.end());
        size_t maxw = 0;
        for (const auto& n : names) maxw = std::max<size_t>(maxw, static_cast<size_t>(strWidth(n)));
        size_t colw = maxw + 2;
        size_t perRow = std::max<size_t>(1, static_cast<size_t>(termCols()) / colw);
        std::string o;
        for (size_t i = 0; i < names.size(); ++i) {
            bool isD = names[i].back() == '/';
            bool isRin = names[i].size() > 4 && names[i].compare(names[i].size() - 4, 4, ".rin") == 0;
            o += (isD ? style_.blue() : isRin ? style_.green() : "") + names[i] + style_.reset();
            if ((i + 1) % perRow == 0 || i + 1 == names.size()) o += "\n";
            else o += std::string(colw - static_cast<size_t>(strWidth(names[i])), ' ');
        }
        print(o);
    }


    // ======================= المشاريع =======================
    static bool validProjectName(const std::string& n) {
        if (n.empty() || n.size() > 64) return false;
        for (char c : n) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
        return std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_';
    }
    static std::string identOf(const std::string& n) {
        std::string o = n;
        for (auto& c : o) if (c == '-') c = '_';
        return o;
    }
    static std::string cwd() { char b[4096]; return ::getcwd(b, sizeof(b)) ? std::string(b) : std::string("."); }
    static bool fileExists2(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }

    std::string findProjectRoot() const {
        std::string dir = cwd();
        for (int i = 0; i < 64 && !dir.empty(); ++i) {
            if (fileExists2(dir + "/rin.toml")) return dir;
            auto pos = dir.find_last_of('/');
            if (pos == std::string::npos || pos == 0) break;
            dir = dir.substr(0, pos);
        }
        return "";
    }

    struct TemplateFile { std::string path, content; };

    static std::vector<TemplateFile> projectFiles(const std::string& name, const std::string& tmpl) {
        std::string id = identOf(name);
        std::vector<TemplateFile> f;
        f.push_back({"rin.toml", "[package]\nname = \"" + name + "\"\nversion = \"0.1.0\"\nedition = \"2026\"\n" +
                                   (tmpl == "indsin" ? "kind = \"indsin\"\n" : tmpl == "lib" ? "kind = \"library\"\n" : "") + "\n[dependencies]\n"});
        f.push_back({".gitignore", "build/\n*.png\n.env\n"});
        if (tmpl == "indsin") {
            f.push_back({"src/main.rin",
                "// " + name + " — تطبيق واجهة indsin.  شغّله داخل طرفية Rin:  :indsin\n"
                "warp count = 0;\n\n"
                "fun inc() { count = count + 1; }\n"
                "fun dec() { count = count - 1; }\n\n"
                "@view.Column=root\n"
                "    padding=16;\n"
                "    gap=14;\n\n"
                "    @view.Text=title text=\"" + name + "\"; size=\"title\"; .end/view\n"
                "    @view.Text=counter text=\"Count: \" + count; size=20; .end/view\n"
                "    @view.Button=btnInc label=\"+1\"; onTap=inc(); radius=12; .end/view\n"
                "    @view.Button=btnDec label=\"-1\"; variant=\"outline\"; tone=\"primary\"; onTap=dec(); radius=12; .end/view\n"
                ".end/view\n"});
            f.push_back({"tests/basic.rin", "// فحص أساسي لا يفتح واجهة\nlet ok = 1;\nif (ok != 1) { print \"FAIL\"; } else { print \"OK\"; }\n"});
            f.push_back({"README.md", "# " + name + "\n\nمشروع واجهة indsin.\n\n```\nrin terminal\n:indsin            # يعرض src/main.rin داخل الطرفية (فأرة + تحميل حيّ عند الحفظ)\n:indsin --dump     # إطار واحد نصّي\n:test\n```\n"});
        } else if (tmpl == "lib") {
            f.push_back({"src/" + name + ".rin",
                "// مكتبة " + name + "\nfun " + id + "_greet(who) {\n    return \"Hello, \" + who + \"!\";\n}\n"});
            f.push_back({"src/main.rin", "// عرض سريع للمكتبة\nfun " + id + "_greet(who) { return \"Hello, \" + who + \"!\"; }\nprint " + id + "_greet(\"Rin\");\n"});
            f.push_back({"tests/basic.rin", "fun " + id + "_greet(who) { return \"Hello, \" + who + \"!\"; }\nif (" + id + "_greet(\"x\") != \"Hello, x!\") { print \"FAIL\"; } else { print \"OK\"; }\n"});
            f.push_back({"README.md", "# " + name + "\n\nمكتبة Rin.\n\n```\nrin terminal\n:load src/" + name + ".rin\n" + id + "_greet(\"Rin\")\n:test\n```\n"});
        } else {
            f.push_back({"src/main.rin", "// نقطة الدخول - " + name + "\nprint \"Hello from Rin!\";\n"});
            f.push_back({"tests/basic.rin", "// اختبار أساسي: ينجح إن لم يُطلق أي خطأ أثناء التنفيذ.\nlet x = 2 + 2;\nif (x != 4) { print \"FAIL: 2+2 != 4\"; } else { print \"OK\"; }\n"});
            f.push_back({"README.md", "# " + name + "\n\nمشروع Rin.\n\n```\nrin terminal\n:run     # يشغّل src/main.rin\n:test\n```\n"});
        }
        return f;
    }

    // ينشئ الملفات تحت root دون الكتابة فوق موجود.
    int writeProject(const std::string& root, const std::string& name, const std::string& tmpl, std::vector<std::string>& created) {
        int n = 0;
        mkdirP(root);
        for (const auto& tf : projectFiles(name, tmpl)) {
            std::string full = root + "/" + tf.path;
            auto slash = full.find_last_of('/');
            if (slash != std::string::npos) mkdirP(full.substr(0, slash));
            if (fileExists2(full)) continue;
            if (writeFileAll(full, tf.content)) { created.push_back(tf.path); ++n; }
        }
        mkdirP(root + "/assets");
        return n;
    }

    static bool parseTemplate(const std::string& t, std::string& out) {
        if (t.empty() || t == "console" || t == "app" || t == "cli") { out = "console"; return true; }
        if (t == "indsin" || t == "ui") { out = "indsin"; return true; }
        if (t == "lib" || t == "library") { out = "lib"; return true; }
        return false;
    }

    void showCreated(const std::string& name, const std::vector<std::string>& created) {
        ok(std::string(sym("✓ ", "")) + tr("أُنشئ المشروع: ", "project created: ") + name);
        for (const auto& c : created) println("  " + style_.dim() + c + style_.reset());
    }

    void cmdNew(const std::string& arg) {
        auto w = splitWords(arg);
        std::string name, tmplArg;
        for (size_t i = 0; i < w.size(); ++i) {
            if ((w[i] == "--template" || w[i] == "-t") && i + 1 < w.size()) tmplArg = w[++i];
            else if (startsWith(w[i], "--template=")) tmplArg = w[i].substr(11);
            else if (name.empty()) name = w[i];
        }
        if (name.empty()) { err(tr("الاستخدام: :new <اسم> [--template console|indsin|lib]", "usage: :new <name> [--template console|indsin|lib]")); lastOk_ = false; return; }
        std::string tmpl;
        if (!parseTemplate(tmplArg, tmpl)) { err(std::string(tr("قالب غير معروف: ", "unknown template: ")) + tmplArg + "  (console | indsin | lib)"); lastOk_ = false; return; }
        if (!validProjectName(name)) { err(tr("اسم غير صالح: حروف/أرقام/_/- فقط ويبدأ بحرف", "invalid name: letters/digits/_/- only, starting with a letter")); lastOk_ = false; return; }
        if (fileExists2(name)) { err(std::string(tr("المسار موجود مسبقاً: ", "path already exists: ")) + name); lastOk_ = false; return; }
        std::vector<std::string> created;
        writeProject(name, name, tmpl, created);
        if (created.empty()) { err(tr("تعذّر إنشاء المشروع", "could not create the project")); lastOk_ = false; return; }
        showCreated(name, created);
        if (::chdir(name.c_str()) == 0) info(std::string(tr("الآن داخل: ", "now in: ")) + cwd());
        info(tmpl == "indsin" ? tr("التالي:  :indsin   (أو :test)", "next:  :indsin   (or :test)")
                              : tr("التالي:  :run   (أو :test)", "next:  :run   (or :test)"));
    }

    void cmdInit(const std::string& arg) {
        std::string tmpl;
        if (!parseTemplate(trim(arg), tmpl)) { err(tr("قالب غير معروف (console | indsin | lib)", "unknown template (console | indsin | lib)")); lastOk_ = false; return; }
        std::string here = cwd();
        if (fileExists2(here + "/rin.toml")) { err(tr("هذا المجلد مشروع Rin بالفعل (rin.toml موجود)", "this directory is already a Rin project (rin.toml exists)")); lastOk_ = false; return; }
        std::string name = here.substr(here.find_last_of('/') + 1);
        for (auto& c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) name = "project_" + name;
        std::vector<std::string> created;
        writeProject(here, name, tmpl, created);
        showCreated(name, created);
    }

    void cmdProject() {
        std::string root = findProjectRoot();
        if (root.empty()) { info(tr("لست داخل مشروع (لا rin.toml صعوداً). جرّب :new <اسم>", "not inside a project (no rin.toml upwards). try :new <name>")); return; }
        std::string toml;
        readFileAll(root + "/rin.toml", toml);
        auto getv = [&](const std::string& key) {
            for (const auto& l : splitLines(toml)) {
                std::string t = trim(l);
                if (startsWith(t, key)) {
                    auto eq = t.find('=');
                    if (eq != std::string::npos) { std::string v = trim(t.substr(eq + 1)); if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2); return v; }
                }
            }
            return std::string();
        };
        std::string mainSrc;
        bool hasMain = readFileAll(root + "/src/main.rin", mainSrc);
        bool isIndsin = hasMain && mainSrc.find("@view") != std::string::npos;
        println(std::string("  ") + tr("المشروع: ", "project: ") + style_.bold() + getv("name") + style_.reset() + "  v" + getv("version"));
        println(std::string("  ") + tr("الجذر:   ", "root:    ") + root);
        println(std::string("  ") + tr("النوع:   ", "kind:    ") + (isIndsin ? "indsin (UI)" : (getv("kind").empty() ? "console" : getv("kind"))));
        println(std::string("  ") + tr("الرئيسي: ", "main:    ") + (hasMain ? "src/main.rin" : tr("(غير موجود)", "(missing)")));
        int tests = 0;
        if (DIR* d = ::opendir((root + "/tests").c_str())) { while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.compare(n.size() - 4, 4, ".rin") == 0) ++tests; } ::closedir(d); }
        println(std::string("  ") + tr("الاختبارات: ", "tests:   ") + std::to_string(tests));
    }

    void cmdRunProject() {
        std::string root = findProjectRoot();
        if (root.empty()) { err(tr("لست داخل مشروع. استخدم :load <ملف> أو :new <اسم>", "not inside a project. use :load <file> or :new <name>")); lastOk_ = false; return; }
        std::string mainPath = root + "/src/main.rin", content;
        if (!readFileAll(mainPath, content)) { err(tr("لا يوجد src/main.rin", "no src/main.rin")); lastOk_ = false; return; }
        if (content.find("@view") != std::string::npos) { info(tr("مشروع واجهة → تشغيل indsin", "UI project → running indsin")); cmdIndsin(mainPath); return; }
        bool r = loadFile(mainPath); lastOk_ = r; if (!r) failures_++;
    }

    void cmdTest(const std::string& arg) {
        std::string root = findProjectRoot();
        std::string dir = !arg.empty() ? expandTilde(arg) : (root.empty() ? "tests" : root + "/tests");
        std::vector<std::string> files;
        if (DIR* d = ::opendir(dir.c_str())) {
            while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.compare(n.size() - 4, 4, ".rin") == 0) files.push_back(dir + "/" + n); }
            ::closedir(d);
        } else { err(std::string(tr("لا يوجد مجلد اختبارات: ", "no tests directory: ")) + dir); lastOk_ = false; return; }
        std::sort(files.begin(), files.end());
        if (files.empty()) { info(std::string(tr("لا توجد ملفات *.rin في ", "no *.rin files in ")) + dir); return; }
        int pass = 0, fail = 0;
        for (const auto& f : files) {
            std::string content;
            std::string shortName = f.substr(f.find_last_of('/') + 1);
            if (!readFileAll(f, content)) { ++fail; err(std::string(sym("✗ ", "FAIL ")) + shortName + "  (read)"); continue; }
            ParseOut po = parseSource(content, f);
            if (!po.ok) { ++fail; err(std::string(sym("✗ ", "FAIL ")) + shortName); print(po.errText + "\n"); continue; }
            rin::Interpreter fresh;   // مفسّر نظيف لكل ملف: لا تتسرّب حالة بين الاختبارات
            std::string captured;
            fresh.setStreamSink([&](const std::string& c) { captured += c; });
            fresh.setSourceFile(f);
            auto t0 = Clock::now();
            fresh.run(po.stmts);
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            bool bad = fresh.hadError() || captured.find("FAIL") != std::string::npos;
            char b[32]; std::snprintf(b, sizeof(b), "%.0f ms", ms);
            if (!bad) { ++pass; println(style_.green() + sym("✓ ", "ok   ") + style_.reset() + shortName + "  " + style_.dim() + b + style_.reset()); }
            else { ++fail; println(style_.red() + sym("✗ ", "FAIL ") + style_.reset() + shortName); print(captured); ensureLineStart(); }
        }
        std::string summary = std::to_string(pass) + " " + tr("نجح", "passed") + ", " + std::to_string(fail) + " " + tr("فشل", "failed");
        if (fail == 0) ok(summary); else err(summary);
        lastOk_ = fail == 0;
        if (fail) failures_++;
    }

    void treeRec(const std::string& dir, const std::string& prefix, int depth, int& count) {
        std::vector<std::string> names;
        if (DIR* d = ::opendir(dir.c_str())) {
            while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n == "." || n == ".." || n[0] == '.' || n == "build") continue; names.push_back(n); }
            ::closedir(d);
        }
        std::sort(names.begin(), names.end());
        for (size_t i = 0; i < names.size(); ++i) {
            bool last = i + 1 == names.size();
            std::string full = dir + "/" + names[i];
            bool isD = isDirPath(full);
            println(prefix + (last ? sym("└── ", "`-- ") : sym("├── ", "|-- ")) + (isD ? style_.blue() + names[i] + "/" + style_.reset() : names[i]));
            if (++count > 300) return;
            if (isD && depth < 3) treeRec(full, prefix + (last ? "    " : std::string(sym("│   ", "|   "))), depth + 1, count);
        }
    }
    void cmdTree(const std::string& arg) {
        std::string dir = arg.empty() ? "." : expandTilde(arg);
        if (!isDirPath(dir)) { err(std::string("tree: ") + tr("ليس مجلداً: ", "not a directory: ") + dir); return; }
        println(style_.bold() + dir + style_.reset());
        int count = 0;
        treeRec(dir, "", 1, count);
    }

    void cmdIndsin(const std::string& arg) {
        IndsinArgs ia; std::string e;
        std::vector<std::string> words = splitWords(arg);
        if (!parseIndsinArgs(words, ia, e)) { err(e); lastOk_ = false; return; }
        if (ia.path.empty()) {
            std::string root = findProjectRoot();
            if (!root.empty() && fileExists2(root + "/src/main.rin")) ia.path = root + "/src/main.rin";
        }
        if (ia.path.empty()) { err(tr("الاستخدام: :indsin <ملف.rin>  (أو ادخل مشروعاً به src/main.rin)", "usage: :indsin <file.rin>  (or enter a project with src/main.rin)")); lastOk_ = false; return; }
        ensureLineStart();
        int rc = runIndsin(ia, style_, utf8_);
        atLineStart_ = true;
        lastOk_ = rc == 0;
        if (rc != 0) failures_++;
    }

    void cmdSession() {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started_).count();
        println(std::string("  Rin ") + RIN_VERSION_STRING);
        println(std::string("  ") + tr("المدخلات المنفَّذة: ", "entries run: ") + std::to_string(entryCounter_));
        println(std::string("  ") + tr("الناجحة المحفوظة لـ :save: ", "successful (for :save): ") + std::to_string(accepted_.size()));
        println(std::string("  ") + tr("المتغيرات/الدوال: ", "bindings: ") + std::to_string(interp_->exportGlobals().size()));
        println(std::string("  ") + tr("مدة الجلسة: ", "uptime: ") + std::to_string(secs) + " s");
        if (history_.persist) println(std::string("  ") + tr("ملف التاريخ: ", "history file: ") + history_.path);
        if (!lastLoaded_.empty()) println(std::string("  ") + tr("آخر ملف: ", "last file: ") + lastLoaded_);
    }

    static bool parseOnOff(const std::string& v, bool& out) {
        if (v == "on" || v == "1" || v == "true" || v == "yes") { out = true; return true; }
        if (v == "off" || v == "0" || v == "false" || v == "no") { out = false; return true; }
        return false;
    }

    void cmdSet(const std::string& arg) {
        auto words = splitWords(arg);
        if (words.empty()) {
            auto yn = [](bool b) { return b ? "on" : "off"; };
            println(std::string("  color      ") + yn(style_.on));
            println(std::string("  highlight  ") + yn(editorCfg_.highlight));
            println(std::string("  hints      ") + yn(editorCfg_.hints));
            println(std::string("  autoprint  ") + yn(autoprint_));
            println(std::string("  timing     ") + timing_);
            println(std::string("  indent     ") + yn(indent_));
            println(std::string("  theme      ") + (style_.dark ? "dark" : "light"));
            return;
        }
        if (words.size() < 2) { err(tr("الاستخدام: :set <خيار> <قيمة>", "usage: :set <option> <value>")); return; }
        const std::string& k = words[0]; const std::string& v = words[1];
        bool b = false;
        if (k == "color" && parseOnOff(v, b)) { style_.on = b && ::isatty(STDOUT_FILENO); }
        else if (k == "highlight" && parseOnOff(v, b)) editorCfg_.highlight = b && style_.on;
        else if (k == "hints" && parseOnOff(v, b)) editorCfg_.hints = b && style_.on;
        else if (k == "autoprint" && parseOnOff(v, b)) autoprint_ = b;
        else if (k == "indent" && parseOnOff(v, b)) indent_ = b;
        else if (k == "timing" && (v == "auto" || v == "on" || v == "off")) timing_ = v;
        else if (k == "theme" && (v == "dark" || v == "light")) style_.dark = (v == "dark");
        else { err(tr("خيار أو قيمة غير صالحة (جرّب :set بلا وسائط)", "invalid option or value (try :set with no args)")); return; }
        ok(std::string(sym("✓ ", "")) + k + " = " + v);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// الواجهة العامة
// ---------------------------------------------------------------------------
void printUsage() {
    std::cout <<
        "rin terminal — " << tr("الطرفية التفاعلية لـ Rin", "Rin's interactive terminal") << "\n\n"
        << tr("الاستخدام:\n", "Usage:\n") <<
        "  rin terminal [options] [file.rin ...]     (" << tr("أو: rin repl / rin shell / rin بلا وسائط", "or: rin repl / rin shell / plain rin") << ")\n\n"
        << tr("الخيارات:\n", "Options:\n") <<
        "  -i, --load <file>      " << tr("حمّل ملفاً قبل أول مؤشّر (يمكن تكراره)", "load a file before the first prompt (repeatable)") << "\n"
        "  -e, --eval <code>      " << tr("نفّذ شيفرة قبل أول مؤشّر (يمكن تكراره)", "run code before the first prompt (repeatable)") << "\n"
        "  --no-color             " << tr("بلا ألوان (يحترم NO_COLOR أيضاً)", "no colors (NO_COLOR is honored too)") << "\n"
        "  --no-highlight         " << tr("بلا تلوين أثناء الكتابة", "no highlighting while typing") << "\n"
        "  --no-hints             " << tr("بلا اقتراحات التاريخ", "no history suggestions") << "\n"
        "  --no-history           " << tr("لا تقرأ/تكتب ملف التاريخ", "do not read/write the history file") << "\n"
        "  --no-banner            " << tr("بلا ترويسة", "no banner") << "\n"
        "  --no-autoprint         " << tr("لا تطبع قيمة التعبير الحرّ", "do not print bare expression values") << "\n"
        "  --timing=auto|on|off   " << tr("إظهار زمن التنفيذ", "show execution time") << "\n"
        "  --theme=dark|light     " << tr("سمة الألوان", "color theme") << "\n"
        "  --history-file <path>  " << tr("ملف تاريخ مخصّص", "custom history file") << "\n"
        "  --batch                " << tr("وضع غير تفاعلي (قراءة الأسطر من stdin)", "non-interactive mode (read lines from stdin)") << "\n"
        "  -h, --help\n\n"
        << tr("ملفات وبيئة:\n", "Files and environment:\n") <<
        "  $RIN_HISTFILE  $XDG_STATE_HOME/rin/history   " << tr("التاريخ", "history") << "\n"
        "  $RIN_TERMINAL_RC  $XDG_CONFIG_HOME/rin/terminal.rc   " << tr("أوامر ':' تُنفَّذ عند البدء", "':' commands run at startup") << "\n"
        "  $RIN_LANG=en   " << tr("واجهة إنجليزية", "English interface") << "\n";
}

bool parseArgs(const std::vector<std::string>& args, Options& opt, std::string& err, bool& showHelp) {
    showHelp = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto needValue = [&](std::string& dst) {
            if (i + 1 >= args.size()) { err = a + " " + tr("يحتاج قيمة", "needs a value"); return false; }
            dst = args[++i]; return true;
        };
        if (a == "-h" || a == "--help") { showHelp = true; return true; }
        else if (a == "--no-color") opt.color = false;
        else if (a == "--no-highlight") opt.highlight = false;
        else if (a == "--no-hints") opt.hints = false;
        else if (a == "--no-history") opt.useHistory = false;
        else if (a == "--no-banner") opt.banner = false;
        else if (a == "--no-autoprint") opt.autoprint = false;
        else if (a == "--batch") opt.forceBatch = true;
        else if (a == "-i" || a == "--load") { std::string v; if (!needValue(v)) return false; opt.preload.push_back(v); }
        else if (a == "-e" || a == "--eval") { std::string v; if (!needValue(v)) return false; opt.evalFirst.push_back(v); }
        else if (a == "--history-file") { if (!needValue(opt.historyFile)) return false; }
        else if (a.compare(0, 9, "--timing=") == 0) {
            opt.timing = a.substr(9);
            if (opt.timing != "auto" && opt.timing != "on" && opt.timing != "off") { err = tr("قيمة --timing غير صالحة", "invalid --timing value"); return false; }
        }
        else if (a.compare(0, 8, "--theme=") == 0) {
            opt.theme = a.substr(8);
            if (opt.theme != "dark" && opt.theme != "light") { err = tr("قيمة --theme غير صالحة", "invalid --theme value"); return false; }
        }
        else if (!a.empty() && a[0] == '-') { err = std::string(tr("خيار غير معروف: ", "unknown option: ")) + a; return false; }
        else opt.preload.push_back(a);
    }
    return true;
}

int runIndsinCommand(const std::vector<std::string>& args) {
    if (const char* l = std::getenv("RIN_LANG")) g_english = std::strncmp(l, "en", 2) == 0;
    IndsinArgs ia; std::string e;
    if (!parseIndsinArgs(args, ia, e)) { std::fprintf(stderr, "rin indsin: %s\n", e.c_str()); return 2; }
    Style st;
    const char* term = std::getenv("TERM");
    st.on = ::isatty(STDOUT_FILENO) && std::getenv("NO_COLOR") == nullptr && term && std::strcmp(term, "dumb") != 0;
    return runIndsin(ia, st, true);
}

int run(const Options& optIn) {
    Options opt = optIn;
    if (const char* l = std::getenv("RIN_LANG")) g_english = std::strncmp(l, "en", 2) == 0;
    if (const char* t = std::getenv("RIN_THEME")) { if (std::strcmp(t, "light") == 0) opt.theme = "light"; }
    if (opt.version.empty()) opt.version = RIN_VERSION_STRING;
    Session s(opt);
    return s.run();
}

} // namespace terminal
} // namespace rin
