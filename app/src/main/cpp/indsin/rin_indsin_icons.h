// indsin/rin_indsin_icons.h — Icon Registry (spec §18): a central, extensible name -> glyph table
// behind @view.Icon / @view.IconButton's `icon=` attribute.
//
// Honest scope note: this engine's Dye/rasterizer (rin_indsin_paint.h) has exactly three primitives
// -- FILL_RECT, STROKE_RECT, TEXT_RUN (+ STROKE_PATH, added later for vector icons) -- historically there was no path primitive (the same
// limitation already documented for GRADIENT and Dialog/Tooltip positioning). A real vector glyph
// set (arrow-head polylines, etc.) would need a fourth DrawOp the rasterizer can fill, which is a
// bigger change than an icon *registry* by itself. Given that constraint, each icon here resolves
// to a single, deliberately plain typographic symbol (→, ✕, ⚙, …) rendered through the existing
// TEXT_RUN path -- NOT colored pictographic Emoji (spec explicitly rules those out as "the
// solution"). What makes this a real Icon System rather than "hardcoded emoji strings" per spec
// §18 is what's built around that glyph: a name -> glyph registry with unknown-name detection, a
// registerIcon() extension API so new icons don't require touching this table's callers, and a
// StrandKind-level Icon/IconButton integration (below / rin_indsin_strand.h, rin_indsin_layout.h,
// rin_indsin_paint.h) -- not the glyph rendering technique itself, which is bounded by the
// rasterizer this engine actually has.
#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include "rin_indsin_iconpaths.h"   // مسارات SVG المتجهية (مُولَّدة من tools/icons/icons_def.py)

namespace indsin {

struct IconRegistry {
    std::unordered_map<std::string, std::string> glyphs;

    IconRegistry() { registerBuiltins(); }

    // registerIcon(): the extension point spec §18 asks for ("يجب أن تكون الأيقونات قابلة
    // للتوسع") -- a caller (app code, a future Bolt plugin) can add or override any name without
    // editing this file. Re-registering an existing name overwrites it (last write wins), matching
    // how ThemeRegistry::registerTheme already behaves for the same reason.
    void registerIcon(const std::string& name, const std::string& glyph) { glyphs[name] = glyph; }

    // resolve(): never crashes and never returns something crash-prone. An unknown name resolves
    // to a plain "?" glyph -- a visibly-a-placeholder icon, not a silent blank -- and `found` is
    // set to false so a caller (Indsin's semantic/attribute validation, §28) can surface a real
    // [Exxxx] diagnostic ("unknown icon 'bogus-name'") instead of just failing quietly.
    std::string resolve(const std::string& name, bool* found = nullptr) const {
        auto it = glyphs.find(name);
        if (it == glyphs.end()) {
            if (found) *found = false;
            return "?";
        }
        if (found) *found = true;
        return it->second;
    }
    bool has(const std::string& name) const { return glyphs.count(name) != 0; }

    // ---- الأيقونات المتجهية (SVG) ------------------------------------------------------------
    // جدول مسارات حقيقي (شبكة 24x24، خط سماكته 2) لـ 187 أيقونة + أسماء بديلة. هذه واجهة بيانات:
    // معاينة الويب (indsin_object.html) ومكتبة lib/indsinicons.og.rin تستهلكانها فعلاً الآن؛ أما
    // الـrasterizer الأصلي فما زال بلا عملية رسم مسارات (FILL_RECT/STROKE_RECT/TEXT_RUN فقط)، فيبقى
    // الرسم الأصلي على الرمز النصي (glyphs) إلى أن تُضاف عملية DrawOp للمسارات. vectorPath() هو ما
    // ستقرؤه تلك العملية. لا يغيّر شيئاً من سلوك resolve()/has() القائم.
    std::unordered_set<std::string> imported;      // أسماء الأيقونات المستوردة وقت التشغيل (تُرسم متجهة افتراضياً)
    bool vectorDefault = false;                    // true ⇒ كل <Icon> له متجه يُرسم متجهاً (الافتراضي: الرمز النصي كما كان)
    bool isImported(const std::string& n) const { return imported.count(n) != 0; }
    std::unordered_map<std::string, std::string> vectors;   // اسم -> d (يشمل ما سُجِّل وقت التشغيل)
    bool vectorsLoaded = false;

    void loadVectors() {
        if (vectorsLoaded) return;
        for (size_t i = 0; i < kIconVecCount; ++i) vectors[kIconVecs[i].name] = kIconVecs[i].path;
        vectorsLoaded = true;
    }
    // canonicalName(): يحوّل الاسم البديل ("gear") إلى الأساسي ("settings")؛ يعيد "" إن لم يوجد.
    std::string canonicalName(const std::string& name) {
        loadVectors();
        if (vectors.count(name)) return name;
        for (size_t i = 0; i < kIconAliasCount; ++i)
            if (name == kIconAliases[i].alias && vectors.count(kIconAliases[i].target)) return kIconAliases[i].target;
        return "";
    }
    bool hasVector(const std::string& name) { return !canonicalName(name).empty(); }
    // vectorPath(): قيمة d، أو "" إن لم توجد أيقونة بهذا الاسم.
    std::string vectorPath(const std::string& name) {
        std::string c = canonicalName(name);
        return c.empty() ? std::string() : vectors[c];
    }
    // registerVector(): توسعة وقت التشغيل (آخر كتابة تفوز) — مقابل registerIcon للرمز النصي.
    void registerVector(const std::string& name, const std::string& path) { loadVectors(); vectors[name] = path; }
    // svg(): وسم <svg> كامل لأيقونة متجهية (currentColor افتراضياً)؛ "" إن لم توجد.
    std::string svg(const std::string& name, int size = 24, const std::string& color = "currentColor", double stroke = 2.0) {
        std::string d = vectorPath(name);
        if (d.empty()) return std::string();
        std::string st = std::to_string(stroke);
        st.erase(st.find_last_not_of('0') + 1); if (!st.empty() && st.back() == '.') st.pop_back();
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"" + std::to_string(size) + "\" height=\"" +
               std::to_string(size) + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"" + st +
               "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"" + d + "\"/></svg>";
    }


    // ---- استيراد أيقونة من مصدر خارجي (ردّ api) ---------------------------------------------------
    //  importSource(name, src, glyph): src = نص SVG كامل، أو مسار d خام، أو data URI (base64 / نسبي-مرمَّز) لـ svg.
    //  يستخرج كل `d` + يحوّل circle/rect/line/polyline/polygon إلى مسارات، يتحقق من الصيغة، ثم يسجّل
    //  المتجه (registerVector) والرمز النصي البديل (glyph) الذي يرسمه الـrasterizer الحالي. يعيد "" عند الفشل.
    static bool validPath(const std::string& d) {
        if (d.empty() || d.size() > 20000) return false;
        char c0 = d[0]; if (c0 != 'M' && c0 != 'm') return false;
        for (char c : d) if (!(std::isalnum((unsigned char)c) || c == ' ' || c == ',' || c == '.' || c == '-' || c == '+' || c == '\n' || c == '\t' || c == '\r')) return false;
        for (char c : d) if (std::isalpha((unsigned char)c) && !std::strchr("MmLlHhVvCcSsQqTtAaZzEe", c)) return false;
        return true;
    }
    static std::string attrOf(const std::string& tag, const std::string& attr) {
        size_t p = 0;
        while ((p = tag.find(attr, p)) != std::string::npos) {
            bool lead = p > 0 && std::isspace((unsigned char)tag[p - 1]);
            size_t q = p + attr.size(); while (q < tag.size() && std::isspace((unsigned char)tag[q])) ++q;
            if (lead && q < tag.size() && tag[q] == '=') {
                ++q; while (q < tag.size() && std::isspace((unsigned char)tag[q])) ++q;
                if (q < tag.size() && (tag[q] == '"' || tag[q] == '\'')) { char qc = tag[q]; size_t e = tag.find(qc, q + 1); if (e != std::string::npos) return tag.substr(q + 1, e - q - 1); }
            }
            p += attr.size();
        }
        return "";
    }
    static std::string num(double v) { char b[32]; snprintf(b, sizeof b, "%.4g", v); return b; }
    static std::string extractPath(const std::string& svg) {
        std::string out; size_t pos = 0;
        while ((pos = svg.find('<', pos)) != std::string::npos) {
            size_t end = svg.find('>', pos); if (end == std::string::npos) break;
            std::string tag = svg.substr(pos + 1, end - pos - 1); pos = end + 1;
            size_t ne = 0; while (ne < tag.size() && (std::isalnum((unsigned char)tag[ne]))) ++ne;
            std::string nm = tag.substr(0, ne), seg;
            auto f = [&](const char* a, double def = 0) { std::string v = attrOf(tag, a); return v.empty() ? def : atof(v.c_str()); };
            if (nm == "path") seg = attrOf(tag, "d");
            else if (nm == "circle") { double cx = f("cx"), cy = f("cy"), r = f("r"); if (r > 0) seg = "M" + num(cx - r) + " " + num(cy) + "A" + num(r) + " " + num(r) + " 0 1 0 " + num(cx + r) + " " + num(cy) + "A" + num(r) + " " + num(r) + " 0 1 0 " + num(cx - r) + " " + num(cy) + "Z"; }
            else if (nm == "line") seg = "M" + num(f("x1")) + " " + num(f("y1")) + "L" + num(f("x2")) + " " + num(f("y2"));
            else if (nm == "rect") { double x = f("x"), y = f("y"), w = f("width"), h = f("height"); if (w > 0 && h > 0) seg = "M" + num(x) + " " + num(y) + "H" + num(x + w) + "V" + num(y + h) + "H" + num(x) + "Z"; }
            else if (nm == "polyline" || nm == "polygon") {
                std::string pts = attrOf(tag, "points"); std::string t; for (char c : pts) t += (c == ',' ? ' ' : c);
                std::vector<double> v; const char* s = t.c_str(); char* e = nullptr;
                for (;;) { double x = strtod(s, &e); if (e == s) break; v.push_back(x); s = e; }
                for (size_t i = 0; i + 1 < v.size(); i += 2) seg += std::string(i ? "L" : "M") + num(v[i]) + " " + num(v[i + 1]);
                if (!seg.empty() && nm == "polygon") seg += "Z";
            }
            if (seg.empty()) continue;
            if (!out.empty()) out += " ";
            out += seg;
        }
        return out;
    }
    static std::string decodeB64(const std::string& in) {
        std::string out; int buf = 0, bits = 0;
        for (char c : in) {
            int v = (c >= 'A' && c <= 'Z') ? c - 'A' : (c >= 'a' && c <= 'z') ? c - 'a' + 26 : (c >= '0' && c <= '9') ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
            if (v < 0) continue;
            buf = (buf << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
        }
        return out;
    }
    static std::string percentDecode(const std::string& in) {
        std::string o;
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit((unsigned char)in[i + 1]) && std::isxdigit((unsigned char)in[i + 2])) { o += (char)strtol(in.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else o += in[i];
        }
        return o;
    }
    // sourcePath(): يحوّل أي مصدر مدعوم إلى مسار d صالح؛ "" إن لم يُفهم.
    static std::string sourcePath(const std::string& src) {
        std::string s = src;
        size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
        s = s.substr(a);
        if (s.rfind("data:image/svg+xml", 0) == 0) {
            size_t comma = s.find(','); if (comma == std::string::npos) return "";
            std::string head = s.substr(0, comma), body = s.substr(comma + 1);
            s = head.find(";base64") != std::string::npos ? decodeB64(body) : percentDecode(body);
        }
        std::string d = (s[0] == '<') ? extractPath(s) : s;
        return validPath(d) ? d : "";
    }
    // symbols(): sprite SVG (<symbol id="x">…</symbol> أو <g id="x">) ⇒ (اسم، مسار) لكل رمز له مسار صالح.
    static std::vector<std::pair<std::string, std::string>> symbols(const std::string& svg) {
        std::vector<std::pair<std::string, std::string>> out; size_t pos = 0;
        while ((pos = svg.find("<symbol", pos)) != std::string::npos) {
            size_t te = svg.find('>', pos); if (te == std::string::npos) break;
            std::string tag = svg.substr(pos + 1, te - pos - 1);
            size_t ce = svg.find("</symbol>", te); if (ce == std::string::npos) ce = svg.size();
            std::string id = attrOf(tag, "id"), d = extractPath(svg.substr(te + 1, ce - te - 1));
            if (!id.empty() && validPath(d)) out.emplace_back(id, d);
            pos = ce;
        }
        return out;
    }
    bool importPath(const std::string& name, const std::string& d, const std::string& glyph = "") {
        if (name.empty() || !validPath(d)) return false;
        registerVector(name, d); imported.insert(name);
        if (!glyph.empty()) glyphs[name] = glyph; else if (!glyphs.count(name)) glyphs[name] = "◆";   // رمز بديل واضح حتى يُضاف رسم المسارات في الـrasterizer
        return true;
    }
    bool importSource(const std::string& name, const std::string& src, const std::string& glyph = "") {
        std::string d = sourcePath(src);
        return !d.empty() && importPath(name, d, glyph);
    }

    void registerBuiltins() {
        // Exactly the spec §18 list.
        glyphs["home"] = "\u2302";        // house/hut symbol
        glyphs["menu"] = "\u2261";        // hamburger (identical/triple-bar)
        glyphs["close"] = "\u2715";       // multiplication X
        glyphs["search"] = "\u26B2";      // magnifier-adjacent glyph (plain typographic, no color)
        glyphs["settings"] = "\u2699";    // gear
        glyphs["user"] = "\u263A";        // person/face placeholder (plain glyph, not an emoji face)
        glyphs["edit"] = "\u270E";        // pencil
        glyphs["delete"] = "\u2716";      // heavy X
        glyphs["download"] = "\u2913";    // arrow to bar (download-style)
        glyphs["upload"] = "\u2911";      // arrow from bar (upload-style)
        glyphs["play"] = "\u25B6";        // play triangle
        glyphs["pause"] = "\u23F8";       // pause bars
        glyphs["stop"] = "\u25A0";        // stop square
        glyphs["check"] = "\u2713";       // checkmark
        glyphs["plus"] = "+";
        glyphs["minus"] = "-";
        glyphs["arrow-left"] = "\u2190";
        glyphs["arrow-right"] = "\u2192";
        glyphs["arrow-up"] = "\u2191";
        glyphs["arrow-down"] = "\u2193";
        glyphs["eye"] = "\u25C9";
        glyphs["eye-off"] = "\u25C8";
        glyphs["lock"] = "\u2BBF";
        glyphs["unlock"] = "\u2BC0";
        glyphs["refresh"] = "\u21BB";
        glyphs["share"] = "\u2197";
        glyphs["more"] = "\u2026";        // ellipsis ("more" overflow indicator)
    }
};

inline IconRegistry& iconRegistry() { static IconRegistry reg; return reg; }

} // namespace indsin
