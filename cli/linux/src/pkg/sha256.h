// cli/linux/src/pkg/sha256.h
// ============================================================================
// RinPM :: Security layer — تنفيذ SHA-256 مستقل (بلا اعتماديات خارجية) لحساب
// وبصمات (checksums) الحزم عند التنزيل والتثبيت والنشر. هذا تنفيذ حتمي قياسي
// لخوارزمية SHA-256 (FIPS 180-4) يعمل على أي بايتات (نص أو ثنائي).
// ============================================================================
#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace rinpm {

// يحسب SHA-256 لسلسلة بايتات ويعيدها كنص hex بحروف صغيرة (64 حرفاً).
std::string sha256Hex(const std::string& data);

// نفس الحساب لكن من محتوى ملف على القرص. يعيد false إن تعذّر فتح الملف.
bool sha256HexOfFile(const std::string& path, std::string& outHex);

} // namespace rinpm
