// ============================================================================
//  rin_libdir.cpp — تكامل «توكن الأداة OG» و«المكتبات» مع المفسّر (انظر rin_libdir.h و docs/split-libraries.md)
// ----------------------------------------------------------------------------
//  التسمية (v2):
//    توكن الأداة OG  = ملف واحد  <rinname>.og.rin          (ما كان يُسمّى «مكتبة Rin» القديمة)
//    مكتبة           = مجلد      <name>.og.rin/            (ملفات ومجلدات: Disconom + .zpp + .rinz + native/ + data/)
//
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp بنفس أسلوب rin_extra_natives*.cpp، فيدخل كل أهداف
//  البناء (APK / CLI / WASM / CI) تلقائياً بلا أي تعديل في ملفات البناء. يحتوي:
//    1) تصنيف الحزم وحلّها: مكتبة (مجلد) أم توكن أداة (ملف افتراضي/مدمج/قرص) — resolvePackage / listToolTokens
//    2) اكتشاف مجلد المكتبة من عبارة @import (قرص حقيقي أو ملفات افتراضية في صورة rin_vm)
//    3) تحميل الوحدة: قراءة Disconom، فحص إصدار اللغة والتبعيات، بناء/تحميل كود C++ المساعد،
//       تحليل كل .zpp/.rinz بالمحلّل الحقيقي ودمجها بالترتيب في قائمة عبارات واحدة — مع تراجع نظيف عند الفشل
//    4) رسم التبعيات (كشف الدورات، فحص الإصدارات، ترتيب التحميل) وبصمة SHA-256 لمحتوى المكتبة
//    5) فحص ساكن كامل (rin lib check / rinlib.check) للمكتبات ولتوكنات الأداة
//    6) rinlib.* — دوال وقت التشغيل: info/list/kind/resolve/tokens/libraries/packages/graph/fingerprint/verify/
//       manifest/read/json/xml/path/native/check/scaffold...
//
//  ما أُخذ من الملفات المرجعية الأخرى في المشروع (لا يوجد ملف باسم rin_lib.h في الأرشيفين):
//    • loader_ui/library_loader_ui.h  ← فكرة «نتيجة التحميل» (مدمج/قرص/كاش) → PackageRef::source + loadMillis
//    • indsin/rin_indsin_rinlib.h     ← أسلوب فحص الوسائط بعبارات خطأ واضحة (اسم الدالة + رقم الوسيط + النوع)
// ============================================================================
#include "rin_interpreter.h"
#include "rin_libdir.h"
#include "rin_version.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "clc/sha256.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace libint {

using Args = std::vector<Value>;
namespace ld = ::rin::libdir;

constexpr int kMaxDepDepth = 64;   // حدّ أمان لعمق سلسلة التبعيات

static Value jToValue(const ld::J& j) {
    switch (j.t) {
        case ld::J::Null: return Value::nil();
        case ld::J::Bool: return Value::boolean_(j.b);
        case ld::J::Num: return Value::num(j.n);
        case ld::J::Str: return Value::string(j.s);
        case ld::J::Arr: { std::vector<Value> v; for (auto& x : j.a) v.push_back(jToValue(x)); return extra4::A4(std::move(v)); }
        case ld::J::Obj: {
            std::vector<std::pair<std::string, Value>> kv;
            for (auto& p : j.o) kv.emplace_back(p.first, jToValue(p.second));
            return extra4::M4(std::move(kv));
        }
    }
    return Value::nil();
}

static Value strArr(const std::vector<std::string>& v) {
    std::vector<Value> out;
    for (auto& s : v) out.push_back(Value::string(s));
    return extra4::A4(std::move(out));
}

// عقدة XML -> {tag, attrs:{..}, text, children:[..]}
static Value xToValue(const ld::X& x) {
    std::vector<std::pair<std::string, Value>> at;
    for (auto& a : x.attrs) at.emplace_back(a.first, Value::string(a.second));
    std::vector<Value> kids;
    for (auto& k : x.kids) kids.push_back(xToValue(k));
    return extra4::M4({{"tag", Value::string(x.tag)}, {"attrs", extra4::M4(std::move(at))},
                       {"text", Value::string(x.text)}, {"children", extra4::A4(std::move(kids))}});
}

static std::string fnvHex(const std::string& s) {
    uint64_t h1 = 1469598103934665603ULL, h2 = 0x9E3779B97F4A7C15ULL;
    for (unsigned char c : s) { h1 = (h1 ^ c) * 1099511628211ULL; h2 = (h2 + c) * 0xff51afd7ed558ccdULL; h2 ^= h2 >> 29; }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", (unsigned long long)h1, (unsigned long long)h2);
    return buf;
}

static std::string sha256Of(const std::string& data) { return clc::sha256_hex(clc::sha256(data)); }

static bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static std::string stripLibSuffix(const std::string& s) {
    return endsWith(s, ld::kLibDirSuffix) ? s.substr(0, s.size() - std::char_traits<char>::length(ld::kLibDirSuffix)) : s;
}
static std::string trimSlash(std::string p) { while (p.size() > 1 && p.back() == '/') p.pop_back(); return p; }
static std::string baseOf(const std::string& p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? p : p.substr(k + 1); }
static std::string parentOf(const std::string& p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? std::string() : p.substr(0, k + 1); }
static std::string lowerStr(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

static bool validParamName(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}

// غلاف Rin لدالة أصلية معلَنة في Disconom (native.functions):
//   fun name(a, b) { return rinlib.native("lib", "name", [a, b]); }
static std::string wrapperSource(const std::string& lib, const ld::FnSpec& f) {
    std::string params, arr;
    for (size_t i = 0; i < f.params.size(); ++i) { params += (i ? ", " : "") + f.params[i]; arr += (i ? ", " : "") + f.params[i]; }
    return "fun " + f.name + "(" + (f.hasParams ? params : std::string("args")) + ") { return rinlib.native(\"" + lib + "\", \"" +
           f.name + "\", " + (f.hasParams ? "[" + arr + "]" : std::string("args")) + "); }\n";
}

// عبارات `@import "x"` في نص توكن أداة (مسح خفيف بلا محلّل) — لرسم التبعيات فقط، لا يُنفَّذ منه شيء.
static std::vector<std::string> scanImports(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        size_t a = i;
        while (a < e && (text[a] == ' ' || text[a] == '\t')) ++a;
        if (text.compare(a, 7, "@import") == 0) {
            size_t q = text.find('"', a + 7);
            if (q != std::string::npos && q < e) {
                size_t q2 = text.find('"', q + 1);
                if (q2 != std::string::npos && q2 < e) out.push_back(text.substr(q + 1, q2 - q - 1));
            }
        }
        i = e + 1;
    }
    return out;
}

// هل المسار الحقيقي p داخل المجلد الحقيقي root بعد حلّ الروابط الرمزية؟ (حماية من هروب symlink)
static bool pathWithin(const std::filesystem::path& root, const std::filesystem::path& p) {
    auto r = std::mismatch(root.begin(), root.end(), p.begin(), p.end());
    return r.first == root.end();
}

static Value graphToValue(const ld::DepGraph& g) {
    std::vector<Value> nodes, order;
    for (auto& n : g.nodes) {
        order.push_back(Value::string(n.name));
        nodes.push_back(extra4::M4({{"name", Value::string(n.name)}, {"version", Value::string(n.version)},
                                    {"kind", Value::string(ld::kindName(n.kind))}, {"root", Value::string(n.root)},
                                    {"source", Value::string(n.source)}, {"range", Value::string(n.range)},
                                    {"found", extra4::B4(n.found)}, {"satisfied", extra4::B4(n.satisfied)},
                                    {"deps", strArr(n.deps)}}));
    }
    return extra4::M4({{"ok", extra4::B4(g.ok())}, {"order", extra4::A4(std::move(order))}, {"nodes", extra4::A4(std::move(nodes))},
                       {"cycles", strArr(g.cycles)}, {"problems", strArr(g.problems)}, {"missing", strArr(g.missing)}});
}

static Value refToValue(const ld::PackageRef& r) {
    return extra4::M4({{"kind", Value::string(ld::kindName(r.kind))}, {"label", Value::string(ld::kindLabelAr(r.kind))},
                       {"name", Value::string(r.name)}, {"root", Value::string(r.root)}, {"source", Value::string(r.source)},
                       {"bytes", Value::num(static_cast<double>(r.bytes))}});
}

} // namespace libint

// ---------------------------------------------------------------------------
//  نظام الملفات المنطقي: ملفات افتراضية (صورة rin_vm) أولاً، ثم القرص عبر resolvePath (معزول بـ basePath)
// ---------------------------------------------------------------------------
libdir::FS Interpreter::makeLibFS() const {
    namespace fs = std::filesystem;
    libdir::FS f;
    f.isDir = [this](const std::string& p) {
        const std::string pre = p + "/";
        for (auto& kv : virtualFiles_) if (kv.first.rfind(pre, 0) == 0) return true;
        std::error_code ec;
        return fs::is_directory(resolvePath(p), ec);
    };
    f.read = [this](const std::string& p, std::string& out) {
        auto it = virtualFiles_.find(p);
        if (it != virtualFiles_.end()) { out = it->second; return true; }
        std::string real = resolvePath(p);
        std::error_code ec;
        if (fs::is_directory(real, ec)) return false;
        std::ifstream in(real, std::ios::binary);
        if (!in) return false;
        std::ostringstream b; b << in.rdbuf(); out = b.str();
        return true;
    };
    f.listFiles = [this](const std::string& dir, std::vector<std::string>& out) {
        std::set<std::string> seen;
        const std::string pre = dir + "/";
        for (auto& kv : virtualFiles_) if (kv.first.rfind(pre, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
        std::error_code ec;
        std::string real = resolvePath(dir);
        fs::recursive_directory_iterator it(real, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            std::string rel = fs::relative(it->path(), real, e2).generic_string();
            if (e2 || rel.empty()) continue;
            std::string logical = pre + rel;
            if (seen.insert(logical).second) out.push_back(logical);
        }
    };
    return f;
}

// يحوّل ما كُتب في @import إلى مجلد مكتبة إن كان هناك مجلد فعلاً.
//   @import "math"               -> lib/math.og.rin/   (أو lib/math/ إن كان فيه Disconom)
//   @import "lib/math.og.rin"    -> المجلد نفسه إن كان مجلداً (وإلا يبقى استيراد توكن أداة/ملف كالمعتاد)
//   @import "libs/shapes"        -> libs/shapes.og.rin/ أو libs/shapes/ (الأخير يتطلب Disconom)
//   @import "./vendor/x.og.rin"  -> نسبةً إلى مجلد الملف المستورِد (عند غياب basePath)
bool Interpreter::findLibraryDir(const std::string& rawPath, const std::string& libPath, std::string& rootOut) const {
    using namespace libint;
    libdir::FS fsys = makeLibFS();
    auto hasManifest = [&](const std::string& d) {
        std::string t;
        return fsys.read(d + "/disconom.json", t) || fsys.read(d + "/disconom.xml", t) ||
               fsys.read(d + "/Disconom.json", t) || fsys.read(d + "/Disconom.xml", t);
    };
    std::vector<std::pair<std::string, bool>> cand;   // (مسار، يتطلب Disconom)
    std::set<std::string> dedup;
    auto add = [&](const std::string& path) {
        std::string p = trimSlash(path);
        auto push = [&](const std::string& q, bool needManifest) { if (dedup.insert(q + (needManifest ? "#m" : "")).second) cand.push_back({q, needManifest}); };
        if (endsWith(p, libdir::kLibDirSuffix)) push(p, false);
        else { push(p + libdir::kLibDirSuffix, false); push(p, true); }
    };
    add(libPath);
    if (rawPath.find('/') == std::string::npos) add("lib/" + stripLibSuffix(rawPath));
    if (basePath.empty() && (rawPath.rfind("./", 0) == 0 || rawPath.rfind("../", 0) == 0)) {
        size_t slash = sourceFile.find_last_of('/');
        if (slash != std::string::npos) add(sourceFile.substr(0, slash + 1) + rawPath);
    }
    for (auto& c : cand) {
        if (!fsys.isDir(c.first)) continue;
        if (c.second && !hasManifest(c.first)) continue;
        rootOut = c.first;
        return true;
    }
    return false;
}

bool Interpreter::locateLibraryDir(const std::string& nameOrPath, std::string& rootOut) const {
    std::string raw = nameOrPath;
    std::string libPath = raw;
    if (libPath.find('/') == std::string::npos) libPath = "lib/" + libint::stripLibSuffix(libPath) + libdir::kLibDirSuffix;
    return findLibraryDir(raw, libPath, rootOut);
}

// تبعية مكتبة: بجوار المكتبة نفسها أولاً (نفس المجلد الأب) ثم في lib/ كالمعتاد.
bool Interpreter::findDependencyDir(const std::string& libRoot, const std::string& depName, std::string& depRootOut, bool* sibling) const {
    using namespace libint;
    if (sibling) *sibling = false;
    const std::string parent = parentOf(libRoot);
    if (!parent.empty() && findLibraryDir(parent + depName, parent + depName + libdir::kLibDirSuffix, depRootOut)) {
        if (sibling) *sibling = true;
        return true;
    }
    return findLibraryDir(depName, "lib/" + depName + libdir::kLibDirSuffix, depRootOut);
}

// ---------------------------------------------------------------------------
//  تصنيف الحزم: مكتبة (مجلد) أم توكن أداة OG (ملف واحد)
//  أولوية المصدر مطابقة تماماً لـ @import:  مجلد  >  افتراضي (rin_vm)  >  مدمج  >  قرص
// ---------------------------------------------------------------------------
bool Interpreter::resolvePackage(const std::string& nameOrPath, libdir::PackageRef& out) const {
    using namespace libint;
    namespace fs = std::filesystem;
    out = libdir::PackageRef();
    std::string root;
    if (locateLibraryDir(nameOrPath, root)) {
        out.kind = libdir::PackageKind::Library;
        out.root = root;
        out.name = stripLibSuffix(baseOf(root));
        const std::string pre = root + "/";
        out.source = "disk";
        for (auto& kv : virtualFiles_) if (kv.first.rfind(pre, 0) == 0) { out.source = "virtual"; break; }
        return true;
    }
    const std::string raw = trimSlash(nameOrPath);
    if (raw.empty()) return false;
    std::vector<std::string> cands;
    const bool hasSuffix = endsWith(raw, libdir::kToolTokenSuffix);
    if (raw.find('/') == std::string::npos) cands.push_back("lib/" + (hasSuffix ? raw : raw + libdir::kToolTokenSuffix));
    else { cands.push_back(raw); if (!hasSuffix) cands.push_back(raw + libdir::kToolTokenSuffix); }
    auto fill = [&](const std::string& path, const char* source, size_t bytes) {
        out.kind = libdir::PackageKind::ToolToken;
        out.root = path;
        out.name = stripLibSuffix(baseOf(path));
        out.source = source;
        out.bytes = bytes;
    };
    for (auto& c : cands) { auto it = virtualFiles_.find(c); if (it != virtualFiles_.end()) { fill(c, "virtual", it->second.size()); return true; } }
    { auto it = virtualFiles_.find(raw); if (it != virtualFiles_.end()) { fill(raw, "virtual", it->second.size()); return true; } }
    const auto& emb = embeddedRinLibraries();
    for (auto& c : cands) { auto it = emb.find(c); if (it != emb.end()) { fill(c, "embedded", it->second.size()); return true; } }
    for (auto& c : cands) {
        std::error_code ec;
        std::string real = resolvePath(c);
        if (fs::is_regular_file(real, ec)) {
            std::error_code e2;
            auto sz = fs::file_size(real, e2);
            fill(c, "disk", e2 ? 0 : static_cast<size_t>(sz));
            return true;
        }
    }
    return false;
}

bool Interpreter::readToolToken(const libdir::PackageRef& ref, std::string& out) const {
    if (ref.kind != libdir::PackageKind::ToolToken) return false;
    auto v = virtualFiles_.find(ref.root);
    if (v != virtualFiles_.end()) { out = v->second; return true; }
    const auto& emb = embeddedRinLibraries();
    auto e = emb.find(ref.root);
    if (e != emb.end()) { out = e->second; return true; }
    std::ifstream in(resolvePath(ref.root), std::ios::binary);
    if (!in) return false;
    std::ostringstream b; b << in.rdbuf(); out = b.str();
    return true;
}

std::vector<libdir::TokenInfo> Interpreter::listToolTokens(const std::string& dirIn) const {
    using namespace libint;
    namespace fs = std::filesystem;
    const std::string dir = trimSlash(dirIn.empty() ? std::string("lib") : dirIn);
    const std::string pre = dir + "/";
    std::map<std::string, std::pair<int, libdir::TokenInfo>> best;   // الاسم -> (أولوية، معلومات)
    auto consider = [&](const std::string& logical, const char* source, int prio, size_t bytes) {
        if (logical.rfind(pre, 0) != 0) return;
        std::string rel = logical.substr(pre.size());
        if (rel.find('/') != std::string::npos || !endsWith(rel, libdir::kToolTokenSuffix)) return;
        std::string name = stripLibSuffix(rel);
        if (name.empty()) return;
        auto it = best.find(name);
        if (it != best.end() && it->second.first >= prio) return;
        libdir::TokenInfo t; t.name = name; t.root = logical; t.source = source; t.bytes = bytes;
        best[name] = {prio, t};
    };
    for (auto& kv : virtualFiles_) consider(kv.first, "virtual", 3, kv.second.size());
    for (auto& kv : embeddedRinLibraries()) consider(kv.first, "embedded", 2, kv.second.size());
    std::error_code ec;
    fs::directory_iterator it(resolvePath(dir), fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::error_code e3;
        auto sz = it->file_size(e3);
        consider(pre + it->path().filename().generic_string(), "disk", 1, e3 ? 0 : static_cast<size_t>(sz));
    }
    std::vector<libdir::TokenInfo> out;
    for (auto& kv : best) {
        std::string shadow;   // مكتبة (مجلد) بالاسم نفسه تحجب التوكن عند @import
        if (locateLibraryDir(kv.second.second.root, shadow) && shadow == kv.second.second.root) continue;
        out.push_back(kv.second.second);
    }
    return out;   // std::map => مرتبة بالاسم
}

std::string Interpreter::LibraryUnit::describe() const {
    size_t parts = plan ? plan->parts.size() : 0, runs = plan ? plan->runFiles.size() : 0;
    std::string s = std::string(" (") + libdir::kindLabelAr(libdir::PackageKind::Library) + ": " + name + " " + version + " — " + std::to_string(parts) + " أجزاء";
    if (runs) s += " + " + std::to_string(runs) + " تشغيل";
    if (!nativeNote.empty()) s += "، C++: " + nativeNote;
    return s + ")";
}

// ---------------------------------------------------------------------------
//  رسم التبعيات (متعدٍّ): مكتبات بالـ Disconom، وتوكنات أداة بمسح @import الخفيف
// ---------------------------------------------------------------------------
libdir::DepGraph Interpreter::dependencyGraph(const std::string& nameOrRoot) const {
    using namespace libint;
    libdir::DepGraph g;
    libdir::PackageRef top;
    {
        libdir::FS f0 = makeLibFS();
        if (f0.isDir(nameOrRoot)) {   // مجلد صريح (حتى لو بلا لاحقة)
            top.kind = libdir::PackageKind::Library; top.root = trimSlash(nameOrRoot);
            top.name = stripLibSuffix(baseOf(top.root)); top.source = "disk";
        } else if (!resolvePackage(nameOrRoot, top)) {
            g.problems.push_back("package `" + nameOrRoot + "` was not found");
            return g;
        }
    }
    libdir::FS fsys = makeLibFS();
    std::set<std::string> done;
    std::vector<std::string> stackKeys, stackNames;

    std::function<void(const libdir::PackageRef&, const std::string&)> visit =
        [&](const libdir::PackageRef& pr, const std::string& range) {
        const std::string key = std::string(libdir::kindName(pr.kind)) + ":" + pr.root;
        auto onStack = std::find(stackKeys.begin(), stackKeys.end(), key);
        if (onStack != stackKeys.end()) {
            std::string cyc;
            for (size_t i = static_cast<size_t>(onStack - stackKeys.begin()); i < stackNames.size(); ++i) cyc += stackNames[i] + " -> ";
            g.cycles.push_back(cyc + pr.name);
            return;
        }
        if (done.count(key)) return;
        if (static_cast<int>(stackKeys.size()) >= kMaxDepDepth) {
            g.problems.push_back("dependency chain deeper than " + std::to_string(kMaxDepDepth) + " at `" + pr.name + "`");
            return;
        }
        stackKeys.push_back(key); stackNames.push_back(pr.name);
        libdir::GraphNode node;
        node.name = pr.name; node.root = pr.root; node.kind = pr.kind; node.source = pr.source; node.range = range;
        struct Child { libdir::PackageRef ref; std::string range; std::string name; bool found; };
        std::vector<Child> kids;
        if (pr.kind == libdir::PackageKind::Library) {
            libdir::Plan plan; std::vector<std::string> errs;
            libdir::planLibrary(fsys, pr.root, "", plan, errs);
            if (!plan.manifest.name.empty()) node.name = plan.manifest.name;
            node.version = plan.manifest.version;
            for (auto& dep : plan.manifest.dependencies) {
                Child c; c.name = dep.name; c.range = dep.range; c.found = false;
                std::string depRoot;
                if (findDependencyDir(pr.root, dep.name, depRoot)) {
                    c.found = true; c.ref.kind = libdir::PackageKind::Library; c.ref.root = depRoot;
                    c.ref.name = stripLibSuffix(baseOf(depRoot)); c.ref.source = "disk";
                } else if (resolvePackage(dep.name, c.ref)) c.found = true;
                kids.push_back(std::move(c));
            }
        } else {
            std::string text;
            if (readToolToken(pr, text) && text.size() <= (2u << 20)) {
                std::set<std::string> seenNames;
                for (auto& imp : scanImports(text)) {
                    if (!seenNames.insert(imp).second) continue;
                    Child c; c.name = stripLibSuffix(baseOf(imp)); c.found = resolvePackage(imp, c.ref);
                    kids.push_back(std::move(c));
                }
            }
        }
        for (auto& c : kids) {
            node.deps.push_back(c.name);
            if (!c.found) {
                libdir::GraphNode miss; miss.name = c.name; miss.range = c.range; miss.found = false;
                g.missing.push_back("`" + node.name + "` depends on `" + c.name + "` which was not found as a library or tool token (it may be a RinPM package)");
                g.nodes.push_back(miss);
                continue;
            }
            visit(c.ref, c.range);
            if (c.ref.kind == libdir::PackageKind::Library && !c.range.empty()) {
                for (auto& n : g.nodes) {
                    if (n.root == c.ref.root && n.found && !libdir::satisfies(n.version, c.range)) {
                        n.satisfied = false;
                        g.problems.push_back("`" + node.name + "` needs `" + c.name + "` " + c.range + " but found " + n.version);
                    }
                }
            }
        }
        stackKeys.pop_back(); stackNames.pop_back();
        done.insert(key);
        g.nodes.push_back(node);
    };
    visit(top, "");
    return g;
}

// ---------------------------------------------------------------------------
//  بصمة SHA-256: مسارات مرتبة + أحجام + بايتات كل الملفات (المكتبة) أو بايتات الملف (توكن الأداة)
// ---------------------------------------------------------------------------
std::string Interpreter::libraryFingerprint(const std::string& nameOrRoot) const {
    using namespace libint;
    libdir::FS fsys = makeLibFS();
    libdir::PackageRef pr;
    if (fsys.isDir(nameOrRoot)) { pr.kind = libdir::PackageKind::Library; pr.root = trimSlash(nameOrRoot); }
    else if (!resolvePackage(nameOrRoot, pr)) return std::string();
    if (pr.kind == libdir::PackageKind::ToolToken) {
        std::string t;
        return readToolToken(pr, t) ? "sha256:" + sha256Of(t) : std::string();
    }
    libdir::Plan plan; std::vector<std::string> errs;
    libdir::planLibrary(fsys, pr.root, "", plan, errs);
    std::string acc;
    for (auto& rel : plan.files) {   // plan.files مرتبة
        std::string t;
        if (!fsys.read(pr.root + "/" + rel, t)) continue;
        acc += rel; acc += '\n'; acc += std::to_string(t.size()); acc += '\n'; acc += t; acc += '\n';
    }
    return "sha256:" + sha256Of(acc);
}

// ---------------------------------------------------------------------------
//  تحميل الوحدة (مكتبة)
// ---------------------------------------------------------------------------
void Interpreter::loadLibraryUnit(const std::string& root, int line, LibraryUnit& unit) {
    using namespace libint;
    using namespace extra4;
    const auto t0 = std::chrono::steady_clock::now();
    libdir::FS fsys = makeLibFS();
    auto plan = std::make_shared<libdir::Plan>();
    std::vector<std::string> errs;
    if (!libdir::planLibrary(fsys, root, RIN_VERSION_STRING, *plan, errs)) {
        auto d = diagErr(diag::Code::E0028_ImportError, line, "@import: invalid library \"" + root + "\"");
        std::string why;
        for (auto& e : errs) why += (why.empty() ? "" : "; ") + e;
        d.diagnostic->withReason(why).withHint("run `rin lib check " + root + "` for the full report (Disconom, parts, syntax)");
        throw d;
    }
    const libdir::Manifest& m = plan->manifest;
    unit.root = root; unit.plan = plan; unit.name = m.name; unit.version = m.version;

    // ---- 1) التبعيات: عبارات @import مُصطنعة تُنفَّذ أولاً بنفس آلية الاستيراد (كشف دورات، منع تكرار...) ----
    for (auto& dep : m.dependencies) {
        std::string depRoot, depImport = dep.name;
        bool sibling = false;
        const bool foundLib = findDependencyDir(root, dep.name, depRoot, &sibling);
        if (foundLib) {
            if (sibling) depImport = depRoot;
            libdir::Plan dp; std::vector<std::string> de;
            if (libdir::planLibrary(fsys, depRoot, "", dp, de) && !libdir::satisfies(dp.manifest.version, dep.range)) {
                auto d = diagErr(diag::Code::E0028_ImportError, line,
                                 "@import: library `" + m.name + "` needs `" + dep.name + "` " + dep.range + " but found " + dp.manifest.version);
                d.diagnostic->withReason("dependency declared in " + plan->manifestFile + " of \"" + root + "\"; found at \"" + depRoot + "\"")
                 .withHint("update `" + dep.name + "` to a version matching " + dep.range + ", or relax the range in Disconom");
                throw d;
            }
        }
        auto imp = std::make_shared<ImportStmt>();
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::STRING; lit->str = depImport;
        imp->path = lit; imp->line = 0;
        unit.statements.push_back(imp);
    }
    if (!m.dependencies.empty()) unit.segments.push_back({unit.statements.size(), "dependencies of " + m.name});

    // ---- 2) كود C++ المساعد (اختياري) ----
    LoadedLibrary lib;
    lib.plan = plan;
    if (m.native.declared()) {
        if (!nativeAllowed()) {
            lib.nativeStatus = "disabled";
            unit.nativeNote = "معطَّل (استعمل --allow-native)";
        } else {
            std::string home = root + "/";
            std::string realRoot = std::filesystem::absolute(resolvePath(root)).generic_string();
            bool onDisk = std::filesystem::is_directory(realRoot);
            std::string hashInput, body;
            std::vector<std::string> incs;
            for (auto& rel : plan->files) {   // بصمة كل ملفات native/ (cpp/h/...) لإبطال الكاش عند أي تعديل
                if (rel.rfind("native/", 0) != 0) continue;
                std::string t; if (fsys.read(home + rel, t)) hashInput += rel + "\n" + t;
            }
            for (auto& src : m.native.sources) {
                std::string rel = src; while (rel.rfind("./", 0) == 0) rel.erase(0, 2);
                if (onDisk) body += "#include \"" + realRoot + "/" + rel + "\"\n";
                else { std::string t; fsys.read(home + rel, t); body += t + "\n"; hashInput += rel + t; }
            }
            incs.push_back(realRoot + "/native");
            for (auto& i : m.native.includes) incs.push_back(onDisk ? realRoot + "/" + i : i);
            std::string src = "// rinlib-native " + m.name + " " + m.version + " " + fnvHex(hashInput + m.native.std) + "\n#include \"rin_abi.h\"\n" + body;
            std::vector<std::pair<std::string, Value>> opts;
            opts.emplace_back("std", S4(m.native.std));
            opts.emplace_back("flags", strArr(m.native.flags));
            opts.emplace_back("libs", strArr(m.native.libs));
            opts.emplace_back("includes", strArr(incs));
            std::vector<Value> la{S4(src), M4(std::move(opts))};
            Value r = natives["cpp.lib"](la, line);
            if (o4bool(r, "ok", false)) {
                lib.nativeLoaded = true; lib.nativeStatus = "loaded";
                lib.nativeHandle = o4get(r, "handle") ? static_cast<int>(o4get(r, "handle")->number) : 0;
                unit.nativeNote = "محمَّل";
            } else {
                std::string why = o4str(r, "error", "failed");
                std::string se = o4str(r, "stderr", "");
                if (se.size() > 600) se = se.substr(0, 600) + "...";
                lib.nativeStatus = "failed: " + why + (se.empty() ? "" : "\n" + se);
                unit.nativeNote = "فشل البناء";
            }
        }
        if (!lib.nativeLoaded && m.native.required) {
            auto d = diagErr(diag::Code::E0028_ImportError, line, "@import: library `" + m.name + "` requires its native C++ helper but it is not available");
            d.diagnostic->withReason(lib.nativeStatus == "disabled" ? std::string(kNativeOffMsg) : lib.nativeStatus)
             .withHint("run with `rin --allow-native` (needs g++/clang++), or set \"required\": false and provide pure-Rin fallbacks");
            throw d;
        }
    }

    // ---- 3) تحليل الملفات ----
    auto parseFile = [&](const std::string& rel) {
        std::string logical = root + "/" + rel, text;
        if (!fsys.read(logical, text)) throw diagErr(diag::Code::E0028_ImportError, line, "@import: cannot read `" + rel + "` of library `" + m.name + "`");
        try {
            Lexer lx(text, logical);
            auto toks = lx.scanTokens();
            Parser ps(toks, logical);
            return ps.parse();
        } catch (RinError& e) {
            auto d = diagErr(diag::Code::E0028_ImportError, line, "@import: error parsing `" + rel + "` of library `" + m.name + "`");
            d.diagnostic->withReason("line " + std::to_string(e.line) + " of \"" + logical + "\": " + e.message);
            if (e.diagnostic) d.diagnostic->withCause(diag::renderShort(*e.diagnostic));
            throw d;
        }
    };
    auto parseSnippet = [&](const std::string& text, const std::string& label) {
        Lexer lx(text, label);
        auto toks = lx.scanTokens();
        Parser ps(toks, label);
        return ps.parse();
    };
    auto wrappers = [&]() {
        std::string src;
        for (auto& f : m.native.functions) {
            for (auto& p : f.params) if (!validParamName(p)) {
                throw diagErr(diag::Code::E0028_ImportError, line, "@import: native function `" + f.name + "` of library `" + m.name + "` has an invalid parameter name `" + p + "`");
            }
            src += wrapperSource(m.name, f);
        }
        return src.empty() ? std::vector<StmtPtr>() : parseSnippet(src, "<native:" + m.name + ">");
    };
    const bool hasNative = !m.native.functions.empty();
    // أغلفة الدوال الأصلية: إن لم يتوفر الكود الأصلي تُوضع *قبل* الأجزاء (فيستطيع جزء احتياطي بـ Rin نقي تعريف نفس الاسم
    // فيحلّ محلّها)، وإن توفّر تُوضع *بعد* الأجزاء (فتحلّ هي محلّ أي بديل Rin نقي).
    std::vector<StmtPtr> wrapperStmts;
    if (hasNative) wrapperStmts = wrappers();
    if (hasNative && !lib.nativeLoaded) {
        for (auto& s : wrapperStmts) unit.statements.push_back(s);
        unit.segments.push_back({unit.statements.size(), "<native stubs>"});
    }
    bool anyExported = false;
    for (auto& rel : plan->parts) {
        auto stmts = parseFile(rel);
        for (auto& s : stmts) { if (s->exported) anyExported = true; unit.statements.push_back(s); }
        unit.segments.push_back({unit.statements.size(), rel});
    }
    if (hasNative && lib.nativeLoaded) {
        for (auto& s : wrapperStmts) { s->exported = anyExported; unit.statements.push_back(s); }
        unit.segments.push_back({unit.statements.size(), "<native wrappers>"});
    } else if (hasNative) {
        for (auto& s : wrapperStmts) s->exported = anyExported;
    }

    // تُسجَّل قبل تنفيذ أي شيء: يستطيع كود المكتبة (و run.rinz) استدعاء rinlib.info/json/native(<name>, ...) أثناء التحميل.
    // عند فشل تحليل ملفات التشغيل يُستعاد الوضع السابق بالضبط (لا يبقى سجلّ مكتبة نصف محمَّلة).
    lib.loadMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const auto prevIt = loadedLibs_.find(m.name);
    const bool hadPrev = prevIt != loadedLibs_.end();
    LoadedLibrary prev;
    if (hadPrev) prev = prevIt->second;
    const bool hadOrder = std::find(loadedLibOrder_.begin(), loadedLibOrder_.end(), m.name) != loadedLibOrder_.end();
    loadedLibs_[m.name] = lib;
    if (!hadOrder) loadedLibOrder_.push_back(m.name);
    if (!skipLibraryRun_) {
        try {
            for (auto& rel : plan->runFiles) {
                auto stmts = parseFile(rel);
                for (auto& s : stmts) unit.statements.push_back(s);
                unit.segments.push_back({unit.statements.size(), rel});
            }
        } catch (...) {
            if (hadPrev) loadedLibs_[m.name] = prev; else loadedLibs_.erase(m.name);
            if (!hadOrder) loadedLibOrder_.erase(std::remove(loadedLibOrder_.begin(), loadedLibOrder_.end(), m.name), loadedLibOrder_.end());
            throw;
        }
    }
}

// ينفّذ عبارات مكتبة مقطعاً مقطعاً (ملف لكل مقطع) ليُنسَب أي خطأ تشغيل إلى ملفه؛ السلوك مطابق لتنفيذها دفعة واحدة.
void Interpreter::execLibraryBody(const std::vector<StmtPtr>& stmts, const std::vector<std::pair<size_t, std::string>>* segs, const EnvPtr& env) {
    if (!segs || segs->empty()) { executeBlock(stmts, env); return; }
    size_t begin = 0;
    for (auto& seg : *segs) {
        size_t end = std::min(seg.first, stmts.size());
        if (end > begin) {
            if (!execFrames_.empty()) execFrames_.back().part = seg.second;   // RINTWIST.file/lib.part
            std::vector<StmtPtr> slice(stmts.begin() + static_cast<long>(begin), stmts.begin() + static_cast<long>(end));
            try { executeBlock(slice, env); }
            catch (RinError& e) { e.message = "[" + seg.second + "] " + e.message; throw; }
        }
        begin = end;
    }
}

// ---------------------------------------------------------------------------
//  فحص ساكن كامل (rin lib check / rinlib.check) — للمكتبات ولتوكنات الأداة
// ---------------------------------------------------------------------------
Interpreter::LibraryCheckReport Interpreter::checkLibraryDir(const std::string& rootIn) {
    using namespace libint;
    LibraryCheckReport rep;
    libdir::FS fsys = makeLibFS();
    std::string root = rootIn;

    // ---- توكن أداة OG (ملف واحد): فحص صياغة فقط، بلا Disconom ----
    if (!fsys.isDir(root)) {
        libdir::PackageRef pr;
        if (resolvePackage(rootIn, pr) && pr.kind == libdir::PackageKind::ToolToken) {
            rep.kind = libdir::kindName(pr.kind); rep.root = pr.root; rep.name = pr.name; rep.version = "";
            std::string text;
            if (!readToolToken(pr, text)) rep.errors.push_back(pr.root + ": cannot read");
            else {
                try { Lexer lx(text, pr.root); auto toks = lx.scanTokens(); Parser ps(toks, pr.root); ps.parse(); }
                catch (RinError& e) { rep.errors.push_back(pr.root + ":" + std::to_string(e.line) + ": " + e.message); }
                rep.fingerprint = "sha256:" + sha256Of(text);
            }
            auto g = dependencyGraph(pr.root);
            for (auto& n : g.nodes) rep.loadOrder.push_back(n.name);
            for (auto& c : g.cycles) rep.errors.push_back("dependency cycle: " + c);
            for (auto& w : g.missing) rep.warnings.push_back(w);
            rep.ok = rep.errors.empty();
            return rep;
        }
        if (locateLibraryDir(rootIn, root)) { /* root محدَّث */ }
    }
    rep.root = root;
    rep.kind = libdir::kindName(libdir::PackageKind::Library);
    libdir::Plan plan;
    std::vector<std::string> errs;
    bool planned = libdir::planLibrary(fsys, root, RIN_VERSION_STRING, plan, errs);
    rep.errors = errs;
    rep.warnings = plan.warnings;
    rep.name = plan.manifest.name; rep.version = plan.manifest.version;
    rep.parts = plan.parts; rep.runFiles = plan.runFiles;
    if (!plan.hasManifest && planned) rep.errors.push_back("missing Disconom file (disconom.json or disconom.xml) — it declares the library name, version and required Rin version");
    if (planned) {
        auto syntax = [&](const std::string& rel) {
            std::string text, logical = root + "/" + rel;
            if (!fsys.read(logical, text)) { rep.errors.push_back(rel + ": cannot read"); return; }
            try { Lexer lx(text, logical); auto toks = lx.scanTokens(); Parser ps(toks, logical); ps.parse(); }
            catch (RinError& e) { rep.errors.push_back(rel + ":" + std::to_string(e.line) + ": " + e.message); }
        };
        for (auto& p : plan.parts) syntax(p);
        for (auto& p : plan.runFiles) syntax(p);
        // رسم التبعيات المتعدّية: دورات وعدم توافق إصدارات = أخطاء، تبعية غير موجودة = تحذير
        libdir::DepGraph g = dependencyGraph(root);
        for (auto& n : g.nodes) rep.loadOrder.push_back(n.name);
        for (auto& c : g.cycles) rep.errors.push_back("dependency cycle: " + c);
        for (auto& p : g.problems) rep.errors.push_back(p);
        for (auto& w : g.missing) rep.warnings.push_back(w);
        if (rep.errors.empty()) rep.fingerprint = libraryFingerprint(root);
    }
    rep.ok = rep.errors.empty();
    return rep;
}

// ---------------------------------------------------------------------------
//  rinlib.*
// ---------------------------------------------------------------------------
void Interpreter::registerNativesLibDir() {
    using namespace libint;
    using namespace extra4;

    // يحلّ اسم مكتبة (محمَّلة أو غير محمَّلة) إلى مجلدها المنطقي. (المكتبات فقط؛ التوكنات ملف لا مجلد)
    auto rootOf = [this](const std::string& nameOrPath, std::string& root) -> bool {
        auto it = loadedLibs_.find(nameOrPath);
        if (it != loadedLibs_.end() && it->second.plan) { root = it->second.plan->root; return true; }
        return locateLibraryDir(nameOrPath, root);
    };
    auto infoOf = [this, rootOf](const std::string& nameOrPath, bool& found) -> Value {
        found = false;
        std::string root;
        if (!rootOf(nameOrPath, root)) {
            libdir::PackageRef pr;
            if (resolvePackage(nameOrPath, pr) && pr.kind == libdir::PackageKind::ToolToken) {
                found = true;
                return refToValue(pr);   // {kind:"tool-token", label, name, root, source, bytes}
            }
            return Value::nil();
        }
        std::shared_ptr<libdir::Plan> plan;
        auto it = loadedLibs_.find(nameOrPath);
        bool loaded = (it != loadedLibs_.end() && it->second.plan);
        if (loaded) plan = it->second.plan;
        else {
            plan = std::make_shared<libdir::Plan>();
            std::vector<std::string> errs;
            libdir::planLibrary(makeLibFS(), root, "", *plan, errs);
            if (!errs.empty() && plan->manifest.name.empty()) plan->manifest.name = plan->dirBase;
        }
        found = true;
        const libdir::Manifest& m = plan->manifest;
        std::vector<Value> fns;
        for (auto& f : m.native.functions) fns.push_back(S4(f.name));
        std::string nstatus = "none"; bool nloaded = false;
        double loadMs = 0;
        if (loaded) { nstatus = it->second.nativeStatus; nloaded = it->second.nativeLoaded; loadMs = it->second.loadMillis; }
        else if (m.native.declared()) nstatus = "not-loaded";
        std::vector<std::pair<std::string, Value>> deps;
        for (auto& d : m.dependencies) deps.emplace_back(d.name, S4(d.range));
        return M4({{"name", S4(m.name)}, {"version", S4(m.version)}, {"description", S4(m.description)},
                   {"kind", S4(libdir::kindName(libdir::PackageKind::Library))}, {"label", S4(libdir::kindLabelAr(libdir::PackageKind::Library))},
                   {"rin", S4(m.rin)}, {"license", S4(m.license)}, {"authors", strArr(m.authors)}, {"keywords", strArr(m.keywords)},
                   {"root", S4(plan->root)}, {"loaded", B4(loaded)}, {"loadMs", N4(loadMs)}, {"format", S4(m.formatName)}, {"disconom", S4(plan->manifestFile)},
                   {"parts", strArr(plan->parts)}, {"run", strArr(plan->runFiles)}, {"files", strArr(plan->files)},
                   {"dependencies", M4(std::move(deps))},
                   {"native", M4({{"declared", B4(m.native.declared())}, {"loaded", B4(nloaded)}, {"status", S4(nstatus)},
                                  {"sources", strArr(m.native.sources)}, {"functions", A4(std::move(fns))}})}});
    };

    natives["rinlib.list"] = [this, infoOf](Args& a, int line) -> Value {
        need("rinlib.list", a, 0, 0, line);
        std::vector<Value> out;
        for (auto& n : loadedLibOrder_) { bool f; Value v = infoOf(n, f); if (f) out.push_back(v); }
        return A4(std::move(out));
    };
    natives["rinlib.info"] = [infoOf](Args& a, int line) -> Value {
        need("rinlib.info", a, 1, 1, line);
        bool f; return infoOf(str(a[0], "rinlib.info", line), f);
    };
    natives["rinlib.exists"] = [this, rootOf](Args& a, int line) -> Value {
        need("rinlib.exists", a, 1, 1, line);
        const std::string n = str(a[0], "rinlib.exists", line);
        std::string r;
        if (rootOf(n, r)) return B4(true);
        libdir::PackageRef pr; return B4(resolvePackage(n, pr));   // توكن أداة OG يُعدّ موجوداً أيضاً
    };

    // ---- التصنيف: مكتبة أم توكن أداة OG ----
    natives["rinlib.kind"] = [this](Args& a, int line) -> Value {
        need("rinlib.kind", a, 1, 1, line);
        libdir::PackageRef pr;
        if (!resolvePackage(str(a[0], "rinlib.kind", line), pr)) return Value::nil();
        return S4(libdir::kindName(pr.kind));
    };
    natives["rinlib.resolve"] = [this](Args& a, int line) -> Value {
        need("rinlib.resolve", a, 1, 1, line);
        libdir::PackageRef pr;
        if (!resolvePackage(str(a[0], "rinlib.resolve", line), pr)) return Value::nil();
        return refToValue(pr);
    };
    natives["rinlib.tokens"] = [this](Args& a, int line) -> Value {
        need("rinlib.tokens", a, 0, 1, line);
        std::vector<Value> out;
        for (auto& t : listToolTokens(a.empty() ? std::string("lib") : str(a[0], "rinlib.tokens", line))) {
            out.push_back(M4({{"name", S4(t.name)}, {"kind", S4(libdir::kindName(libdir::PackageKind::ToolToken))},
                              {"root", S4(t.root)}, {"source", S4(t.source)}, {"bytes", N4(static_cast<double>(t.bytes))}}));
        }
        return A4(std::move(out));
    };

    natives["rinlib.manifest"] = [this, rootOf](Args& a, int line) -> Value {
        need("rinlib.manifest", a, 1, 1, line);
        std::string root;
        if (!rootOf(str(a[0], "rinlib.manifest", line), root)) return Value::nil();
        libdir::Plan plan; std::vector<std::string> errs;
        libdir::planLibrary(makeLibFS(), root, "", plan, errs);
        if (plan.manifest.hasRaw) return jToValue(plan.manifest.raw);
        return jToValue(libdir::manifestToJson(plan.manifest));
    };
    natives["rinlib.discover"] = [this](Args& a, int line) -> Value {
        need("rinlib.discover", a, 0, 1, line);
        std::string dir = a.empty() ? "lib" : str(a[0], "rinlib.discover", line);
        std::set<std::string> names;
        libdir::FS fsys = makeLibFS();
        std::vector<std::string> files;
        fsys.listFiles(dir, files);
        for (auto& f : files) {
            std::string rel = f.substr(dir.size() + 1);
            size_t slash = rel.find('/');
            if (slash == std::string::npos) continue;
            std::string top = rel.substr(0, slash);
            if (endsWith(top, libdir::kLibDirSuffix)) names.insert(top);
            else { std::string leaf = rel.substr(slash + 1); if (leaf == "disconom.json" || leaf == "disconom.xml") names.insert(top); }
        }
        std::vector<Value> out;
        for (auto& n : names) {
            libdir::Plan plan; std::vector<std::string> errs;
            libdir::planLibrary(fsys, dir + "/" + n, "", plan, errs);
            const libdir::Manifest& m = plan.manifest;
            out.push_back(M4({{"name", S4(m.name.empty() ? plan.dirBase : m.name)}, {"version", S4(m.version)}, {"description", S4(m.description)},
                              {"kind", S4(libdir::kindName(libdir::PackageKind::Library))},
                              {"root", S4(plan.root)}, {"ok", B4(errs.empty())}, {"parts", N4(static_cast<double>(plan.parts.size()))}}));
        }
        return A4(std::move(out));
    };
    // rinlib.libraries([dir]) — اسم واضح بالتسمية الجديدة؛ مرادف rinlib.discover
    natives["rinlib.libraries"] = [this](Args& a, int line) -> Value {
        need("rinlib.libraries", a, 0, 1, line);
        return natives["rinlib.discover"](a, line);
    };
    // rinlib.packages([dir]) — كل الحزم (مكتبات + توكنات أداة) مرتبة بالاسم؛ المكتبة تحجب التوكن بنفس الاسم
    natives["rinlib.packages"] = [this](Args& a, int line) -> Value {
        need("rinlib.packages", a, 0, 1, line);
        const std::string dir = a.empty() ? std::string("lib") : str(a[0], "rinlib.packages", line);
        Args da; if (!a.empty()) da.push_back(a[0]);
        Value libs = natives["rinlib.discover"](da, line);
        std::map<std::string, Value> byName;
        if (libs.type == Value::Type::ARRAY && libs.array)
            for (auto& v : *libs.array) {
                std::string n = o4str(v, "name", "");
                if (!n.empty()) byName[n] = v;
            }
        for (auto& t : listToolTokens(dir)) {
            if (byName.count(t.name)) continue;
            byName[t.name] = M4({{"name", S4(t.name)}, {"kind", S4(libdir::kindName(libdir::PackageKind::ToolToken))},
                                 {"version", S4("")}, {"description", S4("")}, {"root", S4(t.root)}, {"source", S4(t.source)},
                                 {"bytes", N4(static_cast<double>(t.bytes))}});
        }
        std::vector<Value> out;
        for (auto& kv : byName) out.push_back(kv.second);
        return A4(std::move(out));
    };
    natives["rinlib.files"] = [rootOf, this](Args& a, int line) -> Value {
        need("rinlib.files", a, 1, 1, line);
        std::string root;
        if (!rootOf(str(a[0], "rinlib.files", line), root)) return Value::nil();
        libdir::Plan plan; std::vector<std::string> errs;
        libdir::planLibrary(makeLibFS(), root, "", plan, errs);
        return strArr(plan.files);
    };

    // ---- التبعيات والسلامة ----
    natives["rinlib.graph"] = [this](Args& a, int line) -> Value {
        need("rinlib.graph", a, 1, 1, line);
        return graphToValue(dependencyGraph(str(a[0], "rinlib.graph", line)));
    };
    natives["rinlib.fingerprint"] = [this](Args& a, int line) -> Value {
        need("rinlib.fingerprint", a, 1, 1, line);
        std::string fp = libraryFingerprint(str(a[0], "rinlib.fingerprint", line));
        return fp.empty() ? Value::nil() : S4(fp);
    };
    natives["rinlib.verify"] = [this](Args& a, int line) -> Value {
        need("rinlib.verify", a, 2, 2, line);
        std::string want = lowerStr(str(a[1], "rinlib.verify", line));
        if (want.rfind("sha256:", 0) != 0) want = "sha256:" + want;
        return B4(lowerStr(libraryFingerprint(str(a[0], "rinlib.verify", line))) == want);
    };

    // ---- ملفات المكتبة نفسها: قراءة / json / xml (مسارات نسبية لمجلد المكتبة فقط، بلا .. ولا مسارات مطلقة ولا هروب symlink) ----
    auto relFile = [this, rootOf](const char* fn, Args& a, int line, std::string& logical, std::string& rel) {
        std::string name = str(a[0], fn, line); rel = str(a[1], fn, line);
        std::string root;
        if (!rootOf(name, root)) throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": library `" + name + "` was not found (import it first, or check the name). Tool tokens are single files and have no inner files");
        if (!libdir::detail::safeRelPath(rel)) throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": path `" + rel + "` must be relative to the library folder (no '..', no absolute paths)");
        logical = root + "/" + rel;
        if (virtualFiles_.find(logical) == virtualFiles_.end()) {
            namespace fs = std::filesystem;
            std::error_code e1, e2;
            fs::path realRoot = fs::weakly_canonical(resolvePath(root), e1);
            fs::path realFile = fs::weakly_canonical(resolvePath(logical), e2);
            if (!e1 && !e2 && !pathWithin(realRoot, realFile))
                throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": `" + rel + "` resolves outside the library folder (symbolic link?)");
        }
    };
    natives["rinlib.has"] = [this, relFile](Args& a, int line) -> Value {
        need("rinlib.has", a, 2, 2, line);
        std::string logical, rel; relFile("rinlib.has", a, line, logical, rel);
        std::string t; return B4(makeLibFS().read(logical, t));
    };
    natives["rinlib.read"] = [this, relFile](Args& a, int line) -> Value {
        need("rinlib.read", a, 2, 2, line);
        std::string logical, rel; relFile("rinlib.read", a, line, logical, rel);
        std::string t;
        if (!makeLibFS().read(logical, t)) throw diagErr(diag::Code::E0036_IOFailure, line, "rinlib.read: cannot read `" + rel + "` in library `" + str(a[0], "rinlib.read", line) + "`");
        return S4(t);
    };
    natives["rinlib.path"] = [this, relFile](Args& a, int line) -> Value {
        need("rinlib.path", a, 2, 2, line);
        std::string logical, rel; relFile("rinlib.path", a, line, logical, rel);
        return S4(std::filesystem::absolute(resolvePath(logical)).generic_string());
    };
    natives["rinlib.json"] = [this, relFile](Args& a, int line) -> Value {
        need("rinlib.json", a, 2, 2, line);
        std::string logical, rel; relFile("rinlib.json", a, line, logical, rel);
        std::string t;
        if (!makeLibFS().read(logical, t)) throw diagErr(diag::Code::E0036_IOFailure, line, "rinlib.json: cannot read `" + rel + "`");
        libdir::J j; std::string err;
        if (!libdir::parseJson(t, j, err)) throw diagErr(diag::Code::E0035_RuntimeError, line, "rinlib.json: `" + rel + "`: " + err);
        return jToValue(j);
    };
    natives["rinlib.xml"] = [this, relFile](Args& a, int line) -> Value {
        need("rinlib.xml", a, 2, 2, line);
        std::string logical, rel; relFile("rinlib.xml", a, line, logical, rel);
        std::string t;
        if (!makeLibFS().read(logical, t)) throw diagErr(diag::Code::E0036_IOFailure, line, "rinlib.xml: cannot read `" + rel + "`");
        libdir::X x; std::string err;
        if (!libdir::parseXml(t, x, err)) throw diagErr(diag::Code::E0035_RuntimeError, line, "rinlib.xml: `" + rel + "`: " + err);
        return xToValue(x);
    };

    // ---- كود C++ المساعد ----
    natives["rinlib.hasNative"] = [this](Args& a, int line) -> Value {
        need("rinlib.hasNative", a, 1, 1, line);
        auto it = loadedLibs_.find(str(a[0], "rinlib.hasNative", line));
        return B4(it != loadedLibs_.end() && it->second.nativeLoaded);
    };
    natives["rinlib.nativeInfo"] = [this](Args& a, int line) -> Value {
        need("rinlib.nativeInfo", a, 1, 1, line);
        auto it = loadedLibs_.find(str(a[0], "rinlib.nativeInfo", line));
        if (it == loadedLibs_.end()) return Value::nil();
        const auto& L = it->second;
        return M4({{"declared", B4(L.plan && L.plan->manifest.native.declared())}, {"loaded", B4(L.nativeLoaded)},
                   {"status", S4(L.nativeStatus)}, {"allowed", B4(nativeAllowed())}});
    };
    natives["rinlib.native"] = [this](Args& a, int line) -> Value {
        need("rinlib.native", a, 2, 3, line);
        std::string name = str(a[0], "rinlib.native", line), fn = str(a[1], "rinlib.native", line);
        auto it = loadedLibs_.find(name);
        if (it == loadedLibs_.end())
            throw diagErr(diag::Code::E0035_RuntimeError, line, "rinlib.native: library `" + name + "` is not loaded (use @import \"" + name + "\" first)");
        if (!it->second.nativeLoaded) {
            std::string st = it->second.nativeStatus;
            throw diagErr(diag::Code::E0035_RuntimeError, line,
                          "native function `" + fn + "` of library `" + name + "` is unavailable (" +
                          (st == "disabled" ? std::string("native code is disabled — run with `rin --allow-native`") : st == "none" ? std::string("the library declares no native code") : st) + ")");
        }
        std::vector<Value> ca{N4(it->second.nativeHandle), S4(fn), a.size() > 2 ? a[2] : A4({})};
        Value r = natives["cpp.call"](ca, line);
        if (!o4bool(r, "ok", false))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "rinlib.native: `" + name + "." + fn + "`: " + o4str(r, "error", "call failed"));
        const Value* v = o4get(r, "value");
        return v ? *v : Value::nil();
    };

    // ---- فحص + مولِّد ----
    natives["rinlib.check"] = [this](Args& a, int line) -> Value {
        need("rinlib.check", a, 1, 1, line);
        auto rep = checkLibraryDir(str(a[0], "rinlib.check", line));
        return M4({{"ok", B4(rep.ok)}, {"kind", S4(rep.kind)}, {"root", S4(rep.root)}, {"name", S4(rep.name)}, {"version", S4(rep.version)},
                   {"fingerprint", S4(rep.fingerprint)},
                   {"errors", strArr(rep.errors)}, {"warnings", strArr(rep.warnings)},
                   {"parts", strArr(rep.parts)}, {"run", strArr(rep.runFiles)}, {"loadOrder", strArr(rep.loadOrder)}});
    };
    natives["rinlib.scaffold"] = [this](Args& a, int line) -> Value {
        need("rinlib.scaffold", a, 2, 3, line);
        Value opts = a.size() > 2 ? a[2] : Value::nil();
        libdir::ScaffoldOpts so;
        so.name = str(a[1], "rinlib.scaffold", line);
        so.version = o4str(opts, "version", so.version);
        so.description = o4str(opts, "description", "");
        so.author = o4str(opts, "author", "");
        so.license = o4str(opts, "license", so.license);
        so.xml = o4bool(opts, "xml", false);
        so.native = o4bool(opts, "native", false);
        std::string parent = str(a[0], "rinlib.scaffold", line), root, err;
        std::vector<std::string> created;
        ensureParentDir(resolvePath(parent + "/x"));
        if (!libdir::scaffoldLibrary(resolvePath(parent), so, RIN_VERSION_STRING, root, created, err))
            return M4({{"ok", B4(false)}, {"error", S4(err)}});
        return M4({{"ok", B4(true)}, {"kind", S4(libdir::kindName(libdir::PackageKind::Library))},
                   {"root", S4(parent + "/" + so.name + libdir::kLibDirSuffix)}, {"files", strArr(created)}});
    };
}

} // namespace rin
