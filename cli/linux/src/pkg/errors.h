// cli/linux/src/pkg/errors.h
// ============================================================================
// RinPM :: هرم الأخطاء الموحّد. كل خطأ يحمل exit code خاصاً به (قسم 11 من
// المواصفة) بحيث تستطيع طبقة CLI إرجاع رمز خروج صحيح دون تخمين، ورسالة نصية
// جاهزة للعرض للمستخدم (بدون استخدام std::cerr مباشرة من الطبقات الداخلية).
// ============================================================================
#pragma once
#include <stdexcept>
#include <string>

namespace rinpm {

enum class ExitCode : int {
    Success            = 0,
    GeneralError       = 1,
    InvalidManifest    = 2,
    DependencyConflict = 3,
    PackageNotFound    = 4,
    SecurityError      = 5,
};

// أساس كل أخطاء RinPM. لا تُستخدم مباشرة؛ استخدم الأنواع المشتقة أدناه.
class RinPackageError : public std::runtime_error {
public:
    RinPackageError(std::string kind, std::string msg, ExitCode code)
        : std::runtime_error(msg), kind_(std::move(kind)), code_(code) {}

    const std::string& kind() const { return kind_; }
    ExitCode exitCode() const { return code_; }

private:
    std::string kind_;
    ExitCode code_;
};

#define RINPM_DEFINE_ERROR(NameX, KindStr, DefaultExit)                              \
    class NameX : public RinPackageError {                                           \
    public:                                                                          \
        explicit NameX(const std::string& msg, ExitCode code = DefaultExit)          \
            : RinPackageError(KindStr, msg, code) {}                                 \
    };

RINPM_DEFINE_ERROR(RinManifestError,    "RinManifestError",    ExitCode::InvalidManifest)
RINPM_DEFINE_ERROR(RinDependencyError,  "RinDependencyError",  ExitCode::DependencyConflict)
RINPM_DEFINE_ERROR(RinRegistryError,    "RinRegistryError",    ExitCode::GeneralError)
RINPM_DEFINE_ERROR(RinChecksumError,    "RinChecksumError",    ExitCode::SecurityError)
RINPM_DEFINE_ERROR(RinAuthError,        "RinAuthError",        ExitCode::GeneralError)
RINPM_DEFINE_ERROR(RinVersionError,     "RinVersionError",     ExitCode::InvalidManifest)
RINPM_DEFINE_ERROR(RinPackageNotFound,  "RinPackageNotFound",  ExitCode::PackageNotFound)

#undef RINPM_DEFINE_ERROR

} // namespace rinpm
