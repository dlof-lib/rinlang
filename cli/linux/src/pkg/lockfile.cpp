// cli/linux/src/pkg/lockfile.cpp
#include "lockfile.h"
#include "toml_lite.h"
#include "errors.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace rinpm {

static std::string lockPath(const std::string& projectDir) {
    return projectDir.empty() ? "rin.lock" : projectDir + "/rin.lock";
}

bool lockfileExists(const std::string& projectDir) {
    struct stat st{};
    return ::stat(lockPath(projectDir).c_str(), &st) == 0;
}

Lockfile loadLockfile(const std::string& projectDir) {
    std::string path = lockPath(projectDir);
    std::ifstream in(path, std::ios::binary);
    if (!in) throw RinPackageError("RinLockfileError", "could not open lockfile `" + path + "`", ExitCode::GeneralError);
    std::ostringstream buf;
    buf << in.rdbuf();

    toml::Document doc;
    try {
        doc = toml::parse(buf.str());
    } catch (const toml::ParseError& e) {
        std::ostringstream msg;
        msg << "syntax error in " << path << ":" << e.line << ": " << e.what();
        throw RinPackageError("RinLockfileError", msg.str(), ExitCode::GeneralError);
    }

    Lockfile lock;
    if (const toml::Table* meta = doc.section("metadata")) {
        std::string v = toml::getStr(*meta, "lockfile_version", "1");
        lock.version = std::atoi(v.c_str());
    }
    auto it = doc.arrayTables.find("package");
    if (it != doc.arrayTables.end()) {
        for (const toml::Table& row : it->second) {
            LockedPackage p;
            p.name = toml::getStr(row, "name");
            p.version = toml::getStr(row, "version");
            p.source = toml::getStr(row, "source");
            p.checksum = toml::getStr(row, "checksum");
            p.dependencies = toml::getStrArray(row, "dependencies");
            lock.packages.push_back(std::move(p));
        }
    }
    return lock;
}

void saveLockfile(const Lockfile& lock, const std::string& projectDir) {
    toml::Writer w;
    w.section("metadata");
    w.kvRaw("lockfile_version", std::to_string(lock.version));
    w.blank();
    for (auto& p : lock.packages) {
        w.beginArrayTable("package");
        w.kv("name", p.name);
        w.kv("version", p.version);
        w.kv("source", p.source);
        w.kv("checksum", p.checksum);
        w.kvArray("dependencies", p.dependencies);
        w.blank();
    }
    std::string path = lockPath(projectDir);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw RinPackageError("RinLockfileError", "could not write lockfile `" + path + "`", ExitCode::GeneralError);
    out << w.str();
}

} // namespace rinpm
