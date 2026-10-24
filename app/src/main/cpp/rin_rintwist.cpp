// ============================================================================
//  rin_rintwist.cpp — RINTWIST: متغيّر مضمَّن للقراءة فقط يصف «أين ومتى وما الذي يعمل الآن»
// ----------------------------------------------------------------------------
//  يُضمَّن في نهاية rin_interpreter.cpp (بعد rin_libdir.cpp). لا يُخزَّن في أي نطاق: كل قراءة لـ RINTWIST
//  تبني خريطة جديدة (لقطة) فتكون الحقول الزمنية دقيقة دائماً، ولا كلفة على أي برنامج لا يستعمله.
//
//    RINTWIST = {
//      name, path,                       ← اسم/مسار الملف الجاري تنفيذه الآن (اختصار لـ file.name/file.path)
//      file:   { path, name, stem, ext, dir, kind, part, main, depth },
//      lib:    { active, kind, name, version, root, part, loaded:[..] },
//      app:    { name, version, platform, arch, mode, nativeAllowed },
//      server: { host, pid, os, cwd, root },
//      date:   { iso, utc, date, time, year, month, day, hour, minute, second, weekday, weekdayName,
//                monthName, dayOfYear, unix, ms, tzMinutes },
//      run:    { uptimeMs, startedUnixMs, args },
//      env:    { lang, tz },
//      stack:  [ {depth, kind, name, version, path, part} ... ]   ← سلسلة الاستيراد من الملف الرئيسي إلى الحالي
//    }
//
//    rintwist.get("lib.name", default)     ← قراءة حقل بمسار منقَّط (يدعم فهارس المصفوفات: "stack.1.name")
//    rintwist.fmt("{file.stem}-{date.date}.log", {extra})   ← قالب نصي (لتسمية الملفات والسجلات)، {{ و }} للأقواس
//    rintwist.stack()                       ← سلسلة الاستيراد
//
//  file.kind: "main" (الملف الرئيسي) | "library-part" (جزء من مكتبة) | "tool-token" (توكن أداة OG: ملف واحد)
//  القيمة لقطة وقت القراءة: لتعرف مكتبةٌ نفسها من داخل دوالها، التقطها عند التحميل:  export let TW = RINTWIST;
// ============================================================================
#include <chrono>
#include <ctime>
#include <filesystem>
#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace rin {

Interpreter::ExecFrameScope::ExecFrameScope(Interpreter& in, const std::string& path, const LibraryUnit* unit) : in_(in) {
    ExecFrame f;
    f.path = path;
    if (unit && !unit->root.empty()) {
        f.kind = "library";
        f.libName = unit->name;
        f.libVersion = unit->version;
        f.libRoot = unit->root;
        f.path = unit->root;
    } else {
        f.kind = "tool-token";
        f.libName = libint::stripLibSuffix(libint::baseOf(path));
    }
    in.execFrames_.push_back(std::move(f));
}
Interpreter::ExecFrameScope::~ExecFrameScope() { if (!in_.execFrames_.empty()) in_.execFrames_.pop_back(); }

namespace {
const char* rtPlatform() {
#if defined(__ANDROID__)
    return "android";
#elif defined(__EMSCRIPTEN__)
    return "wasm";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}
const char* rtArch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__wasm__)
    return "wasm32";
#else
    return "unknown";
#endif
}
std::string rtHost() {
#if defined(_WIN32)
    const char* h = std::getenv("COMPUTERNAME");
    return h ? h : "localhost";
#elif defined(__EMSCRIPTEN__)
    return "localhost";
#else
    char buf[256] = {0};
    if (gethostname(buf, sizeof buf - 1) == 0 && buf[0]) return buf;
    return "localhost";
#endif
}
bool rtDebug() {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}
std::string rtCompiler() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_FULL_VER);
#else
    return "unknown";
#endif
}
std::string envOr(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
const char* const kWeekdays[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
const char* const kMonths[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
std::string pad2(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }
} // namespace

Value Interpreter::rintwistValue() const {
    using namespace extra4;
    namespace fs = std::filesystem;

    // ---- file / lib: من أحدث إطار استيراد (أو الملف الرئيسي) ----
    const ExecFrame* fr = execFrames_.empty() ? nullptr : &execFrames_.back();
    std::string path, kind, part;
    if (!fr) { path = sourceFile; kind = "main"; }
    else if (fr->kind == "library") { part = fr->part; path = part.empty() ? fr->libRoot : fr->libRoot + "/" + part; kind = "library-part"; }
    else { path = fr->path; kind = "tool-token"; }
    const std::string name = libint::baseOf(path);
    const size_t dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    const std::string ext = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot + 1);
    std::string dir = libint::parentOf(path);
    if (!dir.empty() && dir.back() == '/') dir.pop_back();

    Value file = M4({{"path", S4(path)}, {"name", S4(name)}, {"stem", S4(stem)}, {"ext", S4(ext)}, {"dir", S4(dir)},
                     {"kind", S4(kind)}, {"part", S4(part)}, {"main", S4(sourceFile)},
                     {"depth", N4(static_cast<double>(execFrames_.size()))}});

    std::vector<std::string> loaded(loadedLibOrder_.begin(), loadedLibOrder_.end());
    Value lib = M4({{"active", B4(fr != nullptr)}, {"kind", S4(fr ? fr->kind : std::string())},
                    {"name", S4(fr ? fr->libName : std::string())}, {"version", S4(fr ? fr->libVersion : std::string())},
                    {"root", S4(fr ? (fr->kind == "library" ? fr->libRoot : fr->path) : std::string())},
                    {"part", S4(fr ? fr->part : std::string())}, {"loaded", libint::strArr(loaded)}});

    // ---- app ----
#if defined(__ANDROID__)
    const char* mode = "android";
#elif defined(__EMSCRIPTEN__)
    const char* mode = "wasm";
#else
    const char* mode = "cli";
#endif
    Value app = M4({{"name", S4("rin")}, {"version", S4(RIN_VERSION_STRING)}, {"platform", S4(rtPlatform())},
                    {"arch", S4(rtArch())}, {"mode", S4(mode)}, {"nativeAllowed", B4(nativeAllowed())},
                    {"debug", B4(rtDebug())}, {"compiler", S4(rtCompiler())}, {"cppStd", N4(static_cast<double>(__cplusplus))}});

    // ---- server (الجهاز/العملية المضيفة) ----
    std::error_code ec;
    std::string cwd = fs::current_path(ec).generic_string();
    if (ec) cwd.clear();
#if defined(_WIN32)
    const double pid = static_cast<double>(_getpid());
#elif defined(__EMSCRIPTEN__)
    const double pid = 1;
#else
    const double pid = static_cast<double>(getpid());
#endif
    Value server = M4({{"host", S4(rtHost())}, {"pid", N4(pid)}, {"os", S4(rtPlatform())},
                       {"cwd", S4(cwd)}, {"root", S4(basePath)}});

    // ---- date (محلّي) ----
    const auto nowTp = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(nowTp);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(nowTp.time_since_epoch()).count();
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &t);
    long tzSec = 0; _get_timezone(&tzSec); long tzMin = -tzSec / 60 + (lt.tm_isdst > 0 ? 60 : 0);
#else
    localtime_r(&t, &lt);
    long tzMin = lt.tm_gmtoff / 60;
#endif
    const std::string dateStr = std::to_string(lt.tm_year + 1900) + "-" + pad2(lt.tm_mon + 1) + "-" + pad2(lt.tm_mday);
    const std::string timeStr = pad2(lt.tm_hour) + ":" + pad2(lt.tm_min) + ":" + pad2(lt.tm_sec);
    const int ah = static_cast<int>(std::labs(tzMin) / 60), am = static_cast<int>(std::labs(tzMin) % 60);
    const std::string iso = dateStr + "T" + timeStr + (tzMin < 0 ? "-" : "+") + pad2(ah) + ":" + pad2(am);
    std::tm gt{};
#if defined(_WIN32)
    gmtime_s(&gt, &t);
#else
    gmtime_r(&t, &gt);
#endif
    const std::string utcIso = std::to_string(gt.tm_year + 1900) + "-" + pad2(gt.tm_mon + 1) + "-" + pad2(gt.tm_mday) + "T" +
                               pad2(gt.tm_hour) + ":" + pad2(gt.tm_min) + ":" + pad2(gt.tm_sec) + "Z";
    Value date = M4({{"iso", S4(iso)}, {"utc", S4(utcIso)}, {"date", S4(dateStr)}, {"time", S4(timeStr)},
                     {"weekdayName", S4(kWeekdays[(lt.tm_wday % 7 + 7) % 7])}, {"monthName", S4(kMonths[(lt.tm_mon % 12 + 12) % 12])},
                     {"dayOfYear", N4(lt.tm_yday + 1)},
                     {"year", N4(lt.tm_year + 1900)}, {"month", N4(lt.tm_mon + 1)}, {"day", N4(lt.tm_mday)},
                     {"hour", N4(lt.tm_hour)}, {"minute", N4(lt.tm_min)}, {"second", N4(lt.tm_sec)},
                     {"weekday", N4(lt.tm_wday)}, {"unix", N4(static_cast<double>(t))},
                     {"ms", N4(static_cast<double>(ms))}, {"tzMinutes", N4(static_cast<double>(tzMin))}});

    std::vector<Value> stack;
    {
        stack.push_back(M4({{"depth", N4(0)}, {"kind", S4("main")}, {"name", S4(libint::baseOf(sourceFile))}, {"version", S4("")},
                            {"path", S4(sourceFile)}, {"part", S4("")}}));
        double d = 1;
        for (auto& f : execFrames_) {
            stack.push_back(M4({{"depth", N4(d++)}, {"kind", S4(f.kind)}, {"name", S4(f.libName)}, {"version", S4(f.libVersion)},
                                {"path", S4(f.kind == "library" ? f.libRoot : f.path)}, {"part", S4(f.part)}}));
        }
    }
    const double up = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt_).count();
    Value run = M4({{"uptimeMs", N4(std::floor(up))}, {"startedUnixMs", N4(static_cast<double>(startedUnixMs_))},
                    {"args", libint::strArr(programArgs_)}});
    Value env = M4({{"lang", S4(envOr("LANG"))}, {"tz", S4(envOr("TZ"))}});
    return M4({{"name", S4(name)}, {"path", S4(path)}, {"file", file}, {"lib", lib}, {"app", app},
               {"server", server}, {"date", date}, {"run", run}, {"env", env}, {"stack", extra4::A4(std::move(stack))}});
}


// ---------------------------------------------------------------------------
//  rintwist.* — قراءة منقَّطة وقوالب نصية فوق لقطة RINTWIST
// ---------------------------------------------------------------------------
namespace {
// يحلّ مساراً منقَّطاً داخل قيمة (خرائط بالمفتاح، مصفوفات بالفهرس). nullptr = غير موجود.
const Value* rtResolve(const Value& root, const std::string& path) {
    const Value* cur = &root;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find('.', i);
        if (j == std::string::npos) j = path.size();
        const std::string seg = path.substr(i, j - i);
        if (seg.empty()) return nullptr;
        if (cur->type == Value::Type::MAP && cur->map) {
            const Value* nxt = nullptr;
            for (const auto& kv : *cur->map)
                if (kv.first.type == Value::Type::STRING && kv.first.str == seg) { nxt = &kv.second; break; }
            if (!nxt) return nullptr;
            cur = nxt;
        } else if (cur->type == Value::Type::ARRAY && cur->array) {
            char* endp = nullptr;
            long idx = std::strtol(seg.c_str(), &endp, 10);
            if (!endp || *endp != 0 || idx < 0 || static_cast<size_t>(idx) >= cur->array->size()) return nullptr;
            cur = &(*cur->array)[static_cast<size_t>(idx)];
        } else return nullptr;
        i = j + 1;
        if (j == path.size()) break;
    }
    return cur;
}
} // namespace

void Interpreter::registerNativesRintwist() {
    using Args = std::vector<Value>;
    natives["rintwist.get"] = [this](Args& a, int line) -> Value {
        expectArgsRange("rintwist.get", a, 1, 2, line);
        const std::string path = asString(a[0], "rintwist.get", line);
        Value snap = rintwistValue();
        const Value* v = rtResolve(snap, path);
        if (v) return *v;
        return a.size() > 1 ? a[1] : Value::nil();
    };
    natives["rintwist.stack"] = [this](Args& a, int line) -> Value {
        expectArgsRange("rintwist.stack", a, 0, 0, line);
        Value snap = rintwistValue();
        const Value* v = rtResolve(snap, "stack");
        return v ? *v : Value::nil();
    };
    // rintwist.fmt("{file.stem}-{date.date}.log") — الأقواس المزدوجة {{ و }} تُنتج { و } حرفياً.
    // وسيط ثانٍ اختياري (map) تُقدَّم مفاتيحه على حقول RINTWIST: rintwist.fmt("{user}@{server.host}", {user: "jo"})
    natives["rintwist.fmt"] = [this](Args& a, int line) -> Value {
        expectArgsRange("rintwist.fmt", a, 1, 2, line);
        const std::string tpl = asString(a[0], "rintwist.fmt", line);
        if (a.size() > 1 && a[1].type != Value::Type::MAP)
            throw err(diag::Code::E0004_InvalidType, line, "'rintwist.fmt' expects a map as its second argument but got " + a[1].typeName());
        Value snap = rintwistValue();
        std::string out;
        for (size_t i = 0; i < tpl.size(); ++i) {
            char c = tpl[i];
            if (c == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') { out += '{'; ++i; continue; }
            if (c == '}' && i + 1 < tpl.size() && tpl[i + 1] == '}') { out += '}'; ++i; continue; }
            if (c != '{') { out += c; continue; }
            size_t close = tpl.find('}', i + 1);
            if (close == std::string::npos)
                throw errWithReason(diag::Code::E0035_RuntimeError, line, "rintwist.fmt: unclosed '{' in template", "template: " + tpl);
            const std::string key = tpl.substr(i + 1, close - i - 1);
            const Value* v = nullptr;
            if (a.size() > 1) {
                std::string head = key.substr(0, key.find('.'));
                if (const Value* ev = extra4::o4get(a[1], head)) v = (head == key) ? ev : rtResolve(*ev, key.substr(head.size() + 1));
            }
            if (!v) v = rtResolve(snap, key);
            if (!v)
                throw errWithReason(diag::Code::E0035_RuntimeError, line, "rintwist.fmt: unknown field `" + key + "`",
                                    "use a path such as {file.name}, {lib.name}, {date.iso}, {app.version}, {server.host}");
            out += v->toDisplayString();
            i = close;
        }
        return Value::string(out);
    };
}

} // namespace rin
