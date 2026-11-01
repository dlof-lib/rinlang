// cli/linux/src/pkg/semver.h
// ============================================================================
// RinPM :: SemVer — تحليل إصدارات "MAJOR.MINOR.PATCH" (مع pre-release اختياري
// بأسلوب "-beta.1") ومطابقة قيود الإصدار المستخدمة في rin.toml:
//   1.2.3   ^1.2.3   ~1.2.3   >=1.0.0   >1.0.0   <=2.0.0   <2.0.0   1.x   1.2.x   *
// لا يعتمد على أي مكتبة خارجية.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <optional>

namespace rinpm {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string preRelease; // فارغ إن لم يوجد (مثال: "beta.1")

    std::string toString() const;
    // مقارنة SemVer صحيحة: الإصدار بلا pre-release أعلى من نفس الإصدار مع pre-release.
    int compare(const Version& other) const;
    bool operator<(const Version& o) const { return compare(o) < 0; }
    bool operator==(const Version& o) const { return compare(o) == 0; }
};

// يحلل نصاً مثل "1.2.3" أو "1.2.3-beta.1". يرمي std::invalid_argument عند الفشل.
Version parseVersion(const std::string& text);
// نسخة لا ترمي: تعيد false عند فشل التحليل.
bool tryParseVersion(const std::string& text, Version& out);

// نوع قيد إصدار واحد بعد تحليل النص من rin.toml (قد يحتوي rin.toml على عدة قيود
// مفصولة بفواصل لاحقاً؛ الشكل الحالي: قيد واحد لكل تبعية كما في طلب المواصفة).
struct VersionConstraint {
    std::string raw;              // النص الأصلي كما كُتب في rin.toml (لأغراض رسائل الخطأ)
    bool matchesAny = false;      // "*"
    // إن لم تكن matchesAny: نطاق [minInclusive, maxExclusiveOrInclusive]
    std::optional<Version> min;
    bool minInclusive = true;
    std::optional<Version> max;
    bool maxInclusive = false;

    bool matches(const Version& v) const;
    std::string toString() const;
};

// يحلل قيد إصدار واحد من صيغ rin.toml المذكورة أعلاه. يرمي std::invalid_argument عند الفشل.
VersionConstraint parseConstraint(const std::string& text);
bool tryParseConstraint(const std::string& text, VersionConstraint& out);

// من بين مجموعة إصدارات متاحة، يعيد الأعلى الذي يحقق القيد (أو nullopt إن لا شيء يطابق).
std::optional<Version> selectBestVersion(const VersionConstraint& c, const std::vector<Version>& available);

} // namespace rinpm
