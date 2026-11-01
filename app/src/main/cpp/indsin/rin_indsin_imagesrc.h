// indsin/rin_indsin_imagesrc.h — Image Sources: where an <Image>/<Avatar> gets its pixels from.
//
// Concepts (docs/indsin_image_sources.md):
//   Src         src="…"                 the general attribute; its *kind* is detected from the value
//   SrcSet      srcset="a.png 1x, b.png 2x"  /  "s.jpg 400w, l.jpg 1200w"   -> best candidate for the slot
//   URL         https://…  (also //cdn…)           network image (host downloads + caches)
//   RemotePath  remotePath="/img/a.png" base="https://cdn.x.com"  -> joined into a URL
//   LocalPath   localPath="photos/a.png" | file://… | a bare relative path  (project/app sandbox; ".." refused)
//   Base64      base64="iVBOR…" [mime=…]       raw base64 payload; MIME sniffed from the magic bytes
//   DataURI     data:image/png;base64,…        inline image (only image/* types are accepted)
//   Blob        blob:… / blob="blob:…"         runtime object handle produced by the host
//   Upload      uploadImage(cell,url)          alias of uploadMedia() (rin_indsin_media.h)
//   Download    downloadImage(url[,name[,cell]]) request to the host, progress in <cell>_status/_progress
//
// Like media.h the engine never touches the network or disk: this header only *classifies, validates and
// normalizes* the source so every host (Android, desktop, CLI, HTML export, the JS translator) receives one
// renderable `src` plus diagnostic attrs, and the host performs the actual I/O.
//
// Written onto the Strand by applyImageSourceConveniences():
//   src          renderable value (URL / data URI / blob: / cleaned local path)   <- what hosts read, as before
//   src_kind     url | remote_path | local_path | data_uri | base64 | blob | none
//   src_from     which attribute supplied it (src, srcset, url, remotePath, localPath, base64, dataUri, blob, fallback)
//   src_mime     image/png … (data/base64 sources, or guessed from the file extension)
//   src_bytes    decoded payload size (data/base64 only)
//   src_error    non-empty when the source was refused (bad scheme, traversal, oversize, not an image…)
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace indsin {
namespace imagesrc {

enum class SourceKind { NONE, URL, REMOTE_PATH, LOCAL_PATH, DATA_URI, BASE64, BLOB };
inline const char* kindName(SourceKind k) {
    switch (k) {
        case SourceKind::URL: return "url";
        case SourceKind::REMOTE_PATH: return "remote_path";
        case SourceKind::LOCAL_PATH: return "local_path";
        case SourceKind::DATA_URI: return "data_uri";
        case SourceKind::BASE64: return "base64";
        case SourceKind::BLOB: return "blob";
        default: return "none";
    }
}

constexpr size_t kMaxInlineBytes = 8u * 1024u * 1024u; // refuse inline payloads above 8 MiB

// Device-pixel ratio used by srcset x-descriptors. The host may set it (default 1).
inline double& density() { static thread_local double d = 1.0; return d; }

inline std::string lowerStr(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline std::string trimStr(const std::string& in) {
    size_t a = in.find_first_not_of(" \t\r\n"), b = in.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : in.substr(a, b - a + 1);
}
inline bool startsWithI(const std::string& s, const char* p) { return lowerStr(s.substr(0, std::char_traits<char>::length(p))) == p; }

// ---- base64 -------------------------------------------------------------------------------------
inline int b64Val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;   // '-' = url-safe alphabet
    if (c == '/' || c == '_') return 63;   // '_' = url-safe alphabet
    return -1;
}
// Strips whitespace/newlines (MIME-wrapped base64 is common). Returns false on any foreign character.
inline bool cleanBase64(const std::string& in, std::string& out) {
    out.clear();
    for (unsigned char c : in) {
        if (std::isspace(c)) continue;
        if (c == '=' || b64Val(c) >= 0) out += (char)c; else return false;
    }
    size_t pad = 0; while (pad < out.size() && out[out.size() - 1 - pad] == '=') ++pad;
    if (pad > 2) return false;
    for (size_t i = 0; i + pad < out.size(); ++i) if (out[i] == '=') return false; // '=' only as trailing padding
    size_t body = out.size() - pad;
    if (body == 0 || body % 4 == 1) return false;   // impossible length
    if (pad && out.size() % 4 != 0) return false;   // padded input must be a multiple of 4
    return true;
}
inline size_t base64DecodedSize(const std::string& clean) {
    size_t pad = 0; while (pad < clean.size() && clean[clean.size() - 1 - pad] == '=') ++pad;
    size_t body = clean.size() - pad;
    return body * 3 / 4;
}
// Decodes (at most) the first `maxBytes` bytes — enough for magic-number sniffing without decoding megabytes.
inline std::string base64Prefix(const std::string& clean, size_t maxBytes = 16) {
    std::string out; unsigned buf = 0; int bits = 0;
    for (unsigned char c : clean) {
        if (c == '=') break;
        buf = (buf << 6) | (unsigned)b64Val(c); bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); if (out.size() >= maxBytes) break; }
    }
    return out;
}
inline std::string sniffMime(const std::string& head) {
    auto at = [&](size_t i) { return i < head.size() ? (unsigned char)head[i] : 0u; };
    if (head.size() >= 8 && at(0) == 0x89 && head.compare(1, 3, "PNG") == 0) return "image/png";
    if (at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF) return "image/jpeg";
    if (head.compare(0, 4, "GIF8") == 0) return "image/gif";
    if (head.size() >= 12 && head.compare(0, 4, "RIFF") == 0 && head.compare(8, 4, "WEBP") == 0) return "image/webp";
    if (head.size() >= 12 && head.compare(4, 4, "ftyp") == 0 && (head.compare(8, 4, "avif") == 0 || head.compare(8, 4, "avis") == 0)) return "image/avif";
    if (head.compare(0, 2, "BM") == 0) return "image/bmp";
    if (head.compare(0, 4, "<svg") == 0 || head.compare(0, 5, "<?xml") == 0) return "image/svg+xml";
    return "";
}
inline std::string mimeFromExtension(const std::string& path) {
    size_t q = path.find_first_of("?#");
    std::string p = lowerStr(q == std::string::npos ? path : path.substr(0, q));
    size_t dot = p.rfind('.');
    if (dot == std::string::npos) return "";
    std::string e = p.substr(dot + 1);
    if (e == "png") return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif") return "image/gif";
    if (e == "webp") return "image/webp";
    if (e == "avif") return "image/avif";
    if (e == "bmp") return "image/bmp";
    if (e == "svg") return "image/svg+xml";
    return "";
}
inline bool isImageMime(const std::string& m) {
    static const char* ok[] = {"image/png", "image/jpeg", "image/jpg", "image/gif", "image/webp", "image/avif", "image/bmp", "image/svg+xml"};
    std::string l = lowerStr(m);
    for (auto* o : ok) if (l == o) return true;
    return false;
}

// ---- data: URIs ---------------------------------------------------------------------------------
struct DataUri { bool ok = false; std::string mime; bool base64 = false; std::string payload; std::string error; };
inline DataUri parseDataUri(const std::string& uri) {
    DataUri d;
    if (!startsWithI(uri, "data:")) { d.error = "not a data: URI"; return d; }
    size_t comma = uri.find(',');
    if (comma == std::string::npos) { d.error = "malformed data: URI (no ',')"; return d; }
    std::string meta = trimStr(uri.substr(5, comma - 5));
    d.payload = uri.substr(comma + 1);
    size_t semi = meta.find(';');
    d.mime = lowerStr(trimStr(semi == std::string::npos ? meta : meta.substr(0, semi)));
    if (semi != std::string::npos) {
        size_t pos = semi;
        while (pos != std::string::npos) {
            size_t nx = meta.find(';', pos + 1);
            std::string part = lowerStr(trimStr(meta.substr(pos + 1, nx == std::string::npos ? std::string::npos : nx - pos - 1)));
            if (part == "base64") d.base64 = true;
            pos = nx;
        }
    }
    d.ok = true;
    return d;
}

// ---- classification -----------------------------------------------------------------------------
// Scheme (letters/digits/+.-) followed by ':' — "C:\x" has a 1-char scheme and is treated as a path.
inline bool schemeOf(const std::string& s, std::string& scheme) {
    size_t i = 0;
    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.')) ++i;
    if (i < 2 || i >= s.size() || s[i] != ':' || !std::isalpha((unsigned char)s[0])) return false;
    scheme = lowerStr(s.substr(0, i));
    return true;
}
inline SourceKind classify(const std::string& raw, bool hasBase = false) {
    std::string s = trimStr(raw);
    if (s.empty()) return SourceKind::NONE;
    std::string scheme;
    if (schemeOf(s, scheme)) {
        if (scheme == "http" || scheme == "https") return SourceKind::URL;
        if (scheme == "data") return SourceKind::DATA_URI;
        if (scheme == "blob") return SourceKind::BLOB;
        if (scheme == "file") return SourceKind::LOCAL_PATH;
        return SourceKind::NONE; // javascript:, ftp:, … are never image sources
    }
    if (s.compare(0, 2, "//") == 0) return SourceKind::URL;           // protocol-relative
    if (s[0] == '/' && hasBase) return SourceKind::REMOTE_PATH;        // server-relative once a base is known
    return SourceKind::LOCAL_PATH;
}
// Normalizes a local path: file:// stripped, '\' -> '/', "./" folded, any ".." segment refused.
inline bool normalizeLocalPath(const std::string& in, std::string& out, std::string& err) {
    std::string p = trimStr(in);
    if (startsWithI(p, "file://")) { p = p.substr(7); if (p.compare(0, 9, "localhost") == 0) p = p.substr(9); }
    for (auto& c : p) if (c == '\\') c = '/';
    for (unsigned char c : p) if (c < 0x20) { err = "path contains control characters"; return false; }
    std::vector<std::string> keep; std::string seg;
    bool abs = !p.empty() && p[0] == '/';
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            if (seg == "..") { err = "path escapes the project root ('..' is not allowed)"; return false; }
            if (!seg.empty() && seg != ".") keep.push_back(seg);
            seg.clear();
        } else seg += p[i];
    }
    out = abs ? "/" : "";
    for (size_t i = 0; i < keep.size(); ++i) out += (i ? "/" : "") + keep[i];
    if (out.empty() || out == "/") { err = "empty path"; return false; }
    return true;
}
inline std::string joinUrl(const std::string& base, const std::string& path) {
    std::string b = trimStr(base), p = trimStr(path);
    while (!b.empty() && b.back() == '/') b.pop_back();
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    return b + "/" + p;
}

// ---- srcset -------------------------------------------------------------------------------------
struct Candidate { std::string url; double w = 0; double x = 0; };
// HTML-style: "url [descriptor], url [descriptor]". The URL is the whitespace-delimited token, so data:
// URIs (which contain commas) survive. Descriptors: "400w" (width) or "2x" (density); none = 1x.
inline std::vector<Candidate> parseSrcSet(const std::string& s) {
    std::vector<Candidate> out; size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && (std::isspace((unsigned char)s[i]) || s[i] == ',')) ++i;
        if (i >= n) break;
        size_t st = i;
        while (i < n && !std::isspace((unsigned char)s[i])) ++i;
        Candidate c; c.url = s.substr(st, i - st);
        bool endedByComma = false;
        if (c.url.size() > 1 && c.url.back() == ',' && !startsWithI(c.url, "data:")) {
            while (!c.url.empty() && c.url.back() == ',') c.url.pop_back();
            endedByComma = true;
        }
        if (!endedByComma) {
            while (i < n && std::isspace((unsigned char)s[i])) ++i;
            size_t ds = i; int depth = 0;
            while (i < n && (s[i] != ',' || depth)) { if (s[i] == '(') ++depth; else if (s[i] == ')' && depth) --depth; ++i; }
            std::string d = lowerStr(trimStr(s.substr(ds, i - ds)));
            if (!d.empty()) {
                char u = d.back(); std::string num = d.substr(0, d.size() - 1);
                try {
                    size_t used = 0; double v = std::stod(num, &used);
                    if (used == num.size() && v > 0) { if (u == 'w') c.w = v; else if (u == 'x') c.x = v; }
                } catch (...) {}
            }
        }
        if (!c.url.empty()) out.push_back(c);
    }
    return out;
}
// Chooses the candidate for a slot `slotWidth` css-px wide at density `dpr`:
//   width descriptors  -> smallest w >= slotWidth*dpr, else the largest
//   density descriptors-> smallest x >= dpr, else the largest   (no descriptor counts as 1x)
inline const Candidate* pickCandidate(const std::vector<Candidate>& c, double slotWidth, double dpr) {
    if (c.empty()) return nullptr;
    bool anyW = false; for (auto& k : c) if (k.w > 0) anyW = true;
    const Candidate* best = nullptr; const Candidate* largest = nullptr;
    if (anyW && slotWidth > 0) {
        double need = slotWidth * std::max(0.5, dpr);
        for (auto& k : c) if (k.w > 0) {
            if (!largest || k.w > largest->w) largest = &k;
            if (k.w >= need && (!best || k.w < best->w)) best = &k;
        }
        return best ? best : largest;
    }
    for (auto& k : c) {
        double x = k.x > 0 ? k.x : 1.0; // a w-only list with no slot width falls through here as 1x
        double bx = best ? (best->x > 0 ? best->x : 1.0) : 0, lx = largest ? (largest->x > 0 ? largest->x : 1.0) : 0;
        if (!largest || x > lx) largest = &k;
        if (x >= dpr && (!best || x < bx)) best = &k;
    }
    return best ? best : largest;
}

// ---- resolution ---------------------------------------------------------------------------------
struct Resolved {
    SourceKind kind = SourceKind::NONE;
    std::string uri, mime, from, error;
    size_t bytes = 0;
};

inline Resolved resolveValue(const std::string& raw, const std::string& from, const std::string& base, const std::string& mimeHint) {
    Resolved r; r.from = from;
    std::string s = trimStr(raw);
    if (s.empty()) return r;
    SourceKind k = classify(s, !base.empty());
    if (k == SourceKind::NONE) { r.error = "unsupported image source '" + s.substr(0, 40) + "' (only http(s), data:image, blob:, file and relative paths are allowed)"; return r; }
    r.kind = k;
    switch (k) {
        case SourceKind::URL:
            r.uri = s; r.mime = mimeFromExtension(s); break;
        case SourceKind::BLOB:
            r.uri = s; r.mime = mimeHint; break;
        case SourceKind::REMOTE_PATH:
            r.uri = joinUrl(base, s); r.mime = mimeFromExtension(s); break;
        case SourceKind::LOCAL_PATH: {
            std::string p, err;
            if (!normalizeLocalPath(s, p, err)) { r.kind = SourceKind::NONE; r.error = err; return r; }
            r.uri = p; r.mime = mimeFromExtension(p); break;
        }
        case SourceKind::DATA_URI: {
            DataUri d = parseDataUri(s);
            if (!d.ok) { r.kind = SourceKind::NONE; r.error = d.error; return r; }
            std::string mime = d.mime;
            if (d.base64) {
                std::string clean;
                if (!cleanBase64(d.payload, clean)) { r.kind = SourceKind::NONE; r.error = "data: URI has invalid base64"; return r; }
                r.bytes = base64DecodedSize(clean);
                if (mime.empty() || mime == "application/octet-stream") mime = sniffMime(base64Prefix(clean));
                if (r.bytes > kMaxInlineBytes) { r.kind = SourceKind::NONE; r.error = "inline image is larger than 8 MiB"; return r; }
                r.uri = "data:" + mime + ";base64," + clean;
            } else { r.bytes = d.payload.size(); r.uri = s; }
            if (!isImageMime(mime)) { r.kind = SourceKind::NONE; r.uri.clear(); r.error = "data: URI is not an image (" + (mime.empty() ? std::string("unknown type") : mime) + ")"; return r; }
            r.mime = mime; break;
        }
        default: break;
    }
    return r;
}
inline Resolved resolveBase64(const std::string& raw, const std::string& mimeHint) {
    Resolved r; r.from = "base64";
    std::string s = trimStr(raw), clean;
    if (s.empty()) return r;
    if (startsWithI(s, "data:")) return resolveValue(s, "base64", "", mimeHint); // forgiving: a full data URI in base64=
    if (!cleanBase64(s, clean)) { r.error = "base64= is not valid base64"; return r; }
    r.kind = SourceKind::BASE64; r.bytes = base64DecodedSize(clean);
    if (r.bytes > kMaxInlineBytes) { r.kind = SourceKind::NONE; r.error = "inline image is larger than 8 MiB"; return r; }
    std::string mime = mimeHint.empty() ? sniffMime(base64Prefix(clean)) : lowerStr(mimeHint);
    if (mime.empty()) { r.kind = SourceKind::NONE; r.error = "cannot tell the image type from the bytes; add mime=\"image/png\""; return r; }
    if (!isImageMime(mime)) { r.kind = SourceKind::NONE; r.error = "mime= must be an image type, got " + mime; return r; }
    r.mime = mime; r.uri = "data:" + mime + ";base64," + clean;
    return r;
}

// Precedence: srcset > src > url > dataUri > base64 > blob > remotePath > localPath/path. The first source
// that *resolves* wins; a refused one is remembered as the error unless a later source succeeds, and
// fallback= (or placeholder=) is the last resort.
inline Resolved resolveStrand(const Strand& s, double slotWidth, double dpr) {
    std::string base = s.attrStr("base", s.attrStr("origin", ""));
    std::string mimeHint = s.attrStr("mime", s.attrStr("type", ""));
    Resolved firstErr; bool haveErr = false;
    auto consider = [&](Resolved r, Resolved& out) {
        if (r.kind != SourceKind::NONE) { out = r; return true; }
        if (!r.error.empty() && !haveErr) { firstErr = r; haveErr = true; }
        return false;
    };
    Resolved out;
    if (s.attr("srcset")) {
        auto cands = parseSrcSet(s.attrStr("srcset", ""));
        if (const Candidate* c = pickCandidate(cands, slotWidth, dpr))
            if (consider(resolveValue(c->url, "srcset", base, mimeHint), out)) return out;
    }
    if (s.attr("src") && consider(resolveValue(s.attrStr("src", ""), "src", base, mimeHint), out)) return out;
    if (s.attr("url") && consider(resolveValue(s.attrStr("url", ""), "url", base, mimeHint), out)) return out;
    if (s.attr("dataUri") && consider(resolveValue(s.attrStr("dataUri", ""), "dataUri", base, mimeHint), out)) return out;
    if (s.attr("base64") && consider(resolveBase64(s.attrStr("base64", ""), mimeHint), out)) return out;
    if (s.attr("blob") && consider(resolveValue(s.attrStr("blob", ""), "blob", base, mimeHint), out)) return out;
    if (s.attr("remotePath")) {
        std::string rp = s.attrStr("remotePath", "");
        Resolved r;
        if (base.empty()) { r.error = "remotePath= needs base=\"https://host\""; }
        else { r = resolveValue(joinUrl(base, rp), "remotePath", "", mimeHint); r.from = "remotePath"; }
        if (consider(r, out)) return out;
    }
    for (const char* k : {"localPath", "path"}) {
        if (!s.attr(k)) continue;
        std::string v = s.attrStr(k, "");
        std::string sch;
        Resolved r = schemeOf(v, sch) ? resolveValue(v, k, base, mimeHint) : resolveValue("file://" + v, k, "", mimeHint); // an explicit scheme wins
        r.from = k;
        if (consider(r, out)) return out;
    }
    for (const char* k : {"fallback", "placeholder"})
        if (s.attr(k) && consider(resolveValue(s.attrStr(k, ""), "fallback", base, mimeHint), out)) {
            if (haveErr) out.error = firstErr.error; // report why the primary source was refused
            return out;
        }
    return haveErr ? firstErr : Resolved{};
}

inline void setAttr(const StrandPtr& s, const std::string& k, const std::string& v) {
    for (auto& a : s->attrs) if (a.key == k) { a.value = Value::txt(v); return; }
    s->attrs.push_back({k, nullptr, Value::txt(v)});
}
inline void clearAttr(const StrandPtr& s, const std::string& k) {
    s->attrs.erase(std::remove_if(s->attrs.begin(), s->attrs.end(), [&](const ResolvedAttr& a) { return a.key == k && !a.rawExpr; }), s->attrs.end());
}
// Resolves one Image/Avatar strand in place (idempotent: reads the same inputs every time, so the Shuttle
// can call it again after a Warp change re-evaluated `src=<cell>`).
inline void normalizeStrand(const StrandPtr& s, double viewportW = 0) {
    if (s->kind != StrandKind::IMAGE && s->kind != StrandKind::AVATAR) return;
    static const char* inputs[] = {"src", "srcset", "url", "dataUri", "base64", "blob", "remotePath", "localPath", "path", "fallback", "placeholder"};
    bool any = false; for (auto* k : inputs) if (s->attr(k)) any = true;
    if (!any) return;
    double w = s->attrNum("width", 0);
    if (w <= 0) w = s->attrNum("size", 0);
    if (w <= 0) w = viewportW > 0 ? viewportW : viewportWidthPx();
    double dpr = s->attrNum("dpr", density());
    Resolved r = resolveStrand(*s, w, dpr);
    setAttr(s, "src_kind", kindName(r.kind));
    setAttr(s, "src_from", r.from);
    if (!r.uri.empty()) setAttr(s, "src", r.uri);
    else if (r.kind == SourceKind::NONE && s->attr("src") && !s->attr("srcset")) setAttr(s, "src", ""); // refused: never hand a hostile value on
    if (!r.mime.empty()) setAttr(s, "src_mime", r.mime); else clearAttr(s, "src_mime");
    if (r.bytes) setAttr(s, "src_bytes", std::to_string(r.bytes)); else clearAttr(s, "src_bytes");
    if (!r.error.empty()) setAttr(s, "src_error", r.error); else clearAttr(s, "src_error");
}
inline void applyAll(const StrandPtr& s, double viewportW) {
    normalizeStrand(s, viewportW);
    for (auto& c : s->children) applyAll(c, viewportW);
}

// ---- download request (host performs the I/O, like media::UploadRequest) -------------------------
struct DownloadRequest { std::string url, filename, cell; };
inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}
inline std::string downloadRequestJson(const DownloadRequest& r) {
    return "{\"op\":\"download\",\"url\":\"" + jsonEscape(r.url) + "\",\"filename\":\"" + jsonEscape(r.filename) +
           "\",\"cell\":\"" + jsonEscape(r.cell) + "\"}";
}
// Accepts http(s) URLs, image data: URIs and blob: handles. Everything else (file:, javascript:, …) is refused.
inline bool validateDownload(const std::string& url, std::string& err) {
    std::string s = trimStr(url);
    if (s.empty()) { err = "downloadImage(): empty source"; return false; }
    for (unsigned char c : s) if (c < 0x20) { err = "downloadImage(): source contains control characters"; return false; }
    SourceKind k = classify(s, false);
    if (k == SourceKind::URL) {
        std::string l = lowerStr(s); size_t hs = l.rfind("https://", 0) == 0 ? 8 : l.rfind("http://", 0) == 0 ? 7 : 2;
        size_t he = s.find_first_of("/?#", hs); std::string auth = s.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
        if (auth.empty()) { err = "downloadImage(): URL has no host"; return false; }
        if (auth.find('@') != std::string::npos) { err = "downloadImage(): URL must not contain credentials"; return false; }
        return true;
    }
    if (k == SourceKind::DATA_URI) { Resolved r = resolveValue(s, "src", "", ""); if (r.kind == SourceKind::NONE) { err = "downloadImage(): " + r.error; return false; } return true; }
    if (k == SourceKind::BLOB) return true;
    err = "downloadImage(): only http(s) URLs, data:image URIs and blob: handles can be downloaded";
    return false;
}
inline std::string safeFilename(const std::string& in) {
    std::string o;
    for (unsigned char c : in) o += (std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ' || c >= 0x80) ? (char)c : '_';
    while (!o.empty() && (o.front() == '.' || o.front() == ' ')) o.erase(o.begin());
    if (o.size() > 128) o.resize(128);
    return o;
}
// Name a download gets when none is given: last URL path segment, else image.<ext from mime>.
inline std::string defaultFilename(const std::string& url) {
    std::string s = trimStr(url);
    if (classify(s) == SourceKind::URL) {
        size_t q = s.find_first_of("?#"); std::string p = q == std::string::npos ? s : s.substr(0, q);
        size_t sl = p.rfind('/'); std::string last = sl == std::string::npos ? "" : p.substr(sl + 1);
        if (!last.empty() && last.find('.') != std::string::npos) return safeFilename(last);
    }
    std::string mime = classify(s) == SourceKind::DATA_URI ? resolveValue(s, "src", "", "").mime : mimeFromExtension(s);
    std::string ext = mime == "image/jpeg" ? "jpg" : mime == "image/svg+xml" ? "svg" : mime.rfind("image/", 0) == 0 ? mime.substr(6) : "png";
    return "image." + ext;
}

} // namespace imagesrc

inline void applyImageSourceConveniences(const StrandPtr& fabric) {
    imagesrc::applyAll(fabric, viewportWidthPx());
}

} // namespace indsin
