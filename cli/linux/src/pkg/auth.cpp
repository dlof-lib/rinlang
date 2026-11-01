// cli/linux/src/pkg/auth.cpp
#include "auth.h"
#include "toml_lite.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace rinpm::auth {

static std::string credentialsPath(const Cache& cache) { return cache.configDir() + "/credentials"; }

void saveSession(const Cache& cache, const Session& s) {
    cache.ensureLayout();
    toml::Writer w;
    w.section("session");
    w.kv("username", s.username);
    w.kv("registry", s.registryUrl);
    w.kv("token", s.token);
    std::string path = credentialsPath(cache);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << w.str();
    }
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR); // 0600: المالك فقط — لا صلاحيات قراءة لأي مستخدم آخر
}

std::optional<Session> loadSession(const Cache& cache) {
    std::string path = credentialsPath(cache);
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    toml::Document doc;
    try {
        doc = toml::parse(buf.str());
    } catch (...) {
        return std::nullopt;
    }
    const toml::Table* sec = doc.section("session");
    if (!sec) return std::nullopt;
    Session s;
    s.username = toml::getStr(*sec, "username");
    s.registryUrl = toml::getStr(*sec, "registry");
    s.token = toml::getStr(*sec, "token");
    if (s.token.empty()) return std::nullopt;
    return s;
}

void clearSession(const Cache& cache) {
    ::remove(credentialsPath(cache).c_str());
}

} // namespace rinpm::auth
