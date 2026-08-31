// cli/linux/src/pkg/registry.cpp
#include "registry.h"
#include "toml_lite.h"
#include "sha256.h"
#include "errors.h"
#include "rin_http.h"
#include "json_lite.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>

namespace rinpm {

namespace {
bool exists(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0; }
bool isDir(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
void mkdirP(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' || i + 1 == path.size()) if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
    }
}
}

// ============================================================================
// LocalRegistry / FileRegistry
// ============================================================================

LocalRegistry::LocalRegistry(std::string root) : root_(std::move(root)) { mkdirP(root_); }

std::string LocalRegistry::metaPath(const std::string& name, const std::string& version) const {
    return root_ + "/" + name + "/" + version + "/meta.toml";
}
std::string LocalRegistry::archivePath(const std::string& name, const std::string& version) const {
    return root_ + "/" + name + "/" + version + "/" + name + "-" + version + ".rinpkg";
}

std::vector<Version> LocalRegistry::availableVersions(const std::string& name) const {
    std::vector<Version> out;
    std::string dir = root_ + "/" + name;
    if (!isDir(dir)) return out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        std::string vname = ent->d_name;
        if (vname == "." || vname == "..") continue;
        if (!isDir(dir + "/" + vname)) continue;
        Version v;
        if (tryParseVersion(vname, v)) out.push_back(v);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

PackageMeta LocalRegistry::fetchMeta(const std::string& name, const std::string& version) const {
    std::string mp = metaPath(name, version);
    std::ifstream in(mp, std::ios::binary);
    if (!in) {
        throw RinPackageNotFound("package `" + name + "@" + version + "` was not found in local registry `" + root_ + "`");
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    toml::Document doc;
    try {
        doc = toml::parse(buf.str());
    } catch (const toml::ParseError& e) {
        throw RinRegistryError("corrupt registry metadata for `" + name + "@" + version + "`: " + e.what());
    }
    PackageMeta meta;
    meta.name = name;
    meta.version = version;
    if (const toml::Table* p = doc.section("package")) {
        meta.description = toml::getStr(*p, "description");
        meta.authors = toml::getStrArray(*p, "authors");
        meta.license = toml::getStr(*p, "license");
        meta.repository = toml::getStr(*p, "repository");
        meta.homepage = toml::getStr(*p, "homepage");
        meta.keywords = toml::getStrArray(*p, "keywords");
    }
    if (const toml::Table* d = doc.section("dependencies")) {
        for (auto& [k, v] : *d) {
            std::string constraint = (v.kind == toml::Value::Kind::String) ? v.str : "*";
            meta.dependencyNames.push_back({k, constraint});
        }
    }
    if (const toml::Table* r = doc.section("package.rin")) {
        meta.rinVersionConstraint = toml::getStr(*r, "version");
    }
    if (const toml::Table* pl = doc.section("package.platforms")) {
        meta.platforms = toml::getStrArray(*pl, "os");
    }
    if (const toml::Table* sec = doc.section("registry")) {
        meta.checksum = toml::getStr(*sec, "checksum");
    }
    return meta;
}

bool LocalRegistry::downloadPackage(const std::string& name, const std::string& version,
                                     const std::string& destArchivePath) const {
    std::string src = archivePath(name, version);
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    mkdirP(destArchivePath.substr(0, destArchivePath.find_last_of('/')));
    std::ofstream out(destArchivePath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    std::string content = buf.str();
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return true;
}

std::vector<std::string> LocalRegistry::searchNames(const std::string& query) const {
    std::vector<std::string> out;
    if (!isDir(root_)) return out;
    DIR* d = ::opendir(root_.c_str());
    if (!d) return out;
    struct dirent* ent;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    while ((ent = ::readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        if (!isDir(root_ + "/" + name)) continue;
        std::string lname = name;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
        if (q.empty() || lname.find(q) != std::string::npos) out.push_back(name);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

void LocalRegistry::publishPackage(const PackageMeta& meta, const std::string& archivePath_) {
    std::string dir = root_ + "/" + meta.name + "/" + meta.version;
    mkdirP(dir);

    // 1) نسخ الأرشيف كما هو إلى مساحة الـ Registry.
    std::string destArchive = archivePath(meta.name, meta.version);
    std::ifstream in(archivePath_, std::ios::binary);
    if (!in) throw RinRegistryError("could not read archive `" + archivePath_ + "` for publishing");
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();
    std::ofstream out(destArchive, std::ios::binary | std::ios::trunc);
    if (!out) throw RinRegistryError("could not write to local registry at `" + destArchive + "`");
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    // 2) حساب checksum فعلي على الأرشيف كما خُزِّن (وليس القيمة المرسلة، لضمان الدقة).
    std::string checksum;
    if (!sha256HexOfFile(destArchive, checksum)) {
        throw RinRegistryError("could not compute checksum after publishing `" + meta.name + "@" + meta.version + "`");
    }

    // 3) كتابة meta.toml.
    toml::Writer w;
    w.section("package");
    w.kv("description", meta.description);
    w.kvArray("authors", meta.authors);
    w.kv("license", meta.license);
    w.kv("repository", meta.repository);
    w.kv("homepage", meta.homepage);
    w.kvArray("keywords", meta.keywords);
    w.blank();
    w.section("dependencies");
    for (auto& dep : meta.dependencyNames) w.kv(dep.name, dep.constraintRaw); // القيد الحقيقي كما كُتب في rin.toml الأصلي للحزمة
    w.blank();
    w.section("package.rin");
    w.kv("version", meta.rinVersionConstraint.empty() ? ">=1.0.0" : meta.rinVersionConstraint);
    w.blank();
    w.section("package.platforms");
    w.kvArray("os", meta.platforms);
    w.blank();
    w.section("registry");
    w.kv("checksum", checksum);

    std::ofstream mout(metaPath(meta.name, meta.version), std::ios::binary | std::ios::trunc);
    if (!mout) throw RinRegistryError("could not write registry metadata for `" + meta.name + "@" + meta.version + "`");
    mout << w.str();
}

// ============================================================================
// HttpRegistry — عميل حقيقي عبر rin::http::performRequest (نفس عميل curl
// الفعلي المستخدَم من natives اللغة نفسها). البروتوكول المتوقَّع من الخادم:
//   GET  {base}/api/v1/packages/{name}                 -> {"versions": [ {version, checksum, description,
//                                                          license, authors:[], repository, homepage,
//                                                          keywords:[], dependencies:[], rin, platforms:[]} ]}
//   GET  {base}/api/v1/packages/{name}/{version}/archive -> جسم الرد = بايتات .rinpkg الخام
//   GET  {base}/api/v1/search?q=...                     -> {"names": ["a","b"]}
//   POST {base}/api/v1/packages                         -> يُنشئ/يُحدِّث بيانات وصفية لإصدار (JSON)
//   PUT  {base}/api/v1/packages/{name}/{version}/archive -> يرفع بايتات الأرشيف الخام (جسم الطلب)
// كل الطلبات تحمل Header: "Authorization: Bearer <token>" إن توفر authToken.
// لا تعتمد اختبارات RinPM الحالية على أي خادم فعلي؛ هذه الطبقة جاهزة للتفعيل
// فور توفر خادم متوافق مع هذا البروتوكول.
// ============================================================================

HttpRegistry::HttpRegistry(std::string baseUrl, std::string authToken)
    : baseUrl_(std::move(baseUrl)), authToken_(std::move(authToken)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

namespace {
rin::http::HeaderList authHeaders(const std::string& token) {
    rin::http::HeaderList h;
    if (!token.empty()) h.push_back({"Authorization", "Bearer " + token});
    return h;
}
}

std::vector<Version> HttpRegistry::availableVersions(const std::string& name) const {
    auto res = rin::http::performRequest("GET", baseUrl_ + "/api/v1/packages/" + name, authHeaders(authToken_), "", 0);
    if (!res.ok || res.status != 200) return {};
    std::vector<Version> out;
    try {
        json::JsonValue v = json::parse(res.body);
        if (v.type == json::JsonValue::Type::Object) {
            auto it = v.object.find("versions");
            if (it != v.object.end() && it->second.type == json::JsonValue::Type::Array) {
                for (auto& item : it->second.array) {
                    Version parsed;
                    if (tryParseVersion(item.getStr("version"), parsed)) out.push_back(parsed);
                }
            }
        }
    } catch (const std::exception&) { /* رد غير صالح: نعامله كـ "لا إصدارات" بدل الانهيار */ }
    return out;
}

PackageMeta HttpRegistry::fetchMeta(const std::string& name, const std::string& version) const {
    auto res = rin::http::performRequest("GET", baseUrl_ + "/api/v1/packages/" + name, authHeaders(authToken_), "", 0);
    if (!res.ok || res.status != 200) {
        throw RinPackageNotFound("package `" + name + "@" + version + "` was not found on registry `" + baseUrl_ + "`");
    }
    PackageMeta meta;
    meta.name = name;
    meta.version = version;
    try {
        json::JsonValue v = json::parse(res.body);
        if (v.type == json::JsonValue::Type::Object) {
            auto it = v.object.find("versions");
            if (it != v.object.end() && it->second.type == json::JsonValue::Type::Array) {
                for (auto& item : it->second.array) {
                    if (item.getStr("version") != version) continue;
                    meta.description = item.getStr("description");
                    meta.license = item.getStr("license");
                    meta.repository = item.getStr("repository");
                    meta.homepage = item.getStr("homepage");
                    meta.checksum = item.getStr("checksum");
                    meta.rinVersionConstraint = item.getStr("rin");
                    meta.authors = item.stringArray("authors");
                    meta.keywords = item.stringArray("keywords");
                    // بروتوكول HTTP المبسَّط (غير مُختبَر فعلياً بلا شبكة حية، انظر التقرير) يرسل حالياً
                    // أسماء التبعيات فقط بلا قيود إصدار دقيقة؛ نعاملها كـ "*" حتى يُوسَّع البروتوكول لإرسال
                    // كائنات {name, constraint} كما تفعل meta.toml في LocalRegistry (الفعلي والمُختبَر).
                    for (auto& depName : item.stringArray("dependencies")) meta.dependencyNames.push_back({depName, "*"});
                    meta.platforms = item.stringArray("platforms");
                    return meta;
                }
            }
        }
    } catch (const std::exception& e) {
        throw RinRegistryError("invalid metadata response from `" + baseUrl_ + "` for `" + name + "`: " + e.what());
    }
    throw RinPackageNotFound("version `" + version + "` of `" + name + "` was not found on registry `" + baseUrl_ + "`");
}

bool HttpRegistry::downloadPackage(const std::string& name, const std::string& version,
                                    const std::string& destArchivePath) const {
    auto res = rin::http::performRequest("GET",
        baseUrl_ + "/api/v1/packages/" + name + "/" + version + "/archive", authHeaders(authToken_), "", 0);
    if (!res.ok || res.status != 200) return false;
    mkdirP(destArchivePath.substr(0, destArchivePath.find_last_of('/')));
    std::ofstream out(destArchivePath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(res.body.data(), static_cast<std::streamsize>(res.body.size()));
    return true;
}

std::vector<std::string> HttpRegistry::searchNames(const std::string& query) const {
    auto res = rin::http::performRequest("GET", baseUrl_ + "/api/v1/search?q=" + query, authHeaders(authToken_), "", 0);
    if (!res.ok || res.status != 200) return {};
    try {
        json::JsonValue v = json::parse(res.body);
        return v.stringArray("names");
    } catch (const std::exception&) {
        return {};
    }
}

void HttpRegistry::publishPackage(const PackageMeta& meta, const std::string& archivePath) {
    json::JsonValue payload = json::JsonValue::makeObject();
    payload.object["name"] = json::JsonValue::makeString(meta.name);
    payload.object["version"] = json::JsonValue::makeString(meta.version);
    payload.object["description"] = json::JsonValue::makeString(meta.description);
    payload.object["license"] = json::JsonValue::makeString(meta.license);
    payload.object["repository"] = json::JsonValue::makeString(meta.repository);
    payload.object["homepage"] = json::JsonValue::makeString(meta.homepage);
    payload.object["rin"] = json::JsonValue::makeString(meta.rinVersionConstraint);

    auto res1 = rin::http::performRequest("POST", baseUrl_ + "/api/v1/packages", authHeaders(authToken_),
                                           json::encode(payload), 0);
    if (!res1.ok || (res1.status != 200 && res1.status != 201)) {
        throw RinRegistryError("failed to publish metadata for `" + meta.name + "@" + meta.version +
                                "` to `" + baseUrl_ + "`" + (res1.error.empty() ? "" : (": " + res1.error)));
    }

    std::ifstream in(archivePath, std::ios::binary);
    if (!in) throw RinRegistryError("could not read archive `" + archivePath + "` for publishing");
    std::ostringstream buf;
    buf << in.rdbuf();
    auto res2 = rin::http::performRequest("PUT",
        baseUrl_ + "/api/v1/packages/" + meta.name + "/" + meta.version + "/archive",
        authHeaders(authToken_), buf.str(), 0);
    if (!res2.ok || (res2.status != 200 && res2.status != 201)) {
        throw RinRegistryError("failed to upload archive for `" + meta.name + "@" + meta.version +
                                "` to `" + baseUrl_ + "`" + (res2.error.empty() ? "" : (": " + res2.error)));
    }
}

} // namespace rinpm
