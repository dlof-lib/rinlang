// ============================================================================
//  rin_autoload.cpp — Autoload: تحميل كسول للحاويات والمكتبات عند أول إشارة لاسم غير معرَّف
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp بعد rin_container_services.cpp ويُسجَّل عبر
//  Interpreter::registerNativesAutoload(). التوثيق: docs/autoload.md
//
//  بدل @import مسبق لكل شيء، تسجّل «محمِّلات» تُستدعى فقط حين يُذكر اسم لا وجود له (متغيّر/حاوية/فضاء أسماء):
//
//    Autoload.register(fn, prepend?)   fn(name) يعرّف الاسم (حاوية/متغيّراً) بأي طريقة؛ prepend=true يضعه أولاً
//    Autoload.unregister(fn)           يزيل محمِّلاً بعينه
//    Autoload.dir(path, opts?)         محمِّل مجلد: الاسم a.b → path/a/b.rin ثم path/a.b.rin ثم .rcl
//                                      opts: ext:[".rin",".rcl"] · prefix:"App" (يُجرَّد من بداية الاسم ويُشترط وجوده)
//    Autoload.include(path)            ينفّذ ملف .rin/.rcl مرة واحدة في النطاق العام (يُستعمل داخل المحمِّلات)
//    Autoload.load(name)               يشغّل المحمِّلات الآن؛ true إن صار الاسم معرَّفاً
//    Autoload.loaded()                 الأسماء التي حُمِّلت تلقائياً بالترتيب
//    Autoload.isLoaded(name)           · Autoload.loaders() عدد المحمِّلات · Autoload.clear()
//
//  القواعد: المحمِّلات بترتيب التسجيل حتى يصير الاسم معرَّفاً · أخطاء المحمِّل تصعد كما هي (لا تُبتلع) ·
//  الاسم الجاري تحميله لا يُعاد تحميله من داخل نفسه · ملف نُفِّذ مرة لا يُنفَّذ ثانية · أسماء مجلد مُنقّاة (لا ".."
//  ولا "/") فلا خروج من المجلد · بلا محمِّلات لا كلفة تُذكر على أي برنامج (علم autoloadActive_).
//  نقاط الاعتراض: قراءة متغيّر غائب، ونقطة فضاء الأسماء (قراءة/كتابة/نداء) في المفسّر الشجري وRinVM معاً.
// ============================================================================
#include "rin_interpreter.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace autoload {

using Args = std::vector<Value>;

struct Loader {
    Value fn;                       // دالة (إن لم تكن محمِّل مجلد)
    bool isDir = false;
    std::string dir, prefix;
    std::vector<std::string> exts;
};

struct State {
    std::vector<Loader> loaders;
    std::vector<std::string> loaded;
    std::set<std::string> loading;  // أسماء قيد التحميل (حارس إعادة الدخول)
    std::set<std::string> files;    // ملفات نُفِّذت (تنفَّذ مرة واحدة)
    int depth = 0;
    std::string justFailed;         // اسم فشل للتوّ عند فضاء الأسماء: يمنع تكرار المحمِّلات في قراءة المتغيّر التالية لنفس التعبير
};

static bool safeName(const std::string& n) {
    if (n.empty() || n.size() > 200 || n.front() == '.' || n.back() == '.' || n.find("..") != std::string::npos) return false;
    for (unsigned char c : n) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.' || c >= 0x80)) return false;
    }
    return true;
}

} // namespace autoload

bool Interpreter::autoloadInclude(const std::string& cand, int line, const std::string& label) {
    using namespace autoload;
    auto st = std::static_pointer_cast<State>(autoloadState_);
    std::string real = resolvePath(cand, line);
    { std::ifstream t(real, std::ios::binary); if (!t.good()) return false; }
    if (st && !st->files.insert(real).second) return false;   // نُفِّذ من قبل: مرة واحدة فقط
    bool rcl = cand.size() > 4 && cand.compare(cand.size() - 4, 4, ".rcl") == 0;
    if (rcl) {
        std::vector<Value> la{Value::string(cand)};
        doLibraryImport(la, line, globals);
        return true;
    }
    std::ifstream in(real, std::ios::binary);
    std::ostringstream buf; buf << in.rdbuf();
    try {
        Lexer lex(buf.str());
        auto toks = lex.scanTokens();
        Parser p(toks);
        auto stmts = p.parse();
        executeBlock(stmts, globals);
    } catch (RinError& e) {
        throw diagErr(diag::Code::E0028_ImportError, line, "Autoload: خطأ أثناء تحميل '" + label + "' من '" + cand + "' (سطر " + std::to_string(e.line) + "): " + e.message);
    }
    return true;
}

bool Interpreter::autoloadMiss(const std::string& name, const EnvPtr& env, int line, bool fromNs) {
    using namespace autoload;
    auto st = std::static_pointer_cast<State>(autoloadState_);
    if (!st || st->loaders.empty()) return false;
    if (!fromNs && st->justFailed == name) { st->justFailed.clear(); return false; }
    st->justFailed.clear();
    if (st->loading.count(name) || st->depth >= 16) return false;

    auto has = [&]() {
        return (env && env->lookup(name)) || (globals && globals->lookup(name)) || nsResolveHead(name, env ? env : globals) != nullptr;
    };

    st->loading.insert(name);
    st->depth++;
    struct Guard { State* s; const std::string& n; ~Guard() { s->loading.erase(n); s->depth--; } } guard{st.get(), name};

    std::vector<Loader> loaders = st->loaders;   // نسخة: قد يسجّل المحمِّل محمِّلات أخرى
    bool ok = false;
    for (const auto& L : loaders) {
        if (!L.isDir) {
            Args a{Value::string(name)};
            callValue(L.fn, a, line);
        } else if (safeName(name)) {
            std::string rest = name;
            if (!L.prefix.empty()) {
                if (rest.compare(0, L.prefix.size(), L.prefix) != 0 || rest.size() <= L.prefix.size()) continue;
                rest = rest.substr(L.prefix.size());
                if (!rest.empty() && rest.front() == '.') rest.erase(0, 1);
                if (!safeName(rest)) continue;
            }
            std::string nested = rest;
            std::replace(nested.begin(), nested.end(), '.', '/');
            std::string base = L.dir;
            if (!base.empty() && base.back() != '/') base += '/';
            for (const auto& rel : {nested, rest}) {
                for (const auto& ext : L.exts) {
                    autoloadInclude(base + rel + ext, line, name);
                    if (has()) break;
                }
                if (has()) break;
            }
        }
        if (has()) { ok = true; break; }
    }
    if (ok && std::find(st->loaded.begin(), st->loaded.end(), name) == st->loaded.end()) st->loaded.push_back(name);
    if (!ok && fromNs) st->justFailed = name;
    return ok;
}

void Interpreter::registerNativesAutoload() {
    using namespace autoload;
    auto st = std::make_shared<State>();
    autoloadState_ = st;
    State* S = st.get();

    auto need = [](const char* fn, const Args& a, size_t lo, size_t hi, int line) {
        if (a.size() < lo || a.size() > hi)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "' expects " +
                          (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) + " argument(s) but got " + std::to_string(a.size()));
    };
    auto strArg = [](const Value& v, const char* fn, const char* what, int line) -> std::string {
        if (v.type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': " + what + " يجب أن يكون نصاً لكن وُجد نوع " + v.typeName());
        return v.str;
    };

    natives["Autoload.register"] = [this, S, need](Args& a, int line) -> Value {
        need("Autoload.register", a, 1, 2, line);
        if (a[0].type != Value::Type::FUNCTION || !a[0].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'Autoload.register': المحمِّل يجب أن يكون دالة لكن وُجد نوع ") + a[0].typeName());
        Loader L; L.fn = a[0];
        bool prepend = a.size() > 1 && a[1].isTruthy();
        if (prepend) S->loaders.insert(S->loaders.begin(), L); else S->loaders.push_back(L);
        autoloadActive_ = true;
        return Value::num(static_cast<double>(S->loaders.size()));
    };
    natives["Autoload.unregister"] = [this, S, need](Args& a, int line) -> Value {
        need("Autoload.unregister", a, 1, 1, line);
        for (size_t i = 0; i < S->loaders.size(); i++) {
            const Loader& L = S->loaders[i];
            if (!L.isDir && a[0].type == Value::Type::FUNCTION && L.fn.function == a[0].function) {
                S->loaders.erase(S->loaders.begin() + static_cast<long>(i));
                autoloadActive_ = !S->loaders.empty();
                return Value::boolean_(true);
            }
        }
        return Value::boolean_(false);
    };
    natives["Autoload.dir"] = [this, S, need, strArg](Args& a, int line) -> Value {
        need("Autoload.dir", a, 1, 2, line);
        Loader L; L.isDir = true;
        L.dir = strArg(a[0], "Autoload.dir", "المسار", line);
        L.exts = {".rin", ".rcl"};
        if (a.size() > 1 && a[1].type != Value::Type::NIL) {
            if (a[1].type != Value::Type::MAP || !a[1].map)
                throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'Autoload.dir': الخيارات قاموس لكن وُجد نوع ") + a[1].typeName());
            for (const auto& kv : *a[1].map) {
                if (kv.first.type != Value::Type::STRING) continue;
                if (kv.first.str == "prefix") L.prefix = strArg(kv.second, "Autoload.dir", "prefix", line);
                else if (kv.first.str == "ext") {
                    L.exts.clear();
                    if (kv.second.type == Value::Type::STRING) L.exts.push_back(kv.second.str);
                    else if (kv.second.type == Value::Type::ARRAY && kv.second.array) { for (const auto& x : *kv.second.array) L.exts.push_back(strArg(x, "Autoload.dir", "ext", line)); }
                    else throw diagErr(diag::Code::E0004_InvalidType, line, "'Autoload.dir': ext نص أو مصفوفة نصوص");
                    for (const auto& e : L.exts)
                        if (e != ".rin" && e != ".rcl") throw diagErr(diag::Code::E0007_InvalidArguments, line, "'Autoload.dir': الامتداد المسموح .rin أو .rcl فقط، وُجد '" + e + "'");
                } else throw diagErr(diag::Code::E0016_InvalidProperty, line, "'Autoload.dir': خيار غير معروف \"" + kv.first.str + "\" (المتاح: prefix, ext)");
            }
        }
        S->loaders.push_back(L);
        autoloadActive_ = true;
        return Value::num(static_cast<double>(S->loaders.size()));
    };
    natives["Autoload.include"] = [this, need, strArg](Args& a, int line) -> Value {   // ينفّذ ملف .rin/.rcl مرة واحدة في النطاق العام
        need("Autoload.include", a, 1, 1, line);
        std::string p = strArg(a[0], "Autoload.include", "المسار", line);
        if (p.find("..") != std::string::npos)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'Autoload.include': المسار لا يحتوي '..'");
        return Value::boolean_(autoloadInclude(p, line, p));
    };
    natives["Autoload.load"] = [this, S, need, strArg](Args& a, int line) -> Value {
        need("Autoload.load", a, 1, 1, line);
        std::string n = strArg(a[0], "Autoload.load", "الاسم", line);
        if (globals->lookup(n) || nsResolveHead(n, globals)) return Value::boolean_(true);
        size_t before = S->files.size();
        bool ok = autoloadMiss(n, globals, line);
        return Value::boolean_(ok || S->files.size() > before);   // اسم منقوط (a.b) يكفي فيه تنفيذ ملفه
    };
    natives["Autoload.loaded"] = [S, need](Args& a, int line) -> Value {
        need("Autoload.loaded", a, 0, 0, line);
        ArrayData out;
        for (const auto& n : S->loaded) out.push_back(Value::string(n));
        return Value::makeArray(std::make_shared<ArrayData>(std::move(out)));
    };
    natives["Autoload.isLoaded"] = [S, need, strArg](Args& a, int line) -> Value {
        need("Autoload.isLoaded", a, 1, 1, line);
        std::string n = strArg(a[0], "Autoload.isLoaded", "الاسم", line);
        return Value::boolean_(std::find(S->loaded.begin(), S->loaded.end(), n) != S->loaded.end());
    };
    natives["Autoload.loaders"] = [S, need](Args& a, int line) -> Value {
        need("Autoload.loaders", a, 0, 0, line);
        return Value::num(static_cast<double>(S->loaders.size()));
    };
    natives["Autoload.clear"] = [this, S, need](Args& a, int line) -> Value {
        need("Autoload.clear", a, 0, 0, line);
        size_t n = S->loaders.size();
        S->loaders.clear(); S->loaded.clear(); S->files.clear();
        autoloadActive_ = false;
        return Value::num(static_cast<double>(n));
    };
}

} // namespace rin
