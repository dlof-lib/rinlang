// ============================================================================
//  rin_env.cpp — ربط Rin بملفات .env (متغيّرات البيئة)
// ----------------------------------------------------------------------------
//    Env.load()                  يقرأ ".env" (بجانب السكربت/المشروع) ويضبط متغيّرات العملية
//    Env.load(path, override?)   مسار صريح (أو مصفوفة مسارات: اللاحق يغلب السابق) · override=false افتراضياً
//    Env.parse(text)             يحلّل نص .env إلى قاموس بلا أي أثر جانبي
//    Env.get(key, default?)      قيمة نصية أو default (nil افتراضياً)
//    Env.require(key)            كـ get لكن يرمي خطأ واضحاً إن لم يوجد
//    Env.has(key) · Env.set(key, value) · Env.unset(key) · Env.all() · Env.loaded()
//    Env.int / Env.float / Env.bool / Env.list (key, default?) — تحويل مكتوب
//
//  الصيغة المدعومة: KEY=VALUE · export KEY=VALUE · تعليقات # · "..." (هروب \n \t \" \\ \$ وأسطر متعددة)
//  · '...' (حرفي) · غير مقتبس (يُقصّ، وتعليق ' #' في آخره) · توسعة ${VAR} و $VAR و ${VAR:-افتراضي}
//  (من مفاتيح سابقة في الملف ثم من بيئة العملية). تُضمَّن (#include) في نهاية rin_interpreter.cpp.
//  الشرح الكامل: docs/env.md
// ============================================================================
#include "rin_interpreter.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#ifdef _WIN32
#include <stdlib.h>
#define RIN_ENVIRON _environ
#else
extern char** environ;
#define RIN_ENVIRON environ
#endif

namespace rin {
namespace envimpl {

using Args = std::vector<Value>;
using Pairs = std::vector<std::pair<std::string, std::string>>;

static void setProcessEnv(const std::string& k, const std::string& v) {
#ifdef _WIN32
    _putenv_s(k.c_str(), v.c_str());
#else
    ::setenv(k.c_str(), v.c_str(), 1);
#endif
}
static void unsetProcessEnv(const std::string& k) {
#ifdef _WIN32
    _putenv_s(k.c_str(), "");
#else
    ::unsetenv(k.c_str());
#endif
}

static bool validKeyChar(char c, bool first) {
    return std::isalpha((unsigned char)c) || c == '_' || (!first && (std::isdigit((unsigned char)c) || c == '.' || c == '-'));
}

static const std::string* findLocal(const Pairs& p, const std::string& k) {
    for (auto it = p.rbegin(); it != p.rend(); ++it) if (it->first == k) return &it->second;
    return nullptr;
}

// توسعة $VAR و ${VAR} و ${VAR:-def} في نص (مفاتيح الملف السابقة أولاً ثم بيئة العملية)
static std::string expand(const std::string& s, const Pairs& local) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '$' || i + 1 >= s.size()) { out += s[i]; continue; }
        std::string name, def; bool hasDef = false; size_t j = i + 1;
        if (s[j] == '{') {
            size_t close = s.find('}', j);
            if (close == std::string::npos) { out += s[i]; continue; }
            std::string inner = s.substr(j + 1, close - j - 1);
            size_t d = inner.find(":-");
            if (d != std::string::npos) { name = inner.substr(0, d); def = inner.substr(d + 2); hasDef = true; }
            else name = inner;
            j = close + 1;
        } else if (validKeyChar(s[j], true)) {
            size_t k = j;
            while (k < s.size() && (std::isalnum((unsigned char)s[k]) || s[k] == '_')) ++k;
            name = s.substr(j, k - j); j = k;
        } else { out += s[i]; continue; }
        std::string val; bool found = false;
        if (const std::string* l = findLocal(local, name)) { val = *l; found = true; }
        else if (const char* e = std::getenv(name.c_str())) { val = e; found = true; }
        if (!found || (hasDef && val.empty())) val = hasDef ? def : "";
        out += val;
        i = j - 1;
    }
    return out;
}

// يحلّل نص .env. يتجاهل الأسطر غير الصالحة بصمت (سلوك dotenv المعتاد).
static Pairs parseEnv(std::string text) {
    Pairs out;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    size_t i = 0, n = text.size();
    auto skipLine = [&]() { while (i < n && text[i] != '\n') ++i; if (i < n) ++i; };
    while (i < n) {
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) ++i;
        if (i >= n) break;
        if (text[i] == '\n') { ++i; continue; }
        if (text[i] == '#') { skipLine(); continue; }
        if (text.compare(i, 7, "export ") == 0 || text.compare(i, 7, "export\t") == 0) {
            i += 7; while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
        }
        size_t ks = i;
        while (i < n && validKeyChar(text[i], i == ks)) ++i;
        std::string key = text.substr(ks, i - ks);
        while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (key.empty() || i >= n || (text[i] != '=' && text[i] != ':')) { skipLine(); continue; }
        ++i;
        while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
        std::string val;
        if (i < n && text[i] == '"') {
            ++i;
            std::string raw; bool closed = false;
            while (i < n) {
                char c = text[i];
                if (c == '\\' && i + 1 < n) {
                    char e = text[i + 1]; i += 2;
                    switch (e) {
                        case 'n': raw += '\n'; break; case 't': raw += '\t'; break; case 'r': raw += '\r'; break;
                        case '"': raw += '"'; break; case '\\': raw += '\\'; break;
                        case '$': raw += "\x01"; break;   // $ حرفي: يُحمى من التوسعة
                        default: raw += '\\'; raw += e;
                    }
                    continue;
                }
                if (c == '"') { ++i; closed = true; break; }
                raw += c; ++i;
            }
            if (!closed) { skipLine(); continue; }
            val = expand(raw, out);
            std::replace(val.begin(), val.end(), '\x01', '$');
            skipLine();
        } else if (i < n && text[i] == '\'') {
            ++i; size_t e = text.find('\'', i);
            if (e == std::string::npos) { skipLine(); continue; }
            val = text.substr(i, e - i); i = e + 1; skipLine();
        } else {
            size_t ls = i; while (i < n && text[i] != '\n') ++i;
            std::string raw = text.substr(ls, i - ls);
            for (size_t p = 1; p < raw.size(); ++p)   // تعليق ' #' (مسبوق بمسافة)
                if (raw[p] == '#' && (raw[p - 1] == ' ' || raw[p - 1] == '\t')) { raw.erase(p); break; }
            while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r')) raw.pop_back();
            val = expand(raw, out);
            if (i < n) ++i;
        }
        out.emplace_back(key, val);
    }
    // آخر تعريف لكل مفتاح يغلب، بترتيب أول ظهور
    Pairs uniq;
    for (auto& kv : out) {
        bool dup = false;
        for (auto& u : uniq) if (u.first == kv.first) { u.second = kv.second; dup = true; break; }
        if (!dup) uniq.push_back(kv);
    }
    return uniq;
}

static Value toMap(const Pairs& p) {
    auto m = std::make_shared<MapData>();
    for (auto& kv : p) m->emplace_back(Value::string(kv.first), Value::string(kv.second));
    return Value::makeMap(m);
}

static void argc(const std::string& fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi)
        throw diagErr(diag::Code::E0007_InvalidArguments, line,
                      "'" + fn + "' expects " + std::to_string(lo) + (hi != lo ? " to " + std::to_string(hi) : "") +
                      " argument(s) but got " + std::to_string(a.size()));
}
static std::string needStr(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::STRING)
        throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": expected a string but got " + v.typeName());
    return v.str;
}
static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}
static std::set<std::string>& loadedKeys() { static std::set<std::string> s; return s; }

}  // namespace envimpl

void Interpreter::registerNativesEnv() {
    using namespace envimpl;

    natives["Env.parse"] = [](Args& a, int line) -> Value {
        argc("Env.parse", a, 1, 1, line);
        return toMap(parseEnv(needStr(a[0], "Env.parse", line)));
    };

    natives["Env.load"] = [this](Args& a, int line) -> Value {
        argc("Env.load", a, 0, 2, line);
        std::vector<std::string> paths; bool explicitPath = !a.empty() && a[0].type != Value::Type::NIL;
        if (!explicitPath) paths.push_back(".env");
        else if (a[0].type == Value::Type::ARRAY) { for (auto& v : *a[0].array) paths.push_back(needStr(v, "Env.load", line)); }
        else paths.push_back(needStr(a[0], "Env.load", line));
        bool override_ = a.size() > 1 && a[1].isTruthy();
        Pairs merged;
        for (auto& p : paths) {
            std::ifstream in(resolvePath(p, line), std::ios::binary);
            if (!in) {
                if (!explicitPath) continue;   // .env الافتراضي اختياري
                throw diagErr(diag::Code::E0036_IOFailure, line, "Env.load: تعذّر فتح الملف '" + p + "' (غير موجود؟)");
            }
            std::ostringstream buf; buf << in.rdbuf();
            for (auto& kv : parseEnv(buf.str())) {
                bool dup = false;
                for (auto& m : merged) if (m.first == kv.first) { m.second = kv.second; dup = true; break; }
                if (!dup) merged.push_back(kv);
            }
        }
        Pairs applied;
        for (auto& kv : merged) {
            bool existing = std::getenv(kv.first.c_str()) != nullptr && loadedKeys().count(kv.first) == 0;
            if (existing && !override_) continue;   // متغيّر النظام الحقيقي يغلب الملف ما لم override
            setProcessEnv(kv.first, kv.second);
            loadedKeys().insert(kv.first);
            applied.push_back(kv);
        }
        return toMap(applied);
    };

    natives["Env.get"] = [](Args& a, int line) -> Value {
        argc("Env.get", a, 1, 2, line);
        const char* e = std::getenv(needStr(a[0], "Env.get", line).c_str());
        if (e) return Value::string(e);
        return a.size() > 1 ? a[1] : Value::nil();
    };
    natives["Env.require"] = [](Args& a, int line) -> Value {
        argc("Env.require", a, 1, 1, line);
        std::string k = needStr(a[0], "Env.require", line);
        const char* e = std::getenv(k.c_str());
        if (!e) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Env.require: المتغيّر '" + k + "' غير معرَّف (أضِفه إلى .env أو بيئة النظام)");
        return Value::string(e);
    };
    natives["Env.has"] = [](Args& a, int line) -> Value {
        argc("Env.has", a, 1, 1, line);
        return Value::boolean_(std::getenv(needStr(a[0], "Env.has", line).c_str()) != nullptr);
    };
    natives["Env.set"] = [](Args& a, int line) -> Value {
        argc("Env.set", a, 2, 2, line);
        std::string v = a[1].type == Value::Type::STRING ? a[1].str : a[1].toDisplayString();
        std::string k = needStr(a[0], "Env.set", line);
        setProcessEnv(k, v); loadedKeys().erase(k);   // صار مضبوطاً يدوياً: لا يُستبدل بـ load بلا override
        return Value::string(v);
    };
    natives["Env.unset"] = [](Args& a, int line) -> Value {
        argc("Env.unset", a, 1, 1, line);
        std::string k = needStr(a[0], "Env.unset", line);
        bool had = std::getenv(k.c_str()) != nullptr;
        unsetProcessEnv(k); loadedKeys().erase(k);
        return Value::boolean_(had);
    };
    natives["Env.all"] = [](Args& a, int line) -> Value {
        argc("Env.all", a, 0, 0, line);
        Pairs p;
        for (char** e = RIN_ENVIRON; e && *e; ++e) {
            std::string s = *e; size_t eq = s.find('=');
            if (eq != std::string::npos && eq > 0) p.emplace_back(s.substr(0, eq), s.substr(eq + 1));
        }
        std::sort(p.begin(), p.end());
        return toMap(p);
    };
    natives["Env.loaded"] = [](Args& a, int line) -> Value {
        argc("Env.loaded", a, 0, 0, line);
        auto arr = std::make_shared<ArrayData>();
        for (auto& k : loadedKeys()) arr->push_back(Value::string(k));
        return Value::makeArray(arr);
    };

    // ---- تحويلات مكتوبة: نص غير صالح => خطأ واضح (لا قيمة صامتة خاطئة) ----
    natives["Env.int"] = [](Args& a, int line) -> Value {
        argc("Env.int", a, 1, 2, line);
        std::string k = needStr(a[0], "Env.int", line);
        const char* e = std::getenv(k.c_str());
        if (!e) return a.size() > 1 ? a[1] : Value::nil();
        std::string s = trim(e); char* end = nullptr; long long v = std::strtoll(s.c_str(), &end, 10);
        if (s.empty() || *end) throw diagErr(diag::Code::E0005_TypeMismatch, line, "Env.int: قيمة '" + k + "' ليست عدداً صحيحاً: " + s);
        return Value::num((double)v);
    };
    natives["Env.float"] = [](Args& a, int line) -> Value {
        argc("Env.float", a, 1, 2, line);
        std::string k = needStr(a[0], "Env.float", line);
        const char* e = std::getenv(k.c_str());
        if (!e) return a.size() > 1 ? a[1] : Value::nil();
        std::string s = trim(e); char* end = nullptr; double v = std::strtod(s.c_str(), &end);
        if (s.empty() || *end) throw diagErr(diag::Code::E0005_TypeMismatch, line, "Env.float: قيمة '" + k + "' ليست رقماً: " + s);
        return Value::num(v);
    };
    natives["Env.bool"] = [](Args& a, int line) -> Value {
        argc("Env.bool", a, 1, 2, line);
        std::string k = needStr(a[0], "Env.bool", line);
        const char* e = std::getenv(k.c_str());
        if (!e) return a.size() > 1 ? a[1] : Value::nil();
        std::string s = trim(e); std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        if (s == "1" || s == "true" || s == "yes" || s == "on" || s == "y") return Value::boolean_(true);
        if (s == "0" || s == "false" || s == "no" || s == "off" || s == "n" || s.empty()) return Value::boolean_(false);
        throw diagErr(diag::Code::E0005_TypeMismatch, line, "Env.bool: قيمة '" + k + "' ليست منطقية (true/false/1/0/yes/no/on/off): " + s);
    };
    natives["Env.list"] = [](Args& a, int line) -> Value {
        argc("Env.list", a, 1, 3, line);
        std::string k = needStr(a[0], "Env.list", line);
        std::string sep = a.size() > 1 && a[1].type == Value::Type::STRING ? a[1].str : ",";
        const char* e = std::getenv(k.c_str());
        auto arr = std::make_shared<ArrayData>();
        if (!e) return a.size() > 2 ? a[2] : Value::makeArray(arr);
        std::string s = e; size_t pos = 0;
        while (true) {
            size_t d = sep.empty() ? std::string::npos : s.find(sep, pos);
            std::string part = trim(s.substr(pos, d == std::string::npos ? std::string::npos : d - pos));
            if (!part.empty()) arr->push_back(Value::string(part));
            if (d == std::string::npos) break;
            pos = d + sep.size();
        }
        return Value::makeArray(arr);
    };
}

}  // namespace rin
