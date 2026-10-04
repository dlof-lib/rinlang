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
#include <fstream>
#include <thread>
#include <unordered_map>

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

    if (md::isHttpUrl(srcRaw)) return fail6("make.image.removeBg: نزّل الصورة أولاً بـ make.api.download ثم مرّر المسار المحلي");
    std::string src = resolve(srcRaw, line);
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
    if (md::isHttpUrl(srcRaw)) return fail6(std::string(fn) + ": نزّل الصورة أولاً بـ make.api.download ثم مرّر المسار المحلي");
    std::string src = resolve(srcRaw, line);

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
        for (const char* t : {"ffmpeg", "ffprobe", "ffplay", "mpv", "vlc", "magick", "convert", "tesseract", "rembg", "curl", "xdg-open"})
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
        std::string out = resolve(outRaw, line);
        if (out.empty() || out.back() == '/') return fail6("مسار الحفظ يجب أن يكون اسم ملف");
        bool overwrite = optB(opts, "overwrite", true);
        if (!overwrite && md::fileExists(out)) return fail6("الملف موجود مسبقاً — مرّر overwrite: true للاستبدال", {{"path", S6(outRaw)}});
        int timeout = static_cast<int>(clampD(optN(opts, "timeout", 60000), 1000, 600000));
        double maxBytes = clampD(optN(opts, "maxBytes", 256.0 * 1024 * 1024), 1, 4096.0 * 1024 * 1024);

        http::HttpResult r;
        bool custom = optHas(opts, "headers") || optHas(opts, "auth");
#ifndef __ANDROID__
        custom = true;   // على سطح المكتب: performRequest دائماً (curl آمن للبايتات) لأنه يعيد ترويسات الرد (Content-Type)
#endif
        if (custom) {
#ifdef __ANDROID__
            return fail6("ترويسات/مصادقة التنزيل الثنائي غير مدعومة على أندرويد بعد");
#else
            http::HeaderList hl;
            if (const Value* h = opt6(opts, "headers")) if (h->type == Value::Type::MAP && h->map)
                for (auto& kv : *h->map) {
                    std::string k = kv.first.toDisplayString(), v = kv.second.toDisplayString();
                    if (!headerSafe(k, v)) return fail6("ترويسة غير صالحة: '" + k + "'");
                    hl.push_back({k, v});
                }
            if (const Value* au = opt6(opts, "auth")) {
                if (au->type == Value::Type::STRING) hl.push_back({"Authorization", "Bearer " + au->str});
                else if (au->type == Value::Type::MAP && optHas(*au, "bearer")) hl.push_back({"Authorization", "Bearer " + optS(*au, "bearer")});
                else if (au->type == Value::Type::MAP && optHas(*au, "basic")) hl.push_back({"Authorization", "Basic " + b64Encode(optS(*au, "basic"))});
                else if (au->type == Value::Type::MAP && optHas(*au, "key")) hl.push_back({optS(*au, "header", "X-API-Key"), optS(*au, "key")});
                else return fail6("auth: المفاتيح المدعومة bearer | basic | key(+header)");
            }
            hl.push_back({"X-Rin-Capture-Headers", "1"});
            r = http::performRequest("GET", url, hl, "", timeout);
#endif
        } else {
            r = http::performBinaryGet(url, timeout);
        }
        if (!r.ok) return fail6(r.error.empty() ? "فشل الاتصال" : r.error, {{"status", N6(static_cast<double>(r.status))}});
        if (r.status < 200 || r.status >= 300) return fail6("الخادوم أعاد الحالة " + std::to_string(r.status), {{"status", N6(static_cast<double>(r.status))}});
        if (static_cast<double>(r.body.size()) > maxBytes) return fail6("حجم الملف يتجاوز maxBytes");
        ensureDir(out);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) return fail6("تعذّر فتح ملف الحفظ للكتابة: " + outRaw);
        f.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
        f.close();
        if (!f) return fail6("فشلت كتابة الملف");
        std::string ctype;
        for (auto& h : r.headers) if (md::lower(h.first) == "content-type") ctype = h.second;
        return M6({{"ok", B6(true)}, {"path", S6(outRaw)}, {"bytes", N6(static_cast<double>(r.body.size()))},
                   {"status", N6(static_cast<double>(r.status))}, {"contentType", S6(ctype)}});
    };
}

} // namespace rin
