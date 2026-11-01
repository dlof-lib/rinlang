// clc_security.h — كل ما يتعلق بالأمان عند فك الضغط (unpack)، حسب القسم 12 من
// المتطلبات: منع Path Traversal, Absolute Paths, Decompression Bombs, إلخ.
#pragma once
#include <string>
#include <cstdint>

namespace clc {

// يتحقق من مسار ملف داخل الحاوية قبل أي كتابة على القرص. يرفض:
//  - المسارات المطلقة (تبدأ بـ '/' أو تحتوي على "X:\" على ويندوز)
//  - أي عنصر ".." (Path Traversal)
//  - المسارات الفارغة أو التي تحتوي بايت NUL
// يعيد المسار المُطبَّع (منظّف من "./" الزائدة) عند القبول، ويرمي ClcFormatError عند الرفض.
std::string sanitizeEntryPath(const std::string& rawPath);

// يبني المسار الكامل تحت outDir ويتحقق مرة أخرى (defense in depth) أن الناتج
// النهائي يقع فعلياً تحت outDir بعد التطبيع — يحمي حتى لو تسلل شيء غريب من sanitizeEntryPath.
std::string resolveUnderRoot(const std::string& outDir, const std::string& sanitizedRelPath);

// حد أقصى معقول لحجم ملف واحد غير مضغوط ما لم يُصرَّح خلاف ذلك صراحة (--allow-huge)،
// لمنع "Huge File Allocation" من رأس ملف .rcl مزوَّر عمداً. افتراضي: 8 GiB.
constexpr uint64_t DEFAULT_MAX_FILE_SIZE = 8ull * 1024 * 1024 * 1024;
// حد أقصى معقول لعدد الملفات/الكتل المعلَن عنه في الرأس، للحماية من تخصيص جداول
// ضخمة وهمية قبل حتى التحقق من محتواها (Malformed Header / Memory Bomb).
constexpr uint32_t DEFAULT_MAX_ENTRIES = 5'000'000;

} // namespace clc
