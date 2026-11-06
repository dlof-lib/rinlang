// rin_datatypes.h — محرّك أنواع البيانات المشترك (docs/data_types.md)
// يُستعمل من containerType (حقول الأنواع المعرَّفة) ومن الحاويات العادية (setFieldType) ومن isType().
// نص النوع (spec) يُحلَّل مرة واحدة إلى DT ثم يُفحَص به أي Value. لا يعتمد على المفسّر نفسه.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <memory>
#include <functional>
#include <sstream>

namespace rin {
namespace dt {

enum class Kind {
    Any, Nil, Number, Int, UInt, Float, String, Char, Bool, Array, Map, Set, Function,
    Email, Url, Uuid, Date, Time, DateTime, Phone, Color, Ip,
    Enum, Union
};

struct DT {
    Kind kind = Kind::Any;
    bool optional = false;                 // T?  (يقبل nil)
    std::vector<std::string> enumVals;     // enum(a,b,c)
    std::vector<DT> parts;                 // Union: البدائل | Array/Set: [عنصر] | Map: [مفتاح، قيمة]
    bool hasElem = false;                  // هل حُدِّد نوع العناصر (array<T>)
    std::string canon;                     // الشكل القياسي للنص (يُعرض في containerTypeInfo)
};

inline std::string fmtNum(double n) {
    std::ostringstream ss;
    if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) ss << (long long)n; else ss << n;
    return ss.str();
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// يقسم على المحرف sep خارج الأقواس <> () فقط
inline std::vector<std::string> splitTop(const std::string& s, char sep) {
    std::vector<std::string> out; int depth = 0; std::string cur;
    for (char c : s) {
        if (c == '<' || c == '(') depth++;
        else if (c == '>' || c == ')') depth--;
        if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// يعيد false مع err إن كان النص ليس نوعاً صالحاً
inline bool parse(const std::string& specIn, DT& out, std::string& err) {
    std::string s = trim(specIn);
    if (s.empty()) { err = "نوع فارغ"; return false; }
    // اتحاد A|B
    auto alts = splitTop(s, '|');
    if (alts.size() > 1) {
        DT u; u.kind = Kind::Union;
        for (auto& p : alts) {
            DT d; if (!parse(p, d, err)) return false;
            if (d.optional) u.optional = true;
            u.parts.push_back(std::move(d));
        }
        u.canon = "";
        for (size_t i = 0; i < u.parts.size(); i++) u.canon += (i ? "|" : "") + u.parts[i].canon;
        out = std::move(u); return true;
    }
    bool opt = false;
    if (s.back() == '?') { opt = true; s.pop_back(); s = trim(s); }
    std::string low; for (char c : s) low += (char)std::tolower((unsigned char)c);
    DT d; d.optional = opt;
    // enum(a,b,c)
    if (low.rfind("enum(", 0) == 0 && s.back() == ')') {
        d.kind = Kind::Enum;
        std::string body = s.substr(5, s.size() - 6);
        for (auto& p : splitTop(body, ',')) {
            std::string v = trim(p);
            if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) v = v.substr(1, v.size() - 2);
            if (!v.empty()) d.enumVals.push_back(v);
        }
        if (d.enumVals.empty()) { err = "enum بلا قيم"; return false; }
        d.canon = "enum(";
        for (size_t i = 0; i < d.enumVals.size(); i++) d.canon += (i ? "," : "") + d.enumVals[i];
        d.canon += ")"; if (opt) d.canon += "?";
        out = std::move(d); return true;
    }
    // generics: array<T> set<T> map<V> map<K,V>
    size_t lt = s.find('<');
    if (lt != std::string::npos && s.back() == '>') {
        std::string head; for (size_t i = 0; i < lt; i++) head += (char)std::tolower((unsigned char)s[i]);
        head = trim(head);
        std::string inner = s.substr(lt + 1, s.size() - lt - 2);
        auto args = splitTop(inner, ',');
        if (head == "array" || head == "list" || head == "set") {
            if (args.size() != 1) { err = head + "<T> يقبل نوعاً واحداً"; return false; }
            d.kind = head == "set" ? Kind::Set : Kind::Array;
            DT e; if (!parse(args[0], e, err)) return false;
            d.parts.push_back(std::move(e)); d.hasElem = true;
            d.canon = (head == "set" ? "set<" : "array<") + d.parts[0].canon + ">";
        } else if (head == "map" || head == "object") {
            if (args.size() < 1 || args.size() > 2) { err = "map<V> أو map<K,V>"; return false; }
            d.kind = Kind::Map;
            DT k, v;
            if (args.size() == 2) { if (!parse(args[0], k, err) || !parse(args[1], v, err)) return false; }
            else { k.kind = Kind::Any; k.canon = "any"; if (!parse(args[0], v, err)) return false; }
            d.parts.push_back(std::move(k)); d.parts.push_back(std::move(v)); d.hasElem = true;
            d.canon = "map<" + d.parts[0].canon + "," + d.parts[1].canon + ">";
        } else { err = "نوع عام غير معروف '" + head + "'"; return false; }
        if (opt) d.canon += "?";
        out = std::move(d); return true;
    }
    struct N { const char* n; Kind k; const char* canon; };
    static const N names[] = {
        {"any", Kind::Any, "any"}, {"nil", Kind::Nil, "nil"}, {"null", Kind::Nil, "nil"},
        {"number", Kind::Number, "number"}, {"num", Kind::Number, "number"},
        {"int", Kind::Int, "int"}, {"integer", Kind::Int, "int"}, {"uint", Kind::UInt, "uint"},
        {"float", Kind::Float, "float"}, {"double", Kind::Float, "float"}, {"decimal", Kind::Float, "float"},
        {"string", Kind::String, "string"}, {"text", Kind::String, "string"}, {"str", Kind::String, "string"},
        {"char", Kind::Char, "char"},
        {"bool", Kind::Bool, "bool"}, {"boolean", Kind::Bool, "bool"},
        {"array", Kind::Array, "array"}, {"list", Kind::Array, "array"},
        {"map", Kind::Map, "map"}, {"object", Kind::Map, "map"}, {"set", Kind::Set, "set"},
        {"function", Kind::Function, "function"}, {"fun", Kind::Function, "function"},
        {"email", Kind::Email, "email"}, {"url", Kind::Url, "url"}, {"uuid", Kind::Uuid, "uuid"},
        {"date", Kind::Date, "date"}, {"time", Kind::Time, "time"}, {"datetime", Kind::DateTime, "datetime"},
        {"phone", Kind::Phone, "phone"}, {"color", Kind::Color, "color"}, {"ip", Kind::Ip, "ip"},
    };
    for (auto& n : names) if (low == n.n) {
        d.kind = n.k; d.canon = n.canon; if (opt) d.canon += "?";
        out = std::move(d); return true;
    }
    err = "نوع بيانات غير معروف '" + s + "'";
    return false;
}

// هل النص اسم نوع معروف (للتمييز بين «نوع» و«قيمة افتراضية» في مواصفة الحقل)؟
inline bool isSpec(const std::string& s) { DT d; std::string e; return parse(s, d, e); }

// ---- فحوص التنسيقات النصية ----
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isHex(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

inline bool validDate(const std::string& s) {        // YYYY-MM-DD مع فحص الشهر/اليوم/الكبيس
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) if (!isDigit(s[i])) return false;
    int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    if (m < 1 || m > 12 || d < 1) return false;
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return d <= dim[m - 1] + (m == 2 && leap ? 1 : 0);
}
inline bool validTime(const std::string& s) {        // HH:MM أو HH:MM:SS (اختيارياً .mmm)
    size_t n = s.size();
    if (n < 5 || s[2] != ':') return false;
    for (int i : {0, 1, 3, 4}) if (!isDigit(s[i])) return false;
    int h = std::stoi(s.substr(0, 2)), mi = std::stoi(s.substr(3, 2));
    if (h > 23 || mi > 59) return false;
    if (n == 5) return true;
    if (n < 8 || s[5] != ':' || !isDigit(s[6]) || !isDigit(s[7])) return false;
    if (std::stoi(s.substr(6, 2)) > 59) return false;
    if (n == 8) return true;
    if (s[8] != '.' || n == 9) return false;
    for (size_t i = 9; i < n; i++) if (!isDigit(s[i])) return false;
    return true;
}
inline bool validDateTime(const std::string& s) {    // YYYY-MM-DDTHH:MM[:SS[.mmm]][Z|±HH:MM]
    size_t t = s.find_first_of("T ");
    if (t != 10) return false;
    std::string tm = s.substr(11);
    if (!tm.empty() && tm.back() == 'Z') tm.pop_back();
    else {
        size_t z = tm.find_first_of("+-");
        if (z != std::string::npos) {
            std::string off = tm.substr(z + 1); tm = tm.substr(0, z);
            if (!validTime(off.size() == 5 ? off : "99:99")) return false;
        }
    }
    return validDate(s.substr(0, 10)) && validTime(tm);
}
inline bool validEmail(const std::string& s) {
    size_t at = s.find('@');
    if (at == std::string::npos || at == 0 || at != s.rfind('@') || at + 1 >= s.size()) return false;
    std::string dom = s.substr(at + 1);
    size_t dot = dom.find('.');
    if (dot == std::string::npos || dot == 0 || dom.back() == '.' || dom.find("..") != std::string::npos) return false;
    for (char c : s) if (std::isspace((unsigned char)c) || c == ',' || c == ';') return false;
    return true;
}
inline bool validUrl(const std::string& s) {
    size_t p = s.find("://");
    if (p == std::string::npos || p == 0) return false;
    for (size_t i = 0; i < p; i++) if (!std::isalnum((unsigned char)s[i]) && s[i] != '+' && s[i] != '-' && s[i] != '.') return false;
    if (p + 3 >= s.size()) return false;
    for (char c : s) if (std::isspace((unsigned char)c)) return false;
    return true;
}
inline bool validUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return false; }
        else if (!isHex(s[i])) return false;
    }
    return true;
}
inline bool validPhone(const std::string& s) {
    int digits = 0; size_t i = 0;
    if (!s.empty() && s[0] == '+') i = 1;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (isDigit(c)) digits++;
        else if (c != ' ' && c != '-' && c != '(' && c != ')' && c != '.') return false;
    }
    return digits >= 7 && digits <= 15;
}
inline bool validColor(const std::string& s) {
    if (s.size() < 2 || s[0] != '#') return false;
    size_t n = s.size() - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    for (size_t i = 1; i < s.size(); i++) if (!isHex(s[i])) return false;
    return true;
}
inline bool validIp(const std::string& s) {
    if (s.find(':') != std::string::npos) {         // IPv6 مبسّط
        if (s.size() < 2 || s.size() > 45) return false;
        int groups = 0, cur = 0;
        for (char c : s) {
            if (c == ':') { if (cur > 4) return false; cur = 0; groups++; }
            else if (isHex(c)) cur++;
            else return false;
        }
        return cur <= 4 && groups >= 2 && groups <= 7;
    }
    int parts = 0; size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('.', i); if (j == std::string::npos) j = s.size();
        std::string p = s.substr(i, j - i);
        if (p.empty() || p.size() > 3) return false;
        for (char c : p) if (!isDigit(c)) return false;
        if (std::stoi(p) > 255) return false;
        parts++; i = j + 1;
        if (j == s.size()) break;
    }
    return parts == 4;
}
inline size_t utf8Len(const std::string& s) {
    size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}

// واجهة مجرّدة على Value كي لا يعتمد الملف على rin_interpreter.h
struct Probe {
    // تعيد اسم نوع Value الظاهر: nil/number/string/bool/function/array/map/set/instance
    std::function<std::string()> typeName;
    std::function<double()> number;
    std::function<std::string()> str;
    std::function<bool()> boolean;
    // عناصر المصفوفة/المجموعة
    std::function<std::vector<Probe>()> items;
    // أزواج الخريطة
    std::function<std::vector<std::pair<Probe, Probe>>()> pairs;
};

// يعيد "" إن طابق، وإلا سبب الرفض (مع مسار العنصر داخل المركّبات)
inline std::string check(const DT& d, const Probe& v, const std::string& path = "") {
    const std::string tn = v.typeName();
    auto bad = [&](const std::string& expect) {
        return (path.empty() ? std::string() : "[" + path + "] ") + "المتوقع " + expect + " والموجود " + tn;
    };
    if (d.kind == Kind::Union) {
        if (tn == "nil") return d.optional ? "" : bad(d.canon);
        for (auto& p : d.parts) if (check(p, v, path).empty()) return "";
        return bad(d.canon);
    }
    if (tn == "nil") return (d.optional || d.kind == Kind::Any || d.kind == Kind::Nil) ? "" : bad(d.canon);
    switch (d.kind) {
        case Kind::Any: return "";
        case Kind::Nil: return bad("nil");
        case Kind::Number:               // number: أي رقم (سلوك containerType القديم محفوظ)
            return tn == "number" ? "" : bad("number");
        case Kind::Float:                // float: رقم منتهٍ (ليس NaN/inf)
            if (tn != "number") return bad("float");
            if (!std::isfinite(v.number())) return bad("رقم منتهٍ (ليس NaN/inf)");
            return "";
        case Kind::Int: case Kind::UInt: {
            if (tn != "number") return bad(d.canon);
            double n = v.number();
            const std::string pre = path.empty() ? std::string() : "[" + path + "] ";
            if (!std::isfinite(n) || n != std::floor(n)) return pre + "القيمة " + fmtNum(n) + " ليست " + d.canon + " (عدد صحيح)";
            if (d.kind == Kind::UInt && n < 0) return pre + "القيمة " + fmtNum(n) + " ليست uint (صحيح غير سالب)";
            return "";
        }
        case Kind::Bool: return tn == "bool" ? "" : bad("bool");
        case Kind::Function: return tn == "function" ? "" : bad("function");
        case Kind::String: return tn == "string" ? "" : bad("string");
        case Kind::Char:
            if (tn != "string") return bad("char");
            return utf8Len(v.str()) == 1 ? "" : bad("char (محرف واحد)");
        case Kind::Email: case Kind::Url: case Kind::Uuid: case Kind::Date: case Kind::Time:
        case Kind::DateTime: case Kind::Phone: case Kind::Color: case Kind::Ip: {
            if (tn != "string") return bad(d.canon);
            std::string s = v.str(); bool ok = false;
            switch (d.kind) {
                case Kind::Email: ok = validEmail(s); break;
                case Kind::Url: ok = validUrl(s); break;
                case Kind::Uuid: ok = validUuid(s); break;
                case Kind::Date: ok = validDate(s); break;
                case Kind::Time: ok = validTime(s); break;
                case Kind::DateTime: ok = validDateTime(s); break;
                case Kind::Phone: ok = validPhone(s); break;
                case Kind::Color: ok = validColor(s); break;
                default: ok = validIp(s); break;
            }
            std::string nm = d.canon; if (!nm.empty() && nm.back() == '?') nm.pop_back();
            return ok ? "" : (path.empty() ? std::string() : "[" + path + "] ") + "قيمة '" + s + "' ليست " + nm + " صالحاً";
        }
        case Kind::Enum: {
            std::string s = tn == "string" ? v.str() : (tn == "number" ? std::string() : std::string());
            bool ok = false;
            if (tn == "string") { for (auto& e : d.enumVals) if (e == s) ok = true; }
            else if (tn == "number") {
                double n = v.number();
                for (auto& e : d.enumVals) { try { size_t pos; double x = std::stod(e, &pos); if (pos == e.size() && x == n) ok = true; } catch (...) {} }
            } else if (tn == "bool") { for (auto& e : d.enumVals) if (e == (v.boolean() ? "true" : "false")) ok = true; }
            if (ok) return "";
            std::string opts; for (auto& e : d.enumVals) opts += (opts.empty() ? "" : " | ") + e;
            return (path.empty() ? std::string() : "[" + path + "] ") + "قيمة غير مسموحة (المتاح: " + opts + ")";
        }
        case Kind::Array: case Kind::Set: {
            const char* want = d.kind == Kind::Array ? "array" : "set";
            if (tn != want) return bad(d.canon);
            if (!d.hasElem) return "";
            auto its = v.items(); size_t i = 0;
            for (auto& it : its) {
                std::string r = check(d.parts[0], it, path + (path.empty() ? "" : ".") + std::to_string(i));
                if (!r.empty()) return r;
                i++;
            }
            return "";
        }
        case Kind::Map: {
            if (tn != "map") return bad(d.canon);
            if (!d.hasElem) return "";
            for (auto& kv : v.pairs()) {
                std::string kname = kv.first.typeName() == "string" ? kv.first.str() : "?";
                std::string r = check(d.parts[0], kv.first, path + (path.empty() ? "" : ".") + kname + "(مفتاح)");
                if (!r.empty()) return r;
                r = check(d.parts[1], kv.second, path + (path.empty() ? "" : ".") + kname);
                if (!r.empty()) return r;
            }
            return "";
        }
        default: return "";
    }
}

} // namespace dt
} // namespace rin
