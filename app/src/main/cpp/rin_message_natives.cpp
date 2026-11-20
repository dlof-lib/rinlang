// rin_message_natives.cpp — نظام الرسائل الموحّد
// docs/message.md · إضافة بحتة · يُفكَّك msg/msg!/... في المحلّل إلى نداءات message
#include "rin_message_natives.h"
#include "rin_interpreter.h"   // أو الرأس الذي يعرّف Interpreter و Value و diagErr
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <deque>
#include <mutex>

namespace {

using Args = std::vector<Value>;
using Clock = std::chrono::system_clock;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

static std::string levelOf(const Value& v) {
    if (v.type == Value::Type::MAP && v.map) {
        auto it = v.map->find("level");
        if (it != v.map->end() && it->second.type == Value::Type::STRING)
            return it->second.str;
    }
    return "";
}

static bool isMessageValue(const Value& v) {
    if (v.type != Value::Type::MAP || !v.map) return false;
    auto k = v.map->find("kind");
    if (k != v.map->end() && k->second.type == Value::Type::STRING && k->second.str == "message")
        return true;
    // توافق مع أخطاء catch التي تحمل message/code
    return v.map->count("level") && v.map->count("body");
}

static int rankOf(const std::string& level) {
    if (level == "debug") return 0;
    if (level == "info") return 1;
    if (level == "success") return 2;
    if (level == "warning" || level == "warn") return 3;
    if (level == "error") return 4;
    return 1;
}

static std::string iconOf(const std::string& level) {
    if (level == "debug") return "🔍";
    if (level == "success") return "✅";
    if (level == "warning" || level == "warn") return "⚠️";
    if (level == "error") return "❌";
    return "ℹ️";
}

// سجل رسائل بسيط (حلقي)
static std::mutex g_msgMu;
static std::deque<Value> g_msgLog;
static size_t g_msgLimit = 200;
static std::string g_minLevel = "debug";
static std::string g_style = "pretty";

static void pushLog(const Value& m) {
    std::lock_guard<std::mutex> lock(g_msgMu);
    g_msgLog.push_front(m);
    while (g_msgLog.size() > g_msgLimit) g_msgLog.pop_back();
}

static Value makeMessage(const std::string& level, const std::string& body,
                         const Value& meta, const Value& code) {
    auto map = std::make_shared<std::map<std::string, Value>>();
    (*map)["kind"] = Value::string("message");
    (*map)["level"] = Value::string(level);
    (*map)["body"] = Value::string(body);
    (*map)["meta"] = (meta.type == Value::Type::MAP) ? meta : Value::map(std::make_shared<std::map<std::string, Value>>());
    (*map)["code"] = (code.type == Value::Type::STRING) ? code : Value::nil();
    (*map)["ts"] = Value::num(static_cast<double>(nowMs()));
    (*map)["source"] = Value::nil();
    (*map)["tags"] = Value::array(std::make_shared<std::vector<Value>>());
    return Value::map(map);
}

// استبدال بسيط {key} من meta
static std::string applyTemplate(const std::string& tmpl, const Value& meta) {
    if (meta.type != Value::Type::MAP || !meta.map) return tmpl;
    std::string out = tmpl;
    for (auto& kv : *meta.map) {
        std::string placeholder = "{" + kv.first + "}";
        std::string repl;
        if (kv.second.type == Value::Type::STRING) repl = kv.second.str;
        else if (kv.second.type == Value::Type::NUMBER) {
            std::ostringstream oss; oss << kv.second.number; repl = oss.str();
        } else continue;
        size_t pos = 0;
        while ((pos = out.find(placeholder, pos)) != std::string::npos) {
            out.replace(pos, placeholder.size(), repl);
            pos += repl.size();
        }
    }
    return out;
}

static std::string valueToString(const Value& v) {
    if (v.type == Value::Type::STRING) return v.str;
    if (v.type == Value::Type::NUMBER) {
        std::ostringstream oss; oss << v.number; return oss.str();
    }
    if (v.type == Value::Type::BOOL) return v.boolean ? "true" : "false";
    if (v.type == Value::Type::NIL) return "nil";
    return "<value>";
}

} // namespace

void registerNativesMessage(Interpreter& interp) {
    auto& natives = interp.natives;   // يفترض أن natives عام أو friend — عدّل حسب الرؤية الفعلية

    // message(body) | message(level, body) | message(level, body, meta) | message(level, body, meta, code)
    natives["message"] = [](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0004_InvalidType, line, "'message' expects at least 1 argument");
        std::string level = "info";
        std::string body;
        Value meta = Value::nil();
        Value code = Value::nil();

        if (a.size() == 1) {
            body = valueToString(a[0]);
        } else {
            level = valueToString(a[0]);
            body = valueToString(a[1]);
            if (a.size() >= 3) meta = a[2];
            if (a.size() >= 4) code = a[3];
        }
        if (meta.type == Value::Type::MAP)
            body = applyTemplate(body, meta);
        return makeMessage(level, body, meta, code);
    };

    natives["is_message"] = [](Args& a, int line) -> Value {
        expectArgs("is_message", a, 1, line);
        return Value::boolean_(isMessageValue(a[0]));
    };

    natives["message_level"] = [](Args& a, int line) -> Value {
        expectArgs("message_level", a, 1, line);
        if (!isMessageValue(a[0])) return Value::nil();
        return Value::string(levelOf(a[0]));
    };

    natives["message_body"] = [](Args& a, int line) -> Value {
        expectArgs("message_body", a, 1, line);
        if (!isMessageValue(a[0]) || !a[0].map) return Value::nil();
        auto it = a[0].map->find("body");
        return (it != a[0].map->end()) ? it->second : Value::nil();
    };

    natives["message_meta"] = [](Args& a, int line) -> Value {
        expectArgs("message_meta", a, 1, line);
        if (!isMessageValue(a[0]) || !a[0].map) return Value::nil();
        auto it = a[0].map->find("meta");
        return (it != a[0].map->end()) ? it->second : Value::nil();
    };

    natives["message_code"] = [](Args& a, int line) -> Value {
        expectArgs("message_code", a, 1, line);
        if (!isMessageValue(a[0]) || !a[0].map) return Value::nil();
        auto it = a[0].map->find("code");
        return (it != a[0].map->end()) ? it->second : Value::nil();
    };

    natives["message_ts"] = [](Args& a, int line) -> Value {
        expectArgs("message_ts", a, 1, line);
        if (!isMessageValue(a[0]) || !a[0].map) return Value::nil();
        auto it = a[0].map->find("ts");
        return (it != a[0].map->end()) ? it->second : Value::nil();
    };

    natives["message_rank"] = [](Args& a, int line) -> Value {
        expectArgs("message_rank", a, 1, line);
        return Value::num(static_cast<double>(rankOf(levelOf(a[0]))));
    };

    natives["as_message"] = [](Args& a, int line) -> Value {
        expectArgsRange("as_message", a, 1, 3, line);
        if (isMessageValue(a[0])) return a[0];
        if (a[0].type == Value::Type::STRING)
            return makeMessage("info", a[0].str, Value::nil(), Value::nil());
        // خطأ ملتقط: {message, code, line}
        if (a[0].type == Value::Type::MAP && a[0].map) {
            std::string body = "error";
            Value code = Value::nil();
            auto mit = a[0].map->find("message");
            if (mit != a[0].map->end()) body = valueToString(mit->second);
            auto cit = a[0].map->find("code");
            if (cit != a[0].map->end()) code = cit->second;
            return makeMessage("error", body, a[0], code);
        }
        return makeMessage("info", valueToString(a[0]), Value::nil(), Value::nil());
    };

    natives["as_error"] = [](Args& a, int line) -> Value {
        expectArgs("as_error", a, 1, line);
        Value m = isMessageValue(a[0]) ? a[0] : makeMessage("error", valueToString(a[0]), Value::nil(), Value::nil());
        // شكل متوافق مع catch: message / code / line / kind
        auto map = std::make_shared<std::map<std::string, Value>>();
        if (m.map) *map = *m.map;
        (*map)["message"] = (*map)["body"];
        if (!map->count("code") || (*map)["code"].type == Value::Type::NIL)
            (*map)["code"] = Value::string("E_MESSAGE");
        return Value::map(map);
    };

    // banner(m) — يختار حسب المستوى (يعتمد على وجود bannerInfo/... الأصلي)
    natives["banner"] = [&interp](Args& a, int line) -> Value {
        expectArgs("banner", a, 1, line);
        Value m = a[0];
        if (!isMessageValue(m)) m = makeMessage("info", valueToString(m), Value::nil(), Value::nil());
        std::string level = levelOf(m);
        std::string body = valueToString((*m.map)["body"]);
        auto cit = m.map->find("code");
        if (cit != m.map->end() && cit->second.type == Value::Type::STRING && !cit->second.str.empty())
            body = "[" + cit->second.str + "] " + body;

        std::string fn = "bannerInfo";
        if (level == "success") fn = "bannerSuccess";
        else if (level == "warning" || level == "warn") fn = "bannerWarning";
        else if (level == "error") fn = "bannerError";

        auto it = interp.natives.find(fn);
        if (it != interp.natives.end()) {
            Args args = { Value::string(body) };
            return it->second(args, line);
        }
        // إن لم توجد دوال banner، اطبع فقط
        return Value::nil();
    };

    natives["emit"] = [](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0004_InvalidType, line, "'emit' expects a message");
        Value m = isMessageValue(a[0]) ? a[0] : makeMessage("info", valueToString(a[0]), Value::nil(), Value::nil());
        if (rankOf(levelOf(m)) >= rankOf(g_minLevel))
            pushLog(m);
        return m;
    };

    natives["messages"] = [](Args& a, int line) -> Value {
        std::lock_guard<std::mutex> lock(g_msgMu);
        auto out = std::make_shared<std::vector<Value>>();
        std::string filterLevel;
        if (!a.empty() && a[0].type == Value::Type::MAP && a[0].map) {
            auto it = a[0].map->find("level");
            if (it != a[0].map->end() && it->second.type == Value::Type::STRING)
                filterLevel = it->second.str;
        }
        for (auto& m : g_msgLog) {
            if (!filterLevel.empty() && levelOf(m) != filterLevel) continue;
            out->push_back(m);
        }
        return Value::array(out);
    };

    natives["clear_messages"] = [](Args&, int) -> Value {
        std::lock_guard<std::mutex> lock(g_msgMu);
        g_msgLog.clear();
        return Value::nil();
    };

    natives["message_min_level"] = [](Args& a, int line) -> Value {
        expectArgs("message_min_level", a, 1, line);
        g_minLevel = valueToString(a[0]);
        return Value::nil();
    };

    natives["message_style"] = [](Args& a, int line) -> Value {
        expectArgs("message_style", a, 1, line);
        g_style = valueToString(a[0]);
        return Value::nil();
    };

    natives["message_log_limit"] = [](Args& a, int line) -> Value {
        expectArgs("message_log_limit", a, 1, line);
        double n = asNumber(a[0], "message_log_limit", line);
        if (n < 1) n = 1;
        g_msgLimit = static_cast<size_t>(n);
        return Value::nil();
    };

    // تنسيق جميل للاستخدام من مسار print إن رُبط لاحقاً
    natives["__message_format"] = [](Args& a, int line) -> Value {
        expectArgs("__message_format", a, 1, line);
        if (!isMessageValue(a[0]) || !a[0].map) return Value::string(valueToString(a[0]));
        if (g_style == "plain") {
            return (*a[0].map)["body"];
        }
        std::string level = levelOf(a[0]);
        std::string body = valueToString((*a[0].map)["body"]);
        std::string code;
        auto cit = a[0].map->find("code");
        if (cit != a[0].map->end() && cit->second.type == Value::Type::STRING && !cit->second.str.empty())
            code = " [" + cit->second.str + "]";
        std::string s = iconOf(level) + "  [" + level + "]" + code + " " + body;
        return Value::string(s);
    };
}
