// ============================================================================
//  rin_extra_natives3.cpp — Rin 1.0 native library additions
// ----------------------------------------------------------------------------
//  دفعة ثالثة فوق rin_extra_natives.cpp و rin_extra_natives2.cpp دون تعديل أي منهما.
//  تُسجَّل من Interpreter::registerNativesExtra3() التي تُستدعى بعد registerNativesExtra2().
//  يُضمَّن هذا الملف (#include) في نهاية rin_interpreter.cpp، فيدخل كل أهداف البناء تلقائياً.
//
//  المجموعات:
//    1) net.* (اتصال حقيقي بالإنترنت)
//         - HTTP/HTTPS فعلي عبر نفس عميل rin_http (curl على سطح المكتب، HttpURLConnection على
//           أندرويد): fetch/get/post/getJson/postJson/head/headers/status/isUp/measure/
//           checkUrls/download/publicIp/ipInfo/dnsQuery (DNS عبر HTTPS)
//         - مقابس TCP حقيقية (لينكس/macOS/أندرويد): resolve/reverseDns/tcpCheck/tcpPing/
//           tcpSend/whois/localIPs/hostname/isOnline
//         - مساعدات بلا اتصال: urlEncode/urlDecode/joinUrl/isValidUrl/isValidDomain/
//           isValidEmail/isValidPort/isValidMac/ipVersion/cidrInfo/httpStatusText/
//           portService/servicePort/parseHeaders/basicAuth/domainOf
//       * لا تفشل هذه الدوال بخطأ يوقف البرنامج عند فشل الشبكة: تُرجع {ok:false, error:...}.
//       * HTTP مقصور على http:// و https:// فقط (لا file:// ولا أي مخطط آخر).
//       * على ويندوز/WASM تعمل دوال HTTP (إن توفر curl/الجسر) وتُرجع دوال المقابس خطأً واضحاً.
//    2) container.* — أكثر من 35 دالة جديدة: names/remove/removeAll/copyField/moveField/
//       getPath/setPath/setDefault/defaults/decr/toggle/push/pop/contains/avg/min/max/
//       distinct/countBy/sortBy/top/search/equals/mergeFrom/checksum/toRows/toCsv/exportCsv/
//       paginate/summary/requireFields/entries/valuesOf/fieldsOfType/...
//       الدوال المُعدِّلة الجديدة تحترم container.lock (ترفض الكتابة على حاوية مقفلة).
// ============================================================================
#include "rin_interpreter.h"
#include "rin_json.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <thread>

#if (defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)) && !defined(__EMSCRIPTEN__)
  #define RIN_NET_SOCKETS 1
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  #if !defined(__ANDROID__) || (defined(__ANDROID_API__) && __ANDROID_API__ >= 24)
    #define RIN_NET_IFADDRS 1
    #include <ifaddrs.h>
    #include <net/if.h>
  #endif
#endif

namespace rin {
namespace extra3 {

using Args = std::vector<Value>;
using namespace rin::extra;
using namespace rin::extra2;

// ---------------------------------------------------------------- small helpers
static int g_netTimeoutMs = 10000;

static double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static int clampTimeout(double v) {
    if (v < 100) v = 100;
    if (v > 60000) v = 60000;
    return static_cast<int>(v);
}
static double round2(double v) { return std::floor(v * 100.0 + 0.5) / 100.0; }

static Value M(std::vector<std::pair<std::string, Value>> kv) {
    MapData out;
    out.reserve(kv.size());
    for (auto& p : kv) out.push_back({Value::string(p.first), std::move(p.second)});
    return newMap(std::move(out));
}
static Value S(const std::string& s) { return Value::string(s); }
static Value N(double d) { return Value::num(d); }
static Value B(bool b) { return Value::boolean_(b); }

static std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
static bool hasBadChars(const std::string& s) {
    for (unsigned char c : s) if (c <= 0x20 || c == 0x7f) return true;
    return false;
}

static const char* statusTextOf(long code) {
    switch (code) {
        case 100: return "Continue"; case 101: return "Switching Protocols";
        case 200: return "OK"; case 201: return "Created"; case 202: return "Accepted";
        case 204: return "No Content"; case 206: return "Partial Content";
        case 301: return "Moved Permanently"; case 302: return "Found"; case 303: return "See Other";
        case 304: return "Not Modified"; case 307: return "Temporary Redirect"; case 308: return "Permanent Redirect";
        case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden";
        case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 408: return "Request Timeout";
        case 409: return "Conflict"; case 410: return "Gone"; case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type"; case 418: return "I'm a teapot"; case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error"; case 501: return "Not Implemented"; case 502: return "Bad Gateway";
        case 503: return "Service Unavailable"; case 504: return "Gateway Timeout";
        default: return "";
    }
}

static std::string urlEncodeComponent(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

static bool isHttpUrl(const std::string& url) {
    if (url.size() > 8192 || hasBadChars(url)) return false;
    std::string l = lowerAscii(url.substr(0, 8));
    if (l.compare(0, 7, "http://") != 0 && l.compare(0, 8, "https://") != 0) return false;
    UrlParts p = parseUrlImpl(url);
    return p.ok && !p.host.empty();
}

static bool isValidDomainName(const std::string& d) {
    if (d.empty() || d.size() > 253) return false;
    std::vector<std::string> labels;
    std::string cur;
    for (char c : d) { if (c == '.') { labels.push_back(cur); cur.clear(); } else cur += c; }
    labels.push_back(cur);
    if (labels.size() < 2) return false;
    for (const auto& l : labels) {
        if (l.empty() || l.size() > 63 || l.front() == '-' || l.back() == '-') return false;
        for (unsigned char c : l) if (!(std::isalnum(c) || c == '-')) return false;
    }
    const std::string& tld = labels.back();
    if (tld.size() < 2) return false;
    for (unsigned char c : tld) if (!std::isalpha(c)) return false;
    return true;
}

static bool validHostForSocket(const std::string& h) {
    if (h.empty() || h.size() > 253 || hasBadChars(h)) return false;
    for (unsigned char c : h) if (!(std::isalnum(c) || c == '.' || c == '-' || c == ':' || c == '_')) return false;
    return true;
}

static std::string b64(const std::string& in) { return base64Encode(in); }

// ---------------------------------------------------------------- HTTP layer
static Value headersToMap(const http::HeaderList& h) {
    MapData m;
    for (const auto& kv : h) {
        std::string k = lowerAscii(kv.first);
        bool found = false;
        for (auto& e : m) if (e.first.str == k) { e.second = S(e.second.str + ", " + kv.second); found = true; break; }
        if (!found) m.push_back({S(k), S(kv.second)});
    }
    return newMap(std::move(m));
}

static Value httpResultMap(const http::HttpResult& r, double ms, const std::string& url) {
    return M({{"ok", B(r.ok)},
              {"status", N(static_cast<double>(r.status))},
              {"statusText", S(statusTextOf(r.status))},
              {"body", S(r.body)},
              {"json", json::decodeOrRaw(r.body)},
              {"error", S(r.error)},
              {"ms", N(round2(ms))},
              {"size", N(static_cast<double>(r.body.size()))},
              {"headers", headersToMap(r.headers)},
              {"url", S(url)}});
}

static Value failMap(const std::string& err, const std::string& url = "") {
    return M({{"ok", B(false)}, {"status", N(0)}, {"statusText", S("")}, {"body", S("")}, {"json", S("")},
              {"error", S(err)}, {"ms", N(0)}, {"size", N(0)}, {"headers", newMap()}, {"url", S(url)}});
}

static http::HttpResult doRequest(const std::string& method, const std::string& url, http::HeaderList headers,
                                  const std::string& body, int timeoutMs, int retries, bool captureHeaders, double* msOut) {
    if (captureHeaders) headers.push_back({"X-Rin-Capture-Headers", "1"});
    http::HttpResult r;
    double t0 = nowMs();
    for (int attempt = 0; attempt <= retries; ++attempt) {
        r = http::performRequest(method, url, headers, body, timeoutMs);
        if (r.ok && r.status < 500) break;
        if (attempt < retries) std::this_thread::sleep_for(std::chrono::milliseconds(300 * (attempt + 1)));
    }
    if (msOut) *msOut = nowMs() - t0;
    return r;
}

static bool okHttpMethod(const std::string& m) {
    static const std::set<std::string> allowed{"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    return allowed.count(m) > 0;
}

// fetch كامل: يتحقق من المخطط، ينفّذ، ويُرجع خريطة موحّدة.
static Value fetchImpl(const std::string& methodIn, const std::string& url, http::HeaderList headers,
                       const Value& bodyVal, int timeoutMs, int retries, int line) {
    std::string method;
    for (char c : methodIn) method += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (!okHttpMethod(method)) return failMap("فعل HTTP غير مدعوم: " + methodIn, url);
    if (!isHttpUrl(url)) return failMap("رابط غير صالح — المسموح http:// أو https:// فقط", url);
    std::string body;
    if (bodyVal.type != Value::Type::NIL) {
        if (bodyVal.type == Value::Type::STRING) body = bodyVal.str;
        else {
            bool hasCT = false;
            for (auto& h : headers) if (lowerAscii(h.first) == "content-type") hasCT = true;
            if (!hasCT) headers.push_back({"Content-Type", "application/json"});
            body = json::encode(bodyVal);
        }
    }
    (void)line;
    double ms = 0;
    http::HttpResult r = doRequest(method, url, headers, body, timeoutMs, retries, true, &ms);
    return httpResultMap(r, ms, url);
}

static http::HeaderList hdrs(const Value& v, const std::string& fn, int line) {
    http::HeaderList out;
    if (v.type == Value::Type::NIL) return out;
    if (v.type != Value::Type::MAP || !v.map)
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' يتوقّع الترويسات كقاموس {مفتاح: قيمة} أو nil");
    for (auto& kv : *v.map) {
        std::string k = kv.first.toDisplayString(), val = kv.second.toDisplayString();
        if (k.find_first_of("\r\n:") != std::string::npos || val.find_first_of("\r\n") != std::string::npos)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "': ترويسة غير صالحة (أسطر جديدة/نقطتان في الاسم)");
        out.push_back({k, val});
    }
    return out;
}

static const Value* optGet(const Value& opts, const std::string& key) {
    if (opts.type != Value::Type::MAP || !opts.map) return nullptr;
    for (auto& kv : *opts.map) if (kv.first.type == Value::Type::STRING && kv.first.str == key) return &kv.second;
    return nullptr;
}

// ---------------------------------------------------------------- sockets layer
#ifdef RIN_NET_SOCKETS
struct Conn { int fd = -1; std::string ip; double ms = 0; std::string error; };

static std::string numericHost(const sockaddr* sa, socklen_t len) {
    char buf[NI_MAXHOST];
    if (getnameinfo(sa, len, buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST) == 0) return buf;
    return "";
}

static Conn tcpConnect(const std::string& host, int port, int timeoutMs) {
    Conn c;
    double t0 = nowMs();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portS = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portS.c_str(), &hints, &res);
    if (rc != 0) { c.error = std::string("تعذّر حلّ الاسم: ") + gai_strerror(rc); c.ms = nowMs() - t0; return c; }
    std::string lastErr = "تعذّر الاتصال";
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        int remaining = timeoutMs - static_cast<int>(nowMs() - t0);
        if (remaining <= 0) { lastErr = "انتهت المهلة"; break; }
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        int r = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        bool connected = (r == 0);
        if (!connected && errno == EINPROGRESS) {
            pollfd p; p.fd = fd; p.events = POLLOUT; p.revents = 0;
            int pr = ::poll(&p, 1, remaining);
            if (pr > 0) {
                int err = 0; socklen_t l = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
                connected = (err == 0);
                if (!connected) lastErr = std::strerror(err);
            } else lastErr = (pr == 0) ? "انتهت المهلة" : "فشل الانتظار على المقبس";
        } else if (!connected) lastErr = std::strerror(errno);
        if (connected) {
            fcntl(fd, F_SETFL, fl);
            c.fd = fd;
            c.ip = numericHost(ai->ai_addr, ai->ai_addrlen);
            break;
        }
        ::close(fd);
    }
    freeaddrinfo(res);
    c.ms = nowMs() - t0;
    if (c.fd < 0) c.error = lastErr;
    return c;
}

// يرسل [data] ثم يقرأ حتى إغلاق الطرف الآخر/المهلة/السقف.
static Conn tcpExchange(const std::string& host, int port, const std::string& data, int timeoutMs,
                        size_t maxBytes, std::string& response) {
    Conn c = tcpConnect(host, port, timeoutMs);
    if (c.fd < 0) return c;
    double t0 = nowMs();
    size_t sent = 0;
    while (sent < data.size()) {
        pollfd p; p.fd = c.fd; p.events = POLLOUT; p.revents = 0;
        int left = timeoutMs - static_cast<int>(nowMs() - t0);
        if (left <= 0 || ::poll(&p, 1, left) <= 0) { c.error = "انتهت مهلة الإرسال"; break; }
#ifdef MSG_NOSIGNAL
        ssize_t n = ::send(c.fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        ssize_t n = ::send(c.fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (n <= 0) { c.error = "فشل الإرسال"; break; }
        sent += static_cast<size_t>(n);
    }
    if (c.error.empty()) {
        char buf[4096];
        while (response.size() < maxBytes) {
            pollfd p; p.fd = c.fd; p.events = POLLIN; p.revents = 0;
            int left = timeoutMs - static_cast<int>(nowMs() - t0);
            if (left <= 0) break;
            int pr = ::poll(&p, 1, left);
            if (pr <= 0) break;
            ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            response.append(buf, static_cast<size_t>(n));
        }
        if (response.size() > maxBytes) response.resize(maxBytes);
    }
    ::close(c.fd);
    c.fd = -1;
    c.ms = nowMs() - t0;
    return c;
}
#endif // RIN_NET_SOCKETS

static Value noSockets(const std::string& fn) {
    return M({{"ok", B(false)}, {"error", S("'" + fn + "' غير مدعومة على هذه المنصة (لا مقابس TCP أصلية)")}});
}

static bool parsePort(const Value& v, int& out) {
    if (v.type != Value::Type::NUMBER) return false;
    if (v.number < 1 || v.number > 65535 || v.number != std::floor(v.number)) return false;
    out = static_cast<int>(v.number);
    return true;
}

// ---------------------------------------------------------------- pure helpers
static std::string joinUrlImpl(const std::string& base, const std::string& rel) {
    if (rel.find("://") != std::string::npos) return rel;
    UrlParts b = parseUrlImpl(base);
    if (!b.ok) return rel;
    std::string authority = b.host.find(':') != std::string::npos ? "[" + b.host + "]" : b.host;
    if (!b.port.empty()) authority += ":" + b.port;
    std::string origin = b.scheme + "://" + authority;
    if (rel.compare(0, 2, "//") == 0) return b.scheme + ":" + rel;
    if (rel.empty()) return base;
    if (rel[0] == '#') return origin + b.path + (b.query.empty() ? "" : "?" + b.query) + rel;
    if (rel[0] == '?') return origin + b.path + rel;
    std::string path;
    if (rel[0] == '/') path = rel;
    else {
        size_t slash = b.path.rfind('/');
        path = (slash == std::string::npos ? "/" : b.path.substr(0, slash + 1)) + rel;
    }
    // تطبيع المقاطع . و .. (قبل ? و #)
    std::string tail;
    size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) { tail = path.substr(cut); path = path.substr(0, cut); }
    std::vector<std::string> segs;
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (cur == "..") { if (segs.size() > 1) segs.pop_back(); }
            else if (cur != ".") segs.push_back(cur);
            cur.clear();
        } else cur += path[i];
    }
    std::string norm;
    for (size_t i = 0; i < segs.size(); ++i) { if (i == 0 && segs[i].empty()) continue; norm += "/" + segs[i]; }
    if (norm.empty()) norm = "/";
    return origin + norm + tail;
}

static bool parseCidr(const std::string& cidr, uint32_t& ip, int& prefix) {
    size_t slash = cidr.find('/');
    if (slash == std::string::npos) return false;
    if (!isValidIPv4(cidr.substr(0, slash), &ip)) return false;
    std::string p = cidr.substr(slash + 1);
    if (p.empty() || p.size() > 2) return false;
    for (char c : p) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    prefix = std::atoi(p.c_str());
    return prefix >= 0 && prefix <= 32;
}
static std::string ipStr(uint32_t n) {
    return std::to_string((n >> 24) & 255) + "." + std::to_string((n >> 16) & 255) + "." +
           std::to_string((n >> 8) & 255) + "." + std::to_string(n & 255);
}

// ---------------------------------------------------------------- container helpers
static bool isInternalField(const std::string& k) { return k.size() >= 2 && k[0] == '_' && k[1] == '_'; }

static int typeRank(const Value& v) {
    switch (v.type) {
        case Value::Type::NIL: return 0; case Value::Type::BOOL: return 1; case Value::Type::NUMBER: return 2;
        case Value::Type::STRING: return 3; default: return 4;
    }
}
static bool lessVal(const Value& a, const Value& b) {
    if (a.type == Value::Type::NUMBER && b.type == Value::Type::NUMBER) return a.number < b.number;
    if (a.type == Value::Type::STRING && b.type == Value::Type::STRING) return a.str < b.str;
    if (a.type == Value::Type::BOOL && b.type == Value::Type::BOOL) return a.boolean < b.boolean;
    int ra = typeRank(a), rb = typeRank(b);
    if (ra != rb) return ra < rb;
    return a.toDisplayString() < b.toDisplayString();
}

static std::string csvCell(const Value& v) {
    if (v.type == Value::Type::NIL) return "";
    std::string s = (v.type == Value::Type::ARRAY || v.type == Value::Type::MAP) ? json::encode(v) : v.toDisplayString();
    // حماية من حقن الصيغ في برامج الجداول: النصوص التي تبدأ بـ = + - @ تُسبق بفاصلة عليا
    if (v.type == Value::Type::STRING && !s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@')) s = "'" + s;
    if (s.find_first_of(",\"\r\n") != std::string::npos) {
        std::string q = "\"";
        for (char c : s) { if (c == '"') q += "\"\""; else q += c; }
        s = q + "\"";
    }
    return s;
}

static std::vector<std::string> splitPath(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) { if (c == '.') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static bool allDigits(const std::string& s) {
    if (s.empty() || s.size() > 9) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}
static void setIn(Value& cur, const std::vector<std::string>& parts, size_t i, const Value& v) {
    if (i == parts.size()) { cur = v; return; }
    const std::string& k = parts[i];
    bool isIdx = allDigits(k);
    if (cur.type == Value::Type::NIL && isIdx) cur = newArray();
    if (cur.type == Value::Type::ARRAY && cur.array && isIdx) {
        size_t n = static_cast<size_t>(std::stoul(k));
        if (n > cur.array->size()) n = cur.array->size(); // لا فجوات: أي فهرس أبعد يُلحَق في النهاية
        if (n == cur.array->size()) cur.array->push_back(Value::nil());
        setIn((*cur.array)[n], parts, i + 1, v);
        return;
    }
    if (cur.type != Value::Type::MAP || !cur.map) cur = newMap();
    MapData& m = *cur.map;
    for (auto& kv : m) if (kv.first.type == Value::Type::STRING && kv.first.str == k) { setIn(kv.second, parts, i + 1, v); return; }
    m.push_back({S(k), Value::nil()});
    setIn(m.back().second, parts, i + 1, v);
}
static const Value* getIn(const Value& cur, const std::vector<std::string>& parts, size_t i) {
    if (i == parts.size()) return &cur;
    const std::string& k = parts[i];
    if (cur.type == Value::Type::ARRAY && cur.array && allDigits(k)) {
        size_t n = static_cast<size_t>(std::stoul(k));
        return n < cur.array->size() ? getIn((*cur.array)[n], parts, i + 1) : nullptr;
    }
    if (cur.type == Value::Type::MAP && cur.map) {
        for (auto& kv : *cur.map) if (kv.first.type == Value::Type::STRING && kv.first.str == k) return getIn(kv.second, parts, i + 1);
    }
    return nullptr;
}

} // namespace extra3

// ============================================================================
void Interpreter::registerNativesExtra3() {
    using namespace extra3;

    // ======================================================== 1) net.* — real network
    natives["net.setTimeout"] = [](Args& a, int line) -> Value {
        need("net.setTimeout", a, 1, 1, line);
        g_netTimeoutMs = clampTimeout(num(a[0], "net.setTimeout", line));
        return N(g_netTimeoutMs);
    };
    natives["net.getTimeout"] = [](Args& a, int line) -> Value { need("net.getTimeout", a, 0, 0, line); return N(g_netTimeoutMs); };

    // net.fetch(url, opts?) — opts: {method, headers, body, timeout, retries}
    natives["net.fetch"] = [](Args& a, int line) -> Value {
        need("net.fetch", a, 1, 2, line);
        std::string url = str(a[0], "net.fetch", line);
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        if (opts.type != Value::Type::NIL && opts.type != Value::Type::MAP)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'net.fetch' يتوقّع الخيارات كقاموس أو nil");
        std::string method = "GET";
        if (const Value* m = optGet(opts, "method")) method = m->toDisplayString();
        http::HeaderList h;
        if (const Value* hv = optGet(opts, "headers")) h = hdrs(*hv, "net.fetch", line);
        Value body = Value::nil();
        if (const Value* bv = optGet(opts, "body")) body = *bv;
        int timeout = g_netTimeoutMs;
        if (const Value* tv = optGet(opts, "timeout")) if (tv->type == Value::Type::NUMBER) timeout = clampTimeout(tv->number);
        int retries = 0;
        if (const Value* rv = optGet(opts, "retries")) if (rv->type == Value::Type::NUMBER) retries = std::max(0, std::min(5, static_cast<int>(rv->number)));
        return fetchImpl(method, url, h, body, timeout, retries, line);
    };
    natives["net.get"] = [](Args& a, int line) -> Value {
        need("net.get", a, 1, 2, line);
        return fetchImpl("GET", str(a[0], "net.get", line), hdrs(a.size() > 1 ? a[1] : Value::nil(), "net.get", line),
                         Value::nil(), g_netTimeoutMs, 0, line);
    };
    natives["net.head"] = [](Args& a, int line) -> Value {
        need("net.head", a, 1, 2, line);
        return fetchImpl("HEAD", str(a[0], "net.head", line), hdrs(a.size() > 1 ? a[1] : Value::nil(), "net.head", line),
                         Value::nil(), g_netTimeoutMs, 0, line);
    };
    natives["net.post"] = [](Args& a, int line) -> Value {
        need("net.post", a, 2, 3, line);
        return fetchImpl("POST", str(a[0], "net.post", line), hdrs(a.size() > 2 ? a[2] : Value::nil(), "net.post", line),
                         a[1], g_netTimeoutMs, 0, line);
    };
    // getJson/postJson -> القيمة المُحلَّلة مباشرة، أو nil عند أي فشل/رد غير JSON
    auto jsonOnly = [](const Value& res) -> Value {
        const Value* ok = optGet(res, "ok");
        const Value* st = optGet(res, "status");
        const Value* js = optGet(res, "json");
        if (!ok || !ok->boolean || !st || st->number < 200 || st->number >= 300 || !js) return Value::nil();
        if (js->type == Value::Type::STRING) return Value::nil();
        return *js;
    };
    natives["net.getJson"] = [jsonOnly](Args& a, int line) -> Value {
        need("net.getJson", a, 1, 2, line);
        http::HeaderList h = hdrs(a.size() > 1 ? a[1] : Value::nil(), "net.getJson", line);
        h.push_back({"Accept", "application/json"});
        return jsonOnly(fetchImpl("GET", str(a[0], "net.getJson", line), h, Value::nil(), g_netTimeoutMs, 0, line));
    };
    natives["net.postJson"] = [jsonOnly](Args& a, int line) -> Value {
        need("net.postJson", a, 2, 3, line);
        http::HeaderList h = hdrs(a.size() > 2 ? a[2] : Value::nil(), "net.postJson", line);
        h.push_back({"Accept", "application/json"});
        return jsonOnly(fetchImpl("POST", str(a[0], "net.postJson", line), h, a[1], g_netTimeoutMs, 0, line));
    };
    // net.headers(url) -> قاموس ترويسات الرد (مفاتيح صغيرة)، أو nil
    natives["net.headers"] = [](Args& a, int line) -> Value {
        need("net.headers", a, 1, 1, line);
        Value r = fetchImpl("HEAD", str(a[0], "net.headers", line), {}, Value::nil(), g_netTimeoutMs, 0, line);
        const Value* ok = optGet(r, "ok");
        const Value* h = optGet(r, "headers");
        return (ok && ok->boolean && h) ? *h : Value::nil();
    };
    natives["net.status"] = [](Args& a, int line) -> Value {
        need("net.status", a, 1, 1, line);
        Value r = fetchImpl("HEAD", str(a[0], "net.status", line), {}, Value::nil(), g_netTimeoutMs, 0, line);
        const Value* st = optGet(r, "status");
        return st ? *st : N(0);
    };
    // net.isUp(url) — هل يستجيب الخادوم (أي رد بحالة < 500)
    natives["net.isUp"] = [](Args& a, int line) -> Value {
        need("net.isUp", a, 1, 1, line);
        Value r = fetchImpl("HEAD", str(a[0], "net.isUp", line), {}, Value::nil(), g_netTimeoutMs, 0, line);
        const Value* ok = optGet(r, "ok");
        const Value* st = optGet(r, "status");
        return B(ok && ok->boolean && st && st->number > 0 && st->number < 500);
    };
    // net.measure(url, count=3) — زمن استجابة HTTP فعلي (ms) مع إحصاءات
    natives["net.measure"] = [](Args& a, int line) -> Value {
        need("net.measure", a, 1, 2, line);
        std::string url = str(a[0], "net.measure", line);
        int count = a.size() > 1 ? static_cast<int>(num(a[1], "net.measure", line)) : 3;
        count = std::max(1, std::min(20, count));
        std::vector<double> times;
        int failed = 0;
        for (int i = 0; i < count; ++i) {
            Value r = fetchImpl("GET", url, {}, Value::nil(), g_netTimeoutMs, 0, line);
            const Value* ok = optGet(r, "ok");
            const Value* ms = optGet(r, "ms");
            if (ok && ok->boolean && ms) times.push_back(ms->number); else ++failed;
        }
        double mn = 0, mx = 0, avg = 0, med = 0;
        if (!times.empty()) {
            std::sort(times.begin(), times.end());
            mn = times.front(); mx = times.back();
            double sum = 0; for (double t : times) sum += t;
            avg = sum / times.size();
            med = times.size() % 2 ? times[times.size() / 2] : (times[times.size() / 2 - 1] + times[times.size() / 2]) / 2;
        }
        return M({{"url", S(url)}, {"count", N(count)}, {"ok", N(static_cast<double>(times.size()))}, {"failed", N(failed)},
                  {"minMs", N(round2(mn))}, {"avgMs", N(round2(avg))}, {"medianMs", N(round2(med))}, {"maxMs", N(round2(mx))}});
    };
    // net.checkUrls([urls]) -> [{url, ok, status, ms}] (حتى 50 رابطاً، تسلسلياً)
    natives["net.checkUrls"] = [](Args& a, int line) -> Value {
        need("net.checkUrls", a, 1, 1, line);
        const ArrayData& urls = arr(a[0], "net.checkUrls", line);
        if (urls.size() > 50) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'net.checkUrls' يقبل 50 رابطاً كحد أقصى");
        ArrayData out;
        for (const auto& u : urls) {
            std::string url = u.toDisplayString();
            Value r = fetchImpl("HEAD", url, {}, Value::nil(), g_netTimeoutMs, 0, line);
            const Value* ok = optGet(r, "ok"); const Value* st = optGet(r, "status");
            const Value* ms = optGet(r, "ms"); const Value* er = optGet(r, "error");
            out.push_back(M({{"url", S(url)}, {"ok", B(ok && ok->boolean)}, {"status", st ? *st : N(0)},
                             {"ms", ms ? *ms : N(0)}, {"error", er ? *er : S("")}}));
        }
        return newArray(std::move(out));
    };
    // net.download(url, path) — تنزيل حقيقي إلى ملف (المسار يمرّ بعزل resolvePath)
    natives["net.download"] = [this](Args& a, int line) -> Value {
        need("net.download", a, 2, 2, line);
        std::string url = str(a[0], "net.download", line), path = str(a[1], "net.download", line);
        if (!isHttpUrl(url)) return M({{"ok", B(false)}, {"bytes", N(0)}, {"path", S(path)}, {"error", S("رابط غير صالح — المسموح http:// أو https:// فقط")}});
        double t0 = nowMs();
        http::HttpResult r = http::performBinaryGet(url, g_netTimeoutMs > 0 ? std::max(g_netTimeoutMs, 30000) : 0);
        if (!r.ok || r.status < 200 || r.status >= 300)
            return M({{"ok", B(false)}, {"status", N(static_cast<double>(r.status))}, {"bytes", N(0)}, {"path", S(path)},
                      {"error", S(r.error.empty() ? "حالة HTTP غير ناجحة: " + std::to_string(r.status) : r.error)}});
        writeRealFile(path, r.body, line, "net.download");
        return M({{"ok", B(true)}, {"status", N(static_cast<double>(r.status))}, {"bytes", N(static_cast<double>(r.body.size()))},
                  {"path", S(path)}, {"ms", N(round2(nowMs() - t0))}, {"error", S("")}});
    };
    // net.publicIp() — عنوانك العام كما يراه الإنترنت (أو nil)
    natives["net.publicIp"] = [](Args& a, int line) -> Value {
        need("net.publicIp", a, 0, 0, line);
        for (const char* u : {"https://api.ipify.org", "https://icanhazip.com"}) {
            Value r = fetchImpl("GET", u, {}, Value::nil(), g_netTimeoutMs, 0, line);
            const Value* ok = optGet(r, "ok"); const Value* body = optGet(r, "body");
            if (ok && ok->boolean && body) {
                std::string ip = trimStr(body->str);
                if (isValidIPv4(ip) || isValidIPv6(ip)) return S(ip);
            }
        }
        return Value::nil();
    };
    // net.ipInfo(ip?) — معلومات عنوان (بلد/مدينة/مزوّد) من ipinfo.io
    natives["net.ipInfo"] = [](Args& a, int line) -> Value {
        need("net.ipInfo", a, 0, 1, line);
        std::string path = "/json";
        if (!a.empty()) {
            std::string ip = str(a[0], "net.ipInfo", line);
            if (!isValidIPv4(ip) && !isValidIPv6(ip)) return Value::nil();
            path = "/" + urlEncodeComponent(ip) + "/json";
        }
        Value r = fetchImpl("GET", "https://ipinfo.io" + path, {}, Value::nil(), g_netTimeoutMs, 0, line);
        const Value* ok = optGet(r, "ok"); const Value* js = optGet(r, "json");
        if (ok && ok->boolean && js && js->type == Value::Type::MAP) return *js;
        return Value::nil();
    };
    // net.dnsQuery(name, type="A") — سجلات DNS حقيقية عبر DNS-over-HTTPS (Cloudflare)
    natives["net.dnsQuery"] = [](Args& a, int line) -> Value {
        need("net.dnsQuery", a, 1, 2, line);
        std::string name = str(a[0], "net.dnsQuery", line);
        std::string type = a.size() > 1 ? str(a[1], "net.dnsQuery", line) : "A";
        for (auto& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        static const std::set<std::string> okTypes{"A", "AAAA", "MX", "TXT", "NS", "CNAME", "SOA", "CAA", "PTR", "SRV"};
        if (!isValidDomainName(name) || !okTypes.count(type))
            return M({{"ok", B(false)}, {"name", S(name)}, {"type", S(type)}, {"answers", newArray()}, {"error", S("اسم نطاق أو نوع سجل غير صالح")}});
        http::HeaderList h{{"Accept", "application/dns-json"}};
        Value r = fetchImpl("GET", "https://cloudflare-dns.com/dns-query?name=" + urlEncodeComponent(name) + "&type=" + type,
                            h, Value::nil(), g_netTimeoutMs, 0, line);
        const Value* ok = optGet(r, "ok"); const Value* js = optGet(r, "json"); const Value* er = optGet(r, "error");
        if (!ok || !ok->boolean || !js || js->type != Value::Type::MAP)
            return M({{"ok", B(false)}, {"name", S(name)}, {"type", S(type)}, {"answers", newArray()},
                      {"error", S(er && !er->str.empty() ? er->str : "رد DNS غير صالح")}});
        static const std::map<int, std::string> typeNames{{1, "A"}, {2, "NS"}, {5, "CNAME"}, {6, "SOA"}, {12, "PTR"}, {15, "MX"},
                                                          {16, "TXT"}, {28, "AAAA"}, {33, "SRV"}, {257, "CAA"}};
        ArrayData answers;
        if (const Value* an = optGet(*js, "Answer")) if (an->type == Value::Type::ARRAY && an->array) {
            for (const auto& rec : *an->array) {
                const Value* rn = optGet(rec, "name"); const Value* rt = optGet(rec, "type");
                const Value* ttl = optGet(rec, "TTL"); const Value* data = optGet(rec, "data");
                int t = rt ? static_cast<int>(rt->number) : 0;
                auto tn = typeNames.find(t);
                answers.push_back(M({{"name", S(rn ? rn->toDisplayString() : "")}, {"type", S(tn != typeNames.end() ? tn->second : std::to_string(t))},
                                     {"ttl", ttl ? *ttl : N(0)}, {"data", S(data ? data->toDisplayString() : "")}}));
            }
        }
        const Value* status = optGet(*js, "Status");
        return M({{"ok", B(true)}, {"name", S(name)}, {"type", S(type)}, {"status", status ? *status : N(0)},
                  {"answers", newArray(std::move(answers))}, {"error", S("")}});
    };

    // ---- TCP / DNS / system (مقابس حقيقية)
    natives["net.resolve"] = [](Args& a, int line) -> Value {
        need("net.resolve", a, 1, 1, line);
        std::string host = str(a[0], "net.resolve", line);
#ifdef RIN_NET_SOCKETS
        if (!validHostForSocket(host)) return M({{"ok", B(false)}, {"host", S(host)}, {"addresses", newArray()}, {"error", S("اسم مضيف غير صالح")}});
        double t0 = nowMs();
        addrinfo hints; std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
        if (rc != 0) return M({{"ok", B(false)}, {"host", S(host)}, {"addresses", newArray()}, {"ipv4", newArray()}, {"ipv6", newArray()},
                               {"ms", N(round2(nowMs() - t0))}, {"error", S(std::string("تعذّر حلّ الاسم: ") + gai_strerror(rc))}});
        ArrayData all, v4, v6;
        std::set<std::string> seen;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            std::string ip = numericHost(ai->ai_addr, ai->ai_addrlen);
            if (ip.empty() || !seen.insert(ip).second) continue;
            all.push_back(S(ip));
            (ai->ai_family == AF_INET ? v4 : v6).push_back(S(ip));
        }
        freeaddrinfo(res);
        return M({{"ok", B(true)}, {"host", S(host)}, {"addresses", newArray(std::move(all))}, {"ipv4", newArray(std::move(v4))},
                  {"ipv6", newArray(std::move(v6))}, {"ms", N(round2(nowMs() - t0))}, {"error", S("")}});
#else
        (void)host; return noSockets("net.resolve");
#endif
    };
    natives["net.reverseDns"] = [](Args& a, int line) -> Value {
        need("net.reverseDns", a, 1, 1, line);
        std::string ip = str(a[0], "net.reverseDns", line);
#ifdef RIN_NET_SOCKETS
        char host[NI_MAXHOST];
        int rc = -1;
        if (isValidIPv4(ip)) {
            sockaddr_in sa; std::memset(&sa, 0, sizeof(sa)); sa.sin_family = AF_INET;
            if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) return Value::nil();
            rc = getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof(sa), host, sizeof(host), nullptr, 0, NI_NAMEREQD);
        } else if (isValidIPv6(ip)) {
            sockaddr_in6 sa; std::memset(&sa, 0, sizeof(sa)); sa.sin6_family = AF_INET6;
            if (inet_pton(AF_INET6, ip.c_str(), &sa.sin6_addr) != 1) return Value::nil();
            rc = getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof(sa), host, sizeof(host), nullptr, 0, NI_NAMEREQD);
        } else return Value::nil();
        return rc == 0 ? S(host) : Value::nil();
#else
        (void)ip; return Value::nil();
#endif
    };
    // net.tcpCheck(host, port, timeoutMs=3000) — هل المنفذ مفتوح؟ (اتصال TCP واحد فعلي إلى منفذ واحد)
    natives["net.tcpCheck"] = [](Args& a, int line) -> Value {
        need("net.tcpCheck", a, 2, 3, line);
        std::string host = str(a[0], "net.tcpCheck", line);
        int port = 0;
        if (!parsePort(a[1], port)) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'net.tcpCheck': المنفذ يجب أن يكون بين 1 و65535");
        int timeout = a.size() > 2 ? clampTimeout(num(a[2], "net.tcpCheck", line)) : 3000;
#ifdef RIN_NET_SOCKETS
        if (!validHostForSocket(host)) return M({{"ok", B(false)}, {"host", S(host)}, {"port", N(port)}, {"error", S("اسم مضيف غير صالح")}});
        Conn c = tcpConnect(host, port, timeout);
        if (c.fd >= 0) ::close(c.fd);
        return M({{"ok", B(c.fd >= 0)}, {"host", S(host)}, {"ip", S(c.ip)}, {"port", N(port)}, {"ms", N(round2(c.ms))}, {"error", S(c.error)}});
#else
        (void)host; (void)timeout; return noSockets("net.tcpCheck");
#endif
    };
    // net.tcpPing(host, port=443, count=4, timeoutMs=2000) — "ping" عبر TCP (ICMP يحتاج صلاحيات root)
    natives["net.tcpPing"] = [](Args& a, int line) -> Value {
        need("net.tcpPing", a, 1, 4, line);
        std::string host = str(a[0], "net.tcpPing", line);
        int port = 443;
        if (a.size() > 1 && !parsePort(a[1], port)) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'net.tcpPing': المنفذ يجب أن يكون بين 1 و65535");
        int count = a.size() > 2 ? std::max(1, std::min(20, static_cast<int>(num(a[2], "net.tcpPing", line)))) : 4;
        int timeout = a.size() > 3 ? clampTimeout(num(a[3], "net.tcpPing", line)) : 2000;
#ifdef RIN_NET_SOCKETS
        if (!validHostForSocket(host)) return M({{"ok", B(false)}, {"host", S(host)}, {"error", S("اسم مضيف غير صالح")}});
        std::vector<double> times; std::string ip, lastErr;
        for (int i = 0; i < count; ++i) {
            Conn c = tcpConnect(host, port, timeout);
            if (c.fd >= 0) { ::close(c.fd); times.push_back(c.ms); ip = c.ip; } else lastErr = c.error;
            if (i + 1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        double mn = 0, mx = 0, avg = 0;
        if (!times.empty()) {
            mn = *std::min_element(times.begin(), times.end()); mx = *std::max_element(times.begin(), times.end());
            double sum = 0; for (double t : times) sum += t; avg = sum / times.size();
        }
        double loss = 100.0 * (count - static_cast<double>(times.size())) / count;
        return M({{"ok", B(!times.empty())}, {"host", S(host)}, {"ip", S(ip)}, {"port", N(port)}, {"sent", N(count)},
                  {"received", N(static_cast<double>(times.size()))}, {"lossPct", N(round2(loss))}, {"minMs", N(round2(mn))},
                  {"avgMs", N(round2(avg))}, {"maxMs", N(round2(mx))}, {"error", S(times.empty() ? lastErr : "")}});
#else
        (void)host; (void)count; (void)timeout; return noSockets("net.tcpPing");
#endif
    };
    // net.tcpSend(host, port, data, timeoutMs=5000) — يرسل بايتات خام ويقرأ الرد (حتى 64KB)
    natives["net.tcpSend"] = [](Args& a, int line) -> Value {
        need("net.tcpSend", a, 3, 4, line);
        std::string host = str(a[0], "net.tcpSend", line);
        int port = 0;
        if (!parsePort(a[1], port)) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'net.tcpSend': المنفذ يجب أن يكون بين 1 و65535");
        std::string data = str(a[2], "net.tcpSend", line);
        if (data.size() > 65536) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'net.tcpSend': البيانات أكبر من 64KB");
        int timeout = a.size() > 3 ? clampTimeout(num(a[3], "net.tcpSend", line)) : 5000;
#ifdef RIN_NET_SOCKETS
        if (!validHostForSocket(host)) return M({{"ok", B(false)}, {"response", S("")}, {"error", S("اسم مضيف غير صالح")}});
        std::string resp;
        Conn c = tcpExchange(host, port, data, timeout, 65536, resp);
        return M({{"ok", B(c.error.empty())}, {"host", S(host)}, {"ip", S(c.ip)}, {"port", N(port)}, {"response", S(resp)},
                  {"bytes", N(static_cast<double>(resp.size()))}, {"ms", N(round2(c.ms))}, {"error", S(c.error)}});
#else
        (void)host; (void)data; (void)timeout; return noSockets("net.tcpSend");
#endif
    };
    // net.whois(domain) — استعلام WHOIS حقيقي (منفذ 43): IANA ثم خادوم السجل المُحال إليه
    natives["net.whois"] = [](Args& a, int line) -> Value {
        need("net.whois", a, 1, 1, line);
        std::string domain = lowerAscii(str(a[0], "net.whois", line));
        if (!isValidDomainName(domain)) return M({{"ok", B(false)}, {"server", S("")}, {"text", S("")}, {"error", S("اسم نطاق غير صالح")}});
#ifdef RIN_NET_SOCKETS
        std::string resp;
        Conn c = tcpExchange("whois.iana.org", 43, domain + "\r\n", 8000, 65536, resp);
        if (!c.error.empty()) return M({{"ok", B(false)}, {"server", S("whois.iana.org")}, {"text", S("")}, {"error", S(c.error)}});
        std::string server = "whois.iana.org", text = resp;
        std::string lower = lowerAscii(resp);
        size_t p = lower.find("refer:");
        if (p == std::string::npos) p = lower.find("whois:");
        if (p != std::string::npos) {
            size_t e = resp.find('\n', p);
            std::string refer = trimStr(resp.substr(p + 6, e == std::string::npos ? std::string::npos : e - p - 6));
            if (validHostForSocket(refer) && !refer.empty()) {
                std::string resp2;
                Conn c2 = tcpExchange(refer, 43, domain + "\r\n", 8000, 65536, resp2);
                if (c2.error.empty() && !resp2.empty()) { server = refer; text = resp2; }
            }
        }
        return M({{"ok", B(true)}, {"server", S(server)}, {"text", S(text)}, {"error", S("")}});
#else
        return noSockets("net.whois");
#endif
    };
    natives["net.hostname"] = [](Args& a, int line) -> Value {
        need("net.hostname", a, 0, 0, line);
#ifdef RIN_NET_SOCKETS
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0) { buf[sizeof(buf) - 1] = 0; return S(buf); }
#endif
        return Value::nil();
    };
    // net.localIPs() -> [{name, address, family, loopback, private}]
    natives["net.localIPs"] = [](Args& a, int line) -> Value {
        need("net.localIPs", a, 0, 0, line);
        ArrayData out;
#ifdef RIN_NET_IFADDRS
        ifaddrs* ifa = nullptr;
        if (getifaddrs(&ifa) == 0) {
            for (ifaddrs* p = ifa; p; p = p->ifa_next) {
                if (!p->ifa_addr || !(p->ifa_flags & IFF_UP)) continue;
                int fam = p->ifa_addr->sa_family;
                if (fam != AF_INET && fam != AF_INET6) continue;
                std::string ip = numericHost(p->ifa_addr, fam == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
                size_t pct = ip.find('%'); if (pct != std::string::npos) ip = ip.substr(0, pct);
                if (ip.empty()) continue;
                out.push_back(M({{"name", S(p->ifa_name ? p->ifa_name : "")}, {"address", S(ip)}, {"family", S(fam == AF_INET ? "IPv4" : "IPv6")},
                                 {"loopback", B((p->ifa_flags & IFF_LOOPBACK) != 0)}, {"private", B(isPrivateIpImpl(ip))}}));
            }
            freeifaddrs(ifa);
        }
#endif
        return newArray(std::move(out));
    };
    // net.isOnline() — اتصال TCP فعلي بخوادم DNS عامة؛ وإلا طلب HTTP صغير
    natives["net.isOnline"] = [](Args& a, int line) -> Value {
        need("net.isOnline", a, 0, 0, line);
#ifdef RIN_NET_SOCKETS
        const std::pair<const char*, int> targets[] = {{"1.1.1.1", 443}, {"8.8.8.8", 53}, {"9.9.9.9", 443}};
        for (const auto& t : targets) {
            Conn c = tcpConnect(t.first, t.second, 2500);
            if (c.fd >= 0) { ::close(c.fd); return B(true); }
        }
        return B(false);
#else
        Value r = fetchImpl("GET", "https://www.gstatic.com/generate_204", {}, Value::nil(), 5000, 0, line);
        const Value* ok = optGet(r, "ok");
        return B(ok && ok->boolean);
#endif
    };

    // ---- مساعدات بلا اتصال
    natives["net.urlEncode"] = [](Args& a, int line) -> Value {
        need("net.urlEncode", a, 1, 1, line);
        return S(urlEncodeComponent(str(a[0], "net.urlEncode", line)));
    };
    natives["net.urlDecode"] = [](Args& a, int line) -> Value {
        need("net.urlDecode", a, 1, 1, line);
        return S(urlDecode(str(a[0], "net.urlDecode", line)));
    };
    natives["net.joinUrl"] = [](Args& a, int line) -> Value {
        need("net.joinUrl", a, 2, 2, line);
        return S(joinUrlImpl(str(a[0], "net.joinUrl", line), str(a[1], "net.joinUrl", line)));
    };
    natives["net.isValidUrl"] = [](Args& a, int line) -> Value { need("net.isValidUrl", a, 1, 1, line); return B(isHttpUrl(str(a[0], "net.isValidUrl", line))); };
    natives["net.isValidDomain"] = [](Args& a, int line) -> Value { need("net.isValidDomain", a, 1, 1, line); return B(isValidDomainName(lowerAscii(str(a[0], "net.isValidDomain", line)))); };
    natives["net.isValidEmail"] = [](Args& a, int line) -> Value {
        need("net.isValidEmail", a, 1, 1, line);
        std::string e = str(a[0], "net.isValidEmail", line);
        size_t at = e.find('@');
        if (at == std::string::npos || at != e.rfind('@') || at == 0 || at > 64 || e.size() > 254) return B(false);
        for (size_t i = 0; i < at; ++i) {
            unsigned char c = static_cast<unsigned char>(e[i]);
            if (!(std::isalnum(c) || std::strchr("._%+-", c))) return B(false);
        }
        if (e[0] == '.' || e[at - 1] == '.' || e.find("..") != std::string::npos) return B(false);
        return B(isValidDomainName(lowerAscii(e.substr(at + 1))));
    };
    natives["net.isValidPort"] = [](Args& a, int line) -> Value { need("net.isValidPort", a, 1, 1, line); int p; return B(parsePort(a[0], p)); };
    natives["net.isValidMac"] = [](Args& a, int line) -> Value {
        need("net.isValidMac", a, 1, 1, line);
        std::string m = str(a[0], "net.isValidMac", line);
        if (m.size() != 17) return B(false);
        for (size_t i = 0; i < 17; ++i) {
            if (i % 3 == 2) { if (m[i] != ':' && m[i] != '-') return B(false); }
            else if (!std::isxdigit(static_cast<unsigned char>(m[i]))) return B(false);
        }
        return B(true);
    };
    natives["net.ipVersion"] = [](Args& a, int line) -> Value {
        need("net.ipVersion", a, 1, 1, line);
        std::string ip = str(a[0], "net.ipVersion", line);
        if (isValidIPv4(ip)) return N(4);
        if (isValidIPv6(ip)) return N(6);
        return Value::nil();
    };
    natives["net.domainOf"] = [](Args& a, int line) -> Value {
        need("net.domainOf", a, 1, 1, line);
        UrlParts p = parseUrlImpl(str(a[0], "net.domainOf", line));
        return p.ok && !p.host.empty() ? S(lowerAscii(p.host)) : Value::nil();
    };
    // net.cidrInfo("192.168.1.0/24") -> {network, broadcast, netmask, first, last, prefix, total, hosts}
    natives["net.cidrInfo"] = [](Args& a, int line) -> Value {
        need("net.cidrInfo", a, 1, 1, line);
        uint32_t ip; int prefix;
        if (!parseCidr(str(a[0], "net.cidrInfo", line), ip, prefix)) return Value::nil();
        uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
        uint32_t net = ip & mask, bc = net | ~mask;
        double total = std::pow(2.0, 32 - prefix);
        double hosts = prefix >= 31 ? total : total - 2;
        uint32_t first = prefix >= 31 ? net : net + 1, last = prefix >= 31 ? bc : bc - 1;
        return M({{"network", S(ipStr(net))}, {"broadcast", S(ipStr(bc))}, {"netmask", S(ipStr(mask))}, {"first", S(ipStr(first))},
                  {"last", S(ipStr(last))}, {"prefix", N(prefix)}, {"total", N(total)}, {"hosts", N(hosts)}});
    };
    natives["net.httpStatusText"] = [](Args& a, int line) -> Value {
        need("net.httpStatusText", a, 1, 1, line);
        std::string t = statusTextOf(static_cast<long>(num(a[0], "net.httpStatusText", line)));
        return t.empty() ? Value::nil() : S(t);
    };
    static const std::vector<std::pair<int, const char*>> kPorts = {
        {20, "ftp-data"}, {21, "ftp"}, {22, "ssh"}, {23, "telnet"}, {25, "smtp"}, {53, "dns"}, {67, "dhcp"}, {80, "http"},
        {110, "pop3"}, {123, "ntp"}, {143, "imap"}, {161, "snmp"}, {389, "ldap"}, {443, "https"}, {465, "smtps"}, {587, "submission"},
        {636, "ldaps"}, {993, "imaps"}, {995, "pop3s"}, {1433, "mssql"}, {3306, "mysql"}, {3389, "rdp"}, {5432, "postgres"},
        {5672, "amqp"}, {6379, "redis"}, {8080, "http-alt"}, {8443, "https-alt"}, {9200, "elasticsearch"}, {27017, "mongodb"}};
    natives["net.portService"] = [](Args& a, int line) -> Value {
        need("net.portService", a, 1, 1, line);
        int p = static_cast<int>(num(a[0], "net.portService", line));
        for (auto& kv : kPorts) if (kv.first == p) return S(kv.second);
        return Value::nil();
    };
    natives["net.servicePort"] = [](Args& a, int line) -> Value {
        need("net.servicePort", a, 1, 1, line);
        std::string s = lowerAscii(str(a[0], "net.servicePort", line));
        for (auto& kv : kPorts) if (s == kv.second) return N(kv.first);
        return Value::nil();
    };
    natives["net.parseHeaders"] = [](Args& a, int line) -> Value {
        need("net.parseHeaders", a, 1, 1, line);
        http::HeaderList h;
        std::string text = str(a[0], "net.parseHeaders", line), l;
        std::istringstream in(text);
        while (std::getline(in, l)) {
            while (!l.empty() && (l.back() == '\r')) l.pop_back();
            size_t c = l.find(':');
            if (c == std::string::npos || c == 0) continue;
            h.push_back({trimStr(l.substr(0, c)), trimStr(l.substr(c + 1))});
        }
        return headersToMap(h);
    };
    natives["net.basicAuth"] = [](Args& a, int line) -> Value {
        need("net.basicAuth", a, 2, 2, line);
        return S("Basic " + b64(str(a[0], "net.basicAuth", line) + ":" + str(a[1], "net.basicAuth", line)));
    };

    // ======================================================== 2) container.* extras
    auto sortedNames3 = [this]() {
        std::vector<std::string> names;
        names.reserve(containers.size());
        for (const auto& kv : containers) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return names;
    };
    auto kindOf3 = [this](const std::string& name) -> std::string {
        Args args{S(name)};
        Value k = natives["container.kind"](args, 0);
        return k.type == Value::Type::STRING ? k.str : std::string();
    };
    // أسماء حاويات نوع معيّن ("" أو "*" = الكل)
    auto namesOfKind = [this, sortedNames3, kindOf3](const std::string& kind) {
        std::vector<std::string> out;
        for (const auto& n : sortedNames3()) if (kind.empty() || kind == "*" || kindOf3(n) == kind) out.push_back(n);
        return out;
    };
    auto publicFields = [](const EnvPtr& env) {
        std::vector<std::pair<std::string, Value>> out;
        for (const auto& kv : env->values)
            if (kv.second.type != Value::Type::FUNCTION && !isInternalField(kv.first)) out.push_back(kv);
        std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        return out;
    };
    auto isLocked = [](const EnvPtr& env) {
        auto it = env->values.find("__locked");
        return it != env->values.end() && it->second.isTruthy();
    };
    auto guardWrite = [isLocked](const EnvPtr& env, const std::string& fn, const std::string& name, int line) {
        if (isLocked(env)) throw diagErr(diag::Code::E0035_RuntimeError, line, "'" + fn + "': الحاوية '" + name + "' مقفلة (container.unlock أولاً)");
    };

    natives["container.names"] = [namesOfKind](Args& a, int line) -> Value {
        need("container.names", a, 0, 1, line);
        ArrayData out;
        for (const auto& n : namesOfKind(a.empty() ? "" : str(a[0], "container.names", line))) out.push_back(S(n));
        return newArray(std::move(out));
    };
    // container.remove(name) -> true إن حُذفت (ترفض المقفلة)
    natives["container.remove"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.remove", a, 1, 1, line);
        std::string name = str(a[0], "container.remove", line);
        auto it = containers.find(name);
        if (it == containers.end()) return B(false);
        guardWrite(it->second, "container.remove", name, line);
        containers.erase(it);
        tableForget(name); // جدول: صفوفه ونمطه
        containerKinds.erase(name); containerCustomKind.erase(name); containerLifecycle.erase(name);
        containerStateNames.erase(name); containerSlots.erase(name); eventHandlers.erase(name);
        auto par = containerParent.find(name);
        if (par != containerParent.end()) {
            auto ch = containerChildren.find(par->second);
            if (ch != containerChildren.end()) ch->second.erase(std::remove(ch->second.begin(), ch->second.end(), name), ch->second.end());
            containerParent.erase(par);
        }
        auto kids = containerChildren.find(name);
        if (kids != containerChildren.end()) { for (auto& c : kids->second) containerParent.erase(c); containerChildren.erase(kids); }
        for (auto& g : groupMembers) g.second.erase(std::remove(g.second.begin(), g.second.end(), name), g.second.end());
        for (auto& v : volumeMembers) v.second.erase(std::remove(v.second.begin(), v.second.end(), name), v.second.end());
        containerStack.erase(std::remove(containerStack.begin(), containerStack.end(), name), containerStack.end());
        return B(true);
    };
    natives["container.removeAll"] = [this, namesOfKind](Args& a, int line) -> Value {
        need("container.removeAll", a, 1, 1, line);
        std::string kind = str(a[0], "container.removeAll", line);
        if (kind.empty() || kind == "*") throw diagErr(diag::Code::E0007_InvalidArguments, line, "'container.removeAll' يتطلب نوعاً محدداً (لا يقبل '*')");
        int removed = 0;
        for (const auto& n : namesOfKind(kind)) {
            Args ra{S(n)};
            try { if (natives["container.remove"](ra, line).isTruthy()) ++removed; } catch (const RinError&) { /* مقفلة: تُتخطّى */ }
        }
        return N(removed);
    };
    natives["container.copyField"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.copyField", a, 3, 4, line);
        std::string src = str(a[0], "container.copyField", line), field = str(a[1], "container.copyField", line);
        std::string dst = str(a[2], "container.copyField", line);
        std::string as = a.size() > 3 ? str(a[3], "container.copyField", line) : field;
        auto s = containers.find(src); auto d = containers.find(dst);
        if (s == containers.end() || d == containers.end()) return B(false);
        auto f = s->second->values.find(field);
        if (f == s->second->values.end() || f->second.type == Value::Type::FUNCTION) return B(false);
        guardWrite(d->second, "container.copyField", dst, line);
        d->second->values[as] = deepCopyValue(f->second);
        return B(true);
    };
    natives["container.moveField"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.moveField", a, 3, 3, line);
        std::string src = str(a[0], "container.moveField", line), field = str(a[1], "container.moveField", line);
        std::string dst = str(a[2], "container.moveField", line);
        auto s = containers.find(src); auto d = containers.find(dst);
        if (s == containers.end() || d == containers.end() || src == dst) return B(false);
        auto f = s->second->values.find(field);
        if (f == s->second->values.end() || f->second.type == Value::Type::FUNCTION) return B(false);
        guardWrite(s->second, "container.moveField", src, line);
        guardWrite(d->second, "container.moveField", dst, line);
        d->second->values[field] = f->second;
        s->second->values.erase(f);
        return B(true);
    };
    // container.getPath(name, "a.b.0.c", default?)
    natives["container.getPath"] = [this](Args& a, int line) -> Value {
        need("container.getPath", a, 2, 3, line);
        Value dflt = a.size() > 2 ? a[2] : Value::nil();
        auto it = containers.find(str(a[0], "container.getPath", line));
        if (it == containers.end()) return dflt;
        auto parts = splitPath(str(a[1], "container.getPath", line));
        if (parts.empty()) return dflt;
        auto f = it->second->values.find(parts[0]);
        if (f == it->second->values.end()) return dflt;
        const Value* v = getIn(f->second, parts, 1);
        return v ? *v : dflt;
    };
    natives["container.setPath"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.setPath", a, 3, 3, line);
        std::string name = str(a[0], "container.setPath", line);
        auto it = containers.find(name);
        if (it == containers.end()) return B(false);
        auto parts = splitPath(str(a[1], "container.setPath", line));
        if (parts.empty() || isInternalField(parts[0])) return B(false);
        guardWrite(it->second, "container.setPath", name, line);
        setIn(it->second->values[parts[0]], parts, 1, a[2]);
        return B(true);
    };
    natives["container.setDefault"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.setDefault", a, 3, 3, line);
        std::string name = str(a[0], "container.setDefault", line), field = str(a[1], "container.setDefault", line);
        auto it = containers.find(name);
        if (it == containers.end()) return Value::nil();
        auto f = it->second->values.find(field);
        if (f != it->second->values.end()) return f->second;
        guardWrite(it->second, "container.setDefault", name, line);
        it->second->values[field] = a[2];
        return a[2];
    };
    // container.defaults(name, {field: value,...}) -> عدد الحقول التي أُضيفت (الناقصة فقط)
    natives["container.defaults"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.defaults", a, 2, 2, line);
        std::string name = str(a[0], "container.defaults", line);
        auto it = containers.find(name);
        if (it == containers.end()) return Value::nil();
        const MapData& m = mp(a[1], "container.defaults", line);
        guardWrite(it->second, "container.defaults", name, line);
        int added = 0;
        for (const auto& kv : m) {
            std::string k = kv.first.toDisplayString();
            if (isInternalField(k) || it->second->values.count(k)) continue;
            it->second->values[k] = deepCopyValue(kv.second);
            ++added;
        }
        return N(added);
    };
    natives["container.decr"] = [this](Args& a, int line) -> Value {
        need("container.decr", a, 2, 3, line);
        Args ia{a[0], a[1], N(-(a.size() > 2 ? num(a[2], "container.decr", line) : 1.0))};
        return natives["container.incr"](ia, line);
    };
    natives["container.toggle"] = [this, guardWrite](Args& a, int line) -> Value {
        need("container.toggle", a, 2, 2, line);
        std::string name = str(a[0], "container.toggle", line), field = str(a[1], "container.toggle", line);
        auto it = containers.find(name);
        if (it == containers.end()) return Value::nil();
        guardWrite(it->second, "container.toggle", name, line);
        auto f = it->second->values.find(field);
        bool cur = (f != it->second->values.end() && f->second.isTruthy());
        it->second->values[field] = B(!cur);
        return B(!cur);
    };
    // container.push(name, field, value) -> الطول الجديد (ينشئ المصفوفة إن غابت)
    natives["container.push"] = [this, guardWrite](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.push", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.push", a, 3, 3, line);
        std::string name = str(a[0], "container.push", line), field = str(a[1], "container.push", line);
        auto it = containers.find(name);
        if (it == containers.end()) return Value::nil();
        guardWrite(it->second, "container.push", name, line);
        Value& slot = it->second->values[field];
        if (slot.type == Value::Type::NIL) slot = newArray();
        if (slot.type != Value::Type::ARRAY || !slot.array)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'container.push': الحقل '" + field + "' ليس مصفوفة (" + slot.typeName() + ")");
        slot.array->push_back(a[2]);
        return N(static_cast<double>(slot.array->size()));
    };
    natives["container.pop"] = [this, guardWrite](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.pop", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.pop", a, 2, 2, line);
        std::string name = str(a[0], "container.pop", line), field = str(a[1], "container.pop", line);
        auto it = containers.find(name);
        if (it == containers.end()) return Value::nil();
        auto f = it->second->values.find(field);
        if (f == it->second->values.end() || f->second.type != Value::Type::ARRAY || !f->second.array || f->second.array->empty()) return Value::nil();
        guardWrite(it->second, "container.pop", name, line);
        Value v = f->second.array->back();
        f->second.array->pop_back();
        return v;
    };
    // container.contains(name, field, x) — عنصر في مصفوفة / جزء نص / مفتاح في قاموس
    natives["container.contains"] = [this](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.contains", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.contains", a, 3, 3, line);
        auto it = containers.find(str(a[0], "container.contains", line));
        if (it == containers.end()) return B(false);
        auto f = it->second->values.find(str(a[1], "container.contains", line));
        if (f == it->second->values.end()) return B(false);
        const Value& v = f->second;
        if (v.type == Value::Type::ARRAY && v.array) { for (auto& e : *v.array) if (valuesEqual(e, a[2])) return B(true); return B(false); }
        if (v.type == Value::Type::STRING && a[2].type == Value::Type::STRING) return B(v.str.find(a[2].str) != std::string::npos);
        if (v.type == Value::Type::MAP && v.map) { for (auto& kv : *v.map) if (valuesEqual(kv.first, a[2])) return B(true); }
        return B(false);
    };
    natives["container.fieldsOfType"] = [this, publicFields](Args& a, int line) -> Value {
        need("container.fieldsOfType", a, 2, 2, line);
        auto it = containers.find(str(a[0], "container.fieldsOfType", line));
        if (it == containers.end()) return Value::nil();
        std::string want = lowerAscii(str(a[1], "container.fieldsOfType", line));
        ArrayData out;
        for (auto& f : publicFields(it->second)) if (lowerAscii(f.second.typeName()) == want) out.push_back(S(f.first));
        return newArray(std::move(out));
    };
    natives["container.entries"] = [this, publicFields](Args& a, int line) -> Value {
        need("container.entries", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.entries", line));
        if (it == containers.end()) return Value::nil();
        ArrayData out;
        for (auto& f : publicFields(it->second)) { ArrayData pair; pair.push_back(S(f.first)); pair.push_back(f.second); out.push_back(newArray(std::move(pair))); }
        return newArray(std::move(out));
    };
    natives["container.valuesOf"] = [this, publicFields](Args& a, int line) -> Value {
        need("container.valuesOf", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.valuesOf", line));
        if (it == containers.end()) return Value::nil();
        ArrayData out;
        for (auto& f : publicFields(it->second)) out.push_back(f.second);
        return newArray(std::move(out));
    };
    natives["container.requireFields"] = [this](Args& a, int line) -> Value {
        need("container.requireFields", a, 2, 2, line);
        auto it = containers.find(str(a[0], "container.requireFields", line));
        if (it == containers.end()) return Value::nil();
        ArrayData missing;
        for (const auto& f : arr(a[1], "container.requireFields", line)) {
            std::string k = f.toDisplayString();
            auto v = it->second->values.find(k);
            if (v == it->second->values.end() || v->second.type == Value::Type::NIL) missing.push_back(S(k));
        }
        return newArray(std::move(missing));
    };
    natives["container.equals"] = [this, publicFields](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.equals", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.equals", a, 2, 2, line);
        auto x = containers.find(str(a[0], "container.equals", line)), y = containers.find(str(a[1], "container.equals", line));
        if (x == containers.end() || y == containers.end()) return B(false);
        auto fx = publicFields(x->second), fy = publicFields(y->second);
        if (fx.size() != fy.size()) return B(false);
        for (size_t i = 0; i < fx.size(); ++i) if (fx[i].first != fy[i].first || !valuesEqual(fx[i].second, fy[i].second)) return B(false);
        return B(true);
    };
    natives["container.mergeFrom"] = [this, publicFields, guardWrite](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.mergeFrom", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.mergeFrom", a, 2, 3, line);
        std::string dn = str(a[0], "container.mergeFrom", line), sn = str(a[1], "container.mergeFrom", line);
        bool overwrite = a.size() > 2 ? a[2].isTruthy() : true;
        auto d = containers.find(dn), s = containers.find(sn);
        if (d == containers.end() || s == containers.end() || dn == sn) return Value::nil();
        guardWrite(d->second, "container.mergeFrom", dn, line);
        int n = 0;
        for (auto& f : publicFields(s->second)) {
            if (!overwrite && d->second->values.count(f.first)) continue;
            d->second->values[f.first] = deepCopyValue(f.second);
            ++n;
        }
        return N(n);
    };
    // container.checksum(name) -> SHA-256 لمحتوى الحقول العامة (مرتّبة)؛ مفيد لكشف التغيير
    natives["container.checksum"] = [this, publicFields](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.checksum", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.checksum", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.checksum", line));
        if (it == containers.end()) return Value::nil();
        std::string blob;
        for (auto& f : publicFields(it->second)) blob += f.first + "=" + json::encode(f.second) + "\n";
        return S(sha256Hex(blob));
    };

    // ---- تجميعات على نوع كامل
    // يجمع قيم حقل من كل حاويات النوع (القيم الموجودة فقط)
    auto collect = [this, namesOfKind](const std::string& kind, const std::string& field) {
        std::vector<Value> out;
        for (const auto& n : namesOfKind(kind)) {
            auto it = containers.find(n);
            if (it == containers.end()) continue;
            auto f = it->second->values.find(field);
            if (f != it->second->values.end() && f->second.type != Value::Type::FUNCTION) out.push_back(f->second);
        }
        return out;
    };
    natives["container.avg"] = [this, collect](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.avg", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.avg", a, 2, 2, line);
        double sum = 0; int n = 0;
        for (auto& v : collect(str(a[0], "container.avg", line), str(a[1], "container.avg", line))) if (v.type == Value::Type::NUMBER) { sum += v.number; ++n; }
        return n ? N(sum / n) : Value::nil();
    };
    natives["container.min"] = [this, collect](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.min", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.min", a, 2, 2, line);
        bool any = false; double m = 0;
        for (auto& v : collect(str(a[0], "container.min", line), str(a[1], "container.min", line))) if (v.type == Value::Type::NUMBER && (!any || v.number < m)) { m = v.number; any = true; }
        return any ? N(m) : Value::nil();
    };
    natives["container.max"] = [this, collect](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.max", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.max", a, 2, 2, line);
        bool any = false; double m = 0;
        for (auto& v : collect(str(a[0], "container.max", line), str(a[1], "container.max", line))) if (v.type == Value::Type::NUMBER && (!any || v.number > m)) { m = v.number; any = true; }
        return any ? N(m) : Value::nil();
    };
    natives["container.distinct"] = [this, collect](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.distinct", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.distinct", a, 2, 2, line);
        ArrayData out;
        for (auto& v : collect(str(a[0], "container.distinct", line), str(a[1], "container.distinct", line))) {
            bool dup = false;
            for (auto& e : out) if (valuesEqual(e, v)) { dup = true; break; }
            if (!dup) out.push_back(v);
        }
        return newArray(std::move(out));
    };
    natives["container.countBy"] = [this, collect](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.countBy", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.countBy", a, 2, 2, line);
        MapData out;
        for (auto& v : collect(str(a[0], "container.countBy", line), str(a[1], "container.countBy", line))) {
            bool found = false;
            for (auto& kv : out) if (valuesEqual(kv.first, v)) { kv.second.number += 1; found = true; break; }
            if (!found) out.push_back({v, N(1)});
        }
        return newMap(std::move(out));
    };
    // container.sortBy(kind, field, desc=false) -> أسماء مرتّبة (من لا يملك الحقل يأتي أخيراً)
    natives["container.sortBy"] = [this, namesOfKind](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.sortBy", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.sortBy", a, 2, 3, line);
        std::string field = str(a[1], "container.sortBy", line);
        bool desc = a.size() > 2 && a[2].isTruthy();
        std::vector<std::pair<std::string, Value>> rows;
        std::vector<std::string> missing;
        for (const auto& n : namesOfKind(str(a[0], "container.sortBy", line))) {
            auto it = containers.find(n);
            if (it == containers.end()) continue;
            auto f = it->second->values.find(field);
            if (f != it->second->values.end() && f->second.type != Value::Type::FUNCTION) rows.push_back({n, f->second}); else missing.push_back(n);
        }
        std::stable_sort(rows.begin(), rows.end(), [desc](const auto& x, const auto& y) { return desc ? lessVal(y.second, x.second) : lessVal(x.second, y.second); });
        ArrayData out;
        for (auto& r : rows) out.push_back(S(r.first));
        for (auto& m : missing) out.push_back(S(m));
        return newArray(std::move(out));
    };
    natives["container.top"] = [this](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.top", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.top", a, 3, 3, line);
        Args sa{a[0], a[1], B(true)};
        Value sorted = natives["container.sortBy"](sa, line);
        size_t n = static_cast<size_t>(std::max(0.0, num(a[2], "container.top", line)));
        ArrayData out;
        for (size_t i = 0; i < sorted.array->size() && i < n; ++i) out.push_back((*sorted.array)[i]);
        return newArray(std::move(out));
    };
    // container.search(text, kind?) — بحث نصي غير حسّاس للحالة في الأسماء وقيم الحقول
    natives["container.search"] = [this, namesOfKind, publicFields](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.search", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.search", a, 1, 2, line);
        std::string q = lowerAscii(str(a[0], "container.search", line));
        ArrayData out;
        for (const auto& n : namesOfKind(a.size() > 1 ? str(a[1], "container.search", line) : "")) {
            bool hit = lowerAscii(n).find(q) != std::string::npos;
            auto it = containers.find(n);
            if (!hit && it != containers.end())
                for (auto& f : publicFields(it->second)) if (lowerAscii(f.second.toDisplayString()).find(q) != std::string::npos) { hit = true; break; }
            if (hit) out.push_back(S(n));
        }
        return newArray(std::move(out));
    };
    // container.toRows(kind, fields?) -> [{name, field: value, ...}]
    auto rowsOf = [this, namesOfKind, publicFields](const std::string& kind, const Value* fields, const std::string& fn, int line,
                                                    std::vector<std::string>& cols) {
        std::vector<std::string> names = namesOfKind(kind);
        std::set<std::string> colSet;
        if (fields && fields->type == Value::Type::ARRAY && fields->array) for (auto& f : *fields->array) cols.push_back(f.toDisplayString());
        else if (fields && fields->type != Value::Type::NIL) throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' يتوقّع قائمة حقول (مصفوفة) أو nil");
        else {
            for (const auto& n : names) { auto it = containers.find(n); if (it != containers.end()) for (auto& f : publicFields(it->second)) colSet.insert(f.first); }
            cols.assign(colSet.begin(), colSet.end());
        }
        return names;
    };
    natives["container.toRows"] = [this, rowsOf](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.toRows", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.toRows", a, 1, 2, line);
        std::vector<std::string> cols;
        auto names = rowsOf(str(a[0], "container.toRows", line), a.size() > 1 ? &a[1] : nullptr, "container.toRows", line, cols);
        ArrayData out;
        for (const auto& n : names) {
            auto it = containers.find(n);
            if (it == containers.end()) continue;
            MapData row;
            row.push_back({S("name"), S(n)});
            for (const auto& c : cols) { auto f = it->second->values.find(c); row.push_back({S(c), f == it->second->values.end() ? Value::nil() : f->second}); }
            out.push_back(newMap(std::move(row)));
        }
        return newArray(std::move(out));
    };
    auto buildCsv = [this, rowsOf](const std::string& kind, const Value* fields, const std::string& fn, int line) {
        std::vector<std::string> cols;
        auto names = rowsOf(kind, fields, fn, line, cols);
        std::string out = "name";
        for (const auto& c : cols) out += "," + csvCell(S(c));
        out += "\n";
        for (const auto& n : names) {
            auto it = containers.find(n);
            if (it == containers.end()) continue;
            out += csvCell(S(n));
            for (const auto& c : cols) { auto f = it->second->values.find(c); out += "," + (f == it->second->values.end() ? std::string() : csvCell(f->second)); }
            out += "\n";
        }
        return out;
    };
    natives["container.toCsv"] = [this, buildCsv](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.toCsv", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.toCsv", a, 1, 2, line);
        return S(buildCsv(str(a[0], "container.toCsv", line), a.size() > 1 ? &a[1] : nullptr, "container.toCsv", line));
    };
    natives["container.exportCsv"] = [this, buildCsv](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.exportCsv", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.exportCsv", a, 2, 3, line);
        std::string csv = buildCsv(str(a[0], "container.exportCsv", line), a.size() > 2 ? &a[2] : nullptr, "container.exportCsv", line);
        writeRealFile(str(a[1], "container.exportCsv", line), csv, line, "container.exportCsv");
        return B(true);
    };
    // container.paginate(kind, page=1, size=10) -> {items, page, size, total, pages}
    natives["container.paginate"] = [this, namesOfKind](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.paginate", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.paginate", a, 1, 3, line);
        auto names = namesOfKind(str(a[0], "container.paginate", line));
        double page = a.size() > 1 ? num(a[1], "container.paginate", line) : 1;
        double size = a.size() > 2 ? num(a[2], "container.paginate", line) : 10;
        size = std::max(1.0, std::min(1000.0, std::floor(size)));
        page = std::max(1.0, std::floor(page));
        double total = static_cast<double>(names.size()), pages = std::max(1.0, std::ceil(total / size));
        ArrayData items;
        for (size_t i = static_cast<size_t>((page - 1) * size); i < names.size() && items.size() < static_cast<size_t>(size); ++i) items.push_back(S(names[i]));
        return M({{"items", newArray(std::move(items))}, {"page", N(page)}, {"size", N(size)}, {"total", N(total)}, {"pages", N(pages)}});
    };
    // container.summary() -> {total, byKind, locked, fields}
    natives["container.summary"] = [this, sortedNames3, kindOf3, isLocked, publicFields](Args& a, int line) -> Value {
        need("container.summary", a, 0, 0, line);
        MapData byKind;
        double locked = 0, fields = 0;
        for (const auto& n : sortedNames3()) {
            std::string k = kindOf3(n);
            bool f = false;
            for (auto& kv : byKind) if (kv.first.str == k) { kv.second.number += 1; f = true; break; }
            if (!f) byKind.push_back({S(k), N(1)});
            auto it = containers.find(n);
            if (it != containers.end()) { if (isLocked(it->second)) locked += 1; fields += static_cast<double>(publicFields(it->second).size()); }
        }
        return M({{"total", N(static_cast<double>(containers.size()))}, {"byKind", newMap(std::move(byKind))}, {"locked", N(locked)}, {"fields", N(fields)}});
    };
}

} // namespace rin
