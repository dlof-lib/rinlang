// indsin/rin_indsin_rinbridge.h — يجعل indsin يعتمد على لغة Rin نفسها (docs/indsin_rin.md)
//
// InterpreterExprHost ينفّذ RinExprHost (rin_indsin_eval.h) فوق rin::Interpreter الحقيقي:
//   • متغيّرات Rin العامة (let/const) صالحة كقيم خصائص:        text=title;  visible=loggedIn;
//   • أي تعبير Rin صالح:                                          visible=count > 3 && !loading;
//   • دوال Rin الأصلية والمعرَّفة:                                 text=upper(name);  text=len(items) + " عنصر";
//   • فهرسة/عضو/طريقة:                                            text=user.name;  text=items[0];  text=tags.size();
//   • Set/Array/Map حقيقية داخل التعبير:                          visible=Set(roles).has("admin");
// القاعدة الأمنية: التعبير داخل خاصية يُحسَب *نقياً* — تُرفض (فيعود السلوك القديم "<expr>") كل تعبير
// فيه إسناد/تعديل/lambda أو استدعاء ذو أثر جانبي (شبكة/ملفات/طباعة/تعديل مصفوفة أو Set في المكان...).
// كل ما يُسمح به يبقى خاضعاً لميزانية التنفيذ التي يضبطها المضيف (setExecutionBudget).
#pragma once
#include "rin_indsin_eval.h"
#include "../rin_interpreter.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace indsin {

inline Value rinToIndsinValue(const rin::Value& v) {
    switch (v.type) {
        case rin::Value::Type::NUMBER: return Value::num(v.number);
        case rin::Value::Type::BOOL:   return Value::txt(v.boolean ? "true" : "false");
        case rin::Value::Type::STRING: return Value::txt(v.str);
        case rin::Value::Type::NIL:    return Value::txt("");
        default:                       return Value::txt(v.toDisplayString());
    }
}
// خلايا Warp تخزّن المنطقي كنص "true"/"false" (انظر rinValueToIndsin في rin_indsin_needle.h).
inline rin::Value indsinToRinValue(const Value& v) {
    if (v.kind == Value::Kind::NUMBER) return rin::Value::num(v.number);
    if (v.str == "true" || v.str == "false") return rin::Value::boolean_(v.str == "true");
    return rin::Value::string(v.str);
}

namespace rinbridge {

// ---- فحص النقاء ----
inline bool hasPrefix(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

inline bool isImpureCallee(const std::string& name) {
    static const std::unordered_set<std::string> exact = {
        "push", "pop", "shift", "unshift", "splice", "insert", "remove", "clear", "sort", "reverse", "shuffle",
        "input", "inputNumber", "call", "callApi", "run", "spawn", "sleep", "exit", "quit", "print", "println",
        "readFile", "writeFile", "deleteFile", "deleteDoc", "updateDoc", "apiCall", "apiDelete", "apiGet",
        "apiHeader", "apiPatch", "apiPost", "apiPut", "apiRegister", "cacheClear", "cacheDelete", "clearChat",
        "openChat", "sendMessage", "setChatTyping", "setField", "setPath", "setState", "sqlDelete", "sqlUpdate",
        "logClear", "logSave", "fetchIcon", "fetchImage", "clcContainerOpen", "httpSetTimeout",
        "indsin.useTheme", "indsin.defineTheme", "indsin.registerTheme",
    };
    if (exact.count(name)) return true;
    static const char* prefixes[] = {"http", "net.", "fs.", "make.", "sec.", "cpp.", "Env.", "pkg.", "automation.",
                                     "log.", "container.", "oop.", "sql", "cache", "Set.add", "Set.remove",
                                     "Set.clear", "Set.pop", "Set.retain"};
    for (const char* p : prefixes) if (hasPrefix(name, p)) return true;
    return false;
}
inline bool isImpureMethod(const std::string& m) {
    static const std::unordered_set<std::string> bad = {
        "push", "pop", "shift", "unshift", "splice", "insert", "remove", "clear", "sort", "reverse", "shuffle",
        "add", "addAll", "removeAll", "retain", "fill", "set", "update", "delete", "append", "write", "send",
        "emit", "save", "run", "start", "stop",
    };
    return bad.count(m) != 0;
}

// يجمع أسماء المتغيرات الحرّة في e ويرفض أي تعبير غير نقي. reason يشرح الرفض.
inline bool analyze(const rin::ExprPtr& e, const std::function<bool(const std::string&)>& isNativeName,
                    std::vector<std::string>& vars, std::string& reason) {
    if (!e) return true;
    switch (e->exprKind) {
        case rin::ExprKind::Literal: return true;
        case rin::ExprKind::Variable:
            vars.push_back(std::static_pointer_cast<rin::VariableExpr>(e)->name);
            return true;
        case rin::ExprKind::Binary: {
            auto x = std::static_pointer_cast<rin::BinaryExpr>(e);
            return analyze(x->left, isNativeName, vars, reason) && analyze(x->right, isNativeName, vars, reason);
        }
        case rin::ExprKind::Logical: {
            auto x = std::static_pointer_cast<rin::LogicalExpr>(e);
            return analyze(x->left, isNativeName, vars, reason) && analyze(x->right, isNativeName, vars, reason);
        }
        case rin::ExprKind::Conditional: {
            auto x = std::static_pointer_cast<rin::ConditionalExpr>(e);
            return analyze(x->condition, isNativeName, vars, reason) && analyze(x->whenTrue, isNativeName, vars, reason) &&
                   analyze(x->whenFalse, isNativeName, vars, reason);
        }
        case rin::ExprKind::Unary:
            return analyze(std::static_pointer_cast<rin::UnaryExpr>(e)->right, isNativeName, vars, reason);
        case rin::ExprKind::Call: {
            auto x = std::static_pointer_cast<rin::CallExpr>(e);
            if (x->isPipelineNode || x->isPipelineRoot) { reason = "pipeline `|>` is not allowed in a view attribute"; return false; }
            const std::string& c = x->callee;
            auto dot = c.find('.');
            if (isNativeName(c)) {
                if (isImpureCallee(c)) { reason = "`" + c + "` has side effects and cannot run inside a view attribute"; return false; }
            } else if (dot != std::string::npos) { // obj.method(...): الجذر متغيّر، والطريقة تُفحَص
                std::string m = c.substr(c.rfind('.') + 1);
                if (isImpureMethod(m)) { reason = "method `" + m + "` changes its object and cannot run inside a view attribute"; return false; }
                vars.push_back(c.substr(0, dot));
            } else if (isImpureCallee(c)) {
                reason = "`" + c + "` has side effects and cannot run inside a view attribute"; return false;
            }
            for (auto& a : x->args) if (!analyze(a, isNativeName, vars, reason)) return false;
            return true;
        }
        case rin::ExprKind::Array: {
            for (auto& a : std::static_pointer_cast<rin::ArrayExpr>(e)->elements) if (!analyze(a, isNativeName, vars, reason)) return false;
            return true;
        }
        case rin::ExprKind::Map: {
            for (auto& en : std::static_pointer_cast<rin::MapExpr>(e)->entries)
                if (!analyze(en.key, isNativeName, vars, reason) || !analyze(en.value, isNativeName, vars, reason)) return false;
            return true;
        }
        case rin::ExprKind::Index: {
            auto x = std::static_pointer_cast<rin::IndexExpr>(e);
            return analyze(x->object, isNativeName, vars, reason) && analyze(x->index, isNativeName, vars, reason);
        }
        case rin::ExprKind::Get:
            return analyze(std::static_pointer_cast<rin::GetExpr>(e)->object, isNativeName, vars, reason);
        case rin::ExprKind::MethodCall: {
            auto x = std::static_pointer_cast<rin::MethodCallExpr>(e);
            if (isImpureMethod(x->method)) { reason = "method `" + x->method + "` changes its object and cannot run inside a view attribute"; return false; }
            if (!analyze(x->object, isNativeName, vars, reason)) return false;
            for (auto& a : x->args) if (!analyze(a, isNativeName, vars, reason)) return false;
            return true;
        }
        case rin::ExprKind::CallValue: {
            auto x = std::static_pointer_cast<rin::CallValueExpr>(e);
            if (!analyze(x->callee, isNativeName, vars, reason)) return false;
            for (auto& a : x->args) if (!analyze(a, isNativeName, vars, reason)) return false;
            return true;
        }
        default:
            reason = "this expression kind (assignment / lambda / pattern / goal) is not allowed in a view attribute";
            return false;
    }
}

} // namespace rinbridge

class InterpreterExprHost : public RinExprHost {
public:
    using Getter = std::function<rin::Interpreter*()>;
    InterpreterExprHost(Getter g, std::string container = "") : get_(std::move(g)), container_(std::move(container)) {}
    InterpreterExprHost(rin::Interpreter& i, std::string container = "")
        : get_([p = &i] { return p; }), container_(std::move(container)) {}

    std::string lastError;

    bool variable(const std::string& name, const WarpScope&, Value& out) override {
        auto* in = get_(); if (!in) return false;
        rin::Value v;
        if (!in->lookupGlobal(name, v, container_)) return false;
        if (v.type == rin::Value::Type::FUNCTION) return false; // اسم معالج حدث، لا قيمة
        out = rinToIndsinValue(v);
        return true;
    }

    bool canCall(const std::string& callee, const WarpScope& warp) override {
        auto* in = get_(); if (!in) return false;
        if (in->isCallableName(callee)) return !rinbridge::isImpureCallee(callee);
        auto dot = callee.find('.');
        if (dot == std::string::npos) return false;
        std::string root = callee.substr(0, dot);
        if (warp.has(root)) return true;
        rin::Value v;
        return in->lookupGlobal(root, v, container_) && v.type != rin::Value::Type::FUNCTION;
    }

    bool eval(const rin::ExprPtr& e, const WarpScope& warp, std::vector<std::string>* reads, Value& out,
              std::string* err) override {
        auto* in = get_(); if (!in) return false;
        std::vector<std::string> vars;
        std::string reason;
        auto isNative = [in](const std::string& n) { return in->isCallableName(n); };
        if (!rinbridge::analyze(e, isNative, vars, reason)) { lastError = reason; if (err) *err = reason; return false; }
        if (reads) for (auto& n : vars) reads->push_back(n);

        std::unordered_map<std::string, rin::Value> locals;
        for (auto& n : vars) {
            if (!warp.has(n) || locals.count(n)) continue;
            Value cell = warp.get(n);
            rin::Value real;
            // خلية warp أصلها مصفوفة/قاموس/Set: نُمرّر القيمة الحقيقية (لا نصّها) ما دامت لم تتغيّر منذ البذر.
            if (in->lookupGlobal(n, real, container_) &&
                (real.type == rin::Value::Type::ARRAY || real.type == rin::Value::Type::MAP ||
                 real.type == rin::Value::Type::SET || real.type == rin::Value::Type::INSTANCE) &&
                real.toDisplayString() == cell.asString())
                locals[n] = real;
            else
                locals[n] = indsinToRinValue(cell);
        }
        rin::Value rv;
        std::string ee;
        if (!in->evalExpression(e, locals, rv, ee, container_)) { lastError = ee; if (err) *err = ee; return false; }
        out = rinToIndsinValue(rv);
        return true;
    }

private:
    Getter get_;
    std::string container_;
};

} // namespace indsin
