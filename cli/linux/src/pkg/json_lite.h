// cli/linux/src/pkg/json_lite.h
// ============================================================================
// RinPM :: محلّل/مُرمِّز JSON صغير ومستقل تماماً عن قيمة Value الخاصة بمفسّر
// Rin (تلك مرتبطة بدلالات اللغة نفسها: خرائط كمصفوفات أزواج، دوال، إلخ، وليست
// مناسبة لبروتوكول HTTP بسيط بين RinPM وخادم Registry). هذا الملف هو ما
// يستخدمه registry.cpp فعلياً عند التحدث مع HttpRegistry.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace rinpm::json {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    static JsonValue makeString(const std::string& s) { JsonValue v; v.type = Type::String; v.str = s; return v; }
    static JsonValue makeObject() { JsonValue v; v.type = Type::Object; return v; }
    static JsonValue makeArray() { JsonValue v; v.type = Type::Array; return v; }

    std::vector<std::string> stringArray(const std::string& key) const {
        std::vector<std::string> out;
        auto it = object.find(key);
        if (it != object.end() && it->second.type == Type::Array)
            for (auto& e : it->second.array) if (e.type == Type::String) out.push_back(e.str);
        return out;
    }
    std::string getStr(const std::string& key, const std::string& def = "") const {
        auto it = object.find(key);
        if (it != object.end() && it->second.type == Type::String) return it->second.str;
        return def;
    }
};

// يرمي std::runtime_error برسالة موجزة عند JSON غير صالح.
JsonValue parse(const std::string& text);
std::string encode(const JsonValue& v);

} // namespace rinpm::json
