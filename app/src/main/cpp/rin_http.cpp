#include "rin_http.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <cerrno>
#include <cstdlib>

#if defined(__ANDROID__)
    // لا حاجة لأي رؤوس نظام هنا؛ التنفيذ الفعلي يمر بالكامل عبر androidBridge أدناه.
#elif defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/stat.h>
#endif

namespace rin {
namespace http {

static std::function<HttpResult(const std::string&, const std::string&, const HeaderList&, const std::string&, int)> g_androidBridge;
static std::function<HttpResult(const std::string&, int)> g_androidBinaryGetBridge;

void setAndroidBridge(std::function<HttpResult(const std::string&, const std::string&, const HeaderList&, const std::string&, int)> bridge) {
    g_androidBridge = std::move(bridge);
}

void setAndroidBinaryGetBridge(std::function<HttpResult(const std::string&, int)> bridge) {
    g_androidBinaryGetBridge = std::move(bridge);
}

static std::function<DownloadResult(const DownloadRequest&)> g_androidDownloadBridge;

void setAndroidDownloadBridge(std::function<DownloadResult(const DownloadRequest&)> bridge) {
    g_androidDownloadBridge = std::move(bridge);
}


// ---- التقاط ترويسات الرد (اختياري) -----------------------------------------------------------
// الترويسة الوهمية kCaptureHeadersKey في قائمة ترويسات الطلب تطلب من هذه الطبقة إعادة ترويسات
// الرد الفعلية في HttpResult::headers (تُزال قبل الإرسال ولا تصل للخادوم). تعمل على مسارات curl
// (لينكس/macOS/ويندوز). على أندرويد تُزال فقط وتبقى headers فارغة (الجسر لا يعيدها).
// طلب HEAD يُنفَّذ عبر curl -I (وليس -X HEAD الذي يعلّق انتظاراً لجسم لن يصل).
static const char* kCaptureHeadersKey = "X-Rin-Capture-Headers";

static bool takeCaptureFlag(const HeaderList& in, HeaderList& out) {
    bool capture = false;
    for (auto& h : in) {
        if (h.first == kCaptureHeadersKey) { capture = true; continue; }
        out.push_back(h);
    }
    return capture;
}

// يفكّ كتل ترويسات "HTTP/..." من بداية [out] (كتلة لكل تحويل redirect)؛ الكتلة الأخيرة هي النهائية.
static void parseHeaderBlocks(std::string& out, HeaderList& hdrs) {
    while (out.compare(0, 5, "HTTP/") == 0) {
        size_t e = out.find("\r\n\r\n");
        size_t sepLen = 4;
        if (e == std::string::npos) { e = out.find("\n\n"); sepLen = 2; }
        std::string block = (e == std::string::npos) ? out : out.substr(0, e);
        out.erase(0, e == std::string::npos ? out.size() : e + sepLen);
        hdrs.clear();
        size_t pos = 0; bool first = true;
        while (pos <= block.size()) {
            size_t nl = block.find('\n', pos);
            std::string line = block.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? block.size() + 1 : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (first) { first = false; continue; }
            size_t c = line.find(':');
            if (c == std::string::npos || c == 0) continue;
            size_t v = c + 1;
            while (v < line.size() && line[v] == ' ') ++v;
            hdrs.push_back({line.substr(0, c), line.substr(v)});
        }
        if (e == std::string::npos) break;
    }
}

// يستخرج "<<<RIN_HTTP_STATUS:NNN>>>" المُلحَقة بنهاية stdout عبر curl -w (انظر buildCurlArgs)،
// ويُعيد الجسم الحقيقي (بدون هذه اللاحقة) + رمز الحالة المستخرَج منها. status=0 إن لم توجد.
static long extractStatusMarker(std::string& combined) {
    const std::string marker = "<<<RIN_HTTP_STATUS:";
    size_t pos = combined.rfind(marker);
    if (pos == std::string::npos) return 0;
    size_t end = combined.find(">>>", pos);
    if (end == std::string::npos) return 0;
    long status = 0;
    try { status = std::stol(combined.substr(pos + marker.size(), end - (pos + marker.size()))); } catch (...) { status = 0; }
    combined.erase(pos);
    return status;
}

#if !defined(__ANDROID__)
static std::vector<std::string> buildCurlArgs(const std::string& method, const std::string& url,
                                               const HeaderList& headers, const std::string& body, int timeoutMs,
                                               bool captureHeaders = false) {
    std::vector<std::string> args = {"curl", "-s", "-S", "-L"};
    if (method == "HEAD") { args.push_back("-I"); }
    else { args.push_back("-X"); args.push_back(method.empty() ? "GET" : method); if (captureHeaders) { args.push_back("-D"); args.push_back("-"); } }
    int seconds = timeoutMs > 0 ? (timeoutMs + 999) / 1000 : 15;
    args.push_back("--max-time"); args.push_back(std::to_string(seconds));
    for (auto& h : headers) { args.push_back("-H"); args.push_back(h.first + ": " + h.second); }
    if (!body.empty()) { args.push_back("--data-binary"); args.push_back(body); }
    // لاحقة تحمل رمز حالة HTTP الفعلي فقط (نستخرجها بعد التنفيذ)؛ الجسم الحقيقي يبقى كما هو قبلها.
    args.push_back("-w"); args.push_back("<<<RIN_HTTP_STATUS:%{http_code}>>>");
    args.push_back("--"); args.push_back(url);
    return args;
}
#endif

#if defined(__ANDROID__)

HttpResult performRequest(const std::string& method, const std::string& url,
                           const HeaderList& headers, const std::string& body, int timeoutMs) {
    if (!g_androidBridge) {
        HttpResult r; r.ok = false; r.error = "جسر HTTP الخاص بأندرويد غير مُهيَّأ بعد (JNI_OnLoad لم يُسجِّله)"; return r;
    }
    HeaderList clean;
    takeCaptureFlag(headers, clean); // الجسر لا يعيد ترويسات الرد؛ نزيل الترويسة الوهمية فقط
    return g_androidBridge(method, url, clean, body, timeoutMs);
}

#elif defined(_WIN32)

// ويندوز CLI (رسمها curl.exe المرفَق افتراضياً منذ ويندوز 10 1803): لا يوجد fork/exec بلا shell
// جاهز بنفس بساطة POSIX هنا، لذا نبني سطر أوامر واحداً بتهريب صحيح لكل وسيط (اقتباس مزدوج +
// مضاعفة أي علامة اقتباس داخلية) ثم نستخدم _popen لتشغيله فعلياً والتقاط ناتجه الحقيقي.
static std::string winQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c; }
    out += "\"";
    return out;
}

HttpResult performRequest(const std::string& method, const std::string& url,
                           const HeaderList& headersIn, const std::string& body, int timeoutMs) {
    HttpResult r;
    HeaderList hdrs;
    bool capture = takeCaptureFlag(headersIn, hdrs);
    bool isHead = (method == "HEAD");
    std::ostringstream cmd;
    cmd << "curl -s -S -L";
    if (isHead) cmd << " -I";
    else { cmd << " -X " << winQuote(method.empty() ? "GET" : method); if (capture) cmd << " -D -"; }
    int seconds = timeoutMs > 0 ? (timeoutMs + 999) / 1000 : 15;
    cmd << " --max-time " << seconds;
    for (auto& h : hdrs) cmd << " -H " << winQuote(h.first + ": " + h.second);
    if (!body.empty()) cmd << " --data-binary " << winQuote(body);
    cmd << " -w " << winQuote("<<<RIN_HTTP_STATUS:%{http_code}>>>");
    cmd << " -- " << winQuote(url);

    FILE* pipe = _popen(cmd.str().c_str(), "rb");
    if (!pipe) { r.ok = false; r.error = "تعذّر تشغيل curl (تأكد من وجوده في PATH)"; return r; }
    std::string out;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
    int rc = _pclose(pipe);

    long status = extractStatusMarker(out);
    if (capture || isHead) parseHeaderBlocks(out, r.headers);
    r.body = out;
    r.status = status;
    r.ok = (rc == 0) && status > 0;
    if (!r.ok && r.error.empty()) r.error = status > 0 ? "" : "فشل الاتصال (تحقق من الشبكة/الرابط)";
    return r;
}

#else // POSIX desktop (Linux/macOS CLI tools)

HttpResult performRequest(const std::string& method, const std::string& url,
                           const HeaderList& headersIn, const std::string& body, int timeoutMs) {
    HttpResult r;
    HeaderList headers;
    bool capture = takeCaptureFlag(headersIn, headers);
    bool isHead = (method == "HEAD");
    auto argsVec = buildCurlArgs(method, url, headers, body, timeoutMs, capture);

    int outPipe[2];
    if (pipe(outPipe) != 0) { r.ok = false; r.error = "تعذّر إنشاء pipe لالتقاط رد curl"; return r; }

    pid_t pid = fork();
    if (pid < 0) { r.ok = false; r.error = "فشل fork() لتشغيل curl"; close(outPipe[0]); close(outPipe[1]); return r; }

    if (pid == 0) {
        // ---- ابن العملية: ينفّذ curl فعلياً (بلا أي shell وسيط، argv مباشرة) ----
        close(outPipe[0]);
        dup2(outPipe[1], STDOUT_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
        close(outPipe[1]);

        std::vector<char*> argv;
        argv.reserve(argsVec.size() + 1);
        for (auto& s : argsVec) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp("curl", argv.data());
        _exit(127); // curl غير موجود على PATH
    }

    // ---- الأب: يقرأ رد curl فعلياً من الـ pipe حتى انتهائه ----
    close(outPipe[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(outPipe[0], buf, sizeof(buf))) > 0) out.append(buf, (size_t)n);
    close(outPipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    bool execFailed = WIFEXITED(status) && WEXITSTATUS(status) == 127;

    if (execFailed) {
        r.ok = false;
        r.error = "أداة curl غير موجودة على هذا الجهاز (مطلوبة لتنفيذ اتصال HTTP حقيقي من الأداة السطرية)";
        return r;
    }

    long httpStatus = extractStatusMarker(out);
    if (capture || isHead) parseHeaderBlocks(out, r.headers);
    r.body = out;
    r.status = httpStatus;
    r.ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && httpStatus > 0;
    if (!r.ok && r.error.empty()) {
        r.error = httpStatus > 0 ? "" : "فشل الاتصال (DNS/رفض الاتصال/انتهاء المهلة) — تحقق من الرابط والشبكة";
    }
    return r;
}

#endif

// performBinaryGet: على أندرويد يمر عبر g_androidBinaryGetBridge (jbyteArray خام، انظر
// rin_http.h)؛ في غيابه (بناء لم يُسجِّل الجسر الثنائي بعد) يتراجع إلى performRequest العادي حتى
// لا ينهار البرنامج، رغم أن ذلك قد يُفسد بايتات ثنائية حقيقية على أندرويد تحديداً. على أي منصة
// أخرى (CLI/سطح مكتب) نستخدم performRequest العادي دائماً لأنه آمن للبايتات هناك أصلاً (curl عبر
// pipe/subprocess حقيقي، بلا أي تحويل نصي وسيط).
HttpResult performBinaryGet(const std::string& url, int timeoutMs) {
#if defined(__ANDROID__)
    if (g_androidBinaryGetBridge) return g_androidBinaryGetBridge(url, timeoutMs);
#endif
    return performRequest("GET", url, {}, "", timeoutMs);
}

// ================================================================================================
// performDownload — تنزيل متدفّق إلى ملف (انظر rin_http.h). الجسم لا يمرّ عبر الذاكرة.
// ================================================================================================
static long long dlFileSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f.good() ? static_cast<long long>(f.tellg()) : -1;
}

static std::string dlReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string out;
    if (!f) return out;
    char buf[4096];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) out.append(buf, static_cast<size_t>(f.gcount()));
    return out;
}

// يملأ حقول الترويسات من ملف -D (كتلة لكل تحويل redirect؛ الأخيرة هي النهائية).
static void dlFillFromHeaderFile(const std::string& hdrPath, DownloadResult& r) {
    std::string raw = dlReadAll(hdrPath);
    std::remove(hdrPath.c_str());
    HeaderList hl;
    parseHeaderBlocks(raw, hl);
    for (auto& h : hl) {
        std::string k = h.first;
        for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (k == "content-type") {
            std::string v = h.second;
            size_t sc = v.find(';');
            if (sc != std::string::npos) v.erase(sc);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            r.contentType = v;
        } else if (k == "content-disposition") {
            r.contentDisposition = h.second;
        }
    }
}

// "status|finalUrl" المطبوعة عبر -w (الفاصل | بلا أي backslash ليبقى الاقتباس على ويندوز سليماً).
static void dlParseWriteOut(const std::string& out, DownloadResult& r) {
    size_t bar = out.find('|');
    std::string st = out.substr(0, bar);
    try { r.status = std::stol(st); } catch (...) { r.status = 0; }
    if (bar != std::string::npos) {
        r.finalUrl = out.substr(bar + 1);
        while (!r.finalUrl.empty() && (r.finalUrl.back() == '\n' || r.finalUrl.back() == '\r')) r.finalUrl.pop_back();
    }
}

static std::string dlCurlExitMessage(int code) {
    switch (code) {
        case 3:  return "رابط غير صالح";
        case 6:  return "تعذّر حلّ اسم الخادوم (DNS) — تحقق من الرابط والشبكة";
        case 7:  return "رفض الخادوم الاتصال أو لا يمكن الوصول إليه";
        case 22: return "";                     // حالة HTTP >= 400: تُشرح من status
        case 23: return "تعذّرت الكتابة على القرص (المساحة أو الصلاحيات)";
        case 28: return "انتهت المهلة";
        case 35: case 51: case 58: case 60: case 77: case 83:
                 return "فشل اتصال TLS/الشهادة";
        case 47: return "تحويلات كثيرة جداً";
        case 56: return "انقطع الاتصال أثناء التنزيل";
        case 63: return "حجم الملف يتجاوز maxBytes";
        case 1: case 2: return "بروتوكول غير مدعوم — المسموح http:// أو https:// فقط";
        default: return "فشل التنزيل (curl خرج برمز " + std::to_string(code) + ")";
    }
}

#if defined(__ANDROID__)

DownloadResult performDownload(const DownloadRequest& req) {
    if (!g_androidDownloadBridge) {
        DownloadResult r; r.handled = false;
        r.error = "جسر التنزيل المتدفق غير مُهيَّأ (RinHttpBridge.downloadToFile غير موجود في هذا البناء)";
        return r;
    }
    return g_androidDownloadBridge(req);
}

#elif defined(_WIN32)

DownloadResult performDownload(const DownloadRequest& req) {
    DownloadResult r;
    const std::string hdrPath = req.destPath + ".hdr";
    bool resume = req.resume && dlFileSize(req.destPath) > 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::ostringstream cmd;
        cmd << "curl -s -S -L --fail --proto " << winQuote("=http,https") << " --proto-redir " << winQuote("=http,https")
            << " --max-redirs 10 --connect-timeout 20";
        int idleSec = std::max(1, (req.idleTimeoutMs + 999) / 1000);
        cmd << " --speed-limit 1 --speed-time " << idleSec;
        int totalSec = req.totalTimeoutMs > 0 ? (req.totalTimeoutMs + 999) / 1000 : 21600;
        cmd << " --max-time " << totalSec;
        if (req.maxBytes > 0) cmd << " --max-filesize " << req.maxBytes;
        if (resume) cmd << " -C -";
        cmd << " -D " << winQuote(hdrPath) << " -o " << winQuote(req.destPath);
        for (auto& h : req.headers) cmd << " -H " << winQuote(h.first + ": " + h.second);
        cmd << " -w " << winQuote("%{http_code}|%{url_effective}");
        cmd << " -- " << winQuote(req.url);

        FILE* pipe = _popen(cmd.str().c_str(), "rb");
        if (!pipe) { r.error = "تعذّر تشغيل curl (تأكد من وجوده في PATH)"; return r; }
        std::string out; char buf[1024]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
        int rc = _pclose(pipe);

        r = DownloadResult();
        dlParseWriteOut(out, r);
        dlFillFromHeaderFile(hdrPath, r);
        r.bytes = std::max(0LL, dlFileSize(req.destPath));
        if (resume && (r.status == 416 || rc == 33)) {   // استئناف غير ممكن: ابدأ من الصفر
            std::remove(req.destPath.c_str());
            resume = false;
            continue;
        }
        r.resumed = resume && r.status == 206;
        r.ok = (rc == 0) && r.status >= 200 && r.status < 300;
        if (!r.ok) r.error = r.status >= 400 ? "" : dlCurlExitMessage(rc);
        if (r.ok && req.maxBytes > 0 && r.bytes > req.maxBytes) { r.ok = false; r.error = "حجم الملف يتجاوز maxBytes"; }
        return r;
    }
    return r;
}

#else // POSIX (لينكس/macOS): fork/exec مباشر لـ curl بلا shell

DownloadResult performDownload(const DownloadRequest& req) {
    DownloadResult r;
    const std::string hdrPath = req.destPath + ".hdr";
    bool resume = req.resume && dlFileSize(req.destPath) > 0;

    for (int attempt = 0; attempt < 2; ++attempt) {
        std::vector<std::string> args = {"curl", "-s", "-S", "-L", "--fail",
                                         "--proto", "=http,https", "--proto-redir", "=http,https",
                                         "--max-redirs", "10", "--connect-timeout", "20"};
        int idleSec = std::max(1, (req.idleTimeoutMs + 999) / 1000);
        args.push_back("--speed-limit"); args.push_back("1");
        args.push_back("--speed-time"); args.push_back(std::to_string(idleSec));
        int totalSec = req.totalTimeoutMs > 0 ? (req.totalTimeoutMs + 999) / 1000 : 21600;
        args.push_back("--max-time"); args.push_back(std::to_string(totalSec));
        if (req.maxBytes > 0) { args.push_back("--max-filesize"); args.push_back(std::to_string(req.maxBytes)); }
        if (resume) { args.push_back("-C"); args.push_back("-"); }
        args.push_back("-D"); args.push_back(hdrPath);
        args.push_back("-o"); args.push_back(req.destPath);
        for (auto& h : req.headers) { args.push_back("-H"); args.push_back(h.first + ": " + h.second); }
        args.push_back("-w"); args.push_back("%{http_code}|%{url_effective}");
        args.push_back("--"); args.push_back(req.url);

        int outPipe[2];
        if (pipe(outPipe) != 0) { r.error = "تعذّر إنشاء pipe لتشغيل curl"; return r; }
        pid_t pid = fork();
        if (pid < 0) { close(outPipe[0]); close(outPipe[1]); r.error = "فشل fork() لتشغيل curl"; return r; }
        if (pid == 0) {
            close(outPipe[0]);
            dup2(outPipe[1], STDOUT_FILENO);
            int devNull = open("/dev/null", O_RDWR);
            if (devNull >= 0) { dup2(devNull, STDERR_FILENO); dup2(devNull, STDIN_FILENO); close(devNull); }
            close(outPipe[1]);
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            execvp("curl", argv.data());
            _exit(127);
        }
        close(outPipe[1]);

        // انتظار مع مراقبة حجم الملف (لحدّ maxBytes حين لا يعلن الخادوم Content-Length) وسقف زمني.
        auto t0 = std::chrono::steady_clock::now();
        int st = 0;
        bool killedForSize = false, killedForTime = false;
        while (true) {
            pid_t w = waitpid(pid, &st, WNOHANG);
            if (w == pid || (w < 0 && errno != EINTR)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (req.maxBytes > 0 && dlFileSize(req.destPath) > req.maxBytes) {
                kill(pid, SIGKILL); waitpid(pid, &st, 0); killedForSize = true; break;
            }
            auto el = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
            if (el > totalSec + 10) { kill(pid, SIGKILL); waitpid(pid, &st, 0); killedForTime = true; break; }
        }
        std::string out; char buf[1024]; ssize_t n;
        while ((n = read(outPipe[0], buf, sizeof(buf))) > 0) out.append(buf, static_cast<size_t>(n));
        close(outPipe[0]);

        int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        r = DownloadResult();
        dlParseWriteOut(out, r);
        dlFillFromHeaderFile(hdrPath, r);
        r.bytes = std::max(0LL, dlFileSize(req.destPath));

        if (code == 127) { r.error = "أداة curl غير موجودة على هذا الجهاز (مطلوبة لتنزيل الملفات)"; return r; }
        if (resume && (r.status == 416 || code == 33)) {   // الخادوم لا يدعم الاستئناف أو الجزء قديم: ابدأ من الصفر
            std::remove(req.destPath.c_str());
            resume = false;
            continue;
        }
        r.resumed = resume && r.status == 206;
        if (killedForSize) { r.error = "حجم الملف يتجاوز maxBytes"; return r; }
        if (killedForTime) { r.error = "انتهت المهلة الكلية للتنزيل"; return r; }
        r.ok = (code == 0) && r.status >= 200 && r.status < 300;
        if (!r.ok) r.error = (r.status >= 400) ? "" : dlCurlExitMessage(code);
        return r;
    }
    return r;
}

#endif

} // namespace http
} // namespace rin
