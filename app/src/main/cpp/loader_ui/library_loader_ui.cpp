// app/src/main/cpp/loader_ui/library_loader_ui.cpp
#include "library_loader_ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>

#if !defined(__EMSCRIPTEN__)
#include <atomic>
#include <condition_variable>
#include <thread>
#endif

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>
#  include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#  include <pthread.h>
#  include <signal.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace rin::loaderui {

const char* stageLabel(LoadStage stage) {
    switch (stage) {
        case LoadStage::Resolving:    return "Resolving";
        case LoadStage::Locating:     return "Locating";
        case LoadStage::Reading:      return "Reading";
        case LoadStage::Parsing:      return "Parsing";
        case LoadStage::Dependencies: return "Resolving dependencies";
        case LoadStage::Registering:  return "Registering";
        case LoadStage::Initializing: return "Initializing";
        case LoadStage::Completed:    return "Completed";
    }
    return "";
}

// ============================================================================
// أدوات الطرفية
// ============================================================================
namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

const char* envOr(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

bool containsCI(const std::string& hay, const char* needle) {
    std::string h = hay, n = needle;
    for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

// رقم واصف الملف للدفق إن كان std::cout/std::cerr، وإلا -1 (ostringstream, ملف...).
int fdOf(const std::ostream& out) {
    if (&out == &std::cout) return 1;
    if (&out == &std::cerr || &out == &std::clog) return 2;
    return -1;
}

bool fdIsTty(int fd) {
    if (fd < 0) return false;
#if defined(_WIN32)
    return _isatty(fd) != 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::isatty(fd) != 0;
#else
    return false;
#endif
}

// عدد أعمدة العرض (نقاط الشيفرة، بلا محارف الدمج) — تقريب كافٍ للأسماء اللاتينية والعربية.
std::size_t displayWidth(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// اقتطاع بحدود محارف UTF-8 (لا يقطع منتصف محرف) مع علامة حذف.
std::string truncateTo(const std::string& s, std::size_t maxCols, const std::string& ell) {
    if (displayWidth(s) <= maxCols) return s;
    std::size_t ellW = displayWidth(ell);
    if (maxCols <= ellW) return maxCols == 0 ? "" : ell.substr(0, maxCols);
    std::size_t keep = maxCols - ellW, cols = 0, i = 0;
    while (i < s.size()) {
        std::size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        if (cols + 1 > keep) break;
        ++cols;
        i = j;
    }
    return s.substr(0, i) + ell;
}

std::string padRight(const std::string& s, std::size_t w) {
    std::size_t cur = displayWidth(s);
    return cur >= w ? s : s + std::string(w - cur, ' ');
}

enum class St { None, Accent, Ok, Err, Warn, Dim, Bold };

// يرسم النص بالنمط حسب قدرات الطرفية (بلا ألوان => النص كما هو).
std::string paint(const TermCaps& c, St st, const std::string& text) {
    if (!c.color || st == St::None || text.empty()) return text;
    const char* code = "";
    std::string accentBuf;
    if (c.color256) {
        switch (st) {
            case St::Accent: accentBuf = "38;5;" + std::to_string(c.accent256); code = accentBuf.c_str(); break;
            case St::Ok:     code = "38;5;78";  break;
            case St::Err:    code = "38;5;203"; break;
            case St::Warn:   code = "38;5;214"; break;
            case St::Dim:    code = "38;5;244"; break;
            case St::Bold:   code = "1";        break;
            default: break;
        }
    } else {
        switch (st) {
            case St::Accent: accentBuf = std::to_string(c.accentBasic); code = accentBuf.c_str(); break;
            case St::Ok:     code = "32"; break;
            case St::Err:    code = "31"; break;
            case St::Warn:   code = "33"; break;
            case St::Dim:    code = "2";  break;
            case St::Bold:   code = "1";  break;
            default: break;
        }
    }
    return std::string("\x1b[") + code + "m" + text + "\x1b[0m";
}

// الرموز (Unicode أو ASCII).
struct Glyphs {
    const char *ok, *fail, *skip, *warn, *info, *branch, *last, *vert, *blank, *ell, *arrow, *dot;
    std::vector<std::string> spin;
    static Glyphs make(bool unicode, SpinnerStyle sp = SpinnerStyle::Braille) {
        Glyphs g;
        if (unicode) {
            g = {"\xE2\x9C\x94", "\xE2\x9C\x98", "\xE2\x86\xBA", "\xE2\x9A\xA0", "\xE2\x80\xA2",
                 "\xE2\x94\x9C\xE2\x94\x80 ", "\xE2\x94\x94\xE2\x94\x80 ", "\xE2\x94\x82  ", "   ",
                 "\xE2\x80\xA6", "\xE2\x86\x92", "\xC2\xB7", {}};
            g.spin = spinFrames(sp);
        } else {
            g = {"+", "x", "~", "!", "*", "|- ", "`- ", "|  ", "   ", "...", "->", "-", {}};
            g.spin = {"|", "/", "-", "\\"};
        }
        return g;
    }
    static std::vector<std::string> spinFrames(SpinnerStyle sp) {
        auto split = [](const char* s) {   // يقسّم نص UTF-8 إلى محارف
            std::vector<std::string> v; std::string cur;
            for (const char* p = s; *p; ++p) {
                unsigned char b = static_cast<unsigned char>(*p);
                if ((b & 0xC0) != 0x80 && !cur.empty()) { v.push_back(cur); cur.clear(); }
                cur += *p;
            }
            if (!cur.empty()) v.push_back(cur);
            return v;
        };
        switch (sp) {
            case SpinnerStyle::Braille: return split("\u280b\u2819\u2839\u2838\u283c\u2834\u2826\u2827\u2807\u280f");
            case SpinnerStyle::Dots:    return split("\u28fe\u28fd\u28fb\u28bf\u287f\u28df\u28ef\u28f7");
            case SpinnerStyle::Line:    return {"|", "/", "-", "\\"};
            case SpinnerStyle::Arc:     return split("\u25dc\u25e0\u25dd\u25de\u25e1\u25df");
            case SpinnerStyle::Bounce:  return split("\u2801\u2802\u2804\u2840\u2880\u2820\u2810\u2808");
            case SpinnerStyle::Pulse:   return split("\u2219\u2022\u25cf\u2022");
            case SpinnerStyle::Arrow:   return split("\u2190\u2196\u2191\u2197\u2192\u2198\u2193\u2199");
            case SpinnerStyle::Grow:    return split("\u2581\u2582\u2583\u2584\u2585\u2586\u2587\u2588\u2587\u2586\u2585\u2584\u2583\u2582");
            case SpinnerStyle::Slide: {
                std::vector<std::string> v;
                const int w = 10, blk = 3;
                auto mk = [&](int pos) {
                    std::string s;
                    for (int i = 0; i < w; ++i) s += (i >= pos && i < pos + blk) ? "\xE2\x96\xB0" : "\xE2\x96\xB1";
                    return s;
                };
                for (int p = 0; p <= w - blk; ++p) v.push_back(mk(p));
                for (int p = w - blk - 1; p >= 1; --p) v.push_back(mk(p));
                return v;
            }
        }
        return {"|", "/", "-", "\\"};
    }
    const std::string& spinner(std::size_t frame) const { return spin[frame % spin.size()]; }
};

// شريط تقدّم بأنواع متعددة (BarStyle). inner = عدد الخانات (ASCII يضيف قوسين خارجها).
std::string renderBar(const TermCaps& c, double frac, int inner, St fillStyle, BarStyle style = BarStyle::Line) {
    frac = std::clamp(frac, 0.0, 1.0);
    inner = std::max(inner, 4);
    auto rep = [](const char* s, int n) { std::string r; for (int i = 0; i < n; ++i) r += s; return r; };
    if (c.unicode) {
        switch (style) {
            case BarStyle::Line: {
                int halves = static_cast<int>(std::lround(frac * inner * 2));
                int full = halves / 2;
                bool half = (halves % 2) == 1;
                std::string filled = rep("\xE2\x94\x81", full) + (half ? "\xE2\x95\xB8" : "");
                return paint(c, fillStyle, filled) + paint(c, St::Dim, rep("\xE2\x94\x80", inner - full - (half ? 1 : 0)));
            }
            case BarStyle::Block: {
                static const char* part[] = {"", "\xE2\x96\x8F", "\xE2\x96\x8E", "\xE2\x96\x8D", "\xE2\x96\x8C",
                                             "\xE2\x96\x8B", "\xE2\x96\x8A", "\xE2\x96\x89"};
                int eighths = static_cast<int>(std::lround(frac * inner * 8));
                int full = eighths / 8, rem = eighths % 8;
                std::string filled = rep("\xE2\x96\x88", full) + part[rem];
                return paint(c, fillStyle, filled) + paint(c, St::Dim, rep("\xE2\x96\x91", inner - full - (rem ? 1 : 0)));
            }
            case BarStyle::Dots: {
                int n = static_cast<int>(std::lround(frac * inner));
                return paint(c, fillStyle, rep("\xE2\x97\x8F", n)) + paint(c, St::Dim, rep("\xE2\x97\x8B", inner - n));
            }
            case BarStyle::Segments: {
                int n = static_cast<int>(std::lround(frac * inner));
                return paint(c, fillStyle, rep("\xE2\x96\xB0", n)) + paint(c, St::Dim, rep("\xE2\x96\xB1", inner - n));
            }
            case BarStyle::Shade: {
                int n = static_cast<int>(std::lround(frac * inner));
                return paint(c, fillStyle, rep("\xE2\x96\x93", n)) + paint(c, St::Dim, rep("\xE2\x96\x91", inner - n));
            }
        }
    }
    int full = static_cast<int>(frac * inner);
    bool complete = frac >= 1.0;
    std::string s = "[";
    for (int i = 0; i < inner; ++i) {
        if (i < full) s += '=';
        else if (i == full && !complete) s += '>';
        else s += '-';
    }
    s += "]";
    return paint(c, fillStyle, s);
}

// سطر مبني من قطع (نص + نمط). يُقتطع آخر قطعة قابلة للمرونة إن لزم، فلا يلتف السطر أبداً.
struct Seg { std::string text; St st = St::None; bool flex = false; bool raw = false; };
// raw = نص مُلوَّن مسبقاً (شريط)، عرضه يُمرَّر صراحةً عبر rawWidth.
struct Line {
    std::vector<Seg> segs;
    std::vector<std::size_t> rawW;
    void add(const std::string& t, St st = St::None, bool flex = false) { segs.push_back({t, st, flex, false}); rawW.push_back(0); }
    void addRaw(const std::string& painted, std::size_t width) { segs.push_back({painted, St::None, false, true}); rawW.push_back(width); }
    std::string render(const TermCaps& c, std::size_t maxCols, const std::string& ell) const {
        std::size_t total = 0;
        for (std::size_t i = 0; i < segs.size(); ++i) total += segs[i].raw ? rawW[i] : displayWidth(segs[i].text);
        std::vector<std::string> txt(segs.size());
        for (std::size_t i = 0; i < segs.size(); ++i) txt[i] = segs[i].text;
        if (total > maxCols) {
            for (std::size_t i = segs.size(); i-- > 0 && total > maxCols;) {
                if (segs[i].raw || !segs[i].flex) continue;
                std::size_t w = displayWidth(txt[i]);
                std::size_t over = total - maxCols;
                std::size_t target = w > over ? w - over : 0;
                txt[i] = truncateTo(txt[i], target, ell);
                total = total - w + displayWidth(txt[i]);
            }
        }
        std::string out;
        std::size_t used = 0;
        for (std::size_t i = 0; i < segs.size(); ++i) {
            std::size_t w = segs[i].raw ? rawW[i] : displayWidth(txt[i]);
            if (used + w > maxCols) break;   // لا مجال: نتوقف بدل الالتفاف
            used += w;
            out += segs[i].raw ? segs[i].text : paint(c, segs[i].st, txt[i]);
        }
        return out;
    }
};

std::string pctText(int p) {
    char b[8];
    std::snprintf(b, sizeof b, "%3d%%", std::clamp(p, 0, 100));
    return b;
}

// منطقة حيّة: تُعاد كتابتها في نفس الموضع (حركة المؤشر بـ ANSI) في إطار واحد لمنع الوميض.
class LiveRegion {
public:
    explicit LiveRegion(std::ostream& out) : out_(out) {}
    void draw(const std::vector<std::string>& lines) {
        std::string buf;
        eraseSeq(buf);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i) buf += "\n";
            buf += "\x1b[2K" + lines[i];
        }
        buf += "\x1b[J";   // امسح أي بقايا أسفل المنطقة
        lines_ = lines.size();
        out_ << buf << std::flush;
    }
    // يُنهي المنطقة ويطبع lines كمخرج دائم مكانها.
    void commit(const std::vector<std::string>& lines) {
        std::string buf;
        eraseSeq(buf);
        for (const auto& l : lines) buf += "\x1b[2K" + l + "\n";
        buf += "\x1b[J";
        lines_ = 0;
        out_ << buf << std::flush;
    }
    // سطر دائم فوق المنطقة (يُمحى السطر الحي ثم يُعاد رسمه بعد الطباعة من المستدعي).
    void printAbove(const std::string& line) {
        std::string buf;
        eraseSeq(buf);
        buf += "\x1b[2K" + line + "\n";
        lines_ = 0;
        out_ << buf << std::flush;
    }
    std::size_t lines() const { return lines_; }
private:
    void eraseSeq(std::string& buf) {
        if (lines_ > 1) buf += "\x1b[" + std::to_string(lines_ - 1) + "A";
        buf += "\r";
    }
    std::ostream& out_;
    std::size_t lines_ = 0;
};

}  // namespace

// ============================================================================
// detectTermCaps / formatDuration
// ============================================================================
static TermCaps detectTermCapsFd(const std::ostream& out, int fdHint);
TermCaps detectTermCaps(const std::ostream& out) { return detectTermCapsFd(out, -1); }
static TermCaps detectTermCapsFd(const std::ostream& out, int fdHint) {
    TermCaps c;
    int fd = fdHint > 0 ? fdHint : fdOf(out);
    c.tty = fdIsTty(fd);

    std::string term = envOr("TERM");
    bool dumb = (term == "dumb");
    bool noColorEnv = std::getenv("NO_COLOR") != nullptr && *envOr("NO_COLOR") != '\0';
    bool forceColor = std::string(envOr("CLICOLOR_FORCE")) == "1";
    std::string mode = envOr("RIN_PROGRESS");

#if defined(_WIN32)
    bool vt = false;
    if (c.tty) {
        HANDLE h = GetStdHandle(fd == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        DWORD m = 0;
        if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &m)) {
            vt = (m & ENABLE_VIRTUAL_TERMINAL_PROCESSING) || SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info))
            c.cols = info.srWindow.Right - info.srWindow.Left + 1;
        if (vt) SetConsoleOutputCP(CP_UTF8);
    }
    bool ansiOk = c.tty && vt && !dumb;
#else
    bool ansiOk = c.tty && !dumb;
#endif

    c.animate = ansiOk;
    if (mode == "plain") c.animate = false;
    else if (mode == "fancy" && !dumb) c.animate = true;

    c.color = (ansiOk || (mode == "fancy" && !dumb)) && !noColorEnv;
    if (forceColor && !noColorEnv && !dumb) c.color = true;
    c.color256 = c.color && (containsCI(term, "256") || containsCI(term, "direct") ||
                             *envOr("COLORTERM") != '\0' || std::string(envOr("WT_SESSION")) != "");
    // Unicode: نفترضه إلا إن صرّحت اللغة بغير UTF-8 (أو الطرفية الخام linux).
    c.unicode = true;
    if (c.tty || mode == "fancy") {
        const char* loc = *envOr("LC_ALL") ? envOr("LC_ALL") : (*envOr("LC_CTYPE") ? envOr("LC_CTYPE") : envOr("LANG"));
        if (*loc && !containsCI(loc, "utf-8") && !containsCI(loc, "utf8")) c.unicode = false;
        if (term == "linux") c.unicode = false;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (c.tty) {
        struct winsize ws;
        if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) c.cols = ws.ws_col;
    }
#endif
    if (c.cols == 80) {
        int envCols = std::atoi(envOr("COLUMNS"));
        if (envCols > 0) c.cols = envCols;
    }
    c.cols = std::clamp(c.cols, 20, 400);
    return c;
}

std::string formatDuration(double s) {
    char b[32];
    if (s < 0) s = 0;
    if (s < 1.0) { std::snprintf(b, sizeof b, "%dms", static_cast<int>(s * 1000.0)); return b; }
    if (s < 60.0) { std::snprintf(b, sizeof b, "%.1fs", s); return b; }
    int total = static_cast<int>(s);
    std::snprintf(b, sizeof b, "%dm %02ds", total / 60, total % 60);
    return b;
}

// ============================================================================
// ConsoleBarSink
// ============================================================================
struct ConsoleBarSink::Impl {
    struct Node {
        std::string name;
        std::size_t depth = 0;
        int percent = 0;
        std::string stage;
        bool done = false, ok = true, cached = false, embedded = false;
        std::string error;
        Clock::time_point t0 = Clock::now();
        double secs = 0;
    };

    Impl(std::ostream& o, const Options& opts) : out(o), opt(opts), live(o) {
        caps = detectTermCaps(o);
        if (opt.look == Look::Plain) caps.animate = false;
        else if (opt.look == Look::Live) caps.animate = true;
        g = Glyphs::make(caps.unicode, opt.style.spinner);
    }

    std::ostream& out;
    Options opt;
    TermCaps caps;
    Glyphs g;
    LiveRegion live;
    std::mutex mu;

    std::vector<Node> nodes;   // nodes[0] = الجذر (أعلى مستوى في هذه الجلسة)
    std::size_t base = 0;      // عمق الجذر
    bool active = false;
    std::size_t frame = 0;

    // --- مساعدات -----------------------------------------------------------
    int barInner(std::size_t reserved) const {
        int avail = caps.cols - static_cast<int>(reserved);
        return std::clamp(avail, 6, std::max(6, opt.barWidth));
    }

    Node* find(const std::string& name, std::size_t depth) {
        for (std::size_t i = nodes.size(); i-- > 0;)
            if (!nodes[i].done && nodes[i].depth == depth && nodes[i].name == name) return &nodes[i];
        return nullptr;
    }

    // بادئة الشجرة للعقدة i (├─ / └─ مع │ للأسلاف). الجذر بلا بادئة.
    std::string treePrefix(std::size_t i) const {
        if (i == 0) return "";
        auto isLast = [&](std::size_t k) {
            for (std::size_t j = k + 1; j < nodes.size(); ++j) {
                if (nodes[j].depth < nodes[k].depth) return true;
                if (nodes[j].depth == nodes[k].depth) return false;
            }
            return true;
        };
        std::size_t d = nodes[i].depth - base;   // >= 1
        std::vector<bool> ancestorLast(d + 1, true);
        // لكل مستوى بين 1..d-1: هل سلف ذلك المستوى الأخير بين إخوته؟
        std::size_t cur = i;
        for (std::size_t lvl = d; lvl >= 1; --lvl) {
            // ابحث عن أقرب عقدة سابقة بعمق base+lvl (سلف المستوى lvl)
            std::size_t k = cur;
            while (k > 0 && nodes[k].depth != base + lvl) --k;
            ancestorLast[lvl] = isLast(k);
            cur = k;
            if (lvl == 1) break;
        }
        std::string p = "  ";
        for (std::size_t lvl = 1; lvl < d; ++lvl) p += ancestorLast[lvl] ? g.blank : g.vert;
        p += ancestorLast[d] ? g.last : g.branch;
        return p;
    }

    std::string rootLabel() const { return "Importing " + nodes[0].name; }

    std::string tailFor(const Node& n) const {
        std::string t;
        if (n.cached) t = "already imported";
        else if (!n.ok) t = "failed: " + n.error;
        else {
            t = n.embedded ? std::string("loaded ") + g.dot + " embedded" : "loaded";
        }
        return t;
    }

    std::string timeTag(const Node& n) const {
        if (!opt.showElapsed) return "";
        return formatDuration(n.done ? n.secs : secondsSince(n.t0));
    }

    // سطر حي (قيد التقدّم) أو منتهٍ لعقدة.
    std::string lineFor(std::size_t i, std::size_t nameW) const {
        const Node& n = nodes[i];
        Line L;
        const std::size_t maxCols = static_cast<std::size_t>(caps.cols) - 1;
        std::string prefix = treePrefix(i);
        if (!prefix.empty()) L.add(prefix, St::Dim);

        auto glyphSeg = [&]() {
            if (!n.done) L.add(g.spinner(frame), St::Accent);
            else if (n.cached) L.add(g.skip, St::Dim);
            else if (n.ok) L.add(g.ok, St::Ok);
            else L.add(g.fail, St::Err);
            L.add(" ");
        };
        glyphSeg();

        if (i == 0) {
            if (!n.done) {
                L.add(rootLabel(), St::Bold, true);
                L.add("  ");
                std::size_t fixedW = displayWidth(prefix) + 2 + displayWidth(rootLabel()) + 2 + 5 + 2 + displayWidth(n.stage) + 2 + 8 + (caps.unicode ? 0 : 2);
                int inner = barInner(fixedW);
                L.addRaw(renderBar(caps, n.percent / 100.0, inner, St::Accent, opt.style.bar),
                         static_cast<std::size_t>(inner) + (caps.unicode ? 0 : 2));
                L.add(" " + pctText(n.percent), St::Bold);
                L.add("  ");
                L.add(n.stage, St::Dim, true);
                std::string tt = timeTag(n);
                if (!tt.empty()) L.add("  " + tt, St::Dim);
            } else {
                L.add(n.name, St::Bold, true);
                L.add("  ");
                L.add(tailFor(n), n.cached ? St::Dim : (n.ok ? St::Ok : St::Err), true);
                std::string tt = timeTag(n);
                if (!tt.empty() && !n.cached) L.add(std::string(" ") + g.dot + " " + tt, St::Dim);
            }
        } else {
            L.add(padRight(n.name, nameW), n.done ? St::None : St::Bold, true);
            L.add("  ");
            if (!n.done) {
                int inner = std::clamp(opt.barWidth / 2, 6, 14);
                if (static_cast<int>(displayWidth(prefix) + 2 + nameW + 2 + displayWidth(n.stage) + 8) + inner + 7 > caps.cols) inner = 6;
                L.addRaw(renderBar(caps, n.percent / 100.0, inner, St::Accent, opt.style.bar),
                         static_cast<std::size_t>(inner) + (caps.unicode ? 0 : 2));
                L.add(" " + pctText(n.percent), St::None);
                L.add("  ");
                L.add(n.stage, St::Dim, true);
            } else {
                L.add(tailFor(n), n.cached ? St::Dim : (n.ok ? St::Ok : St::Err), true);
                std::string tt = timeTag(n);
                if (!tt.empty() && !n.cached) L.add(std::string(" ") + g.dot + " " + tt, St::Dim);
            }
        }
        return L.render(caps, maxCols, g.ell);
    }

    std::size_t nameWidth() const {
        std::size_t w = 0;
        for (std::size_t i = 1; i < nodes.size(); ++i) w = std::max(w, displayWidth(nodes[i].name));
        return std::min<std::size_t>(w, 24);
    }

    std::vector<std::string> allLines() const {
        std::vector<std::string> v;
        std::size_t nw = nameWidth();
        for (std::size_t i = 0; i < nodes.size(); ++i) v.push_back(lineFor(i, nw));
        return v;
    }

    void redraw() { if (caps.animate) live.draw(allLines()); }

    // --- الوضع النظيف (بلا ANSI) --------------------------------------------
    std::string indent(const Node& n) const { return std::string(2 * (n.depth - base), ' '); }

    void plainBegin(const Node& n) {
        out << indent(n) << g.arrow << " Importing " << n.name << "\n";
    }
    void plainStage(const Node& n, LoadStage st, const std::string& detail) {
        // المراحل التفصيلية للجذر فقط؛ التبعيات تُبلَّغ ببدايتها ونهايتها (سجل مقروء لا مُغرِق).
        if (n.depth != base) return;
        out << indent(n) << "  [" << pctText(percentOf(st)) << "] " << (detail.empty() ? stageLabel(st) : detail) << "\n";
    }
    void plainEnd(const Node& n) {
        out << indent(n);
        if (n.cached) out << g.skip << " " << n.name << " already imported (skipped)\n";
        else if (n.ok) {
            out << g.ok << " " << n.name << " loaded";
            if (n.embedded) out << " (embedded)";
            if (opt.showElapsed) out << " " << g.dot << " " << formatDuration(n.secs);
            out << "\n";
        } else {
            out << g.fail << " " << n.name << " failed: " << n.error << "\n";
        }
    }

    // --- نهاية الجذر: ملخص دائم -------------------------------------------------
    void finalizeLive() {
        std::vector<std::string> lines = allLines();
        live.commit(lines);
    }
};

ConsoleBarSink::ConsoleBarSink(std::ostream& out, const Options& opts)
    : impl_(std::make_unique<Impl>(out, opts)) {}

ConsoleBarSink::ConsoleBarSink(std::ostream& out, int barWidth, bool useCarriageReturn)
    : impl_(std::make_unique<Impl>(out, [&] {
          Options o;
          o.barWidth = barWidth;
          if (!useCarriageReturn) o.look = Look::Plain;
          return o;
      }())) {}

ConsoleBarSink::~ConsoleBarSink() {
    // استيراد لم ينتهِ (مثلاً خروج مفاجئ): اترك الطرفية في حالة نظيفة.
    if (impl_ && impl_->active && impl_->caps.animate) {
        for (auto& n : impl_->nodes)
            if (!n.done) { n.done = true; n.ok = false; n.error = "interrupted"; n.secs = secondsSince(n.t0); }
        impl_->finalizeLive();
    }
}

void ConsoleBarSink::onBegin(const std::string& libraryName, std::size_t depth) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    Impl& m = *impl_;
    if (!m.active || depth <= m.base) {
        // جذر جديد (أو استيراد مستوى أعلى تالٍ): ابدأ منطقة جديدة نظيفة.
        if (m.active && m.caps.animate) m.finalizeLive();
        m.nodes.clear();
        m.base = depth;
        m.active = true;
        m.frame = 0;
    }
    Impl::Node n;
    n.name = libraryName;
    n.depth = std::max(depth, m.base);
    n.stage = "Starting";
    m.nodes.push_back(std::move(n));
    if (m.caps.animate) m.redraw();
    else m.plainBegin(m.nodes.back());
}

void ConsoleBarSink::onStage(const std::string& libraryName, LoadStage stage,
                              const std::string& detail, std::size_t depth) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    Impl& m = *impl_;
    Impl::Node* n = m.find(libraryName, depth);
    if (!n) return;
    n->percent = percentOf(stage);
    n->stage = detail.empty() ? (stage == LoadStage::Completed ? "Done" : stageLabel(stage)) : detail;
    ++m.frame;
    if (m.caps.animate) m.redraw();
    else m.plainStage(*n, stage, detail);
}

void ConsoleBarSink::onEnd(const std::string& libraryName, const LoadOutcome& outcome, std::size_t depth) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    Impl& m = *impl_;
    Impl::Node* n = m.find(libraryName, depth);
    if (!n) return;
    n->done = true;
    n->ok = outcome.success;
    n->cached = outcome.fromCache;
    n->embedded = outcome.fromEmbedded;
    n->error = outcome.errorMessage;
    n->secs = secondsSince(n->t0);
    if (outcome.success) n->percent = 100;
    ++m.frame;

    bool isRoot = (n == &m.nodes[0]);
    if (!m.caps.animate) {
        m.plainEnd(*n);
        if (isRoot) m.active = false;
        return;
    }
    if (isRoot) {
        m.finalizeLive();
        m.active = false;
    } else {
        m.redraw();
    }
}

// ============================================================================
// ProgressBar
// ============================================================================
struct ProgressBar::Impl {
    Impl(std::ostream& o, std::string t, std::size_t tot, IndicatorStyle st)
        : out(o), title(std::move(t)), total(tot), style(st), live(o) {
        caps = detectTermCaps(o);
        g = Glyphs::make(caps.unicode, st.spinner);
    }
    Impl(std::ostream& o, const TaskProfile& p, std::size_t tot, int fd)
        : out(o), title(p.verb), total(tot), style(p.style), live(o) {
        unit = p.unit; showEta = p.eta; showRate = p.rate;
        caps = detectTermCapsFd(o, fd);
        caps.accent256 = p.accent256;
        caps.accentBasic = p.accentBasic;
        g = Glyphs::make(caps.unicode, p.style.spinner);
    }
    std::string unit;
    bool showEta = false, showRate = false;

    std::ostream& out;
    std::string title, status;
    std::size_t total, done = 0, frame = 0;
    IndicatorStyle style;
    TermCaps caps;
    Glyphs g;
    LiveRegion live;
    Clock::time_point t0 = Clock::now();
    bool finished = false;
    std::mutex mu;
#if !defined(__EMSCRIPTEN__)
    std::thread ticker;
    std::condition_variable cv;
    bool stop = false;
#endif

    std::string barLine() const {
        Line L;
        double elapsed = secondsSince(t0);
        double frac = total ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
        int pct = static_cast<int>(std::lround(frac * 100));
        std::string counter = std::to_string(done) + "/" + std::to_string(total) + (unit.empty() ? "" : " " + unit);
        std::string elapsedTxt = formatDuration(elapsed);
        std::string etaTxt, rateTxt;
        if (showEta && done > 0 && done < total && elapsed > 0.05) {
            etaTxt = "ETA " + formatDuration(elapsed / static_cast<double>(done) * static_cast<double>(total - done));
        }
        if (showRate && done > 0 && elapsed > 0.05) {
            char b[32];
            std::snprintf(b, sizeof b, "%.1f%s/s", static_cast<double>(done) / elapsed, unit.empty() ? "" : (" " + unit).c_str());
            rateTxt = b;
        }
        std::size_t others = 2 + displayWidth(title) + 2 + 1 + displayWidth(counter) + 5 + 2 + displayWidth(elapsedTxt) +
                             (etaTxt.empty() ? 0 : 3 + displayWidth(etaTxt)) + (rateTxt.empty() ? 0 : 3 + displayWidth(rateTxt)) +
                             (caps.unicode ? 0 : 2) + 14;   // 14 = حد أدنى للحالة
        int inner = std::clamp(caps.cols - 1 - static_cast<int>(others), 8, 30);
        L.add(g.spinner(frame), St::Accent);
        L.add(" ");
        L.add(title, St::Bold);
        L.add("  ");
        L.addRaw(renderBar(caps, frac, inner, St::Accent, style.bar), static_cast<std::size_t>(inner) + (caps.unicode ? 0 : 2));
        L.add(" " + counter, St::None);
        L.add(" " + pctText(pct), St::Bold);
        L.add(std::string("  ") + elapsedTxt, St::Dim);
        if (!etaTxt.empty())  L.add(std::string(" ") + g.dot + " " + etaTxt, St::Warn);
        if (!rateTxt.empty()) L.add(std::string(" ") + g.dot + " " + rateTxt, St::Dim);
        if (!status.empty()) { L.add("  "); L.add(status, St::Dim, true); }
        return L.render(caps, static_cast<std::size_t>(caps.cols) - 1, g.ell);
    }

    void redraw() { if (caps.animate && !finished) live.draw({barLine()}); }

    void stopTicker() {
#if !defined(__EMSCRIPTEN__)
        {
            std::lock_guard<std::mutex> lk(mu);
            stop = true;
        }
        cv.notify_all();
        if (ticker.joinable()) ticker.join();
#endif
    }
};

static void startBarTicker(ProgressBar::Impl* m);

ProgressBar::ProgressBar(std::ostream& out, std::string title, std::size_t total, IndicatorStyle style)
    : impl_(std::make_unique<Impl>(out, std::move(title), total, style)) {
    startBarTicker(impl_.get());
}

ProgressBar::ProgressBar(std::ostream& out, const TaskProfile& profile, std::size_t total, int fd)
    : impl_(std::make_unique<Impl>(out, profile, total, fd)) {
    startBarTicker(impl_.get());
}

static void startBarTicker(ProgressBar::Impl* m) {
    if (m->caps.animate) {
        { std::lock_guard<std::mutex> lk(m->mu); m->redraw(); }
#if !defined(__EMSCRIPTEN__)
        m->ticker = std::thread([m] {
            std::unique_lock<std::mutex> lk(m->mu);
            while (!m->stop) {
                m->cv.wait_for(lk, std::chrono::milliseconds(80));
                if (m->stop) break;
                ++m->frame;
                m->redraw();
            }
        });
#endif
    }
}

ProgressBar::~ProgressBar() {
    if (!impl_) return;
    if (!impl_->finished) finish(false, "interrupted");
    impl_->stopTicker();
}

void ProgressBar::setStatus(const std::string& status) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->status = status;
    impl_->redraw();
}

void ProgressBar::advance(std::size_t n) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->done = std::min(impl_->total, impl_->done + n);
    impl_->redraw();
}

void ProgressBar::note(Glyph glyph, const std::string& text) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    Impl& m = *impl_;
    const char* sym = m.g.info;
    St st = St::None;
    switch (glyph) {
        case Glyph::Ok:   sym = m.g.ok;   st = St::Ok;   break;
        case Glyph::Fail: sym = m.g.fail; st = St::Err;  break;
        case Glyph::Warn: sym = m.g.warn; st = St::Warn; break;
        case Glyph::Skip: sym = m.g.skip; st = St::Dim;  break;
        case Glyph::Info: sym = m.g.info; st = St::Accent; break;
    }
    Line L;
    L.add(sym, st);
    L.add(" ");
    L.add(text, St::None, true);
    std::string line = L.render(m.caps, static_cast<std::size_t>(m.caps.cols) - 1, m.g.ell);
    if (m.caps.animate && !m.finished) {
        m.live.printAbove(line);
        m.redraw();
    } else {
        m.out << line << "\n" << std::flush;
    }
}

void ProgressBar::finish(bool success, const std::string& summary) {
    Impl& m = *impl_;
    {
        std::lock_guard<std::mutex> lk(m.mu);
        if (m.finished) return;
        if (success) m.done = m.total;
        Line L;
        L.add(success ? m.g.ok : m.g.fail, success ? St::Ok : St::Err);
        L.add(" ");
        L.add(summary, success ? St::Bold : St::Err, true);
        L.add("  " + formatDuration(secondsSince(m.t0)), St::Dim);
        std::string line = L.render(m.caps, static_cast<std::size_t>(m.caps.cols) - 1, m.g.ell);
        if (m.caps.animate) m.live.commit({line});
        else m.out << line << "\n" << std::flush;
        m.finished = true;
    }
    m.stopTicker();
}

// ============================================================================
// أنواع المؤشرات (parse/env)
// ============================================================================
const std::vector<std::string>& barStyleNames() {
    static const std::vector<std::string> v = {"line", "block", "dots", "segments", "shade"};
    return v;
}
const std::vector<std::string>& spinnerStyleNames() {
    static const std::vector<std::string> v = {"braille", "dots", "line", "arc", "bounce", "pulse", "arrow", "grow", "slide"};
    return v;
}
bool parseBarStyle(const std::string& name, BarStyle& out) {
    const auto& n = barStyleNames();
    for (std::size_t i = 0; i < n.size(); ++i) if (containsCI(name, n[i].c_str()) && name.size() == n[i].size()) { out = static_cast<BarStyle>(i); return true; }
    return false;
}
bool parseSpinnerStyle(const std::string& name, SpinnerStyle& out) {
    const auto& n = spinnerStyleNames();
    for (std::size_t i = 0; i < n.size(); ++i) if (containsCI(name, n[i].c_str()) && name.size() == n[i].size()) { out = static_cast<SpinnerStyle>(i); return true; }
    return false;
}
IndicatorStyle styleFromEnv() {
    IndicatorStyle s;
    parseBarStyle(envOr("RIN_PROGRESS_BAR"), s.bar);
    parseSpinnerStyle(envOr("RIN_PROGRESS_SPINNER"), s.spinner);
    return s;
}

// ============================================================================
// Spinner (غير محدَّد المدة)
// ============================================================================
struct Spinner::Impl {
    Impl(std::ostream& o, std::string m, IndicatorStyle st) : out(o), message(std::move(m)), live(o) {
        caps = detectTermCaps(o);
        g = Glyphs::make(caps.unicode, st.spinner);
    }
    Impl(std::ostream& o, const TaskProfile& p, std::string m, int fd) : out(o), message(std::move(m)), live(o) {
        caps = detectTermCapsFd(o, fd);
        caps.accent256 = p.accent256;
        caps.accentBasic = p.accentBasic;
        g = Glyphs::make(caps.unicode, p.style.spinner);
        verb = p.verb;
    }
    std::string verb;
    std::ostream& out;
    std::string message;
    TermCaps caps;
    Glyphs g;
    LiveRegion live;
    std::size_t frame = 0;
    Clock::time_point t0 = Clock::now();
    bool finished = false;
    std::mutex mu;
#if !defined(__EMSCRIPTEN__)
    std::thread ticker;
    std::condition_variable cv;
    bool stop = false;
#endif
    std::string spinLine() const {
        Line L;
        L.add(g.spinner(frame), St::Accent);
        L.add(" ");
        if (!verb.empty()) { L.add(verb, St::Bold); L.add("  "); }
        L.add(message, St::None, true);
        L.add("  " + formatDuration(secondsSince(t0)), St::Dim);
        return L.render(caps, static_cast<std::size_t>(caps.cols) - 1, g.ell);
    }
    void redraw() { if (caps.animate && !finished) live.draw({spinLine()}); }
    void halt() {
#if !defined(__EMSCRIPTEN__)
        { std::lock_guard<std::mutex> lk(mu); stop = true; }
        cv.notify_all();
        if (ticker.joinable()) ticker.join();
#endif
    }
    void end(const char* sym, St st, const std::string& text, bool keep) {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (finished) return;
            finished = true;
            Line L;
            L.add(sym, st);
            L.add(" ");
            L.add(text, St::None, true);
            L.add("  " + formatDuration(secondsSince(t0)), St::Dim);
            std::string line = L.render(caps, static_cast<std::size_t>(caps.cols) - 1, g.ell);
            if (caps.animate) { if (keep) live.commit({line}); else live.commit({}); }
            else if (keep) out << line << "\n" << std::flush;
        }
        halt();
    }
};

static void startSpinner(Spinner::Impl* m, std::ostream& out) {
    if (m->caps.animate) {
        { std::lock_guard<std::mutex> lk(m->mu); m->redraw(); }
#if !defined(__EMSCRIPTEN__)
        m->ticker = std::thread([m] {
            std::unique_lock<std::mutex> lk(m->mu);
            while (!m->stop) {
                m->cv.wait_for(lk, std::chrono::milliseconds(80));
                if (m->stop) break;
                ++m->frame;
                m->redraw();
            }
        });
#endif
    } else {
        out << m->g.info << " " << (m->verb.empty() ? "" : m->verb + "  ") << m->message << m->g.ell << "\n" << std::flush;
    }
}

Spinner::Spinner(std::ostream& out, std::string message, IndicatorStyle style)
    : impl_(std::make_unique<Impl>(out, std::move(message), style)) {
    startSpinner(impl_.get(), out);
}
Spinner::Spinner(std::ostream& out, const TaskProfile& profile, std::string message, int fd)
    : impl_(std::make_unique<Impl>(out, profile, std::move(message), fd)) {
    startSpinner(impl_.get(), out);
}
Spinner::~Spinner() { if (impl_) { impl_->end(impl_->g.fail, St::Err, "interrupted", true); impl_->halt(); } }
void Spinner::setMessage(const std::string& message) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->message = message;
    impl_->redraw();
}
void Spinner::succeed(const std::string& t) { impl_->end(impl_->g.ok, St::Ok, t, true); }
void Spinner::fail(const std::string& t)    { impl_->end(impl_->g.fail, St::Err, t, true); }
void Spinner::warn(const std::string& t)    { impl_->end(impl_->g.warn, St::Warn, t, true); }
void Spinner::stop()                        { impl_->end(impl_->g.info, St::None, "", false); }

// ============================================================================
// أنواع الأوامر / report / formatBytes
// ============================================================================
TaskProfile profileFor(Task t) {
    TaskProfile p;
    switch (t) {
        case Task::Install: p = {"Installing",  "pkgs",   {BarStyle::Line,     SpinnerStyle::Braille}, 141, 35, true,  false}; break;
        case Task::Test:    p = {"Testing",     "tests",  {BarStyle::Shade,    SpinnerStyle::Arc},      78, 32, true,  true};  break;
        case Task::Build:   p = {"Building",    "steps",  {BarStyle::Block,    SpinnerStyle::Grow},    214, 33, true,  false}; break;
        case Task::Check:   p = {"Checking",    "files",  {BarStyle::Segments, SpinnerStyle::Dots},     80, 36, true,  true};  break;
        case Task::Format:  p = {"Formatting",  "files",  {BarStyle::Dots,     SpinnerStyle::Line},    212, 35, true,  false}; break;
        case Task::Clean:   p = {"Cleaning",    "items",  {BarStyle::Segments, SpinnerStyle::Bounce},  111, 34, false, false}; break;
        case Task::Doctor:  p = {"Diagnosing",  "checks", {BarStyle::Dots,     SpinnerStyle::Pulse},   120, 32, false, false}; break;
        case Task::Run:     p = {"Running",     "",       {BarStyle::Line,     SpinnerStyle::Slide},   141, 35, false, false}; break;
        case Task::Load:    p = {"Loading",     "files",  {BarStyle::Line,     SpinnerStyle::Dots},    117, 36, false, false}; break;
        case Task::Eval:    p = {"Evaluating",  "",       {BarStyle::Line,     SpinnerStyle::Arrow},   183, 35, false, false}; break;
        case Task::Project: p = {"Scaffolding", "files",  {BarStyle::Segments, SpinnerStyle::Bounce},  150, 32, false, false}; break;
        case Task::Image:   p = {"Rendering",   "",       {BarStyle::Block,    SpinnerStyle::Dots},    213, 35, false, false}; break;
        case Task::Files:   p = {"Processing",  "files",  {BarStyle::Shade,    SpinnerStyle::Line},    180, 33, true,  true};  break;
        case Task::Hash:    p = {"Hashing",     "files",  {BarStyle::Block,    SpinnerStyle::Bounce},  222, 33, true,  false}; break;
        case Task::Bench:   p = {"Benchmarking","runs",   {BarStyle::Dots,     SpinnerStyle::Pulse},   209, 31, true,  true};  break;
        case Task::Generic: p = {"Working",     "",       {BarStyle::Line,     SpinnerStyle::Braille}, 141, 35, false, false}; break;
    }
    // متغيّرات البيئة (إن وُجدت وصحّت) تتغلّب على نمط النوع.
    parseBarStyle(envOr("RIN_PROGRESS_BAR"), p.style.bar);
    parseSpinnerStyle(envOr("RIN_PROGRESS_SPINNER"), p.style.spinner);
    return p;
}

Task taskForCommand(const std::string& n) {
    static const std::vector<std::pair<Task, std::vector<std::string>>> table = {
        {Task::Load,    {"load", "source", "reload"}},
        {Task::Run,     {"run"}},
        {Task::Test,    {"test"}},
        {Task::Check,   {"check", "verify"}},
        {Task::Project, {"new", "init"}},
        {Task::Image,   {"img", "ocr", "indsin"}},
        {Task::Bench,   {"time"}},
        {Task::Install, {"pkg"}},
        {Task::Hash,    {"sha1", "sha256", "md5", "crc32", "base64", "hex", "hexdump", "cmp"}},
        {Task::Files,   {"cat", "head", "tail", "wc", "grep", "find", "cp", "mv", "rm", "rename", "du", "df",
                         "diff", "comm", "dupes", "bigfiles", "recent", "backup", "trash", "split", "sort", "uniq",
                         "tree", "ls", "ll", "loc", "stats", "wordfreq", "replace", "sync", "csv", "json", "count"}},
    };
    for (const auto& row : table)
        for (const auto& name : row.second) if (name == n) return row.first;
    return Task::Generic;
}

std::string formatBytes(unsigned long long b) {
    static const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(b);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    char buf[32];
    if (i == 0) std::snprintf(buf, sizeof buf, "%llu B", b);
    else std::snprintf(buf, sizeof buf, "%.1f %s", v, u[i]);
    return buf;
}

void report(std::ostream& out, const TaskProfile& p, bool ok, const std::string& text, double seconds, int fd) {
    TermCaps c = detectTermCapsFd(out, fd);
    c.accent256 = p.accent256;
    c.accentBasic = p.accentBasic;
    Glyphs g = Glyphs::make(c.unicode, p.style.spinner);
    Line L;
    L.add(ok ? g.ok : g.fail, ok ? St::Ok : St::Err);
    L.add(" ");
    L.add(p.verb, St::Accent);
    L.add("  ");
    L.add(text, ok ? St::Bold : St::Err, true);
    L.add(std::string("  ") + g.dot + " " + formatDuration(seconds), St::Dim);
    out << L.render(c, static_cast<std::size_t>(c.cols) - 1, g.ell) << "\n" << std::flush;
}

// ============================================================================
// RunIndicator — مؤشر حيّ لتشغيل كود Rin (انظر الترويسة)
// ============================================================================
struct RunIndicator::Impl {
    Impl(std::ostream& o, const TaskProfile& p, std::string t, int fd, const RunIndicator::Options& op)
        : out(o), opts(op), target(std::move(t)), verb(p.verb), live(o) {
        caps = detectTermCapsFd(o, fd);
        caps.accent256 = p.accent256;
        caps.accentBasic = p.accentBasic;
        g = Glyphs::make(caps.unicode, p.style.spinner);
        // الرسم الحي يحتاج tty + ANSI، وإلا فلا شيء (لا ضجيج في السجلات).
        enabled = caps.animate && !op.forceInactive;
#if defined(__EMSCRIPTEN__)
        enabled = false;
#endif
    }
    std::ostream& out;
    RunIndicator::Options opts;
    TermCaps caps;
    Glyphs g;
    std::string target, verb, detail;
    std::vector<std::string> imports;   // مكدّس المكتبات الجارية (الأعمق أخيراً)
    LiveRegion live;

    std::mutex mu;
    bool enabled = false;
    bool drawn = false;       // سطر المؤشر مرسوم الآن
    bool everShown = false;
    bool paused = false;
    bool midLine = false;     // آخر مخرجات لم تنتهِ بسطر جديد: لا نرسم فوقها
    bool stopped = false;
    std::size_t frame = 0;
    std::size_t lines = 0;
    Clock::time_point t0 = Clock::now();
    Clock::time_point lastOut = Clock::now() - std::chrono::hours(1);
#if !defined(__EMSCRIPTEN__)
    std::thread ticker;
    std::condition_variable cv;
#endif

    std::string importDetail() const {
        if (imports.empty()) return detail;
        std::string d = "importing " + imports.back();
        if (!detail.empty()) d += " " + std::string(g.dot) + " " + detail;
        return d;
    }
    std::string render() const {
        double el = secondsSince(t0);
        Line L;
        L.add(g.spinner(frame), St::Accent);
        L.add(" ");
        L.add(verb, St::Bold);
        L.add("  ");
        std::string what = target;
        std::string d = importDetail();
        if (!d.empty()) what += std::string(" ") + g.dot + " " + d;
        if (lines > 0) what += std::string(" ") + g.dot + " " + std::to_string(lines) + (lines == 1 ? " line" : " lines");
        L.add(what, St::None, true);
        L.add("  " + formatDuration(el), St::Dim);
        if (!opts.hint.empty() && el >= 2.0) L.add("   " + opts.hint, St::Dim, true);
        return L.render(caps, static_cast<std::size_t>(caps.cols) - 1, g.ell);
    }
    // الاستدعاء بقفل mu.
    void eraseLocked() { if (drawn) { live.commit({}); drawn = false; } }
    void drawLocked() {
        if (!enabled || stopped || paused || midLine) return;
        if (secondsSince(t0) < opts.delaySec) return;
        if (secondsSince(lastOut) < 0.15) return;   // البرنامج يطبع الآن: لا نزاحمه
        live.draw({render()});
        drawn = true;
        everShown = true;
    }
};

RunIndicator::RunIndicator(std::ostream& out, const TaskProfile& profile, std::string target, int fd, const Options& opts)
    : impl_(std::make_shared<Impl>(out, profile, std::move(target), fd, opts)) {
#if !defined(__EMSCRIPTEN__)
    Impl* m = impl_.get();
    if (m->enabled) {
#if defined(__unix__) || defined(__APPLE__)
        // خيط الرسم لا يستقبل إشارات أبداً: Ctrl-C (SIGINT) يجب أن يصل لخيط التنفيذ الرئيسي كي تُقاطع
        // read()/sleep() الجارية فيه (الطرفية تعتمد ذلك). الخيط الجديد يرث القناع من الخيط المنشئ.
        sigset_t all, prev;
        sigfillset(&all);
        pthread_sigmask(SIG_BLOCK, &all, &prev);
#endif
        m->ticker = std::thread([m] {
            std::unique_lock<std::mutex> lk(m->mu);
            while (!m->stopped) {
                m->cv.wait_for(lk, std::chrono::milliseconds(80));
                if (m->stopped) break;
                ++m->frame;
                m->drawLocked();
            }
        });
#if defined(__unix__) || defined(__APPLE__)
        pthread_sigmask(SIG_SETMASK, &prev, nullptr);
#endif
    }
#endif
}
RunIndicator::RunIndicator(std::ostream& out, const TaskProfile& profile, std::string target, int fd)
    : RunIndicator(out, profile, std::move(target), fd, Options{}) {}
RunIndicator::~RunIndicator() { stop(); }

bool RunIndicator::active() const { return impl_->enabled && !impl_->stopped; }
bool RunIndicator::wasShown() const { std::lock_guard<std::mutex> lk(impl_->mu); return impl_->everShown; }
double RunIndicator::elapsed() const { return secondsSince(impl_->t0); }
void RunIndicator::setTarget(const std::string& t) { std::lock_guard<std::mutex> lk(impl_->mu); impl_->target = t; }
void RunIndicator::setDetail(const std::string& d) { std::lock_guard<std::mutex> lk(impl_->mu); impl_->detail = d; }

void RunIndicator::emit(const std::string& chunk, const std::function<void(const std::string&)>& writer) {
    if (!impl_->enabled) { writer(chunk); return; }
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->eraseLocked();
    writer(chunk);
    if (chunk.empty()) return;
    impl_->lastOut = Clock::now();
    impl_->midLine = chunk.back() != '\n';
    for (char c : chunk) if (c == '\n') ++impl_->lines;
}
void RunIndicator::pause() {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->eraseLocked();
    impl_->paused = true;
}
void RunIndicator::resume() {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->paused = false;
    impl_->lastOut = Clock::now();
}
void RunIndicator::stop() {
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (impl_->stopped) return;
        impl_->eraseLocked();
        impl_->stopped = true;
    }
#if !defined(__EMSCRIPTEN__)
    impl_->cv.notify_all();
    if (impl_->ticker.joinable()) impl_->ticker.join();
#endif
}

namespace {
class RunImportSink final : public ILoadSink {
public:
    explicit RunImportSink(std::shared_ptr<RunIndicator::Impl> m) : m_(std::move(m)) {}
    void onBegin(const std::string& lib, std::size_t) override {
        std::lock_guard<std::mutex> lk(m_->mu);
        m_->imports.push_back(lib);
        m_->detail.clear();
    }
    void onStage(const std::string&, LoadStage st, const std::string&, std::size_t) override {
        std::lock_guard<std::mutex> lk(m_->mu);
        m_->detail = stageLabel(st);
    }
    void onEnd(const std::string&, const LoadOutcome&, std::size_t) override {
        std::lock_guard<std::mutex> lk(m_->mu);
        if (!m_->imports.empty()) m_->imports.pop_back();
        m_->detail.clear();
    }
private:
    std::shared_ptr<RunIndicator::Impl> m_;
};
}  // namespace

std::shared_ptr<ILoadSink> RunIndicator::importSink() { return std::make_shared<RunImportSink>(impl_); }

std::function<bool(const std::string&, std::string&)> RunIndicator::inputProvider() {
    std::shared_ptr<Impl> m = impl_;
    return [this, m](const std::string& prompt, std::string& answer) {
        pause();
        if (!prompt.empty()) { std::cout << prompt; std::cout.flush(); }
        bool got = static_cast<bool>(std::getline(std::cin, answer));
        if (!got) answer.clear();
        if (!answer.empty() && answer.back() == '\r') answer.pop_back();
        {
            // السؤال والإجابة تركا المؤشر عند بداية سطر جديد (Enter) — يمكن الرسم من جديد.
            std::lock_guard<std::mutex> lk(m->mu);
            m->midLine = false;
        }
        resume();
        return got;   // EOF (Ctrl-D) = إلغاء الإدخال
    };
}

// ============================================================================
// LoadSession
// ============================================================================
LoadSession::LoadSession(std::shared_ptr<ILoadSink> sink, std::string libraryName, std::size_t depth)
    : sink_(std::move(sink)), name_(std::move(libraryName)), depth_(depth) {
    if (sink_) sink_->onBegin(name_, depth_);
}

LoadSession::~LoadSession() {
    if (!finished_ && sink_) {
        // جلسة انتهت دون finish() صريح (مثال: استثناء بين مرحلتين، تماماً كخطأ
        // Parser/RinError في @import الحالي) — تُبلَّغ كفشل بدل تجميد الشريط
        // عند آخر نسبة وصل إليها بصمت.
        LoadOutcome outcome;
        outcome.success = false;
        outcome.errorMessage = "import interrupted";
        sink_->onEnd(name_, outcome, depth_);
    }
}

void LoadSession::stage(LoadStage s, const std::string& detail) {
    if (finished_ || !sink_) return;
    sink_->onStage(name_, s, detail, depth_);
}

void LoadSession::finish(LoadOutcome outcome) {
    if (finished_) return;
    finished_ = true;
    if (sink_) sink_->onEnd(name_, outcome, depth_);
}

// ============================================================================
// makeSink
// ============================================================================
std::shared_ptr<ILoadSink> makeSink(Mode mode, std::ostream& out) {
    switch (mode) {
        case Mode::Verbose: return std::make_shared<ConsoleBarSink>(out, ConsoleBarSink::Options{});
        case Mode::Quiet:   return std::make_shared<SilentSink>();
    }
    return std::make_shared<SilentSink>();
}

} // namespace rin::loaderui
