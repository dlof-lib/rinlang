// indsin/rin_indsin_html.h — DrawList -> standalone HTML preview. Same Dye output the native
// rasterizer consumes, but text is laid out by the browser, so Arabic/Hebrew shaping, RTL,
// emoji and system fonts are correct (the native bitmap font is ASCII-only by design).
// Purely a serializer: it re-implements no layout or paint logic.
#pragma once
#include "rin_indsin_paint.h"

namespace indsin {

inline std::string htmlEscape(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) { if (c=='&') o+="&amp;"; else if (c=='<') o+="&lt;"; else if (c=='>') o+="&gt;"; else if (c=='"') o+="&quot;"; else o+=c; }
    return o;
}
inline std::string cssRgba(const Color& c) {
    std::ostringstream os; os << "rgba(" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (c.a/255.0) << ")"; return os.str();
}

inline std::string drawListToHtml(const DrawList& list, int W, int H, const std::string& title = "indsin preview") {
    std::ostringstream os;
    os << "<!doctype html><html lang=\"ar\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       << "<title>" << htmlEscape(title) << "</title><style>"
       << "body{margin:0;min-height:100vh;background:#0d0e14;display:flex;justify-content:center;padding:24px;box-sizing:border-box;font-family:system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans Arabic','Noto Sans',sans-serif}"
       << ".dev{border:10px solid #23242e;border-radius:34px;box-shadow:0 20px 60px #000a;overflow:hidden;height:fit-content}"
       << ".scr{position:relative;background:#121212;overflow:hidden}"
       << ".scr>*{position:absolute;box-sizing:border-box}"
       << ".t{display:flex;padding:0 2px;overflow:hidden;unicode-bidi:plaintext;text-align:start}"
       << ".t.c{justify-content:center;text-align:center}.t.e{text-align:end}"
       << ".t.s{align-items:center;white-space:nowrap;text-overflow:ellipsis}"
       << ".t.w{align-items:flex-start;line-height:1.4;word-break:break-word}"
       << ".t>span{display:block;min-width:0;overflow:hidden;text-overflow:ellipsis;unicode-bidi:plaintext}"
       << ".t.s>span{white-space:nowrap}.t.e>span{margin-inline-start:auto}"
       << "</style></head><body><div class=\"dev\"><div class=\"scr\" style=\"width:" << W << "px;height:" << H << "px\">\n";
    for (auto& d : list) {
        const Rect& b = d.bounds;
        std::ostringstream pos;
        pos << "left:" << b.x << "px;top:" << b.y << "px;width:" << b.w << "px;height:" << b.h << "px;";
        switch (d.op) {
            case DrawOp::FILL_RECT: case DrawOp::SCRIM_RECT:
                os << "<div data-o=\"" << d.owner << "\" style=\"" << pos.str() << "background:" << cssRgba(d.color)
                   << ";border-radius:" << std::min(d.radius, std::min(b.w, b.h)/2) << "px\"></div>\n"; break;
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
                os << "<div class=\"t " << (d.align == 1 ? "c " : d.align == 2 ? "e " : "") << (d.singleLine ? "s" : "w") << "\" data-o=\"" << d.owner
                   << "\" style=\"" << pos.str() << "font-size:" << fs << "px;color:" << cssRgba(d.color) << (d.bold ? ";font-weight:700" : "")
                   << "\"><span dir=\"auto\">" << htmlEscape(d.text) << "</span></div>\n"; break;
            }
        }
    }
    os << "</div></div></body></html>\n";
    return os.str();
}

} // namespace indsin
