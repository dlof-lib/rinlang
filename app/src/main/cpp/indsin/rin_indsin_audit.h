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
//   LAYOUT-001 warning عنصر يتجاوز عرض الشاشة أفقياً (عدا الطبقات المنبثقة)
//
// الدرجة (score): 100 - 10*errors - 3*warnings (حدّها الأدنى 0).
#pragma once
#include "rin_indsin_paint.h"
#include "rin_indsin_query.h"
#include "rin_indsin_system.h"
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

    walkFabric(root, [&](const StrandPtr& sp, int depth, const Strand* parent) {
        const Strand& s = *sp;
        r.nodes++;
        bool decorative = s.attrStr("decorative", "false") == "true";

        // A11Y-001: الاسم المُتاح
        if (isInteractiveKind(s.kind) && !decorative && accessibleName(s).empty()) {
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
        if (s.kind == StrandKind::TEXT && s.children.empty() && s.attrStr("text", "").empty()) {
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
