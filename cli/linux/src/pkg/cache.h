// cli/linux/src/pkg/cache.h
// ============================================================================
// RinPM :: Cache — تخزين عالمي للحزم المثبَّتة، مستقل عن أي مشروع بعينه
// (قسم 3 وقسم 17 من المواصفة). لا تُخزَّن أي تبعية داخل مجلد المشروع نفسه.
//
//   Linux/macOS/Windows(المحاكاة عبر MSYS/WSL): ~/.rin/
//   Android/Termux: $RIN_HOME إن وُجد، وإلا $HOME/.rin، وإلا /data/data/.../rin
//
// البنية: ~/.rin/{cache,registry,packages,config}
//   packages/<name>/<version>/src/...      المصدر المفكوك الجاهز للاستيراد
//   packages/<name>/<version>/rin.toml     مانيفست الحزمة نفسها
//   packages/<name>/<version>/.checksum    بصمة sha256 للأرشيف الأصلي (للتحقق السريع)
//   cache/<name>-<version>.rinpkg          الأرشيف الخام كما نُزِّل (لإعادة التحقق/عدم إعادة التنزيل)
// ============================================================================
#pragma once
#include <string>
#include <optional>

namespace rinpm {

class Cache {
public:
    Cache();
    explicit Cache(std::string rootOverride);

    const std::string& root() const { return root_; }
    std::string cacheDir() const { return root_ + "/cache"; }
    std::string registryDir() const { return root_ + "/registry"; }
    std::string packagesDir() const { return root_ + "/packages"; }
    std::string configDir() const { return root_ + "/config"; }

    void ensureLayout() const;

    std::string archivePath(const std::string& name, const std::string& version) const; // في cache/
    std::string installDir(const std::string& name, const std::string& version) const;  // في packages/
    std::string checksumFile(const std::string& name, const std::string& version) const;

    // موجودة في الكاش وصالحة (checksum المخزَّن يطابق checksum متوقَّع)؟
    bool isInstalledAndValid(const std::string& name, const std::string& version,
                              const std::string& expectedChecksum) const;
    bool isArchiveCached(const std::string& name, const std::string& version,
                          const std::string& expectedChecksum) const;

    // يحذف كل شيء في cache/ (المحفوظات المضغوطة) مع إبقاء packages/ المثبَّتة فعلياً،
    // إلا إن purgeInstalled=true فيحذف كل شيء (يُستخدم من `rin pkg clean --all`).
    void clean(bool purgeInstalled) const;

private:
    std::string root_;
};

// يحسب جذر ~/.rin المناسب للنظام الحالي فعلياً (لا قيمة ثابتة/hardcoded).
std::string defaultCacheRoot();

} // namespace rinpm
