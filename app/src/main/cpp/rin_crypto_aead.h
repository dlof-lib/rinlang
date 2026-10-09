// rin_crypto_aead.h — standard cryptographic primitives for the Rin engine (header-only, no external dependency).
//
//   * OS random bytes         (/dev/urandom, fallback std::random_device) — replaces mt19937 for secrets
//   * AES-128/192/256         (FIPS 197)
//   * AES-GCM                 (NIST SP 800-38D, 96-bit nonce, 128-bit tag)
//   * PBKDF2-HMAC-SHA256      (RFC 8018 / RFC 7914 test vectors)
//
// Verified against the NIST GCM test cases and the RFC 7914 PBKDF2 vectors (see tools/test_crypto_aead.cpp).
// Honest note: AES uses lookup tables, so it is not hardened against cache-timing attacks from a
// co-located attacker. Tag comparison is constant-time.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include "clc/sha256.h"

namespace rincrypto {

// ---------------------------------------------------------------- OS randomness
inline bool osRandomBytes(uint8_t* out, size_t n) {
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = std::fread(out, 1, n, f);
        std::fclose(f);
        if (got == n) return true;
    }
    std::random_device rd;   // fallback (Windows/other): non-deterministic device where available
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(rd() & 0xFF);
    return true;
}

// ---------------------------------------------------------------- AES
struct AesTables {
    uint8_t sbox[256];
    AesTables() {
        // build the S-box from the GF(2^8) inverse + affine transform (avoids a hand-typed table)
        uint8_t p = 1, q = 1;
        do {
            p = p ^ (uint8_t)(p << 1) ^ ((p & 0x80) ? 0x1B : 0);        // p *= 3
            q ^= q << 1; q ^= q << 2; q ^= q << 4; if (q & 0x80) q ^= 0x09; // q /= 3
            uint8_t x = q ^ (uint8_t)((q << 1) | (q >> 7)) ^ (uint8_t)((q << 2) | (q >> 6))
                          ^ (uint8_t)((q << 3) | (q >> 5)) ^ (uint8_t)((q << 4) | (q >> 4));
            sbox[p] = x ^ 0x63;
        } while (p != 1);
        sbox[0] = 0x63;
    }
};
inline const AesTables& aesTables() { static const AesTables t; return t; }

inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

struct AesKey {
    uint8_t rk[15][16];
    int rounds = 0;
};

inline bool aesSetKey(AesKey& k, const uint8_t* key, size_t len) {
    if (len != 16 && len != 24 && len != 32) return false;
    const uint8_t* S = aesTables().sbox;
    int nk = (int)len / 4;
    k.rounds = nk + 6;
    int total = 4 * (k.rounds + 1);
    std::vector<uint8_t> w(total * 4);
    std::memcpy(w.data(), key, len);
    uint8_t rcon = 1;
    for (int i = nk; i < total; ++i) {
        uint8_t t[4];
        std::memcpy(t, &w[(i - 1) * 4], 4);
        if (i % nk == 0) {
            uint8_t a = t[0];
            t[0] = S[t[1]] ^ rcon; t[1] = S[t[2]]; t[2] = S[t[3]]; t[3] = S[a];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; ++j) t[j] = S[t[j]];
        }
        for (int j = 0; j < 4; ++j) w[i * 4 + j] = w[(i - nk) * 4 + j] ^ t[j];
    }
    for (int r = 0; r <= k.rounds; ++r) std::memcpy(k.rk[r], &w[r * 16], 16);
    return true;
}

inline void aesEncryptBlock(const AesKey& k, const uint8_t in[16], uint8_t out[16]) {
    const uint8_t* S = aesTables().sbox;
    uint8_t s[16];
    for (int i = 0; i < 16; ++i) s[i] = in[i] ^ k.rk[0][i];
    for (int r = 1; r <= k.rounds; ++r) {
        uint8_t t[16];
        // SubBytes + ShiftRows
        static const int sh[16] = {0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12, 1, 6, 11};
        for (int i = 0; i < 16; ++i) t[i] = S[s[sh[i]]];
        if (r != k.rounds) {  // MixColumns
            for (int c = 0; c < 4; ++c) {
                uint8_t* col = &t[c * 4];
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
                col[1] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
                col[2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
                col[3] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
            }
        }
        for (int i = 0; i < 16; ++i) s[i] = t[i] ^ k.rk[r][i];
    }
    std::memcpy(out, s, 16);
}

// ---------------------------------------------------------------- GCM
inline void gfMul(uint8_t x[16], const uint8_t h[16]) {
    uint8_t z[16] = {0}, v[16];
    std::memcpy(v, h, 16);
    for (int i = 0; i < 128; ++i) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1) for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        bool lsb = v[15] & 1;
        for (int j = 15; j > 0; --j) v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1;
    }
    std::memcpy(x, z, 16);
}

inline void ghashUpdate(uint8_t y[16], const uint8_t h[16], const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t blk[16] = {0};
        size_t n = (len - i < 16) ? len - i : 16;
        std::memcpy(blk, data + i, n);
        for (int j = 0; j < 16; ++j) y[j] ^= blk[j];
        gfMul(y, h);
        i += n;
    }
}

inline void gcmCrypt(const AesKey& k, const uint8_t j0[16], const uint8_t* in, size_t len, uint8_t* out) {
    uint8_t ctr[16], ks[16];
    std::memcpy(ctr, j0, 16);
    for (size_t off = 0; off < len; off += 16) {
        for (int i = 15; i >= 12; --i) { if (++ctr[i] != 0) break; }   // inc32
        aesEncryptBlock(k, ctr, ks);
        size_t n = (len - off < 16) ? len - off : 16;
        for (size_t j = 0; j < n; ++j) out[off + j] = in[off + j] ^ ks[j];
    }
}

inline void gcmTag(const AesKey& k, const uint8_t j0[16], const uint8_t* aad, size_t aadLen,
                   const uint8_t* ct, size_t ctLen, uint8_t tag[16]) {
    uint8_t h[16] = {0}, y[16] = {0};
    aesEncryptBlock(k, h, h);
    ghashUpdate(y, h, aad, aadLen);
    ghashUpdate(y, h, ct, ctLen);
    uint8_t lens[16] = {0};
    uint64_t a = (uint64_t)aadLen * 8, c = (uint64_t)ctLen * 8;
    for (int i = 0; i < 8; ++i) { lens[7 - i] = (uint8_t)(a >> (8 * i)); lens[15 - i] = (uint8_t)(c >> (8 * i)); }
    for (int j = 0; j < 16; ++j) y[j] ^= lens[j];
    gfMul(y, h);
    uint8_t e[16];
    aesEncryptBlock(k, j0, e);
    for (int j = 0; j < 16; ++j) tag[j] = y[j] ^ e[j];
}

// returns ciphertext || 16-byte tag; empty optional-style flag via bool
inline bool gcmSeal(const std::string& key, const std::string& nonce, const std::string& pt,
                    const std::string& aad, std::string& out) {
    AesKey k;
    if (nonce.size() != 12 || !aesSetKey(k, (const uint8_t*)key.data(), key.size())) return false;
    uint8_t j0[16];
    std::memcpy(j0, nonce.data(), 12); j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    out.assign(pt.size() + 16, '\0');
    gcmCrypt(k, j0, (const uint8_t*)pt.data(), pt.size(), (uint8_t*)&out[0]);
    gcmTag(k, j0, (const uint8_t*)aad.data(), aad.size(), (const uint8_t*)out.data(), pt.size(), (uint8_t*)&out[pt.size()]);
    return true;
}

inline bool gcmOpen(const std::string& key, const std::string& nonce, const std::string& ctTag,
                    const std::string& aad, std::string& out) {
    AesKey k;
    if (nonce.size() != 12 || ctTag.size() < 16 || !aesSetKey(k, (const uint8_t*)key.data(), key.size())) return false;
    size_t n = ctTag.size() - 16;
    uint8_t j0[16];
    std::memcpy(j0, nonce.data(), 12); j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    uint8_t tag[16];
    gcmTag(k, j0, (const uint8_t*)aad.data(), aad.size(), (const uint8_t*)ctTag.data(), n, tag);
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= tag[i] ^ (uint8_t)ctTag[n + i];
    if (diff != 0) return false;                       // authentication failed: release nothing
    out.assign(n, '\0');
    gcmCrypt(k, j0, (const uint8_t*)ctTag.data(), n, (uint8_t*)&out[0]);
    return true;
}

// ---------------------------------------------------------------- PBKDF2-HMAC-SHA256
inline std::string pbkdf2Sha256(const std::string& password, const std::string& salt, uint32_t iterations, size_t dkLen) {
    std::string key = password;
    if (key.size() > 64) { auto d = clc::sha256(key); key.assign((const char*)d.data(), d.size()); }
    key.resize(64, '\0');
    std::string ipad(64, 0x36), opad(64, 0x5c);
    for (int i = 0; i < 64; ++i) { ipad[i] ^= key[i]; opad[i] ^= key[i]; }
    auto hmac = [&](const std::string& msg) {
        auto inner = clc::sha256(ipad + msg);
        std::string in2((const char*)inner.data(), 32);
        auto outer = clc::sha256(opad + in2);
        return std::string((const char*)outer.data(), 32);
    };
    std::string dk;
    for (uint32_t block = 1; dk.size() < dkLen; ++block) {
        std::string s = salt;
        s += (char)(block >> 24); s += (char)(block >> 16); s += (char)(block >> 8); s += (char)block;
        std::string u = hmac(s), t = u;
        for (uint32_t i = 1; i < iterations; ++i) {
            u = hmac(u);
            for (int j = 0; j < 32; ++j) t[j] ^= u[j];
        }
        dk += t;
    }
    dk.resize(dkLen);
    return dk;
}

} // namespace rincrypto
