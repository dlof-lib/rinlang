// ============================================================================
//  rin_libdir.cpp — تكامل "مكتبات Rin المقسَّمة" مع المفسّر (انظر rin_libdir.h و docs/split-libraries.md)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp بنفس أسلوب rin_extra_natives*.cpp، فيدخل كل أهداف
//  البناء (APK / CLI / WASM / CI) تلقائياً بلا أي تعديل في ملفات البناء. يحتوي:
//    1) اكتشاف مجلد المكتبة من عبارة @import (قرص حقيقي أو ملفات افتراضية في صورة rin_vm)
//    2) تحميل الوحدة: قراءة Disconom، فحص إصدار اللغة والتبعيات، بناء/تحميل كود C++ المساعد،
//       تحليل كل .zpp/.rinz بالمحلّل الحقيقي ودمجها بالترتيب في قائمة عبارات واحدة
//    3) rinlib.* — دوال وقت التشغيل: info/list/manifest/read/json/xml/path/native/check/scaffold...
// ============================================================================
#include "rin_interpreter.h"
#include "rin_libdir.h"
#include "rin_version.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace libint {

using Args = std::vector<Value>;
namespace ld = ::rin::libdir;

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

static bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static std::string stripLibSuffix(const std::string& s) {
    return endsWith(s, ld::kLibDirSuffix) ? s.substr(0, s.size() - std::char_traits<char>::length(ld::kLibDirSuffix)) : s;
}
static std::string trimSlash(std::string p) { while (p.size() > 1 && p.back() == '/') p.pop_back(); return p; }

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

// يحوّل ما كُتب في @import إلى مجلد مكتبة مقسَّمة إن كان هناك مجلد فعلاً.
//   @import "math"               -> lib/math.og.rin/   (أو lib/math/ إن كان فيه Disconom)
//   @import "lib/math.og.rin"    -> المجلد نفسه إن كان مجلداً (وإلا يبقى استيراد ملف كالمعتاد)
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
    auto add = [&](const std::string& path) {
        std::string p = trimSlash(path);
        if (endsWith(p, libdir::kLibDirSuffix)) cand.push_back({p, false});
        else { cand.push_back({p + libdir::kLibDirSuffix, false}); cand.push_back({p, true}); }
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

std::string Interpreter::LibraryUnit::describe() const {
    size_t parts = plan ? plan->parts.size() : 0, runs = plan ? plan->runFiles.size() : 0;
    std::string s = " (مكتبة مقسَّمة: " + name + " " + version + " — " + std::to_string(parts) + " أجزاء";
    if (runs) s += " + " + std::to_string(runs) + " تشغيل";
    if (!nativeNote.empty()) s += "، C++: " + nativeNote;
    return s + ")";
}

// ---------------------------------------------------------------------------
//  تحميل الوحدة
// ---------------------------------------------------------------------------
void Interpreter::loadLibraryUnit(const std::string& root, int line, LibraryUnit& unit) {
    using namespace libint;
    using namespace extra4;
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
        // التبعية تُبحث أولاً بجوار المكتبة نفسها (نفس المجلد الأب)، ثم في lib/ كالمعتاد
        std::string parent = root.find('/') == std::string::npos ? std::string() : root.substr(0, root.find_last_of('/') + 1);
        bool sibling = !parent.empty() && findLibraryDir(parent + dep.name, parent + dep.name + libdir::kLibDirSuffix, depRoot);
        if (sibling) depImport = depRoot;
        if (sibling || findLibraryDir(dep.name, "lib/" + dep.name + libdir::kLibDirSuffix, depRoot)) {
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
    // تُسجَّل قبل تنفيذ أي شيء: يستطيع كود المكتبة (و run.rinz) استدعاء rinlib.info/json/native(<name>, ...) أثناء التحميل
    loadedLibs_[m.name] = lib;
    if (std::find(loadedLibOrder_.begin(), loadedLibOrder_.end(), m.name) == loadedLibOrder_.end()) loadedLibOrder_.push_back(m.name);
    if (!skipLibraryRun_) {
        for (auto& rel : plan->runFiles) {
            auto stmts = parseFile(rel);
            for (auto& s : stmts) unit.statements.push_back(s);
            unit.segments.push_back({unit.statements.size(), rel});
        }
    }
}

// ينفّذ عبارات مكتبة مقسَّمة مقطعاً مقطعاً (ملف لكل مقطع) ليُنسَب أي خطأ تشغيل إلى ملفه؛ السلوك مطابق لتنفيذها دفعة واحدة.
void Interpreter::execLibraryBody(const std::vector<StmtPtr>& stmts, const std::vector<std::pair<size_t, std::string>>* segs, const EnvPtr& env) {
    if (!segs || segs->empty()) { executeBlock(stmts, env); return; }
    size_t begin = 0;
    for (auto& seg : *segs) {
        size_t end = std::min(seg.first, stmts.size());
        if (end > begin) {
            std::vector<StmtPtr> slice(stmts.begin() + static_cast<long>(begin), stmts.begin() + static_cast<long>(end));
            try { executeBlock(slice, env); }
            catch (RinError& e) { e.message = "[" + seg.second + "] " + e.message; throw; }
        }
        begin = end;
    }
}

// ---------------------------------------------------------------------------
//  فحص ساكن كامل (rin lib check / rinlib.check)
// ---------------------------------------------------------------------------
Interpreter::LibraryCheckReport Interpreter::checkLibraryDir(const std::string& rootIn) {
    LibraryCheckReport rep;
    std::string root = rootIn;
    if (!makeLibFS().isDir(root)) { std::string found; if (locateLibraryDir(rootIn, found)) root = found; }
    rep.root = root;
    libdir::FS fsys = makeLibFS();
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
        for (auto& dep : plan.manifest.dependencies) {
            std::string depRoot;
            std::string parent = root.find('/') == std::string::npos ? std::string() : root.substr(0, root.find_last_of('/') + 1);
            bool sib = !parent.empty() && findLibraryDir(parent + dep.name, parent + dep.name + libdir::kLibDirSuffix, depRoot);
            if (sib || findLibraryDir(dep.name, "lib/" + dep.name + libdir::kLibDirSuffix, depRoot)) {
                libdir::Plan dp; std::vector<std::string> de;
                if (libdir::planLibrary(fsys, depRoot, "", dp, de) && !libdir::satisfies(dp.manifest.version, dep.range))
                    rep.errors.push_back("dependency `" + dep.name + "` " + dp.manifest.version + " does not satisfy " + dep.range);
            } else if (embeddedRinLibraries().count("lib/" + dep.name + ".og.rin") == 0) {
                rep.warnings.push_back("dependency `" + dep.name + "` was not found as a split library or embedded library (it may be a single-file or RinPM library)");
            }
        }
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

    // يحلّ اسم مكتبة (محمَّلة أو غير محمَّلة) إلى مجلدها المنطقي.
    auto rootOf = [this](const std::string& nameOrPath, std::string& root) -> bool {
        auto it = loadedLibs_.find(nameOrPath);
        if (it != loadedLibs_.end() && it->second.plan) { root = it->second.plan->root; return true; }
        return locateLibraryDir(nameOrPath, root);
    };
    auto infoOf = [this, rootOf](const std::string& nameOrPath, bool& found) -> Value {
        found = false;
        std::string root;
        if (!rootOf(nameOrPath, root)) return Value::nil();
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
        if (loaded) { nstatus = it->second.nativeStatus; nloaded = it->second.nativeLoaded; }
        else if (m.native.declared()) nstatus = "not-loaded";
        std::vector<std::pair<std::string, Value>> deps;
        for (auto& d : m.dependencies) deps.emplace_back(d.name, S4(d.range));
        return M4({{"name", S4(m.name)}, {"version", S4(m.version)}, {"description", S4(m.description)},
                   {"rin", S4(m.rin)}, {"license", S4(m.license)}, {"authors", strArr(m.authors)}, {"keywords", strArr(m.keywords)},
                   {"root", S4(plan->root)}, {"loaded", B4(loaded)}, {"format", S4(m.formatName)}, {"disconom", S4(plan->manifestFile)},
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
    natives["rinlib.exists"] = [rootOf](Args& a, int line) -> Value {
        need("rinlib.exists", a, 1, 1, line);
        std::string r; return B4(rootOf(str(a[0], "rinlib.exists", line), r));
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
                              {"root", S4(plan.root)}, {"ok", B4(errs.empty())}, {"parts", N4(static_cast<double>(plan.parts.size()))}}));
        }
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

    // ---- ملفات المكتبة نفسها: قراءة / json / xml (مسارات نسبية لمجلد المكتبة فقط، بلا .. ولا مسارات مطلقة) ----
    auto relFile = [this, rootOf](const char* fn, Args& a, int line, std::string& logical, std::string& rel) {
        std::string name = str(a[0], fn, line); rel = str(a[1], fn, line);
        std::string root;
        if (!rootOf(name, root)) throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": library `" + name + "` was not found (import it first, or check the name)");
        if (!libdir::detail::safeRelPath(rel)) throw diagErr(diag::Code::E0035_RuntimeError, line, std::string(fn) + ": path `" + rel + "` must be relative to the library folder (no '..', no absolute paths)");
        logical = root + "/" + rel;
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
        return M4({{"ok", B4(rep.ok)}, {"root", S4(rep.root)}, {"name", S4(rep.name)}, {"version", S4(rep.version)},
                   {"errors", strArr(rep.errors)}, {"warnings", strArr(rep.warnings)},
                   {"parts", strArr(rep.parts)}, {"run", strArr(rep.runFiles)}});
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
        return M4({{"ok", B4(true)}, {"root", S4(parent + "/" + so.name + libdir::kLibDirSuffix)}, {"files", strArr(created)}});
    };
}

} // namespace rin
