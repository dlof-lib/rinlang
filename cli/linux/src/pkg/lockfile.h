// cli/linux/src/pkg/lockfile.h
// ============================================================================
// RinPM :: rin.lock — يضمن نفس الإصدارات المحلولة بالضبط بين كل من يشغّل
// `rin pkg install` على نفس المشروع (قسم 7 من المواصفة).
// ============================================================================
#pragma once
#include <string>
#include <vector>

namespace rinpm {

struct LockedPackage {
    std::string name;
    std::string version;                    // إصدار محلول بدقة، مثال: "1.2.4"
    std::string source;                      // "registry:local" أو "registry:<url>" ...
    std::string checksum;                    // sha256 hex لأرشيف الحزمة
    std::vector<std::string> dependencies;   // أسماء التبعيات المباشرة لهذه الحزمة
};

struct Lockfile {
    int version = 1; // صيغة الملف نفسه (لأغراض التوافق المستقبلي)
    std::vector<LockedPackage> packages;

    const LockedPackage* find(const std::string& name) const {
        for (auto& p : packages) if (p.name == name) return &p;
        return nullptr;
    }
};

bool lockfileExists(const std::string& projectDir);
Lockfile loadLockfile(const std::string& projectDir);
void saveLockfile(const Lockfile& lock, const std::string& projectDir);

} // namespace rinpm
