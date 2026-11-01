// ============================================================================
//  rin_vm.h — مكتبة صورة الآلة الافتراضية لـ Rin (RVM)         [header-only، C++17]
// ============================================================================
//  تحوّل برنامج Rin (مع كل ما يستورده) إلى **صورة VM** واحدة في ثوانٍ: ملف `.rvm` أو حمولة
//  تُلحَق بتنفيذي. الصورة مغلقة ذاتياً:
//     • مصدر البرنامج الرئيسي
//     • كل ملفات @import / import…from المحلولة وقت البناء (تُنفَّذ بلا ملفات مرافقة)
//     • اختيارياً: بايتكود RBC1 جاهز لآلة toolchain/BytecodeVM السريعة (للنواة البسيطة)
//  التنفيذ: `rin image prog.rvm` أو التنفيذي الناتج عن `rin_ava -fvm`. إن حملت الصورة بايتكود
//  تُشغَّل بآلة البايتكود؛ وإلا بالمحرّك الكامل rin_interpreter (كل اللغة) مع ملفات الاستيراد الافتراضية
//  (Interpreter::setVirtualFiles). لا تعتمد المكتبة على المحرّك نفسه، فتُستخدم في أدوات خفيفة.
//
//  تخطيط الملف (كل الأعداد u32 little-endian):
//     "RVM1" | flags | name | main | bytecode | nfiles | (key, data)* | fnv1a64(الباقي)
//     كل نص = [u32 len][bytes]
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace rin_vm {

constexpr char kMagic[5] = "RVM1";
constexpr uint32_t kMaxBlob = 1u << 28;   // 256MB حدّ أمان عند القراءة

struct Image {
    std::string name;                           // اسم الملف المعروض في الأخطاء
    std::string main;                           // مصدر Rin الرئيسي
    std::string bytecode;                       // RBC1 اختياري (فارغ = المحرّك الكامل)
    std::map<std::string, std::string> files;   // ملفات الاستيراد الافتراضية: libPath → مصدر
    std::vector<std::string> unresolved;        // استيرادات لم تُوجَد وقت البناء (مدمجة/حزم: تُحلّ وقت التشغيل)
};

// ----------------------------------------------------------------------------- تسلسل
namespace detail {
inline void putU32(std::string& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); }
inline void putStr(std::string& o, const std::string& s) { putU32(o, static_cast<uint32_t>(s.size())); o += s; }
inline uint64_t fnv1a(const char* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= static_cast<unsigned char>(p[i]); h *= 1099511628211ULL; }
    return h;
}
struct Reader {
    const std::string& b; size_t pos = 0; bool ok = true;
    explicit Reader(const std::string& s, size_t start = 0) : b(s), pos(start) {}
    uint32_t u32() {
        if (pos + 4 > b.size()) { ok = false; return 0; }
        uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[pos + i]);
        pos += 4; return v;
    }
    std::string str() {
        uint32_t n = u32();
        if (!ok || n > kMaxBlob || pos + n > b.size()) { ok = false; return {}; }
        std::string s = b.substr(pos, n); pos += n; return s;
    }
};
}  // namespace detail

inline std::string serialize(const Image& im) {
    std::string body;
    detail::putU32(body, im.bytecode.empty() ? 0u : 1u);   // flags: bit0 = يحمل بايتكود
    detail::putStr(body, im.name);
    detail::putStr(body, im.main);
    detail::putStr(body, im.bytecode);
    detail::putU32(body, static_cast<uint32_t>(im.files.size()));
    for (auto& kv : im.files) { detail::putStr(body, kv.first); detail::putStr(body, kv.second); }
    std::string out(kMagic, 4);
    out += body;
    uint64_t h = detail::fnv1a(body.data(), body.size());
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((h >> (8 * i)) & 0xff));
    return out;
}

inline bool deserialize(const std::string& bytes, Image& im, std::string* err = nullptr) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (bytes.size() < 4 + 4 + 8 || bytes.compare(0, 4, kMagic) != 0) return fail("not an RVM1 image");
    uint64_t want = 0;
    for (int i = 7; i >= 0; --i) want = (want << 8) | static_cast<unsigned char>(bytes[bytes.size() - 8 + i]);
    if (detail::fnv1a(bytes.data() + 4, bytes.size() - 12) != want) return fail("image is corrupt (checksum mismatch)");
    std::string body = bytes.substr(4, bytes.size() - 12);
    detail::Reader r(body);
    r.u32();                       // flags (محجوز؛ الحقول مُرمَّزة بذاتها)
    Image t;
    t.name = r.str(); t.main = r.str(); t.bytecode = r.str();
    uint32_t n = r.u32();
    for (uint32_t i = 0; r.ok && i < n; ++i) { std::string k = r.str(); t.files[k] = r.str(); }
    if (!r.ok) return fail("image is truncated");
    im = std::move(t);
    return true;
}

// ----------------------------------------------------------------------------- ملفات
inline bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
inline bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}
inline bool fileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
inline std::string dirOf(const std::string& p) {
    auto s = p.find_last_of('/');
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

// ----------------------------------------------------------------------------- الاستيرادات
// يستخرج مسارات الاستيراد الحرفية: @import "x" / @import("x") / import {…} from "x" / use … from "x".
// يتجاهل الأسطر المعلَّقة (// أو #). الاستيرادات غير الحرفية (تعبيرات) تبقى للتشغيل.
inline std::vector<std::string> scanImports(const std::string& src) {
    std::vector<std::string> out;
    std::istringstream ss(src);
    std::string line;
    auto quoted = [](const std::string& l, size_t from, std::string& v) {
        size_t q = l.find_first_of("\"'", from);
        if (q == std::string::npos) return false;
        size_t e = l.find(l[q], q + 1);
        if (e == std::string::npos) return false;
        v = l.substr(q + 1, e - q - 1);
        return true;
    };
    while (std::getline(ss, line)) {
        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos) continue;
        std::string t = line.substr(i);
        if (t.rfind("//", 0) == 0 || t[0] == '#') continue;
        std::string v;
        if (t.rfind("@import", 0) == 0) { if (quoted(t, 7, v)) out.push_back(v); }
        else if (t.rfind("import", 0) == 0 || t.rfind("use", 0) == 0) {
            size_t f = t.find(" from ");
            if (f != std::string::npos && quoted(t, f, v)) out.push_back(v);
        }
    }
    return out;
}

// مسار المكتبة كما يحسبه المفسّر: اسم عارٍ → lib/<name>.og.rin ، وإلا كما كُتب.
inline std::string libPathFor(const std::string& raw) {
    if (raw.find('/') != std::string::npos) return raw;
    static const std::string ext = ".og.rin";
    std::string p = raw;
    if (p.size() < ext.size() || p.compare(p.size() - ext.size(), ext.size(), ext) != 0) p += ext;
    return "lib/" + p;
}

inline void collectInto(Image& im, const std::string& src, const std::string& importerDir,
                        const std::vector<std::string>& roots, std::set<std::string>& seen, int depth = 0) {
    if (depth > 64) return;
    for (const std::string& raw : scanImports(src)) {
        std::string key = libPathFor(raw);
        if (seen.count(key)) continue;
        seen.insert(key);
        std::vector<std::string> cands;
        if (raw.rfind("./", 0) == 0 || raw.rfind("../", 0) == 0) cands.push_back(importerDir + raw);
        for (auto& r : roots) cands.push_back(r + key);
        cands.push_back(key);
        std::string data, found;
        for (auto& c : cands) if (readFile(c, data)) { found = c; break; }
        if (found.empty()) { im.unresolved.push_back(raw); continue; }
        im.files[key] = data;
        if (key != raw) im.files[raw] = data;   // المفسّر يفحص المفتاح المحسوب ثم المسار الحرفي
        collectInto(im, data, dirOf(found), roots, seen, depth + 1);
    }
}

// يبني صورة من مصدر رئيسي: يحزم كل الاستيرادات الحرفية المتاحة على القرص (جذور البحث: مجلد الملف ثم cwd).
inline Image build(const std::string& name, const std::string& mainPath, const std::string& mainSrc,
                   const std::string& bytecode = std::string()) {
    Image im;
    im.name = name; im.main = mainSrc; im.bytecode = bytecode;
    std::set<std::string> seen;
    std::vector<std::string> roots;
    std::string d = dirOf(mainPath);
    if (!d.empty()) roots.push_back(d);
    roots.push_back("");
    collectInto(im, mainSrc, d, roots, seen);
    return im;
}

inline std::string describe(const Image& im) {
    std::ostringstream o;
    o << "name:      " << im.name << "\n"
      << "engine:    " << (im.bytecode.empty() ? "interpreter (full language)" : "bytecode VM") << "\n"
      << "main:      " << im.main.size() << " bytes\n"
      << "bytecode:  " << im.bytecode.size() << " bytes\n"
      << "imports:   " << im.files.size() << " bundled file key(s)\n";
    for (auto& kv : im.files) o << "  - " << kv.first << " (" << kv.second.size() << " bytes)\n";
    for (auto& u : im.unresolved) o << "  ? " << u << " (resolved at run time: embedded library or package)\n";
    return o.str();
}

}  // namespace rin_vm
