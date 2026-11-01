// clc_format.h — التخطيط الثنائي الرسمي لصيغة CLC 1.0 (Rin Compact Library Container).
// هذا الملف هو "مصدر الحقيقة" للتخطيط؛ docs/FORMAT.md يشرحه بالنثر لكنه يجب أن
// يبقى مطابقاً لما هنا بالضبط.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "sha256.h"

namespace clc {

// "RCLF" = Rin Compact Library Format. 4 بايتات ASCII واضحة عند فتح الملف بأي hex
// editor، ومختلفة تماماً عن PK\x03\x04 (ZIP) أو أي توقيع أرشيف معروف آخر.
constexpr uint8_t CLC_MAGIC[4] = {'R','C','L','F'};
// توقيع الـ Footer في آخر الملف — يسمح بالتحقق السريع من نهاية الملف قبل قراءة
// الرأس بالكامل، ومفيد لاكتشاف truncation (ملف مقطوع/تحميل غير مكتمل).
constexpr uint8_t CLC_FOOTER_MAGIC[4] = {'R','C','L','E'}; // End

constexpr uint8_t CLC_VERSION_MAJOR = 1;
constexpr uint8_t CLC_VERSION_MINOR = 0;

// حجم الرأس الثابت بالبايت (موثّق صراحة، لا يعتمد على sizeof(struct) لأي بنية C++).
constexpr size_t CLC_HEADER_SIZE = 96;
// حجم الـ Footer الثابت بالبايت.
constexpr size_t CLC_FOOTER_SIZE = 4 + 8 + 32 + 4; // magic + header_offset + container_hash + crc32(header)

enum class CompressionMethod : uint8_t {
    STORE = 0,        // بلا ضغط (نسخ مباشر)
    DEFLATE = 1,       // zlib DEFLATE خام (RFC 1951) فقط كخوارزمية entropy coding
    RIN_DICT_DEFLATE = 2, // استبدال قاموس Rin أولاً، ثم DEFLATE على الناتج
};

enum class Level : uint8_t {
    L0 = 0, // بدون ضغط إطلاقاً
    L1 = 1, // سريع
    L2 = 2, // متوازن (افتراضي)
    L3 = 3, // قوي
    L4 = 4, // أعلى
    ULTRA = 5, // أقصى ضغط (كتلة واحدة لكل ملف نصي، لا تقسيم، أعلى مستوى zlib)
};

// Flags على مستوى الحاوية (bitmask في الرأس).
constexpr uint16_t FLAG_HAS_DEPENDENCIES = 1 << 0;
constexpr uint16_t FLAG_HAS_RIN_DICT     = 1 << 1;
constexpr uint16_t FLAG_STREAMING_SAFE   = 1 << 2; // كُتب بترتيب يسمح بالقراءة التدريجية

// Flags على مستوى كل ملف داخل File Index.
constexpr uint8_t FILE_FLAG_IS_RIN_SOURCE = 1 << 0;
constexpr uint8_t FILE_FLAG_IS_TEXT       = 1 << 1;
constexpr uint8_t FILE_FLAG_IS_DIR        = 1 << 2; // إدخال مجلد فارغ (لا بيانات)

// ---- الرأس (96 بايت، Little-Endian بالكامل) ----
// offset  size  field
// 0       4     magic "RCLF"
// 4       1     version_major
// 5       1     version_minor
// 6       2     flags (u16)
// 8       1     compression_method الافتراضي (قد يُخالَف لكل block على حدة)
// 9       1     level (0..5)
// 10      2     reserved (0)
// 12      4     file_count (u32)
// 16      4     block_count (u32)
// 20      8     metadata_offset (u64)
// 28      8     dependency_offset (u64, 0 إن لم توجد)
// 36      8     symbol_table_offset (u64, 0 إن لم توجد)
// 44      8     index_offset (u64)      -> File Index
// 52      8     block_table_offset (u64)
// 60      8     data_offset (u64)       -> بداية الـ Compressed Blocks
// 68      8     integrity_offset (u64)  -> قسم Integrity Data
// 76      8     footer_offset (u64)
// 84      4     header_crc32 (لجزء الرأس فيما عدا هذا الحقل نفسه)
// 88      8     reserved2 (0) — محجوز للتوسع المستقبلي (CLC 1.1/2.0)
struct ClcHeader {
    uint8_t  magic[4] = {'R','C','L','F'};
    uint8_t  versionMajor = CLC_VERSION_MAJOR;
    uint8_t  versionMinor = CLC_VERSION_MINOR;
    uint16_t flags = 0;
    uint8_t  compressionMethod = uint8_t(CompressionMethod::DEFLATE);
    uint8_t  level = uint8_t(Level::L2);
    uint16_t reserved = 0;
    uint32_t fileCount = 0;
    uint32_t blockCount = 0;
    uint64_t metadataOffset = 0;
    uint64_t dependencyOffset = 0;
    uint64_t symbolTableOffset = 0;
    uint64_t indexOffset = 0;
    uint64_t blockTableOffset = 0;
    uint64_t dataOffset = 0;
    uint64_t integrityOffset = 0;
    uint64_t footerOffset = 0;
    uint32_t headerCrc32 = 0;
    uint64_t reserved2 = 0;
};

struct DependencyEntry {
    std::string name;
    std::string constraint; // مثال: ">=1.2.0" أو "^2.0.0"
};

struct FileEntry {
    std::string path;         // مسار نسبي بـ '/'، مُتحقَّق منه أمنياً عند unpack
    uint64_t originalSize = 0;
    uint8_t  flags = 0;
    uint32_t firstBlock = 0;
    uint32_t blockCount = 0;
    Sha256Digest contentHash{}; // sha256 لكامل محتوى الملف الأصلي (للـ dedup والتحقق)
};

struct BlockEntry {
    uint32_t id = 0;
    uint64_t originalSize = 0;
    uint64_t compressedSize = 0;
    uint64_t offset = 0; // نسبي إلى data_offset
    uint8_t  method = uint8_t(CompressionMethod::STORE);
    uint32_t crc32OfStored = 0; // CRC32 للبايتات المخزَّنة فعلياً (سريع لـ `clc check`)
};

} // namespace clc
