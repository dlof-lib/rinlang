// cli/linux/src/pkg/resolver.h
// ============================================================================
// RinPM :: Resolver — يحل شجرة التبعيات كاملة عبر Registry واحد أو أكثر
// (قسم 6 من المواصفة). يكتشف: تبعية دائرية، تعارض إصدار، حزمة مفقودة، إصدار
// غير صالح، عدم توافق إصدار Rin، منصة غير مدعومة.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include "manifest.h"
#include "registry.h"

namespace rinpm {

struct ResolvedPackage {
    std::string name;
    Version version;
    PackageMeta meta;
    std::vector<std::string> directDependencies; // أسماء فقط (النسخ المحلولة تُقرأ من الخريطة العامة)
};

struct ResolveResult {
    std::vector<ResolvedPackage> packages; // ترتيب تثبيت آمن: التبعيات قبل من يعتمد عليها
};

class Resolver {
public:
    // registries بالترتيب: يُجرَّب كل واحد بدوره لكل حزمة (أول من يملك الاسم يُستخدم لكل إصداراته،
    // لتفادي خلط إصدارات نفس الحزمة من مصادر مختلفة في نفس الحل).
    explicit Resolver(std::vector<Registry*> registries, std::string currentRinVersion,
                       std::string currentPlatform);

    // يحل تبعيات المانيفست (dependencies + dev-dependencies إن includeDev).
    // يرمي RinDependencyError/RinVersionError/RinPackageNotFound برسائل تفصيلية جاهزة للطباعة.
    ResolveResult resolve(const Manifest& root, bool includeDev) const;

private:
    std::vector<Registry*> registries_;
    std::string currentRinVersion_;
    std::string currentPlatform_;

    Registry* registryFor(const std::string& name) const;
};

} // namespace rinpm
