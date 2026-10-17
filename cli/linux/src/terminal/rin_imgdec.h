// ============================================================================
//  rin_imgdec.h — مفكّك صور مدمج بلا ImageMagick (header-only، يعتمد على zlib فقط)
// ============================================================================
//  يدعم:  PNG (كل أنماط الألوان والأعماق 1–16 بت، Adam7، tRNS)
//         JPEG (Baseline + Progressive، تكرار/تقليل العينات، فواصل إعادة البدء، تدوير EXIF)
//         GIF (الإطار الأول، شفافية، interlace)    BMP (1/4/8/16/24/32 بت بلا ضغط)
//  لا يدعم: WebP / TIFF / JPEG حسابي أو lossless / CMYK — يعيد خطأً واضحاً ليُجرَّب ImageMagick إن وُجد.
//
//  الحدود (حماية الذاكرة على الهاتف): PNG/GIF/BMP حتى 25 ميغابكسل. JPEG أكبر من 12 ميغابكسل يُفكّ بمقياس 1/8
//  (DC فقط) فتُقرأ حتى صور الكاميرا 100+ ميغابكسل بذاكرة صغيرة.
//  يُستعمل من rin_terminal.cpp (:img / rin img) على لينكس وأندرويد بنفس الكود.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>

namespace rin {
namespace imgdec {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // 8 بت لكل قناة، ألفا مباشرة (غير مضروبة)
    int orientation = 1;         // EXIF 1..8 (يُطبَّق في decodeFile)
};

constexpr long long kMaxPixels = 25LL * 1000 * 1000;          // PNG/GIF/BMP
constexpr long long kJpegFullMaxPixels = 12LL * 1000 * 1000;  // فوقه: مقياس 1/8
constexpr long long kJpegMaxPixels = 1000LL * 1000 * 1000;    // سقف مطلق لأبعاد JPEG
constexpr size_t kMaxFileBytes = 256u * 1024u * 1024u;

namespace detail {

inline uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
inline uint32_t be16(const uint8_t* p) { return (uint32_t(p[0]) << 8) | p[1]; }
inline uint32_t le16(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }
inline uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

inline bool allocImage(Image& im, long long w, long long h, long long maxPx, std::string& err) {
    if (w <= 0 || h <= 0) { err = "invalid image size"; return false; }
    if (w * h > maxPx) { err = "image too large (max " + std::to_string(maxPx / 1000000) + " megapixels)"; return false; }
    im.w = static_cast<int>(w); im.h = static_cast<int>(h);
    im.rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    return true;
}

// ----------------------------------------------------------------------------- PNG
inline bool pngUnfilter(std::vector<uint8_t>& raw, size_t off, size_t rowBytes, size_t rows, size_t bpp, std::vector<uint8_t>& out) {
    // raw[off..]: rows × (1 + rowBytes). out: rows × rowBytes.
    out.assign(rowBytes * rows, 0);
    for (size_t y = 0; y < rows; ++y) {
        const uint8_t* in = &raw[off + y * (rowBytes + 1)];
        uint8_t f = in[0];
        uint8_t* cur = &out[y * rowBytes];
        const uint8_t* prev = y ? &out[(y - 1) * rowBytes] : nullptr;
        in++;
        switch (f) {
            case 0: std::memcpy(cur, in, rowBytes); break;
            case 1: for (size_t i = 0; i < rowBytes; ++i) cur[i] = uint8_t(in[i] + (i >= bpp ? cur[i - bpp] : 0)); break;
            case 2: for (size_t i = 0; i < rowBytes; ++i) cur[i] = uint8_t(in[i] + (prev ? prev[i] : 0)); break;
            case 3: for (size_t i = 0; i < rowBytes; ++i) {
                        int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0;
                        cur[i] = uint8_t(in[i] + ((a + b) >> 1));
                    } break;
            case 4: for (size_t i = 0; i < rowBytes; ++i) {
                        int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                        int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                        int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                        cur[i] = uint8_t(in[i] + pr);
                    } break;
            default: return false;
        }
    }
    return true;
}

}  // namespace detail

inline bool decodePng(const uint8_t* d, size_t n, Image& out, std::string& err) {
    using namespace detail;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n < 8 || std::memcmp(d, sig, 8) != 0) { err = "not a PNG"; return false; }
    size_t pos = 8;
    uint32_t W = 0, H = 0;
    int depth = 0, ctype = -1, interlace = 0;
    bool haveHdr = false;
    std::vector<uint8_t> idat, pal, trns;
    uint32_t key[3] = {0, 0, 0};
    bool haveKey = false;
    while (pos + 12 <= n) {
        uint32_t len = be32(d + pos);
        const uint8_t* ty = d + pos + 4;
        if (len > n - pos - 12) break;   // مقطوع
        const uint8_t* cd = d + pos + 8;
        if (!std::memcmp(ty, "IHDR", 4) && len >= 13) {
            W = be32(cd); H = be32(cd + 4); depth = cd[8]; ctype = cd[9]; interlace = cd[12]; haveHdr = true;
        } else if (!std::memcmp(ty, "PLTE", 4)) pal.assign(cd, cd + len);
        else if (!std::memcmp(ty, "tRNS", 4)) {
            if (ctype == 3) trns.assign(cd, cd + len);
            else if (ctype == 0 && len >= 2) { key[0] = be16(cd); haveKey = true; }
            else if (ctype == 2 && len >= 6) { key[0] = be16(cd); key[1] = be16(cd + 2); key[2] = be16(cd + 4); haveKey = true; }
        } else if (!std::memcmp(ty, "IDAT", 4)) idat.insert(idat.end(), cd, cd + len);
        else if (!std::memcmp(ty, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (!haveHdr) { err = "PNG: missing IHDR"; return false; }
    int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    bool depthOk = (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) &&
                   (ctype == 0 || (ctype == 3 && depth <= 8) || ((ctype == 2 || ctype == 4 || ctype == 6) && depth >= 8));
    if (!channels || !depthOk || interlace > 1) { err = "PNG: unsupported format"; return false; }
    if (ctype == 3 && pal.size() < 3) { err = "PNG: missing palette"; return false; }
    if (!allocImage(out, W, H, kMaxPixels, err)) return false;

    const size_t bits = size_t(channels) * size_t(depth);
    const size_t bpp = std::max<size_t>(1, bits / 8);
    struct Pass { size_t x0, y0, dx, dy; };
    static const Pass adam[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass flat[1] = {{0, 0, 1, 1}};
    const Pass* passes = interlace ? adam : flat;
    int nPasses = interlace ? 7 : 1;

    // الحجم المتوقع بعد فك الضغط
    size_t expect = 0;
    for (int p = 0; p < nPasses; ++p) {
        size_t pw = W > passes[p].x0 ? (W - passes[p].x0 + passes[p].dx - 1) / passes[p].dx : 0;
        size_t ph = H > passes[p].y0 ? (H - passes[p].y0 + passes[p].dy - 1) / passes[p].dy : 0;
        if (pw && ph) expect += ph * (1 + (pw * bits + 7) / 8);
    }
    std::vector<uint8_t> raw(expect);
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) { err = "PNG: zlib init failed"; return false; }
    zs.next_in = idat.data(); zs.avail_in = static_cast<uInt>(idat.size());
    zs.next_out = raw.data(); zs.avail_out = static_cast<uInt>(raw.size());
    int zr = inflate(&zs, Z_FINISH);
    size_t produced = zs.total_out;
    inflateEnd(&zs);
    if ((zr != Z_STREAM_END && zr != Z_BUF_ERROR && zr != Z_OK) || produced < expect) { err = "PNG: damaged or truncated image data"; return false; }

    size_t off = 0;
    std::vector<uint8_t> rows;
    for (int p = 0; p < nPasses; ++p) {
        size_t pw = W > passes[p].x0 ? (W - passes[p].x0 + passes[p].dx - 1) / passes[p].dx : 0;
        size_t ph = H > passes[p].y0 ? (H - passes[p].y0 + passes[p].dy - 1) / passes[p].dy : 0;
        if (!pw || !ph) continue;
        size_t rowBytes = (pw * bits + 7) / 8;
        if (!pngUnfilter(raw, off, rowBytes, ph, bpp, rows)) { err = "PNG: bad filter"; return false; }
        off += ph * (rowBytes + 1);
        for (size_t y = 0; y < ph; ++y) {
            const uint8_t* row = &rows[y * rowBytes];
            for (size_t x = 0; x < pw; ++x) {
                auto sample = [&](int c) -> uint32_t {   // القيمة الخام (16 بت كاملة لعمق 16)
                    size_t i = x * size_t(channels) + size_t(c);
                    if (depth == 8) return row[i];
                    if (depth == 16) return be16(row + i * 2);
                    size_t bit = i * size_t(depth);
                    int shift = 8 - depth - int(bit % 8);
                    return (row[bit / 8] >> shift) & ((1u << depth) - 1);
                };
                auto to8 = [&](uint32_t v) -> uint8_t {
                    if (depth == 16) return uint8_t(v >> 8);
                    if (depth == 8) return uint8_t(v);
                    return uint8_t(v * 255 / ((1u << depth) - 1));
                };
                uint8_t r, g, b, a = 255;
                if (ctype == 3) {
                    uint32_t idx = sample(0);
                    if (idx * 3 + 2 >= pal.size()) { r = g = b = 0; }
                    else { r = pal[idx * 3]; g = pal[idx * 3 + 1]; b = pal[idx * 3 + 2]; }
                    if (idx < trns.size()) a = trns[idx];
                } else if (ctype == 0 || ctype == 4) {
                    uint32_t v = sample(0);
                    r = g = b = to8(v);
                    if (ctype == 4) a = to8(sample(1));
                    else if (haveKey && v == key[0]) a = 0;
                } else {
                    uint32_t rv = sample(0), gv = sample(1), bv = sample(2);
                    r = to8(rv); g = to8(gv); b = to8(bv);
                    if (ctype == 6) a = to8(sample(3));
                    else if (haveKey && rv == key[0] && gv == key[1] && bv == key[2]) a = 0;
                }
                size_t dx = passes[p].x0 + x * passes[p].dx, dy = passes[p].y0 + y * passes[p].dy;
                uint8_t* o = &out.rgba[(dy * W + dx) * 4];
                o[0] = r; o[1] = g; o[2] = b; o[3] = a;
            }
        }
    }
    return true;
}

// ----------------------------------------------------------------------------- GIF
inline bool decodeGif(const uint8_t* d, size_t n, Image& out, std::string& err) {
    using namespace detail;
    if (n < 13 || std::memcmp(d, "GIF8", 4) != 0) { err = "not a GIF"; return false; }
    int sw = int(le16(d + 6)), sh = int(le16(d + 8));
    uint8_t flags = d[10];
    size_t pos = 13;
    std::vector<uint8_t> gpal;
    if (flags & 0x80) {
        size_t sz = size_t(3) << ((flags & 7) + 1);
        if (pos + sz > n) { err = "GIF: truncated"; return false; }
        gpal.assign(d + pos, d + pos + sz); pos += sz;
    }
    int transparent = -1;
    while (pos < n) {
        uint8_t b = d[pos++];
        if (b == 0x3B) break;
        if (b == 0x21) {   // امتداد
            if (pos >= n) break;
            uint8_t label = d[pos++];
            if (label == 0xF9 && pos + 5 < n && d[pos] >= 4) { if (d[pos + 1] & 1) transparent = d[pos + 4]; }
            while (pos < n && d[pos]) pos += size_t(d[pos]) + 1;
            pos++;
            continue;
        }
        if (b != 0x2C) { err = "GIF: bad block"; return false; }
        if (pos + 9 > n) { err = "GIF: truncated"; return false; }
        int fx = int(le16(d + pos)), fy = int(le16(d + pos + 2)), fw = int(le16(d + pos + 4)), fh = int(le16(d + pos + 6));
        uint8_t fl = d[pos + 8];
        pos += 9;
        std::vector<uint8_t> lpal;
        if (fl & 0x80) {
            size_t sz = size_t(3) << ((fl & 7) + 1);
            if (pos + sz > n) { err = "GIF: truncated"; return false; }
            lpal.assign(d + pos, d + pos + sz); pos += sz;
        }
        const std::vector<uint8_t>& pal = lpal.empty() ? gpal : lpal;
        if (pos >= n) { err = "GIF: truncated"; return false; }
        int minCode = d[pos++];
        if (minCode < 2 || minCode > 11) { err = "GIF: bad LZW size"; return false; }
        std::vector<uint8_t> data;
        while (pos < n && d[pos]) {
            size_t len = d[pos];
            if (pos + 1 + len > n) len = n - pos - 1;
            data.insert(data.end(), d + pos + 1, d + pos + 1 + len);
            pos += 1 + len;
        }
        if (sw <= 0 || sh <= 0) { sw = fx + fw; sh = fy + fh; }
        if (fw <= 0 || fh <= 0) { err = "GIF: empty frame"; return false; }
        if (!allocImage(out, sw, sh, kMaxPixels, err)) return false;
        // LZW
        const int clear = 1 << minCode, eoi = clear + 1;
        int codeSize = minCode + 1, next = eoi + 1, old = -1;
        std::vector<int16_t> prefix(4096, 0);
        std::vector<uint8_t> suffix(4096, 0), stack;
        stack.reserve(4097);
        uint32_t bitbuf = 0; int nbits = 0; size_t dp = 0;
        std::vector<uint8_t> idxs;
        idxs.reserve(size_t(fw) * size_t(fh));
        uint8_t first = 0;
        const size_t total = size_t(fw) * size_t(fh);
        while (idxs.size() < total) {
            while (nbits < codeSize && dp < data.size()) { bitbuf |= uint32_t(data[dp++]) << nbits; nbits += 8; }
            if (nbits < codeSize) break;
            int code = int(bitbuf & ((1u << codeSize) - 1));
            bitbuf >>= codeSize; nbits -= codeSize;
            if (code == clear) { codeSize = minCode + 1; next = eoi + 1; old = -1; continue; }
            if (code == eoi) break;
            if (old == -1) {
                if (code >= clear) { err = "GIF: corrupt data"; return false; }
                idxs.push_back(uint8_t(code)); old = code; first = uint8_t(code); continue;
            }
            int in = code;
            stack.clear();
            if (code >= next) {
                if (code > next) { err = "GIF: corrupt data"; return false; }
                stack.push_back(first); code = old;
            }
            while (code >= clear) { if (code >= 4096) { err = "GIF: corrupt data"; return false; } stack.push_back(suffix[code]); code = prefix[code]; }
            stack.push_back(uint8_t(code)); first = uint8_t(code);
            if (next < 4096) {
                prefix[next] = int16_t(old); suffix[next] = first; ++next;
                if (next == (1 << codeSize) && codeSize < 12) ++codeSize;
            }
            old = in;
            while (!stack.empty() && idxs.size() < total) { idxs.push_back(stack.back()); stack.pop_back(); }
        }
        idxs.resize(total, 0);
        // ترتيب الصفوف (interlace)
        std::vector<int> rowOf(static_cast<size_t>(fh), 0);
        if (fl & 0x40) {
            int r = 0;
            static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
            for (int pass = 0; pass < 4; ++pass) for (int y = start[pass]; y < fh; y += step[pass]) rowOf[size_t(r++)] = y;
        } else for (int y = 0; y < fh; ++y) rowOf[size_t(y)] = y;
        for (int sy = 0; sy < fh; ++sy) {
            int y = fy + rowOf[size_t(sy)];
            if (y < 0 || y >= sh) continue;
            for (int x = 0; x < fw; ++x) {
                int px = fx + x;
                if (px < 0 || px >= sw) continue;
                uint8_t idx = idxs[size_t(sy) * size_t(fw) + size_t(x)];
                if (idx == transparent) continue;
                uint8_t* o = &out.rgba[(size_t(y) * size_t(sw) + size_t(px)) * 4];
                if (size_t(idx) * 3 + 2 < pal.size()) { o[0] = pal[size_t(idx) * 3]; o[1] = pal[size_t(idx) * 3 + 1]; o[2] = pal[size_t(idx) * 3 + 2]; }
                o[3] = 255;
            }
        }
        return true;   // الإطار الأول فقط
    }
    err = "GIF: no image data";
    return false;
}

// ----------------------------------------------------------------------------- BMP
inline bool decodeBmp(const uint8_t* d, size_t n, Image& out, std::string& err) {
    using namespace detail;
    if (n < 26 || d[0] != 'B' || d[1] != 'M') { err = "not a BMP"; return false; }
    uint32_t dataOff = le32(d + 10), dib = le32(d + 14);
    long long w, h; int bpp; uint32_t comp = 0, clrUsed = 0;
    if (dib == 12) { w = int(le16(d + 18)); h = int(le16(d + 20)); bpp = int(le16(d + 24)); }
    else if (dib >= 40 && n >= 14 + 40) {
        w = int32_t(le32(d + 18)); h = int32_t(le32(d + 22)); bpp = int(le16(d + 28)); comp = le32(d + 30); clrUsed = le32(d + 46);
    } else { err = "BMP: unsupported header"; return false; }
    bool topDown = h < 0; if (h < 0) h = -h;
    if (w <= 0 || h <= 0) { err = "BMP: invalid size"; return false; }
    if (comp != 0 && comp != 3) { err = "BMP: compressed BMP is not supported"; return false; }
    if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) { err = "BMP: unsupported bit depth"; return false; }
    if (!allocImage(out, w, h, kMaxPixels, err)) return false;
    // لوحة الألوان
    std::vector<uint8_t> pal;   // RGB
    size_t palOff = 14 + dib, entry = dib == 12 ? 3 : 4;
    if (bpp <= 8) {
        size_t cnt = clrUsed ? clrUsed : (size_t(1) << bpp);
        if (cnt > 256) cnt = 256;
        for (size_t i = 0; i < cnt; ++i) {
            size_t o = palOff + i * entry;
            if (o + 2 >= n) break;
            pal.push_back(d[o + 2]); pal.push_back(d[o + 1]); pal.push_back(d[o]);
        }
    }
    uint32_t masks[3] = {0x7C00, 0x03E0, 0x001F};
    if (bpp == 16 && comp == 3 && 14 + dib + 12 <= n && dib >= 40) { masks[0] = le32(d + 54); masks[1] = le32(d + 58); masks[2] = le32(d + 62); }
    uint32_t m32[3] = {0x00FF0000, 0x0000FF00, 0x000000FF};
    if (bpp == 32 && comp == 3 && dib >= 40 && n >= 66) { m32[0] = le32(d + 54); m32[1] = le32(d + 58); m32[2] = le32(d + 62); }
    size_t stride = ((size_t(w) * size_t(bpp) + 31) / 32) * 4;
    auto ch = [](uint32_t v, uint32_t mask) -> uint8_t {
        if (!mask) return 0;
        int sh = 0; while (!((mask >> sh) & 1)) ++sh;
        uint32_t m = mask >> sh, x = (v & mask) >> sh;
        return uint8_t(m ? x * 255 / m : 0);
    };
    for (long long y = 0; y < h; ++y) {
        size_t srcRow = size_t(topDown ? y : h - 1 - y);
        size_t ro = dataOff + srcRow * stride;
        if (ro + (size_t(w) * size_t(bpp) + 7) / 8 > n) { err = "BMP: truncated"; return false; }
        const uint8_t* row = d + ro;
        for (long long x = 0; x < w; ++x) {
            uint8_t r = 0, g = 0, b = 0;
            if (bpp == 24) { b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; }
            else if (bpp == 32) {
                uint32_t v = le32(row + x * 4);
                if (comp == 3) { r = ch(v, m32[0]); g = ch(v, m32[1]); b = ch(v, m32[2]); }
                else { b = uint8_t(v); g = uint8_t(v >> 8); r = uint8_t(v >> 16); }
            } else if (bpp == 16) { uint32_t v = le16(row + x * 2); r = ch(v, masks[0]); g = ch(v, masks[1]); b = ch(v, masks[2]); }
            else {
                size_t bit = size_t(x) * size_t(bpp);
                int shift = 8 - bpp - int(bit % 8);
                size_t idx = (row[bit / 8] >> shift) & ((1u << bpp) - 1);
                if (idx * 3 + 2 < pal.size()) { r = pal[idx * 3]; g = pal[idx * 3 + 1]; b = pal[idx * 3 + 2]; }
            }
            uint8_t* o = &out.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            o[0] = r; o[1] = g; o[2] = b; o[3] = 255;
        }
    }
    return true;
}

// ----------------------------------------------------------------------------- JPEG
namespace detail {

static const uint8_t kZigzag[64 + 16] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

struct JHuff {
    bool defined = false;
    uint8_t vals[256];
    int mincode[17], maxcode[17], valptr[17];
    void build(const uint8_t* counts) {
        int code = 0, k = 0;
        for (int len = 1; len <= 16; ++len) {
            valptr[len] = k; mincode[len] = code;
            code += counts[len - 1]; k += counts[len - 1];
            maxcode[len] = counts[len - 1] ? code - 1 : -1;
            code <<= 1;
        }
        defined = true;
    }
};

struct JBits {
    const uint8_t* d = nullptr; size_t n = 0, pos = 0;
    uint32_t bits = 0; int nbits = 0;
    bool marker = false; int markerCode = 0;
    void fill() {
        if (marker || pos >= n) { bits = 0; nbits = 8; return; }
        uint8_t b = d[pos++];
        if (b == 0xFF) {
            uint8_t b2 = pos < n ? d[pos] : 0xD9;
            if (b2 == 0) pos++;
            else { marker = true; markerCode = b2; pos--; bits = 0; nbits = 8; return; }
        }
        bits = b; nbits = 8;
    }
    int bit() { if (nbits == 0) fill(); --nbits; return int((bits >> nbits) & 1); }
    int getbits(int cnt) { int v = 0; while (cnt-- > 0) v = (v << 1) | bit(); return v; }
    void restart() {
        nbits = 0; bits = 0;
        if (!marker) { while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7)) { if (d[pos] == 0xFF && d[pos + 1] != 0 && d[pos + 1] != 0xFF) break; ++pos; } if (pos + 1 < n && d[pos] == 0xFF && d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7) { markerCode = d[pos + 1]; marker = true; } }
        if (marker && markerCode >= 0xD0 && markerCode <= 0xD7) { pos += 2; marker = false; }
    }
};

inline int jextend(int v, int t) { return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

inline int jdecode(JBits& br, const JHuff& h) {
    int code = 0;
    for (int len = 1; len <= 16; ++len) {
        code = (code << 1) | br.bit();
        if (h.maxcode[len] >= 0 && code <= h.maxcode[len] && code >= h.mincode[len]) return h.vals[h.valptr[len] + code - h.mincode[len]];
    }
    return -1;
}

struct JComp {
    int id = 0, h = 1, v = 1, tq = 0;
    int bw = 0, bh = 0;     // كتل مبطَّنة (لمسح متداخل)
    int cbw = 0, cbh = 0;   // كتل حقيقية (لمسح غير متداخل)
    int td = 0, ta = 0, pred = 0;
    std::vector<int16_t> coef;
    std::vector<uint8_t> plane;
};

}  // namespace detail

inline bool decodeJpeg(const uint8_t* d, size_t n, Image& out, std::string& err) {
    using namespace detail;
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) { err = "not a JPEG"; return false; }
    uint16_t qt[4][64];
    std::memset(qt, 0, sizeof(qt));
    JHuff hdc[4], hac[4];
    JComp comps[4];
    int nc = 0, W = 0, H = 0, hmax = 1, vmax = 1, mcux = 0, mcuy = 0, restartInt = 0, adobe = -1, orient = 1;
    bool progressive = false, haveSof = false, anyScan = false;
    int scale = 1;   // 1 أو 8 (DC فقط)
    const int B = 8;
    size_t pos = 2;

    auto skipToMarker = [&](size_t p) {
        while (p + 1 < n) {
            if (d[p] == 0xFF && d[p + 1] != 0 && d[p + 1] != 0xFF && !(d[p + 1] >= 0xD0 && d[p + 1] <= 0xD7)) return p;
            ++p;
        }
        return n;
    };

    while (pos + 1 < n) {
        if (d[pos] != 0xFF) { ++pos; continue; }
        while (pos < n && d[pos] == 0xFF) ++pos;
        if (pos >= n) break;
        int m = d[pos++];
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0) continue;
        if (m == 0xD9) break;
        if (pos + 2 > n) break;
        size_t len = be16(d + pos);
        if (len < 2 || pos + len > n) { if (anyScan) break; err = "JPEG: truncated"; return false; }
        const uint8_t* seg = d + pos + 2;
        size_t sl = len - 2;
        size_t nextPos = pos + len;

        if (m == 0xE1 && sl >= 14 && !std::memcmp(seg, "Exif\0\0", 6)) {   // EXIF orientation
            const uint8_t* t = seg + 6; size_t tl = sl - 6;
            bool le = t[0] == 'I';
            auto r16 = [&](size_t o) { return o + 2 <= tl ? (le ? le16(t + o) : be16(t + o)) : 0u; };
            auto r32 = [&](size_t o) { return o + 4 <= tl ? (le ? le32(t + o) : be32(t + o)) : 0u; };
            if (tl >= 8 && (t[0] == 'I' || t[0] == 'M')) {
                size_t ifd = r32(4);
                if (ifd + 2 <= tl) {
                    size_t cnt = r16(ifd);
                    for (size_t i = 0; i < cnt && ifd + 2 + i * 12 + 12 <= tl; ++i) {
                        size_t e = ifd + 2 + i * 12;
                        if (r16(e) == 0x0112) { uint32_t v = r16(e + 8); if (v >= 1 && v <= 8) orient = int(v); }
                    }
                }
            }
        } else if (m == 0xEE && sl >= 12 && !std::memcmp(seg, "Adobe", 5)) adobe = seg[11];
        else if (m == 0xDB) {
            size_t p = 0;
            while (p < sl) {
                int pq = seg[p] >> 4, tq = seg[p] & 15; ++p;
                if (tq > 3) { err = "JPEG: bad quantization table"; return false; }
                for (int i = 0; i < 64; ++i) {
                    if (pq) { if (p + 1 >= sl + 1 || p + 2 > sl) break; qt[tq][kZigzag[i]] = uint16_t(be16(seg + p)); p += 2; }
                    else { if (p >= sl) break; qt[tq][kZigzag[i]] = seg[p++]; }
                }
            }
        } else if (m == 0xC4) {
            size_t p = 0;
            while (p + 17 <= sl) {
                int tc = seg[p] >> 4, th = seg[p] & 15;
                if (tc > 1 || th > 3) { err = "JPEG: bad Huffman table"; return false; }
                int total = 0;
                for (int i = 0; i < 16; ++i) total += seg[p + 1 + size_t(i)];
                if (total > 256 || p + 17 + size_t(total) > sl) { err = "JPEG: bad Huffman table"; return false; }
                JHuff& h = tc ? hac[th] : hdc[th];
                std::memcpy(h.vals, seg + p + 17, size_t(total));
                h.build(seg + p + 1);
                p += 17 + size_t(total);
            }
        } else if (m == 0xDD && sl >= 2) restartInt = int(be16(seg));
        else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            if (haveSof) { err = "JPEG: multiple frames"; return false; }
            if (sl < 6 || seg[0] != 8) { err = "JPEG: only 8-bit precision is supported"; return false; }
            progressive = m == 0xC2;
            H = int(be16(seg + 1)); W = int(be16(seg + 3)); nc = seg[5];
            if (W <= 0 || H <= 0) { err = "JPEG: invalid size"; return false; }
            if (nc != 1 && nc != 3) { err = "JPEG: only grayscale and YCbCr/RGB are supported (not CMYK)"; return false; }
            if (sl < size_t(6 + nc * 3)) { err = "JPEG: truncated"; return false; }
            if (static_cast<long long>(W) * H > kJpegMaxPixels) { err = "JPEG: image too large"; return false; }
            for (int i = 0; i < nc; ++i) {
                comps[i].id = seg[6 + i * 3];
                comps[i].h = seg[7 + i * 3] >> 4; comps[i].v = seg[7 + i * 3] & 15; comps[i].tq = seg[8 + i * 3];
                if (comps[i].h < 1 || comps[i].h > 4 || comps[i].v < 1 || comps[i].v > 4 || comps[i].tq > 3) { err = "JPEG: bad component"; return false; }
                hmax = std::max(hmax, comps[i].h); vmax = std::max(vmax, comps[i].v);
            }
            mcux = (W + 8 * hmax - 1) / (8 * hmax); mcuy = (H + 8 * vmax - 1) / (8 * vmax);
            scale = (static_cast<long long>(W) * H > kJpegFullMaxPixels) ? 8 : 1;
            for (int i = 0; i < nc; ++i) {
                JComp& c = comps[i];
                c.bw = mcux * c.h; c.bh = mcuy * c.v;
                c.cbw = ((W * c.h + hmax - 1) / hmax + 7) / 8; c.cbh = ((H * c.v + vmax - 1) / vmax + 7) / 8;
                c.coef.assign(size_t(c.bw) * size_t(c.bh) * (scale == 1 ? 64 : 1), 0);
            }
            haveSof = true;
        } else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) { err = "JPEG: arithmetic/lossless JPEG is not supported"; return false; }
        else if (m == 0xDA) {
            if (!haveSof) { err = "JPEG: scan before frame header"; return false; }
            int ns = seg[0];
            if (ns < 1 || ns > 4 || sl < size_t(1 + ns * 2 + 3)) { err = "JPEG: bad scan header"; return false; }
            JComp* sc[4] = {nullptr, nullptr, nullptr, nullptr};
            for (int i = 0; i < ns; ++i) {
                int cs = seg[1 + i * 2];
                for (int k = 0; k < nc; ++k) if (comps[k].id == cs) sc[i] = &comps[k];
                if (!sc[i]) { err = "JPEG: bad scan component"; return false; }
                sc[i]->td = seg[2 + i * 2] >> 4; sc[i]->ta = seg[2 + i * 2] & 15;
                if (sc[i]->td > 3 || sc[i]->ta > 3) { err = "JPEG: bad table selector"; return false; }
            }
            int Ss = seg[1 + ns * 2], Se = seg[2 + ns * 2], Ah = seg[3 + ns * 2] >> 4, Al = seg[3 + ns * 2] & 15;
            if (!progressive) { Ss = 0; Se = 63; Ah = 0; Al = 0; }
            if (Ss > Se || Se > 63) { err = "JPEG: bad scan parameters"; return false; }
            if (scale == 8 && progressive && Ss > 0) { pos = skipToMarker(nextPos); anyScan = true; continue; }   // AC غير لازم في 1/8

            JBits br; br.d = d; br.n = n; br.pos = nextPos;
            for (int i = 0; i < nc; ++i) comps[i].pred = 0;
            int eobrun = 0, rstCount = 0;
            bool full = scale == 1;
            int16_t scratch[64];

            auto decodeBlock = [&](JComp& c, int bx, int by) -> bool {
                int16_t* blk;
                if (full) blk = &c.coef[(size_t(by) * size_t(c.bw) + size_t(bx)) * 64];
                else { blk = scratch; if (!progressive) std::memset(scratch, 0, sizeof(scratch)); else { scratch[0] = c.coef[size_t(by) * size_t(c.bw) + size_t(bx)]; } }
                const JHuff& hd = hdc[c.td];
                const JHuff& ha = hac[c.ta];
                if (!progressive) {
                    if (!hd.defined || !ha.defined) return false;
                    int t = jdecode(br, hd);
                    if (t < 0 || t > 16) return false;
                    c.pred += t ? jextend(br.getbits(t), t) : 0;
                    blk[0] = int16_t(c.pred);
                    for (int k = 1; k < 64;) {
                        int rs = jdecode(br, ha);
                        if (rs < 0) return false;
                        int r = rs >> 4, s = rs & 15;
                        if (s == 0) { if (r == 15) { k += 16; continue; } break; }
                        k += r;
                        if (k > 63) return false;
                        blk[kZigzag[k]] = int16_t(jextend(br.getbits(s), s));
                        ++k;
                    }
                } else if (Ss == 0) {   // DC
                    if (Ah == 0) {
                        if (!hd.defined) return false;
                        int t = jdecode(br, hd);
                        if (t < 0 || t > 16) return false;
                        c.pred += t ? jextend(br.getbits(t), t) : 0;
                        blk[0] = int16_t(c.pred * (1 << Al));
                    } else if (br.bit()) blk[0] = int16_t(blk[0] | (1 << Al));
                } else if (Ah == 0) {   // AC أول
                    if (eobrun > 0) { --eobrun; return true; }
                    if (!ha.defined) return false;
                    for (int k = Ss; k <= Se;) {
                        int rs = jdecode(br, ha);
                        if (rs < 0) return false;
                        int r = rs >> 4, s = rs & 15;
                        if (s == 0) {
                            if (r < 15) { eobrun = (1 << r) - 1; if (r) eobrun += br.getbits(r); break; }
                            k += 16;
                        } else {
                            k += r;
                            if (k > 63) return false;
                            blk[kZigzag[k]] = int16_t(jextend(br.getbits(s), s) * (1 << Al));
                            ++k;
                        }
                    }
                } else {   // AC تنقيح
                    if (!ha.defined) return false;
                    int p1 = 1 << Al, m1 = -(1 << Al), k = Ss;
                    auto refine = [&](int16_t& cf) {
                        if (br.bit() && (cf & p1) == 0) cf = int16_t(cf + (cf >= 0 ? p1 : m1));
                    };
                    if (eobrun == 0) {
                        for (; k <= Se; ++k) {
                            int rs = jdecode(br, ha);
                            if (rs < 0) return false;
                            int r = rs >> 4, s = rs & 15, val = 0;
                            if (s) val = br.bit() ? p1 : m1;
                            else if (r != 15) { eobrun = 1 << r; if (r) eobrun += br.getbits(r); break; }
                            do {
                                int16_t& cf = blk[kZigzag[k]];
                                if (cf != 0) refine(cf);
                                else if (--r < 0) break;
                                ++k;
                            } while (k <= Se);
                            if (val && k <= Se) blk[kZigzag[k]] = int16_t(val);
                        }
                    }
                    if (eobrun > 0) {
                        for (; k <= Se; ++k) { int16_t& cf = blk[kZigzag[k]]; if (cf != 0) refine(cf); }
                        --eobrun;
                    }
                }
                if (!full) c.coef[size_t(by) * size_t(c.bw) + size_t(bx)] = blk[0];
                return true;
            };

            auto checkRestart = [&]() {
                if (restartInt && ++rstCount == restartInt) {
                    rstCount = 0; br.restart();
                    for (int i = 0; i < nc; ++i) comps[i].pred = 0;
                    eobrun = 0;
                }
            };

            bool ok = true;
            if (ns == 1) {   // غير متداخل: كتل المكوّن الحقيقية
                JComp& c = *sc[0];
                for (int by = 0; by < c.cbh && ok; ++by)
                    for (int bx = 0; bx < c.cbw && ok; ++bx) {
                        ok = decodeBlock(c, bx, by);
                        if (ok) checkRestart();
                    }
            } else {
                for (int my = 0; my < mcuy && ok; ++my)
                    for (int mx = 0; mx < mcux && ok; ++mx) {
                        for (int i = 0; i < ns && ok; ++i)
                            for (int v = 0; v < sc[i]->v && ok; ++v)
                                for (int h = 0; h < sc[i]->h && ok; ++h) ok = decodeBlock(*sc[i], mx * sc[i]->h + h, my * sc[i]->v + v);
                        if (ok) checkRestart();
                    }
            }
            anyScan = true;
            if (!ok && !anyScan) { err = "JPEG: corrupt data"; return false; }
            // بعد المسح: تابع من العلامة التالية (البيانات التالفة تُحتمل: نعرض ما فُكّ)
            pos = br.marker ? br.pos : skipToMarker(br.pos);
            if (!ok) pos = skipToMarker(br.pos);
            continue;
        }
        pos = nextPos;
    }
    if (!haveSof || !anyScan) { err = "JPEG: no image data"; return false; }

    // ---- IDCT إلى مستويات 8 بت ----
    static float T[8][8];
    static bool tInit = false;
    if (!tInit) {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u) T[x][u] = float((u == 0 ? 0.70710678118654752 : 1.0) * 0.5 * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0));
        tInit = true;
    }
    const int ps = scale == 1 ? B : 1;
    for (int i = 0; i < nc; ++i) {
        JComp& c = comps[i];
        const uint16_t* q = qt[c.tq];
        int pw = c.bw * ps, ph = c.bh * ps;
        c.plane.assign(size_t(pw) * size_t(ph), 0);
        for (int by = 0; by < c.bh; ++by)
            for (int bx = 0; bx < c.bw; ++bx) {
                if (scale == 8) {
                    float v = float(c.coef[size_t(by) * size_t(c.bw) + size_t(bx)]) * float(q[0]) / 8.0f + 128.0f;
                    c.plane[size_t(by) * size_t(pw) + size_t(bx)] = uint8_t(std::min(255.0f, std::max(0.0f, std::floor(v + 0.5f))));
                    continue;
                }
                const int16_t* cf = &c.coef[(size_t(by) * size_t(c.bw) + size_t(bx)) * 64];
                float F[64], tmp[64];
                for (int k = 0; k < 64; ++k) F[k] = float(cf[k]) * float(q[k]);
                for (int v = 0; v < 8; ++v)
                    for (int x = 0; x < 8; ++x) {
                        float s = 0;
                        for (int u = 0; u < 8; ++u) s += T[x][u] * F[v * 8 + u];
                        tmp[v * 8 + x] = s;
                    }
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 8; ++y) {
                        float s = 0;
                        for (int v = 0; v < 8; ++v) s += T[y][v] * tmp[v * 8 + x];
                        s += 128.0f;
                        c.plane[(size_t(by) * 8 + size_t(y)) * size_t(pw) + size_t(bx) * 8 + size_t(x)] = uint8_t(std::min(255.0f, std::max(0.0f, std::floor(s + 0.5f))));
                    }
            }
        c.coef.clear(); c.coef.shrink_to_fit();
    }

    // ---- رفع العينات + تحويل الألوان ----
    int ow = scale == 1 ? W : (W + 7) / 8, oh = scale == 1 ? H : (H + 7) / 8;
    if (!allocImage(out, ow, oh, kJpegMaxPixels, err)) return false;
    bool rgbDirect = nc == 3 && (adobe == 0 || (comps[0].id == 'R' && comps[1].id == 'G' && comps[2].id == 'B'));
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            uint8_t* o = &out.rgba[(size_t(y) * size_t(ow) + size_t(x)) * 4];
            auto at = [&](int i) -> int {
                const JComp& c = comps[i];
                const size_t stride = size_t(c.bw) * size_t(ps);
                if (c.h == hmax && c.v == vmax) return c.plane[size_t(y) * stride + size_t(x)];
                // مكوّن مُقلَّل العينات: استيفاء خطي حول مركز العيّنة (بدل أقرب جار) ثم تثبيت عند حدود المكوّن الحقيقية
                const int cw = scale == 1 ? (W * c.h + hmax - 1) / hmax : c.cbw;
                const int chh = scale == 1 ? (H * c.v + vmax - 1) / vmax : c.cbh;
                float fx = (float(x) + 0.5f) * float(c.h) / float(hmax) - 0.5f;
                float fy = (float(y) + 0.5f) * float(c.v) / float(vmax) - 0.5f;
                int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
                float tx = fx - float(x0), ty = fy - float(y0);
                auto cx = [&](int v) { return std::min(std::max(v, 0), cw - 1); };
                auto cy = [&](int v) { return std::min(std::max(v, 0), chh - 1); };
                float p00 = c.plane[size_t(cy(y0)) * stride + size_t(cx(x0))], p10 = c.plane[size_t(cy(y0)) * stride + size_t(cx(x0 + 1))];
                float p01 = c.plane[size_t(cy(y0 + 1)) * stride + size_t(cx(x0))], p11 = c.plane[size_t(cy(y0 + 1)) * stride + size_t(cx(x0 + 1))];
                float top = p00 + (p10 - p00) * tx, bot = p01 + (p11 - p01) * tx;
                return int(top + (bot - top) * ty + 0.5f);
            };
            if (nc == 1) { o[0] = o[1] = o[2] = uint8_t(at(0)); }
            else if (rgbDirect) { o[0] = uint8_t(at(0)); o[1] = uint8_t(at(1)); o[2] = uint8_t(at(2)); }
            else {
                float Y = float(at(0)), cb = float(at(1)) - 128.0f, cr = float(at(2)) - 128.0f;
                auto cl = [](float v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : int(v + 0.5f)); };
                o[0] = cl(Y + 1.402f * cr);
                o[1] = cl(Y - 0.344136f * cb - 0.714136f * cr);
                o[2] = cl(Y + 1.772f * cb);
            }
            o[3] = 255;
        }
    out.orientation = orient;
    return true;
}

// ----------------------------------------------------------------------------- عام
// يعرّف الصيغة من البايتات (لا من الامتداد). يعيد "" إن لم تكن صيغة نقطية معروفة.
inline std::string sniff(const uint8_t* h, size_t n) {
    if (n >= 8 && h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') return "png";
    if (n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return "jpeg";
    if (n >= 4 && std::memcmp(h, "GIF8", 4) == 0) return "gif";
    if (n >= 2 && h[0] == 'B' && h[1] == 'M') return "bmp";
    if (n >= 12 && std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WEBP", 4) == 0) return "webp";
    if (n >= 4 && ((h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) || (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42))) return "tiff";
    return "";
}

inline bool readFile(const std::string& path, std::vector<uint8_t>& buf, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot open file"; return false; }
    std::streamoff sz = f.tellg();
    if (sz <= 0) { err = "empty file"; return false; }
    if (static_cast<unsigned long long>(sz) > kMaxFileBytes) { err = "file too large"; return false; }
    buf.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (!f) { err = "read error"; return false; }
    return true;
}

// تدوير/قلب حسب EXIF (1 = لا شيء).
inline void applyOrientation(Image& im, int o) {
    if (o < 2 || o > 8 || im.w <= 0 || im.h <= 0) return;
    const bool swap = o >= 5;
    const int ow = swap ? im.h : im.w, oh = swap ? im.w : im.h;
    std::vector<uint8_t> dst(im.rgba.size());
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            int dx, dy;
            switch (o) {
                case 2: dx = im.w - 1 - x; dy = y; break;
                case 3: dx = im.w - 1 - x; dy = im.h - 1 - y; break;
                case 4: dx = x; dy = im.h - 1 - y; break;
                case 5: dx = y; dy = x; break;
                case 6: dx = im.h - 1 - y; dy = x; break;
                case 7: dx = im.h - 1 - y; dy = im.w - 1 - x; break;
                default: dx = y; dy = im.w - 1 - x; break;
            }
            std::memcpy(&dst[(size_t(dy) * size_t(ow) + size_t(dx)) * 4], &im.rgba[(size_t(y) * size_t(im.w) + size_t(x)) * 4], 4);
        }
    im.rgba.swap(dst); im.w = ow; im.h = oh;
}

// يفكّ الملف بالمفكّكات المدمجة. unsupportedFormat=true عندما تكون الصيغة معروفة لكنها خارج المدمج (webp/tiff).
inline bool decodeFile(const std::string& path, Image& out, std::string& err, bool* unsupportedFormat = nullptr) {
    if (unsupportedFormat) *unsupportedFormat = false;
    std::vector<uint8_t> buf;
    if (!readFile(path, buf, err)) return false;
    std::string kind = sniff(buf.data(), buf.size());
    bool ok = false;
    if (kind == "png") ok = decodePng(buf.data(), buf.size(), out, err);
    else if (kind == "jpeg") ok = decodeJpeg(buf.data(), buf.size(), out, err);
    else if (kind == "gif") ok = decodeGif(buf.data(), buf.size(), out, err);
    else if (kind == "bmp") ok = decodeBmp(buf.data(), buf.size(), out, err);
    else {
        if (unsupportedFormat) *unsupportedFormat = true;
        err = kind.empty() ? "not a supported raster image" : kind + " is not supported by the built-in decoder";
        return false;
    }
    if (!ok) return false;
    applyOrientation(out, out.orientation);
    out.orientation = 1;
    return true;
}

// تصغير/تكبير بمتوسط المساحة (box) إلى pw×ph، مع دمج الشفافية على خلفية بيضاء. الناتج RGB 8 بت.
inline void resizeToRgb(const Image& src, int pw, int ph, std::vector<uint8_t>& rgb) {
    struct Span { int i0; std::vector<float> w; };
    auto spans = [](int srcN, int dstN) {
        std::vector<Span> v(static_cast<size_t>(dstN));
        const double scale = double(srcN) / double(dstN);
        for (int i = 0; i < dstN; ++i) {
            double a = i * scale, b = (i + 1) * scale;
            int i0 = int(std::floor(a)), i1 = std::min(srcN - 1, int(std::ceil(b)) - 1);
            if (i1 < i0) i1 = i0;
            Span s; s.i0 = i0;
            double sum = 0;
            for (int k = i0; k <= i1; ++k) {
                double w = std::min(b, double(k + 1)) - std::max(a, double(k));
                if (w < 0) w = 0;
                s.w.push_back(float(w)); sum += w;
            }
            if (sum <= 0) { s.w.assign(1, 1.0f); sum = 1; }
            for (auto& w : s.w) w = float(w / sum);
            v[size_t(i)] = std::move(s);
        }
        return v;
    };
    std::vector<Span> xs = spans(src.w, pw), ys = spans(src.h, ph);
    std::vector<float> tmp(size_t(src.h) * size_t(pw) * 3);
    for (int y = 0; y < src.h; ++y) {
        const uint8_t* row = &src.rgba[size_t(y) * size_t(src.w) * 4];
        for (int x = 0; x < pw; ++x) {
            float r = 0, g = 0, b = 0;
            const Span& s = xs[size_t(x)];
            for (size_t k = 0; k < s.w.size(); ++k) {
                const uint8_t* p = row + size_t(s.i0 + int(k)) * 4;
                float a = p[3] / 255.0f;   // دمج على الأبيض
                r += s.w[k] * (p[0] * a + 255.0f * (1 - a));
                g += s.w[k] * (p[1] * a + 255.0f * (1 - a));
                b += s.w[k] * (p[2] * a + 255.0f * (1 - a));
            }
            float* t = &tmp[(size_t(y) * size_t(pw) + size_t(x)) * 3];
            t[0] = r; t[1] = g; t[2] = b;
        }
    }
    rgb.assign(size_t(pw) * size_t(ph) * 3, 0);
    for (int y = 0; y < ph; ++y) {
        const Span& s = ys[size_t(y)];
        for (int x = 0; x < pw; ++x) {
            float r = 0, g = 0, b = 0;
            for (size_t k = 0; k < s.w.size(); ++k) {
                const float* t = &tmp[(size_t(s.i0 + int(k)) * size_t(pw) + size_t(x)) * 3];
                r += s.w[k] * t[0]; g += s.w[k] * t[1]; b += s.w[k] * t[2];
            }
            uint8_t* o = &rgb[(size_t(y) * size_t(pw) + size_t(x)) * 3];
            o[0] = uint8_t(std::min(255.0f, std::max(0.0f, r + 0.5f)));
            o[1] = uint8_t(std::min(255.0f, std::max(0.0f, g + 0.5f)));
            o[2] = uint8_t(std::min(255.0f, std::max(0.0f, b + 0.5f)));
        }
    }
}

}  // namespace imgdec
}  // namespace rin
