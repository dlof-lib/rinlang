// cli/linux/src/pkg/cache.cpp
#include "cache.h"
#include "sha256.h"
#include <cstdlib>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <string>

namespace rinpm {

namespace {
bool exists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}
void mkdirP(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' || i + 1 == path.size()) {
            if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
        }
    }
}
void rmrf(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = ::opendir(path.c_str());
        if (d) {
            struct dirent* ent;
            while ((ent = ::readdir(d)) != nullptr) {
                std::string name = ent->d_name;
                if (name == "." || name == "..") continue;
                rmrf(path + "/" + name);
            }
            ::closedir(d);
        }
        ::rmdir(path.c_str());
    } else {
        ::remove(path.c_str());
    }
}
} // namespace

std::string defaultCacheRoot() {
    // 1) متغيّر بيئة صريح (يسمح بتخصيص الموقع على أي منصة، بما فيها Termux/Android
    //    حيث لا يوجد $HOME كتابي دائماً بنفس المعنى).
    if (const char* explicitHome = std::getenv("RIN_HOME")) {
        if (*explicitHome) return std::string(explicitHome);
    }
    // 2) Android/Termux: تخزين خاص بالتطبيق قابل للكتابة دوماً.
    if (const char* androidData = std::getenv("ANDROID_DATA")) {
        (void)androidData;
        if (const char* home = std::getenv("HOME")) {
            return std::string(home) + "/.rin"; // على Termux $HOME كتابي فعلاً (/data/data/com.termux/files/home)
        }
        return "/data/local/tmp/.rin";
    }
    // 3) لينكس/ماك: ~/.rin القياسي.
    if (const char* home = std::getenv("HOME")) {
        return std::string(home) + "/.rin";
    }
    // 4) ويندوز (لو بُني هذا الكود بمترجم يدعم getenv فقط، مثال MSYS/WSL): %USERPROFILE%\.rin
    if (const char* profile = std::getenv("USERPROFILE")) {
        return std::string(profile) + "/.rin";
    }
    // 5) fallback أخير غير مستحسن لكنه يمنع الانهيار في بيئة بلا أي متغيّر بيئي معروف.
    return "./.rin";
}

Cache::Cache() : root_(defaultCacheRoot()) {}
Cache::Cache(std::string rootOverride) : root_(std::move(rootOverride)) {}

void Cache::ensureLayout() const {
    mkdirP(cacheDir());
    mkdirP(registryDir());
    mkdirP(packagesDir());
    mkdirP(configDir());
}

std::string Cache::archivePath(const std::string& name, const std::string& version) const {
    return cacheDir() + "/" + name + "-" + version + ".rinpkg";
}
std::string Cache::installDir(const std::string& name, const std::string& version) const {
    return packagesDir() + "/" + name + "/" + version;
}
std::string Cache::checksumFile(const std::string& name, const std::string& version) const {
    return installDir(name, version) + "/.checksum";
}

bool Cache::isArchiveCached(const std::string& name, const std::string& version,
                             const std::string& expectedChecksum) const {
    std::string ap = archivePath(name, version);
    if (!exists(ap)) return false;
    std::string hex;
    if (!sha256HexOfFile(ap, hex)) return false;
    return hex == expectedChecksum;
}

bool Cache::isInstalledAndValid(const std::string& name, const std::string& version,
                                 const std::string& expectedChecksum) const {
    std::string cf = checksumFile(name, version);
    std::ifstream in(cf);
    if (!in) return false;
    std::string stored;
    std::getline(in, stored);
    std::string srcDir = installDir(name, version) + "/src";
    return exists(srcDir) && stored == expectedChecksum;
}

void Cache::clean(bool purgeInstalled) const {
    // يحذف الأرشيفات المضغوطة غير المستخدمة دائماً.
    DIR* d = ::opendir(cacheDir().c_str());
    if (d) {
        struct dirent* ent;
        while ((ent = ::readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            rmrf(cacheDir() + "/" + name);
        }
        ::closedir(d);
    }
    if (purgeInstalled) {
        rmrf(packagesDir());
        mkdirP(packagesDir());
    }
}

} // namespace rinpm
