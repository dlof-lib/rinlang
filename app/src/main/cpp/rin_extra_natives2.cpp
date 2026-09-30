// ============================================================================
//  rin_extra_natives2.cpp — Rin 1.2 native library additions
// ----------------------------------------------------------------------------
//  دفعة ثانية من الدوال الأصلية، فوق rin_extra_natives.cpp (Rin 1.1) دون تعديل أي
//  منهما. تُسجَّل من Interpreter::registerNativesExtra2()، تُستدعى مباشرة بعد
//  registerNativesExtra() في registerNatives() (rin_interpreter.cpp).
//
//  ملاحظة بناء: هذا الملف يُضمَّن (#include) في نهاية rin_interpreter.cpp بعد
//  rin_extra_natives.cpp، فيدخل تلقائياً في كل أهداف البناء (APK/CLI/WASM/CI) دون
//  تعديل قوائم الملفات، ويُعيد استخدام كل أدوات extra:: المُعرَّفة هناك (need/str/num/
//  arr/mp/newArray/newMap/mapFind/mapSet/deepCopyValue/codePoints...) عبر
//  `using namespace rin::extra;`.
//
//  المجموعات:
//    1) sec.*        — أمن: تجزئة (sha256/md5/crc32/hmacSha256)، ترميز (base64/hex)،
//                       رموز عشوائية، مقارنة بزمن ثابت، تقييم قوة كلمة مرور، rot13/xor
//    2) fs.*        — تحليل ملفات: بصمة ملف، عدّ أسطر، tail، grep بنمط regex، مسح
//                       مجلد (متكرر اختياري)، إحصاء الامتدادات، بحث بالاسم
//    3) net.*         — تحليل شبكي (بلا اتصال فعلي): تفكيك URL، استعلام query string،
//                       IPv4/IPv6، CIDR، كشف عناوين خاصة
//    4) log.*         — تحليل سجلات: تفكيك أسطر، Common/Combined Log Format، استخراج
//                       IPs وطوابع زمنية، تجميع/عدّ، أعلى-N، تصفية، عدّ حسب المستوى،
//                       كشف ارتفاعات مفاجئة بسيطة
//    5) automation.*  — أتمتة: إعادة محاولة مع تأخير، أنابيب دوال، قياس زمن التنفيذ،
//                       تقسيم إلى دفعات، تشغيل تسلسلي، إيقاف مؤقت محدود السقف
//    6) container.*   — امتدادات: لقطة/استعادة، قفل/فتح، إحصاءات، تصفية بدالة، تحقق
//                       من مخطط (schema)، تصدير/استيراد كملف JSON فعلي، سجل أحداث
// ============================================================================
#include "rin_interpreter.h"
#include "clc/sha256.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <thread>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);
static std::regex compileRinRegex(const std::string& rawPattern, const std::string& fn, int line);

namespace extra2 {

using Args = std::vector<Value>;
using namespace rin::extra; // need/str/num/arr/mp/newArray/newMap/mapFind/mapSet/deepCopyValue/
                             // deepMergeValue/codePoints/clampIndex/joinCodePoints

// ============================================================ 1) sec.* helpers

std::string toHex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[(data[i] >> 4) & 0xF];
        out += digits[data[i] & 0xF];
    }
    return out;
}

bool fromHex(const std::string& s, std::string& out) {
    if (s.size() % 2 != 0) return false;
    out.clear();
    out.reserve(s.size() / 2);
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        int hi = val(s[i]), lo = val(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out += static_cast<char>((hi << 4) | lo);
    }
    return true;
}

std::string sha256Hex(const std::string& s) { return clc::sha256_hex(clc::sha256(s)); }

// ---- MD5 (RFC 1321) — يُستخدَم فقط كبصمة/checksum توافقية، ليس لأي غرض أمني حديث ----
std::string md5Hex(const std::string& msg) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const uint32_t S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21};
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    std::vector<uint8_t> data(msg.begin(), msg.end());
    uint64_t origBits = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 0; i < 8; ++i) data.push_back(static_cast<uint8_t>((origBits >> (8 * i)) & 0xFF));

    auto rotl = [](uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); };
    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i)
            M[i] = static_cast<uint32_t>(data[chunk + i * 4]) | (static_cast<uint32_t>(data[chunk + i * 4 + 1]) << 8) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 2]) << 16) | (static_cast<uint32_t>(data[chunk + i * 4 + 3]) << 24);
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t F; uint32_t g;
            if (i < 16)      { F = (B & C) | (~B & D);         g = i; }
            else if (i < 32) { F = (D & B) | (~D & C);         g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;                  g = (3 * i + 5) % 16; }
            else             { F = C ^ (B | ~D);               g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + rotl(F, S[i]);
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    uint8_t digest[16];
    uint32_t parts[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = static_cast<uint8_t>((parts[i] >> (8 * j)) & 0xFF);
    return toHex(digest, 16);
}

uint32_t crc32Of(const std::string& s) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : s) crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::string base64Encode(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    out.reserve(((in.size() + 2) / 3) * 4);
    while (i + 3 <= in.size()) {
        uint32_t n = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i+1]) << 8) | static_cast<uint8_t>(in[i+2]);
        out += tbl[(n >> 18) & 0x3F]; out += tbl[(n >> 12) & 0x3F]; out += tbl[(n >> 6) & 0x3F]; out += tbl[n & 0x3F];
        i += 3;
    }
    size_t rem = in.size() - i;
    if (rem == 1) {
        uint32_t n = static_cast<uint8_t>(in[i]) << 16;
        out += tbl[(n >> 18) & 0x3F]; out += tbl[(n >> 12) & 0x3F]; out += "==";
    } else if (rem == 2) {
        uint32_t n = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i+1]) << 8);
        out += tbl[(n >> 18) & 0x3F]; out += tbl[(n >> 12) & 0x3F]; out += tbl[(n >> 6) & 0x3F]; out += '=';
    }
    return out;
}

bool base64Decode(const std::string& in, std::string& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return true;
}

std::string hmacSha256Hex(const std::string& key, const std::string& msg) {
    const size_t blockSize = 64;
    std::string k = key;
    if (k.size() > blockSize) {
        auto d = clc::sha256(k);
        k.assign(reinterpret_cast<const char*>(d.data()), d.size());
    }
    k.resize(blockSize, '\0');
    std::string oKeyPad(blockSize, 0x5c), iKeyPad(blockSize, 0x36);
    for (size_t i = 0; i < blockSize; ++i) {
        oKeyPad[i] ^= k[i];
        iKeyPad[i] ^= k[i];
    }
    auto inner = clc::sha256(iKeyPad + msg);
    std::string innerStr(reinterpret_cast<const char*>(inner.data()), inner.size());
    return clc::sha256_hex(clc::sha256(oKeyPad + innerStr));
}

std::string xorCipher(const std::string& s, const std::string& key) {
    if (key.empty()) return s;
    std::string out(s.size(), '\0');
    for (size_t i = 0; i < s.size(); ++i) out[i] = static_cast<char>(s[i] ^ key[i % key.size()]);
    return out;
}

std::mt19937_64& secureRng() {
    static std::mt19937_64 rng([] {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    }());
    return rng;
}

// ============================================================ 3) net.* helpers

struct UrlParts { std::string scheme, host, port, path, query, fragment; bool ok = false; };

UrlParts parseUrlImpl(const std::string& url) {
    UrlParts p;
    std::string rest = url;
    size_t schemeEnd = rest.find("://");
    if (schemeEnd == std::string::npos) return p; // ok=false
    p.scheme = rest.substr(0, schemeEnd);
    rest = rest.substr(schemeEnd + 3);
    size_t pathStart = rest.find_first_of("/?#");
    std::string authority = pathStart == std::string::npos ? rest : rest.substr(0, pathStart);
    std::string tail = pathStart == std::string::npos ? std::string() : rest.substr(pathStart);
    // authority: [user@]host[:port] — نتجاهل معلومات المستخدم إن وُجدت.
    size_t at = authority.rfind('@');
    std::string hostPort = at == std::string::npos ? authority : authority.substr(at + 1);
    if (!hostPort.empty() && hostPort[0] == '[') { // IPv6 literal [::1]:8080
        size_t close = hostPort.find(']');
        if (close != std::string::npos) {
            p.host = hostPort.substr(1, close - 1);
            if (close + 1 < hostPort.size() && hostPort[close + 1] == ':') p.port = hostPort.substr(close + 2);
        } else p.host = hostPort;
    } else {
        size_t colon = hostPort.rfind(':');
        if (colon != std::string::npos) { p.host = hostPort.substr(0, colon); p.port = hostPort.substr(colon + 1); }
        else p.host = hostPort;
    }
    size_t qpos = tail.find('?');
    size_t hpos = tail.find('#');
    if (qpos == std::string::npos && hpos == std::string::npos) {
        p.path = tail;
    } else if (hpos == std::string::npos) {
        p.path = tail.substr(0, qpos);
        p.query = tail.substr(qpos + 1);
    } else if (qpos == std::string::npos || hpos < qpos) {
        p.path = tail.substr(0, hpos);
        p.fragment = tail.substr(hpos + 1);
    } else {
        p.path = tail.substr(0, qpos);
        p.query = tail.substr(qpos + 1, hpos - qpos - 1);
        p.fragment = tail.substr(hpos + 1);
    }
    if (p.path.empty()) p.path = "/";
    p.ok = true;
    return p;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexv(s[i+1]), lo = hexv(s[i+2]);
            if (hi >= 0 && lo >= 0) { out += static_cast<char>((hi << 4) | lo); i += 2; continue; }
            out += s[i];
        } else if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

MapData parseQueryImpl(const std::string& qs) {
    MapData out;
    size_t i = 0;
    while (i <= qs.size()) {
        size_t amp = qs.find('&', i);
        std::string pair = amp == std::string::npos ? qs.substr(i) : qs.substr(i, amp - i);
        if (!pair.empty()) {
            size_t eq = pair.find('=');
            std::string k = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
            std::string v = eq == std::string::npos ? std::string() : urlDecode(pair.substr(eq + 1));
            mapSet(out, Value::string(k), Value::string(v));
        }
        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return out;
}

bool isValidIPv4(const std::string& s, uint32_t* outNum = nullptr) {
    std::vector<int> parts;
    size_t i = 0;
    while (i <= s.size()) {
        size_t dot = s.find('.', i);
        std::string seg = dot == std::string::npos ? s.substr(i) : s.substr(i, dot - i);
        if (seg.empty() || seg.size() > 3 || (seg.size() > 1 && seg[0] == '0')) return false;
        for (char c : seg) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        int v = std::stoi(seg);
        if (v < 0 || v > 255) return false;
        parts.push_back(v);
        if (dot == std::string::npos) break;
        i = dot + 1;
    }
    if (parts.size() != 4) return false;
    if (outNum) *outNum = (static_cast<uint32_t>(parts[0]) << 24) | (static_cast<uint32_t>(parts[1]) << 16) |
                          (static_cast<uint32_t>(parts[2]) << 8) | static_cast<uint32_t>(parts[3]);
    return true;
}

bool isValidIPv6(const std::string& s) {
    if (s.find(':') == std::string::npos) return false;
    int groups = 0;
    bool sawDouble = false;
    std::string rest = s;
    if (rest.find("::") != std::string::npos) {
        sawDouble = true;
        if (rest.find("::", rest.find("::") + 1) != std::string::npos) return false; // أكثر من "::"
    }
    size_t i = 0;
    while (i <= rest.size()) {
        size_t colon = rest.find(':', i);
        std::string seg = colon == std::string::npos ? rest.substr(i) : rest.substr(i, colon - i);
        if (!seg.empty()) {
            if (seg.size() > 4) return false;
            for (char c : seg) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
            groups++;
        }
        if (colon == std::string::npos) break;
        i = colon + 1;
    }
    return groups > 0 && (sawDouble ? groups <= 8 : groups == 8);
}

bool cidrContainsImpl(const std::string& cidr, const std::string& ip) {
    size_t slash = cidr.find('/');
    if (slash == std::string::npos) return false;
    std::string netStr = cidr.substr(0, slash);
    int prefix = std::atoi(cidr.substr(slash + 1).c_str());
    if (prefix < 0 || prefix > 32) return false;
    uint32_t net = 0, addr = 0;
    if (!isValidIPv4(netStr, &net) || !isValidIPv4(ip, &addr)) return false;
    uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    return (net & mask) == (addr & mask);
}

bool isPrivateIpImpl(const std::string& ip) {
    uint32_t n = 0;
    if (!isValidIPv4(ip, &n)) return false;
    uint8_t a = (n >> 24) & 0xFF, b = (n >> 16) & 0xFF;
    if (a == 10) return true;                              // 10.0.0.0/8
    if (a == 172 && b >= 16 && b <= 31) return true;        // 172.16.0.0/12
    if (a == 192 && b == 168) return true;                  // 192.168.0.0/16
    if (a == 127) return true;                               // 127.0.0.0/8 (loopback)
    if (a == 169 && b == 254) return true;                  // 169.254.0.0/16 (link-local)
    return false;
}

} // namespace extra2

using namespace extra2;
using namespace extra;

// ============================================================================
void Interpreter::registerNativesExtra2() {

    // ------------------------------------------------------------- 1) sec.*
    natives["sec.sha256"] = [](Args& a, int line) -> Value {
        need("sec.sha256", a, 1, 1, line);
        return Value::string(sha256Hex(str(a[0], "sec.sha256", line)));
    };
    natives["sec.md5"] = [](Args& a, int line) -> Value {
        need("sec.md5", a, 1, 1, line);
        return Value::string(md5Hex(str(a[0], "sec.md5", line)));
    };
    natives["sec.crc32"] = [](Args& a, int line) -> Value {
        need("sec.crc32", a, 1, 1, line);
        return Value::num(static_cast<double>(crc32Of(str(a[0], "sec.crc32", line))));
    };
    natives["sec.hmacSha256"] = [](Args& a, int line) -> Value {
        need("sec.hmacSha256", a, 2, 2, line);
        return Value::string(hmacSha256Hex(str(a[0], "sec.hmacSha256", line), str(a[1], "sec.hmacSha256", line)));
    };
    natives["sec.base64Encode"] = [](Args& a, int line) -> Value {
        need("sec.base64Encode", a, 1, 1, line);
        return Value::string(base64Encode(str(a[0], "sec.base64Encode", line)));
    };
    natives["sec.base64Decode"] = [](Args& a, int line) -> Value {
        need("sec.base64Decode", a, 1, 1, line);
        std::string out;
        if (!base64Decode(str(a[0], "sec.base64Decode", line), out))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'sec.base64Decode': نص base64 غير صالح");
        return Value::string(out);
    };
    natives["sec.hexEncode"] = [](Args& a, int line) -> Value {
        need("sec.hexEncode", a, 1, 1, line);
        std::string s = str(a[0], "sec.hexEncode", line);
        return Value::string(toHex(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    };
    natives["sec.hexDecode"] = [](Args& a, int line) -> Value {
        need("sec.hexDecode", a, 1, 1, line);
        std::string out;
        if (!fromHex(str(a[0], "sec.hexDecode", line), out))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'sec.hexDecode': نص hex غير صالح (طول فردي أو رموز غير سداسية عشرية)");
        return Value::string(out);
    };
    natives["sec.randomToken"] = [](Args& a, int line) -> Value {
        need("sec.randomToken", a, 0, 1, line);
        double n = a.empty() ? 16.0 : num(a[0], "sec.randomToken", line);
        if (n < 1 || n > 4096) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'sec.randomToken' يقبل طول بايتات بين 1 و4096");
        size_t bytes = static_cast<size_t>(n);
        std::uniform_int_distribution<int> dist(0, 255);
        std::string raw(bytes, '\0');
        for (size_t i = 0; i < bytes; ++i) raw[i] = static_cast<char>(dist(secureRng()));
        return Value::string(toHex(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
    };
    natives["sec.constantTimeEqual"] = [](Args& a, int line) -> Value {
        need("sec.constantTimeEqual", a, 2, 2, line);
        std::string x = str(a[0], "sec.constantTimeEqual", line), y = str(a[1], "sec.constantTimeEqual", line);
        if (x.size() != y.size()) return Value::boolean_(false);
        unsigned char diffBits = 0;
        for (size_t i = 0; i < x.size(); ++i) diffBits |= static_cast<unsigned char>(x[i] ^ y[i]);
        return Value::boolean_(diffBits == 0);
    };
    natives["sec.xorCipher"] = [](Args& a, int line) -> Value {
        need("sec.xorCipher", a, 2, 2, line);
        return Value::string(xorCipher(str(a[0], "sec.xorCipher", line), str(a[1], "sec.xorCipher", line)));
    };
    natives["sec.rot13"] = [](Args& a, int line) -> Value {
        need("sec.rot13", a, 1, 1, line);
        std::string s = str(a[0], "sec.rot13", line);
        for (char& c : s) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>('a' + (c - 'a' + 13) % 26);
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>('A' + (c - 'A' + 13) % 26);
        }
        return Value::string(s);
    };
    // sec.passwordStrength(s) -> {score:0..4, length, hasUpper, hasLower, hasDigit, hasSymbol, feedback:[..]}
    natives["sec.passwordStrength"] = [](Args& a, int line) -> Value {
        need("sec.passwordStrength", a, 1, 1, line);
        std::string s = str(a[0], "sec.passwordStrength", line);
        bool hasUpper = false, hasLower = false, hasDigit = false, hasSymbol = false;
        for (unsigned char c : s) {
            if (std::isupper(c)) hasUpper = true;
            else if (std::islower(c)) hasLower = true;
            else if (std::isdigit(c)) hasDigit = true;
            else if (!std::isspace(c)) hasSymbol = true;
        }
        int variety = static_cast<int>(hasUpper) + static_cast<int>(hasLower) + static_cast<int>(hasDigit) + static_cast<int>(hasSymbol);
        int score = 0;
        ArrayData feedback;
        if (s.size() < 8) feedback.push_back(Value::string("قصيرة جداً (أقل من 8 محارف)"));
        else if (s.size() >= 8) score += 1;
        if (s.size() >= 12) score += 1;
        if (variety >= 3) score += 1;
        if (variety == 4) score += 1;
        if (variety < 3) feedback.push_back(Value::string("اخلط أحرفاً كبيرة/صغيرة وأرقاماً ورموزاً"));
        score = std::min(score, 4);
        MapData out;
        out.push_back({Value::string("score"), Value::num(score)});
        out.push_back({Value::string("length"), Value::num(static_cast<double>(s.size()))});
        out.push_back({Value::string("hasUpper"), Value::boolean_(hasUpper)});
        out.push_back({Value::string("hasLower"), Value::boolean_(hasLower)});
        out.push_back({Value::string("hasDigit"), Value::boolean_(hasDigit)});
        out.push_back({Value::string("hasSymbol"), Value::boolean_(hasSymbol)});
        out.push_back({Value::string("feedback"), newArray(std::move(feedback))});
        return newMap(std::move(out));
    };

    // ------------------------------------------------------------ 2) fs.*
    auto hashFileImpl = [this](const std::string& algo, const std::string& path, int line) -> std::string {
        std::string full = resolvePath(path, line);
        std::ifstream in(full, std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "'file.hash': تعذّر فتح الملف '" + path + "'");
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string content = buf.str();
        if (algo == "sha256") return sha256Hex(content);
        if (algo == "md5") return md5Hex(content);
        if (algo == "crc32") { std::ostringstream h; h << std::hex << crc32Of(content); return h.str(); }
        throw diagErr(diag::Code::E0007_InvalidArguments, line, "'file.hash': خوارزمية غير معروفة '" + algo + "' (استخدم sha256/md5/crc32)");
    };
    natives["fs.hash"] = [this, hashFileImpl](Args& a, int line) -> Value {
        need("fs.hash", a, 1, 2, line);
        std::string algo = a.size() > 1 ? str(a[1], "fs.hash", line) : "sha256";
        return Value::string(hashFileImpl(algo, str(a[0], "fs.hash", line), line));
    };
    natives["fs.lineCount"] = [this](Args& a, int line) -> Value {
        need("fs.lineCount", a, 1, 1, line);
        std::ifstream in(resolvePath(str(a[0], "fs.lineCount", line), line), std::ios::binary);
        if (!in) return Value::nil();
        double n = 0; std::string l;
        bool any = false;
        while (std::getline(in, l)) { n += 1; any = true; }
        (void)any;
        return Value::num(n);
    };
    natives["fs.tail"] = [this](Args& a, int line) -> Value {
        need("fs.tail", a, 1, 2, line);
        std::ifstream in(resolvePath(str(a[0], "fs.tail", line), line), std::ios::binary);
        if (!in) return Value::nil();
        double nD = a.size() > 1 ? num(a[1], "fs.tail", line) : 10.0;
        size_t n = nD < 0 ? 0 : static_cast<size_t>(nD);
        std::vector<std::string> ring;
        std::string l;
        size_t idx = 0;
        while (std::getline(in, l)) {
            if (ring.size() < n) ring.push_back(l);
            else if (n > 0) { ring[idx % n] = l; }
            idx++;
        }
        ArrayData out;
        if (idx <= n) { for (auto& s : ring) out.push_back(Value::string(s)); }
        else {
            size_t start = idx % n;
            for (size_t i = 0; i < n; ++i) out.push_back(Value::string(ring[(start + i) % n]));
        }
        return newArray(std::move(out));
    };
    // file.grep(path, pattern, flags?) -> [{line:N, text:"..."}] — pattern بصيغة ECMAScript regex
    // (يدعم نفس بادئة "i:"/"m:" لـ compileRinRegex المستخدمة في regexTest/regexFindAll).
    natives["fs.grep"] = [this](Args& a, int line) -> Value {
        need("fs.grep", a, 2, 2, line);
        std::string path = str(a[0], "fs.grep", line);
        std::regex re = compileRinRegex(str(a[1], "fs.grep", line), "fs.grep", line);
        std::ifstream in(resolvePath(path, line), std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "'file.grep': تعذّر فتح الملف '" + path + "'");
        ArrayData out;
        std::string l;
        double lineNo = 0;
        while (std::getline(in, l)) {
            lineNo += 1;
            if (std::regex_search(l, re)) {
                MapData row;
                row.push_back({Value::string("line"), Value::num(lineNo)});
                row.push_back({Value::string("text"), Value::string(l)});
                out.push_back(newMap(std::move(row)));
            }
        }
        return newArray(std::move(out));
    };
    // file.scanDir(path, recursive?) -> [{name, path, isDir, size, extension}, ...]
    natives["fs.scanDir"] = [this](Args& a, int line) -> Value {
        need("fs.scanDir", a, 1, 2, line);
        std::string base = resolvePath(str(a[0], "fs.scanDir", line), line);
        bool recursive = a.size() > 1 && a[1].isTruthy();
        std::error_code ec;
        if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) return Value::nil();
        ArrayData out;
        auto addEntry = [&](const std::filesystem::directory_entry& e) {
            MapData row;
            bool isDir = e.is_directory(ec);
            row.push_back({Value::string("name"), Value::string(e.path().filename().string())});
            row.push_back({Value::string("path"), Value::string(e.path().string())});
            row.push_back({Value::string("isDir"), Value::boolean_(isDir)});
            row.push_back({Value::string("size"), Value::num(isDir ? 0.0 : static_cast<double>(std::filesystem::file_size(e.path(), ec)))});
            row.push_back({Value::string("extension"), Value::string(e.path().extension().string())});
            out.push_back(newMap(std::move(row)));
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                addEntry(*it);
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                addEntry(*it);
            }
        }
        return newArray(std::move(out));
    };
    natives["fs.extensionStats"] = [this](Args& a, int line) -> Value {
        need("fs.extensionStats", a, 1, 2, line);
        std::string base = resolvePath(str(a[0], "fs.extensionStats", line), line);
        bool recursive = a.size() > 1 && a[1].isTruthy();
        std::error_code ec;
        if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) return Value::nil();
        MapData out;
        auto bump = [&](const std::string& ext) {
            std::string key = ext.empty() ? "(none)" : ext;
            const Value* v = mapFind(out, Value::string(key));
            double cur = v ? v->number : 0.0;
            mapSet(out, Value::string(key), Value::num(cur + 1));
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (!it->is_directory(ec)) bump(it->path().extension().string());
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (!it->is_directory(ec)) bump(it->path().extension().string());
            }
        }
        return newMap(std::move(out));
    };
    // file.find(path, namePattern, recursive?) -> [مسارات متطابقة مع regex على اسم الملف]
    natives["fs.find"] = [this](Args& a, int line) -> Value {
        need("fs.find", a, 2, 3, line);
        std::string base = resolvePath(str(a[0], "fs.find", line), line);
        std::regex re = compileRinRegex(str(a[1], "fs.find", line), "fs.find", line);
        bool recursive = a.size() > 2 && a[2].isTruthy();
        std::error_code ec;
        if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) return Value::nil();
        ArrayData out;
        auto check = [&](const std::filesystem::directory_entry& e) {
            std::string name = e.path().filename().string();
            if (std::regex_search(name, re)) out.push_back(Value::string(e.path().string()));
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) { if (ec) break; check(*it); }
        } else {
            for (auto it = std::filesystem::directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::directory_iterator(); it.increment(ec)) { if (ec) break; check(*it); }
        }
        return newArray(std::move(out));
    };

    // ------------------------------------------------------------- 3) net.*
    natives["net.parseUrl"] = [](Args& a, int line) -> Value {
        need("net.parseUrl", a, 1, 1, line);
        UrlParts p = parseUrlImpl(str(a[0], "net.parseUrl", line));
        if (!p.ok) return Value::nil();
        MapData out;
        out.push_back({Value::string("scheme"), Value::string(p.scheme)});
        out.push_back({Value::string("host"), Value::string(p.host)});
        out.push_back({Value::string("port"), p.port.empty() ? Value::nil() : Value::num(std::atof(p.port.c_str()))});
        out.push_back({Value::string("path"), Value::string(p.path)});
        out.push_back({Value::string("query"), Value::string(p.query)});
        out.push_back({Value::string("fragment"), Value::string(p.fragment)});
        return newMap(std::move(out));
    };
    natives["net.parseQuery"] = [](Args& a, int line) -> Value {
        need("net.parseQuery", a, 1, 1, line);
        return newMap(parseQueryImpl(str(a[0], "net.parseQuery", line)));
    };
    natives["net.buildQuery"] = [](Args& a, int line) -> Value {
        need("net.buildQuery", a, 1, 1, line);
        const MapData& m = mp(a[0], "net.buildQuery", line);
        std::string out;
        bool first = true;
        for (const auto& kv : m) {
            if (!first) out += '&';
            first = false;
            out += kv.first.toDisplayString();
            out += '=';
            out += kv.second.toDisplayString();
        }
        return Value::string(out);
    };
    natives["net.isValidIPv4"] = [](Args& a, int line) -> Value {
        need("net.isValidIPv4", a, 1, 1, line);
        return Value::boolean_(isValidIPv4(str(a[0], "net.isValidIPv4", line)));
    };
    natives["net.isValidIPv6"] = [](Args& a, int line) -> Value {
        need("net.isValidIPv6", a, 1, 1, line);
        return Value::boolean_(isValidIPv6(str(a[0], "net.isValidIPv6", line)));
    };
    natives["net.ipToInt"] = [](Args& a, int line) -> Value {
        need("net.ipToInt", a, 1, 1, line);
        uint32_t n = 0;
        if (!isValidIPv4(str(a[0], "net.ipToInt", line), &n)) return Value::nil();
        return Value::num(static_cast<double>(n));
    };
    natives["net.intToIp"] = [](Args& a, int line) -> Value {
        need("net.intToIp", a, 1, 1, line);
        double d = num(a[0], "net.intToIp", line);
        if (d < 0 || d > 4294967295.0) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'net.intToIp': خارج مدى IPv4 (0..4294967295)");
        uint32_t n = static_cast<uint32_t>(d);
        std::ostringstream o;
        o << ((n >> 24) & 0xFF) << '.' << ((n >> 16) & 0xFF) << '.' << ((n >> 8) & 0xFF) << '.' << (n & 0xFF);
        return Value::string(o.str());
    };
    natives["net.cidrContains"] = [](Args& a, int line) -> Value {
        need("net.cidrContains", a, 2, 2, line);
        return Value::boolean_(cidrContainsImpl(str(a[0], "net.cidrContains", line), str(a[1], "net.cidrContains", line)));
    };
    natives["net.isPrivateIp"] = [](Args& a, int line) -> Value {
        need("net.isPrivateIp", a, 1, 1, line);
        return Value::boolean_(isPrivateIpImpl(str(a[0], "net.isPrivateIp", line)));
    };

    // ------------------------------------------------------------- 4) log.*
    natives["log.parseLines"] = [](Args& a, int line) -> Value {
        need("log.parseLines", a, 1, 1, line);
        std::string text = str(a[0], "log.parseLines", line);
        ArrayData out;
        std::istringstream in(text);
        std::string l;
        while (std::getline(in, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            out.push_back(Value::string(l));
        }
        return newArray(std::move(out));
    };
    // log.parseCommonLog(line) -> Apache/Nginx Common/Combined Log Format
    // مثال: 127.0.0.1 - frank [10/Oct/2023:13:55:36 -0700] "GET /x HTTP/1.1" 200 2326
    natives["log.parseCommonLog"] = [](Args& a, int line) -> Value {
        need("log.parseCommonLog", a, 1, 1, line);
        static const std::regex clf(
            R"RX(^(\S+) (\S+) (\S+) \[([^\]]+)\] "([A-Z]+) (\S+) ([^"]*)" (\d{3}) (\S+))RX");
        std::smatch m;
        std::string l = str(a[0], "log.parseCommonLog", line);
        if (!std::regex_search(l, m, clf)) return Value::nil();
        MapData out;
        out.push_back({Value::string("ip"), Value::string(m[1].str())});
        out.push_back({Value::string("ident"), Value::string(m[2].str())});
        out.push_back({Value::string("user"), Value::string(m[3].str())});
        out.push_back({Value::string("time"), Value::string(m[4].str())});
        out.push_back({Value::string("method"), Value::string(m[5].str())});
        out.push_back({Value::string("path"), Value::string(m[6].str())});
        out.push_back({Value::string("protocol"), Value::string(m[7].str())});
        out.push_back({Value::string("status"), Value::num(std::atof(m[8].str().c_str()))});
        std::string sizeStr = m[9].str();
        out.push_back({Value::string("size"), sizeStr == "-" ? Value::num(0) : Value::num(std::atof(sizeStr.c_str()))});
        return newMap(std::move(out));
    };
    natives["log.extractIPs"] = [](Args& a, int line) -> Value {
        need("log.extractIPs", a, 1, 1, line);
        static const std::regex ipRe(R"(\b(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)\b)");
        std::string text = str(a[0], "log.extractIPs", line);
        std::vector<std::string> seen;
        ArrayData out;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), ipRe); it != std::sregex_iterator(); ++it) {
            std::string ip = it->str();
            if (std::find(seen.begin(), seen.end(), ip) == seen.end()) { seen.push_back(ip); out.push_back(Value::string(ip)); }
        }
        return newArray(std::move(out));
    };
    // log.extractTimestamps(text, pattern?) — الافتراضي يطابق صيغ شائعة (ISO8601 و[dd/Mon/yyyy:HH:MM:SS])
    natives["log.extractTimestamps"] = [](Args& a, int line) -> Value {
        need("log.extractTimestamps", a, 1, 2, line);
        std::string text = str(a[0], "log.extractTimestamps", line);
        std::regex re = a.size() > 1
            ? compileRinRegex(str(a[1], "log.extractTimestamps", line), "log.extractTimestamps", line)
            : std::regex(R"((\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}|\d{2}/\w{3}/\d{4}:\d{2}:\d{2}:\d{2}))");
        ArrayData out;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
            out.push_back(Value::string(it->str()));
        return newArray(std::move(out));
    };
    // log.countBy(lines, fn) -> {مفتاح: عدد} — fn(line) تُرجع المفتاح لكل سطر
    natives["log.countBy"] = [this](Args& a, int line) -> Value {
        need("log.countBy", a, 2, 2, line);
        const ArrayData& lines = arr(a[0], "log.countBy", line);
        if (a[1].type != Value::Type::FUNCTION || !a[1].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'log.countBy' expects a function as its second argument");
        MapData out;
        for (const auto& l : lines) {
            Args callArgs{l};
            Value key = callFunction(a[1].function, callArgs, line);
            std::string k = key.toDisplayString();
            const Value* v = mapFind(out, Value::string(k));
            double cur = v ? v->number : 0.0;
            mapSet(out, Value::string(k), Value::num(cur + 1));
        }
        return newMap(std::move(out));
    };
    natives["log.topN"] = [](Args& a, int line) -> Value {
        need("log.topN", a, 2, 2, line);
        const MapData& m = mp(a[0], "log.topN", line);
        double nD = num(a[1], "log.topN", line);
        size_t n = nD < 0 ? 0 : static_cast<size_t>(nD);
        std::vector<std::pair<Value, Value>> items(m.begin(), m.end());
        std::sort(items.begin(), items.end(), [](const auto& x, const auto& y) {
            double a_ = x.second.type == Value::Type::NUMBER ? x.second.number : 0.0;
            double b_ = y.second.type == Value::Type::NUMBER ? y.second.number : 0.0;
            return a_ > b_;
        });
        if (items.size() > n) items.resize(n);
        ArrayData out;
        for (auto& kv : items) out.push_back(newArray({kv.first, kv.second}));
        return newArray(std::move(out));
    };
    natives["log.filterLines"] = [](Args& a, int line) -> Value {
        need("log.filterLines", a, 2, 2, line);
        const ArrayData& lines = arr(a[0], "log.filterLines", line);
        std::regex re = compileRinRegex(str(a[1], "log.filterLines", line), "log.filterLines", line);
        ArrayData out;
        for (const auto& l : lines) {
            if (l.type != Value::Type::STRING) continue;
            if (std::regex_search(l.str, re)) out.push_back(l);
        }
        return newArray(std::move(out));
    };
    // log.levelCounts(lines) -> {ERROR:N, WARN:N, INFO:N, DEBUG:N, FATAL:N, OTHER:N}
    natives["log.levelCounts"] = [](Args& a, int line) -> Value {
        need("log.levelCounts", a, 1, 1, line);
        const ArrayData& lines = arr(a[0], "log.levelCounts", line);
        static const std::vector<std::string> levels = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
        MapData out;
        for (const auto& lv : levels) out.push_back({Value::string(lv), Value::num(0)});
        double other = 0;
        for (const auto& lRaw : lines) {
            if (lRaw.type != Value::Type::STRING) continue;
            std::string upper = lRaw.str;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            bool matched = false;
            for (const auto& lv : levels) {
                if (upper.find(lv) != std::string::npos) {
                    Value* slot = nullptr;
                    for (auto& kv : out) if (kv.first.str == lv) { slot = &kv.second; break; }
                    if (slot) slot->number += 1;
                    matched = true;
                    break;
                }
            }
            if (!matched) other += 1;
        }
        out.push_back({Value::string("OTHER"), Value::num(other)});
        return newMap(std::move(out));
    };
    // log.detectSpikes(counts, factor?) -> فهارس العناصر التي تتجاوز (المتوسط * factor) — الافتراضي 2.0
    natives["log.detectSpikes"] = [](Args& a, int line) -> Value {
        need("log.detectSpikes", a, 1, 2, line);
        const ArrayData& counts = arr(a[0], "log.detectSpikes", line);
        double factor = a.size() > 1 ? num(a[1], "log.detectSpikes", line) : 2.0;
        if (counts.empty()) return newArray();
        double sum = 0;
        for (const auto& v : counts) sum += num(v, "log.detectSpikes", line);
        double avg = sum / static_cast<double>(counts.size());
        ArrayData out;
        for (size_t i = 0; i < counts.size(); ++i) {
            double v = num(counts[i], "log.detectSpikes", line);
            if (avg > 0 && v > avg * factor) out.push_back(Value::num(static_cast<double>(i)));
            else if (avg == 0 && v > 0) out.push_back(Value::num(static_cast<double>(i)));
        }
        return newArray(std::move(out));
    };

    // -------------------------------------------------------- 5) automation.*
    // automation.retry(fn, times, delayMs?) — يستدعي fn() حتى النجاح أو استنفاد المحاولات؛
    // يعيد رمي آخر خطأ إن فشلت كل المحاولات. delayMs (اختياري، سقف 60000) إيقاف فعلي بين المحاولات.
    natives["automation.retry"] = [this](Args& a, int line) -> Value {
        need("automation.retry", a, 2, 3, line);
        if (a[0].type != Value::Type::FUNCTION || !a[0].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'automation.retry' expects a function as its first argument");
        double timesD = num(a[1], "automation.retry", line);
        if (timesD < 1) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'automation.retry': times يجب أن تكون 1 على الأقل");
        int times = static_cast<int>(std::min(timesD, 1000.0));
        double delayMs = a.size() > 2 ? num(a[2], "automation.retry", line) : 0.0;
        delayMs = std::max(0.0, std::min(delayMs, 60000.0));
        for (int attempt = 1; attempt <= times; ++attempt) {
            try {
                Args callArgs;
                return callFunction(a[0].function, callArgs, line);
            } catch (const ThrowSignal&) {
                if (attempt == times) throw;
            } catch (const RinError&) {
                if (attempt == times) throw;
            }
            if (delayMs > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delayMs));
        }
        return Value::nil(); // لن يُصَل هنا فعلياً
    };
    // automation.chain(value, [fn1, fn2, ...]) -> fnN(...fn2(fn1(value)))
    natives["automation.chain"] = [this](Args& a, int line) -> Value {
        need("automation.chain", a, 2, 2, line);
        const ArrayData& fns = arr(a[1], "automation.chain", line);
        Value cur = a[0];
        for (const auto& f : fns) {
            if (f.type != Value::Type::FUNCTION || !f.function)
                throw diagErr(diag::Code::E0004_InvalidType, line, "'automation.chain' expects an array of functions");
            Args callArgs{cur};
            cur = callFunction(f.function, callArgs, line);
        }
        return cur;
    };
    // automation.timeIt(fn) -> {result, ms}
    natives["automation.timeIt"] = [this](Args& a, int line) -> Value {
        need("automation.timeIt", a, 1, 1, line);
        if (a[0].type != Value::Type::FUNCTION || !a[0].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'automation.timeIt' expects a function as its argument");
        auto t0 = std::chrono::steady_clock::now();
        Args callArgs;
        Value result = callFunction(a[0].function, callArgs, line);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        MapData out;
        out.push_back({Value::string("result"), result});
        out.push_back({Value::string("ms"), Value::num(ms)});
        return newMap(std::move(out));
    };
    natives["automation.batch"] = [](Args& a, int line) -> Value {
        need("automation.batch", a, 2, 2, line);
        const ArrayData& items = arr(a[0], "automation.batch", line);
        double sizeD = num(a[1], "automation.batch", line);
        if (sizeD < 1) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'automation.batch': الحجم يجب أن يكون 1 على الأقل");
        size_t size = static_cast<size_t>(sizeD);
        ArrayData out;
        ArrayData cur;
        for (const auto& it : items) {
            cur.push_back(it);
            if (cur.size() >= size) { out.push_back(newArray(cur)); cur.clear(); }
        }
        if (!cur.empty()) out.push_back(newArray(cur));
        return newArray(std::move(out));
    };
    // automation.sequence([fn1, fn2, ...]) -> [fn1(), fn2(), ...] بالترتيب (توقف عند أول خطأ)
    natives["automation.sequence"] = [this](Args& a, int line) -> Value {
        need("automation.sequence", a, 1, 1, line);
        const ArrayData& fns = arr(a[0], "automation.sequence", line);
        ArrayData out;
        for (const auto& f : fns) {
            if (f.type != Value::Type::FUNCTION || !f.function)
                throw diagErr(diag::Code::E0004_InvalidType, line, "'automation.sequence' expects an array of functions");
            Args callArgs;
            out.push_back(callFunction(f.function, callArgs, line));
        }
        return newArray(std::move(out));
    };
    // automation.sleepMs(ms) — إيقاف فعلي محدود السقف (60 ثانية) لأتمتة تحتاج فواصل زمنية.
    natives["automation.sleepMs"] = [](Args& a, int line) -> Value {
        need("automation.sleepMs", a, 1, 1, line);
        double ms = num(a[0], "automation.sleepMs", line);
        if (ms < 0 || ms > 60000.0)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'automation.sleepMs' يقبل قيمة بين 0 و60000 (60 ثانية كحد أقصى)");
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
        return Value::boolean_(true);
    };

    // ------------------------------------------------------ 6) container.* extras
    auto kindOf2 = [this](const std::string& name) -> std::string {
        Args args{Value::string(name)};
        Value k = natives["container.kind"](args, 0);
        return k.type == Value::Type::STRING ? k.str : std::string();
    };
    auto sortedNames2 = [this]() {
        std::vector<std::string> names;
        names.reserve(containers.size());
        for (const auto& kv : containers) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return names;
    };
    auto kindMatches2 = [kindOf2](const std::string& name, const std::string& kind) {
        return kind.empty() || kind == "*" || kindOf2(name) == kind;
    };

    // container.snapshot(name) -> نسخة عميقة من كل الحقول غير الدوال (map)، أو nil إن غابت الحاوية
    natives["container.snapshot"] = [this](Args& a, int line) -> Value {
        need("container.snapshot", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.snapshot", line));
        if (it == containers.end()) return Value::nil();
        MapData out;
        for (const auto& kv : it->second->values) {
            if (kv.second.type == Value::Type::FUNCTION) continue;
            out.push_back({Value::string(kv.first), deepCopyValue(kv.second)});
        }
        return newMap(std::move(out));
    };
    // container.restore(name, snap) — يستبدل حقول الحاوية بمحتوى snap (map) كاملاً؛ false إن غابت الحاوية
    natives["container.restore"] = [this](Args& a, int line) -> Value {
        need("container.restore", a, 2, 2, line);
        auto it = containers.find(str(a[0], "container.restore", line));
        if (it == containers.end()) return Value::boolean_(false);
        const MapData& snap = mp(a[1], "container.restore", line);
        for (const auto& kv : snap) it->second->values[kv.first.toDisplayString()] = deepCopyValue(kv.second);
        return Value::boolean_(true);
    };
    natives["container.lock"] = [this](Args& a, int line) -> Value {
        need("container.lock", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.lock", line));
        if (it == containers.end()) return Value::boolean_(false);
        it->second->values["__locked"] = Value::boolean_(true);
        return Value::boolean_(true);
    };
    natives["container.unlock"] = [this](Args& a, int line) -> Value {
        need("container.unlock", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.unlock", line));
        if (it == containers.end()) return Value::boolean_(false);
        it->second->values["__locked"] = Value::boolean_(false);
        return Value::boolean_(true);
    };
    natives["container.isLocked"] = [this](Args& a, int line) -> Value {
        need("container.isLocked", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.isLocked", line));
        if (it == containers.end()) return Value::nil();
        Value v;
        return Value::boolean_(it->second->get("__locked", v) && v.isTruthy());
    };
    // container.logEvent(name, message) -> يضيف {t, msg} إلى __events ويعيد الطول الجديد؛ nil إن غابت الحاوية
    natives["container.logEvent"] = [this](Args& a, int line) -> Value {
        need("container.logEvent", a, 2, 2, line);
        auto it = containers.find(str(a[0], "container.logEvent", line));
        if (it == containers.end()) return Value::nil();
        Value cur;
        ArrayPtr events;
        if (it->second->get("__events", cur) && cur.type == Value::Type::ARRAY && cur.array) events = cur.array;
        else events = std::make_shared<ArrayData>();
        MapData ev;
        ev.push_back({Value::string("t"), Value::num(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count())});
        ev.push_back({Value::string("msg"), a[1]});
        events->push_back(newMap(std::move(ev)));
        it->second->values["__events"] = Value::makeArray(events);
        return Value::num(static_cast<double>(events->size()));
    };
    natives["container.history"] = [this](Args& a, int line) -> Value {
        need("container.history", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.history", line));
        if (it == containers.end()) return Value::nil();
        Value cur;
        if (it->second->get("__events", cur) && cur.type == Value::Type::ARRAY) return cur;
        return newArray();
    };
    // container.filter(kind, fn) -> [أسماء] حيث fn(fieldsMap) صحيحة؛ يمر بـ container.pick-كامل
    natives["container.filter"] = [this, sortedNames2, kindMatches2](Args& a, int line) -> Value {
        need("container.filter", a, 2, 2, line);
        std::string kind = str(a[0], "container.filter", line);
        if (a[1].type != Value::Type::FUNCTION || !a[1].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'container.filter' expects a function as its second argument");
        ArrayData out;
        for (const auto& name : sortedNames2()) {
            auto it = containers.find(name);
            if (it == containers.end() || !kindMatches2(name, kind)) continue;
            MapData fields;
            for (const auto& kv : it->second->values)
                if (kv.second.type != Value::Type::FUNCTION) fields.push_back({Value::string(kv.first), kv.second});
            Args callArgs{newMap(std::move(fields))};
            if (callFunction(a[1].function, callArgs, line).isTruthy()) out.push_back(Value::string(name));
        }
        return newArray(std::move(out));
    };
    // container.stats(kind) -> {count, fields: {field: {count, numeric, min?, max?, sum?, avg?}}}
    natives["container.stats"] = [this, sortedNames2, kindMatches2](Args& a, int line) -> Value {
        need("container.stats", a, 1, 1, line);
        std::string kind = str(a[0], "container.stats", line);
        double count = 0;
        struct Agg { double count = 0; bool numeric = true; double mn = 0, mx = 0, sum = 0; bool first = true; };
        std::unordered_map<std::string, Agg> aggs;
        std::vector<std::string> fieldOrder;
        for (const auto& name : sortedNames2()) {
            auto it = containers.find(name);
            if (it == containers.end() || !kindMatches2(name, kind)) continue;
            count += 1;
            for (const auto& kv : it->second->values) {
                if (kv.second.type == Value::Type::FUNCTION) continue;
                if (kv.first.size() >= 2 && kv.first[0] == '_' && kv.first[1] == '_') continue; // تجاهل __locked/__events الداخلية
                auto ins = aggs.find(kv.first);
                if (ins == aggs.end()) { fieldOrder.push_back(kv.first); ins = aggs.emplace(kv.first, Agg{}).first; }
                Agg& ag = ins->second;
                ag.count += 1;
                if (kv.second.type == Value::Type::NUMBER) {
                    double v = kv.second.number;
                    if (ag.first) { ag.mn = ag.mx = v; ag.first = false; } else { ag.mn = std::min(ag.mn, v); ag.mx = std::max(ag.mx, v); }
                    ag.sum += v;
                } else ag.numeric = false;
            }
        }
        MapData fieldsOut;
        for (const auto& f : fieldOrder) {
            const Agg& ag = aggs[f];
            MapData row;
            row.push_back({Value::string("count"), Value::num(ag.count)});
            row.push_back({Value::string("numeric"), Value::boolean_(ag.numeric)});
            if (ag.numeric && ag.count > 0) {
                row.push_back({Value::string("min"), Value::num(ag.mn)});
                row.push_back({Value::string("max"), Value::num(ag.mx)});
                row.push_back({Value::string("sum"), Value::num(ag.sum)});
                row.push_back({Value::string("avg"), Value::num(ag.sum / ag.count)});
            }
            fieldsOut.push_back({Value::string(f), newMap(std::move(row))});
        }
        MapData out;
        out.push_back({Value::string("count"), Value::num(count)});
        out.push_back({Value::string("fields"), newMap(std::move(fieldsOut))});
        return newMap(std::move(out));
    };
    // container.validate(name, schema) -> {valid, errors:[..]} — schema: {field: "type"} أو "type?" لحقل اختياري
    // الأنواع المدعومة: number/string/bool/array/map/function/any
    natives["container.validate"] = [this](Args& a, int line) -> Value {
        need("container.validate", a, 2, 2, line);
        auto it = containers.find(str(a[0], "container.validate", line));
        ArrayData errors;
        if (it == containers.end()) {
            errors.push_back(Value::string("container not found"));
            MapData out; out.push_back({Value::string("valid"), Value::boolean_(false)}); out.push_back({Value::string("errors"), newArray(std::move(errors))});
            return newMap(std::move(out));
        }
        const MapData& schema = mp(a[1], "container.validate", line);
        for (const auto& kv : schema) {
            std::string field = kv.first.toDisplayString();
            std::string wantType = str(kv.second, "container.validate", line);
            bool optional = !wantType.empty() && wantType.back() == '?';
            if (optional) wantType.pop_back();
            Value v;
            bool present = it->second->get(field, v);
            if (!present) {
                if (!optional) errors.push_back(Value::string("missing field '" + field + "'"));
                continue;
            }
            if (wantType == "any") continue;
            std::string got = v.typeName();
            std::string wantNorm = wantType == "bool" ? "bool" : wantType;
            if (got != wantNorm)
                errors.push_back(Value::string("field '" + field + "': expected " + wantType + " but got " + got));
        }
        MapData out;
        out.push_back({Value::string("valid"), Value::boolean_(errors.empty())});
        out.push_back({Value::string("errors"), newArray(std::move(errors))});
        return newMap(std::move(out));
    };
    // container.exportToFile(name, path) — يكتب حقول الحاوية كـ JSON فعلي على القرص (عبر container.toJson + writeFile المعزولة بـ resolvePath)
    natives["container.exportToFile"] = [this](Args& a, int line) -> Value {
        need("container.exportToFile", a, 2, 2, line);
        std::string name = str(a[0], "container.exportToFile", line);
        std::string path = str(a[1], "container.exportToFile", line);
        Args jsonArgs{Value::string(name)};
        Value json = natives["container.toJson"](jsonArgs, line);
        if (json.type != Value::Type::STRING) return Value::boolean_(false);
        writeRealFile(path, json.str, line, "container.exportToFile");
        return Value::boolean_(true);
    };
    // container.importFromFile(name, path, overwrite?) — يقرأ JSON من القرص ويدمجه في الحاوية (تُنشأ إن غابت عبر container.ensure أولاً من كود Rin)
    natives["container.importFromFile"] = [this](Args& a, int line) -> Value {
        need("container.importFromFile", a, 2, 3, line);
        std::string name = str(a[0], "container.importFromFile", line);
        std::string path = str(a[1], "container.importFromFile", line);
        std::ifstream in(resolvePath(path, line), std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "'container.importFromFile': تعذّر فتح الملف '" + path + "'");
        std::ostringstream buf; buf << in.rdbuf();
        Args jsonArgs{Value::string(name), Value::string(buf.str())};
        if (a.size() > 2) jsonArgs.push_back(a[2]);
        return natives["container.fromJson"](jsonArgs, line);
    };
}

} // namespace rin
