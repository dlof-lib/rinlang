// cli/linux/src/pkg/package_ops.cpp
#include "package_ops.h"
#include "archive.h"
#include "sha256.h"
#include "errors.h"
#include <sys/stat.h>
#include <fstream>

namespace rinpm::package {

namespace {
bool exists(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0; }
bool isDir(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
}

namespace validator {

void validatePackageDirectory(const std::string& dir, std::vector<std::string>& warningsOut) {
    std::string manifestPath = dir + "/rin.toml";
    if (!exists(manifestPath)) {
        throw RinManifestError("cannot publish `" + dir + "`: missing rin.toml");
    }
    // يعيد استخدام نفس تحليل/تحقق rin.toml المستخدَم في بقية RinPM (مصدر حقيقة واحد).
    Manifest m = loadManifestFromDir(dir);
    (void)m;

    if (!isDir(dir + "/src")) {
        warningsOut.push_back("no `src/` directory found — the package will contain no importable Rin source files");
    }
    if (!exists(dir + "/README.md")) {
        warningsOut.push_back("no README.md found — consider adding one before publishing");
    }
    if (!exists(dir + "/LICENSE")) {
        warningsOut.push_back("no LICENSE file found — consider adding one before publishing");
    }
}

} // namespace validator

namespace installer {

void installResolvedPackage(const ResolvedPackage& pkg, Registry& reg, const Cache& cache, bool offline) {
    const std::string& name = pkg.name;
    std::string version = pkg.version.toString();
    const std::string& expectedChecksum = pkg.meta.checksum;

    if (cache.isInstalledAndValid(name, version, expectedChecksum)) {
        return; // موجودة وصالحة مسبقاً: لا إعادة تنزيل (قسم 17)
    }

    std::string archivePath = cache.archivePath(name, version);
    bool haveArchive = cache.isArchiveCached(name, version, expectedChecksum);

    if (!haveArchive) {
        if (offline) {
            throw RinPackageError("RinOfflineError",
                "package not available in offline cache: `" + name + "@" + version + "`",
                ExitCode::PackageNotFound);
        }
        if (!reg.downloadPackage(name, version, archivePath)) {
            throw RinPackageNotFound("could not download `" + name + "@" + version + "` from registry `" +
                                      reg.sourceLabel() + "`");
        }
    }

    // التحقق الأمني: لا نثق بأي حزمة قبل مطابقة بصمتها فعلياً (قسم 10).
    std::string actualChecksum;
    if (!sha256HexOfFile(archivePath, actualChecksum)) {
        throw RinChecksumError("could not read downloaded archive to verify checksum: `" + archivePath + "`");
    }
    if (!expectedChecksum.empty() && actualChecksum != expectedChecksum) {
        std::string msg = "Checksum mismatch for `" + name + "@" + version + "`.\n\n" +
                           "Expected:\n  " + expectedChecksum + "\n\n" +
                           "Received:\n  " + actualChecksum + "\n";
        // لا نُبقي أرشيفاً فاسداً في الكاش يمكن أن يُعاد استخدامه صمتاً لاحقاً.
        ::remove(archivePath.c_str());
        throw RinChecksumError(msg);
    }

    std::string installDir = cache.installDir(name, version);
    unpackArchive(archivePath, installDir);

    std::ofstream cf(cache.checksumFile(name, version), std::ios::binary | std::ios::trunc);
    cf << actualChecksum;
}

} // namespace installer

namespace builder {

std::string buildArchive(const Manifest& m, const std::string& projectDir, const std::string& destDir) {
    struct stat st{};
    if (::stat(destDir.c_str(), &st) != 0) {
        std::string cur;
        for (size_t i = 0; i < destDir.size(); ++i) {
            cur.push_back(destDir[i]);
            if (destDir[i] == '/' || i + 1 == destDir.size()) if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
        }
    }
    std::string archivePath = destDir + "/" + m.name + "-" + m.version + ".rinpkg";
    packDirectory(projectDir, archivePath);
    return archivePath;
}

} // namespace builder

namespace publisher {

PackageMeta publish(const Manifest& m, const std::string& projectDir, const std::string& buildDir, Registry& reg) {
    std::vector<std::string> warnings;
    validator::validatePackageDirectory(projectDir, warnings);

    std::string archivePath = builder::buildArchive(m, projectDir, buildDir);

    std::string checksum;
    if (!sha256HexOfFile(archivePath, checksum)) {
        throw RinChecksumError("could not compute checksum for built archive `" + archivePath + "`");
    }

    PackageMeta meta;
    meta.name = m.name;
    meta.version = m.version;
    meta.description = m.description;
    meta.authors = m.authors;
    meta.license = m.license;
    meta.repository = m.repository;
    meta.homepage = m.homepage;
    meta.keywords = m.keywords;
    for (auto& d : m.dependencies) meta.dependencyNames.push_back(d);
    meta.rinVersionConstraint = m.rinVersionConstraint;
    meta.platforms = m.platforms;
    meta.checksum = checksum;

    reg.publishPackage(meta, archivePath);
    return meta;
}

} // namespace publisher

} // namespace rinpm::package
