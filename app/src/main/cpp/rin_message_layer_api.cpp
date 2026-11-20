// rin_message_layer_api.cpp — ربط message.layer بـ @container.api / callApi
// يُستدعى بعد registerNativesMessageLayer
#include "rin_interpreter.h"
#include <map>
#include <sstream>

namespace {

using Args = std::vector<Value>;

static std::string str(const Value& v) {
    if (v.type == Value::Type::STRING) return v.str;
    if (v.type == Value::Type::NUMBER) { std::ostringstream o; o << v.number; return o.str(); }
    return "";
}

static bool isMsg(const Value& v) {
    if (v.type != Value::Type::MAP || !v.map) return false;
    auto k = v.map->find("kind");
    return (k != v.map->end() && k->second.type == Value::Type::STRING && k->second.str == "message")
        || (v.map->count("level") && v.map->count("body"));
}

} // namespace

void registerNativesMessageLayerApi(Interpreter& interp) {
    auto& natives = interp.natives;

    // message.from_callapi(response, opts?)
    // response = نتيجة callApi: {ok, status, body, attempts, ...}
    natives["message.from_callapi"] = [&interp](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0004_InvalidType, line, "message.from_callapi expects callApi result");
        auto fromHttp = interp.natives.find("message.from_http");
        if (fromHttp != interp.natives.end()) {
            Value opts = (a.size() >= 2) ? a[1] : Value::map(std::make_shared<std::map<std::string, Value>>());
            return fromHttp->second(Args{a[0], opts}, line);
        }
        // fallback مبسّط
        if (a[0].type != Value::Type::MAP || !a[0].map) return a[0];
        auto& r = *a[0].map;
        bool ok = r.count("ok") && r["ok"].isTruthy();
        int status = r.count("status") && r["status"].type == Value::Type::NUMBER
            ? static_cast<int>(r["status"].number) : 0;
        std::string level = !ok ? "error" : (status >= 500 ? "error" : (status >= 400 ? "warning" : "success"));
        auto map = std::make_shared<std::map<std::string, Value>>();
        (*map)["kind"] = Value::string("message");
        (*map)["level"] = Value::string(level);
        (*map)["body"] = r.count("body") ? r["body"] : Value::nil();
        (*map)["code"] = Value::string(!ok ? "NET_FAIL" : ("HTTP_" + std::to_string(status)));
        (*map)["meta"] = a[0];
        return Value::map(map);
    };

    // message.layer_info(name) = apiInfo + layer flags
    natives["message.layer_info"] = [&interp](Args& a, int line) -> Value {
        expectArgs("message.layer_info", a, 1, line);
        Value info = Value::map(std::make_shared<std::map<std::string, Value>>());
        auto it = interp.natives.find("apiInfo");
        if (it != interp.natives.end()) {
            try { info = it->second(Args{a[0]}, line); } catch (...) {}
        }
        if (info.type == Value::Type::MAP && info.map) {
            (*info.map)["layer"] = Value::boolean_(true);
        }
        return info;
    };

    // message.layer_bind(name) — يفعّل layer ويحاول قراءة token من الحاوية عبر apiInfo
    natives["message.layer_bind"] = [&interp](Args& a, int line) -> Value {
        expectArgs("message.layer_bind", a, 1, line);
        auto layer = interp.natives.find("message.layer");
        if (layer != interp.natives.end()) {
            auto opts = std::make_shared<std::map<std::string, Value>>();
            (*opts)["audit"] = Value::boolean_(false);
            (*opts)["emit"] = Value::boolean_(false);
            layer->second(Args{a[0], Value::map(opts)}, line);
        }
        return Value::boolean_(true);
    };

    // لفّ message.get/post/... لتفضيل callApi عند وجود الحاوية
    // (إن كانت النسخ السابقة مسجّلة، نعيد تعريف المغلفات لتستدعي callApi أولاً)
    auto wrapCallApi = [&interp](const std::string& method) {
        return [&interp, method](Args& a, int line) -> Value {
            if (a.size() < 2) throw diagErr(diag::Code::E0004_InvalidType, line, "message.* needs api name and path");
            Value name = a[0];
            Value path = a[1];
            Value body = Value::nil();
            Value opts = Value::nil();
            if (method == "GET" || method == "DELETE") {
                opts = (a.size() >= 3) ? a[2] : Value::nil();
            } else {
                body = (a.size() >= 3) ? a[2] : Value::nil();
                opts = (a.size() >= 4) ? a[3] : Value::nil();
            }

            auto callApi = interp.natives.find("callApi");
            Value raw = Value::nil();
            bool used = false;
            if (callApi != interp.natives.end()) {
                try {
                    Args ca;
                    ca.push_back(name);
                    ca.push_back(Value::string(method));
                    ca.push_back(path);
                    if (method != "GET" && method != "DELETE") ca.push_back(body);
                    if (opts.type != Value::Type::NIL) ca.push_back(opts);
                    raw = callApi->second(ca, line);
                    used = true;
                } catch (...) {
                    used = false;
                }
            }
            if (!used) {
                // اترك السلوك السابق لـ message.get إن وُجد مسار آخر
                auto fallback = interp.natives.find("message.http");
                if (fallback != interp.natives.end() && method == "GET") {
                    return fallback->second(Args{Value::string("GET"), path}, line);
                }
                throw diagErr(diag::Code::E0004_InvalidType, line, "callApi not available for container.api");
            }

            auto meta = std::make_shared<std::map<std::string, Value>>();
            (*meta)["api"] = name;
            (*meta)["method"] = Value::string(method);
            (*meta)["path"] = path;

            auto fromCa = interp.natives.find("message.from_callapi");
            if (fromCa != interp.natives.end())
                return fromCa->second(Args{raw, Value::map(meta)}, line);
            return raw;
        };
    };

    natives["message.get"] = wrapCallApi("GET");
    natives["message.post"] = wrapCallApi("POST");
    natives["message.put"] = wrapCallApi("PUT");
    natives["message.patch"] = wrapCallApi("PATCH");
    natives["message.delete"] = wrapCallApi("DELETE");
}
