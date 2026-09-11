#include "rin_artifact.h"
#include "clc/sha256.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace rin::artifact {
namespace {
QrBridge g_qrBridge = nullptr;

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; case '\'': o += "&apos;"; break; default: o += c; }
    }
    return o;
}

// Code 128-B: complete printable ASCII subset, including start/stop/checksum.
static const int C128[107][7] = {
{2,1,2,2,2,2,0},
{2,2,2,1,2,2,0},
{2,2,2,2,2,1,0},
{1,2,1,2,2,3,0},
{1,2,1,3,2,2,0},
{1,3,1,2,2,2,0},
{1,2,2,2,1,3,0},
{1,2,2,3,1,2,0},
{1,3,2,2,1,2,0},
{2,2,1,2,1,3,0},
{2,2,1,3,1,2,0},
{2,3,1,2,1,2,0},
{1,1,2,2,3,2,0},
{1,2,2,1,3,2,0},
{1,2,2,2,3,1,0},
{1,1,3,2,2,2,0},
{1,2,3,1,2,2,0},
{1,2,3,2,2,1,0},
{2,2,3,2,1,1,0},
{2,2,1,1,3,2,0},
{2,2,1,2,3,1,0},
{2,1,3,2,1,2,0},
{2,2,3,1,1,2,0},
{3,1,2,1,3,1,0},
{3,1,1,2,2,2,0},
{3,2,1,1,2,2,0},
{3,2,1,2,2,1,0},
{3,1,2,2,1,2,0},
{3,2,2,1,1,2,0},
{3,2,2,2,1,1,0},
{2,1,2,1,2,3,0},
{2,1,2,3,2,1,0},
{2,3,2,1,2,1,0},
{1,1,1,3,2,3,0},
{1,3,1,1,2,3,0},
{1,3,1,3,2,1,0},
{1,1,2,3,1,3,0},
{1,3,2,1,1,3,0},
{1,3,2,3,1,1,0},
{2,1,1,3,1,3,0},
{2,3,1,1,1,3,0},
{2,3,1,3,1,1,0},
{1,1,2,1,3,3,0},
{1,1,2,3,3,1,0},
{1,3,2,1,3,1,0},
{1,1,3,1,2,3,0},
{1,1,3,3,2,1,0},
{1,3,3,1,2,1,0},
{3,1,3,1,2,1,0},
{2,1,1,3,3,1,0},
{2,3,1,1,3,1,0},
{2,1,3,1,1,3,0},
{2,1,3,3,1,1,0},
{2,1,3,1,3,1,0},
{3,1,1,1,2,3,0},
{3,1,1,3,2,1,0},
{3,3,1,1,2,1,0},
{3,1,2,1,1,3,0},
{3,1,2,3,1,1,0},
{3,3,2,1,1,1,0},
{3,1,4,1,1,1,0},
{2,2,1,4,1,1,0},
{4,3,1,1,1,1,0},
{1,1,1,2,2,4,0},
{1,1,1,4,2,2,0},
{1,2,1,1,2,4,0},
{1,2,1,4,2,1,0},
{1,4,1,1,2,2,0},
{1,4,1,2,2,1,0},
{1,1,2,2,1,4,0},
{1,1,2,4,1,2,0},
{1,2,2,1,1,4,0},
{1,2,2,4,1,1,0},
{1,4,2,1,1,2,0},
{1,4,2,2,1,1,0},
{2,4,1,2,1,1,0},
{2,2,1,1,1,4,0},
{4,1,3,1,1,1,0},
{2,4,1,1,1,2,0},
{1,3,4,1,1,1,0},
{1,1,1,2,4,2,0},
{1,2,1,1,4,2,0},
{1,2,1,2,4,1,0},
{1,1,4,2,1,2,0},
{1,2,4,1,1,2,0},
{1,2,4,2,1,1,0},
{4,1,1,2,1,2,0},
{4,2,1,1,1,2,0},
{4,2,1,2,1,1,0},
{2,1,2,1,4,1,0},
{2,1,4,1,2,1,0},
{4,1,2,1,2,1,0},
{1,1,1,1,4,3,0},
{1,1,1,3,4,1,0},
{1,3,1,1,4,1,0},
{1,1,4,1,1,3,0},
{1,1,4,3,1,1,0},
{4,1,1,1,1,3,0},
{4,1,1,3,1,1,0},
{1,1,3,1,4,1,0},
{1,1,4,1,3,1,0},
{3,1,1,1,4,1,0},
{4,1,1,1,3,1,0},
{2,1,1,4,1,2,0},
{2,1,1,2,1,4,0},
{2,1,1,2,3,2,0},
{2,3,3,1,1,1,2},
};

static std::vector<int> code128Values(const std::string& data) {
    std::vector<int> v; v.push_back(104); // Start B
    int checksum = 104;
    for (size_t i = 0; i < data.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c < 32 || c > 126) throw std::runtime_error("barcode code128 supports printable ASCII only");
        int code = static_cast<int>(c) - 32;
        v.push_back(code); checksum += code * static_cast<int>(i + 1);
    }
    v.push_back(checksum % 103); v.push_back(106);
    return v;
}
} // namespace

std::string sanitizeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') out += c;
        else out += '_';
    }
    if (out.empty() || out == "." || out == "..") out = "artifact";
    return out;
}

std::string uniqueFilename(const std::string& prefix, const std::string& extension) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::ostringstream o; o << sanitizeName(prefix.empty() ? "artifact" : prefix) << "_" << ms;
    if (!extension.empty()) o << "." << (extension[0] == '.' ? extension.substr(1) : extension);
    return o.str();
}

std::string uuidV4() {
    std::random_device rd; std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> d;
    uint64_t a = d(gen), b = d(gen); b = (b & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
    a = (a & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    std::ostringstream o; o << std::hex << std::setfill('0')
      << std::setw(8) << uint32_t(a >> 32) << '-' << std::setw(4) << uint16_t(a >> 16)
      << '-' << std::setw(4) << uint16_t(a) << '-' << std::setw(4) << uint16_t(b >> 48)
      << '-' << std::setw(12) << (b & 0xffffffffffffULL);
    return o.str();
}

std::string sha256Hex(const std::string& data) { return clc::sha256_hex(clc::sha256(data)); }

std::string barcodeSvg(const std::string& data, const Options& o) {
    auto values = code128Values(data);
    int module = std::max(1, o.width / 110);
    int quiet = 10 * module;
    int totalModules = quiet * 2 / module;
    for (int code : values) for (int i = 0; i < 6 + (code == 106 ? 1 : 0); ++i) totalModules += C128[code][i];
    int width = std::max(o.width, totalModules * module);
    int height = std::max(60, o.height);
    std::ostringstream s;
    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height << "\">";
    s << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/><g fill=\"black\">";
    int x = quiet;
    for (int code : values) {
        int bars = (code == 106 ? 7 : 6);
        for (int i = 0; i < bars; ++i) {
            int w = C128[code][i] * module;
            if ((i & 1) == 0) s << "<rect x=\"" << x << "\" y=\"8\" width=\"" << w << "\" height=\"" << height - 35 << "\"/>";
            x += w;
        }
    }
    s << "</g><text x=\"" << width/2 << "\" y=\"" << height-8 << "\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"16\">" << esc(data) << "</text></svg>";
    return s.str();
}

void setQrBridge(QrBridge bridge) { g_qrBridge = bridge; }

std::string qrSvg(const std::string& data, const Options& o) {
    if (!g_qrBridge) throw std::runtime_error("qr requires the Rin Android QR bridge (ZXing) in this build");
    return g_qrBridge(data, std::max(64, o.size), 4);
}

} // namespace rin::artifact
