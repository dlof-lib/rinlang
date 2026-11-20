// rin_cap_token_natives.cpp — توكنات الصلاحيات (capability / session tokens)
#include "rin_cap_token_natives.h"
#include "rin_interpreter.h"
#include <chrono>
#include <sstream>
#include <random>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {

using Args = std::vector<Value>;
using Clock = std::chrono::system_clock;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

static std::string valueToString(const Value& v) {
    if (v.type == Value::Type::STRING) return v.str;
    if (v.type == Value::Type::NUMBER) { std::ostringstream o; o << v.number; return o.str(); }
    if (v.type == Value::Type::BOOL) return v.boolean ? "true" : "false";
    return "";
}

static std::string g_secret = "rin-default-token-secret";
static std::mutex g_tokMu;
static std::unordered_set<std::string> g_revoked;
static std::unordered_map<std::string, Value> g_store;

static std::string simpleSig(const std::string& data) {
    // توقيع بسيط غير مشفّر — للسلامة الداخلية فقط
    uint64_t h = 14695981039346656037ull;
    for (char c : g_secret) { h ^= static_cast<uint8_t>(c); h *= 1099511628211ull; }
    for (char c : data) { h ^= static_cast<uint8_t>(c); h *= 1099511628211ull; }
    std::ostringstream o; o << std::hex << h;
    return o.str();
}

static std::string genId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream o;
    o << "tok_" << std::hex << rng() << rng();
    return o.str();
}

static bool isTokenValue(const Value& v) {
    if (v.type != Value::Type::MAP || !v.map) return false;
    auto k = v.map->find("kind");
    return k != v.map->end() && k->second.type == Value::Type::STRING && k->second.str == "token";
}

static Value makeToken(const std::string& subject, const std::vector<std::string>& scopes,
                       int64_t expires, const Value& meta, const std::string& issuer, const std::string& id) {
    auto map = std::make_shared<std::map<std::string, Value>>();
    (*map)["kind"] = Value::string("token");
    (*map)["id"] = Value::string(id);
    (*map)["subject"] = Value::string(subject);
    auto sc = std::make_shared<std::vector<Value>>();
    for (auto& s : scopes) sc->push_back(Value::string(s));
    (*map)["scopes"] = Value::array(sc);
    (*map)["meta"] = (meta.type == Value::Type::MAP) ? meta : Value::map(std::make_shared<std::map<std::string, Value>>());
    (*map)["issued"] = Value::num(static_cast<double>(nowMs()));
    (*map)["expires"] = (expires > 0) ? Value::num(static_cast<double>(expires)) : Value::nil();
    (*map)["issuer"] = Value::string(issuer);
    (*map)["revoked"] = Value::boolean_(false);
    std::string payload = id + "|" + subject + "|";
    for (auto& s : scopes) payload += s + ",";
    payload += "|" + std::to_string(expires) + "|" + issuer;
    (*map)["sig"] = Value::string(simpleSig(payload));
    return Value::map(map);
}

static bool tokenOk(const Value& t) {
    if (!isTokenValue(t) || !t.map) return false;
    auto& m = *t.map;
    if (m.count("revoked") && m["revoked"].type == Value::Type::BOOL && m["revoked"].boolean)
        return false;
    {
        std::lock_guard<std::mutex> lock(g_tokMu);
        auto idit = m.find("id");
        if (idit != m.end() && idit->second.type == Value::Type::STRING &&
            g_revoked.count(idit->second.str)) return false;
    }
    auto exp = m.find("expires");
    if (exp != m.end() && exp->second.type == Value::Type::NUMBER) {
        if (nowMs() >= static_cast<int64_t>(exp->second.number)) return false;
    }
    // تحقق توقيع مبسّط
    auto id = valueToString(m["id"]);
    auto subject = valueToString(m["subject"]);
    auto issuer = valueToString(m["issuer"]);
    int64_t expires = 0;
    if (m["expires"].type == Value::Type::NUMBER) expires = static_cast<int64_t>(m["expires"].number);
    std::string payload = id + "|" + subject + "|";
    if (m["scopes"].type == Value::Type::ARRAY && m["scopes"].array)
        for (auto& s : *m["scopes"].array) payload += valueToString(s) + ",";
    payload += "|" + std::to_string(expires) + "|" + issuer;
    auto sigit = m.find("sig");
    if (sigit == m.end() || sigit->second.type != Value::Type::STRING) return false;
    return sigit->second.str == simpleSig(payload);
}

static bool hasScope(const Value& t, const std::string& scope) {
    if (!isTokenValue(t) || !t.map) return false;
    auto it = t.map->find("scopes");
    if (it == t.map->end() || it->second.type != Value::Type::ARRAY || !it->second.array) return false;
    for (auto& s : *it->second.array)
        if (valueToString(s) == scope) return true;
    return false;
}

static Value tokenError(const std::string& code, const std::string& body, int line) {
    auto map = std::make_shared<std::map<std::string, Value>>();
    (*map)["kind"] = Value::string("message");
    (*map)["level"] = Value::string("error");
    (*map)["body"] = Value::string(body);
    (*map)["code"] = Value::string(code);
    (*map)["meta"] = Value::map(std::make_shared<std::map<std::string, Value>>());
    (*map)["ts"] = Value::num(static_cast<double>(nowMs()));
    (*map)["message"] = Value::string("[" + code + "] " + body);
    (*map)["line"] = Value::num(static_cast<double>(line));
    return Value::map(map);
}

} // namespace

void registerNativesCapToken(Interpreter& interp) {
    auto& natives = interp.natives;

    // token(subject, scopes, opts?)
    natives["token"] = [](Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0004_InvalidType, line, "'token' expects subject and scopes");
        std::string subject = valueToString(a[0]);
        std::vector<std::string> scopes;
        if (a[1].type == Value::Type::ARRAY && a[1].array) {
            for (auto& s : *a[1].array) scopes.push_back(valueToString(s));
        } else {
            scopes.push_back(valueToString(a[1]));
        }
        int64_t expires = 0;
        Value meta = Value::nil();
        std::string issuer = "app";
        std::string id = genId();
        if (a.size() >= 3 && a[2].type == Value::Type::MAP && a[2].map) {
            auto& o = *a[2].map;
            if (o.count("ttl") && o["ttl"].type == Value::Type::NUMBER)
                expires = nowMs() + static_cast<int64_t>(o["ttl"].number * 1000.0);
            if (o.count("expires") && o["expires"].type == Value::Type::NUMBER)
                expires = static_cast<int64_t>(o["expires"].number);
            if (o.count("meta")) meta = o["meta"];
            if (o.count("issuer") && o["issuer"].type == Value::Type::STRING) issuer = o["issuer"].str;
            if (o.count("id") && o["id"].type == Value::Type::STRING) id = o["id"].str;
        }
        return makeToken(subject, scopes, expires, meta, issuer, id);
    };

    natives["is_token"] = [](Args& a, int line) -> Value {
        expectArgs("is_token", a, 1, line);
        return Value::boolean_(isTokenValue(a[0]));
    };

    natives["token_ok"] = [](Args& a, int line) -> Value {
        expectArgs("token_ok", a, 1, line);
        return Value::boolean_(tokenOk(a[0]));
    };

    natives["token_expired"] = [](Args& a, int line) -> Value {
        expectArgs("token_expired", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::boolean_(true);
        auto exp = a[0].map->find("expires");
        if (exp == a[0].map->end() || exp->second.type != Value::Type::NUMBER) return Value::boolean_(false);
        return Value::boolean_(nowMs() >= static_cast<int64_t>(exp->second.number));
    };

    natives["token_revoked"] = [](Args& a, int line) -> Value {
        expectArgs("token_revoked", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::boolean_(true);
        if (a[0].map->count("revoked") && (*a[0].map)["revoked"].boolean) return Value::boolean_(true);
        std::lock_guard<std::mutex> lock(g_tokMu);
        auto id = valueToString((*a[0].map)["id"]);
        return Value::boolean_(g_revoked.count(id) > 0);
    };

    natives["token_has"] = [](Args& a, int line) -> Value {
        expectArgs("token_has", a, 2, line);
        return Value::boolean_(hasScope(a[0], valueToString(a[1])));
    };

    natives["token_has_all"] = [](Args& a, int line) -> Value {
        expectArgs("token_has_all", a, 2, line);
        if (a[1].type != Value::Type::ARRAY || !a[1].array) return Value::boolean_(false);
        for (auto& s : *a[1].array)
            if (!hasScope(a[0], valueToString(s))) return Value::boolean_(false);
        return Value::boolean_(true);
    };

    natives["token_has_any"] = [](Args& a, int line) -> Value {
        expectArgs("token_has_any", a, 2, line);
        if (a[1].type != Value::Type::ARRAY || !a[1].array) return Value::boolean_(false);
        for (auto& s : *a[1].array)
            if (hasScope(a[0], valueToString(s))) return Value::boolean_(true);
        return Value::boolean_(false);
    };

    natives["token_require"] = [](Args& a, int line) -> Value {
        expectArgs("token_require", a, 2, line);
        if (!tokenOk(a[0]))
            throw RinRuntimeError(tokenError("E_TOKEN_INVALID", "token is not valid", line), line);
        if (!hasScope(a[0], valueToString(a[1])))
            throw RinRuntimeError(tokenError("E_TOKEN_SCOPE", "missing scope: " + valueToString(a[1]), line), line);
        return Value::boolean_(true);
    };

    natives["token_require_ok"] = [](Args& a, int line) -> Value {
        expectArgs("token_require_ok", a, 1, line);
        if (!tokenOk(a[0]))
            throw RinRuntimeError(tokenError("E_TOKEN_INVALID", "token is not valid or expired/revoked", line), line);
        return Value::boolean_(true);
    };

    natives["token_subject"] = [](Args& a, int line) -> Value {
        expectArgs("token_subject", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::nil();
        return (*a[0].map)["subject"];
    };

    natives["token_scopes"] = [](Args& a, int line) -> Value {
        expectArgs("token_scopes", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::nil();
        return (*a[0].map)["scopes"];
    };

    natives["token_meta"] = [](Args& a, int line) -> Value {
        expectArgs("token_meta", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::nil();
        return (*a[0].map)["meta"];
    };

    natives["token_id"] = [](Args& a, int line) -> Value {
        expectArgs("token_id", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::nil();
        return (*a[0].map)["id"];
    };

    natives["token_age"] = [](Args& a, int line) -> Value {
        expectArgs("token_age", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::nil();
        double issued = (*a[0].map)["issued"].number;
        return Value::num((nowMs() - static_cast<int64_t>(issued)) / 1000.0);
    };

    natives["token_remaining"] = [](Args& a, int line) -> Value {
        expectArgs("token_remaining", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::nil();
        auto exp = a[0].map->find("expires");
        if (exp == a[0].map->end() || exp->second.type != Value::Type::NUMBER) return Value::nil();
        double left = (exp->second.number - static_cast<double>(nowMs())) / 1000.0;
        return Value::num(left > 0 ? left : 0.0);
    };

    natives["token_revoke"] = [](Args& a, int line) -> Value {
        expectArgs("token_revoke", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::boolean_(false);
        (*a[0].map)["revoked"] = Value::boolean_(true);
        std::lock_guard<std::mutex> lock(g_tokMu);
        g_revoked.insert(valueToString((*a[0].map)["id"]));
        return Value::boolean_(true);
    };

    natives["token_refresh"] = [](Args& a, int line) -> Value {
        expectArgsRange("token_refresh", a, 1, 2, line);
        if (!isTokenValue(a[0]) || !a[0].map) throw diagErr(diag::Code::E0004_InvalidType, line, "token_refresh expects a token");
        auto& m = *a[0].map;
        std::vector<std::string> scopes;
        if (m["scopes"].type == Value::Type::ARRAY && m["scopes"].array)
            for (auto& s : *m["scopes"].array) scopes.push_back(valueToString(s));
        int64_t expires = 0;
        if (a.size() >= 2 && a[1].type == Value::Type::NUMBER)
            expires = nowMs() + static_cast<int64_t>(a[1].number * 1000.0);
        else if (m["expires"].type == Value::Type::NUMBER) {
            double issued = m["issued"].number;
            double oldExp = m["expires"].number;
            expires = nowMs() + static_cast<int64_t>(oldExp - issued);
        }
        return makeToken(valueToString(m["subject"]), scopes, expires, m["meta"],
                         valueToString(m["issuer"]), genId());
    };

    natives["token_narrow"] = [](Args& a, int line) -> Value {
        expectArgs("token_narrow", a, 2, line);
        if (!isTokenValue(a[0]) || !a[0].map) throw diagErr(diag::Code::E0004_InvalidType, line, "token_narrow expects a token");
        std::vector<std::string> scopes;
        if (a[1].type == Value::Type::ARRAY && a[1].array)
            for (auto& s : *a[1].array) {
                std::string sc = valueToString(s);
                if (hasScope(a[0], sc)) scopes.push_back(sc);
            }
        else {
            std::string sc = valueToString(a[1]);
            if (hasScope(a[0], sc)) scopes.push_back(sc);
        }
        auto& m = *a[0].map;
        int64_t expires = 0;
        if (m["expires"].type == Value::Type::NUMBER) expires = static_cast<int64_t>(m["expires"].number);
        return makeToken(valueToString(m["subject"]), scopes, expires, m["meta"],
                         valueToString(m["issuer"]), genId());
    };

    natives["token_secret"] = [](Args& a, int line) -> Value {
        expectArgs("token_secret", a, 1, line);
        g_secret = valueToString(a[0]);
        return Value::nil();
    };

    natives["token_store"] = [](Args& a, int line) -> Value {
        expectArgs("token_store", a, 1, line);
        if (!isTokenValue(a[0]) || !a[0].map) return Value::boolean_(false);
        std::lock_guard<std::mutex> lock(g_tokMu);
        g_store[valueToString((*a[0].map)["id"])] = a[0];
        return Value::boolean_(true);
    };

    natives["token_load"] = [](Args& a, int line) -> Value {
        expectArgs("token_load", a, 1, line);
        std::lock_guard<std::mutex> lock(g_tokMu);
        auto it = g_store.find(valueToString(a[0]));
        return (it != g_store.end()) ? it->second : Value::nil();
    };

    natives["token_list"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_tokMu);
        auto out = std::make_shared<std::vector<Value>>();
        for (auto& kv : g_store) out->push_back(kv.second);
        return Value::array(out);
    };

    natives["token_clear"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_tokMu);
        g_store.clear();
        return Value::nil();
    };

    natives["token_purge_expired"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_tokMu);
        for (auto it = g_store.begin(); it != g_store.end(); ) {
            if (!tokenOk(it->second)) it = g_store.erase(it);
            else ++it;
        }
        return Value::nil();
    };

    natives["token_event"] = [](Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0004_InvalidType, line, "token_event(token, action, meta?)");
        auto map = std::make_shared<std::map<std::string, Value>>();
        (*map)["kind"] = Value::string("token-event");
        (*map)["level"] = Value::string("info");
        (*map)["action"] = Value::string(valueToString(a[1]));
        (*map)["token"] = a[0];
        (*map)["meta"] = (a.size() >= 3) ? a[2] : Value::map(std::make_shared<std::map<std::string, Value>>());
        (*map)["ts"] = Value::num(static_cast<double>(nowMs()));
        (*map)["body"] = Value::string("token " + valueToString(a[1]));
        return Value::map(map);
    };
}
