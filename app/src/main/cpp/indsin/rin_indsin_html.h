// indsin/rin_indsin_html.h — DrawList -> standalone HTML preview. Same Dye output the native
// rasterizer consumes, but text is laid out by the browser, so Arabic/Hebrew shaping, RTL,
// emoji and system fonts are correct (the native bitmap font is ASCII-only by design).
// Purely a serializer: it re-implements no layout or paint logic.
#pragma once
#include "rin_indsin_paint.h"
#include "rin_indsin_typography.h"

namespace indsin {

inline std::string htmlEscape(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) { if (c=='&') o+="&amp;"; else if (c=='<') o+="&lt;"; else if (c=='>') o+="&gt;"; else if (c=='"') o+="&quot;"; else o+=c; }
    return o;
}
inline std::string cssRgba(const Color& c) {
    std::ostringstream os; os << "rgba(" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (c.a/255.0) << ")"; return os.str();
}

inline std::string gradientCss(const DrawCommand& d) {
    std::string o = "linear-gradient(" + std::to_string((int)d.angleDeg) + "deg";
    for (auto& c : d.stops) o += "," + cssRgba(c);
    return o + ")";
}

// خيارات اختيارية للتصدير: لغة الصفحة، مرآة الوصول الدلالية (a11y::outlineHtml)، تقليل الحركة.
struct HtmlOptions {
    std::string lang;          // "" => "ar" (السلوك القديم)
    std::string dir;           // "" | rtl | ltr
    std::string a11yOutline;   // HTML دلالي مخفي بصرياً يقرؤه القارئ بدل مربعات الرسم
    bool reduceMotion = false; // يُضاف prefers-reduced-motion افتراضياً لأي CSS animation في الصفحة
};

// نص بوحدات (حرف/كلمة) لكل منها شفافية/إزاحة (TextFx): يُخرج span لكل وحدة.
inline std::string glyphSpansHtml(const DrawCommand& d) {
    auto cps = font::decodeUtf8(d.text);
    std::ostringstream os; size_t gi = 0;
    auto openUnit = [&](size_t idx) {
        const GlyphFx& g = d.glyphFx[std::min(idx, d.glyphFx.size() - 1)];
        os << "<span style=\"display:inline-block;opacity:" << g.alpha << ";transform:translate(" << g.dx << "px," << g.dy << "px)\">";
    };
    if (d.glyphUnit == 2) {
        std::string word; bool in = false;
        auto flush = [&]() { if (!word.empty()) { openUnit(gi++); os << htmlEscape(word) << "</span>"; word.clear(); } };
        for (uint32_t c : cps) { bool sp = c == ' ' || c == '\n'; if (sp) { flush(); os << (c == ' ' ? " " : "<br>"); in = false; } else { typo::appendUtf8(word, c); in = true; } }
        (void)in; flush();
    } else {
        for (uint32_t c : cps) {
            std::string ch; typo::appendUtf8(ch, c);
            if (c == ' ') { os << ' '; gi++; continue; }
            if (c == '\n') { os << "<br>"; gi++; continue; }
            openUnit(gi++); os << htmlEscape(ch) << "</span>";
        }
    }
    return os.str();
}

inline std::string drawListToHtml(const DrawList& list, int W, int H, const std::string& title = "indsin preview", Color canvas = Color{18, 18, 18}, const HtmlOptions* opt = nullptr) {
    std::ostringstream os;
    const std::string pageLang = (opt && !opt->lang.empty()) ? opt->lang : std::string("ar");
    os << "<!doctype html><html lang=\"" << htmlEscape(pageLang) << "\"" << ((opt && !opt->dir.empty()) ? " dir=\"" + opt->dir + "\"" : std::string()) << "><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       << "<title>" << htmlEscape(title) << "</title><style>"
       << "body{margin:0;min-height:100vh;background:#0d0e14;display:flex;justify-content:center;padding:24px;box-sizing:border-box;font-family:system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans Arabic','Noto Sans',sans-serif}"
       << ".dev{border:10px solid #23242e;border-radius:34px;box-shadow:0 20px 60px #000a;overflow:hidden;height:fit-content}"
       << ".scr{position:relative;background:" << cssRgba(canvas) << ";overflow:hidden}"
       << ".scr>*{position:absolute;box-sizing:border-box}"
       << ".t{display:flex;padding:0 2px;overflow:hidden;unicode-bidi:plaintext;text-align:start}"
       << ".t.c{justify-content:center;text-align:center}.t.e{text-align:end}"
       << ".t.s{align-items:center;white-space:nowrap;text-overflow:ellipsis}"
       << ".t.w{align-items:flex-start;line-height:1.4;word-break:break-word}"
       << ".t>span{display:block;min-width:0;overflow:hidden;text-overflow:ellipsis;unicode-bidi:plaintext}"
       << ".t.s>span{white-space:nowrap}.t.e>span{margin-inline-start:auto}"
       << ".sr-tree{position:absolute;width:1px;height:1px;margin:-1px;padding:0;overflow:hidden;clip:rect(0 0 0 0);clip-path:inset(50%);white-space:nowrap;border:0}";
    if (opt && opt->reduceMotion) os << "@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}";
    os << typo::fontFacesCss();   // CustomFont/FontLoad: @font-face لكل وجه مسجَّل (فارغ إن لم تُسجَّل خطوط)
    os << "</style>" << typo::fontPreloadLinks([](const std::string& u) { return htmlEscape(u); });
    os << "</head><body>";
    if (opt && !opt->a11yOutline.empty()) os << "<div class=\"sr-tree\" role=\"document\">\n" << opt->a11yOutline << "</div>\n";
    os << "<div class=\"dev\"" << ((opt && !opt->a11yOutline.empty()) ? " aria-hidden=\"true\"" : "") << "><div class=\"scr\" style=\"width:" << W << "px;height:" << H << "px\">\n";
    for (auto& d : list) {
        const Rect& b = d.bounds;
        std::ostringstream pos;
        pos << "left:" << b.x << "px;top:" << b.y << "px;width:" << b.w << "px;height:" << b.h << "px;";
        switch (d.op) {
            case DrawOp::FILL_RECT: case DrawOp::SCRIM_RECT:
                os << "<div data-o=\"" << d.owner << "\" style=\"" << pos.str() << "background:"
                   << (d.isGradient() ? gradientCss(d) : cssRgba(d.color))
                   << ";border-radius:" << std::min(d.radius, std::min(b.w, b.h)/2) << "px"
                   << (d.hasShadow() ? ";box-shadow:0 " + std::to_string(d.shadowDy) + "px " + std::to_string(d.shadowBlur) + "px " + std::to_string(d.shadowSpread) + "px " + cssRgba(d.shadowColor) : std::string())
                   << "\"></div>\n"; break;
            case DrawOp::STROKE_RECT:
                os << "<div data-o=\"" << d.owner << "\" style=\"" << pos.str() << "border:" << std::max(1.0, d.strokeWidth) << "px solid "
                   << cssRgba(d.color) << ";border-radius:" << std::min(d.radius, std::min(b.w, b.h)/2) << "px\"></div>\n"; break;
            case DrawOp::STROKE_ARC: {
                double sw = std::max(1.0, d.strokeWidth), R = std::min(b.w, b.h)/2 - sw/2, cx = b.w/2, cy = b.h/2;
                double a0 = d.startAngleDeg * 3.14159265358979/180.0, a1 = (d.startAngleDeg + d.sweepAngleDeg) * 3.14159265358979/180.0;
                os << "<svg style=\"" << pos.str() << "\" viewBox=\"0 0 " << b.w << " " << b.h << "\">";
                if (d.sweepAngleDeg >= 359.9)
                    os << "<circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << R << "\" fill=\"none\" stroke=\"" << cssRgba(d.color) << "\" stroke-width=\"" << sw << "\"/>";
                else
                    os << "<path d=\"M" << cx + R*std::cos(a0) << " " << cy + R*std::sin(a0) << " A" << R << " " << R << " 0 " << (d.sweepAngleDeg > 180 ? 1 : 0)
                       << " 1 " << cx + R*std::cos(a1) << " " << cy + R*std::sin(a1) << "\" fill=\"none\" stroke=\"" << cssRgba(d.color) << "\" stroke-width=\"" << sw << "\" stroke-linecap=\"round\"/>";
                os << "</svg>\n"; break;
            }
            case DrawOp::TEXT_RUN: {
                double fs = d.fontSize > 0 ? d.fontSize : 14;
                std::ostringstream extra;
                if (d.fontWeight) extra << ";font-weight:" << d.fontWeight; else if (d.bold) extra << ";font-weight:700";
                if (!d.fontFamily.empty()) extra << ";font-family:" << d.fontFamily;
                if (d.italic) extra << ";font-style:italic"; else if (d.oblique != 0) extra << ";font-style:oblique " << d.oblique << "deg";
                if (!d.fontCss.empty()) extra << ";" << d.fontCss;
                if (!d.textShadows.empty()) extra << ";text-shadow:" << typo::shadowsToCss(d.textShadows);
                std::string tf;
                if (d.rotateDeg != 0) { std::ostringstream t; t << "rotate(" << d.rotateDeg << "deg)"; tf = t.str(); }
                if (!tf.empty()) extra << ";transform:" << tf;
                if (d.clipL > 0 || d.clipT > 0 || d.clipR > 0 || d.clipB > 0)
                    extra << ";clip-path:inset(" << d.clipT * 100 << "% " << d.clipR * 100 << "% " << d.clipB * 100 << "% " << d.clipL * 100 << "%)";
                std::string attrs;
                if (!d.lang.empty()) attrs += " lang=\"" + htmlEscape(d.lang) + "\"";
                std::string inner = d.glyphFx.empty() ? htmlEscape(d.text) : glyphSpansHtml(d);
                os << "<div class=\"t " << (d.align == 1 ? "c " : d.align == 2 ? "e " : "") << (d.singleLine ? "s" : "w") << "\" data-o=\"" << d.owner << "\"" << attrs
                   << " style=\"" << pos.str() << "font-size:" << fs << "px;color:" << cssRgba(d.color) << extra.str()
                   << "\"><span dir=\"" << (d.dir.empty() ? "auto" : d.dir) << "\">" << inner << "</span></div>\n"; break;
            }
        }
    }
    os << "</div></div></body></html>\n";
    return os.str();
}

} // namespace indsin
