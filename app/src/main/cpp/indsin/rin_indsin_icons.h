// indsin/rin_indsin_icons.h — Icon Registry (spec §18): a central, extensible name -> glyph table
// behind @view.Icon / @view.IconButton's `icon=` attribute.
//
// Honest scope note: this engine's Dye/rasterizer (rin_indsin_paint.h) has exactly three primitives
// -- FILL_RECT, STROKE_RECT, TEXT_RUN -- there is no path/line-drawing primitive yet (the same
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
