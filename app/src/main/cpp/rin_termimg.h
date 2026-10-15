// ============================================================================
//  rin_termimg.h — طباعة الصور كنص في الطرفية (terminal image rendering)
// ----------------------------------------------------------------------------
//  مفكّك صور مستقل بلا اعتماديات (غير zlib): PNG (كل أنماط الألوان وأعماق البتات + Adam7 + tRNS)،
//  PPM/PGM (P5/P6/P3)، BMP (8/24/32 بت غير مضغوط). لغيرها (JPEG/GIF/WebP...) يُستعمل ImageMagick
//  (convert/magick) إن وُجد على النظام.
//  أنماط العرض: blocks (نصف كتلة ▀ بألوان 24-بت) · ansi256 · shade ( ░▒▓█ ) · ascii · braille.
//  يُستعمل من native `imageText(...)` ومن `print.image(path, mode=...)`. docs/print-image-terminal.md
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <zlib.h>
#ifdef _WIN32
#include <io.h>
#define rin_ti_isatty _isatty
#else
#include <unistd.h>
#define rin_ti_isatty isatty
#endif

namespace rin {
namespace termimg {

struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // RGBA8، w*h*4
};

// ---------------------------------------------------------------- سياسة الألوان (نفس سياسة سمات الخط)
inline bool colorAllowed() {
    const char* force = std::getenv("FORCE_COLOR");
    if (force && *force && std::strcmp(force, "0") != 0) return true;
    if (std::getenv("NO_COLOR")) return false;
    return rin_ti_isatty(1) != 0;
}

// ---------------------------------------------------------------- PNG
namespace detail {
inline uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

inline bool inflateAll(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t expect) {
    out.assign(expect, 0);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return false;
    zs.next_in = const_cast<uint8_t*>(in.data());
    zs.avail_in = static_cast<uInt>(in.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return r == Z_STREAM_END || (r == Z_OK) || (r == Z_BUF_ERROR && zs.avail_out == 0);
}

inline int paeth(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}
}  // namespace detail

inline bool decodePng(const std::vector<uint8_t>& f, Img& img, std::string& err) {
    using namespace detail;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (f.size() < 33 || std::memcmp(f.data(), sig, 8) != 0) { err = "not a PNG file"; return false; }
    uint32_t W = 0, H = 0; int depth = 0, ctype = 0, interlace = 0;
    std::vector<uint8_t> idat, plte, trns;
    size_t pos = 8; bool gotHdr = false;
    while (pos + 12 <= f.size()) {
        uint32_t len = be32(&f[pos]);
        if (pos + 12 + len > f.size()) break;
        const uint8_t* ty = &f[pos + 4];
        const uint8_t* d = &f[pos + 8];
        if (!std::memcmp(ty, "IHDR", 4) && len >= 13) {
            W = be32(d); H = be32(d + 4); depth = d[8]; ctype = d[9]; interlace = d[12]; gotHdr = true;
        } else if (!std::memcmp(ty, "PLTE", 4)) plte.assign(d, d + len);
        else if (!std::memcmp(ty, "tRNS", 4)) trns.assign(d, d + len);
        else if (!std::memcmp(ty, "IDAT", 4)) idat.insert(idat.end(), d, d + len);
        else if (!std::memcmp(ty, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (!gotHdr || W == 0 || H == 0) { err = "corrupt PNG header"; return false; }
    if ((uint64_t)W * H > 64ull * 1024 * 1024) { err = "image too large"; return false; }
    int ch;
    switch (ctype) { case 0: ch = 1; break; case 2: ch = 3; break; case 3: ch = 1; break; case 4: ch = 2; break; case 6: ch = 4; break; default: err = "unsupported PNG color type"; return false; }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) { err = "unsupported PNG bit depth"; return false; }
    if (ctype == 3 && plte.empty()) { err = "PNG palette missing"; return false; }

    struct Pass { int xs, ys, dx, dy; };
    static const Pass adam[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass plain[1] = {{0, 0, 1, 1}};
    const Pass* passes = interlace ? adam : plain;
    int np = interlace ? 7 : 1;
    size_t expect = 0;
    for (int p = 0; p < np; p++) {
        int pw = (W - passes[p].xs + passes[p].dx - 1) / passes[p].dx, ph = (H - passes[p].ys + passes[p].dy - 1) / passes[p].dy;
        if (W <= (uint32_t)passes[p].xs || H <= (uint32_t)passes[p].ys) continue;
        expect += (size_t)ph * (1 + ((size_t)pw * ch * depth + 7) / 8);
    }
    std::vector<uint8_t> raw;
    if (!inflateAll(idat, raw, expect)) { err = "corrupt PNG data"; return false; }

    img.w = (int)W; img.h = (int)H; img.px.assign((size_t)W * H * 4, 255);
    const int bpp = std::max(1, ch * depth / 8);
    size_t off = 0;
    const int maxv = (1 << std::min(depth, 8)) - 1;
    for (int p = 0; p < np; p++) {
        if (W <= (uint32_t)passes[p].xs || H <= (uint32_t)passes[p].ys) continue;
        int pw = (W - passes[p].xs + passes[p].dx - 1) / passes[p].dx, ph = (H - passes[p].ys + passes[p].dy - 1) / passes[p].dy;
        size_t rb = ((size_t)pw * ch * depth + 7) / 8;
        std::vector<uint8_t> prev(rb, 0), cur(rb, 0);
        for (int y = 0; y < ph; y++) {
            if (off + 1 + rb > raw.size()) { err = "truncated PNG data"; return false; }
            int ft = raw[off++];
            std::memcpy(cur.data(), &raw[off], rb); off += rb;
            for (size_t i = 0; i < rb; i++) {
                int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0;
                switch (ft) {
                    case 1: cur[i] = uint8_t(cur[i] + a); break;
                    case 2: cur[i] = uint8_t(cur[i] + b); break;
                    case 3: cur[i] = uint8_t(cur[i] + ((a + b) >> 1)); break;
                    case 4: cur[i] = uint8_t(cur[i] + paeth(a, b, c)); break;
                    default: break;
                }
            }
            auto sample = [&](int x, int c) -> int {          // عينة مقيسة إلى 0..maxv (16 بت → أعلى بايت)
                size_t idx = (size_t)x * ch + c;
                if (depth == 8) return cur[idx];
                if (depth == 16) return cur[idx * 2];
                size_t bit = idx * depth;
                return (cur[bit >> 3] >> (8 - depth - (bit & 7))) & maxv;
            };
            int oy = passes[p].ys + y * passes[p].dy;
            for (int x = 0; x < pw; x++) {
                int ox = passes[p].xs + x * passes[p].dx;
                uint8_t* o = &img.px[((size_t)oy * W + ox) * 4];
                int r, g, b, a = 255;
                auto sc = [&](int v) { return depth >= 8 ? v : v * 255 / maxv; };
                if (ctype == 3) {
                    size_t pi = (size_t)sample(x, 0);
                    r = pi * 3 + 2 < plte.size() ? plte[pi * 3] : 0; g = pi * 3 + 2 < plte.size() ? plte[pi * 3 + 1] : 0; b = pi * 3 + 2 < plte.size() ? plte[pi * 3 + 2] : 0;
                    if (pi < trns.size()) a = trns[pi];
                } else if (ctype == 0 || ctype == 4) {
                    int v = sc(sample(x, 0)); r = g = b = v;
                    if (ctype == 4) a = sc(sample(x, 1));
                    else if (trns.size() >= 2) { int tv = (trns[0] << 8) | trns[1]; int raw0 = depth == 16 ? ((cur[(size_t)x * 2] << 8) | cur[(size_t)x * 2 + 1]) : sample(x, 0); if (raw0 == tv) a = 0; }
                } else {
                    r = sc(sample(x, 0)); g = sc(sample(x, 1)); b = sc(sample(x, 2));
                    if (ctype == 6) a = sc(sample(x, 3));
                }
                o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b; o[3] = (uint8_t)a;
            }
            prev = cur;
        }
    }
    return true;
}

// ---------------------------------------------------------------- PPM / PGM
inline bool decodePnm(const std::vector<uint8_t>& f, Img& img, std::string& err) {
    if (f.size() < 3 || f[0] != 'P' || (f[1] != '5' && f[1] != '6' && f[1] != '3' && f[1] != '2')) { err = "not a PNM file"; return false; }
    size_t pos = 2;
    auto skipWs = [&]() { while (pos < f.size()) { if (f[pos] == '#') { while (pos < f.size() && f[pos] != '\n') pos++; } else if (std::isspace(f[pos])) pos++; else break; } };
    auto num = [&]() -> long { skipWs(); long v = 0; bool any = false; while (pos < f.size() && std::isdigit(f[pos])) { v = v * 10 + (f[pos++] - '0'); any = true; } return any ? v : -1; };
    long w = num(), h = num(), mx = num();
    if (w <= 0 || h <= 0 || mx <= 0 || mx > 65535 || (uint64_t)w * h > 64ull * 1024 * 1024) { err = "corrupt PNM header"; return false; }
    bool color = f[1] == '6' || f[1] == '3', ascii = f[1] == '3' || f[1] == '2';
    int bps = mx > 255 ? 2 : 1;
    if (!ascii) pos++;  // مسافة واحدة بعد maxval
    img.w = (int)w; img.h = (int)h; img.px.assign((size_t)w * h * 4, 255);
    for (long i = 0; i < w * h; i++) {
        int c[3];
        for (int k = 0; k < (color ? 3 : 1); k++) {
            long v;
            if (ascii) { v = num(); if (v < 0) { err = "truncated PNM data"; return false; } }
            else {
                if (pos + bps > f.size()) { err = "truncated PNM data"; return false; }
                v = bps == 2 ? (f[pos] << 8 | f[pos + 1]) : f[pos]; pos += bps;
            }
            c[k] = (int)(v * 255 / mx);
        }
        uint8_t* o = &img.px[(size_t)i * 4];
        o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[color ? 1 : 0]; o[2] = (uint8_t)c[color ? 2 : 0];
    }
    return true;
}

// ---------------------------------------------------------------- BMP
inline bool decodeBmp(const std::vector<uint8_t>& f, Img& img, std::string& err) {
    auto le16 = [&](size_t o) { return f[o] | (f[o + 1] << 8); };
    auto le32 = [&](size_t o) { return (uint32_t)f[o] | ((uint32_t)f[o + 1] << 8) | ((uint32_t)f[o + 2] << 16) | ((uint32_t)f[o + 3] << 24); };
    if (f.size() < 54 || f[0] != 'B' || f[1] != 'M') { err = "not a BMP file"; return false; }
    uint32_t dataOff = le32(10), hdr = le32(14);
    int32_t w = (int32_t)le32(18), h = (int32_t)le32(22);
    int bits = le16(28); uint32_t comp = le32(30);
    bool topDown = h < 0; if (topDown) h = -h;
    if (w <= 0 || h <= 0 || (uint64_t)w * h > 64ull * 1024 * 1024 || comp != 0 || (bits != 8 && bits != 24 && bits != 32)) { err = "unsupported BMP (need uncompressed 8/24/32-bit)"; return false; }
    size_t rb = (((size_t)w * bits + 31) / 32) * 4;
    if (dataOff + rb * h > f.size()) { err = "truncated BMP data"; return false; }
    img.w = w; img.h = h; img.px.assign((size_t)w * h * 4, 255);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = &f[dataOff + rb * (topDown ? y : h - 1 - y)];
        for (int x = 0; x < w; x++) {
            uint8_t* o = &img.px[((size_t)y * w + x) * 4];
            if (bits == 8) { size_t po = 14 + hdr + (size_t)row[x] * 4; if (po + 2 < f.size()) { o[0] = f[po + 2]; o[1] = f[po + 1]; o[2] = f[po]; } }
            else { o[0] = row[x * (bits / 8) + 2]; o[1] = row[x * (bits / 8) + 1]; o[2] = row[x * (bits / 8)]; if (bits == 32) o[3] = row[x * 4 + 3] ? row[x * 4 + 3] : 255; }
        }
    }
    return true;
}

// ---------------------------------------------------------------- فك أي صيغة
inline bool decodeFile(const std::string& path, Img& img, std::string& err) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { err = "cannot open file"; return false; }
    std::streamsize n = in.tellg();
    if (n <= 0 || n > 256ll * 1024 * 1024) { err = "file is empty or too large"; return false; }
    in.seekg(0);
    std::vector<uint8_t> f((size_t)n);
    in.read(reinterpret_cast<char*>(f.data()), n);
    std::string e;
    if (f.size() > 8 && f[0] == 137 && f[1] == 'P') return decodePng(f, img, err);
    if (f.size() > 2 && f[0] == 'P' && f[1] >= '2' && f[1] <= '6') return decodePnm(f, img, err);
    if (f.size() > 2 && f[0] == 'B' && f[1] == 'M' && decodeBmp(f, img, e)) return true;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // JPEG/GIF/WebP/...: ImageMagick إن وُجد
    std::string q = "'";
    for (char c : path) { if (c == '\'') q += "'\\''"; else q += c; }
    q += "'";
    for (const char* tool : {"convert", "magick"}) {
        std::string cmd = std::string(tool) + " " + q + "[0] -background black -alpha remove -depth 8 ppm:- 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) continue;
        std::vector<uint8_t> out; uint8_t buf[65536]; size_t r;
        while ((r = fread(buf, 1, sizeof buf, p)) > 0) { out.insert(out.end(), buf, buf + r); if (out.size() > 256u * 1024 * 1024) break; }
        int rc = pclose(p);
        if (rc == 0 && out.size() > 3 && decodePnm(out, img, e)) return true;
    }
    err = "unsupported image format (PNG/PPM/BMP are built in; install ImageMagick for JPEG/GIF/WebP)";
#else
    err = "unsupported image format (PNG/PPM/BMP only on this platform)";
#endif
    return false;
}

// ---------------------------------------------------------------- إعادة التحجيم (متوسط صندوقي) فوق خلفية سوداء
struct Rgb { float r, g, b; };
inline std::vector<Rgb> resample(const Img& s, int dw, int dh) {
    std::vector<Rgb> out((size_t)dw * dh);
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((int64_t)y * s.h / dh), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * s.h / dh));
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((int64_t)x * s.w / dw), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * s.w / dw));
            double r = 0, g = 0, b = 0; int n = 0;
            for (int yy = y0; yy < y1 && yy < s.h; yy++)
                for (int xx = x0; xx < x1 && xx < s.w; xx++) {
                    const uint8_t* p = &s.px[((size_t)yy * s.w + xx) * 4];
                    double a = p[3] / 255.0;
                    r += p[0] * a; g += p[1] * a; b += p[2] * a; n++;
                }
            if (n == 0) n = 1;
            out[(size_t)y * dw + x] = {(float)(r / n), (float)(g / n), (float)(b / n)};
        }
    }
    return out;
}
inline float luma(const Rgb& c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f; }
inline int c8(float v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)(v + 0.5f)); }
inline int to256(const Rgb& c) {
    int r = c8(c.r), g = c8(c.g), b = c8(c.b);
    if (std::abs(r - g) < 8 && std::abs(g - b) < 8) {            // تدرّج رمادي: 232..255
        int v = (r + g + b) / 3;
        if (v < 8) return 16; if (v > 248) return 231;
        return 232 + (v - 8) * 24 / 240;
    }
    return 16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + (b * 5 / 255);
}

// ---------------------------------------------------------------- العرض
// mode: blocks | ansi256 | shade | ascii | braille . color: -1 = تلقائي (طرفية/FORCE_COLOR)، 0 = بلا، 1 = بألوان
inline std::string render(const Img& img, int cols, std::string mode, bool invert, int color) {
    if (cols < 1) cols = 1;
    if (cols > 400) cols = 400;
    bool useColor = color < 0 ? colorAllowed() : color > 0;
    if ((mode == "blocks" || mode == "ansi256") && !useColor) mode = "shade";   // بلا ألوان: تدرّج رمادي عادي
    std::string out;
    auto lumaCh = [&](float l) { return invert ? 1.f - l : l; };
    if (mode == "blocks" || mode == "ansi256") {
        int dh = std::max(2, (int)std::lround((double)img.h * cols / img.w)); dh += dh & 1;
        auto g = resample(img, cols, dh);
        bool t256 = mode == "ansi256";
        for (int y = 0; y < dh; y += 2) {
            int lastF = -1, lastB = -1;
            for (int x = 0; x < cols; x++) {
                const Rgb &t = g[(size_t)y * cols + x], &b = g[(size_t)(y + 1) * cols + x];
                char buf[96];
                if (t256) {
                    int f = to256(t), bg = to256(b);
                    if (f != lastF) { std::snprintf(buf, sizeof buf, "\x1b[38;5;%dm", f); out += buf; lastF = f; }
                    if (bg != lastB) { std::snprintf(buf, sizeof buf, "\x1b[48;5;%dm", bg); out += buf; lastB = bg; }
                } else {
                    int f = (c8(t.r) << 16) | (c8(t.g) << 8) | c8(t.b), bg = (c8(b.r) << 16) | (c8(b.g) << 8) | c8(b.b);
                    if (f != lastF) { std::snprintf(buf, sizeof buf, "\x1b[38;2;%d;%d;%dm", c8(t.r), c8(t.g), c8(t.b)); out += buf; lastF = f; }
                    if (bg != lastB) { std::snprintf(buf, sizeof buf, "\x1b[48;2;%d;%d;%dm", c8(b.r), c8(b.g), c8(b.b)); out += buf; lastB = bg; }
                }
                out += "\xE2\x96\x80";   // ▀
            }
            out += "\x1b[0m";
            if (y + 2 < dh) out += "\n";
        }
        return out;
    }
    if (mode == "braille") {
        int dw = cols * 2;
        int dh = std::max(4, (int)std::lround((double)img.h * dw / img.w)); dh = (dh + 3) / 4 * 4;
        auto g = resample(img, dw, dh);
        double mean = 0; for (auto& c : g) mean += luma(c); mean /= g.size();
        static const int bit[2][4] = {{1, 2, 4, 64}, {8, 16, 32, 128}};
        for (int y = 0; y < dh; y += 4) {
            for (int x = 0; x < dw; x += 2) {
                int bits = 0;
                for (int dx = 0; dx < 2; dx++) for (int dy = 0; dy < 4; dy++) {
                    bool on = luma(g[(size_t)(y + dy) * dw + x + dx]) > mean;
                    if (invert) on = !on;
                    if (on) bits |= bit[dx][dy];
                }
                uint32_t cp = 0x2800 + bits;
                out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
            }
            if (y + 4 < dh) out += "\n";
        }
        return out;
    }
    // shade / ascii: خلية الحرف ≈ ضعف ارتفاع عرضها
    bool shade = mode == "shade";
    int dh = std::max(1, (int)std::lround((double)img.h * cols / img.w * 0.5));
    auto g = resample(img, cols, dh);
    static const char* ramp = " .:-=+*#%@";
    static const char* sh[5] = {" ", "\xE2\x96\x91", "\xE2\x96\x92", "\xE2\x96\x93", "\xE2\x96\x88"};  // ░▒▓█
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < cols; x++) {
            float l = lumaCh(luma(g[(size_t)y * cols + x]));
            if (shade) out += sh[std::min(4, (int)(l * 5))];
            else out += ramp[std::min(9, (int)(l * 10))];
        }
        if (y + 1 < dh) out += "\n";
    }
    return out;
}

}  // namespace termimg
}  // namespace rin
