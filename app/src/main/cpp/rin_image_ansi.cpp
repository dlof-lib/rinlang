// ============================================================================
//  rin_image_ansi.cpp — طباعة الصور داخل الـ terminal كنص
// ----------------------------------------------------------------------------
//    printImage(path[, width[, mode]])   يطبع الصورة في الخرج (طرفية/ملف/كونسول) ويُرجع nil
//    imageAnsi(path[, width[, mode]])    يُرجع نفس النص كقيمة نصية (لحفظه/تمريره/طباعته لاحقاً)
//    imageInfo(path)                     {width, height, channels, format}
//
//  width = عدد الأعمدة (الافتراضي: min(عرض الصورة، عرض الطرفية)). الارتفاع يُحسب بالحفاظ على النسبة.
//  mode:
//    "auto"   (افتراضي) ألوان 24-bit إن كان الخرج طرفية أو FORCE_COLOR=1 (وبلا NO_COLOR)، وإلا "ascii"
//    "color"  نصف-كتل ▀ بلون أمامي/خلفي: بكسلان رأسيان لكل خلية (دقة أعلى مرتين)
//    "blocks" تدرّج رمادي بكتل ░▒▓█
//    "ascii"  تدرّج رمادي بحروف ASCII فقط (آمن لأي خرج: ملفات، كونسول أندرويد، CI)
//  الصيغ: PNG · JPEG · BMP · GIF (الإطار الأول). الشفافية: البكسل الشفاف يُترك فارغاً.
//  المفكّك: stb_image (Public Domain / MIT) في third_party/stb_image.h. تُضمَّن في نهاية rin_interpreter.cpp.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#ifndef _WIN32
#include <sys/ioctl.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#include "third_party/stb_image.h"

namespace rin {
namespace imgansi {

struct Rgba { uint8_t r, g, b, a; };
struct Image { int w = 0, h = 0; std::vector<uint8_t> px; /* RGBA */ };

static int terminalColumns() {
#ifndef _WIN32
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
#endif
    if (const char* c = std::getenv("COLUMNS")) { int v = std::atoi(c); if (v > 0) return v; }
    return 80;
}

static bool colorOutputOn() {
    const char* force = std::getenv("FORCE_COLOR");
    if (force && *force && std::strcmp(force, "0") != 0) return true;
    if (std::getenv("NO_COLOR")) return false;
    return rin_isatty(1) != 0;
}

static bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss; ss << in.rdbuf();
    const std::string s = ss.str();
    out.assign(s.begin(), s.end());
    return true;
}

static const char* formatName(const std::vector<unsigned char>& b) {
    if (b.size() > 4 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return "png";
    if (b.size() > 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return "jpeg";
    if (b.size() > 2 && b[0] == 'B' && b[1] == 'M') return "bmp";
    if (b.size() > 3 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') return "gif";
    return "";
}

// متوسط صندوقي (box filter) بحساب ألفا مسبق الضرب: جودة جيدة عند التصغير الكبير، ونسخ أقرب جار عند التكبير.
static std::vector<Rgba> resample(const Image& im, int tw, int th) {
    std::vector<Rgba> out((size_t)tw * th);
    for (int y = 0; y < th; ++y) {
        int y0 = (int)std::floor((double)y * im.h / th);
        int y1 = std::max(y0 + 1, (int)std::ceil((double)(y + 1) * im.h / th));
        y1 = std::min(y1, im.h); y0 = std::min(y0, im.h - 1);
        for (int x = 0; x < tw; ++x) {
            int x0 = (int)std::floor((double)x * im.w / tw);
            int x1 = std::max(x0 + 1, (int)std::ceil((double)(x + 1) * im.w / tw));
            x1 = std::min(x1, im.w); x0 = std::min(x0, im.w - 1);
            double sr = 0, sg = 0, sb = 0, sa = 0; int n = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const uint8_t* p = &im.px[((size_t)yy * im.w + xx) * 4];
                    double a = p[3] / 255.0;
                    sr += p[0] * a; sg += p[1] * a; sb += p[2] * a; sa += a; ++n;
                }
            Rgba o{0, 0, 0, 0};
            if (sa > 0 && n > 0) {
                o.r = (uint8_t)std::lround(sr / sa); o.g = (uint8_t)std::lround(sg / sa); o.b = (uint8_t)std::lround(sb / sa);
                o.a = (uint8_t)std::lround(255.0 * sa / n);
            }
            out[(size_t)y * tw + x] = o;
        }
    }
    return out;
}

static inline bool transparent(const Rgba& c) { return c.a < 90; }
static inline double luma(const Rgba& c) {
    double a = c.a / 255.0;
    return (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) / 255.0 * a;
}

static std::string render(const Image& im, int cols, const std::string& mode) {
    cols = std::max(1, cols);
    const double ratio = (double)im.h / im.w;
    std::ostringstream o;
    if (mode == "color") {
        int th = std::max(2, (int)std::lround(cols * ratio));
        if (th % 2) ++th;
        auto g = resample(im, cols, th);
        for (int row = 0; row < th / 2; ++row) {
            if (row) o << "\n";
            for (int x = 0; x < cols; ++x) {
                const Rgba& t = g[(size_t)(row * 2) * cols + x];
                const Rgba& b = g[(size_t)(row * 2 + 1) * cols + x];
                bool tt = transparent(t), bt = transparent(b);
                if (tt && bt) { o << "\x1b[0m "; continue; }
                if (tt) { o << "\x1b[0m\x1b[38;2;" << (int)b.r << ";" << (int)b.g << ";" << (int)b.b << "m\xE2\x96\x84"; continue; } // ▄
                if (bt) { o << "\x1b[0m\x1b[38;2;" << (int)t.r << ";" << (int)t.g << ";" << (int)t.b << "m\xE2\x96\x80"; continue; } // ▀
                o << "\x1b[38;2;" << (int)t.r << ";" << (int)t.g << ";" << (int)t.b << ";48;2;"
                  << (int)b.r << ";" << (int)b.g << ";" << (int)b.b << "m\xE2\x96\x80";
            }
            o << "\x1b[0m";
        }
        return o.str();
    }
    // رمادي: خلية الحرف أطول من عرضها بنحو الضعف
    int th = std::max(1, (int)std::lround(cols * ratio * 0.5));
    auto g = resample(im, cols, th);
    static const char* ascii = " .:-=+*#%@";
    static const char* blocks[5] = {" ", "\xE2\x96\x91", "\xE2\x96\x92", "\xE2\x96\x93", "\xE2\x96\x88"}; // ░▒▓█
    for (int y = 0; y < th; ++y) {
        if (y) o << "\n";
        for (int x = 0; x < cols; ++x) {
            const Rgba& c = g[(size_t)y * cols + x];
            double l = transparent(c) ? 0.0 : std::min(1.0, std::max(0.0, luma(c)));
            if (mode == "blocks") o << blocks[std::min(4, (int)(l * 5.0))];
            else o << ascii[std::min(9, (int)(l * 10.0))];
        }
    }
    return o.str();
}

} // namespace imgansi

void Interpreter::registerNativesImage() {
    using namespace imgansi;

    // يحمّل الصورة ويحوّلها لنص؛ يرمي أخطاء واضحة. fn = اسم الدالة للرسائل.
    auto build = [this](const char* fn, std::vector<Value>& a, int line) -> std::string {
        if (a.empty() || a.size() > 3)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "' expects 1 to 3 arguments (path, width?, mode?) but got " + std::to_string(a.size()));
        if (a[0].type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': the path must be a string, but got " + a[0].typeName());
        int cols = 0;
        if (a.size() >= 2 && a[1].type != Value::Type::NIL) {
            if (a[1].type != Value::Type::NUMBER)
                throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': width must be a number of columns (or nil)");
            cols = (int)a[1].number;
            if (cols < 1 || cols > 1000)
                throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': width must be between 1 and 1000 columns");
        }
        std::string mode = "auto";
        if (a.size() == 3 && a[2].type != Value::Type::NIL) {
            if (a[2].type != Value::Type::STRING)
                throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': mode must be a string");
            mode = a[2].str;
            if (mode != "auto" && mode != "color" && mode != "blocks" && mode != "ascii")
                throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': unknown mode \"" + mode + "\" — use auto, color, blocks or ascii");
        }
        const std::string path = a[0].str;
        std::vector<unsigned char> bytes;
        if (!readFileBytes(resolvePath(path, line), bytes))
            throw diagErr(diag::Code::E0036_IOFailure, line, std::string(fn) + ": could not open image '" + path + "' for reading (missing or not accessible)");
        int w = 0, h = 0, n = 0;
        if (!stbi_info_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n) || w <= 0 || h <= 0)
            throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": '" + path + "' is not a supported image (PNG, JPEG, BMP or GIF)");
        if ((double)w * h > 64e6)
            throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": image too large (" + std::to_string(w) + "x" + std::to_string(h) + "; limit 64 megapixels)");
        unsigned char* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
        if (!data)
            throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": could not decode '" + path + "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
        Image im; im.w = w; im.h = h; im.px.assign(data, data + (size_t)w * h * 4);
        stbi_image_free(data);
        if (cols == 0) cols = std::min(w, terminalColumns());
        if (mode == "auto") mode = colorOutputOn() ? "color" : "ascii";
        return render(im, cols, mode);
    };

    natives["imageAnsi"] = [build](std::vector<Value>& a, int line) -> Value {
        return Value::string(build("imageAnsi", a, line));
    };
    natives["printImage"] = [this, build](std::vector<Value>& a, int line) -> Value {
        this->output << build("printImage", a, line) << "\n";
        return Value::nil();
    };
    natives["imageInfo"] = [this](std::vector<Value>& a, int line) -> Value {
        if (a.size() != 1 || a[0].type != Value::Type::STRING)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'imageInfo' expects 1 string argument (path)");
        std::vector<unsigned char> bytes;
        if (!readFileBytes(resolvePath(a[0].str, line), bytes))
            throw diagErr(diag::Code::E0036_IOFailure, line, "imageInfo: could not open image '" + a[0].str + "' for reading (missing or not accessible)");
        int w = 0, h = 0, n = 0;
        if (!stbi_info_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "imageInfo: '" + a[0].str + "' is not a supported image (PNG, JPEG, BMP or GIF)");
        MapData out;
        out.push_back({Value::string("width"), Value::num(w)});
        out.push_back({Value::string("height"), Value::num(h)});
        out.push_back({Value::string("channels"), Value::num(n)});
        out.push_back({Value::string("format"), Value::string(formatName(bytes))});
        return Value::makeMap(std::make_shared<MapData>(std::move(out)));
    };
}

} // namespace rin
