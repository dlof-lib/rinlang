// indsin/rin_indsin_media.h — Indsin Media: اختيار الوسائط (صورة/فيديو/صوت/ملف) ورفعها.
//
// المبدأ نفسه الذي يعمل به `open:` و`navigate:`: المحرّك لا يلمس نظام التشغيل. هو (1) يفهم الأفعال
// pickMedia()/uploadMedia()/clearMedia()/removeMedia() ويتحقق من وسائطها، (2) يُخرج *طلباً* منظَّماً
// للمضيف (أندرويد: منتقي النظام + رفع multipart؛ سطح المكتب/CLI: لا شيء)، (3) يستقبل نتيجة المضيف
// (ملفات مختارة / تقدّم الرفع) ويكتبها في خلايا Warp فتتحدّث الواجهة بالآلية الحالية نفسها.
//
// خلايا Warp المشتقة من خلية وسائط اسمها `photo` (تُزرَع تلقائياً إن وُجد pickMedia/uploadMedia عليها):
//   photo           مسار أول ملف (نسبي لجذر المشروع) — يصلح مباشرة لـ `<Image src=photo>`
//   photo_name      اسم الملف الأصلي            photo_mime   نوع MIME
//   photo_size      الحجم بالبايت (رقم)         photo_sizeText   "2.4 MB"
//   photo_kind      image|video|audio|file      photo_count  عدد الملفات المختارة
//   photo_all       مصفوفة JSON لكل الملفات     photo_status idle|picked|uploading|done|error
//   photo_progress  0..100                      photo_error  رسالة خطأ (فارغة عند النجاح)
//   photo_response  جسم ردّ الخادم عند النجاح (مقتطع)
#pragma once
#include "rin_indsin_eval.h"
#include "../rin_ast.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace indsin {
namespace media {

enum class Kind { ANY, IMAGE, VIDEO, AUDIO, FILE };

inline std::string kindName(Kind k) {
    switch (k) {
        case Kind::IMAGE: return "image";
        case Kind::VIDEO: return "video";
        case Kind::AUDIO: return "audio";
        case Kind::FILE:  return "file";
        default:          return "any";
    }
}

inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "image" / "images" / "photo" / "صورة" ... -> Kind. false إن لم يُعرَف.
inline bool parseKind(const std::string& raw, Kind& out) {
    std::string s = lower(raw);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t b = 0; while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    s = s.substr(b);
    if (s == "image" || s == "images" || s == "photo" || s == "picture" || s == "صورة" || s == "صور" || s == "image/*") { out = Kind::IMAGE; return true; }
    if (s == "video" || s == "videos" || s == "فيديو" || s == "video/*") { out = Kind::VIDEO; return true; }
    if (s == "audio" || s == "sound" || s == "music" || s == "صوت" || s == "audio/*") { out = Kind::AUDIO; return true; }
    if (s == "file" || s == "files" || s == "document" || s == "doc" || s == "ملف") { out = Kind::FILE; return true; }
    if (s == "any" || s == "media" || s == "*" || s == "*/*" || s == "وسائط") { out = Kind::ANY; return true; }
    return false;
}

// أنواع الاختيار الموسَّعة: "camera" / "camera:image" / "camera:photo" -> صورة من الكاميرا،
// "camera:video" -> فيديو من الكاميرا، وإلا نفس parseKind من المكتبة. source = "camera" | "library".
inline bool parsePickKind(const std::string& raw, Kind& kind, std::string& source) {
    std::string s = lower(raw);
    if (s == "camera" || s == "كاميرا") { kind = Kind::IMAGE; source = "camera"; return true; }
    if (s.rfind("camera:", 0) == 0) {
        std::string k = s.substr(7);
        source = "camera";
        if (k == "image" || k == "photo") { kind = Kind::IMAGE; return true; }
        if (k == "video") { kind = Kind::VIDEO; return true; }
        return false;
    }
    source = "library";
    return parseKind(raw, kind);
}

inline std::string extOf(const std::string& name) {
    size_t slash = name.find_last_of("/\\");
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return lower(name.substr(dot + 1));
}

inline std::string mimeForName(const std::string& name) {
    static const struct { const char* ext; const char* mime; } table[] = {
        {"jpg","image/jpeg"},{"jpeg","image/jpeg"},{"png","image/png"},{"gif","image/gif"},
        {"webp","image/webp"},{"bmp","image/bmp"},{"heic","image/heic"},{"heif","image/heif"},{"svg","image/svg+xml"},
        {"mp4","video/mp4"},{"m4v","video/mp4"},{"mov","video/quicktime"},{"webm","video/webm"},
        {"mkv","video/x-matroska"},{"3gp","video/3gpp"},{"avi","video/x-msvideo"},
        {"mp3","audio/mpeg"},{"m4a","audio/mp4"},{"aac","audio/aac"},{"wav","audio/wav"},
        {"ogg","audio/ogg"},{"oga","audio/ogg"},{"opus","audio/opus"},{"flac","audio/flac"},
        {"pdf","application/pdf"},{"txt","text/plain"},{"csv","text/csv"},{"json","application/json"},
        {"zip","application/zip"},{"doc","application/msword"},
        {"docx","application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx","application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    };
    std::string e = extOf(name);
    for (auto& t : table) if (e == t.ext) return t.mime;
    return "application/octet-stream";
}

inline Kind kindFromMime(const std::string& mimeRaw) {
    std::string m = lower(mimeRaw);
    if (m.rfind("image/", 0) == 0) return Kind::IMAGE;
    if (m.rfind("video/", 0) == 0) return Kind::VIDEO;
    if (m.rfind("audio/", 0) == 0) return Kind::AUDIO;
    return Kind::FILE;
}

inline std::string humanSize(double bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 3) { bytes /= 1024.0; u++; }
    char buf[48];
    if (u == 0) std::snprintf(buf, sizeof buf, "%.0f %s", bytes, units[u]);
    else        std::snprintf(buf, sizeof buf, "%.1f %s", bytes, units[u]);
    return buf;
}

struct Item {
    std::string path;   // نسبي لجذر المشروع (مثل media/20260101_a.jpg)
    std::string name;   // الاسم الأصلي الذي رآه المستخدم
    std::string mime;
    double size = 0;    // بايت
    Kind kind = Kind::FILE;
};

inline std::string jsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    return o;
}

inline std::string itemsToJson(const std::vector<Item>& items) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < items.size(); i++) {
        if (i) os << ",";
        const Item& it = items[i];
        os << "{\"path\":\"" << jsonEsc(it.path) << "\",\"name\":\"" << jsonEsc(it.name)
           << "\",\"mime\":\"" << jsonEsc(it.mime) << "\",\"size\":" << static_cast<long long>(it.size)
           << ",\"kind\":\"" << kindName(it.kind) << "\"}";
    }
    os << "]";
    return os.str();
}

// ---- parser JSON صغير: مصفوفة كائنات مسطّحة (نصوص/أرقام) كما يرسلها المضيف ---------------------
namespace detail {
struct Cur { const std::string& s; size_t i = 0; };
inline void ws(Cur& c) { while (c.i < c.s.size() && std::isspace(static_cast<unsigned char>(c.s[c.i]))) c.i++; }
inline bool str(Cur& c, std::string& out) {
    if (c.i >= c.s.size() || c.s[c.i] != '"') return false;
    c.i++; out.clear();
    while (c.i < c.s.size()) {
        char ch = c.s[c.i++];
        if (ch == '"') return true;
        if (ch != '\\') { out += ch; continue; }
        if (c.i >= c.s.size()) return false;
        char e = c.s[c.i++];
        switch (e) {
            case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': {
                if (c.i + 4 > c.s.size()) return false;
                unsigned cp = 0;
                for (int k = 0; k < 4; k++) {
                    char h = c.s[c.i++]; cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= h - '0';
                    else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    else return false;
                }
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                break;
            }
            default: out += e;
        }
    }
    return false;
}
} // namespace detail

inline bool parseItemsJson(const std::string& json, std::vector<Item>& out, std::string& err) {
    using namespace detail;
    out.clear();
    Cur c{json};
    ws(c);
    if (c.i >= json.size() || json[c.i] != '[') { err = "media items must be a JSON array"; return false; }
    c.i++; ws(c);
    if (c.i < json.size() && json[c.i] == ']') return true;
    while (c.i < json.size()) {
        ws(c);
        if (c.i >= json.size() || json[c.i] != '{') { err = "media item must be an object"; return false; }
        c.i++;
        Item it; bool hasKind = false;
        while (true) {
            ws(c);
            if (c.i < json.size() && json[c.i] == '}') { c.i++; break; }
            std::string key;
            if (!str(c, key)) { err = "bad media item key"; return false; }
            ws(c);
            if (c.i >= json.size() || json[c.i] != ':') { err = "bad media item (missing ':')"; return false; }
            c.i++; ws(c);
            std::string sval; double nval = 0; bool isStr = false;
            if (c.i < json.size() && json[c.i] == '"') {
                if (!str(c, sval)) { err = "bad media item string"; return false; }
                isStr = true;
            } else {
                size_t st = c.i;
                while (c.i < json.size() && (std::isdigit(static_cast<unsigned char>(json[c.i])) || json[c.i]=='-' || json[c.i]=='+' || json[c.i]=='.' || json[c.i]=='e' || json[c.i]=='E')) c.i++;
                if (st == c.i) { err = "unsupported media item value"; return false; }
                nval = std::atof(json.substr(st, c.i - st).c_str());
            }
            if (key == "path" && isStr) it.path = sval;
            else if (key == "name" && isStr) it.name = sval;
            else if (key == "mime" && isStr) it.mime = sval;
            else if (key == "size" && !isStr) it.size = nval;
            else if (key == "kind" && isStr) { Kind k; if (parseKind(sval, k)) { it.kind = k; hasKind = true; } }
            ws(c);
            if (c.i < json.size() && json[c.i] == ',') { c.i++; continue; }
        }
        if (it.name.empty()) { size_t sl = it.path.find_last_of('/'); it.name = sl == std::string::npos ? it.path : it.path.substr(sl + 1); }
        if (it.mime.empty()) it.mime = mimeForName(it.name.empty() ? it.path : it.name);
        if (!hasKind) it.kind = kindFromMime(it.mime);
        out.push_back(std::move(it));
        ws(c);
        if (c.i < json.size() && json[c.i] == ',') { c.i++; continue; }
        if (c.i < json.size() && json[c.i] == ']') return true;
        err = "bad media items array"; return false;
    }
    err = "unterminated media items array";
    return false;
}

// ---- التحقق -------------------------------------------------------------------------------------

// المسار يجب أن يكون نسبياً داخل المشروع: لا "..", لا مسار مطلق، لا مخطط (scheme://)، لا محارف تحكّم.
inline bool safeRelativePath(const std::string& p) {
    if (p.empty() || p.size() > 512) return false;
    if (p[0] == '/' || p[0] == '\\') return false;
    if (p.find("://") != std::string::npos || p.find(':') != std::string::npos) return false;
    for (unsigned char c : p) if (c < 0x20) return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t e = p.find_first_of("/\\", start);
        std::string seg = p.substr(start, e == std::string::npos ? std::string::npos : e - start);
        if (seg == "..") return false;
        if (e == std::string::npos) break;
        start = e + 1;
    }
    return true;
}

inline bool kindAccepts(Kind want, Kind got) { return want == Kind::ANY || want == got; }

// maxMb <= 0 تعني بلا حدّ. يُرجع true إن صلح الملف وإلا يملأ err.
inline bool validateItem(const Item& it, Kind want, double maxMb, std::string& err) {
    if (!safeRelativePath(it.path)) { err = "unsafe media path: " + it.path; return false; }
    if (!kindAccepts(want, it.kind)) {
        err = "'" + it.name + "' is " + kindName(it.kind) + ", expected " + kindName(want);
        return false;
    }
    if (maxMb > 0 && it.size > maxMb * 1024.0 * 1024.0) {
        char b[64]; std::snprintf(b, sizeof b, "%g", maxMb);
        err = "'" + it.name + "' is " + humanSize(it.size) + " (limit " + b + " MB)";
        return false;
    }
    return true;
}

// رفع آمن: http/https فقط (لا file:/content:/javascript:/intent:)، بلا userinfo (user:pass@).
inline bool validateUploadUrl(const std::string& url, std::string& err) {
    std::string l = lower(url);
    size_t hostStart;
    if (l.rfind("https://", 0) == 0) hostStart = 8;
    else if (l.rfind("http://", 0) == 0) hostStart = 7;
    else { err = "upload URL must start with http:// or https://"; return false; }
    size_t hostEnd = url.find_first_of("/?#", hostStart);
    std::string authority = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
    if (authority.empty()) { err = "upload URL has no host"; return false; }
    if (authority.find('@') != std::string::npos) { err = "upload URL must not contain credentials"; return false; }
    for (unsigned char c : url) if (c <= 0x20) { err = "upload URL contains whitespace/control characters"; return false; }
    return true;
}

// ---- خلايا Warp ---------------------------------------------------------------------------------

inline const char* kDerivedSuffixes[] = {"_name","_mime","_size","_sizeText","_kind","_count","_all",
                                         "_status","_progress","_error","_response","_attempt"};

inline void seedCells(WarpScope& w, const std::string& cell) {
    auto ensure = [&](const std::string& k, Value v) { if (!w.has(k)) w.set(k, std::move(v)); };
    ensure(cell, Value::txt(""));
    ensure(cell + "_name", Value::txt(""));
    ensure(cell + "_mime", Value::txt(""));
    ensure(cell + "_size", Value::num(0));
    ensure(cell + "_sizeText", Value::txt(""));
    ensure(cell + "_kind", Value::txt(""));
    ensure(cell + "_count", Value::num(0));
    ensure(cell + "_all", Value::txt("[]"));
    ensure(cell + "_status", Value::txt("idle"));
    ensure(cell + "_progress", Value::num(0));
    ensure(cell + "_error", Value::txt(""));
    ensure(cell + "_response", Value::txt(""));
    ensure(cell + "_attempt", Value::num(0));
}

inline void setIfChanged(WarpScope& w, const std::string& k, Value v, std::vector<std::string>& changed) {
    if (w.has(k) && w.get(k) == v) return;
    w.set(k, std::move(v));
    changed.push_back(k);
}

inline std::vector<Item> itemsFromCell(const WarpScope& w, const std::string& cell) {
    std::vector<Item> items; std::string err;
    if (w.has(cell + "_all")) parseItemsJson(w.get(cell + "_all").asString(), items, err);
    return items;
}

inline std::vector<std::string> writeSelection(WarpScope& w, const std::string& cell, const std::vector<Item>& items) {
    std::vector<std::string> ch;
    seedCells(w, cell);
    const Item* first = items.empty() ? nullptr : &items[0];
    setIfChanged(w, cell, Value::txt(first ? first->path : ""), ch);
    setIfChanged(w, cell + "_name", Value::txt(first ? first->name : ""), ch);
    setIfChanged(w, cell + "_mime", Value::txt(first ? first->mime : ""), ch);
    setIfChanged(w, cell + "_size", Value::num(first ? first->size : 0), ch);
    setIfChanged(w, cell + "_sizeText", Value::txt(first ? humanSize(first->size) : ""), ch);
    setIfChanged(w, cell + "_kind", Value::txt(first ? kindName(first->kind) : ""), ch);
    setIfChanged(w, cell + "_count", Value::num(static_cast<double>(items.size())), ch);
    setIfChanged(w, cell + "_all", Value::txt(itemsToJson(items)), ch);
    setIfChanged(w, cell + "_status", Value::txt(items.empty() ? "idle" : "picked"), ch);
    setIfChanged(w, cell + "_progress", Value::num(0), ch);
    setIfChanged(w, cell + "_error", Value::txt(""), ch);
    setIfChanged(w, cell + "_response", Value::txt(""), ch);
    setIfChanged(w, cell + "_attempt", Value::num(0), ch);
    return ch;
}

inline std::vector<std::string> writeError(WarpScope& w, const std::string& cell, const std::string& msg) {
    std::vector<std::string> ch;
    seedCells(w, cell);
    setIfChanged(w, cell + "_status", Value::txt("error"), ch);
    setIfChanged(w, cell + "_error", Value::txt(msg), ch);
    return ch;
}

// نتيجة المنتقي من المضيف: يتحقق من كل ملف؛ أول ملف مرفوض يضع الحالة error دون المساس بالاختيار السابق.
// `append` = true يضيف إلى الاختيار الحالي (للاختيار المتعدد المتتابع).
inline std::vector<std::string> applyPicked(WarpScope& w, const std::string& cell, const std::string& itemsJson,
                                            Kind want, double maxMb, bool multiple, bool append, std::string& err) {
    std::vector<Item> items;
    if (!parseItemsJson(itemsJson, items, err)) return writeError(w, cell, err);
    if (!multiple && items.size() > 1) items.resize(1);
    for (auto& it : items) {
        if (!validateItem(it, want, maxMb, err)) return writeError(w, cell, err);
    }
    if (append) {
        std::vector<Item> merged = itemsFromCell(w, cell);
        for (auto& it : items) merged.push_back(it);
        items.swap(merged);
        if (!multiple && items.size() > 1) items.erase(items.begin(), items.end() - 1);
    }
    return writeSelection(w, cell, items);
}

inline std::vector<std::string> clearSelection(WarpScope& w, const std::string& cell) {
    return writeSelection(w, cell, {});
}

// حذف الملف رقم index (يبدأ من 0) من الاختيار المتعدد. false إن خرج الفهرس عن النطاق.
inline bool removeAt(WarpScope& w, const std::string& cell, int index, std::vector<std::string>& changed) {
    std::vector<Item> items = itemsFromCell(w, cell);
    if (index < 0 || index >= static_cast<int>(items.size())) return false;
    items.erase(items.begin() + index);
    changed = writeSelection(w, cell, items);
    return true;
}

// تقدّم الرفع من المضيف. status: uploading|done|error. progress 0..100 يُقصّ تلقائياً.
inline std::vector<std::string> applyProgress(WarpScope& w, const std::string& cell, const std::string& status,
                                              double progress, const std::string& detail) {
    std::vector<std::string> ch;
    seedCells(w, cell);
    std::string st = lower(status);
    if (st != "uploading" && st != "done" && st != "error" && st != "picked" && st != "idle") st = "error";
    progress = std::max(0.0, std::min(100.0, progress));
    if (st == "done") progress = 100;
    setIfChanged(w, cell + "_status", Value::txt(st), ch);
    setIfChanged(w, cell + "_progress", Value::num(progress), ch);
    setIfChanged(w, cell + "_error", Value::txt(st == "error" ? detail : ""), ch);
    setIfChanged(w, cell + "_response", Value::txt(st == "done" ? detail : ""), ch);
    // أثناء uploading يحمل detail رقم المحاولة (1 = الأولى، 2+ = إعادة محاولة) -> _attempt.
    if (st == "uploading") {
        int attempt = std::atoi(detail.c_str());
        setIfChanged(w, cell + "_attempt", Value::num(attempt > 0 ? attempt : 1), ch);
    } else if (st == "idle" || st == "picked") {
        setIfChanged(w, cell + "_attempt", Value::num(0), ch);
    }
    return ch;
}

// ---- طلبات المضيف -------------------------------------------------------------------------------

struct PickRequest {
    Kind kind = Kind::ANY;
    std::string cell;
    bool multiple = false;
    bool append = false;   // "append": كل اختيار جديد يُضاف إلى السابق بدل أن يستبدله
    double maxMb = 0;
    std::string source = "library"; // library | camera (الكاميرا: التقاط واحد، لا صلاحية مطلوبة)
    int maxDim = 0;        // تصغير الصور: أطول ضلع بالبكسل بعد المعالجة (0 = بلا تصغير)
    int quality = 85;      // جودة JPEG 1..100 عند التصغير/الضغط
};

struct UploadRequest {
    std::string cell;
    std::string url;
    std::string field = "file";
    int retries = 2;       // إعادة محاولة تلقائية عند فشل الشبكة أو 5xx/408/429 (0..5؛ 0 = معطَّلة)
    std::string auth;      // اختياري: قيمة ترويسة Authorization (مثل "Bearer xyz") — لا تُسجَّل ولا تُخزَّن في Warp
    std::string itemsJson = "[]";
};

inline std::string pickRequestJson(const PickRequest& r) {
    std::ostringstream os;
    os << "{\"op\":\"pick\",\"kind\":\"" << kindName(r.kind) << "\",\"cell\":\"" << jsonEsc(r.cell)
       << "\",\"multiple\":" << (r.multiple ? "true" : "false") << ",\"append\":" << (r.append ? "true" : "false") << ",\"maxMb\":" << r.maxMb
       << ",\"source\":\"" << r.source << "\",\"maxDim\":" << r.maxDim << ",\"quality\":" << r.quality << "}";
    return os.str();
}
inline std::string uploadRequestJson(const UploadRequest& r) {
    std::ostringstream os;
    os << "{\"op\":\"upload\",\"cell\":\"" << jsonEsc(r.cell) << "\",\"url\":\"" << jsonEsc(r.url)
       << "\",\"field\":\"" << jsonEsc(r.field) << "\",\"auth\":\"" << jsonEsc(r.auth) << "\",\"retries\":" << r.retries << ",\"items\":" << r.itemsJson << "}";
    return os.str();
}

inline std::string cancelRequestJson(const std::string& cell) {
    return "{\"op\":\"cancel\",\"cell\":\"" + jsonEsc(cell) + "\"}";
}

// قيمة Authorization: بلا CR/LF (حقن ترويسات) وبطول معقول.
inline bool validateAuth(const std::string& a, std::string& err) {
    if (a.size() > 2048) { err = "auth value too long"; return false; }
    for (unsigned char c : a) if (c < 0x20 || c == 0x7f) { err = "auth value contains control characters"; return false; }
    return true;
}

// ---- زرع الخلايا تلقائياً من شجرة @view ---------------------------------------------------------
// ليظهر `text=photo_status` كـ "idle" منذ أول رسم (لا "{{photo_status}}"). يمسح كل on*= ويجد
// pickMedia(kind, cell)/uploadMedia(cell, url)/clearMedia(cell)/removeMedia(cell, i).
inline void collectMediaCells(const std::shared_ptr<rin::ViewStmt>& v, std::vector<std::string>& out) {
    if (!v) return;
    for (auto& a : v->attrs) {
        auto call = std::dynamic_pointer_cast<rin::CallExpr>(a.value);
        if (!call) continue;
        size_t cellArg;
        if (call->callee == "pickMedia") cellArg = 1;
        else if (call->callee == "uploadMedia" || call->callee == "clearMedia" || call->callee == "removeMedia" || call->callee == "cancelUpload") cellArg = 0;
        else continue;
        if (cellArg < call->args.size())
            if (auto var = std::dynamic_pointer_cast<rin::VariableExpr>(call->args[cellArg]))
                out.push_back(var->name);
    }
    for (auto& c : v->children) collectMediaCells(c, out);
}

inline void seedMediaCellsFromView(const std::shared_ptr<rin::ViewStmt>& root, WarpScope& w) {
    std::vector<std::string> cells;
    collectMediaCells(root, cells);
    for (auto& c : cells) seedCells(w, c);
}

} // namespace media
} // namespace indsin
