// clc_identity.h — «هوية التطبيق/الموقع» (App/Site Identity) داخل حاويات Rin و CLC.
//
// يجمع في بنية واحدة ما يلزم لتسمية تطبيق أو موقع وتعريفه: الاسم، الاسم المختصر، المعرّف، الإصدار
// (semver + رقم بناء + قناة)، الأيقونات (أحجام/أنواع/غرض)، الوصف (طويل/مختصر)، ونصوص محركات البحث (SEO:
// عنوان، وصف، كلمات مفتاحية، canonical، robots، Open Graph، Twitter، لغات بديلة، JSON-LD).
//
// header-only عمداً (لا ملفات .cpp جديدة في أنظمة البناء: CMake/Gradle/Makefile/build_from_source.sh).
//
// التخزين داخل ملف .rcl: بلا أي تغيير على التخطيط الثنائي (CLC 1.0 كما هو). الهوية تُحمَل في الحقل
// القائم Metadata::extra بمفاتيح محجوزة ببادئة «app.» و«seo.» و«icon.N.» — قارئ قديم يراها حقولاً
// مخصّصة عادية، وقارئ جديد يعيد بناء AppIdentity كاملة منها (readIdentity). الحقول القديمة
// (name/version/description/author/license) تبقى هي المصدر الأساسي ويُزامَن معها (applyIdentity).
//
// كل النصوص UTF-8 خام (العربية مدعومة بلا أي تحويل).
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "clc_container.h"

namespace clc {

struct IconEntry {
    std::string src;            // مسار نسبي داخل المشروع/الحاوية
    std::string sizes;          // "192x192" أو "any" (SVG) أو "48x48 96x96"
    std::string type;           // image/png ...
    std::string purpose = "any"; // any | maskable | monochrome (أو مزيج بمسافات)
};

struct SeoInfo {
    std::string title;          // <title> ؛ الأمثل 30–60 حرفاً
    std::string description;    // <meta name=description> ؛ الأمثل 70–160 حرفاً
    std::vector<std::string> keywords;
    std::string canonical;      // رابط مطلق
    std::string robots;         // مثال: "index, follow"
    std::string author;
    std::string ogTitle, ogDescription, ogImage, ogType, ogSiteName;
    std::string twitterCard, twitterSite;
    std::string locale;         // og:locale مثل ar_AR
    std::vector<std::pair<std::string, std::string>> alternates; // (lang -> url) لـ hreflang
    bool jsonLd = true;         // يُولّد JSON-LD (WebSite / SoftwareApplication)
};

// ترجمة/تخصيص لغوي للهوية: يُطبَّق فوق الهوية الأساسية عند توليد head/manifest للغة معيّنة (localizeIdentity).
struct LocaleInfo {
    std::string lang;           // ar | en | fr-CA ...
    std::string name, shortName, description, shortDescription;
    std::string seoTitle, seoDescription;
    std::vector<std::string> keywords;
    std::string url;            // رابط هذه اللغة المطلق (canonical + hreflang تلقائي)
    std::string ogImage;
};

// سجل إصدار: يُحفَظ مع الهوية فيبقى تاريخ الإصدارات في الحاوية نفسها لا في ملف مستقل.
struct ReleaseInfo {
    std::string version, date, channel, notes; // notes: أسطر مفصولة بـ '\n'
};

struct AppIdentity {
    std::string kind = "app";   // "app" | "site"
    std::string id;             // معرّف ثابت: com.example.app أو slug
    std::string name;           // الاسم الكامل المعروض
    std::string shortName;      // اسم مختصر (تحت الأيقونة)
    std::string version;        // semver
    int64_t versionCode = 0;    // رقم بناء تصاعدي؛ 0 = يُشتق من version
    std::string channel;        // stable | beta | alpha | dev
    std::string author, publisher, license, homepage;
    std::string lang, dir;      // dir: rtl | ltr (يُشتق من lang عند الفراغ)
    std::string category;
    std::string description;        // وصف كامل
    std::string shortDescription;   // سطر واحد
    std::string themeColor, backgroundColor;
    std::string display, orientation, startUrl, scope;
    std::string favicon;
    std::vector<IconEntry> icons;
    std::string iconsDir;           // مجلد أيقونات يُمسَح تلقائياً (نسبي لـ dir): يُكمل icons بما فيه من PNG/SVG/ICO/WebP
    SeoInfo seo;
    std::vector<LocaleInfo> locales;
    std::vector<ReleaseInfo> releases;
};

struct IdentityIssue {
    std::string level;   // error | warn | info
    std::string code;    // معرّف ثابت قابل للاختبار: seo.title.too_long
    std::string field;
    std::string message; // بالعربية
};

// ------------------------------------------------------------------ أدوات نصية
namespace idn {

inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}
inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// عدد محارف Unicode (لا بايتات) — لأطوال العنوان/الوصف مع العربية.
inline size_t utf8Len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}
inline bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
inline bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
inline std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
inline std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; }
    return o;
}
inline bool isAbsUrl(const std::string& u) { return startsWith(u, "http://") || startsWith(u, "https://"); }

inline std::string htmlEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}
inline std::string jsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '<': o += "\\u003c"; break; // آمن داخل <script type=application/ld+json>
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    return o;
}
inline std::string jstr(const std::string& s) { return "\"" + jsonEsc(s) + "\""; }

// ------------------------------------------------------------------ semver
struct Semver { long major = 0, minor = 0, patch = 0; std::string pre, build; };

inline bool parseSemver(const std::string& v, Semver& out) {
    std::string s = trim(v);
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    size_t plus = s.find('+');
    if (plus != std::string::npos) { out.build = s.substr(plus + 1); s.erase(plus); }
    size_t dash = s.find('-');
    if (dash != std::string::npos) { out.pre = s.substr(dash + 1); s.erase(dash); if (out.pre.empty()) return false; }
    auto parts = split(s, '.');
    if (parts.size() != 3) return false;
    long* dst[3] = {&out.major, &out.minor, &out.patch};
    for (int i = 0; i < 3; ++i) {
        if (parts[i].empty() || parts[i].size() > 9) return false;
        for (char c : parts[i]) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        *dst[i] = std::stol(parts[i]);
    }
    return true;
}
inline std::string semverStr(const Semver& s) {
    std::string o = std::to_string(s.major) + "." + std::to_string(s.minor) + "." + std::to_string(s.patch);
    if (!s.pre.empty()) o += "-" + s.pre;
    if (!s.build.empty()) o += "+" + s.build;
    return o;
}
// "major" | "minor" | "patch" ؛ يُسقط الوسم التمهيدي (pre) ويرفع الباقي. يرمي std::invalid_argument عند خطأ.
inline std::string bumpSemver(const std::string& version, const std::string& part) {
    Semver s;
    if (!parseSemver(version, s)) throw std::invalid_argument("الإصدار '" + version + "' ليس semver صالحاً (major.minor.patch)");
    std::string p = lower(part);
    if (p == "major") { ++s.major; s.minor = 0; s.patch = 0; }
    else if (p == "minor") { ++s.minor; s.patch = 0; }
    else if (p == "patch") { ++s.patch; }
    else throw std::invalid_argument("جزء الإصدار يجب أن يكون major أو minor أو patch");
    s.pre.clear(); s.build.clear();
    return semverStr(s);
}
inline int64_t deriveVersionCode(const std::string& version) {
    Semver s;
    if (!parseSemver(version, s)) return 0;
    return int64_t(s.major) * 1000000 + int64_t(s.minor) * 1000 + int64_t(s.patch);
}

// ------------------------------------------------------------------ أيقونات
inline std::string mimeOfPath(const std::string& p) {
    std::string l = lower(p);
    if (endsWith(l, ".png")) return "image/png";
    if (endsWith(l, ".jpg") || endsWith(l, ".jpeg")) return "image/jpeg";
    if (endsWith(l, ".svg")) return "image/svg+xml";
    if (endsWith(l, ".ico")) return "image/x-icon";
    if (endsWith(l, ".webp")) return "image/webp";
    if (endsWith(l, ".gif")) return "image/gif";
    return "";
}
// يقرأ أبعاد PNG من رأس IHDR (يرجع false إن لم يكن PNG صالحاً).
inline bool probePngSize(const std::string& file, int& w, int& h) {
    std::ifstream in(file, std::ios::binary);
    unsigned char b[24];
    if (!in || !in.read(reinterpret_cast<char*>(b), 24)) return false;
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) if (b[i] != sig[i]) return false;
    w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return w > 0 && h > 0 && w < 65536 && h < 65536;
}
// تخمين الحجم من اسم الملف: icon_192.png / icon-512x512.png → "192x192" / "512x512".
inline std::string guessSizesFromName(const std::string& path) {
    std::string base = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.erase(dot);
    std::vector<long> nums;
    std::string cur;
    for (char c : base + " ") {
        if (std::isdigit(static_cast<unsigned char>(c))) cur += c;
        else { if (!cur.empty() && cur.size() <= 4) nums.push_back(std::stol(cur)); cur.clear(); }
    }
    if (nums.empty()) return "";
    long w = nums[0], h = nums.size() > 1 && base.find('x') != std::string::npos ? nums[1] : nums[0];
    if (w < 16 || w > 4096 || h < 16 || h > 4096) return "";
    return std::to_string(w) + "x" + std::to_string(h);
}
// أكبر بُعد مذكور في sizes ("48x48 192x192" → 192؛ any → -1 لـ SVG).
inline long maxSize(const std::string& sizes) {
    long best = 0;
    for (auto& tok : split(sizes, ' ')) {
        if (tok == "any") return -1;
        size_t x = tok.find('x');
        if (x == std::string::npos) continue;
        try { best = std::max(best, std::stol(tok.substr(0, x))); } catch (...) {}
    }
    return best;
}
// يكمل حقول الأيقونة الناقصة (type/sizes/purpose). baseDir اختياري: إن وُجد يُفحَص الملف الحقيقي (PNG).
inline void completeIcon(IconEntry& ic, const std::string& baseDir = "") {
    if (ic.type.empty()) ic.type = mimeOfPath(ic.src);
    if (ic.purpose.empty()) ic.purpose = lower(ic.src).find("maskable") != std::string::npos ? "maskable" : "any";
    if (ic.sizes.empty()) {
        if (ic.type == "image/svg+xml" || ic.type == "image/x-icon") ic.sizes = "any";
        else {
            int w = 0, h = 0;
            if (!baseDir.empty() && ic.type == "image/png" &&
                probePngSize(baseDir + (baseDir.back() == '/' ? "" : "/") + ic.src, w, h))
                ic.sizes = std::to_string(w) + "x" + std::to_string(h);
            else ic.sizes = guessSizesFromName(ic.src);
        }
    }
}

inline std::string defaultDir(const std::string& lang) {
    std::string l = lower(lang);
    size_t cut = l.find_first_of("-_");
    if (cut != std::string::npos) l.erase(cut);
    static const char* rtl[] = {"ar", "he", "fa", "ur", "ps", "sd", "ug", "yi", "dv", "ku"};
    for (auto r : rtl) if (l == r) return "rtl";
    return l.empty() ? "" : "ltr";
}
inline bool looksHexColor(const std::string& c) {
    if (c.size() != 4 && c.size() != 7 && c.size() != 9) return false;
    if (c[0] != '#') return false;
    for (size_t i = 1; i < c.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(c[i]))) return false;
    return true;
}
inline bool validLang(const std::string& l) {
    if (l.size() < 2 || l.size() > 12) return false;
    auto parts = split(l, '-');
    if (parts[0].size() < 2 || parts[0].size() > 3) return false;
    for (char c : parts[0]) if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    for (auto& p : parts) { if (p.empty()) return false; for (char c : p) if (!std::isalnum(static_cast<unsigned char>(c))) return false; }
    return true;
}

// مقارنة semver: <0 أصغر، 0 متساوٍ، >0 أكبر (الوسم التمهيدي pre أصغر من الإصدار النهائي؛ build يُتجاهل).
inline int cmpSemver(const std::string& x, const std::string& y) {
    Semver a, b;
    bool oa = parseSemver(x, a), ob = parseSemver(y, b);
    if (!oa || !ob) return oa == ob ? x.compare(y) : (oa ? 1 : -1);
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() != b.pre.empty()) return a.pre.empty() ? 1 : -1;
    return a.pre == b.pre ? 0 : (a.pre < b.pre ? -1 : 1);
}

// يمسح مجلد أيقونات (غير تكراري، بلا روابط رمزية ولا ملفات مخفية): PNG/SVG/ICO/WebP، ويرتّبها بالحجم ثم الاسم.
// الأحجام من ترويسة PNG الحقيقية؛ المسارات نسبية لـ baseDir: "<rel>/<file>". مسار غير آمن أو غير موجود ⇒ قائمة فارغة.
inline std::vector<IconEntry> scanIconsDir(const std::string& baseDir, const std::string& rel) {
    namespace fs = std::filesystem;
    std::vector<IconEntry> out;
    std::string r = rel;
    while (!r.empty() && r.back() == '/') r.pop_back();
    if (baseDir.empty() || r.empty() || r[0] == '/' || r.find("..") != std::string::npos || r.find(":\\") != std::string::npos) return out;
    std::error_code ec;
    fs::path d = fs::path(baseDir) / r;
    if (!fs::is_directory(d, ec)) return out;
    std::vector<std::pair<std::pair<long, std::string>, IconEntry>> items;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec) || !it->is_regular_file(ec)) continue;
        std::string n = it->path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        std::string t = mimeOfPath(n);
        if (t.empty() || t == "image/jpeg" || t == "image/gif") continue; // أيقونات: لا JPEG/GIF
        IconEntry ic; ic.src = r + "/" + n;
        completeIcon(ic, baseDir);
        long m = maxSize(ic.sizes);
        items.push_back({{m < 0 ? 0 : m, ic.src}, ic});
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& i : items) out.push_back(i.second);
    return out;
}

} // namespace idn

// يُكمل القيم المشتقة/الافتراضية بلا تغيير ما حدّده المستخدم صراحةً.
inline void completeIdentity(AppIdentity& a, const std::string& baseDir = "") {
    using namespace idn;
    if (a.shortName.empty() && !a.name.empty()) {
        a.shortName = utf8Len(a.name) <= 12 ? a.name : "";
        if (a.shortName.empty()) { // أول كلمتين
            auto w = split(a.name, ' ');
            a.shortName = w.size() > 1 ? w[0] + " " + w[1] : a.name;
        }
    }
    if (a.versionCode == 0) a.versionCode = deriveVersionCode(a.version);
    if (a.dir.empty()) a.dir = defaultDir(a.lang);
    if (a.display.empty() && a.kind == "app") a.display = "standalone";
    if (a.startUrl.empty()) a.startUrl = "/";
    if (a.scope.empty()) a.scope = "/";
    if (a.shortDescription.empty() && !a.description.empty()) {
        std::string d = a.description;
        size_t nl = d.find('\n'); if (nl != std::string::npos) d.erase(nl);
        // اقتطاع عند 80 محرفاً (على حدود UTF-8)
        size_t count = 0, i = 0;
        for (; i < d.size() && count < 80; ++i) if ((static_cast<unsigned char>(d[i]) & 0xC0) != 0x80) ++count;
        while (i < d.size() && (static_cast<unsigned char>(d[i]) & 0xC0) == 0x80) ++i;
        a.shortDescription = i < d.size() ? trim(d.substr(0, i)) + "…" : d;
    }
    for (auto& ic : a.icons) completeIcon(ic, baseDir);
    if (!a.iconsDir.empty()) { // مسح المجلد: يُضاف ما لم يُذكر صراحةً (الصريح يتقدّم ويحتفظ بترتيبه)
        for (auto& f : scanIconsDir(baseDir, a.iconsDir)) {
            bool have = false;
            for (auto& ic : a.icons) if (ic.src == f.src) { have = true; break; }
            if (!have) a.icons.push_back(f);
        }
    }
    if (a.icons.empty() && !a.favicon.empty()) { IconEntry e; e.src = a.favicon; completeIcon(e, baseDir); a.icons.push_back(e); }
    if (a.favicon.empty()) {
        for (auto& ic : a.icons) if (idn::endsWith(idn::lower(ic.src), ".ico")) { a.favicon = ic.src; break; }
        if (a.favicon.empty()) { // أصغر أيقونة ≥ 32
            long best = 0;
            for (auto& ic : a.icons) { long m = idn::maxSize(ic.sizes); if (m >= 32 && (best == 0 || m < best)) { best = m; a.favicon = ic.src; } }
        }
    }
    SeoInfo& s = a.seo;
    if (s.title.empty()) s.title = a.name;
    if (s.description.empty()) s.description = !a.shortDescription.empty() && a.description.empty() ? a.shortDescription : a.description;
    if (s.author.empty()) s.author = a.author;
    if (s.robots.empty()) s.robots = "index, follow";
    if (s.canonical.empty() && idn::isAbsUrl(a.homepage)) s.canonical = a.homepage;
    if (s.ogTitle.empty()) s.ogTitle = s.title;
    if (s.ogDescription.empty()) s.ogDescription = s.description;
    if (s.ogType.empty()) s.ogType = "website";
    if (s.ogSiteName.empty()) s.ogSiteName = a.name;
    if (s.locale.empty() && !a.lang.empty()) {
        std::string l = a.lang; for (auto& c : l) if (c == '-') c = '_';
        if (l.find('_') == std::string::npos) { // og:locale يتطلّب لغة_منطقة: الأشهر لكل لغة (وإلا تكرار الرمز: fr→fr_FR)
            static const std::pair<const char*, const char*> reg[] = {{"en", "US"}, {"ar", "AR"}, {"fa", "IR"}, {"ur", "PK"}, {"he", "IL"}, {"hi", "IN"},
                                                                   {"zh", "CN"}, {"ja", "JP"}, {"ko", "KR"}, {"pt", "BR"}, {"sv", "SE"}, {"da", "DK"}, {"el", "GR"}, {"uk", "UA"}};
            std::string region;
            for (auto& r : reg) if (l == r.first) region = r.second;
            if (region.empty()) { region = l; for (auto& c : region) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
            l += "_" + region;
        }
        s.locale = l;
    }
    if (s.ogImage.empty()) { // أكبر أيقونة PNG/JPG كصورة مشاركة احتياطية
        long best = 0;
        for (auto& ic : a.icons) { long m = idn::maxSize(ic.sizes); if (ic.type != "image/svg+xml" && m > best) { best = m; s.ogImage = ic.src; } }
    }
    if (s.twitterCard.empty()) s.twitterCard = s.ogImage.empty() ? "summary" : "summary_large_image";
}


// ------------------------------------------------------------------ لغات متعددة
inline const LocaleInfo* findLocale(const AppIdentity& a, const std::string& lang) {
    std::string want = idn::lower(lang);
    for (auto& l : a.locales) if (idn::lower(l.lang) == want) return &l;
    std::string prim = want.substr(0, want.find_first_of("-_"));
    for (auto& l : a.locales) if (idn::lower(l.lang).substr(0, idn::lower(l.lang).find_first_of("-_")) == prim) return &l;
    return nullptr;
}

// hreflang تلقائي من روابط اللغات (اللغة الأساسية + كل locale له url + x-default). فارغ إن لم يُعرَّف أي رابط لغة.
inline std::vector<std::pair<std::string, std::string>> autoAlternates(const AppIdentity& a) {
    std::vector<std::pair<std::string, std::string>> out;
    bool any = false;
    for (auto& l : a.locales) if (!l.url.empty()) any = true;
    if (!any) return out;
    std::string def = !a.seo.canonical.empty() ? a.seo.canonical : (idn::isAbsUrl(a.homepage) ? a.homepage : std::string());
    if (!a.lang.empty() && !def.empty()) out.push_back({a.lang, def});
    for (auto& l : a.locales) if (!l.url.empty()) out.push_back({l.lang, l.url});
    if (!def.empty()) out.push_back({"x-default", def});
    return out;
}

// هوية بلغة معيّنة: يُطبَّق locales[lang] فوق الأساس ثم تُشتق بقية القيم (عنوان/وصف/og/locale/اتجاه) من النصوص الجديدة.
// lang فارغ أو = اللغة الأساسية ⇒ الأساس نفسه (مع hreflang التلقائي). لغة غير معرّفة ⇒ std::invalid_argument.
inline AppIdentity localizeIdentity(const AppIdentity& base, const std::string& lang) {
    AppIdentity a = base;
    auto alts = autoAlternates(base);
    bool isBase = lang.empty() || idn::lower(lang) == idn::lower(base.lang);
    if (!isBase) {
        const LocaleInfo* L = findLocale(base, lang);
        if (!L) throw std::invalid_argument("لا توجد ترجمة للغة '" + lang + "' في locales");
        SeoInfo& s = a.seo;
        a.lang = L->lang; a.dir.clear(); s.locale.clear();
        bool textChanged = !L->name.empty() || !L->description.empty() || !L->shortDescription.empty();
        if (!L->name.empty()) { a.name = L->name; a.shortName.clear(); s.ogSiteName.clear(); }
        if (!L->shortName.empty()) a.shortName = L->shortName;
        if (!L->description.empty()) { a.description = L->description; a.shortDescription.clear(); }
        if (!L->shortDescription.empty()) a.shortDescription = L->shortDescription;
        if (!L->seoTitle.empty()) s.title = L->seoTitle; else if (!L->name.empty()) s.title.clear();
        if (!L->seoDescription.empty()) s.description = L->seoDescription; else if (!L->description.empty() || !L->shortDescription.empty()) s.description.clear();
        if (!L->keywords.empty()) s.keywords = L->keywords;
        if (textChanged || !L->seoTitle.empty() || !L->seoDescription.empty()) { s.ogTitle.clear(); s.ogDescription.clear(); }
        if (!L->url.empty()) s.canonical = L->url;
        if (!L->ogImage.empty()) s.ogImage = L->ogImage;
    }
    for (auto& al : alts) { // الصريح يتقدّم على التلقائي
        bool have = false;
        for (auto& e : a.seo.alternates) if (idn::lower(e.first) == idn::lower(al.first)) { have = true; break; }
        if (!have) a.seo.alternates.push_back(al);
    }
    return a;
}

// ------------------------------------------------------------------ سجل الإصدارات
// markdown مرتّب من الأحدث للأقدم. العنوان بالعربية إن كانت اللغة ar.
inline std::string buildChangelog(const AppIdentity& a) {
    std::vector<ReleaseInfo> rs = a.releases;
    std::stable_sort(rs.begin(), rs.end(), [](const ReleaseInfo& x, const ReleaseInfo& y) { return idn::cmpSemver(x.version, y.version) > 0; });
    bool ar = idn::lower(a.lang).compare(0, 2, "ar") == 0;
    std::ostringstream o;
    o << "# " << (ar ? "سجل التغييرات" : "Changelog") << (a.name.empty() ? "" : " — " + a.name) << "\n";
    for (auto& r : rs) {
        o << "\n## " << r.version;
        if (!r.date.empty()) o << " — " << r.date;
        if (!r.channel.empty() && idn::lower(r.channel) != "stable") o << " [" << r.channel << "]";
        o << "\n";
        if (!r.notes.empty()) {
            o << "\n";
            for (auto& line : idn::split(r.notes, '\n')) {
                std::string t = idn::trim(line);
                if (t.empty()) continue;
                bool bullet = (t[0] == '-' || t[0] == '*') && t.size() > 1 && t[1] == ' ';
                o << (bullet ? "" : "- ") << t << "\n";
            }
        }
    }
    return o.str();
}

// ------------------------------------------------------------------ project.og.urin
// يولّد نص project.og.urin (schema=2) من الهوية — فيبقى ملف المشروع تابعاً للحاوية لا نسخة يدوية موازية.
// الترتيب والتهريب والحدود مطابقة لـ RinProjectMeta.render (docs/project-og-urin.md).
struct UrinOptions {
    std::string type;                    // ui|html|free|...؛ فارغ ⇒ site→html وإلا free
    std::vector<std::string> tags, permissions, dependencies;
    std::string minRinVersion, created, modified;
    std::string icon;                    // أيقونة الحزمة (PNG نسبي)؛ يختارها المستدعي
};
inline std::string urinEscape(const std::string& v) {
    std::string o;
    for (char c : v) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"': o += "\\\""; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}
// يقتطع على حدود محارف UTF-8 (لا بايتات) إلى n محرفاً.
inline std::string clipChars(const std::string& s, size_t n) {
    size_t count = 0, i = 0;
    for (; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) { if (count == n) break; ++count; }
    }
    return s.substr(0, i);
}
inline std::string urinSlug(const std::string& id) {
    std::string o; bool dash = false;
    for (unsigned char c : id) {
        char l = static_cast<char>(std::tolower(c));
        if (std::isalnum(c) && c < 128) { o += l; dash = false; }
        else if (l == '.' || l == '_' || l == '-') { o += l; dash = false; }
        else if (!dash && !o.empty()) { o += '-'; dash = true; }
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    return clipChars(o, 64);
}
inline std::string buildUrin(AppIdentity a, const UrinOptions& u) {
    completeIdentity(a);
    std::ostringstream o;
    std::string type = u.type.empty() ? (a.kind == "site" ? "html" : "free") : u.type;
    std::string safeType;
    for (char c : type) if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') safeType += c;
    if (safeType.empty()) safeType = "free";
    o << "# بيانات وصفية لمشروع Rin — يُدار تلقائياً من التطبيق، يمكن تعديله بحذر\n"
         "// الحقول: النوع، المعرّف، الإصدار، المؤلف، الرخصة، الوسوم، الصلاحيات...\n"
         "// مُولَّد من حاوية container." << a.kind << " — عدّل الحاوية ثم أعد التوليد (docs/container_app.md)\n"
         "@container=ProjectMeta\n    schema=2;\n    type=\"" << clipChars(safeType, 64) << "\";\n";
    auto put = [&](const char* k, const std::string& v) { if (!v.empty()) o << "    " << k << "=\"" << urinEscape(v) << "\";\n"; };
    auto list = [&](const std::vector<std::string>& v, size_t maxN, size_t maxLen) {
        std::string j; size_t n = 0;
        for (auto& x : v) {
            std::string t = idn::trim(x);
            for (auto& c : t) if (c == ',') c = ' ';
            t = clipChars(t, maxLen);
            if (t.empty() || n >= maxN) continue;
            j += (n ? "," : "") + t; ++n;
        }
        return j;
    };
    put("id", urinSlug(a.id));
    put("name", clipChars(a.name, 64));
    put("version", clipChars(a.version, 32));
    put("description", clipChars(a.description.empty() ? a.shortDescription : a.description, 1000));
    put("author", clipChars(a.author, 128));
    put("license", clipChars(a.license, 64));
    put("tags", list(u.tags, 16, 32));
    put("created", u.created);
    put("modified", u.modified);
    put("minRinVersion", clipChars(u.minRinVersion, 32));
    put("permissions", list(u.permissions, 64, 64));
    put("dependencies", list(u.dependencies, 64, 128));
    put("icon", clipChars(u.icon, 256));
    put("shortName", clipChars(a.shortName, 32));
    put("versionCode", a.versionCode > 0 ? std::to_string(a.versionCode).substr(0, 12) : "");
    put("channel", clipChars(a.channel, 16));
    put("lang", clipChars(a.lang, 16));
    put("homepage", clipChars(a.homepage, 256));
    put("category", clipChars(a.category, 32));
    put("shortDescription", clipChars(a.shortDescription, 160));
    put("themeColor", clipChars(a.themeColor, 9));
    put("backgroundColor", clipChars(a.backgroundColor, 9));
    { std::vector<std::string> srcs; for (auto& ic : a.icons) if (!ic.src.empty()) srcs.push_back(ic.src); put("icons", list(srcs, 16, 256)); }
    put("seoTitle", clipChars(a.seo.title, 120));
    put("seoDescription", clipChars(a.seo.description, 320));
    put("seoKeywords", list(a.seo.keywords, 16, 64));
    put("canonical", clipChars(a.seo.canonical, 256));
    put("robots", clipChars(a.seo.robots, 64));
    put("ogImage", clipChars(a.seo.ogImage, 256));
    o << ".end/container\n";
    return o.str();
}

// ------------------------------------------------------------------ حقن الهوية في صفحة HTML
// الفكرة: الصفحة تحمل كتلة <!-- rin:app-head --> … <!-- /rin:app-head --> يملكها الحاوية. إعادة الحقن idempotent،
// وفحص الانجراف (check) يقارن النتيجة بالملف دون كتابة. أول حقن (بلا علامات) يزيل وسوم head المكرَّرة التي ستولّدها الحاوية
// (title/description/icons/OG/canonical…) ويبقي ما عداها (style/script/viewport/charset...).
namespace html {
using idn::lower; using idn::startsWith;

// نهاية وسم يبدأ عند i (s[i]=='<') مع احترام علامات الاقتباس؛ npos إن لم يُغلق.
inline size_t tagEnd(const std::string& s, size_t i) {
    char q = 0;
    for (size_t k = i + 1; k < s.size(); ++k) {
        char c = s[k];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '>') return k;
    }
    return std::string::npos;
}
// قيمة سمة من نص وسم (بأحرف صغيرة): name="x" | name='x' | name=x ؛ فارغ إن غابت.
inline std::string attr(const std::string& tagLower, const std::string& name) {
    size_t p = 0;
    while ((p = tagLower.find(name, p)) != std::string::npos) {
        bool left = p > 0 && (std::isspace(static_cast<unsigned char>(tagLower[p - 1])));
        size_t q = p + name.size();
        while (q < tagLower.size() && std::isspace(static_cast<unsigned char>(tagLower[q]))) ++q;
        if (left && q < tagLower.size() && tagLower[q] == '=') {
            ++q;
            while (q < tagLower.size() && std::isspace(static_cast<unsigned char>(tagLower[q]))) ++q;
            if (q >= tagLower.size()) return "";
            if (tagLower[q] == '"' || tagLower[q] == '\'') { size_t e = tagLower.find(tagLower[q], q + 1); return e == std::string::npos ? "" : tagLower.substr(q + 1, e - q - 1); }
            size_t e = q; while (e < tagLower.size() && !std::isspace(static_cast<unsigned char>(tagLower[e])) && tagLower[e] != '>') ++e;
            return tagLower.substr(q, e - q);
        }
        p += name.size();
    }
    return "";
}
inline bool nameIs(const std::string& tagLower, const char* tagName) { // "<meta" متبوعاً بفراغ أو > أو /
    size_t n = std::strlen(tagName);
    return tagLower.compare(0, n, tagName) == 0 && tagLower.size() > n && (std::isspace(static_cast<unsigned char>(tagLower[n])) || tagLower[n] == '>' || tagLower[n] == '/');
}
// هل يولّد buildHead وسماً مكافئاً لهذا الوسم؟ (فيُحذف عند أول حقن لتفادي التكرار)
inline bool isGenerated(const std::string& tagLower) {
    if (nameIs(tagLower, "<meta")) {
        std::string nm = attr(tagLower, "name"), pr = attr(tagLower, "property");
        static const char* names[] = {"description", "keywords", "robots", "author", "application-name", "apple-mobile-web-app-title", "theme-color", "generator"};
        for (auto n : names) if (nm == n) return true;
        return startsWith(nm, "twitter:") || startsWith(pr, "og:");
    }
    if (nameIs(tagLower, "<link")) {
        std::string rel = attr(tagLower, "rel");
        std::istringstream ss(rel); std::string tok;
        while (ss >> tok) {
            if (tok == "icon" || tok == "apple-touch-icon" || tok == "manifest" || tok == "canonical") return true;
            if (tok == "alternate" && !attr(tagLower, "hreflang").empty()) return true;
        }
    }
    return false;
}
// يحذف الوسوم المُولَّدة من نص head ويترك الباقي كما هو (النص الخام داخل style/script/تعليقات لا يُفسَّر).
inline std::string stripGenerated(const std::string& head) {
    std::string low = lower(head), out;
    size_t i = 0;
    auto dropWs = [&](size_t& next) { // يزيل فراغ نهاية السطر بعد وسم محذوف + المسافة البادئة المتبقية قبله
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
        size_t k = next; while (k < head.size() && (head[k] == ' ' || head[k] == '\t')) ++k;
        if (k < head.size() && head[k] == '\r') ++k;
        if (k < head.size() && head[k] == '\n') next = k + 1;
        else if (!out.empty() && out.back() != '\n') { /* وسم وسط سطر: اترك المسافات */ }
    };
    while (i < head.size()) {
        if (head[i] != '<') { out += head[i++]; continue; }
        if (low.compare(i, 4, "<!--") == 0) { size_t e = head.find("-->", i + 4); e = e == std::string::npos ? head.size() : e + 3; out.append(head, i, e - i); i = e; continue; }
        size_t e = tagEnd(head, i);
        if (e == std::string::npos) { out.append(head, i, std::string::npos); break; }
        std::string tl = low.substr(i, e - i + 1);
        auto skipTo = [&](const char* closeTag) -> size_t { size_t c = low.find(closeTag, e); return c == std::string::npos ? head.size() : c + std::strlen(closeTag); };
        if (nameIs(tl, "<title")) { size_t next = skipTo("</title>"); dropWs(next); i = next; continue; }
        if (nameIs(tl, "<script")) {
            size_t next = skipTo("</script>");
            if (tl.find("application/ld+json") != std::string::npos) { dropWs(next); i = next; continue; }
            out.append(head, i, next - i); i = next; continue;
        }
        if (nameIs(tl, "<style")) { size_t next = skipTo("</style>"); out.append(head, i, next - i); i = next; continue; }
        if (isGenerated(tl)) { size_t next = e + 1; dropWs(next); i = next; continue; }
        out.append(head, i, e + 1 - i); i = e + 1;
    }
    return out;
}
// موضع الإدراج داخل نص head: بعد آخر meta من charset/viewport (يبقى charset ضمن أول 1024 بايت)، وإلا 0.
inline size_t afterBasicMeta(const std::string& head) {
    std::string low = lower(head);
    size_t pos = 0, i = 0;
    while ((i = low.find('<', i)) != std::string::npos) {
        if (low.compare(i, 4, "<!--") == 0) { size_t e = low.find("-->", i + 4); if (e == std::string::npos) break; i = e + 3; continue; }
        size_t e = tagEnd(head, i);
        if (e == std::string::npos) break;
        std::string tl = low.substr(i, e - i + 1);
        if (nameIs(tl, "<script")) { size_t c = low.find("</script>", e); if (c == std::string::npos) break; i = c + 9; continue; }
        if (nameIs(tl, "<style")) { size_t c = low.find("</style>", e); if (c == std::string::npos) break; i = c + 8; continue; }
        if (nameIs(tl, "<meta") && (tl.find("charset") != std::string::npos || attr(tl, "name") == "viewport")) pos = e + 1;
        i = e + 1;
    }
    return pos;
}
} // namespace html

inline const char* kHeadOpen = "<!-- rin:app-head -->";
inline const char* kHeadClose = "<!-- /rin:app-head -->";

// يحقن headBlock (ينتهي بسطر جديد) في html. mode: "updated" | "inserted" | "unchanged".
// strip: عند أول حقن (بلا علامات) احذف وسوم head المكرَّرة. يرمي std::invalid_argument إن لم يوجد <head> ولا علامات.
inline std::string injectHeadIntoHtml(const std::string& page, const std::string& headBlock, bool strip, const std::string& indent, std::string& mode) {
    size_t o = page.find("<!-- rin:app-head");
    if (o != std::string::npos) {
        size_t oe = page.find("-->", o);
        size_t c = oe == std::string::npos ? std::string::npos : page.find(kHeadClose, oe);
        if (oe == std::string::npos || c == std::string::npos) throw std::invalid_argument("علامة <!-- rin:app-head --> بلا علامة إغلاق <!-- /rin:app-head -->");
        size_t ls = page.rfind('\n', o); ls = ls == std::string::npos ? 0 : ls + 1;
        std::string pad = page.substr(ls, o - ls);
        if (!pad.empty() && pad.find_first_not_of(" \t") != std::string::npos) pad.clear();
        std::string res = page.substr(0, oe + 3) + "\n" + headBlock + pad + page.substr(c);
        mode = res == page ? "unchanged" : "updated";
        return res;
    }
    std::string low = idn::lower(page);
    size_t hs = std::string::npos;
    for (size_t p = 0; (p = low.find("<head", p)) != std::string::npos; p += 5) {
        char n = p + 5 < low.size() ? low[p + 5] : '\0';
        if (n == '>' || std::isspace(static_cast<unsigned char>(n))) { hs = p; break; }
    }
    if (hs == std::string::npos) throw std::invalid_argument("لا يوجد <head> في الملف ولا علامات <!-- rin:app-head -->");
    size_t he = html::tagEnd(page, hs);
    size_t close = he == std::string::npos ? std::string::npos : low.find("</head", he);
    if (close == std::string::npos) throw std::invalid_argument("وسم <head> غير مُغلق");
    std::string inner = page.substr(he + 1, close - he - 1);
    if (strip) inner = html::stripGenerated(inner);
    size_t at = html::afterBasicMeta(inner);
    std::string block = std::string("\n") + indent + kHeadOpen + "\n" + headBlock + indent + kHeadClose;
    inner.insert(at, block);
    mode = "inserted";
    return page.substr(0, he + 1) + inner + page.substr(close);
}

// ------------------------------------------------------------------ تحقق
// fileExists اختياري: يُستدعى بمسار نسبي ويعيد هل الملف موجود (للتحقق من الأيقونات/صورة المشاركة).
inline std::vector<IdentityIssue> validateIdentity(const AppIdentity& a,
                                                   const std::function<bool(const std::string&)>& fileExists = nullptr) {
    using namespace idn;
    std::vector<IdentityIssue> out;
    auto add = [&](const char* lvl, const char* code, const char* field, const std::string& msg) {
        out.push_back({lvl, code, field, msg});
    };
    if (a.name.empty()) add("error", "name.missing", "name", "الاسم (name) مطلوب");
    else if (utf8Len(a.name) > 45) add("warn", "name.too_long", "name", "الاسم أطول من 45 حرفاً وقد يُقتطع في المتاجر والشاشات");
    if (!a.shortName.empty() && utf8Len(a.shortName) > 12) add("warn", "short_name.too_long", "short_name", "الاسم المختصر أطول من 12 حرفاً وقد يُقتطع تحت الأيقونة");
    if (a.version.empty()) add("error", "version.missing", "version", "الإصدار (version) مطلوب");
    else { Semver s; if (!parseSemver(a.version, s)) add("error", "version.invalid", "version", "الإصدار '" + a.version + "' ليس semver صالحاً (مثال: 1.2.0 أو 1.0.0-beta.1)"); }
    if (a.versionCode < 0) add("error", "version_code.negative", "version_code", "رقم البناء (version_code) يجب ألا يكون سالباً");
    if (!a.channel.empty()) {
        std::string c = lower(a.channel);
        if (c != "stable" && c != "beta" && c != "alpha" && c != "dev" && c != "nightly" && c != "rc")
            add("warn", "channel.unknown", "channel", "قناة غير معتادة '" + a.channel + "' (المعتاد: stable/beta/alpha/dev/nightly/rc)");
    }
    if (a.kind != "app" && a.kind != "site") add("error", "kind.invalid", "kind", "النوع (kind) يجب أن يكون app أو site");
    if (!a.id.empty()) {
        bool ok = true;
        for (char c : a.id) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) ok = false;
        if (!ok) add("warn", "id.chars", "id", "المعرّف (id) يُفضَّل أن يحوي أحرفاً لاتينية وأرقاماً ونقاطاً وشرطات فقط (مثل com.example.app)");
    } else if (a.kind == "app") add("info", "id.missing", "id", "لا يوجد معرّف ثابت (id) — يُنصح به لتمييز التطبيق بين الإصدارات");
    if (a.description.empty() && a.shortDescription.empty()) add("warn", "description.missing", "description", "لا يوجد وصف للتطبيق/الموقع");
    if (!a.lang.empty() && !validLang(a.lang)) add("warn", "lang.invalid", "lang", "رمز اللغة '" + a.lang + "' غير صالح (مثل ar أو ar-SA أو en)");
    if (a.lang.empty()) add("info", "lang.missing", "lang", "لم تُحدَّد اللغة (lang) — تُحسّن إمكانية الوصول وSEO");
    if (!a.dir.empty() && a.dir != "rtl" && a.dir != "ltr") add("error", "dir.invalid", "dir", "الاتجاه (dir) يجب أن يكون rtl أو ltr");
    auto colorCheck = [&](const std::string& c, const char* f) {
        if (!c.empty() && !looksHexColor(c)) add("warn", "color.invalid", f, std::string("اللون في ") + f + " ليس بصيغة #RGB أو #RRGGBB");
    };
    colorCheck(a.themeColor, "theme_color");
    colorCheck(a.backgroundColor, "background_color");
    if (!a.homepage.empty() && !isAbsUrl(a.homepage)) add("warn", "homepage.not_absolute", "homepage", "رابط الصفحة الرئيسية يجب أن يبدأ بـ http:// أو https://");

    // أيقونات
    if (a.icons.empty()) add("warn", "icons.missing", "icons", "لا توجد أيقونات — أضف icon أو icons");
    else {
        long best = 0; bool maskable = false;
        for (size_t i = 0; i < a.icons.size(); ++i) {
            const auto& ic = a.icons[i];
            std::string f = "icons[" + std::to_string(i) + "]";
            if (ic.src.empty()) { add("error", "icon.src.missing", f.c_str(), "أيقونة بلا مسار (src)"); continue; }
            if (ic.src.find("..") != std::string::npos || (!ic.src.empty() && ic.src[0] == '/' ) || ic.src.find(":\\") != std::string::npos)
                { if (!isAbsUrl(ic.src)) add("error", "icon.src.unsafe", f.c_str(), "مسار الأيقونة '" + ic.src + "' يجب أن يكون نسبياً وداخل المشروع (بلا .. أو / في البداية)"); continue; }
            if (ic.type.empty()) add("warn", "icon.type.unknown", f.c_str(), "نوع الأيقونة '" + ic.src + "' غير معروف (امتداد غير مدعوم)");
            if (ic.sizes.empty()) add("warn", "icon.sizes.unknown", f.c_str(), "تعذّر تحديد حجم الأيقونة '" + ic.src + "' — حدّد sizes");
            if (fileExists && !isAbsUrl(ic.src) && !fileExists(ic.src)) add("error", "icon.file.missing", f.c_str(), "ملف الأيقونة '" + ic.src + "' غير موجود");
            long m = maxSize(ic.sizes); if (m == -1) m = 100000; best = std::max(best, m);
            if (ic.purpose.find("maskable") != std::string::npos) maskable = true;
        }
        if (best > 0 && best < 192) add("warn", "icons.too_small", "icons", "أكبر أيقونة أصغر من 192×192 — مطلوب 192 و512 لتثبيت PWA/أندرويد");
        else if (best >= 192 && best < 512) add("info", "icons.no_512", "icons", "يُنصح بأيقونة 512×512 لشاشة البداية وتثبيت PWA");
        if (a.kind == "app" && !maskable) add("info", "icons.no_maskable", "icons", "يُنصح بأيقونة maskable لأندرويد (purpose: \"maskable\")");
    }

    // SEO
    const SeoInfo& s = a.seo;
    size_t tl = utf8Len(s.title), dl = utf8Len(s.description);
    if (s.title.empty()) add("warn", "seo.title.missing", "seo.title", "لا يوجد عنوان SEO");
    else if (tl > 60) add("warn", "seo.title.too_long", "seo.title", "عنوان SEO " + std::to_string(tl) + " حرفاً — يُقتطع في نتائج البحث بعد ~60");
    else if (tl < 10) add("info", "seo.title.too_short", "seo.title", "عنوان SEO قصير (" + std::to_string(tl) + " أحرف) — الأمثل 30–60");
    if (s.description.empty()) add("warn", "seo.description.missing", "seo.description", "لا يوجد وصف SEO");
    else if (dl > 160) add("warn", "seo.description.too_long", "seo.description", "وصف SEO " + std::to_string(dl) + " حرفاً — يُقتطع بعد ~160");
    else if (dl < 50) add("info", "seo.description.too_short", "seo.description", "وصف SEO قصير (" + std::to_string(dl) + " حرفاً) — الأمثل 70–160");
    if (s.keywords.size() > 12) add("warn", "seo.keywords.too_many", "seo.keywords", "أكثر من 12 كلمة مفتاحية — محركات البحث الحديثة تتجاهلها، والإفراط قد يُعدّ حشواً");
    if (!s.canonical.empty() && !isAbsUrl(s.canonical)) add("error", "seo.canonical.not_absolute", "seo.canonical", "canonical يجب أن يكون رابطاً مطلقاً يبدأ بـ http:// أو https://");
    if (!s.robots.empty()) {
        static const char* ok[] = {"index", "noindex", "follow", "nofollow", "none", "all", "noarchive", "nosnippet", "noimageindex", "notranslate"};
        for (auto tok : split(s.robots, ',')) {
            std::string t = lower(trim(tok));
            if (t.empty()) continue;
            bool known = false;
            for (auto k : ok) if (t == k) known = true;
            if (!known && !startsWith(t, "max-snippet") && !startsWith(t, "max-image-preview") && !startsWith(t, "max-video-preview") && !startsWith(t, "unavailable_after"))
                add("error", "seo.robots.invalid", "seo.robots", "قيمة robots غير معروفة: '" + t + "'");
        }
    }
    if (!s.ogImage.empty() && fileExists && !isAbsUrl(s.ogImage) && !fileExists(s.ogImage)) add("warn", "seo.og_image.missing_file", "seo.og_image", "صورة المشاركة '" + s.ogImage + "' غير موجودة");
    if (s.ogImage.empty()) add("info", "seo.og_image.missing", "seo.og_image", "لا توجد صورة مشاركة (og_image) — تُحسّن المعاينة على وسائل التواصل");
    for (auto& alt : s.alternates)
        if (!validLang(alt.first) && alt.first != "x-default") add("warn", "seo.alternate.lang", "seo.alternates", "رمز لغة غير صالح في alternates: '" + alt.first + "'");
        else if (!isAbsUrl(alt.second)) add("error", "seo.alternate.url", "seo.alternates", "رابط اللغة البديلة '" + alt.first + "' يجب أن يكون مطلقاً");

    // مجلد الأيقونات (يُفحَص عبر fileExists بمسار ينتهي بـ '/')
    if (!a.iconsDir.empty() && fileExists) {
        std::string d = a.iconsDir; while (!d.empty() && d.back() == '/') d.pop_back();
        if (d.find("..") != std::string::npos || (!d.empty() && d[0] == '/')) add("error", "icons_dir.unsafe", "icons_dir", "مجلد الأيقونات '" + a.iconsDir + "' يجب أن يكون نسبياً وداخل المشروع");
        else if (!fileExists(d + "/")) add("error", "icons_dir.missing", "icons_dir", "مجلد الأيقونات '" + a.iconsDir + "' غير موجود");
    }

    // اللغات
    for (size_t i = 0; i < a.locales.size(); ++i) {
        const LocaleInfo& l = a.locales[i];
        std::string f = "locales." + l.lang;
        if (!validLang(l.lang)) { add("error", "locale.lang.invalid", f.c_str(), "رمز لغة غير صالح في locales: '" + l.lang + "'"); continue; }
        bool dup = false;
        for (size_t j = 0; j < i; ++j) if (lower(a.locales[j].lang) == lower(l.lang)) dup = true;
        if (dup) { add("error", "locale.lang.duplicate", f.c_str(), "اللغة '" + l.lang + "' مكرَّرة في locales"); continue; }
        if (!a.lang.empty() && lower(a.lang) == lower(l.lang)) add("warn", "locale.lang.same_as_default", f.c_str(), "اللغة '" + l.lang + "' هي نفسها لغة الهوية الأساسية — ضع نصوصها في الحقول الأساسية");
        if (l.name.empty() && l.description.empty() && l.shortDescription.empty() && l.seoTitle.empty() && l.seoDescription.empty())
            add("info", "locale.empty", f.c_str(), "اللغة '" + l.lang + "' بلا نصوص — ستعيد نصوص اللغة الأساسية");
        if (!l.url.empty() && !isAbsUrl(l.url)) add("error", "locale.url.not_absolute", f.c_str(), "رابط اللغة '" + l.lang + "' يجب أن يكون مطلقاً");
        try {
            AppIdentity x = localizeIdentity(a, l.lang);
            completeIdentity(x);
            if (!l.name.empty() && utf8Len(x.name) > 45) add("warn", "locale.name.too_long", f.c_str(), "اسم اللغة '" + l.lang + "' أطول من 45 حرفاً");
            if (!l.shortName.empty() && utf8Len(x.shortName) > 12) add("warn", "locale.short_name.too_long", f.c_str(), "الاسم المختصر للغة '" + l.lang + "' أطول من 12 حرفاً");
            if (utf8Len(x.seo.title) > 60) add("warn", "locale.seo.title.too_long", f.c_str(), "عنوان SEO للغة '" + l.lang + "' " + std::to_string(utf8Len(x.seo.title)) + " حرفاً — يُقتطع بعد ~60");
            if (utf8Len(x.seo.description) > 160) add("warn", "locale.seo.description.too_long", f.c_str(), "وصف SEO للغة '" + l.lang + "' أطول من 160 حرفاً");
        } catch (const std::exception&) {}
    }

    // سجل الإصدارات
    {
        bool hasCurrent = false;
        for (size_t i = 0; i < a.releases.size(); ++i) {
            const ReleaseInfo& r = a.releases[i];
            std::string f = "releases[" + std::to_string(i) + "]";
            Semver sv;
            if (!parseSemver(r.version, sv)) { add("error", "release.version.invalid", f.c_str(), "إصدار غير صالح في releases: '" + r.version + "'"); continue; }
            if (r.version == a.version) hasCurrent = true;
            for (size_t j = 0; j < i; ++j) if (a.releases[j].version == r.version) add("warn", "release.version.duplicate", f.c_str(), "الإصدار '" + r.version + "' مكرَّر في releases");
            bool dateOk = r.date.size() == 10 && r.date[4] == '-' && r.date[7] == '-';
            for (size_t k = 0; dateOk && k < 10; ++k) if (k != 4 && k != 7 && !std::isdigit(static_cast<unsigned char>(r.date[k]))) dateOk = false;
            if (!r.date.empty() && !dateOk) add("warn", "release.date.invalid", f.c_str(), "تاريخ الإصدار '" + r.date + "' ليس بصيغة YYYY-MM-DD");
        }
        if (!a.releases.empty() && !hasCurrent && !a.version.empty())
            add("info", "release.current.missing", "releases", "لا يوجد سجل للإصدار الحالي " + a.version + " في releases");
        if (!a.releases.empty()) {
            std::string top = a.releases[0].version; for (auto& r : a.releases) if (cmpSemver(r.version, top) > 0) top = r.version;
            if (!a.version.empty() && cmpSemver(top, a.version) > 0) add("warn", "release.ahead", "releases", "في releases إصدار (" + top + ") أحدث من الإصدار الحالي " + a.version);
        }
    }
    return out;
}

// ------------------------------------------------------------------ توليد
struct HeadOptions {
    std::string manifestHref = "manifest.webmanifest"; // فارغ = بلا وسم manifest
    std::string indent = "    ";
    bool charsetViewport = true;
    bool jsonLdOverride = true; // يحترم seo.jsonLd
    std::string assetsUrl;      // بادئة تُضاف للمسارات النسبية (أيقونات/og:image) — لصفحة تُخدَم من مكان غير مكان الأصول
    std::function<std::string(const std::string&)> embed; // src → data: URI لتضمين أيقونة صغيرة داخل الصفحة ("" = لا تضمين)
};

inline std::string iconRel(const IconEntry& ic) { return ic.purpose.find("maskable") != std::string::npos ? "" : "icon"; }

inline std::string buildHead(AppIdentity a, const HeadOptions& o = HeadOptions()) {
    completeIdentity(a);
    using namespace idn;
    std::ostringstream h;
    const std::string& I = o.indent;
    auto meta = [&](const std::string& name, const std::string& content) {
        if (!content.empty()) h << I << "<meta name=\"" << htmlEsc(name) << "\" content=\"" << htmlEsc(content) << "\">\n";
    };
    auto prop = [&](const std::string& p, const std::string& content) {
        if (!content.empty()) h << I << "<meta property=\"" << htmlEsc(p) << "\" content=\"" << htmlEsc(content) << "\">\n";
    };
    const SeoInfo& s = a.seo;
    auto U = [&](const std::string& p) -> std::string { // مسار → رابط نهائي (بادئة الأصول للنسبي فقط)
        if (o.assetsUrl.empty() || p.empty() || isAbsUrl(p) || startsWith(p, "data:") || p[0] == '/') return p;
        return o.assetsUrl + (o.assetsUrl.back() == '/' ? "" : "/") + p;
    };
    auto IH = [&](const std::string& p) -> std::string { // href لأيقونة: مضمَّنة إن أمكن وإلا U(p)
        if (o.embed) { std::string d = o.embed(p); if (!d.empty()) return d; }
        return U(p);
    };
    if (o.charsetViewport) {
        h << I << "<meta charset=\"utf-8\">\n";
        h << I << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    }
    h << I << "<title>" << htmlEsc(s.title.empty() ? a.name : s.title) << "</title>\n";
    meta("description", s.description);
    if (!s.keywords.empty()) meta("keywords", join(s.keywords, ", "));
    meta("robots", s.robots);
    meta("author", s.author);
    meta("application-name", a.name);
    meta("apple-mobile-web-app-title", a.shortName);
    meta("theme-color", a.themeColor);
    meta("generator", a.id.empty() ? std::string() : a.id + "@" + a.version);
    if (!s.canonical.empty()) h << I << "<link rel=\"canonical\" href=\"" << htmlEsc(s.canonical) << "\">\n";
    for (auto& alt : s.alternates) h << I << "<link rel=\"alternate\" hreflang=\"" << htmlEsc(alt.first) << "\" href=\"" << htmlEsc(alt.second) << "\">\n";
    // أيقونات
    bool favDone = false;
    if (!a.favicon.empty()) {
        std::string t = mimeOfPath(a.favicon);
        std::string fsz;
        for (auto& ic : a.icons) if (ic.src == a.favicon) fsz = ic.sizes;
        h << I << "<link rel=\"icon\" href=\"" << htmlEsc(IH(a.favicon)) << "\"" << (t.empty() ? "" : " type=\"" + t + "\"")
          << (fsz.empty() ? "" : " sizes=\"" + fsz + "\"") << ">\n";
        favDone = true;
    }
    for (auto& ic : a.icons) {
        if (ic.purpose.find("maskable") != std::string::npos) continue;
        if (favDone && ic.src == a.favicon) continue;
        h << I << "<link rel=\"icon\" href=\"" << htmlEsc(IH(ic.src)) << "\"";
        if (!ic.type.empty()) h << " type=\"" << htmlEsc(ic.type) << "\"";
        if (!ic.sizes.empty() && ic.sizes != "any") h << " sizes=\"" << htmlEsc(ic.sizes) << "\"";
        else if (ic.sizes == "any") h << " sizes=\"any\"";
        h << ">\n";
    }
    { // apple-touch-icon: أقرب أيقونة PNG ≥ 180
        const IconEntry* best = nullptr; long bm = 0;
        for (auto& ic : a.icons) {
            if (ic.type != "image/png") continue;
            long m = maxSize(ic.sizes);
            if (m >= 180 && (!best || m < bm)) { best = &ic; bm = m; }
        }
        if (best) h << I << "<link rel=\"apple-touch-icon\" href=\"" << htmlEsc(IH(best->src)) << "\">\n";
    }
    if (!o.manifestHref.empty()) h << I << "<link rel=\"manifest\" href=\"" << htmlEsc(o.manifestHref) << "\">\n";
    // Open Graph / Twitter
    prop("og:type", s.ogType);
    prop("og:title", s.ogTitle);
    prop("og:description", s.ogDescription);
    prop("og:site_name", s.ogSiteName);
    prop("og:url", s.canonical);
    prop("og:image", U(s.ogImage));
    prop("og:locale", s.locale);
    meta("twitter:card", s.twitterCard);
    meta("twitter:title", s.ogTitle);
    meta("twitter:description", s.ogDescription);
    meta("twitter:image", U(s.ogImage));
    meta("twitter:site", s.twitterSite);
    // JSON-LD
    if (s.jsonLd && o.jsonLdOverride && !a.name.empty()) {
        std::ostringstream j;
        j << "{\"@context\":\"https://schema.org\",\"@type\":" << jstr(a.kind == "site" ? "WebSite" : "SoftwareApplication")
          << ",\"name\":" << jstr(a.name);
        if (!s.description.empty()) j << ",\"description\":" << jstr(s.description);
        if (!s.canonical.empty()) j << ",\"url\":" << jstr(s.canonical);
        if (!a.lang.empty()) j << ",\"inLanguage\":" << jstr(a.lang);
        if (!s.ogImage.empty()) j << ",\"image\":" << jstr(U(s.ogImage));
        if (a.kind == "app") {
            if (!a.version.empty()) j << ",\"softwareVersion\":" << jstr(a.version);
            if (!a.category.empty()) j << ",\"applicationCategory\":" << jstr(a.category);
        }
        if (!a.author.empty()) j << ",\"author\":{\"@type\":\"Person\",\"name\":" << jstr(a.author) << "}";
        if (!a.publisher.empty()) j << ",\"publisher\":{\"@type\":\"Organization\",\"name\":" << jstr(a.publisher) << "}";
        if (!a.license.empty()) j << ",\"license\":" << jstr(a.license);
        j << "}";
        h << I << "<script type=\"application/ld+json\">" << j.str() << "</script>\n";
    }
    return h.str();
}

inline std::string buildManifest(AppIdentity a) {
    completeIdentity(a);
    using namespace idn;
    std::ostringstream m;
    m << "{\n";
    auto kv = [&](const char* k, const std::string& v, bool& first) {
        if (v.empty()) return;
        if (!first) m << ",\n";
        m << "  \"" << k << "\": " << jstr(v);
        first = false;
    };
    bool first = true;
    kv("id", a.id, first);
    kv("name", a.name, first);
    kv("short_name", a.shortName, first);
    kv("description", a.description.empty() ? a.shortDescription : a.description, first);
    kv("start_url", a.startUrl, first);
    kv("scope", a.scope, first);
    kv("display", a.display.empty() ? "browser" : a.display, first);
    kv("orientation", a.orientation, first);
    kv("lang", a.lang, first);
    kv("dir", a.dir, first);
    kv("theme_color", a.themeColor, first);
    kv("background_color", a.backgroundColor, first);
    if (!a.category.empty()) { if (!first) m << ",\n"; m << "  \"categories\": [" << jstr(a.category) << "]"; first = false; }
    if (!a.version.empty()) { if (!first) m << ",\n"; m << "  \"version\": " << jstr(a.version); first = false; }
    if (!a.icons.empty()) {
        if (!first) m << ",\n";
        m << "  \"icons\": [\n";
        for (size_t i = 0; i < a.icons.size(); ++i) {
            const auto& ic = a.icons[i];
            m << "    {\"src\": " << jstr(ic.src);
            if (!ic.sizes.empty()) m << ", \"sizes\": " << jstr(ic.sizes);
            if (!ic.type.empty()) m << ", \"type\": " << jstr(ic.type);
            if (!ic.purpose.empty()) m << ", \"purpose\": " << jstr(ic.purpose);
            m << "}" << (i + 1 < a.icons.size() ? "," : "") << "\n";
        }
        m << "  ]";
        first = false;
    }
    m << "\n}\n";
    return m.str();
}

inline std::string buildRobots(const AppIdentity& a, const std::string& sitemapUrl, const std::vector<std::string>& disallow) {
    std::ostringstream r;
    bool noindex = idn::lower(a.seo.robots).find("noindex") != std::string::npos || idn::lower(a.seo.robots) == "none";
    r << "User-agent: *\n";
    if (noindex) r << "Disallow: /\n";
    else if (disallow.empty()) r << "Allow: /\n";
    else for (auto& d : disallow) r << "Disallow: " << d << "\n";
    if (!sitemapUrl.empty()) r << "\nSitemap: " << sitemapUrl << "\n";
    return r.str();
}

inline std::string buildSitemap(const std::string& baseUrl, const std::vector<std::string>& paths, const std::string& lastmod) {
    std::string base = baseUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (auto p : paths) {
        if (p.empty() || p[0] != '/') p = "/" + p;
        x << "  <url><loc>" << idn::htmlEsc(base + p) << "</loc>";
        if (!lastmod.empty()) x << "<lastmod>" << idn::htmlEsc(lastmod) << "</lastmod>";
        x << "</url>\n";
    }
    x << "</urlset>\n";
    return x.str();
}

// ------------------------------------------------------------------ تخزين في Metadata::extra
// مفاتيح محجوزة (موثّقة في docs/container_app.md). كل القيم نصوص UTF-8؛ القوائم تُفصل بـ '\n'.
inline bool hasIdentity(const Metadata& m) { return m.extra.count("app.kind") != 0; }

inline void applyIdentity(Metadata& m, const AppIdentity& in) {
    AppIdentity a = in;
    completeIdentity(a);
    auto put = [&](const std::string& k, const std::string& v) { if (v.empty()) m.extra.erase(k); else m.extra[k] = v; };
    // نظّف أي بقايا قديمة للأيقونات/البدائل قبل الكتابة
    for (auto it = m.extra.begin(); it != m.extra.end();) {
        if (idn::startsWith(it->first, "icon.") || idn::startsWith(it->first, "seo.alt.") || idn::startsWith(it->first, "locale.") || idn::startsWith(it->first, "release.")) it = m.extra.erase(it); else ++it;
    }
    m.extra["app.kind"] = a.kind;
    put("app.id", a.id); put("app.short_name", a.shortName);
    put("app.version_code", a.versionCode ? std::to_string(a.versionCode) : "");
    put("app.channel", a.channel); put("app.publisher", a.publisher); put("app.homepage", a.homepage);
    put("app.lang", a.lang); put("app.dir", a.dir); put("app.category", a.category);
    put("app.short_description", a.shortDescription);
    put("app.theme_color", a.themeColor); put("app.background_color", a.backgroundColor);
    put("app.display", a.display); put("app.orientation", a.orientation);
    put("app.start_url", a.startUrl); put("app.scope", a.scope); put("app.favicon", a.favicon);
    put("app.icons_dir", a.iconsDir);
    m.extra["app.icon_count"] = std::to_string(a.icons.size());
    for (size_t i = 0; i < a.icons.size(); ++i) {
        std::string p = "icon." + std::to_string(i) + ".";
        m.extra[p + "src"] = a.icons[i].src; put(p + "sizes", a.icons[i].sizes);
        put(p + "type", a.icons[i].type); put(p + "purpose", a.icons[i].purpose);
    }
    const SeoInfo& s = a.seo;
    put("seo.title", s.title); put("seo.description", s.description);
    put("seo.keywords", idn::join(s.keywords, "\n"));
    put("seo.canonical", s.canonical); put("seo.robots", s.robots); put("seo.author", s.author);
    put("seo.og_title", s.ogTitle); put("seo.og_description", s.ogDescription); put("seo.og_image", s.ogImage);
    put("seo.og_type", s.ogType); put("seo.og_site_name", s.ogSiteName);
    put("seo.twitter_card", s.twitterCard); put("seo.twitter_site", s.twitterSite); put("seo.locale", s.locale);
    m.extra["seo.json_ld"] = s.jsonLd ? "1" : "0";
    for (auto& alt : s.alternates) m.extra["seo.alt." + alt.first] = alt.second;
    // اللغات والإصدارات (locale.<i>.* و release.<i>.* بفهرس رقمي كي لا تتصادم رموز اللغات مع النقاط)
    put("app.locale_count", a.locales.empty() ? "" : std::to_string(a.locales.size()));
    for (size_t i = 0; i < a.locales.size(); ++i) {
        const LocaleInfo& l = a.locales[i]; std::string p = "locale." + std::to_string(i) + ".";
        m.extra[p + "lang"] = l.lang; put(p + "name", l.name); put(p + "short_name", l.shortName);
        put(p + "description", l.description); put(p + "short_description", l.shortDescription);
        put(p + "seo_title", l.seoTitle); put(p + "seo_description", l.seoDescription);
        put(p + "keywords", idn::join(l.keywords, "\n")); put(p + "url", l.url); put(p + "og_image", l.ogImage);
    }
    put("app.release_count", a.releases.empty() ? "" : std::to_string(a.releases.size()));
    for (size_t i = 0; i < a.releases.size(); ++i) {
        const ReleaseInfo& r = a.releases[i]; std::string p = "release." + std::to_string(i) + ".";
        m.extra[p + "version"] = r.version; put(p + "date", r.date); put(p + "channel", r.channel); put(p + "notes", r.notes);
    }
    // الحقول القياسية القديمة (ما يقرؤه أي قارئ قديم)
    if (!a.name.empty()) m.name = a.name;
    if (!a.version.empty()) m.version = a.version;
    if (!a.description.empty()) m.description = a.description;
    else if (!a.shortDescription.empty()) m.description = a.shortDescription;
    if (!a.author.empty()) m.author = a.author;
    if (!a.license.empty()) m.license = a.license;
}

inline AppIdentity readIdentity(const Metadata& m) {
    AppIdentity a;
    auto get = [&](const std::string& k) -> std::string { auto it = m.extra.find(k); return it == m.extra.end() ? std::string() : it->second; };
    a.kind = get("app.kind").empty() ? "app" : get("app.kind");
    a.id = get("app.id"); a.name = m.name; a.shortName = get("app.short_name");
    a.version = m.version;
    { std::string vc = get("app.version_code"); if (!vc.empty()) { try { a.versionCode = std::stoll(vc); } catch (...) {} } }
    a.channel = get("app.channel"); a.author = m.author; a.publisher = get("app.publisher"); a.license = m.license;
    a.iconsDir = get("app.icons_dir");
    a.homepage = get("app.homepage"); a.lang = get("app.lang"); a.dir = get("app.dir"); a.category = get("app.category");
    a.description = m.description; a.shortDescription = get("app.short_description");
    a.themeColor = get("app.theme_color"); a.backgroundColor = get("app.background_color");
    a.display = get("app.display"); a.orientation = get("app.orientation");
    a.startUrl = get("app.start_url"); a.scope = get("app.scope"); a.favicon = get("app.favicon");
    size_t n = 0;
    { std::string c = get("app.icon_count"); if (!c.empty()) { try { n = std::min<size_t>(std::stoul(c), 256); } catch (...) {} } }
    for (size_t i = 0; i < n; ++i) {
        std::string p = "icon." + std::to_string(i) + ".";
        IconEntry e; e.src = get(p + "src"); e.sizes = get(p + "sizes"); e.type = get(p + "type"); e.purpose = get(p + "purpose");
        if (e.purpose.empty()) e.purpose = "any";
        if (!e.src.empty()) a.icons.push_back(e);
    }
    SeoInfo& s = a.seo;
    s.title = get("seo.title"); s.description = get("seo.description");
    { std::string k = get("seo.keywords"); if (!k.empty()) for (auto& w : idn::split(k, '\n')) if (!w.empty()) s.keywords.push_back(w); }
    s.canonical = get("seo.canonical"); s.robots = get("seo.robots"); s.author = get("seo.author");
    s.ogTitle = get("seo.og_title"); s.ogDescription = get("seo.og_description"); s.ogImage = get("seo.og_image");
    s.ogType = get("seo.og_type"); s.ogSiteName = get("seo.og_site_name");
    s.twitterCard = get("seo.twitter_card"); s.twitterSite = get("seo.twitter_site"); s.locale = get("seo.locale");
    s.jsonLd = get("seo.json_ld") != "0";
    for (auto& kv : m.extra) if (idn::startsWith(kv.first, "seo.alt.")) s.alternates.push_back({kv.first.substr(8), kv.second});
    auto count = [&](const char* k) { size_t c = 0; std::string v = get(k); if (!v.empty()) { try { c = std::min<size_t>(std::stoul(v), 256); } catch (...) {} } return c; };
    for (size_t i = 0, c = count("app.locale_count"); i < c; ++i) {
        std::string p = "locale." + std::to_string(i) + ".";
        LocaleInfo l; l.lang = get(p + "lang"); if (l.lang.empty()) continue;
        l.name = get(p + "name"); l.shortName = get(p + "short_name"); l.description = get(p + "description");
        l.shortDescription = get(p + "short_description"); l.seoTitle = get(p + "seo_title"); l.seoDescription = get(p + "seo_description");
        { std::string k = get(p + "keywords"); if (!k.empty()) for (auto& w : idn::split(k, '\n')) if (!w.empty()) l.keywords.push_back(w); }
        l.url = get(p + "url"); l.ogImage = get(p + "og_image");
        a.locales.push_back(l);
    }
    for (size_t i = 0, c = count("app.release_count"); i < c; ++i) {
        std::string p = "release." + std::to_string(i) + ".";
        ReleaseInfo r; r.version = get(p + "version"); if (r.version.empty()) continue;
        r.date = get(p + "date"); r.channel = get(p + "channel"); r.notes = get(p + "notes");
        a.releases.push_back(r);
    }
    return a;
}

} // namespace clc
