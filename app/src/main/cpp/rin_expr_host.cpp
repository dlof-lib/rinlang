// ============================================================================
//  rin_expr_host.cpp — واجهة التقييم المعزول لمحرّك indsin (Indsin <-> Rin)
// ----------------------------------------------------------------------------
//  كان indsin يقيّم قيم خصائص @view بمُقيِّم مصغَّر (indsin::evalAttrExpr: أرقام/نصوص + - * /). هذه
//  الدوال تتيح له تفويض أي تعبير Rin آخر إلى المفسّر الحقيقي: عوامل المقارنة والمنطق، النفي، الفهرسة،
//  `.member`، استدعاء الدوال الأصلية و`fun` المعرَّفة، Set/Array/Map ... بدلالات اللغة نفسها.
//  يُضمَّن في نهاية rin_interpreter.cpp (مثل rin_set.cpp) فلا تغيير في ملفات البناء.
// ============================================================================
#include "rin_interpreter.h"

namespace rin {

bool Interpreter::lookupGlobal(const std::string& name, Value& out, const std::string& containerName) const {
    if (!containerName.empty()) {
        auto it = containers.find(containerName);
        if (it != containers.end() && it->second) {
            auto v = it->second->values.find(name);
            if (v != it->second->values.end()) { out = v->second; return true; }
        }
    }
    if (!globals) return false;
    auto v = globals->values.find(name);
    if (v == globals->values.end()) return false;
    out = v->second;
    return true;
}

bool Interpreter::isCallableName(const std::string& name) const {
    if (natives.find(name) != natives.end()) return true;
    if (globals) {
        auto v = globals->values.find(name);
        if (v != globals->values.end() && v->second.type == Value::Type::FUNCTION) return true;
    }
    return false;
}

bool Interpreter::evalExpression(const ExprPtr& e, const std::unordered_map<std::string, Value>& locals, Value& out,
                                 std::string& err, const std::string& containerName) {
    if (!e) { err = "empty expression"; return false; }
    EnvPtr parent = globals;
    if (!containerName.empty()) {
        auto it = containers.find(containerName);
        if (it != containers.end() && it->second) parent = it->second;
    }
    if (!parent) { err = "interpreter has no global environment"; return false; }
    auto env = std::make_shared<Environment>(parent);
    for (auto& kv : locals) env->define(kv.first, kv.second);
    try {
        out = evaluate(e, env);
        return true;
    } catch (const RinError& ex) {
        err = ex.message;
    } catch (const std::exception& ex) {
        err = ex.what();
    } catch (...) {
        err = "unknown error";
    }
    return false;
}

} // namespace rin
