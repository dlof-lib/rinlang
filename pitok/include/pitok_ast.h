#pragma once
// pitok_ast.h — شجرة بناء التركيب الخاصة بـ PITOK. هذا هو "الحد الفاصل" الفعلي بين
// لغة PITOK ومحرك التخطيط: أي واجهة أمامية (parser) — سواء كانت هذه أو أخرى مستقبلاً —
// يكفيها إنتاج هذه الأشجار ليعمل معها Loomtime دون تغيير.

#include <string>
#include <vector>
#include <memory>

namespace pitok {

// ---------- تعابير قيم السمات (attribute values) ----------
// يغطي ما يظهر فعليًا في عيّنات RinLang: أرقام، سلاسل، معرفات (مراجع لمتغيرات warp)،
// نداءات دوال بوسائط، وعمليات ثنائية (خصوصًا "+" لدمج/جمع نصوص وأرقام).

enum class ExprKind { Number, String, Identifier, Binary, Call };

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    ExprKind kind;
    int line = 0;

    // Number
    double numValue = 0.0;
    // String
    std::string strValue;
    // Identifier
    std::string name;
    // Binary
    std::string op; // "+" "-" "*" "/"
    ExprPtr left;
    ExprPtr right;
    // Call: name(args...)
    std::vector<ExprPtr> args;
};

inline ExprPtr makeNumber(double v, int line) {
    auto e = std::make_shared<Expr>(); e->kind = ExprKind::Number; e->numValue = v; e->line = line; return e;
}
inline ExprPtr makeString(std::string v, int line) {
    auto e = std::make_shared<Expr>(); e->kind = ExprKind::String; e->strValue = std::move(v); e->line = line; return e;
}
inline ExprPtr makeIdentifier(std::string n, int line) {
    auto e = std::make_shared<Expr>(); e->kind = ExprKind::Identifier; e->name = std::move(n); e->line = line; return e;
}
inline ExprPtr makeBinary(std::string op, ExprPtr l, ExprPtr r, int line) {
    auto e = std::make_shared<Expr>(); e->kind = ExprKind::Binary; e->op = std::move(op);
    e->left = std::move(l); e->right = std::move(r); e->line = line; return e;
}
inline ExprPtr makeCall(std::string n, std::vector<ExprPtr> args, int line) {
    auto e = std::make_shared<Expr>(); e->kind = ExprKind::Call; e->name = std::move(n);
    e->args = std::move(args); e->line = line; return e;
}

// ---------- سمة واحدة: key = expr; ----------
struct Attribute {
    std::string key;
    ExprPtr value;
    int line = 0;
};

// ---- سمة استجابة حدث: on.target.event = expr; ----
// من اللهجة الأقدم (@container/@element)، مثل: on.run.click=runCode();
struct OnBinding {
    std::string target; // "run"
    std::string event;  // "click"
    ExprPtr action;      // runCode()
    int line = 0;
};

// ---------- عقدة شجرة الواجهة ----------
// تمثّل @view.Kind=name، @loop=name، @element.Kind=name، و@container=name معًا
// (بوضع kind="__loop__" أو kind="__container__" للحالتين الخاصتين) حتى تبقى الشجرة نوعًا
// واحدًا موحّدًا يسهل المشي عليه (visit) بلا تفرّع أنواع — هذا هو "التوحيد" فعليًا: نحو
// واحد وAST واحد للهجتين اللتين كانتا منفصلتين في المصدر الأصلي.
struct ViewNode;
using ViewNodePtr = std::shared_ptr<ViewNode>;

struct ViewNode {
    std::string kind;   // "Column" | "Row" | "Card" | "button" | ... أو "__loop__"/"__container__"
    std::string name;   // المعرّف بعد "="
    std::vector<Attribute> attributes;
    std::vector<OnBinding> bindings; // on.x.y=expr; (لهجة @container/@element فقط)
    std::vector<ViewNodePtr> children;
    int line = 0;

    const Attribute* findAttr(const std::string& key) const {
        for (auto& a : attributes) if (a.key == key) return &a;
        return nullptr;
    }
};

// ---------- إعلان warp (حالة تفاعلية على مستوى الملف) ----------
struct WarpDecl {
    std::string name;
    ExprPtr initValue;
    int line = 0;
};

// ---------- جذر البرنامج ----------
struct Program {
    std::vector<WarpDecl> warps;
    std::vector<ViewNodePtr> roots; // عادة عنصر @loop واحد يحوي شجرة @view كاملة
};

} // namespace pitok
