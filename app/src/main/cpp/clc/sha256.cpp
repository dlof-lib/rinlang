// sha256.cpp — تنفيذ خوارزمية SHA-256 القياسية (FIPS 180-4)، مكتوب من الصفر
// اعتماداً على المواصفة الرسمية فقط، بلا نسخ من أي مكتبة خارجية.
#include "sha256.h"
#include <cstring>

namespace clc {

namespace {
constexpr uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
} // namespace

Sha256Stream::Sha256Stream() : bitlen_(0), buflen_(0) {
    state_[0]=0x6a09e667; state_[1]=0xbb67ae85; state_[2]=0x3c6ef372; state_[3]=0xa54ff53a;
    state_[4]=0x510e527f; state_[5]=0x9b05688c; state_[6]=0x1f83d9ab; state_[7]=0x5be0cd19;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256Stream::transform(const uint8_t* chunk) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(chunk[i*4]) << 24) | (uint32_t(chunk[i*4+1]) << 16) |
               (uint32_t(chunk[i*4+2]) << 8) | uint32_t(chunk[i*4+3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

void Sha256Stream::update(const uint8_t* data, size_t len) {
    bitlen_ += uint64_t(len) * 8;
    size_t i = 0;
    if (buflen_ > 0) {
        size_t need = 64 - buflen_;
        size_t take = need < len ? need : len;
        std::memcpy(buffer_ + buflen_, data, take);
        buflen_ += take;
        i += take;
        if (buflen_ == 64) { transform(buffer_); buflen_ = 0; }
    }
    for (; i + 64 <= len; i += 64) transform(data + i);
    if (i < len) {
        std::memcpy(buffer_, data + i, len - i);
        buflen_ = len - i;
    }
}

Sha256Digest Sha256Stream::finish() {
    uint8_t pad[72];
    size_t padlen = 0;
    pad[padlen++] = 0x80;
    size_t rem = (buflen_ + 1) % 64;
    size_t zeros = (rem <= 56) ? (56 - rem) : (120 - rem);
    for (size_t i = 0; i < zeros; ++i) pad[padlen++] = 0x00;
    for (int i = 7; i >= 0; --i) pad[padlen++] = uint8_t((bitlen_ >> (i*8)) & 0xff);
    update(pad, padlen);
    // ملاحظة: update() أعلاه ستستدعي transform() على أي كتل كاملة تراكمت، وقد يتبقى صفر بالضبط
    // لأن الحشو صُمم ليجعل الطول كاملاً على حدود 64 بايت.
    Sha256Digest out;
    for (int i = 0; i < 8; ++i) {
        out[i*4]   = uint8_t((state_[i] >> 24) & 0xff);
        out[i*4+1] = uint8_t((state_[i] >> 16) & 0xff);
        out[i*4+2] = uint8_t((state_[i] >> 8) & 0xff);
        out[i*4+3] = uint8_t(state_[i] & 0xff);
    }
    return out;
}

Sha256Digest sha256(const uint8_t* data, size_t len) {
    Sha256Stream s;
    s.update(data, len);
    return s.finish();
}
Sha256Digest sha256(const std::string& data) {
    return sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}
Sha256Digest sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

std::string sha256_hex(const Sha256Digest& d) {
    static const char* hex = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[i*2] = hex[(d[i] >> 4) & 0xf];
        out[i*2+1] = hex[d[i] & 0xf];
    }
    return out;
}

} // namespace clc
