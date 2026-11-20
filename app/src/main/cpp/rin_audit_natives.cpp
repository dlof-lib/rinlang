// rin_audit_natives.cpp — تدقيق الصلاحيات
#include "rin_audit_natives.h"
#include "rin_interpreter.h"
#include <chrono>
#include <deque>
#include <mutex>
#include <sstream>

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

static std::mutex g_audMu;
static std::deque<Value> g_auditLog;
static size_t g_auditLimit = 500;
static bool g_auditEnabled = false;
static std::string g_auditLevel = "minimal"; // minimal | detailed | full
static bool g_emitDenies = false;

static bool isAuditValue(const Value& v) {
    if (v.type != Value::Type::MAP || !v.map) return false;
    auto k = v.map->find("kind");
    return k != v.map->end() && k->second.type == Value::Type::STRING && k->second.str == "audit";
}

static Value makeAudit(const std::string& action, const std::string& result,
                       const std::string& subject, const std::string& scope,
                       const std::string& tokenId, const std::string& reason,
                       const Value& meta) {
    auto map = std::make_shared<std::map<std::string, Value>>();
    (*map)["kind"] = Value::string("audit");
    (*map)["id"] = Value::string("aud_" + std::to_string(nowMs()));
    (*map)["ts"] = Value::num(static_cast<double>(nowMs()));
    (*map)["action"] = Value::string(action);
    (*map)["result"] = Value::string(result);
    (*map)["subject"] = Value::string(subject);
    (*map)["scope"] = Value::string(scope);
    (*map)["token_id"] = Value::string(tokenId);
    (*map)["container"] = Value::nil();
    (*map)["function"] = Value::nil();
    (*map)["reason"] = Value::string(reason);
    (*map)["meta"] = (meta.type == Value::Type::MAP) ? meta : Value::map(std::make_shared<std::map<std::string, Value>>());
    return Value::map(map);
}

static void pushAudit(const Value& e) {
    if (!g_auditEnabled) return;
    std::lock_guard<std::mutex> lock(g_audMu);
    g_auditLog.push_front(e);
    while (g_auditLog.size() > g_auditLimit) g_auditLog.pop_back();
}

static std::string extractSubject(const Value& t) {
    if (t.type == Value::Type::MAP && t.map && t.map->count("subject"))
        return valueToString((*t.map)["subject"]);
    return "";
}

static std::string extractTokenId(const Value& t) {
    if (t.type == Value::Type::MAP && t.map && t.map->count("id"))
        return valueToString((*t.map)["id"]);
    return "";
}

} // namespace

void registerNativesAudit(Interpreter& interp) {
    auto& natives = interp.natives;

    natives["audit_on"] = [](Args&, int) -> Value {
        g_auditEnabled = true;
        return Value::boolean_(true);
    };
    natives["audit_off"] = [](Args&, int) -> Value {
        g_auditEnabled = false;
        return Value::boolean_(false);
    };
    natives["audit_enabled"] = [](Args&, int) -> Value {
        return Value::boolean_(g_auditEnabled);
    };

    natives["audit_level"] = [](Args& a, int line) -> Value {
        expectArgs("audit_level", a, 1, line);
        g_auditLevel = valueToString(a[0]);
        return Value::nil();
    };

    natives["audit_limit"] = [](Args& a, int line) -> Value {
        expectArgs("audit_limit", a, 1, line);
        double n = asNumber(a[0], "audit_limit", line);
        if (n < 1) n = 1;
        g_auditLimit = static_cast<size_t>(n);
        return Value::nil();
    };

    natives["audit_log"] = [](Args& a, int line) -> Value {
        expectArgs("audit_log", a, 1, line);
        if (a[0].type != Value::Type::MAP || !a[0].map)
            throw diagErr(diag::Code::E0004_InvalidType, line, "audit_log expects a map");
        auto map = std::make_shared<std::map<std::string, Value>>(*a[0].map);
        (*map)["kind"] = Value::string("audit");
        if (!map->count("ts")) (*map)["ts"] = Value::num(static_cast<double>(nowMs()));
        if (!map->count("id")) (*map)["id"] = Value::string("aud_" + std::to_string(nowMs()));
        Value e = Value::map(map);
        pushAudit(e);
        return e;
    };

    natives["audit_check"] = [&interp](Args& a, int line) -> Value {
        expectArgs("audit_check", a, 2, line);
        // يستدعي token_has إن وُجد
        bool ok = false;
        auto it = interp.natives.find("token_has");
        if (it != interp.natives.end()) {
            Args args = {a[0], a[1]};
            Value r = it->second(args, line);
            ok = (r.type == Value::Type::BOOL && r.boolean);
        }
        std::string result = ok ? "allow" : "deny";
        Value entry = makeAudit("check", result, extractSubject(a[0]), valueToString(a[1]),
                                extractTokenId(a[0]), ok ? "" : "missing scope", Value::nil());
        pushAudit(entry);
        return Value::boolean_(ok);
    };

    natives["audit_require"] = [&interp](Args& a, int line) -> Value {
        expectArgs("audit_require", a, 2, line);
        auto it = interp.natives.find("token_require");
        bool ok = true;
        std::string reason;
        try {
            if (it != interp.natives.end()) {
                Args args = {a[0], a[1]};
                it->second(args, line);
            }
        } catch (...) {
            ok = false;
            reason = "missing scope or invalid token";
            Value entry = makeAudit("require", "deny", extractSubject(a[0]), valueToString(a[1]),
                                    extractTokenId(a[0]), reason, Value::nil());
            pushAudit(entry);
            throw;
        }
        Value entry = makeAudit("require", "allow", extractSubject(a[0]), valueToString(a[1]),
                                extractTokenId(a[0]), "", Value::nil());
        pushAudit(entry);
        return Value::boolean_(true);
    };

    natives["audit_entries"] = [](Args& a, int) -> Value {
        std::lock_guard<std::mutex> lock(g_audMu);
        auto out = std::make_shared<std::vector<Value>>();
        std::string filterResult, filterSubject, filterScope;
        if (!a.empty() && a[0].type == Value::Type::MAP && a[0].map) {
            auto& f = *a[0].map;
            if (f.count("result") && f["result"].type == Value::Type::STRING) filterResult = f["result"].str;
            if (f.count("subject") && f["subject"].type == Value::Type::STRING) filterSubject = f["subject"].str;
            if (f.count("scope") && f["scope"].type == Value::Type::STRING) filterScope = f["scope"].str;
        }
        for (auto& e : g_auditLog) {
            if (!e.map) continue;
            if (!filterResult.empty() && valueToString((*e.map)["result"]) != filterResult) continue;
            if (!filterSubject.empty() && valueToString((*e.map)["subject"]) != filterSubject) continue;
            if (!filterScope.empty() && valueToString((*e.map)["scope"]) != filterScope) continue;
            out->push_back(e);
        }
        return Value::array(out);
    };

    natives["audit_last"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_audMu);
        if (g_auditLog.empty()) return Value::nil();
        return g_auditLog.front();
    };

    natives["audit_count"] = [](Args& a, int) -> Value {
        std::lock_guard<std::mutex> lock(g_audMu);
        if (a.empty()) return Value::num(static_cast<double>(g_auditLog.size()));
        std::string filterResult;
        if (a[0].type == Value::Type::MAP && a[0].map && a[0].map->count("result"))
            filterResult = valueToString((*a[0].map)["result"]);
        size_t n = 0;
        for (auto& e : g_auditLog) {
            if (!filterResult.empty() && e.map && valueToString((*e.map)["result"]) != filterResult) continue;
            ++n;
        }
        return Value::num(static_cast<double>(n));
    };

    natives["audit_summary"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_audMu);
        auto map = std::make_shared<std::map<std::string, Value>>();
        size_t allow = 0, deny = 0;
        auto byScope = std::make_shared<std::map<std::string, Value>>();
        auto bySubject = std::make_shared<std::map<std::string, Value>>();
        for (auto& e : g_auditLog) {
            if (!e.map) continue;
            std::string res = valueToString((*e.map)["result"]);
            if (res == "allow") ++allow; else if (res == "deny") ++deny;
            std::string sc = valueToString((*e.map)["scope"]);
            std::string sub = valueToString((*e.map)["subject"]);
            if (!sc.empty()) {
                double c = byScope->count(sc) ? (*byScope)[sc].number : 0;
                (*byScope)[sc] = Value::num(c + 1);
            }
            if (!sub.empty()) {
                double c = bySubject->count(sub) ? (*bySubject)[sub].number : 0;
                (*bySubject)[sub] = Value::num(c + 1);
            }
        }
        (*map)["total"] = Value::num(static_cast<double>(g_auditLog.size()));
        (*map)["allow"] = Value::num(static_cast<double>(allow));
        (*map)["deny"] = Value::num(static_cast<double>(deny));
        (*map)["by_scope"] = Value::map(byScope);
        (*map)["by_subject"] = Value::map(bySubject);
        return Value::map(map);
    };

    natives["audit_clear"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_audMu);
        g_auditLog.clear();
        return Value::nil();
    };

    natives["audit_purge"] = [](Args& a, int line) -> Value {
        if (a.empty() || a[0].type != Value::Type::MAP) return Value::nil();
        double before = 0;
        if (a[0].map->count("before") && (*a[0].map)["before"].type == Value::Type::NUMBER)
            before = (*a[0].map)["before"].number;
        std::lock_guard<std::mutex> lock(g_audMu);
        std::deque<Value> kept;
        for (auto& e : g_auditLog) {
            if (e.map && (*e.map)["ts"].type == Value::Type::NUMBER && (*e.map)["ts"].number < before)
                continue;
            kept.push_back(e);
        }
        g_auditLog.swap(kept);
        return Value::nil();
    };

    natives["audit_emit_denies"] = [](Args& a, int line) -> Value {
        expectArgs("audit_emit_denies", a, 1, line);
        g_emitDenies = a[0].isTruthy();
        return Value::boolean_(g_emitDenies);
    };

    natives["is_audit"] = [](Args& a, int line) -> Value {
        expectArgs("is_audit", a, 1, line);
        return Value::boolean_(isAuditValue(a[0]));
    };

    // as_message على سجل تدقيق (إعادة استخدام منطق message إن وُجد)
    natives["__audit_as_message"] = [](Args& a, int line) -> Value {
        expectArgs("__audit_as_message", a, 1, line);
        if (!isAuditValue(a[0]) || !a[0].map) return Value::nil();
        auto& m = *a[0].map;
        std::string result = valueToString(m["result"]);
        std::string level = (result == "deny") ? "warning" : "info";
        std::string body = "audit " + valueToString(m["action"]) + " " + result;
        if (!valueToString(m["scope"]).empty()) body += " scope=" + valueToString(m["scope"]);
        auto map = std::make_shared<std::map<std::string, Value>>();
        (*map)["kind"] = Value::string("message");
        (*map)["level"] = Value::string(level);
        (*map)["body"] = Value::string(body);
        (*map)["meta"] = a[0];
        (*map)["code"] = Value::string(result == "deny" ? "E_AUDIT_DENY" : "I_AUDIT");
        (*map)["ts"] = Value::num(static_cast<double>(nowMs()));
        return Value::map(map);
    };
}
