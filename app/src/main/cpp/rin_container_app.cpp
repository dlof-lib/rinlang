// ============================================================================
//  rin_container_app.cpp — هوية التطبيقات والمواقع: container.app / container.site
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp بعد rin_container_blend.cpp ويُسجَّل عبر
//  Interpreter::registerNativesContainerApp(). التوثيق الكامل: docs/container_app.md
//
//  الحاوية:  @container.app=name ... .end/container.app   (أو @container.site / @name.app / @name.site)
//            حقول let: name short_name id version version_code channel author publisher license homepage
//            lang dir category description short_description theme_color background_color display
//            orientation start_url scope favicon icon icons keywords seo{...} kind("app"|"site")
//
//  الدوال (كلها بصيغتين متكافئتين: container.appXxx و app.xxx — الأولى لا تتأثر بوجود حاوية اسمها app):
//    container.appInfo(src)               هوية مكتملة (قيم مشتقة/افتراضية) كقاموس
//    container.appValidate(src, opts?)    مصفوفة مشاكل {level, code, field, message}؛ فارغة = سليمة
//    container.appHead(src, opts?)        وسوم <head> جاهزة: title/description/keywords/robots/canonical/
//                                         icons/manifest/Open Graph/Twitter/hreflang/JSON-LD
//    container.appManifest(src)           web app manifest (JSON نصاً)
//    container.appRobots(src, opts?)      robots.txt
//    container.appSitemap(src, paths, opts?) sitemap.xml
//    container.appBump(name, part)        يرفع الإصدار (major/minor/patch) ويحدّث الحقل في الحاوية
//    container.appExport(src, dir, opts?) يكتب head.html + manifest.webmanifest + robots.txt + app.json (+ sitemap.xml، CHANGELOG.md)؛ opts.page ⇒ يعيد أمر indsin2web الجاهز
//    container.appPack(src, dir, out, opts?)  يبني ملف .rcl من مجلد ويحفظ الهوية داخل Metadata
//    container.appOpen(path)              يقرأ الهوية من ملف .rcl
//    container.appList()                  كل حاويات الهوية المعرَّفة {name, kind, id, version, lang}
//    container.appPick(name?)             اسم حاوية الهوية (الوحيدة إن لم يُعطَ اسم؛ خطأ واضح عند الغياب/الالتباس)
//    container.appInject(src, file, opts?) يحقن <head> المُولَّد في صفحة HTML بين علامات rin:app-head (check ⇒ فحص انجراف فقط)
//    container.appChangelog(src)          سجل الإصدارات (releases) كـ markdown
//    container.appUrin(src, opts?)        نص project.og.urin مُولَّداً من الهوية؛ appFromUrin(textOrPath) عكسه
//  opts مشتركة: dir (جذر الملفات للأيقونات/icons_dir) · lang (لغة من locales) · assets_url · embed (حد بايتات لتضمين الأيقونات).
//  src = اسم حاوية | قاموس هوية (مثل ناتج appInfo/appOpen).
//  إضافةً: clcContainerIdentity(handle) لخدمة واجهة ClcActivity (فحص الحاويات).
// ============================================================================
#include "rin_interpreter.h"
#include "clc/clc_identity.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rin {
namespace capp {
using namespace extra;
namespace fs = std::filesystem;

static Value strV(const std::string& s) { return Value::string(s); }
static Value numV(double d) { return Value::num(d); }
static Value boolV(bool b) { return Value::boolean_(b); }
static Value mkMap(std::initializer_list<std::pair<const char*, Value>> kv) {
    MapData m;
    for (const auto& p : kv) m.push_back({Value::string(p.first), p.second});
    return newMap(std::move(m));
}

// يبحث في قاموس عن أول مفتاح موجود من قائمة مرادفات (snake_case / camelCase / كلمات مختصرة).
static const Value* pick(const MapData& m, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        const Value* v = m.findValue(Value::string(k));
        if (v && v->type != Value::Type::NIL) return v;
    }
    return nullptr;
}
static std::string asText(const Value* v) {
    if (!v) return "";
    switch (v->type) {
        case Value::Type::STRING: return v->str;
        case Value::Type::NUMBER: return v->toDisplayString();
        case Value::Type::BOOL: return v->boolean ? "true" : "false";
        default: return "";
    }
}
static std::string textOf(const MapData& m, std::initializer_list<const char*> keys) { return asText(pick(m, keys)); }

// «،» العربية و«;» تُعامَل كفاصلة.
static std::vector<std::string> splitList(std::string s) {
    static const std::string arComma = "\xD8\x8C"; // ،
    for (size_t p; (p = s.find(arComma)) != std::string::npos;) s.replace(p, arComma.size(), ",");
    std::replace(s.begin(), s.end(), ';', ',');
    std::vector<std::string> out;
    for (auto& t : clc::idn::split(s, ',')) { std::string x = clc::idn::trim(t); if (!x.empty()) out.push_back(x); }
    return out;
}
static std::vector<std::string> listOf(const Value* v) {
    std::vector<std::string> out;
    if (!v) return out;
    if (v->type == Value::Type::STRING) return splitList(v->str);
    if (v->type == Value::Type::ARRAY && v->array)
        for (const auto& x : *v->array) if (x.type == Value::Type::STRING && !x.str.empty()) out.push_back(x.str);
    return out;
}

static void addIcon(std::vector<clc::IconEntry>& out, const Value& v, const std::string& sizeHint = "") {
    clc::IconEntry e;
    if (v.type == Value::Type::STRING) { e.src = v.str; e.sizes = sizeHint; }
    else if (v.type == Value::Type::MAP && v.map) {
        e.src = textOf(*v.map, {"src", "path", "file", "url"});
        e.sizes = textOf(*v.map, {"sizes", "size"});
        if (e.sizes.empty()) e.sizes = sizeHint;
        if (!e.sizes.empty() && e.sizes.find('x') == std::string::npos && e.sizes != "any" &&
            std::all_of(e.sizes.begin(), e.sizes.end(), [](unsigned char c) { return std::isdigit(c); }))
            e.sizes += "x" + e.sizes; // 192 -> 192x192
        e.type = textOf(*v.map, {"type", "mime"});
        e.purpose = textOf(*v.map, {"purpose"});
    } else return;
    if (!e.src.empty()) out.push_back(e);
}

// قاموس → AppIdentity (دون إكمال القيم المشتقة).
static clc::AppIdentity identityFromMap(const MapData& m) {
    clc::AppIdentity a;
    std::string k = clc::idn::lower(textOf(m, {"kind", "type"}));
    if (k == "site" || k == "app") a.kind = k;
    a.id = textOf(m, {"id", "app_id", "appId", "package"});
    a.name = textOf(m, {"name", "title"});
    a.shortName = textOf(m, {"short_name", "shortName", "short"});
    a.version = textOf(m, {"version"});
    if (const Value* vc = pick(m, {"version_code", "versionCode", "build"}); vc && vc->type == Value::Type::NUMBER) a.versionCode = static_cast<int64_t>(vc->number);
    a.channel = textOf(m, {"channel"});
    a.author = textOf(m, {"author"});
    a.publisher = textOf(m, {"publisher"});
    a.license = textOf(m, {"license"});
    a.homepage = textOf(m, {"homepage", "url", "website"});
    a.lang = textOf(m, {"lang", "language"});
    a.dir = clc::idn::lower(textOf(m, {"dir", "direction"}));
    a.category = textOf(m, {"category"});
    a.description = textOf(m, {"description", "desc"});
    a.shortDescription = textOf(m, {"short_description", "shortDescription", "tagline"});
    a.themeColor = textOf(m, {"theme_color", "themeColor"});
    a.backgroundColor = textOf(m, {"background_color", "backgroundColor"});
    a.display = textOf(m, {"display"});
    a.orientation = textOf(m, {"orientation"});
    a.startUrl = textOf(m, {"start_url", "startUrl"});
    a.scope = textOf(m, {"scope"});
    a.favicon = textOf(m, {"favicon"});

    // الأيقونات: icon (نص/قاموس) ثم icons (مصفوفة نصوص/قواميس، أو قاموس {"192": "a.png", "512": "b.png"})
    if (const Value* i = pick(m, {"icon"})) addIcon(a.icons, *i);
    if (const Value* is = pick(m, {"icons"})) {
        if (is->type == Value::Type::ARRAY && is->array) for (const auto& x : *is->array) addIcon(a.icons, x);
        else if (is->type == Value::Type::MAP && is->map) {
            if (is->map->findValue(Value::string("src"))) addIcon(a.icons, *is);
            else for (const auto& kv : *is->map) {
                std::string sz = kv.first.type == Value::Type::STRING ? kv.first.str : kv.first.toDisplayString();
                if (sz.find('x') == std::string::npos && sz != "any") sz += "x" + sz;
                addIcon(a.icons, kv.second, sz);
            }
        } else addIcon(a.icons, *is);
    }
    a.iconsDir = textOf(m, {"icons_dir", "iconsDir", "icons_from"});
    // اللغات: قاموس {ar: {...}} أو مصفوفة [{lang, ...}]
    auto addLocale = [&](const std::string& lang, const MapData& lm) {
        clc::LocaleInfo l; l.lang = lang;
        l.name = textOf(lm, {"name", "title"});
        l.shortName = textOf(lm, {"short_name", "shortName", "short"});
        l.description = textOf(lm, {"description", "desc"});
        l.shortDescription = textOf(lm, {"short_description", "shortDescription", "tagline"});
        l.url = textOf(lm, {"url", "homepage", "canonical"});
        l.ogImage = textOf(lm, {"og_image", "ogImage", "image"});
        const MapData* ls = nullptr;
        if (const Value* sv = pick(lm, {"seo"}); sv && sv->type == Value::Type::MAP && sv->map) ls = sv->map.get();
        l.seoTitle = ls ? textOf(*ls, {"title"}) : textOf(lm, {"seo_title", "seoTitle"});
        l.seoDescription = ls ? textOf(*ls, {"description", "desc"}) : textOf(lm, {"seo_description", "seoDescription"});
        const Value* kw = ls ? pick(*ls, {"keywords"}) : nullptr;
        if (!kw) kw = pick(lm, {"keywords", "seo_keywords", "seoKeywords"});
        l.keywords = listOf(kw);
        a.locales.push_back(l);
    };
    if (const Value* lv = pick(m, {"locales", "translations", "i18n"})) {
        if (lv->type == Value::Type::MAP && lv->map) {
            for (const auto& kv : *lv->map)
                if (kv.second.type == Value::Type::MAP && kv.second.map)
                    addLocale(kv.first.type == Value::Type::STRING ? kv.first.str : kv.first.toDisplayString(), *kv.second.map);
        } else if (lv->type == Value::Type::ARRAY && lv->array) {
            for (const auto& x : *lv->array)
                if (x.type == Value::Type::MAP && x.map) addLocale(textOf(*x.map, {"lang", "language", "code"}), *x.map);
        }
    }
    // سجل الإصدارات: مصفوفة [{version, date, channel, notes}] أو قاموس {"1.0.0": "ملاحظات"}؛ notes نص أو مصفوفة أسطر
    auto notesOf = [&](const Value* v) {
        if (!v) return std::string();
        if (v->type == Value::Type::ARRAY) return clc::idn::join(listOf(v), "\n");
        return asText(v);
    };
    if (const Value* rv = pick(m, {"releases", "history", "changelog"})) {
        if (rv->type == Value::Type::ARRAY && rv->array) {
            for (const auto& x : *rv->array) {
                if (x.type != Value::Type::MAP || !x.map) continue;
                clc::ReleaseInfo r; r.version = textOf(*x.map, {"version"}); r.date = textOf(*x.map, {"date"});
                r.channel = textOf(*x.map, {"channel"}); r.notes = notesOf(pick(*x.map, {"notes", "changes", "note"}));
                a.releases.push_back(r);
            }
        } else if (rv->type == Value::Type::MAP && rv->map) {
            for (const auto& kv : *rv->map) {
                clc::ReleaseInfo r; r.version = kv.first.type == Value::Type::STRING ? kv.first.str : kv.first.toDisplayString();
                if (kv.second.type == Value::Type::MAP && kv.second.map) { r.date = textOf(*kv.second.map, {"date"}); r.channel = textOf(*kv.second.map, {"channel"}); r.notes = notesOf(pick(*kv.second.map, {"notes", "changes"})); }
                else r.notes = notesOf(&kv.second);
                a.releases.push_back(r);
            }
        }
    }
    // SEO: قاموس seo + اختصارات مسطّحة
    clc::SeoInfo& s = a.seo;
    const MapData* sm = nullptr;
    if (const Value* sv = pick(m, {"seo"}); sv && sv->type == Value::Type::MAP && sv->map) sm = sv->map.get();
    auto S = [&](std::initializer_list<const char*> nested, std::initializer_list<const char*> flat) {
        std::string v = sm ? textOf(*sm, nested) : "";
        return v.empty() ? textOf(m, flat) : v;
    };
    s.title = S({"title"}, {"seo_title", "seoTitle"});
    s.description = S({"description", "desc"}, {"seo_description", "seoDescription"});
    s.canonical = S({"canonical", "url"}, {"canonical"});
    s.robots = S({"robots"}, {"robots"});
    s.author = S({"author"}, {});
    s.ogTitle = S({"og_title", "ogTitle"}, {"og_title"});
    s.ogDescription = S({"og_description", "ogDescription"}, {"og_description"});
    s.ogImage = S({"og_image", "ogImage", "image"}, {"og_image", "ogImage"});
    s.ogType = S({"og_type", "ogType"}, {});
    s.ogSiteName = S({"og_site_name", "ogSiteName", "site_name"}, {});
    s.twitterCard = S({"twitter_card", "twitterCard"}, {});
    s.twitterSite = S({"twitter_site", "twitterSite"}, {});
    s.locale = S({"locale"}, {});
    const Value* kw = sm ? pick(*sm, {"keywords"}) : nullptr;
    if (!kw) kw = pick(m, {"keywords", "seo_keywords", "seoKeywords"});
    s.keywords = listOf(kw);
    if (const Value* jl = sm ? pick(*sm, {"json_ld", "jsonLd", "jsonld"}) : nullptr; jl && jl->type == Value::Type::BOOL) s.jsonLd = jl->boolean;
    if (const Value* al = sm ? pick(*sm, {"alternates", "alternate", "hreflang"}) : nullptr) {
        if (al->type == Value::Type::MAP && al->map)
            for (const auto& kv : *al->map) s.alternates.push_back({kv.first.toDisplayString(), asText(&kv.second)});
        else if (al->type == Value::Type::ARRAY && al->array)
            for (const auto& x : *al->array)
                if (x.type == Value::Type::MAP && x.map) s.alternates.push_back({textOf(*x.map, {"lang", "hreflang"}), textOf(*x.map, {"url", "href"})});
    }
    return a;
}

static Value iconsToValue(const std::vector<clc::IconEntry>& icons) {
    ArrayData arr;
    for (const auto& ic : icons)
        arr.push_back(mkMap({{"src", strV(ic.src)}, {"sizes", strV(ic.sizes)}, {"type", strV(ic.type)}, {"purpose", strV(ic.purpose)}}));
    return newArray(std::move(arr));
}
static Value localesToValue(const clc::AppIdentity& a) {
    MapData out;
    for (const auto& l : a.locales) {
        ArrayData kws; for (auto& k : l.keywords) kws.push_back(strV(k));
        out.push_back({strV(l.lang), mkMap({{"name", strV(l.name)}, {"short_name", strV(l.shortName)}, {"description", strV(l.description)},
                                            {"short_description", strV(l.shortDescription)}, {"seo_title", strV(l.seoTitle)},
                                            {"seo_description", strV(l.seoDescription)}, {"keywords", newArray(std::move(kws))},
                                            {"url", strV(l.url)}, {"og_image", strV(l.ogImage)}})});
    }
    return newMap(std::move(out));
}
static Value releasesToValue(const clc::AppIdentity& a) {
    ArrayData arr;
    for (const auto& r : a.releases) {
        ArrayData notes; for (auto& n : clc::idn::split(r.notes, '\n')) if (!clc::idn::trim(n).empty()) notes.push_back(strV(n));
        arr.push_back(mkMap({{"version", strV(r.version)}, {"date", strV(r.date)}, {"channel", strV(r.channel)}, {"notes", newArray(std::move(notes))}}));
    }
    return newArray(std::move(arr));
}
// ترميز JSON مضغوط لقيمة Rin (قاموس/مصفوفة/نص/رقم/منطقي/nil) — لملف app.json الذي يقرؤه indsin2web --app-dir.
static void valueJson(const Value& v, std::string& out, int depth = 0) {
    if (depth > 16) { out += "null"; return; }
    switch (v.type) {
        case Value::Type::STRING: out += clc::idn::jstr(v.str); break;
        case Value::Type::NUMBER: out += v.toDisplayString(); break;
        case Value::Type::BOOL: out += v.boolean ? "true" : "false"; break;
        case Value::Type::ARRAY:
            out += '[';
            if (v.array) for (size_t i = 0; i < v.array->size(); ++i) { if (i) out += ','; valueJson((*v.array)[i], out, depth + 1); }
            out += ']'; break;
        case Value::Type::MAP:
            out += '{';
            if (v.map) { bool first = true; for (const auto& kv : *v.map) { if (!first) out += ','; first = false; out += clc::idn::jstr(kv.first.toDisplayString()); out += ':'; valueJson(kv.second, out, depth + 1); } }
            out += '}'; break;
        default: out += "null";
    }
}
static Value identityToValue(const clc::AppIdentity& a) {
    ArrayData kws; for (auto& k : a.seo.keywords) kws.push_back(strV(k));
    MapData alts; for (auto& al : a.seo.alternates) alts.push_back({strV(al.first), strV(al.second)});
    Value seo = mkMap({{"title", strV(a.seo.title)}, {"description", strV(a.seo.description)}, {"keywords", newArray(std::move(kws))},
                       {"canonical", strV(a.seo.canonical)}, {"robots", strV(a.seo.robots)}, {"author", strV(a.seo.author)},
                       {"og_title", strV(a.seo.ogTitle)}, {"og_description", strV(a.seo.ogDescription)}, {"og_image", strV(a.seo.ogImage)},
                       {"og_type", strV(a.seo.ogType)}, {"og_site_name", strV(a.seo.ogSiteName)},
                       {"twitter_card", strV(a.seo.twitterCard)}, {"twitter_site", strV(a.seo.twitterSite)},
                       {"locale", strV(a.seo.locale)}, {"alternates", newMap(std::move(alts))}, {"json_ld", boolV(a.seo.jsonLd)}});
    clc::idn::Semver sv; bool ok = clc::idn::parseSemver(a.version, sv);
    Value parts = ok ? mkMap({{"major", numV(double(sv.major))}, {"minor", numV(double(sv.minor))}, {"patch", numV(double(sv.patch))},
                              {"pre", strV(sv.pre)}, {"build", strV(sv.build)}}) : Value::nil();
    return mkMap({{"kind", strV(a.kind)}, {"id", strV(a.id)}, {"name", strV(a.name)}, {"short_name", strV(a.shortName)},
                  {"version", strV(a.version)}, {"version_code", numV(double(a.versionCode))}, {"version_parts", parts},
                  {"channel", strV(a.channel)}, {"author", strV(a.author)}, {"publisher", strV(a.publisher)},
                  {"license", strV(a.license)}, {"homepage", strV(a.homepage)}, {"lang", strV(a.lang)}, {"dir", strV(a.dir)},
                  {"category", strV(a.category)}, {"description", strV(a.description)}, {"short_description", strV(a.shortDescription)},
                  {"theme_color", strV(a.themeColor)}, {"background_color", strV(a.backgroundColor)}, {"display", strV(a.display)},
                  {"orientation", strV(a.orientation)}, {"start_url", strV(a.startUrl)}, {"scope", strV(a.scope)},
                  {"favicon", strV(a.favicon)}, {"icons", iconsToValue(a.icons)}, {"icons_dir", strV(a.iconsDir)}, {"seo", seo},
                  {"locales", localesToValue(a)}, {"releases", releasesToValue(a)}});
}
static Value issuesToValue(const std::vector<clc::IdentityIssue>& v) {
    ArrayData arr;
    for (const auto& i : v) arr.push_back(mkMap({{"level", strV(i.level)}, {"code", strV(i.code)}, {"field", strV(i.field)}, {"message", strV(i.message)}}));
    return newArray(std::move(arr));
}
static const Value* optv(const Value* o, const char* k) {
    if (!o || o->type != Value::Type::MAP || !o->map) return nullptr;
    return o->map->findValue(Value::string(k));
}
static std::string optStr(const Value* o, const char* k, const std::string& def = "") {
    const Value* v = optv(o, k); return (v && v->type == Value::Type::STRING) ? v->str : def;
}
static bool optBool(const Value* o, const char* k, bool def) { const Value* v = optv(o, k); return (v && v->type != Value::Type::NIL) ? v->isTruthy() : def; }

// أصل الموقع (https://host) من رابط مطلق.
static std::string originOf(const std::string& url) {
    size_t p = url.find("://");
    if (p == std::string::npos) return "";
    size_t e = url.find('/', p + 3);
    return e == std::string::npos ? url : url.substr(0, e);
}
// العنوان الأساسي للموقع: canonical إن كان مطلقاً وإلا homepage إن كان مطلقاً.
static std::string siteOrigin(const clc::AppIdentity& id) {
    std::string o = originOf(id.seo.canonical);
    return o.empty() ? originOf(id.homepage) : o;
}
static std::string b64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; o.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == in.size()) { unsigned v = (unsigned char)in[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == in.size()) { unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
    return o;
}
// data: URI لأيقونة صغيرة (≤ maxBytes) من base/rel؛ "" إن كبرت أو غابت أو ليست صورة.
static std::string embedIcon(const std::string& base, const std::string& rel, size_t maxBytes) {
    if (base.empty() || maxBytes == 0 || rel.empty() || clc::idn::isAbsUrl(rel) || rel.find("..") != std::string::npos || rel[0] == '/') return "";
    std::string mime = clc::idn::mimeOfPath(rel);
    if (mime.empty()) return "";
    std::error_code ec; fs::path p = fs::path(base) / rel;
    if (!fs::is_regular_file(p, ec) || fs::is_symlink(p, ec)) return "";
    auto sz = fs::file_size(p, ec);
    if (ec || sz > maxBytes) return "";
    std::ifstream in(p, std::ios::binary); std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return "data:" + mime + ";base64," + b64(data);
}
static std::string todayIso() {
    std::time_t t = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[16]; std::strftime(b, sizeof b, "%Y-%m-%d", &tm); return b;
}

} // namespace capp

void Interpreter::registerNativesContainerApp() {
    using namespace capp;
    using Args = std::vector<Value>;

    // src → هوية (اسم حاوية | قاموس). fromContainer يُملأ باسم الحاوية الفعلي.
    auto sourceIdentity = [this](const Value& v, const std::string& who, int line, std::string* fromContainer) -> clc::AppIdentity {
        if (v.type == Value::Type::MAP && v.map) return identityFromMap(*v.map);
        if (v.type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + who + "' يتوقع اسم حاوية أو قاموس هوية لكنه تلقّى " + v.typeName());
        std::string c = canonContainer(v.str);
        auto it = containers.find(c);
        if (it == containers.end() || !it->second)
            throw diagErr(diag::Code::E0014_InvalidContainer, line, who + ": الحاوية '" + v.str + "' غير موجودة");
        MapData m;
        for (const auto& kv : it->second->values) m.push_back({Value::string(kv.first), kv.second});
        clc::AppIdentity a = identityFromMap(m);
        auto kit = containerKinds.find(c);
        if (a.name.empty()) a.name = v.str;                       // الاسم الافتراضي = اسم الحاوية
        if (kit != containerKinds.end() && kit->second != ContainerKind::APP && a.version.empty()) a.version = "0.1.0";
        if (a.version.empty()) a.version = "0.1.0";
        if (fromContainer) *fromContainer = c;
        return a;
    };
    auto guard = [](int line, const std::string& who, const std::function<Value()>& f) -> Value {
        try { return f(); }
        catch (const std::invalid_argument& e) { throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": " + e.what()); }
        catch (const clc::ClcFormatError& e) { throw diagErr(diag::Code::E0036_IOFailure, line, who + ": " + e.what()); }
        catch (const std::filesystem::filesystem_error& e) { throw diagErr(diag::Code::E0036_IOFailure, line, who + ": " + e.what()); }
    };
    // قاعدة ملفات للتحقق من الأيقونات: opts.dir (يمرّ عبر resolvePath لعزل المشروع).
    auto baseDirOf = [this](const Value* opts, int line) -> std::string {
        std::string d = optStr(opts, "dir");
        return d.empty() ? std::string() : resolvePath(d, line);
    };
    auto existsIn = [](const std::string& base) -> std::function<bool(const std::string&)> {
        if (base.empty()) return nullptr;
        return [base](const std::string& rel) {
            std::error_code ec;
            if (!rel.empty() && rel.back() == '/') return fs::is_directory(fs::path(base) / rel, ec); // «مجلد/» = فحص مجلد
            return fs::is_regular_file(fs::path(base) / rel, ec);
        };
    };
    auto writeText = [this](const std::string& rawPath, const std::string& text, int line, const std::string& who) {
        std::string full = resolvePath(rawPath, line);
        ensureParentDir(full);
        std::string tmp = full + ".rin-tmp";
        { std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
          if (!out) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر إنشاء '" + rawPath + "'");
          out.write(text.data(), static_cast<std::streamsize>(text.size())); out.flush();
          if (!out.good()) { std::error_code ec; fs::remove(tmp, ec); throw diagErr(diag::Code::E0036_IOFailure, line, who + ": فشلت الكتابة إلى '" + rawPath + "'"); } }
        std::error_code ec; fs::rename(tmp, full, ec);
        if (ec) { fs::remove(full, ec); std::error_code e2; fs::rename(tmp, full, e2); if (e2) { fs::remove(tmp, e2); throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر استبدال '" + rawPath + "'"); } }
    };

    // ------------------------------------------------------------------ تسجيل بالاسمين
    auto reg = [this](const char* shortName, std::function<Value(Args&, int)> fn) {
        natives[std::string("container.app") + shortName] = fn;      // container.appInfo
        std::string lowered = shortName; lowered[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[0])));
        natives["app." + lowered] = fn;                                 // app.info
    };

    // هوية جاهزة للتوليد: لغة (opts.lang) ثم الإكمال بجذر الملفات (opts.dir) — الأخطاء تُترجَم لخطأ معاملات واضح.
    auto resolve = [baseDirOf](clc::AppIdentity id, const Value* opts, int line) -> clc::AppIdentity {
        id = clc::localizeIdentity(id, optStr(opts, "lang"));
        clc::completeIdentity(id, baseDirOf(opts, line));
        return id;
    };

    reg("Info", [this, sourceIdentity, resolve, guard](Args& a, int line) -> Value {
        need("container.appInfo", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        return guard(line, "container.appInfo", [&] {
            std::string c;
            clc::AppIdentity id = resolve(sourceIdentity(a[0], "container.appInfo", line, &c), opts, line);
            Value v = identityToValue(id);
            if (!c.empty()) v.map->put(strV("container"), strV(c));
            return v;
        });
    });

    reg("Validate", [this, sourceIdentity, baseDirOf, existsIn, guard](Args& a, int line) -> Value {
        need("container.appValidate", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        return guard(line, "container.appValidate", [&] {
            clc::AppIdentity id = sourceIdentity(a[0], "container.appValidate", line, nullptr);
            std::string base = baseDirOf(opts, line);
            clc::completeIdentity(id, base);
            auto issues = clc::validateIdentity(id, existsIn(base));
            if (!optBool(opts, "info", true))
                issues.erase(std::remove_if(issues.begin(), issues.end(), [](const clc::IdentityIssue& i) { return i.level == "info"; }), issues.end());
            return issuesToValue(issues);
        });
    });

    // خيارات توليد head المشتركة بين Head وExport وInject.
    auto headOptions = [baseDirOf](const Value* opts, int line) -> clc::HeadOptions {
        clc::HeadOptions ho;
        if (const Value* m = optv(opts, "manifest")) {
            if (m->type == Value::Type::STRING) ho.manifestHref = m->str;
            else if (!m->isTruthy()) ho.manifestHref.clear();
        }
        if (const Value* ind = optv(opts, "indent"); ind && ind->type == Value::Type::STRING) ho.indent = ind->str;
        ho.charsetViewport = optBool(opts, "charset", true);
        ho.assetsUrl = optStr(opts, "assets_url");
        if (const Value* e = optv(opts, "embed"); e && e->type == Value::Type::NUMBER && e->number > 0) {
            std::string base = baseDirOf(opts, line); size_t lim = size_t(e->number);
            if (!base.empty()) ho.embed = [base, lim](const std::string& src) { return embedIcon(base, src, lim); };
        } else if (e && e->type == Value::Type::BOOL && e->boolean) {
            std::string base = baseDirOf(opts, line);
            if (!base.empty()) ho.embed = [base](const std::string& src) { return embedIcon(base, src, 8192); };
        }
        return ho;
    };

    reg("Head", [this, sourceIdentity, resolve, headOptions, guard](Args& a, int line) -> Value {
        need("container.appHead", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        return guard(line, "container.appHead", [&] {
        clc::AppIdentity id = resolve(sourceIdentity(a[0], "container.appHead", line, nullptr), opts, line);
        return strV(clc::buildHead(id, headOptions(opts, line)));
        });
    });
    reg("Manifest", [this, sourceIdentity, resolve, guard](Args& a, int line) -> Value {
        need("container.appManifest", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        return guard(line, "container.appManifest", [&] {
            return strV(clc::buildManifest(resolve(sourceIdentity(a[0], "container.appManifest", line, nullptr), opts, line)));
        });
    });

    reg("Robots", [this, sourceIdentity](Args& a, int line) -> Value {
        need("container.appRobots", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        clc::AppIdentity id = sourceIdentity(a[0], "container.appRobots", line, nullptr);
        clc::completeIdentity(id);
        std::string sm = optStr(opts, "sitemap");
        std::vector<std::string> dis = listOf(optv(opts, "disallow"));
        if (sm.empty() && optBool(opts, "autoSitemap", true)) { std::string o = siteOrigin(id); if (!o.empty()) sm = o + "/sitemap.xml"; }
        return strV(clc::buildRobots(id, sm, dis));
    });

    reg("Sitemap", [this, sourceIdentity](Args& a, int line) -> Value {
        need("container.appSitemap", a, 2, 3, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        clc::AppIdentity id = sourceIdentity(a[0], "container.appSitemap", line, nullptr);
        clc::completeIdentity(id);
        std::vector<std::string> paths = listOf(&a[1]);
        if (paths.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "container.appSitemap: مصفوفة المسارات فارغة");
        std::string base = optStr(opts, "base");
        if (base.empty()) base = siteOrigin(id);
        if (!clc::idn::isAbsUrl(base)) throw diagErr(diag::Code::E0007_InvalidArguments, line, "container.appSitemap: لا يوجد عنوان أساسي مطلق — حدّد homepage/seo.canonical أو opts.base");
        std::string lm = optStr(opts, "lastmod");
        if (lm.empty() && optBool(opts, "today", false)) lm = todayIso();
        return strV(clc::buildSitemap(base, paths, lm));
    });

    reg("Bump", [this, guard](Args& a, int line) -> Value {
        need("container.appBump", a, 2, 3, line);
        std::string who = "container.appBump";
        const Value* bopts = a.size() > 2 ? &a[2] : nullptr; // {notes, date, channel}: يُسجَّل الإصدار الجديد في releases
        std::string name = str(a[0], who, line), part = str(a[1], who, line);
        std::string c = canonContainer(name);
        auto it = containers.find(c);
        if (it == containers.end() || !it->second) throw diagErr(diag::Code::E0014_InvalidContainer, line, who + ": الحاوية '" + name + "' غير موجودة");
        return guard(line, who, [&] {
            auto& vals = it->second->values;
            auto vit = vals.find("version");
            std::string cur = (vit != vals.end() && vit->second.type == Value::Type::STRING) ? vit->second.str : "0.1.0";
            std::string next = clc::idn::bumpSemver(cur, part);
            vals["version"] = strV(next);
            auto cit = vals.find("version_code"); // يُزاد 1 فقط إن عُرِّف صراحةً (وإلا يُشتق من الإصدار)
            if (cit != vals.end() && cit->second.type == Value::Type::NUMBER) cit->second = numV(cit->second.number + 1);
            const Value* nv = optv(bopts, "notes");
            if (nv && nv->type != Value::Type::NIL) { // سجل إصدار جديد في الحاوية نفسها
                std::string notes = nv->type == Value::Type::ARRAY ? clc::idn::join(listOf(nv), "\n") : asText(nv);
                std::string ch = optStr(bopts, "channel");
                if (ch.empty()) { auto chit = vals.find("channel"); if (chit != vals.end() && chit->second.type == Value::Type::STRING) ch = chit->second.str; }
                ArrayData arr;
                auto rit = vals.find("releases");
                if (rit != vals.end() && rit->second.type == Value::Type::ARRAY && rit->second.array) arr = *rit->second.array;
                ArrayData lines; for (auto& l : clc::idn::split(notes, '\n')) if (!clc::idn::trim(l).empty()) lines.push_back(strV(l));
                arr.push_back(mkMap({{"version", strV(next)}, {"date", strV(optStr(bopts, "date", todayIso()))}, {"channel", strV(ch)}, {"notes", newArray(std::move(lines))}}));
                vals["releases"] = newArray(std::move(arr));
            }
            return strV(next);
        });
    });

    reg("Export", [this, sourceIdentity, baseDirOf, headOptions, guard, writeText](Args& a, int line) -> Value {
        need("container.appExport", a, 2, 3, line);
        std::string who = "container.appExport";
        std::string dir = str(a[1], who, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        return guard(line, who, [&] {
            clc::AppIdentity raw = sourceIdentity(a[0], who, line, nullptr);
            std::string files = baseDirOf(opts, line);
            while (!dir.empty() && dir.back() == '/') dir.pop_back();
            std::string pre = dir.empty() ? "" : dir + "/";
            ArrayData written;
            auto put = [&](const std::string& rel, const std::string& text) { writeText(pre + rel, text, line, who); written.push_back(strV(pre + rel)); };
            auto finish = [&](const std::string& lang) { clc::AppIdentity id = clc::localizeIdentity(raw, lang); clc::completeIdentity(id, files); return id; };
            // اللغة الأساسية: head.html/manifest.webmanifest؛ وكل لغة في locales: head.<lang>.html/manifest.<lang>.webmanifest
            std::vector<std::string> langs{""};
            if (optBool(opts, "all_langs", true)) for (auto& l : raw.locales) langs.push_back(l.lang);
            for (auto& lg : langs) {
                clc::AppIdentity id = finish(lg);
                std::string sfx = lg.empty() ? "" : "." + lg;
                clc::HeadOptions ho = headOptions(opts, line);
                if (ho.manifestHref == "manifest.webmanifest") ho.manifestHref = "manifest" + sfx + ".webmanifest";
                put("head" + sfx + ".html", clc::buildHead(id, ho));
                put("manifest" + sfx + ".webmanifest", clc::buildManifest(id));
            }
            clc::AppIdentity id = finish("");
            { std::string js; valueJson(identityToValue(id), js); put("app.json", js + "\n"); } // lang/dir وغيرها لـ indsin2web --app-dir
            std::string base = optStr(opts, "base");
            if (base.empty()) base = siteOrigin(id);
            std::vector<std::string> paths = listOf(optv(opts, "paths"));
            std::string smUrl = clc::idn::isAbsUrl(base) && !paths.empty() ? base + "/sitemap.xml" : "";
            put("robots.txt", clc::buildRobots(id, smUrl, listOf(optv(opts, "disallow"))));
            if (!smUrl.empty()) put("sitemap.xml", clc::buildSitemap(base, paths, optStr(opts, "lastmod")));
            if (!id.releases.empty()) put("CHANGELOG.md", clc::buildChangelog(id));
            // opts.page = ملف indsin (.rin): نعيد سطر الأمر الجاهز لبناء صفحة الويب بهذه الهوية
            std::string page = optStr(opts, "page");
            std::string cmd = page.empty() ? "" : "indsin2web " + page + " -o " + pre + "index.html --app-dir " + (dir.empty() ? "." : dir);
            return mkMap({{"dir", strV(dir)}, {"files", newArray(std::move(written))}, {"indsin2web", strV(cmd)}});
        });
    });

    reg("Pack", [this, sourceIdentity, existsIn, guard](Args& a, int line) -> Value {
        need("container.appPack", a, 3, 4, line);
        std::string who = "container.appPack";
        const Value* opts = a.size() > 3 ? &a[3] : nullptr;
        std::string srcRaw = str(a[1], who, line), outRaw = str(a[2], who, line);
        std::string srcDir = resolvePath(srcRaw, line), outPath = resolvePath(outRaw, line);
        return guard(line, who, [&] {
            std::error_code ec;
            if (!fs::is_directory(srcDir, ec)) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": المجلد '" + srcRaw + "' غير موجود");
            clc::AppIdentity id = sourceIdentity(a[0], who, line, nullptr);
            clc::completeIdentity(id, srcDir);
            auto issues = clc::validateIdentity(id, existsIn(srcDir));
            std::string errs;
            for (auto& i : issues) if (i.level == "error") errs += "\n  • [" + i.field + "] " + i.message;
            if (!errs.empty() && !optBool(opts, "force", false))
                throw diagErr(diag::Code::E0014_InvalidContainer, line, who + ": الهوية بها أخطاء، لن تُبنى الحاوية (استعمل opts.force=true لتجاوزها):" + errs);
            { // الملف الناتج داخل المجلد المُحزَّم سيُحزَّم هو نفسه — نمنعه صراحةً
                fs::path sd = fs::weakly_canonical(srcDir, ec), od = fs::weakly_canonical(outPath, ec);
                auto rel = od.lexically_relative(sd);
                if (!rel.empty() && *rel.begin() != "..")
                    throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": ملف الحاوية الناتج '" + outRaw + "' داخل المجلد المُحزَّم '" + srcRaw + "' — اكتبه خارجه");
            }
            ensureParentDir(outPath);
            clc::PackOptions po;
            double lv = 2; if (const Value* l = optv(opts, "level"); l && l->type == Value::Type::NUMBER) lv = l->number;
            if (lv < 0 || lv > 5) throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": level بين 0 و5");
            po.level = static_cast<clc::Level>(static_cast<int>(lv));
            po.quiet = true;
            po.metadata.entryPoint = optStr(opts, "entry");
            po.metadata.rinVersion = optStr(opts, "rin_version");
            clc::applyIdentity(po.metadata, id);
            auto st = clc::packDirectory(srcDir, outPath, po);
            return mkMap({{"path", strV(outRaw)}, {"size", numV(double(st.compressedSize))}, {"original", numV(double(st.originalSize))},
                          {"files", numV(double(st.fileCount))}, {"ratio", numV(st.ratioPercent())},
                          {"name", strV(id.name)}, {"version", strV(id.version)}, {"issues", issuesToValue(issues)}});
        });
    });

    reg("Open", [this, guard](Args& a, int line) -> Value {
        need("container.appOpen", a, 1, 1, line);
        std::string raw = str(a[0], "container.appOpen", line);
        std::string full = resolvePath(raw, line);
        return guard(line, "container.appOpen", [&] {
            auto info = clc::readContainerInfo(full);
            clc::AppIdentity id = clc::readIdentity(info.metadata);
            Value v = identityToValue(id);
            v.map->put(strV("has_identity"), boolV(clc::hasIdentity(info.metadata)));
            v.map->put(strV("files"), numV(double(info.files.size())));
            v.map->put(strV("entry_point"), strV(info.metadata.entryPoint));
            v.map->put(strV("rin_version"), strV(info.metadata.rinVersion));
            v.map->put(strV("created"), strV(info.metadata.created));
            v.map->put(strV("modified"), strV(info.metadata.modified));
            return v;
        });
    });

    // ------------------------------------------------------------------ قائمة الحاويات
    auto appContainers = [this]() {
        std::vector<std::string> names;
        for (const auto& kv : containerKinds) if (kv.second == ContainerKind::APP && containers.count(kv.first)) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return names;
    };
    reg("List", [this, appContainers](Args& a, int line) -> Value {
        need("container.appList", a, 0, 0, line);
        ArrayData out;
        for (auto& n : appContainers()) {
            MapData m; for (const auto& kv : containers.at(n)->values) m.push_back({Value::string(kv.first), kv.second});
            clc::AppIdentity id = identityFromMap(m);
            if (id.name.empty()) id.name = n;
            if (id.version.empty()) id.version = "0.1.0";
            out.push_back(mkMap({{"name", strV(n)}, {"kind", strV(id.kind)}, {"id", strV(id.id)}, {"title", strV(id.name)},
                                 {"version", strV(id.version)}, {"lang", strV(id.lang)}}));
        }
        return newArray(std::move(out));
    });
    reg("Pick", [this, appContainers](Args& a, int line) -> Value {
        need("container.appPick", a, 0, 1, line);
        auto names = appContainers();
        if (!a.empty() && a[0].type == Value::Type::STRING && !a[0].str.empty()) {
            std::string c = canonContainer(a[0].str);
            if (std::find(names.begin(), names.end(), c) == names.end()) {
                std::string have; for (auto& n : names) have += (have.empty() ? "" : "، ") + n;
                throw diagErr(diag::Code::E0014_InvalidContainer, line, "container.appPick: لا توجد حاوية هوية باسم '" + a[0].str + "'" + (have.empty() ? " (لا توجد حاويات container.app/site)" : " — المتاح: " + have));
            }
            return strV(c);
        }
        if (names.empty()) throw diagErr(diag::Code::E0014_InvalidContainer, line, "container.appPick: لا توجد أي حاوية container.app/container.site معرَّفة");
        if (names.size() > 1) {
            std::string have; for (auto& n : names) have += (have.empty() ? "" : "، ") + n;
            throw diagErr(diag::Code::E0014_InvalidContainer, line, "container.appPick: عدة حاويات هوية (" + have + ") — حدّد الاسم");
        }
        return strV(names[0]);
    });

    // ------------------------------------------------------------------ سجل الإصدارات
    reg("Changelog", [this, sourceIdentity, guard](Args& a, int line) -> Value {
        need("container.appChangelog", a, 1, 1, line);
        return guard(line, "container.appChangelog", [&] { return strV(clc::buildChangelog(sourceIdentity(a[0], "container.appChangelog", line, nullptr))); });
    });

    // ------------------------------------------------------------------ حقن الهوية في صفحة HTML
    reg("Inject", [this, sourceIdentity, resolve, headOptions, guard, writeText](Args& a, int line) -> Value {
        need("container.appInject", a, 2, 3, line);
        std::string who = "container.appInject";
        std::string fileRaw = str(a[1], who, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        return guard(line, who, [&] {
            std::string full = resolvePath(fileRaw, line);
            std::ifstream in(full, std::ios::binary);
            if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر فتح '" + fileRaw + "'");
            std::string page((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            clc::AppIdentity id = resolve(sourceIdentity(a[0], who, line, nullptr), opts, line);
            clc::HeadOptions ho = headOptions(opts, line);
            if (!optv(opts, "charset")) ho.charsetViewport = false; // الصفحة تملك charset/viewport عادةً
            if (!optv(opts, "manifest")) ho.manifestHref.clear();   // لا رابط manifest ما لم يُطلب صراحةً
            std::string mode;
            std::string res = clc::injectHeadIntoHtml(page, clc::buildHead(id, ho), optBool(opts, "strip", true), optStr(opts, "indent", ""), mode);
            bool changed = res != page, check = optBool(opts, "check", false);
            if (changed && !check) writeText(fileRaw, res, line, who);
            return mkMap({{"path", strV(fileRaw)}, {"mode", strV(mode)}, {"changed", boolV(changed)}, {"written", boolV(changed && !check)},
                          {"in_sync", boolV(!changed)}});
        });
    });

    // ------------------------------------------------------------------ project.og.urin
    reg("Urin", [this, sourceIdentity, resolve, guard](Args& a, int line) -> Value {
        need("container.appUrin", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        return guard(line, "container.appUrin", [&] {
            clc::AppIdentity id = resolve(sourceIdentity(a[0], "container.appUrin", line, nullptr), opts, line);
            clc::UrinOptions u;
            u.type = optStr(opts, "type");
            u.tags = listOf(optv(opts, "tags")); u.permissions = listOf(optv(opts, "permissions")); u.dependencies = listOf(optv(opts, "dependencies"));
            u.minRinVersion = optStr(opts, "min_rin_version"); u.created = optStr(opts, "created"); u.modified = optStr(opts, "modified");
            u.icon = optStr(opts, "icon");
            if (u.icon.empty()) { // أيقونة الحزمة: أكبر PNG ≤ 512KB (يُفحص الحجم إن عُرف dir) وإلا أول PNG
                std::string base = optStr(opts, "dir"); base = base.empty() ? "" : resolvePath(base, line);
                std::vector<const clc::IconEntry*> pngs;
                for (auto& ic : id.icons) if (ic.type == "image/png" && !clc::idn::isAbsUrl(ic.src)) pngs.push_back(&ic);
                std::stable_sort(pngs.begin(), pngs.end(), [](auto* x, auto* y) { return clc::idn::maxSize(x->sizes) > clc::idn::maxSize(y->sizes); });
                for (auto* ic : pngs) {
                    if (base.empty()) { u.icon = ic->src; break; }
                    std::error_code ec; auto sz = fs::file_size(fs::path(base) / ic->src, ec);
                    if (!ec && sz <= 512 * 1024) { u.icon = ic->src; break; }
                }
            }
            return strV(clc::buildUrin(id, u));
        });
    });
    reg("FromUrin", [this, guard](Args& a, int line) -> Value {
        need("container.appFromUrin", a, 1, 1, line);
        std::string raw = str(a[0], "container.appFromUrin", line), text = raw;
        if (raw.find('\n') == std::string::npos && raw.find('=') == std::string::npos) { // مسار ملف
            std::ifstream in(resolvePath(raw, line), std::ios::binary);
            if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "container.appFromUrin: تعذّر فتح '" + raw + "'");
            text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.size() > 64 * 1024) throw diagErr(diag::Code::E0007_InvalidArguments, line, "container.appFromUrin: الملف أكبر من 64KB");
        }
        return guard(line, "container.appFromUrin", [&] {
            std::map<std::string, std::string> kv;
            for (auto& line_ : clc::idn::split(text, '\n')) {
                std::string t = clc::idn::trim(line_);
                if (t.empty() || t[0] == '#' || clc::idn::startsWith(t, "//") || clc::idn::startsWith(t, "@container") || clc::idn::startsWith(t, ".end")) continue;
                size_t eq = t.find('='); if (eq == std::string::npos) continue;
                std::string k = clc::idn::trim(t.substr(0, eq)), v = clc::idn::trim(t.substr(eq + 1));
                if (!v.empty() && v.back() == ';') v.pop_back();
                v = clc::idn::trim(v);
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                    std::string o; v = v.substr(1, v.size() - 2);
                    for (size_t i = 0; i < v.size(); ++i) {
                        if (v[i] == '\\' && i + 1 < v.size()) { char n = v[++i]; o += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n; }
                        else o += v[i];
                    }
                    v = o;
                }
                kv[k] = v;
            }
            auto g = [&](const char* k) { auto it = kv.find(k); return it == kv.end() ? std::string() : it->second; };
            auto csv = [&](const char* k) { ArrayData arr; for (auto& x : splitList(g(k))) arr.push_back(strV(x)); return newArray(std::move(arr)); };
            std::string type = g("type");
            ArrayData icons; if (!g("icon").empty()) icons.push_back(strV(g("icon")));
            for (auto& x : splitList(g("icons"))) if (x != g("icon")) icons.push_back(strV(x));
            Value seo = mkMap({{"title", strV(g("seoTitle"))}, {"description", strV(g("seoDescription"))}, {"keywords", csv("seoKeywords")},
                               {"canonical", strV(g("canonical"))}, {"robots", strV(g("robots"))}, {"og_image", strV(g("ogImage"))}});
            double vc = 0; try { if (!g("versionCode").empty()) vc = std::stod(g("versionCode")); } catch (...) {}
            Value out = mkMap({{"kind", strV(clc::idn::startsWith(type, "html") ? "site" : "app")}, {"type", strV(type)}, {"id", strV(g("id"))},
                               {"name", strV(g("name"))}, {"short_name", strV(g("shortName"))}, {"version", strV(g("version"))},
                               {"version_code", numV(vc)}, {"channel", strV(g("channel"))}, {"author", strV(g("author"))},
                               {"license", strV(g("license"))}, {"homepage", strV(g("homepage"))}, {"lang", strV(g("lang"))},
                               {"category", strV(g("category"))}, {"description", strV(g("description"))},
                               {"short_description", strV(g("shortDescription"))}, {"theme_color", strV(g("themeColor"))},
                               {"background_color", strV(g("backgroundColor"))}, {"icons", newArray(std::move(icons))}, {"seo", seo},
                               {"tags", csv("tags")}, {"permissions", csv("permissions")}, {"dependencies", csv("dependencies")},
                               {"min_rin_version", strV(g("minRinVersion"))}, {"created", strV(g("created"))}, {"modified", strV(g("modified"))}});
            return out;
        });
    });

    // clcContainerIdentity(handle): نفس appOpen لكن لحاوية مفتوحة مسبقاً بـ clcContainerOpen (لواجهة ClcActivity).
    natives["clcContainerIdentity"] = [this](Args& a, int line) -> Value {
        need("clcContainerIdentity", a, 1, 1, line);
        int handle = int(num(a[0], "clcContainerIdentity", line));
        auto it = clcOpenContainers_.find(handle);
        if (it == clcOpenContainers_.end())
            throw diagErr(diag::Code::E0035_RuntimeError, line, "clcContainerIdentity: handle غير صالح أو مُغلَق بالفعل");
        clc::AppIdentity id = clc::readIdentity(it->second.metadata);
        Value v = identityToValue(id);
        v.map->put(strV("has_identity"), boolV(clc::hasIdentity(it->second.metadata)));
        return v;
    };
}

} // namespace rin
