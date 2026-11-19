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
//    container.appExport(src, dir, opts?) يكتب manifest.webmanifest + robots.txt + head.html (+ sitemap.xml)
//    container.appPack(src, dir, out, opts?)  يبني ملف .rcl من مجلد ويحفظ الهوية داخل Metadata
//    container.appOpen(path)              يقرأ الهوية من ملف .rcl
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
                  {"favicon", strV(a.favicon)}, {"icons", iconsToValue(a.icons)}, {"seo", seo}});
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
        return [base](const std::string& rel) { std::error_code ec; return fs::is_regular_file(fs::path(base) / rel, ec); };
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

    reg("Info", [this, sourceIdentity](Args& a, int line) -> Value {
        need("container.appInfo", a, 1, 1, line);
        std::string c;
        clc::AppIdentity id = sourceIdentity(a[0], "container.appInfo", line, &c);
        clc::completeIdentity(id);
        Value v = identityToValue(id);
        if (!c.empty()) v.map->put(strV("container"), strV(c));
        return v;
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

    reg("Head", [this, sourceIdentity](Args& a, int line) -> Value {
        need("container.appHead", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        clc::AppIdentity id = sourceIdentity(a[0], "container.appHead", line, nullptr);
        clc::HeadOptions ho;
        if (const Value* m = optv(opts, "manifest")) {
            if (m->type == Value::Type::STRING) ho.manifestHref = m->str;
            else if (!m->isTruthy()) ho.manifestHref.clear();
        }
        if (const Value* ind = optv(opts, "indent"); ind && ind->type == Value::Type::STRING) ho.indent = ind->str;
        ho.charsetViewport = optBool(opts, "charset", true);
        return strV(clc::buildHead(id, ho));
    });

    reg("Manifest", [this, sourceIdentity](Args& a, int line) -> Value {
        need("container.appManifest", a, 1, 1, line);
        return strV(clc::buildManifest(sourceIdentity(a[0], "container.appManifest", line, nullptr)));
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
        need("container.appBump", a, 2, 2, line);
        std::string who = "container.appBump";
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
            return strV(next);
        });
    });

    reg("Export", [this, sourceIdentity, baseDirOf, guard, writeText](Args& a, int line) -> Value {
        need("container.appExport", a, 2, 3, line);
        std::string who = "container.appExport";
        std::string dir = str(a[1], who, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        return guard(line, who, [&] {
            clc::AppIdentity id = sourceIdentity(a[0], who, line, nullptr);
            while (!dir.empty() && dir.back() == '/') dir.pop_back();
            std::string pre = dir.empty() ? "" : dir + "/";
            ArrayData written;
            auto put = [&](const std::string& rel, const std::string& text) { writeText(pre + rel, text, line, who); written.push_back(strV(pre + rel)); };
            clc::HeadOptions ho; ho.manifestHref = "manifest.webmanifest";
            put("head.html", clc::buildHead(id, ho));
            put("manifest.webmanifest", clc::buildManifest(id));
            clc::completeIdentity(id);
            std::string base = optStr(opts, "base");
            if (base.empty()) base = siteOrigin(id);
            std::vector<std::string> paths = listOf(optv(opts, "paths"));
            std::string smUrl = clc::idn::isAbsUrl(base) && !paths.empty() ? base + "/sitemap.xml" : "";
            put("robots.txt", clc::buildRobots(id, smUrl, listOf(optv(opts, "disallow"))));
            if (!smUrl.empty()) put("sitemap.xml", clc::buildSitemap(base, paths, optStr(opts, "lastmod")));
            return mkMap({{"dir", strV(dir)}, {"files", newArray(std::move(written))}});
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
