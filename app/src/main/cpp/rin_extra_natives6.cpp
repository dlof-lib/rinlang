// ============================================================================
//  rin_extra_natives6.cpp — عائلة الوسائط تحت مفهوم `make`:
//    فيديو · صوت · API · إزالة خلفية الصور · قراءة النص من الصور (OCR)
// ----------------------------------------------------------------------------
//  دفعة سادسة فوق rin_extra_natives*.cpp دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra6() وتُضمَّن (#include) في نهاية rin_interpreter.cpp
//  (كالدفعة الخامسة تماماً)، فتدخل كل أهداف البناء بلا أي تعديل في ملفات البناء.
//
//  الدوال (كلها داخل مساحة الاسم make — نفس مساحة make.qr / make.file / make.hash):
//
//    make.media.tools()                          |  ما المتاح فعلاً على هذا الجهاز (أدوات/لغات OCR)
//
//    make.video(src[, opts])                     |  فتح/تشغيل فيديو (ملف أو http/https)
//    make.video.info(src)                        |  مدة/أبعاد/ترميز/إطارات... (ffprobe)
//    make.video.thumbnail(src, out[, opts])      |  استخراج إطار كصورة (ffmpeg)
//    make.video.stop()                           |  إيقاف المشغّلات التي فتحها make.video
//
//    make.audio(src[, opts])                     |  تشغيل صوت (ملف أو رابط)، wait=true للانتظار
//    make.audio.info(src)                        |  مدة/ترميز/قنوات/معدّل العيّنات
//    make.audio.stop()                           |  إيقاف الصوت الجاري
//
//    make.api(url[, opts])                       |  جلب بيانات API: إعادة محاولة بتراجع أسّي، مصادقة،
//                                                |  ?query تلقائي، كاش بمدة، استخراج مسار JSON، expect
//    make.api.download(url, path[, opts])        |  تنزيل ملف ثنائي إلى مسار داخل المشروع
//    make.api.clearCache()                       |  تفريغ كاش make.api
//
//    make.image.removeBg(src[, out][, opts])     |  إزالة الخلفية -> PNG/WebP بشفافية
//    make.image.text(src[, opts])  =  make.ocr   |  استخراج النص (tesseract) مع الثقة والكلمات
//
//  القواعد المشتركة:
//    * فشل بيئي (أداة مفقودة/شبكة/ملف غير صالح) لا يرمي خطأً: يُرجع {ok:false, error:"..."}.
//      سوء استخدام الدالة (نوع وسيط خاطئ/عدد وسائط) يرمي خطأً صريحاً كبقية اللغة.
//    * كل مسار محلي يمرّ عبر resolvePath فيبقى داخل مجلد المشروع المعزول.
//    * لا shell إطلاقاً: argv مباشرة (rin_media.h)، والصور تُعرَّف بالبايتات لا بالامتداد.
//    * على أندرويد تُمرَّر العملية إلى Kotlin عبر rin::media::setBridge (RinMediaBridge.kt).
//  الشرح الكامل: docs/MAKE_MEDIA.md
// ============================================================================
#include "rin_interpreter.h"
#include "rin_media.h"
#include "rin_json.h"
#include "rin_http.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>
#include <unordered_map>
#ifdef RIN_MEDIA_POSIX
#  include <dirent.h>
#endif

namespace rin {
namespace extra6 {

using Args = std::vector<Value>;
namespace md = rin::media;

// ------------------------------------------------------------------ قيم صغيرة
static Value S6(const std::string& s) { return Value::string(s); }
static Value N6(double d) { return Value::num(d); }
static Value B6(bool b) { return Value::boolean_(b); }
static Value A6(std::vector<Value> v) { return Value::makeArray(std::make_shared<ArrayData>(std::move(v))); }
static Value M6(std::vector<std::pair<std::string, Value>> kv) {
    MapData out;
    out.reserve(kv.size());
    for (auto& p : kv) out.push_back({Value::string(p.first), std::move(p.second)});
    return Value::makeMap(std::make_shared<MapData>(std::move(out)));
}

static void argc6(const char* fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi)
        throw diagErr(diag::Code::E0007_InvalidArguments, line,
                      std::string("'") + fn + "' expects " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) +
                          " argument(s) but got " + std::to_string(a.size()));
}
static std::string needStr(const Value& v, const char* fn, const char* what, int line) {
    if (v.type != Value::Type::STRING)
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      std::string("'") + fn + "': " + what + " يجب أن يكون نصاً، لكن وُجد `" + v.typeName() + "`");
    return v.str;
}
static void needOpts(const Value& v, const char* fn, int line) {
    if (v.type != Value::Type::NIL && v.type != Value::Type::MAP)
        throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': الخيارات يجب أن تكون قاموساً أو nil");
}

static const Value* opt6(const Value& o, const char* key) {
    if (o.type != Value::Type::MAP || !o.map) return nullptr;
    for (auto& kv : *o.map) if (kv.first.type == Value::Type::STRING && kv.first.str == key) return &kv.second;
    return nullptr;
}
static std::string optS(const Value& o, const char* k, const std::string& def = "") {
    const Value* v = opt6(o, k);
    return (v && v->type != Value::Type::NIL) ? v->toDisplayString() : def;
}
static double optN(const Value& o, const char* k, double def) {
    const Value* v = opt6(o, k);
    if (v && v->type == Value::Type::NUMBER && !std::isnan(v->number)) return v->number;
    return def;
}
static bool optB(const Value& o, const char* k, bool def) {
    const Value* v = opt6(o, k);
    if (v && v->type == Value::Type::BOOL) return v->boolean;
    return def;
}
static bool optHas(const Value& o, const char* k) { const Value* v = opt6(o, k); return v && v->type != Value::Type::NIL; }

static Value fail6(const std::string& error, std::vector<std::pair<std::string, Value>> extra = {}) {
    std::vector<std::pair<std::string, Value>> kv{{"ok", B6(false)}, {"error", S6(error)}};
    for (auto& e : extra) kv.push_back(std::move(e));
    return M6(std::move(kv));
}

static double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static std::string numStr(double v) {
    char b[48];
    if (v == std::floor(v) && std::fabs(v) < 1e12) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
    else std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

// ------------------------------------------------------------------ مسارات
static std::string extOf(const std::string& p) {
    size_t s = p.find_last_of('/'), d = p.find_last_of('.');
    if (d == std::string::npos || (s != std::string::npos && d < s)) return "";
    return md::lower(p.substr(d + 1));
}
static std::string stemOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    std::string f = s == std::string::npos ? p : p.substr(s + 1);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? f : f.substr(0, d);
}
static std::string dirOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}
static bool copyFile6(const std::string& from, const std::string& to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return static_cast<bool>(out);
}

// مجلد مؤقت يُنظَّف تلقائياً: نعمل دائماً على نسخ بأسماء آمنة داخله (in.png / out.png ...) فلا
// تؤثّر الأسماء الغريبة (أقواس/%/@/مسافات) في ImageMagick/tesseract/ffmpeg أبداً.
struct TempDir {
    std::string path;
    std::vector<std::string> files;
    TempDir() {
#ifdef RIN_MEDIA_POSIX
        const char* base = std::getenv("TMPDIR");
        std::string tpl = std::string(base && *base ? base : "/tmp") + "/rinmedia-XXXXXX";
        std::vector<char> buf(tpl.begin(), tpl.end());
        buf.push_back('\0');
        if (mkdtemp(buf.data())) path = buf.data();
#endif
    }
    ~TempDir() {
        for (auto& f : files) std::remove(f.c_str());
#ifdef RIN_MEDIA_POSIX
        if (!path.empty()) rmdir(path.c_str());
#endif
    }
    bool ok() const { return !path.empty(); }
    std::string file(const std::string& name) { std::string f = path + "/" + name; files.push_back(f); return f; }
};

using Argv = md::Argv;
using Resolver = std::function<std::string(const std::string&, int)>;
static std::string fetchToTemp(const std::string& url, const std::string& tmpFile, std::string& error);   // تُعرَّف قرب make.api.download

static std::string trimStr6(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
static std::string firstLine(const std::string& s) {
    std::string t = trimStr6(s);
    size_t nl = t.find('\n');
    return nl == std::string::npos ? t : trimStr6(t.substr(0, nl));
}
static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t nl = s.find('\n', i);
        std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        out.push_back(ln);
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return out;
}
static std::vector<std::string> splitTab(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (true) {
        size_t t = s.find('\t', i);
        out.push_back(s.substr(i, t == std::string::npos ? std::string::npos : t - i));
        if (t == std::string::npos) break;
        i = t + 1;
    }
    return out;
}

// ------------------------------------------------------------------ جرد الأدوات
static std::string imTool() { return md::firstTool({"magick", "convert"}); }
static Argv imRun(const std::string& tool, Argv rest) {
    Argv a{tool};
    a.insert(a.end(), rest.begin(), rest.end());
    return a;
}
static Argv imIdentify(const std::string& tool, Argv rest) {
    Argv a;
    if (tool == "magick") a = {"magick", "identify"}; else a = {"identify"};
    a.insert(a.end(), rest.begin(), rest.end());
    return a;
}
static const std::vector<std::string>& tesseractLangs() {
    static std::vector<std::string> langs;
    static bool done = false;
    if (done) return langs;
    done = true;
    md::ProcResult r = md::run({"tesseract", "--list-langs"}, 10000);
    if (r.started && r.exitCode == 0) {
        auto lines = splitLines(r.out + "\n" + r.err);
        for (auto& l : lines) {
            std::string t = trimStr6(l);
            if (t.empty() || t.rfind("List of", 0) == 0 || t == "osd") continue;
            langs.push_back(t);
        }
    }
    return langs;
}
static bool langAvailable(const std::string& l) {
    for (auto& x : tesseractLangs()) if (x == l) return true;
    return false;
}
static bool hasDisplay() {
#if defined(__linux__) && !defined(__ANDROID__)
    const char* d = std::getenv("DISPLAY");
    const char* w = std::getenv("WAYLAND_DISPLAY");
    return (d && *d) || (w && *w);
#else
    return true;
#endif
}

// ------------------------------------------------------------------ جسر المنصة (أندرويد)
static md::KV bridgeKV(const Value& opts, md::KV base) {
    if (opts.type == Value::Type::MAP && opts.map)
        for (auto& kv : *opts.map)
            if (kv.first.type == Value::Type::STRING && kv.second.type != Value::Type::NIL &&
                kv.second.type != Value::Type::MAP && kv.second.type != Value::Type::ARRAY)
                base.push_back({kv.first.str, kv.second.toDisplayString()});
    return base;
}
static bool numericField(const std::string& k) {
    return k == "confidence" || k == "bytes" || k == "width" || k == "height" || k == "wordCount" || k == "duration" || k == "pid";
}
static std::vector<std::string> splitLines(const std::string& s);
static Value bridgeValue(const md::BridgeResult& r) {
    std::vector<std::pair<std::string, Value>> kv{{"ok", B6(r.ok)}};
    if (!r.ok) kv.push_back({"error", S6(r.error.empty() ? "فشلت العملية على هذه المنصة" : r.error)});
    for (auto& f : r.fields) {
        if (numericField(f.first)) {
            char* end = nullptr;
            double d = std::strtod(f.second.c_str(), &end);
            if (end && *end == '\0' && !f.second.empty()) { kv.push_back({f.first, N6(d)}); continue; }
        }
        kv.push_back({f.first, S6(f.second)});
    }
    for (auto& f : r.fields) if (f.first == "text") {      // OCR عبر الجسر: نشتق lines من النص (سطر لكل عنصر)
        std::vector<Value> ln;
        for (auto& l : splitLines(f.second)) if (!l.empty()) ln.push_back(S6(l));
        kv.push_back({"lines", A6(std::move(ln))});
        break;
    }
    kv.push_back({"backend", S6("bridge")});
    return M6(std::move(kv));
}
// يعيد true وقيمة النتيجة إن عالج الجسر العملية.
static bool tryBridge(const char* op, const md::KV& args, Value& out) {
    if (!md::hasBridge()) return false;
    md::BridgeResult r = md::bridgeSlot()(op, args);
    if (!r.handled) return false;
    out = bridgeValue(r);
    return true;
}

// ------------------------------------------------------------------ تحليل مصدر وسائط
struct Source {
    bool ok = false;
    bool isUrl = false;
    std::string arg;    // ما يُمرَّر كوسيط (رابط، أو مسار لا يبدأ بـ '-')
    std::string error;
};
static Source resolveSource(const std::string& raw, const Resolver& resolve, int line) {
    Source s;
    if (raw.empty()) { s.error = "المصدر فارغ"; return s; }
    if (md::isHttpUrl(raw)) {
        if (!md::safeArg(raw)) { s.error = "رابط غير صالح"; return s; }
        s.ok = true; s.isUrl = true; s.arg = raw;
        return s;
    }
    if (raw.find("://") != std::string::npos) { s.error = "البروتوكول غير مسموح — المتاح http:// و https:// فقط"; return s; }
    std::string p = resolve(raw, line);
    if (!md::fileExists(p)) { s.error = "الملف غير موجود: " + raw; return s; }
    if (!p.empty() && p[0] == '-') p = "./" + p;
    if (!md::safeArg(p)) { s.error = "مسار غير صالح"; return s; }
    s.ok = true; s.arg = p;
    return s;
}
static Argv protoGuard() { return {"-protocol_whitelist", "file,http,https,tcp,tls,crypto"}; }

static std::vector<long> g_videoPids;
static std::vector<long> g_audioPids;

static Value stopPids(std::vector<long>& pids) {
    int n = 0;
    for (long p : pids) if (md::pidAlive(p) && md::killPid(p)) ++n;
    pids.clear();
    return M6({{"ok", B6(true)}, {"stopped", N6(n)}});
}

// ------------------------------------------------------------------ معلومات الوسائط (ffprobe)
static double parseRate(const std::string& r) {
    size_t sl = r.find('/');
    if (sl == std::string::npos) return std::atof(r.c_str());
    double n = std::atof(r.substr(0, sl).c_str()), d = std::atof(r.substr(sl + 1).c_str());
    return d == 0 ? 0 : n / d;
}
static Value mediaInfo(const Source& src, const char* fn, bool wantVideo) {
    if (md::which("ffprobe").empty())
        return fail6(std::string(fn) + ": يحتاج الأداة ffprobe (ضمن ffmpeg) — ثبّتها: sudo apt install ffmpeg");
    Argv av{"ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams"};
    if (src.isUrl) { auto g = protoGuard(); av.insert(av.end(), g.begin(), g.end()); }
    av.push_back("-i"); av.push_back(src.arg);
    md::ProcResult r = md::run(av, 45000);
    if (!r.started) return fail6(r.error);
    if (r.timedOut) return fail6("انتهت مهلة قراءة معلومات الملف");
    if (r.exitCode != 0) return fail6("تعذّر قراءة الملف (غير صالح أو غير مدعوم): " + firstLine(r.err));
    Value j = json::decodeOrRaw(r.out);
    if (j.type != Value::Type::MAP) return fail6("ردّ ffprobe غير مفهوم");

    double duration = 0, size = 0, bitrate = 0;
    std::string format;
    if (const Value* f = opt6(j, "format")) {
        duration = std::atof(optS(*f, "duration", "0").c_str());
        size = std::atof(optS(*f, "size", "0").c_str());
        bitrate = std::atof(optS(*f, "bit_rate", "0").c_str());
        format = optS(*f, "format_name");
    }
    bool hasVideo = false, hasAudio = false;
    double width = 0, height = 0, fps = 0, channels = 0, sampleRate = 0;
    std::string vcodec, acodec;
    if (const Value* st = opt6(j, "streams")) if (st->type == Value::Type::ARRAY && st->array) {
        for (auto& s : *st->array) {
            std::string type = optS(s, "codec_type");
            if (type == "video") {
                bool cover = false;
                if (const Value* d = opt6(s, "disposition")) cover = optN(*d, "attached_pic", 0) == 1;
                if (cover || hasVideo) continue;       // غلاف ألبوم داخل ملف صوتي ليس فيديو
                hasVideo = true;
                width = optN(s, "width", 0); height = optN(s, "height", 0);
                vcodec = optS(s, "codec_name");
                fps = parseRate(optS(s, "avg_frame_rate", optS(s, "r_frame_rate", "0")));
            } else if (type == "audio" && !hasAudio) {
                hasAudio = true;
                acodec = optS(s, "codec_name");
                channels = optN(s, "channels", 0);
                sampleRate = std::atof(optS(s, "sample_rate", "0").c_str());
            }
        }
    }
    if (wantVideo && !hasVideo) return fail6(std::string(fn) + ": الملف لا يحوي مسار فيديو", {{"hasAudio", B6(hasAudio)}});
    if (!wantVideo && !hasAudio) return fail6(std::string(fn) + ": الملف لا يحوي مسار صوت", {{"hasVideo", B6(hasVideo)}});
    std::vector<std::pair<std::string, Value>> kv{
        {"ok", B6(true)}, {"duration", N6(std::floor(duration * 1000 + 0.5) / 1000)}, {"size", N6(size)},
        {"bitrate", N6(bitrate)}, {"format", S6(format)}, {"hasVideo", B6(hasVideo)}, {"hasAudio", B6(hasAudio)}};
    if (hasVideo) {
        kv.push_back({"width", N6(width)}); kv.push_back({"height", N6(height)});
        kv.push_back({"fps", N6(std::floor(fps * 100 + 0.5) / 100)}); kv.push_back({"videoCodec", S6(vcodec)});
    }
    if (hasAudio) {
        kv.push_back({"audioCodec", S6(acodec)}); kv.push_back({"channels", N6(channels)});
        kv.push_back({"sampleRate", N6(sampleRate)});
    }
    return M6(std::move(kv));
}

// ------------------------------------------------------------------ بناء أمر المشغّل
struct PlayPlan {
    std::string backend;
    Argv argv;
    std::vector<std::string> ignored;   // خيارات طلبها المستخدم ولا يدعمها هذا المشغّل
    std::string error;
};

static std::string volStr(double v) { return numStr(clampD(v, 0, 100)); }

static PlayPlan planVideo(const std::string& wanted, const Source& src, const Value& o) {
    PlayPlan p;
    static const std::vector<std::string> allowed{"mpv", "vlc", "ffplay", "xdg-open", "open"};
    std::string b = wanted;
    if (!b.empty() && std::find(allowed.begin(), allowed.end(), b) == allowed.end()) {
        p.error = "المشغّل '" + b + "' غير مسموح — المتاح: mpv, vlc, ffplay, xdg-open, open";
        return p;
    }
    if (b.empty()) b = md::firstTool({"mpv", "vlc", "ffplay", "xdg-open", "open"});
    if (b.empty()) {
        p.error = "لا يوجد مشغّل فيديو مثبّت — ثبّت واحداً: sudo apt install mpv (أو vlc / ffmpeg)";
        return p;
    }
    bool fs = optB(o, "fullscreen", false), loop = optB(o, "loop", false), mute = optB(o, "mute", false);
    double start = optN(o, "start", 0), vol = optN(o, "volume", -1);
    p.backend = b;
    if (b == "mpv") {
        p.argv = {"mpv", "--force-window=yes"};
        if (fs) p.argv.push_back("--fullscreen");
        if (loop) p.argv.push_back("--loop-file=inf");
        if (mute) p.argv.push_back("--mute=yes");
        if (start > 0) p.argv.push_back("--start=" + numStr(start));
        if (vol >= 0) p.argv.push_back("--volume=" + volStr(vol));
        p.argv.push_back("--"); p.argv.push_back(src.arg);
    } else if (b == "vlc") {
        p.argv = {"vlc"};
        if (fs) p.argv.push_back("--fullscreen");
        if (loop) p.argv.push_back("--loop"); else p.argv.push_back("--play-and-exit");
        if (mute) p.argv.push_back("--no-audio");
        if (start > 0) p.argv.push_back("--start-time=" + numStr(start));
        if (vol >= 0) p.ignored.push_back("volume");
        p.argv.push_back(src.arg);
    } else if (b == "ffplay") {
        p.argv = {"ffplay", "-autoexit", "-loglevel", "error"};
        if (fs) p.argv.push_back("-fs");
        if (loop) { p.argv.push_back("-loop"); p.argv.push_back("0"); }
        if (mute) p.argv.push_back("-an");
        if (start > 0) { p.argv.push_back("-ss"); p.argv.push_back(numStr(start)); }
        if (vol >= 0) { p.argv.push_back("-volume"); p.argv.push_back(volStr(vol)); }
        if (src.isUrl) { auto g = protoGuard(); p.argv.insert(p.argv.end(), g.begin(), g.end()); }
        p.argv.push_back("-i"); p.argv.push_back(src.arg);
    } else {   // xdg-open / open: يفتح بالتطبيق الافتراضي للنظام
        p.argv = {b, src.arg};
        if (fs) p.ignored.push_back("fullscreen");
        if (loop) p.ignored.push_back("loop");
        if (mute) p.ignored.push_back("mute");
        if (start > 0) p.ignored.push_back("start");
        if (vol >= 0) p.ignored.push_back("volume");
    }
    return p;
}

static PlayPlan planAudio(const std::string& wanted, const Source& src, const Value& o) {
    PlayPlan p;
    static const std::vector<std::string> allowed{"mpv", "ffplay", "vlc", "afplay", "paplay", "aplay"};
    std::string b = wanted;
    if (!b.empty() && std::find(allowed.begin(), allowed.end(), b) == allowed.end()) {
        p.error = "المشغّل '" + b + "' غير مسموح — المتاح: mpv, ffplay, vlc, afplay, paplay, aplay";
        return p;
    }
    if (b.empty()) {
        // afplay/paplay/aplay لا تقرأ الروابط، فلا تُختار تلقائياً لمصدر http
        b = md::firstTool(src.isUrl ? std::vector<std::string>{"mpv", "ffplay", "vlc"}
                                    : std::vector<std::string>{"mpv", "ffplay", "vlc", "afplay", "paplay", "aplay"});
    }
    if (b.empty()) {
        p.error = "لا يوجد مشغّل صوت مثبّت — ثبّت واحداً: sudo apt install mpv (أو ffmpeg)";
        return p;
    }
    if (src.isUrl && (b == "afplay" || b == "paplay" || b == "aplay")) {
        p.error = "المشغّل '" + b + "' لا يقرأ الروابط — استخدم mpv أو ffplay أو vlc، أو نزّل الملف بـ make.api.download";
        return p;
    }
    bool loop = optB(o, "loop", false);
    double start = optN(o, "start", 0), vol = optN(o, "volume", -1);
    p.backend = b;
    if (b == "mpv") {
        p.argv = {"mpv", "--no-video", "--really-quiet"};
        if (loop) p.argv.push_back("--loop-file=inf");
        if (start > 0) p.argv.push_back("--start=" + numStr(start));
        if (vol >= 0) p.argv.push_back("--volume=" + volStr(vol));
        p.argv.push_back("--"); p.argv.push_back(src.arg);
    } else if (b == "ffplay") {
        p.argv = {"ffplay", "-nodisp", "-autoexit", "-loglevel", "error"};
        if (loop) { p.argv.push_back("-loop"); p.argv.push_back("0"); }
        if (start > 0) { p.argv.push_back("-ss"); p.argv.push_back(numStr(start)); }
        if (vol >= 0) { p.argv.push_back("-volume"); p.argv.push_back(volStr(vol)); }
        if (src.isUrl) { auto g = protoGuard(); p.argv.insert(p.argv.end(), g.begin(), g.end()); }
        p.argv.push_back("-i"); p.argv.push_back(src.arg);
    } else if (b == "vlc") {
        p.argv = {"vlc", "-I", "dummy", "--no-video"};
        p.argv.push_back(loop ? "--loop" : "--play-and-exit");
        if (start > 0) p.argv.push_back("--start-time=" + numStr(start));
        if (vol >= 0) p.ignored.push_back("volume");
        p.argv.push_back(src.arg);
    } else if (b == "afplay") {
        p.argv = {"afplay"};
        if (vol >= 0) { p.argv.push_back("-v"); p.argv.push_back(numStr(clampD(vol, 0, 100) / 100.0)); }
        if (loop) p.ignored.push_back("loop");
        if (start > 0) p.ignored.push_back("start");
        p.argv.push_back(src.arg);
    } else {   // paplay / aplay
        p.argv = {b};
        if (b == "aplay") p.argv.push_back("-q");
        if (loop) p.ignored.push_back("loop");
        if (start > 0) p.ignored.push_back("start");
        if (vol >= 0) p.ignored.push_back("volume");
        p.argv.push_back(src.arg);
    }
    return p;
}

static Value strArray(const std::vector<std::string>& v) {
    std::vector<Value> out;
    for (auto& s : v) out.push_back(S6(s));
    return A6(std::move(out));
}

// ينفّذ الخطة: dryRun -> يعرض الأمر فقط، wait -> متزامن بمهلة، غير ذلك -> خلفية + تتبّع pid.
static Value launch(const PlayPlan& plan, const Source& src, const Value& o, std::vector<long>& pids,
                    bool needsScreen, bool aliveCheck) {
    std::vector<std::pair<std::string, Value>> base{
        {"backend", S6(plan.backend)}, {"command", strArray(plan.argv)}, {"src", S6(src.arg)},
        {"isUrl", B6(src.isUrl)}, {"ignored", strArray(plan.ignored)}};
    auto withBase = [&](std::vector<std::pair<std::string, Value>> head) {
        for (auto& b : base) head.push_back(b);
        return M6(std::move(head));
    };
    if (optB(o, "dryRun", false)) return withBase({{"ok", B6(true)}, {"dryRun", B6(true)}});
    if (needsScreen && !hasDisplay())
        return withBase({{"ok", B6(false)}, {"error", S6("لا توجد شاشة متاحة (DISPLAY/WAYLAND_DISPLAY غير مضبوط) — شغّل البرنامج داخل جلسة رسومية، أو استخدم dryRun: true")}});

    if (optB(o, "wait", false)) {
        int timeoutMs = static_cast<int>(clampD(optN(o, "timeout", 3600000), 1000, 86400000));
        md::ProcResult r = md::run(plan.argv, timeoutMs, 1u << 20);
        if (!r.started) return withBase({{"ok", B6(false)}, {"error", S6(r.error)}});
        if (r.timedOut) return withBase({{"ok", B6(false)}, {"timedOut", B6(true)}, {"error", S6("انتهت مهلة التشغيل")}});
        bool good = r.exitCode == 0;
        std::vector<std::pair<std::string, Value>> head{{"ok", B6(good)}, {"exitCode", N6(r.exitCode)}, {"finished", B6(true)}};
        if (!good) head.push_back({"error", S6("المشغّل انتهى بخطأ: " + firstLine(r.err))});
        return withBase(std::move(head));
    }
    std::string err;
    long pid = md::spawnDetached(plan.argv, err);
    if (pid < 0) return withBase({{"ok", B6(false)}, {"error", S6(err)}});
    if (aliveCheck) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (!md::pidAlive(pid))
            return withBase({{"ok", B6(false)}, {"error", S6("المشغّل أُغلق فوراً — تحقق من أن الملف صالح وأن هناك شاشة/جهاز صوت متاح")}});
    }
    pids.push_back(pid);
    return withBase({{"ok", B6(true)}, {"pid", N6(static_cast<double>(pid))}, {"playing", B6(true)}});
}

// ------------------------------------------------------------------ صور: أدوات مشتركة
using EnsureDir = std::function<void(const std::string&)>;

static std::string coderExt(const std::string& coder) { return coder == "jpeg" ? "jpg" : coder; }

// نسخ الصورة إلى المجلد المؤقت باسم آمن + تعريف نوعها من البايتات. يملأ error عند الفشل.
struct StagedImage { bool ok = false; std::string path, coder, error; };
static StagedImage stageImage(const std::string& srcResolved, TempDir& tmp, const char* name) {
    StagedImage s;
    if (!md::fileExists(srcResolved)) { s.error = "الملف غير موجود: " + srcResolved; return s; }
    s.coder = md::sniffImage(srcResolved);
    if (s.coder.empty()) { s.error = "الملف ليس صورة نقطية مدعومة (png / jpg / webp / gif / bmp / tiff)"; return s; }
    if (!tmp.ok()) { s.error = "تعذّر إنشاء مجلد مؤقت"; return s; }
    s.path = tmp.file(std::string(name) + "." + coderExt(s.coder));
    if (!copyFile6(srcResolved, s.path)) { s.error = "تعذّر نسخ الصورة للمعالجة"; return s; }
    s.ok = true;
    return s;
}
static bool imageSize(const std::string& tool, const StagedImage& im, int& w, int& h) {
    std::string frame = (im.coder == "gif" || im.coder == "tiff" || im.coder == "webp") ? "[0]" : "";
    md::ProcResult r = md::run(imIdentify(tool, {"-format", "%w %h", im.coder + ":" + im.path + frame}), 20000);
    if (!r.started || r.exitCode != 0) return false;
    return std::sscanf(r.out.c_str(), "%d %d", &w, &h) == 2 && w > 0 && h > 0;
}
static bool colorTokenSafe(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '(' || c == ')' || c == ',' || c == '.' || c == '%' || c == '#' || c == ' ')) return false;
    return true;
}

// ------------------------------------------------------------------ make.image.removeBg
// الطريقة "auto": rembg (ذكاء اصطناعي) إن وُجد، وإلا ملء فيضي (flood fill) من حواف الصورة بلون الخلفية
// المكتشف تلقائياً (أكثر ألوان الزوايا الأربع تكراراً) مع تسامح fuzz — يزيل الخلفية المتصلة بالحواف فقط
// فيحافظ على أي منطقة داخلية بنفس اللون (عين بيضاء، نص...). مناسبة لخلفيات الاستوديو/الشاشة/اللقطات.
static Value removeBgImpl(const Resolver& resolve, const EnsureDir& ensureDir, Args& a, int line) {
    argc6("make.image.removeBg", a, 1, 3, line);
    std::string srcRaw = needStr(a[0], "make.image.removeBg", "المصدر", line);
    std::string outRaw;
    Value opts = Value::nil();
    for (size_t i = 1; i < a.size(); ++i) {
        if (a[i].type == Value::Type::STRING) outRaw = a[i].str;
        else if (a[i].type == Value::Type::MAP || a[i].type == Value::Type::NIL) { if (a[i].type == Value::Type::MAP) opts = a[i]; }
        else throw diagErr(diag::Code::E0004_InvalidType, line, "'make.image.removeBg': وسيط غير صالح (متوقّع مسار خرج نصي أو قاموس خيارات)");
    }
    if (outRaw.empty()) outRaw = optS(opts, "out");

    TempDir srcTmp;                       // رابط بدل مسار: يُنزَّل لملف مؤقت بنفس نواة make.api.download ثم يُعالَج كأي ملف محلي
    std::string src;
    if (md::isHttpUrl(srcRaw)) {
        if (outRaw.empty()) return fail6("make.image.removeBg: مع رابط مصدر يجب تحديد مسار الخرج (الوسيط الثاني)");
        if (!md::safeArg(srcRaw)) return fail6("رابط غير صالح — المسموح http:// أو https:// فقط");
        if (!srcTmp.ok()) return fail6("تعذّر إنشاء مجلد مؤقت لتنزيل الصورة");
        std::string derr;
        src = fetchToTemp(srcRaw, srcTmp.file("src.img"), derr);
        if (src.empty()) return fail6("make.image.removeBg: " + derr);
    } else src = resolve(srcRaw, line);
    if (outRaw.empty()) outRaw = dirOf(srcRaw) + stemOf(srcRaw) + ".nobg.png";
    std::string out = resolve(outRaw, line);

    std::string bgFill = optS(opts, "bg");               // لون تعبئة بدل الشفافية (اختياري)
    std::string oext = extOf(out);
    bool opaqueOut = !bgFill.empty() && md::lower(bgFill) != "transparent" && md::lower(bgFill) != "none";
    if (oext != "png" && oext != "webp" && !(opaqueOut && (oext == "jpg" || oext == "jpeg")))
        return fail6("ملف الخرج يجب أن يكون .png أو .webp (للشفافية)" + std::string(opaqueOut ? "، أو .jpg مع bg" : ""));
    std::string outCoder = (oext == "jpg" || oext == "jpeg") ? "jpeg" : oext;
    if (opaqueOut && md::safeColor(bgFill).empty()) return fail6("لون bg غير صالح (استخدم #rrggbb أو اسماً بسيطاً)");

    double tol = clampD(optN(opts, "tolerance", 12), 0, 100);
    double feather = clampD(optN(opts, "feather", 1), 0, 10);
    int erode = static_cast<int>(clampD(optN(opts, "erode", 0), 0, 5));
    bool trim = optB(opts, "trim", false);
    std::string method = md::lower(optS(opts, "method", "auto"));
    if (method != "auto" && method != "ai" && method != "color")
        return fail6("method غير معروفة: '" + method + "' — المتاح: auto | ai | color");
    std::string colorOpt = optS(opts, "color");
    if (!colorOpt.empty() && md::safeColor(colorOpt).empty()) return fail6("لون color غير صالح (استخدم #rrggbb أو اسماً بسيطاً)");

    Value bridged;
    if (tryBridge("image.removeBg", bridgeKV(opts, {{"src", src}, {"out", out}}), bridged)) return bridged;

    TempDir tmp;
    StagedImage in = stageImage(src, tmp, "in");
    if (!in.ok) return fail6(in.error);
    ensureDir(out);

    // 1) الذكاء الاصطناعي (rembg) إن طُلب أو كان متاحاً في الوضع التلقائي
    if (method != "color" && !(opaqueOut || trim)) {
        if (!md::which("rembg").empty()) {
            std::string rbOut = tmp.file("rembg.png");
            md::ProcResult r = md::run({"rembg", "i", in.path, rbOut}, 180000);
            if (r.started && !r.timedOut && r.exitCode == 0 && md::fileSize(rbOut) > 0) {
                if (outCoder == "png") {
                    if (!copyFile6(rbOut, out)) return fail6("تعذّر كتابة ملف الخرج");
                    return M6({{"ok", B6(true)}, {"path", S6(outRaw)}, {"method", S6("rembg")}, {"bytes", N6(static_cast<double>(md::fileSize(out)))}});
                }
                // webp: حوّل ناتج rembg عبر ImageMagick إن وُجد
                std::string imw = imTool();
                if (!imw.empty()) {
                    std::string tmpOut = tmp.file("final." + oext);
                    md::ProcResult c = md::run(imRun(imw, {"png:" + rbOut, outCoder + ":" + tmpOut}), 60000);
                    if (c.started && c.exitCode == 0 && md::fileSize(tmpOut) > 0 && copyFile6(tmpOut, out))
                        return M6({{"ok", B6(true)}, {"path", S6(outRaw)}, {"method", S6("rembg")}, {"bytes", N6(static_cast<double>(md::fileSize(out)))}});
                }
            }
            if (method == "ai") return fail6("فشل rembg: " + (r.started ? firstLine(r.err) : r.error));
        } else if (method == "ai") {
            return fail6("method=ai يحتاج rembg — ثبّته: pip install rembg[cli]");
        }
    }

    // 2) الملء الفيضي بـ ImageMagick
    std::string im = imTool();
    if (im.empty())
        return fail6("يحتاج ImageMagick — ثبّته: sudo apt install imagemagick (أو ثبّت rembg للذكاء الاصطناعي)");
    int w = 0, h = 0;
    if (!imageSize(im, in, w, h)) return fail6("تعذّر قراءة أبعاد الصورة (ملف تالف؟)");
    if (static_cast<long long>(w) * h > 120000000LL) return fail6("الصورة كبيرة جداً (> 120 ميغابكسل)");

    std::string frame = (in.coder == "gif" || in.coder == "tiff" || in.coder == "webp") ? "[0]" : "";
    std::string inSpec = in.coder + ":" + in.path + frame;

    std::string bg = colorOpt;
    if (bg.empty()) {
        // أكثر الألوان تكراراً بين الزوايا الأربع (التعادل -> الزاوية العليا اليسرى)
        std::string fmt = "%[pixel:p{0,0}]|%[pixel:p{" + std::to_string(w - 1) + ",0}]|%[pixel:p{0," + std::to_string(h - 1) +
                          "}]|%[pixel:p{" + std::to_string(w - 1) + "," + std::to_string(h - 1) + "}]";
        md::ProcResult r = md::run(imRun(im, {inSpec, "-format", fmt, "info:"}), 30000);
        if (!r.started || r.exitCode != 0) return fail6("تعذّر اكتشاف لون الخلفية: " + firstLine(r.err));
        std::vector<std::string> corners;
        std::string cur;
        for (char c : trimStr6(r.out)) { if (c == '|') { corners.push_back(cur); cur.clear(); } else cur += c; }
        corners.push_back(cur);
        size_t bestN = 0;
        for (auto& c : corners) {
            size_t n = static_cast<size_t>(std::count(corners.begin(), corners.end(), c));
            if (n > bestN) { bestN = n; bg = c; }
        }
        if (!colorTokenSafe(bg)) return fail6("لون الخلفية المكتشف غير مفهوم: " + bg);
        // الزاوية الغالبة شفافة أصلاً -> لا شيء لإزالته
        std::string lc = md::lower(bg);
        bool transparent = lc == "none" || lc == "transparent";
        if (!transparent && lc.rfind("srgba(", 0) == 0) {
            size_t comma = lc.find_last_of(','), close = lc.find_last_of(')');
            if (comma != std::string::npos && close != std::string::npos && close > comma)
                transparent = std::atof(lc.substr(comma + 1, close - comma - 1).c_str()) == 0.0;
        }
        if (transparent && !opaqueOut) {
            std::string tmpOut = tmp.file("same." + oext);
            md::ProcResult c = md::run(imRun(im, {inSpec, outCoder + ":" + tmpOut}), 60000);
            if (!c.started || c.exitCode != 0 || !copyFile6(tmpOut, out)) return fail6("تعذّر كتابة ملف الخرج");
            return M6({{"ok", B6(true)}, {"path", S6(outRaw)}, {"method", S6("color")}, {"alreadyTransparent", B6(true)},
                       {"bytes", N6(static_cast<double>(md::fileSize(out)))}});
        }
    } else {
        bg = md::safeColor(colorOpt);
    }

    Argv args{inSpec, "-alpha", "set", "-bordercolor", bg, "-border", "1", "-fuzz", numStr(tol) + "%",
              "-fill", "none", "-floodfill", "+0+0", bg, "-shave", "1x1"};
    if (erode > 0) { args.insert(args.end(), {"-channel", "A", "-morphology", "Erode", "Diamond:" + std::to_string(erode), "+channel"}); }
    if (feather > 0) { args.insert(args.end(), {"-channel", "A", "-blur", "0x" + numStr(feather), "-level", "50%,100%", "+channel"}); }
    if (trim) { args.push_back("-trim"); args.push_back("+repage"); }
    if (opaqueOut) { args.insert(args.end(), {"-background", md::safeColor(bgFill), "-alpha", "remove", "-alpha", "off"}); }
    std::string tmpOut = tmp.file("result." + oext);
    args.push_back(outCoder + ":" + tmpOut);
    md::ProcResult r = md::run(imRun(im, args), 120000);
    if (!r.started) return fail6(r.error);
    if (r.timedOut) return fail6("انتهت مهلة المعالجة");
    if (r.exitCode != 0 || md::fileSize(tmpOut) <= 0) return fail6("فشلت إزالة الخلفية: " + firstLine(r.err));
    if (!copyFile6(tmpOut, out)) return fail6("تعذّر كتابة ملف الخرج");

    // نسبة ما أُزيل: 100 × (1 − متوسط العتامة)
    double removed = -1;
    if (!opaqueOut) {
        md::ProcResult s = md::run(imRun(im, {outCoder + ":" + tmpOut, "-alpha", "extract", "-format", "%[fx:mean]", "info:"}), 30000);
        if (s.started && s.exitCode == 0) removed = std::floor((1.0 - std::atof(s.out.c_str())) * 10000 + 0.5) / 100.0;
    }
    // أبعاد الناتج الفعلية (تختلف عن المصدر عند trim)
    int ow = w, oh = h;
    {
        md::ProcResult d = md::run(imIdentify(im, {"-format", "%w %h", outCoder + ":" + tmpOut}), 20000);
        int tw = 0, th = 0;
        if (d.started && d.exitCode == 0 && std::sscanf(d.out.c_str(), "%d %d", &tw, &th) == 2 && tw > 0 && th > 0) { ow = tw; oh = th; }
    }
    std::vector<std::pair<std::string, Value>> kv{
        {"ok", B6(true)}, {"path", S6(outRaw)}, {"method", S6("color")}, {"background", S6(bg)},
        {"tolerance", N6(tol)}, {"bytes", N6(static_cast<double>(md::fileSize(out)))}, {"width", N6(ow)}, {"height", N6(oh)},
        {"sourceWidth", N6(w)}, {"sourceHeight", N6(h)}};
    if (removed >= 0) kv.push_back({"removedPercent", N6(removed)});
    return M6(std::move(kv));
}

// ------------------------------------------------------------------ make.image.text  (OCR)
static Value ocrImpl(const Resolver& resolve, Args& a, int line, const char* fn) {
    argc6(fn, a, 1, 2, line);
    std::string srcRaw = needStr(a[0], fn, "المصدر", line);
    Value opts = a.size() > 1 ? a[1] : Value::nil();
    needOpts(opts, fn, line);
    TempDir srcTmp;                       // رابط بدل مسار: يُنزَّل لملف مؤقت بنفس نواة make.api.download
    std::string src;
    if (md::isHttpUrl(srcRaw)) {
        if (!md::safeArg(srcRaw)) return fail6("رابط غير صالح — المسموح http:// أو https:// فقط");
        if (!srcTmp.ok()) return fail6(std::string(fn) + ": تعذّر إنشاء مجلد مؤقت لتنزيل الصورة");
        std::string derr;
        src = fetchToTemp(srcRaw, srcTmp.file("src.img"), derr);
        if (src.empty()) return fail6(std::string(fn) + ": " + derr);
    } else src = resolve(srcRaw, line);

    int psm = static_cast<int>(clampD(optN(opts, "psm", 3), 0, 13));
    double minConf = clampD(optN(opts, "minConfidence", 0), 0, 100);
    bool wantWords = optB(opts, "words", false);
    bool enhance = optB(opts, "enhance", false);
    int timeoutMs = static_cast<int>(clampD(optN(opts, "timeout", 120000), 1000, 600000));

    Value bridged;
    if (tryBridge("image.ocr", bridgeKV(opts, {{"src", src}}), bridged)) return bridged;

    if (md::which("tesseract").empty())
        return fail6(std::string(fn) + " يحتاج tesseract — ثبّته: sudo apt install tesseract-ocr tesseract-ocr-ara");

    // اللغة: المطلوبة صراحة، وإلا ara+eng المتاحتان فعلاً (أو eng)
    std::string lang = optS(opts, "lang");
    if (lang.empty()) {
        std::string pick;
        for (const char* l : {"ara", "eng"}) if (langAvailable(l)) pick += (pick.empty() ? "" : "+") + std::string(l);
        lang = pick.empty() ? "eng" : pick;
    } else {
        size_t i = 0;
        while (i <= lang.size()) {
            size_t plus = lang.find('+', i);
            std::string one = lang.substr(i, plus == std::string::npos ? std::string::npos : plus - i);
            bool safe = !one.empty() && one.size() <= 24;
            for (unsigned char c : one) if (!(std::isalnum(c) || c == '_')) safe = false;
            if (!safe) return fail6("اسم لغة غير صالح: '" + one + "'");
            if (!langAvailable(one)) {
                std::string have;
                for (auto& l : tesseractLangs()) have += (have.empty() ? "" : ", ") + l;
                return fail6("حزمة اللغة '" + one + "' غير مثبّتة (المتاح: " + have + ") — ثبّتها: sudo apt install tesseract-ocr-" + one);
            }
            if (plus == std::string::npos) break;
            i = plus + 1;
        }
    }

    TempDir tmp;
    StagedImage in = stageImage(src, tmp, "in");
    if (!in.ok) return fail6(in.error);

    std::string input = in.path;
    bool enhanced = false;
    if (enhance) {
        std::string im = imTool();
        if (!im.empty()) {
            int w = 0, h = 0;
            bool sized = imageSize(im, in, w, h);
            std::string enh = tmp.file("enh.png");
            Argv args{in.coder + ":" + in.path + ((in.coder == "gif" || in.coder == "tiff" || in.coder == "webp") ? "[0]" : ""),
                      "-colorspace", "Gray"};
            if (sized && w < 1500) { args.push_back("-resize"); args.push_back("200%"); }
            args.insert(args.end(), {"-normalize", "-sharpen", "0x1", "png:" + enh});
            md::ProcResult e = md::run(imRun(im, args), 60000);
            if (e.started && e.exitCode == 0 && md::fileSize(enh) > 0) { input = enh; enhanced = true; }
        }
    }

    md::ProcResult r = md::run({"tesseract", input, "stdout", "-l", lang, "--psm", std::to_string(psm), "tsv"}, timeoutMs);
    if (!r.started) return fail6(r.error);
    if (r.timedOut) return fail6("انتهت مهلة قراءة النص");
    if (r.exitCode != 0) return fail6("فشلت قراءة النص: " + firstLine(r.err));

    // TSV: level page block par line word left top width height conf text
    std::string text;
    std::vector<Value> lines, words;
    std::string curLine;
    long lastBlock = -1, lastPar = -1, lastLn = -1;
    double confSum = 0;
    int wordCount = 0;
    auto flushLine = [&]() { if (!curLine.empty()) { lines.push_back(S6(curLine)); text += curLine; curLine.clear(); } };
    for (auto& row : splitLines(r.out)) {
        auto f = splitTab(row);
        if (f.size() < 12 || f[0] != "5") continue;
        double conf = std::atof(f[10].c_str());
        std::string w = trimStr6(f[11]);
        if (w.empty() || conf < 0 || conf < minConf) continue;
        long blk = std::atol(f[2].c_str()), par = std::atol(f[3].c_str()), ln = std::atol(f[4].c_str());
        if (blk != lastBlock || par != lastPar || ln != lastLn) {
            bool newPar = (lastBlock != -1) && (blk != lastBlock || par != lastPar);
            flushLine();
            if (lastBlock != -1) text += newPar ? "\n\n" : "\n";
            lastBlock = blk; lastPar = par; lastLn = ln;
        }
        if (!curLine.empty()) curLine += " ";
        curLine += w;
        confSum += conf; ++wordCount;
        if (wantWords)
            words.push_back(M6({{"text", S6(w)}, {"confidence", N6(conf)}, {"x", N6(std::atof(f[6].c_str()))}, {"y", N6(std::atof(f[7].c_str()))},
                                {"width", N6(std::atof(f[8].c_str()))}, {"height", N6(std::atof(f[9].c_str()))}}));
    }
    flushLine();
    double avg = wordCount ? std::floor(confSum / wordCount * 100 + 0.5) / 100.0 : 0.0;
    std::vector<std::pair<std::string, Value>> kv{
        {"ok", B6(true)}, {"text", S6(text)}, {"confidence", N6(avg)}, {"wordCount", N6(wordCount)},
        {"lines", A6(std::move(lines))}, {"lang", S6(lang)}, {"psm", N6(psm)}, {"enhanced", B6(enhanced)}};
    if (wantWords) kv.push_back({"words", A6(std::move(words))});
    if (wordCount == 0) kv.push_back({"warning", S6("لم يُعثر على نص — جرّب enhance: true أو psm مختلفاً (6 لكتلة نص، 7 لسطر واحد)")});
    return M6(std::move(kv));
}

// ------------------------------------------------------------------ make.video.thumbnail
static Value thumbnailImpl(const Resolver& resolve, const EnsureDir& ensureDir, Args& a, int line) {
    argc6("make.video.thumbnail", a, 2, 3, line);
    std::string srcRaw = needStr(a[0], "make.video.thumbnail", "المصدر", line);
    std::string outRaw = needStr(a[1], "make.video.thumbnail", "مسار الخرج", line);
    Value opts = a.size() > 2 ? a[2] : Value::nil();
    needOpts(opts, "make.video.thumbnail", line);
    std::string out = resolve(outRaw, line);
    std::string oext = extOf(out);
    if (oext != "png" && oext != "jpg" && oext != "jpeg" && oext != "webp") return fail6("ملف الخرج يجب أن يكون .png أو .jpg أو .webp");
    Source src = resolveSource(srcRaw, resolve, line);
    if (!src.ok) return fail6(src.error);
    if (md::which("ffmpeg").empty()) return fail6("make.video.thumbnail يحتاج ffmpeg — ثبّته: sudo apt install ffmpeg");

    double at = std::max(0.0, optN(opts, "at", 1));
    int width = static_cast<int>(clampD(optN(opts, "width", 0), 0, 8192));
    TempDir tmp;
    if (!tmp.ok()) return fail6("تعذّر إنشاء مجلد مؤقت");
    std::string tmpOut = tmp.file("thumb." + oext);
    double used = at;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::remove(tmpOut.c_str());
        Argv av{"ffmpeg", "-v", "error", "-y"};
        if (src.isUrl) { auto g = protoGuard(); av.insert(av.end(), g.begin(), g.end()); }
        av.insert(av.end(), {"-ss", numStr(used), "-i", src.arg, "-frames:v", "1"});
        if (width > 0) { av.push_back("-vf"); av.push_back("scale=" + std::to_string(width) + ":-2"); }
        av.push_back("-update"); av.push_back("1");
        av.push_back(tmpOut);
        md::ProcResult r = md::run(av, 90000);
        if (!r.started) return fail6(r.error);
        if (r.timedOut) return fail6("انتهت مهلة استخراج الإطار");
        if (r.exitCode == 0 && md::fileSize(tmpOut) > 0) break;
        if (attempt == 0 && used > 0) { used = 0; continue; }   // الوقت بعد نهاية الفيديو -> أول إطار
        return fail6("تعذّر استخراج إطار: " + firstLine(r.err));
    }
    ensureDir(out);
    if (!copyFile6(tmpOut, out)) return fail6("تعذّر كتابة ملف الخرج");
    return M6({{"ok", B6(true)}, {"path", S6(outRaw)}, {"at", N6(used)}, {"bytes", N6(static_cast<double>(md::fileSize(out)))}});
}

// ------------------------------------------------------------------ make.api : أدوات
static std::string urlEncode6(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
static std::string b64Encode(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
        o += t[(n >> 18) & 63]; o += t[(n >> 12) & 63]; o += t[(n >> 6) & 63]; o += t[n & 63];
    }
    if (i + 1 == in.size()) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16;
        o += t[(n >> 18) & 63]; o += t[(n >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        o += t[(n >> 18) & 63]; o += t[(n >> 12) & 63]; o += t[(n >> 6) & 63]; o += '=';
    }
    return o;
}
static bool headerSafe(const std::string& k, const std::string& v) {
    if (k.empty() || k.find_first_of("\r\n:") != std::string::npos || v.find_first_of("\r\n") != std::string::npos) return false;
    return true;
}
// expect: غائبة -> 200..299 | رقم | مصفوفة أرقام | "2xx"/"3xx"/"any"
static bool statusMatches(const Value* expect, long st) {
    if (!expect || expect->type == Value::Type::NIL) return st >= 200 && st < 300;
    if (expect->type == Value::Type::NUMBER) return st == static_cast<long>(expect->number);
    if (expect->type == Value::Type::ARRAY && expect->array) {
        for (auto& v : *expect->array) if (v.type == Value::Type::NUMBER && st == static_cast<long>(v.number)) return true;
        return false;
    }
    if (expect->type == Value::Type::STRING) {
        std::string s = md::lower(expect->str);
        if (s == "any") return st > 0;
        if (s.size() == 3 && s[1] == 'x' && s[2] == 'x' && std::isdigit(static_cast<unsigned char>(s[0]))) return st / 100 == s[0] - '0';
    }
    return st >= 200 && st < 300;
}
struct ApiCacheEntry { double at = 0; Value value; };
static std::unordered_map<std::string, ApiCacheEntry>& apiCache() { static std::unordered_map<std::string, ApiCacheEntry> c; return c; }
static double nowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static double nowMs6() { return nowSec() * 1000.0; }

// ============================================================================
//  تنزيل الروابط (make.api.download) — الصور/الفيديو/الصوت/المستندات وأي ملف
// ----------------------------------------------------------------------------
//  دالة واحدة موجودة أصلاً (make.api.download) بنفس توقيعها (url, path[, opts])، وكل ما تغيّر أنها
//  صارت تفهم أنواع الروابط الشائعة بدل حفظ أي رد كما هو:
//    * ملف مباشر (صورة/فيديو/صوت/PDF/...) : يُكتب مباشرة إلى القرص بالتدفق (لا ذاكرة) + استئناف.
//    * path ينتهي بـ "/" أو مجلد موجود    : الاسم من Content-Disposition أو الرابط، والامتداد من
//                                          البايتات (لا من الامتداد المزيَّف: image.php → .jpg).
//    * قائمة بث HLS (.m3u8)               : تُجمَّع إلى ملف فيديو واحد عبر ffmpeg (موجود أصلاً في الجرد).
//    * صفحة فيديو (يوتيوب وغيره)          : عبر yt-dlp إن وُجد؛ وإلا خطأ واضح بدل حفظ HTML كأنه فيديو.
//  لا مفاهيم جديدة: خيارات إضافية اختيارية فقط (resume, quality) والنتيجة خريطة ok/error كبقية make.*
// ============================================================================
struct Sniff { std::string kind, ext, mime; };

static Sniff sniffFile(const std::string& path) {
    Sniff s{"file", "", ""};
    std::ifstream f(path, std::ios::binary);
    unsigned char h[1024];
    size_t n = 0;
    if (f) { f.read(reinterpret_cast<char*>(h), sizeof h); n = static_cast<size_t>(f.gcount()); }
    if (n == 0) { s.kind = "empty"; return s; }
    auto is = [&](size_t off, const char* m) { size_t L = std::strlen(m); return n >= off + L && std::memcmp(h + off, m, L) == 0; };
    auto set = [&](const char* k, const char* e, const char* m) { s.kind = k; s.ext = e; s.mime = m; return s; };

    // صور (نفس تعريف md::sniffImage + المزيد)
    if (n >= 4 && h[0] == 0x89 && is(1, "PNG")) return set("image", "png", "image/png");
    if (n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return set("image", "jpg", "image/jpeg");
    if (is(0, "RIFF") && is(8, "WEBP")) return set("image", "webp", "image/webp");
    if (is(0, "GIF8")) return set("image", "gif", "image/gif");
    if (n >= 2 && h[0] == 'B' && h[1] == 'M' && n > 14) return set("image", "bmp", "image/bmp");
    if (n >= 4 && ((h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) || (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42)))
        return set("image", "tiff", "image/tiff");
    if (n >= 4 && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0) return set("image", "ico", "image/x-icon");
    // حاوية ISO-BMFF (mp4/mov/m4a/heic/avif)
    if (n >= 12 && is(4, "ftyp")) {
        std::string b(reinterpret_cast<char*>(h) + 8, 4);
        if (b == "M4A " || b == "M4B ") return set("audio", "m4a", "audio/mp4");
        if (b == "qt  ") return set("video", "mov", "video/quicktime");
        if (b == "avif" || b == "avis") return set("image", "avif", "image/avif");
        if (b == "heic" || b == "heix" || b == "hevc" || b == "hevx" || b == "mif1" || b == "msf1") return set("image", "heic", "image/heic");
        if (b.compare(0, 3, "3gp") == 0) return set("video", "3gp", "video/3gpp");
        return set("video", "mp4", "video/mp4");
    }
    if (n >= 4 && h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) {
        std::string head(reinterpret_cast<char*>(h), std::min<size_t>(n, 64));
        return head.find("webm") != std::string::npos ? set("video", "webm", "video/webm") : set("video", "mkv", "video/x-matroska");
    }
    if (is(0, "RIFF") && is(8, "AVI ")) return set("video", "avi", "video/x-msvideo");
    if (is(0, "RIFF") && is(8, "WAVE")) return set("audio", "wav", "audio/wav");
    if (is(0, "FLV") && n >= 4 && h[3] == 1) return set("video", "flv", "video/x-flv");
    if (is(0, "OggS")) return set("audio", "ogg", "audio/ogg");
    if (is(0, "fLaC")) return set("audio", "flac", "audio/flac");
    if (is(0, "#!AMR")) return set("audio", "amr", "audio/amr");
    if (is(0, "ID3") || (n >= 2 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && ((h[1] >> 1) & 3) != 0 && ((h[1] >> 3) & 3) != 1))
        return set("audio", "mp3", "audio/mpeg");
    if (n >= 377 && h[0] == 0x47 && h[188] == 0x47 && h[376] == 0x47) return set("video", "ts", "video/mp2t");
    // مستندات وأرشيفات
    if (is(0, "%PDF-")) return set("document", "pdf", "application/pdf");
    if (is(0, "PK\x03\x04")) return set("archive", "zip", "application/zip");
    if (n >= 2 && h[0] == 0x1F && h[1] == 0x8B) return set("archive", "gz", "application/gzip");
    if (is(0, "7z\xBC\xAF\x27\x1C")) return set("archive", "7z", "application/x-7z-compressed");
    if (is(0, "Rar!\x1A\x07")) return set("archive", "rar", "application/vnd.rar");
    if (is(0, "BZh")) return set("archive", "bz2", "application/x-bzip2");
    if (is(0, "\xFD" "7zXZ")) return set("archive", "xz", "application/x-xz");
    // نصوص: قائمة بث / صفحة ويب / SVG / نص عادي
    size_t i0 = 0;
    if (n >= 3 && h[0] == 0xEF && h[1] == 0xBB && h[2] == 0xBF) i0 = 3;
    while (i0 < n && std::isspace(h[i0])) ++i0;
    std::string head(reinterpret_cast<char*>(h) + i0, n - i0);
    std::string lh = md::lower(head);
    if (lh.compare(0, 7, "#extm3u") == 0) return set("playlist", "m3u8", "application/vnd.apple.mpegurl");
    if (lh.find("<svg") != std::string::npos && (lh.compare(0, 4, "<svg") == 0 || lh.compare(0, 5, "<?xml") == 0 || lh.compare(0, 9, "<!doctype") == 0))
        return set("image", "svg", "image/svg+xml");
    if (lh.compare(0, 9, "<!doctype") == 0 || lh.compare(0, 5, "<html") == 0 || lh.find("<html") != std::string::npos || lh.find("<head") != std::string::npos)
        return set("page", "html", "text/html");
    bool text = true;
    for (size_t i = 0; i < n; ++i) { unsigned char c = h[i]; if (c == 0 || (c < 9) || (c > 13 && c < 32 && c != 27)) { text = false; break; } }
    if (text) {
        if (!lh.empty() && (lh[0] == '{' || lh[0] == '[')) return set("text", "json", "application/json");
        return set("text", "txt", "text/plain");
    }
    s.ext = "bin"; s.mime = "application/octet-stream";
    return s;
}

static std::string extFromMime(const std::string& mimeIn) {
    static const std::unordered_map<std::string, std::string> m = {
        {"image/jpeg", "jpg"}, {"image/png", "png"}, {"image/gif", "gif"}, {"image/webp", "webp"}, {"image/svg+xml", "svg"},
        {"image/bmp", "bmp"}, {"image/avif", "avif"}, {"image/heic", "heic"}, {"image/x-icon", "ico"}, {"image/tiff", "tiff"},
        {"video/mp4", "mp4"}, {"video/webm", "webm"}, {"video/x-matroska", "mkv"}, {"video/quicktime", "mov"}, {"video/mp2t", "ts"},
        {"video/x-msvideo", "avi"}, {"video/3gpp", "3gp"}, {"audio/mpeg", "mp3"}, {"audio/mp4", "m4a"}, {"audio/aac", "aac"},
        {"audio/ogg", "ogg"}, {"audio/wav", "wav"}, {"audio/x-wav", "wav"}, {"audio/flac", "flac"}, {"audio/opus", "opus"},
        {"application/pdf", "pdf"}, {"application/zip", "zip"}, {"application/gzip", "gz"}, {"application/json", "json"},
        {"application/vnd.apple.mpegurl", "m3u8"}, {"application/x-mpegurl", "m3u8"}, {"text/plain", "txt"}, {"text/csv", "csv"},
        {"text/html", "html"}, {"text/css", "css"}, {"application/javascript", "js"}, {"text/javascript", "js"}, {"application/xml", "xml"}};
    auto it = m.find(md::lower(mimeIn));
    return it == m.end() ? std::string() : it->second;
}

static bool isMediaExt(const std::string& e) {
    static const char* k[] = {"jpg", "jpeg", "png", "gif", "webp", "bmp", "tif", "tiff", "ico", "svg", "avif", "heic",
                              "mp4", "m4v", "mkv", "webm", "mov", "avi", "flv", "ts", "3gp", "wmv",
                              "mp3", "m4a", "aac", "ogg", "opus", "wav", "flac", "amr",
                              "pdf", "zip", "gz", "7z", "rar", "xz", "bz2"};
    for (const char* x : k) if (e == x) return true;
    return false;
}
static bool isVideoExt(const std::string& e) { return e == "mp4" || e == "m4v" || e == "mkv" || e == "webm" || e == "mov" || e == "ts"; }
static bool isAudioExt(const std::string& e) { return e == "mp3" || e == "m4a" || e == "aac" || e == "ogg" || e == "opus" || e == "wav" || e == "flac"; }

static std::string pctDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else o += s[i];
    }
    return o;
}

// اسم ملف آمن: بلا مسار ولا محارف تحكّم ولا محارف ممنوعة على الأنظمة، وبطول معقول (بلا قطع UTF-8 من المنتصف).
static std::string sanitizeName(std::string n) {
    size_t sl = n.find_last_of("/\\");
    if (sl != std::string::npos) n = n.substr(sl + 1);
    std::string o;
    for (unsigned char c : n) {
        if (c < 32 || c == 127) continue;
        if (std::strchr("<>:\"|?*", c)) { o += '_'; continue; }
        o += static_cast<char>(c);
    }
    size_t a = 0, b = o.size();
    while (a < b && (o[a] == ' ' || o[a] == '.')) ++a;
    while (b > a && (o[b - 1] == ' ' || o[b - 1] == '.')) --b;
    o = o.substr(a, b - a);
    if (o.size() > 120) {
        size_t cut = 120;
        while (cut > 0 && (static_cast<unsigned char>(o[cut]) & 0xC0) == 0x80) --cut;
        std::string e = extOf(o);
        o = o.substr(0, cut);
        if (!e.empty() && e.size() <= 6 && o.find('.') == std::string::npos) o += "." + e;
    }
    return o.empty() ? std::string("download") : o;
}

// filename*=UTF-8''... أو filename="..." من Content-Disposition (بلا تحليل كامل لـ RFC 6266، يكفي الشائع).
static std::string nameFromDisposition(const std::string& cd) {
    if (cd.empty()) return "";
    std::string l = md::lower(cd);
    size_t p = l.find("filename*=");
    if (p != std::string::npos) {
        std::string v = cd.substr(p + 10);
        size_t semi = v.find(';'); if (semi != std::string::npos) v.erase(semi);
        size_t q = v.find("''");
        if (q != std::string::npos) v = v.substr(q + 2);
        return sanitizeName(pctDecode(trimStr6(v)));
    }
    p = l.find("filename=");
    if (p == std::string::npos) return "";
    std::string v = trimStr6(cd.substr(p + 9));
    if (!v.empty() && v[0] == '"') {
        size_t e = v.find('"', 1);
        v = v.substr(1, e == std::string::npos ? std::string::npos : e - 1);
    } else {
        size_t semi = v.find(';'); if (semi != std::string::npos) v.erase(semi);
        v = trimStr6(v);
    }
    return sanitizeName(v);
}

static std::string nameFromUrl(const std::string& url) {
    size_t s = url.find("://");
    std::string rest = s == std::string::npos ? url : url.substr(s + 3);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) return "";
    std::string path = rest.substr(slash);
    size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) path.erase(cut);
    while (!path.empty() && path.back() == '/') path.pop_back();
    size_t ls = path.find_last_of('/');
    std::string seg = ls == std::string::npos ? path : path.substr(ls + 1);
    return seg.empty() ? "" : sanitizeName(pctDecode(seg));
}

// يكمّل/يصحّح الامتداد من النوع الفعلي للبايتات: بلا امتداد، أو امتداد ديناميكي (php/asp/...) لا يدل على المحتوى.
static std::string fixExtension(std::string name, const std::string& wantExt) {
    if (wantExt.empty()) return name;
    static const char* dyn[] = {"php", "php3", "php4", "php5", "php7", "phtml", "asp", "aspx", "ashx", "axd", "jsp", "jspx", "cgi", "do",
                                "action", "bin", "dat", "tmp", "download", "image", "file", "img"};
    std::string e = extOf(name);
    bool need = e.empty() || e.size() > 6;
    for (const char* d : dyn) if (e == d) need = true;
    if (!need) return name;
    if (!e.empty() && e.size() <= 6) name = name.substr(0, name.size() - e.size() - 1);
    return name + "." + wantExt;
}

static std::string urlStem(const std::string& url) {
    std::string n = nameFromUrl(url);
    std::string st = n.empty() ? "" : stemOf(n);
    std::string l = md::lower(st);
    if (st.empty() || l == "index" || l == "master" || l == "playlist" || l == "manifest" || l == "stream" || l == "chunklist" || l == "prog_index")
        return "video";
    return st;
}

static bool pathIsDir(const std::string& p) {
#ifdef RIN_MEDIA_POSIX
    struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#else
    (void)p; return false;
#endif
}

static std::string uniqueName(const std::string& dir, const std::string& name) {
    if (!md::fileExists(dir + name)) return name;
    std::string st = stemOf(name), e = extOf(name);
    for (int i = 1; i < 1000; ++i) {
        std::string cand = st + " (" + std::to_string(i) + ")" + (e.empty() ? "" : "." + e);
        if (!md::fileExists(dir + cand)) return cand;
    }
    return name;
}

struct DlParams {
    std::string url;
    std::string outAbs;              // مسار الملف الكامل (وضع الملف) أو المجلد (وضع المجلد، ينتهي بـ "/")
    std::string outRaw;              // كما كتبه المستخدم (لبناء path في النتيجة)
    bool dirMode = false;
    bool overwrite = true;
    bool overwriteExplicit = false;
    bool resume = false;
    long long maxBytes = 2048LL * 1024 * 1024;
    bool maxBytesExplicit = false;
    int idleMs = 30000;
    int totalMs = 0;                 // 0 = السقف الافتراضي (6 ساعات)
    int quality = 0;                 // أقصى ارتفاع فيديو (yt-dlp)؛ 0 = الأفضل
    http::HeaderList headers;
    EnsureDir ensureDir;
};

struct DlOutcome {
    bool ok = false;
    std::string error;
    std::string path;                // المسار النهائي كما يُعاد للمستخدم
    std::string abs;                 // المسار الكامل على القرص
    long long bytes = 0;
    long status = 0;
    std::string contentType, kind, ext, via = "curl", finalUrl;
    bool resumed = false;
    std::vector<std::pair<std::string, Value>> extra;
};

static DlOutcome dlFail(const std::string& e, long status = 0) { DlOutcome o; o.error = e; o.status = status; return o; }

static std::string joinOutRaw(const std::string& raw, bool dirMode, const std::string& name) {
    if (!dirMode) return raw;
    return raw + (raw.empty() || raw.back() == '/' ? "" : "/") + name;
}

// تحرير الملف المؤقت وإعطاء الاسم النهائي (وضع المجلد: من الترويسة/الرابط/البايتات).
static DlOutcome dlFinalize(const DlParams& p, const std::string& part, const Sniff& sn, const std::string& cdHeader,
                            const std::string& mime, const std::string& finalUrl, long status, bool resumed, const std::string& via) {
    DlOutcome o;
    std::string wantExt = sn.ext;
    if ((wantExt.empty() || wantExt == "bin" || wantExt == "txt") && !mime.empty()) {
        std::string me = extFromMime(mime);
        if (!me.empty()) wantExt = me;
    }
    std::string name, finalAbs;
    if (p.dirMode) {
        name = nameFromDisposition(cdHeader);
        if (name.empty()) name = nameFromUrl(finalUrl.empty() ? p.url : finalUrl);
        if (name.empty()) name = nameFromUrl(p.url);
        if (name.empty()) name = "download";
        name = fixExtension(name, (sn.kind == "text" || sn.kind == "file") ? wantExt : sn.ext.empty() ? wantExt : sn.ext);
        if (p.overwriteExplicit) {
            if (!p.overwrite && md::fileExists(p.outAbs + name)) { std::remove(part.c_str()); return dlFail("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال"); }
        } else name = uniqueName(p.outAbs, name);
        finalAbs = p.outAbs + name;
    } else {
        finalAbs = p.outAbs;
        name = finalAbs.substr(finalAbs.find_last_of('/') == std::string::npos ? 0 : finalAbs.find_last_of('/') + 1);
    }
    if (std::rename(part.c_str(), finalAbs.c_str()) != 0) {
        if (!copyFile6(part, finalAbs)) { std::remove(part.c_str()); return dlFail("تعذّر حفظ الملف النهائي: " + name); }
        std::remove(part.c_str());
    }
    o.ok = true; o.abs = finalAbs; o.path = joinOutRaw(p.outRaw, p.dirMode, name);
    o.bytes = md::fileSize(finalAbs); o.status = status; o.contentType = mime; o.kind = sn.kind; o.ext = extOf(name);
    if (o.ext.empty()) o.ext = sn.ext;
    o.via = via; o.finalUrl = finalUrl.empty() ? p.url : finalUrl; o.resumed = resumed;
    if (o.kind == "empty") o.kind = "file";
    return o;
}

#ifdef RIN_MEDIA_POSIX
// ---- بث HLS (.m3u8) عبر ffmpeg ------------------------------------------------------------
static DlOutcome dlViaFfmpeg(const DlParams& p, const std::string& streamUrl) {
    if (md::which("ffmpeg").empty())
        return dlFail("رابط بث HLS (.m3u8) يحتاج ffmpeg — ثبّته: sudo apt install ffmpeg");
    std::string finalAbs, name;
    if (p.dirMode) {
        name = urlStem(streamUrl) + ".mp4";
        if (p.overwriteExplicit) { if (!p.overwrite && md::fileExists(p.outAbs + name)) return dlFail("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال"); }
        else name = uniqueName(p.outAbs, name);
        finalAbs = p.outAbs + name;
    } else {
        finalAbs = p.outAbs;
        std::string e = extOf(finalAbs);
        if (e.empty()) finalAbs += ".mp4";
        else if (!isVideoExt(e) && !isAudioExt(e)) return dlFail("ملف الخرج لبث HLS يجب أن يكون .mp4 أو .mkv أو .ts (وُجد: ." + e + ")");
        name = finalAbs.substr(finalAbs.find_last_of('/') == std::string::npos ? 0 : finalAbs.find_last_of('/') + 1);
    }
    std::string e = extOf(finalAbs);
    std::string fmt = e == "mkv" ? "matroska" : e == "ts" ? "mpegts" : e == "webm" ? "webm" : e == "mov" ? "mov"
                      : (e == "mp3") ? "mp3" : (e == "m4a") ? "ipod" : (e == "ogg" || e == "opus") ? "ogg" : (e == "flac") ? "flac" : (e == "wav") ? "wav" : (e == "aac") ? "adts" : "mp4";
    if (!p.overwrite && !p.dirMode && md::fileExists(finalAbs)) return dlFail("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال");
    p.ensureDir(finalAbs);
    std::string part = finalAbs + ".part";
    std::remove(part.c_str());

    md::Argv av = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y"};
    if (!p.headers.empty()) {
        std::string hs;
        for (auto& h : p.headers) hs += h.first + ": " + h.second + "\r\n";
        av.push_back("-headers"); av.push_back(hs);
    }
    av.push_back("-rw_timeout"); av.push_back(std::to_string(static_cast<long long>(p.idleMs) * 1000));
    // بلا "file": قائمة بعيدة لا يجوز أن تُحيل إلى ملفات محلية (ثغرة قراءة ملفات عبر HLS)
    av.push_back("-protocol_whitelist"); av.push_back("http,https,tcp,tls,crypto");
    av.push_back("-i"); av.push_back(streamUrl);
    if (!isAudioExt(e) || e == "m4a" || e == "aac") { av.push_back("-c"); av.push_back("copy"); }
    if (e == "mp4" || e == "mov" || e == "m4a") { av.push_back("-bsf:a"); av.push_back("aac_adtstoasc"); }
    if (p.maxBytes > 0) { av.push_back("-fs"); av.push_back(std::to_string(p.maxBytes)); }
    av.push_back("-f"); av.push_back(fmt);
    av.push_back(part);

    int total = p.totalMs > 0 ? p.totalMs : 21600000;
    md::ProcResult r = md::run(av, total, 1u << 20);
    if (!r.started) return dlFail(r.error);
    if (r.timedOut) { std::remove(part.c_str()); return dlFail("انتهت مهلة تجميع البث"); }
    long long sz = md::fileSize(part);
    if (r.exitCode != 0 || sz <= 0) {
        std::remove(part.c_str());
        std::string msg = firstLine(r.err);
        auto ls = splitLines(trimStr6(r.err));
        if (!ls.empty()) msg = ls.back();
        return dlFail("فشل ffmpeg في تنزيل البث: " + (msg.empty() ? "رمز الخروج " + std::to_string(r.exitCode) : msg));
    }
    if (p.maxBytes > 0 && sz >= p.maxBytes) { std::remove(part.c_str()); return dlFail("حجم الملف يتجاوز maxBytes"); }
    if (std::rename(part.c_str(), finalAbs.c_str()) != 0) {
        if (!copyFile6(part, finalAbs)) { std::remove(part.c_str()); return dlFail("تعذّر حفظ الملف النهائي"); }
        std::remove(part.c_str());
    }
    DlOutcome o;
    o.ok = true; o.abs = finalAbs; o.path = p.dirMode ? joinOutRaw(p.outRaw, true, name) : (p.outRaw + (extOf(p.outRaw).empty() ? ".mp4" : ""));
    o.bytes = sz; o.status = 200; o.kind = isAudioExt(e) ? "audio" : "video"; o.ext = e; o.via = "ffmpeg"; o.finalUrl = streamUrl;
    o.contentType = e == "mkv" ? "video/x-matroska" : e == "ts" ? "video/mp2t" : e == "webm" ? "video/webm" : isAudioExt(e) ? "audio/" + e : "video/mp4";
    return o;
}

// ---- صفحات الفيديو (يوتيوب وغيره) عبر yt-dlp ------------------------------------------------
static std::vector<std::string> listFiles(const std::string& dir) {
    std::vector<std::string> v;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            v.push_back(n);
        }
        closedir(d);
    }
    return v;
}

static DlOutcome dlViaYtDlp(const DlParams& p, const std::string& pageUrl) {
    if (md::which("yt-dlp").empty())
        return dlFail("الرابط صفحة ويب وليس ملفاً مباشراً. لتنزيل الفيديو من الصفحات (يوتيوب وغيره) ثبّت yt-dlp: pip install yt-dlp");
    std::string base = p.dirMode ? p.outAbs : p.outAbs.substr(0, p.outAbs.find_last_of('/') == std::string::npos ? 0 : p.outAbs.find_last_of('/') + 1);
    if (base.empty()) base = "./";
    p.ensureDir(base + "x");
    std::string tmpl = base + ".rin-yt-XXXXXX";
    std::vector<char> tb(tmpl.begin(), tmpl.end()); tb.push_back('\0');
    if (!mkdtemp(tb.data())) return dlFail("تعذّر إنشاء مجلد مؤقت للتنزيل");
    std::string tdir = tb.data();
    auto cleanup = [&]() {
        for (auto& f : listFiles(tdir)) std::remove((tdir + "/" + f).c_str());
        rmdir(tdir.c_str());
    };

    std::string want = p.dirMode ? "" : extOf(p.outAbs);
    md::Argv av = {"yt-dlp", "--ignore-config", "--no-playlist", "--quiet", "--no-warnings", "--no-progress",
                   "--socket-timeout", std::to_string(std::max(5, p.idleMs / 1000)), "-P", tdir, "-o", "%(title).120B.%(ext)s"};
    if (p.maxBytes > 0) { av.push_back("--max-filesize"); av.push_back(std::to_string(p.maxBytes)); }
    for (auto& h : p.headers) { av.push_back("--add-header"); av.push_back(h.first + ":" + h.second); }
    if (isAudioExt(want)) {
        av.push_back("-x"); av.push_back("--audio-format"); av.push_back(want);
    } else {
        std::string q = p.quality > 0 ? "[height<=" + std::to_string(p.quality) + "]" : "";
        av.push_back("-f"); av.push_back("bv*" + q + "+ba/b" + q + "/bv*+ba/b");
        if (want == "mp4" || want == "mkv" || want == "webm" || want == "mov") { av.push_back("--merge-output-format"); av.push_back(want); }
    }
    av.push_back("--"); av.push_back(pageUrl);

    md::ProcResult r = md::run(av, p.totalMs > 0 ? p.totalMs : 21600000, 1u << 20);
    if (!r.started) { cleanup(); return dlFail(r.error); }
    if (r.timedOut) { cleanup(); return dlFail("انتهت مهلة yt-dlp"); }
    std::string got; long long best = -1;
    for (auto& f : listFiles(tdir)) {
        std::string e = extOf(f);
        if (e == "part" || e == "ytdl" || e == "temp") continue;
        long long sz = md::fileSize(tdir + "/" + f);
        if (sz > best) { best = sz; got = f; }
    }
    if (r.exitCode != 0 || got.empty()) {
        cleanup();
        auto ls = splitLines(trimStr6(r.err));
        std::string msg = ls.empty() ? "" : ls.back();
        return dlFail("فشل yt-dlp: " + (msg.empty() ? "رمز الخروج " + std::to_string(r.exitCode) : msg));
    }
    std::string finalAbs, name;
    std::string gotExt = extOf(got);
    if (p.dirMode) {
        name = sanitizeName(got);
        if (p.overwriteExplicit) { if (!p.overwrite && md::fileExists(p.outAbs + name)) { cleanup(); return dlFail("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال"); } }
        else name = uniqueName(p.outAbs, name);
        finalAbs = p.outAbs + name;
    } else {
        finalAbs = p.outAbs;
        if (extOf(finalAbs).empty()) finalAbs += "." + gotExt;
        if (!p.overwrite && md::fileExists(finalAbs)) { cleanup(); return dlFail("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال"); }
        name = finalAbs.substr(finalAbs.find_last_of('/') == std::string::npos ? 0 : finalAbs.find_last_of('/') + 1);
    }
    std::string src = tdir + "/" + got;
    if (std::rename(src.c_str(), finalAbs.c_str()) != 0) {
        if (!copyFile6(src, finalAbs)) { cleanup(); return dlFail("تعذّر حفظ الملف النهائي"); }
    }
    cleanup();
    Sniff sn = sniffFile(finalAbs);
    DlOutcome o;
    o.ok = true; o.abs = finalAbs; o.bytes = md::fileSize(finalAbs); o.status = 200; o.via = "yt-dlp"; o.finalUrl = pageUrl;
    o.path = p.dirMode ? joinOutRaw(p.outRaw, true, name) : (p.outRaw + (extOf(p.outRaw).empty() ? "." + gotExt : ""));
    o.kind = sn.kind == "file" || sn.kind == "empty" ? (isAudioExt(extOf(name)) ? "audio" : "video") : sn.kind;
    o.ext = extOf(name); o.contentType = sn.mime;
    return o;
}
#endif // RIN_MEDIA_POSIX

// ---- النواة: مباشر ← (HLS | صفحة) ← ملف نهائي --------------------------------------------------
static DlOutcome downloadCore(const DlParams& p) {
    bool urlIsPlaylist = false;
    {   // .m3u8 في مسار الرابط (قبل أي شبكة)
        std::string path = p.url.substr(p.url.find("://") + 3);
        size_t cut = path.find_first_of("?#"); if (cut != std::string::npos) path.erase(cut);
        std::string l = md::lower(path);
        urlIsPlaylist = l.size() > 5 && l.compare(l.size() - 5, 5, ".m3u8") == 0;
    }
    if (p.dirMode) p.ensureDir(p.outAbs + "x");
#ifdef RIN_MEDIA_POSIX
    if (urlIsPlaylist) return dlViaFfmpeg(p, p.url);
#else
    if (urlIsPlaylist) return dlFail("تنزيل بث HLS غير مدعوم على هذه المنصة");
#endif

    std::string part;
    if (p.dirMode) {
        char hx[24]; std::snprintf(hx, sizeof hx, "%016zx", std::hash<std::string>{}(p.url));
        part = p.outAbs + ".rin-" + hx + ".part";
    } else {
        if (!p.overwrite && md::fileExists(p.outAbs)) return dlFail("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال");
        p.ensureDir(p.outAbs);
        part = p.outAbs + ".part";
    }
    if (!p.resume) std::remove(part.c_str());

    http::DownloadRequest rq;
    rq.url = p.url; rq.destPath = part; rq.headers = p.headers; rq.idleTimeoutMs = p.idleMs; rq.totalTimeoutMs = p.totalMs;
    rq.maxBytes = p.maxBytes; rq.resume = p.resume;
    bool haveUa = false;
    for (auto& h : rq.headers) if (md::lower(h.first) == "user-agent") haveUa = true;
    if (!haveUa) rq.headers.push_back({"User-Agent", "Mozilla/5.0 (compatible; Rin; +https://github.com/dlof-lib/rinlang)"});

    http::DownloadResult r = http::performDownload(rq);
    std::string cdHeader = r.contentDisposition, mime = r.contentType, finalUrl = r.finalUrl;
    long status = r.status; bool resumed = r.resumed; std::string via = "curl";

    if (!r.handled) {
        // منصة بلا تنزيل متدفق (أندرويد بجسر قديم): الجسم كاملاً في الذاكرة — محدود بـ maxBytes (256MB افتراضياً هنا)
        if (!p.headers.empty()) return dlFail("ترويسات/مصادقة التنزيل الثنائي غير مدعومة على أندرويد بعد");
        http::HttpResult hr = http::performBinaryGet(p.url, p.totalMs > 0 ? p.totalMs : 120000);
        if (!hr.ok) return dlFail(hr.error.empty() ? "فشل الاتصال" : hr.error, hr.status);
        if (hr.status < 200 || hr.status >= 300) return dlFail("الخادوم أعاد الحالة " + std::to_string(hr.status), hr.status);
        double cap = p.maxBytesExplicit ? static_cast<double>(p.maxBytes) : 256.0 * 1024 * 1024;
        if (static_cast<double>(hr.body.size()) > cap) return dlFail("حجم الملف يتجاوز maxBytes");
        std::ofstream f(part, std::ios::binary | std::ios::trunc);
        if (!f) return dlFail("تعذّر فتح ملف الحفظ للكتابة");
        f.write(hr.body.data(), static_cast<std::streamsize>(hr.body.size()));
        f.close();
        if (!f) { std::remove(part.c_str()); return dlFail("فشلت كتابة الملف"); }
        status = hr.status; via = "android";
        for (auto& h : hr.headers) if (md::lower(h.first) == "content-type") mime = md::lower(h.second.substr(0, h.second.find(';')));
    } else if (!r.ok) {
        std::string err = r.error;
        if (r.status >= 400) err = "الخادوم أعاد الحالة " + std::to_string(r.status);
        if (err.empty()) err = "فشل الاتصال";
        bool keep = p.resume && r.status < 400 && md::fileSize(part) > 0;   // انقطاع شبكة: أبقِ الجزء للاستئناف لاحقاً
        if (!keep) std::remove(part.c_str());
        DlOutcome o = dlFail(err, r.status);
        if (keep) o.extra.push_back({"partial", S6(part.substr(part.find_last_of('/') == std::string::npos ? 0 : part.find_last_of('/') + 1))});
        return o;
    }

    Sniff sn = sniffFile(part);
    if (sn.kind == "playlist") {          // قائمة بث بلا امتداد واضح في الرابط
        std::remove(part.c_str());
#ifdef RIN_MEDIA_POSIX
        return dlViaFfmpeg(p, finalUrl.empty() ? p.url : finalUrl);
#else
        return dlFail("تنزيل بث HLS غير مدعوم على هذه المنصة");
#endif
    }
    if (sn.kind == "page") {
        std::string e = p.dirMode ? "" : extOf(p.outAbs);
        bool wantsMedia = p.dirMode || e.empty() || isMediaExt(e);
        if (wantsMedia) {
            std::remove(part.c_str());
#ifdef RIN_MEDIA_POSIX
            return dlViaYtDlp(p, finalUrl.empty() ? p.url : finalUrl);
#else
            return dlFail("الرابط صفحة ويب وليس ملفاً مباشراً");
#endif
        }
    }
    if (sn.kind == "empty") { std::remove(part.c_str()); return dlFail("الخادوم أعاد ملفاً فارغاً"); }
    return dlFinalize(p, part, sn, cdHeader, mime, finalUrl, status, resumed, via);
}

// يحوّل نتيجة النواة إلى خريطة make.* القياسية.
static Value dlToValue(const DlOutcome& o) {
    if (!o.ok) {
        std::vector<std::pair<std::string, Value>> ex = o.extra;
        if (o.status > 0) ex.push_back({"status", N6(static_cast<double>(o.status))});
        return fail6(o.error, std::move(ex));
    }
    std::string name = o.path.substr(o.path.find_last_of('/') == std::string::npos ? 0 : o.path.find_last_of('/') + 1);
    return M6({{"ok", B6(true)}, {"path", S6(o.path)}, {"name", S6(name)}, {"bytes", N6(static_cast<double>(o.bytes))},
               {"status", N6(static_cast<double>(o.status))}, {"contentType", S6(o.contentType)}, {"kind", S6(o.kind)},
               {"ext", S6(o.ext)}, {"via", S6(o.via)}, {"resumed", B6(o.resumed)}, {"url", S6(o.finalUrl)}});
}

// ينزّل رابطاً إلى ملف مؤقت (لـ make.image.removeBg / make.image.text عند تمرير رابط بدل مسار).
static std::string fetchToTemp(const std::string& url, const std::string& tmpFile, std::string& error) {
    DlParams p;
    p.url = url; p.outAbs = tmpFile; p.outRaw = tmpFile; p.overwrite = true;
    p.maxBytes = 64LL * 1024 * 1024; p.maxBytesExplicit = true; p.idleMs = 30000; p.totalMs = 120000;
    p.ensureDir = [](const std::string&) {};
    DlOutcome o = downloadCore(p);
    if (!o.ok) { error = o.error; return ""; }
    if (o.kind != "image") { std::remove(o.abs.c_str()); error = "الرابط لا يشير إلى صورة (النوع الفعلي: " + o.kind + ")"; return ""; }
    return o.abs;
}

} // namespace extra6

// ============================================================================
//  التسجيل
// ============================================================================
void Interpreter::registerNativesExtra6() {
    using namespace extra6;

    Resolver resolve = [this](const std::string& p, int line) { return resolvePath(p, line); };
    EnsureDir ensureDir = [this](const std::string& full) { ensureParentDir(full); };

    // ---- جرد ما يعمل فعلاً هنا ----------------------------------------------------------------
    natives["make.media.tools"] = [](Args& a, int line) -> Value {
        argc6("make.media.tools", a, 0, 0, line);
        std::vector<std::pair<std::string, Value>> tools;
        for (const char* t : {"ffmpeg", "ffprobe", "ffplay", "mpv", "vlc", "magick", "convert", "tesseract", "rembg", "curl", "yt-dlp", "xdg-open"})
            tools.push_back({t, B6(!md::which(t).empty())});
        std::vector<std::string> vp, ap, langs = tesseractLangs();
        if (md::hasBridge()) {      // أندرويد: اللغات المتاحة تأتي من الجسر (ملفات tessdata) + "latin" من ML Kit
            md::BridgeResult br = md::bridgeSlot()("ocr.langs", {});
            if (br.handled && br.ok) {
                langs.clear();
                for (auto& f : br.fields) {
                    if (f.first != "langs") continue;
                    size_t i = 0;
                    while (i <= f.second.size()) {
                        size_t c = f.second.find(',', i);
                        std::string one = f.second.substr(i, c == std::string::npos ? std::string::npos : c - i);
                        if (!one.empty()) langs.push_back(one);
                        if (c == std::string::npos) break;
                        i = c + 1;
                    }
                }
                langs.push_back("latin");
            }
        }
        for (const char* t : {"mpv", "vlc", "ffplay", "xdg-open", "open"}) if (!md::which(t).empty()) vp.push_back(t);
        for (const char* t : {"mpv", "ffplay", "vlc", "afplay", "paplay", "aplay"}) if (!md::which(t).empty()) ap.push_back(t);
        bool br = md::hasBridge();
        std::string rb = br ? "bridge" : (!md::which("rembg").empty() ? "rembg" : (!imTool().empty() ? "imagemagick" : "none"));
        return M6({{"platform", S6(md::platformName())}, {"bridge", B6(br)}, {"tools", M6(std::move(tools))},
                   {"videoPlayers", strArray(vp)}, {"audioPlayers", strArray(ap)}, {"ocrLangs", strArray(langs)},
                   {"display", B6(hasDisplay())}, {"removeBg", S6(rb)},
                   {"ocr", B6(br || !md::which("tesseract").empty())}});
    };

    // ---- فيديو ---------------------------------------------------------------------------------
    natives["make.video"] = [resolve](Args& a, int line) -> Value {
        argc6("make.video", a, 1, 2, line);
        std::string raw = needStr(a[0], "make.video", "المصدر", line);
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        needOpts(opts, "make.video", line);
        Source src = resolveSource(raw, resolve, line);
        if (!src.ok) return fail6(src.error);
        Value bridged;
        if (tryBridge("video.open", bridgeKV(opts, {{"src", src.arg}}), bridged)) return bridged;
        PlayPlan plan = planVideo(optS(opts, "player"), src, opts);
        if (!plan.error.empty()) return fail6(plan.error);
        bool opener = plan.backend == "xdg-open" || plan.backend == "open";
        return launch(plan, src, opts, g_videoPids, true, !opener);
    };
    natives["make.video.info"] = [resolve](Args& a, int line) -> Value {
        argc6("make.video.info", a, 1, 1, line);
        Source src = resolveSource(needStr(a[0], "make.video.info", "المصدر", line), resolve, line);
        if (!src.ok) return fail6(src.error);
        return mediaInfo(src, "make.video.info", true);
    };
    natives["make.video.thumbnail"] = [resolve, ensureDir](Args& a, int line) -> Value { return thumbnailImpl(resolve, ensureDir, a, line); };
    natives["make.video.stop"] = [](Args& a, int line) -> Value { argc6("make.video.stop", a, 0, 0, line); return stopPids(g_videoPids); };

    // ---- صوت -----------------------------------------------------------------------------------
    natives["make.audio"] = [resolve](Args& a, int line) -> Value {
        argc6("make.audio", a, 1, 2, line);
        std::string raw = needStr(a[0], "make.audio", "المصدر", line);
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        needOpts(opts, "make.audio", line);
        Source src = resolveSource(raw, resolve, line);
        if (!src.ok) return fail6(src.error);
        Value bridged;
        if (tryBridge("audio.play", bridgeKV(opts, {{"src", src.arg}}), bridged)) return bridged;
        PlayPlan plan = planAudio(optS(opts, "player"), src, opts);
        if (!plan.error.empty()) return fail6(plan.error);
        return launch(plan, src, opts, g_audioPids, false, false);
    };
    natives["make.audio.info"] = [resolve](Args& a, int line) -> Value {
        argc6("make.audio.info", a, 1, 1, line);
        Source src = resolveSource(needStr(a[0], "make.audio.info", "المصدر", line), resolve, line);
        if (!src.ok) return fail6(src.error);
        return mediaInfo(src, "make.audio.info", false);
    };
    natives["make.audio.stop"] = [](Args& a, int line) -> Value {
        argc6("make.audio.stop", a, 0, 0, line);
        Value bridged;
        if (tryBridge("audio.stop", {}, bridged)) return bridged;
        return stopPids(g_audioPids);
    };

    // ---- صور -----------------------------------------------------------------------------------
    natives["make.image.removeBg"] = [resolve, ensureDir](Args& a, int line) -> Value { return removeBgImpl(resolve, ensureDir, a, line); };
    natives["make.image.text"] = [resolve](Args& a, int line) -> Value { return ocrImpl(resolve, a, line, "make.image.text"); };
    natives["make.ocr"] = [resolve](Args& a, int line) -> Value { return ocrImpl(resolve, a, line, "make.ocr"); };

    // ---- API -----------------------------------------------------------------------------------
    natives["make.api.clearCache"] = [](Args& a, int line) -> Value {
        argc6("make.api.clearCache", a, 0, 0, line);
        size_t n = apiCache().size();
        apiCache().clear();
        return M6({{"ok", B6(true)}, {"cleared", N6(static_cast<double>(n))}});
    };

    natives["make.api"] = [this](Args& a, int line) -> Value {
        argc6("make.api", a, 1, 2, line);
        std::string url = needStr(a[0], "make.api", "الرابط", line);
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        needOpts(opts, "make.api", line);
        if (!md::isHttpUrl(url) || !md::safeArg(url)) return fail6("رابط غير صالح — المسموح http:// أو https:// فقط", {{"url", S6(url)}});

        std::string method = md::lower(optS(opts, "method", "get"));
        for (auto& c : method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        static const char* okMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
        bool mOk = false;
        for (auto m : okMethods) if (method == m) mOk = true;
        if (!mOk) return fail6("فعل HTTP غير مدعوم: " + method);

        // ?query تلقائي (قاموس -> ترميز URL)
        if (const Value* q = opt6(opts, "query")) if (q->type == Value::Type::MAP && q->map) {
            std::string qs;
            for (auto& kv : *q->map) {
                if (kv.second.type == Value::Type::NIL) continue;
                if (!qs.empty()) qs += "&";
                qs += urlEncode6(kv.first.toDisplayString()) + "=" + urlEncode6(kv.second.toDisplayString());
            }
            if (!qs.empty()) url += (url.find('?') == std::string::npos ? "?" : "&") + qs;
        }

        // ترويسات
        MapData hdrs;
        bool hasAccept = false, hasAuth = false;
        if (const Value* h = opt6(opts, "headers")) if (h->type == Value::Type::MAP && h->map) {
            for (auto& kv : *h->map) {
                std::string k = kv.first.toDisplayString(), v = kv.second.toDisplayString();
                if (!headerSafe(k, v)) return fail6("ترويسة غير صالحة: '" + k + "'");
                std::string lk = md::lower(k);
                if (lk == "accept") hasAccept = true;
                if (lk == "authorization") hasAuth = true;
                hdrs.push_back({S6(k), S6(v)});
            }
        }
        if (const Value* au = opt6(opts, "auth")) {
            auto put = [&](const std::string& k, const std::string& v) -> bool {
                if (!headerSafe(k, v)) return false;
                hdrs.push_back({S6(k), S6(v)});
                return true;
            };
            bool good = true;
            if (au->type == Value::Type::STRING) good = put("Authorization", "Bearer " + au->str);
            else if (au->type == Value::Type::MAP) {
                if (optHas(*au, "bearer")) good = put("Authorization", "Bearer " + optS(*au, "bearer"));
                else if (optHas(*au, "basic")) good = put("Authorization", "Basic " + b64Encode(optS(*au, "basic")));
                else if (optHas(*au, "key")) good = put(optS(*au, "header", "X-API-Key"), optS(*au, "key"));
                else return fail6("auth: المفاتيح المدعومة bearer | basic | key(+header)");
            } else return fail6("auth يجب أن يكون نصاً (Bearer) أو قاموساً");
            if (!good) return fail6("auth يحوي محارف غير صالحة");
            hasAuth = true;
        }
        if (!hasAccept) hdrs.push_back({S6("Accept"), S6("application/json")});

        const Value* body = opt6(opts, "body");
        int timeout = static_cast<int>(clampD(optN(opts, "timeout", 15000), 100, 120000));
        int retries = static_cast<int>(clampD(optN(opts, "retries", method == "GET" ? 2 : 0), 0, 6));
        double backoff = clampD(optN(opts, "backoff", 400), 50, 10000);
        double cacheTtl = method == "GET" ? clampD(optN(opts, "cache", 0), 0, 86400) : 0;
        std::string path = optS(opts, "path");
        const Value* expect = opt6(opts, "expect");
        const Value* fallback = opt6(opts, "fallback");

        // كاش داخل الذاكرة (GET فقط)
        std::string cacheKey;
        if (cacheTtl > 0) {
            cacheKey = method + "\n" + url + "\n" + path;
            for (auto& h : hdrs) cacheKey += "\n" + h.first.str + "=" + h.second.str;
            auto it = apiCache().find(cacheKey);
            if (it != apiCache().end()) {
                if (nowSec() - it->second.at < cacheTtl) {
                    Value hit = it->second.value;
                    if (hit.type == Value::Type::MAP && hit.map) {
                        MapData copy = *hit.map;
                        bool set = false;
                        for (auto& kv : copy) if (kv.first.type == Value::Type::STRING && kv.first.str == "cached") { kv.second = B6(true); set = true; }
                        if (!set) copy.push_back({S6("cached"), B6(true)});
                        return Value::makeMap(std::make_shared<MapData>(std::move(copy)));
                    }
                }
                apiCache().erase(it);
            }
        }

        auto fetchIt = natives.find("net.fetch");
        if (fetchIt == natives.end()) return fail6("net.fetch غير متاحة في هذا البناء");
        auto jsonIt = natives.find("json.get");

        double t0 = nowMs6();
        Value last;
        int attempts = 0;
        bool good = false;
        for (int i = 0; i <= retries; ++i) {
            ++attempts;
            std::vector<std::pair<std::string, Value>> fo{{"method", S6(method)}, {"timeout", N6(timeout)}, {"retries", N6(0)},
                                                          {"headers", Value::makeMap(std::make_shared<MapData>(hdrs))}};
            if (body && body->type != Value::Type::NIL) fo.push_back({"body", *body});
            Args fa{S6(url), M6(std::move(fo))};
            last = fetchIt->second(fa, line);
            bool conn = optB(last, "ok", false);
            long st = static_cast<long>(optN(last, "status", 0));
            good = conn && statusMatches(expect, st);
            bool retryable = !conn || st == 429 || st >= 500;
            if (good || !retryable || i == retries) break;
            double wait = std::min(8000.0, backoff * std::pow(2.0, i));
            if (const Value* hv = opt6(last, "headers")) if (hv->type == Value::Type::MAP && hv->map)
                for (auto& kv : *hv->map)
                    if (md::lower(kv.first.toDisplayString()) == "retry-after") {
                        double secs = std::atof(kv.second.toDisplayString().c_str());
                        if (secs > 0) wait = std::min(10000.0, secs * 1000.0);
                    }
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long>(wait)));
        }

        long status = static_cast<long>(optN(last, "status", 0));
        std::string error = optS(last, "error");
        bool conn = optB(last, "ok", false);
        if (conn && !good) error = "الخادوم أعاد الحالة " + std::to_string(status) + (optS(last, "statusText").empty() ? "" : " " + optS(last, "statusText"));
        if (good) error.clear();

        Value data = Value::nil();
        const Value* js = opt6(last, "json");
        if (js) data = *js;
        if (!path.empty() && good && jsonIt != natives.end()) {
            Args ja{data, S6(path)};
            Value picked = jsonIt->second(ja, line);
            if (picked.type == Value::Type::NIL) {
                if (fallback) picked = *fallback;
                else { good = false; error = "المسار '" + path + "' غير موجود في الاستجابة"; }
            }
            data = picked;
        } else if (!good && fallback) data = *fallback;

        Value result = M6({{"ok", B6(good)}, {"status", N6(static_cast<double>(status))}, {"statusText", S6(optS(last, "statusText"))},
                           {"data", data}, {"json", js ? *js : Value::nil()}, {"body", S6(optS(last, "body"))},
                           {"headers", opt6(last, "headers") ? *opt6(last, "headers") : M6({})},
                           {"error", S6(error)}, {"attempts", N6(attempts)}, {"cached", B6(false)},
                           {"ms", N6(std::floor((nowMs6() - t0) * 100) / 100)}, {"url", S6(url)}});
        if (good && cacheTtl > 0) {
            if (apiCache().size() >= 200) apiCache().clear();
            apiCache()[cacheKey] = ApiCacheEntry{nowSec(), result};
        }
        return result;
    };

    natives["make.api.download"] = [resolve, ensureDir](Args& a, int line) -> Value {
        argc6("make.api.download", a, 2, 3, line);
        std::string url = needStr(a[0], "make.api.download", "الرابط", line);
        std::string outRaw = needStr(a[1], "make.api.download", "مسار الحفظ", line);
        Value opts = a.size() > 2 ? a[2] : Value::nil();
        needOpts(opts, "make.api.download", line);
        if (!md::isHttpUrl(url) || !md::safeArg(url)) return fail6("رابط غير صالح — المسموح http:// أو https:// فقط");
        if (outRaw.empty()) return fail6("مسار الحفظ فارغ");

        DlParams p;
        p.url = url; p.outRaw = outRaw; p.ensureDir = ensureDir;
        // مجلد: ينتهي بـ "/" أو مجلد موجود فعلاً → الاسم يُستنتج (Content-Disposition ← الرابط ← نوع الملف)
        bool slash = outRaw.back() == '/';
        std::string probe = slash ? (outRaw.size() > 1 ? outRaw.substr(0, outRaw.size() - 1) : std::string(".")) : outRaw;
        std::string abs = resolve(probe, line);
        p.dirMode = slash || pathIsDir(abs);
        if (p.dirMode) { if (!abs.empty() && abs.back() != '/') abs += '/'; if (!slash && outRaw.back() != '/') p.outRaw = outRaw + "/"; }
        p.outAbs = abs;
        if (!p.dirMode && (abs.empty() || abs.back() == '/')) return fail6("مسار الحفظ يجب أن يكون اسم ملف أو مجلداً ينتهي بـ /");

        p.overwriteExplicit = optHas(opts, "overwrite");
        p.overwrite = optB(opts, "overwrite", true);
        p.resume = optB(opts, "resume", false);
        p.maxBytesExplicit = optHas(opts, "maxBytes");
        p.maxBytes = static_cast<long long>(clampD(optN(opts, "maxBytes", 2048.0 * 1024 * 1024), 1, 16384.0 * 1024 * 1024));
        // timeout الصريح = سقف كلي (كما كان)؛ غيابه = مهلة جمود 30ث + سقف 6 ساعات (فيديو كبير لا يُقطع لأنه بطيء)
        if (optHas(opts, "timeout")) {
            p.totalMs = static_cast<int>(clampD(optN(opts, "timeout", 60000), 1000, 21600000));
            p.idleMs = std::min(p.totalMs, 30000);
        }
        p.quality = static_cast<int>(clampD(optN(opts, "quality", 0), 0, 4320));

        if (const Value* h = opt6(opts, "headers")) if (h->type == Value::Type::MAP && h->map)
            for (auto& kv : *h->map) {
                std::string k = kv.first.toDisplayString(), v = kv.second.toDisplayString();
                if (!headerSafe(k, v)) return fail6("ترويسة غير صالحة: '" + k + "'");
                p.headers.push_back({k, v});
            }
        if (const Value* au = opt6(opts, "auth")) {
            if (au->type == Value::Type::STRING) p.headers.push_back({"Authorization", "Bearer " + au->str});
            else if (au->type == Value::Type::MAP && optHas(*au, "bearer")) p.headers.push_back({"Authorization", "Bearer " + optS(*au, "bearer")});
            else if (au->type == Value::Type::MAP && optHas(*au, "basic")) p.headers.push_back({"Authorization", "Basic " + b64Encode(optS(*au, "basic"))});
            else if (au->type == Value::Type::MAP && optHas(*au, "key")) p.headers.push_back({optS(*au, "header", "X-API-Key"), optS(*au, "key")});
            else return fail6("auth: المفاتيح المدعومة bearer | basic | key(+header)");
        }
        return dlToValue(downloadCore(p));
    };
}

} // namespace rin
