// ============================================================================
//  rin_image_draw.cpp — إنشاء الصور والرسم عليها وحفظها (PNG/BMP/PPM) + سجلّ log كصورة
// ----------------------------------------------------------------------------
//  يُضمَّن في نهاية rin_interpreter.cpp (بعد rin_image_ansi.cpp) كبقية دفعات natives،
//  فيستعمل نفس مساعدات الملف (asNumber/asString/diagErr) وخط rinfont العربي والمفكّك stb.
//  بلا أي اعتماد جديد: الترميز يدويّاً + zlib (موجود أصلاً في المشروع).
//
//  اللوحة = RGBA 8-بت تُعرَّف بمعرّف رقمي (handle). كل الرسم مُنعَّم (anti-aliased) بمزج
//  source-over على ألفا مستقيمة (straight alpha).
//
//    إنشاء :  imgNew(w,h[,bg]) · imgLoad(path) · imgCopy(id) · imgFree(id)
//    معلومات:  imgSize(id) · imgPixel(id,x,y[,color])
//    رسم    :  imgClear · imgLine · imgRect · imgCircle · imgEllipse · imgPolygon · imgText
//              imgGradient · imgFloodFill · imgTextSize
//    تحويل  :  imgResize · imgCrop · imgPaste · imgFlip · imgFilter
//    إخراج  :  imgSave(id,path[,fmt]) · imgEncode(id,fmt) · imgPrint(id[,cols,mode]) · imgAnsi
//    نص/سجلّ:  imgFromText(text[,opts]) · imgFromLog([entries,][opts]) · logSave("x.png")
//
//  اللون: "#rgb" "#rrggbb" "#rrggbbaa" rgb()/hsl()/اسم CSS · رقم 0xRRGGBB · [r,g,b[,a]].
//  النص: خط 5×7 نقطي لـ ASCII (حاد بتكبير صحيح)، وللحروف العربية خط الخطوط المتجهي
//  rinfont (تقريبي: أشكال معزولة بلا وصل حروف) — انظر docs/image-draw.md.
// ============================================================================
#include "rin_interpreter.h"
#include "indsin/rin_indsin_font.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <zlib.h>

namespace rin {
namespace imgdraw {

using rincolor::Color;

struct Canvas { int w = 0, h = 0; std::vector<uint8_t> px; };   // RGBA

static std::unordered_map<int, Canvas>& store() { static std::unordered_map<int, Canvas> s; return s; }
static int g_nextId = 1;
static const int kMaxSide = 16384;
static const double kMaxPixels = 32e6;   // 128MB RGBA كحدّ أعلى للوحة واحدة
static const size_t kMaxLive = 64;

bool getRgba(int id, int& w, int& h, std::vector<uint8_t>& px) {
    auto it = store().find(id);
    if (it == store().end()) return false;
    w = it->second.w; h = it->second.h; px = it->second.px;
    return true;
}

// ---------------------------------------------------------------- مزج
static inline void blendPx(Canvas& c, int x, int y, const Color& col, double cov) {
    if (x < 0 || y < 0 || x >= c.w || y >= c.h || cov <= 0.0) return;
    double sa = (col.a / 255.0) * std::min(1.0, cov);
    if (sa <= 0.0) return;
    uint8_t* d = &c.px[((size_t)y * c.w + x) * 4];
    if (sa >= 0.9999) { d[0] = col.r; d[1] = col.g; d[2] = col.b; d[3] = 255; return; }
    double da = d[3] / 255.0;
    double oa = sa + da * (1.0 - sa);
    if (oa <= 0.0) { d[0] = d[1] = d[2] = d[3] = 0; return; }
    d[0] = (uint8_t)std::lround((col.r * sa + d[0] * da * (1.0 - sa)) / oa);
    d[1] = (uint8_t)std::lround((col.g * sa + d[1] * da * (1.0 - sa)) / oa);
    d[2] = (uint8_t)std::lround((col.b * sa + d[2] * da * (1.0 - sa)) / oa);
    d[3] = (uint8_t)std::lround(oa * 255.0);
}
static inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// خط مستقيم بسماكة t وأطراف دائرية: يمشي على المحور الأطول فيكلّفه O(طول × سماكة).
static void drawLine(Canvas& c, double x0, double y0, double x1, double y1, const Color& col, double t) {
    t = std::max(0.5, t);
    double r = t / 2.0 + 0.75;
    double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    auto dist = [&](double px, double py) {
        double u = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0;
        u = std::max(0.0, std::min(1.0, u));
        double qx = x0 + u * dx - px, qy = y0 + u * dy - py;
        return std::sqrt(qx * qx + qy * qy);
    };
    auto put = [&](int x, int y) { blendPx(c, x, y, col, clamp01(t / 2.0 + 0.5 - dist(x + 0.5, y + 0.5))); };
    if (std::fabs(dx) >= std::fabs(dy)) {
        double slope = dx != 0 ? dy / dx : 0.0, half = r * std::sqrt(1 + slope * slope) + 1;
        int xa = (int)std::floor(std::min(x0, x1) - r), xb = (int)std::ceil(std::max(x0, x1) + r);
        xa = std::max(xa, 0); xb = std::min(xb, c.w - 1);
        for (int x = xa; x <= xb; ++x) {
            double xc = std::max(std::min(x0, x1), std::min(std::max(x0, x1), x + 0.5));
            double yc = dx != 0 ? y0 + (xc - x0) * slope : y0;
            int ya = std::max(0, (int)std::floor(yc - half)), yb = std::min(c.h - 1, (int)std::ceil(yc + half));
            for (int y = ya; y <= yb; ++y) put(x, y);
        }
    } else {
        double slope = dx / dy, half = r * std::sqrt(1 + slope * slope) + 1;
        int ya = (int)std::floor(std::min(y0, y1) - r), yb = (int)std::ceil(std::max(y0, y1) + r);
        ya = std::max(ya, 0); yb = std::min(yb, c.h - 1);
        for (int y = ya; y <= yb; ++y) {
            double yc = std::max(std::min(y0, y1), std::min(std::max(y0, y1), y + 0.5));
            double xc = x0 + (yc - y0) * slope;
            int xa = std::max(0, (int)std::floor(xc - half)), xb = std::min(c.w - 1, (int)std::ceil(xc + half));
            for (int x = xa; x <= xb; ++x) put(x, y);
        }
    }
}

// مستطيل (بزوايا مدوّرة اختيارياً) عبر signed-distance: الحدّ يقع داخل المستطيل.
static void drawRect(Canvas& c, double x, double y, double w, double h, const Color& col, bool fill, double sw, double rad) {
    if (w <= 0 || h <= 0) return;
    double cx = x + w / 2, cy = y + h / 2, hw = w / 2, hh = h / 2;
    rad = std::max(0.0, std::min(rad, std::min(hw, hh)));
    int xa = std::max(0, (int)std::floor(x) - 1), xb = std::min(c.w - 1, (int)std::ceil(x + w) + 1);
    int ya = std::max(0, (int)std::floor(y) - 1), yb = std::min(c.h - 1, (int)std::ceil(y + h) + 1);
    for (int py = ya; py <= yb; ++py) for (int px = xa; px <= xb; ++px) {
        double qx = std::fabs(px + 0.5 - cx) - (hw - rad), qy = std::fabs(py + 0.5 - cy) - (hh - rad);
        double d = std::sqrt(std::max(qx, 0.0) * std::max(qx, 0.0) + std::max(qy, 0.0) * std::max(qy, 0.0))
                   + std::min(std::max(qx, qy), 0.0) - rad;
        double cov = clamp01(0.5 - d);
        if (!fill) cov *= clamp01(d + sw + 0.5);
        blendPx(c, px, py, col, cov);
    }
}

static void drawEllipse(Canvas& c, double cx, double cy, double rx, double ry, const Color& col, bool fill, double sw) {
    if (rx <= 0 || ry <= 0) return;
    double m = std::min(rx, ry);
    int xa = std::max(0, (int)std::floor(cx - rx) - 1), xb = std::min(c.w - 1, (int)std::ceil(cx + rx) + 1);
    int ya = std::max(0, (int)std::floor(cy - ry) - 1), yb = std::min(c.h - 1, (int)std::ceil(cy + ry) + 1);
    for (int py = ya; py <= yb; ++py) for (int px = xa; px <= xb; ++px) {
        double nx = (px + 0.5 - cx) / rx, ny = (py + 0.5 - cy) / ry;
        double d = (std::sqrt(nx * nx + ny * ny) - 1.0) * m;
        double cov = clamp01(0.5 - d);
        if (!fill) cov *= clamp01(d + sw + 0.5);
        blendPx(c, px, py, col, cov);
    }
}

// ملء مضلّع (even-odd): 4 صفوف فرعية لكل بكسل + تغطية أفقية كسرية.
static void fillPolygon(Canvas& c, const std::vector<std::pair<double, double>>& pts, const Color& col) {
    if (pts.size() < 3) return;
    double miny = 1e18, maxy = -1e18;
    for (auto& p : pts) { miny = std::min(miny, p.second); maxy = std::max(maxy, p.second); }
    int ya = std::max(0, (int)std::floor(miny)), yb = std::min(c.h - 1, (int)std::ceil(maxy));
    const int SS = 4;
    std::vector<double> cov((size_t)c.w);
    for (int y = ya; y <= yb; ++y) {
        std::fill(cov.begin(), cov.end(), 0.0);
        bool any = false;
        for (int s = 0; s < SS; ++s) {
            double sy = y + (s + 0.5) / SS;
            std::vector<double> xs;
            for (size_t i = 0; i < pts.size(); ++i) {
                auto a = pts[i], b = pts[(i + 1) % pts.size()];
                if ((a.second <= sy && b.second > sy) || (b.second <= sy && a.second > sy))
                    xs.push_back(a.first + (sy - a.second) / (b.second - a.second) * (b.first - a.first));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                double l = std::max(0.0, xs[i]), r = std::min((double)c.w, xs[i + 1]);
                if (r <= l) continue;
                any = true;
                int xl = (int)std::floor(l), xr = (int)std::ceil(r);
                for (int x = xl; x < xr && x < c.w; ++x)
                    cov[(size_t)x] += (std::min(r, x + 1.0) - std::max(l, (double)x)) / SS;
            }
        }
        if (!any) continue;
        for (int x = 0; x < c.w; ++x) if (cov[(size_t)x] > 0) blendPx(c, x, y, col, cov[(size_t)x]);
    }
}

// ---------------------------------------------------------------- نص
static const int kAdvance = 6;      // وحدات خط 5×7 لكل حرف (5 + 1 فراغ)
static const int kLineH = 9;        // 7 صفوف + 2 فراغ

static std::vector<char32_t> visualCps(const std::string& s) {
    auto cps = rinfont::utf8Decode(s);
    bool ar = false;
    return rinfont::visualOrder(cps, ar);
}

static int textCols(const std::string& line) { return (int)visualCps(line).size(); }

// يرسم سطراً واحداً بزاوية علوية يسرى (x,y)؛ size = تكبير صحيح لخلية 5×7. يُرجع العرض بالبكسل.
static int drawLineText(Canvas& c, int x, int y, const std::string& line, const Color& col, int size) {
    auto cps = visualCps(line);
    int cx = x;
    const auto& table = rinfont::glyphTable();
    double s = size * 0.7, thick = std::max(1.0, size * 0.55);
    for (char32_t cp : cps) {
        if (cp == U'\t') { cx += 4 * kAdvance * size; continue; }
        if (cp >= 0x20 && cp <= 0x7E) {
            const uint8_t* g = indsin::font::glyph5x7((unsigned char)cp);
            for (int gc = 0; g && gc < 5; ++gc) for (int gr = 0; gr < 7; ++gr) if (g[gc] & (1 << gr)) {
                int x0 = cx + gc * size, y0 = y + gr * size;
                for (int yy = 0; yy < size; ++yy) for (int xx = 0; xx < size; ++xx) blendPx(c, x0 + xx, y0 + yy, col, 1.0);
            }
        } else if (cp > 0x7E) {
            auto it = table.find(cp);
            if (it != table.end()) {
                for (const auto& sg : it->second)
                    drawLine(c, cx + sg.x0 * s, y + sg.y0 * s, cx + sg.x1 * s, y + sg.y1 * s, col, thick);
            } else {      // غير مدعوم: مربع مفرّغ صريح بدل الإخفاء الصامت
                drawRect(c, cx, y, 5.0 * size, 7.0 * size, col, false, std::max(1.0, size * 0.5), 0);
            }
        }
        cx += kAdvance * size;
    }
    return (int)cps.size() * kAdvance * size;
}

static std::vector<std::string> splitLines(const std::string& t) {
    std::vector<std::string> out; std::string cur;
    for (char ch : t) { if (ch == '\n') { out.push_back(cur); cur.clear(); } else if (ch != '\r') cur += ch; }
    out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------- ترميز
static void put32(std::string& o, uint32_t v) { for (int i = 3; i >= 0; --i) o += (char)((v >> (i * 8)) & 0xFF); }
static void chunk(std::string& o, const char* tag, const std::string& data) {
    put32(o, (uint32_t)data.size());
    std::string td(tag, 4); td += data;
    o += td;
    put32(o, (uint32_t)::crc32(0L, (const Bytef*)td.data(), (uInt)td.size()));
}
static bool isOpaque(const Canvas& c) {
    for (size_t i = 3; i < c.px.size(); i += 4) if (c.px[i] != 255) return false;
    return true;
}

static std::string encodePng(const Canvas& c) {
    const bool opaque = isOpaque(c);
    const int bpp = opaque ? 3 : 4;
    const size_t stride = (size_t)c.w * bpp;
    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr; put32(ihdr, c.w); put32(ihdr, c.h);
    ihdr += (char)8; ihdr += (char)(opaque ? 2 : 6); ihdr += (char)0; ihdr += (char)0; ihdr += (char)0;
    chunk(out, "IHDR", ihdr);
    std::vector<uint8_t> cur(stride), prev(stride, 0), f[5];
    for (auto& v : f) v.assign(stride, 0);
    std::string raw; raw.reserve((stride + 1) * c.h);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) for (int k = 0; k < bpp; ++k) cur[(size_t)x * bpp + k] = c.px[((size_t)y * c.w + x) * 4 + k];
        for (size_t i = 0; i < stride; ++i) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], cc = i >= (size_t)bpp ? prev[i - bpp] : 0;
            int p = a + b - cc, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - cc);
            int paeth = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : cc);
            f[0][i] = cur[i]; f[1][i] = (uint8_t)(cur[i] - a); f[2][i] = (uint8_t)(cur[i] - b);
            f[3][i] = (uint8_t)(cur[i] - ((a + b) >> 1)); f[4][i] = (uint8_t)(cur[i] - paeth);
        }
        int best = 0; long bestSum = -1;
        for (int k = 0; k < 5; ++k) {
            long sum = 0;
            for (size_t i = 0; i < stride; ++i) sum += std::abs((int)(int8_t)f[k][i]);
            if (bestSum < 0 || sum < bestSum) { bestSum = sum; best = k; }
        }
        raw += (char)best; raw.append((const char*)f[best].data(), stride);
        prev = cur;
    }
    uLongf cap = compressBound((uLong)raw.size());
    std::string z(cap, '\0');
    if (compress2((Bytef*)&z[0], &cap, (const Bytef*)raw.data(), (uLong)raw.size(), 6) != Z_OK) return "";
    z.resize(cap);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", "");
    return out;
}

static std::string encodeBmp(const Canvas& c) {
    const bool opaque = isOpaque(c);
    const int bpp = opaque ? 3 : 4;
    const size_t rowBytes = ((size_t)c.w * bpp + 3) & ~(size_t)3;
    auto le32 = [](std::string& o, uint32_t v) { for (int i = 0; i < 4; ++i) o += (char)((v >> (i * 8)) & 0xFF); };
    auto le16 = [](std::string& o, uint16_t v) { o += (char)(v & 0xFF); o += (char)(v >> 8); };
    std::string o("BM", 2);
    le32(o, (uint32_t)(54 + rowBytes * c.h)); le32(o, 0); le32(o, 54);
    le32(o, 40); le32(o, (uint32_t)c.w); le32(o, (uint32_t)c.h);   // ارتفاع موجب = صفوف من الأسفل (أوسع توافقاً؛ stb يرفض السالب في info)
    le16(o, 1); le16(o, (uint16_t)(bpp * 8)); le32(o, 0); le32(o, (uint32_t)(rowBytes * c.h));
    le32(o, 2835); le32(o, 2835); le32(o, 0); le32(o, 0);
    for (int y = c.h - 1; y >= 0; --y) {
        size_t start = o.size();
        for (int x = 0; x < c.w; ++x) {
            const uint8_t* p = &c.px[((size_t)y * c.w + x) * 4];
            o += (char)p[2]; o += (char)p[1]; o += (char)p[0]; if (bpp == 4) o += (char)p[3];
        }
        while (o.size() - start < rowBytes) o += (char)0;
    }
    return o;
}

static std::string encodePpm(const Canvas& c) {      // P6؛ الشفافية تُدمَج فوق الأبيض
    std::string o = "P6\n" + std::to_string(c.w) + " " + std::to_string(c.h) + "\n255\n";
    o.reserve(o.size() + (size_t)c.w * c.h * 3);
    for (size_t i = 0; i < (size_t)c.w * c.h; ++i) {
        const uint8_t* p = &c.px[i * 4]; double a = p[3] / 255.0;
        for (int k = 0; k < 3; ++k) o += (char)std::lround(p[k] * a + 255 * (1 - a));
    }
    return o;
}

static std::string lowerStr(std::string s) { for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; }

static std::string formatFromPath(const std::string& path) {
    size_t d = path.find_last_of('.');
    return d == std::string::npos ? "" : lowerStr(path.substr(d + 1));
}
bool isImagePath(const std::string& path) {
    std::string f = formatFromPath(path);
    return f == "png" || f == "bmp" || f == "ppm";
}

static std::string encodeAs(const Canvas& c, const std::string& fmt) {
    if (fmt == "png") return encodePng(c);
    if (fmt == "bmp") return encodeBmp(c);
    if (fmt == "ppm") return encodePpm(c);
    return "";
}

// ---------------------------------------------------------------- نص/سجلّ → لوحة
struct Run { std::string text; Color col; };
using StyledLine = std::vector<Run>;

struct TextStyle {
    int size = 2, pad = 16, wrap = 0, minW = 0;
    Color fg{0xe6, 0xed, 0xf3, 255}, bg{0x0d, 0x11, 0x17, 255}, titleCol{0x8b, 0x94, 0x9e, 255};
    std::string title;
    bool lineNumbers = false;
};

static Canvas renderStyled(const std::vector<StyledLine>& in, const TextStyle& st) {
    const int size = st.size, cell = kAdvance * size;
    int maxCols = std::max(8, (8192 - 2 * st.pad) / cell);
    if (st.wrap > 0) maxCols = std::min(maxCols, st.wrap);
    // لفّ صلب بعدد الأحرف مع إبقاء ألوان المقاطع
    std::vector<StyledLine> lines;
    for (const auto& sl : in) {
        StyledLine cur; int cols = 0;
        for (const auto& run : sl) {
            auto cps = rinfont::utf8Decode(run.text);
            std::string piece; int pcols = 0;
            for (char32_t cp : cps) {
                if (cols + pcols >= maxCols) {
                    if (!piece.empty()) cur.push_back({piece, run.col});
                    lines.push_back(cur); cur.clear(); piece.clear(); cols = 0; pcols = 0;
                }
                // إعادة ترميز UTF-8
                if (cp < 0x80) piece += (char)cp;
                else if (cp < 0x800) { piece += (char)(0xC0 | (cp >> 6)); piece += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { piece += (char)(0xE0 | (cp >> 12)); piece += (char)(0x80 | ((cp >> 6) & 0x3F)); piece += (char)(0x80 | (cp & 0x3F)); }
                else { piece += (char)(0xF0 | (cp >> 18)); piece += (char)(0x80 | ((cp >> 12) & 0x3F)); piece += (char)(0x80 | ((cp >> 6) & 0x3F)); piece += (char)(0x80 | (cp & 0x3F)); }
                ++pcols;
            }
            if (!piece.empty()) cur.push_back({piece, run.col});
            cols += pcols;
        }
        lines.push_back(cur);
    }
    int gutter = 0;
    if (st.lineNumbers) gutter = ((int)std::to_string(lines.size()).size() + 1) * cell;
    int widest = 0;
    for (const auto& sl : lines) { int cols = 0; for (const auto& r : sl) cols += textCols(r.text); widest = std::max(widest, cols * cell); }
    int titleH = st.title.empty() ? 0 : (kLineH * size + st.pad);
    int W = std::max(st.minW, gutter + widest + 2 * st.pad);
    int H = titleH + (int)lines.size() * kLineH * size + 2 * st.pad - (size > 0 ? size : 0);
    W = std::max(W, 1); H = std::max(H, 1);
    Canvas c; c.w = std::min(W, kMaxSide); c.h = std::min(H, kMaxSide);
    c.px.assign((size_t)c.w * c.h * 4, 0);
    drawRect(c, 0, 0, c.w, c.h, st.bg, true, 0, 0);
    int y = st.pad;
    if (titleH) { drawLineText(c, st.pad, y, st.title, st.titleCol, size); y += titleH; }
    int n = 1;
    for (const auto& sl : lines) {
        int x = st.pad;
        if (st.lineNumbers) { std::string num = std::to_string(n++); drawLineText(c, x + (int)(gutter - (num.size() + 1) * cell), y, num, st.titleCol, size); }
        x += gutter;
        for (const auto& r : sl) x += drawLineText(c, x, y, r.text, r.col, size);
        y += kLineH * size;
    }
    return c;
}

static const Color& levelColor(const std::string& lv) {
    static const Color info{0x58, 0xa6, 0xff, 255}, warn{0xd2, 0x99, 0x22, 255}, err{0xf8, 0x51, 0x49, 255},
                       dbg{0x8b, 0x94, 0x9e, 255}, other{0xc9, 0xd1, 0xd9, 255};
    if (lv == "info") return info;
    if (lv == "warn") return warn;
    if (lv == "error") return err;
    if (lv == "debug") return dbg;
    return other;
}

Canvas renderLog(const std::vector<LogEntry>& entries, const TextStyle& st, bool showSource, const std::string& onlyLevel, int last) {
    std::vector<StyledLine> lines;
    size_t from = 0;
    std::vector<const LogEntry*> sel;
    for (const auto& e : entries) if (onlyLevel.empty() || e.level == onlyLevel) sel.push_back(&e);
    if (last > 0 && sel.size() > (size_t)last) from = sel.size() - (size_t)last;
    static const Color dim{0x6e, 0x76, 0x81, 255};
    for (size_t i = from; i < sel.size(); ++i) {
        const LogEntry& e = *sel[i];
        std::string up = e.level; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
        StyledLine sl;
        sl.push_back({e.time + " ", dim});
        sl.push_back({"[" + up + "] ", levelColor(e.level)});
        sl.push_back({e.message, st.fg});
        if (showSource) sl.push_back({"  (" + e.source + ":" + std::to_string(e.line) + ")", dim});
        lines.push_back(sl);
    }
    if (lines.empty()) lines.push_back({{"(empty log)", dim}});
    return renderStyled(lines, st);
}

// الدالة التي يستعملها logSave("x.png"): تكتب سجلّ print.log كصورة بالصيغة المستنتجة من الامتداد.
std::string logAsImageBytes(const std::vector<LogEntry>& entries, const std::string& path) {
    TextStyle st; st.title = "Rin log  -  " + std::to_string(entries.size()) + " entries";
    Canvas c = renderLog(entries, st, true, "", 0);
    return encodeAs(c, formatFromPath(path));
}

// ---------------------------------------------------------------- مساعدات الوسائط
static Color colorArg(const Value& v, const char* fn, int line, const char* what = "color") {
    Color c;
    if (v.type == Value::Type::STRING) {
        if (rincolor::tryParseColor(v.str, c)) return c;
        throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': invalid " + what + " \"" + v.str + "\" (use #rrggbb, rgb(), hsl() or a CSS name)");
    }
    if (v.type == Value::Type::NUMBER) {
        long n = (long)v.number;
        if (n < 0 || n > 0xFFFFFF) throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': numeric color must be 0xRRGGBB");
        return Color{(uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n, 255};
    }
    if (v.type == Value::Type::ARRAY && (v.array->size() == 3 || v.array->size() == 4)) {
        int ch[4] = {0, 0, 0, 255};
        for (size_t i = 0; i < v.array->size(); ++i) {
            if ((*v.array)[i].type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': color array must hold numbers [r,g,b,a?]");
            ch[i] = std::max(0, std::min(255, (int)std::lround((*v.array)[i].number)));
        }
        return Color{(uint8_t)ch[0], (uint8_t)ch[1], (uint8_t)ch[2], (uint8_t)ch[3]};
    }
    throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': " + what + " must be a string, 0xRRGGBB number or [r,g,b,a?] array but got " + v.typeName());
}

static std::string hexOf(const uint8_t* p) {
    static const char* H = "0123456789abcdef";
    std::string s = "#";
    int n = p[3] == 255 ? 3 : 4;
    for (int i = 0; i < n; ++i) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
    return s;
}

static const Value* optGet(const Value& m, const char* key) {
    if (m.type != Value::Type::MAP) return nullptr;
    return m.map->findValue(Value::string(key));
}
static double optNum(const Value& m, const char* key, double def, const char* fn, int line) {
    const Value* v = optGet(m, key);
    if (!v || v->type == Value::Type::NIL) return def;
    return asNumber(*v, std::string(fn) + "." + key, line);
}
static bool optBool(const Value& m, const char* key, bool def) {
    const Value* v = optGet(m, key);
    return (!v || v->type == Value::Type::NIL) ? def : v->isTruthy();
}
static std::string optStr(const Value& m, const char* key, const std::string& def, const char* fn, int line) {
    const Value* v = optGet(m, key);
    if (!v || v->type == Value::Type::NIL) return def;
    return asString(*v, std::string(fn) + "." + key, line);
}

static TextStyle styleFrom(const Value& o, const char* fn, int line) {
    TextStyle st;
    st.size = std::max(1, std::min(32, (int)optNum(o, "size", 2, fn, line)));
    st.pad = std::max(0, std::min(512, (int)optNum(o, "padding", 16, fn, line)));
    st.wrap = std::max(0, (int)optNum(o, "wrap", 0, fn, line));
    st.minW = std::max(0, std::min(kMaxSide, (int)optNum(o, "minWidth", 0, fn, line)));
    if (const Value* v = optGet(o, "fg")) if (v->type != Value::Type::NIL) st.fg = colorArg(*v, fn, line, "fg");
    if (const Value* v = optGet(o, "bg")) if (v->type != Value::Type::NIL) st.bg = colorArg(*v, fn, line, "bg");
    st.title = optStr(o, "title", "", fn, line);
    st.lineNumbers = optBool(o, "lineNumbers", false);
    return st;
}

} // namespace imgdraw

void Interpreter::registerNativesImageDraw() {
    using namespace imgdraw;
    auto& S = store();

    auto count = [](std::vector<Value>& a, const char* fn, size_t lo, size_t hi, int line) {
        if (a.size() < lo || a.size() > hi)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "' expects " +
                (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) + " arguments but got " + std::to_string(a.size()));
    };
    auto canvasOf = [&S](const Value& v, const char* fn, int line) -> Canvas& {
        if (v.type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': first argument must be an image id (from imgNew/imgLoad) but got " + v.typeName());
        auto it = S.find((int)v.number);
        if (it == S.end()) throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': unknown or freed image id " + std::to_string((int)v.number));
        return it->second;
    };
    auto create = [&S](int w, int h, const Color* bg, const char* fn, int line) -> int {
        if (w < 1 || h < 1 || w > kMaxSide || h > kMaxSide || (double)w * h > kMaxPixels)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': invalid size " + std::to_string(w) + "x" + std::to_string(h) + " (each side 1.." + std::to_string(kMaxSide) + ", max 32 megapixels)");
        if (S.size() >= kMaxLive)
            throw diagErr(diag::Code::E0035_RuntimeError, line, std::string("'") + fn + "': too many live images (" + std::to_string(kMaxLive) + "); release some with imgFree(id)");
        Canvas c; c.w = w; c.h = h; c.px.assign((size_t)w * h * 4, 0);
        if (bg) for (size_t i = 0; i < (size_t)w * h; ++i) { c.px[i * 4] = bg->r; c.px[i * 4 + 1] = bg->g; c.px[i * 4 + 2] = bg->b; c.px[i * 4 + 3] = bg->a; }
        int id = g_nextId++;
        S[id] = std::move(c);
        return id;
    };
    auto adopt = [&S, create](Canvas&& c, const char* fn, int line) -> Value {
        int id = create(c.w, c.h, nullptr, fn, line);
        S[id] = std::move(c);
        return Value::num(id);
    };
    auto num = [](std::vector<Value>& a, size_t i, const char* fn, int line) { return asNumber(a[i], fn, line); };
    // ملء/خطّ: الوسيط الاختياري قد يكون bool (fill) أو map خيارات {fill,width,radius}
    struct Shape { bool fill = true; double width = 1, radius = 0; };
    auto shapeOpt = [](std::vector<Value>& a, size_t i, const char* fn, int line) {
        Shape s;
        if (a.size() > i) {
            if (a[i].type == Value::Type::BOOL) s.fill = a[i].boolean;
            else if (a[i].type == Value::Type::MAP) {
                s.fill = optBool(a[i], "fill", true);
                s.width = std::max(0.5, optNum(a[i], "width", 1, fn, line));
                s.radius = std::max(0.0, optNum(a[i], "radius", 0, fn, line));
            } else if (a[i].type != Value::Type::NIL)
                throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': last argument must be a bool (fill) or an options map");
        }
        return s;
    };

    // ---- إنشاء ----
    natives["imgNew"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgNew", 2, 3, line);
        Color bg{0, 0, 0, 0}; bool has = a.size() == 3 && a[2].type != Value::Type::NIL;
        if (has) bg = colorArg(a[2], "imgNew", line, "background");
        return Value::num(create((int)num(a, 0, "imgNew", line), (int)num(a, 1, "imgNew", line), has ? &bg : nullptr, "imgNew", line));
    };
    natives["imgLoad"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgLoad", 1, 1, line);
        std::string path = asString(a[0], "imgLoad", line);
        std::ifstream in(resolvePath(path, line), std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "imgLoad: could not open image '" + path + "' for reading (missing or not accessible)");
        std::ostringstream ss; ss << in.rdbuf(); std::string bytes = ss.str();
        int w = 0, h = 0, n = 0;
        if (!stbi_info_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &n) || w <= 0 || h <= 0)
            throw diagErr(diag::Code::E0035_RuntimeError, line, "imgLoad: '" + path + "' is not a supported image (PNG, JPEG, BMP or GIF)");
        if ((double)w * h > kMaxPixels)
            throw diagErr(diag::Code::E0035_RuntimeError, line, "imgLoad: image too large (" + std::to_string(w) + "x" + std::to_string(h) + "; limit 32 megapixels)");
        unsigned char* d = stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
        if (!d) throw diagErr(diag::Code::E0035_RuntimeError, line, std::string("imgLoad: could not decode '") + path + "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
        Canvas c; c.w = w; c.h = h; c.px.assign(d, d + (size_t)w * h * 4);
        stbi_image_free(d);
        return adopt(std::move(c), "imgLoad", line);
    };
    natives["imgCopy"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgCopy", 1, 1, line);
        Canvas c = canvasOf(a[0], "imgCopy", line);
        return adopt(std::move(c), "imgCopy", line);
    };
    natives["imgFree"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgFree", 1, 1, line);
        canvasOf(a[0], "imgFree", line);
        store().erase((int)a[0].number);
        return Value::boolean_(true);
    };

    // ---- معلومات ----
    natives["imgSize"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgSize", 1, 1, line);
        Canvas& c = canvasOf(a[0], "imgSize", line);
        MapData m; m.push_back({Value::string("width"), Value::num(c.w)}); m.push_back({Value::string("height"), Value::num(c.h)});
        return Value::makeMap(std::make_shared<MapData>(std::move(m)));
    };
    natives["imgPixel"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgPixel", 3, 4, line);
        Canvas& c = canvasOf(a[0], "imgPixel", line);
        int x = (int)std::floor(num(a, 1, "imgPixel", line)), y = (int)std::floor(num(a, 2, "imgPixel", line));
        if (a.size() == 4) {
            Color col = colorArg(a[3], "imgPixel", line);
            if (x >= 0 && y >= 0 && x < c.w && y < c.h) { uint8_t* p = &c.px[((size_t)y * c.w + x) * 4]; p[0] = col.r; p[1] = col.g; p[2] = col.b; p[3] = col.a; }
            return Value::nil();
        }
        if (x < 0 || y < 0 || x >= c.w || y >= c.h) return Value::nil();
        return Value::string(hexOf(&c.px[((size_t)y * c.w + x) * 4]));
    };

    // ---- رسم ----
    natives["imgClear"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgClear", 1, 2, line);
        Canvas& c = canvasOf(a[0], "imgClear", line);
        Color col = a.size() == 2 ? colorArg(a[1], "imgClear", line) : Color{0, 0, 0, 0};
        for (size_t i = 0; i < (size_t)c.w * c.h; ++i) { c.px[i * 4] = col.r; c.px[i * 4 + 1] = col.g; c.px[i * 4 + 2] = col.b; c.px[i * 4 + 3] = col.a; }
        return Value::nil();
    };
    natives["imgLine"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgLine", 6, 7, line);
        Canvas& c = canvasOf(a[0], "imgLine", line);
        drawLine(c, num(a, 1, "imgLine", line) + 0.5, num(a, 2, "imgLine", line) + 0.5, num(a, 3, "imgLine", line) + 0.5, num(a, 4, "imgLine", line) + 0.5,
                 colorArg(a[5], "imgLine", line), a.size() == 7 ? num(a, 6, "imgLine", line) : 1.0);
        return Value::nil();
    };
    natives["imgRect"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgRect", 6, 7, line);
        Canvas& c = canvasOf(a[0], "imgRect", line);
        Shape s = shapeOpt(a, 6, "imgRect", line);
        drawRect(c, num(a, 1, "imgRect", line), num(a, 2, "imgRect", line), num(a, 3, "imgRect", line), num(a, 4, "imgRect", line),
                 colorArg(a[5], "imgRect", line), s.fill, s.width, s.radius);
        return Value::nil();
    };
    natives["imgCircle"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgCircle", 5, 6, line);
        Canvas& c = canvasOf(a[0], "imgCircle", line);
        Shape s = shapeOpt(a, 5, "imgCircle", line);
        double r = num(a, 3, "imgCircle", line);
        drawEllipse(c, num(a, 1, "imgCircle", line) + 0.5, num(a, 2, "imgCircle", line) + 0.5, r, r, colorArg(a[4], "imgCircle", line), s.fill, s.width);
        return Value::nil();
    };
    natives["imgEllipse"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgEllipse", 6, 7, line);
        Canvas& c = canvasOf(a[0], "imgEllipse", line);
        Shape s = shapeOpt(a, 6, "imgEllipse", line);
        drawEllipse(c, num(a, 1, "imgEllipse", line) + 0.5, num(a, 2, "imgEllipse", line) + 0.5, num(a, 3, "imgEllipse", line), num(a, 4, "imgEllipse", line),
                    colorArg(a[5], "imgEllipse", line), s.fill, s.width);
        return Value::nil();
    };
    natives["imgPolygon"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgPolygon", 3, 4, line);
        Canvas& c = canvasOf(a[0], "imgPolygon", line);
        if (a[1].type != Value::Type::ARRAY || a[1].array->size() < 2)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'imgPolygon': points must be an array of [x,y] pairs (at least 2)");
        std::vector<std::pair<double, double>> pts;
        for (const auto& p : *a[1].array) {
            if (p.type != Value::Type::ARRAY || p.array->size() != 2 || (*p.array)[0].type != Value::Type::NUMBER || (*p.array)[1].type != Value::Type::NUMBER)
                throw diagErr(diag::Code::E0004_InvalidType, line, "'imgPolygon': every point must be [x, y] with numbers");
            pts.push_back({(*p.array)[0].number + 0.5, (*p.array)[1].number + 0.5});
        }
        Color col = colorArg(a[2], "imgPolygon", line);
        Shape s = shapeOpt(a, 3, "imgPolygon", line);
        if (a.size() < 4) s.fill = true;
        if (s.fill) fillPolygon(c, pts, col);
        else for (size_t i = 0; i < pts.size(); ++i) { auto p = pts[i], q = pts[(i + 1) % pts.size()]; drawLine(c, p.first, p.second, q.first, q.second, col, s.width); }
        return Value::nil();
    };
    natives["imgText"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgText", 5, 6, line);
        Canvas& c = canvasOf(a[0], "imgText", line);
        std::string text = a[3].type == Value::Type::STRING ? a[3].str : a[3].toDisplayString();
        Color col = colorArg(a[4], "imgText", line);
        Value opts = a.size() == 6 ? a[5] : Value::nil();
        int size = std::max(1, std::min(64, (int)optNum(opts, "size", 2, "imgText", line)));
        std::string align = optStr(opts, "align", "left", "imgText", line);
        if (align != "left" && align != "center" && align != "right")
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imgText': align must be left, center or right");
        bool hasBg = optGet(opts, "bg") && optGet(opts, "bg")->type != Value::Type::NIL;
        auto lines = splitLines(text);
        int x = (int)std::lround(num(a, 1, "imgText", line)), y = (int)std::lround(num(a, 2, "imgText", line));
        int widest = 0;
        for (auto& l : lines) widest = std::max(widest, textCols(l) * kAdvance * size);
        int totalH = (int)lines.size() * kLineH * size - 2 * size;
        if (hasBg) {
            Color bg = colorArg(*optGet(opts, "bg"), "imgText", line, "bg");
            int pad = (int)optNum(opts, "padding", size * 2, "imgText", line);
            int bx = align == "left" ? x : (align == "center" ? x - widest / 2 : x - widest);
            drawRect(c, bx - pad, y - pad, widest + 2 * pad, totalH + 2 * pad, bg, true, 0, (double)optNum(opts, "radius", 0, "imgText", line));
        }
        int yy = y;
        for (auto& l : lines) {
            int w = textCols(l) * kAdvance * size;
            int lx = align == "left" ? x : (align == "center" ? x - w / 2 : x - w);
            drawLineText(c, lx, yy, l, col, size);
            yy += kLineH * size;
        }
        MapData m; m.push_back({Value::string("width"), Value::num(widest)}); m.push_back({Value::string("height"), Value::num(totalH)});
        return Value::makeMap(std::make_shared<MapData>(std::move(m)));
    };
    natives["imgTextSize"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgTextSize", 1, 2, line);
        std::string text = a[0].type == Value::Type::STRING ? a[0].str : a[0].toDisplayString();
        int size = a.size() == 2 ? std::max(1, std::min(64, (int)num(a, 1, "imgTextSize", line))) : 2;
        auto lines = splitLines(text);
        int widest = 0;
        for (auto& l : lines) widest = std::max(widest, textCols(l) * kAdvance * size);
        MapData m; m.push_back({Value::string("width"), Value::num(widest)});
        m.push_back({Value::string("height"), Value::num((int)lines.size() * kLineH * size - 2 * size)});
        return Value::makeMap(std::make_shared<MapData>(std::move(m)));
    };
    natives["imgGradient"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgGradient", 3, 4, line);   // (id, c1, c2[, "v"|"h"|"d"])
        Canvas& c = canvasOf(a[0], "imgGradient", line);
        Color c1 = colorArg(a[1], "imgGradient", line), c2 = colorArg(a[2], "imgGradient", line);
        std::string dir = a.size() == 4 ? asString(a[3], "imgGradient", line) : "v";
        if (dir != "v" && dir != "h" && dir != "d") throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imgGradient': direction must be \"v\", \"h\" or \"d\"");
        for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w; ++x) {
            double t = dir == "v" ? (c.h > 1 ? (double)y / (c.h - 1) : 0) : dir == "h" ? (c.w > 1 ? (double)x / (c.w - 1) : 0)
                     : ((double)x / std::max(1, c.w - 1) + (double)y / std::max(1, c.h - 1)) / 2;
            uint8_t* p = &c.px[((size_t)y * c.w + x) * 4];
            p[0] = (uint8_t)std::lround(c1.r + (c2.r - c1.r) * t); p[1] = (uint8_t)std::lround(c1.g + (c2.g - c1.g) * t);
            p[2] = (uint8_t)std::lround(c1.b + (c2.b - c1.b) * t); p[3] = (uint8_t)std::lround(c1.a + (c2.a - c1.a) * t);
        }
        return Value::nil();
    };
    natives["imgFloodFill"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgFloodFill", 4, 4, line);
        Canvas& c = canvasOf(a[0], "imgFloodFill", line);
        int sx = (int)num(a, 1, "imgFloodFill", line), sy = (int)num(a, 2, "imgFloodFill", line);
        Color col = colorArg(a[3], "imgFloodFill", line);
        if (sx < 0 || sy < 0 || sx >= c.w || sy >= c.h) return Value::num(0);
        uint8_t tgt[4]; std::memcpy(tgt, &c.px[((size_t)sy * c.w + sx) * 4], 4);
        if (tgt[0] == col.r && tgt[1] == col.g && tgt[2] == col.b && tgt[3] == col.a) return Value::num(0);
        std::vector<std::pair<int, int>> st{{sx, sy}}; long n = 0;
        auto same = [&](int x, int y) { return std::memcmp(&c.px[((size_t)y * c.w + x) * 4], tgt, 4) == 0; };
        while (!st.empty()) {
            auto [x, y] = st.back(); st.pop_back();
            if (!same(x, y)) continue;
            int l = x, r = x;
            while (l > 0 && same(l - 1, y)) --l;
            while (r < c.w - 1 && same(r + 1, y)) ++r;
            for (int i = l; i <= r; ++i) { uint8_t* p = &c.px[((size_t)y * c.w + i) * 4]; p[0] = col.r; p[1] = col.g; p[2] = col.b; p[3] = col.a; ++n; }
            for (int ny : {y - 1, y + 1}) if (ny >= 0 && ny < c.h) for (int i = l; i <= r; ++i) if (same(i, ny)) st.push_back({i, ny});
        }
        return Value::num((double)n);
    };

    // ---- تحويل ----
    natives["imgResize"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgResize", 3, 3, line);
        Canvas& s = canvasOf(a[0], "imgResize", line);
        int nw = (int)num(a, 1, "imgResize", line), nh = (int)num(a, 2, "imgResize", line);
        int id = create(nw, nh, nullptr, "imgResize", line);
        Canvas& d = store()[id];
        for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) {
            double fx = std::max(0.0, std::min((x + 0.5) * s.w / nw - 0.5, s.w - 1.0)), fy = std::max(0.0, std::min((y + 0.5) * s.h / nh - 0.5, s.h - 1.0));
            int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, s.w - 1), y1 = std::min(y0 + 1, s.h - 1);
            double tx = fx - x0, ty = fy - y0, acc[3] = {0, 0, 0}, aa = 0;
            const int xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
            const double ws[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
            for (int k = 0; k < 4; ++k) {
                const uint8_t* p = &s.px[((size_t)ys[k] * s.w + xs[k]) * 4]; double al = p[3] / 255.0 * ws[k];
                acc[0] += p[0] * al; acc[1] += p[1] * al; acc[2] += p[2] * al; aa += al;
            }
            uint8_t* o = &d.px[((size_t)y * nw + x) * 4];
            if (aa > 0) { o[0] = (uint8_t)std::lround(acc[0] / aa); o[1] = (uint8_t)std::lround(acc[1] / aa); o[2] = (uint8_t)std::lround(acc[2] / aa); }
            o[3] = (uint8_t)std::lround(std::min(1.0, aa) * 255);
        }
        return Value::num(id);
    };
    natives["imgCrop"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgCrop", 5, 5, line);
        Canvas& s = canvasOf(a[0], "imgCrop", line);
        int x = (int)num(a, 1, "imgCrop", line), y = (int)num(a, 2, "imgCrop", line), w = (int)num(a, 3, "imgCrop", line), h = (int)num(a, 4, "imgCrop", line);
        int id = create(w, h, nullptr, "imgCrop", line);
        Canvas& d = store()[id];
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            int sx = x + xx, sy = y + yy;
            if (sx >= 0 && sy >= 0 && sx < s.w && sy < s.h) std::memcpy(&d.px[((size_t)yy * w + xx) * 4], &s.px[((size_t)sy * s.w + sx) * 4], 4);
        }
        return Value::num(id);
    };
    natives["imgPaste"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgPaste", 4, 5, line);       // (dst, src, x, y[, opacity 0..1])
        Canvas& d = canvasOf(a[0], "imgPaste", line);
        Canvas s = canvasOf(a[1], "imgPaste", line);   // نسخة: يصحّ لصق صورة على نفسها
        int ox = (int)num(a, 2, "imgPaste", line), oy = (int)num(a, 3, "imgPaste", line);
        double op = a.size() == 5 ? std::max(0.0, std::min(1.0, num(a, 4, "imgPaste", line))) : 1.0;
        for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x) {
            const uint8_t* p = &s.px[((size_t)y * s.w + x) * 4];
            blendPx(d, ox + x, oy + y, Color{p[0], p[1], p[2], (uint8_t)std::lround(p[3] * op)}, 1.0);
        }
        return Value::nil();
    };
    natives["imgFlip"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgFlip", 2, 2, line);
        Canvas& c = canvasOf(a[0], "imgFlip", line);
        std::string dir = asString(a[1], "imgFlip", line);
        if (dir == "h") { for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w / 2; ++x) for (int k = 0; k < 4; ++k) std::swap(c.px[((size_t)y * c.w + x) * 4 + k], c.px[((size_t)y * c.w + (c.w - 1 - x)) * 4 + k]); }
        else if (dir == "v") { for (int y = 0; y < c.h / 2; ++y) for (int x = 0; x < c.w * 4; ++x) std::swap(c.px[(size_t)y * c.w * 4 + x], c.px[(size_t)(c.h - 1 - y) * c.w * 4 + x]); }
        else throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imgFlip': direction must be \"h\" or \"v\"");
        return Value::nil();
    };
    natives["imgFilter"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgFilter", 2, 3, line);      // grayscale · invert · brightness(n -255..255) · opacity(0..1)
        Canvas& c = canvasOf(a[0], "imgFilter", line);
        std::string f = asString(a[1], "imgFilter", line);
        double v = a.size() == 3 ? num(a, 2, "imgFilter", line) : 0;
        if (f != "grayscale" && f != "invert" && f != "brightness" && f != "opacity")
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imgFilter': unknown filter \"" + f + "\" (grayscale, invert, brightness, opacity)");
        for (size_t i = 0; i < (size_t)c.w * c.h; ++i) {
            uint8_t* p = &c.px[i * 4];
            if (f == "grayscale") { uint8_t g = (uint8_t)std::lround(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]); p[0] = p[1] = p[2] = g; }
            else if (f == "invert") { p[0] = 255 - p[0]; p[1] = 255 - p[1]; p[2] = 255 - p[2]; }
            else if (f == "brightness") for (int k = 0; k < 3; ++k) p[k] = (uint8_t)std::max(0, std::min(255, (int)std::lround(p[k] + v)));
            else p[3] = (uint8_t)std::lround(p[3] * std::max(0.0, std::min(1.0, v)));
        }
        return Value::nil();
    };

    // ---- إخراج ----
    natives["imgEncode"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgEncode", 1, 2, line);
        Canvas& c = canvasOf(a[0], "imgEncode", line);
        std::string fmt = a.size() == 2 ? lowerStr(asString(a[1], "imgEncode", line)) : "png";
        std::string out = encodeAs(c, fmt);
        if (out.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imgEncode': unsupported format \"" + fmt + "\" (png, bmp, ppm)");
        return Value::string(out);
    };
    natives["imgSave"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgSave", 2, 3, line);
        Canvas& c = canvasOf(a[0], "imgSave", line);
        std::string path = asString(a[1], "imgSave", line);
        std::string fmt = a.size() == 3 ? lowerStr(asString(a[2], "imgSave", line)) : formatFromPath(path);
        if (fmt.empty()) fmt = "png";
        std::string bytes = encodeAs(c, fmt);
        if (bytes.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imgSave': unsupported format \"" + fmt + "\" (png, bmp, ppm) — use a .png/.bmp/.ppm path or pass the format");
        writeRealFile(path, bytes, line, "imgSave");
        return Value::num((double)bytes.size());
    };
    auto ansiOf = [=, this](std::vector<Value>& a, const char* fn, int line) -> std::string {
        count(a, fn, 1, 3, line);
        Canvas& c = canvasOf(a[0], fn, line);
        int cols = std::min(c.w, 80);
        if (a.size() >= 2 && a[1].type != Value::Type::NIL) {
            cols = (int)num(a, 1, fn, line);
            if (cols < 1 || cols > 1000) throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': width must be between 1 and 1000 columns");
        } else cols = std::min(c.w, imgansi::terminalColumns());
        std::string mode = a.size() == 3 && a[2].type != Value::Type::NIL ? asString(a[2], fn, line) : "auto";
        if (mode != "auto" && mode != "color" && mode != "blocks" && mode != "ascii")
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': unknown mode \"" + mode + "\" — use auto, color, blocks or ascii");
        if (mode == "auto") mode = imgansi::colorOutputOn() ? "color" : "ascii";
        imgansi::Image im; im.w = c.w; im.h = c.h; im.px = c.px;
        return imgansi::render(im, cols, mode);
    };
    natives["imgAnsi"] = [=, this](std::vector<Value>& a, int line) -> Value { return Value::string(ansiOf(a, "imgAnsi", line)); };
    natives["imgPrint"] = [=, this](std::vector<Value>& a, int line) -> Value { output << ansiOf(a, "imgPrint", line) << "\n"; return Value::nil(); };

    // ---- نص / سجلّ → صورة ----
    natives["imgFromText"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgFromText", 1, 2, line);
        std::string text = a[0].type == Value::Type::STRING ? a[0].str : a[0].toDisplayString();
        TextStyle st = styleFrom(a.size() == 2 ? a[1] : Value::nil(), "imgFromText", line);
        std::vector<StyledLine> lines;
        for (auto& l : splitLines(text)) lines.push_back({{l, st.fg}});
        return adopt(renderStyled(lines, st), "imgFromText", line);
    };
    natives["imgFromLog"] = [=, this](std::vector<Value>& a, int line) -> Value {
        count(a, "imgFromLog", 0, 2, line);     // () | (opts) | (entries) | (entries, opts)
        Value entries = Value::nil(), opts = Value::nil();
        for (auto& v : a) { if (v.type == Value::Type::ARRAY) entries = v; else if (v.type == Value::Type::MAP) opts = v; else if (v.type != Value::Type::NIL) throw diagErr(diag::Code::E0004_InvalidType, line, "'imgFromLog': arguments are (entries?, options?)"); }
        TextStyle st = styleFrom(opts, "imgFromLog", line);
        if (st.title.empty() && !(optGet(opts, "title"))) st.title = "Rin log";
        std::vector<LogEntry> use;
        if (entries.type == Value::Type::ARRAY) {
            for (const auto& e : *entries.array) {
                if (e.type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'imgFromLog': entries must be maps like logHistory() returns");
                LogEntry le;
                le.time = optStr(e, "time", "", "imgFromLog", line); le.level = optStr(e, "level", "log", "imgFromLog", line);
                le.message = optStr(e, "message", "", "imgFromLog", line); le.source = optStr(e, "source", "", "imgFromLog", line);
                le.line = (int)optNum(e, "line", 0, "imgFromLog", line);
                use.push_back(le);
            }
        } else use = logHistory_;
        return adopt(renderLog(use, st, optBool(opts, "source", true), optStr(opts, "level", "", "imgFromLog", line), (int)optNum(opts, "last", 0, "imgFromLog", line)), "imgFromLog", line);
    };
}

} // namespace rin
