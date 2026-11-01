// rin_indsin_live.cpp — Indsin Live Preview server (Windows / macOS / Linux host).
//
// A real, cross-platform live preview for .indsin / .rin UI files. It is NOT a re-implementation:
// every frame comes from the same native engine the Android app calls over JNI
// (rin_indsin_session_* in indsin/rin_indsin_c_api.h — lexer -> parser -> Warp -> Fabric -> Indsin ->
// Dye). This tool only (1) watches the files, (2) keeps one engine *session* per connected browser
// (so Warp state such as a tapped counter survives edits, exactly like the Android panel),
// (3) forwards input (tap / long-press / double-tap / hover / text / tick) to the engine and
// (4) ships the engine's paint plan + fabric tree to a browser client (client/index.html), where the
// browser lays out text (Arabic shaping, RTL, emoji) and draws it.
//
// Any device with a modern browser is a host: desktop browsers, a phone or tablet on the same Wi-Fi
// (--lan), VS Code's webview (editors/vscode-indsin), CI (GET /shot.png).
//
// Usage:
//   rin_indsin_live <file.indsin|file.rin|dir> [--port N] [--lan] [--no-open] [--width N] [--json]
//
// Security model: the server binds to 127.0.0.1 unless --lan is given; every request needs the random
// per-run key printed in the URL (header X-Indsin-Key or ?k=), so other web pages / other LAN hosts
// can't drive it; files are only served from inside the project root and only for media types.
//
// Dependencies: C++17 + POSIX sockets or Winsock. No third-party libraries.

#include "../../app/src/main/cpp/indsin/rin_indsin_c_api.h"
#include "live_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <shellapi.h>
  typedef SOCKET sock_t;
  static const sock_t BAD_SOCK = INVALID_SOCKET;
  static void closeSock(sock_t s) { closesocket(s); }
#else
  #include <arpa/inet.h>
  #include <ifaddrs.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
  typedef int sock_t;
  static const sock_t BAD_SOCK = -1;
  static void closeSock(sock_t s) { close(s); }
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// Paths travel through the API as UTF-8; on Windows std::string -> fs::path would use the ANSI code page
// (breaking Arabic file names), so every conversion goes through these two helpers.
static fs::path P(const std::string& u8) {
#if defined(_WIN32)
  #if defined(__cpp_lib_char8_t) && __cplusplus >= 202002L
    return fs::path(std::u8string(u8.begin(), u8.end()));
  #else
    return fs::u8path(u8);
  #endif
#else
    return fs::path(u8);
#endif
}
static std::string U(const fs::path& p) {
#if defined(_WIN32)
    auto u = p.generic_u8string(); return std::string(u.begin(), u.end());
#else
    return p.generic_string();
#endif
}

// ===================================================================================================
// Small helpers
// ===================================================================================================
static std::string jesc(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
        }
    }
    return o;
}
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) out.erase(0, 3); // BOM
    return true;
}
static std::string takeC(char* p, const char* fallback = "{\"ok\":false,\"error\":\"engine returned null\"}") {
    if (!p) return fallback;
    std::string s = p; rin_free_string(p); return s;
}
static std::string randomHex(int bytes) {
    std::random_device rd; std::mt19937_64 g((uint64_t)rd() << 32 ^ rd() ^ (uint64_t)Clock::now().time_since_epoch().count());
    std::string o; char b[3];
    for (int i = 0; i < bytes; i++) { snprintf(b, sizeof b, "%02x", (unsigned)(g() & 0xFF)); o += b; }
    return o;
}
static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2;
        } else if (s[i] == '+') o += ' '; else o += s[i];
    }
    return o;
}

// ===================================================================================================
// Minimal JSON reader for the request bodies we accept: one flat object; values are kept as raw text
// and decoded on demand (string / number / bool). Nested values (arrays/objects) are kept raw.
// ===================================================================================================
struct JReq {
    std::map<std::string, std::string> raw; // key -> raw JSON text of the value
    bool ok = false;
    static void appendUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    static bool parseString(const std::string& s, size_t& i, std::string& out) { // s[i]=='"'
        out.clear(); i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break; case '/': out += '/'; break;
                case '\\': out += '\\'; break; case '"': out += '"'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                        if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                    }
                    appendUtf8(out, cp); break;
                }
                default: return false;
            }
        }
        return false;
    }
    static bool skipValue(const std::string& s, size_t& i) { // advances i past one JSON value
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i >= s.size()) return false;
        if (s[i] == '"') { std::string t; return parseString(s, i, t); }
        if (s[i] == '{' || s[i] == '[') {
            int depth = 0;
            for (; i < s.size(); i++) {
                if (s[i] == '"') { std::string t; if (!parseString(s, i, t)) return false; i--; }
                else if (s[i] == '{' || s[i] == '[') depth++;
                else if (s[i] == '}' || s[i] == ']') { if (--depth == 0) { i++; return true; } }
            }
            return false;
        }
        while (i < s.size() && s[i] != ',' && s[i] != '}' && !isspace((unsigned char)s[i])) i++;
        return true;
    }
    explicit JReq(const std::string& s) {
        size_t i = 0;
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i >= s.size() || s[i] != '{') return;
        i++;
        for (;;) {
            while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ',')) i++;
            if (i < s.size() && s[i] == '}') { ok = true; return; }
            if (i >= s.size() || s[i] != '"') return;
            std::string k; if (!parseString(s, i, k)) return;
            while (i < s.size() && isspace((unsigned char)s[i])) i++;
            if (i >= s.size() || s[i] != ':') return;
            i++;
            while (i < s.size() && isspace((unsigned char)s[i])) i++;
            size_t st = i; if (!skipValue(s, i)) return;
            raw[k] = s.substr(st, i - st);
        }
    }
    bool has(const std::string& k) const { return raw.count(k) > 0; }
    std::string str(const std::string& k, const std::string& d = "") const {
        auto it = raw.find(k); if (it == raw.end() || it->second.empty()) return d;
        if (it->second[0] != '"') return it->second;
        size_t i = 0; std::string o; return parseString(it->second, i, o) ? o : d;
    }
    double num(const std::string& k, double d = 0) const {
        auto it = raw.find(k); if (it == raw.end()) return d;
        const char* p = it->second.c_str(); char* e = nullptr; double v = strtod(p, &e); return e == p ? d : v;
    }
    bool boolean(const std::string& k, bool d = false) const {
        auto it = raw.find(k); if (it == raw.end()) return d;
        return it->second == "true" || it->second == "1";
    }
    std::string rawJson(const std::string& k, const std::string& d = "null") const {
        auto it = raw.find(k); return it == raw.end() ? d : it->second;
    }
};

// ===================================================================================================
// Project state: root directory, documents, clients (one engine session each)
// ===================================================================================================
struct Doc {
    std::string text;
    bool overridden = false;                // text came from an editor buffer (POST source), not disk
    fs::file_time_type mtime{};
    uintmax_t size = 0;
};
struct QMsg { bool coalesce; std::string data; };
struct Client {
    std::string cid;
    void* sess = nullptr;
    std::string page;                       // current page, relative to root, forward slashes
    std::vector<std::string> stack;         // pages to go back to
    int width = 390, vp = 844;
    std::string bg;
    std::string applied;                    // last source text pushed into the session
    Clock::time_point seen = Clock::now();
    unsigned gen = 0;                       // SSE connection generation
    std::mutex qmu; std::condition_variable qcv; std::deque<QMsg> q;
};

static fs::path g_root;
static std::string g_entry, g_key;
static std::mutex g_mu;                      // guards engine + all state below (the engine has process-wide registries)
static std::map<std::string, Doc> g_docs;
static std::map<std::string, std::shared_ptr<Client>> g_clients;
static std::map<std::string, std::pair<fs::file_time_type, uintmax_t>> g_tracked; // every project source file seen by the watcher
static std::atomic<bool> g_run{true};
static int g_defaultWidth = 390;

static std::string relPath(const fs::path& p) {
    std::error_code ec; fs::path r = fs::relative(p, g_root, ec);
    return U(ec ? p : r);
}
// Resolves a page name against the root; refuses anything that escapes it. Returns "" on failure.
static std::string resolvePage(const std::string& fromPage, std::string target, std::string& err) {
    target = trim(target);
    if (target.empty()) { err = "empty page"; return ""; }
    if (target.find('\0') != std::string::npos || target.find(':') != std::string::npos || target[0] == '\\') { err = "invalid page"; return ""; }
    std::vector<fs::path> cands;
    fs::path base = P(fromPage).parent_path();
    fs::path t = target[0] == '/' ? P(target.substr(1)) : (base / P(target));
    cands.push_back(t);
    if (!t.has_extension()) { cands.push_back(P(U(t) + ".rin")); cands.push_back(P(U(t) + ".indsin")); }
    std::error_code ec;
    fs::path rootCanon = fs::weakly_canonical(g_root, ec);
    for (auto& c : cands) {
        fs::path full = fs::weakly_canonical(g_root / c, ec);
        if (ec) continue;
        auto rs = U(rootCanon); auto fsr = U(full);
        if (fsr.size() <= rs.size() || fsr.compare(0, rs.size(), rs) != 0 || fsr[rs.size()] != '/') { err = "page is outside the project"; return ""; }
        std::string ext = lower(U(full.extension()));
        if (ext != ".rin" && ext != ".indsin") continue;
        if (fs::is_regular_file(full, ec)) return U(fs::relative(full, rootCanon, ec));
    }
    if (err.empty()) err = "page not found: " + target;
    return "";
}
static Doc* getDoc(const std::string& page, bool reload = false) {
    auto it = g_docs.find(page);
    if (it != g_docs.end() && !reload) return &it->second;
    Doc d; fs::path full = g_root / P(page);
    if (!readFile(full, d.text)) return nullptr;
    std::error_code ec; d.mtime = fs::last_write_time(full, ec); d.size = fs::file_size(full, ec);
    g_docs[page] = d;
    return &g_docs[page];
}

// ---- wire format: every frame is {"reason","page","depth","src"?,"env":<engine envelope>} ----------
static std::string wrapFrame(Client& c, const char* reason, const std::string& env, bool withSrc) {
    std::ostringstream o;
    o << "{\"reason\":\"" << reason << "\",\"page\":\"" << jesc(c.page) << "\",\"depth\":" << c.stack.size();
    if (withSrc) { auto it = g_docs.find(c.page); o << ",\"src\":\"" << jesc(it == g_docs.end() ? "" : it->second.text) << "\""; }
    o << ",\"env\":" << (env.empty() ? "null" : env) << "}";
    return o.str();
}
static void pushQ(Client& c, bool coalesce, const std::string& data) {
    std::lock_guard<std::mutex> l(c.qmu);
    if (coalesce && !c.q.empty() && c.q.back().coalesce) c.q.back().data = data; else c.q.push_back({coalesce, data});
    while (c.q.size() > 16) c.q.pop_front();
    c.qcv.notify_all();
}

static void applyViewport(Client& c) {
    if (!c.sess) return;
    rin_indsin_session_resize(c.sess, c.width, c.vp);
    if (!c.bg.empty()) rin_indsin_session_set_default_background(c.sess, c.bg.c_str());
}
// (Re)creates the engine session of a client for its current page. Warp state starts fresh.
static std::string startSession(Client& c) {
    if (c.sess) { rin_indsin_session_free(c.sess); c.sess = nullptr; }
    Doc* d = getDoc(c.page);
    if (!d) return "{\"ok\":false,\"error\":\"cannot read " + jesc(c.page) + "\"}";
    c.sess = rin_indsin_session_create(d->text.c_str(), c.width);
    if (!c.sess) return "{\"ok\":false,\"error\":\"could not create session\"}";
    c.applied = d->text;
    rin_indsin_session_set_viewport(c.sess, c.vp);
    if (!c.bg.empty()) rin_indsin_session_set_default_background(c.sess, c.bg.c_str());
    return takeC(rin_indsin_session_render_json(c.sess));
}
// Hot-diff new source into the live session (Shuttle): keeps Warp state; a parse error keeps the last good frame.
static std::string hotApply(Client& c, const std::string& text) {
    if (!c.sess) { Doc* d = getDoc(c.page); (void)d; return startSession(c); }
    c.applied = text;
    return takeC(rin_indsin_session_update_source(c.sess, text.c_str()));
}

// ===================================================================================================
// API
// ===================================================================================================
static std::shared_ptr<Client> findClient(const std::string& cid) {
    auto it = g_clients.find(cid); if (it == g_clients.end()) return nullptr;
    it->second->seen = Clock::now(); return it->second;
}
static std::string errJson(const std::string& m) { return "{\"ok\":false,\"error\":\"" + jesc(m) + "\"}"; }

// returns HTTP status via `status`, JSON body as result
static std::string handleApi(const JReq& q, int& status) {
    status = 200;
    std::string cmd = q.str("cmd");
    std::lock_guard<std::mutex> lock(g_mu);

    // ---- editor buffer push (VS Code / any editor): live-as-you-type without saving -----------------
    if (cmd == "source") {
        std::string err, page = q.has("page") && !q.str("page").empty() ? resolvePage(g_entry, q.str("page"), err) : g_entry;
        if (page.empty()) { status = 404; return errJson(err); }
        Doc* d = getDoc(page); if (!d) { status = 404; return errJson("cannot read page"); }
        std::string text = q.str("text");
        if (text == d->text) return "{\"ok\":true,\"changed\":false}";
        d->text = text; d->overridden = true;
        int n = 0;
        for (auto& kv : g_clients) if (kv.second->page == page) { n++; pushQ(*kv.second, true, wrapFrame(*kv.second, "edit", hotApply(*kv.second, text), true)); }
        return "{\"ok\":true,\"changed\":true,\"clients\":" + std::to_string(n) + "}";
    }
    if (cmd == "info") {
        return "{\"ok\":true,\"entry\":\"" + jesc(g_entry) + "\",\"root\":\"" + jesc(U(g_root)) + "\",\"clients\":" + std::to_string(g_clients.size()) + "}";
    }

    // ---- commands bound to a client session ----------------------------------------------------------
    std::string cid = q.str("cid");
    if (cid.empty() || cid.size() > 64) { status = 400; return errJson("missing cid"); }

    if (cmd == "open") {
        auto c = findClient(cid);
        if (!c) { c = std::make_shared<Client>(); c->cid = cid; g_clients[cid] = c; }
        c->width = std::max(120, std::min(4096, (int)q.num("width", g_defaultWidth)));
        c->vp = std::max(100, std::min(8192, (int)q.num("vp", 844)));
        c->bg = q.str("bg");
        std::string err, page = q.has("page") && !q.str("page").empty() ? resolvePage(g_entry, q.str("page"), err) : g_entry;
        if (page.empty()) page = g_entry;
        c->page = page; c->stack.clear();
        getDoc(page, true);
        return wrapFrame(*c, "open", startSession(*c), true);
    }
    auto c = findClient(cid);
    if (!c) { status = 410; return errJson("no-session"); }
    if (!c->sess) { status = 410; return errJson("no-session"); }
    void* s = c->sess;
    double x = q.num("x"), y = q.num("y");

    if (cmd == "tap")         return wrapFrame(*c, "tap", takeC(rin_indsin_session_tap(s, x, y)), false);
    if (cmd == "long_press")  return wrapFrame(*c, "tap", takeC(rin_indsin_session_long_press(s, x, y)), false);
    if (cmd == "double_tap")  return wrapFrame(*c, "tap", takeC(rin_indsin_session_double_tap(s, x, y)), false);
    if (cmd == "hover")       return wrapFrame(*c, "tap", takeC(rin_indsin_session_hover(s, x, y, q.boolean("enter", true) ? 1 : 0)), false);
    if (cmd == "tick")        return wrapFrame(*c, "tick", takeC(rin_indsin_session_tick(s)), false);
    if (cmd == "input")       return wrapFrame(*c, "tap", takeC(rin_indsin_session_set_input(s, q.str("name").c_str(), q.str("text").c_str())), false);
    if (cmd == "render")      return wrapFrame(*c, "render", takeC(rin_indsin_session_render_json(s)), true);
    if (cmd == "resize") {
        c->width = std::max(120, std::min(4096, (int)q.num("width", c->width)));
        c->vp = std::max(100, std::min(8192, (int)q.num("vp", c->vp)));
        applyViewport(*c);
        return wrapFrame(*c, "resize", takeC(rin_indsin_session_render_json(s)), false);
    }
    if (cmd == "bg") {
        c->bg = q.str("color");
        if (!c->bg.empty() && !rin_indsin_session_set_default_background(s, c->bg.c_str())) { c->bg.clear(); status = 400; return errJson("bad color"); }
        if (c->bg.empty()) { // no API to "clear" except re-creating; resize forces a fresh pass with the cleared host default
            rin_indsin_session_set_default_background(s, "");
        }
        return wrapFrame(*c, "resize", takeC(rin_indsin_session_render_json(s)), false);
    }
    if (cmd == "reload") { // restart: re-read from disk, fresh Warp state
        getDoc(c->page, true);
        return wrapFrame(*c, "reload", startSession(*c), true);
    }
    if (cmd == "navigate" || cmd == "replace") {
        std::string err, page = resolvePage(c->page, q.str("target"), err);
        if (page.empty()) { status = 404; return errJson(err); }
        if (!getDoc(page, true)) { status = 404; return errJson("cannot read " + page); }
        if (cmd == "navigate") c->stack.push_back(c->page);
        c->page = page;
        return wrapFrame(*c, "navigate", startSession(*c), true);
    }
    if (cmd == "back") {
        if (c->stack.empty()) { status = 409; return errJson("no history"); }
        c->page = c->stack.back(); c->stack.pop_back();
        getDoc(c->page);
        return wrapFrame(*c, "navigate", startSession(*c), true);
    }
    if (cmd == "inspect") {
        std::string what = q.str("what", "audit");
        char* r = what == "stats" ? rin_indsin_session_stats_json(s) : what == "outline" ? rin_indsin_session_outline(s) : rin_indsin_session_audit_json(s);
        std::string body = takeC(r, "null");
        if (what == "outline") body = "\"" + jesc(body) + "\"";
        return "{\"ok\":true,\"what\":\"" + jesc(what) + "\",\"data\":" + body + "}";
    }
    if (cmd == "media_picked") {
        return wrapFrame(*c, "tap", takeC(rin_indsin_session_media_picked(s, q.str("cell").c_str(), q.rawJson("items", "[]").c_str(),
                          q.str("kind", "any").c_str(), q.boolean("multiple") ? 1 : 0, q.num("maxMb", 0), q.boolean("append") ? 1 : 0)), false);
    }
    if (cmd == "media_progress") {
        return wrapFrame(*c, "tap", takeC(rin_indsin_session_media_progress(s, q.str("cell").c_str(), q.str("status").c_str(), q.num("progress"), q.str("detail").c_str())), false);
    }
    if (cmd == "play_effect")  return wrapFrame(*c, "tap", takeC(rin_indsin_session_play_effect(s, q.str("target").c_str(), q.str("effect").c_str())), false);
    status = 400; return errJson("unknown cmd: " + cmd);
}

// ===================================================================================================
// File watcher: polls mtime/size (portable, no inotify/FSEvents/ReadDirectoryChanges dependency).
// Every change to a project source file hot-applies into each client's session; imports are covered
// because ANY tracked file change re-feeds the current page (re-parse resolves @import again).
// ===================================================================================================
static bool isSourceExt(const fs::path& p) {
    std::string e = lower(U(p.extension()));
    return e == ".rin" || e == ".indsin" || e == ".zpp" || e == ".rinz" || e == ".json" || e == ".toml" || e == ".xml";
}
static void watcherLoop() {
    bool first = true;
    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::map<std::string, std::pair<fs::file_time_type, uintmax_t>> now;
        std::error_code ec; size_t count = 0;
        fs::recursive_directory_iterator it(g_root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end && count < 4000; it.increment(ec)) {
            const fs::path& p = it->path(); std::string name = U(p.filename());
            if (it->is_directory(ec)) {
                if (name == ".git" || name == "node_modules" || name == "build" || name == "dist" || name == ".gradle") it.disable_recursion_pending();
                else if (it.depth() > 4) it.disable_recursion_pending();
                continue;
            }
            if (!isSourceExt(p)) continue;
            std::error_code e2; auto mt = fs::last_write_time(p, e2); auto sz = fs::file_size(p, e2);
            if (!e2) { now[relPath(p)] = {mt, sz}; count++; }
        }
        std::vector<std::string> changed;
        if (!first) {
            for (auto& kv : now) { auto o = g_tracked.find(kv.first); if (o == g_tracked.end() || o->second != kv.second) changed.push_back(kv.first); }
            for (auto& kv : g_tracked) if (!now.count(kv.first)) changed.push_back(kv.first);
        }
        first = false;
        g_tracked = now;
        if (changed.empty()) continue;

        std::lock_guard<std::mutex> lock(g_mu);
        // refresh docs that changed on disk (this also clears an editor-buffer override: disk is now newer)
        for (auto& ch : changed) {
            auto d = g_docs.find(ch);
            if (d != g_docs.end()) {
                std::string text;
                if (readFile(g_root / P(ch), text)) { d->second.text = text; d->second.overridden = false; }
            }
        }
        for (auto& kv : g_clients) {
            Client& c = *kv.second; Doc* d = getDoc(c.page); if (!d) continue;
            bool pageChanged = std::find(changed.begin(), changed.end(), c.page) != changed.end();
            bool depChanged = !pageChanged; // an imported/library file changed: re-parse the same text
            if (pageChanged && d->text == c.applied) continue; // e.g. save of what the editor already pushed live
            (void)depChanged;
            pushQ(c, true, wrapFrame(c, "file", hotApply(c, d->text), true));
        }
    }
}
static void gcLoop() {
    while (g_run) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::lock_guard<std::mutex> lock(g_mu);
        for (auto it = g_clients.begin(); it != g_clients.end();) {
            if (Clock::now() - it->second->seen > std::chrono::minutes(15)) {
                if (it->second->sess) rin_indsin_session_free(it->second->sess);
                it = g_clients.erase(it);
            } else ++it;
        }
    }
}

// ===================================================================================================
// HTTP plumbing
// ===================================================================================================
struct Req { std::string method, path, query, body; std::map<std::string, std::string> h; std::map<std::string, std::string> qs; };

static bool sendAll(sock_t s, const char* d, size_t n) {
    while (n > 0) {
#ifdef _WIN32
        int w = send(s, d, (int)std::min<size_t>(n, 1 << 20), 0);
#elif defined(MSG_NOSIGNAL)
        ssize_t w = send(s, d, n, MSG_NOSIGNAL);
#else
        ssize_t w = send(s, d, n, 0);
#endif
        if (w <= 0) return false;
        d += w; n -= (size_t)w;
    }
    return true;
}
static bool sendAll(sock_t s, const std::string& d) { return sendAll(s, d.data(), d.size()); }

static const char* statusText(int c) {
    switch (c) { case 200: return "OK"; case 204: return "No Content"; case 206: return "Partial Content"; case 400: return "Bad Request"; case 401: return "Unauthorized";
                 case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 409: return "Conflict"; case 410: return "Gone";
                 case 413: return "Payload Too Large"; case 416: return "Range Not Satisfiable"; default: return "Error"; }
}
static void respond(sock_t s, int code, const std::string& ctype, const std::string& body, const std::string& extra = "") {
    std::ostringstream h;
    h << "HTTP/1.1 " << code << " " << statusText(code) << "\r\nContent-Type: " << ctype << "\r\nContent-Length: " << body.size()
      << "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n" << extra << "\r\n";
    sendAll(s, h.str()); if (!body.empty()) sendAll(s, body);
}
static void respondJson(sock_t s, int code, const std::string& body) { respond(s, code, "application/json; charset=utf-8", body); }

static bool readRequest(sock_t s, Req& r) {
    std::string buf; char tmp[8192]; size_t hdrEnd = std::string::npos;
    while (hdrEnd == std::string::npos) {
        int n = (int)recv(s, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
        hdrEnd = buf.find("\r\n\r\n");
        if (buf.size() > 64 * 1024 && hdrEnd == std::string::npos) return false;
    }
    std::istringstream hs(buf.substr(0, hdrEnd)); std::string line;
    if (!std::getline(hs, line)) return false;
    { std::istringstream rl(line); std::string target, ver; rl >> r.method >> target >> ver; if (r.method.empty() || target.empty()) return false;
      size_t qm = target.find('?'); r.path = urlDecode(target.substr(0, qm)); if (qm != std::string::npos) r.query = target.substr(qm + 1); }
    while (std::getline(hs, line)) {
        size_t c = line.find(':'); if (c == std::string::npos) continue;
        r.h[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    { std::istringstream qs(r.query); std::string kv; while (std::getline(qs, kv, '&')) { size_t e = kv.find('='); r.qs[urlDecode(kv.substr(0, e))] = e == std::string::npos ? "" : urlDecode(kv.substr(e + 1)); } }
    size_t clen = r.h.count("content-length") ? (size_t)strtoull(r.h["content-length"].c_str(), nullptr, 10) : 0;
    if (clen > 8u * 1024 * 1024) return false;
    r.body = buf.substr(hdrEnd + 4);
    while (r.body.size() < clen) { int n = (int)recv(s, tmp, sizeof tmp, 0); if (n <= 0) return false; r.body.append(tmp, (size_t)n); }
    r.body.resize(clen);
    return true;
}

static bool constEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned d = 0;
    for (size_t i = 0; i < a.size(); i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
static bool authorized(const Req& r) {
    auto h = r.h.find("x-indsin-key"); if (h != r.h.end() && constEq(h->second, g_key)) return true;
    auto q = r.qs.find("k"); return q != r.qs.end() && constEq(q->second, g_key);
}

static const char* mimeFor(const std::string& ext, bool& sandbox) {
    sandbox = false;
    static const std::map<std::string, const char*> m = {
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".webp", "image/webp"}, {".avif", "image/avif"},
        {".bmp", "image/bmp"}, {".ico", "image/x-icon"}, {".svg", "image/svg+xml"}, {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".ogv", "video/ogg"},
        {".mov", "video/quicktime"}, {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".ogg", "audio/ogg"}, {".m4a", "audio/mp4"}, {".aac", "audio/aac"},
        {".flac", "audio/flac"}, {".pdf", "application/pdf"}, {".woff2", "font/woff2"}, {".woff", "font/woff"}, {".ttf", "font/ttf"}, {".otf", "font/otf"}};
    auto it = m.find(ext); if (it == m.end()) return nullptr;
    if (ext == ".svg" || ext == ".pdf") sandbox = true;
    return it->second;
}
static void serveProjectFile(sock_t s, const Req& r, const std::string& rel) {
    std::error_code ec;
    if (rel.empty() || rel.find('\0') != std::string::npos || rel.find("..") != std::string::npos) { respond(s, 403, "text/plain", "forbidden"); return; }
    fs::path rootCanon = fs::weakly_canonical(g_root, ec);
    fs::path full = fs::weakly_canonical(rootCanon / P(rel), ec);
    std::string rs = U(rootCanon), fsr = U(full);
    if (ec || fsr.size() <= rs.size() || fsr.compare(0, rs.size(), rs) != 0 || fsr[rs.size()] != '/') { respond(s, 403, "text/plain", "forbidden"); return; }
    bool sandbox; const char* mime = mimeFor(lower(U(full.extension())), sandbox);
    if (!mime) { respond(s, 403, "text/plain", "this file type is not served"); return; }
    if (!fs::is_regular_file(full, ec)) { respond(s, 404, "text/plain", "not found"); return; }
    uintmax_t size = fs::file_size(full, ec);
    uintmax_t from = 0, to = size ? size - 1 : 0; int code = 200;
    auto rg = r.h.find("range");
    if (rg != r.h.end() && rg->second.compare(0, 6, "bytes=") == 0) {
        std::string spec = rg->second.substr(6); size_t dash = spec.find('-');
        if (dash != std::string::npos) {
            std::string a = spec.substr(0, dash), b = spec.substr(dash + 1);
            if (a.empty()) { uintmax_t n = strtoull(b.c_str(), nullptr, 10); from = n > size ? 0 : size - n; }
            else { from = strtoull(a.c_str(), nullptr, 10); if (!b.empty()) to = std::min<uintmax_t>(strtoull(b.c_str(), nullptr, 10), size - 1); }
            if (from >= size) { respond(s, 416, "text/plain", "bad range", "Content-Range: bytes */" + std::to_string(size) + "\r\n"); return; }
            code = 206;
        }
    }
    std::ifstream f(full, std::ios::binary); if (!f) { respond(s, 404, "text/plain", "not found"); return; }
    std::ostringstream h; uintmax_t len = size ? to - from + 1 : 0;
    h << "HTTP/1.1 " << code << " " << statusText(code) << "\r\nContent-Type: " << mime << "\r\nContent-Length: " << len << "\r\nAccept-Ranges: bytes\r\nCache-Control: no-cache\r\n"
      << "X-Content-Type-Options: nosniff\r\n" << (sandbox ? "Content-Security-Policy: sandbox\r\n" : "")
      << (code == 206 ? "Content-Range: bytes " + std::to_string(from) + "-" + std::to_string(to) + "/" + std::to_string(size) + "\r\n" : std::string()) << "Connection: close\r\n\r\n";
    if (!sendAll(s, h.str())) return;
    f.seekg((std::streamoff)from); char buf[65536]; uintmax_t left = len;
    while (left > 0 && f) { f.read(buf, (std::streamsize)std::min<uintmax_t>(sizeof buf, left)); std::streamsize n = f.gcount(); if (n <= 0 || !sendAll(s, buf, (size_t)n)) return; left -= (uintmax_t)n; }
}

static void serveEvents(sock_t s, const Req& r) {
    std::string cid = r.qs.count("cid") ? r.qs.at("cid") : "";
    std::shared_ptr<Client> c; unsigned myGen;
    { std::lock_guard<std::mutex> lock(g_mu);
      auto it = g_clients.find(cid); if (it == g_clients.end()) { respond(s, 410, "text/plain", "no-session"); return; }
      c = it->second; myGen = ++c->gen; }
    std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nCache-Control: no-store\r\nX-Accel-Buffering: no\r\nConnection: keep-alive\r\n\r\nretry: 1000\n\n";
    if (!sendAll(s, head)) return;
    for (;;) {
        std::string out;
        { std::unique_lock<std::mutex> l(c->qmu);
          c->qcv.wait_for(l, std::chrono::seconds(12), [&] { return !c->q.empty() || !g_run || c->gen != myGen; });
          if (!g_run || c->gen != myGen) return; // superseded by a reconnect
          if (c->q.empty()) out = ": ping\n\n";
          else { while (!c->q.empty()) { out += "data: " + c->q.front().data + "\n\n"; c->q.pop_front(); } } }
        { std::lock_guard<std::mutex> lock(g_mu); c->seen = Clock::now(); }
        if (!sendAll(s, out)) return;
    }
}

static bool hostAllowed(const Req& r, bool lan, int port) {
    auto it = r.h.find("host"); if (it == r.h.end()) return false;
    std::string h = lower(it->second);
    if (h == "127.0.0.1:" + std::to_string(port) || h == "localhost:" + std::to_string(port) || h == "[::1]:" + std::to_string(port)) return true;
    if (!lan) return false;
    // LAN mode: only IP literals or .local names (blocks DNS-rebinding hostnames); the key still has to match.
    std::string name = h.substr(0, h.rfind(':'));
    if (name.size() > 6 && name.compare(name.size() - 6, 6, ".local") == 0) return true;
    for (char ch : name) if (!(isdigit((unsigned char)ch) || ch == '.')) return false;
    return !name.empty();
}

static std::string makeShot(Client& c) { // returns PNG bytes ("" on failure); caller holds g_mu
    if (!c.sess) return "";
    fs::path tmp = fs::temp_directory_path() / ("indsin_live_" + randomHex(6) + ".png");
    std::string out;
    if (rin_indsin_session_export_png(c.sess, tmp.string().c_str())) readFile(tmp, out);
    std::error_code ec; fs::remove(tmp, ec);
    return out;
}

static std::atomic<int> g_conns{0};
static bool g_lan = false; static int g_port = 0;

static void handleConn(sock_t s) {
#ifdef _WIN32
    DWORD tv = 15000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
    struct timeval tv; tv.tv_sec = 15; tv.tv_usec = 0; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
  #ifdef SO_NOSIGPIPE
    int one = 1; setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
  #endif
#endif
    int nodelay = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof nodelay);
    Req r;
    if (!readRequest(s, r)) { closeSock(s); return; }
    if (!hostAllowed(r, g_lan, g_port)) { respond(s, 403, "text/plain", "bad host"); closeSock(s); return; }

    if (r.path == "/" || r.path == "/index.html") {
        if (!authorized(r)) { respond(s, 401, "text/plain; charset=utf-8", "Unauthorized — open the exact URL printed by rin_indsin_live (it contains the key)."); closeSock(s); return; }
        respond(s, 200, "text/html; charset=utf-8", std::string(indsin_live::kClientHtml, indsin_live::kClientHtmlSize),
                "Content-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' https: data: blob:; media-src 'self' https: blob:; "
                "frame-src https: data:; connect-src 'self'; base-uri 'none'; form-action 'none'\r\n");
        closeSock(s); return;
    }
    if (!authorized(r)) { respondJson(s, 401, errJson("unauthorized")); closeSock(s); return; }

    if (r.path == "/api" && r.method == "POST") {
        JReq q(r.body);
        if (!q.ok) { respondJson(s, 400, errJson("bad json")); closeSock(s); return; }
        int st = 200; std::string out = handleApi(q, st);
        respondJson(s, st, out); closeSock(s); return;
    }
    if (r.path == "/events" && r.method == "GET") { g_conns++; serveEvents(s, r); g_conns--; closeSock(s); return; }
    if (r.path == "/shot.png" && r.method == "GET") {
        std::string png;
        { std::lock_guard<std::mutex> lock(g_mu);
          std::string cid = r.qs.count("cid") ? r.qs.at("cid") : "";
          std::shared_ptr<Client> c = findClient(cid);
          if (!c) { // headless: a throw-away session for the entry (or ?page=), width from ?w=
              c = std::make_shared<Client>(); c->width = r.qs.count("w") ? std::max(120, std::min(4096, atoi(r.qs.at("w").c_str()))) : g_defaultWidth;
              std::string err, page = r.qs.count("page") ? resolvePage(g_entry, r.qs.at("page"), err) : g_entry; c->page = page.empty() ? g_entry : page;
              startSession(*c); png = makeShot(*c); if (c->sess) rin_indsin_session_free(c->sess);
          } else png = makeShot(*c); }
        if (png.empty()) respondJson(s, 409, errJson("no valid frame (parse error?)")); else respond(s, 200, "image/png", png, "Content-Disposition: inline; filename=\"indsin-preview.png\"\r\n");
        closeSock(s); return;
    }
    if (r.path.rfind("/f/", 0) == 0 && r.method == "GET") { serveProjectFile(s, r, r.path.substr(3)); closeSock(s); return; }
    respond(s, 404, "text/plain", "not found"); closeSock(s);
}

// ===================================================================================================
// main
// ===================================================================================================
static std::vector<std::string> lanAddresses() {
    std::vector<std::string> out;
#ifdef _WIN32
    char host[256]; if (gethostname(host, sizeof host) == 0) {
        addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) { for (auto* p = res; p; p = p->ai_next) { char b[64]; inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, b, sizeof b); if (strncmp(b, "127.", 4)) out.push_back(b); } freeaddrinfo(res); }
    }
#else
    ifaddrs* ifa = nullptr; if (getifaddrs(&ifa) == 0) {
        for (auto* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            char b[64]; inet_ntop(AF_INET, &((sockaddr_in*)p->ifa_addr)->sin_addr, b, sizeof b);
            if (strncmp(b, "127.", 4) && strncmp(b, "169.254.", 8)) out.push_back(b);
        }
        freeifaddrs(ifa);
    }
#endif
    return out;
}
static void openBrowser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open '" + url + "' >/dev/null 2>&1 &"; if (system(cmd.c_str())) {}
#else
    std::string cmd = "(xdg-open '" + url + "' || sensible-browser '" + url + "') >/dev/null 2>&1 &"; if (system(cmd.c_str())) {}
#endif
}
static void usage(const char* p) {
    fprintf(stderr,
        "Indsin Live Preview — real engine, any browser\n"
        "usage: %s <file.indsin|file.rin|project-dir> [options]\n"
        "  --port N     TCP port (default 5757; falls back to a free port)\n"
        "  --lan        listen on all interfaces so phones/tablets on the same network can open it\n"
        "  --width N    default screen width in px (default 390)\n"
        "  --no-open    don't launch a browser\n"
        "  --json       print one machine-readable line: INDSIN_LIVE {\"url\":...,\"port\":...,\"key\":...}\n", p);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Re-read the command line as UTF-16 and convert to UTF-8 so Arabic/Unicode paths survive.
    std::vector<std::string> u8args; std::vector<char*> u8ptrs;
    { int wn = 0; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &wn);
      if (wa) { for (int i = 0; i < wn; i++) { int n = WideCharToMultiByte(CP_UTF8, 0, wa[i], -1, nullptr, 0, nullptr, nullptr);
                std::string s2((size_t)std::max(n, 1) - 1, '\0'); WideCharToMultiByte(CP_UTF8, 0, wa[i], -1, &s2[0], n, nullptr, nullptr); u8args.push_back(s2); }
                LocalFree(wa); for (auto& a : u8args) u8ptrs.push_back(&a[0]); argc = (int)u8ptrs.size(); argv = u8ptrs.data(); } }
#endif
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { usage(argv[0]); return argc < 2 ? 2 : 0; }
    int port = 5757; bool lan = false, openIt = true, json = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = atoi(argv[++i]);
        else if (a == "--lan") lan = true;
        else if (a == "--no-open") openIt = false;
        else if (a == "--json") json = true;
        else if (a == "--width" && i + 1 < argc) g_defaultWidth = std::max(120, std::min(4096, atoi(argv[++i])));
        else { usage(argv[0]); return 2; }
    }
    std::error_code ec; fs::path target = fs::weakly_canonical(P(argv[1]), ec);
    if (!fs::exists(target)) { fprintf(stderr, "error: %s does not exist\n", argv[1]); return 2; }
    if (fs::is_directory(target)) {
        g_root = target; g_entry.clear();
        for (const char* n : {"main.indsin", "index.indsin", "app.indsin", "main.rin", "index.rin", "app.rin"}) if (fs::exists(target / P(n))) { g_entry = n; break; }
        if (g_entry.empty()) for (auto& e : fs::directory_iterator(target)) { auto x = lower(U(e.path().extension())); if (x == ".indsin" || x == ".rin") { g_entry = U(e.path().filename()); break; } }
        if (g_entry.empty()) { fprintf(stderr, "error: no .indsin/.rin file found in %s\n", argv[1]); return 2; }
    } else { g_root = target.parent_path(); g_entry = U(target.filename()); }
    fs::current_path(g_root, ec); // @import "lib/..." resolves relative to the project
    g_key = randomHex(16);

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    sock_t ls = BAD_SOCK; int bound = 0;
    for (int attempt = 0; attempt < 40 && bound == 0; attempt++) {
        ls = socket(AF_INET, SOCK_STREAM, 0); if (ls == BAD_SOCK) { fprintf(stderr, "error: cannot create socket\n"); return 1; }
#ifndef _WIN32
        int yes = 1; // (Windows' SO_REUSEADDR would allow port hijacking; leave it off there)
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
#endif
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)(port + attempt));
        addr.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
        if (bind(ls, (sockaddr*)&addr, sizeof addr) == 0 && listen(ls, 64) == 0) { bound = port + attempt; break; }
        closeSock(ls); ls = BAD_SOCK;
    }
    if (!bound) { fprintf(stderr, "error: no free port near %d\n", port); return 1; }
    g_lan = lan; g_port = bound;

    // initial parse check so a broken first file is reported immediately in the terminal as well
    { std::lock_guard<std::mutex> lock(g_mu);
      Doc* d = getDoc(g_entry);
      if (!d) { fprintf(stderr, "error: cannot read %s\n", U(g_root / P(g_entry)).c_str()); return 2; } }

    std::string url = "http://127.0.0.1:" + std::to_string(bound) + "/?k=" + g_key;
    if (json) printf("INDSIN_LIVE {\"url\":\"%s\",\"port\":%d,\"key\":\"%s\",\"entry\":\"%s\"}\n", url.c_str(), bound, g_key.c_str(), jesc(g_entry).c_str());
    printf("Indsin Live Preview — %s\n  project : %s\n  open    : %s\n", g_entry.c_str(), U(g_root).c_str(), url.c_str());
    if (lan) { for (auto& ip : lanAddresses()) printf("  network : http://%s:%d/?k=%s   (phone / tablet)\n", ip.c_str(), bound, g_key.c_str());
               printf("  note    : --lan exposes the preview to your local network; the key in the URL is the only credential.\n"); }
    printf("  Ctrl+C to stop. Save the file (or push buffers via the VS Code extension) and the preview updates live.\n");
    fflush(stdout);
    if (openIt) openBrowser(url);

    std::thread(watcherLoop).detach();
    std::thread(gcLoop).detach();
    for (;;) {
        sockaddr_in ca{}; socklen_t cl = sizeof ca;
        sock_t cs = accept(ls, (sockaddr*)&ca, &cl);
        if (cs == BAD_SOCK) { if (!g_run) break; continue; }
        std::thread(handleConn, cs).detach();
    }
    return 0;
}
