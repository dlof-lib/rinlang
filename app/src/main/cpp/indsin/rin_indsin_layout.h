// indsin/rin_indsin_layout.h — The Indsin: two-pass Propose/Settle layout engine.
// (Same verified algorithm as the standalone prototype, operating on the real Strand type.)
//
// ---------------------------------------------------------------------------------------------
// EXTENSION NOTE (added to satisfy: top bar / bottom bar / side drawer / dropdown menus / video
// & audio / web-embeds (e.g. YouTube) / printable tables / header / align+valign / splash-screen
// / page navigation main.rin -> mu.rin on tap or timer):
//
// This header only owns *geometry* (measuring/placing rectangles). It cannot by itself add new
// widgets end-to-end — that also requires (not included in this patch, since those files weren't
// provided):
//   1. rin_indsin_strand.h  — StrandKind enum must gain: HEADER, TOPBAR, BOTTOMBAR, DRAWER, MENU,
//      MENUITEM, TABLE, TABLEROW, VIDEO, AUDIO, WEBVIEW, SCAFFOLD, SPLASH.
//   2. The .rin parser     — must recognize the new tag names and the new attributes used below
//      (align, valign, role, open, src, columns, duration, navigate, onTap="navigate:...").
//   3. indsin::fabricToJsonString (native) + RinEngine/JNI bridge — must serialize the new kinds'
//      attrs the same way existing ones are (no change needed if it's already generic/attr-map
//      based; only add the new enum-to-string names in one place).
//   4. The host app (Activity/Fragment that owns IndsinPreviewManager) — must actually perform the
//      navigation (loading a new .rin file, e.g. "mu.rin") when it sees an onTap/timer navigate
//      instruction, and must drive a real <video>/<audio>/<WebView> player + YouTube embed and a
//      real print/export routine for <Table>. The Indsin only computes where those boxes sit and
//      IndsinFabricView only draws a *placeholder* box for them in the design-time preview — exactly
//      how Image already works today (see drawImagePlaceholder in IndsinFabricView.kt).
//
// New attributes this file adds support for:
//   align   = "left" | "center" | "right" | "stretch"   (Column's cross-axis, Row/Table/Stack too)
//   valign  = "top"  | "center" | "bottom" | "stretch"   (Row's cross-axis, Stack too)
//   role    = "topbar" | "bottombar" | "drawer" | "content"  (used only by SCAFFOLD's children)
//   open    = "true"/"false"   (Drawer/Menu visibility — collapses to 0 size when closed)
//   columns = "Name,Age,City" (Table header labels, comma separated)
//   ratio   = "16:9" etc.      (Video/WebView aspect ratio when no explicit height=)
//   direction = "ltr" | "rtl"  (Row and anything else built on the axis=X flow path — TopBar/
//                               BottomBar/Tabs/ListItem/TableRow — mirrors main-axis child order)
// ---------------------------------------------------------------------------------------------
#pragma once
#include "rin_indsin_strand.h"
#include "rin_indsin_tokens.h"
#include "rin_indsin_webcore.h" // مفاهيم تخطيط الويب (position/order/gap/media/...)
#include <algorithm>
#include <sstream>

namespace indsin {

enum class Axis { X, Y };

// Web Layout Concepts (rin_indsin_webconcepts.h, يُضمَّن في آخر هذا الملف): المرور اللاحق للتخطيط
// (absolute/fixed/sticky/collision/bounds) وقياس min-content. معرَّفتان هنا لأن Indsin::layout() تستدعيهما.
struct Indsin;
inline void webPostLayout(Indsin& ix, const StrandPtr& root, double scrollX = 0, double scrollY = 0);
inline double webMinContentWidth(const Strand& s);

// left/top => 0, center => centered, right/bottom => pushed to far edge, stretch => fills (0 offset,
// caller is expected to have already sized the child to `available` in that case).
inline double alignOffset(double available, double size, const std::string& mode) {
    if (mode == "center") return std::max(0.0, (available - size) / 2.0);
    if (mode == "right" || mode == "bottom" || mode == "end") return std::max(0.0, available - size);
    return 0.0; // left / top / start / stretch
}

// Overlay Engine hook (see rin_indsin_overlay.h for the full second pass): whether `s` is going to
// be re-homed onto a separate overlay layer instead of staying in normal document flow. Consulted
// by layoutLinear() and layoutStack() below, which are the actual places flow-exclusion happens
// (skipping a Dialog/Tooltip's contribution to their main-axis budget / cross-axis footprint,
// while still measuring its own content box against the full available space) — NOT inside
// layout()'s own generic tail. An earlier version of this hook tried exactly that (making
// layout() itself report {0,0,0,0} for an overlay Strand while still recording its real box) and
// it silently corrupted layoutLinear's two-pass flex budget: that pass calls layout() on the same
// child twice (an unconstrained probe, then a real placement call built from the probe's
// *returned* main-axis size), so a returned 0 fed back in as a maxH=0 constraint on the second,
// "real" call -- the Dialog's own box came out zeroed instead of merely excluded from its
// parent's size. Keeping layout() itself honest (always returns the Strand's real measured size)
// and doing the skip explicitly, once, in each container helper's own bookkeeping avoids that.
inline bool isOverlayStrand(const Strand& s) {
    if (s.kind == StrandKind::DIALOG)  return s.attrStr("open", "false") == "true";
    if (s.kind == StrandKind::TOOLTIP) return !s.attrStr("anchor", "").empty() && s.attrStr("open", "true") == "true";
    return false;
}
// خارج التدفق العادي لحاوية خطية/مكدّسة: نافذة/تلميح مفتوح (Overlay Engine)، أو position=absolute|fixed،
// أو عنصر مطوي (display=none / hidden / media غير مطابق). لا يحجز مساحة ولا فجوة.
inline bool isOutOfFlowStrand(const Strand& s) { return isOverlayStrand(s) || webOutOfFlow(s) || webCollapsed(s); }

inline std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t a = item.find_first_not_of(' ');
        size_t b = item.find_last_not_of(' ');
        out.push_back(a == std::string::npos ? "" : item.substr(a, b - a + 1));
    }
    return out;
}

// Counts Unicode codepoints in a UTF-8 string (not bytes) — text.size() was counting bytes,
// which silently under-measured any multi-byte text (e.g. Arabic labels/attrs).
inline size_t utf8Length(const std::string& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
        i += len;
        count++;
    }
    return count;
}

// Rough proportional-font width estimate used only to size a Strand *before* paint. The real
// text is later measured/ellipsized against this box with Android's actual font metrics
// (IndsinFabricView.drawText), so if this estimate runs narrow, correctly-sized text gets
// truncated even though there was room for it on screen. 0.58 was too tight for real UI fonts
// (bold labels, uppercase-heavy titles, digits); 0.72 plus a small fixed pad biases the box
// slightly *wide* instead — a little extra breathing room beats a clipped "…".
inline double measureTextWidth(const std::string& text, double fontSize) {
    return utf8Length(text) * fontSize * 0.72 + 2.0;
}

// Word-wraps `text` into lines that each fit within `maxWidth` (estimated via measureTextWidth).
// UTF-8 safe: words are split on the ASCII space byte (0x20), which is always a standalone byte
// in UTF-8 (never a continuation byte), so this is safe for Arabic and any other multi-byte text.
// A single word wider than maxWidth is kept on its own line rather than dropped/clipped — the
// paint layer is responsible for the final real-font ellipsis if it still overflows its box.
inline std::vector<std::string> wrapText(const std::string& text, double fontSize, double maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) { lines.push_back(""); return lines; }
    if (maxWidth <= 0) { lines.push_back(text); return lines; }

    std::vector<std::string> words;
    std::string cur;
    for (size_t i = 0; i < text.size(); ) {
        if (text[i] == ' ') {
            if (!cur.empty()) { words.push_back(cur); cur.clear(); }
            i++;
        } else {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
            cur += text.substr(i, len);
            i += len;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    if (words.empty()) { lines.push_back(""); return lines; }

    std::string lineBuf;
    for (auto& w : words) {
        std::string candidate = lineBuf.empty() ? w : (lineBuf + " " + w);
        if (lineBuf.empty() || measureTextWidth(candidate, fontSize) <= maxWidth) {
            lineBuf = candidate;
        } else {
            lines.push_back(lineBuf);
            lineBuf = w;
        }
    }
    if (!lineBuf.empty() || lines.empty()) lines.push_back(lineBuf);
    return lines;
}

// Avatar size (§?): a small named scale (small/medium/large), same dual token-or-number
// convention every other Indsin scale (spacing/radius/typography/button-size) already uses.
inline double resolveAvatarSize(const Strand& s) {
    const Value* v = s.attr("size");
    if (!v) return 40;
    if (v->kind == Value::Kind::NUMBER) return v->number;
    if (v->str == "small") return 28;
    if (v->str == "medium") return 40;
    if (v->str == "large") return 56;
    if (v->str == "xl") return 72;
    return v->asNumber(40);
}

struct IndsinStats { int strandsMeasured = 0; int cacheHits = 0; };

struct Indsin {
    IndsinStats stats;
    std::vector<const Strand*> intrinsicProbing; // عناصر قيد القياس الاستكشافي (منع التكرار الذاتي)

    Rect layout(StrandPtr s, Constraints cIncoming, double originXIncoming, double originYIncoming) {
        bool sameConstraints = s->hasLastConstraints &&
            s->lastConstraints.maxW == cIncoming.maxW && s->lastConstraints.maxH == cIncoming.maxH &&
            s->lastConstraints.minW == cIncoming.minW && s->lastConstraints.minH == cIncoming.minH;
        bool sameContent = s->hasLastContentHash && s->lastContentHash == s->contentHash;
        if (sameConstraints && sameContent && s->geometry.w > 0) {
            stats.cacheHits++;
            double dx = originXIncoming - s->geometryOuter.x, dy = originYIncoming - s->geometryOuter.y;
            if (dx != 0.0 || dy != 0.0) { translate(s, dx, dy); s->geometryOuter.x += dx; s->geometryOuter.y += dy; }
            if (s->screenRoot) webPostLayout(*this, s);
            return s->geometryOuter;
        }
        stats.strandsMeasured++;
        s->lastConstraints = cIncoming; s->hasLastConstraints = true;
        s->lastContentHash = s->contentHash; s->hasLastContentHash = true;

        // visible=false (عام لكل العناصر — يعتمد عليه <If>/<Else> في صيغة الوسوم): يُطوى إلى حجم صفر فلا يشغل
        // مساحة ولا يُرسم (انظر Dye::paintInto). Banner/Tag/Drawer لهم قاعدتهم الخاصة أدناه.
        if (webCollapsed(*s) || (s->kind != StrandKind::BANNER && s->kind != StrandKind::TAG && s->attrStr("visible", "true") == "false")) {
            s->geometry = {originXIncoming, originYIncoming, 0, 0};
            s->geometryOuter = s->geometry;
            return s->geometryOuter;
        }

        // Web Layout Concepts: order=<n> يعيد ترتيب الأبناء (مستقراً)، و<Flex> يُحسم نوعه الفعلي (Row/Column/Wrap)
        // من flex_direction / flow / flex_wrap — آمن للتكرار (العكس يتم مرة واحدة عبر _reversed).
        webApplyOrder(*s);
        if (s->sourceTag == "Flex" || s->sourceTag == "flex") {
            WebFlex wf = webReadFlex(*s);
            s->kind = wf.wrap ? StrandKind::WRAP : (wf.column ? StrandKind::COLUMN : StrandKind::ROW);
            if (wf.reverse && !s->attr("_reversed")) {
                if (wf.column || wf.wrap) std::reverse(s->children.begin(), s->children.end());
                else s->attrs.push_back({"direction", nullptr, Value::txt("rtl")}); // row-reverse = RTL mirror (ترتيب المصفوفة ثابت)
                s->attrs.push_back({"_reversed", nullptr, Value::txt("1")});
            }
        }

        // ---- Margin box model (new: style/screen/spacing/width/height feature pass) -----------
        // Margin is space OUTSIDE this Strand's own box: reserved so siblings don't overlap it,
        // but never painted into by this Strand's own background/border/content. Resolved against
        // the *incoming* (pre-margin) constraints, then folded into a shrunk `c`/shifted `origin`
        // so every line below this block — completely unchanged — keeps working exactly as before
        // for any Strand that never sets margin= (mL=mT=mR=mB=0, c==cIncoming, origin==incoming).
        double marginL=0, marginT=0, marginR=0, marginB=0;
        resolveMargin(*s, cIncoming.maxW, marginL, marginT, marginR, marginB);
        Constraints c = cIncoming;
        c.maxW = std::max(0.0, c.maxW - marginL - marginR);
        c.maxH = std::max(0.0, c.maxH - marginT - marginB);
        c.minW = std::min(c.minW, c.maxW);
        c.minH = std::min(c.minH, c.maxH);
        double originX = originXIncoming + marginL, originY = originYIncoming + marginT;

        // Screen size presets (new): `@loop=... screen="phone"|"tablet"|"desktop"|...;` fills in
        // whichever of width=/height= wasn't set explicitly. No effect on non-Loop strands, and no
        // effect at all once an explicit width=/height= is present (checked first, right below).
        if (s->role == rin::UiRole::LOOP && s->attr("screen")) {
            double pw=0, ph=0;
            if (resolveScreenPreset(s->attrStr("screen",""), pw, ph)) {
                if (!s->attr("width"))  { double w = std::min(pw, c.maxW); c.minW = w; c.maxW = w; }
                if (!s->attr("height")) { double h = std::min(ph, c.maxH); c.minH = h; c.maxH = h; }
            }
        }

        // Design canvas on the screen root (new): `screen="phone|390x844|..."`, `canvas_height=` set a
        // *minimum* canvas height (width is the session's canvas width, resolved by the host from the same
        // attributes). Content taller than the minimum still grows the canvas.
        if (s->screenRoot && s->role != rin::UiRole::LOOP) {
            double cw = 0, ch = 0;
            if (resolveCanvasSize(*s, c.maxW, cw, ch) && ch > 0) c.minH = std::min(ch, c.maxH);
        }

        // Explicit width/height (real sizing, not just an estimate hint): if the .rin source sets
        // width=/height= on ANY strand kind, that becomes a hard min==max constraint for it — the
        // same "box model" every real UI toolkit uses — clamped so it never exceeds what the
        // parent actually offered (a Card can't demand more room than its Column gave it).
        // Both accept a percentage ("50%"), resolved against the parent's own available extent on
        // that axis, in addition to the original plain-number form.
        Constraints c2 = c;
        // Web Layout Concepts: width/height="min-content|max-content|fit-content" (IntrinsicSize) تُقاس من المحتوى
        // لا من الأب؛ box_sizing="content-box" يجعل width/height للمحتوى فقط (يُضاف إليه padding+border).
        int kwW = webIntrinsicKeyword(s->attr("width")), kwH = webIntrinsicKeyword(s->attr("height"));
        double bsExtra = (webLower(s->attrStr("box_sizing", "border-box")) == "content-box")
                         ? 2.0 * (resolveSpacing(*s, "padding", 0) + s->attrNum("border", 0)) : 0.0;
        if (s->attr("width") && !kwW)  { double w = std::max(0.0, std::min(resolveSizeAttr(*s, "width", c.maxW, c.maxW) + bsExtra, c.maxW));  c2.minW = w; c2.maxW = w; }
        if (s->attr("height") && !kwH) { double h = std::max(0.0, std::min(resolveSizeAttr(*s, "height", c.maxH, c.maxH) + bsExtra, c.maxH)); c2.minH = h; c2.maxH = h; }
        if (kwW && std::find(intrinsicProbing.begin(), intrinsicProbing.end(), s.get()) == intrinsicProbing.end()) {
            intrinsicProbing.push_back(s.get());
            layout(s, Constraints{0, 1e9, 0, 1e9}, 0, 0);          // قياس طبيعي (max-content) بلا قيد
            intrinsicProbing.pop_back();
            double maxContent = s->geometry.w;
            double target = (kwW == 1) ? webMinContentWidth(*s) : (kwW == 2 ? maxContent : std::min(maxContent, c.maxW));
            target = std::max(0.0, std::min(target, c.maxW));
            c2.minW = target; c2.maxW = target;
            s->lastConstraints = cIncoming; s->hasLastConstraints = true;   // أعد ختم الكاش بعد القياس الاستكشافي
            s->lastContentHash = s->contentHash; s->hasLastContentHash = true;
        }
        // intrinsic_size="160x90" | 120: حجم افتراضي لعنصر بلا محتوى ذاتي (صورة/فيديو/صندوق فارغ) ولا width/height.
        if (const Value* isz = s->attr("intrinsic_size")) {
            if (!s->attr("width") && !s->attr("height") && s->children.empty() &&
                (s->kind == StrandKind::IMAGE || s->kind == StrandKind::VIDEO || s->kind == StrandKind::WEBVIEW ||
                 s->kind == StrandKind::BOX || s->kind == StrandKind::CUSTOM)) {
                double iw = 0, ih = 0; std::string t = webLower(isz->asString());
                size_t x = t.find('x');
                try { if (x == std::string::npos) { iw = ih = std::stod(t); } else { iw = std::stod(t.substr(0, x)); ih = std::stod(t.substr(x + 1)); } } catch (...) {}
                if (iw > 0) { double w = std::min(iw, c.maxW); c2.minW = w; c2.maxW = w; }
                if (ih > 0) { double h = std::min(ih, c.maxH); c2.minH = h; c2.maxH = h; }
            }
        }

        // aspect="16:9" (new, any strand): height follows a *definite* width (explicit width= or a width
        // forced by the parent, e.g. fill/stretch). An explicit height= always wins.
        // shape="circle"|"square" implies aspect 1 (a circle also gets a pill radius at paint time).
        Value shapeAspect; const Value* aspectV = s->attr("aspect");
        { std::string shp = s->attrStr("shape", ""); if (!aspectV && (shp == "circle" || shp == "square")) { shapeAspect = Value::num(1.0); aspectV = &shapeAspect; } }
        if (aspectV && !s->attr("height") && s->kind != StrandKind::TEXT) {
            double ar = 0;
            if (parseAspectRatio(aspectV, ar)) {
                double defW = s->attr("width") ? c2.maxW : ((c2.minW > 0 && c2.minW >= c2.maxW - 0.01) ? c2.maxW : -1);
                if (defW > 0) { double hh = std::min(defW / ar, c.maxH); c2.minH = hh; c2.maxH = hh; }
            }
        }

        // Constraint System (§6): min/max-*, independent of width=/height= above, so e.g. a
        // content-sized Card can still be given a floor or a ceiling without pinning it exactly.
        // Same percentage support as width=/height= just above.
        if (s->attr("min_width"))  c2.minW = std::max(c2.minW, resolveSizeAttr(*s, "min_width", c.maxW, c2.minW));
        if (s->attr("max_width"))  c2.maxW = std::min(c2.maxW, resolveSizeAttr(*s, "max_width", c.maxW, c2.maxW));
        if (s->attr("min_height")) c2.minH = std::max(c2.minH, resolveSizeAttr(*s, "min_height", c.maxH, c2.minH));
        if (s->attr("max_height")) c2.maxH = std::min(c2.maxH, resolveSizeAttr(*s, "max_height", c.maxH, c2.maxH));

        // Sizing modes (§5): fill/expand claim the parent's full *bounded* extent; fixed defers
        // to width=/height= above (a no-op here if neither was actually given -- see the scope
        // note on SizingMode in rin_indsin_tokens.h); fit/hug/auto all mean "measure my own
        // content", which is what every per-kind measure function below already does.
        SizingMode sizing = resolveSizingMode(*s);
        // Spacer (§15) defaults to "expand" without requiring sizing="expand" on every use — an
        // explicit sizing= (or width=/height=, handled above via c2 already) still overrides it.
        if (s->kind == StrandKind::SPACER && !s->attr("sizing")) sizing = SizingMode::EXPAND;
        if ((sizing == SizingMode::FILL || sizing == SizingMode::EXPAND) && !s->attr("width") && c.maxW < 1e8)
            { c2.minW = c2.maxW = c.maxW; }
        if ((sizing == SizingMode::FILL || sizing == SizingMode::EXPAND) && !s->attr("height") && c.maxH < 1e8)
            { c2.minH = c2.maxH = c.maxH; }

        Rect size;
        // Web Layout Concepts — Scroll: حاوية التمرير تقيس أبناءها بلا قيد على محور التمرير ثم تُقصّ إلى نافذتها.
        const bool scrollY = webScrollsY(*s), scrollX = webScrollsX(*s);
        const Constraints cView = c2;
        if (scrollY && c2.maxH < 1e8) c2.maxH = 1e9;
        if (scrollX && c2.maxW < 1e8) c2.maxW = 1e9;
        // Web Layout Concepts — LayoutEngine: layout="row|column|stack|grid|wrap|flex|block" يختار خوارزمية التخطيط
        // لأي حاوية عامة (Box/Column/Row/Stack/Grid/Wrap) دون تغيير وسمها.
        bool modeHandled = false;
        if (s->kind == StrandKind::BOX || s->kind == StrandKind::COLUMN || s->kind == StrandKind::ROW ||
            s->kind == StrandKind::STACK || s->kind == StrandKind::GRID || s->kind == StrandKind::WRAP) {
            std::string lm = webLower(s->attrStr("layout", ""));
            if (!lm.empty()) {
                modeHandled = true;
                if (lm == "flex") { WebFlex wf = webReadFlex(*s); lm = wf.wrap ? "wrap" : (wf.column ? "column" : "row"); }
                if (lm == "row" || lm == "horizontal") size = layoutLinear(s, c2, Axis::X, originX, originY);
                else if (lm == "column" || lm == "vertical" || lm == "block") size = layoutLinear(s, c2, Axis::Y, originX, originY);
                else if (lm == "stack" || lm == "layered") size = layoutStack(s, c2, originX, originY);
                else if (lm == "grid") size = layoutGrid(s, c2, originX, originY);
                else if (lm == "wrap" || lm == "flow") size = layoutWrap(s, c2, originX, originY);
                else modeHandled = false;
            }
        }
        if (!modeHandled) switch (s->kind) {
            case StrandKind::TEXT:    size = measureText(s, c2); break;
            case StrandKind::IMAGE:   size = measureImage(s, c2); break;
            case StrandKind::BUTTON:  size = measureButton(s, c2); break;
            case StrandKind::ICONBUTTON: size = measureButton(s, c2); break; // same box rules as Button (§18/§2)
            case StrandKind::ICON: {
                double sz = std::min(s->attrNum("size", 24), std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            case StrandKind::DIVIDER: size = {0,0, c2.maxW, std::max(1.0, c2.minH)}; break;
            case StrandKind::CARD:    size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 12) + s->attrNum("border", 0), originX, originY); break;
            case StrandKind::COLUMN:  size = layoutLinear(s, c2, Axis::Y, originX, originY); break;
            case StrandKind::ROW:     size = layoutLinear(s, c2, Axis::X, originX, originY); break;
            case StrandKind::STACK:   size = layoutStack(s, c2, originX, originY); break;

            // ---- new: app-chrome bars — a Row of items, pinned by the Scaffold that hosts them,
            // default height 56 (a standard mobile bar height) unless height= overrides it.
            case StrandKind::HEADER:
            case StrandKind::TOPBAR:
            case StrandKind::BOTTOMBAR: {
                Constraints barC = c2;
                if (!s->attr("height")) { barC.minH = barC.maxH = std::min(56.0, c2.maxH); }
                // FIX (display bug): app-chrome bars must span the full available width by
                // default, same as every real mobile top/bottom bar — a TopBar that only hugs
                // its children's natural width (e.g. just a title's text width) leaves the rest
                // of the row unpainted and everything below/after it misreads as if it had been
                // laid out at the wrong origin. layoutLinear() itself only ever *hugs* content
                // (see its final `return {..., max(totalW,c.minW), ...}` — it fills only when the
                // caller hands it a minW/maxW floor), so — exactly like the height default just
                // above — width= still overrides this when the .rin source sets it explicitly.
                if (!s->attr("width")) { barC.minW = barC.maxW = c2.maxW; }
                size = layoutLinear(s, barC, Axis::X, originX, originY);
                break;
            }

            // ---- new: side drawer / dropdown menu — a Column of items that collapses to zero
            // size when open=false, so it never consumes layout space while hidden. Width
            // defaults to 280 (drawer) unless width= is set; menus size to their content.
            case StrandKind::DRAWER: {
                bool isOpen = s->attrStr("open", "true") != "false";
                if (!isOpen) { size = {0,0,0,0}; break; }
                Constraints dc = c2;
                if (!s->attr("width")) { dc.minW = dc.maxW = std::min(280.0, c2.maxW); }
                // full height overlay — but only when we have a real (finite) height budget to fill.
                // A Drawer that ends up outside Scaffold's dedicated role="drawer"/"sidebar" slot
                // (wrong/typo'd role, or no Scaffold at all) is measured here with whatever
                // constraint its parent handed it, which can be the ~1e9 "unbounded" sentinel used
                // for probe passes and root calls. Forcing dc.maxH to that sentinel used to make the
                // Drawer literally report a ~1e9px height, which then propagated up as the
                // *measured* size of everything above it (e.g. Scaffold's contentUsedH) and made
                // IndsinFabricView try to allocate a view a billion pixels tall — nothing drew at all.
                // When the budget isn't finite, size to content instead (like a normal Column) so a
                // misclassified Drawer degrades to a visibly-wrong-but-recoverable element rather
                // than a fully black preview.
                if (c2.maxH < 1e9) { dc.minH = dc.maxH = c2.maxH; }
                size = layoutLinear(s, dc, Axis::Y, originX, originY);
                break;
            }
            case StrandKind::MENU: {
                bool isOpen = s->attrStr("open", "false") == "true";
                if (!isOpen) { size = {0,0,0,0}; break; }
                size = layoutLinear(s, c2, Axis::Y, originX, originY);
                break;
            }
            case StrandKind::MENUITEM: size = measureButton(s, c2); break; // same box rules as Button

            // ---- new: Banner — a full-width notice box (Card-like padded box, not pinned like
            // TopBar/BottomBar). Height is content-driven unless height= is set; visible=false
            // collapses it to zero size so it takes no layout space while hidden, same rule as
            // Drawer/Menu's open=false above. Its children are its content (Text/Button/Menu
            // strands built from banner.text/banner.action/banner.menu) — Banner does not draw
            // them itself, it just boxes and pads them like Card does.
            case StrandKind::BANNER: {
                bool isVisible = s->attrStr("visible", "true") != "false";
                if (!isVisible) { size = {0,0,0,0}; break; }
                size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 16) + s->attrNum("border", 0), originX, originY);
                break;
            }

            // ---- new: table — a header row (columns=) plus TABLEROW children of cells.
            case StrandKind::TABLE: size = layoutTable(s, c2, originX, originY); break;
            case StrandKind::TABLEROW: size = layoutLinear(s, c2, Axis::X, originX, originY); break;

            // ---- Missing-components pass ----
            // Box (§5/§6, the resolved "Container" naming collision — see the enum comment in
            // rin_indsin_strand.h): the same padded, multi-child, sizing-aware box Card already is,
            // just without Card's elevation-styled default paint (see colorForKind in paint.h).
            case StrandKind::BOX:
                // Box flex mode: any of direction/gap/justify/align/valign turns the Box into a Row/Column with the full
                // linear-layout feature set (gap, alignment, Spacer/fill flex, justify). Plain Box keeps the old stacking.
                if (s->attr("direction") || s->attr("gap") || s->attr("justify") || s->attr("align") || s->attr("valign") ||
                    s->attr("flex_direction") || s->attr("flow") || s->attr("align_items") || s->attr("row_gap") ||
                    s->attr("column_gap") || s->attr("place") || s->attr("space_allocation") || s->attr("auto_space")) {
                    std::string dir = s->attrStr("direction", "column");
                    bool horiz = (dir == "row" || dir == "horizontal");
                    if (s->attr("flex_direction") || s->attr("flow")) horiz = !webReadFlex(*s).column;
                    size = layoutLinear(s, c2, horiz ? Axis::X : Axis::Y, originX, originY);
                } else {
                    size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 0) + s->attrNum("border", 0), originX, originY);
                }
                break;

            case StrandKind::GRID: size = layoutGrid(s, c2, originX, originY); break;
            case StrandKind::WRAP: size = layoutWrap(s, c2, originX, originY); break;

            // §21: Object Inspector — a padded box (identical rules to Card) whose content is
            // the title + field-row Text strands synthesized by applyObjectConveniences() in
            // rin_indsin_object.h *before* layout ever runs, so by the time this switch executes,
            // s->children is just an ordinary list of Text strands like any hand-authored Card.
            case StrandKind::OBJECT: size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 12) + s->attrNum("border", 0), originX, originY); break;

            // Spacer (§15): sizing defaults to "expand" (fills the parent's remaining main-axis
            // extent) unless the .rin source overrides sizing= or gives an explicit width=/height=
            // for a fixed-size gap instead — see the sizing-mode override just above this switch.
            case StrandKind::SPACER: size = {0,0, c2.minW, c2.minH}; break;

            case StrandKind::BADGE: size = measureBadge(s, c2); break;
            case StrandKind::PROGRESS: {
                double h = s->attr("height") ? c2.minH : std::min(8.0, c2.maxH);
                size = {0,0, c2.maxW, std::max(h, c2.minH)};
                break;
            }
            case StrandKind::CHECKBOX: {
                double sz = std::min(s->attrNum("size", 22), std::min(c2.maxW, c2.maxH));
                // Fixed-size control: a parent's cross-axis stretch (screen-root Column) must not blow it up to full width.
                size = {0,0, s->attr("width") ? std::max(sz,c2.minW) : sz, s->attr("height") ? std::max(sz,c2.minH) : sz};
                break;
            }
            case StrandKind::SWITCH: {
                double w = std::min(s->attrNum("width", 44), c2.maxW);
                double h = std::min(s->attrNum("height", 24), c2.maxH);
                size = {0,0, s->attr("width") ? std::max(w,c2.minW) : w, s->attr("height") ? std::max(h,c2.minH) : h}; // same: no implicit stretch
                break;
            }
            case StrandKind::AVATAR: {
                double sz = std::min(resolveAvatarSize(*s), std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            case StrandKind::INPUT: size = measureField(s, c2, 40); break;
            case StrandKind::TEXTAREA: size = measureField(s, c2, 96); break;

            // Dialog/Modal (§?): the same collapse-when-closed rule Drawer/Menu/Banner already
            // use (open=false / visible=false -> zero layout space), and the same padded
            // single-child box Card/Box use otherwise. Scope note (kept honest rather than
            // half-faking a real overlay compositor this engine doesn't have): a Dialog lays out
            // in normal document flow, at whatever size its content needs — true screen-centered
            // overlay positioning is achieved the same way Stack's own doc comment already
            // recommends for badges/FABs: wrap it in `@view.Stack align="center" valign="center"`.
            case StrandKind::DIALOG: {
                bool isOpen = s->attrStr("open", "false") == "true";
                if (!isOpen) { size = {0,0,0,0}; break; }
                size = layoutSingleChildBox(s, c2, resolveSpacing(*s, "padding", 24) + s->attrNum("border", 0), originX, originY);
                break;
            }

            case StrandKind::TABS: size = layoutLinear(s, c2, Axis::X, originX, originY); break;
            case StrandKind::TABITEM: size = measureButton(s, c2); break; // same box rules as Button/MenuItem

            case StrandKind::TOOLTIP: size = measureTooltip(s, c2); break;

            // ---- Ready-elements expansion (docs/RIN_ELEMENTS.md) ----
            // Link: same box a Text of the same string would occupy — it's a plain text run with
            // a link-toned color (see paint.h), not its own layout primitive.
            case StrandKind::LINK: size = measureText(s, c2); break;
            // Radio: same square-ish hit-box sizing as Checkbox (paint.h draws it as a ring
            // instead of a filled square, but the box it's measured into is identical).
            case StrandKind::RADIO: {
                double sz = std::min(s->attrNum("size", 22), std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            // Slider: a full-width track, same default height as Progress's bar (a bit taller so
            // the thumb painted on top of it in paint.h has room), unless height= overrides it.
            case StrandKind::SLIDER: {
                double h = s->attr("height") ? c2.minH : std::min(24.0, c2.maxH);
                size = {0,0, c2.maxW, std::max(h, c2.minH)};
                break;
            }
            // Search/Select/File/Date/Time/CodeEditor: all "a bordered field box sized around its
            // text" like Input/TextArea already are — measureField's existing value=/placeholder=
            // convention covers every one of them (Select's placeholder is its unset label, File's
            // is "no file chosen", Date/Time's is their format hint). Only the default box height
            // differs per kind (CodeEditor gets a much taller default, like a small TextArea).
            case StrandKind::SEARCH: size = measureField(s, c2, 40); break;
            case StrandKind::SELECT: size = measureField(s, c2, 40); break;
            case StrandKind::FILE:   size = measureField(s, c2, 44); break;
            case StrandKind::DATE:   size = measureField(s, c2, 40); break;
            case StrandKind::TIME:   size = measureField(s, c2, 40); break;
            case StrandKind::CODE_EDITOR: size = measureField(s, c2, 220); break;

            // List/ListItem: List is just a Column of ListItem rows (or anything else nested
            // inside it); ListItem is just a Row (icon/text/trailing-action children lay out
            // left-to-right — and right-to-left for free via the new direction="rtl" support
            // layoutLinear gains below, since both reuse the exact same function as Row/Column).
            case StrandKind::LIST: size = layoutLinear(s, c2, Axis::Y, originX, originY); break;
            case StrandKind::LISTITEM: size = layoutLinear(s, c2, Axis::X, originX, originY); break;

            // Calculator: a self-contained widget. Ships a sensible default box (280x360, like
            // Image's 96x96 default) when the .rin source gives it no children of its own; if the
            // author nests real children (e.g. a Grid of Buttons for the keypad), it instead sizes
            // around them exactly like Box/Card do — same "don't force a choice" precedent Object
            // Inspector and Card share.
            case StrandKind::CALCULATOR: size = measureCalculator(s, c2, originX, originY); break;

            // ---- UI/UX Library Expansion ----
            // Tag/Chip: a pill Row of a synthesized Text label + optional close Button (see
            // applyTagConveniences() in rin_indsin_components_ext.h) — exactly the same
            // "synthesize real children, then let the ordinary Row layout do the work" strategy
            // Banner already uses for title=/message=/closable=.
            case StrandKind::TAG: {
                // removable="true" (applyTagConveniences, rin_indsin_components_ext.h) binds
                // visible= to an auto-created Warp cell the synthesized close Button flips to
                // "false" -- same collapse-to-zero-when-hidden rule Banner's visible=false uses
                // just above, so a removed Tag actually leaves the layout instead of just going
                // invisible-but-still-occupying-space.
                bool isVisible = s->attrStr("visible", "true") != "false";
                if (!isVisible) { size = {0,0,0,0}; break; }
                size = layoutLinear(s, c2, Axis::X, originX, originY);
                break;
            }
            case StrandKind::KBD: size = measureKbd(s, c2); break;
            case StrandKind::RATING: size = measureRating(s, c2); break;
            case StrandKind::SKELETON: {
                double w = s->attr("width") ? c2.minW : std::min(120.0, c2.maxW);
                double h = s->attr("height") ? c2.minH : std::min(16.0, c2.maxH);
                size = {0,0, std::max(w,c2.minW), std::max(h,c2.minH)};
                break;
            }
            case StrandKind::SPINNER: {
                // Reuses Avatar's small/medium/large/xl size tokens (resolveAvatarSize) since the
                // semantics ("size=<token or number>") are identical -- just with a smaller
                // un-tokened default (24, not Avatar's 40) since a loading glyph reads oversized
                // at Avatar's default.
                double sz = s->attr("size") ? resolveAvatarSize(*s) : 24.0;
                sz = std::min(sz, std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }
            case StrandKind::STEPS: size = layoutSteps(s, c2, originX, originY); break;
            case StrandKind::STEPITEM: size = measureStepItem(s, c2); break;
            case StrandKind::TIMELINE: size = layoutLinear(s, c2, Axis::Y, originX, originY); break;
            case StrandKind::TIMELINEITEM: size = measureTimelineItem(s, c2); break;
            // Breadcrumb/Pagination: both are convenience-synthesized Rows too (items=/current=+
            // total= -> real Text/Button children -- see rin_indsin_components_ext.h), so plain
            // layoutLinear(Axis::X) already does the right thing once those children exist.
            case StrandKind::BREADCRUMB: size = layoutLinear(s, c2, Axis::X, originX, originY); break;
            case StrandKind::PAGINATION: size = layoutLinear(s, c2, Axis::X, originX, originY); break;
            case StrandKind::DONUT_CHART: {
                // Square, like Spinner/Avatar -- size= (token or number), default 120 (bigger than
                // Spinner's 24 since a chart needs room for its segments to actually read).
                double sz = s->attr("size") ? resolveAvatarSize(*s) : 120.0;
                sz = std::min(sz, std::min(c2.maxW, c2.maxH));
                size = {0,0, std::max(sz,c2.minW), std::max(sz,c2.minH)};
                break;
            }

            // ---- new: media placeholders — sized like an Image (explicit width/height, else a
            // sensible default box). Actual playback happens in the real app, not this preview.
            case StrandKind::VIDEO:
            case StrandKind::WEBVIEW: size = measureMedia(s, c2, 16.0/9.0); break;
            case StrandKind::AUDIO:   size = measureMedia(s, c2, -1); break; // fixed-height bar, full width

            // ---- new: scaffold — composes Header/TopBar (fixed top), BottomBar (fixed bottom),
            // Drawer (overlay), and the remaining Content area, via each child's role= attr.
            case StrandKind::SCAFFOLD: size = layoutScaffold(s, c2, originX, originY); break;

            // ---- new: splash/loading screen — just a centered Stack; the *duration* the app
            // shows it for, and the auto-navigate afterwards, are attrs (duration=, navigate=)
            // read and acted on by the host runtime, not by layout.
            case StrandKind::SPLASH: size = layoutStack(s, c2, originX, originY); break;

            default:                  size = layoutLinear(s, c2, Axis::Y, originX, originY); break; // Bolt fallback
        }
        // fill/expand sizing (§5) is a floor, not just a ceiling: several measure functions above
        // (layoutSingleChildBox, layoutLinear) return content-sized results even when c2's min
        // was widened to c.maxW, since they compute min(content, max) without consulting min. This
        // is the one place that applies to every StrandKind uniformly, so individual measure
        // functions don't each need their own copy of the same clamp.
        // Exception: fixed-size controls (Switch/Checkbox) keep their own size when the parent merely stretches the
        // cross axis — only an explicit width=/height= or sizing=fill/expand widens them.
        bool fixedControl = s->kind == StrandKind::SWITCH || s->kind == StrandKind::CHECKBOX;
        bool wantsFill = sizing == SizingMode::FILL || sizing == SizingMode::EXPAND;
        if (!(fixedControl && !wantsFill && !s->attr("width")))  size.w = std::max(size.w, c2.minW);
        if (!(fixedControl && !wantsFill && !s->attr("height"))) size.h = std::max(size.h, c2.minH);
        if (scrollY || scrollX) {
            s->scrollContentW = size.w; s->scrollContentH = size.h;
            if (scrollY && cView.maxH < 1e8) size.h = std::max(cView.minH, std::min(size.h, cView.maxH));
            if (scrollX && cView.maxW < 1e8) size.w = std::max(cView.minW, std::min(size.w, cView.maxW));
        } else { s->scrollContentW = s->scrollContentH = 0; }
        size.x = originX; size.y = originY;
        s->geometry = size; // true, margin-EXCLUDED box: what paint.h and hit-testing use.
        s->scrollOffX = s->scrollOffY = 0; s->stickyDx = s->stickyDy = 0;
        if (scrollY || scrollX) {
            double maxX = std::max(0.0, s->scrollContentW - size.w), maxY = std::max(0.0, s->scrollContentH - size.h);
            double sx = scrollX ? std::min(std::max(0.0, webReadScroll(*s, 'x')), maxX) : 0.0;
            double sy = scrollY ? std::min(std::max(0.0, webReadScroll(*s, 'y')), maxY) : 0.0;
            if (scrollY) sy = std::min(maxY, std::max(0.0, webSnapScroll(*s, 'y', sy)));
            if (scrollX) sx = std::min(maxX, std::max(0.0, webSnapScroll(*s, 'x', sx)));
            if (sx != 0.0 || sy != 0.0) for (auto& ch : s->children) translate(ch, -sx, -sy);
            s->scrollOffX = sx; s->scrollOffY = sy;
        }
        // position=relative/sticky + offset / translate: إزاحة بصرية للصندوق وأحفاده (المساحة الأصلية محفوظة).
        if (!webOutOfFlow(*s)) {
            WebOffsets wo = webReadOffsets(*s, cIncoming.maxW < 1e8 ? cIncoming.maxW : 0.0, cIncoming.maxH < 1e8 ? cIncoming.maxH : 0.0);
            double sdx = wo.dx, sdy = wo.dy;
            if (webPosition(*s) == WebPosition::RELATIVE) {
                sdx += wo.hasL ? wo.l : (wo.hasR ? -wo.r : 0.0);
                sdy += wo.hasT ? wo.t : (wo.hasB ? -wo.b : 0.0);
            }
            WebTransform wt = webReadTransform(*s, size.w, size.h);
            sdx += wt.tx; sdy += wt.ty;
            if (sdx != 0.0 || sdy != 0.0) translate(s, sdx, sdy);
        }

        // Outer (margin-inclusive) box: what the CALLER of layout() gets back, so a parent
        // (layoutLinear/layoutStack/layoutGrid/layoutWrap) reserves the right amount of cursor
        // space for this child's margin without that margin ever being part of what gets painted.
        // Zero margin (the default) makes this identical to `size` — no behavior change for any
        // existing .rin program that never sets margin=.
        Rect outer = size;
        outer.x = originXIncoming; outer.y = originYIncoming;
        outer.w = size.w + marginL + marginR;
        outer.h = size.h + marginT + marginB;
        s->geometryOuter = outer;
        if (s->screenRoot) webPostLayout(*this, s);
        return outer;
    }

    void translate(StrandPtr s, double dx, double dy) {
        s->geometry.x += dx; s->geometry.y += dy;
        for (auto& c : s->children) translate(c, dx, dy);
    }

    // Real multi-line measurement: text now wraps against the actual available width instead of
    // always being sized (and later ellipsized in paint) as if it were one line — that mismatch
    // is what produced boxes too short for their wrapped content and a screen full of "…".
    // `maxLines=` on the Strand caps how many lines are kept; an explicit height= (baked into
    // c.maxH by layout()) caps it the same way, based on how many lines actually fit.
    Rect measureText(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 16);
        double lineHeight = fontSize * 1.4;

        std::vector<std::string> lines = wrapText(text, fontSize, c.maxW);

        double maxLinesAttr = s->attrNum("maxLines", 0);
        if (maxLinesAttr > 0 && lines.size() > (size_t)maxLinesAttr)
            lines.resize((size_t)maxLinesAttr);

        if (c.maxH < 1e9) {
            size_t fitLines = std::max((size_t)1, (size_t)(c.maxH / lineHeight));
            if (lines.size() > fitLines) lines.resize(fitLines);
        }

        double w = 0;
        for (auto& l : lines) w = std::max(w, measureTextWidth(l, fontSize));
        w = std::min(w, c.maxW);
        double h = std::min(lineHeight * (double)std::max((size_t)1, lines.size()), c.maxH);
        return {0,0, std::max(w, c.minW), std::max(h, c.minH)};
    }
    Rect measureImage(StrandPtr s, Constraints c) {
        double w = std::min(s->attrNum("width", 96), c.maxW);
        double h = std::min(s->attrNum("height", 96), c.maxH);
        return {0,0,w,h};
    }
    Rect measureButton(StrandPtr s, Constraints c) {
        std::string label = s->attrStr("label", "");
        if (label.empty()) label = s->attrStr("text", "");
        // IconButton (§18/§2): the icon glyph takes visual width too, so measurement accounts for
        // it the same way it accounts for label= -- a one-glyph-wide stand-in ("@ ") prefixed
        // when icon= is present, whether or not label= is also set. Uses measureTextWidth(), the
        // same estimator label= already goes through -- no separate icon-measurement code path.
        std::string measureLabel = label;
        if (s->kind == StrandKind::ICONBUTTON && !s->attrStr("icon", "").empty())
            measureLabel = label.empty() ? "@" : ("@ " + label);
        ButtonSizePreset sz = resolveButtonSize(*s);
        double fontSize = resolveFontSize(*s, "labelSize", sz.fontSize); // labelSize= can still override the size-token's font size explicitly
        double w = std::min(measureTextWidth(measureLabel, fontSize) + sz.hPadding * 2, c.maxW);
        double h = std::min(sz.height, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }
    Rect layoutSingleChildBox(StrandPtr s, Constraints c, double pad, double originX, double originY) {
        double innerMaxW = std::max(0.0, c.maxW - pad*2), innerMaxH = std::max(0.0, c.maxH - pad*2);
        double contentW=0, contentH=0;
        for (auto& child : s->children) {
            if (isOutOfFlowStrand(*child)) { layout(child, {0, innerMaxW, 0, innerMaxH}, originX + pad, originY + pad); continue; }
            // A Card that its parent stretched (c.minW > 0, e.g. Column align=stretch / screen root) hands that
            // width on: its children fill the inner box instead of shrink-wrapping inside a wide card.
            double childMinW = (c.minW > 0 && innerMaxW < 1e8) ? std::max(0.0, std::min(innerMaxW, c.minW - pad*2)) : 0.0;
            Rect r = layout(child, {childMinW, innerMaxW, 0, innerMaxH}, originX + pad, originY + pad + contentH);
            contentW = std::max(contentW, r.w); contentH += r.h + 8;
        }
        if (!s->children.empty()) contentH -= 8;
        return {0,0, std::min(contentW + pad*2, c.maxW), std::min(contentH + pad*2, c.maxH)};
    }
    // A child counts as "flexible" on the main axis when its sizing resolves to fill/expand
    // for that axis and it has no explicit width=/height= pinning it (same precedence
    // width=/height= already has over sizing= elsewhere — see layout()'s c2 computation).
    // Spacer (§15) is flexible on the main axis by definition (its own default sizing, applied
    // in layout() before this runs) even without an explicit sizing= attribute.
    bool isMainAxisFlexible(const StrandPtr& child, Axis axis) {
        bool hasExplicitMain = (axis == Axis::Y) ? (child->attr("height") != nullptr) : (child->attr("width") != nullptr);
        if (hasExplicitMain) return false;
        SizingMode sm = resolveSizingMode(*child);
        if (child->kind == StrandKind::SPACER && !child->attr("sizing")) sm = SizingMode::EXPAND;
        return sm == SizingMode::FILL || sm == SizingMode::EXPAND;
    }

    Rect layoutLinear(StrandPtr s, Constraints c, Axis axis, double originX, double originY) {
        // RowGap/ColumnGap: فجوة المحور الرئيسي (Column -> row_gap، Row -> column_gap) وإلا gap.
        double gap = resolveSpacing(*s, axis == Axis::Y ? "row_gap" : "column_gap", resolveSpacing(*s, "gap", 0));
        double padding = resolveSpacing(*s, "padding", 0) + s->attrNum("border", 0);
        double crossMax=0, cursorMain=padding;
        double innerMaxW = std::max(0.0, c.maxW - padding*2), innerMaxH = std::max(0.0, c.maxH - padding*2);
        double innerMain = (axis == Axis::Y) ? innerMaxH : innerMaxW;
        // Screen root (a <Screen size=…> / canvas height): its main axis is unbounded but it has a minimum height,
        // so flexible children (Spacer / sizing=fill) share the slack of that minimum -- this is what pins a NavBar
        // placed after a <Spacer /> to the bottom of the screen.
        if (s->screenRoot && axis == Axis::Y && innerMain >= 1e8 && c.minH > 0 && c.minH < 1e8)
            innerMain = std::max(0.0, c.minH - padding * 2);
        // Cross-axis alignment: Column (axis=Y) aligns children horizontally via `align`;
        // Row (axis=X) aligns children vertically via `valign`. Default is left/top (offset 0),
        // matching the previous unaligned behavior exactly, so existing .rin files don't move.
        std::string placeV, placeH;
        const bool hasPlace = webReadPlace(*s, placeV, placeH);
        std::string crossMode = (axis == Axis::Y) ? (s->screenRoot ? "stretch" : "left") : "top";
        { const char* ck = (axis == Axis::Y) ? "align" : "valign";
          if (s->attr(ck)) crossMode = webAlignWord(s->attrStr(ck));
          else if (s->attr("align_items")) crossMode = webAlignWord(s->attrStr("align_items"));
          else if (hasPlace) crossMode = webAlignWord(axis == Axis::Y ? placeH : placeV); }
        // AlignSelf: محاذاة العمود/الصف الفرعي الخاصة به (تتفوق على align_items).
        auto modeOf = [&](const StrandPtr& ch) -> std::string {
            const Value* v = ch->attr("align_self"); return v ? webAlignWord(v->asString()) : crossMode; };
        double innerCross = (axis == Axis::Y) ? innerMaxW : innerMaxH;

        // Two-pass flex distribution (needed for Spacer/fill children to share the main axis
        // correctly with siblings placed *after* them — a single top-to-bottom pass would let an
        // earlier flexible child claim 100% of the remaining space and starve later siblings,
        // e.g. `Row { Text; Spacer; Button; }` would previously give the trailing Button zero
        // width). Pass 1 measures every non-flexible child at its natural size (bounded only by
        // the *unbounded-so-far* main axis, since we don't yet know how much flexible siblings
        // will need) and sums how much main-axis space they + gaps actually consume. Whatever is
        // left over (never negative) is split evenly across the flexible children in pass 2.
        // FIX (display bug): when this container's own main axis is itself unbounded (the ~1e9
        // sentinel used for the true render root / probe passes, same convention as the `< 1e8`
        // checks elsewhere in this file), "leftover" below would be ~1e9 and every flexible child
        // would be handed a ~1e9 share of it as a *real* min==max constraint -- not an estimate,
        // a concrete height (or width in a Row) that IndsinFabricView then tries to actually
        // allocate and draw. That's the exact class of bug already fixed for Drawer above ("size
        // to content instead" when the budget isn't finite) and for the implicit-Spacer cross-axis
        // case below -- just never generalized to ordinary sizing="fill"/"expand" children. A
        // sizing="fill" Card as the second child of a root Column with no fixed screen height (see
        // samples/indsin_sizing_demo.rin) used to report height=1000000000 and silently push every
        // sibling after it to y=1000000000 too. With no real budget to divide, flexible children
        // just fall back to measuring their own natural size this pass, exactly like non-flexible
        // children already do.
        bool unboundedMain = innerMain >= 1e8;
        const std::string allocation = webLower(s->attrStr("space_allocation", ""));
        double sumWeight = 0;
        std::vector<bool> flexible(s->children.size());
        std::vector<double> mainSizeOf(s->children.size(), 0.0);
        double fixedMainUsed = 0; int flexCount = 0; size_t flowChildren = 0;
        for (size_t i = 0; i < s->children.size(); i++) {
            // Overlay Engine (rin_indsin_overlay.h): an open Dialog / anchored Tooltip is excluded
            // from this container's main-axis budget entirely -- it isn't "flexible" and it isn't
            // "fixed-size-and-counted" either, it simply isn't part of this flow anymore (like a
            // real position:fixed/absolute element). It's still measured, just later, against its
            // own full available box rather than a budget derived from siblings that don't concern
            // it (see the placement loop below) -- an important distinction: measuring it *here*
            // against a probe constraint, the way ordinary fixed-size children are, is exactly
            // what previously corrupted its own box (a stale note in isOverlayStrand's own comment
            // above explains why that approach was abandoned).
            if (isOutOfFlowStrand(*s->children[i])) { flexible[i] = false; mainSizeOf[i] = 0; continue; }
            flowChildren++;
            flexible[i] = !unboundedMain && isMainAxisFlexible(s->children[i], axis);
            // SpaceAllocation: equal | proportional يجعلان كل ابن بلا مقاس رئيسي صريح مرناً (proportional يستعمل weight=).
            if (!flexible[i] && !unboundedMain && (allocation == "equal" || allocation == "proportional") &&
                !s->children[i]->attr(axis == Axis::Y ? "height" : "width")) flexible[i] = true;
            if (flexible[i]) { flexCount++; sumWeight += (allocation == "equal") ? 1.0 : std::max(0.0, s->children[i]->attrNum("weight", 1.0)); continue; }
            Constraints probeC = (axis==Axis::Y) ? Constraints{0, innerMaxW, 0, 1e9} : Constraints{0, 1e9, 0, innerMaxH};
            // Relative widths ("50%", half, 30vw...) inside a Row resolve against the Row's content box, not the
            // unbounded probe axis (which made "50%" mean "50% of 1e9" -> clamped to the full row).
            if (axis==Axis::X && innerMain < 1e8 && isRelativeSize(s->children[i]->attr("width"))) probeC.maxW = innerMain;
            // align/valign="stretch": the child claims the container's full (bounded) cross extent.
            if (modeOf(s->children[i]) == "stretch" && !s->children[i]->attr(axis==Axis::Y ? "width" : "height")) {
                if (axis==Axis::Y && innerMaxW < 1e8) probeC.minW = innerMaxW;
                if (axis==Axis::X && innerMaxH < 1e8) probeC.minH = innerMaxH;
            }
            Rect probe = layout(s->children[i], probeC, 0, 0);
            mainSizeOf[i] = (axis==Axis::Y) ? probe.h : probe.w;
            fixedMainUsed += mainSizeOf[i];
        }
        double gapsTotal = flowChildren == 0 ? 0 : gap * (flowChildren - 1);
        double leftover = std::max(0.0, innerMain - fixedMainUsed - gapsTotal);
        double perFlex = flexCount > 0 ? leftover / flexCount : 0.0;
        if (sumWeight <= 0) sumWeight = (double)std::max(1, flexCount);

        struct Placed { StrandPtr child; double crossSize; double localX; double mainSize; };
        std::vector<Placed> placed;
        double mainUsed = 0;

        for (size_t i = 0; i < s->children.size(); i++) {
            auto& child = s->children[i];
            if (isOutOfFlowStrand(*child)) {
                // Out of flow: measured against the container's own full inner box (not a
                // cursor-derived budget -- there is no "space left for it" question, it doesn't
                // share the axis with its siblings at all), so its real content size is correct
                // and ready for the Overlay Engine's second pass. It advances no cursor, claims no
                // gap, and isn't considered below for cross-axis alignment.
                layout(child, {0, innerMaxW, 0, innerMaxH}, originX + padding, originY + padding);
                continue;
            }
            double childWeight = (allocation == "equal") ? 1.0 : std::max(0.0, child->attrNum("weight", 1.0));
            double mainBudget = flexible[i] ? (flexCount > 0 ? leftover * childWeight / sumWeight : 0.0) : mainSizeOf[i];
            (void)perFlex;
            double childMaxW = (axis==Axis::Y) ? innerMaxW : mainBudget;
            double childMaxH = (axis==Axis::Y) ? mainBudget : innerMaxH;
            // An implicit-default Spacer (no explicit sizing= from the .rin source) is only meant
            // to eat slack along the *main* axis, same as every other flex-box-style engine's
            // spacer/expander. Left unguarded, the generic FILL/EXPAND handling in layout() also
            // stretches it across the *cross* axis whenever this container's cross extent happens
            // to be finite -- which it isn't during the Propose (probe) pass (unbounded, ~1e9) but
            // IS during this real Settle pass (bounded to whatever budget the parent already
            // committed to). That Propose/Settle disagreement is exactly what previously let a Row
            // containing a Spacer measure short during Propose (Spacer contributing 0 to cross
            // size) and then grow taller during Settle (Spacer now stretched to the row's full
            // cross budget) -- inflating the row (and any Card/Box stacking it above a sibling)
            // past the box size its own parent already fixed it to, overflowing into whatever sits
            // below. Explicit valign="stretch" / align="stretch" on the container still stretches
            // it on purpose, same as before.
            bool implicitSpacerExpand = (child->kind == StrandKind::SPACER && !child->attr("sizing"));
            if (flexible[i] && implicitSpacerExpand && modeOf(child) != "stretch") {
                if (axis == Axis::Y) childMaxW = 0; else childMaxH = 0;
            }
            if (axis==Axis::X && innerMain < 1e8 && !flexible[i] && isRelativeSize(child->attr("width"))) childMaxW = innerMain;
            Constraints cc = flexible[i]
                ? Constraints{(axis==Axis::Y)?0:mainBudget, childMaxW, (axis==Axis::Y)?mainBudget:0, childMaxH} // flexible: min==max==its share
                : Constraints{0, childMaxW, 0, childMaxH};
            if (modeOf(child) == "stretch" && !implicitSpacerExpand && !child->attr(axis==Axis::Y ? "width" : "height")) {
                if (axis==Axis::Y && innerMaxW < 1e8) cc.minW = innerMaxW;
                if (axis==Axis::X && innerMaxH < 1e8) cc.minH = innerMaxH;
            }
            double localX = (axis==Axis::Y) ? padding : cursorMain;
            double localY = (axis==Axis::Y) ? cursorMain : padding;
            Rect r = layout(child, cc, originX + localX, originY + localY);
            double mainSize = (axis==Axis::Y) ? r.h : r.w, crossSize = (axis==Axis::Y) ? r.w : r.h;
            cursorMain += mainSize + gap; mainUsed += mainSize + gap; crossMax = std::max(crossMax, crossSize);
            placed.push_back({child, crossSize, localX, mainSize});
        }
        if (flowChildren > 0) mainUsed -= gap;

        // justify= (Row / Column / flex Box): distributes the box's spare main-axis space when nothing flexes.
        //   start (default) | center | end | between | around | evenly
        {
            std::string jm = "start";
            if (s->attr("justify")) jm = webJustifyWord(s->attrStr("justify"));
            else if (s->attr("auto_space")) { const Value* av = s->attr("auto_space"); jm = webTruthy(av) ? "between" : webJustifyWord(av->asString()); }
            else if (hasPlace) jm = webJustifyWord(axis == Axis::Y ? placeV : placeH);
            double minMain = (axis == Axis::Y) ? c.minH : c.minW;
            if (jm != "start" && flexCount == 0 && !placed.empty() && minMain > 0 && minMain < 1e8) {
                double free = minMain - padding * 2 - mainUsed;
                size_t n = placed.size();
                if (free > 0.5) {
                    double lead = 0, between = 0;
                    if (jm == "center") lead = free / 2;
                    else if (jm == "end") lead = free;
                    else if (jm == "between") between = n > 1 ? free / (n - 1) : 0;
                    else if (jm == "around") { between = free / n; lead = between / 2; }
                    else if (jm == "evenly") { between = free / (n + 1); lead = between; }
                    double shift = lead;
                    for (auto& pl : placed) {
                        if (axis == Axis::Y) translate(pl.child, 0, shift); else translate(pl.child, shift, 0);
                        shift += between;
                    }
                    mainUsed += free;
                }
            }
        }

        // RTL row support (§ ready-elements expansion — "صف يمين يسار"/row right-to-left): a
        // `direction="rtl"` attribute on a Row (or anything else built on this same axis=X path —
        // TopBar/BottomBar/Tabs/ListItem/TableRow all get it for free) mirrors each child's
        // main-axis position across the content span it just computed above, so child #1 in
        // source order ends up flush against the *right* edge and later children proceed
        // leftward — without touching gap/alignment math or reordering the children array itself
        // (hit-testing, a11y traversal order, etc. all stay in source order; only the visual X
        // shifts). Column (axis=Y) is unaffected — RTL only ever changes horizontal flow.
        if (axis == Axis::X && s->attrStr("direction", "ltr") == "rtl" && !placed.empty()) {
            for (auto& p : placed) {
                double newLocalX = mainUsed - (p.localX - padding) - p.mainSize;
                double dx = (padding + newLocalX) - p.localX;
                if (dx != 0.0) translate(p.child, dx, 0);
            }
        }

        // Second pass: nudge each child along the cross axis now that innerCross is finalized.
        // (Doesn't touch main-axis position, so gap/order above is untouched — only left/top vs
        // center/right/bottom shifts.) Skipped when mode is "stretch" or default (offset stays 0).
        bool anySelf = false;
        for (auto& p : placed) if (p.child->attr("align_self")) { anySelf = true; break; }
        if ((anySelf || (crossMode != "left" && crossMode != "top" && crossMode != "stretch")) && !placed.empty()) {
            for (auto& p : placed) {
                double off = alignOffset(innerCross, p.crossSize, modeOf(p.child));
                if (off <= 0.0) continue;
                if (axis == Axis::Y) translate(p.child, off, 0); else translate(p.child, 0, off);
            }
        }

        double totalW = (axis==Axis::Y) ? std::min(crossMax+padding*2,c.maxW) : std::min(mainUsed+padding*2,c.maxW);
        double totalH = (axis==Axis::Y) ? std::min(mainUsed+padding*2,c.maxH) : std::min(crossMax+padding*2,c.maxH);
        return {0,0, std::max(totalW,c.minW), std::max(totalH,c.minH)};
    }
    Rect layoutStack(StrandPtr s, Constraints c, double originX, double originY) {
        // align (horizontal) / valign (vertical) position each child within the Stack's full
        // bounds — e.g. align="right" valign="bottom" pins a child to the bottom-right corner,
        // useful for badges/FABs over a Card, or a Splash logo centered with align=center valign=center.
        std::string ha = webAlignWord(s->attrStr("align", "left")), va = webAlignWord(s->attrStr("valign", "top"));
        { std::string pv, ph;
          if (!s->attr("align") && !s->attr("valign") && webReadPlace(*s, pv, ph)) { va = webAlignWord(pv); ha = webAlignWord(ph); } }
        double w=0,h=0;
        std::vector<Rect> rects; rects.reserve(s->children.size());
        for (auto& child : s->children) {
            Rect r = layout(child, {0, c.maxW, 0, c.maxH}, originX, originY);
            rects.push_back(r);
            // Overlay Engine (rin_indsin_overlay.h): an open Dialog / anchored Tooltip is measured
            // (so its content box is ready) but excluded from the Stack's own footprint below --
            // the Stack shouldn't grow (or get treated as if it grew) to fit a modal that's about
            // to be re-homed onto the viewport by the overlay pass, and it's excluded from the
            // alignment loop just below for the same reason: its final position is the overlay
            // pass's job now, not this Stack's align=/valign=.
            if (isOutOfFlowStrand(*child)) continue;
            w = std::max(w, r.w); h = std::max(h, r.h);
        }
        // justify_self (أفقي) / align_self (عمودي) لكل ابن؛ center=true / middle=true على الابن تتفوّقان.
        bool anySelf = false;
        for (auto& ch : s->children) if (ch->attr("justify_self") || ch->attr("align_self") || ch->attr("center") || ch->attr("middle")) { anySelf = true; break; }
        if (anySelf || (ha != "left" && ha != "stretch") || (va != "top" && va != "stretch")) {
            for (size_t i = 0; i < s->children.size(); i++) {
                if (isOutOfFlowStrand(*s->children[i])) continue;
                auto& ch = *s->children[i];
                std::string hi = ch.attr("justify_self") ? webAlignWord(ch.attrStr("justify_self")) : ha;
                std::string vi = ch.attr("align_self") ? webAlignWord(ch.attrStr("align_self")) : va;
                if (webTruthy(ch.attr("center"))) hi = vi = "center";
                if (webTruthy(ch.attr("middle"))) vi = "center";
                double dx = alignOffset(w, rects[i].w, hi);
                double dy = alignOffset(h, rects[i].h, vi);
                if (dx != 0.0 || dy != 0.0) translate(s->children[i], dx, dy);
            }
        }
        return {0,0, std::min(w,c.maxW), std::min(h,c.maxH)};
    }

    // ---- new: Table — renders `columns="A,B,C"` as a bold header row, then one TABLEROW per
    // data row (each row's children are the cells for that row, laid out left-to-right). Column
    // widths are the max natural width seen for that column across the header + all rows, summed
    // and clamped to the available width (matching how the rest of the Indsin never overflows its
    // parent). This is what backs "printing"/rendering a table — actual paper/PDF export is a
    // host-app concern layered on top of this same geometry.
    Rect layoutTable(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0);
        double innerMaxW = std::max(0.0, c.maxW - padding*2);
        std::vector<std::string> headers = splitCsv(s->attrStr("columns", ""));
        size_t nCols = headers.size();
        for (auto& row : s->children) nCols = std::max(nCols, row->children.size());
        if (nCols == 0) return {0,0, std::max(0.0,c.minW), std::max(0.0,c.minH)};

        std::vector<double> colW(nCols, 0.0);
        double fontSize = resolveFontSize(*s, "size", 14);
        for (size_t i = 0; i < headers.size(); i++) colW[i] = std::max(colW[i], measureTextWidth(headers[i], fontSize) + 16);
        for (auto& row : s->children)
            for (size_t i = 0; i < row->children.size(); i++)
                colW[i] = std::max(colW[i], measureTextWidth(row->children[i]->attrStr("text",""), fontSize) + 16);

        double naturalTotal = 0; for (double w : colW) naturalTotal += w;
        double scale = (naturalTotal > innerMaxW && naturalTotal > 0) ? innerMaxW / naturalTotal : 1.0;
        for (double& w : colW) w *= scale;

        double rowH = fontSize * 1.4 + 12;
        double cursorY = originY + padding;
        double cursorX0 = originX + padding;

        if (!headers.empty()) { cursorY += rowH; } // header row reserved (drawn by the view layer)

        for (auto& row : s->children) {
            double cx = cursorX0;
            for (size_t i = 0; i < row->children.size() && i < nCols; i++) {
                layout(row->children[i], {colW[i], colW[i], 0, rowH}, cx, cursorY);
                cx += colW[i];
            }
            row->geometry = {cursorX0, cursorY, naturalTotal > 0 ? std::min(naturalTotal*scale, innerMaxW) : innerMaxW, rowH};
            cursorY += rowH;
        }

        double totalH = (cursorY - originY - padding) + padding;
        double totalW = std::min(innerMaxW, naturalTotal * scale) + padding*2;
        return {0,0, std::max(totalW, c.minW), std::max(totalH, c.minH)};
    }

    // ---- new: Video/Audio/WebView(YouTube etc.) — sized like Image. `ratio` (e.g. -1 for Audio)
    // means "fixed height bar, full available width" instead of an aspect-ratio box.
    Rect measureMedia(StrandPtr s, Constraints c, double defaultRatio) {
        if (defaultRatio < 0) { // Audio: a slim full-width control bar
            double w = c.maxW;
            double h = std::min(s->attrNum("height", 56), c.maxH);
            return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
        }
        double w = s->attr("width") ? std::min(s->attrNum("width", c.maxW), c.maxW) : c.maxW;
        double ratio = s->attrNum("ratio", defaultRatio);
        double h = s->attr("height") ? std::min(s->attrNum("height", c.maxH), c.maxH) : std::min(w / std::max(0.01, ratio), c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- new: Scaffold — the "page shell": Header/TopBar (role="topbar", fixed to top),
    // BottomBar (role="bottombar", fixed to bottom), Drawer (role="drawer" or role="sidebar",
    // both accepted — overlays above content, positioned off-screen at x=-width when its own
    // open=false), and Content (role="content" or unmarked, gets whatever vertical space is
    // left between the bars). This is the piece a whole .rin *page* is expected to be wrapped
    // in once TopBar/BottomBar/Drawer are used, so they stay pinned while Content scrolls
    // (scrolling itself is a host ScrollView concern — Content here is simply given the full
    // remaining height to lay out in).
    Rect layoutScaffold(StrandPtr s, Constraints c, double originX, double originY) {
        double topH = 0, bottomH = 0;
        StrandPtr topbar, bottombar, drawer;
        std::vector<StrandPtr> content;
        for (auto& child : s->children) {
            std::string role = child->attrStr("role", "");
            if (role == "topbar" || role == "header") topbar = child;
            else if (role == "bottombar") bottombar = child;
            else if (role == "drawer" || role == "sidebar") drawer = child; // "sidebar" is a common alias authors reach for
            else content.push_back(child);
        }
        if (topbar) { Rect r = layout(topbar, {c.maxW, c.maxW, 0, c.maxH}, originX, originY); topH = r.h; }
        if (bottombar) { Rect probe = layout(bottombar, {c.maxW, c.maxW, 0, c.maxH}, originX, originY); bottomH = probe.h; }
        double contentH = std::max(0.0, c.maxH - topH - bottomH);
        double cy = originY + topH;
        // FIX: track the content's own *measured* height instead of assuming it fills the full
        // (often effectively unbounded, c.maxH == 1e9 from the root call) contentH budget. A
        // Scaffold used to always report back {c.maxW, c.maxH} verbatim as its own size, which at
        // the root meant a Fabric height of ~1e9 px — IndsinFabricView.setFabric() then tried to
        // measure/allocate a view that tall and nothing drew (fully black preview). The Scaffold's
        // true height is topH + however much vertical space its content actually used + bottomH.
        double contentUsedH = 0;
        for (auto& ch : content) {
            Rect r = layout(ch, {c.maxW, c.maxW, 0, contentH}, originX, cy);
            contentUsedH = std::max(contentUsedH, r.h);
        }
        if (bottombar) layout(bottombar, {c.maxW, c.maxW, bottomH, bottomH}, originX, originY + topH + contentUsedH);
        double totalH = topH + contentUsedH + bottomH;
        double screenH = std::min(totalH, c.maxH);
        // FIX: same class of bug as the Scaffold-height fix above, one level down. The drawer/
        // sidebar slot used to be laid out with {c.maxH, c.maxH} as its height constraint --
        // Scaffold's *own incoming* constraint, which at the root is the engine's unbounded
        // sentinel (1e9, see rin_indsin_c_api.cpp). That forced the drawer/sidebar child's
        // measured height to ~1e9px (visible in inspectors as e.g. role=sidebar, h=1000000000),
        // which rendered as a giant absolutely-positioned box covering/blanking the whole
        // preview. The drawer should fill the *actual* screen height Scaffold just computed
        // (screenH, already finite from contentUsedH above), not its own possibly-unbounded
        // incoming budget.
        if (drawer) {
            bool isOpen = drawer->attrStr("open", "false") == "true";
            double dw = drawer->attr("width") ? drawer->attrNum("width", 280) : 280.0;
            double dx = isOpen ? originX : originX - dw; // off-canvas to the left when closed
            layout(drawer, {isOpen ? dw : 0, isOpen ? dw : 0, screenH, screenH}, dx, originY);
        }
        return {0,0, c.maxW, screenH};
    }

    // ---- Missing-components pass: Grid (§15) — `columns=` (default 2) fixed-column-count grid,
    // row-major placement, `gap=` between both rows and columns (a single spacing token/number
    // for both axes, matching the level of detail Row/Column's own `gap=` already offers — a
    // split row_gap=/column_gap= is easy future work if a real .rin author needs it).
    // "2" | "2 / 4" | "2 / span 3" | "span 2" -> (start 0-based or -1 auto, span)
    static void parseGridLine(const Value* v, int& start, int& span) {
        start = -1; span = 1;
        if (!v) return;
        if (v->kind == Value::Kind::NUMBER) { start = std::max(0, (int)v->number - 1); return; }
        std::string t = webLower(v->str);
        size_t sl = t.find('/');
        std::string a = sl == std::string::npos ? t : t.substr(0, sl), b = sl == std::string::npos ? "" : t.substr(sl + 1);
        auto trim = [](std::string x) { while (!x.empty() && x.front() == ' ') x.erase(x.begin()); while (!x.empty() && x.back() == ' ') x.pop_back(); return x; };
        a = trim(a); b = trim(b);
        try {
            if (a.rfind("span", 0) == 0) span = std::max(1, std::stoi(trim(a.substr(4))));
            else if (!a.empty() && a != "auto") start = std::max(0, std::stoi(a) - 1);
            if (b.rfind("span", 0) == 0) span = std::max(1, std::stoi(trim(b.substr(4))));
            else if (!b.empty() && start >= 0) span = std::max(1, std::stoi(b) - 1 - start);
        } catch (...) {}
    }

    // Grid: columns=N | "auto-fit" | "auto-fill" (+ min_column_width=), gap/row_gap/column_gap، وضع مُتقدّم عند
    // grid_column/grid_row (تموضع ومدى)، justify_items/align_items/justify_self/align_self. بلا أي من هذه تبقى
    // الخوارزمية القديمة حرفياً (صف بصف، خلايا بعرض كامل).
    Rect layoutGrid(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0), gap = resolveSpacing(*s, "gap", 0);
        double colGap = resolveSpacing(*s, "column_gap", gap), rowGap = resolveSpacing(*s, "row_gap", gap);
        double innerMaxW = std::max(0.0, c.maxW - padding*2);
        std::vector<StrandPtr> flow;
        for (auto& ch : s->children) {
            if (isOutOfFlowStrand(*ch)) { layout(ch, {0, innerMaxW, 0, 1e9}, originX + padding, originY + padding); continue; }
            flow.push_back(ch);
        }
        // أعمدة: رقم | auto-fit | auto-fill
        int columns = 2; bool autoFit = false, autoFill = false;
        std::string colSpec = webLower(s->attrStr("columns", ""));
        if (colSpec == "auto-fit" || colSpec == "autofit") autoFit = true;
        else if (colSpec == "auto-fill" || colSpec == "autofill") autoFill = true;
        else if (s->attr("auto_fit"))  autoFit = true;
        else if (s->attr("auto_fill")) autoFill = true;
        else columns = std::max(1, (int)s->attrNum("columns", 2));
        if (autoFit || autoFill) {
            double minW = 160;
            for (const char* k : {"min_column_width", "column_width"}) if (s->attr(k)) { minW = std::max(1.0, resolveSpacing(*s, k, 160, innerMaxW)); break; }
            const Value* af = s->attr(autoFit ? "auto_fit" : "auto_fill");
            if (af && af->kind == Value::Kind::NUMBER && af->number > 1) minW = af->number;
            columns = std::max(1, (int)std::floor((innerMaxW + colGap) / (minW + colGap)));
            if (autoFit) columns = std::max(1, std::min(columns, (int)flow.size())); // المسارات الفارغة تنطوي فتتمدد الخلايا
        }
        double cellW = std::max(0.0, (innerMaxW - colGap*(columns-1)) / columns);

        bool advanced = autoFit || autoFill || s->attr("align_items") || s->attr("justify_items");
        for (auto& ch : flow) if (ch->attr("grid_column") || ch->attr("grid_row") || ch->attr("align_self") || ch->attr("justify_self")) { advanced = true; break; }

        if (!advanced) {
            double cursorY = padding;
            for (size_t i = 0; i < flow.size(); i += columns) {
                double rowH = 0;
                size_t rowEnd = std::min(flow.size(), i + (size_t)columns);
                for (size_t j = i; j < rowEnd; j++) {
                    int col = (int)(j - i);
                    double localX = padding + col * (cellW + colGap);
                    Rect r = layout(flow[j], {cellW, cellW, 0, 1e9}, originX + localX, originY + cursorY);
                    rowH = std::max(rowH, r.h);
                }
                cursorY += rowH + rowGap;
            }
            if (!flow.empty()) cursorY -= rowGap;
            double totalH = cursorY + padding*2;
            double totalW = innerMaxW + padding*2;
            return {0,0, std::min(totalW,c.maxW), std::max(std::min(totalH,c.maxH), c.minH)};
        }

        // ---- الوضع المتقدّم: تموضع صريح + مدى + محاذاة الخلايا ----
        struct Item { StrandPtr ch; int col, row, cs, rs; double h = 0; };
        std::vector<Item> items; std::vector<std::vector<char>> occ;
        auto fits = [&](int r, int cc, int rs, int cs) {
            if (cc + cs > columns) return false;
            for (int y = r; y < r + rs; y++) for (int x = cc; x < cc + cs; x++) if (y < (int)occ.size() && occ[y][x]) return false;
            return true; };
        auto mark = [&](int r, int cc, int rs, int cs) {
            while ((int)occ.size() < r + rs) occ.emplace_back(columns, 0);
            for (int y = r; y < r + rs; y++) for (int x = cc; x < cc + cs; x++) occ[y][x] = 1; };
        int cursorR = 0, cursorC = 0;
        for (auto& ch : flow) {                       // 1) العناصر المثبّتة صراحةً أولاً
            Item it{ch, -1, -1, 1, 1};
            parseGridLine(ch->attr("grid_column"), it.col, it.cs); parseGridLine(ch->attr("grid_row"), it.row, it.rs);
            it.cs = std::min(it.cs, columns);
            if (it.col >= 0) it.col = std::min(it.col, columns - it.cs);
            items.push_back(it);
        }
        for (auto& it : items) if (it.col >= 0 && it.row >= 0) mark(it.row, it.col, it.rs, it.cs);
        for (auto& it : items) {                      // 2) الباقي: تدفّق تلقائي بالترتيب
            if (it.col >= 0 && it.row >= 0) continue;
            if (it.col >= 0) { int r = 0; while (!fits(r, it.col, it.rs, it.cs)) r++; it.row = r; mark(it.row, it.col, it.rs, it.cs); continue; }
            int r = it.row >= 0 ? it.row : cursorR, cc = it.row >= 0 ? 0 : cursorC;
            for (;; ) {
                if (fits(r, cc, it.rs, it.cs)) break;
                if (++cc + it.cs > columns) { cc = 0; r++; }
            }
            it.row = r; it.col = cc; mark(r, cc, it.rs, it.cs);
            cursorR = r; cursorC = cc + it.cs; if (cursorC >= columns) { cursorC = 0; cursorR++; }
        }
        int rows = (int)occ.size();
        std::string gJust = webLower(s->attrStr("justify_items", "stretch")), gAlign = webAlignWord(s->attrStr("align_items", "start"));
        gJust = webAlignWord(gJust);
        auto cellWidth = [&](const Item& it) { return cellW * it.cs + colGap * (it.cs - 1); };
        // قياس الارتفاعات
        std::vector<double> rowH(rows, 0.0);
        for (auto& it : items) {
            double w = cellWidth(it);
            std::string ji = it.ch->attr("justify_self") ? webAlignWord(it.ch->attrStr("justify_self")) : gJust;
            Constraints pc = (ji == "stretch") ? Constraints{w, w, 0, 1e9} : Constraints{0, w, 0, 1e9};
            Rect r = layout(it.ch, pc, 0, 0); it.h = r.h;
            if (it.rs == 1) rowH[it.row] = std::max(rowH[it.row], r.h);
        }
        for (auto& it : items) if (it.rs > 1) {       // مدى متعدد الصفوف: العجز يُضاف لآخر صف في المدى
            double have = rowGap * (it.rs - 1);
            for (int y = it.row; y < it.row + it.rs && y < rows; y++) have += rowH[y];
            if (it.h > have) rowH[std::min(rows - 1, it.row + it.rs - 1)] += it.h - have;
        }
        std::vector<double> rowY(rows + 1, padding);
        for (int y = 0; y < rows; y++) rowY[y + 1] = rowY[y] + rowH[y] + rowGap;
        for (auto& it : items) {
            double w = cellWidth(it), x = padding + it.col * (cellW + colGap), y = rowY[it.row];
            double spanH = 0; for (int k = it.row; k < it.row + it.rs && k < rows; k++) spanH += rowH[k];
            spanH += rowGap * (it.rs - 1);
            std::string ji = it.ch->attr("justify_self") ? webAlignWord(it.ch->attrStr("justify_self")) : gJust;
            std::string ai = it.ch->attr("align_self") ? webAlignWord(it.ch->attrStr("align_self")) : gAlign;
            Constraints cc2 = (ji == "stretch") ? Constraints{w, w, 0, 1e9} : Constraints{0, w, 0, 1e9};
            if (ai == "stretch") cc2.minH = spanH;
            Rect r = layout(it.ch, cc2, originX + x, originY + y);
            double dx = (ji == "center" || ji == "end" || ji == "right") ? alignOffset(w, r.w, ji) : 0.0;
            double dy = (ai == "center" || ai == "end" || ai == "bottom") ? alignOffset(spanH, r.h, ai) : 0.0;
            if (dx != 0.0 || dy != 0.0) translate(it.ch, dx, dy);
        }
        double totalH = (rows > 0 ? rowY[rows] - rowGap : 0.0) + padding;   // rowY[0] == padding
        totalH += padding;
        double totalW = innerMaxW + padding*2;
        return {0,0, std::min(totalW,c.maxW), std::max(std::min(totalH,c.maxH), c.minH)};
    }

    // ---- Missing-components pass: Wrap (§15) — a flow layout: children run left-to-right like
    // Row, but overflow onto a new line instead of being clipped/overlapping once a line's
    // accumulated width would exceed the available width. `gap=` applies to both the horizontal
    // space between items on a line and the vertical space between lines.
    Rect layoutWrap(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0), gap = resolveSpacing(*s, "gap", 0);
        double colGap = resolveSpacing(*s, "column_gap", gap), rowGap = resolveSpacing(*s, "row_gap", gap);
        double innerMaxW = std::max(0.0, c.maxW - padding*2);

        double cursorX = 0, cursorY = 0, lineH = 0, usedW = 0;
        bool lineHasItem = false;
        for (auto& child : s->children) {
            if (isOutOfFlowStrand(*child)) { layout(child, {0, innerMaxW, 0, 1e9}, originX + padding, originY + padding); continue; }
            // Probe the child's natural width first (unbounded height) so we know whether it
            // fits on the current line before committing to a final position for it.
            Rect probe = layout(child, {0, innerMaxW, 0, 1e9}, 0, 0);
            if (lineHasItem && cursorX + probe.w > innerMaxW) {
                cursorY += lineH + rowGap; cursorX = 0; lineH = 0; lineHasItem = false;
            }
            Rect r = layout(child, {0, innerMaxW, 0, 1e9}, originX + padding + cursorX, originY + padding + cursorY);
            cursorX += r.w + colGap; lineH = std::max(lineH, r.h); lineHasItem = true;
            usedW = std::max(usedW, originX + padding + cursorX - colGap - originX);
        }
        double totalH = cursorY + lineH + padding*2;
        double totalW = innerMaxW + padding*2; // Wrap claims the full offered width, same as Row
        return {0,0, std::min(totalW,c.maxW), std::max(std::min(totalH,c.maxH), c.minH)};
    }

    // ---- Missing-components pass: Badge (§?) — a small pill/label, content-sized around its
    // own `text=` attribute (it is not a Text strand itself, same relationship Button's `label=`
    // has to the real TEXT strand kind: one self-contained box that draws its own text in Dye).
    Rect measureBadge(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 12);
        double hPad = 8, vPad = 4;
        double w = std::min(measureTextWidth(text, fontSize) + hPad*2, c.maxW);
        double h = std::min(fontSize*1.4 + vPad*2, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }
    // Tooltip: same idea as Badge (self-contained text box) but rectangular with soft corners
    // and slightly larger padding, matching a tooltip bubble's usual proportions rather than a
    // status pill's. See the DIALOG case above for the same honest scope note this shares: real
    // anchored positioning next to a target is a `@view.Stack align=/valign=` composition job for
    // the .rin author, not something Tooltip does for itself.
    Rect measureTooltip(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 12);
        double pad = resolveSpacing(*s, "padding", 8);
        double w = std::min(measureTextWidth(text, fontSize) + pad*2, c.maxW);
        double h = std::min(fontSize*1.4 + pad*2, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- UI/UX Library Expansion: Kbd — a small bordered "key" box, same content-sized-box idea
    // as Badge/Tooltip just above, with proportions closer to a monospace keycap (a touch more
    // horizontal padding, minimum width so a single-character key like "K" still reads as a key
    // and not a sliver).
    Rect measureKbd(StrandPtr s, Constraints c) {
        std::string text = s->attrStr("text", "");
        double fontSize = resolveFontSize(*s, "size", 12);
        double hPad = 7, vPad = 3;
        double w = std::min(std::max(measureTextWidth(text, fontSize) + hPad*2, fontSize*1.8), c.maxW);
        double h = std::min(fontSize*1.5 + vPad*2, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // Rating: `max=` (default 5) equal-width star cells laid out side by side; `size=` sets each
    // star glyph's box (default 18), `gap=` the spacing between cells (default 2). Read-only by
    // design (see paintRating's doc comment in rin_indsin_paint.h) — the same "measures a box,
    // doesn't model interaction state" scope Progress/Slider already have.
    Rect measureRating(StrandPtr s, Constraints c) {
        int maxStars = (int)std::max(1.0, s->attrNum("max", 5));
        double gap = resolveSpacing(*s, "gap", 2);
        double starSize = std::min(s->attrNum("size", 18), std::min((c.maxW - (maxStars-1)*gap) / maxStars, c.maxH));
        starSize = std::max(starSize, 4.0);
        double w = std::min(maxStars*starSize + (maxStars-1)*gap, c.maxW);
        double h = std::min(starSize, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- Missing-components pass: Input/TextArea (§?) — a bordered field box sized around its
    // `value=`/`placeholder=` text (whichever is longer), with a sensible default height per kind
    // (`defaultH`: 40 for a single-line Input, 96 for a multi-line TextArea) unless height=
    // overrides it. There's no real caret/selection/multi-line-wrapping-while-typing model here —
    // this engine only computes the field's box, exactly the same design-time-preview scope
    // Image/Video/WebView already have (see measureMedia's doc comment above).
    Rect measureField(StrandPtr s, Constraints c, double defaultH) {
        std::string value = s->attrStr("value", ""), placeholder = s->attrStr("placeholder", "");
        double fontSize = resolveFontSize(*s, "size", 14);
        double contentW = measureTextWidth(value.empty() ? placeholder : value, fontSize);
        double w = s->attr("width") ? c.minW : std::min(std::max(120.0, contentW + 24.0), c.maxW);
        double h = s->attr("height") ? c.minH : std::min(defaultH, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // Calculator (ready-elements expansion): see the StrandKind::CALCULATOR case comment above
    // for why this branches on whether the .rin source gave it real children.
    Rect measureCalculator(StrandPtr s, Constraints c, double originX, double originY) {
        if (!s->children.empty()) {
            return layoutSingleChildBox(s, c, resolveSpacing(*s, "padding", 12) + s->attrNum("border", 0), originX, originY);
        }
        double w = std::min(s->attrNum("width", 280), c.maxW);
        double h = std::min(s->attrNum("height", 360), c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- UI/UX Library Expansion: Steps/Stepper --------------------------------------------
    // Steps lays its StepItem children left-to-right, each claiming an EQUAL share of the
    // available width (unlike an ordinary Row, which packs each child to its own content size) —
    // a stepper reads correctly only when every marker is evenly spaced, so the connector line
    // paintSteps() draws afterward (rin_indsin_paint.h) lines up center-to-center with each
    // circle regardless of how long that step's label= is.
    Rect layoutSteps(StrandPtr s, Constraints c, double originX, double originY) {
        double padding = resolveSpacing(*s, "padding", 0);
        int n = (int)s->children.size();
        double innerW = std::max(0.0, c.maxW - padding*2);
        double itemW = n > 0 ? innerW / n : innerW;
        double cursorX = 0, maxH = 0;
        for (auto& child : s->children) {
            Rect r = layout(child, {itemW, itemW, 0, c.maxH}, originX + padding + cursorX, originY + padding);
            cursorX += itemW; maxH = std::max(maxH, r.h);
        }
        double totalH = maxH + padding*2;
        return {0,0, std::min(innerW + padding*2, c.maxW), std::max(std::min(totalH,c.maxH), c.minH)};
    }
    // StepItem: a circular marker (markerSize=, default 28) stacked above its label= text —
    // measured as one box (Stack-style) rather than built from real child Strands, since its
    // marker/label pairing is fixed and never needs arbitrary nested content the way Card's does.
    Rect measureStepItem(StrandPtr s, Constraints c) {
        double circle = std::min(s->attrNum("markerSize", 28), std::min(c.maxW, c.maxH));
        double fontSize = resolveFontSize(*s, "size", 12);
        std::string label = s->attrStr("label", "");
        double labelW = measureTextWidth(label, fontSize);
        double gap = 6;
        double w = std::min(std::max(circle, labelW), c.maxW);
        double h = std::min(circle + gap + fontSize*1.3, c.maxH);
        return {0,0, std::max(w,c.minW), std::max(h,c.minH)};
    }

    // ---- UI/UX Library Expansion: Timeline/TimelineItem -------------------------------------
    // TimelineItem: an attribute-driven leaf (date=/title=/desc=), the same "measured as one
    // self-contained box, not built from real child Strands" design as StepItem above — a
    // vertical stack of however many of date=/title=/desc= are actually set, claiming the full
    // row width like a List/ListItem row does.
    Rect measureTimelineItem(StrandPtr s, Constraints c) {
        double dot = 12;
        double fontSize = resolveFontSize(*s, "size", 14);
        double lineH = fontSize*1.35;
        int lines = 1; // title= (always reserves at least one line, even if empty)
        if (!s->attrStr("date", "").empty()) lines++;
        if (!s->attrStr("desc", "").empty()) lines++;
        double h = std::min(std::max(dot, lines*lineH), c.maxH);
        return {0,0, c.maxW, std::max(h, c.minH)};
    }
};

} // namespace indsin

#include "rin_indsin_webconcepts.h" // تعريفات webPostLayout / webMinContentWidth (بعد تعريف Indsin)
