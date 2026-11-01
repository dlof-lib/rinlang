// sha256.h — تنفيذ SHA-256 مستقل بلا أي اعتمادية خارجية (Public Domain style).
// يُستخدَم في CLC لتكامل الملفات/الـ blocks/الحاوية بالكامل (integrity + dedup hashing).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

namespace clc {

using Sha256Digest = std::array<uint8_t, 32>;

// يحسب SHA-256 لبيانات خام ويعيد 32 بايت.
Sha256Digest sha256(const uint8_t* data, size_t len);
Sha256Digest sha256(const std::string& data);
Sha256Digest sha256(const std::vector<uint8_t>& data);

// تحويل digest إلى نص hex بطول 64 حرفاً (لعرضه في clc info/list/verify).
std::string sha256_hex(const Sha256Digest& d);

// حاسبة تدريجية (streaming) — لملفات كبيرة/chunking بلا تحميل كل شيء في الذاكرة دفعة واحدة.
class Sha256Stream {
public:
    Sha256Stream();
    void update(const uint8_t* data, size_t len);
    Sha256Digest finish();
private:
    uint32_t state_[8];
    uint64_t bitlen_;
    uint8_t buffer_[64];
    size_t buflen_;
    void transform(const uint8_t* chunk);
};

} // namespace clc
