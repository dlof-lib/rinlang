// clc_compress.h — طبقة رقيقة فوق zlib للقيام بمرحلة "Entropy Compression" فقط من
// الـ pipeline. هذا استخدام لخوارزمية DEFLATE (RFC 1951) كخطوة ضغط، وليس استخدام
// صيغة ZIP كحاوية — CLC لا يكتب أي PK header ولا Central Directory ولا أي جزء من
// بنية ZIP نفسها؛ فقط تُستدعى نفس دالة الضغط bit-level التي يستخدمها zlib داخلياً.
#pragma once
#include <cstdint>
#include <vector>
#include "clc_format.h"

namespace clc {

// يحوّل مستوى CLC (0..ULTRA) إلى مستوى zlib الداخلي (0..9) + هل نمنع تقسيم الملف
// لكتل متعددة (ultra يفضّل كتلة واحدة كبيرة لضغط أفضل عبر السياق الأطول).
int levelToZlib(Level lvl);

// ضغط خام (بلا أي رأس zlib/gzip) — RFC 1951 raw deflate.
std::vector<uint8_t> deflateRaw(const uint8_t* data, size_t len, int zlibLevel);
// فك ضغط خام؛ يجب معرفة الحجم الأصلي مسبقاً (مخزَّن في Block Table) لتفادي
// "decompression bomb" عبر تخصيص غير محدود: نخصص originalSize بالضبط ثم نتحقق.
std::vector<uint8_t> inflateRaw(const uint8_t* data, size_t len, size_t originalSize);

// CRC32 قياسي (زlib) — يُستخدم للتحقق السريع من كتلة واحدة في `clc check`
// (أرخص بكثير من sha256 الكامل، والذي يُحسب فقط عند `clc verify`).
uint32_t crc32Of(const uint8_t* data, size_t len);

} // namespace clc
