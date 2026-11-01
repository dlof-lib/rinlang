// rin_abi.h — ترويسة مساعدة لكتابة دوال C++ تُستدعى من Rin (cpp.call / cpp.exec).
// ---------------------------------------------------------------------------
//  نمط 1 (الأسهل): دوال JSON — تستقبل وسائط Rin كمصفوفة JSON وتُرجع أي قيمة:
//
//      #include "rin_abi.h"
//      RIN_FN(sumAll) {                       // args = مصفوفة الوسائط كما مرّرها Rin
//          double t = 0;
//          for (auto& v : args.arr) t += v.num;
//          return rin::Json::number(t);
//      }
//
//      من Rin:   let h = cpp.lib(src).handle;  print cpp.call(h, "sumAll", [1,2,3]).value;
//
//  نمط 2 (الأسرع): دوال رقمية C عادية، كلها double أو كلها int64:
//
//      extern "C" double hyp(double a, double b) { return std::sqrt(a*a + b*b); }
//      من Rin:   cpp.callNum(h, "hyp", [3, 4])   // => 5
// ---------------------------------------------------------------------------
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace rin {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // يحفظ ترتيب الإدخال

    static Json null() { return Json(); }
    static Json boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
    static Json number(double v) { Json j; j.type = Num; j.num = v; return j; }
    static Json string(const std::string& v) { Json j; j.type = Str; j.str = v; return j; }
    static Json array() { Json j; j.type = Arr; return j; }
    static Json object() { Json j; j.type = Obj; return j; }

    Json& push(const Json& v) { if (type != Arr) { type = Arr; arr.clear(); } arr.push_back(v); return *this; }
    Json& set(const std::string& k, const Json& v) {
        if (type != Obj) { type = Obj; obj.clear(); }
        for (auto& kv : obj) if (kv.first == k) { kv.second = v; return *this; }
        obj.push_back({k, v});
        return *this;
    }
    const Json* get(const std::string& k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    double numOr(const std::string& k, double d) const { auto* p = get(k); return p && p->type == Num ? p->num : d; }
    std::string strOr(const std::string& k, const std::string& d) const { auto* p = get(k); return p && p->type == Str ? p->str : d; }

    // ------------------------------------------------------------ dump
    static void esc(const std::string& s, std::string& o) {
        o += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); o += buf; }
                    else o += (char)c;
            }
        }
        o += '"';
    }
    std::string dump() const { std::string o; dumpTo(o); return o; }
    void dumpTo(std::string& o) const {
        switch (type) {
            case Null: o += "null"; break;
            case Bool: o += b ? "true" : "false"; break;
            case Num: {
                if (!std::isfinite(num)) { o += "null"; break; }
                char buf[40];
                if (num == std::floor(num) && std::fabs(num) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", num);
                else std::snprintf(buf, sizeof buf, "%.17g", num);
                o += buf;
                break;
            }
            case Str: esc(str, o); break;
            case Arr:
                o += '[';
                for (size_t i = 0; i < arr.size(); ++i) { if (i) o += ','; arr[i].dumpTo(o); }
                o += ']';
                break;
            case Obj:
                o += '{';
                for (size_t i = 0; i < obj.size(); ++i) { if (i) o += ','; esc(obj[i].first, o); o += ':'; obj[i].second.dumpTo(o); }
                o += '}';
                break;
        }
    }

    // ------------------------------------------------------------ parse
    static Json parse(const std::string& s) { size_t p = 0; Json j = val(s, p); return j; }

private:
    static void ws(const std::string& s, size_t& p) { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\t' || s[p] == '\r')) ++p; }
    static void utf8(unsigned cp, std::string& o) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    static std::string strLit(const std::string& s, size_t& p) {
        std::string o; ++p;   // "
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                ++p;
                switch (s[p]) {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                    case 'u': {
                        unsigned cp = (unsigned)std::strtoul(s.substr(p + 1, 4).c_str(), nullptr, 16); p += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && s.compare(p + 1, 2, "\\u") == 0) {
                            unsigned lo = (unsigned)std::strtoul(s.substr(p + 3, 4).c_str(), nullptr, 16);
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6;
                        }
                        utf8(cp, o); break;
                    }
                    default: o += s[p];
                }
                ++p;
            } else o += s[p++];
        }
        ++p;   // "
        return o;
    }
    static Json val(const std::string& s, size_t& p) {
        ws(s, p);
        if (p >= s.size()) return Json();
        char c = s[p];
        if (c == '{') {
            Json j = object(); ++p; ws(s, p);
            if (p < s.size() && s[p] == '}') { ++p; return j; }
            while (p < s.size()) {
                ws(s, p); std::string k = strLit(s, p); ws(s, p);
                if (p < s.size() && s[p] == ':') ++p;
                j.obj.push_back({k, val(s, p)}); ws(s, p);
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                break;
            }
            if (p < s.size() && s[p] == '}') ++p;
            return j;
        }
        if (c == '[') {
            Json j = array(); ++p; ws(s, p);
            if (p < s.size() && s[p] == ']') { ++p; return j; }
            while (p < s.size()) {
                j.arr.push_back(val(s, p)); ws(s, p);
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                break;
            }
            if (p < s.size() && s[p] == ']') ++p;
            return j;
        }
        if (c == '"') return string(strLit(s, p));
        if (s.compare(p, 4, "true") == 0) { p += 4; return boolean(true); }
        if (s.compare(p, 5, "false") == 0) { p += 5; return boolean(false); }
        if (s.compare(p, 4, "null") == 0) { p += 4; return Json(); }
        char* end = nullptr;
        double d = std::strtod(s.c_str() + p, &end);
        p = (size_t)(end - s.c_str());
        return number(d);
    }
};

// مخزن مؤقت لنص النتيجة (صالح حتى الاستدعاء التالي في نفس الخيط).
inline const char* keep(const std::string& s) {
    static thread_local std::string buf;
    buf = s;
    return buf.c_str();
}

}  // namespace rin

// يعرّف دالة JSON مُصدَّرة:  RIN_FN(name) { ... return rin::Json::...; }
// المتغيّر args (من نوع rin::Json، مصفوفة) متاح داخل الجسم.
#define RIN_FN(name)                                                                     \
    static rin::Json name##_impl(const rin::Json& args);                                 \
    extern "C" __attribute__((visibility("default"))) const char* name(const char* a) { \
        return rin::keep(name##_impl(rin::Json::parse(a ? a : "[]")).dump());            \
    }                                                                                    \
    static rin::Json name##_impl(const rin::Json& args)
