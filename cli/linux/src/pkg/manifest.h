// cli/linux/src/pkg/manifest.h
// ============================================================================
// RinPM :: Manifest — يمثّل ويُحلّل/يكتب rin.toml (قسم 4 من المواصفة).
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include "semver.h"

namespace rinpm {

struct DependencySpec {
    std::string name;
    std::string constraintRaw; // كما كُتب في rin.toml، مثال: "^1.2.0"
};

struct Manifest {
    // [package]
    std::string name;
    std::string version;       // نص خام (يُحلَّل عبر semver عند الحاجة)
    std::string description;
    std::vector<std::string> authors;
    std::string license;
    std::string repository;
    std::string homepage;
    std::vector<std::string> keywords;

    // [dependencies] / [dev-dependencies]
    std::vector<DependencySpec> dependencies;
    std::vector<DependencySpec> devDependencies;

    // [package.rin]
    std::string rinVersionConstraint; // مثال: ">=1.0.0"

    // [package.platforms]
    std::vector<std::string> platforms; // فارغ = كل المنصات

    std::string manifestPath; // المسار الذي حُمِّل منه (لأغراض رسائل الخطأ)

    const DependencySpec* findDependency(const std::string& depName) const;
};

// يحمّل ويحلل rin.toml من مسار مشروع (يبحث عن <projectDir>/rin.toml).
// يرمي RinManifestError مع رسالة واضحة ومحددة السطر عند أي خلل في البنية أو القيم.
Manifest loadManifestFromFile(const std::string& tomlPath);
Manifest loadManifestFromDir(const std::string& projectDir);

// يتحقق من صحة البيانات المنطقية للمانيفست (اسم/إصدار/قيود صالحة) بعد التحليل
// النحوي الأساسي. يرمي RinManifestError عند أول خلل يجده مع شرح دقيق.
void validateManifest(const Manifest& m);

// يكتب rin.toml من كائن Manifest (يُستخدم من `rin pkg init` و`rin pkg add/remove`).
std::string renderManifest(const Manifest& m);
void saveManifest(const Manifest& m, const std::string& tomlPath);

} // namespace rinpm
