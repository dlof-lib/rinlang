// indsin/rin_indsin_eval.h — minimal expression evaluator for @view attribute values.
//
// This is intentionally NOT the full rin::Interpreter: view attributes only need literals,
// Warp-cell lookups, string concatenation, and (for event handlers like onTap=increment;)
// a captured-but-unevaluated function reference. Anything requiring full language semantics
// (loops, user functions, side effects) belongs to rin::Interpreter — see the note in
// rin_indsin_c_api.cpp for how a future IndsintimeRuntime would delegate to it for CallExpr.
#pragma once
#include "../rin_ast.h"
#include "../rin_common.h"
#include <string>
#include <unordered_map>
#include <variant>
#include <sstream>
#include <cctype>

namespace indsin {

// A resolved attribute value: either a number or a string (RIN's two literal kinds relevant here).
struct Value {
    enum class Kind { NUMBER, STRING } kind = Kind::STRING;
    double number = 0.0;
    std::string str;

    static Value num(double n) { Value v; v.kind = Kind::NUMBER; v.number = n; return v; }
    static Value txt(std::string s) { Value v; v.kind = Kind::STRING; v.str = std::move(s); return v; }

    double asNumber(double def = 0.0) const {
        if (kind == Kind::NUMBER) return number;
        try { size_t idx = 0; double v = std::stod(str, &idx); if (idx == str.size()) return v; } catch (...) {}
        return def;
    }
    std::string asString() const {
        if (kind == Kind::STRING) return str;
        std::ostringstream os; os << number; return os.str();
    }
    bool operator==(const Value& o) const {
        return kind == o.kind && number == o.number && str == o.str;
    }
};

// Warp scope: RIN's reactive state cells (see WarpStmt in rin_ast.h). Subscribers are tracked
// by the Fabric builder (rin_indsin_strand.h) — this struct only holds the current values.
struct WarpScope {
    std::unordered_map<std::string, Value> cells;
    bool has(const std::string& name) const { return cells.count(name) != 0; }
    Value get(const std::string& name) const {
        auto it = cells.find(name);
        return it != cells.end() ? it->second : Value::txt("{{" + name + "}}");
    }
    void set(const std::string& name, Value v) { cells[name] = std::move(v); }
};

// ---------------------------------------------------------------------------------------------
// جسر Rin الكامل (docs/indsin_rin.md): يجعل قيم خصائص @view تعتمد على لغة Rin نفسها.
//
// المُقيِّم المصغَّر أدناه يفهم الحرفيات + - * / وخلايا Warp والشرط الثلاثي. أي شيء آخر (مقارنات،
// && || !، %، فهرسة x[i]، x.member، x.method()، استدعاء دوال Rin الأصلية/المعرَّفة، Set/Array/Map...)
// كان يتحوّل إلى "<expr>" أو يُلصَق كنص. الآن إن وُجد مضيف (RinExprHost) يُفوَّض التعبير إلى مفسّر Rin
// الحقيقي بدلالات اللغة الكاملة؛ وبلا مضيف يبقى السلوك القديم حرفياً (توافق خلفي كامل).
// المضيف الفعلي (فوق rin::Interpreter) في rin_indsin_rinbridge.h؛ يُثبَّت لكل جلسة/تمريرة بـ ScopedRinHost.
// ---------------------------------------------------------------------------------------------
struct RinExprHost {
    virtual ~RinExprHost() = default;
    // متغيّر Rin عام (let/const/warp) غير دالة، باسم `name` وليس خلية warp؟ يملأ out ويعيد true.
    virtual bool variable(const std::string& name, const WarpScope& warp, Value& out) = 0;
    // هل `callee` دالة Rin (أصلية أو fun معرَّفة) مسموح بتقييمها كقيمة (ليست ذات أثر جانبي)؟
    virtual bool canCall(const std::string& callee, const WarpScope& warp) = 0;
    // يقيّم e بدلالات Rin الكاملة؛ يضيف أسماء المتغيرات الحرّة إلى reads. false = ممنوع/فشل (err يشرح).
    virtual bool eval(const rin::ExprPtr& e, const WarpScope& warp, std::vector<std::string>* reads,
                      Value& out, std::string* err) = 0;
};
inline RinExprHost*& currentRinHost() { static thread_local RinExprHost* h = nullptr; return h; }
struct ScopedRinHost {
    RinExprHost* prev;
    explicit ScopedRinHost(RinExprHost* h) : prev(currentRinHost()) { currentRinHost() = h; }
    ~ScopedRinHost() { currentRinHost() = prev; }
};
// "سياق قيمة": الاستدعاء f(x) يُحسَب قيمةً (لا وصف معالج حدث). يُفعَّل لكل الخصائص عدا on<Event>
// ولوسائط المعالجات/الـ actions.
inline bool& valueContextFlag() { static thread_local bool v = false; return v; }
struct ScopedValueContext {
    bool prev;
    explicit ScopedValueContext(bool on = true) : prev(valueContextFlag()) { valueContextFlag() = on; }
    ~ScopedValueContext() { valueContextFlag() = prev; }
};
// مفتاح معالج حدث (onTap/onLongPress/onChange...): on + حرف كبير.
inline bool isHandlerKey(const std::string& k) {
    return k.size() > 2 && k[0] == 'o' && k[1] == 'n' && std::isupper(static_cast<unsigned char>(k[2]));
}

// Evaluates the subset of rin::Expr that's meaningful inside a @view attribute value.
// `readNames` (optional) collects every Warp cell name that was actually read, so the
// Fabric builder can register this Strand as a subscriber (see buildFabric in rin_indsin_strand.h) —
// this is what lets a warp update find exactly the Strands that need re-resolution.
inline Value evalAttrExpr(const rin::ExprPtr& e, const WarpScope& warp, std::vector<std::string>* readNames = nullptr) {
    if (!e) return Value::txt("");

    if (auto lit = std::dynamic_pointer_cast<rin::LiteralExpr>(e)) {
        switch (lit->kind) {
            case rin::LiteralExpr::Kind::NUMBER: return Value::num(lit->number);
            case rin::LiteralExpr::Kind::STRING: return Value::txt(lit->str);
            case rin::LiteralExpr::Kind::BOOL:   return Value::txt(lit->boolean ? "true" : "false");
            case rin::LiteralExpr::Kind::NIL:    return Value::txt("");
        }
        return Value::txt("");
    }
    if (auto var = std::dynamic_pointer_cast<rin::VariableExpr>(e)) {
        if (readNames) readNames->push_back(var->name);
        if (warp.has(var->name)) return warp.get(var->name);
        // متغيّر Rin عام عادي (let/const) => قيمته الفعلية، لا اسمه (يتطلّب مضيفاً؛ الدوال تبقى أسماء معالجات).
        if (auto* host = currentRinHost()) { Value gv; if (host->variable(var->name, warp, gv)) return gv; }
        // Not a known Warp cell (e.g. a plain identifier used as an event-handler reference,
        // such as onTap=increment;) — keep it as its own name so it can be dispatched by Needle.
        return Value::txt(var->name);
    }
    if (auto bin = std::dynamic_pointer_cast<rin::BinaryExpr>(e)) {
        const bool basicOp = bin->op == rin::TokenType::PLUS || bin->op == rin::TokenType::MINUS ||
                             bin->op == rin::TokenType::STAR || bin->op == rin::TokenType::SLASH;
        if (!basicOp) { // مقارنات/== !=/% ...: كانت تُلصَق كنص خطأً؛ الآن بدلالات Rin الحقيقية
            if (auto* host = currentRinHost()) { Value out; if (host->eval(e, warp, readNames, out, nullptr)) return out; }
        }
        Value l = evalAttrExpr(bin->left, warp, readNames);
        Value r = evalAttrExpr(bin->right, warp, readNames);
        if (bin->op == rin::TokenType::PLUS) {
            if (l.kind == Value::Kind::NUMBER && r.kind == Value::Kind::NUMBER)
                return Value::num(l.number + r.number);
            return Value::txt(l.asString() + r.asString()); // string concatenation, RIN's '+' overload
        }
        if (bin->op == rin::TokenType::MINUS) return Value::num(l.asNumber() - r.asNumber());
        if (bin->op == rin::TokenType::STAR)  return Value::num(l.asNumber() * r.asNumber());
        if (bin->op == rin::TokenType::SLASH) return Value::num(r.asNumber()!=0 ? l.asNumber() / r.asNumber() : 0.0);
        return Value::txt(l.asString() + r.asString());
    }
    if (auto cond = std::dynamic_pointer_cast<rin::ConditionalExpr>(e)) {
        // Ternary in a @view attribute (background=pressed ? "#a" : "#b";). RIN's own
        // truthiness (rin_interpreter.cpp's isTruthy): a STRING is truthy unless empty or
        // literally "false"; a NUMBER is truthy unless exactly 0. Only the winning branch is
        // evaluated -- readNames still records reads from *both* the condition and whichever
        // branch actually ran, exactly like every other case above, so a Warp update to a cell
        // used only in the untaken branch still re-subscribes correctly next time it's taken.
        Value c = evalAttrExpr(cond->condition, warp, readNames);
        bool truthy = (c.kind == Value::Kind::NUMBER) ? (c.number != 0.0) : !(c.str.empty() || c.str == "false");
        return truthy ? evalAttrExpr(cond->whenTrue, warp, readNames)
                      : evalAttrExpr(cond->whenFalse, warp, readNames);
    }
    if (auto call = std::dynamic_pointer_cast<rin::CallExpr>(e)) {
        // سياق قيمة (text=upper(name); visible=len(items) > 0): الاستدعاء حساب فعلي بدالة Rin.
        if (valueContextFlag()) {
            if (auto* host = currentRinHost()) {
                Value out;
                if (host->canCall(call->callee, warp) && host->eval(e, warp, readNames, out, nullptr)) return out;
            }
        }
        // Event-handler attributes (onTap=increment(count);) are captured as a raw descriptor
        // string "increment(count)" rather than invoked here — Needle (input dispatch) matches
        // against this descriptor. Full arbitrary-function dispatch is future work (Appendix A).
        std::ostringstream os; os << call->callee << "(";
        for (size_t i = 0; i < call->args.size(); i++) {
            if (i) os << ",";
            if (auto v = std::dynamic_pointer_cast<rin::VariableExpr>(call->args[i])) os << v->name;
            else { ScopedValueContext vc(true); os << evalAttrExpr(call->args[i], warp, readNames).asString(); }
        }
        os << ")";
        return Value::txt(os.str());
    }
    // أي نوع تعبير آخر (unary/logical/index/member/method/array/map...): مفسّر Rin الحقيقي إن وُجد.
    if (auto* host = currentRinHost()) { Value out; if (host->eval(e, warp, readNames, out, nullptr)) return out; }
    // Fallback for any other expression kind (unary/logical/etc.): stringify defensively rather
    // than throw — a Snag-worthy attribute should degrade to a visible placeholder, not crash
    // the whole render pass (see rin_indsin_c_api.cpp's error containment).
    return Value::txt("<expr>");
}

// قيمة خاصية بمفتاحها: on<Event> => وصف معالج (السلوك القديم)، غير ذلك => سياق قيمة.
inline Value evalAttrExprKeyed(const std::string& key, const rin::ExprPtr& e, const WarpScope& warp,
                               std::vector<std::string>* readNames = nullptr) {
    if (isHandlerKey(key)) return evalAttrExpr(e, warp, readNames);
    ScopedValueContext vc(true);
    return evalAttrExpr(e, warp, readNames);
}
// وسيط معالج/action أو حقل كائن: دائماً قيمة.
inline Value evalAttrValue(const rin::ExprPtr& e, const WarpScope& warp, std::vector<std::string>* readNames = nullptr) {
    ScopedValueContext vc(true);
    return evalAttrExpr(e, warp, readNames);
}

} // namespace indsin
