// rin_media.h — طبقة الوسائط الحقيقية (فيديو / صوت / صور / OCR) خلف دوال `make.*`.
//
// الفلسفة نفسها المتّبعة في rin_http.h:
//   * سطح المكتب/CLI (لينكس/macOS): تنفيذ فعلي عبر أدوات النظام الموجودة أصلاً (ffmpeg/ffprobe/
//     ffplay/mpv/vlc، ImageMagick، tesseract، rembg) — دائماً عبر fork/exec بمصفوفة argv مباشرة،
//     بلا أي shell، فلا يمكن حقن أوامر عبر أسماء الملفات أو الروابط القادمة من كود المستخدم.
//   * أندرويد: لا توجد أدوات سطر أوامر، فتُمرَّر العملية إلى Kotlin عبر جسر (setBridge) يسجّله
//     jni_bridge.cpp — انظر RinMediaBridge.kt. إن لم يُسجَّل جسر تعود الدوال بخطأ واضح بدل الانهيار.
//   * WASM / ويندوز: غير مدعوم بعد؛ تُرجع الدوال ok=false مع رسالة واضحة.
//
// الملف header-only عمداً (كل الدوال inline) ويُضمَّن من rin_extra_natives6.cpp الذي يُضمَّن بدوره
// في نهاية rin_interpreter.cpp — فيدخل كل أهداف البناء (APK / CLI / WASM / CI) بلا أي تعديل
// في ملفات CMake أو سكربتات البناء.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <algorithm>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
#  define RIN_MEDIA_POSIX 1
#  include <unistd.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>
#  include <sys/wait.h>
#  include <sys/stat.h>
#  include <chrono>
#endif

namespace rin {
namespace media {

using Argv = std::vector<std::string>;
using KV = std::vector<std::pair<std::string, std::string>>;

// ---------------------------------------------------------------------------------------------
// جسر المنصة (أندرويد): عملية واحدة عامة op + معاملات نصية، يُرجع ok/error/حقول نصية.
// op المعروفة: video.open | audio.play | audio.stop | image.removeBg | image.ocr
// ---------------------------------------------------------------------------------------------
struct BridgeResult {
    bool handled = false;   // false = الجسر لا يعرف هذه العملية (جرّب التنفيذ المحلي)
    bool ok = false;
    std::string error;
    KV fields;              // حقول إضافية تُدمَج في الخريطة المُرجَعة (text/confidence/path/...)
};
using BridgeFn = std::function<BridgeResult(const std::string& op, const KV& args)>;

inline BridgeFn& bridgeSlot() { static BridgeFn b; return b; }
inline void setBridge(BridgeFn fn) { bridgeSlot() = std::move(fn); }
inline bool hasBridge() { return static_cast<bool>(bridgeSlot()); }

inline const char* platformName() {
#if defined(__ANDROID__)
    return "android";
#elif defined(__EMSCRIPTEN__)
    return "wasm";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#else
    return "unknown";
#endif
}

// ---------------------------------------------------------------------------------------------
// تشغيل العمليات
// ---------------------------------------------------------------------------------------------
struct ProcResult {
    bool started = false;     // false = لم نستطع تشغيل العملية أصلاً (أداة مفقودة/منصة غير مدعومة)
    bool timedOut = false;
    int exitCode = -1;
    std::string out;
    std::string err;
    std::string error;        // سبب الفشل البشري إن started == false
};

#ifdef RIN_MEDIA_POSIX

inline std::string which(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) {
        return (!name.empty() && access(name.c_str(), X_OK) == 0) ? name : std::string();
    }
    const char* pathEnv = std::getenv("PATH");
    std::string path = pathEnv ? pathEnv : "/usr/local/bin:/usr/bin:/bin";
    size_t i = 0;
    while (i <= path.size()) {
        size_t colon = path.find(':', i);
        std::string dir = path.substr(i, colon == std::string::npos ? std::string::npos : colon - i);
        if (dir.empty()) dir = ".";
        std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(full.c_str(), X_OK) == 0) return full;
        if (colon == std::string::npos) break;
        i = colon + 1;
    }
    return std::string();
}

inline std::vector<char*> toCArgv(const Argv& argv) {
    std::vector<char*> v;
    v.reserve(argv.size() + 1);
    for (auto& s : argv) v.push_back(const_cast<char*>(s.c_str()));
    v.push_back(nullptr);
    return v;
}

// يشغّل argv (argv[0] اسم أداة أو مسار) ويجمع stdout/stderr حتى [maxBytes] لكل منهما، مع مهلة.
inline ProcResult run(const Argv& argv, int timeoutMs = 60000, size_t maxBytes = 8u << 20) {
    ProcResult r;
    if (argv.empty()) { r.error = "أمر فارغ"; return r; }
    std::string exe = which(argv[0]);
    if (exe.empty()) { r.error = "الأداة '" + argv[0] + "' غير موجودة على هذا الجهاز"; return r; }

    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0) { r.error = "pipe() فشلت"; return r; }
    if (pipe(errPipe) != 0) { close(outPipe[0]); close(outPipe[1]); r.error = "pipe() فشلت"; return r; }

    pid_t pid = fork();
    if (pid < 0) {
        close(outPipe[0]); close(outPipe[1]); close(errPipe[0]); close(errPipe[1]);
        r.error = "fork() فشلت"; return r;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); close(devnull); }
        dup2(outPipe[1], 1); dup2(errPipe[1], 2);
        close(outPipe[0]); close(outPipe[1]); close(errPipe[0]); close(errPipe[1]);
        Argv a = argv; a[0] = exe;
        auto cargv = toCArgv(a);
        execv(exe.c_str(), cargv.data());
        _exit(127);
    }
    close(outPipe[1]); close(errPipe[1]);
    r.started = true;

    auto t0 = std::chrono::steady_clock::now();
    struct pollfd fds[2] = {{outPipe[0], POLLIN, 0}, {errPipe[0], POLLIN, 0}};
    bool open0 = true, open1 = true;
    char buf[8192];
    while (open0 || open1) {
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (timeoutMs > 0 && el >= timeoutMs) { r.timedOut = true; kill(pid, SIGKILL); break; }
        int wait = timeoutMs > 0 ? static_cast<int>(std::min<long long>(200, timeoutMs - el)) : 200;
        if (wait < 1) wait = 1;
        int pr = poll(fds, 2, wait);
        if (pr < 0) break;
        for (int k = 0; k < 2; ++k) {
            bool& o = k == 0 ? open0 : open1;
            if (!o) continue;
            if (fds[k].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = read(fds[k].fd, buf, sizeof(buf));
                if (n > 0) {
                    std::string& dst = k == 0 ? r.out : r.err;
                    if (dst.size() < maxBytes) dst.append(buf, static_cast<size_t>(n));
                } else { o = false; fds[k].fd = -1; }
            }
        }
    }
    close(outPipe[0]); close(errPipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exitCode = 128 + WTERMSIG(status);
    return r;
}

// يشغّل argv منفصلاً تماماً (خلفية) ويُرجع pid؛ stdio كلها إلى /dev/null. fork مزدوج لتفادي الزومبي.
inline long spawnDetached(const Argv& argv, std::string& error) {
    if (argv.empty()) { error = "أمر فارغ"; return -1; }
    std::string exe = which(argv[0]);
    if (exe.empty()) { error = "الأداة '" + argv[0] + "' غير موجودة على هذا الجهاز"; return -1; }
    int pidPipe[2];
    if (pipe(pidPipe) != 0) { error = "pipe() فشلت"; return -1; }
    pid_t first = fork();
    if (first < 0) { close(pidPipe[0]); close(pidPipe[1]); error = "fork() فشلت"; return -1; }
    if (first == 0) {
        close(pidPipe[0]);
        setsid();
        pid_t second = fork();
        if (second < 0) _exit(1);
        if (second > 0) {
            long p = second;
            ssize_t w = write(pidPipe[1], &p, sizeof(p)); (void)w;
            close(pidPipe[1]);
            _exit(0);
        }
        close(pidPipe[1]);
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
        Argv a = argv; a[0] = exe;
        auto cargv = toCArgv(a);
        execv(exe.c_str(), cargv.data());
        _exit(127);
    }
    close(pidPipe[1]);
    long pid = -1;
    ssize_t n = read(pidPipe[0], &pid, sizeof(pid));
    close(pidPipe[0]);
    int st = 0; waitpid(first, &st, 0);
    if (n != static_cast<ssize_t>(sizeof(pid)) || pid <= 0) { error = "تعذّر تشغيل العملية"; return -1; }
    return pid;
}

inline bool killPid(long pid) { return pid > 1 && kill(static_cast<pid_t>(pid), SIGTERM) == 0; }
inline bool pidAlive(long pid) { return pid > 1 && kill(static_cast<pid_t>(pid), 0) == 0; }

#else  // لا POSIX (ويندوز/WASM)

inline std::string which(const std::string&) { return std::string(); }
inline ProcResult run(const Argv&, int = 0, size_t = 0) {
    ProcResult r; r.error = std::string("تشغيل أدوات الوسائط غير مدعوم على المنصة: ") + platformName(); return r;
}
inline long spawnDetached(const Argv&, std::string& error) {
    error = std::string("تشغيل أدوات الوسائط غير مدعوم على المنصة: ") + platformName(); return -1;
}
inline bool killPid(long) { return false; }
inline bool pidAlive(long) { return false; }

#endif

// أول أداة موجودة من القائمة (اسم فارغ إن لا شيء).
inline std::string firstTool(const std::vector<std::string>& names) {
    for (auto& n : names) if (!which(n).empty()) return n;
    return std::string();
}

// ---------------------------------------------------------------------------------------------
// أمان المدخلات
// ---------------------------------------------------------------------------------------------
inline bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
inline std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

inline bool isHttpUrl(const std::string& s) {
    std::string l = lower(s.substr(0, 8));
    return startsWith(l, "http://") || startsWith(l, "https://");
}

// رابط/مسار آمن للتمرير كوسيط: لا يبدأ بـ '-' (حقن خيارات) ولا يحوي NUL أو أسطراً جديدة.
inline bool safeArg(const std::string& s) {
    if (s.empty() || s[0] == '-') return false;
    for (unsigned char c : s) if (c == 0 || c == '\n' || c == '\r') return false;
    return true;
}

inline bool fileExists(const std::string& p) {
#ifdef RIN_MEDIA_POSIX
    struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#else
    std::ifstream f(p, std::ios::binary); return f.good();
#endif
}

inline long long fileSize(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    return f.good() ? static_cast<long long>(f.tellg()) : -1;
}

// تعريف نوع الصورة من البايتات الأولى (magic bytes) — لا من الامتداد. يُرجع اسم coder
// لـ ImageMagick ("png"/"jpeg"/"webp"/"gif"/"bmp"/"tiff") أو "" إن لم يكن صورة نقطية معروفة.
// نمرّره صراحة كبادئة (png:file) فيستحيل خداع ImageMagick بملف MVG/MSL/SVG يتنكّر بامتداد صورة.
inline std::string sniffImage(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    unsigned char h[16] = {0};
    if (!f.read(reinterpret_cast<char*>(h), sizeof(h)) && f.gcount() < 4) return "";
    if (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') return "png";
    if (h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return "jpeg";
    if (std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WEBP", 4) == 0) return "webp";
    if (std::memcmp(h, "GIF8", 4) == 0) return "gif";
    if (h[0] == 'B' && h[1] == 'M') return "bmp";
    if ((h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) || (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42)) return "tiff";
    return "";
}

// "#rgb" / "#rrggbb" / "#rrggbbaa" / اسم لون بسيط (حروف فقط) -> نص آمن لـ ImageMagick، أو "".
inline std::string safeColor(const std::string& in) {
    std::string s = in;
    if (s.empty()) return "";
    if (s[0] == '#') {
        if (s.size() != 4 && s.size() != 7 && s.size() != 9) return "";
        for (size_t i = 1; i < s.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return "";
        return s;
    }
    if (s.size() > 24) return "";
    for (unsigned char c : s) if (!std::isalpha(c)) return "";
    return s;
}

} // namespace media
} // namespace rin
