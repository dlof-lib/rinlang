// ============================================================================
//  rin terminal — extra commands (batch 1: 50; batch 2: rin_terminal_xcmds2.h; batch 3: rin_terminal_xcmds3.h) (header-only, POSIX + std only).
//
//  Shared by the Linux terminal (`rin terminal`) and RinStudio (the embedded
//  terminal compiled into librinengine.so). No process is spawned: every
//  command is implemented natively, so they behave the same on Android.
//
//  Style
//    $ cmd args …            `$` and `:` both start a command  ($ ls == :ls)
//    $ cp *.rin ~/           `*` ? [..] globs, `~/` = home directory
//    $ seq 1:10   $ cat f -r 5:12   $ cut -f 2:4 f      `A:B` / `A…B` ranges
//
//  All text printed by these commands is English only.
// ============================================================================
#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utime.h>

#include <regex>

extern char** environ;

namespace rin {
namespace terminal {
namespace xcmds {

struct Io {
    std::function<void(const std::string&)> out;   // raw text (already contains newlines)
    std::function<void(const std::string&)> err;   // one error line (no trailing newline)
};

struct Spec {
    const char* name;
    const char* usage;   // always starts with "$ "
    const char* desc;    // English
    bool files;          // arguments are paths (Tab completion)
};

// ----------------------------------------------------------------------------
// small helpers
// ----------------------------------------------------------------------------
inline std::string cwd() {
    char b[4096];
    return ::getcwd(b, sizeof(b)) ? std::string(b) : std::string(".");
}
inline bool isDir(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
inline bool exists(const std::string& p) { struct stat st; return ::lstat(p.c_str(), &st) == 0; }
inline bool hasWild(const std::string& s) { return s.find_first_of("*?[") != std::string::npos; }
inline bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
inline std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}
inline std::string baseName(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    if (p == "/") return p;
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
inline std::string dirName(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t s = p.find_last_of('/');
    if (s == std::string::npos) return ".";
    if (s == 0) return "/";
    return p.substr(0, s);
}
inline std::string tildeExpand(const std::string& p) {
    if (!p.empty() && p[0] == '~' && (p.size() == 1 || p[1] == '/')) {
        const char* h = std::getenv("HOME");
        if (h) return std::string(h) + p.substr(1);
    }
    return p;
}
inline std::string human(unsigned long long v) {
    static const char* u[] = {"B", "K", "M", "G", "T", "P"};
    double d = static_cast<double>(v);
    int i = 0;
    while (d >= 1024.0 && i < 5) { d /= 1024.0; ++i; }
    char b[64];
    if (i == 0) std::snprintf(b, sizeof(b), "%llu%s", v, u[0]);
    else std::snprintf(b, sizeof(b), "%.1f%s", d, u[i]);
    return b;
}
inline std::string decodeEsc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case '0': o += '\0'; break;
                case '\\': o += '\\'; break;
                default: o += '\\'; o += n; break;
            }
        } else o += s[i];
    }
    return o;
}
// "N", "A:B", "A…B", "A..B", ":B", "A:"  → 1-based inclusive [a,b]
inline bool parseRange(const std::string& in, long& a, long& b) {
    std::string s = in;
    size_t pos = std::string::npos, len = 0;
    if ((pos = s.find("\xE2\x80\xA6")) != std::string::npos) len = 3;
    else if ((pos = s.find("..")) != std::string::npos) len = 2;
    else if ((pos = s.find(':')) != std::string::npos) len = 1;
    auto num = [](const std::string& t, long dflt, long& out) {
        if (t.empty()) { out = dflt; return true; }
        char* e = nullptr;
        long v = std::strtol(t.c_str(), &e, 10);
        if (!e || *e != 0) return false;
        out = v; return true;
    };
    if (pos == std::string::npos) { if (!num(s, 1, a)) return false; b = a; return !s.empty(); }
    if (!num(s.substr(0, pos), 1, a)) return false;
    if (!num(s.substr(pos + len), 0x7fffffffL, b)) return false;
    return a >= 1 && b >= a;
}
inline std::vector<std::string> utf8Chars(const std::string& s) {
    std::vector<std::string> v;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + n > s.size()) n = 1;
        v.push_back(s.substr(i, n));
        i += n;
    }
    return v;
}
inline std::vector<std::string> textLines(const std::string& t) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i < t.size()) {
        size_t j = t.find('\n', i);
        std::string l = t.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(l);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return v;
}
inline bool readFileCapped(const std::string& p, std::string& out, std::string& why, size_t cap = 64u << 20) {
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) { why = std::strerror(errno); return false; }
    if (S_ISDIR(st.st_mode)) { why = "Is a directory"; return false; }
    if (st.st_size > 0 && static_cast<size_t>(st.st_size) > cap) { why = "file too large (limit 64 MiB)"; return false; }
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) { why = std::strerror(errno); return false; }
    out.clear();
    char buf[65536];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > cap) { ::close(fd); why = "file too large (limit 64 MiB)"; return false; }
    }
    ::close(fd);
    return true;
}

// ----------------------------------------------------------------------------
// tokenizer:  'single'  "double"  \escape  $VAR ${VAR}  ~/  and * ? [..] globs
// ----------------------------------------------------------------------------
struct Tok { std::string s; bool glob = false; };

inline size_t expandVar(const std::string& in, size_t i, std::string& cur) {
    size_t n = in.size();
    size_t j = i + 1;
    std::string name;
    if (j < n && in[j] == '{') {
        size_t e = in.find('}', j);
        if (e == std::string::npos) { cur += '$'; return i + 1; }
        name = in.substr(j + 1, e - j - 1);
        j = e + 1;
    } else {
        while (j < n && (std::isalnum(static_cast<unsigned char>(in[j])) || in[j] == '_')) name += in[j++];
        if (name.empty()) { cur += '$'; return i + 1; }
    }
    if (const char* v = std::getenv(name.c_str())) cur += v;
    return j;
}

inline std::vector<Tok> tokenize(const std::string& in) {
    std::vector<Tok> out;
    std::string cur;
    bool have = false, glob = false, tilde = false;
    auto flush = [&]() {
        if (have) out.push_back({tilde ? tildeExpand(cur) : cur, glob});
        cur.clear(); have = glob = tilde = false;
    };
    size_t i = 0, n = in.size();
    while (i < n) {
        char ch = in[i];
        if (ch == ' ' || ch == '\t') { flush(); ++i; continue; }
        have = true;
        if (ch == '\'') {
            ++i;
            while (i < n && in[i] != '\'') cur += in[i++];
            if (i < n) ++i;
            continue;
        }
        if (ch == '"') {
            ++i;
            while (i < n && in[i] != '"') {
                if (in[i] == '\\' && i + 1 < n && (in[i + 1] == '"' || in[i + 1] == '\\' || in[i + 1] == '$')) { cur += in[i + 1]; i += 2; }
                else if (in[i] == '$') i = expandVar(in, i, cur);
                else cur += in[i++];
            }
            if (i < n) ++i;
            continue;
        }
        if (ch == '\\' && i + 1 < n) { cur += in[i + 1]; i += 2; continue; }
        if (ch == '$') { i = expandVar(in, i, cur); continue; }
        if (ch == '~' && cur.empty()) tilde = true;
        if (ch == '*' || ch == '?' || ch == '[') glob = true;
        cur += ch; ++i;
    }
    flush();
    return out;
}

inline std::vector<std::string> globExpand(const std::string& pat) {
    std::vector<std::string> parts;
    {
        std::string c;
        for (char ch : pat) { if (ch == '/') { if (!c.empty()) parts.push_back(c); c.clear(); } else c += ch; }
        if (!c.empty()) parts.push_back(c);
    }
    std::vector<std::string> cur{pat[0] == '/' ? "/" : ""};
    for (const auto& part : parts) {
        std::vector<std::string> next;
        for (const auto& base : cur) {
            if (!hasWild(part)) { next.push_back(joinPath(base, part)); continue; }
            DIR* d = ::opendir(base.empty() ? "." : base.c_str());
            if (!d) continue;
            while (dirent* e = ::readdir(d)) {
                std::string name = e->d_name;
                if (name == "." || name == "..") continue;
                if (name[0] == '.' && part[0] != '.') continue;
                if (::fnmatch(part.c_str(), name.c_str(), 0) == 0) next.push_back(joinPath(base, name));
            }
            ::closedir(d);
            if (next.size() > 5000) break;
        }
        cur.swap(next);
        if (cur.size() > 5000) cur.resize(5000);
    }
    std::vector<std::string> res;
    for (const auto& p : cur) if (!p.empty() && exists(p)) res.push_back(p);
    std::sort(res.begin(), res.end());
    return res;
}

inline std::vector<std::string> expandTokens(const std::vector<Tok>& toks) {
    std::vector<std::string> w;
    for (const auto& t : toks) {
        if (t.glob) {
            auto m = globExpand(t.s);
            if (!m.empty()) { w.insert(w.end(), m.begin(), m.end()); continue; }
        }
        w.push_back(t.s);
    }
    return w;
}

// ----------------------------------------------------------------------------
// command context + option parsing
// ----------------------------------------------------------------------------
struct Ctx {
    const char* name = "";
    Io* io = nullptr;
    std::vector<Tok> toks;
    std::vector<std::string> w;                // expanded words
    std::vector<std::string> pos;              // positional words (after parse)
    std::map<char, std::string> val;
    std::set<char> fl;
    std::string o;
    int rc = 0;

    void say(const std::string& s) { o += s; o += '\n'; }
    void flush() { if (!o.empty() && io) { io->out(o); } o.clear(); }
    void warn(const std::string& m) { flush(); if (io) io->err(std::string(name) + ": " + m); rc = 1; }
    int fail(const std::string& m) { warn(m); return 1; }
    bool has(char f) const { return fl.count(f) > 0; }
    bool hasv(char f) const { return val.count(f) > 0; }
    std::string v(char f, const std::string& d = "") const { auto it = val.find(f); return it == val.end() ? d : it->second; }

    // bools: letters that are plain flags;  vals: letters that take a value (-n 5, -n5)
    bool parse(const char* bools, const char* vals) {
        pos.clear(); val.clear(); fl.clear();
        bool endOpts = false;
        for (size_t i = 0; i < w.size(); ++i) {
            const std::string& a = w[i];
            if (endOpts || a.size() < 2 || a[0] != '-') { pos.push_back(a); continue; }
            if (a == "--") { endOpts = true; continue; }
            bool numeric = a.find_first_not_of("0123456789", 1) == std::string::npos;
            if (numeric && std::strchr(vals, 'n')) { val['n'] = a.substr(1); continue; }
            if (std::isdigit(static_cast<unsigned char>(a[1])) && !std::strchr(bools, a[1]) && !std::strchr(vals, a[1])) { pos.push_back(a); continue; }  // negative number / range
            for (size_t k = 1; k < a.size(); ++k) {
                char f = a[k];
                if (std::strchr(bools, f)) fl.insert(f);
                else if (std::strchr(vals, f)) {
                    std::string x = a.substr(k + 1);
                    if (x.empty()) {
                        if (i + 1 >= w.size()) { fail(std::string("option requires an argument -- '") + f + "'"); return false; }
                        x = w[++i];
                    }
                    val[f] = x;
                    break;
                } else { fail(std::string("invalid option -- '") + f + "'"); return false; }
            }
        }
        return true;
    }
};

struct In { std::string name, text; };
// text input: -e TEXT, or the file operands starting at pos[first]
inline bool gatherInputs(Ctx& c, std::vector<In>& v, size_t first = 0) {
    if (c.hasv('e')) { v.push_back({"-e", decodeEsc(c.v('e'))}); return true; }
    if (c.pos.size() <= first) { c.fail("missing file operand (or use -e TEXT)"); return false; }
    for (size_t i = first; i < c.pos.size(); ++i) {
        std::string t, why;
        if (!readFileCapped(c.pos[i], t, why)) { c.warn(c.pos[i] + ": " + why); continue; }
        v.push_back({c.pos[i], std::move(t)});
    }
    return true;
}

// ----------------------------------------------------------------------------
// hashing / encoding
// ----------------------------------------------------------------------------
inline std::string sha256hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string m = data;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xff);
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 3]));
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}
inline uint32_t crc32of(const std::string& s) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) c = table[(c ^ ch) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
inline std::string b64enc(const std::string& s) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < s.size(); i += 3) {
        uint32_t v = (static_cast<unsigned char>(s[i]) << 16) | (static_cast<unsigned char>(s[i + 1]) << 8) | static_cast<unsigned char>(s[i + 2]);
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == s.size()) {
        uint32_t v = static_cast<unsigned char>(s[i]) << 16;
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == s.size()) {
        uint32_t v = (static_cast<unsigned char>(s[i]) << 16) | (static_cast<unsigned char>(s[i + 1]) << 8);
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '=';
    }
    return o;
}
inline bool b64dec(const std::string& s, std::string& out) {
    out.clear();
    uint32_t v = 0; int bits = 0;
    for (char ch : s) {
        int d;
        if (ch >= 'A' && ch <= 'Z') d = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') d = ch - '0' + 52;
        else if (ch == '+' || ch == '-') d = 62;
        else if (ch == '/' || ch == '_') d = 63;
        else if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') continue;
        else return false;
        v = (v << 6) | static_cast<uint32_t>(d); bits += 6;
        if (bits >= 8) { bits -= 8; out += static_cast<char>((v >> bits) & 0xff); }
    }
    return true;
}

// ----------------------------------------------------------------------------
// filesystem helpers
// ----------------------------------------------------------------------------
inline bool mkdirsP(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' && cur.size() > 1 && !isDir(cur)) ::mkdir(cur.c_str(), 0755);
    }
    if (isDir(path)) return true;
    return ::mkdir(path.c_str(), 0755) == 0;
}
inline bool copyFileRaw(const std::string& a, const std::string& b, std::string& why) {
    int in = ::open(a.c_str(), O_RDONLY);
    if (in < 0) { why = std::strerror(errno); return false; }
    struct stat st;
    mode_t mode = 0644;
    if (::fstat(in, &st) == 0) mode = st.st_mode & 07777;
    int out = ::open(b.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { why = std::strerror(errno); ::close(in); return false; }
    char buf[65536];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = ::write(out, buf + off, static_cast<size_t>(n - off));
            if (w < 0) { why = std::strerror(errno); ok = false; break; }
            off += w;
        }
        if (!ok) break;
    }
    ::close(in); ::close(out);
    if (ok) ::chmod(b.c_str(), mode);
    return ok;
}
inline std::string realPathOf(const std::string& p) {
    char b[4096];
    return ::realpath(p.c_str(), b) ? std::string(b) : std::string();
}
inline bool copyTree(const std::string& a, const std::string& b, std::string& why, int depth = 0) {
    if (depth > 64) { why = "directory tree too deep"; return false; }
    struct stat st;
    if (::lstat(a.c_str(), &st) != 0) { why = std::strerror(errno); return false; }
    if (S_ISDIR(st.st_mode)) {
        if (!isDir(b) && ::mkdir(b.c_str(), st.st_mode & 0777) != 0) { why = std::strerror(errno); return false; }
        DIR* d = ::opendir(a.c_str());
        if (!d) { why = std::strerror(errno); return false; }
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n != "." && n != "..") names.push_back(n); }
        ::closedir(d);
        for (const auto& n : names) if (!copyTree(joinPath(a, n), joinPath(b, n), why, depth + 1)) return false;
        return true;
    }
    if (S_ISLNK(st.st_mode)) {
        char t[4096];
        ssize_t n = ::readlink(a.c_str(), t, sizeof(t) - 1);
        if (n < 0) { why = std::strerror(errno); return false; }
        t[n] = 0;
        ::unlink(b.c_str());
        if (::symlink(t, b.c_str()) != 0) { why = std::strerror(errno); return false; }
        return true;
    }
    return copyFileRaw(a, b, why);
}
inline bool removeTree(const std::string& p, std::string& why, int depth = 0) {
    if (depth > 64) { why = "directory tree too deep"; return false; }
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) { why = std::strerror(errno); return false; }
    if (S_ISDIR(st.st_mode)) {
        DIR* d = ::opendir(p.c_str());
        if (!d) { why = std::strerror(errno); return false; }
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n != "." && n != "..") names.push_back(n); }
        ::closedir(d);
        for (const auto& n : names) if (!removeTree(joinPath(p, n), why, depth + 1)) return false;
        if (::rmdir(p.c_str()) != 0) { why = std::strerror(errno); return false; }
        return true;
    }
    if (::unlink(p.c_str()) != 0) { why = std::strerror(errno); return false; }
    return true;
}
inline unsigned long long duWalk(const std::string& p, bool printDirs, bool hum, std::string& o, int depth = 0) {
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) return 0;
    unsigned long long total = st.st_blocks > 0 ? static_cast<unsigned long long>(st.st_blocks) * 512ULL : static_cast<unsigned long long>(st.st_size);
    if (S_ISDIR(st.st_mode) && depth < 64) {
        if (DIR* d = ::opendir(p.c_str())) {
            std::vector<std::string> names;
            while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n != "." && n != "..") names.push_back(n); }
            ::closedir(d);
            for (const auto& n : names) total += duWalk(joinPath(p, n), printDirs, hum, o, depth + 1);
        }
        if (printDirs) o += (hum ? human(total) : std::to_string((total + 1023) / 1024)) + "\t" + p + "\n";
    }
    return total;
}
inline std::string modeString(mode_t m) {
    std::string s(10, '-');
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    const char* r = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) if (m & (1u << (8 - i))) s[1 + i] = r[i];
    return s;
}
inline std::string timeString(time_t t, bool utc = false) {
    struct tm tmv;
    if (utc) ::gmtime_r(&t, &tmv); else ::localtime_r(&t, &tmv);
    char b[64];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tmv);
    return b;
}

// ----------------------------------------------------------------------------
// state: aliases + directory stack
// ----------------------------------------------------------------------------
inline std::map<std::string, std::string>& aliases() { static std::map<std::string, std::string> a; return a; }
inline std::vector<std::string>& dirStack() { static std::vector<std::string> s; return s; }

inline std::string expandAlias(const std::string& line) {
    std::string cur = line;
    std::set<std::string> seen;
    for (int depth = 0; depth < 5; ++depth) {
        size_t a = cur.find_first_not_of(" \t");
        if (a == std::string::npos) return cur;
        size_t b = cur.find_first_of(" \t", a);
        std::string first = cur.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (first == "alias" || first == "unalias" || seen.count(first)) return cur;
        auto it = aliases().find(first);
        if (it == aliases().end()) return cur;
        seen.insert(first);
        cur = it->second + (b == std::string::npos ? std::string() : cur.substr(b));
    }
    return cur;
}

// ============================================================================
//  THE 50 COMMANDS
// ============================================================================
using Fn = std::function<int(Ctx&)>;   // std::function: batch 4+ registers table-driven (capturing) commands

// ---- 1-4: reading -----------------------------------------------------------
inline int cmdCat(Ctx& c) {
    if (!c.parse("n", "r")) return 2;
    long ra = 1, rb = 0x7fffffffL;
    if (c.hasv('r') && !parseRange(c.v('r'), ra, rb)) return c.fail("bad range '" + c.v('r') + "' (use A:B)");
    if (c.pos.empty()) return c.fail("missing file operand");
    size_t n = 0;
    for (const auto& p : c.pos) {
        std::string t, why;
        if (!readFileCapped(p, t, why)) { c.warn(p + ": " + why); continue; }
        if (!c.has('n') && !c.hasv('r')) { c.o += t; if (!t.empty() && t.back() != '\n') c.o += '\n'; continue; }
        long ln = 0;
        for (const auto& l : textLines(t)) {
            ++ln;
            if (ln < ra) continue;
            if (ln > rb) break;
            char b[32];
            if (c.has('n')) { std::snprintf(b, sizeof(b), "%6ld\t", ln); c.o += b; }
            c.say(l);
            ++n;
        }
    }
    return c.rc;
}
inline int headTail(Ctx& c, bool head) {
    if (!c.parse("", "n")) return 2;
    long n = 10;
    if (c.hasv('n')) { char* e = nullptr; n = std::strtol(c.v('n').c_str(), &e, 10); if (!e || *e || n < 0) return c.fail("invalid line count '" + c.v('n') + "'"); }
    if (c.pos.empty()) return c.fail("missing file operand");
    for (size_t i = 0; i < c.pos.size(); ++i) {
        std::string t, why;
        if (!readFileCapped(c.pos[i], t, why)) { c.warn(c.pos[i] + ": " + why); continue; }
        if (c.pos.size() > 1) c.o += std::string(i ? "\n" : "") + "==> " + c.pos[i] + " <==\n";
        auto L = textLines(t);
        size_t cnt = std::min(L.size(), static_cast<size_t>(n));
        if (head) for (size_t k = 0; k < cnt; ++k) c.say(L[k]);
        else for (size_t k = L.size() - cnt; k < L.size(); ++k) c.say(L[k]);
    }
    return c.rc;
}
inline int cmdHead(Ctx& c) { return headTail(c, true); }
inline int cmdTail(Ctx& c) { return headTail(c, false); }
inline int cmdWc(Ctx& c) {
    if (!c.parse("lwcm", "e")) return 2;
    bool L = c.has('l'), W = c.has('w'), B = c.has('c'), M = c.has('m');
    if (!L && !W && !B && !M) L = W = B = true;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    unsigned long long tl = 0, tw = 0, tb = 0, tm = 0;
    auto row = [&](unsigned long long l, unsigned long long w, unsigned long long b, unsigned long long m, const std::string& name) {
        char buf[160];
        std::string s;
        if (L) { std::snprintf(buf, sizeof(buf), "%8llu", l); s += buf; }
        if (W) { std::snprintf(buf, sizeof(buf), "%8llu", w); s += buf; }
        if (M) { std::snprintf(buf, sizeof(buf), "%8llu", m); s += buf; }
        if (B) { std::snprintf(buf, sizeof(buf), "%8llu", b); s += buf; }
        c.say(s + " " + name);
    };
    for (const auto& f : in) {
        unsigned long long l = 0, w = 0, m = 0;
        bool inw = false;
        for (unsigned char ch : f.text) {
            if (ch == '\n') ++l;
            if (std::isspace(ch)) inw = false; else if (!inw) { inw = true; ++w; }
            if ((ch & 0xC0) != 0x80) ++m;
        }
        row(l, w, f.text.size(), m, f.name);
        tl += l; tw += w; tb += f.text.size(); tm += m;
    }
    if (in.size() > 1) row(tl, tw, tb, tm, "total");
    return c.rc;
}

// ---- 5-6: search --------------------------------------------------------------
inline void grepFile(Ctx& c, const std::string& path, const std::string& text, const std::string& pat, const std::regex* re,
                     bool multi, size_t& matches) {
    bool I = c.has('i'), N = c.has('n'), V = c.has('v'), C = c.has('c'), Lf = c.has('l');
    std::string needle = pat;
    if (I) std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
    size_t cnt = 0, ln = 0;
    for (const auto& l : textLines(text)) {
        ++ln;
        bool hit;
        if (re) hit = std::regex_search(l, *re);
        else {
            std::string hay = l;
            if (I) std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
            hit = hay.find(needle) != std::string::npos;
        }
        if (hit == V) continue;
        ++cnt;
        if (C || Lf) { if (Lf) break; continue; }
        std::string pre;
        if (multi) pre += path + ":";
        if (N) pre += std::to_string(ln) + ":";
        c.say(pre + l);
    }
    matches += cnt;
    if (Lf && cnt) c.say(path);
    else if (C) c.say((multi ? path + ":" : std::string()) + std::to_string(cnt));
}
inline void grepWalk(Ctx& c, const std::string& p, const std::string& pat, const std::regex* re, size_t& matches, int depth) {
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) { c.warn(p + ": " + std::strerror(errno)); return; }
    if (S_ISDIR(st.st_mode)) {
        if (depth > 32) return;
        DIR* d = ::opendir(p.c_str());
        if (!d) { c.warn(p + ": " + std::strerror(errno)); return; }
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n != "." && n != ".." && n[0] != '.') names.push_back(n); }
        ::closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) grepWalk(c, joinPath(p, n), pat, re, matches, depth + 1);
        return;
    }
    if (!S_ISREG(st.st_mode)) return;
    std::string t, why;
    if (!readFileCapped(p, t, why, 16u << 20)) { c.warn(p + ": " + why); return; }
    if (t.find('\0') != std::string::npos && t.find('\0') < 4096) return;   // binary
    grepFile(c, p, t, pat, re, true, matches);
}
inline int cmdGrep(Ctx& c) {
    if (!c.parse("incvlrE", "")) return 2;
    if (c.pos.empty()) return c.fail("missing pattern");
    std::string pat = c.pos[0];
    std::unique_ptr<std::regex> re;
    if (c.has('E')) {
        try { re.reset(new std::regex(pat, std::regex::extended | (c.has('i') ? std::regex::icase : std::regex::flag_type(0)))); }
        catch (const std::regex_error&) { return c.fail("invalid regular expression"); }
    }
    std::vector<std::string> files(c.pos.begin() + 1, c.pos.end());
    if (files.empty()) { if (c.has('r')) files.push_back("."); else return c.fail("missing file operand"); }
    size_t matches = 0;
    bool multi = files.size() > 1 || c.has('r');
    for (const auto& f : files) {
        if (c.has('r') && isDir(f)) { grepWalk(c, f, pat, re.get(), matches, 0); continue; }
        std::string t, why;
        if (!readFileCapped(f, t, why)) { c.warn(f + ": " + why); continue; }
        grepFile(c, f, t, pat, re.get(), multi, matches);
    }
    if (c.rc) return c.rc;
    return matches ? 0 : 1;
}
inline void findWalk(Ctx& c, const std::string& p, const std::string& glob, bool icase, char type, long maxd, long depth, size_t& n) {
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) return;
    bool dir = S_ISDIR(st.st_mode);
    bool matchType = type == 0 || (type == 'd' && dir) || (type == 'f' && S_ISREG(st.st_mode)) || (type == 'l' && S_ISLNK(st.st_mode));
    auto low = [](std::string x) { std::transform(x.begin(), x.end(), x.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }); return x; };
    bool matchName = glob.empty() || (icase ? ::fnmatch(low(glob).c_str(), low(baseName(p)).c_str(), 0)
                                            : ::fnmatch(glob.c_str(), baseName(p).c_str(), 0)) == 0;
    if (matchType && matchName) { c.say(p); ++n; }
    if (n >= 20000) return;
    if (dir && depth < maxd) {
        DIR* d = ::opendir(p.c_str());
        if (!d) return;
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) { std::string nm = e->d_name; if (nm != "." && nm != "..") names.push_back(nm); }
        ::closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto& nm : names) findWalk(c, joinPath(p, nm), glob, icase, type, maxd, depth + 1, n);
    }
}
inline int cmdFind(Ctx& c) {
    // uses raw tokens so `-name *.rin` is not expanded by the shell-style glob
    std::string dir, glob;
    bool icase = false;
    char type = 0;
    long maxd = 64;
    const auto& t = c.toks;
    for (size_t i = 0; i < t.size(); ++i) {
        const std::string& a = t[i].s;
        auto need = [&](std::string& dst) { if (i + 1 >= t.size()) return false; dst = t[++i].s; return true; };
        if (a == "-name" || a == "-iname") { icase = a == "-iname"; if (!need(glob)) return c.fail("missing argument to '" + a + "'"); }
        else if (a == "-type") { std::string x; if (!need(x) || x.size() != 1 || !std::strchr("fdl", x[0])) return c.fail("-type needs f, d or l"); type = x[0]; }
        else if (a == "-maxdepth") { std::string x; if (!need(x)) return c.fail("missing argument to '-maxdepth'"); char* e = nullptr; maxd = std::strtol(x.c_str(), &e, 10); if (!e || *e || maxd < 0) return c.fail("invalid -maxdepth"); }
        else if (!a.empty() && a[0] == '-' && a.size() > 1) return c.fail("unknown option '" + a + "'");
        else if (dir.empty()) dir = a;
        else return c.fail("only one start directory is supported");
    }
    if (dir.empty()) dir = ".";
    if (!exists(dir)) return c.fail(dir + ": No such file or directory");
    size_t n = 0;
    findWalk(c, dir, glob, icase, type, maxd, 0, n);
    if (n >= 20000) c.say("… (stopped after 20000 results)");
    return 0;
}

// ---- 7-15: files and directories -----------------------------------------------
inline int cmdMkdir(Ctx& c) {
    if (!c.parse("p", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& p : c.pos) {
        if (c.has('p')) { if (!mkdirsP(p)) c.warn("cannot create '" + p + "': " + std::strerror(errno)); }
        else if (::mkdir(p.c_str(), 0755) != 0) c.warn("cannot create '" + p + "': " + std::strerror(errno));
    }
    return c.rc;
}
inline int cmdRmdir(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& p : c.pos) if (::rmdir(p.c_str()) != 0) c.warn("failed to remove '" + p + "': " + std::strerror(errno));
    return c.rc;
}
inline int cmdRm(Ctx& c) {
    if (!c.parse("rfR", "")) return 2;
    if (c.pos.empty()) return c.has('f') ? 0 : c.fail("missing operand");
    std::string home = realPathOf(std::getenv("HOME") ? std::getenv("HOME") : "/");
    for (const auto& p : c.pos) {
        std::string b = baseName(p);
        std::string rp = realPathOf(p);
        if (b == "." || b == ".." || p == "/" || (!rp.empty() && (rp == "/" || rp == home)))
            { c.warn("refusing to remove '" + p + "'"); continue; }
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0) { if (!c.has('f')) c.warn("cannot remove '" + p + "': " + std::strerror(errno)); continue; }
        if (S_ISDIR(st.st_mode) && !c.has('r') && !c.has('R')) { c.warn("cannot remove '" + p + "': Is a directory (use -r)"); continue; }
        std::string why;
        if (!removeTree(p, why) && !c.has('f')) c.warn("cannot remove '" + p + "': " + why);
    }
    return c.rc;
}
inline int cmdCp(Ctx& c) {
    if (!c.parse("rRf", "")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ cp [-r] <src…> <dest>");
    std::string dest = c.pos.back();
    std::vector<std::string> src(c.pos.begin(), c.pos.end() - 1);
    bool destDir = isDir(dest);
    if (src.size() > 1 && !destDir) return c.fail("target '" + dest + "' is not a directory");
    for (const auto& s : src) {
        std::string target = destDir ? joinPath(dest, baseName(s)) : dest;
        if (isDir(s)) {
            if (!c.has('r') && !c.has('R')) { c.warn("omitting directory '" + s + "' (use -r)"); continue; }
            std::string rs = realPathOf(s), rd = realPathOf(destDir ? dest : dirName(dest));
            if (!rs.empty() && !rd.empty() && (rd == rs || startsWith(rd, rs + "/"))) { c.warn("cannot copy '" + s + "' into itself"); continue; }
        }
        std::string why;
        if (!copyTree(s, target, why)) c.warn("cannot copy '" + s + "': " + why);
    }
    return c.rc;
}
inline int cmdMv(Ctx& c) {
    if (!c.parse("f", "")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ mv <src…> <dest>");
    std::string dest = c.pos.back();
    std::vector<std::string> src(c.pos.begin(), c.pos.end() - 1);
    bool destDir = isDir(dest);
    if (src.size() > 1 && !destDir) return c.fail("target '" + dest + "' is not a directory");
    for (const auto& s : src) {
        std::string target = destDir ? joinPath(dest, baseName(s)) : dest;
        if (::rename(s.c_str(), target.c_str()) == 0) continue;
        if (errno != EXDEV) { c.warn("cannot move '" + s + "': " + std::strerror(errno)); continue; }
        std::string why;
        if (!copyTree(s, target, why) || !removeTree(s, why)) c.warn("cannot move '" + s + "': " + why);
    }
    return c.rc;
}
inline int cmdTouch(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing file operand");
    for (const auto& p : c.pos) {
        if (exists(p)) { if (::utime(p.c_str(), nullptr) != 0) c.warn("cannot touch '" + p + "': " + std::strerror(errno)); }
        else { int fd = ::open(p.c_str(), O_WRONLY | O_CREAT, 0644); if (fd < 0) c.warn("cannot touch '" + p + "': " + std::strerror(errno)); else ::close(fd); }
    }
    return c.rc;
}
inline int cmdStat(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& p : c.pos) {
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0) { c.warn("cannot stat '" + p + "': " + std::strerror(errno)); continue; }
        const char* kind = S_ISDIR(st.st_mode) ? "directory" : S_ISLNK(st.st_mode) ? "symbolic link" : S_ISREG(st.st_mode) ? "regular file" : "special file";
        char mode[16];
        std::snprintf(mode, sizeof(mode), "%04o", static_cast<unsigned>(st.st_mode & 07777));
        c.say("  File: " + p);
        c.say("  Type: " + std::string(kind));
        c.say("  Size: " + std::to_string(static_cast<long long>(st.st_size)) + " (" + human(static_cast<unsigned long long>(st.st_size)) + ")");
        c.say("  Mode: " + std::string(mode) + " (" + modeString(st.st_mode) + ")");
        c.say(" Owner: " + std::to_string(st.st_uid) + ":" + std::to_string(st.st_gid));
        c.say(" Inode: " + std::to_string(static_cast<unsigned long long>(st.st_ino)) + "  Links: " + std::to_string(static_cast<unsigned long long>(st.st_nlink)));
        c.say("Modify: " + timeString(st.st_mtime));
    }
    return c.rc;
}
inline int cmdDu(Ctx& c) {
    if (!c.parse("hs", "")) return 2;
    std::vector<std::string> paths = c.pos;
    if (paths.empty()) paths.push_back(".");
    for (const auto& p : paths) {
        if (!exists(p)) { c.warn("cannot access '" + p + "': No such file or directory"); continue; }
        std::string lines;
        unsigned long long total = duWalk(p, !c.has('s'), c.has('h'), lines);
        if (c.has('s')) c.say((c.has('h') ? human(total) : std::to_string((total + 1023) / 1024)) + "\t" + p);
        else c.o += lines;
    }
    return c.rc;
}
inline int cmdRealpath(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& p : c.pos) {
        std::string r = realPathOf(p);
        if (r.empty()) c.warn(p + ": " + std::strerror(errno)); else c.say(r);
    }
    return c.rc;
}
inline int cmdBasename(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty() || c.pos.size() > 2) return c.fail("usage: $ basename <path> [suffix]");
    std::string b = baseName(c.pos[0]);
    if (c.pos.size() == 2) {
        const std::string& s = c.pos[1];
        if (b.size() > s.size() && b.compare(b.size() - s.size(), s.size(), s) == 0) b.resize(b.size() - s.size());
    }
    c.say(b);
    return 0;
}
inline int cmdDirname(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& p : c.pos) c.say(dirName(p));
    return 0;
}

// ---- 18-27: text ----------------------------------------------------------------
inline int cmdEcho(Ctx& c) {
    if (!c.parse("ne", "")) return 2;
    std::string s;
    for (size_t i = 0; i < c.pos.size(); ++i) { if (i) s += ' '; s += c.pos[i]; }
    if (c.has('e')) s = decodeEsc(s);
    c.o += s;
    if (!c.has('n')) c.o += '\n';
    return 0;
}
inline int cmdSort(Ctx& c) {
    if (!c.parse("rnuf", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    std::vector<std::string> L;
    for (const auto& f : in) { auto l = textLines(f.text); L.insert(L.end(), l.begin(), l.end()); }
    auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); }); return s; };
    bool num = c.has('n'), fold = c.has('f');
    auto less = [&](const std::string& a, const std::string& b) {
        if (num) { double x = std::strtod(a.c_str(), nullptr), y = std::strtod(b.c_str(), nullptr); if (x != y) return x < y; }
        return fold ? lower(a) < lower(b) : a < b;
    };
    std::stable_sort(L.begin(), L.end(), less);
    if (c.has('r')) std::reverse(L.begin(), L.end());
    for (size_t i = 0; i < L.size(); ++i) {
        if (c.has('u') && i && !less(L[i - 1], L[i]) && !less(L[i], L[i - 1])) continue;
        c.say(L[i]);
    }
    return c.rc;
}
inline int cmdUniq(Ctx& c) {
    if (!c.parse("cdu", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) {
        auto L = textLines(f.text);
        for (size_t i = 0; i < L.size();) {
            size_t j = i;
            while (j < L.size() && L[j] == L[i]) ++j;
            size_t cnt = j - i;
            bool show = c.has('d') ? cnt > 1 : c.has('u') ? cnt == 1 : true;
            if (show) {
                if (c.has('c')) { char b[24]; std::snprintf(b, sizeof(b), "%7zu ", cnt); c.o += b; }
                c.say(L[i]);
            }
            i = j;
        }
    }
    return c.rc;
}
inline int cmdTac(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) { auto L = textLines(f.text); for (size_t i = L.size(); i-- > 0;) c.say(L[i]); }
    return c.rc;
}
inline int cmdRev(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in)
        for (const auto& l : textLines(f.text)) { auto ch = utf8Chars(l); std::string r; for (size_t i = ch.size(); i-- > 0;) r += ch[i]; c.say(r); }
    return c.rc;
}
inline int cmdNl(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    size_t n = 0;
    for (const auto& f : in)
        for (const auto& l : textLines(f.text)) { char b[24]; std::snprintf(b, sizeof(b), "%6zu\t", ++n); c.o += b; c.say(l); }
    return c.rc;
}
inline int caseConv(Ctx& c, bool up) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (auto& f : in) {
        for (auto& ch : f.text) ch = static_cast<char>(up ? std::toupper(static_cast<unsigned char>(ch)) : std::tolower(static_cast<unsigned char>(ch)));
        c.o += f.text;
        if (!f.text.empty() && f.text.back() != '\n') c.o += '\n';
    }
    return c.rc;
}
inline int cmdUpper(Ctx& c) { return caseConv(c, true); }
inline int cmdLower(Ctx& c) { return caseConv(c, false); }
inline int cmdCut(Ctx& c) {
    if (!c.parse("", "dfce")) return 2;
    bool byField = c.hasv('f');
    if (byField == c.hasv('c')) return c.fail("specify exactly one of -f LIST or -c LIST");
    std::string spec = byField ? c.v('f') : c.v('c');
    std::vector<std::pair<long, long>> ranges;
    {
        std::stringstream ss(spec);
        std::string part;
        while (std::getline(ss, part, ',')) {
            long a, b;
            if (!parseRange(part, a, b)) return c.fail("bad list '" + spec + "' (use 1,3 or 2:4)");
            ranges.push_back({a, b});
        }
    }
    std::string delim = c.hasv('d') ? decodeEsc(c.v('d')) : "\t";
    if (delim.empty()) return c.fail("empty delimiter");
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in)
        for (const auto& line : textLines(f.text)) {
            std::vector<std::string> items;
            if (byField) {
                size_t i = 0;
                while (true) { size_t j = line.find(delim, i); if (j == std::string::npos) { items.push_back(line.substr(i)); break; } items.push_back(line.substr(i, j - i)); i = j + delim.size(); }
            } else items = utf8Chars(line);
            std::string out;
            bool first = true;
            for (auto [a, b] : ranges)
                for (long k = a; k <= b && k <= static_cast<long>(items.size()); ++k) {
                    if (byField && !first) out += delim;
                    out += items[static_cast<size_t>(k - 1)];
                    first = false;
                }
            c.say(out);
        }
    return c.rc;
}
inline int cmdCmp(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ cmp <file1> <file2>");
    std::string a, b, why;
    if (!readFileCapped(c.pos[0], a, why)) return c.fail(c.pos[0] + ": " + why);
    if (!readFileCapped(c.pos[1], b, why)) return c.fail(c.pos[1] + ": " + why);
    size_t n = std::min(a.size(), b.size()), line = 1;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) { c.say(c.pos[0] + " " + c.pos[1] + " differ: byte " + std::to_string(i + 1) + ", line " + std::to_string(line)); return 1; }
        if (a[i] == '\n') ++line;
    }
    if (a.size() != b.size()) { c.say("EOF on " + (a.size() < b.size() ? c.pos[0] : c.pos[1]) + " after byte " + std::to_string(n)); return 1; }
    c.say("files are identical");
    return 0;
}

// ---- 28-31: hashes and encodings ---------------------------------------------------
inline int cmdSha256(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) c.say(sha256hex(f.text) + "  " + f.name);
    return c.rc;
}
inline int cmdBase64(Ctx& c) {
    if (!c.parse("d", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) {
        if (c.has('d')) {
            std::string out;
            if (!b64dec(f.text, out)) { c.warn(f.name + ": invalid base64 input"); continue; }
            c.o += out;
            if (!out.empty() && out.back() != '\n') c.o += '\n';
        } else {
            c.say(b64enc(f.text));
        }
    }
    return c.rc;
}
inline int cmdHexdump(Ctx& c) {
    if (!c.parse("C", "n")) return 2;
    if (c.pos.size() != 1) return c.fail("usage: $ hexdump [-n BYTES] <file>");
    long limit = 256;
    if (c.hasv('n')) { char* e = nullptr; limit = std::strtol(c.v('n').c_str(), &e, 10); if (!e || *e || limit < 1) return c.fail("invalid byte count"); }
    std::string t, why;
    if (!readFileCapped(c.pos[0], t, why)) return c.fail(c.pos[0] + ": " + why);
    size_t n = std::min(t.size(), static_cast<size_t>(limit));
    for (size_t off = 0; off < n; off += 16) {
        char b[32];
        std::snprintf(b, sizeof(b), "%08zx  ", off);
        std::string line = b, asc;
        for (size_t i = 0; i < 16; ++i) {
            if (off + i < n) {
                unsigned char ch = static_cast<unsigned char>(t[off + i]);
                std::snprintf(b, sizeof(b), "%02x ", ch);
                line += b;
                asc += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '.';
            } else line += "   ";
            if (i == 7) line += ' ';
        }
        c.say(line + " |" + asc + "|");
    }
    if (n < t.size()) c.say("… (" + std::to_string(t.size() - n) + " more bytes; use -n)");
    return 0;
}
inline int cmdCrc32(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) { char b[16]; std::snprintf(b, sizeof(b), "%08x", crc32of(f.text)); c.say(std::string(b) + "  " + f.name); }
    return c.rc;
}

// ---- 32-43: system --------------------------------------------------------------
inline int cmdEnv(Ctx& c) {
    if (!c.parse("", "")) return 2;
    std::vector<std::string> v;
    for (char** e = environ; e && *e; ++e) v.push_back(*e);
    std::sort(v.begin(), v.end());
    for (const auto& kv : v) {
        std::string name = kv.substr(0, kv.find('='));
        bool ok = c.toks.empty();
        for (const auto& t : c.toks) if (::fnmatch(t.s.c_str(), name.c_str(), 0) == 0) { ok = true; break; }
        if (ok) c.say(kv);
    }
    return 0;
}
inline int cmdExport(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return cmdEnv(c);
    for (const auto& a : c.pos) {
        size_t eq = a.find('=');
        std::string name = eq == std::string::npos ? a : a.substr(0, eq);
        bool valid = !name.empty() && !std::isdigit(static_cast<unsigned char>(name[0]));
        for (char ch : name) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') valid = false;
        if (!valid) { c.warn("not a valid identifier: '" + a + "'"); continue; }
        if (eq == std::string::npos) continue;
        ::setenv(name.c_str(), a.substr(eq + 1).c_str(), 1);
    }
    return c.rc;
}
inline int cmdUnset(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& n : c.pos) ::unsetenv(n.c_str());
    return 0;
}
inline int cmdDate(Ctx& c) {
    if (!c.parse("u", "")) return 2;
    std::string fmt = "%Y-%m-%d %H:%M:%S %Z";
    for (const auto& p : c.pos) { if (p[0] == '+') fmt = p.substr(1); else return c.fail("invalid argument '" + p + "' (use +FORMAT)"); }
    time_t t = std::time(nullptr);
    struct tm tmv;
    if (c.has('u')) ::gmtime_r(&t, &tmv); else ::localtime_r(&t, &tmv);
    char b[512];
    size_t n = std::strftime(b, sizeof(b), fmt.c_str(), &tmv);
    if (n == 0 && !fmt.empty()) return c.fail("format produced no output");
    c.say(std::string(b, n));
    return 0;
}
inline int cmdWhoami(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (struct passwd* pw = ::getpwuid(::geteuid())) { c.say(pw->pw_name); return 0; }
    if (const char* u = std::getenv("USER")) { c.say(u); return 0; }
    c.say("uid " + std::to_string(static_cast<unsigned>(::geteuid())));
    return 0;
}
inline int cmdHostname(Ctx& c) {
    if (!c.parse("", "")) return 2;
    char b[256];
    if (::gethostname(b, sizeof(b)) != 0 || !b[0]) { c.say("localhost"); return 0; }
    b[sizeof(b) - 1] = 0;
    c.say(b);
    return 0;
}
inline int cmdUname(Ctx& c) {
    if (!c.parse("asnrvm", "")) return 2;
    struct utsname u;
    if (::uname(&u) != 0) return c.fail(std::strerror(errno));
    std::string s;
    auto add = [&](const char* x) { if (!s.empty()) s += ' '; s += x; };
    if (c.has('a')) { add(u.sysname); add(u.nodename); add(u.release); add(u.version); add(u.machine); }
    else {
        bool any = false;
        if (c.has('s')) { add(u.sysname); any = true; }
        if (c.has('n')) { add(u.nodename); any = true; }
        if (c.has('r')) { add(u.release); any = true; }
        if (c.has('v')) { add(u.version); any = true; }
        if (c.has('m')) { add(u.machine); any = true; }
        if (!any) add(u.sysname);
    }
    c.say(s);
    return 0;
}
inline const std::vector<Spec>& specs();
inline int cmdWhich(Ctx& c) {
    if (!c.parse("a", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    const char* pe = std::getenv("PATH");
    std::string path = pe ? pe : "/usr/bin:/bin";
    for (const auto& name : c.pos) {
        bool found = false;
        for (const auto& s : specs()) if (name == s.name) { c.say(name + ": rin terminal command ($ " + name + ")"); found = true; break; }
        if (found && !c.has('a')) continue;
        std::stringstream ss(path);
        std::string dir;
        bool hit = false;
        while (std::getline(ss, dir, ':')) {
            std::string full = joinPath(dir.empty() ? "." : dir, name);
            struct stat st;
            if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(full.c_str(), X_OK) == 0) { c.say(full); hit = true; if (!c.has('a')) break; }
        }
        if (!hit && !found) { c.warn("no " + name + " in PATH"); }
    }
    return c.rc;
}
inline int cmdUptime(Ctx& c) {
    if (!c.parse("", "")) return 2;
    std::string t, why;
    if (!readFileCapped("/proc/uptime", t, why)) return c.fail("cannot read /proc/uptime: " + why);
    long secs = static_cast<long>(std::strtod(t.c_str(), nullptr));
    long d = secs / 86400, h = (secs % 86400) / 3600, m = (secs % 3600) / 60;
    std::string s = "up ";
    if (d) s += std::to_string(d) + (d == 1 ? " day, " : " days, ");
    char b[32];
    std::snprintf(b, sizeof(b), "%ld:%02ld", h, m);
    s += b;
    std::string la;
    if (readFileCapped("/proc/loadavg", la, why)) {
        std::istringstream is(la);
        std::string a1, a2, a3;
        if (is >> a1 >> a2 >> a3) s += "   load average: " + a1 + ", " + a2 + ", " + a3;
    }
    c.say(s);
    return 0;
}
inline int cmdFree(Ctx& c) {
    if (!c.parse("b", "")) return 2;
    std::string t, why;
    if (!readFileCapped("/proc/meminfo", t, why)) return c.fail("cannot read /proc/meminfo: " + why);
    std::map<std::string, unsigned long long> m;
    for (const auto& l : textLines(t)) {
        size_t col = l.find(':');
        if (col == std::string::npos) continue;
        m[l.substr(0, col)] = std::strtoull(l.c_str() + col + 1, nullptr, 10) * 1024ULL;
    }
    auto f = [&](unsigned long long v) { return c.has('b') ? std::to_string(v) : human(v); };
    unsigned long long total = m["MemTotal"], freeM = m["MemFree"], avail = m.count("MemAvailable") ? m["MemAvailable"] : freeM;
    unsigned long long used = total > avail ? total - avail : 0;
    unsigned long long st = m["SwapTotal"], sf = m["SwapFree"];
    char b[200];
    std::snprintf(b, sizeof(b), "%-6s %12s %12s %12s %12s", "", "total", "used", "free", "available");
    c.say(b);
    std::snprintf(b, sizeof(b), "%-6s %12s %12s %12s %12s", "Mem:", f(total).c_str(), f(used).c_str(), f(freeM).c_str(), f(avail).c_str());
    c.say(b);
    std::snprintf(b, sizeof(b), "%-6s %12s %12s %12s", "Swap:", f(st).c_str(), f(st > sf ? st - sf : 0).c_str(), f(sf).c_str());
    c.say(b);
    return 0;
}
inline int cmdNproc(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long n = ::sysconf(_SC_NPROCESSORS_ONLN);
    c.say(std::to_string(n > 0 ? n : 1));
    return 0;
}
inline int cmdDf(Ctx& c) {
    if (!c.parse("b", "")) return 2;
    std::vector<std::string> paths = c.pos;
    if (paths.empty()) paths.push_back(".");
    char b[256];
    std::snprintf(b, sizeof(b), "%12s %12s %12s %5s  %s", "Size", "Used", "Avail", "Use%", "Path");
    c.say(b);
    for (const auto& p : paths) {
        struct statvfs s;
        if (::statvfs(p.c_str(), &s) != 0) { c.warn(p + ": " + std::strerror(errno)); continue; }
        unsigned long long total = static_cast<unsigned long long>(s.f_blocks) * s.f_frsize;
        unsigned long long fr = static_cast<unsigned long long>(s.f_bfree) * s.f_frsize;
        unsigned long long av = static_cast<unsigned long long>(s.f_bavail) * s.f_frsize;
        unsigned long long used = total > fr ? total - fr : 0;
        unsigned long long den = used + av;
        auto f = [&](unsigned long long v) { return c.has('b') ? std::to_string(v) : human(v); };
        std::snprintf(b, sizeof(b), "%12s %12s %12s %4llu%%  %s", f(total).c_str(), f(used).c_str(), f(av).c_str(), den ? used * 100 / den : 0ULL, p.c_str());
        c.say(b);
    }
    return c.rc;
}

// ---- 44-45: utilities --------------------------------------------------------------
inline int cmdSeq(Ctx& c) {
    if (!c.parse("", "s")) return 2;
    long long first = 1, step = 1, last = 0;
    auto num = [](const std::string& t, long long& out) { char* e = nullptr; out = std::strtoll(t.c_str(), &e, 10); return !t.empty() && e && !*e; };
    std::vector<std::string> p = c.pos;
    if (p.size() == 1) {
        // A:B  A:B:S  A…B  A..B  or a single LAST
        std::string s = p[0];
        std::vector<std::string> parts;
        std::string cur;
        for (size_t i = 0; i < s.size();) {
            if (s.compare(i, 3, "\xE2\x80\xA6") == 0) { parts.push_back(cur); cur.clear(); i += 3; }
            else if (s.compare(i, 2, "..") == 0) { parts.push_back(cur); cur.clear(); i += 2; }
            else if (s[i] == ':') { parts.push_back(cur); cur.clear(); ++i; }
            else cur += s[i++];
        }
        parts.push_back(cur);
        p = parts;
    }
    bool ok = false;
    if (p.size() == 1) ok = num(p[0], last);
    else if (p.size() == 2) ok = num(p[0], first) && num(p[1], last);
    else if (p.size() == 3) {
        // GNU style (first step last) for words, A:B:STEP for ranges
        bool range = c.pos.size() == 1;
        ok = range ? (num(p[0], first) && num(p[1], last) && num(p[2], step)) : (num(p[0], first) && num(p[1], step) && num(p[2], last));
    }
    if (!ok) return c.fail("usage: $ seq A:B[:STEP]   or   $ seq [first [step]] last");
    if (step == 0) return c.fail("step must not be zero");
    if (step < 0 && first < last) step = -step;
    if (step > 0 && first > last) step = -step;
    long long count = (last - first) / step + 1;
    if (count > 100000) return c.fail("too many items (limit 100000)");
    std::string sep = c.hasv('s') ? decodeEsc(c.v('s')) : "\n";
    long long k = 0;
    for (long long v = first; step > 0 ? v <= last : v >= last; v += step, ++k) { if (k) c.o += sep; c.o += std::to_string(v); }
    c.o += "\n";
    return 0;
}
inline int cmdSleep(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 1) return c.fail("usage: $ sleep <seconds>");
    char* e = nullptr;
    double s = std::strtod(c.pos[0].c_str(), &e);
    if (!e || *e || s < 0) return c.fail("invalid time interval '" + c.pos[0] + "'");
    if (s > 300) return c.fail("limit is 300 seconds");
    c.flush();
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(s);
    ts.tv_nsec = static_cast<long>((s - std::floor(s)) * 1e9);
    while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
    return 0;
}

// ---- 46-50: aliases and directory stack -----------------------------------------------
inline int cmdAlias(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) {
        if (aliases().empty()) c.say("(no aliases)  e.g.  $ alias ll='ls -a'");
        for (const auto& kv : aliases()) c.say("alias " + kv.first + "='" + kv.second + "'");
        return 0;
    }
    std::string a = c.pos[0];
    size_t eq = a.find('=');
    if (eq == std::string::npos) {
        auto it = aliases().find(a);
        if (it == aliases().end()) return c.fail(a + ": not found");
        c.say("alias " + a + "='" + it->second + "'");
        return 0;
    }
    std::string name = a.substr(0, eq), value = a.substr(eq + 1);
    for (size_t i = 1; i < c.pos.size(); ++i) value += " " + c.pos[i];
    bool valid = !name.empty();
    for (char ch : name) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') valid = false;
    if (!valid) return c.fail("invalid alias name '" + name + "'");
    if (name == "alias" || name == "unalias") return c.fail("cannot alias '" + name + "'");
    if (value.empty()) return c.fail("empty alias value (use $ unalias " + name + ")");
    aliases()[name] = value;
    return 0;
}
inline int cmdUnalias(Ctx& c) {
    if (!c.parse("a", "")) return 2;
    if (c.has('a')) { aliases().clear(); return 0; }
    if (c.pos.empty()) return c.fail("usage: $ unalias <name…> | -a");
    for (const auto& n : c.pos) if (!aliases().erase(n)) c.warn(n + ": not found");
    return c.rc;
}
inline void printDirs(Ctx& c) {
    std::string s = cwd();
    auto& st = dirStack();
    for (size_t i = st.size(); i-- > 0;) s += " " + st[i];
    c.say(s);
}
inline int cmdPushd(Ctx& c) {
    if (!c.parse("", "")) return 2;
    auto& st = dirStack();
    std::string here = cwd();
    if (c.pos.empty()) {
        if (st.empty()) return c.fail("no other directory");
        std::string top = st.back();
        if (::chdir(top.c_str()) != 0) return c.fail(top + ": " + std::strerror(errno));
        st.back() = here;
    } else {
        if (::chdir(c.pos[0].c_str()) != 0) return c.fail(c.pos[0] + ": " + std::strerror(errno));
        st.push_back(here);
    }
    printDirs(c);
    return 0;
}
inline int cmdPopd(Ctx& c) {
    if (!c.parse("", "")) return 2;
    auto& st = dirStack();
    if (st.empty()) return c.fail("directory stack empty");
    std::string top = st.back();
    if (::chdir(top.c_str()) != 0) return c.fail(top + ": " + std::strerror(errno));
    st.pop_back();
    printDirs(c);
    return 0;
}
inline int cmdDirs(Ctx& c) {
    if (!c.parse("c", "")) return 2;
    if (c.has('c')) { dirStack().clear(); return 0; }
    printDirs(c);
    return 0;
}

// ----------------------------------------------------------------------------
// registry
// ----------------------------------------------------------------------------
struct Entry { Spec spec; Fn fn; };
inline const std::vector<Entry>& registry2();   // batch 2: rin_terminal_xcmds2.h
inline const std::vector<Entry>& registry3();   // batch 3: rin_terminal_xcmds3.h
inline const std::vector<Entry>& registry4();   // batch 4: rin_terminal_xcmds4.h .. xcmds7.h
inline const std::vector<Entry>& registry() {
    static const std::vector<Entry> base = {
        {{"cat",      "$ cat [-n] [-r A:B] <file…>",                  "print files (-n number lines, -r line range A:B)", true}, cmdCat},
        {{"head",     "$ head [-n N] <file…>",                        "first N lines (default 10)", true}, cmdHead},
        {{"tail",     "$ tail [-n N] <file…>",                        "last N lines (default 10)", true}, cmdTail},
        {{"wc",       "$ wc [-l -w -c -m] <file…>",                   "count lines, words, bytes, chars", true}, cmdWc},
        {{"grep",     "$ grep [-i -n -c -v -l -r -E] <pattern> <file…>", "search text in files (-r recursive, -E regex)", true}, cmdGrep},
        {{"find",     "$ find [dir] [-name GLOB] [-type f|d] [-maxdepth N]", "find files and directories by name/type", true}, cmdFind},
        {{"mkdir",    "$ mkdir [-p] <dir…>",                          "create directories (-p: with parents)", true}, cmdMkdir},
        {{"rmdir",    "$ rmdir <dir…>",                               "remove empty directories", true}, cmdRmdir},
        {{"rm",       "$ rm [-r -f] <path…>",                         "remove files (-r: directories too)", true}, cmdRm},
        {{"cp",       "$ cp [-r] <src…> <dest>   e.g. $ cp *.rin ~/", "copy files and directories", true}, cmdCp},
        {{"mv",       "$ mv <src…> <dest>",                           "move or rename files", true}, cmdMv},
        {{"touch",    "$ touch <file…>",                              "create empty files or update their time", true}, cmdTouch},
        {{"stat",     "$ stat <path…>",                               "size, mode, owner and times of a path", true}, cmdStat},
        {{"du",       "$ du [-h -s] [path…]",                         "disk usage of directories", true}, cmdDu},
        {{"realpath", "$ realpath <path…>",                           "resolve to an absolute path", true}, cmdRealpath},
        {{"basename", "$ basename <path> [suffix]",                   "file name part of a path", true}, cmdBasename},
        {{"dirname",  "$ dirname <path…>",                            "directory part of a path", true}, cmdDirname},
        {{"echo",     "$ echo [-n -e] <text…>",                       "print text ($VAR and * are expanded)", false}, cmdEcho},
        {{"sort",     "$ sort [-r -n -u -f] <file…|-e TEXT>",         "sort lines", true}, cmdSort},
        {{"uniq",     "$ uniq [-c -d -u] <file…|-e TEXT>",            "collapse repeated adjacent lines", true}, cmdUniq},
        {{"tac",      "$ tac <file…|-e TEXT>",                        "print lines in reverse order", true}, cmdTac},
        {{"rev",      "$ rev <file…|-e TEXT>",                        "reverse the characters of each line", true}, cmdRev},
        {{"nl",       "$ nl <file…|-e TEXT>",                         "number lines", true}, cmdNl},
        {{"upper",    "$ upper <file…|-e TEXT>",                      "convert text to upper case", true}, cmdUpper},
        {{"lower",    "$ lower <file…|-e TEXT>",                      "convert text to lower case", true}, cmdLower},
        {{"cut",      "$ cut [-d DELIM] <-f A:B,N | -c A:B> <file…>", "select fields or characters (ranges A:B)", true}, cmdCut},
        {{"cmp",      "$ cmp <file1> <file2>",                        "compare two files byte by byte", true}, cmdCmp},
        {{"sha256",   "$ sha256 <file…|-e TEXT>",                     "SHA-256 checksum", true}, cmdSha256},
        {{"base64",   "$ base64 [-d] <file…|-e TEXT>",                "base64 encode / decode", true}, cmdBase64},
        {{"hexdump",  "$ hexdump [-n BYTES] <file>",                  "hex + ASCII dump (default 256 bytes)", true}, cmdHexdump},
        {{"crc32",    "$ crc32 <file…|-e TEXT>",                      "CRC-32 checksum", true}, cmdCrc32},
        {{"env",      "$ env [PATTERN*…]",                            "list environment variables", false}, cmdEnv},
        {{"export",   "$ export NAME=VALUE…",                         "set environment variables for this session", false}, cmdExport},
        {{"unset",    "$ unset <NAME…>",                              "remove environment variables", false}, cmdUnset},
        {{"date",     "$ date [-u] [+FORMAT]",                        "current date and time (strftime format)", false}, cmdDate},
        {{"whoami",   "$ whoami",                                     "current user name", false}, cmdWhoami},
        {{"hostname", "$ hostname",                                   "host name", false}, cmdHostname},
        {{"uname",    "$ uname [-a -s -n -r -v -m]",                  "system information", false}, cmdUname},
        {{"which",    "$ which [-a] <name…>",                         "locate a command (terminal command or PATH)", false}, cmdWhich},
        {{"uptime",   "$ uptime",                                     "time since boot and load average", false}, cmdUptime},
        {{"free",     "$ free [-b]",                                  "memory usage", false}, cmdFree},
        {{"nproc",    "$ nproc",                                      "number of CPU cores", false}, cmdNproc},
        {{"df",       "$ df [-b] [path…]",                            "free disk space", true}, cmdDf},
        {{"seq",      "$ seq A:B[:STEP]   |   $ seq [first [step]] last", "print a sequence of numbers (-s SEP)", false}, cmdSeq},
        {{"sleep",    "$ sleep <seconds>",                            "pause (max 300 seconds)", false}, cmdSleep},
        {{"alias",    "$ alias [name='command …']",                   "define or list command aliases", false}, cmdAlias},
        {{"unalias",  "$ unalias <name…> | -a",                       "remove aliases", false}, cmdUnalias},
        {{"pushd",    "$ pushd [dir]   e.g. $ pushd ~/",              "push the current directory and change to dir", true}, cmdPushd},
        {{"popd",     "$ popd",                                       "return to the directory on top of the stack", false}, cmdPopd},
        {{"dirs",     "$ dirs [-c]",                                  "show the directory stack (-c: clear)", false}, cmdDirs},
    };
    static const std::vector<Entry> r = [] {
        std::vector<Entry> v = base;
        for (const auto& e : registry2()) v.push_back(e);
        for (const auto& e : registry3()) v.push_back(e);
        for (const auto& e : registry4()) v.push_back(e);
        return v;
    }();
    return r;
}
inline const std::vector<Spec>& specs() {
    static const std::vector<Spec> s = [] { std::vector<Spec> v; for (const auto& e : registry()) v.push_back(e.spec); return v; }();
    return s;
}
inline bool takesFiles(const std::string& name) {
    for (const auto& s : specs()) if (name == s.name) return s.files;
    return false;
}
inline bool isCommand(const std::string& name) {
    for (const auto& s : specs()) if (name == s.name) return true;
    return false;
}

// Is this input line a command line?   `:cmd …`  or  `$ cmd …` (also `$cmd …`)
inline bool isCommandLine(const std::string& line) {
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos) return false;
    if (line[a] == ':') return true;
    if (line[a] != '$') return false;
    size_t b = line.find_first_not_of(" \t", a + 1);
    return b != std::string::npos && (std::isalpha(static_cast<unsigned char>(line[b])) || line[b] == '?');
}
// strip the leading `:` or `$` (and spaces)
inline std::string commandBody(const std::string& line) {
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = line.find_first_not_of(" \t", a + 1);
    return b == std::string::npos ? "" : line.substr(b);
}

// Runs `name arg…`. Returns true if `name` is one of the 50 commands (rc = exit status).
inline bool run(const std::string& name, const std::string& arg, const Io& io, int& rc) {
    for (const auto& e : registry()) {
        if (name != e.spec.name) continue;
        Ctx c;
        c.name = e.spec.name;
        Io local = io;
        c.io = &local;
        c.toks = tokenize(arg);
        c.w = expandTokens(c.toks);
        int r = e.fn(c);
        c.flush();
        rc = r != 0 ? r : c.rc;
        return true;
    }
    return false;
}

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin

#include "rin_terminal_xcmds2.h"   // batch 2 (50 more commands)
#include "rin_terminal_xcmds3.h"   // batch 3 (15 advanced commands)
#include "rin_terminal_xcmds7.h"   // batch 4 (500 commands; pulls in xcmds4..6)
