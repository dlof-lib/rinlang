// indsin/rin_indsin_audit.h — مدقّق الجودة وإمكانية الوصول (Audit Engine) لشجرة الـ Fabric.
//
// يفحص شجرة Strand **بعد التخطيط** (الأبعاد الفعلية معروفة) ويُخرج قائمة مشكلات مرتّبة بأكواد ثابتة،
// قابلة للاستهلاك من المحرّر (RinStudio) ومن الـ CI (يفشل البناء عند وجود errors).
//
// القواعد (الأكواد ثابتة ولا يُعاد استخدامها):
//   A11Y-001  error    عنصر تفاعلي بلا اسم مُتاح للقارئات (label=/text=/placeholder=/a11y_label=)
//   A11Y-002  error    Image بلا alt= أو a11y_label= (ما لم يكن decorative=true)
//   A11Y-003  warning  هدف لمس أصغر من 44px في أحد البُعدين
//   A11Y-004  error    تباين نص أقل من WCAG AA (4.5:1، أو 3:1 للنص الكبير >= 24px أو عريض >= 19px)
//   A11Y-005  warning  Dialog بلا اسم (title=/a11y_label=/label=)
//   STRUCT-001 warning  اسمان متطابقان بين الإخوة (يُنتجان نفس الـ StrandId ويفسدان hot-reload)
//   STRUCT-002 warning  Text فارغ بلا أبناء
//   STRUCT-003 warning  عمق تداخل أكبر من 12
//   A11Y-006  warning  وسم لغة غير صالح (lang=) — يجب أن يكون BCP-47 مثل ar أو en-US أو zh-Hans-CN
//   A11Y-007  warning  لا لغة معلنة في الشاشة رغم وجود نص (أضف lang= على الجذر)
//   A11Y-008  warning  ترتيب القراءة يخالف الترتيب البصري (order=/row-reverse) — أضف reading_order=
//   A11Y-009  warning  قفزة في مستويات العناوين (h1 ثم h3)
//   A11Y-010  error    aria_labelledby/aria_describedby يشير إلى اسم عنصر غير موجود
//   A11Y-011  error    عنصر تفاعلي مع aria_hidden=true (يختفي عن القارئ وهو قابل للتركيز)
//   A11Y-012  warning  قيمة aria_live غير صالحة (off|polite|assertive)
//   TYPO-001  warning  خط مخصّص فشل تحميله (يُستعمل الاحتياطي)
//   TYPO-002  warning  خاصية طباعية غير صالحة (font_feature/font_variation/font_variant/text_shadow/…)
//   TXTFX-001 warning  مسبق/تأثير حركة نص غير معروف
//   LAYOUT-001 warning عنصر يتجاوز عرض الشاشة أفقياً (عدا الطبقات المنبثقة)
//
// الدرجة (score): 100 - 10*errors - 3*warnings (حدّها الأدنى 0).
#pragma once
#include "rin_indsin_paint.h"
#include "rin_indsin_query.h"
#include "rin_indsin_system.h"
#include "rin_indsin_a11y.h"
#include "rin_indsin_typography.h"
#include "rin_indsin_textfx.h"
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace indsin {

struct AuditIssue {
    std::string code;
    std::string severity; // "error" | "warning"
    StrandId strandId = 0;
    std::string kind, name, path, message, hint;
    int line = 0;
};
struct AuditReport {
    std::vector<AuditIssue> issues;
    int nodes = 0;
    int errors() const { int n = 0; for (auto& i : issues) if (i.severity == "error") n++; return n; }
    int warnings() const { int n = 0; for (auto& i : issues) if (i.severity == "warning") n++; return n; }
    int score() const { int s = 100 - 10 * errors() - 3 * warnings(); return s < 0 ? 0 : s; }
    bool passed() const { return errors() == 0; }
    int count(const std::string& code) const { int n = 0; for (auto& i : issues) if (i.code == code) n++; return n; }
};

struct AuditOptions {
    double screenWidth = 0;          // 0 => يُستنتج من هندسة الجذر
    double minTouchTarget = kMinTouchTarget;
    double minTextContrast = 4.5;
    double minLargeTextContrast = 3.0;
    int maxDepth = 12;
};

namespace audit_detail {

// يُرسم له تعبئة خاصة به؟ (وإلا فهو شفاف ويُستخدم لون ما خلفه).
inline bool paintsOwnFill(StrandKind k) {
    switch (k) {
        case StrandKind::CARD: case StrandKind::BOX: case StrandKind::DIALOG: case StrandKind::OBJECT:
        case StrandKind::BANNER: case StrandKind::HEADER: case StrandKind::TOPBAR: case StrandKind::BOTTOMBAR:
        case StrandKind::DRAWER: case StrandKind::MENU: case StrandKind::TOOLTIP:
            return true;
        default: return false;
    }
}
// الخلفية الفعلية خلف `target`: أقرب سلف له bg=/background=/tone= صريح، أو نوع يرسم تعبئة (Card...)،
// وإلا خلفية الثيم. ألوان شفافة تُمزَج فوق خلفية الثيم.
inline Color effectiveBackground(const StrandPtr& root, const Strand* target) {
    std::vector<const Strand*> path;
    const Theme& th = themeRegistry().active();
    Color base = th.background;
    if (!pathTo(root, target, path)) return base;
    for (int i = (int)path.size() - 2; i >= 0; i--) { // من الأب الأقرب صعوداً
        const Strand* a = path[i];
        Color c;
        bool found = false;
        StrandPtr tmp(const_cast<Strand*>(a), [](Strand*) {}); // غلاف غير مالك لاستخدام resolveColorAttr
        if (auto tone = a->attr("tone")) {
            if (tone->kind == Value::Kind::STRING && resolveSemanticColor(tone->str, c)) found = true;
        }
        if (!found && resolveColorAttr(tmp, "bg", c)) found = true;
        if (!found && resolveColorAttr(tmp, "background", c)) found = true;
        if (!found && paintsOwnFill(a->kind)) { c = resolveColor(tmp); found = true; }
        if (found) return c.a == 255 ? c : rincolor::alphaBlend(c, base);
    }
    return base;
}

inline bool isLargeText(const Strand& s) {
    double size = resolveFontSize(s, "size", 14);
    std::string weight = s.attrStr("weight", "");
    bool bold = s.attrStr("bold", "false") == "true" || weight == "bold" || weight == "semibold";
    return size >= 24.0 || (bold && size >= 18.66);
}

inline void add(AuditReport& r, const StrandPtr& root, const Strand& s, const char* code, const char* sev,
                std::string msg, std::string hint) {
    AuditIssue i;
    i.code = code; i.severity = sev; i.strandId = s.id; i.kind = strandKindName(s.kind); i.name = s.name;
    i.path = pathString(root, &s); i.message = std::move(msg); i.hint = std::move(hint); i.line = s.sourceLine;
    r.issues.push_back(std::move(i));
}

} // namespace audit_detail

inline AuditReport auditFabric(const StrandPtr& root, const AuditOptions& opt = AuditOptions{}) {
    using namespace audit_detail;
    AuditReport r;
    if (!root) return r;
    double screenW = opt.screenWidth > 0 ? opt.screenWidth : root->geometry.w;
    int lastHeading = 0; bool anyText = false;
    walkFabric(root, [&](const StrandPtr& sp, int, const Strand*) { if (sp->kind == StrandKind::TEXT && !sp->attrStr("text", "").empty()) anyText = true; });
    if (anyText && a11y::rootLangOf(root).empty())
        add(r, root, *root, "A11Y-007", "warning", "no language declared (lang=)", "set lang=\"ar\" (or en, …) on the root so screen readers pick the right voice");

    walkFabric(root, [&](const StrandPtr& sp, int depth, const Strand* parent) {
        const Strand& s = *sp;
        r.nodes++;
        bool decorative = s.attrStr("decorative", "false") == "true";

        // A11Y-001: الاسم المُتاح
        if (isInteractiveKind(s.kind) && !decorative && a11y::nameOf(root, s).empty()) {
            add(r, root, s, "A11Y-001", "error",
                std::string(strandKindName(s.kind)) + " has no accessible name",
                "add label=/text=/placeholder= (or a11y_label=) so screen readers can announce it");
        }
        // A11Y-002: alt للصور
        if (s.kind == StrandKind::IMAGE && !decorative && s.attrStr("alt", "").empty() && s.attrStr("a11y_label", "").empty()) {
            add(r, root, s, "A11Y-002", "error", "Image has no alternative text",
                "add alt=\"...\" (or decorative=true if it carries no meaning)");
        }
        // A11Y-003: هدف اللمس
        if (isInteractiveKind(s.kind) && s.kind != StrandKind::LINK && s.geometry.w > 0 && s.geometry.h > 0 &&
            (s.geometry.w + 1e-6 < opt.minTouchTarget || s.geometry.h + 1e-6 < opt.minTouchTarget)) {
            char buf[120];
            std::snprintf(buf, sizeof(buf), "touch target %.0fx%.0f is smaller than %.0fpx", s.geometry.w, s.geometry.h, opt.minTouchTarget);
            add(r, root, s, "A11Y-003", "warning", buf, "increase width=/height= or padding= (recommended 48px)");
        }
        // A11Y-004: تباين النص
        bool textual = (s.kind == StrandKind::TEXT || s.kind == StrandKind::LINK) && !s.attrStr("text", "").empty();
        if (textual) {
            StrandPtr self = sp;
            Color fg = resolveColor(self);
            Color bg = effectiveBackground(root, &s);
            if (fg.a != 255) fg = rincolor::alphaBlend(fg, bg);
            double need = isLargeText(s) ? opt.minLargeTextContrast : opt.minTextContrast;
            double ratio = rincolor::contrastRatio(fg, bg);
            if (ratio + 1e-9 < need) {
                char buf[140];
                std::snprintf(buf, sizeof(buf), "text contrast %.2f:1 is below %.1f:1 (%s on %s)", ratio, need,
                              rincolor::toHex6(fg).c_str(), rincolor::toHex6(bg).c_str());
                Color fix = ensureContrast(fg, bg, need);
                add(r, root, s, "A11Y-004", "error", buf, "use a color such as " + rincolor::toHex6(fix) + " or tone=\"text\"");
            }
        }
        // A11Y-005: Dialog
        if (s.kind == StrandKind::DIALOG && s.attrStr("title", "").empty() && s.attrStr("a11y_label", "").empty() &&
            s.attrStr("label", "").empty()) {
            add(r, root, s, "A11Y-005", "warning", "Dialog has no title or accessible label",
                "add title=\"...\" so assistive tech can announce the dialog");
        }
        // STRUCT-002
        if (s.kind == StrandKind::TEXT && s.children.empty() && s.attrStr("text", "").empty() && !s.attr("text_rotator")) {
            add(r, root, s, "STRUCT-002", "warning", "Text is empty", "set text=\"...\" or remove the element");
        }
        // STRUCT-003 (مرة واحدة عند أول تجاوز لكل فرع)
        if (depth == opt.maxDepth + 1) {
            add(r, root, s, "STRUCT-003", "warning", "nesting deeper than " + std::to_string(opt.maxDepth) + " levels",
                "flatten the tree or extract a @container");
        }
        // LAYOUT-001
        if (parent && screenW > 0 && !isOverlayKind(s.kind) && !isOverlayKind(parent->kind) &&
            s.geometry.w > 0 && s.geometry.x + s.geometry.w > screenW + 1.0) {
            char buf[120];
            std::snprintf(buf, sizeof(buf), "extends %.0fpx beyond the %.0fpx-wide screen",
                          s.geometry.x + s.geometry.w - screenW, screenW);
            add(r, root, s, "LAYOUT-001", "warning", buf, "use sizing=\"fill\", a Wrap, or responsive width_<bp>= overrides");
        }
        // A11Y-006: وسم اللغة
        for (const char* lk : {"lang", "locale", "language_tag"}) {
            if (s.kind == StrandKind::CODE_EDITOR) break;
            std::string lv = typo::trim(s.attrStr(lk, ""));
            if (!lv.empty() && typo::normalizeLangTag(typo::unquote(lv)).empty())
                add(r, root, s, "A11Y-006", "warning", "invalid language tag '" + lv + "'", "use a BCP-47 tag such as ar, en-US or zh-Hans-CN");
        }
        // A11Y-008: ترتيب القراءة مقابل البصري
        if (a11y::visualOrderDiffers(s) && s.attrStr("reading_order", "").empty()) {
            bool reordered = false; for (auto& c : s.children) if (c->attr("order")) reordered = true;
            std::string fl = webLower(s.attrStr("flex_direction", s.attrStr("flow", "")));
            if (reordered || fl.find("reverse") != std::string::npos)
                add(r, root, s, "A11Y-008", "warning", "visual order differs from reading (source) order",
                    "add reading_order=\"visual\" on this container or reading_order=N on its children");
        }
        // A11Y-009: مستويات العناوين
        { a11y::Semantic sm = a11y::semanticOf(s);
          if (sm.level > 0) {
              if (lastHeading > 0 && sm.level > lastHeading + 1)
                  add(r, root, s, "A11Y-009", "warning", "heading level jumps from " + std::to_string(lastHeading) + " to " + std::to_string(sm.level), "do not skip heading levels");
              lastHeading = sm.level;
          } }
        // A11Y-010/011/012: ARIA
        for (const char* rk : {"aria_labelledby", "aria_describedby"})
            for (auto& ref : a11y::idRefs(s, rk))
                if (!a11y::byName(root, ref)) add(r, root, s, "A11Y-010", "error", std::string(rk) + " references missing element '" + ref + "'", "give the target an id: <Text#" + ref + ">");
        if (isInteractiveKind(s.kind) && webTruthy(s.attr("aria_hidden")))
            add(r, root, s, "A11Y-011", "error", std::string(strandKindName(s.kind)) + " is aria_hidden but interactive", "remove aria_hidden or disable the control");
        if (const Value* lv = s.attr("aria_live")) { std::string t = webLower(lv->asString()); if (t != "off" && t != "polite" && t != "assertive") add(r, root, s, "A11Y-012", "warning", "aria_live must be off|polite|assertive", "use polite for status updates"); }
        // TYPO-001/002: الخطوط والطباعة (بلا وراثة: يُفحص ما كُتب على العنصر نفسه)
        { typo::AttrScope sc; sc.self = &s; typo::TextTypography tp = typo::resolveTypography(sc);
          if (tp.custom && tp.status == "error") add(r, root, s, "TYPO-001", "warning", "custom font '" + tp.customFamily + "' failed to load", "check src= of <Font#" + tp.customFamily + ">; the fallback font is used");
          for (auto& w : tp.warnings) add(r, root, s, "TYPO-002", "warning", w, "see docs/indsin_typography_motion_a11y.md"); }
        // TXTFX-001: مسبقات حركة النص
        if (const Value* av = s.attr("text_animation"))
            for (auto& w : typo::words(av->asString()))
                if (!textfx::findPreset(w)) add(r, root, s, "TXTFX-001", "warning", "unknown text_animation preset '" + w + "'", "see docs/indsin_typography_motion_a11y.md for the list");
        // STRUCT-001: أسماء الإخوة
        if (!s.children.empty()) {
            std::set<std::string> seen;
            for (auto& c : s.children) {
                if (c->name.empty()) continue;
                if (!seen.insert(c->name).second) {
                    add(r, root, *c, "STRUCT-001", "warning", "duplicate sibling name '" + c->name + "'",
                        "sibling names derive the StrandId; make each one unique");
                }
            }
        }
    });
    return r;
}

inline std::string auditReportToJson(const AuditReport& r) {
    std::ostringstream os;
    os << "{\"ok\":" << (r.passed() ? "true" : "false") << ",\"score\":" << r.score()
       << ",\"nodes\":" << r.nodes << ",\"errors\":" << r.errors() << ",\"warnings\":" << r.warnings() << ",\"issues\":[";
    for (size_t i = 0; i < r.issues.size(); i++) {
        auto& is = r.issues[i];
        if (i) os << ",";
        os << "{\"code\":\"" << is.code << "\",\"severity\":\"" << is.severity << "\",\"strandId\":" << is.strandId
           << ",\"kind\":\"" << jsonEscape(is.kind) << "\",\"name\":\"" << jsonEscape(is.name) << "\",\"path\":\""
           << jsonEscape(is.path) << "\",\"line\":" << is.line << ",\"message\":\"" << jsonEscape(is.message)
           << "\",\"hint\":\"" << jsonEscape(is.hint) << "\"}";
    }
    os << "]}";
    return os.str();
}

} // namespace indsin
