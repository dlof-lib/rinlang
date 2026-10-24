// ============================================================================
//  rin_disconom.h — مترجم Disconom (Disconom Translator) + ملف القفل (disconom.lock).
// ----------------------------------------------------------------------------
//  Disconom هو وصف مكتبة Rin (الاسم، الإصدار، التبعيات، الأجزاء، الكود الأصلي…). يعيش في ملف بإحدى
//  ثلاث صيغ متكافئة: disconom.json | disconom.xml | disconom.toml — يقرؤها المحرّك كلها.
//  هذا الملف يضيف «المترجم»: تحويل أي وصف إلى أي صيغة، مع الحفاظ على كل الحقول:
//
//     json  ⇄  xml  ⇄  toml                        (تحويل كامل بلا فقدان؛ الحقول المخصّصة تُحفظ في json/toml)
//     npm   (package.json)  ⇄  Disconom            (استيراد/تصدير؛ يُحفظ ما لا يعرفه npm تحت مفتاح "disconom")
//     rinpm (rin.toml لمدير RinPM)  ⇄  Disconom    (يُسجَّل في notes ما لا يستطيع rin.toml حمله)
//
//  كل مترجم يمرّ عبر الشكل الموحَّد libdir::Manifest، لذلك أي صيغة → أي صيغة بخطوتين (parse ثم render).
//  كل ما يُفقد أو يُعدَّل أثناء الترجمة يُسجَّل في Result::notes (لا شيء يضيع بصمت).
//
//  وفيه أيضاً ملف القفل disconom.lock (JSON حتمي بلا طوابع زمنية): الإصدارات المحلولة + بصمة sha256 لكل تبعية،
//  و compareLock لكشف أي انحراف (إصدار تغيّر/بصمة تغيّرت/تبعية أُضيفت أو أُزيلت).
//
//  الملف مستقلّ عن المفسّر (لا يُضمِّن rin_interpreter.h)، ويُستعمل من `rin lib translate|fmt|lock`.
// ============================================================================
#pragma once

#include "rin_libdir.h"

namespace rin {
namespace disconom {

using libdir::Dep;
using libdir::J;
using libdir::Manifest;

enum class Fmt { Json, Xml, Toml, RinPm, Npm };

inline const char* fmtName(Fmt f) {
    switch (f) { case Fmt::Json: return "json"; case Fmt::Xml: return "xml"; case Fmt::Toml: return "toml"; case Fmt::RinPm: return "rinpm"; case Fmt::Npm: return "npm"; }
    return "json";
}
inline const char* fmtFile(Fmt f) {   // اسم الملف الافتراضي لكل صيغة
    switch (f) { case Fmt::Json: return "disconom.json"; case Fmt::Xml: return "disconom.xml"; case Fmt::Toml: return "disconom.toml"; case Fmt::RinPm: return "rin.toml"; case Fmt::Npm: return "package.json"; }
    return "disconom.json";
}
inline bool parseFmtName(std::string n, Fmt& out) {
    n = libdir::detail::lower(n);
    if (n == "json" || n == "disconom.json") out = Fmt::Json;
    else if (n == "xml" || n == "disconom.xml") out = Fmt::Xml;
    else if (n == "toml" || n == "disconom.toml") out = Fmt::Toml;
    else if (n == "rinpm" || n == "rin.toml" || n == "rinpm.toml") out = Fmt::RinPm;
    else if (n == "npm" || n == "package.json" || n == "node") out = Fmt::Npm;
    else return false;
    return true;
}

struct Result {
    bool ok = false;
    Manifest m;
    std::string out;                       // النص المُولَّد (render/translate)
    std::vector<std::string> errors, warnings, notes;
};

namespace detail {
using libdir::detail::lower;
using libdir::detail::trimStr;

inline std::string xmlEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; default: o += c; }
    }
    return o;
}
inline J strArr(const std::vector<std::string>& v) { J a = J::array(); for (auto& s : v) a.push(J::string(s)); return a; }
inline J depObj(const std::vector<Dep>& v) { J o = J::object(); for (auto& d : v) o.set(d.name, J::string(d.range)); return o; }
inline std::string baseOf(const std::string& p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? p : p.substr(k + 1); }

// المفاتيح التي يعرفها Disconom (ما عداها «حقول مخصّصة» تُحفظ عند الترجمة بين json وtoml).
inline bool knownKey(const std::string& k) {
    static const std::set<std::string> kKnown = {
        "disconom", "format", "name", "version", "description", "details", "desc", "license", "homepage", "repository", "authors", "author",
        "keywords", "language", "rin", "rinVersion", "rin_version", "engine", "main", "entry", "parts", "exclude", "run", "dependencies",
        "devDependencies", "dev-dependencies", "dev_dependencies", "optionalDependencies", "optional-dependencies", "optional_dependencies",
        "peerDependencies", "peer-dependencies", "peer_dependencies", "scripts", "features", "files", "categories", "platforms", "workspaces",
        "contributors", "bugs", "readme", "funding", "private", "native"};
    return kKnown.count(k) > 0;
}
} // namespace detail

// ----------------------------------------------------------------------------
//  الشكل القانوني (canonical) كـ JSON: ترتيب ثابت، بلا حقول فارغة، مع حفظ الحقول المخصّصة.
// ----------------------------------------------------------------------------
inline J canonicalJson(const Manifest& m) {
    using namespace detail;
    J j = J::object();
    j.set("disconom", J::number((m.format < 2 && m.hasFormat2) ? 2 : m.format));
    j.set("name", J::string(m.name));
    j.set("version", J::string(m.version));
    if (!m.description.empty()) j.set("description", J::string(m.description));
    if (!m.authors.empty()) j.set("authors", strArr(m.authors));
    if (!m.contributors.empty()) j.set("contributors", strArr(m.contributors));
    if (!m.license.empty()) j.set("license", J::string(m.license));
    if (!m.keywords.empty()) j.set("keywords", strArr(m.keywords));
    if (!m.categories.empty()) j.set("categories", strArr(m.categories));
    if (!m.homepage.empty()) j.set("homepage", J::string(m.homepage));
    if (!m.repository.empty()) j.set("repository", J::string(m.repository));
    if (!m.bugs.empty()) j.set("bugs", J::string(m.bugs));
    if (!m.funding.empty()) j.set("funding", J::string(m.funding));
    if (!m.readme.empty()) j.set("readme", J::string(m.readme));
    if (libdir::detail::lower(m.language) != "rin") j.set("language", J::string(m.language));
    if (!m.rin.empty()) j.set("rin", J::string(m.rin));
    if (!m.platforms.empty()) j.set("platforms", strArr(m.platforms));
    if (m.isPrivate) j.set("private", J::boolean(true));
    if (!m.main.empty()) j.set("main", J::string(m.main));
    if (m.hasParts) j.set("parts", strArr(m.parts));
    if (!m.exclude.empty()) j.set("exclude", strArr(m.exclude));
    if (m.hasRun) j.set("run", strArr(m.run));
    if (!m.files.empty()) j.set("files", strArr(m.files));
    if (!m.workspaces.empty()) j.set("workspaces", strArr(m.workspaces));
    if (!m.dependencies.empty()) j.set("dependencies", depObj(m.dependencies));
    if (!m.optionalDependencies.empty()) j.set("optionalDependencies", depObj(m.optionalDependencies));
    if (!m.peerDependencies.empty()) j.set("peerDependencies", depObj(m.peerDependencies));
    if (!m.devDependencies.empty()) j.set("devDependencies", depObj(m.devDependencies));
    if (!m.scripts.empty()) { J o = J::object(); for (auto& kv : m.scripts) o.set(kv.first, J::string(kv.second)); j.set("scripts", o); }
    if (!m.features.empty() || !m.defaultFeatures.empty()) {
        J o = J::object();
        if (!m.defaultFeatures.empty()) o.set("default", strArr(m.defaultFeatures));
        for (auto& kv : m.features) o.set(kv.first, strArr(kv.second));
        j.set("features", o);
    }
    const auto& n = m.native;
    if (n.declared() || !n.functions.empty()) {
        J o = J::object();
        o.set("sources", strArr(n.sources));
        if (n.std != "c++17") o.set("std", J::string(n.std));
        if (n.required) o.set("required", J::boolean(true));
        if (!n.flags.empty()) o.set("flags", strArr(n.flags));
        if (!n.libs.empty()) o.set("libs", strArr(n.libs));
        if (!n.includes.empty()) o.set("includes", strArr(n.includes));
        if (!n.functions.empty()) {
            J fa = J::array();
            for (auto& f : n.functions) {
                if (!f.hasParams) fa.push(J::string(f.name));
                else { J fo = J::object(); fo.set("name", J::string(f.name)); fo.set("params", strArr(f.params)); fa.push(fo); }
            }
            o.set("functions", fa);
        }
        j.set("native", o);
    }
    if (m.hasRaw && m.raw.t == J::Obj)   // الحقول المخصّصة (x-…) تُحفظ كما هي
        for (auto& kv : m.raw.o) if (!detail::knownKey(kv.first)) j.set(kv.first, kv.second);
    return j;
}

// ----------------------------------------------------------------------------
//  XML
// ----------------------------------------------------------------------------
inline std::string renderXml(const Manifest& m) {
    using detail::xmlEsc;
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    int fmt = (m.format < 2 && m.hasFormat2) ? 2 : m.format;
    x += "<disconom format=\"" + std::to_string(fmt) + "\">\n";
    auto el = [&](const char* tag, const std::string& v) { if (!v.empty()) x += std::string("  <") + tag + ">" + xmlEsc(v) + "</" + tag + ">\n"; };
    auto list = [&](const char* plural, const char* singular, const std::vector<std::string>& v, bool force = false) {
        if (v.empty() && !force) return;
        if (v.empty()) { x += std::string("  <") + plural + "/>\n"; return; }
        x += std::string("  <") + plural + ">\n";
        for (auto& s : v) x += std::string("    <") + singular + ">" + xmlEsc(s) + "</" + singular + ">\n";
        x += std::string("  </") + plural + ">\n";
    };
    auto deps = [&](const char* tag, const std::vector<Dep>& v) {
        if (v.empty()) return;
        x += std::string("  <") + tag + ">\n";
        for (auto& d : v) x += "    <dependency name=\"" + xmlEsc(d.name) + "\" version=\"" + xmlEsc(d.range) + "\"/>\n";
        x += std::string("  </") + tag + ">\n";
    };
    el("name", m.name); el("version", m.version); el("description", m.description);
    list("authors", "author", m.authors); list("contributors", "contributor", m.contributors);
    el("license", m.license);
    list("keywords", "keyword", m.keywords); list("categories", "category", m.categories);
    el("homepage", m.homepage); el("repository", m.repository); el("bugs", m.bugs); el("funding", m.funding); el("readme", m.readme);
    if (libdir::detail::lower(m.language) != "rin") el("language", m.language);
    el("rin", m.rin);
    list("platforms", "platform", m.platforms);
    if (m.isPrivate) el("private", "true");
    el("main", m.main);
    if (m.hasParts) list("parts", "part", m.parts, true);
    list("exclude", "ignore", m.exclude);
    if (m.hasRun) {
        if (m.run.empty()) x += "  <run/>\n"; else list("run", "file", m.run);
    }
    list("files", "file", m.files); list("workspaces", "workspace", m.workspaces);
    deps("dependencies", m.dependencies); deps("optionalDependencies", m.optionalDependencies);
    deps("peerDependencies", m.peerDependencies); deps("devDependencies", m.devDependencies);
    if (!m.scripts.empty()) {
        x += "  <scripts>\n";
        for (auto& kv : m.scripts) x += "    <script name=\"" + xmlEsc(kv.first) + "\">" + xmlEsc(kv.second) + "</script>\n";
        x += "  </scripts>\n";
    }
    if (!m.features.empty() || !m.defaultFeatures.empty()) {
        x += "  <features>\n";
        auto feat = [&](const std::string& n, const std::vector<std::string>& items) {
            if (items.empty()) { x += "    <feature name=\"" + xmlEsc(n) + "\"/>\n"; return; }
            x += "    <feature name=\"" + xmlEsc(n) + "\">\n";
            for (auto& it : items) x += "      <item>" + xmlEsc(it) + "</item>\n";
            x += "    </feature>\n";
        };
        if (!m.defaultFeatures.empty()) feat("default", m.defaultFeatures);
        for (auto& f : m.features) feat(f.first, f.second);
        x += "  </features>\n";
    }
    const auto& n = m.native;
    if (n.declared() || !n.functions.empty()) {
        x += "  <native std=\"" + xmlEsc(n.std) + "\" required=\"" + (n.required ? "true" : "false") + "\">\n";
        for (auto& s : n.sources) x += "    <source>" + xmlEsc(s) + "</source>\n";
        for (auto& s : n.flags) x += "    <flag>" + xmlEsc(s) + "</flag>\n";
        for (auto& s : n.libs) x += "    <lib>" + xmlEsc(s) + "</lib>\n";
        for (auto& s : n.includes) x += "    <include>" + xmlEsc(s) + "</include>\n";
        for (auto& f : n.functions) {
            std::string ps; for (size_t i = 0; i < f.params.size(); ++i) ps += (i ? "," : "") + f.params[i];
            x += "    <function name=\"" + xmlEsc(f.name) + "\"" + (f.hasParams ? " params=\"" + xmlEsc(ps) + "\"" : "") + "/>\n";
        }
        x += "  </native>\n";
    }
    if (m.hasRaw && m.raw.t == J::Obj)   // حقول مخصّصة: JSON مضغوط داخل <extra name="…">
        for (auto& kv : m.raw.o) if (!detail::knownKey(kv.first)) x += "  <extra name=\"" + xmlEsc(kv.first) + "\">" + xmlEsc(libdir::dumpJson(kv.second, 0)) + "</extra>\n";
    x += "</disconom>\n";
    return x;
}

// ----------------------------------------------------------------------------
//  RinPM (rin.toml)
// ----------------------------------------------------------------------------
inline bool parseRinPm(const std::string& text, Manifest& m, std::vector<std::string>& errs, std::vector<std::string>& notes) {
    J j; std::string e;
    if (!libdir::parseToml(text, j, e)) { errs.push_back(e); return false; }
    const J* pkg = j.get("package");
    if (!pkg || pkg->t != J::Obj) { errs.push_back("rin.toml: missing [package] table"); return false; }
    auto S = [](const J* x) { return (x && x->t == J::Str) ? x->s : std::string(); };
    m.format = libdir::kDisconomFormat; m.formatName = "rinpm";
    m.name = S(pkg->get("name")); m.version = S(pkg->get("version")); m.description = S(pkg->get("description"));
    m.license = S(pkg->get("license")); m.repository = S(pkg->get("repository")); m.homepage = S(pkg->get("homepage"));
    auto L = [](const J* x) { std::vector<std::string> v; if (x && x->t == J::Arr) for (auto& i : x->a) if (i.t == J::Str) v.push_back(i.s); return v; };
    m.authors = L(pkg->get("authors")); m.keywords = L(pkg->get("keywords"));
    auto deps = [&](const J* t, std::vector<Dep>& out) {
        if (!t || t->t != J::Obj) return;
        for (auto& kv : t->o) {
            if (kv.second.t == J::Str) out.push_back({kv.first, kv.second.s});
            else if (kv.second.t == J::Obj) {
                std::string v = S(kv.second.get("version")); if (v.empty()) v = "*";
                out.push_back({kv.first, v});
                if (kv.second.get("path") || kv.second.get("git")) notes.push_back("rin.toml: dependency '" + kv.first + "' uses path/git — kept only its version range");
            }
        }
    };
    deps(j.get("dependencies"), m.dependencies);
    deps(j.get("dev-dependencies"), m.devDependencies);
    if (!m.devDependencies.empty()) m.hasFormat2 = true;
    if (const J* r = pkg->get("rin")) m.rin = S(r->get("version"));            // [package.rin]
    if (const J* pl = pkg->get("platforms")) m.platforms = L(pl->get("os"));   // [package.platforms]
    if (!m.platforms.empty()) m.hasFormat2 = true;
    return true;
}
inline std::string renderRinPm(const Manifest& m, std::vector<std::string>& notes) {
    J pkg = J::object();
    pkg.set("name", J::string(m.name)); pkg.set("version", J::string(m.version));
    if (!m.description.empty()) pkg.set("description", J::string(m.description));
    if (!m.authors.empty()) pkg.set("authors", detail::strArr(m.authors));
    if (!m.license.empty()) pkg.set("license", J::string(m.license));
    if (!m.repository.empty()) pkg.set("repository", J::string(m.repository));
    if (!m.homepage.empty()) pkg.set("homepage", J::string(m.homepage));
    if (!m.keywords.empty()) pkg.set("keywords", detail::strArr(m.keywords));
    J rin = J::object(); rin.set("version", J::string(m.rin.empty() ? ">=1.0.0" : m.rin)); pkg.set("rin", rin);
    std::vector<std::string> plats;
    static const std::set<std::string> kRinPmOs = {"linux", "android", "windows"};
    for (auto& p : m.platforms) { if (kRinPmOs.count(p)) plats.push_back(p); else notes.push_back("platform '" + p + "' is not supported by rin.toml (linux/android/windows) — dropped"); }
    if (plats.empty()) plats = {"linux", "android", "windows"};
    J pl = J::object(); pl.set("os", detail::strArr(plats)); pkg.set("platforms", pl);
    J root = J::object();
    root.set("package", pkg);
    root.set("dependencies", detail::depObj(m.dependencies));
    if (!m.devDependencies.empty()) root.set("dev-dependencies", detail::depObj(m.devDependencies));
    // ما لا يحمله rin.toml:
    auto lost = [&](bool has, const char* what) { if (has) notes.push_back(std::string("rin.toml cannot hold '") + what + "' — dropped"); };
    lost(!m.optionalDependencies.empty(), "optionalDependencies"); lost(!m.peerDependencies.empty(), "peerDependencies");
    lost(!m.scripts.empty(), "scripts"); lost(!m.features.empty() || !m.defaultFeatures.empty(), "features");
    lost(m.hasParts, "parts"); lost(m.hasRun, "run"); lost(!m.main.empty(), "main"); lost(!m.exclude.empty(), "exclude");
    lost(m.native.declared(), "native"); lost(!m.files.empty(), "files"); lost(!m.workspaces.empty(), "workspaces");
    lost(!m.bugs.empty(), "bugs"); lost(!m.categories.empty(), "categories"); lost(m.isPrivate, "private");
    // [package] يجب أن يأتي أولاً ثم [package.rin] و[package.platforms] ثم الاعتماديات — dumpToml يضع القيم البسيطة أولاً ثم الجداول بترتيبها
    std::string out;
    out += libdir::dumpToml(J::object().set("package", pkg));
    out += "\n" + libdir::dumpToml(J::object().set("dependencies", detail::depObj(m.dependencies)).set("dev-dependencies", detail::depObj(m.devDependencies)));
    if (m.dependencies.empty()) out += "\n[dependencies]\n";
    while (out.find("\n\n\n") != std::string::npos) out.erase(out.find("\n\n\n"), 1);
    return out;
}

// ----------------------------------------------------------------------------
//  npm (package.json)
// ----------------------------------------------------------------------------
namespace detail {
inline std::string sanitizeName(const std::string& n, std::vector<std::string>& notes) {
    std::string o;
    for (char c : n) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') o += c;
        else if (c == '/' || c == '@') { if (c == '/') o += '_'; }
        else o += '_';
    }
    if (!o.empty() && std::isdigit((unsigned char)o[0])) o = "_" + o;
    if (o != n) notes.push_back("package name '" + n + "' is not a valid Disconom name — renamed to '" + o + "'");
    return o;
}
inline std::string personName(const J& a) {   // "Name <mail> (url)" | {name,email,url}
    if (a.t == J::Obj) { auto* n = a.get("name"); return (n && n->t == J::Str) ? n->s : std::string(); }
    if (a.t != J::Str) return "";
    std::string s = a.s; size_t k = s.find_first_of("<(");
    if (k != std::string::npos) s = s.substr(0, k);
    return trimStr(s);
}
inline std::string cleanRange(const std::string& name, const std::string& r, std::vector<std::string>& notes) {
    std::string t = trimStr(r);
    if (t.empty() || t == "latest" || t == "next" || t == "*") { if (!t.empty() && t != "*") notes.push_back("dependency '" + name + "': tag '" + t + "' replaced with '*'"); return "*"; }
    if (t.rfind("workspace:", 0) == 0) { std::string rest = t.substr(10); notes.push_back("dependency '" + name + "': workspace protocol '" + t + "' converted to '" + (rest.empty() ? "*" : rest) + "'"); return (rest.empty() || rest == "^" || rest == "~") ? "*" : rest; }
    if (t.find(':') != std::string::npos || t.find('/') != std::string::npos) { notes.push_back("dependency '" + name + "': source '" + t + "' is not a version range — replaced with '*'"); return "*"; }
    if (!libdir::validRange(t)) { notes.push_back("dependency '" + name + "': unsupported range '" + t + "' replaced with '*'"); return "*"; }
    return t;
}
} // namespace detail

inline bool parseNpm(const std::string& text, Manifest& m, std::vector<std::string>& errs, std::vector<std::string>& notes) {
    using namespace detail;
    J j; std::string e;
    if (!libdir::parseJson(text, j, e)) { errs.push_back(e); return false; }
    if (j.t != J::Obj) { errs.push_back("package.json must be an object"); return false; }
    m.format = libdir::kDisconomFormat; m.formatName = "npm";
    auto S = [&](const char* k) { auto* x = j.get(k); return (x && x->t == J::Str) ? x->s : std::string(); };
    m.name = sanitizeName(S("name"), notes);
    m.version = S("version");
    if (m.version.empty()) { m.version = "0.0.0"; notes.push_back("no 'version' in package.json — using 0.0.0"); }
    m.description = S("description"); m.homepage = S("homepage");
    if (auto* l = j.get("license")) { if (l->t == J::Str) m.license = l->s; else if (l->t == J::Obj) m.license = libdir::detail::strOf(l->get("type")); }
    if (auto* a = j.get("author")) { std::string n = personName(*a); if (!n.empty()) m.authors.push_back(n); }
    if (auto* c = j.get("contributors")) if (c->t == J::Arr) for (auto& x : c->a) { std::string n = personName(x); if (!n.empty()) m.contributors.push_back(n); }
    if (auto* k = j.get("keywords")) m.keywords = libdir::detail::strList(k);
    if (auto* r = j.get("repository")) { if (r->t == J::Str) m.repository = r->s; else if (r->t == J::Obj) m.repository = libdir::detail::strOf(r->get("url")); }
    if (auto* b = j.get("bugs")) { if (b->t == J::Str) m.bugs = b->s; else if (b->t == J::Obj) m.bugs = libdir::detail::strOf(b->get("url")); }
    if (auto* f = j.get("funding")) {
        if (f->t == J::Str) m.funding = f->s;
        else if (f->t == J::Obj) m.funding = libdir::detail::strOf(f->get("url"));
        else if (f->t == J::Arr && !f->a.empty()) { auto& f0 = f->a[0]; m.funding = f0.t == J::Str ? f0.s : libdir::detail::strOf(f0.get("url")); if (f->a.size() > 1) notes.push_back("only the first 'funding' entry was kept"); }
    }
    auto deps = [&](const char* key, std::vector<Dep>& out) {
        auto* d = j.get(key); if (!d || d->t != J::Obj) return;
        for (auto& kv : d->o) out.push_back({kv.first, kv.second.t == J::Str ? cleanRange(kv.first, kv.second.s, notes) : std::string("*")});
    };
    deps("dependencies", m.dependencies); deps("devDependencies", m.devDependencies);
    deps("optionalDependencies", m.optionalDependencies); deps("peerDependencies", m.peerDependencies);
    if (auto* s = j.get("scripts")) if (s->t == J::Obj) for (auto& kv : s->o) if (kv.second.t == J::Str) m.scripts.emplace_back(kv.first, kv.second.s);
    if (auto* f = j.get("files")) m.files = libdir::detail::strList(f);
    if (auto* p = j.get("private")) m.isPrivate = (p->t == J::Bool && p->b);
    if (auto* w = j.get("workspaces")) { if (w->t == J::Arr) m.workspaces = libdir::detail::strList(w); else if (w->t == J::Obj) m.workspaces = libdir::detail::strList(w->get("packages")); }
    if (auto* os = j.get("os")) for (auto& p : libdir::detail::strList(os)) {
        std::string q = p == "darwin" ? "macos" : p == "win32" ? "windows" : p;
        m.platforms.push_back(q);
    }
    if (auto* en = j.get("engines")) if (en->t == J::Obj) {
        m.rin = libdir::detail::strOf(en->get("rin"));
        if (en->get("node")) notes.push_back("engines.node ignored (Disconom constrains the Rin version: engines.rin)");
    }
    if (auto* mn = j.get("main")) if (mn->t == J::Str) {
        if (libdir::detail::hasSuffix(mn->s, ".zpp")) m.main = mn->s; else notes.push_back("'main' (" + mn->s + ") is not a .zpp part — ignored");
    }
    // امتداد "disconom": ما لا يعرفه npm (يكتبه render عند التصدير فيُستعاد كاملاً عند الاستيراد)
    if (auto* d = j.get("disconom")) if (d->t == J::Obj) {
        Manifest tmp; std::vector<std::string> te;
        libdir::manifestFromJson(*d, tmp, te);
        if (!tmp.main.empty()) m.main = tmp.main;
        if (tmp.hasParts) { m.hasParts = true; m.parts = tmp.parts; }
        if (tmp.hasRun) { m.hasRun = true; m.run = tmp.run; }
        if (!tmp.exclude.empty()) m.exclude = tmp.exclude;
        if (!tmp.features.empty() || !tmp.defaultFeatures.empty()) { m.features = tmp.features; m.defaultFeatures = tmp.defaultFeatures; }
        if (tmp.native.declared() || !tmp.native.functions.empty()) m.native = tmp.native;
        if (!tmp.categories.empty()) m.categories = tmp.categories;
        if (!tmp.readme.empty()) m.readme = tmp.readme;
        if (tmp.authors.size() > 1) m.authors = tmp.authors;
        if (!tmp.platforms.empty()) m.platforms = tmp.platforms;
        for (auto& x : te) notes.push_back("package.json 'disconom' extension: " + x);
    }
    for (auto& kv : j.o) if (kv.first.rfind("x-", 0) == 0) {   // الحقول المخصّصة x-* تُحفظ
        if (m.raw.t != J::Obj) m.raw = J::object();
        m.raw.set(kv.first, kv.second); m.hasRaw = true;
    }
    m.hasFormat2 = true;
    return true;
}
inline std::string renderNpm(const Manifest& m, std::vector<std::string>& notes) {
    using namespace detail;
    J j = J::object();
    j.set("name", J::string(m.name)); j.set("version", J::string(m.version));
    if (!m.description.empty()) j.set("description", J::string(m.description));
    if (!m.license.empty()) j.set("license", J::string(m.license));
    if (!m.authors.empty()) j.set("author", J::string(m.authors[0]));   // npm يعرف مؤلفاً واحداً؛ القائمة الكاملة تحت "disconom"
    if (!m.contributors.empty()) j.set("contributors", strArr(m.contributors));
    if (!m.keywords.empty()) j.set("keywords", strArr(m.keywords));
    if (!m.homepage.empty()) j.set("homepage", J::string(m.homepage));
    if (!m.repository.empty()) { J r = J::object(); r.set("type", J::string("git")); r.set("url", J::string(m.repository)); j.set("repository", r); }
    if (!m.bugs.empty()) { J b = J::object(); b.set("url", J::string(m.bugs)); j.set("bugs", b); }
    if (!m.funding.empty()) j.set("funding", J::string(m.funding));
    if (m.isPrivate) j.set("private", J::boolean(true));
    if (!m.files.empty()) j.set("files", strArr(m.files));
    if (!m.workspaces.empty()) j.set("workspaces", strArr(m.workspaces));
    if (!m.dependencies.empty()) j.set("dependencies", depObj(m.dependencies));
    if (!m.devDependencies.empty()) j.set("devDependencies", depObj(m.devDependencies));
    if (!m.optionalDependencies.empty()) j.set("optionalDependencies", depObj(m.optionalDependencies));
    if (!m.peerDependencies.empty()) j.set("peerDependencies", depObj(m.peerDependencies));
    if (!m.scripts.empty()) { J o = J::object(); for (auto& kv : m.scripts) o.set(kv.first, J::string(kv.second)); j.set("scripts", o); }
    if (!m.rin.empty()) { J en = J::object(); en.set("rin", J::string(m.rin)); j.set("engines", en); }
    std::vector<std::string> os;
    for (auto& p : m.platforms) os.push_back(p == "macos" ? "darwin" : p == "windows" ? "win32" : p);
    if (!os.empty()) j.set("os", strArr(os));
    // امتداد "disconom": كل ما لا يعرفه npm، فيعود كاملاً عند الاستيراد
    Manifest ext;
    ext.format = m.format; ext.main = m.main; ext.hasParts = m.hasParts; ext.parts = m.parts; ext.hasRun = m.hasRun; ext.run = m.run;
    ext.exclude = m.exclude; ext.features = m.features; ext.defaultFeatures = m.defaultFeatures; ext.native = m.native;
    ext.categories = m.categories; ext.readme = m.readme;
    if (m.authors.size() > 1) ext.authors = m.authors;
    J full = canonicalJson(ext), extra = J::object();
    for (const char* k : {"authors", "main", "parts", "exclude", "run", "features", "native", "categories", "readme"}) if (auto* v = full.get(k)) extra.set(k, *v);
    if (!extra.o.empty()) { j.set("disconom", extra); notes.push_back("fields npm does not know (authors list/main/parts/run/exclude/features/native/categories/readme) were written under the \"disconom\" key"); }
    if (m.hasRaw && m.raw.t == J::Obj) for (auto& kv : m.raw.o) if (!knownKey(kv.first)) j.set(kv.first, kv.second);
    return libdir::dumpJson(j, 2) + "\n";
}

// ----------------------------------------------------------------------------
//  الواجهة العامة
// ----------------------------------------------------------------------------
// يخمّن الصيغة من اسم الملف ثم من المحتوى.
inline bool detect(const std::string& path, const std::string& text, Fmt& out) {
    using libdir::detail::lower;
    std::string b = lower(detail::baseOf(path));
    if (b == "package.json") { out = Fmt::Npm; return true; }
    if (b == "rin.toml") { out = Fmt::RinPm; return true; }
    if (b == "disconom.json") { out = Fmt::Json; return true; }
    if (b.size() > 5 && b.compare(b.size() - 5, 5, ".json") == 0) {   // اسم عام: خمّن npm من المحتوى
        bool npmish = text.find("\"disconom\"") == std::string::npos &&
                      (text.find("\"scripts\"") != std::string::npos || text.find("\"devDependencies\"") != std::string::npos || text.find("\"engines\"") != std::string::npos);
        out = npmish ? Fmt::Npm : Fmt::Json; return true;
    }
    if (b == "disconom.xml" || (b.size() > 4 && b.compare(b.size() - 4, 4, ".xml") == 0)) { out = Fmt::Xml; return true; }
    if (b == "disconom.toml" || (b.size() > 5 && b.compare(b.size() - 5, 5, ".toml") == 0)) {
        out = (text.find("[package]") != std::string::npos && text.find("disconom") == std::string::npos) ? Fmt::RinPm : Fmt::Toml; return true;
    }
    size_t k = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (k == std::string::npos) return false;
    if (text[k] == '<') { out = Fmt::Xml; return true; }
    if (text[k] == '{') { out = text.find("\"disconom\"") != std::string::npos ? Fmt::Json : (text.find("\"scripts\"") != std::string::npos || text.find("\"devDependencies\"") != std::string::npos ? Fmt::Npm : Fmt::Json); return true; }
    out = (text.find("[package]") != std::string::npos) ? Fmt::RinPm : Fmt::Toml; return true;
}

// نص بصيغة معيّنة → Manifest. validate=true يشغّل validateManifest (الأخطاء → errors، التحذيرات → warnings).
inline Result parse(Fmt f, const std::string& text, bool validate = false) {
    Result r;
    std::vector<std::string> errs;
    bool ok = false;
    switch (f) {
        case Fmt::Json: { J j; std::string e; if (!libdir::parseJson(text, j, e)) errs.push_back(e); else ok = libdir::manifestFromJson(j, r.m, errs); break; }
        case Fmt::Xml: {
            libdir::X x; std::string e;
            if (!libdir::parseXml(text, x, e)) errs.push_back(e);
            else {
                ok = libdir::manifestFromXml(x, r.m, errs);
                for (auto& k : x.kids) if (k.tag == "extra" && k.attr("name")) {   // حقل مخصّص
                    J v; std::string je;
                    if (!libdir::parseJson(k.text, v, je)) { r.warnings.push_back("<extra name=\"" + *k.attr("name") + "\"> is not valid JSON: " + je); continue; }
                    if (r.m.raw.t != J::Obj) r.m.raw = J::object();
                    r.m.raw.set(*k.attr("name"), v); r.m.hasRaw = true;
                }
            }
            break;
        }
        case Fmt::Toml: ok = libdir::manifestFromToml(text, r.m, errs); break;
        case Fmt::RinPm: ok = parseRinPm(text, r.m, errs, r.notes); break;
        case Fmt::Npm: ok = parseNpm(text, r.m, errs, r.notes); break;
    }
    r.errors = errs;
    if (ok && errs.empty() && validate) libdir::validateManifest(r.m, "", "", r.errors, r.warnings);
    r.ok = ok && r.errors.empty();
    return r;
}

// Manifest → نص بصيغة معيّنة. ما يُفقد/يُعدَّل يُسجَّل في notes.
inline std::string render(Fmt f, const Manifest& m, std::vector<std::string>& notes) {
    switch (f) {
        case Fmt::Json: return libdir::dumpJson(canonicalJson(m), 2) + "\n";
        case Fmt::Toml: return libdir::dumpToml(canonicalJson(m));
        case Fmt::Xml: return renderXml(m);
        case Fmt::RinPm: return renderRinPm(m, notes);
        case Fmt::Npm: return renderNpm(m, notes);
    }
    return "";
}

// الترجمة الكاملة: نص بصيغة from → نص بصيغة to. notes = كل ما فُقد أو عُدِّل (لا شيء يضيع بصمت).
// يتحقق من الناتج بإعادة قراءته (round-trip) ويضيف تحذيراً إن اختلف المانيفست الناتج عن المصدر.
inline Result translate(const std::string& text, Fmt from, Fmt to, bool validate = false) {
    Result r = parse(from, text, validate);
    if (!r.ok) return r;
    r.out = render(to, r.m, r.notes);
    Result back = parse(to, r.out);
    if (!back.ok) { r.ok = false; for (auto& e : back.errors) r.errors.push_back("internal: the generated " + std::string(fmtName(to)) + " does not parse back: " + e); return r; }
    auto sig = [](const Manifest& m) { return libdir::dumpJson(canonicalJson(m), 0); };
    if (to != Fmt::RinPm && to != Fmt::Npm && sig(back.m) != sig(r.m)) r.warnings.push_back("round-trip check: the converted manifest differs from the source (please report this)");
    return r;
}

// ----------------------------------------------------------------------------
//  ملف القفل disconom.lock
// ----------------------------------------------------------------------------
inline constexpr int kLockVersion = 1;

// يبني JSON القفل من رسم التبعيات. fingerprintOf(root) تُرجع "sha256:…" أو فارغاً. الجذر لا يُبصَم (يغيّر القفلُ بصمتَه).
inline J buildLock(const Manifest& root, const libdir::DepGraph& g, const std::string& engineVersion,
                   const std::function<std::string(const std::string&)>& fingerprintOf) {
    J j = J::object();
    j.set("lockfile", J::number(kLockVersion));
    j.set("generatedBy", J::string("rin lib lock"));
    if (!engineVersion.empty()) j.set("rin", J::string(engineVersion));
    J r = J::object(); r.set("name", J::string(root.name)); r.set("version", J::string(root.version)); j.set("root", r);
    J pk = J::array();
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        const auto& n = g.nodes[i];
        if (!n.found) continue;
        bool isRoot = (i + 1 == g.nodes.size());   // الجذر آخر العقد بترتيب التحميل
        if (isRoot) continue;
        J p = J::object();
        p.set("name", J::string(n.name));
        if (!n.version.empty()) p.set("version", J::string(n.version));
        p.set("kind", J::string(libdir::kindName(n.kind)));
        if (!n.range.empty()) p.set("range", J::string(n.range));
        std::string fp = fingerprintOf ? fingerprintOf(n.root) : std::string();
        if (!fp.empty()) p.set("fingerprint", J::string(fp));
        J ds = J::array(); for (auto& d : n.deps) ds.push(J::string(d)); p.set("dependencies", ds);
        pk.push(p);
    }
    // ترتيب حتمي (بالاسم) كي لا يتغيّر الملف دون سبب
    std::stable_sort(pk.a.begin(), pk.a.end(), [](const J& a, const J& b) { return libdir::detail::strOf(a.get("name")) < libdir::detail::strOf(b.get("name")); });
    j.set("packages", pk);
    return j;
}

// يقارن قفلاً محفوظاً بقفل مُولَّد الآن. يُرجع قائمة الفروق (فارغة = مطابق).
inline std::vector<std::string> compareLock(const J& saved, const J& fresh) {
    using libdir::detail::strOf;
    std::vector<std::string> diffs;
    auto index = [](const J& l) {
        std::map<std::string, const J*> m;
        if (auto* p = l.get("packages")) if (p->t == J::Arr) for (auto& e : p->a) m[strOf(e.get("name"))] = &e;
        return m;
    };
    auto a = index(saved), b = index(fresh);
    for (auto& kv : a) {
        auto it = b.find(kv.first);
        if (it == b.end()) { diffs.push_back("removed: " + kv.first + " " + strOf(kv.second->get("version")) + " is locked but no longer a dependency"); continue; }
        std::string va = strOf(kv.second->get("version")), vb = strOf(it->second->get("version"));
        if (va != vb) diffs.push_back("version: " + kv.first + " locked " + va + " but found " + vb);
        std::string fa = strOf(kv.second->get("fingerprint")), fb = strOf(it->second->get("fingerprint"));
        if (!fa.empty() && !fb.empty() && fa != fb) diffs.push_back("content: " + kv.first + " " + vb + " changed since it was locked (fingerprint mismatch)");
        std::string ra = strOf(kv.second->get("range")), rb = strOf(it->second->get("range"));
        if (ra != rb) diffs.push_back("range: " + kv.first + " locked with '" + ra + "' but the manifest now asks '" + rb + "'");
    }
    for (auto& kv : b) if (!a.count(kv.first)) diffs.push_back("added: " + kv.first + " " + strOf(kv.second->get("version")) + " is a dependency but not in the lock");
    return diffs;
}

} // namespace disconom
} // namespace rin
