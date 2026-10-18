// ============================================================================
//  rin terminal — batch 4 (part 2/4): encoding, hashing, checksums, colors,
//  network and reference tables.  Header-only, POSIX + std only.
// ============================================================================
#pragma once

#include "rin_terminal_xcmds4.h"

namespace rin {
namespace terminal {
namespace xcmds {

// ----------------------------------------------------------------------------
// byte helpers
// ----------------------------------------------------------------------------
inline std::string hexOfStr(const std::string& s) { return hexOf(reinterpret_cast<const unsigned char*>(s.data()), s.size()); }
inline bool bytesOfHex(const std::string& h, std::string& out) {
    out.clear();
    std::string t;
    for (char ch : h) if (!std::isspace(static_cast<unsigned char>(ch)) && ch != ':' && ch != '-') t += ch;
    if (t.size() % 2) return false;
    for (size_t i = 0; i < t.size(); i += 2) {
        if (!std::isxdigit(static_cast<unsigned char>(t[i])) || !std::isxdigit(static_cast<unsigned char>(t[i + 1]))) return false;
        out += static_cast<char>(std::strtol(t.substr(i, 2).c_str(), nullptr, 16));
    }
    return true;
}
inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

// ----------------------------------------------------------------------------
// SHA-2 family (224 / 384 / 512; SHA-256 itself lives in rin_terminal_xcmds.h)
// ----------------------------------------------------------------------------
static const uint64_t K512[80] = {0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL, 0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
static const uint64_t IV512[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
static const uint64_t IV384[8] = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL, 0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
static const uint32_t IV224[8] = {0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u, 0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u};

inline std::string sha512Core(const std::string& data, const uint64_t iv[8], size_t outBytes) {
    uint64_t h[8];
    for (int i = 0; i < 8; ++i) h[i] = iv[i];
    std::string m = data;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 128 != 112) m += '\0';
    for (int i = 0; i < 8; ++i) m += '\0';                       // high 64 bits of the 128-bit length
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xff);
    for (size_t off = 0; off < m.size(); off += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) { uint64_t v = 0; for (int k = 0; k < 8; ++k) v = (v << 8) | static_cast<unsigned char>(m[off + static_cast<size_t>(i) * 8 + static_cast<size_t>(k)]); w[i] = v; }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ (~e & g);
            uint64_t t1 = hh + S1 + ch + K512[i] + w[i];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string o;
    for (int i = 0; i < 8; ++i) for (int k = 7; k >= 0; --k) o += static_cast<char>((h[i] >> (k * 8)) & 0xff);
    return hexOfStr(o.substr(0, outBytes));
}
inline std::string sha224Hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8];
    for (int i = 0; i < 8; ++i) h[i] = IV224[i];
    std::string m = data;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xff);
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(m[off + static_cast<size_t>(i) * 4])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + static_cast<size_t>(i) * 4 + 1])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + static_cast<size_t>(i) * 4 + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(m[off + static_cast<size_t>(i) * 4 + 3]));
        for (int i = 16; i < 64; ++i) { uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3); uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 7; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 56);
}
inline std::string hmacSha256(const std::string& key, const std::string& msg) {
    std::string k = key;
    if (k.size() > 64) { std::string raw; bytesOfHex(sha256hex(k), raw); k = raw; }
    k.resize(64, '\0');
    std::string ip(64, 0), op(64, 0);
    for (size_t i = 0; i < 64; ++i) { ip[i] = static_cast<char>(k[i] ^ 0x36); op[i] = static_cast<char>(k[i] ^ 0x5c); }
    std::string inner; bytesOfHex(sha256hex(ip + msg), inner);
    return sha256hex(op + inner);
}

// ----------------------------------------------------------------------------
// small hashes / checksums
// ----------------------------------------------------------------------------
inline uint16_t crc16ccitt(const std::string& s) { uint16_t c = 0xFFFF; for (unsigned char ch : s) { c ^= static_cast<uint16_t>(ch << 8); for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021) : static_cast<uint16_t>(c << 1); } return c; }
inline uint8_t crc8of(const std::string& s) { uint8_t c = 0; for (unsigned char ch : s) { c ^= ch; for (int k = 0; k < 8; ++k) c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0x07) : static_cast<uint8_t>(c << 1); } return c; }
inline uint32_t adler32of(const std::string& s) { uint32_t a = 1, b = 0; for (unsigned char ch : s) { a = (a + ch) % 65521; b = (b + a) % 65521; } return (b << 16) | a; }
inline uint32_t fletcher16of(const std::string& s) { uint32_t a = 0, b = 0; for (unsigned char ch : s) { a = (a + ch) % 255; b = (b + a) % 255; } return (b << 8) | a; }
inline uint32_t fnv1a32(const std::string& s) { uint32_t h = 2166136261u; for (unsigned char ch : s) { h ^= ch; h *= 16777619u; } return h; }
inline uint64_t fnv1a64(const std::string& s) { uint64_t h = 1469598103934665603ULL; for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ULL; } return h; }
inline uint32_t djb2of(const std::string& s) { uint32_t h = 5381; for (unsigned char ch : s) h = h * 33 + ch; return h; }
inline uint32_t sdbmof(const std::string& s) { uint32_t h = 0; for (unsigned char ch : s) h = ch + (h << 6) + (h << 16) - h; return h; }
inline uint32_t murmur3_32(const std::string& s, uint32_t seed = 0) {
    const uint32_t c1 = 0xcc9e2d51, c2 = 0x1b873593;
    uint32_t h = seed;
    size_t nb = s.size() / 4;
    for (size_t i = 0; i < nb; ++i) {
        uint32_t k = static_cast<uint32_t>(static_cast<unsigned char>(s[i * 4])) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i * 4 + 1])) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i * 4 + 2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i * 4 + 3])) << 24);
        k *= c1; k = rotl32(k, 15); k *= c2; h ^= k; h = rotl32(h, 13); h = h * 5 + 0xe6546b64;
    }
    uint32_t k = 0;
    size_t tail = nb * 4;
    switch (s.size() & 3) {
        case 3: k ^= static_cast<uint32_t>(static_cast<unsigned char>(s[tail + 2])) << 16; [[fallthrough]];
        case 2: k ^= static_cast<uint32_t>(static_cast<unsigned char>(s[tail + 1])) << 8; [[fallthrough]];
        case 1: k ^= static_cast<uint32_t>(static_cast<unsigned char>(s[tail])); k *= c1; k = rotl32(k, 15); k *= c2; h ^= k; break;
        default: break;
    }
    h ^= static_cast<uint32_t>(s.size());
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    return h;
}
inline uint32_t xxh32(const std::string& s, uint32_t seed = 0) {
    const uint32_t P1 = 2654435761u, P2 = 2246822519u, P3 = 3266489917u, P4 = 668265263u, P5 = 374761393u;
    auto rd = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(s[i])) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1])) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 3])) << 24); };
    size_t i = 0, n = s.size();
    uint32_t h;
    if (n >= 16) {
        uint32_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        for (; i + 16 <= n; i += 16) {
            v1 = rotl32(v1 + rd(i) * P2, 13) * P1; v2 = rotl32(v2 + rd(i + 4) * P2, 13) * P1;
            v3 = rotl32(v3 + rd(i + 8) * P2, 13) * P1; v4 = rotl32(v4 + rd(i + 12) * P2, 13) * P1;
        }
        h = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
    } else h = seed + P5;
    h += static_cast<uint32_t>(n);
    for (; i + 4 <= n; i += 4) h = rotl32(h + rd(i) * P3, 17) * P4;
    for (; i < n; ++i) h = rotl32(h + static_cast<unsigned char>(s[i]) * P5, 11) * P1;
    h ^= h >> 15; h *= P2; h ^= h >> 13; h *= P3; h ^= h >> 16;
    return h;
}
inline std::string hex32(uint32_t v) { char b[16]; std::snprintf(b, sizeof(b), "%08x", v); return b; }

// ----------------------------------------------------------------------------
// codecs
// ----------------------------------------------------------------------------
inline std::string b32enc(const std::string& s) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string o; uint32_t buf = 0; int bits = 0;
    for (unsigned char ch : s) { buf = (buf << 8) | ch; bits += 8; while (bits >= 5) { bits -= 5; o += A[(buf >> bits) & 31]; } }
    if (bits) o += A[(buf << (5 - bits)) & 31];
    while (o.size() % 8) o += '=';
    return o;
}
inline bool b32dec(const std::string& s, std::string& out) {
    out.clear(); uint32_t buf = 0; int bits = 0;
    for (char ch : s) {
        int d;
        if (ch >= 'A' && ch <= 'Z') d = ch - 'A'; else if (ch >= 'a' && ch <= 'z') d = ch - 'a'; else if (ch >= '2' && ch <= '7') d = ch - '2' + 26;
        else if (ch == '=' || std::isspace(static_cast<unsigned char>(ch))) continue; else return false;
        buf = (buf << 5) | static_cast<uint32_t>(d); bits += 5;
        if (bits >= 8) { bits -= 8; out += static_cast<char>((buf >> bits) & 0xff); }
    }
    return true;
}
inline std::string b58enc(const std::string& s) {
    static const char* A = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    size_t zeros = 0; while (zeros < s.size() && s[zeros] == 0) ++zeros;
    std::vector<unsigned char> d;                                  // base-58 digits, little endian
    for (size_t i = zeros; i < s.size(); ++i) {
        int carry = static_cast<unsigned char>(s[i]);
        for (auto& x : d) { carry += 256 * x; x = static_cast<unsigned char>(carry % 58); carry /= 58; }
        while (carry) { d.push_back(static_cast<unsigned char>(carry % 58)); carry /= 58; }
    }
    std::string o(zeros, '1');
    for (auto it = d.rbegin(); it != d.rend(); ++it) o += A[*it];
    return o;
}
inline bool b58dec(const std::string& s, std::string& out) {
    static const char* A = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    size_t zeros = 0; while (zeros < s.size() && s[zeros] == '1') ++zeros;
    std::vector<unsigned char> d;                                  // bytes, little endian
    for (size_t i = zeros; i < s.size(); ++i) {
        const char* p = std::strchr(A, s[i]);
        if (!p || !s[i]) return false;
        int carry = static_cast<int>(p - A);
        for (auto& x : d) { carry += 58 * x; x = static_cast<unsigned char>(carry & 0xff); carry >>= 8; }
        while (carry) { d.push_back(static_cast<unsigned char>(carry & 0xff)); carry >>= 8; }
    }
    out.assign(zeros, '\0');
    for (auto it = d.rbegin(); it != d.rend(); ++it) out += static_cast<char>(*it);
    return true;
}
inline std::string jsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break; case '\b': o += "\\b"; break; case '\f': o += "\\f"; break;
            default: if (ch < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", ch); o += b; } else o += static_cast<char>(ch);
        }
    }
    return o;
}
inline bool jsonUnesc(const std::string& s, std::string& o) {
    o.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { o += s[i]; continue; }
        if (++i >= s.size()) return false;
        switch (s[i]) {
            case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break; case 'b': o += '\b'; break; case 'f': o += '\f'; break;
            case '"': case '\\': case '/': o += s[i]; break;
            case 'u': {
                if (i + 4 >= s.size()) return false;
                uint32_t cp = static_cast<uint32_t>(std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16)); i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                    uint32_t lo = static_cast<uint32_t>(std::strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16));
                    if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                }
                o += cpToU8(cp); break;
            }
            default: return false;
        }
    }
    return true;
}
inline void addCodec(Reg& r, const std::string& name, const std::string& desc, std::function<std::string(const std::string&)> enc,
                     std::function<bool(const std::string&, std::string&)> dec) {
    r.add(name, "$ " + name + " [-d] <text…|-f FILE>", desc + (dec ? " (-d decodes)" : ""), true, [enc, dec](Ctx& c) -> int {
        bool d = !c.toks.empty() && c.toks[0].s == "-d";
        if (d && !dec) return c.fail("this codec has no decoder");
        std::string t;
        if (!textIn(c, t, d ? 1 : 0)) return 1;
        if (!d) { c.say(enc(t)); return 0; }
        std::string o;
        if (!dec(t, o)) return c.fail("input is not valid for decoding");
        c.say(o);
        return 0;
    });
}
inline bool luhnValid(const std::string& d) {
    int sum = 0; bool dbl = false; size_t n = 0;
    for (size_t i = d.size(); i-- > 0;) { unsigned char ch = static_cast<unsigned char>(d[i]); if (ch == ' ' || ch == '-') continue; if (!std::isdigit(ch)) return false; int v = ch - '0'; if (dbl) { v *= 2; if (v > 9) v -= 9; } sum += v; dbl = !dbl; ++n; }
    return n > 1 && sum % 10 == 0;
}
inline void addCodecs(Reg& r) {
    addCodec(r, "base32", "RFC 4648 Base32", b32enc, b32dec);
    addCodec(r, "base58", "Base58 (Bitcoin alphabet)", b58enc, b58dec);
    addCodec(r, "base64url", "URL-safe Base64 (no padding)", [](const std::string& s) { std::string o = b64enc(s); for (auto& ch : o) { if (ch == '+') ch = '-'; else if (ch == '/') ch = '_'; } while (!o.empty() && o.back() == '=') o.pop_back(); return o; },
             [](const std::string& s, std::string& o) { return b64dec(s, o); });
    addCodec(r, "binary", "text <-> 8-bit binary groups",
             [](const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); ++i) { if (i) o += ' '; for (int b = 7; b >= 0; --b) o += ((static_cast<unsigned char>(s[i]) >> b) & 1) ? '1' : '0'; } return o; },
             [](const std::string& s, std::string& o) { o.clear(); std::string bits; for (char ch : s) { if (ch == '0' || ch == '1') bits += ch; else if (!std::isspace(static_cast<unsigned char>(ch))) return false; } if (bits.size() % 8) return false; for (size_t i = 0; i < bits.size(); i += 8) o += static_cast<char>(std::strtol(bits.substr(i, 8).c_str(), nullptr, 2)); return true; });
    addCodec(r, "octal", "text <-> octal bytes (3 digits each)",
             [](const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); ++i) { char b[8]; std::snprintf(b, sizeof(b), "%03o", static_cast<unsigned char>(s[i])); if (i) o += ' '; o += b; } return o; },
             [](const std::string& s, std::string& o) { o.clear(); for (const auto& w : wordsOf(s)) { char* e = nullptr; long v = std::strtol(w.c_str(), &e, 8); if (!e || *e || v < 0 || v > 255) return false; o += static_cast<char>(v); } return true; });
    addCodec(r, "htmlesc", "escape & < > \" ' for HTML/XML",
             [](const std::string& s) { std::string o; for (char ch : s) { switch (ch) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; case '\'': o += "&#39;"; break; default: o += ch; } } return o; },
             [](const std::string& s, std::string& o) {
                 o.clear();
                 for (size_t i = 0; i < s.size(); ++i) {
                     if (s[i] != '&') { o += s[i]; continue; }
                     size_t e = s.find(';', i);
                     if (e == std::string::npos || e - i > 10) { o += '&'; continue; }
                     std::string n = s.substr(i + 1, e - i - 1);
                     if (n == "amp") o += '&'; else if (n == "lt") o += '<'; else if (n == "gt") o += '>'; else if (n == "quot") o += '"'; else if (n == "apos") o += '\''; else if (n == "nbsp") o += "\xC2\xA0";
                     else if (n.size() > 1 && n[0] == '#') { uint32_t cp = (n[1] == 'x' || n[1] == 'X') ? static_cast<uint32_t>(std::strtoul(n.c_str() + 2, nullptr, 16)) : static_cast<uint32_t>(std::strtoul(n.c_str() + 1, nullptr, 10)); o += cpToU8(cp); }
                     else { o += '&'; continue; }
                     i = e;
                 }
                 return true; });
    addCodec(r, "jsonesc", "escape text for use inside a JSON string", jsonEsc, jsonUnesc);
    addCodec(r, "shquote", "quote for a POSIX shell (single quotes)", [](const std::string& s) { std::string o = "'"; for (char ch : s) { if (ch == '\'') o += "'\\''"; else o += ch; } return o + "'"; }, nullptr);
    addCodec(r, "reesc", "escape regular-expression metacharacters", [](const std::string& s) { std::string o; for (char ch : s) { if (std::strchr("\\^$.|?*+()[]{}", ch) && ch) o += '\\'; o += ch; } return o; }, nullptr);
    addCodec(r, "csvesc", "quote a CSV field when needed", [](const std::string& s) { if (s.find_first_of(",\"\n\r") == std::string::npos) return s; std::string o = "\""; for (char ch : s) { if (ch == '"') o += '"'; o += ch; } return o + "\""; }, nullptr);
    addCodec(r, "qp", "quoted-printable",
             [](const std::string& s) { std::string o; for (unsigned char ch : s) { if ((ch >= 33 && ch <= 126 && ch != '=') || ch == ' ' || ch == '\n') o += static_cast<char>(ch); else { char b[8]; std::snprintf(b, sizeof(b), "=%02X", ch); o += b; } } return o; },
             [](const std::string& s, std::string& o) { o.clear(); for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '=' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) { o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; } else if (s[i] == '=' && i + 1 < s.size() && s[i + 1] == '\n') ++i; else o += s[i]; } return true; });
    addText(r, "utf8bytes", "UTF-8 bytes (hex) of every character", [](const std::string& t) { std::vector<std::string> o; for (const auto& ch : u8chars(t)) { if (ch == "\n") continue; o.push_back(ch + "=" + hexOfStr(ch)); } return joinStr(o, "  "); });
    addText(r, "utf16", "UTF-16 code units (hex) of the text", [](const std::string& t) {
        std::vector<std::string> o;
        for (const auto& ch : u8chars(t)) { uint32_t cp = u8cp(ch); char b[16]; if (cp >= 0x10000) { cp -= 0x10000; std::snprintf(b, sizeof(b), "%04X", 0xD800 + (cp >> 10)); o.push_back(b); std::snprintf(b, sizeof(b), "%04X", 0xDC00 + (cp & 0x3FF)); o.push_back(b); } else { std::snprintf(b, sizeof(b), "%04X", cp); o.push_back(b); } }
        return joinStr(o, " "); });
    addText(r, "codepoints", "Unicode code points (U+XXXX) of the text", [](const std::string& t) { std::vector<std::string> o; for (const auto& ch : u8chars(t)) { char b[16]; std::snprintf(b, sizeof(b), "U+%04X", u8cp(ch)); o.push_back(b); } return joinStr(o, " "); });
    addText(r, "fromcp", "code points (U+1F600, 0x41, 65) to text", [](const std::string& t) {
        std::string o;
        for (const auto& w : wordsOf(t)) { std::string x = w; if (x.size() > 2 && (x[0] == 'U' || x[0] == 'u') && x[1] == '+') x = "0x" + x.substr(2); char* e = nullptr; unsigned long v = std::strtoul(x.c_str(), &e, 0); if (!e || *e || v > 0x10FFFF) return std::string("error: bad code point '" + w + "'"); o += cpToU8(static_cast<uint32_t>(v)); }
        return o; });
}
inline void addHashes(Reg& r) {
    addText(r, "sha512", "SHA-512 digest (hex)", [](const std::string& t) { return sha512Core(t, IV512, 64); });
    addText(r, "sha384", "SHA-384 digest (hex)", [](const std::string& t) { return sha512Core(t, IV384, 48); });
    addText(r, "sha224", "SHA-224 digest (hex)", [](const std::string& t) { return sha224Hex(t); });
    addText(r, "crc16", "CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)", [](const std::string& t) { char b[16]; std::snprintf(b, sizeof(b), "%04x", crc16ccitt(t)); return std::string(b); });
    addText(r, "crc8", "CRC-8 (poly 0x07)", [](const std::string& t) { char b[16]; std::snprintf(b, sizeof(b), "%02x", crc8of(t)); return std::string(b); });
    addText(r, "adler32", "Adler-32 checksum (zlib)", [](const std::string& t) { return hex32(adler32of(t)); });
    addText(r, "fletcher16", "Fletcher-16 checksum", [](const std::string& t) { char b[16]; std::snprintf(b, sizeof(b), "%04x", fletcher16of(t)); return std::string(b); });
    addText(r, "djb2", "DJB2 hash (32-bit)", [](const std::string& t) { return hex32(djb2of(t)); });
    addText(r, "sdbm", "SDBM hash (32-bit)", [](const std::string& t) { return hex32(sdbmof(t)); });
    addText(r, "fnv1a32", "FNV-1a 32-bit hash", [](const std::string& t) { return hex32(fnv1a32(t)); });
    addText(r, "fnv1a64", "FNV-1a 64-bit hash", [](const std::string& t) { char b[24]; std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(fnv1a64(t))); return std::string(b); });
    addText(r, "murmur3", "MurmurHash3 x86 32-bit (seed 0)", [](const std::string& t) { return hex32(murmur3_32(t)); });
    addText(r, "xxh32", "xxHash32 (seed 0)", [](const std::string& t) { return hex32(xxh32(t)); });
    addParamText(r, "hmac", "<KEY>", "HMAC-SHA256 of the text with KEY", 1, [](const std::vector<std::string>& p, const std::string& t) { return hmacSha256(p[0], t); });
    // ---- check digits
    addText(r, "luhn", "validate a number with the Luhn algorithm (cards, IMEI)", [](const std::string& t) { return std::string(luhnValid(t) ? "valid" : "invalid"); });
    addText(r, "luhnmake", "append the Luhn check digit to a number", [](const std::string& t) {
        std::string d; for (char ch : t) if (std::isdigit(static_cast<unsigned char>(ch))) d += ch;
        if (d.empty()) return std::string("error: digits required");
        for (int k = 0; k < 10; ++k) if (luhnValid(d + std::to_string(k))) return d + std::to_string(k);
        return std::string("error"); });
    addText(r, "isbn", "validate an ISBN-10 / ISBN-13", [](const std::string& t) {
        std::string d; for (char ch : t) if (std::isdigit(static_cast<unsigned char>(ch)) || ch == 'X' || ch == 'x') d += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (d.size() == 10) { int s = 0; for (size_t i = 0; i < 10; ++i) { int v = d[i] == 'X' ? 10 : (std::isdigit(static_cast<unsigned char>(d[i])) ? d[i] - '0' : -1); if (v < 0 || (d[i] == 'X' && i != 9)) return std::string("invalid"); s += static_cast<int>(10 - i) * v; } return std::string(s % 11 == 0 ? "valid ISBN-10" : "invalid"); }
        if (d.size() == 13) { int s = 0; for (size_t i = 0; i < 13; ++i) { if (!std::isdigit(static_cast<unsigned char>(d[i]))) return std::string("invalid"); s += (d[i] - '0') * (i % 2 ? 3 : 1); } return std::string(s % 10 == 0 ? "valid ISBN-13" : "invalid"); }
        return std::string("invalid (needs 10 or 13 characters)"); });
    addText(r, "ean13", "EAN-13 check digit: give 12 digits to compute it, 13 to validate", [](const std::string& t) {
        std::string d; for (char ch : t) if (std::isdigit(static_cast<unsigned char>(ch))) d += ch;
        if (d.size() != 12 && d.size() != 13) return std::string("error: needs 12 or 13 digits");
        int s = 0; for (size_t i = 0; i < 12; ++i) s += (d[i] - '0') * (i % 2 ? 3 : 1);
        int chk = (10 - s % 10) % 10;
        if (d.size() == 12) return d + std::to_string(chk);
        return std::string(d[12] - '0' == chk ? "valid" : "invalid (expected check digit " + std::to_string(chk) + ")"); });
    addText(r, "iban", "validate an IBAN (mod 97) and show the country", [](const std::string& t) {
        std::string s; for (char ch : t) if (!std::isspace(static_cast<unsigned char>(ch))) s += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (s.size() < 15 || s.size() > 34) return std::string("invalid (length)");
        std::string re = s.substr(4) + s.substr(0, 4); long rem = 0;
        for (char ch : re) { if (std::isdigit(static_cast<unsigned char>(ch))) rem = (rem * 10 + (ch - '0')) % 97; else if (std::isalpha(static_cast<unsigned char>(ch))) rem = (rem * 100 + (ch - 'A' + 10)) % 97; else return std::string("invalid (characters)"); }
        return std::string(rem == 1 ? "valid, country " + s.substr(0, 2) : "invalid (checksum)"); });
    addText(r, "entropy", "Shannon entropy (bits per character and total)", [](const std::string& t) {
        std::map<std::string, size_t> m; auto cs = u8chars(t); for (const auto& c : cs) m[c]++;
        double h = 0; for (const auto& kv : m) { double p = static_cast<double>(kv.second) / static_cast<double>(cs.size()); h -= p * std::log2(p); }
        return fnum(h, 6) + " bits/char, " + fnum(h * static_cast<double>(cs.size()), 6) + " bits total (" + std::to_string(m.size()) + " distinct of " + std::to_string(cs.size()) + ")"; });
    addText(r, "strength", "password strength estimate (length, character classes, bits)", [](const std::string& t) {
        bool lo = false, up = false, di = false, sy = false; for (unsigned char ch : t) { if (std::islower(ch)) lo = true; else if (std::isupper(ch)) up = true; else if (std::isdigit(ch)) di = true; else sy = true; }
        int pool = (lo ? 26 : 0) + (up ? 26 : 0) + (di ? 10 : 0) + (sy ? 33 : 0);
        double bits = static_cast<double>(u8len(t)) * std::log2(static_cast<double>(std::max(pool, 1)));
        const char* verdict = bits < 28 ? "very weak" : bits < 36 ? "weak" : bits < 60 ? "fair" : bits < 80 ? "strong" : "very strong";
        return "length " + std::to_string(u8len(t)) + ", classes " + std::to_string((lo ? 1 : 0) + (up ? 1 : 0) + (di ? 1 : 0) + (sy ? 1 : 0)) + ", ~" + fnum(bits, 4) + " bits: " + verdict; });
}

// ----------------------------------------------------------------------------
// colors
// ----------------------------------------------------------------------------
struct RGB { int r = 0, g = 0, b = 0; };
inline bool parseColor(const std::string& in, RGB& c);
inline const std::vector<std::pair<const char*, const char*>>& colorNames() {
    static const std::vector<std::pair<const char*, const char*>> t = {
        {"black", "000000"}, {"white", "ffffff"}, {"red", "ff0000"}, {"lime", "00ff00"}, {"blue", "0000ff"}, {"yellow", "ffff00"}, {"cyan", "00ffff"}, {"magenta", "ff00ff"}, {"silver", "c0c0c0"}, {"gray", "808080"},
        {"maroon", "800000"}, {"olive", "808000"}, {"green", "008000"}, {"purple", "800080"}, {"teal", "008080"}, {"navy", "000080"}, {"orange", "ffa500"}, {"pink", "ffc0cb"}, {"brown", "a52a2a"}, {"gold", "ffd700"},
        {"coral", "ff7f50"}, {"salmon", "fa8072"}, {"tomato", "ff6347"}, {"crimson", "dc143c"}, {"indigo", "4b0082"}, {"violet", "ee82ee"}, {"turquoise", "40e0d0"}, {"khaki", "f0e68c"}, {"beige", "f5f5dc"}, {"ivory", "fffff0"},
        {"lavender", "e6e6fa"}, {"orchid", "da70d6"}, {"plum", "dda0dd"}, {"tan", "d2b48c"}, {"chocolate", "d2691e"}, {"sienna", "a0522d"}, {"skyblue", "87ceeb"}, {"steelblue", "4682b4"}, {"slategray", "708090"}, {"aqua", "00ffff"},
    };
    return t;
}
inline bool parseColor(const std::string& in, RGB& c) {
    std::string s = lowerStr(in);
    for (const auto& n : colorNames()) if (s == n.first) { s = n.second; break; }
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.find(',') != std::string::npos) {
        int v[3] = {0, 0, 0}; int k = 0; std::stringstream ss(s); std::string part;
        while (std::getline(ss, part, ',') && k < 3) { char* e = nullptr; long x = std::strtol(part.c_str(), &e, 10); if (!e || *e || x < 0 || x > 255) return false; v[k++] = static_cast<int>(x); }
        if (k != 3) return false;
        c = {v[0], v[1], v[2]}; return true;
    }
    if (s.size() == 3) s = std::string{s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6) return false;
    for (char ch : s) if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    c = {static_cast<int>(std::strtol(s.substr(0, 2).c_str(), nullptr, 16)), static_cast<int>(std::strtol(s.substr(2, 2).c_str(), nullptr, 16)), static_cast<int>(std::strtol(s.substr(4, 2).c_str(), nullptr, 16))};
    return true;
}
inline std::string hexOfRgb(const RGB& c) { char b[16]; std::snprintf(b, sizeof(b), "#%02x%02x%02x", std::clamp(c.r, 0, 255), std::clamp(c.g, 0, 255), std::clamp(c.b, 0, 255)); return b; }
inline void rgbToHsl(const RGB& c, double& h, double& s, double& l) {
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    l = (mx + mn) / 2; h = 0; s = 0;
    if (d > 1e-12) {
        s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) h = std::fmod((g - b) / d + 6, 6); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
        h *= 60;
    }
}
inline RGB hslToRgb(double h, double s, double l) {
    h = std::fmod(std::fmod(h, 360) + 360, 360);
    double cc = (1 - std::fabs(2 * l - 1)) * s, x = cc * (1 - std::fabs(std::fmod(h / 60, 2) - 1)), m = l - cc / 2, r = 0, g = 0, b = 0;
    if (h < 60) { r = cc; g = x; } else if (h < 120) { r = x; g = cc; } else if (h < 180) { g = cc; b = x; } else if (h < 240) { g = x; b = cc; } else if (h < 300) { r = x; b = cc; } else { r = cc; b = x; }
    return {static_cast<int>(std::lround((r + m) * 255)), static_cast<int>(std::lround((g + m) * 255)), static_cast<int>(std::lround((b + m) * 255))};
}
inline double relLuminance(const RGB& c) {
    auto f = [](int v) { double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b);
}
inline std::string hslStr(const RGB& c) { double h, s, l; rgbToHsl(c, h, s, l); return "hsl(" + fnum(std::round(h), 4) + ", " + fnum(std::round(s * 100), 4) + "%, " + fnum(std::round(l * 100), 4) + "%)"; }
inline void addColors(Reg& r) {
    auto col1 = [&r](const std::string& name, const std::string& desc, std::function<std::string(const RGB&)> f, const std::string& extra = "") {
        r.add(name, "$ " + name + " " + (extra.empty() ? "<color…>" : extra), desc, false, [f, extra](Ctx& c) -> int {
            if (c.toks.empty()) return c.fail("missing color   (#rrggbb, #rgb, r,g,b or a name like coral)");
            for (const auto& t : c.toks) { RGB x; if (!parseColor(t.s, x)) return c.fail("bad color '" + t.s + "'"); c.say(f(x)); }
            return 0;
        });
    };
    col1("hex2rgb", "hex color to rgb(r, g, b)", [](const RGB& c) { return "rgb(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ")"; });
    r.add("rgb2hex", "$ rgb2hex <r> <g> <b>", "red green blue (0-255) to #rrggbb", false, [](Ctx& c) -> int {
        std::vector<double> v; if (!numsIn(c, v, 3)) return 1;
        if (v.size() != 3) return c.fail("expected exactly 3 numbers");
        RGB x{static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2])};
        for (double d : v) if (d < 0 || d > 255) return c.fail("components must be 0..255");
        c.say(hexOfRgb(x)); return 0; });
    col1("rgb2hsl", "color to hsl(h, s%, l%)", [](const RGB& c) { return hslStr(c); });
    col1("hex2hsl", "color to hsl(h, s%, l%) (same as rgb2hsl, accepts hex)", [](const RGB& c) { return hslStr(c); });
    r.add("hsl2rgb", "$ hsl2rgb <h 0-360> <s 0-100> <l 0-100>", "HSL to #rrggbb and rgb()", false, [](Ctx& c) -> int {
        std::vector<double> v; if (!numsIn(c, v, 3)) return 1;
        if (v.size() != 3) return c.fail("expected exactly 3 numbers");
        RGB x = hslToRgb(v[0], std::clamp(v[1], 0.0, 100.0) / 100.0, std::clamp(v[2], 0.0, 100.0) / 100.0);
        c.say(hexOfRgb(x) + "  rgb(" + std::to_string(x.r) + ", " + std::to_string(x.g) + ", " + std::to_string(x.b) + ")"); return 0; });
    auto shade = [&r](const std::string& name, const std::string& desc, int sign) {
        r.add(name, "$ " + name + " <color> [percent=10]", desc, false, [sign](Ctx& c) -> int {
            if (c.toks.empty()) return c.fail("missing color");
            RGB x; if (!parseColor(c.toks[0].s, x)) return c.fail("bad color '" + c.toks[0].s + "'");
            double p = c.toks.size() > 1 ? std::strtod(c.toks[1].s.c_str(), nullptr) : 10.0;
            double h, s, l; rgbToHsl(x, h, s, l); l = std::clamp(l + sign * p / 100.0, 0.0, 1.0);
            c.say(hexOfRgb(hslToRgb(h, s, l))); return 0; });
    };
    shade("lighten", "make a color lighter by N percent (HSL lightness)", +1);
    shade("darken", "make a color darker by N percent (HSL lightness)", -1);
    r.add("mix", "$ mix <color1> <color2> [weight2=50]", "blend two colors (weight of the second color in percent)", false, [](Ctx& c) -> int {
        if (c.toks.size() < 2) return c.fail("usage: $ mix <color1> <color2> [percent]");
        RGB a, b; if (!parseColor(c.toks[0].s, a) || !parseColor(c.toks[1].s, b)) return c.fail("bad color");
        double w = c.toks.size() > 2 ? std::clamp(std::strtod(c.toks[2].s.c_str(), nullptr), 0.0, 100.0) / 100.0 : 0.5;
        RGB m{static_cast<int>(std::lround(a.r * (1 - w) + b.r * w)), static_cast<int>(std::lround(a.g * (1 - w) + b.g * w)), static_cast<int>(std::lround(a.b * (1 - w) + b.b * w))};
        c.say(hexOfRgb(m)); return 0; });
    r.add("contrast", "$ contrast <foreground> <background>", "WCAG contrast ratio with AA / AAA verdicts", false, [](Ctx& c) -> int {
        if (c.toks.size() != 2) return c.fail("usage: $ contrast <foreground> <background>");
        RGB a, b; if (!parseColor(c.toks[0].s, a) || !parseColor(c.toks[1].s, b)) return c.fail("bad color");
        double l1 = relLuminance(a), l2 = relLuminance(b); if (l1 < l2) std::swap(l1, l2);
        double ratio = (l1 + 0.05) / (l2 + 0.05);
        c.say(fnum(ratio, 4) + ":1   normal text AA " + (ratio >= 4.5 ? "pass" : "fail") + ", AAA " + (ratio >= 7 ? "pass" : "fail") + "; large text AA " + (ratio >= 3 ? "pass" : "fail")); return 0; });
    col1("luminance", "WCAG relative luminance (0..1)", [](const RGB& c) { return fnum(relLuminance(c), 6); });
    col1("complement", "complementary color (hue + 180)", [](const RGB& c) { double h, s, l; rgbToHsl(c, h, s, l); return hexOfRgb(hslToRgb(h + 180, s, l)); });
    col1("grayscale", "convert to gray (luma 0.299/0.587/0.114)", [](const RGB& c) { int g = static_cast<int>(std::lround(0.299 * c.r + 0.587 * c.g + 0.114 * c.b)); return hexOfRgb({g, g, g}); });
    col1("invertcolor", "invert a color (255 - channel)", [](const RGB& c) { return hexOfRgb({255 - c.r, 255 - c.g, 255 - c.b}); });
    r.add("palette", "$ palette <color> [count=5]", "tints and shades of a color (HSL lightness steps)", false, [](Ctx& c) -> int {
        if (c.toks.empty()) return c.fail("missing color");
        RGB x; if (!parseColor(c.toks[0].s, x)) return c.fail("bad color");
        long n = c.toks.size() > 1 ? std::strtol(c.toks[1].s.c_str(), nullptr, 10) : 5; n = std::clamp(n, 2L, 20L);
        double h, s, l; rgbToHsl(x, h, s, l);
        for (long i = 0; i < n; ++i) { double li = 0.1 + 0.8 * static_cast<double>(i) / static_cast<double>(n - 1); c.say(hexOfRgb(hslToRgb(h, s, li)) + "  L=" + fnum(std::round(li * 100), 4) + "%"); }
        return 0; });
    r.add("colorname", "$ colorname <color|name>", "nearest CSS-style color name (or the hex of a name)", false, [](Ctx& c) -> int {
        if (c.toks.empty()) return c.fail("missing color or name");
        RGB x; if (!parseColor(c.toks[0].s, x)) return c.fail("bad color '" + c.toks[0].s + "'");
        std::string best; double bd = 1e18;
        for (const auto& n : colorNames()) { RGB y; parseColor(n.second, y); double d = std::pow(x.r - y.r, 2) + std::pow(x.g - y.g, 2) + std::pow(x.b - y.b, 2); if (d < bd) { bd = d; best = n.first; } }
        c.say(hexOfRgb(x) + " ~ " + best + (bd == 0 ? " (exact)" : "")); return 0; });
}

// ----------------------------------------------------------------------------
// network / reference
// ----------------------------------------------------------------------------
inline bool parseIpv4(const std::string& s, uint32_t& out) {
    uint32_t v = 0; int parts = 0; size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('.', i); std::string p = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (p.empty() || p.size() > 3 || p.find_first_not_of("0123456789") != std::string::npos) return false;
        long x = std::strtol(p.c_str(), nullptr, 10); if (x > 255) return false;
        v = (v << 8) | static_cast<uint32_t>(x); ++parts;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    if (parts != 4) return false;
    out = v;
    return true;
}
inline std::string ipv4Str(uint32_t v) { return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 255) + "." + std::to_string((v >> 8) & 255) + "." + std::to_string(v & 255); }
inline bool parseCidr(const std::string& s, uint32_t& ip, int& pre) {
    size_t sl = s.find('/'); std::string a = s.substr(0, sl);
    if (!parseIpv4(a, ip)) return false;
    pre = 32;
    if (sl != std::string::npos) { std::string p = s.substr(sl + 1); if (p.empty() || p.find_first_not_of("0123456789") != std::string::npos) return false; pre = std::atoi(p.c_str()); if (pre < 0 || pre > 32) return false; }
    return true;
}
inline uint32_t maskOf(int pre) { return pre == 0 ? 0u : 0xFFFFFFFFu << (32 - pre); }
inline bool validIpv6(const std::string& s) {
    if (s.empty() || s.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos) return false;
    size_t dbl = s.find("::"); if (dbl != std::string::npos && s.find("::", dbl + 1) != std::string::npos) return false;
    int groups = 0; size_t i = 0; std::string t = s;
    if (t.find('.') != std::string::npos) { size_t ls = t.rfind(':'); uint32_t x; if (ls == std::string::npos || !parseIpv4(t.substr(ls + 1), x)) return false; t = t.substr(0, ls + 1) + "0:0"; }
    while (i < t.size()) { size_t j = t.find(':', i); std::string g = t.substr(i, j == std::string::npos ? std::string::npos : j - i); if (!g.empty()) { if (g.size() > 4) return false; ++groups; } if (j == std::string::npos) break;
        i = j + 1; }
    return dbl != std::string::npos ? groups < 8 : groups == 8;
}
inline void addNet(Reg& r) {
    auto ip1 = [&r](const std::string& name, const std::string& desc, std::function<std::string(const std::string&)> f, const std::string& arg = "<ip…>") {
        r.add(name, "$ " + name + " " + arg, desc, false, [f](Ctx& c) -> int {
            if (c.toks.empty()) return c.fail("missing argument");
            for (const auto& t : c.toks) { std::string o = f(t.s); if (o.compare(0, 6, "error:") == 0) return c.fail(o.substr(7)); c.say(o); }
            return 0; });
    };
    ip1("ip2int", "IPv4 address to a 32-bit integer", [](const std::string& s) { uint32_t v; if (!parseIpv4(s, v)) return std::string("error: bad IPv4 address '" + s + "'"); return std::to_string(v); });
    ip1("int2ip", "32-bit integer to an IPv4 address", [](const std::string& s) { double d; if (!parseNum(s, d) || d < 0 || d > 4294967295.0) return std::string("error: bad number '" + s + "'"); return ipv4Str(static_cast<uint32_t>(d)); }, "<int…>");
    ip1("ipbin", "IPv4 address in binary", [](const std::string& s) { uint32_t v; if (!parseIpv4(s, v)) return std::string("error: bad IPv4 address '" + s + "'"); std::string o; for (int i = 3; i >= 0; --i) { for (int b = 7; b >= 0; --b) o += ((v >> (i * 8 + b)) & 1) ? '1' : '0'; if (i) o += '.'; } return o; });
    ip1("cidr", "network details of an IPv4/prefix: mask, network, broadcast, host range, count", [](const std::string& s) {
        uint32_t ip; int pre; if (!parseCidr(s, ip, pre)) return std::string("error: bad CIDR '" + s + "'   e.g. 192.168.1.10/24");
        uint32_t m = maskOf(pre), net = ip & m, bc = net | ~m;
        unsigned long long total = 1ULL << (32 - pre), hosts = pre >= 31 ? total : total - 2;
        std::string o = "network    " + ipv4Str(net) + "/" + std::to_string(pre) + "\nnetmask    " + ipv4Str(m) + "\nwildcard   " + ipv4Str(~m) + "\nbroadcast  " + ipv4Str(bc) + "\n";
        o += "first host " + ipv4Str(pre >= 31 ? net : net + 1) + "\nlast host  " + ipv4Str(pre >= 31 ? bc : bc - 1) + "\nhosts      " + std::to_string(hosts) + " (" + std::to_string(total) + " addresses)";
        return o; }, "<ip/prefix…>");
    ip1("netmask", "prefix length to netmask, or netmask to prefix length", [](const std::string& s) {
        uint32_t m;
        if (s.find('.') == std::string::npos) { if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos || std::atoi(s.c_str()) > 32) return std::string("error: bad prefix '" + s + "'"); int p = std::atoi(s.c_str()); return ipv4Str(maskOf(p)) + "  (wildcard " + ipv4Str(~maskOf(p)) + ")"; }
        if (!parseIpv4(s, m)) return std::string("error: bad mask '" + s + "'");
        int p = 0; while (p < 32 && (m & (0x80000000u >> p))) ++p;
        if (maskOf(p) != m) return std::string("error: not a contiguous netmask");
        return "/" + std::to_string(p); }, "<prefix|mask…>");
    ip1("ipcheck", "validate IPv4 / IPv6 and classify it (private, loopback, …)", [](const std::string& s) {
        uint32_t v;
        if (parseIpv4(s, v)) {
            std::string kind = "public";
            if ((v >> 24) == 10 || (v >> 20) == 0xAC1 || (v >> 16) == 0xC0A8) kind = "private (RFC 1918)"; else if ((v >> 24) == 127) kind = "loopback"; else if ((v >> 16) == 0xA9FE) kind = "link-local"; else if ((v >> 28) == 0xE) kind = "multicast"; else if (v == 0) kind = "unspecified"; else if ((v >> 22) == 0x191) kind = "carrier-grade NAT"; else if ((v >> 28) == 0xF) kind = "reserved";
            return s + ": valid IPv4, " + kind; }
        if (validIpv6(s)) return s + ": valid IPv6";
        return s + ": invalid"; });
    ip1("macfmt", "normalize a MAC address to aa:bb:cc:dd:ee:ff (also shows the OUI and flags)", [](const std::string& s) {
        std::string h; for (char ch : s) if (std::isxdigit(static_cast<unsigned char>(ch))) h += static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); else if (ch != ':' && ch != '-' && ch != '.') return std::string("error: bad MAC address '" + s + "'");
        if (h.size() != 12) return std::string("error: a MAC address has 12 hex digits");
        std::string o; for (size_t i = 0; i < 12; i += 2) { if (i) o += ':'; o += h.substr(i, 2); }
        int b0 = static_cast<int>(std::strtol(h.substr(0, 2).c_str(), nullptr, 16));
        return o + "   OUI " + h.substr(0, 6) + (b0 & 1 ? ", multicast" : ", unicast") + (b0 & 2 ? ", locally administered" : ", universal"); }, "<mac…>");
    r.add("ipinrange", "$ ipinrange <ip> <cidr>", "is the IPv4 address inside the network?", false, [](Ctx& c) -> int {
        if (c.toks.size() != 2) return c.fail("usage: $ ipinrange <ip> <ip/prefix>");
        uint32_t ip, net; int pre;
        if (!parseIpv4(c.toks[0].s, ip) || !parseCidr(c.toks[1].s, net, pre)) return c.fail("bad address or network");
        bool in = (ip & maskOf(pre)) == (net & maskOf(pre));
        c.say(std::string(in ? "yes" : "no")); return in ? 0 : 1; });
    r.add("urlparse", "$ urlparse <url…>", "split a URL into scheme, user, host, port, path, query parameters and fragment", false, [](Ctx& c) -> int {
        if (c.toks.empty()) return c.fail("missing URL");
        for (const auto& tk : c.toks) {
            std::string u = tk.s, scheme, auth, path, query, frag;
            size_t p = u.find("://"); if (p != std::string::npos) { scheme = u.substr(0, p); u = u.substr(p + 3); }
            size_t h = u.find('#'); if (h != std::string::npos) { frag = u.substr(h + 1); u = u.substr(0, h); }
            size_t q = u.find('?'); if (q != std::string::npos) { query = u.substr(q + 1); u = u.substr(0, q); }
            size_t sl = u.find('/'); if (sl != std::string::npos) { path = u.substr(sl); u = u.substr(0, sl); }
            auth = u; std::string user, host = auth, port;
            size_t at = auth.rfind('@'); if (at != std::string::npos) { user = auth.substr(0, at); host = auth.substr(at + 1); }
            size_t co = host.rfind(':'); if (co != std::string::npos && host.find(']') == std::string::npos) { port = host.substr(co + 1); host = host.substr(0, co); }
            c.say("scheme    " + scheme); if (!user.empty()) c.say("user      " + user); c.say("host      " + host); if (!port.empty()) c.say("port      " + port);
            c.say("path      " + (path.empty() ? "/" : path));
            if (!query.empty()) { std::stringstream ss(query); std::string kv; while (std::getline(ss, kv, '&')) { size_t e = kv.find('='); c.say("  param   " + (e == std::string::npos ? kv : kv.substr(0, e)) + " = " + (e == std::string::npos ? "" : kv.substr(e + 1))); } }
            if (!frag.empty()) c.say("fragment  " + frag);
        }
        return 0; });
    ip1("domainof", "host name of a URL or e-mail address", [](const std::string& s) { std::string u = s; size_t p = u.find("://"); if (p != std::string::npos) u = u.substr(p + 3); size_t at = u.rfind('@'); if (at != std::string::npos && at < u.find('/')) u = u.substr(at + 1); size_t e = u.find_first_of("/:?#"); return u.substr(0, e); }, "<url|email…>");
    ip1("emailcheck", "validate the syntax of an e-mail address", [](const std::string& s) {
        static const std::regex re("^[A-Za-z0-9._%+\\-]+@[A-Za-z0-9](?:[A-Za-z0-9\\-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9\\-]{0,61}[A-Za-z0-9])?)+$");
        return s + ": " + (s.size() <= 254 && std::regex_match(s, re) ? "valid" : "invalid"); }, "<email…>");
    static const char* PORTS[] = {"20 ftp-data", "21 ftp", "22 ssh", "23 telnet", "25 smtp", "53 dns", "67 dhcp-server", "68 dhcp-client", "69 tftp", "80 http", "88 kerberos", "110 pop3", "119 nntp", "123 ntp", "135 msrpc", "137 netbios-ns", "139 netbios-ssn", "143 imap", "161 snmp", "162 snmp-trap", "179 bgp", "389 ldap", "443 https", "445 smb", "465 smtps", "514 syslog", "515 lpd", "587 submission", "636 ldaps", "993 imaps", "995 pop3s", "1080 socks", "1433 mssql", "1521 oracle", "1723 pptp", "2049 nfs", "3000 dev-server", "3306 mysql", "3389 rdp", "5060 sip", "5432 postgresql", "5672 amqp", "5900 vnc", "6379 redis", "8080 http-alt", "8443 https-alt", "9000 php-fpm/sonarqube", "9092 kafka", "9200 elasticsearch", "27017 mongodb"};
    r.add("port", "$ port <number|name…>", "well-known TCP/UDP port lookup (by number or service name)", false, [](Ctx& c) -> int {
        if (c.toks.empty()) { for (const char* p : PORTS) c.say(p); return 0; }
        for (const auto& t : c.toks) { bool f = false; for (const char* p : PORTS) { std::string e = p; if (e.compare(0, t.s.size() + 1, t.s + " ") == 0 || e.find(" " + lowerStr(t.s)) != std::string::npos) { c.say(e); f = true; } } if (!f) c.warn("no entry for '" + t.s + "'"); }
        return c.rc; });
    static const char* HTTP[] = {"100 Continue", "101 Switching Protocols", "200 OK", "201 Created", "202 Accepted", "204 No Content", "206 Partial Content", "301 Moved Permanently", "302 Found", "303 See Other", "304 Not Modified", "307 Temporary Redirect", "308 Permanent Redirect", "400 Bad Request", "401 Unauthorized", "403 Forbidden", "404 Not Found", "405 Method Not Allowed", "406 Not Acceptable", "408 Request Timeout", "409 Conflict", "410 Gone", "411 Length Required", "412 Precondition Failed", "413 Payload Too Large", "414 URI Too Long", "415 Unsupported Media Type", "416 Range Not Satisfiable", "418 I'm a teapot", "422 Unprocessable Content", "423 Locked", "425 Too Early", "426 Upgrade Required", "428 Precondition Required", "429 Too Many Requests", "431 Request Header Fields Too Large", "451 Unavailable For Legal Reasons", "500 Internal Server Error", "501 Not Implemented", "502 Bad Gateway", "503 Service Unavailable", "504 Gateway Timeout", "505 HTTP Version Not Supported", "507 Insufficient Storage", "511 Network Authentication Required"};
    r.add("httpstatus", "$ httpstatus <code|text…>", "HTTP status code lookup (by code, class like 4xx, or words)", false, [](Ctx& c) -> int {
        if (c.toks.empty()) { for (const char* p : HTTP) c.say(p); return 0; }
        for (const auto& t : c.toks) { std::string k = lowerStr(t.s); bool f = false; for (const char* p : HTTP) { std::string e = p; bool m = e.compare(0, k.size(), k) == 0 || (k.size() == 3 && k[1] == 'x' && k[2] == 'x' && e[0] == k[0]) || lowerStr(e).find(k) != std::string::npos; if (m) { c.say(e); f = true; } } if (!f) c.warn("no entry for '" + t.s + "'"); }
        return c.rc; });
    static const char* MIME[] = {"txt text/plain", "html text/html", "css text/css", "js text/javascript", "mjs text/javascript", "json application/json", "xml application/xml", "csv text/csv", "md text/markdown", "pdf application/pdf", "zip application/zip", "gz application/gzip", "tar application/x-tar", "7z application/x-7z-compressed", "png image/png", "jpg image/jpeg", "jpeg image/jpeg", "gif image/gif", "webp image/webp", "svg image/svg+xml", "ico image/vnd.microsoft.icon", "bmp image/bmp", "avif image/avif", "mp3 audio/mpeg", "wav audio/wav", "ogg audio/ogg", "flac audio/flac", "mp4 video/mp4", "webm video/webm", "mkv video/x-matroska", "mov video/quicktime", "avi video/x-msvideo", "woff font/woff", "woff2 font/woff2", "ttf font/ttf", "otf font/otf", "wasm application/wasm", "doc application/msword", "docx application/vnd.openxmlformats-officedocument.wordprocessingml.document", "xls application/vnd.ms-excel", "xlsx application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "ppt application/vnd.ms-powerpoint", "pptx application/vnd.openxmlformats-officedocument.presentationml.presentation", "apk application/vnd.android.package-archive", "rin text/x-rin", "yaml application/yaml", "toml application/toml", "sh application/x-sh", "py text/x-python", "c text/x-c", "cpp text/x-c++", "h text/x-c", "java text/x-java-source", "kt text/x-kotlin"};
    r.add("mime", "$ mime <extension|file…>", "MIME type of a file extension or file name", false, [](Ctx& c) -> int {
        if (c.toks.empty()) return c.fail("missing extension or file name");
        for (const auto& t : c.toks) { std::string e = lowerStr(t.s); size_t d = e.rfind('.'); if (d != std::string::npos) e = e.substr(d + 1); bool f = false; for (const char* p : MIME) { std::string m = p; if (m.compare(0, e.size() + 1, e + " ") == 0) { c.say(t.s + ": " + m.substr(e.size() + 1)); f = true; break; } } if (!f) c.say(t.s + ": application/octet-stream (unknown)"); }
        return 0; });
    auto semParse = [](const std::string& s, long v[3], std::string& pre) -> bool {
        std::string t = s; if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
        size_t m = t.find('+'); if (m != std::string::npos) t = t.substr(0, m);
        size_t h = t.find('-'); pre.clear(); if (h != std::string::npos) { pre = t.substr(h + 1); t = t.substr(0, h); }
        int k = 0; std::stringstream ss(t); std::string part;
        while (std::getline(ss, part, '.')) { if (k >= 3 || part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return false; v[k++] = std::strtol(part.c_str(), nullptr, 10); }
        return k == 3;
    };
    r.add("semverbump", "$ semverbump <major|minor|patch> <version…>", "increment a semantic version", false, [semParse](Ctx& c) -> int {
        if (c.toks.size() < 2) return c.fail("usage: $ semverbump <major|minor|patch> <version…>");
        std::string part = lowerStr(c.toks[0].s); if (part != "major" && part != "minor" && part != "patch") return c.fail("first argument must be major, minor or patch");
        for (size_t i = 1; i < c.toks.size(); ++i) { long v[3]; std::string pre; if (!semParse(c.toks[i].s, v, pre)) return c.fail("not a semantic version: '" + c.toks[i].s + "'"); if (part == "major") { ++v[0]; v[1] = v[2] = 0; } else if (part == "minor") { ++v[1]; v[2] = 0; } else ++v[2]; c.say(std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2])); }
        return 0; });
    r.add("semvercmp", "$ semvercmp <a> <b>", "compare two semantic versions (prints <, = or >)", false, [semParse](Ctx& c) -> int {
        if (c.toks.size() != 2) return c.fail("usage: $ semvercmp <a> <b>");
        long a[3], b[3]; std::string pa, pb;
        if (!semParse(c.toks[0].s, a, pa) || !semParse(c.toks[1].s, b, pb)) return c.fail("not a semantic version");
        int cmp = 0; for (int i = 0; i < 3 && !cmp; ++i) cmp = a[i] < b[i] ? -1 : (a[i] > b[i] ? 1 : 0);
        if (!cmp && pa != pb) cmp = pa.empty() ? 1 : (pb.empty() ? -1 : (pa < pb ? -1 : 1));
        c.say(std::string(cmp < 0 ? "<" : (cmp > 0 ? ">" : "="))); return 0; });
}

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin
