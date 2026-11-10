// ============================================================================
//  rin_fetch.cpp — fetch(url, opts) وبقية عميل الروابط الحقيقي الكامل + url.* (docs/fetch.md)
// ----------------------------------------------------------------------------
//  يُضمَّن من rin_timer.cpp بعد تعريف TimerOps (داخل namespace rin) ولا يُترجَم وحده.
//  النقل: http::performRequestEx (curl بلا shell، http/https فقط). هنا: خيارات، ترميز الجسم، مصادقة، كوكيز، إعادة محاولة
//  (Retry-After)، كاش، تحويل الردّ إلى Value، وأدوات الروابط.
// ============================================================================

// ---------------------------------------------------------------- أدوات الروابط
namespace urlx {
inline std::string enc(const std::string& s, bool keepSlash = false) {
    static const char* hx = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (keepSlash && c == '/')) o += (char)c;
        else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
    }
    return o;
}
inline std::string dec(const std::string& s, bool plus = false) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) { o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else if (plus && s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}
struct Parts { std::string scheme, user, host, port, path, query, fragment; bool ok = false; };
inline Parts parse(const std::string& u) {
    Parts p; std::string r = u;
    size_t h = r.find('#'); if (h != std::string::npos) { p.fragment = r.substr(h + 1); r.erase(h); }
    size_t q = r.find('?'); if (q != std::string::npos) { p.query = r.substr(q + 1); r.erase(q); }
    size_t se = r.find("://");
    if (se == std::string::npos) { p.path = r; p.ok = false; return p; }
    p.scheme = r.substr(0, se); for (auto& c : p.scheme) c = (char)std::tolower((unsigned char)c);
    r = r.substr(se + 3);
    size_t sl = r.find('/'); std::string auth = sl == std::string::npos ? r : r.substr(0, sl); p.path = sl == std::string::npos ? "" : r.substr(sl);
    size_t at = auth.rfind('@'); if (at != std::string::npos) { p.user = auth.substr(0, at); auth = auth.substr(at + 1); }
    if (!auth.empty() && auth[0] == '[') { size_t b = auth.find(']'); p.host = auth.substr(0, b == std::string::npos ? auth.size() : b + 1); if (b != std::string::npos && b + 1 < auth.size() && auth[b + 1] == ':') p.port = auth.substr(b + 2); }
    else { size_t c = auth.rfind(':'); if (c != std::string::npos) { p.host = auth.substr(0, c); p.port = auth.substr(c + 1); } else p.host = auth; }
    for (auto& c : p.host) c = (char)std::tolower((unsigned char)c);
    p.ok = !p.scheme.empty() && !p.host.empty();
    return p;
}
inline std::string build(const Parts& p) {
    std::string o = p.scheme + "://"; if (!p.user.empty()) o += p.user + "@"; o += p.host; if (!p.port.empty()) o += ":" + p.port;
    o += p.path.empty() ? ((!p.query.empty() || !p.fragment.empty()) ? "/" : "") : (p.path[0] == '/' ? p.path : "/" + p.path);
    if (!p.query.empty()) o += "?" + p.query; if (!p.fragment.empty()) o += "#" + p.fragment; return o;
}
inline std::string normPath(const std::string& path) {
    std::vector<std::string> seg; size_t i = 0; const bool trail = !path.empty() && path.back() == '/';
    while (i <= path.size()) { size_t j = path.find('/', i); if (j == std::string::npos) j = path.size(); std::string s = path.substr(i, j - i);
        if (s == "..") { if (!seg.empty()) seg.pop_back(); } else if (!s.empty() && s != ".") seg.push_back(s); i = j + 1; }
    std::string o; for (auto& s : seg) o += "/" + s; if (o.empty()) o = "/"; else if (trail) o += "/"; return o;
}
inline std::string join(const std::string& base, const std::string& rel) {
    if (rel.find("://") != std::string::npos) return rel;
    Parts b = parse(base); if (!b.ok) return rel;
    if (rel.rfind("//", 0) == 0) return b.scheme + ":" + rel;
    Parts r; r.scheme = b.scheme; r.user = b.user; r.host = b.host; r.port = b.port;
    std::string rr = rel; size_t h = rr.find('#'); if (h != std::string::npos) { r.fragment = rr.substr(h + 1); rr.erase(h); }
    size_t q = rr.find('?'); if (q != std::string::npos) { r.query = rr.substr(q + 1); rr.erase(q); }
    if (rr.empty()) { r.path = b.path; if (r.query.empty() && q == std::string::npos) r.query = b.query; }
    else if (rr[0] == '/') r.path = normPath(rr);
    else { size_t sl = b.path.rfind('/'); std::string dir = sl == std::string::npos ? "/" : b.path.substr(0, sl + 1); r.path = normPath(dir + rr); }
    return build(r);
}
} // namespace urlx

// ---------------------------------------------------------------- خيارات fetch
// سياق يمرّره المسجِّل (عضو في Interpreter) لأن بعض الأعضاء خاصة: حلّ المسارات، المهلة الافتراضية، at، النوم.
struct FetchCtx {
    Interpreter* ip = nullptr; int defaultTimeoutMs = 15000;
    std::function<std::string(const std::string&, int)> resolvePath;
    std::function<double(const Value&, int, const std::string&)> atArg;
    std::function<void(double, int)> sleep;
};
struct FetchOpts {
    std::string method, url; bool methodGiven = false;
    http::HeaderList headers; std::string body; std::vector<http::RequestEx::Part> parts;
    double timeoutMs = 0, connectMs = 0, backoffMs = 0, backoffFactor = 1, cacheMs = -1; long retry = 0; std::vector<long> retryOn{408, 425, 429, 500, 502, 503, 504};
    bool follow = true; long maxRedirects = 5; std::string cookieJar, proxy; long long maxBytes = 0; bool strict = false, compressed = true;
    std::string into; Value then;
    bool hasSched = false; double delay = 0, interval = 0; long times = 1;
};

static std::string fetchStatusText(long s) {
    switch (s) {
        case 200: return "OK"; case 201: return "Created"; case 202: return "Accepted"; case 204: return "No Content"; case 206: return "Partial Content";
        case 301: return "Moved Permanently"; case 302: return "Found"; case 303: return "See Other"; case 304: return "Not Modified"; case 307: return "Temporary Redirect"; case 308: return "Permanent Redirect";
        case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
        case 408: return "Request Timeout"; case 409: return "Conflict"; case 410: return "Gone"; case 413: return "Payload Too Large"; case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Entity"; case 429: return "Too Many Requests"; case 500: return "Internal Server Error"; case 501: return "Not Implemented";
        case 502: return "Bad Gateway"; case 503: return "Service Unavailable"; case 504: return "Gateway Timeout"; default: return s == 0 ? "" : (s < 300 ? "Success" : s < 400 ? "Redirect" : s < 500 ? "Client Error" : "Server Error");
    }
}
static bool fetchHasHeader(const http::HeaderList& h, const char* name) {
    for (auto& kv : h) if (toUpperAscii(kv.first) == toUpperAscii(name)) return true; return false;
}
static std::unordered_map<std::string, std::string>& fetchJars() {
    static std::unordered_map<std::string, std::string> jars;
    struct Cleaner { std::unordered_map<std::string, std::string>* m; ~Cleaner() { for (auto& kv : *m) std::remove(kv.second.c_str()); } };
    static Cleaner cl{&jars};
    return jars;
}
static std::string fetchJarPath(const std::string& name) {
    auto& jars = fetchJars();
    auto it = jars.find(name); if (it != jars.end()) return it->second;
    std::string safe; for (char c : name) safe += std::isalnum((unsigned char)c) ? c : '_';
    std::string p = "/tmp/rin_cookies_" + std::to_string((long)getpid()) + "_" + safe; jars[name] = p; return p;
}
static long fetchClearJars(const std::string& only) {   // يحذف جرّة بعينها أو كل الجرار؛ يعيد العدد
    auto& jars = fetchJars(); long n = 0;
    for (auto it = jars.begin(); it != jars.end();) { if (only.empty() || it->first == only) { std::remove(it->second.c_str()); it = jars.erase(it); ++n; } else ++it; }
    return n;
}

// يحلّل الخيارات (map) إلى FetchOpts. fn = اسم الدالة لرسائل الخطأ؛ allowSched = هل after/every/... مسموحة.
static FetchOpts fetchParse(FetchCtx& I, const std::string& fn, const std::string& url, const Value& opts, int line) {
    using TO = TimerOps;
    FetchOpts o; o.url = url;
    if (opts.type != Value::Type::NIL && (opts.type != Value::Type::MAP || !opts.map)) throw TO::badArg(line, fn + ": options must be a map but got " + opts.typeName());
    Value vJson, vForm, vMulti, vBody, vQuery, vHeaders, vBearer, vBasic, vTimeout, vConnect, vRetry, vBackoff, vBFactor, vRetryOn, vCache, vFollow, vMaxRed, vUA, vCookies, vMaxBytes, vProxy, vInto, vThen, vAfter, vAt, vEvery, vTimes, vStrict, vMethod, vCompress;
    if (opts.type == Value::Type::MAP) for (auto& kv : *opts.map) {
        const std::string k = kv.first.toDisplayString(); const Value& v = kv.second;
        if (k == "method") vMethod = v; else if (k == "headers") vHeaders = v; else if (k == "query") vQuery = v; else if (k == "json") vJson = v;
        else if (k == "form") vForm = v; else if (k == "multipart") vMulti = v; else if (k == "body") vBody = v; else if (k == "bearer") vBearer = v;
        else if (k == "basic") vBasic = v; else if (k == "timeout") vTimeout = v; else if (k == "connectTimeout") vConnect = v; else if (k == "retry") vRetry = v;
        else if (k == "backoff") vBackoff = v; else if (k == "backoffFactor") vBFactor = v; else if (k == "retryOn") vRetryOn = v; else if (k == "cache") vCache = v;
        else if (k == "follow") vFollow = v; else if (k == "maxRedirects") vMaxRed = v; else if (k == "userAgent") vUA = v; else if (k == "cookies") vCookies = v;
        else if (k == "maxBytes") vMaxBytes = v; else if (k == "proxy") vProxy = v; else if (k == "into") vInto = v; else if (k == "then") vThen = v;
        else if (k == "after") vAfter = v; else if (k == "at") vAt = v; else if (k == "every") vEvery = v; else if (k == "times") vTimes = v;
        else if (k == "strict") vStrict = v; else if (k == "compressed") vCompress = v;
        else throw diagErr(diag::Code::E0007_InvalidArguments, line, fn + ": unknown option '" + k + "' (allowed: method, headers, query, json, form, multipart, body, bearer, basic, timeout, connectTimeout, retry, backoff, backoffFactor, retryOn, cache, follow, maxRedirects, userAgent, cookies, maxBytes, proxy, strict, compressed, into, then, after, at, every, times)");
    }
    auto isNil = [](const Value& v) { return v.type == Value::Type::NIL; };
    // الرابط + query
    { urlx::Parts p = urlx::parse(url);
      if (!p.ok || (p.scheme != "http" && p.scheme != "https")) throw TO::badArg(line, fn + ": only absolute http:// or https:// URLs are supported (got `" + url + "`)"); }
    if (!isNil(vQuery)) {
        if (vQuery.type != Value::Type::MAP || !vQuery.map) throw TO::badArg(line, fn + ": `query` must be a map");
        std::string qs;
        auto add = [&](const std::string& k, const Value& v) { if (v.type == Value::Type::NIL) return; if (!qs.empty()) qs += "&"; qs += urlx::enc(k) + "=" + urlx::enc(v.toDisplayString()); };
        for (auto& kv : *vQuery.map) { const std::string k = kv.first.toDisplayString(); if (kv.second.type == Value::Type::ARRAY && kv.second.array) for (auto& e : *kv.second.array) add(k, e); else add(k, kv.second); }
        if (!qs.empty()) { size_t h = o.url.find('#'); std::string frag = h == std::string::npos ? "" : o.url.substr(h); std::string base = o.url.substr(0, h);
            base += (base.find('?') == std::string::npos ? "?" : (base.back() == '?' || base.back() == '&' ? "" : "&")) + qs; o.url = base + frag; }
    }
    o.headers = headersFromValue(vHeaders, fn, line);
    // الجسم: واحد فقط
    int nb = (!isNil(vJson)) + (!isNil(vForm)) + (!isNil(vMulti)) + (!isNil(vBody));
    if (nb > 1) throw TO::badArg(line, fn + ": use only one of json / form / multipart / body");
    if (!isNil(vJson)) { o.body = json::encode(vJson); if (!fetchHasHeader(o.headers, "Content-Type")) o.headers.push_back({"Content-Type", "application/json"}); if (!fetchHasHeader(o.headers, "Accept")) o.headers.push_back({"Accept", "application/json"}); }
    else if (!isNil(vForm)) {
        if (vForm.type != Value::Type::MAP || !vForm.map) throw TO::badArg(line, fn + ": `form` must be a map");
        for (auto& kv : *vForm.map) { if (kv.second.type == Value::Type::NIL) continue; if (!o.body.empty()) o.body += "&"; o.body += urlx::enc(kv.first.toDisplayString()) + "=" + urlx::enc(kv.second.toDisplayString()); }
        if (!fetchHasHeader(o.headers, "Content-Type")) o.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    } else if (!isNil(vMulti)) {
        if (vMulti.type != Value::Type::MAP || !vMulti.map) throw TO::badArg(line, fn + ": `multipart` must be a map");
        for (auto& kv : *vMulti.map) {
            http::RequestEx::Part pt; pt.name = kv.first.toDisplayString();
            if (kv.second.type == Value::Type::MAP && kv.second.map) {
                for (auto& f : *kv.second.map) { const std::string fk = f.first.toDisplayString(); if (fk == "file" || fk == "path") pt.file = I.resolvePath(f.second.toDisplayString(), line); else if (fk == "filename") pt.filename = f.second.toDisplayString(); else if (fk == "type") pt.type = f.second.toDisplayString(); else if (fk == "value") pt.value = f.second.toDisplayString(); }
                if (pt.file.empty() && pt.value.empty()) throw TO::badArg(line, fn + ": multipart field `" + pt.name + "` needs `path` (or `file`) or `value`");
            } else pt.value = kv.second.toDisplayString();
            o.parts.push_back(pt);
        }
    } else if (!isNil(vBody)) o.body = bodyToString(vBody, o.headers);
    // الطريقة
    o.methodGiven = !isNil(vMethod);
    o.method = o.methodGiven ? toUpperAscii(vMethod.toDisplayString()) : ((nb > 0) ? "POST" : "GET");
    static const char* ok[] = {"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"}; bool mok = false; for (auto m : ok) if (o.method == m) mok = true;
    if (!mok) throw TO::badArg(line, fn + ": unsupported method `" + o.method + "` (GET HEAD POST PUT PATCH DELETE OPTIONS)");
    if (o.method == "GET" && nb > 0 && !o.methodGiven) o.method = "POST";
    // مصادقة
    if (!isNil(vBearer) && !fetchHasHeader(o.headers, "Authorization")) o.headers.push_back({"Authorization", "Bearer " + vBearer.toDisplayString()});
    if (!isNil(vBasic) && !fetchHasHeader(o.headers, "Authorization")) {
        std::string u, p;
        if (vBasic.type == Value::Type::ARRAY && vBasic.array && vBasic.array->size() == 2) { u = (*vBasic.array)[0].toDisplayString(); p = (*vBasic.array)[1].toDisplayString(); }
        else if (vBasic.type == Value::Type::MAP && vBasic.map) { for (auto& kv : *vBasic.map) { const std::string k = kv.first.toDisplayString(); if (k == "user") u = kv.second.toDisplayString(); else if (k == "pass" || k == "password") p = kv.second.toDisplayString(); } }
        else if (vBasic.type == Value::Type::STRING) { size_t c = vBasic.str.find(':'); u = vBasic.str.substr(0, c); if (c != std::string::npos) p = vBasic.str.substr(c + 1); }
        else throw TO::badArg(line, fn + ": `basic` must be [user, pass], {user, pass} or \"user:pass\"");
        o.headers.push_back({"Authorization", "Basic " + rinB64Encode(u + ":" + p)});
    }
    if (!fetchHasHeader(o.headers, "User-Agent")) o.headers.push_back({"User-Agent", isNil(vUA) ? std::string("Rin-fetch/1.0") : vUA.toDisplayString()});
    // أرقام
    if (!isNil(vTimeout)) o.timeoutMs = TO::dur(vTimeout, line, fn + " `timeout`", false);
    if (!isNil(vConnect)) o.connectMs = TO::dur(vConnect, line, fn + " `connectTimeout`", false);
    if (!isNil(vBackoff)) o.backoffMs = TO::dur(vBackoff, line, fn + " `backoff`", true);
    if (!isNil(vBFactor)) { if (vBFactor.type != Value::Type::NUMBER || vBFactor.number < 1 || vBFactor.number > 10) throw TO::badArg(line, fn + ": `backoffFactor` must be a number from 1 to 10"); o.backoffFactor = vBFactor.number; }
    if (!isNil(vRetry)) { if (vRetry.type != Value::Type::NUMBER || vRetry.number < 0 || vRetry.number != std::floor(vRetry.number) || vRetry.number > 20) throw TO::badArg(line, fn + ": `retry` must be a whole number from 0 to 20"); o.retry = (long)vRetry.number; }
    if (!isNil(vBackoff) && isNil(vRetry)) throw TO::badArg(line, fn + ": `backoff` needs `retry`");
    if (!isNil(vRetryOn)) { if (vRetryOn.type != Value::Type::ARRAY || !vRetryOn.array) throw TO::badArg(line, fn + ": `retryOn` must be an array of status codes"); o.retryOn.clear(); for (auto& e : *vRetryOn.array) { if (e.type != Value::Type::NUMBER) throw TO::badArg(line, fn + ": `retryOn` must contain numbers"); o.retryOn.push_back((long)e.number); } }
    if (!isNil(vCache)) { o.cacheMs = TO::dur(vCache, line, fn + " `cache`", true); if (o.method != "GET") throw TO::badArg(line, fn + ": `cache` is for GET only (a " + o.method + " changes data)"); }
    if (!isNil(vFollow)) o.follow = vFollow.isTruthy();
    if (!isNil(vMaxRed)) { if (vMaxRed.type != Value::Type::NUMBER || vMaxRed.number < 0 || vMaxRed.number > 30) throw TO::badArg(line, fn + ": `maxRedirects` must be 0..30"); o.maxRedirects = (long)vMaxRed.number; }
    if (!isNil(vCookies)) { if (vCookies.type == Value::Type::BOOL) { if (vCookies.boolean) o.cookieJar = fetchJarPath("default"); } else if (vCookies.type == Value::Type::STRING) o.cookieJar = fetchJarPath(vCookies.str); else throw TO::badArg(line, fn + ": `cookies` must be true or a jar name"); }
    if (!isNil(vMaxBytes)) { if (vMaxBytes.type != Value::Type::NUMBER || vMaxBytes.number < 1) throw TO::badArg(line, fn + ": `maxBytes` must be a positive number"); o.maxBytes = (long long)vMaxBytes.number; }
    if (!isNil(vProxy)) o.proxy = vProxy.toDisplayString();
    if (!isNil(vStrict)) o.strict = vStrict.isTruthy();
    if (!isNil(vCompress)) o.compressed = vCompress.isTruthy();
    if (!isNil(vInto)) { if (vInto.type != Value::Type::STRING && vInto.type != Value::Type::MAP) throw TO::badArg(line, fn + ": `into` must be a container"); std::string key; TO::containerEnv(*I.ip, vInto, fn.c_str(), line, &key); o.into = key; }
    if (!isNil(vThen)) { if (vThen.type != Value::Type::FUNCTION) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": `then` must be a function"); o.then = vThen; }
    if (!isNil(vAfter) && !isNil(vAt)) throw TO::badArg(line, fn + ": use `after` or `at`, not both");
    if (!isNil(vTimes) && isNil(vEvery)) throw TO::badArg(line, fn + ": `times` needs `every`");
    if (!isNil(vAfter) || !isNil(vAt) || !isNil(vEvery)) {
        o.hasSched = true;
        if (!isNil(vEvery)) { o.interval = TO::dur(vEvery, line, fn + " `every`", false); o.times = -1; o.delay = o.interval; }
        if (!isNil(vAfter)) o.delay = TO::dur(vAfter, line, fn + " `after`", true);
        if (!isNil(vAt)) o.delay = I.atArg(vAt, line, fn + " `at`");
        if (!isNil(vTimes)) { if (vTimes.type != Value::Type::NUMBER || vTimes.number < 1 || vTimes.number != std::floor(vTimes.number)) throw TO::badArg(line, fn + ": `times` must be a whole number >= 1"); o.times = (long)vTimes.number; }
    }
    return o;
}

// الردّ -> Value
static Value fetchToValue(const http::ResultEx& r, const FetchOpts& o, long attempts) {
    Value base = httpResultToValue(r);
    auto m = std::make_shared<MapData>();
    const bool good = r.status >= 200 && r.status < 300;
    for (auto& kv : *base.map) { const std::string k = kv.first.toDisplayString(); if (k == "ok") m->push_back({kv.first, Value::boolean_(good)}); else m->push_back(kv); }
    auto put = [&](const char* k, Value v) { m->push_back({Value::string(k), std::move(v)}); };
    put("statusText", Value::string(fetchStatusText(r.status)));
    put("method", Value::string(o.method));
    put("url", Value::string(r.finalUrl.empty() ? o.url : r.finalUrl));
    put("redirected", Value::boolean_(r.redirects > 0)); put("redirects", Value::num((double)r.redirects));
    auto hm = std::make_shared<MapData>(); std::vector<Value> cookies;
    for (auto& h : r.headers) {
        std::string k; for (char c : h.first) k += (char)std::tolower((unsigned char)c);
        if (k == "set-cookie") cookies.push_back(Value::string(h.second));
        bool done = false; for (auto& e : *hm) if (e.first.str == k) { e.second = Value::string(e.second.str + ", " + h.second); done = true; break; }
        if (!done) hm->push_back({Value::string(k), Value::string(h.second)});
    }
    put("headers", Value::makeMap(hm)); put("cookies", TimerOps::arr(std::move(cookies)));
    std::string ct = r.contentType; for (auto& c : ct) c = (char)std::tolower((unsigned char)c);
    put("contentType", Value::string(ct)); put("size", Value::num((double)r.body.size()));
    put("time", Value::num(std::round(r.timeMs))); put("attempts", Value::num((double)attempts)); put("cached", Value::boolean_(false));
    return Value::makeMap(m);
}
static Value fetchWith(const Value& v, const char* k, Value val) {
    auto m = std::make_shared<MapData>(*v.map); bool set = false;
    for (auto& e : *m) if (e.first.toDisplayString() == k) { e.second = val; set = true; }
    if (!set) m->push_back({Value::string(k), val}); return Value::makeMap(m);
}

// طلب واحد بإعادة محاولة وكاش. `publish` (اختيارية) تُستدعى للبدء/الانتهاء عند into.
static Value fetchRun(FetchCtx& I, const FetchOpts& o, int line,
                      const std::function<void(const Value&, bool)>& publish) {
    static std::unordered_map<std::string, std::pair<double, Value>> cache;
    std::string ck;
    if (o.cacheMs >= 0) { ck = o.method + "|" + o.url + "|"; for (auto& h : o.headers) ck += h.first + "=" + h.second + ";"; }
    if (o.cacheMs == 0) cache.erase(ck);
    else if (o.cacheMs > 0) { auto it = cache.find(ck); if (it != cache.end() && clk::monoMs() < it->second.first) { Value r = fetchWith(it->second.second, "cached", Value::boolean_(true)); if (publish) publish(r, false); return r; } }
    if (publish) publish(Value::nil(), true);
    http::RequestEx q; q.method = o.method; q.url = o.url; q.headers = o.headers; q.body = o.body; q.parts = o.parts;
    q.timeoutMs = (int)(o.timeoutMs > 0 ? o.timeoutMs : I.defaultTimeoutMs); q.connectTimeoutMs = (int)o.connectMs;
    q.follow = o.follow; q.maxRedirects = (int)o.maxRedirects; q.compressed = o.compressed; q.cookieJar = o.cookieJar; q.proxy = o.proxy; q.maxBytes = o.maxBytes;
    Value res; long attempts = 0; double delay = o.backoffMs;
    try {
        for (;;) {
            ++attempts;
            http::ResultEx r = http::performRequestEx(q);
            res = fetchToValue(r, o, attempts);
            const bool good = r.status >= 200 && r.status < 300;
            const bool again = !good && (r.status == 0 || std::find(o.retryOn.begin(), o.retryOn.end(), r.status) != o.retryOn.end());
            if (good || !again || attempts > o.retry) break;
            double wait = delay;
            for (auto& h : r.headers) { std::string k; for (char c : h.first) k += (char)std::tolower((unsigned char)c); if (k == "retry-after") { double s = std::atof(h.second.c_str()); if (s > 0) wait = std::max(wait, std::min(s, 60.0) * 1000.0); } }
            if (wait > 0) I.sleep(wait, line);
            delay *= o.backoffFactor;
        }
    } catch (...) { if (publish) publish(Value::nil(), false); throw; }
    if (o.cacheMs > 0) { bool good = false; for (auto& kv : *res.map) if (kv.first.toDisplayString() == "ok") good = kv.second.isTruthy(); if (good) cache[ck] = {clk::monoMs() + o.cacheMs, res}; }
    if (publish) publish(res, false);
    if (o.strict) {
        bool good = false; long st = 0; std::string err;
        for (auto& kv : *res.map) { const std::string k = kv.first.toDisplayString(); if (k == "ok") good = kv.second.isTruthy(); else if (k == "status" && kv.second.type == Value::Type::NUMBER) st = (long)kv.second.number; else if (k == "error") err = kv.second.toDisplayString(); }
        if (!good) throw diagErr(diag::Code::E0035_RuntimeError, line, "fetch: " + o.method + " " + o.url + " failed: " + (st ? "HTTP " + std::to_string(st) + " " + fetchStatusText(st) : err));
    }
    return res;
}
