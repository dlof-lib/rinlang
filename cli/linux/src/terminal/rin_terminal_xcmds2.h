// ============================================================================
//  rin terminal — 50 MORE commands (batch 2). Header-only, POSIX + std only.
//  Same style as rin_terminal_xcmds.h:  $ cmd args …   * globs   ~/   A:B ranges
//  All text printed by these commands is English only.
// ============================================================================
#pragma once

#include "rin_terminal_xcmds.h"

#include <random>
#include <sys/ioctl.h>

namespace rin {
namespace terminal {
namespace xcmds {

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------
inline std::mt19937_64& rng() {
    static std::mt19937_64 g = [] {
        std::random_device rd;
        uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ static_cast<uint64_t>(std::time(nullptr)) ^ (static_cast<uint64_t>(::getpid()) << 16);
        return std::mt19937_64(seed);
    }();
    return g;
}
inline std::string hexOf(const unsigned char* d, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < n; ++i) { o += H[d[i] >> 4]; o += H[d[i] & 15]; }
    return o;
}
inline std::string md5hex(const std::string& data) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                              4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    std::string m = data;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 0; i < 8; ++i) m += static_cast<char>((bits >> (i * 8)) & 0xff);
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i)
            M[i] = static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4])) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 1])) << 8) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 3])) << 24);
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i) {
            uint32_t F; int g;
            if (i < 16) { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + ((F << S[i]) | (F >> (32 - S[i])));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    unsigned char out[16];
    uint32_t v[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) out[i * 4 + k] = static_cast<unsigned char>((v[i] >> (k * 8)) & 0xff);
    return hexOf(out, 16);
}
inline std::string sha1hex(const std::string& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = data;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xff);
    auto rotl = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 3]));
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    char out[41];
    for (int i = 0; i < 5; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 40);
}
// expands "a-z" ranges for tr
inline std::string trSet(const std::string& s) {
    std::string o;
    std::string d = decodeEsc(s);
    for (size_t i = 0; i < d.size(); ++i) {
        if (i + 2 < d.size() && d[i + 1] == '-' && static_cast<unsigned char>(d[i]) <= static_cast<unsigned char>(d[i + 2])) {
            for (int ch = static_cast<unsigned char>(d[i]); ch <= static_cast<unsigned char>(d[i + 2]); ++ch) o += static_cast<char>(ch);
            i += 2;
        } else o += d[i];
    }
    return o;
}
inline long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
inline bool parseDate(const std::string& s, int& y, int& m, int& d) {
    return std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// ---- calc: recursive-descent expression evaluator -------------------------------
struct CalcParser {
    std::string s; size_t i = 0; std::string error;
    void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    double expr() {
        double v = term();
        while (error.empty()) { if (eat('+')) v += term(); else if (eat('-')) v -= term(); else break; }
        return v;
    }
    double term() {
        double v = power();
        while (error.empty()) {
            if (eat('*')) v *= power();
            else if (eat('/')) { double d = power(); if (d == 0) { error = "division by zero"; return 0; } v /= d; }
            else if (eat('%')) { double d = power(); if (d == 0) { error = "division by zero"; return 0; } v = std::fmod(v, d); }
            else break;
        }
        return v;
    }
    double power() {
        double b = unary();
        ws();
        if (i < s.size() && (s[i] == '^' || (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*'))) { i += s[i] == '^' ? 1 : 2; return std::pow(b, power()); }
        return b;
    }
    double unary() {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return atom();
    }
    double atom() {
        ws();
        if (i >= s.size()) { error = "unexpected end of expression"; return 0; }
        if (eat('(')) { double v = expr(); if (!eat(')')) error = "missing ')'"; return v; }
        if (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.') {
            char* e = nullptr;
            double v = std::strtod(s.c_str() + i, &e);
            i = static_cast<size_t>(e - s.c_str());
            return v;
        }
        if (std::isalpha(static_cast<unsigned char>(s[i]))) {
            std::string n;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) n += s[i++];
            if (n == "pi") return 3.14159265358979323846;
            if (n == "e") return 2.71828182845904523536;
            if (!eat('(')) { error = "unknown name '" + n + "'"; return 0; }
            double a = expr(), b = 0;
            bool two = eat(',');
            if (two) b = expr();
            if (!eat(')')) { error = "missing ')'"; return 0; }
            if (n == "sqrt") return std::sqrt(a);
            if (n == "sin") return std::sin(a);
            if (n == "cos") return std::cos(a);
            if (n == "tan") return std::tan(a);
            if (n == "asin") return std::asin(a);
            if (n == "acos") return std::acos(a);
            if (n == "atan") return std::atan(a);
            if (n == "ln") return std::log(a);
            if (n == "log") return std::log10(a);
            if (n == "exp") return std::exp(a);
            if (n == "abs") return std::fabs(a);
            if (n == "floor") return std::floor(a);
            if (n == "ceil") return std::ceil(a);
            if (n == "round") return std::round(a);
            if (n == "pow" && two) return std::pow(a, b);
            if (n == "min" && two) return std::min(a, b);
            if (n == "max" && two) return std::max(a, b);
            error = "unknown function '" + n + "'";
            return 0;
        }
        error = std::string("unexpected '") + s[i] + "'";
        return 0;
    }
};

// ============================================================================
//  COMMANDS 51-100
// ============================================================================

// ---- files -----------------------------------------------------------------------
inline int cmdLl(Ctx& c) {
    if (!c.parse("ah", "")) return 2;
    std::vector<std::string> paths = c.pos;
    if (paths.empty()) paths.push_back(".");
    auto row = [&](const std::string& full, const std::string& name) {
        struct stat st;
        if (::lstat(full.c_str(), &st) != 0) return;
        std::string owner;
        if (struct passwd* pw = ::getpwuid(st.st_uid)) owner = pw->pw_name; else owner = std::to_string(st.st_uid);
        char b[64];
        std::snprintf(b, sizeof(b), "%10s", c.has('h') ? human(static_cast<unsigned long long>(st.st_size)).c_str() : std::to_string(static_cast<long long>(st.st_size)).c_str());
        std::string nm = name + (S_ISDIR(st.st_mode) ? "/" : "");
        if (S_ISLNK(st.st_mode)) { char t[4096]; ssize_t n = ::readlink(full.c_str(), t, sizeof(t) - 1); if (n > 0) { t[n] = 0; nm += std::string(" -> ") + t; } }
        c.say(modeString(st.st_mode) + " " + owner + " " + b + " " + timeString(st.st_mtime).substr(0, 16) + " " + nm);
    };
    for (const auto& p : paths) {
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0) { c.warn("cannot access '" + p + "': " + std::strerror(errno)); continue; }
        if (!S_ISDIR(st.st_mode)) { row(p, p); continue; }
        if (paths.size() > 1) c.say(p + ":");
        DIR* d = ::opendir(p.c_str());
        if (!d) { c.warn(p + ": " + std::strerror(errno)); continue; }
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n == "." || n == "..") continue; if (n[0] == '.' && !c.has('a')) continue; names.push_back(n); }
        ::closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) row(joinPath(p, n), n);
    }
    return c.rc;
}
inline int cmdLn(Ctx& c) {
    if (!c.parse("sf", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ ln [-s] [-f] <target> <link>");
    std::string link = isDir(c.pos[1]) ? joinPath(c.pos[1], baseName(c.pos[0])) : c.pos[1];
    if (c.has('f')) ::unlink(link.c_str());
    int r = c.has('s') ? ::symlink(c.pos[0].c_str(), link.c_str()) : ::link(c.pos[0].c_str(), link.c_str());
    if (r != 0) return c.fail("cannot create link '" + link + "': " + std::strerror(errno));
    return 0;
}
inline int cmdChmod(Ctx& c) {
    if (!c.parse("R", "")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ chmod <755 | u+x | a-w | go=r> <path…>");
    const std::string& m = c.pos[0];
    for (size_t k = 1; k < c.pos.size(); ++k) {
        struct stat st;
        if (::stat(c.pos[k].c_str(), &st) != 0) { c.warn("cannot access '" + c.pos[k] + "': " + std::strerror(errno)); continue; }
        mode_t mode = st.st_mode & 07777;
        if (m.find_first_not_of("01234567") == std::string::npos) mode = static_cast<mode_t>(std::strtol(m.c_str(), nullptr, 8));
        else {
            size_t i = 0;
            std::string who;
            while (i < m.size() && std::strchr("ugoa", m[i])) who += m[i++];
            if (who.empty()) who = "a";
            if (i >= m.size() || !std::strchr("+-=", m[i])) return c.fail("invalid mode '" + m + "'");
            char op = m[i++];
            mode_t bits = 0;
            for (; i < m.size(); ++i) {
                mode_t b = m[i] == 'r' ? 4 : m[i] == 'w' ? 2 : m[i] == 'x' ? 1 : 0;
                if (!b) return c.fail("invalid mode '" + m + "'");
                for (char w : who) {
                    if (w == 'u' || w == 'a') bits |= b << 6;
                    if (w == 'g' || w == 'a') bits |= b << 3;
                    if (w == 'o' || w == 'a') bits |= b;
                }
            }
            if (op == '+') mode |= bits; else if (op == '-') mode &= ~bits;
            else { mode_t mask = 0; for (char w : who) { if (w == 'u' || w == 'a') mask |= 0700; if (w == 'g' || w == 'a') mask |= 070; if (w == 'o' || w == 'a') mask |= 07; } mode = (mode & ~mask) | bits; }
        }
        if (::chmod(c.pos[k].c_str(), mode) != 0) c.warn("cannot change '" + c.pos[k] + "': " + std::strerror(errno));
    }
    return c.rc;
}
inline int cmdReadlink(Ctx& c) {
    if (!c.parse("f", "")) return 2;
    if (c.pos.empty()) return c.fail("missing operand");
    for (const auto& p : c.pos) {
        if (c.has('f')) { std::string r = realPathOf(p); if (r.empty()) c.warn(p + ": " + std::strerror(errno)); else c.say(r); continue; }
        char t[4096];
        ssize_t n = ::readlink(p.c_str(), t, sizeof(t) - 1);
        if (n < 0) c.warn(p + ": " + std::strerror(errno)); else { t[n] = 0; c.say(t); }
    }
    return c.rc;
}
inline std::string detectType(const std::string& p, std::string& why) {
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) { why = std::strerror(errno); return ""; }
    if (S_ISDIR(st.st_mode)) return "directory";
    if (S_ISLNK(st.st_mode)) return "symbolic link";
    if (!S_ISREG(st.st_mode)) return "special file";
    if (st.st_size == 0) return "empty";
    std::string t;
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) { why = std::strerror(errno); return ""; }
    char b[4096];
    ssize_t n = ::read(fd, b, sizeof(b));
    ::close(fd);
    t.assign(b, n > 0 ? static_cast<size_t>(n) : 0);
    auto is = [&](const char* m, size_t l) { return t.size() >= l && std::memcmp(t.data(), m, l) == 0; };
    if (is("\x89PNG", 4)) return "PNG image";
    if (is("\xFF\xD8\xFF", 3)) return "JPEG image";
    if (is("GIF8", 4)) return "GIF image";
    if (is("%PDF", 4)) return "PDF document";
    if (is("PK\x03\x04", 4)) return "ZIP archive (zip/apk/jar/docx...)";
    if (is("\x1f\x8b", 2)) return "gzip compressed data";
    if (is("\x7f" "ELF", 4)) return "ELF executable/library";
    if (is("SQLite format 3", 15)) return "SQLite database";
    if (is("RIFF", 4) && t.size() > 11 && t.compare(8, 4, "WEBP") == 0) return "WebP image";
    if (is("#!", 2)) return "script (" + t.substr(0, t.find('\n')) + ")";
    for (unsigned char ch : t) if (ch == 0) return "binary data";
    bool ascii = true;
    for (unsigned char ch : t) if (ch >= 0x80) ascii = false;
    return ascii ? "ASCII text" : "UTF-8 text";
}
inline int cmdFile(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("missing file operand");
    for (const auto& p : c.pos) { std::string why; std::string t = detectType(p, why); if (t.empty()) c.warn(p + ": " + why); else c.say(p + ": " + t); }
    return c.rc;
}
inline int cmdMktemp(Ctx& c) {
    if (!c.parse("d", "")) return 2;
    const char* td = std::getenv("TMPDIR");
    std::string tmpl = joinPath(td && *td ? td : "/tmp", "rin.XXXXXX");
    std::vector<char> b(tmpl.begin(), tmpl.end());
    b.push_back(0);
    if (c.has('d')) { if (!::mkdtemp(b.data())) return c.fail(std::strerror(errno)); }
    else { int fd = ::mkstemp(b.data()); if (fd < 0) return c.fail(std::strerror(errno)); ::close(fd); }
    c.say(b.data());
    return 0;
}
inline int cmdExists(Ctx& c) {
    if (!c.parse("fdrwxsz", "")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ exists [-f -d -r -w -x -s -z] <path…>");
    bool all = true;
    for (const auto& p : c.pos) {
        struct stat st;
        bool ok = ::stat(p.c_str(), &st) == 0;
        if (ok && c.has('f')) ok = S_ISREG(st.st_mode);
        if (ok && c.has('d')) ok = S_ISDIR(st.st_mode);
        if (ok && c.has('r')) ok = ::access(p.c_str(), R_OK) == 0;
        if (ok && c.has('w')) ok = ::access(p.c_str(), W_OK) == 0;
        if (ok && c.has('x')) ok = ::access(p.c_str(), X_OK) == 0;
        if (ok && c.has('s')) ok = st.st_size > 0;
        if (ok && c.has('z')) ok = st.st_size == 0;
        c.say(std::string(ok ? "true   " : "false  ") + p);
        all = all && ok;
    }
    return all ? 0 : 1;
}
inline bool writeText(const std::string& path, const std::string& text, bool append, std::string& why) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) { why = std::strerror(errno); return false; }
    size_t off = 0;
    while (off < text.size()) {
        ssize_t w = ::write(fd, text.data() + off, text.size() - off);
        if (w < 0) { why = std::strerror(errno); ::close(fd); return false; }
        off += static_cast<size_t>(w);
    }
    ::close(fd);
    return true;
}
inline int cmdTee(Ctx& c) {
    if (!c.parse("a", "e")) return 2;
    if (c.pos.size() != 1 || !c.hasv('e')) return c.fail("usage: $ tee [-a] <file> -e TEXT");
    std::string text = decodeEsc(c.v('e')) + "\n", why;
    if (!writeText(c.pos[0], text, c.has('a'), why)) return c.fail(c.pos[0] + ": " + why);
    c.o += text;
    return 0;
}
inline int cmdAppend(Ctx& c) {
    if (!c.parse("e", "")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ append [-e] <file> <text…>");
    std::string text;
    for (size_t i = 1; i < c.pos.size(); ++i) { if (i > 1) text += ' '; text += c.pos[i]; }
    if (c.has('e')) text = decodeEsc(text);
    text += "\n";
    std::string why;
    if (!writeText(c.pos[0], text, true, why)) return c.fail(c.pos[0] + ": " + why);
    return 0;
}
inline int cmdSplit(Ctx& c) {
    if (!c.parse("", "l")) return 2;
    if (c.pos.empty() || c.pos.size() > 2) return c.fail("usage: $ split [-l LINES] <file> [prefix]");
    long n = 1000;
    if (c.hasv('l')) { char* e = nullptr; n = std::strtol(c.v('l').c_str(), &e, 10); if (!e || *e || n < 1) return c.fail("invalid line count"); }
    std::string t, why;
    if (!readFileCapped(c.pos[0], t, why)) return c.fail(c.pos[0] + ": " + why);
    std::string prefix = c.pos.size() == 2 ? c.pos[1] : "x";
    auto L = textLines(t);
    size_t parts = (L.size() + static_cast<size_t>(n) - 1) / static_cast<size_t>(n);
    if (parts > 1000) return c.fail("too many parts (use a larger -l)");
    for (size_t k = 0; k < parts; ++k) {
        std::string chunk;
        for (size_t i = k * static_cast<size_t>(n); i < std::min(L.size(), (k + 1) * static_cast<size_t>(n)); ++i) chunk += L[i] + "\n";
        char sfx[16];
        std::snprintf(sfx, sizeof(sfx), "%03zu", k);
        if (!writeText(prefix + sfx, chunk, false, why)) return c.fail(prefix + sfx + ": " + why);
        c.say(prefix + sfx);
    }
    return 0;
}

// ---- text ------------------------------------------------------------------------
inline int cmdDiff(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ diff <file1> <file2>");
    std::string a, b, why;
    if (!readFileCapped(c.pos[0], a, why)) return c.fail(c.pos[0] + ": " + why);
    if (!readFileCapped(c.pos[1], b, why)) return c.fail(c.pos[1] + ": " + why);
    auto A = textLines(a), B = textLines(b);
    if (A.size() > 3000 || B.size() > 3000) return c.fail("files too long for diff (limit 3000 lines)");
    size_t n = A.size(), m = B.size();
    std::vector<std::vector<unsigned short>> L(n + 1, std::vector<unsigned short>(m + 1, 0));
    for (size_t i = n; i-- > 0;) for (size_t j = m; j-- > 0;) L[i][j] = A[i] == B[j] ? static_cast<unsigned short>(L[i + 1][j + 1] + 1) : std::max(L[i + 1][j], L[i][j + 1]);
    size_t i = 0, j = 0;
    bool diff = false;
    while (i < n || j < m) {
        if (i < n && j < m && A[i] == B[j]) { ++i; ++j; }
        else if (i < n && (j == m || L[i + 1][j] >= L[i][j + 1])) { c.say("- " + std::to_string(i + 1) + ": " + A[i]); ++i; diff = true; }
        else { c.say("+ " + std::to_string(j + 1) + ": " + B[j]); ++j; diff = true; }
    }
    if (!diff) c.say("files are identical");
    return diff ? 1 : 0;
}
inline int cmdComm(Ctx& c) {
    if (!c.parse("123", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ comm [-1 -2 -3] <sorted1> <sorted2>");
    std::string a, b, why;
    if (!readFileCapped(c.pos[0], a, why)) return c.fail(c.pos[0] + ": " + why);
    if (!readFileCapped(c.pos[1], b, why)) return c.fail(c.pos[1] + ": " + why);
    auto A = textLines(a), B = textLines(b);
    size_t i = 0, j = 0;
    while (i < A.size() || j < B.size()) {
        if (j >= B.size() || (i < A.size() && A[i] < B[j])) { if (!c.has('1')) c.say(A[i]); ++i; }
        else if (i >= A.size() || B[j] < A[i]) { if (!c.has('2')) c.say(std::string(c.has('1') ? "" : "\t") + B[j]); ++j; }
        else { if (!c.has('3')) c.say(std::string((c.has('1') ? 0 : 1) + (c.has('2') ? 0 : 1), '\t') + A[i]); ++i; ++j; }
    }
    return 0;
}
inline int cmdPaste(Ctx& c) {
    if (!c.parse("", "d")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ paste [-d DELIM] <file1> <file2…>");
    std::string d = c.hasv('d') ? decodeEsc(c.v('d')) : "\t";
    std::vector<std::vector<std::string>> cols;
    size_t rows = 0;
    for (const auto& p : c.pos) {
        std::string t, why;
        if (!readFileCapped(p, t, why)) return c.fail(p + ": " + why);
        cols.push_back(textLines(t));
        rows = std::max(rows, cols.back().size());
    }
    for (size_t r = 0; r < rows; ++r) {
        std::string line;
        for (size_t k = 0; k < cols.size(); ++k) { if (k) line += d; if (r < cols[k].size()) line += cols[k][r]; }
        c.say(line);
    }
    return 0;
}
inline int cmdTr(Ctx& c) {
    if (!c.parse("ds", "e")) return 2;
    bool del = c.has('d');
    size_t sets = del ? 1 : 2;
    if (c.pos.size() < sets) return c.fail(del ? "usage: $ tr -d <set> <file…|-e TEXT>" : "usage: $ tr <from> <to> <file…|-e TEXT>");
    std::string from = trSet(c.pos[0]), to = del ? "" : trSet(c.pos[1]);
    if (!del && to.empty()) return c.fail("empty replacement set");
    std::vector<In> in;
    if (!gatherInputs(c, in, sets)) return 1;
    for (auto& f : in) {
        std::string o;
        char last = 0;
        for (char ch : f.text) {
            size_t k = from.find(ch);
            if (k == std::string::npos) { o += ch; last = 0; continue; }
            if (del) continue;
            char r = to[std::min(k, to.size() - 1)];
            if (c.has('s') && last == r) continue;
            o += r; last = r;
        }
        c.o += o;
        if (!o.empty() && o.back() != '\n') c.o += '\n';
    }
    return c.rc;
}
inline int cmdFold(Ctx& c) {
    if (!c.parse("", "we")) return 2;
    long w = 80;
    if (c.hasv('w')) { char* e = nullptr; w = std::strtol(c.v('w').c_str(), &e, 10); if (!e || *e || w < 1) return c.fail("invalid width"); }
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in)
        for (const auto& l : textLines(f.text)) {
            auto ch = utf8Chars(l);
            if (ch.empty()) { c.say(""); continue; }
            for (size_t i = 0; i < ch.size(); i += static_cast<size_t>(w)) {
                std::string o;
                for (size_t k = i; k < std::min(ch.size(), i + static_cast<size_t>(w)); ++k) o += ch[k];
                c.say(o);
            }
        }
    return c.rc;
}
inline int cmdReplace(Ctx& c) {
    if (!c.parse("i", "e")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ replace [-i] <old> <new> <file…|-e TEXT>");
    std::string from = c.pos[0], to = c.pos[1];
    if (from.empty()) return c.fail("empty search text");
    std::vector<In> in;
    if (!gatherInputs(c, in, 2)) return 1;
    for (auto& f : in) {
        std::string o;
        size_t i = 0, n = 0;
        while (true) { size_t j = f.text.find(from, i); if (j == std::string::npos) { o += f.text.substr(i); break; } o += f.text.substr(i, j - i) + to; i = j + from.size(); ++n; }
        if (c.has('i') && f.name != "-e") {
            std::string why;
            if (!writeText(f.name, o, false, why)) c.warn(f.name + ": " + why); else c.say(f.name + ": " + std::to_string(n) + " replacement(s)");
        } else { c.o += o; if (!o.empty() && o.back() != '\n') c.o += '\n'; }
    }
    return c.rc;
}
inline int cmdTrim(Ctx& c) {
    if (!c.parse("lr", "e")) return 2;
    bool L = c.has('l') || !c.has('r'), R = c.has('r') || !c.has('l');
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in)
        for (std::string l : textLines(f.text)) {
            if (L) { size_t a = l.find_first_not_of(" \t"); l = a == std::string::npos ? "" : l.substr(a); }
            if (R) { size_t b = l.find_last_not_of(" \t"); l = b == std::string::npos ? "" : l.substr(0, b + 1); }
            c.say(l);
        }
    return c.rc;
}
inline int cmdShuf(Ctx& c) {
    if (!c.parse("", "ne")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    std::vector<std::string> L;
    for (const auto& f : in) { auto l = textLines(f.text); L.insert(L.end(), l.begin(), l.end()); }
    std::shuffle(L.begin(), L.end(), rng());
    size_t n = L.size();
    if (c.hasv('n')) n = std::min(n, static_cast<size_t>(std::max(0L, std::strtol(c.v('n').c_str(), nullptr, 10))));
    for (size_t i = 0; i < n; ++i) c.say(L[i]);
    return c.rc;
}
inline int cmdWordfreq(Ctx& c) {
    if (!c.parse("", "ne")) return 2;
    size_t top = 10;
    if (c.hasv('n')) top = static_cast<size_t>(std::max(1L, std::strtol(c.v('n').c_str(), nullptr, 10)));
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    std::map<std::string, size_t> freq;
    for (const auto& f : in) {
        std::string w;
        auto flush = [&]() { if (!w.empty()) { ++freq[w]; w.clear(); } };
        for (unsigned char ch : f.text) { if (std::isalnum(ch) || ch >= 0x80 || ch == '\'') w += static_cast<char>(std::tolower(ch)); else flush(); }
        flush();
    }
    std::vector<std::pair<std::string, size_t>> v(freq.begin(), freq.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < std::min(top, v.size()); ++i) { char b[24]; std::snprintf(b, sizeof(b), "%7zu ", v[i].second); c.say(std::string(b) + v[i].first); }
    return c.rc;
}
inline int cmdCount(Ctx& c) {
    if (!c.parse("i", "e")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ count [-i] <needle> <file…|-e TEXT>");
    std::string needle = c.pos[0];
    if (needle.empty()) return c.fail("empty needle");
    auto low = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); }); return s; };
    if (c.has('i')) needle = low(needle);
    std::vector<In> in;
    if (!gatherInputs(c, in, 1)) return 1;
    for (const auto& f : in) {
        std::string t = c.has('i') ? low(f.text) : f.text;
        size_t n = 0, i = 0;
        while ((i = t.find(needle, i)) != std::string::npos) { ++n; i += needle.size(); }
        c.say(std::to_string(n) + "  " + f.name);
    }
    return c.rc;
}
inline int cmdRot13(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (auto& f : in) {
        for (auto& ch : f.text) {
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>('a' + (ch - 'a' + 13) % 26);
            else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>('A' + (ch - 'A' + 13) % 26);
        }
        c.o += f.text;
        if (!f.text.empty() && f.text.back() != '\n') c.o += '\n';
    }
    return c.rc;
}

// ---- hashes / encodings ------------------------------------------------------------
inline int cmdMd5(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) c.say(md5hex(f.text) + "  " + f.name);
    return c.rc;
}
inline int cmdSha1(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) c.say(sha1hex(f.text) + "  " + f.name);
    return c.rc;
}
inline int cmdUrlencode(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) {
        std::string o;
        for (unsigned char ch : f.text) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += static_cast<char>(ch);
            else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", ch); o += b; }
        }
        c.say(o);
    }
    return c.rc;
}
inline int cmdUrldecode(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) {
        std::string o;
        for (size_t i = 0; i < f.text.size(); ++i) {
            if (f.text[i] == '%' && i + 2 < f.text.size() + 0 && std::isxdigit(static_cast<unsigned char>(f.text[i + 1])) && std::isxdigit(static_cast<unsigned char>(f.text[i + 2]))) {
                o += static_cast<char>(std::strtol(f.text.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2;
            } else if (f.text[i] == '+') o += ' ';
            else o += f.text[i];
        }
        c.say(o);
    }
    return c.rc;
}
inline int cmdHex(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) c.say(hexOf(reinterpret_cast<const unsigned char*>(f.text.data()), f.text.size()));
    return c.rc;
}
inline int cmdUnhex(Ctx& c) {
    if (!c.parse("", "e")) return 2;
    std::vector<In> in;
    if (!gatherInputs(c, in)) return 1;
    for (const auto& f : in) {
        std::string clean, o;
        for (char ch : f.text) if (!std::isspace(static_cast<unsigned char>(ch))) clean += ch;
        if (clean.size() % 2 || clean.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) { c.warn(f.name + ": invalid hex input"); continue; }
        for (size_t i = 0; i < clean.size(); i += 2) o += static_cast<char>(std::strtol(clean.substr(i, 2).c_str(), nullptr, 16));
        c.o += o;
        if (o.empty() || o.back() != '\n') c.o += '\n';
    }
    return c.rc;
}
inline uint32_t decodeCp(const std::string& ch) {
    unsigned char b0 = static_cast<unsigned char>(ch[0]);
    if (ch.size() == 1) return b0;
    if (ch.size() == 2) return ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(ch[1]) & 0x3F);
    if (ch.size() == 3) return ((b0 & 0x0F) << 12) | ((static_cast<unsigned char>(ch[1]) & 0x3F) << 6) | (static_cast<unsigned char>(ch[2]) & 0x3F);
    return ((b0 & 0x07) << 18) | ((static_cast<unsigned char>(ch[1]) & 0x3F) << 12) | ((static_cast<unsigned char>(ch[2]) & 0x3F) << 6) | (static_cast<unsigned char>(ch[3]) & 0x3F);
}
inline int cmdAscii(Ctx& c) {
    if (!c.parse("", "")) return 2;
    char b[96];
    if (c.pos.empty()) {
        for (int row = 0; row < 8; ++row) {
            std::string l;
            for (int col = 0; col < 12; ++col) { int v = 32 + row + col * 8; if (v < 127) { std::snprintf(b, sizeof(b), "%3d %02x %c   ", v, v, v); l += b; } }
            c.say(l);
        }
        return 0;
    }
    for (const auto& a : c.pos)
        for (const auto& ch : utf8Chars(a)) { uint32_t cp = decodeCp(ch); std::snprintf(b, sizeof(b), "U+%04X  dec %u  hex %X  utf8 %s", cp, cp, cp, hexOf(reinterpret_cast<const unsigned char*>(ch.data()), ch.size()).c_str()); c.say(ch + "  " + b); }
    return 0;
}
inline std::string toBase(unsigned long long v, int base) {
    if (v == 0) return "0";
    std::string s;
    while (v) { s += "0123456789abcdefghijklmnopqrstuvwxyz"[v % static_cast<unsigned>(base)]; v /= static_cast<unsigned>(base); }
    std::reverse(s.begin(), s.end());
    return s;
}
inline int cmdConv(Ctx& c) {
    if (!c.parse("", "ft")) return 2;
    if (c.pos.size() != 1) return c.fail("usage: $ conv <number> [-f BASE] [-t BASE]   (0x / 0b / 0o prefixes auto-detected)");
    std::string s = c.pos[0];
    int from = 10;
    if (c.hasv('f')) from = static_cast<int>(std::strtol(c.v('f').c_str(), nullptr, 10));
    else if (startsWith(s, "0x") || startsWith(s, "0X")) { from = 16; s = s.substr(2); }
    else if (startsWith(s, "0b") || startsWith(s, "0B")) { from = 2; s = s.substr(2); }
    else if (startsWith(s, "0o") || startsWith(s, "0O")) { from = 8; s = s.substr(2); }
    if (from < 2 || from > 36) return c.fail("base must be 2..36");
    char* e = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &e, from);
    if (s.empty() || !e || *e || errno) return c.fail("invalid number '" + c.pos[0] + "' for base " + std::to_string(from));
    if (c.hasv('t')) {
        int to = static_cast<int>(std::strtol(c.v('t').c_str(), nullptr, 10));
        if (to < 2 || to > 36) return c.fail("base must be 2..36");
        c.say(toBase(v, to));
    } else { c.say("bin  " + toBase(v, 2)); c.say("oct  " + toBase(v, 8)); c.say("dec  " + toBase(v, 10)); c.say("hex  " + toBase(v, 16)); }
    return 0;
}

// ---- math ---------------------------------------------------------------------------
inline int cmdCalc(Ctx& c) {
    if (c.toks.empty()) return c.fail("usage: $ calc <expression>   e.g. $ calc 2*(3+4)^2 / sqrt(16)");
    CalcParser p;
    for (size_t i = 0; i < c.toks.size(); ++i) { if (i) p.s += ' '; p.s += c.toks[i].s; }
    double v = p.expr();
    p.ws();
    if (p.error.empty() && p.i < p.s.size()) p.error = std::string("unexpected '") + p.s[p.i] + "'";
    if (!p.error.empty()) return c.fail(p.error);
    char b[64];
    if (std::fabs(v) < 1e15 && v == std::floor(v)) std::snprintf(b, sizeof(b), "%.0f", v);
    else std::snprintf(b, sizeof(b), "%.12g", v);
    c.say(b);
    return 0;
}
inline int cmdFactor(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ factor <number…>");
    for (const auto& a : c.pos) {
        char* e = nullptr;
        unsigned long long n = std::strtoull(a.c_str(), &e, 10);
        if (a.empty() || !e || *e || n < 2) { c.warn("'" + a + "' is not an integer >= 2"); continue; }
        if (n > 1000000000000000ULL) { c.warn("'" + a + "' is too large (limit 1e15)"); continue; }
        std::string o = a + ":";
        unsigned long long m = n;
        for (unsigned long long p = 2; p * p <= m; p += p == 2 ? 1 : 2) while (m % p == 0) { o += " " + std::to_string(p); m /= p; }
        if (m > 1) o += " " + std::to_string(m);
        c.say(o);
    }
    return c.rc;
}
inline int cmdPrimes(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long a = 2, b = 100;
    if (c.pos.size() != 1 || !parseRange(c.pos[0], a, b)) return c.fail("usage: $ primes A:B   e.g. $ primes 1:100");
    if (b > 10000000) return c.fail("limit is 10000000");
    std::vector<bool> comp(static_cast<size_t>(b) + 1, false);
    size_t shown = 0;
    for (long i = 2; i <= b; ++i) {
        if (comp[static_cast<size_t>(i)]) continue;
        for (long j = i * i; j <= b; j += i) comp[static_cast<size_t>(j)] = true;
        if (i >= a) { if (shown++ >= 100000) { c.say("… (stopped after 100000)"); break; } c.o += std::to_string(i) + "\n"; }
    }
    return 0;
}
inline int cmdFib(Ctx& c) {
    if (!c.parse("", "")) return 2;
    long n = 10;
    if (!c.pos.empty()) { char* e = nullptr; n = std::strtol(c.pos[0].c_str(), &e, 10); if (!e || *e || n < 1 || n > 90) return c.fail("usage: $ fib [count 1..90]"); }
    unsigned long long a = 0, b = 1;
    for (long i = 0; i < n; ++i) { c.say(std::to_string(a)); unsigned long long t = a + b; a = b; b = t; }
    return 0;
}
inline int cmdGcd(Ctx& c) {
    if (!c.parse("l", "")) return 2;
    if (c.pos.size() < 2) return c.fail("usage: $ gcd [-l] <a> <b…>   (-l: lcm)");
    unsigned long long g = 0, l = 1;
    for (const auto& a : c.pos) {
        char* e = nullptr;
        unsigned long long v = std::strtoull(a.c_str(), &e, 10);
        if (a.empty() || !e || *e || v == 0) return c.fail("'" + a + "' is not a positive integer");
        unsigned long long x = g, y = v;
        while (y) { unsigned long long t = x % y; x = y; y = t; }
        g = g ? x : v;
        unsigned long long gg = l, vv = v;
        while (vv) { unsigned long long t = gg % vv; gg = vv; vv = t; }
        l = l / gg * v;
    }
    c.say(std::to_string(c.has('l') ? l : g));
    return 0;
}
inline int cmdRand(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    long a = 0, b = 99, n = 1;
    if (!c.pos.empty() && !parseRange(c.pos[0], a, b)) {
        // allow 0 / negatives through simple A:B parsing
        size_t k = c.pos[0].find(':');
        if (k == std::string::npos) return c.fail("usage: $ rand [A:B] [-n COUNT]");
        a = std::strtol(c.pos[0].substr(0, k).c_str(), nullptr, 10);
        b = std::strtol(c.pos[0].substr(k + 1).c_str(), nullptr, 10);
        if (b < a) return c.fail("bad range");
    }
    if (c.hasv('n')) { n = std::strtol(c.v('n').c_str(), nullptr, 10); if (n < 1 || n > 10000) return c.fail("count must be 1..10000"); }
    std::uniform_int_distribution<long long> d(a, b);
    for (long i = 0; i < n; ++i) c.say(std::to_string(d(rng())));
    return 0;
}
inline int cmdUuid(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    long n = 1;
    if (c.hasv('n')) { n = std::strtol(c.v('n').c_str(), nullptr, 10); if (n < 1 || n > 1000) return c.fail("count must be 1..1000"); }
    for (long k = 0; k < n; ++k) {
        unsigned char b[16];
        uint64_t x = rng()(), y = rng()();
        std::memcpy(b, &x, 8); std::memcpy(b + 8, &y, 8);
        b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
        b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
        std::string h = hexOf(b, 16);
        c.say(h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20));
    }
    return 0;
}

// ---- time ----------------------------------------------------------------------------
inline int cmdTimestamp(Ctx& c) {
    if (!c.parse("u", "")) return 2;
    if (c.pos.empty()) { c.say(std::to_string(static_cast<long long>(std::time(nullptr)))); return 0; }
    std::string s;
    for (size_t i = 0; i < c.pos.size(); ++i) { if (i) s += ' '; s += c.pos[i]; }
    char* e = nullptr;
    long long n = std::strtoll(s.c_str(), &e, 10);
    if (e && !*e && s.find('-', 1) == std::string::npos) { c.say(timeString(static_cast<time_t>(n), c.has('u')) + (c.has('u') ? " UTC" : "")); return 0; }
    int y, mo, d, h = 0, mi = 0, se = 0;
    int got = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (got < 3) return c.fail("usage: $ timestamp [EPOCH | \"YYYY-MM-DD [HH:MM:SS]\"] [-u]");
    struct tm t;
    std::memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se; t.tm_isdst = -1;
    time_t r = c.has('u') ? ::timegm(&t) : ::mktime(&t);
    c.say(std::to_string(static_cast<long long>(r)));
    return 0;
}
inline int cmdCal(Ctx& c) {
    if (!c.parse("", "")) return 2;
    time_t now = std::time(nullptr);
    struct tm tmv;
    ::localtime_r(&now, &tmv);
    int month = tmv.tm_mon + 1, year = tmv.tm_year + 1900;
    if (c.pos.size() == 2) { month = std::atoi(c.pos[0].c_str()); year = std::atoi(c.pos[1].c_str()); }
    else if (c.pos.size() == 1) { year = std::atoi(c.pos[0].c_str()); month = 0; }
    else if (!c.pos.empty()) return c.fail("usage: $ cal [month] [year]  |  $ cal <year>");
    if (year < 1 || year > 9999 || month < 0 || month > 12) return c.fail("invalid month/year");
    auto one = [&](int m) {
        static const char* names[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        std::string title = std::string(names[m - 1]) + " " + std::to_string(year);
        c.say(std::string((20 - title.size()) / 2, ' ') + title);
        c.say("Su Mo Tu We Th Fr Sa");
        int dim[] = {31, (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        long long days = daysFromCivil(year, static_cast<unsigned>(m), 1);
        int wd = static_cast<int>(((days % 7) + 11) % 7);   // 1970-01-01 was Thursday(4) → Sunday = 0
        std::string line(static_cast<size_t>(wd) * 3, ' ');
        for (int d = 1; d <= dim[m - 1]; ++d) {
            char b[16]; std::snprintf(b, sizeof(b), "%2d", d);
            line += b;
            if ((wd + d) % 7 == 0) { c.say(line); line.clear(); } else line += ' ';
        }
        if (!line.empty()) { while (!line.empty() && line.back() == ' ') line.pop_back(); c.say(line); }
    };
    if (month == 0) for (int m = 1; m <= 12; ++m) { one(m); c.say(""); } else one(month);
    return 0;
}
inline int cmdDatediff(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ datediff <YYYY-MM-DD> <YYYY-MM-DD>");
    int y1, m1, d1, y2, m2, d2;
    if (!parseDate(c.pos[0], y1, m1, d1) || !parseDate(c.pos[1], y2, m2, d2)) return c.fail("dates must look like 2026-10-17");
    long long diff = daysFromCivil(y2, static_cast<unsigned>(m2), static_cast<unsigned>(d2)) - daysFromCivil(y1, static_cast<unsigned>(m1), static_cast<unsigned>(d1));
    c.say(std::to_string(diff) + " days");
    return 0;
}

// ---- system --------------------------------------------------------------------------
inline int cmdId(Ctx& c) {
    if (!c.parse("", "")) return 2;
    std::string name = "?";
    if (struct passwd* pw = ::getpwuid(::geteuid())) name = pw->pw_name;
    std::string s = "uid=" + std::to_string(static_cast<unsigned>(::getuid())) + "(" + name + ") gid=" + std::to_string(static_cast<unsigned>(::getgid()));
    gid_t g[64];
    int n = ::getgroups(64, g);
    if (n > 0) { s += " groups="; for (int i = 0; i < n; ++i) { if (i) s += ","; s += std::to_string(static_cast<unsigned>(g[i])); } }
    c.say(s);
    return 0;
}
inline int cmdPs(Ctx& c) {
    if (!c.parse("", "")) return 2;
    DIR* d = ::opendir("/proc");
    if (!d) return c.fail("cannot read /proc: " + std::string(std::strerror(errno)));
    std::vector<long> pids;
    while (dirent* e = ::readdir(d)) { char* end = nullptr; long p = std::strtol(e->d_name, &end, 10); if (end && !*end && p > 0) pids.push_back(p); }
    ::closedir(d);
    std::sort(pids.begin(), pids.end());
    c.say("   PID   PPID S NAME");
    size_t shown = 0;
    for (long p : pids) {
        std::string t, why;
        if (!readFileCapped("/proc/" + std::to_string(p) + "/stat", t, why)) continue;
        size_t a = t.find('('), b = t.rfind(')');
        if (a == std::string::npos || b == std::string::npos || b < a) continue;
        std::string name = t.substr(a + 1, b - a - 1);
        std::istringstream is(t.substr(b + 2));
        std::string state; long ppid = 0;
        is >> state >> ppid;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%6ld %6ld %s ", p, ppid, state.c_str());
        c.say(std::string(buf) + name);
        if (++shown >= 300) { c.say("… (stopped after 300 processes)"); break; }
    }
    return 0;
}
inline int cmdCpuinfo(Ctx& c) {
    if (!c.parse("", "")) return 2;
    std::string t, why;
    if (!readFileCapped("/proc/cpuinfo", t, why)) return c.fail("cannot read /proc/cpuinfo: " + why);
    int procs = 0;
    std::string model, hw, arch;
    for (const auto& l : textLines(t)) {
        size_t col = l.find(':');
        if (col == std::string::npos) continue;
        std::string k = l.substr(0, col), v = l.substr(col + 1);
        k.erase(k.find_last_not_of(" \t") + 1);
        if (!v.empty() && v[0] == ' ') v = v.substr(1);
        if (k == "processor") ++procs;
        else if (k == "model name" && model.empty()) model = v;
        else if (k == "Hardware" && hw.empty()) hw = v;
        else if (k == "CPU architecture" && arch.empty()) arch = v;
    }
    long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    c.say("processors: " + std::to_string(procs ? procs : online));
    if (!model.empty()) c.say("model: " + model);
    if (!hw.empty()) c.say("hardware: " + hw);
    if (!arch.empty()) c.say("architecture: " + arch);
    return 0;
}
inline int cmdMounts(Ctx& c) {
    if (!c.parse("a", "")) return 2;
    std::string t, why;
    if (!readFileCapped("/proc/mounts", t, why)) return c.fail("cannot read /proc/mounts: " + why);
    size_t n = 0;
    for (const auto& l : textLines(t)) {
        std::istringstream is(l);
        std::string dev, mp, type;
        is >> dev >> mp >> type;
        if (!c.has('a') && (type == "proc" || type == "sysfs" || type == "cgroup" || type == "cgroup2" || type == "devpts" || type == "tmpfs" || type == "mqueue" || type == "securityfs" || type == "debugfs" || type == "tracefs" || type == "bpf" || type == "pstore" || type == "configfs" || type == "selinuxfs" || type == "fusectl")) continue;
        c.say(type + "  " + mp + "  (" + dev + ")");
        if (++n >= 200) break;
    }
    if (!n) c.say("(nothing to show; use -a for all mounts)");
    return 0;
}
inline int cmdPrintenv(Ctx& c) {
    if (!c.parse("", "")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ printenv <NAME…>");
    bool all = true;
    for (const auto& n : c.pos) { const char* v = std::getenv(n.c_str()); if (v) c.say(v); else all = false; }
    return all ? 0 : 1;
}
inline int cmdYes(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    long n = 10;
    if (c.hasv('n')) { char* e = nullptr; n = std::strtol(c.v('n').c_str(), &e, 10); if (!e || *e || n < 1 || n > 10000) return c.fail("count must be 1..10000"); }
    std::string t = "y";
    if (!c.pos.empty()) { t.clear(); for (size_t i = 0; i < c.pos.size(); ++i) { if (i) t += ' '; t += c.pos[i]; } }
    for (long i = 0; i < n; ++i) c.say(t);
    return 0;
}
inline int cmdTrue(Ctx&) { return 0; }
inline int cmdFalse(Ctx&) { return 1; }
inline int cmdTerm(Ctx& c) {
    if (!c.parse("", "")) return 2;
    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    bool tty = ::ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0;
    auto env = [](const char* n) { const char* v = std::getenv(n); return std::string(v ? v : "(unset)"); };
    if (tty) c.say("size:      " + std::to_string(ws.ws_col) + " x " + std::to_string(ws.ws_row));
    else c.say("size:      not a tty (COLUMNS=" + env("COLUMNS") + " LINES=" + env("LINES") + ")");
    c.say("TERM:      " + env("TERM"));
    c.say("COLORTERM: " + env("COLORTERM"));
    c.say("LANG:      " + env("LANG"));
    c.say("NO_COLOR:  " + env("NO_COLOR"));
    return 0;
}
inline int cmdPath(Ctx& c) {
    if (!c.parse("c", "")) return 2;
    const char* pe = std::getenv("PATH");
    std::stringstream ss(pe ? pe : "");
    std::string dir;
    while (std::getline(ss, dir, ':')) c.say(c.has('c') ? std::string(isDir(dir) ? "ok       " : "missing  ") + dir : dir);
    return 0;
}
inline int cmdNumfmt(Ctx& c) {
    if (!c.parse("r", "")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ numfmt <bytes…>   |   $ numfmt -r <1.5G…>");
    for (const auto& a : c.pos) {
        if (!c.has('r')) {
            char* e = nullptr;
            unsigned long long v = std::strtoull(a.c_str(), &e, 10);
            if (a.empty() || !e || *e) { c.warn("'" + a + "' is not a number"); continue; }
            c.say(human(v));
        } else {
            char* e = nullptr;
            double v = std::strtod(a.c_str(), &e);
            std::string u = e ? e : "";
            double mul = 1;
            if (u.empty() || u == "B") mul = 1;
            else if (u == "K" || u == "k" || u == "KB" || u == "KiB") mul = 1024.0;
            else if (u == "M" || u == "MB" || u == "MiB") mul = 1024.0 * 1024;
            else if (u == "G" || u == "GB" || u == "GiB") mul = 1024.0 * 1024 * 1024;
            else if (u == "T" || u == "TB" || u == "TiB") mul = 1024.0 * 1024 * 1024 * 1024;
            else { c.warn("'" + a + "': unknown unit"); continue; }
            if (e == a.c_str()) { c.warn("'" + a + "' is not a number"); continue; }
            char b[64];
            std::snprintf(b, sizeof(b), "%.0f", v * mul);
            c.say(b);
        }
    }
    return c.rc;
}

// ----------------------------------------------------------------------------
// registry (batch 2) — merged into registry() in rin_terminal_xcmds.h
// ----------------------------------------------------------------------------
inline const std::vector<Entry>& registry2() {
    static const std::vector<Entry> r = {
        {{"ll",        "$ ll [-a -h] [path…]",                         "long listing: mode, owner, size, time, name", true}, cmdLl},
        {{"ln",        "$ ln [-s -f] <target> <link>",                 "create hard or symbolic links", true}, cmdLn},
        {{"chmod",     "$ chmod <755 | u+x | a-w | go=r> <path…>",     "change file permissions", true}, cmdChmod},
        {{"readlink",  "$ readlink [-f] <link…>",                      "print where a symlink points (-f: fully resolved)", true}, cmdReadlink},
        {{"file",      "$ file <path…>",                               "detect file type (PNG, ZIP, ELF, text, …)", true}, cmdFile},
        {{"mktemp",    "$ mktemp [-d]",                                "create a temporary file or directory", false}, cmdMktemp},
        {{"exists",    "$ exists [-f -d -r -w -x -s -z] <path…>",      "test paths: prints true/false (exit 1 if any is false)", true}, cmdExists},
        {{"tee",       "$ tee [-a] <file> -e TEXT",                    "write TEXT to a file and print it", true}, cmdTee},
        {{"append",    "$ append [-e] <file> <text…>",                 "append a line of text to a file", true}, cmdAppend},
        {{"split",     "$ split [-l LINES] <file> [prefix]",           "split a file into pieces (prefix000, prefix001, …)", true}, cmdSplit},
        {{"diff",      "$ diff <file1> <file2>",                       "line differences between two files (+ / -)", true}, cmdDiff},
        {{"comm",      "$ comm [-1 -2 -3] <sorted1> <sorted2>",       "compare two sorted files line by line", true}, cmdComm},
        {{"paste",     "$ paste [-d DELIM] <file1> <file2…>",          "merge files side by side", true}, cmdPaste},
        {{"tr",        "$ tr [-s] <from> <to> | -d <set> <file…|-e TEXT>", "translate or delete characters (ranges a-z)", true}, cmdTr},
        {{"fold",      "$ fold [-w N] <file…|-e TEXT>",                "wrap long lines at N characters (default 80)", true}, cmdFold},
        {{"replace",   "$ replace [-i] <old> <new> <file…|-e TEXT>",   "replace text (-i: edit files in place)", true}, cmdReplace},
        {{"trim",      "$ trim [-l -r] <file…|-e TEXT>",               "strip leading/trailing spaces of each line", true}, cmdTrim},
        {{"shuf",      "$ shuf [-n N] <file…|-e TEXT>",                "shuffle lines randomly", true}, cmdShuf},
        {{"wordfreq",  "$ wordfreq [-n TOP] <file…|-e TEXT>",          "most frequent words", true}, cmdWordfreq},
        {{"count",     "$ count [-i] <needle> <file…|-e TEXT>",        "count occurrences of a text", true}, cmdCount},
        {{"rot13",     "$ rot13 <file…|-e TEXT>",                      "ROT13 cipher", true}, cmdRot13},
        {{"md5",       "$ md5 <file…|-e TEXT>",                        "MD5 checksum", true}, cmdMd5},
        {{"sha1",      "$ sha1 <file…|-e TEXT>",                       "SHA-1 checksum", true}, cmdSha1},
        {{"urlencode", "$ urlencode <file…|-e TEXT>",                  "percent-encode text for URLs", true}, cmdUrlencode},
        {{"urldecode", "$ urldecode <file…|-e TEXT>",                  "decode percent-encoded text", true}, cmdUrldecode},
        {{"hex",       "$ hex <file…|-e TEXT>",                        "text/bytes to a hex string", true}, cmdHex},
        {{"unhex",     "$ unhex <file…|-e HEX>",                       "hex string back to text", true}, cmdUnhex},
        {{"ascii",     "$ ascii [chars…]",                             "code points of characters (no args: ASCII table)", false}, cmdAscii},
        {{"conv",      "$ conv <number> [-f BASE] [-t BASE]",          "convert between bases 2..36 (0x/0b/0o auto)", false}, cmdConv},
        {{"calc",      "$ calc <expression>",                          "calculator: + - * / % ^ ( ) sqrt sin cos ln log abs pi e …", false}, cmdCalc},
        {{"factor",    "$ factor <number…>",                           "prime factorization", false}, cmdFactor},
        {{"primes",    "$ primes A:B",                                 "list primes in a range (up to 10,000,000)", false}, cmdPrimes},
        {{"fib",       "$ fib [count]",                                "Fibonacci numbers (default 10, max 90)", false}, cmdFib},
        {{"gcd",       "$ gcd [-l] <a> <b…>",                          "greatest common divisor (-l: least common multiple)", false}, cmdGcd},
        {{"rand",      "$ rand [A:B] [-n COUNT]",                      "random integers (default 0:99)", false}, cmdRand},
        {{"uuid",      "$ uuid [-n COUNT]",                            "random UUID v4", false}, cmdUuid},
        {{"timestamp", "$ timestamp [EPOCH | \"YYYY-MM-DD [HH:MM:SS]\"] [-u]", "epoch <-> date conversion (no args: now)", false}, cmdTimestamp},
        {{"cal",       "$ cal [month] [year]  |  $ cal <year>",        "print a calendar", false}, cmdCal},
        {{"datediff",  "$ datediff <YYYY-MM-DD> <YYYY-MM-DD>",         "days between two dates", false}, cmdDatediff},
        {{"id",        "$ id",                                         "user and group ids", false}, cmdId},
        {{"ps",        "$ ps",                                         "list processes (from /proc)", false}, cmdPs},
        {{"cpuinfo",   "$ cpuinfo",                                    "CPU model and core count", false}, cmdCpuinfo},
        {{"mounts",    "$ mounts [-a]",                                "mounted file systems", false}, cmdMounts},
        {{"printenv",  "$ printenv <NAME…>",                           "print the value of environment variables", false}, cmdPrintenv},
        {{"yes",       "$ yes [text…] [-n COUNT]",                     "repeat a line COUNT times (default 10)", false}, cmdYes},
        {{"true",      "$ true",                                       "do nothing, successfully (exit 0)", false}, cmdTrue},
        {{"false",     "$ false",                                      "do nothing, unsuccessfully (exit 1)", false}, cmdFalse},
        {{"term",      "$ term",                                       "terminal size and TERM / COLORTERM / LANG", false}, cmdTerm},
        {{"path",      "$ path [-c]",                                  "PATH entries, one per line (-c: check they exist)", false}, cmdPath},
        {{"numfmt",    "$ numfmt <bytes…>  |  $ numfmt -r <1.5G…>",    "bytes <-> human readable sizes", false}, cmdNumfmt},
    };
    return r;
}

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin
