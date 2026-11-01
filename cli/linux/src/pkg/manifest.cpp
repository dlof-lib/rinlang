// cli/linux/src/pkg/manifest.cpp
#include "manifest.h"
#include "toml_lite.h"
#include "errors.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace rinpm {

const DependencySpec* Manifest::findDependency(const std::string& depName) const {
    for (auto& d : dependencies) if (d.name == depName) return &d;
    for (auto& d : devDependencies) if (d.name == depName) return &d;
    return nullptr;
}

static bool fileExists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}

static std::vector<DependencySpec> readDeps(const toml::Table& t) {
    std::vector<DependencySpec> out;
    for (auto& [k, v] : t) {
        if (v.kind != toml::Value::Kind::String) {
            throw RinManifestError("dependency `" + k + "` must be a version constraint string, e.g. \"^1.2.0\"");
        }
        out.push_back({k, v.str});
    }
    return out;
}

Manifest loadManifestFromFile(const std::string& tomlPath) {
    if (!fileExists(tomlPath)) {
        throw RinManifestError("manifest not found: `" + tomlPath + "` (run `rin pkg init` to create one)");
    }
    std::ifstream in(tomlPath, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();

    toml::Document doc;
    try {
        doc = toml::parse(text);
    } catch (const toml::ParseError& e) {
        std::ostringstream msg;
        msg << "syntax error in " << tomlPath << ":" << e.line << ": " << e.what();
        throw RinManifestError(msg.str());
    }

    Manifest m;
    m.manifestPath = tomlPath;

    const toml::Table* pkg = doc.section("package");
    if (!pkg) {
        throw RinManifestError("missing required [package] section in " + tomlPath);
    }
    m.name = toml::getStr(*pkg, "name");
    m.version = toml::getStr(*pkg, "version");
    m.description = toml::getStr(*pkg, "description");
    m.authors = toml::getStrArray(*pkg, "authors");
    m.license = toml::getStr(*pkg, "license");
    m.repository = toml::getStr(*pkg, "repository");
    m.homepage = toml::getStr(*pkg, "homepage");
    m.keywords = toml::getStrArray(*pkg, "keywords");

    if (const toml::Table* deps = doc.section("dependencies")) {
        m.dependencies = readDeps(*deps);
    }
    if (const toml::Table* devDeps = doc.section("dev-dependencies")) {
        m.devDependencies = readDeps(*devDeps);
    }
    if (const toml::Table* rin = doc.section("package.rin")) {
        m.rinVersionConstraint = toml::getStr(*rin, "version");
    }
    if (const toml::Table* plat = doc.section("package.platforms")) {
        m.platforms = toml::getStrArray(*plat, "os");
    }

    validateManifest(m);
    return m;
}

Manifest loadManifestFromDir(const std::string& projectDir) {
    std::string path = projectDir.empty() ? "rin.toml" : projectDir + "/rin.toml";
    return loadManifestFromFile(path);
}

static bool isValidPackageName(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}

void validateManifest(const Manifest& m) {
    const std::string where = m.manifestPath.empty() ? "rin.toml" : m.manifestPath;
    if (m.name.empty()) {
        throw RinManifestError(where + ": [package].name is required");
    }
    if (!isValidPackageName(m.name)) {
        throw RinManifestError(where + ": [package].name `" + m.name +
                                "` is invalid (allowed: letters, digits, '-', '_', '.')");
    }
    if (m.version.empty()) {
        throw RinManifestError(where + ": [package].version is required");
    }
    Version v;
    if (!tryParseVersion(m.version, v)) {
        throw RinManifestError(where + ": [package].version `" + m.version +
                                "` is not valid SemVer (expected MAJOR.MINOR.PATCH, e.g. 1.0.0)");
    }
    for (auto& d : m.dependencies) {
        VersionConstraint c;
        if (!tryParseConstraint(d.constraintRaw, c)) {
            throw RinManifestError(where + ": dependency `" + d.name + "` has invalid version constraint `" +
                                    d.constraintRaw + "`");
        }
        if (!isValidPackageName(d.name)) {
            throw RinManifestError(where + ": dependency name `" + d.name + "` is invalid");
        }
    }
    for (auto& d : m.devDependencies) {
        VersionConstraint c;
        if (!tryParseConstraint(d.constraintRaw, c)) {
            throw RinManifestError(where + ": dev-dependency `" + d.name + "` has invalid version constraint `" +
                                    d.constraintRaw + "`");
        }
    }
    if (!m.rinVersionConstraint.empty()) {
        VersionConstraint c;
        if (!tryParseConstraint(m.rinVersionConstraint, c)) {
            throw RinManifestError(where + ": [package.rin].version constraint `" + m.rinVersionConstraint +
                                    "` is invalid");
        }
    }
    static const std::vector<std::string> kKnownPlatforms = {"linux", "android", "windows", "macos"};
    for (auto& p : m.platforms) {
        bool known = false;
        for (auto& kp : kKnownPlatforms) if (kp == p) { known = true; break; }
        if (!known) {
            throw RinManifestError(where + ": [package.platforms].os contains unknown platform `" + p +
                                    "` (known: linux, android, windows, macos)");
        }
    }
}

std::string renderManifest(const Manifest& m) {
    toml::Writer w;
    w.section("package");
    w.kv("name", m.name);
    w.kv("version", m.version);
    if (!m.description.empty()) w.kv("description", m.description);
    if (!m.authors.empty()) w.kvArray("authors", m.authors);
    if (!m.license.empty()) w.kv("license", m.license);
    if (!m.repository.empty()) w.kv("repository", m.repository);
    if (!m.homepage.empty()) w.kv("homepage", m.homepage);
    if (!m.keywords.empty()) w.kvArray("keywords", m.keywords);
    w.blank();

    w.section("dependencies");
    for (auto& d : m.dependencies) w.kv(d.name, d.constraintRaw);
    w.blank();

    if (!m.devDependencies.empty()) {
        w.section("dev-dependencies");
        for (auto& d : m.devDependencies) w.kv(d.name, d.constraintRaw);
        w.blank();
    }

    w.section("package.rin");
    w.kv("version", m.rinVersionConstraint.empty() ? ">=1.0.0" : m.rinVersionConstraint);
    w.blank();

    w.section("package.platforms");
    w.kvArray("os", m.platforms.empty() ? std::vector<std::string>{"linux", "android", "windows"} : m.platforms);

    return w.str();
}

void saveManifest(const Manifest& m, const std::string& tomlPath) {
    std::ofstream out(tomlPath, std::ios::binary | std::ios::trunc);
    if (!out) throw RinManifestError("could not write manifest to `" + tomlPath + "`");
    out << renderManifest(m);
}

} // namespace rinpm
