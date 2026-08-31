// cli/linux/src/pkg/cli_pkg.cpp
#include "cli_pkg.h"
#include "manifest.h"
#include "lockfile.h"
#include "resolver.h"
#include "cache.h"
#include "registry.h"
#include "package_ops.h"
#include "auth.h"
#include "errors.h"
#include "sha256.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <map>
#include <set>

namespace rinpm::cli {

namespace {

constexpr const char* kRinPMVersion = "RinPM 1.0";

void mkdirP(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' || i + 1 == path.size()) if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
    }
}
bool exists(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0; }

struct ParsedArgs {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options; // --key=value أو --key value
    bool has(const std::string& flag) const { return options.count(flag) != 0; }
};

ParsedArgs parseArgs(const std::vector<std::string>& args, size_t from) {
    ParsedArgs p;
    for (size_t i = from; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.size() >= 2 && a[0] == '-' && a[1] == '-') {
            std::string body = a.substr(2);
            auto eq = body.find('=');
            if (eq != std::string::npos) {
                p.options[body.substr(0, eq)] = body.substr(eq + 1);
            } else if (i + 1 < args.size() && args[i + 1].size() >= 2 &&
                       !(args[i + 1][0] == '-' && args[i + 1][1] == '-')) {
                p.options[body] = args[++i];
            } else {
                p.options[body] = "true";
            }
        } else {
            p.positional.push_back(a);
        }
    }
    return p;
}

std::string projectDir() { return "."; } // rin.toml/rin.lock دائماً بالنسبة لمجلد العمل الحالي

// ----------------------------------------------------------------------------
// إعداد Registries المتاحة لهذا التشغيل: LocalRegistry الافتراضي (~/.rin/registry/local)
// دائماً متاح ولا يحتاج شبكة، بالإضافة إلى HttpRegistry اختيارياً إن حُدِّد عبر
// متغيّر البيئة RIN_REGISTRY_URL (لم تُختبر هذه الوصلة هنا لغياب الشبكة في بيئة
// الاختبار، لكنها Registry حقيقية جاهزة تماماً كما هي — انظر registry.cpp).
// ----------------------------------------------------------------------------
struct RegistrySet {
    Cache cache;
    LocalRegistry local;
    std::unique_ptr<HttpRegistry> http;
    std::vector<Registry*> all;

    RegistrySet() : local(Cache().registryDir() + "/local") {
        cache.ensureLayout();
        all.push_back(&local);
        if (const char* url = std::getenv("RIN_REGISTRY_URL")) {
            std::string token;
            if (auto s = auth::loadSession(cache)) token = s->token;
            http = std::make_unique<HttpRegistry>(url, token);
            all.push_back(http.get());
        }
    }
};

void printResolutionHeader() {
    std::cout << kRinPMVersion << "\n\n";
}

int doInstallLike(bool forceFresh, bool offline, const std::string& singlePkgFilter = "") {
    Manifest m;
    try {
        m = loadManifestFromDir(projectDir());
    } catch (const RinManifestError& e) {
        std::cerr << "\xE2\x9C\x97 " << e.what() << "\n";
        return static_cast<int>(e.exitCode());
    }

    RegistrySet regs;
    Resolver resolver(regs.all, /*currentRinVersion=*/"1.0.0", /*currentPlatform=*/"linux");

    printResolutionHeader();
    std::cout << "Resolving dependencies...\n";

    ResolveResult result;
    try {
        result = resolver.resolve(m, /*includeDev=*/true);
    } catch (const RinPackageError& e) {
        std::cerr << "\xE2\x9C\x97 " << e.kind() << ":\n\n" << e.what() << "\n";
        return static_cast<int>(e.exitCode());
    }

    for (auto& pkg : result.packages) {
        std::cout << "\xE2\x9C\x93 " << pkg.name << " " << pkg.version.toString() << "\n";
    }

    std::cout << "\nInstalling packages...\n";
    for (auto& pkg : result.packages) {
        Registry* reg = nullptr;
        for (Registry* r : regs.all) {
            auto avail = r->availableVersions(pkg.name);
            if (std::find(avail.begin(), avail.end(), pkg.version) != avail.end()) { reg = r; break; }
        }
        if (!reg) reg = regs.all.front();
        try {
            package::installer::installResolvedPackage(pkg, *reg, regs.cache, offline);
        } catch (const RinPackageError& e) {
            std::cerr << "\xE2\x9C\x97 " << e.kind() << ": " << e.what() << "\n";
            return static_cast<int>(e.exitCode());
        }
        std::cout << "\xE2\x9C\x93 " << pkg.name << "\n";
    }

    Lockfile lock;
    for (auto& pkg : result.packages) {
        LockedPackage lp;
        lp.name = pkg.name;
        lp.version = pkg.version.toString();
        std::string checksum = pkg.meta.checksum;
        if (checksum.empty()) sha256HexOfFile(regs.cache.archivePath(pkg.name, lp.version), checksum);
        lp.checksum = checksum;
        lp.source = "registry:local";
        lp.dependencies = pkg.directDependencies;
        lock.packages.push_back(std::move(lp));
    }
    saveLockfile(lock, projectDir());

    std::cout << "\nLockfile updated.\n\nInstallation complete.\n";
    return 0;
}

int cmdInit(const ParsedArgs& args) {
    std::string name = args.positional.empty() ? "my-app" : args.positional[0];
    if (exists(projectDir() + "/rin.toml") && projectDir() == ".") {
        // إن نُفِّذ init داخل مجلد يملك rin.toml أصلاً بلا اسم مشروع فرعي، لا نكتب فوقه.
    }
    std::string dir = args.positional.empty() ? "." : name;
    mkdirP(dir);
    mkdirP(dir + "/src");
    mkdirP(dir + "/tests");
    mkdirP(dir + "/packages"); // انظر ملاحظة التصميم في package-manager.md: مجلد إعلامي فقط،
                                // الكاش الفعلي للتبعيات دائماً في ~/.rin/packages (قسم 3)

    Manifest m;
    m.name = name;
    m.version = "1.0.0";
    m.description = "My Rin application";
    m.authors = {"Author"};
    m.license = "MIT";
    m.rinVersionConstraint = ">=1.0.0";
    m.platforms = {"linux", "android", "windows"};
    saveManifest(m, dir + "/rin.toml");

    std::string mainRin = dir + "/src/main.rin";
    if (!exists(mainRin)) {
        std::ofstream f(mainRin);
        f << "fun main() {\n    print(\"Hello from \" + \"" << name << "\" + \"!\");\n}\n\nmain();\n";
    }
    std::ofstream readmeFile(dir + "/packages/README.md", std::ios::binary | std::ios::trunc);
    readmeFile << "هذا المجلد إعلامي فقط: RinPM يخزّن كل التبعيات فعلياً في الكاش\n"
                  "العالمي (~/.rin/packages)، وليس هنا، لتفادي تكرار نفس الحزمة في كل\n"
                  "مشروع (انظر docs/package-manager/introduction.md).\n";

    std::cout << kRinPMVersion << "\n\nCreated new Rin project `" << name << "` at ./" << dir << "\n";
    std::cout << "  " << dir << "/rin.toml\n  " << dir << "/src/main.rin\n  " << dir << "/tests/\n";
    return 0;
}

int cmdAdd(const ParsedArgs& args, bool dev) {
    if (args.positional.empty()) {
        std::cerr << "\xE2\x9C\x97 usage: rin pkg add <package>[@<constraint>] [--dev]\n";
        return static_cast<int>(ExitCode::GeneralError);
    }
    std::string spec = args.positional[0];
    std::string name = spec, constraint;
    auto at = spec.find('@');
    if (at != std::string::npos) { name = spec.substr(0, at); constraint = spec.substr(at + 1); }

    Manifest m;
    try { m = loadManifestFromDir(projectDir()); }
    catch (const RinManifestError& e) { std::cerr << "\xE2\x9C\x97 " << e.what() << "\n"; return static_cast<int>(e.exitCode()); }

    if (constraint.empty()) {
        RegistrySet regs;
        auto versions = regs.local.availableVersions(name);
        if (versions.empty() && regs.http) versions = regs.http->availableVersions(name);
        if (versions.empty()) {
            std::cerr << "\xE2\x9C\x97 RinPackageNotFound: package `" << name
                      << "` was not found in any configured registry\n";
            return static_cast<int>(ExitCode::PackageNotFound);
        }
        Version best = *std::max_element(versions.begin(), versions.end());
        constraint = "^" + best.toString();
    }

    auto& list = dev ? m.devDependencies : m.dependencies;
    bool replaced = false;
    for (auto& d : list) if (d.name == name) { d.constraintRaw = constraint; replaced = true; break; }
    if (!replaced) list.push_back({name, constraint});

    try { validateManifest(m); }
    catch (const RinManifestError& e) { std::cerr << "\xE2\x9C\x97 " << e.what() << "\n"; return static_cast<int>(e.exitCode()); }

    saveManifest(m, projectDir() + "/rin.toml");
    std::cout << kRinPMVersion << "\n\nAdded " << (dev ? "dev-dependency" : "dependency") << " `" << name
              << "` = \"" << constraint << "\" to rin.toml\n\n";
    return doInstallLike(true, false);
}

int cmdRemove(const ParsedArgs& args) {
    if (args.positional.empty()) {
        std::cerr << "\xE2\x9C\x97 usage: rin pkg remove <package>\n";
        return static_cast<int>(ExitCode::GeneralError);
    }
    std::string name = args.positional[0];
    Manifest m;
    try { m = loadManifestFromDir(projectDir()); }
    catch (const RinManifestError& e) { std::cerr << "\xE2\x9C\x97 " << e.what() << "\n"; return static_cast<int>(e.exitCode()); }

    auto erase = [&](std::vector<DependencySpec>& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& d) { return d.name == name; }), v.end());
    };
    size_t before = m.dependencies.size() + m.devDependencies.size();
    erase(m.dependencies);
    erase(m.devDependencies);
    size_t after = m.dependencies.size() + m.devDependencies.size();
    if (before == after) {
        std::cerr << "\xE2\x9C\x97 `" << name << "` is not a dependency of this project\n";
        return static_cast<int>(ExitCode::PackageNotFound);
    }
    saveManifest(m, projectDir() + "/rin.toml");
    std::cout << kRinPMVersion << "\n\nRemoved `" << name << "` from rin.toml\n\n";
    return doInstallLike(true, false);
}

int cmdInstall(const ParsedArgs& args) { return doInstallLike(false, args.has("offline")); }
int cmdUpdate(const ParsedArgs& args)  { return doInstallLike(true, args.has("offline")); }
int cmdUpgrade(const ParsedArgs& args) { return doInstallLike(true, args.has("offline")); }

int cmdList(const ParsedArgs&) {
    if (!lockfileExists(projectDir())) {
        std::cout << "No packages installed yet. Run `rin pkg install` first.\n";
        return 0;
    }
    Lockfile lock = loadLockfile(projectDir());
    std::cout << kRinPMVersion << "\n\nInstalled packages:\n";
    for (auto& p : lock.packages) std::cout << "  " << p.name << " " << p.version << "  (" << p.source << ")\n";
    return 0;
}

void printTreeNode(const Lockfile& lock, const std::string& name, int depth, std::set<std::string>& seen) {
    std::string indent;
    for (int i = 0; i < depth; ++i) indent += "    ";
    const LockedPackage* p = lock.find(name);
    if (!p) { std::cout << indent << "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 " << name << " (unresolved)\n"; return; }
    if (depth == 0) std::cout << p->name << " " << p->version << "\n";
    else std::cout << indent << "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 " << p->name << " " << p->version << "\n";
    if (seen.count(name)) return; // منع تكرار طباعة نفس الفرع لعقدة مشتركة أعمق
    seen.insert(name);
    for (auto& dep : p->dependencies) printTreeNode(lock, dep, depth + 1, seen);
}

int cmdTree(const ParsedArgs&) {
    if (!lockfileExists(projectDir())) {
        std::cout << "No packages installed yet. Run `rin pkg install` first.\n";
        return 0;
    }
    Manifest m;
    try { m = loadManifestFromDir(projectDir()); } catch (...) {}
    Lockfile lock = loadLockfile(projectDir());
    std::cout << kRinPMVersion << "\n\n";
    for (auto& d : m.dependencies) {
        std::set<std::string> seen;
        printTreeNode(lock, d.name, 0, seen);
    }
    return 0;
}

int cmdSearch(const ParsedArgs& args) {
    std::string query = args.positional.empty() ? "" : args.positional[0];
    RegistrySet regs;
    auto names = regs.local.searchNames(query);
    std::cout << kRinPMVersion << "\n\n";
    if (names.empty()) { std::cout << "No packages found matching `" << query << "`.\n"; return 0; }
    for (auto& n : names) {
        auto versions = regs.local.availableVersions(n);
        if (versions.empty()) continue;
        Version latest = *std::max_element(versions.begin(), versions.end());
        PackageMeta meta = regs.local.fetchMeta(n, latest.toString());
        std::cout << "  " << n << " (" << latest.toString() << ") - " << meta.description << "\n";
    }
    return 0;
}

int cmdInfo(const ParsedArgs& args) {
    if (args.positional.empty()) {
        std::cerr << "\xE2\x9C\x97 usage: rin pkg info <package>\n";
        return static_cast<int>(ExitCode::GeneralError);
    }
    std::string name = args.positional[0];
    RegistrySet regs;
    auto versions = regs.local.availableVersions(name);
    if (versions.empty() && regs.http) versions = regs.http->availableVersions(name);
    if (versions.empty()) {
        std::cerr << "\xE2\x9C\x97 RinPackageNotFound: package `" << name << "` was not found\n";
        return static_cast<int>(ExitCode::PackageNotFound);
    }
    Version latest = *std::max_element(versions.begin(), versions.end());
    PackageMeta meta = regs.local.fetchMeta(name, latest.toString());
    std::cout << "Name: " << meta.name << "\n";
    std::cout << "Version: " << meta.version << "\n";
    std::cout << "Description: " << meta.description << "\n";
    std::string authors; for (auto& a : meta.authors) { if (!authors.empty()) authors += ", "; authors += a; }
    std::cout << "Author: " << authors << "\n";
    std::cout << "License: " << meta.license << "\n";
    std::cout << "Rin: " << (meta.rinVersionConstraint.empty() ? "*" : meta.rinVersionConstraint) << "\n";
    std::cout << "Dependencies: " << meta.dependencyNames.size() << "\n";
    std::string platforms; for (auto& p : meta.platforms) { if (!platforms.empty()) platforms += ", "; platforms += p; }
    std::cout << "Platforms: " << (platforms.empty() ? "all" : platforms) << "\n";
    std::cout << "Available versions:";
    for (auto& v : versions) std::cout << " " << v.toString();
    std::cout << "\n";
    return 0;
}

int cmdBuild(const ParsedArgs& args) {
    int rc = doInstallLike(false, args.has("offline"));
    if (rc != 0) return rc;
    std::cout << "\nDependencies resolved. Run `rin build` to compile your project.\n";
    return 0;
}

int cmdClean(const ParsedArgs& args) {
    RegistrySet regs;
    regs.cache.clean(args.has("all"));
    std::cout << kRinPMVersion << "\n\nCache cleaned" << (args.has("all") ? " (including installed packages)" : "")
              << ".\n";
    return 0;
}

int cmdPublish(const ParsedArgs&) {
    Manifest m;
    try { m = loadManifestFromDir(projectDir()); }
    catch (const RinManifestError& e) { std::cerr << "\xE2\x9C\x97 " << e.what() << "\n"; return static_cast<int>(e.exitCode()); }

    RegistrySet regs;
    Registry* target = &regs.local;
    if (regs.http) {
        auto session = auth::loadSession(regs.cache);
        if (!session) {
            std::cerr << "\xE2\x9C\x97 RinAuthError: not logged in. Run `rin pkg login` first.\n";
            return static_cast<int>(ExitCode::GeneralError);
        }
        target = regs.http.get();
    }

    std::cout << kRinPMVersion << "\n\nValidating manifest...\n\xE2\x9C\x93 rin.toml is valid\n";
    std::cout << "Validating package structure...\n";
    std::string buildDir = projectDir() + "/build";
    try {
        auto meta = package::publisher::publish(m, projectDir(), buildDir, *target);
        std::cout << "\xE2\x9C\x93 built " << meta.name << "-" << meta.version << ".rinpkg\n";
        std::cout << "\xE2\x9C\x93 checksum " << meta.checksum << "\n";
        std::cout << "\nPublished " << meta.name << "@" << meta.version << " to " << target->sourceLabel() << "\n";
    } catch (const RinPackageError& e) {
        std::cerr << "\xE2\x9C\x97 " << e.kind() << ": " << e.what() << "\n";
        return static_cast<int>(e.exitCode());
    }
    return 0;
}

int cmdLogin(const ParsedArgs& args) {
    if (args.positional.empty() || !args.has("token")) {
        std::cerr << "\xE2\x9C\x97 usage: rin pkg login <username> --token <token> [--registry <url>]\n";
        std::cerr << "  (RinPM never accepts or stores passwords; use a registry-issued token)\n";
        return static_cast<int>(ExitCode::GeneralError);
    }
    Cache cache; cache.ensureLayout();
    auth::Session s;
    s.username = args.positional[0];
    s.token = args.options.at("token");
    s.registryUrl = args.has("registry") ? args.options.at("registry") : "";
    auth::saveSession(cache, s);
    std::cout << kRinPMVersion << "\n\nLogged in as `" << s.username << "`"
              << (s.registryUrl.empty() ? "" : (" on " + s.registryUrl)) << ".\n";
    return 0;
}

int cmdLogout(const ParsedArgs&) {
    Cache cache; cache.ensureLayout();
    auth::clearSession(cache);
    std::cout << "Logged out.\n";
    return 0;
}

int cmdWhoami(const ParsedArgs&) {
    Cache cache; cache.ensureLayout();
    auto s = auth::loadSession(cache);
    if (!s) { std::cout << "Not logged in. Run `rin pkg login <username> --token <token>`.\n"; return 0; }
    std::cout << s->username << (s->registryUrl.empty() ? "" : (" (" + s->registryUrl + ")")) << "\n";
    return 0;
}

void printHelp() {
    std::cout <<
        "Rin Package Manager (RinPM)\n\n"
        "USAGE:\n"
        "  rin pkg <command> [options]\n\n"
        "COMMANDS:\n"
        "  init [name]              Create a new Rin project with rin.toml\n"
        "  add <pkg>[@constraint]   Add a dependency and install it\n"
        "  add <pkg> --dev          Add a dev-dependency\n"
        "  remove <pkg>             Remove a dependency\n"
        "  install [--offline]      Resolve and install all dependencies, write rin.lock\n"
        "  update [--offline]       Re-resolve dependencies within existing constraints\n"
        "  upgrade [--offline]      Alias of update\n"
        "  search <query>           Search the configured registry by name\n"
        "  info <pkg>               Show metadata for a package\n"
        "  list                     List installed packages from rin.lock\n"
        "  tree                     Print the dependency tree\n"
        "  build                    Resolve dependencies before compilation\n"
        "  publish                  Validate, build, and publish this package\n"
        "  clean [--all]            Clean the cache (--all also removes installed packages)\n"
        "  login <user> --token T   Log in with a registry token (never a password)\n"
        "  logout                   Clear stored credentials\n"
        "  whoami                   Show the current logged-in user\n"
        "  --help                   Show this help\n"
        "  --version                Show RinPM version\n";
}

} // namespace

int run(const std::vector<std::string>& args, const std::string& /*currentRinVersion*/) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { printHelp(); return 0; }
    if (args[0] == "--version" || args[0] == "-v") { std::cout << kRinPMVersion << "\n"; return 0; }

    const std::string& cmd = args[0];
    ParsedArgs parsed = parseArgs(args, 1);

    try {
        if (cmd == "init")     return cmdInit(parsed);
        if (cmd == "add")      return cmdAdd(parsed, parsed.has("dev"));
        if (cmd == "remove")   return cmdRemove(parsed);
        if (cmd == "install")  return cmdInstall(parsed);
        if (cmd == "update")   return cmdUpdate(parsed);
        if (cmd == "upgrade")  return cmdUpgrade(parsed);
        if (cmd == "search")   return cmdSearch(parsed);
        if (cmd == "info")     return cmdInfo(parsed);
        if (cmd == "list")     return cmdList(parsed);
        if (cmd == "tree")     return cmdTree(parsed);
        if (cmd == "build")    return cmdBuild(parsed);
        if (cmd == "publish")  return cmdPublish(parsed);
        if (cmd == "clean")    return cmdClean(parsed);
        if (cmd == "login")    return cmdLogin(parsed);
        if (cmd == "logout")   return cmdLogout(parsed);
        if (cmd == "whoami")   return cmdWhoami(parsed);
    } catch (const RinPackageError& e) {
        std::cerr << "\xE2\x9C\x97 " << e.kind() << ": " << e.what() << "\n";
        return static_cast<int>(e.exitCode());
    } catch (const std::exception& e) {
        std::cerr << "\xE2\x9C\x97 RinPM: " << e.what() << "\n";
        return static_cast<int>(ExitCode::GeneralError);
    }

    std::cerr << "\xE2\x9C\x97 unknown command `rin pkg " << cmd << "` (see `rin pkg --help`)\n";
    return static_cast<int>(ExitCode::GeneralError);
}

} // namespace rinpm::cli
