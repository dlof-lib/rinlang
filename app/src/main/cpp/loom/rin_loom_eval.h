// loom/rin_loom_eval.h — minimal expression evaluator for @view attribute values.
//
// This is intentionally NOT the full rin::Interpreter: view attributes only need literals,
// Warp-cell lookups, string concatenation, and (for event handlers like onTap=increment;)
// a captured-but-unevaluated function reference. Anything requiring full language semantics
// (loops, user functions, side effects) belongs to rin::Interpreter — see the note in
// rin_loom_c_api.cpp for how a future LoomtimeRuntime would delegate to it for CallExpr.
#pragma once
#include "../rin_ast.h"
#include "../rin_common.h"
#include <string>
#include <unordered_map>
#include <variant>
#include <sstream>

namespace loom {

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
// by the Fabric builder (rin_loom_strand.h) — this struct only holds the current values.
struct WarpScope {
    std::unordered_map<std::string, Value> cells;
    bool has(const std::string& name) const { return cells.count(name) != 0; }
    Value get(const std::string& name) const {
        auto it = cells.find(name);
        return it != cells.end() ? it->second : Value::txt("{{" + name + "}}");
    }
    void set(const std::string& name, Value v) { cells[name] = std::move(v); }
};

// Evaluates the subset of rin::Expr that's meaningful inside a @view attribute value.
// `readNames` (optional) collects every Warp cell name that was actually read, so the
// Fabric builder can register this Strand as a subscriber (see buildFabric in rin_loom_strand.h) —
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
        // Not a known Warp cell (e.g. a plain identifier used as an event-handler reference,
        // such as onTap=increment;) — keep it as its own name so it can be dispatched by Needle.
        return Value::txt(var->name);
    }
    if (auto bin = std::dynamic_pointer_cast<rin::BinaryExpr>(e)) {
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
    if (auto call = std::dynamic_pointer_cast<rin::CallExpr>(e)) {
        // Event-handler attributes (onTap=increment(count);) are captured as a raw descriptor
        // string "increment(count)" rather than invoked here — Needle (input dispatch) matches
        // against this descriptor. Full arbitrary-function dispatch is future work (Appendix A).
        std::ostringstream os; os << call->callee << "(";
        for (size_t i = 0; i < call->args.size(); i++) {
            if (i) os << ",";
            if (auto v = std::dynamic_pointer_cast<rin::VariableExpr>(call->args[i])) os << v->name;
            else os << evalAttrExpr(call->args[i], warp, readNames).asString();
        }
        os << ")";
        return Value::txt(os.str());
    }
    // Fallback for any other expression kind (unary/logical/etc.): stringify defensively rather
    // than throw — a Snag-worthy attribute should degrade to a visible placeholder, not crash
    // the whole render pass (see rin_loom_c_api.cpp's error containment).
    return Value::txt("<expr>");
}

} // namespace loom
