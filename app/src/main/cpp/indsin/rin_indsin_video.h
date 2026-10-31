// indsin/rin_indsin_video.h — فهم روابط الفيديو (يوتيوب · Vimeo · Dailymotion · ملفات مباشرة · HLS · صفحات عامة)
// واستخراج رابط الفيديو من استجابة API (JSON). مشترك بين الرسم (شارة المزوّد) ودوال Rin (indsin.videoInfo/videoUrl …).
// نقي بلا حالة. التشغيل الفعلي مسؤولية المضيف (IndsinFabricView: VideoView للملفات المباشرة، WebView للتضمين).
#pragma once
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "../rin_interpreter.h"

namespace indsin {
namespace video {

struct Source {
    std::string provider = "none";   // youtube | vimeo | dailymotion | direct | page | none
    std::string id;                  // معرّف المقطع (للمزوّدين المعروفين)
    std::string url;                 // الرابط الأصلي (مقصوص)
    int start = 0;                   // ثوانٍ (t= / start=)
    bool valid() const { return provider != "none"; }
    bool isDirect() const { return provider == "direct"; }
};

inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
// "90" · "1m30s" · "1h2m3s" -> ثوانٍ
inline int parseStart(const std::string& t) {
    if (t.empty()) return 0;
    bool plain = true; for (char c : t) if (!std::isdigit((unsigned char)c)) plain = false;
    if (plain) return std::atoi(t.c_str());
    int total = 0, cur = 0;
    for (char c : t) {
        if (std::isdigit((unsigned char)c)) cur = cur * 10 + (c - '0');
        else if (c == 'h') { total += cur * 3600; cur = 0; }
        else if (c == 'm') { total += cur * 60; cur = 0; }
        else if (c == 's') { total += cur; cur = 0; }
    }
    return total;
}
inline std::string queryParam(const std::string& url, const std::string& key) {
    size_t q = url.find('?'); size_t h = url.find('#');
    for (size_t from : {q, h}) {
        if (from == std::string::npos) continue;
        size_t i = from + 1;
        while (i < url.size()) {
            size_t e = url.find_first_of("&#", i); if (e == std::string::npos) e = url.size();
            std::string kv = url.substr(i, e - i); size_t eq = kv.find('=');
            if (eq != std::string::npos && kv.substr(0, eq) == key) return kv.substr(eq + 1);
            i = e + 1;
        }
    }
    return "";
}

inline Source parse(const std::string& raw) {
    Source s; s.url = trim(raw);
    const std::string u = lower(s.url);
    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) return s;
    s.provider = "page";
    std::smatch m;
    static const std::regex yt1("(?:youtube(?:-nocookie)?\\.com/(?:watch\\?(?:.*&)?v=|embed/|shorts/|live/|v/)|youtu\\.be/)([A-Za-z0-9_-]{6,15})", std::regex::icase);
    static const std::regex vm("vimeo\\.com/(?:video/|channels/[^/]+/|groups/[^/]+/videos/)?([0-9]{5,12})", std::regex::icase);
    static const std::regex dm("(?:dailymotion\\.com/(?:embed/)?video/|dai\\.ly/)([A-Za-z0-9]+)", std::regex::icase);
    if (std::regex_search(s.url, m, yt1)) {
        s.provider = "youtube"; s.id = m[1];
        s.start = parseStart(queryParam(s.url, "t").empty() ? queryParam(s.url, "start") : queryParam(s.url, "t"));
    } else if (std::regex_search(s.url, m, vm)) {
        s.provider = "vimeo"; s.id = m[1];
        size_t h = s.url.find("#t="); if (h != std::string::npos) s.start = parseStart(s.url.substr(h + 3));
    } else if (std::regex_search(s.url, m, dm)) {
        s.provider = "dailymotion"; s.id = m[1];
        s.start = parseStart(queryParam(s.url, "start"));
    } else {
        std::string path = u.substr(0, u.find_first_of("?#"));
        static const char* ext[] = {".mp4", ".m4v", ".mov", ".webm", ".mkv", ".3gp", ".avi", ".ogv", ".m3u8", ".mpd", ".ts"};
        for (auto e : ext) { size_t n = std::char_traits<char>::length(e); if (path.size() > n && path.compare(path.size() - n, n, e) == 0) { s.provider = "direct"; break; } }
    }
    return s;
}

struct Options { bool autoplay = false, loop = false, muted = false, controls = true; };

inline std::string embedUrl(const Source& s, const Options& o = {}) {
    auto b = [](bool v) { return v ? "1" : "0"; };
    if (s.provider == "youtube") {
        std::string r = "https://www.youtube-nocookie.com/embed/" + s.id + "?rel=0&playsinline=1&controls=" + b(o.controls);
        if (o.autoplay) r += "&autoplay=1";
        if (o.muted || o.autoplay) r += "&mute=1";
        if (o.loop) r += "&loop=1&playlist=" + s.id;
        if (s.start > 0) r += "&start=" + std::to_string(s.start);
        return r;
    }
    if (s.provider == "vimeo") {
        std::string r = "https://player.vimeo.com/video/" + s.id + "?playsinline=1&controls=" + b(o.controls);
        if (o.autoplay) r += "&autoplay=1";
        if (o.muted || o.autoplay) r += "&muted=1";
        if (o.loop) r += "&loop=1";
        if (s.start > 0) r += "#t=" + std::to_string(s.start) + "s";
        return r;
    }
    if (s.provider == "dailymotion") {
        std::string r = "https://www.dailymotion.com/embed/video/" + s.id + "?controls=" + (o.controls ? "true" : "false");
        if (o.autoplay) r += "&autoplay=1";
        if (o.muted || o.autoplay) r += "&mute=1";
        if (s.start > 0) r += "&start=" + std::to_string(s.start);
        return r;
    }
    return s.url;
}

inline std::string thumbnailUrl(const Source& s) {
    if (s.provider == "youtube") return "https://img.youtube.com/vi/" + s.id + "/hqdefault.jpg";
    if (s.provider == "dailymotion") return "https://www.dailymotion.com/thumbnail/video/" + s.id;
    return "";
}

// لقطات المصغّرة المتحركة: يوتيوب = الرئيسية + 1/2/3.jpg؛ Dailymotion = الرئيسية فقط (الملفات المباشرة تُستخرج لقطاتها على الجهاز).
inline std::vector<std::string> thumbnailFrames(const Source& s) {
    std::vector<std::string> v;
    std::string t = thumbnailUrl(s); if (!t.empty()) v.push_back(t);
    if (s.provider == "youtube") for (int i = 1; i <= 3; i++) v.push_back("https://img.youtube.com/vi/" + s.id + "/" + std::to_string(i) + ".jpg");
    return v;
}

inline std::string providerLabel(const Source& s) {
    if (s.provider == "youtube") return "YouTube";
    if (s.provider == "vimeo") return "Vimeo";
    if (s.provider == "dailymotion") return "Dailymotion";
    if (s.provider == "direct") { std::string u = lower(s.url.substr(0, s.url.find_first_of("?#"))); return u.size() > 5 && u.compare(u.size() - 5, 5, ".m3u8") == 0 ? "HLS" : "Video"; }
    if (s.provider == "page") return "Web";
    return "";
}

// ---- استخراج رابط الفيديو من استجابة API (قيمة Rin: map/array من r.json) ----
// path اختياري: "data.items[0].video.url". بلا path: بحث بأولوية المفاتيح المعروفة ثم أي نص يبدو رابط فيديو.
inline bool isHttp(const std::string& s) { std::string l = lower(trim(s)); return l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0; }

inline const rin::Value* mapGet(const rin::Value& v, const std::string& key) {
    if (v.type != rin::Value::Type::MAP || !v.map) return nullptr;
    for (auto& p : *v.map) if (p.first.type == rin::Value::Type::STRING && p.first.str == key) return &p.second;
    return nullptr;
}
inline std::string walkPath(const rin::Value& root, const std::string& path) {
    const rin::Value* cur = &root; size_t i = 0;
    while (i < path.size() && cur) {
        if (path[i] == '.') { i++; continue; }
        if (path[i] == '[') {
            size_t e = path.find(']', i); if (e == std::string::npos) return "";
            int idx = std::atoi(path.substr(i + 1, e - i - 1).c_str());
            if (cur->type != rin::Value::Type::ARRAY || !cur->array || idx < 0 || (size_t)idx >= cur->array->size()) return "";
            cur = &(*cur->array)[(size_t)idx]; i = e + 1; continue;
        }
        size_t e = path.find_first_of(".[", i); if (e == std::string::npos) e = path.size();
        cur = mapGet(*cur, path.substr(i, e - i)); i = e;
    }
    return cur && cur->type == rin::Value::Type::STRING ? trim(cur->str) : "";
}
inline void collectStrings(const rin::Value& v, int depth, std::vector<std::string>& out) {
    if (depth > 6) return;
    if (v.type == rin::Value::Type::STRING) { if (isHttp(v.str)) out.push_back(trim(v.str)); }
    else if (v.type == rin::Value::Type::ARRAY && v.array) { for (auto& x : *v.array) collectStrings(x, depth + 1, out); }
    else if (v.type == rin::Value::Type::MAP && v.map) { for (auto& p : *v.map) collectStrings(p.second, depth + 1, out); }
}
inline std::string findInTree(const rin::Value& v, int depth) {
    static const char* keys[] = {"video_url", "videoUrl", "video", "stream_url", "streamUrl", "hls", "hls_url", "mp4", "mp4_url", "embed_url", "embedUrl",
                                 "embed", "contentUrl", "content_url", "url", "src", "link", "source", "file", "download_url"};
    if (depth > 6) return "";
    if (v.type == rin::Value::Type::MAP && v.map) {
        for (auto k : keys) if (auto* x = mapGet(v, k)) {
            if (x->type == rin::Value::Type::STRING && isHttp(x->str)) return trim(x->str);
            if (x->type == rin::Value::Type::MAP || x->type == rin::Value::Type::ARRAY) { std::string r = findInTree(*x, depth + 1); if (!r.empty()) return r; }
        }
        for (auto& p : *v.map) { std::string r = findInTree(p.second, depth + 1); if (!r.empty()) return r; }
    } else if (v.type == rin::Value::Type::ARRAY && v.array) {
        for (auto& x : *v.array) { std::string r = findInTree(x, depth + 1); if (!r.empty()) return r; }
    }
    return "";
}
inline std::string fromData(const rin::Value& data, const std::string& path) {
    if (data.type == rin::Value::Type::STRING) return isHttp(data.str) ? trim(data.str) : "";
    if (!path.empty()) { std::string r = walkPath(data, path); return isHttp(r) ? r : ""; }
    std::string r = findInTree(data, 0);
    if (!r.empty() && parse(r).provider != "page") return r;
    std::vector<std::string> all; collectStrings(data, 0, all);
    for (auto& s : all) { std::string p = parse(s).provider; if (p == "youtube" || p == "vimeo" || p == "dailymotion" || p == "direct") return s; }
    return r;
}

} // namespace video
} // namespace indsin
