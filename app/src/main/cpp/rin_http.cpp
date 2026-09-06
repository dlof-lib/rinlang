#include "rin_http.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(__ANDROID__)
    // لا حاجة لأي رؤوس نظام هنا؛ التنفيذ الفعلي يمر بالكامل عبر androidBridge أدناه.
#elif defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <signal.h>
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
                                               const HeaderList& headers, const std::string& body, int timeoutMs) {
    std::vector<std::string> args = {"curl", "-s", "-S", "-L"};
    args.push_back("-X"); args.push_back(method.empty() ? "GET" : method);
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
    return g_androidBridge(method, url, headers, body, timeoutMs);
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
                           const HeaderList& headers, const std::string& body, int timeoutMs) {
    HttpResult r;
    std::ostringstream cmd;
    cmd << "curl -s -S -L -X " << winQuote(method.empty() ? "GET" : method);
    int seconds = timeoutMs > 0 ? (timeoutMs + 999) / 1000 : 15;
    cmd << " --max-time " << seconds;
    for (auto& h : headers) cmd << " -H " << winQuote(h.first + ": " + h.second);
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
    r.body = out;
    r.status = status;
    r.ok = (rc == 0) && status > 0;
    if (!r.ok && r.error.empty()) r.error = status > 0 ? "" : "فشل الاتصال (تحقق من الشبكة/الرابط)";
    return r;
}

#else // POSIX desktop (Linux/macOS CLI tools)

HttpResult performRequest(const std::string& method, const std::string& url,
                           const HeaderList& headers, const std::string& body, int timeoutMs) {
    HttpResult r;
    auto argsVec = buildCurlArgs(method, url, headers, body, timeoutMs);

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

} // namespace http
} // namespace rin
