// ============================================================================
//  rin terminal — batch 4 (part 4/4): generators, file tools, Rin source tools,
//  data tools, and registry4() which assembles the whole batch.
//  Header-only, POSIX + std only. No process is spawned (works on Android).
//  Rin source tools are text based (they never run the interpreter).
// ============================================================================
#pragma once

#include "rin_terminal_xcmds6.h"

#include <random>

namespace rin {
namespace terminal {
namespace xcmds {

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------
inline uint64_t randBelow(uint64_t n) { return std::uniform_int_distribution<uint64_t>(0, n - 1)(rng()); }
inline long long randRange(long long lo, long long hi) { return std::uniform_int_distribution<long long>(lo, hi)(rng()); }
inline bool intArg(Ctx& c, const std::string& s, const char* what, long long lo, long long hi, long long& out) {
    char* e = nullptr;
    long long v = std::strtoll(s.c_str(), &e, 10);
    if (s.empty() || !e || *e || v < lo || v > hi) { c.fail(std::string(what) + " must be an integer " + std::to_string(lo) + ".." + std::to_string(hi) + ": '" + s + "'"); return false; }
    out = v;
    return true;
}
// read every operand file; `fn` gets (name, data). Returns rc 1 if any file could not be read.
inline int eachFile(Ctx& c, const std::function<void(const std::string&, const std::string&)>& fn, size_t from = 0) {
    if (c.pos.size() <= from) return c.fail("missing file operand");
    for (size_t i = from; i < c.pos.size(); ++i) {
        std::string data, why;
        if (!readFileCapped(c.pos[i], data, why)) { c.warn(c.pos[i] + ": " + why); continue; }
        fn(c.pos[i], data);
    }
    return c.rc;
}
inline size_t dispWidth(const std::string& s) { size_t n = 0; for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n; return n; }
inline std::string padTo(const std::string& s, size_t w, bool right = false) {
    size_t d = dispWidth(s);
    if (d >= w) return s;
    return right ? std::string(w - d, ' ') + s : s + std::string(w - d, ' ');
}
inline std::string trimWs(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
inline char csvDelim(Ctx& c, const std::string& data) {
    if (c.hasv('d')) { std::string x = c.v('d'); return (x == "tab" || x == "\\t") ? '\t' : (x.empty() ? ',' : x[0]); }
    std::string first = data.substr(0, data.find('\n'));
    char d = ','; size_t best = 0;
    for (char cand : {',', ';', '\t', '|'}) { size_t n = static_cast<size_t>(std::count(first.begin(), first.end(), cand)); if (n > best) { best = n; d = cand; } }
    return d;
}
inline std::string csvField(const std::string& f, char d) {
    if (f.find_first_of(std::string("\"\n\r") + d) == std::string::npos) return f;
    std::string o = "\"";
    for (char ch : f) { if (ch == '"') o += '"'; o += ch; }
    return o + "\"";
}
inline bool plainNumber(const std::string& s) {
    size_t i = 0, n = s.size();
    if (i < n && s[i] == '-') ++i;
    if (i >= n || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    if (s[i] == '0' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1]))) return false;
    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i < n && s[i] == '.') { ++i; if (i >= n || !std::isdigit(static_cast<unsigned char>(s[i]))) return false; while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i; }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) { ++i; if (i < n && (s[i] == '+' || s[i] == '-')) ++i; if (i >= n || !std::isdigit(static_cast<unsigned char>(s[i]))) return false; while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i; }
    return i == n;
}
inline bool replaceFile(const std::string& path, const std::string& data, std::string& why) {
    struct stat st;
    mode_t mode = ::stat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0644;
    std::string tmp = path + ".rin-tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) { why = std::strerror(errno); return false; }
    size_t off = 0;
    while (off < data.size()) { ssize_t n = ::write(fd, data.data() + off, data.size() - off); if (n <= 0) { why = std::strerror(errno); ::close(fd); ::unlink(tmp.c_str()); return false; } off += static_cast<size_t>(n); }
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) { why = std::strerror(errno); ::unlink(tmp.c_str()); return false; }
    return true;
}

// ----------------------------------------------------------------------------
// D. generators (12)
// ----------------------------------------------------------------------------
inline int cmdDice(Ctx& c) {
    if (!c.parse("", "")) return 2;
    std::vector<std::string> specs = c.pos.empty() ? std::vector<std::string>{"1d6"} : c.pos;
    int rc = 0;
    for (auto& sp : specs) {
        long long n = 0, m = 0, k = 0; char x = 0; int used = 0;
        std::string s = lowerStr(sp);
        if (!s.empty() && s[0] == 'd') s = "1" + s;
        int got = std::sscanf(s.c_str(), "%lldd%lld%n", &n, &m, &used);
        if (got < 2) { c.warn("bad dice '" + sp + "'   (examples: 2d6, d20, 3d8+2)"); rc = 1; continue; }
        if (static_cast<size_t>(used) < s.size()) {
            if (std::sscanf(s.c_str() + used, "%lld%c", &k, &x) != 1) { c.warn("bad dice '" + sp + "'   (examples: 2d6, d20, 3d8+2)"); rc = 1; continue; }
        }
        if (n < 1 || n > 1000 || m < 2 || m > 1000000) { c.warn("dice out of range (1..1000 dice, 2..1000000 sides): " + sp); rc = 1; continue; }
        long long total = k; std::string rolls;
        for (long long i = 0; i < n; ++i) { long long v = randRange(1, m); total += v; if (i < 30) { if (i) rolls += ' '; rolls += std::to_string(v); } }
        if (n > 30) rolls += " …";
        c.say(sp + ":  [" + rolls + "]" + (k ? (k > 0 ? " +" : " ") + std::to_string(k) : "") + "  =  " + std::to_string(total));
    }
    return rc;
}
inline int cmdCoin(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long long n = 1;
    if (!c.pos.empty() && !intArg(c, c.pos[0], "count", 1, 100000, n)) return 1;
    if (n == 1) { c.say(randBelow(2) ? "heads" : "tails"); return 0; }
    long long h = 0; for (long long i = 0; i < n; ++i) h += static_cast<long long>(randBelow(2));
    c.say("heads " + std::to_string(h) + "   tails " + std::to_string(n - h));
    return 0;
}
inline int cmdPick(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ pick [-n COUNT] <item…>   e.g. $ pick tea coffee juice");
    long long n = 1;
    if (c.hasv('n') && !intArg(c, c.v('n'), "count", 1, 1000000, n)) return 1;
    std::vector<std::string> v = c.pos;
    if (static_cast<size_t>(n) > v.size()) return c.fail("cannot pick " + std::to_string(n) + " distinct items from " + std::to_string(v.size()));
    for (long long i = 0; i < n; ++i) { size_t j = static_cast<size_t>(i) + randBelow(v.size() - static_cast<size_t>(i)); std::swap(v[static_cast<size_t>(i)], v[j]); c.say(v[static_cast<size_t>(i)]); }
    return 0;
}
inline int cmdPassword(Ctx& c) {
    if (!c.parse("s", "ln")) return 2;
    long long len = 20, cnt = 1;
    if (c.hasv('l') && !intArg(c, c.v('l'), "length", 4, 256, len)) return 1;
    if (c.hasv('n') && !intArg(c, c.v('n'), "count", 1, 100, cnt)) return 1;
    if (!c.pos.empty() && !c.hasv('l') && !intArg(c, c.pos[0], "length", 4, 256, len)) return 1;
    std::vector<std::string> classes = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789"};
    if (c.has('s')) classes.push_back("!@#$%^&*()-_=+[]{};:,.?");
    std::string all; for (auto& k : classes) all += k;
    for (long long j = 0; j < cnt; ++j) {
        std::string p;
        for (auto& k : classes) p += k[randBelow(k.size())];                     // one of every class
        while (p.size() < static_cast<size_t>(len)) p += all[randBelow(all.size())];
        std::shuffle(p.begin(), p.end(), rng());
        c.say(p);
    }
    return 0;
}
inline int cmdLorem(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long long n = 30;
    if (!c.pos.empty() && !intArg(c, c.pos[0], "words", 1, 5000, n)) return 1;
    static const char* W[] = {"lorem", "ipsum", "dolor", "sit", "amet", "consectetur", "adipiscing", "elit", "sed", "do", "eiusmod", "tempor", "incididunt", "ut", "labore", "et", "dolore", "magna", "aliqua", "enim", "ad", "minim", "veniam", "quis", "nostrud", "exercitation", "ullamco", "laboris", "nisi", "aliquip", "ex", "ea", "commodo", "consequat", "duis", "aute", "irure", "in", "reprehenderit", "voluptate", "velit", "esse", "cillum", "eu", "fugiat", "nulla", "pariatur", "excepteur", "sint", "occaecat", "cupidatat", "non", "proident", "sunt", "culpa", "qui", "officia", "deserunt", "mollit", "anim", "id", "est", "laborum"};
    std::string o; bool start = true; int sentLen = 0, sentMax = static_cast<int>(randRange(6, 14));
    for (long long i = 0; i < n; ++i) {
        std::string w = W[randBelow(sizeof(W) / sizeof(W[0]))];
        if (start) w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        o += w; ++sentLen; start = false;
        if (i == n - 1 || sentLen >= sentMax) { o += '.'; start = true; sentLen = 0; sentMax = static_cast<int>(randRange(6, 14)); }
        if (i != n - 1) o += ' ';
    }
    c.say(o);
    return 0;
}
inline int cmdRandhex(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long long n = 16;
    if (!c.pos.empty() && !intArg(c, c.pos[0], "bytes", 1, 4096, n)) return 1;
    std::string b; for (long long i = 0; i < n; ++i) b += static_cast<char>(randBelow(256));
    c.say(hexOfStr(b));
    return 0;
}
inline int cmdToken(Ctx& c) {
    if (!c.parse("", "ln")) return 2;
    long long len = 32, cnt = 1;
    if (c.hasv('l') && !intArg(c, c.v('l'), "length", 1, 1024, len)) return 1;
    if (c.hasv('n') && !intArg(c, c.v('n'), "count", 1, 100, cnt)) return 1;
    if (!c.pos.empty() && !c.hasv('l') && !intArg(c, c.pos[0], "length", 1, 1024, len)) return 1;
    static const char A[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (long long j = 0; j < cnt; ++j) { std::string t; for (long long i = 0; i < len; ++i) t += A[randBelow(62)]; c.say(t); }
    return 0;
}
inline int cmdUlid(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long long n = 1;
    if (!c.pos.empty() && !intArg(c, c.pos[0], "count", 1, 1000, n)) return 1;
    static const char A[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (long long k = 0; k < n; ++k) {
        struct timeval tv; ::gettimeofday(&tv, nullptr);
        uint64_t ms = static_cast<uint64_t>(tv.tv_sec) * 1000 + static_cast<uint64_t>(tv.tv_usec) / 1000;
        std::string t(26, '0');
        for (int i = 9; i >= 0; --i) { t[static_cast<size_t>(i)] = A[ms & 31]; ms >>= 5; }
        for (int i = 10; i < 26; ++i) t[static_cast<size_t>(i)] = A[randBelow(32)];
        c.say(t);
    }
    return 0;
}
inline int cmdNanoid(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long long len = 21;
    if (!c.pos.empty() && !intArg(c, c.pos[0], "length", 1, 256, len)) return 1;
    static const char A[] = "useandom-26T198340PX75pxJACKVERYMINDBUSHWOLF_GQZbfghjklqvwyzrict";
    std::string t; for (long long i = 0; i < len; ++i) t += A[randBelow(64)];
    c.say(t);
    return 0;
}
inline int cmdRandint(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() < 2 || c.pos.size() > 3) return c.fail("usage: $ randint <lo> <hi> [count]");
    long long lo, hi, n = 1;
    if (!intArg(c, c.pos[0], "lo", -1000000000000LL, 1000000000000LL, lo) || !intArg(c, c.pos[1], "hi", -1000000000000LL, 1000000000000LL, hi)) return 1;
    if (c.pos.size() == 3 && !intArg(c, c.pos[2], "count", 1, 100000, n)) return 1;
    if (lo > hi) return c.fail("lo must be <= hi");
    for (long long i = 0; i < n; ++i) c.say(std::to_string(randRange(lo, hi)));
    return 0;
}
inline int cmdLottery(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ lottery <how-many> <max>   e.g. $ lottery 6 49");
    long long k, m;
    if (!intArg(c, c.pos[0], "how-many", 1, 1000, k) || !intArg(c, c.pos[1], "max", 1, 1000000, m)) return 1;
    if (k > m) return c.fail("cannot draw " + std::to_string(k) + " distinct numbers from 1.." + std::to_string(m));
    std::set<long long> s; while (static_cast<long long>(s.size()) < k) s.insert(randRange(1, m));
    std::string o; for (long long v : s) { if (!o.empty()) o += ' '; o += std::to_string(v); }
    c.say(o);
    return 0;
}
inline int cmdRandcolor(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long long n = 1;
    if (!c.pos.empty() && !intArg(c, c.pos[0], "count", 1, 1000, n)) return 1;
    for (long long i = 0; i < n; ++i) { char b[16]; std::snprintf(b, sizeof(b), "#%06x", static_cast<unsigned>(randBelow(0x1000000))); c.say(b); }
    return 0;
}

// ----------------------------------------------------------------------------
// E. file tools (13)
// ----------------------------------------------------------------------------
inline void treeWalk(Ctx& c, const std::string& dir, const std::string& prefix, bool hidden, int maxDepth, int depth, size_t& dirs, size_t& files, size_t& lines) {
    if (depth >= maxDepth || lines > 5000) return;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n == "." || n == ".." || n == ".git") continue; if (!hidden && n[0] == '.') continue; names.push_back(n); }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size(); ++i) {
        if (++lines > 5000) { c.say(prefix + "… (output limit reached)"); return; }
        std::string p = joinPath(dir, names[i]);
        struct stat st; bool isD = ::lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        bool last = i + 1 == names.size();
        c.say(prefix + (last ? "└── " : "├── ") + names[i] + (isD ? "/" : ""));
        if (isD) { ++dirs; treeWalk(c, p, prefix + (last ? "    " : "│   "), hidden, maxDepth, depth + 1, dirs, files, lines); } else ++files;
    }
}
inline int cmdTree(Ctx& c) {
    if (!c.parse("a", "L")) return 2;
    long long depth = 64;
    if (c.hasv('L') && !intArg(c, c.v('L'), "depth", 1, 64, depth)) return 1;
    std::string root = c.pos.empty() ? "." : c.pos[0];
    if (!isDir(root)) return c.fail("'" + root + "': not a directory");
    c.say(root);
    size_t dirs = 0, files = 0, lines = 0;
    treeWalk(c, root, "", c.has('a'), static_cast<int>(depth), 0, dirs, files, lines);
    c.say("\n" + std::to_string(dirs) + (dirs == 1 ? " directory, " : " directories, ") + std::to_string(files) + (files == 1 ? " file" : " files"));
    return 0;
}
inline int cmdFilesize(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing file operand");
    unsigned long long total = 0; size_t n = 0;
    for (auto& p : c.pos) {
        struct stat st;
        if (::stat(p.c_str(), &st) != 0) { c.warn("cannot stat '" + p + "': " + std::strerror(errno)); continue; }
        if (S_ISDIR(st.st_mode)) { c.warn("'" + p + "' is a directory (use $ du)"); continue; }
        unsigned long long sz = static_cast<unsigned long long>(st.st_size); total += sz; ++n;
        c.say(padTo(std::to_string(sz), 14, true) + " B  " + padTo(human(sz), 9, true) + "  " + p);
    }
    if (n > 1) c.say(padTo(std::to_string(total), 14, true) + " B  " + padTo(human(total), 9, true) + "  total");
    return c.rc;
}
inline int cmdExtstats(Ctx& c) {
    if (!c.parse("a", "")) return 2;
    bool trunc = false;
    auto fl = collectFiles(c.pos, c.has('a'), false, trunc);
    if (fl.empty()) { c.say("(no files)"); return 0; }
    struct G { size_t n = 0; unsigned long long b = 0; };
    std::map<std::string, G> m; unsigned long long all = 0;
    for (auto& f : fl) {
        std::string base = baseName(f.path); size_t dot = base.rfind('.');
        std::string ext = (dot == std::string::npos || dot == 0) ? "(none)" : lowerStr(base.substr(dot));
        auto& g = m[ext]; ++g.n; g.b += static_cast<unsigned long long>(f.st.st_size); all += static_cast<unsigned long long>(f.st.st_size);
    }
    std::vector<std::pair<std::string, G>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.b != b.second.b ? a.second.b > b.second.b : a.first < b.first; });
    c.say(padTo("EXT", 12) + padTo("FILES", 8, true) + padTo("SIZE", 11, true) + "   SHARE");
    for (auto& kv : v) {
        char sh[16]; std::snprintf(sh, sizeof(sh), "%5.1f%%", all ? 100.0 * static_cast<double>(kv.second.b) / static_cast<double>(all) : 0.0);
        c.say(padTo(kv.first, 12) + padTo(std::to_string(kv.second.n), 8, true) + padTo(human(kv.second.b), 11, true) + "  " + sh);
    }
    c.say(padTo("total", 12) + padTo(std::to_string(fl.size()), 8, true) + padTo(human(all), 11, true) + (trunc ? "   (truncated)" : ""));
    return 0;
}
inline int cmdOldest(Ctx& c) {
    if (!c.parse("a", "n")) return 2;
    long long n = 10;
    if (c.hasv('n') && !intArg(c, c.v('n'), "count", 1, 10000, n)) return 1;
    bool trunc = false;
    auto fl = collectFiles(c.pos, c.has('a'), false, trunc);
    std::sort(fl.begin(), fl.end(), [](const WalkEnt& a, const WalkEnt& b) { return a.st.st_mtime != b.st.st_mtime ? a.st.st_mtime < b.st.st_mtime : a.path < b.path; });
    for (size_t i = 0; i < fl.size() && static_cast<long long>(i) < n; ++i) c.say(padTo(agoStr(fl[i].st.st_mtime), 6, true) + " ago  " + padTo(human(static_cast<unsigned long long>(fl[i].st.st_size)), 9, true) + "  " + fl[i].path);
    if (fl.empty()) c.say("(no files)");
    return 0;
}
inline int cmdLonglines(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    long long lim = 100;
    if (c.hasv('n') && !intArg(c, c.v('n'), "length", 1, 100000, lim)) return 1;
    size_t found = 0;
    int rc = eachFile(c, [&](const std::string& f, const std::string& d) {
        auto ls = textLines(d); size_t shown = 0;
        for (size_t i = 0; i < ls.size(); ++i) {
            size_t w = dispWidth(ls[i]);
            if (static_cast<long long>(w) > lim) { ++found; if (shown++ < 200) c.say(f + ":" + std::to_string(i + 1) + ": " + std::to_string(w) + " chars"); }
        }
    });
    if (!found && !rc) c.say("no lines longer than " + std::to_string(lim) + " characters");
    return rc;
}
inline int cmdBadws(Ctx& c) {
    if (!c.parse("q", "")) return 2;
    size_t bad = 0;
    int rc = eachFile(c, [&](const std::string& f, const std::string& d) {
        if (looksBinary(d)) return;
        auto ls = textLines(d); size_t trail = 0, tabs = 0, shown = 0;
        for (size_t i = 0; i < ls.size(); ++i) {
            const std::string& l = ls[i];
            bool tw = !l.empty() && (l.back() == ' ' || l.back() == '\t');
            bool lt = l.find('\t') != std::string::npos;
            if (tw) ++trail;
            if (lt) ++tabs;
            if ((tw || lt) && !c.has('q') && shown++ < 10) c.say(f + ":" + std::to_string(i + 1) + ": " + (tw ? "trailing whitespace" : "tab character"));
        }
        size_t crlf = 0; for (size_t i = 1; i < d.size(); ++i) if (d[i] == '\n' && d[i - 1] == '\r') ++crlf;
        bool noeol = !d.empty() && d.back() != '\n';
        if (trail || tabs || crlf || noeol) {
            ++bad;
            c.say(f + ": " + std::to_string(trail) + " trailing-space lines, " + std::to_string(tabs) + " lines with tabs, " + std::to_string(crlf) + " CRLF" + (noeol ? ", no newline at end" : ""));
        }
    });
    if (!bad && !rc) c.say("clean: no whitespace problems found");
    return rc ? rc : (bad ? 1 : 0);
}
struct EolCount { size_t lf = 0, crlf = 0, cr = 0; };
inline EolCount countEol(const std::string& d) {
    EolCount e;
    for (size_t i = 0; i < d.size(); ++i) {
        if (d[i] == '\r') { if (i + 1 < d.size() && d[i + 1] == '\n') { ++e.crlf; ++i; } else ++e.cr; }
        else if (d[i] == '\n') ++e.lf;
    }
    return e;
}
inline int cmdEol(Ctx& c) {
    if (!c.parse("", "")) return 2;
    return eachFile(c, [&](const std::string& f, const std::string& d) {
        if (looksBinary(d)) { c.say(f + ": binary file"); return; }
        EolCount e = countEol(d);
        int kinds = (e.lf > 0) + (e.crlf > 0) + (e.cr > 0);
        std::string v = kinds == 0 ? "no line breaks" : kinds > 1 ? "MIXED" : e.lf ? "LF (unix)" : e.crlf ? "CRLF (dos)" : "CR (old mac)";
        c.say(f + ": " + v + "   (LF " + std::to_string(e.lf) + ", CRLF " + std::to_string(e.crlf) + ", CR " + std::to_string(e.cr) + ")");
    });
}
inline int cmdFixeol(Ctx& c) {
    if (!c.parse("n", "t")) return 2;
    std::string target = c.v('t', "unix");
    if (target != "unix" && target != "dos") return c.fail("-t must be 'unix' or 'dos'");
    return eachFile(c, [&](const std::string& f, const std::string& d) {
        if (looksBinary(d)) { c.warn(f + ": binary file, skipped"); return; }
        std::string o; o.reserve(d.size() + d.size() / 16);
        for (size_t i = 0; i < d.size(); ++i) {
            if (d[i] == '\r') { if (i + 1 < d.size() && d[i + 1] == '\n') ++i; o += target == "dos" ? "\r\n" : "\n"; }
            else if (d[i] == '\n') o += target == "dos" ? "\r\n" : "\n";
            else o += d[i];
        }
        if (o == d) { c.say(f + ": already " + target); return; }
        if (c.has('n')) { c.say(f + ": would convert to " + target); return; }
        std::string why;
        if (!replaceFile(f, o, why)) { c.warn(f + ": " + why); return; }
        c.say(f + ": converted to " + target);
    });
}
inline int cmdBom(Ctx& c) {
    if (!c.parse("r", "")) return 2;
    return eachFile(c, [&](const std::string& f, const std::string& d) {
        std::string k = d.compare(0, 3, "\xEF\xBB\xBF") == 0 ? "UTF-8 BOM" : d.compare(0, 2, "\xFF\xFE") == 0 ? "UTF-16 LE BOM" : d.compare(0, 2, "\xFE\xFF") == 0 ? "UTF-16 BE BOM" : "";
        if (k.empty()) { c.say(f + ": no BOM"); return; }
        if (c.has('r') && k == "UTF-8 BOM") {
            std::string why;
            if (replaceFile(f, d.substr(3), why)) c.say(f + ": UTF-8 BOM removed"); else c.warn(f + ": " + why);
        } else c.say(f + ": " + k + (k == "UTF-8 BOM" ? "   (use -r to remove)" : ""));
    });
}
inline std::string magicOf(const std::string& d) {
    auto sw = [&](const char* m, size_t n, size_t off = 0) { return d.size() >= off + n && std::memcmp(d.data() + off, m, n) == 0; };
    if (sw("\x89PNG\r\n\x1a\n", 8)) return "PNG image";
    if (sw("\xFF\xD8\xFF", 3)) return "JPEG image";
    if (sw("GIF87a", 6) || sw("GIF89a", 6)) return "GIF image";
    if (sw("RIFF", 4) && sw("WEBP", 4, 8)) return "WebP image";
    if (sw("RIFF", 4) && sw("WAVE", 4, 8)) return "WAV audio";
    if (sw("BM", 2) && d.size() > 14) return "BMP image";
    if (sw("%PDF-", 5)) return "PDF document";
    if (sw("PK\x03\x04", 4)) return "ZIP archive (also .apk .jar .docx .xlsx .rinpak)";
    if (sw("\x1f\x8b", 2)) return "gzip data";
    if (sw("BZh", 3)) return "bzip2 data";
    if (sw("\xFD" "7zXZ\x00", 6)) return "xz data";
    if (sw("7z\xBC\xAF\x27\x1C", 6)) return "7-Zip archive";
    if (sw("Rar!\x1a\x07", 6)) return "RAR archive";
    if (sw("\x7f" "ELF", 4)) return "ELF executable/library";
    if (sw("\x00" "asm", 4)) return "WebAssembly module";
    if (sw("SQLite format 3", 15)) return "SQLite database";
    if (sw("OggS", 4)) return "Ogg media";
    if (sw("ID3", 3)) return "MP3 audio (ID3)";
    if (sw("fLaC", 4)) return "FLAC audio";
    if (sw("ftyp", 4, 4)) return "MP4/MOV media";
    if (sw("\x1a\x45\xdf\xa3", 4)) return "Matroska/WebM media";
    if (sw("\xCA\xFE\xBA\xBE", 4)) return "Java class / Mach-O fat binary";
    if (sw("ustar", 5, 257)) return "tar archive";
    if (sw("#!", 2)) return "script (" + d.substr(0, d.find('\n')) + ")";
    if (sw("\xEF\xBB\xBF", 3)) return "UTF-8 text with BOM";
    if (d.empty()) return "empty";
    if (looksBinary(d)) return "binary data";
    size_t i = 0; bool utf8 = true;
    while (i < d.size() && i < 65536) {
        unsigned char ch = static_cast<unsigned char>(d[i]); size_t n = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : (ch >> 3) == 30 ? 4 : 0;
        if (!n || i + n > d.size()) { if (i + n <= d.size() || i + 4 < d.size()) utf8 = false; break; }
        for (size_t k = 1; k < n; ++k) if ((static_cast<unsigned char>(d[i + k]) & 0xC0) != 0x80) utf8 = false;
        if (!utf8) break;
        i += n;
    }
    bool ascii = true; for (size_t k = 0; k < d.size() && k < 65536; ++k) if (static_cast<unsigned char>(d[k]) >= 0x80) { ascii = false; break; }
    return ascii ? "ASCII text" : utf8 ? "UTF-8 text" : "text (unknown 8-bit encoding)";
}
inline int cmdMagic(Ctx& c) {
    if (!c.parse("", "")) return 2;
    return eachFile(c, [&](const std::string& f, const std::string& d) { c.say(f + ": " + magicOf(d)); });
}
inline int cmdSums(Ctx& c) {
    if (!c.parse("", "")) return 2;
    return eachFile(c, [&](const std::string& f, const std::string& d) {
        c.say(f);
        c.say("  md5     " + md5hex(d));
        c.say("  sha1    " + sha1hex(d));
        c.say("  sha256  " + sha256hex(d));
        c.say("  sha512  " + sha512Core(d, IV512, 64));
    });
}
inline int cmdCountin(Ctx& c) {
    if (!c.parse("i", "")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ countin [-i] <regex> <file…>");
    std::regex re;
    try { re = std::regex(c.pos[0], c.has('i') ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript); }
    catch (const std::regex_error& e) { return c.fail(std::string("bad regex: ") + e.what()); }
    size_t totalLines = 0, totalHits = 0;
    int rc = eachFile(c, [&](const std::string& f, const std::string& d) {
        size_t lines = 0, hits = 0;
        for (auto& l : textLines(d)) {
            size_t k = static_cast<size_t>(std::distance(std::sregex_iterator(l.begin(), l.end(), re), std::sregex_iterator()));
            if (k) { ++lines; hits += k; }
        }
        totalLines += lines; totalHits += hits;
        c.say(padTo(std::to_string(lines), 7, true) + " lines  " + padTo(std::to_string(hits), 7, true) + " matches  " + f);
    }, 1);
    if (c.pos.size() > 2) c.say(padTo(std::to_string(totalLines), 7, true) + " lines  " + padTo(std::to_string(totalHits), 7, true) + " matches  total");
    return rc;
}
inline int cmdLinesSel(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ lines <spec> <file>   spec: 5   5,9   10-20   30:   e.g. $ lines 1,10-12,40: main.rin");
    std::string data, why;
    if (!readFileCapped(c.pos[1], data, why)) return c.fail("cannot read '" + c.pos[1] + "': " + why);
    auto ls = textLines(data);
    std::vector<char> want(ls.size() + 2, 0);
    std::stringstream ss(c.pos[0]); std::string part;
    while (std::getline(ss, part, ',')) {
        long a = 0, b = 0; size_t sep = part.find_first_of("-:");
        char* e = nullptr;
        if (part.empty()) return c.fail("bad spec '" + c.pos[0] + "'");
        if (sep == std::string::npos) { a = b = std::strtol(part.c_str(), &e, 10); if (*e) return c.fail("bad spec part '" + part + "'"); }
        else {
            std::string l = part.substr(0, sep), r = part.substr(sep + 1);
            a = l.empty() ? 1 : std::strtol(l.c_str(), &e, 10); if (e && *e) return c.fail("bad spec part '" + part + "'");
            b = r.empty() ? static_cast<long>(ls.size()) : std::strtol(r.c_str(), &e, 10); if (e && *e) return c.fail("bad spec part '" + part + "'");
        }
        if (a < 1 || b < a) return c.fail("bad range '" + part + "' (lines start at 1)");
        for (long i = a; i <= b && static_cast<size_t>(i) <= ls.size(); ++i) want[static_cast<size_t>(i)] = 1;
    }
    size_t w = std::to_string(ls.size()).size(); bool any = false;
    for (size_t i = 1; i <= ls.size(); ++i) if (want[i]) { c.say(padTo(std::to_string(i), w, true) + "  " + ls[i - 1]); any = true; }
    if (!any) c.say("(no such lines; the file has " + std::to_string(ls.size()) + ")");
    return 0;
}

// ----------------------------------------------------------------------------
// F. Rin source tools (9) — text based heuristics on  @ns.Type=name … .end/ns  blocks
// ----------------------------------------------------------------------------
struct RinTok { bool open; std::string ns, type, id; size_t col; };
// events on one line, ignoring // comments and "strings"
inline std::vector<RinTok> rinEvents(const std::string& line) {
    std::vector<RinTok> ev;
    bool str = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (str) { if (ch == '\\') ++i; else if (ch == '"') str = false; continue; }
        if (ch == '"') { str = true; continue; }
        if (ch == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (ch == '@' && (i == 0 || !std::isalnum(static_cast<unsigned char>(line[i - 1])))) {
            size_t j = i + 1; std::string ns, ty, id;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ns += line[j++];
            if (ns.empty() || j >= line.size() || line[j] != '.') continue;
            ++j;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ty += line[j++];
            if (ty.empty()) continue;
            if (j < line.size() && line[j] == '=') { ++j; while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_' || line[j] == '-')) id += line[j++]; }
            ev.push_back({true, ns, ty, id, i});
            i = j - 1;
        } else if (ch == '.' && line.compare(i, 5, ".end/") == 0) {
            size_t j = i + 5; std::string ns;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ns += line[j++];
            if (!ns.empty()) { ev.push_back({false, ns, "", "", i}); i = j - 1; }
        }
    }
    return ev;
}
inline std::vector<std::string> rinSources(Ctx& c, bool& trunc) {
    std::vector<std::string> out;
    std::vector<std::string> roots = c.pos.empty() ? std::vector<std::string>{"."} : c.pos;
    for (auto& r : roots) {
        if (isDir(r)) { for (auto& f : collectFiles({r}, false, false, trunc)) if (f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".rin") == 0) out.push_back(f.path); }
        else out.push_back(r);
    }
    return out;
}
inline int eachRin(Ctx& c, const std::function<void(const std::string&, const std::string&)>& fn, size_t* nfiles = nullptr) {
    bool trunc = false;
    auto files = rinSources(c, trunc);
    if (nfiles) *nfiles = files.size();
    for (auto& f : files) {
        std::string d, why;
        if (!readFileCapped(f, d, why, 16u << 20)) { c.warn(f + ": " + why); continue; }
        fn(f, d);
    }
    if (trunc) c.warn("too many files; list truncated");
    return c.rc;
}
inline int cmdRinfiles(Ctx& c) {
    if (!c.parse("", "")) return 2;
    size_t tl = 0; unsigned long long tb = 0, n = 0;
    int rc = eachRin(c, [&](const std::string& f, const std::string& d) {
        size_t l = static_cast<size_t>(std::count(d.begin(), d.end(), '\n')) + (!d.empty() && d.back() != '\n');
        tl += l; tb += d.size(); ++n;
        c.say(padTo(std::to_string(l), 7, true) + " lines  " + padTo(human(d.size()), 9, true) + "  " + f);
    });
    if (!n) c.say("(no .rin files found)"); else c.say(padTo(std::to_string(tl), 7, true) + " lines  " + padTo(human(tb), 9, true) + "  " + std::to_string(n) + " files");
    return rc;
}
inline int cmdRinelements(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    std::map<std::string, size_t> m; size_t total = 0;
    int rc = eachRin(c, [&](const std::string&, const std::string& d) { for (auto& l : textLines(d)) for (auto& e : rinEvents(l)) if (e.open) { ++m[e.ns + "." + e.type]; ++total; } });
    std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    size_t lim = c.hasv('n') ? static_cast<size_t>(std::max(1L, std::strtol(c.v('n').c_str(), nullptr, 10))) : v.size();
    for (size_t i = 0; i < v.size() && i < lim; ++i) c.say(padTo(std::to_string(v[i].second), 7, true) + "  @" + v[i].first);
    c.say(padTo(std::to_string(total), 7, true) + "  elements, " + std::to_string(v.size()) + " kinds");
    return rc;
}
inline int cmdRinlint(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    long long maxLen = 160;
    if (c.hasv('n') && !intArg(c, c.v('n'), "max-length", 20, 100000, maxLen)) return 1;
    size_t issues = 0, nf = 0;
    eachRin(c, [&](const std::string& f, const std::string& d) {
        auto ls = textLines(d);
        std::vector<std::pair<std::string, size_t>> st;   // (ns, line)
        std::map<std::string, size_t> ids;
        auto say = [&](size_t line, const std::string& m) { ++issues; c.say(f + ":" + std::to_string(line) + ": " + m); };
        for (size_t i = 0; i < ls.size(); ++i) {
            const std::string& l = ls[i];
            if (!l.empty() && (l.back() == ' ' || l.back() == '\t')) say(i + 1, "trailing whitespace");
            if (l.find('\t') != std::string::npos) say(i + 1, "tab character (use spaces)");
            if (static_cast<long long>(dispWidth(l)) > maxLen) say(i + 1, "line longer than " + std::to_string(maxLen));
            for (auto& e : rinEvents(l)) {
                if (e.open) {
                    st.push_back({e.ns, i + 1});
                    if (!e.id.empty()) { auto it = ids.find(e.ns + "." + e.id); if (it != ids.end()) say(i + 1, "duplicate id '" + e.id + "' (first at line " + std::to_string(it->second) + ")"); else ids[e.ns + "." + e.id] = i + 1; }
                } else if (st.empty()) say(i + 1, ".end/" + e.ns + " has no matching opening element");
                else if (st.back().first != e.ns) { say(i + 1, ".end/" + e.ns + " closes '" + st.back().first + "' opened at line " + std::to_string(st.back().second)); st.pop_back(); }
                else st.pop_back();
            }
        }
        for (auto& o : st) say(o.second, "element @" + o.first + " is never closed (.end/" + o.first + ")");
        if (!d.empty() && d.back() != '\n') say(ls.size(), "no newline at end of file");
    }, &nf);
    if (!nf) { c.say("(no .rin files found)"); return 0; }
    c.say(issues ? std::to_string(issues) + " issue(s) in " + std::to_string(nf) + " file(s)" : "ok: no issues in " + std::to_string(nf) + " file(s)");
    return issues ? 1 : 0;
}
inline int cmdRinblocks(Ctx& c) {
    if (!c.parse("", "L")) return 2;
    long long maxD = 64;
    if (c.hasv('L') && !intArg(c, c.v('L'), "depth", 1, 64, maxD)) return 1;
    return eachRin(c, [&](const std::string& f, const std::string& d) {
        c.say(f); long depth = 0; size_t lines = 0;
        for (auto& l : textLines(d)) for (auto& e : rinEvents(l)) {
            if (e.open) { if (depth < maxD && ++lines < 3000) c.say(std::string(static_cast<size_t>(depth) * 2 + 2, ' ') + "@" + e.ns + "." + e.type + (e.id.empty() ? "" : " = " + e.id)); ++depth; }
            else if (depth > 0) --depth;
        }
    });
}
inline int cmdRindepth(Ctx& c) {
    if (!c.parse("", "")) return 2;
    return eachRin(c, [&](const std::string& f, const std::string& d) {
        long depth = 0, maxd = 0; size_t at = 0, ln = 0;
        for (auto& l : textLines(d)) { ++ln; for (auto& e : rinEvents(l)) { if (e.open) { ++depth; if (depth > maxd) { maxd = depth; at = ln; } } else if (depth > 0) --depth; } }
        c.say(padTo(std::to_string(maxd), 4, true) + " levels  (deepest at line " + std::to_string(at) + ")  " + f);
    });
}
inline int cmdRincomments(Ctx& c) {
    if (!c.parse("", "")) return 2;
    size_t tc = 0, tk = 0, tb = 0;
    int rc = eachRin(c, [&](const std::string& f, const std::string& d) {
        size_t code = 0, com = 0, blank = 0;
        for (auto& l : textLines(d)) { std::string t = trimWs(l); if (t.empty()) ++blank; else if (t.compare(0, 2, "//") == 0) ++com; else ++code; }
        tc += com; tk += code; tb += blank;
        char b[48]; std::snprintf(b, sizeof(b), "%5.1f%%", code + com ? 100.0 * static_cast<double>(com) / static_cast<double>(code + com) : 0.0);
        c.say("code " + padTo(std::to_string(code), 6, true) + "  comments " + padTo(std::to_string(com), 5, true) + " (" + b + ")  blank " + padTo(std::to_string(blank), 5, true) + "  " + f);
    });
    c.say("code " + std::to_string(tk) + ", comments " + std::to_string(tc) + ", blank " + std::to_string(tb));
    return rc;
}
inline int cmdRinattrs(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    std::map<std::string, size_t> m;
    int rc = eachRin(c, [&](const std::string&, const std::string& d) {
        for (auto& l : textLines(d)) {
            bool str = false;
            for (size_t i = 0; i < l.size(); ++i) {
                if (str) { if (l[i] == '\\') ++i; else if (l[i] == '"') str = false; continue; }
                if (l[i] == '/' && i + 1 < l.size() && l[i + 1] == '/') break;
                if (l[i] == '"') {
                    size_t j = i; while (j > 0 && l[j - 1] == ' ') --j;
                    if (j > 0 && l[j - 1] == '=') { --j; while (j > 0 && l[j - 1] == ' ') --j; size_t e = j; while (j > 0 && (std::isalnum(static_cast<unsigned char>(l[j - 1])) || l[j - 1] == '_')) --j; if (e > j) ++m[l.substr(j, e - j)]; }
                    str = true;
                }
            }
        }
    });
    std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    size_t lim = c.hasv('n') ? static_cast<size_t>(std::max(1L, std::strtol(c.v('n').c_str(), nullptr, 10))) : v.size();
    for (size_t i = 0; i < v.size() && i < lim; ++i) c.say(padTo(std::to_string(v[i].second), 7, true) + "  " + v[i].first);
    if (v.empty()) c.say("(no attributes found)");
    return rc;
}
inline int cmdRinstrings(Ctx& c) {
    if (!c.parse("", "a")) return 2;
    std::set<std::string> attrs = {"text", "title", "label", "placeholder", "hint"};
    if (c.hasv('a')) { attrs.clear(); std::stringstream ss(c.v('a')); std::string p; while (std::getline(ss, p, ',')) if (!p.empty()) attrs.insert(p); }
    size_t n = 0;
    int rc = eachRin(c, [&](const std::string& f, const std::string& d) {
        auto ls = textLines(d);
        for (size_t i = 0; i < ls.size(); ++i) {
            const std::string& l = ls[i];
            for (auto& a : attrs) {
                size_t pos = 0;
                while ((pos = l.find(a, pos)) != std::string::npos) {
                    size_t e = pos + a.size(); bool okL = pos == 0 || !(std::isalnum(static_cast<unsigned char>(l[pos - 1])) || l[pos - 1] == '_');
                    while (e < l.size() && l[e] == ' ') ++e;
                    if (okL && e < l.size() && l[e] == '=') { ++e; while (e < l.size() && l[e] == ' ') ++e; if (e < l.size() && l[e] == '"') { size_t q = e + 1; while (q < l.size() && l[q] != '"') { if (l[q] == '\\') ++q; ++q; } if (q < l.size()) { ++n; c.say(f + ":" + std::to_string(i + 1) + ": " + a + " = " + l.substr(e, q - e + 1)); } } }
                    pos += a.size();
                }
            }
        }
    });
    if (!n && !rc) c.say("(no matching strings)");
    return rc;
}
inline int cmdRinids(Ctx& c) {
    if (!c.parse("", "")) return 2;
    size_t dup = 0, n = 0;
    int rc = eachRin(c, [&](const std::string& f, const std::string& d) {
        std::map<std::string, size_t> seen; size_t ln = 0;
        for (auto& l : textLines(d)) { ++ln; for (auto& e : rinEvents(l)) if (e.open && !e.id.empty()) {
            ++n; auto it = seen.find(e.id);
            if (it != seen.end()) { ++dup; c.say(f + ":" + std::to_string(ln) + ": " + e.id + "  (@" + e.ns + "." + e.type + ")  DUPLICATE of line " + std::to_string(it->second)); }
            else { seen[e.id] = ln; c.say(f + ":" + std::to_string(ln) + ": " + e.id + "  (@" + e.ns + "." + e.type + ")"); }
        } }
    });
    c.say(std::to_string(n) + " ids, " + std::to_string(dup) + " duplicate(s)");
    return rc ? rc : (dup ? 1 : 0);
}

// ----------------------------------------------------------------------------
// G. data tools (11)
// ----------------------------------------------------------------------------
inline int cmdSpark(Ctx& c) {
    std::vector<double> v;
    if (!numsIn(c, v, 1)) return 1;
    static const char* B[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    auto mm = std::minmax_element(v.begin(), v.end());
    double lo = *mm.first, hi = *mm.second; std::string o;
    for (double x : v) o += B[hi == lo ? 3 : std::min(7, static_cast<int>((x - lo) / (hi - lo) * 8))];
    c.say(o + "   min " + fnum(lo) + "  max " + fnum(hi));
    return 0;
}
inline int cmdBarchart(Ctx& c) {
    if (!c.parse("", "w")) return 2;
    long long width = 40;
    if (c.hasv('w') && !intArg(c, c.v('w'), "width", 5, 200, width)) return 1;
    std::vector<std::pair<std::string, double>> rows;
    double probe = 0;
    if (c.pos.size() == 1 && !parseNum(c.pos[0], probe)) {
        std::string data, why;
        if (!readFileCapped(c.pos[0], data, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
        for (auto& l : textLines(data)) {
            std::string t = trimWs(l); if (t.empty()) continue;
            size_t sp = t.find_last_of(" \t,;"); double d;
            if (sp == std::string::npos || !parseNum(trimWs(t.substr(sp + 1)), d)) return c.fail("bad line (expected 'label value'): " + t);
            rows.push_back({trimWs(t.substr(0, sp)), d});
        }
    } else {
        if (c.pos.size() < 2 || c.pos.size() % 2) return c.fail("usage: $ barchart [-w WIDTH] <label value …>   or   $ barchart <file with 'label value' lines>");
        for (size_t i = 0; i < c.pos.size(); i += 2) { double d; if (!parseNum(c.pos[i + 1], d)) return c.fail("not a number: '" + c.pos[i + 1] + "'"); rows.push_back({c.pos[i], d}); }
    }
    if (rows.empty()) return c.fail("no data");
    size_t lw = 0; double mx = 0; for (auto& r : rows) { lw = std::max(lw, dispWidth(r.first)); mx = std::max(mx, std::fabs(r.second)); }
    for (auto& r : rows) {
        size_t n = mx > 0 ? static_cast<size_t>(std::round(std::fabs(r.second) / mx * static_cast<double>(width))) : 0;
        std::string bar; for (size_t i = 0; i < n; ++i) bar += "█";
        c.say(padTo(r.first, lw) + " │" + bar + " " + fnum(r.second));
    }
    return 0;
}
inline std::vector<std::string> splitCols(const std::string& l, const std::string& sep) {
    std::vector<std::string> v;
    if (sep.empty()) return wordsOf(l);
    size_t i = 0;
    for (;;) { size_t j = l.find(sep, i); if (j == std::string::npos) { v.push_back(l.substr(i)); break; } v.push_back(l.substr(i, j - i)); i = j + sep.size(); }
    return v;
}
inline int cmdColumns(Ctx& c) {
    // raw tokens: -s SEP / -r, everything else (or -f FILE) is the text
    std::string sep; bool right = false; size_t skip = 0;
    for (;;) {
        if (skip < c.toks.size() && c.toks[skip].s == "-r") { right = true; ++skip; continue; }
        if (skip + 1 < c.toks.size() && c.toks[skip].s == "-s") { sep = decodeEsc(c.toks[skip + 1].s); skip += 2; continue; }
        break;
    }
    std::string t;
    if (!textIn(c, t, skip)) return 1;
    std::vector<std::vector<std::string>> rows; std::vector<size_t> w;
    for (auto& l : textLines(t)) {
        if (trimWs(l).empty()) continue;
        auto cols = splitCols(l, sep);
        for (size_t i = 0; i < cols.size(); ++i) { if (sep.size()) cols[i] = trimWs(cols[i]); if (w.size() <= i) w.push_back(0); w[i] = std::max(w[i], dispWidth(cols[i])); }
        rows.push_back(cols);
    }
    for (auto& r : rows) {
        std::string o;
        for (size_t i = 0; i < r.size(); ++i) { if (i) o += "  "; o += (i + 1 == r.size() && !right) ? r[i] : padTo(r[i], w[i], right); }
        c.say(o);
    }
    return 0;
}
inline int cmdTranspose(Ctx& c) {
    std::string sep; size_t skip = 0;
    if (c.toks.size() > 1 && c.toks[0].s == "-s") { sep = decodeEsc(c.toks[1].s); skip = 2; }
    std::string t;
    if (!textIn(c, t, skip)) return 1;
    std::vector<std::vector<std::string>> rows; size_t mw = 0;
    for (auto& l : textLines(t)) { if (trimWs(l).empty()) continue; rows.push_back(splitCols(l, sep)); mw = std::max(mw, rows.back().size()); }
    std::string joinWith = sep.empty() ? " " : sep;
    for (size_t j = 0; j < mw; ++j) { std::string o; for (size_t i = 0; i < rows.size(); ++i) { if (i) o += joinWith; if (j < rows[i].size()) o += rows[i][j]; } c.say(o); }
    return 0;
}
inline bool csvLoad(Ctx& c, size_t fileIdx, std::vector<std::vector<std::string>>& rows, char& d) {
    std::string data, why;
    if (c.pos.size() <= fileIdx) { c.fail("missing file operand"); return false; }
    if (!readFileCapped(c.pos[fileIdx], data, why)) { c.fail("cannot read '" + c.pos[fileIdx] + "': " + why); return false; }
    d = csvDelim(c, data);
    rows = parseCsv(data, d);
    if (rows.empty()) { c.fail("'" + c.pos[fileIdx] + "' is empty"); return false; }
    return true;
}
inline int cmdCsvcol(Ctx& c) {
    if (!c.parse("", "d")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ csvcol [-d DELIM] <col[,col…]> <file>   col = number (from 1) or header name");
    std::vector<std::vector<std::string>> rows; char d;
    if (!csvLoad(c, c.pos.size() - 1, rows, d)) return 1;
    std::vector<size_t> idx; std::stringstream ss(c.pos[0]); std::string p;
    while (std::getline(ss, p, ',')) {
        char* e = nullptr; long k = std::strtol(p.c_str(), &e, 10);
        if (!p.empty() && e && !*e) { if (k < 1 || static_cast<size_t>(k) > rows[0].size()) return c.fail("column " + p + " is out of range (1.." + std::to_string(rows[0].size()) + ")"); idx.push_back(static_cast<size_t>(k - 1)); }
        else { auto it = std::find(rows[0].begin(), rows[0].end(), p); if (it == rows[0].end()) return c.fail("no column named '" + p + "'"); idx.push_back(static_cast<size_t>(it - rows[0].begin())); }
    }
    for (auto& r : rows) { std::string o; for (size_t i = 0; i < idx.size(); ++i) { if (i) o += d; o += csvField(idx[i] < r.size() ? r[idx[i]] : "", d); } c.say(o); }
    return 0;
}
inline int cmdCsvsort(Ctx& c) {
    if (!c.parse("rnH", "dk")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ csvsort [-d DELIM] [-k COL] [-n] [-r] [-H no header] <file>");
    std::vector<std::vector<std::string>> rows; char d;
    if (!csvLoad(c, 0, rows, d)) return 1;
    size_t key = 0;
    if (c.hasv('k')) {
        std::string k = c.v('k'); char* e = nullptr; long n = std::strtol(k.c_str(), &e, 10);
        if (!k.empty() && e && !*e) { if (n < 1 || static_cast<size_t>(n) > rows[0].size()) return c.fail("column out of range"); key = static_cast<size_t>(n - 1); }
        else { auto it = std::find(rows[0].begin(), rows[0].end(), k); if (it == rows[0].end()) return c.fail("no column named '" + k + "'"); key = static_cast<size_t>(it - rows[0].begin()); }
    }
    std::vector<std::string> head; if (!c.has('H')) { head = rows[0]; rows.erase(rows.begin()); }
    bool num = c.has('n'), rev = c.has('r');
    std::stable_sort(rows.begin(), rows.end(), [&](const std::vector<std::string>& a, const std::vector<std::string>& b) {
        std::string x = key < a.size() ? a[key] : "", y = key < b.size() ? b[key] : "";
        bool lt;
        if (num) { double p = 0, q = 0; parseNum(x, p); parseNum(y, q); lt = p < q; if (rev) lt = q < p; }
        else { lt = x < y; if (rev) lt = y < x; }
        return lt;
    });
    auto emit = [&](const std::vector<std::string>& r) { std::string o; for (size_t i = 0; i < r.size(); ++i) { if (i) o += d; o += csvField(r[i], d); } c.say(o); };
    if (!c.has('H')) emit(head);
    for (auto& r : rows) emit(r);
    return 0;
}
inline int cmdCsv2json(Ctx& c) {
    if (!c.parse("H", "d")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ csv2json [-d DELIM] [-H no header: use col1 col2 …] <file>");
    std::vector<std::vector<std::string>> rows; char d;
    if (!csvLoad(c, 0, rows, d)) return 1;
    std::vector<std::string> head;
    if (c.has('H')) { for (size_t i = 0; i < rows[0].size(); ++i) head.push_back("col" + std::to_string(i + 1)); }
    else { head = rows[0]; rows.erase(rows.begin()); }
    std::string o = "[";
    for (size_t r = 0; r < rows.size(); ++r) {
        o += r ? ",\n  {" : "\n  {";
        for (size_t i = 0; i < head.size(); ++i) {
            if (i) o += ", ";
            std::string v = i < rows[r].size() ? rows[r][i] : "";
            o += "\"" + jsonEsc(head[i]) + "\": ";
            if (plainNumber(v) || v == "true" || v == "false" || v == "null") o += v; else o += "\"" + jsonEsc(v) + "\"";
        }
        o += "}";
    }
    o += rows.empty() ? "]" : "\n]";
    c.say(o);
    return 0;
}
inline int cmdCsv2md(Ctx& c) {
    if (!c.parse("", "d")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ csv2md [-d DELIM] <file>");
    std::vector<std::vector<std::string>> rows; char d;
    if (!csvLoad(c, 0, rows, d)) return 1;
    size_t nc = 0; for (auto& r : rows) nc = std::max(nc, r.size());
    std::vector<size_t> w(nc, 3);
    auto cell = [](std::string s) { std::string o; for (char ch : s) { if (ch == '|') o += "\\|"; else if (ch == '\n') o += ' '; else o += ch; } return o; };
    for (auto& r : rows) for (size_t i = 0; i < r.size(); ++i) w[i] = std::max(w[i], dispWidth(cell(r[i])));
    auto line = [&](const std::vector<std::string>& r) { std::string o = "|"; for (size_t i = 0; i < nc; ++i) o += " " + padTo(i < r.size() ? cell(r[i]) : "", w[i]) + " |"; c.say(o); };
    line(rows[0]);
    std::string sepL = "|"; for (size_t i = 0; i < nc; ++i) sepL += " " + std::string(w[i], '-') + " |";
    c.say(sepL);
    for (size_t r = 1; r < rows.size(); ++r) line(rows[r]);
    return 0;
}
inline void jsonPaths(Ctx& c, const JV& v, const std::string& path, size_t& n) {
    if (n > 2000) return;
    auto kind = [](const JV& x) { return x.t == 'o' ? "object" : x.t == 'a' ? "array" : x.t == 's' ? "string" : x.t == 'n' ? "number" : x.t == 'b' ? "boolean" : "null"; };
    if (!path.empty()) { ++n; c.say(padTo(path, 40) + " " + kind(v) + (v.t == 'a' ? " (" + std::to_string(v.items.size()) + " items)" : v.t == 'o' ? " (" + std::to_string(v.items.size()) + " keys)" : "")); }
    if (v.t == 'o') for (size_t i = 0; i < v.items.size(); ++i) jsonPaths(c, v.items[i], (path.empty() ? "" : path + ".") + jsonUnquote(v.keys[i]), n);
    else if (v.t == 'a' && !v.items.empty()) jsonPaths(c, v.items[0], path + "[]", n);
}
inline int cmdJsonkeys(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 1) return c.fail("usage: $ jsonkeys <file>   (lists every key path and its type)");
    std::string data, why;
    if (!readFileCapped(c.pos[0], data, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
    JsonParser p(data); JV root;
    if (!p.value(root, 0)) return c.fail(c.pos[0] + ": " + p.err);
    p.ws();
    if (p.i < data.size()) { p.fail("unexpected data after the value"); return c.fail(c.pos[0] + ": " + p.err); }
    size_t n = 0;
    if (root.t != 'o' && root.t != 'a') { c.say(c.pos[0] + ": top level is a scalar"); return 0; }
    c.say(std::string("(root)  ") + (root.t == 'o' ? "object" : "array") + " (" + std::to_string(root.items.size()) + (root.t == 'o' ? " keys)" : " items)"));
    if (root.t == 'a') { if (!root.items.empty()) jsonPaths(c, root.items[0], "[]", n); } else jsonPaths(c, root, "", n);
    if (n > 2000) c.say("… (output limit reached)");
    return 0;
}
inline int cmdIniget(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty() || c.pos.size() > 3) return c.fail("usage: $ iniget <file> [section] [key]   (no section: list sections; section only: list keys)");
    std::string data, why;
    if (!readFileCapped(c.pos[0], data, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
    std::vector<std::string> order; std::map<std::string, std::vector<std::pair<std::string, std::string>>> sec; std::string cur = "";
    for (auto& raw : textLines(data)) {
        std::string l = trimWs(raw);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        if (l[0] == '[' && l.back() == ']') { cur = trimWs(l.substr(1, l.size() - 2)); if (!sec.count(cur)) order.push_back(cur); sec[cur]; continue; }
        size_t eq = l.find_first_of("=:");
        if (eq == std::string::npos) continue;
        if (!sec.count(cur)) order.push_back(cur);
        std::string v = trimWs(l.substr(eq + 1));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) v = v.substr(1, v.size() - 2);
        sec[cur].push_back({trimWs(l.substr(0, eq)), v});
    }
    if (c.pos.size() == 1) { for (auto& s : order) c.say(s.empty() ? "(global)" : s); return 0; }
    std::string want = c.pos[1] == "-" || c.pos[1] == "global" ? "" : c.pos[1];
    auto it = sec.find(want);
    if (it == sec.end()) return c.fail("no section '" + c.pos[1] + "'");
    if (c.pos.size() == 2) { for (auto& kv : it->second) c.say(kv.first + " = " + kv.second); return 0; }
    for (auto& kv : it->second) if (kv.first == c.pos[2]) { c.say(kv.second); return 0; }
    return c.fail("no key '" + c.pos[2] + "' in section '" + c.pos[1] + "'");
}
inline int cmdLinefreq(Ctx& c) {
    if (!c.parse("i", "n")) return 2;
    long long top = 20;
    if (c.hasv('n') && !intArg(c, c.v('n'), "count", 1, 100000, top)) return 1;
    std::map<std::string, size_t> m; size_t total = 0;
    int rc = eachFile(c, [&](const std::string&, const std::string& d) { for (auto& l : textLines(d)) { ++total; ++m[c.has('i') ? lowerStr(l) : l]; } });
    std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    for (size_t i = 0; i < v.size() && static_cast<long long>(i) < top; ++i) c.say(padTo(std::to_string(v[i].second), 7, true) + "  " + v[i].first);
    c.say(std::to_string(total) + " lines, " + std::to_string(v.size()) + " distinct");
    return rc;
}

// ----------------------------------------------------------------------------
// registry (batch 4) — merged into registry() in rin_terminal_xcmds.h
// ----------------------------------------------------------------------------
inline void addBatch4Misc(Reg& r, size_t from, size_t to) {
    struct E { const char* n; const char* u; const char* d; bool f; int (*fn)(Ctx&); };
    static const E T[] = {
        {"dice", "$ dice [NdM[+K]…]", "roll dice, e.g. $ dice 2d6 d20 3d8+2", false, cmdDice},
        {"coin", "$ coin [count]", "flip a coin (or many)", false, cmdCoin},
        {"pick", "$ pick [-n COUNT] <item…>", "pick random item(s) from the arguments", false, cmdPick},
        {"password", "$ password [-l LEN] [-n COUNT] [-s]", "random password (-s adds symbols; every class is used)", false, cmdPassword},
        {"lorem", "$ lorem [words]", "lorem ipsum placeholder text", false, cmdLorem},
        {"randhex", "$ randhex [bytes]", "random bytes as hex (default 16)", false, cmdRandhex},
        {"token", "$ token [-l LEN] [-n COUNT]", "random base62 token (default 32 characters)", false, cmdToken},
        {"ulid", "$ ulid [count]", "sortable unique id (ULID)", false, cmdUlid},
        {"nanoid", "$ nanoid [length]", "short URL-safe random id (default 21)", false, cmdNanoid},
        {"randint", "$ randint <lo> <hi> [count]", "random integers in lo..hi", false, cmdRandint},
        {"lottery", "$ lottery <how-many> <max>", "draw distinct numbers from 1..max, sorted", false, cmdLottery},
        {"randcolor", "$ randcolor [count]", "random #rrggbb colors", false, cmdRandcolor},
        {"tree", "$ tree [-a] [-L DEPTH] [dir]", "directory tree (e.g. $ tree -L 2 ~/)", true, cmdTree},
        {"filesize", "$ filesize <file…>", "exact size in bytes and human form", true, cmdFilesize},
        {"extstats", "$ extstats [-a] [dir…]", "file count and size per extension", true, cmdExtstats},
        {"oldest", "$ oldest [-n N] [-a] [dir…]", "least recently modified files", true, cmdOldest},
        {"longlines", "$ longlines [-n LEN] <file…>", "lines longer than LEN characters (default 100)", true, cmdLonglines},
        {"badws", "$ badws [-q] <file…>", "trailing whitespace, tabs, CRLF, missing final newline", true, cmdBadws},
        {"eol", "$ eol <file…>", "detect line endings (LF / CRLF / CR / mixed)", true, cmdEol},
        {"fixeol", "$ fixeol [-n] [-t unix|dos] <file…>", "convert line endings in place (-n: dry run)", true, cmdFixeol},
        {"bom", "$ bom [-r] <file…>", "detect a byte-order mark (-r removes a UTF-8 BOM)", true, cmdBom},
        {"magic", "$ magic <file…>", "identify a file by its content (signatures)", true, cmdMagic},
        {"sums", "$ sums <file…>", "MD5, SHA-1, SHA-256 and SHA-512 of files", true, cmdSums},
        {"countin", "$ countin [-i] <regex> <file…>", "count matching lines and matches per file", true, cmdCountin},
        {"lines", "$ lines <spec> <file>", "print chosen lines, e.g. $ lines 1,10-12,40: main.rin", true, cmdLinesSel},
        {"rinfiles", "$ rinfiles [dir|file…]", "list .rin sources with line counts", true, cmdRinfiles},
        {"rinelements", "$ rinelements [-n N] [dir|file…]", "how often each @ns.Type element is used", true, cmdRinelements},
        {"rinlint", "$ rinlint [-n MAXLEN] [dir|file…]", "check blocks, duplicate ids, tabs, trailing spaces, long lines", true, cmdRinlint},
        {"rinblocks", "$ rinblocks [-L DEPTH] [dir|file…]", "outline of the element tree of Rin sources", true, cmdRinblocks},
        {"rindepth", "$ rindepth [dir|file…]", "deepest element nesting per file", true, cmdRindepth},
        {"rincomments", "$ rincomments [dir|file…]", "code / comment / blank line ratio", true, cmdRincomments},
        {"rinattrs", "$ rinattrs [-n N] [dir|file…]", "attribute names used in Rin sources, by frequency", true, cmdRinattrs},
        {"rinstrings", "$ rinstrings [-a attr,attr] [dir|file…]", "list user-visible strings (text, title, label, placeholder, hint)", true, cmdRinstrings},
        {"rinids", "$ rinids [dir|file…]", "list element ids and flag duplicates", true, cmdRinids},
        {"spark", "$ spark <numbers…>", "sparkline of numbers", false, cmdSpark},
        {"barchart", "$ barchart [-w W] <label value …|file>", "text bar chart", true, cmdBarchart},
        {"columns", "$ columns [-s SEP] [-r] <text|-f FILE>", "align text into columns (-r right-align)", true, cmdColumns},
        {"transpose", "$ transpose [-s SEP] <text|-f FILE>", "swap rows and columns", true, cmdTranspose},
        {"csvcol", "$ csvcol [-d D] <col[,col…]> <file>", "extract CSV columns by number or header name", true, cmdCsvcol},
        {"csvsort", "$ csvsort [-d D] [-k COL] [-n] [-r] [-H] <file>", "sort a CSV file by a column", true, cmdCsvsort},
        {"csv2json", "$ csv2json [-d D] [-H] <file>", "CSV to a JSON array of objects", true, cmdCsv2json},
        {"csv2md", "$ csv2md [-d D] <file>", "CSV to a Markdown table", true, cmdCsv2md},
        {"jsonkeys", "$ jsonkeys <file>", "list every key path of a JSON file with its type", true, cmdJsonkeys},
        {"iniget", "$ iniget <file> [section] [key]", "read INI / properties files", true, cmdIniget},
        {"linefreq", "$ linefreq [-i] [-n N] <file…>", "most frequent identical lines", true, cmdLinefreq},
    };
    for (size_t i = from; i < to && i < sizeof(T) / sizeof(T[0]); ++i) { const E& e = T[i]; int (*fn)(Ctx&) = e.fn; r.add(e.n, e.u, e.d, e.f, [fn](Ctx& c) { return fn(c); }); }
}

// Rin Toolbox families (used by :toolbox): start/count are indexes into registry4()
struct ToolFamily { std::string key, title; size_t start, count; };
inline std::vector<ToolFamily>& toolFamiliesMut() { static std::vector<ToolFamily> v; return v; }

inline const std::vector<Entry>& registry4() {
    static const std::vector<Entry> r = [] {
        static Reg g;                               // owns the C strings the Spec pointers refer to
        auto fam = [&](const char* key, const char* title, const std::function<void()>& add) {
            size_t s0 = g.v.size();
            add();
            toolFamiliesMut().push_back({key, title, s0, g.v.size() - s0});
        };
        fam("units",      "Units & conversion",       [&] { addUnits(g); });
        fam("text",       "Text tools",               [&] { addText4(g); });
        fam("encoding",   "Encodings",                [&] { addCodecs(g); });
        fam("hashes",     "Hashes & checksums",       [&] { addHashes(g); });
        fam("colors",     "Colors",                   [&] { addColors(g); });
        fam("network",    "Network & reference",      [&] { addNet(g); });
        fam("math",       "Math, stats, finance",     [&] { addMathCmds(g); });
        fam("dates",      "Dates & calendars",        [&] { addDateCmds(g); });
        fam("generators", "Generators",               [&] { addBatch4Misc(g, 0, 12); });
        fam("files",      "File tools",               [&] { addBatch4Misc(g, 12, 25); });
        fam("rin",        "Rin source tools",         [&] { addBatch4Misc(g, 25, 34); });
        fam("data",       "Data tools",               [&] { addBatch4Misc(g, 34, 45); });
        return g.v;
    }();
    return r;
}
inline const std::vector<ToolFamily>& toolFamilies() { registry4(); return toolFamiliesMut(); }

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin
