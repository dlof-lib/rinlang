// ============================================================================
//  rin_libdir.h — نظام الحزم في Rin: «توكن الأداة OG» (ملف واحد) و«المكتبات» (مجلدات).
// ----------------------------------------------------------------------------
//  التسمية الرسمية (v2):
//
//    توكن الأداة OG  (Tool Token OG)   ← ما كان يُسمّى سابقاً «مكتبة Rin» القديمة: ملف واحد فقط
//                                         <rinname>.og.rin   مثل  lib/math.og.rin
//    مكتبة           (Library)          ← الجديد: مجلد فيه ملفات ومجلدات فرعية
//                                         <name>.og.rin/   (التقسيم حر، كل أجزائها تُربط كأنها ملف واحد)
//
//  الاستيراد واحد للنوعين: @import "math";  — والمحرّك يميّز النوع تلقائياً (المجلد = مكتبة، الملف = توكن أداة).
//  إن وُجد الاثنان بالاسم نفسه فالمكتبة (المجلد) لها الأولوية.
//
//  بنية المكتبة:
//
//     math.og.rin/
//       disconom.json | .xml | .toml   ← وصف المكتبة (Disconom): الاسم، الإصدار، إصدار اللغة المطلوب،
//                                         التفاصيل، التبعيات، الأجزاء، الكود الأصلي…  (الأهم)
//       main.zpp                       ← الجزء الأساسي (يُحمَّل أولاً)
//       job.zpp / print.zpp / oop.zpp  ← أجزاء أخرى بأي اسم وفي أي مجلد فرعي (تقسيم حر)
//       run.rinz                       ← ملف تشغيل/تهيئة: يُنفَّذ بعد ربط كل الأجزاء
//       native/*.cpp                   ← كود C++ مساعد لكود Rin (اختياري، عبر جسر cpp.*)
//       data/*.json|xml                ← أي ملفات بيانات تقرأها المكتبة عبر rinlib.json/rinlib.xml
//
//  كل ملفات .zpp / .rinz هي ملفات نصية بلغة Rin نفسها (ليست أرشيفاً). تُربط كلها في نطاق واحد
//  كأنها ملف واحد، ثم يُطبَّق عليها نفس منطق @import/export المعتاد.
//
//  هذا الملف مستقلّ تماماً عن المفسّر (لا يُضمِّن rin_interpreter.h): فيه محلّل JSON/XML صغير، قارئ
//  Disconom، مطابقة إصدارات، اكتشاف الأجزاء وترتيبها، مولِّد هيكل مكتبة جديدة، وأنواع التصنيف
//  (PackageKind / PackageRef / TokenInfo / DepGraph) المشتركة بين المفسّر (rin_libdir.cpp) وأداة
//  سطر الأوامر (`rin lib ...`) بنفس الشيفرة بالضبط.
// ============================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rin {
namespace libdir {

inline constexpr const char* kLibDirSuffix = ".og.rin";   // اسم مجلد المكتبة: <name>.og.rin
inline constexpr const char* kPartExt = ".zpp";           // جزء مكتبة (كود Rin)
inline constexpr const char* kRunExt = ".rinz";           // ملف تشغيل/تهيئة (كود Rin يُنفَّذ بعد ربط الأجزاء)
inline constexpr int kDisconomFormat = 2;                 // أعلى إصدار لصيغة Disconom يفهمه هذا المحرّك (2 = scripts/features/devDependencies/…)
inline constexpr const char* kManifestFiles[] = {"disconom.json", "disconom.xml", "disconom.toml"};   // بترتيب الأولوية
inline constexpr const char* kLockFile = "disconom.lock";  // ملف القفل (JSON) الذي يولّده `rin lib lock`
inline constexpr const char* kToolTokenSuffix = ".og.rin"; // توكن أداة OG: ملف واحد <rinname>.og.rin (التسمية القديمة «مكتبة Rin»)

// نوع الحزمة التي يحلّ إليها @import / rinlib.resolve.
enum class PackageKind { None, ToolToken, Library };
inline const char* kindName(PackageKind k) {          // معرّف ثابت للبرامج (JSON / rinlib.kind)
    return k == PackageKind::ToolToken ? "tool-token" : k == PackageKind::Library ? "library" : "none";
}
inline const char* kindLabelAr(PackageKind k) {       // اسم للعرض
    return k == PackageKind::ToolToken ? "توكن أداة OG" : k == PackageKind::Library ? "مكتبة" : "—";
}

// مرجع حزمة محلولة (مسار منطقي + مصدرها الفعلي). المصدر بترتيب أولوية @import:
//   مجلد (مكتبة)  >  ملف افتراضي (صورة rin_vm)  >  مدمج داخل المفسّر  >  ملف على القرص
struct PackageRef {
    PackageKind kind = PackageKind::None;
    std::string name;            // الاسم العاري: math
    std::string root;            // المسار المنطقي: lib/math.og.rin (مجلد للمكتبة، ملف للتوكن)
    std::string source;          // "disk" | "virtual" | "embedded"
    size_t bytes = 0;            // حجم التوكن (0 للمكتبة)
    bool found() const { return kind != PackageKind::None; }
};

// عنصر في قائمة توكنات الأداة (rinlib.tokens / rin lib tokens).
struct TokenInfo { std::string name, root, source; size_t bytes = 0; };

// رسم التبعيات: العُقد بترتيب التحميل الفعلي (التبعيات قبل من يعتمد عليها، والجذر أخيراً).
struct GraphNode {
    std::string name, version, root, range;  // range = القيد الذي طلبه المعتمِد (فارغ للجذر)
    PackageKind kind = PackageKind::None;
    std::string source;
    bool found = true, satisfied = true;
    std::vector<std::string> deps;           // أسماء التبعيات المباشرة
};
struct DepGraph {
    std::vector<GraphNode> nodes;            // ترتيب التحميل
    std::vector<std::string> cycles;         // "a -> b -> a"
    std::vector<std::string> problems;       // إصدار غير مطابق / عمق تبعيات مفرط (أخطاء)
    std::vector<std::string> missing;        // تبعية لم تُوجَد (تحذير: قد تكون حزمة RinPM تُحلّ وقت التشغيل)
    bool ok() const { return cycles.empty() && problems.empty(); }
};

// ============================================================================
//  JSON صغير (DOM)
// ============================================================================
struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    static J null() { return J(); }
    static J boolean(bool v) { J j; j.t = Bool; j.b = v; return j; }
    static J number(double v) { J j; j.t = Num; j.n = v; return j; }
    static J string(std::string v) { J j; j.t = Str; j.s = std::move(v); return j; }
    static J array() { J j; j.t = Arr; return j; }
    static J object() { J j; j.t = Obj; return j; }
    J& set(const std::string& k, J v) {
        for (auto& kv : o) if (kv.first == k) { kv.second = std::move(v); return *this; }
        o.emplace_back(k, std::move(v));
        return *this;
    }
    J& push(J v) { a.push_back(std::move(v)); return *this; }
    const J* get(const std::string& k) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    bool isStr() const { return t == Str; }
};

namespace detail {

inline void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct JsonParser {
    const std::string& s;
    size_t p = 0;
    std::string err;
    explicit JsonParser(const std::string& text) : s(text) {
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) p = 3; // BOM
    }
    bool fail(const std::string& m) {
        if (err.empty()) {
            int line = 1, col = 1;
            for (size_t i = 0; i < p && i < s.size(); ++i) { if (s[i] == '\n') { ++line; col = 1; } else ++col; }
            err = m + " (line " + std::to_string(line) + ", column " + std::to_string(col) + ")";
        }
        return false;
    }
    void ws() { while (p < s.size() && std::isspace((unsigned char)s[p])) ++p; }
    bool str(std::string& out) {
        if (p >= s.size() || s[p] != '"') return fail("expected '\"'");
        ++p;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= s.size()) return fail("bad escape");
                char e = s[p++];
                switch (e) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case '/': out += '/'; break; case '\\': out += '\\'; break; case '"': out += '"'; break;
                    case 'u': {
                        auto hex4 = [&](unsigned& v) {
                            if (p + 4 > s.size()) return false;
                            v = 0;
                            for (int i = 0; i < 4; ++i) {
                                char h = s[p + i]; v <<= 4;
                                if (h >= '0' && h <= '9') v |= h - '0'; else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                                else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10; else return false;
                            }
                            p += 4; return true;
                        };
                        unsigned cp = 0;
                        if (!hex4(cp)) return fail("bad \\u escape");
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                            size_t save = p; p += 2; unsigned lo = 0;
                            if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else p = save;
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return fail("bad escape");
                }
            } else out += c;
        }
        return fail("unterminated string");
    }
    bool value(J& out, int depth) {
        if (depth > 128) return fail("nesting too deep");
        ws();
        if (p >= s.size()) return fail("unexpected end of input");
        char c = s[p];
        if (c == '{') {
            ++p; out = J::object(); ws();
            if (p < s.size() && s[p] == '}') { ++p; return true; }
            while (true) {
                ws(); std::string k;
                if (!str(k)) return false;
                ws();
                if (p >= s.size() || s[p] != ':') return fail("expected ':'");
                ++p;
                J v; if (!value(v, depth + 1)) return false;
                out.set(k, std::move(v));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++p; out = J::array(); ws();
            if (p < s.size() && s[p] == ']') { ++p; return true; }
            while (true) {
                J v; if (!value(v, depth + 1)) return false;
                out.a.push_back(std::move(v));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string v; if (!str(v)) return false; out = J::string(std::move(v)); return true; }
        if (s.compare(p, 4, "true") == 0) { p += 4; out = J::boolean(true); return true; }
        if (s.compare(p, 5, "false") == 0) { p += 5; out = J::boolean(false); return true; }
        if (s.compare(p, 4, "null") == 0) { p += 4; out = J::null(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            double d = std::strtod(s.c_str() + p, &end);
            if (end == s.c_str() + p) return fail("bad number");
            p = static_cast<size_t>(end - s.c_str());
            out = J::number(d); return true;
        }
        return fail(std::string("unexpected character '") + c + "'");
    }
};

inline void dumpStr(const std::string& v, std::string& o) {
    o += '"';
    for (unsigned char c : v) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
        }
    }
    o += '"';
}

inline void dumpJ(const J& j, std::string& o, int indent, int depth) {
    auto nl = [&](int d) { if (indent > 0) { o += '\n'; o.append(static_cast<size_t>(indent * d), ' '); } };
    switch (j.t) {
        case J::Null: o += "null"; break;
        case J::Bool: o += j.b ? "true" : "false"; break;
        case J::Num: {
            char b[40];
            if (j.n == std::floor(j.n) && std::fabs(j.n) < 1e15) std::snprintf(b, sizeof b, "%.0f", j.n);
            else std::snprintf(b, sizeof b, "%.15g", j.n);
            o += b; break;
        }
        case J::Str: dumpStr(j.s, o); break;
        case J::Arr:
            if (j.a.empty()) { o += "[]"; break; }
            o += '[';
            for (size_t i = 0; i < j.a.size(); ++i) { if (i) o += ','; nl(depth + 1); dumpJ(j.a[i], o, indent, depth + 1); }
            nl(depth); o += ']'; break;
        case J::Obj:
            if (j.o.empty()) { o += "{}"; break; }
            o += '{';
            for (size_t i = 0; i < j.o.size(); ++i) {
                if (i) o += ',';
                nl(depth + 1); dumpStr(j.o[i].first, o); o += indent > 0 ? ": " : ":";
                dumpJ(j.o[i].second, o, indent, depth + 1);
            }
            nl(depth); o += '}'; break;
    }
}

} // namespace detail

inline bool parseJson(const std::string& text, J& out, std::string& err) {
    detail::JsonParser jp(text);
    if (!jp.value(out, 0)) { err = jp.err; return false; }
    jp.ws();
    if (jp.p != text.size()) { jp.fail("unexpected trailing content"); err = jp.err; return false; }
    return true;
}
inline std::string dumpJson(const J& j, int indent = 2) { std::string o; detail::dumpJ(j, o, indent, 0); return o; }

// ============================================================================
//  TOML (مجموعة جزئية كافية للمانيفست): جداول [a.b] ومصفوفات الجداول [[a]]، مفاتيح منقّطة،
//  نصوص (أساسية/حرفية/متعدّدة الأسطر)، أعداد، منطقيات، مصفوفات (متعدّدة الأسطر)، جداول مضمّنة { }،
//  وتواريخ (تُقرأ كنص). يُحوَّل إلى نفس DOM الخاص بـ JSON فتُستعمل manifestFromJson نفسها.
// ============================================================================
namespace detail {
struct TomlParser {
    const std::string& s;
    size_t p = 0;
    std::string err;
    explicit TomlParser(const std::string& text) : s(text) {
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) p = 3;
    }
    bool fail(const std::string& m) {
        if (err.empty()) {
            int line = 1, col = 1;
            for (size_t i = 0; i < p && i < s.size(); ++i) { if (s[i] == '\n') { ++line; col = 1; } else ++col; }
            err = m + " (line " + std::to_string(line) + ", column " + std::to_string(col) + ")";
        }
        return false;
    }
    bool eof() const { return p >= s.size(); }
    void sp() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p; }
    void comment() { if (p < s.size() && s[p] == '#') while (p < s.size() && s[p] != '\n') ++p; }
    void skipAll() {   // مسافات + أسطر + تعليقات
        while (p < s.size()) {
            if (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n') ++p;
            else if (s[p] == '#') comment();
            else break;
        }
    }
    static bool bareChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-'; }
    bool basicString(std::string& out, bool multi) {   // p بعد علامة الفتح
        while (p < s.size()) {
            if (multi) { if (s.compare(p, 3, "\"\"\"") == 0) { p += 3; while (p < s.size() && s[p] == '"') { out += '"'; ++p; } return true; } }
            else if (s[p] == '"') { ++p; return true; }
            char c = s[p++];
            if (!multi && (c == '\n' || c == '\r')) return fail("unterminated string");
            if (c != '\\') { out += c; continue; }
            if (p >= s.size()) return fail("bad escape");
            char e = s[p++];
            switch (e) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break; case '\\': out += '\\'; break; case '"': out += '"'; break;
                case 'u': case 'U': {
                    int n = e == 'u' ? 4 : 8; if (p + n > s.size()) return fail("bad unicode escape");
                    unsigned cp = 0;
                    for (int i = 0; i < n; ++i) {
                        char h = s[p + i]; cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0'; else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10; else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10; else return fail("bad unicode escape");
                    }
                    p += n; appendUtf8(out, cp); break;
                }
                case '\n': case '\r': if (multi) { while (p < s.size() && std::isspace((unsigned char)s[p])) ++p; break; } return fail("bad escape");
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool str(std::string& out) {   // عند " أو '
        if (s.compare(p, 3, "\"\"\"") == 0) { p += 3; if (p < s.size() && s[p] == '\r') ++p; if (p < s.size() && s[p] == '\n') ++p; return basicString(out, true); }
        if (s.compare(p, 3, "'''") == 0) {
            p += 3; if (p < s.size() && s[p] == '\r') ++p; if (p < s.size() && s[p] == '\n') ++p;
            size_t e = s.find("'''", p); if (e == std::string::npos) return fail("unterminated string");
            out = s.substr(p, e - p); p = e + 3; return true;
        }
        if (s[p] == '"') { ++p; return basicString(out, false); }
        ++p; size_t e = p;
        while (e < s.size() && s[e] != '\'' && s[e] != '\n') ++e;
        if (e >= s.size() || s[e] != '\'') return fail("unterminated string");
        out = s.substr(p, e - p); p = e + 1; return true;
    }
    bool keyPart(std::string& out) {
        sp();
        if (p < s.size() && (s[p] == '"' || s[p] == '\'')) return str(out);
        size_t b = p; while (p < s.size() && bareChar(s[p])) ++p;
        if (p == b) return fail("expected a key");
        out = s.substr(b, p - b); return true;
    }
    bool keyPath(std::vector<std::string>& path) {
        while (true) {
            std::string k; if (!keyPart(k)) return false; path.push_back(k); sp();
            if (p < s.size() && s[p] == '.') { ++p; continue; }
            return true;
        }
    }
    bool value(J& out, int depth) {
        if (depth > 64) return fail("nesting too deep");
        sp();
        if (p >= s.size()) return fail("expected a value");
        char c = s[p];
        if (c == '"' || c == '\'') { std::string v; if (!str(v)) return false; out = J::string(std::move(v)); return true; }
        if (c == '[') {
            ++p; out = J::array();
            while (true) {
                skipAll();
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                J v; if (!value(v, depth + 1)) return false;
                out.a.push_back(std::move(v)); skipAll();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '{') {
            ++p; out = J::object();
            while (true) {
                skipAll();
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                std::vector<std::string> path; if (!keyPath(path)) return false; sp();
                if (p >= s.size() || s[p] != '=') return fail("expected '='");
                ++p; J v; if (!value(v, depth + 1)) return false;
                if (!assign(out, path, std::move(v))) return false;
                skipAll();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                return fail("expected ',' or '}'");
            }
        }
        size_t b = p;
        while (p < s.size() && s[p] != ',' && s[p] != ']' && s[p] != '}' && s[p] != '\n' && s[p] != '\r' && s[p] != '#' && s[p] != ' ' && s[p] != '\t') ++p;
        std::string tok = s.substr(b, p - b);
        if (tok == "true") { out = J::boolean(true); return true; }
        if (tok == "false") { out = J::boolean(false); return true; }
        if (tok.empty()) return fail("expected a value");
        std::string num; for (char ch : tok) if (ch != '_') num += ch;
        char* end = nullptr; double d = std::strtod(num.c_str(), &end);
        if (end && *end == '\0' && (std::isdigit((unsigned char)num[0]) || num[0] == '-' || num[0] == '+')) { out = J::number(d); return true; }
        if (std::isdigit((unsigned char)tok[0])) {   // تاريخ/وقت: يُحفظ كنص
            if (p < s.size() && s[p] == ' ' && p + 1 < s.size() && std::isdigit((unsigned char)s[p + 1])) { ++p; size_t b2 = p; while (p < s.size() && s[p] != ',' && s[p] != ']' && s[p] != '}' && s[p] != '\n' && s[p] != '#' && s[p] != ' ') ++p; tok += " " + s.substr(b2, p - b2); }
            out = J::string(tok); return true;
        }
        p = b; return fail("unexpected value '" + tok + "'");
    }
    // يصل إلى كائن (ينشئه عند الحاجة). مصفوفة جداول ⇒ آخر عنصر.
    J* objAt(J& parent, const std::string& k) {
        for (auto& kv : parent.o) if (kv.first == k) {
            if (kv.second.t == J::Obj) return &kv.second;
            if (kv.second.t == J::Arr && !kv.second.a.empty() && kv.second.a.back().t == J::Obj) return &kv.second.a.back();
            fail("key '" + k + "' is already defined as a value"); return nullptr;
        }
        parent.o.emplace_back(k, J::object()); return &parent.o.back().second;
    }
    bool assign(J& cur, const std::vector<std::string>& path, J v) {
        J* t = &cur;
        for (size_t i = 0; i + 1 < path.size(); ++i) { t = objAt(*t, path[i]); if (!t) return false; }
        if (t->get(path.back())) return fail("duplicate key '" + path.back() + "'");
        t->o.emplace_back(path.back(), std::move(v)); return true;
    }
    bool doc(J& root) {
        root = J::object(); J* cur = &root;
        while (true) {
            skipAll();
            if (eof()) return true;
            if (s[p] == '[') {
                bool dbl = (p + 1 < s.size() && s[p + 1] == '[');
                p += dbl ? 2 : 1;
                std::vector<std::string> path; if (!keyPath(path)) return false; sp();
                if (dbl ? s.compare(p, 2, "]]") != 0 : (p >= s.size() || s[p] != ']')) return fail("expected ']'");
                p += dbl ? 2 : 1;
                J* t = &root;
                for (size_t i = 0; i + 1 < path.size(); ++i) { t = objAt(*t, path[i]); if (!t) return false; }
                if (dbl) {
                    J* arr = nullptr;
                    for (auto& kv : t->o) if (kv.first == path.back()) arr = &kv.second;
                    if (!arr) { t->o.emplace_back(path.back(), J::array()); arr = &t->o.back().second; }
                    if (arr->t != J::Arr) return fail("'" + path.back() + "' is not an array of tables");
                    arr->a.push_back(J::object()); cur = &arr->a.back();
                } else {
                    cur = objAt(*t, path.back()); if (!cur) return false;
                }
            } else {
                std::vector<std::string> path; if (!keyPath(path)) return false; sp();
                if (p >= s.size() || s[p] != '=') return fail("expected '='");
                ++p; J v; if (!value(v, 0)) return false;
                if (!assign(*cur, path, std::move(v))) return false;
            }
            sp(); comment();
            if (!eof() && s[p] != '\n' && s[p] != '\r') return fail("unexpected content after value");
        }
    }
};

inline std::string tomlKey(const std::string& k) {
    bool bare = !k.empty();
    for (char c : k) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) bare = false;
    if (bare) return k;
    std::string o; dumpStr(k, o); return o;
}
inline void tomlInline(const J& j, std::string& o) {
    switch (j.t) {
        case J::Null: o += "\"\""; break;
        case J::Bool: o += j.b ? "true" : "false"; break;
        case J::Num: dumpJ(j, o, 0, 0); break;
        case J::Str: dumpStr(j.s, o); break;
        case J::Arr: o += '['; for (size_t i = 0; i < j.a.size(); ++i) { if (i) o += ", "; tomlInline(j.a[i], o); } o += ']'; break;
        case J::Obj: o += "{ "; for (size_t i = 0; i < j.o.size(); ++i) { if (i) o += ", "; o += tomlKey(j.o[i].first) + " = "; tomlInline(j.o[i].second, o); } o += j.o.empty() ? "}" : " }"; break;
    }
}
inline void tomlTable(const J& obj, const std::string& path, std::string& o, bool& first) {
    bool header = false;
    auto emitHeader = [&]() { if (!path.empty() && !header) { if (!first) o += "\n"; o += "[" + path + "]\n"; header = true; first = false; } };
    for (auto& kv : obj.o) {
        if (kv.second.t == J::Obj || kv.second.t == J::Null) continue;
        emitHeader();
        o += tomlKey(kv.first) + " = "; tomlInline(kv.second, o); o += "\n"; first = false;
    }
    for (auto& kv : obj.o) {
        if (kv.second.t != J::Obj || kv.second.o.empty()) continue;
        tomlTable(kv.second, path.empty() ? tomlKey(kv.first) : path + "." + tomlKey(kv.first), o, first);
    }
}
} // namespace detail

inline bool parseToml(const std::string& text, J& out, std::string& err) {
    detail::TomlParser tp(text);
    if (!tp.doc(out)) { err = tp.err; return false; }
    return true;
}
// J (كائن) → TOML: القيم البسيطة والمصفوفات أولاً ثم الجداول؛ الكائنات الفارغة تُحذف.
inline std::string dumpToml(const J& obj) { std::string o; bool first = true; detail::tomlTable(obj, "", o, first); return o; }

// ============================================================================
//  XML صغير (DOM): عناصر + سمات + نص (+ CDATA + تعليقات + كيانات أساسية)
// ============================================================================
struct X {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;           // النص المباشر مجمَّعاً (مقصوص الأطراف)
    std::vector<X> kids;
    int line = 0;
    const std::string* attr(const std::string& k) const {
        for (auto& kv : attrs) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    const X* child(const std::string& t) const {
        for (auto& c : kids) if (c.tag == t) return &c;
        return nullptr;
    }
};

namespace detail {
inline std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline std::string xmlUnescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { o += s[i]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") o += '<'; else if (ent == "gt") o += '>'; else if (ent == "amp") o += '&';
        else if (ent == "quot") o += '"'; else if (ent == "apos") o += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned cp = 0;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) cp = (unsigned)std::strtoul(ent.c_str() + 2, nullptr, 16);
            else cp = (unsigned)std::strtoul(ent.c_str() + 1, nullptr, 10);
            appendUtf8(o, cp);
        } else { o += s[i]; continue; }
        i = semi;
    }
    return o;
}
struct XmlParser {
    const std::string& s;
    size_t p = 0;
    int line = 1;
    std::string err;
    explicit XmlParser(const std::string& t) : s(t) {
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) p = 3;
    }
    bool fail(const std::string& m) { if (err.empty()) err = m + " (line " + std::to_string(line) + ")"; return false; }
    void adv(size_t n = 1) { for (size_t i = 0; i < n && p < s.size(); ++i) { if (s[p] == '\n') ++line; ++p; } }
    bool starts(const char* lit) const { return s.compare(p, std::char_traits<char>::length(lit), lit) == 0; }
    void ws() { while (p < s.size() && std::isspace((unsigned char)s[p])) adv(); }
    bool skipMisc() {   // تعليقات / تعليمات معالجة / DOCTYPE
        while (true) {
            ws();
            if (starts("<!--")) { size_t e = s.find("-->", p); if (e == std::string::npos) return fail("unterminated comment"); adv(e + 3 - p); }
            else if (starts("<?")) { size_t e = s.find("?>", p); if (e == std::string::npos) return fail("unterminated <? ?>"); adv(e + 2 - p); }
            else if (starts("<!DOCTYPE")) { size_t e = s.find('>', p); if (e == std::string::npos) return fail("unterminated DOCTYPE"); adv(e + 1 - p); }
            else return true;
        }
    }
    static bool nameCh(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':' || c == '.'; }
    bool element(X& out, int depth) {
        if (depth > 128) return fail("nesting too deep");
        if (p >= s.size() || s[p] != '<') return fail("expected '<'");
        out.line = line;
        adv();
        size_t b = p;
        while (p < s.size() && nameCh(s[p])) adv();
        out.tag = s.substr(b, p - b);
        if (out.tag.empty()) return fail("missing tag name");
        while (true) {
            ws();
            if (p >= s.size()) return fail("unterminated tag <" + out.tag + ">");
            if (s[p] == '/') { adv(); if (p >= s.size() || s[p] != '>') return fail("expected '>'"); adv(); return true; }
            if (s[p] == '>') { adv(); break; }
            size_t nb = p;
            while (p < s.size() && nameCh(s[p])) adv();
            if (p == nb) return fail(std::string("unexpected character '") + s[p] + "' in tag <" + out.tag + ">");
            std::string an = s.substr(nb, p - nb);
            ws();
            if (p >= s.size() || s[p] != '=') return fail("attribute '" + an + "' needs a value");
            adv(); ws();
            if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) return fail("attribute value must be quoted");
            char q = s[p]; adv();
            size_t vb = p;
            while (p < s.size() && s[p] != q) adv();
            if (p >= s.size()) return fail("unterminated attribute value");
            out.attrs.emplace_back(an, xmlUnescape(s.substr(vb, p - vb)));
            adv();
        }
        std::string text;
        while (true) {
            if (p >= s.size()) return fail("missing closing tag </" + out.tag + ">");
            if (starts("<!--")) { size_t e = s.find("-->", p); if (e == std::string::npos) return fail("unterminated comment"); adv(e + 3 - p); continue; }
            if (starts("<![CDATA[")) {
                size_t e = s.find("]]>", p); if (e == std::string::npos) return fail("unterminated CDATA");
                text += s.substr(p + 9, e - p - 9); adv(e + 3 - p); continue;
            }
            if (starts("</")) {
                adv(2); size_t cb = p;
                while (p < s.size() && nameCh(s[p])) adv();
                std::string ct = s.substr(cb, p - cb);
                if (ct != out.tag) return fail("closing tag </" + ct + "> does not match <" + out.tag + ">");
                ws();
                if (p >= s.size() || s[p] != '>') return fail("expected '>'");
                adv();
                out.text = trimStr(text);
                return true;
            }
            if (s[p] == '<') { X k; if (!element(k, depth + 1)) return false; out.kids.push_back(std::move(k)); continue; }
            size_t tb = p;
            while (p < s.size() && s[p] != '<') adv();
            text += xmlUnescape(s.substr(tb, p - tb));
        }
    }
};
} // namespace detail

inline bool parseXml(const std::string& text, X& root, std::string& err) {
    detail::XmlParser xp(text);
    if (!xp.skipMisc()) { err = xp.err; return false; }
    if (!xp.element(root, 0)) { err = xp.err; return false; }
    if (!xp.skipMisc()) { err = xp.err; return false; }
    if (xp.p < text.size()) { xp.fail("unexpected content after the root element"); err = xp.err; return false; }
    return true;
}

// ============================================================================
//  Semver + نطاقات الإصدار  (^ ~ >= <= > < = ، 1.2.x ، * ، || ، مسافة = AND)
// ============================================================================
struct SemVer {
    long long ma = 0, mi = 0, pa = 0;
    std::string pre;
};

inline bool parseSemVer(std::string s, SemVer& out) {
    s = detail::trimStr(s);
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    size_t plus = s.find('+');
    if (plus != std::string::npos) s = s.substr(0, plus);
    std::string pre;
    size_t dash = s.find('-');
    if (dash != std::string::npos) { pre = s.substr(dash + 1); s = s.substr(0, dash); if (pre.empty()) return false; }
    long long n[3] = {0, 0, 0};
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        size_t e = s.find('.', pos);
        std::string part = s.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (part.empty() || part.size() > 12) return false;
        for (char c : part) if (!std::isdigit((unsigned char)c)) return false;
        n[i] = std::stoll(part);
        if (e == std::string::npos) { if (i != 2) return false; pos = s.size(); break; }
        pos = e + 1;
        if (i == 2) return false;
    }
    out.ma = n[0]; out.mi = n[1]; out.pa = n[2]; out.pre = pre;
    return true;
}

inline int cmpSemVer(const SemVer& a, const SemVer& b) {
    if (a.ma != b.ma) return a.ma < b.ma ? -1 : 1;
    if (a.mi != b.mi) return a.mi < b.mi ? -1 : 1;
    if (a.pa != b.pa) return a.pa < b.pa ? -1 : 1;
    if (a.pre.empty() != b.pre.empty()) return a.pre.empty() ? 1 : -1;   // الإصدار بلا لاحقة أحدث من pre-release
    if (a.pre != b.pre) return a.pre < b.pre ? -1 : 1;
    return 0;
}

namespace detail {
struct Partial { long long v[3] = {0, 0, 0}; int given = 0; };   // given = عدد المكوّنات المحدَّدة (0 = *)
inline bool parsePartial(const std::string& t, Partial& out) {
    std::string s = t;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    if (s.empty() || s == "*" || s == "x" || s == "X") { out.given = 0; return true; }
    size_t dash = s.find('-'); if (dash != std::string::npos) s = s.substr(0, dash);
    size_t plus = s.find('+'); if (plus != std::string::npos) s = s.substr(0, plus);
    int i = 0; size_t pos = 0;
    while (i < 3 && pos <= s.size()) {
        size_t e = s.find('.', pos);
        std::string part = s.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (part == "x" || part == "X" || part == "*") break;
        if (part.empty() || part.size() > 12) return false;
        for (char c : part) if (!std::isdigit((unsigned char)c)) return false;
        out.v[i++] = std::stoll(part);
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    out.given = i;
    return true;
}
inline SemVer mk(long long a, long long b, long long c) { SemVer v; v.ma = a; v.mi = b; v.pa = c; return v; }
// يحوّل مقارِناً واحداً إلى [lo, hi) ؛ hasHi=false = بلا حد أعلى
struct Bounds { SemVer lo; bool hasLo = true, loIncl = true; SemVer hi; bool hasHi = false, hiIncl = false; };
inline bool comparatorBounds(const std::string& tok, Bounds& b) {
    std::string op, rest = tok;
    for (const char* o : {">=", "<=", ">", "<", "=", "^", "~"})
        if (rest.rfind(o, 0) == 0) { op = o; rest = rest.substr(std::char_traits<char>::length(o)); break; }
    Partial p; if (!parsePartial(rest, p)) return false;
    const long long* v = p.v;
    if (op.empty() || op == "=") {
        if (p.given == 0) { b.hasLo = false; return true; }
        b.lo = mk(v[0], v[1], v[2]);
        if (p.given == 3) { b.hi = b.lo; b.hasHi = true; b.hiIncl = true; }
        else { b.hasHi = true; b.hi = p.given == 1 ? mk(v[0] + 1, 0, 0) : mk(v[0], v[1] + 1, 0); }
        return true;
    }
    if (op == "^") {
        if (p.given == 0) { b.hasLo = false; return true; }
        b.lo = mk(v[0], v[1], v[2]); b.hasHi = true;
        if (v[0] > 0 || p.given == 1) b.hi = mk(v[0] + 1, 0, 0);
        else if (v[1] > 0 || p.given == 2) b.hi = mk(0, v[1] + 1, 0);
        else b.hi = mk(0, 0, v[2] + 1);
        return true;
    }
    if (op == "~") {
        if (p.given == 0) { b.hasLo = false; return true; }
        b.lo = mk(v[0], v[1], v[2]); b.hasHi = true;
        b.hi = p.given == 1 ? mk(v[0] + 1, 0, 0) : mk(v[0], v[1] + 1, 0);
        return true;
    }
    if (p.given == 0) { b.hasLo = false; return true; }   // >=* و<=* ... = أي إصدار
    if (op == ">=") { b.lo = mk(v[0], v[1], v[2]); return true; }
    if (op == ">") {
        if (p.given == 3) { b.lo = mk(v[0], v[1], v[2]); b.loIncl = false; }
        else b.lo = p.given == 1 ? mk(v[0] + 1, 0, 0) : mk(v[0], v[1] + 1, 0);
        return true;
    }
    if (op == "<") { b.hasLo = false; b.hasHi = true; b.hi = mk(v[0], v[1], v[2]); return true; }
    if (op == "<=") {
        b.hasLo = false; b.hasHi = true;
        if (p.given == 3) { b.hi = mk(v[0], v[1], v[2]); b.hiIncl = true; }
        else b.hi = p.given == 1 ? mk(v[0] + 1, 0, 0) : mk(v[0], v[1] + 1, 0);
        return true;
    }
    return false;
}
} // namespace detail

// يفحص صلاحية صياغة النطاق فقط (بلا مقارنة).
inline bool validRange(const std::string& range) {
    std::string r = detail::trimStr(range);
    if (r.empty()) return true;
    size_t pos = 0;
    while (pos <= r.size()) {
        size_t bar = r.find("||", pos);
        std::string alt = r.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
        std::istringstream is(alt); std::string tok, pending;
        int count = 0;
        while (is >> tok) {
            if (tok == ">=" || tok == "<=" || tok == ">" || tok == "<" || tok == "=" || tok == "^" || tok == "~") { pending = tok; continue; }
            detail::Bounds b;
            if (!detail::comparatorBounds(pending + tok, b)) return false;
            pending.clear(); ++count;
        }
        if (!pending.empty()) return false;
        (void)count;
        if (bar == std::string::npos) break;
        pos = bar + 2;
    }
    return true;
}

inline bool satisfies(const std::string& version, const std::string& range) {
    SemVer v; if (!parseSemVer(version, v)) return false;
    std::string r = detail::trimStr(range);
    if (r.empty()) return true;
    size_t pos = 0;
    while (pos <= r.size()) {
        size_t bar = r.find("||", pos);
        std::string alt = r.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
        std::istringstream is(alt); std::string tok, pending;
        bool ok = true, any = false;
        while (is >> tok) {
            if (tok == ">=" || tok == "<=" || tok == ">" || tok == "<" || tok == "=" || tok == "^" || tok == "~") { pending = tok; continue; }
            detail::Bounds b;
            if (!detail::comparatorBounds(pending + tok, b)) { ok = false; break; }
            pending.clear(); any = true;
            if (b.hasLo) { int c = cmpSemVer(v, b.lo); if (c < 0 || (c == 0 && !b.loIncl)) ok = false; }
            if (b.hasHi) {
                SemVer hi = b.hi;
                int c = cmpSemVer(v, hi);
                // pre-release للحد الأعلى غير الشامل (مثل 1.2.3-beta < 2.0.0) يبقى داخل النطاق كعادة semver
                if (c > 0 || (c == 0 && !b.hiIncl)) ok = false;
            }
        }
        if (ok && (any || alt.find_first_not_of(" \t") == std::string::npos)) return true;
        if (bar == std::string::npos) break;
        pos = bar + 2;
    }
    return false;
}

// ============================================================================
//  Disconom: وصف المكتبة (الإصدار، اللغة، التفاصيل، الأجزاء، التبعيات، الكود الأصلي)
// ============================================================================
struct FnSpec {
    std::string name;
    std::vector<std::string> params;
    bool hasParams = false;   // false = دالة تستقبل مصفوفة وسائط واحدة (args)
};
struct NativeSpec {
    std::vector<std::string> sources, flags, libs, includes;
    std::string std = "c++17";
    bool required = false;    // true = فشل/تعطيل الكود الأصلي يُفشل استيراد المكتبة
    std::vector<FnSpec> functions;
    bool declared() const { return !sources.empty(); }
};
struct Dep { std::string name, range; };

struct Manifest {
    int format = kDisconomFormat;
    std::string name, version, description, license, homepage, repository, language = "rin";
    std::string rin;                         // قيد إصدار لغة Rin المطلوب، مثل ">=1.0.0" أو "^1.0"
    std::vector<std::string> authors, keywords;
    std::string main;                        // الجزء الأساسي (افتراضياً main.zpp)
    std::vector<std::string> parts, exclude, run;
    bool hasParts = false, hasRun = false;
    std::vector<Dep> dependencies;
    // ---- Disconom 2 ----
    std::vector<Dep> devDependencies;        // للتطوير/الاختبار فقط: لا تُحمَّل عند استيراد المكتبة
    std::vector<Dep> optionalDependencies;   // تُحمَّل إن وُجدت، ولا تُعدّ غيابها خطأً
    std::vector<Dep> peerDependencies;       // يجب أن يوفّرها المستهلِك (تُفحص ولا تُحمَّل)
    std::vector<std::pair<std::string, std::string>> scripts;                    // اسم → أمر
    std::vector<std::pair<std::string, std::vector<std::string>>> features;     // اسم → [تبعيات اختيارية/ميزات أخرى]
    std::vector<std::string> defaultFeatures;   // الميزات المفعّلة افتراضياً (features.default)
    std::vector<std::string> files;          // ما يُضمَّن عند النشر/الحزم (فارغ = الكل)
    std::vector<std::string> categories, platforms, workspaces, contributors;
    std::string bugs, readme, funding;
    bool isPrivate = false;                  // true = لا يُنشر إلى سجلّ
    bool hasFormat2 = false;                 // استُعملت حقول من الصيغة 2 (للتحذير إن أُعلنت الصيغة 1)
    NativeSpec native;
    J raw;                                   // محتوى JSON الأصلي كاملاً (حقول إضافية مخصّصة) إن كان المصدر JSON
    bool hasRaw = false;
    std::string file, formatName;            // اسم ملف Disconom الفعلي، و "json" | "xml"
};

namespace detail {
inline std::vector<std::string> strList(const J* j) {
    std::vector<std::string> out;
    if (!j) return out;
    if (j->t == J::Str) out.push_back(j->s);
    else if (j->t == J::Arr) for (auto& x : j->a) if (x.t == J::Str) out.push_back(x.s);
    return out;
}
inline std::string strOf(const J* j) { return (j && j->t == J::Str) ? j->s : std::string(); }
inline std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s + ",") { if (c == ',' || c == ' ') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    return out;
}
} // namespace detail

inline bool manifestFromJson(const J& j, Manifest& m, std::vector<std::string>& errs) {
    using namespace detail;
    if (j.t != J::Obj) { errs.push_back("Disconom JSON must be an object at the top level"); return false; }
    m.raw = j; m.hasRaw = true; m.formatName = "json";
    if (auto* f = j.get("disconom")) {
        if (f->t == J::Num) m.format = static_cast<int>(f->n);
        else if (f->t == J::Obj) { if (auto* ff = f->get("format")) if (ff->t == J::Num) m.format = static_cast<int>(ff->n); }
        else errs.push_back("'disconom' must be the format version number (e.g. 1)");
    } else if (auto* f2 = j.get("format")) { if (f2->t == J::Num) m.format = static_cast<int>(f2->n); }
    m.name = strOf(j.get("name"));
    m.version = strOf(j.get("version"));
    for (const char* k : {"description", "details", "desc"}) if (auto* x = j.get(k)) { if (x->t == J::Str) { m.description = x->s; break; } }
    m.license = strOf(j.get("license"));
    m.homepage = strOf(j.get("homepage"));
    if (auto* r = j.get("repository")) { if (r->t == J::Str) m.repository = r->s; else if (r->t == J::Obj) m.repository = strOf(r->get("url")); }
    m.authors = strList(j.get("authors"));
    if (m.authors.empty()) m.authors = strList(j.get("author"));
    m.keywords = strList(j.get("keywords"));
    if (auto* l = j.get("language")) {
        if (l->t == J::Str) m.language = l->s;
        else if (l->t == J::Obj) {
            if (auto* n = l->get("name")) if (n->t == J::Str) m.language = n->s;
            if (auto* v = l->get("version")) if (v->t == J::Str) m.rin = v->s;
        }
    }
    for (const char* k : {"rin", "rinVersion", "rin_version", "engine"})
        if (auto* x = j.get(k)) { if (x->t == J::Str) { m.rin = x->s; break; } else errs.push_back(std::string("'") + k + "' must be a version-constraint string"); }
    for (const char* k : {"main", "entry"}) if (auto* x = j.get(k)) { if (x->t == J::Str) { m.main = x->s; break; } }
    if (auto* x = j.get("parts")) { m.hasParts = true; m.parts = strList(x); if (x->t != J::Arr && x->t != J::Str) errs.push_back("'parts' must be an array of paths/globs"); }
    m.exclude = strList(j.get("exclude"));
    if (auto* x = j.get("run")) { m.hasRun = true; m.run = strList(x); if (x->t != J::Arr && x->t != J::Str) errs.push_back("'run' must be a path or an array of paths"); }
    auto depList = [&](const J* d, std::vector<Dep>& out, const char* label) {
        if (!d) return;
        if (d->t == J::Obj) { for (auto& kv : d->o) out.push_back({kv.first, kv.second.t == J::Str ? kv.second.s : std::string("*")}); }
        else if (d->t == J::Arr) {
            for (auto& e : d->a) {
                if (e.t == J::Str) out.push_back({e.s, "*"});
                else if (e.t == J::Obj) { std::string n = strOf(e.get("name")), v = strOf(e.get("version")); if (v.empty()) v = strOf(e.get("range")); out.push_back({n, v.empty() ? "*" : v}); }
            }
        } else errs.push_back(std::string("'") + label + "' must be an object {name: range} or an array");
    };
    auto firstOf = [&](std::initializer_list<const char*> keys) -> const J* { for (const char* k : keys) if (auto* x = j.get(k)) return x; return nullptr; };
    depList(j.get("dependencies"), m.dependencies, "dependencies");
    if (auto* x = firstOf({"devDependencies", "dev-dependencies", "dev_dependencies"})) { depList(x, m.devDependencies, "devDependencies"); m.hasFormat2 = true; }
    if (auto* x = firstOf({"optionalDependencies", "optional-dependencies", "optional_dependencies"})) { depList(x, m.optionalDependencies, "optionalDependencies"); m.hasFormat2 = true; }
    if (auto* x = firstOf({"peerDependencies", "peer-dependencies", "peer_dependencies"})) { depList(x, m.peerDependencies, "peerDependencies"); m.hasFormat2 = true; }
    if (auto* x = j.get("scripts")) {
        m.hasFormat2 = true;
        if (x->t == J::Obj) { for (auto& kv : x->o) { if (kv.second.t == J::Str) m.scripts.emplace_back(kv.first, kv.second.s); else errs.push_back("'scripts." + kv.first + "' must be a command string"); } }
        else errs.push_back("'scripts' must be an object {name: command}");
    }
    if (auto* x = j.get("features")) {
        m.hasFormat2 = true;
        if (x->t == J::Obj) {
            for (auto& kv : x->o) {
                if (kv.first == "default") { m.defaultFeatures = strList(&kv.second); continue; }
                if (kv.second.t != J::Arr) { errs.push_back("'features." + kv.first + "' must be an array of strings"); continue; }
                m.features.emplace_back(kv.first, strList(&kv.second));
            }
        } else errs.push_back("'features' must be an object {name: [items]}");
    }
    if (auto* x = j.get("files")) { m.files = strList(x); m.hasFormat2 = true; }
    m.categories = strList(j.get("categories")); m.platforms = strList(j.get("platforms"));
    m.workspaces = strList(j.get("workspaces")); m.contributors = strList(j.get("contributors"));
    if (auto* x = j.get("bugs")) { if (x->t == J::Str) m.bugs = x->s; else if (x->t == J::Obj) m.bugs = strOf(x->get("url")); }
    m.readme = strOf(j.get("readme")); m.funding = strOf(j.get("funding"));
    if (auto* x = j.get("private")) { if (x->t == J::Bool) m.isPrivate = x->b; else errs.push_back("'private' must be true or false"); m.hasFormat2 = true; }
    if (!m.categories.empty() || !m.platforms.empty() || !m.workspaces.empty() || !m.contributors.empty() || !m.bugs.empty() || !m.funding.empty() || !m.readme.empty()) m.hasFormat2 = true;
    if (auto* n = j.get("native")) {
        NativeSpec& ns = m.native;
        if (n->t == J::Arr || n->t == J::Str) ns.sources = strList(n);
        else if (n->t == J::Obj) {
            ns.sources = strList(n->get("sources"));
            if (ns.sources.empty()) ns.sources = strList(n->get("source"));
            if (auto* s = n->get("std")) if (s->t == J::Str) ns.std = s->s;
            ns.flags = strList(n->get("flags")); ns.libs = strList(n->get("libs")); ns.includes = strList(n->get("includes"));
            if (auto* r = n->get("required")) ns.required = (r->t == J::Bool && r->b);
            if (auto* fs = n->get("functions")) {
                auto addFn = [&](const std::string& nm, const J* params) {
                    FnSpec f; f.name = nm;
                    if (params && params->t == J::Arr) { f.hasParams = true; f.params = strList(params); }
                    ns.functions.push_back(std::move(f));
                };
                if (fs->t == J::Arr) {
                    for (auto& e : fs->a) {
                        if (e.t == J::Str) addFn(e.s, nullptr);
                        else if (e.t == J::Obj) addFn(strOf(e.get("name")), e.get("params"));
                    }
                } else if (fs->t == J::Obj) { for (auto& kv : fs->o) addFn(kv.first, &kv.second); }
                else errs.push_back("'native.functions' must be an array or an object");
            }
        } else errs.push_back("'native' must be an object or an array of source paths");
    }
    return true;
}

inline bool manifestFromXml(const X& root, Manifest& m, std::vector<std::string>& errs) {
    using namespace detail;
    if (root.tag != "disconom") { errs.push_back("the root element of a Disconom XML file must be <disconom> (found <" + root.tag + ">)"); return false; }
    m.formatName = "xml";
    auto scalar = [&](const char* key) -> std::string {
        if (auto* a = root.attr(key)) return *a;
        if (auto* c = root.child(key)) return c->text;
        return "";
    };
    auto list = [&](const char* plural, const char* singular) {
        std::vector<std::string> out;
        if (auto* c = root.child(plural)) { for (auto& k : c->kids) out.push_back(k.text); if (out.empty() && !c->text.empty()) out = splitCsv(c->text); }
        for (auto& k : root.kids) if (k.tag == singular) out.push_back(k.text);
        return out;
    };
    if (auto* f = root.attr("format")) m.format = std::atoi(f->c_str());
    else if (auto* f2 = root.attr("version-format")) m.format = std::atoi(f2->c_str());
    m.name = scalar("name"); m.version = scalar("version");
    m.description = scalar("description"); if (m.description.empty()) m.description = scalar("details");
    m.license = scalar("license"); m.homepage = scalar("homepage"); m.repository = scalar("repository");
    std::string lang = scalar("language"); if (!lang.empty()) m.language = lang;
    m.rin = scalar("rin"); if (m.rin.empty()) m.rin = scalar("engine");
    if (m.rin.empty()) if (auto* c = root.child("language")) if (auto* v = c->attr("version")) m.rin = *v;
    m.main = scalar("main");
    m.authors = list("authors", "author"); m.keywords = list("keywords", "keyword");
    m.exclude = list("exclude", "ignore");
    if (root.child("parts")) { m.hasParts = true; m.parts = list("parts", "part"); }
    else { auto p = list("parts", "part"); if (!p.empty()) { m.hasParts = true; m.parts = p; } }
    if (root.child("run")) { m.hasRun = true; if (!root.child("run")->kids.empty()) m.run = list("run", "file"); else if (!root.child("run")->text.empty()) m.run = {root.child("run")->text}; }
    auto xdeps = [&](const char* tag, std::vector<Dep>& out) {
        auto* d = root.child(tag); if (!d) return false;
        for (auto& k : d->kids) {
            std::string n = k.attr("name") ? *k.attr("name") : k.tag == "dependency" ? k.text : k.tag;
            std::string v = k.attr("version") ? *k.attr("version") : (k.attr("range") ? *k.attr("range") : (k.tag != "dependency" ? k.text : ""));
            out.push_back({n, v.empty() ? "*" : v});
        }
        return true;
    };
    xdeps("dependencies", m.dependencies);
    if (xdeps("devDependencies", m.devDependencies) | xdeps("optionalDependencies", m.optionalDependencies) | xdeps("peerDependencies", m.peerDependencies)) m.hasFormat2 = true;
    if (auto* sc = root.child("scripts")) {
        m.hasFormat2 = true;
        for (auto& k : sc->kids) m.scripts.emplace_back(k.attr("name") ? *k.attr("name") : k.tag, k.text);
    }
    if (auto* fe = root.child("features")) {
        m.hasFormat2 = true;
        for (auto& k : fe->kids) {
            std::string n = k.attr("name") ? *k.attr("name") : k.tag;
            std::vector<std::string> items;
            for (auto& it : k.kids) items.push_back(it.text);
            if (items.empty() && !k.text.empty()) items = splitCsv(k.text);
            if (n == "default") m.defaultFeatures = items; else m.features.emplace_back(n, items);
        }
    }
    if (root.child("files")) { m.files = list("files", "file"); m.hasFormat2 = true; }
    m.categories = list("categories", "category"); m.platforms = list("platforms", "platform");
    m.workspaces = list("workspaces", "workspace"); m.contributors = list("contributors", "contributor");
    m.bugs = scalar("bugs"); m.readme = scalar("readme"); m.funding = scalar("funding");
    { std::string pv = scalar("private"); if (!pv.empty()) { m.isPrivate = (pv == "true" || pv == "1" || pv == "yes"); m.hasFormat2 = true; } }
    if (!m.categories.empty() || !m.platforms.empty() || !m.workspaces.empty() || !m.contributors.empty() || !m.bugs.empty() || !m.funding.empty() || !m.readme.empty()) m.hasFormat2 = true;
    if (auto* n = root.child("native")) {
        NativeSpec& ns = m.native;
        for (auto& k : n->kids) {
            if (k.tag == "source") ns.sources.push_back(k.text);
            else if (k.tag == "flag") ns.flags.push_back(k.text);
            else if (k.tag == "lib") ns.libs.push_back(k.text);
            else if (k.tag == "include") ns.includes.push_back(k.text);
            else if (k.tag == "function") {
                FnSpec f; f.name = k.attr("name") ? *k.attr("name") : k.text;
                if (auto* p = k.attr("params")) { f.hasParams = true; f.params = splitCsv(*p); }
                ns.functions.push_back(std::move(f));
            }
        }
        if (auto* s = n->attr("std")) ns.std = *s;
        if (auto* r = n->attr("required")) ns.required = (*r == "true" || *r == "1" || *r == "yes");
    }
    return true;
}

// Disconom بصيغة TOML (disconom.toml): نفس مفاتيح JSON تماماً، بجداول [dependencies] [scripts] [features] [native] …
inline bool manifestFromToml(const std::string& text, Manifest& m, std::vector<std::string>& errs) {
    J j; std::string e;
    if (!parseToml(text, j, e)) { errs.push_back(e); return false; }
    if (!manifestFromJson(j, m, errs)) return false;
    m.formatName = "toml";
    return true;
}

// الشكل الموحَّد (normalized) للمانيفست كـ JSON — يستعمله rinlib.manifest و`rin lib info --json`.
inline J manifestToJson(const Manifest& m) {
    J j = J::object();
    j.set("disconom", J::number(m.format));
    j.set("name", J::string(m.name)); j.set("version", J::string(m.version));
    j.set("description", J::string(m.description));
    j.set("language", J::string(m.language));
    j.set("rin", J::string(m.rin));
    auto arr = [](const std::vector<std::string>& v) { J a = J::array(); for (auto& s : v) a.push(J::string(s)); return a; };
    j.set("authors", arr(m.authors)); j.set("license", J::string(m.license));
    j.set("homepage", J::string(m.homepage)); j.set("repository", J::string(m.repository));
    j.set("keywords", arr(m.keywords));
    j.set("main", J::string(m.main)); j.set("parts", arr(m.parts)); j.set("exclude", arr(m.exclude)); j.set("run", arr(m.run));
    auto depObj = [](const std::vector<Dep>& v) { J o = J::object(); for (auto& d : v) o.set(d.name, J::string(d.range)); return o; };
    j.set("dependencies", depObj(m.dependencies));
    if (!m.devDependencies.empty()) j.set("devDependencies", depObj(m.devDependencies));
    if (!m.optionalDependencies.empty()) j.set("optionalDependencies", depObj(m.optionalDependencies));
    if (!m.peerDependencies.empty()) j.set("peerDependencies", depObj(m.peerDependencies));
    if (!m.scripts.empty()) { J o = J::object(); for (auto& kv : m.scripts) o.set(kv.first, J::string(kv.second)); j.set("scripts", o); }
    if (!m.features.empty() || !m.defaultFeatures.empty()) {
        J o = J::object();
        if (!m.defaultFeatures.empty()) o.set("default", arr(m.defaultFeatures));
        for (auto& kv : m.features) o.set(kv.first, arr(kv.second));
        j.set("features", o);
    }
    if (!m.files.empty()) j.set("files", arr(m.files));
    if (!m.categories.empty()) j.set("categories", arr(m.categories));
    if (!m.platforms.empty()) j.set("platforms", arr(m.platforms));
    if (!m.workspaces.empty()) j.set("workspaces", arr(m.workspaces));
    if (!m.contributors.empty()) j.set("contributors", arr(m.contributors));
    if (!m.bugs.empty()) j.set("bugs", J::string(m.bugs));
    if (!m.readme.empty()) j.set("readme", J::string(m.readme));
    if (!m.funding.empty()) j.set("funding", J::string(m.funding));
    if (m.isPrivate) j.set("private", J::boolean(true));
    J nat = J::object();
    nat.set("sources", arr(m.native.sources)); nat.set("std", J::string(m.native.std));
    nat.set("required", J::boolean(m.native.required));
    if (!m.native.flags.empty()) nat.set("flags", arr(m.native.flags));
    if (!m.native.libs.empty()) nat.set("libs", arr(m.native.libs));
    if (!m.native.includes.empty()) nat.set("includes", arr(m.native.includes));
    J fns = J::array();
    for (auto& f : m.native.functions) {
        J fo = J::object(); fo.set("name", J::string(f.name));
        if (f.hasParams) fo.set("params", arr(f.params));
        fns.push(fo);
    }
    nat.set("functions", fns);
    j.set("native", nat);
    return j;
}

namespace detail {
inline bool validIdent(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}
inline bool hasSuffix(const std::string& s, const std::string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }
inline std::string baseNameOf(const std::string& p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? p : p.substr(k + 1); }
inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline std::string stripDotSlashLite(std::string p) { while (p.rfind("./", 0) == 0) p.erase(0, 2); return p; }
inline bool safeRelPath(const std::string& rel) {   // يرفض المسارات المطلقة والهروب بـ ..
    if (rel.empty() || rel[0] == '/' || rel[0] == '\\' || rel.find(':') != std::string::npos || rel.find('\\') != std::string::npos) return false;
    std::stringstream ss(rel); std::string seg;
    while (std::getline(ss, seg, '/')) if (seg == "..") return false;
    return true;
}
} // namespace detail

// engineVersion فارغ = لا تتحقق من توافق اللغة (يُستعمل في الفحص الساكن).
inline void validateManifest(const Manifest& m, const std::string& dirBase, const std::string& engineVersion,
                             std::vector<std::string>& errs, std::vector<std::string>& warns) {
    using namespace detail;
    if (m.format < 1 || m.format > kDisconomFormat)
        errs.push_back("unsupported Disconom format version " + std::to_string(m.format) + " (this engine understands up to " + std::to_string(kDisconomFormat) + ")");
    if (m.name.empty()) errs.push_back("Disconom: 'name' is required");
    else if (!validIdent(m.name)) errs.push_back("Disconom: invalid library name '" + m.name + "' (letters, digits, '_', '-', '.' only; must not start with a digit)");
    else if (!dirBase.empty() && m.name != dirBase) warns.push_back("Disconom name '" + m.name + "' differs from the folder name '" + dirBase + kLibDirSuffix + "'");
    SemVer sv;
    if (m.version.empty()) errs.push_back("Disconom: 'version' is required (semver, e.g. 1.0.0)");
    else if (!parseSemVer(m.version, sv)) errs.push_back("Disconom: invalid version '" + m.version + "' (expected semver like 1.2.3)");
    if (lower(m.language) != "rin") errs.push_back("Disconom: language '" + m.language + "' is not supported (expected \"rin\")");
    if (m.rin.empty()) warns.push_back("Disconom: no Rin language version declared ('rin': \">=1.0.0\") — recommended so incompatible engines fail clearly");
    else if (!validRange(m.rin)) errs.push_back("Disconom: invalid Rin version constraint '" + m.rin + "'");
    else if (!engineVersion.empty() && !satisfies(engineVersion, m.rin))
        errs.push_back("library '" + m.name + "' " + m.version + " requires Rin " + m.rin + " but this engine is " + engineVersion);
    if (m.description.empty()) warns.push_back("Disconom: no 'description'");
    if (m.license.empty()) warns.push_back("Disconom: no 'license'");
    if (!m.main.empty() && !safeRelPath(m.main)) errs.push_back("Disconom: unsafe 'main' path '" + m.main + "'");
    for (auto& d : m.dependencies) {
        if (!validIdent(d.name)) errs.push_back("Disconom: invalid dependency name '" + d.name + "'");
        else if (!validRange(d.range)) errs.push_back("Disconom: invalid version range '" + d.range + "' for dependency '" + d.name + "'");
        if (d.name == m.name && !m.name.empty()) errs.push_back("Disconom: a library cannot depend on itself");
    }
    // ---- Disconom 2: تبعيات التطوير/الاختيارية/النظيرة ----
    std::set<std::string> seenDeps;
    for (auto& d : m.dependencies) seenDeps.insert(d.name);
    auto checkDeps = [&](const std::vector<Dep>& v, const char* label) {
        std::set<std::string> local;
        for (auto& d : v) {
            if (!validIdent(d.name)) errs.push_back(std::string("Disconom: invalid ") + label + " name '" + d.name + "'");
            else if (!validRange(d.range)) errs.push_back("Disconom: invalid version range '" + d.range + "' for " + label + " '" + d.name + "'");
            if (d.name == m.name && !m.name.empty()) errs.push_back(std::string("Disconom: a library cannot list itself in ") + label);
            if (!local.insert(d.name).second) warns.push_back(std::string("Disconom: duplicate ") + label + " '" + d.name + "'");
            else if (seenDeps.count(d.name)) warns.push_back("Disconom: '" + d.name + "' is declared in more than one dependency section (" + label + " and another)");
        }
        for (auto& d : v) seenDeps.insert(d.name);
    };
    {
        std::set<std::string> dup;
        for (auto& d : m.dependencies) if (!dup.insert(d.name).second) warns.push_back("Disconom: duplicate dependency '" + d.name + "'");
    }
    checkDeps(m.optionalDependencies, "optionalDependency");
    checkDeps(m.peerDependencies, "peerDependency");
    checkDeps(m.devDependencies, "devDependency");
    if (m.format < 2 && m.hasFormat2)
        warns.push_back("Disconom: uses format-2 fields (devDependencies/scripts/features/files/private/...) but declares \"disconom\": " + std::to_string(m.format) + " — set it to 2");
    for (auto& kv : m.scripts) {
        bool okName = !kv.first.empty();
        for (char c : kv.first) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':' || c == '.')) okName = false;
        if (!okName) errs.push_back("Disconom: invalid script name '" + kv.first + "' (letters, digits, '_', '-', ':', '.')");
        if (trimStr(kv.second).empty()) errs.push_back("Disconom: script '" + kv.first + "' has an empty command");
    }
    {   // features: عناصرها = تبعيات اختيارية | ميزات أخرى | dep:<name> ؛ بلا دورات؛ default يجب أن تكون ميزات موجودة
        std::set<std::string> fnames, optNames;
        for (auto& f : m.features) {
            if (!fnames.insert(f.first).second) errs.push_back("Disconom: duplicate feature '" + f.first + "'");
            if (!validIdent(f.first)) errs.push_back("Disconom: invalid feature name '" + f.first + "'");
        }
        for (auto& d : m.optionalDependencies) optNames.insert(d.name);
        for (auto& f : m.features)
            for (auto& it : f.second) {
                std::string n = it.rfind("dep:", 0) == 0 ? it.substr(4) : it;
                if (!fnames.count(n) && !optNames.count(n))
                    errs.push_back("Disconom: feature '" + f.first + "' references '" + it + "' which is neither a feature nor an optional dependency");
            }
        for (auto& d : m.defaultFeatures) if (!fnames.count(d)) errs.push_back("Disconom: default feature '" + d + "' is not defined in 'features'");
        std::map<std::string, int> color;   // 0 = لم تُزَر، 1 = قيد الزيارة، 2 = انتهت
        std::function<bool(const std::string&)> cyc = [&](const std::string& n) -> bool {
            int& c = color[n];
            if (c == 1) return true;
            if (c == 2) return false;
            c = 1;
            for (auto& f : m.features) if (f.first == n) for (auto& it : f.second) if (fnames.count(it) && cyc(it)) return true;
            color[n] = 2; return false;
        };
        for (auto& f : m.features) if (cyc(f.first)) { errs.push_back("Disconom: feature cycle involving '" + f.first + "'"); break; }
    }
    for (auto& f : m.files) if (!safeRelPath(stripDotSlashLite(f))) errs.push_back("Disconom: unsafe 'files' path '" + f + "'");
    for (auto& w : m.workspaces) if (!safeRelPath(stripDotSlashLite(w))) errs.push_back("Disconom: unsafe 'workspaces' path '" + w + "'");
    if (!m.readme.empty() && !safeRelPath(m.readme)) errs.push_back("Disconom: unsafe 'readme' path '" + m.readme + "'");
    {
        static const std::set<std::string> kPlat = {"linux", "android", "windows", "macos", "ios", "web", "wasm"};
        for (auto& p : m.platforms) if (!kPlat.count(lower(p))) warns.push_back("Disconom: unknown platform '" + p + "' (linux, android, windows, macos, ios, web, wasm)");
    }
    auto isUrl = [](const std::string& u) { return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0 || u.rfind("git+", 0) == 0 || u.rfind("git@", 0) == 0 || u.rfind("ssh://", 0) == 0; };
    if (!m.homepage.empty() && !isUrl(m.homepage)) warns.push_back("Disconom: 'homepage' is not a URL: " + m.homepage);
    if (!m.repository.empty() && !isUrl(m.repository)) warns.push_back("Disconom: 'repository' is not a URL: " + m.repository);
    if (!m.bugs.empty() && !isUrl(m.bugs) && m.bugs.find('@') == std::string::npos) warns.push_back("Disconom: 'bugs' is neither a URL nor an e-mail: " + m.bugs);
    if (!m.license.empty()) {
        bool okLic = true;
        for (char c : m.license) if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '+' || c == ' ' || c == '(' || c == ')')) okLic = false;
        if (!okLic && m.license.rfind("SEE LICENSE", 0) != 0) warns.push_back("Disconom: 'license' is not a valid SPDX expression: " + m.license);
    }
    if (m.name.size() > 64) errs.push_back("Disconom: library name longer than 64 characters");
    if (m.keywords.size() > 20) warns.push_back("Disconom: more than 20 keywords");
    if (m.description.size() > 400) warns.push_back("Disconom: 'description' is longer than 400 characters");
    if (m.isPrivate && m.name.empty() == false && m.repository.empty() && m.homepage.empty()) { /* خاصة: لا تحذيرات إضافية */ }
    for (auto& s : m.native.sources) if (!safeRelPath(s)) errs.push_back("Disconom: unsafe native source path '" + s + "'");
    for (auto& f : m.native.functions) if (!validIdent(f.name) || f.name.find('.') != std::string::npos || f.name.find('-') != std::string::npos)
        errs.push_back("Disconom: invalid native function name '" + f.name + "' (must be a valid Rin identifier)");
}

// ---- الميزات (features) ----------------------------------------------------
// الميزات المفعّلة = default ∪ extra، موسَّعة تعدّياً (ميزة تحوي ميزات أخرى). تُرجع أسماء الميزات فقط.
inline std::set<std::string> activeFeatures(const Manifest& m, const std::set<std::string>& extra = {}, bool useDefault = true) {
    std::map<std::string, std::vector<std::string>> byName;
    for (auto& f : m.features) byName[f.first] = f.second;
    std::set<std::string> on;
    std::vector<std::string> work;
    if (useDefault) for (auto& d : m.defaultFeatures) work.push_back(d);
    for (auto& e : extra) work.push_back(e);
    while (!work.empty()) {
        std::string f = work.back(); work.pop_back();
        if (!byName.count(f) || !on.insert(f).second) continue;
        for (auto& it : byName[f]) if (byName.count(it)) work.push_back(it);
    }
    return on;
}
// التبعيات الاختيارية المفعَّلة: التي لا تذكرها أي ميزة تُفعَّل دائماً (وتُحمَّل إن وُجدت)،
// والتي تذكرها ميزة تُفعَّل فقط إذا كانت ميزة مفعّلة تشير إليها (name أو dep:name).
inline std::vector<Dep> enabledOptionalDeps(const Manifest& m, const std::set<std::string>& extraFeatures = {}) {
    std::set<std::string> mentioned, enabled;
    for (auto& f : m.features) for (auto& it : f.second) mentioned.insert(it.rfind("dep:", 0) == 0 ? it.substr(4) : it);
    std::set<std::string> on = activeFeatures(m, extraFeatures);
    for (auto& f : m.features) if (on.count(f.first)) for (auto& it : f.second) enabled.insert(it.rfind("dep:", 0) == 0 ? it.substr(4) : it);
    std::vector<Dep> out;
    for (auto& d : m.optionalDependencies) if (!mentioned.count(d.name) || enabled.count(d.name)) out.push_back(d);
    return out;
}

// ============================================================================
//  نظام ملفات مُجرَّد (قرص حقيقي أو ملفات افتراضية داخل صورة rin_vm) + خطة التحميل
// ============================================================================
struct FS {
    std::function<bool(const std::string&)> isDir;                                // مسار منطقي
    std::function<bool(const std::string&, std::string&)> read;
    std::function<void(const std::string&, std::vector<std::string>&)> listFiles; // كل الملفات تحت مجلد (متكرر)، مسارات منطقية
};

// قرص حقيقي: المسار المنطقي = المسار الحقيقي (نسبةً إلى CWD أو مطلق).
inline FS diskFS() {
    namespace fs = std::filesystem;
    FS f;
    f.isDir = [](const std::string& p) { std::error_code ec; return fs::is_directory(p, ec); };
    f.read = [](const std::string& p, std::string& out) {
        std::error_code ec; if (fs::is_directory(p, ec)) return false;
        std::ifstream in(p, std::ios::binary); if (!in) return false;
        std::ostringstream b; b << in.rdbuf(); out = b.str(); return true;
    };
    f.listFiles = [](const std::string& dir, std::vector<std::string>& out) {
        std::error_code ec;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2)) out.push_back(it->path().generic_string());
        }
    };
    return f;
}

struct Plan {
    std::string root;            // مسار منطقي للمجلد، مثل lib/math.og.rin
    std::string dirName;         // math.og.rin
    std::string dirBase;         // math
    Manifest manifest;
    bool hasManifest = false;
    std::string manifestFile;    // مسار نسبي: disconom.json | .xml | .toml
    std::vector<std::string> parts;      // مسارات نسبية بترتيب التحميل
    std::vector<std::string> runFiles;   // مسارات نسبية بترتيب التنفيذ (بعد كل الأجزاء)
    std::vector<std::string> files;      // كل ملفات المكتبة (نسبية)
    std::vector<std::string> warnings;
};

namespace detail {
inline bool globMatch(const std::string& pat, const std::string& str, size_t pi = 0, size_t si = 0) {
    while (pi < pat.size()) {
        char c = pat[pi];
        if (c == '*') {
            bool dbl = (pi + 1 < pat.size() && pat[pi + 1] == '*');
            size_t next = pi + (dbl ? 2 : 1);
            if (dbl && next < pat.size() && pat[next] == '/') ++next;   // "**/x" يطابق "x" أيضاً
            for (size_t k = si; k <= str.size(); ++k) {
                if (globMatch(pat, str, next, k)) return true;
                if (k < str.size() && !dbl && str[k] == '/') break;
            }
            return false;
        }
        if (si >= str.size()) return false;
        if (c == '?') { if (str[si] == '/') return false; }
        else if (c != str[si]) return false;
        ++pi; ++si;
    }
    return si == str.size();
}
inline bool hasGlob(const std::string& p) { return p.find_first_of("*?") != std::string::npos; }
inline std::string stripDotSlash(std::string p) { while (p.rfind("./", 0) == 0) p.erase(0, 2); return p; }
inline bool inExcludedDir(const std::string& rel) {
    static const std::set<std::string> kSkip = {"native", "tests", "test", "examples", "example", "docs", "doc", "build", "node_modules"};
    std::stringstream ss(rel); std::string seg; std::vector<std::string> segs;
    while (std::getline(ss, seg, '/')) segs.push_back(seg);
    for (size_t i = 0; i + 1 < segs.size(); ++i)   // مكوّنات المجلدات فقط (لا اسم الملف)
        if (kSkip.count(segs[i]) || (!segs[i].empty() && segs[i][0] == '.')) return true;
    return false;
}
} // namespace detail

// يبني خطة تحميل المكتبة: يقرأ Disconom، يتحقق منه، يكتشف الأجزاء .zpp وملفات .rinz ويرتّبها.
// engineVersion فارغ = بلا فحص توافق اللغة. ترجع true عند غياب أخطاء.
inline bool planLibrary(const FS& fsys, const std::string& root, const std::string& engineVersion,
                        Plan& plan, std::vector<std::string>& errs) {
    using namespace detail;
    std::string r = root;
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    plan.root = r;
    plan.dirName = baseNameOf(r);
    plan.dirBase = hasSuffix(plan.dirName, kLibDirSuffix) ? plan.dirName.substr(0, plan.dirName.size() - std::char_traits<char>::length(kLibDirSuffix)) : plan.dirName;
    if (!fsys.isDir(r)) { errs.push_back("library folder not found: " + r); return false; }

    std::vector<std::string> abs;
    fsys.listFiles(r, abs);
    std::string prefix = r + "/";
    for (auto& a : abs) {
        std::string rel = a.rfind(prefix, 0) == 0 ? a.substr(prefix.size()) : a;
        plan.files.push_back(rel);
    }
    std::sort(plan.files.begin(), plan.files.end());

    // ---- Disconom ----
    std::string mf;
    for (const char* want : kManifestFiles) {
        for (auto& f : plan.files) if (lower(f) == want) { mf = f; break; }
        if (!mf.empty()) break;
    }
    size_t errBefore = errs.size();
    if (mf.empty()) {
        plan.warnings.push_back("no Disconom file (disconom.json / disconom.xml / disconom.toml) — defaults used; add one to declare name, version and the Rin version");
        plan.manifest.name = plan.dirBase; plan.manifest.version = "0.0.0"; plan.manifest.formatName = "none";
    } else {
        plan.hasManifest = true; plan.manifestFile = mf; plan.manifest.file = mf;
        std::string text;
        if (!fsys.read(r + "/" + mf, text)) errs.push_back("cannot read " + mf);
        else if (lower(mf) == "disconom.json") {
            J j; std::string e;
            if (!parseJson(text, j, e)) errs.push_back(mf + ": " + e);
            else manifestFromJson(j, plan.manifest, errs);
        } else if (lower(mf) == "disconom.toml") {
            std::vector<std::string> te;
            if (!manifestFromToml(text, plan.manifest, te)) for (auto& e : te) errs.push_back(mf + ": " + e);
            else for (auto& e : te) errs.push_back(e);
        } else {
            X x; std::string e;
            if (!parseXml(text, x, e)) errs.push_back(mf + ": " + e);
            else manifestFromXml(x, plan.manifest, errs);
        }
        if (errs.size() == errBefore) validateManifest(plan.manifest, plan.dirBase, engineVersion, errs, plan.warnings);
        for (auto& f : plan.files) if (f != mf) for (const char* other : kManifestFiles)
            if (lower(f) == other) { plan.warnings.push_back(std::string("both ") + mf + " and " + f + " exist — using " + mf); break; }
    }
    if (errs.size() != errBefore) return false;
    const Manifest& m = plan.manifest;

    // ---- exclude ----
    auto excludedByManifest = [&](const std::string& rel) {
        for (auto& pat : m.exclude) {
            std::string pp = stripDotSlash(pat);
            if (globMatch(pp, rel)) return true;
            if (!hasGlob(pp)) { std::string d = pp; while (!d.empty() && d.back() == '/') d.pop_back(); if (rel == d || rel.rfind(d + "/", 0) == 0) return true; }
        }
        return false;
    };

    // ---- الأجزاء ----
    std::set<std::string> seen;
    auto addPart = [&](const std::string& rel) { if (seen.insert(rel).second) plan.parts.push_back(rel); };
    std::vector<std::string> autoParts, autoRun;
    for (auto& f : plan.files) {
        if (inExcludedDir(f) || excludedByManifest(f)) continue;
        if (hasSuffix(f, kPartExt)) autoParts.push_back(f);
        else if (hasSuffix(f, kRunExt)) autoRun.push_back(f);
    }
    auto expand = [&](const std::string& rawPat, const char* what, std::vector<std::string>& dest, std::set<std::string>& dedup, const char* defExt) {
        std::string pat = stripDotSlash(rawPat);
        if (!safeRelPath(pat)) { errs.push_back(std::string("Disconom: unsafe ") + what + " path '" + rawPat + "'"); return; }
        std::vector<std::string> hits;
        if (hasGlob(pat)) {
            for (auto& f : plan.files) if (globMatch(pat, f) && f != plan.manifestFile) hits.push_back(f);
        } else {
            bool isFile = std::find(plan.files.begin(), plan.files.end(), pat) != plan.files.end();
            if (isFile) hits.push_back(pat);
            else {
                std::string d = pat; while (!d.empty() && d.back() == '/') d.pop_back();
                for (auto& f : plan.files) if (f.rfind(d + "/", 0) == 0 && hasSuffix(f, defExt)) hits.push_back(f);
            }
        }
        if (hits.empty()) {
            if (hasGlob(pat)) plan.warnings.push_back(std::string("Disconom: ") + what + " pattern '" + rawPat + "' matched no files");
            else errs.push_back(std::string("Disconom: ") + what + " '" + rawPat + "' not found in the library folder");
        }
        for (auto& h : hits) if (dedup.insert(h).second) dest.push_back(h);
    };
    if (m.hasParts) {
        for (auto& pat : m.parts) expand(pat, "part", plan.parts, seen, kPartExt);
    } else {
        std::string mainRel = stripDotSlash(m.main.empty() ? std::string("main") + kPartExt : m.main);
        bool mainExists = std::find(plan.files.begin(), plan.files.end(), mainRel) != plan.files.end();
        if (!m.main.empty() && !mainExists) errs.push_back("Disconom: main part '" + m.main + "' not found");
        if (mainExists) addPart(mainRel);
        for (auto& f : autoParts) addPart(f);
    }
    // ---- ملفات التشغيل .rinz ----
    std::set<std::string> seenRun;
    if (m.hasRun) {
        for (auto& pat : m.run) expand(pat, "run file", plan.runFiles, seenRun, kRunExt);
    } else {
        auto it = std::find_if(autoRun.begin(), autoRun.end(), [](const std::string& f) { return baseNameOf(f) == "run.rinz" && f.find('/') == std::string::npos; });
        if (it != autoRun.end()) { plan.runFiles.push_back(*it); seenRun.insert(*it); }
        for (auto& f : autoRun) if (seenRun.insert(f).second) plan.runFiles.push_back(f);
    }
    // ---- الكود الأصلي ----
    for (auto& s : m.native.sources) {
        if (hasGlob(s)) continue;
        if (std::find(plan.files.begin(), plan.files.end(), stripDotSlash(s)) == plan.files.end())
            errs.push_back("Disconom: native source '" + s + "' not found in the library folder");
    }
    if (plan.parts.empty() && plan.runFiles.empty() && !m.native.declared())
        errs.push_back("library has no " + std::string(kPartExt) + " parts (expected main" + kPartExt + ") and no " + kRunExt + " files");
    else if (plan.parts.empty()) plan.warnings.push_back(std::string("no ") + kPartExt + " parts — the library only has run files and/or native code");
    return errs.empty();
}

// ============================================================================
//  مولِّد هيكل مكتبة جديدة:  <parent>/<name>.og.rin/
// ============================================================================
struct ScaffoldOpts {
    std::string name, version = "0.1.0", description, author, license = "MIT";
    bool xml = false;       // disconom.xml بدل disconom.json
    bool toml = false;      // disconom.toml بدل disconom.json
    bool native = false;    // أضف native/helper.cpp
};

inline bool scaffoldLibrary(const std::string& parentDir, const ScaffoldOpts& o, const std::string& engineVersion,
                            std::string& outRoot, std::vector<std::string>& created, std::string& err) {
    namespace fs = std::filesystem;
    using namespace detail;
    if (!validIdent(o.name) || o.name.find('.') != std::string::npos || o.name.find('-') != std::string::npos) {
        err = "invalid library name '" + o.name + "' (use letters, digits and '_', not starting with a digit)"; return false;
    }
    SemVer sv; if (!parseSemVer(o.version, sv)) { err = "invalid version '" + o.version + "' (expected semver like 0.1.0)"; return false; }
    std::string root = (parentDir.empty() ? std::string(".") : parentDir) + "/" + o.name + kLibDirSuffix;
    std::error_code ec;
    if (fs::exists(root, ec)) { err = "already exists: " + root; return false; }
    fs::create_directories(root, ec);
    if (ec) { err = "cannot create " + root + ": " + ec.message(); return false; }
    auto put = [&](const std::string& rel, const std::string& text) {
        std::string full = root + "/" + rel;
        std::error_code e2; fs::create_directories(fs::path(full).parent_path(), e2);
        std::ofstream out(full, std::ios::binary);
        if (!out) return false;
        out << text; created.push_back(full); return true;
    };
    const std::string N = o.name;
    std::string rinReq = ">=" + (engineVersion.empty() ? std::string("1.0.0") : engineVersion);
    std::string desc = o.description.empty() ? ("مكتبة " + N) : o.description;

    // ---- Disconom ----
    if (!o.xml) {
        J j = J::object();
        j.set("disconom", J::number(kDisconomFormat));
        j.set("name", J::string(N)); j.set("version", J::string(o.version));
        j.set("description", J::string(desc));
        j.set("rin", J::string(rinReq));
        J au = J::array(); if (!o.author.empty()) au.push(J::string(o.author)); j.set("authors", au);
        j.set("license", J::string(o.license));
        j.set("main", J::string("main.zpp"));
        j.set("dependencies", J::object());
        { J sc = J::object(); sc.set("test", J::string("rin tests/basic.rin")); j.set("scripts", sc); }
        if (o.native) {
            J nat = J::object();
            J src = J::array(); src.push(J::string("native/helper.cpp")); nat.set("sources", src);
            nat.set("std", J::string("c++17")); nat.set("required", J::boolean(false));
            J fn = J::array(); { J f = J::object(); f.set("name", J::string(N + "_nativeSum")); J pr = J::array(); pr.push(J::string("numbers")); f.set("params", pr); fn.push(f); }
            nat.set("functions", fn);
            j.set("native", nat);
        }
        if (o.toml) put("disconom.toml", dumpToml(j));
        else put("disconom.json", dumpJson(j, 2) + "\n");
    } else {
        std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<disconom format=\"" + std::to_string(kDisconomFormat) + "\">\n";
        x += "  <name>" + N + "</name>\n  <version>" + o.version + "</version>\n  <description>" + desc + "</description>\n";
        x += "  <rin>" + std::string(rinReq.rfind(">=", 0) == 0 ? "&gt;=" + rinReq.substr(2) : rinReq) + "</rin>\n";
        x += "  <authors>" + (o.author.empty() ? std::string() : "<author>" + o.author + "</author>") + "</authors>\n";
        x += "  <license>" + o.license + "</license>\n  <main>main.zpp</main>\n  <dependencies/>\n";
        if (o.native)
            x += "  <native std=\"c++17\" required=\"false\">\n    <source>native/helper.cpp</source>\n    <function name=\"" + N + "_nativeSum\" params=\"numbers\"/>\n  </native>\n";
        x += "</disconom>\n";
        put("disconom.xml", x);
    }
    // ---- الأجزاء ----
    put("main.zpp",
        "// " + N + " — الجزء الأساسي (main.zpp) من مكتبة " + N + ".og.rin\n"
        "// كل ملفات .zpp في هذا المجلد (بأي اسم وبأي مجلد فرعي) تُربط معاً في نطاق واحد كأنها ملف واحد.\n"
        "// استعمل `export` على ما يجب أن يظهر للمستورِد؛ عندها يصبح المُصدَّر فقط هو الواجهة العامة.\n\n"
        "export let " + N + "_VERSION = \"" + o.version + "\";\n\n"
        "export fun " + N + "_hello(who) {\n"
        "    return \"مرحباً \" + who + \" من " + N + "\";\n"
        "}\n");
    put("core/helpers.zpp",
        "// جزء إضافي في مجلد فرعي: أي اسم، أي مجلد — يُكتشف تلقائياً.\n\n"
        "export fun " + N + "_add(a, b) {\n"
        "    return a + b;\n"
        "}\n");
    put("run.rinz",
        "// run.rinz — ملف التشغيل: يُنفَّذ مرة واحدة بعد ربط كل أجزاء .zpp (تهيئة/تسجيل/فحص ذاتي).\n"
        "// يمكن أيضاً تشغيله مباشرةً:  rin run " + N + ".og.rin/run.rinz\n\n"
        "print \"[" + N + "] \" + " + N + "_VERSION + \" جاهزة\";\n");
    put("data/info.json", "{\n  \"name\": \"" + N + "\",\n  \"tags\": [\"rin\", \"library\"]\n}\n");
    if (o.native) {
        put("native/helper.cpp",
            "// كود C++ مساعد لمكتبة " + N + " — يُبنى ويُحمَّل تلقائياً عند الاستيراد إذا فُعِّل الكود الأصلي:\n"
            "//   rin --allow-native program.rin      (أو RIN_ALLOW_NATIVE=1)\n"
            "// عقد الدوال: RIN_FN(name) بوسائط JSON (args.arr) وتُرجع rin::Json. تُستدعى من Rin عبر\n"
            "// rinlib.native(\"" + N + "\", \"name\", [..]) أو عبر الغلاف التلقائي المُعلَن في Disconom (native.functions).\n"
            "#include \"rin_abi.h\"\n\n"
            "RIN_FN(" + N + "_nativeSum) {\n"
            "    double total = 0;\n"
            "    if (!args.arr.empty() && args.arr[0].type == rin::Json::Arr)\n"
            "        for (auto& v : args.arr[0].arr) total += v.num;\n"
            "    return rin::Json::number(total);\n"
            "}\n");
        // بديل Rin نقي: يعمل عند تعطيل الكود الأصلي؛ وعند تفعيله يحلّ غلاف الدالة الأصلية محلّه تلقائياً.
        put("core/fallback.zpp",
            "// بديل Rin نقي للدالة الأصلية " + N + "_nativeSum — يُستعمل عندما يكون الكود الأصلي معطَّلاً.\n"
            "// (عند تفعيل --allow-native يُركَّب غلاف الدالة الأصلية بعد كل الأجزاء فيحلّ محلّ هذا التعريف.)\n\n"
            "export fun " + N + "_nativeSum(numbers) {\n"
            "    let total = 0;\n"
            "    for (let n in numbers) { total = total + n; }\n"
            "    return total;\n"
            "}\n");
    }
    put("tests/basic.rin",
        "// اختبار أساسي: rin tests/basic.rin (من مجلد فيه lib/" + N + ".og.rin) أو rin lib check " + N + ".og.rin\n"
        "@import \"" + N + "\";\n"
        "print " + N + "_hello(\"عالم\");\n"
        "print " + N + "_add(2, 3);\n");
    put("README.md",
        "# " + N + " " + o.version + "\n\n" + desc + "\n\n"
        "مكتبة Rin مقسّمة (`" + N + ".og.rin/`):\n\n"
        "| الملف | الدور |\n|---|---|\n"
        "| `disconom." + std::string(o.xml ? "xml" : o.toml ? "toml" : "json") + "` | وصف المكتبة: الاسم، الإصدار، إصدار Rin المطلوب، التبعيات |\n"
        "| `main.zpp` | الجزء الأساسي |\n| `core/*.zpp` | أجزاء إضافية (أي اسم/مجلد) |\n"
        "| `run.rinz` | ملف تشغيل/تهيئة بعد ربط الأجزاء |\n| `data/*.json\\|xml` | بيانات تقرؤها المكتبة عبر `rinlib.json/xml` |\n"
        + std::string(o.native ? "| `native/*.cpp` | كود C++ مساعد (يتطلب `--allow-native`) |\n" : "") +
        "\nالاستيراد:\n\n```rin\n@import \"" + N + "\";\nprint " + N + "_hello(\"Rin\");\n```\n");
    outRoot = root;
    return true;
}

} // namespace libdir
} // namespace rin
